// LoopDetection.c: finds the natural loops of the flow graph and their preheaders, and the
// counting loops among them.

#include "compiler/LoopDetection.h"
#include "compiler/BitVectors.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CompilerTools.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/TOC.h"
#include "compiler/UseDefChains.h"

Loop* loopsinflowgraph;
int loopdetection_nblocks;
static UInt32** dominators;
static BlockList* loopheaders;
static int nloopheaders;
static PCodeBlock** loopstack;
// the front end's loop unroller's (IroUnrollLoop.c)
BitVector* LoopTemp;
struct LoopList* LoopList_First;

static void computedominators(void) {
    SInt32 blockCount;
    PCLink* link;
    UInt32* myvec;
    PCodeBlock* block;
    SInt32 i;
    int changed; // NOTE: invented name

    blockCount = pcblockcount;
    changed = TRUE;

    dominators = oalloc(sizeof(UInt32*) * pcblockcount);
    for (i = 0; i < pcblockcount; i++) {
        dominators[i] = oalloc(4 * ((blockCount + 31) >> 5));
    }

    myvec = oalloc(4 * ((blockCount + 31) >> 5));

    bitvectorinitialize(dominators[pcbasicblocks->blockIndex], blockCount, 0);
    bitvectorsetbit(0, dominators[pcbasicblocks->blockIndex]);

    for (block = pcbasicblocks->nextBlock; block; block = block->nextBlock) {
        bitvectorinitialize(dominators[block->blockIndex], blockCount, 0xFFFFFFFF);
    }

    computedepthfirstordering();

    while (changed) {
        changed = FALSE;
        for (i = 0; i < pcblockcount; i++) {
            block = depthfirstordering[i];
            if (block && block->blockIndex != pcbasicblocks->blockIndex) {
                bitvectorcopy(
                    myvec, dominators[block->predecessors->block->blockIndex], blockCount
                );
                for (link = block->predecessors->nextLink; link; link = link->nextLink) {
                    bitvectorintersect(myvec, dominators[link->block->blockIndex], blockCount);
                }
                bitvectorsetbit(block->blockIndex, myvec);

                if (bitvectorchanged(dominators[block->blockIndex], myvec, blockCount)) {
                    changed = TRUE;
                }
            }
        }
    }
}

static BlockList* findloopheaders(void) {
    PCodeBlock* block;
    PCLink* link;
    BlockList* list;

    loopheaders = NULL;
    nloopheaders = 0;

    for (block = pcbasicblocks->nextBlock; block; block = block->nextBlock) {
        for (link = block->predecessors; link; link = link->nextLink) {
            if (bitvectorgetbit(block->blockIndex, dominators[link->block->blockIndex])) {
                break;
            }
        }

        if (link) {
            list = oalloc(sizeof(BlockList));
            list->block = block;
            list->next = loopheaders;
            loopheaders = list;
            nloopheaders++;
        }
    }

    return loopheaders;
}

void addblocktoloop(Loop* loop, PCodeBlock* block) {
    BlockList* list = lalloc(sizeof(BlockList));

    bitvectorsetbit(block->blockIndex, loop->memberblocks);

    list->block = block;
    list->next = loop->blocks;
    loop->blocks = list;
}

