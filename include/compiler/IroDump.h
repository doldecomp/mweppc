#ifndef COMPILER_IRODUMP_H
#define COMPILER_IRODUMP_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void IRO_InitializeNodeNamesArray(void);
extern void IRO_DumpBits(char* name, BitVector* bv);
extern void IRO_DumpAfterPhase(char* str, Boolean dumpGraph);
extern void IRO_DumpFlowgraph(void);
extern void IRO_DumpExprs(void);
extern void IRO_SetupDump(void);
extern void IRO_CleanupDump(void);
extern void IRO_Dump(char* format, ...);
extern void IRO_SpellType(Type* type, char* buf);

#ifdef __cplusplus
}
#endif

#endif
