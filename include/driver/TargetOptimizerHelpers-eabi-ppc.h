#ifndef DRIVER_TARGETOPTIMIZERHELPERS_EABI_PPC_H
#define DRIVER_TARGETOPTIMIZERHELPERS_EABI_PPC_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int TargetSetOptFlags(short val, Boolean set);
extern void TargetDisplayOptimizationOptions(Handle txt);
extern void TargetSetPragmaOptimizationsToUnspecified(void);

#ifdef __cplusplus
}
#endif

#endif
