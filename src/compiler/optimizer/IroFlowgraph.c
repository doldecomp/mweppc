#include "compiler/IroFlowgraph.h"
#include "compiler/IroCSE.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroPropagate.h"
#include "compiler/IROUseDef.h"
#include "compiler/IroUtil.h"
#include "compiler/BitVector.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CompilerTools.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/Switch.h"
#include <string.h>

UInt16 IRO_NumNodes;
IRONode* IRO_FirstNode;
IRONode* IRO_LastNode;
IRONode* IRO_EndNode;
IRONode** IRO_NodeTable;
BitVector* IRO_VarKills;
BitVector* IRO_Avail;
BitVector* IRO_FuncKills;
BitVector* IRO_ExprKills;

static IRONode* StartNode(IROLinear* linear) {
    IRONode* node = oalloc(sizeof(IRONode));

    node->index = IRO_NumNodes;
    node->numsucc = 0;
    node->succ = NULL;
    node->numpred = 0;
    node->pred = NULL;
    node->first = linear;
    node->last = linear;
    node->in = NULL;
    node->out = NULL;
    node->gen = NULL;
    node->kill = NULL;
    node->unused = 0;
    node->copyOut = NULL;
    node->dom = NULL;
    node->nextnode = NULL;
    node->reachable = FALSE;
    node->visited = FALSE;
    node->mustreach = FALSE;
    node->referenced = FALSE;
    node->loopdepth = 0;
    node->marked = FALSE;
    node->addressed = NULL;

    IRO_NumNodes++;
    if (!IRO_FirstNode) {
        IRO_FirstNode = node;
    } else {
        IRO_LastNode->nextnode = node;
    }
    IRO_LastNode = node;
    return node;
}

static void AddSucc(IRONode* a, IRONode* b) {
    a->succ[a->numsucc++] = b->index;
    b->numpred++;
}

static void AddLabelSucc(IRONode* node, CLabel* label) {
    IRONode* targetnode = (IRONode*)label->stmt;
    if (targetnode) {
        AddSucc(node, targetnode);
        targetnode->referenced = TRUE;
    } else {
        CError_FATAL(126);
    }
}

static void AddSwitchSucc(IRONode* node) {
    SwitchInfo* info = node->last->u.swtch.info;
    SwitchCase* curcase = info->cases;
    SInt32 i = 1;

    while (curcase) {
        i++;
        curcase = curcase->next;
    }

    node->succ = oalloc(sizeof(UInt16) * i);
    for (curcase = info->cases; curcase; curcase = curcase->next) {
        AddLabelSucc(node, curcase->label);
    }
    AddLabelSucc(node, info->defaultlabel);
}

static void AddPred(UInt32 a, UInt32 b) {
    IRONode* node = IRO_NodeTable[a];
    node->pred[node->numpred++] = b;
}

