#ifndef COMPILER_INLINEASMMNEMONICSPPC_H
#define COMPILER_INLINEASMMNEMONICSPPC_H

#include "compiler/common.h"
#include "compiler/InlineAsm.h"

#ifdef __cplusplus
extern "C" {
#endif

extern IAMnemonic* InlineAsm_LookupMnemonicPPC(char* name);
extern void InlineAsm_InitializeMnemonicsPPC(void);

#ifdef __cplusplus
}
#endif

#endif
