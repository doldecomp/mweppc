#include "compiler/IroEval.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroMalloc.h"
#include "compiler/IroPointerAnalysis.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/Switch.h"
#include "compiler/tokens.h"
#include "compiler/types.h"

static Boolean IsAssociativeENodeType[MAXEXPR];

void IRO_InitializeIsAssociativeENodeTypeArray(void) {
    int i;

    for (i = 0; i < MAXEXPR; i++) {
        IsAssociativeENodeType[i] = FALSE;
    }

    IsAssociativeENodeType[EPOSTINC] = FALSE;
    IsAssociativeENodeType[EPOSTDEC] = FALSE;
    IsAssociativeENodeType[EPREINC] = FALSE;
    IsAssociativeENodeType[EPREDEC] = FALSE;
    IsAssociativeENodeType[EINDIRECT] = FALSE;
    IsAssociativeENodeType[EMONMIN] = FALSE;
    IsAssociativeENodeType[EBINNOT] = FALSE;
    IsAssociativeENodeType[ELOGNOT] = FALSE;
    IsAssociativeENodeType[EFORCELOAD] = FALSE;
    IsAssociativeENodeType[EMUL] = TRUE;
    IsAssociativeENodeType[EMULV] = TRUE;
    IsAssociativeENodeType[EDIV] = FALSE;
    IsAssociativeENodeType[EMODULO] = FALSE;
    IsAssociativeENodeType[EADDV] = TRUE;
    IsAssociativeENodeType[ESUBV] = FALSE;
    IsAssociativeENodeType[EADD] = TRUE;
    IsAssociativeENodeType[ESUB] = FALSE;
    IsAssociativeENodeType[ESHL] = FALSE;
    IsAssociativeENodeType[ESHR] = FALSE;
    IsAssociativeENodeType[ELESS] = FALSE;
    IsAssociativeENodeType[EGREATER] = FALSE;
    IsAssociativeENodeType[ELESSEQU] = FALSE;
    IsAssociativeENodeType[EGREATEREQU] = FALSE;
    IsAssociativeENodeType[EEQU] = FALSE;
    IsAssociativeENodeType[ENOTEQU] = FALSE;
    IsAssociativeENodeType[EAND] = TRUE;
    IsAssociativeENodeType[EXOR] = TRUE;
    IsAssociativeENodeType[EOR] = TRUE;
    IsAssociativeENodeType[ELAND] = FALSE;
    IsAssociativeENodeType[ELOR] = FALSE;
    IsAssociativeENodeType[EASS] = FALSE;
    IsAssociativeENodeType[EMULASS] = FALSE;
    IsAssociativeENodeType[EDIVASS] = FALSE;
    IsAssociativeENodeType[EMODASS] = FALSE;
    IsAssociativeENodeType[EADDASS] = FALSE;
    IsAssociativeENodeType[ESUBASS] = FALSE;
    IsAssociativeENodeType[ESHLASS] = FALSE;
    IsAssociativeENodeType[ESHRASS] = FALSE;
    IsAssociativeENodeType[EANDASS] = FALSE;
    IsAssociativeENodeType[EXORASS] = FALSE;
    IsAssociativeENodeType[EORASS] = FALSE;
    IsAssociativeENodeType[ECOMMA] = FALSE;
    IsAssociativeENodeType[EPMODULO] = FALSE;
    IsAssociativeENodeType[EROTL] = FALSE;
    IsAssociativeENodeType[EROTR] = FALSE;
    IsAssociativeENodeType[EBCLR] = FALSE;
    IsAssociativeENodeType[EBTST] = FALSE;
    IsAssociativeENodeType[EBSET] = FALSE;
    IsAssociativeENodeType[ETYPCON] = FALSE;
    IsAssociativeENodeType[EBITFIELD] = FALSE;
    IsAssociativeENodeType[EINTCONST] = FALSE;
    IsAssociativeENodeType[EFLOATCONST] = FALSE;
    IsAssociativeENodeType[ESTRINGCONST] = FALSE;
    IsAssociativeENodeType[ECOND] = FALSE;
    IsAssociativeENodeType[EFUNCCALL] = FALSE;
    IsAssociativeENodeType[EFUNCCALLP] = FALSE;
    IsAssociativeENodeType[EOBJREF] = FALSE;
    IsAssociativeENodeType[EMFPOINTER] = FALSE;
    IsAssociativeENodeType[ENULLCHECK] = FALSE;
    IsAssociativeENodeType[EPRECOMP] = FALSE;
    IsAssociativeENodeType[ETEMP] = FALSE;
    IsAssociativeENodeType[EARGOBJ] = FALSE;
    IsAssociativeENodeType[ELOCOBJ] = FALSE;
    IsAssociativeENodeType[ELABEL] = FALSE;
    IsAssociativeENodeType[ESETCONST] = FALSE;
    IsAssociativeENodeType[ENEWEXCEPTION] = FALSE;
    IsAssociativeENodeType[ENEWEXCEPTIONARRAY] = FALSE;
    IsAssociativeENodeType[EOBJLIST] = FALSE;
    IsAssociativeENodeType[EMEMBER] = FALSE;
    IsAssociativeENodeType[ETEMPLDEP] = FALSE;
    IsAssociativeENodeType[EINSTRUCTION] = FALSE;
    IsAssociativeENodeType[EDEFINE] = FALSE;
    IsAssociativeENodeType[EREUSE] = FALSE;
    IsAssociativeENodeType[EASSBLK] = FALSE;
    IsAssociativeENodeType[EVECTOR128CONST] = FALSE;
    IsAssociativeENodeType[ECONDASS] = FALSE;
}

