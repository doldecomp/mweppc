#include "compiler/IroUnrollLoop.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroLoop.h"
#include "compiler/IrOptimizer.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/BitVector.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/LoopDetection.h"
#include "compiler/types.h"
#include <string.h>

#pragma pack(push, 2)
typedef struct LoopList {
    UInt8 flags;
    BitVector* bv;
    struct LoopList* next;
    IRONode* fnode;
    int unused;
} LoopList;
#pragma pack(pop)

static void IRO_FindLoops_Unroll(void);
static void LoopUnroll(int count, IRONode* header);
static int IsLoopUnrollable(IROLoop* loop);
static int IsDifferenceOfTermsConstant(
    IROAddrRecord* lowerRec, IROAddrRecord* upperRec, int isUnsigned, CInt64* pval
);
static IROLinear* BuildOrigIterationCount_DoWhile(IROList* list, IROLoop* loop);
static IROLinear* BuildPreAlignTemp(IROLoopInd* ind, UInt32 unrollFactor, IROList* list);
static IROLinear* BuildNewFinalvalue_DoWhile(
    IROLinear* iterCount, UInt32 unrollFactor, IROList* list, IROLoop* loop
);
static IROLinear* BuildUnrolledFinalvalue_DoWhile(
    IROLinear* iterCount, UInt32 unrollFactor, IROList* list, IROLoop* loop
);
IROLinear* BuildEarlyLoopExitTest(IROLinearType type, IROList* list);
IROLinear* BuildLoopExitTest(IROLinearType type, IROList* list);
int IsIterationCountConstant(IROLoop* loop, CInt64* pval);
void NoOpBlock(IRONode* fnode);
void IRO_TestConstantIterationCount(
    IROLoop* loop,
    CInt64* iterCount,
    SInt32 vectorStride,
    UInt32* unrollFactor,
    SInt32* leftOver,
    UInt32* needOrigLoop,
    UInt32* needUnrollBodyTest,
    UInt32* resetUnrolledFinalValue
);
IROLinear* BuildOrigIterationCount(IROList* list, IROLoop* loop);
IROLinear* BuildNewFinalvalue(
    IROLinear* iterCount, UInt32 unrollFactor, IROList* list, IROLoop* loop
);
void BuildUnrolledBodyEntryTest(
    IROList* list, IROLinear* iterCount, UInt32 unrollFactor, CLabel* label
);
void ChangeInductionReference(IROLinear* first, IROLinear* last, CInt64 val, IROLoop* loop);
IROLinear* UpdateInductionIncrement(IROLoop* loop, SInt32 value, IROLinear* before);

void IRO_LoopUnroller(void) {
    VectorPhaseCalledFromUnroll = TRUE;
    IRO_FindLoops_Unroll();
    IRO_CheckForUserBreak();
}

static void IRO_FindLoops_Unroll(void) {
    IRONode* fnode;
    IRONode* pred;
    UInt16 i;
    UInt16 isHeader;
    LoopList* list;
    LoopList* other;

    fnode = IRO_FirstNode;
    LoopList_First = NULL;

    while (fnode) {
        isHeader = FALSE;
        for (i = 0; i < fnode->numpred; i++) {
            pred = IRO_NodeTable[fnode->pred[i]];
            if (Bv_IsBitSet(fnode->index, pred->dom)) {
                if (!isHeader) {
                    Bv_AllocVector(&InLoop, IRO_NumNodes + 1);
                    Bv_Clear(InLoop);
                    Bv_SetBit(fnode->index, InLoop);
                }
                isHeader = TRUE;
                Bv_SetBit(pred->index, InLoop);
                if (pred != fnode) {
                    AddPreds(pred);
                }
            }
        }

        if (isHeader) {
            if (!LoopList_First) {
                list = oalloc(sizeof(LoopList));
                list->next = NULL;
            } else {
                list = oalloc(sizeof(LoopList));
                list->next = LoopList_First;
            }
            LoopList_First = list;

            Bv_AllocVector(&list->bv, IRO_NumNodes + 1);
            list->flags |= 1;
            Bv_Copy(InLoop, list->bv);
            list->fnode = fnode;
            list->unused = 0;
        }

        fnode = fnode->nextnode;
    }

    list = LoopList_First;
    Bv_AllocVector(&LoopTemp, IRO_NumNodes + 1);
    while (list) {
        for (other = LoopList_First; other; other = other->next) {
            if (other != list) {
                IRO_Dump(" header = %d \n", other->fnode->index);
                IRO_Dump(" l1 bit vector=\n");
                IRO_DumpBits("", other->bv);
                IRO_Dump(" l bit vector=\n");
                IRO_DumpBits("", list->bv);
                if (Bv_IsSubset(list->bv, other->bv)) {
                    other->flags &= ~1;
                }
            }
        }
        list = list->next;
    }

    for (list = LoopList_First; list; list = list->next) {
        if (list->flags & 1) {
            IRONode* listfnode;
            Bv_Copy(list->bv, InLoop);
            listfnode = list->fnode;
            IRO_Dump("IRO_FindLoops_Unroll:Found loop with header %d\n", listfnode->index);
            IRO_DumpBits("Loop includes: ", InLoop);
            LoopUnroll(copts.unrollfactor, listfnode);
            IRO_UpdateFlagsOnInts();
        }
    }
}

static int CheckConstant(CInt64 a, CInt64 b, CInt64* result) {
    CInt64 shl = cint64_zero;
    CInt64 work = cint64_zero;
    CInt64 and = cint64_zero;
    CInt64 i = cint64_zero;

    for (i = cint64_zero; CInt64_Less(i, a); i = CInt64_Add(i, cint64_one)) {
        shl = CInt64_Shl(b, i);
        and = CInt64_And(shl, work);
        if (CInt64_NotEqual(and, cint64_zero)) {
            return FALSE;
        } else {
            work = CInt64_Or(shl, work);
        }
    }

    *result = work;
    return TRUE;
}

typedef struct LoopPattern {
    IROLinear* indexExpr;
    IROLinear* baseExpr;
    Type* addrType;
    IROLinear* bitExpr;
    IROLinear* valueExpr;
    CInt64 bits;
    CInt64 shift;
} LoopPattern;

static void UnrollWhileLoopBody(
    IRONode* header,
    IRONode* fnode2,
    IRONode* fnode3,
    IROLoop* loop,
    LoopPattern* pattern,
    UInt32 unrollFactor
) {
    IRONode* scan;
    int pass;
    IROLinear* firstnode;
    IROLinear* lastnd;
    IROLinear* indexNd;
    IROLinear* offsetNd;
    IROLinear* nd;
    IROLinear* baseNd;
    IROLinear* addressNd;
    IROLinear* targetNd;
    IROLinear* assignNd;
    IROLinear* valueNd;
    IROLinear* bitsNd;
    ENode* expr;
    IROList list;
    CInt64 zero;
    CInt64 shiftval;

    CInt64_SetLong(&zero, 0);

    pass = 0;

    do {
        firstnode = NULL;
        for (scan = fnode3; scan && scan != header; scan = scan->nextnode) {
            IRO_InitList(&list);
            lastnd = scan->last;
            nd = scan->first;
            while (1) {
                if (nd->stmt) {
                    nd->stmt->flags |= StmtFlag_Unrolled;
                }

                if ((nd->index < loop->lo || nd->index > loop->hi) &&
                    nd->type != IROLinearLabel &&
                    nd->type != IROLinearNop &&
                    !(nd->flags & IROLF_Reffed)) {
                    CError_ASSERT(
                        345,
                        nd->nodetype == EORASS || nd->nodetype == EANDASS || nd->nodetype == EXORASS
                    );

                    IRO_DuplicateExpr(pattern->indexExpr, &list);
                    indexNd = list.tail;

                    shiftval = cint64_one;
                    shiftval = CInt64_Shl(shiftval, pattern->shift);

                    offsetNd = IRO_NewLinear(IROLinearOperand);
                    offsetNd->index = ++IRO_NumLinear;
                    offsetNd->rtype = pattern->indexExpr->rtype;
                    expr = IRO_NewENode(EINTCONST);
                    expr->rtype = pattern->indexExpr->rtype;
                    CInt64_SetLong(&expr->data.intval, pass * CInt64_GetULong(&shiftval));
                    offsetNd->u.node = expr;
                    IRO_AddToList(offsetNd, &list);

                    IRO_DuplicateExpr(pattern->baseExpr, &list);

                    baseNd = IRO_NewLinear(IROLinearOp2Arg);
                    baseNd->index = ++IRO_NumLinear;
                    baseNd->nodetype = EADD;
                    baseNd->rtype = pattern->addrType;
                    baseNd->u.diadic.left = list.tail;
                    baseNd->u.diadic.right = offsetNd;
                    IRO_AddToList(baseNd, &list);

                    addressNd = IRO_NewLinear(IROLinearOp2Arg);
                    addressNd->index = ++IRO_NumLinear;
                    addressNd->nodetype = EADD;
                    addressNd->rtype = pattern->addrType;
                    addressNd->u.diadic.left = baseNd;
                    addressNd->u.diadic.right = indexNd;
                    IRO_AddToList(addressNd, &list);

                    targetNd = IRO_NewLinear(IROLinearOp1Arg);
                    targetNd->index = ++IRO_NumLinear;
                    targetNd->nodetype = EINDIRECT;
                    targetNd->rtype = nd->rtype;
                    targetNd->u.monadic = addressNd;
                    IRO_AddToList(targetNd, &list);

                    assignNd = IRO_NewLinear(IROLinearOp2Arg);
                    *assignNd = *nd;
                    assignNd->index = ++IRO_NumLinear;
                    assignNd->u.diadic.left = list.tail;
                    assignNd->next = NULL;

                    bitsNd = IRO_NewLinear(IROLinearOperand);
                    bitsNd->index = ++IRO_NumLinear;
                    bitsNd->rtype = pattern->bitExpr->rtype;
                    expr = IRO_NewENode(EINTCONST);
                    expr->rtype = pattern->bitExpr->rtype;
                    bitsNd->u.node = expr;
                    bitsNd->next = NULL;
                    expr->data.intval = pattern->bits;

                    if (IS_LINEAR_DIADIC(nd, EANDASS) && CInt64_Equal(pattern->bits, cint64_zero)) {
                        assignNd->nodetype = EASS;
                    } else if (IS_LINEAR_DIADIC(nd, EORASS) && !pattern->bits.hi) {
                        UInt32 mask = CInt64_GetULong(&pattern->bits);
                        if ((nd->rtype->size == 1 && mask == 0xFF) ||
                            (nd->rtype->size == 2 && mask == 0xFFFF) ||
                            (nd->rtype->size == 4 && mask == 0xFFFFFFFF)) {
                            assignNd->nodetype = EASS;
                        }
                    }

                    IRO_AddToList(bitsNd, &list);

                    if (IS_LINEAR_MONADIC(pattern->valueExpr, ETYPCON)) {
                        valueNd = IRO_NewLinear(IROLinearOp1Arg);
                        *valueNd = *pattern->valueExpr;
                        valueNd->index = ++IRO_NumLinear;
                        valueNd->u.monadic = bitsNd;
                        valueNd->next = NULL;
                        IRO_AddToList(valueNd, &list);
                    } else {
                        valueNd = bitsNd;
                    }
                    assignNd->u.diadic.right = valueNd;
                    IRO_AddToList(assignNd, &list);

                    if (!firstnode) {
                        firstnode = list.head;
                    }
                }

                if (nd == lastnd) {
                    break;
                }
                nd = nd->next;
            }

            if (list.head && list.tail) {
                IRO_Paste(list.head, list.tail, fnode2->last);
            }
        }
    } while (++pass < 8);
}

