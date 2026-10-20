#ifndef COMPILER_IROLOOP_H
#define COMPILER_IROLOOP_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// IROLoop.flags. The LP_LOOP_*, LP_IFEXPR_NON_CANONICAL, LP_INDUCTION_NOT_FOUND and
// LP_HAS_MULTIPLE_INDUCTIONS names are the ones the empty-loop and unrolling passes' dumps print.
// The others are the Wii compilers': their flag dump lists the names in bit order, with the bits
// 2.6's dumps name at the same places once 0x80 is left out, and each fits where ExtractLoopInfo
// sets it.
typedef enum IROLoopFlags {
    LP_INDUCTION_AT_LEFT = 1,
    LP_LOOP_HAS_CALLS = 2,
    LP_LOOP_HAS_CNTRLFLOW = 4,
    LP_HAS_NONASSIGN = 8,
    LP_INDUCTION_NOT_FOUND = 0x10,
    LP_IFEXPR_NON_CANONICAL = 0x20,
    LP_HAS_MULTIPLE_INDUCTIONS = 0x40,
    LP_LOOP_HDR_HAS_SIDEEFFECTS = 0x80,
    LP_LOOP_STEP_ISADD = 0x100,
    LP_HEADER_FOLLOWS_UPDATE = 0x200,
    LP_LOOP_STEP_ISPOS = 0x400,
    LP_IND_USED_IN_LOOP = 0x800,
    LP_HAS_MULTIPLE_EXITS = 0x1000,
    LP_STEP_IS_NEG_ONE = 0x2000,
    LP_LOOP_STEP_ISNEG = 0x4000,
    LP_LOOP_HAS_ASM = 0x8000,
    LP_WHILE_LOOP = 0x10000, // the test is the induction's own ++ or -- (`while (n--)`)
    LP_RECURSIVE_LOOP = 0x20000, // the header is its own only in-loop successor
    LP_IS_REDUCTION_CAND = 0x40000
} IROLoopFlags;

typedef enum IROLoopIndFlags {
    LoopInd_HasMod = 1,
    LoopInd_HasDiv = 2
} IROLoopIndFlags;

#pragma pack(push, 2)
struct IROLoopInd {
    IROLoopIndFlags flags;
    VarRecord* var;
    IRONode* fnode;
    IROLinear* nd;
    SInt32 addConst;
    IROLinear* addNode;
    IROLoopInd* next;
};

// NOTE: invented names: unused, preheader, body, init, cond, lo, hi, iterCount, finalStoreVal (from
// IroEmptyLoop's dump: "# of iterations", "FinalStoreVal"), weight
// A loop's analysis (ExtractLoopInfo): its blocks, its test and the induction variable the test
// uses.
struct IROLoop {
    SInt32 flags;
    IRONode* fnode;
    int unused; // ExtractLoopInfo clears it, nothing reads it
    IRONode* preheader; // set by UnrollStandardLoop, never read
    IRONode* body; // set by UnrollStandardLoop, never read
    IROLinear* init; // the assignment giving the induction its initial value
    IROLinear* cond; // the loop test's condition
    IROLoopInd* induction;
    // the index range of the induction update's tree
    int lo;
    int hi;
    CInt64 iterCount;
    CInt64 finalStoreVal;
    int weight; // the node count ComputeIntWeight adds up, for the unroller
};
#pragma pack(pop)

extern IRONode* LoopNode;
extern Boolean ConditionalHeaderAtBottom;
extern IROLoopInd* FirstInd;
extern BitVector* InLoop;

extern void FindMustReach(void);
extern void AddPreds(IRONode* fnode);
extern void IRO_SetLoopDepth(void);
extern void IRO_FindLoops(void);
extern void ComputeLoopKills(void);
extern void ComputeLoopInvariance(void);
extern void ComputeLoopInduction(void);
extern void FindAssignmenttoInductionVar(IROLoop* loop, IRONode* fnode);
extern IROLoop* ExtractLoopInfo(IRONode* fnode);
extern CLabel* BuildLabel(IROList* list);

#ifdef __cplusplus
}
#endif

#endif
