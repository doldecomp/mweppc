#include "compiler/ConstantPropagation.h"
#include "compiler/Alias.h"
#include "compiler/BitVectors.h"
#include "compiler/CompilerTools.h"
#include "compiler/objects.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/StackFrame.h"
#include "compiler/UseDefChains.h"

#define changed constprop_changed
#define defininginstruction constprop_defininginstruction
#define fprdefininginstruction constprop_fprdefininginstruction
#define vrdefininginstruction constprop_vrdefininginstruction

int propagatedconstants;
int changed;
PCode** defininginstruction;
PCode** fprdefininginstruction;
PCode** vrdefininginstruction;

static void computedefininginstructions(PCodeBlock* block) {
    RegUseOrDef* list;
    PCode* instr;
    int i;

    for (i = 0; i < used_virtual_registers[RegClass_GPR]; i++) {
        for (instr = NULL, list = reg_Defs[RegClass_GPR][i]; list; list = list->next) {
            if (bitvectorgetbit(list->id, usedefinfo[block->blockIndex].defin)) {
                if (instr == NULL) {
                    instr = Defs[list->id].pcode;
                } else {
                    instr = NULL;
                    break;
                }
            }
        }
        defininginstruction[i] = instr;
    }

    for (i = 0; i < used_virtual_registers[RegClass_FPR]; i++) {
        for (instr = NULL, list = reg_Defs[RegClass_FPR][i]; list; list = list->next) {
            if (bitvectorgetbit(list->id, usedefinfo[block->blockIndex].defin)) {
                if (instr == NULL) {
                    instr = Defs[list->id].pcode;
                } else {
                    instr = NULL;
                    break;
                }
            }
        }
        fprdefininginstruction[i] = instr;
    }

    for (i = 0; i < used_virtual_registers[RegClass_VR]; i++) {
        for (instr = NULL, list = reg_Defs[RegClass_VR][i]; list; list = list->next) {
            if (bitvectorgetbit(list->id, usedefinfo[block->blockIndex].defin)) {
                if (instr == NULL) {
                    instr = Defs[list->id].pcode;
                } else {
                    instr = NULL;
                    break;
                }
            }
        }
        vrdefininginstruction[i] = instr;
    }
}

static PCode* isstackoperand(PCodeArg* op, SInt16* resultValue, SInt16 addend) {
    PCode* instr;

    if ((instr = defininginstruction[op->data.reg.reg]) && instr->op == PC_ADDI) {
        if (instr->args[2].kind == PCOp_MEMORY &&
            (instr->args[1].data.reg.reg == _FP_ || instr->args[1].data.reg.reg == _CALLER_SP_) &&
            instr->args[2].data.mem.obj->datatype == DLOCAL) {
            if (can_add_displ_to_local(instr->args[2].data.mem.obj, addend)) {
                *resultValue = instr->args[2].data.mem.offset;
                return instr;
            } else {
                return NULL;
            }
        } else {
            return NULL;
        }
    } else {
        return NULL;
    }
}

static int isconstantoperand(PCodeArg* op, SInt16* resultValue) {
    PCode* instr;

    if ((instr = defininginstruction[op->data.reg.reg]) &&
        instr->op == PC_LI &&
        instr->args[1].kind == PCOp_IMMEDIATE) {
        *resultValue = instr->args[1].data.imm.value;
        return TRUE;
    } else {
        return FALSE;
    }
}

static int isuint16constantoperand(PCodeArg* op, SInt16* resultValue) {
    PCode* instr;

    if ((instr = defininginstruction[op->data.reg.reg]) &&
        instr->op == PC_LI &&
        instr->args[1].kind == PCOp_IMMEDIATE &&
        FITS_IN_USHORT(instr->args[1].data.imm.value)) {
        *resultValue = instr->args[1].data.imm.value;
        return TRUE;
    } else {
        return FALSE;
    }
}

