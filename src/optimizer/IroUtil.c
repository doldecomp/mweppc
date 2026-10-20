#include "compiler/IroUtil.h"
#include "compiler/IroCSE.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroVars.h"
#include "compiler/CError.h"
#include "compiler/CException.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/objects.h"
#include "compiler/types.h"
#include "driver/CCompiler.h"
#include "driver/COSToolsCLT.h"
#include "driver/CWPluginsPrivate.h"

static UInt32 IRO_LastUserBreakTick;
static IROLinear* ExprStart;
static IROLinear* ExprEnd;
static IRONode* IRO_Node;
static SInt32 FuncLevel;
Object* FunctionName;
Boolean IRO_IsLeafFunction;
Boolean IRO_FunctionHasReturn;
Boolean DisableDueToAsm;
Boolean LoopOptimizerRun;

Object* IRO_IsVariable(IROLinear* linear) {
    if (linear->type == IROLinearOp1Arg &&
        linear->nodetype == EINDIRECT &&
        linear->u.monadic->type == IROLinearOperand &&
        linear->u.monadic->u.node->type == EOBJREF) {
        return linear->u.monadic->u.node->data.objref;
    }

    return NULL;
}

Boolean IRO_IsConstant(IROLinear* linear) {
    if (linear->type == IROLinearOperand &&
        ENODE_IS3(linear->u.node, EINTCONST, EVECTOR128CONST, EFLOATCONST)) {
        return TRUE;
    }
    return FALSE;
}

Boolean IRO_IsPow2(IROLinear* linear, int* powvalue) {
    UInt32 desired;
    UInt32 value;
    UInt32 i;

    *powvalue = -1;
    if (linear->type == IROLinearOperand && linear->u.node->type == EINTCONST) {
        if (linear->u.node->data.intval.hi) {
            return FALSE;
        }

        desired = CInt64_GetULong(&linear->u.node->data.intval);
        value = 2;
        for (i = 1; i < 31; i++) {
            if (value == desired) {
                *powvalue = i;
                return TRUE;
            }
            value += value;
        }
    }

    return FALSE;
}

Boolean IRO_IsIntConstant(IROLinear* linear) {
    if (linear->type == IROLinearOperand && ENODE_IS(linear->u.node, EINTCONST)) {
        return TRUE;
    }
    return FALSE;
}

Boolean IRO_IsFloatConstant(IROLinear* linear) {
    if (linear->type == IROLinearOperand && ENODE_IS(linear->u.node, EFLOATCONST)) {
        return TRUE;
    }
    return FALSE;
}

Boolean IRO_IsVector128Constant(IROLinear* linear) {
    if (linear->type == IROLinearOperand && ENODE_IS(linear->u.node, EVECTOR128CONST)) {
        return TRUE;
    }
    return FALSE;
}

Boolean IRO_IsAssignment(IROLinear* linear) {
    if (linear->type == IROLinearOp1Arg || linear->type == IROLinearOp2Arg) {
        if (IRO_IsAssignOp[linear->nodetype]) {
            return TRUE;
        }
    }
    return FALSE;
}

static Boolean IRO_OperandsSame(ENode* a, ENode* b) {
    if (a->type == b->type) {
        switch (a->type) {
            case EINTCONST:
                return CInt64_Equal(a->data.intval, b->data.intval);
            case ESTRINGCONST:
                return FALSE;
            case EFLOATCONST:
                return a->data.floatval.data.value == b->data.floatval.data.value;
            case EVECTOR128CONST:
                return (a->data.vector128val.ul[0] == b->data.vector128val.ul[0]) &&
                    (a->data.vector128val.ul[1] == b->data.vector128val.ul[1]) &&
                    (a->data.vector128val.ul[2] == b->data.vector128val.ul[2]) &&
                    (a->data.vector128val.ul[3] == b->data.vector128val.ul[3]);
            case EOBJREF:
                return a->data.objref == b->data.objref;
            default:
                break;
        }
    }

    return FALSE;
}

Boolean IRO_TypesEqual(Type* a, Type* b) {
    TypeBitfield* ba;
    TypeBitfield* bb;

    if (IS_TYPE_BITFIELD(a)) {
        if (IS_TYPE_BITFIELD(b)) {
            ba = TYPE_BITFIELD(a);
            bb = TYPE_BITFIELD(b);
            if (ba->bitfieldtype == bb->bitfieldtype &&
                ba->offset == bb->offset &&
                ba->bitlength == bb->bitlength) {
                return TRUE;
            }
        }
        return FALSE;
    }

    if (IS_TYPE_POINTER_ONLY(a) && IS_TYPE_POINTER_ONLY(b)) {
        return TRUE;
    }

    return !!is_typeequal(a, b);
}

