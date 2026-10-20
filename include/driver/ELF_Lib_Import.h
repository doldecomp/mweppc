#ifndef DRIVER_ELF_LIB_IMPORT_H
#define DRIVER_ELF_LIB_IMPORT_H

#include <setjmp.h>
#include "compiler/common.h"
#include "driver/CWPlugins.h"

#ifdef __cplusplus
extern "C" {
#endif

// The linker's state (0x598 bytes): the compiler's importer clears it and uses only
// nolinkwarnings, set from the linker panel's PLinker.suppresswarn, which keeps linkwarning quiet.
// The type's and nolinkwarnings' names are Wii's. NOTE: unknown000 and unknown291 stand for the
// rest, which the importer never touches (Wii's later LinkInfo puts a short, ten file specs, five
// creators and types and four flags before nolinkwarnings; the 0x290 here fits nine 70-byte file
// specs, so which one is missing, and the members' places, are unknown)
typedef struct LinkInfo {
    UInt8 unknown000[0x290];
    UInt8 nolinkwarnings;
    UInt8 unknown291[0x307];
} LinkInfo;

extern jmp_buf linkerexit;
extern LinkInfo linkinfo;
// (the linker diagnostics' counts: Wii's names, which fit the COMMONs' hash order)
extern SInt32 linkererrors;
extern SInt32 linkerwarnings;

extern void ReportLinkerError(char* message, char* line2);
extern void ReportLinkerWarning(char* message, char* line2);
extern CWPLUGIN_ENTRY(ELFImport_main)(CWPluginPrivateContext* context);
extern void MyAlert(char* message);
extern void OSAlert(char* message, short error);

#ifdef __cplusplus
}
#endif

#endif
