#include "compiler/Operands.h"
#include "compiler/BE_symbol.h"
#include "compiler/CABI.h"
#include "compiler/CError.h"
#include "compiler/CMachine.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/StackFrame.h"
#include "compiler/TOC.h"
#include "compiler/types.h"

// These doubles are stored as big-endian word pairs (ObjGen_Setup stores them again in the host's
// order at startup).
Float one_point_zero = {0x3FF00000, 0};
// 8 bytes nothing in the original reads (ObjGen_Setup leaves them alone): a single-precision 1.0
// in the first word, presumably one_point_zero for float
Float one_point_zero_single = {0x3F800000, 0}; // NOTE: invented name
Float int_to_float_cc = {0x43300000, 0x80000000};
Float uns_to_float_cc = {0x43300000, 0};

// NOTE: invented names: hiReg, newReg
void load_immediate(short reg, SInt32 value) {
    short hiReg = reg;

    if (!FITS_IN_SHORT(value)) {
        if (copts.optimizationlevel > 1 && LOW_PART(value)) {
            SInt32 newReg = used_virtual_registers[RegClass_GPR]++;
            hiReg = newReg;
        }
        emitpcode(PC_LIS, hiReg, 0, (short)HIGH_PART(value));
        if (LOW_PART(value)) {
            emitpcode(PC_ADDI, reg, hiReg, 0, LOW_PART(value));
        }
    } else {
        emitpcode(PC_LI, reg, value);
    }
}

// load_immediate for code inserted after the current instruction: lis and ori, so the high half
// takes no carry from the low one
void insert_load_immediate_after(short reg, SInt32 value) {
    short hiReg = reg; // NOTE: invented name
    short hi;

    if (!FITS_IN_SHORT(value)) {
        if (copts.optimizationlevel > 1 && LOW_PART(value)) {
            hiReg = used_virtual_registers[RegClass_GPR]++;
        }
        hi = value >> 16;
        emitpcode(PC_LIS, hiReg, 0, hi);
        if (LOW_PART(value)) {
            emitpcode(PC_ORI, reg, hiReg, LOW_PART(value));
        }
    } else {
        emitpcode(PC_LI, reg, value);
    }
}

void set_op_flags(Operand* op, ENode* expr) {
    CError_ASSERT(118, op);

    if (expr) {
        if (expr->type == EINTCONST) {
            op->flags = 0;
            if (expr->flags & ENODE_FLAG_VOLATILE) {
                op->flags |= OpndFlags_Volatile;
            }
            if (expr->flags & ENODE_FLAG_CONST) {
                op->flags |= OpndFlags_Const;
            }
        } else {
            op->flags = CParserIsVolatileExpr(expr) ? OpndFlags_Volatile : 0;
            op->flags |= CParserIsConstExpr(expr) ? OpndFlags_Const : 0;
        }
    } else {
        op->flags = 0;
    }
}

void symbol_operand(Operand* op, Object* obj) {
    memclrw(op, sizeof(Operand));
    op->optype = OpndType_Symbol;
    op->object = obj;
}

void indirect(Operand* op, ENode* expr) {
    switch (op->optype) {
        case OpndType_GPRPair:
            CError_FATAL(163);
        case OpndType_CRField:
        case OpndType_IndirectGPR_ImmOffset:
        case OpndType_IndirectGPR_Indexed:
        case OpndType_IndirectSymbol:
            if (op->optype) {
                Coerce_to_register(op, TYPE(&void_ptr), 0);
            }
        case OpndType_GPR:
            op->immOffset = 0;
            op->object = NULL;
        case OpndType_GPR_ImmOffset:
            op->optype = OpndType_IndirectGPR_ImmOffset;
            set_op_flags(op, expr);
            break;
        case OpndType_GPR_Indexed:
            op->optype = OpndType_IndirectGPR_Indexed;
            set_op_flags(op, expr);
            break;
        case OpndType_Absolute:
            if (FITS_IN_SHORT(op->immediate)) {
                op->reg = 0;
                op->immOffset = op->immediate;
            } else {
                emitpcode(
                    PC_LIS,
                    op->reg = used_virtual_registers[RegClass_GPR]++,
                    0,
                    (short)HIGH_PART(op->immediate)
                );
                op->immOffset = LOW_PART(op->immediate);
            }
            op->object = NULL;
            op->optype = OpndType_IndirectGPR_ImmOffset;
            set_op_flags(op, expr);
            break;
        case OpndType_Symbol:
            op->optype = OpndType_IndirectSymbol;
            set_op_flags(op, expr);
            break;
        default:
            CError_FATAL(215);
    }
}

#define COMBO_OP(a, b) (b + (a * 11))