void IRO_TruncateValueToType(CInt64* val, Type* type) {
    if (is_unsigned(type)) {
        switch (type->size) {
            case 1:
                CInt64_ConvertUInt8(val);
                break;
            case 2:
                CInt64_ConvertUInt16(val);
                break;
            case 4:
                CInt64_ConvertUInt32(val);
                break;
        }
    } else {
        switch (type->size) {
            case 1:
                CInt64_ConvertInt8(val);
                break;
            case 2:
                CInt64_ConvertInt16(val);
                break;
            case 4:
                CInt64_ConvertInt32(val);
                break;
        }
    }
}

void IRO_TruncateBitfieldValueToType(CInt64* val, Type* type, TypeBitfield* bitfieldType) {
    UInt32 limit;
    UInt32 i;
    UInt32 j;
    CInt64 work;

    work = cint64_zero;
    limit = bitfieldType->bitlength;
    for (i = 0; i < limit; i++) {
        work = CInt64_Or(work, CInt64_Shl(cint64_one, IRO_MakeULong(i)));
    }
    *val = CInt64_And(*val, work);

    if (!is_unsigned(type)) {
        work = cint64_zero;
        for (j = 0; j <= (i - 1); j++) {
            if (j == (i - 1)) {
                work = CInt64_Or(work, CInt64_Shl(cint64_one, IRO_MakeULong(j)));
            }
        }
        if (CInt64_NotEqual(CInt64_And(work, *val), cint64_zero)) {
            for (j = i - 1; j < 64; j++) {
                *val = CInt64_Or(*val, CInt64_Shl(cint64_one, IRO_MakeULong(j)));
            }
        }
    }

    IRO_TruncateValueToType(val, type);
}

