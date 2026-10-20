#include "driver/Memory.h"
#include "driver/Generic.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include <stdio.h>
#include <stdlib.h>

static OSErr memError;

typedef struct HandleStructure {
    char* addr;
    OSHandle hand;
} HandleStructure;

#define HS(h) ((HandleStructure*)(h))

OSHandle* OS_PeekMacHandle(Handle h) {
    return &HS(h)->hand;
}

static void FixHandlePtr(Handle h, void* ptr) {
    HS(h)->addr = ptr;
}

Handle OS_CreateMacHandle(OSHandle* h) {
    HandleStructure* hs = malloc(sizeof(HandleStructure));
    if (!hs) {
        memError = memFullErr;
        return 0;
    }

    hs->hand = *h;
    OS_InvalidateHandle(h);
    hs->addr = OS_LockHandle(&hs->hand);
    OS_UnlockHandle(&hs->hand);
    return (Handle)hs;
}

void OS_DestroyMacHandle(Handle h, OSHandle* ret) {
    *ret = *OS_PeekMacHandle(h);
    free(h);
}

OSErr MemError(void) {
    return memError;
}

Handle NewHandle(SInt32 dataSize) {
    int err;
    OSHandle oh;

    err = OS_NewHandle(dataSize, &oh);
    if (err) {
        memError = OS_OSErrorToMacError(err);
        return 0;
    }

    return OS_CreateMacHandle(&oh);
}

Handle TempNewHandle(UInt32 dataSize, OSErr* resultCode) {
    Handle hdl;

    *resultCode = 0;
    hdl = NewHandle(dataSize);
    if (!hdl) {
        *resultCode = memError;
    }
    return hdl;
}

void __stdcall DisposeHandle(Handle h) {
    if (h) {
        memError = OS_OSErrorToMacError(OS_FreeHandle(&HS(h)->hand));
        FixHandlePtr(h, 0);
    } else {
        memError = nilHandleErr;
    }
}

void __stdcall HLock(Handle h) {
    void* buffer;

    if (h) {
        buffer = OS_LockHandle(&HS(h)->hand);
        FixHandlePtr(h, buffer);
        memError = 0;
    } else {
        memError = nilHandleErr;
    }
}

void __stdcall HLockHi(Handle h) {
    HLock(h);
}

void __stdcall HUnlock(Handle h) {
    if (h) {
        memError = 0;
        OS_UnlockHandle(&HS(h)->hand);
    } else {
        memError = nilHandleErr;
    }
}

SInt32 __stdcall GetHandleSize(Handle h) {
    UInt32 sz;

    if (h) {
        memError = OS_OSErrorToMacError(OS_GetHandleSize(&HS(h)->hand, &sz));
    } else {
        sz = 0;
        memError = nilHandleErr;
    }

    return sz;
}

void __stdcall SetHandleSize(Handle h, SInt32 newSize) {
    OSHandle* oh;

    memError = OS_OSErrorToMacError(OS_ResizeHandle(oh = &HS(h)->hand, newSize));
    FixHandlePtr(h, OS_LockHandle(oh));
    OS_UnlockHandle(oh);
}

OSErr __stdcall PtrAndHand(const void* srcPtr, Handle destHandle, SInt32 size) {
    OSErr err;
    OSHandle* oh;

    err = OS_OSErrorToMacError(OS_AppendHandle(oh = &HS(destHandle)->hand, srcPtr, size));
    FixHandlePtr(destHandle, OS_LockHandle(oh));
    OS_UnlockHandle(oh);

    return err;
}

OSErr __stdcall HandAndHand(Handle src, Handle dst) {
    OSErr err;
    SInt32 size;

    HLock(src);
    size = GetHandleSize(src);
    err = PtrAndHand(*src, dst, size);
    HUnlock(src);
    return err;
}

void DebugStr(ConstStringPtr str) {
    fprintf(stderr, "%*.*s", str[0], str[0], &str[1]);
}
