#include "compiler/ValueNumbering.h"
#include "compiler/Alias.h"
#include "compiler/CError.h"
#include "compiler/CompilerTools.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"

typedef struct ValueLabel {
    struct ValueLabel* next;
    PCodeArg op;
} ValueLabel;

typedef struct AvailableValue {
    struct AvailableValue* next;
    ValueLabel* labelled;
    PCode* pcode;
    int killedregister;
    int aliasnumber;
    int opnumbers[0];
} AvailableValue;

typedef struct RegValue {
    int number;
    int copyreg;
    AvailableValue* available;
} RegValue;

typedef struct State {
    void* stackedvalues;
    int valueceiling;
} State;

typedef struct StackedValue {
    struct StackedValue* next;
    PCodeArg op;
    RegValue value;
    Alias* alias;
    PCode* valuepcode;
} StackedValue;

int removedcommonsubexpressions;
int nextvaluenumber;
static AvailableValue* opvalue[OPCODE_MAX];
static RegValue* regvalue[RegClassMax];
static StackedValue* stackedvalues;
static int valueceiling;
static int moreaggressiveoptimization;

static void allocatecsedatastructures(void) {
    RegClass rclass;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        regvalue[rclass] = oalloc(sizeof(RegValue) * used_virtual_registers[rclass]);
    }
}

static void initializecsedatastructures(void) {
    RegValue* rv;
    RegClass rclass;
    int i;

    nextvaluenumber = 0;

    for (i = 0; i < OPCODE_MAX; i++) {
        opvalue[i] = NULL;
    }

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        rv = regvalue[rclass];
        for (i = 0; i < used_virtual_registers[rclass]; i++, rv++) {
            rv->number = nextvaluenumber++;
            rv->copyreg = 0;
            rv->available = NULL;
        }
    }

    initialize_alias_values();
    stackedvalues = NULL;
    valueceiling = 0x7FFFFFFF;
}

static void labelvalue(AvailableValue* av, PCodeArg* op) {
    ValueLabel* label = oalloc(sizeof(ValueLabel));
    label->op = *op;
    label->next = av->labelled;
    av->labelled = label;
}

static void unlabelvalue(AvailableValue* av, PCodeArg* op) {
    ValueLabel* labelled;
    ValueLabel** ptr;

    ptr = &av->labelled;
    while ((labelled = *ptr)) {
        if (labelled->op.data.reg.reg == op->data.reg.reg) {
            *ptr = labelled->next;
        } else {
            ptr = &labelled->next;
        }
    }
}

static void stackregistervalue(PCodeArg* op, RegValue* value) {
    StackedValue* stacked = oalloc(sizeof(StackedValue));
    stacked->next = stackedvalues;
    stackedvalues = stacked;

    stacked->op = *op;
    stacked->value = *value;
}

static void stackmemoryvalue(Alias* alias) {
    StackedValue* stacked = oalloc(sizeof(StackedValue));
    stacked->next = stackedvalues;
    stackedvalues = stacked;

    stacked->op.kind = PCOp_MEMORY;
    stacked->alias = alias;
    stacked->value.number = alias->valuenumber;
    stacked->valuepcode = alias->valuepcode;
}

static void unstackvalue(StackedValue* stacked) {
    PCodeArg* op = &stacked->op;
    RegValue* value;

    if (op->kind == PCOp_MEMORY) {
        stacked->alias->valuenumber = stacked->value.number;
        stacked->alias->valuepcode = stacked->valuepcode;
    } else {
        value = &regvalue[op->arg][op->data.reg.reg];
        if (value->available) {
            unlabelvalue(value->available, op);
        }
        value->number = stacked->value.number;
        value->copyreg = stacked->value.copyreg;
        value->available = stacked->value.available;
        if (value->available) {
            labelvalue(value->available, op);
        }
    }
}

static int samevalue(PCodeArg* op1, PCodeArg* op2) {
    return regvalue[op1->arg][op1->data.reg.reg].number ==
        regvalue[op2->arg][op2->data.reg.reg].number;
}

