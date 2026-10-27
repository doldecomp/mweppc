#include "compiler/CopyPropagation.h"
#include "compiler/BitVectors.h"
#include "compiler/CMangler.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeListing.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/UseDefChains.h"
#include <stdio.h>

int propagatedcopies;
int propagated_instructions;
int recursive_propagation;
SInt32 number_of_candidates;
Candidate* Candidates;
PropInfo* propinfo;
int* ncandidatesinblock;
int* firstcandidateinblock;
static int moreaggressiveoptimization;
static UInt32 cp_mask;
static PropagateAndFinishFunc propagateandfinish;
static PropagatesToUseFunc propagatestouse;
static IsCandidateFunc is_candidate;

static void precomputecanidatecounts(void) {
    PCodeBlock* block;
    PCode* pcode;
    int count;

    ncandidatesinblock = oalloc(sizeof(int) * pcblockcount);
    firstcandidateinblock = oalloc(sizeof(int) * pcblockcount);
    number_of_candidates = 0;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        firstcandidateinblock[block->blockIndex] = number_of_candidates;

        count = 0;
        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
            if (is_candidate(pcode)) {
                number_of_candidates++;
                count++;
            }
        }
        ncandidatesinblock[block->blockIndex] = count;
    }
}

static void computecandidatelist(void) {
    RegUseOrDef* list;
    PCodeBlock* block;
    PCode* pcode;
    Candidate* candidate;
    UInt32* vec;
    int reg;
    RegUseOrDef* newlist;
    UseOrDef* def;
    UseOrDef* use;
    int defID;
    int useID;

    Candidates = oalloc(sizeof(Candidate) * number_of_candidates);
    vec = oalloc(4 * ((number_of_Uses + 31) >> 5));

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        if (ncandidatesinblock[block->blockIndex]) {
            bitvectorcopy(vec, usedefinfo[block->blockIndex].useout, number_of_Uses);
            candidate = &Candidates
                            [firstcandidateinblock[block->blockIndex] +
                             ncandidatesinblock[block->blockIndex] - 1];
            for (pcode = block->lastPCode; pcode; pcode = pcode->prevPCode) {
                if (!(pcode->flags & fIsBranch) && pcode->argCount) {
                    if (is_candidate(pcode)) {
                        reg = pcode->args[0].data.reg.reg;
                        candidate->pcode = pcode;
                        candidate->list = NULL;

                        for (list = reg_Uses[pcode->args[0].arg][reg]; list; list = list->next) {
                            if (bitvectorgetbit(list->id, vec)) {
                                newlist = oalloc(sizeof(RegUseOrDef));
                                newlist->id = list->id;
                                newlist->next = candidate->list;
                                candidate->list = newlist;
                            }
                        }

#if VERSION < VERSION_GC_2_0
                        // 1.3.2: the read/write test is always true (precedence slip)
                        if (((pcode->flags & fIsRead) | fIsWrite) && (pcode->flags & fUpdatesPtr)) {
#else
                        if (PCODE_FLAG_SET_F(pcode) & fUpdatesPtr) {
#endif
                            for (list = reg_Uses[pcode->args[1].arg][pcode->args[1].data.reg.reg];
                                 list;
                                 list = list->next) {
                                if (bitvectorgetbit(list->id, vec)) {
                                    newlist = oalloc(sizeof(RegUseOrDef));
                                    newlist->id = list->id;
                                    newlist->next = candidate->list;
                                    candidate->list = newlist;
                                }
                            }
                        }

                        candidate--;
                    }

                    for (def = &Defs[defID = pcode->defID];
                         defID < number_of_Defs && def->pcode == pcode;
                         def++, defID++) {
                        if (def->v.kind == PCOp_REGISTER) {
                            for (list = reg_Uses[def->v.arg][def->v.u.reg]; list;
                                 list = list->next) {
                                bitvectorclearbit(list->id, vec);
                            }
                        }
                    }
                    for (use = &Uses[useID = pcode->useID];
                         useID < number_of_Uses && use->pcode == pcode;
                         use++, useID++) {
                        if (use->v.kind == PCOp_REGISTER) {
                            bitvectorsetbit(useID, vec);
                        }
                    }
                }
            }
        }
    }
}

