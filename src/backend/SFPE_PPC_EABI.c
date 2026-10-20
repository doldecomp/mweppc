// SFPE_PPC_EABI.c: soft-float (-fp soft) code generation through calls to the runtime's rt_sfpe_*
// routines.
#include "compiler/SFPE_PPC_EABI.h"
#include "compiler/CError.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/InstrSelection.h"
#include "compiler/objects.h"
#include "compiler/Operands.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/types.h"

// NOTE: invented names: used_regs_r3_r4, used_regs_r3_r6, used_regs_r3
// the registers (SPR, CR field, VR, FPR, GPR) the soft-float library calls read, for
// branch_subroutine: r3/r4, r3-r6 and r3
static UInt32 used_regs_r3_r4[RegClassMax] = {0, 0, 0, 0, (1 << 3) | (1 << 4)};
static UInt32 used_regs_r3_r6[RegClassMax] = {
    0, 0, 0, 0, (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6)
};
static UInt32 used_regs_r3[RegClassMax] = {0, 0, 0, 0, (1 << 3)};

// NOTE: invented names: lift_result, LoadFloatArg, LoadFloatArgGPR, LoadDoubleArg (from the GC 1.x
// decompilation)
// The helpers below; every call to them is inlined.
static inline void lift_result(Operand* op, int kind) {
    op->optype = kind;
    op->reg = used_virtual_registers[RegClass_GPR]++;
    if (kind == OpndType_GPR) {
        emitpcode(PC_MR, op->reg, 3);
    } else {
        op->regHi = used_virtual_registers[RegClass_GPR]++;
        emitpcode(PC_MR, op->reg, low_reg);
        emitpcode(PC_MR, op->regHi, high_reg);
    }
}

static inline void LoadFloatArg(int reg) {
    union {
        float f;
        SInt32 l;
    } u;

    u.f = one_point_zero.data.value;
    insert_load_immediate_after(reg, u.l);
}

// (a second inline level: SFPE_gen_POSTINCDEC's in-memory case calls through it)
static inline void LoadFloatArgGPR(int reg) {
    LoadFloatArg(reg);
}

static inline void LoadDoubleArg(int reg1, int reg2) {
    union {
        Float f;
        CInt64 i;
    } v;

    v.f = one_point_zero;
    insert_load_immediate_after(reg2, v.i.lo);
    insert_load_immediate_after(reg1, v.i.hi);
    if (reg2 != high_reg2) {
        emitpcode(PC_MR, high_reg2, reg2);
    }
    if (reg1 != low_reg2) {
        emitpcode(PC_MR, low_reg2, reg1);
    }
}