static int PatternMatchLoop(
    IRONode* fnode,
    IROLoop* loop,
    IROLoopInd* ind,
    UInt32* unrollFactor,
    SInt32* matchCount,
    SInt32* bitOpCount,
    LoopPattern* pattern
) {
    IROLinear* scan;
    IROLinear* varnode;
    IROLinear* indexNd;
    IROLinear* maskNd;
    IROLinear* left1;
    IROLinear* left2;
    IROLinear* right1;
    IROLinear* right2;
    Object* obj1;
    Object* obj2;
    CInt64 shl;
    CInt64 val;

    *matchCount = 0;
    *bitOpCount = 0;

    if ((scan = fnode->first)) {
        while (1) {
            if ((scan->index < loop->lo || scan->index > loop->hi) &&
                !(scan->flags & IROLF_Reffed) &&
                scan->type != IROLinearNop &&
                scan->type != IROLinearLabel) {
                if (IS_LINEAR_DIADIC_3(scan, EORASS, EXORASS, EANDASS)) {
                    (*bitOpCount)++;
                    if (IS_LINEAR_MONADIC(scan->u.diadic.left, EINDIRECT)) {
                        varnode = scan->u.diadic.left->u.monadic;
                        if (IS_LINEAR_DIADIC(varnode, EADD)) {
                            pattern->baseExpr = varnode->u.diadic.left;
                            pattern->addrType = varnode->rtype;
                            if (IRO_IsVariable(varnode->u.diadic.left)) {
                                pattern->indexExpr = varnode->u.diadic.right;
                                if (IS_LINEAR_DIADIC(pattern->indexExpr, ESHL) &&
                                    IRO_IsConstant(pattern->indexExpr->u.diadic.right)) {
                                    pattern->shift =
                                        pattern->indexExpr->u.diadic.right->u.node->data.intval;
                                    indexNd = pattern->indexExpr->u.diadic.left;
                                } else {
                                    return FALSE;
                                }
                            } else {
                                return FALSE;
                            }
                        } else {
                            return FALSE;
                        }
                    } else {
                        return FALSE;
                    }

                    pattern->valueExpr = scan->u.diadic.right;

                    if (IS_LINEAR_MONADIC(pattern->valueExpr, ETYPCON)) {
                        if (IS_LINEAR_DIADIC(scan, EANDASS)) {
                            if (IS_LINEAR_MONADIC(pattern->valueExpr->u.monadic, EBINNOT)) {
                                pattern->bitExpr = pattern->valueExpr->u.monadic->u.monadic;
                            } else {
                                return FALSE;
                            }
                        } else {
                            pattern->bitExpr = pattern->valueExpr->u.monadic;
                        }

                        if (IS_LINEAR_DIADIC(pattern->bitExpr, ESHL) &&
                            IRO_IsConstant(pattern->bitExpr->u.diadic.left)) {
                            val = pattern->bitExpr->u.diadic.left->u.node->data.intval;
                            maskNd = pattern->bitExpr->u.diadic.right;
                        } else {
                            return FALSE;
                        }
                    } else if (IS_LINEAR_DIADIC(pattern->valueExpr, ESHL) &&
                               IS_LINEAR_DIADIC_2(scan, EORASS, EXORASS)) {
                        pattern->bitExpr = pattern->valueExpr;
                        if (IRO_IsConstant(pattern->bitExpr->u.diadic.left)) {
                            val = pattern->bitExpr->u.diadic.left->u.node->data.intval;
                            maskNd = pattern->bitExpr->u.diadic.right;
                        } else {
                            return FALSE;
                        }
                    } else if (IS_LINEAR_MONADIC(pattern->valueExpr, EBINNOT) &&
                               IS_LINEAR_DIADIC(scan, EANDASS)) {
                        pattern->bitExpr = pattern->valueExpr->u.monadic;
                        if (IS_LINEAR_DIADIC(pattern->bitExpr, ESHL) &&
                            IRO_IsConstant(pattern->bitExpr->u.diadic.left)) {
                            val = pattern->bitExpr->u.diadic.left->u.node->data.intval;
                            maskNd = pattern->bitExpr->u.diadic.right;
                        } else {
                            return FALSE;
                        }
                    } else {
                        return FALSE;
                    }

                    if (IS_LINEAR_DIADIC(maskNd, EAND) && IS_LINEAR_DIADIC(indexNd, ESHR)) {
                        left1 = indexNd->u.diadic.left;
                        left2 = maskNd->u.diadic.left;
                        obj1 = IRO_IsVariable(left1);
                        obj2 = IRO_IsVariable(left2);
                        if (obj1 == obj2 && obj1 == ind->var->object) {
                            right1 = indexNd->u.diadic.right;
                            right2 = maskNd->u.diadic.right;
                            if (IRO_IsConstant(right1) && IRO_IsConstant(right2)) {
                                shl = cint64_one;
                                shl = CInt64_Shl(shl, right1->u.node->data.intval);
                                shl = CInt64_Sub(shl, cint64_one);
                                if (CInt64_Equal(shl, right2->u.node->data.intval)) {
                                    if (shl.hi == 0) {
                                        *unrollFactor = CInt64_GetULong(&shl) + 1;
                                        if (CheckConstant(
                                                CInt64_Add(shl, cint64_one), val, &pattern->bits
                                            )) {
                                            (*matchCount)++;
                                            if (IS_LINEAR_DIADIC(scan, EANDASS)) {
                                                pattern->bits = CInt64_Not(pattern->bits);
                                            }
                                        }
                                    } else {
                                        return FALSE;
                                    }
                                } else {
                                    return FALSE;
                                }
                            } else {
                                return FALSE;
                            }
                        } else {
                            return FALSE;
                        }
                    } else {
                        return FALSE;
                    }
                } else {
                    return FALSE;
                }
            }

            if (scan == fnode->last) {
                break;
            }
            scan = scan->next;
        }
    }

    return TRUE;
}

