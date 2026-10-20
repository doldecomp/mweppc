#include "compiler/InstrSelection.h"
#include "compiler/BE_symbol.h"
#include "compiler/CError.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/FunctionCalls.h"
#include "compiler/Intrinsics.h"
#include "compiler/objects.h"
#include "compiler/Operands.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/RegisterInfo.h"
#include "compiler/SFPE_PPC_EABI.h"
#include "compiler/StructMoves.h"
#include "compiler/TOC.h"
#include "compiler/types.h"
#include <stdlib.h>

PrecomputedOperand* precomputedoperands;
void (*cgdispatch[MAXEXPR + 1])(ENode*, short, short, Operand*);

static int ispowerof2(SInt32 val);
static void binary_immediate(
    short opcode, ENode* left, SInt32 value, short outputReg, Operand* output
);
static void shift_left_immediate(
    ENode* expr, short shift, short negate, short outputReg, Operand* output
);
static void shift_right_immediate(
    ENode* expr, Type* type, short shift, short outputReg, Operand* output
);
static void or_xor_immediate(
    short opcode, ENode* expr, SInt32 value, short outputReg, Operand* output
);
static void signed_divide_by_power_of_2(
    ENode* expr, int shift, int negate, short outputReg, Operand* output
);
static void signed_mod_by_power_of_2(
    ENode* expr, int shift, int negate, short outputReg, Operand* output
);
static void fp_binary_operator(
    short opcode, ENode* left, ENode* right, short outputReg, Operand* output
);
static void logical_expression_nobranch(ENode* cond, Boolean invert, Operand* output);
static void shift_and_mask(
    ENode* expr, short sh, short mb, short me, short outputReg, Operand* output
);
static ENodeType invert_relop(ENodeType nt);

#define IS_INT_CONST(node) \
    (ENODE_IS((node), EINTCONST) && IS_TYPE_INT((node)->rtype) && (node)->rtype->size <= 4)
#define IS_INT_CONST_ZERO(node) (IS_INT_CONST(node) && (node)->data.intval.lo == 0)

#define RLWINM_MASK(mb, me) \
    (((mb) <= (me)) ? ((((mb) > 31) ? 0 : (0xFFFFFFFF >> (mb))) & \
                       ~((((me) + 1) > 31) ? 0 : (0xFFFFFFFF >> ((me) + 1)))) \
                    : ((((mb) > 31) ? 0 : (0xFFFFFFFF >> (mb))) | \
                       ~((((me) + 1) > 31) ? 0 : (0xFFFFFFFF >> ((me) + 1)))))

void init_cgdispatch(void) {
    ENodeType t;

    for (t = 0; t <= MAXEXPR; t++) {
        cgdispatch[t] = gen_UNEXPECTED;
    }

    cgdispatch[EPOSTINC] = gen_POSTINCDEC;
    cgdispatch[EPOSTDEC] = gen_POSTINCDEC;
    cgdispatch[EINDIRECT] = gen_INDIRECT;
    cgdispatch[EMONMIN] = gen_MONMIN;
    cgdispatch[EBINNOT] = gen_BINNOT;
    cgdispatch[ELOGNOT] = gen_LOGICAL;
    cgdispatch[EFORCELOAD] = gen_FORCELOAD;
    cgdispatch[EMUL] = gen_MUL;
    cgdispatch[EDIV] = gen_DIV;
    cgdispatch[EMODULO] = gen_MODULO;
    cgdispatch[EADD] = gen_ADD;
    cgdispatch[ESUB] = gen_SUB;
    cgdispatch[ESHL] = gen_SHL;
    cgdispatch[ESHR] = gen_SHR;
    cgdispatch[ELESS] = gen_COMPARE;
    cgdispatch[EGREATER] = gen_COMPARE;
    cgdispatch[ELESSEQU] = gen_COMPARE;
    cgdispatch[EGREATEREQU] = gen_COMPARE;
    cgdispatch[EEQU] = gen_COMPARE;
    cgdispatch[ENOTEQU] = gen_COMPARE;
    cgdispatch[EAND] = gen_AND;
    cgdispatch[EXOR] = gen_XOR;
    cgdispatch[EOR] = gen_OR;
    cgdispatch[ELAND] = gen_LOGICAL;
    cgdispatch[ELOR] = gen_LOGICAL;
    cgdispatch[EASS] = gen_ASS;
    cgdispatch[ECOMMA] = gen_COMMA;
    cgdispatch[ETYPCON] = gen_TYPCON;
    cgdispatch[EBITFIELD] = gen_BITFIELD;
    cgdispatch[EINTCONST] = gen_INTCONST;
    cgdispatch[EFLOATCONST] = gen_FLOATCONST;
    cgdispatch[ESTRINGCONST] = gen_STRINGCONST;
    cgdispatch[ECOND] = gen_COND;
    cgdispatch[EFUNCCALL] = gen_FUNCCALL;
    cgdispatch[EFUNCCALLP] = gen_FUNCCALL;
    cgdispatch[EOBJREF] = gen_OBJREF;
    cgdispatch[ENULLCHECK] = gen_NULLCHECK;
    cgdispatch[EPRECOMP] = gen_PRECOMP;
    cgdispatch[EDEFINE] = gen_DEFINE;
    cgdispatch[EREUSE] = gen_REUSE;
    cgdispatch[EVECTOR128CONST] = gen_VECTOR128CONST;
    cgdispatch[ECONDASS] = gen_CONDASS;
}

void gen_DEFINE(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Operand* op;

    if (!expr->data.diadic.right) {
        op = lalloc(sizeof(Operand));
        memclrw(op, sizeof(Operand));
        expr->data.diadic.right = (ENode*)op;
        GEN_NODE(expr->data.diadic.left, op);
    }

    op = (Operand*)expr->data.diadic.right;
    *output = *op;
}

void gen_REUSE(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* inner = expr->data.monadic;
    CError_ASSERT(258, ENODE_IS(inner, EDEFINE));
    gen_DEFINE(inner, outputReg, outputRegHi, output);
}

void gen_POSTINCDEC(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    TypeBitfield* tbitfield;
    ENode* inner;
    Type* type;
    Operand addr;
    Operand value;
    Operand word;
    Float fval;
    int objReg;
    int constReg;
    int finalReg;
    SInt32 incval;

    inner = expr->data.monadic->data.monadic;
    type = expr->rtype;
    tbitfield = NULL;

    memclrw(&addr, sizeof(Operand));
    memclrw(&value, sizeof(Operand));
    memclrw(&word, sizeof(Operand));

    if (TYPE_IS_SOFT_FLOAT(type)) {
        SFPE_gen_POSTINCDEC(expr, outputReg, outputRegHi, output);
        return;
    }

    if (TYPE_IS_8BYTES(type)) {
        I8_gen_POSTINCDEC(expr, outputReg, outputRegHi, output);
        return;
    }

    if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
        if (ENODE_IS(inner, EOBJREF) && (objReg = OBJECT_REG(inner->data.objref))) {
            output->optype = OpndType_FPR;
            output->reg = (outputReg && outputReg != objReg) ? outputReg : ALLOC_FPR();
            emitpcode(PC_FMR, output->reg, objReg);
            fval = one_point_zero;
            load_floating_constant(constReg = ALLOC_FPR(), type, &fval);

            if (ENODE_IS(expr, EPOSTINC)) {
                emitpcode((type->size == 4) ? PC_FADDS : PC_FADD, objReg, objReg, constReg);
            } else {
                emitpcode((type->size == 4) ? PC_FSUBS : PC_FSUB, objReg, objReg, constReg);
            }
        } else {
            GEN_NODE(inner, &addr);
            indirect(&addr, inner);
            value = addr;
            ENSURE_FPR(&value, type, 0);

            output->optype = OpndType_FPR;
            output->reg = ALLOC_FPR();
            emitpcode(PC_FMR, output->reg, value.reg);

            fval = one_point_zero;
            load_floating_constant(constReg = ALLOC_FPR(), type, &fval);

            finalReg = ALLOC_FPR();
            if (ENODE_IS(expr, EPOSTINC)) {
                emitpcode((type->size == 4) ? PC_FADDS : PC_FADD, finalReg, value.reg, constReg);
            } else {
                emitpcode((type->size == 4) ? PC_FSUBS : PC_FSUB, finalReg, value.reg, constReg);
            }

            store_fp(finalReg, &addr, type);
        }
    } else {
        if (IS_TYPE_POINTER(type)) {
            if (ENODE_IS(expr, EPOSTINC)) {
                incval = TPTR_TARGET(type)->size;
            } else {
                incval = -TPTR_TARGET(type)->size;
            }
        } else {
            if (ENODE_IS(expr, EPOSTINC)) {
                incval = 1;
            } else {
                incval = -1;
            }
        }

        if (ENODE_IS(inner, EOBJREF) && (objReg = OBJECT_REG(inner->data.objref))) {
            output->optype = OpndType_GPR;
            output->reg = (outputReg && outputReg != objReg) ? outputReg : ALLOC_GPR();
            emitpcode(PC_MR, output->reg, objReg);
            add_register_immediate(objReg, objReg, incval);
        } else {
            if (ENODE_IS(inner, EBITFIELD)) {
                tbitfield = TYPE_BITFIELD(TPTR_TARGET(inner));
                inner = inner->data.monadic;
            }
            GEN_NODE(inner, &addr);
            indirect(&addr, inner);
            value = addr;
            ENSURE_GPR(&value, type, 0);

            if (tbitfield) {
                word = value;
                extract_bitfield(&word, tbitfield, 0, &value);
            }
            output->optype = OpndType_GPR;

            output->reg = ALLOC_GPR();
            emitpcode(PC_MR, output->reg, value.reg);

            finalReg = ALLOC_GPR();
            add_register_immediate(finalReg, value.reg, incval);

            if (tbitfield) {
                insert_bitfield(finalReg, &word, tbitfield);
                finalReg = word.reg;
            }

            store(finalReg, &addr, type);
        }
    }
}

void gen_INDIRECT(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Type* type;
    ENode* inner;
    VarInfo* vi;
    Operand op;
    SInt32 postincvalue;

    type = expr->rtype;
    inner = expr->data.monadic;

    if (ENODE_IS(inner, EOBJREF) && is_pooled_object(inner->data.objref)) {
        Object* object = inner->data.objref;
        output->optype = OpndType_IndirectSymbol;
        output->reg = GetPooledObjectRegister(object);
        output->regHi = 0;
        output->object = object;
        output->immOffset = 0;
        set_op_flags(output, expr);
        return;
    }

    if (TYPE_IS_SOFT_FLOAT(type)) {
        SFPE_gen_INDIRECT(expr, outputReg, outputRegHi, output);
        return;
    }

    if (TYPE_IS_8BYTES(type)) {
        I8_gen_INDIRECT(expr, outputReg, outputRegHi, output);
        return;
    }

    memclrw(&op, sizeof(Operand));
    if (ENODE_IS(inner, EOBJREF) && OBJECT_REG(inner->data.objref)) {
        vi = Registers_GetVarInfo(inner->data.objref);
        switch (vi->rclass) {
            case RegClass_GPR:
                output->optype = OpndType_GPR;
                break;
            case RegClass_FPR:
                output->optype = OpndType_FPR;
                break;
            case RegClass_VR:
                output->optype = OpndType_VR;
                break;
            case RegClass_CRFIELD:
                output->optype = OpndType_CRField;
                break;
            default:
                CError_FATAL(463);
        }
        output->reg = vi->reg;
        output->object = NULL;
    } else if (ENODE_IS(inner, EBITFIELD)) {
        GEN_NODE(inner->data.monadic, &op);
        indirect(&op, expr);
        ENSURE_GPR(&op, type, 0);
        extract_bitfield(&op, TYPE_BITFIELD(inner->rtype), outputReg, output);
    } else if (ispostincrementopportunity(inner, &op, &postincvalue) &&
               (TYPE_FITS_IN_REGISTER(type) ||
                IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) ||
                IS_TYPE_VECTOR(type))) {
        indirect(&op, expr);
        *output = op;
        if (TYPE_FITS_IN_REGISTER(type)) {
            ENSURE_GPR(output, type, outputReg);
        } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
            ENSURE_FPR(output, type, outputReg);
        } else {
            ENSURE_VR(output, type, outputReg);
        }

        add_register_immediate(op.reg, op.reg, postincvalue);
    } else {
        GEN_NODE(inner, output);
        indirect(output, expr);
    }
}

void gen_MONMIN(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* inner;
    Type* type;
    ENode* scan;

    inner = expr->data.monadic;
    type = expr->rtype;

    if (TYPE_IS_SOFT_FLOAT(type)) {
        SFPE_gen_MONMIN(expr, outputReg, outputRegHi, output);
        return;
    }

    if (TYPE_IS_8BYTES(type)) {
        I8_gen_MONMIN(expr, outputReg, outputRegHi, output);
        return;
    }

    if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
        if (ENODE_IS(inner, EADD) && ENODE_IS(inner->data.diadic.left, EMUL) && copts.fp_contract) {
            fp_multiply_add(
                (type->size == 4) ? PC_FNMADDS : PC_FNMADD,
                inner->data.diadic.left->data.diadic.left,
                inner->data.diadic.left->data.diadic.right,
                inner->data.diadic.right,
                outputReg,
                output
            );
        } else if (ENODE_IS(inner, EADD) &&
                   ENODE_IS(inner->data.diadic.right, EMUL) &&
                   copts.fp_contract) {
            fp_multiply_add(
                (type->size == 4) ? PC_FNMADDS : PC_FNMADD,
                inner->data.diadic.right->data.diadic.left,
                inner->data.diadic.right->data.diadic.right,
                inner->data.diadic.left,
                outputReg,
                output
            );
        } else if (ENODE_IS(inner, ESUB) &&
                   ENODE_IS(inner->data.diadic.left, EMUL) &&
                   copts.fp_contract) {
            fp_multiply_add(
                (type->size == 4) ? PC_FNMSUBS : PC_FNMSUB,
                inner->data.diadic.left->data.diadic.left,
                inner->data.diadic.left->data.diadic.right,
                inner->data.diadic.right,
                outputReg,
                output
            );
        } else {
            fp_unary_operator(PC_FNEG, inner, outputReg, output);
        }
    } else {
        scan = inner;
        while (ENODE_IS(scan, ETYPCON) && IS_TYPE_INT_OR_ENUM(type) && !is_unsigned(type)) {
            scan = scan->data.monadic;
        }

        switch (scan->type) {
            case ELESS:
            case EGREATER:
            case ELESSEQU:
            case EGREATEREQU:
            case EEQU:
            case ENOTEQU:
                if (TYPE_FITS_IN_REGISTER(scan->data.diadic.left->rtype) &&
                    TYPE_FITS_IN_REGISTER(scan->data.diadic.right->rtype)) {
                    gen_negated_condition_gpr(scan, output, outputReg);
                    return;
                }
            default:
                break;
        }
        unary_operator(PC_NEG, inner, outputReg, output);
    }
}

void gen_BINNOT(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* inner;

    inner = expr->data.monadic;

    if (TYPE_IS_SOFT_FLOAT(expr->rtype)) {
        CError_FATAL(609);
        return;
    }

    if (TYPE_IS_8BYTES(expr->rtype)) {
        I8_gen_BINNOT(expr, outputReg, outputRegHi, output);
        return;
    }

    if (ENODE_IS(inner, EAND)) {
        binary_operator(
            PC_NAND, inner->data.diadic.left, inner->data.diadic.right, outputReg, output
        );
    } else if (ENODE_IS(inner, EOR)) {
        binary_operator(
            PC_NOR, inner->data.diadic.left, inner->data.diadic.right, outputReg, output
        );
    } else if (ENODE_IS(inner, EXOR)) {
        binary_operator(
            PC_EQV, inner->data.diadic.left, inner->data.diadic.right, outputReg, output
        );
    } else {
        unary_operator(PC_NOT, inner, outputReg, output);
    }
}

void gen_FORCELOAD(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* inner;

    inner = expr->data.monadic;
    GEN_NODE(inner, output);

    if (TYPE_IS_SOFT_FLOAT(inner->rtype)) {
        if (inner->rtype->size == 4) {
            ENSURE_GPR(output, inner->rtype, outputReg);
        } else {
            coerce_to_register_pair(output, inner->rtype, outputReg, outputRegHi);
        }
    } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(inner->rtype)) {
        ENSURE_FPR(output, inner->rtype, outputReg);
    } else if (IS_TYPE_VECTOR(inner->rtype)) {
        ENSURE_VR(output, inner->rtype, outputReg);
    } else if (TYPE_FITS_IN_REGISTER(inner->rtype)) {
        if (TYPE_IS_8BYTES(inner->rtype)) {
            coerce_to_register_pair(output, inner->rtype, outputReg, outputRegHi);
        } else {
            ENSURE_GPR(output, inner->rtype, outputReg);
        }
    } else if (!IS_TYPE_VOID(inner->rtype)) {
        if (!(IS_TYPE_STRUCT(inner->rtype) || IS_TYPE_CLASS(inner->rtype)) ||
            Type_RequiresMemoryReturn(inner->rtype)) {
            CError_FATAL(688);
        }
    }
}

void gen_MUL(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* left;
    ENode* right;
    Type* type;
    int shift;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    type = expr->rtype;

    if (TYPE_IS_SOFT_FLOAT(type)) {
        SFPE_gen_OP(expr, outputReg, outputRegHi, output);
        return;
    }

    if (TYPE_IS_8BYTES(type)) {
        I8_gen_MUL(expr, outputReg, outputRegHi, output);
        return;
    }

    if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
        fp_binary_operator((type->size == 4) ? PC_FMULS : PC_FMUL, left, right, outputReg, output);
    } else if (IS_TYPE_VEC2X32FLOAT(type)) {
        fp_binary_operator(PC_PS_MUL, left, right, outputReg, output);
    } else if (ENODE_IS(right, EINTCONST) && (shift = ispowerof2(right->data.intval.lo))) {
        shift_left_immediate(left, shift, 0, outputReg, output);
    } else if (ENODE_IS(right, EINTCONST) && (shift = ispowerof2(-right->data.intval.lo))) {
        shift_left_immediate(left, shift, 1, outputReg, output);
    } else if (ENODE_IS(left, EINTCONST) && (shift = ispowerof2(left->data.intval.lo))) {
        shift_left_immediate(right, shift, 0, outputReg, output);
    } else if (ENODE_IS(left, EINTCONST) && (shift = ispowerof2(-left->data.intval.lo))) {
        shift_left_immediate(right, shift, 1, outputReg, output);
    } else if (ENODE_IS(right, EINTCONST) && FITS_IN_SHORT(right->data.intval.lo)) {
        binary_immediate(PC_MULLI, left, right->data.intval.lo, outputReg, output);
    } else if (ENODE_IS(left, EINTCONST) && FITS_IN_SHORT(left->data.intval.lo)) {
        binary_immediate(PC_MULLI, right, left->data.intval.lo, outputReg, output);
    } else {
        binary_operator(PC_MULLW, left, right, outputReg, output);
    }
}

struct ms {
    SInt32 m;
    int s;
};
static void create_signed_magic(SInt32 val, struct ms* output) {
    int p;
    UInt32 ad, anc, delta, q1, r1, q2, r2, t;

    if (val == 0) {
        CError_ErrorTerm(ERR_DIVISION_BY_0);
    }

    ad = abs(val);
    t = 0x80000000U + ((UInt32)val >> 31);
    anc = t - 1 - t % ad;
    p = 31;
    q1 = 0x80000000U / anc;
    r1 = 0x80000000U - q1 * anc;
    q2 = 0x80000000U / ad;
    r2 = 0x80000000U - q2 * ad;

    do {
        p = p + 1;
        q1 = 2 * q1;
        r1 = 2 * r1;
        if (r1 >= anc) {
            q1 = q1 + 1;
            r1 = r1 - anc;
        }
        q2 = 2 * q2;
        r2 = 2 * r2;
        if (r2 >= ad) {
            q2 = q2 + 1;
            r2 = r2 - ad;
        }
        delta = ad - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));

    output->m = q2 + 1;
    if (val < 0) {
        output->m = -output->m;
    }
    output->s = p - 32;
}

struct mu {
    UInt32 m;
    int a;
    int s;
};
static void create_unsigned_magic(UInt32 val, struct mu* output) {
    int p;
    UInt32 nc, delta, q1, r1, q2, r2;

    if (val == 0) {
        CError_ErrorTerm(ERR_DIVISION_BY_0);
    }

    output->a = 0;
    nc = -1 - (-val) % val;
    p = 31;
    q1 = 0x80000000U / nc;
    r1 = 0x80000000U - q1 * nc;
    q2 = 0x7FFFFFFFU / val;
    r2 = 0x7FFFFFFFU - q2 * val;
    do {
        p = p + 1;
        if (r1 >= nc - r1) {
            q1 = 2 * q1 + 1;
            r1 = 2 * r1 - nc;
        } else {
            q1 = 2 * q1;
            r1 = 2 * r1;
        }
        if (r2 + 1 >= val - r2) {
            if (q2 >= 0x7FFFFFFFU) {
                output->a = 1;
            }
            q2 = 2 * q2 + 1;
            r2 = 2 * r2 + 1 - val;
        } else {
            if (q2 >= 0x80000000U) {
                output->a = 1;
            }
            q2 = 2 * q2;
            r2 = 2 * r2 + 1;
        }
        delta = val - 1 - r2;
    } while (p < 64 && (q1 < delta || (q1 == delta && r1 == 0)));

    output->m = q2 + 1;
    output->s = p - 32;
}

void gen_DIV(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* left;
    Type* type;
    ENode* right;
    int shift;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    type = expr->rtype;

    if (TYPE_IS_SOFT_FLOAT(type)) {
        SFPE_gen_OP(expr, outputReg, outputRegHi, output);
        return;
    }

    if (TYPE_IS_8BYTES(type)) {
        I8_gen_DIV_MOD(expr, outputReg, outputRegHi, output);
        return;
    }

    if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
        fp_binary_operator((type->size == 4) ? PC_FDIVS : PC_FDIV, left, right, outputReg, output);
    } else if (IS_TYPE_VEC2X32FLOAT(type)) {
        fp_binary_operator(PC_PS_DIV, left, right, outputReg, output);
    } else if (is_unsigned(type)) {
        if (ENODE_IS(right, EINTCONST) && (shift = ispowerof2(right->data.intval.lo))) {
            shift_right_immediate(left, type, shift, outputReg, output);
        } else if (!copts.optimizesize &&
                   ENODE_IS(right, EINTCONST) &&
                   right->data.intval.lo != 1 &&
                   right->data.intval.lo != 0) {
            SInt32 value;
            int quotientReg;
            int magicReg;
            int dividendReg;
            int diffReg;
            int halfDiffReg;
            int sumReg;
            int finalReg;
            Operand op1;
            struct mu u_magicoutput;
            value = right->data.intval.lo;
            quotientReg = ALLOC_GPR();
            magicReg = ALLOC_GPR();
            finalReg = outputReg ? outputReg : ALLOC_GPR();

            memclrw(&op1, sizeof(Operand));
            GEN_NODE(left, &op1);
            ENSURE_GPR(&op1, left->rtype, 0);

            dividendReg = op1.reg;
            create_unsigned_magic(value, &u_magicoutput);
            load_immediate(magicReg, u_magicoutput.m);
            emitpcode(PC_MULHWU, quotientReg, magicReg, dividendReg);
            if (u_magicoutput.a == 0) {
                if (u_magicoutput.s) {
                    emitpcode(
                        PC_RLWINM,
                        finalReg,
                        quotientReg,
                        (32 - u_magicoutput.s) & 31,
                        u_magicoutput.s,
                        31
                    );
                } else {
                    emitpcode(PC_MR, finalReg, quotientReg);
                }
            } else if (u_magicoutput.a == 1) {
                diffReg = ALLOC_GPR();
                if (copts.optimizationlevel > 1) {
                    halfDiffReg = ALLOC_GPR();
                    sumReg = ALLOC_GPR();
                } else {
                    halfDiffReg = diffReg;
                    sumReg = diffReg;
                }

                emitpcode(PC_SUBF, diffReg, quotientReg, dividendReg);
                emitpcode(PC_RLWINM, halfDiffReg, diffReg, 31, 1, 31);
                emitpcode(PC_ADD, sumReg, halfDiffReg, quotientReg);
                emitpcode(
                    PC_RLWINM,
                    finalReg,
                    sumReg,
                    (32 - (u_magicoutput.s - 1)) & 31,
                    u_magicoutput.s - 1,
                    31
                );
            }

            output->optype = OpndType_GPR;
            output->reg = finalReg;
        } else {
            binary_operator(PC_DIVWU, left, right, outputReg, output);
        }
    } else {
        SInt32 value;
        if (ENODE_IS(right, EINTCONST) && (shift = ispowerof2(right->data.intval.lo))) {
            signed_divide_by_power_of_2(left, shift, 0, outputReg, output);
        } else if (ENODE_IS(right, EINTCONST) && (shift = ispowerof2(-right->data.intval.lo))) {
            signed_divide_by_power_of_2(left, shift, 1, outputReg, output);
        } else if (!copts.optimizesize &&
                   ENODE_IS(right, EINTCONST) &&
                   (value = right->data.intval.lo) != 1 &&
                   value != 0 &&
                   value != -1) {
            int dividendReg;
            int signReg;
            int magicReg;
            int estimateReg;
            int finalReg;
            Operand op2;
            struct ms s_magicoutput;
            estimateReg = ALLOC_GPR();
            magicReg = ALLOC_GPR();
            signReg = ALLOC_GPR();
            finalReg = outputReg ? outputReg : ALLOC_GPR();

            memclrw(&op2, sizeof(Operand));
            GEN_NODE(left, &op2);
            ENSURE_GPR(&op2, left->rtype, 0);

            dividendReg = op2.reg;
            create_signed_magic(value, &s_magicoutput);
            load_immediate(magicReg, s_magicoutput.m);
            emitpcode(PC_MULHW, estimateReg, magicReg, dividendReg);
            if (value > 0 && s_magicoutput.m < 0) {
                int nextReg = ALLOC_GPR();
                emitpcode(PC_ADD, nextReg, estimateReg, dividendReg);
                estimateReg = nextReg;
            } else if (value < 0 && s_magicoutput.m > 0) {
                int nextReg = ALLOC_GPR();
                emitpcode(PC_SUBF, nextReg, dividendReg, estimateReg);
                estimateReg = nextReg;
            }

            if (s_magicoutput.s) {
                int nextReg = ALLOC_GPR();
                emitpcode(PC_SRAWI, nextReg, estimateReg, s_magicoutput.s);
                estimateReg = nextReg;
            }

            emitpcode(PC_RLWINM, signReg, estimateReg, 1, 31, 31);
            emitpcode(PC_ADD, finalReg, estimateReg, signReg);

            output->optype = OpndType_GPR;
            output->reg = finalReg;
        } else {
            binary_operator(PC_DIVW, left, right, outputReg, output);
        }
    }
}

