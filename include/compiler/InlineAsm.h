#ifndef COMPILER_INLINEASM_H
#define COMPILER_INLINEASM_H

#include <setjmp.h>
#include "compiler/common.h"
#include "compiler/PCodeInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
struct IAMnemonic {
    char* name;
    UInt32 opcode;
    char* format;
    UInt32 cpu;
    UInt32 instruction;
};

struct IARegister {
    char* name;
    RegClass rclass;
    SInt32 num;
    struct Object* object;
};

typedef enum IAOperandType {
    IAOpnd_Placeholder,
    IAOpnd_Imm,
    IAOpnd_Reg,
    IAOpnd_GlobalObj,
    IAOpnd_LocalObj,
    IAOpnd_Lab,
    IAOpnd_GCCReg,
    IAOpnd_GCCImm,
    IAOpnd_LabDiff
} IAOperandType;

typedef struct IAOperand {
    IAOperandType type;
    Boolean negated;
    union {
        struct {
            SInt32 value;
        } imm;
        struct {
            SInt32 num;
            struct Object* object;
            short effect;
            short rclass;
        } reg;
        struct {
            SInt32 offset;
            struct Object* obj;
            short reftype;
        } obj;
        struct {
            struct CLabel* label;
        } lab;
        struct {
            SInt32 num;
            UInt8 kind;
            short effect;
            short rclass;
        } gccreg;
        struct {
            SInt32 value;
            Boolean kind;
            short effect;
            short rclass;
        } gccimm;
        struct {
            struct CLabel* label1;
            struct CLabel* label2;
            SInt32 offset;
        } labdiff;
    } u;
} IAOperand;

typedef enum IAExprType {
    IAExpr_None,
    IAExpr_Local16,
    IAExpr_Addr16,
    IAExpr_Lo16,
    IAExpr_Branch,
    IAExpr_Value,
    IAExpr_Lo,
    IAExpr_Hi,
    IAExpr_Ha,
    IAExpr_AbsBranch,
    IAExpr_LoAlt
} IAExprType;

typedef struct IAExpr {
    IAExprType type;
    UInt8 flags;
    short reg;
    SInt32 value;
    struct Object* object;
    struct Object* var;
    struct Object* var2;
    struct CLabel* label;
    struct CLabel* label2;
} IAExpr;

enum {
    IAFlag_Directive = 1,
    IAFlag_Call = 2
};

enum {
    IAFlagsB_Record = 1,
    IAFlagsB_Overflow = 2,
    IAFlagsB_Absolute = 4,
    IAFlagsB_Link = 8,
    IAFlagsB_HintPlus = 0x10,
    IAFlagsB_HintMinus = 0x20,
    IAFlagsB_Volatile = 0x40
};

typedef struct InlineAsm {
    Opcode opcode;
    unsigned char flags;
    unsigned char flags2;
    short argcount;
    IAOperand args[0];
} InlineAsm;

typedef struct IALookupResult {
    struct HashNameNode* name;
    struct CLabel* label;
    struct Object* object;
    struct Type* type;
    UInt32 value;
    Boolean has_value;
} IALookupResult;

typedef struct IAEntryPoint {
    short opcode;
    char flags;
    int argcount;
    struct Object* object;
    SInt32 size;
} IAEntryPoint;

typedef enum IAEffectType {
    IAEffect_Read,
    IAEffect_Write,
    IAEffect_Clobber,
    IAEffect_AddressOf,
    IAEffect_ReadWrite
} IAEffectType;

struct IAEffect {
    IAEffectType type;
    struct Object* object;
    SInt32 offset;
    SInt32 size;
};

enum {
    IAMaxOperands = 16,
    IAMaxLabels = 16
};

struct IAEffects {
    UInt8 optimizationBarrier;
    UInt8 writesMemory;
    UInt8 readsMemory;
    UInt8 unmodeledControlFlow;
    UInt8 branchWithLink;
    UInt8 noFallthrough;
    SInt32 numoperands;
    SInt32 numlabels;
    IAEffect operands[IAMaxOperands];
    struct CLabel* labels[IAMaxLabels];
};
#pragma pack(pop)

extern jmp_buf InlineAsm_assemblererror;
extern int allow_array_expressions;
extern Boolean InlineAsm_setjmp_valid;

extern void AssemblerError(void);
extern void InlineAsm_SyntaxError(short code);
extern CLabel* InlineAsm_DeclareLabel(HashNameNode* name);
extern Boolean InlineAsm_LookupSymbolOrTag(
    HashNameNode* name, IALookupResult* result, Boolean allow_tag
);
extern Boolean InlineAsm_LookupSymbol(HashNameNode* name, IALookupResult* result);
extern SInt32 InlineAsm_StructMemberOffset(Type* type);
extern SInt32 InlineAsm_StructArrayMemberOffset(Type* type);
extern SInt32 InlineAsm_StructPointerMemberOffset(Type* type);
extern void InlineAsm_ScanFunction(volatile short endToken);
extern void InlineAsm_Assemble(void);
extern void InlineAsm_PackAsmStatement(
    Statement* stmt, Statement* first, void** output, SInt32* outsize
);
extern void InlineAsm_UnpackAsmStatement(
    Statement* stmt, CLabel** labelArray, Boolean inlineCopy, void* data, SInt32 size
);
extern void InlineAsm_CheckLocalUsage(Statement* stmt);
extern CLabel* InlineAsm_GetReferencedLabel(Statement* stmt);
extern CLabel* InlineAsm_GetReferencedLabel2(Statement* stmt);
extern Object* InlineAsm_GetObjectOffset(void* p, SInt32 index, SInt32* offset);

HEADER_INLINE void InlineAsm_InitExpr5(IAExpr* expr, SInt32 value) {
    expr->type = IAExpr_Value;
    expr->flags = 0;
    expr->value = value;
    expr->reg = 0;
    expr->object = NULL;
    expr->var = NULL;
    expr->var2 = NULL;
    expr->label = NULL;
    expr->label2 = NULL;
}

HEADER_INLINE int InlineAsm_CheckExpr(IAExpr* expr) {
    return (expr->var == NULL && expr->object == NULL && expr->label == NULL);
}

HEADER_INLINE SInt32 InlineAsm_GetExprValue(IAExpr* expr) {
    switch (expr->type) {
        case IAExpr_Ha:
            return HIGH_PART(expr->value);
        case IAExpr_Hi:
            return (short)(expr->value >> 16);
        case IAExpr_Lo:
            return (short)expr->value;
        default:
            return expr->value;
    }
}

#ifdef __cplusplus
}
#endif

#endif