static UInt32 UnrollWhileLoop(
    IRONode* header, IRONode* fnode2, IRONode* fnode3, IROLoop* loop, UInt32 unrollFactor
) {
    IROLinear* less;
    IRONode* newfnode7;
    IROLinear* label2nd;
    IROLoopInd* ind;
    IRONode* scan;
    CLabel* label3;
    IROLinear* unrolledFinalValue;
    IROLinear* earlyLoopExitTest;
    CLabel* earlyLoopExitTestLabel;
    CLabel* lastlabel;
    CLabel* label;
    IROLinear* gotond;
    IROLinear* preAlignTemp;
    IROLinear* savedHead2;
    CLabel* label2;
    IROLinear* indvar;
    IROLinear* bodyHead;
    IROLinear* loopExitTest;
    IROLinear* saveTail;
    IROLinear* newFinalValue;
    IROLinear* saveTail2;
    IROLinear* origIterationCount;
    IROLinear* iterCountHead;
    IROLinear* unrolledBodyEntryTest;
    IROLinear* savedHead3;
    IROLinear* less2;
    IROLinear* loopscan;
    IROLinear* loopend;
    IROLinear* updIndInc;
    IROLinear* less3;
    IROLinear* exitCond;
    IROLinear* constnd;
    IROLinear* ass;
    IROLinear* oldLast;
    IRONode* oldNext;
    IRONode* newfnode2;
    IRONode* newfnode1;
    IROLinear* lastlabelnode;
    IRONode* newfnode3;
    IRONode* newfnode4;
    IRONode* newfnode5;
    IRONode* newfnode6;
    IRONode* newfnode8;
    IROLinear* lastnd;
    ENode* expr;
    IROList list;
    SInt32 matchCount;
    SInt32 bitOpCount;
    LoopPattern pattern;

    IRO_Dump("while(n--) loop \n");

    if (loop->flags & LP_IND_USED_IN_LOOP) {
        IRO_Dump("loop not unrolled because induction used in loop \n");
        return 0;
    }
    if (loop->flags & LP_HAS_MULTIPLE_EXITS) {
        IRO_Dump("loop not unrolled because loop has multiple exits \n");
        return 0;
    }

    if (!(loop->flags & LP_HAS_MULTIPLE_INDUCTIONS)) {
        return 0;
    }

    for (ind = FirstInd; ind; ind = ind->next) {
        if ((ind->flags & LoopInd_HasMod) && (ind->flags & LoopInd_HasDiv)) {
            break;
        }
    }

    if (!ind) {
        IRO_Dump("Could not find loop with and induction with MOD and DIV operation\n");
        return 0;
    }

    if (!is_unsigned(ind->nd->rtype)) {
        return 0;
    }

    if (ind->nd->type == IROLinearOp2Arg) {
        if (ind->nd->nodetype == EADDASS && IRO_IsConstant(ind->nd->u.diadic.right)) {
            if (ind->addConst != 1) {
                return 0;
            }
        } else if (ind->nd->nodetype == EASS) {
            if (ind->nd->u.diadic.right->type != IROLinearOp2Arg ||
                ind->nd->u.diadic.right->nodetype != EADD ||
                !IRO_IsConstant(ind->nd->u.diadic.right->u.diadic.right)) {
                return 0;
            }

            if (ind->addConst != 1) {
                return 0;
            }
        } else {
            return 0;
        }
    } else if (ind->nd->type == IROLinearOp1Arg &&
               ind->nd->nodetype != EPREINC &&
               ind->nd->nodetype != EPOSTINC) {
        return 0;
    }

    loop->induction = ind;
    loop->hi = ind->nd->index;
    loop->lo = IRO_FindStart(ind->nd)->index;

    scan = IRO_FirstNode;
    memset(&pattern, 0, sizeof(pattern));
    while (scan) {
        if (Bv_IsBitSet(scan->index, InLoop) && scan != header) {
            if (!PatternMatchLoop(
                    scan, loop, ind, &unrollFactor, &matchCount, &bitOpCount, &pattern
                )) {
                return 0;
            }
        }
        scan = scan->nextnode;
    }

    if (matchCount > 1 || bitOpCount > 1) {
        return 0;
    }

    lastlabel = fnode2->last->u.label.label;
    lastlabelnode = IRO_FindLabelNode(fnode2->last->u.label.label, fnode2->last);

    IRO_InitList(&list);
    IRO_DuplicateExprRange(lastlabelnode->next, LoopNode->last->u.label.cond->u.diadic.left, &list);
    IRO_DuplicateExpr(LoopNode->last->u.label.cond, &list);
    IRO_Paste(list.head, list.tail, fnode2->last);
    lastlabelnode = list.tail;

    IRO_InitList(&list);
    earlyLoopExitTest = BuildEarlyLoopExitTest(LoopNode->last->type, &list);
    earlyLoopExitTestLabel = IRO_NewLabel();
    earlyLoopExitTest->u.label.label = earlyLoopExitTestLabel;
    earlyLoopExitTest->u.label.cond = lastlabelnode;
    earlyLoopExitTest->u.label.cond->flags |= IROLF_Reffed;
    earlyLoopExitTest->rtype = LoopNode->last->rtype;
    IRO_Paste(list.head, list.tail, fnode2->last);

    IRO_InitList(&list);
    origIterationCount = BuildOrigIterationCount_DoWhile(&list, loop);
    IRO_Paste(list.head, list.tail, fnode2->last);
    iterCountHead = list.head;

    IRO_InitList(&list);
    preAlignTemp = BuildPreAlignTemp(ind, unrollFactor, &list);
    IRO_Paste(list.head, list.tail, fnode2->last);

    IRO_InitList(&list);
    unrolledFinalValue =
        BuildUnrolledFinalvalue_DoWhile(origIterationCount, unrollFactor, &list, loop);
    IRO_Paste(list.head, list.tail, fnode2->last);

    IRO_InitList(&list);
    newFinalValue = BuildNewFinalvalue_DoWhile(origIterationCount, unrollFactor, &list, loop);
    IRO_Paste(list.head, list.tail, fnode2->last);

    IRO_InitList(&list);
    BuildUnrolledBodyEntryTest(&list, origIterationCount, unrollFactor, lastlabel);
    IRO_Paste(list.head, list.tail, fnode2->last);
    unrolledBodyEntryTest = list.tail;

    IRO_InitList(&list);
    label = BuildLabel(&list);
    IRO_Paste(list.head, list.tail, fnode2->last);

    savedHead2 = list.head;
    bodyHead = NULL;
    for (scan = fnode3; scan && scan != header; scan = scan->nextnode) {
        IRO_InitList(&list);
        loopend = scan->last;
        loopscan = scan->first;
        while (1) {
            if (loopscan->stmt) {
                loopscan->stmt->flags |= StmtFlag_Unrolled;
            }
            if (loopscan->type != IROLinearLabel && !(loopscan->flags & IROLF_Reffed)) {
                IRO_DuplicateExpr(loopscan, &list);
                if (!bodyHead) {
                    bodyHead = list.head;
                }
            }
            if (loopscan == loopend) {
                break;
            }
            loopscan = loopscan->next;
        }

        if (list.head && list.tail) {
            IRO_Paste(list.head, list.tail, fnode2->last);
        }
    }

    IRO_InitList(&list);

    if (ind->nd->type == IROLinearOp1Arg) {
        IRO_DuplicateExpr(ind->nd->u.monadic, &list);
    } else {
        IRO_DuplicateExpr(ind->nd->u.diadic.left, &list);
    }
    list.tail->flags &= ~IROLF_Assigned;
    indvar = list.tail;

    IRO_DuplicateExpr(preAlignTemp, &list);
    list.tail->flags &= ~IROLF_Assigned;

    less = IRO_NewLinear(IROLinearOp2Arg);
    less->nodetype = ELESS;
    less->rtype = (Type*)&stbool;
    less->index = ++IRO_NumLinear;
    less->next = NULL;
    less->u.diadic.left = indvar;
    less->u.diadic.right = list.tail;
    IRO_AddToList(less, &list);
    less->flags |= IROLF_Reffed;

    loopExitTest = BuildLoopExitTest(LoopNode->last->type, &list);
    loopExitTest->u.label.label = label;
    loopExitTest->u.label.cond = less;
    loopExitTest->u.label.cond->flags |= IROLF_Reffed;
    loopExitTest->rtype = LoopNode->last->rtype;
    IRO_Paste(list.head, list.tail, fnode2->last);
    saveTail = list.tail;

    IRO_InitList(&list);
    label2 = IRO_NewLabel();
    gotond = IRO_NewLinear(IROLinearOp1Arg);
    gotond->index = ++IRO_NumLinear;
    gotond->type = IROLinearGoto;
    gotond->u.label.label = label2;
    IRO_AddToList(gotond, &list);
    IRO_Paste(list.head, list.tail, fnode2->last);

    IRO_InitList(&list);
    label3 = BuildLabel(&list);
    IRO_Paste(list.head, list.tail, fnode2->last);
    savedHead3 = list.head;

    UnrollWhileLoopBody(header, fnode2, fnode3, loop, &pattern, unrollFactor);
    updIndInc = UpdateInductionIncrement(loop, 8 * unrollFactor, fnode2->last);

    IRO_InitList(&list);
    label2nd = IRO_NewLinear(IROLinearLabel);
    label2nd->index = IRO_NumLinear++;
    label2nd->u.label.label = label2;
    label2nd->flags |= IROLF_StartsNode;
    IRO_AddToList(label2nd, &list);
    IRO_Paste(list.head, list.tail, fnode2->last);

    IRO_InitList(&list);

    if (ind->nd->type == IROLinearOp1Arg) {
        IRO_DuplicateExpr(ind->nd->u.monadic, &list);
    } else {
        IRO_DuplicateExpr(ind->nd->u.diadic.left, &list);
    }
    list.tail->flags &= ~IROLF_Assigned;
    indvar = list.tail;

    IRO_DuplicateExpr(unrolledFinalValue, &list);
    list.tail->flags &= ~IROLF_Assigned;

    less2 = IRO_NewLinear(IROLinearOp2Arg);
    less2->nodetype = ELESS;
    less2->rtype = (Type*)&stbool;
    less2->index = ++IRO_NumLinear;
    less2->next = NULL;
    less2->u.diadic.left = indvar;
    less2->u.diadic.right = list.tail;
    IRO_AddToList(less2, &list);
    less2->flags |= IROLF_Reffed;

    loopExitTest = BuildLoopExitTest(LoopNode->last->type, &list);
    loopExitTest->u.label.label = label3;
    loopExitTest->u.label.cond = less2;
    loopExitTest->u.label.cond->flags |= IROLF_Reffed;
    loopExitTest->rtype = LoopNode->last->rtype;
    IRO_Paste(list.head, list.tail, fnode2->last);
    saveTail2 = list.tail;

    IRO_InitList(&list);

    if (ind->nd->type == IROLinearOp1Arg) {
        IRO_DuplicateExpr(ind->nd->u.monadic, &list);
    } else {
        IRO_DuplicateExpr(ind->nd->u.diadic.left, &list);
    }
    list.tail->flags &= ~IROLF_Assigned;
    indvar = list.tail;

    IRO_DuplicateExpr(newFinalValue, &list);
    list.tail->flags &= ~IROLF_Assigned;

    less3 = IRO_NewLinear(IROLinearOp2Arg);
    less3->nodetype = ELESS;
    less3->rtype = (Type*)&stbool;
    less3->index = ++IRO_NumLinear;
    less3->next = NULL;
    less3->u.diadic.left = indvar;
    less3->u.diadic.right = list.tail;
    IRO_AddToList(less3, &list);
    less3->flags |= IROLF_Reffed;

    exitCond = LoopNode->last->u.label.cond;
    IRO_Paste(list.head, list.tail, LoopNode->last);
    LoopNode->last->u.label.cond = list.tail;

    IRO_InitList(&list);

    constnd = IRO_NewLinear(IROLinearOperand);
    constnd->index = ++IRO_NumLinear;
    expr = IRO_NewENode(EINTCONST);
    expr->rtype = exitCond->u.diadic.left->rtype;
    expr->data.intval = cint64_zero;
    constnd->u.node = expr;
    constnd->rtype = expr->rtype;
    IRO_AddToList(constnd, &list);
    constnd->flags |= IROLF_Reffed;

    IRO_DuplicateExpr(exitCond->u.diadic.left, &list);

    ass = IRO_NewLinear(IROLinearOp2Arg);
    ass->nodetype = EASS;
    ass->rtype = list.tail->rtype;
    ass->index = ++IRO_NumLinear;
    ass->next = NULL;
    ass->u.diadic.left = list.tail;
    ass->u.diadic.right = constnd;
    IRO_AddToList(ass, &list);
    ass->flags |= IROLF_Assigned;

    IRO_NopOut(exitCond);

    oldNext = fnode2->nextnode;
    oldLast = fnode2->last;
    fnode2->last = earlyLoopExitTest;

    newfnode1 = IRO_NewFlowGraphNode();
    newfnode1->first = iterCountHead;
    newfnode1->last = unrolledBodyEntryTest;
    fnode2->nextnode = newfnode1;

    newfnode2 = IRO_NewFlowGraphNode();
    newfnode2->first = savedHead2;
    newfnode2->last = saveTail;
    savedHead2->u.label.label->stmt = (Statement*)newfnode2;
    newfnode1->nextnode = newfnode2;

    newfnode3 = IRO_NewFlowGraphNode();
    newfnode3->first = gotond;
    newfnode3->last = gotond;
    newfnode2->nextnode = newfnode3;

    newfnode4 = IRO_NewFlowGraphNode();
    newfnode4->first = savedHead3;
    newfnode4->last = updIndInc;
    savedHead3->u.label.label->stmt = (Statement*)newfnode4;
    newfnode3->nextnode = newfnode4;

    newfnode5 = IRO_NewFlowGraphNode();
    newfnode5->first = label2nd;
    newfnode5->last = saveTail2;
    label2nd->u.label.label->stmt = (Statement*)newfnode5;
    newfnode4->nextnode = newfnode5;

    newfnode6 = IRO_NewFlowGraphNode();
    newfnode6->first = oldLast;
    newfnode6->last = oldLast;
    newfnode5->nextnode = newfnode6;
    newfnode6->nextnode = oldNext;

    newfnode7 = oalloc(sizeof(IRONode));
    memset(newfnode7, 0, sizeof(IRONode));
    newfnode7->index = IRO_NumNodes;
    IRO_NumNodes++;

    newfnode7->first = list.head;
    newfnode7->last = list.tail;

    list.tail->next = LoopNode->last->next;
    LoopNode->last->next = list.head;

    newfnode7->nextnode = LoopNode->nextnode;
    LoopNode->nextnode = newfnode7;

    newfnode8 = oalloc(sizeof(IRONode));
    memset(newfnode8, 0, sizeof(IRONode));
    newfnode8->index = IRO_NumNodes;
    IRO_NumNodes++;

    lastnd = IRO_NewLinear(IROLinearLabel);
    lastnd->index = IRO_NumLinear++;
    lastnd->next = NULL;
    lastnd->u.label.label = earlyLoopExitTestLabel;
    lastnd->flags |= IROLF_StartsNode;
    earlyLoopExitTestLabel->stmt = (Statement*)newfnode8;

    newfnode8->first = lastnd;
    newfnode8->last = lastnd;

    lastnd->next = newfnode7->last->next;
    newfnode7->last->next = lastnd;

    newfnode8->nextnode = newfnode7->nextnode;
    newfnode7->nextnode = newfnode8;

    return 1;
}

void IRO_IterateForLoopBody(
    IRONode* start,
    IRONode* end,
    IROLoop* loop,
    IROLinear* destnode,
    SInt32 addConst,
    CInt64* val,
    Boolean notFirstCopy
) {
    IROLinear* first = NULL;
    IROLinear* last = NULL;
    IRONode* fnode;
    IROLinear* lastnd;
    IROLinear* nd;
    IROList list;

    for (fnode = start; fnode && fnode != end; fnode = fnode->nextnode) {
        IRO_InitList(&list);

        lastnd = fnode->last;
        nd = fnode->first;
        while (1) {
            if (nd->stmt) {
                nd->stmt->flags |= StmtFlag_Unrolled;
            }

            if ((nd->index < loop->lo || nd->index > loop->hi) &&
                nd->type != IROLinearLabel &&
                !(nd->flags & IROLF_Reffed)) {
                IRO_DuplicateExpr(nd, &list);
                if (!first) {
                    first = list.head;
                }
                last = list.tail;
            }

            if (nd == lastnd) {
                break;
            }
            nd = nd->next;
        }

        if (list.head && list.tail) {
            IRO_Paste(list.head, list.tail, destnode);
        }
    }

    if (notFirstCopy) {
        *val = CInt64_Add(*val, IRO_MakeLong(loop->induction->addConst));
        ChangeInductionReference(first, last, *val, loop);
    }
}