void IRO_ConstantFolding(void) {
    IROLinear* nd;
    ENode* expr;
    int isCompare;
    int folded;
    CInt64 val;

    for (nd = IRO_FirstLinear; nd; nd = nd->next) {
        switch (nd->type) {
            case IROLinearOp1Arg:
                if (IRO_IsIntConstant(nd->u.monadic)) {
                    expr = NULL;
                    folded = FALSE;
                    val = nd->u.monadic->u.node->data.intval;
                    if (nd->nodetype == ETYPCON && IS_TYPE_FLOAT(nd->rtype)) {
                        expr = IRO_NewENode(EFLOATCONST);
                        if (!is_unsigned(nd->u.monadic->rtype)) {
                            expr->data.floatval.data.value = CInt64_ConvertToLongDouble(&val);
                        } else {
                            expr->data.floatval.data.value = CInt64_ConvertUToLongDouble(&val);
                        }
                        expr->rtype = nd->rtype;
                    } else {
                        switch (nd->nodetype) {
                            case ETYPCON:
                                folded = TRUE;
                                break;
                            case ELOGNOT:
                                val = CInt64_Not(val);
                                folded = TRUE;
                                break;
                            case EBINNOT:
                                val = CInt64_Inv(val);
                                folded = TRUE;
                                break;
                            case EMONMIN:
                                val = CInt64_Neg(val);
                                folded = TRUE;
                                break;
                            default:
                                break;
                        }

                        if (folded) {
                            IRO_TruncateValueToType(&val, nd->rtype);
                            expr = IRO_NewENode(EINTCONST);
                            expr->rtype = nd->rtype;
                            expr->data.intval = val;
                        }
                    }

                    if (expr) {
                        nd->u.monadic->type = IROLinearNop;
                        nd->type = IROLinearOperand;
                        nd->u.node = expr;
                    }
                }
                break;

            case IROLinearOp2Arg:
                if (IRO_IsIntConstant(nd->u.diadic.left) &&
                    !IRO_IsIntConstant(nd->u.diadic.right) &&
                    IsAssociativeENodeType[nd->nodetype]) {
                    IROLinear* swap = nd->u.diadic.right;
                    nd->u.diadic.right = nd->u.diadic.left;
                    nd->u.diadic.left = swap;
                }

                if (IRO_IsIntConstant(nd->u.diadic.right) && nd->nodetype == ESUB) {
                    nd->nodetype = EADD;
                    if (IRO_IsIntConstant(nd->u.diadic.right)) {
                        CInt64 v;
                        v = CInt64_Neg(nd->u.diadic.right->u.node->data.intval);
                        nd->u.diadic.right->u.node->data.intval = v;
                    } else {
                        Float f;
                        f = CMach_CalcFloatMonadic(
                            nd->u.diadic.right->rtype,
                            '-',
                            nd->u.diadic.right->u.node->data.floatval
                        );
                        nd->u.diadic.right->u.node->data.floatval = f;
                    }
                }

                if (IRO_IsIntConstant(nd->u.diadic.right) &&
                    IsAssociativeENodeType[nd->nodetype] &&
                    nd->u.diadic.left->type == IROLinearOp2Arg &&
                    nd->u.diadic.left->nodetype == nd->nodetype &&
                    nd->u.diadic.left->rtype == nd->rtype &&
                    IRO_IsIntConstant(nd->u.diadic.left->u.diadic.right) &&
                    nd->u.diadic.left->u.diadic.right->rtype == nd->u.diadic.right->rtype) {
                    IROLinear* inner = nd->u.diadic.left;
                    nd->u.diadic.left = inner->u.diadic.left;
                    inner->u.diadic.left = inner->u.diadic.right;
                    inner->u.diadic.right = nd->u.diadic.right;
                    inner->rtype = inner->u.diadic.left->rtype;
                    nd->u.diadic.right = inner;
                    nd = inner;
                }

                if (IRO_IsIntConstant(nd->u.diadic.left) && IRO_IsIntConstant(nd->u.diadic.right)) {
                    CInt64 val1 = nd->u.diadic.left->u.node->data.intval;
                    CInt64 val2 = nd->u.diadic.right->u.node->data.intval;
                    folded = FALSE;
                    switch (nd->nodetype) {
                        case EADD:
                            val = CInt64_Add(val1, val2);
                            folded = TRUE;
                            break;
                        case ESUB:
                            val = CInt64_Sub(val1, val2);
                            folded = TRUE;
                            break;
                        case EMUL:
                            if (is_unsigned(nd->rtype)) {
                                val = CInt64_MulU(val1, val2);
                            } else {
                                val = CInt64_Mul(val1, val2);
                            }
                            folded = TRUE;
                            break;
                        case EDIV:
                            if (!CInt64_IsZero(&val2)) {
                                if (is_unsigned(nd->rtype)) {
                                    val = CInt64_DivU(val1, val2);
                                } else {
                                    val = CInt64_Div(val1, val2);
                                }
                                folded = TRUE;
                            }
                            break;
                        case EMODULO:
                            if (!CInt64_IsZero(&val2)) {
                                if (is_unsigned(nd->rtype)) {
                                    val = CInt64_ModU(val1, val2);
                                } else {
                                    val = CInt64_Mod(val1, val2);
                                }
                                folded = TRUE;
                            }
                            break;
                        case ESHL:
                            val = CInt64_Shl(val1, val2);
                            folded = TRUE;
                            break;
                        case ESHR:
                            if (is_unsigned(nd->rtype)) {
                                val = CInt64_ShrU(val1, val2);
                            } else {
                                val = CInt64_Shr(val1, val2);
                            }
                            folded = TRUE;
                            break;
                        case EAND:
                            val = CInt64_And(val1, val2);
                            folded = TRUE;
                            break;
                        case EOR:
                            val = CInt64_Or(val1, val2);
                            folded = TRUE;
                            break;
                        case EXOR:
                            val = CInt64_Xor(val1, val2);
                            folded = TRUE;
                            break;
                        case ELESS:
                            if (is_unsigned(nd->u.diadic.left->rtype)) {
                                CInt64_SetULong(&val, CInt64_LessU(val1, val2));
                            } else {
                                CInt64_SetULong(&val, CInt64_Less(val1, val2));
                            }
                            folded = TRUE;
                            break;
                        case EGREATER:
                            if (is_unsigned(nd->u.diadic.left->rtype)) {
                                CInt64_SetULong(&val, CInt64_GreaterU(val1, val2));
                            } else {
                                CInt64_SetULong(&val, CInt64_Greater(val1, val2));
                            }
                            folded = TRUE;
                            break;
                        case ELESSEQU:
                            if (is_unsigned(nd->u.diadic.left->rtype)) {
                                CInt64_SetULong(&val, CInt64_LessEqualU(val1, val2));
                            } else {
                                CInt64_SetULong(&val, CInt64_LessEqual(val1, val2));
                            }
                            folded = TRUE;
                            break;
                        case EGREATEREQU:
                            if (is_unsigned(nd->u.diadic.left->rtype)) {
                                CInt64_SetULong(&val, CInt64_GreaterEqualU(val1, val2));
                            } else {
                                CInt64_SetULong(&val, CInt64_GreaterEqual(val1, val2));
                            }
                            folded = TRUE;
                            break;
                        case EEQU:
                            CInt64_SetULong(&val, CInt64_Equal(val1, val2));
                            folded = TRUE;
                            break;
                        case ENOTEQU:
                            CInt64_SetULong(&val, CInt64_NotEqual(val1, val2));
                            folded = TRUE;
                            break;
                        default:
                            break;
                    }

                    if (folded) {
                        IRO_TruncateValueToType(&val, nd->rtype);
                        expr = IRO_NewENode(EINTCONST);
                        expr->rtype = nd->rtype;
                        expr->data.intval = val;
                        nd->u.diadic.left->type = IROLinearNop;
                        nd->u.diadic.right->type = IROLinearNop;
                        nd->type = IROLinearOperand;
                        nd->u.node = expr;
                    }
                }

                if (IRO_IsFloatConstant(nd->u.diadic.left) &&
                    IRO_IsFloatConstant(nd->u.diadic.right)) {
                    Float fval1 = nd->u.diadic.left->u.node->data.floatval;
                    Float fval2 = nd->u.diadic.right->u.node->data.floatval;
                    Float fval;
                    folded = FALSE;
                    isCompare = FALSE;
                    switch (nd->nodetype) {
                        case EADD:
                            fval = CMach_CalcFloatDiadic(nd->rtype, fval1, '+', fval2);
                            folded = TRUE;
                            break;
                        case ESUB:
                            fval = CMach_CalcFloatDiadic(nd->rtype, fval1, '-', fval2);
                            folded = TRUE;
                            break;
                        case EMUL:
                            fval = CMach_CalcFloatDiadic(nd->rtype, fval1, '*', fval2);
                            folded = TRUE;
                            break;
                        case EDIV:
                            fval = CMach_CalcFloatDiadic(nd->rtype, fval1, '/', fval2);
                            folded = TRUE;
                            break;
                        case ELESS:
                            CInt64_SetULong(
                                &val, CMach_CalcFloatDiadicBool(nd->rtype, fval1, '<', fval2)
                            );
                            folded = TRUE;
                            isCompare = TRUE;
                            break;
                        case EGREATER:
                            CInt64_SetULong(
                                &val, CMach_CalcFloatDiadicBool(nd->rtype, fval1, '>', fval2)
                            );
                            folded = TRUE;
                            isCompare = TRUE;
                            break;
                        case ELESSEQU:
                            CInt64_SetULong(
                                &val,
                                CMach_CalcFloatDiadicBool(nd->rtype, fval1, TK_LESS_EQUAL, fval2)
                            );
                            folded = TRUE;
                            isCompare = TRUE;
                            break;
                        case EGREATEREQU:
                            CInt64_SetULong(
                                &val,
                                CMach_CalcFloatDiadicBool(nd->rtype, fval1, TK_GREATER_EQUAL, fval2)
                            );
                            folded = TRUE;
                            isCompare = TRUE;
                            break;
                        case EEQU:
                            CInt64_SetULong(
                                &val,
                                CMach_CalcFloatDiadicBool(nd->rtype, fval1, TK_LOGICAL_EQ, fval2)
                            );
                            folded = TRUE;
                            isCompare = TRUE;
                            break;
                        case ENOTEQU:
                            CInt64_SetULong(
                                &val,
                                CMach_CalcFloatDiadicBool(nd->rtype, fval1, TK_LOGICAL_NE, fval2)
                            );
                            folded = TRUE;
                            isCompare = TRUE;
                            break;
                        default:
                            break;
                    }

                    if (folded) {
                        if (isCompare) {
                            IRO_TruncateValueToType(&val, nd->rtype);
                            expr = IRO_NewENode(EINTCONST);
                            expr->rtype = nd->rtype;
                            expr->data.intval = val;
                            nd->u.diadic.left->type = IROLinearNop;
                            nd->u.diadic.right->type = IROLinearNop;
                            nd->type = IROLinearOperand;
                            nd->u.node = expr;
                        } else {
                            expr = IRO_NewENode(EFLOATCONST);
                            expr->rtype = nd->rtype;
                            expr->data.floatval = fval;
                            nd->u.diadic.left->type = IROLinearNop;
                            nd->u.diadic.right->type = IROLinearNop;
                            nd->type = IROLinearOperand;
                            nd->u.node = expr;
                        }
                    }
                }

                break;
            default:
                break;
        }
    }

    IRO_CheckForUserBreak();
}