// NOTE: invented names: swapOp, sumReg
void combine(Operand* opA, Operand* opB, short output_reg, Operand* opOut) {
    Operand* swapOp;
    int sumReg;

    if (opA->optype == OpndType_Symbol || opA->optype == OpndType_IndirectSymbol) {
        coerce_to_addressable(opA);
    }
    if (opB->optype == OpndType_Symbol || opB->optype == OpndType_IndirectSymbol) {
        coerce_to_addressable(opB);
    }

    switch (COMBO_OP(opA->optype, opB->optype)) {
        case COMBO_OP(OpndType_GPR, OpndType_GPR):
            opOut->optype = OpndType_GPR_Indexed;
            opOut->reg = opA->reg;
            opOut->regOffset = opB->reg;
            break;
        case COMBO_OP(OpndType_GPR_ImmOffset, OpndType_GPR_ImmOffset):
            if (FITS_IN_SHORT(opA->immOffset + opB->immOffset) && (!opA->object || !opB->object)) {
                opB->immOffset += opA->immOffset;
                if (!opB->object) {
                    opB->object = opA->object;
                }
            } else {
                sumReg = (output_reg && (output_reg != opB->reg))
                    ? output_reg
                    : used_virtual_registers[RegClass_GPR]++;
                add_immediate(sumReg, opA->reg, opA->object, opA->immOffset);
                opA->reg = sumReg;
            }
        case COMBO_OP(OpndType_GPR, OpndType_GPR_ImmOffset):
            swapOp = opA;
            opA = opB;
            opB = swapOp;
        case COMBO_OP(OpndType_GPR_ImmOffset, OpndType_GPR):
            if (opA->reg == _FP_ || opA->reg == _CALLER_SP_) {
                opOut->optype = OpndType_GPR_Indexed;
                opOut->reg = (output_reg && (output_reg != opB->reg))
                    ? output_reg
                    : used_virtual_registers[RegClass_GPR]++;
                opOut->regOffset = opB->reg;
                add_immediate(opOut->reg, opA->reg, opA->object, LOW_PART(opA->immOffset));
            } else if (opB->reg == _FP_ || opB->reg == _CALLER_SP_) {
                opOut->optype = OpndType_GPR_Indexed;
                opOut->reg = (output_reg && (output_reg != opA->reg))
                    ? output_reg
                    : used_virtual_registers[RegClass_GPR]++;
                opOut->regOffset = opA->reg;
                add_immediate(opOut->reg, opB->reg, opA->object, LOW_PART(opA->immOffset));
            } else if (opA->object) {
                opOut->optype = OpndType_GPR_Indexed;
                opOut->reg = (output_reg && (output_reg != opB->reg))
                    ? output_reg
                    : used_virtual_registers[RegClass_GPR]++;
                opOut->regOffset = opB->reg;
                add_immediate(opOut->reg, opA->reg, opA->object, LOW_PART(opA->immOffset));
            } else {
                opOut->optype = OpndType_GPR_ImmOffset;
                opOut->reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
                opOut->immOffset = opA->immOffset;
                opOut->object = opA->object;
                emitpcode(PC_ADD, opOut->reg, opA->reg, opB->reg);
            }
            break;
        case COMBO_OP(OpndType_GPR, OpndType_GPR_Indexed):
            swapOp = opA;
            opA = opB;
            opB = swapOp;
        case COMBO_OP(OpndType_GPR_Indexed, OpndType_GPR):
            opOut->optype = OpndType_GPR_Indexed;
            opOut->reg = opA->reg;
            opOut->regOffset = (output_reg && (output_reg != opA->reg))
                ? output_reg
                : used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_ADD, opOut->regOffset, opA->regOffset, opB->reg);
            break;
        case COMBO_OP(OpndType_GPR_ImmOffset, OpndType_GPR_Indexed):
            swapOp = opA;
            opA = opB;
            opB = swapOp;
        case COMBO_OP(OpndType_GPR_Indexed, OpndType_GPR_ImmOffset):
            if (opB->object) {
                opOut->optype = OpndType_GPR_Indexed;
                opOut->reg = (output_reg && (output_reg != opB->reg))
                    ? output_reg
                    : used_virtual_registers[RegClass_GPR]++;
                opOut->regOffset = used_virtual_registers[RegClass_GPR]++;
                emitpcode(PC_ADD, opOut->reg, opA->reg, opA->regOffset);
                add_immediate(opOut->regOffset, opB->reg, opB->object, opB->immOffset);
            } else {
                opOut->optype = OpndType_GPR_ImmOffset;
                opOut->immOffset = opB->immOffset;
                opOut->object = opB->object;
                opOut->reg = (output_reg && (output_reg != opB->reg))
                    ? output_reg
                    : used_virtual_registers[RegClass_GPR]++;
                emitpcode(PC_ADD, opOut->reg, opA->reg, opA->regOffset);
                emitpcode(PC_ADD, opOut->reg, opOut->reg, opB->reg);
            }
            break;
        case COMBO_OP(OpndType_GPR_Indexed, OpndType_GPR_Indexed):
            opOut->optype = OpndType_GPR_Indexed;
            opOut->reg = opA->reg;
            opOut->regOffset = (output_reg && (output_reg != opA->regOffset))
                ? output_reg
                : used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_ADD, opOut->regOffset, opB->reg, opB->regOffset);
            emitpcode(PC_ADD, opOut->regOffset, opOut->regOffset, opA->regOffset);
            break;
        case COMBO_OP(OpndType_GPR_ImmOffset, OpndType_Absolute):
            swapOp = opA;
            opA = opB;
            opB = swapOp;
        case COMBO_OP(OpndType_Absolute, OpndType_GPR_ImmOffset):
            if (!opB->object) {
                opOut->optype = OpndType_GPR_ImmOffset;
                opOut->reg = opB->reg;
                opOut->immOffset = opB->immOffset;
                opOut->object = opB->object;
                if (FITS_IN_SHORT(opOut->immOffset + opA->immediate)) {
                    opOut->immOffset += opA->immediate;
                } else {
                    opOut->reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
                    if (!HIGH_PART(opA->immediate)) {
                        emitpcode(PC_ADDI, opOut->reg, opB->reg, 0, LOW_PART(opA->immediate));
                    } else {
                        emitpcode(
                            PC_ADDIS, opOut->reg, opB->reg, 0, (short)HIGH_PART(opA->immediate)
                        );
                        if (FITS_IN_SHORT(opOut->immOffset + LOW_PART(opA->immediate))) {
                            opOut->immOffset += LOW_PART(opA->immediate);
                        } else {
                            emitpcode(PC_ADDI, opOut->reg, opOut->reg, 0, LOW_PART(opA->immediate));
                        }
                    }
                }
                break;
            } else if (opB->object->datatype == DLOCAL &&
                       can_add_displ_to_local(opB->object, opB->immOffset + opA->immediate)) {
                opOut->optype = OpndType_GPR_ImmOffset;
                opOut->object = opB->object;
                opOut->reg = opB->reg;
                opOut->immOffset = LOW_PART(opB->immOffset + opA->immediate);
                break;
            } else {
                opOut->reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
                add_immediate(opOut->reg, opB->reg, opB->object, opB->immOffset);
                opB->optype = OpndType_GPR;
                opB->reg = opOut->reg;
                swapOp = opA;
                opA = opB;
                opB = swapOp;
            }
        case COMBO_OP(OpndType_GPR, OpndType_Absolute):
            swapOp = opA;
            opA = opB;
            opB = swapOp;
        case COMBO_OP(OpndType_Absolute, OpndType_GPR):
            opOut->optype = (LOW_PART(opA->immediate) != 0) ? OpndType_GPR_ImmOffset : OpndType_GPR;
            opOut->immOffset = LOW_PART(opA->immediate);
            opOut->object = NULL;
            if (FITS_IN_SHORT(opA->immediate)) {
                opOut->reg = opB->reg;
            } else {
                opOut->reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
                emitpcode(PC_ADDIS, opOut->reg, opB->reg, 0, (short)HIGH_PART(opA->immediate));
            }
            break;
        case COMBO_OP(OpndType_GPR_Indexed, OpndType_Absolute):
            swapOp = opA;
            opA = opB;
            opB = swapOp;
        case COMBO_OP(OpndType_Absolute, OpndType_GPR_Indexed):
            opOut->optype = OpndType_GPR_Indexed;
            opOut->reg = opB->reg;
            opOut->regOffset = (output_reg && (output_reg != opB->reg))
                ? output_reg
                : used_virtual_registers[RegClass_GPR]++;
            if (!HIGH_PART(opA->immediate)) {
                emitpcode(PC_ADDI, opOut->regOffset, opB->regOffset, 0, LOW_PART(opA->immediate));
            } else {
                emitpcode(
                    PC_ADDIS, opOut->regOffset, opB->regOffset, 0, (short)HIGH_PART(opA->immediate)
                );
                if (LOW_PART(opA->immediate)) {
                    emitpcode(
                        PC_ADDI, opOut->regOffset, opOut->regOffset, 0, LOW_PART(opA->immediate)
                    );
                }
            }
            break;
        case COMBO_OP(OpndType_Absolute, OpndType_Absolute):
            opOut->optype = OpndType_Absolute;
            opOut->immediate = opA->immediate + opB->immediate;
            break;
        default:
            CError_FATAL(415);
    }
}

