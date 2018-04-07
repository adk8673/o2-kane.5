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
//#include"ProcessControlBlock.h"
#include"ResourceDescriptor.h"

#define ID_SECONDS 1
#define ID_NANO_SECONDS 2
#define ID_RESOURCE_DESCRIPTOR 3
#define ID_MSG_REQUEST 4
#define ID_MSG_CLOCK 5
#define NUM_DIFF_RESOURCES 20
#define MAX_RESOURCE_COUNT 10
#define MAX_NUM_PROCESSES 1
#define NANO_PER_SECOND 1000000000
#define MAX_SPAWN_NANO 100000
#define MAX_INTERNAL_SECONDS 8

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

void attachToExistingIPC();
void dettachFromExistingIPC();

int main(int argc, char** argv)
{
	processName = argv[0];
	printf("Beginning execution of child %d\n", getpid());	
	
	attachToExistingIPC();

//	dettachFromExistingIPC();	

	return 0;
}

void attachToExistingIPC()
{
	seconds = getExistingSharedMemory(ID_SECONDS, processName);
	nanoSeconds = getExistingSharedMemory(ID_NANO_SECONDS, processName);
	resourceDescriptor = getExistingSharedMemory(ID_RESOURCE_DESCRIPTOR, processName);
	
	msgIdRequest = getExistingMessageQueue(ID_MSG_REQUEST, processName);
	msgIdClock = getExistingMessageQueue(ID_MSG_CLOCK, processName);

}

void dettachFromExistingIPC()
{
	if (seconds != NULL)
	{
		if ( shmdt(seconds) == -1 )
			writeError("Failed to dettach from seconds\n", processName);
	}

	if (nanoSeconds != NULL)
	{
		if ( shmdt(nanoSeconds) == -1 )
			writeError("Failed to dettach from nano seconds\n", processName);
	}
	
	if (resourceDescriptor != NULL)
	{
		if ( shmdt(resourceDescriptor) == -1 )
			writeError("Failed to dettach from resource descriptor\n", processName);
	}
}