static void findnaturalloop(Loop* loop) {
    BlockList* list2;
    BlockList* list;
    PCLink* link;
    int i;

    i = 0;
    addblocktoloop(loop, loop->body);
    for (link = loop->body->predecessors; link; link = link->nextLink) {
        if (bitvectorgetbit(loop->body->blockIndex, dominators[link->block->blockIndex]) &&
            link->block != loop->body) {
            addblocktoloop(loop, link->block);
            loopstack[i++] = link->block;
        }
    }

    while (i) {
        link = loopstack[--i]->predecessors;
        while (link) {
            if (!bitvectorgetbit(link->block->blockIndex, loop->memberblocks)) {
                addblocktoloop(loop, link->block);
                loopstack[i++] = link->block;
            }
            link = link->nextLink;
        }
    }

    for (list = loop->blocks; list; list = list->next) {
        // walks the successors (PCLinks) with the BlockList walker: the two have one layout
        for (list2 = (BlockList*)list->block->successors; list2; list2 = list2->next) {
            if (!bitvectorgetbit(list2->block->blockIndex, loop->memberblocks)) {
                bitvectorsetbit(list->block->blockIndex, loop->exitblocks);
                break;
            }
        }
    }

    for (list = loop->blocks; list; list = list->next) {
        for (list2 = loop->blocks; list2; list2 = list2->next) {
            if (bitvectorgetbit(list2->block->blockIndex, loop->exitblocks) &&
                !bitvectorgetbit(list->block->blockIndex, dominators[list2->block->blockIndex])) {
                break;
            }
        }

        if (!list2) {
            bitvectorsetbit(list->block->blockIndex, loop->exit_dominators);
        }
    }

    for (list = loop->blocks; list; list = list->next) {
        for (link = loop->body->predecessors; link; link = link->nextLink) {
            if (bitvectorgetbit(link->block->blockIndex, loop->memberblocks) &&
                !bitvectorgetbit(list->block->blockIndex, dominators[link->block->blockIndex])) {
                break;
            }
        }

        if (!link) {
            bitvectorsetbit(list->block->blockIndex, loop->backedge_dominators);
        }
    }
}

static void addlooptolist(Loop* loop, Loop** list) {
    Loop** scan;
    Loop* scanloop;

    scan = list;
    while ((scanloop = *scan)) {
        if (bitvectorgetbit(loop->body->blockIndex, scanloop->memberblocks)) {
            loop->parent = scanloop;
            addlooptolist(loop, &scanloop->children);
            return;
        }

        if (bitvectorgetbit(scanloop->body->blockIndex, loop->memberblocks)) {
            *scan = scanloop->nextSibling;
            scanloop->parent = loop;
            scanloop->nextSibling = loop->children;
            loop->children = scanloop;
        } else {
            scan = &scanloop->nextSibling;
        }
    }

    loop->nextSibling = *list;
    *list = loop;
}

static void findnaturalloops(void) {
    Loop* loop;

    loopdetection_nblocks = pcblockcount + 5 * nloopheaders;
    loopstack = oalloc(sizeof(PCodeBlock*) * pcblockcount);
    while (loopheaders) {
        loop = lalloc(sizeof(Loop));
        loop->parent = loop->nextSibling = loop->children = NULL;
        loop->body = loopheaders->block;
        loop->preheader = NULL;
        loop->blocks = NULL;
        loop->basicInductionVars = NULL;
        loop->footer = NULL;
        loop->inductionUpdate = NULL;
        loop->loopWeight = loop->body->loopWeight;

        bitvectorinitialize(
            loop->memberblocks = lalloc(4 * ((loopdetection_nblocks + 31) >> 5)),
            loopdetection_nblocks,
            0
        );
        bitvectorinitialize(
            loop->exitblocks = lalloc(4 * ((loopdetection_nblocks + 31) >> 5)),
            loopdetection_nblocks,
            0
        );
        bitvectorinitialize(
            loop->exit_dominators = lalloc(4 * ((loopdetection_nblocks + 31) >> 5)),
            loopdetection_nblocks,
            0
        );
        bitvectorinitialize(
            loop->backedge_dominators = lalloc(4 * ((loopdetection_nblocks + 31) >> 5)),
            loopdetection_nblocks,
            0
        );

        findnaturalloop(loop);
        addlooptolist(loop, &loopsinflowgraph);

        loopheaders = loopheaders->next;
    }
}

static PCodeBlock* makepreheaderblock(void) {
    PCodeLabel* label;
    PCodeBlock* block;

    label = makepclabel();
    block = lalloc(sizeof(PCodeBlock));
    block->nextBlock = NULL;
    block->prevBlock = NULL;
    block->labels = NULL;
    block->predecessors = block->successors = NULL;
    block->firstPCode = block->lastPCode = NULL;
    block->pcodeCount = 0;
    block->flags = 0;
    block->blockIndex = pcblockcount++;
    pclabel(block, label);
    return block;
}

static void insertpreheaderbefore(PCodeBlock* a, PCodeBlock* b) {
    a->nextBlock = b;
    a->prevBlock = b->prevBlock;
    b->prevBlock->nextBlock = a;
    b->prevBlock = a;
}

