#include "compiler/IroExprRegeneration.h"
#include "compiler/IroCSE.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroSubable.h"
#include "compiler/IroTransform.h"
#include "compiler/IROUseDef.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/BitVector.h"
#include "compiler/CDecl.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/types.h"
#include <setjmp.h>
#include <string.h>

static void Regen_GetExprUses(IROLinear* linear, Boolean isEntry);
static void RebuildPossibleCondExpression(IRONode* fnode);
static void AddNodeAndSuccessorsRecursively(IRONodes* nodes, IRONode* fnode1, IRONode* fnode2);

// NOTE: invented names: node, prevNode
static Boolean FindFlowgraphNodeThatStartsWithLabel(
    CLabel* label, IRONode** node, IRONode** prevNode
) {
    IRONode* prev;
    IRONode* iter;

    // (the original's: iter is left unset for an empty flowgraph, which never reaches here)
    prev = IRO_FirstNode;
    if (prev != NULL) {
        iter = prev->nextnode;
    }

    while (iter) {
        if (iter->first->type == IROLinearLabel && iter->first->u.label.label == label) {
            *node = iter;
            *prevNode = prev;
            return TRUE;
        }

        prev = iter;
        iter = iter->nextnode;
    }

    return FALSE;
}

static IROLinear* FindLastDiadicTopLevelAssignmentInFlowgraphNode(IRONode* fnode) {
    IROLinear* scan;
    IROLinear* result;

    result = NULL;

    for (scan = fnode->first; scan != fnode->last->next; scan = scan->next) {
        if (scan->type == IROLinearOp2Arg &&
            scan->nodetype == EASS &&
            !(scan->flags & IROLF_Reffed)) {
            result = scan;
        }
    }

    return result;
}

// NOTE: invented names: use, enode, nd, replaceNode
static Boolean RewriteUse(IROUse* use, ENode* enode, IROLinear* nd, Boolean replaceNode) {
    IROLinear* father;
    IROLinear* father2;

    if (use &&
        use->reachingDefCount &&
        use->linear &&
        use->linear->type == IROLinearOperand &&
        use->linear->u.node->type == EOBJREF &&
        use->linear->u.node->data.objref == enode->data.objref &&
        (father = IRO_LocateFather(use->linear)) &&
        father->type == IROLinearOp1Arg &&
        father->nodetype == EINDIRECT &&
        father->rtype == nd->rtype &&
        (father2 = IRO_LocateFather(father)) &&
        ((father2->type != IROLinearOp1Arg && father2->type != IROLinearOp2Arg) ||
         !IRO_IsModifyOp[father2->nodetype])) {
        if (replaceNode) {
            IRO_LocateFather_Cut_And_Paste(father, nd);
        }
        return TRUE;
    }

    return FALSE;
}

#pragma pack(push, 2)
static struct {
    jmp_buf buf;
    UInt16 index;
    Boolean found; // NOTE: invented name
} scuai;
#pragma pack(pop)

static void StatementContainsUseAction(IROLinear* linear, Boolean isFirst) {
    if (isFirst && linear->index == scuai.index) {
        scuai.found = TRUE;
        longjmp(scuai.buf, 1);
    }
}

// NOTE: invented names: stmt, use
static Boolean StatementContainsUse(IROLinear* stmt, IROLinear* use) {
    memset(&scuai, 0, sizeof(scuai));
    scuai.index = use->index;
    scuai.found = FALSE;

    if (!setjmp(scuai.buf)) {
        IRO_WalkInts(stmt, stmt, StatementContainsUseAction);
    }

    return scuai.found;
}

#pragma pack(push, 2)
static struct {
    jmp_buf buf;
    Boolean found; // NOTE: invented name
} scseai;
#pragma pack(pop)

static void StatementContainsSideEffectAction(IROLinear* linear, Boolean isFirst) {
    if (isFirst) {
        switch (linear->type) {
            case IROLinearOperand:
            case IROLinearOp1Arg:
            case IROLinearOp2Arg:
            case IROLinearOp3Arg:
            case IROLinearFunccall:
            case IROLinearAsm:
                if (IRO_HasSideEffect(linear)) {
                    scseai.found = TRUE;
                    longjmp(scseai.buf, 1);
                }
                break;
            default:
                break;
        }
    }
}

static Boolean StatementContainsSideEffect(IROLinear* linear) {
    memset(&scseai, 0, sizeof(scseai));
    scseai.found = FALSE;

    if (!setjmp(scseai.buf)) {
        IRO_WalkInts(linear, linear, StatementContainsSideEffectAction);
    }

    return scseai.found;
}

// whether an operand of nd evaluated unordered with skip has a side effect
static void CheckUnorderedRegionsForSideEffects(IROLinear* nd, IROLinear* skip, Boolean* result) {
    int i;

    switch (nd->type) {
        case IROLinearOp2Arg:
            if (nd->u.diadic.left != skip && IRO_HasSideEffect(nd->u.diadic.left)) {
                *result = TRUE;
            }
            if (nd->nodetype != ELAND && nd->nodetype != ELOR && nd->nodetype != ECOMMA) {
                if (nd->u.diadic.right != skip && IRO_HasSideEffect(nd->u.diadic.right)) {
                    *result = TRUE;
                }
            }
            break;
        case IROLinearOp3Arg:
            if (nd->u.args3.a != skip && IRO_HasSideEffect(nd->u.args3.a)) {
                *result = TRUE;
            }
            if (nd->nodetype != ECOND && nd->nodetype != ECONDASS) {
                if (nd->u.args3.b != skip && IRO_HasSideEffect(nd->u.args3.b)) {
                    *result = TRUE;
                }
                if (nd->u.args3.c != skip && IRO_HasSideEffect(nd->u.args3.c)) {
                    *result = TRUE;
                }
            }
            break;
        case IROLinearFunccall:
            if (nd->u.funccall.callee != skip && IRO_HasSideEffect(nd->u.funccall.callee)) {
                *result = TRUE;
            }
            for (i = 0; !*result && i < nd->u.funccall.argCount; i++) {
                if (nd->u.funccall.args[i] != skip && IRO_HasSideEffect(nd->u.funccall.args[i])) {
                    *result = TRUE;
                }
            }
            break;
        default:
            break;
    }
}

