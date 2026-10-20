#include "compiler/IroEmptyLoop.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroLoop.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/BitVector.h"
#include "compiler/CError.h"
#include "compiler/CInt64.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"

static int EmptyLoop(IRONode* fnode);
static int CanRemoveRedundantLoop(IROLoop* loop);
static int CanRemoveRedundantLoop1(IROLoop* loop);
static int RedundantLoopCheck(IROLoop* loop);
static int CheckStepOverFlow1_EmptyLoop(IROLoop* loop, CInt64* iterCount, CInt64* finalValue);
static int CheckStepOverFlow2_EmptyLoop(IROLoop* loop, CInt64* iterCount, CInt64* finalValue);

void IRO_FindEmptyLoops(void) {
    IRONode* fnode;
    IRONode* pred;
    UInt16 i;
    UInt16 found;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        found = FALSE;
        for (i = 0; i < fnode->numpred; i++) {
            pred = IRO_NodeTable[fnode->pred[i]];
            if (Bv_IsBitSet(fnode->index, pred->dom)) {
                if (!found) {
                    Bv_AllocVector(&InLoop, IRO_NumNodes + 1);
                    Bv_Clear(InLoop);
                    Bv_SetBit(fnode->index, InLoop);
                }
                found = TRUE;
                Bv_SetBit(pred->index, InLoop);
                if (pred != fnode) {
                    AddPreds(pred);
                }
            }
        }

        if (found) {
            IRO_Dump("IRO_FindEmptyLoops:Found loop with header %d\n", fnode->index);
            IRO_DumpBits("Loop includes: ", InLoop);
            EmptyLoop(fnode);
            IRO_UpdateFlagsOnInts();
        }
    }

    IRO_CheckForUserBreak();
}