Type* IRO_UnsignedType(Type* type) {
    TypeIntegral* t = TYPE_INTEGRAL(type);
    if (IS_TYPE_ENUM(t) || IS_TYPE_POINTER_ONLY(t)) {
        if (t->size == stunsignedchar.size) {
            return TYPE(&stunsignedchar);
        }
        if (t->size == stunsignedint.size) {
            return TYPE(&stunsignedint);
        }
        if (t->size == stunsignedshort.size) {
            return TYPE(&stunsignedshort);
        }
        if (t->size == stunsignedlong.size) {
            return TYPE(&stunsignedlong);
        }
        if (t->size == stunsignedlonglong.size) {
            return TYPE(&stunsignedlonglong);
        }
        CError_FATAL(281);
        return NULL;
    }

    if (!IS_TYPE_INT(t)) {
        CError_FATAL(287);
        return NULL;
    }

    if (t == &stbool || t == &stwchar) {
        return type;
    }

    if (t == &stchar || t == &stsignedchar || t == &stunsignedchar) {
        return TYPE(&stunsignedchar);
    }
    if (t == &stsignedshort || t == &stunsignedshort) {
        return TYPE(&stunsignedshort);
    }
    if (t == &stsignedint || t == &stunsignedint) {
        return TYPE(&stunsignedint);
    }
    if (t == &stsignedlong || t == &stunsignedlong) {
        return TYPE(&stunsignedlong);
    }
    if (t == &stsignedlonglong || t == &stunsignedlonglong) {
        return TYPE(&stunsignedlonglong);
    }

    CError_FATAL(319);
    return NULL;
}

Type* IRO_SignedType(Type* type) {
    if (IS_TYPE_ENUM(type) || IS_TYPE_POINTER_ONLY(type)) {
        if (type->size == stsignedchar.size) {
            return TYPE(&stsignedchar);
        }
        if (type->size == stsignedint.size) {
            return TYPE(&stsignedint);
        }
        if (type->size == stsignedshort.size) {
            return TYPE(&stsignedshort);
        }
        if (type->size == stsignedlong.size) {
            return TYPE(&stsignedlong);
        }
        if (type->size == stsignedlonglong.size) {
            return TYPE(&stsignedlonglong);
        }
        CError_FATAL(357);
        return NULL;
    }

    if (!IS_TYPE_INT(type)) {
        CError_FATAL(363);
        return NULL;
    }

    if (type == TYPE(&stbool) && type->size == stsignedchar.size) {
        return TYPE(&stsignedchar);
    }

    if (type == TYPE(&stwchar) && type->size == stsignedshort.size) {
        return TYPE(&stsignedshort);
    }

    if (type == TYPE(&stchar) || type == TYPE(&stsignedchar) || type == TYPE(&stunsignedchar)) {
        return TYPE(&stsignedchar);
    }
    if (type == TYPE(&stsignedshort) || type == TYPE(&stunsignedshort)) {
        return TYPE(&stsignedshort);
    }
    if (type == TYPE(&stsignedint) || type == TYPE(&stunsignedint)) {
        return TYPE(&stsignedint);
    }
    if (type == TYPE(&stsignedlong) || type == TYPE(&stunsignedlong)) {
        return TYPE(&stsignedlong);
    }
    if (type == TYPE(&stsignedlonglong) || type == TYPE(&stunsignedlonglong)) {
        return TYPE(&stsignedlonglong);
    }

    CError_FATAL(399);
    return NULL;
}

Boolean IRO_is_CPtypeequal(Type* a, Type* b) {
    if (IS_TYPE_POINTER_ONLY(a) && IS_TYPE_POINTER_ONLY(b)) {
        return TRUE;
    }

    return !!is_typeequal(a, b);
}

Boolean IRO_ExprsSame(IROLinear* a, IROLinear* b) {
    if (a->type == b->type && IRO_TypesEqual(a->rtype, b->rtype)) {
        switch (a->type) {
            case IROLinearOperand:
                return IRO_OperandsSame(a->u.node, b->u.node);
            case IROLinearOp1Arg:
                if (a->nodetype == b->nodetype) {
                    return IRO_ExprsSame(a->u.monadic, b->u.monadic);
                }
                return FALSE;
            case IROLinearOp2Arg:
                if (a->nodetype == b->nodetype) {
                    return IRO_ExprsSame(a->u.diadic.left, b->u.diadic.left) &&
                        IRO_ExprsSame(a->u.diadic.right, b->u.diadic.right);
                }
                return FALSE;
            case IROLinearOp3Arg:
                if (a->nodetype == b->nodetype) {
                    return IRO_ExprsSame(a->u.args3.a, b->u.args3.a) &&
                        IRO_ExprsSame(a->u.args3.b, b->u.args3.b) &&
                        IRO_ExprsSame(a->u.args3.c, b->u.args3.c);
                }
                return FALSE;
            case IROLinearFunccall:
                return FALSE;
            default:
                return FALSE;
        }
    }

    return FALSE;
}

CLabel* IRO_NewLabel(void) {
    CLabel* label = newlabel();
    label->next = Labels;
    Labels = label;
    return label;
}

Boolean IRO_ExprsSameSemantically(IROLinear* a, IROLinear* b) {
    if (a->type == b->type && IRO_TypesEqual(a->rtype, b->rtype)) {
        Boolean swappedSame = FALSE;

        switch (a->type) {
            case IROLinearOperand:
                return IRO_OperandsSame(a->u.node, b->u.node);
            case IROLinearOp1Arg:
                if (a->nodetype == b->nodetype) {
                    return IRO_ExprsSameSemantically(a->u.monadic, b->u.monadic);
                }
                return FALSE;
            case IROLinearOp2Arg:
                if (a->nodetype == b->nodetype) {
                    switch (a->nodetype) {
                        case EMUL:
                        case EADD:
                        case EAND:
                        case EXOR:
                        case EOR:
                        case ELAND:
                        case ELOR:
                            if (!IRO_HasSideEffect(a)) {
                                swappedSame = IRO_ExprsSameSemantically(
                                                  a->u.diadic.left, b->u.diadic.right
                                              ) &&
                                    IRO_ExprsSameSemantically(a->u.diadic.right, b->u.diadic.left);
                            }
                            break;
                        default:
                            break;
                    }

                    return swappedSame ||
                        (IRO_ExprsSameSemantically(a->u.diadic.left, b->u.diadic.left) &&
                         IRO_ExprsSameSemantically(a->u.diadic.right, b->u.diadic.right));
                }
                return FALSE;
            case IROLinearOp3Arg:
                if (a->nodetype == b->nodetype) {
                    return IRO_ExprsSameSemantically(a->u.args3.a, b->u.args3.a) &&
                        IRO_ExprsSameSemantically(a->u.args3.b, b->u.args3.b) &&
                        IRO_ExprsSameSemantically(a->u.args3.c, b->u.args3.c);
                }
                return FALSE;
            case IROLinearFunccall:
                return FALSE;
            default:
                return FALSE;
        }
    }

    return FALSE;
}