// NOTE: invented names: fnode1, fnode2, fnode3, nd, clearMarks
static Boolean HasSideEffectsBeforeUse(
    IRONode* fnode1, IRONode* fnode2, IRONode* fnode3, IROLinear* nd, Boolean clearMarks
) {
    IRONode* fnode;
    IROLinear* scannd;
    UInt16 i;
    IROLinear* iter;
    Boolean result;

    if (fnode1 != fnode3) {
        if (clearMarks) {
            for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
                fnode->marked = FALSE;
            }
        } else if (fnode1->marked) {
            return FALSE;
        }
        fnode1->marked = TRUE;
    }

    for (scannd = fnode1->first; scannd && scannd != fnode1->last->next; scannd = scannd->next) {
        if (!(scannd->flags & IROLF_Reffed)) {
            if (StatementContainsUse(scannd, nd)) {
                result = FALSE;
                if (scannd != nd) {
                    iter = nd;
                    nd = IRO_LocateFather(nd);
                    while (nd && !result) {
                        CheckUnorderedRegionsForSideEffects(nd, iter, &result);
                        iter = nd;
                        nd = IRO_LocateFather(nd);
                    }
                }
                return result;
            }
            if (StatementContainsSideEffect(scannd)) {
                return TRUE;
            }
        }
    }

    if (fnode1 != fnode3) {
        for (i = 0; i < fnode1->numsucc; i++) {
            if (HasSideEffectsBeforeUse(
                    IRO_NodeTable[fnode1->succ[i]], fnode2, fnode3, nd, FALSE
                )) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static void CheckUnorderedRegionForUses(IROLinear* nd, BitVector* bv, Boolean* result) {
    Bv_Clear(IRO_VarKills);
    IRO_WalkInts(nd, nd, Regen_GetExprUses);
    if (Bv_BitsInCommon(bv, IRO_VarKills)) {
        *result = TRUE;
    }
}

// whether an operand of nd evaluated unordered with skip uses a variable of bv
static void CheckUnorderedRegionsForUses(
    IROLinear* nd, IROLinear* skip, BitVector* bv, Boolean* result
) {
    int i;

    switch (nd->type) {
        case IROLinearOp2Arg:
            if (nd->u.diadic.left != skip) {
                CheckUnorderedRegionForUses(nd->u.diadic.left, bv, result);
            }
            if (nd->nodetype != ELAND && nd->nodetype != ELOR && nd->nodetype != ECOMMA) {
                if (nd->u.diadic.right != skip) {
                    CheckUnorderedRegionForUses(nd->u.diadic.right, bv, result);
                }
            }
            break;
        case IROLinearOp3Arg:
            if (nd->u.args3.a != skip) {
                CheckUnorderedRegionForUses(nd->u.args3.a, bv, result);
            }
            if (nd->nodetype != ECOND && nd->nodetype != ECONDASS) {
                if (nd->u.args3.b != skip) {
                    CheckUnorderedRegionForUses(nd->u.args3.b, bv, result);
                }
                if (nd->u.args3.c != skip) {
                    CheckUnorderedRegionForUses(nd->u.args3.c, bv, result);
                }
            }
            break;
        case IROLinearFunccall:
            if (nd->u.funccall.callee != skip) {
                CheckUnorderedRegionForUses(nd->u.funccall.callee, bv, result);
            }
            for (i = 0; !*result && i < nd->u.funccall.argCount; i++) {
                if (nd->u.funccall.args[i] != skip) {
                    CheckUnorderedRegionForUses(nd->u.funccall.args[i], bv, result);
                }
            }
            break;
        default:
            break;
    }
}

// NOTE: invented names: fnode1, fnode2, fnode3, nd, bv, clearMarks
static Boolean UsesAKilledVarBeforeUse(
    IRONode* fnode1,
    IRONode* fnode2,
    IRONode* fnode3,
    IROLinear* nd,
    BitVector* bv,
    Boolean clearMarks
) {
    IRONode* fnode;
    UInt16 i;
    IROLinear* scannd;
    IROLinear* iter;
    Boolean result;

    if (fnode1 != fnode3) {
        if (clearMarks) {
            for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
                fnode->marked = FALSE;
            }
        } else if (fnode1->marked) {
            return FALSE;
        }
        fnode1->marked = TRUE;
    }

    for (scannd = fnode1->first; scannd && scannd != fnode1->last->next; scannd = scannd->next) {
        if (!(scannd->flags & IROLF_Reffed)) {
            if (StatementContainsUse(scannd, nd)) {
                result = FALSE;
                if (scannd != nd) {
                    iter = nd;
                    nd = IRO_LocateFather(nd);
                    while (nd && !result) {
                        CheckUnorderedRegionsForUses(nd, iter, bv, &result);
                        iter = nd;
                        nd = IRO_LocateFather(nd);
                    }
                }
                return result;
            }
            Bv_Clear(IRO_VarKills);
            IRO_WalkInts(scannd, scannd, Regen_GetExprUses);
            if (Bv_BitsInCommon(bv, IRO_VarKills)) {
                return TRUE;
            }
        }
    }

    if (fnode1 != fnode3) {
        for (i = 0; i < fnode1->numsucc; i++) {
            if (UsesAKilledVarBeforeUse(
                    IRO_NodeTable[fnode1->succ[i]], fnode2, fnode3, nd, bv, FALSE
                )) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static void Regen_GetExprUses(IROLinear* linear, Boolean isEntry) {
    Object* obj;
    VarRecord* var;

    if (isEntry) {
        if (linear->type == IROLinearOperand && linear->u.node->type == EOBJREF) {
            obj = linear->u.node->data.objref;
            if ((linear->flags & IROLF_Ind) &&
                (!(linear->flags & IROLF_Assigned) || (linear->flags & IROLF_Used))) {
                if ((var = IRO_FindVar(obj, FALSE, TRUE))) {
                    Bv_SetBit(var->index, IRO_VarKills);
                }
            }
        }
    }
}

static void Regen_GetExprKills(IROLinear* linear, Boolean isEntry) {
    if (isEntry) {
        IRO_GetKills(linear);
    }
}

static void CheckUnorderedRegionForSideEffectsAndUses(
    IROLinear* nd, BitVector* bv1, BitVector* bv2, Boolean* result
) {
    if (IRO_HasSideEffect(nd)) {
        *result = TRUE;
    } else {
        Bv_Clear(bv2);
        Bv_Clear(IRO_VarKills);
        IRO_WalkTree(nd, Regen_GetExprUses);
        Bv_Or(IRO_VarKills, bv2);

        if (Bv_BitsInCommon(bv1, bv2)) {
            *result = TRUE;
        }
    }
}

// NOTE: invented names: nd, excluded
static void CheckUnorderedRegionsForSideEffectsAndUses(
    IROLinear* nd, IROLinear* excluded, BitVector* bv1, BitVector* bv2, Boolean* result
) {
    int i;

    switch (nd->type) {
        case IROLinearOp2Arg:
            if (nd->nodetype != ELAND && nd->nodetype != ELOR && nd->nodetype != ECOMMA) {
                if (nd->u.diadic.left != excluded) {
                    CheckUnorderedRegionForSideEffectsAndUses(nd->u.diadic.left, bv1, bv2, result);
                }
                if (nd->u.diadic.right != excluded) {
                    CheckUnorderedRegionForSideEffectsAndUses(nd->u.diadic.right, bv1, bv2, result);
                }
            }
            break;
        case IROLinearFunccall:
            if (nd->u.funccall.callee != excluded) {
                CheckUnorderedRegionForSideEffectsAndUses(nd->u.funccall.callee, bv1, bv2, result);
            }
            for (i = 0; !*result && i < nd->u.funccall.argCount; i++) {
                if (nd->u.funccall.args[i] != excluded) {
                    CheckUnorderedRegionForSideEffectsAndUses(
                        nd->u.funccall.args[i], bv1, bv2, result
                    );
                }
            }
            break;
        default:
            break;
    }
}

// NOTE: invented names: fnode1, fnode2, nd, stopAtReturn
static Boolean CheckThenOrElseBranch(
    IRONode* fnode1, IRONode* fnode2, IROLinear* nd, Boolean stopAtReturn
) {
    IRONode* fnode;
    IROLinear* scannd;
    IRONodes nodes;
    Boolean result;

    IRO_FNodeWorkStack_Init(&nodes);

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        fnode->marked = FALSE;
    }
    AddNodeAndSuccessorsRecursively(&nodes, fnode1, fnode2);

    while (IRO_FNodeWorkStack_Count(&nodes)) {
        fnode = IRO_NodeTable[IRO_FNodeWorkStack_Remove(&nodes)];
        if (fnode) {
            if (!Bv_IsBitSet(fnode1->index, fnode->dom)) {
                result = FALSE;
                goto done;
            }

            for (scannd = fnode->first; scannd && scannd != fnode->last->next;
                 scannd = scannd->next) {
                if (!nd || scannd != nd) {
                    if (!stopAtReturn || (scannd->type != IROLinearReturn)) {
                        if (fnode->numpred == 2 && scannd->type == IROLinearLabel) {
                            RebuildPossibleCondExpression(fnode);
                        }

                        if (scannd->type != IROLinearNop &&
                            scannd->type != IROLinearLabel &&
                            scannd->type != IROLinearGoto &&
                            !(scannd->flags & IROLF_Reffed)) {
                            result = FALSE;
                            goto done;
                        }
                    }
                }
            }
        }
    }

    result = TRUE;
done:
    IRO_FNodeWorkStack_Free(&nodes);
    return result;
}

static void RebuildPossibleCondExpression(IRONode* fnode) {
    IRONode* pred1;
    IRONode* pred2;
    IROLinear* leftAss;
    IROLinear* rightAss;
    IROLinear* cond;
    IROLinear* leftVal;
    IROLinear* rightVal;
    IRONode* left;
    IRONode* right;
    IRONode* ifnode;
    ENode* objnode;
    IRODef* def1;
    IRODef* def2;
    IRODef* def;
    UInt32 useCount;
    UInt32 rewrittenUseCount;
    Boolean killedFlag;
    Boolean sideEffectFlag;
    UInt32 i;
    Boolean compareType;
    Type* type;
    IROLinear copy1;
    IROLinear copy2;
    IROLinear* tmp;
    IROUse* use;
    Object* obj;
    IRONode* target;

    if (!fnode || fnode->numpred != 2 || !fnode->first || fnode->first->type != IROLinearLabel) {
        return;
    }

    pred1 = IRO_NodeTable[fnode->pred[0]];
    pred2 = IRO_NodeTable[fnode->pred[1]];
    if (!pred1 || !pred2 || pred1->numsucc != 1 || pred2->numsucc != 1) {
        return;
    }

    ifnode = NULL;
    for (i = 0; i < IRO_NumNodes; i++) {
        if (Bv_IsBitSet(i, fnode->dom) &&
            (ifnode = IRO_NodeTable[i]) &&
            ifnode->last &&
            (ifnode->last->type == IROLinearIf || ifnode->last->type == IROLinearIfNot) &&
            ifnode->last->u.label.label &&
            ifnode->numsucc == 2) {
            if (Bv_IsBitSet(ifnode->succ[0], pred1->dom) &&
                !Bv_IsBitSet(ifnode->succ[0], pred2->dom) &&
                Bv_IsBitSet(ifnode->succ[1], pred2->dom) &&
                !Bv_IsBitSet(ifnode->succ[1], pred1->dom)) {
                break;
            }

            if (Bv_IsBitSet(ifnode->succ[0], pred2->dom) &&
                !Bv_IsBitSet(ifnode->succ[0], pred1->dom) &&
                Bv_IsBitSet(ifnode->succ[1], pred1->dom) &&
                !Bv_IsBitSet(ifnode->succ[1], pred2->dom)) {
                break;
            }
        }
    }

    if (i >= IRO_NumNodes) {
        return;
    }

    target = IRO_NodeTable[ifnode->succ[0]];
    if (target &&
        target->numpred == 1 &&
        target->first &&
        target->first->type == IROLinearLabel &&
        target->first->u.label.label == ifnode->last->u.label.label) {
        (void)0;
    } else {
        target = IRO_NodeTable[ifnode->succ[1]];
        if (target &&
            target->numpred == 1 &&
            target->first &&
            target->first->type == IROLinearLabel &&
            target->first->u.label.label == ifnode->last->u.label.label) {
        } else {
            return;
        }
    }

    if (Bv_IsBitSet(target->index, pred1->dom)) {
        left = pred2;
        right = pred1;
    } else {
        left = pred1;
        right = pred2;
    }

    leftAss = FindLastDiadicTopLevelAssignmentInFlowgraphNode(left);
    if (!leftAss ||
        leftAss->type != IROLinearOp2Arg ||
        !leftAss->u.diadic.left ||
        leftAss->u.diadic.left->type != IROLinearOp1Arg ||
        leftAss->u.diadic.left->nodetype != EINDIRECT ||
        !leftAss->u.diadic.left->u.monadic ||
        leftAss->u.diadic.left->u.monadic->type != IROLinearOperand ||
        !(objnode = leftAss->u.diadic.left->u.monadic->u.node) ||
        !objnode->rtype ||
        !IS_TYPE_POINTER_ONLY(objnode->rtype) ||
        !objnode->data.objref ||
        is_volatile_object(objnode->data.objref) ||
        objnode->data.objref->datatype != DLOCAL) {
        objnode = NULL;
    }

    if (objnode) {
        rightAss = FindLastDiadicTopLevelAssignmentInFlowgraphNode(right);
        if (!rightAss ||
            rightAss->type != IROLinearOp2Arg ||
            !rightAss->u.diadic.left ||
            rightAss->u.diadic.left->type != IROLinearOp1Arg ||
            rightAss->u.diadic.left->nodetype != EINDIRECT ||
            !rightAss->u.diadic.left->u.monadic ||
            rightAss->u.diadic.left->u.monadic->type != IROLinearOperand ||
            !rightAss->u.diadic.left->u.monadic->u.node ||
            !rightAss->u.diadic.left->u.monadic->u.node->rtype ||
            !IS_TYPE_POINTER_ONLY(rightAss->u.diadic.left->u.monadic->u.node->rtype) ||
            rightAss->u.diadic.left->u.monadic->u.node->type != EOBJREF ||
            rightAss->u.diadic.left->u.monadic->u.node->data.objref != objnode->data.objref ||
            rightAss->rtype != leftAss->rtype) {
            objnode = NULL;
        }
    }

    if (objnode) {
        if (!CheckThenOrElseBranch(ifnode->nextnode, left, leftAss, FALSE) ||
            !CheckThenOrElseBranch(target, right, rightAss, FALSE)) {
            return;
        }

        useCount = rewrittenUseCount = 0;
        sideEffectFlag = killedFlag = FALSE;
        if (!objnode->data.objref->varptr ||
            !objnode->data.objref->varptr->uses ||
            !objnode->data.objref->varptr->defs) {
            objnode = NULL;
        } else {
            def1 = def2 = NULL;
            for (def = objnode->data.objref->varptr->defs; def && (!def1 || !def2);
                 def = def->varnext) {
                if (def->useCount && def->linear == leftAss) {
                    def1 = def;
                } else if (def->useCount && def->linear == rightAss) {
                    def2 = def;
                }
            }

            if (!def1 || !def2) {
                objnode = NULL;
            } else {
                for (use = objnode->data.objref->varptr->uses; objnode && use; use = use->varnext) {
                    if (use->reachingDefCount) {
                        useCount++;
                        if (Bv_IsBitSet(def1->index, use->reachingDefs) &&
                            Bv_IsBitSet(def2->index, use->reachingDefs)) {
                            for (i = 0; objnode && i < use->reachingDefs->size; i++) {
                                if (i != (UInt32)def1->index &&
                                    i != (UInt32)def2->index &&
                                    Bv_IsBitSet(i, use->reachingDefs)) {
                                    objnode = NULL;
                                }
                            }

                            if (objnode && !Bv_IsBitSet(fnode->index, use->node->dom)) {
                                objnode = NULL;
                            }

                            if (objnode) {
                                if (RewriteUse(use, objnode, rightAss, FALSE)) {
                                    rewrittenUseCount++;
                                } else {
                                    objnode = NULL;
                                }
                            }

                            if (objnode && !sideEffectFlag) {
                                sideEffectFlag = HasSideEffectsBeforeUse(
                                    fnode, fnode, use->node, use->linear, TRUE
                                );
                            }

                            if (objnode &&
                                !killedFlag &&
                                (IRO_HasSideEffect(ifnode->last->u.label.cond) ||
                                 IRO_HasSideEffect(leftAss->u.diadic.right) ||
                                 IRO_HasSideEffect(rightAss->u.diadic.right))) {
                                killedFlag = TRUE;
                            }
                        } else if (Bv_IsBitSet(def1->index, use->reachingDefs) ||
                                   Bv_IsBitSet(def2->index, use->reachingDefs)) {
                            objnode = NULL;
                        }
                    }
                }
            }
        }
    }

    if (!objnode) {
        return;
    }

    compareType = ifnode->last->type == IROLinearIf;
    cond = ifnode->last->u.label.cond;
    leftVal = leftAss->u.diadic.right;
    rightVal = rightAss->u.diadic.right;
    type = rightAss->rtype;
    memcpy(&copy1, leftVal, sizeof(IROLinear));
    memcpy(&copy2, rightAss, sizeof(IROLinear));

    ifnode->last->type = IROLinearNop;
    ifnode->last->expr = NULL;
    target->first->type = IROLinearNop;
    target->first->expr = NULL;
    if (right->last->type == IROLinearGoto) {
        right->last->type = IROLinearNop;
        right->last->expr = NULL;
    }

    if (objnode) {
        IRO_NopOut(leftAss->u.diadic.left);
        leftAss->type = IROLinearNop;
        leftAss->expr = NULL;
    }

    memcpy(rightAss, &copy1, sizeof(IROLinear));
    rightAss->type = IROLinearOp3Arg;
    rightAss->nodetype = ECOND;
    rightAss->index = copy2.index;
    rightAss->u.args3.a = cond;
    rightAss->rtype = type;
    rightAss->next = copy2.next;
    if (compareType) {
        rightAss->u.args3.b = rightVal;
        rightAss->u.args3.c = leftVal;
    } else {
        rightAss->u.args3.b = leftVal;
        rightAss->u.args3.c = rightVal;
    }
    rightAss->flags |= IROLF_Reffed;

    if (rewrittenUseCount == 1 &&
        !sideEffectFlag &&
        !killedFlag &&
        !objnode->data.objref->varptr->noregister) {
        for (use = objnode->data.objref->varptr->uses; use; use = use->varnext) {
            if (use->reachingDefCount &&
                Bv_IsBitSet(def1->index, use->reachingDefs) &&
                Bv_IsBitSet(def2->index, use->reachingDefs)) {
                RewriteUse(use, objnode, rightAss, TRUE);
                if ((killedFlag =
                         (use->linear->flags & IROLF_InCallArg) && IRO_HasSideEffect(cond))) {
                    tmp = IRO_NewLinear(IROLinearOperand);
                    memcpy(tmp, copy2.u.diadic.left->u.monadic, sizeof(IROLinear));
                    tmp->index = IRO_NumLinear++;
                    tmp->rtype = cond->rtype;
                    if (useCount == 1) {
                        tmp->u.node->data.objref->type = cond->rtype;
                        tmp->u.node->rtype = CDecl_NewPointerType(cond->rtype);
                    } else {
                        obj = create_temp_object(tmp->rtype);
                        IRO_FindVar(obj, TRUE, TRUE);
                        tmp->u.node = create_objectrefnode(obj);
                    }
                    IRO_PasteAfter(tmp, tmp, cond);

                    tmp = IRO_NewLinear(IROLinearOp1Arg);
                    memcpy(tmp, copy2.u.diadic.left, sizeof(IROLinear));
                    tmp->index = IRO_NumLinear++;
                    tmp->u.monadic = cond->next;
                    tmp->rtype = cond->rtype;
                    IRO_PasteAfter(tmp, tmp, cond->next);

                    tmp = IRO_NewLinear(IROLinearOp2Arg);
                    memcpy(tmp, &copy2, sizeof(IROLinear));
                    tmp->index = IRO_NumLinear++;
                    tmp->u.diadic.right = cond;
                    tmp->u.diadic.left = cond->next->next;
                    tmp->rtype = cond->rtype;
                    IRO_PasteAfter(tmp, tmp, cond->next->next);

                    if (useCount != 1) {
                        tmp = copy2.u.diadic.left->u.monadic;
                        tmp->u.node = create_objectrefnode(obj);
                    }

                    rightAss->u.args3.a = copy2.u.diadic.left;
                    rightAss->u.args3.a->rtype = cond->rtype;

                    if (useCount == 1) {
                        def2->linear = cond->next->next;
                        if (Bv_IsBitSet(def2->index, right->out)) {
                            Bv_ClearBit(def2->index, right->out);
                            Bv_SetBit(def2->index, ifnode->out);
                        }
                        if (Bv_IsBitSet(def2->index, right->gen)) {
                            Bv_ClearBit(def2->index, right->gen);
                            Bv_SetBit(def2->index, ifnode->gen);
                        }
                        if (Bv_IsBitSet(def2->index, right->kill)) {
                            Bv_ClearBit(def2->index, right->kill);
                            Bv_SetBit(def2->index, ifnode->kill);
                        }
                    }
                }
            }
        }

        if (!killedFlag) {
            if (useCount == 1 && objnode->data.objref->u.var.info) {
                objnode->data.objref->u.var.info->usage = 0;
                objnode->data.objref->u.var.info->used = FALSE;
            }
            def1->useCount = 0;
            IRO_NopOut(copy2.u.diadic.left);
            def2->useCount = 0;
        }
    } else {
        tmp = IRO_NewLinear(IROLinearOp2Arg);
        memcpy(tmp, &copy2, sizeof(IROLinear));
        tmp->index = IRO_NumLinear++;
        tmp->u.diadic.right = rightAss;
        IRO_PasteAfter(tmp, tmp, fnode->first);
        def1->useCount = 0;
        def2->linear = fnode->first;
        if (Bv_IsBitSet(def2->index, right->out)) {
            Bv_ClearBit(def2->index, right->out);
            Bv_SetBit(def2->index, fnode->out);
        }
        if (Bv_IsBitSet(def2->index, right->gen)) {
            Bv_ClearBit(def2->index, right->gen);
            Bv_SetBit(def2->index, fnode->gen);
        }
        if (Bv_IsBitSet(def2->index, right->kill)) {
            Bv_ClearBit(def2->index, right->kill);
            Bv_SetBit(def2->index, fnode->kill);
        }
    }
}

// NOTE: invented names: condition, thenValue, elseValue, notNode
static void RebuildPossibleReturnCondExpression(IRONode* fnode) {
    IRONode* succ2;
    IROLinear* condition;
    IROLinear* thenValue;
    IROLinear* tmp1;
    IRONode* succ1;
    IROLinear* elseValue;
    IRONode* target;
    Boolean isIf;
    Type* type;
    IROList list;
    IROLinear* last;
    IROLinear* tmp2;
    IROLinear* notNode;

    if (!fnode ||
        fnode->numsucc != 2 ||
        !(last = fnode->last) ||
        (last->type != IROLinearIf && last->type != IROLinearIfNot)) {
        return;
    }

    succ1 = IRO_NodeTable[fnode->succ[0]];
    succ2 = IRO_NodeTable[fnode->succ[1]];
    if (!succ1 ||
        !succ2 ||
        succ1->numsucc != 0 ||
        succ1->numpred != 1 ||
        succ2->numsucc != 0 ||
        succ2->numpred != 1) {
        return;
    }

    if (succ1->first &&
        succ1->first->type == IROLinearLabel &&
        succ1->first->u.label.label == fnode->last->u.label.label) {
        target = succ1;
    } else if (succ2->first &&
               succ2->first->type == IROLinearLabel &&
               succ2->first->u.label.label == fnode->last->u.label.label) {
        target = succ2;
    } else {
        return;
    }

    if (target->numpred != 1) {
        return;
    }

    if (!CheckThenOrElseBranch(fnode->nextnode, fnode->nextnode, NULL, TRUE) ||
        !CheckThenOrElseBranch(target, target, NULL, TRUE)) {
        return;
    }

    if (!fnode->nextnode->last ||
        fnode->nextnode->last->type != IROLinearReturn ||
        !fnode->nextnode->last->u.monadic ||
        !target->last ||
        target->last->type != IROLinearReturn ||
        !target->last->u.monadic ||
        !IRO_TypesEqual(fnode->nextnode->last->u.monadic->rtype, target->last->u.monadic->rtype)) {
        return;
    }

    isIf = fnode->last->type == IROLinearIf;
    condition = fnode->last->u.label.cond;
    thenValue = fnode->nextnode->last->u.monadic;
    elseValue = target->last->u.monadic;
    type = thenValue->rtype;

    fnode->last->type = IROLinearNop;
    fnode->last->expr = NULL;
    target->last->type = IROLinearNop;
    target->last->expr = NULL;
    target->first->type = IROLinearNop;
    target->first->expr = NULL;

    if (IRO_IsIntConstant(thenValue) &&
        IRO_IsIntConstant(elseValue) &&
        ((IRO_IsConstantOne(thenValue) && IRO_IsConstantZero(elseValue)) ||
         (IRO_IsConstantZero(thenValue) && IRO_IsConstantOne(elseValue)))) {
        if (!(condition->type == IROLinearOp1Arg &&
              condition->nodetype == ELOGNOT &&
              condition->rtype == type)) {
            tmp1 = IRO_NewLinear(IROLinearOp1Arg);
            memcpy(tmp1, thenValue, sizeof(IROLinear));
            tmp1->type = IROLinearOp1Arg;
            tmp1->nodetype = ELOGNOT;
            tmp1->index = IRO_NumLinear++;
            tmp1->u.monadic = condition;

            tmp1->next = tmp2 = IRO_NewLinear(IROLinearOp1Arg);
            memcpy(tmp2, thenValue, sizeof(IROLinear));
            tmp2->type = IROLinearOp1Arg;
            tmp2->nodetype = ELOGNOT;
            tmp2->index = IRO_NumLinear++;
            tmp2->u.monadic = tmp1;

            IRO_PasteAfter(tmp1, tmp2, condition);
            condition = tmp2;
        }

        if ((IRO_IsConstantZero(thenValue) && !isIf) || (IRO_IsConstantOne(thenValue) && isIf)) {
            if (condition->type == IROLinearOp1Arg &&
                condition->nodetype == ELOGNOT &&
                condition->rtype == condition->u.monadic->rtype) {
                notNode = condition;
                condition = condition->u.monadic;
                notNode->type = IROLinearNop;
                notNode->expr = NULL;
            } else {
                notNode = IRO_NewLinear(IROLinearOp1Arg);
                memcpy(notNode, thenValue, sizeof(IROLinear));
                notNode->type = IROLinearOp1Arg;
                notNode->nodetype = ELOGNOT;
                notNode->index = IRO_NumLinear++;
                notNode->u.monadic = condition;
                IRO_PasteAfter(notNode, notNode, condition);
                condition = notNode;
            }
        }

        thenValue->type = IROLinearNop;
        thenValue->expr = NULL;
        elseValue->type = IROLinearNop;
        elseValue->expr = NULL;
        fnode->nextnode->last->u.monadic = condition;
    } else {
        IRO_InitList(&list);
        tmp1 = IRO_DuplicateExpr(elseValue, &list);
        IRO_NopOut(elseValue);
        IRO_Paste(list.head, list.tail, fnode->nextnode->last);

        tmp2 = IRO_NewLinear(IROLinearOp3Arg);
        memcpy(tmp2, thenValue, sizeof(IROLinear));
        tmp2->type = IROLinearOp3Arg;
        tmp2->nodetype = ECOND;
        tmp2->index = IRO_NumLinear++;
        tmp2->u.args3.a = condition;
        tmp2->rtype = type;
        IRO_Paste(tmp2, tmp2, fnode->nextnode->last);

        if (isIf) {
            tmp2->u.args3.b = tmp1;
            tmp2->u.args3.c = thenValue;
        } else {
            tmp2->u.args3.b = thenValue;
            tmp2->u.args3.c = tmp1;
        }

        fnode->nextnode->last->u.monadic = tmp2;
    }
}

static void RebuildCondExpressions(void) {
    IRONode* fnode;
    IROLinear* nd;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (fnode->numpred == 2) {
            if ((nd = fnode->first) && nd->type == IROLinearLabel) {
                RebuildPossibleCondExpression(fnode);
            }
        } else if (fnode->numsucc == 2) {
            if ((nd = fnode->last) && (nd->type == IROLinearIf || nd->type == IROLinearIfNot)) {
                RebuildPossibleReturnCondExpression(fnode);
            }
        }
    }

    IRO_CheckForUserBreak();
}

static Boolean IsLogicalExpressionAssign(IROLinear* nd) {
    IROLinear* right;
    IROLinear* left;

    return (
        nd->type == IROLinearOp2Arg &&
        nd->nodetype == EASS &&
        (left = nd->u.diadic.left) &&
        (right = nd->u.diadic.right) &&
        right->type == IROLinearOperand &&
        right->u.node &&
        right->u.node->type == EINTCONST &&
        (CInt64_Equal(right->u.node->data.intval, cint64_one) ||
         CInt64_Equal(right->u.node->data.intval, cint64_zero)) &&
        left->type == IROLinearOp1Arg &&
        left->nodetype == EINDIRECT &&
        left->u.monadic &&
        IS_TYPE_POINTER_ONLY(left->u.monadic->rtype) &&
        left->u.monadic->type == IROLinearOperand &&
        left->u.monadic->u.node &&
        left->u.monadic->u.node->type == EOBJREF &&
        left->u.monadic->u.node->data.objref &&
        left->u.monadic->u.node->data.objref->datatype == DLOCAL
    );
}

static Boolean IsPossibleLogicalExpressionStart(IRONode* fnode, IROLinear* nd) {
    IROLinear* last;

    return fnode->numsucc == 2 &&
        (last = fnode->last) &&
        (last->type == IROLinearIf || last->type == IROLinearIfNot) &&
        IsLogicalExpressionAssign(nd);
}

// NOTE: invented names: fnode1, fnode2, fnode3, start, stop, clearMarks
static Boolean CheckForTopLevelExpressions(
    IRONode* fnode1,
    IRONode* fnode2,
    IRONode* fnode3,
    IROLinear* start,
    IROLinear* stop,
    Boolean clearMarks
) {
    IRONode* fnode;
    IROLinear* nd;
    UInt16 i;

    if (fnode1 != fnode3) {
        if (!Bv_IsBitSet(fnode2->index, fnode1->dom)) {
            return FALSE;
        }

        if (clearMarks) {
            for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
                fnode->marked = FALSE;
            }
        } else if (fnode1->marked) {
            return TRUE;
        }
        fnode1->marked = TRUE;

        for (i = 0; i < fnode1->numsucc; i++) {
            if (!CheckForTopLevelExpressions(
                    IRO_NodeTable[fnode1->succ[i]],
                    fnode2,
                    fnode3,
                    IRO_NodeTable[fnode1->succ[i]]->first,
                    stop,
                    FALSE
                )) {
                return FALSE;
            }
        }
    }

    for (nd = start; nd && nd != fnode1->last->next; nd = nd->next) {
        if (nd == stop) {
            return TRUE;
        }

        if (nd->type != IROLinearNop && nd->type != IROLinearLabel && !(nd->flags & IROLF_Reffed)) {
            return FALSE;
        }
    }

    return TRUE;
}

static void AddNodeAndSuccessorsRecursively(IRONodes* nodes, IRONode* fnode1, IRONode* fnode2) {
    UInt16 i;

    if (fnode1 && !fnode1->marked) {
        fnode1->marked = TRUE;
        IRO_FNodeWorkStack_Add(nodes, fnode1->index);
        if (fnode1 != fnode2) {
            for (i = 0; i < fnode1->numsucc; i++) {
                AddNodeAndSuccessorsRecursively(nodes, IRO_NodeTable[fnode1->succ[i]], fnode2);
            }
        }
    }
}

// TODO: `fnode` is a copy of the parameter that nothing here needs; with it the function MWCC
// compiles next, AddNodeAndSuccessorsRecursively, gets the original's code. No evidence otherwise.
static Boolean RebuildPossibleLogicalExpression(IRONode* start, IROLinear* nd) {
    IRONode* fnode = start;
    IRONode* prevnode;
    IROLinear* newlin;
    IROUse* use;
    IROLinear* prevAssign; // NOTE: invented name
    IROLinear* left;
    IROLinear* right;
    IRONode* test1node;
    IRONode* test2node;
    UInt8 compareType;
    ENode* objnode;
    IRODef* def1;
    IRODef* def2;
    UInt32 useCount;
    UInt32 rewrittenUseCount;
    Boolean sideEffectFlag;
    CLabel* somelab;
    IRONode* labelnode;
    CInt64 compareVal;
    Boolean killedFlag;
    BitVector* bv1;
    BitVector* bv2;
    IROLinear* father;
    IROLinear* iter;
    IRODef* def;
    UInt32 i;

    objnode = nd->u.diadic.left->u.monadic->u.node;
    if (!objnode->data.objref->varptr) {
        return FALSE;
    }

    if (CInt64_Equal(nd->u.diadic.right->u.node->data.intval, cint64_zero)) {
        compareType = IROLinearIfNot;
        compareVal = cint64_one;
    } else {
        compareType = IROLinearIf;
        compareVal = cint64_zero;
    }

    labelnode = NULL;
    def1 = NULL;
    for (def = objnode->data.objref->varptr->defs; def && !def1; def = def->varnext) {
        if (def->useCount && def->linear == nd) {
            def1 = def;
        }
    }

    if (def1) {
        for (use = objnode->data.objref->varptr->uses; use; use = use->varnext) {
            if (use->reachingDefCount == 2 && Bv_IsBitSet(def1->index, use->reachingDefs)) {
                if (!labelnode) {
                    labelnode = use->node;
                } else if (Bv_IsBitSet(use->node->index, labelnode->dom)) {
                    labelnode = use->node;
                }
            }
        }

        if (labelnode) {
            for (use = objnode->data.objref->varptr->uses; use; use = use->varnext) {
                if (use->node != labelnode &&
                    use->reachingDefCount == 2 &&
                    Bv_IsBitSet(def1->index, use->reachingDefs) &&
                    !Bv_IsBitSet(labelnode->index, use->node->dom)) {
                    labelnode = NULL;
                    break;
                }
            }
        }
    }

    if (!labelnode ||
        !labelnode->first ||
        labelnode->first->type != IROLinearLabel ||
        !(somelab = labelnode->first->u.label.label) ||
        labelnode->numpred != 3) {
        return FALSE;
    }

    test1node = IRO_NodeTable[labelnode->pred[0]];
    if (Bv_IsBitSet(IRO_NodeTable[labelnode->pred[1]]->index, test1node->dom)) {
        test1node = IRO_NodeTable[labelnode->pred[1]];
    } else if (!Bv_IsBitSet(test1node->index, IRO_NodeTable[labelnode->pred[1]]->dom)) {
        return FALSE;
    }

    if (Bv_IsBitSet(IRO_NodeTable[labelnode->pred[2]]->index, test1node->dom)) {
        test1node = IRO_NodeTable[labelnode->pred[2]];
    } else if (!Bv_IsBitSet(test1node->index, IRO_NodeTable[labelnode->pred[2]]->dom)) {
        return FALSE;
    }

    if (!test1node ||
        !(left = test1node->last) ||
        left->type != compareType ||
        left->u.label.label != somelab) {
        return FALSE;
    }

    if (!FindFlowgraphNodeThatStartsWithLabel(left->u.label.label, &labelnode, &prevnode) ||
        !prevnode ||
        prevnode->numpred != 1 ||
        !(test2node = IRO_NodeTable[prevnode->pred[0]]) ||
        !(right = test2node->last) ||
        right->type != compareType ||
        right->u.label.label != left->u.label.label ||
        !(prevAssign = FindLastDiadicTopLevelAssignmentInFlowgraphNode(prevnode)) ||
        !IsLogicalExpressionAssign(prevAssign) ||
        prevAssign->u.diadic.left->u.monadic->u.node->data.objref != objnode->data.objref ||
        !CInt64_Equal(compareVal, prevAssign->u.diadic.right->u.node->data.intval) ||
        prevAssign->rtype != nd->rtype) {
        return FALSE;
    }

    if (!CheckForTopLevelExpressions(fnode, fnode, test1node, nd->next, left, TRUE) ||
        !CheckForTopLevelExpressions(
            test1node->nextnode,
            test1node->nextnode,
            test2node,
            test1node->nextnode->first,
            right,
            TRUE
        ) ||
        !CheckForTopLevelExpressions(
            test2node->nextnode, test2node->nextnode, prevnode, right->next, prevAssign, TRUE
        ) ||
        !CheckForTopLevelExpressions(
            prevnode, prevnode, prevnode, prevAssign->next, labelnode->first, TRUE
        )) {
        return FALSE;
    }

    useCount = rewrittenUseCount = 0;
    sideEffectFlag = killedFlag = FALSE;
    if (!objnode->data.objref->varptr ||
        !(objnode->data.objref->varptr->uses) ||
        !(objnode->data.objref->varptr->defs)) {
        objnode = NULL;
    } else {
        def1 = def2 = NULL;
        for (def = objnode->data.objref->varptr->defs; def && (!def1 || !def2);
             def = def->varnext) {
            if (def->useCount && def->linear == nd) {
                def1 = def;
            } else if (def->useCount && def->linear == prevAssign) {
                def2 = def;
            }
        }

        if (!def1 || !def2) {
            objnode = NULL;
        } else {
            for (use = objnode->data.objref->varptr->uses; objnode && use; use = use->varnext) {
                if (use->reachingDefCount) {
                    useCount++;
                    if (Bv_IsBitSet(def1->index, use->reachingDefs) &&
                        Bv_IsBitSet(def2->index, use->reachingDefs)) {
                        for (i = 0; objnode && i < use->reachingDefs->size; i++) {
                            if (i != (UInt32)def1->index &&
                                i != (UInt32)def2->index &&
                                Bv_IsBitSet(i, use->reachingDefs)) {
                                objnode = NULL;
                            }
                        }

                        if (objnode && !Bv_IsBitSet(labelnode->index, use->node->dom)) {
                            objnode = NULL;
                        }

                        if (objnode) {
                            if (RewriteUse(use, objnode, prevAssign, FALSE)) {
                                rewrittenUseCount++;
                            } else {
                                objnode = NULL;
                            }
                        }

                        if (objnode) {
                            if (!sideEffectFlag) {
                                sideEffectFlag = HasSideEffectsBeforeUse(
                                    labelnode, labelnode, use->node, use->linear, TRUE
                                );
                            }

                            if (!killedFlag &&
                                (IRO_HasSideEffect(left->u.label.cond) ||
                                 IRO_HasSideEffect(right->u.label.cond))) {
                                iter = use->linear;
                                if ((father = IRO_LocateFather(use->linear))) {
                                    Bv_AllocVector(&bv1, IRO_NumVars + 1);
                                    Bv_AllocVector(&bv2, IRO_NumVars + 1);
                                    Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);
                                    IRO_WalkTree(left->u.label.cond, Regen_GetExprKills);
                                    IRO_WalkTree(right->u.label.cond, Regen_GetExprKills);
                                    Bv_Or(IRO_VarKills, bv1);
                                    while (father && !killedFlag) {
                                        CheckUnorderedRegionsForSideEffectsAndUses(
                                            father, iter, bv1, bv2, &killedFlag
                                        );
                                        iter = father;
                                        father = IRO_LocateFather(father);
                                    }
                                }

                                if (!killedFlag) {
                                    killedFlag = UsesAKilledVarBeforeUse(
                                        labelnode, labelnode, use->node, use->linear, bv1, TRUE
                                    );
                                }
                            }
                        }
                    } else if (Bv_IsBitSet(def1->index, use->reachingDefs) ||
                               Bv_IsBitSet(def2->index, use->reachingDefs)) {
                        objnode = NULL;
                    }
                }
            }
        }
    }

    if (!objnode) {
        return FALSE;
    }

    left->type = right->type = IROLinearNop;
    left->expr = right->expr = NULL;

    IRO_NopOut(nd);

    newlin = IRO_NewLinear(IROLinearOp2Arg);
    memcpy(newlin, prevAssign, sizeof(IROLinear));
    newlin->nodetype = (compareType == IROLinearIf) ? ELOR : ELAND;
    newlin->index = IRO_NumLinear++;
    newlin->u.diadic.left = left->u.label.cond;
    newlin->u.diadic.right = right->u.label.cond;
    newlin->flags |= IROLF_Reffed;
    IRO_Paste(newlin, newlin, prevAssign);

    if (rewrittenUseCount == 1 &&
        !sideEffectFlag &&
        !killedFlag &&
        !objnode->data.objref->varptr->noregister) {
        for (use = objnode->data.objref->varptr->uses; use; use = use->varnext) {
            if (use->reachingDefCount &&
                Bv_IsBitSet(def1->index, use->reachingDefs) &&
                Bv_IsBitSet(def2->index, use->reachingDefs)) {
                RewriteUse(use, objnode, newlin, TRUE);
            }
        }

        if (useCount == 1 && objnode->data.objref->u.var.info) {
            objnode->data.objref->u.var.info->usage = 0;
            objnode->data.objref->u.var.info->used = FALSE;
        }
        IRO_NopOut(prevAssign);
        def1->useCount = 0;
        def2->useCount = 0;
    } else {
        IRO_NopOut(prevAssign->u.diadic.right);
        prevAssign->u.diadic.right = newlin;
        def1->useCount = 0;
    }

    if (test2node->numpred == 1 &&
        IRO_NodeTable[test2node->pred[0]] == test1node &&
        IRO_MergeFlowGraphNodes(test1node, test2node)) {
        test2node = test1node;
    }
    if (prevnode->numpred == 1 &&
        IRO_NodeTable[prevnode->pred[0]] == test2node &&
        IRO_MergeFlowGraphNodes(test2node, prevnode)) {
        prevnode = test2node;
    }
    if (labelnode->numpred == 1 && test2node == test1node && prevnode == test2node) {
        IRO_MergeFlowGraphNodes(prevnode, labelnode);
    }
    return TRUE;
}