// NOTE: invented names: loopPredCount, loopSuccCount, isSelfLoop, outsidePredCount, hasOutsidePred
// TODO: the declaration order, the separate counter `k`, the block-local variables of the second
// branch and the counters incremented before the assignments beside them are register-allocation
// shapes found by search: plausible as the original's, not proven.
static int EmptyLoop(IRONode* fnode) {
    ENode* enode;
    int loopPredCount;
    int loopSuccCount;
    IROLoop* loop;
    IRONode* succ;
    IRONode* pred;
    int j;
    IRONode* bestpred;
    IROLinear* constnd;
    Boolean isSelfLoop;
    UInt16 i;
    IRONode* preheader;
    UInt32 outsidePredCount;
    IRONode* walk;
    int hasOutsidePred;
    IRONode* bestsucc;
    IRONode* tail;
    VarRecord* var;
    Type* type;
    IROLinear* save;
    UInt16 k;

    isSelfLoop = FALSE;
    loopSuccCount = 0;
    LoopNode = fnode;
    FindMustReach();

    for (var = IRO_FirstVar; var; var = var->next) {
        var->inductionState = TRUE;
    }

    ComputeLoopKills();
    ComputeLoopInvariance();
    ComputeLoopInduction();

    LoopNode = fnode;
    ConditionalHeaderAtBottom = FALSE;

    bestpred = NULL;
    hasOutsidePred = FALSE;
    for (i = 0; i < LoopNode->numpred; i++) {
        pred = IRO_NodeTable[LoopNode->pred[i]];
        if (!Bv_IsBitSet(pred->index, InLoop)) {
            hasOutsidePred = TRUE;
            if (pred->nextnode == fnode) {
                CError_ASSERT(173, !bestpred || pred == bestpred);
                bestpred = pred;
            }
        }
    }

    if (!hasOutsidePred) {
        IRO_Dump("No predecessor outside the loop\n");
        return FALSE;
    }

    bestsucc = NULL;
    for (k = 0; k < LoopNode->numsucc; k++) {
        succ = IRO_NodeTable[LoopNode->succ[k]];
        if (Bv_IsBitSet(succ->index, InLoop)) {
            loopSuccCount++;
            bestsucc = succ;
        }
    }

    if (LoopNode == bestsucc && loopSuccCount == 1) {
        isSelfLoop = TRUE;
    }

    // the original's bug: the first two tests together are always true (it evidently meant `&&`)
    if (LoopNode->last->type != IROLinearIf ||
        LoopNode->last->type != IROLinearIfNot ||
        isSelfLoop) {
        loopPredCount = 0;
        for (j = 0; j < LoopNode->numpred; j++) {
            IRONode* node = IRO_NodeTable[LoopNode->pred[j]];
            if (Bv_IsBitSet(node->index, InLoop)) {
                loopPredCount++;
                tail = node;
            }
        }

        preheader = NULL;
        outsidePredCount = 0;
        for (j = 0; j < LoopNode->numpred; j++) {
            if (!Bv_IsBitSet(IRO_NodeTable[LoopNode->pred[j]]->index, InLoop)) {
                outsidePredCount++;
                preheader = IRO_NodeTable[LoopNode->pred[j]];
            }
        }

        if (loopPredCount == 1 && outsidePredCount == 1) {
            if (tail->last->type == IROLinearIf) {
                if ((Bv_IsBitSet(LoopNode->nextnode->index, InLoop) &&
                     !Bv_IsBitSet(tail->nextnode->index, InLoop)) ||
                    isSelfLoop) {
                    IRO_Dump("Standard while loop layout\n");
                    loop = ExtractLoopInfo(tail);
                    if (isSelfLoop) {
                        loop->flags |= LP_RECURSIVE_LOOP;
                    }
                    FindAssignmenttoInductionVar(loop, preheader);
                    walk = preheader;
                    while (walk &&
                           !loop->init &&
                           walk->numpred == 1 &&
                           IRO_NodeTable[walk->pred[0]]->numsucc == 1) {
                        FindAssignmenttoInductionVar(loop, IRO_NodeTable[walk->pred[0]]);
                        walk = IRO_NodeTable[walk->pred[0]];
                    }

                    if (CanRemoveRedundantLoop(loop)) {
                        IRO_Dump(
                            "EmptyLoop: # of iterations =%ld, FinalStoreVal=%ld\n",
                            CInt64_GetULong(&loop->iterCount),
                            CInt64_GetULong(&loop->finalStoreVal)
                        );
                        IRO_NopOut(tail->last->u.label.cond);
                        tail->last->type = IROLinearNop;
                        type = loop->induction->nd->rtype;
                        constnd = IRO_NewLinear(IROLinearOperand);
                        constnd->index = ++IRO_NumLinear;
                        enode = IRO_NewENode(EINTCONST);
                        enode->rtype = type;
                        enode->data.intval = loop->finalStoreVal;
                        constnd->u.node = enode;
                        constnd->rtype = type;

                        if (loop->induction->nd->type == IROLinearOp1Arg) {
                            save = loop->induction->nd->u.monadic;
                            loop->induction->nd->type = IROLinearOp2Arg;
                            loop->induction->nd->nodetype = EASS;
                            loop->induction->nd->u.diadic.left = save;
                            loop->induction->nd->u.diadic.right = constnd;
                            IRO_Paste(constnd, constnd, loop->induction->nd);
                        } else if (loop->induction->nd->type == IROLinearOp2Arg) {
                            loop->induction->nd->nodetype = EASS;
                            IRO_NopOut(loop->induction->nd->u.diadic.right);
                            loop->induction->nd->u.diadic.right = constnd;
                            IRO_Paste(constnd, constnd, loop->induction->nd);
                        }
                    } else if (CanRemoveRedundantLoop1(loop)) {
                        Type* type2;
                        IROLinear* const2;
                        IROLinear* save2;

                        IRO_Dump("EmptyLoop: self recursive dowhile(--n ) loop\n");

                        tail->last->type = IROLinearNop;
                        type2 = loop->induction->nd->rtype;
                        const2 = IRO_NewLinear(IROLinearOperand);
                        const2->index = ++IRO_NumLinear;
                        enode = IRO_NewENode(EINTCONST);
                        enode->rtype = type2;
                        enode->data.intval = cint64_zero;
                        const2->u.node = enode;
                        const2->rtype = type2;

                        save2 = loop->induction->nd->u.monadic;
                        loop->induction->nd->type = IROLinearOp2Arg;
                        loop->induction->nd->nodetype = EASS;
                        loop->induction->nd->u.diadic.left = save2;
                        loop->induction->nd->u.diadic.right = const2;
                        IRO_Paste(const2, const2, loop->induction->nd);
                    }
                } else {
                    IRO_Dump("NonStandard while loop layout\n");
                }
            } else {
                IRO_Dump("NonStandard while loop layout\n");
            }
        } else {
            IRO_Dump("Cannot handle Do While Loop with multiple tails\n");
        }
    }

    return FALSE;
}