void IRO_LinearizeForLoopPostLoop(
    IRONode* fnode1, IRONode* fnode2, IROLoop* loop, IRONode* fnode3, UInt32 unrollFactor
) {
    IRONode* newfnode;
    IROLinear* newnd;
    SInt32 i;
    CInt64 val;

    newfnode = oalloc(sizeof(IRONode));
    memset(newfnode, 0, sizeof(IRONode));
    newfnode->index = IRO_NumNodes;
    IRO_NumNodes++;

    newnd = IRO_NewLinear(IROLinearNop);
    newnd->index = IRO_NumLinear++;
    newnd->next = NULL;
    newnd->flags |= IROLF_StartsNode;

    newfnode->first = newfnode->last = newnd;

    newfnode->nextnode = fnode3->nextnode;
    fnode3->nextnode = newfnode;

    newnd->next = fnode3->last->next;
    fnode3->last->next = newnd;

    val = cint64_zero;
    for (i = 0; (UInt32)i < unrollFactor; i++) {
        IRO_IterateForLoopBody(
            fnode2, fnode1, loop, newfnode->last, loop->induction->addConst, &val, i > 0
        );
    }
    UpdateInductionIncrement(loop, unrollFactor, newfnode->last);
}

static UInt32 UnrollForLoop(
    IRONode* header, IRONode* fnode2, IRONode* fnode3, IROLoop* loop, UInt32 unrollFactor
) {
    IROLinear* saveTail2;
    IROLinear* earlyLoopExitTest;
    IROLinear* origIterationCount;
    IROLinear* lastlabelnode;
    IROLinear* saveHead1;
    IROLinear* unrolledBodyEntryTest;
    IROLinear* saveHead2;
    IROLinear* updIndInc;
    IROLinear* newFinalValue;
    CLabel* label2;
    CLabel* earlyLoopExitTestLabel;
    CLabel* lastlabel;
    CLabel* label;
    IROLinear* labelNd;
    IROLinear* gotoNd;
    IROLinear* ndCopy;
    IROLinear* labelnode;
    IROLinear* saveTail3;
    IROLinear* loopExitTest;
    IROLinear* lastnd;
    IROLinear* labelNd2;
    IROLinear* saveTail4;
    IROLinear* labelNd3;
    IROLinear* scan;
    IRONode* oldNext;
    IRONode* newfnode1;
    IRONode* newfnode2;
    IRONode* newfnode3;
    IRONode* newfnode4;
    IRONode* newfnode5;
    IRONode* newfnode6;
    SInt32 i;
    IROList list;
    int isConstant;
    UInt32 needOrigLoop = FALSE;
    UInt32 needUnrollBodyTest = FALSE;
    UInt32 resetUnrolledFinalValue = FALSE;
    SInt32 leftOver;
    CInt64 iterCount;
    CInt64 val;

    labelnode = IRO_FindLabelNode(fnode2->last->u.label.label, fnode2->last);
    lastlabel = IRO_NewLabel();

    IRO_InitList(&list);
    IRO_DuplicateExprRange(labelnode->next, LoopNode->last->u.label.cond, &list);
    IRO_DuplicateExpr(LoopNode->last->u.label.cond, &list);
    IRO_Paste(list.head, list.tail, fnode2->last);
    lastlabelnode = list.tail;

    IRO_InitList(&list);
    earlyLoopExitTest = BuildEarlyLoopExitTest(LoopNode->last->type, &list);
    earlyLoopExitTestLabel = IRO_NewLabel();
    earlyLoopExitTest->u.label.label = earlyLoopExitTestLabel;
    earlyLoopExitTest->u.label.cond = lastlabelnode;
    earlyLoopExitTest->u.label.cond->flags |= IROLF_Reffed;
    earlyLoopExitTest->rtype = LoopNode->last->rtype;
    IRO_Paste(list.head, list.tail, fnode2->last);

    isConstant = IsIterationCountConstant(loop, &iterCount);
    needOrigLoop = TRUE;
    needUnrollBodyTest = TRUE;
    resetUnrolledFinalValue = FALSE;
    if (isConstant) {
        IRO_TestConstantIterationCount(
            loop,
            &iterCount,
            1,
            &unrollFactor,
            &leftOver,
            &needOrigLoop,
            &needUnrollBodyTest,
            &resetUnrolledFinalValue
        );
    }

    IRO_InitList(&list);
    origIterationCount = BuildOrigIterationCount(&list, loop);
    IRO_Paste(list.head, list.tail, fnode2->last);
    saveHead1 = list.head;

    IRO_InitList(&list);
    newFinalValue = BuildNewFinalvalue(origIterationCount, unrollFactor, &list, loop);
    IRO_Paste(list.head, list.tail, fnode2->last);

    IRO_InitList(&list);
    BuildUnrolledBodyEntryTest(&list, origIterationCount, unrollFactor, lastlabel);
    IRO_Paste(list.head, list.tail, fnode2->last);
    unrolledBodyEntryTest = list.tail;

    label = IRO_NewLabel();
    IRO_InitList(&list);
    gotoNd = IRO_NewLinear(IROLinearOp1Arg);
    gotoNd->index = ++IRO_NumLinear;
    gotoNd->type = IROLinearGoto;
    gotoNd->u.label.label = label;
    IRO_AddToList(gotoNd, &list);
    IRO_Paste(list.head, list.tail, fnode2->last);

    IRO_InitList(&list);
    label2 = BuildLabel(&list);
    IRO_Paste(list.head, list.tail, fnode2->last);
    saveHead2 = list.head;

    val = cint64_zero;
    for (i = 0; (UInt32)i < unrollFactor; i++) {
        IRO_IterateForLoopBody(
            fnode3, header, loop, fnode2->last, loop->induction->addConst, &val, i > 0
        );
    }
    updIndInc = UpdateInductionIncrement(loop, unrollFactor, fnode2->last);

    IRO_InitList(&list);
    labelNd = IRO_NewLinear(IROLinearLabel);
    labelNd->index = IRO_NumLinear++;
    labelNd->u.label.label = label;
    labelNd->flags |= IROLF_StartsNode;
    IRO_AddToList(labelNd, &list);
    IRO_Paste(list.head, list.tail, fnode2->last);

    IRO_InitList(&list);

    IRO_DuplicateExpr(LoopNode->last->u.label.cond->u.diadic.left, &list);
    saveTail2 = list.tail;

    if (resetUnrolledFinalValue) {
        IRO_DuplicateExpr(loop->cond->u.diadic.right, &list);
    } else {
        IRO_DuplicateExpr(newFinalValue, &list);
    }

    ndCopy = IRO_NewLinear(LoopNode->last->u.label.cond->type);
    *ndCopy = *LoopNode->last->u.label.cond;
    ndCopy->index = ++IRO_NumLinear;
    ndCopy->next = NULL;
    ndCopy->expr = NULL;
    ndCopy->u.diadic.left = saveTail2;
    ndCopy->u.diadic.right = list.tail;
    IRO_AddToList(ndCopy, &list);

    IRO_Paste(list.head, list.tail, fnode2->last);
    saveTail3 = list.tail;

    IRO_InitList(&list);
    loopExitTest = BuildLoopExitTest(LoopNode->last->type, &list);
    loopExitTest->u.label.label = label2;
    loopExitTest->u.label.cond = saveTail3;
    loopExitTest->u.label.cond->flags |= IROLF_Reffed;
    loopExitTest->rtype = LoopNode->last->rtype;
    IRO_Paste(list.head, list.tail, fnode2->last);
    saveTail4 = list.tail;

    IRO_InitList(&list);
    labelNd2 = IRO_NewLinear(IROLinearLabel);
    labelNd2->index = IRO_NumLinear++;
    labelNd2->u.label.label = lastlabel;
    labelNd2->flags |= IROLF_StartsNode;
    IRO_AddToList(labelNd2, &list);
    IRO_Paste(list.head, list.tail, fnode2->last);

    lastnd = fnode2->last;
    oldNext = fnode2->nextnode;
    fnode2->last = earlyLoopExitTest;

    newfnode1 = IRO_NewFlowGraphNode();
    newfnode1->first = saveHead1;
    newfnode1->last = unrolledBodyEntryTest;
    fnode2->nextnode = newfnode1;

    newfnode2 = IRO_NewFlowGraphNode();
    newfnode2->first = gotoNd;
    newfnode2->last = gotoNd;
    newfnode1->nextnode = newfnode2;

    newfnode3 = IRO_NewFlowGraphNode();
    newfnode3->first = saveHead2;
    newfnode3->last = updIndInc;

    saveHead2->u.label.label->stmt = (Statement*)newfnode3;
    if (newfnode2) {
        newfnode2->nextnode = newfnode3;
    } else {
        newfnode1->nextnode = newfnode3;
    }

    newfnode4 = IRO_NewFlowGraphNode();
    newfnode4->first = labelNd;
    newfnode4->last = saveTail4;
    labelNd->u.label.label->stmt = (Statement*)newfnode4;
    newfnode3->nextnode = newfnode4;

    newfnode5 = IRO_NewFlowGraphNode();
    newfnode5->first = labelNd2;
    newfnode5->last = lastnd;
    newfnode4->nextnode = newfnode5;
    newfnode5->nextnode = oldNext;

    newfnode6 = oalloc(sizeof(IRONode));
    memset(newfnode6, 0, sizeof(IRONode));
    newfnode6->index = IRO_NumNodes;
    IRO_NumNodes++;

    labelNd3 = IRO_NewLinear(IROLinearLabel);
    labelNd3->index = IRO_NumLinear++;
    labelNd3->next = NULL;
    labelNd3->u.label.label = earlyLoopExitTestLabel;
    labelNd3->flags |= IROLF_StartsNode;
    earlyLoopExitTestLabel->stmt = (Statement*)newfnode6;

    newfnode6->first = labelNd3;
    newfnode6->last = labelNd3;

    labelNd3->next = LoopNode->last->next;
    LoopNode->last->next = labelNd3;

    newfnode6->nextnode = LoopNode->nextnode;
    LoopNode->nextnode = newfnode6;

    if (!needOrigLoop) {
        NoOpBlock(newfnode5);
        NoOpBlock(header);
        NoOpBlock(fnode3);
        NoOpBlock(loop->induction->fnode);
        IRO_NopOut(newfnode1->last->u.label.cond);
        newfnode1->last->type = IROLinearNop;
    }

    if (!needUnrollBodyTest) {
        IRO_NopOut(earlyLoopExitTest->u.label.cond);
        earlyLoopExitTest->type = IROLinearNop;

        IRO_NopOut(newfnode4->last->u.label.cond);
        newfnode4->last->type = IROLinearNop;

        if (newfnode2) {
            newfnode2->last->type = IROLinearNop;
        }

        for (scan = newfnode1->first; scan; scan = scan->next) {
            if (!(scan->flags & IROLF_Reffed)) {
                IRO_NopOut(scan);
            }
            if (scan == newfnode1->last) {
                break;
            }
        }
    }

    return 1;
}