static int isvectorconstantoperand(PCodeArg* op, SInt16* resultValue, short* resultNewOp) {
    PCode* instr;

    if ((instr = vrdefininginstruction[op->data.reg.reg]) &&
        (instr->op == PC_VSPLTISB || instr->op == PC_VSPLTISH || instr->op == PC_VSPLTISW) &&
        instr->args[1].kind == PCOp_IMMEDIATE) {
        *resultValue = instr->args[1].data.imm.value;
        *resultNewOp = instr->op;
        return TRUE;
    } else {
        return FALSE;
    }
}

static int isunsignedloadoperand(PCodeArg* op) {
    PCode* instr;

    if ((instr = defininginstruction[op->data.reg.reg])) {
        if (instr->flags & fIsRead) {
            if (instr->op >= PC_LHZ && instr->op <= PC_LHZUX) {
                return 2;
            }
            if (instr->op >= PC_LBZ && instr->op <= PC_LBZUX) {
                return 1;
            }
        } else if (instr->op == PC_RLWINM) {
            int mb = instr->args[3].data.imm.value;
            int me = instr->args[4].data.imm.value;
            if (me == 31) {
                if (mb == 24) {
                    return 1;
                }
                if (mb == 16) {
                    return 2;
                }
            }
        }
    }

    return 0;
}

static int ismaskedoperand(PCodeArg* op, UInt32* resultMask) {
    PCode* instr;
    UInt32 mask;

    if ((instr = defininginstruction[op->data.reg.reg]) && instr->op == PC_RLWINM) {
        if (instr->args[3].data.imm.value <= instr->args[4].data.imm.value) {
            mask = ((instr->args[3].data.imm.value > 31)
                        ? 0
                        : (0xFFFFFFFFU >> instr->args[3].data.imm.value)) &
                ~(((instr->args[4].data.imm.value + 1) > 31)
                      ? 0
                      : (0xFFFFFFFFU >> (instr->args[4].data.imm.value + 1)));
        } else {
            mask = ((instr->args[3].data.imm.value > 31)
                        ? 0
                        : (0xFFFFFFFFU >> instr->args[3].data.imm.value)) |
                ~(((instr->args[4].data.imm.value + 1) > 31)
                      ? 0
                      : (0xFFFFFFFFU >> (instr->args[4].data.imm.value + 1)));
        }
        *resultMask = mask;
        return TRUE;
    }

    return FALSE;
}

static int issignedloadoperand(PCodeArg* op) {
    PCode* instr;

    if ((instr = defininginstruction[op->data.reg.reg])) {
        if (instr->flags & fIsRead) {
            if (instr->op >= PC_LHA && instr->op <= PC_LHAUX) {
                return 2;
            }
        } else if (instr->op == PC_EXTSB) {
            return 1;
        } else if (instr->op == PC_EXTSH) {
            return 2;
        }
    }

    return 0;
}

static inline int issingleoperand(PCodeArg* op) {
    PCode* instr;

    if ((instr = fprdefininginstruction[op->data.reg.reg])) {
        switch (instr->op) {
            case PC_LFS:
            case PC_LFSU:
            case PC_LFSX:
            case PC_LFSUX:
            case PC_FADDS:
            case PC_FSUBS:
            case PC_FMULS:
            case PC_FDIVS:
            case PC_FMADDS:
            case PC_FMSUBS:
            case PC_FNMADDS:
            case PC_FNMSUBS:
            case PC_FRSP:
            case PC_FSQRTS:
                return TRUE;
        }
    }

    return FALSE;
}

