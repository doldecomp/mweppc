#ifndef COMPILER_INLINEASM_H
#define COMPILER_INLINEASM_H

#include <setjmp.h>
#include "compiler/common.h"
#include "compiler/PCodeInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
// NOTE: invented names: opcode, instruction
// A mnemonic of the assembler's table (InlineAsmMnemonicsPPC.c)
struct IAMnemonic {
    char* name;
    // the opcodeinfo index
    UInt32 opcode;
    char* format;
    UInt32 cpu;
    // the instruction word, its low bits flagging the forms (1 link, 2 absolute, 0x400 overflow)
    //
    UInt32 instruction;
};

// A register name the assembler knows (InlineAsmRegisters.c)
struct IARegister {
    char* name;
    RegClass rclass;
    SInt32 num;
    struct Object* object;
};

typedef enum IAOperandType {
    // NOTE: invented names: IAOpnd_Placeholder, GlobalObj, LocalObj, GCCReg and GCCImm
    IAOpnd_Placeholder, // no operand (PCOp_PLACEHOLDEROPERAND)
    IAOpnd_Imm,
    IAOpnd_Reg,
    IAOpnd_GlobalObj, // an object other than a local, with a relocation (u.obj)
    IAOpnd_LocalObj, // a local (u.obj)
    IAOpnd_Lab,
    IAOpnd_GCCReg, // u.gccreg
    IAOpnd_GCCImm, // u.gccimm
    IAOpnd_LabDiff
} IAOperandType;

// NOTE: invented names: reftype, gccreg, gccimm, kind (both), and gccimm's effect and rclass
typedef struct IAOperand {
    IAOperandType type;
    Boolean negated;
    union {
        // IAOpnd_Imm
        struct {
            SInt32 value;
        } imm;
        // IAOpnd_Reg
        struct {
            // -3 for an @hiword reference to a register pair's high half
            SInt32 num;
            struct Object* object;
            short effect;
            short rclass;
        } reg;
        // IAOpnd_GlobalObj, IAOpnd_LocalObj
        struct {
            SInt32 offset;
            struct Object* obj;
            // the relocation: an IAExprType, which numbers them as PCodeArg.arg does for a memory
            // operand
            short reftype;
        } obj;
        // IAOpnd_Lab
        struct {
            struct CLabel* label;
        } lab;
        // IAOpnd_GCCReg and IAOpnd_GCCImm: a GCC-style `%N` operand, the Nth of the asm
        // statement's operand list, replaced once the list is known (GCCInlineAsm.c). The two
        // share a layout: `kind` is 2 for a register and 1 for an immediate.
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
        // IAOpnd_LabDiff
        struct {
            struct CLabel* label1;
            struct CLabel* label2;
            SInt32 offset;
        } labdiff;
    } u;
} IAOperand;

// NOTE: invented names: IAExprType, IAExpr_None, IAExpr_Local16, IAExpr_Addr16, IAExpr_Lo16,
// IAExpr_Branch, IAExpr_Value, IAExpr_Lo, IAExpr_Hi, IAExpr_Ha, IAExpr_AbsBranch, IAExpr_LoAlt
// An assembler expression's kind, also an object operand's relocation (IAOperand.u.obj.reftype,
// numbered as RefType, PCodeInfo.h, whose comments say what each is)
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

// NOTE: invented names: var, var2, label, label2
typedef struct IAExpr {
    IAExprType type;
    UInt8 flags;
    short reg; // the base register of `var` (1 for a local)
    SInt32 value;
    struct Object* object; // a register variable pointing to a struct, the base of `value`
    // `var - var2 + value`, `label - label2 + value`
    struct Object* var;
    struct Object* var2;
    struct CLabel* label;
    struct CLabel* label2;
} IAExpr;

// NOTE: invented names: IAFlag_Directive, IAFlag_Call
// InlineAsm.flags: a directive, or an instruction that calls (a branch to a function, a trap, sc);
// on IAExpr.flags, IAFlag_Directive marks a label (nothing tests it)
enum {
    IAFlag_Directive = 1,
    IAFlag_Call = 2
};

// NOTE: invented names: IAFlagsB_Record, IAFlagsB_Overflow, IAFlagsB_Absolute, IAFlagsB_Link,
// IAFlagsB_HintPlus, IAFlagsB_HintMinus, IAFlagsB_Volatile
// InlineAsm.flags2: the mnemonic's suffixes and `volatile`
enum {
    IAFlagsB_Record = 1, // `.`
    IAFlagsB_Overflow = 2, // `o`
    IAFlagsB_Absolute = 4, // `a`
    IAFlagsB_Link = 8, // `l`
    IAFlagsB_HintPlus = 0x10, // a conditional branch's `+`
    IAFlagsB_HintMinus = 0x20, // `-`
    IAFlagsB_Volatile = 0x40 // keep the instruction as written (side effects)
};

typedef struct InlineAsm {
    Opcode opcode;
    unsigned char flags;
    unsigned char flags2;
    short argcount;
    IAOperand args[0];
} InlineAsm;

// What InlineAsm_LookupSymbol found for a name
typedef struct IALookupResult {
    struct HashNameNode* name;
    struct CLabel* label;
    struct Object* object;
    struct Type* type;
    UInt32 value;
    Boolean has_value;
} IALookupResult;

// NOTE: invented names: IAEntryPoint, opcode, flags, argcount, object, size (those of the InlineAsm
// fields they overlay)
// An `entry` directive: an InlineAsm with opcode IADirective_Entry, written through its InlineAsm
// header
typedef struct IAEntryPoint {
    short opcode;
    char flags;
    int argcount;
    struct Object* object;
    SInt32 size;
} IAEntryPoint;

// NOTE: invented names: IAEffectType, IAEffect_Read, IAEffect_Write, IAEffect_Clobber,
// IAEffect_AddressOf, IAEffect_ReadWrite
// What an asm instruction does to a variable (CodeGen_GetAsmEffects, for the IR optimizer's use-def
// analysis)
typedef enum IAEffectType {
    IAEffect_Read, // a use
    IAEffect_Write, // a definition, of all of it when it covers the variable's size
    IAEffect_Clobber, // a partial definition (the assembler sets none)
    IAEffect_AddressOf, // its address taken: an operand neither read nor written (`la r3, x`)
    IAEffect_ReadWrite // a use and a definition (the assembler sets none)
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

// NOTE: invented names: optimizationBarrier, writesMemory, readsMemory, unmodeledControlFlow,
// branchWithLink, noFallthrough
struct IAEffects {
    // an optimization barrier (side effects, or a branch to an absolute address)
    UInt8 optimizationBarrier;
    // set for an instruction writing memory; IroVars invalidates noregister variables as for
    // branchWithLink
    UInt8 writesMemory;
    // set for an instruction reading memory; IROUseDef marks reaching callDefs
    UInt8 readsMemory;
    // indirect branches, label differences, branches without labels
    UInt8 unmodeledControlFlow;
    UInt8 branchWithLink;
    // a `b`, which has no fallthrough
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

// NOTE: invented names: InlineAsm_InitExpr5, InlineAsm_CheckExpr, InlineAsm_GetExprValue (inlined
// everywhere, they leave none in the binary)
// Expression helpers
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