static UInt32 UnrollStandardLoop(IRONode* header, IRONode* fnode2, IRONode* fnode3, int count) {
    IROLoop* loop;

    ConditionalHeaderAtBottom = TRUE;
    loop = ExtractLoopInfo(header);
    loop->preheader = fnode2;
    loop->body = fnode3;
    FindAssignmenttoInductionVar(loop, fnode2);

    if (!IsLoopUnrollable(loop)) {
        IRO_Dump(
            "LoopUnroll:loop with header %d not unrolled because IsLoopUnrollable failed\n",
            header->index
        );
        return 0;
    }

    if (loop->flags & LP_WHILE_LOOP) {
        return UnrollWhileLoop(header, fnode2, fnode3, loop, count);
    } else {
        return UnrollForLoop(header, fnode2, fnode3, loop, count);
    }
}

static void LoopUnroll(int count, IRONode* header) {
    VarRecord* var;
    IRONode* node;
    IRONode* next;
    UInt16 i;
    IRONode* prevpred;
    IRONode* prevsucc;
    int foundpred;
    UInt32 predcount;
    UInt32 success = FALSE;

    LoopNode = header;
    FindMustReach();

    for (var = IRO_FirstVar; var; var = var->next) {
        var->inductionState = TRUE;
    }

    ComputeLoopKills();
    ComputeLoopInvariance();
    ComputeLoopInduction();

    LoopNode = header;
    ConditionalHeaderAtBottom = FALSE;

    prevpred = NULL;
    foundpred = FALSE;
    for (i = 0; i < LoopNode->numpred; i++) {
        node = IRO_NodeTable[LoopNode->pred[i]];
        if (!Bv_IsBitSet(node->index, InLoop)) {
            foundpred = TRUE;
            if (node->nextnode == header) {
                CError_ASSERT(2101, !prevpred || node == prevpred);
                prevpred = node;
            }
        }
    }

    if (!foundpred) {
        IRO_Dump("No predecessor outside the loop\n");
        return;
    }

    if (LoopNode->last->type == IROLinearIf || LoopNode->last->type == IROLinearIfNot) {
        next = LoopNode->nextnode;
        if (next && !Bv_IsBitSet(next->index, InLoop)) {
            prevsucc = NULL;
            for (i = 0; i < LoopNode->numsucc; i++) {
                node = IRO_NodeTable[LoopNode->succ[i]];
                if (Bv_IsBitSet(node->index, InLoop)) {
                    CError_ASSERT(2159, !prevsucc);
                    prevsucc = node;
                }
            }

            prevpred = NULL;
            predcount = 0;
            for (i = 0; i < LoopNode->numpred; i++) {
                node = IRO_NodeTable[LoopNode->pred[i]];
                if (!Bv_IsBitSet(node->index, InLoop)) {
                    predcount++;
                    prevpred = node;
                }
            }

            if (predcount == 1 &&
                prevpred->last->type == IROLinearGoto &&
                prevpred->nextnode == prevsucc &&
                prevsucc != LoopNode) {
                success = UnrollStandardLoop(header, prevpred, prevsucc, count);
            }
        }
    } else {
        IRO_Dump(" LoopUnroll:Loop with header = %d is not a conditional loop\n", header->index);
    }

    if (success) {
        IRO_NodeTable = oalloc(sizeof(IRONode*) * IRO_NumNodes);
        memset(IRO_NodeTable, 0, sizeof(IRONode*) * IRO_NumNodes);
        for (node = IRO_FirstNode; node; node = node->nextnode) {
            IRO_NodeTable[node->index] = node;
        }
        IRO_ComputeSuccPred();
        IRO_ComputeDom();
        if (success) {
            IRO_Dump(" LoopUnroll:Loop with header = %d Unrolled\n", header->index);
        }
    }
}

static int IsLoopUnrollable(IROLoop* loop) {
    CInt64 iterCount;

    if (loop->flags & LP_LOOP_HAS_ASM) {
        IRO_Dump("IsLoopUnrollable:No due to LP_LOOP_HAS_ASM \n");
        return FALSE;
    }
    if (loop->flags & LP_IFEXPR_NON_CANONICAL) {
        IRO_Dump("IsLoopUnrollable:No due to LP_IFEXPR_NON_CANONICAL \n");
        return FALSE;
    }
    if (loop->flags & LP_LOOP_HAS_CALLS) {
        IRO_Dump("IsLoopUnrollable:No due to LP_LOOP_HAS_CALLS \n");
        return FALSE;
    }
    if (loop->flags & LP_LOOP_HAS_CNTRLFLOW) {
        IRO_Dump("IsLoopUnrollable:No due to LP_LOOP_HAS_CNTRLFLOW \n");
        return FALSE;
    }
    if (loop->flags & LP_INDUCTION_NOT_FOUND) {
        IRO_Dump("IsLoopUnrollable:No due to LP_INDUCTION_NOT_FOUND \n");
        return FALSE;
    }
    if (loop->flags & LP_LOOP_HDR_HAS_SIDEEFFECTS) {
        IRO_Dump("IsLoopUnrollable:No due to LP_LOOP_HDR_HAS_SIDEEFFECTS \n");
        return FALSE;
    }
    if (!(loop->flags & LP_HEADER_FOLLOWS_UPDATE)) {
        IRO_Dump("IsLoopUnrollable:No because header does not follow induction update \n");
        return FALSE;
    }

    if (!(loop->flags & LP_WHILE_LOOP)) {
        IROLinear* upperBound = loop->cond->u.diadic.right;
        if (!IRO_IsIntConstant(upperBound) && !(upperBound->flags & IROLF_LoopInvariant)) {
            IRO_Dump("IsLoopUnrollable:No because Loop Upper Bound is Variant in the loop\n");
            return FALSE;
        }
        if (!loop->init) {
            IRO_Dump(
                "IsLoopUnrollable:No because there is no initialization of loop index in PreHeader\n"
            );
            return FALSE;
        }
        if (!IRO_IsVariable(loop->init->u.diadic.left)) {
            IRO_Dump(
                "IsLoopUnrollable:No because initial value of induction stored thru pointer\n"
            );
            return FALSE;
        }

        if (!is_unsigned(loop->init->rtype)) {
            if (IRO_IsIntConstant(loop->init->u.diadic.right)) {
                if (!CInt64_GreaterEqual(
                        loop->init->u.diadic.right->u.node->data.intval, cint64_zero
                    )) {
                    IRO_Dump(
                        "IsLoopUnrollable:No because initial value of induction is signed but init < 0\n"
                    );
                    return FALSE;
                }
            } else if (IsIterationCountConstant(loop, &iterCount)) {
                IRO_Dump("IsLoopUnrollable:Yes, the limits substract out to be constants\n");
            } else {
                IRO_Dump(
                    "IsLoopUnrollable:No because initial value of induction is signed and not constant\n"
                );
                return FALSE;
            }
        }

        if (!(loop->flags & LP_LOOP_STEP_ISADD)) {
            IRO_Dump(
                "IsLoopUnrollable:No because LP_LOOP_STEP_ISADD  is not set i.e induciton is not updated by 1\n"
            );
            return FALSE;
        }

    } else {
        if (!is_unsigned(loop->cond->u.diadic.left->rtype)) {
            IRO_Dump("IsLoopUnrollable:No because the while loop induction is signed\n");
            return FALSE;
        }
        if (!(loop->flags & LP_STEP_IS_NEG_ONE)) {
            IRO_Dump(
                "IsLoopUnrollable:No because the while loop operator is not of decrement form\n"
            );
            return FALSE;
        }
    }

    if (loop->weight > copts.unrollinstrfactor) {
        IRO_Dump("IsLoopUnrollable:No because loop size greater than threshold\n");
        return FALSE;
    }

    return TRUE;
}

IROLinear* BuildEarlyLoopExitTest(IROLinearType type, IROList* list) {
    IROLinear* nd = IRO_NewLinear(IROLinearOp1Arg);
    nd->index = ++IRO_NumLinear;
    if (type == IROLinearIf) {
        nd->type = IROLinearIfNot;
    } else {
        nd->type = IROLinearIf;
    }
    IRO_AddToList(nd, list);
    return nd;
}

IROLinear* BuildLoopExitTest(IROLinearType type, IROList* list) {
    IROLinear* nd = IRO_NewLinear(IROLinearOp1Arg);
    nd->index = ++IRO_NumLinear;
    nd->type = type;
    IRO_AddToList(nd, list);
    return nd;
}

int IsIterationCountConstant(IROLoop* loop, CInt64* pval) {
    IROLinear* lowerBound;
    IROLinear* upperBound;
    Type* type;
    int isUnsigned;
    IROAddrRecord* lowerRec;
    IROAddrRecord* upperRec;
    CInt64 lowerval;
    CInt64 upperval;
    CInt64 incval;
    CInt64 negOne;

    lowerBound = loop->init->u.diadic.right;
    if (loop->flags & LP_INDUCTION_AT_LEFT) {
        upperBound = loop->cond->u.diadic.right;
        type = loop->cond->u.diadic.right->rtype;
    } else {
        upperBound = loop->cond->u.diadic.left;
        type = loop->cond->u.diadic.left->rtype;
    }

    isUnsigned = is_unsigned(type);

    if (IRO_IsIntConstant(lowerBound) && IRO_IsIntConstant(upperBound)) {
        lowerval = lowerBound->u.node->data.intval;
        upperval = upperBound->u.node->data.intval;
        if (isUnsigned) {
            if (CInt64_LessEqualU(upperval, lowerval)) {
                return FALSE;
            }
        } else {
            if (CInt64_LessEqual(upperval, lowerval)) {
                return FALSE;
            }
        }

        CInt64_SetLong(&incval, loop->induction->addConst);
        CInt64_SetLong(&negOne, -1);
        *pval = CInt64_Sub(upperval, lowerval);
        *pval = CInt64_Add(*pval, incval);

        if (IS_LINEAR_DIADIC(loop->cond, ELESS)) {
            *pval = CInt64_Add(*pval, negOne);
        }

        CError_ASSERT(2486, !CInt64_IsZero(&incval));

        if (isUnsigned) {
            *pval = CInt64_DivU(*pval, incval);
        } else {
            *pval = CInt64_Div(*pval, incval);
        }

        if (CInt64_Equal(*pval, cint64_zero)) {
            return FALSE;
        }

        if (isUnsigned) {
            CError_ASSERT(2508, !CInt64_LessEqualU(*pval, cint64_zero));
        } else {
            CError_ASSERT(2517, !CInt64_LessEqual(*pval, cint64_zero));
        }

        return TRUE;
    }

    lowerRec = IRO_InitAddrRecordPointer(lowerBound);
    upperRec = IRO_InitAddrRecordPointer(upperBound);

    if (IS_LINEAR_DIADIC(lowerBound, EADD)) {
        IRO_DecomposeAddressExpression(lowerBound, lowerRec);
    } else if (IRO_IsIntConstant(lowerBound)) {
        lowerRec->numInts++;
        IRO_AddElmToList(lowerBound, &lowerRec->ints);
        lowerRec->numObjRefs = 0;
        lowerRec->numMisc = 0;
    } else {
        lowerRec->numMisc++;
        IRO_AddElmToList(lowerBound, &lowerRec->misc);
        lowerRec->numObjRefs = 0;
        lowerRec->numInts = 0;
    }

    if (IS_LINEAR_DIADIC(upperBound, EADD)) {
        IRO_DecomposeAddressExpression(upperBound, upperRec);
    } else if (IRO_IsIntConstant(upperBound)) {
        upperRec->numInts++;
        IRO_AddElmToList(upperBound, &upperRec->ints);
        upperRec->numObjRefs = 0;
        upperRec->numMisc = 0;
    } else {
        upperRec->numMisc++;
        IRO_AddElmToList(upperBound, &upperRec->misc);
        upperRec->numObjRefs = 0;
        upperRec->numInts = 0;
    }

    if (IsDifferenceOfTermsConstant(lowerRec, upperRec, isUnsigned, pval)) {
        if (IS_LINEAR_DIADIC(loop->cond, ELESSEQU)) {
            *pval = CInt64_Add(*pval, cint64_one);
        }
        return TRUE;
    }

    return FALSE;
}

