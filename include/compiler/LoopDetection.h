#ifndef COMPILER_LOOPDETECTION_H
#define COMPILER_LOOPDETECTION_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// 2-byte (mac68k) packing: a BasicInductionVar is 26 bytes (addbasicinductionvariable's oalloc)
#pragma pack(push, 2)
struct BlockList {
    BlockList* next;
    PCodeBlock* block;
};

struct InstrList {
    InstrList* next;
    PCode* instr;
};

// how a counting loop's lower or upper bound is known (Loop.lowerType, .upperType)
typedef enum LoopBound {
    LOOP_BOUND_INDETERMINATE,
    LOOP_BOUND_CONSTANT,
    LOOP_BOUND_VARIABLE
} LoopBound;

// NOTE: invented names: inductionUpdate, exitblocks, exit_dominators, backedge_dominators,
// has_call, uses_count_register, is_straight_line, has_unrolled_code, has_indexed_load,
// has_indexed_store, has_memory_barrier
struct Loop {
    Loop* parent;
    Loop* nextSibling;
    Loop* children;
    PCodeBlock* body; // repeated block
    PCodeBlock* preheader; // block at the start of the loop
    PCodeBlock* footer; // block at the end of the loop
    PCode* inductionUpdate; // the addi stepping a counting loop
    BlockList* blocks;
    UInt32* memberblocks;
    UInt32* exitblocks; // the blocks with a successor outside the loop
    UInt32* exit_dominators; // the blocks that dominate every exit block
    UInt32* backedge_dominators; // the blocks that dominate every back edge
    BasicInductionVar* basicInductionVars;
    int loopWeight;
    SInt32 bodySize; // the instructions in the body
    SInt32 iterationCount;
    SInt32 lower;
    SInt32 upper;
    SInt32 step; // the value added in each iteration
    UInt8 unknownCondition; // the test (ELESS ...) of a counting loop with an unknown count
    Boolean has_call;
    Boolean uses_count_register;
    Boolean is_straight_line; // every block but the body has one predecessor and one successor
    Boolean isUnknownCountingLoop; // is a counting loop with non-constant iteration count
    Boolean isKnownCountingLoop;
    Boolean has_unrolled_code; // a block has fUnrolled (code the front end unrolled)
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
    InstrList* instrs; // the instructions that step it. NOTE: invented name
    PCode* initializer;
    SInt32 step;
    short reg;
};

// NOTE: invented names: replacement_instruction, outer_loop, base_operand, source_operand,
// source_register, destination_register
struct InductionVar {
    InductionVar* next;
    BasicInductionVar* basicVar;
    PCode* instr;
    PCode* replacement_instruction; // the instruction initializeinductionvariable generates
    // for an indexed load or store, the outermost loop in which the index's other operand is
    // invariant and which does not hold the basic variable's initializer: the initialization goes
    // in its preheader
    Loop* outer_loop;
    SInt32 step;
    short base_operand; // the index in instr of the base register operand
    short source_operand; // the index in instr of the source operand
    short source_register; // the register initializeinductionvariable sets up
    short destination_register; // the register the reduced value is kept in
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
