#include "compiler/IroLoop.h"
#include "compiler/IroCSE.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroPropagate.h"
#include "compiler/IroSubable.h"
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
#include "compiler/objects.h"
#include "compiler/Switch.h"
#include "compiler/types.h"
#include "version.h"
#include <string.h>

// NOTE: invented names: IROLoopMemRefFlags, LoopMemRef_Load, LoopMemRef_Store, LoopMemRef_Invalid,
// LoopMemRef_Speculative (Load, Store and Invalid after MyHandleLoop_Motion's dump: "<load>",
// "<store>", "<invalid>")
// IROLoopMemRef.flags. Speculative is set until a reference is seen that runs on every iteration or
// must not be moved, and a candidate still marked so is not moved.
typedef enum IROLoopMemRefFlags {
    LoopMemRef_Load = 1,
    LoopMemRef_Store = 2,
    LoopMemRef_Invalid = 8,
    LoopMemRef_Speculative = 0x10
} IROLoopMemRefFlags;

// A memory reference through a loop-invariant address (MyHandleLoop_Motion's candidates for
// moving out of the loop). The flags are a byte enum, so the packing places the pointers.
#pragma pack(push, 2)
typedef struct IROLoopMemRef {
    IROLoopMemRefFlags flags;
    IROLinear* nd;
    IROElmList* list;
    IROAddrRecord* rec;
    struct IROLoopMemRef* next;
} IROLoopMemRef;
#pragma pack(pop)

IRONode* LoopNode;
Boolean ConditionalHeaderAtBottom;
IROLoopInd* FirstInd;
BitVector* InLoop;
BitVector* InLoop_Exits;
BitVector* InLoop_Tails;
UInt32 LoopExitNumber;
UInt32 LoopTailNum;
IRONode* LoopExitSuccessor;
IRONode* LoopTail;
IROLoopMemRef* IRO_LoopMemRefFirst;
IROLoopMemRef* IRO_LoopMemRefCurrent;
static IROExpr* RisList;
static BitVector* AllKills;
static Boolean KnownBounds;
static SInt32 Times;
static IROLinear* PredInt;
static IROElmList* FirstAddendLinear;
static IROElmList* LastAddendLinear;
static int NoSubableSubs;
#if VERSION >= VERSION_GC_2_5
// NOTE: invented names: SearchTargetFound, SearchTarget
// 2.5 and later: TreeContains' search state.
static Boolean SearchTargetFound;
static IROLinear* SearchTarget;
#endif

static void MyHandleLoop_Vector(IRONode* fnode);
static void MyHandleLoop_Motion(IRONode* fnode);
static void CheckAllLoopAddresses(IRONode* fnode, BitVector* bv, Boolean* resultFlag);
static void MakeLoopEntry(
    IROLinear* nd, IROAddrRecord* rec, Boolean mustreach1, Boolean cannotSpeculate
);
static void MoveInvarianceInAddressExpr(void);
static IROLinear* RearrangeInvarianceInAddressExpr(IROLinear* nd, IROList* list);
static UInt32 IsAssignmentReductionCandidate(IROLinear* nd);

void FindMustReach(void) {
    IRONode* fnode;
    IRONode* fnode2;
    IRONode* pred;
    int i;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        fnode->mustreach = FALSE;
        if (Bv_IsBitSet(fnode->index, InLoop)) {
            fnode->mustreach = TRUE;

            for (i = 0; i < LoopNode->numpred; i++) {
                pred = IRO_NodeTable[LoopNode->pred[i]];
                if (Bv_IsBitSet(pred->index, InLoop) && !Bv_IsBitSet(fnode->index, pred->dom)) {
                    fnode->mustreach = FALSE;
                    break;
                }
            }

            for (fnode2 = IRO_FirstNode; fnode2; fnode2 = fnode2->nextnode) {
                if (Bv_IsBitSet(fnode2->index, InLoop)) {
                    for (i = 0; i < fnode2->numsucc; i++) {
                        if (!Bv_IsBitSet(fnode2->succ[i], InLoop) &&
                            !Bv_IsBitSet(fnode->index, fnode2->dom)) {
                            fnode->mustreach = FALSE;
                            break;
                        }
                    }

                    if (!fnode->mustreach) {
                        break;
                    }
                }
            }
        }
    }
}

void FindMustReach1(IRONode* checkfnode) {
    IRONode* fnode;
    IRONode* fnode2;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop)) {
            fnode->mustreach1 = TRUE;
            for (fnode2 = IRO_FirstNode; fnode2; fnode2 = fnode2->nextnode) {
                if (Bv_IsBitSet(fnode2->index, InLoop)) {
                    if (Bv_IsBitSet(fnode2->index, InLoop_Tails) &&
                        !Bv_IsBitSet(fnode->index, fnode2->dom)) {
                        fnode->mustreach1 = FALSE;
                    }

                    if (Bv_IsBitSet(fnode2->index, InLoop_Exits) &&
                        fnode2 != checkfnode &&
                        !Bv_IsBitSet(fnode->index, fnode2->dom)) {
                        fnode->mustreach1 = FALSE;
                    }

                    if (!fnode->mustreach1) {
                        break;
                    }
                }
            }
        }
    }
}

static void IRO_FindLoopTailsAndExits(IRONode* fnode) {
    IRONode* scan;
    IRONode* succ;
    int i;

    for (scan = IRO_FirstNode; scan; scan = scan->nextnode) {
        if (Bv_IsBitSet(scan->index, InLoop)) {
            for (i = 0; i < scan->numsucc; i++) {
                succ = IRO_NodeTable[scan->succ[i]];
                if (succ == fnode) {
                    Bv_SetBit(scan->index, InLoop_Tails);
                    LoopTail = scan;
                    LoopTailNum++;
                }
                if (!Bv_IsBitSet(succ->index, InLoop)) {
                    LoopExitNumber++;
                    LoopExitSuccessor = succ;
                    Bv_SetBit(scan->index, InLoop_Exits);
                }
            }
        }
    }

    IRO_Dump(
        "IRO_FindLoopTailsAndExits:For  header %d, Loop exits and loop tails are  \n", fnode->index
    );
    IRO_DumpBits("Loop Exits: ", InLoop_Exits);
    IRO_DumpBits("Loop Tails: ", InLoop_Tails);
    IRO_Dump("LoopExitNum=%d: \n", LoopExitNumber);
    if (LoopExitSuccessor) {
        IRO_Dump("LoopExitSuccessor node =%d: \n", LoopExitSuccessor->index);
    }
}

static int IsSafeTypcon(IROLinear* nd) {
    Type* srcType;
    Type* destType;
    SInt32 srcSize;
    SInt32 destSize;
    Boolean srcUnsigned;
    Boolean destUnsigned;

    srcType = nd->u.monadic->rtype;
    destType = nd->rtype;
    if (!IS_TYPE_INT(srcType) || !IS_TYPE_INT(destType)) {
        return FALSE;
    }

    srcSize = srcType->size;
    destSize = destType->size;
    srcUnsigned = is_unsigned(srcType);
    destUnsigned = is_unsigned(destType);

    if (srcUnsigned == destUnsigned && destSize >= srcSize) {
        return TRUE;
    }
    if (srcUnsigned == TRUE && destUnsigned == FALSE && destSize > srcSize) {
        return TRUE;
    }
    return FALSE;
}

#if VERSION >= VERSION_GC_2_5
// 2.5 and later: an IRO_WalkTree visitor noting whether SearchTarget occurs in the tree.
// NOTE: invented name (after the IRO_Act* visitors; Wii keeps it as an unnamed static)
static void IRO_ActFindSearchTarget(IROLinear* linear, Boolean isFirst) {
    if (isFirst && linear == SearchTarget) {
        SearchTargetFound = TRUE;
    }
}

// 2.5 and later: whether TARGET occurs in TREE.
// NOTE: invented name (inlined at both uses, so the binary has no copy of it)
static Boolean TreeContains(IROLinear* tree, IROLinear* target) {
    SearchTarget = target;
    SearchTargetFound = FALSE;
    IRO_WalkTree(tree, IRO_ActFindSearchTarget);
    return SearchTargetFound;
}
#endif