void insertpreheaderblock(Loop* loop) {
    PCodeBlock* preheader;
    PCodeBlock* backedgeBlock;
    PCodeBlock* header;
    PCode* branch;
    PCLink* link;
    PCLink** linkptr;
    PCodeLabel* newlabel;
    PCLink* innerlink;
    PCodeBlock* block;
    PCodeArg* arg;
    int i;

    preheader = loop->preheader = makepreheaderblock();
    backedgeBlock = NULL;
    header = loop->body;

    if (!header->labels) {
        pclabel(header, makepclabel());
    }

    appendpcode(preheader, makepcode(PC_B, header->labels));
    preheader->loopWeight = loop->parent ? loop->parent->loopWeight : 1;

    linkptr = &header->predecessors;
    while ((link = *linkptr)) {
        if (bitvectorgetbit(link->block->blockIndex, loop->memberblocks)) {
            linkptr = &link->nextLink;
        } else {
            if (link->block->pcodeCount) {
                branch = link->block->lastPCode;
                if (branch->op == PC_B) {
                    CError_ASSERT(462, branch->args[0].kind == PCOp_LABEL);
                    if (branch->args[0].data.label.label->block == header) {
                        branch->args[0].data.label.label = preheader->labels;
                    }
                } else if (branch->op == PC_BT || branch->op == PC_BF) {
                    CError_ASSERT(474, branch->args[2].kind == PCOp_LABEL);
                    if (branch->args[2].data.label.label->block == header) {
                        branch->args[2].data.label.label = preheader->labels;
                    }
                } else if (branch->op == PC_BCTR) {
                    if (branch->argCount > 1 && branch->args[1].kind == PCOp_MEMORY) {
                        Object* obj = branch->args[1].data.mem.obj;
                        PCodeLabel** array = (PCodeLabel**)obj->u.data.u.switchtable.data;
                        int i;
                        for (i = 0; i < obj->u.data.u.switchtable.size; i++) {
                            if (array[i]->block == header) {
                                array[i] = preheader->labels;
                            }
                        }
                    } else {
                        CodeLabelList* cll;
                        for (cll = codelabellist; cll; cll = cll->next) {
                            if (cll->label->pclabel->block == header) {
                                cll->label->pclabel = preheader->labels;
                            }
                        }
                    }
                } else {
                    CError_ASSERT(505, link->block->nextBlock == header);
                }
            }

            for (innerlink = link->block->successors; innerlink; innerlink = innerlink->nextLink) {
                if (innerlink->block == header) {
                    innerlink->block = preheader;
                }
            }

            *linkptr = link->nextLink;
            link->nextLink = preheader->predecessors;
            preheader->predecessors = link;
        }
    }

    if (!bitvectorgetbit(header->prevBlock->blockIndex, loop->memberblocks)) {
        insertpreheaderbefore(preheader, header);

        if ((!header->nextBlock ||
             !bitvectorgetbit(header->nextBlock->blockIndex, loop->memberblocks)) &&
            header->lastPCode &&
            (header->lastPCode->flags & fIsBranch) &&
            header->lastPCode->op != PC_BDNZ) {
            i = header->lastPCode->argCount;
            arg = header->lastPCode->args;
            while (i && arg->kind != PCOp_LABEL) {
                arg++;
                i--;
            }

            if (i && arg->kind == PCOp_LABEL && arg->data.label.label->block == header) {
                backedgeBlock = makepreheaderblock();
                insertpreheaderbefore(backedgeBlock, header);
                newlabel = makepclabel();
                pclabel(backedgeBlock, newlabel);
                arg->data.label.label = newlabel;

                link = lalloc(sizeof(PCLink));
                link->block = header;
                link->nextLink = backedgeBlock->predecessors;
                backedgeBlock->predecessors = link;

                link = lalloc(sizeof(PCLink));
                link->block = header;
                link->nextLink = backedgeBlock->successors;
                backedgeBlock->successors = link;

                for (link = header->successors; link; link = link->nextLink) {
                    if (link->block == header) {
                        link->block = backedgeBlock;
                    }
                }
                for (link = header->predecessors; link; link = link->nextLink) {
                    if (link->block == header) {
                        link->block = backedgeBlock;
                    }
                }

                bitvectorsetbit(backedgeBlock->blockIndex, loop->backedge_dominators);
                addblocktoloop(loop, backedgeBlock);
            }
        }
    } else {
        for (block = pcbasicblocks; block; block = block->nextBlock) {
            if (bitvectorgetbit(block->blockIndex, loop->memberblocks)) {
                break;
            }
        }
        insertpreheaderbefore(preheader, block);
    }

    link = lalloc(sizeof(PCLink));
    link->block = preheader;
    link->nextLink = header->predecessors;
    header->predecessors = link;

    link = lalloc(sizeof(PCLink));
    link->block = header;
    link->nextLink = preheader->successors;
    preheader->successors = link;

    for (loop = loop->parent; loop; loop = loop->parent) {
        addblocktoloop(loop, preheader);
        if (bitvectorgetbit(header->blockIndex, loop->exit_dominators)) {
            bitvectorsetbit(preheader->blockIndex, loop->exit_dominators);
            if (backedgeBlock) {
                bitvectorsetbit(backedgeBlock->blockIndex, loop->exit_dominators);
            }
        }
        if (bitvectorgetbit(header->blockIndex, loop->backedge_dominators)) {
            bitvectorsetbit(preheader->blockIndex, loop->backedge_dominators);
            if (backedgeBlock) {
                bitvectorsetbit(backedgeBlock->blockIndex, loop->backedge_dominators);
            }
        }
    }
}