IROLinear* IRO_FindPrecedAfter(IROLinear* a, IROLinear* iter) {
    IROLinear* prev = iter;
    while (iter && iter != a) {
        prev = iter;
        iter = iter->next;
    }

    if (iter) {
        return prev;
    }
    return iter;
}

IROLinear* IRO_FindPreced(IROLinear* a) {
    IROLinear* iter;
    IROLinear* prev;

    prev = iter = IRO_FirstLinear;
    while (iter && iter != a) {
        prev = iter;
        iter = iter->next;
    }

    if (iter) {
        return prev;
    }
    return iter;
}

static IROLinear* FindFirst(IROLinear* linear) {
    short i;

    switch (linear->type) {
        case IROLinearOperand:
            return linear;
        case IROLinearOp1Arg:
            return FindFirst(linear->u.monadic);
        case IROLinearOp2Arg:
            if (linear->u.diadic.right->index < linear->u.diadic.left->index) {
                return FindFirst(linear->u.diadic.right);
            } else {
                return FindFirst(linear->u.diadic.left);
            }
        case IROLinearOp3Arg:
            if (linear->u.args3.a->index < linear->u.args3.b->index) {
                if (linear->u.args3.a->index < linear->u.args3.c->index) {
                    return FindFirst(linear->u.args3.a);
                } else {
                    return FindFirst(linear->u.args3.c);
                }
            } else {
                if (linear->u.args3.b->index < linear->u.args3.c->index) {
                    return FindFirst(linear->u.args3.b);
                } else {
                    return FindFirst(linear->u.args3.c);
                }
            }
        case IROLinearFunccall:
            i = linear->u.funccall.argCount - 1;
            return FindFirst(linear->u.funccall.args[i]);
        default:
            CError_FATAL(641);
            return NULL;
    }
}

IROLinear* IRO_FindFirst(IROLinear* linear) {
    IRO_RenumberInts();
    return FindFirst(linear);
}

void IRO_CutAndPasteAfter(IROLinear* first, IROLinear* last, IROLinear* after) {
    IROLinear* preced = IRO_FindPreced(first);
    preced->next = last->next;
    last->next = after->next;
    after->next = first;
}

Boolean IRO_IsConstantZero(IROLinear* linear) {
    return (IRO_IsIntConstant(linear) && CInt64_IsZero(&linear->u.node->data.intval)) ||
        (IRO_IsFloatConstant(linear) && CMach_FloatIsZero(linear->u.node->data.floatval));
}

Boolean IRO_IsConstantOne(IROLinear* linear) {
    return (IRO_IsIntConstant(linear) && CInt64_IsOne(&linear->u.node->data.intval)) ||
        (IRO_IsFloatConstant(linear) && CMach_FloatIsOne(linear->u.node->data.floatval));
}

Boolean IRO_IsConstantNegativeOne(IROLinear* linear) {
    CInt64 neg = CInt64_Neg(linear->u.node->data.intval);
    return (IRO_IsIntConstant(linear) && CInt64_IsOne(&neg)) ||
        (IRO_IsFloatConstant(linear) && CMach_FloatIsNegOne(linear->u.node->data.floatval));
}

static void ActNop(IROLinear* linear, Boolean isEntry) {
    if (!isEntry) {
        linear->type = IROLinearNop;
        linear->expr = NULL;
    }
}

void IRO_NopOut(IROLinear* linear) {
    IRO_WalkTree(linear, ActNop);
}

static Boolean NotNoppable(IROLinear* linear) {
    return (linear->type == IROLinearFunccall) ||
        IRO_IsAssignment(linear) ||
        ((linear->type == IROLinearOperand) && ENODE_IS(linear->u.node, EINSTRUCTION)) ||
        (linear->rtype &&
         CParser_IsVolatile(linear->rtype, linear->nodeflags & ENODE_FLAG_QUALS)) ||
        ((linear->type == IROLinearOperand) &&
         ENODE_IS(linear->u.node, EOBJREF) &&
         is_volatile_object(linear->u.node->data.objref));
}

static void ActNopNonSideEffects(IROLinear* linear, Boolean isEntry) {
    if (isEntry) {
        if (NotNoppable(linear)) {
            if (!FuncLevel) {
                linear->flags &= ~IROLF_Reffed;
            }
            FuncLevel++;
        }
    } else {
        if (NotNoppable(linear)) {
            FuncLevel--;
        } else if (!FuncLevel) {
            linear->type = IROLinearNop;
        }
    }
}