// NOTE: invented names: isIndirect, hiReg
void coerce_to_addressable(Operand* op) {
    UInt32 offset;
    short reg;
    short isIndirect;
    short hiReg;
    Object* obj;

    isIndirect = FALSE;
    obj = op->object;
    hiReg = 0;

    switch (op->optype) {
        case OpndType_GPR:
        case OpndType_GPR_ImmOffset:
        case OpndType_GPR_Indexed:
        case OpndType_GPRPair:
        case OpndType_Absolute:
        case OpndType_VR:
        case OpndType_CRField:
        case OpndType_IndirectGPR_ImmOffset:
        case OpndType_IndirectGPR_Indexed:
            break;
        case OpndType_IndirectSymbol:
            isIndirect = TRUE;
        case OpndType_Symbol:
            if (obj->datatype == DLOCAL) {
                if (!local_is_16bit_offset(obj)) {
                    reg = used_virtual_registers[RegClass_GPR]++;
                    op_absolute_ha(reg, local_base_register(obj), obj, 0, 1);
                    op->optype = OpndType_GPR_ImmOffset;
                    op->reg = reg;
                    op->object = obj;
                } else {
                    op->optype = OpndType_GPR_ImmOffset;
                    op->reg = local_base_register(obj);
                    op->object = obj;
                }
            } else if (obj->datatype == DABSOLUTE) {
                offset = obj->u.address;
                if (FITS_IN_SHORT(offset)) {
                    op->reg = 0;
                    op->immOffset = obj->u.address;
                } else {
                    emitpcode(
                        PC_LIS,
                        op->reg = used_virtual_registers[RegClass_GPR]++,
                        0,
                        (short)HIGH_PART(offset)
                    );
                    op->immOffset = LOW_PART(obj->u.address);
                }
                op->object = obj;
                op->optype = OpndType_GPR_ImmOffset;
            } else {
                if (is_pooled_object(obj)) {
                    // a pooled constant is reached through its pool's anchor register
                    reg = GetPooledObjectRegister(obj);
                } else {
                    // small data is reached through its section's base register (r2 or r13),
                    // other data through its address
                    reg = base_register(obj);
                    if (!is_16bitdata(obj)) {
                        hiReg = used_virtual_registers[RegClass_GPR]++;
                        reg = used_virtual_registers[RegClass_GPR]++;
                        op_absolute_ha(hiReg, 0, obj, 0, 1);
                        add_immediate_lo(reg, hiReg, obj, 0, 1);
                        op->object = NULL;
                    } else if (!reg) {
                        reg = used_virtual_registers[RegClass_GPR]++;
                        add_immediate(reg, 0, obj, 0);
                        op->object = NULL;
                    }
                }
                op->optype = OpndType_GPR_ImmOffset;
                op->reg = reg;
            }
            if (isIndirect) {
                if (op->optype == OpndType_GPR_ImmOffset) {
                    op->optype = OpndType_IndirectGPR_ImmOffset;
                } else {
                    CError_FATAL(518);
                }
            }
            break;
        default:
            CError_FATAL(536);
    }
}

void Coerce_to_register(Operand* op, Type* type, short output_reg) {
    short opcode;
    short hiReg; // NOTE: invented name
    short cond_neg;
    short reg;
    short cond;
    SInt32 offset;
    short bit_offset;
    short bit_size;

    if (TYPE_IS_GPR_PAIR(type)) {
        coerce_to_register_pair(op, type, output_reg, 0);
        return;
    }

    coerce_to_addressable(op);
    switch (op->optype) {
        case OpndType_GPRPair:
            return;
        case OpndType_GPR:
            return;
        case OpndType_GPR_ImmOffset:
            reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
            add_immediate(reg, op->reg, op->object, op->immOffset);
            break;
        case OpndType_GPR_Indexed:
            reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_ADD, reg, op->reg, op->regOffset);
            break;
        case OpndType_Absolute:
            reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
            offset = op->immediate;
            if (FITS_IN_SHORT(offset)) {
                emitpcode(PC_LI, reg, offset);
            } else {
                hiReg = reg;
                if (copts.optimizationlevel > 1 && LOW_PART(offset)) {
                    hiReg = used_virtual_registers[RegClass_GPR]++;
                }
                emitpcode(PC_LIS, hiReg, 0, (short)HIGH_PART(offset));
                if (LOW_PART(offset)) {
                    emitpcode(PC_ADDI, reg, hiReg, 0, LOW_PART(offset));
                }
            }
            break;
        case OpndType_IndirectGPR_ImmOffset:
            reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
            // TODO: fakematch: this case's opcode goes in the CRField case's `bit_offset`; a
            // variable of its own (`opcode`) gives other code
            bit_offset = PC_LWZ;
            if (IS_TYPE_INT(type) || IS_TYPE_ENUM(type)) {
                switch (type->size) {
                    case 1:
                        bit_offset = PC_LBZ;
                        break;
                    case 2:
                        if (is_unsigned(type)) {
                            bit_offset = PC_LHZ;
                        } else {
                            bit_offset = PC_LHA;
                        }
                        break;
                }
            } else {
                CError_ASSERT(
                    635,
                    IS_TYPE_POINTER(type) ||
                        IS_TYPE_4BYTES_MEMBERPOINTER(type) ||
                        (TYPE_IS_SOFT_FLOAT(type) && type->size == 4)
                );
            }
            load_store_register(bit_offset, reg, op->reg, op->object, op->immOffset);
            setpcodeflags(op->flags);
            break;
        case OpndType_IndirectGPR_Indexed:
            reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
            opcode = PC_LWZX;
            if (IS_TYPE_INT(type) || IS_TYPE_ENUM(type)) {
                switch (type->size) {
                    case 1:
                        opcode = PC_LBZX;
                        break;
                    case 2:
                        if (is_unsigned(type)) {
                            opcode = PC_LHZX;
                        } else {
                            opcode = PC_LHAX;
                        }
                        break;
                }
            } else {
                CError_ASSERT(
                    679,
                    IS_TYPE_POINTER(type) ||
                        IS_TYPE_4BYTES_MEMBERPOINTER(type) ||
                        (TYPE_IS_SOFT_FLOAT(type) && type->size == 4)
                );
            }
            emitpcode(opcode, reg, op->reg, op->regOffset);
            setpcodeflags(op->flags);
            break;
        case OpndType_CRField:
            cond_neg = FALSE;
            cond = 0;
            reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_MFCR, reg);
            switch (op->regOffset) {
                case ENOTEQU:
                    cond_neg = TRUE;
                case EEQU:
                    cond = 2;
                    break;
                case EGREATEREQU:
                    cond_neg = TRUE;
                case ELESS:
                    cond = 0;
                    break;
                case ELESSEQU:
                    cond_neg = TRUE;
                case EGREATER:
                    cond = 1;
                    break;
                default:
                    CError_FATAL(713);
            }
            // (the bit offset is computed in 16 bits)
            bit_offset = op->reg;
            bit_offset <<= 2;
            bit_offset += cond;
            bit_size = 1;
            emitpcode(PC_RLWINM, reg, reg, (bit_size + bit_offset) & 31, 32 - bit_size, 31);
            if (cond_neg) {
                emitpcode(PC_XORI, reg, reg, 1);
            }
            break;
        default:
            CError_FATAL(724);
    }

    op->optype = OpndType_GPR;
    op->reg = reg;
}