static void insertpreheaderblocks(Loop* loop) {
    while (loop) {
        if (loop->children) {
            insertpreheaderblocks(loop->children);
        }
        insertpreheaderblock(loop);
        loop = loop->nextSibling;
    }
}

void findloopsinflowgraph(void) {
    loopsinflowgraph = NULL;
    computedominators();
    if (findloopheaders()) {
        findnaturalloops();
        insertpreheaderblocks(loopsinflowgraph);
    }
    freeoheap();
}

static int checklooplimits(
    SInt32 opcode, SInt32 condition, SInt32 lower, SInt32 upper, SInt32 addend, SInt32* result
) {
    if (opcode == PC_BT) {
        if (condition == 0) {
            if (addend <= 0) {
                return FALSE;
            }
            if (lower < upper) {
                *result = (upper - lower + addend - 1) / addend;
            } else {
                *result = 0;
            }
        } else if (condition == 1) {
            if (addend >= 0) {
                return FALSE;
            }
            if (lower > upper) {
                *result = (lower - upper - addend - 1) / -addend;
            } else {
                *result = 0;
            }
        } else {
            return FALSE;
        }
    } else {
        if (condition == 0) {
            if (addend >= 0) {
                return FALSE;
            }
            if (lower >= upper) {
                *result = (lower - upper - addend) / -addend;
            } else {
                *result = 0;
            }
        } else if (condition == 1) {
            if (addend <= 0) {
                return FALSE;
            }
            if (lower <= upper) {
                *result = (upper - lower + addend) / addend;
            } else {
                *result = 0;
            }
        } else if (lower < upper) {
            if (addend <= 0) {
                return FALSE;
            }
            if ((upper - lower) % addend) {
                return FALSE;
            }
            *result = (upper - lower) / addend;
        } else if (lower > upper) {
            if (addend >= 0) {
                return FALSE;
            }
            if ((lower - upper) % -addend) {
                return FALSE;
            }
            *result = (lower - upper) / -addend;
        } else {
            *result = 0;
        }
    }

    return TRUE;
}