static int IsDifferenceOfTermsConstant(
    IROAddrRecord* lowerRec, IROAddrRecord* upperRec, int isUnsigned, CInt64* pval
) {
    CInt64 upperval;
    CInt64 lowerval;
    CInt64 diff;
    UInt32 i;
    IROElmList* el;
    IROLinear* nd;

    if (upperRec->numObjRefs == lowerRec->numObjRefs && upperRec->numObjRefs != 0) {
        return FALSE;
    } else if (upperRec->numObjRefs != lowerRec->numObjRefs) {
        return FALSE;
    }

    if (upperRec->numMisc == lowerRec->numMisc && upperRec->numMisc != 0) {
        for (i = 0; i < upperRec->numMisc; i++) {
            if (!IRO_ExprsSame(lowerRec->misc->element, upperRec->misc->element)) {
                return FALSE;
            }
        }
    } else if (upperRec->numMisc != lowerRec->numMisc) {
        return FALSE;
    }

    upperval = cint64_zero;
    for (el = upperRec->ints; el; el = el->next) {
        nd = el->element;
        upperval = CMach_CalcIntDiadic(nd->rtype, upperval, '+', nd->u.node->data.intval);
    }

    lowerval = cint64_zero;
    for (el = lowerRec->ints; el; el = el->next) {
        nd = el->element;
        lowerval = CMach_CalcIntDiadic(nd->rtype, lowerval, '+', nd->u.node->data.intval);
    }

    if (CInt64_Equal(upperval, lowerval)) {
        return FALSE;
    }

    if (CInt64_Greater(upperval, lowerval)) {
        diff = CInt64_Sub(upperval, lowerval);
        *pval = diff;
        return TRUE;
    } else {
        return FALSE;
    }
}

void NoOpBlock(IRONode* fnode) {
    IROLinear *last, *scan;

    for (scan = fnode->first, last = fnode->last; scan; scan = scan->next) {
        scan->type = IROLinearNop;
        if (scan == last) {
            break;
        }
    }
}

void IRO_TestConstantIterationCount(
    IROLoop* loop,
    CInt64* iterCount,
    SInt32 vectorStride,
    UInt32* unrollFactor,
    SInt32* leftOver,
    UInt32* needOrigLoop,
    UInt32* needUnrollBodyTest,
    UInt32* resetUnrolledFinalValue
) {
    UInt32 isUnsigned;
    CInt64 val;
    CInt64 strideRemainder;
    CInt64 mod;
    CInt64 factor;
    CInt64 loopvar3;
    CInt64 loopvar1;
    CInt64 loopvar2;
    CInt64 strideVal;

    CError_ASSERT(2737, *unrollFactor);

    isUnsigned = is_unsigned(
        (loop->flags & LP_INDUCTION_AT_LEFT) ? loop->cond->u.diadic.right->rtype
                                             : loop->cond->u.diadic.left->rtype
    );

    CError_ASSERT(2756, vectorStride);

    strideVal = IRO_MakeLong(vectorStride);
    if (isUnsigned ? CInt64_LessU(*iterCount, strideVal) : CInt64_Less(*iterCount, strideVal)) {
        *needOrigLoop = TRUE;
        *needUnrollBodyTest = FALSE;
        *unrollFactor = 0;
        *leftOver = CInt64_GetULong(iterCount);
    } else {
        switch (vectorStride) {
            case 1:
                val = *iterCount;
                break;
            case 2:
                val = CInt64_ShrU(*iterCount, cint64_one);
                break;
            case 4:
                val = CInt64_ShrU(*iterCount, IRO_MakeLong(2));
                break;
            case 8:
                val = CInt64_ShrU(*iterCount, IRO_MakeLong(3));
                break;
            case 16:
                val = CInt64_ShrU(*iterCount, IRO_MakeLong(4));
                break;
            default:
                val = CInt64_Div(*iterCount, strideVal);
        }

        if (CInt64_LessU(val, IRO_MakeLong(*unrollFactor))) {
            *unrollFactor = CInt64_GetULong(&val);
        }

        CInt64_SetLong(&factor, *unrollFactor);
        switch (vectorStride) {
            case 1:
                strideRemainder = cint64_zero;
                break;
            case 2:
                strideRemainder = CInt64_And(*iterCount, cint64_one);
                break;
            case 4:
                strideRemainder = CInt64_And(*iterCount, IRO_MakeLong(3));
                break;
            case 8:
                strideRemainder = CInt64_And(*iterCount, IRO_MakeLong(7));
                break;
            case 16:
                strideRemainder = CInt64_And(*iterCount, IRO_MakeLong(15));
                break;
            default:
                strideRemainder = CInt64_Mod(*iterCount, strideVal);
        }

        if (CInt64_LessEqualU(val, IRO_MakeLong(8))) {
            *needUnrollBodyTest = vectorStride > 1;
            *unrollFactor = CInt64_GetULong(&val);
            *leftOver = CInt64_GetULong(&strideRemainder);
            *needOrigLoop = *leftOver != 0;
            *resetUnrolledFinalValue = !(*needOrigLoop && *needUnrollBodyTest);
        } else {
            loopvar1 = IRO_MakeLong(0x7FFFFFFF);
            loopvar2 = IRO_MakeLong(0x7FFFFFFF);
            do {
                mod = CInt64_Mod(val, factor);
                loopvar3 = CInt64_Add(CInt64_Mul(mod, strideVal), strideRemainder);
                if (CInt64_Less(loopvar3, loopvar2)) {
                    loopvar2 = loopvar3;
                    loopvar1 = factor;
                }
                if (vectorStride > 1) {
                    break;
                }
                factor = CInt64_Add(factor, cint64_negone);
            } while (CInt64_GreaterEqualU(CInt64_Mul(factor, factor), val));

            *unrollFactor = CInt64_GetULong(&loopvar1);
            *leftOver = CInt64_GetULong(&loopvar2);
            *needOrigLoop = *leftOver != 0;
            *needUnrollBodyTest = CInt64_Less(loopvar1, val) || vectorStride > 1;
            *resetUnrolledFinalValue = !(*needOrigLoop && *needUnrollBodyTest);
        }
    }

    IRO_Dump(
        "---- IterCount = %d, VectorStride = %d, UnrollFactor = %d, LeftOver = %d,\n"
        "\tNeedOrigLoop = %d, NeedUnrollBodyTest = %d, ResetUnrolledFinalValue = %d\n",
        CInt64_GetULong(iterCount),
        vectorStride,
        *unrollFactor,
        *leftOver,
        *needOrigLoop,
        *needUnrollBodyTest,
        *resetUnrolledFinalValue
    );
}

IROLinear* BuildOrigIterationCount(IROList* list, IROLoop* loop) {
    Type* type;
    IROLinear* ass;
    Object* tempobj;
    IROLinear* finalCount;
    IROLinear* prevSum;
    IROLinear* negone;
    ENode* expr;
    IROLinear* iterCount;
    IROLinear* lowerBound;
    IROLinear* upperBound;
    IROLinear* stepCopy;
    Boolean isZeroBase;
    IROLinear* sum;
    IROLinear* divisor;
    int powval;

    isZeroBase = FALSE;
    lowerBound = loop->init->u.diadic.right;
    if (IRO_IsIntConstant(lowerBound) &&
        CInt64_Equal(lowerBound->u.node->data.intval, cint64_zero)) {
        isZeroBase = TRUE;
    }

    if (!isZeroBase) {
        lowerBound = IRO_DuplicateExpr(lowerBound, list);
    }

    if (loop->flags & LP_INDUCTION_AT_LEFT) {
        upperBound = IRO_DuplicateExpr(loop->cond->u.diadic.right, list);
        type = loop->cond->u.diadic.right->rtype;
    } else {
        upperBound = IRO_DuplicateExpr(loop->cond->u.diadic.left, list);
        type = loop->cond->u.diadic.left->rtype;
    }

    CError_ASSERT(2924, loop->induction);
    CError_ASSERT(2929, loop->induction->addConst);

    divisor = IRO_NewLinear(IROLinearOperand);
    divisor->index = ++IRO_NumLinear;
    divisor->rtype = type;
    expr = IRO_NewENode(EINTCONST);
    expr->rtype = type;
    CInt64_SetLong(&expr->data.intval, loop->induction->addConst);
    divisor->u.node = expr;

    if (isZeroBase) {
        iterCount = upperBound;
    } else {
        iterCount = IRO_NewLinear(IROLinearOp2Arg);
        iterCount->index = ++IRO_NumLinear;
        iterCount->nodetype = ESUB;
        iterCount->u.diadic.left = upperBound;
        iterCount->u.diadic.right = lowerBound;
        iterCount->rtype = type;
        IRO_AddToList(iterCount, list);
    }

    stepCopy = IRO_DuplicateExpr(divisor, list);

    sum = IRO_NewLinear(IROLinearOp2Arg);
    sum->index = ++IRO_NumLinear;
    sum->nodetype = EADD;
    sum->u.diadic.left = iterCount;
    sum->u.diadic.right = stepCopy;
    sum->rtype = type;
    IRO_AddToList(sum, list);

    if (loop->cond->type == IROLinearOp2Arg && loop->cond->nodetype == ELESS) {
        prevSum = sum;

        negone = IRO_NewLinear(IROLinearOperand);
        negone->index = ++IRO_NumLinear;
        negone->rtype = type;
        expr = IRO_NewENode(EINTCONST);
        expr->rtype = type;
        CInt64_SetLong(&expr->data.intval, -1);
        negone->u.node = expr;
        IRO_AddToList(negone, list);

        sum = IRO_NewLinear(IROLinearOp2Arg);
        sum->index = ++IRO_NumLinear;
        sum->nodetype = EADD;
        sum->u.diadic.left = prevSum;
        sum->u.diadic.right = negone;
        sum->rtype = type;
        IRO_AddToList(sum, list);
    }

    if (CInt64_Equal(divisor->u.node->data.intval, cint64_one)) {
        finalCount = sum;
    } else {
        if (divisor->rtype->size <= 4 &&
            IS_TYPE_INT(divisor->rtype) &&
            IRO_IsPow2(divisor, &powval)) {
            finalCount = IRO_NewLinear(IROLinearOp2Arg);
            finalCount->index = ++IRO_NumLinear;
            finalCount->nodetype = ESHL;
            finalCount->u.diadic.left = sum;
            finalCount->u.diadic.right = divisor;
            CInt64_SetLong(&divisor->u.node->data.intval, powval);
            finalCount->rtype = type;
            IRO_AddToList(divisor, list);
            IRO_AddToList(finalCount, list);
        } else {
            finalCount = IRO_NewLinear(IROLinearOp2Arg);
            finalCount->index = ++IRO_NumLinear;
            finalCount->nodetype = EDIV;
            finalCount->u.diadic.left = sum;
            finalCount->u.diadic.right = divisor;
            finalCount->rtype = type;
            IRO_AddToList(divisor, list);
            IRO_AddToList(finalCount, list);
        }
    }

    tempobj = create_temp_object(type);
    IRO_FindVar(tempobj, TRUE, TRUE);

    ass = IRO_NewLinear(IROLinearOp2Arg);
    ass->index = ++IRO_NumLinear;
    ass->nodetype = EASS;
    ass->u.diadic.left = IRO_TempReference(tempobj, list);
    ass->u.diadic.left->flags |= IROLF_Assigned | IROLF_Ind;
    ass->u.diadic.left->u.monadic->flags |= IROLF_Assigned | IROLF_Ind;
    ass->u.diadic.right = finalCount;
    ass->u.diadic.right->flags |= IROLF_Reffed;
    ass->rtype = type;
    IRO_AddToList(ass, list);

    return ass->u.diadic.left;
}