Boolean IRO_EvaluateConditionals(void) {
    IRONode* fnode;
    IROLinear* nd;
    Boolean changed = FALSE;
    SwitchInfo* switchInfo;
    SwitchCase* swcase;
    char found;
    CInt64 val;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        nd = fnode->last;
        switch (nd->type) {
            case IROLinearIf:
            case IROLinearIfNot:
                if (IRO_IsIntConstant(nd->u.label.cond)) {
                    Boolean isZero = CInt64_IsZero(&nd->u.label.cond->u.node->data.intval);
                    IRO_NopOut(nd->u.label.cond);
                    if (((!isZero) != 0) == (nd->type == IROLinearIf)) {
                        nd->type = IROLinearGoto;
                    } else {
                        nd->type = IROLinearNop;
                    }
                    changed = TRUE;
                }
                break;

            case IROLinearSwitch:
                if (IRO_IsIntConstant(nd->u.swtch.cond)) {
                    val = nd->u.swtch.cond->u.node->data.intval;
                    switchInfo = nd->u.swtch.info;
                    swcase = switchInfo->cases;

                    IRO_NopOut(nd->u.swtch.cond);
                    nd->type = IROLinearGoto;

                    found = FALSE;
                    while (swcase) {
#if VERSION < VERSION_GC_2_0
                        if (CInt64_GreaterEqual(val, swcase->min) && CInt64_LessEqual(val, swcase->max)) {
#else
                        if (CInt64_Equal(swcase->min, val)) {
#endif
                            found = TRUE;
                            nd->u.label.label = swcase->label;
                            break;
                        }
                        swcase = swcase->next;
                    }

                    if (!found) {
                        nd->u.label.label = switchInfo->defaultlabel;
                    }
                    changed = TRUE;
                }
                break;
            default:
                break;
        }
    }

    if (changed) {
        IRO_ComputeSuccPred();
        IRO_ComputeDom();
    }
    IRO_CheckForUserBreak();

    return changed;
}

