#include "compiler/IroRangePropagation.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroMalloc.h"
#include "compiler/IroPointerAnalysis.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/CError.h"
#include "compiler/CInt64.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/types.h"

// NOTE: invented names: ERangeType, ERangeConstant, ERangeBounded, ERangeComputed, ERangeUnknown
// (only ERangeUnknown is ever tested)
typedef enum ERangeType {
    ERangeConstant, // an operand constant (an integer's range is its value)
    ERangeBounded, // 0 to a bound: a mask, or a comparison's 0 or 1
    ERangeComputed, // derived from the operands' ranges
    ERangeUnknown // no range: unknown, or overflows the type
} ERangeType;

#pragma pack(push, 2)
typedef struct ERange {
    ERangeType type;
    CInt64 upper;
    CInt64 lower;
} ERange;

typedef struct ERecord {
    Object* object;
    ERange* range;
    struct ERecord* next;
} ERecord;
#pragma pack(pop)

static ERecord* ERangeFirst;
static ERecord* ERangeLast;

static ERange* ERnewERange(ERangeType type) {
    ERange* range;

    range = oalloc(sizeof(ERange));
    range->type = type;
    return range;
}

static ERecord* ERnewRecord(Object* object, ERange* range) {
    ERecord* record;

    record = oalloc(sizeof(ERecord));
    record->object = object;
    record->range = range;
    record->next = ERangeFirst;
    ERangeFirst = record;
    if (!ERangeLast) {
        ERangeLast = record;
    }
    return record;
}

static ERecord* ERecordFound(Object* obj) {
    ERecord* scan;

    scan = ERangeFirst;
    while (scan && obj != scan->object) {
        scan = scan->next;
    }

    return scan;
}

static Boolean EREandHasNoUse(ERange* range, CInt64 val) {
    UInt16 i;
    CInt64 mask; // NOTE: invented name
    CInt64 work;

    i = 0;
    work = range->upper;
    while (CInt64_NotEqual(work = CInt64_ShrU(work, cint64_one), cint64_zero)) {
        i++;
    }

    if (CInt64_NotEqual(range->upper, cint64_zero)) {
        i++;
    }

    CInt64_SetULong(&work, i);
    mask = CInt64_Sub(CInt64_Shl(cint64_one, work), cint64_one);
    if (CInt64_NotEqual(cint64_zero, CInt64_And(CInt64_Inv(val), mask))) {
        return FALSE;
    } else {
        return TRUE;
    }
}

static void ERcheckOverflow(ERange* range, Type* type) {
    CInt64 typeSize;
    CInt64 work;
    CInt64 work2;
    CInt64 three; // NOTE: invented name

    if (!range) {
        return;
    }

    if (!IS_TYPE_INT(type)) {
        range->type = ERangeUnknown;
        return;
    }

    CInt64_SetLong(&typeSize, type->size);
    CInt64_SetLong(&three, 3);

    if (is_unsigned(type)) {
        if (type->size < 8) {
            work = CInt64_Sub(CInt64_Shl(cint64_one, CInt64_Shl(typeSize, three)), cint64_one);
            if (CInt64_GreaterU(range->upper, work)) {
                range->type = ERangeUnknown;
            }
        } else {
            range->type = ERangeUnknown;
        }
    } else {
        if (type->size < 8) {
            work = CInt64_Sub(
                CInt64_Shl(cint64_one, CInt64_Sub(CInt64_Shl(typeSize, three), cint64_one)),
                cint64_one
            );
            work2 = CInt64_Shl(cint64_negone, CInt64_Sub(CInt64_Shl(typeSize, three), cint64_one));
            if (CInt64_Greater(range->upper, work) || CInt64_Less(range->lower, work2)) {
                range->type = ERangeUnknown;
            }
        } else {
            range->type = ERangeUnknown;
        }
    }
}

static void ERinvalidAll(void) {
    ERecord* record;

    for (record = ERangeFirst; record; record = record->next) {
        record->range->type = ERangeUnknown;
    }
}

