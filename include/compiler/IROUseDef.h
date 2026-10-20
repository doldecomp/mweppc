#ifndef COMPILER_IROUSEDEF_H
#define COMPILER_IROUSEDEF_H

#include "compiler/common.h"
#include "compiler/enode.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
struct IROUse {
    SInt32 index;
    IRONode* node;
    IROLinear* linear;
    VarRecord* var;
    IROUse* globalnext;
    IROUse* varnext;
    BitVector* reachingDefs;
    UInt16 reachingDefCount;
};
struct IRODef {
    SInt32 index;
    IRONode* node;
    IROLinear* linear;
    VarRecord* var;
    IRODef* globalnext;
    IRODef* varnext;
    UInt16 useCount;
    Boolean global;
    Boolean noregister;
    Boolean definite;
    Boolean removable;
};
#pragma pack(pop)

extern ENodeType IRO_NonAssignmentOp[MAXEXPR];
extern IROUse* IRO_FirstVarUse;
extern IROUse* IRO_LastVarUse;

extern CInt64 IRO_GetSelfAssignmentVal(IROLinear* linear);
extern void IRO_InitializeNonAssignmentOpArray(void);
extern void IRO_InitializeAssignmentFoldingFunctionArray(void);
extern Boolean IRO_UseDef(Boolean optDeadAssignments, Boolean optPropagation);
extern void IRO_SplitLifetimes(void);

#ifdef __cplusplus
}
#endif

#endif
