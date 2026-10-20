#ifndef DRIVER_CLDEPENDENCIES_H
#define DRIVER_CLDEPENDENCIES_H

#include "compiler/common.h"
#include "driver/cmdline.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

struct InclFile {
    SInt32 filenameoffs;
    struct Path* accesspath;
    struct Path* globalpath;
    struct Path* specialpath;
    Boolean syspath;
};
struct Deps {
    int numDeps;
    int maxDeps;
    SInt32* list;
    struct Incls* incls;
};
struct Incls {
    struct Target* targ;
    SInt32 numincls;
    SInt32 maxincls;
    struct InclFile* files;
    SInt32 buflen;
    SInt32 bufpos;
    char* buffer;
    struct Paths* allPaths;
};
extern SInt16* CLT_filesp;
extern struct CPrepFileInfo** CLT_filestack;

extern Boolean Incls_Initialize(Incls* incls, Target* targ);
extern void Incls_Terminate(Incls* incls);
extern Boolean Incls_FindFileInPaths(
    Incls* incls, char* filename, Boolean fullsearch, OSSpec* spec, SInt32* inclidx
);
extern Boolean Deps_Initialize(Deps* deps, Incls* incls);
extern int Deps_ChangeSpecialAccessPath(OSSpec* srcfss, Boolean initialize);
extern void Deps_AddDependency(
    Deps* deps,
    SInt32 incl,
    OSSpec* spec,
    Boolean syspath,
    SInt16 dependencyType,
    Boolean* alreadyincluded
);
extern void Deps_ListDependencies(Incls* incls, File* file, OSHandle* h);

#ifdef __cplusplus
}
#endif

#endif
