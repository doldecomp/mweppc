#ifndef DRIVER_COSTOOLSCLT_H
#define DRIVER_COSTOOLSCLT_H

#include "compiler/common.h"
#include "driver/Files.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

// The compiler's OS services
extern Handle COS_NewHandle(SInt32 byteCount);
extern Handle COS_NewOSHandle(SInt32 logicalSize);
extern void COS_FreeHandle(Handle handle);
extern Boolean COS_ResizeHandle(Handle handle, SInt32 newSize);
extern void COS_LockHandle(Handle handle);
extern void COS_LockHandleHi(Handle handle);
extern void COS_UnlockHandle(Handle handle);
extern SInt32 COS_GetTicks(void);
extern void COS_GetString(char* buffer, SInt16 strListID, SInt16 index);
extern Boolean COS_IsMultiByte(const void* str, const void* pos);
extern OSErr COS_FileNew(const FSSpec* spec, SInt16* refNum, OSType creator, OSType fileType);
extern OSErr COS_FileOpen(const FSSpec* spec, SInt16* refNum);
extern OSErr COS_FileGetType(const FSSpec* spec, OSType* fileType);
extern OSErr COS_FileGetSize(SInt16 refNum, SInt32* logEOF);
extern OSErr COS_FileRead(SInt16 refNum, void* buffPtr, SInt32 count);
extern OSErr COS_FileWrite(SInt16 refNum, const void* buffPtr, SInt32 count);
extern OSErr COS_FileGetPos(SInt16 refNum, SInt32* filePos);
extern OSErr COS_FileSetPos(SInt16 refNum, SInt32 filePos);
extern OSErr COS_FileClose(SInt16 refNum);
extern void COS_FileGetFSSpecInfo(
    const FSSpec* spec, SInt16* vRefNum, SInt32* dirID, StringPtr fileName
);
extern void COS_FileGetPathName(char* buffer, const FSSpec* spec, SInt32* mdDat);

#ifdef __cplusplus
}
#endif

#endif