static void propagateconstantstoblock(PCodeBlock* block) {
    PCode* instr;
    SInt16 immAddend;
    SInt16 value1;
    SInt16 valueU16;
    short newOpcode;
    SInt16 value2;
    UInt32 mask;
    int loadSize;
    PCodeArg* op;
    int i;

    for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
        if (instr->flags & fIsVolatile) {
            continue;
        }

        switch (instr->op) {
            case PC_MR:
                if (isconstantoperand(&instr->args[1], &value1)) {
                    change_opcode(instr, PC_LI);
                    instr->args[1].kind = PCOp_IMMEDIATE;
                    instr->args[1].data.imm.value = value1;
                    instr->args[1].data.imm.obj = NULL;
                    changed = propagatedconstants = TRUE;
                }
                break;
            case PC_VMR:
                if (isvectorconstantoperand(&instr->args[1], &value1, &newOpcode)) {
                    change_opcode(instr, newOpcode);
                    instr->args[1].kind = PCOp_IMMEDIATE;
                    instr->args[1].data.imm.value = value1;
                    instr->args[1].data.imm.obj = NULL;
                    changed = propagatedconstants = TRUE;
                }
                break;
            case PC_RLWINM:
                if (!(PCODE_FLAG_SET_F(instr) & fRecordBit) &&
                    instr->args[2].data.imm.value == 0 &&
                    instr->args[4].data.imm.value == 31) {
                    if (isconstantoperand(&instr->args[1], &value1)) {
                        if ((instr->args[3].data.imm.value == 16 && value1 == (value1 & 0x7FFF)) ||
                            (instr->args[3].data.imm.value == 24 && value1 == (value1 & 0xFF))) {
                            change_opcode(instr, PC_LI);
                            instr->args[1].kind = PCOp_IMMEDIATE;
                            instr->args[1].data.imm.value = value1;
                            instr->args[1].data.imm.obj = NULL;
                            change_num_operands(instr, 2);
                            changed = propagatedconstants = TRUE;
                            break;
                        }
                    }

                    loadSize = isunsignedloadoperand(&instr->args[1]);
                    if ((loadSize == 2 && instr->args[3].data.imm.value <= 16) ||
                        (loadSize == 1 && instr->args[3].data.imm.value <= 24)) {
                        change_opcode(instr, PC_MR);
                        change_num_operands(instr, 2);
                        changed = propagatedconstants = TRUE;
                        break;
                    }

                    if (ismaskedoperand(&instr->args[1], &mask)) {
                        if (instr->args[3].data.imm.value <= instr->args[4].data.imm.value) {
                            loadSize = ((instr->args[3].data.imm.value > 31)
                                            ? 0
                                            : (0xFFFFFFFFU >> instr->args[3].data.imm.value)) &
                                ~(((instr->args[4].data.imm.value + 1) > 31)
                                      ? 0
                                      : (0xFFFFFFFFU >> (instr->args[4].data.imm.value + 1)));
                        } else {
                            loadSize = ((instr->args[3].data.imm.value > 31)
                                            ? 0
                                            : (0xFFFFFFFFU >> instr->args[3].data.imm.value)) |
                                ~(((instr->args[4].data.imm.value + 1) > 31)
                                      ? 0
                                      : (0xFFFFFFFFU >> (instr->args[4].data.imm.value + 1)));
                        }
                        if (mask == (mask & loadSize)) {
                            change_opcode(instr, PC_MR);
                            change_num_operands(instr, 2);
                            changed = propagatedconstants = TRUE;
                        }
                    }
                }
                break;

            case PC_EXTSH:
                if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                    break;
                }

                if (isconstantoperand(&instr->args[1], &value1)) {
                    change_opcode(instr, PC_LI);
                    instr->args[1].kind = PCOp_IMMEDIATE;
                    instr->args[1].data.imm.value = value1;
                    instr->args[1].data.imm.obj = NULL;
                    change_num_operands(instr, 2);
                    changed = propagatedconstants = TRUE;
                    break;
                }

                loadSize = issignedloadoperand(&instr->args[1]);
                if (loadSize == 1 || loadSize == 2) {
                    change_opcode(instr, PC_MR);
                    change_num_operands(instr, 2);
                    changed = propagatedconstants = TRUE;
                }
                break;

            case PC_EXTSB:
                if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                    break;
                }

                if (isconstantoperand(&instr->args[1], &value1) &&
                    value1 >= -128 &&
                    value1 <= 127) {
                    change_opcode(instr, PC_LI);
                    instr->args[1].kind = PCOp_IMMEDIATE;
                    instr->args[1].data.imm.value = value1;
                    instr->args[1].data.imm.obj = NULL;
                    change_num_operands(instr, 2);
                    changed = propagatedconstants = TRUE;
                    break;
                }

                loadSize = issignedloadoperand(&instr->args[1]);
                if (loadSize == 1) {
                    change_opcode(instr, PC_MR);
                    change_num_operands(instr, 2);
                    changed = propagatedconstants = TRUE;
                }
                break;

            case PC_FRSP:
                if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                    break;
                }

                if (issingleoperand(&instr->args[1])) {
                    change_opcode(instr, PC_FMR);
                    change_num_operands(instr, 2);
                    changed = propagatedconstants = TRUE;
                }
                break;

            case PC_ADDI:
                if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                    break;
                }
                if (instr->args[2].kind != PCOp_IMMEDIATE) {
                    break;
                }

                immAddend = instr->args[2].data.imm.value;
                if (isconstantoperand(&instr->args[1], &value1) &&
                    FITS_IN_SHORT(immAddend + value1)) {
                    change_opcode(instr, PC_LI);
                    instr->args[1].kind = PCOp_IMMEDIATE;
                    instr->args[1].data.imm.value = immAddend + value1;
                    instr->args[1].data.imm.obj = NULL;
                    change_num_operands(instr, 2);
                    changed = propagatedconstants = TRUE;
                } else if (immAddend == 0) {
                    change_opcode(instr, PC_MR);
                    change_num_operands(instr, 2);
                    changed = propagatedconstants = TRUE;
                }
                break;

            case PC_ADD:
                if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                    break;
                }

                if (isconstantoperand(&instr->args[2], &value1)) {
                    if (value1 == 0) {
                        change_opcode(instr, PC_MR);
                        change_num_operands(instr, 2);
                    } else {
                        change_opcode(instr, PC_ADDI);
                        instr->args[2].kind = PCOp_IMMEDIATE;
                        instr->args[2].data.imm.value = value1;
                        instr->args[2].data.imm.obj = NULL;
                    }
                    changed = propagatedconstants = TRUE;
                    immAddend = value1;
                }

                if (isconstantoperand(&instr->args[1], &value1)) {
                    if (instr->op == PC_ADDI || instr->op == PC_MR) {
                        if (FITS_IN_SHORT(immAddend + value1)) {
                            change_opcode(instr, PC_LI);
                            instr->args[1].kind = PCOp_IMMEDIATE;
                            instr->args[1].data.imm.value = immAddend + value1;
                            instr->args[1].data.imm.obj = NULL;
                            change_num_operands(instr, 2);
                            changed = propagatedconstants = TRUE;
                        }
                    } else {
                        instr->args[1] = instr->args[2];
                        if (value1 == 0) {
                            change_opcode(instr, PC_MR);
                            change_num_operands(instr, 2);
                        } else {
                            change_opcode(instr, PC_ADDI);
                            instr->args[2].kind = PCOp_IMMEDIATE;
                            instr->args[2].data.imm.value = value1;
                            instr->args[2].data.imm.obj = NULL;
                        }
                        changed = propagatedconstants = TRUE;
                    }
                }

                if (changed) {
                    if (instr->op == PC_MR) {
                        PCode* stackInstr;
                        if ((stackInstr = isstackoperand(&instr->args[1], &value1, 0))) {
                            change_opcode(instr, PC_ADDI);
                            instr->flags = stackInstr->flags;
                            instr->args[1] = stackInstr->args[1];
                            instr->args[2] = stackInstr->args[2];
                            change_num_operands(instr, 3);
                            changed = propagatedconstants = TRUE;
                        }
                    } else if (instr->op == PC_ADDI && instr->args[2].kind == PCOp_IMMEDIATE) {
                        PCode* stackInstr;
                        SInt16 addend = instr->args[2].data.imm.value;
                        if ((stackInstr = isstackoperand(&instr->args[1], &value1, addend))) {
                            change_opcode(instr, PC_ADDI);
                            instr->flags = stackInstr->flags;
                            instr->args[1] = stackInstr->args[1];
                            instr->args[2] = stackInstr->args[2];
                            instr->args[2].data.imm.value = value1 + addend;
                            if (instr->flags & (fIsRead | fIsWrite | fOpFromMemory | fOpToMemory)) {
                                instr->alias = make_alias(
                                    instr->args[2].data.imm.obj, instr->args[2].data.imm.value, 1
                                );
                            }
                            changed = propagatedconstants = TRUE;
                        }
                    }
                }
                break;

            case PC_OR:
                if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                    break;
                }

                immAddend = value1 = 0;
                if (isconstantoperand(&instr->args[2], &value1)) {
                    if (isuint16constantoperand(&instr->args[2], &valueU16)) {
                        if (valueU16 != 0) {
                            change_opcode(instr, PC_ORI);
                            instr->args[2].kind = PCOp_IMMEDIATE;
                            instr->args[2].data.imm.value = valueU16;
                            instr->args[2].data.imm.obj = NULL;
                            changed = propagatedconstants = TRUE;
                        } else {
                            change_opcode(instr, PC_MR);
                            change_num_operands(instr, 2);
                            changed = propagatedconstants = TRUE;
                        }
                        value1 = valueU16;
                    } else if (value1 == 0) {
                        change_opcode(instr, PC_MR);
                        change_num_operands(instr, 2);
                        changed = propagatedconstants = TRUE;
                    }
                    immAddend = value1;
                }

                if (isconstantoperand(&instr->args[1], &value1)) {
                    if (instr->op == PC_ORI || instr->op == PC_MR) {
                        change_opcode(instr, PC_LI);
                        instr->args[1].kind = PCOp_IMMEDIATE;
                        instr->args[1].data.imm.value = immAddend | value1;
                        instr->args[1].data.imm.obj = NULL;
                        change_num_operands(instr, 2);
                        changed = propagatedconstants = TRUE;
                    } else if (isuint16constantoperand(&instr->args[1], &valueU16)) {
                        if (valueU16 != 0) {
                            change_opcode(instr, PC_ORI);
                            instr->args[1] = instr->args[2];
                            instr->args[2].kind = PCOp_IMMEDIATE;
                            instr->args[2].data.imm.value = valueU16;
                            instr->args[2].data.imm.obj = NULL;
                            changed = propagatedconstants = TRUE;
                        } else {
                            change_opcode(instr, PC_MR);
                            instr->args[1] = instr->args[2];
                            change_num_operands(instr, 2);
                            changed = propagatedconstants = TRUE;
                        }
                    } else if (value1 == 0) {
                        change_opcode(instr, PC_MR);
                        instr->args[1] = instr->args[2];
                        change_num_operands(instr, 2);
                        changed = propagatedconstants = TRUE;
                    }
                }
                break;

            case PC_SUBF:
                if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                    break;
                }

                if (isconstantoperand(&instr->args[1], &value1) && FITS_IN_SHORT(-value1)) {
                    if (isconstantoperand(&instr->args[2], &value2) &&
                        FITS_IN_SHORT(value2 - value1)) {
                        change_opcode(instr, PC_LI);
                        instr->args[1].kind = PCOp_IMMEDIATE;
                        instr->args[1].data.imm.value = value2 - value1;
                        instr->args[1].data.imm.obj = NULL;
                        change_num_operands(instr, 2);
                    } else if (value1 == 0) {
                        change_opcode(instr, PC_MR);
                        instr->args[1] = instr->args[2];
                        change_num_operands(instr, 2);
                    } else {
                        change_opcode(instr, PC_ADDI);
                        instr->args[1] = instr->args[2];
                        instr->args[2].kind = PCOp_IMMEDIATE;
                        instr->args[2].data.imm.value = -value1;
                        instr->args[2].data.imm.obj = NULL;
                    }
                    changed = propagatedconstants = TRUE;
                    value2 = value1;
                } else if (isconstantoperand(&instr->args[2], &value1) && FITS_IN_SHORT(-value1)) {
                    if (value1 == 0) {
                        change_opcode(instr, PC_NEG);
                        change_num_operands(instr, 2);
                    } else {
                        instr->flags = opcodeinfo[PC_SUBFIC].flags |
                            (instr->flags & ~opcodeinfo[PC_SUBF].flags);
                        change_opcode(instr, PC_SUBFIC);
                        instr->args[2].kind = PCOp_IMMEDIATE;
                        instr->args[2].data.imm.value = value1;
                        instr->args[2].data.imm.obj = NULL;
                        instr->args[3].kind = PCOp_REGISTER;
                        instr->args[3].arg = RegClass_SPR;
                        instr->args[3].data.reg.reg = 0;
                        instr->args[3].data.reg.effect = EffectWrite;
                        change_num_operands(instr, 4);
                    }
                    changed = propagatedconstants = TRUE;
                }

                break;

            case PC_LBZ:
            case PC_LHZ:
            case PC_LHA:
            case PC_LWZ:
            case PC_STB:
            case PC_STH:
            case PC_STW:
            case PC_LFS:
            case PC_LFD:
            case PC_STFS:
            case PC_STFD:
                if (instr->args[2].kind == PCOp_IMMEDIATE) {
                    PCode* stackInstr;
                    SInt16 addend = instr->args[2].data.imm.value;

                    if ((stackInstr = isstackoperand(&instr->args[1], &value1, addend))) {
                        instr->args[1] = stackInstr->args[1];
                        instr->args[2] = stackInstr->args[2];
                        instr->args[2].data.imm.value = value1 + addend;
                        if (instr->flags & (fIsRead | fIsWrite | fOpFromMemory | fOpToMemory)) {
                            instr->alias = make_alias(
                                instr->args[2].data.imm.obj,
                                instr->args[2].data.imm.value,
                                nbytes_loaded_or_stored_by(instr)
                            );
                        }
                        changed = propagatedconstants = TRUE;
                    }
                }
                break;

            case PC_LBZX:
            case PC_LHZX:
            case PC_LHAX:
            case PC_LWZX:
            case PC_STBX:
            case PC_STHX:
            case PC_STWX:
            case PC_LFSX:
            case PC_LFDX:
            case PC_STFSX:
            case PC_STFDX:
                if (isconstantoperand(&instr->args[2], &value1)) {
                    instr->op -= 2;
                    instr->args[2].kind = PCOp_IMMEDIATE;
                    instr->args[2].data.imm.value = value1;
                    instr->args[2].data.imm.obj = NULL;
                    changed = propagatedconstants = TRUE;
                } else if (isconstantoperand(&instr->args[1], &value1)) {
                    instr->op -= 2;
                    instr->args[1] = instr->args[2];
                    instr->args[2].kind = PCOp_IMMEDIATE;
                    instr->args[2].data.imm.value = value1;
                    instr->args[2].data.imm.obj = NULL;
                    changed = propagatedconstants = TRUE;
                }

                break;
        }

        for (i = 0, op = instr->args; i < instr->argCount; i++, op++) {
            if (op->kind == PCOp_REGISTER &&
                op->arg == RegClass_GPR &&
                (op->data.reg.effect & EffectWrite)) {
                defininginstruction[op->data.reg.reg] = instr;
            } else if (op->kind == PCOp_REGISTER &&
                       op->arg == RegClass_FPR &&
                       (op->data.reg.effect & EffectWrite)) {
                fprdefininginstruction[op->data.reg.reg] = instr;
            } else if (op->kind == PCOp_REGISTER &&
                       op->arg == RegClass_VR &&
                       (op->data.reg.effect & EffectWrite)) {
                vrdefininginstruction[op->data.reg.reg] = instr;
            }
        }
    }
}

void propagateconstants(void) {
    PCodeBlock* block;
    SInt32 i;

    propagatedconstants = FALSE;
    computeusedefchains(FALSE);
    defininginstruction = galloc(sizeof(PCode*) * used_virtual_registers[RegClass_GPR]);
    fprdefininginstruction = galloc(sizeof(PCode*) * used_virtual_registers[RegClass_FPR]);
    vrdefininginstruction = galloc(sizeof(PCode*) * used_virtual_registers[RegClass_VR]);

    do {
        changed = FALSE;
        for (i = 0; i < pcblockcount; i++) {
            if ((block = depthfirstordering[i])) {
                computedefininginstructions(block);
                propagateconstantstoblock(block);
            }
        }
    } while (changed);

    freeoheap();
}