static IROLinear* BuildOrigIterationCount_DoWhile(IROList* list, IROLoop* loop) {
    IROLinear* finalCount;
    IROLinear* count;
    IROLinear* ass;
    Type* type;
    Object* tempobj;
    ENode* expr;

    type = loop->cond->u.diadic.left->rtype;

    count = IRO_NewLinear(IROLinearOperand);
    count->index = ++IRO_NumLinear;
    expr = IRO_NewENode(EINTCONST);
    expr->rtype = type;
    expr->data.intval = cint64_one;
    count->u.node = expr;
    count->rtype = type;
    IRO_AddToList(count, list);
    count->flags |= IROLF_Reffed;

    finalCount = IRO_NewLinear(IROLinearOp2Arg);
    finalCount->index = ++IRO_NumLinear;
    finalCount->nodetype = EADD;
    finalCount->rtype = type;
    finalCount->u.diadic.left = IRO_DuplicateExpr(loop->cond->u.diadic.left, list);
    finalCount->u.diadic.left->flags |= IROLF_Reffed;
    finalCount->u.diadic.left->flags &= ~IROLF_Assigned;
    finalCount->u.diadic.left->u.monadic->flags &= ~IROLF_Assigned;
    finalCount->u.diadic.right = count;
    IRO_AddToList(finalCount, list);

    tempobj = create_temp_object(type);
    IRO_FindVar(tempobj, TRUE, TRUE);

    ass = IRO_NewLinear(IROLinearOp2Arg);
    ass->index = ++IRO_NumLinear;
    ass->nodetype = EASS;
    ass->u.diadic.left = IRO_TempReference(tempobj, list);
    ass->u.diadic.left->flags |= IROLF_Assigned | IROLF_Ind;
    ass->u.diadic.left->u.monadic->flags |= IROLF_Assigned | IROLF_Ind;
    ass->u.diadic.right = finalCount;
    ass->rtype = type;
    IRO_AddToList(ass, list);

    return ass->u.diadic.left;
}

IROLinear* BuildNewFinalvalue(
    IROLinear* iterCount, UInt32 unrollFactor, IROList* list, IROLoop* loop
) {
    IROLinear* dupbound;
    ENode* expr;
    IROLinear* ass;
    IROLinear* addvalue;
    IROLinear* sub;
    Type* type;
    Object* tempobj;

    type = iterCount->rtype;

    addvalue = IRO_NewLinear(IROLinearOperand);
    addvalue->index = ++IRO_NumLinear;
    addvalue->rtype = type;
    expr = IRO_NewENode(EINTCONST);
    expr->rtype = type;
    CInt64_SetLong(&expr->data.intval, loop->induction->addConst * unrollFactor);
    addvalue->u.node = expr;
    IRO_AddToList(addvalue, list);

    if (loop->flags & LP_INDUCTION_AT_LEFT) {
        dupbound = IRO_DuplicateExpr(loop->cond->u.diadic.right, list);
    } else {
        dupbound = IRO_DuplicateExpr(loop->cond->u.diadic.left, list);
    }

    sub = IRO_NewLinear(IROLinearOp2Arg);
    sub->index = ++IRO_NumLinear;
    sub->nodetype = ESUB;
    sub->u.diadic.left = dupbound;
    sub->u.diadic.right = addvalue;
    sub->rtype = type;
    IRO_AddToList(sub, list);

    tempobj = create_temp_object(type);
    IRO_FindVar(tempobj, TRUE, TRUE);

    ass = IRO_NewLinear(IROLinearOp2Arg);
    ass->index = ++IRO_NumLinear;
    ass->nodetype = EASS;
    ass->u.diadic.left = IRO_TempReference(tempobj, list);
    ass->u.diadic.left->flags |= IROLF_Assigned | IROLF_Ind;
    ass->u.diadic.left->u.monadic->flags |= IROLF_Assigned | IROLF_Ind;
    ass->u.diadic.right = sub;
    ass->u.diadic.right->flags |= IROLF_Reffed;
    ass->rtype = type;
    IRO_AddToList(ass, list);

    return ass->u.diadic.left;
}

static IROLinear* BuildPreAlignTemp(IROLoopInd* ind, UInt32 unrollFactor, IROList* list) {
    Type* type;
    IROLinear* indnd;
    IROLinear* factornd;
    IROLinear* constnd;
    IROLinear* div;
    IROLinear* add;
    IROLinear* mul;
    IROLinear* ass;
    Object* tempobj;
    ENode* expr;

    indnd = ind->nd;
    type = indnd->rtype;

    factornd = IRO_NewLinear(IROLinearOperand);
    factornd->index = ++IRO_NumLinear;
    factornd->rtype = type;
    expr = IRO_NewENode(EINTCONST);
    expr->rtype = type;
    CInt64_SetLong(&expr->data.intval, unrollFactor);
    factornd->u.node = expr;
    IRO_AddToList(factornd, list);

    if (indnd->type == IROLinearOp1Arg) {
        IRO_DuplicateExpr(indnd->u.monadic, list);
    } else {
        IRO_DuplicateExpr(indnd->u.diadic.left, list);
    }

    list->tail->flags &= ~IROLF_Assigned;
    list->tail->u.monadic->flags &= ~IROLF_Assigned;

    div = IRO_NewLinear(IROLinearOp2Arg);
    div->index = ++IRO_NumLinear;
    div->nodetype = EDIV;
    div->u.diadic.left = list->tail;
    div->u.diadic.right = factornd;
    div->rtype = type;
    IRO_AddToList(div, list);
    div->flags |= IROLF_Reffed;

    constnd = IRO_NewLinear(IROLinearOperand);
    constnd->index = ++IRO_NumLinear;
    expr = IRO_NewENode(EINTCONST);
    expr->rtype = type;
    expr->data.intval = cint64_one;
    constnd->u.node = expr;
    constnd->rtype = type;
    IRO_AddToList(constnd, list);
    constnd->flags |= IROLF_Reffed;

    add = IRO_NewLinear(IROLinearOp2Arg);
    add->index = ++IRO_NumLinear;
    add->nodetype = EADD;
    add->u.diadic.left = div;
    add->u.diadic.right = constnd;
    add->rtype = type;
    IRO_AddToList(add, list);
    add->flags |= IROLF_Reffed;

    IRO_DuplicateExpr(factornd, list);

    mul = IRO_NewLinear(IROLinearOp2Arg);
    mul->index = ++IRO_NumLinear;
    mul->nodetype = EMUL;
    mul->u.diadic.left = add;
    mul->u.diadic.right = list->tail;
    mul->rtype = type;
    IRO_AddToList(mul, list);
    mul->flags |= IROLF_Reffed;

    tempobj = create_temp_object(type);
    IRO_FindVar(tempobj, TRUE, TRUE);

    ass = IRO_NewLinear(IROLinearOp2Arg);
    ass->index = ++IRO_NumLinear;
    ass->nodetype = EASS;
    ass->u.diadic.left = IRO_TempReference(tempobj, list);
    ass->u.diadic.left->flags |= IROLF_Assigned | IROLF_Ind;
    ass->u.diadic.left->u.monadic->flags |= IROLF_Assigned | IROLF_Ind;
    ass->u.diadic.right = mul;
    ass->u.diadic.right->flags |= IROLF_Reffed;
    ass->rtype = type;
    IRO_AddToList(ass, list);

    return ass->u.diadic.left;
}

static IROLinear* BuildNewFinalvalue_DoWhile(
    IROLinear* iterCount, UInt32 unrollFactor, IROList* list, IROLoop* loop
) {
    IROLinear* addvalue;
    IROLinear* add;
    IROLinear* mul;
    IROLinear* ass;
    Type* type;
    Object* tempobj;
    ENode* expr;

    type = iterCount->rtype;

    addvalue = IRO_NewLinear(IROLinearOperand);
    addvalue->index = ++IRO_NumLinear;
    addvalue->rtype = type;
    expr = IRO_NewENode(EINTCONST);
    expr->rtype = type;
    CInt64_SetLong(&expr->data.intval, loop->induction->addConst);
    addvalue->u.node = expr;
    IRO_AddToList(addvalue, list);
    addvalue->flags |= IROLF_Reffed;

    mul = IRO_NewLinear(IROLinearOp2Arg);
    mul->index = ++IRO_NumLinear;
    mul->nodetype = EMUL;
    mul->u.diadic.left = IRO_DuplicateExpr(iterCount, list);
    mul->u.diadic.right = addvalue;
    mul->rtype = type;
    IRO_AddToList(mul, list);
    mul->flags |= IROLF_Reffed;
    mul->u.diadic.left->flags &= ~IROLF_Assigned;
    mul->u.diadic.left->u.diadic.left->flags &= ~IROLF_Assigned;

    if (loop->induction->nd->type == IROLinearOp1Arg) {
        IRO_DuplicateExpr(loop->induction->nd->u.monadic, list);
    } else {
        IRO_DuplicateExpr(loop->induction->nd->u.diadic.left, list);
    }
    list->tail->flags &= ~IROLF_Assigned;
    list->tail->u.diadic.left->flags &= ~IROLF_Assigned;

    add = IRO_NewLinear(IROLinearOp2Arg);
    add->index = ++IRO_NumLinear;
    add->nodetype = EADD;
    add->u.diadic.left = mul;
    add->u.diadic.right = list->tail;
    add->rtype = type;
    IRO_AddToList(add, list);
    add->flags |= IROLF_Reffed;

    tempobj = create_temp_object(type);
    IRO_FindVar(tempobj, TRUE, TRUE);

    ass = IRO_NewLinear(IROLinearOp2Arg);
    ass->index = ++IRO_NumLinear;
    ass->nodetype = EASS;
    ass->u.diadic.left = IRO_TempReference(tempobj, list);
    ass->u.diadic.left->flags |= IROLF_Assigned | IROLF_Ind;
    ass->u.diadic.left->u.monadic->flags |= IROLF_Assigned | IROLF_Ind;
    ass->u.diadic.right = add;
    ass->u.diadic.right->flags |= IROLF_Reffed;
    ass->rtype = type;
    IRO_AddToList(ass, list);

    return ass->u.diadic.left;
}

