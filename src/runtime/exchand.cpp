// The x86 runtime's exchand.cpp as 2.x links it, a revision later than lib/runtime's: _MWCHandler writes its message
// to the standard error handle where lib/runtime's shows a message box, and the exception table registry and the
// lookup of a return address's exception record are here (lib/runtime's ExceptionX86.cpp has them, with these
// bodies)
#include "ansi_prefix.Win32.h"
#include <stdio.h>
#include <string.h>
#include <WINDEF.H>
#include <WINBASE.H>
#include <exception>
#include "ExceptionX86.h"

// Any of these bits means handler is called in unwinding mode
#define UNWINDING_MASK 0x66

// Exception code for a C++ exception
#define CPP_EXCEPTION_CODE 0xE06D7363

// (ExceptionX86.cpp's)
typedef struct ExceptionInfo {
	ExceptionRecord *exception_record;		//	pointer to exception record
	char			*current_function;		//	pointer to current function
	char			*action_pointer;		//	pointer to action
}	ExceptionInfo;

extern "C" {

// Head of list of exception table headers (ExceptionX86.cpp's)
extern ExceptionTableHeader *FirstExceptionTable;

// Default exception handler for the Metrowerks C/C++ runtime library
int _MWCHandler(PEXCEPTION_RECORD exc, void * /*exc_stack*/, LPCONTEXT context, void * /*mystery*/)
{
	HANDLE stderr_handle;
	char buffer[1024];
	DWORD written;

	// Don't do anything if we are unwinding
	if (exc->ExceptionFlags & UNWINDING_MASK)
	{
		return 1;
	}

	// Check for a C++ exception
	if (exc->ExceptionCode == CPP_EXCEPTION_CODE)
	{
		// Call the terminate function, which the user
		// might replace
		terminate();
	}

	// Make a message
	sprintf(buffer, "Unhandled exception: %08x\n"
					"At address:          %08x\n",
					(unsigned int)exc->ExceptionCode,
					(unsigned int)context->Eip);

	// And write it to the standard error handle
	stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
	WriteFile(stderr_handle, buffer, strlen(buffer), &written, NULL);

	// return success
	return 1;
}

#ifdef __MWERKS__
// (clang's build has no MWCC exception tables: src/runtime/maincrt.c)

// Register an exception table

void _RegisterExceptionTables(ExceptionTableHeader *header)
{
    ExceptionTableHeader *prev, *next;

    // Link the tables together in order of decreasing Pc, that way, the first in the list with a Pc less
    // than the one we are looking for must be the right one.

    // First, find position in table
    prev = 0;
    next = FirstExceptionTable;
    while (next && next->First->Pc > header->First->Pc)
    {
        prev = next;
        next = next->Next;
    }

    // Now link it in there
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

/************************************************************************/
/* Purpose..: Get a exception record pointer							*/
/* Input....: pointer to return address									*/
/* Input....: pointer to ExceptionInfo struct for result				*/
/* Return...: ---														*/
/************************************************************************/
void ExX86_FindExceptionRecord(char *returnaddr,ExceptionInfo *info)
{
    FunctionTableEntry *functab;
    int num, min, max, mid;
    char *Pc;
    static long def = 0;
    ExceptionTableHeader *table;

    // Assume not found
    info->exception_record = 0;
    info->action_pointer = 0;

    // Find the correct function mapping table.  Since they are always non-overlapping,
    // and linked in order of decreasing address, the first table with a starting address
    // less than the pc we are looking for must be it

    table = FirstExceptionTable;
    while (table && table->First->Pc > returnaddr)
    {
        table = table->Next;
    }

    if (!table)
    {
        // no table found in the correct range
        return;
    }

    // Get pointer to the table and the number of entries (Last-First)
    functab = table->First;
    num = table->Last - functab;

    // binary search for the return address
    min = 0;
    max = num-1;
    while (min <= max)
    {
        mid = (max + min) / 2;
        Pc = functab[mid].Pc;

        // See if it is in the range
        if (returnaddr >= Pc && returnaddr < functab[mid+1].Pc)
        {
            char *ptr;
            int i;
            unsigned char *pcaddr;

            // Get pointer to exception record
            ptr = functab[mid].exctable;
            info->exception_record = (ExceptionRecord *)ptr;

            // Make sure we actually have actions for this guy
            if ((info->exception_record->register_mask & 1) == 0)
            {
                // Get number of offsets
                num = info->exception_record->num_offsets;
                ptr += sizeof(ExceptionRecord);

                // Now find the last return address pointer that
                // is lower than the actual return address

                for (i=0; i<num; i++)
                {
                    pcaddr = *(unsigned char **)ptr;
                    ptr += 4;

                    // the pcaddr is the address of the call, get
                    // the address of the next instruction to match
                    // the return address.  If you don't like this,
                    // send complaints to Cam.

                    if (*pcaddr == 0xe8)
                    {
                        // 32-bit relative call, always 5 byte
                        pcaddr += 5;
                    }
                    else if (*pcaddr == 0xff)
                    {
                        int mod, rm;

                        // indirect call, interpret the mode byte

                        pcaddr++;

                        mod = *pcaddr >> 6;
                        rm = (*pcaddr) & 7;
                        pcaddr++;

                        // Register mode (mod=3) has no more byte, others do
                        if (mod != 3)
                        {
                            // Watch for sib byte

                            if (rm == 4)
                            {
                                pcaddr++;
                            }

                            // and for 32-bit direct addressing

                            if (mod == 0 && rm == 5)
                            {
                                // direct 32-bit skip the 4 byte addr
                                pcaddr += 4;
                            }
                            else
                            {
                                // size of displacement depends on rm

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
                        // Error: not a call instruction.

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

} // extern "C"
