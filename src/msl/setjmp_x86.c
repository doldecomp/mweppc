#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <setjmp.x86.c>
#else
#include <setjmp.h>

__attribute__((naked)) int _Setjmp(jmp_buf env)
{
	__asm__(
		"popl %ecx\n\t"
		"popl %eax\n\t"
		"movl %ebx, 0(%eax)\n\t"
		"movl %esi, 4(%eax)\n\t"
		"movl %edi, 8(%eax)\n\t"
		"movl %esp, 12(%eax)\n\t"
		"movl %ebp, 16(%eax)\n\t"
		"movl %ecx, 20(%eax)\n\t"
		"xorl %eax, %eax\n\t"
		"pushl %eax\n\t"
		"pushl %ecx\n\t"
		"ret");
}

__attribute__((naked)) void longjmp(jmp_buf env, int value)
{
	__asm__(
		"popl %ecx\n\t"
		"popl %edx\n\t"
		"popl %eax\n\t"
		"movl 0(%edx), %ebx\n\t"
		"movl 4(%edx), %esi\n\t"
		"movl 8(%edx), %edi\n\t"
		"movl 12(%edx), %esp\n\t"
		"movl 16(%edx), %ebp\n\t"
		"testl %eax, %eax\n\t"
		"jne 1f\n\t"
		"incl %eax\n"
		"1:\n\t"
		"pushl %eax\n\t"
		"pushl 20(%edx)\n\t"
		"ret");
}
#endif
