#include "ansi_prefix.Win32.h"
#include <stdio.h>
#include <string.h>
#include <WINDEF.H>
#include <WINBASE.H>
#include <exception>
#include "ExceptionX86.h"

#define UNWINDING_MASK 0x66

#define CPP_EXCEPTION_CODE 0xE06D7363

typedef struct ExceptionInfo {
	ExceptionRecord *exception_record;
	char			*current_function;
	char			*action_pointer;
}	ExceptionInfo;

extern "C" {
extern ExceptionTableHeader *FirstExceptionTable;

int _MWCHandler(PEXCEPTION_RECORD exc, void *  , LPCONTEXT context, void *  )
{
	HANDLE stderr_handle;
	char buffer[1024];
	DWORD written;

	if (exc->ExceptionFlags & UNWINDING_MASK)
	{
		return 1;
	}

	if (exc->ExceptionCode == CPP_EXCEPTION_CODE)
	{
		terminate();
	}

	sprintf(buffer, "Unhandled exception: %08x\n"
					"At address:          %08x\n",
					(unsigned int)exc->ExceptionCode,
					(unsigned int)context->Eip);

	stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
	WriteFile(stderr_handle, buffer, strlen(buffer), &written, NULL);

	return 1;
}

#ifdef __MWERKS__

void _RegisterExceptionTables(ExceptionTableHeader *header)
{
    ExceptionTableHeader *prev, *next;

    prev = 0;
    next = FirstExceptionTable;
    while (next && next->First->Pc > header->First->Pc)
    {
        prev = next;
        next = next->Next;
    }

    header->Next = next;
    if (prev)
    {
        prev->Next = header;
    }
    else
    {
        FirstExceptionTable = header;
    }
}

void ExX86_FindExceptionRecord(char *returnaddr,ExceptionInfo *info)
{
    FunctionTableEntry *functab;
    int num, min, max, mid;
    char *Pc;
    static long def = 0;
    ExceptionTableHeader *table;

    info->exception_record = 0;
    info->action_pointer = 0;

    table = FirstExceptionTable;
    while (table && table->First->Pc > returnaddr)
    {
        table = table->Next;
    }

    if (!table)
    {
        return;
    }

    functab = table->First;
    num = table->Last - functab;

    min = 0;
    max = num-1;
    while (min <= max)
    {
        mid = (max + min) / 2;
        Pc = functab[mid].Pc;

        if (returnaddr >= Pc && returnaddr < functab[mid+1].Pc)
        {
            char *ptr;
            int i;
            unsigned char *pcaddr;

            ptr = functab[mid].exctable;
            info->exception_record = (ExceptionRecord *)ptr;

            if ((info->exception_record->register_mask & 1) == 0)
            {
                num = info->exception_record->num_offsets;
                ptr += sizeof(ExceptionRecord);

                for (i=0; i<num; i++)
                {
                    pcaddr = *(unsigned char **)ptr;
                    ptr += 4;

                    if (*pcaddr == 0xe8)
                    {
                        pcaddr += 5;
                    }
                    else if (*pcaddr == 0xff)
                    {
                        int mod, rm;

                        pcaddr++;

                        mod = *pcaddr >> 6;
                        rm = (*pcaddr) & 7;
                        pcaddr++;

                        if (mod != 3)
                        {
                            if (rm == 4)
                            {
                                pcaddr++;
                            }

                            if (mod == 0 && rm == 5)
                            {
                                pcaddr += 4;
                            }
                            else
                            {
                                switch(mod)
                                {
                                case 1:
                                    pcaddr++;
                                    break;

                                case 2:
                                    pcaddr += 4;
                                    break;
                                }
                            }
                        }
                    }
                    else
                    {
                        terminate();
                    }

                    if ((char *)pcaddr == returnaddr)
                    {
                        info->action_pointer = *(char **)ptr;
                        return;
                    }
                    ptr += 4;
                }
            }
            info->action_pointer = 0;
            return;
        }
        else if (Pc < returnaddr)
        {
            min = mid+1;
        }
        else
        {
            max = mid-1;
        }
    }
}

#endif

}
