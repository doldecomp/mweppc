#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <newop.cpp>
#else
#include <stdlib.h>

extern void terminate();

void * operator new(size_t size)
{
    if (size == 0) {
        size = 1;
    }
    void * p;
    while ((p = malloc(size)) == NULL) {
        terminate();
    }
    return(p);
}
#endif