static int CanRemoveRedundantLoop(IROLoop* loop) {
    IROLinear* inner;

    if (loop->flags & LP_WHILE_LOOP) {
        IRO_Dump(
            "CanRemoveRedundantLoop:No because detection of dowhile(n--) loop not supported\n"
        );
        return FALSE;
    }

    if (loop->flags & LP_LOOP_HAS_ASM) {
        IRO_Dump("CanRemoveRedundantLoop:No due to LP_LOOP_HAS_ASM \n");
        return FALSE;
    }

    if (loop->flags & LP_IFEXPR_NON_CANONICAL) {
        IRO_Dump("CanRemoveRedundantLoop:No due to LP_IFEXPR_NON_CANONICAL \n");
        return FALSE;
    }

    if (loop->flags & LP_LOOP_HAS_CALLS) {
        IRO_Dump("CanRemoveRedundantLoop:No due to LP_LOOP_HAS_CALLS \n");
        return FALSE;
    }

    if (loop->flags & LP_LOOP_HAS_CNTRLFLOW) {
        IRO_Dump("CanRemoveRedundantLoop:No due to LP_LOOP_HAS_CNTRLFLOW \n");
        return FALSE;
    }

    if (loop->flags & LP_INDUCTION_NOT_FOUND) {
        IRO_Dump("CanRemoveRedundantLoop:No due to LP_INDUCTION_NOT_FOUND \n");
        return FALSE;
    }

    if (loop->flags & LP_HAS_MULTIPLE_INDUCTIONS) {
        IRO_Dump("CanRemoveRedundantLoop:No due to LP_HAS_MULTIPLE_INDUCTIONS \n");
        return FALSE;
    }

    if (loop->flags & LP_LOOP_HDR_HAS_SIDEEFFECTS) {
        IRO_Dump("CanRemoveRedundantLoop:No due to LP_LOOP_HDR_HAS_SIDEEFFECTS \n");
        return FALSE;
    }

    if (!(loop->flags & LP_HEADER_FOLLOWS_UPDATE)) {
        IRO_Dump("CanRemoveRedundantLoop:No because header does not follow induction update \n");
        return FALSE;
    }

    if (!(loop->flags & LP_WHILE_LOOP)) {
        inner = loop->cond->u.diadic.right;
        if (!IRO_IsIntConstant(inner) && !(inner->flags & IROLF_LoopInvariant)) {
            IRO_Dump("CanRemoveRedundantLoop:No because Loop Upper Bound is Variant in the loop\n");
            return FALSE;
        }

        if (!loop->init) {
            IRO_Dump(
                "CanRemoveRedundantLoop:No because there is no initialization of loop index in PreHeader\n"
            );
            return FALSE;
        }

        if (!IRO_IsVariable(loop->init->u.diadic.left)) {
            IRO_Dump(
                "CanRemoveRedundantLoop:No because initial value of induction stored thru pointer\n"
            );
            return FALSE;
        }

        if (!is_unsigned(loop->init->rtype)) {
            if (IRO_IsIntConstant(loop->init->u.diadic.right)) {
                if (!CInt64_GreaterEqual(
                        loop->init->u.diadic.right->u.node->data.intval, cint64_zero
                    )) {
                    IRO_Dump(
                        "CanRemoveRedundantLoop:No because initial value of induction is signed but < 0\n"
                    );
                    return FALSE;
                }
            } else {
                IRO_Dump(
                    "CanRemoveRedundantLoop:No because initial value of induction is signed and not constant\n"
                );
                return FALSE;
            }
        }

        if (!(loop->flags & LP_LOOP_STEP_ISPOS) && !(loop->flags & LP_LOOP_STEP_ISNEG)) {
            IRO_Dump(
                "CanRemoveRedundantLoop:No because LP_LOOP_STEP_ISPOS/LP_LOOP_STEP_ISNEG   is not set\n"
            );
            return FALSE;
        }

        if ((loop->flags & LP_LOOP_STEP_ISPOS) &&
            CheckStepOverFlow1_EmptyLoop(loop, &loop->iterCount, &loop->finalStoreVal)) {
            IRO_Dump("CanRemoveRedundantLoop:No because Final Value of indution will overflow\n");
            return FALSE;
        }

        if ((loop->flags & LP_LOOP_STEP_ISNEG) &&
            CheckStepOverFlow2_EmptyLoop(loop, &loop->iterCount, &loop->finalStoreVal)) {
            IRO_Dump("CanRemoveRedundantLoop:No because Final Value of indution will overflow\n");
            return FALSE;
        }
    }

    if (RedundantLoopCheck(loop)) {
        return TRUE;
    }
    return FALSE;
}

