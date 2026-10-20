#include "compiler/LoopOptimization.h"
#include "compiler/BitVectors.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/LoopDetection.h"
#include "compiler/objects.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Registers.h"
#include "compiler/types.h"
#include "compiler/UseDefChains.h"
#include <stdlib.h>

int optimizedloops;
int optimizedloop_full_unroll;
int optimizedloop_trans_regs;
static UInt32* liveonexit;
static UInt32* inductionvariables;
static SInt32 last_virtual_GPR;

static int ispowerof2(SInt32 value) {
    int bit = getbit(value);
    return (bit > 0 && bit < 31) ? bit : 0;
}

static void insertupdateinstructions(Loop* loop) {}

static void computeliveonexit(Loop* loop) {
    UInt32* usevec;
    UInt32* defvec;
    BlockList* blocklist;
    RegUseOrDef* list;
    int gpr;

    bitvectorinitialize(usevec = oalloc(4 * ((number_of_Uses + 31) >> 5)), number_of_Uses, 0);
    for (blocklist = loop->blocks; blocklist; blocklist = blocklist->next) {
        if (bitvectorgetbit(blocklist->block->blockIndex, loop->exitblocks)) {
            bitvectorunion(usevec, usedefinfo[blocklist->block->blockIndex].useout, number_of_Uses);
        }
    }

    bitvectorinitialize(defvec = oalloc(4 * ((number_of_Defs + 31) >> 5)), number_of_Defs, 0);
    if (loop->preheader) {
        bitvectorunion(defvec, usedefinfo[loop->preheader->blockIndex].defin, number_of_Defs);
    }

    bitvectorinitialize(liveonexit, last_virtual_GPR, 0);

    for (gpr = 32; last_virtual_GPR > gpr; gpr++) {
        for (list = reg_Defs[RegClass_GPR][gpr]; list; list = list->next) {
            if (bitvectorgetbit(list->id, defvec)) {
                if (!Defs[list->id].pcode->block ||
                    bitvectorgetbit(Defs[list->id].pcode->block->blockIndex, loop->memberblocks)) {
                    bitvectorsetbit(gpr, liveonexit);
                    break;
                }
            }
        }

        for (list = reg_Uses[RegClass_GPR][gpr]; list; list = list->next) {
            if (bitvectorgetbit(list->id, usevec)) {
                if (!Uses[list->id].pcode->block ||
                    !bitvectorgetbit(Uses[list->id].pcode->block->blockIndex, loop->memberblocks)) {
                    bitvectorsetbit(gpr, liveonexit);
                    break;
                }
            }
        }
    }
}

static void eliminateinductionvariables(Loop* loop) {
    BlockList* blocklist;
    PCode* instr;
    PCodeArg* op;
    PCode* nextInstr;
    int i;
    int reg;

    bitvectorinitialize(inductionvariables, last_virtual_GPR, 0xFFFFFFFF);

    for (blocklist = loop->blocks; blocklist; blocklist = blocklist->next) {
        for (instr = blocklist->block->firstPCode; instr; instr = instr->nextPCode) {
            if (instr->op != PC_ADDI ||
                (reg = instr->args[0].data.reg.reg) < 32 ||
                reg >= last_virtual_GPR ||
                instr->args[1].data.reg.reg != reg) {
                op = instr->args;
                i = instr->argCount;
                while (i--) {
                    if (op->kind == PCOp_REGISTER &&
                        op->arg == RegClass_GPR &&
                        op->data.reg.reg >= n_real_registers[RegClass_GPR] &&
                        op->data.reg.reg < last_virtual_GPR) {
                        bitvectorclearbit(op->data.reg.reg, inductionvariables);
                    }
                    op++;
                }
            }
        }
    }

    if (loop->parent) {
        for (instr = loop->preheader->firstPCode; instr; instr = nextInstr) {
            nextInstr = instr->nextPCode;
            op = instr->args;
            i = instr->argCount;
            while (i--) {
                if (op->kind == PCOp_REGISTER &&
                    op->arg == RegClass_GPR &&
                    op->data.reg.reg >= n_real_registers[RegClass_GPR] &&
                    op->data.reg.reg < last_virtual_GPR) {
                    bitvectorsetbit(op->data.reg.reg, liveonexit);
                }
                op++;
            }
        }
    }

    for (blocklist = loop->blocks; blocklist; blocklist = blocklist->next) {
        for (instr = blocklist->block->firstPCode; instr; instr = nextInstr) {
            nextInstr = instr->nextPCode;
            if (instr->op == PC_ADDI &&
                (reg = instr->args[0].data.reg.reg) >= 32 &&
                reg < last_virtual_GPR &&
                instr->args[1].data.reg.reg == reg &&
                bitvectorgetbit(reg, inductionvariables) &&
                !bitvectorgetbit(reg, liveonexit)) {
                deletepcode(instr);
                optimizedloops = TRUE;
            }
        }
    }
}

static void skiplooptest(Loop* loop) {
    PCodeBlock* preheader;
    PCodeLabel* label;
    PCLink** ptr;
    PCLink* link;
    PCode* lastInstr;
    PCode* instr;

    preheader = loop->preheader;
    lastInstr = loop->body->lastPCode;
    CError_ASSERT(340, lastInstr->args[2].kind == PCOp_LABEL);

    label = lastInstr->args[2].data.label.label;
    preheader->lastPCode->args[0].data.label.label = label;
    preheader->successors->block = label->block;

    ptr = &loop->body->predecessors;
    while ((link = *ptr)) {
        if (link->block == preheader) {
            *ptr = link->nextLink;
            break;
        }
        ptr = &link->nextLink;
    }

    link->nextLink = label->block->predecessors;
    label->block->predecessors = link;

    while (1) {
        instr = loop->body->firstPCode;
        CError_ASSERT(369, instr);

        if (instr->op == PC_CMP ||
            instr->op == PC_CMPI ||
            instr->op == PC_CMPLI ||
            instr->op == PC_CMPL) {
            break;
        }

        deletepcode(instr);
        insertpcodebefore(loop->preheader->lastPCode, instr);
        loop->bodySize--;
    }

    for (instr = instr->nextPCode; instr && !(instr->flags & fIsBranch); instr = instr->nextPCode) {
        insertpcodebefore(loop->preheader->lastPCode, copypcode(instr));
    }
}