#if VERSION < VERSION_GC_2_5
// 2.0's file has 26 lines fewer above here
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -26
#endif
static int Reducable(
    IROLinear* nd, IROLinear** resultNd1, IROLinear** resultNd2, VarRecord** resultVar
) {
    IROLinear* indirect;
    IROLinear* left;
    IROLinear* right;
    Boolean leftInvariant;
    Boolean rightInvariant;
    Boolean leftTypcon;
    Boolean rightTypcon;
    Object* obj;
    ENode* enode;
    IROLoopInd* ind;
    CInt64 divisor; // NOTE: invented name
    SInt32 val;
    SInt32 div;

    leftInvariant = FALSE;
    rightInvariant = FALSE;
    leftTypcon = FALSE;
    rightTypcon = FALSE;

    if (nd->type == IROLinearOp2Arg &&
        nd->nodetype == EADD &&
        (IS_TYPE_INT(nd->rtype) || IS_TYPE_POINTER_ONLY(nd->rtype))) {
        left = nd->u.diadic.left;
        right = nd->u.diadic.right;
        if (left->type == IROLinearOp1Arg && left->nodetype == ETYPCON) {
            leftTypcon = TRUE;
            leftInvariant = (left->flags & IROLF_LoopInvariant) != 0;
            left = left->u.monadic;
        }
        if (right->type == IROLinearOp1Arg && right->nodetype == ETYPCON) {
            rightTypcon = TRUE;
            rightInvariant = (right->flags & IROLF_LoopInvariant) != 0;
            right = right->u.monadic;
        }

        if (((left->flags & IROLF_LoopInvariant) || leftInvariant) && IRO_IsVariable(right)) {
            if (leftInvariant ||
                ((!(obj = IRO_IsVariable(left)) || !IRO_IsRegable(obj)) && !IRO_IsConstant(left))) {
                if (left->type == IROLinearOp2Arg &&
                    left->nodetype == EADD &&
                    (obj = IRO_IsVariable(left->u.diadic.left)) &&
                    IRO_IsRegable(obj) &&
                    IRO_IsConstant(left->u.diadic.right)) {
                    return FALSE;
                }

                if (rightTypcon) {
                    if (!IsSafeTypcon(nd->u.diadic.right)) {
                        return FALSE;
                    }
                    *resultNd2 = nd->u.diadic.right;
                } else {
                    *resultNd2 = right;
                }

                indirect = right;
                *resultNd1 = IRO_NewLinear(IROLinearOperand);

                enode = IRO_NewENode(EINTCONST);
                enode->data.intval = cint64_one;
                enode->rtype = nd->u.diadic.right->rtype;
                (*resultNd1)->rtype = nd->u.diadic.right->rtype;
                (*resultNd1)->u.node = enode;
            } else {
                return FALSE;
            }
        } else if (((left->flags & IROLF_LoopInvariant) || leftInvariant) &&
                   right->type == IROLinearOp2Arg &&
                   !rightTypcon &&
                   (right->nodetype == EMUL || right->nodetype == ESHL)) {
            if (IRO_IsConstant(right->u.diadic.right)) {
                if (right->nodetype == ESHL) {
                    right->nodetype = EMUL;
                    right->u.diadic.right->u.node->data.intval =
                        CInt64_Shl(cint64_one, right->u.diadic.right->u.node->data.intval);
                }
                if (right->u.diadic.left->type == IROLinearOp1Arg) {
                    if (IRO_IsVariable(right->u.diadic.left)) {
                        *resultNd2 = right->u.diadic.left;
                        indirect = right->u.diadic.left;
                        *resultNd1 = right->u.diadic.right;
                    } else if (right->u.diadic.left->nodetype == ETYPCON &&
                               IRO_IsVariable(right->u.diadic.left->u.monadic)) {
                        if (!IsSafeTypcon(right->u.diadic.left)) {
                            return FALSE;
                        }
                        *resultNd2 = right->u.diadic.left;
                        indirect = right->u.diadic.left->u.monadic;
                        *resultNd1 = right->u.diadic.right;
                    } else {
                        return FALSE;
                    }
                } else {
                    return FALSE;
                }
            } else {
                return FALSE;
            }
        } else if (((right->flags & IROLF_LoopInvariant) || rightInvariant) &&
                   left->type == IROLinearOp2Arg &&
                   !leftTypcon &&
                   (left->nodetype == EMUL || left->nodetype == ESHL)) {
            if (IRO_IsConstant(left->u.diadic.right)) {
                if (left->nodetype == ESHL) {
                    left->nodetype = EMUL;
                    left->u.diadic.right->u.node->data.intval =
                        CInt64_Shl(cint64_one, left->u.diadic.right->u.node->data.intval);
                }
                if (left->u.diadic.left->type == IROLinearOp1Arg) {
                    if (IRO_IsVariable(left->u.diadic.left)) {
                        *resultNd2 = left->u.diadic.left;
                        indirect = left->u.diadic.left;
                        *resultNd1 = left->u.diadic.right;
                    } else if (left->u.diadic.left->nodetype == ETYPCON &&
                               IRO_IsVariable(left->u.diadic.left->u.monadic)) {
                        if (!IsSafeTypcon(left->u.diadic.left)) {
                            return FALSE;
                        }
                        *resultNd2 = left->u.diadic.left;
                        indirect = left->u.diadic.left->u.monadic;
                        *resultNd1 = left->u.diadic.right;
                    } else {
                        return FALSE;
                    }
                } else {
                    return FALSE;
                }
            } else {
                return FALSE;
            }
        } else if (((right->flags & IROLF_LoopInvariant) || rightInvariant) &&
                   IRO_IsVariable(left)) {
            if (rightInvariant ||
                ((!(obj = IRO_IsVariable(right)) || !IRO_IsRegable(obj)) &&
                 !IRO_IsConstant(right))) {
                if (right->type == IROLinearOp2Arg &&
                    right->nodetype == EADD &&
                    (obj = IRO_IsVariable(right->u.diadic.left)) &&
                    IRO_IsRegable(obj) &&
                    IRO_IsConstant(right->u.diadic.right)) {
                    return FALSE;
                }

                if (leftTypcon) {
                    if (!IsSafeTypcon(nd->u.diadic.left)) {
                        return FALSE;
                    }
                    *resultNd2 = nd->u.diadic.left;
                } else {
                    *resultNd2 = left;
                }

                indirect = left;
                *resultNd1 = IRO_NewLinear(IROLinearOperand);

                enode = IRO_NewENode(EINTCONST);
                enode->data.intval = cint64_one;
                enode->rtype = nd->u.diadic.left->rtype;
                (*resultNd1)->rtype = nd->u.diadic.left->rtype;
                (*resultNd1)->u.node = enode;
            } else {
                return FALSE;
            }
        } else {
            return FALSE;
        }
    } else if (nd->type == IROLinearOp2Arg &&
               (nd->nodetype == EMUL || nd->nodetype == ESHL) &&
               nd->rtype->size <= 4 &&
               (IS_TYPE_INT(nd->rtype) || IS_TYPE_POINTER_ONLY(nd->rtype))) {
        left = nd->u.diadic.left;
        right = nd->u.diadic.right;

        if (IRO_IsConstant(nd->u.diadic.right) && IRO_IsVariable(left)) {
            *resultNd2 = left;
            indirect = left;
            *resultNd1 = right;
        } else if (IRO_IsConstant(nd->u.diadic.right) &&
                   left->type == IROLinearOp1Arg &&
                   left->nodetype == ETYPCON &&
                   IRO_IsVariable(left->u.monadic)) {
            if (!IsSafeTypcon(left)) {
                return FALSE;
            }
            *resultNd2 = left;
            indirect = left->u.monadic;
            *resultNd1 = right;
        } else {
            if (nd->type == IROLinearOp2Arg && nd->nodetype == ESHL) {
                return FALSE;
            }

            if (nd->u.diadic.right->flags & IROLF_LoopInvariant) {
                if (IRO_IsVariable(left)) {
                    *resultNd2 = left;
                    indirect = left;
                    *resultNd1 = right;
                } else if (left->type == IROLinearOp1Arg &&
                           left->nodetype == ETYPCON &&
                           IRO_IsVariable(left->u.monadic)) {
                    if (!IsSafeTypcon(left)) {
                        return FALSE;
                    }
                    *resultNd2 = left;
                    indirect = left->u.monadic;
                    *resultNd1 = right;
                } else {
                    return FALSE;
                }
            } else if (nd->u.diadic.left->flags & IROLF_LoopInvariant) {
                if (IRO_IsVariable(right)) {
                    *resultNd2 = right;
                    indirect = right;
                    *resultNd1 = left;
                } else if (right->type == IROLinearOp1Arg &&
                           right->nodetype == ETYPCON &&
                           IRO_IsVariable(right->u.monadic) &&
                           nd->type == IROLinearOp2Arg &&
                           nd->nodetype == EMUL) {
                    if (!IsSafeTypcon(right)) {
                        return FALSE;
                    }
                    *resultNd2 = right;
                    indirect = right->u.monadic;
                    *resultNd1 = left;
                } else {
                    return FALSE;
                }
            } else {
                return FALSE;
            }
        }
    } else if (nd->type == IROLinearOp2Arg &&
               (nd->nodetype == EDIV || nd->nodetype == ESHR) &&
               nd->rtype->size <= 4 &&
               IS_TYPE_INT(nd->rtype)) {
        if (IRO_IsVariable(nd->u.diadic.left) && IRO_IsConstant(nd->u.diadic.right)) {
            divisor = nd->u.diadic.right->u.node->data.intval;
            if (nd->type == IROLinearOp2Arg && nd->nodetype == ESHR) {
                if (CInt64_GetULong(&divisor) > 32 || divisor.hi) {
                    return FALSE;
                }
                divisor = CInt64_Shl(cint64_one, divisor);
            }
            *resultNd2 = nd->u.diadic.left;
            indirect = nd->u.diadic.left;
        } else {
            return FALSE;
        }
    } else {
        return FALSE;
    }

    // TODO: fakematch: the original loads the constant's high word into a register before testing
    // it, where the plain `...intval.hi` test compiles to a memory compare; the dead store to `div`
    // stands in.
    if (nd->type == IROLinearOp2Arg &&
        nd->nodetype == ESHL &&
        (!IRO_IsConstant(*resultNd1) ||
         (SInt32)CInt64_GetULong(&(*resultNd1)->u.node->data.intval) < 0 ||
         (SInt32)CInt64_GetULong(&(*resultNd1)->u.node->data.intval) > 32 ||
         (div = (*resultNd1)->u.node->data.intval.hi))) {
        return FALSE;
    }

    CError_ASSERT(834, indirect->u.monadic->u.node != NULL);
    *resultVar = IRO_FindVar(indirect->u.monadic->u.node->data.objref, FALSE, TRUE);
    if (!*resultVar || (*resultVar)->inductionState != 2) {
        return FALSE;
    }

    if (copts.ANSIstrict || copts.strengthreductionstrict) {
        Type* type = (*resultVar)->object->type;
        if (is_unsigned(type) && type->size < stunsignedlong.size) {
            return FALSE;
        }
    }

#if VERSION >= VERSION_GC_2_5
    // 2.5 and later look the induction record up for every candidate (2.0: only for ESHR and EDIV),
    // for the check at the end
    ind = FirstInd;
    while (ind && ind->var != *resultVar) {
        ind = ind->next;
    }

    CError_ASSERT(873, ind != NULL);
#endif

    if (nd->type == IROLinearOp2Arg && (nd->nodetype == ESHR || nd->nodetype == EDIV)) {
#if VERSION < VERSION_GC_2_5
        ind = FirstInd;
        while (ind && ind->var != *resultVar) {
            ind = ind->next;
        }

// 2.0's file has 22 lines fewer above here
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -22
        CError_ASSERT(873, ind != NULL);
#endif

        if (ind->addNode == NULL) {
            SInt32 addConst = ind->addConst;
            if (addConst < (val = CInt64_GetULong(&divisor))) {
                return FALSE;
            }
            if ((div = addConst / val) <= 0) {
                return FALSE;
            }
#if VERSION >= VERSION_GC_2_5
            // 2.5 and later: the step must also be a multiple of the divisor
            if (addConst % val) {
                return FALSE;
            }
#endif

            *resultNd1 = IRO_NewLinear(IROLinearOperand);
            enode = IRO_NewENode(EINTCONST);
            CInt64_SetULong(&enode->data.intval, div);
            enode->rtype = nd->u.diadic.left->rtype;
            (*resultNd1)->rtype = nd->u.diadic.left->rtype;
            (*resultNd1)->u.node = enode;
        } else {
            return FALSE;
        }
    }

#if VERSION >= VERSION_GC_2_5
    // 2.5 and later: an add that the induction variable's update uses as a value is not reduced
    if (nd->type == IROLinearOp2Arg &&
        nd->nodetype == EADD &&
        (IS_TYPE_INT(nd->rtype) || IS_TYPE_POINTER_ONLY(nd->rtype))) {
        if (TreeContains(ind->nd, nd) && (ind->nd->flags & IROLF_Reffed)) {
            return FALSE;
        }
    }
#endif

    return TRUE;
}

static void IRO_RemoveExpr_Action(IROLinear* linear, Boolean isFirst) {
    if (isFirst && linear->expr) {
        IRO_RemoveExpr(linear->expr);
    }
}

static void IRO_ActUnmarkRISCandidate(IROLinear* linear, Boolean isFirst) {
    if (isFirst && linear->expr) {
        linear->flags &= ~IROLF_Ris;
    }
}