void IRO_NopNonSideEffects(IROLinear* linear, SInt32 level) {
    FuncLevel = level;
    IRO_WalkTree(linear, ActNopNonSideEffects);
}

void IRO_WalkTree(IROLinear* linear, IROWalkTreeFunc func) {
    int i;

    func(linear, TRUE);
    switch (linear->type) {
        case IROLinearOperand:
            break;
        case IROLinearOp1Arg:
            IRO_WalkTree(linear->u.monadic, func);
            break;
        case IROLinearOp2Arg:
            IRO_WalkTree(linear->u.diadic.left, func);
            IRO_WalkTree(linear->u.diadic.right, func);
            break;
        case IROLinearOp3Arg:
            IRO_WalkTree(linear->u.args3.a, func);
            IRO_WalkTree(linear->u.args3.b, func);
            IRO_WalkTree(linear->u.args3.c, func);
            break;
        case IROLinearFunccall:
            IRO_WalkTree(linear->u.funccall.callee, func);
            for (i = 0; i < linear->u.funccall.argCount; i++) {
                IRO_WalkTree(linear->u.funccall.args[i], func);
            }
            break;
        default:
            break;
    }
    func(linear, FALSE);
}

void IRO_WalkTreeToPropagateFlags(IROLinear* linear, IROWalkTreeFunc func) {
    int i;

    switch (linear->type) {
        case IROLinearOperand:
            break;
        case IROLinearOp1Arg:
            IRO_WalkTreeToPropagateFlags(linear->u.monadic, func);
            break;
        case IROLinearOp2Arg:
            IRO_WalkTreeToPropagateFlags(linear->u.diadic.left, func);
            IRO_WalkTreeToPropagateFlags(linear->u.diadic.right, func);
            break;
        case IROLinearOp3Arg:
            IRO_WalkTreeToPropagateFlags(linear->u.args3.a, func);
            IRO_WalkTreeToPropagateFlags(linear->u.args3.b, func);
            IRO_WalkTreeToPropagateFlags(linear->u.args3.c, func);
            break;
        case IROLinearFunccall:
            IRO_WalkTreeToPropagateFlags(linear->u.funccall.callee, func);
            for (i = 0; i < linear->u.funccall.argCount; i++) {
                IRO_WalkTreeToPropagateFlags(linear->u.funccall.args[i], func);
            }
            break;
        default:
            break;
    }
    func(linear, FALSE);
}

void IRO_WalkInts(IROLinear* first, IROLinear* last, IROWalkTreeFunc func) {
    IROLinear* scan;

    for (scan = first; scan; scan = scan->next) {
        switch (scan->type) {
            case IROLinearBeginCatch:
            case IROLinearEndCatch:
            case IROLinearEndCatchDtor:
                IRO_WalkTree(scan->u.ctch.linear, func);
                break;
            case IROLinearOperand:
            case IROLinearOp1Arg:
            case IROLinearOp2Arg:
            case IROLinearOp3Arg:
            case IROLinearFunccall:
                IRO_WalkTree(scan, func);
                break;
            case IROLinearIf:
            case IROLinearIfNot:
                IRO_WalkTree(scan->u.label.cond, func);
                break;
            case IROLinearReturn:
                if (scan->u.monadic) {
                    IRO_WalkTree(scan->u.monadic, func);
                }
                break;
            case IROLinearSwitch:
                IRO_WalkTree(scan->u.swtch.cond, func);
                break;
            case IROLinearAsm:
                func(scan, TRUE);
                func(scan, FALSE);
                break;
            case IROLinearNop:
            case IROLinear18:
            case IROLinear19:
            case IROLinearEnd:
                break;
            default:
                break;
        }

        if (scan == last) {
            return;
        }
    }
}

void IRO_Cut(IROLinear* first, IROLinear* last) {
    IROLinear* scan;
    IROLinear* prev;
    IRONode* node;

    scan = first;
    while (1) {
        scan->stmt = NULL;
        if (scan == last) {
            break;
        }
        scan = scan->next;
    }

    prev = NULL;
    for (scan = IRO_FirstLinear; scan && scan != first; scan = scan->next) {
        prev = scan;
    }

    CError_ASSERT(960, scan);

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        if (node->first == first) {
            if (node->last == last) {
                node->first = node->last = NULL;
                break;
            } else {
                node->first = last->next;
                break;
            }
        } else if (node->last == last) {
            node->last = prev;
            break;
        }
    }

    if (prev) {
        prev->next = last->next;
    } else {
        IRO_FirstLinear = last->next;
    }
}

void IRO_Paste(IROLinear* first, IROLinear* last, IROLinear* before) {
    IROLinear* scan;
    IROLinear* prev;
    IRONode* node;

    CError_ASSERT(1011, before && before->type != IROLinearLabel);

    prev = NULL;
    for (scan = IRO_FirstLinear; scan && scan != before; scan = scan->next) {
        prev = scan;
    }

    CError_ASSERT(1025, scan);

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        if (node->first == before) {
            node->first = first;
            break;
        }
    }

    last->next = before;
    if (prev) {
        prev->next = first;
    } else {
        IRO_FirstLinear = first;
    }
}

void IRO_PasteAfter(IROLinear* first, IROLinear* last, IROLinear* after) {
    IRONode* node;

    switch (after->type) {
        case IROLinearGoto:
        case IROLinearIf:
        case IROLinearIfNot:
        case IROLinearSwitch:
            CError_FATAL(1069);
            break;
        default:
            break;
    }

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        if (node->last == after) {
            node->last = last;
            break;
        }
    }

    last->next = after->next;
    after->next = first;
}

