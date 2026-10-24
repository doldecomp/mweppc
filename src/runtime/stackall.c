#include "ansi_prefix.Win32.h"
#include <stackall.c>
#if !defined(__MWERKS__) && !defined(MWCC_LINUX)
__asm__(".globl __alloca\n__alloca:\n\tjmp __chkstk\n");
#endif
