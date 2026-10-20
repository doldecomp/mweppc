#ifndef COMPILER_GCCINLINEASM_H
#define COMPILER_GCCINLINEASM_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Statement* first_ST_ASM;

extern void InlineAsm_gcc_parse(void);

#ifdef __cplusplus
}
#endif

#endif
