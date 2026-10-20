#ifndef DRIVER_RESOURCES_H
#define DRIVER_RESOURCES_H

#include "compiler/common.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Boolean OS_UsingMacResourceForkInfo(void);
extern OSErr SystemInitResources(OSSpec* spec);
extern void OS_RemoveMacResourceForkRef(int ref);
extern OSSpec* OS_GetMacResourceForkFromRef(int ref);
extern Boolean OS_SearchMacResourceForkList(const OSSpec* rspec, int* ref);
extern int OS_SetMacResourceForkCreatorAndType(int ref, OSType creator, OSType type);
extern int OS_GetMacResourceForkCreatorAndType(int ref, OSType* creator, OSType* type);
extern void OS_CleanupMacResourceForkDir(const OSPathSpec* dir);
extern Handle GetResource(OSType theType, SInt16 theID);
extern void __stdcall ReleaseResource(Handle theResource);

#ifdef __cplusplus
}
#endif

#endif
