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
#include"ProcessControlBlock.h"
#include"ResourceDescriptor.h"

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
#define ACTION_TIME_BOUND 10000
#define ACTION_TYPES 2

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

// Pointer to shared PCB
ProcessControlBlock* pcb = NULL;

// shared memory id of process control block
int shmidPCB = 0;

// Message queue ID for sending requests
int msgIdRequest = 0;

// Message queue ID for controlling clock access
int msgIdClock = 0;

// Message queue ID for granting requests
int msgIdGrant = 0;

// Process name
char* processName = NULL;

// Define structure for receiving and sending messages
typedef struct {
	long mtype;
	char mtext[50];
} mymsg_t;

void attachToExistingIPC();
void dettachFromExistingIPC();

int main(int argc, char** argv)
{
	processName = argv[0];
	printf("Beginning execution of child %d\n", getpid());	
	
	attachToExistingIPC();

	mainUserLoop();

	dettachFromExistingIPC();	

	return 0;
}

void mainUserLoop()
{
	int pcbIndex = findProcessInPcb( getpid() );
printf("PCB index for %d: %d\n", pcbIndex, getpid());
	int nextActionNanoSeconds = rand() % ACTION_TIME_BOUND;

	mymsg_t clockMsg;
	if ( msgrcv(msgIdClock, &clockMsg, sizeof(clockMsg), 0, 0) == -1 )
		writeError("Failed to receive message to access critical sections\n", processName);

	int actionNanoSeconds = *nanoSeconds + nextActionNanoSeconds;
	int actionSeconds = *seconds;	

	if ( actionNanoSeconds >= NANO_PER_SECOND )
	{
		actionNanoSeconds -= NANO_PER_SECOND;
		++actionSeconds;
	}
	
	clockMsg.mtype = 1;
	if ( msgsnd(msgIdClock, &clockMsg, sizeof(clockMsg), 0) == -1 )
		writeError("Failed to send clock message to give access to critical section\n", processName);
	
	int finishedExecution = 0;
	while ( !finishedExecution )
	{
		int resourceIndex = -1;
		if ( msgrcv(msgIdClock, &clockMsg, sizeof(clockMsg), 0, 0) == -1 )
			writeError("Failed to receive message to access critical sections\n", processName);

		if ( (*seconds > actionSeconds) || (*seconds >= actionSeconds && *nanoSeconds >= actionNanoSeconds) )
		{
			int action = rand() % ACTION_TYPES;

			if ( action == 1 || action == 0 )
			{	
				resourceIndex = rand() % NUM_DIFF_RESOURCES;
					
				if ( pcb[pcbIndex].CurrentResource[resourceIndex] >= pcb[pcbIndex].MaxResource[resourceIndex] )
					resourceIndex = -1;

			}

			actionNanoSeconds = *nanoSeconds + nextActionNanoSeconds;
			actionSeconds = *seconds;	

			if ( actionNanoSeconds >= NANO_PER_SECOND )
			{
				actionNanoSeconds -= NANO_PER_SECOND;
				++actionSeconds;
			}
		}
		
		clockMsg.mtype = 1;
		if ( msgsnd(msgIdClock, &clockMsg, sizeof(clockMsg), 0) == -1 )
			writeError("Failed to send clock message to give access to critical section\n", processName);

		if ( resourceIndex != -1 )
		{
			printf("Child %d about to request resource %d\n", getpid(), resourceIndex);
			mymsg_t msgRequest;
			msgRequest.mtype = getpid();
			
			snprintf(msgRequest.mtext, 50, "%d", resourceIndex);
	
			if ( msgsnd(msgIdRequest, &msgRequest, sizeof(msgRequest), 0) == -1 )
				writeError("Failed to send message requesting resource\n", processName);
			mymsg_t msgGrant;
			
			if ( msgrcv(msgIdGrant, &msgGrant, sizeof(msgGrant), getpid(), 0) == -1 )
				writeError("Failed to receive message granting resource\n", processName);
			
			finishedExecution = 1;
		}
	}
}

int findProcessInPcb(pid_t pid)
{
	int index, found;
	for ( index = 0, found = 0; index < MAX_NUM_PROCESSES && !found; ++index)
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

void attachToExistingIPC()
{
	seconds = getExistingSharedMemory(ID_SECONDS, processName);
	nanoSeconds = getExistingSharedMemory(ID_NANO_SECONDS, processName);
	resourceDescriptor = getExistingSharedMemory(ID_RESOURCE_DESCRIPTOR, processName);
	pcb = getExistingSharedMemory(ID_PCB, processName);	

	msgIdRequest = getExistingMessageQueue(ID_MSG_REQUEST, processName);
	msgIdClock = getExistingMessageQueue(ID_MSG_CLOCK, processName);
	msgIdGrant = getExistingMessageQueue(ID_MSG_GRANT, processName);
}

void dettachFromExistingIPC()
{
	if ( seconds != NULL )
	{
		if ( shmdt(seconds) == -1 )
			writeError("Failed to dettach from seconds\n", processName);
	}

	if ( nanoSeconds != NULL )
	{
		if ( shmdt(nanoSeconds) == -1 )
			writeError("Failed to dettach from nano seconds\n", processName);
	}
	
	if ( resourceDescriptor != NULL )
	{
		if ( shmdt(resourceDescriptor) == -1 )
			writeError("Failed to dettach from resource descriptor\n", processName);
	}

	if ( pcb != NULL)
	{
		if ( shmdt(pcb) == -1 )
			writeError("Failed to dettach from pcb\n", processName);
	}
}