void gen_MODULO(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Operand op1;
    Operand op2;
    ENode* left;
    ENode* right;
    SInt32 shift;
    struct mu u_magicoutput;
    struct ms s_magicoutput;
    SInt32 value;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    memclrw(&op1, sizeof(Operand));
    memclrw(&op2, sizeof(Operand));

    if (TYPE_IS_SOFT_FLOAT(expr->rtype)) {
        SFPE_gen_OP(expr, outputReg, outputRegHi, output);
        return;
    }

    if (TYPE_IS_8BYTES(expr->rtype)) {
        I8_gen_DIV_MOD(expr, outputReg, outputRegHi, output);
        return;
    }

    if (ENODE_IS(right, EINTCONST) && (shift = ispowerof2(right->data.intval.lo))) {
        if (is_unsigned(expr->rtype)) {
            shift_and_mask(left, 0, 32 - shift, 31, outputReg, output);
        } else {
            signed_mod_by_power_of_2(left, shift, 0, outputReg, output);
        }
        return;
    }

    if (!copts.optimizesize &&
        ENODE_IS(right, EINTCONST) &&
        (value = right->data.intval.lo) != 1u &&
        value != -1) {
        GEN_NODE(left, &op1);
        ENSURE_GPR(&op1, left->rtype, 0);

        if (is_unsigned(expr->rtype)) {
            SInt32 dividendReg;
            SInt32 quotientReg;
            SInt32 magicReg;
            SInt32 productReg;
            SInt32 diffReg;
            SInt32 halfDiffReg;
            SInt32 sumReg;
            SInt32 shiftedReg;
            SInt32 finalReg;

            dividendReg = op1.reg;
            quotientReg = ALLOC_GPR();
            magicReg = ALLOC_GPR();
            productReg = ALLOC_GPR();
            finalReg = outputReg ? outputReg : ALLOC_GPR();

            create_unsigned_magic(right->data.intval.lo, &u_magicoutput);
            load_immediate(magicReg, u_magicoutput.m);
            emitpcode(PC_MULHWU, quotientReg, magicReg, dividendReg);

            if (u_magicoutput.a == 0 && u_magicoutput.s != 0) {
                emitpcode(
                    PC_RLWINM,
                    quotientReg,
                    quotientReg,
                    (32 - u_magicoutput.s) & 31,
                    u_magicoutput.s,
                    31
                );
            }

            if (u_magicoutput.a == 1) {
                diffReg = ALLOC_GPR();
                if (copts.optimizationlevel > 1) {
                    halfDiffReg = ALLOC_GPR();
                    sumReg = ALLOC_GPR();
                    shiftedReg = ALLOC_GPR();
                } else {
                    halfDiffReg = diffReg;
                    sumReg = diffReg;
                    shiftedReg = diffReg;
                }
                emitpcode(PC_SUBF, diffReg, quotientReg, dividendReg);
                emitpcode(PC_RLWINM, halfDiffReg, diffReg, 31, 1, 31);
                emitpcode(PC_ADD, sumReg, halfDiffReg, quotientReg);
                emitpcode(
                    PC_RLWINM,
                    shiftedReg,
                    sumReg,
                    (32 - (u_magicoutput.s - 1)) & 31,
                    u_magicoutput.s - 1,
                    31
                );
                quotientReg = shiftedReg;
            }

            if (value > 0 && value < 0x7FFF) {
                emitpcode(PC_MULLI, productReg, quotientReg, value);
            } else {
                GEN_NODE(right, &op2);
                ENSURE_GPR(&op2, right->rtype, 0);
                emitpcode(PC_MULLW, productReg, quotientReg, op2.reg);
            }

            emitpcode(PC_SUBF, finalReg, productReg, dividendReg);
            output->optype = OpndType_GPR;
            output->reg = finalReg;
        } else {
            SInt32 dividendReg;
            SInt32 estimateReg;
            SInt32 magicReg;
            SInt32 signReg;
            SInt32 quotientReg;
            SInt32 productReg;
            SInt32 finalReg;

            dividendReg = op1.reg;
            estimateReg = ALLOC_GPR();
            magicReg = ALLOC_GPR();
            signReg = ALLOC_GPR();
            quotientReg = ALLOC_GPR();
            productReg = ALLOC_GPR();
            finalReg = outputReg ? outputReg : ALLOC_GPR();

            create_signed_magic(right->data.intval.lo, &s_magicoutput);
            load_immediate(magicReg, s_magicoutput.m);
            emitpcode(PC_MULHW, estimateReg, magicReg, dividendReg);

            if (value > 0 && s_magicoutput.m < 0) {
                SInt32 nextReg = ALLOC_GPR();
                emitpcode(PC_ADD, nextReg, estimateReg, dividendReg);
                estimateReg = nextReg;
            } else if (value < 0 && s_magicoutput.m > 0) {
                SInt32 nextReg = ALLOC_GPR();
                emitpcode(PC_SUBF, nextReg, dividendReg, estimateReg);
                estimateReg = nextReg;
            }

            if (s_magicoutput.s != 0) {
                SInt32 nextReg = ALLOC_GPR();
                emitpcode(PC_SRAWI, nextReg, estimateReg, s_magicoutput.s);
                estimateReg = nextReg;
            }

            emitpcode(PC_RLWINM, signReg, estimateReg, 1, 31, 31);
            emitpcode(PC_ADD, quotientReg, estimateReg, signReg);

            if (value < 0x7FFF && value > -0x4000) {
                emitpcode(PC_MULLI, productReg, quotientReg, value);
            } else {
                GEN_NODE(right, &op2);
                ENSURE_GPR(&op2, right->rtype, 0);

                emitpcode(PC_MULLW, productReg, quotientReg, op2.reg);
            }

            emitpcode(PC_SUBF, finalReg, productReg, dividendReg);
            output->optype = OpndType_GPR;
            output->reg = finalReg;
        }
    } else {
        SInt32 quotientReg;
        SInt32 productReg;
        SInt32 finalReg;

        if (right->hascall) {
            GEN_NODE(right, &op2);
            ENSURE_GPR(&op2, right->rtype, 0);
            GEN_NODE(left, &op1);
            ENSURE_GPR(&op1, left->rtype, 0);
        } else {
            GEN_NODE(left, &op1);
            ENSURE_GPR(&op1, left->rtype, 0);
            GEN_NODE(right, &op2);
            ENSURE_GPR(&op2, right->rtype, 0);
        }

        quotientReg = ALLOC_GPR();
        productReg = ALLOC_GPR();
        finalReg = outputReg ? outputReg : ALLOC_GPR();

        emitpcode(is_unsigned(expr->rtype) ? PC_DIVWU : PC_DIVW, quotientReg, op1.reg, op2.reg);
        emitpcode(PC_MULLW, productReg, quotientReg, op2.reg);
        emitpcode(PC_SUBF, finalReg, productReg, op1.reg);

        output->optype = OpndType_GPR;
        output->reg = finalReg;
    }
}

void gen_ADD(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* left;
    ENode* right;
    Type* type;
    Operand opleft;
    Operand opright;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    type = expr->rtype;

    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));

    if (TYPE_IS_SOFT_FLOAT(type)) {
        SFPE_gen_OP(expr, outputReg, outputRegHi, output);
        return;
    }

    if (TYPE_IS_8BYTES(type)) {
        I8_gen_ADD(expr, outputReg, outputRegHi, output);
        return;
    }

    if (IS_TYPE_VEC2X32FLOAT(type)) {
        if (ENODE_IS(left, EMUL) && copts.fp_contract) {
            fp_multiply_add(
                PC_PS_MADD,
                left->data.diadic.left,
                left->data.diadic.right,
                right,
                outputReg,
                output
            );
        } else if (ENODE_IS(right, EMUL) && copts.fp_contract) {
            fp_multiply_add(
                PC_PS_MADD,
                right->data.diadic.left,
                right->data.diadic.right,
                left,
                outputReg,
                output
            );
        } else {
            fp_binary_operator(PC_PS_ADD, left, right, outputReg, output);
        }
    } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
        if (ENODE_IS(left, EMUL) && copts.fp_contract) {
            fp_multiply_add(
                (type->size == 4) ? PC_FMADDS : PC_FMADD,
                left->data.diadic.left,
                left->data.diadic.right,
                right,
                outputReg,
                output
            );
        } else if (ENODE_IS(right, EMUL) && copts.fp_contract) {
            fp_multiply_add(
                (type->size == 4) ? PC_FMADDS : PC_FMADD,
                right->data.diadic.left,
                right->data.diadic.right,
                left,
                outputReg,
                output
            );
        } else {
            fp_binary_operator(
                (type->size == 4) ? PC_FADDS : PC_FADD, left, right, outputReg, output
            );
        }
    } else {
        if (right->hascall) {
            GEN_NODE(right, &opright);
            if (opright.optype >= OpndType_IndirectGPR_ImmOffset) {
                ENSURE_GPR(&opright, right->rtype, 0);
            }

            GEN_NODE(left, &opleft);
            if (opleft.optype >= OpndType_IndirectGPR_ImmOffset) {
                ENSURE_GPR(&opleft, left->rtype, 0);
            }
        } else {
            GEN_NODE(left, &opleft);
            if (opleft.optype >= OpndType_IndirectGPR_ImmOffset) {
                ENSURE_GPR(&opleft, left->rtype, 0);
            }

            GEN_NODE(right, &opright);
            if (opright.optype >= OpndType_IndirectGPR_ImmOffset) {
                ENSURE_GPR(&opright, right->rtype, 0);
            }
        }

        if (IS_TYPE_POINTER(expr->rtype)) {
            if (TYPE_IS_8BYTES(expr->data.diadic.left->rtype)) {
                opleft.optype = OpndType_GPR;
                opleft.regHi = 0;
            }
            if (TYPE_IS_8BYTES(expr->data.diadic.right->rtype)) {
                opright.optype = OpndType_GPR;
                opright.regHi = 0;
            }
        }

        combine(&opleft, &opright, outputReg, output);
    }
}

void gen_SUB(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* left;
    ENode* right;
    Type* type;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    type = expr->rtype;

    if (TYPE_IS_SOFT_FLOAT(type)) {
        SFPE_gen_OP(expr, outputReg, outputRegHi, output);
        return;
    }

    if (TYPE_IS_8BYTES(type)) {
        I8_gen_SUB(expr, outputReg, outputRegHi, output);
        return;
    }

    if (IS_TYPE_VEC2X32FLOAT(type)) {
        if (ENODE_IS(left, EMUL) && copts.fp_contract) {
            fp_multiply_add(
                PC_PS_MSUB,
                left->data.diadic.left,
                left->data.diadic.right,
                right,
                outputReg,
                output
            );
        } else if (ENODE_IS(right, EMUL) && copts.fp_contract) {
            fp_multiply_add(
                PC_PS_MSUB,
                right->data.diadic.left,
                right->data.diadic.right,
                left,
                outputReg,
                output
            );
        } else {
            fp_binary_operator(PC_PS_SUB, left, right, outputReg, output);
        }
    } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
        if (ENODE_IS(left, EMUL) && copts.fp_contract) {
            fp_multiply_add(
                (type->size == 4) ? PC_FMSUBS : PC_FMSUB,
                left->data.diadic.left,
                left->data.diadic.right,
                right,
                outputReg,
                output
            );
        } else if (ENODE_IS(right, EMUL) && copts.fp_contract) {
            fp_multiply_add(
                (type->size == 4) ? PC_FNMSUBS : PC_FNMSUB,
                right->data.diadic.left,
                right->data.diadic.right,
                left,
                outputReg,
                output
            );
        } else {
            fp_binary_operator(
                (type->size == 4) ? PC_FSUBS : PC_FSUB, left, right, outputReg, output
            );
        }
    } else if (ENODE_IS(left, EINTCONST) && FITS_IN_SHORT(left->data.intval.lo)) {
        binary_immediate(PC_SUBFIC, right, left->data.intval.lo, outputReg, output);
    } else {
        binary_operator(PC_SUBF, right, left, outputReg, output);
    }
}

void gen_SHL(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Type* type;
    ENode* left;
    ENode* right;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    if (TYPE_IS_SOFT_FLOAT(expr->rtype)) {
        CError_FATAL(1495);
        return;
    }
    type = expr->rtype;

    if (TYPE_IS_8BYTES(type)) {
        I8_gen_SHL_SHR(expr, outputReg, outputRegHi, output);
        return;
    }

    if (ENODE_IS(right, EINTCONST)) {
        shift_left_immediate(left, right->data.intval.lo, 0, outputReg, output);
    } else {
        binary_operator(PC_SLW, left, right, outputReg, output);
    }
}

void gen_SHR(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* left;
    Type* type;
    ENode* right;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    type = expr->rtype;
    if (TYPE_IS_SOFT_FLOAT(type)) {
        CError_FATAL(1535);
        return;
    }

    if (TYPE_IS_8BYTES(type)) {
        I8_gen_SHL_SHR(expr, outputReg, outputRegHi, output);
        return;
    }

    if (ENODE_IS(right, EINTCONST)) {
        shift_right_immediate(left, type, right->data.intval.lo, outputReg, output);
    } else {
        binary_operator(is_unsigned(type) ? PC_SRW : PC_SRAW, left, right, outputReg, output);
    }
}

void gen_AND(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Type* type;
    ENode* left;
    ENode* right;
    short first;
    short last;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    if (copts.soft_float) {
        type = expr->rtype;
        if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
            CError_FATAL(1572);
            return;
        }
    }
    type = expr->rtype;

    if (TYPE_IS_8BYTES(type)) {
        I8_gen_AND(expr, outputReg, outputRegHi, output);
        return;
    }

    if (ENODE_IS(right, EINTCONST) && ismaskconstant(right->data.intval.lo, &first, &last)) {
        if (ENODE_IS(left, ESHL) &&
            ENODE_IS(left->data.diadic.right, EINTCONST) &&
            (int)(left->data.diadic.right->data.intval.lo + last) < 32) {
            shift_and_mask(
                left->data.diadic.left,
                left->data.diadic.right->data.intval.lo,
                first,
                last,
                outputReg,
                output
            );
        } else if (ENODE_IS(left, ESHR) &&
                   ENODE_IS(left->data.diadic.right, EINTCONST) &&
                   (int)left->data.diadic.right->data.intval.lo <= first &&
                   last >= first) {
            if (left->data.diadic.right->data.intval.lo == 0) {
                shift_and_mask(left->data.diadic.left, 0, first, last, outputReg, output);
            } else {
                shift_and_mask(
                    left->data.diadic.left,
                    32 - left->data.diadic.right->data.intval.lo,
                    first,
                    last,
                    outputReg,
                    output
                );
            }
        } else {
            shift_and_mask(left, 0, first, last, outputReg, output);
        }
    } else if (ENODE_IS(right, EINTCONST) &&
               (right->data.intval.lo & 0xFFFF) == right->data.intval.lo) {
        binary_immediate(PC_ANDI, left, right->data.intval.lo, outputReg, output);
    } else if (ENODE_IS(right, EINTCONST) && FITS_IN_HI_SHORT(right->data.intval.lo)) {
        binary_immediate(PC_ANDIS, left, right->data.intval.lo >> 16, outputReg, output);
    } else if (ENODE_IS(left, EINTCONST) && ismaskconstant(left->data.intval.lo, &first, &last)) {
        if (ENODE_IS(right, ESHL) &&
            ENODE_IS(right->data.diadic.right, EINTCONST) &&
            (int)(right->data.diadic.right->data.intval.lo + last) < 32) {
            shift_and_mask(
                right->data.diadic.left,
                right->data.diadic.right->data.intval.lo,
                first,
                last,
                outputReg,
                output
            );
        } else if (ENODE_IS(right, ESHR) &&
                   ENODE_IS(right->data.diadic.right, EINTCONST) &&
                   (int)right->data.diadic.right->data.intval.lo <= first) {
            if (right->data.diadic.right->data.intval.lo == 0) {
                shift_and_mask(right->data.diadic.left, 0, first, last, outputReg, output);
            } else {
                shift_and_mask(
                    right->data.diadic.left,
                    32 - right->data.diadic.right->data.intval.lo,
                    first,
                    last,
                    outputReg,
                    output
                );
            }
        } else {
            shift_and_mask(right, 0, first, last, outputReg, output);
        }
    } else if (ENODE_IS(left, EINTCONST) &&
               (left->data.intval.lo & 0xFFFF) == left->data.intval.lo) {
        binary_immediate(PC_ANDI, right, left->data.intval.lo, outputReg, output);
    } else if (ENODE_IS(left, EINTCONST) && FITS_IN_HI_SHORT(left->data.intval.lo)) {
        binary_immediate(PC_ANDIS, right, left->data.intval.lo >> 16, outputReg, output);
    } else if (ENODE_IS(right, EBINNOT)) {
        binary_operator(PC_ANDC, left, right->data.monadic, outputReg, output);
    } else if (ENODE_IS(left, EBINNOT)) {
        binary_operator(PC_ANDC, right, left->data.monadic, outputReg, output);
    } else {
        binary_operator(PC_AND, left, right, outputReg, output);
    }
}

void gen_XOR(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Type* type;
    ENode* left;
    ENode* right;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    if (TYPE_IS_SOFT_FLOAT(expr->rtype)) {
        CError_FATAL(1651);
        return;
    }
    type = expr->rtype;

    if (TYPE_IS_8BYTES(type)) {
        I8_gen_XOR(expr, outputReg, outputRegHi, output);
        return;
    }

    if (ENODE_IS(left, EINTCONST)) {
        or_xor_immediate(PC_XORI, right, left->data.intval.lo, outputReg, output);
    } else if (ENODE_IS(right, EINTCONST)) {
        or_xor_immediate(PC_XORI, left, right->data.intval.lo, outputReg, output);
    } else if (ENODE_IS(right, EBINNOT)) {
        binary_operator(PC_EQV, left, right->data.monadic, outputReg, output);
    } else if (ENODE_IS(left, EBINNOT)) {
        binary_operator(PC_EQV, left->data.monadic, right, outputReg, output);
    } else {
        binary_operator(PC_XOR, left, right, outputReg, output);
    }
}

void gen_OR(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Type* type;
    ENode* left;
    ENode* right;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    if (TYPE_IS_SOFT_FLOAT(expr->rtype)) {
        CError_FATAL(1702);
        return;
    }
    type = expr->rtype;

    if (TYPE_IS_8BYTES(type)) {
        I8_gen_OR(expr, outputReg, outputRegHi, output);
        return;
    }

    if (ENODE_IS(left, EINTCONST)) {
        or_xor_immediate(PC_ORI, right, left->data.intval.lo, outputReg, output);
    } else if (ENODE_IS(right, EINTCONST)) {
        or_xor_immediate(PC_ORI, left, right->data.intval.lo, outputReg, output);
    } else if (ENODE_IS(right, EBINNOT)) {
        binary_operator(PC_ORC, left, right->data.monadic, outputReg, output);
    } else if (ENODE_IS(left, EBINNOT)) {
        binary_operator(PC_ORC, right, left->data.monadic, outputReg, output);
    } else {
        binary_operator(PC_OR, left, right, outputReg, output);
    }
}

static inline int is_gpr_float_copy(Operand* op, ENode* left) {
    if (op->optype >= OpndType_IndirectGPR_ImmOffset &&
        ((ENODE_IS(left, EINDIRECT) && ENODE_IS(left->data.monadic, EOBJREF)) ||
         (ENODE_IS(left, EADD) &&
          ENODE_IS(left->data.diadic.right, EINTCONST) &&
          ENODE_IS(left->data.diadic.left, EINDIRECT) &&
          ENODE_IS(left->data.diadic.left->data.monadic, EOBJREF)))) {
        return TRUE;
    }
    return FALSE;
}

void gen_ASS(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Type* type;
    ENode* left;
    ENode* right;
    Operand opleft;
    Operand opright;
    Operand word;
    VarInfo* vi;
    SInt32 incval;
    short align;
    short align2;

    type = expr->rtype;
    if (ENODE_IS(expr, ECONDASS)) {
        left = expr->data.cond.expr1;
        if (ENODE_IS(left, EINDIRECT)) {
            left = left->data.monadic;
        } else {
            CError_FATAL(1766);
        }
        right = expr->data.cond.expr2;
    } else {
        left = expr->data.diadic.left;
        right = expr->data.diadic.right;
    }

    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));
    memclrw(&word, sizeof(Operand));

    if (TYPE_IS_SOFT_FLOAT(type)) {
        SFPE_gen_ASS(expr, outputReg, outputRegHi, output);
        return;
    }

    if (TYPE_IS_8BYTES(type)) {
        I8_gen_ASS(expr, outputReg, outputRegHi, output);
        return;
    }

    if (ENODE_IS(left, EOBJREF) && OBJECT_REG(left->data.objref)) {
        vi = Registers_GetVarInfo(left->data.objref);
        GEN_NODE_TO_REG(right, vi->reg, 0, &opright);
        switch (vi->rclass) {
            case RegClass_GPR:
                ENSURE_GPR(&opright, type, vi->reg);
                output->optype = OpndType_GPR;
                break;
            case RegClass_FPR:
                ENSURE_FPR(&opright, type, vi->reg);
                output->optype = OpndType_FPR;
                break;
            case RegClass_VR:
                ENSURE_VR(&opright, type, vi->reg);
                output->optype = OpndType_VR;
                break;
            default:
                CError_FATAL(1817);
        }
        if (opright.reg != vi->reg) {
            PCodeArg dst, src;
            if ((type->type == TYPEINT && TYPE_INTEGRAL(type)->integral == IT_17) ||
                (type->type == TYPEINT && TYPE_INTEGRAL(type)->integral == IT_18) ||
                IS_TYPE_VEC2X32FLOAT(type)) {
                dst.kind = PCOp_REGISTER;
                dst.arg = vi->rclass;
                dst.data.reg.reg = vi->reg;
                dst.data.reg.effect = EffectPS0 | EffectPS1 | EffectWrite;
                src.kind = PCOp_REGISTER;
                src.arg = vi->rclass;
                src.data.reg.reg = opright.reg;
                src.data.reg.effect = EffectPS0 | EffectPS1 | EffectRead;
            } else {
                dst.kind = PCOp_REGISTER;
                dst.arg = vi->rclass;
                dst.data.reg.reg = vi->reg;
                dst.data.reg.effect = EffectWrite;
                src.kind = PCOp_REGISTER;
                src.arg = vi->rclass;
                src.data.reg.reg = opright.reg;
                src.data.reg.effect = EffectRead;
            }
            appendpcode(pclastblock, makecopyinstruction(&src, &dst));
        }
        output->reg = vi->reg;
    } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
        GEN_NODE(right, &opright);
        if (copts.gprfloatcopy && expr->ignored && is_gpr_float_copy(&opright, left)) {
            type = (type->size == 4) ? (Type*)&stunsignedlong : (Type*)&stunsignedlonglong;
            ENSURE_GPR(&opright, type, 0);
            if (!TYPE_IS_8BYTES(type) && ispostincrementopportunity(left, &opleft, &incval)) {
                indirect(&opleft, expr);
                store(opright.reg, &opleft, type);
                add_register_immediate(opleft.reg, opleft.reg, incval);
            } else {
                GEN_NODE(left, &opleft);
                indirect(&opleft, expr);
                if (TYPE_IS_8BYTES(type)) {
                    store_pair(opright.reg, opright.regHi, &opleft, type);
                } else {
                    store(opright.reg, &opleft, type);
                }
            }
            if (TYPE_IS_8BYTES(type)) {
                output->optype = OpndType_GPRPair;
                output->reg = opright.reg;
                output->regHi = opright.regHi;
            } else {
                output->optype = OpndType_GPR;
                output->reg = opright.reg;
            }
        } else {
            ENSURE_FPR(&opright, right->rtype, 0);
            if (ispostincrementopportunity(left, &opleft, &incval)) {
                indirect(&opleft, expr);
                store_fp(opright.reg, &opleft, type);
                add_register_immediate(opleft.reg, opleft.reg, incval);
            } else {
                GEN_NODE(left, &opleft);
                indirect(&opleft, expr);
                store_fp(opright.reg, &opleft, type);
            }
            output->optype = OpndType_FPR;
            output->reg = opright.reg;
        }
    } else if (IS_TYPE_VECTOR(type)) {
        GEN_NODE(right, &opright);
        if (opright.optype == OpndType_Absolute) {
            ENSURE_VR(&opright, type, 0);
        } else if (opright.optype != OpndType_VR) {
            Coerce_to_v_register(&opright, right->rtype, 0);
        }

        if (ispostincrementopportunity(left, &opleft, &incval)) {
            indirect(&opleft, expr);
            store_v(opright.reg, &opleft, type);
            add_register_immediate(opleft.reg, opleft.reg, incval);
        } else {
            GEN_NODE(left, &opleft);
            indirect(&opleft, expr);
            store_v(opright.reg, &opleft, type);
        }
        output->optype = OpndType_VR;
        output->reg = opright.reg;
    } else if (IS_TYPE_VEC2X32FLOAT(type) && copts.processor == CPU_PPCGEKKO) {
        GEN_NODE(right, &opright);
        ENSURE_FPR(&opright, right->rtype, 0);
        GEN_NODE(left, &opleft);
        indirect(&opleft, expr);
        vectorstore(opright.reg, &opleft, type);
        output->optype = OpndType_FPR;
        output->reg = opright.reg;
    } else if (TYPE_FITS_IN_REGISTER(type)) {
        GEN_NODE(right, &opright);
        ENSURE_GPR(&opright, right->rtype, 0);

        if (ENODE_IS(left, EBITFIELD)) {
            GEN_NODE(left->data.monadic, &opleft);
            indirect(&opleft, expr);

            word = opleft;
            ENSURE_GPR(&word, type, 0);
            insert_bitfield(opright.reg, &word, TYPE_BITFIELD(left->rtype));
            store(word.reg, &opleft, type);

            if (!expr->ignored) {
                extract_bitfield(&word, TYPE_BITFIELD(left->rtype), opright.reg, &opleft);
            }
        } else if (ispostincrementopportunity(left, &opleft, &incval)) {
            indirect(&opleft, expr);
            store(opright.reg, &opleft, type);
            add_register_immediate(opleft.reg, opleft.reg, incval);
        } else {
            GEN_NODE(left, &opleft);
            indirect(&opleft, expr);
            store(opright.reg, &opleft, type);
        }

        output->optype = OpndType_GPR;
        output->reg = opright.reg;
    } else {
        GEN_NODE(right, &opright);
        GEN_NODE(left, output);

        indirect(output, expr);
        if (output->object) {
            if (output->object->datatype == DLOCAL &&
                (output->object->u.var.info->flags & VarInfoFlag_ParamArea)) {
                align = CMach_ArgumentAlignment(type);
            } else {
                align = CMach_AllocationAlignment(type, output->object->qual);
            }
        } else {
            align = CMach_AllocationAlignment(type, 0);
        }
        if (opright.object) {
            if (opright.object->datatype == DLOCAL &&
                (opright.object->u.var.info->flags & VarInfoFlag_ParamArea)) {
                align2 = CMach_ArgumentAlignment(type);
            } else {
                align2 = CMach_AllocationAlignment(type, opright.object->qual);
            }
        } else {
            align2 = CMach_AllocationAlignment(type, 0);
        }

        if (align2 < align) {
            align = align2;
        }

        if ((type->type == TYPESTRUCT || type->type == TYPECLASS) &&
            !Type_RequiresMemoryReturn(type)) {
            if (opright.optype == OpndType_GPRPair) {
                store_small_block_from_reg_pair(opright.reg, opright.regHi, output, type, align);
            } else if (opright.optype == OpndType_GPR) {
                store_small_block_from_reg(opright.reg, output, type, align);
            } else {
                move_block(output, &opright, type->size, align);
            }
        } else {
            move_block(output, &opright, type->size, align);
        }
    }
}

ENode* evaluate_and_skip_comma(ENode* expr) {
    Operand op;
    ENode* inner;

    memclrw(&op, sizeof(Operand));
    while (ENODE_IS(expr, ECOMMA)) {
        inner = expr->data.diadic.left;
        GEN_NODE(inner, &op);
        if (ENODE_IS(inner, EINDIRECT) && (op.flags & OpndFlags_Volatile)) {
            if (TYPE_FITS_IN_REGISTER_2(inner->rtype) ||
                (TYPE_IS_SOFT_FLOAT(inner->rtype) && inner->rtype->size == 4)) {
                ENSURE_GPR(&op, inner->rtype, 0);
            } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(inner->rtype)) {
                ENSURE_FPR(&op, inner->rtype, 0);
            }
        }
        expr = expr->data.diadic.right;
    }
    return expr;
}

void gen_COMMA(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* left;
    ENode* right;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    GEN_NODE(left, output);

    if (ENODE_IS(left, EINDIRECT) && (output->flags & OpndFlags_Volatile)) {
        if (TYPE_FITS_IN_REGISTER_2(left->rtype) ||
            (TYPE_IS_SOFT_FLOAT(left->rtype) && left->rtype->size == 4)) {
            ENSURE_GPR(output, left->rtype, 0);
        } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(left->rtype)) {
            ENSURE_FPR(output, left->rtype, 0);
        }
    }

    GEN_NODE_TO_REG(right, outputReg, 0, output);
}