static void unrollloop(Loop* loop) {
    PCodeBlock* newBlock;
    int i;
    int factor;
    PCode* instr;
    PCodeBlock* block;
    PCode* firstInstr;
    PCode* nextInstr;
    PCode* blockInstr;
    PCode* bodyInstr;

    for (factor = copts.unroll_factor_limit; factor > 1; factor--) {
        if ((loop->iterationCount % factor) == 0 &&
            (loop->bodySize - 2) * factor <= copts.unroll_instr_limit) {
            break;
        }
    }

    if (factor == 1) {
        return;
    }
    if ((loop->iterationCount / factor) != 1 && loop->bodySize < 4) {
        return;
    }

    newBlock = oalloc(sizeof(PCodeBlock));
    newBlock->firstPCode = newBlock->lastPCode = NULL;
    for (i = 0; i < factor - 1; i++) {
        firstInstr = loop->body->firstPCode;
        CError_ASSERT(448, firstInstr);
        if (firstInstr->op != PC_CMP &&
            firstInstr->op != PC_CMPL &&
            firstInstr->op != PC_CMPI &&
            firstInstr->op != PC_CMPLI) {
            CError_FATAL(450);
        }

        for (bodyInstr = firstInstr->nextPCode; bodyInstr && !(bodyInstr->flags & fIsBranch);
             bodyInstr = bodyInstr->nextPCode) {
            appendpcode(newBlock, copypcode(bodyInstr));
        }

        for (block = loop->preheader->successors->block; block != loop->body;
             block = block->successors->block) {
            for (blockInstr = block->firstPCode; blockInstr; blockInstr = blockInstr->nextPCode) {
                if (blockInstr->op != PC_B) {
                    appendpcode(newBlock, copypcode(blockInstr));
                }
            }
        }
    }

    block = loop->body->predecessors->block;
    for (instr = newBlock->firstPCode; instr; instr = nextInstr) {
        nextInstr = instr->nextPCode;
        appendpcode(block, instr);
    }
    loop->iterationCount /= factor;
}

void pccomputepredecessors1(PCodeBlock* block) {
    PCLink* succ;
    PCLink* pred;

    for (succ = block->successors; succ; succ = succ->nextLink) {
        if (!succ->block) {
            CError_FATAL(496);
        } else {
            for (pred = succ->block->predecessors; pred; pred = pred->nextLink) {
                if (pred->block == block) {
                    break;
                }
            }

            if (!pred) {
                pred = lalloc(sizeof(PCLink));
                pred->block = block;
                pred->nextLink = succ->block->predecessors;
                succ->block->predecessors = pred;
            }
        }
    }
}

static PCodeBlock* insertnewpcblock(PCodeBlock* block, int loopWeight) {
    PCodeLabel* label;
    PCodeBlock* newBlock;
    PCodeBlock* nextBlock;
    PCLink* prev;
    PCLink* link;

    label = makepclabel();
    newBlock = lalloc(sizeof(PCodeBlock));

    nextBlock = block->nextBlock;
    newBlock->nextBlock = nextBlock;
    block->nextBlock = newBlock;

    newBlock->prevBlock = block;
    nextBlock->prevBlock = newBlock;

    newBlock->labels = NULL;
    newBlock->predecessors = newBlock->successors = NULL;
    newBlock->firstPCode = newBlock->lastPCode = NULL;
    newBlock->pcodeCount = 0;
    newBlock->loopWeight = loopWeight;
    newBlock->flags = 0;
    newBlock->blockIndex = pcblockcount++;
    pclabel(newBlock, label);

    prev = NULL;
    for (link = block->successors; link; link = link->nextLink) {
        if (link->block == nextBlock) {
            if (!prev) {
                block->successors = link->nextLink;
            } else {
                prev->nextLink = link->nextLink;
            }
            link->nextLink = NULL;
            newBlock->successors = link;
            break;
        }
        prev = link;
    }

    prev = NULL;
    for (link = nextBlock->predecessors; link; link = link->nextLink) {
        if (link->block == block) {
            if (!prev) {
                nextBlock->predecessors = link->nextLink;
            } else {
                prev->nextLink = link->nextLink;
            }
            link->nextLink = NULL;
            newBlock->predecessors = link;
            break;
        }
        prev = link;
    }

    link = lalloc(sizeof(PCLink));
    link->block = newBlock;
    link->nextLink = block->successors;
    block->successors = link;

    link = lalloc(sizeof(PCLink));
    link->block = newBlock;
    link->nextLink = nextBlock->predecessors;
    nextBlock->predecessors = link;

    return newBlock;
}

