#include "compiler/PCode.h"
#include "compiler/Alias.h"
#include "compiler/CFunc.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/PCodeInfo.h"
#include <stddef.h>

PCodeBlock* pcbasicblocks;
PCodeBlock* pclastblock;
PCodeBlock* prologue;
PCodeBlock* epilogue;
PCodeBlock** depthfirstordering;
int pcblockcount;
unsigned int pcloopweight;
static unsigned short pclabelcount;

void initpcode(void) {
    pcbasicblocks = pclastblock = NULL;
    pclabelcount = pcblockcount = 0;
    pcloopweight = 1;
    initialize_aliases();
}

PCode* makepcode(VarArgShort op, ...) {
    PCode* pcode;
    va_list list;

    va_start(list, op);
    pcode = vformatpcode(op, list);
    pcode->sourceoffset = current_statement ? current_statement->sourceoffset : -1;
    return pcode;
}

void emitpcode(VarArgShort op, ...) {
    PCode* pcode;
    va_list list;

    va_start(list, op);
    pcode = vformatpcode(op, list);
    pcode->sourceoffset = current_statement ? current_statement->sourceoffset : -1;
    appendpcode(pclastblock, pcode);
}

PCode* copypcode(PCode* pcode) {
    PCode* newpc;
    int extra_arg;
    int i;

    extra_arg = 0;
    if ((PCODE_FLAG_SET_F(pcode) & fCanSetRecordBit) && !(PCODE_FLAG_SET_F(pcode) & fRecordBit)) {
        extra_arg = 1;
    }
    // every copy has at least 5 operands, the extra ones placeholders
    if (pcode->argCount + extra_arg < 5) {
        extra_arg += 5 - (pcode->argCount + extra_arg);
    }

    newpc = lalloc(offsetof(PCode, args) + sizeof(PCodeArg) * (pcode->argCount + extra_arg));
    memclrw(newpc, offsetof(PCode, args) + sizeof(PCodeArg) * (pcode->argCount + extra_arg));

    newpc->op = pcode->op;
    newpc->flags = pcode->flags;
    newpc->argCount = pcode->argCount;
    newpc->alias = pcode->alias;
    for (i = 0; i < pcode->argCount; i++) {
        newpc->args[i] = pcode->args[i];
    }

    while (extra_arg) {
        newpc->args[pcode->argCount + extra_arg - 1].kind = PCOp_PLACEHOLDEROPERAND;
        extra_arg--;
    }

    return newpc;
}

PCodeLabel* makepclabel(void) {
    PCodeLabel* label;

    label = (PCodeLabel*)lalloc(sizeof(PCodeLabel));
    memclrw(label, sizeof(PCodeLabel));
    label->index = pclabelcount++;
    return label;
}

PCodeBlock* makepcblock(void) {
    PCodeBlock* block;

    block = (PCodeBlock*)lalloc(sizeof(PCodeBlock));
    memclrw(block, sizeof(PCodeBlock));
    block->loopWeight = pcloopweight;
    block->blockIndex = pcblockcount++;
    if (pclastblock) {
        // TODO: one comma expression (probably a macro's expansion): as two statements the code
        // differs
        pclastblock->nextBlock = block, block->prevBlock = pclastblock;
    } else {
        pcbasicblocks = block;
    }
    pclastblock = block;
    return block;
}

void pclabel(PCodeBlock* block, PCodeLabel* label) {
    PCLink* iter;
    PCLink* next;

    iter = (PCLink*)label->block;
    while (iter) {
        next = (PCLink*)iter->block;
        iter->block = block;
        iter = next;
    }

    label->block = block;
    label->resolved = TRUE;
    label->nextLabel = block->labels;
    block->labels = label;
}

void pcbranch(PCodeBlock* block, PCodeLabel* label) {
    PCLink* link;

    link = (PCLink*)lalloc(sizeof(PCLink));
    memclrw(link, sizeof(PCLink));

    link->block = label->block;
    if (!label->resolved) {
        label->block = (PCodeBlock*)link;
    }
    link->nextLink = block->successors;
    block->successors = link;
}

void pccomputepredecessors(void) {
    PCodeBlock* block;
    PCLink* succ;
    PCLink* pred;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (succ = block->successors; succ; succ = succ->nextLink) {
            pred = (PCLink*)lalloc(sizeof(PCLink));
            memclrw(pred, sizeof(PCLink));

            pred->block = block;
            pred->nextLink = succ->block->predecessors;
            succ->block->predecessors = pred;
        }
    }
}

