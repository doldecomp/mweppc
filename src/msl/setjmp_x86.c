#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <setjmp.x86.c>
#else
/* clang: MSL's _Setjmp and longjmp in GNU assembler, the same registers in the same jmp_buf slots
 * (EBX, ESI, EDI, ESP, EBP, the return address). MSL's are MWCC inline assembler, whose "JNE 1"
 * (skip the one-byte INC) clang's does not take. */
#include <setjmp.h>

__attribute__((naked)) int _Setjmp(jmp_buf env)
{
	__asm__(
		"popl %ecx\n\t"				/* return address */
		"popl %eax\n\t"				/* env */
		"movl %ebx, 0(%eax)\n\t"
		"movl %esi, 4(%eax)\n\t"
		"movl %edi, 8(%eax)\n\t"
		"movl %esp, 12(%eax)\n\t"
		"movl %ebp, 16(%eax)\n\t"
		"movl %ecx, 20(%eax)\n\t"
		"xorl %eax, %eax\n\t"		/* 0: the return from setjmp itself */
		"pushl %eax\n\t"			/* the caller pops its argument */
		"pushl %ecx\n\t"
		"ret");
}

__attribute__((naked)) void longjmp(jmp_buf env, int value)
{
	__asm__(
		"popl %ecx\n\t"				/* return address */
		"popl %edx\n\t"				/* env */
		"popl %eax\n\t"				/* value */
		"movl 0(%edx), %ebx\n\t"
		"movl 4(%edx), %esi\n\t"
		"movl 8(%edx), %edi\n\t"
		"movl 12(%edx), %esp\n\t"
		"movl 16(%edx), %ebp\n\t"
		"testl %eax, %eax\n\t"		/* longjmp(env, 0) returns 1 */
		"jne 1f\n\t"
		"incl %eax\n"
		"1:\n\t"
		"pushl %eax\n\t"			/* the setjmp caller pops its argument */
		"pushl 20(%edx)\n\t"
		"ret");
}
#endif