static void FindStart(IROLinear* linear, Boolean isEntry) {
    IROLinear* scan;
    if (isEntry) {
        scan = linear;
        do {
            if (scan == ExprStart) {
                ExprStart = linear;
                return;
            }
            if (scan == ExprEnd) {
                return;
            }
        } while ((scan = scan->next));
    }
}

void IRO_ClipExpr(IROExpr* expr) {
    ExprStart = ExprEnd = expr->linear;
    IRO_WalkTree(expr->linear, FindStart);
    IRO_Cut(ExprStart, ExprEnd);
}

void IRO_ClipExprTree(IROLinear* linear) {
    ExprStart = ExprEnd = linear;
    IRO_WalkTree(linear, FindStart);
    IRO_Cut(ExprStart, ExprEnd);
}

static void SetNodeNumInExprList(IROLinear* linear, Boolean isEntry) {
    if (isEntry && linear->expr) {
        linear->expr->node = IRO_Node;
    }
}

void IRO_MoveExpression(IROExpr* expr, IROLinear* linear) {
    IRONode* node;
    IROLinear* scan;

    ExprStart = ExprEnd = expr->linear;
    IRO_WalkTree(expr->linear, FindStart);

    if (ExprStart == linear) {
        return;
    }

    IRO_Cut(ExprStart, ExprEnd);
    IRO_Paste(ExprStart, ExprEnd, linear);
    for (node = IRO_FirstNode; node; node = node->nextnode) {
        for (scan = node->first; scan; scan = scan->next) {
            if (scan == expr->linear) {
                expr->node = node;
                break;
            }
            if (scan == node->last) {
                break;
            }
        }
    }

    IRO_Node = expr->node;
    IRO_WalkTree(expr->linear, SetNodeNumInExprList);
}

void IRO_InitList(IROList* list) {
    list->head = list->tail = NULL;
}

void IRO_AddToList(IROLinear* linear, IROList* list) {
    if (list->head) {
        list->tail->next = linear;
    } else {
        list->head = linear;
    }

    list->tail = linear;
    while (list->tail->next) {
        list->tail = list->tail->next;
    }
}

IROLinear* IRO_FindLabelNode(CLabel* label, IROLinear* linear) {
    IROLinear* scan;

    for (scan = linear; scan; scan = scan->next) {
        if (scan->type == IROLinearLabel && scan->u.label.label == label) {
            break;
        }
    }

    CError_ASSERT(1253, scan);
    return scan;
}

void IRO_DuplicateExprRange(IROLinear* start, IROLinear* end, IROList* list) {
    IROLinear* scan;

    for (scan = start; scan; scan = scan->next) {
        if (scan->type != IROLinearNop && !(scan->flags & IROLF_Reffed)) {
            IRO_DuplicateExpr(scan, list);
        }
        if (scan == end) {
            break;
        }
    }
}

IROLinear* IRO_DuplicateExpr(IROLinear* linear, IROList* list) {
    IROLinear* copy;
    ENode* copynode;
    int i;

    copy = IRO_NewLinear(linear->type);
    *copy = *linear;

    copy->index = ++IRO_NumLinear;
    copy->next = NULL;
    copy->expr = NULL;

    switch (copy->type) {
        case IROLinearNop:
            break;
        case IROLinearOperand:
            copynode = lalloc(sizeof(ENode));
            *copynode = *linear->u.node;
            copy->u.node = copynode;
            break;
        case IROLinearOp1Arg:
            copy->u.monadic = IRO_DuplicateExpr(copy->u.monadic, list);
            break;
        case IROLinearOp2Arg:
            if (linear->flags & IROLF_RightFirst) {
                copy->u.diadic.right = IRO_DuplicateExpr(copy->u.diadic.right, list);
                copy->u.diadic.left = IRO_DuplicateExpr(copy->u.diadic.left, list);
            } else {
                copy->u.diadic.left = IRO_DuplicateExpr(copy->u.diadic.left, list);
                copy->u.diadic.right = IRO_DuplicateExpr(copy->u.diadic.right, list);
            }
            break;
        case IROLinearOp3Arg:
            copy->u.args3.a = IRO_DuplicateExpr(copy->u.args3.a, list);
            copy->u.args3.b = IRO_DuplicateExpr(copy->u.args3.b, list);
            copy->u.args3.c = IRO_DuplicateExpr(copy->u.args3.c, list);
            break;
        case IROLinearFunccall:
            copy->u.funccall.callee = IRO_DuplicateExpr(copy->u.funccall.callee, list);
            copy->u.funccall.args = oalloc(sizeof(IROLinear*) * copy->u.funccall.argCount);
            for (i = 0; i < copy->u.funccall.argCount; i++) {
                copy->u.funccall.args[i] = IRO_DuplicateExpr(linear->u.funccall.args[i], list);
            }
            break;
        case IROLinearAsm:
            copy->u.asm_stmt = CodeGen_CopyAsmStat(linear->u.asm_stmt);
            break;
        default:
            break;
    }

    IRO_AddToList(copy, list);
    return copy;
}

