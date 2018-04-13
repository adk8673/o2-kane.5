// oss.c
// CS 4760 Project 5 
// Alex Kane 4/7/2018
// Master executable code - contains most of application logic
#include<unistd.h>
#include<math.h>
#include<signal.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/types.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/msg.h>
#include<time.h>
#include"ErrorLogging.h"
#include"IPCUtilities.h"
#include"QueueUtilities.h"
#include"PeriodicTimer.h"
#include"ProcessControlBlock.h"
#include"ProcessUtilities.h"
#include"ResourceDescriptor.h"
#include"StringUtilities.h"

#define MAX_REAL_SECONDS 2
#define ID_SECONDS 1
#define ID_NANO_SECONDS 2
#define ID_RESOURCE_DESCRIPTOR 3
#define ID_MSG_REQUEST 4
#define ID_MSG_CLOCK 5
#define ID_PCB 6
#define ID_MSG_GRANT 7
#define ID_MSG_RELEASE 8
#define NUM_DIFF_RESOURCES 20
#define MAX_RESOURCE_COUNT 10
#define MAX_NUM_PROCESSES 18
#define NANO_PER_SECOND 1000000000
#define MAX_SPAWN_NANO 100000
#define MAX_INTERNAL_SECONDS 8
#define MAX_NEEDED_RESOURCE 6
#define TIME_INCREMENT 5000
#define MAX_LINES_WRITE 10000
#define WRITE_ALLOCATION_COUNT 20

// Global definitions
// Pointer to shared global seconds integer
int* seconds = NULL;

// shared memory id of seconds
int shmidSeconds = 0;

// Pointer to shared memory global nanoseconds integer
int* nanoSeconds = NULL;

// shared memory id of nano seconds
int shmidNanoSeconds = 0;

// Pointer to shared resource descriptor array
ResourceDescriptor* resourceDescriptor = NULL;

// shared memory id of resource descriptor
int shmidResourceDescriptor = 0;

// Pointer to shared process control block
ProcessControlBlock* pcb = NULL;

// shared memory id of PCB
int shmidPCB = 0;

// Message queue ID for sending requests
int msgIdRequest = 0;

// Message queue ID for controlling clock access
int msgIdClock = 0;

// Message queue ID for granting resource requests
int msgIdGrant = 0;

// Message queue ID for releasing resources
int msgIdRelease = 0;

// Process name
char* processName = NULL;

// Define structure for receiving and sending messages
typedef struct {
	long mtype;
	char mtext[50];
} mymsg_t;

// maximum number of global processes
int maxNumProcesses = 0;

// current number of child processes
int currentNumProcesses = 0;

// List of availables PCB slots
int* pcbOccupied = NULL;

// Blocked queue
pid_t* blockedQueue = NULL;

// File
FILE* ossLog = NULL;

// Lines written
int linesWritten = 0;

// Verbose flag
int verboseFlag = 0;

// Total granted requests
int totalGrantedRequests = 0;

// Granted requests up to write max
int grantedRequests = 0;

// Total check counts
int totalResourceChecks = 0;

// Total wait nano seconds
long nanoSecondsWait = 0;

// Total Processes spawned
int totalProcessesCount = 0;

void allocateAllSharedMemory();
void deallocateAllSharedMemory();
void handleInterruption( int );
void initializeResourceDescriptor();
void allocateAllMessageQueue();
void deallocateAllMessageQueue();
void checkCommandArgs( int, char * * );
void executeOss();
void spawnProcess( int *, int * );
void processResourceRequest( mymsg_t );
int findProcessInPcb( pid_t );
void incrementClock( int );
int checkRequest( int, pid_t );
void writeCurrentAllocationToLog();
void printStatistics();

