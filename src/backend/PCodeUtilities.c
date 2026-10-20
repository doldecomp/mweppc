#include "compiler/PCodeUtilities.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CodeGen.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Registers.h"
#include "compiler/StackFrame.h"

void pcsetrecordbit(PCode* pc) {
    int reg;
    PCodeArg* arg;
    int argIdx;
    short argCount;

    pc->flags &= ~(fIsMove | fCommutative | fIsCSE);
    if ((pc->flags & fOpTypeMask) == fOpTypeFPR) {
        reg = 1;
    } else if ((pc->flags & fOpTypeMask) == fOpTypeVR) {
        reg = 6;
    } else {
        reg = 0;
    }

    if (pc->op == PC_ANDI || pc->op == PC_ANDIS) {
        pc->flags |= fRecordBit;
        return;
    }

    if (pc->op == PC_ADDI || pc->op == PC_ADDIC) {
        pc->flags |= fSetsCarry;
        pc->flags |= fRecordBit;
        change_num_operands(pc, 5);
        pc->op = PC_ADDICR;

        arg = &pc->args[3];
        CError_ASSERT(76, arg->kind == PCOp_PLACEHOLDEROPERAND);
        arg->kind = PCOp_REGISTER;
        arg->arg = RegClass_SPR;
        arg->data.reg.reg = 0;
        arg->data.reg.effect = EffectWrite;
        arg = &pc->args[4];
        CError_ASSERT(80, arg->kind == PCOp_PLACEHOLDEROPERAND);
        arg->kind = PCOp_REGISTER;
        arg->arg = RegClass_CRFIELD;
        arg->data.reg.reg = reg;
        arg->data.reg.effect = EffectWrite;
        return;
    }

    arg = pc->args;
    argIdx = argCount = pc->argCount;
    while (arg->kind != PCOp_PLACEHOLDEROPERAND && argIdx) {
        if (arg->kind == PCOp_REGISTER &&
            arg->arg == RegClass_CRFIELD &&
            arg->data.reg.reg == reg) {
            arg->data.reg.effect |= EffectWrite;
            pc->flags |= fRecordBit;
            return;
        }
        arg++;
        argIdx--;
    }

    if (argIdx <= 0) {
        arg = &pc->args[argCount];
        pc->argCount++;
    }

    CError_ASSERT(105, arg->kind == PCOp_PLACEHOLDEROPERAND);
    arg->kind = PCOp_REGISTER;
    arg->arg = RegClass_CRFIELD;
    arg->data.reg.reg = reg;
    arg->data.reg.effect = EffectWrite;
    if (pc->op != PC_ADDICR) {
        pc->flags |= fRecordBit;
    }
}

void pcsetsideeffects(PCode* pc) {
    pc->flags &= ~(fIsMove | fCommutative | fIsCSE);
    pc->flags |= fSideEffects;
}

void pcsetlinkbit(PCode* pc) {
    PCodeArg* arg;
    int argIdx;

    switch (pc->op) {
        case PC_B:
            pc->op = PC_BL;
            break;
        case PC_BCTR:
            pc->op = PC_BCTRL;
            break;
        case PC_BLR:
            pc->op = PC_BLRL;
            break;
    }

    arg = pc->args;
    argIdx = pc->argCount;
    while (arg->kind != PCOp_PLACEHOLDEROPERAND && argIdx) {
        if (arg->kind == PCOp_REGISTER && arg->arg == RegClass_SPR && arg->data.reg.reg == 1) {
            arg->data.reg.effect |= EffectWrite;
            pc->flags |= fLink;
            return;
        }
        arg++;
        argIdx--;
    }

    CError_ASSERT(169, arg->kind == PCOp_PLACEHOLDEROPERAND);
    arg->kind = PCOp_REGISTER;
    arg->arg = RegClass_SPR;
    arg->data.reg.reg = 1;
    arg->data.reg.effect = EffectWrite;

    if (opcodeinfo[pc->op].flags & fIsCall) {
        pc->flags &= ~fIsBranch;
        pc->flags |= fIsCall;
    }
    pc->flags |= fLink;
}

void branch_label(PCodeLabel* label) {
    if (pclastblock->pcodeCount) {
        pcbranch(pclastblock, label);
        makepcblock();
    }
    pclabel(pclastblock, label);
}

// NOTE: invented names: crfield, sense, crbit, fallthrough
// branches to `label` when CR field `crfield` holds `compareop`'s result (`sense` FALSE: does not
// hold it).
void branch_conditional(short crfield, short compareop, short sense, PCodeLabel* label) {
    PCodeLabel* fallthrough;
    int crbit;

    fallthrough = makepclabel();

    switch (compareop) {
        case ENOTEQU:
            sense = !sense;
        case EEQU:
            crbit = 2;
            break;
        case EGREATEREQU:
            sense = !sense;
        case ELESS:
            crbit = 0;
            break;
        case ELESSEQU:
            sense = !sense;
        case EGREATER:
            crbit = 1;
            break;
    }

    emitpcode(sense ? PC_BT : PC_BF, crfield, crbit, label);
    pcbranch(pclastblock, label);
    pcbranch(pclastblock, fallthrough);
    makepcblock();
    pclabel(pclastblock, fallthrough);
}

