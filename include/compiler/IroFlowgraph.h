#ifndef COMPILER_IROFLOWGRAPH_H
#define COMPILER_IROFLOWGRAPH_H

#include "compiler/common.h"
#include "compiler/CError.h"
#include "compiler/CompilerTools.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
// NOTE: invented names: unused, copyOut, reachable, visited, referenced, marked
struct IRONode {
    UInt16 index;
    UInt16 numsucc;
    UInt16* succ;
    UInt16 numpred;
    UInt16* pred;
    IROLinear* first;
    IROLinear* last;
    // The data-flow sets, named as IRO_DumpDf labels them
    BitVector* in;
    BitVector* out;
    BitVector* gen;
    BitVector* kill;
    UInt32 unused; // only StartNode writes it, nothing reads it
    // IroPropagate's available-copies out set, dumped as "AA"
    BitVector* copyOut;
    BitVector* dom;
    IRONode* nextnode;
    Boolean reachable; // set by IRO_MakeReachable
    Boolean visited; // IRO_MakeReachable's worklist mark
    Boolean mustreach;
    Boolean referenced; // a label or catch entry jumps here
    UInt16 loopdepth;
    // a visited mark (IroExprRegeneration, the pointer analysis)
    Boolean marked;
    struct ObjectSet* addressed;
    Boolean mustreach1;
};

// NOTE: invented names: IRONodes, IRO_FNodeWorkStack_Init, IRO_FNodeWorkStack_Free,
// IRO_FNodeWorkStack_Count, IRO_FNodeWorkStack_Remove
// A queue of flowgraph node indices (IroExprRegeneration's worklist). The original's out-of-line
// IRO_FNodeWorkStack_Add asserts with this header's name (line 93), so these are the header's
// inlines; Wii names that one, and the others are named after it.
typedef struct IRONodes {
    UInt16* indices;
    UInt16 num;
    UInt16 base;
} IRONodes;
#pragma pack(pop)

extern UInt16 IRO_NumNodes;
extern IRONode* IRO_FirstNode;
extern IRONode* IRO_LastNode;
extern IRONode* IRO_EndNode;
extern IRONode** IRO_NodeTable;
extern BitVector* IRO_VarKills;
extern BitVector* IRO_Avail;
extern BitVector* IRO_FuncKills;
extern BitVector* IRO_ExprKills;

extern void IRO_BuildFlowgraph(IROLinear* linear);
extern void IRO_ComputeDom(void);
extern void IRO_ComputeSuccPred(void);
extern IRONode* IRO_NewFlowGraphNode(void);
extern IRONode* IRO_MergeFlowGraphNodes(IRONode* a, IRONode* b);

HEADER_INLINE void IRO_FNodeWorkStack_Init(IRONodes* nodes) {
    nodes->indices = oalloc(sizeof(UInt16) * IRO_NumNodes);
    nodes->num = 0;
    nodes->base = 0;
}

HEADER_INLINE void IRO_FNodeWorkStack_Free(IRONodes* nodes) {
    (void)nodes;
}

HEADER_INLINE UInt16 IRO_FNodeWorkStack_Count(IRONodes* nodes) {
    return nodes->num;
}

HEADER_INLINE UInt16 IRO_FNodeWorkStack_Remove(IRONodes* nodes) {
    UInt16 result = -1;
    if (nodes->num) {
        result = nodes->indices[nodes->base];
        nodes->base = (nodes->base + 1) % IRO_NumNodes;
        nodes->num--;
    }
    return result;
}

HEADER_INLINE void IRO_FNodeWorkStack_Add(IRONodes* nodes, UInt16 index) {
    if (nodes->num < IRO_NumNodes) {
        nodes->indices[(nodes->base + nodes->num) % IRO_NumNodes] = index;
        nodes->num++;
    } else {
        CError_FATAL(93);
    }
}

#ifdef __cplusplus
}
#endif

#endif