void SFPE_gen_OP(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* left;
    Operand leftOperand;
    Operand rightOperand;
    ENode* right;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;

    memclrw(&leftOperand, sizeof(leftOperand));
    memclrw(&rightOperand, sizeof(rightOperand));

    if (expr->rtype->size == 4) {
        GEN_NODE(right, &rightOperand);
        if (rightOperand.optype) {
            Coerce_to_register(&rightOperand, right->rtype, 0);
        }
        CError_ASSERT(154, rightOperand.optype == OpndType_GPR);

        GEN_NODE(left, &leftOperand);
        if (leftOperand.optype) {
            Coerce_to_register(&leftOperand, left->rtype, 0);
        }
        CError_ASSERT(161, leftOperand.optype == OpndType_GPR);

        {
            SInt32 reg = leftOperand.reg;
            if (reg != 3) {
                emitpcode(PC_MR, 3, reg);
            }
        }
        {
            SInt32 reg = rightOperand.reg;
            if (reg != 4) {
                emitpcode(PC_MR, 4, reg);
            }
        }

        if (expr->type == EADD) {
            branch_subroutine(rt_sfpe_f_add, 0, used_regs_r3_r4);
        } else if (expr->type == ESUB) {
            branch_subroutine(rt_sfpe_f_sub, 0, used_regs_r3_r4);
        } else if (expr->type == EMUL) {
            branch_subroutine(rt_sfpe_f_mul, 0, used_regs_r3_r4);
        } else if (expr->type == EDIV) {
            branch_subroutine(rt_sfpe_f_div, 0, used_regs_r3_r4);
        } else {
            CError_FATAL(218);
        }

        output->optype = OpndType_GPR;
        output->reg = used_virtual_registers[RegClass_GPR]++;
        emitpcode(PC_MR, output->reg, 3);
    } else {
        GEN_NODE(right, &rightOperand);
        coerce_to_register_pair(&rightOperand, right->rtype, 0, 0);
        CError_ASSERT(237, rightOperand.optype == OpndType_GPRPair);

        GEN_NODE(left, &leftOperand);
        coerce_to_register_pair(&leftOperand, left->rtype, 0, 0);
        CError_ASSERT(243, leftOperand.optype == OpndType_GPRPair);

        if (leftOperand.regHi != high_reg) {
            emitpcode(PC_MR, high_reg, leftOperand.regHi);
        }
        if (leftOperand.reg != low_reg) {
            emitpcode(PC_MR, low_reg, leftOperand.reg);
        }
        if (rightOperand.regHi != high_reg2) {
            emitpcode(PC_MR, high_reg2, rightOperand.regHi);
        }
        if (rightOperand.reg != low_reg2) {
            emitpcode(PC_MR, low_reg2, rightOperand.reg);
        }

        if (expr->type == EADD) {
            branch_subroutine(rt_sfpe_d_add, 0, used_regs_r3_r6);
        } else if (expr->type == ESUB) {
            branch_subroutine(rt_sfpe_d_sub, 0, used_regs_r3_r6);
        } else if (expr->type == EMUL) {
            branch_subroutine(rt_sfpe_d_mul, 0, used_regs_r3_r6);
        } else if (expr->type == EDIV) {
            if (((left->rtype->type == TYPEINT || left->rtype->type == TYPEENUM) &&
                 left->rtype->size == 8 &&
                 is_unsigned(left->rtype)) ||
                ((right->rtype->type == TYPEINT || right->rtype->type == TYPEENUM) &&
                 right->rtype->size == 8 &&
                 is_unsigned(right->rtype))) {
                PPCError_Error(PPCERR_FLOAT_CHECKED_AT, __FILE__, 269);
                branch_subroutine(rt_sfpe_d_div, 0, used_regs_r3_r6);
            } else {
                branch_subroutine(rt_sfpe_d_div, 0, used_regs_r3_r6);
            }
        } else {
            CError_FATAL(276);
        }

        output->optype = OpndType_GPRPair;
        output->reg = used_virtual_registers[RegClass_GPR]++;
        output->regHi = used_virtual_registers[RegClass_GPR]++;
        emitpcode(PC_MR, output->reg, low_reg);
        emitpcode(PC_MR, output->regHi, high_reg);
    }
}

void SFPE_gen_FLOATCONST(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    if (expr->rtype->size == 4) {
        union {
            float f;
            SInt32 l;
        } u;
        int reg;
        reg = outputReg ? outputReg : used_virtual_registers[RegClass_GPR]++;
        u.f = expr->data.floatval.data.value;
        insert_load_immediate_after(reg, u.l);
        output->optype = OpndType_GPR;
        output->reg = reg;
    } else {
        int regHi;
        int reg;
        CInt64 v;
        reg = outputReg ? outputReg : used_virtual_registers[RegClass_GPR]++;
        regHi = outputRegHi ? outputRegHi : used_virtual_registers[RegClass_GPR]++;
        v = expr->data.intval;
        insert_load_immediate_after(regHi, v.lo);
        insert_load_immediate_after(reg, v.hi);
        output->optype = OpndType_GPRPair;
        output->reg = reg;
        output->regHi = regHi;
    }
}