void coerce_to_register_pair(Operand* op, Type* type, short output_reg, short output_regHi) {
    SInt32 offset;
    short reg;
    short regHi;
    short tmp1;
    short tmp2;

    regHi = -1;

    CError_ASSERT(749, TYPE_IS_GPR_PAIR(type) || (IS_TYPE_STRUCT(type) && type->size == 8));

    coerce_to_addressable(op);
    switch (op->optype) {
        case OpndType_GPRPair:
            if (output_reg && !output_regHi) {
                output_regHi = used_virtual_registers[RegClass_GPR]++;
            }
            if (output_regHi && !output_reg) {
                output_reg = used_virtual_registers[RegClass_GPR]++;
            }
            if (op->reg != output_reg || op->regHi != output_regHi) {
                tmp1 = output_reg ? output_reg : op->reg;
                tmp2 = output_regHi ? output_regHi : op->regHi;
                if (tmp1 != op->reg) {
                    if (tmp1 == op->regHi) {
                        CError_ASSERT(773, tmp1 != tmp2);
                        emitpcode(PC_MR, tmp2, op->regHi);
                        emitpcode(PC_MR, tmp1, op->reg);
                    } else {
                        emitpcode(PC_MR, tmp1, op->reg);
                        if (op->regHi != tmp2) {
                            emitpcode(PC_MR, tmp2, op->regHi);
                        }
                    }
                } else if (tmp2 != op->regHi) {
                    if (tmp2 == op->reg) {
                        CError_ASSERT(787, tmp1 != tmp2);
                        emitpcode(PC_MR, tmp1, op->reg);
                        emitpcode(PC_MR, tmp2, op->regHi);
                    } else {
                        emitpcode(PC_MR, tmp2, op->regHi);
                        if (op->reg != tmp1) {
                            emitpcode(PC_MR, tmp1, op->reg);
                        }
                    }
                }
            }
            reg = op->reg;
            regHi = op->regHi;
            break;
        case OpndType_GPR:
            CError_FATAL(804);
            break;
        case OpndType_GPR_ImmOffset:
            CError_FATAL(807);
            break;
        case OpndType_GPR_Indexed:
            CError_FATAL(810);
            break;
        case OpndType_Absolute:
            reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
            offset = op->immediate;
            if (FITS_IN_SHORT(offset)) {
                emitpcode(PC_LI, reg, offset);
            } else {
                tmp1 = reg;
                if (copts.optimizationlevel > 1 && LOW_PART(offset)) {
                    tmp1 = used_virtual_registers[RegClass_GPR]++;
                }
                emitpcode(PC_LIS, tmp1, 0, (short)HIGH_PART(offset));
                if (LOW_PART(offset)) {
                    emitpcode(PC_ADDI, reg, tmp1, 0, LOW_PART(offset));
                }
            }
            regHi = output_regHi ? output_regHi : used_virtual_registers[RegClass_GPR]++;
            if (is_unsigned(type) || offset >= 0) {
                load_immediate(regHi, 0);
            } else {
                load_immediate(regHi, -1);
            }
            break;
        case OpndType_IndirectGPR_ImmOffset:
            reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
            regHi = output_regHi ? output_regHi : used_virtual_registers[RegClass_GPR]++;
            if (op->reg == regHi) {
                if (op->reg == reg) {
                    CError_FATAL(842);
                } else {
                    load_store_register(
                        PC_LWZ, reg, op->reg, op->object, op->immOffset + low_offset
                    );
                    setpcodeflags(op->flags);
                    load_store_register(
                        PC_LWZ, regHi, op->reg, op->object, op->immOffset + high_offset
                    );
                    setpcodeflags(op->flags);
                }
            } else {
                load_store_register(
                    PC_LWZ, regHi, op->reg, op->object, op->immOffset + high_offset
                );
                setpcodeflags(op->flags);
                load_store_register(PC_LWZ, reg, op->reg, op->object, op->immOffset + low_offset);
                setpcodeflags(op->flags);
            }
            break;
        case OpndType_IndirectGPR_Indexed:
            reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
            regHi = output_regHi ? output_regHi : used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_ADD, reg, op->reg, op->regOffset);
            load_store_register(PC_LWZ, regHi, reg, NULL, high_offset);
            setpcodeflags(op->flags);
            load_store_register(PC_LWZ, reg, reg, NULL, low_offset);
            setpcodeflags(op->flags);
            break;
        default:
            CError_FATAL(867);
    }

    if (regHi == -1) {
        CError_FATAL(871);
    } else {
        op->optype = OpndType_GPRPair;
        op->reg = reg;
        op->regHi = regHi;
    }
}

