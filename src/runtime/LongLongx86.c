#include "ansi_prefix.Win32.h"
#include <LongLongx86.c>
#ifndef __MWERKS__
/* clang: the 64-bit division, remainder and multiplication helpers clang's code calls, each the
 * runtime's own. Microsoft's take their operands as the runtime's do (on the stack, popped by the
 * callee, the result in EDX:EAX); libgcc's (the compiler's MinGW objects) are cdecl. */
__asm__(
	".globl __alldiv\n__alldiv:\n\tjmp __rt_divs64@16\n"
	".globl __aulldiv\n__aulldiv:\n\tjmp __rt_divu64@16\n"
	".globl __allrem\n__allrem:\n\tjmp __rt_mods64@16\n"
	".globl __aullrem\n__aullrem:\n\tjmp __rt_modu64@16\n"
	".globl __allmul\n__allmul:\n\tjmp __rt_mul64@16\n");

long long __divdi3(long long left, long long right)
{
	return _rt_divs64(left, right);
}

unsigned long long __udivdi3(unsigned long long left, unsigned long long right)
{
	return _rt_divu64(left, right);
}

long long __moddi3(long long left, long long right)
{
	return _rt_mods64(left, right);
}

unsigned long long __umoddi3(unsigned long long left, unsigned long long right)
{
	return _rt_modu64(left, right);
}
#endif
