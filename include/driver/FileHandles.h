#ifndef DRIVER_FILEHANDLES_H
#define DRIVER_FILEHANDLES_H

#include "compiler/common.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

// A file's contents in a handle, written back when freed (the OS layer's FileHandles)
extern int __stdcall OS_NewFileHandle(
    const OSSpec* spec, OSHandle* src, Boolean writeable, OSFileHandle* hand
);
extern int __stdcall OS_LockFileHandle(OSFileHandle* hand, Ptr* ptr, UInt32* size);
extern int __stdcall OS_FreeFileHandle(OSFileHandle* hand);

#ifdef __cplusplus
}
#endif

#endif