void gen_TYPCON(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* inner;
    Type* srctype;
    Type* dsttype;
    int gprTemp;
    int fprTemp;
    short gqr;
    int gqr2;
    Operand srcTemp;
    Operand dstTemp;
    int reg;
    int resultReg;

    inner = expr->data.monadic;
    srctype = inner->rtype;
    dsttype = expr->rtype;

    if (TYPE_IS_SOFT_FLOAT(srctype) || TYPE_IS_SOFT_FLOAT(dsttype)) {
        SFPE_gen_TYPCON(expr, outputReg, outputRegHi, output);
        return;
    }

    if (TYPE_IS_8BYTES(srctype) || TYPE_IS_8BYTES(dsttype)) {
        I8_gen_TYPCON(expr, outputReg, outputRegHi, output);
        return;
    }

    if (dsttype->type == TYPEVOID) {
        GEN_NODE(inner, output);
        if (ENODE_IS(inner, EINDIRECT) && (output->flags & OpndFlags_Volatile)) {
            if (srctype->type == TYPEINT ||
                srctype->type == TYPEENUM ||
                IS_TYPE_POINTER(srctype) ||
                (srctype->type == TYPEMEMBERPOINTER && srctype->size == 4) ||
                (copts.soft_float &&
                 IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(srctype) &&
                 srctype->size == 4)) {
                ENSURE_GPR(output, srctype, 0);
            } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(srctype)) {
                ENSURE_FPR(output, srctype, 0);
            }
        }
    } else if (srctype->type == TYPEINT || srctype->type == TYPEENUM) {
        if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(dsttype) || IS_TYPE_VEC2X32FLOAT(dsttype)) {
            if (srctype->size < 4 &&
                copts.gecko_float_typecons &&
                copts.processor == CPU_PPCGEKKO) {
                fprTemp = ALLOC_FPR();
                gprTemp = ALLOC_GPR();
                if (is_unsigned(srctype)) {
                    gqr = (srctype->size == 1) ? 4 : 5;
                } else {
                    gqr = (srctype->size == 1) ? 6 : 7;
                }
                emitpcode(PC_LI, gprTemp, gqr);
                emitpcode(PC_ORIS, gprTemp, gprTemp, gqr);
                emitpcode(PC_MTSPR, SPR_GQR1, gprTemp);
                GEN_NODE(inner, output);
                if (output->optype == OpndType_GPR) {
                    symbol_operand(&srcTemp, maketemporary(srctype));
                    coerce_to_addressable(&srcTemp);
                    load_store_register(
                        (srctype->size == 1) ? PC_STB : PC_STH,
                        output->reg,
                        srcTemp.reg,
                        srcTemp.object,
                        0
                    );
                    gecko_load_store_register(
                        fprTemp, srcTemp.reg, srcTemp.object, 0, 1, SPR_GQR1, 1
                    );
                } else {
                    coerce_to_addressable(output);
                    switch (output->optype) {
                        case OpndType_IndirectGPR_ImmOffset:
                            gecko_load_store_register(
                                fprTemp,
                                output->reg,
                                output->object,
                                output->immOffset,
                                1,
                                SPR_GQR1,
                                1
                            );
                            break;
                        case OpndType_IndirectGPR_Indexed:
                            emitpcode(
                                PC_PSQ_LX, fprTemp, output->reg, output->regOffset, 1, SPR_GQR1
                            );
                            break;
                        default:
                            CError_FATAL(2227);
                    }
                }
                if (IS_TYPE_VEC2X32FLOAT(dsttype)) {
                    emitpcode(PC_PS_MERGE00, fprTemp, fprTemp, fprTemp);
                }
                output->optype = OpndType_FPR;
                output->reg = fprTemp;
            } else {
                GEN_NODE(inner, output);
                if (srctype->size < 4) {
                    extend32(output, srctype, 0);
                }
                ENSURE_GPR(output, srctype, 0);
                if (is_unsigned(srctype)) {
                    convert_unsigned_to_floating(output, dsttype->size == 4, outputReg);
                } else {
                    convert_integer_to_floating(output, dsttype->size == 4, outputReg);
                }
                if (IS_TYPE_VEC2X32FLOAT(dsttype)) {
                    emitpcode(PC_PS_MERGE00, output->reg, output->reg, output->reg);
                }
            }
        } else if (IS_TYPE_VECTOR(dsttype)) {
            GEN_NODE_TO_REG(inner, outputReg, 0, output);
            ENSURE_VR(output, dsttype, outputReg);
        } else if (srctype->size < dsttype->size &&
                   !(ENODE_IS(inner, EINDIRECT) && ENODE_IS(inner->data.monadic, EBITFIELD)) &&
                   !((inner->type >= EASS && inner->type <= EORASS) &&
                     ENODE_IS(inner->data.monadic->data.monadic, EBITFIELD)) &&
                   !((inner->type >= EPOSTINC && inner->type <= EPREDEC) &&
                     ENODE_IS(inner->data.monadic->data.monadic, EBITFIELD))) {
            GEN_NODE(inner, output);
            extend32(output, srctype, outputReg);
        } else if (dsttype->size < srctype->size || dsttype->size < 4) {
            GEN_NODE(inner, output);
            ENSURE_GPR(output, srctype, 0);
            extend32(output, dsttype, outputReg);
        } else {
            GEN_NODE_TO_REG(inner, outputReg, 0, output);
        }
    } else if (IS_TYPE_POINTER(srctype)) {
        GEN_NODE_TO_REG(inner, outputReg, 0, output);
        if (dsttype->size < srctype->size && output->optype != OpndType_GPR) {
            Coerce_to_register(output, srctype, outputReg);
        }
    } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(srctype)) {
        if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(dsttype)) {
            GEN_NODE_TO_REG(inner, outputReg, 0, output);
            ENSURE_FPR(output, srctype, outputReg);
            if (dsttype->size == 4 && srctype->size != 4) {
                resultReg = outputReg ? outputReg : ALLOC_FPR();
                emitpcode(PC_FRSP, resultReg, output->reg);
                output->optype = OpndType_FPR;
                output->reg = resultReg;
            }
        } else if (IS_TYPE_VEC2X32FLOAT(dsttype)) {
            GEN_NODE_TO_REG(inner, outputReg, 0, output);
            ENSURE_FPR(output, srctype, outputReg);
            resultReg = outputReg ? outputReg : ALLOC_FPR();
            if (srctype->size != 4) {
                emitpcode(PC_FRSP, resultReg, output->reg);
                emitpcode(PC_PS_MERGE00, resultReg, resultReg, resultReg);
            } else {
                emitpcode(PC_PS_MERGE00, resultReg, output->reg, output->reg);
            }
            output->optype = OpndType_FPR;
            output->reg = resultReg;
        } else if (dsttype->size < 4 &&
                   srctype->size == 4 &&
                   copts.gecko_float_typecons &&
                   copts.processor == CPU_PPCGEKKO) {
            fprTemp = ALLOC_FPR();
            gprTemp = ALLOC_GPR();
            GEN_NODE(inner, output);
            reg = ALLOC_GPR();
            if (is_unsigned(dsttype)) {
                gqr2 = (short)((dsttype->size == 1) ? 4 : 5);
            } else {
                gqr2 = (short)((dsttype->size == 1) ? 6 : 7);
            }
            emitpcode(PC_LI, reg, gqr2);
            emitpcode(PC_ORIS, reg, reg, gqr2);
            emitpcode(PC_MTSPR, SPR_GQR1, reg);
            symbol_operand(&dstTemp, maketemporary(dsttype));
            coerce_to_addressable(&dstTemp);
            if (output->optype != OpndType_FPR) {
                ENSURE_FPR(output, srctype, fprTemp);
                gecko_load_store_register(fprTemp, dstTemp.reg, dstTemp.object, 0, 1, SPR_GQR1, 0);
            } else {
                gecko_load_store_register(
                    output->reg, dstTemp.reg, dstTemp.object, 0, 1, SPR_GQR1, 0
                );
            }
            load_store_register(
                (dsttype->size == 1) ? PC_LBZ : PC_LHZ, gprTemp, dstTemp.reg, dstTemp.object, 0
            );
            output->optype = OpndType_GPR;
            output->reg = gprTemp;
        } else if (is_unsigned(dsttype) && dsttype->size == 4) {
            GEN_NODE_TO_REG(inner, 1, 0, output);
            ENSURE_FPR(output, srctype, 1);
            convert_floating_to_unsigned(output, outputReg);
        } else {
            GEN_NODE(inner, output);
            ENSURE_FPR(output, srctype, 0);
            convert_floating_to_integer(output, outputReg);
        }
    } else if (IS_TYPE_VECTOR(srctype) && IS_TYPE_VECTOR(dsttype)) {
        GEN_NODE_TO_REG(inner, outputReg, 0, output);
        ENSURE_VR(output, srctype, outputReg);
    } else if (srctype->size == dsttype->size) {
        GEN_NODE_TO_REG(inner, outputReg, 0, output);
    } else {
        CError_FATAL(2469);
    }
}

void gen_BITFIELD(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    CError_FATAL(2483);
}

void gen_INTCONST(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    if (TYPE_IS_8BYTES(expr->rtype)) {
        I8_gen_INTCONST(expr, outputReg, outputRegHi, output);
        return;
    }

    output->optype = OpndType_Absolute;
    output->immediate = CInt64_GetULong(&expr->data.intval);
}

void gen_FLOATCONST(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    if (copts.soft_float) {
        SFPE_gen_FLOATCONST(expr, outputReg, outputRegHi, output);
    } else {
        CError_FATAL(2539);
    }
}

void gen_STRINGCONST(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    CError_FATAL(2553);
}

static Boolean COND_is_ABS_MatchNodes(ENode* cond, ENode* expr1, ENode* expr2) {
    if (cond->type != expr1->type || cond->type != expr2->type) {
        return FALSE;
    }

    if (!(TYPE_FITS_IN_REGISTER(cond->rtype) &&
          TYPE_FITS_IN_REGISTER(expr1->rtype) &&
          TYPE_FITS_IN_REGISTER(expr2->rtype))) {
        return FALSE;
    }

    if (cond->rtype->size != expr1->rtype->size || cond->rtype->size != expr2->rtype->size) {
        return FALSE;
    }

    switch (cond->type) {
        case EOBJREF:
            if (cond->data.objref != expr1->data.objref ||
                cond->data.objref != expr2->data.objref) {
                return FALSE;
            }
            return TRUE;
        case EINDIRECT:
        case ETYPCON:
            return COND_is_ABS_MatchNodes(
                cond->data.monadic, expr1->data.monadic, expr2->data.monadic
            );
        default:
            return FALSE;
    }
}

static ENode* COND_is_ABS(ENode* cond, ENode* expr1, ENode* expr2) {
    ENode* swapNode;
    ENode* inner;

    int parity = 0;
    while (ENODE_IS(cond, ELOGNOT)) {
        parity = (parity + 1) & 1;
        cond = cond->data.monadic;
    }

    if (parity) {
        swapNode = expr1;
        expr1 = expr2;
        expr2 = swapNode;
    }

    switch (cond->type) {
        case ELESS:
        case ELESSEQU:
            swapNode = expr1;
            expr1 = expr2;
            expr2 = swapNode;
            break;
        case EGREATER:
        case EGREATEREQU:
            break;
        default:
            return NULL;
    }

    if (IS_INT_CONST_ZERO(cond->data.diadic.right)) {
        inner = cond->data.diadic.left;
    } else if (IS_INT_CONST_ZERO(cond->data.diadic.left)) {
        inner = cond->data.diadic.left;
        swapNode = expr1;
        expr1 = expr2;
        expr2 = swapNode;
    } else {
        return NULL;
    }

    if (ENODE_IS(expr1, EADD) && ENODE_IS(expr2, ESUB)) {
        if (COND_is_ABS_MatchNodes(inner, expr1->data.diadic.right, expr2->data.diadic.right)) {
            return expr1;
        } else {
            return NULL;
        }
    }

    if (!ENODE_IS(expr2, EMONMIN)) {
        return NULL;
    }

    expr2 = expr2->data.monadic;
    if (COND_is_ABS_MatchNodes(inner, expr1, expr2)) {
        return expr1;
    }

    return NULL;
}

static int COND_has_const(ENode* expr1, ENode* expr2) {
    SInt32 diff;
    int result = 0;

    if (IS_INT_CONST(expr1)) {
        result += 1;
    }
    if (IS_INT_CONST(expr2)) {
        result += 2;
    }

    if (result & 1) {
        if (IS_INT_CONST_ZERO(expr1)) {
            return 5;
        }
    }
    if (result & 2) {
        if (IS_INT_CONST_ZERO(expr2)) {
            return 6;
        }
    }

    if (result == 3) {
        diff = expr1->data.intval.lo - expr2->data.intval.lo;
        if (diff == 1 || diff == -1) {
            return 4;
        }
    }

    return result;
}

static Boolean COND_is_COMPARE(
    ENode* cond, ENode* expr1, ENode* expr2, short outputReg, Operand* output
) {
    SInt32 left;
    SInt32 right;
    int parity;
    int negate;
    ENodeType nt;

    while (ENODE_IS(expr1, ETYPCON) && TYPE_FITS_IN_REGISTER(expr1->rtype)) {
        expr1 = expr1->data.monadic;
    }
    while (ENODE_IS(expr2, ETYPCON) && TYPE_FITS_IN_REGISTER(expr2->rtype)) {
        expr2 = expr2->data.monadic;
    }

    if (!(ENODE_IS(expr1, EINTCONST) &&
          TYPE_FITS_IN_REGISTER(expr1->rtype) &&
          CInt64_IsInRange(expr1->data.intval, 4))) {
        return FALSE;
    }
    if (!(ENODE_IS(expr2, EINTCONST) &&
          TYPE_FITS_IN_REGISTER(expr2->rtype) &&
          CInt64_IsInRange(expr2->data.intval, 4))) {
        return FALSE;
    }

    left = CInt64_GetULong(&expr1->data.intval);
    right = CInt64_GetULong(&expr2->data.intval);
    parity = FALSE;
    negate = FALSE;
    switch (left) {
        case 1:
            if (right != 0) {
                return FALSE;
            }
            break;
        case 0:
            parity = TRUE;
            if (right == -1) {
                negate = TRUE;
            } else if (right != 1) {
                return FALSE;
            }
            break;
        case -1:
            if (right != 0) {
                return FALSE;
            }
            negate = TRUE;
            break;
        default:
            return FALSE;
    }

    while (ENODE_IS(cond, ELOGNOT)) {
        parity = (parity + 1) & 1;
        cond = cond->data.monadic;
    }

    if (parity) {
        nt = invert_relop(cond->type);
        if (nt == cond->type) {
            return FALSE;
        }
        cond->type = nt;
    }

    if (negate) {
        gen_negated_condition_gpr(cond, output, outputReg);
    } else {
        gen_condition_gpr(cond, output, outputReg);
    }

    return TRUE;
}

void gen_COND(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* expr1;
    ENode* expr2;
    Type* type;
    ENode* cond;
    PCodeLabel* label1;
    PCodeLabel* label2;
    PCodeLabel* label3;
    Operand op1;
    Operand op2;
    int has_const;
    int reg1;
    SInt32 reg2;
    int reg3;
    short align;
    short max_align;

    expr1 = expr->data.cond.expr1;
    expr2 = expr->data.cond.expr2;
    type = expr->rtype;

    label1 = makepclabel();
    label2 = makepclabel();
    label3 = makepclabel();

    memclrw(&op1, sizeof(Operand));
    memclrw(&op2, sizeof(Operand));

    cond = evaluate_and_skip_comma(expr->data.cond.cond);

    if (TOC_use_fsel(expr)) {
        ENode* left;
        ENode* right;
        ENode* swapNode;
        ENodeType nt;
        Boolean leftIsZero;
        Operand op;
        int fneg_reg;
        int fneg_reg2;
        int fneg_reg3;
        int fsel_reg;
        int final_reg;

        left = cond->data.diadic.left;
        right = cond->data.diadic.right;
        nt = cond->type;
        leftIsZero = FALSE;
        memclrw(&op, sizeof(Operand));

        switch (nt) {
            case EGREATEREQU:
            case EEQU:
                break;
            case EGREATER:
                swapNode = left;
                left = right;
                right = swapNode;
            case ELESS:
            case ENOTEQU:
                swapNode = expr1;
                expr1 = expr2;
                expr2 = swapNode;
                break;
            case ELESSEQU:
                swapNode = left;
                left = right;
                right = swapNode;
                break;
            default:
                CError_FATAL(3025);
        }

        if (ENODE_IS(left, EFLOATCONST) && CMach_FloatIsZero(left->data.floatval)) {
            GEN_NODE(right, &op);
            ENSURE_FPR(&op, right->rtype, 0);
            leftIsZero = TRUE;
        } else if (ENODE_IS(right, EFLOATCONST) && CMach_FloatIsZero(right->data.floatval)) {
            GEN_NODE(left, &op);
            ENSURE_FPR(&op, left->rtype, 0);
        } else {
            fp_binary_operator((type->size == 4) ? PC_FSUBS : PC_FSUB, left, right, 0, &op);
        }

        switch (cond->type) {
            case EEQU:
            case ENOTEQU:
                if (leftIsZero) {
                    GEN_NODE(expr1, &op1);
                    ENSURE_FPR(&op1, expr1->rtype, 0);
                    GEN_NODE(expr2, &op2);
                    ENSURE_FPR(&op2, expr2->rtype, 0);

                    fneg_reg = ALLOC_FPR();
                    emitpcode(PC_FNEG, fneg_reg, op.reg);
                    fsel_reg = ALLOC_FPR();
                    emitpcode(PC_FSEL, fsel_reg, op.reg, op1.reg, op2.reg);
                    final_reg = outputReg ? outputReg : ALLOC_FPR();
                    emitpcode(PC_FSEL, final_reg, fneg_reg, fsel_reg, op2.reg);
                } else {
                    GEN_NODE(expr1, &op1);
                    ENSURE_FPR(&op1, expr1->rtype, 0);
                    GEN_NODE(expr2, &op2);
                    ENSURE_FPR(&op2, expr2->rtype, 0);

                    fneg_reg2 = ALLOC_FPR();
                    emitpcode(PC_FNEG, fneg_reg2, op.reg);
                    fsel_reg = ALLOC_FPR();
                    emitpcode(PC_FSEL, fsel_reg, op.reg, op1.reg, op2.reg);
                    final_reg = outputReg ? outputReg : ALLOC_FPR();
                    emitpcode(PC_FSEL, final_reg, fneg_reg2, fsel_reg, op2.reg);
                }
                break;
            case ELESS:
            case EGREATER:
            case ELESSEQU:
            case EGREATEREQU:
                GEN_NODE(expr1, &op1);
                ENSURE_FPR(&op1, expr1->rtype, 0);
                GEN_NODE(expr2, &op2);
                ENSURE_FPR(&op2, expr2->rtype, 0);

                fneg_reg3 = op.reg;
                if (leftIsZero) {
                    fneg_reg3 = ALLOC_FPR();
                    emitpcode(PC_FNEG, fneg_reg3, op.reg);
                }

                final_reg = outputReg ? outputReg : ALLOC_FPR();
                emitpcode(PC_FSEL, final_reg, fneg_reg3, op1.reg, op2.reg);
                break;
            default:
                CError_FATAL(3107);
        }

        output->optype = OpndType_FPR;
        output->reg = final_reg;
        return;
    }

    if (TOC_use_isel(expr, FALSE)) {
        Operand condOp;
        Operand swapOp;
        int bit;
        int swap;

        memclrw(&condOp, sizeof(Operand));
        gen_condition(cond, &condOp);
        GEN_NODE(expr1, &op1);
        ENSURE_GPR(&op1, expr1->rtype, 0);
        GEN_NODE(expr2, &op2);
        ENSURE_GPR(&op2, expr2->rtype, 0);

        reg1 = outputReg ? outputReg : ALLOC_GPR();
        bit = 0;
        swap = FALSE;
        switch (condOp.regOffset) {
            case ENOTEQU:
                swap = TRUE;
            case EEQU:
                bit = 2;
                break;
            case EGREATEREQU:
                swap = TRUE;
            case ELESS:
                bit = 0;
                break;
            case ELESSEQU:
                swap = TRUE;
            case EGREATER:
                bit = 1;
                break;
        }
        if (swap) {
            swapOp = op1;
            op1 = op2;
            op2 = swapOp;
        }
        switch (bit) {
            case 0:
                emitpcode(PC_ISELLT, reg1, op1.reg, op2.reg, condOp.reg);
                break;
            case 1:
                emitpcode(PC_ISELGT, reg1, op1.reg, op2.reg, condOp.reg);
                break;
            case 2:
                emitpcode(PC_ISELEQ, reg1, op1.reg, op2.reg, condOp.reg);
                break;
            default:
                CError_FATAL(3175);
        }
        output->optype = OpndType_GPR;
        output->reg = reg1;
        return;
    }

    if (TOC_use_isel(expr, TRUE)) {
        Operand isel_op1;
        Operand isel_op2;
        ENode* x;
        ENode* y;
        ENode* abs_expr;

        memclrw(&isel_op1, sizeof(Operand));
        memclrw(&isel_op2, sizeof(Operand));

        if (COND_is_COMPARE(cond, expr1, expr2, outputReg, output)) {
            return;
        }

        if ((abs_expr = COND_is_ABS(cond, expr1, expr2))) {
            if (ENODE_IS(expr1, EADD) && ENODE_IS(expr2, ESUB)) {
                x = expr1->data.diadic.left;
                y = expr2->data.diadic.right;
                if (y->hascall) {
                    GEN_NODE(y, &op2);
                    ENSURE_GPR(&op2, y->rtype, 0);

                    GEN_NODE(x, &op1);
                    if (op1.optype >= OpndType_IndirectGPR_ImmOffset) {
                        ENSURE_GPR(&op1, x->rtype, 0);
                    }
                } else {
                    GEN_NODE(x, &op1);
                    if (op1.optype >= OpndType_IndirectGPR_ImmOffset) {
                        ENSURE_GPR(&op1, x->rtype, 0);
                    }

                    GEN_NODE(y, &op2);
                    ENSURE_GPR(&op2, y->rtype, 0);
                }

                reg1 = ALLOC_GPR();
                emitpcode(PC_SRAWI, reg1, op2.reg, 31);
                reg2 = ALLOC_GPR();
                emitpcode(PC_XOR, reg2, reg1, op2.reg);
                reg3 = ALLOC_GPR();
                emitpcode(PC_SUBF, reg3, reg1, reg2);
                op2.optype = OpndType_GPR;
                op2.reg = reg3;
                combine(&op1, &op2, outputReg, output);
            } else {
                GEN_NODE(abs_expr, output);
                ENSURE_GPR(output, abs_expr->rtype, 0);

                reg1 = ALLOC_GPR();
                emitpcode(PC_SRAWI, reg1, output->reg, 31);
                reg2 = ALLOC_GPR();
                emitpcode(PC_XOR, reg2, reg1, output->reg);
                reg3 = outputReg ? outputReg : ALLOC_GPR();
                emitpcode(PC_SUBF, reg3, reg1, reg2);
                output->optype = OpndType_GPR;
                output->reg = reg3;
            }
            return;
        }

        if ((has_const = COND_has_const(expr1, expr2))) {
            switch (COND_has_const(expr1, expr2)) {
                case 0:
                case 2:
                    break;
                case 3:
                case 4:
                    if (has_const == 4) {
                        if (expr1->data.intval.lo < expr2->data.intval.lo) {
                            gen_negated_condition_gpr(cond, &isel_op1, 0);
                        } else {
                            gen_condition_gpr(cond, &isel_op1, 0);
                        }

                        GEN_NODE(expr1, &op1);
                        GEN_NODE(expr2, &op2);
                        reg1 = ALLOC_GPR();
                        ENSURE_GPR(&op2, expr2->rtype, reg1);
                        emitpcode(PC_ADD, reg1, isel_op1.reg, op2.reg);
                        if (outputReg) {
                            emitpcode(PC_MR, reg2 = outputReg, reg1);
                            reg1 = reg2;
                        }
                        output->optype = OpndType_GPR;
                        output->reg = reg1;
                        return;
                    }
                    break;
                case 5:
                case 6:
                    gen_negated_condition_gpr(cond, &isel_op1, 0);
                    ENSURE_GPR(&isel_op1, (Type*)&stunsignedint, 0);
                    GEN_NODE(expr1, &op1);
                    GEN_NODE(expr2, &op2);

                    reg1 = outputReg ? outputReg : ALLOC_GPR();
                    if (op1.optype == OpndType_Absolute && op1.immediate == 0) {
                        ENSURE_GPR(&op2, expr2->rtype, 0);
                        emitpcode(PC_ANDC, reg1, op2.reg, isel_op1.reg);
                    } else if (op2.optype == OpndType_Absolute && op2.immediate == 0) {
                        ENSURE_GPR(&op1, expr1->rtype, 0);
                        emitpcode(PC_AND, reg1, op1.reg, isel_op1.reg);
                    } else {
                        CError_FATAL(3364);
                    }

                    output->optype = OpndType_GPR;
                    output->reg = reg1;
                    return;
                case 1:
                    reg2 = ALLOC_GPR();
                    reg1 = reg2;
                    logical_expression_nobranch(cond, FALSE, &isel_op2);

                    GEN_NODE_TO_REG(expr1, reg1, 0, &op1);
                    ENSURE_GPR(&op1, expr1->rtype, reg1);
                    if (op1.reg != reg1) {
                        emitpcode(PC_MR, reg1, op1.reg);
                    }

                    branch_conditional(isel_op2.reg, isel_op2.regOffset, 1, label2);
                    branch_label(label1);

                    GEN_NODE_TO_REG(expr2, reg1, 0, &op2);
                    ENSURE_GPR(&op2, expr2->rtype, reg1);
                    if (op2.reg != reg1) {
                        emitpcode(PC_MR, reg1, op2.reg);
                    }

                    branch_label(label2);

                    if (outputReg) {
                        emitpcode(PC_MR, reg2 = outputReg, reg1);
                        reg1 = reg2;
                    }

                    output->optype = OpndType_GPR;
                    output->reg = reg1;
                    return;

                default:
                    CError_FATAL(3412);
            }
        }

        reg1 = ALLOC_GPR();
        logical_expression_nobranch(cond, FALSE, &isel_op2);

        GEN_NODE_TO_REG(expr2, reg1, 0, &op2);
        ENSURE_GPR(&op2, expr2->rtype, reg1);
        if (op2.reg != reg1) {
            emitpcode(PC_MR, reg1, op2.reg);
        }

        branch_conditional(isel_op2.reg, isel_op2.regOffset, 0, label2);
        branch_label(label1);

        GEN_NODE_TO_REG(expr1, reg1, 0, &op1);
        ENSURE_GPR(&op1, expr1->rtype, reg1);
        if (op1.reg != reg1) {
            emitpcode(PC_MR, reg1, op1.reg);
        }

        branch_label(label2);

        if (outputReg) {
            emitpcode(PC_MR, reg2 = outputReg, reg1);
            reg1 = reg2;
        }

        output->optype = OpndType_GPR;
        output->reg = reg1;
        return;
    }

    logical_expression(cond, label1, label2, label1);
    branch_label(label1);

    if (type->type == TYPEVOID || expr->ignored) {
        GEN_NODE(expr1, &op1);
        branch_always(label3);
        branch_label(label2);
        GEN_NODE(expr2, &op2);
    } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) && !TYPE_IS_SOFT_FLOAT(type)) {
        if (expr1->hascall || expr2->hascall) {
            reg1 = ALLOC_FPR();
        } else {
            reg1 = outputReg ? outputReg : ALLOC_FPR();
        }

        GEN_NODE_TO_REG(expr1, reg1, 0, &op1);
        ENSURE_FPR(&op1, expr1->rtype, reg1);
        if (op1.reg != reg1) {
            emitpcode(PC_FMR, reg1, op1.reg);
        }

        branch_always(label3);
        branch_label(label2);

        GEN_NODE_TO_REG(expr2, reg1, 0, &op2);
        ENSURE_FPR(&op2, expr2->rtype, reg1);
        if (op2.reg != reg1) {
            emitpcode(PC_FMR, reg1, op2.reg);
        }

        output->optype = OpndType_FPR;
        output->reg = reg1;
    } else if (TYPE_IS_8BYTES(type) || (TYPE_IS_SOFT_FLOAT(type) && type->size != 4)) {
        if (expr1->hascall || expr2->hascall) {
            reg1 = ALLOC_GPR();
            reg3 = ALLOC_GPR();
            reg2 = reg3;
        } else {
            reg1 = outputReg ? outputReg : ALLOC_GPR();
            reg3 = outputRegHi ? outputRegHi : ALLOC_GPR();
            reg2 = reg3;
        }

        GEN_NODE_TO_REG(expr1, reg1, reg2, &op1);
        coerce_to_register_pair(&op1, expr1->rtype, reg1, reg2);

        branch_always(label3);
        branch_label(label2);

        GEN_NODE_TO_REG(expr2, reg1, reg2, &op2);
        coerce_to_register_pair(&op2, expr2->rtype, reg1, reg2);

        output->optype = OpndType_GPRPair;
        output->reg = reg1;
        output->regHi = reg2;
    } else if (TYPE_FITS_IN_REGISTER(type) || (TYPE_IS_SOFT_FLOAT(type) && type->size == 4)) {
        if (expr1->hascall || expr2->hascall) {
            reg1 = ALLOC_GPR();
        } else {
            reg1 = outputReg ? outputReg : ALLOC_GPR();
        }

        GEN_NODE_TO_REG(expr1, reg1, 0, &op1);
        ENSURE_GPR(&op1, expr1->rtype, reg1);
        if (op1.reg != reg1) {
            emitpcode(PC_MR, reg1, op1.reg);
        }

        branch_always(label3);
        branch_label(label2);

        GEN_NODE_TO_REG(expr2, reg1, 0, &op2);
        ENSURE_GPR(&op2, expr2->rtype, reg1);
        if (op2.reg != reg1) {
            emitpcode(PC_MR, reg1, op2.reg);
        }

        output->optype = OpndType_GPR;
        output->reg = reg1;
    } else if (IS_TYPE_VECTOR(type)) {
        if (expr1->hascall || expr2->hascall) {
            reg1 = ALLOC_VR();
        } else {
            reg1 = outputReg ? outputReg : ALLOC_VR();
        }

        GEN_NODE_TO_REG(expr1, reg1, 0, &op1);
        ENSURE_VR(&op1, expr1->rtype, reg1);
        if (op1.reg != reg1) {
            emitpcode(PC_VMR, reg1, op1.reg);
        }

        branch_always(label3);
        branch_label(label2);

        GEN_NODE_TO_REG(expr2, reg1, 0, &op2);
        ENSURE_VR(&op2, expr2->rtype, reg1);
        if (op2.reg != reg1) {
            emitpcode(PC_VMR, reg1, op2.reg);
        }

        output->optype = OpndType_VR;
        output->reg = reg1;
    } else {
        symbol_operand(output, maketemporary(type));
        indirect(output, NULL);
        coerce_to_addressable(output);

        GEN_NODE(expr1, &op1);

        if (op1.object) {
            if (op1.object->datatype == DLOCAL &&
                (op1.object->u.var.info->flags & VarInfoFlag_ParamArea)) {
                align = CMach_ArgumentAlignment(type);
            } else {
                align = CMach_AllocationAlignment(type, op1.object->qual);
            }
        } else {
            align = CMach_AllocationAlignment(type, 0);
        }

        max_align = CMach_AllocationAlignment(type, 0);
        if (align > max_align) {
            align = max_align;
        }

        if ((type->type == TYPESTRUCT || type->type == TYPECLASS) &&
            !Type_RequiresMemoryReturn(type)) {
            if (op1.optype == OpndType_GPRPair) {
                store_small_block_from_reg_pair(op1.reg, op1.regHi, output, type, align);
            } else if (op1.optype == OpndType_GPR) {
                store_small_block_from_reg(op1.reg, output, type, align);
            } else {
                move_block(output, &op1, type->size, align);
            }
        } else {
            move_block(output, &op1, type->size, align);
        }

        branch_always(label3);
        branch_label(label2);

        GEN_NODE(expr2, &op2);

        if (op2.object) {
            if (op2.object->datatype == DLOCAL &&
                (op2.object->u.var.info->flags & VarInfoFlag_ParamArea)) {
                align = CMach_ArgumentAlignment(type);
            } else {
                align = CMach_AllocationAlignment(type, op2.object->qual);
            }
        } else {
            align = CMach_AllocationAlignment(type, 0);
        }

        if (align > max_align) {
            align = max_align;
        }

        if ((type->type == TYPESTRUCT || type->type == TYPECLASS) &&
            !Type_RequiresMemoryReturn(type)) {
            if (op2.optype == OpndType_GPRPair) {
                store_small_block_from_reg_pair(op2.reg, op2.regHi, output, type, align);
            } else if (op2.optype == OpndType_GPR) {
                store_small_block_from_reg(op2.reg, output, type, align);
            } else {
                move_block(output, &op2, type->size, align);
            }
        } else {
            move_block(output, &op2, type->size, align);
        }
    }

    branch_label(label3);
}

