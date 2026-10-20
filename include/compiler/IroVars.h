#ifndef COMPILER_IROVARS_H
#define COMPILER_IROVARS_H

#include "compiler/common.h"
#include "compiler/enode.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2) // 2-byte (mac68k) alignment
// NOTE: invented names: unused, inductionState, noregister, usedAtCall, bitfieldType
struct VarRecord {
    UInt16 index;
    Object* object;
    int unused; // IRO_FindVar clears it, nothing reads it
    // IroLoop's induction states 0, 1 and 2
    Boolean inductionState;
    // the variable's address is taken: it cannot live in a register
    Boolean noregister;
    Boolean usedAtCall; // IROUseDef's MarkUses sets it at calls
    VarRecord* next;
    IRODef* defs;
    IROUse* uses;
    // the bit-field type IRO_RewriteBitFieldTemps' temporary stands for
    TypeBitfield* bitfieldType;
    // IRO_RewriteBitFieldTemps clones this expression to replace the bit-field temporary (NOTE:
    // invented name)
    IROLinear* bitFieldReplacement;
};
#pragma pack(pop)

extern VarRecord* IRO_FirstVar;
extern SInt32 IRO_NumVars;
extern Boolean IRO_IsBitField;
extern SInt32 IRO_BaseTerms;
extern SInt32 IRO_VarTerms;
extern Boolean IRO_IsModifyOp[MAXEXPR];
extern Boolean IRO_IsAssignOp[MAXEXPR];

extern void IRO_InitializeIRO_IsModifyOpArray(void);
extern void IRO_InitializeIRO_IsAssignOpArray(void);
extern VarRecord* IRO_FindVar(Object* object, Boolean create, Boolean allowRegister);
extern void ResetVars(void);
extern void IRO_FindAllVars(void);
extern void IRO_ZapVarPtrs(void);
extern void IRO_UpdateVars(void);
extern void IRO_AddElmToList(IROLinear* linear, IROElmList** list);
extern void IRO_DecomposeAddressExpression(IROLinear* linear, IROAddrRecord* rec);
extern void IRO_DecomposeAddressExpression_Cheap(IROLinear* linear);
extern VarRecord* IRO_FindAssigned(IROLinear* linear);
extern void IRO_GetKills(IROLinear* linear);
extern void IRO_CheckInit(void);
extern void IRO_RewriteBitFieldTemps(void);
extern void IRO_ScalarizeClassDataMembers(void);

#ifdef __cplusplus
}
#endif

#endif