void SFPE_gen_MONMIN(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Operand operand;
    ENode* subexpr;

    subexpr = expr->data.monadic;
    memclrw(&operand, sizeof(operand));
    if (expr->rtype->size == 4) {
        SInt32 resultReg;
        GEN_NODE(subexpr, &operand);
        if (operand.optype) {
            Coerce_to_register(&operand, subexpr->rtype, 0);
        }
        resultReg = outputReg ? outputReg : used_virtual_registers[RegClass_GPR]++;
        emitpcode(PC_XORIS, resultReg, operand.reg, 0x8000);
        output->optype = OpndType_GPR;
        output->reg = resultReg;
    } else {
        SInt32 resultLowReg, resultHighReg;
        GEN_NODE(subexpr, &operand);
        coerce_to_register_pair(&operand, subexpr->rtype, 0, 0);
        resultHighReg = outputRegHi ? outputRegHi : used_virtual_registers[RegClass_GPR]++;
        emitpcode(PC_XORIS, resultHighReg, operand.regHi, 0x8000);
        resultLowReg = outputReg ? outputReg : used_virtual_registers[RegClass_GPR]++;
        if (resultLowReg != operand.reg) {
            emitpcode(PC_MR, resultLowReg, operand.reg);
        }
        output->optype = OpndType_GPRPair;
        output->reg = resultLowReg;
        output->regHi = resultHighReg;
    }
}

void SFPE_gen_ASS(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    VarInfo* vi;
    Type* type;
    ENode* left;
    ENode* right;
    Operand opleft;
    Operand opright;
    Operand op2;
    SInt32 incval;

    type = expr->rtype;
    if (ENODE_IS(expr, ECONDASS)) {
        left = expr->data.cond.expr1;
        if (ENODE_IS(left, EINDIRECT)) {
            left = left->data.monadic;
        } else {
            CError_FATAL(413);
        }
        right = expr->data.cond.expr2;
    } else {
        left = expr->data.diadic.left;
        right = expr->data.diadic.right;
    }

    memclrw(&opleft, sizeof(Operand));
    memclrw(&opright, sizeof(Operand));
    memclrw(&op2, sizeof(Operand));

    if (ENODE_IS(left, EOBJREF) && OBJECT_REG(left->data.objref)) {
        vi = Registers_GetVarInfo(left->data.objref);
        GEN_NODE_TO_REG(right, vi->reg, vi->regHi, &opright);
        if (type->size == 4) {
            ENSURE_GPR(&opright, type, vi->reg);
            if (opright.reg != vi->reg) {
                emitpcode(PC_MR, vi->reg, opright.reg);
            }
            output->optype = OpndType_GPR;
            output->reg = vi->reg;
        } else {
            coerce_to_register_pair(&opright, type, vi->reg, vi->regHi);
            if (opright.reg != vi->reg) {
                emitpcode(PC_MR, vi->reg, opright.reg);
            }
            if (opright.regHi != vi->regHi) {
                emitpcode(PC_MR, vi->regHi, opright.regHi);
            }
            output->optype = OpndType_GPRPair;
            output->reg = vi->reg;
            output->regHi = vi->regHi;
        }
    } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
        GEN_NODE(right, &opright);
        if (type->size == 4) {
            ENSURE_GPR(&opright, right->rtype, 0);
            if (ispostincrementopportunity(left, &opleft, &incval)) {
                indirect(&opleft, left);
                store(opright.reg, &opleft, type);
                add_register_immediate(opleft.reg, opleft.reg, incval);
            } else {
                GEN_NODE(left, &opleft);
                indirect(&opleft, left);
                store(opright.reg, &opleft, type);
            }
            output->optype = OpndType_GPR;
            output->reg = opright.reg;
        } else {
            coerce_to_register_pair(&opright, right->rtype, 0, 0);
            if (ENODE_IS(left, EBITFIELD)) {
                CError_FATAL(489);
            }
            if (ispostincrementopportunity(left, &opleft, &incval)) {
                indirect(&opleft, left);
                store_pair(opright.reg, opright.regHi, &opleft, type);
                add_register_immediate(opleft.reg, opleft.reg, incval);
            } else {
                GEN_NODE(left, &opleft);
                indirect(&opleft, left);
                store_pair(opright.reg, opright.regHi, &opleft, type);
            }
            output->optype = OpndType_GPRPair;
            output->reg = opright.reg;
            output->regHi = opright.regHi;
        }
    } else if (TYPE_FITS_IN_REGISTER(type)) {
        CError_FATAL(511);
    }
}