int main(int argc, char** argv)
{
	// Set our global process name
	processName = argv[0];
	
	// seed our random values
	srand(time(0) * getpid());

	printf("Intialize oss\n");

	// Intiailize our max number of child processes to default, can be cahnged by command line if passed
	maxNumProcesses = MAX_NUM_PROCESSES;

	// Check what was passed in from the command line
	checkCommandArgs(argc, argv);
		
	// set our signal handlers
	signal(SIGINT, handleInterruption);
	signal(SIGALRM, handleInterruption);

	// allocate all our shared ipc utilities
	allocateAllSharedMemory();	
	allocateAllMessageQueue();

	// set an interrupt for our max real run time
	setPeriodic(MAX_REAL_SECONDS);

	printf("Begin main execution of oss\n");

	executeOss();	

	int status;
	pid_t childpid;
	while((childpid = wait(&status)) > 0);	

	// deallocate all shared ipc resources
	deallocateAllSharedMemory();
	deallocateAllMessageQueue();

	printStatistics();

	printf("Exiting execution of oss\n");

	return 0;
}

void executeOss()
{

	ossLog = fopen("oss.log", "w");
	
	// populate our resource descriptor array with random resource counts
	initializeResourceDescriptor();	

	*seconds = 0;
	*nanoSeconds = 0;

	int spawnSeconds = 0;
	int spawnNanoSeconds = 0;

	pcbOccupied = malloc(sizeof(int) * maxNumProcesses);
	blockedQueue = malloc(sizeof(pid_t) * maxNumProcesses);
	initializeQueue(blockedQueue, MAX_NUM_PROCESSES);

	mymsg_t requestMsg, clockMsg, releaseMsg;
	
	// need to allow access to critical section with one initial message
	clockMsg.mtype = 1;
	if ( msgsnd(msgIdClock, &clockMsg, sizeof(clockMsg), 0) == -1 )
		writeError("Failed to send message to clock access\n", processName);

	while( *seconds <= MAX_INTERNAL_SECONDS )
	{
		spawnProcess( &spawnSeconds, &spawnNanoSeconds );

		checkQueue();
		
		while ( msgrcv( msgIdRequest, &requestMsg, sizeof(requestMsg), 0, IPC_NOWAIT) > 0 )
		{
			printf("Received request from child %d\n", requestMsg.mtype);
			
			if ( strcmp(requestMsg.mtext, "END") == 0 )
			{
				// if a child finished, make sure to wait on it or it won't be fully removed
				int status;
				waitpid(requestMsg.mtype, &status, 0); 
				
				// Next - free up it's resources since it's over
				int index = findProcessInPcb(requestMsg.mtype);
				
				int i;
				for ( i = 0; i < NUM_DIFF_RESOURCES; ++i )
				{
					if ( pcb[index].CurrentResource > 0 )
					{
						resourceDescriptor[i].AvailableResources += pcb[index].CurrentResource[i];
						pcb[index].CurrentResource[i] = 0;
						pcb[index].MaxResource[i] = 0;
					}
				}
			
				nanoSecondsWait += (((*seconds) * NANO_PER_SECOND) + *nanoSeconds) - ((pcb[index].BlockedAtSeconds * NANO_PER_SECOND) + pcb[index].BlockedAtNanoSeconds);
	
				pcb[index].ProcessId = 0;
				pcb[index].BlockedResource = 0;
				pcbOccupied[index] = 0;

			}
			else
			{
				processResourceRequest(	requestMsg );
			}
		}

		while ( msgrcv( msgIdRelease, &releaseMsg, sizeof(releaseMsg), 0, IPC_NOWAIT) > 0 )
		{
			int resourceIndex = atoi(releaseMsg.mtext);
			printf("Child %d wants to release resource %d\n", releaseMsg.mtype, resourceIndex);

			int index = findProcessInPcb(releaseMsg.mtype);
			if ( pcb[index].CurrentResource[resourceIndex] > 0 )
			{
				if ( ossLog != NULL && linesWritten < MAX_LINES_WRITE)
				{
					fprintf(ossLog, "Child %d about to release resource R%d at %d:%d\n", releaseMsg.mtype, resourceIndex, *seconds, *nanoSeconds);
					++linesWritten;
				}

				resourceDescriptor[resourceIndex].AvailableResources += 1;
				pcb[index].CurrentResource[resourceIndex] -= 1;

				writeCurrentAllocationToLog();	
			}

			mymsg_t grantMsg;
			grantMsg.mtype = releaseMsg.mtype;
			if ( msgsnd(msgIdGrant, &grantMsg, sizeof(grantMsg), 0) == -1 )
				writeError("Failed to send confirmation to child\n", processName);
		}

		int increment = rand() % TIME_INCREMENT;
		
		if ( msgrcv(msgIdClock, &clockMsg, sizeof(clockMsg), 0, 0) == -1 )
			writeError("Failed to receive clock message\n", processName);

		incrementClock(increment);
		clockMsg.mtype = 1;

		if ( msgsnd(msgIdClock, &clockMsg, sizeof(clockMsg), 0) == -1 )
			writeError("Failed to send clock message\m", processName);

	}
	
	if ( pcbOccupied != NULL )
		free(pcbOccupied);

	if ( blockedQueue != NULL )
		free(blockedQueue);
	
	if ( ossLog != NULL )
		fclose(ossLog);
}