static void SetRangesForKillsByIndirectAssignment(IROLinear* nd) {
    IROListNode* list;
    IROListNode* scan;
    IROLinear* inner;
    Boolean failed;
    IROListNode* resultList;
    IROLinear* scannd;
    Boolean foundObjRef;
    ERecord* record;
    ERange* range;
    IROListNode* next;
    Object* obj;
    IROLinear* analysend;

    failed = FALSE;
    if (nd->type == IROLinearOp2Arg) {
        analysend = nd->u.diadic.left;
    } else {
        analysend = nd->u.monadic;
    }

    inner = analysend;
    if (inner &&
        inner->type == IROLinearOp1Arg &&
        inner->nodetype == EINDIRECT &&
        (analysend = inner->u.monadic) &&
        copts.opt_pointer_analysis &&
        analysend->pointsToFunction &&
        FunctionName) {
        resultList = NULL;
        PointerAnalysis_LookupLinearNodePointerExpr(FunctionName, analysend, &resultList);

        if (resultList) {
            for (scan = resultList; scan; scan = scan->nextList) {
                if (!scan->list.head || !scan->list.tail) {
                    failed = TRUE;
                    break;
                }

                foundObjRef = FALSE;
                for (scannd = scan->list.head; scannd != scan->list.tail->next;
                     scannd = scannd->next) {
                    if (scannd->type == IROLinearOperand && scannd->u.node->type == EOBJREF) {
                        foundObjRef = TRUE;
                        break;
                    }
                }

                if (!foundObjRef) {
                    failed = TRUE;
                    break;
                }
            }

            if (!failed) {
                for (list = resultList; list; list = list->nextList) {
                    for (scannd = list->list.head; scannd != list->list.tail->next;
                         scannd = scannd->next) {
                        if (scannd->type == IROLinearOperand && scannd->u.node->type == EOBJREF) {
                            obj = scannd->u.node->data.objref;
                            CError_ASSERT(302, obj != NULL);

                            range = nd->range;
                            if (nd->nodetype == EPOSTINC || nd->nodetype == EPOSTDEC) {
                                range = inner->range;
                            }

                            if (!(record = ERecordFound(obj))) {
                                ERnewRecord(obj, range);
                            } else {
                                record->range = range;
                            }
                        }
                    }
                }
            }

            while (resultList) {
                next = resultList->nextList;
                IRO_free(resultList);
                resultList = next;
            }
        } else {
            failed = TRUE;
        }
    } else {
        failed = TRUE;
    }

    if (failed) {
        ERinvalidAll();
        nd->range = ERnewERange(ERangeUnknown);
    }
}

static void InvalidateRangesForKillsByFunctionCall(IROLinear* nd) {
    IROListNode* scan;
    IROLinear* scannd;
    Boolean failed;
    Boolean foundObjRef;
    IROListNode* list;
    IROListNode* resultList;
    ERecord* record;
    IROListNode* next;
    Object* obj;
    IROLinear* analysend;
    ObjectList* olist;
    ObjectList* killList;

    failed = FALSE;

    if ((analysend = nd->u.funccall.callee) &&
        copts.opt_pointer_analysis &&
        analysend->pointsToFunction &&
        FunctionName) {
        resultList = NULL;
        PointerAnalysis_LookupLinearNodePointerExpr(FunctionName, analysend, &resultList);

        if (resultList) {
            for (scan = resultList; scan; scan = scan->nextList) {
                if (!scan->list.head || !scan->list.tail) {
                    failed = TRUE;
                    break;
                }

                foundObjRef = FALSE;
                for (scannd = scan->list.head; scannd != scan->list.tail->next;
                     scannd = scannd->next) {
                    if (scannd->type == IROLinearOperand && scannd->u.node->type == EOBJREF) {
                        foundObjRef = TRUE;
                        obj = scannd->u.node->data.objref;
                        CError_ASSERT(385, obj != NULL);

                        killList = NULL;
                        PointerAnalysis_GetFunctionKills(obj, nd, &killList);

                        for (olist = killList; olist; olist = olist->next) {
                            if (!olist->object) {
                                failed = TRUE;
                                break;
                            }
                        }

                        while (killList) {
                            olist = killList->next;
                            IRO_free(killList);
                            killList = olist;
                        }

                        if (failed) {
                            break;
                        }
                    }
                }

                if (!foundObjRef) {
                    failed = TRUE;
                }
                if (failed) {
                    break;
                }
            }

            if (!failed) {
                for (list = resultList; list; list = list->nextList) {
                    for (scannd = list->list.head; scannd != list->list.tail->next;
                         scannd = scannd->next) {
                        if (scannd->type == IROLinearOperand && scannd->u.node->type == EOBJREF) {
                            obj = scannd->u.node->data.objref;
                            killList = NULL;
                            PointerAnalysis_GetFunctionKills(obj, nd, &killList);

                            for (olist = killList; olist; olist = olist->next) {
                                record = ERecordFound(olist->object);
                                if (record) {
                                    record->range->type = ERangeUnknown;
                                }
                            }

                            while (killList) {
                                olist = killList->next;
                                IRO_free(killList);
                                killList = olist;
                            }
                        }
                    }
                }
            }

            while (resultList) {
                next = resultList->nextList;
                IRO_free(resultList);
                resultList = next;
            }
        } else {
            failed = TRUE;
        }
    } else {
        failed = TRUE;
    }

    if (failed) {
        ERinvalidAll();
    }
}

