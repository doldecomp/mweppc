#include "compiler/CException.h"
#include "compiler/CABI.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CInline.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include <stdio.h>
#include <string.h>

typedef struct UniqueObj {
    struct UniqueObj* next;
    Object* object;
    SInt32 uniqueid;
} UniqueObj;

ExceptionAction* cexcept_dobjstack;
Boolean cexcept_hasdobjects;
Boolean cexcept_magic;
static UniqueObj* cexcept_uniqueobjs;
static Boolean cexcept_canthrow;
static DtorTemp* cexcept_dtortemps;
static Statement* cexcept_prevstmt;
static ExceptionAction* cexcept_eabefore;
static ExceptionAction* cexcept_eaafter;
static Boolean cexcept_expandtrycatch;
static Boolean cexcept_hastrycatch;
static Boolean cexcept_serialize;

static ENode* CExcept_TempTransExprCond(ENode* expr);
static ENode* CExcept_TempTransExpr(ENode* expr);

void CExcept_Setup(void) {
    cexcept_dobjstack = NULL;
    cexcept_uniqueobjs = NULL;
    cexcept_hasdobjects = FALSE;
    cexcept_magic = FALSE;
}

// NOTE: invented names: object, isVirtualCall
Boolean CExcept_CanThrowException(Object* object, Boolean isVirtualCall) {
    if (copts.optEH && IS_TYPE_FUNC(object->type)) {
        if (isVirtualCall) {
            return TRUE;
        }

        if (TYPE_FUNC(object->type)->flags & FUNC_NOTHROW) {
            return FALSE;
        }

        if (!isVirtualCall &&
            TYPE_FUNC(object->type)->exspecs &&
            TYPE_FUNC(object->type)->exspecs &&
            !TYPE_FUNC(object->type)->exspecs->type) {
            return FALSE;
        }

        if (copts.optEH2 &&
            !(object->qual & Q_MANGLE_NAME) &&
            object != rt_ptmf_call &&
            object != rt_ptmf_scall &&
            object != Rdync_func &&
            object != rt_som_glue1 &&
            object != rt_som_glue2 &&
            object != rt_som_glue3 &&
            object != carr_func &&
            object != cnar_func &&
            object != darr_func &&
            object != dnar_func &&
            object != dnar3_func &&
            object != Xthrw_func) {
            return FALSE;
        }
    }

    return TRUE;
}

void CExcept_CheckStackRefs(ExceptionAction* actions) {
    while (actions) {
        switch (actions->type) {
            case EAT_DESTROYLOCAL:
                CInline_ObjectAddrRef(actions->data.destroy_local.dtor);
                break;
            case EAT_DESTROYLOCALCOND:
                CInline_ObjectAddrRef(actions->data.destroy_local_cond.dtor);
                break;
            case EAT_DESTROYLOCALOFFSET:
                CInline_ObjectAddrRef(actions->data.destroy_local_offset.dtor);
                break;
            case EAT_DESTROYLOCALPOINTER:
                CInline_ObjectAddrRef(actions->data.destroy_local_pointer.dtor);
                break;
            case EAT_DESTROYLOCALARRAY:
                CInline_ObjectAddrRef(actions->data.destroy_local_array.dtor);
                break;
            case EAT_DESTROYPARTIALARRAY:
                CInline_ObjectAddrRef(actions->data.destroy_partial_array.dtor);
                break;
            case EAT_DESTROYMEMBER:
            case EAT_DESTROYBASE:
                CInline_ObjectAddrRef(actions->data.destroy_member.dtor);
                break;
            case EAT_DESTROYMEMBERCOND:
                CInline_ObjectAddrRef(actions->data.destroy_member_cond.dtor);
                break;
            case EAT_DESTROYMEMBERARRAY:
                CInline_ObjectAddrRef(actions->data.destroy_member_array.dtor);
                break;
            case EAT_DELETEPOINTER:
            case EAT_DELETELOCALPOINTER:
                CInline_ObjectAddrRef(actions->data.delete_pointer.deletefunc);
                break;
            case EAT_DELETEPOINTERCOND:
                CInline_ObjectAddrRef(actions->data.delete_pointer_cond.deletefunc);
                break;
            case EAT_CATCHBLOCK:
            case EAT_ACTIVECATCHBLOCK:
            case EAT_SPECIFICATION:
            case EAT_TERMINATE:
                break;
            default:
                CError_FATAL(192);
        }
        actions = actions->prev;
    }
}

void CExcept_CompareSpecifications(ExceptSpecList* a, ExceptSpecList* b) {
    ExceptSpecList* aa;
    ExceptSpecList* bb;

    aa = a;
    bb = b;
    while (1) {
        if (!aa) {
            if (!bb) {
                break;
            }
            CError_Error(ERR_EXCEPTION_SPECIFICATION_LIST_MISMATCH);
            return;
        }

        if (!bb) {
            CError_Error(ERR_EXCEPTION_SPECIFICATION_LIST_MISMATCH);
            return;
        }

        aa = aa->next;
        bb = bb->next;
    }

    if (a->type == NULL) {
        if (b->type != NULL) {
            CError_Error(ERR_EXCEPTION_SPECIFICATION_LIST_MISMATCH);
        }
        return;
    }

    if (b->type == NULL) {
        CError_Error(ERR_EXCEPTION_SPECIFICATION_LIST_MISMATCH);
        return;
    }

    for (aa = a; aa; aa = aa->next) {
        for (bb = b; bb; bb = bb->next) {
            if (is_typesame(aa->type, bb->type) && aa->qual == bb->qual) {
                break;
            }
        }

        if (bb == NULL) {
            CError_Error(ERR_EXCEPTION_SPECIFICATION_LIST_MISMATCH);
            return;
        }
    }
}

Boolean CExcept_ActionCompare(ExceptionAction* a, ExceptionAction* b) {
    if (a->type == b->type) {
        switch (a->type) {
            case EAT_DESTROYLOCAL:
                return a->data.destroy_local.local == b->data.destroy_local.local;
            case EAT_DESTROYLOCALCOND:
                return a->data.destroy_local_cond.local == b->data.destroy_local_cond.local;
            case EAT_DESTROYLOCALOFFSET:
                return a->data.destroy_local_offset.local == b->data.destroy_local_offset.local &&
                    a->data.destroy_local_offset.offset == b->data.destroy_local_offset.offset;
            case EAT_DESTROYLOCALPOINTER:
                return a->data.destroy_local_pointer.pointer ==
                    b->data.destroy_local_pointer.pointer;
            case EAT_DESTROYLOCALARRAY:
                return a->data.destroy_local_array.localarray ==
                    b->data.destroy_local_array.localarray;
            case EAT_DESTROYPARTIALARRAY:
                return a->data.destroy_partial_array.arraypointer ==
                    b->data.destroy_partial_array.arraypointer;
            case EAT_DESTROYMEMBER:
            case EAT_DESTROYBASE:
                return a->data.destroy_member.objectptr == b->data.destroy_member.objectptr &&
                    a->data.destroy_member.offset == b->data.destroy_member.offset;
            case EAT_DESTROYMEMBERCOND:
                return a->data.destroy_member_cond.objectptr ==
                    b->data.destroy_member_cond.objectptr &&
                    a->data.destroy_member_cond.offset == b->data.destroy_member_cond.offset;
            case EAT_DESTROYMEMBERARRAY:
                return a->data.destroy_member_array.objectptr ==
                    b->data.destroy_member_array.objectptr &&
                    a->data.destroy_member_array.offset == b->data.destroy_member_array.offset;
            case EAT_DELETEPOINTER:
            case EAT_DELETELOCALPOINTER:
                return a->data.delete_pointer.pointerobject ==
                    b->data.delete_pointer.pointerobject &&
                    a->data.delete_pointer.deletefunc == b->data.delete_pointer.deletefunc;
            case EAT_DELETEPOINTERCOND:
                return a->data.delete_pointer_cond.cond == b->data.delete_pointer_cond.cond;
            case EAT_CATCHBLOCK:
                return a->data.catch_block.catch_label == b->data.catch_block.catch_label;
            case EAT_ACTIVECATCHBLOCK:
                return a->data.active_catch_block.catch_info_object ==
                    b->data.active_catch_block.catch_info_object;
            case EAT_TERMINATE:
                return TRUE;
            case EAT_SPECIFICATION:
                return a->data.specification.unexp_id == b->data.specification.unexp_id;
            default:
                CError_FATAL(314);
        }
    }

    return FALSE;
}

int CExcept_IsSubList(ExceptionAction* a, ExceptionAction* b) {
    int diff;
    int i;
    int count1;
    int count2;
    ExceptionAction* scan;

    if (a == b) {
        return 0;
    }

    count1 = 0;
    scan = a;
    while (scan) {
        scan = scan->prev;
        count1++;
    }

    scan = b;
    count2 = 0;
    while (scan) {
        scan = scan->prev;
        count2++;
    }

    diff = count2 - count1;
    if (diff < 0) {
        return -1;
    }

    for (i = 0; i < diff; i++) {
        b = b->prev;
    }

    while (a != b) {
        if (!a || !b || !CExcept_ActionCompare(a, b)) {
            return -1;
        }
        a = a->prev;
        b = b->prev;
    }

    return diff;
}

Boolean CExcept_ActionNeedsDestruction(ExceptionAction* action) {
    switch (action->type) {
        case EAT_CATCHBLOCK:
            return FALSE;
        case EAT_DESTROYLOCAL:
        case EAT_DESTROYLOCALOFFSET:
        case EAT_DESTROYLOCALARRAY:
        case EAT_DELETELOCALPOINTER:
        case EAT_ACTIVECATCHBLOCK:
            return TRUE;
        case EAT_NOP:
        case EAT_DESTROYMEMBER:
        case EAT_DESTROYMEMBERCOND:
        case EAT_DESTROYMEMBERARRAY:
        case EAT_DESTROYBASE:
            break;
        default:
            CError_FATAL(363);
    }

    return FALSE;
}

// NOTE: invented names: local, offset, dtor, unused
ENode* CExcept_RegisterDestructorObject(
    Object* local, SInt32 offset, Object* dtor, Boolean unused
) {
    ExceptionAction* action;
    ENode* expr;

    action = lalloc(sizeof(ExceptionAction));
    memclrw(action, sizeof(ExceptionAction));

    action->prev = cexcept_dobjstack;
    cexcept_dobjstack = action;

    expr = create_objectrefnode(local);
    dtor = CABI_GetDestructorObject(dtor, CABIDestroyComplete);

    if (offset == 0) {
        action->type = EAT_DESTROYLOCAL;
        action->data.destroy_local.local = local;
        action->data.destroy_local.dtor = dtor;
    } else {
        action->type = EAT_DESTROYLOCALOFFSET;
        action->data.destroy_local_offset.local = local;
        action->data.destroy_local_offset.dtor = dtor;
        action->data.destroy_local_offset.offset = offset;
        expr = makediadicnode(expr, intconstnode(TYPE(&stunsignedlong), offset), EADD);
    }

    cexcept_hasdobjects = TRUE;
    return expr;
}

void CExcept_RegisterLocalArray(
    Statement* stmt, Object* localarray, Object* dtor, SInt32 elements, SInt32 element_size
) {
    ExceptionAction* action;

    action = lalloc(sizeof(ExceptionAction));
    memclrw(action, sizeof(ExceptionAction));

    action->prev = cexcept_dobjstack;
    cexcept_dobjstack = action;

    dtor = CABI_GetDestructorObject(dtor, CABIDestroyComplete);

    action->type = EAT_DESTROYLOCALARRAY;
    action->data.destroy_local_array.localarray = localarray;
    action->data.destroy_local_array.dtor = dtor;
    action->data.destroy_local_array.elements = elements;
    action->data.destroy_local_array.element_size = element_size;

    cexcept_hasdobjects = TRUE;
}

void CExcept_RegisterDeleteObject(Statement* stmt, Object* pointerobject, Object* deletefunc) {
    ExceptionAction* action;

    action = lalloc(sizeof(ExceptionAction));
    memclrw(action, sizeof(ExceptionAction));

    action->prev = cexcept_dobjstack;
    cexcept_dobjstack = action;

    action->type = EAT_DELETELOCALPOINTER;
    action->data.delete_pointer.pointerobject = pointerobject;
    action->data.delete_pointer.deletefunc = deletefunc;

    cexcept_hasdobjects = TRUE;
}

static void CExcept_PatchConstructorAction(Statement* stmt, ExceptionAction* actionArg) {
    ExceptionAction* scan;
    ExceptionAction* action;

    for (action = stmt->dobjstack; action; action = action->prev) {
        switch (action->type) {
            case EAT_DESTROYMEMBER:
            case EAT_DESTROYMEMBERCOND:
            case EAT_DESTROYMEMBERARRAY:
            case EAT_SPECIFICATION:
            case EAT_DESTROYBASE:
                goto exitFirstLoop;
            case EAT_CATCHBLOCK:
                scan = action;
                while (1) {
                    if (!scan->prev) {
                        goto exitFirstLoop;
                    }
                    scan = scan->prev;
                    if (scan->type != EAT_CATCHBLOCK) {
                        CError_FATAL(481);
                    }
                }
            default:
                break;
        }
    }
exitFirstLoop:
    if (action == NULL) {
        while (stmt) {
            if ((scan = stmt->dobjstack)) {
                while (1) {
                    if (scan == actionArg) {
                        break;
                    }
                    if (scan->prev == NULL) {
                        scan->prev = actionArg;
                        break;
                    }
                    scan = scan->prev;
                }
            } else {
                stmt->dobjstack = actionArg;
            }
            stmt = stmt->next;
        }
    } else {
        actionArg->prev = action;
        while (stmt) {
            if (stmt->dobjstack != action) {
                scan = stmt->dobjstack;
                do {
                    if (scan == actionArg) {
                        goto nextStmt;
                    }
                    if (scan->prev == action) {
                        scan->prev = actionArg;
                        goto nextStmt;
                    }
                } while ((scan = scan->prev));

                while (1) {
                    if (action->type == EAT_CATCHBLOCK && action->prev == NULL) {
                        return;
                    }

                    action = action->prev;
                    if (!action) {
                        CError_FATAL(531);
                    }
                }
            } else {
                stmt->dobjstack = actionArg;
            }
nextStmt:
            stmt = stmt->next;
        }
    }
}

void CExcept_Terminate(void) {
    ExceptionAction* action;

    action = lalloc(sizeof(ExceptionAction));
    memclrw(action, sizeof(ExceptionAction));

    action->type = EAT_TERMINATE;

    action->prev = cexcept_dobjstack;
    cexcept_dobjstack = action;
}

void CExcept_Magic(void) {
    cexcept_magic = TRUE;
}

static Object* CExcept_FindLocalObject(char* name) {
    NameResult result;
    NameSpaceObjectList* list;

    list = CScope_FindObjectList(&result, GetHashNameNodeExport(name));
    if (list && list->object->otype == OT_OBJECT && OBJECT(list->object)->datatype == DLOCAL) {
        return OBJECT(list->object);
    }

    CError_FATAL(580);
    return NULL;
}

void CExcept_ArrayInit(void) {
    ExceptionAction* action;

    action = lalloc(sizeof(ExceptionAction));
    memclrw(action, sizeof(ExceptionAction));

    action->type = EAT_DESTROYPARTIALARRAY;
    action->data.destroy_partial_array.arraypointer = CExcept_FindLocalObject("ptr");
    action->data.destroy_partial_array.arraycounter = CExcept_FindLocalObject("i");
    action->data.destroy_partial_array.dtor = CExcept_FindLocalObject("dtor");
    action->data.destroy_partial_array.element_size = CExcept_FindLocalObject("size");

    action->prev = cexcept_dobjstack;
    cexcept_dobjstack = action;
}

void CExcept_RegisterMember(
    Statement* stmt, Object* objectptr, SInt32 offset, Object* dtor, Object* cond, Boolean isMember
) {
    ExceptionAction* action;

    action = lalloc(sizeof(ExceptionAction));
    memclrw(action, sizeof(ExceptionAction));

    if (cond == NULL) {
        if (isMember) {
            action->type = EAT_DESTROYMEMBER;
            action->data.destroy_member.dtor = CABI_GetDestructorObject(dtor, CABIDestroyComplete);
        } else {
            action->type = EAT_DESTROYBASE;
            action->data.destroy_member.dtor = CABI_GetDestructorObject(dtor, CABIDestroyBase);
        }
        action->data.destroy_member.objectptr = objectptr;
        action->data.destroy_member.offset = offset;
    } else {
        CError_ASSERT(632, cond->type == TYPE(&stsignedshort));
        action->type = EAT_DESTROYMEMBERCOND;
        action->data.destroy_member_cond.objectptr = objectptr;
        action->data.destroy_member_cond.cond = cond;
        action->data.destroy_member_cond.dtor = CABI_GetDestructorObject(dtor, CABIDestroyComplete);
        action->data.destroy_member_cond.offset = offset;
    }

    CExcept_PatchConstructorAction(stmt, action);
    stmt->flags |= StmtFlag_EndsConstruction;
}

void CExcept_RegisterMemberArray(
    Statement* stmt,
    Object* objectptr,
    SInt32 offset,
    Object* dtor,
    SInt32 elements,
    SInt32 element_size
) {
    ExceptionAction* action;

    action = lalloc(sizeof(ExceptionAction));
    memclrw(action, sizeof(ExceptionAction));

    action->type = EAT_DESTROYMEMBERARRAY;
    action->data.destroy_member_array.objectptr = objectptr;
    action->data.destroy_member_array.dtor = CABI_GetDestructorObject(dtor, CABIDestroyComplete);
    action->data.destroy_member_array.offset = offset;
    action->data.destroy_member_array.elements = elements;
    action->data.destroy_member_array.element_size = element_size;

    CExcept_PatchConstructorAction(stmt, action);
    stmt->flags |= StmtFlag_EndsConstruction;
}

static Statement* CExcept_DestroyLocal(
    ExceptionAction* ea, Statement* stmt, Object* object, Object* dtor, SInt32 offset
) {
    ENode* expr;

    expr = create_objectrefnode(object);
    if (offset) {
        expr = makediadicnode(expr, intconstnode(TYPE(&stunsignedlong), offset), EADD);
    }

    expr = CABI_DestroyObject(dtor, expr, CABIDestroyComplete, TRUE, FALSE);

    CError_ASSERT(687, expr->type == EFUNCCALL && expr->data.funccall.funcref->type == EOBJREF);
    if (expr->data.funccall.funcref->data.objref->datatype == DVFUNC) {
        expr->data.funccall.funcref->flags |= ENODE_FLAG_NONVIRTUAL;
    }

    stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
    stmt->expr = expr;
    stmt->dobjstack = ea->prev;
    return stmt;
}

static Statement* CExcept_DestroyLocalPointer(
    ExceptionAction* ea, Statement* stmt, Object* object, Object* deletefunc
) {
    Statement* newStmt;

    newStmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
    newStmt->expr = funccallexpr(deletefunc, create_objectnode2(object), NULL, NULL, NULL);
    newStmt->dobjstack = ea->prev;
    return newStmt;
}

static Statement* CExcept_DestroyLocalArray(
    ExceptionAction* ea,
    Statement* stmt,
    Object* object,
    Object* dtor,
    SInt32 elements,
    SInt32 element_size
) {
    Statement* newStmt;
    ENode* dtorExpr;

    newStmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);

    dtorExpr = dtor ? create_objectrefnode(CABI_GetDestructorObject(dtor, CABIDestroyComplete))
                    : nullnode();

    newStmt->expr = funccallexpr(
        darr_func,
        create_objectrefnode(object),
        dtorExpr,
        intconstnode(TYPE(&stunsignedlong), element_size),
        intconstnode(TYPE(&stunsignedlong), elements)
    );

    newStmt->dobjstack = ea->prev;
    return newStmt;
}

static Statement* CExcept_EndCatch(ExceptionAction* ea, Statement* stmt) {
    stmt = CFunc_InsertStatement(ST_ENDCATCHDTOR, stmt);
    stmt->expr = create_objectrefnode(ea->data.active_catch_block.catch_info_object);
    stmt->dobjstack = ea->prev;
    if (!ea->data.active_catch_block.call_dtor) {
        stmt->type = ST_ENDCATCH;
    }
    return stmt;
}

Statement* CExcept_ActionCleanup(ExceptionAction* ea, Statement* stmt) {
    switch (ea->type) {
        case EAT_DESTROYLOCALCOND:
        case EAT_DESTROYLOCALPOINTER:
        case EAT_DESTROYPARTIALARRAY:
        case EAT_DESTROYMEMBER:
        case EAT_DESTROYMEMBERCOND:
        case EAT_DESTROYMEMBERARRAY:
        case EAT_DELETEPOINTER:
        case EAT_DELETEPOINTERCOND:
        case EAT_CATCHBLOCK:
        case EAT_SPECIFICATION:
        case EAT_TERMINATE:
        case EAT_DESTROYBASE:
            break;
        case EAT_DESTROYLOCAL:
            stmt = CExcept_DestroyLocal(
                ea, stmt, ea->data.destroy_local.local, ea->data.destroy_local.dtor, 0
            );
            break;
        case EAT_DESTROYLOCALOFFSET:
            stmt = CExcept_DestroyLocal(
                ea,
                stmt,
                ea->data.destroy_local.local,
                ea->data.destroy_local.dtor,
                ea->data.destroy_local_offset.offset
            );
            break;
        case EAT_DELETELOCALPOINTER:
            stmt = CExcept_DestroyLocalPointer(
                ea, stmt, ea->data.delete_pointer.pointerobject, ea->data.delete_pointer.deletefunc
            );
            break;
        case EAT_DESTROYLOCALARRAY:
            stmt = CExcept_DestroyLocalArray(
                ea,
                stmt,
                ea->data.destroy_local_array.localarray,
                ea->data.destroy_local_array.dtor,
                ea->data.destroy_local_array.elements,
                ea->data.destroy_local_array.element_size
            );
            break;
        case EAT_ACTIVECATCHBLOCK:
            stmt = CExcept_EndCatch(ea, stmt);
            break;
        default:
            CError_FATAL(827);
    }

    return stmt;
}

static void CExcept_MangleNameSpaceName(NameSpace* nspace) {
    while (nspace) {
        if (nspace->name) {
            CExcept_MangleNameSpaceName(nspace->parent);
            AppendGListName(&name_mangle_list, nspace->name->name);
            AppendGListName(&name_mangle_list, "::");
            return;
        }
        nspace = nspace->parent;
    }
}

static void CExcept_MangleClassName(TypeClass* tclass) {
    NameSpace* nspace;
    char buf[64];

    CExcept_MangleNameSpaceName(tclass->nspace->parent);
    AppendGListName(&name_mangle_list, tclass->classname->name);

    for (nspace = tclass->nspace->parent; nspace; nspace = nspace->parent) {
        if (!nspace->is_global && !nspace->is_templ && !nspace->name) {
            CError_ASSERT(868, cscope_currentfunc != NULL);

            // the original's: the variables' addresses, not the function's and the namespace's
            // (a global's address in the original executable, and a local's: the running
            // compiler's stack address, which differs with the host and between builds of it)
            sprintf(buf, "*%lx*%lx*", ORIGINAL_ADDRESS(&cscope_currentfunc), (UInt32)&nspace);
            AppendGListName(&name_mangle_list, buf);
            break;
        }
    }
}

typedef struct BCL {
    struct BCL* next;
    TypeClass* tclass;
    SInt32 offset;
    Boolean is_virtual;
    Boolean is_public;
    Boolean is_ambig;
} BCL;

static void CExcept_MakeBaseClassListAmbig(BCL* bcl, TypeClass* tclass) {
    BCL* scan;
    ClassList* list;

    for (scan = bcl; scan; scan = scan->next) {
        if (scan->tclass == tclass) {
            scan->is_ambig = TRUE;
        }
    }

    for (list = tclass->bases; list; list = list->next) {
        CExcept_MakeBaseClassListAmbig(bcl, list->base);
    }
}

static BCL* CExcept_GetBaseClassList(
    BCL* bcl,
    TypeClass* tclass1,
    TypeClass* tclass2,
    SInt32 offset,
    Boolean is_virtual,
    Boolean is_public
) {
    BCL* node;
    ClassList* base;
    Boolean new_is_public;

    for (node = bcl; node; node = node->next) {
        if (node->tclass == tclass2) {
            if (is_virtual && node->is_virtual) {
                if (is_public) {
                    node->is_public = TRUE;
                }
            } else {
                CExcept_MakeBaseClassListAmbig(bcl, tclass2);
            }
            return bcl;
        }
    }

    node = lalloc(sizeof(BCL));
    node->tclass = tclass2;
    node->offset = offset;
    node->is_virtual = is_virtual;
    node->is_public = is_public;
    node->is_ambig = FALSE;
    node->next = bcl;
    bcl = node;

    for (base = tclass2->bases; base; base = base->next) {
        new_is_public = is_public && (base->access == ACCESSPUBLIC);
        if (base->is_virtual) {
            bcl = CExcept_GetBaseClassList(
                bcl,
                tclass1,
                base->base,
                CClass_VirtualBaseOffset(tclass1, base->base),
                TRUE,
                new_is_public
            );
        } else {
            bcl = CExcept_GetBaseClassList(
                bcl, tclass1, base->base, offset + base->offset, FALSE, new_is_public
            );
        }
    }

    return bcl;
}

static void CExcept_MangleClass(TypeClass* tclass) {
    BCL* bcl;
    char buf[16];

    for (bcl = CExcept_GetBaseClassList(NULL, tclass, tclass, 0, FALSE, TRUE); bcl;
         bcl = bcl->next) {
        if (bcl->is_public && !bcl->is_ambig) {
            CExcept_MangleClassName(bcl->tclass);
            AppendGListByte(&name_mangle_list, '!');

            if (bcl->offset) {
                sprintf(buf, "%ld!", bcl->offset);
                AppendGListName(&name_mangle_list, buf);
            } else {
                AppendGListByte(&name_mangle_list, '!');
            }
        }
    }
}

// NOTE: invented names: type, qual, isThrow
static ENode* CExcept_GetTypeID(Type* type, UInt32 qual, Boolean isThrow) {
    ENode* expr;
    TypePointer my_tptr;

    if (IS_TYPE_REFERENCE(type)) {
        type = TPTR_TARGET(type);
    }

    if (IS_TYPE_CLASS(type) || (IS_TYPE_POINTER_ONLY(type) && IS_TYPE_CLASS(TPTR_TARGET(type)))) {
        name_mangle_list.size = 0;
        if (IS_TYPE_POINTER_ONLY(type)) {
            AppendGListByte(&name_mangle_list, '*');
            type = TPTR_TARGET(type);
        } else {
            AppendGListByte(&name_mangle_list, '!');
        }

        if (isThrow) {
            CExcept_MangleClass(TYPE_CLASS(type));
        } else {
            CExcept_MangleClassName(TYPE_CLASS(type));
            AppendGListByte(&name_mangle_list, '!');
        }
    } else {
        if (IS_TYPE_POINTER_ONLY(type)) {
            if (TPTR_QUAL(type) & (Q_CONST | Q_VOLATILE)) {
                my_tptr = *TYPE_POINTER(type);
                my_tptr.qual = 0;
                type = TYPE(&my_tptr);
            }
        } else {
            qual = 0;
        }
        CMangler_MangleType(type, qual);
    }

    AppendGListByte(&name_mangle_list, 0);

    expr = CExpr_NewENode(ESTRINGCONST);
    expr->rtype = CDecl_NewPointerType(TYPE(&stchar));
    expr->data.string.size = name_mangle_list.size;
    expr->data.string.data = galloc(name_mangle_list.size);
    memcpy(expr->data.string.data, *name_mangle_list.data, name_mangle_list.size);
    return expr;
}

static Object* CExcept_TypeID(Type* type, UInt32 qual) {
    ENode* expr = CExcept_GetTypeID(type, qual, FALSE);
    CError_ASSERT(1086, expr->type == ESTRINGCONST);
    return CInit_DeclareString(expr->data.string.data, expr->data.string.size, FALSE, FALSE);
}

void CExcept_ScanExceptionSpecification(TypeFunc* tfunc) {
    ExceptSpecList* exspecs;
    ExceptSpecList* exspec;
    DeclInfo di;

    exspecs = NULL;

    if (lex() != '(') {
        CError_Error(ERR_LPAREN_EXPECTED);
        return;
    }

    if ((tk = lex()) != ')') {
        while (1) {
            memclrw(&di, sizeof(di));
            CParser_GetDeclSpecs(&di, FALSE);
            if (di.storageclass) {
                CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
            }

            CError_QualifierCheck(di.qual & ~(Q_CONST | Q_VOLATILE | Q_PASCAL | Q_ALIGNED_MASK));

            scandeclarator(&di);
            if (di.name) {
                CError_Error(ERR_ILLEGAL_TYPE);
            }

            if (IS_TYPE_POINTER_ONLY(di.thetype)) {
                if (TPTR_QUAL(di.thetype) & (Q_CONST | Q_VOLATILE)) {
                    TypePointer* newtype = galloc(sizeof(TypePointer));
                    *newtype = *TYPE_POINTER(di.thetype);
                    newtype->qual = 0;
                    di.thetype = TYPE(newtype);
                }
            } else {
                di.qual = 0;
            }

            for (exspec = exspecs; exspec; exspec = exspec->next) {
                if (is_typesame(exspec->type, di.thetype) && exspec->qual == di.qual) {
                    break;
                }
            }

            if (!exspec) {
                exspec = galloc(sizeof(ExceptSpecList));
                memclrw(exspec, sizeof(ExceptSpecList));
                exspec->next = exspecs;
                exspec->type = di.thetype;
                exspec->qual = di.qual;
                exspecs = exspec;
            }

            if (tk == ')') {
                break;
            }

            if (tk != ',') {
                CError_Error(ERR_RPAREN_EXPECTED);
                break;
            }

            tk = lex();
        }
    }

    if (!exspecs) {
        exspecs = galloc(sizeof(ExceptSpecList));
        memclrw(exspecs, sizeof(ExceptSpecList));
    }
    tfunc->exspecs = exspecs;
    tk = lex();
}

static ENode* CExcept_CallCopyCtor(Object* object, TypeClass* tclass, ENode* expr1, ENode* expr2) {
    ENodeList* list;
    ENode* expr;
    FuncArg* arg;

    if (!IS_TYPE_FUNC(object->type) ||
        !(arg = TYPE_FUNC(object->type)->args) ||
        !(arg = arg->next)) {
        CError_FATAL(1169);
    }

    expr = funccallexpr(object, expr1, NULL, NULL, NULL);
    list = expr->data.funccall.args;

    if (tclass->flags & CLASS_HAS_VBASES) {
        CError_ASSERT(1179, arg = arg->next);
        list->next = lalloc(sizeof(ENodeList));
        list = list->next;
        list->next = NULL;
        list->node = intconstnode(TYPE(&stsignedshort), 1);
    }

    list->next = lalloc(sizeof(ENodeList));
    list = list->next;
    list->next = NULL;
    list->node = expr2;

    while ((arg = arg->next)) {
        CError_ASSERT(1195, arg->dexpr);

        list->next = lalloc(sizeof(ENodeList));
        list = list->next;
        list->next = NULL;
        list->node = CExpr_GetDefaultArgument(expr->data.funccall.funcref, arg);
    }

    return expr;
}

ENode* CExcept_ScanThrowExpression(void) {
    ENode* expr;
    Object* func;
    ENode* resultExpr;
    Object* obj;
    ENode* thrownExpr;
    ENode* tempExpr;

    if (!copts.exceptions) {
        CError_Error(ERR_EXCEPTION_HANDLING_OPTION_DISABLED);
    }

    switch ((tk = lex())) {
        case ')':
        case ',':
        case ':':
        case ';':
            expr = funccallexpr(Xthrw_func, nullnode(), nullnode(), nullnode(), NULL);
            break;
        default:
            thrownExpr = pointer_generation(assignment_expression());
            obj = create_temp_object(thrownExpr->rtype);
            if (!IS_TYPE_CLASS(thrownExpr->rtype) ||
                !(resultExpr = CExpr_IsTempConstruction(thrownExpr, thrownExpr->rtype, &expr))) {
                tempExpr = create_objectrefnode(obj);
                if (IS_TYPE_CLASS(thrownExpr->rtype) &&
                    (func = CClass_CopyConstructor(TYPE_CLASS(thrownExpr->rtype)))) {
                    resultExpr = CExcept_CallCopyCtor(
                        func,
                        TYPE_CLASS(thrownExpr->rtype),
                        tempExpr,
                        getnodeaddress(thrownExpr, FALSE)

                    );
                } else {
                    if (thrownExpr->rtype->size == 0) {
                        CError_Error(ERR_ILLEGAL_TYPE);
                    }

                    tempExpr = makemonadicnode(tempExpr, EINDIRECT);
                    tempExpr->rtype = thrownExpr->rtype;
                    resultExpr = makediadicnode(tempExpr, thrownExpr, EASS);
                    resultExpr = makediadicnode(resultExpr, create_objectrefnode(obj), ECOMMA);
                    resultExpr->rtype = TYPE(&void_ptr);
                }
            } else {
                *expr = *create_objectrefnode(obj);
            }

            expr = CExcept_GetTypeID(thrownExpr->rtype, ENODE_QUALS(thrownExpr), TRUE);
            if (IS_TYPE_CLASS(thrownExpr->rtype) &&
                (func = CClass_Destructor(TYPE_CLASS(thrownExpr->rtype)))) {
                expr = funccallexpr(
                    Xthrw_func,
                    expr,
                    resultExpr,
                    create_objectrefnode(CABI_GetDestructorObject(func, CABIDestroyComplete)),
                    NULL
                );
            } else {
                expr = funccallexpr(Xthrw_func, expr, resultExpr, nullnode(), NULL);
            }
    }

    expr->flags |= ENODE_FLAG_VOLATILE;
    return expr;
}

static Boolean CExcept_MightNeedDtor(Type* type) {
    if (type) {
        if (IS_TYPE_CLASS(type)) {
            if (!CClass_Destructor(TYPE_CLASS(type))) {
                return FALSE;
            }
        } else if (IS_TYPE_POINTER_ONLY(type)) {
            if (!(TPTR_QUAL(type) & Q_REFERENCE) || !IS_TYPE_CLASS(TPTR_TARGET(type))) {
                return FALSE;
            }
        } else {
            return FALSE;
        }
    }

    return TRUE;
}

typedef struct CatchBlock {
    struct CatchBlock* next;
    Object* catch_object;
    Object* catch_info_object;
    Statement* stmt;
    // the handler's last statement (set, never read).
    Statement* lastStmt; // NOTE: invented name
    Type* type;
    UInt32 qual;
} CatchBlock;

static void CExcept_PatchDObjStack(
    Statement* beginCatchStmt, Statement* tryEndStmt, Statement* endStmt, CatchBlock* catchBlock
) {
    ExceptionAction* ea;
    ExceptionAction* stackHead;
    ExceptionAction* stackTail;
    ExceptionAction* firstEA;
    Statement* stmt;
    Object* catch_info_object;
    Boolean call_dtor;

    catch_info_object = catchBlock->catch_info_object;
    call_dtor = FALSE;
    stackHead = stackTail = beginCatchStmt->dobjstack;
    firstEA = NULL;

    while (catchBlock) {
        ea = lalloc(sizeof(ExceptionAction));
        memclrw(ea, sizeof(ExceptionAction));

        ea->prev = stackTail;
        stackTail = ea;

        if (!firstEA) {
            firstEA = ea;
        }

        ea->type = EAT_CATCHBLOCK;
        ea->data.catch_block.catch_object = catchBlock->catch_object;
        ea->data.catch_block.catch_label = catchBlock->stmt->label;
        if (catchBlock->type) {
            ea->data.catch_block.catch_typeid = CExcept_TypeID(catchBlock->type, catchBlock->qual);
        }
        ea->data.catch_block.catch_info_object = catch_info_object;

        if (!call_dtor && CExcept_MightNeedDtor(catchBlock->type)) {
            call_dtor = TRUE;
        }

        ea->data.catch_block.catch_type = catchBlock->type;
        ea->data.catch_block.catch_qual = catchBlock->qual;
        catchBlock = catchBlock->next;
    }

    stmt = beginCatchStmt;
    while (1) {
        if ((ea = stmt->dobjstack) != stackHead) {
            while (1) {
                CError_ASSERT(1404, ea);
                if (ea->prev == stackTail) {
                    break;
                }
                if (ea->prev == stackHead) {
                    ea->prev = stackTail;
                    break;
                }
                ea = ea->prev;
            }
        } else {
            stmt->dobjstack = stackTail;
        }

        if (stmt == endStmt) {
            break;
        }

        if (stmt == tryEndStmt) {
            ea = lalloc(sizeof(ExceptionAction));
            memclrw(ea, sizeof(ExceptionAction));
            ea->prev = stackHead;
            ea->type = EAT_ACTIVECATCHBLOCK;
            ea->data.active_catch_block.catch_info_object = catch_info_object;
            ea->data.active_catch_block.call_dtor = call_dtor;
            stackTail = ea;
        }

        stmt = stmt->next;
        CError_ASSERT(1426, stmt);
    }

    cexcept_hasdobjects = TRUE;
}

static void CExcept_CheckTryObjects(ENode* expr) {
    if (expr->data.objref->datatype == DLOCAL && expr->data.objref->u.var.info) {
        expr->data.objref->u.var.info->noregister = TRUE;
    }
}

static ENode* CExcept_CatchExpressionInit(DeclInfo* di, CatchBlock* catchBlock) {
    Object* catch_object;
    Object* copyCtor;
    Object* dtor;
    ENode* expr;

    if (CScope_FindName(cscope_current, di->name)) {
        CError_Error(ERR_IDENTIFIER_REDECLARED, di->name->name);
    }

    catch_object = CParser_NewLocalDataObject(di, TRUE);
    CFunc_SetupLocalVarInfo(catch_object);
    CScope_AddObject(cscope_current, di->name, OBJ_BASE(catch_object));
    catchBlock->catch_object = catch_object;

    expr = makediadicnode(
        create_objectrefnode(catchBlock->catch_info_object),
        intconstnode(TYPE(&stunsignedlong), 12),
        EADD
    );
    expr->rtype = CDecl_NewPointerType(CDecl_NewPointerType(di->thetype));

    expr = makemonadicnode(expr, EINDIRECT);
    expr->rtype = CDecl_NewPointerType(di->thetype);

    if (IS_TYPE_REFERENCE(di->thetype)) {
        return makediadicnode(create_objectnode2(catch_object), expr, EASS);
    }

    if (IS_TYPE_CLASS(di->thetype) &&
        (copyCtor = CClass_CopyConstructor(TYPE_CLASS(di->thetype)))) {
        dtor = CClass_Destructor(TYPE_CLASS(di->thetype));
        return CExcept_CallCopyCtor(
            copyCtor,
            TYPE_CLASS(di->thetype),

            (dtor == NULL) ? create_objectrefnode(catch_object)
                           : CExcept_RegisterDestructorObject(catch_object, 0, dtor, FALSE),
            expr
        );
    }

    expr = makemonadicnode(expr, EINDIRECT);
    expr->rtype = di->thetype;
    return makediadicnode(create_objectnode(catch_object), expr, EASS);
}

// NOTE: invented names: dt, isCtorOrDtor
void CExcept_ScanTryBlock(DeclThing* dt, Boolean isCtorOrDtor) {
    Object* catch_info_object;
    Statement* beginCatchStmt;
    Statement* tryEndStmt;
    Statement* endStmt;
    Statement* stmt;
    CLabel* catchEndLabel;
    CLabel* endLabel;
    CatchBlock* catchStack;
    CatchBlock* catchBlock;
    DeclBlock* declBlock;
    ENode* expr;
    DeclInfo di;

    if (!copts.exceptions) {
        CError_Error(ERR_EXCEPTION_HANDLING_OPTION_DISABLED);
    }

    catch_info_object = create_temp_object(TYPE(&catchinfostruct));
    if (cexcept_magic) {
        catch_info_object->name = GetHashNameNodeExport("__exception_magic");
        CScope_AddObject(cscope_current, catch_info_object->name, OBJ_BASE(catch_info_object));
    }

    stmt = CFunc_AppendStatement(ST_LABEL);
    stmt->flags = StmtFlag_KeepLabel;
    // catchEndLabel is reused below for the label after the handlers
    catchEndLabel = newlabel();
    stmt->label = catchEndLabel;
    stmt->label->stmt = stmt;

    stmt = CFunc_AppendStatement(ST_BEGINCATCH);
    stmt->expr = create_objectrefnode(catch_info_object);
    beginCatchStmt = stmt;

    if (tk != '{') {
        CError_Error(ERR_LBRACE_EXPECTED);
        return;
    }
    CFunc_CompoundStatement(dt);
    if (tk != TK_CATCH) {
        CError_Error(ERR_CATCH_EXPECTED);
        return;
    }

    stmt = CFunc_AppendStatement(ST_GOTO);
    catchEndLabel = stmt->label = newlabel();
    tryEndStmt = stmt;

    endLabel = newlabel();
    catchStack = NULL;

    while (1) {
        CPrep_GetFileOffsetInfo2(&cparser_fileoffset, &sourceoffset, &sourcefilepath);

        stmt = CFunc_AppendStatement(ST_LABEL);
        stmt->flags = StmtFlag_KeepLabel;
        stmt->label = newlabel();
        stmt->label->stmt = stmt;
        declBlock = NULL;

        catchBlock = lalloc(sizeof(CatchBlock));
        memclrw(catchBlock, sizeof(CatchBlock));
        catchBlock->next = catchStack;
        catchStack = catchBlock;
        catchBlock->stmt = stmt;
        catchBlock->catch_info_object = catch_info_object;

        if ((tk = lex()) != '(') {
            CError_Error(ERR_LPAREN_EXPECTED);
            break;
        }
        if ((tk = lex()) == TK_ELLIPSIS) {
            tk = lex();
        } else {
            memclrw(&di, sizeof(di));
            CParser_GetDeclSpecs(&di, FALSE);
            if (di.missingTypeSpecifier) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            if (di.storageclass) {
                CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
            }
            CError_QualifierCheck(di.qual & ~(Q_CONST | Q_VOLATILE | Q_PASCAL | Q_ALIGNED_MASK));
            scandeclarator(&di);
            if (IS_TYPE_FUNC(di.thetype)) {
                di.thetype = CDecl_NewPointerType(di.thetype);
            } else if (IS_TYPE_ARRAY(di.thetype)) {
                di.thetype = CDecl_NewPointerType(TPTR_TARGET(di.thetype));
            }
            IsCompleteType(di.thetype);
            if (IS_TYPE_CLASS(di.thetype) && (TYPE_CLASS(di.thetype)->flags & CLASS_ABSTRACT)) {
                CError_AbstractClassError(TYPE_CLASS(di.thetype));
            }
            catchBlock->type = di.thetype;
            catchBlock->qual = di.qual;
            if (di.name) {
                declBlock = CFunc_NewDeclBlock();
                expr = CExcept_CatchExpressionInit(&di, catchBlock);
                stmt = CFunc_AppendStatement(ST_EXPRESSION);
                stmt->expr = expr;
            }
        }

        if (tk != ')') {
            CError_Error(ERR_RPAREN_EXPECTED);
            break;
        }
        if ((tk = lex()) != '{') {
            CError_Error(ERR_LBRACE_EXPECTED);
            break;
        }
        CFunc_CompoundStatement(dt);
        if (isCtorOrDtor) {
            stmt = CFunc_AppendStatement(ST_EXPRESSION);
            stmt->expr = funccallexpr(Xthrw_func, nullnode(), nullnode(), nullnode(), NULL);
        }
        catchBlock->lastStmt = stmt;
        if (declBlock) {
            CFunc_RestoreBlock(declBlock);
        }
        if (tk != TK_CATCH) {
            break;
        }
        CFunc_AppendStatement(ST_GOTO)->label = endLabel;
    }

    endStmt = CFunc_AppendStatement(ST_LABEL);
    endStmt->label = endLabel;
    endLabel->stmt = endStmt;

    CExcept_PatchDObjStack(beginCatchStmt, tryEndStmt, endStmt, catchBlock);
    stmt = CFunc_AppendStatement(ST_LABEL);
    stmt->label = catchEndLabel;
    catchEndLabel->stmt = stmt;
}

static Statement* CExcept_InsertPrevStatement(StatementType sttype) {
    Statement* stmt = CFunc_InsertStatement(sttype, cexcept_prevstmt);
    stmt->sourceoffset = stmt->next->sourceoffset;
    stmt->sourcefilepath = stmt->next->sourcefilepath;
    stmt->dobjstack = cexcept_eabefore;
    return stmt;
}

static Object* CExcept_GetETEMPObject(ENode* expr) {
    UniqueObj* uobj;
    SInt32 id;

    if ((id = expr->data.temp.uniqueid)) {
        for (uobj = cexcept_uniqueobjs; uobj; uobj = uobj->next) {
            if (uobj->uniqueid == id) {
                return uobj->object;
            }
        }

        uobj = galloc(sizeof(UniqueObj));
        uobj->next = cexcept_uniqueobjs;
        cexcept_uniqueobjs = uobj;
        uobj->uniqueid = id;
        return (uobj->object = create_temp_object(expr->data.temp.type));
    } else {
        return create_temp_object(expr->data.temp.type);
    }
}

static ENode* CExcept_TempTrans_ETEMP(ENode* expr) {
    Object* object;
    DtorTemp* dtorTemp;
    ExceptionAction* ea;

    object = CExcept_GetETEMPObject(expr);
    if (expr->data.temp.needs_dtor) {
        dtorTemp = lalloc(sizeof(DtorTemp));
        dtorTemp->next = cexcept_dtortemps;
        cexcept_dtortemps = dtorTemp;
        dtorTemp->object = object;
        dtorTemp->temp = NULL;

        if (!IS_TYPE_CLASS(expr->data.temp.type) ||
            !(dtorTemp->dtor = CClass_Destructor(TYPE_CLASS(expr->data.temp.type)))) {
            CError_FATAL(1749);
        }

        ea = lalloc(sizeof(ExceptionAction));
        ea->prev = cexcept_eabefore;
        cexcept_eabefore = ea;
        ea->type = EAT_DESTROYLOCAL;
        ea->data.destroy_local.local = dtorTemp->object;
        ea->data.destroy_local.dtor = CABI_GetDestructorObject(dtorTemp->dtor, CABIDestroyComplete);

        ea = lalloc(sizeof(ExceptionAction));
        *ea = *cexcept_eabefore;
        ea->prev = cexcept_eaafter;
        cexcept_eaafter = ea;
    }

    expr->type = EOBJREF;
    expr->data.objref = object;
    return expr;
}

static ENode* CExcept_TransNewException(ENode* expr, Boolean isCond) {
    Object* tempObj;
    CLabel* label;
    Boolean isArray;
    Statement* stmt;
    ExceptionAction* ea;
    ENode* result;

    isArray = expr->type == ENEWEXCEPTIONARRAY;

    if (isCond) {
        expr->data.newexception.initexpr =
            CExcept_TempTransExprCond(expr->data.newexception.initexpr);
        expr->data.newexception.tryexpr =
            CExcept_TempTransExprCond(expr->data.newexception.tryexpr);
        tempObj = create_temp_object(TYPE(&stchar));

        stmt = CExcept_InsertPrevStatement(ST_EXPRESSION);
        stmt->expr =
            makediadicnode(create_objectnode(tempObj), intconstnode(TYPE(&stchar), 0), EASS);
        cexcept_prevstmt = stmt;

        ea = lalloc(sizeof(ExceptionAction));
        ea->prev = cexcept_eabefore;
        cexcept_eabefore = ea;

        ea->type = EAT_DELETEPOINTERCOND;
        ea->data.delete_pointer_cond.pointerobject = expr->data.newexception.pointertemp;
        ea->data.delete_pointer_cond.deletefunc = expr->data.newexception.deletefunc;
        ea->data.delete_pointer_cond.cond = tempObj;

        if (isArray) {
            result =
                makediadicnode(create_objectnode(tempObj), intconstnode(TYPE(&stchar), 1), EASS);
            result = makediadicnode(expr->data.newexception.initexpr, result, ECOMMA);
            result = makediadicnode(result, expr->data.newexception.tryexpr, ECOMMA);
            result = makediadicnode(
                result,
                makediadicnode(create_objectnode(tempObj), intconstnode(TYPE(&stchar), 0), EASS),
                ECOMMA
            );
            result = makediadicnode(
                result, create_objectnode(expr->data.newexception.pointertemp), ECOMMA
            );
        } else {
            result =
                makediadicnode(create_objectnode(tempObj), intconstnode(TYPE(&stchar), 1), EASS);
            result = makediadicnode(result, expr->data.newexception.tryexpr, ECOMMA);
            result = makediadicnode(
                result,
                makediadicnode(create_objectnode(tempObj), intconstnode(TYPE(&stchar), 0), EASS),
                ECOMMA
            );
            result = makediadicnode(expr->data.newexception.initexpr, result, ELAND);
            result = makediadicnode(
                result, create_objectnode(expr->data.newexception.pointertemp), ECOMMA
            );
        }
    } else {
        expr->data.newexception.initexpr = CExcept_TempTransExpr(expr->data.newexception.initexpr);
        expr->data.newexception.tryexpr = CExcept_TempTransExpr(expr->data.newexception.tryexpr);

        if (isArray) {
            stmt = CExcept_InsertPrevStatement(ST_EXPRESSION);
            stmt->expr = expr->data.newexception.initexpr;
        } else {
            stmt = CExcept_InsertPrevStatement(ST_IFNGOTO);
            stmt->expr = expr->data.newexception.initexpr;
            label = newlabel();
            stmt->label = label;
        }

        stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
        stmt->expr = expr->data.newexception.tryexpr;

        ea = lalloc(sizeof(ExceptionAction));
        ea->prev = cexcept_eabefore;
        ea->type = EAT_DELETEPOINTER;
        ea->data.delete_pointer.pointerobject = expr->data.newexception.pointertemp;
        ea->data.delete_pointer.deletefunc = expr->data.newexception.deletefunc;
        stmt->dobjstack = ea;

        if (!isArray) {
            stmt = CFunc_InsertStatement(ST_LABEL, stmt);
            stmt->label = label;
            label->stmt = stmt;
            stmt->dobjstack = cexcept_eabefore;
        }

        cexcept_prevstmt = stmt;
        result = create_objectnode(expr->data.newexception.pointertemp);
    }

    result->rtype = expr->rtype;
    return result;
}

static ENode* CExcept_TransInitTryCatch(ENode* expr, Boolean isCond) {
    CLabel* label1;
    CLabel* label2;
    Object* catch_info_object;
    Statement* stmt;
    ExceptionAction* ea;
    CLabel* label3;

    cexcept_hastrycatch = TRUE;

    if (isCond) {
        CError_ASSERT(1877, !cexcept_expandtrycatch);

        cexcept_serialize = TRUE;
        expr->data.itc.initexpr = CExcept_TempTransExprCond(expr->data.itc.initexpr);
        expr->data.itc.tryexpr = CExcept_TempTransExprCond(expr->data.itc.tryexpr);
        expr->data.itc.catchexpr = CExcept_TempTransExprCond(expr->data.itc.catchexpr);
        expr->data.itc.result = CExcept_TempTransExprCond(expr->data.itc.result);
        return expr;
    }

    expr->data.itc.initexpr = CExcept_TempTransExpr(expr->data.itc.initexpr);
    expr->data.itc.tryexpr = CExcept_TempTransExpr(expr->data.itc.tryexpr);
    expr->data.itc.catchexpr = CExcept_TempTransExpr(expr->data.itc.catchexpr);
    expr->data.itc.result = CExcept_TempTransExpr(expr->data.itc.result);

    if (!cexcept_expandtrycatch) {
        return expr;
    }

    label1 = newlabel();
    label2 = newlabel();
    label3 = newlabel();

    catch_info_object = create_temp_object(TYPE(&catchinfostruct));

    stmt = CExcept_InsertPrevStatement(ST_IFNGOTO);
    stmt->expr = expr->data.itc.initexpr;
    stmt->label = label3;

    ea = lalloc(sizeof(ExceptionAction));
    memclrw(ea, sizeof(ExceptionAction));
    ea->type = EAT_CATCHBLOCK;
    ea->data.catch_block.catch_label = label2;
    ea->data.catch_block.catch_info_object = catch_info_object;
    ea->prev = stmt->dobjstack;

    stmt = CFunc_InsertStatement(ST_LABEL, stmt);
    stmt->flags = StmtFlag_KeepLabel;
    stmt->label = label1;
    label1->stmt = stmt;
    stmt->dobjstack = ea;

    stmt = CFunc_InsertStatement(ST_BEGINCATCH, stmt);
    stmt->expr = create_objectrefnode(catch_info_object);

    stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
    stmt->expr = expr->data.itc.tryexpr;

    stmt = CFunc_InsertStatement(ST_GOTO, stmt);
    stmt->label = label3;

    CError_ASSERT(1928, stmt->dobjstack == ea);

    stmt->dobjstack = ea->prev;

    ea = lalloc(sizeof(ExceptionAction));
    memclrw(ea, sizeof(ExceptionAction));
    ea->type = EAT_ACTIVECATCHBLOCK;
    ea->data.active_catch_block.catch_info_object = catch_info_object;
    ea->prev = stmt->dobjstack;

    stmt = CFunc_InsertStatement(ST_LABEL, stmt);
    stmt->flags = StmtFlag_KeepLabel;
    stmt->label = label2;
    label2->stmt = stmt;
    stmt->dobjstack = ea;

    stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
    stmt->expr = expr->data.itc.catchexpr;

    stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
    stmt->expr = funccallexpr(Xthrw_func, nullnode(), nullnode(), nullnode(), NULL);

    stmt = CFunc_InsertStatement(ST_LABEL, stmt);
    stmt->label = label3;
    label3->stmt = stmt;

    CError_ASSERT(1968, stmt->dobjstack == ea);

    stmt->dobjstack = ea->prev;

    cexcept_prevstmt = stmt;
    return expr->data.itc.result;
}

static ENode* CExcept_TempTransFuncCall(ENode* expr, Boolean isCond) {
    ENode* tempNode;
    ENodeList* tempArg;
    ENodeList** argArray;
    ENodeList* args;
    ExceptionAction* ea;
    Statement* stmt;
    DtorTemp* dtorTemp;

    tempArg = NULL;
    if (expr->data.funccall.args) {
        tempNode = expr->data.funccall.args->node;
        if (tempNode->type == ETEMP) {
            if (tempNode->data.temp.needs_dtor) {
                tempArg = expr->data.funccall.args;
            }
        } else if (expr->data.funccall.args->next) {
            tempNode = expr->data.funccall.args->next->node;
            if (tempNode->type == ETEMP) {
                if (tempNode->data.temp.needs_dtor) {
                    tempArg = expr->data.funccall.args->next;
                }
            }
        }
    }

    if (tempArg) {
        if (isCond) {
            ENodeList* arg = expr->data.funccall.args;
            SInt32 i = 0;
            while (arg) {
                i++;
                arg = arg->next;
            }

            argArray = lalloc(sizeof(ENodeList*) * i);
            for (args = expr->data.funccall.args, i = 0; args; args = args->next) {
                argArray[i++] = args;
            }

            while (i > 0) {
                i--;
                if (argArray[i] != tempArg) {
                    argArray[i]->node = CExcept_TempTransExprCond(argArray[i]->node);
                }
            }
        } else {
            for (args = expr->data.funccall.args; args; args = args->next) {
                if (args != tempArg) {
                    args->node = CExcept_TempTransExpr(args->node);
                }
            }
        }

        dtorTemp = lalloc(sizeof(DtorTemp));
        dtorTemp->next = cexcept_dtortemps;
        cexcept_dtortemps = dtorTemp;

        dtorTemp->object = CExcept_GetETEMPObject(tempNode);
        dtorTemp->temp = NULL;

        if (!IS_TYPE_CLASS(tempNode->data.temp.type) ||
            !(dtorTemp->dtor = CClass_Destructor(TYPE_CLASS(tempNode->data.temp.type)))) {
            CError_FATAL(2046);
        }

        tempNode->type = EOBJREF;
        tempNode->data.objref = dtorTemp->object;

        if (isCond) {
            Type* rtype = expr->rtype;
            dtorTemp->temp = create_temp_object(TYPE(&stchar));

            expr = makediadicnode(
                expr,
                makediadicnode(
                    create_objectnode(dtorTemp->temp), intconstnode(TYPE(&stchar), 1), EASS
                ),
                ECOMMA
            );

            expr = makediadicnode(expr, create_objectrefnode(dtorTemp->object), ECOMMA);
            expr->rtype = rtype;

            stmt = CExcept_InsertPrevStatement(ST_EXPRESSION);
            stmt->expr = makediadicnode(
                create_objectnode(dtorTemp->temp), intconstnode(TYPE(&stchar), 0), EASS
            );
            cexcept_prevstmt = stmt;

            ea = lalloc(sizeof(ExceptionAction));
            ea->prev = cexcept_eabefore;
            cexcept_eabefore = ea;

            ea->type = EAT_DESTROYLOCALCOND;
            ea->data.destroy_local_cond.local = dtorTemp->object;
            ea->data.destroy_local_cond.dtor =
                CABI_GetDestructorObject(dtorTemp->dtor, CABIDestroyComplete);
            ea->data.destroy_local_cond.cond = dtorTemp->temp;

            ea = lalloc(sizeof(ExceptionAction));
            *ea = *cexcept_eabefore;
            ea->prev = cexcept_eaafter;
            cexcept_eaafter = ea;
        } else {
            stmt = CExcept_InsertPrevStatement(ST_EXPRESSION);
            stmt->expr = expr;
            cexcept_prevstmt = stmt;

            ea = lalloc(sizeof(ExceptionAction));
            ea->prev = cexcept_eabefore;
            cexcept_eabefore = ea;

            ea->type = EAT_DESTROYLOCAL;
            ea->data.destroy_local.local = dtorTemp->object;
            ea->data.destroy_local.dtor =
                CABI_GetDestructorObject(dtorTemp->dtor, CABIDestroyComplete);

            ea = lalloc(sizeof(ExceptionAction));
            *ea = *cexcept_eabefore;
            ea->prev = cexcept_eaafter;
            cexcept_eaafter = ea;

            expr = lalloc(sizeof(ENode));
            *expr = *stmt->expr;
            expr->type = EOBJREF;
            expr->data.objref = tempArg->node->data.objref;
        }
        return expr;
    } else {
        if (isCond) {
            SInt32 i;
            args = expr->data.funccall.args;
            i = 0;
            while (args) {
                i++;
                args = args->next;
            }

            argArray = lalloc(sizeof(ENodeList*) * i);
            for (args = expr->data.funccall.args, i = 0; args; args = args->next) {
                argArray[i++] = args;
            }

            while (i > 0) {
                i--;
                argArray[i]->node = CExcept_TempTransExprCond(argArray[i]->node);
            }

            expr->data.funccall.funcref = CExcept_TempTransExprCond(expr->data.funccall.funcref);
        } else {
            for (args = expr->data.funccall.args; args; args = args->next) {
                args->node = CExcept_TempTransExpr(args->node);
            }

            expr->data.funccall.funcref = CExcept_TempTransExpr(expr->data.funccall.funcref);
        }

        return expr;
    }
}

static ENode* CExcept_TempTransExprCond(ENode* expr) {
    switch (expr->type) {
        case ETEMP:
            return CExcept_TempTrans_ETEMP(expr);
        case ENEWEXCEPTION:
        case ENEWEXCEPTIONARRAY:
            return CExcept_TransNewException(expr, TRUE);
        case EINITTRYCATCH:
            return CExcept_TransInitTryCatch(expr, TRUE);
        case ENULLCHECK:
            expr->data.nullcheck.nullcheckexpr =
                CExcept_TempTransExprCond(expr->data.nullcheck.nullcheckexpr);
            expr->data.nullcheck.condexpr =
                CExcept_TempTransExprCond(expr->data.nullcheck.condexpr);
            return expr;
        case ECOND:
            expr->data.cond.cond = CExcept_TempTransExprCond(expr->data.cond.cond);
            expr->data.cond.expr1 = CExcept_TempTransExprCond(expr->data.cond.expr1);
            expr->data.cond.expr2 = CExcept_TempTransExprCond(expr->data.cond.expr2);
            return expr;
        case ELAND:
        case ELOR:
            expr->data.diadic.left = CExcept_TempTransExprCond(expr->data.diadic.left);
            expr->data.diadic.right = CExcept_TempTransExprCond(expr->data.diadic.right);
            return expr;
        case EFUNCCALL:
        case EFUNCCALLP:
            return CExcept_TempTransFuncCall(expr, TRUE);
        case EMUL:
        case EDIV:
        case EMODULO:
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
        case ECOMMA:
        case EROTL:
        case EROTR:
            expr->data.diadic.left = CExcept_TempTransExprCond(expr->data.diadic.left);
            expr->data.diadic.right = CExcept_TempTransExprCond(expr->data.diadic.right);
            return expr;
        case EPOSTINC:
        case EPOSTDEC:
        case EPREINC:
        case EPREDEC:
        case EINDIRECT:
        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
        case ETYPCON:
        case EBITFIELD:
            expr->data.monadic = CExcept_TempTransExprCond(expr->data.monadic);
            return expr;
        case EINTCONST:
        case EFLOATCONST:
        case ESTRINGCONST:
        case EOBJREF:
        case EPRECOMP:
        case ELABEL:
        case EINSTRUCTION:
        case EVECTOR128CONST:
            return expr;
        default:
            CError_FATAL(2236);
            return expr;
    }
}

static ENode* CExcept_TempTransExpr(ENode* expr) {
    switch (expr->type) {
        case ETEMP:
            return CExcept_TempTrans_ETEMP(expr);
        case ENEWEXCEPTION:
        case ENEWEXCEPTIONARRAY:
            return CExcept_TransNewException(expr, FALSE);
        case EINITTRYCATCH:
            return CExcept_TransInitTryCatch(expr, FALSE);
        case ENULLCHECK:
            expr->data.nullcheck.nullcheckexpr =
                CExcept_TempTransExpr(expr->data.nullcheck.nullcheckexpr);
            expr->data.nullcheck.condexpr =
                CExcept_TempTransExprCond(expr->data.nullcheck.condexpr);
            return expr;
        case ECOND:
            expr->data.cond.cond = CExcept_TempTransExpr(expr->data.cond.cond);
            expr->data.cond.expr1 = CExcept_TempTransExprCond(expr->data.cond.expr1);
            expr->data.cond.expr2 = CExcept_TempTransExprCond(expr->data.cond.expr2);
            return expr;
        case ELAND:
        case ELOR:
        case ECOMMA:
            expr->data.diadic.left = CExcept_TempTransExpr(expr->data.diadic.left);
            expr->data.diadic.right = CExcept_TempTransExprCond(expr->data.diadic.right);
            return expr;
        case EFUNCCALL:
        case EFUNCCALLP:
            return CExcept_TempTransFuncCall(expr, FALSE);
        case EMUL:
        case EDIV:
        case EMODULO:
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
        case EROTL:
        case EROTR:
            expr->data.diadic.left = CExcept_TempTransExpr(expr->data.diadic.left);
            expr->data.diadic.right = CExcept_TempTransExpr(expr->data.diadic.right);
            return expr;
        case EPOSTINC:
        case EPOSTDEC:
        case EPREINC:
        case EPREDEC:
        case EINDIRECT:
        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
        case ETYPCON:
        case EBITFIELD:
            expr->data.monadic = CExcept_TempTransExpr(expr->data.monadic);
            return expr;
        case EINTCONST:
        case EFLOATCONST:
        case ESTRINGCONST:
        case EOBJREF:
        case EMFPOINTER:
        case EPRECOMP:
        case ELABEL:
        case EOBJLIST:
        case EMEMBER:
        case EINSTRUCTION:
        case EVECTOR128CONST:
            return expr;
        default:
            CError_FATAL(2350);
            return expr;
    }
}

static Statement* CExcept_DtorTransform(Statement* stmt) {
    DtorTemp* dtorTemp;
    Statement* curStmt;
    CLabel* lastLabel;

    dtorTemp = cexcept_dtortemps;
    curStmt = stmt;
    while (dtorTemp) {
        if (cexcept_eaafter &&
            (cexcept_eaafter->type == EAT_DESTROYLOCAL ||
             cexcept_eaafter->type == EAT_DESTROYLOCALCOND) &&
            cexcept_eaafter->data.destroy_local.local == dtorTemp->object) {
            cexcept_eaafter = cexcept_eaafter->prev;
        } else {
            CError_FATAL(2374);
        }

        if (dtorTemp->temp) {
            curStmt = CFunc_InsertStatement(ST_IFNGOTO, curStmt);
            curStmt->expr = create_objectnode(dtorTemp->temp);
            curStmt->dobjstack = cexcept_eaafter;
            lastLabel = curStmt->label = newlabel();
        }

        curStmt = CFunc_InsertStatement(ST_EXPRESSION, curStmt);
        curStmt->expr = CABI_DestroyObject(
            dtorTemp->dtor, create_objectrefnode(dtorTemp->object), CABIDestroyComplete, TRUE, FALSE
        );
        curStmt->dobjstack = cexcept_eaafter;

        if (dtorTemp->temp) {
            curStmt = CFunc_InsertStatement(ST_LABEL, curStmt);
            curStmt->label = lastLabel;
            lastLabel->stmt = curStmt;
        }

        dtorTemp = dtorTemp->next;
    }

    return curStmt;
}

// NOTE: invented names: stmt, discardValue, isCondition
static void CExcept_TempTransform(Statement* stmt, Boolean discardValue, Boolean isCondition) {
    Statement* prevStmt;
    Statement copy;
    Object* tempObj;
    Statement* last;
    Statement* iter;
    Statement* next;

    prevStmt = cexcept_prevstmt;
    last = stmt;
    cexcept_dtortemps = NULL;
    cexcept_serialize = FALSE;
    cexcept_expandtrycatch = FALSE;
    cexcept_hastrycatch = FALSE;
    stmt->expr = CExcept_TempTransExpr(stmt->expr);

    if (cexcept_hastrycatch) {
        cexcept_expandtrycatch = TRUE;
        if (cexcept_serialize) {
            next = stmt->next;
            CInline_SerializeStatement(stmt);
            cexcept_prevstmt = stmt;
            last = stmt;
            while (1) {
                CError_ASSERT(2428, last);
                if (last->next == next) {
                    break;
                }
                last = last->next;
            }
        } else {
            cexcept_prevstmt = prevStmt;
        }

        iter = prevStmt;
        while (1) {
            CError_ASSERT(2435, iter);

            switch (iter->type) {
                case ST_RETURN:
                    CError_ASSERT(2439, iter->expr != NULL);
                case ST_EXPRESSION:
                case ST_SWITCH:
                case ST_IFGOTO:
                case ST_IFNGOTO:
                    iter->expr = CExcept_TempTransExpr(iter->expr);
                    break;
                default:
                    break;
            }

            if (iter == stmt) {
                break;
            }
            cexcept_prevstmt = iter;
            iter = iter->next;
        }
    }

    if (cexcept_dtortemps) {
        if (!discardValue) {
            if (isCondition) {
                ENode* expr = stmt->expr;
                CError_ASSERT(
                    2466, !IS_TYPE_CLASS(expr->rtype) || !CClass_Destructor(TYPE_CLASS(expr->rtype))
                );

                tempObj = create_temp_object(expr->rtype);
                stmt->expr = makediadicnode(create_objectnode(tempObj), expr, EASS);
            }

            copy = *stmt;
            stmt->type = ST_EXPRESSION;
            stmt = CExcept_DtorTransform(last);
            last = CFunc_InsertStatement(copy.type, stmt);
            last->label = copy.label;

            if (isCondition) {
                last->expr = create_objectnode(tempObj);
            } else {
                last->expr = nullnode();
            }
        } else {
            CExcept_DtorTransform(last);
        }
    }
}

static void CExcept_CleanupExceptionActions(Statement* stmt) {
    Statement* iter;
    ENode* expr;
    ExceptionAction* ea;

    for (iter = cexcept_prevstmt = stmt; iter; iter = iter->next) {
        cexcept_eabefore = cexcept_eaafter = iter->dobjstack;

        if (iter->flags & StmtFlag_EndsConstruction) {
            cexcept_eabefore = cexcept_eabefore->prev;
        } else if (iter->type == ST_EXPRESSION) {
            expr = iter->expr;
            while (ENODE_IS(expr, ECOMMA)) {
                expr = expr->data.diadic.left;
            }
            if (ENODE_IS(expr, EINDIRECT)) {
                expr = expr->data.monadic;
            }

            if (ENODE_IS(expr, EFUNCCALL) &&
                ENODE_IS(expr->data.funccall.funcref, EOBJREF) &&
                CClass_IsConstructor(expr->data.funccall.funcref->data.objref) &&
                (ea = iter->dobjstack) &&
                ea->type == EAT_DESTROYLOCAL &&
                expr->data.funccall.args &&
                ENODE_IS(expr->data.funccall.args->node, EOBJREF) &&
                expr->data.funccall.args->node->data.objref == ea->data.destroy_local.local) {
                cexcept_eabefore = cexcept_eabefore->prev;
            }
        }

        switch (iter->type) {
            case ST_EXPRESSION:
                CExcept_TempTransform(iter, TRUE, FALSE);
                break;
            case ST_SWITCH:
            case ST_IFGOTO:
            case ST_IFNGOTO:
                CExcept_TempTransform(iter, FALSE, TRUE);
                break;
            case ST_RETURN:
                if (iter->expr) {
                    CExcept_TempTransform(
                        iter,
                        FALSE,
                        CMach_GetFunctionResultClass(TYPE_FUNC(cscope_currentfunc->type)) != 1
                    );
                }
                break;
            default:
                break;
        }

        iter->dobjstack = cexcept_eabefore;
        cexcept_prevstmt = iter;
    }
}

static void CExcept_InsertSpecificationActions(Statement* stmt, ExceptSpecList* exspecs) {
    Statement* iter;
    ExceptionAction* ea_spec;
    ExceptionAction* ea;
    ExceptSpecList* spec_iter;
    SInt32 i;
    Object* catch_info_object;
    CLabel* label;
    ExceptionAction* ea_catch;

    ea_spec = lalloc(sizeof(ExceptionAction));
    memclrw(ea_spec, sizeof(ExceptionAction));
    ea_spec->type = EAT_SPECIFICATION;

    for (iter = stmt; iter; iter = iter->next) {
        if ((ea = iter->dobjstack)) {
            while (1) {
                if (ea->type == EAT_SPECIFICATION) {
                    break;
                }
                if (ea->prev == NULL) {
                    ea->prev = ea_spec;
                    break;
                }
                ea = ea->prev;
            }
        } else {
            iter->dobjstack = ea_spec;
        }
    }

    iter = stmt;
    while (iter->next) {
        iter = iter->next;
    }

    if (iter->type != ST_GOTO && iter->type != ST_RETURN) {
        iter = CFunc_InsertStatement(ST_RETURN, iter);
        iter->expr = NULL;
        iter->dobjstack = NULL;
        if (TYPE_FUNC(cscope_currentfunc->type)->functype != &stvoid &&
            (copts.pedantic || copts.cplusplus)) {
            CError_Warning(ERR_RETURN_VALUE_EXPECTED);
        }
    }

    iter = CFunc_InsertStatement(ST_LABEL, iter);
    iter->label = newlabel();
    iter->label->stmt = iter;
    iter->flags = StmtFlag_KeepLabel;
    iter->dobjstack = NULL;

    catch_info_object = create_temp_object(TYPE(&catchinfostruct));

    if (!exspecs->type) {
        exspecs = NULL;
    }

    i = 0;
    spec_iter = exspecs;
    while (spec_iter) {
        i++;
        spec_iter = spec_iter->next;
    }

    ea_spec->data.specification.unexp_ids = i;
    ea_spec->data.specification.unexp_id = galloc(sizeof(Object*) * i);
    ea_spec->data.specification.unexp_label = iter->label;
    ea_spec->data.specification.unexp_info_object = catch_info_object;

    i = 0;
    for (spec_iter = exspecs; spec_iter; spec_iter = spec_iter->next) {
        ea_spec->data.specification.unexp_id[i++] =
            CExcept_TypeID(spec_iter->type, spec_iter->qual);
    }

    iter = CFunc_InsertStatement(ST_EXPRESSION, iter);
    iter->expr =
        funccallexpr(Xunex_func, create_objectrefnode(catch_info_object), NULL, NULL, NULL);

    ea_catch = lalloc(sizeof(ExceptionAction));
    memclrw(ea_catch, sizeof(ExceptionAction));
    ea_catch->type = EAT_ACTIVECATCHBLOCK;
    ea_catch->data.active_catch_block.catch_info_object = catch_info_object;
    ea_catch->data.active_catch_block.call_dtor = TRUE;
    iter->dobjstack = ea_catch;

    iter = CFunc_InsertStatement(ST_LABEL, iter);
    iter->label = label = newlabel();
    iter->label->stmt = iter;
    iter->dobjstack = NULL;

    iter = CFunc_InsertStatement(ST_GOTO, iter);
    iter->label = label;
}

static void CExcept_HasFuncCallCallBack(ENode* expr) {
    ENode* funcref = expr->data.funccall.funcref;
    if (ENODE_IS(funcref, EOBJREF)) {
        Object* func = funcref->data.objref;
        // (the original's bug: ENODE_FLAG_NONVIRTUAL is set on the callee, funcref, not on the call, so
        // a virtual function called directly still counts as a virtual call that can throw)
        if (CExcept_CanThrowException(
                func, func->datatype == DVFUNC && (expr->flags & ENODE_FLAG_NONVIRTUAL) == 0
            )) {
            cexcept_canthrow = TRUE;
        }
    } else {
        cexcept_canthrow = TRUE;
    }
}

static Boolean CExcept_CanThrowCheck(Object* func, Statement* stmt) {
    cexcept_canthrow = FALSE;

    for (; stmt; stmt = stmt->next) {
        switch (stmt->type) {
            case ST_NOP:
            case ST_LABEL:
            case ST_GOTO:
            case ST_ASM:
                break;
            case ST_RETURN:
                if (!stmt->expr) {
                    break;
                }
            case ST_EXPRESSION:
            case ST_SWITCH:
            case ST_IFGOTO:
            case ST_IFNGOTO:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
            case ST_GOTOEXPR:
                CExpr_SearchExprTree(
                    stmt->expr, CExcept_HasFuncCallCallBack, 2, EFUNCCALL, EFUNCCALLP
                );
                // the original's: both ways go on to the next statement (evidently meant to stop
                // the scan once a call can throw)
                if (cexcept_canthrow) {
                    continue;
                }
                break;
            default:
                CError_FATAL(2698);
        }
    }

    if (!cexcept_canthrow) {
        TYPE_FUNC(cscope_currentfunc->type)->flags |= FUNC_NOTHROW;
        return FALSE;
    } else {
        return TRUE;
    }
}

void CExcept_ExceptionTansform(Statement* stmt) {
    cexcept_uniqueobjs = NULL;
    CExcept_CleanupExceptionActions(stmt);

    if (cscope_currentfunc && CExcept_CanThrowCheck(cscope_currentfunc, stmt)) {
        CError_ASSERT(2727, IS_TYPE_FUNC(cscope_currentfunc->type));
        if (TYPE_FUNC(cscope_currentfunc->type)->exspecs && copts.exceptions) {
            CExcept_InsertSpecificationActions(stmt, TYPE_FUNC(cscope_currentfunc->type)->exspecs);
        }
    }
}

// The unit's statics are 2-byte packed, as in the original (MWCC lays out .bss with the packing in
// force at the end of the file)
#pragma pack(2)
