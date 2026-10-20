#ifndef DRIVER_CLTARG_H
#define DRIVER_CLTARG_H

#include "compiler/common.h"
#include "driver/cmdline.h"
#include "driver/oslib.h"
#include "driver/CLAccessPaths.h"
#include "driver/CLDependencies.h"
#include "driver/CLFiles.h"
#include "driver/CLOverlays.h"
#include "driver/CLSegs.h"
#include "driver/DropInCompilerLinker.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LinkModel_Flat = 0,
    LinkModel_Segmented = 1,
    LinkModel_Overlay1 = 2
};

struct Target {
    struct BuildInfo {
        UInt32 linesCompiled;
        UInt32 codeSize;
        UInt32 iDataSize;
        UInt32 uDataSize;
    } info;
    CWTargetInfo* targetinfo;
    struct {
        Segments segs;
        Overlays overlays;
    } linkage;
    SInt32 linkmodel;
    Files files;
    Files pchs;
    Incls incls;
    Paths sysPaths;
    Paths userPaths;
    OSType lang;
    OSType cpu;
    OSType os;
    char targetName[64];
    struct Plugin* preLinker;
    struct Plugin* linker;
    struct Plugin* postLinker;
    UInt32 preLinkerDropinFlags;
    UInt32 linkerDropinFlags;
    UInt32 postLinkerDropinFlags;
    OSPathSpec outputDirectory;
    VFile* virtualFiles;
    struct Target* next;
};
extern Target* Target_New(const char* name, OSType cpu, OSType os, OSType lang);
extern void Targets_Term(Target* list);
extern void Target_Add(Target** list, Target* targ);

#ifdef __cplusplus
}
#endif

#endif