void SFPE_gen_POSTINCDEC(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* inner;
    Type* type;
    int sourceReg;
    int sourceReg2;
    int argumentReg;
    int argumentReg2;
    Operand operand;
    Operand value;

    inner = expr->data.monadic->data.monadic;
    type = expr->rtype;
    memclrw(&operand, sizeof(operand));
    memclrw(&value, sizeof(value));

    if (type->size == 4) {
        if (inner->type == EOBJREF && (sourceReg = OBJECT_REG(inner->data.objref)) != 0) {
            // the original makes the call and drops its result here (the double case below reads
            // its regHi)
            Registers_GetVarInfo(inner->data.objref);
            output->optype = OpndType_GPR;
            output->reg = (outputReg != 0 && outputReg != sourceReg)
                ? outputReg
                : used_virtual_registers[RegClass_GPR]++;
            CError_ASSERT(546, sourceReg != output->reg);
            emitpcode(PC_MR, output->reg, sourceReg);
            argumentReg = used_virtual_registers[RegClass_GPR]++;
            LoadFloatArg(argumentReg);
            if (sourceReg != 3) {
                emitpcode(PC_MR, 3, sourceReg);
            }
            if (argumentReg != 4) {
                emitpcode(PC_MR, 4, argumentReg);
            }
            if (expr->type == EPOSTINC) {
                branch_subroutine(rt_sfpe_f_add, 0, used_regs_r3_r4);
            } else {
                branch_subroutine(rt_sfpe_f_sub, 0, used_regs_r3_r4);
            }
            emitpcode(PC_MR, sourceReg, 3);
        } else {
            if (inner->type == EBITFIELD) {
                CError_FATAL(590);
            }
            GEN_NODE(inner, &operand);
            indirect(&operand, inner);
            value = operand;
            if (value.optype != OpndType_GPR) {
                Coerce_to_register(&value, type, 0);
            }
            output->optype = OpndType_GPR;
            output->reg = used_virtual_registers[RegClass_GPR]++;
            argumentReg = used_virtual_registers[RegClass_GPR]++;
            LoadFloatArgGPR(argumentReg);
            emitpcode(PC_MR, output->reg, value.reg);
            {
                SInt32 reg = value.reg;
                if (reg != 3) {
                    emitpcode(PC_MR, 3, reg);
                }
            }
            if (argumentReg != 4) {
                emitpcode(PC_MR, 4, argumentReg);
            }
            if (expr->type == EPOSTINC) {
                branch_subroutine(rt_sfpe_f_add, 0, used_regs_r3_r4);
            } else {
                branch_subroutine(rt_sfpe_f_sub, 0, used_regs_r3_r4);
            }
            store(3, &operand, type);
        }
    } else {
        if (inner->type == EOBJREF && (sourceReg = OBJECT_REG(inner->data.objref)) != 0) {
            sourceReg2 = Registers_GetVarInfo(inner->data.objref)->regHi;
            output->optype = OpndType_GPRPair;
            output->regHi =
                (outputRegHi != 0 && (outputRegHi != sourceReg2 && outputRegHi != sourceReg))
                ? outputRegHi
                : used_virtual_registers[RegClass_GPR]++;
            output->reg = (outputReg != 0 && (outputReg != sourceReg && outputReg != sourceReg2))
                ? outputReg
                : used_virtual_registers[RegClass_GPR]++;
            CError_ASSERT(654, sourceReg != output->reg && sourceReg2 != output->reg);
            CError_ASSERT(655, sourceReg != output->regHi && sourceReg2 != output->regHi);
            emitpcode(PC_MR, output->regHi, sourceReg2);
            emitpcode(PC_MR, output->reg, sourceReg);
            if (sourceReg2 != high_reg) {
                emitpcode(PC_MR, high_reg, sourceReg2);
            }
            if (sourceReg != low_reg) {
                emitpcode(PC_MR, low_reg, sourceReg);
            }
            argumentReg = used_virtual_registers[RegClass_GPR]++;
            argumentReg2 = used_virtual_registers[RegClass_GPR]++;
            LoadDoubleArg(argumentReg, argumentReg2);
            if (expr->type == EPOSTINC) {
                branch_subroutine(rt_sfpe_d_add, 0, used_regs_r3_r6);
            } else {
                branch_subroutine(rt_sfpe_d_sub, 0, used_regs_r3_r6);
            }
            emitpcode(PC_MR, sourceReg2, high_reg);
            emitpcode(PC_MR, sourceReg, low_reg);
        } else {
            if (inner->type == EBITFIELD) {
                CError_FATAL(690);
            }
            GEN_NODE(inner, &operand);
            indirect(&operand, inner);
            value = operand;
            coerce_to_register_pair(&value, type, 0, 0);
            output->optype = OpndType_GPRPair;
            output->reg = used_virtual_registers[RegClass_GPR]++;
            output->regHi = used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_MR, output->reg, value.reg);
            emitpcode(PC_MR, output->regHi, value.regHi);
            if (value.regHi != high_reg) {
                emitpcode(PC_MR, high_reg, value.regHi);
            }
            if (value.reg != low_reg) {
                emitpcode(PC_MR, low_reg, value.reg);
            }
            argumentReg = used_virtual_registers[RegClass_GPR]++;
            argumentReg2 = used_virtual_registers[RegClass_GPR]++;
            LoadDoubleArg(argumentReg, argumentReg2);
            if (expr->type == EPOSTINC) {
                branch_subroutine(rt_sfpe_d_add, 0, used_regs_r3_r6);
            } else {
                branch_subroutine(rt_sfpe_d_sub, 0, used_regs_r3_r6);
            }
            store_pair(low_reg, high_reg, &operand, type);
        }
    }
}

