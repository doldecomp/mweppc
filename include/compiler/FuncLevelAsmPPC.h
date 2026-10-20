#ifndef COMPILER_FUNCLEVELASMPPC_H
#define COMPILER_FUNCLEVELASMPPC_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void setup_assembly_argument(Object* obj, short reg);
extern void assign_local_addresses(void);
extern void Assembler(Object* func);
extern void SetupAssembler(void);
extern void CleanupAssembler(void);

#ifdef __cplusplus
}
#endif

#endif