static int checkunsignedlooplimits(
    SInt32 opcode, SInt32 condition, UInt32 lower, UInt32 upper, SInt32 addend, UInt32* result
) {
    if (opcode == PC_BT) {
        if (condition == 0) {
            if (addend <= 0) {
                return FALSE;
            }
            if (lower < upper) {
                *result = (upper - lower + addend - 1) / addend;
            } else {
                *result = 0;
            }
        } else if (condition == 1) {
            if (addend >= 0) {
                return FALSE;
            }
            if (lower > upper) {
                *result = (lower - upper - addend - 1) / -addend;
            } else {
                *result = 0;
            }
        } else {
            return FALSE;
        }
    } else {
        if (condition == 0) {
            if (addend >= 0) {
                return FALSE;
            }
            if (lower >= upper) {
                *result = (lower - upper - addend) / -addend;
            } else {
                *result = 0;
            }
        } else if (condition == 1) {
            if (addend <= 0) {
                return FALSE;
            }
            if (lower <= upper) {
                *result = (upper - lower + addend) / addend;
            } else {
                *result = 0;
            }
        } else if (lower < upper) {
            if (addend <= 0) {
                return FALSE;
            }
            if ((upper - lower) % addend) {
                return FALSE;
            }
            *result = (upper - lower) / addend;
        } else if (lower > upper) {
            if (addend >= 0) {
                return FALSE;
            }
            if ((lower - upper) % -addend) {
                return FALSE;
            }
            *result = (lower - upper) / -addend;
        } else {
            *result = 0;
        }
    }

    if ((SInt32)*result < 0) {
        return FALSE;
    }
    return TRUE;
}

static int checkunknownloop(int opcode, int condition, int step, unsigned char* op) {
    if (opcode == PC_BT) {
        if (condition == 0) {
            if (step <= 0) {
                return FALSE;
            }
            *op = ELESS;
        } else if (condition == 1) {
            if (step >= 0) {
                return FALSE;
            }
            *op = EGREATER;
        } else {
            return FALSE;
        }
    } else {
        if (condition == 0) {
            if (step >= 0) {
                return FALSE;
            }
            *op = EGREATEREQU;
        } else if (condition == 1) {
            if (step <= 0) {
                return FALSE;
            }
            *op = ELESSEQU;
        } else if (step == 1) {
            *op = ENOTEQU;
        } else if (step == -1) {
            *op = ENOTEQU;
        } else {
            return FALSE;
        }
    }

    return TRUE;
}