static Boolean CONDASS_is_ABS(ENode* cond, ENode* expr1, ENode* expr2) {
    ENode* inner;

    int parity = 0;
    while (ENODE_IS(cond, ELOGNOT)) {
        parity = (parity + 1) & 1;
        cond = cond->data.monadic;
    }

    if (IS_INT_CONST_ZERO(cond->data.diadic.right)) {
        inner = cond->data.diadic.left;
    } else if (IS_INT_CONST_ZERO(cond->data.diadic.left)) {
        inner = cond->data.diadic.left;
        parity = (parity + 1) & 1;
    } else {
        return FALSE;
    }

    switch (cond->type) {
        case EGREATER:
        case EGREATEREQU:
            if (!parity) {
                return FALSE;
            }
            break;
        case ELESS:
        case ELESSEQU:
            if (parity) {
                return FALSE;
            }
            break;
        default:
            return FALSE;
    }

    if (!ENODE_IS(expr2, EMONMIN)) {
        return FALSE;
    }

    expr2 = expr2->data.monadic;
    if (ENODE_IS(inner, EASS)) {
        inner = inner->data.diadic.left;
        if (!ENODE_IS(expr2, EINDIRECT)) {
            return FALSE;
        }
        expr2 = expr2->data.monadic;
        if (!ENODE_IS(expr1, EINDIRECT)) {
            return FALSE;
        }
        expr1 = expr1->data.monadic;
    }

    return COND_is_ABS_MatchNodes(inner, expr1, expr2);
}

static int CONDASS_is_OPASS_One(ENode* target, ENode* source, SInt32* value, ENodeType* nodetype) {
    Type* type;

    type = target->rtype;
    if (!ENODE_IS(target, EINDIRECT)) {
        return FALSE;
    }
    target = target->data.monadic;
    if (!ENODE_IS(target, EOBJREF)) {
        return FALSE;
    }

    if (ENODE_IS(source, ETYPCON) && source->rtype == type) {
        source = source->data.monadic;
    }

    if (!ENODE_IS(source, EOR) && !ENODE_IS(source, EADD) && !ENODE_IS(source, ESUB)) {
        return FALSE;
    }

    *nodetype = source->type;
    if (!IS_INT_CONST(source->data.diadic.right)) {
        return FALSE;
    }
    *value = source->data.diadic.right->data.intval.lo;

    if (*value != 1 && *value != -1) {
        return FALSE;
    }

    source = source->data.diadic.left;
    if (ENODE_IS(source, ETYPCON) && TYPE_FITS_IN_REGISTER(source->rtype)) {
        source = source->data.monadic;
    }

    if (!ENODE_IS(source, EINDIRECT)) {
        return FALSE;
    }
    source = source->data.monadic;
    if (!ENODE_IS(source, EOBJREF)) {
        return FALSE;
    }

    if (target->data.objref == source->data.objref) {
        return TRUE;
    }
    return FALSE;
}

void gen_CONDASS(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* cond;
    ENode* expr1;
    ENode* expr2;
    Type* type;
    PCodeLabel* label1;
    PCodeLabel* label2;
    Operand op1;
    Operand op2;
    Operand dest;
    int reg1;
    int reg2;

    expr1 = expr->data.cond.expr1;
    expr2 = expr->data.cond.expr2;
    type = expr->rtype;

    label1 = makepclabel();
    label2 = makepclabel();

    memclrw(&op1, sizeof(Operand));
    memclrw(&op2, sizeof(Operand));
    memclrw(&dest, sizeof(Operand));

    cond = evaluate_and_skip_comma(expr->data.cond.cond);

    if (TOC_use_fsel(expr)) {
        ENode* left;
        ENode* right;
        ENode* swapNode;
        ENodeType nt;
        Boolean leftIsZero;
        Boolean armsSwapped;
        Operand op;
        int fsel_reg;
        int fneg_reg;
        int final_reg;

        left = cond->data.diadic.left;
        right = cond->data.diadic.right;
        nt = cond->type;
        leftIsZero = FALSE;
        memclrw(&op, sizeof(Operand));

        CError_ASSERT(3948, ENODE_IS(expr1, EINDIRECT));
        CError_ASSERT(3949, ENODE_IS(expr1->data.monadic, EOBJREF));

        outputReg = OBJECT_REG(expr1->data.monadic->data.objref);
        final_reg = outputReg ? outputReg : ALLOC_FPR();

        switch (nt) {
            case EGREATER:
                swapNode = left;
                left = right;
                right = swapNode;
            case ELESS:
            case ENOTEQU:
                swapNode = expr1;
                expr1 = expr2;
                expr2 = swapNode;
                armsSwapped = TRUE;
                break;
            case ELESSEQU:
                swapNode = left;
                left = right;
                right = swapNode;
                armsSwapped = FALSE;
                break;
            case EGREATEREQU:
            case EEQU:
                armsSwapped = FALSE;
                break;
            default:
                CError_FATAL(3988);
        }

        if (ENODE_IS(left, EFLOATCONST) && CMach_FloatIsZero(left->data.floatval)) {
            GEN_NODE(right, &op);
            ENSURE_FPR(&op, right->rtype, 0);
            leftIsZero = TRUE;
        } else if (ENODE_IS(right, EFLOATCONST) && CMach_FloatIsZero(right->data.floatval)) {
            GEN_NODE(left, &op);
            ENSURE_FPR(&op, left->rtype, 0);
        } else {
            fp_binary_operator((type->size == 4) ? PC_FSUBS : PC_FSUB, left, right, 0, &op);
        }

        switch (cond->type) {
            case EEQU:
            case ENOTEQU:
                if (leftIsZero) {
                    GEN_NODE(expr1, &op1);
                    dest = op1;
                    ENSURE_FPR(&op1, expr1->rtype, 0);
                    GEN_NODE(expr2, &op2);
                    ENSURE_FPR(&op2, expr2->rtype, 0);

                    fneg_reg = ALLOC_FPR();
                    emitpcode(PC_FNEG, fneg_reg, op.reg);
                    fsel_reg = ALLOC_FPR();
                    emitpcode(PC_FSEL, fsel_reg, op.reg, op2.reg, op1.reg);
                    emitpcode(PC_FSEL, final_reg, fneg_reg, fsel_reg, op1.reg);
                } else {
                    GEN_NODE(expr1, &op1);
                    dest = op1;
                    ENSURE_FPR(&op1, expr1->rtype, 0);
                    GEN_NODE(expr2, &op2);
                    ENSURE_FPR(&op2, expr2->rtype, 0);

                    fneg_reg = ALLOC_FPR();
                    emitpcode(PC_FNEG, fneg_reg, op.reg);
                    fsel_reg = ALLOC_FPR();
                    emitpcode(PC_FSEL, fsel_reg, op.reg, op2.reg, op1.reg);
                    emitpcode(PC_FSEL, final_reg, fneg_reg, fsel_reg, op1.reg);
                }
                break;
            case ELESS:
            case EGREATER:
            case ELESSEQU:
            case EGREATEREQU:
                GEN_NODE(expr1, &op1);
                GEN_NODE(expr2, &op2);
                dest = armsSwapped ? op2 : op1;

                ENSURE_FPR(&op1, expr1->rtype, 0);
                ENSURE_FPR(&op2, expr2->rtype, 0);

                fneg_reg = op.reg;
                if (leftIsZero) {
                    fneg_reg = ALLOC_FPR();
                    emitpcode(PC_FNEG, fneg_reg, op.reg);
                }

                emitpcode(PC_FSEL, final_reg, fneg_reg, op2.reg, op1.reg);
                break;
            default:
                CError_FATAL(4083);
        }

        if (dest.optype != OpndType_FPR) {
            store_fp(final_reg, &dest, type);
        }

        output->optype = OpndType_FPR;
        output->reg = final_reg;
        return;
    }

    if (TOC_use_isel(expr, FALSE)) {
        Operand condOp;
        Operand swapOp;
        int bit;
        int swap;
        short objreg;

        memclrw(&condOp, sizeof(Operand));
        gen_condition(cond, &condOp);

        CError_ASSERT(4115, ENODE_IS(expr1, EINDIRECT));
        CError_ASSERT(4116, ENODE_IS(expr1->data.monadic, EOBJREF));

        objreg = OBJECT_REG(expr1->data.monadic->data.objref);

        GEN_NODE(expr1, &op1);
        dest = op1;
        ENSURE_GPR(&op1, expr1->rtype, 0);
        GEN_NODE(expr2, &op2);
        ENSURE_GPR(&op2, expr2->rtype, 0);

        reg1 = objreg ? objreg : ALLOC_GPR();
        bit = 0;
        swap = FALSE;
        switch (condOp.regOffset) {
            case ENOTEQU:
                swap = TRUE;
            case EEQU:
                bit = 2;
                break;
            case EGREATEREQU:
                swap = TRUE;
            case ELESS:
                bit = 0;
                break;
            case ELESSEQU:
                swap = TRUE;
            case EGREATER:
                bit = 1;
                break;
        }
        if (swap) {
            swapOp = op1;
            op1 = op2;
            op2 = swapOp;
        }
        switch (bit) {
            case 0:
                emitpcode(PC_ISELLT, reg1, op2.reg, op1.reg, condOp.reg);
                break;
            case 1:
                emitpcode(PC_ISELGT, reg1, op2.reg, op1.reg, condOp.reg);
                break;
            case 2:
                emitpcode(PC_ISELEQ, reg1, op2.reg, op1.reg, condOp.reg);
                break;
            default:
                CError_FATAL(4171);
        }
        if (dest.optype != OpndType_GPR) {
            store(reg1, &dest, type);
        }
        output->optype = OpndType_GPR;
        output->reg = reg1;
        return;
    }

    if (TOC_use_isel(expr, TRUE)) {
        Operand isel_op;

        memclrw(&isel_op, sizeof(Operand));
        CError_ASSERT(4210, ENODE_IS(expr1, EINDIRECT));
        CError_ASSERT(4212, ENODE_IS(expr1->data.monadic, EOBJREF));

        if (CONDASS_is_ABS(cond, expr1, expr2)) {
            if (ENODE_IS(cond->data.diadic.left, EASS)) {
                GEN_NODE(cond->data.diadic.left, &isel_op);
            } else if (ENODE_IS(cond->data.diadic.right, EASS)) {
                GEN_NODE(cond->data.diadic.right, &isel_op);
            }

            outputReg = OBJECT_REG(expr1->data.monadic->data.objref);
            CError_ASSERT(4223, outputReg);

            GEN_NODE(expr1, &op1);
            dest = op1;

            CError_ASSERT(4230, dest.optype == OpndType_GPR && dest.reg == outputReg);

            ENSURE_GPR(&op1, expr1->rtype, 0);
            if (expr1->rtype->size < 4) {
                extend32(output, expr1->rtype, dest.reg);
            }

            reg1 = ALLOC_GPR();
            reg2 = ALLOC_GPR();
            emitpcode(PC_SRAWI, reg1, op1.reg, 31);
            emitpcode(PC_XOR, reg2, reg1, op1.reg);
            emitpcode(PC_SUBF, outputReg, reg1, reg2);
            output->optype = OpndType_GPR;
            output->reg = dest.reg;

            if ((type = expr1->rtype)->size < 4) {
                extend32(output, type, dest.reg);
            }

            return;
        }
    }

    logical_expression(cond, label1, label2, label1);
    branch_label(label1);
    gen_ASS(expr, outputReg, outputRegHi, output);
    branch_label(label2);
}

void gen_FUNCCALL(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    if (is_intrinsic_function_call(expr)) {
        call_intrinsic_function(expr, outputReg, output);
    } else {
        call_function(expr, output);
    }
}

void gen_OBJREF(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Object* object = expr->data.objref;

    symbol_operand(output, object);
    if (is_pooled_object(object)) {
        output->optype = OpndType_Symbol;
        output->reg = GetPooledObjectRegister(object);
        output->regHi = 0;
        output->object = object;
        output->immOffset = 0;
        set_op_flags(output, expr);
    }
}

void gen_UNEXPECTED(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    CError_FATAL(4404);
}

static int small(ENode* expr) {
    Type* type;

    type = expr->rtype;
    if (!ENODE_IS(expr, ETYPCON)) {
        return FALSE;
    }

    do {
        expr = expr->data.monadic;
    } while (ENODE_IS(expr, ETYPCON) && (type = expr->rtype)->size == 4);

    return IS_TYPE_INT_OR_ENUM(type) &&
        ((type->size < 2) || (type->size == 2 && !is_unsigned(type)));
}

void binary_operator(Opcode opcode, ENode* left, ENode* right, short outputReg, Operand* output) {
    Operand opleft;
    Operand opright;
    SInt32 reg;

    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));

    if (right->hascall) {
        GEN_NODE_TO_GPR(right, &opright, right->rtype, 0);
        GEN_NODE_TO_GPR(left, &opleft, left->rtype, 0);
    } else {
        GEN_NODE_TO_GPR(left, &opleft, left->rtype, 0);
        GEN_NODE_TO_GPR(right, &opright, right->rtype, 0);
    }

    reg = outputReg ? outputReg : ALLOC_GPR();

    if (opcode == PC_MULLW && small(left)) {
        emitpcode(opcode, reg, opright.reg, opleft.reg);
    } else {
        emitpcode(opcode, reg, opleft.reg, opright.reg);
    }

    output->optype = OpndType_GPR;
    output->reg = reg;
}

static void binary_immediate(
    short opcode, ENode* left, SInt32 value, short outputReg, Operand* output
) {
    Operand opleft;
    int reg;

    memclrw(&opleft, sizeof(Operand));
    GEN_NODE_TO_GPR(left, &opleft, left->rtype, 0);

    reg = outputReg ? outputReg : ALLOC_GPR();

    if (opcode == PC_MULLI && value == 0) {
        emitpcode(PC_LI, reg, 0);
    } else if (opcode == PC_MULLI && value == 1) {
        emitpcode(PC_MR, reg, opleft.reg);
    } else {
        emitpcode(opcode, reg, opleft.reg, value);
    }

    output->optype = OpndType_GPR;
    output->reg = reg;
}

void unary_operator(Opcode opcode, ENode* expr, short outputReg, Operand* output) {
    Operand op;
    int reg;

    memclrw(&op, sizeof(Operand));
    GEN_NODE_TO_GPR(expr, &op, expr->rtype, 0);

    reg = outputReg ? outputReg : ALLOC_GPR();
    emitpcode(opcode, reg, op.reg);

    output->optype = OpndType_GPR;
    output->reg = reg;
}

static void or_xor_immediate(
    short opcode, ENode* expr, SInt32 value, short outputReg, Operand* output
) {
    Operand op;
    int reg;

    memclrw(&op, sizeof(Operand));
    GEN_NODE_TO_GPR(expr, &op, expr->rtype, 0);

    reg = outputReg ? outputReg : ALLOC_GPR();

    if (expr->rtype->size > 2 && value != (UInt16)value) {
        if (value & 0xFFFF) {
            if ((value & 0xFFFF8000) == 0xFFFF8000) {
                int valueReg = ALLOC_GPR();
                emitpcode(PC_LI, valueReg, value);
                emitpcode((opcode == PC_ORI) ? PC_OR : PC_XOR, reg, op.reg, valueReg);
            } else {
                emitpcode(
                    (opcode == PC_ORI) ? PC_ORIS : PC_XORIS, reg, op.reg, (value >> 16) & 0xFFFF
                );
                emitpcode(opcode, reg, reg, value & 0xFFFF);
            }
        } else {
            emitpcode((opcode == PC_ORI) ? PC_ORIS : PC_XORIS, reg, op.reg, (value >> 16) & 0xFFFF);
        }
    } else {
        emitpcode(opcode, reg, op.reg, value & 0xFFFF);
    }

    output->optype = OpndType_GPR;
    output->reg = reg;
}

static void shift_left_immediate(
    ENode* expr, short shift, short negate, short outputReg, Operand* output
) {
    Operand op;
    int reg;

    memclrw(&op, sizeof(Operand));
    GEN_NODE_TO_GPR(expr, &op, expr->rtype, 0);

    if (negate) {
        reg = ALLOC_GPR();
    } else {
        reg = outputReg ? outputReg : ALLOC_GPR();
    }

    shift &= 31;
    emitpcode(PC_RLWINM, reg, op.reg, shift, 0, 31 - shift);

    if (negate) {
        int negReg = outputReg ? outputReg : ALLOC_GPR();
        emitpcode(PC_NEG, negReg, reg);
        reg = negReg;
    }

    output->optype = OpndType_GPR;
    output->reg = reg;
}

static void shift_right_immediate(
    ENode* expr, Type* type, short shift, short outputReg, Operand* output
) {
    Operand op;
    SInt32 reg;

    memclrw(&op, sizeof(Operand));
    GEN_NODE_TO_GPR(expr, &op, expr->rtype, 0);

    reg = outputReg ? outputReg : ALLOC_GPR();

    shift &= 31;
    if (is_unsigned(type)) {
        emitpcode(PC_RLWINM, reg, op.reg, (32 - shift) & 31, 32 - type->size * 8 + shift, 31);
    } else {
        emitpcode(PC_SRAWI, reg, op.reg, shift);
    }

    output->optype = OpndType_GPR;
    output->reg = reg;
}

static void signed_divide_by_power_of_2(
    ENode* expr, int shift, int negate, short outputReg, Operand* output
) {
    Operand op;
    SInt32 reg;
    int tmpreg1;
    int tmpreg2;

    memclrw(&op, sizeof(Operand));
    GEN_NODE_TO_GPR(expr, &op, expr->rtype, 0);

    if (!copts.optimizesize && shift == 1) {
        tmpreg1 = ALLOC_GPR();
        emitpcode(PC_RLWINM, tmpreg1, op.reg, 1, 31, 31);
        tmpreg2 = ALLOC_GPR();
        emitpcode(PC_ADD, tmpreg2, tmpreg1, op.reg);
        reg = (outputReg && !negate) ? outputReg : ALLOC_GPR();
        emitpcode(PC_SRAWI, reg, tmpreg2, 1);
    } else {
        tmpreg1 = ALLOC_GPR();
        emitpcode(PC_SRAWI, tmpreg1, op.reg, shift);
        reg = (outputReg && !negate) ? outputReg : ALLOC_GPR();
        emitpcode(PC_ADDZE, reg, tmpreg1);
    }

    if (negate) {
        tmpreg1 = reg;
        reg = outputReg ? outputReg : ALLOC_GPR();
        emitpcode(PC_NEG, reg, tmpreg1);
    }

    output->optype = OpndType_GPR;
    output->reg = reg;
}

static void signed_mod_by_power_of_2(
    ENode* expr, int shift, int negate, short outputReg, Operand* output
) {
    Operand op;
    int tmpreg1;
    int tmpreg2;
    int tmpreg3;
    int tmpreg4;
    int reg;

    memclrw(&op, sizeof(Operand));
    GEN_NODE_TO_GPR(expr, &op, expr->rtype, 0);

    reg = outputReg ? outputReg : ALLOC_GPR();
    tmpreg1 = ALLOC_GPR();
    tmpreg2 = ALLOC_GPR();
    tmpreg3 = ALLOC_GPR();

    if (shift == 1) {
        emitpcode(PC_RLWINM, tmpreg1, op.reg, 1, 31, 31);
        emitpcode(PC_RLWINM, tmpreg2, op.reg, 0, 31, 31);
        emitpcode(PC_XOR, tmpreg3, tmpreg2, tmpreg1);
        emitpcode(PC_SUBF, reg, tmpreg1, tmpreg3);
    } else {
        tmpreg4 = ALLOC_GPR();
        emitpcode(PC_RLWINM, tmpreg1, op.reg, 32 - shift, 0, 31 - (32 - shift));
        emitpcode(PC_RLWINM, tmpreg2, op.reg, 1, 31, 31);
        emitpcode(PC_SUBF, tmpreg3, tmpreg2, tmpreg1);
        emitpcode(PC_RLWINM, tmpreg4, tmpreg3, shift, 0, 31);
        emitpcode(PC_ADD, reg, tmpreg4, tmpreg2);
    }

    output->optype = OpndType_GPR;
    output->reg = reg;
}

static void fp_binary_operator(
    short opcode, ENode* left, ENode* right, short outputReg, Operand* output
) {
    Operand opleft;
    Operand opright;
    int reg;

    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));

    if (right->hascall) {
        GEN_NODE_TO_FPR(right, &opright, right->rtype, 0);
        GEN_NODE_TO_FPR(left, &opleft, left->rtype, 0);
    } else {
        GEN_NODE_TO_FPR(left, &opleft, left->rtype, 0);
        GEN_NODE_TO_FPR(right, &opright, right->rtype, 0);
    }

    reg = outputReg ? outputReg : ALLOC_FPR();
    emitpcode(opcode, reg, opleft.reg, opright.reg);

    output->optype = OpndType_FPR;
    output->reg = reg;
}

void fp_unary_operator(Opcode opcode, ENode* expr, short outputReg, Operand* output) {
    Operand op;
    int reg;

    memclrw(&op, sizeof(Operand));
    GEN_NODE_TO_FPR(expr, &op, expr->rtype, 0);

    reg = outputReg ? outputReg : ALLOC_FPR();
    emitpcode(opcode, reg, op.reg);

    output->optype = OpndType_FPR;
    output->reg = reg;
}

void fp_multiply_add(
    Opcode opcode, ENode* a, ENode* b, ENode* c, short outputReg, Operand* output
) {
    Operand opA;
    Operand opB;
    Operand opC;
    SInt32 reg;

    memclrw(&opA, sizeof(Operand));
    memclrw(&opB, sizeof(Operand));
    memclrw(&opC, sizeof(Operand));

    if (c->hascall) {
        GEN_NODE_TO_FPR(c, &opC, c->rtype, 0);
        if (b->hascall) {
            GEN_NODE_TO_FPR(b, &opB, b->rtype, 0);
            GEN_NODE_TO_FPR(a, &opA, a->rtype, 0);
        } else {
            GEN_NODE_TO_FPR(a, &opA, a->rtype, 0);
            GEN_NODE_TO_FPR(b, &opB, b->rtype, 0);
        }
    } else {
        if (b->hascall) {
            GEN_NODE_TO_FPR(b, &opB, b->rtype, 0);
            GEN_NODE_TO_FPR(a, &opA, a->rtype, 0);
            GEN_NODE_TO_FPR(c, &opC, c->rtype, 0);
        } else {
            GEN_NODE_TO_FPR(a, &opA, a->rtype, 0);
            GEN_NODE_TO_FPR(b, &opB, b->rtype, 0);
            GEN_NODE_TO_FPR(c, &opC, c->rtype, 0);
        }
    }

    reg = outputReg ? outputReg : ALLOC_FPR();
    emitpcode(opcode, reg, opA.reg, opB.reg, opC.reg);

    output->optype = OpndType_FPR;
    output->reg = reg;
}