static int killregister(PCodeArg* op) {
    RegValue* value;

    value = &regvalue[op->arg][op->data.reg.reg];
    if (value->number < valueceiling && nextvaluenumber >= valueceiling) {
        stackregistervalue(op, value);
    }

    if (value->available) {
        unlabelvalue(value->available, op);
    }
    value->available = NULL;
    value->copyreg = 0;
    return value->number = nextvaluenumber++;
}

void killmemory(Alias* alias, PCode* newValue) {
    if (alias->valuenumber < valueceiling && nextvaluenumber >= valueceiling) {
        stackmemoryvalue(alias);
    }

    if (newValue) {
        alias->valuenumber = regvalue[newValue->args[0].arg][newValue->args[0].data.reg.reg].number;
        alias->valuepcode = newValue;
    } else {
        alias->valuenumber = nextvaluenumber++;
        alias->valuepcode = NULL;
    }
}

static void killspecificCSEs(short op) {
    AvailableValue* av;
    ValueLabel* labelled;

    for (av = opvalue[op]; av; av = av->next) {
        for (labelled = av->labelled; labelled; labelled = labelled->next) {
            killregister(&labelled->op);
        }
    }
}

static void killallCSEs(void) {
    AvailableValue* av;
    ValueLabel* labelled;
    int i;

    for (i = 0; i < OPCODE_MAX; i++) {
        for (av = opvalue[i]; av; av = av->next) {
            for (labelled = av->labelled; labelled; labelled = labelled->next) {
                killregister(&labelled->op);
            }
        }
    }
}

static void killregisters(PCode* pcode) {
    PCodeArg* op;
    int i;

    for (i = 0, op = pcode->args; i < pcode->argCount; i++, op++) {
        if (op->kind == PCOp_REGISTER && (op->data.reg.effect & EffectWrite)) {
            killregister(op);
        }
    }
}

static void copyregister(PCodeArg* src, PCodeArg* dest) {
    RegValue* srcvalue;
    RegValue* destvalue;

    srcvalue = &regvalue[src->arg][src->data.reg.reg];
    destvalue = &regvalue[dest->arg][dest->data.reg.reg];

    if (destvalue->number < valueceiling && nextvaluenumber >= valueceiling) {
        stackregistervalue(dest, destvalue);
    }

    if (destvalue->available) {
        unlabelvalue(destvalue->available, dest);
    }
    destvalue->available = srcvalue->available;
    if (destvalue->available) {
        labelvalue(destvalue->available, dest);
    }

    destvalue->number = srcvalue->number;

    if (srcvalue->copyreg && srcvalue->number == regvalue[src->arg][srcvalue->copyreg].number) {
        destvalue->copyreg = srcvalue->copyreg;
    } else {
        destvalue->copyreg = src->data.reg.reg;
    }
}

