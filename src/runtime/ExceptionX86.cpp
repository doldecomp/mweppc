#ifdef __MWERKS__
#define ExX86_CurrentAction ExceptionX86_ExX86_CurrentAction
#define ExX86_DeletePointer ExceptionX86_ExX86_DeletePointer
#define ExX86_DeletePointerCond ExceptionX86_ExX86_DeletePointerCond
#define ExX86_DestroyLocal ExceptionX86_ExX86_DestroyLocal
#define ExX86_DestroyLocalArray ExceptionX86_ExX86_DestroyLocalArray
#define ExX86_DestroyLocalCond ExceptionX86_ExX86_DestroyLocalCond
#define ExX86_DestroyLocalPointer ExceptionX86_ExX86_DestroyLocalPointer
#define ExX86_DestroyMember ExceptionX86_ExX86_DestroyMember
#define ExX86_DestroyMemberArray ExceptionX86_ExX86_DestroyMemberArray
#define ExX86_DestroyMemberCond ExceptionX86_ExX86_DestroyMemberCond
#define ExX86_DestroyPartialArray ExceptionX86_ExX86_DestroyPartialArray
#define ExX86_GetValue ExceptionX86_ExX86_GetValue
#define ExX86_HandleUnexpected ExceptionX86_ExX86_HandleUnexpected
#define ExX86_IsInSpecification ExceptionX86_ExX86_IsInSpecification
#define ExX86_LongJump ExceptionX86_ExX86_LongJump
#define ExX86_NextAction ExceptionX86_ExX86_NextAction
#define ExX86_PopStackFrame ExceptionX86_ExX86_PopStackFrame
#define ExX86_ThrowHandler ExceptionX86_ExX86_ThrowHandler
#define ExX86_UnwindStack ExceptionX86_ExX86_UnwindStack
#include "ansi_prefix.Win32.h"

#pragma exceptions on

#define __NOSTRING__

#include <stdlib.h>
#include "CPlusLib.h"
#include <exception.h>
#include "exceptionx86.h"
#include "NMWException.h"

#define EXPORTALLPCOFFSETS		0

#define REG_EBX 	3
#define REG_ESI		6
#define REG_EDI		7

#if 1
#define INLINE			static
#else
#define INLINE			inline
#endif

typedef struct ThrowContext {
	long		EBX, ESI, EDI;
	char 		*EBP;
	char		*returnaddr;
	char		*throwtype;
	void		*location;
	void		*dtor;
	CatchInfo	*catchinfo;
}	ThrowContext;

typedef struct ExceptionInfo {
	ExceptionRecord	*exception_record;
	char			*current_function;
	char			*action_pointer;
}	ExceptionInfo;

typedef struct ActionIterator {
	ExceptionInfo	info;
	char			*current_bp;
}	ActionIterator;

extern "C" ExceptionTableHeader *FirstExceptionTable = 0;
extern "C" void ExX86_FindExceptionRecord(char *returnaddr, ExceptionInfo *info);

static exaction_type ExX86_CurrentAction(const ActionIterator *iter)
{
	if(iter->info.action_pointer==0) return EXACTION_ENDOFLIST;
	return ((ex_destroylocal *)iter->info.action_pointer)->action&EXACTION_MASK;
}