void gen_COMPARE(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    expr = evaluate_and_skip_comma(expr);
    if (TYPE_IS_SOFT_FLOAT(expr->data.diadic.right->rtype) ||
        TYPE_IS_SOFT_FLOAT(expr->data.diadic.left->rtype)) {
        SFPE_gen_condition(expr, output, 1);
        return;
    }
    if (TYPE_IS_8BYTES(expr->data.diadic.right->rtype) ||
        TYPE_IS_8BYTES(expr->data.diadic.left->rtype)) {
        I8_gen_condition(expr, output, TRUE);
        return;
    }
    gen_condition_gpr(expr, output, outputReg);
}

void gen_LOGICAL(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* inner;
    ENodeType op;

    expr = evaluate_and_skip_comma(expr);
    inner = evaluate_and_skip_comma(expr->data.monadic);
    expr->data.monadic = inner;

    if (ENODE_IS(expr, ELOGNOT) && !ENODE_IS2(inner, ELAND, ELOR)) {
        op = inner->type;
        if (ENODE_IS(inner, ELOGNOT)) {
            switch (inner->data.monadic->type) {
                case ELOGNOT:
                case ELESS:
                case EGREATER:
                case ELESSEQU:
                case EGREATEREQU:
                case EEQU:
                case ENOTEQU:
                case ELAND:
                case ELOR:
                    GEN_NODE(inner->data.monadic, output);
                    if (expr->data.monadic->rtype->size < 4) {
                        extend32(output, expr->data.monadic->rtype, 0);
                    }
                    ENSURE_GPR(output, expr->data.monadic->rtype, 0);
                    return;
                default:
                    break;
            }
        }

        if (ENODE_IS(inner, ENOTEQU) &&
            !TYPE_IS_8BYTES(inner->data.diadic.left->rtype) &&
            ENODE_IS(inner->data.diadic.right, EINTCONST) &&
            inner->data.diadic.right->data.intval.lo == 0) {
            int resultReg;
            int leadingZerosReg;
            GEN_NODE(inner->data.diadic.left, output);
            if (inner->data.diadic.left->rtype->size < 4) {
                extend32(output, inner->data.diadic.left->rtype, 0);
            }
            ENSURE_GPR(output, inner->data.diadic.left->rtype, 0);

            resultReg = ALLOC_GPR();
            leadingZerosReg = ALLOC_GPR();
            CError_ASSERT(5106, output->optype == OpndType_GPR);

            emitpcode(PC_CNTLZW, leadingZerosReg, output->reg);
            emitpcode(PC_RLWINM, resultReg, leadingZerosReg, 27, 5, 31);
            output->optype = OpndType_GPR;
            output->reg = resultReg;
        } else {
            int resultReg;
            int leadingZerosReg;
            ENodeType inverted;

            inverted = invert_relop(op);
            if (op != inverted && !IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(inner->data.diadic.left->rtype)) {
                inner->type = inverted;
                gen_COMPARE(inner, 0, 0, output);
                inner->type = inverted;
                return;
            }

            GEN_NODE(inner, output);
            if (inner->rtype->size < 4) {
                extend32(output, inner->rtype, 0);
            }
            ENSURE_GPR(output, inner->rtype, 0);

            resultReg = ALLOC_GPR();
            leadingZerosReg = ALLOC_GPR();
            CError_ASSERT(5136, output->optype == OpndType_GPR);

            emitpcode(PC_CNTLZW, leadingZerosReg, output->reg);
            emitpcode(PC_RLWINM, resultReg, leadingZerosReg, 27, 5, 31);
            output->optype = OpndType_GPR;
            output->reg = resultReg;
        }
    } else {
        PCodeLabel* label1;
        PCodeLabel* label2;
        int resultReg;

        label1 = makepclabel();
        label2 = makepclabel();

        resultReg = ALLOC_GPR();
        emitpcode(PC_LI, resultReg, 0);
        logical_expression(expr, label1, label2, label1);
        branch_label(label1);
        emitpcode(PC_LI, resultReg, 1);
        branch_label(label2);
        output->optype = OpndType_GPR;
        output->reg = resultReg;
    }
}

void gen_NULLCHECK(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* left;
    ENode* right;
    Operand opleft;
    Operand opright;
    PrecomputedOperand* precomp;
    int hasValue;
    int reg;
    PCodeLabel* label;

    left = expr->data.nullcheck.nullcheckexpr;
    right = expr->data.nullcheck.condexpr;
    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));
    hasValue = !IS_TYPE_VOID(expr->rtype) && !expr->ignored;

    GEN_NODE(left, &opleft);
    if (left->rtype->size < 4) {
        extend32(&opleft, left->rtype, 0);
    }
    ENSURE_GPR(&opleft, left->rtype, 0);

    precomp = lalloc(sizeof(PrecomputedOperand));
    precomp->precompid = expr->data.nullcheck.precompid;
    precomp->operand = opleft;
    precomp->next = precomputedoperands;
    precomputedoperands = precomp;

    emitpcode(PC_CMPI, 0, opleft.reg, 0);
    if (hasValue) {
        emitpcode(PC_MR, reg = ALLOC_GPR(), opleft.reg);
    }

    label = makepclabel();
    branch_conditional(0, EEQU, 1, label);
    GEN_NODE(right, &opright);
    precomputedoperands = precomputedoperands->next;

    if (hasValue) {
        ENSURE_GPR(&opright, right->rtype, reg);
        if (opright.reg != reg) {
            emitpcode(PC_MR, reg, opright.reg);
        }
        output->optype = OpndType_GPR;
        output->reg = reg;
    }

    branch_label(label);
}

void gen_PRECOMP(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    PrecomputedOperand* precomp;

    for (precomp = precomputedoperands; precomp; precomp = precomp->next) {
        if (precomp->precompid == expr->data.precompid) {
            break;
        }
    }

    *output = precomp->operand;
}

void logical_expression(ENode* cond, PCodeLabel* if_true, PCodeLabel* if_false, PCodeLabel* end) {
    PCodeLabel* label;
    Operand op;
    memclrw(&op, sizeof(Operand));

    cond = evaluate_and_skip_comma(cond);
    switch (cond->type) {
        case ELAND:
            label = makepclabel();
            logical_expression(cond->data.diadic.left, label, if_false, label);
            branch_label(label);
            logical_expression(cond->data.diadic.right, if_true, if_false, end);
            break;
        case ELOR:
            label = makepclabel();
            logical_expression(cond->data.diadic.left, if_true, label, label);
            branch_label(label);
            logical_expression(cond->data.diadic.right, if_true, if_false, end);
            break;
        case ELOGNOT:
            logical_expression(cond->data.monadic, if_false, if_true, end);
            break;
        case ELESS:
        case EGREATER:
        case ELESSEQU:
        case EGREATEREQU:
        case EEQU:
        case ENOTEQU:
            if (TYPE_IS_SOFT_FLOAT(cond->data.diadic.right->rtype) ||
                TYPE_IS_SOFT_FLOAT(cond->data.diadic.left->rtype)) {
                SFPE_gen_condition(cond, &op, 0);
            } else if (TYPE_IS_8BYTES(cond->data.diadic.right->rtype) ||
                       TYPE_IS_8BYTES(cond->data.diadic.left->rtype)) {
                I8_gen_condition(cond, &op, FALSE);
            } else {
                gen_condition(cond, &op);
            }

            if (end == if_true) {
                branch_conditional(op.reg, op.regOffset, 0, if_false);
            } else {
                branch_conditional(op.reg, op.regOffset, 1, if_true);
            }
            break;
        default:
            CError_FATAL(5413);
    }
}

static void logical_expression_nobranch(ENode* cond, Boolean invert, Operand* output) {
    cond = evaluate_and_skip_comma(cond);
    switch (cond->type) {
        case ELOGNOT:
            logical_expression_nobranch(cond->data.monadic, TRUE, output);
            break;
        case ELESS:
        case EGREATER:
        case ELESSEQU:
        case EGREATEREQU:
        case EEQU:
        case ENOTEQU:
            if (invert) {
                ENodeType nt = invert_relop(cond->type);
                CError_ASSERT(5443, nt != cond->type);
                cond->type = nt;
            }

            if (TYPE_IS_SOFT_FLOAT(cond->data.diadic.right->rtype) ||
                TYPE_IS_SOFT_FLOAT(cond->data.diadic.left->rtype)) {
                SFPE_gen_condition(cond, output, 0);
            } else if (TYPE_IS_8BYTES(cond->data.diadic.right->rtype) ||
                       TYPE_IS_8BYTES(cond->data.diadic.left->rtype)) {
                I8_gen_condition(cond, output, FALSE);
            } else {
                gen_condition(cond, output);
            }

            break;
        default:
            CError_FATAL(5459);
    }
}

static ENodeType invert_relop(ENodeType nt) {
    switch (nt) {
        case ELESS:
            return EGREATEREQU;
        case EGREATER:
            return ELESSEQU;
        case ELESSEQU:
            return EGREATER;
        case EGREATEREQU:
            return ELESS;
        case EEQU:
            return ENOTEQU;
        case ENOTEQU:
            return EEQU;
        default:
            return nt;
    }
}

static int reverse_relop(int nt) {
    switch (nt) {
        case ELESS:
            return EGREATER;
        case EGREATER:
            return ELESS;
        case ELESSEQU:
            return EGREATEREQU;
        case EGREATEREQU:
            return ELESSEQU;
        default:
            return nt;
    }
}

void gen_condition(ENode* cond, Operand* output) {
    ENode* left;
    ENode* right;

    left = cond->data.diadic.left;
    right = cond->data.diadic.right;
    if (copts.soft_float) {
        Type* type = left->rtype;
        if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
            CError_FATAL(5536);
        }
    }

    if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(left->rtype)) {
        compare_floating(cond->type, left, right, output);
    } else {
        if (ENODE_IS(right, EINTCONST)) {
            if (is_unsigned(left->rtype)) {
                UInt32 val = right->data.intval.lo;
                if (FITS_IN_USHORT(val)) {
                    compare_immediate(cond->type, left, val, output);
                    return;
                } else if (ENODE_IS2(cond, EEQU, ENOTEQU)) {
                    compare_immediate_long(cond->type, left, val, output);
                    return;
                }
            } else {
                UInt32 val = right->data.intval.lo;
                if (FITS_IN_SHORT(val)) {
                    compare_immediate(cond->type, left, val, output);
                    return;
                } else if (ENODE_IS2(cond, EEQU, ENOTEQU)) {
                    compare_immediate_long(cond->type, left, val, output);
                    return;
                }
            }
        } else if (ENODE_IS(left, EINTCONST)) {
            if (is_unsigned(right->rtype)) {
                UInt32 val = left->data.intval.lo;
                if (FITS_IN_USHORT(val)) {
                    compare_immediate(reverse_relop(cond->type), right, val, output);
                    return;
                } else if (ENODE_IS2(cond, EEQU, ENOTEQU)) {
                    compare_immediate_long(reverse_relop(cond->type), right, val, output);
                    return;
                }
            } else {
                UInt32 val = left->data.intval.lo;
                if (FITS_IN_SHORT(val)) {
                    compare_immediate(reverse_relop(cond->type), right, val, output);
                    return;
                } else if (ENODE_IS2(cond, EEQU, ENOTEQU)) {
                    compare_immediate_long(reverse_relop(cond->type), right, val, output);
                    return;
                }
            }
        }

        compare_integer(cond->type, left, right, output);
    }
}

void gen_condition_gpr(ENode* cond, Operand* output, short outputReg) {
    ENode* left;
    ENode* right;
    Operand condOp;
    PCode* pc;
    SInt32 finalReg;
    int tmpReg;
    int tmpReg2;
    int tmpReg3;
    int tmpReg4;
    int invert;
    int bit;

    left = cond->data.diadic.left;
    right = cond->data.diadic.right;
    pc = NULL;
    memclrw(&condOp, sizeof(Operand));

    if (TYPE_IS_SOFT_FLOAT(left->rtype)) {
        CError_FATAL(5614);
    }

    if (!IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(left->rtype)) {
        Operand op1;
        Operand op2;
        Operand swapOp;
        memclrw(&op1, sizeof(Operand));
        memclrw(&op2, sizeof(Operand));
        memclrw(&swapOp, sizeof(Operand));

        if (right->hascall) {
            GEN_NODE(right, &op2);
            if (!IS_INT_CONST_ZERO(right)) {
                if (right->rtype->size < 4) {
                    extend32(&op2, right->rtype, 0);
                }
                ENSURE_GPR(&op2, right->rtype, 0);
            }

            GEN_NODE(left, &op1);
            if (left->rtype->size < 4) {
                extend32(&op1, left->rtype, 0);
            }
            ENSURE_GPR(&op1, left->rtype, 0);
            if (pclastblock->pcodeCount > 0 &&
                pclastblock->lastPCode->op == PC_RLWINM &&
                pclastblock->lastPCode->args[0].data.reg.reg == op1.reg) {
                pc = pclastblock->lastPCode;
            }
        } else {
            GEN_NODE(left, &op1);
            ENSURE_GPR(&op1, left->rtype, 0);
            if (left->rtype->size < 4) {
                extend32(&op1, left->rtype, 0);
            }
            if (pclastblock->pcodeCount > 0 &&
                pclastblock->lastPCode->op == PC_RLWINM &&
                pclastblock->lastPCode->args[0].data.reg.reg == op1.reg) {
                pc = pclastblock->lastPCode;
            }

            GEN_NODE(right, &op2);
            if (!IS_INT_CONST_ZERO(right)) {
                if (right->rtype->size < 4) {
                    extend32(&op2, right->rtype, 0);
                }
                ENSURE_GPR(&op2, right->rtype, 0);
            }
        }

        switch (cond->type) {
            case EEQU:
                if (copts.peephole && IS_INT_CONST(right) && pc) {
                    SInt32 sh = pc->args[2].data.imm.value;
                    SInt32 mb = pc->args[3].data.imm.value;
                    SInt32 me = pc->args[4].data.imm.value;
                    UInt32 value = right->data.intval.lo;
                    if (mb == me) {
                        finalReg = outputReg ? outputReg : ALLOC_GPR();

                        if (value != (value & RLWINM_MASK(mb, me))) {
                            emitpcode(PC_LI, finalReg, 0);
                        } else if (value != RLWINM_MASK(mb, me)) {
                            tmpReg = ALLOC_GPR();
                            emitpcode(
                                PC_RLWINM,
                                tmpReg,
                                pc->args[1].data.reg.reg,
                                (sh + mb + 1) & 31,
                                31,
                                31
                            );
                            emitpcode(PC_XORI, finalReg, tmpReg, 1);
                        } else if (value == RLWINM_MASK(mb, me)) {
                            emitpcode(
                                PC_RLWINM,
                                finalReg,
                                pc->args[1].data.reg.reg,
                                (sh + mb + 1) & 31,
                                31,
                                31
                            );
                        } else {
                            CError_FATAL(5695);
                        }
                        output->optype = OpndType_GPR;
                        output->reg = finalReg;
                        return;
                    }
                }

                if (IS_INT_CONST_ZERO(right)) {
                    tmpReg = ALLOC_GPR();
                    emitpcode(PC_CNTLZW, tmpReg, op1.reg, 0);
                    finalReg = outputReg ? outputReg : ALLOC_GPR();
                    emitpcode(PC_RLWINM, finalReg, tmpReg, 27, 5, 31);
                    output->optype = OpndType_GPR;
                    output->reg = finalReg;
                    return;
                } else {
                    tmpReg = ALLOC_GPR();
                    emitpcode(PC_SUBF, tmpReg, op1.reg, op2.reg);
                    tmpReg2 = ALLOC_GPR();
                    emitpcode(PC_CNTLZW, tmpReg2, tmpReg);
                    finalReg = outputReg ? outputReg : ALLOC_GPR();
                    emitpcode(PC_RLWINM, finalReg, tmpReg2, 27, 5, 31);
                    output->optype = OpndType_GPR;
                    output->reg = finalReg;
                    return;
                }

            case ENOTEQU:
                if (copts.peephole && IS_INT_CONST(right) && pc) {
                    SInt32 sh = pc->args[2].data.imm.value;
                    SInt32 mb = pc->args[3].data.imm.value;
                    SInt32 me = pc->args[4].data.imm.value;
                    UInt32 value = right->data.intval.lo;
                    if (mb == me) {
                        finalReg = outputReg ? outputReg : ALLOC_GPR();

                        if (value != (value & RLWINM_MASK(mb, me))) {
                            emitpcode(PC_LI, finalReg, 1);
                        } else if (value != RLWINM_MASK(mb, me)) {
                            emitpcode(
                                PC_RLWINM,
                                finalReg,
                                pc->args[1].data.reg.reg,
                                (sh + mb + 1) & 31,
                                31,
                                31
                            );
                        } else if (value == RLWINM_MASK(mb, me)) {
                            tmpReg = ALLOC_GPR();
                            emitpcode(
                                PC_RLWINM,
                                tmpReg,
                                pc->args[1].data.reg.reg,
                                (sh + mb + 1) & 31,
                                31,
                                31
                            );
                            emitpcode(PC_XORI, finalReg, tmpReg, 1);
                        } else {
                            CError_FATAL(5761);
                        }
                        output->optype = OpndType_GPR;
                        output->reg = finalReg;
                        return;
                    }
                }

                if (IS_INT_CONST_ZERO(right)) {
                    if (copts.optimizesize) {
                        tmpReg = ALLOC_GPR();
                        emitpcode(PC_ADDIC, tmpReg, op1.reg, -1);
                        finalReg = outputReg ? outputReg : ALLOC_GPR();
                        emitpcode(PC_SUBFE, finalReg, tmpReg, op1.reg);
                    } else {
                        tmpReg = ALLOC_GPR();
                        emitpcode(PC_NEG, tmpReg, op1.reg);
                        tmpReg2 = ALLOC_GPR();
                        emitpcode(PC_OR, tmpReg2, tmpReg, op1.reg);
                        finalReg = outputReg ? outputReg : ALLOC_GPR();
                        emitpcode(PC_RLWINM, finalReg, tmpReg2, 1, 31, 31);
                    }
                    output->optype = OpndType_GPR;
                    output->reg = finalReg;
                    return;
                }

                if (copts.optimizesize) {
                    tmpReg = ALLOC_GPR();
                    emitpcode(PC_SUBF, tmpReg, op1.reg, op2.reg);
                    tmpReg2 = ALLOC_GPR();
                    emitpcode(PC_ADDIC, tmpReg2, tmpReg, -1);
                    finalReg = outputReg ? outputReg : ALLOC_GPR();
                    emitpcode(PC_SUBFE, finalReg, tmpReg2, tmpReg);
                } else {
                    tmpReg = ALLOC_GPR();
                    emitpcode(PC_SUBF, tmpReg, op1.reg, op2.reg);
                    tmpReg2 = ALLOC_GPR();
                    emitpcode(PC_SUBF, tmpReg2, op2.reg, op1.reg);
                    tmpReg3 = ALLOC_GPR();
                    emitpcode(PC_OR, tmpReg3, tmpReg, tmpReg2);
                    finalReg = outputReg ? outputReg : ALLOC_GPR();
                    emitpcode(PC_RLWINM, finalReg, tmpReg3, 1, 31, 31);
                }
                output->optype = OpndType_GPR;
                output->reg = finalReg;
                return;

            case EGREATEREQU:
                if (IS_INT_CONST_ZERO(right)) {
                    finalReg = outputReg ? outputReg : ALLOC_GPR();
                    if (is_unsigned(left->rtype)) {
                        emitpcode(PC_LI, finalReg, 1);
                    } else {
                        tmpReg = ALLOC_GPR();
                        emitpcode(PC_RLWINM, tmpReg, op1.reg, 1, 31, 31);
                        emitpcode(PC_XORI, finalReg, tmpReg, 1);
                    }
                    output->optype = OpndType_GPR;
                    output->reg = finalReg;
                    return;
                }
                swapOp = op2;
                op2 = op1;
                op1 = swapOp;

            case ELESSEQU:
                if (is_unsigned(left->rtype)) {
                    finalReg = outputReg ? outputReg : ALLOC_GPR();
                    if (IS_INT_CONST_ZERO(right)) {
                        tmpReg = ALLOC_GPR();
                        emitpcode(PC_CNTLZW, tmpReg, op1.reg, 0);
                        emitpcode(PC_RLWINM, finalReg, tmpReg, 27, 5, 31);
                    } else if (copts.optimizesize) {
                        tmpReg = ALLOC_GPR();
                        emitpcode(PC_LI, tmpReg, -1);
                        tmpReg2 = ALLOC_GPR();
                        emitpcode(PC_SUBFC, tmpReg2, op1.reg, op2.reg);
                        emitpcode(PC_SUBFZE, finalReg, tmpReg);
                    } else {
                        tmpReg = ALLOC_GPR();
                        emitpcode(PC_SUBF, tmpReg, op1.reg, op2.reg);
                        tmpReg2 = ALLOC_GPR();
                        emitpcode(PC_ORC, tmpReg2, op2.reg, op1.reg);
                        tmpReg3 = ALLOC_GPR();
                        emitpcode(PC_RLWINM, tmpReg3, tmpReg, 31, 1, 31);
                        tmpReg4 = ALLOC_GPR();
                        emitpcode(PC_SUBF, tmpReg4, tmpReg3, tmpReg2);
                        emitpcode(PC_RLWINM, finalReg, tmpReg4, 1, 31, 31);
                    }
                    output->optype = OpndType_GPR;
                    output->reg = finalReg;
                    return;
                }

                if (IS_INT_CONST_ZERO(right)) {
                    tmpReg = ALLOC_GPR();
                    emitpcode(PC_LI, tmpReg, 1);
                    tmpReg2 = ALLOC_GPR();
                    emitpcode(PC_CNTLZW, tmpReg2, op1.reg);
                    finalReg = outputReg ? outputReg : ALLOC_GPR();
                    emitpcode(PC_RLWNM, finalReg, tmpReg, tmpReg2, 31, 31);
                    output->optype = OpndType_GPR;
                    output->reg = finalReg;
                    return;
                }

                tmpReg2 = ALLOC_GPR();
                emitpcode(PC_SRAWI, tmpReg2, op2.reg, 31);
                tmpReg = ALLOC_GPR();
                emitpcode(PC_RLWINM, tmpReg, op1.reg, 1, 31, 31);
                tmpReg3 = ALLOC_GPR();
                emitpcode(PC_SUBFC, tmpReg3, op1.reg, op2.reg);
                finalReg = outputReg ? outputReg : ALLOC_GPR();
                emitpcode(PC_ADDE, finalReg, tmpReg2, tmpReg);
                output->optype = OpndType_GPR;
                output->reg = finalReg;
                return;

            case EGREATER:
                if (IS_INT_CONST_ZERO(right)) {
                    finalReg = outputReg ? outputReg : ALLOC_GPR();
                    if (is_unsigned(left->rtype)) {
                        if (copts.optimizesize) {
                            tmpReg = ALLOC_GPR();
                            emitpcode(PC_ADDIC, tmpReg, op1.reg, -1);
                            emitpcode(PC_SUBFE, finalReg, tmpReg, op1.reg);
                        } else {
                            tmpReg = ALLOC_GPR();
                            emitpcode(PC_NEG, tmpReg, op1.reg);
                            tmpReg2 = ALLOC_GPR();
                            emitpcode(PC_OR, tmpReg2, tmpReg, op1.reg);
                            emitpcode(PC_RLWINM, finalReg, tmpReg2, 1, 31, 31);
                        }
                    } else {
                        tmpReg = ALLOC_GPR();
                        emitpcode(PC_NEG, tmpReg, op1.reg);
                        tmpReg2 = ALLOC_GPR();
                        emitpcode(PC_ANDC, tmpReg2, tmpReg, op1.reg);
                        emitpcode(PC_RLWINM, finalReg, tmpReg2, 1, 31, 31);
                    }
                    output->optype = OpndType_GPR;
                    output->reg = finalReg;
                    return;
                }

                swapOp = op2;
                op2 = op1;
                op1 = swapOp;

            case ELESS:
                if (is_unsigned(left->rtype)) {
                    finalReg = outputReg ? outputReg : ALLOC_GPR();
                    if (IS_INT_CONST_ZERO(right)) {
                        emitpcode(PC_LI, finalReg, 0);
                    } else if (left->rtype->size <= 2) {
                        tmpReg = ALLOC_GPR();
                        emitpcode(PC_SUBF, tmpReg, op2.reg, op1.reg);
                        emitpcode(PC_RLWINM, finalReg, tmpReg, 1, 31, 31);
                        output->optype = OpndType_GPR;
                        output->reg = finalReg;
                    } else if (copts.optimizesize) {
                        tmpReg = ALLOC_GPR();
                        emitpcode(PC_SUBFC, tmpReg, op2.reg, op1.reg);
                        tmpReg2 = ALLOC_GPR();
                        emitpcode(PC_SUBFE, tmpReg2, tmpReg, tmpReg);
                        emitpcode(PC_NEG, finalReg, tmpReg2);
                    } else {
                        tmpReg = ALLOC_GPR();
                        emitpcode(PC_XOR, tmpReg, op2.reg, op1.reg);
                        tmpReg2 = ALLOC_GPR();
                        emitpcode(PC_CNTLZW, tmpReg2, tmpReg);
                        tmpReg3 = ALLOC_GPR();
                        emitpcode(PC_SLW, tmpReg3, op2.reg, tmpReg2);
                        emitpcode(PC_RLWINM, finalReg, tmpReg3, 1, 31, 31);
                    }
                    output->optype = OpndType_GPR;
                    output->reg = finalReg;
                    return;
                }

                if (IS_INT_CONST_ZERO(right)) {
                    finalReg = outputReg ? outputReg : ALLOC_GPR();
                    emitpcode(PC_RLWINM, finalReg, op1.reg, 1, 31, 31);
                    output->optype = OpndType_GPR;
                    output->reg = finalReg;
                    return;
                }

                tmpReg = ALLOC_GPR();
                emitpcode(PC_XOR, tmpReg, op2.reg, op1.reg);
                tmpReg2 = ALLOC_GPR();
                emitpcode(PC_SRAWI, tmpReg2, tmpReg, 1);
                tmpReg3 = ALLOC_GPR();
                emitpcode(PC_AND, tmpReg3, tmpReg, op2.reg);
                tmpReg4 = ALLOC_GPR();
                emitpcode(PC_SUBF, tmpReg4, tmpReg3, tmpReg2);

                finalReg = outputReg ? outputReg : ALLOC_GPR();
                emitpcode(PC_RLWINM, finalReg, tmpReg4, 1, 31, 31);
                output->optype = OpndType_GPR;
                output->reg = finalReg;
                return;

            default:
                CError_FATAL(6077);
        }
    }

    gen_condition(cond, &condOp);
    emitpcode(PC_MFCR, tmpReg = ALLOC_GPR());
    invert = 0;
    bit = condOp.reg * 4;
    switch (condOp.regOffset) {
        case ENOTEQU:
            invert = 1;
        case EEQU:
            bit += 2;
            break;
        case EGREATEREQU:
            invert = 1;
            break;
        case ELESSEQU:
            invert = 1;
        case EGREATER:
            bit += 1;
            break;
        case ELESS:
            break;
    }

    finalReg = outputReg ? outputReg : ALLOC_GPR();
    if (invert) {
        emitpcode(PC_RLWINM, tmpReg, tmpReg, bit + 1, 31, 31);
        emitpcode(PC_XORI, finalReg, tmpReg, 1);
    } else {
        emitpcode(PC_RLWINM, finalReg, tmpReg, bit + 1, 31, 31);
    }
    output->optype = OpndType_GPR;
    output->reg = finalReg;
}

