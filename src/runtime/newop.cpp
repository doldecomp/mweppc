#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <newop.cpp>
#else
/* clang: newop.cpp's operator new, in a build without exceptions. When malloc fails, newop.cpp's
 * calls the new handler, NewHand.cpp's default one, which throws bad_alloc; nothing in the compiler
 * catches it, so the throw ends in terminate(), which this calls. */
#include <stdlib.h>

extern void terminate();

void * operator new(size_t size)
{
    // If the user does a new 0 do not try to allocate 0 or we will crash.
    // I should still return a real pointer so I am allocating 1.
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