static void RebuildLogicalExpressions(void) {
    IRONode* fnode;
    IROLinear* nd;
    Boolean changed; // NOTE: invented name

    do {
        changed = FALSE;
        for (fnode = IRO_FirstNode; fnode && fnode->last; fnode = fnode->nextnode) {
            for (nd = fnode->first; nd && nd != fnode->last->next; nd = nd->next) {
                if (IsPossibleLogicalExpressionStart(fnode, nd) &&
                    RebuildPossibleLogicalExpression(fnode, nd)) {
                    changed = TRUE;
                }
            }
        }
    } while (changed);

    IRO_CheckForUserBreak();
}

static Boolean IsPossibleCondAssStart(IRONode* fnode) {
    IROLinear* last;

    return (fnode->numsucc == 2) &&
        (last = fnode->last) &&
        (last->type == IROLinearIf || last->type == IROLinearIfNot);
}

static Type* logicalType(void) {
    if (copts.cplusplus && copts.booltruefalse) {
        return TYPE(&stbool);
    } else {
        return TYPE(&stsignedint);
    }
}

static Boolean GeneratePossibleCondAss(IRONode* fnode) {
    IROLinear* ass1;
    IROLinear* dest;
    IRONode* node1;
    IROLinear* ass;
    IRONode* node2;
    Object* obj;
    IROLinear* scan;
    IROLinear* right2;
    IROLinear* condnd;
    Boolean useTemp;
    IROLinear* notnd;
    IRONode* joinnode;
    IROLinear* ass2;
    IROLinear* left2;
    IROLinear* cond;
    Boolean hasElse;
    BitVector* saveVarKills;
    IROList list;
    Boolean isIf;
    IROLinear* test;
    IROLinear* last;
    IRONode* succ;
    IROLinear* node2last;
    UInt16 succ0;
    IRONode* succnode;
    Boolean sameDest;
    IROLinear* value1;
    IROLinear* left;

    if (!fnode ||
        fnode->numsucc != 2 ||
        !(last = fnode->last) ||
        (last->type != IROLinearIf && last->type != IROLinearIfNot)) {
        return FALSE;
    }

    node1 = node2 = IRO_NodeTable[fnode->succ[0]];
    if (node1 &&
        node1->first &&
        node1->first->type == IROLinearLabel &&
        node1->first->u.label.label == fnode->last->u.label.label) {
        node2 = IRO_NodeTable[fnode->succ[1]];
    } else {
        succ = node1 = IRO_NodeTable[fnode->succ[1]];
        if (!succ ||
            !succ->first ||
            succ->first->type != IROLinearLabel ||
            succ->first->u.label.label != fnode->last->u.label.label) {
            return FALSE;
        }
    }

    if (!node2 ||
        node2->numpred != 1 ||
        node2->numsucc != 1 ||
        (node2last = node2->last)->next != node1->first) {
        return FALSE;
    }

    succ0 = node2->succ[0];
    if ((succnode = IRO_NodeTable[succ0]) == node1 && Bv_IsBitSet(fnode->index, node1->dom)) {
        hasElse = FALSE;
    } else if (node1->numpred == 1 &&
               node1->numsucc == 1 &&
               succ0 == node1->succ[0] &&
               (joinnode = IRO_NodeTable[succ0]) &&
               Bv_IsBitSet(fnode->index, joinnode->dom) &&
               node1->last->next == joinnode->first) {
        hasElse = TRUE;
    } else {
        return FALSE;
    }

    ass1 = NULL;
    if (node2last) {
        for (scan = node2->first; scan && scan != node2->last->next; scan = scan->next) {
            if (IRO_IsAssignment(scan) && !(scan->flags & IROLF_Reffed) && !ass1) {
                ass1 = scan;
            } else if (scan->type != IROLinearNop &&
                       scan->type != IROLinearLabel &&
                       scan->type != IROLinearGoto &&
                       !(scan->flags & IROLF_Reffed)) {
                return FALSE;
            }
        }
    }

    if (!ass1 ||
        (ass1->nodetype != EASS && !IRO_TransformSelfAssignmentToAssignment(ass1)) ||
        !ass1->u.diadic.left ||
        ass1->u.diadic.left->type != IROLinearOp1Arg ||
        ass1->u.diadic.left->nodetype != EINDIRECT ||
        !ass1->u.diadic.left->u.monadic ||
        ass1->u.diadic.left->u.monadic->type != IROLinearOperand ||
        !ass1->u.diadic.left->u.monadic->u.node ||
        !ass1->u.diadic.left->u.monadic->u.node->rtype ||
        !IS_TYPE_POINTER_ONLY(ass1->u.diadic.left->u.monadic->u.node->rtype) ||
        !ass1->u.diadic.left->u.monadic->u.node->data.objref) {
        return FALSE;
    }

    if (hasElse) {
        ass2 = NULL;
        if (node1->last) {
            for (scan = node1->first; scan && scan != node1->last->next; scan = scan->next) {
                if (IRO_IsAssignment(scan) && !(scan->flags & IROLF_Reffed) && !ass2) {
                    ass2 = scan;
                } else if (scan->type != IROLinearNop &&
                           scan->type != IROLinearLabel &&
                           scan->type != IROLinearGoto &&
                           !(scan->flags & IROLF_Reffed)) {
                    return FALSE;
                }
            }
        }

        if (!ass2 ||
            (ass2->nodetype != EASS && !IRO_TransformSelfAssignmentToAssignment(ass2)) ||
            !(left = ass2->u.diadic.left) ||
            left->type != IROLinearOp1Arg ||
            left->nodetype != EINDIRECT ||
            !left->u.monadic ||
            left->u.monadic->type != IROLinearOperand ||
            !left->u.monadic->u.node ||
            !left->u.monadic->u.node->rtype ||
            !IS_TYPE_POINTER_ONLY(left->u.monadic->u.node->rtype) ||
            !left->u.monadic->u.node->data.objref) {
            return FALSE;
        }

        sameDest = IRO_ExprsSame(ass1->u.diadic.left, ass2->u.diadic.left);
    }

    isIf = fnode->last->type == IROLinearIf;
    condnd = fnode->last->u.label.cond;
    useTemp = hasElse && copts.commonsubs && IRO_IsSubableExpression(condnd);

    if (useTemp) {
        IRO_FindDepends(condnd);
        useTemp = !IRO_NotSubable;
    }

    if (hasElse) {
        if (sameDest) {
            useTemp = FALSE;
        } else if (IRO_HasSideEffect(condnd)) {
            useTemp = TRUE;
        } else {
            Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);
            IRO_WalkTree(condnd, Regen_GetExprUses);
            saveVarKills = IRO_VarKills;
            Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);
            IRO_WalkTree(ass1, Regen_GetExprKills);
            if (Bv_BitsInCommon(IRO_VarKills, saveVarKills)) {
                useTemp = TRUE;
            }
        }

        if (useTemp && !copts.commonsubs) {
            return FALSE;
        }
    }

    if (useTemp) {
        IRO_InitList(&list);
        obj = create_temp_object(condnd->rtype);
        IRO_FindVar(obj, TRUE, TRUE);
        ass = IRO_NewLinear(IROLinearOp2Arg);
        ass->u.diadic.left = IRO_TempReference(obj, &list);
        ass->rtype = condnd->rtype;
        ass->nodetype = EASS;
        ass->index = ++IRO_NumLinear;
        ass->u.diadic.left->flags |= IROLF_Ind | IROLF_Assigned;
        ass->u.diadic.left->u.monadic->flags |= IROLF_Ind | IROLF_Assigned;
        ass->u.diadic.right = condnd;
        IRO_AddToList(ass, &list);
        IRO_PasteAfter(list.head, list.tail, condnd);
    }

    dest = ass1->u.diadic.left;
    fnode->last->type = IROLinearNop;
    value1 = ass1->u.diadic.right;
    fnode->last->expr = NULL;
    if (node2->last->type == IROLinearGoto) {
        node2->last->type = IROLinearNop;
        node2->last->expr = NULL;
    }
    if (hasElse || node1->numpred == 2) {
        node1->first->type = IROLinearNop;
        node1->first->expr = NULL;
    }

    if (hasElse && sameDest) {
        if (node1->last->type == IROLinearGoto) {
            node1->last->type = IROLinearNop;
            node1->last->expr = NULL;
        }
        if (joinnode->numpred == 2) {
            joinnode->first->type = IROLinearNop;
            joinnode->first->expr = NULL;
        }

        cond = IRO_NewLinear(IROLinearOp3Arg);
        memcpy(cond, ass1, sizeof(IROLinear));
        cond->type = IROLinearOp3Arg;
        cond->nodetype = ECOND;
        cond->index = IRO_NumLinear++;
        cond->rtype = dest->rtype;
        cond->flags |= IROLF_Reffed;
        if (useTemp) {
            IRO_InitList(&list);
            condnd = IRO_TempReference(obj, &list);
            IRO_Paste(list.head, list.tail, ass2);
        }
        cond->u.args3.a = condnd;

        if (isIf) {
            cond->u.args3.b = ass2->u.diadic.right;
            cond->u.args3.c = value1;
        } else {
            cond->u.args3.b = value1;
            cond->u.args3.c = ass2->u.diadic.right;
        }

        ass1->type = IROLinearNop;
        ass1->expr = NULL;
        IRO_NopOut(ass1->u.diadic.left);
        IRO_Paste(cond, cond, ass2);
        ass2->u.diadic.right = cond;
    } else {
        ass1->type = IROLinearOp3Arg;
        ass1->nodetype = ECONDASS;
        if (useTemp) {
            IRO_InitList(&list);
            test = IRO_TempReference(obj, &list);
            IRO_Paste(list.head, list.tail, ass1);
        } else {
            test = condnd;
        }
        cond = test;
        if (isIf) {
            notnd = IRO_NewLinear(IROLinearOp1Arg);
            memcpy(notnd, test, sizeof(IROLinear));
            notnd->type = IROLinearOp1Arg;
            notnd->nodetype = ELOGNOT;
            notnd->index = IRO_NumLinear++;
            notnd->rtype = logicalType();
            notnd->u.monadic = test;
            notnd->flags |= IROLF_Reffed;
            IRO_Paste(notnd, notnd, ass1);
            test = notnd;
        }
        ass1->u.args3.a = test;
        ass1->u.args3.b = dest;
        ass1->u.args3.c = value1;
        if (hasElse) {
            left2 = ass2->u.diadic.left;
            right2 = ass2->u.diadic.right;
            if (node1->last->type == IROLinearGoto) {
                node1->last->type = IROLinearNop;
                node1->last->expr = NULL;
            }
            if (joinnode->numpred == 2) {
                joinnode->first->type = IROLinearNop;
                joinnode->first->expr = NULL;
            }
            ass2->type = IROLinearOp3Arg;
            ass2->nodetype = ECONDASS;
            if (useTemp) {
                IRO_InitList(&list);
                cond = IRO_TempReference(obj, &list);
                IRO_Paste(list.head, list.tail, ass2);
            } else {
                IRO_InitList(&list);
                cond = IRO_DuplicateExpr(cond, &list);
                IRO_Paste(list.head, list.tail, ass2);
            }
            if (!isIf) {
                test = IRO_NewLinear(IROLinearOp1Arg);
                memcpy(test, cond, sizeof(IROLinear));
                test->type = IROLinearOp1Arg;
                test->nodetype = ELOGNOT;
                test->index = IRO_NumLinear++;
                test->rtype = logicalType();
                test->u.monadic = cond;
                test->flags |= IROLF_Reffed;
                IRO_Paste(test, test, ass2);
                cond = test;
            }
            ass2->u.args3.a = cond;
            ass2->u.args3.b = left2;
            ass2->u.args3.c = right2;
        }
    }

    if (IRO_MergeFlowGraphNodes(fnode, node2)) {
        node2 = fnode;
    }
    if ((hasElse || node1->numpred == 1) && IRO_MergeFlowGraphNodes(node2, node1)) {
        node1 = node2;
    }
    if (hasElse && joinnode->numpred == 1) {
        IRO_MergeFlowGraphNodes(node1, joinnode);
    }
    return TRUE;
}