static int EEquConst(IROLinear* nd) {
    return nd && (nd->nodetype == EEQU) && IRO_IsIntConstant(nd->u.diadic.right);
}

static Object* VEquConst(IROLinear* nd) {
    if (EEquConst(nd)) {
        return IRO_IsVariable(nd->u.diadic.left);
    } else {
        return NULL;
    }
}

static int IsConsecutive(CInt64 a, CInt64 b) {
    CInt64 diff;

    if (!CInt64_Equal(a, cint64_min) && !CInt64_Equal(b, cint64_min)) {
        diff = CInt64_Sub(b, a);
        return CInt64_Equal(diff, cint64_one) || CInt64_Equal(diff, cint64_negone);
    }

    return FALSE;
}

static IROLinear* findLabel(CLabel* label) {
    IROLinear* nd;

    for (nd = IRO_FirstLinear; nd; nd = nd->next) {
        if (nd->type == IROLinearLabel && nd->u.label.label == label) {
            break;
        }
    }

    return nd;
}

static IROLinear* leftLeaveOf(IROLinear* nd) {
    switch (nd->type) {
        case IROLinearOp1Arg:
            return leftLeaveOf(nd->u.monadic);
        case IROLinearOp2Arg:
            return leftLeaveOf(nd->u.diadic.left);
        case IROLinearOperand:
            return nd;
        default:
            return NULL;
    }
}

