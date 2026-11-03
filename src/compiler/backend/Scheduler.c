#include "compiler/Scheduler.h"
#include "compiler/Alias.h"
#include "compiler/CError.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Registers.h"

#pragma pack(2)
typedef struct DGNode {
    struct DGNode* next;
    struct DGNode* prev;
    struct DGSuccessor* successors;
    PCode* instr;
    UInt16 latency;
    UInt16 earliest;
    UInt16 latest;
    UInt16 height;
    short predCount;
} DGNode;

typedef struct DGSuccessor {
    struct DGSuccessor* next;
    DGNode* node;
    UInt16 latency;
} DGSuccessor;

typedef struct DGNodeList {
    struct DGNodeList* next;
    DGNode* node;
} DGNodeList;

static DGNodeList** register_uses[RegClassMax];
static DGNodeList** register_defs[RegClassMax];
static DGNodeList* memory_uses;
static DGNodeList* memory_defs;
static DGNodeList* side_effects;
static DGNodeList* volatile_refs;
static DGNode* defaultsuccessor;
static UInt16 criticalpath;
static MachineInfo* MI;

static void initresources(void) {
    RegClass rclass;
    int i;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        register_uses[rclass] = oalloc(sizeof(DGNodeList*) * used_virtual_registers[rclass]);
        register_defs[rclass] = oalloc(sizeof(DGNodeList*) * used_virtual_registers[rclass]);
        for (i = 0; i < used_virtual_registers[rclass]; i++) {
            register_uses[rclass][i] = register_defs[rclass][i] = NULL;
        }
    }

    memory_uses = memory_defs = NULL;
    side_effects = NULL;
    volatile_refs = NULL;
    criticalpath = 0;
}

static DGNode* makedgnode(PCode* instr) {
    DGNode* node;

    node = oalloc(sizeof(DGNode));
    node->next = NULL;
    node->prev = NULL;
    node->successors = NULL;
    node->instr = instr;
    node->latency = node->height = MI->latency(instr);
    node->earliest = 0;
    node->latest = 0;
    node->predCount = 0;
    return node;
}

static DGNode* adddgnode(DGNode* head, DGNode* node) {
    if (head) {
        head->prev = node;
    }
    node->next = head;
    return node;
}

static DGNode* removedgnode(DGNode* head, DGNode* node) {
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        head = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    }

    return head;
}

static void addtolist(DGNodeList** list, DGNode* node) {
    DGNodeList* entry = oalloc(sizeof(DGNodeList));
    entry->node = node;
    entry->next = *list;
    *list = entry;
}

static DGNodeList* makedglistnode(DGNode* node) {
    DGNodeList* list = oalloc(sizeof(DGNodeList));
    list->next = NULL;
    list->node = node;
    return list;
}

int is_same_operand(PCodeArg* a, PCodeArg* b) {
    if (a->kind != b->kind) {
        return FALSE;
    }

    switch (a->kind) {
        case PCOp_IMMEDIATE:
            if (a->data.imm.value != b->data.imm.value) {
                return FALSE;
            }
            break;
        case PCOp_REGISTER:
            if ((char)a->arg != (char)b->arg) {
                return FALSE;
            }
            if (a->data.reg.reg != b->data.reg.reg) {
                return FALSE;
            }
            break;
        case PCOp_MEMORY:
            if (a->data.mem.offset != b->data.mem.offset) {
                return FALSE;
            }
            if (a->data.mem.obj != b->data.mem.obj) {
                return FALSE;
            }
            break;
        case PCOp_LABEL:
            if (a->data.label.label != b->data.label.label) {
                return FALSE;
            }
            break;
        default:
            break;
    }

    return TRUE;
}

static void addsuccessor(DGNode* a, DGNode* b, Boolean hasLatency) {
    int latency;
    DGSuccessor* succ;

    if (a == b) {
        return;
    }

    if (hasLatency) {
        latency = a->latency;
    } else {
        latency = 0;
    }

    for (succ = a->successors; succ; succ = succ->next) {
        if (succ->node == b) {
            if (succ->latency < latency) {
                succ->latency = latency;
                if (b->height + succ->latency > a->height) {
                    a->height = b->height + succ->latency;
                }
            }
            return;
        }
    }

    succ = oalloc(sizeof(DGSuccessor));
    succ->node = b;
    succ->next = a->successors;
    a->successors = succ;
    succ->latency = latency;

    if (hasLatency && (succ->node->instr->flags & fIsBranch)) {
        succ->latency += MI->branch_latency;
    }

    b->predCount++;

    if (b->height + succ->latency > a->height) {
        a->height = b->height + succ->latency;
    }
}

static void serializeall(DGNode* nodes, DGNode* node) {
    DGNode* scan;

    for (scan = nodes; scan; scan = scan->next) {
        addsuccessor(node, scan, FALSE);
    }
}

static void serializelist(DGNode* node, DGNodeList* list) {
    while (list) {
        if (list->node != node) {
            addsuccessor(node, list->node, FALSE);
        }
        list = list->next;
    }
}