static void GenerateCondAssignments(void) {
    IRONode* fnode;

    fnode = IRO_FirstNode;
    while (fnode && fnode->last) {
        if (IsPossibleCondAssStart(fnode)) {
            if (!GeneratePossibleCondAss(fnode)) {
                fnode = fnode->nextnode;
            }
        } else {
            fnode = fnode->nextnode;
        }
    }

    IRO_CheckForUserBreak();
}

void IRO_RegenerateExpressions(void) {
    IRO_UpdateFlagsOnInts();
    IRO_ComputeSuccPred();
    IRO_ComputeDom();
    IRO_UseDef(FALSE, FALSE);

    if (copts.optimizationlevel > 0) {
        IRO_Dump("Rebuilding ELORs and ELANDs\n");
        RebuildLogicalExpressions();
        IRO_DumpAfterPhase("RebuildLogicalExpressions", FALSE);
    }

    if (copts.optimizationlevel > 0) {
        IRO_Dump("Rebuilding ECONDs\n");
        RebuildCondExpressions();
        IRO_DumpAfterPhase("RebuildCondExpressions", FALSE);
    }

    if (copts.optimizationlevel > 0) {
        IRO_Dump("Generating ECONDASSes\n");
        GenerateCondAssignments();
        IRO_DumpAfterPhase("GenerateCondAssignments", FALSE);
    }
}
