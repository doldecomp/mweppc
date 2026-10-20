#ifndef COMPILER_INTERFERENCEGRAPH_H
#define COMPILER_INTERFERENCEGRAPH_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    fSpilled = 1,
    fPushed = 2,
    fCoalesced = 4,
    fCoalescedInto = 8,
    fPairHigh = 0x10,
    fPairLow = 0x20,
    fMultipleDefs = 0x40
};

enum {
    fPairedSingle1 = 0x800,
    fPairedSingle0 = 0x1000
};

typedef struct IGNode {
    struct IGNode* next;
    Object* spillTemporary;
    PCode* instr;
    int spillCost;
    short reg;
    short degree;
    short color;
    UInt16 flags;
    short arraySize;
    short array[1];
} IGNode;

extern IGNode** interferencegraph;

extern void buildinterferencegraph(Object* proc);

#ifdef __cplusplus
}
#endif

#endif