void printStatistics()
{
	printf("Statistics:\nTotal Resource Checks: %d\nTotal Granted Resouces: %d\nPercentage granted: %lf\nTotal Waiting time: %d\nAverage waiting time: %d\n"
	,totalResourceChecks
	,totalGrantedRequests
	,((double)totalGrantedRequests/(double)totalResourceChecks) * (double)100
	,nanoSecondsWait
	,nanoSecondsWait / totalProcessesCount);
}

void checkQueue()
{
	pid_t* tempQueue = malloc(sizeof(pid_t) * maxNumProcesses);
	pid_t blockedPid = dequeueValue(blockedQueue, maxNumProcesses);

	++totalResourceChecks;

	initializeQueue(tempQueue, MAX_NUM_PROCESSES);

	while ( blockedPid != 0 )
	{
		int index = findProcessInPcb(blockedPid);
		int resourceIndex = pcb[index].BlockedResource;

		if ( checkRequest(resourceIndex, blockedPid) == 1 )
		{
			pcb[index].CurrentResource[resourceIndex] = pcb[index].CurrentResource[resourceIndex] + 1;
			resourceDescriptor[resourceIndex].AllocatedResources = resourceDescriptor[resourceIndex].AllocatedResources + 1;
			resourceDescriptor[resourceIndex].AvailableResources = resourceDescriptor[resourceIndex].AvailableResources - 1;

			mymsg_t grantMsg;
			grantMsg.mtype = blockedPid;
	
			if ( msgsnd(msgIdGrant, &grantMsg, sizeof(grantMsg), 0) == -1 )
				writeError("Failed to send grant message to child\n", processName);
			
			if ( ossLog != NULL && linesWritten < MAX_LINES_WRITE )
			{
				fprintf(ossLog, "Child %d was removed from blocked queue at %d:%d after waiting for resource %d\n", blockedPid, *seconds, *nanoSeconds, resourceIndex);
				++linesWritten;
			}

			++totalGrantedRequests;
			++grantedRequests;
		
			nanoSecondsWait += (((*seconds) * NANO_PER_SECOND) + *nanoSeconds) - ((pcb[index].BlockedAtSeconds * NANO_PER_SECOND) + pcb[index].BlockedAtNanoSeconds);
			pcb[index].BlockedResource = 0;
			pcb[index].BlockedAtNanoSeconds = 0;
			pcb[index].BlockedAtSeconds = 0;
	
			if ( grantedRequests == 20 )
			{
				writeCurrentAllocationToLog();
				grantedRequests = 0;
			}
			
			int timeIncrement = rand() % TIME_INCREMENT;
			incrementClock(timeIncrement);
		}
		else 
		{
			enqueueValue(tempQueue, blockedPid, maxNumProcesses);
		}
		
		blockedPid = dequeueValue(blockedQueue, maxNumProcesses);	
	}

	blockedPid = dequeueValue(tempQueue, maxNumProcesses);
	while ( blockedPid != 0 )
	{
		enqueueValue(blockedQueue, blockedPid, maxNumProcesses);
		blockedPid = dequeueValue(tempQueue, maxNumProcesses);
	}

	free(tempQueue);
	
}

