#ifndef COMPILER_DUMPIR_H
#define COMPILER_DUMPIR_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void CleanupDumpIR(void);
extern void DumpIR(Statement* statements, Object* func);

#ifdef __cplusplus
}
#endif

#endif
