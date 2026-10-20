#ifndef DRIVER_MEMORY_H
#define DRIVER_MEMORY_H

#include "compiler/common.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

extern OSHandle* OS_PeekMacHandle(Handle h);
extern Handle OS_CreateMacHandle(OSHandle* h);
extern void OS_DestroyMacHandle(Handle h, OSHandle* ret);
extern OSErr MemError(void);
extern Handle NewHandle(SInt32 dataSize);
extern Handle TempNewHandle(UInt32 dataSize, OSErr* resultCode);
extern void __stdcall DisposeHandle(Handle h);
extern void __stdcall HLock(Handle h);
extern void __stdcall HLockHi(Handle h);
extern void __stdcall HUnlock(Handle h);
extern SInt32 __stdcall GetHandleSize(Handle h);
extern void __stdcall SetHandleSize(Handle h, SInt32 newSize);
extern OSErr __stdcall PtrAndHand(const void* srcPtr, Handle destHandle, SInt32 size);
extern OSErr __stdcall HandAndHand(Handle src, Handle dst);

#ifdef __cplusplus
}
#endif

#endif