IROLinear* IRO_TempReference(Object* obj, IROList* list) {
    IROLinear* op;
    IROLinear* ind;

    op = IRO_NewLinear(IROLinearOperand);
    op->u.node = create_objectrefnode(obj);
    op->rtype = op->u.node->data.objref->type;
    op->index = ++IRO_NumLinear;
    op->flags |= IROLF_Reffed | IROLF_Ind;
    IRO_AddToList(op, list);

    ind = IRO_NewLinear(IROLinearOp1Arg);
    ind->nodetype = EINDIRECT;
    ind->rtype = obj->type;
    ind->u.monadic = op;
    ind->index = ++IRO_NumLinear;
    ind->next = NULL;
    IRO_AddToList(ind, list);

    return ind;
}

inline IROLinear* LocateFatherHelper(IROLinear* linear, Boolean wrapAround, IROLinear*** refPtr) {
    IROLinear* scan;
    SInt32 index;
    int i;

    for (scan = linear->next, index = 0; index < (wrapAround ? 2 : 1); index++) {
        while (scan) {
            switch (scan->type) {
                case IROLinearIf:
                case IROLinearIfNot:
                    if (scan->u.label.cond == linear) {
                        if (refPtr) {
                            *refPtr = &scan->u.label.cond;
                        }
                        return scan;
                    }
                    break;
                case IROLinearReturn:
                    if (scan->u.monadic == linear) {
                        if (refPtr) {
                            *refPtr = &scan->u.monadic;
                        }
                        return scan;
                    }
                    break;
                case IROLinearOp1Arg:
                    if (scan->u.monadic == linear) {
                        if (refPtr) {
                            *refPtr = &scan->u.monadic;
                        }
                        return scan;
                    }
                    break;
                case IROLinearSwitch:
                    if (scan->u.swtch.cond == linear) {
                        if (refPtr) {
                            *refPtr = &scan->u.swtch.cond;
                        }
                        return scan;
                    }
                    break;
                case IROLinearOp2Arg:
                    if (scan->u.diadic.left == linear) {
                        if (refPtr) {
                            *refPtr = &scan->u.diadic.left;
                        }
                        return scan;
                    }
                    if (scan->u.diadic.right == linear) {
                        if (refPtr) {
                            *refPtr = &scan->u.diadic.right;
                        }
                        return scan;
                    }
                    break;
                case IROLinearOp3Arg:
                    if (scan->u.args3.a == linear) {
                        if (refPtr) {
                            *refPtr = &scan->u.args3.a;
                        }
                        return scan;
                    }
                    if (scan->u.args3.b == linear) {
                        if (refPtr) {
                            *refPtr = &scan->u.args3.b;
                        }
                        return scan;
                    }
                    if (scan->u.args3.c == linear) {
                        if (refPtr) {
                            *refPtr = &scan->u.args3.c;
                        }
                        return scan;
                    }
                    break;
                case IROLinearFunccall:
                    if (scan->u.funccall.callee == linear) {
                        if (refPtr) {
                            *refPtr = &scan->u.funccall.callee;
                        }
                        return scan;
                    }
                    for (i = 0; i < scan->u.funccall.argCount; i++) {
                        if (scan->u.funccall.args[i] == linear) {
                            if (refPtr) {
                                *refPtr = &scan->u.funccall.args[i];
                            }
                            return scan;
                        }
                    }
                    break;
                case IROLinearNop:
                case IROLinearOperand:
                case IROLinearGoto:
                case IROLinearLabel:
                case IROLinearEntry:
                case IROLinearExit:
                case IROLinearBeginCatch:
                case IROLinearEndCatch:
                case IROLinearEndCatchDtor:
                case IROLinearAsm:
                case IROLinearEnd:
                    break;
                default:
                    CError_FATAL(1545);
            }
            scan = scan->next;
        }

        scan = IRO_FirstLinear;
    }

    if (refPtr) {
        *refPtr = NULL;
    }
    return NULL;
}

IROLinear* IRO_LocateFather(IROLinear* linear) {
    return LocateFatherHelper(linear, FALSE, NULL);
}

IROLinear* IRO_LocateFather_Cut_And_Paste(IROLinear* nd, IROLinear* replacement) {
    IROLinear** link;
    IROLinear* father = LocateFatherHelper(nd, FALSE, &link);
    if (father) {
        CError_ASSERT(1577, link && *link);
        IRO_NopOut(nd);
        *link = replacement;
    }
    return father;
}

IROLinear* IRO_LocateFather_Cut_And_Paste_Without_Nopping(IROLinear* nd, IROLinear* replacement) {
    IROLinear** link;
    IROLinear* father = LocateFatherHelper(nd, FALSE, &link);
    if (father) {
        CError_ASSERT(1594, link && *link);
        *link = replacement;
    }
    return father;
}

void IRO_ReplaceReference(IROLinear* nd, Object* obj, IROLinear* after) {
    IROLinear** ptr;
    IROList list;

    if (LocateFatherHelper(nd, TRUE, &ptr)) {
        CError_ASSERT(1614, ptr && *ptr);
        IRO_InitList(&list);
        *ptr = IRO_TempReference(obj, &list);
        IRO_PasteAfter(list.head, list.tail, after);
        if (nd->flags & IROLF_LoopInvariant) {
            list.tail->flags |= IROLF_LoopInvariant;
        }
    } else {
        IRO_Dump("Oh, oh, did not find reference to replace\n");
    }
}

void IRO_ReplaceReferenceWithNode(IROLinear* nd, IROLinear* replacement) {
    IROLinear** ptr;

    if (LocateFatherHelper(nd, TRUE, &ptr)) {
        CError_ASSERT(1673, ptr && *ptr);
        *ptr = replacement;
        replacement->flags |= IROLF_Reffed;
    } else {
        IRO_Dump("Oh, oh, did not find reference to replace\n");
    }
}