static exaction_type ExX86_NextAction(ActionIterator *iter)
{
	exaction_type	action;

	for(;;)
	{
		if(	iter->info.action_pointer==0
		||	((action=((ex_destroylocal *)iter->info.action_pointer)->action)&EXACTION_ENDBIT)!=0 )
		{
			char *return_addr;

			return_addr=*(char **)(iter->current_bp+4);
			ExX86_FindExceptionRecord(return_addr,&iter->info);
			if(iter->info.exception_record==0) terminate();
			iter->current_bp=*(char **)iter->current_bp;
			if(iter->info.action_pointer==0) continue;
		}
		else
		{
			switch(action)
			{
			case EXACTION_DESTROYLOCAL:
				iter->info.action_pointer+=sizeof(ex_destroylocal); break;
			case EXACTION_DESTROYLOCALCOND:
				iter->info.action_pointer+=sizeof(ex_destroylocalcond); break;
			case EXACTION_DESTROYLOCALPOINTER:
				iter->info.action_pointer+=sizeof(ex_destroylocalpointer); break;
			case EXACTION_DESTROYLOCALARRAY:
				iter->info.action_pointer+=sizeof(ex_destroylocalarray); break;
			case EXACTION_DESTROYPARTIALARRAY:
				iter->info.action_pointer+=sizeof(ex_destroypartialarray); break;
			case EXACTION_DESTROYMEMBER:
			case EXACTION_DESTROYBASE:
				iter->info.action_pointer+=sizeof(ex_destroymember); break;
			case EXACTION_DESTROYMEMBERCOND:
				iter->info.action_pointer+=sizeof(ex_destroymembercond); break;
			case EXACTION_DESTROYMEMBERARRAY:
				iter->info.action_pointer+=sizeof(ex_destroymemberarray); break;
			case EXACTION_DELETEPOINTER:
				iter->info.action_pointer+=sizeof(ex_deletepointer); break;
			case EXACTION_DELETEPOINTERCOND:
				iter->info.action_pointer+=sizeof(ex_deletepointercond); break;
			case EXACTION_CATCHBLOCK:
				iter->info.action_pointer+=sizeof(ex_catchblock); break;
			case EXACTION_ACTIVECATCHBLOCK:
				iter->info.action_pointer+=sizeof(ex_activecatchblock); break;
			case EXACTION_SPECIFICATION:
				iter->info.action_pointer+=
					sizeof(ex_specification)
				+	((ex_specification *)iter->info.action_pointer)->specs*sizeof(void *);
				break;

			default:
				terminate();
			}
		}
		return ((ex_destroylocal *)iter->info.action_pointer)->action&EXACTION_MASK;
	}
}

static char *ExX86_PopStackFrame(ThrowContext *context,ExceptionInfo *info)
{
    long *frame;
    char mask;
    char *returnaddr;

    frame = (long *)context->EBP;

    returnaddr = (char *)frame[1];

    context->EBP = (char *)*frame;

    mask = info->exception_record->register_mask;
	if (mask & (1<<REG_EBX))
	{
	    context->EBX = *(--frame);
	}
	if (mask & (1<<REG_ESI))
	{
	    context->ESI = *(--frame);
	}
	if (mask & (1<<REG_EDI))
	{
	    context->EDI = *(--frame);
	}

	return returnaddr;
}

static long ExX86_GetValue(ThrowContext *context,long cond,short size)
{
	char	*ptr;

	if((cond&EXLREFREGMASK)==EXLREFREGBITS)
	{
		cond&=~EXLREFREGMASK;
		switch(cond)
		{
		case 3:
		    ptr=(char *)&(context->EBX);
		    break;
		case 6:
		    ptr=(char *)&(context->ESI);
		    break;
		case 7:
		    ptr=(char *)&(context->EDI);
		    break;
		default:
		    terminate();
		}
		switch(size)
		{
		default:	return *(char *)(ptr);
		case 2:		return *(short *)(ptr);
		case 4:		return *(long *)ptr;
		}
	}
	else
	{
		ptr=context->EBP+cond;
		switch(size)
		{
		default:	return *(char *)ptr;
		case 2:		return *(short *)ptr;
		case 4:		return *(long *)ptr;
		}
	}
}

INLINE void ExX86_DestroyLocal(ThrowContext *context,const ex_destroylocal *ex)
{
    asm
    {
      		mov		ecx, context
      		mov		ecx, ThrowContext.EBP[ecx]
      		mov		eax, ex
      		add		ecx, ex_destroylocal.local[eax]
      		call	ex_destroylocal.dtor[eax]
    }
}