static void unrollloopconditional(Loop* loop) {
    PCodeBlock* lastBlock;
    PCodeLabel* bodyLabel;
    PCode* instr;
    PCode* instrCopy;
    int outputBlockCount;
    int inputBlockCount;
    PCodeBlock** blocks;
    PCodeBlock** blocks2;
    PCodeBlock** blocks3;
    PCode* firstInstr;
    int factor;
    PCodeBlock* block;
    PCLink* link;
    PCLink* prev;
    int i;
    int j;
    int k;
    int instructionCount;

    bodyLabel = NULL;
    outputBlockCount = 0;
    instructionCount = 0;

    for (factor = copts.unroll_factor_limit; factor > 1; factor--) {
        if ((loop->iterationCount % factor) == 0 &&
            (loop->bodySize - 2) * factor <= copts.unroll_instr_limit) {
            break;
        }
    }

    if (factor == 1) {
        return;
    }

    inputBlockCount = 0;
    for (block = loop->preheader->successors->block; block != loop->body;
         block = block->nextBlock) {
        inputBlockCount++;
        if (!bitvectorgetbit(block->blockIndex, loop->memberblocks)) {
            instructionCount += block->pcodeCount;
        }
    }

    if ((loop->bodySize - instructionCount - 2) < instructionCount || instructionCount > 8) {
        return;
    }

    blocks = oalloc(inputBlockCount * sizeof(PCodeBlock*));
    blocks2 = oalloc(inputBlockCount * sizeof(PCodeBlock*));
    blocks3 = oalloc(factor * (inputBlockCount * sizeof(PCodeBlock*)));
    memclrw(blocks, inputBlockCount * sizeof(PCodeBlock*));
    memclrw(blocks2, inputBlockCount * sizeof(PCodeBlock*));
    memclrw(blocks3, factor * (inputBlockCount * sizeof(PCodeBlock*)));

    block = loop->preheader->nextBlock;
    for (i = 0; i < inputBlockCount; i++) {
        blocks[i] = block;
        block = block->nextBlock;
    }

    lastBlock = blocks[inputBlockCount - 1];
    for (i = 0; i < factor - 1; i++) {
        for (j = 0; j < inputBlockCount; j++) {
            blocks2[j] = insertnewpcblock(lastBlock, loop->loopWeight);
            blocks3[outputBlockCount++] = blocks2[j];
            lastBlock = blocks2[j];
        }
        if (bodyLabel) {
            pclabel(blocks2[0], bodyLabel);
            bodyLabel = NULL;
        }

        for (j = 0; j < inputBlockCount; j++) {
            for (instr = blocks[j]->firstPCode; instr; instr = instr->nextPCode) {
                if (instr->flags & fIsBranch) {
                    PCodeArg* op;
                    int opID;
                    instrCopy = copypcode(instr);
                    op = NULL;
                    for (opID = 0; opID < instr->argCount; opID++) {
                        if (instr->args[opID].kind == PCOp_LABEL) {
                            op = &instr->args[opID];
                            break;
                        }
                    }

                    if (op) {
                        if (op->data.label.label->block == loop->body) {
                            if (!bodyLabel) {
                                bodyLabel = makepclabel();
                            }
                            instrCopy->args[opID].data.label.label = bodyLabel;
                        } else {
                            for (k = 0; k < inputBlockCount; k++) {
                                if (op->data.label.label->block == blocks[k]) {
                                    instrCopy->args[opID].data.label.label = blocks2[k]->labels;
                                    break;
                                }
                            }
                        }
                    }

                    appendpcode(blocks2[j], instrCopy);
                    if (op) {
                        pcbranch(blocks2[j], instrCopy->args[opID].data.label.label);
                    }
                } else {
                    appendpcode(blocks2[j], copypcode(instr));
                }
            }
        }

        firstInstr = loop->body->firstPCode;
        CError_ASSERT(762, firstInstr != NULL);
        if (firstInstr->op != PC_CMP &&
            firstInstr->op != PC_CMPL &&
            firstInstr->op != PC_CMPI &&
            firstInstr->op != PC_CMPLI) {
            CError_FATAL(764);
        }

        for (instr = firstInstr->nextPCode; instr && !(instr->flags & fIsBranch);
             instr = instr->nextPCode) {
            appendpcode(blocks2[inputBlockCount - 1], copypcode(instr));
        }

        for (j = 0; j < inputBlockCount; j++) {
            for (link = blocks[j]->successors; link; link = link->nextLink) {
                if (link->block == blocks[j]->nextBlock) {
                    break;
                }
            }

            if (!link) {
                for (link = blocks2[j]->successors, prev = NULL; link; link = link->nextLink) {
                    if (link->block == blocks2[j]->nextBlock) {
                        if (prev) {
                            prev->nextLink = link->nextLink;
                        } else {
                            blocks2[j]->successors = link->nextLink;
                        }
                    } else {
                        prev = link;
                    }
                }

                for (link = blocks2[j]->nextBlock->predecessors, prev = NULL; link;
                     link = link->nextLink) {
                    if (link->block == blocks2[j]) {
                        if (prev) {
                            prev->nextLink = link->nextLink;
                        } else {
                            blocks2[j]->nextBlock->predecessors = link->nextLink;
                        }
                    } else {
                        prev = link;
                    }
                }
            }
        }
    }

    if (bodyLabel) {
        pclabel(loop->body, bodyLabel);
    }

    for (i = 0; i < inputBlockCount; i++) {
        for (instr = blocks[i]->firstPCode; instr; instr = instr->nextPCode) {
            if (instr->flags & fIsBranch) {
                PCodeArg* op;
                int opID;
                op = NULL;
                for (opID = 0; opID < instr->argCount; opID++) {
                    if (instr->args[opID].kind == PCOp_LABEL) {
                        op = &instr->args[opID];
                        break;
                    }
                }

                if (op && op->data.label.label->block == loop->body) {
                    instr->args[opID].data.label.label = blocks3[0]->labels;

                    for (link = blocks[i]->successors, prev = NULL; link; link = link->nextLink) {
                        if (link->block == loop->body) {
                            if (prev) {
                                prev->nextLink = link->nextLink;
                            } else {
                                blocks[i]->successors = link->nextLink;
                            }
                        } else {
                            prev = link;
                        }
                    }

                    for (link = loop->body->predecessors, prev = NULL; link;
                         link = link->nextLink) {
                        if (link->block == blocks[i]) {
                            if (prev) {
                                prev->nextLink = link->nextLink;
                            } else {
                                loop->body->predecessors = link->nextLink;
                            }
                        } else {
                            prev = link;
                        }
                    }

                    link = blocks[i]->successors;
                    while (link && link->block != blocks3[0]) {
                        link = link->nextLink;
                    }

                    if (!link) {
                        pcbranch(blocks[i], op->data.label.label);
                        pccomputepredecessors1(blocks[i]);
                    }
                }
            }
        }
    }

    for (i = 0; i < outputBlockCount; i++) {
        pccomputepredecessors1(blocks3[i]);
    }

    loop->iterationCount /= factor;
}