static int checkNode(IRONode* fnode) {
    IROLinear* nd;

    if (fnode->numpred <= 1) {
        nd = fnode->first;
        while (nd != fnode->last && (nd->type == IROLinearNop || nd->type == IROLinearLabel)) {
            nd = nd->next;
        }

        if (nd == leftLeaveOf(fnode->last->u.label.cond)) {
            return TRUE;
        }
    }

    return FALSE;
}

static int checkLabel(CLabel* label, IRONode* fnode) {
    switch (fnode->last->type) {
        case IROLinearIf:
            if (label == fnode->last->u.label.label) {
                return TRUE;
            }
            break;
        default:
            break;
    }

    return FALSE;
}

static Object* checkExpr(Object* expected, IROLinear* nd) {
    Object* var = VEquConst(nd);

    if ((!expected || expected == var) && !IRO_HasSideEffect(nd)) {
        return var;
    }

    return NULL;
}

static int checkStruct(IRONode* fnode1, IRONode* fnode2) {
    CLabel* label;
    Object* var;

    if (fnode1 == fnode2) {
        return (int)checkExpr(NULL, fnode1->last->u.label.cond);
    }

    label = fnode1->last->u.label.label;
    var = IRO_IsVariable(fnode1->last->u.label.cond->u.monadic);
    return checkNode(fnode2) &&
        checkLabel(label, fnode2) &&
        checkExpr(var, fnode2->last->u.label.cond);
}

typedef struct ReduceInfo {
    int mark;
    int state;
    Object* var;
    IRONode* fnode;
    struct ReduceInfo* next;
    CInt64 val;
} ReduceInfo;

static int MarkPattern1(ReduceInfo* info1, ReduceInfo* info2, CInt64* val) {
    ReduceInfo* scan;

    if (!info2) {
        return FALSE;
    }

    if (info2->mark) {
        return MarkPattern1(info1, info2->next, val);
    }

    for (scan = info1; scan; scan = scan->next) {
        if (scan->mark == 2) {
            if (CInt64_Equal(info2->val, scan->val)) {
                IRO_NopOut(scan->fnode->last);
                IRO_NopOut(scan->fnode->last->u.label.cond);
                scan->mark = -1;
                return MarkPattern1(info1, info2->next, val);
            }

            if (IsConsecutive(info2->val, scan->val)) {
                info2->mark = 2;
                if (CInt64_Greater(*val, scan->val)) {
                    *val = scan->val;
                }
                if (CInt64_Greater(*val, info2->val)) {
                    *val = info2->val;
                }
                MarkPattern1(scan->next, info2, val);
                MarkPattern1(info1, info1->next, val);
                return TRUE;
            }
        }
    }

    return MarkPattern1(info1, info2->next, val);
}