static void serializeregister(
    RegClass rclass, DGNode* node, DGNodeList** defs, DGNodeList** uses, int isWrite
) {
    DGNodeList* list;

    if (isWrite) {
        for (list = *uses; list; list = list->next) {
            if (list->node != node) {
                addsuccessor(node, list->node, TRUE);
            }
        }
        for (list = *defs; list; list = list->next) {
            if (list->node != node) {
                addsuccessor(node, list->node, (rclass == RegClass_SPR) || !MI->renames_registers);
            }
        }

        list = makedglistnode(node);
        list->next = *defs;
        *defs = list;
    } else {
        for (list = *defs; list; list = list->next) {
            if (list->node != node) {
                addsuccessor(node, list->node, (rclass == RegClass_SPR) || !MI->renames_registers);
            }
        }

        list = makedglistnode(node);
        list->next = *uses;
        *uses = list;
    }
}

static void serialize_load(DGNode* node) {
    DGNodeList* list;

    for (list = memory_defs; list; list = list->next) {
#if VERSION == VERSION_GC_2_0P2
        if (may_alias_scheduling(node->instr, list->node->instr)) {
#else
        if (may_alias(node->instr, list->node->instr)) {
#endif
            addsuccessor(node, list->node, TRUE);
        }
    }

    addtolist(&memory_uses, node);
}

static void serialize_store(DGNode* node) {
    DGNodeList* list;

    for (list = memory_uses; list; list = list->next) {
#if VERSION == VERSION_GC_2_0P2
        if (may_alias_scheduling(node->instr, list->node->instr)) {
#else
        if (may_alias(node->instr, list->node->instr)) {
#endif
            addsuccessor(node, list->node, TRUE);
        }
    }
    for (list = memory_defs; list; list = list->next) {
#if VERSION == VERSION_GC_2_0P2
        if (may_alias_scheduling(node->instr, list->node->instr)) {
#else
        if (may_alias(node->instr, list->node->instr)) {
#endif
            addsuccessor(node, list->node, TRUE);
        }
    }

    addtolist(&memory_defs, node);
    if (node->instr->flags & fOpToMemory) {
        addtolist(&memory_uses, node);
    }
}

static void findsuccessors(DGNode* nodes, DGNode* node) {
    PCode* instr;
    PCodeArg* op;
    int i;

    instr = node->instr;
    for (i = 0, op = instr->args; i < instr->argCount; i++, op++) {
        switch (op->kind) {
            case PCOp_IMMEDIATE:
            case PCOp_MEMORY:
                break;
            case PCOp_REGISTER:
                if (op->data.reg.reg < 0 ||
                    op->data.reg.reg > used_virtual_registers[(char)op->arg]) {
                    CError_FATAL(470);
                }

                if (op->kind == PCOp_REGISTER && op->arg == RegClass_GPR) {
                    if (op->data.reg.reg == Register2) {
                        break;
                    }
                    if ((int)op->data.reg.reg == 13) {
                        break;
                    }
                    if (op->data.reg.reg == Register0 &&
                        !(op->data.reg.effect & (EffectRead | EffectWrite))) {
                        break;
                    }
                }

                serializeregister(
                    op->arg,
                    node,
                    &register_defs[(char)op->arg][op->data.reg.reg],
                    &register_uses[(char)op->arg][op->data.reg.reg],
                    op->data.reg.effect & EffectWrite
                );

                break;
            default:
                break;
        }
    }

    if (instr->flags & (fIsRead | fOpFromMemory)) {
        serialize_load(node);
    } else if (instr->flags & (fIsWrite | fOpToMemory)) {
        serialize_store(node);
    }

    if (instr->flags & fIsVolatile) {
        serializelist(node, volatile_refs);
        addtolist(&volatile_refs, node);
    }

    if ((((instr->flags & fIsCall) | fIsBranch) && (instr->flags & fLink)) ||
        (instr->flags & fSideEffects) ||
        MI->serializes(instr)) {
        serializeall(nodes, node);
        addtolist(&side_effects, node);
    }

    if (side_effects) {
        serializelist(node, side_effects);
    }

    if (!node->successors && defaultsuccessor) {
        addsuccessor(node, defaultsuccessor, FALSE);
    }

    if (node->height > criticalpath) {
        criticalpath = node->height;
    }
}

static void computedeadlines(DGNode* nodes) {
    while (nodes) {
        nodes->latest = criticalpath - nodes->height;
        nodes = nodes->next;
    }
}

static int uncovering(DGNode* node) {
    int counter;
    DGSuccessor* succ;

    counter = 0;
    for (succ = node->successors; succ; succ = succ->next) {
        if (succ->node->predCount == 1) {
            counter++;
        }
    }

    return counter;
}

static DGNode* selectinstruction(DGNode* nodes, UInt16 counter) {
    DGNode* node;
    DGNode* candidate;
    int nodeUncovers;
    int candidateUncovers;

    node = nodes;
    while (node) {
        if (node->predCount == 0 && node->earliest <= counter && MI->can_issue(node->instr)) {
            break;
        }
        node = node->next;
    }

    if (!node) {
        return NULL;
    }

    for (candidate = node->next; candidate; candidate = candidate->next) {
        if (candidate->predCount == 0 &&
            candidate->earliest <= counter &&
            MI->can_issue(candidate->instr) &&
            (node->latest > counter || candidate->latest <= counter)) {
            if (node->latest > counter && candidate->latest <= counter) {
                node = candidate;
                continue;
            }

            candidateUncovers = uncovering(candidate);
            nodeUncovers = uncovering(node);
            if (nodeUncovers > candidateUncovers) {
                continue;
            }

            if (nodeUncovers < candidateUncovers) {
                node = candidate;
                continue;
            }

            if (node->height > candidate->height) {
                continue;
            }

            if (node->height < candidate->height) {
                node = candidate;
                continue;
            }

            if (coloring) {
                if (opcodeinfo[node->instr->op].rank < opcodeinfo[candidate->instr->op].rank) {
                    continue;
                }

                if (opcodeinfo[node->instr->op].rank > opcodeinfo[candidate->instr->op].rank) {
                    node = candidate;
                }
            }
        }
    }

    return node;
}

static void holdoffsuccessors(DGNode* node, UInt16 counter) {
    DGSuccessor* succ;
    DGNode* n;

    for (succ = node->successors; succ; succ = succ->next) {
        n = succ->node;
        n->predCount--;
        if (n->earliest < counter + succ->latency) {
            n->earliest = counter + succ->latency;
        }
    }
}

static void scheduleblock(PCodeBlock* block) {
    DGNode* node;
    UInt16 counter;
    PCode* instr;
    UInt16 i;
    DGNode* head;

    initresources();
    defaultsuccessor = NULL;
    head = NULL;

    for (instr = block->lastPCode; instr; instr = instr->prevPCode) {
        node = makedgnode(instr);
        findsuccessors(head, node);
        if (instr->flags & fIsBranch) {
            defaultsuccessor = node;
        }
        head = adddgnode(head, node);
    }

    computedeadlines(head);
    block->firstPCode = block->lastPCode = NULL;
    block->pcodeCount = 0;

    MI->initialize();
    counter = 0;
    while (head != NULL) {
        for (i = 0; i < MI->issue_width; i++) {
            if (head == NULL) {
                break;
            }

            node = selectinstruction(head, counter);
            if (!node) {
                break;
            }

            instr = node->instr;
            if (node->successors) {
                holdoffsuccessors(node, counter);
            }

            appendpcode(block, instr);
            MI->issue(instr);
            head = removedgnode(head, node);
        }

        MI->advance_clock();
        counter++;
    }

    freeoheap();
}

void scheduleinstructions(Boolean includePrologEpilog) {
    PCodeBlock* block;
    int cpu;

    cpu = copts.scheduling;
    if (cpu == 10) {
        MI = &machine7450;
    } else if (copts.altivec_model != 0 || cpu == 7) {
        MI = &machine7400;
    } else if (cpu == 2) {
        MI = &machine603;
    } else if (cpu == 5) {
        MI = &machine603e;
    } else if (cpu == 3) {
        MI = &machine604;
    } else if (cpu == 6) {
        MI = &machine604;
    } else if (cpu == 4) {
        MI = &machine750;
    } else if (cpu == 1) {
        MI = &machine601;
    } else if (cpu == 9) {
        MI = &machine821;
    } else {
        MI = &machine603;
    }

    gather_alias_info();

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        if (block->pcodeCount > 2 &&
            (includePrologEpilog || !(block->flags & (fIsProlog | fIsEpilogue))) &&
            !(block->flags & fScheduled)) {
            scheduleblock(block);
            block->flags |= fScheduled;
        }
    }
}

int is_dependent(PCode* a, PCode* b, char rclass) {
    int i;
    int reg;
    PCodeArg* op;
    PCodeArg* def;

    if (b) {
        def = b->args;
        if (b->argCount >= 1 &&
            def->kind == PCOp_REGISTER &&
            (char)def->arg == rclass &&
            (def->data.reg.effect & EffectWrite)) {
            reg = def->data.reg.reg;
            for (i = 0; i < a->argCount; i++) {
                op = &a->args[i];
                if (op->kind == PCOp_REGISTER &&
                    (char)op->arg == rclass &&
                    (op->data.reg.effect & (EffectRead | EffectWrite)) &&
                    op->data.reg.reg == reg) {
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

int uses_vpermute_unit(PCode* instr) {
    int cpu;

    cpu = copts.scheduling;
    if (cpu == 10) {
        return machine7450.uses_vpermute_unit(instr);
    }
    if (copts.altivec_model != 0 || cpu == 7) {
        return machine7400.uses_vpermute_unit(instr);
    }
    return FALSE;
}

int default_uses_vpermute_unit(PCode* instr) {
    return FALSE;
}
