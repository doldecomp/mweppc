#include "compiler/SpillCode.h"
#include "compiler/CError.h"
#include "compiler/CMachine.h"
#include "compiler/CodeGen.h"
#include "compiler/Coloring.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/InterferenceGraph.h"
#include "compiler/objects.h"
#include "compiler/Operands.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/StackFrame.h"
#include "compiler/types.h"

// (the original's statics; global while a lifted function of this unit still uses them)
int last_unused_vreg_before_spilling;
short rTEMP_for_VR_spill;
// the second spill temporary: the address of a paired-single spill slot
short rTEMP_for_PS_spill; // NOTE: invented name (after rTEMP_for_VR_spill)

void estimatespillcosts(void) {
    PCodeBlock* block;
    PCode* instr;
    IGNode* node;
    PCodeArg* op;
    int i;
    int weight;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        if (copts.optimizesize) {
            weight = 1;
        } else {
            weight = block->loopWeight;
        }

        for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
            op = instr->args;
            i = instr->argCount;
            while (i--) {
                if (PC_OP_IS_READ_ANY_REGISTER(op, coloring_class)) {
                    node = interferencegraph[op->data.reg.reg];
                    if (node->instr || copts.optimizesize) {
                        node->spillCost += weight;
                    } else {
                        node->spillCost += weight * 2;
                    }
                }
                op++;
            }

            op = instr->args;
            i = instr->argCount;
            while (i--) {
                if (PC_OP_IS_WRITE_ANY_REGISTER(op, coloring_class)) {
                    node = interferencegraph[op->data.reg.reg];
                    if (node->instr || (instr->flags & fIsArgInit)) {
                        node->spillCost -= weight;
                    } else {
                        node->spillCost += weight;
                    }
                }
                op++;
            }
        }
    }
}

static Object* makespilltemporary(Type* type) {
    Object* obj = lalloc(sizeof(Object));
    memclrw(obj, sizeof(Object));

    obj->otype = OT_OBJECT;
    obj->access = ACCESSPUBLIC;
    obj->datatype = DLOCAL;
    obj->type = type;
    obj->name = CParser_GetUniqueName();
    obj->u.var.info = CodeGen_GetNewVarInfo();
    obj->u.var.uid = 0;

    return obj;
}

static PCode* rematerialize_spilled_register(short reg, IGNode* node) {
    PCode* instr = copypcode(node->instr);

    CError_ASSERT(130, instr->args[0].kind == PCOp_REGISTER);

    instr->args[0].data.reg.reg = reg;
    return instr;
}

static void insert_load_spilled_register(PCode* instr, short reg, IGNode* node) {
    Type* type;
    Opcode opcode;
    PCode* newInstr;
    PCode* newInstr2;
    SInt32 offset;
    SInt32 offset2;
    Operand operand;

    type = node->spillTemporary->type;
    switch (coloring_class) {
        case RegClass_CRFIELD:
        case RegClass_GPR:
            switch (type->size) {
                case 1:
                    opcode = PC_LBZ;
                    break;
                case 2:
                    opcode = is_unsigned(type) ? PC_LHZ : PC_LHA;
                    break;
                case 4:
                    opcode = PC_LWZ;
                    break;
                case 8:
                    opcode = PC_LWZ;
                    break;
                default:
                    CError_FATAL(187);
            }

            memclrw(&operand, sizeof(Operand));
            operand.optype = OpndType_Symbol;
            operand.object = node->spillTemporary;
            CError_ASSERT(227, node->spillTemporary->datatype == DLOCAL);

            coerce_to_addressable(&operand);

            CError_ASSERT(238, operand.optype == OpndType_GPR_ImmOffset);

            if (node->flags & fPairLow) {
                offset = low_offset;
            } else if (node->flags & fPairHigh) {
                offset = high_offset;
            } else {
                offset = 0;
            }
            insertpcodebefore(
                instr,
                makepcode(opcode, reg, operand.reg, operand.object, operand.immOffset + offset)
            );
            break;

        case RegClass_FPR:
            CError_ASSERT(257, node->spillTemporary->datatype == DLOCAL);

            // a paired single is loaded with psq_l, or psq_lx past a 12-bit offset
            if ((node->flags & fPairedSingle1) &&
                IS_TYPE_VEC2X32FLOAT(node->spillTemporary->type)) {
                Opcode psop = Select_Gecko_Load_Store(node->spillTemporary, 0, TRUE);
                if (psop == PC_PSQ_L) {
                    insertpcodebefore(
                        instr,
                        makepcode(
                            psop,
                            reg,
                            local_base_register(node->spillTemporary),
                            node->spillTemporary,
                            0,
                            0,
                            SPR_GQR0
                        )
                    );
                } else {
                    newInstr = makepcode(
                        PC_ADDI,
                        rTEMP_for_PS_spill,
                        local_base_register(node->spillTemporary),
                        node->spillTemporary,
                        0
                    );
                    newInstr2 = makepcode(psop, reg, 0, rTEMP_for_PS_spill, 0, SPR_GQR0);
                    insertpcodebefore(instr, newInstr);
                    insertpcodeafter(newInstr, newInstr2);
                }
                break;
            }

            if (node->flags & fPairLow) {
                offset2 = low_offset;
            } else if (node->flags & fPairHigh) {
                offset2 = high_offset;
            } else {
                offset2 = 0;
            }

            insertpcodebefore(
                instr,
                makepcode(
                    (type->size == 8) ? PC_LFD : PC_LFS,
                    reg,
                    local_base_register(node->spillTemporary),
                    node->spillTemporary,
                    offset2
                )
            );
            break;

        case RegClass_VR:
            CError_ASSERT(304, node->spillTemporary->datatype == DLOCAL);

            newInstr = makepcode(
                PC_ADDI,
                rTEMP_for_VR_spill,
                local_base_register(node->spillTemporary),
                node->spillTemporary,
                0
            );
            newInstr2 = makepcode(PC_LVX, reg, 0, rTEMP_for_VR_spill);
            insertpcodebefore(instr, newInstr);
            insertpcodeafter(newInstr, newInstr2);
            break;

        default:
            CError_FATAL(317);
    }
}

