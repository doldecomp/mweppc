#include "ansi_prefix.Win32.h"
#ifndef __MWERKS__
/* clang: _alloca is its stack probe (src/runtime/stackall.c) and a builtin of another type; MSL's
 * naked one, which takes the size on the stack, is renamed out of the way (clang expands alloca
 * inline). */
#define _alloca _MSL_alloca
#endif
#include <extras.c>