VarRecord* IRO_GetTemp(IROExpr* expr) {
    expr->temp = create_temp_object(expr->linear->rtype);
    return IRO_FindVar(expr->temp, TRUE, TRUE);
}

IROLinear* IRO_AssignToTemp(IROExpr* expr) {
    IROLinear* objref;
    IROLinear* ind;
    IROLinear* ass;

    objref = IRO_NewLinear(IROLinearOperand);
    objref->u.node = create_objectrefnode(expr->temp);
    objref->rtype = objref->u.node->data.objref->type;
    objref->index = ++IRO_NumLinear;
    objref->flags |= IROLF_Reffed | IROLF_Assigned | IROLF_Ind;

    ind = IRO_NewLinear(IROLinearOp1Arg);
    ind->nodetype = EINDIRECT;
    ind->rtype = expr->linear->rtype;
    ind->u.monadic = objref;
    ind->index = ++IRO_NumLinear;
    ind->flags |= IROLF_Reffed | IROLF_Assigned;

    ass = IRO_NewLinear(IROLinearOp2Arg);
    ass->nodetype = EASS;
    ass->u.diadic.left = ind;
    ass->u.diadic.right = expr->linear;
    ass->rtype = expr->linear->rtype;
    ass->index = ++IRO_NumLinear;

    objref->next = ind;
    ind->next = ass;
    IRO_PasteAfter(objref, ass, expr->linear);
    return ass;
}

IROLinear* IRO_FindStart(IROLinear* linear) {
    ExprStart = ExprEnd = linear;
    IRO_WalkTree(linear, FindStart);
    return ExprStart;
}

void IRO_DeleteCommaNode(IROLinear* linear, IROExpr* expr) {
    IROLinear* scan;
    for (scan = linear; scan; scan = scan->next) {
        if (scan->nodetype == ECOMMA) {
            if (scan != expr->linear) {
                IRO_NopOut(scan->u.diadic.left);
                IRO_LocateFather_Cut_And_Paste(scan, scan->u.diadic.right);
            } else {
                IRO_NopOut(scan->u.diadic.left);
                expr->linear = scan->u.diadic.right;
            }
        }
    }
}

void IRO_RemoveCommaNodeFromIR(void) {
    IRONode* node;
    IROLinear* linear;

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        linear = node->first;
        do {
            if (!linear) {
                break;
            }
            if (linear->nodetype == ECOMMA) {
                linear->u.diadic.left->flags = linear->u.diadic.left->flags & ~IROLF_Reffed;
                IRO_LocateFather_Cut_And_Paste_Without_Nopping(linear, linear->u.diadic.right);
                linear->type = IROLinearNop;
            }
            linear = linear->next;
        } while (linear != node->last);
    }
}

IROAddrRecord* IRO_InitAddrRecordPointer(IROLinear* linear) {
    IROAddrRecord* rec = oalloc(sizeof(IROAddrRecord));
    rec->numObjRefs = 0;
    rec->objRefs = NULL;
    rec->numMisc = 0;
    rec->misc = NULL;
    rec->numInts = 0;
    rec->ints = NULL;
    rec->unused = 0;
    rec->linear = linear;
    return rec;
}

IROLinear* IRO_HasSideEffect(IROLinear* linear) {
    IROLinear* effect;

    if (!linear) {
        return linear;
    }

    if (linear->rtype && CParser_IsVolatile(linear->rtype, linear->nodeflags & ENODE_FLAG_QUALS)) {
        return linear;
    }

    switch (linear->type) {
        case IROLinearAsm:
            return linear;
        case IROLinearOperand:
            if (ENODE_IS(linear->u.node, EOBJREF) &&
                is_volatile_object(linear->u.node->data.objref)) {
                return linear;
            }
            return NULL;
        case IROLinearOp1Arg:
            if (IRO_IsAssignOp[linear->nodetype]) {
                return linear;
            } else {
                return IRO_HasSideEffect(linear->u.monadic);
            }
        case IROLinearOp2Arg:
            if (IRO_IsAssignOp[linear->nodetype]) {
                return linear;
            }
            effect = IRO_HasSideEffect(linear->u.diadic.left);
            if (!effect) {
                effect = IRO_HasSideEffect(linear->u.diadic.right);
            }
            return effect;
        case IROLinearOp3Arg:
            if ((effect = IRO_HasSideEffect(linear->u.args3.a))) {
                return effect;
            } else if ((effect = IRO_HasSideEffect(linear->u.args3.b))) {
                return effect;
            } else {
                return IRO_HasSideEffect(linear->u.args3.c);
            }
        case IROLinearFunccall:
            return linear;
        default:
            return linear;
    }
}

