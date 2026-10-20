#ifndef COMPILER_INLINEASMREGISTERSPPC_H
#define COMPILER_INLINEASMREGISTERSPPC_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern IARegister* InlineAsm_LookupRegisterPPCName(HashNameNode* name);
extern IARegister* InlineAsm_LookupRegisterPPC(char* name);
extern IARegister* InlineAsm_LookupDCRRegister(char* name);
extern void InlineAsm_InitializeRegistersPPC(void);

#ifdef __cplusplus
}
#endif

#endif