static int CanRemoveRedundantLoop1(IROLoop* loop) {
    if ((loop->flags & LP_WHILE_LOOP) && (loop->flags & LP_RECURSIVE_LOOP)) {
        if (loop->flags & LP_LOOP_HAS_ASM) {
            IRO_Dump("CanRemoveRedundantLoop1:No due to LP_LOOP_HAS_ASM \n");
            return FALSE;
        }

        if (loop->flags & LP_IFEXPR_NON_CANONICAL) {
            IRO_Dump("CanRemoveRedundantLoop1:No due to LP_IFEXPR_NON_CANONICAL \n");
            return FALSE;
        }

        if (loop->flags & LP_LOOP_HAS_CALLS) {
            IRO_Dump("CanRemoveRedundantLoop1:No due to LP_LOOP_HAS_CALLS \n");
            return FALSE;
        }

        if (loop->flags & LP_LOOP_HAS_CNTRLFLOW) {
            IRO_Dump("CanRemoveRedundantLoop1:No due to LP_LOOP_HAS_CNTRLFLOW \n");
            return FALSE;
        }

        if (loop->flags & LP_INDUCTION_NOT_FOUND) {
            IRO_Dump("CanRemoveRedundantLoop1:No due to LP_INDUCTION_NOT_FOUND \n");
            return FALSE;
        }

        if (loop->flags & LP_HAS_MULTIPLE_INDUCTIONS) {
            IRO_Dump("CanRemoveRedundantLoop1:No due to LP_HAS_MULTIPLE_INDUCTIONS \n");
            return FALSE;
        }

        if (loop->flags & LP_LOOP_HDR_HAS_SIDEEFFECTS) {
            IRO_Dump("CanRemoveRedundantLoop1:No due to LP_LOOP_HDR_HAS_SIDEEFFECTS \n");
            return FALSE;
        }

        if (!(loop->flags & LP_HEADER_FOLLOWS_UPDATE)) {
            IRO_Dump(
                "CanRemoveRedundantLoop1:No because header does not follow induction update \n"
            );
            return FALSE;
        }

        if (loop->induction->nd->type == IROLinearOp1Arg &&
            loop->induction->nd->nodetype == EPREDEC) {
            if (is_unsigned(loop->induction->nd->rtype)) {
                return TRUE;
            }
            IRO_Dump("CanRemoveRedundantLoop1:No because induction not of the right type \n");
            return FALSE;
        }

        IRO_Dump("CanRemoveRedundantLoop1:No because induction operator not a predec \n");
        return FALSE;
    } else {
        return FALSE;
    }
}