void SFPE_gen_INDIRECT(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    Type* type;
    ENode* inner;
    VarInfo* vi;
    Operand op;
    SInt32 postincvalue;

    type = expr->rtype;
    inner = expr->data.monadic;

    memclrw(&op, sizeof(Operand));
    if (ENODE_IS(inner, EOBJREF) && OBJECT_REG(inner->data.objref)) {
        vi = Registers_GetVarInfo(inner->data.objref);
        if (type->size == 4) {
            output->optype = OpndType_GPR;
            output->reg = vi->reg;
            output->object = NULL;
        } else {
            output->optype = OpndType_GPRPair;
            output->reg = vi->reg;
            output->regHi = vi->regHi;
            output->object = NULL;
        }
    } else if (ENODE_IS(inner, EBITFIELD)) {
        CError_FATAL(763);
    } else if (ispostincrementopportunity(inner, &op, &postincvalue) &&
               (TYPE_FITS_IN_REGISTER(type) || IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type))) {
        indirect(&op, inner);
        *output = op;
        if (type->size == 8) {
            coerce_to_register_pair(output, type, outputReg, outputRegHi);
            add_register_immediate(op.reg, op.reg, postincvalue);
        } else {
            ENSURE_GPR(output, type, outputReg);
            add_register_immediate(op.reg, op.reg, postincvalue);
        }
    } else {
        GEN_NODE(inner, output);
        indirect(output, inner);
    }
}

