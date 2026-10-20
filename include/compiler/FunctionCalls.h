#ifndef COMPILER_FUNCTIONCALLS_H
#define COMPILER_FUNCTIONCALLS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void call_function(ENode* expr, Operand* output);

#ifdef __cplusplus
}
#endif

#endif