static int RedundantLoopCheck(IROLoop* loop) {
    IRONode* fnode;
    IROLinear* nd;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop) && fnode != loop->fnode && (nd = fnode->first)) {
            while (1) {
                if ((nd->index < loop->lo || nd->index > loop->hi) &&
                    nd->type != IROLinearNop &&
                    nd->type != IROLinearLabel) {
                    if (IS_LINEAR_DIADIC(nd, EASS)) {
                        if (!(nd->flags & IROLF_Reffed)) {
                            if (IS_LINEAR_MONADIC(nd->u.diadic.left, EINDIRECT)) {
                                if (nd->u.diadic.left->rtype &&
                                    CParser_IsVolatile(
                                        nd->u.diadic.left->rtype,
                                        nd->u.diadic.left->nodeflags & ENODE_FLAG_QUALS
                                    )) {
                                    IRO_Dump(
                                        " EASS at %d fail as store to volatile memory \n", nd->index
                                    );
                                    return FALSE;
                                }

                                if ((nd->u.diadic.left->u.monadic->flags & IROLF_LoopInvariant) &&
                                    (nd->u.diadic.right->flags & IROLF_LoopInvariant)) {
                                    IRO_Dump(" EASS at %d pass\n", nd->index);
                                } else {
                                    IRO_Dump(
                                        " EASS at %d fail,  either LHS address or RHS is variant \n",
                                        nd->index
                                    );
                                    return FALSE;
                                }
                            } else {
                                IRO_Dump(
                                    "Found EASS nodes whose lhs root  is not a EINDIRECT node\n"
                                );
                                return FALSE;
                            }
                        } else {
                            IRO_Dump(
                                "Found EASS node that is referenced i.e embedded assignment\n"
                            );
                            return FALSE;
                        }
                    } else {
                        if (!(nd->flags & IROLF_Reffed)) {
                            IRO_Dump("Found non EASS top level node in the loop\n");
                            return FALSE;
                        }
                    }
                }

                if (nd == fnode->last) {
                    break;
                }
                nd = nd->next;
            }
        }
    }

    return TRUE;
}

// NOTE: invented names: iterCount, finalValue, initNd, limitNd, initValue, limitValue
static int CheckStepOverFlow1_EmptyLoop(IROLoop* loop, CInt64* iterCount, CInt64* finalValue) {
    Boolean isUnsigned;
    IROLinear* initNd;
    IROLinear* limitNd;
    CInt64 initValue;
    CInt64 limitValue;
    CInt64 addConst;
    CInt64 work;
    CInt64 neg1;

    initNd = loop->init->u.diadic.right;
    limitNd = loop->cond->u.diadic.right;
    isUnsigned = is_unsigned(loop->cond->u.diadic.right->rtype);

    if (IRO_IsIntConstant(initNd) && IRO_IsIntConstant(limitNd)) {
        initValue = initNd->u.node->data.intval;
        limitValue = limitNd->u.node->data.intval;
        if (isUnsigned) {
            if (CInt64_LessEqualU(limitValue, initValue)) {
                return TRUE;
            }
        } else {
            if (CInt64_LessEqual(limitValue, initValue)) {
                return TRUE;
            }
        }

        CInt64_SetLong(&addConst, loop->induction->addConst);
        CInt64_SetLong(&neg1, -1);
        *iterCount = CInt64_Sub(limitValue, initValue);
        *iterCount = CInt64_Add(*iterCount, addConst);
        if (IS_LINEAR_DIADIC(loop->cond, ELESS)) {
            *iterCount = CInt64_Add(*iterCount, neg1);
        }

        CError_ASSERT(855, !CInt64_IsZero(&addConst));

        if (isUnsigned) {
            *iterCount = CInt64_DivU(*iterCount, addConst);
        } else {
            *iterCount = CInt64_Div(*iterCount, addConst);
        }

        if (CInt64_Equal(*iterCount, cint64_zero)) {
            return TRUE;
        }

        if (isUnsigned) {
            if (CInt64_LessEqualU(*iterCount, cint64_zero)) {
                CError_FATAL(877);
            }
        } else {
            if (CInt64_LessEqual(*iterCount, cint64_zero)) {
                CError_FATAL(886);
            }
        }

        if (isUnsigned) {
            *finalValue = CInt64_MulU(*iterCount, addConst);
            *finalValue = CInt64_Add(*finalValue, initValue);
        } else {
            *finalValue = CInt64_Mul(*iterCount, addConst);
            *finalValue = CInt64_Add(*finalValue, initValue);
        }
    } else {
        return TRUE;
    }

    CInt64_SetLong(&addConst, loop->induction->addConst);
    work = CInt64_Add(limitValue, addConst);

    if (isUnsigned) {
        if (CInt64_LessU(work, limitValue)) {
            return TRUE;
        }
    } else {
        if (CInt64_Less(work, limitValue)) {
            return TRUE;
        }
    }

    return FALSE;
}

