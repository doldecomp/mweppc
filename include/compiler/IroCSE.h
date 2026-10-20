#ifndef COMPILER_IROCSE_H
#define COMPILER_IROCSE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
struct IROExpr {
    Boolean flags;
    UInt16 index;
    IROLinear* linear;
    Object* temp;
    IRONode* node;
    BitVector* depends;
    IROExpr* use;
    Boolean couldError;
    Boolean notSubable;
    IROLinear* factor;
    VarRecord* var;
    IROLinear* indOperand;
    IROExpr* next;
};
#pragma pack(pop)

extern BitVector* IRO_Depends;
extern Boolean IRO_NotSubable;
extern Boolean IRO_IsVolatile;
extern Boolean IRO_CouldError;
extern IROExpr* IRO_FirstExpr;
extern IROExpr* IRO_LastExpr;
extern SInt32 IRO_NumExprs;

extern void IRO_FindDepends_NoAlloc(IROLinear* linear);
extern void IRO_FindDepends(IROLinear* linear);
extern void IRO_FindExpressions(BitVector* bv, Boolean unused);
extern void IRO_RemoveExpr(IROExpr* expr);
extern void IRO_ComputeAvail(void);
extern void IRO_CommonSubs(void);

#ifdef __cplusplus
}
#endif

#endif