INLINE void ExX86_DestroyLocalCond(ThrowContext *context,const ex_destroylocalcond *ex)
{
	if(ExX86_GetValue(context,ex->cond,sizeof(local_cond_type)))
	{
	    asm
	    {
	      		mov		ecx, context
	      		mov		ecx, ThrowContext.EBP[ecx]
	      		mov		eax, ex
	      		add		ecx, ex_destroylocalcond.local[eax]
	      		call	ex_destroylocalcond.dtor[eax]
	    }
	}
}

INLINE void ExX86_DestroyLocalPointer(ThrowContext *context,const ex_destroylocalpointer *ex)
{
    void *location;

    location = (void *)ExX86_GetValue(context, ex->pointer, sizeof(void *));
    asm
    {
	        	mov		ecx, location;
	      		mov		eax, ex
	      		call	ex_destroylocalpointer.dtor[eax]
    }
}

INLINE void ExX86_DestroyLocalArray(ThrowContext *context,const ex_destroylocalarray *ex)
{
	char					*pt;
	ConstructorDestructor	dtor;
	long					n,size;

	pt		= context->EBP+ex->localarray;
	dtor	= (ConstructorDestructor)ex->dtor;
	n		= ex->elements;
	size	= ex->element_size;

	for(pt=pt+size*n; n>0; n--)
	{
	    pt -= size;
		asm
		{
		    	mov			ecx, pt
		    	call		dtor
		}
	}
}

INLINE void ExX86_DestroyPartialArray(ThrowContext *context,const ex_destroypartialarray *ex)
{
	char					*pt;
	ConstructorDestructor	dtor;
	long					n,size;

	pt		= (char *)ExX86_GetValue(context,ex->arraypointer,sizeof(void *));
	n		= ExX86_GetValue(context,ex->arraycounter,sizeof(long));
	dtor	= (ConstructorDestructor)ExX86_GetValue(context,ex->dtor,sizeof(void *));
	size	= ExX86_GetValue(context,ex->element_size,sizeof(long));

	for(pt=pt+size*n; n>0; n--)
	{
	    pt -= size;
		asm
		{
		    	mov			ecx, pt
		    	call		dtor
		}
	}
}

INLINE void ExX86_DestroyMember(ThrowContext *context,const ex_destroymember *ex, int dtor_arg)
{
    void *location;

    location =(char *)ExX86_GetValue(context,ex->objectptr,sizeof(void *))+ex->offset;
    asm
    {
	        	mov		ecx, location;
	      		mov		eax, ex
	      		call	ex_destroymember.dtor[eax]
    }
}

INLINE void ExX86_DestroyMemberCond(ThrowContext *context,const ex_destroymembercond *ex)
{
    void *location;
	if(ExX86_GetValue(context,ex->cond,sizeof(vbase_ctor_arg_type)))
	{
	    location = (char *)ExX86_GetValue(context,ex->objectptr,sizeof(void *))+ex->offset;
	    asm
	    {
		        	mov		ecx, location;
		      		mov		eax, ex
		      		call	ex_destroymembercond.dtor[eax]
	    }
	}
}

INLINE void ExX86_DestroyMemberArray(ThrowContext *context,const ex_destroymemberarray *ex)
{
	char					*pt;
	ConstructorDestructor	dtor;
	long					n,size;

	pt		= (char *)ExX86_GetValue(context,ex->objectptr,sizeof(void *))+ex->offset;
	dtor	= (ConstructorDestructor)ex->dtor;
	n		= ex->elements;
	size	= ex->element_size;

	for(pt=pt+size*n; n>0; n--)
	{
	    pt -= size;
		asm
		{
		    	mov			ecx, pt
		    	call		dtor
		}
	}
}

INLINE void ExX86_DeletePointer(ThrowContext *context,const ex_deletepointer *ex)
{
	((void (*)(void *))ex->deletefunc)((void *)ExX86_GetValue(context,ex->pointerobject,sizeof(void *)));
}

