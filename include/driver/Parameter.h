#ifndef DRIVER_PARAMETER_H
#define DRIVER_PARAMETER_H

#include "compiler/common.h"
#include "driver/option_system.h"

#ifdef __cplusplus
extern "C" {
#endif

extern char curparam[4096];

extern void Param_DescHelp(
    PARAM_T* param, const char** desc, const char** help, const char** defaul
);
extern int Param_Compare(PARAM_T* param);
extern int Params_Parse(PARAM_T* param, int flags);
extern void Param_Error(VarArgShort id, ...);

#ifdef __cplusplus
}
#endif

#endif