static int DoReducible1(ReduceInfo* info, CInt64 val) {
    ReduceInfo* last;
    ReduceInfo* scan;
    IROLinear* cond;
    IROLinear* typconLeft;
    IROLinear* typconRight;
    IROLinear* left;
    IROLinear* right;
    int count;

    count = 0;
    for (scan = info; scan; scan = scan->next) {
        if (scan->mark == 2) {
            last = scan;
            count++;
        }
    }

    if (!count) {
        return 0;
    }

    for (scan = info; scan != last; scan = scan->next) {
        if (scan->mark == 2) {
            scan->mark = -1;
            IRO_NopOut(scan->fnode->last);
            IRO_NopOut(scan->fnode->last->u.label.cond);
        }
    }

    last->mark = -1;

    cond = last->fnode->last;
    cond->u.label.cond->nodetype = ELESSEQU;
    CInt64_SetULong(&cond->u.label.cond->u.diadic.right->u.node->data.intval, count - 1);

    typconLeft = IRO_NewLinear(IROLinearOp1Arg);
    typconLeft->nodetype = ETYPCON;
    typconLeft->rtype = IRO_UnsignedType(cond->u.label.cond->u.diadic.left->rtype);
    typconLeft->index = ++IRO_NumLinear;

    typconRight = IRO_NewLinear(IROLinearOp1Arg);
    *typconRight = *typconLeft;
    typconRight->index = ++IRO_NumLinear;

    left = IRO_NewLinear(IROLinearOp2Arg);
    left->nodetype = EADD;
    left->rtype = cond->u.label.cond->u.diadic.left->rtype;
    left->index = ++IRO_NumLinear;

    right = IRO_NewLinear(IROLinearOperand);
    right->nodetype = EINTCONST;
    right->rtype = cond->u.label.cond->u.diadic.left->rtype;
    right->index = ++IRO_NumLinear;
    right->u.node = IRO_NewENode(EINTCONST);
    right->u.node->data.intval = CInt64_Neg(val);
    right->u.node->rtype = right->rtype;

    typconLeft->next = cond->u.label.cond->u.diadic.left->next;
    cond->u.label.cond->u.diadic.left->next = right;
    right->next = left;
    left->next = typconLeft;

    typconRight->next = cond->u.label.cond->u.diadic.right->next;
    cond->u.label.cond->u.diadic.right->next = typconRight;

    typconLeft->u.monadic = left;
    left->u.diadic.left = cond->u.label.cond->u.diadic.left;
    left->u.diadic.right = right;
    cond->u.label.cond->u.diadic.left = typconLeft;
    typconRight->u.monadic = cond->u.label.cond->u.diadic.right;
    cond->u.label.cond->u.diadic.right = typconRight;

    return count;
}

static int ReducePattern1(IRONode* startnode, IRONode* endnode) {
    ReduceInfo* infos;
    ReduceInfo* info;
    int changed = FALSE;
    int count;
    IRONode* fnode;
    int i;
    int j;
    CInt64 val;

    if (startnode == endnode) {
        return FALSE;
    }

    fnode = startnode;
    count = 0;
    for (; fnode != endnode; fnode = fnode->nextnode) {
        count++;
    }

    infos = oalloc(sizeof(ReduceInfo) * ++count);

    fnode = startnode;
    for (i = 0; i < count; i++) {
        infos[i].mark = 0;
        infos[i].state = 0;
        infos[i].fnode = fnode;
        infos[i].next = NULL;
        infos[i].var = VEquConst(fnode->last->u.label.cond);
        if (infos[i].var) {
            infos[i].val = fnode->last->u.label.cond->u.diadic.right->u.node->data.intval;
            infos[i].state = 1;
        }
        fnode = fnode->nextnode;
    }

    for (j = 0; j < count; j++) {
        if (infos[j].state == 1 && infos[j].var) {
            infos[j].state = -1;
            info = &infos[j];
            for (i = j + 1; i < count; i++) {
                if (infos[j].var == infos[i].var) {
                    info->next = &infos[i];
                    info = info->next;
                    infos[i].state = 0;
                }
            }
        }
    }

    for (j = 0; j < count; j++) {
        if (infos[j].state == -1) {
            for (info = &infos[j]; info; info = info->next) {
                if (info->mark == 0) {
                    info->mark = 2;
                    val = info->val;
                    if (MarkPattern1(&infos[j], info->next, &val)) {
                        changed = TRUE;
                        DoReducible1(&infos[j], val);
                    } else {
                        info->mark = -1;
                    }
                }
            }
        }
    }

    return changed;
}

