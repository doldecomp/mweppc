#ifndef COMPILER_COPTIMIZER_H
#define COMPILER_COPTIMIZER_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void SetVarUsage(Object* obj, Boolean noregister);
extern void COpt_SimpleOptimizer(Object* obj, Statement* stmt);
extern Statement* COpt_Optimizer(Object* obj, Statement* stmt);

#ifdef __cplusplus
}
#endif

#endif