// NOTE: invented names: instr, before, reg, node
static void insert_store_spilled_register(PCode* instr, Boolean before, short reg, IGNode* node) {
    Object* object;
    int opcode;
    SInt32 offset;
    PCode* newInstr2;
    PCode* newInstr;
    Type* type;

    object = node->spillTemporary;
    type = object->type;

    switch (coloring_class) {
        case RegClass_CRFIELD:
        case RegClass_GPR:
            switch (type->size) {
                case 1:
                    opcode = PC_STB;
                    break;
                case 2:
                    opcode = PC_STH;
                    break;
                case 4:
                    opcode = PC_STW;
                    break;
                case 8:
                    opcode = PC_STW;
                    break;
                default:
                    CError_FATAL(372);
            }

            if (node->flags & fPairLow) {
                offset = low_offset;
            } else if (node->flags & fPairHigh) {
                offset = high_offset;
            } else {
                offset = 0;
            }

            newInstr = makepcode(opcode, reg, local_base_register(object), object, offset);
            if (before) {
                insertpcodebefore(instr, newInstr);
            } else {
                insertpcodeafter(instr, newInstr);
            }
            break;

        case RegClass_FPR:
            // a paired single is stored with psq_st, or psq_stx past a 12-bit offset
            if ((node->flags & fPairedSingle1) && IS_TYPE_VEC2X32FLOAT(object->type)) {
                Opcode psop = Select_Gecko_Load_Store(object, 0, FALSE);
                if (psop == PC_PSQ_ST) {
                    newInstr = makepcode(
                        psop,
                        reg,
                        local_base_register(node->spillTemporary),
                        node->spillTemporary,
                        0,
                        0,
                        SPR_GQR0
                    );
                    if (before) {
                        insertpcodebefore(instr, newInstr);
                    } else {
                        insertpcodeafter(instr, newInstr);
                    }
                } else {
                    newInstr = makepcode(
                        PC_ADDI,
                        rTEMP_for_PS_spill,
                        local_base_register(node->spillTemporary),
                        node->spillTemporary,
                        0
                    );
                    newInstr2 = makepcode(psop, reg, 0, rTEMP_for_PS_spill, 0, SPR_GQR0);
                    if (before) {
                        insertpcodebefore(instr, newInstr);
                    } else {
                        insertpcodeafter(instr, newInstr);
                    }
                    // the original's bug: the addi is inserted a second time (the load path's two
                    // inserts pasted here), so the pcode is linked in twice
                    insertpcodebefore(instr, newInstr);
                    insertpcodeafter(newInstr, newInstr2);
                }
            } else {
                newInstr = makepcode(
                    (type->size == 8) ? PC_STFD : PC_STFS,
                    reg,
                    local_base_register(object),
                    object,
                    0
                );
                if (before) {
                    insertpcodebefore(instr, newInstr);
                } else {
                    insertpcodeafter(instr, newInstr);
                }
            }
            break;

        case RegClass_VR:
            newInstr =
                makepcode(PC_ADDI, rTEMP_for_VR_spill, local_base_register(object), object, 0);
            newInstr2 = makepcode(PC_STVX, reg, 0, rTEMP_for_VR_spill);
            if (before) {
                insertpcodebefore(instr, newInstr);
            } else {
                insertpcodeafter(instr, newInstr);
            }
            insertpcodeafter(newInstr, newInstr2);
            break;

        default:
            CError_FATAL(491);
    }
}