void Coerce_to_fp_register(Operand* op, Type* type, short output_reg) {
    short reg;

    if (TYPE_IS_SOFT_FLOAT(type)) {
        PPCError_ErrorTerm(PPCERR_NO_HARDWARE_FLOAT);
    }

    coerce_to_addressable(op);

    switch (op->optype) {
        case OpndType_FPR:
            reg = op->reg;
            break;
        case OpndType_IndirectGPR_ImmOffset:
            reg = output_reg ? output_reg : used_virtual_registers[RegClass_FPR]++;
            if (IS_TYPE_VEC2X32FLOAT(type)) {
                // a paired single (psq_l with GQR0)
                gecko_load_store_register(reg, op->reg, op->object, op->immOffset, 0, SPR_GQR0, 1);
            } else {
                load_store_register(
                    (type->size == 4) ? PC_LFS : PC_LFD, reg, op->reg, op->object, op->immOffset
                );
            }
            setpcodeflags(op->flags);
            break;
        case OpndType_IndirectGPR_Indexed:
            reg = output_reg ? output_reg : used_virtual_registers[RegClass_FPR]++;
            if (IS_TYPE_VEC2X32FLOAT(type)) {
                emitpcode(PC_PSQ_LX, reg, op->reg, op->regOffset, 0, SPR_GQR0);
            } else {
                emitpcode((type->size == 4) ? PC_LFSX : PC_LFDX, reg, op->reg, op->regOffset);
            }
            setpcodeflags(op->flags);
            break;
        default:
            CError_FATAL(939);
    }

    op->optype = OpndType_FPR;
    op->reg = reg;
}

void Coerce_to_v_register(Operand* op, Type* type, short output_reg) {
    short reg;
    TypeStruct* tstruct;

    coerce_to_addressable(op);

    switch (op->optype) {
        case OpndType_VR:
            reg = op->reg;
            break;
        case OpndType_IndirectGPR_ImmOffset:
            reg = output_reg ? output_reg : used_virtual_registers[RegClass_VR]++;
            load_store_register(PC_LVX, reg, op->reg, op->object, op->immOffset);
            setpcodeflags(op->flags);
            break;
        case OpndType_IndirectGPR_Indexed:
            reg = output_reg ? output_reg : used_virtual_registers[RegClass_VR]++;
            emitpcode(PC_LVX, reg, op->reg, op->regOffset);
            setpcodeflags(op->flags);
            break;
        case OpndType_Absolute:
            tstruct = TYPE_STRUCT(type);
            reg = output_reg ? output_reg : used_virtual_registers[RegClass_VR]++;
            switch (tstruct->stype) {
                case STRUCT_VECTOR_UCHAR:
                case STRUCT_VECTOR_SCHAR:
                case STRUCT_VECTOR_BCHAR:
                    emitpcode(PC_VSPLTISB, reg, op->immediate);
                    break;
                case STRUCT_VECTOR_USHORT:
                case STRUCT_VECTOR_SSHORT:
                case STRUCT_VECTOR_BSHORT:
                case STRUCT_VECTOR_PIXEL:
                    emitpcode(PC_VSPLTISH, reg, op->immediate);
                    break;
                case STRUCT_VECTOR_UINT:
                case STRUCT_VECTOR_SINT:
                case STRUCT_VECTOR_BINT:
                case STRUCT_VECTOR_FLOAT:
                    emitpcode(PC_VSPLTISW, reg, op->immediate);
                    break;
                default:
                    CError_FATAL(1002);
            }
            op->optype = OpndType_VR;
            op->reg = reg;
            setpcodeflags(op->flags);
            break;
        default:
            CError_FATAL(1012);
    }

    op->optype = OpndType_VR;
    op->reg = reg;
}

// Stores a Gekko paired single (psq_st / psq_stx with the quantization register GQR0)
void vectorstore(short reg, Operand* op, Type* type) {
    coerce_to_addressable(op);
    if (IS_TYPE_VEC2X32FLOAT(type)) {
        switch (op->optype) {
            case OpndType_IndirectGPR_ImmOffset:
                gecko_load_store_register(reg, op->reg, op->object, op->immOffset, 0, SPR_GQR0, 0);
                setpcodeflags(op->flags);
                break;
            case OpndType_IndirectGPR_Indexed:
                emitpcode(PC_PSQ_STX, reg, op->reg, op->regOffset, 0, SPR_GQR0);
                setpcodeflags(op->flags);
                break;
            default:
                CError_FATAL(1084);
        }
    } else {
        CError_FATAL(1089);
    }
}

void store(short reg, Operand* op, Type* type) {
    short opcode;

    coerce_to_addressable(op);
    switch (op->optype) {
        case OpndType_IndirectGPR_ImmOffset:
            opcode = PC_STW;
            if (IS_TYPE_INT(type) || IS_TYPE_ENUM(type)) {
                switch (type->size) {
                    case 1:
                        opcode = PC_STB;
                        break;
                    case 2:
                        opcode = PC_STH;
                        break;
                }
            } else {
                CError_ASSERT(
                    1119,
                    IS_TYPE_POINTER(type) ||
                        IS_TYPE_4BYTES_MEMBERPOINTER(type) ||
                        (TYPE_IS_SOFT_FLOAT(type) && type->size == 4)
                );
            }
            load_store_register(opcode, reg, op->reg, op->object, op->immOffset);
            setpcodeflags(op->flags);
            break;
        case OpndType_IndirectGPR_Indexed:
            opcode = PC_STWX;
            if (IS_TYPE_INT(type) || IS_TYPE_ENUM(type)) {
                switch (type->size) {
                    case 1:
                        opcode = PC_STBX;
                        break;
                    case 2:
                        opcode = PC_STHX;
                        break;
                }
            } else {
                CError_ASSERT(
                    1136,
                    IS_TYPE_POINTER(type) ||
                        IS_TYPE_4BYTES_MEMBERPOINTER(type) ||
                        (TYPE_IS_SOFT_FLOAT(type) && type->size == 4)
                );
            }
            emitpcode(opcode, reg, op->reg, op->regOffset);
            setpcodeflags(op->flags);
            break;
        default:
            CError_FATAL(1141);
    }
}

void store_pair(short reg, short regHi, Operand* op, Type* type) {
    short addrReg; // NOTE: invented name

    CError_ASSERT(1156, TYPE_IS_GPR_PAIR(type));

    coerce_to_addressable(op);
    switch (op->optype) {
        case OpndType_IndirectGPR_ImmOffset:
            load_store_register(PC_STW, reg, op->reg, op->object, op->immOffset + low_offset);
            setpcodeflags(op->flags);
            load_store_register(PC_STW, regHi, op->reg, op->object, op->immOffset + high_offset);
            setpcodeflags(op->flags);
            break;
        case OpndType_IndirectGPR_Indexed:
            addrReg = used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_ADD, addrReg, op->reg, op->regOffset);
            load_store_register(PC_STW, reg, addrReg, NULL, low_offset);
            setpcodeflags(op->flags);
            load_store_register(PC_STW, regHi, addrReg, NULL, high_offset);
            setpcodeflags(op->flags);
            break;
        default:
            CError_FATAL(1176);
    }
}

