#ifndef COMPILER_IROPROPAGATE_H
#define COMPILER_IROPROPAGATE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2) // the original's 68K alignment
struct IROAssign {
    IROLinear* linear;
    UInt16 index;
    UInt16 varIndex;
    IROLinear* linear2;
    BitVector* depends;
    Object* varObj;
    VarRecord* var;
    IROAssign* next;
    IROAssign* prev;
    UInt16 useCount; // NOTE: invented name (IRO_ExpressionPropagation counts the variable's uses)
    IRONode* node;
};
#pragma pack(pop)

extern IROAssign* IRO_FirstAssign;
extern IROAssign* IRO_LastAssign;

extern int IRO_IsRegable(Object* obj);
extern Boolean IRO_CopyAndConstantPropagation(void);
extern void IRO_ExpressionPropagation(void);

#ifdef __cplusplus
}
#endif

#endif