// a floating-point comparison through the soft-float library
void SFPE_gen_condition(ENode* expr, Operand* output, int branch) {
    ENode* left;
    ENode* right;
    Operand leftOperand, rightOperand, tempOperand;
    SInt32 size;
    UInt32* masks;

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;

    memclrw(&leftOperand, sizeof(leftOperand));
    memclrw(&rightOperand, sizeof(rightOperand));
    memclrw(&tempOperand, sizeof(tempOperand));

    size = right->rtype->size;
    if (left->rtype->size != right->rtype->size) {
        CError_FATAL(820);
    }
    if (size < 4 || size > 8) {
        CError_FATAL(823);
    }

    if (right->hascall) {
        GEN_NODE(right, &rightOperand);
        ENSURE_GPR(&rightOperand, right->rtype, 0);
        if (right->rtype->size < size) {
            CError_FATAL(832);
        }

        GEN_NODE(left, &leftOperand);
        ENSURE_GPR(&leftOperand, left->rtype, 0);
        if (left->rtype->size < size) {
            CError_FATAL(839);
        }
    } else {
        GEN_NODE(left, &leftOperand);
        ENSURE_GPR(&leftOperand, left->rtype, 0);
        if (left->rtype->size < size) {
            CError_FATAL(847);
        }

        GEN_NODE(right, &rightOperand);
        ENSURE_GPR(&rightOperand, right->rtype, 0);
        if (right->rtype->size < size) {
            CError_FATAL(854);
        }
    }

    if (size == 8) {
        CError_ASSERT(
            859, leftOperand.optype == OpndType_GPRPair && rightOperand.optype == OpndType_GPRPair
        );
    }
    if (size == 4) {
        CError_ASSERT(
            861, leftOperand.optype == OpndType_GPR && rightOperand.optype == OpndType_GPR
        );
    }

    if (size == 8) {
        masks = used_regs_r3_r6;
        if (leftOperand.regHi != high_reg) {
            emitpcode(PC_MR, high_reg, leftOperand.regHi);
        }
        if (leftOperand.reg != low_reg) {
            emitpcode(PC_MR, low_reg, leftOperand.reg);
        }
        if (rightOperand.regHi != high_reg2) {
            emitpcode(PC_MR, high_reg2, rightOperand.regHi);
        }
        if (rightOperand.reg != low_reg2) {
            emitpcode(PC_MR, low_reg2, rightOperand.reg);
        }
    } else {
        masks = used_regs_r3_r4;
        if ((SInt32)leftOperand.reg != 3) {
            emitpcode(PC_MR, 3, leftOperand.reg);
        }
        if ((SInt32)rightOperand.reg != 4) {
            emitpcode(PC_MR, 4, rightOperand.reg);
        }
    }

    switch (expr->type) {
        case ENOTEQU:
            branch_subroutine(size == 8 ? rt_sfpe_d_fne : rt_sfpe_f_fne, 0, masks);
            break;
        case EEQU:
            branch_subroutine(size == 8 ? rt_sfpe_d_feq : rt_sfpe_f_feq, 0, masks);
            break;
        case EGREATER:
            branch_subroutine(size == 8 ? rt_sfpe_d_fgt : rt_sfpe_f_fgt, 0, masks);
            break;
        case ELESS:
            branch_subroutine(size == 8 ? rt_sfpe_d_flt : rt_sfpe_f_flt, 0, masks);
            break;
        case ELESSEQU:
            branch_subroutine(size == 8 ? rt_sfpe_d_fle : rt_sfpe_f_fle, 0, masks);
            break;
        case EGREATEREQU:
            branch_subroutine(size == 8 ? rt_sfpe_d_fge : rt_sfpe_f_fge, 0, masks);
            break;
        default:
            CError_FATAL(963);
    }

    if (branch) {
        output->optype = OpndType_GPR;
        output->reg = used_virtual_registers[RegClass_GPR]++;
        emitpcode(PC_MR, output->reg, 3);
    } else {
        emitpcode(PC_CMPI, 0, 3, 1);
        output->optype = OpndType_CRField;
        output->reg = 0;
        output->regOffset = EEQU;
    }
}

