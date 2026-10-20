#include "compiler/IroJump.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroUtil.h"
#include "compiler/CFunc.h"
#include "compiler/Exceptions.h"
#include "compiler/Switch.h"

static Boolean CheckChain(CLabel** labelptr) {
    IRONode* node;

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        if (node->first &&
            node->first->type == IROLinearLabel &&
            node->first->u.label.label == *labelptr) {
            IROLinear* linear;
            CLabel* lab;
            for (linear = node->first->next, lab = NULL;
                 linear && (linear->type == IROLinearLabel || linear->type == IROLinearNop);
                 linear = linear->next) {
                if (linear->type == IROLinearLabel) {
                    lab = linear->u.label.label;
                }
            }

            if (linear->type == IROLinearGoto && *labelptr != linear->u.label.label) {
                *labelptr = linear->u.label.label;
                IRO_Dump("Chaining goto at %d\n", linear->index);
                return TRUE;
            }

            if (lab && *labelptr != lab) {
                *labelptr = lab;
            }
            return FALSE;
        }
    }

    return FALSE;
}

Boolean IRO_DoJumpChaining(void) {
    IRONode* node;
    IROLinear* linear;
    Boolean changed; // NOTE: invented name
    SwitchInfo* info;
    SwitchCase* curcase;
    Boolean result;

    result = FALSE;
    do {
        changed = FALSE;
        for (node = IRO_FirstNode; node; node = node->nextnode) {
            if (node->first) {
                linear = node->last;
                switch (linear->type) {
                    case IROLinearGoto:
                        if (CheckChain(&linear->u.label.label)) {
                            changed = TRUE;
                        }
                        break;
                    case IROLinearIf:
                    case IROLinearIfNot:
                        if (CheckChain(&linear->u.label.label)) {
                            changed = TRUE;
                        }
                        break;
                    case IROLinearSwitch:
                        info = linear->u.swtch.info;
                        for (curcase = info->cases; curcase; curcase = curcase->next) {
                            if (CheckChain(&curcase->label)) {
                                changed = TRUE;
                            }
                        }
                        if (CheckChain(&info->defaultlabel)) {
                            changed = TRUE;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
        result |= changed;
        IRO_CheckForUserBreak();
    } while (changed);

    return result;
}

void IRO_MakeReachable(IRONode* node) {
    UInt16 i;
    Boolean changed; // NOTE: invented name
    IRONode* scan;

    node->reachable = TRUE;
    do {
        changed = FALSE;
        for (scan = IRO_FirstNode; scan; scan = scan->nextnode) {
            if (scan->reachable && !scan->visited) {
                for (i = 0; i < scan->numsucc; i++) {
                    if (!IRO_NodeTable[scan->succ[i]]->reachable) {
                        changed = TRUE;
                        IRO_NodeTable[scan->succ[i]]->reachable = TRUE;
                    }
                }
                scan->visited = TRUE;
            }
        }
    } while (changed);
}

// NOTE: invented names: node, prevNode
Boolean IRO_RemoveUnreachable(void) {
    IRONode* node;
    IRONode* prevNode;
    IROLinear* scan;
    IROLinear* linear;
    Boolean result;
    ExceptionAction** actptr;
    ExceptionAction* act;

    result = FALSE;
    IRO_ComputeSuccPred();
    IRO_MakeReachable(IRO_FirstNode);
    prevNode = IRO_FirstNode;
    for (node = IRO_FirstNode->nextnode; node; node = node->nextnode) {
        if (node->first && !node->reachable) {
            IRO_Dump("Removing unreachable code at: %d\n", node->index);
            prevNode->nextnode = node->nextnode;
            prevNode->last->next = node->last->next;
            result = TRUE;
            for (linear = node->first;
                 linear && linear->type == IROLinearLabel && linear != node->last->next;
                 linear = linear->next) {
                for (scan = IRO_FirstLinear; scan; scan = scan->next) {
                    if (scan->stmt) {
                        scan->stmt->marked = FALSE;
                    }
                }

                for (scan = IRO_FirstLinear; scan; scan = scan->next) {
                    if (scan->stmt && !scan->stmt->marked) {
                        scan->stmt->marked = TRUE;
                        for (actptr = &scan->stmt->dobjstack; (act = *actptr);
                             actptr = &act->prev) {
                            if ((act->type == EAT_CATCHBLOCK &&
                                 act->data.catch_block.catch_label == linear->u.label.label) ||
                                (act->type == EAT_SPECIFICATION &&
                                 act->data.specification.unexp_label == linear->u.label.label)) {
                                *actptr = act->prev;
                            }
                        }
                    }
                }
            }
            if (node == IRO_LastNode) {
                IRO_LastNode = prevNode;
            }
        } else {
            prevNode = node;
        }
    }

    if (result) {
        IRO_ComputeSuccPred();
        IRO_ComputeDom();
    }

    IRO_CheckForUserBreak();
    return result;
}

Boolean IRO_RemoveRedundantJumps(void) {
    IRONode* node;
    IROLinear* linear;
    IROLinear* scan;
    IROLinear* scan2;
    SwitchInfo* info;
    SwitchCase* curcase;
    Boolean result;

    result = FALSE;
    for (node = IRO_FirstNode; node; node = node->nextnode) {
        if (node->first) {
            linear = node->last;
            switch (linear->type) {
                case IROLinearGoto:
                    scan = linear->next;
                    while (scan &&
                           ((scan->type == IROLinearNop) ||
                            (scan->type == IROLinearLabel &&
                             scan->u.label.label != linear->u.label.label))) {
                        scan = scan->next;
                    }
                    while (1) {
                        if (!scan) {
                            break;
                        }
                        if (scan->type != IROLinearLabel) {
                            break;
                        }
                        if (scan->u.label.label == linear->u.label.label) {
                            IRO_Dump("Removing goto next at %d\n", linear->index);
                            linear->type = IROLinearNop;
                            result = TRUE;
                            break;
                        }
                        scan = scan->next;
                    }
                    break;

                case IROLinearIf:
                case IROLinearIfNot:
                    scan = linear->next;
                    while (scan && scan->type == IROLinearNop) {
                        scan = scan->next;
                    }
                    if (scan && scan->type == IROLinearGoto) {
                        scan2 = scan->next;
                        while (scan2 &&
                               ((scan2->type == IROLinearNop) ||
                                (scan2->type == IROLinearLabel &&
                                 scan2->u.label.label != linear->u.label.label))) {
                            scan2 = scan2->next;
                        }

                        if (scan2 &&
                            scan2->type == IROLinearLabel &&
                            scan2->u.label.label == linear->u.label.label) {
                            if (linear->type == IROLinearIf) {
                                linear->type = IROLinearIfNot;
                            } else {
                                linear->type = IROLinearIf;
                            }

                            linear->u.label.label = scan->u.label.label;
                            scan->type = IROLinearNop;
                            IRO_Dump("Removing branch around goto at %d\n", linear->index);
                            result = TRUE;
                        }
                    }

                    scan2 = linear->next;
                    while (scan2 &&
                           ((scan2->type == IROLinearNop) ||
                            (scan2->type == IROLinearLabel &&
                             scan2->u.label.label != linear->u.label.label))) {
                        scan2 = scan2->next;
                    }
                    while (1) {
                        if (!scan2) {
                            break;
                        }
                        if (scan2->type != IROLinearLabel) {
                            break;
                        }
                        if (scan2->u.label.label == linear->u.label.label) {
                            IRO_Dump("Removing If/IfNot_Goto next at %d\n", linear->index);
                            linear->type = IROLinearNop;
                            IRO_CheckSideEffect(linear->u.label.cond);
                            result = TRUE;
                            break;
                        }
                        scan2 = scan2->next;
                    }
                    break;

                case IROLinearSwitch:
                    info = linear->u.swtch.info;
                    curcase = info->cases;
                    while (curcase && curcase->label == info->defaultlabel) {
                        curcase = curcase->next;
                    }
                    if (!curcase) {
                        IRO_Dump("Removing Switch next at %d\n", linear->index);
                        IRO_CheckSideEffect(linear->u.swtch.cond);
                        linear->type = IROLinearGoto;
                        linear->u.label.label = info->defaultlabel;
                        result = TRUE;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    if (result) {
        IRO_ComputeSuccPred();
        IRO_ComputeDom();
    }

    IRO_CheckForUserBreak();
    return result;
}

Boolean IRO_RemoveLabels(void) {
    Boolean result;
    IRONode* node;

    result = FALSE;
    IRO_ComputeSuccPred();
    for (node = IRO_FirstNode; node; node = node->nextnode) {
        if (node->first && node->first->type == IROLinearLabel && !node->referenced) {
            node->first->type = IROLinearNop;
            node->first->flags &= ~IROLF_StartsNode;
            result = TRUE;
        }
    }

    IRO_CheckForUserBreak();
    return result;
}
