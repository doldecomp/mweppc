#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <globdest.c>
#else
#define __NOSTRING__
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