// NOTE: invented names: inductionNd, factorNd, squared (it multiplies inductionNd by itself; the
// one caller passes FALSE), stepTempObj, cast
static IROExpr* CreateRIS(
    IROExpr* expr, IROLinear* inductionNd, IROLinear* factorNd, IROLoopInd* induction, int squared
) {
    Object* tempObj;
    IROLinear* thirdnode;
    Object* stepTempObj;
    Boolean stepTemp;
    IROLinear* firstnode;
    IROLinear* secondnode;
    IROLinear* fifthnode;
    IROLinear* fourthnode;
    Type* type;
    IROLinear* cast;
    ENode* enode;
    IROList list1;
    IROList list2;

    stepTemp = FALSE;
    type = expr->linear->rtype;
    tempObj = create_temp_object(type);
    IRO_FindVar(tempObj, TRUE, TRUE);
    IRO_InitList(&list1);

    if (IS_LINEAR_DIADIC(expr->linear, EADD)) {
        firstnode = IRO_DuplicateExpr(expr->linear, &list1);
    } else if (IS_LINEAR_DIADIC_2(expr->linear, EDIV, ESHR)) {
        firstnode = IRO_DuplicateExpr(expr->linear, &list1);
    } else {
        firstnode = IRO_NewLinear(IROLinearOp2Arg);
        firstnode->index = ++IRO_NumLinear;
        firstnode->rtype = type;
        firstnode->nodetype = EMUL;
        firstnode->u.diadic.left = IRO_DuplicateExpr(inductionNd, &list1);
        if (squared) {
            firstnode->u.diadic.right = IRO_DuplicateExpr(inductionNd, &list1);
        } else {
            firstnode->u.diadic.right = IRO_DuplicateExpr(factorNd, &list1);
        }
        IRO_AddToList(firstnode, &list1);
    }

    secondnode = IRO_NewLinear(IROLinearOp2Arg);
    secondnode->index = ++IRO_NumLinear;
    secondnode->rtype = type;
    secondnode->nodetype = EASS;
    secondnode->u.diadic.left = IRO_TempReference(tempObj, &list1);
    secondnode->u.diadic.left->flags |= IROLF_Ind | IROLF_Assigned;
    secondnode->u.diadic.left->u.monadic->flags |= IROLF_Ind | IROLF_Assigned;
    secondnode->u.diadic.right = firstnode;
    IRO_AddToList(secondnode, &list1);
    IRO_Paste(list1.head, list1.tail, PredInt);
    if (!IS_LINEAR_DIADIC_2(expr->linear, EDIV, ESHR) &&
        induction->addConst != 1 &&
        (induction->addNode || !IRO_IsConstant(factorNd))) {
        stepTemp = TRUE;
        IRO_InitList(&list1);
        thirdnode = IRO_NewLinear(IROLinearOp2Arg);
        thirdnode->index = ++IRO_NumLinear;
        thirdnode->rtype = factorNd->rtype;
        thirdnode->nodetype = EMUL;

        if (!induction->addNode) {
            thirdnode->u.diadic.left = IRO_DuplicateExpr(factorNd, &list1);
            thirdnode->u.diadic.right = IRO_NewLinear(IROLinearOperand);
            thirdnode->u.diadic.right->index = ++IRO_NumLinear;
            enode = IRO_NewENode(EINTCONST);
            enode->rtype = factorNd->rtype;
            thirdnode->u.diadic.right->rtype = factorNd->rtype;
            CInt64_SetLong(&enode->data.intval, induction->addConst);
            thirdnode->u.diadic.right->u.node = enode;
            IRO_AddToList(thirdnode->u.diadic.right, &list1);
        } else {
            thirdnode->u.diadic.left = IRO_DuplicateExpr(factorNd, &list1);
            thirdnode->u.diadic.right = IRO_DuplicateExpr(induction->addNode, &list1);
            if (factorNd->rtype != induction->addNode->rtype) {
                cast = IRO_NewLinear(IROLinearOp1Arg);
                cast->nodetype = ETYPCON;
                cast->index = ++IRO_NumLinear;
                cast->rtype = factorNd->rtype;
                cast->u.monadic = thirdnode->u.diadic.right;
                IRO_AddToList(cast, &list1);
                thirdnode->u.diadic.right = cast;
            }
        }
        IRO_AddToList(thirdnode, &list1);

        stepTempObj = create_temp_object(factorNd->rtype);

        fourthnode = IRO_NewLinear(IROLinearOp2Arg);
        fourthnode->index = ++IRO_NumLinear;
        fourthnode->rtype = factorNd->rtype;
        fourthnode->nodetype = EASS;
        fourthnode->u.diadic.left = IRO_TempReference(stepTempObj, &list1);
        fourthnode->u.diadic.left->flags |= IROLF_Ind | IROLF_Assigned;
        fourthnode->u.diadic.left->u.monadic->flags |= IROLF_Ind | IROLF_Assigned;
        fourthnode->u.diadic.right = thirdnode;
        IRO_AddToList(fourthnode, &list1);
        IRO_Paste(list1.head, list1.tail, PredInt);
    }

    IRO_InitList(&list2);
    fifthnode = IRO_NewLinear(IROLinearOp2Arg);
    fifthnode->index = ++IRO_NumLinear;
    fifthnode->rtype = type;
    if (induction->nd->type == IROLinearOp2Arg) {
        if (induction->nd->nodetype == EASS &&
            IS_LINEAR_DIADIC(induction->nd->u.diadic.right, EADD)) {
            fifthnode->nodetype = EADDASS;
        } else if (induction->nd->nodetype == EASS &&
                   IS_LINEAR_DIADIC(induction->nd->u.diadic.right, ESUB)) {
            fifthnode->nodetype = ESUBASS;
        } else {
            fifthnode->nodetype = induction->nd->nodetype;
        }
    } else {
        if (induction->nd->nodetype == EPREINC || induction->nd->nodetype == EPOSTINC) {
            fifthnode->nodetype = EADDASS;
        } else {
            fifthnode->nodetype = ESUBASS;
        }
    }

    fifthnode->u.diadic.left = IRO_TempReference(tempObj, &list2);
    fifthnode->u.diadic.left->flags |= IROLF_Ind | IROLF_Used | IROLF_Assigned;
    fifthnode->u.diadic.left->u.monadic->flags |= IROLF_Ind | IROLF_Used | IROLF_Assigned;
    if (!squared) {
        if (!stepTemp) {
            if (induction->addConst == 1 || IS_LINEAR_DIADIC_2(expr->linear, EDIV, ESHR)) {
                fifthnode->u.diadic.right = IRO_DuplicateExpr(factorNd, &list2);
            } else {
                fifthnode->u.diadic.right = IRO_NewLinear(IROLinearOperand);
                fifthnode->u.diadic.right->index = ++IRO_NumLinear;
                enode = IRO_NewENode(EINTCONST);
                enode->rtype = factorNd->rtype;
                fifthnode->u.diadic.right->rtype = factorNd->rtype;
                CInt64_SetLong(
                    &enode->data.intval,
                    induction->addConst * CInt64_GetULong(&factorNd->u.node->data.intval)
                );
                fifthnode->u.diadic.right->u.node = enode;
                IRO_AddToList(fifthnode->u.diadic.right, &list2);
            }
        } else {
            fifthnode->u.diadic.right = IRO_TempReference(stepTempObj, &list2);
            fifthnode->u.diadic.right->flags |= IROLF_Used;
        }
    }

    fifthnode->index = ++IRO_NumLinear;
    IRO_AddToList(fifthnode, &list2);
#if VERSION < VERSION_GC_2_5
    IRO_Paste(list2.head, list2.tail, IRO_FindStart(induction->nd));
#else
    // 2.5 and later: when the induction update itself contains the reduced add, the new update
    // goes after it
    if (IS_LINEAR_DIADIC(expr->linear, EADD) &&
        (IS_TYPE_INT(expr->linear->rtype) || IS_TYPE_POINTER_ONLY(expr->linear->rtype)) &&
        TreeContains(induction->nd, expr->linear) &&
        !(induction->nd->flags & IROLF_Reffed)) {
        IRO_PasteAfter(list2.head, list2.tail, induction->nd);
    } else {
        IRO_Paste(list2.head, list2.tail, IRO_FindStart(induction->nd));
    }
#endif
    IRO_WalkTree(expr->linear, IRO_RemoveExpr_Action);
    expr->temp = tempObj;
    expr->next = RisList;
    RisList = expr;
    return expr;
}

#if VERSION < VERSION_GC_2_5
// 2.0's file has 53 lines fewer above here
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -53
#endif
static IRONode* CreatePreHeader(IRONode* fnode1, IRONode* fnode2) {
    IROLinear* labelnode;
    IRONode* newfnode;
    IRONode* iter;
    CLabel* oldlabel;
    CLabel* newlabel;
    SwitchInfo* swinfo;
    SwitchCase* swcase;

    newfnode = oalloc(sizeof(IRONode));
    memset(newfnode, 0, sizeof(IRONode));
    newfnode->index = IRO_NumNodes;
    IRO_NumNodes++;

    labelnode = IRO_NewLinear(IROLinearLabel);
    labelnode->index = IRO_NumLinear++;
    labelnode->next = NULL;
    labelnode->u.label.label = IRO_NewLabel();
    labelnode->flags |= IROLF_StartsNode;
    labelnode->u.label.label->stmt = (Statement*)newfnode;
    newfnode->first = labelnode;
    newfnode->last = labelnode;

    if (fnode2) {
        fnode2->last->next = labelnode;
        labelnode->next = IRO_NewLinear(IROLinearNop);
        labelnode->next->next = fnode1->first;
        fnode2->nextnode = newfnode;
        newfnode->nextnode = fnode1;
    } else {
        CError_ASSERT(1313, fnode1->first->type == IROLinearLabel);
        labelnode->next = IRO_NewLinear(IROLinearGoto);
        labelnode->next->u.label.label = fnode1->first->u.label.label;
        IRO_LastNode->last->next = labelnode;
        IRO_LastNode->nextnode = newfnode;
        IRO_LastNode = newfnode;
        IRO_LastLinear = labelnode->next;
    }

    newfnode->last = labelnode->next;

    IRO_NodeTable = oalloc(sizeof(IRONode*) * IRO_NumNodes);
    memset(IRO_NodeTable, 0, sizeof(IRONode*) * IRO_NumNodes);
    for (iter = IRO_FirstNode; iter; iter = iter->nextnode) {
        IRO_NodeTable[iter->index] = iter;
    }

    if (fnode1->first->type == IROLinearLabel) {
        oldlabel = fnode1->first->u.label.label;
        newlabel = newfnode->first->u.label.label;
        for (iter = IRO_FirstNode; iter; iter = iter->nextnode) {
            if (!Bv_IsBitSet(iter->index, InLoop) && iter != newfnode) {
                switch (iter->last->type) {
                    case IROLinearGoto:
                        if (iter->last->u.label.label == oldlabel) {
                            iter->last->u.label.label = newlabel;
                        }
                        break;
                    case IROLinearIf:
                    case IROLinearIfNot:
                        if (iter->last->u.label.label == oldlabel) {
                            iter->last->u.label.label = newlabel;
                        }
                        break;
                    case IROLinearSwitch:
                        swinfo = iter->last->u.swtch.info;
                        for (swcase = swinfo->cases; swcase; swcase = swcase->next) {
                            if (swcase->label == oldlabel) {
                                swcase->label = newlabel;
                            }
                        }
                        if (swinfo->defaultlabel == oldlabel) {
                            swinfo->defaultlabel = newlabel;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    IRO_ComputeSuccPred();
    IRO_ComputeDom();
    return newfnode;
}

static IRONode* CreateNewLoopExitSuccessor(IRONode* fnode1) {
    IROLinear* labelnode;
    IRONode* fnode2;
    CLabel* oldlabel;
    SwitchInfo* swinfo;
    IRONode* iter;
    UInt16 i;
    IRONode* newfnode;
    Boolean placeAfterExit; // NOTE: invented name (the new node falls through from the exit)
    CLabel* newlabel;
    SwitchCase* swcase;

    CError_ASSERT(1414, fnode1 != NULL && LoopExitNumber == 1);

    fnode2 = NULL;
    placeAfterExit = FALSE;

    for (i = 0; i < fnode1->numpred; i++) {
        iter = IRO_NodeTable[fnode1->pred[i]];
        if (Bv_IsBitSet(iter->index, InLoop_Exits)) {
            CError_ASSERT(1425, fnode2 == NULL);
            fnode2 = iter;
            if (!placeAfterExit) {
                if (!iter->last ||
                    !fnode1->first ||
                    fnode1->first->type != IROLinearLabel ||
                    ((iter->last->type == IROLinearIf || iter->last->type == IROLinearIfNot) &&
                     iter->last->u.label.label != fnode1->first->u.label.label)) {
                    placeAfterExit = TRUE;
                }
            }
        }
    }

    CError_ASSERT(1441, fnode2 != NULL);

    newfnode = oalloc(sizeof(IRONode));
    memset(newfnode, 0, sizeof(IRONode));
    newfnode->index = IRO_NumNodes;
    IRO_NumNodes++;

    labelnode = IRO_NewLinear(IROLinearLabel);
    labelnode->index = IRO_NumLinear++;
    labelnode->next = NULL;
    labelnode->u.label.label = IRO_NewLabel();
    labelnode->flags |= IROLF_StartsNode;
    labelnode->u.label.label->stmt = (Statement*)newfnode;
    newfnode->first = labelnode;
    newfnode->last = labelnode;

    if (placeAfterExit) {
        fnode2->last->next = labelnode;
        labelnode->next = IRO_NewLinear(IROLinearNop);
        labelnode->next->next = fnode1->first;
        fnode2->nextnode = newfnode;
        newfnode->nextnode = fnode1;
    } else {
        CError_ASSERT(1481, fnode1->first->type == IROLinearLabel);
        labelnode->next = IRO_NewLinear(IROLinearGoto);
        labelnode->next->u.label.label = fnode1->first->u.label.label;
        IRO_LastNode->last->next = labelnode;
        IRO_LastNode->nextnode = newfnode;
        IRO_LastNode = newfnode;
        IRO_LastLinear = labelnode->next;
    }

    newfnode->last = labelnode->next;

    IRO_NodeTable = oalloc(sizeof(IRONode*) * IRO_NumNodes);
    memset(IRO_NodeTable, 0, sizeof(IRONode*) * IRO_NumNodes);
    for (iter = IRO_FirstNode; iter; iter = iter->nextnode) {
        IRO_NodeTable[iter->index] = iter;
    }

    if (fnode1->first->type == IROLinearLabel) {
        oldlabel = fnode1->first->u.label.label;
        newlabel = newfnode->first->u.label.label;
        switch (fnode2->last->type) {
            case IROLinearGoto:
                if (fnode2->last->u.label.label == oldlabel) {
                    fnode2->last->u.label.label = newlabel;
                }
                break;
            case IROLinearIf:
            case IROLinearIfNot:
                if (fnode2->last->u.label.label == oldlabel) {
                    fnode2->last->u.label.label = newlabel;
                }
                break;
            case IROLinearSwitch:
                swinfo = fnode2->last->u.swtch.info;
                for (swcase = swinfo->cases; swcase; swcase = swcase->next) {
                    if (swcase->label == oldlabel) {
                        swcase->label = newlabel;
                    }
                }
                if (swinfo->defaultlabel == oldlabel) {
                    swinfo->defaultlabel = newlabel;
                }
                break;
            default:
                break;
        }
    }

    IRO_ComputeSuccPred();
    IRO_ComputeDom();
    return newfnode;
}

void AddPreds(IRONode* fnode) {
    IRONode* pred;
    int i;

    for (i = 0; i < fnode->numpred; i++) {
        pred = IRO_NodeTable[fnode->pred[i]];
        if (!Bv_IsBitSet(pred->index, InLoop)) {
            Bv_SetBit(pred->index, InLoop);
            AddPreds(pred);
        }
    }
}

static void RemoveNoopsFromExprList(void) {
    IROExpr* expr;
    IROExpr* prev;

    expr = IRO_FirstExpr;
    prev = NULL;
    while (expr) {
        if (expr->linear->type == IROLinearNop) {
            if (prev) {
                prev->next = expr->next;
            } else {
                IRO_FirstExpr = expr->next;
            }
            expr = expr->next;
        } else {
            prev = expr;
            expr = expr->next;
        }
    }
}

void IncLoopDepth(void) {
    IRONode* fnode;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop)) {
            fnode->loopdepth++;
        }
    }
}

void IRO_SetLoopDepth(void) {
    IRONode* fnode;
    IRONode* pred;
    UInt16 i;
    UInt16 isHeader;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        fnode->loopdepth = 0;
    }

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
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
            IncLoopDepth();
        }
    }

    IRO_CheckForUserBreak();
}