static int ReduceConsecutiveIf(IRONode* startnode, IRONode* endnode) {
    IRONode* runEnd;
    IRONode* lastMatch;
    int changed = FALSE;

    while (startnode != endnode) {
        if (checkStruct(startnode, startnode)) {
            break;
        }
        startnode = startnode->nextnode;
    }

    runEnd = startnode;
    if (startnode != endnode) {
        lastMatch = startnode;
        runEnd = startnode->nextnode;
        while (runEnd != endnode) {
            if (checkStruct(startnode, runEnd)) {
                lastMatch = runEnd;
                runEnd = runEnd->nextnode;
            } else {
                runEnd = lastMatch;
                break;
            }
        }

        if (runEnd == endnode && !checkStruct(startnode, runEnd)) {
            runEnd = lastMatch;
        }

        if (startnode != runEnd && ReducePattern1(startnode, runEnd)) {
            changed = TRUE;
        }

        if (runEnd != endnode) {
            runEnd = runEnd->nextnode;
        }
    }

    if (runEnd != endnode && ReduceConsecutiveIf(runEnd, endnode)) {
        changed = TRUE;
    }

    return changed;
}

int IRO_SimplifyConditionals(void) {
    IRONode* fnode;
    IRONode* start;
    IRONode* end;
    int changed = FALSE;

    for (fnode = IRO_FirstNode; fnode; fnode = fnode->nextnode) {
        if (fnode->last->type == IROLinearIf) {
            start = end = fnode;
            while (fnode->nextnode && fnode->nextnode->last->type == IROLinearIf) {
                end = fnode = fnode->nextnode;
            }
            if (start != end && ReduceConsecutiveIf(start, end)) {
                changed = TRUE;
            }
        }
    }

    if (changed) {
        IRO_ComputeSuccPred();
        IRO_ComputeDom();
    }
    IRO_CheckForUserBreak();
    return changed;
}

Boolean IRO_EvaluateDefinitePointers(Object* func) {
    IROLinear* nd;
    Boolean result;
    Boolean changed;
    Boolean changed2;
    IROLinear* nd2;
    IROListNode* scan;
    IROListNode* list;

    if (!copts.opt_pointer_analysis) {
        return FALSE;
    }

    result = FALSE;

    do {
        changed = FALSE;

        for (nd = IRO_FirstLinear; nd; nd = nd->next) {
            if (nd->type == IROLinearOp1Arg &&
                nd->nodetype == EINDIRECT &&
                !(nd->flags & IROLF_Assigned) &&
                nd->pointsToFunction &&
                !IRO_HasSideEffect(nd) &&
                PointerAnalysis_IsLinearNodePointerExprDefinite(func, nd)) {
                list = NULL;
                PointerAnalysis_LookupLinearNodePointerExpr(func, nd, &list);
                if (list) {
                    if (list->list.head && list->list.tail && !list->nextList) {
                        changed2 = IRO_LocateFather_Cut_And_Paste(nd, list->list.tail) != NULL;
                        if (changed2) {
                            IRO_PasteAfter(list->list.head, list->list.tail, nd);
                            for (nd2 = list->list.head; nd2 != list->list.tail->next;
                                 nd2 = nd2->next) {
                                if (nd2->type == IROLinearOperand && nd2->u.node->type == EOBJREF) {
                                    Object* obj = nd2->u.node->data.objref;
                                    if (Inline_IsObjectData(obj) || obj->datatype == DLOCAL) {
                                        IRO_FindVar(obj, TRUE, TRUE);
                                    } else {
                                        obj->varptr = NULL;
                                    }
                                }
                            }
                        }
                        changed |= changed2;
                    }

                    while (list) {
                        scan = list->nextList;
                        IRO_free(list);
                        list = scan;
                    }
                }
            }
        }

        result |= changed;
        IRO_CheckForUserBreak();
    } while (changed);

    return result;
}
