#ifndef COMPILER_IROUSEDEF_H
#define COMPILER_IROUSEDEF_H

#include "compiler/common.h"
#include "compiler/enode.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
// NOTE: invented names: reachingDefs, reachingDefCount
// A use of a variable (FindDefsAndUses; chained from IRO_FirstVarUse).
struct IROUse {
    SInt32 index;
    IRONode* node;
    IROLinear* linear;
    VarRecord* var;
    IROUse* globalnext;
    IROUse* varnext;
    BitVector* reachingDefs; // the definitions reaching it
    UInt16 reachingDefCount; // their count
};
// NOTE: invented names: useCount, global, noregister, definite, removable
// A definition of a variable (MakeDef; chained from FirstDef).
struct IRODef {
    SInt32 index;
    IRONode* node;
    IROLinear* linear;
    VarRecord* var;
    IRODef* globalnext;
    IRODef* varnext;
    UInt16 useCount; // the uses it reaches
    Boolean global; // of a global (Inline_IsObjectData), live at a call or return
    Boolean noregister; // MakeDef copies the variable's
    Boolean definite; // it defines all of the variable
    Boolean removable; // set by MarkRemovableLoopDefs
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
