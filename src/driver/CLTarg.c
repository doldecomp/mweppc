#include "driver/CLTarg.h"
#include "driver/CLAccessPaths.h"
#include "driver/CLDependencies.h"
#include "driver/CLFiles.h"
#include "driver/CLOverlays.h"
#include "driver/CLSegs.h"
#include "driver/cmdline.h"
#include "driver/DropInCompilerLinker.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include <string.h>

Target* Target_New(const char* name, OSType cpu, OSType os, OSType lang) {
    Target* targ;

    targ = xcalloc("target", sizeof(Target));

    strncpy(targ->targetName, name, sizeof(targ->targetName));
    targ->targetinfo = xcalloc("target info", sizeof(CWTargetInfo));
    targ->cpu = cpu;
    targ->os = os;
    targ->lang = lang;

    OS_GetCWD(&targ->outputDirectory);

    OS_ASSERT(25, Segments_Initialize(&targ->linkage.segs));
    OS_ASSERT(28, Overlays_Initialize(&targ->linkage.overlays));
    OS_ASSERT(
        36,
        Files_Initialize(&targ->files) && Files_Initialize(&targ->pchs) &&
            VFiles_Initialize(&targ->virtualFiles) && Paths_Initialize(&targ->sysPaths) &&
            Paths_Initialize(&targ->userPaths) && Incls_Initialize(&targ->incls, targ)
    );

    return targ;
}

void Target_Free(Target* targ) {
    Segments_Terminate(&targ->linkage.segs);
    Overlays_Terminate(&targ->linkage.overlays);
    Paths_Terminate(&targ->sysPaths);
    Paths_Terminate(&targ->userPaths);
    Files_Terminate(&targ->files);
    Files_Terminate(&targ->pchs);
    VFiles_Terminate(&targ->virtualFiles);
    Incls_Terminate(&targ->incls);
    xfree(targ);
}

void Targets_Term(Target* list) {
    Target* scan;
    Target* next;

    for (scan = list; scan; scan = next) {
        next = scan->next;
        Target_Free(scan);
    }
}

void Target_Add(Target** list, Target* targ) {
    Target** scan;

    for (scan = list; *scan; scan = &(*scan)->next) {}

    *scan = targ;
}