// NOTE: invented names: newReg, other, deleteInstr
static void spillinstruction(PCodeBlock* block, PCode* instr) {
    int reg;
    int newReg;
    int regs;
    IGNode* node;
    PCodeArg* op;
    int i;
    PCodeArg* other;
    int j;
    int readCounter;
    int writeCounter;
    Boolean deleteInstr;

    regs = used_virtual_registers[coloring_class];
    deleteInstr = FALSE;
    for (i = 0, op = instr->args; i < instr->argCount; i++, op++) {
        CError_ASSERT(528, instr->block != NULL);

        if (PC_OP_IS_ANY_REGISTER(op, coloring_class) &&
            op->data.reg.reg < regs &&
            ((node = interferencegraph[reg = op->data.reg.reg])->flags & fSpilled)) {
            newReg = used_virtual_registers[coloring_class]++;
            // too many virtual registers
            if (newReg > 0x7FFF) {
                PPCError_ErrorTerm(PPCERR_TOO_MANY_REGISTERS);
            }
            readCounter = 0;
            writeCounter = 0;

            for (j = i, other = op; j < instr->argCount; j++, other++) {
                if (PC_OP_IS_REGISTER(other, coloring_class, reg)) {
                    if (other->data.reg.effect & EffectRead) {
                        readCounter++;
                    }
                    if (other->data.reg.effect & EffectWrite) {
                        writeCounter++;
                    }
                    other->data.reg.reg = newReg;
                    other->data.reg.effect |= EffectSpilled;
                }
            }

            if (readCounter) {
                if (node->instr) {
                    insertpcodebefore(instr, rematerialize_spilled_register(newReg, node));
                } else {
                    insert_load_spilled_register(instr, newReg, node);
                }
            }

            if (writeCounter) {
                if (node->instr || (instr->flags & fIsArgInit)) {
                    deleteInstr = TRUE;
                } else {
                    insert_store_spilled_register(instr, FALSE, newReg, node);
                }
            }
        }
    }

    if (deleteInstr) {
        deletepcode(instr);
    }
}

// NOTE: invented names: srcNode, destNode
static void spillcopy(PCodeBlock* block, PCode* instr) {
    IGNode* srcNode;
    IGNode* destNode;
    int reg;

    srcNode = interferencegraph[instr->args[1].data.reg.reg];
    destNode = interferencegraph[instr->args[0].data.reg.reg];

    if (srcNode->flags & fSpilled) {
        if (destNode->flags & fSpilled) {
            reg = used_virtual_registers[coloring_class]++;
            if (reg > 0x7FFF) {
                PPCError_ErrorTerm(PPCERR_TOO_MANY_REGISTERS);
            }
            if (srcNode->instr) {
                insertpcodebefore(instr, rematerialize_spilled_register(reg, srcNode));
            } else {
                insert_load_spilled_register(instr, reg, srcNode);
            }
            insert_store_spilled_register(instr, TRUE, reg, destNode);
        } else {
            if (srcNode->instr) {
                insertpcodebefore(
                    instr, rematerialize_spilled_register(instr->args[0].data.reg.reg, srcNode)
                );
            } else {
                insert_load_spilled_register(instr, instr->args[0].data.reg.reg, srcNode);
            }
        }
    } else {
        insert_store_spilled_register(instr, TRUE, instr->args[1].data.reg.reg, destNode);
    }

    deletepcode(instr);
}

