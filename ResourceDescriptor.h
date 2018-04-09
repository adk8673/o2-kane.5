#ifndef RESOURCE_DESCRIPTOR_H
#define RESOURCE_DESCRIPTOR_H

#define MAX_NUM_PROCESSES 18

typedef struct {
	int TotalResources;
	int AvailableResources;
	int AllocatedResources;
} ResourceDescriptor; 

#endif