static void unrollunknownBDNZ(Loop* loop) {
    int factor;
    PCodeBlock* preheader;
    PCodeBlock* exitBlock;
    PCodeBlock* postheader;
    PCodeBlock* newblock1;
    PCodeBlock* newblock2;
    PCodeBlock* newblock3;
    PCodeBlock* newblock4;
    PCodeBlock* newblock5;
    PCode* mtctr;
    short mtctr_reg;
    PCode* updateCopy;
    PCode* ivUse;
    PCodeBlock* block;
    PCode* instr;
    PCodeArg* op;
    int i;
    int val;
    short mtctr_shifted_reg;
    short incrementReg;
    PCLink* link;

    if (loop->bodySize < 4) {
        return;
    }

    factor = 128;
    while (factor > copts.unroll_factor_limit) {
        factor >>= 1;
    }
    while (factor > 1 && (loop->bodySize - 2) * factor > copts.unroll_instr_limit) {
        factor >>= 1;
    }

    if (factor < 2) {
        return;
    }

    preheader = loop->preheader;
    exitBlock = loop->body->nextBlock;
    postheader = loop->preheader->nextBlock;

    newblock1 = insertnewpcblock(preheader, loop->loopWeight);
    newblock2 = insertnewpcblock(newblock1, loop->loopWeight);
    newblock3 = insertnewpcblock(newblock2, loop->loopWeight);
    newblock4 = insertnewpcblock(newblock3, loop->loopWeight);
    newblock5 = insertnewpcblock(newblock4, loop->loopWeight);
    addblocktoloop(loop, newblock1);
    addblocktoloop(loop, newblock2);
    addblocktoloop(loop, newblock3);
    addblocktoloop(loop, newblock4);
    addblocktoloop(loop, newblock5);

    for (instr = preheader->lastPCode; instr; instr = instr->prevPCode) {
        if (instr->op == PC_MTCTR) {
            mtctr = instr;
            mtctr_reg = instr->args[0].data.reg.reg;
        }
    }

    if (!mtctr) {
        return;
    }

    updateCopy = NULL;
    for (block = postheader; block != loop->body; block = block->successors->block) {
        for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
            if (instr->op != PC_B) {
                appendpcode(newblock2, copypcode(instr));
                if (instr == loop->inductionUpdate) {
                    updateCopy = newblock2->lastPCode;
                }
            }
        }
    }

    if (!updateCopy) {
        appendpcode(newblock2, copypcode(loop->inductionUpdate));
        updateCopy = newblock2->lastPCode;
    }

    ivUse = NULL;
    for (instr = newblock2->firstPCode; instr; instr = instr->nextPCode) {
        if (instr != updateCopy) {
            op = instr->args;
            i = instr->argCount;
            while (i--) {
                if (op->kind == PCOp_REGISTER &&
                    op->arg == RegClass_GPR &&
                    op->data.reg.reg == loop->inductionUpdate->args[0].data.reg.reg &&
                    (op->data.reg.effect & (EffectRead | EffectWrite))) {
                    ivUse = instr;
                    break;
                }
                op++;
            }
        }
        if (ivUse) {
            break;
        }
    }

    if (!ivUse) {
        deletepcode(updateCopy);
        deletepcode(loop->inductionUpdate);
        if (loop->footer) {
            exitBlock = loop->footer;
        } else {
            exitBlock = insertnewpcblock(loop->body, loop->loopWeight);
        }
    } else {
        updateCopy = NULL;
    }

    for (i = 1; i < factor; i++) {
        for (block = postheader; block != loop->body; block = block->successors->block) {
            for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
                if (instr->op != PC_B) {
                    appendpcode(newblock2, copypcode(instr));
                }
            }
        }
    }

    mtctr_shifted_reg = used_virtual_registers[RegClass_GPR]++;
    appendpcode(
        newblock1,
        makepcode(
            PC_RLWINM, mtctr_shifted_reg, mtctr_reg, 32 - ispowerof2(factor), ispowerof2(factor), 31
        )
    );

    appendpcode(newblock1, makepcode(PC_CMPLI, 0, mtctr_shifted_reg, 0));

    if (updateCopy) {
        incrementReg = used_virtual_registers[RegClass_GPR]++;
        if (loop->step == 1) {
            instr = makepcode(PC_MR, incrementReg, mtctr_reg);
        } else if (loop->step == -1) {
            instr = makepcode(PC_NEG, incrementReg, mtctr_reg);
        } else {
            val = ispowerof2(abs(loop->step));
            if (val > 0) {
                instr = makepcode(PC_RLWINM, incrementReg, mtctr_reg, val, 0, 31 - val);
                if (loop->step < 0) {
                    appendpcode(newblock1, instr);
                    instr = makepcode(PC_NEG, incrementReg, incrementReg);
                }
            } else {
                instr = makepcode(PC_MULLI, incrementReg, mtctr_reg, loop->step);
            }
        }
        appendpcode(newblock1, instr);
    }

    appendpcode(newblock1, makepcode(PC_MTCTR, mtctr_shifted_reg));
    appendpcode(newblock1, makepcode(PC_BT, 0, 2, newblock5->labels));
    pcbranch(newblock1, newblock5->labels);

    link = lalloc(sizeof(PCLink));
    link->block = newblock1;
    link->nextLink = newblock5->predecessors;
    newblock5->predecessors = link;

    appendpcode(newblock3, makepcode(PC_BDNZ, newblock2->labels));
    pcbranch(newblock3, newblock2->labels);

    link = lalloc(sizeof(PCLink));
    link->block = newblock3;
    link->nextLink = newblock2->predecessors;
    newblock2->predecessors = link;

    appendpcode(newblock4, makepcode(PC_ANDI, mtctr_reg, mtctr_reg, factor - 1));
    appendpcode(newblock4, makepcode(PC_BT, 0, 2, exitBlock->labels));
    pcbranch(newblock4, exitBlock->labels);

    link = lalloc(sizeof(PCLink));
    link->block = newblock4;
    link->nextLink = exitBlock->predecessors;
    exitBlock->predecessors = link;

    deletepcode(mtctr);
    appendpcode(newblock5, mtctr);

    if (updateCopy && bitvectorgetbit(updateCopy->args[0].data.reg.reg, liveonexit)) {
        instr = makepcode(
            PC_ADD, updateCopy->args[0].data.reg.reg, updateCopy->args[0].data.reg.reg, incrementReg
        );
        if (exitBlock->firstPCode) {
            insertpcodebefore(exitBlock->firstPCode, instr);
        } else {
            appendpcode(exitBlock, instr);
        }
    }
}

static void deleteloop(Loop* loop) {
    PCodeBlock* body;
    PCodeBlock* preheader;
    PCodeBlock* nextblock;
    PCodeLabel* label;
    PCode* instr;
    PCLink** ptr;
    PCLink* link;
    PCodeBlock* block;

    body = loop->body;
    preheader = loop->preheader;
    nextblock = body->nextBlock;
    label = makepclabel();

    while (1) {
        instr = body->firstPCode;
        CError_ASSERT(1294, instr != NULL);

        if (instr->op == PC_CMP ||
            instr->op == PC_CMPI ||
            instr->op == PC_CMPLI ||
            instr->op == PC_CMPL) {
            break;
        }

        deletepcode(instr);
        insertpcodebefore(loop->preheader->lastPCode, instr);
        loop->bodySize--;
    }

    pclabel(nextblock, label);
    preheader->lastPCode->args[0].data.label.label = label;
    preheader->successors->block = nextblock;

    ptr = &nextblock->predecessors;
    while ((link = *ptr)) {
        if (link->block == body) {
            link->block = preheader;
            break;
        }
        ptr = &link->nextLink;
    }

    block = pcbasicblocks;
    while ((nextblock = block->nextBlock)) {
        if (bitvectorgetbit(nextblock->blockIndex, loop->memberblocks)) {
            block->nextBlock = nextblock->nextBlock;
        } else {
            block = nextblock;
        }
    }
}

static void deleteloopheader(Loop* loop) {
    PCLink* link;
    PCLink** ptr;

    ptr = &loop->body->successors;
    while ((link = *ptr)) {
        if (link->block == loop->body->lastPCode->args[2].data.label.label->block) {
            *ptr = link->nextLink;
            break;
        }
        ptr = &link->nextLink;
    }

    ptr = &loop->body->lastPCode->args[2].data.label.label->block->predecessors;
    while ((link = *ptr)) {
        if (link->block == loop->body) {
            *ptr = link->nextLink;
            break;
        }
        ptr = &link->nextLink;
    }

    deletepcode(loop->body->firstPCode);
    deletepcode(loop->body->lastPCode);
    optimizedloop_full_unroll = TRUE;
}