void store_fp(short reg, Operand* op, Type* type) {
    if (TYPE_IS_SOFT_FLOAT(type)) {
        PPCError_ErrorTerm(PPCERR_NO_HARDWARE_FLOAT);
    }

    coerce_to_addressable(op);
    switch (op->optype) {
        case OpndType_IndirectGPR_ImmOffset:
            load_store_register(
                (type->size == 4) ? PC_STFS : PC_STFD, reg, op->reg, op->object, op->immOffset
            );
            setpcodeflags(op->flags);
            break;
        case OpndType_IndirectGPR_Indexed:
            emitpcode((type->size == 4) ? PC_STFSX : PC_STFDX, reg, op->reg, op->regOffset);
            setpcodeflags(op->flags);
            break;
        default:
            CError_FATAL(1207);
    }
}

void store_v(short reg, Operand* op, Type* type) {
    coerce_to_addressable(op);
    switch (op->optype) {
        case OpndType_IndirectGPR_ImmOffset:
            load_store_register(PC_STVX, reg, op->reg, op->object, op->immOffset);
            setpcodeflags(op->flags);
            break;
        case OpndType_IndirectGPR_Indexed:
            emitpcode(PC_STVX, reg, op->reg, op->regOffset);
            setpcodeflags(op->flags);
            break;
        default:
            CError_FATAL(1231);
    }
}

// Whether the last instruction already extends OP's register: an rlwinm by 0 masked MB..ME, or
// the extension OPCODE (an extsh also accepts an extsb)
static Boolean last_matches_rlwinm_or_exts(Operand* op, short opcode, short mb, short me) {
    PCode* pc;

    if (pclastblock->pcodeCount <= 0) {
        return FALSE;
    }

    pc = pclastblock->lastPCode;
    if (pc->args[0].kind != PCOp_REGISTER ||
        pc->args[0].arg != RegClass_GPR ||
        pc->args[0].data.reg.reg != op->reg) {
        return FALSE;
    }

    if (pc->op != opcode && (opcode != PC_EXTSH || pc->op != PC_EXTSB)) {
        return FALSE;
    }

    if (opcode == PC_RLWINM) {
        if (pc->args[2].data.imm.value != 0 ||
            pc->args[3].data.imm.value != mb ||
            pc->args[4].data.imm.value != me) {
            return FALSE;
        }
    }

    return TRUE;
}

void extend32(Operand* op, Type* type, short output_reg) {
    int loaded;
    int reg;
    SInt32 tmp; // (each result register goes through this copy)

    loaded = op->optype >= OpndType_IndirectGPR_ImmOffset;
    if (op->optype != OpndType_GPR) {
        Coerce_to_register(op, type, output_reg);
    }

    switch (type->size) {
        case 1:
            if (is_unsigned(type)) {
                if (loaded) {
                    return;
                }
                if (last_matches_rlwinm_or_exts(op, PC_RLWINM, 24, 31)) {
                    return;
                }
                tmp = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
                reg = tmp;
                emitpcode(PC_RLWINM, tmp, op->reg, 0, 24, 31);
            } else {
                tmp = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
                reg = tmp;
                if (last_matches_rlwinm_or_exts(op, PC_EXTSB, 0, 0)) {
                    return;
                }
                emitpcode(PC_EXTSB, tmp, op->reg);
            }
            break;
        case 2:
            if (loaded) {
                return;
            }
            if (is_unsigned(type)) {
                if (last_matches_rlwinm_or_exts(op, PC_RLWINM, 16, 31)) {
                    return;
                }
                tmp = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
                reg = tmp;
                emitpcode(PC_RLWINM, tmp, op->reg, 0, 16, 31);
            } else {
                if (last_matches_rlwinm_or_exts(op, PC_EXTSH, 0, 0)) {
                    return;
                }
                tmp = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
                reg = tmp;
                emitpcode(PC_EXTSH, tmp, op->reg);
            }
            break;
        default:
            CError_FATAL(1337);
    }

    op->optype = OpndType_GPR;
    op->reg = reg;
}

void extend64(Operand* op, Type* type, short output_reg, short output_regHi) {
    short regHi;
    short loReg; // NOTE: invented name

    if (op->optype != OpndType_GPR) {
        Coerce_to_register(op, type, output_reg);
    }

    regHi = output_regHi ? output_regHi : used_virtual_registers[RegClass_GPR]++;
    if (regHi == op->reg) {
        loReg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
        emitpcode(PC_MR, loReg, op->reg);
        op->reg = loReg;
    }

    if (is_unsigned(type)) {
        load_immediate(regHi, 0);
    } else {
        emitpcode(PC_SRAWI, regHi, op->reg, 31);
    }

    op->optype = OpndType_GPRPair;
    op->regHi = regHi;
}

// NOTE: invented names: baseOp, offsetOp
void load_floating_constant(short reg, Type* type, Float* data) {
    Object* object;
    Operand op1;
    Operand baseOp;
    Operand offsetOp;
    SInt32 offset = 0;

    memclrw(&op1, sizeof(Operand));
    object = createfloatconstant(type, data);
    symbol_operand(&op1, object);

    if (offset) {
        baseOp = op1;
        memclrw(&offsetOp, sizeof(Operand));
        offsetOp.optype = OpndType_Absolute;
        offsetOp.immediate = offset;
        if (baseOp.optype != OpndType_GPR) {
            Coerce_to_register(&baseOp, TYPE(&void_ptr), 0);
        }
        combine(&baseOp, &offsetOp, 0, &op1);
    }

    indirect(&op1, NULL);
    if (op1.optype != OpndType_FPR) {
        Coerce_to_fp_register(&op1, type, reg);
    }
}

void convert_integer_to_floating(Operand* op, Boolean is_single, short output_reg) {
    Float d;
    Operand temp_op;
    int const_reg;
    int word_reg; // NOTE: invented name
    int work_reg;
    int result_reg;
    short opcode;

    symbol_operand(&temp_op, maketemporary(TYPE(&stdouble)));
    coerce_to_addressable(&temp_op);
    d = int_to_float_cc;

    load_floating_constant(const_reg = used_virtual_registers[RegClass_FPR]++, TYPE(&stdouble), &d);

    word_reg = used_virtual_registers[RegClass_GPR]++;
    emitpcode(PC_XORIS, word_reg, op->reg, 0x8000);
    load_store_register(PC_STW, word_reg, temp_op.reg, temp_op.object, low_offset);

    emitpcode(PC_LIS, word_reg = used_virtual_registers[RegClass_GPR]++, 0, 0x4330);
    load_store_register(PC_STW, word_reg, temp_op.reg, temp_op.object, high_offset);

    load_store_register(
        PC_LFD, work_reg = used_virtual_registers[RegClass_FPR]++, temp_op.reg, temp_op.object, 0
    );

    result_reg = output_reg ? output_reg : used_virtual_registers[RegClass_FPR]++;
    if (is_single != 0) {
        opcode = PC_FSUBS;
    } else {
        opcode = PC_FSUB;
    }
    emitpcode(opcode, result_reg, work_reg, const_reg);

    op->optype = OpndType_FPR;
    op->reg = result_reg;
}

