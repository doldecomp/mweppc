#include "ansi_prefix.Win32.h"
#include <stackall.c>
#ifndef __MWERKS__
/* clang: the stack probe clang's code calls before it allocates a frame of 4K or more, MinGW's
 * _alloca (the size in EAX, ESP lowered by it on return), which is _chkstk. */
__asm__(".globl __alloca\n__alloca:\n\tjmp __chkstk\n");
#endif
