#ifndef DRIVER_CCOMPILER_H
#define DRIVER_CCOMPILER_H

#include "compiler/common.h"
#include "driver/CWPlugins.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Boolean systemHandles;
extern Boolean crippled;
extern CompilerLinkerParamBlk cparams;

extern CWPLUGIN_ENTRY(MWC_main)(CWPluginContext context);
extern void PrintProgressFunction(const char* str);

#ifdef __cplusplus
}
#endif

#endif
