#ifndef COMPILER_PCODEASSEMBLY_H
#define COMPILER_PCODEASSEMBLY_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct EntryPoint {
    struct EntryPoint* next;
    struct Object* object;
    struct PCodeBlock* block;
};

extern SInt32 assemblefunction(Object* func, EntryPoint* entrypoints);

#ifdef __cplusplus
}
#endif

#endif
