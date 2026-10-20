#ifndef DRIVER_STDTARGETWARNINGHELPERS_CC_H
#define DRIVER_STDTARGETWARNINGHELPERS_CC_H

#include "compiler/common.h"
#include "driver/parser.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int TargetSetWarningFlags(short val, Boolean set);
extern void TargetDisplayWarningOptions(Handle txt);
extern Pragma warningPragmas[];

#ifdef __cplusplus
}
#endif

#endif