void processResourceRequest(mymsg_t requestMsg)
{
	pid_t requestingPid = requestMsg.mtype;

	// find this process in our PCB
	int index = findProcessInPcb(requestingPid);

	int resourceIndex = atoi(requestMsg.mtext);

	if ( checkRequest(resourceIndex, requestingPid) == 1 )
	{
		pcb[index].CurrentResource[resourceIndex] = pcb[index].CurrentResource[resourceIndex] + 1;
		resourceDescriptor[resourceIndex].AllocatedResources = resourceDescriptor[resourceIndex].AllocatedResources + 1;
		resourceDescriptor[resourceIndex].AvailableResources = resourceDescriptor[resourceIndex].AvailableResources - 1;

		if ( ossLog != NULL && linesWritten < MAX_LINES_WRITE )
		{
			fprintf(ossLog, "Child %d was granted resource %d at %d:%d\n", pcb[index].ProcessId, resourceIndex, *seconds, *nanoSeconds);
			++linesWritten;
		}

		mymsg_t grantMsg;
		grantMsg.mtype = requestingPid;
	
		if ( msgsnd(msgIdGrant, &grantMsg, sizeof(grantMsg), 0) == -1 )
			writeError("Failed to send grant message to child\n", processName);
		
		++grantedRequests;
		++totalGrantedRequests;

		if ( grantedRequests == 20 )
		{
			writeCurrentAllocationToLog();
			grantedRequests = 0;
		}
	}
	else
	{
		if ( ossLog != NULL && linesWritten < MAX_LINES_WRITE )
		{
			fprintf(ossLog, "Child %d was blocked on resource R%d at %d:%d\n", pcb[index].ProcessId, resourceIndex, *seconds, *nanoSeconds);
			++linesWritten;
		}
		
		printf("Child %d became blocked on resource R%d\n", requestingPid, resourceIndex);
		pcb[index].BlockedResource = resourceIndex;
		pcb[index].BlockedAtSeconds = *seconds;
		pcb[index].BlockedAtNanoSeconds = *nanoSeconds;
		enqueueValue(blockedQueue, requestingPid, maxNumProcesses);	
	}
}

