#ifndef COMPILER_IROPTIMIZER_H
#define COMPILER_IROPTIMIZER_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Boolean DoLinearize;
extern Boolean VectorPhaseCalledFromUnroll;
extern Boolean IRO_Log;

extern Statement* IRO_Optimizer(Object* func, Statement* statements);
extern void IRO_Setup(void);
extern void CodeGen_UpdateOptimizerOptions(void);

#ifdef __cplusplus
}
#endif

#endif
