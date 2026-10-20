#include "ansi_prefix.Win32.h"
#include <stackall.c>
#ifndef __MWERKS__
__asm__(".globl __alloca\n__alloca:\n\tjmp __chkstk\n");
#endif