static int matchvalues(AvailableValue* av, PCode* match) {
    PCodeArg* avOp;
    PCodeArg* matchOp;
    int i;

    for (avOp = &av->pcode->args[0], matchOp = &match->args[0], i = 0; i < match->argCount;
         i++, avOp++, matchOp++) {
        if (i != 0) {
            switch (avOp->kind) {
                case PCOp_REGISTER:
                    if (av->opnumbers[i] != regvalue[matchOp->arg][matchOp->data.reg.reg].number) {
                        return FALSE;
                    }
                    break;
                case PCOp_MEMORY:
                    if (matchOp->kind != PCOp_MEMORY) {
                        return FALSE;
                    }
                    if (matchOp->data.mem.obj != avOp->data.mem.obj) {
                        return FALSE;
                    }
                    if (matchOp->data.mem.offset != avOp->data.mem.offset) {
                        return FALSE;
                    }
                    if ((unsigned char)matchOp->arg != (unsigned char)avOp->arg) {
                        return FALSE;
                    }
                    break;
                case PCOp_IMMEDIATE:
                    if (matchOp->kind != PCOp_IMMEDIATE) {
                        return FALSE;
                    }
                    if (matchOp->data.imm.value != avOp->data.imm.value) {
                        return FALSE;
                    }
                    break;
                case PCOp_LABEL:
                    if (matchOp->kind != PCOp_LABEL) {
                        return FALSE;
                    }
                    if (matchOp->data.label.label != avOp->data.label.label) {
                        return FALSE;
                    }
                    break;
                case PCOp_SYSREG:
                    if (matchOp->data.reg.reg != 912) {
                        return FALSE;
                    }
                    if (matchOp->data.reg.reg != avOp->data.reg.reg) {
                        return FALSE;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    if ((match->flags & (fIsRead | fOpFromMemory)) &&
        match->alias->valuenumber != av->aliasnumber) {
        return FALSE;
    }

    return TRUE;
}

static void chooselocation(AvailableValue* av, PCodeArg* op) {
    PCodeArg* baseop;
    ValueLabel* labelled;

    baseop = &av->pcode->args[0];
    labelled = av->labelled;
    while (labelled) {
        if (labelled->op.data.reg.reg == baseop->data.reg.reg) {
            *op = labelled->op;
            return;
        }
        labelled = labelled->next;
    }

    *op = av->labelled[0].op;
}

static int findavailablevalue(PCode* pcode, PCodeArg* op) {
    AvailableValue* av;
    PCodeArg swap;
    PCodeArg unswap;

    for (av = opvalue[pcode->op]; av; av = av->next) {
        if (av->labelled &&
            av->pcode->flags == pcode->flags &&
            av->pcode->argCount == pcode->argCount) {
            if (!matchvalues(av, pcode)) {
                if (!(pcode->flags & fCommutative)) {
                    continue;
                }

                swap = pcode->args[1];
                pcode->args[1] = pcode->args[2];
                pcode->args[2] = swap;

                if (!matchvalues(av, pcode)) {
                    unswap = pcode->args[1];
                    pcode->args[1] = pcode->args[2];
                    pcode->args[2] = unswap;
                    continue;
                }
            }
            chooselocation(av, op);
            return TRUE;
        }
    }

    return FALSE;
}

static void addavailablevalue(PCode* pcode) {
    AvailableValue* av;
    PCodeArg* op;
    int i;

    av = oalloc(sizeof(AvailableValue) + sizeof(int) * pcode->argCount);
    av->labelled = NULL;
    av->pcode = pcode;
    for (i = 0, op = &pcode->args[0]; i < pcode->argCount; i++, op++) {
        if (op->kind == PCOp_REGISTER) {
            av->opnumbers[i] = regvalue[op->arg][op->data.reg.reg].number;
        }
    }

    if (pcode->flags & (fIsRead | fOpFromMemory)) {
        av->aliasnumber = pcode->alias->valuenumber;
    }

    op = &pcode->args[0];
    av->killedregister = killregister(op);
    labelvalue(av, op);
    regvalue[op->arg][op->data.reg.reg].available = av;
    av->next = opvalue[pcode->op], opvalue[pcode->op] = av;
}

static int isCSEop(PCode* pcode) {
    PCodeArg* baseOp;
    PCodeArg* op;
    int i;

    baseOp = &pcode->args[0];

    switch (pcode->op) {
        case PC_CMPI:
        case PC_CMP:
        case PC_CMPLI:
        case PC_CMPL:
        case PC_FCMPU:
        case PC_FCMPO:
            if (!moreaggressiveoptimization) {
                return FALSE;
            }
            break;
        case PC_LI:
        case PC_LIS:
            if (!moreaggressiveoptimization) {
                return FALSE;
            }
            if (pcode->args[0].data.reg.reg < first_fe_temporary_register[RegClass_GPR] ||
                pcode->args[0].data.reg.reg > last_temporary_register[RegClass_GPR]) {
                return FALSE;
            }
            break;
    }

    if (PCODE_FLAG_SET_F(pcode) &
        (fIsVolatile | fSideEffects | fOverflow | fSetsCarry | fRecordBit)) {
        return FALSE;
    }

    for (i = 0, op = &pcode->args[0]; i < pcode->argCount; i++, op++) {
        if (op != baseOp &&
            op->kind == baseOp->kind &&
            op->arg == baseOp->arg &&
            op->data.reg.reg == baseOp->data.reg.reg) {
            return FALSE;
        }
    }

    return TRUE;
}

static int isCSEload(PCode* pcode) {
    PCodeArg* op;
    int i;
    int count;

    count = 0;
    for (i = 0, op = &pcode->args[0]; i < pcode->argCount; i++, op++) {
        if (op->kind == PCOp_REGISTER && (op->data.reg.effect & EffectWrite)) {
            count++;
        }
    }

    return count == 1;
}

static void registercopy(PCode* pcode) {
    PCodeArg* dest;
    PCodeArg* src;

    dest = &pcode->args[0];
    src = &pcode->args[1];
    if (samevalue(src, dest)) {
        deletepcode(pcode);
    } else {
        copyregister(src, dest);
    }
}

static PCode* recentlystored(Alias* alias, PCodeArg* op) {
    PCode* pc;
    if ((pc = alias->valuepcode) &&
        alias->valuenumber == regvalue[pc->args[0].arg][pc->args[0].data.reg.reg].number) {
        *op = pc->args[0];
        return pc;
    } else {
        return NULL;
    }
}

static void simpleload(PCode* pcode) {
    PCodeArg* origOp;
    PCodeArg op;
    PCode* rs;
    int kind;
    PCode* copy;

    origOp = &pcode->args[0];
    if ((pcode->flags & fIsVolatile) || !isCSEload(pcode)) {
        killregisters(pcode);
    } else if (findavailablevalue(pcode, &op)) {
        if (!samevalue(origOp, &op)) {
            insertpcodebefore(pcode, makecopyinstruction(&op, origOp));
            copyregister(&op, origOp);
        }
        deletepcode(pcode);
        removedcommonsubexpressions = TRUE;
    } else {
        rs = recentlystored(pcode->alias, &op);
        if (rs) {
            kind = can_reuse_stored_value(rs, pcode);
        }
        if (rs && kind) {
            if (kind != 1 || !samevalue(origOp, &op)) {
                copy = makecopyforload(kind, &op, origOp);
                insertpcodebefore(pcode, copy);
                if (copy->flags & fIsMove) {
                    copyregister(&op, origOp);
                } else {
                    addavailablevalue(copy);
                }
            }
            deletepcode(pcode);
            removedcommonsubexpressions = TRUE;
        } else {
            addavailablevalue(pcode);
        }
    }
}

static void simplestore(PCode* pcode) {
    update_alias_value(pcode->alias, pcode);
#if VERSION == VERSION_GC_2_0P2
    update_small_data_values(pcode);
#endif
    killregisters(pcode);
}

static void pointerload(PCode* pcode) {
    PCodeArg* op;
    PCodeArg buf;

    op = &pcode->args[0];

    if ((pcode->flags & fIsVolatile) || !isCSEload(pcode)) {
        killregisters(pcode);
    } else if (findavailablevalue(pcode, &buf)) {
        if (!samevalue(op, &buf)) {
            insertpcodebefore(pcode, makecopyinstruction(&buf, op));
            copyregister(&buf, op);
        }
        deletepcode(pcode);
        removedcommonsubexpressions = TRUE;
    } else {
        addavailablevalue(pcode);
    }
}

static void pointerstore(PCode* pcode) {
    update_alias_value(pcode->alias, NULL);
#if VERSION == VERSION_GC_2_0P2
    update_small_data_values(pcode);
#endif
    killregisters(pcode);
}

static void arithmeticop(PCode* pcode) {
    PCodeArg* op;
    PCodeArg buf;

    op = &pcode->args[0];

    if (findavailablevalue(pcode, &buf)) {
        if (!samevalue(op, &buf)) {
            insertpcodebefore(pcode, makecopyinstruction(&buf, op));
            copyregister(&buf, op);
        }
        deletepcode(pcode);
        removedcommonsubexpressions = TRUE;
    } else {
        addavailablevalue(pcode);
    }
}

static void functioncall(PCode* pcode) {
    killregisters(pcode);
    if (coloring) {
        update_all_alias_values();
        killallCSEs();
    } else {
        update_alias_value(pcode->alias, NULL);
    }
}

static void operatefrommemory(PCode* pcode) {
    CError_FATAL(1015);
}

static void operatetomemory(PCode* pcode) {
    CError_FATAL(1046);
}

static void propagatecopiesto(PCode* pcode) {
    PCodeArg* op;
    int i;
    RegValue* value;

    for (i = 0, op = &pcode->args[0]; i < pcode->argCount; i++, op++) {
        if (op->kind == PCOp_REGISTER &&
            (op->data.reg.effect & (EffectRead | EffectWrite | EffectPinned)) == EffectRead &&
            op->data.reg.reg >= n_real_registers[op->arg]) {
            value = &regvalue[op->arg][op->data.reg.reg];
            if (value->copyreg &&
                value->copyreg >= n_real_registers[op->arg] &&
                value->number == regvalue[op->arg][value->copyreg].number) {
                op->data.reg.reg = value->copyreg;
            }
        }
    }
}

static void removecsesfrombasicblock(PCodeBlock* block) {
    PCode* pcode;
    PCode* next;

    for (pcode = block->firstPCode; pcode; pcode = next) {
        next = pcode->nextPCode;
        propagatecopiesto(pcode);
        if (pcode->flags & fIsMove) {
            registercopy(pcode);
        } else if ((pcode->flags & fIsCall) && (pcode->flags & (fLink | fSideEffects))) {
            functioncall(pcode);
        } else if (pcode->flags & fIsRead) {
            if (pcode->flags & fIsPtrOp) {
                pointerload(pcode);
            } else {
                simpleload(pcode);
            }
        } else if (pcode->flags & fIsWrite) {
            if (pcode->flags & fIsPtrOp) {
                pointerstore(pcode);
            } else {
                simplestore(pcode);
            }
        } else if (pcode->flags & fOpFromMemory) {
            operatefrommemory(pcode);
        } else if (pcode->flags & fOpToMemory) {
            operatetomemory(pcode);
        } else if ((pcode->flags & fIsCSE) && isCSEop(pcode)) {
            arithmeticop(pcode);
        } else {
            killregisters(pcode);
        }
    }

    block->flags |= fVisited;
}

static void getvaluestate(State* state) {
    state->stackedvalues = stackedvalues;
    state->valueceiling = valueceiling;
}

static void setvaluestate(State* state) {
    stackedvalues = state->stackedvalues;
    valueceiling = state->valueceiling;
}

static void forkvaluestate(int number) {
    stackedvalues = NULL;
    valueceiling = number;
}

static void regressvaluestate(void) {
    AvailableValue* av;
    AvailableValue** ptr;
    int i;
    StackedValue* stacked;

    for (i = 0; i < OPCODE_MAX; i++) {
        ptr = &opvalue[i];
        while ((av = *ptr)) {
            if (av->killedregister >= valueceiling) {
                *ptr = av->next;
            } else {
                ptr = &av->next;
            }
        }
    }

    for (stacked = stackedvalues; stacked; stacked = stacked->next) {
        unstackvalue(stacked);
    }
}

static void removecsesfromextendedbasicblock(PCodeBlock* block) {
    PCLink* succ;
    int counter;
    State state;

    removecsesfrombasicblock(block);
    while (block->successors &&
           !block->successors->nextLink &&
           block->successors->block->predecessors &&
           !block->successors->block->predecessors->nextLink) {
        removecsesfrombasicblock(block = block->successors->block);
    }

    counter = 0;
    for (succ = block->successors; succ; succ = succ->nextLink) {
        if (!(succ->block->flags & fVisited) &&
            succ->block->predecessors &&
            !succ->block->predecessors->nextLink) {
            counter++;
        }
    }

    if (!counter) {
        return;
    }

    getvaluestate(&state);
    forkvaluestate(nextvaluenumber);
    for (succ = block->successors; succ; succ = succ->nextLink) {
        if (!(succ->block->flags & fVisited) &&
            succ->block->predecessors &&
            !succ->block->predecessors->nextLink) {
            removecsesfromextendedbasicblock(succ->block);
            regressvaluestate();
        }
    }
    setvaluestate(&state);
}

void removecommonsubexpressions(Object* proc, int moreAggressive) {
    PCodeBlock* block;

    moreaggressiveoptimization = moreAggressive;
    removedcommonsubexpressions = FALSE;
    gather_alias_info();
    allocatecsedatastructures();

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        block->flags &= ~fVisited;
    }

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        if (!(block->flags & fVisited)) {
            initializecsedatastructures();
            removecsesfromextendedbasicblock(block);
        }
    }

    freeoheap();
}
