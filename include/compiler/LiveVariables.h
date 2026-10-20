#ifndef COMPILER_LIVEVARIABLES_H
#define COMPILER_LIVEVARIABLES_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LiveInfo {
    UInt32* use;
    UInt32* def;
    UInt32* in;
    UInt32* out;
} LiveInfo;

extern LiveInfo* liveinfo;

extern void computelivevariables(Object* proc);
extern int dead(PCode* instr, RegClass rclass, UInt32* vec);

#ifdef __cplusplus
}
#endif

#endif