int checkRequest(int resourceIndex, pid_t requestingPid)
{
	int pcbIndex = findProcessInPcb(requestingPid);

	int tempAvailable[NUM_DIFF_RESOURCES];
	int i;
	for ( i = 0; i < NUM_DIFF_RESOURCES; ++i)
	{
		tempAvailable[i] = resourceDescriptor[i].AvailableResources;
	}

	// Matrix of current needs - instead of using dynamic max array value, we need to use the constant
	// of MAX_NUM_PROCESSES so we can declare a two dimensional array
	int processNeeds[MAX_NUM_PROCESSES][NUM_DIFF_RESOURCES];

	for ( i = 0; i < maxNumProcesses; ++i )
	{
		int j;
		for ( j = 0; j < NUM_DIFF_RESOURCES; ++j )
		{
			processNeeds[i][j] = pcb[i].MaxResource[j] - pcb[i].CurrentResource[j];
		}
	}

	// simulate the state if were to give the requested resource
	tempAvailable[resourceIndex] = tempAvailable[resourceIndex] - 1;
	processNeeds[pcbIndex][resourceIndex] = processNeeds[pcbIndex][resourceIndex] - 1;	

	int* processFinish = malloc(sizeof(int) * maxNumProcesses);

	for (i = 0; i < maxNumProcesses; ++i)
		processFinish[i] = 0; 

	// If we don't have enough resources to grant this request, we don't even need to check if we are 
	// in a safe state
	int grantRequest = 1;
	if ( resourceDescriptor[resourceIndex].AvailableResources > 0 )
	{
			// outer loop to check for finished processes
			int finishedCount = 0, foundSafe;
			do
			{
				foundSafe = 0;

				// iterate over each process
				int processNum;
				for ( processNum = 0; processNum < maxNumProcesses; ++processNum )
				{
					// we don't need to check processes that have already finished
					if ( processFinish[processNum] == 0 )
					{
						int resourceNum, hasEnoughResources = 1;
						for ( resourceNum = 0; resourceNum < NUM_DIFF_RESOURCES && hasEnoughResources == 1; ++resourceNum )
						{
							if ( processNeeds[processNum][resourceNum] > tempAvailable[resourceNum] )
								hasEnoughResources = 0;
						}

						// we can finish this process
						if ( hasEnoughResources == 1 )
						{
							processFinish[processNum] = 1;
							foundSafe = 1;
		
							// free up all the resources for this process
							for ( resourceNum = 0; resourceNum < NUM_DIFF_RESOURCES; ++resourceNum)
								tempAvailable[resourceNum] += pcb[processNum].CurrentResource[resourceNum];
							
							++finishedCount;
						}
					}
				}
				
				if ( foundSafe == 0 )
					grantRequest = 0;
			} while ( finishedCount < maxNumProcesses && foundSafe );	
	}
	
	if ( processFinish != NULL )
		free (processFinish);
	
	return grantRequest;
}

void writeCurrentAllocationToLog()
{
	if (ossLog != NULL 
		&& linesWritten < MAX_LINES_WRITE
		&& verboseFlag == 1)
	{
		fprintf(ossLog, "Total resource allocation: \n");
		++linesWritten;
		
		int i;
		fprintf(ossLog, "\t");
		for ( i = 0; i < NUM_DIFF_RESOURCES; ++i )
		{
			fprintf(ossLog, "R%d\t", i); 	
		}

		fprintf(ossLog, "\n");
		++linesWritten;

		for ( i = 0; i < maxNumProcesses; ++i)
		{
			if (pcb[i].ProcessId != 0)
			{
				fprintf(ossLog, "P%d\t", i);
				int j;
				for ( j = 0; j < NUM_DIFF_RESOURCES; ++j)
				{
					fprintf(ossLog, "%d\t", pcb[i].CurrentResource[j]);
				}

				fprintf(ossLog, "\n");
				++linesWritten;
			}
		}
	}
}

void incrementClock(int incrementNanoSeconds)
{
	*nanoSeconds += incrementNanoSeconds;

	if (*nanoSeconds >= NANO_PER_SECOND)
	{
		*seconds = *seconds + 1;
		*nanoSeconds -= NANO_PER_SECOND;
	}
}

int findProcessInPcb(pid_t pid)
{
	int index, found;
	for ( index = 0, found = 0; index < maxNumProcesses && !found; ++index)
	{
		if ( pcb[index].ProcessId == pid )
		{
			found = 1;
			break;
		}
	}

	if ( !found )
		index = -1;
	
	return index;
}

