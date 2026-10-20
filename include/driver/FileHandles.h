#ifndef DRIVER_FILEHANDLES_H
#define DRIVER_FILEHANDLES_H

#include "compiler/common.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int __stdcall OS_NewFileHandle(
    const OSSpec* spec, OSHandle* src, Boolean writeable, OSFileHandle* hand
);
extern int __stdcall OS_LockFileHandle(OSFileHandle* hand, Ptr* ptr, UInt32* size);
extern int __stdcall OS_FreeFileHandle(OSFileHandle* hand);

#ifdef __cplusplus
}
#endif

#endif
