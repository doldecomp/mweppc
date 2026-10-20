#ifndef COMPILER_IROCSE_H
#define COMPILER_IROCSE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
// NOTE: invented names: flags, temp, use, factor, var, indOperand
struct IROExpr {
    // cleared by IRO_AddExpression and nothing sets a bit, but IroLoop skips an expression with
    // bit 4 set
    Boolean flags;
    UInt16 index;
    IROLinear* linear;
    Object* temp; // the temporary holding the expression's value
    IRONode* node;
    BitVector* depends;
    IROExpr* use; // the available expression this one is replaced with
    // IRO_AddExpression copies IRO_CouldError (unknown indirection or possibly zero divisor);
    // IroLoop requires mustreach before hoisting
    Boolean couldError;
    // IRO_AddExpression copies IRO_NotSubable (volatile access, effectful operator or call);
    // IroCSE excludes these candidates
    Boolean notSubable;
    // Reducable's results for strength reduction: the factor, the induction variable and the
    // operand that reads it
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
