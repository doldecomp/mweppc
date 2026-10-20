/* MSL Win32's ThreadLocalData.c, as 2.x has it: the updater's (lib/msl/MSL_Win32/Src), but _GetThreadLocalData writes
 * its message to the standard error handle and exits with 7 where the updater's shows a message box and exits with 0.
 * (_DisposeThreadData, not linked, is left out.) */
#include "ansi_prefix.Win32.h"
#include "ThreadLocalData.h"
#include <stdlib.h>
#include <string.h>
#include <WINDEF.H>
#include <WINBASE.H>

void _DisposeThreadDataIndex(void);

static unsigned long _gThreadDataIndex = -1;

int _InitializeThreadDataIndex(void)
{
	if (_gThreadDataIndex != -1) {
		return 1;
	}

	_gThreadDataIndex = TlsAlloc();

	if (_gThreadDataIndex == -1) {
		return 0;
	}

	if (atexit(_DisposeThreadDataIndex)) {
		return 0;
	} else {
		return 1;
	}
}

void _DisposeThreadDataIndex(void)
{
	if (_gThreadDataIndex == -1) {
		return;
	}

	TlsFree(_gThreadDataIndex);
	_gThreadDataIndex = -1;
}

int _InitializeThreadData(HANDLE threadHandle)
{
	_ThreadLocalData * theThreadLocalData =
			(_ThreadLocalData *)malloc(sizeof(_ThreadLocalData));

	if (!theThreadLocalData) {
		return 0;
	}

	theThreadLocalData->errno = 0;
	theThreadLocalData->random_next = 1;
	/* TODO: "\0\0\0" is "" with the 4 bytes the original's literal takes (this compiler packs a 1-byte "" and
	   the next literal together); the original's source surely said "". Same string to every reader. */
	theThreadLocalData->strtok_n = (unsigned char *) "\0\0\0";
	theThreadLocalData->strtok_s = (unsigned char *) "\0\0\0";
	theThreadLocalData->thread_handle = threadHandle;

	return TlsSetValue(_gThreadDataIndex, theThreadLocalData);
}

int _InitializeMainThreadData(void)
{
    if (_InitializeThreadDataIndex())
    {
        if (_InitializeThreadData(NULL))
        {
            return 1;
        }
    }

    return 0;
}

static const char *no_thread_data = "Could not get thread local data\n";

_ThreadLocalData * _GetThreadLocalData(void)
{
    _ThreadLocalData *tld;
    DWORD written;

	tld = (_ThreadLocalData *)TlsGetValue(_gThreadDataIndex);

	if (tld == 0)
	{
		_InitializeThreadData(NULL);
		tld = (_ThreadLocalData *)TlsGetValue(_gThreadDataIndex);
	}

	if (tld == 0)
	{
	    HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
	    WriteFile(handle, no_thread_data, strlen(no_thread_data), &written, NULL);
	    exit(7);
	}
	return tld;
}