void branch_always(PCodeLabel* label) {
    emitpcode(PC_B, label);
    pcbranch(pclastblock, label);
    makepcblock();
}

void branch_decrement_always(Opcode opcode, PCodeLabel* label) {
    PCodeLabel* fallthrough = makepclabel(); // NOTE: invented name
    emitpcode(opcode, label);
    pcbranch(pclastblock, label);
    pcbranch(pclastblock, fallthrough);
    makepcblock();
    pclabel(pclastblock, fallthrough);
}

void branch_indirect(Object* obj) {
    emitpcode(PC_BCTR, obj, 0);
    makepcblock();
}

int branch_count_volatiles(void) {
    int count = 0;
    int i;
    RegClass rclass;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        for (i = 0; n_scratch_registers[rclass] > i; i++) {
            count++;
        }
    }

    return count;
}

PCodeArg* branch_record_volatiles(PCodeArg* arglist, UInt32* masks) {
    int i;
    RegClass rclass;

    for (rclass = RegClassMax - 1; rclass >= 0; rclass--) {
        for (i = 0; i < n_scratch_registers[rclass]; i++) {
            arglist->kind = PCOp_REGISTER;
            arglist->arg = rclass;
            arglist->data.reg.reg = scratch_registers[rclass][i];
            arglist->data.reg.effect = EffectWrite;
            if (masks[rclass] & (1 << scratch_registers[rclass][i])) {
                arglist->data.reg.effect |= EffectRead;
            }
            // cr1 is also read by every call
            if (rclass == RegClass_CRFIELD && arglist->data.reg.reg == 1) {
                arglist->data.reg.effect |= EffectRead;
            }
            arglist++;
        }
    }

    return arglist;
}

void branch_subroutine(Object* obj, short add_nop, UInt32* masks) {
    int count;
    PCode* pc;
    PCodeArg* arg;
    char addressing;

    count = branch_count_volatiles();
    if (copts.exceptions && current_statement) {
        count += countexceptionactionregisters(current_statement->dobjstack);
    }

    // a call to an absolute address (addressing 11) sets fAbsolute; the original tests 10 first
    // though its arm is the default's
    addressing = ObjGen_GetAddressing(obj);
    if (addressing == 10) {
        pc = makepcode(PC_BL, count, obj, 0);
    } else if (addressing == 11) {
        pc = makepcode(PC_BL, count, obj, 0);
        pc->flags |= fAbsolute;
    } else {
        pc = makepcode(PC_BL, count, obj, 0);
    }
    arg = branch_record_volatiles(pc->args + 1, masks);
    if (copts.exceptions && current_statement) {
        noteexceptionactionregisters(current_statement->dobjstack, arg);
    }
    appendpcode(pclastblock, pc);

    if (add_nop) {
        emitpcode(PC_NOP);
    }

    branch_label(makepclabel());
    if (copts.exceptions && current_statement) {
        recordexceptionactions(pc, current_statement->dobjstack);
    }
}

void branch_subroutine_ctr(UInt32* masks) {
    int count;
    PCode* pc;
    PCodeArg* arg;

    count = branch_count_volatiles();
    if (copts.exceptions && current_statement) {
        count += countexceptionactionregisters(current_statement->dobjstack);
    }

    pc = makepcode(PC_BCTRL, count);
    arg = branch_record_volatiles(pc->args + 1, masks);
    if (copts.exceptions && current_statement) {
        noteexceptionactionregisters(current_statement->dobjstack, arg);
    }
    appendpcode(pclastblock, pc);

    branch_label(makepclabel());
    if (copts.exceptions && current_statement) {
        recordexceptionactions(pc, current_statement->dobjstack);
    }
}

void add_immediate(short dest_reg, short base_reg, Object* obj, SInt16 offset) {
    short addr_reg = base_reg; // NOTE: invented name
    PCode* pc;

    // a small-data object is addressed from r2/r13 as an absolute 16-bit address
    if (obj && ((int)base_reg == 2 || (int)base_reg == 13)) {
        CError_ASSERT(534, obj->datatype != DLOCAL);
        addr_reg = 0;
    }

    if (!obj && !offset) {
        emitpcode(PC_MR, dest_reg, addr_reg);
        return;
    }

    emitpcode(PC_ADDI, dest_reg, addr_reg, obj, offset);
    if (obj) {
        if ((int)addr_reg == 0) {
            pc = pclastblock->lastPCode;
            pc->flags = (pc->flags & ~opcodeinfo[pc->op].flags) | opcodeinfo[PC_LI].flags;
            pc->op = PC_LI;
            pc->args[1] = pc->args[2];
            change_num_operands(pc, 2);
        }
    } else {
        CError_ASSERT(557, base_reg != 0);
    }
}