INLINE void ExX86_DeletePointerCond(ThrowContext *context,const ex_deletepointercond *ex)
{
	if(ExX86_GetValue(context,ex->cond,sizeof(local_cond_type)))
	{
		((void (*)(void *))ex->deletefunc)((void *)ExX86_GetValue(context,ex->pointerobject,sizeof(void *)));
	}
}

static void ExX86_UnwindStack(ThrowContext *context,ExceptionInfo *info, void *catcher)
{
	exaction_type	action;

#pragma exception_terminate

	for(;;)
	{
		if(info->action_pointer==0)
		{
			char *return_addr;

			return_addr=ExX86_PopStackFrame(context,info);
			ExX86_FindExceptionRecord(return_addr,info);
			if(info->exception_record==0) terminate();
			continue;
		}

		action=((ex_destroylocal *)info->action_pointer)->action;
		switch(action&EXACTION_MASK)
		{
		case EXACTION_DESTROYLOCAL:
			ExX86_DestroyLocal(context,(ex_destroylocal *)info->action_pointer);
			info->action_pointer+=sizeof(ex_destroylocal); break;

		case EXACTION_DESTROYLOCALCOND:
			ExX86_DestroyLocalCond(context,(ex_destroylocalcond *)info->action_pointer);
			info->action_pointer+=sizeof(ex_destroylocalcond); break;

		case EXACTION_DESTROYLOCALPOINTER:
			ExX86_DestroyLocalPointer(context,(ex_destroylocalpointer *)info->action_pointer);
			info->action_pointer+=sizeof(ex_destroylocalpointer); break;

		case EXACTION_DESTROYLOCALARRAY:
			ExX86_DestroyLocalArray(context,(ex_destroylocalarray *)info->action_pointer);
			info->action_pointer+=sizeof(ex_destroylocalarray); break;

		case EXACTION_DESTROYPARTIALARRAY:
			ExX86_DestroyPartialArray(context,(ex_destroypartialarray *)info->action_pointer);
			info->action_pointer+=sizeof(ex_destroypartialarray); break;

		case EXACTION_DESTROYMEMBER:
			ExX86_DestroyMember(context,(ex_destroymember *)info->action_pointer, -1);
			info->action_pointer+=sizeof(ex_destroymember); break;

		case EXACTION_DESTROYBASE:
			ExX86_DestroyMember(context,(ex_destroymember *)info->action_pointer, 0);
			info->action_pointer+=sizeof(ex_destroymember); break;

		case EXACTION_DESTROYMEMBERCOND:
			ExX86_DestroyMemberCond(context,(ex_destroymembercond *)info->action_pointer);
			info->action_pointer+=sizeof(ex_destroymembercond); break;

		case EXACTION_DESTROYMEMBERARRAY:
			ExX86_DestroyMemberArray(context,(ex_destroymemberarray *)info->action_pointer);
			info->action_pointer+=sizeof(ex_destroymemberarray); break;

		case EXACTION_DELETEPOINTER:
			ExX86_DeletePointer(context,(ex_deletepointer *)info->action_pointer);
			info->action_pointer+=sizeof(ex_deletepointer); break;

		case EXACTION_DELETEPOINTERCOND:
			ExX86_DeletePointerCond(context,(ex_deletepointercond *)info->action_pointer);
			info->action_pointer+=sizeof(ex_deletepointercond); break;

		case EXACTION_CATCHBLOCK:
			if(catcher==(ex_catchblock *)info->action_pointer) return;
			info->action_pointer+=sizeof(ex_catchblock); break;

		case EXACTION_ACTIVECATCHBLOCK:
			{
				CatchInfo		*catchinfo;

				catchinfo=(CatchInfo *)(context->EBP+((ex_activecatchblock *)info->action_pointer)->cinfo_ref);
				if(catchinfo->dtor)
				{
					if(context->location == catchinfo->location)
					{
					    context->dtor = catchinfo->dtor;
					}
					else
					{
						asm
						{
							mov		eax, catchinfo
							mov		ecx, CatchInfo.location[eax]
							call	CatchInfo.dtor[eax]
						}
					}
				}

				info->action_pointer+=sizeof(ex_activecatchblock);
			}
			break;

		case EXACTION_SPECIFICATION:
			if(catcher==(void *)info->action_pointer) return;
			info->action_pointer+=
				sizeof(ex_specification)
			+	((ex_specification *)info->action_pointer)->specs*sizeof(void *);
			break;

		default:
			terminate();
		}
		if(action&EXACTION_ENDBIT) info->action_pointer=0;
	}
}

