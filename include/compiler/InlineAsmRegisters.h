#ifndef COMPILER_INLINEASMREGISTERS_H
#define COMPILER_INLINEASMREGISTERS_H

#include "compiler/common.h"
#include "compiler/InlineAsm.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void InlineAsm_InitializeRegisters(void);
extern void InlineAsm_InsertRegister(char* name, RegClass rclass, short num, Object* object);
extern IARegister* InlineAsm_LookupRegister(char* name);

#ifdef __cplusplus
}
#endif

#endif
