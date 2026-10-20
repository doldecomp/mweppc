#ifndef DRIVER_OPTIMIZERHELPERS_H
#define DRIVER_OPTIMIZERHELPERS_H

#include "driver/parser.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Pragma irPragmas[];

extern int SetPragmaOptimizationsToUnspecified(
    const char* opt, void* str, const char* param, int flags
);
extern int SetOptFlags(const char* opt, void* str, const char* param, int flags);
extern int DisplayOptimizationOptions(const char* opt, void* str, const char* param, int flags);

#ifdef __cplusplus
}
#endif

#endif