static int ExX86_IsInSpecification(char *extype,ex_specification *spec)
{
	long	i,offset;

	for(i=0; i<spec->specs; i++)
	{
		if(__throw_catch_compare(extype,spec->spec[i],&offset)) return 1;
	}
	return 0;
}

extern "C" void __unexpected(CatchInfo* catchinfo)
{
	ex_specification *unexp=(ex_specification *)catchinfo->stacktop;

#pragma exception_magic

	try {
		unexpected();
	}
	catch(...)
	{
		if(ExX86_IsInSpecification((char *)((CatchInfo *)&__exception_magic)->typeinfo,unexp))
		{
			throw;
		}
#ifndef _MSL_NO_CPP_NAMESPACE
		if(ExX86_IsInSpecification("!std::bad_exception!!",unexp))
		{
			throw bad_exception();
		}
#else
		if(ExX86_IsInSpecification("!bad_exception!!",unexp))
		{
			throw bad_exception();
		}
#endif
	}
	terminate();
}

static asm void ExX86_LongJump(ThrowContext *context,void *pc)
{
}

static void ExX86_HandleUnexpected(ThrowContext *context,ExceptionInfo *info,ex_specification *unexp)
{
	CatchInfo	*catchinfo;

#pragma exception_terminate

	ExX86_UnwindStack(context,info,unexp);

	catchinfo=(CatchInfo *)(context->EBP+unexp->cinfo_ref);
	catchinfo->location		= context->location;
	catchinfo->typeinfo		= context->throwtype;
	catchinfo->dtor			= context->dtor;
	catchinfo->stacktop		= unexp;

	asm
	{
			mov		eax, unexp
			mov		ecx, ex_specification.pcoffset[eax]
			mov		eax, context
			mov		ebx, ThrowContext.EBX[eax]
			mov		esi, ThrowContext.ESI[eax]
			mov		edi, ThrowContext.EDI[eax]
			mov		ebp, ThrowContext.EBP[eax]

			finit
			fwait

			jmp		ecx
	}
}