void convert_unsigned_to_floating(Operand* op, Boolean is_single, short output_reg) {
    Float d;
    Operand temp_op;
    int const_reg;
    int word_reg; // NOTE: invented name
    int work_reg;
    int result_reg;
    short opcode;

    symbol_operand(&temp_op, maketemporary(TYPE(&stdouble)));
    coerce_to_addressable(&temp_op);
    d = uns_to_float_cc;

    load_floating_constant(const_reg = used_virtual_registers[RegClass_FPR]++, TYPE(&stdouble), &d);

    load_store_register(PC_STW, op->reg, temp_op.reg, temp_op.object, low_offset);

    emitpcode(PC_LIS, word_reg = used_virtual_registers[RegClass_GPR]++, 0, 0x4330);
    load_store_register(PC_STW, word_reg, temp_op.reg, temp_op.object, high_offset);

    load_store_register(
        PC_LFD, work_reg = used_virtual_registers[RegClass_FPR]++, temp_op.reg, temp_op.object, 0
    );

    result_reg = output_reg ? output_reg : used_virtual_registers[RegClass_FPR]++;
    if (is_single != 0) {
        opcode = PC_FSUBS;
    } else {
        opcode = PC_FSUB;
    }
    emitpcode(opcode, result_reg, work_reg, const_reg);

    op->optype = OpndType_FPR;
    op->reg = result_reg;
}

void convert_floating_to_integer(Operand* op, short output_reg) {
    Operand temp_op;
    int converted_reg; // NOTE: invented name
    int result_reg;

    symbol_operand(&temp_op, maketemporary(TYPE(&stdouble)));
    coerce_to_addressable(&temp_op);

    converted_reg = used_virtual_registers[RegClass_FPR]++;
    emitpcode(PC_FCTIWZ, converted_reg, op->reg);
    load_store_register(PC_STFD, converted_reg, temp_op.reg, temp_op.object, 0);

    result_reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
    load_store_register(PC_LWZ, result_reg, temp_op.reg, temp_op.object, low_offset);

    op->optype = OpndType_GPR;
    op->reg = result_reg;
}

void convert_floating_to_unsigned(Operand* op, short output_reg) {
    static UInt32 used_regs[RegClassMax] = {0, 0, 0, 2, 0};

    if ((int)op->reg != 1) {
        emitpcode(PC_FMR, 1, op->reg);
    }

    branch_subroutine(rt_cvt_fp2unsigned, 0, used_regs);

    op->optype = OpndType_GPR;
    op->reg = used_virtual_registers[RegClass_GPR]++;
    emitpcode(PC_MR, op->reg, 3);
}

// NOTE: invented names: rotated_reg, reversed
void extract_bitfield(
    Operand* input_op, TypeBitfield* tbitfield, short output_reg, Operand* output_op
) {
    int start;
    int offset;
    int rotated_reg;
    int output;
    TypeBitfield reversed;

    offset = tbitfield->bitlength;
    output = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
    if (copts.littleendian) {
        reversed = *tbitfield;
        tbitfield = &reversed;
        CABI_ReverseBitField(&reversed);
    }
    start = tbitfield->offset + (32 - (tbitfield->bitfieldtype->size * 8));
    if (is_unsigned(tbitfield->bitfieldtype)) {
        emitpcode(PC_RLWINM, output, input_op->reg, (start + offset) & 31, 32 - offset, 31);
    } else if (start == 0) {
        emitpcode(PC_SRAWI, output, input_op->reg, 32 - offset);
    } else {
        rotated_reg = used_virtual_registers[RegClass_GPR]++;
        emitpcode(PC_RLWINM, rotated_reg, input_op->reg, start & 31, 0, offset);
        emitpcode(PC_SRAWI, output, rotated_reg, 32 - offset);
    }

    output_op->optype = OpndType_GPR;
    output_op->reg = output;
}

void insert_bitfield(short reg, Operand* op, TypeBitfield* tbitfield) {
    TypeBitfield reversed; // NOTE: invented name
    int start = (32 - (tbitfield->bitfieldtype->size * 8)) + tbitfield->offset;
    int offset = tbitfield->bitlength;

    if (copts.littleendian) {
        reversed = *tbitfield;
        CABI_ReverseBitField(&reversed);
        start = (32 - (reversed.bitfieldtype->size * 8)) + reversed.offset;
    }
    emitpcode(PC_RLWIMI, op->reg, reg, 32 - (start + offset), start, start + offset - 1);
}

void load_address(short dest_reg, Operand* op) {
    coerce_to_addressable(op);
    if (op->optype == OpndType_IndirectGPR_ImmOffset) {
        if (op->immOffset == 0 && op->object == NULL) {
            if (op->reg != dest_reg) {
                emitpcode(PC_MR, dest_reg, op->reg);
            }
        } else {
            add_immediate(dest_reg, op->reg, op->object, (short)op->immOffset);
        }
    } else if (op->optype == OpndType_IndirectGPR_Indexed) {
        emitpcode(PC_ADD, dest_reg, op->reg, op->regOffset);
    } else {
        CError_FATAL(1797);
    }
}

