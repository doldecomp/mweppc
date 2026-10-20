#ifndef DRIVER_ELF_LIB_IMPORT_H
#define DRIVER_ELF_LIB_IMPORT_H

#include <setjmp.h>
#include "compiler/common.h"
#include "driver/CWPlugins.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LinkInfo {
    UInt8 unknown000[0x290];
    UInt8 nolinkwarnings;
    UInt8 unknown291[0x307];
} LinkInfo;

extern jmp_buf linkerexit;
extern LinkInfo linkinfo;
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