static void checkcountingloop(Loop* loop) {
    RegUseOrDef* list;
    PCode* lastpcode;
    PCode* prevpcode;
    PCode* instr;
    short cmpOp;
    short crField;
    SInt16 condition;
    short ivReg;
    Loop* child;

    if (!(lastpcode = loop->body->lastPCode)) {
        return;
    }
    if (lastpcode->op != PC_BT && lastpcode->op != PC_BF) {
        return;
    }
    if (lastpcode->args[2].kind != PCOp_LABEL) {
        return;
    }

    if (!bitvectorgetbit(
            lastpcode->args[2].data.label.label->block->blockIndex, loop->memberblocks
        )) {
        return;
    }
    if (bitvectorgetbit(loop->body->nextBlock->blockIndex, loop->memberblocks)) {
        return;
    }

    crField = lastpcode->args[0].data.reg.reg;
    condition = lastpcode->args[1].data.imm.value;
    prevpcode = lastpcode->prevPCode;
    if (!prevpcode) {
        return;
    }

    cmpOp = prevpcode->op;
    if (cmpOp == PC_ADDI && prevpcode->args[2].kind == PCOp_IMMEDIATE) {
        instr = prevpcode;
        prevpcode = prevpcode->prevPCode;
        if (!prevpcode) {
            return;
        }

        cmpOp = prevpcode->op;
        if (instr->args[0].data.reg.reg != instr->args[1].data.reg.reg) {
            return;
        }
        if (cmpOp != PC_CMP && cmpOp != PC_CMPL && cmpOp != PC_CMPI && cmpOp != PC_CMPLI) {
            return;
        }
        if (prevpcode->args[1].data.reg.reg != instr->args[0].data.reg.reg) {
            return;
        }
        if ((loop->step = instr->args[2].data.imm.value) == 0) {
            return;
        }
    }

    if (cmpOp != PC_CMP && cmpOp != PC_CMPL && cmpOp != PC_CMPI && cmpOp != PC_CMPLI) {
        return;
    }

    if (prevpcode->args[0].data.reg.reg != crField) {
        return;
    }

    ivReg = prevpcode->args[1].data.reg.reg;
    if (ivReg < 32) {
        return;
    }

    if (loop->preheader->nextBlock != lastpcode->args[2].data.label.label->block) {
        return;
    }

    if (cmpOp == PC_CMPI) {
        if (prevpcode->prevPCode) {
            return;
        }
        loop->upper = prevpcode->args[2].data.imm.value;
        loop->upperType = LOOP_BOUND_CONSTANT;
    } else if (cmpOp == PC_CMPLI) {
        if (prevpcode->prevPCode) {
            return;
        }
        loop->upper = (UInt16)prevpcode->args[2].data.imm.value;
        loop->upperType = LOOP_BOUND_CONSTANT;
    } else if (cmpOp == PC_CMP || cmpOp == PC_CMPL) {
        if (prevpcode->prevPCode) {
            if (prevpcode->prevPCode->op == PC_LI &&
                prevpcode->prevPCode->args[1].kind == PCOp_IMMEDIATE &&
                prevpcode->prevPCode->args[0].data.reg.reg == prevpcode->args[2].data.reg.reg &&
                !prevpcode->prevPCode->prevPCode) {
                loop->upper = prevpcode->prevPCode->args[1].data.imm.value;
                loop->upperType = LOOP_BOUND_CONSTANT;
            } else if (prevpcode->prevPCode->op == PC_LIS &&
                       prevpcode->prevPCode->args[1].kind == PCOp_IMMEDIATE &&
                       prevpcode->prevPCode->args[0].data.reg.reg ==
                           prevpcode->args[2].data.reg.reg &&
                       !prevpcode->prevPCode->prevPCode) {
                loop->upper = prevpcode->prevPCode->args[1].data.imm.value << 16;
                loop->upperType = LOOP_BOUND_CONSTANT;
            } else if (prevpcode->prevPCode->op == PC_ADDI &&
                       prevpcode->prevPCode->args[2].kind == PCOp_IMMEDIATE &&
                       prevpcode->prevPCode->args[0].data.reg.reg ==
                           prevpcode->args[2].data.reg.reg &&
                       prevpcode->prevPCode->args[1].data.reg.reg ==
                           prevpcode->args[2].data.reg.reg &&
                       prevpcode->prevPCode->prevPCode &&
                       prevpcode->prevPCode->prevPCode->op == PC_LIS &&
                       prevpcode->prevPCode->prevPCode->args[1].kind == PCOp_IMMEDIATE &&
                       prevpcode->prevPCode->prevPCode->args[0].data.reg.reg ==
                           prevpcode->args[2].data.reg.reg &&
                       !prevpcode->prevPCode->prevPCode->prevPCode) {
                loop->upper = prevpcode->prevPCode->args[2].data.imm.value +
                    (prevpcode->prevPCode->prevPCode->args[1].data.imm.value << 16);
                loop->upperType = LOOP_BOUND_CONSTANT;
            } else {
                return;
            }
        } else {
            instr = NULL;
            for (list = reg_Defs[RegClass_GPR][prevpcode->args[2].data.reg.reg]; list;
                 list = list->next) {
                if (bitvectorgetbit(Defs[list->id].pcode->block->blockIndex, loop->memberblocks)) {
                    return;
                }
            }
            for (list = reg_Defs[RegClass_GPR][prevpcode->args[2].data.reg.reg]; list;
                 list = list->next) {
                if (bitvectorgetbit(list->id, usedefinfo[loop->preheader->blockIndex].defin)) {
                    if (!instr) {
                        instr = Defs[list->id].pcode;
                        if (instr->op == PC_LI && instr->args[1].kind == PCOp_IMMEDIATE) {
                            loop->upper = instr->args[1].data.imm.value;
                            loop->upperType = LOOP_BOUND_CONSTANT;
                        } else if (instr->op == PC_LIS && instr->args[1].kind == PCOp_IMMEDIATE) {
                            loop->upper = instr->args[1].data.imm.value << 16;
                            loop->upperType = LOOP_BOUND_CONSTANT;
                        } else if (instr->op == PC_ADDI &&
                                   instr->args[2].kind == PCOp_IMMEDIATE &&
                                   instr->args[1].data.reg.reg == prevpcode->args[2].data.reg.reg &&
                                   instr->prevPCode &&
                                   instr->prevPCode->op == PC_LIS &&
                                   instr->prevPCode->args[1].kind == PCOp_IMMEDIATE &&
                                   instr->prevPCode->args[0].data.reg.reg ==
                                       prevpcode->args[2].data.reg.reg) {
                            loop->upper = instr->args[2].data.imm.value +
                                (instr->prevPCode->args[1].data.imm.value << 16);
                            loop->upperType = LOOP_BOUND_CONSTANT;
                        } else {
                            loop->upperType = LOOP_BOUND_VARIABLE;
                            break;
                        }
                    } else {
                        loop->upperType = LOOP_BOUND_VARIABLE;
                        break;
                    }
                }
            }

            if (loop->upperType == LOOP_BOUND_INDETERMINATE) {
                loop->upperType = LOOP_BOUND_VARIABLE;
            }
        }
    }

    instr = NULL;

    for (list = reg_Defs[RegClass_GPR][ivReg]; list; list = list->next) {
        if (bitvectorgetbit(Defs[list->id].pcode->block->blockIndex, loop->memberblocks)) {
            if (!instr) {
                instr = Defs[list->id].pcode;
                if (instr->op != PC_ADDI) {
                    return;
                }
                if (instr->args[1].data.reg.reg != ivReg) {
                    return;
                }
                if (instr->args[2].kind != PCOp_IMMEDIATE) {
                    return;
                }
                if (!(loop->step = instr->args[2].data.imm.value)) {
                    return;
                }
            } else {
                return;
            }
        }
    }

    if (!instr) {
        return;
    }

    if (instr->block != prevpcode->block &&
        !bitvectorgetbit(instr->block->blockIndex, loop->backedge_dominators)) {
        return;
    }

    if (loop->children) {
        for (child = loop->children; child; child = child->nextSibling) {
            if (bitvectorgetbit(instr->block->blockIndex, child->memberblocks)) {
                return;
            }
        }
    }

    loop->inductionUpdate = instr;

    instr = NULL;

    for (list = reg_Defs[RegClass_GPR][ivReg]; list; list = list->next) {
        if (bitvectorgetbit(list->id, usedefinfo[loop->preheader->blockIndex].defin)) {
            if (!instr) {
                instr = Defs[list->id].pcode;
                if (instr->op == PC_LI && instr->args[1].kind == PCOp_IMMEDIATE) {
                    loop->lower = instr->args[1].data.imm.value;
                    loop->lowerType = LOOP_BOUND_CONSTANT;
                } else if (instr->op == PC_LIS && instr->args[1].kind == PCOp_IMMEDIATE) {
                    loop->lower = instr->args[1].data.imm.value << 16;
                    loop->lowerType = LOOP_BOUND_CONSTANT;
                } else if (instr->op == PC_ADDI &&
                           instr->args[2].kind == PCOp_IMMEDIATE &&
                           instr->args[1].data.reg.reg == ivReg &&
                           instr->prevPCode &&
                           instr->prevPCode->op == PC_LIS &&
                           instr->prevPCode->args[1].kind == PCOp_IMMEDIATE &&
                           instr->prevPCode->args[0].data.reg.reg == ivReg) {
                    loop->lower = instr->args[2].data.imm.value +
                        (instr->prevPCode->args[1].data.imm.value << 16);
                    loop->lowerType = LOOP_BOUND_CONSTANT;
                } else {
                    loop->lowerType = LOOP_BOUND_VARIABLE;
                    break;
                }
            } else {
                loop->lowerType = LOOP_BOUND_INDETERMINATE;
                break;
            }
        }
    }

    if (loop->lowerType == LOOP_BOUND_INDETERMINATE) {
        loop->lowerType = LOOP_BOUND_VARIABLE;
    }

    if (loop->lowerType == LOOP_BOUND_CONSTANT && loop->upperType == LOOP_BOUND_CONSTANT) {
        if (cmpOp == PC_CMP || cmpOp == PC_CMPI) {
            if (!checklooplimits(
                    lastpcode->op,
                    condition,
                    loop->lower,
                    loop->upper,
                    loop->step,
                    &loop->iterationCount
                )) {
                return;
            }
        } else {
            if (!checkunsignedlooplimits(
                    lastpcode->op,
                    condition,
                    loop->lower,
                    loop->upper,
                    loop->step,
                    (UInt32*)&loop->iterationCount
                )) {
                return;
            }
        }
        loop->isKnownCountingLoop = TRUE;
    } else if (loop->lowerType != LOOP_BOUND_INDETERMINATE ||
               loop->upperType != LOOP_BOUND_INDETERMINATE) {
        if (!checkunknownloop(lastpcode->op, condition, loop->step, &loop->unknownCondition)) {
            return;
        }
        loop->isUnknownCountingLoop = TRUE;
    }
}