void spawnProcess(int* spawnSeconds, int* spawnNanoSeconds)
{
	// We need to make sure that is both time to spawn a new processes and there isn't already too 
	// many process spawned
	
	if ( (*seconds > *spawnSeconds || (*seconds >= *seconds && *nanoSeconds >= *spawnNanoSeconds))
		&&  currentNumProcesses < maxNumProcesses) 	
	{
		int index, found;
		for ( index = 0, found = 0; index < maxNumProcesses && !found; ++index )
		{
			if (pcbOccupied[index] == 0)
			{
				found = 1;
				break;
			}
		}
		
		// If we can't find a spot, don't spawn
		if ( !found )
			return;
		
		pid_t newChild = createChildProcess("./user", processName);
		
		++totalProcessesCount;
		pcbOccupied[index] = 1;
		pcb[index].ProcessId = newChild;
		
		int i;
		for ( i = 0; i < NUM_DIFF_RESOURCES; ++i )
		{
			int maxNeeded = MAX_NEEDED_RESOURCE;
			if ( resourceDescriptor[i].TotalResources < maxNeeded)
				maxNeeded = resourceDescriptor[i].TotalResources;

			pcb[index].CurrentResource[i] = 0;
			pcb[index].MaxResource[i] = rand() % maxNeeded;
		}		
		
		++currentNumProcesses;

		// schedule next spawn time
		*spawnNanoSeconds = *nanoSeconds + (rand() % MAX_SPAWN_NANO);

		if (*spawnNanoSeconds >= NANO_PER_SECOND)
		{
			*spawnSeconds = *seconds + 1;
			*spawnNanoSeconds -= NANO_PER_SECOND;
		}
		else
		{
			*spawnSeconds = *seconds;
		}

		printf("Spawned child %d\nMax Resource requirements ", newChild);
		for (i = 0; i < NUM_DIFF_RESOURCES; ++i)
			printf("%d: %d ", i, pcb[index].MaxResource[i]);
		printf("\n");
	}
}

// Check our arguments passed from the command line.  In this case, since we are only accepting the
// -h option from the command line, we only need to return 1 int which indicates if a the help 
// argument was passed.
void checkCommandArgs(int argc, char** argv)
{
	int c;
	while ((c = getopt(argc, argv, "vhn:")) != -1)
	{
		switch (c)
		{
			case 'h':
				printf("oss (second iteration):\nWhen ran (using the option ./oss), simulates the management of resource allocation of several children by a master process \nThe following options may be used:\n-h\tDisplay help\n-n\tNumber of processes to spawn at max (cannot be greater than 18)\n-v\tVerbose log setting\n");
				exit(0);
				break;	
			case 'n':
				if (optarg != NULL && checkNumber(optarg))
				{
					maxNumProcesses = atoi(optarg);
					if (maxNumProcesses > MAX_NUM_PROCESSES)
						printf("Argument exceeed max number of child processes, using default of %d\n", MAX_NUM_PROCESSES);
				}
				else
					printf("Invalid number of max children, will use default of %d\n", MAX_NUM_PROCESSES);
				break;
			case 'v':
				verboseFlag = 1;
			default:
				break;
		}
	}
}


void allocateAllMessageQueue()
{
	msgIdRequest = allocateMessageQueue(ID_MSG_REQUEST, processName);
	msgIdClock = allocateMessageQueue(ID_MSG_CLOCK, processName);
	msgIdGrant = allocateMessageQueue(ID_MSG_GRANT, processName);
	msgIdRelease = allocateMessageQueue(ID_MSG_RELEASE, processName);
}

void deallocateAllMessageQueue()
{
	if ( msgIdRequest > 0 )
		deallocateMessageQueue(msgIdRequest, processName);

	if ( msgIdClock > 0 )
		deallocateMessageQueue(msgIdClock, processName);

	if ( msgIdGrant > 0 )
		deallocateMessageQueue(msgIdGrant, processName);

	if ( msgIdRelease > 0 )
		deallocateMessageQueue(msgIdRelease, processName);
}