static void allocatepropinfo(void) {
    PropInfo* info;
    int i;

    propinfo = oalloc(sizeof(PropInfo) * pcblockcount);
    for (i = 0, info = propinfo; i < pcblockcount; i++, info++) {
        info->gen = oalloc(4 * ((number_of_candidates + 31) >> 5));
        info->kill = oalloc(4 * ((number_of_candidates + 31) >> 5));
        info->in = oalloc(4 * ((number_of_candidates + 31) >> 5));
        info->out = oalloc(4 * ((number_of_candidates + 31) >> 5));
    }
}

static void computelocalpropinfo(Boolean localflag) {
    PropInfo* info;
    PCode* pcode;
    PCodeBlock* block;
    UInt32* gen;
    UInt32* kill;
    int index;
    PCodeArg* op;
    PCodeArg* candOp;
    int i;
    int j;
    int cndi;
    Candidate* candidate;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        info = &propinfo[block->blockIndex];
        gen = info->gen;
        kill = info->kill;
        bitvectorinitialize(gen, number_of_candidates, 0);
        bitvectorinitialize(kill, number_of_candidates, 0);
        index = firstcandidateinblock[block->blockIndex];

        if (localflag) {
            for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
                if (!(pcode->flags & fIsBranch) && pcode->argCount) {
                    op = pcode->args;
                    i = pcode->argCount;
                    while (i--) {
                        if (op->kind == PCOp_REGISTER &&
                            op->arg == RegClass_GPR &&
                            (op->data.reg.effect & EffectWrite)) {
                            for (cndi = 0, candidate = Candidates; number_of_candidates > cndi;
                                 cndi++, candidate++) {
                                for (j = 0; j < candidate->pcode->argCount; j++) {
                                    candOp = candidate->pcode->args + j;
                                    if (candOp->kind == PCOp_REGISTER &&
                                        candOp->arg == RegClass_GPR) {
                                        if (candOp->data.reg.reg == op->data.reg.reg) {
                                            if (candidate->pcode->block == block) {
                                                bitvectorclearbit(cndi, gen);
                                            } else {
                                                bitvectorsetbit(cndi, kill);
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        op++;
                    }

                    if (is_candidate(pcode)) {
                        bitvectorsetbit(index, gen);
                        index++;
                    }
                }
            }
        } else {
            for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
                if (!(pcode->flags & fIsBranch) && pcode->argCount) {
                    op = pcode->args;
                    i = pcode->argCount;
                    while (i--) {
                        if (op->kind == PCOp_REGISTER && (op->data.reg.effect & EffectWrite)) {
                            for (cndi = 0, candidate = Candidates; number_of_candidates > cndi;
                                 cndi++, candidate++) {
                                for (j = 0; j < candidate->pcode->argCount; j++) {
                                    candOp = candidate->pcode->args + j;
                                    if (candOp->kind == PCOp_REGISTER && candOp->arg == op->arg) {
                                        if (candOp->data.reg.reg == op->data.reg.reg) {
                                            if (candidate->pcode->block == block) {
                                                bitvectorclearbit(cndi, gen);
                                            } else {
                                                bitvectorsetbit(cndi, kill);
                                            }
                                            break;
                                        }
                                    } else {
                                        if (j == 0) {
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        op++;
                    }

                    if (is_candidate(pcode)) {
                        bitvectorsetbit(index, gen);
                        index++;
                    }
                }
            }
        }
    }
}

static void computeglobalpropinfo(void) {
    PCLink* preds;
    PropInfo* info;
    PCodeBlock* block;
    int bitvecsize;
    UInt32* gen;
    UInt32* kill;
    UInt32* in;
    UInt32* out;
    int i;
    int blockIndex;
    int changed;
    UInt32 val;

    bitvecsize = (number_of_candidates + 31) >> 5;
    changed = TRUE;
    info = &propinfo[pcbasicblocks->blockIndex];
    bitvectorinitialize(info->in, number_of_candidates, 0);
    bitvectorcopy(info->out, info->gen, number_of_candidates);

    for (block = pcbasicblocks->nextBlock; block; block = block->nextBlock) {
        info = &propinfo[block->blockIndex];
        out = info->out;
        kill = info->kill;
        for (i = 0; i < bitvecsize; out++, kill++, i++) {
            *out = ~*kill;
        }
    }

    while (changed) {
        changed = FALSE;
        for (blockIndex = 0; blockIndex < pcblockcount; blockIndex++) {
            if (depthfirstordering[blockIndex]) {
                info = &propinfo[depthfirstordering[blockIndex]->blockIndex];
                if ((preds = depthfirstordering[blockIndex]->predecessors)) {
                    in = info->in;
                    bitvectorcopy(in, propinfo[preds->block->blockIndex].out, number_of_candidates);
                    for (preds = preds->nextLink; preds; preds = preds->nextLink) {
                        bitvectorintersect(
                            in, propinfo[preds->block->blockIndex].out, number_of_candidates
                        );
                    }
                }

                out = info->out;
                in = info->in;
                gen = info->gen;
                kill = info->kill;
                for (i = 0; i < bitvecsize; i++) {
                    val = *gen | (*in & ~*kill);
                    if (val != *out) {
                        *out = val;
                        changed = TRUE;
                    }
                    in++;
                    out++;
                    kill++;
                    gen++;
                }
            }
        }
    }
}

#if VERSION < VERSION_GC_2_5
int precedes(PCode* a, PCode* b) {
    for (a = a->nextPCode; a; a = a->nextPCode) {
        if (a == b) {
            return TRUE;
        }
    }

    return FALSE;
}
#endif

static int canidatepropagatestoalluses(int id) {
    RegUseOrDef* list;
    PCode* pcode;

    pcode = Candidates[id].pcode;
    if (cp_mask && (PCODE_FLAG_SET_F(pcode) & cp_mask)) {
        return FALSE;
    }

    for (list = Candidates[id].list; list; list = list->next) {
        if (!propagatestouse(id, list->id)) {
            return FALSE;
        }
    }

    return TRUE;
}

static void propagatecandidates(void) {
    int i;

    for (i = 0; number_of_candidates > i; i++) {
        if (canidatepropagatestoalluses(i)) {
            propagateandfinish(i);
        }
    }
}

void propagateinstructions(Object* proc, Propagation* config, int passCount, Boolean localflag) {
    char buf[52];

    is_candidate = config->is_candidate;
    propagatestouse = config->propagatestouse;
    propagateandfinish = config->propagateandfinish;
    cp_mask = config->mask;
    propagated_instructions = FALSE;

    while (1) {
        recursive_propagation = FALSE;
        precomputecanidatecounts();
        if (number_of_candidates <= 0) {
            freeoheap();
            if (config->computesUseDefChains) {
                computeusedefchains(FALSE);
            }
            return;
        }

        computeusedefchains(FALSE);
        computecandidatelist();
        allocatepropinfo();
        computelocalpropinfo(localflag);
        computedepthfirstordering();
        computeglobalpropinfo();
        propagatecandidates();
        passCount--;

        if (propagated_instructions && copts.debuglisting) {
            sprintf(buf, "AFTER %s PROPAGATION", config->name);
            pclistblocks(CMangler_GetLinkName(proc)->name, buf);
        }

        if (!passCount || !recursive_propagation) {
            if (!config->computesUseDefChains) {
                freeoheap();
            }
            return;
        }

        freeoheap();
    }
}

static int is_copy(PCode* pcode) {
    return (pcode->flags & fIsMove) &&
        !(pcode->args[0].data.reg.reg < n_real_registers[pcode->args[0].arg]);
}

static int copypropagatestouse(int candidateID, int useID) {
    UseOrDef* use;
    int destReg;
    int srcReg;
    RegClass rclass;
    PCode* pcode;
    int i;
    PCode* scan;
    PCodeArg* op;

    pcode = Candidates[candidateID].pcode;
    use = Uses + useID;
    rclass = pcode->args[0].arg;
    destReg = pcode->args[0].data.reg.reg;
    srcReg = pcode->args[1].data.reg.reg;

    if (use->pcode->flags & fIsMove) {
        return FALSE;
    }

    op = use->pcode->args;
    i = use->pcode->argCount;
    while (i--) {
        if (op->kind == PCOp_REGISTER &&
            op->arg == rclass &&
            op->data.reg.reg == destReg &&
            (op->data.reg.effect & (EffectRead | EffectWrite)) == (EffectRead | EffectWrite)) {
            return FALSE;
        }
        op++;
    }

    if (pcode->block == use->pcode->block && precedes(pcode, use->pcode)) {
        for (scan = pcode->nextPCode; scan && scan != use->pcode; scan = scan->nextPCode) {
            op = scan->args;
            i = scan->argCount;
            while (i--) {
                if (op->kind == PCOp_REGISTER &&
                    op->arg == rclass &&
                    (op->data.reg.effect & EffectWrite) &&
                    op->data.reg.reg == srcReg) {
                    return FALSE;
                }
                op++;
            }
        }
    } else {
        if (!bitvectorgetbit(candidateID, propinfo[use->pcode->block->blockIndex].in)) {
            return FALSE;
        }

        for (scan = use->pcode->block->firstPCode; scan; scan = scan->nextPCode) {
            op = scan->args;
            i = scan->argCount;
            while (i--) {
                if (op->kind == PCOp_REGISTER &&
                    op->arg == rclass &&
                    (op->data.reg.effect & EffectWrite) &&
                    op->data.reg.reg == srcReg) {
                    return FALSE;
                }
                op++;
            }
            if (scan == use->pcode) {
                break;
            }
        }
    }

    return TRUE;
}

static void propagateandremovecopy(int id) {
    Candidate* candidate;
    RegClass rclass;
    int destReg;
    int srcReg;
    int i;
    RegUseOrDef* list;
    PCodeArg* op;

    candidate = &Candidates[id];
    rclass = candidate->pcode->args[0].arg;
    destReg = candidate->pcode->args[0].data.reg.reg;
    srcReg = candidate->pcode->args[1].data.reg.reg;

    if (rclass == RegClass_GPR &&
        destReg >= 32 &&
        destReg <= last_exception_register[RegClass_GPR]) {
        return;
    }

    if (!moreaggressiveoptimization && srcReg < n_real_registers[rclass]) {
        return;
    }

    for (list = candidate->list; list; list = list->next) {
        op = Uses[list->id].pcode->args;
        i = Uses[list->id].pcode->argCount;
        while (i--) {
            if (op->kind == PCOp_REGISTER &&
                op->arg == rclass &&
                op->data.reg.reg == destReg &&
                (op->data.reg.effect & EffectRead)) {
                op->data.reg.reg = srcReg;
            }
            op++;
        }
    }

    deletepcode(candidate->pcode);
    propagated_instructions = TRUE;
}

// clang-format off
static Propagation copy_prop = {
    &is_copy,
    &copypropagatestouse,
    &propagateandremovecopy,
    "COPY",
    "COPIES",
    "c%ld",
    fSideEffects | fRecordBit,
    FALSE
};
// clang-format on

void propagatecopyinstructions(Object* proc, int moreAggressive) {
    moreaggressiveoptimization = moreAggressive;
    propagateinstructions(proc, &copy_prop, 1, FALSE);
    propagatedcopies = propagated_instructions;
}