static void rewriteloopwithBDNZ(Loop* loop) {
    PCode* instr;

    if (!FITS_IN_SHORT(loop->iterationCount)) {
        instr = makepcode(
            PC_LIS, used_virtual_registers[RegClass_GPR]++, 0, HIGH_PART(loop->iterationCount)
        );
        insertpcodebefore(loop->preheader->lastPCode, instr);

        if (LOW_PART(loop->iterationCount) != 0) {
            insertpcodeafter(
                instr,
                makepcode(
                    PC_ADDI,
                    instr->args[0].data.reg.reg,
                    instr->args[0].data.reg.reg,
                    0,
                    LOW_PART(loop->iterationCount)
                )
            );
        }
    } else {
        instr = makepcode(PC_LI, used_virtual_registers[RegClass_GPR]++, loop->iterationCount);
        insertpcodebefore(loop->preheader->lastPCode, instr);
    }

    insertpcodebefore(loop->preheader->lastPCode, makepcode(PC_MTCTR, instr->args[0].data.reg.reg));

    instr = makepcode(PC_BDNZ, loop->body->lastPCode->args[2].data.label.label);
    deletepcode(loop->body->firstPCode);
    deletepcode(loop->body->lastPCode);
    appendpcode(loop->body, instr);

    for (loop = loop->parent; loop; loop = loop->parent) {
        loop->uses_count_register = TRUE;
    }
}

