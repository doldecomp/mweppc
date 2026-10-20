#ifndef COMPILER_COPYPROPAGATION_H
#define COMPILER_COPYPROPAGATION_H

// CopyPropagation.c: the generic propagation pass (propagateinstructions), which copy, add and
// load-immediate propagation and the alias pass each run with a Propagation record of their own,
// and copy propagation itself

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*IsCandidateFunc)(PCode* pcode);
typedef int (*PropagatesToUseFunc)(int candidateID, int useID);
typedef void (*PropagateAndFinishFunc)(int candidateID);

// 2-byte packing: a Propagation is 30 bytes in the records' .data
#pragma pack(push, 2)
typedef struct Propagation {
    IsCandidateFunc is_candidate;
    PropagatesToUseFunc propagatestouse;
    PropagateAndFinishFunc propagateandfinish;
    char* name;
    char* nameplural;
    char* format;
    UInt32 mask; // a candidate with one of these instruction flags is not propagated
    Boolean computesUseDefChains;
} Propagation;
#pragma pack(pop)

struct Candidate {
    PCode* pcode;
    RegUseOrDef* list;
};

// NOTE: invented names: gen, kill, in, out
// the dataflow sets of a block
struct PropInfo {
    UInt32* gen;
    UInt32* kill;
    UInt32* in;
    UInt32* out;
};

extern int propagatedcopies;
extern int propagated_instructions;
extern int recursive_propagation;
extern SInt32 number_of_candidates;
extern Candidate* Candidates;
extern PropInfo* propinfo;

extern void propagateinstructions(
    Object* proc, Propagation* config, int passCount, Boolean localflag
);
extern void propagatecopyinstructions(Object* proc, int moreAggressive);
#if VERSION < VERSION_GC_2_5
// (2.5 and later have it in UseDefChains.c)
extern int precedes(PCode* a, PCode* b);
#endif

#ifdef __cplusplus
}
#endif

#endif