void initializeResourceDescriptor()
{
	if ( ossLog != NULL && linesWritten < MAX_LINES_WRITE )
	{
		fprintf(ossLog, "Intial Resources\n");
		++linesWritten;
		
		fprintf(ossLog, "\t");
		int i;
		for ( i = 0; i < NUM_DIFF_RESOURCES; ++i )
		{
			fprintf(ossLog, "R%d\t", i);
		}

		fprintf(ossLog, "\n");
		++linesWritten;
	}

	if ( ossLog != NULL && linesWritten < MAX_LINES_WRITE )
	{
		fprintf( ossLog, "Num:\t");
	}

	int i;
	for ( i = 0; i < NUM_DIFF_RESOURCES; ++i)
	{
		resourceDescriptor[i].TotalResources = (rand() % MAX_RESOURCE_COUNT) + 1;
		resourceDescriptor[i].AvailableResources = resourceDescriptor[i].TotalResources;
		resourceDescriptor[i].AllocatedResources = 0;

		if ( ossLog != NULL && linesWritten < MAX_LINES_WRITE )
		{
			fprintf(ossLog, "%d\t", resourceDescriptor[i].TotalResources);
		}
	} 

	if ( ossLog != NULL && linesWritten < MAX_LINES_WRITE)
	{
		fprintf(ossLog, "\n");
		+linesWritten;
	}
}

void handleInterruption(int signo)
{
	if (signo == SIGINT || signo == SIGALRM)
	{

		int i;
		for ( i = 0; i < maxNumProcesses; ++i )
		{
			if ( pcb[i].ProcessId != 0 && pcb[i].BlockedResource )
				nanoSecondsWait += (((*seconds) * NANO_PER_SECOND) + *nanoSeconds) - ((pcb[i].BlockedAtSeconds * NANO_PER_SECOND) + pcb[i].BlockedAtNanoSeconds);
		}
		
		printStatistics();

		deallocateAllSharedMemory();
		deallocateAllMessageQueue();
	
		if ( pcbOccupied != NULL )
			free(pcbOccupied);

		if ( ossLog != NULL )
			fclose(ossLog);

		kill(0, SIGKILL);
	}
}

void allocateAllSharedMemory()
{
	// allocate and attach to seconds
	shmidSeconds = allocateSharedMemory(ID_SECONDS, sizeof(int), processName);	
	seconds = shmat(shmidSeconds, 0, 0);	

	// allocate and attach to nano seconds
	shmidNanoSeconds = allocateSharedMemory(ID_NANO_SECONDS, sizeof(int), processName);
	nanoSeconds = shmat(shmidNanoSeconds, 0, 0);

	// allocate and attach to resource desctipro array
	shmidResourceDescriptor = allocateSharedMemory(ID_RESOURCE_DESCRIPTOR, sizeof(ResourceDescriptor) * NUM_DIFF_RESOURCES, processName);
	resourceDescriptor = shmat(shmidResourceDescriptor, 0, 0);

	// allocate and attach to pcb
	shmidPCB = allocateSharedMemory(ID_PCB, sizeof(ProcessControlBlock) * maxNumProcesses, processName);
	pcb = shmat(shmidPCB, 0, 0);
}

void deallocateAllSharedMemory()
{
	// Dettach and deallocate seconds
	if ( seconds != NULL )
	{
		if ( shmdt(seconds) == -1 )
			writeError("Failed to deattach from shared seconds\n", processName);	
	}

	if ( shmidSeconds > 0 )
	{
		deallocateSharedMemory(shmidSeconds, processName);
	}

	if ( nanoSeconds != NULL )
	{
		if ( shmdt(nanoSeconds) == -1 )
			writeError("Failed to dettach from shared nanoSeconds\n", processName);
	}

	if ( shmidNanoSeconds > 0)
	{
		deallocateSharedMemory(shmidNanoSeconds, processName);
	}

	if ( resourceDescriptor != NULL )
	{
		if ( shmdt(resourceDescriptor) == -1 )
			writeError("Failed to dettach from shared resource descriptor\n", processName);
	}

	if ( shmidResourceDescriptor > 0 )
	{
		deallocateSharedMemory(shmidResourceDescriptor, processName);
	}

	if ( pcb != NULL )
	{
		if ( shmdt(pcb) == -1)
			writeError("Failed to dettach from shared pcb\n", processName);
	}

	if ( shmidPCB > 0 )
	{
		deallocateSharedMemory(shmidPCB, processName);
	}

}