void gen_negated_condition_gpr(ENode* cond, Operand* output, short outputReg) {
    ENode* left;
    ENode* right;
    Operand op1;
    Operand op2;
    Operand swapOp;
    PCode* pc;
    SInt32 finalReg;
    int tmpReg;
    int tmpReg2;
    int tmpReg3;

    left = cond->data.diadic.left;
    right = cond->data.diadic.right;
    pc = NULL;
    CError_ASSERT(6144, TYPE_FITS_IN_REGISTER(left->rtype) && TYPE_FITS_IN_REGISTER(right->rtype));

    memclrw(&op1, sizeof(Operand));
    memclrw(&op2, sizeof(Operand));
    memclrw(&swapOp, sizeof(Operand));

    if (right->hascall) {
        GEN_NODE(right, &op2);
        if (!IS_INT_CONST_ZERO(right)) {
            if (right->rtype->size < 4) {
                extend32(&op2, right->rtype, 0);
            }
            ENSURE_GPR(&op2, right->rtype, 0);
        }

        GEN_NODE(left, &op1);
        if (left->rtype->size < 4) {
            extend32(&op1, left->rtype, 0);
        }
        ENSURE_GPR(&op1, left->rtype, 0);
        if (pclastblock->pcodeCount > 0 &&
            pclastblock->lastPCode->op == PC_RLWINM &&
            pclastblock->lastPCode->args[0].data.reg.reg == op1.reg) {
            pc = pclastblock->lastPCode;
        }
    } else {
        GEN_NODE(left, &op1);
        ENSURE_GPR(&op1, left->rtype, 0);
        if (left->rtype->size < 4) {
            extend32(&op1, left->rtype, 0);
        }
        if (pclastblock->pcodeCount > 0 &&
            pclastblock->lastPCode->op == PC_RLWINM &&
            pclastblock->lastPCode->args[0].data.reg.reg == op1.reg) {
            pc = pclastblock->lastPCode;
        }

        GEN_NODE(right, &op2);
        if (!IS_INT_CONST_ZERO(right)) {
            if (right->rtype->size < 4) {
                extend32(&op2, right->rtype, 0);
            }
            ENSURE_GPR(&op2, right->rtype, 0);
        }
    }

    switch (cond->type) {
        case EEQU:
            if (copts.peephole && IS_INT_CONST(right) && pc) {
                SInt32 sh = pc->args[2].data.imm.value;
                SInt32 mb = pc->args[3].data.imm.value;
                SInt32 me = pc->args[4].data.imm.value;
                UInt32 value = right->data.intval.lo;
                if (mb == me) {
                    finalReg = outputReg ? outputReg : ALLOC_GPR();

                    if (value != (value & RLWINM_MASK(mb, me))) {
                        emitpcode(PC_LI, finalReg, 0);
                        output->optype = OpndType_GPR;
                        output->reg = finalReg;
                        return;
                    }

                    if (value != RLWINM_MASK(mb, me)) {
                        tmpReg = ALLOC_GPR();
                        emitpcode(
                            PC_RLWINM, tmpReg, pc->args[1].data.reg.reg, (sh + mb + 1) & 31, 31, 31
                        );
                        emitpcode(PC_ADDI, finalReg, tmpReg, 0, -1);
                        output->optype = OpndType_GPR;
                        output->reg = finalReg;
                        return;
                    }

                    if (value == RLWINM_MASK(mb, me)) {
                        tmpReg = ALLOC_GPR();
                        emitpcode(
                            PC_RLWINM, tmpReg, pc->args[1].data.reg.reg, (sh + mb + 1) & 31, 31, 31
                        );
                        emitpcode(PC_NEG, finalReg, tmpReg);
                        output->optype = OpndType_GPR;
#if VERSION < VERSION_GC_2_5
                        output->reg = tmpReg;
#else
                        output->reg = finalReg;
#endif
                        return;
                    }

                    CError_FATAL(6231);
                }
            }

            if (IS_INT_CONST_ZERO(right)) {
                if (copts.optimizesize) {
                    tmpReg = ALLOC_GPR();
                    emitpcode(PC_ADDIC, tmpReg, op1.reg, -1);
                    finalReg = outputReg ? outputReg : ALLOC_GPR();
                    emitpcode(PC_SUBFE, finalReg, tmpReg, tmpReg);
                } else {
                    tmpReg = ALLOC_GPR();
                    emitpcode(PC_CNTLZW, tmpReg, op1.reg);
                    tmpReg2 = ALLOC_GPR();
                    emitpcode(PC_RLWINM, tmpReg2, tmpReg, 27, 31, 31);
                    finalReg = outputReg ? outputReg : ALLOC_GPR();
                    emitpcode(PC_NEG, finalReg, tmpReg2);
                }
                output->optype = OpndType_GPR;
                output->reg = finalReg;
                return;
            }

            if (copts.optimizesize) {
                tmpReg = ALLOC_GPR();
                emitpcode(PC_SUBF, tmpReg, op2.reg, op1.reg);
                tmpReg2 = ALLOC_GPR();
                emitpcode(PC_ADDIC, tmpReg2, tmpReg, -1);
                finalReg = outputReg ? outputReg : ALLOC_GPR();
                emitpcode(PC_SUBFE, finalReg, tmpReg2, tmpReg2);
            } else {
                tmpReg = ALLOC_GPR();
                emitpcode(PC_SUBF, tmpReg, op2.reg, op1.reg);
                tmpReg2 = ALLOC_GPR();
                emitpcode(PC_SUBF, tmpReg2, op1.reg, op2.reg);
                tmpReg3 = ALLOC_GPR();
                emitpcode(PC_NOR, tmpReg3, tmpReg, tmpReg2);
                finalReg = outputReg ? outputReg : ALLOC_GPR();
                emitpcode(PC_SRAWI, finalReg, tmpReg3, 31);
            }
            output->optype = OpndType_GPR;
            output->reg = finalReg;
            return;

        case ENOTEQU:
            if (copts.peephole && IS_INT_CONST(right) && pc) {
                SInt32 sh = pc->args[2].data.imm.value;
                SInt32 mb = pc->args[3].data.imm.value;
                SInt32 me = pc->args[4].data.imm.value;
                UInt32 value = right->data.intval.lo;
                if (mb == me) {
                    finalReg = outputReg ? outputReg : ALLOC_GPR();

                    if (value != (value & RLWINM_MASK(mb, me))) {
                        emitpcode(PC_LI, finalReg, -1);
                        output->optype = OpndType_GPR;
                        output->reg = finalReg;
                        return;
                    }

                    if (value != RLWINM_MASK(mb, me)) {
                        tmpReg = ALLOC_GPR();
                        emitpcode(
                            PC_RLWINM, tmpReg, pc->args[1].data.reg.reg, (sh + mb + 1) & 31, 31, 31
                        );
                        emitpcode(PC_NEG, finalReg, tmpReg);
                        output->optype = OpndType_GPR;
                        output->reg = finalReg;
                        return;
                    }

                    if (value == RLWINM_MASK(mb, me)) {
                        tmpReg = ALLOC_GPR();
                        emitpcode(
                            PC_RLWINM, tmpReg, pc->args[1].data.reg.reg, (sh + mb + 1) & 31, 31, 31
                        );
                        emitpcode(PC_ADDI, finalReg, tmpReg, 0, -1);
                        output->optype = OpndType_GPR;
                        output->reg = finalReg;
                        return;
                    }

                    CError_FATAL(6336);
                }
            }

            if (IS_INT_CONST_ZERO(right)) {
                finalReg = outputReg ? outputReg : ALLOC_GPR();
                if (copts.optimizesize) {
                    tmpReg = ALLOC_GPR();
                    emitpcode(PC_SUBFIC, tmpReg, op1.reg, 0);
                    emitpcode(PC_SUBFE, finalReg, tmpReg, tmpReg);
                } else {
                    tmpReg = ALLOC_GPR();
                    emitpcode(PC_NEG, tmpReg, op1.reg);
                    tmpReg2 = ALLOC_GPR();
                    emitpcode(PC_OR, tmpReg2, tmpReg, op1.reg);
                    emitpcode(PC_SRAWI, finalReg, tmpReg2, 31);
                }
                output->optype = OpndType_GPR;
                output->reg = finalReg;
                return;
            }

            if (copts.optimizesize) {
                tmpReg = ALLOC_GPR();
                emitpcode(PC_SUBF, tmpReg, op2.reg, op1.reg);
                tmpReg2 = ALLOC_GPR();
                emitpcode(PC_SUBFIC, tmpReg2, tmpReg, 0);
                finalReg = outputReg ? outputReg : ALLOC_GPR();
                emitpcode(PC_SUBFE, finalReg, tmpReg2, tmpReg2);
            } else {
                tmpReg = ALLOC_GPR();
                emitpcode(PC_SUBF, tmpReg, op2.reg, op1.reg);
                tmpReg2 = ALLOC_GPR();
                emitpcode(PC_SUBF, tmpReg2, op1.reg, op2.reg);
                tmpReg3 = ALLOC_GPR();
                emitpcode(PC_OR, tmpReg3, tmpReg, tmpReg2);
                finalReg = outputReg ? outputReg : ALLOC_GPR();
                emitpcode(PC_SRAWI, finalReg, tmpReg3, 31);
            }
            output->optype = OpndType_GPR;
            output->reg = finalReg;
            return;

        case EGREATEREQU:
            if (IS_INT_CONST_ZERO(right)) {
                finalReg = outputReg ? outputReg : ALLOC_GPR();
                if (is_unsigned(left->rtype)) {
                    emitpcode(PC_LI, finalReg, -1);
                } else {
                    tmpReg = ALLOC_GPR();
                    emitpcode(PC_RLWINM, tmpReg, op1.reg, 1, 31, 31);
                    emitpcode(PC_ADDI, finalReg, tmpReg, 0, -1);
                }
                output->optype = OpndType_GPR;
                output->reg = finalReg;
                return;
            }
            swapOp = op2;
            op2 = op1;
            op1 = swapOp;

        case ELESSEQU:
            if (is_unsigned(left->rtype)) {
                finalReg = outputReg ? outputReg : ALLOC_GPR();
                if (IS_INT_CONST_ZERO(right)) {
                    if (copts.optimizesize) {
                        tmpReg = ALLOC_GPR();
                        emitpcode(PC_ADDIC, tmpReg, op1.reg, -1);
                        emitpcode(PC_SUBFE, finalReg, tmpReg, tmpReg);
                    } else {
                        tmpReg = ALLOC_GPR();
                        emitpcode(PC_CNTLZW, tmpReg, op1.reg);
                        tmpReg2 = ALLOC_GPR();
                        emitpcode(PC_RLWINM, tmpReg2, tmpReg, 27, 31, 31);
                        emitpcode(PC_NEG, finalReg, tmpReg2);
                    }
                } else {
                    tmpReg = ALLOC_GPR();
                    emitpcode(PC_SUBFC, tmpReg, op1.reg, op2.reg);
                    tmpReg2 = ALLOC_GPR();
                    emitpcode(PC_ADDZE, tmpReg2, op1.reg);
                    emitpcode(PC_SUBF, finalReg, tmpReg2, op1.reg);
                }
                output->optype = OpndType_GPR;
                output->reg = finalReg;
                return;
            }

            if (IS_INT_CONST_ZERO(right)) {
                tmpReg = ALLOC_GPR();
                emitpcode(PC_NEG, tmpReg, op1.reg);
                tmpReg2 = ALLOC_GPR();
                emitpcode(PC_ORC, tmpReg2, op1.reg, tmpReg);
                finalReg = outputReg ? outputReg : ALLOC_GPR();
                emitpcode(PC_SRAWI, finalReg, tmpReg2, 31);
                output->optype = OpndType_GPR;
                output->reg = finalReg;
                return;
            }

            tmpReg = ALLOC_GPR();
            emitpcode(PC_XORIS, tmpReg, op1.reg, 0x8000);
            tmpReg2 = ALLOC_GPR();
            emitpcode(PC_SUBF, tmpReg2, op1.reg, op2.reg);
            tmpReg3 = ALLOC_GPR();
            emitpcode(PC_ADDC, tmpReg3, tmpReg2, tmpReg);
            finalReg = outputReg ? outputReg : ALLOC_GPR();
            emitpcode(PC_SUBFE, finalReg, tmpReg3, tmpReg3);
            output->optype = OpndType_GPR;
            output->reg = finalReg;
            return;

        case EGREATER:
            if (IS_INT_CONST_ZERO(right)) {
                finalReg = outputReg ? outputReg : ALLOC_GPR();
                if (is_unsigned(left->rtype)) {
                    if (copts.optimizesize) {
                        tmpReg = ALLOC_GPR();
                        emitpcode(PC_SUBFIC, tmpReg, op1.reg, 0);
                        emitpcode(PC_SUBFE, finalReg, tmpReg, tmpReg);
                    } else {
                        tmpReg = ALLOC_GPR();
                        emitpcode(PC_NEG, tmpReg, op1.reg);
                        tmpReg2 = ALLOC_GPR();
                        emitpcode(PC_OR, tmpReg2, tmpReg, op1.reg);
                        emitpcode(PC_SRAWI, finalReg, tmpReg2, 31);
                    }
                } else {
                    tmpReg = ALLOC_GPR();
                    emitpcode(PC_NEG, tmpReg, op1.reg);
                    tmpReg2 = ALLOC_GPR();
                    emitpcode(PC_ANDC, tmpReg2, tmpReg, op1.reg);
                    emitpcode(PC_SRAWI, finalReg, tmpReg2, 31);
                }
                output->optype = OpndType_GPR;
                output->reg = finalReg;
                return;
            }
            swapOp = op2;
            op2 = op1;
            op1 = swapOp;

        case ELESS:
            if (is_unsigned(left->rtype)) {
                finalReg = outputReg ? outputReg : ALLOC_GPR();
                if (IS_INT_CONST_ZERO(right)) {
                    emitpcode(PC_LI, finalReg, 0);
                } else {
                    tmpReg = ALLOC_GPR();
                    emitpcode(PC_SUBFC, tmpReg, op2.reg, op1.reg);
                    emitpcode(PC_SUBFE, finalReg, tmpReg, tmpReg);
                }
                output->optype = OpndType_GPR;
                output->reg = finalReg;
                return;
            }

            if (IS_INT_CONST_ZERO(right)) {
                finalReg = outputReg ? outputReg : ALLOC_GPR();
                emitpcode(PC_SRAWI, finalReg, op1.reg, 31);
                output->optype = OpndType_GPR;
                output->reg = finalReg;
                return;
            }

            tmpReg = ALLOC_GPR();
            emitpcode(PC_SUBFC, tmpReg, op2.reg, op1.reg);
            tmpReg2 = ALLOC_GPR();
            emitpcode(PC_RLWINM, tmpReg2, op2.reg, 1, 31, 31);
            tmpReg3 = ALLOC_GPR();
            emitpcode(PC_RLWINM, tmpReg3, op1.reg, 1, 31, 31);
            finalReg = outputReg ? outputReg : ALLOC_GPR();
            emitpcode(PC_SUBFE, finalReg, tmpReg3, tmpReg2);
            output->optype = OpndType_GPR;
            output->reg = finalReg;
            return;

        default:
            CError_FATAL(6600);
    }
}

void compare_floating(short nt, ENode* left, ENode* right, Operand* output) {
    Operand opleft;
    Operand opright;

    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));

    if (right->hascall) {
        GEN_NODE_TO_FPR(right, &opright, right->rtype, 0);
        GEN_NODE_TO_FPR(left, &opleft, left->rtype, 0);
    } else {
        GEN_NODE_TO_FPR(left, &opleft, left->rtype, 0);
        GEN_NODE_TO_FPR(right, &opright, right->rtype, 0);
    }

    emitpcode((nt == EEQU || nt == ENOTEQU) ? PC_FCMPU : PC_FCMPO, 0, opleft.reg, opright.reg);
    if (nt == ELESSEQU) {
        emitpcode(PC_CROR, 0, 2, 0, 0, 0, 2);
        nt = EEQU;
    } else if (nt == EGREATEREQU) {
        emitpcode(PC_CROR, 0, 2, 0, 1, 0, 2);
        nt = EEQU;
    }

    output->optype = OpndType_CRField;
    output->reg = 0;
    output->regOffset = nt;
}

void compare_integer(short nt, ENode* left, ENode* right, Operand* output) {
    Operand opleft;
    Operand opright;

    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));

    if (right->hascall) {
        GEN_NODE(right, &opright);
        if (right->rtype->size < 4) {
            extend32(&opright, right->rtype, 0);
        }
        ENSURE_GPR(&opright, right->rtype, 0);

        GEN_NODE(left, &opleft);
        if (left->rtype->size < 4) {
            extend32(&opleft, left->rtype, 0);
        }
        ENSURE_GPR(&opleft, left->rtype, 0);
    } else {
        GEN_NODE(left, &opleft);
        ENSURE_GPR(&opleft, left->rtype, 0);
        if (left->rtype->size < 4) {
            extend32(&opleft, left->rtype, 0);
        }

        GEN_NODE(right, &opright);
        if (right->rtype->size < 4) {
            extend32(&opright, right->rtype, 0);
        }
        ENSURE_GPR(&opright, right->rtype, 0);
    }

    emitpcode(is_unsigned(left->rtype) ? PC_CMPL : PC_CMP, 0, opleft.reg, opright.reg);

    output->optype = OpndType_CRField;
    output->reg = 0;
    output->regOffset = nt;
}

void compare_immediate(short nt, ENode* left, SInt32 value, Operand* output) {
    int postIncFlag;
    short postIncReg;
    Operand op;
    SInt32 postIncValue;

    memclrw(&op, sizeof(Operand));

    postIncFlag = ispostincrementopportunity(left, &op, &postIncValue);
    if (!postIncFlag) {
        GEN_NODE(left, &op);
        if (op.optype != OpndType_CRField) {
            if (left->rtype->size < 4) {
                extend32(&op, left->rtype, 0);
            } else {
                ENSURE_GPR(&op, left->rtype, 0);
            }
        }
    } else {
        postIncReg = op.reg;
        if (left->rtype->size < 4) {
            extend32(&op, left->rtype, 0);
        }
        ENSURE_GPR(&op, left->rtype, 0);
    }

    if (op.optype == OpndType_CRField) {
        if ((nt == EEQU && value == 1) ||
            (nt == ENOTEQU && value == 0) ||
            (nt == EGREATER && value == 0) ||
            (nt == EGREATEREQU && value == 1)) {
            *output = op;
            return;
        }

        if ((nt == EEQU && value == 0) ||
            (nt == ENOTEQU && value == 1) ||
            (nt == ELESS && value == 1) ||
            (nt == ELESSEQU && value == 0)) {
            *output = op;
            switch (op.regOffset) {
                case EEQU:
                    output->regOffset = ENOTEQU;
                    return;
                case ENOTEQU:
                    output->regOffset = EEQU;
                    return;
                case ELESS:
                    output->regOffset = EGREATEREQU;
                    return;
                case EGREATER:
                    output->regOffset = ELESSEQU;
                    return;
                case ELESSEQU:
                    output->regOffset = EGREATER;
                    return;
                case EGREATEREQU:
                    output->regOffset = ELESS;
                    return;
            }
        }

        ENSURE_GPR(&op, left->rtype, 0);
    }

    if (copts.peephole &&
        value == 0 &&
        pclastblock->pcodeCount > 0 &&
        pclastblock->lastPCode->op != PC_RLWINM &&
        (PCODE_FLAG_SET_F(pclastblock->lastPCode) &
         (fIsMove | fSideEffects | fCanSetRecordBit | fOpTypeGPR)) ==
            (fCanSetRecordBit | fOpTypeGPR) &&
        pclastblock->lastPCode->args[0].data.reg.reg == op.reg &&
        (!is_unsigned(left->rtype) || nt == EEQU || nt == ENOTEQU)) {
        pcsetrecordbit(pclastblock->lastPCode);
    } else {
        emitpcode(is_unsigned(left->rtype) ? PC_CMPLI : PC_CMPI, 0, op.reg, value);
    }

    if (postIncFlag) {
        add_register_immediate(postIncReg, postIncReg, postIncValue);
    }

    output->optype = OpndType_CRField;
    output->reg = 0;
    output->regOffset = nt;
}

void compare_immediate_long(short nt, ENode* left, SInt32 value, Operand* output) {
    int postIncFlag;
    short postIncReg;
    int outputReg;
    Operand op;
    SInt32 postIncValue;

    memclrw(&op, sizeof(Operand));

    postIncFlag = ispostincrementopportunity(left, &op, &postIncValue);
    if (!postIncFlag) {
        GEN_NODE(left, &op);
    } else {
        postIncReg = op.reg;
    }

    if (left->rtype->size < 4) {
        extend32(&op, left->rtype, 0);
    }
    ENSURE_GPR(&op, left->rtype, 0);

    outputReg = ALLOC_GPR();
    emitpcode(PC_ADDIS, outputReg, op.reg, 0, (SInt16)(~(value >> 16) + 1));
    emitpcode(PC_CMPLI, 0, outputReg, (UInt16)value);

    if (postIncFlag) {
        add_register_immediate(postIncReg, postIncReg, postIncValue);
    }

    output->optype = OpndType_CRField;
    output->reg = 0;
    output->regOffset = nt;
}

static int ismask(SInt32 value, short* first, short* last) {
    int start, end, bit;
    start = end = -1;
    bit = 31;
    do {
        if (value & 1) {
            if (start != -1) {
                return FALSE;
            }
            if (end == -1) {
                end = bit;
            }
        } else {
            if (end != -1 && start == -1) {
                start = bit + 1;
            }
        }
        --bit;
        value >>= 1;
    } while (bit >= 0);

    if (end == -1) {
        return FALSE;
    }
    if (start == -1) {
        start = 0;
    }
    *first = start;
    *last = end;
    return TRUE;
}

int ismaskconstant(SInt32 value, short* first, short* last) {
    short my_first;
    short my_last;
    if (ismask(value, first, last)) {
        return TRUE;
    }

    if (value && ismask(~value, &my_first, &my_last)) {
        *first = my_last + 1;
        *last = my_first - 1;
        return TRUE;
    } else {
        return FALSE;
    }
}

static void shift_and_mask(
    ENode* expr, short sh, short mb, short me, short outputReg, Operand* output
) {
    Operand op;
    SInt32 reg;

    memclrw(&op, sizeof(Operand));
    GEN_NODE(expr, &op);
    ENSURE_GPR(&op, expr->rtype, 0);

    reg = outputReg ? outputReg : ALLOC_GPR();
    emitpcode(PC_RLWINM, reg, op.reg, sh, mb, me);

    output->optype = OpndType_GPR;
    output->reg = reg;
}

int ispostincrementopportunity(ENode* expr, Operand* op, SInt32* value) {
    Type* type;
    int reg;

    type = expr->rtype;
    if (!ENODE_IS2(expr, EPOSTINC, EPOSTDEC)) {
        return FALSE;
    }
    if (!ENODE_IS(expr->data.monadic, EINDIRECT)) {
        return FALSE;
    }
    if (!ENODE_IS(expr->data.monadic->data.monadic, EOBJREF)) {
        return FALSE;
    }

    if (!(reg = OBJECT_REG(expr->data.monadic->data.monadic->data.objref))) {
        return FALSE;
    }

    if (IS_TYPE_POINTER(type)) {
        if (ENODE_IS(expr, EPOSTINC)) {
            *value = TPTR_TARGET(type)->size;
        } else {
            *value = -TPTR_TARGET(type)->size;
        }
    } else {
        if (ENODE_IS(expr, EPOSTINC)) {
            *value = 1;
        } else {
            *value = -1;
        }
    }

    op->optype = OpndType_GPR;
    op->reg = reg;
    return TRUE;
}

void add_register_immediate(short regA, short regB, SInt32 value) {
    if (!FITS_IN_SHORT(value)) {
        emitpcode(PC_ADDIS, regA, regB, 0, HIGH_PART(value));
        if (LOW_PART(value)) {
            emitpcode(PC_ADDI, regA, regA, 0, LOW_PART(value));
        }
    } else {
        emitpcode(PC_ADDI, regA, regB, 0, value);
    }
}

static int ispowerof2(SInt32 val) {
    int bit = getbit(val);
    return (bit > 0 && bit < 31) ? bit : 0;
}

void I8_gen_ADD(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    int is_signed_left;
    int is_signed_right;
    ENode* left;
    ENode* right;
    short reg;
    short regHi;
    short tmpreg1;
    short tmpreg2;
    SInt32 skipleft;
    SInt32 skipright;
    Operand opleft;
    Operand opright;
    Operand swapOp;
    SInt32 swapSkip;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));

    if (right->hascall) {
        GEN_NODE(right, &opright);
        if (TYPE_IS_8BYTES(right->rtype)) {
            coerce_to_register_pair(&opright, right->rtype, 0, 0);
        } else {
            ENSURE_GPR(&opright, right->rtype, 0);
        }
        GEN_NODE(left, &opleft);
        if (TYPE_IS_8BYTES(left->rtype)) {
            coerce_to_register_pair(&opleft, left->rtype, 0, 0);
        } else {
            ENSURE_GPR(&opleft, left->rtype, 0);
        }
    } else {
        GEN_NODE(left, &opleft);
        if (TYPE_IS_8BYTES(left->rtype)) {
            coerce_to_register_pair(&opleft, left->rtype, 0, 0);
        } else {
            ENSURE_GPR(&opleft, left->rtype, 0);
        }
        GEN_NODE(right, &opright);
        if (TYPE_IS_8BYTES(right->rtype)) {
            coerce_to_register_pair(&opright, right->rtype, 0, 0);
        } else {
            ENSURE_GPR(&opright, right->rtype, 0);
        }
    }

    reg = ALLOC_GPR();
    regHi = ALLOC_GPR();
    is_signed_left = !is_unsigned(left->rtype);
    is_signed_right = !is_unsigned(right->rtype);
    skipleft = GetSizeSkip(left);
    skipright = GetSizeSkip(right);

    if (skipleft < skipright) {
        expr->data.diadic.left = right;
        expr->data.diadic.right = left;
        left = expr->data.diadic.left;
        right = expr->data.diadic.right;

        swapOp = opright;
        opright = opleft;
        opleft = swapOp;

        swapSkip = skipleft;
        skipleft = skipright;
        skipright = swapSkip;
    }

    switch (skipleft + skipright) {
        case 1 + 1:
        case 1 + 2:
        case 2 + 2:
            if (ENODE_IS(left, EINTCONST) && FITS_IN_SHORT2(left->data.intval.lo)) {
                emitpcode(PC_ADDIC, reg, opright.reg, LOW_PART(left->data.intval.lo));
            } else if (ENODE_IS(right, EINTCONST) && FITS_IN_SHORT2(right->data.intval.lo)) {
                emitpcode(PC_ADDIC, reg, opleft.reg, LOW_PART(right->data.intval.lo));
            } else {
                emitpcode(PC_ADDC, reg, opleft.reg, opright.reg);
            }
            if (!is_signed_left && !is_signed_right) {
                emitpcode(PC_LI, regHi, 0);
            } else {
                emitpcode(PC_SRAWI, regHi, reg, 31);
            }
            break;
        case 1 + 4:
        case 2 + 4:
        case 4 + 4:
            if (is_signed_left) {
                tmpreg1 = ALLOC_GPR();
                emitpcode(PC_SRAWI, tmpreg1, opleft.reg, 31);
            }
            if (is_signed_right) {
                tmpreg2 = ALLOC_GPR();
                emitpcode(PC_SRAWI, tmpreg2, opright.reg, 31);
            }
            if (ENODE_IS(left, EINTCONST) && FITS_IN_SHORT2(left->data.intval.lo)) {
                emitpcode(PC_ADDIC, reg, opright.reg, LOW_PART(left->data.intval.lo));
            } else if (ENODE_IS(right, EINTCONST) && FITS_IN_SHORT2(right->data.intval.lo)) {
                emitpcode(PC_ADDIC, reg, opleft.reg, LOW_PART(right->data.intval.lo));
            } else {
                emitpcode(PC_ADDC, reg, opleft.reg, opright.reg);
            }
            if (is_signed_left) {
                if (is_signed_right) {
                    emitpcode(PC_ADDE, regHi, tmpreg1, tmpreg2);
                } else {
                    emitpcode(PC_ADDZE, regHi, tmpreg1);
                }
            } else if (is_signed_right) {
                emitpcode(PC_ADDZE, regHi, tmpreg2);
            } else {
                tmpreg1 = ALLOC_GPR();
                emitpcode(PC_LI, tmpreg1, 0);
                emitpcode(PC_ADDZE, regHi, tmpreg1);
            }
            break;
        case 1 + 8:
        case 2 + 8:
        case 4 + 8:
            CError_ASSERT(7328, skipleft == 8);
            if (is_signed_right) {
                tmpreg2 = ALLOC_GPR();
                emitpcode(PC_SRAWI, tmpreg2, opright.reg, 31);
            }
            if (ENODE_IS(left, EINTCONST) &&
                FITS_IN_SHORT2(left->data.intval.lo) &&
                left->data.intval.hi == 0) {
                emitpcode(PC_ADDIC, reg, opright.reg, LOW_PART(left->data.intval.lo));
            } else if (ENODE_IS(right, EINTCONST) &&
                       FITS_IN_SHORT2(right->data.intval.lo) &&
                       right->data.intval.hi == 0) {
                emitpcode(PC_ADDIC, reg, opleft.reg, LOW_PART(right->data.intval.lo));
            } else {
                emitpcode(PC_ADDC, reg, opleft.reg, opright.reg);
            }
            if (!is_signed_right) {
                emitpcode(PC_ADDZE, regHi, opleft.regHi);
            } else {
                emitpcode(PC_ADDE, regHi, opleft.regHi, tmpreg2);
            }
            break;
        case 8 + 8:
            emitpcode(PC_ADDC, reg, opleft.reg, opright.reg);
            emitpcode(PC_ADDE, regHi, opleft.regHi, opright.regHi);
            break;
        default:
            CError_FATAL(7374);
    }

    output->optype = OpndType_GPRPair;
    output->reg = reg;
    output->regHi = regHi;
}