IROLinear* IRO_CheckSideEffect(IROLinear* linear) {
    IROLinear* result;
    IROLinear* effect1;
    IROLinear* effect2;
    IROLinear* effect3;

    if (!linear) {
        return linear;
    }

    if (linear->rtype && CParser_IsVolatile(linear->rtype, linear->nodeflags & ENODE_FLAG_QUALS)) {
        linear->flags &= ~IROLF_Reffed;
        return linear;
    }

    result = NULL;
    switch (linear->type) {
        case IROLinearAsm:
            linear->flags &= ~IROLF_Reffed;
            return linear;
        case IROLinearOperand:
            if (ENODE_IS(linear->u.node, EOBJREF) &&
                is_volatile_object(linear->u.node->data.objref)) {
                linear->flags &= ~IROLF_Reffed;
                return linear;
            }
            break;
        case IROLinearOp1Arg:
            if (IRO_IsAssignOp[linear->nodetype]) {
                linear->flags &= ~IROLF_Reffed;
                return linear;
            }
            if ((result = IRO_CheckSideEffect(linear->u.monadic))) {
                if (linear->nodetype == EINDIRECT && linear->u.monadic->type == IROLinearOperand) {
                    linear->flags &= ~IROLF_Reffed;
                    return linear;
                }
            }
            break;
        case IROLinearOp2Arg:
            if (IRO_IsAssignOp[linear->nodetype]) {
                linear->flags &= ~IROLF_Reffed;
                return linear;
            }
            effect1 = IRO_CheckSideEffect(linear->u.diadic.left);
            effect2 = IRO_CheckSideEffect(linear->u.diadic.right);
            if (effect1) {
                result = effect1;
            } else {
                result = effect2;
            }
            break;
        case IROLinearOp3Arg:
            effect1 = IRO_CheckSideEffect(linear->u.args3.a);
            effect2 = IRO_CheckSideEffect(linear->u.args3.b);
            effect3 = IRO_CheckSideEffect(linear->u.args3.c);
            result = effect1 ? effect1 : effect2 ? effect2 : effect3;
            break;
        case IROLinearFunccall:
            linear->flags &= ~IROLF_Reffed;
            return linear;
        default:
            return linear;
    }

    linear->type = IROLinearNop;
    IRO_Dump("Nop out with side-effects checking at: %d\n", linear->index);
    return result;
}

void IRO_WalkExcActions(ExceptionAction* action, WalkObjFunc func) {
    while (action) {
        switch (action->type) {
            case EAT_DESTROYLOCAL:
                func(action->data.destroy_local.local);
                break;
            case EAT_DESTROYLOCALCOND:
                func(action->data.destroy_local_cond.local);
                func(action->data.destroy_local_cond.cond);
                break;
            case EAT_DESTROYLOCALOFFSET:
                func(action->data.destroy_local_offset.local);
                break;
            case EAT_DESTROYLOCALPOINTER:
                func(action->data.destroy_local_pointer.pointer);
                break;
            case EAT_DESTROYLOCALARRAY:
                func(action->data.destroy_local_array.localarray);
                break;
            case EAT_DESTROYPARTIALARRAY:
                func(action->data.destroy_partial_array.arraypointer);
                func(action->data.destroy_partial_array.arraycounter);
                func(action->data.destroy_partial_array.element_size);
                break;
            case EAT_DESTROYBASE:
                func(action->data.destroy_base.objectptr);
                break;
            case EAT_DESTROYMEMBER:
                func(action->data.destroy_member.objectptr);
                break;
            case EAT_DESTROYMEMBERCOND:
                func(action->data.destroy_member_cond.objectptr);
                func(action->data.destroy_member_cond.cond);
                break;
            case EAT_DESTROYMEMBERARRAY:
                func(action->data.destroy_member_array.objectptr);
                break;
            case EAT_DELETEPOINTER:
                func(action->data.delete_pointer.pointerobject);
                break;
            case EAT_DELETEPOINTERCOND:
                func(action->data.delete_pointer_cond.pointerobject);
                func(action->data.delete_pointer_cond.cond);
                break;
            default:
                break;
        }
        action = action->prev;
    }
}

Boolean IRO_FunctionCallMightThrowException(IROLinear* linear) {
    Object* obj;
    if (linear->type == IROLinearFunccall) {
        obj = NULL;
        if (linear->u.funccall.callee->type == IROLinearOperand &&
            ENODE_IS(linear->u.funccall.callee->u.node, EOBJREF)) {
            obj = linear->u.funccall.callee->u.node->data.objref;
        }
        if (!obj ||
            CExcept_CanThrowException(
                obj, obj->datatype == DVFUNC && (linear->nodeflags & ENODE_FLAG_NONVIRTUAL) == 0
            )) {
            return TRUE;
        }
    }
    return FALSE;
}

IROLinear* IRO_NewIntConst(CInt64 val, Type* type) {
    ENode* node;
    IROLinear* linear;

    node = IRO_NewENode(EINTCONST);
    node->data.intval = val;
    node->rtype = type;

    linear = IRO_NewLinear(IROLinearOperand);
    linear->index = ++IRO_NumLinear;
    linear->rtype = type;
    linear->u.node = node;
    return linear;
}

IROLinear* IRO_NewFloatConst(const Float val, Type* type) {
    ENode* node;
    IROLinear* linear;

    node = IRO_NewENode(EFLOATCONST);
    node->data.floatval = val;
    node->rtype = type;

    linear = IRO_NewLinear(IROLinearOperand);
    linear->index = ++IRO_NumLinear;
    linear->rtype = type;
    linear->u.node = node;
    return linear;
}

Boolean IRO_IsAddressMultiply(IROLinear* linear) {
    return FALSE;
}

void IRO_SetupForUserBreakChecking(void) {
    IRO_LastUserBreakTick = 0;
}

void IRO_CheckForUserBreak(void) {
    if (IRO_LastUserBreakTick + 8 < (UInt32)COS_GetTicks()) {
        if (CWUserBreak(cparams.context)) {
            CError_UserBreak();
        }
        IRO_LastUserBreakTick = COS_GetTicks();
    }
}
