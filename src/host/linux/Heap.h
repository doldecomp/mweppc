#ifndef HOST_LINUX_HEAP_H
#define HOST_LINUX_HEAP_H

void* Heap_Alloc(unsigned int size, int zero);
void* Heap_Realloc(void* p, unsigned int size, int zero);
int Heap_Free(void* p);
void* Heap_MapAligned(unsigned int size, unsigned int alignment);

#endif
