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
#include"PeriodicTimer.h"
//#include"ProcessControlBlock.h"
//#include"ProcessUtilities.h"
#include"ResourceDescriptor.h"
#include"StringUtilities.h"

#define MAX_REAL_SECONDS 2
#define ID_SECONDS 1
#define ID_NANO_SECONDS 2
#define ID_RESOURCE_DESCRIPTOR 3
#define ID_MSG_REQUEST 4
#define ID_MSG_CLOCK 5
#define NUM_DIFF_RESOURCES 20
#define MAX_RESOURCE_COUNT 10
#define MAX_NUM_PROCESSES 1

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

// Message queue ID for sending requests
int msgIdRequest = 0;

// Message queue ID for controlling clock access
int msgIdClock = 0;

// Process name
char* processName = NULL;

// Define structure for receiving and sending messages
typedef struct {
	long mtype;
	char mtext[50];
} mymsg_t;

// maximum number of global processes
int maxNumProcesses = 0;

void allocateAllSharedMemory();
void deallocateAllSharedMemory();
void handleInterruption(int);
void initializeResourceDescriptor();
void allocateAllMessageQueue();
void deallocateAllMessageQueue();
void checkCommandArgs(int, char**);

int main(int argc, char** argv)
{
	// Set our global process name
	processName = argv[0];
	
	// seed our random values
	srand(time(0) * getpid());

	// Intiailize our max number of child processes to default, can be cahnged by command line if passed
	maxNumProcesses = MAX_NUM_PROCESSES;

	// Check what was passed in from the command line
	checkCommandArgs(argc, argv);
		
	// set our signal handlers
	signal(SIGINT, handleInterruption);
	signal(SIGALRM, handleInterruption);

	// allocate all our shared memory
	allocateAllSharedMemory();	

	// allocate message queues
	allocateAllMessageQueue();

	// set an interrupt for our max real run time
	setPeriodic(MAX_REAL_SECONDS);

	// populate our resource descriptor array with random resource counts
	initializeResourceDescriptor();	
	

	// deallocate all shared memory
	deallocateAllSharedMemory();

	// deallocate message queues
	deallocateAllMessageQueue();

	return 0;
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
}

void deallocateAllMessageQueue()
{
	if (msgIdRequest > 0)
		deallocateMessageQueue(msgIdRequest, processName);

	if (msgIdClock > 0)
		deallocateMessageQueue(msgIdClock, processName);
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
		exit(0);
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
}
