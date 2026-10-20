#ifndef COMPILER_LOOPDETECTION_H
#define COMPILER_LOOPDETECTION_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
struct BlockList {
    BlockList* next;
    PCodeBlock* block;
};

struct InstrList {
    InstrList* next;
    PCode* instr;
};

typedef enum LoopBound {
    LOOP_BOUND_INDETERMINATE,
    LOOP_BOUND_CONSTANT,
    LOOP_BOUND_VARIABLE
} LoopBound;

struct Loop {
    Loop* parent;
    Loop* nextSibling;
    Loop* children;
    PCodeBlock* body;
    PCodeBlock* preheader;
    PCodeBlock* footer;
    PCode* inductionUpdate;
    BlockList* blocks;
    UInt32* memberblocks;
    UInt32* exitblocks;
    UInt32* exit_dominators;
    UInt32* backedge_dominators;
    BasicInductionVar* basicInductionVars;
    int loopWeight;
    SInt32 bodySize;
    SInt32 iterationCount;
    SInt32 lower;
    SInt32 upper;
    SInt32 step;
    UInt8 unknownCondition;
    Boolean has_call;
    Boolean uses_count_register;
    Boolean is_straight_line;
    Boolean isUnknownCountingLoop;
    Boolean isKnownCountingLoop;
    Boolean has_unrolled_code;
    Boolean has_indexed_load;
    Boolean has_indexed_store;
    LoopBound lowerType;
    LoopBound upperType;
    Boolean has_memory_barrier;
};

struct BasicInductionVar {
    BasicInductionVar* next;
    Loop* loop;
    InductionVar* inductionVars;
    InstrList* instrs;
    PCode* initializer;
    SInt32 step;
    short reg;
};

struct InductionVar {
    InductionVar* next;
    BasicInductionVar* basicVar;
    PCode* instr;
    PCode* replacement_instruction;
    Loop* outer_loop;
    SInt32 step;
    short base_operand;
    short source_operand;
    short source_register;
    short destination_register;
};
#pragma pack(pop)

extern Loop* loopsinflowgraph;
extern int loopdetection_nblocks;
extern struct BitVector* LoopTemp;
extern struct LoopList* LoopList_First;

extern void addblocktoloop(Loop* loop, PCodeBlock* block);
extern void insertpreheaderblock(Loop* loop);
extern void findloopsinflowgraph(void);
extern void analyzeForCountableLoops(Loop* loop);
extern void analyzeloop(Loop* loop);
extern void analyzeloopsinflowgraph(void);

#ifdef __cplusplus
}
#endif

#endif