static IROLinear* BuildUnrolledFinalvalue_DoWhile(
    IROLinear* iterCount, UInt32 unrollFactor, IROList* list, IROLoop* loop
) {
    IROLinear* mul;
    IROLinear* addvalue;
    Type* type;
    IROLinear* sub;
    IROLinear* add;
    IROLinear* ass;
    IROLinear* addvalue_mult;
    Object* tempobj;
    ENode* expr;

    type = iterCount->rtype;

    addvalue_mult = IRO_NewLinear(IROLinearOperand);
    addvalue_mult->index = ++IRO_NumLinear;
    addvalue_mult->rtype = type;
    expr = IRO_NewENode(EINTCONST);
    expr->rtype = type;
    CInt64_SetLong(&expr->data.intval, loop->induction->addConst * unrollFactor);
    addvalue_mult->u.node = expr;
    IRO_AddToList(addvalue_mult, list);
    addvalue_mult->flags |= IROLF_Reffed;

    addvalue = IRO_NewLinear(IROLinearOperand);
    addvalue->index = ++IRO_NumLinear;
    addvalue->rtype = type;
    expr = IRO_NewENode(EINTCONST);
    expr->rtype = type;
    CInt64_SetLong(&expr->data.intval, loop->induction->addConst);
    addvalue->u.node = expr;
    IRO_AddToList(addvalue, list);
    addvalue->flags |= IROLF_Reffed;

    mul = IRO_NewLinear(IROLinearOp2Arg);
    mul->index = ++IRO_NumLinear;
    mul->nodetype = EMUL;
    mul->u.diadic.left = IRO_DuplicateExpr(iterCount, list);
    mul->u.diadic.right = addvalue;
    mul->rtype = type;
    IRO_AddToList(mul, list);
    mul->flags |= IROLF_Reffed;
    mul->u.diadic.left->flags &= ~IROLF_Assigned;
    mul->u.diadic.left->u.diadic.left->flags &= ~IROLF_Assigned;

    sub = IRO_NewLinear(IROLinearOp2Arg);
    sub->index = ++IRO_NumLinear;
    sub->nodetype = ESUB;
    sub->u.diadic.left = mul;
    sub->u.diadic.right = addvalue_mult;
    sub->rtype = type;
    IRO_AddToList(sub, list);
    sub->flags |= IROLF_Reffed;

    if (loop->induction->nd->type == IROLinearOp1Arg) {
        IRO_DuplicateExpr(loop->induction->nd->u.monadic, list);
    } else {
        IRO_DuplicateExpr(loop->induction->nd->u.diadic.left, list);
    }
    list->tail->flags &= ~IROLF_Assigned;
    list->tail->u.diadic.left->flags &= ~IROLF_Assigned;

    add = IRO_NewLinear(IROLinearOp2Arg);
    add->index = ++IRO_NumLinear;
    add->nodetype = EADD;
    add->u.diadic.left = sub;
    add->u.diadic.right = list->tail;
    add->rtype = type;
    IRO_AddToList(add, list);
    add->flags |= IROLF_Reffed;

    tempobj = create_temp_object(type);
    IRO_FindVar(tempobj, TRUE, TRUE);

    ass = IRO_NewLinear(IROLinearOp2Arg);
    ass->index = ++IRO_NumLinear;
    ass->nodetype = EASS;
    ass->u.diadic.left = IRO_TempReference(tempobj, list);
    ass->u.diadic.left->flags |= IROLF_Assigned | IROLF_Ind;
    ass->u.diadic.left->u.monadic->flags |= IROLF_Assigned | IROLF_Ind;
    ass->u.diadic.right = add;
    ass->u.diadic.right->flags |= IROLF_Reffed;
    ass->rtype = type;
    IRO_AddToList(ass, list);

    return ass->u.diadic.left;
}

void BuildUnrolledBodyEntryTest(
    IROList* list, IROLinear* iterCount, UInt32 unrollFactor, CLabel* label
) {
    IROLinear* value;
    IROLinear* countCopy;
    IROLinear* ifnot;
    IROLinear* comp;
    Type* type;
    ENode* expr;

    type = iterCount->rtype;

    value = IRO_NewLinear(IROLinearOperand);
    value->index = ++IRO_NumLinear;
    value->rtype = type;
    expr = IRO_NewENode(EINTCONST);
    expr->rtype = type;
    CInt64_SetLong(&expr->data.intval, unrollFactor);
    value->u.node = expr;
    IRO_AddToList(value, list);

    countCopy = IRO_DuplicateExpr(iterCount, list);

    comp = IRO_NewLinear(IROLinearOp2Arg);
    comp->index = ++IRO_NumLinear;
    comp->nodetype = EGREATER;
    comp->u.diadic.left = countCopy;
    comp->u.diadic.right = value;
    comp->u.diadic.right->flags |= IROLF_Reffed;
    comp->rtype = type;
    IRO_AddToList(comp, list);

    ifnot = IRO_NewLinear(IROLinearOp1Arg);
    ifnot->index = ++IRO_NumLinear;
    ifnot->type = IROLinearIfNot;
    ifnot->u.label.cond = comp;
    ifnot->u.label.cond->flags |= IROLF_Reffed;
    ifnot->rtype = type;
    ifnot->u.label.label = label;
    IRO_AddToList(ifnot, list);
}

void ChangeInductionReference(IROLinear* first, IROLinear* last, CInt64 val, IROLoop* loop) {
    IROLinear* nd;
    IROLinear* value;
    IROLinear* add;
    UInt32 isUnsigned;
    IROLinear* father;
    Boolean typconWidens;
    IROLinear* father2;
    IROLinear* father3;
    Type* fatherType;
    UInt32 foldedIntoAdd;
    Object* varobj;
    IROLinear* next;
    ENode* expr;
    Type* type;

    CInt64 scale;
    CInt64 product;
    IROList list;

    type = loop->induction->nd->rtype;
    isUnsigned = is_unsigned(type);

    for (nd = first; nd; nd = next) {
        next = nd->next;

        varobj = IRO_IsVariable(nd);
        if (varobj && loop->induction->var->object == varobj) {
            value = IRO_NewLinear(IROLinearOperand);
            value->index = ++IRO_NumLinear;
            value->rtype = type;
            expr = IRO_NewENode(EINTCONST);
            expr->rtype = type;
            expr->data.intval = val;
            value->u.node = expr;

            add = IRO_NewLinear(IROLinearOp2Arg);
            add->index = ++IRO_NumLinear;
            add->nodetype = EADD;
            add->rtype = type;

            father = IRO_LocateFather(nd);
            typconWidens = TRUE;
            if (father && IS_LINEAR_MONADIC(father, ETYPCON)) {
                fatherType = father->rtype;
                father = IRO_LocateFather(father);
                if (fatherType->type != nd->rtype->type || fatherType->size < nd->rtype->size) {
                    typconWidens = FALSE;
                }
            }

            foldedIntoAdd = 0;
            if (typconWidens &&
                father &&
                IS_LINEAR_DIADIC_2(father, ESHL, EMUL) &&
                IRO_IsIntConstant(father->u.diadic.right) &&
                (father2 = IRO_LocateFather(father)) &&
                IS_LINEAR_DIADIC(father2, EADD) &&
                father2->u.diadic.right == father &&
                (father3 = IRO_LocateFather(father2))) {
                IRO_InitList(&list);
                scale = father->u.diadic.right->u.node->data.intval;
                if (father->nodetype == ESHL) {
                    scale = CInt64_Shl(cint64_one, scale);
                }

                product = value->u.node->data.intval;
                if (isUnsigned) {
                    product = CInt64_MulU(scale, product);
                } else {
                    product = CInt64_Mul(scale, product);
                }
                value->u.node->data.intval = product;

                IRO_AddToList(value, &list);
                IRO_AddToList(add, &list);
                add->u.diadic.right = value;
                IRO_Paste(list.head, list.tail, father3);
                IRO_LocateFather_Cut_And_Paste_Without_Nopping(father2, add);
                add->u.diadic.left = father2;
                add->rtype = father2->rtype;
                foldedIntoAdd = 1;
            }

            if (!foldedIntoAdd) {
                add->u.diadic.right = value;
                add->u.diadic.right->flags |= IROLF_Reffed;
                value->next = add;

                add->u.diadic.left = nd;
                IRO_LocateFather_Cut_And_Paste_Without_Nopping(nd, add);
                add->flags |= IROLF_Reffed;

                nd->next = value;
                add->next = next;
            }
        }

        if (nd == last) {
            break;
        }
    }
}

IROLinear* UpdateInductionIncrement(IROLoop* loop, SInt32 value, IROLinear* before) {
    IROLinear* ind_nd;
    IROLinear* addvalue;
    IROLinear* ass;
    IROLinear* dup;
    Type* type;
    ENode* expr;
    IROList list;

    IRO_InitList(&list);
    ind_nd = loop->induction->nd;
    type = ind_nd->rtype;

    addvalue = IRO_NewLinear(IROLinearOperand);
    addvalue->index = ++IRO_NumLinear;
    addvalue->rtype = type;
    expr = IRO_NewENode(EINTCONST);
    expr->rtype = type;
    CInt64_SetLong(&expr->data.intval, value * loop->induction->addConst);
    addvalue->u.node = expr;
    IRO_AddToList(addvalue, &list);

    if (IS_LINEAR_MONADIC_2(ind_nd, EPREINC, EPOSTINC)) {
        dup = IRO_DuplicateExpr(ind_nd->u.monadic, &list);

        ass = IRO_NewLinear(IROLinearOp2Arg);
        ass->index = ++IRO_NumLinear;
        ass->nodetype = EADDASS;
        ass->u.diadic.left = dup;
        ass->u.diadic.right = addvalue;
        ass->rtype = type;
        IRO_AddToList(ass, &list);
    } else if (IS_LINEAR_MONADIC_2(ind_nd, EPREDEC, EPOSTDEC)) {
        dup = IRO_DuplicateExpr(ind_nd->u.monadic, &list);

        ass = IRO_NewLinear(IROLinearOp2Arg);
        ass->index = ++IRO_NumLinear;
        ass->nodetype = ESUBASS;
        ass->u.diadic.left = dup;
        ass->u.diadic.right = addvalue;
        ass->rtype = type;
        IRO_AddToList(ass, &list);
    } else if (IS_LINEAR_DIADIC(ind_nd, EADDASS)) {
        dup = IRO_DuplicateExpr(ind_nd->u.monadic, &list);

        ass = IRO_NewLinear(IROLinearOp2Arg);
        ass->index = ++IRO_NumLinear;
        ass->nodetype = EADDASS;
        ass->u.diadic.left = dup;
        ass->u.diadic.right = addvalue;
        ass->rtype = type;
        IRO_AddToList(ass, &list);
    }

    IRO_Paste(list.head, list.tail, before);
    return list.tail;
}

void GenInitialAssignment(IROLoop* loop, Object* var, IROList* list) {
    Type* type;
    IROLinear* nd;

    CError_ASSERT(3924, loop->init && loop->init->type == IROLinearOp2Arg);

    type = loop->induction->nd->rtype;

    nd = IRO_NewLinear(IROLinearOp2Arg);
    nd->index = ++IRO_NumLinear;
    nd->nodetype = EASS;
    nd->u.diadic.left = IRO_TempReference(var, list);
    nd->u.diadic.right = IRO_DuplicateExpr(loop->init->u.diadic.right, list);
    nd->rtype = type;
    IRO_AddToList(nd, list);
}

void GenNewInduction(void) {
    CError_FATAL(3941);
}
