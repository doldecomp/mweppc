#ifndef DRIVER_CLACCESSPATHS_H
#define DRIVER_CLACCESSPATHS_H

#include "compiler/common.h"
#include "driver/cmdline.h"
#include "driver/oslib.h"
#include "driver/Files.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Path {
    OSPathSpec* spec;
    struct Paths* recursive;
    char* dirlist;
    SInt16 flags;
};
struct Paths {
    Path** pathsArray;
    UInt16 arraySize;
    UInt16 pathsCount;
};
extern Path* Path_New(const OSPathSpec* dir);
extern Boolean Paths_Initialize(Paths* paths);
extern Boolean Paths_Terminate(Paths* paths);
extern Boolean Paths_AddPath(Paths* paths, Path* path);
extern Boolean Paths_InsertPath(Paths* paths, UInt16 index, Path* path);
extern Boolean Paths_RemovePath(Paths* paths, UInt16 index);
extern Path* Paths_GetPath(Paths* paths, UInt16 pathnum);
extern UInt16 Paths_Count(const Paths* paths);
extern Path* Paths_FindPathSpec(const Paths* paths, const OSPathSpec* dir);
extern Boolean Paths_GatherRecurse(Path* path);
extern int Paths_CountRecurse(Paths* paths);
extern void Paths_CopyRecurseFSS(FSSpec* list, Paths* paths, UInt16 count);

#ifdef __cplusplus
}
#endif

#endif