// NOTE: invented names: iterCount, finalValue, limitNd, initNd, limitValue, initValue
static int CheckStepOverFlow2_EmptyLoop(IROLoop* loop, CInt64* iterCount, CInt64* finalValue) {
    Boolean isUnsigned;
    IROLinear* limitNd;
    IROLinear* initNd;
    CInt64 limitValue;
    CInt64 initValue;
    CInt64 addConst;
    CInt64 work;
    CInt64 neg1;

    initNd = loop->init->u.diadic.right;
    limitNd = loop->cond->u.diadic.right;
    isUnsigned = is_unsigned(loop->cond->u.diadic.right->rtype);

    if (IRO_IsIntConstant(limitNd) && IRO_IsIntConstant(initNd)) {
        limitValue = limitNd->u.node->data.intval;
        initValue = initNd->u.node->data.intval;
        if (isUnsigned) {
            if (CInt64_LessEqualU(initValue, limitValue)) {
                return TRUE;
            }
        } else {
            if (CInt64_LessEqual(initValue, limitValue)) {
                return TRUE;
            }
        }

        CInt64_SetLong(&addConst, loop->induction->addConst);
        CInt64_SetLong(&neg1, -1);
        *iterCount = CInt64_Sub(initValue, limitValue);
        *iterCount = CInt64_Add(*iterCount, addConst);
        if (IS_LINEAR_DIADIC(loop->cond, EGREATER)) {
            *iterCount = CInt64_Add(*iterCount, neg1);
        }

        CError_ASSERT(995, !CInt64_IsZero(&addConst));

        if (isUnsigned) {
            *iterCount = CInt64_DivU(*iterCount, addConst);
        } else {
            *iterCount = CInt64_Div(*iterCount, addConst);
        }

        if (CInt64_Equal(*iterCount, cint64_zero)) {
            return TRUE;
        }

        if (isUnsigned) {
            if (CInt64_LessEqualU(*iterCount, cint64_zero)) {
                return FALSE;
            }
        } else {
            if (CInt64_LessEqual(*iterCount, cint64_zero)) {
                return FALSE;
            }
        }

        if (isUnsigned) {
            *finalValue = CInt64_MulU(*iterCount, addConst);
            *finalValue = CInt64_Sub(initValue, *finalValue);
        } else {
            *finalValue = CInt64_Mul(*iterCount, addConst);
            *finalValue = CInt64_Sub(initValue, *finalValue);
        }
    } else {
        return TRUE;
    }

    CInt64_SetLong(&addConst, loop->induction->addConst);
    work = CInt64_Sub(limitValue, addConst);

    if (isUnsigned) {
        if (CInt64_GreaterU(work, limitValue)) {
            return TRUE;
        }
    } else {
        // the original's bug: compares with initValue where the unsigned branch has limitValue
        if (CInt64_Greater(work, initValue)) {
            return TRUE;
        }
    }

    return FALSE;
}
