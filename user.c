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
#include"ProcessControlBlock.h"
#include"ResourceDescriptor.h"

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
#define ACTION_TIME_BOUND 10000
#define ACTION_TYPES 3
#define MAX_PERCENT 100

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

// Message queue id for release reasources
int msgIdRelease = 0;

// Process name
char* processName = NULL;

// Define structure for receiving and sending messages
typedef struct {
	long mtype;
	char mtext[50];
} mymsg_t;

void attachToExistingIPC();
void dettachFromExistingIPC();
void handleTimer(int);

int main(int argc, char** argv)
{
	srand(time(0) * getpid());
	processName = argv[0];
	printf("Beginning execution of child %d\n", getpid());	
	
	// kill this child in case it outlives parent
	setPeriodic(4);
	signal(SIGALRM, handleTimer);

	attachToExistingIPC();

	mainUserLoop();

	dettachFromExistingIPC();	

	return 0;
}

void mainUserLoop()
{
	int pcbIndex = findProcessInPcb( getpid() );
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
		int release = 0;
		if ( msgrcv(msgIdClock, &clockMsg, sizeof(clockMsg), 0, 0) == -1 )
			writeError("Failed to receive message to access critical sections\n", processName);

		if ( (*seconds > actionSeconds) || (*seconds >= actionSeconds && *nanoSeconds >= actionNanoSeconds) )
		{
			int action = rand() % ACTION_TYPES;

			if ( action == 0 || action == 1 )
			{
				resourceIndex = rand() % NUM_DIFF_RESOURCES;
				
				if ( action == 0 )	
				{
					if ( pcb[pcbIndex].CurrentResource[resourceIndex] >= pcb[pcbIndex].MaxResource[resourceIndex] )
						resourceIndex = -1;
				}
				else
				{
					if ( pcb[pcbIndex].CurrentResource[resourceIndex] <= 0 )
					{
						resourceIndex = -1;
					}
					else
					{
						release = 1;
					}
				}
			}
			else
			{
				if (rand() % MAX_PERCENT < 10)
				{
					finishedExecution = resourceIndex = 1;
				}
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
			mymsg_t msgGrant, msgRequest;
			msgRequest.mtype = getpid();
			
			if ( finishedExecution != 1 )
			{
				snprintf(msgRequest.mtext, 50, "%d", resourceIndex);

				if ( release == 1 )
				{
					if ( msgsnd(msgIdRelease, &msgRequest, sizeof(msgRequest), 0) == -1 )
						writeError("Failed to send message release resource\n", processName);
					
					if ( msgrcv(msgIdGrant, &msgGrant, sizeof(msgGrant), getpid(), 0) == -1 )
						writeError("Failed to receive confirmation from oss\n", processName);
				}
				else	
				{
					if ( msgsnd(msgIdRequest, &msgRequest, sizeof(msgRequest), 0) == -1 )
						writeError("Failed to send message requesting resource\n", processName);
					
								
					if ( msgrcv(msgIdGrant, &msgGrant, sizeof(msgGrant), getpid(), 0) == -1 )
						writeError("Failed to receive message granting resource\n", processName);
				}
				
			}
			else
			{
				snprintf(msgRequest.mtext, 50, "END");
				
				if ( msgsnd(msgIdRelease, &msgRequest, sizeof(msgRequest), 0) == -1 )
						writeError("Failed to send message release resource\n", processName);

			}
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
	msgIdRelease = getExistingMessageQueue(ID_MSG_RELEASE, processName);
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

void handleTimer(int signo)
{
	if (signo == SIGALRM)
	{
		kill(getpid(), SIGKILL);
	}
}