PCode* add_immediate_lo(
    short dest_reg, short base_reg, Object* obj, SInt16 offset, char add_to_block
) {
    PCode* pc;

    CError_ASSERT(577, obj);

    pc = makepcode(PC_ADDI, dest_reg, base_reg, obj, offset);
    if (add_to_block) {
        appendpcode(pclastblock, pc);
    }
    return pc;
}

PCode* op_absolute_ha(
    short dest_reg, short base_reg, Object* obj, short offset, char add_to_block
) {
    PCode* pc;
    int addr_reg; // NOTE: invented name

    if (obj->datatype == DLOCAL) {
        pc = makepcode(PC_ADDIS, dest_reg, base_reg, obj, offset);
    } else if (copts.codegen_pic) {
        addr_reg = base_reg;
        CError_ASSERT(601, addr_reg);
        pc = makepcode(PC_ADDIS, dest_reg, addr_reg, obj, offset);
    } else {
        CError_ASSERT(606, base_reg == 0);
        pc = makepcode(PC_LIS, dest_reg, obj, offset);
    }

    if (add_to_block) {
        appendpcode(pclastblock, pc);
    }
    return pc;
}

void load_store_register(
    Opcode opcode, short dest_reg, short base_reg, Object* obj, SInt32 offset
) {
    short offset_reg1;
    short offset_reg2;

    offset_reg1 = base_reg;
    // a small-data object is addressed from r2/r13 as an absolute 16-bit address
    if (obj &&
        obj->datatype != DLOCAL &&
        is_16bitdata(obj) &&
        ((int)base_reg == 2 || (int)base_reg == 13)) {
        offset_reg1 = 0;
    }

    if (offset != (short)offset) {
        if (opcode == PC_LWZ && dest_reg == 12) {
            offset_reg2 = 12;
        } else if (opcode == PC_LWZ && dest_reg == 11) {
            offset_reg2 = 11;
        } else {
            offset_reg2 = used_virtual_registers[RegClass_GPR]++;
        }

        emitpcode(
            PC_ADDIS, offset_reg2, offset_reg1, 0, (short)((offset >> 16) + ((offset >> 15) & 1))
        );
        offset = (short)offset;
        offset_reg1 = offset_reg2;
    }

    if (opcode == PC_STVX || opcode == PC_LVX) {
        short index_reg;

        index_reg = 0;
        if (obj) {
            short addi_tmp;

            addi_tmp = used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_ADDI, addi_tmp, offset_reg1, obj, offset);
            offset_reg1 = addi_tmp;
        } else if (offset) {
            index_reg = used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_LI, index_reg, offset);
        }
        if (!index_reg) {
            emitpcode(opcode, dest_reg, 0, offset_reg1);
        } else {
            emitpcode(opcode, dest_reg, offset_reg1, index_reg);
        }
    } else {
        emitpcode(opcode, dest_reg, offset_reg1, obj, offset);
    }
}

// Gekko paired singles: psq_l/psq_st when a local's offset fits their 12-bit displacement, else
// psq_lx/psq_stx
Opcode Select_Gecko_Load_Store(Object* obj, int offset, int load) {
    Opcode op;

    if (obj && obj->datatype == DLOCAL) {
        if ((local_offset_32(obj) + offset) >> 11) {
            op = load ? PC_PSQ_LX : PC_PSQ_STX;
        } else {
            op = load ? PC_PSQ_L : PC_PSQ_ST;
        }
    } else {
        op = load ? PC_PSQ_LX : PC_PSQ_STX;
    }
    return op;
}

// Gekko paired singles: load_store_register for psq_l/psq_st and their indexed forms
void gecko_load_store_register(
    short dest_reg, short base_reg, Object* obj, SInt32 offset, SInt32 w, SInt32 i, int load
) {
    Opcode op;
    short offset_reg1;
    short offset_reg2;
    short addi_tmp;
    short opcode;

    offset_reg1 = base_reg;
    op = Select_Gecko_Load_Store(obj, offset, load);
    opcode = op;

    if (obj &&
        obj->datatype != DLOCAL &&
        is_16bitdata(obj) &&
        ((int)base_reg == 2 || (int)base_reg == 13)) {
        offset_reg1 = 0;
    }

    if (opcode == PC_PSQ_L || opcode == PC_PSQ_ST) {
        emitpcode(op, dest_reg, offset_reg1, obj, offset, w, i);
    } else if (opcode == PC_PSQ_LX || opcode == PC_PSQ_STX) {
        offset_reg2 = 0;
        if (obj) {
            addi_tmp = used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_ADDI, addi_tmp, offset_reg1, obj, offset);
            offset_reg1 = addi_tmp;
        } else if (offset) {
            offset_reg2 = used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_LI, offset_reg2, offset);
        }
        if (!offset_reg2) {
            emitpcode(op, dest_reg, 0, offset_reg1, w, i);
        } else {
            emitpcode(op, dest_reg, offset_reg1, offset_reg2, w, i);
        }
    } else {
        CError_FATAL(847);
    }
}