void I8_gen_INTCONST(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    short reg;
    short regHi;

    reg = outputReg ? outputReg : ALLOC_GPR();
    regHi = outputRegHi ? outputRegHi : ALLOC_GPR();

    load_immediate(reg, expr->data.intval.lo);
    load_immediate(regHi, expr->data.intval.hi);

    output->optype = OpndType_GPRPair;
    output->reg = reg;
    output->regHi = regHi;
}

void I8_gen_SUB(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* left;
    ENode* right;
    short reg;
    short regHi;
    short tmpreg1;
    short tmpreg2;
    short tmpreg3;
    SInt32 skipleft;
    SInt32 skipright;
    Operand opleft;
    Operand opright;
    int is_signed_left;
    int is_signed_right;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));

    if (right->hascall) {
        GEN_NODE(right, &opright);
        if (TYPE_IS_8BYTES(right->rtype)) {
            coerce_to_register_pair(&opright, right->rtype, 0, 0);
        } else {
            ENSURE_GPR(&opright, right->rtype, 0);
        }
        GEN_NODE(left, &opleft);
        if (TYPE_IS_8BYTES(left->rtype)) {
            coerce_to_register_pair(&opleft, left->rtype, 0, 0);
        } else {
            ENSURE_GPR(&opleft, left->rtype, 0);
        }
    } else {
        GEN_NODE(left, &opleft);
        if (TYPE_IS_8BYTES(left->rtype)) {
            coerce_to_register_pair(&opleft, left->rtype, 0, 0);
        } else {
            ENSURE_GPR(&opleft, left->rtype, 0);
        }
        GEN_NODE(right, &opright);
        if (TYPE_IS_8BYTES(right->rtype)) {
            coerce_to_register_pair(&opright, right->rtype, 0, 0);
        } else {
            ENSURE_GPR(&opright, right->rtype, 0);
        }
    }

    reg = ALLOC_GPR();
    regHi = ALLOC_GPR();
    is_signed_left = !is_unsigned(left->rtype);
    is_signed_right = !is_unsigned(right->rtype);
    skipleft = GetSizeSkip(left);
    skipright = GetSizeSkip(right);

    switch (skipleft + skipright) {
        case 1 + 1:
        case 1 + 2:
        case 2 + 2:
            if (ENODE_IS(left, EINTCONST) && FITS_IN_SHORT2(left->data.intval.lo)) {
                emitpcode(PC_SUBFIC, reg, opright.reg, LOW_PART(left->data.intval.lo));
            } else {
                emitpcode(PC_SUBFC, reg, opright.reg, opleft.reg);
            }
            if (!is_signed_left && !is_signed_right) {
                emitpcode(PC_LI, regHi, 0);
            } else {
                emitpcode(PC_SRAWI, regHi, reg, 31);
            }
            break;
        case 1 + 4:
        case 2 + 4:
        case 4 + 4:
            if (is_signed_left) {
                tmpreg1 = ALLOC_GPR();
                emitpcode(PC_SRAWI, tmpreg1, opleft.reg, 31);
            }
            if (is_signed_right) {
                tmpreg2 = ALLOC_GPR();
                emitpcode(PC_SRAWI, tmpreg2, opright.reg, 31);
            }
            if (ENODE_IS(left, EINTCONST) && FITS_IN_SHORT2(left->data.intval.lo)) {
                emitpcode(PC_SUBFIC, reg, opright.reg, LOW_PART(left->data.intval.lo));
            } else {
                emitpcode(PC_SUBFC, reg, opright.reg, opleft.reg);
            }
            if (is_signed_left) {
                if (is_signed_right) {
                    emitpcode(PC_SUBFE, regHi, tmpreg2, tmpreg1);
                } else {
                    tmpreg2 = ALLOC_GPR();
                    emitpcode(PC_LI, tmpreg2, 0);
                    emitpcode(PC_SUBFE, regHi, tmpreg2, tmpreg1);
                }
            } else if (is_signed_right) {
                emitpcode(PC_SUBFZE, regHi, tmpreg2);
            } else {
                emitpcode(PC_SUBFE, regHi, opleft.reg, opleft.reg);
            }
            break;
        case 1 + 8:
        case 2 + 8:
        case 4 + 8:
            if (skipleft < skipright) {
                emitpcode(PC_SUBFC, reg, opright.reg, opleft.reg);
                emitpcode(PC_SUBFE, regHi, opright.regHi, opleft.regHi);
            } else {
                if (ENODE_IS(left, EINTCONST) &&
                    FITS_IN_SHORT2(left->data.intval.lo) &&
                    left->data.intval.hi == 0) {
                    emitpcode(PC_SUBFIC, reg, opright.reg, LOW_PART(left->data.intval.lo));
                } else {
                    emitpcode(PC_SUBFC, reg, opright.reg, opleft.reg);
                }
                tmpreg1 = ALLOC_GPR();
                emitpcode(PC_LI, tmpreg1, 0);
                emitpcode(PC_SUBFE, regHi, tmpreg1, opleft.regHi);
            }
            break;
        case 8 + 8:
            emitpcode(PC_SUBFC, reg, opright.reg, opleft.reg);
            emitpcode(PC_SUBFE, regHi, opright.regHi, opleft.regHi);
            break;
        default:
            CError_FATAL(7616);
    }

    output->optype = OpndType_GPRPair;
    output->reg = reg;
    output->regHi = regHi;
}

void I8_gen_XOR(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* left;
    ENode* right;
    short reg;
    short regHi;
    Operand opleft;
    Operand opright;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));

    if (right->hascall) {
        GEN_NODE(right, &opright);
        coerce_to_register_pair(&opright, right->rtype, 0, 0);

        GEN_NODE(left, &opleft);
        coerce_to_register_pair(&opleft, left->rtype, 0, 0);
    } else {
        GEN_NODE(left, &opleft);
        coerce_to_register_pair(&opleft, left->rtype, 0, 0);

        GEN_NODE(right, &opright);
        coerce_to_register_pair(&opright, right->rtype, 0, 0);
    }

    CError_ASSERT(7659, opleft.optype == OpndType_GPRPair && opright.optype == OpndType_GPRPair);

    reg = outputReg ? outputReg : ALLOC_GPR();
    regHi = outputRegHi ? outputRegHi : ALLOC_GPR();

    emitpcode(PC_XOR, reg, opleft.reg, opright.reg);
    emitpcode(PC_XOR, regHi, opleft.regHi, opright.regHi);

    output->optype = OpndType_GPRPair;
    output->reg = reg;
    output->regHi = regHi;
}

void I8_gen_OR(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* left;
    ENode* right;
    short reg;
    short regHi;
    Operand opleft;
    Operand opright;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));

    if (right->hascall) {
        GEN_NODE(right, &opright);
        coerce_to_register_pair(&opright, right->rtype, 0, 0);

        GEN_NODE(left, &opleft);
        coerce_to_register_pair(&opleft, left->rtype, 0, 0);
    } else {
        GEN_NODE(left, &opleft);
        coerce_to_register_pair(&opleft, left->rtype, 0, 0);

        GEN_NODE(right, &opright);
        coerce_to_register_pair(&opright, right->rtype, 0, 0);
    }

    CError_ASSERT(7709, opleft.optype == OpndType_GPRPair && opright.optype == OpndType_GPRPair);

    reg = outputReg ? outputReg : ALLOC_GPR();
    regHi = outputRegHi ? outputRegHi : ALLOC_GPR();

    emitpcode(PC_OR, reg, opleft.reg, opright.reg);
    emitpcode(PC_OR, regHi, opleft.regHi, opright.regHi);

    output->optype = OpndType_GPRPair;
    output->reg = reg;
    output->regHi = regHi;
}

void I8_gen_AND(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* left;
    ENode* right;
    short reg;
    short regHi;
    Operand opleft;
    Operand opright;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));

    if (right->hascall) {
        GEN_NODE(right, &opright);
        coerce_to_register_pair(&opright, right->rtype, 0, 0);

        GEN_NODE(left, &opleft);
        coerce_to_register_pair(&opleft, left->rtype, 0, 0);
    } else {
        GEN_NODE(left, &opleft);
        coerce_to_register_pair(&opleft, left->rtype, 0, 0);

        GEN_NODE(right, &opright);
        coerce_to_register_pair(&opright, right->rtype, 0, 0);
    }

    CError_ASSERT(7759, opleft.optype == OpndType_GPRPair && opright.optype == OpndType_GPRPair);

    reg = outputReg ? outputReg : ALLOC_GPR();
    regHi = outputRegHi ? outputRegHi : ALLOC_GPR();

    emitpcode(PC_AND, reg, opleft.reg, opright.reg);
    emitpcode(PC_AND, regHi, opleft.regHi, opright.regHi);

    output->optype = OpndType_GPRPair;
    output->reg = reg;
    output->regHi = regHi;
}

int I8_getbit(long long value) {
    switch (value) {
        case 0:
            return -1;
        case 0x1LL:
            return 0;
        case 0x2LL:
            return 1;
        case 0x4LL:
            return 2;
        case 0x8LL:
            return 3;
        case 0x10LL:
            return 4;
        case 0x20LL:
            return 5;
        case 0x40LL:
            return 6;
        case 0x80LL:
            return 7;
        case 0x100LL:
            return 8;
        case 0x200LL:
            return 9;
        case 0x400LL:
            return 10;
        case 0x800LL:
            return 11;
        case 0x1000LL:
            return 12;
        case 0x2000LL:
            return 13;
        case 0x4000LL:
            return 14;
        case 0x8000LL:
            return 15;
        case 0x10000LL:
            return 16;
        case 0x20000LL:
            return 17;
        case 0x40000LL:
            return 18;
        case 0x80000LL:
            return 19;
        case 0x100000LL:
            return 20;
        case 0x200000LL:
            return 21;
        case 0x400000LL:
            return 22;
        case 0x800000LL:
            return 23;
        case 0x1000000LL:
            return 24;
        case 0x2000000LL:
            return 25;
        case 0x4000000LL:
            return 26;
        case 0x8000000LL:
            return 27;
        case 0x10000000LL:
            return 28;
        case 0x20000000LL:
            return 29;
        case 0x40000000LL:
            return 30;
        case 0x80000000LL:
            return 31;
        case 0x100000000LL:
            return 32;
        case 0x200000000LL:
            return 33;
        case 0x400000000LL:
            return 34;
        case 0x800000000LL:
            return 35;
        case 0x1000000000LL:
            return 36;
        case 0x2000000000LL:
            return 37;
        case 0x4000000000LL:
            return 38;
        case 0x8000000000LL:
            return 39;
        case 0x10000000000LL:
            return 40;
        case 0x20000000000LL:
            return 41;
        case 0x40000000000LL:
            return 42;
        case 0x80000000000LL:
            return 43;
        case 0x100000000000LL:
            return 44;
        case 0x200000000000LL:
            return 45;
        case 0x400000000000LL:
            return 46;
        case 0x800000000000LL:
            return 47;
        case 0x1000000000000LL:
            return 48;
        case 0x2000000000000LL:
            return 49;
        case 0x4000000000000LL:
            return 50;
        case 0x8000000000000LL:
            return 51;
        case 0x10000000000000LL:
            return 52;
        case 0x20000000000000LL:
            return 53;
        case 0x40000000000000LL:
            return 54;
        case 0x80000000000000LL:
            return 55;
        case 0x100000000000000LL:
            return 56;
        case 0x200000000000000LL:
            return 57;
        case 0x400000000000000LL:
            return 58;
        case 0x800000000000000LL:
            return 59;
        case 0x1000000000000000LL:
            return 60;
        case 0x2000000000000000LL:
            return 61;
        case 0x4000000000000000LL:
            return 62;
    }
    if (value == 0x8000000000000000LL) {
        return 63;
    }
    return -2;
}

int I8_log2n(long long val) {
    int bit = I8_getbit(val);
    return (bit > 0 && bit < 63) ? bit : 0;
}

void I8_ShiftLeftImmediate(
    Operand opnd, SInt32 value, int is_unsigned, SInt32 size, short reg, short regHi
) {
    SInt32 shift;

    if (opnd.reg == reg || opnd.regHi == regHi || opnd.reg == regHi || opnd.regHi == reg) {
        CError_FATAL(8104);
    }

    shift = value & 63;
    if (shift == 0) {
        emitpcode(PC_MR, regHi, opnd.regHi);
        emitpcode(PC_MR, reg, opnd.reg);
    } else if (shift < 32) {
        emitpcode(PC_RLWINM, reg, opnd.reg, shift, 0, 31 - shift);
        if (size > 4) {
            emitpcode(PC_RLWINM, regHi, opnd.regHi, shift, 0, 31 - shift);
            emitpcode(PC_RLWIMI, regHi, opnd.reg, shift, 32 - shift, 31);
        } else if (is_unsigned) {
            emitpcode(PC_RLWINM, regHi, opnd.reg, shift, 32 - shift, 31);
        } else {
            emitpcode(PC_SRAWI, regHi, opnd.reg, shift, 32 - shift, 31);
        }
    } else if (shift <= 63) {
        if (shift == 32) {
            emitpcode(PC_MR, regHi, opnd.reg);
        } else {
            emitpcode(PC_RLWINM, regHi, opnd.reg, shift - 32, 0, 63 - shift);
        }
        emitpcode(PC_LI, reg, 0);
    } else {
        CError_FATAL(8143);
    }
}

#pragma sym off
void I8_ShiftRightImmediate(
    Operand opnd, SInt32 value, int is_unsigned, short reg, short regHi, int for_division
) {
    SInt32 m1;
    short tmpreg1;
    short tmpreg2;
    short tmpreg3;
    short tmpreg4;
    SInt32 shift;

    if (opnd.reg == reg || opnd.regHi == regHi || opnd.reg == regHi || opnd.regHi == reg) {
        CError_FATAL(8167);
    }

    shift = value;
    shift &= 63;
    if (shift == 0) {
        emitpcode(PC_MR, regHi, opnd.regHi);
        emitpcode(PC_MR, reg, opnd.reg);
    } else if (shift < 32) {
        emitpcode(PC_RLWINM, reg, opnd.reg, 32 - shift, 0, 31);
        m1 = shift - 1;
        emitpcode(PC_RLWIMI, reg, opnd.regHi, 32 - shift, 0, m1);
        if (is_unsigned) {
            emitpcode(PC_RLWINM, regHi, opnd.regHi, 32 - shift, shift, 31);
        } else if (for_division) {
            tmpreg1 = ALLOC_GPR();
            emitpcode(PC_MR, tmpreg1, opnd.regHi);
            emitpcode(PC_RLWIMI, tmpreg1, opnd.reg, 0, 31 - m1, 31);
            emitpcode(PC_SRAWI, regHi, tmpreg1, shift);
        } else {
            emitpcode(PC_SRAWI, regHi, opnd.regHi, shift);
        }
    } else if (shift == 32) {
        if (is_unsigned) {
            emitpcode(PC_MR, reg, opnd.regHi);
            emitpcode(PC_LI, regHi, 0);
        } else if (for_division) {
            tmpreg1 = ALLOC_GPR();
            tmpreg2 = ALLOC_GPR();
            tmpreg3 = ALLOC_GPR();
            emitpcode(PC_NEG, tmpreg1, opnd.reg);
            emitpcode(PC_OR, tmpreg2, tmpreg1, opnd.reg);
            emitpcode(PC_RLWINM, tmpreg3, tmpreg2, 1, 31, 31);
            emitpcode(PC_RLWIMI, tmpreg3, opnd.regHi, 0, 0, 0);
            emitpcode(PC_MR, reg, opnd.regHi);
            emitpcode(PC_SRAWI, regHi, tmpreg3, 31);
        } else {
            emitpcode(PC_MR, reg, opnd.regHi);
            emitpcode(PC_SRAWI, regHi, opnd.regHi, 31);
        }
    } else if (shift <= 63) {
        if (is_unsigned) {
            emitpcode(PC_RLWINM, reg, opnd.regHi, 64 - shift, shift - 32, 31);
            emitpcode(PC_LI, regHi, 0);
        } else if (for_division) {
            tmpreg1 = ALLOC_GPR();
            tmpreg2 = ALLOC_GPR();
            tmpreg3 = ALLOC_GPR();
            tmpreg4 = ALLOC_GPR();
            emitpcode(PC_NEG, tmpreg1, opnd.reg);
            emitpcode(PC_OR, tmpreg2, tmpreg1, opnd.reg);
            emitpcode(PC_RLWINM, tmpreg3, tmpreg2, 1, 31, 31);
            emitpcode(PC_OR, tmpreg4, opnd.regHi, tmpreg3);
            emitpcode(PC_SRAWI, regHi, opnd.regHi, 31);
            emitpcode(PC_SRAWI, reg, tmpreg4, shift - 32);
        } else {
            emitpcode(PC_SRAWI, reg, opnd.regHi, shift - 32);
            emitpcode(PC_SRAWI, regHi, opnd.regHi, 31);
        }
    } else {
        CError_FATAL(8285);
    }
}

#pragma sym reset
void I8_gen_MUL(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    int is_signed_left;
    int is_signed_right;
    ENode* left;
    ENode* right;
    short reg;
    short regHi;
    short tmpreg1;
    short tmpreg2;
    short tmpreg3;
    short tmpreg4;
    SInt32 skipleft;
    SInt32 skipright;
    Operand opleft;
    Operand opright;
    Operand swapOp;
    long long leftval;
    long long rightval;
    long long swapValue;
    int shift;
    SInt32 swapSkip;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));
    skipleft = GetSizeSkip(left);
    skipright = GetSizeSkip(right);

    if (ENODE_IS(right, EINTCONST) && ENODE_IS(left, EINTCONST)) {
        CError_FATAL(8319);
    }

    if (ENODE_IS(left, EINTCONST)) {
        leftval =
            left->data.intval.lo + (((long long)((skipleft < 8) ? 0 : left->data.intval.hi)) << 32);
    }
    if (ENODE_IS(right, EINTCONST)) {
        rightval = right->data.intval.lo +
            (((long long)((skipright < 8) ? 0 : right->data.intval.hi)) << 32);
    }

    if (right->hascall) {
        GEN_NODE(right, &opright);
        if (TYPE_IS_8BYTES(right->rtype)) {
            coerce_to_register_pair(&opright, right->rtype, 0, 0);
        } else {
            ENSURE_GPR(&opright, right->rtype, 0);
        }

        if (!(ENODE_IS(left, EINTCONST) && I8_log2n(leftval) > 0)) {
            GEN_NODE(left, &opleft);
            if (TYPE_IS_8BYTES(left->rtype)) {
                coerce_to_register_pair(&opleft, left->rtype, 0, 0);
            } else {
                ENSURE_GPR(&opleft, left->rtype, 0);
            }
        }
    } else {
        if (!(ENODE_IS(left, EINTCONST) && I8_log2n(leftval) > 0)) {
            GEN_NODE(left, &opleft);
            if (TYPE_IS_8BYTES(left->rtype)) {
                coerce_to_register_pair(&opleft, left->rtype, 0, 0);
            } else {
                ENSURE_GPR(&opleft, left->rtype, 0);
            }
        }

        if (!(ENODE_IS(right, EINTCONST) && I8_log2n(rightval) > 0)) {
            GEN_NODE(right, &opright);
            if (TYPE_IS_8BYTES(right->rtype)) {
                coerce_to_register_pair(&opright, right->rtype, 0, 0);
            } else {
                ENSURE_GPR(&opright, right->rtype, 0);
            }
        }
    }

    is_signed_left = !is_unsigned(left->rtype);
    is_signed_right = !is_unsigned(right->rtype);

    if (skipleft < skipright) {
        expr->data.diadic.left = right;
        expr->data.diadic.right = left;
        left = expr->data.diadic.left;
        right = expr->data.diadic.right;

        swapOp = opright;
        opright = opleft;
        opleft = swapOp;

        swapValue = leftval;
        leftval = rightval;
        rightval = swapValue;

        swapSkip = skipleft;
        skipleft = skipright;
        skipright = swapSkip;

        shift = is_signed_left;
        is_signed_left = is_signed_right;
        is_signed_right = shift;
    }

    reg = ALLOC_GPR();
    regHi = ALLOC_GPR();

    if (ENODE_IS(left, EINTCONST) && (shift = I8_log2n(leftval)) > 0) {
        I8_ShiftLeftImmediate(opright, shift, !is_signed_right, skipright, reg, regHi);
    } else if (ENODE_IS(right, EINTCONST) && (shift = I8_log2n(rightval)) > 0) {
        I8_ShiftLeftImmediate(opleft, shift, !is_signed_left, skipleft, reg, regHi);
    } else {
        switch (skipleft + skipright) {
            case 1 + 1:
            case 1 + 2:
            case 2 + 2:
                if (ENODE_IS(left, EINTCONST) && FITS_IN_SHORT2(left->data.intval.lo)) {
                    emitpcode(PC_MULLI, reg, opright.reg, LOW_PART(left->data.intval.lo));
                } else if (ENODE_IS(right, EINTCONST) && FITS_IN_SHORT2(right->data.intval.lo)) {
                    emitpcode(PC_MULLI, reg, opleft.reg, LOW_PART(right->data.intval.lo));
                } else {
                    emitpcode(PC_MULLW, reg, opleft.reg, opright.reg);
                }
                if (!is_signed_right && !is_signed_left) {
                    emitpcode(PC_LI, regHi, 0);
                } else {
                    emitpcode(PC_SRAWI, regHi, reg, 31);
                }
                break;
            case 1 + 4:
            case 2 + 4:
            case 4 + 4:
                if (ENODE_IS(left, EINTCONST) && FITS_IN_SHORT2(left->data.intval.lo)) {
                    emitpcode(PC_MULLI, reg, opright.reg, LOW_PART(left->data.intval.lo));
                } else if (ENODE_IS(right, EINTCONST) && FITS_IN_SHORT2(right->data.intval.lo)) {
                    emitpcode(PC_MULLI, reg, opleft.reg, LOW_PART(right->data.intval.lo));
                } else {
                    emitpcode(PC_MULLW, reg, opleft.reg, opright.reg);
                }
                if (!is_signed_right && !is_signed_left) {
                    emitpcode(PC_MULHWU, regHi, opleft.reg, opright.reg);
                } else {
                    emitpcode(PC_MULHW, regHi, opleft.reg, opright.reg);
                }
                break;
            case 1 + 8:
            case 2 + 8:
            case 4 + 8:
                CError_ASSERT(8521, skipleft == 8);
                if (ENODE_IS(left, EINTCONST) &&
                    FITS_IN_SHORT2(left->data.intval.lo) &&
                    left->data.intval.hi == 0) {
                    emitpcode(PC_MULLI, reg, opright.reg, LOW_PART(left->data.intval.lo));
                    if (!is_signed_right && !is_signed_left) {
                        emitpcode(PC_MULHWU, regHi, opright.reg, opleft.reg);
                    } else {
                        emitpcode(PC_MULHW, regHi, opright.reg, opleft.reg);
                    }
                } else if (ENODE_IS(right, EINTCONST) &&
                           FITS_IN_SHORT2(right->data.intval.lo) &&
                           right->data.intval.hi == 0) {
                    tmpreg1 = ALLOC_GPR();
                    tmpreg2 = ALLOC_GPR();
                    if (!is_signed_right && !is_signed_left) {
                        emitpcode(PC_MULHWU, tmpreg2, opleft.reg, opright.reg);
                        emitpcode(PC_MULLI, tmpreg1, opleft.regHi, LOW_PART(right->data.intval.lo));
                        emitpcode(PC_MULLI, reg, opleft.reg, LOW_PART(right->data.intval.lo));
                        emitpcode(PC_ADD, regHi, tmpreg2, tmpreg1);
                    } else {
                        tmpreg3 = ALLOC_GPR();
                        tmpreg4 = ALLOC_GPR();
                        emitpcode(PC_SRAWI, tmpreg4, opright.reg, 31);
                        emitpcode(PC_MULHWU, tmpreg2, opleft.reg, opright.reg);
                        emitpcode(PC_MULLI, tmpreg1, opleft.regHi, LOW_PART(right->data.intval.lo));
                        emitpcode(PC_MULLI, reg, opleft.reg, LOW_PART(right->data.intval.lo));
                        emitpcode(PC_MULLW, tmpreg3, opleft.reg, tmpreg4);
                        emitpcode(PC_ADD, regHi, tmpreg2, tmpreg1);
                        emitpcode(PC_ADD, regHi, regHi, tmpreg3);
                    }
                } else {
                    tmpreg1 = ALLOC_GPR();
                    tmpreg2 = ALLOC_GPR();
                    if (!is_signed_right && !is_signed_left) {
                        emitpcode(PC_MULHWU, tmpreg2, opleft.reg, opright.reg);
                        emitpcode(PC_MULLW, tmpreg1, opleft.regHi, opright.reg);
                        emitpcode(PC_MULLW, reg, opleft.reg, opright.reg);
                        emitpcode(PC_ADD, regHi, tmpreg2, tmpreg1);
                    } else {
                        tmpreg3 = ALLOC_GPR();
                        tmpreg4 = ALLOC_GPR();
                        emitpcode(PC_SRAWI, tmpreg4, opright.reg, 31);
                        emitpcode(PC_MULHWU, tmpreg2, opleft.reg, opright.reg);
                        emitpcode(PC_MULLW, tmpreg1, opleft.regHi, opright.reg);
                        emitpcode(PC_MULLW, reg, opleft.reg, opright.reg);
                        emitpcode(PC_MULLW, tmpreg3, opleft.reg, tmpreg4);
                        emitpcode(PC_ADD, regHi, tmpreg2, tmpreg1);
                        emitpcode(PC_ADD, regHi, regHi, tmpreg3);
                    }
                }
                break;
            case 8 + 8:
                if (ENODE_IS(left, EINTCONST) &&
                    FITS_IN_SHORT2(left->data.intval.lo) &&
                    left->data.intval.hi == 0) {
                    tmpreg1 = ALLOC_GPR();
                    tmpreg2 = ALLOC_GPR();
                    emitpcode(PC_MULHWU, tmpreg1, opright.reg, opleft.reg);
                    emitpcode(PC_MULLW, tmpreg2, opright.reg, opleft.regHi);
                    emitpcode(PC_MULLI, reg, opright.reg, LOW_PART(left->data.intval.lo));
                    emitpcode(PC_ADD, regHi, tmpreg1, tmpreg2);
                } else if (ENODE_IS(right, EINTCONST) &&
                           FITS_IN_SHORT2(right->data.intval.lo) &&
                           right->data.intval.hi == 0) {
                    tmpreg1 = ALLOC_GPR();
                    tmpreg2 = ALLOC_GPR();
                    emitpcode(PC_MULHWU, tmpreg2, opleft.reg, opright.reg);
                    emitpcode(PC_MULLW, tmpreg1, opleft.regHi, opright.reg);
                    emitpcode(PC_MULLI, reg, opleft.reg, LOW_PART(right->data.intval.lo));
                    emitpcode(PC_ADD, regHi, tmpreg2, tmpreg1);
                } else {
                    tmpreg1 = ALLOC_GPR();
                    tmpreg2 = ALLOC_GPR();
                    tmpreg3 = ALLOC_GPR();
                    tmpreg4 = ALLOC_GPR();
                    emitpcode(PC_MULHWU, tmpreg2, opleft.reg, opright.reg);
                    emitpcode(PC_MULLW, tmpreg1, opleft.regHi, opright.reg);
                    emitpcode(PC_ADD, tmpreg3, tmpreg2, tmpreg1);
                    emitpcode(PC_MULLW, tmpreg4, opleft.reg, opright.regHi);
                    emitpcode(PC_MULLW, reg, opleft.reg, opright.reg);
                    emitpcode(PC_ADD, regHi, tmpreg3, tmpreg4);
                }
                break;
            default:
                CError_FATAL(8642);
        }
    }

    output->optype = OpndType_GPRPair;
    output->reg = reg;
    output->regHi = regHi;
}