static void InsertBranchAroundLoopPreheaders(void) {
    IRONode* fnode;
    IROLinear* labelnode;
    IRONode* iter;
    IROLinear* endnode;
    CLabel* label;

    if (IRO_EndNode && IRO_EndNode->last && IRO_EndNode->last->type == IROLinearEnd) {
        label = IRO_NewLabel();
        fnode = IRO_NewFlowGraphNode();
        IRO_LastNode->nextnode = fnode;
        label->stmt = (Statement*)fnode;

        IRO_NodeTable = oalloc(sizeof(IRONode*) * IRO_NumNodes);
        memset(IRO_NodeTable, 0, sizeof(IRONode*) * IRO_NumNodes);
        for (iter = IRO_FirstNode; iter; iter = iter->nextnode) {
            IRO_NodeTable[iter->index] = iter;
        }

        labelnode = IRO_NewLinear(IROLinearLabel);
        labelnode->index = ++IRO_NumLinear;
        labelnode->u.label.label = label;
        labelnode->flags |= IROLF_StartsNode;
        fnode->first = labelnode;
        IRO_LastLinear->next = labelnode;

        endnode = IRO_NewLinear(IROLinearEnd);
        memcpy(endnode, IRO_EndNode->last, sizeof(IROLinear));
        endnode->index = ++IRO_NumLinear;
        endnode->next = NULL;
        fnode->last = endnode;
        labelnode->next = endnode;
        IRO_LastLinear = endnode;

        IRO_EndNode->last->type = IROLinearGoto;
        IRO_EndNode->last->u.label.label = label;
        IRO_EndNode = IRO_LastNode = fnode;

        IRO_ComputeSuccPred();
        IRO_ComputeDom();
    }
}

void IRO_FindLoops(void) {
    IRONode* fnode;
    IRONode* pred;
    UInt16 i;
    UInt16 isHeader;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
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
            IncLoopDepth();
            IRO_Dump("IRO_FindLoops:Found loop with header %d\n", fnode->index);
            IRO_DumpBits("Loop includes: ", InLoop);
            MyHandleLoop_Motion(fnode);
            IRO_UpdateFlagsOnInts();
            MyHandleLoop_Vector(fnode);
            RemoveNoopsFromExprList();
            IRO_UpdateFlagsOnInts();
            IRO_UpdateVars();
        }
    }

    if (!IRO_FunctionHasReturn && IRO_EndNode != IRO_LastNode) {
        InsertBranchAroundLoopPreheaders();
    }
}

static void CheckSubableSub(IROLinear* linear, Boolean isFirst) {
    if (isFirst && IRO_IsSubableExpression(linear)) {
        IRO_Dump("Subable Expression is %d\n", linear->index);
        NoSubableSubs = FALSE;
    }
}

static int NoSubableSubExprs(IROLinear* nd) {
    NoSubableSubs = TRUE;
    IRO_WalkTree(nd, CheckSubableSub);
    return NoSubableSubs;
}

void ComputeLoopKills(void) {
    IRONode* fnode;
    IROLinear* nd;

    Bv_AllocVector(&AllKills, IRO_NumVars + 1);
    Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop) && (nd = fnode->first)) {
            Bv_AllocVector(&fnode->in, IRO_NumVars + 1);
            Bv_AllocVector(&fnode->gen, IRO_NumVars + 1);
            while (1) {
                Bv_Clear(IRO_VarKills);
                IRO_GetKills(nd);
                Bv_Or(IRO_VarKills, AllKills);
                if (nd == fnode->last) {
                    break;
                }
                nd = nd->next;
            }
        }
    }
}

void ComputeLoopInvariance(void) {
    IRONode* fnode;
    IROLinear* nd;

    IRO_Depends = IRO_VarKills;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop) && (nd = fnode->first)) {
            nd->flags &= ~IROLF_LoopInvariant;
            while (1) {
                if ((nd->flags & IROLF_Reffed) && nd->type != IROLinearNop) {
                    IRO_FindDepends_NoAlloc(nd);
                    if (!IRO_IsVolatile && !Bv_BitsInCommon(IRO_Depends, AllKills)) {
                        nd->flags |= IROLF_LoopInvariant;
                    }

                    if (IRO_CouldError) {
                        nd->flags |= IROLF_CouldError;
                    } else {
                        nd->flags &= ~IROLF_CouldError;
                    }
                }

                if (nd == fnode->last) {
                    break;
                }
                nd = nd->next;
            }
        }
    }
}

