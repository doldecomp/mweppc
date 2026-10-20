#ifndef COMPILER_IROMALLOC_H
#define COMPILER_IROMALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void* IRO_malloc(size_t size);
extern void IRO_free(void* buf);
extern void IRO_InitializeAllocator(void);
extern void IRO_TerminateAllocator(void);

#ifdef __cplusplus
}
#endif

#endif
