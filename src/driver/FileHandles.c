#include "driver/FileHandles.h"
#include "compiler/win32.h"
#include "driver/Generic.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"

static int OS_LoadFileHandle(OSFileHandle* hand) {
    int err;
    int ref;
    UInt32 sz;
    void* buffer;

    hand->loaded = FALSE;

    err = OS_Open(&hand->spec, OSReadOnly, &ref);
    if (!err) {
        err = OS_GetSize(ref, &sz);
        if (!err) {
            err = OS_ResizeHandle(&hand->hand, sz);
            if (!err) {
                buffer = OS_LockHandle(&hand->hand);
                err = OS_Read(ref, buffer, &sz);
                if (!err) {
                    hand->loaded = TRUE;
                    hand->changed = FALSE;
                }
                OS_UnlockHandle(&hand->hand);
            }
        }
        OS_Close(ref);
    }

    return err;
}

static int OS_WriteFileHandle(OSFileHandle* hand) {
    int err;
    int ref;
    UInt32 sz;
    void* buffer;

    if (!hand->loaded && !hand->changed) {
        return 0;
    }

    OS_Delete(&hand->spec);
    err = OS_Create(&hand->spec, &OS_TEXTTYPE);
    if (!err) {
        err = OS_Open(&hand->spec, OSReadWrite, &ref);
        if (!err) {
            err = OS_GetHandleSize(&hand->hand, &sz);
            if (!err) {
                buffer = OS_LockHandle(&hand->hand);
                err = OS_Write(ref, buffer, &sz);
                if (!err) {
                    hand->changed = FALSE;
                }
                OS_UnlockHandle(&hand->hand);
                OS_Close(ref);
            }
        }
    }

    return err;
}

int __stdcall OS_NewFileHandle(
    const OSSpec* spec, OSHandle* src, Boolean writeable, OSFileHandle* hand
) {
    int err;

    if (!writeable && src) {
        return ERROR_INVALID_ACCESS;
    }

    hand->spec = *spec;
    hand->writeable = writeable;

    if (!src) {
        err = OS_NewHandle(0, &hand->hand);
        if (err) {
            return err;
        }
        err = OS_LoadFileHandle(hand);
    } else {
        err = OS_CopyHandle(src, &hand->hand);
        if (err) {
            return err;
        }
        hand->changed = TRUE;
        hand->loaded = TRUE;
    }

    return err;
}

int __stdcall OS_LockFileHandle(OSFileHandle* hand, Ptr* ptr, UInt32* size) {
    *size = 0;

    if (!OS_ValidHandle(&hand->hand)) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    *ptr = OS_LockHandle(&hand->hand);
    OS_GetHandleSize(&hand->hand, size);
    return 0;
}

int __stdcall OS_UnlockFileHandle(OSFileHandle* hand) {
    if (!OS_ValidHandle(&hand->hand)) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    OS_UnlockHandle(&hand->hand);
    return 0;
}

int __stdcall OS_FreeFileHandle(OSFileHandle* hand) {
    int err;

    if (hand->writeable && hand->changed) {
        err = OS_WriteFileHandle(hand);
        if (err) {
            return err;
        }
    }

    if (!OS_ValidHandle(&hand->hand)) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    err = OS_FreeHandle(&hand->hand);
    if (err) {
        return err;
    }

    hand->loaded = FALSE;
    return 0;
}

void __stdcall OS_GetFileHandleSpec(const OSFileHandle* hand, OSSpec* spec) {
    *spec = hand->spec;
}