// extract_bitfield for a 64-bit bitfield (input is a GPR pair)
// TODO: not exact: the original keeps `lo1`, `lo2` and `sh` in memory, updates `bitlength` where
// the statement is, and tail-merges the first signed path's srawi into its sibling
void I8_extract_bitfield(
    Operand* input, TypeBitfield* tbitfield, short output_reg, short output_regHi, Operand* output
) {
    SInt32 bitlength;
    int reg;
    SInt32 offset;
    int end;
    int tmp;
    SInt32 lo1;
    SInt32 lo;
    SInt32 hi;
    SInt32 sh;
    int regHi;
    int lo2;
    TypeBitfield copy;

    bitlength = tbitfield->bitlength;
    reg = output_reg ? output_reg : used_virtual_registers[RegClass_GPR]++;
    regHi = output_regHi ? output_regHi : used_virtual_registers[RegClass_GPR]++;
    if (copts.littleendian) {
        copy = *tbitfield;
        tbitfield = &copy;
        CABI_ReverseBitField(&copy);
    }
    CError_ASSERT(1823, TYPE_IS_8BYTES(tbitfield->bitfieldtype));

    offset = tbitfield->offset;
    end = offset + bitlength;
    if (end <= 32) {
        if (is_unsigned(tbitfield->bitfieldtype)) {
            emitpcode(PC_LI, regHi, 0);
            emitpcode(PC_RLWINM, reg, input->regHi, end & 31, 32 - bitlength, 31);
            goto done;
        }
        if (offset == 0) {
            emitpcode(PC_SRAWI, reg, input->regHi, 32 - bitlength);
        } else {
            tmp = used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_RLWINM, tmp, input->regHi, offset & 31, 0, bitlength);
            emitpcode(PC_SRAWI, reg, tmp, 32 - bitlength);
        }
    } else if (offset < 32 && end > 32) {
        if (bitlength <= 32) {
            lo1 = 32 - offset;
            hi = bitlength - lo1;
            if (is_unsigned(tbitfield->bitfieldtype)) {
                emitpcode(PC_LI, regHi, 0);
                emitpcode(PC_RLWINM, reg, input->regHi, hi, 32 - bitlength, 31 - hi);
                emitpcode(PC_RLWIMI, reg, input->reg, hi, 32 - hi, 31);
                goto done;
            }
            tmp = used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_RLWINM, tmp, input->regHi, offset & 31, 0, lo1);
            emitpcode(PC_SRAWI, reg, tmp, 32 - lo1);
        } else {
            lo = 32 - (end - 32);
            lo2 = lo;
            bitlength -= 32;
            sh = 32 - lo2;
            if (end == 64) {
                CError_ASSERT(1882, lo2 == 0);
                if (is_unsigned(tbitfield->bitfieldtype)) {
                    emitpcode(PC_RLWINM, regHi, input->regHi, 0, offset, 31);
                    emitpcode(PC_MR, reg, input->reg);
                } else {
                    tmp = used_virtual_registers[RegClass_GPR]++;
                    emitpcode(PC_RLWINM, tmp, input->regHi, offset & 31, 0, bitlength);
                    emitpcode(PC_SRAWI, regHi, tmp, 32 - bitlength);
                    emitpcode(PC_MR, reg, input->reg);
                }
            } else {
                CError_ASSERT(1894, lo2 != 0);
                if (is_unsigned(tbitfield->bitfieldtype)) {
                    tmp = (offset + bitlength) & 31;
                    emitpcode(PC_RLWINM, regHi, input->regHi, tmp, 32 - bitlength, 31);
                    emitpcode(PC_RLWINM, reg, input->regHi, tmp, 0, lo2);
                    emitpcode(PC_RLWIMI, reg, input->reg, sh & 31, lo, 31);
                } else {
                    emitpcode(PC_RLWINM, regHi, input->regHi, offset & 31, 0, bitlength);
                    emitpcode(PC_SRAWI, regHi, regHi, 32 - bitlength);
                    emitpcode(PC_RLWINM, reg, input->regHi, (offset + bitlength) & 31, 0, lo2);
                    emitpcode(PC_RLWIMI, reg, input->reg, sh & 31, lo, 31);
                }
            }
            goto done;
        }
    } else {
        tmp = offset - 32;
        if (is_unsigned(tbitfield->bitfieldtype)) {
            emitpcode(PC_LI, regHi, 0);
            emitpcode(PC_RLWINM, reg, input->reg, (tmp + bitlength) & 31, 32 - bitlength, 31);
            goto done;
        }
        // the original's test: `offset` is 32 or more here, so the srawi-only path is dead;
        // `tmp == 0` was probably meant
        if (offset == 0) {
            emitpcode(PC_SRAWI, reg, input->reg, 32 - bitlength);
        } else {
            int rotatedReg = used_virtual_registers[RegClass_GPR]++; // NOTE: invented name
            emitpcode(PC_RLWINM, rotatedReg, input->reg, tmp & 31, 0, bitlength);
            emitpcode(PC_SRAWI, reg, rotatedReg, 32 - bitlength);
        }
    }
    emitpcode(PC_SRAWI, regHi, reg, 31);
done:
    output->optype = OpndType_GPRPair;
    output->reg = reg;
    output->regHi = regHi;
}

// insert_bitfield for a 64-bit bitfield (op is a GPR pair)
void I8_insert_bitfield(short reg, short regHi, Operand* op, TypeBitfield* tbitfield) {
    int bitlength;
    int offset;
    int hiBits;
    SInt32 shift;
    int shift2;
    int loBits;
    int end;
    TypeBitfield copy;

    bitlength = tbitfield->bitlength;
    if (copts.littleendian) {
        copy = *tbitfield;
        tbitfield = &copy;
        CABI_ReverseBitField(&copy);
    }
    CError_ASSERT(
        1954,
        (tbitfield->bitfieldtype->type == TYPEINT || tbitfield->bitfieldtype->type == TYPEENUM) &&
            tbitfield->bitfieldtype->size == 8
    );

    offset = tbitfield->offset;
    end = offset + bitlength;
    if (end <= 32) {
        emitpcode(PC_RLWIMI, op->regHi, reg, 32 - end, offset, end - 1);
    } else if (offset < 32 && end > 32) {
        if (bitlength <= 32) {
            bitlength -= 32 - offset;
            emitpcode(PC_RLWIMI, op->regHi, reg, 32 - bitlength, offset, 31);
            emitpcode(PC_RLWIMI, op->reg, reg, 32 - bitlength, 0, bitlength - 1);
        } else {
            hiBits = end - 32;
            shift = 32 - hiBits;
            shift2 = shift;
            loBits = bitlength - 32;
            if (end == 64) {
                CError_ASSERT(1972, shift2 == 0);
                emitpcode(PC_RLWIMI, op->regHi, regHi, 0, offset, 31);
                emitpcode(PC_MR, op->reg, reg);
            } else {
                CError_ASSERT(1977, shift2 != 0);
                emitpcode(PC_RLWIMI, op->regHi, regHi, 32 - shift2, offset, offset + loBits - 1);
                emitpcode(PC_RLWIMI, op->regHi, reg, shift2, 32 - shift2, 31);
                emitpcode(PC_RLWIMI, op->reg, reg, shift, 0, hiBits - 1);
            }
        }
    } else {
        offset -= 32;
        end = offset + bitlength;
        emitpcode(PC_RLWIMI, op->reg, reg, 32 - end, offset, end - 1);
    }
}
