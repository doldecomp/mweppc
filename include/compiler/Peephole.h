#ifndef COMPILER_PEEPHOLE_H
#define COMPILER_PEEPHOLE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void peepholeoptimizeforward(Object* func);
extern void peepholemergeblocks(Object* func, Boolean mergePrologEpilog);
extern void peepholeoptimizepcode(Object* func);

#ifdef __cplusplus
}
#endif

#endif