// NOTE: invented names: leftObj, rightObj, swapNd
void ComputeLoopInduction(void) {
    IRONode* fnode;
    IROLinear* nd;
    Boolean isUpdate;
    Object* obj;
    Object* leftObj;
    Object* rightObj;
    VarRecord* var;
    IROLinear* swapNd;
    IROLoopInd* ind;
    Boolean isUnsigned;

    Bv_AllocVector(&AllKills, IRO_NumVars + 1);
    Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop) && (nd = fnode->first)) {
            while (1) {
                Bv_Clear(IRO_VarKills);
                IRO_GetKills(nd);
                Bv_Or(IRO_VarKills, AllKills);
                isUpdate = FALSE;
                if ((nd->type == IROLinearOp2Arg &&
                     nd->rtype->size <= 4 &&
                     (nd->nodetype == EADDASS || nd->nodetype == ESUBASS) &&
                     (obj = IRO_IsVariable(nd->u.diadic.left)) &&
                     obj->type->size == nd->rtype->size &&
                     IS_TYPE_INT(obj->type) &&
                     ((IRO_IsIntConstant(nd->u.diadic.right) &&
                       CInt64_GetULong(&nd->u.diadic.right->u.node->data.intval)) ||
                      (nd->u.diadic.right->flags & IROLF_LoopInvariant))) ||
                    (nd->type == IROLinearOp1Arg &&
                     nd->rtype->size <= 4 &&
                     (nd->nodetype == EPOSTINC ||
                      nd->nodetype == EPOSTDEC ||
                      nd->nodetype == EPREINC ||
                      nd->nodetype == EPREDEC) &&
                     (obj = IRO_IsVariable(nd->u.monadic)) &&
                     obj->type->size == nd->rtype->size &&
                     IS_TYPE_INT(obj->type))) {
                    isUpdate = TRUE;
                } else if (nd->type == IROLinearOp2Arg &&
                           nd->rtype->size <= 4 &&
                           nd->nodetype == EASS &&
                           (obj = IRO_IsVariable(nd->u.diadic.left)) &&
                           obj->type->size == nd->rtype->size &&
                           IS_TYPE_INT(obj->type) &&
                           nd->u.diadic.right->type == IROLinearOp2Arg &&
                           (nd->u.diadic.right->nodetype == EADD ||
                            nd->u.diadic.right->nodetype == ESUB)) {
                    if (nd->u.diadic.right->nodetype == EADD) {
                        leftObj = IRO_IsVariable(nd->u.diadic.right->u.diadic.left);
                        rightObj = IRO_IsVariable(nd->u.diadic.right->u.diadic.right);
                        if (leftObj == obj &&
                            rightObj != obj &&
                            (nd->u.diadic.right->u.diadic.right->flags & IROLF_LoopInvariant)) {
                            isUpdate = TRUE;
                        }

                        if (rightObj == obj &&
                            leftObj != obj &&
                            (nd->u.diadic.right->u.diadic.left->flags & IROLF_LoopInvariant)) {
                            isUpdate = TRUE;
                            swapNd = nd->u.diadic.right->u.diadic.left;
                            nd->u.diadic.right->u.diadic.left = nd->u.diadic.right->u.diadic.right;
                            nd->u.diadic.right->u.diadic.right = swapNd;
                        }
                    } else {
                        leftObj = IRO_IsVariable(nd->u.diadic.right->u.diadic.left);
                        rightObj = IRO_IsVariable(nd->u.diadic.right->u.diadic.right);
                        if (leftObj == obj &&
                            rightObj != obj &&
                            (nd->u.diadic.right->u.diadic.right->flags & IROLF_LoopInvariant)) {
                            isUpdate = TRUE;
                        }
                    }
                }

                if (isUpdate) {
                    if ((var = IRO_FindAssigned(nd))) {
                        if (var->inductionState == 2) {
                            var->inductionState = FALSE;
                        } else if (var->inductionState == 1) {
                            var->inductionState = 2;
                        }
                    }
                } else {
                    for (var = IRO_FirstVar; var; var = var->next) {
                        if (Bv_IsBitSet(var->index, IRO_VarKills)) {
                            var->inductionState = FALSE;
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

    IRO_DumpBits("Killed in loop: ", AllKills);

    for (fnode = IRO_FirstNode, FirstInd = NULL; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop) && (nd = fnode->first)) {
            while (1) {
                if ((
                        nd->type == IROLinearOp2Arg &&
                        (nd->nodetype == EADDASS || nd->nodetype == ESUBASS || nd->nodetype == EASS)
                    ) ||
                    (nd->type == IROLinearOp1Arg &&
                     (nd->nodetype == EPOSTINC ||
                      nd->nodetype == EPOSTDEC ||
                      nd->nodetype == EPREINC ||
                      nd->nodetype == EPREDEC))) {
                    if ((var = IRO_FindAssigned(nd)) &&
                        var->inductionState == 2 &&
                        var->object->type->size <= 4) {
                        ind = oalloc(sizeof(IROLoopInd));
                        ind->fnode = fnode;
                        ind->var = var;
                        ind->nd = nd;
                        ind->next = FirstInd;
                        ind->addNode = NULL;
                        ind->addConst = 0;
                        ind->flags = 0;

                        if (nd->type == IROLinearOp2Arg) {
                            isUnsigned = is_unsigned(nd->rtype);
                            if (IRO_IsIntConstant(nd->u.diadic.right)) {
                                CInt64 val = nd->u.diadic.right->u.node->data.intval;
                                if (IS_LINEAR_DIADIC(nd, EADDASS) &&
                                    CInt64_Less(val, cint64_zero)) {
                                    nd->nodetype = ESUBASS;
                                    nd->u.diadic.right->u.node->data.intval = CInt64_Neg(val);
                                }
                                if (isUnsigned) {
                                    CInt64_ConvertUInt32(&nd->u.diadic.right->u.node->data.intval);
                                    ind->addConst =
                                        CInt64_GetULong(&nd->u.diadic.right->u.node->data.intval);
                                } else {
                                    CInt64_ConvertInt32(&nd->u.diadic.right->u.node->data.intval);
                                    ind->addConst =
                                        CInt64_GetULong(&nd->u.diadic.right->u.node->data.intval);
                                }
                                ind->addNode = NULL;
                            } else if (nd->nodetype == EADDASS || nd->nodetype == ESUBASS) {
                                ind->addNode = nd->u.diadic.right;
                            } else if (nd->nodetype == EASS) {
                                ind->addNode = nd->u.diadic.right->u.diadic.right;
                            }
                        } else {
                            if (IS_TYPE_POINTER_ONLY(nd->rtype)) {
                                ind->addConst = TPTR_TARGET(nd->rtype)->size;
                            } else {
                                ind->addConst = 1;
                            }
                            ind->addNode = NULL;
                        }

                        FirstInd = ind;

                        if (IS_LINEAR_DIADIC_2(nd, EADDASS, ESUBASS)) {
                            if (nd->u.diadic.right->flags & IROLF_LoopInvariant) {
                                IRO_Dump(
                                    "Found induction variable the new way: %s\n",
                                    var->object->name->name
                                );
                            } else {
                                IRO_Dump(
                                    "Found induction variable the old way: %s\n",
                                    var->object->name->name
                                );
                            }
                        } else if (nd->type == IROLinearOp2Arg &&
                                   (nd->u.diadic.right->nodetype == EADD ||
                                    nd->u.diadic.right->nodetype == ESUB)) {
                            IRO_Dump(
                                "Found induction variable the new way: %s\n",
                                var->object->name->name
                            );
                        } else {
                            IRO_Dump(
                                "Found induction variable the old way: %s\n",
                                var->object->name->name
                            );
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
}

static void IRO_ActUnmarkLoopInvariance(IROLinear* linear, Boolean isFirst) {
    if (isFirst) {
        linear->flags &= ~IROLF_LoopInvariant;
    }
}

static void UnmarkSubexpressionsOfInvariantExpressions(void) {
    IROExpr* expr;

    for (expr = IRO_FirstExpr; expr; expr = expr->next) {
        if (Bv_IsBitSet(expr->node->index, InLoop) &&
            !expr->notSubable &&
            (!expr->couldError || expr->node->mustreach) &&
            (expr->linear->flags & IROLF_LoopInvariant)) {
            IRO_WalkTree(expr->linear, IRO_ActUnmarkLoopInvariance);
            expr->linear->flags |= IROLF_LoopInvariant;
        }
    }
}

static void MyHandleLoop_Vector(IRONode* fnode) {
    IRONode* pred;
    UInt16 i;
    IROExpr* expr;
    IROExpr* removedExprs;
    IROExpr* exprnext;
    IROLoopInd* induction;
    IROExpr* exprinner;
    Boolean reused;
    IRONode* fallpred;
    IRONode* preheader;
    int hasOutsidePred;
    IRONode* iter;
    IROExpr* searchris;
    IROLinear* reduceNd1;
    VarRecord* var;
    IROLinear* reduceNd2;

    IRO_FirstExpr = NULL;
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

    fallpred = NULL;
    hasOutsidePred = FALSE;
    preheader = NULL;
    for (i = 0; i < LoopNode->numpred; i++) {
        pred = IRO_NodeTable[LoopNode->pred[i]];
        if (!Bv_IsBitSet(pred->index, InLoop)) {
            if (!hasOutsidePred) {
                preheader = pred;
            } else {
                preheader = NULL;
            }
            hasOutsidePred = TRUE;
            if (pred->nextnode == fnode) {
                CError_ASSERT(2377, fallpred == NULL || pred == fallpred);
                fallpred = pred;
            }
        }
    }

    if (!hasOutsidePred) {
        IRO_Dump("No predecessor outside the loop\n");
        return;
    }

    if (!preheader || preheader->last->type != IROLinearGoto) {
        preheader = CreatePreHeader(fnode, fallpred);
    }
    PredInt = preheader->last;

    if (PredInt->type == IROLinearGoto &&
        PredInt->u.label.label != LoopNode->first->u.label.label) {
        if (!KnownBounds || !Times) {
            for (iter = IRO_FirstNode; iter; iter = iter->nextnode) {
                iter->mustreach = FALSE;
            }
        } else {
            PredInt->u.label.label = LoopNode->first->u.label.label;
            IRO_ComputeSuccPred();
        }
    }

    if (copts.loopinvariants || copts.strengthreduction) {
        MoveInvarianceInAddressExpr();
        IRO_DumpAfterPhase("MoveInvarianceInAddressExpr", FALSE);
        IRO_FindExpressions(InLoop, TRUE);
        IRO_DumpExprs();
    }

    if (copts.loopinvariants) {
        UnmarkSubexpressionsOfInvariantExpressions();
    }

    if (!copts.optimizesize && copts.strengthreduction) {
        for (expr = IRO_FirstExpr; expr; expr = expr->next) {
            if (!(expr->flags & 4) &&
                Bv_IsBitSet(expr->node->index, InLoop) &&
                !expr->notSubable &&
                (!expr->couldError || expr->node->mustreach) &&
                Reducable(expr->linear, &reduceNd1, &reduceNd2, &var)) {
                IRO_WalkTree(expr->linear, IRO_ActUnmarkRISCandidate);
                expr->linear->flags |= IROLF_Ris;
                expr->factor = reduceNd1;
                expr->var = var;
                expr->indOperand = reduceNd2;
            }
        }
    }

    if (!copts.optimizesize && copts.strengthreduction) {
        RisList = NULL;
        for (expr = IRO_FirstExpr; expr; expr = exprnext) {
            exprnext = expr->next;
            if (!(expr->flags & 4) && (expr->linear->flags & IROLF_Ris)) {
                reduceNd1 = expr->factor;
                var = expr->var;
                reduceNd2 = expr->indOperand;

                induction = FirstInd;
                while (induction && induction->var != var) {
                    induction = induction->next;
                }
                CError_ASSERT(3035, induction != NULL);

                IRO_FindDepends(reduceNd1);
                if (!Bv_BitsInCommon(IRO_Depends, AllKills)) {
                    IRO_Dump("Found reduction in strength: %d\n", expr->linear->index);
                    if (IS_LINEAR_DIADIC(expr->linear, ESHL)) {
                        expr->linear->nodetype = EMUL;
                        CInt64_SetULong(
                            &reduceNd1->u.node->data.intval,
                            1 << CInt64_GetULong(&reduceNd1->u.node->data.intval)
                        );
                        reduceNd1->u.node->rtype = expr->linear->rtype;
                    }

                    searchris = RisList;
                    while (1) {
                        if (!searchris) {
                            break;
                        }
                        if (IRO_ExprsSame(expr->linear, searchris->linear)) {
                            break;
                        }
                        searchris = searchris->next;
                    }

                    if (searchris) {
                        IRO_Dump("Using existing RIS: %d\n", searchris->linear->index);
                        IRO_WalkTree(expr->linear, IRO_RemoveExpr_Action);
                    } else {
                        searchris = CreateRIS(expr, reduceNd2, reduceNd1, induction, FALSE);
                    }
                    IRO_ReplaceReference(expr->linear, searchris->temp, expr->linear);
                    IRO_ClipExpr(expr);
                }
            }
        }
    }

    RisList = NULL;

    expr = IRO_FirstExpr;
    removedExprs = NULL;
    if (copts.loopinvariants) {
        while (expr) {
            exprnext = expr->next;
            reused = FALSE;

            if (Bv_IsBitSet(expr->node->index, InLoop) &&
                !expr->notSubable &&
                (!expr->couldError || expr->node->mustreach) &&
                (expr->linear->flags & IROLF_LoopInvariant) &&
                !(expr->flags & 4)) {
                IRO_Dump("Found loop invariant: %d\n", expr->linear->index);

                for (exprinner = removedExprs; exprinner; exprinner = exprinner->next) {
                    if (IRO_ExprsSame(exprinner->linear, expr->linear)) {
                        IRO_ReplaceReference(expr->linear, exprinner->temp, expr->linear);
                        IRO_NopOut(expr->linear);
                        IRO_Dump("Using already removed expr: %d\n", exprinner->linear->index);
                        IRO_RemoveExpr(expr);
                        reused = TRUE;
                        break;
                    }
                }

                if (!reused && !expr->temp) {
                    IRO_GetTemp(expr);
                    IRO_ReplaceReference(expr->linear, expr->temp, expr->linear);
                    IRO_MoveExpression(expr, PredInt);
                    IRO_AssignToTemp(expr);
                    IRO_RemoveExpr(expr);
                    expr->next = removedExprs;
                    removedExprs = expr;
                }
            }

            expr = exprnext;
        }
    }
}

static void MyHandleLoop_Motion(IRONode* fnode) {
    IROLoopMemRef* memref;
    IRONode* pred;
    UInt16 i;
    IRONode* fallpred;
    IRONode* preheader;
    int hasOutsidePred;
    IRONode* iter;
    IROLinear* ass;
    Object* tempobj;
    VarRecord* var;
    Boolean addressesOk; // NOTE: invented name
    IROLoop* loop;
    IROList list;
    IROElmList* refiter;

    IRO_FirstExpr = NULL;
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

    fallpred = NULL;
    hasOutsidePred = FALSE;
    preheader = NULL;
    for (i = 0; i < LoopNode->numpred; i++) {
        pred = IRO_NodeTable[LoopNode->pred[i]];
        if (!Bv_IsBitSet(pred->index, InLoop)) {
            if (!hasOutsidePred) {
                preheader = pred;
            } else {
                preheader = NULL;
            }
            hasOutsidePred = TRUE;
            if (pred->nextnode == fnode) {
                CError_ASSERT(3388, fallpred == NULL || pred == fallpred);
                fallpred = pred;
            }
        }
    }

    if (!hasOutsidePred) {
        IRO_Dump("No predecessor outside the loop\n");
        return;
    }

    if (!preheader || preheader->last->type != IROLinearGoto) {
        preheader = CreatePreHeader(fnode, fallpred);
    }
    PredInt = preheader->last;

    if (PredInt->type == IROLinearGoto &&
        PredInt->u.label.label != LoopNode->first->u.label.label) {
        if (!KnownBounds || !Times) {
            for (iter = IRO_FirstNode; iter; iter = iter->nextnode) {
                iter->mustreach = FALSE;
            }
        } else {
            PredInt->u.label.label = LoopNode->first->u.label.label;
            IRO_ComputeSuccPred();
        }
    }

    if (copts.loopinvariants || copts.strengthreduction) {
        MoveInvarianceInAddressExpr();
        IRO_DumpAfterPhase("MoveInvarianceInAddressExpr", FALSE);
        IRO_FindExpressions(InLoop, FALSE);
        IRO_DumpExprs();
    }

    if (copts.loopinvariants) {
        UnmarkSubexpressionsOfInvariantExpressions();
    }

    addressesOk = TRUE;
    Bv_AllocVector(&InLoop_Exits, IRO_NumNodes + 1);
    Bv_AllocVector(&InLoop_Tails, IRO_NumNodes + 1);
    LoopExitNumber = 0;
    LoopExitSuccessor = NULL;
    LoopTailNum = 0;
    LoopTail = NULL;
    IRO_FindLoopTailsAndExits(fnode);
    FindMustReach1(fnode);

    loop = NULL;
    if (LoopTailNum == 1) {
        if (fnode->last->type == IROLinearIf || fnode->last->type == IROLinearIfNot) {
            loop = ExtractLoopInfo(fnode);
        } else if (LoopTail->last->type == IROLinearIf || LoopTail->last->type == IROLinearIfNot) {
            loop = ExtractLoopInfo(LoopTail);
        }
    }

    if (loop && !(loop->flags & LP_IFEXPR_NON_CANONICAL) && LoopExitNumber == 1) {
        IRO_LoopMemRefFirst = NULL;
        IRO_LoopMemRefCurrent = NULL;
        CheckAllLoopAddresses(IRO_FirstNode, InLoop, &addressesOk);
        CheckAllLoopAddresses(IRO_FirstNode, InLoop, &addressesOk);
        if (addressesOk) {
            for (memref = IRO_LoopMemRefFirst; memref; memref = memref->next) {
                if (memref->flags & LoopMemRef_Speculative) {
                    memref->flags |= LoopMemRef_Invalid;
                }
            }

            for (memref = IRO_LoopMemRefFirst; memref; memref = memref->next) {
                IRO_Dump("Loop Motion Candidate:: Int= %d", memref->nd->index);
                if (memref->flags & LoopMemRef_Invalid) {
                    IRO_Dump("<invalid>");
                }
                if (memref->flags & LoopMemRef_Load) {
                    IRO_Dump("<load>");
                }
                if (memref->flags & LoopMemRef_Store) {
                    IRO_Dump("<store>");
                }
                IRO_Dump("\n");

                if (!(memref->flags & LoopMemRef_Invalid)) {
                    tempobj = create_temp_object(memref->nd->rtype);
                    IRO_FindVar(tempobj, TRUE, TRUE);
                    if (IRO_FindVar(
                            ((IROLinear*)memref->rec->objRefs->element)->u.node->data.objref,
                            FALSE,
                            TRUE
                        )) {
                        IRO_InitList(&list);
                        ass = IRO_NewLinear(IROLinearOp2Arg);
                        ass->index = ++IRO_NumLinear;
                        ass->rtype = memref->nd->rtype;
                        ass->nodetype = EASS;
                        ass->u.diadic.left = IRO_TempReference(tempobj, &list);
                        ass->u.diadic.left->flags |= IROLF_Ind | IROLF_Assigned;
                        ass->u.diadic.left->u.monadic->flags |= IROLF_Ind | IROLF_Assigned;
                        ass->u.diadic.right = IRO_DuplicateExpr(memref->nd, &list);
                        IRO_AddToList(ass, &list);
                        IRO_Paste(list.head, list.tail, PredInt);
                    } else {
                        CError_FATAL(3631);
                    }

                    if (LoopExitSuccessor->numpred != 1) {
                        LoopExitSuccessor = CreateNewLoopExitSuccessor(LoopExitSuccessor);
                    }

                    IRO_InitList(&list);
                    ass = IRO_NewLinear(IROLinearOp2Arg);
                    ass->index = ++IRO_NumLinear;
                    ass->rtype = memref->nd->rtype;
                    ass->nodetype = EASS;
                    ass->u.diadic.left = IRO_DuplicateExpr(memref->nd, &list);
                    ass->u.diadic.left->flags |= IROLF_Ind | IROLF_Assigned;
                    ass->u.diadic.left->u.monadic->flags |= IROLF_Ind | IROLF_Assigned;
                    ass->u.diadic.right = IRO_TempReference(tempobj, &list);
                    IRO_AddToList(ass, &list);

                    if (LoopExitSuccessor->first->type == IROLinearLabel) {
                        IRO_PasteAfter(list.head, list.tail, LoopExitSuccessor->first);
                    } else {
                        IRO_Paste(list.head, list.tail, LoopExitSuccessor->first);
                    }

                    for (refiter = memref->list; refiter; refiter = refiter->next) {
                        IRO_Dump(
                            "Loop Motion Candidate Reference at Int= %d\n",
                            ((IROLinear*)refiter->element)->index
                        );
                        IRO_InitList(&list);
                        IRO_TempReference(tempobj, &list);
                        IRO_Paste(list.head, list.tail, refiter->element);
                        IRO_LocateFather_Cut_And_Paste(refiter->element, list.tail);
                    }
                }
            }
        }
    }

    IRO_DumpAfterPhase("After Motion", FALSE);
}

static Boolean CheckLoopAddress(IROLinear* nd, Boolean mustreach1) {
    IROLoopMemRef* memref;
    IROAddrRecord* rec;
    IROAddrRecord* otherrec;
    IROLinear* inner;
    IROElmList* list;
    Boolean cannotSpeculate; // NOTE: invented name

    inner = nd->u.monadic;
    rec = IRO_InitAddrRecordPointer(inner);
    if (IS_LINEAR_ENODE(inner, EOBJREF)) {
        rec->numObjRefs++;
        IRO_AddElmToList(inner, &rec->objRefs);
    } else if (IS_LINEAR_DIADIC(inner, EADD)) {
        IRO_DecomposeAddressExpression(inner, rec);
    } else {
        return FALSE;
    }

    if (rec->numObjRefs != 1) {
        return FALSE;
    }

    cannotSpeculate = (nd->flags & IROLF_CouldError) || ((nd->flags & IROLF_Reffed) == 0);
    if (!IRO_LoopMemRefFirst) {
        MakeLoopEntry(nd, rec, mustreach1, cannotSpeculate);
    } else {
        for (memref = IRO_LoopMemRefFirst; memref; memref = memref->next) {
            otherrec = memref->rec;
            if (((IROLinear*)rec->objRefs->element)->u.node->data.objref ==
                ((IROLinear*)otherrec->objRefs->element)->u.node->data.objref) {
                if (IRO_ExprsSame(inner, otherrec->linear)) {
                    list = oalloc(sizeof(IROElmList));
                    list->element = nd;
                    list->next = NULL;
                    if (!memref->list) {
                        memref->list = list;
                    } else {
                        list->next = memref->list;
                        memref->list = list;
                    }
                } else {
                    memref->flags |= LoopMemRef_Invalid;
                }

                if (!mustreach1 && cannotSpeculate) {
                    memref->flags |= LoopMemRef_Invalid;
                }
                if (mustreach1 || cannotSpeculate) {
                    memref->flags &= ~LoopMemRef_Speculative;
                }
                break;
            }
        }

        if (!memref) {
            MakeLoopEntry(nd, rec, mustreach1, cannotSpeculate);
        }
    }

    return TRUE;
}

static void CheckAllLoopAddresses(IRONode* fnode, BitVector* bv, Boolean* resultFlag) {
    IROLinear* nd;
    Boolean ok; // NOTE: invented name

    ok = *resultFlag;
    while (fnode) {
        if (Bv_IsBitSet(fnode->index, bv) && (nd = fnode->first)) {
            while (ok) {
                if (nd->type == IROLinearOp1Arg && nd->nodetype == EINDIRECT) {
                    ok = CheckLoopAddress(nd, fnode->mustreach1);
                    // A call of a non-intrinsic function or an asm statement fails the check. The
                    // original tests for a call twice, as if through a macro testing the kind
                    // again.
                } else if ((nd->type == IROLinearFunccall &&
                            (nd->type == IROLinearFunccall &&
                             !(nd->u.funccall.functype->flags & FUNC_INTRINSIC))) ||
                           nd->type == IROLinearAsm) {
                    ok = FALSE;
                }

                if (nd == fnode->last) {
                    break;
                }
                nd = nd->next;
            }

            if (!ok) {
                break;
            }
        }
        fnode = fnode->nextnode;
    }
    *resultFlag = ok;
}

// NOTE: invented name: cannotSpeculate
// `cannotSpeculate`: the reference could fault or is a whole statement, so it must not be hoisted
// unless it runs on every iteration
static void MakeLoopEntry(
    IROLinear* nd, IROAddrRecord* rec, Boolean mustreach1, Boolean cannotSpeculate
) {
    IROElmList* list;

    if (!IRO_IsRegable(((IROLinear*)rec->objRefs->element)->u.node->data.objref) &&
        (nd->u.monadic->flags & IROLF_LoopInvariant) &&
        !IRO_HasSideEffect(nd)) {
        if (!IRO_LoopMemRefFirst) {
            IRO_LoopMemRefCurrent = IRO_LoopMemRefFirst = oalloc(sizeof(IROLoopMemRef));
        } else {
            IRO_LoopMemRefCurrent->next = oalloc(sizeof(IROLoopMemRef));
            IRO_LoopMemRefCurrent = IRO_LoopMemRefCurrent->next;
        }
        IRO_LoopMemRefCurrent->flags = LoopMemRef_Speculative;
        IRO_LoopMemRefCurrent->nd = nd;
        IRO_LoopMemRefCurrent->rec = rec;
        IRO_LoopMemRefCurrent->next = NULL;
        IRO_LoopMemRefCurrent->list = NULL;

        list = oalloc(sizeof(IROElmList));
        list->element = nd;
        list->next = NULL;
        if (!IRO_LoopMemRefCurrent->list) {
            IRO_LoopMemRefCurrent->list = list;
        } else {
            list->next = IRO_LoopMemRefCurrent->list;
            IRO_LoopMemRefCurrent->list = list;
        }

        if (((IROLinear*)rec->objRefs->element)->flags & IROLF_Assigned) {
            IRO_LoopMemRefCurrent->flags |= LoopMemRef_Store;
            if (((IROLinear*)rec->objRefs->element)->flags & IROLF_Used) {
                IRO_LoopMemRefCurrent->flags |= LoopMemRef_Load;
            }
        } else {
            IRO_LoopMemRefCurrent->flags |= LoopMemRef_Load;
        }

        if (!mustreach1 && cannotSpeculate) {
            IRO_LoopMemRefCurrent->flags |= LoopMemRef_Invalid;
        }

        if (mustreach1 || cannotSpeculate) {
            IRO_LoopMemRefCurrent->flags &= ~LoopMemRef_Speculative;
        }
    }
}

void FindAssignmenttoInductionVar(IROLoop* loop, IRONode* fnode) {
    IROLinear* nd;
    UInt32 index;

    if (!loop->induction) {
        loop->init = NULL;
        return;
    }

    index = loop->induction->var->index;
    for (nd = fnode->first; nd; nd = nd->next) {
        Bv_Clear(IRO_VarKills);
        IRO_GetKills(nd);
        if (Bv_IsBitSet(index, IRO_VarKills)) {
            loop->init = nd;
        }

        if (nd == fnode->last) {
            break;
        }
    }

    if (loop->init && loop->init->type != IROLinearOp2Arg) {
        loop->init = NULL;
    }
}

static void ComputeIntWeight(IROLoop* loop, IROLinear* Int) {
    loop->weight++;
}

static IROLinear* IsTypconVar(IROLinear* nd) {
    if (IS_LINEAR_MONADIC(nd, ETYPCON) && IRO_IsVariable(nd->u.monadic)) {
        return nd->u.monadic;
    } else {
        return NULL;
    }
}

// NOTE: invented names: condStart, inductionCount
// TODO: not exact (register allocation). The split variables (`ind`, `right2`, `scannd2`,
// `testRight` reused for the swapped compare) are guesses that come closest to the original's code,
// not evidence of its source.
IROLoop* ExtractLoopInfo(IRONode* fnode) {
    Boolean hasCalls;
    Boolean hasCntrlFlow;
    Boolean gotoCount;
    UInt32 assignCount;
    IROLoopInd* testInd;
    Object* obj;
    IROLinear* nd;
    IRONode* scanfnode;
    IROLinear* testLeft;
    IROLinear* step;
    IROLinear* scannd;
    IROLinear* scannd2;
    IROLinear* condStart;
    VarRecord* var;
    IROLoopInd* scanind;
    IROLoopInd* ind;
    IROLinear* testRight;
    IROLinear* left;
    IROLinear* right;
    IROLinear* right2;
    UInt32 inductionCount;
    IROLoop* loop;
    UInt32 i;
    SInt32 hi;

    hasCalls = FALSE;
    hasCntrlFlow = FALSE;
    gotoCount = FALSE;
    assignCount = 0;
    LoopNode = fnode;

    loop = oalloc(sizeof(IROLoop));
    loop->fnode = fnode;
    nd = LoopNode->last->u.label.cond;
    loop->cond = nd;
    loop->flags = 0;
    loop->unused = 0;
    loop->init = NULL;
    loop->induction = NULL;
    loop->lo = -1;
    loop->hi = -1;
    loop->weight = 0;
    // renumber the nodes first: lo and hi below compare their indices
    IRO_RenumberInts();

    if (nd->type == IROLinearOp2Arg && IS_TYPE_INT(nd->rtype)) {
        testInd = NULL;
        testLeft = nd->u.diadic.left;
        testRight = nd->u.diadic.right;
        if (IRO_IsVariable(testLeft) || (testLeft = IsTypconVar(testLeft))) {
            if ((var = IRO_FindVar(testLeft->u.monadic->u.node->data.objref, FALSE, TRUE))) {
                scanind = FirstInd;
                while (scanind && scanind->var != var) {
                    scanind = scanind->next;
                }
                if (scanind) {
                    testInd = scanind;
                    loop->flags |= LP_INDUCTION_AT_LEFT;
                    loop->induction = scanind;
                }
            }
        }

        if (IRO_IsVariable(testRight) || (testRight = IsTypconVar(testRight))) {
            if ((var = IRO_FindVar(testRight->u.monadic->u.node->data.objref, FALSE, TRUE))) {
                scanind = FirstInd;
                while (scanind && scanind->var != var) {
                    scanind = scanind->next;
                }
                if (scanind) {
                    testInd = scanind;
                    loop->flags &= ~LP_INDUCTION_AT_LEFT;
                    loop->induction = scanind;
                }
            }
        }

        if (testInd && testInd->addConst > 0) {
            if (loop->flags & LP_INDUCTION_AT_LEFT) {
                if (loop->cond->type != IROLinearOp2Arg ||
                    !(loop->cond->nodetype == ELESS ||
                      loop->cond->nodetype == EGREATER ||
                      loop->cond->nodetype == ELESSEQU ||
                      loop->cond->nodetype == EGREATEREQU) ||
                    !loop->cond->u.diadic.left ||
                    !loop->cond->u.diadic.left->rtype ||
                    !IS_TYPE_INT(loop->cond->u.diadic.left->rtype) ||
                    !loop->cond->u.diadic.right ||
                    !loop->cond->u.diadic.right->rtype ||
                    !IS_TYPE_INT(loop->cond->u.diadic.right->rtype)) {
                    loop->flags |= LP_IFEXPR_NON_CANONICAL;
                    return loop;
                }
            } else {
                if (loop->cond->type == IROLinearOp2Arg &&
                    (loop->cond->nodetype == EGREATER || loop->cond->nodetype == EGREATEREQU) &&
                    (testRight = loop->cond->u.diadic.left) &&
                    testRight->rtype &&
                    IS_TYPE_INT(testRight->rtype) &&
                    (right = loop->cond->u.diadic.right) &&
                    right->rtype &&
                    IS_TYPE_INT(right->rtype)) {
                    loop->cond->u.diadic.left = right;
                    loop->cond->u.diadic.right = testRight;
                    if (loop->cond->nodetype == EGREATER) {
                        loop->cond->nodetype = ELESS;
                    } else if (loop->cond->nodetype == EGREATEREQU) {
                        loop->cond->nodetype = ELESSEQU;
                    }
                    loop->flags |= LP_INDUCTION_AT_LEFT;
                } else if (loop->cond->type == IROLinearOp2Arg &&
                           (loop->cond->nodetype == ELESS || loop->cond->nodetype == ELESSEQU) &&
                           (left = loop->cond->u.diadic.left) &&
                           left->rtype &&
                           IS_TYPE_INT(left->rtype) &&
                           (right2 = loop->cond->u.diadic.right) &&
                           right2->rtype &&
                           IS_TYPE_INT(right2->rtype)) {
                    loop->cond->u.diadic.left = right2;
                    loop->cond->u.diadic.right = left;
                    if (loop->cond->nodetype == ELESS) {
                        loop->cond->nodetype = EGREATER;
                    } else if (loop->cond->nodetype == ELESSEQU) {
                        loop->cond->nodetype = EGREATEREQU;
                    }
                    loop->flags |= LP_INDUCTION_AT_LEFT;
                } else {
                    loop->flags |= LP_IFEXPR_NON_CANONICAL;
                    return loop;
                }
            }
        } else {
            loop->flags |= LP_INDUCTION_NOT_FOUND;
            return loop;
        }
    } else if (nd->type == IROLinearOp1Arg && IS_TYPE_INT(nd->rtype)) {
        if (nd->nodetype == EPREINC ||
            nd->nodetype == EPOSTINC ||
            nd->nodetype == EPREDEC ||
            nd->nodetype == EPOSTDEC) {
            if (IRO_IsVariable(nd->u.monadic)) {
                if ((var =
                         IRO_FindVar(nd->u.monadic->u.monadic->u.node->data.objref, FALSE, TRUE))) {
                    scanind = FirstInd;
                    while (scanind && scanind->var != var) {
                        scanind = scanind->next;
                    }
                    if (scanind) {
                        testInd = scanind;
                        loop->flags |= LP_WHILE_LOOP;
                        loop->induction = scanind;
                    } else {
                        loop->flags |= LP_INDUCTION_NOT_FOUND;
                        return loop;
                    }
                } else {
                    loop->flags |= LP_INDUCTION_NOT_FOUND;
                    return loop;
                }
            } else {
                loop->flags |= LP_INDUCTION_NOT_FOUND;
                return loop;
            }
        } else {
            loop->flags |= LP_INDUCTION_NOT_FOUND;
            return loop;
        }
    } else {
        loop->flags |= LP_IFEXPR_NON_CANONICAL;
        return loop;
    }

    inductionCount = 0;
    scanind = FirstInd;
    while (scanind) {
        inductionCount++;
        scanind = scanind->next;
    }

    if (inductionCount > 1) {
        loop->flags |= LP_HAS_MULTIPLE_INDUCTIONS;
    }

    if ((ind = loop->induction)) {
        nd = ind->nd;
        if (nd->type == IROLinearOp2Arg) {
            if (IS_LINEAR_DIADIC_2(loop->cond, ELESS, ELESSEQU)) {
                if (nd->nodetype == EADDASS) {
                    if (ind->addConst == 1) {
                        loop->flags |= LP_LOOP_STEP_ISADD;
                    }
                    if (ind->addConst > 0) {
                        loop->flags |= LP_LOOP_STEP_ISPOS;
                    }
                } else if (nd->nodetype == EASS &&
                           IS_LINEAR_DIADIC(nd->u.diadic.right, EADD) &&
                           IRO_IsIntConstant(step = nd->u.diadic.right->u.diadic.right) &&
                           // TODO: fakematch, as in Reducable: the original tests the high word in
                           // a register; the dead store to `hi` stands in
                           (hi = step->u.node->data.intval.hi) == 0) {
                    if (CInt64_GetULong(&step->u.node->data.intval) == 1) {
                        loop->flags |= LP_LOOP_STEP_ISADD;
                    }
                    if (CInt64_GetULong(&step->u.node->data.intval) > 0) {
                        loop->flags |= LP_LOOP_STEP_ISPOS;
                    }
                }
            }
        } else if (nd->type == IROLinearOp1Arg) {
            if (nd->nodetype == EPREINC || nd->nodetype == EPOSTINC) {
                if (ind->addConst == 1) {
                    loop->flags |= LP_LOOP_STEP_ISADD;
                }
                if (ind->addConst > 0) {
                    loop->flags |= LP_LOOP_STEP_ISPOS;
                }
            }
            if (nd->nodetype == EPREDEC || nd->nodetype == EPOSTDEC) {
                if (ind->addConst == 1) {
                    loop->flags |= LP_STEP_IS_NEG_ONE;
                }
                if (ind->addConst > 0) {
                    loop->flags |= LP_LOOP_STEP_ISNEG;
                }
            }
        }
        loop->hi = nd->index;
        loop->lo = IRO_FindStart(nd)->index;
    }

    if (testInd) {
        condStart = IRO_FindStart(fnode->last->u.label.cond);
        loop->flags |= LP_HEADER_FOLLOWS_UPDATE;
        if (loop->flags & LP_WHILE_LOOP) {
            for (scannd = loop->fnode->first; scannd && scannd != condStart;
                 scannd = scannd->next) {
                if (scannd->type != IROLinearLabel && scannd->type != IROLinearNop) {
                    loop->flags &= ~LP_HEADER_FOLLOWS_UPDATE;
                }
            }
        } else {
            for (scannd = testInd->nd->next; scannd && scannd != condStart; scannd = scannd->next) {
                if (scannd->type != IROLinearLabel && scannd->type != IROLinearNop) {
                    loop->flags &= ~LP_HEADER_FOLLOWS_UPDATE;
                }
            }
            for (scannd2 = loop->fnode->first; scannd2 && scannd2 != condStart;
                 scannd2 = scannd2->next) {
                if ((scannd2->index < loop->lo || scannd2->index > loop->hi) &&
                    scannd2->type != IROLinearLabel &&
                    scannd2->type != IROLinearNop) {
                    loop->flags &= ~LP_HEADER_FOLLOWS_UPDATE;
                }
            }
        }
    }

    for (scanfnode = IRO_FirstNode; scanfnode; scanfnode = scanfnode->nextnode) {
        if (Bv_IsBitSet(scanfnode->index, InLoop) &&
            scanfnode != fnode &&
            (scannd2 = scanfnode->first)) {
            while (1) {
                if (scannd2->type == IROLinearFunccall) {
                    hasCalls = TRUE;
                }
                if (scannd2->type == IROLinearGoto) {
                    gotoCount++;
                }
                if (gotoCount >= 1) {
                    hasCntrlFlow = TRUE;
                }
                if (scannd2->type == IROLinearIf ||
                    scannd2->type == IROLinearIfNot ||
                    scannd2->type == IROLinearSwitch ||
                    scannd2->type == IROLinearReturn) {
                    hasCntrlFlow = TRUE;
                }

                if (scannd2->type == IROLinearAsm) {
                    loop->flags |= LP_LOOP_HAS_ASM;
                }

                if (!(scannd2->flags & IROLF_Reffed) &&
                    scannd2->type != IROLinearNop &&
                    scannd2->type != IROLinearLabel) {
                    if (!IRO_IsAssignOp[scannd2->nodetype]) {
                        loop->flags |= LP_HAS_NONASSIGN;
                    } else if (scannd2->index < loop->lo || scannd2->index > loop->hi) {
                        assignCount++;
                        if (IsAssignmentReductionCandidate(scannd2) && assignCount == 1) {
                            loop->flags |= LP_IS_REDUCTION_CAND;
                        }
                        if (assignCount > 1) {
                            loop->flags &= ~LP_IS_REDUCTION_CAND;
                        }
                    }
                }

                if (scannd2->index < loop->lo || scannd2->index > loop->hi) {
                    if (IS_LINEAR_ENODE(scannd2, EOBJREF)) {
                        obj = scannd2->u.node->data.objref;
                        if ((scannd2->flags & IROLF_Ind) &&
                            obj &&
                            obj == loop->induction->var->object) {
                            if (!(scannd2->flags & IROLF_Assigned) ||
                                (scannd2->flags & IROLF_Used)) {
                                IRO_Dump("Induction Used in loop\n");
                                loop->flags |= LP_IND_USED_IN_LOOP;
                            }
                        }
                    }

                    if (IS_LINEAR_DIADIC(scannd2, ESHR) &&
                        (obj = IRO_IsVariable(scannd2->u.diadic.left)) &&
                        IRO_IsIntConstant(scannd2->u.diadic.right)) {
                        for (scanind = FirstInd; scanind; scanind = scanind->next) {
                            if (scanind->var->object == obj) {
                                IRO_Dump("Induction has DIV: %s\n", obj->name->name);
                                scanind->flags |= LoopInd_HasDiv;
                            }
                        }
                    }

                    if (IS_LINEAR_DIADIC(scannd2, EAND) &&
                        (obj = IRO_IsVariable(scannd2->u.diadic.left)) &&
                        IRO_IsIntConstant(scannd2->u.diadic.right)) {
                        for (scanind = FirstInd; scanind && obj; scanind = scanind->next) {
                            if (scanind->var->object == obj) {
                                IRO_Dump("Induction has MOD: %s\n", obj->name->name);
                                scanind->flags |= LoopInd_HasMod;
                            }
                        }
                    }
                }

                ComputeIntWeight(loop, scannd2);
                if (scannd2 == scanfnode->last) {
                    break;
                }
                scannd2 = scannd2->next;
            }
        }

        if (hasCalls) {
            loop->flags |= LP_LOOP_HAS_CALLS;
        }
        if (hasCntrlFlow) {
            loop->flags |= LP_LOOP_HAS_CNTRLFLOW;
        }

        for (i = 0; i < scanfnode->numsucc && scanfnode != fnode; i++) {
            if (Bv_IsBitSet(scanfnode->index, InLoop) && !Bv_IsBitSet(scanfnode->succ[i], InLoop)) {
                IRO_Dump(
                    "Node %d has an out of loop successor %d\n",
                    scanfnode->index,
                    scanfnode->succ[i]
                );
                IRO_DumpBits("Loop includes: ", InLoop);
                IRO_Dump("loop has multiple exits\n");
                loop->flags |= LP_HAS_MULTIPLE_EXITS;
            }
        }
    }

    return loop;
}

CLabel* BuildLabel(IROList* list) {
    IROLinear* nd;

    nd = IRO_NewLinear(IROLinearLabel);
    nd->index = IRO_NumLinear++;
    nd->u.label.label = IRO_NewLabel();
    nd->flags |= IROLF_StartsNode;
    IRO_AddToList(nd, list);
    return nd->u.label.label;
}

static void MoveInvarianceInAddressExpr(void) {
    IROLinear* nd;
    IRONode* fnode;
    IROLinear* start;
    IROList list;

    IRO_FirstExpr = IRO_LastExpr = NULL;
    IRO_NumExprs = 0;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (Bv_IsBitSet(fnode->index, InLoop)) {
            for (nd = fnode->first; nd; nd = nd->next) {
                if (nd->type == IROLinearOp1Arg &&
                    // (the original tests EINDIRECT twice)
                    (nd->nodetype == EINDIRECT || nd->nodetype == EINDIRECT) &&
                    IS_LINEAR_DIADIC(nd->u.monadic, EADD)) {
                    RearrangeInvarianceInAddressExpr(nd->u.monadic, &list);
                    start = IRO_FindStart(nd->u.monadic);
                    IRO_LocateFather_Cut_And_Paste(nd->u.monadic, list.tail);
                    IRO_Paste(list.head, list.tail, start);
                }

                if (nd == fnode->last) {
                    break;
                }
            }
        }
    }

    IRO_UpdateFlagsOnInts();
    ComputeLoopInvariance();
}

static void AddAddendLinear(IROLinear* nd) {
    IROElmList* list;

    if (IS_LINEAR_DIADIC(nd, EADD)) {
        AddAddendLinear(nd->u.diadic.left);
        AddAddendLinear(nd->u.diadic.right);
    } else {
        list = oalloc(sizeof(IROElmList));
        list->element = nd;
        list->next = NULL;
        if (FirstAddendLinear) {
            LastAddendLinear->next = list;
        } else {
            FirstAddendLinear = list;
        }
        LastAddendLinear = list;
    }
}

// TODO: not exact: the original stores `elist = NULL` before `result`.
static IROLinear* RearrangeInvarianceInAddressExpr(IROLinear* nd, IROList* list) {
    IROLinear* scannd;
    IROLinear* result;
    IROElmList* scanlist;
    IROElmList* elist;

    IRO_InitList(list);
    FirstAddendLinear = LastAddendLinear = NULL;
    AddAddendLinear(nd->u.diadic.left);
    AddAddendLinear(nd->u.diadic.right);

    result = NULL;
    for (scanlist = FirstAddendLinear, elist = NULL; scanlist; scanlist = scanlist->next) {
        scannd = scanlist->element;
        if (!IRO_IsIntConstant(scannd) && (scannd->flags & IROLF_LoopInvariant)) {
            if (result) {
                IROLinear* sum = IRO_NewLinear(IROLinearOp2Arg); // NOTE: invented name
                sum->index = ++IRO_NumLinear;
                sum->nodetype = EADD;
                sum->u.diadic.left = result;
                sum->u.diadic.right = IRO_DuplicateExpr(scannd, list);
                IRO_AddToList(sum, list);
                sum->flags |= IROLF_LoopInvariant;
                sum->flags |= IROLF_Reffed;
                sum->rtype = result->rtype;
                result = sum;
            } else {
                result = IRO_DuplicateExpr(scannd, list);
            }

            if (elist) {
                elist->next = scanlist->next;
            } else {
                FirstAddendLinear = scanlist->next;
            }
        } else {
            elist = scanlist;
        }
    }

    for (scanlist = FirstAddendLinear, elist = NULL; scanlist; scanlist = scanlist->next) {
        scannd = scanlist->element;
        if (!IRO_IsIntConstant(scannd)) {
            if (result) {
                IROLinear* sum = IRO_NewLinear(IROLinearOp2Arg);
                sum->index = ++IRO_NumLinear;
                sum->nodetype = EADD;
                sum->u.diadic.left = result;
                sum->u.diadic.right = IRO_DuplicateExpr(scannd, list);
                IRO_AddToList(sum, list);
                sum->flags |= IROLF_Reffed;
                sum->rtype = result->rtype;
                result = sum;
            } else {
                result = IRO_DuplicateExpr(scannd, list);
            }

            if (elist) {
                elist->next = scanlist->next;
            } else {
                FirstAddendLinear = scanlist->next;
            }
        } else {
            elist = scanlist;
        }
    }

    for (scanlist = FirstAddendLinear; scanlist; scanlist = scanlist->next) {
        scannd = scanlist->element;
        if (result) {
            IROLinear* sum = IRO_NewLinear(IROLinearOp2Arg);
            sum->index = ++IRO_NumLinear;
            sum->nodetype = EADD;
            sum->u.diadic.left = result;
            sum->u.diadic.right = scannd; // (the original's: overwritten at once)
            sum->u.diadic.right = IRO_DuplicateExpr(scannd, list);
            IRO_AddToList(sum, list);
            sum->flags |= IROLF_Reffed;
            sum->rtype = result->rtype;
            result = sum;
        } else {
            result = IRO_DuplicateExpr(scannd, list);
        }
    }

    return result;
}

// NOTE: invented names: target, sum, inLeft, subInLeft
static IROLinear* FindAddendForReductionPattern(
    IROLinear* target, IROLinear* sum, Boolean* inLeft
) {
    IROLinear* left;
    IROLinear* right;
    IROLinear* inner;
    Boolean subInLeft;

    left = sum->u.diadic.left;
    right = sum->u.diadic.right;
    if (sum->nodetype == EADD || sum->nodetype == ESUB) {
        inner = left;
        while (IS_LINEAR_MONADIC(inner, ETYPCON)) {
            inner = inner->u.monadic;
        }

        if (IS_LINEAR_MONADIC(inner, EINDIRECT) &&
            (inner->u.monadic->flags & IROLF_LoopInvariant) &&
            IRO_ExprsSame(target, inner)) {
            *inLeft = TRUE;
            return left;
        }

        if (IS_LINEAR_DIADIC_2(inner, EADD, ESUB)) {
            if ((inner = FindAddendForReductionPattern(target, inner, &subInLeft))) {
                *inLeft = TRUE;
                return inner;
            }
        }
    }

    *inLeft = FALSE;
    if (sum->nodetype == EADD) {
        inner = right;
        while (IS_LINEAR_MONADIC(inner, ETYPCON)) {
            inner = inner->u.monadic;
        }

        if (IS_LINEAR_MONADIC(inner, EINDIRECT) &&
            (inner->u.monadic->flags & IROLF_LoopInvariant) &&
            IRO_ExprsSame(target, inner)) {
            return right;
        }

        if (IS_LINEAR_DIADIC_2(inner, EADD, ESUB)) {
            if ((inner = FindAddendForReductionPattern(target, inner, &subInLeft))) {
                return inner;
            }
        }
    }

    return NULL;
}

// NOTE: invented names: target, sum, swap, addendInLeft
static void ReorderOperandsForReductionPattern(IROLinear* target, IROLinear* sum) {
    IROLinear* left;
    IROLinear* right;
    IROLinear* addend;
    IROLinear* swap;
    Boolean addendInLeft;

    left = sum->u.diadic.left;
    right = sum->u.diadic.right;

    if (sum->nodetype == EADD &&
        (addend = FindAddendForReductionPattern(target, sum, &addendInLeft)) &&
        addend != left) {
        if (addendInLeft && IS_LINEAR_DIADIC_2(left, EADD, ESUB) && addend->rtype == right->rtype) {
            swap = left;
            sum->u.diadic.left = right;
            left = sum->u.diadic.left;
            sum->u.diadic.right = swap;
            right = sum->u.diadic.right;
            addendInLeft = FALSE;
        }

        if (!addendInLeft &&
            IS_LINEAR_DIADIC_2(right, EADD, ESUB) &&
            addend->rtype == left->rtype) {
#if VERSION < VERSION_GC_2_5
            if (IRO_LocateFather_Cut_And_Paste_Without_Nopping(addend, left)) {
                sum->u.diadic.left = addend;
            }
#else
            // 2.5 and later move the operands as copies
            IROList list;
            IRO_InitList(&list);
            IRO_DuplicateExpr(left, &list);
            IRO_NopOut(left);
            IRO_PasteAfter(list.head, list.tail, addend);
            if (IRO_LocateFather_Cut_And_Paste_Without_Nopping(addend, list.tail)) {
                IRO_InitList(&list);
                IRO_DuplicateExpr(addend, &list);
                IRO_NopOut(addend);
                IRO_Paste(list.head, list.tail, sum);
                sum->u.diadic.left = list.tail;
            }
#endif
        }
    }
}

static UInt32 IsAssignmentReductionCandidate(IROLinear* nd) {
    IROLinear* left;
    IROLinear* right;
    IROLinear* operand; // NOTE: invented name

    if (nd->type == IROLinearOp2Arg) {
        if (nd->nodetype == EASS) {
            left = nd->u.diadic.left;
            right = nd->u.diadic.right;
            if (IS_LINEAR_MONADIC(left, EINDIRECT) &&
                (left->u.monadic->flags & IROLF_LoopInvariant)) {
                if (IS_LINEAR_MONADIC(right, ETYPCON) &&
                    right->rtype->type == right->u.monadic->rtype->type &&
                    IS_TYPE_INT(right->rtype) &&
                    right->rtype->size < right->u.monadic->rtype->size) {
                    right = right->u.monadic;
                }

                if (IS_LINEAR_DIADIC_2(right, EADD, ESUB)) {
                    ReorderOperandsForReductionPattern(left, right);
                    operand = right->u.diadic.left;
                    if (IS_LINEAR_MONADIC(operand, ETYPCON)) {
                        operand = operand->u.monadic;
                    }
                    if (IS_LINEAR_MONADIC(operand, EINDIRECT) &&
                        (operand->u.monadic->flags & IROLF_LoopInvariant) &&
                        IRO_ExprsSame(left, operand) &&
                        right->u.diadic.right->type == IROLinearOp2Arg) {
                        if (right->u.diadic.right->nodetype == EADD) {
                            return TRUE;
                        }
                        if (right->u.diadic.right->nodetype == ESUB) {
                            return TRUE;
                        }
                        if (right->u.diadic.right->nodetype == EMUL) {
                            return TRUE;
                        }
                        if (right->u.diadic.right->nodetype == EDIV) {
                            return TRUE;
                        }
                    }
                }
            }
        } else if (nd->nodetype == EADDASS || nd->nodetype == ESUBASS) {
            left = nd->u.diadic.left;
            right = nd->u.diadic.right;

            if (IS_LINEAR_MONADIC(right, ETYPCON) &&
                right->rtype->type == right->u.monadic->rtype->type &&
                IS_TYPE_INT(right->rtype) &&
                right->rtype->size < right->u.monadic->rtype->size) {
                right = right->u.monadic;
            }

            if (IS_LINEAR_MONADIC(left, EINDIRECT) &&
                (left->u.monadic->flags & IROLF_LoopInvariant) &&
                right->type == IROLinearOp2Arg) {
                if (right->nodetype == EADD) {
                    return TRUE;
                }
                if (right->nodetype == ESUB) {
                    return TRUE;
                }
                if (right->nodetype == EMUL) {
                    return TRUE;
                }
                if (right->nodetype == EDIV) {
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

// The unit's statics are 2-byte packed, as in the original (MWCC lays out .bss with the packing in
// force at the end of the file)
#pragma pack(2)
