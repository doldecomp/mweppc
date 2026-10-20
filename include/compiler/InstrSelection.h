#ifndef COMPILER_INSTRSELECTION_H
#define COMPILER_INSTRSELECTION_H

#include "compiler/common.h"
#include "compiler/Operands.h"
#include "compiler/PCodeInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PrecomputedOperand {
    struct PrecomputedOperand* next;
    SInt32 precompid;
    Operand operand;
} PrecomputedOperand;

extern PrecomputedOperand* precomputedoperands;
extern void (*cgdispatch[])(ENode*, short, short, Operand*);

extern void init_cgdispatch(void);
extern void gen_DEFINE(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_REUSE(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_POSTINCDEC(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_INDIRECT(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_MONMIN(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_BINNOT(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_FORCELOAD(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_MUL(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_DIV(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_MODULO(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_ADD(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_SUB(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_SHL(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_SHR(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_AND(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_XOR(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_OR(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_ASS(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern ENode* evaluate_and_skip_comma(ENode* expr);
extern void gen_COMMA(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_TYPCON(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_BITFIELD(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_INTCONST(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_FLOATCONST(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_STRINGCONST(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_COND(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_CONDASS(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_FUNCCALL(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_OBJREF(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_UNEXPECTED(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void binary_operator(
    Opcode opcode, ENode* left, ENode* right, short outputReg, Operand* output
);
extern void unary_operator(Opcode opcode, ENode* expr, short outputReg, Operand* output);
extern void fp_unary_operator(Opcode opcode, ENode* expr, short outputReg, Operand* output);
extern void fp_multiply_add(
    Opcode opcode, ENode* a, ENode* b, ENode* c, short outputReg, Operand* output
);
extern void gen_COMPARE(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_LOGICAL(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_NULLCHECK(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_PRECOMP(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void logical_expression(
    ENode* cond, PCodeLabel* if_true, PCodeLabel* if_false, PCodeLabel* end
);
extern void gen_condition(ENode* cond, Operand* output);
extern void gen_condition_gpr(ENode* cond, Operand* output, short outputReg);
extern void gen_negated_condition_gpr(ENode* cond, Operand* output, short outputReg);
extern void compare_floating(short nt, ENode* left, ENode* right, Operand* output);
extern void compare_integer(short nt, ENode* left, ENode* right, Operand* output);
extern void compare_immediate(short nt, ENode* left, SInt32 value, Operand* output);
extern void compare_immediate_long(short nt, ENode* left, SInt32 value, Operand* output);
extern int ismaskconstant(SInt32 value, short* first, short* last);
extern int ispostincrementopportunity(ENode* expr, Operand* op, SInt32* value);
extern void add_register_immediate(short regA, short regB, SInt32 value);
extern void I8_gen_ADD(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void I8_gen_INTCONST(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void I8_gen_SUB(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void I8_gen_XOR(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void I8_gen_OR(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void I8_gen_AND(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern int I8_log2n(long long val);
extern void I8_gen_MUL(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void I8_gen_BINNOT(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void I8_gen_MONMIN(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void I8_gen_ASS(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void I8_gen_POSTINCDEC(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void I8_gen_INDIRECT(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void I8_gen_condition(ENode* cond, Operand* output, int write_to_gpr);
extern void I8_gen_SHL_SHR(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void I8_gen_DIV_MOD(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void I8_gen_TYPCON(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void gen_VECTOR128CONST(ENode* expr, short outputReg, short outputRegHi, Operand* output);

#define GEN_NODE(node, opnd) cgdispatch[(node)->type]((node), 0, 0, (opnd))
#define GEN_NODE_TO_REG(node, reg, regHi, opnd) \
    cgdispatch[(node)->type]((node), (reg), (regHi), (opnd))
#define ENSURE_GPR(opnd, type, reg) \
    do { \
        if ((opnd)->optype != OpndType_GPR) { \
            Coerce_to_register((opnd), (type), (reg)); \
        } \
    } while (0)
#define ENSURE_FPR(opnd, type, reg) \
    do { \
        if ((opnd)->optype != OpndType_FPR) { \
            Coerce_to_fp_register((opnd), (type), (reg)); \
        } \
    } while (0)
#define ENSURE_VR(opnd, type, reg) \
    do { \
        if ((opnd)->optype != OpndType_VR) { \
            Coerce_to_v_register((opnd), (type), (reg)); \
        } \
    } while (0)
#define GEN_NODE_TO_GPR(node, opnd, type, reg) \
    do { \
        GEN_NODE((node), (opnd)); \
        ENSURE_GPR((opnd), (type), (reg)); \
    } while (0)
#define GEN_NODE_TO_FPR(node, opnd, type, reg) \
    do { \
        GEN_NODE((node), (opnd)); \
        ENSURE_FPR((opnd), (type), (reg)); \
    } while (0)
#define GEN_NODE_TO_VR(node, opnd, type, reg) \
    do { \
        GEN_NODE((node), (opnd)); \
        ENSURE_VR((opnd), (type), (reg)); \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif
