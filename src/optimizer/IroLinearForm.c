#include "compiler/IroLinearForm.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/types.h"
#include <string.h>

typedef struct NullCheckListNode {
    SInt32 precompid;
    Object* tempobj;
    struct NullCheckListNode* next;
} NullCheckListNode;

IROLinear* IRO_FirstLinear;
IROLinear* IRO_LastLinear;
UInt32 IRO_NumLinear;
Statement* CurStat;
static NullCheckListNode* NullCheckList;
static Statement* CurrStmt;
static Statement* PrevStmt;

static void LinkLinear(IROLinear* linear) {
    linear->index = IRO_NumLinear++;
    if (IRO_FirstLinear) {
        IRO_LastLinear->next = linear;
    } else {
        IRO_FirstLinear = linear;
    }
    IRO_LastLinear = linear;
}

IROLinear* IRO_NewLinear(IROLinearType type) {
    IROLinear* linear = oalloc(sizeof(IROLinear));
    memset(linear, 0, sizeof(IROLinear));
    linear->stmt = CurStat;
    linear->nodetype = EPOSTINC;
    linear->next = NULL;
    linear->type = type;
    linear->rtype = NULL;
    linear->flags = 0;
    linear->nodeflags = 0;
    linear->expr = NULL;
    linear->range = NULL;
    return linear;
}

// NOTE: invented names: linear, isModify
static void MarkAssigned(IROLinear* linear, Boolean isModify) {
    IROLinear* inner;
    IROAddrRecord* rec;

    if (IS_LINEAR_MONADIC(linear, EINDIRECT) && IS_LINEAR_MONADIC(linear->u.monadic, EBITFIELD)) {
        linear->flags |= IROLF_Assigned;
        inner = linear->u.monadic->u.monadic;
        if (inner->type == IROLinearOperand) {
            inner->flags |= IROLF_Assigned;
            if (isModify) {
                linear->flags |= IROLF_Used;
                inner->flags |= IROLF_Used;
            }
        }

        if (IS_LINEAR_DIADIC(inner, EADD)) {
            if (IS_LINEAR_ENODE(inner->u.diadic.left, EOBJREF)) {
                if (IS_LINEAR_ENODE(inner->u.diadic.right, EINTCONST)) {
                    inner->u.diadic.left->flags |= IROLF_Assigned;
                }
                if (isModify) {
                    linear->flags |= IROLF_Used;
                    inner->u.diadic.left->flags |= IROLF_Used;
                }
            }

            rec = IRO_InitAddrRecordPointer(inner);
            IRO_DecomposeAddressExpression(inner, rec);
            if (rec->numObjRefs == 1 && IS_LINEAR_ENODE(inner->u.diadic.left, EOBJREF)) {
                ((IROLinear*)rec->objRefs->element)->flags |= IROLF_Assigned;
                if (isModify) {
                    linear->flags |= IROLF_Used;
                    ((IROLinear*)rec->objRefs->element)->flags |= IROLF_Used;
                }
            }
        }
    }

    if (IS_LINEAR_MONADIC(linear, EINDIRECT)) {
        linear->flags |= IROLF_Assigned;
        if (linear->u.monadic->type == IROLinearOperand) {
            linear->u.monadic->flags |= IROLF_Assigned;
            if (isModify) {
                linear->flags |= IROLF_Used;
                linear->u.monadic->flags |= IROLF_Used;
            }
        }
    }

    if (IS_LINEAR_MONADIC(linear, EINDIRECT)) {
        linear->flags |= IROLF_Assigned;
        inner = linear->u.monadic;

        if (IS_LINEAR_DIADIC(inner, EADD)) {
            if (IS_LINEAR_ENODE(inner->u.diadic.left, EOBJREF)) {
                if (IS_LINEAR_ENODE(inner->u.diadic.right, EINTCONST)) {
                    inner->u.diadic.left->flags |= IROLF_Assigned;
                }
                if (isModify) {
                    linear->flags |= IROLF_Used;
                    inner->u.diadic.left->flags |= IROLF_Used;
                }
            }

            rec = IRO_InitAddrRecordPointer(inner);
            IRO_DecomposeAddressExpression(inner, rec);
            if (rec->numObjRefs == 1 && IS_LINEAR_ENODE(inner->u.diadic.left, EOBJREF)) {
                ((IROLinear*)rec->objRefs->element)->flags |= IROLF_Assigned;
                if (isModify) {
                    linear->flags |= IROLF_Used;
                    ((IROLinear*)rec->objRefs->element)->flags |= IROLF_Used;
                }
            }
        }
    }
}

static void MarkSubscript(IROLinear* linear) {
    linear->flags |= IROLF_Subs;
    if (IS_LINEAR_DIADIC(linear, EADD)) {
        if (IRO_IsAddressMultiply(linear->u.diadic.left) ||
            IS_LINEAR_MONADIC(linear->u.diadic.left, EINDIRECT)) {
            linear->u.diadic.left->flags |= IROLF_Subs;
        }
        if (IRO_IsAddressMultiply(linear->u.diadic.right) ||
            IS_LINEAR_MONADIC(linear->u.diadic.right, EINDIRECT)) {
            linear->u.diadic.right->flags |= IROLF_Subs;
        }

        if (IS_LINEAR_DIADIC(linear->u.diadic.left, EADD)) {
            MarkSubscript(linear->u.diadic.left);
        }
        if (IS_LINEAR_DIADIC(linear->u.diadic.right, EADD)) {
            MarkSubscript(linear->u.diadic.right);
        }
    }
}

static void MarkArgs(IROLinear* linear) {
    int i;
    linear->flags |= IROLF_InCallArg;

    switch (linear->type) {
        case IROLinearNop:
        case IROLinearEnd:
            break;
        case IROLinearOp1Arg:
        case IROLinearBeginCatch:
        case IROLinearEndCatch:
        case IROLinearEndCatchDtor:
            MarkArgs(linear->u.monadic);
            break;
        case IROLinearOp2Arg:
            MarkArgs(linear->u.diadic.left);
            MarkArgs(linear->u.diadic.right);
            break;
        case IROLinearIf:
        case IROLinearIfNot:
            MarkArgs(linear->u.label.cond);
            break;
        case IROLinearReturn:
            if (linear->u.monadic) {
                MarkArgs(linear->u.monadic);
            }
            break;
        case IROLinearSwitch:
            MarkArgs(linear->u.swtch.cond);
            break;
        case IROLinearOp3Arg:
            MarkArgs(linear->u.args3.a);
            MarkArgs(linear->u.args3.b);
            MarkArgs(linear->u.args3.c);
            break;
        case IROLinearFunccall:
            MarkArgs(linear->u.funccall.callee);
            for (i = 0; i < linear->u.funccall.argCount; i++) {
                MarkArgs(linear->u.funccall.args[i]);
            }
            break;
        default:
            break;
    }
}

// NOTE: invented name (inlined everywhere, so the binary has no copy of it)
static inline void MarkSubExpr(IROLinear* linear) {
    int i;

    switch (linear->type) {
        case IROLinearNop:
        case IROLinearOperand:
        case IROLinearGoto:
        case IROLinearLabel:
        case IROLinearEntry:
        case IROLinearExit:
        case IROLinearAsm:
        case IROLinearEnd:
            break;
        case IROLinearOp1Arg:
        case IROLinearBeginCatch:
        case IROLinearEndCatch:
        case IROLinearEndCatchDtor:
            linear->u.monadic->flags |= IROLF_Reffed;
            break;
        case IROLinearOp2Arg:
            linear->u.diadic.left->flags |= IROLF_Reffed;
            linear->u.diadic.right->flags |= IROLF_Reffed;
            break;
        case IROLinearIf:
        case IROLinearIfNot:
            linear->u.label.cond->flags |= IROLF_Reffed;
            break;
        case IROLinearReturn:
            if (linear->u.monadic) {
                linear->u.monadic->flags |= IROLF_Reffed;
            }
            break;
        case IROLinearSwitch:
            linear->u.swtch.cond->flags |= IROLF_Reffed;
            break;
        case IROLinearOp3Arg:
            linear->u.args3.a->flags |= IROLF_Reffed;
            linear->u.args3.b->flags |= IROLF_Reffed;
            linear->u.args3.c->flags |= IROLF_Reffed;
            break;
        case IROLinearFunccall:
            linear->u.funccall.callee->flags |= IROLF_Reffed;
            for (i = 0; i < linear->u.funccall.argCount; i++) {
                linear->u.funccall.args[i]->flags |= IROLF_Reffed;
            }
            break;
        default:
            CError_FATAL(368);
    }
}

static void MarkSubs1(IROLinear* linear) {
    IROAddrRecord* rec;

    if (IS_LINEAR_MONADIC(linear, EBITFIELD)) {
        linear = linear->u.monadic;
        if (IS_LINEAR_ENODE(linear, EOBJREF)) {
            linear->flags |= IROLF_Ind;
        }
    }

    if (IS_LINEAR_DIADIC(linear, EADD)) {
        if (IS_LINEAR_ENODE(linear->u.diadic.left, EOBJREF) &&
            IS_LINEAR_ENODE(linear->u.diadic.right, EINTCONST)) {
            linear->u.diadic.left->flags |= IROLF_Ind;
        }

        rec = IRO_InitAddrRecordPointer(linear);
        IRO_DecomposeAddressExpression(linear, rec);
        if (rec->numObjRefs == 1 && IS_LINEAR_ENODE(linear->u.diadic.left, EOBJREF)) {
            ((IROLinear*)rec->objRefs->element)->flags |= IROLF_Ind;
        }
    }
}

static void MakeLeftChildAsAssignment(ENode* enode) {
    Statement* stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_EXPRESSION;
    stmt->expr = enode->data.diadic.left;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;
}

// NOTE: invented names: enode, discardResult
static void AssignCommaRToTemp(ENode* enode, Boolean discardResult) {
    Statement* stmt;

    if (!IS_TYPE_VOID(enode->rtype) && !discardResult) {
        Object* obj = create_temp_object(enode->rtype);
        ENode* indnode = IRO_NewENode(EINDIRECT);
        indnode->data.monadic = create_objectrefnode(obj);

        if (!IS_TYPE_VOID(enode->rtype)) {
            indnode->rtype = enode->rtype;
        } else {
            CError_FATAL(548);
        }

        stmt = lalloc(sizeof(Statement));
        memset(stmt, 0, sizeof(Statement));
        stmt->type = ST_EXPRESSION;
        stmt->expr = IRO_NewENode(EASS);
        stmt->expr->data.diadic.left = indnode;
        stmt->expr->data.diadic.right = enode->data.diadic.right;
        stmt->expr->rtype = enode->rtype;
        stmt->dobjstack = CurrStmt->dobjstack;
        stmt->sourceoffset = CurrStmt->sourceoffset;
        stmt->sourcefilepath = CurrStmt->sourcefilepath;
        stmt->value = CurrStmt->value;
        stmt->flags = CurrStmt->flags;

        if (PrevStmt) {
            stmt->next = PrevStmt->next;
            PrevStmt->next = stmt;
        } else {
            stmt->next = NULL;
        }
        PrevStmt = stmt;

        enode->type = EINDIRECT;
        enode->data.monadic = create_objectrefnode(obj);
        CError_ASSERT(580, !IS_TYPE_VOID(enode->rtype));
    } else {
        stmt = lalloc(sizeof(Statement));
        memset(stmt, 0, sizeof(Statement));
        stmt->type = ST_EXPRESSION;
        stmt->expr = enode->data.diadic.right;
        stmt->dobjstack = CurrStmt->dobjstack;
        stmt->sourceoffset = CurrStmt->sourceoffset;
        stmt->sourcefilepath = CurrStmt->sourcefilepath;
        stmt->value = CurrStmt->value;
        stmt->flags = CurrStmt->flags;

        if (PrevStmt) {
            stmt->next = PrevStmt->next;
            PrevStmt->next = stmt;
        } else {
            stmt->next = NULL;
        }
        PrevStmt = stmt;

        enode->type = EINTCONST;
        enode->data.intval = cint64_zero;
    }
}

static void AssignDangerousArgumentToTemp(ENode* enode) {
    Statement* stmt;
    Object* obj;
    ENode* indnode;
    ENode* rightnode;

    obj = create_temp_object(enode->rtype);
    indnode = IRO_NewENode(EINDIRECT);
    indnode->data.monadic = create_objectrefnode(obj);

    if (!IS_TYPE_VOID(enode->rtype)) {
        indnode->rtype = enode->rtype;
    } else {
        CError_FATAL(627);
    }

    rightnode = IRO_NewENode(enode->type);
    memcpy(rightnode, enode, sizeof(ENode));

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_EXPRESSION;
    stmt->expr = IRO_NewENode(EASS);
    stmt->expr->data.diadic.left = indnode;
    stmt->expr->data.diadic.right = rightnode;
    stmt->expr->rtype = enode->rtype;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;

    enode->type = EINDIRECT;
    enode->data.monadic = create_objectrefnode(obj);
}

static void CreateTempAssignmentToZero(ENode* enode, Object** objptr) {
    Statement* stmt;
    ENode* indnode;
    ENode* rightnode;

    *objptr = create_temp_object(enode->rtype);
    indnode = IRO_NewENode(EINDIRECT);
    indnode->data.monadic = create_objectrefnode(*objptr);
    if (!IS_TYPE_VOID(enode->rtype)) {
        indnode->rtype = enode->rtype;
    } else {
        CError_FATAL(678);
    }

    rightnode = IRO_NewENode(EINTCONST);
    rightnode->data.intval = cint64_zero;
    rightnode->rtype = enode->rtype;

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_EXPRESSION;
    stmt->expr = IRO_NewENode(EASS);
    stmt->expr->data.diadic.left = indnode;
    stmt->expr->data.diadic.right = rightnode;
    stmt->expr->rtype = enode->rtype;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;
}

static void CreateTempAssignmentToOne(ENode* enode, Object** objptr) {
    Statement* stmt;
    ENode* indnode;
    ENode* rightnode;

    *objptr = create_temp_object(enode->rtype);
    indnode = IRO_NewENode(EINDIRECT);
    indnode->data.monadic = create_objectrefnode(*objptr);
    if (!IS_TYPE_VOID(enode->rtype)) {
        indnode->rtype = enode->rtype;
    } else {
        CError_FATAL(720);
    }

    rightnode = IRO_NewENode(EINTCONST);
    rightnode->data.intval = cint64_one;
    rightnode->rtype = enode->rtype;

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_EXPRESSION;
    stmt->expr = IRO_NewENode(EASS);
    stmt->expr->data.diadic.left = indnode;
    stmt->expr->data.diadic.right = rightnode;
    stmt->expr->rtype = enode->rtype;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;
}

static void GenerateCondExpression(ENode* enode, CLabel** label) {
    Statement* stmt;

    *label = IRO_NewLabel();

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_IFNGOTO;
    stmt->expr = enode->data.cond.cond;
    stmt->label = *label;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;
}

static void GenerateGoToStmt(ENode* enode, CLabel** label) {
    Statement* stmt;

    *label = IRO_NewLabel();

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_GOTO;
    stmt->label = *label;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;
}

static void PutTempOnNullCheckList(ENode* expr, Object* obj) {
    NullCheckListNode* n = lalloc(sizeof(NullCheckListNode));
    n->precompid = expr->data.nullcheck.precompid;
    n->tempobj = obj;
    n->next = NULL;
    if (NullCheckList) {
        n->next = NullCheckList;
        NullCheckList = n;
    } else {
        NullCheckList = n;
    }
}

static Object* GetTempFromtheList(ENode* expr) {
    NullCheckListNode* n;

    for (n = NullCheckList; n; n = n->next) {
        if (n->precompid == expr->data.precompid) {
            break;
        }
    }

    CError_ASSERT(839, n);
    return n->tempobj;
}

static void GenerateNullcheckExprTempAssignment(ENode* enode, Object** objptr) {
    Statement* stmt;
    ENode* indnode;

    if (!IS_TYPE_VOID(enode->rtype)) {
        *objptr = create_temp_object(enode->rtype);
    } else {
        *objptr = create_temp_object(enode->data.nullcheck.nullcheckexpr->rtype);
    }

    PutTempOnNullCheckList(enode, *objptr);

    indnode = IRO_NewENode(EINDIRECT);
    indnode->data.monadic = create_objectrefnode(*objptr);
    if (!IS_TYPE_VOID(enode->rtype)) {
        indnode->rtype = enode->rtype;
    } else {
        indnode->rtype = enode->data.nullcheck.nullcheckexpr->rtype;
    }

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_EXPRESSION;
    stmt->expr = IRO_NewENode(EASS);
    stmt->expr->data.diadic.left = indnode;
    stmt->expr->data.diadic.right = enode->data.nullcheck.nullcheckexpr;
    if (!IS_TYPE_VOID(enode->rtype)) {
        stmt->expr->rtype = enode->rtype;
    } else {
        stmt->expr->rtype = enode->data.nullcheck.nullcheckexpr->rtype;
    }

    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;
}

static void GenerateExpr1TempAssignment(ENode* enode, Object** objptr) {
    Statement* stmt;
    ENode* indnode;

    if (!IS_TYPE_VOID(enode->rtype) && !IS_TYPE_VOID(enode->data.cond.expr1->rtype)) {
        *objptr = create_temp_object(enode->rtype);
        indnode = IRO_NewENode(EINDIRECT);
        indnode->data.monadic = create_objectrefnode(*objptr);
        indnode->rtype = enode->rtype;
        CError_ASSERT(905, !IS_TYPE_VOID(enode->rtype));
    }

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_EXPRESSION;
    if (!IS_TYPE_VOID(enode->rtype) && !IS_TYPE_VOID(enode->data.cond.expr1->rtype)) {
        stmt->expr = IRO_NewENode(EASS);
        stmt->expr->data.diadic.left = indnode;
        stmt->expr->data.diadic.right = enode->data.cond.expr1;
        stmt->expr->rtype = enode->rtype;
    } else {
        stmt->expr = enode->data.cond.expr1;
    }

    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;
}

static void GenerateExpr2TempAssignment(ENode* enode, Object** objptr) {
    Statement* stmt;
    ENode* indnode;

    if (!IS_TYPE_VOID(enode->rtype) && !IS_TYPE_VOID(enode->data.cond.expr2->rtype)) {
        if (!*objptr) {
            *objptr = create_temp_object(enode->rtype);
        }
        indnode = IRO_NewENode(EINDIRECT);
        indnode->data.monadic = create_objectrefnode(*objptr);
        indnode->rtype = enode->rtype;
        CError_ASSERT(956, !IS_TYPE_VOID(enode->rtype));
    }

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_EXPRESSION;
    if (!IS_TYPE_VOID(enode->rtype) && !IS_TYPE_VOID(enode->data.cond.expr2->rtype)) {
        stmt->expr = IRO_NewENode(EASS);
        stmt->expr->data.diadic.left = indnode;
        stmt->expr->data.diadic.right = enode->data.cond.expr2;
        stmt->expr->rtype = enode->rtype;
    } else {
        stmt->expr = enode->data.cond.expr2;
    }

    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;
}

static void GenerateForceLoadTempAssignment(ENode* enode, Object** objptr) {
    Statement* stmt;
    ENode* indnode;

    if (!IS_TYPE_VOID(enode->rtype)) {
        *objptr = create_temp_object(enode->rtype);
        indnode = IRO_NewENode(EINDIRECT);
        indnode->data.monadic = create_objectrefnode(*objptr);
        indnode->rtype = enode->rtype;
        CError_ASSERT(1006, !IS_TYPE_VOID(enode->rtype));
    }

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_EXPRESSION;
    if (!IS_TYPE_VOID(enode->rtype)) {
        stmt->expr = IRO_NewENode(EASS);
        stmt->expr->data.diadic.left = indnode;
        stmt->expr->data.diadic.right = enode->data.monadic;
        stmt->expr->rtype = enode->rtype;
    } else {
        stmt->expr = enode->data.monadic;
    }

    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;
}

static void GenerateLabel(CLabel** labelptr) {
    Statement* stmt;

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_LABEL;
    stmt->label = *labelptr;
    stmt->label->stmt = stmt;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;
}

static void GenerateIfNotTemp(ENode* enode, Object** objptr, CLabel** labelptr) {
    Statement* stmt;
    ENode* indnode;

    indnode = IRO_NewENode(EINDIRECT);
    indnode->data.monadic = create_objectrefnode(*objptr);
    if (!IS_TYPE_VOID(enode->rtype)) {
        indnode->rtype = enode->rtype;
    } else {
        indnode->rtype = enode->data.monadic->rtype;
    }

    *labelptr = IRO_NewLabel();

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_IFNGOTO;
    stmt->expr = indnode;
    stmt->label = *labelptr;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;
}

static void GenerateNullcheckCondExpr(ENode* enode, Object** objptr) {
    Statement* stmt;
    ENode* indnode;

    indnode = IRO_NewENode(EINDIRECT);
    indnode->data.monadic = create_objectrefnode(*objptr);
    if (!IS_TYPE_VOID(enode->rtype)) {
        indnode->rtype = enode->rtype;
    } else {
        indnode->rtype = enode->data.nullcheck.nullcheckexpr->rtype;
    }

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_EXPRESSION;
    stmt->expr = IRO_NewENode(EASS);
    stmt->expr->data.diadic.left = indnode;
    stmt->expr->data.diadic.right = enode->data.nullcheck.condexpr;
    if (!IS_TYPE_VOID(enode->rtype)) {
        stmt->expr->rtype = enode->rtype;
    } else {
        stmt->expr->rtype = enode->data.nullcheck.nullcheckexpr->rtype;
    }
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;
}

static void TransformLogicalAndLHS(ENode* enode, Object** objptr, CLabel** labelptr) {
    Statement* stmt;

    *labelptr = IRO_NewLabel();

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_IFNGOTO;
    stmt->expr = enode->data.diadic.left;
    stmt->label = *labelptr;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;
}

static void TransformLogicalAndRHS(ENode* enode, Object** objptr, CLabel** labelptr) {
    Statement* stmt;
    ENode* indnode;
    ENode* rightnode;

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_IFNGOTO;
    stmt->expr = enode->data.diadic.right;
    stmt->label = *labelptr;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;

    indnode = IRO_NewENode(EINDIRECT);
    indnode->data.monadic = create_objectrefnode(*objptr);
    if (!IS_TYPE_VOID(enode->rtype)) {
        indnode->rtype = enode->rtype;
    } else {
        CError_FATAL(1228);
    }

    rightnode = IRO_NewENode(EINTCONST);
    rightnode->data.intval = cint64_one;
    rightnode->rtype = enode->rtype;

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_EXPRESSION;
    stmt->expr = IRO_NewENode(EASS);
    stmt->expr->data.diadic.left = indnode;
    stmt->expr->data.diadic.right = rightnode;
    stmt->expr->rtype = enode->rtype;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_LABEL;
    stmt->label = *labelptr;
    stmt->label->stmt = stmt;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;

    enode->type = EINDIRECT;
    enode->data.monadic = create_objectrefnode(*objptr);
    CError_ASSERT(1279, !IS_TYPE_VOID(enode->rtype));
}

static void TransformLogicalOrLHS(ENode* enode, Object** objptr, CLabel** labelptr) {
    Statement* stmt;

    *labelptr = IRO_NewLabel();

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_IFGOTO;
    stmt->expr = enode->data.diadic.left;
    stmt->label = *labelptr;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;
}

static void TransformLogicalOrRHS(ENode* enode, Object** objptr, CLabel** labelptr) {
    Statement* stmt;
    ENode* indnode;
    ENode* rightnode;

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_IFGOTO;
    stmt->expr = enode->data.diadic.right;
    stmt->label = *labelptr;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;

    indnode = IRO_NewENode(EINDIRECT);
    indnode->data.monadic = create_objectrefnode(*objptr);
    if (!IS_TYPE_VOID(enode->rtype)) {
        indnode->rtype = enode->rtype;
    } else {
        CError_FATAL(1357);
    }

    rightnode = IRO_NewENode(EINTCONST);
    rightnode->data.intval = cint64_zero;
    rightnode->rtype = enode->rtype;

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_EXPRESSION;
    stmt->expr = IRO_NewENode(EASS);
    stmt->expr->data.diadic.left = indnode;
    stmt->expr->data.diadic.right = rightnode;
    stmt->expr->rtype = enode->rtype;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;

    stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = ST_LABEL;
    stmt->label = *labelptr;
    stmt->label->stmt = stmt;
    stmt->dobjstack = CurrStmt->dobjstack;
    stmt->sourceoffset = CurrStmt->sourceoffset;
    stmt->sourcefilepath = CurrStmt->sourcefilepath;
    stmt->value = CurrStmt->value;
    stmt->flags = CurrStmt->flags;

    if (PrevStmt) {
        stmt->next = PrevStmt->next;
        PrevStmt->next = stmt;
    } else {
        stmt->next = NULL;
    }
    PrevStmt = stmt;

    enode->type = EINDIRECT;
    enode->data.monadic = create_objectrefnode(*objptr);
    CError_ASSERT(1408, !IS_TYPE_VOID(enode->rtype));
}

// NOTE: invented names: expr, parent, discardResult, unused
static void LinearizeExpr1(ENode* expr, ENode* parent, Boolean discardResult, Boolean unused) {
    switch (expr->type) {
        case EPOSTINC:
        case EPOSTDEC:
        case EPREINC:
        case EPREDEC:
        case EINDIRECT:
        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
        case EFORCELOAD:
        case ETYPCON:
        case EBITFIELD:
            LinearizeExpr1(expr->data.monadic, expr, FALSE, FALSE);
            if (expr->type == EFORCELOAD) {
                Object* obj;
                GenerateForceLoadTempAssignment(expr, &obj);
                expr->type = EINDIRECT;
                expr->data.monadic = create_objectrefnode(obj);
                CError_ASSERT(1431, !IS_TYPE_VOID(expr->rtype));
            }
            break;
        case EMUL:
        case EMULV:
        case EDIV:
        case EMODULO:
        case EADDV:
        case ESUBV:
        case EADD:
        case ESUB:
        case ESHL:
        case ESHR:
        case ELESS:
        case EGREATER:
        case ELESSEQU:
        case EGREATEREQU:
        case EEQU:
        case ENOTEQU:
        case EAND:
        case EXOR:
        case EOR:
        case ECOMMA:
        case EPMODULO:
        case EROTL:
        case EROTR:
        case EBTST:
            if (expr->type == ECOMMA) {
                LinearizeExpr1(expr->data.diadic.left, expr, TRUE, FALSE);
                MakeLeftChildAsAssignment(expr);
                LinearizeExpr1(expr->data.diadic.right, expr, FALSE, FALSE);
                AssignCommaRToTemp(expr, discardResult);
            } else if (expr->data.diadic.right->cost >= expr->data.diadic.left->cost) {
                LinearizeExpr1(expr->data.diadic.right, expr, FALSE, FALSE);
                LinearizeExpr1(expr->data.diadic.left, expr, FALSE, FALSE);
            } else {
                LinearizeExpr1(expr->data.diadic.left, expr, FALSE, FALSE);
                LinearizeExpr1(expr->data.diadic.right, expr, FALSE, FALSE);
            }
            break;
        case EASS:
        case EMULASS:
        case EDIVASS:
        case EMODASS:
        case EADDASS:
        case ESUBASS:
        case ESHLASS:
        case ESHRASS:
        case EANDASS:
        case EXORASS:
        case EORASS:
        case EBCLR:
        case EBSET:
            LinearizeExpr1(expr->data.diadic.right, expr, FALSE, FALSE);
            LinearizeExpr1(expr->data.diadic.left, expr, FALSE, FALSE);
            break;
        case ELAND:
        case ELOR:
            if (expr->type == ELAND) {
                CLabel* label;
                Object* obj;
                CreateTempAssignmentToZero(expr, &obj);
                LinearizeExpr1(expr->data.diadic.left, expr, FALSE, FALSE);
                TransformLogicalAndLHS(expr, &obj, &label);
                LinearizeExpr1(expr->data.diadic.right, expr, FALSE, FALSE);
                TransformLogicalAndRHS(expr, &obj, &label);
            } else if (expr->type == ELOR) {
                CLabel* label;
                Object* obj;
                CreateTempAssignmentToOne(expr, &obj);
                LinearizeExpr1(expr->data.diadic.left, expr, FALSE, FALSE);
                TransformLogicalOrLHS(expr, &obj, &label);
                LinearizeExpr1(expr->data.diadic.right, expr, FALSE, FALSE);
                TransformLogicalOrRHS(expr, &obj, &label);
            }
            break;
        case ECOND: {
            CLabel *label1, *label2;
            Object* obj = NULL;
            LinearizeExpr1(expr->data.cond.cond, expr, FALSE, FALSE);
            GenerateCondExpression(expr, &label1);
            LinearizeExpr1(expr->data.cond.expr1, expr, FALSE, FALSE);
            GenerateExpr1TempAssignment(expr, &obj);
            GenerateGoToStmt(expr, &label2);
            GenerateLabel(&label1);
            LinearizeExpr1(expr->data.cond.expr2, expr, FALSE, FALSE);
            GenerateExpr2TempAssignment(expr, &obj);
            GenerateLabel(&label2);
            if (!IS_TYPE_VOID(expr->rtype)) {
                expr->type = EINDIRECT;
                expr->data.monadic = create_objectrefnode(obj);
                CError_ASSERT(1599, !IS_TYPE_VOID(expr->rtype));
            } else {
                expr->type = EINTCONST;
                expr->data.intval = cint64_zero;
            }
            break;
        }
        case EPRECOMP: {
            Object* temp = GetTempFromtheList(expr);
            expr->type = EINDIRECT;
            expr->data.monadic = create_objectrefnode(temp);
            CError_ASSERT(1617, !IS_TYPE_VOID(expr->rtype));
            break;
        }
        case ENULLCHECK: {
            CLabel* label;
            Object* obj;
            LinearizeExpr1(expr->data.nullcheck.nullcheckexpr, expr, FALSE, FALSE);
            GenerateNullcheckExprTempAssignment(expr, &obj);
            GenerateIfNotTemp(expr, &obj, &label);
            LinearizeExpr1(expr->data.nullcheck.condexpr, expr, FALSE, FALSE);
            GenerateNullcheckCondExpr(expr, &obj);
            GenerateLabel(&label);
            if (!IS_TYPE_VOID(expr->rtype)) {
                expr->type = EINDIRECT;
                expr->data.monadic = create_objectrefnode(obj);
                CError_ASSERT(1642, !IS_TYPE_VOID(expr->rtype));
            } else {
                expr->type = EINTCONST;
                expr->data.intval = cint64_zero;
            }
            break;
        }
        case EFUNCCALL:
        case EFUNCCALLP: {
            SInt32 count;
            SInt32 i;
            ENodeList* list;
            ENode** arr;
            SInt16* arr2;

            IRO_IsLeafFunction = FALSE;

            list = expr->data.funccall.args;
            count = 0;
            while (list) {
                count++;
                list = list->next;
            }

            if (count) {
                arr = oalloc(sizeof(ENode*) * count);
                list = expr->data.funccall.args;
                count = 0;
                while (list) {
                    arr[count] = list->node;
                    list = list->next;
                    count++;
                }

                arr2 = oalloc(sizeof(SInt16) * count);
                for (i = 0; i < count; i++) {
                    arr2[i] = count - i - 1;
                }

                for (i = 0; i < count; i++) {
                    LinearizeExpr1(arr[arr2[i]], expr, FALSE, FALSE);
                }
            }

            LinearizeExpr1(expr->data.funccall.funcref, expr, FALSE, FALSE);
            break;
        }

        case EINTCONST:
        case EFLOATCONST:
        case ESTRINGCONST:
        case EOBJREF:
        case ESETCONST:
        case EVECTOR128CONST:
            break;

        default:
            CError_FATAL(1726);
    }
}

static IROLinear* LinearizeExpr(ENode* enode) {
    IROLinear* linear = NULL;

    switch (enode->type) {
        case EPOSTINC:
        case EPOSTDEC:
        case EPREINC:
        case EPREDEC:
        case EINDIRECT:
        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
        case EFORCELOAD:
        case ETYPCON:
        case EBITFIELD:
            linear = IRO_NewLinear(IROLinearOp1Arg);
            linear->u.monadic = LinearizeExpr(enode->data.monadic);
            linear->nodetype = enode->type;
            linear->rtype = enode->rtype;
            linear->nodeflags = enode->flags;
            if (IRO_IsAssignOp[linear->nodetype]) {
                MarkAssigned(linear->u.monadic, TRUE);
            }
            if (linear->nodetype == EINDIRECT) {
                MarkSubs1(linear->u.monadic);
                linear->u.monadic->flags |= IROLF_Immind | IROLF_Ind;
                if (IS_LINEAR_DIADIC(linear->u.monadic, EADD)) {
                    MarkSubscript(linear->u.monadic);
                }
            }
            break;
        case EMUL:
        case EMULV:
        case EDIV:
        case EMODULO:
        case EADDV:
        case ESUBV:
        case EADD:
        case ESUB:
        case ESHL:
        case ESHR:
        case ELESS:
        case EGREATER:
        case ELESSEQU:
        case EGREATEREQU:
        case EEQU:
        case ENOTEQU:
        case EAND:
        case EXOR:
        case EOR:
        case ECOMMA:
        case EPMODULO:
        case EROTL:
        case EROTR:
        case EBTST:
            linear = IRO_NewLinear(IROLinearOp2Arg);
            linear->nodeflags = enode->flags;
            if (!ENODE_IS(enode, ECOMMA) &&
                enode->data.diadic.right->cost >= enode->data.diadic.left->cost) {
                linear->u.diadic.right = LinearizeExpr(enode->data.diadic.right);
                linear->u.diadic.left = LinearizeExpr(enode->data.diadic.left);
                linear->flags |= IROLF_RightFirst;
            } else {
                linear->u.diadic.left = LinearizeExpr(enode->data.diadic.left);
                linear->u.diadic.right = LinearizeExpr(enode->data.diadic.right);
            }
            linear->nodetype = enode->type;
            linear->rtype = enode->rtype;
            if (IRO_IsAssignOp[linear->nodetype]) {
                MarkAssigned(linear->u.diadic.left, IRO_IsModifyOp[linear->nodetype]);
            }
            break;
        case EASS:
        case EMULASS:
        case EDIVASS:
        case EMODASS:
        case EADDASS:
        case ESUBASS:
        case ESHLASS:
        case ESHRASS:
        case EANDASS:
        case EXORASS:
        case EORASS:
        case EBCLR:
        case EBSET:
            linear = IRO_NewLinear(IROLinearOp2Arg);
            linear->nodeflags = enode->flags;
            linear->u.diadic.right = LinearizeExpr(enode->data.diadic.right);
            linear->u.diadic.left = LinearizeExpr(enode->data.diadic.left);
            linear->flags |= IROLF_RightFirst;
            linear->nodetype = enode->type;
            linear->rtype = enode->rtype;
            MarkAssigned(linear->u.diadic.left, IRO_IsModifyOp[linear->nodetype]);
            break;
        case EINTCONST:
        case EFLOATCONST:
        case ESTRINGCONST:
        case EOBJREF:
        case EVECTOR128CONST:
            linear = IRO_NewLinear(IROLinearOperand);
            linear->nodeflags = enode->flags;
            linear->u.node = enode;
            linear->rtype = enode->rtype;
            break;
        case EFUNCCALL:
        case EFUNCCALLP: {
            SInt32 count;
            SInt32 i;
            ENodeList* list;
            ENode** arr;
            SInt16* arr2;

            linear = IRO_NewLinear(IROLinearFunccall);
            linear->nodeflags = enode->flags;
            linear->u.funccall.ispascal = enode->type == EFUNCCALLP;

            list = enode->data.funccall.args;
            count = 0;
            while (list) {
                count++;
                list = list->next;
            }

            arr = NULL;
            if (count) {
                arr = oalloc(sizeof(ENode*) * count);
                list = enode->data.funccall.args;
                count = 0;
                while (list) {
                    arr[count] = list->node;
                    list = list->next;
                    count++;
                }

                arr2 = oalloc(sizeof(SInt16) * count);
                for (i = 0; i < count; i++) {
                    arr2[i] = count - i - 1;
                }

                for (i = 0; i < count; i++) {
                    arr[arr2[i]] = (ENode*)LinearizeExpr(arr[arr2[i]]);
                    MarkArgs((IROLinear*)arr[arr2[i]]);
                }
            }

            linear->u.funccall.argCount = count;
            linear->u.funccall.args = (IROLinear**)arr;
            linear->u.funccall.callee = LinearizeExpr(enode->data.funccall.funcref);
            linear->u.funccall.functype = enode->data.funccall.functype;
            linear->rtype = enode->rtype;
            break;
        }
        default:
            CError_FATAL(1946);
    }

    if (linear) {
        LinkLinear(linear);
    }
    return linear;
}

void IRO_PreLinearize(Statement* stmt) {
    IRO_FirstLinear = IRO_LastLinear = NULL;
    IRO_NumLinear = 0;
    CurrStmt = PrevStmt = NULL;

    while (stmt) {
        CurStat = stmt;
        CurrStmt = stmt;
        NullCheckList = NULL;

        switch (stmt->type) {
            case ST_NOP:
            case ST_LABEL:
            case ST_GOTO:
                break;
            case ST_OVF:
                CError_FATAL(1992);
            case ST_EXPRESSION:
                LinearizeExpr1(stmt->expr, NULL, FALSE, FALSE);
                break;
            case ST_SWITCH:
                LinearizeExpr1(stmt->expr, NULL, FALSE, FALSE);
                break;
            case ST_IFGOTO:
                LinearizeExpr1(stmt->expr, NULL, FALSE, FALSE);
                break;
            case ST_IFNGOTO:
                LinearizeExpr1(stmt->expr, NULL, FALSE, FALSE);
                break;
            case ST_RETURN:
                if (stmt->expr) {
                    LinearizeExpr1(stmt->expr, NULL, FALSE, FALSE);
                }
                break;
            case ST_BEGINCATCH:
                LinearizeExpr1(stmt->expr, NULL, FALSE, FALSE);
                break;
            case ST_ENDCATCH:
                LinearizeExpr1(stmt->expr, NULL, FALSE, FALSE);
                break;
            case ST_ENDCATCHDTOR:
                LinearizeExpr1(stmt->expr, NULL, FALSE, FALSE);
                break;

            case ST_EXIT:
            case ST_ENTRY:
            case ST_ASM:
                break;

            default:
                CError_FATAL(2041);
        }

        PrevStmt = stmt;
        stmt = stmt->next;
    }

    IRO_CheckForUserBreak();
}

static void MarkAllSubExprs(IROLinear* linear) {
    IROLinear* scan;

    for (scan = linear; scan; scan = scan->next) {
        scan->flags &= ~IROLF_Reffed;
    }

    for (scan = linear; scan; scan = scan->next) {
        MarkSubExpr(scan);
    }
}

void IRO_Linearize(Statement* stmt) {
    IROLinear* linear;

    IRO_FirstLinear = IRO_LastLinear = NULL;
    IRO_NumLinear = 0;

    while (stmt) {
        CurStat = stmt;
        linear = NULL;

        switch (stmt->type) {
            case ST_NOP:
                linear = IRO_NewLinear(IROLinearNop);
                break;
            case ST_LABEL:
                linear = IRO_NewLinear(IROLinearLabel);
                linear->u.label.label = stmt->label;
                linear->flags |= IROLF_StartsNode;
                break;
            case ST_GOTO:
                linear = IRO_NewLinear(IROLinearGoto);
                linear->u.label.label = stmt->label;
                break;
            case ST_EXPRESSION:
                LinearizeExpr(stmt->expr);
                break;
            case ST_SWITCH:
                linear = IRO_NewLinear(IROLinearSwitch);
                linear->u.swtch.cond = LinearizeExpr(stmt->expr);
                linear->u.swtch.info = (SwitchInfo*)stmt->label;
                break;
            case ST_IFGOTO:
                linear = IRO_NewLinear(IROLinearIf);
                linear->u.label.cond = LinearizeExpr(stmt->expr);
                linear->u.label.label = stmt->label;
                break;
            case ST_IFNGOTO:
                linear = IRO_NewLinear(IROLinearIfNot);
                linear->u.label.cond = LinearizeExpr(stmt->expr);
                linear->u.label.label = stmt->label;
                break;
            case ST_RETURN:
                IRO_FunctionHasReturn = TRUE;
                linear = IRO_NewLinear(IROLinearReturn);
                if (stmt->expr) {
                    linear->u.monadic = LinearizeExpr(stmt->expr);
                } else {
                    linear->u.monadic = NULL;
                }
                break;
            case ST_OVF:
                CError_FATAL(2146);
                break;
            case ST_EXIT:
                linear = IRO_NewLinear(IROLinearExit);
                linear->u.label.label = stmt->label;
                break;
            case ST_ENTRY:
                linear = IRO_NewLinear(IROLinearEntry);
                linear->u.label.label = stmt->label;
                linear->flags |= IROLF_StartsNode;
                break;
            case ST_BEGINCATCH:
                linear = IRO_NewLinear(IROLinearBeginCatch);
                linear->u.ctch.linear = LinearizeExpr(stmt->expr);
                linear->u.ctch.unused1 = 0;
                linear->u.ctch.unused2 = 0;
                break;
            case ST_ENDCATCH:
                linear = IRO_NewLinear(IROLinearEndCatch);
                linear->u.monadic = LinearizeExpr(stmt->expr);
                break;
            case ST_ENDCATCHDTOR:
                linear = IRO_NewLinear(IROLinearEndCatchDtor);
                linear->u.monadic = LinearizeExpr(stmt->expr);
                break;
            case ST_ASM:
                linear = IRO_NewLinear(IROLinearAsm);
                linear->u.asm_stmt = stmt;
                if (copts.optimizewithasm) {
                    IAEffects effects;
                    CodeGen_GetAsmEffects(stmt, &effects);
                    if (effects.optimizationBarrier || effects.unmodeledControlFlow) {
                        DisableDueToAsm = TRUE;
                    }
                } else {
                    DisableDueToAsm = TRUE;
                }
                break;
            default:
                CError_FATAL(2197);
        }

        if (linear) {
            LinkLinear(linear);
        }
        stmt = stmt->next;
    }

    linear = IRO_NewLinear(IROLinearEnd);
    linear->flags |= IROLF_StartsNode;
    LinkLinear(linear);

    MarkAllSubExprs(IRO_FirstLinear);
    IRO_CheckForUserBreak();
}

static Statement* NewStatement(IROLinear* linear, StatementType sttype) {
    Statement* stmt = lalloc(sizeof(Statement));
    memset(stmt, 0, sizeof(Statement));
    stmt->type = sttype;
    stmt->value = 1;
    if (linear->stmt) {
        stmt->dobjstack = linear->stmt->dobjstack;
        stmt->sourceoffset = linear->stmt->sourceoffset;
        stmt->sourcefilepath = linear->stmt->sourcefilepath;
        stmt->value = linear->stmt->value;
        stmt->flags = linear->stmt->flags;
    } else {
        stmt->sourceoffset = -1;
        stmt->sourcefilepath = NULL;
    }
    return stmt;
}

ENode* IRO_NewENode(ENodeType nodetype) {
    ENode* enode = lalloc(sizeof(ENode));
    memset(enode, 0, sizeof(ENode));
    enode->type = nodetype;
    return enode;
}

static ENode* BuildExpr(IROLinear* linear) {
    ENode* enode;

    switch (linear->type) {
        case IROLinearOperand:
            enode = IRO_NewENode(linear->u.node->type);
            enode->flags = linear->nodeflags;
            *enode = *linear->u.node;
            break;
        case IROLinearOp1Arg:
            enode = IRO_NewENode(linear->nodetype);
            enode->flags = linear->nodeflags;
            enode->data.monadic = BuildExpr(linear->u.monadic);
            enode->rtype = linear->rtype;
            enode->cost = enode->data.monadic->cost;
            if (!enode->cost) {
                enode->cost = 1;
            }
            break;
        case IROLinearOp2Arg:
            enode = IRO_NewENode(linear->nodetype);
            enode->flags = linear->nodeflags;
            enode->data.diadic.left = BuildExpr(linear->u.diadic.left);
            enode->data.diadic.right = BuildExpr(linear->u.diadic.right);

            enode->cost = enode->data.diadic.left->cost;
            if (enode->data.diadic.right->cost > enode->cost) {
                enode->cost = enode->data.diadic.right->cost;
            } else if (enode->data.diadic.right->cost == enode->cost) {
                enode->cost += 1;
            }

            if (ENODE_IS4(enode, ESHL, ESHR, EDIV, EMODULO)) {
                enode->cost += 2;
            }
            if (enode->cost > 200) {
                enode->cost = 200;
            }

            enode->rtype = linear->rtype;
            break;
        case IROLinearOp3Arg:
            enode = IRO_NewENode(linear->nodetype);
            enode->flags = linear->nodeflags;
            enode->data.cond.cond = BuildExpr(linear->u.args3.a);
            enode->data.cond.expr1 = BuildExpr(linear->u.args3.b);
            enode->data.cond.expr2 = BuildExpr(linear->u.args3.c);
            enode->rtype = linear->rtype;

            enode->cost = enode->data.cond.cond->cost;
            if (enode->data.cond.expr1->cost > enode->cost) {
                enode->cost = enode->data.cond.expr1->cost;
            }
            if (enode->data.cond.expr2->cost > enode->cost) {
                enode->cost = enode->data.cond.expr2->cost;
            }
            enode->cost += 1;

            if (enode->cost > 200) {
                enode->cost = 200;
            }

            break;

        case IROLinearFunccall: {
            ENodeList* list;
            int i;
            enode = IRO_NewENode(linear->u.funccall.ispascal ? EFUNCCALLP : EFUNCCALL);
            enode->flags = linear->nodeflags;
            enode->data.funccall.funcref = BuildExpr(linear->u.funccall.callee);
            enode->data.funccall.functype = linear->u.funccall.functype;
            enode->data.funccall.args = NULL;
            enode->cost = 200;
            for (i = linear->u.funccall.argCount - 1; i >= 0; i--) {
                list = lalloc(sizeof(ENodeList));
                list->node = BuildExpr(linear->u.funccall.args[i]);
                list->next = enode->data.funccall.args;
                enode->data.funccall.args = list;
            }
            enode->rtype = linear->rtype;
            break;
        }

        default:
            IRO_Dump("Oh, oh, bad expression type in BuildExpr at: %d\n", linear->index);
            CError_FATAL(2393);
    }

    enode->pointsTo = linear->pointsToFunction;
    return enode;
}

Statement* IRO_Delinearize(IRONode* node, IROLinear* linear) {
    IROLinear* scanlin;
    Statement* firstStmt;
    Statement* lastStmt;
    IRONode* scan;
    Statement* stmt;
    IRONode mynode;

    firstStmt = lastStmt = NULL;

    IRO_RenumberInts();
    if (node) {
        scan = node;
        MarkAllSubExprs(IRO_FirstLinear);
    } else {
        memset(&mynode, 0, sizeof(IRONode));
        mynode.first = linear;
        scan = &mynode;
        MarkAllSubExprs(linear);
    }

    while (scan) {
        for (scanlin = scan->first; scanlin; scanlin = scanlin->next) {
            stmt = NULL;
            if (!(scanlin->flags & IROLF_Reffed)) {
                switch (scanlin->type) {
                    case IROLinearNop:
                        if (scan == node) {
                            stmt = NewStatement(scanlin, ST_NOP);
                        } else {
                            stmt = NULL;
                        }
                        break;
                    case IROLinearOperand:
                    case IROLinearOp1Arg:
                    case IROLinearOp2Arg:
                    case IROLinearOp3Arg:
                    case IROLinearFunccall:
                        stmt = NewStatement(scanlin, ST_EXPRESSION);
                        stmt->expr = BuildExpr(scanlin);
                        break;
                    case IROLinearGoto:
                        stmt = NewStatement(scanlin, ST_GOTO);
                        stmt->label = scanlin->u.label.label;
                        break;
                    case IROLinearExit:
                        stmt = NewStatement(scanlin, ST_EXIT);
                        stmt->label = scanlin->u.label.label;
                        break;
                    case IROLinearIf:
                    case IROLinearIfNot:
                        stmt = NewStatement(
                            scanlin, (scanlin->type == IROLinearIf) ? ST_IFGOTO : ST_IFNGOTO
                        );
                        stmt->label = scanlin->u.label.label;
                        stmt->expr = BuildExpr(scanlin->u.label.cond);
                        break;
                    case IROLinearReturn:
                        stmt = NewStatement(scanlin, ST_RETURN);
                        if (scanlin->u.monadic) {
                            stmt->expr = BuildExpr(scanlin->u.monadic);
                        }
                        break;
                    case IROLinearLabel:
                        stmt = NewStatement(scanlin, ST_LABEL);
                        stmt->label = scanlin->u.label.label;
                        stmt->label->stmt = stmt;
                        break;
                    case IROLinearEntry:
                        stmt = NewStatement(scanlin, ST_ENTRY);
                        stmt->label = scanlin->u.label.label;
                        stmt->label->stmt = stmt;
                        break;
                    case IROLinearSwitch:
                        stmt = NewStatement(scanlin, ST_SWITCH);
                        stmt->expr = BuildExpr(scanlin->u.swtch.cond);
                        stmt->label = (CLabel*)scanlin->u.swtch.info;
                        break;
                    case IROLinearBeginCatch:
                        stmt = NewStatement(scanlin, ST_BEGINCATCH);
                        stmt->expr = BuildExpr(scanlin->u.ctch.linear);
                        break;
                    case IROLinearEndCatch:
                        stmt = NewStatement(scanlin, ST_ENDCATCH);
                        stmt->expr = BuildExpr(scanlin->u.monadic);
                        break;
                    case IROLinearEndCatchDtor:
                        stmt = NewStatement(scanlin, ST_ENDCATCHDTOR);
                        stmt->expr = BuildExpr(scanlin->u.monadic);
                        break;
                    case IROLinearAsm:
                        stmt = scanlin->u.asm_stmt;
                        break;
                    case IROLinearEnd:
                        stmt = NULL;
                        break;
                    default:
                        CError_FATAL(2692);
                }

                if (stmt) {
                    if (LoopOptimizerRun) {
                        SInt32 i;
                        SInt32 value = 1;
                        for (i = 0; i < scan->loopdepth; i++) {
                            value = (value < 4096) ? (value * 8) : (value + 1);
                        }
                        stmt->value = value;
                    }
                    if (firstStmt) {
                        lastStmt->next = stmt;
                    } else {
                        firstStmt = stmt;
                    }
                    lastStmt = stmt;
                }
            }
            if (scanlin == scan->last) {
                break;
            }
        }
        scan = scan->nextnode;
    }

    return firstStmt;
}

void IRO_RenumberInts(void) {
    IROLinear* linear = IRO_FirstLinear;
    IRO_NumLinear = 0;
    while (linear) {
        linear->index = IRO_NumLinear++;
        linear = linear->next;
    }
}

static void TravExprToUpdateFlags(IROLinear* linear) {
    int i;

    linear->flags &=
        ~(IROLF_Assigned | IROLF_ReplaceCSE | IROLF_Used | IROLF_Ind | IROLF_Subs |
          IROLF_LoopInvariant | IROLF_Ris | IROLF_Immind | IROLF_CouldError);
    switch (linear->type) {
        case IROLinearNop:
        case IROLinearOperand:
            break;
        case IROLinearOp1Arg:
            TravExprToUpdateFlags(linear->u.monadic);
            if (IRO_IsAssignOp[linear->nodetype]) {
                MarkAssigned(linear->u.monadic, TRUE);
            }
            if (linear->nodetype == EINDIRECT) {
                MarkSubs1(linear->u.monadic);
                linear->u.monadic->flags |= IROLF_Immind | IROLF_Ind;
                if (IS_LINEAR_DIADIC(linear->u.monadic, EADD)) {
                    MarkSubscript(linear->u.monadic);
                }
            }
            break;
        case IROLinearOp2Arg:
            TravExprToUpdateFlags(linear->u.diadic.left);
            TravExprToUpdateFlags(linear->u.diadic.right);
            if (IRO_IsAssignOp[linear->nodetype]) {
                MarkAssigned(linear->u.diadic.left, IRO_IsModifyOp[linear->nodetype]);
            }
            break;
        case IROLinearOp3Arg:
            TravExprToUpdateFlags(linear->u.args3.a);
            TravExprToUpdateFlags(linear->u.args3.b);
            TravExprToUpdateFlags(linear->u.args3.c);
            break;
        case IROLinearFunccall:
            TravExprToUpdateFlags(linear->u.funccall.callee);
            for (i = linear->u.funccall.argCount - 1; i >= 0; i--) {
                TravExprToUpdateFlags(linear->u.funccall.args[i]);
            }
            break;
        default:
            IRO_Dump(
                "Oh, oh, bad expression type in TravExprToUpdateFlags at: %d\n", linear->index
            );
            CError_FATAL(2860);
    }
}

void IRO_UpdateFlagsOnInts(void) {
    IROLinear* linear;
    IRONode* node;

    MarkAllSubExprs(IRO_FirstLinear);

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        for (linear = node->first; linear; linear = linear->next) {
            if (!(linear->flags & IROLF_Reffed)) {
                switch (linear->type) {
                    case IROLinearOperand:
                    case IROLinearOp1Arg:
                    case IROLinearOp2Arg:
                    case IROLinearOp3Arg:
                    case IROLinearFunccall:
                        TravExprToUpdateFlags(linear);
                        break;
                    case IROLinearIf:
                    case IROLinearIfNot:
                        TravExprToUpdateFlags(linear->u.label.cond);
                        break;
                    case IROLinearReturn:
                        if (linear->u.monadic) {
                            TravExprToUpdateFlags(linear->u.monadic);
                        }
                        break;
                    case IROLinearSwitch:
                        TravExprToUpdateFlags(linear->u.swtch.cond);
                        break;
                    case IROLinearBeginCatch:
                        TravExprToUpdateFlags(linear->u.ctch.linear);
                        break;
                    case IROLinearEndCatch:
                        TravExprToUpdateFlags(linear->u.monadic);
                        break;
                    case IROLinearEndCatchDtor:
                        TravExprToUpdateFlags(linear->u.monadic);
                        break;
                    case IROLinearNop:
                    case IROLinearGoto:
                    case IROLinearLabel:
                    case IROLinearEntry:
                    case IROLinearExit:
                    case IROLinearAsm:
                    case IROLinearEnd:
                        break;
                    default:
                        CError_FATAL(2938);
                }
            }
            if (linear == node->last) {
                break;
            }
        }
    }
}

void IRO_SaveLinearIR(IROLinearIRSave* save) {
    save->firstLinear = IRO_FirstLinear;
    save->lastLinear = IRO_LastLinear;
    save->numLinear = IRO_NumLinear;
    save->curStat = CurStat;
    save->disableDueToAsm = DisableDueToAsm;
    save->isLeafFunction = IRO_IsLeafFunction;
    save->functionHasReturn = IRO_FunctionHasReturn;
    save->nullCheckList = NullCheckList;
    save->currStmt = CurrStmt;
    save->prevStmt = PrevStmt;
}

void IRO_RestoreLinearIR(IROLinearIRSave* save) {
    IRO_FirstLinear = save->firstLinear;
    IRO_LastLinear = save->lastLinear;
    IRO_NumLinear = save->numLinear;
    CurStat = save->curStat;
    DisableDueToAsm = save->disableDueToAsm;
    IRO_IsLeafFunction = save->isLeafFunction;
    IRO_FunctionHasReturn = save->functionHasReturn;
    NullCheckList = save->nullCheckList;
    CurrStmt = save->currStmt;
    PrevStmt = save->prevStmt;
}
