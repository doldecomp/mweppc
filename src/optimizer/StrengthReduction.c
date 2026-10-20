#include "compiler/StrengthReduction.h"
#include "compiler/BitVectors.h"
#include "compiler/CError.h"
#include "compiler/CompilerTools.h"
#include "compiler/LoopDetection.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Registers.h"
#include "compiler/UseDefChains.h"

int strengthreducedloops;

static PCode* findinitializer(Loop* loop, short reg) {
    UInt32* vec;
    PCode* best;
    RegUseOrDef* list;

    vec = usedefinfo[loop->body->blockIndex].defin;
    best = NULL;

    for (list = reg_Defs[RegClass_GPR][reg]; list; list = list->next) {
        if (!(bitvectorgetbit(Defs[list->id].pcode->block->blockIndex, loop->memberblocks)) &&
            bitvectorgetbit(list->id, vec)) {
            if (best) {
                return NULL;
            }
            best = Defs[list->id].pcode;
        }
    }

    if (best) {
        if (best->op == PC_LI || best->op == PC_ADDI || best->op == PC_ADD) {
            return best;
        }
    }

    return NULL;
}

static int isbasicinductionvariable(Loop* loop, short reg, SInt32 step) {
    RegUseOrDef* list;
    PCode* instr;

    for (list = reg_Defs[RegClass_GPR][reg]; list; list = list->next) {
        instr = Defs[list->id].pcode;
        if (bitvectorgetbit(instr->block->blockIndex, loop->memberblocks)) {
            if (instr->op != PC_ADDI) {
                return FALSE;
            }
            if (instr->args[1].data.reg.reg != reg) {
                return FALSE;
            }
            if (instr->args[2].data.imm.value != step) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

static void addbasicinductionvariable(Loop* loop, short reg, SInt32 step) {
    BasicInductionVar* biv;
    RegUseOrDef* list;
    PCode* instr;
    InstrList* instrList;

    for (biv = loop->basicInductionVars; biv; biv = biv->next) {
        if (biv->reg == reg) {
            return;
        }
    }

    biv = oalloc(sizeof(BasicInductionVar));
    biv->next = loop->basicInductionVars;
    loop->basicInductionVars = biv;

    biv->loop = loop;
    biv->inductionVars = NULL;
    biv->instrs = NULL;
    biv->step = step;
    biv->reg = reg;

    for (list = reg_Defs[RegClass_GPR][reg]; list; list = list->next) {
        instr = Defs[list->id].pcode;
        if (bitvectorgetbit(instr->block->blockIndex, loop->memberblocks)) {
            instrList = oalloc(sizeof(InstrList));
            instrList->next = biv->instrs;
            biv->instrs = instrList;
            instrList->instr = instr;
        }
    }

    biv->initializer = findinitializer(loop, reg);
}

static void findbasicinductionvariables(Loop* loop) {
    BlockList* block;
    PCode* instr;
    SInt16 step;
    short reg;

    for (block = loop->blocks; block; block = block->next) {
        for (instr = block->block->firstPCode; instr; instr = instr->nextPCode) {
            if (instr->op == PC_ADDI) {
                reg = instr->args[0].data.reg.reg;
                if (reg >= 32 && instr->args[1].data.reg.reg == reg) {
                    step = instr->args[2].data.imm.value;
                    if (isbasicinductionvariable(loop, reg, step)) {
                        addbasicinductionvariable(loop, reg, step);
                    }
                }
            }
        }
    }
}

static void findallbasicinductionvariables(Loop* loop) {
    while (loop) {
        if (loop->children) {
            findallbasicinductionvariables(loop->children);
        }
        findbasicinductionvariables(loop);
        loop = loop->nextSibling;
    }
}

static int isinductionvariable(
    BasicInductionVar* biv,
    int useID,
    SInt32* step,
    short* baseOperand,
    short* sourceOperand,
    Loop** outerLoop
) {
    PCode* instr;
    RegUseOrDef* list;
    int counter;
    Loop* loop;
    Loop* scanloop;

    instr = Uses[useID].pcode;
    *baseOperand = 0;
    *sourceOperand = 0;
    *outerLoop = NULL;

    switch (instr->op) {
        case PC_MULLI:
            *step = instr->args[2].data.imm.value;
            break;

        case PC_RLWINM:
            if (instr->args[3].data.imm.value) {
                return FALSE;
            }
            if (instr->args[2].data.imm.value > 15) {
                return FALSE;
            }
            if (instr->args[4].data.imm.value != (31 - instr->args[2].data.imm.value)) {
                return FALSE;
            }
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                return FALSE;
            }
            *step = 1 << instr->args[2].data.imm.value;
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
            *baseOperand = 0;
            *sourceOperand = 0;
            if (instr->args[1].data.reg.reg == biv->reg) {
                *baseOperand = 1;
                *sourceOperand = 2;
            } else if (instr->args[2].data.reg.reg == biv->reg) {
                *baseOperand = 2;
                *sourceOperand = 1;
            }

            counter = 0;
            for (list = reg_Defs[RegClass_GPR][instr->args[*sourceOperand].data.reg.reg]; list;
                 list = list->next) {
                if (bitvectorgetbit(
                        Defs[list->id].pcode->block->blockIndex, biv->loop->memberblocks
                    )) {
                    counter++;
                }
            }
            if (counter) {
                return FALSE;
            }

            loop = biv->loop;
            for (scanloop = loop->parent; scanloop; scanloop = scanloop->parent) {
                counter = 0;
                for (list = reg_Defs[RegClass_GPR][instr->args[*sourceOperand].data.reg.reg]; list;
                     list = list->next) {
                    if (bitvectorgetbit(
                            Defs[list->id].pcode->block->blockIndex, scanloop->memberblocks
                        )) {
                        counter++;
                    }
                }
                if (!biv->initializer ||
                    bitvectorgetbit(biv->initializer->block->blockIndex, scanloop->memberblocks)) {
                    counter++;
                }
                if (counter) {
                    break;
                }
                loop = scanloop;
            }

            *outerLoop = loop;
            *step = 1;
            return TRUE;

        default:
            return FALSE;
    }

    counter = 0;
    for (list = reg_Defs[RegClass_GPR][instr->args[0].data.reg.reg]; list; list = list->next) {
        if (bitvectorgetbit(Defs[list->id].pcode->block->blockIndex, biv->loop->memberblocks)) {
            counter++;
        }
    }

    if (counter != 1) {
        return FALSE;
    }
    return TRUE;
}

static void addinductionvariable(
    BasicInductionVar* biv,
    PCode* instr,
    SInt32 step,
    short baseOperand,
    short sourceOperand,
    Loop* outerLoop
) {
    InductionVar* iv;

    iv = oalloc(sizeof(InductionVar));
    iv->next = biv->inductionVars, biv->inductionVars = iv;

    iv->basicVar = biv;
    iv->instr = instr;
    iv->replacement_instruction = NULL;
    iv->step = step;
    iv->base_operand = baseOperand;
    iv->source_operand = sourceOperand;
    iv->outer_loop = outerLoop;
    if (instr->flags & (fIsRead | fIsWrite)) {
        iv->source_register = -1;
    } else {
        iv->source_register = instr->args[0].data.reg.reg;
    }
    iv->destination_register = -1;
}

static void findnonbasicinductionvariables(Loop* loop) {
    BasicInductionVar* biv;
    RegUseOrDef* list;
    SInt32 step;
    short baseOperand;
    short sourceOperand;
    Loop* outerLoop;

    for (biv = loop->basicInductionVars; biv; biv = biv->next) {
        for (list = reg_Uses[RegClass_GPR][biv->reg]; list; list = list->next) {
            if (bitvectorgetbit(Uses[list->id].pcode->block->blockIndex, loop->memberblocks)) {
                if (isinductionvariable(
                        biv, list->id, &step, &baseOperand, &sourceOperand, &outerLoop
                    )) {
                    addinductionvariable(
                        biv, Uses[list->id].pcode, step, baseOperand, sourceOperand, outerLoop
                    );
                }
            }
        }
    }
}

static void findallnonbasicinductionvariables(Loop* loop) {
    while (loop) {
        if (loop->children) {
            findallnonbasicinductionvariables(loop->children);
        }
        if (loop->basicInductionVars) {
            findnonbasicinductionvariables(loop);
        }
        loop = loop->nextSibling;
    }
}

static void initializeinductionvariable(InductionVar* iv) {
    BasicInductionVar* biv;
    PCodeBlock* preheader;
    SInt32 value;
    PCode* instr;
    short sourceReg;
    short baseReg;

    biv = iv->basicVar;
    preheader = biv->loop->preheader;

    if (iv->source_operand) {
        sourceReg = iv->instr->args[iv->source_operand].data.reg.reg;
        baseReg = iv->instr->args[iv->base_operand].data.reg.reg;
        instr = NULL;

        if (biv->initializer &&
            biv->initializer->op == PC_LI &&
            biv->initializer->block == preheader) {
            value = biv->initializer->args[1].data.imm.value;
            if (value == 0) {
                instr = makepcode(PC_MR, iv->destination_register, sourceReg);
            } else if (FITS_IN_SHORT(value)) {
                instr = makepcode(PC_ADDI, iv->destination_register, sourceReg, 0, value);
            }
        }

        if (!instr) {
            instr = makepcode(PC_ADD, iv->destination_register, sourceReg, baseReg);
        }

        if (biv->initializer && instr->op != PC_ADD) {
            insertpcodeafter(biv->initializer, instr);
        } else if (iv->outer_loop && iv->outer_loop->preheader->lastPCode) {
            insertpcodebefore(iv->outer_loop->preheader->lastPCode, instr);
        } else {
            insertpcodebefore(preheader->lastPCode, instr);
        }

        iv->replacement_instruction = instr;
        iv->source_register = sourceReg;
        return;
    }

    if (!biv->initializer || biv->initializer->op != PC_LI) {
        instr = copypcode(iv->instr);
        instr->args[0].data.reg.reg = iv->destination_register;
        insertpcodebefore(preheader->lastPCode, instr);
    } else {
        value = biv->initializer->args[1].data.imm.value * iv->step;
        if (!FITS_IN_SHORT(value)) {
            instr = makepcode(PC_LIS, iv->destination_register, 0, HIGH_PART(value));
            insertpcodeafter(biv->initializer, instr);
            if (LOW_PART(value) != 0) {
                insertpcodeafter(
                    instr,
                    makepcode(
                        PC_ADDI,
                        iv->destination_register,
                        iv->destination_register,
                        0,
                        LOW_PART(value)
                    )
                );
            }
        } else {
            instr = makepcode(PC_LI, iv->destination_register, value);
            insertpcodeafter(biv->initializer, instr);
        }
    }
}

static void incrementinductionvariable(InductionVar* iv) {
    SInt32 value;
    BasicInductionVar* biv;
    PCode* instr;
    InstrList* list;

    biv = iv->basicVar;
    value = iv->step * biv->step;
    for (list = biv->instrs; list; list = list->next) {
        if (!FITS_IN_SHORT(value)) {
            instr = makepcode(
                PC_ADDIS, iv->destination_register, iv->destination_register, 0, HIGH_PART(value)
            );
            insertpcodeafter(list->instr, instr);

            if (LOW_PART(value) != 0) {
                instr = makepcode(
                    PC_ADDI, iv->destination_register, iv->destination_register, 0, LOW_PART(value)
                );
                insertpcodeafter(list->instr->nextPCode, instr);
            }
        } else {
            instr =
                makepcode(PC_ADDI, iv->destination_register, iv->destination_register, 0, value);
            insertpcodeafter(list->instr, instr);
        }
    }
}

static void copyinductionvariable(InductionVar* iv) {
    if (iv->instr->flags & (fIsRead | fIsWrite)) {
        iv->instr->op -= 2;
        iv->instr->args[1].data.reg.reg = iv->destination_register;
        iv->instr->args[2].kind = PCOp_IMMEDIATE;
        iv->instr->args[2].data.imm.value = 0;
        iv->instr->args[2].data.mem.obj = NULL;
    } else {
        insertpcodeafter(
            iv->instr, makepcode(PC_MR, iv->source_register, iv->destination_register)
        );
        deletepcode(iv->instr);
    }
}

static int testnestediv(
    InductionVar* iv, SInt32 step1, int reg, SInt32 step2, Loop* loop1, Loop* loop2
) {
    PCode* instr;
    int i;
    BlockList* list;
    PCodeArg* op;
    SInt32 addend;

    if (iv->replacement_instruction && iv->source_register == reg) {
        if (iv->replacement_instruction->op == PC_MR) {
            addend = 0;
        } else if (iv->replacement_instruction->op == PC_ADDI) {
            addend = iv->replacement_instruction->args[2].data.imm.value;
        } else {
            return FALSE;
        }

        if (step2 == (addend + (step1 * iv->step * loop2->iterationCount))) {
            for (list = loop1->blocks; list && list->block != loop2->blocks->block;
                 list = list->next) {
                for (instr = list->block->firstPCode; instr; instr = instr->nextPCode) {
                    op = instr->args;
                    i = instr->argCount;
                    while (i--) {
                        if (op->kind == PCOp_REGISTER &&
                            op->arg == RegClass_GPR &&
                            op->data.reg.reg == reg) {
                            return FALSE;
                        }
                        op++;
                    }
                }
            }
            return TRUE;
        }
    }

    return FALSE;
}

static void strengthreducenestediv(short reg, SInt32 step, PCode* initializer, Loop* loop) {
    Loop* scanloop;
    BasicInductionVar* biv;
    InductionVar* iv;
    PCode* instr;
    PCodeArg* op;
    int i;

    for (scanloop = loop->children; scanloop; scanloop = scanloop->nextSibling) {
        if (scanloop->isKnownCountingLoop &&
            scanloop->is_straight_line &&
            bitvectorgetbit(scanloop->body->blockIndex, loop->backedge_dominators)) {
            for (biv = scanloop->basicInductionVars; biv; biv = biv->next) {
                for (iv = biv->inductionVars; iv; iv = iv->next) {
                    if (testnestediv(iv, biv->step, reg, step, loop, scanloop)) {
                        deletepcode(iv->replacement_instruction);
                        if (initializer) {
                            insertpcodeafter(initializer, iv->replacement_instruction);
                        } else if (loop->body->lastPCode) {
                            for (instr = loop->body->lastPCode; instr; instr = instr->prevPCode) {
                                op = instr->args;
                                i = instr->argCount;
                                while (i--) {
                                    if (op->kind == PCOp_REGISTER &&
                                        op->arg == RegClass_GPR &&
                                        (op->data.reg.effect & EffectWrite) &&
                                        op->data.reg.reg == reg) {
                                        break;
                                    }
                                    op++;
                                }
                            }

                            if (instr) {
                                insertpcodeafter(instr, iv->replacement_instruction);
                            } else {
                                insertpcodebefore(
                                    loop->body->firstPCode, iv->replacement_instruction
                                );
                            }
                        } else {
                            appendpcode(loop->body, iv->replacement_instruction);
                        }
                    }
                }
            }
        }
    }
}

static void strengthreducenestedbiv(BasicInductionVar* biv) {
    Loop* loop;
    InductionVar* iv;

    loop = biv->loop;
    for (iv = biv->inductionVars; iv; iv = iv->next) {
        strengthreducenestediv(
            iv->destination_register, iv->step * biv->step, iv->replacement_instruction, loop
        );
    }

    strengthreducenestediv(biv->reg, biv->step, biv->initializer, loop);
}

static void strengthreduceinductionvariable(BasicInductionVar* biv) {
    int counter;
    InductionVar* iv;
    InductionVar* otherIv;
    short reg;

    counter = 0;
    for (iv = biv->inductionVars; iv; iv = iv->next) {
        if (iv->step == 1) {
            counter++;
        }
    }

    for (iv = biv->inductionVars; iv; iv = iv->next) {
        if ((counter <= 4 || iv->step != 1) &&
            iv->instr->block &&
            (iv->source_operand == 0 || iv->instr->args[2].kind != PCOp_IMMEDIATE)) {
            if (iv->destination_register == -1) {
                iv->destination_register = used_virtual_registers[RegClass_GPR]++;
                initializeinductionvariable(iv);
                incrementinductionvariable(iv);
                if (iv->step == 1) {
                    reg = iv->instr->args[iv->source_operand].data.reg.reg;
                    for (otherIv = iv->next; otherIv; otherIv = otherIv->next) {
                        if (otherIv->source_operand != 0 &&
                            otherIv->instr->args[otherIv->source_operand].data.reg.reg == reg) {
                            otherIv->destination_register = iv->destination_register;
                        }
                    }
                } else {
                    for (otherIv = iv->next; otherIv; otherIv = otherIv->next) {
                        if (otherIv->step == iv->step) {
                            otherIv->destination_register = iv->destination_register;
                        }
                    }
                }
            }

            copyinductionvariable(iv);
            strengthreducedloops = TRUE;
        }
    }
}

// NOTE: invented names: addend, basereg, indexreg, offset, object
// what a basic induction variable's initializer adds up (2-byte packing: 14 bytes)
#pragma pack(push, 2)
typedef struct BivInit {
    SInt32 addend; // the initializer's immediate, when it adds to the variable itself
    short basereg; // the register added to, or -1
    short indexreg; // a register added, or -1
    short offset; // an immediate added
    Object* object; // a memory operand's object
} BivInit;
#pragma pack(pop)

static void calc_biv_init(BasicInductionVar* biv, BivInit* init) {
    PCode* instr;
    PCodeArg* op;
    int i;
    PCode* scan;

    instr = biv->initializer;
    init->addend = 0;
    init->basereg = -1;
    init->indexreg = -1;
    init->offset = 0;
    init->object = NULL;

    if (!biv->initializer || (biv->initializer->op != PC_ADDI && biv->initializer->op != PC_ADD)) {
        return;
    }

    if (instr->op == PC_ADDI) {
        if (instr->args[1].data.reg.reg == biv->reg) {
            init->addend = instr->args[2].data.imm.value;
            for (scan = instr->prevPCode; scan; scan = scan->prevPCode) {
                op = scan->args;
                i = scan->argCount;
                while (i--) {
                    if (op->kind == PCOp_REGISTER &&
                        op->arg == RegClass_GPR &&
                        op->data.reg.reg == biv->reg &&
                        (op->data.reg.effect & EffectWrite)) {
                        if (scan->op == PC_ADD) {
                            init->basereg = scan->args[1].data.reg.reg;
                            init->indexreg = scan->args[2].data.reg.reg;
                        } else if (scan->op == PC_ADDI) {
                            if (scan->args[2].kind == PCOp_IMMEDIATE) {
                                init->basereg = scan->args[1].data.reg.reg;
                                init->offset = scan->args[2].data.imm.value;
                            } else if (scan->args[2].kind == PCOp_MEMORY) {
                                init->basereg = scan->args[1].data.reg.reg;
                                init->offset = scan->args[2].data.imm.value;
                                init->object = scan->args[2].data.mem.obj;
                            }
                        }
                        return;
                    }
                    op++;
                }
            }
        } else {
            if (instr->args[2].kind == PCOp_IMMEDIATE) {
                init->basereg = instr->args[1].data.reg.reg;
                init->offset = instr->args[2].data.imm.value;
            } else if (instr->args[2].kind == PCOp_MEMORY) {
                init->basereg = instr->args[1].data.reg.reg;
                init->offset = instr->args[2].data.imm.value;
                init->object = instr->args[2].data.mem.obj;
            }
        }
    } else if (instr->op == PC_ADD) {
        if (instr->args[1].data.reg.reg == biv->reg) {
            init->indexreg = instr->args[2].data.reg.reg;
            for (scan = instr->prevPCode; scan; scan = scan->prevPCode) {
                op = scan->args;
                i = scan->argCount;
                while (i--) {
                    if (op->kind == PCOp_REGISTER &&
                        op->arg == RegClass_GPR &&
                        op->data.reg.reg == biv->reg &&
                        (op->data.reg.effect & EffectWrite) &&
                        scan->op == PC_ADDI) {
                        if (scan->args[2].kind == PCOp_IMMEDIATE) {
                            init->basereg = scan->args[1].data.reg.reg;
                            init->offset = scan->args[2].data.imm.value;
                        } else if (scan->args[2].kind == PCOp_MEMORY) {
                            init->basereg = scan->args[1].data.reg.reg;
                            init->offset = scan->args[2].data.imm.value;
                            init->object = scan->args[2].data.mem.obj;
                        }
                        return;
                    }
                    op++;
                }
            }
        } else {
            init->basereg = instr->args[1].data.reg.reg;
            init->indexreg = instr->args[2].data.reg.reg;
        }
    }
}

static void combineinductionvariables(
    Loop* loop, BasicInductionVar* biv1, BasicInductionVar* biv2, SInt32 difference
) {
    PCode* instr1;
    int reg2;
    PCode* instr2;
    int reg1;
    PCodeBlock* nextBlock;
    BlockList* list;
    int i;
    PCodeArg* op;
    PCode* instr;
    PCode* last;

    instr1 = NULL;
    instr2 = NULL;

    reg1 = biv1->reg;
    CError_ASSERT(930, reg1 >= 0);

    reg2 = biv2->reg;
    CError_ASSERT(934, reg2 >= 0);

    if (!FITS_IN_SHORT(difference)) {
        return;
    }

    for (list = loop->blocks; list; list = list->next) {
        for (instr = list->block->firstPCode; instr; instr = instr->nextPCode) {
            if (instr1) {
                op = instr->args;
                i = instr->argCount;
                while (i--) {
                    if (op->kind == PCOp_REGISTER &&
                        op->arg == RegClass_GPR &&
                        op->data.reg.reg == reg1) {
                        return;
                    }
                    op++;
                }
            }

            if (instr->op == PC_ADDI) {
                if (instr->args[0].data.reg.reg == reg1) {
                    if (instr1) {
                        return;
                    }
                    instr1 = instr;
                } else if (instr->args[0].data.reg.reg == reg2) {
                    if (instr2) {
                        return;
                    }
                    instr2 = instr;
                }
            }
        }
    }

    last = loop->body->lastPCode;
    if (last->flags & fIsBranch) {
        nextBlock = NULL;

        for (i = 0; i < last->argCount; i++) {
            if (last->args[i].kind == PCOp_LABEL) {
                nextBlock = last->args[i].data.label.label->block;
                break;
            }
        }

        if (!nextBlock) {
            return;
        }
    } else {
        nextBlock = loop->body->nextBlock;
    }

    deletepcode(instr1);
    instr1->args[1].data.reg.reg = reg2;
    instr1->args[2].data.imm.value = difference;

    if (nextBlock->firstPCode) {
        insertpcodebefore(nextBlock->firstPCode, instr1);
    } else {
        appendpcode(nextBlock, instr1);
    }

    biv1->reg = -1;
    strengthreducedloops = TRUE;
}

static void strengthreduceinductionvariables(Loop* loop) {
    BasicInductionVar* biv1;
    BasicInductionVar* biv2;
    BivInit init1;
    BivInit init2;

    for (biv1 = loop->basicInductionVars; biv1; biv1 = biv1->next) {
        if (biv1->inductionVars) {
            strengthreduceinductionvariable(biv1);
        }
        strengthreducenestedbiv(biv1);
    }

    for (biv1 = loop->basicInductionVars; biv1; biv1 = biv1->next) {
        if (biv1->reg != -1) {
            calc_biv_init(biv1, &init1);
            if (init1.basereg != -1) {
                for (biv2 = loop->basicInductionVars; biv2; biv2 = biv2->next) {
                    if (biv2->reg != -1 && biv2 != biv1) {
                        calc_biv_init(biv2, &init2);
                        if (init2.basereg != -1 &&
                            init1.basereg == init2.basereg &&
                            init1.indexreg == init2.indexreg &&
                            init1.offset == init2.offset &&
                            init1.object == init2.object &&
                            biv1->step == biv2->step) {
                            if (init1.addend < init2.addend) {
                                combineinductionvariables(
                                    loop, biv2, biv1, init2.addend - init1.addend
                                );
                            } else {
                                combineinductionvariables(
                                    loop, biv1, biv2, init1.addend - init2.addend
                                );
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

static void strengthreduceallinductionvariables(Loop* loop) {
    while (loop) {
        if (loop->children) {
            strengthreduceallinductionvariables(loop->children);
        }
        if (loop->basicInductionVars) {
            strengthreduceinductionvariables(loop);
        }
        loop = loop->nextSibling;
    }
}

void strengthreduceloops(void) {
    strengthreducedloops = FALSE;
    if (loopsinflowgraph) {
        computeusedefchains(FALSE);
        findallbasicinductionvariables(loopsinflowgraph);
        findallnonbasicinductionvariables(loopsinflowgraph);
        strengthreduceallinductionvariables(loopsinflowgraph);
        freeoheap();
    }
}
