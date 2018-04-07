// oss.c
// CS 4760 Project 5 
// Alex Kane 4/7/2018
// Master executable code - contains most of application logic
#include<unistd.h>
#include<math.h>
#include<signal.h>
#include<stdio.h>
#include<stdlib.h>
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

#define MAX_REAL_SECONDS 2
#define ID_SECONDS 1
#define ID_NANO_SECONDS 2
#define ID_RESOURCE_DESCRIPTOR 3
#define NUM_DIFF_RESOURCES 20

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
struct ResourceDescriptor* resourceDescriptor = NULL;

// shared memory id of resource descriptor
int shmidResourceDescriptor = 0;

// Process name
char* processName = NULL;

void allocateAllSharedMemory();
void deallocateAllSharedMemory();
void handleInterruption(int);

int main(int argc, char** argv)
{
	// Set our global process name
	processName = argv[0];
	
	// set our signal handlers
	signal(SIGINT, handleInterruption);
	signal(SIGALRM, handleInterruption);

	// allocate all our shared memory
	allocateAllSharedMemory();	

	// set an interrupt for our max real run time
	setPeriodic(MAX_REAL_SECONDS);

	sleep (20000);
	// deallocate all shared memory
	deallocateAllSharedMemory();

	return 0;
}

handleInterruption(int signo)
{
	if (signo == SIGINT || signo == SIGALRM)
	{
		deallocateAllSharedMemory();
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
