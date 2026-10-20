#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <globdest.c>
#else
/* clang: globdest.c with the destructor called in C, as its assembler calls it (the object in ECX:
 * __thiscall). clang registers its own static objects' destructors with atexit; the chain is the
 * MWCC runtime's interface. */
#define __NOSTRING__	//	do not include <string>
#include <ansi_parms.h>
#include <stdlib.h>
#include <CPlusLib.h>
#include "NMWException.h"

typedef void (__thiscall *GlobalDestructor)(void *object);

extern void *__register_global_object(void *object,void *destructor,void *regmem)
{
	((DestructorChain *)regmem)->next=__global_destructor_chain;
	((DestructorChain *)regmem)->destructor=destructor;
	((DestructorChain *)regmem)->object=object;
	__global_destructor_chain=(DestructorChain *)regmem;
	return object;
}

DestructorChain  * __global_destructor_chain;

void __destroy_global_chain(void)
{
	DestructorChain	*gdc;

	while((gdc=__global_destructor_chain)!=0L)
	{
		__global_destructor_chain=gdc->next;
		((GlobalDestructor)gdc->destructor)(gdc->object);
	}
}
#endif