static void rewriteunknownloopwithBDNZ(Loop* loop) {
    SInt32 absStep;
    SInt32 value2;
    PCode* branch;
    PCode* lis;
    SInt32 value1;
    int branchOpcode;
    int branchCondition;
    int counterReg;
    int reg1;
    int reg2;
    int tmpReg;
    unsigned char mode;
    PCode* addiInstr;
    PCode* high;
    int lo;
    PCode* instr;
    PCodeBlock* afterLoop;
    PCLink* link;
    PCLink** ptr;

    absStep = abs(loop->step);
    afterLoop = NULL;
    addiInstr = loop->body->lastPCode->prevPCode;
    while (addiInstr->op == PC_ADDI) {
        deletepcode(addiInstr);
        if (loop->body->lastPCode->args[2].data.label.label->block->firstPCode) {
            insertpcodebefore(
                loop->body->lastPCode->args[2].data.label.label->block->firstPCode, addiInstr
            );
        } else {
            appendpcode(loop->body->lastPCode->args[2].data.label.label->block, addiInstr);
        }

        afterLoop = insertnewpcblock(loop->body, loop->loopWeight);
        appendpcode(afterLoop, copypcode(addiInstr));
        loop->footer = afterLoop;
        addiInstr = loop->body->lastPCode->prevPCode;
    }

    if (loop->unknownCondition == ELESS) {
        branchOpcode = PC_BF;
        if (loop->lowerType == LOOP_BOUND_CONSTANT) {
            branchCondition = 1;
            value1 = loop->lower;
            reg1 = addiInstr->args[2].data.reg.reg;
            value2 = absStep - 1 - loop->lower;
            mode = 0;
        } else if (loop->upperType == LOOP_BOUND_CONSTANT) {
            branchCondition = 0;
            value1 = loop->upper;
            reg1 = addiInstr->args[1].data.reg.reg;
            value2 = absStep - 1 + loop->upper;
            mode = 1;
        } else {
            branchCondition = 0;
            reg1 = addiInstr->args[1].data.reg.reg;
            reg2 = addiInstr->args[2].data.reg.reg;
            value2 = absStep - 1;
            mode = 2;
        }
    } else if (loop->unknownCondition == ELESSEQU) {
        branchOpcode = PC_BT;
        if (loop->lowerType == LOOP_BOUND_CONSTANT) {
            branchCondition = 0;
            value1 = loop->lower;
            reg1 = addiInstr->args[2].data.reg.reg;
            value2 = absStep - loop->lower;
            mode = 0;
        } else if (loop->upperType == LOOP_BOUND_CONSTANT) {
            branchCondition = 1;
            value1 = loop->upper;
            reg1 = addiInstr->args[1].data.reg.reg;
            value2 = loop->upper + absStep;
            mode = 1;
        } else {
            branchCondition = 1;
            value1 = 0;
            reg1 = addiInstr->args[1].data.reg.reg;
            reg2 = addiInstr->args[2].data.reg.reg;
            value2 = absStep;
            mode = 2;
        }
    } else if (loop->unknownCondition == EGREATER) {
        branchOpcode = PC_BF;
        if (loop->lowerType == LOOP_BOUND_CONSTANT) {
            branchCondition = 0;
            value1 = loop->lower;
            reg1 = addiInstr->args[2].data.reg.reg;
            value2 = absStep - 1 + loop->lower;
            mode = 1;
        } else if (loop->upperType == LOOP_BOUND_CONSTANT) {
            branchCondition = 1;
            value1 = loop->upper;
            reg1 = addiInstr->args[1].data.reg.reg;
            value2 = absStep - 1 - loop->upper;
            mode = 0;
        } else {
            branchCondition = 1;
            value1 = 0;
            reg1 = addiInstr->args[1].data.reg.reg;
            reg2 = addiInstr->args[2].data.reg.reg;
            value2 = absStep - 1;
            mode = 3;
        }
    } else if (loop->unknownCondition == EGREATEREQU) {
        branchOpcode = PC_BT;
        if (loop->lowerType == LOOP_BOUND_CONSTANT) {
            branchCondition = 1;
            value1 = loop->lower;
            reg1 = addiInstr->args[2].data.reg.reg;
            value2 = loop->lower + absStep;
            mode = 1;
        } else if (loop->upperType == LOOP_BOUND_CONSTANT) {
            branchCondition = 0;
            value1 = loop->upper;
            reg1 = addiInstr->args[1].data.reg.reg;
            value2 = absStep - loop->upper;
            mode = 0;
        } else {
            branchCondition = 0;
            reg1 = addiInstr->args[1].data.reg.reg;
            reg2 = addiInstr->args[2].data.reg.reg;
            value2 = absStep;
            mode = 3;
        }
    } else if (loop->unknownCondition == ENOTEQU) {
        branchOpcode = PC_BT;
        branchCondition = 2;
        if (loop->step > 0) {
            if (loop->lowerType == LOOP_BOUND_CONSTANT) {
                value1 = loop->lower;
                reg1 = addiInstr->args[2].data.reg.reg;
                value2 = absStep - 1 - loop->lower;
                mode = 0;
            } else if (loop->upperType == LOOP_BOUND_CONSTANT) {
                value1 = loop->upper;
                reg1 = addiInstr->args[1].data.reg.reg;
                value2 = absStep - 1 + loop->upper;
                mode = 1;
            } else {
                reg1 = addiInstr->args[1].data.reg.reg;
                reg2 = addiInstr->args[2].data.reg.reg;
                value2 = absStep - 1;
                mode = 2;
            }
        } else {
            if (loop->lowerType == LOOP_BOUND_CONSTANT) {
                value1 = loop->lower;
                reg1 = addiInstr->args[2].data.reg.reg;
                value2 = absStep - 1 + loop->lower;
                mode = 1;
            } else if (loop->upperType == LOOP_BOUND_CONSTANT) {
                value1 = loop->upper;
                reg1 = addiInstr->args[1].data.reg.reg;
                value2 = absStep - 1 - loop->upper;
                mode = 0;
            } else {
                reg1 = addiInstr->args[1].data.reg.reg;
                reg2 = addiInstr->args[2].data.reg.reg;
                value2 = absStep - 1;
                mode = 3;
            }
        }
    }

    while (1) {
        instr = loop->body->firstPCode;
        CError_ASSERT(1857, instr);

        if (instr->op == PC_CMP ||
            instr->op == PC_CMPI ||
            instr->op == PC_CMPLI ||
            instr->op == PC_CMPL) {
            break;
        }
        deletepcode(instr);
        insertpcodebefore(loop->preheader->lastPCode, instr);
        loop->bodySize--;
    }

    counterReg = used_virtual_registers[RegClass_GPR]++;
    if (mode == 1) {
        if (value2 == 0) {
            insertpcodebefore(loop->preheader->lastPCode, makepcode(PC_NEG, counterReg, reg1));
        } else if (FITS_IN_SHORT(value2)) {
            insertpcodebefore(
                loop->preheader->lastPCode, makepcode(PC_SUBFIC, counterReg, reg1, value2)
            );
        } else {
            insertpcodebefore(
                loop->preheader->lastPCode, makepcode(PC_LIS, counterReg, 0, HIGH_PART(value2))
            );
            if (LOW_PART(value2)) {
                insertpcodebefore(
                    loop->preheader->lastPCode,
                    makepcode(PC_ADDI, counterReg, counterReg, 0, LOW_PART(value2))
                );
            }
            insertpcodebefore(
                loop->preheader->lastPCode, makepcode(PC_SUBF, counterReg, reg1, counterReg)
            );
        }
    } else if (mode == 0) {
        if (value2 == 0) {
            insertpcodebefore(loop->preheader->lastPCode, makepcode(PC_MR, counterReg, reg1));
        } else if (FITS_IN_SHORT(value2)) {
            insertpcodebefore(
                loop->preheader->lastPCode, makepcode(PC_ADDI, counterReg, reg1, 0, value2)
            );
        } else {
            insertpcodebefore(
                loop->preheader->lastPCode,
                makepcode(PC_ADDIS, counterReg, reg1, 0, HIGH_PART(value2))
            );
            if (LOW_PART(value2)) {
                insertpcodebefore(
                    loop->preheader->lastPCode,
                    makepcode(PC_ADDI, counterReg, counterReg, 0, LOW_PART(value2))
                );
            }
        }
    } else if (mode == 2) {
        if (value2 == 0) {
            insertpcodebefore(
                loop->preheader->lastPCode, makepcode(PC_SUBF, counterReg, reg1, reg2)
            );
        } else if (FITS_IN_SHORT(value2)) {
            insertpcodebefore(
                loop->preheader->lastPCode, makepcode(PC_ADDI, counterReg, reg2, 0, value2)
            );
            insertpcodebefore(
                loop->preheader->lastPCode, makepcode(PC_SUBF, counterReg, reg1, counterReg)
            );
        } else {
            insertpcodebefore(
                loop->preheader->lastPCode,
                makepcode(PC_ADDIS, counterReg, reg2, 0, HIGH_PART(value2))
            );
            if (LOW_PART(value2)) {
                insertpcodebefore(
                    loop->preheader->lastPCode,
                    makepcode(PC_ADDI, counterReg, counterReg, 0, LOW_PART(value2))
                );
            }
            insertpcodebefore(
                loop->preheader->lastPCode, makepcode(PC_SUBF, counterReg, reg1, counterReg)
            );
        }
    } else {
        if (value2 == 0) {
            insertpcodebefore(
                loop->preheader->lastPCode, makepcode(PC_SUBF, counterReg, reg2, reg1)
            );
        } else {
            if (FITS_IN_SHORT(value2)) {
                insertpcodebefore(
                    loop->preheader->lastPCode, makepcode(PC_ADDI, counterReg, reg1, 0, value2)
                );
            } else {
                insertpcodebefore(
                    loop->preheader->lastPCode,
                    makepcode(PC_ADDIS, counterReg, reg1, 0, HIGH_PART(value2))
                );
                if (LOW_PART(value2)) {
                    insertpcodebefore(
                        loop->preheader->lastPCode,
                        makepcode(PC_ADDI, counterReg, counterReg, 0, LOW_PART(value2))
                    );
                }
            }
            insertpcodebefore(
                loop->preheader->lastPCode, makepcode(PC_SUBF, counterReg, reg2, counterReg)
            );
        }
    }

    if (absStep > 1) {
        unsigned char bits;
        if ((bits = ispowerof2(absStep))) {
            insertpcodebefore(
                loop->preheader->lastPCode,
                makepcode(PC_RLWINM, counterReg, counterReg, 32 - bits, bits, 31)
            );
        } else {
            tmpReg = used_virtual_registers[RegClass_GPR]++;
            insertpcodebefore(loop->preheader->lastPCode, makepcode(PC_LI, tmpReg, absStep));
            insertpcodebefore(
                loop->preheader->lastPCode, makepcode(PC_DIVWU, counterReg, counterReg, tmpReg)
            );
        }
    }

    insertpcodebefore(loop->preheader->lastPCode, makepcode(PC_MTCTR, counterReg));

    if (mode < 2) {
        if (addiInstr->op == PC_CMPL || addiInstr->op == PC_CMPLI) {
            if (FITS_IN_USHORT(value1)) {
                insertpcodebefore(loop->preheader->lastPCode, makepcode(PC_CMPLI, 0, reg1, value1));
            } else {
                high =
                    makepcode(PC_LIS, used_virtual_registers[RegClass_GPR]++, 0, HIGH_PART(value1));
                insertpcodebefore(loop->preheader->lastPCode, high);
                if (LOW_PART(loop->iterationCount) != 0) {
                    insertpcodeafter(
                        high,
                        makepcode(
                            PC_ADDI,
                            high->args[0].data.reg.reg,
                            high->args[0].data.reg.reg,
                            0,
                            LOW_PART(value1)
                        )
                    );
                }
                insertpcodebefore(
                    loop->preheader->lastPCode,
                    makepcode(PC_CMPL, 0, reg1, high->args[0].data.reg.reg)
                );
            }
        } else {
            lo = LOW_PART(value1);
            if (value1 == lo) {
                insertpcodebefore(loop->preheader->lastPCode, makepcode(PC_CMPI, 0, reg1, value1));
            } else {
                lis =
                    makepcode(PC_LIS, used_virtual_registers[RegClass_GPR]++, 0, HIGH_PART(value1));
                insertpcodebefore(loop->preheader->lastPCode, lis);
                if (LOW_PART(loop->iterationCount) != 0) {
                    insertpcodeafter(
                        lis,
                        makepcode(
                            PC_ADDI, lis->args[0].data.reg.reg, lis->args[0].data.reg.reg, 0, lo
                        )
                    );
                }
                insertpcodebefore(
                    loop->preheader->lastPCode,
                    makepcode(PC_CMP, 0, reg1, lis->args[0].data.reg.reg)
                );
            }
        }
    } else {
        if (addiInstr->op == PC_CMPL || addiInstr->op == PC_CMPLI) {
            insertpcodebefore(loop->preheader->lastPCode, makepcode(PC_CMPL, 0, reg1, reg2));
        } else {
            insertpcodebefore(loop->preheader->lastPCode, makepcode(PC_CMP, 0, reg1, reg2));
        }
    }

    if (!afterLoop) {
        afterLoop = loop->body->nextBlock;
    }
    instr = makepcode(branchOpcode, 0, branchCondition, afterLoop->labels);
    deletepcode(loop->preheader->lastPCode);
    appendpcode(loop->preheader, instr);
    branch = makepcode(PC_BDNZ, loop->body->lastPCode->args[2].data.label.label);
    deletepcode(loop->body->firstPCode);
    deletepcode(loop->body->lastPCode);
    appendpcode(loop->body, branch);

    loop->preheader->successors = NULL;
    ptr = &loop->body->predecessors;
    while ((link = *ptr)) {
        if (link->block == loop->preheader) {
            *ptr = link->nextLink;
            break;
        }
        ptr = &link->nextLink;
    }

    link = lalloc(sizeof(PCLink));
    link->block = loop->preheader->nextBlock;
    link->nextLink = loop->preheader->successors;
    loop->preheader->successors = link;

    link = lalloc(sizeof(PCLink));
    link->block = loop->preheader;
    link->nextLink = loop->preheader->nextBlock->predecessors;
    loop->preheader->nextBlock->predecessors = link;

    link = lalloc(sizeof(PCLink));
    link->block = afterLoop;
    link->nextLink = loop->preheader->successors;
    loop->preheader->successors = link;

    link = lalloc(sizeof(PCLink));
    link->block = loop->preheader;
    link->nextLink = afterLoop->predecessors;
    afterLoop->predecessors = link;

    for (loop = loop->parent; loop; loop = loop->parent) {
        loop->uses_count_register = TRUE;
    }
}