void IRO_ComputeSuccPred(void) {
    CLabel* label;
    IRONode* node;
    SInt32 count;
    ExceptionAction* action;
    IAEffects effects;
    UInt16 i;
    IROLinear* linear;

    for (label = Labels; label; label = label->next) {
        label->stmt = NULL;
    }

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        node->referenced = FALSE;
        node->numsucc = 0;
        node->numpred = 0;
        node->reachable = FALSE;
        node->visited = FALSE;
        if (node->first && node->first->type == IROLinearLabel) {
            node->first->u.label.label->stmt = (Statement*)node;
        }
    }

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        if (!node->first) {
            if (node->nextnode) {
                node->succ = oalloc(sizeof(UInt16));
                AddSucc(node, node->nextnode);
            }
        } else {
            linear = node->last;
next_linear:
            switch (linear->type) {
                case IROLinearReturn:
                case IROLinearEnd:
                    break;
                case IROLinearGoto:
                    node->succ = oalloc(sizeof(UInt16));
                    AddLabelSucc(node, linear->u.label.label);
                    break;
                case IROLinearIf:
                case IROLinearIfNot:
                    node->succ = oalloc(sizeof(UInt16) * 2);
                    AddSucc(node, node->nextnode);
                    AddLabelSucc(node, linear->u.label.label);
                    break;
                case IROLinearSwitch:
                    AddSwitchSucc(node);
                    break;
                case IROLinearFunccall:
                    count = 1;
                    if (linear->stmt && IRO_FunctionCallMightThrowException(linear)) {
                        for (action = linear->stmt->dobjstack; action; action = action->prev) {
                            if (action->type == EAT_CATCHBLOCK ||
                                action->type == EAT_SPECIFICATION) {
                                count++;
                            }
                        }
                    }
                    node->succ = oalloc(sizeof(UInt16) * count);
                    AddSucc(node, node->nextnode);
                    if (linear->stmt && IRO_FunctionCallMightThrowException(linear)) {
                        for (action = linear->stmt->dobjstack; action; action = action->prev) {
                            if (action->type == EAT_CATCHBLOCK) {
                                AddLabelSucc(node, action->data.catch_block.catch_label);
                            } else if (action->type == EAT_SPECIFICATION) {
                                AddLabelSucc(node, action->data.specification.unexp_label);
                            }
                        }
                    }
                    break;
                case IROLinearAsm:
                    CodeGen_GetAsmEffects(linear->u.asm_stmt, &effects);
                    node->succ =
                        oalloc(sizeof(UInt16) * (!effects.noFallthrough + effects.numlabels));
                    if (!effects.noFallthrough) {
                        AddSucc(node, node->nextnode);
                    }
                    for (i = 0; i < effects.numlabels; i++) {
                        AddLabelSucc(node, effects.labels[i]);
                    }
                    break;
                case IROLinearOp2Arg:
                    if (linear->nodetype == ECOMMA) {
                        linear = linear->u.diadic.right;
                        goto next_linear;
                    }
                default:
                    if (node->nextnode) {
                        node->succ = oalloc(sizeof(UInt16));
                        AddSucc(node, node->nextnode);
                    }
            }
        }
    }

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        if (node->numpred) {
            node->pred = oalloc(sizeof(UInt16) * node->numpred);
        } else {
            node->pred = NULL;
        }
        node->numpred = 0;
    }

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        for (i = 0; i < node->numsucc; i++) {
            AddPred(node->succ[i], node->index);
        }
    }

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        if (node->first && node->first->type == IROLinearLabel) {
            IROLinear* linear = node->first;
            do {
                if (linear->type == IROLinearBeginCatch ||
                    linear->type == IROLinearEndCatch ||
                    linear->type == IROLinearEndCatchDtor) {
                    node->referenced = TRUE;
                    break;
                }
            } while (linear != node->last && (linear = linear->next));
        }
    }
}

void IRO_ComputeDom(void) {
    BitVector* bv;
    IRONode* node;
    int repeat;
    SInt32 i;

    Bv_AllocVector(&IRO_FirstNode->dom, IRO_NumNodes);
    Bv_SetBit(IRO_FirstNode->index, IRO_FirstNode->dom);
    for (node = IRO_FirstNode->nextnode; node; node = node->nextnode) {
        Bv_AllocVector(&node->dom, IRO_NumNodes);
        Bv_Set(node->dom);
    }

    Bv_AllocVector(&bv, IRO_NumNodes);

    do {
        repeat = FALSE;
        for (node = IRO_FirstNode->nextnode; node; node = node->nextnode) {
            if (node->numpred) {
                Bv_Set(bv);
                for (i = 0; i < node->numpred; i++) {
                    Bv_And(IRO_NodeTable[node->pred[i]]->dom, bv);
                }
                Bv_SetBit(node->index, bv);
            } else {
                Bv_Clear(bv);
                Bv_SetBit(node->index, bv);
            }

            if (!Bv_Compare(bv, node->dom)) {
                Bv_Copy(bv, node->dom);
                repeat = TRUE;
            }
        }
    } while (repeat);
}

void IRO_BuildFlowgraph(IROLinear* linear) {
    IROLinear* scan;
    CLabel* label;
    ExceptionAction* action;
    IAEffects effects;
    IRONode* node;
    SInt32 i;
    int endsNode;

    for (label = Labels; label; label = label->next) {
        label->stmt = NULL;
    }

    scan = linear;
    IRO_NumNodes = 0;
    IRO_FirstNode = IRO_LastNode = IRO_EndNode = NULL;
    while (scan) {
        StartNode(scan);
        if (scan->type == IROLinearLabel) {
            scan->u.label.label->stmt = (Statement*)IRO_LastNode;
        }

        endsNode = FALSE;
        while (!endsNode && scan->next && !(scan->next->flags & IROLF_StartsNode)) {
            switch (scan->type) {
                case IROLinearGoto:
                case IROLinearReturn:
                case IROLinearEntry:
                case IROLinearExit:
                case IROLinearEnd:
                    endsNode = TRUE;
                    break;
                case IROLinearIf:
                case IROLinearIfNot:
                case IROLinearSwitch:
                    endsNode = TRUE;
skip:
                    if (scan->next->type == IROLinearLabel) {
                        IROLinear* nw = IRO_NewLinear(IROLinearNop);
                        nw->index = ++IRO_NumLinear;
                        nw->next = scan->next;
                        scan->next = nw;
                    }
                    break;
                case IROLinearFunccall:
                    if (scan->stmt && IRO_FunctionCallMightThrowException(scan)) {
                        for (action = scan->stmt->dobjstack; action; action = action->prev) {
                            if (action->type == EAT_CATCHBLOCK ||
                                action->type == EAT_SPECIFICATION) {
                                endsNode = TRUE;
                                goto skip;
                            }
                        }
                    }
                    break;
                case IROLinearAsm:
                    CodeGen_GetAsmEffects(scan->u.asm_stmt, &effects);
                    if (effects.numlabels) {
                        endsNode = TRUE;
                    }
                    break;
                default:
                    break;
            }
            if (!endsNode) {
                scan = scan->next;
            }
        }

        if (scan->type == IROLinearEnd) {
            IRO_EndNode = IRO_LastNode;
        }
        IRO_LastNode->last = scan;
        scan = scan->next;
    }

    IRO_NodeTable = oalloc(IRO_NumNodes * sizeof(IRONode*));
    memset(IRO_NodeTable, 0, IRO_NumNodes * sizeof(IRONode*));
    for (node = IRO_FirstNode, i = 0; node; node = node->nextnode) {
        IRO_NodeTable[i++] = node;
    }

    IRO_ComputeSuccPred();
    IRO_ComputeDom();
    IRO_CheckForUserBreak();
}