static void spillcall(PCodeBlock* block, PCode* instr) {
    PCodeArg* opSrc;
    PCodeArg* opDst;
    int opCount;
    int i;

    opCount = instr->argCount;
    i = branch_count_volatiles();

    opSrc = instr->args + i;
    opDst = instr->args + i;
    for (; i < opCount; i++, opSrc++) {
        if (PC_OP_IS_ANY_REGISTER(opSrc, coloring_class) &&
            opSrc->data.reg.reg >= n_real_registers[coloring_class] &&
            (interferencegraph[opSrc->data.reg.reg]->flags & fSpilled)) {
            instr->argCount--;
        } else {
            *opDst = *opSrc;
            opDst++;
        }
    }

    spillinstruction(block, instr);
}

static void assign_spill_locations(void) {
    UInt32 i;
    IGNode* node;
    Type* type;

    last_unused_vreg_before_spilling = used_virtual_registers[coloring_class];
    for (i = n_real_registers[coloring_class]; i < (UInt32)last_unused_vreg_before_spilling; i++) {
        node = interferencegraph[i];
        if (node->flags & fCoalesced) {
            continue;
        }
        if (!(node->flags & fSpilled)) {
            continue;
        }

        if (!node->spillTemporary) {
            switch (coloring_class) {
                case RegClass_GPR:
                    type = TYPE(&stunsignedlong);
                    break;
                case RegClass_CRFIELD:
                    type = TYPE(&stunsignedlong);
                    break;
                case RegClass_FPR:
                    // a paired single's slot holds both halves
                    if (node->flags & fPairedSingle1) {
                        type = TYPE(&stvec2x32float);
                    } else {
                        type = TYPE(&stdouble);
                    }
                    break;
                case RegClass_VR:
                    type = TYPE(&stvectorunsignedchar);
                    break;
                default:
                    CError_FATAL(743);
            }

            node->spillTemporary = makespilltemporary(type);
        }

        if (node->spillTemporary->datatype == DLOCAL &&
            !(node->spillTemporary->u.var.info->flags & VarInfoFlag_ParamArea)) {
            assign_local_memory(node->spillTemporary);
        }

        if (node->flags & fPairHigh) {
            Registers_GetVarInfo(node->spillTemporary)->regHi = Register0;
        } else {
            Registers_GetVarInfo(node->spillTemporary)->reg = Register0;
        }
    }
}

void insertspillcode(void) {
    PCodeBlock* block;
    PCode* instr;
    PCode* nextInstr;
    PCodeArg* arg;
    UInt32 i;
    int usesSpilled; // NOTE: invented name
    PCodeArg* op;

    rTEMP_for_VR_spill = 0;
    rTEMP_for_PS_spill = 0;

    // the paired-single operands' nodes are marked first
    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
            for (i = 0; i < (UInt32)instr->argCount; i++) {
                arg = &instr->args[i];
                if (PC_OP_IS_ANY_REGISTER(arg, coloring_class) && is_paired_single_operand(*arg)) {
                    interferencegraph[instr->args[i].data.reg.reg]->flags |=
                        ((arg->data.reg.effect & EffectPS1) ? fPairedSingle1 : 0) |
                        ((arg->data.reg.effect & EffectPS0) ? fPairedSingle0 : 0);
                }
            }
        }
    }

    assign_spill_locations();

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (instr = block->firstPCode; instr; instr = nextInstr) {
            nextInstr = instr->nextPCode;
            usesSpilled = FALSE;
            op = instr->args;
            i = instr->argCount;
            while (i--) {
                if (PC_OP_IS_ANY_REGISTER(op, coloring_class) &&
                    op->data.reg.reg < last_unused_vreg_before_spilling &&
                    (interferencegraph[op->data.reg.reg]->flags & fSpilled)) {
                    usesSpilled = TRUE;
                    break;
                }
                op++;
            }

            if (usesSpilled) {
                if (coloring_class == RegClass_FPR && rTEMP_for_PS_spill == 0) {
                    rTEMP_for_PS_spill = used_virtual_registers[RegClass_GPR]++;
                }
                if (coloring_class == RegClass_VR && rTEMP_for_VR_spill == 0) {
                    rTEMP_for_VR_spill = used_virtual_registers[RegClass_GPR]++;
                }

                if (instr->flags & fIsMove) {
                    spillcopy(block, instr);
                } else if (instr->flags & fIsCall) {
                    spillcall(block, instr);
                } else {
                    spillinstruction(block, instr);
                }
            }
        }
    }
}