void deleteblock(PCodeBlock* block) {
    block->prevBlock->nextBlock = block->nextBlock;
    if (block->nextBlock) {
        block->nextBlock->prevBlock = block->prevBlock;
    }
    block->flags |= fDeleted;
}

void deleteunreachableblocks(void) {
    PCodeBlock* block;

    computedepthfirstordering();

    for (block = pcbasicblocks->nextBlock; block; block = block->nextBlock) {
        if (!(block->flags & fVisited)) {
            deleteblock(block);
        }
    }
}

void appendpcode(PCodeBlock* block, PCode* pcode) {
    if (block->firstPCode) {
        pcode->nextPCode = NULL;
        pcode->prevPCode = block->lastPCode;
        block->lastPCode->nextPCode = pcode;
        block->lastPCode = pcode;
    } else {
        block->firstPCode = block->lastPCode = pcode;
        pcode->nextPCode = pcode->prevPCode = NULL;
    }
    pcode->block = block;
    block->pcodeCount++;
}

void deletepcode(PCode* pcode) {
    PCodeBlock* block;

    block = pcode->block;
    if (pcode->prevPCode) {
        pcode->prevPCode->nextPCode = pcode->nextPCode;
    } else {
        block->firstPCode = pcode->nextPCode;
    }
    if (pcode->nextPCode) {
        pcode->nextPCode->prevPCode = pcode->prevPCode;
    } else {
        block->lastPCode = pcode->prevPCode;
    }

    pcode->block = NULL;
    block->pcodeCount--;
    block->flags &= ~fScheduled;
}

void insertpcodebefore(PCode* anchor, PCode* newpcode) {
    PCodeBlock* block;

    block = anchor->block;
    if (anchor->prevPCode) {
        anchor->prevPCode->nextPCode = newpcode;
    } else {
        block->firstPCode = newpcode;
    }
    newpcode->nextPCode = anchor;
    newpcode->prevPCode = anchor->prevPCode;
    anchor->prevPCode = newpcode;
    newpcode->sourceoffset = anchor->sourceoffset;

    newpcode->block = block;
    block->pcodeCount++;
    block->flags &= ~fScheduled;
}

void insertpcodeafter(PCode* anchor, PCode* newpcode) {
    PCodeBlock* block;

    block = anchor->block;
    if (anchor->nextPCode) {
        anchor->nextPCode->prevPCode = newpcode;
    } else {
        block->lastPCode = newpcode;
    }
    newpcode->prevPCode = anchor;
    newpcode->nextPCode = anchor->nextPCode;
    anchor->nextPCode = newpcode;
    newpcode->sourceoffset = anchor->sourceoffset;

    newpcode->block = block;
    block->pcodeCount++;
    block->flags &= ~fScheduled;
}

void setpcodeflags(unsigned int flags) {
    pclastblock->lastPCode->flags |= flags;
    if (flags & fSideEffects) {
        pclastblock->lastPCode->flags &= ~(fIsCSE | fCommutative | fIsMove);
    }
}

void clearpcodeflags(int flags) {
    pclastblock->lastPCode->flags &= ~flags;
}

int pccomputeoffsets(void) {
    int offset;
    PCodeBlock* block;

    offset = 0;
    for (block = pcbasicblocks; block; block = block->nextBlock) {
        block->codeOffset = offset;
        offset += block->pcodeCount * 4;
    }

    return offset;
}

typedef struct _DFO {
    PCodeBlock* block;
    PCLink* link;
} DFO;

static int depthfirstorder;

void computedepthfirstordering(void) {
    PCodeBlock* block;
    PCLink* link;
    DFO* dfo;
    int index;

    depthfirstordering = (PCodeBlock**)lalloc(sizeof(PCodeBlock*) * pcblockcount);
    memclrw(depthfirstordering, sizeof(PCodeBlock*) * pcblockcount);
    depthfirstorder = pcblockcount;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        block->flags &= ~fVisited;
    }

    dfo = (DFO*)oalloc(sizeof(DFO) * pcblockcount);
    index = 0;
    pcbasicblocks->flags |= fVisited;

    dfo[index].block = pcbasicblocks;
    dfo[index].link = pcbasicblocks->successors;
    index++;
    while (index) {
        if ((link = dfo[index - 1].link)) {
            dfo[index - 1].link = link->nextLink;
            block = link->block;
            if (!(block->flags & fVisited)) {
                block->flags |= fVisited;
                dfo[index].block = block;
                dfo[index].link = block->successors;
                index++;
            }
        } else {
            block = dfo[--index].block;
            depthfirstordering[--depthfirstorder] = block;
        }
    }

    while (depthfirstorder) {
        depthfirstordering[--depthfirstorder] = NULL;
    }
}