static void optimizeloop(Loop* loop) {
    computeliveonexit(loop);

    if (loop->has_indexed_load || loop->has_indexed_store) {
        insertupdateinstructions(loop);
    }

    if (loop->isKnownCountingLoop) {
        if (loop->iterationCount > 0) {
            skiplooptest(loop);
            if (!copts.optimizesize && !loop->has_call && !loop->has_memory_barrier) {
                if (loop->is_straight_line) {
                    unrollloop(loop);
                } else if (!loop->uses_count_register) {
                    unrollloopconditional(loop);
                }
            }
        }

        if (loop->iterationCount != 0) {
            if (loop->iterationCount == 1) {
                deleteloopheader(loop);
            } else if (!loop->uses_count_register && !loop->has_call) {
                rewriteloopwithBDNZ(loop);
            }
        }

        optimizedloops = TRUE;
    } else if (loop->isUnknownCountingLoop && !loop->uses_count_register && !loop->has_call) {
        rewriteunknownloopwithBDNZ(loop);
        if (copts.unroll_speculative && !copts.optimizesize) {
            if (loop->is_straight_line && !loop->has_memory_barrier && !loop->has_unrolled_code) {
                unrollunknownBDNZ(loop);
            }
        }
        optimizedloops = TRUE;
    }

    eliminateinductionvariables(loop);
}

typedef struct LocalArray {
    struct LocalArray* next;
    Object* object;
    unsigned int invalid : 1;
    unsigned int isFloat : 1;
    unsigned int isSigned : 1;
    SInt32 elementSize;
    SInt32 arraySize;
    SInt32 elementCount;
    SInt32 totalUses;
    SInt32 elements[1];
} LocalArray;

static LocalArray* scanforlocalarrays(void) {
    LocalArray* head;
    LocalArray* array;
    ObjectList* list;
    int i;
    SInt32 arraySize;
    SInt32 elementSize;
    SInt32 elementCount;

    head = NULL;

    for (list = locals; list; list = list->next) {
        if (list->object &&
            !(IS_TYPE_POINTER(list->object->type) ? (TPTR_QUAL(list->object->type) & Q_VOLATILE)
                                                  : (list->object->qual & Q_VOLATILE)) &&
            list->object->type &&
            IS_TYPE_ARRAY(list->object->type) &&
            TPTR_TARGET(list->object->type) &&
            TPTR_TARGET(list->object->type)->type < TYPESTRUCT) {
            arraySize = list->object->type->size;
            elementSize = TPTR_TARGET(list->object->type)->size;
            elementCount = arraySize / elementSize;
            if (elementCount > 0 && elementCount <= 8) {
                array = oalloc(sizeof(int) * (elementCount - 1) + sizeof(LocalArray));
                array->next = head;
                head = array;

                array->object = list->object;
                array->elementSize = elementSize;
                array->arraySize = arraySize;
                array->elementCount = arraySize / elementSize;
                array->totalUses = 0;
                array->isSigned = TRUE;
                array->isFloat =
                    !copts.soft_float && IS_TYPE_FLOAT(TPTR_TARGET(list->object->type));
                array->invalid = FALSE;
                array->isSigned = TRUE;
                if (!array->isFloat && is_unsigned(TPTR_TARGET(list->object->type))) {
                    array->isSigned = FALSE;
                }

                for (i = 0; i < elementCount; i++) {
                    array->elements[i] = 0;
                }
            }
        }
    }

    return head;
}

static LocalArray* lookup_array_object(LocalArray* arrays, Object* object) {
    while (arrays) {
        if (arrays->object == object) {
            return arrays;
        }
        arrays = arrays->next;
    }
    return NULL;
}

