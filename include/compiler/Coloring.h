#ifndef COMPILER_COLORING_H
#define COMPILER_COLORING_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern RegClass coloring_class;

extern void colorinstructions(Object* proc);

#ifdef __cplusplus
}
#endif

#endif