static void ERfoldOperand(IROLinear* nd) {
    switch (nd->u.node->type) {
        case EOBJREF:
            nd->range = NULL;
            break;
        case EINTCONST:
            nd->range = ERnewERange(ERangeConstant);
            nd->range->upper = nd->range->lower = nd->u.node->data.intval;
            break;
        case EFLOATCONST:
        case ESTRINGCONST:
            nd->range = ERnewERange(ERangeConstant);
            break;
        default:
            break;
    }
}

static Boolean ERfoldExpr(IROLinear* nd) {
    ERecord* record;
    ERange* range;
    IROLinear* operand; // NOTE: invented name
    IROLinear* inner;
    Object* obj;

    switch (nd->nodetype) {
        case EINDIRECT:
            inner = nd->u.monadic;
            if (IS_TYPE_INT(nd->rtype)) {
                if (inner->type == IROLinearOperand && inner->u.node->type == EOBJREF) {
                    if (!inner->range && (obj = inner->u.node->data.objref)) {
                        if ((record = ERecordFound(obj))) {
                            inner->range = ERnewERange(ERangeUnknown);
                            *inner->range = *record->range;
                        } else {
                            inner->range = ERnewERange(ERangeUnknown);
                            inner->range->upper = cint64_max;
                            inner->range->lower = cint64_min;
                            ERnewRecord(obj, inner->range);
                        }
                    }
                    nd->range = inner->range;
                } else {
                    nd->range = ERnewERange(ERangeUnknown);
                    nd->range->upper = cint64_max;
                    nd->range->lower = cint64_min;
                }
            } else {
                if (inner->type == IROLinearOperand &&
                    inner->u.node->type == EOBJREF &&
                    !inner->range) {
                    inner->range = ERnewERange(ERangeUnknown);
                }
                nd->range = ERnewERange(ERangeUnknown);
            }
            break;
        case EAND:
        case EANDASS:
            if (IRO_IsIntConstant(nd->u.diadic.right)) {
                CInt64 val = nd->u.diadic.right->u.node->data.intval;
                nd->range = ERnewERange(ERangeBounded);
                nd->range->upper = val;
                nd->range->lower = cint64_zero;
                if ((range = nd->u.diadic.left->range) &&
                    range->type != ERangeUnknown &&
                    CInt64_LessEqualU(range->upper, val) &&
                    CInt64_LessEqualU(range->lower, val) &&
                    EREandHasNoUse(range, val) &&
                    !IRO_HasSideEffect(nd->u.diadic.left)) {
                    // the original's: it prints the upper bound as the lower one too
                    IRO_Dump(
                        "eliminating redundant EAND %ld; upperBound==0x%x, lowerBound==0x%x, Constant==0x%x\n",
                        nd->index,
                        CInt64_GetULong(&range->upper),
                        CInt64_GetULong(&range->upper),
                        CInt64_GetULong(&val)
                    );
                    IRO_NopOut(nd->u.diadic.right);
                    nd->type = IROLinearNop;
                    nd->expr = NULL;
                    operand = nd->u.diadic.left;
                    nd->u.diadic.left = nd->u.diadic.right;
                    if (!IRO_LocateFather_Cut_And_Paste(nd, operand)) {
                        operand->flags &= ~IROLF_Reffed;
                        if (IRO_IsVariable(operand)) {
                            IRO_NopOut(operand);
                        }
                    }
                }
            } else {
                if (nd->u.diadic.right->range) {
                    nd->range = ERnewERange(ERangeUnknown);
                    *nd->range = *nd->u.diadic.right->range;
                }
            }
            ERcheckOverflow(nd->range, nd->rtype);
            break;
        case ELOGNOT:
        case ELESS:
        case EGREATER:
        case ELESSEQU:
        case EGREATEREQU:
        case EEQU:
        case ENOTEQU:
        case ELAND:
        case ELOR:
            nd->range = ERnewERange(ERangeBounded);
            nd->range->upper = cint64_one;
            nd->range->lower = cint64_zero;
            ERcheckOverflow(nd->range, nd->rtype);
            break;
        case EBINNOT:
        case EFORCELOAD:
        case EXOR:
        case EOR:
        case EXORASS:
        case EORASS:
        case ECOMMA:
        case ETYPCON:
        case EBITFIELD:
        case ECOND:
        case ENULLCHECK:
            nd->range = ERnewERange(ERangeUnknown);
            nd->range->upper = cint64_max;
            nd->range->lower = cint64_min;
            break;
        case EASS:
            if (IS_TYPE_INT(nd->rtype)) {
                nd->range = nd->u.diadic.right->range;
            }
            break;
        case EMUL:
        case EMULV:
        case EMULASS:
            if (IS_TYPE_INT(nd->rtype)) {
                if (nd->u.diadic.left->range &&
                    nd->u.diadic.left->range->type != ERangeUnknown &&
                    nd->u.diadic.right->range &&
                    nd->u.diadic.right->range->type != ERangeUnknown) {
                    nd->range = ERnewERange(ERangeComputed);
                    if (is_unsigned(nd->rtype)) {
                        nd->range->upper = CInt64_MulU(
                            nd->u.diadic.left->range->upper, nd->u.diadic.right->range->upper
                        );
                        nd->range->lower = CInt64_MulU(
                            nd->u.diadic.left->range->lower, nd->u.diadic.right->range->lower
                        );
                    } else {
                        nd->range->upper = CInt64_Mul(
                            nd->u.diadic.left->range->upper, nd->u.diadic.right->range->upper
                        );
                        nd->range->lower = CInt64_Mul(
                            nd->u.diadic.left->range->lower, nd->u.diadic.right->range->lower
                        );
                    }
                } else {
                    nd->range = ERnewERange(ERangeUnknown);
                    nd->range->upper = cint64_max;
                    nd->range->lower = cint64_min;
                }
            }
            ERcheckOverflow(nd->range, nd->rtype);
            break;
        case EDIV:
        case EDIVASS:
            if (IS_TYPE_INT(nd->rtype)) {
                if (nd->u.diadic.left->range &&
                    nd->u.diadic.left->range->type != ERangeUnknown &&
                    nd->u.diadic.right->range &&
                    nd->u.diadic.right->range->type != ERangeUnknown) {
                    nd->range = ERnewERange(ERangeComputed);
                    if (!CInt64_IsZero(&nd->u.diadic.right->range->lower) &&
                        !CInt64_IsZero(&nd->u.diadic.right->range->upper)) {
                        if (is_unsigned(nd->rtype)) {
                            nd->range->upper = CInt64_DivU(
                                nd->u.diadic.left->range->upper, nd->u.diadic.right->range->lower
                            );
                            nd->range->lower = CInt64_DivU(
                                nd->u.diadic.left->range->lower, nd->u.diadic.right->range->upper
                            );
                        } else {
                            nd->range->upper = CInt64_Div(
                                nd->u.diadic.left->range->upper, nd->u.diadic.right->range->lower
                            );
                            nd->range->lower = CInt64_Div(
                                nd->u.diadic.left->range->lower, nd->u.diadic.right->range->upper
                            );
                        }
                    } else {
                        nd->range = ERnewERange(ERangeUnknown);
                        nd->range->upper = cint64_max;
                        nd->range->lower = cint64_min;
                    }
                } else {
                    nd->range = ERnewERange(ERangeUnknown);
                    nd->range->upper = cint64_max;
                    nd->range->lower = cint64_min;
                }
            }
            ERcheckOverflow(nd->range, nd->rtype);
            break;
        case EMODULO:
        case EMODASS:
            if (IS_TYPE_INT(nd->rtype)) {
                if (nd->u.diadic.left->range &&
                    nd->u.diadic.left->range->type != ERangeUnknown &&
                    nd->u.diadic.right->range &&
                    nd->u.diadic.right->range->type != ERangeUnknown) {
                    nd->range = ERnewERange(ERangeComputed);
                    if (!CInt64_IsZero(&nd->u.diadic.right->range->lower) &&
                        !CInt64_IsZero(&nd->u.diadic.right->range->upper)) {
                        if (is_unsigned(nd->rtype)) {
                            nd->range->upper = CInt64_ModU(
                                nd->u.diadic.left->range->upper, nd->u.diadic.right->range->lower
                            );
                            nd->range->lower = CInt64_ModU(
                                nd->u.diadic.left->range->lower, nd->u.diadic.right->range->upper
                            );
                        } else {
                            nd->range->upper = CInt64_Mod(
                                nd->u.diadic.left->range->upper, nd->u.diadic.right->range->lower
                            );
                            nd->range->lower = CInt64_Mod(
                                nd->u.diadic.left->range->lower, nd->u.diadic.right->range->upper
                            );
                        }
                    } else {
                        nd->range = ERnewERange(ERangeUnknown);
                        nd->range->upper = cint64_max;
                        nd->range->lower = cint64_min;
                    }
                } else {
                    nd->range = ERnewERange(ERangeUnknown);
                    nd->range->upper = cint64_max;
                    nd->range->lower = cint64_min;
                }
            }
            ERcheckOverflow(nd->range, nd->rtype);
            break;
        case EADDV:
        case EADD:
        case EADDASS:
            if (IS_TYPE_INT(nd->rtype)) {
                if (nd->u.diadic.left->range &&
                    nd->u.diadic.left->range->type != ERangeUnknown &&
                    nd->u.diadic.right->range &&
                    nd->u.diadic.right->range->type != ERangeUnknown) {
                    nd->range = ERnewERange(ERangeComputed);
                    nd->range->upper = CInt64_Add(
                        nd->u.diadic.left->range->upper, nd->u.diadic.right->range->upper
                    );
                    nd->range->lower = CInt64_Add(
                        nd->u.diadic.left->range->lower, nd->u.diadic.right->range->lower
                    );
                } else {
                    nd->range = ERnewERange(ERangeUnknown);
                    nd->range->upper = cint64_max;
                    nd->range->lower = cint64_min;
                }
            }
            ERcheckOverflow(nd->range, nd->rtype);
            break;
        case ESUBV:
        case ESUB:
        case ESUBASS:
            if (IS_TYPE_INT(nd->rtype)) {
                if (nd->u.diadic.left->range &&
                    nd->u.diadic.left->range->type != ERangeUnknown &&
                    nd->u.diadic.right->range &&
                    nd->u.diadic.right->range->type != ERangeUnknown) {
                    nd->range = ERnewERange(ERangeComputed);
                    nd->range->upper = CInt64_Sub(
                        nd->u.diadic.left->range->upper, nd->u.diadic.right->range->lower
                    );
                    nd->range->lower = CInt64_Sub(
                        nd->u.diadic.left->range->lower, nd->u.diadic.right->range->upper
                    );
                } else {
                    nd->range = ERnewERange(ERangeUnknown);
                    nd->range->upper = cint64_max;
                    nd->range->lower = cint64_min;
                }
            }
            ERcheckOverflow(nd->range, nd->rtype);
            break;
        case ESHL:
        case ESHLASS:
            if (IS_TYPE_INT(nd->rtype)) {
                if (nd->u.diadic.left->range &&
                    nd->u.diadic.left->range->type != ERangeUnknown &&
                    nd->u.diadic.right->range &&
                    nd->u.diadic.right->range->type != ERangeUnknown) {
                    nd->range = ERnewERange(ERangeComputed);
                    nd->range->upper = CInt64_Shl(
                        nd->u.diadic.left->range->upper, nd->u.diadic.right->range->upper
                    );
                    nd->range->lower = CInt64_Shl(
                        nd->u.diadic.left->range->lower, nd->u.diadic.right->range->lower
                    );
                } else {
                    nd->range = ERnewERange(ERangeUnknown);
                    nd->range->upper = cint64_max;
                    nd->range->lower = cint64_min;
                }
            }
            ERcheckOverflow(nd->range, nd->rtype);
            break;
        case ESHR:
        case ESHRASS:
            if (IS_TYPE_INT(nd->rtype)) {
                if (nd->u.diadic.left->range &&
                    nd->u.diadic.left->range->type != ERangeUnknown &&
                    nd->u.diadic.right->range &&
                    nd->u.diadic.right->range->type != ERangeUnknown) {
                    nd->range = ERnewERange(ERangeComputed);
                    if (is_unsigned(nd->rtype)) {
                        nd->range->upper = CInt64_ShrU(
                            nd->u.diadic.left->range->upper, nd->u.diadic.right->range->lower
                        );
                        nd->range->lower = CInt64_ShrU(
                            nd->u.diadic.left->range->lower, nd->u.diadic.right->range->upper
                        );
                    } else {
                        nd->range->upper = CInt64_Shr(
                            nd->u.diadic.left->range->upper, nd->u.diadic.right->range->lower
                        );
                        nd->range->lower = CInt64_Shr(
                            nd->u.diadic.left->range->lower, nd->u.diadic.right->range->upper
                        );
                    }
                } else {
                    nd->range = ERnewERange(ERangeUnknown);
                    nd->range->upper = cint64_max;
                    nd->range->lower = cint64_min;
                }
            }
            ERcheckOverflow(nd->range, nd->rtype);
            break;
        case EPOSTINC:
            if (IS_TYPE_INT(nd->rtype)) {
                if (nd->u.monadic->range && nd->u.monadic->range->type != ERangeUnknown) {
                    nd->range = ERnewERange(ERangeComputed);
                    range = nd->u.monadic->range;
                    *nd->range = *range;
                    range->upper = CInt64_Add(range->upper, cint64_one);
                    range->lower = CInt64_Add(range->lower, cint64_one);
                } else {
                    nd->range = ERnewERange(ERangeUnknown);
                    nd->range->upper = cint64_max;
                    nd->range->lower = cint64_min;
                }
            }
            ERcheckOverflow(nd->range, nd->rtype);
            break;
        case EPOSTDEC:
            if (IS_TYPE_INT(nd->rtype)) {
                if (nd->u.monadic->range && nd->u.monadic->range->type != ERangeUnknown) {
                    nd->range = ERnewERange(ERangeComputed);
                    range = nd->u.monadic->range;
                    *nd->range = *range;
                    range->upper = CInt64_Sub(range->upper, cint64_one);
                    range->lower = CInt64_Sub(range->lower, cint64_one);
                } else {
                    nd->range = ERnewERange(ERangeUnknown);
                    nd->range->upper = cint64_max;
                    nd->range->lower = cint64_min;
                }
            }
            ERcheckOverflow(nd->range, nd->rtype);
            break;
        case EPREINC:
            if (IS_TYPE_INT(nd->rtype)) {
                if (nd->u.monadic->range && nd->u.monadic->range->type != ERangeUnknown) {
                    nd->range = ERnewERange(ERangeComputed);
                    range = nd->u.monadic->range;
                    nd->range->upper = CInt64_Add(range->upper, cint64_one);
                    nd->range->lower = CInt64_Add(range->lower, cint64_one);
                } else {
                    nd->range = ERnewERange(ERangeUnknown);
                    nd->range->upper = cint64_max;
                    nd->range->lower = cint64_min;
                }
            }
            ERcheckOverflow(nd->range, nd->rtype);
            break;
        case EPREDEC:
            if (IS_TYPE_INT(nd->rtype)) {
                if (nd->u.monadic->range && nd->u.monadic->range->type != ERangeUnknown) {
                    nd->range = ERnewERange(ERangeComputed);
                    range = nd->u.monadic->range;
                    nd->range->upper = CInt64_Sub(range->upper, cint64_one);
                    nd->range->lower = CInt64_Sub(range->lower, cint64_one);
                } else {
                    nd->range = ERnewERange(ERangeUnknown);
                    nd->range->upper = cint64_max;
                    nd->range->lower = cint64_min;
                }
            }
            ERcheckOverflow(nd->range, nd->rtype);
            break;
        case EMONMIN:
            nd->range = ERnewERange(ERangeUnknown);
            nd->range->upper = cint64_max;
            nd->range->lower = cint64_min;
            break;
        case EPMODULO:
        case EROTL:
        case EROTR:
        case EBCLR:
        case EBTST:
        case EBSET:
            nd->range = ERnewERange(ERangeUnknown);
            nd->range->upper = cint64_max;
            nd->range->lower = cint64_min;
            break;
        default:
            ERcheckOverflow(nd->range, nd->rtype);
            break;
    }

    if ((nd->type == IROLinearOp1Arg || nd->type == IROLinearOp2Arg) &&
        IRO_IsAssignOp[nd->nodetype] &&
        nd->range &&
        IS_TYPE_INT(nd->rtype)) {
        IROLinear* x = NULL;
        if (nd->type == IROLinearOp2Arg) {
            x = nd->u.diadic.left;
        } else if (nd->type == IROLinearOp1Arg) {
            x = nd->u.monadic;
        }

        // the original's: `x` is tested for NULL only after this use (it is never NULL here)
        if (x->type == IROLinearOp1Arg &&
            x->nodetype == EINDIRECT &&
            (x->u.monadic->nodetype == EINDIRECT || x->u.monadic->nodetype == EADD)) {
            SetRangesForKillsByIndirectAssignment(nd);
        } else {
            obj = NULL;
            if (x) {
                obj = IRO_IsVariable(x);
            }
            if (!obj) {
                return FALSE;
            }

            range = nd->range;
            if (nd->nodetype == EPOSTINC || nd->nodetype == EPOSTDEC) {
                range = x->range;
            }

            if (!(record = ERecordFound(obj))) {
                ERnewRecord(obj, range);
            } else {
                record->range = range;
            }
        }
    }

    return nd->range != NULL;
}

static Boolean ERfoldLinear(IROLinear* nd) {
    nd->range = NULL;

    switch (nd->type) {
        case IROLinearNop:
        case IROLinearEnd:
            break;
        case IROLinearOperand:
            ERfoldOperand(nd);
            break;
        case IROLinearOp1Arg:
        case IROLinearOp2Arg:
            ERfoldExpr(nd);
            break;
        case IROLinearFunccall:
            InvalidateRangesForKillsByFunctionCall(nd);
            break;
        case IROLinearAsm:
            ERinvalidAll();
            break;
        default:
            break;
    }

    return FALSE;
}

Boolean IRO_RangePropagateInFNode(void) {
    IRONode* fnode;
    IROLinear* nd;
    Boolean result;

    result = FALSE;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        ERangeFirst = ERangeLast = NULL;
        for (nd = fnode->first; nd != fnode->last; nd = nd->next) {
            ERfoldLinear(nd);
        }
        if (ERfoldLinear(nd)) {
            result = TRUE;
        }
    }

    if (result) {
        IRO_ComputeSuccPred();
        IRO_ComputeDom();
    }

    IRO_CheckForUserBreak();
    return result;
}
