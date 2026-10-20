#ifndef DRIVER_MSDOS_H
#define DRIVER_MSDOS_H

#include "compiler/common.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uOSTypePair OS_TEXTTYPE;
extern const char* __stdcall OS_GetErrText(int err);
extern int __stdcall OS_InitProgram(int* pArgc, char*** pArgv);
extern int __stdcall OS_Create(const OSSpec* spec, const uOSTypePair* type);
extern int __stdcall OS_Status(const OSSpec* spec);
extern int __stdcall OS_SetFileType(const OSSpec* spec, const uOSTypePair* type);
extern int __stdcall OS_GetFileTime(const OSSpec* spec, OSTime* crtm, OSTime* chtm);
extern int __stdcall OS_SetFileTime(const OSSpec* spec, const OSTime* crtm, const OSTime* chtm);
extern int __stdcall OS_Open(const OSSpec* spec, OSOpenMode mode, int* ref);
extern int __stdcall OS_Write(int ref, const void* buffer, UInt32* length);
extern int __stdcall OS_Read(int ref, void* buffer, UInt32* length);
extern int __stdcall OS_Seek(int ref, OSSeekMode how, SInt32 offset);
extern int __stdcall OS_Tell(int ref, SInt32* offset);
extern int __stdcall OS_Close(int ref);
extern int __stdcall OS_GetSize(int ref, UInt32* length);
extern int __stdcall OS_SetSize(int ref, UInt32 size);
extern int __stdcall OS_Delete(const OSSpec* spec);
extern int __stdcall OS_Mkdir(const OSSpec* spec);
extern int __stdcall OS_Rmdir(const OSPathSpec* spec);
extern int __stdcall OS_GetCWD(OSPathSpec* spec);
extern int __stdcall OS_Execute(
    OSSpec* spec,
    char** argv,
    char** envp,
    const char* stdoutfile,
    const char* stderrfile,
    int* exitcode
);
extern char* __stdcall OS_GetDirPtr(char* path);
extern int OS_CanonPath(const char* src, char* dst);
extern int __stdcall OS_IsFullPath(const char* path);
extern int __stdcall OS_EqualPath(const char* a, const char* b);
extern int __stdcall OS_MakeSpec(const char* path, OSSpec* spec, Boolean* isfile);
extern int __stdcall OS_MakeFileSpec(const char* path, OSSpec* spec);
extern int __stdcall OS_MakePathSpec(const char* vol, const char* dir, OSPathSpec* spec);
extern int __stdcall OS_MakeNameSpec(const char* name, OSNameSpec* spec);
extern char* __stdcall OS_SpecToString(const OSSpec* spec, char* path, int size);
extern char* __stdcall OS_PathSpecToString(const OSPathSpec* pspec, char* path, int size);
extern char* __stdcall OS_NameSpecToString(const OSNameSpec* nspec, char* name, int size);
extern int __stdcall OS_EqualSpec(const OSSpec* a, const OSSpec* b);
extern int __stdcall OS_EqualPathSpec(const OSPathSpec* a, const OSPathSpec* b);
extern int __stdcall OS_IsDir(const OSSpec* spec);
extern int __stdcall OS_IsFile(const OSSpec* spec);
extern int __stdcall OS_IsLink(const OSSpec* spec);
extern int __stdcall OS_ResolveLink(const OSSpec* link, OSSpec* target);
extern int __stdcall OS_OpenDir(const OSPathSpec* spec, OSOpenedDir* ref);
extern int __stdcall OS_ReadDir(OSOpenedDir* ref, OSSpec* spec, char* filename, Boolean* isfile);
extern int __stdcall OS_CloseDir(OSOpenedDir* ref);
extern UInt32 OS_GetMilliseconds(void);
extern void __stdcall OS_GetTime(OSTime* p);
extern int __stdcall OS_NewHandle(UInt32 size, OSHandle* hand);
extern int __stdcall OS_ResizeHandle(OSHandle* hand, UInt32 size);
extern void* __stdcall OS_LockHandle(OSHandle* hand);
extern void __stdcall OS_UnlockHandle(OSHandle* hand);
extern int __stdcall OS_FreeHandle(OSHandle* hand);
extern int __stdcall OS_GetHandleSize(OSHandle* hand, UInt32* size);
extern void __stdcall OS_InvalidateHandle(OSHandle* hand);
extern Boolean __stdcall OS_ValidHandle(OSHandle* hand);
extern int __stdcall OS_OSErrorToMacError(int err);
extern void __stdcall OS_TimeToMac(OSTime sectm, UInt32* secs);
extern void __stdcall OS_MacToTime(UInt32 secs, OSTime* sectm);
extern SInt16 __stdcall OS_RefToMac(int ref);
extern int OS_MacToRef(SInt16 refnum);
extern int __stdcall OS_LoadMacResourceFork(const OSSpec* spec, void** file_data, SInt32* file_len);
extern Boolean __stdcall OS_IsMultiByte(const char* str, const char* pos);

#ifdef __cplusplus
}
#endif

#endif