IRONode* IRO_NewFlowGraphNode(void) {
    IRONode* node = oalloc(sizeof(IRONode));
    memset(node, 0, sizeof(IRONode));
    node->index = IRO_NumNodes;
    IRO_NumNodes++;
    node->nextnode = NULL;
    return node;
}

IRONode* IRO_MergeFlowGraphNodes(IRONode* a, IRONode* b) {
    IRONode* succ;
    Boolean found;
    UInt32 i;
    UInt32 j;
    UInt32 k;

    if (a->nextnode == b && a->last && b->first && a->last->next == b->first) {
        if (b->first->type == IROLinearLabel) {
            IRO_NopOut(b->first);
        }

        a->nextnode = b->nextnode;
        a->last = b->last;

        for (i = 0; i < a->numsucc; i++) {
            if (b->index == a->succ[i]) {
                for (j = i; j < a->numsucc; j++) {
                    if ((j + 1) < a->numsucc) {
                        a->succ[j] = a->succ[j + 1];
                    } else {
                        a->succ[j] = 0;
                    }
                }
                a->numsucc--;
                break;
            }
        }

        for (i = 0; i < b->numsucc; i++) {
            succ = IRO_NodeTable[b->succ[i]];
            for (j = 0; j < a->numsucc; j++) {
                if (b->succ[i] == a->succ[j]) {
                    break;
                }
            }

            if (j == a->numsucc) {
                AddSucc(a, succ);
                succ->numpred--;
            }

            found = FALSE;
            for (j = 0; j < succ->numpred; j++) {
                if (a->index == succ->pred[j]) {
                    found = TRUE;
                    break;
                }
            }

            for (j = 0; j < succ->numpred; j++) {
                if (b->index == succ->pred[j]) {
                    if (!found) {
                        succ->pred[j] = a->index;
                    } else {
                        for (k = j; k < succ->numpred; k++) {
                            if ((k + 1) < succ->numpred) {
                                succ->pred[k] = succ->pred[k + 1];
                            } else {
                                succ->pred[k] = 0;
                            }
                        }
                        succ->numpred--;
                    }
                    break;
                }
            }
        }

        b->numsucc = b->numpred = 0;
        b->first = b->last = NULL;
        b->nextnode = NULL;
        b->reachable = FALSE;
        b->visited = FALSE;
        b->mustreach = FALSE;
        b->referenced = FALSE;
        b->loopdepth = 0;

        if (IRO_LastNode == b) {
            IRO_LastNode = a;
        }

        if (IRO_FirstExpr && IRO_LastExpr) {
            IROExpr* expr;
            for (expr = IRO_FirstExpr; expr && expr != IRO_LastExpr->next; expr = expr->next) {
                if (expr->node == b) {
                    expr->node = a;
                }
            }
        }

        if (IRO_FirstAssign && IRO_LastAssign) {
            IROAssign* assign;
            for (assign = IRO_FirstAssign; assign && assign != IRO_LastAssign->next;
                 assign = assign->next) {
                if (assign->node == b) {
                    assign->node = a;
                }
            }
        }

        if (IRO_FirstVarUse && IRO_LastVarUse) {
            IROUse* use;
            for (use = IRO_FirstVarUse; use && use != IRO_LastVarUse->globalnext;
                 use = use->globalnext) {
                if (use->node == b) {
                    use->node = a;
                }
            }
        }

        IRO_NodeTable[b->index] = NULL;
        return a;
    }

    return NULL;
}
