#ifndef DRIVER_PARSERHELPERS_CC_H
#define DRIVER_PARSERHELPERS_CC_H

#include "compiler/common.h"
#include "driver/parser.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Handle definesHandle;

extern int Opt_AddStringToDefines(const char* opt, void* str, const char* param, int flags);
extern int Opt_DefineSymbol(const char* name, const char* value);
extern int Opt_UndefineSymbol(const char* opt, void* var, const char* arg, int flags);
extern int Opt_AddPrefixFile(const char* opt, void* handle, const char* filename, int flags);
extern int SetupPragmas(const Pragma* pragmas);

#ifdef __cplusplus
}
#endif

#endif