void SFPE_gen_TYPCON(ENode* expr, short outputReg, short outputRegHi, Operand* output) {
    ENode* inner;
    Type* srctype;
    Type* dsttype;

    inner = expr->data.monadic;
    srctype = inner->rtype;
    dsttype = expr->rtype;

    if (IS_TYPE_VOID(dsttype)) {
        PPCError_Error(PPCERR_FLOAT_CHECKED_AT, __FILE__, 1001);
        GEN_NODE(inner, output);
        output->optype = OpndType_Absolute;
        output->immediate = 0;
    } else if (IS_TYPE_INT_OR_ENUM(srctype)) {
        if (TYPE_IS_SOFT_FLOAT(dsttype) && TYPE_IS_8BYTES(srctype)) {
            GEN_NODE(inner, output);
            coerce_to_register_pair(output, srctype, 0, 0);
            if (output->optype != OpndType_GPRPair) {
                CError_FATAL(1020);
            }
            if (output->regHi != high_reg) {
                emitpcode(PC_MR, high_reg, output->regHi);
            }
            if (output->reg != low_reg) {
                emitpcode(PC_MR, low_reg, output->reg);
            }
            branch_subroutine(
                is_unsigned(srctype) ? (dsttype->size == 4 ? rt_sfpe_f_ulltof : rt_sfpe_d_ulltod)
                                     : (dsttype->size == 4 ? rt_sfpe_f_lltof : rt_sfpe_d_lltod),
                0,
                used_regs_r3_r4
            );
            if (dsttype->size == 4) {
                lift_result(output, OpndType_GPR);
            } else {
                lift_result(output, OpndType_GPRPair);
            }
        } else if (TYPE_IS_SOFT_FLOAT(dsttype)) {
            GEN_NODE(inner, output);
            if (srctype->size < 4) {
                extend32(output, srctype, 0);
            }
            ENSURE_GPR(output, srctype, 0);
            {
                SInt32 reg = output->reg;
                if (reg != 3) {
                    emitpcode(PC_MR, 3, reg);
                }
            }
            branch_subroutine(
                is_unsigned(srctype) ? (dsttype->size == 4 ? rt_sfpe_f_utof : rt_sfpe_d_utod)
                                     : (dsttype->size == 4 ? rt_sfpe_f_itof : rt_sfpe_d_itod),
                0,
                used_regs_r3
            );
            if (dsttype->size == 4) {
                lift_result(output, OpndType_GPR);
            } else {
                lift_result(output, OpndType_GPRPair);
            }
        } else {
            CError_FATAL(1089);
        }
    } else if (IS_TYPE_POINTER(srctype)) {
        CError_FATAL(1098);
    } else if (TYPE_IS_SOFT_FLOAT(dsttype)) {
        if (dsttype->size == 4 && srctype->size != 4) {
            GEN_NODE(inner, output);
            coerce_to_register_pair(output, srctype, 0, 0);
            if (output->optype != OpndType_GPRPair) {
                CError_FATAL(1115);
            }
            if (output->regHi != high_reg) {
                emitpcode(PC_MR, high_reg, output->regHi);
            }
            if (output->reg != low_reg) {
                emitpcode(PC_MR, low_reg, output->reg);
            }
            branch_subroutine(rt_sfpe_d_dtof, 0, used_regs_r3_r4);
            lift_result(output, OpndType_GPR);
        } else if (srctype->size == 4 && dsttype->size != 4) {
            GEN_NODE(inner, output);
            ENSURE_GPR(output, srctype, 0);
            {
                SInt32 reg = output->reg;
                if (reg != 3) {
                    emitpcode(PC_MR, 3, reg);
                }
            }
            branch_subroutine(rt_sfpe_f_ftod, 0, used_regs_r3);
            lift_result(output, OpndType_GPRPair);
        } else {
            if (dsttype->size != srctype->size) {
                CError_FATAL(1152);
            }
            GEN_NODE_TO_REG(inner, outputReg, outputRegHi, output);
            if (srctype->size == 4 && output->optype != OpndType_GPR) {
                ENSURE_GPR(output, srctype, outputReg);
                return;
            }
            if (srctype->size != 4 && output->optype != OpndType_GPRPair) {
                coerce_to_register_pair(output, srctype, outputReg, outputRegHi);
            }
        }
    } else if (TYPE_IS_8BYTES(dsttype) && TYPE_IS_SOFT_FLOAT(srctype) && srctype->size != 4) {
        GEN_NODE(inner, output);
        coerce_to_register_pair(output, srctype, 0, 0);
        if (output->optype != OpndType_GPRPair) {
            CError_FATAL(1175);
        }
        if (output->regHi != high_reg) {
            emitpcode(PC_MR, high_reg, output->regHi);
        }
        if (output->reg != low_reg) {
            emitpcode(PC_MR, low_reg, output->reg);
        }
        if (is_unsigned(expr->rtype)) {
            branch_subroutine(rt_sfpe_d_dtoull, 0, used_regs_r3_r4);
        } else {
            branch_subroutine(rt_sfpe_d_dtoll, 0, used_regs_r3_r4);
        }
        lift_result(output, OpndType_GPRPair);
    } else if (TYPE_IS_8BYTES(dsttype) && TYPE_IS_SOFT_FLOAT(srctype) && srctype->size == 4) {
        GEN_NODE(inner, output);
        ENSURE_GPR(output, srctype, 0);
        {
            SInt32 reg = output->reg;
            if (reg != 3) {
                emitpcode(PC_MR, 3, reg);
            }
        }
        if (is_unsigned(expr->rtype)) {
            branch_subroutine(rt_sfpe_f_ftoull, 0, used_regs_r3);
        } else {
            branch_subroutine(rt_sfpe_f_ftoll, 0, used_regs_r3);
        }
        lift_result(output, OpndType_GPRPair);
    } else if (IS_TYPE_INT_OR_ENUM(dsttype) && TYPE_IS_SOFT_FLOAT(srctype) && srctype->size != 4) {
        GEN_NODE(inner, output);
        coerce_to_register_pair(output, srctype, 0, 0);
        if (output->optype != OpndType_GPRPair) {
            CError_FATAL(1231);
        }
        if (output->regHi != high_reg) {
            emitpcode(PC_MR, high_reg, output->regHi);
        }
        if (output->reg != low_reg) {
            emitpcode(PC_MR, low_reg, output->reg);
        }
        if (is_unsigned(expr->rtype)) {
            branch_subroutine(rt_sfpe_d_dtou, 0, used_regs_r3_r4);
        } else {
            branch_subroutine(rt_sfpe_d_dtoi, 0, used_regs_r3_r4);
        }
        lift_result(output, OpndType_GPR);
    } else if (IS_TYPE_INT_OR_ENUM(dsttype) && TYPE_IS_SOFT_FLOAT(srctype) && srctype->size == 4) {
        GEN_NODE(inner, output);
        ENSURE_GPR(output, srctype, 0);
        {
            SInt32 reg = output->reg;
            if (reg != 3) {
                emitpcode(PC_MR, 3, reg);
            }
        }
        if (is_unsigned(expr->rtype)) {
            branch_subroutine(rt_sfpe_f_ftou, 0, used_regs_r3);
        } else {
            branch_subroutine(rt_sfpe_f_ftoi, 0, used_regs_r3);
        }
        lift_result(output, OpndType_GPR);
    } else {
        CError_FATAL(1280);
    }
}