static void ExX86_ThrowHandler(ThrowContext *context)
{
	ActionIterator	iter;
	ExceptionInfo	info;
	exaction_type	action;
	ex_catchblock	*catchblock;
	CatchInfo		*catchinfo;
	long			offset;

	ExX86_FindExceptionRecord(context->returnaddr,&info);
	if(info.exception_record==0) terminate();

	if(context->throwtype==0)
	{
		iter.info		= info;
		iter.current_bp	= context->EBP;
		for(action=ExX86_CurrentAction(&iter);; action=ExX86_NextAction(&iter))
		{
			switch(action)
			{
			case EXACTION_ACTIVECATCHBLOCK:
				break;

			case EXACTION_ENDOFLIST:
			case EXACTION_DESTROYLOCAL:
			case EXACTION_DESTROYLOCALCOND:
			case EXACTION_DESTROYLOCALPOINTER:
			case EXACTION_DESTROYLOCALARRAY:
			case EXACTION_DESTROYPARTIALARRAY:
			case EXACTION_DESTROYMEMBER:
			case EXACTION_DESTROYBASE:
			case EXACTION_DESTROYMEMBERCOND:
			case EXACTION_DESTROYMEMBERARRAY:
			case EXACTION_DELETEPOINTER:
			case EXACTION_DELETEPOINTERCOND:
			case EXACTION_CATCHBLOCK:
			case EXACTION_SPECIFICATION:
				continue;

			case EXACTION_TERMINATE:
			default:
				terminate();
			}
			break;
		}
		catchinfo=(CatchInfo *)(iter.current_bp+((ex_activecatchblock *)iter.info.action_pointer)->cinfo_ref);
		context->throwtype	= (char *)catchinfo->typeinfo;
		context->location	= catchinfo->location;
		context->dtor		= 0;
		context->catchinfo	= catchinfo;
	}
	else context->catchinfo=0L;

	iter.info		= info;
	iter.current_bp	= context->EBP;
	for(action=ExX86_CurrentAction(&iter);; action=ExX86_NextAction(&iter))
	{
		switch(action)
		{
		case EXACTION_CATCHBLOCK:
			if(__throw_catch_compare(context->throwtype,((ex_catchblock *)iter.info.action_pointer)->catch_type,&offset))
			{
				break;
			}
			continue;

		case EXACTION_SPECIFICATION:
			if(!ExX86_IsInSpecification(context->throwtype,(ex_specification *)iter.info.action_pointer))
			{
				ExX86_HandleUnexpected(context,&info,(ex_specification *)iter.info.action_pointer);
			}
			continue;

		case EXACTION_ENDOFLIST:
		case EXACTION_DESTROYLOCAL:
		case EXACTION_DESTROYLOCALCOND:
		case EXACTION_DESTROYLOCALPOINTER:
		case EXACTION_DESTROYLOCALARRAY:
		case EXACTION_DESTROYPARTIALARRAY:
		case EXACTION_DESTROYMEMBER:
		case EXACTION_DESTROYBASE:
		case EXACTION_DESTROYMEMBERCOND:
		case EXACTION_DESTROYMEMBERARRAY:
		case EXACTION_DELETEPOINTER:
		case EXACTION_DELETEPOINTERCOND:
		case EXACTION_ACTIVECATCHBLOCK:
			continue;

		case EXACTION_TERMINATE:
		default:
			terminate();
		}
		break;
	}

	catchblock=(ex_catchblock *)iter.info.action_pointer;
	ExX86_UnwindStack(context,&info,catchblock);

	catchinfo=(CatchInfo *)(context->EBP+catchblock->cinfo_ref);
	catchinfo->location		= context->location;
	catchinfo->typeinfo		= context->throwtype;
	catchinfo->dtor			= context->dtor;
	if(*context->throwtype=='*')
	{
		catchinfo->sublocation	= &catchinfo->pointercopy;
		catchinfo->pointercopy	= *(long *)context->location+offset;
	}
	else
	{
		catchinfo->sublocation	= (char *)context->location+offset;
	}

	asm
	{
			mov		eax, catchblock
			mov		ecx, ex_catchblock.catch_pcoffset[eax]
			mov		eax, context
			mov		ebx, ThrowContext.EBX[eax]
			mov		esi, ThrowContext.ESI[eax]
			mov		edi, ThrowContext.EDI[eax]
			mov		ebp, ThrowContext.EBP[eax]

			finit
			fwait

			jmp		ecx
	}
}

extern asm void __throw(char *throwtype,void *location,void *dtor)
{
	asm
	{
			push	ebp
			mov		ebp, esp

			sub		esp, ThrowContext
			mov		0[esp], ebx
			mov		4[esp], esi
			mov		8[esp], edi
			mov		eax, 0[ebp]
			mov		12[esp], eax
			mov		eax, 4[ebp]
			mov		16[esp], eax
			mov		eax, throwtype
			mov		20[esp], eax
			mov		eax, location
			mov		24[esp], eax
			mov		eax, dtor
			mov		28[esp], eax

			push	esp
			call 	ExX86_ThrowHandler
			ret
	}

}

#endif