void changearraytoregisters(void) {
    LocalArray* arrays;
    PCodeBlock* block;
    PCode* instr;
    int i;
    PCodeArg* op;
    LocalArray* array;
    LocalArray** ptr;
    int intCounter;
    int reg;
    int reg2;
    PCode* newInstr;
    int floatCounter;
    LocalArray* best;
    int score;
    int score2;

    arrays = NULL;
    if ((arrays = scanforlocalarrays())) {
        for (block = pcbasicblocks; block; block = block->nextBlock) {
            for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
                if (!(instr->flags & fIsBranch) && instr->argCount) {
                    op = instr->args;
                    i = instr->argCount;
                    while (i--) {
                        if (op->kind == PCOp_MEMORY && (RefType)op->arg == RefType_Local16) {
                            array = lookup_array_object(arrays, op->data.mem.obj);
                            if (array && !array->invalid) {
                                if ((instr->flags & (fIsRead | fIsWrite)) &&
                                    op->data.mem.offset % array->elementSize == 0 &&
                                    op->data.mem.offset < array->arraySize) {
                                    switch (instr->op) {
                                        case PC_LBZ:
                                        case PC_STB:
                                            if (array->elementSize != 1) {
                                                array->invalid = TRUE;
                                            }
                                            break;
                                        case PC_LHZ:
                                        case PC_LHA:
                                        case PC_STH:
                                            if (array->elementSize != 2) {
                                                array->invalid = TRUE;
                                            }
                                            break;
                                        case PC_LWZ:
                                        case PC_STW:
                                            if (array->elementSize != 4) {
                                                array->invalid = TRUE;
                                            }
                                            break;
                                        case PC_LFD:
                                        case PC_STFD:
                                            if (array->elementSize != 8) {
                                                array->invalid = TRUE;
                                            }
                                            break;
                                        default:
                                            array->invalid = TRUE;
                                    }
                                    if (!array->invalid) {
                                        array->elements[op->data.mem.offset / array->elementSize]++;
                                    }
                                } else {
                                    array->invalid = TRUE;
                                }
                            }
                        }
                        op++;
                    }
                }
            }
        }
        intCounter = 0;
        floatCounter = 0;
        ptr = &arrays;
        array = *ptr;
        while (array) {
            if (array->invalid) {
                *ptr = array->next;
                array = *ptr;
                continue;
            }

            if (array->isFloat) {
                floatCounter += array->elementCount;
            } else {
                intCounter += array->elementCount;
            }

            for (i = 0; i < array->elementCount; i++) {
                array->totalUses += array->elements[i];
            }

            array = array->next;
        }
        if (arrays) {
            while (intCounter > 8) {
                score = 0;
                best = NULL;
                for (array = arrays; array; array = array->next) {
                    if (!array->isFloat) {
                        if (best) {
                            if (array->totalUses < score) {
                                score = array->totalUses;
                                best = array;
                            }
                        } else {
                            best = array;
                            score = array->totalUses;
                        }
                    }
                }
                if (!best) {
                    break;
                }
                if (best == arrays) {
                    arrays = best->next;
                } else {
                    for (array = arrays; array; array = array->next) {
                        if (array->next == best) {
                            array->next = best->next;
                            break;
                        }
                    }
                }
                intCounter -= best->elementCount;
            }
            while (floatCounter > 8) {
                score2 = 0;
                best = NULL;
                for (array = arrays; array; array = array->next) {
                    if (array->isFloat) {
                        if (best) {
                            if (array->totalUses < score2) {
                                score2 = array->totalUses;
                                best = array;
                            }
                        } else {
                            best = array;
                            score2 = array->totalUses;
                        }
                    }
                }
                if (!best) {
                    break;
                }
                if (best == arrays) {
                    arrays = best->next;
                } else {
                    for (array = arrays; array; array = array->next) {
                        if (array->next == best) {
                            array->next = best->next;
                            break;
                        }
                    }
                }
                floatCounter -= best->elementCount;
            }
            CError_ASSERT(2394, intCounter <= 8 && floatCounter <= 8);
            if (!(array = arrays)) {
                return;
            }
            optimizedloop_trans_regs = TRUE;
            for (; array; array = array->next) {
                for (i = 0; i < array->elementCount; i++) {
                    if (array->isFloat) {
                        array->elements[i] = used_virtual_registers[RegClass_FPR]++;
                    } else {
                        array->elements[i] = used_virtual_registers[RegClass_GPR]++;
                    }
                }
            }
            for (block = pcbasicblocks; block; block = block->nextBlock) {
                for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
                    if (!(instr->flags & fIsBranch) &&
                        instr->argCount &&
                        (instr->flags & (fIsRead | fIsWrite)) &&
                        instr->args[2].kind == PCOp_MEMORY &&
                        (RefType)instr->args[2].arg == RefType_Local16) {
                        array = lookup_array_object(arrays, instr->args[2].data.mem.obj);
                        if (array && !array->invalid) {
                            reg2 = instr->args[0].data.reg.reg;
                            reg =
                                array
                                    ->elements[instr->args[2].data.mem.offset / array->elementSize];
                            newInstr = NULL;
                            switch (instr->op) {
                                case PC_LBZ:
                                    if (array->isSigned) {
                                        newInstr = makepcode(PC_MR, reg2, reg);
                                    } else {
                                        newInstr = makepcode(PC_RLWINM, reg2, reg, 0, 24, 31);
                                    }
                                    break;
                                case PC_STB:
                                    if (array->isSigned) {
                                        newInstr = makepcode(PC_EXTSB, reg, reg2);
                                    } else {
                                        newInstr = makepcode(PC_RLWINM, reg, reg2, 0, 24, 31);
                                    }
                                    break;
                                case PC_LHZ:
                                    newInstr = makepcode(PC_RLWINM, reg2, reg, 0, 16, 31);
                                    break;
                                case PC_LHA:
                                    newInstr = makepcode(PC_EXTSH, reg2, reg);
                                    break;
                                case PC_STH:
                                    if (array->isSigned) {
                                        newInstr = makepcode(PC_EXTSH, reg, reg2);
                                    } else {
                                        newInstr = makepcode(PC_RLWINM, reg, reg2, 0, 16, 31);
                                    }
                                    break;
                                case PC_LWZ:
                                    newInstr = makepcode(PC_MR, reg2, reg);
                                    break;
                                case PC_STW:
                                    newInstr = makepcode(PC_MR, reg, reg2);
                                    break;
                                case PC_LFD:
                                    newInstr = makepcode(PC_FMR, reg2, reg);
                                    break;
                                case PC_STFD:
                                    newInstr = makepcode(PC_FMR, reg, reg2);
                                    break;
                                default:
                                    CError_FATAL(2494);
                            }
                            if (newInstr) {
                                insertpcodebefore(instr, newInstr);
                                deletepcode(instr);
                                instr = newInstr;
                            }
                        }
                    }
                }
            }
        }
    }
    freeoheap();
}

static void optimizenestedloops(Loop* loop) {
    while (loop) {
        if (loop->children) {
            optimizenestedloops(loop->children);
        }
        optimizeloop(loop);
        loop = loop->nextSibling;
    }
}

void optimizeloops(void) {
    optimizedloops = FALSE;
    optimizedloop_full_unroll = FALSE;
    optimizedloop_trans_regs = FALSE;
    if (loopsinflowgraph) {
        computeusedefchains(FALSE);
        last_virtual_GPR = used_virtual_registers[RegClass_GPR];
        liveonexit = oalloc(4 * ((used_virtual_registers[RegClass_GPR] + 31) >> 5));
        inductionvariables = oalloc(4 * ((last_virtual_GPR + 31) >> 5));
        optimizenestedloops(loopsinflowgraph);
        freeoheap();
    }
}