void analyzeForCountableLoops(Loop* loop) {
    if (!loop) {
        return;
    }

    while (loop) {
        if (loop->children) {
            analyzeForCountableLoops(loop->children);
        }
        checkcountingloop(loop);
        loop = loop->nextSibling;
    }
}

void analyzeloop(Loop* loop) {
    BlockList* list;
    PCodeBlock* block;
    PCode* pcode;

    loop->bodySize = 0;
    loop->has_call = FALSE;
    loop->uses_count_register = FALSE;
    loop->is_straight_line = TRUE;
    loop->isKnownCountingLoop = FALSE;
    loop->isUnknownCountingLoop = FALSE;
    loop->lowerType = LOOP_BOUND_INDETERMINATE;
    loop->upperType = LOOP_BOUND_INDETERMINATE;
    loop->iterationCount = -1;
    loop->has_memory_barrier = FALSE;
    loop->has_unrolled_code = FALSE;
    // the original never clears has_indexed_load and has_indexed_store (harmless: only the empty
    // insertupdateinstructions depends on them)

    for (list = loop->blocks; list; list = list->next) {
        block = list->block;
        if (!loop->children) {
            block->flags |= fInnerLoop;
        }
        loop->bodySize += block->pcodeCount;

        if (block != loop->body) {
            if (!block->successors ||
                !block->predecessors ||
                block->successors->nextLink ||
                block->predecessors->nextLink) {
                loop->is_straight_line = FALSE;
            }
        }

        if ((block->flags & fUnrolled) == fUnrolled) {
            loop->has_unrolled_code = TRUE;
        }

        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
            if (PCODE_FLAG_SET_T(pcode) & fLink) {
                loop->has_call = TRUE;
            }

            if (pcode->op == PC_BCTRL ||
                pcode->op == PC_BCTR ||
                pcode->op == PC_BCCTR ||
                pcode->op == PC_MTCTR ||
                pcode->op == PC_MFCTR) {
                loop->uses_count_register = TRUE;
            } else if (pcode->flags & fIsRead) {
                if (pcode->op == PC_LBZX ||
                    pcode->op == PC_LHZX ||
                    pcode->op == PC_LHAX ||
                    pcode->op == PC_LWZX ||
                    pcode->op == PC_LFSX ||
                    pcode->op == PC_LFDX) {
                    loop->has_indexed_load = TRUE;
                }
            } else if (pcode->flags & fIsWrite) {
                if (pcode->op == PC_STBX ||
                    pcode->op == PC_STHX ||
                    pcode->op == PC_STWX ||
                    pcode->op == PC_STFSX ||
                    pcode->op == PC_STFDX) {
                    loop->has_indexed_store = TRUE;
                }
            } else {
                if (pcode->op == PC_EIEIO || pcode->op == PC_SYNC || pcode->op == PC_ISYNC) {
                    loop->has_memory_barrier = TRUE;
                }
            }
        }
    }

    if (!loop->children && !loop->has_call && loop->bodySize < 32) {
        for (list = loop->blocks; list; list = list->next) {
            list->block->flags |= fInnerLoop;
        }
    }
}

static void analyzeloops(Loop* loop) {
    while (loop) {
        if (loop->children) {
            analyzeloops(loop->children);
        }
        analyzeloop(loop);
        loop = loop->nextSibling;
    }
}

void analyzeloopsinflowgraph(void) {
    if (loopsinflowgraph) {
        analyzeloops(loopsinflowgraph);
    }
}
