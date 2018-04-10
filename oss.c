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
#define NUM_DIFF_RESOURCES 20
#define MAX_RESOURCE_COUNT 10
#define MAX_NUM_PROCESSES 2
#define NANO_PER_SECOND 1000000000
#define MAX_SPAWN_NANO 100000
#define MAX_INTERNAL_SECONDS 8
#define MAX_NEEDED_RESOURCE 6
#define TIME_INCREMENT 5000
#define MAX_LINES_WRITE 10000

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

	printf("Exiting execution of oss\n");

	return 0;
}

void executeOss()
{
	// populate our resource descriptor array with random resource counts
	initializeResourceDescriptor();	

	*seconds = 0;
	*nanoSeconds = 0;

	int spawnSeconds = 0;
	int spawnNanoSeconds = 0;

	ossLog = fopen("oss.log", "w");

	pcbOccupied = malloc(sizeof(int) * maxNumProcesses);
	blockedQueue = malloc(sizeof(pid_t) * maxNumProcesses);

	mymsg_t requestMsg, clockMsg;
	
	// need to allow access to critical section with one initial message
	clockMsg.mtype = 1;
	if ( msgsnd(msgIdClock, &clockMsg, sizeof(clockMsg), 0) == -1 )
		writeError("Failed to send message to clock access\n", processName);

	while( *seconds <= MAX_INTERNAL_SECONDS )
	{
		spawnProcess( &spawnSeconds, &spawnNanoSeconds );
		
		while ( msgrcv( msgIdRequest, &requestMsg, sizeof(requestMsg), 0, IPC_NOWAIT) > 0 )
		{
			printf("Received request from child %d\n", requestMsg.mtype);
			processResourceRequest(	requestMsg );
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

void processResourceRequest(mymsg_t requestMsg)
{
	pid_t requestingPid = requestMsg.mtype;

	// find this process in our PCB
	int index = findProcessInPcb(requestingPid);

	int resourceIndex = atoi(requestMsg.mtext);

	writeCurrentAllocationToLog();
	
	if ( checkRequest(resourceIndex, requestingPid) == 1 )
	{
		printf("resourceIndex: %d\n", resourceIndex);
		pcb[index].CurrentResource[resourceIndex] = pcb[index].CurrentResource[resourceIndex] + 1;
		resourceDescriptor[resourceIndex].AllocatedResources = resourceDescriptor[resourceIndex].AllocatedResources + 1;
		resourceDescriptor[resourceIndex].AvailableResources = resourceDescriptor[resourceIndex].AvailableResources - 1;

		mymsg_t grantMsg;
		grantMsg.mtype = requestingPid;
	
		if ( msgsnd(msgIdGrant, &grantMsg, sizeof(grantMsg), 0) == -1 )
			writeError("Failed to send grant message to child\n", processName);

		writeCurrentAllocationToLog();
	}
	else
	{
		pcb[index].BlockedResource = resourceIndex;
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
//			if (processNeeds[i][j] < 0)
//			printf("curr: %d max: %d\n", pcb[i].CurrentResource[j], pcb[i].MaxResource[j]);
		}
	}

	// simulate the state if were to give the requested resource
	--tempAvailable[resourceIndex];
	--processNeeds[pcbIndex][resourceIndex];	

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
//							printf("needs: [%d][%d] %d tempAvailable: %d\n", processNum, resourceNum, processNeeds[processNum][resourceNum], tempAvailable[resourceNum]);
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
	if (ossLog != NULL && linesWritten < MAX_LINES_WRITE)
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
	while ((c = getopt(argc, argv, "hn:")) != -1)
	{
		switch (c)
		{
			case 'h':
				printf("oss (second iteration):\nWhen ran (using the option ./oss), \n");
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
}

void deallocateAllMessageQueue()
{
	if (msgIdRequest > 0)
		deallocateMessageQueue(msgIdRequest, processName);

	if (msgIdClock > 0)
		deallocateMessageQueue(msgIdClock, processName);

	if (msgIdGrant > 0)
		deallocateMessageQueue(msgIdGrant, processName);
}

void initializeResourceDescriptor()
{
	int i;
	for (i = 0; i < NUM_DIFF_RESOURCES; ++i)
	{
		resourceDescriptor[i].TotalResources = (rand() % MAX_RESOURCE_COUNT) + 1;
		resourceDescriptor[i].AvailableResources = resourceDescriptor[i].TotalResources;
		resourceDescriptor[i].AllocatedResources = 0;
	} 
}

void handleInterruption(int signo)
{
	if (signo == SIGINT || signo == SIGALRM)
	{
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