void I8_gen_BINNOT(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* inner;
    Operand op;
    int regHi;
    int reg;

    inner = expr->data.monadic;
    memclrw(&op, sizeof(Operand));
    GEN_NODE(inner, &op);
    coerce_to_register_pair(&op, inner->rtype, 0, 0);

    reg = outputReg ? outputReg : ALLOC_GPR();
    regHi = outputRegHi ? outputRegHi : ALLOC_GPR();
    emitpcode(PC_NOR, reg, op.reg, op.reg);
    emitpcode(PC_NOR, regHi, op.regHi, op.regHi);

    output->optype = OpndType_GPRPair;
    output->reg = reg;
    output->regHi = regHi;
}

void I8_gen_MONMIN(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* inner;
    Operand op;
    int regHi;
    int reg;

    inner = expr->data.monadic;
    memclrw(&op, sizeof(Operand));
    GEN_NODE(inner, &op);
    coerce_to_register_pair(&op, inner->rtype, 0, 0);

    reg = outputReg ? outputReg : ALLOC_GPR();
    regHi = outputRegHi ? outputRegHi : ALLOC_GPR();
    emitpcode(PC_SUBFIC, reg, op.reg, 0);
    emitpcode(PC_SUBFZE, regHi, op.regHi);

    output->optype = OpndType_GPRPair;
    output->reg = reg;
    output->regHi = regHi;
}

void I8_gen_ASS(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Type* type;
    ENode* left;
    ENode* right;
    Operand opleft;
    Operand opright;
    Operand opbf;
    VarInfo* vi;

    type = expr->rtype;
    if (ENODE_IS(expr, ECONDASS)) {
        left = expr->data.cond.expr1;
        if (ENODE_IS(left, EINDIRECT)) {
            left = left->data.monadic;
        } else {
            CError_FATAL(8752);
        }
        right = expr->data.cond.expr2;
    } else {
        left = expr->data.diadic.left;
        right = expr->data.diadic.right;
    }

    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));

    if (ENODE_IS(left, EOBJREF) && OBJECT_REG(left->data.objref)) {
        vi = Registers_GetVarInfo(left->data.objref);
        GEN_NODE_TO_REG(right, vi->reg, vi->regHi, &opright);
        if (vi->rclass != RegClass_GPR) {
            CError_FATAL(8772);
        } else {
            coerce_to_register_pair(&opright, type, vi->reg, vi->regHi);
            *output = opright;
        }
    } else if (TYPE_FITS_IN_REGISTER(type)) {
        GEN_NODE(right, &opright);
        coerce_to_register_pair(&opright, right->rtype, 0, 0);

        if (ENODE_IS(left, EBITFIELD)) {
            memclrw(&opbf, sizeof(Operand));
            GEN_NODE(left->data.monadic, &opleft);
            indirect(&opleft, expr);
            opbf = opleft;
            coerce_to_register_pair(&opbf, type, 0, 0);
            I8_insert_bitfield(opright.reg, opright.regHi, &opbf, TYPE_BITFIELD(left->rtype));
            store_pair(opbf.reg, opbf.regHi, &opleft, type);
            if (!expr->ignored) {
                I8_extract_bitfield(
                    &opbf, TYPE_BITFIELD(left->rtype), opright.reg, opright.regHi, &opleft
                );
            }
        } else {
            GEN_NODE(left, &opleft);
            indirect(&opleft, left);
            store_pair(opright.reg, opright.regHi, &opleft, type);
        }

        output->optype = OpndType_GPRPair;
        output->reg = opright.reg;
        output->regHi = opright.regHi;
    }
}

void I8_gen_POSTINCDEC(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* inner;
    Type* type;
    TypeBitfield* tbitfield;
    int reg;
    int regHi;
    Operand op1;
    Operand op2;
    Operand word;

    inner = expr->data.monadic->data.monadic;
    type = expr->rtype;
    tbitfield = NULL;
    memclrw(&word, sizeof(Operand));
    memclrw(&op1, sizeof(Operand));
    memclrw(&op2, sizeof(Operand));

    if (ENODE_IS(inner, EOBJREF) && (reg = OBJECT_REG(inner->data.objref))) {
        regHi = Registers_GetVarInfo(inner->data.objref)->regHi;
        output->optype = OpndType_GPRPair;
        output->reg =
            (outputReg && (outputReg != reg && outputReg != regHi)) ? outputReg : ALLOC_GPR();
        output->regHi = (outputRegHi && (outputRegHi != regHi && outputRegHi != reg)) ? outputRegHi
                                                                                      : ALLOC_GPR();
        emitpcode(PC_MR, output->reg, reg);
        emitpcode(PC_MR, output->regHi, regHi);
        if (ENODE_IS(expr, EPOSTINC)) {
            emitpcode(PC_ADDIC, reg, reg, 1);
            emitpcode(PC_ADDZE, regHi, regHi);
        } else {
            emitpcode(PC_ADDIC, reg, reg, -1);
            emitpcode(PC_ADDME, regHi, regHi);
        }
    } else {
        if (ENODE_IS(inner, EBITFIELD)) {
            tbitfield = TYPE_BITFIELD(inner->rtype);
            inner = inner->data.monadic;
        }

        GEN_NODE(inner, &op1);
        indirect(&op1, inner);
        op2 = op1;
        coerce_to_register_pair(&op2, type, 0, 0);
        if (tbitfield) {
            word = op2;
            I8_extract_bitfield(&word, tbitfield, 0, 0, &op2);
        }

        output->optype = OpndType_GPRPair;
        output->reg = ALLOC_GPR();
        output->regHi = ALLOC_GPR();

        emitpcode(PC_MR, output->reg, op2.reg);
        emitpcode(PC_MR, output->regHi, op2.regHi);

        reg = ALLOC_GPR();
        regHi = ALLOC_GPR();

        if (ENODE_IS(expr, EPOSTINC)) {
            emitpcode(PC_ADDIC, reg, op2.reg, 1);
            emitpcode(PC_ADDZE, regHi, op2.regHi);
        } else {
            emitpcode(PC_ADDIC, reg, op2.reg, -1);
            emitpcode(PC_ADDME, regHi, op2.regHi);
        }

        if (tbitfield) {
            I8_insert_bitfield(reg, regHi, &word, tbitfield);
            reg = word.reg;
            regHi = word.regHi;
        }
        store_pair(reg, regHi, &op1, type);
    }
}

void I8_gen_INDIRECT(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Type* type;
    ENode* inner;
    VarInfo* vi;
    Operand op;

    type = expr->rtype;
    inner = expr->data.monadic;
    if (ENODE_IS(inner, EOBJREF) && OBJECT_REG(inner->data.objref)) {
        vi = Registers_GetVarInfo(inner->data.objref);
        switch (vi->rclass) {
            case RegClass_GPR:
                output->optype = OpndType_GPRPair;
                break;
            case RegClass_FPR:
                output->optype = OpndType_FPR;
                break;
            default:
                CError_FATAL(8935);
        }

        output->reg = vi->reg;
        output->regHi = vi->regHi;
        output->object = NULL;
    } else if (ENODE_IS(inner, EBITFIELD)) {
        memclrw(&op, sizeof(Operand));
        GEN_NODE(inner->data.monadic, &op);
        indirect(&op, expr);
        coerce_to_register_pair(&op, type, 0, 0);
        I8_extract_bitfield(&op, TYPE_BITFIELD(inner->rtype), outputReg, outputRegHi, output);
    } else {
        GEN_NODE(inner, output);
        indirect(output, inner);
    }
}

void I8_gen_condition(ENode* cond, Operand* output, int write_to_gpr) {
    ENode* left;
    ENode* right;
    Operand opleft;
    Operand opright;
    Operand swapOp;
    int reg1;
    int reg2;
    int reg3;
    int reg4;
    int reg5;

    left = cond->data.diadic.left;
    right = cond->data.diadic.right;
    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));
    memclrw(&swapOp, sizeof(Operand));

    if (right->hascall) {
        GEN_NODE(right, &opright);
        if (right->rtype->size < 4) {
            extend32(&opright, right->rtype, 0);
        }
        ENSURE_GPR(&opright, right->rtype, 0);

        if (right->rtype->size < 8) {
            short hiReg = ALLOC_GPR();
            if (is_unsigned(right->rtype)) {
                load_immediate(hiReg, 0);
            } else {
                emitpcode(PC_SRAWI, hiReg, opright.reg, 31);
            }
            opright.optype = OpndType_GPRPair;
            opright.regHi = hiReg;
        }

        GEN_NODE(left, &opleft);
        if (left->rtype->size < 4) {
            extend32(&opleft, left->rtype, 0);
        }
        ENSURE_GPR(&opleft, left->rtype, 0);

        if (left->rtype->size < 8) {
            short hiReg = ALLOC_GPR();
            if (is_unsigned(right->rtype)) {
                load_immediate(hiReg, 0);
            } else {
                emitpcode(PC_SRAWI, hiReg, opleft.reg, 31);
            }
            opleft.optype = OpndType_GPRPair;
            opleft.regHi = hiReg;
        }
    } else {
        GEN_NODE(left, &opleft);
        ENSURE_GPR(&opleft, left->rtype, 0);
        if (left->rtype->size < 4) {
            extend32(&opleft, left->rtype, 0);
        }

        if (left->rtype->size < 8) {
            short hiReg = ALLOC_GPR();
            if (is_unsigned(right->rtype)) {
                load_immediate(hiReg, 0);
            } else {
                emitpcode(PC_SRAWI, hiReg, opleft.reg, 31);
            }
            opleft.optype = OpndType_GPRPair;
            opleft.regHi = hiReg;
        }

        GEN_NODE(right, &opright);
        if (right->rtype->size < 4) {
            extend32(&opright, right->rtype, 0);
        }
        ENSURE_GPR(&opright, right->rtype, 0);

        if (right->rtype->size < 8) {
            short hiReg = ALLOC_GPR();
            if (is_unsigned(right->rtype)) {
                load_immediate(hiReg, 0);
            } else {
                emitpcode(PC_SRAWI, hiReg, opright.reg, 31);
            }
            opright.optype = OpndType_GPRPair;
            opright.regHi = hiReg;
        }
    }

    CError_ASSERT(9128, opleft.optype == OpndType_GPRPair && opright.optype == OpndType_GPRPair);

    switch (cond->type) {
        case EEQU:
        case ENOTEQU:
            reg1 = ALLOC_GPR();
            reg2 = ALLOC_GPR();
            emitpcode(PC_XOR, reg1, opleft.reg, opright.reg);
            emitpcode(PC_XOR, reg2, opleft.regHi, opright.regHi);
            emitpcode(PC_OR, reg2, reg1, reg2);
            if (write_to_gpr) {
                if (ENODE_IS(cond, EEQU)) {
                    emitpcode(PC_CNTLZW, reg2, reg2);
                    emitpcode(PC_RLWINM, reg2, reg2, 27, 5, 31);
                } else {
                    emitpcode(PC_ADDIC, reg1, reg2, -1);
                    emitpcode(PC_SUBFE, reg2, reg1, reg2);
                }
                output->optype = OpndType_GPR;
                output->reg = reg2;
                output->regHi = 0;
            } else {
                emitpcode(PC_CMPI, 0, reg2, 0);
                output->optype = OpndType_CRField;
                output->reg = 0;
                output->regOffset = cond->type;
            }
            break;
        case EGREATER:
            swapOp = opleft;
            opleft = opright;
            opright = swapOp;
        case ELESS:
            reg1 = ALLOC_GPR();
            reg2 = ALLOC_GPR();
            reg3 = ALLOC_GPR();
            if (left->rtype != TYPE(&stunsignedlonglong) &&
                right->rtype != TYPE(&stunsignedlonglong)) {
                emitpcode(PC_XORIS, reg1, opleft.regHi, 0x8000);
                emitpcode(PC_XORIS, reg2, opright.regHi, 0x8000);
                reg4 = reg1;
                reg5 = reg2;
            } else {
                reg4 = opleft.regHi;
                reg5 = opright.regHi;
            }
            emitpcode(PC_SUBFC, reg3, opright.reg, opleft.reg);
            emitpcode(PC_SUBFE, reg2, reg5, reg4);
            emitpcode(PC_SUBFE, reg2, reg1, reg1);
            emitpcode(PC_NEG, reg2, reg2);
            if (write_to_gpr) {
                output->optype = OpndType_GPR;
                output->reg = reg2;
                output->regHi = 0;
            } else {
                emitpcode(PC_CMPI, 0, reg2, 0);
                output->optype = OpndType_CRField;
                output->reg = 0;
                output->regOffset = ENOTEQU;
            }
            break;
        case ELESSEQU:
            swapOp = opleft;
            opleft = opright;
            opright = swapOp;
        case EGREATEREQU:
            reg1 = ALLOC_GPR();
            reg2 = ALLOC_GPR();
            reg3 = ALLOC_GPR();
            if (left->rtype != TYPE(&stunsignedlonglong) &&
                right->rtype != TYPE(&stunsignedlonglong)) {
                emitpcode(PC_XORIS, reg1, opleft.regHi, 0x8000);
                emitpcode(PC_XORIS, reg2, opright.regHi, 0x8000);
                reg4 = reg1;
                reg5 = reg2;
            } else {
                reg4 = opleft.regHi;
                reg5 = opright.regHi;
            }
            emitpcode(PC_SUBFC, reg3, opright.reg, opleft.reg);
            emitpcode(PC_SUBFE, reg2, reg5, reg4);
            emitpcode(PC_SUBFE, reg2, reg1, reg1);
            emitpcode(PC_NEG, reg2, reg2);
            if (write_to_gpr) {
                emitpcode(PC_SUBFIC, reg2, reg2, 1);
                output->optype = OpndType_GPR;
                output->reg = reg2;
                output->regHi = 0;
            } else {
                emitpcode(PC_CMPI, 0, reg2, 0);
                output->optype = OpndType_CRField;
                output->reg = 0;
                output->regOffset = EEQU;
            }
            break;
        default:
            CError_FATAL(9238);
    }
}

void I8_gen_SHL_SHR(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    static UInt32 used_regs[RegClassMax] = {0, 0, 0, 0, (1 << 3) | (1 << 4) | (1 << 5)};

    ENode* left;
    ENode* right;
    Operand opleft;
    Operand opright;
    int is_signed;
    int is_signed_right;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));

    GEN_NODE(left, &opleft);
    coerce_to_register_pair(&opleft, left->rtype, 0, 0);

    output->optype = OpndType_GPRPair;
    output->reg = ALLOC_GPR();
    output->regHi = ALLOC_GPR();

    is_signed = !is_unsigned(left->rtype);
    is_signed_right = !is_unsigned(right->rtype);

    if (ENODE_IS(right, EINTCONST)) {
        if (ENODE_IS(expr, ESHL)) {
            I8_ShiftLeftImmediate(
                opleft,
                right->data.intval.lo,
                !is_signed,
                expr->rtype->size,
                output->reg,
                output->regHi
            );
        } else {
            I8_ShiftRightImmediate(
                opleft, right->data.intval.lo, !is_signed, output->reg, output->regHi, FALSE
            );
        }
    } else {
        GEN_NODE(right, &opright);
        ENSURE_GPR(&opright, right->rtype, 0);
        if (opright.optype == OpndType_GPRPair) {
            opright.regHi = 0;
            opright.optype = OpndType_GPR;
        }

        CError_ASSERT(9314, opleft.optype == OpndType_GPRPair && opright.optype == OpndType_GPR);

        if (opleft.regHi != high_reg) {
            emitpcode(PC_MR, high_reg, opleft.regHi);
        }
        if (opleft.reg != low_reg) {
            emitpcode(PC_MR, low_reg, opleft.reg);
        }
        if ((int)opright.reg != 5) {
            emitpcode(PC_MR, 5, opright.reg);
        }

        if (ENODE_IS(expr, ESHR)) {
            if (!is_signed) {
                branch_subroutine(rt_shr2u, 0, used_regs);
            } else {
                branch_subroutine(rt_shr2i, 0, used_regs);
            }
        } else if (ENODE_IS(expr, ESHL)) {
            branch_subroutine(rt_shl2i, 0, used_regs);
        } else {
            CError_FATAL(9333);
        }

        emitpcode(PC_MR, output->reg, low_reg);
        emitpcode(PC_MR, output->regHi, high_reg);
    }
}

void I8_gen_DIV_MOD(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    static UInt32 used_regs[RegClassMax] = {0, 0, 0, 0, (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6)};

    int is_signed;
    int is_signed_right;
    ENode* left;
    ENode* right;
    Operand opleft;
    Operand opright;
    long long constval;
    SInt32 shift;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));

    GEN_NODE(left, &opleft);
    coerce_to_register_pair(&opleft, left->rtype, 0, 0);

    output->optype = OpndType_GPRPair;
    output->reg = ALLOC_GPR();
    output->regHi = ALLOC_GPR();

    is_signed = !is_unsigned(left->rtype);
    is_signed_right = !is_unsigned(right->rtype);

    if (ENODE_IS(right, EINTCONST)) {
        constval = (((long long)right->data.intval.hi) << 32) + right->data.intval.lo;
    }
    if (ENODE_IS(right, EINTCONST) && ((shift = I8_log2n(constval)) > 0)) {
        CError_ASSERT(9400, opleft.optype == OpndType_GPRPair);
        if (ENODE_IS(expr, EDIV)) {
            I8_ShiftRightImmediate(opleft, shift, !is_signed, output->reg, output->regHi, TRUE);
            if (is_signed) {
                emitpcode(PC_ADDZE, output->reg, output->reg);
                emitpcode(PC_ADDZE, output->regHi, output->regHi);
            }
        } else {
            if (!is_signed) {
                if (shift < 32) {
                    emitpcode(PC_LI, output->regHi, 0);
                    emitpcode(PC_RLWINM, output->reg, opleft.reg, 0, 32 - shift, 31);
                } else if (shift == 32) {
                    emitpcode(PC_LI, output->regHi, 0);
                    emitpcode(PC_MR, output->reg, opleft.reg);
                } else if (shift <= 63) {
                    emitpcode(PC_RLWINM, output->regHi, opleft.regHi, 0, 32 - (shift - 32), 31);
                    emitpcode(PC_MR, output->reg, opleft.reg);
                } else {
                    CError_FATAL(9442);
                }
            } else {
                short tmpreg1 = ALLOC_GPR();
                short tmpreg2 = ALLOC_GPR();
                I8_ShiftRightImmediate(opleft, shift, !is_signed, output->reg, output->regHi, TRUE);
                emitpcode(PC_ADDZE, output->reg, output->reg);
                emitpcode(PC_ADDZE, output->regHi, output->regHi);
                I8_ShiftLeftImmediate(
                    *output, shift, !is_signed, expr->rtype->size, tmpreg1, tmpreg2
                );
                emitpcode(PC_SUBFC, output->reg, tmpreg1, opleft.reg);
                emitpcode(PC_SUBFE, output->regHi, tmpreg2, opleft.regHi);
            }
        }
    } else {
        GEN_NODE(right, &opright);
        coerce_to_register_pair(&opright, right->rtype, 0, 0);

        CError_ASSERT(
            9472, opleft.optype == OpndType_GPRPair && opright.optype == OpndType_GPRPair
        );

        if (opleft.regHi != high_reg) {
            emitpcode(PC_MR, high_reg, opleft.regHi);
        }
        if (opleft.reg != low_reg) {
            emitpcode(PC_MR, low_reg, opleft.reg);
        }
        if (opright.regHi != high_reg2) {
            emitpcode(PC_MR, high_reg2, opright.regHi);
        }
        if (opright.reg != low_reg2) {
            emitpcode(PC_MR, low_reg2, opright.reg);
        }

        if (ENODE_IS(expr, EDIV)) {
            if (is_unsigned(left->rtype) || is_unsigned(right->rtype)) {
                branch_subroutine(rt_div2u, 0, used_regs);
            } else {
                branch_subroutine(rt_div2i, 0, used_regs);
            }
        } else if (ENODE_IS(expr, EMODULO)) {
            if (is_unsigned(left->rtype) || is_unsigned(right->rtype)) {
                branch_subroutine(rt_mod2u, 0, used_regs);
            } else {
                branch_subroutine(rt_mod2i, 0, used_regs);
            }
        } else {
            CError_FATAL(9498);
        }

        emitpcode(PC_MR, output->reg, low_reg);
        emitpcode(PC_MR, output->regHi, high_reg);
    }
}

void I8_gen_TYPCON(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    static UInt32 used_regs[RegClassMax] = {0, 0, 0, 0, (1 << 3) | (1 << 4)};
    static UInt32 used_regs_f1[RegClassMax] = {0, 0, 0, (1 << 1), 0};

    Type* dsttype;
    Type* type;
    ENode* inner;
    Type* srctype;
    short regHi;
    short reg;

    inner = expr->data.monadic;
    srctype = inner->rtype;
    dsttype = type = expr->rtype;

    if (dsttype->type == TYPEVOID) {
        GEN_NODE(inner, output);
        if (ENODE_IS(inner, EINDIRECT) && (output->flags & OpndFlags_Volatile)) {
            coerce_to_register_pair(output, inner->rtype, 0, 0);
        }
        output->optype = OpndType_Absolute;
        output->immediate = 0;
    } else if (srctype->type == TYPEINT || srctype->type == TYPEENUM) {
        if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(dsttype)) {
            GEN_NODE(inner, output);
            coerce_to_register_pair(output, srctype, 0, 0);
            if (output->regHi != high_reg) {
                emitpcode(PC_MR, high_reg, output->regHi);
            }
            if (output->reg != low_reg) {
                emitpcode(PC_MR, low_reg, output->reg);
            }

            if (is_unsigned(srctype)) {
                branch_subroutine(
                    (dsttype->size == 4) ? rt_cvt_ull_flt : rt_cvt_ull_dbl, 0, used_regs
                );
            } else {
                branch_subroutine(
                    (dsttype->size == 4) ? rt_cvt_sll_flt : rt_cvt_sll_dbl, 0, used_regs
                );
            }

            output->optype = OpndType_FPR;
            output->reg = ALLOC_FPR();
            emitpcode(PC_FMR, output->reg, 1);
        } else if (srctype->size < dsttype->size) {
            CError_ASSERT(
                9595, (type->type == TYPEINT || type->type == TYPEENUM) && type->size == 8
            );

            GEN_NODE(inner, output);
            if (srctype->size < 4 &&
                !(ENODE_IS(inner, EINDIRECT) && ENODE_IS(inner->data.monadic, EBITFIELD)) &&
                !(((inner->type >= EASS && inner->type <= EORASS) ||
                   (inner->type >= EPOSTINC && inner->type <= EPREDEC)) &&
                  ENODE_IS(inner->data.monadic->data.monadic, EBITFIELD))) {
                extend32(output, srctype, outputReg);
            }
            extend64(output, srctype, outputReg, outputRegHi);
        } else {
            GEN_NODE_TO_REG(inner, outputReg, 0, output);
            if (dsttype->size < srctype->size) {
                coerce_to_register_pair(output, srctype, outputReg, outputRegHi);
                output->optype = OpndType_GPR;
                output->regHi = 0;
            }
        }
    } else if (IS_TYPE_POINTER(srctype)) {
        GEN_NODE_TO_REG(inner, outputReg, 0, output);
        CError_ASSERT(
            9624,
            (expr->rtype->type == TYPEINT || expr->rtype->type == TYPEENUM) &&
                expr->rtype->size == 8
        );
        GEN_NODE_TO_REG(inner, outputReg, 0, output);

        regHi = outputRegHi ? outputRegHi : ALLOC_GPR();
        if (regHi == output->reg) {
            reg = outputReg ? outputReg : ALLOC_GPR();
            emitpcode(PC_MR, reg, output->reg);
            output->reg = reg;
        }
        if (is_unsigned(inner->rtype)) {
            load_immediate(regHi, 0);
        } else {
            emitpcode(PC_SRAWI, regHi, output->reg, 31);
        }
        output->optype = OpndType_GPRPair;
        output->regHi = regHi;
    } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(srctype)) {
        if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(dsttype)) {
            CError_FATAL(9646);
        } else {
            GEN_NODE(inner, output);
            ENSURE_FPR(output, srctype, 0);
            if ((int)output->reg != 1) {
                emitpcode(PC_FMR, 1, output->reg);
            }

            branch_subroutine(rt_cvt_dbl_usll, 0, used_regs_f1);

            output->optype = OpndType_GPRPair;
            output->reg = ALLOC_GPR();
            output->regHi = ALLOC_GPR();
            emitpcode(PC_MR, output->reg, low_reg);
            emitpcode(PC_MR, output->regHi, high_reg);
        }
    } else if (srctype->type == TYPESTRUCT) {
        GEN_NODE_TO_REG(inner, outputReg, 0, output);

        if ((expr->rtype->type == TYPEINT || expr->rtype->type == TYPEENUM) &&
            expr->rtype->size == 8 &&
            dsttype->size == srctype->size) {
            coerce_to_register_pair(output, srctype, outputReg, outputRegHi);
        } else {
            CError_FATAL(9680);
        }
    } else {
        CError_FATAL(9685);
    }
}

void gen_VECTOR128CONST(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    int gpr;
    int vr;
    COVCResult result;

    vr = outputReg ? outputReg : ALLOC_VR();
    if (!canoptimizevectorconst(&expr->data.vector128val, expr->rtype, &result)) {
        CError_FATAL(9706);
    }

    if (result.op1 != -1) {
        emitpcode(result.op1, vr, result.arg);
        output->optype = OpndType_VR;
        output->reg = vr;
        return;
    }

    if (result.op2 != -1) {
        gpr = ALLOC_GPR();
        emitpcode(PC_LI, gpr, result.arg);
        emitpcode(result.op2, vr, 0, gpr);
        output->optype = OpndType_VR;
        output->reg = vr;
        return;
    }

    CError_FATAL(9722);
}
