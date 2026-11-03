#include "compiler/CFunc.h"
#include "compiler/CABI.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CException.h"
#include "compiler/CExpr.h"
#include "compiler/CInit.h"
#include "compiler/CInline.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/CSOM.h"
#include "compiler/CTemplateTools.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/FuncLevelAsmPPC.h"
#include "compiler/InlineAsm.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/Switch.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include <stdio.h>
#include <string.h>

FuncArg elipsis;
FuncArg oldstyle;
ObjectList* arguments;
ObjectList* locals;
short localcount;
SInt32 curstmtvalue;
SInt32 sourceoffset;
HashNameNode* sourcefilepath;
SInt32 functionbodyoffset;
HashNameNode* functionbodypath;
InitExpr* init_expressions;
CLabel* Labels;
CtorChain* ctor_chain;
Statement* curstmt;
SInt32 symdecloffset;
SInt32 symdeclend;
UInt8 spaceskip;

static short temp_destructor_object_regmem;
static short temp_destructor_objects;
static short temp_expression_has_conditionals;
static DeclBlock* firstblock;
static DeclBlock* currentblock;
static short blockcount;
static Object* sinit_first_object;
static CLabel* sinit_label;
static Boolean ainit_only_one;
static ENode* ainit_expr;
static FuncArg* check_arglist;
static Boolean cfunc_is_extern_c;
static SInt16 cfunc_staticvarcount;
static void* destroyobjects;
static Boolean cfunc_hasdtortemp;

typedef struct CFuncSave {
    CScopeSave scope;
    ObjectList* arguments;
    ObjectList* locals;
    CLabel* labels;
    Statement* curstmt;
    DeclBlock* firstblock;
    DeclBlock* currentblock;
    ExceptionAction* cexcept_dobjstack;
    CLabel* sinit_label;
    ENode* ainit_expr;
    CtorChain* ctor_chain;
    ParserTryBlock* trychain;
    TempNodeCB cinit_tempnodefunc;
    HashNameNode* tkidentifier;
    Object* sinit_first_object;
    FileOffsetInfo cparser_fileoffset;
    TStreamElement symdecltoken;
    SInt32 functionbodyoffset;
    HashNameNode* functionbodypath;
    SInt32 symdecloffset;
    SInt32 symdeclend;
    SInt32 sourceoffset;
    HashNameNode* sourcefilepath;
    SInt32 curstmtvalue;
    NameObjCheckCB name_obj_check;
    FuncArg* check_arglist;
    short blockcount;
    short localcount;
    short tk;
    AccessType global_access;
    Boolean cexcept_hasdobjects;
    Boolean ainit_only_one;
    Boolean cfunc_is_extern_c;
    Boolean temp_reference_init;
} CFuncSave;

static void statement(DeclThing* thing);

static void CFunc_LoopIncrement(void) {
    if (curstmtvalue >= 0x1000) {
        if (curstmtvalue >= 0xF000) {
            curstmtvalue++;
        } else {
            curstmtvalue += 0x1000;
        }
    } else {
        curstmtvalue <<= 3;
    }
}

static void CFunc_LoopDecrement(void) {
    if (curstmtvalue > 0x1000) {
        if (curstmtvalue > 0xF000) {
            curstmtvalue--;
        } else {
            curstmtvalue -= 0x1000;
        }
    } else {
        curstmtvalue >>= 3;
    }

    if (curstmtvalue < 1) {
        curstmtvalue = 1;
    }
}

DeclBlock* CFunc_NewDeclBlock(void) {
    DeclBlock* block;
    NameSpace* nspace;

    block = lalloc(sizeof(DeclBlock));
    if (firstblock) {
        currentblock->next = block;
        currentblock = block;
    } else {
        currentblock = firstblock = block;
    }

    block->index = blockcount++;
    block->parent_nspace = cscope_current;
    block->dobjstack = cexcept_dobjstack;

    nspace = CScope_NewListNameSpace(NULL, FALSE);
    nspace->parent = cscope_current;
    cscope_current = nspace;

    return block;
}

void CFunc_RestoreBlock(DeclBlock* block) {
    if (curstmt && curstmt->dobjstack != cexcept_dobjstack) {
        Statement* stmt = CFunc_AppendStatement(ST_EXPRESSION);
        stmt->expr = nullnode();
    }

    cscope_current = block->parent_nspace;
    cexcept_dobjstack = block->dobjstack;
}

void CFunc_SetupLocalVarInfo(Object* obj) {
    obj->u.var.info = CodeGen_GetNewVarInfo();
    obj->u.var.info->func = cscope_currentfunc;

    if (obj->sclass == TK_REGISTER) {
        if (!copts.optimizesize) {
            obj->u.var.info->usage = 100;
        } else {
            obj->u.var.info->usage = 5;
        }
    }

    if (obj->type && is_volatile_object(obj)) {
        obj->u.var.info->noregister = TRUE;
    }
}

static void adjustargumenttype(DeclInfo* declinfo) {
    switch ((int)declinfo->thetype->type) {
        case TYPECLASS:
            if (TYPE_CLASS(declinfo->thetype)->sominfo) {
                CError_Error(ERR_FUNCTIONS_CANNOT_SOM_CLASS_ARGUMENTS);
                declinfo->thetype = TYPE(&stsignedint);
            }
            break;
        case TYPEFUNC:
            makethetypepointer(declinfo, 0);
            return;
        case TYPEARRAY:
            declinfo->thetype = CDecl_NewPointerType(TPTR_TARGET(declinfo->thetype));
            return;
        case TYPETEMPLATE:
            if (TYPE_TEMPLATE(declinfo->thetype)->dtype == TEMPLDEP_ARRAY) {
                declinfo->thetype =
                    CDecl_NewPointerType(TYPE_TEMPLATE(declinfo->thetype)->u.array.type);
            }
            return;
    }

    if (!CanCreateObject(declinfo->thetype)) {
        declinfo->thetype = TYPE(&stsignedint);
    }
}

static FuncArg* CFunc_IsInArgList(FuncArg* list, HashNameNode* name) {
    while (list) {
        if (name == list->name) {
            return list;
        }
        list = list->next;
    }

    return NULL;
}

static Object* CFunc_IsInObjList(ObjectList* list, HashNameNode* name) {
    while (list) {
        if (name == list->object->name) {
            return list->object;
        }
        list = list->next;
    }

    return NULL;
}

static void CFunc_AppendArg(FuncArg** list, FuncArg* arg) {
    FuncArg* tail;

    if ((tail = *list)) {
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = arg;
    } else {
        *list = arg;
    }
}

static void identifier_list(DeclInfo* declinfo) {
    FuncArg* arg;

    declinfo->oldStyleParameters = TRUE;

    while (1) {
        if (tk != TK_IDENTIFIER) {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        } else {
            if (CFunc_IsInArgList(declinfo->parameterNames, tkidentifier)) {
                CError_Error(ERR_IDENTIFIER_REDECLARED, tkidentifier->name);
            }

            arg = CParser_NewFuncArg();
            arg->name = tkidentifier;
            CFunc_AppendArg(&declinfo->parameterNames, arg);
            declinfo->hasParameterNames = TRUE;
        }

        if ((tk = lex()) != ',') {
            return;
        }
        tk = lex();
    }
}

static Boolean defarg_name_obj_check(HashNameNode* name, Object* obj) {
    FuncArg* arg;

    if (name) {
        for (arg = check_arglist; arg; arg = arg->next) {
            if (arg->name == name) {
                CError_Error(ERR_ILLEGAL_DEFAULT_ARGUMENTS);
                return FALSE;
            }
        }
    }

    if (obj && obj->datatype == DLOCAL) {
        CError_Error(ERR_ILLEGAL_DEFAULT_ARGUMENTS);
        return FALSE;
    }

    return TRUE;
}

static ENode* CFunc_DefaultArg(Type* type, UInt32 qual, FuncArg* args) {
    TStreamElement* element;
    ENode* expr;
    ENode* templdepexpr;

    name_obj_check = defarg_name_obj_check;
    check_arglist = args;
    expr = conv_assignment_expression();
    name_obj_check = NULL;

    if (!CTemplTool_IsTemplateArgumentDependentExpression(expr) &&
        !CTemplTool_IsTemplateArgumentDependentType(type)) {
        expr = argumentpromotion(expr, type, qual, TRUE);
    } else {
        element = CPrep_CurStreamElement();
        if (element && element->tokenfile) {
            templdepexpr = CExpr_NewTemplDepENode(TDE_SOURCEREF);
            templdepexpr->data.templdep.u.sourceref.expr = expr;
            templdepexpr->data.templdep.u.sourceref.token = galloc(sizeof(TStreamElement));
            *templdepexpr->data.templdep.u.sourceref.token = *element;
            expr = templdepexpr;
        }
    }

    return CInline_CopyExpression(expr, CopyModeGlobal);
}

static FuncArg* parameter_list(DeclInfo* declinfo) {
    Boolean is_first;
    FuncArg* arg;
    FuncArg* args;
    DeclInfo my_di;
    Boolean isArray;

    args = NULL;
    is_first = TRUE;

    while (1) {
        if (tk == TK_ELLIPSIS) {
            if (is_first) {
                if (!copts.cplusplus && copts.ANSIstrict) {
                    CError_Warning(ERR_ILLEGAL_FUNCTION_DEFINITION);
                }
                args = &elipsis;
            } else {
                CFunc_AppendArg(&args, &elipsis);
            }

            tk = lex();
            return args;
        }

        memclrw(&my_di, sizeof(my_di));
        CParser_GetDeclSpecs(&my_di, FALSE);
        if (my_di.missingTypeSpecifier) {
            CError_Error(ERR_ILLEGAL_FUNCTION_DEFINITION);
        }

        if (my_di.storageclass &&
            (int)my_di.storageclass != TK_REGISTER &&
            (!copts.cplusplus || (int)my_di.storageclass != TK_AUTO)) {
            CError_Error(ERR_ILLEGAL_FUNCTION_DEFINITION);
            my_di.storageclass = 0;
        }

        my_di.name = NULL;

        scandeclarator(&my_di);

        if (is_first) {
            is_first = FALSE;
            if (my_di.thetype == &stvoid) {
                if (my_di.storageclass || my_di.qual || my_di.name) {
                    CError_Error(ERR_ILLEGAL_FUNCTION_DEFINITION);
                }
                return NULL;
            }
        }

        isArray = IS_TYPE_ARRAY(my_di.thetype);
        adjustargumenttype(&my_di);

        if (my_di.name) {
            if (args && CFunc_IsInArgList(args, my_di.name)) {
                CError_Error(ERR_IDENTIFIER_REDECLARED, my_di.name->name);
            }
        } else {
            my_di.name = no_name_node;
        }

        if (my_di.thetype == &stvoid) {
            CError_Error(ERR_ILLEGAL_USE_VOID);
        }

        arg = CParser_NewFuncArg();
        arg->name = my_di.name;
        arg->type = my_di.thetype;
        arg->qual = my_di.qual;
        arg->sclass = my_di.storageclass;
        arg->is_array = isArray;
        CFunc_AppendArg(&args, arg);

        if (copts.cplusplus && tk == '=') {
            tk = lex();
            arg->dexpr = CFunc_DefaultArg(arg->type, arg->qual, args);
        }

        if (tk != ',') {
            if (tk != TK_ELLIPSIS || !copts.cplusplus) {
                return args;
            }
        } else {
            tk = lex();
        }
    }
}

Boolean CFunc_ParseFakeArgList(Boolean stopAtComma) {
    DeclInfo di;

    if (tk == TK_ELLIPSIS) {
        return TRUE;
    }

    do {
        memclrw(&di, sizeof(di));
        CParser_GetDeclSpecs(&di, FALSE);

        if (di.missingTypeSpecifier) {
            return FALSE;
        }

        scandeclarator(&di);

        if (tk == '=') {
            tk = lex();
            assignment_expression();
        }

        switch (tk) {
            case TK_ELLIPSIS:
                return TRUE;
            case ',':
                if (!stopAtComma) {
                    tk = lex();
                } else {
                    return TRUE;
                }
                break;
            default:
                return FALSE;
        }
    } while (tk != TK_ELLIPSIS);

    return TRUE;
}

FuncArg* parameter_type_list(DeclInfo* declinfo) {
    FuncArg* args;
    NameSpace* nspace;
    Boolean save_in_func_arglist;

    declinfo->hasParameterNames = FALSE;
    declinfo->oldStyleParameters = FALSE;
    declinfo->parameterScope = NULL;

    if (tk == TK_ELLIPSIS || isdeclaration(0, 0, 0, 0)) {
        if (!copts.cplusplus) {
            nspace = CScope_NewListNameSpace(NULL, FALSE);
            nspace->parent = cscope_current;
            cscope_current = nspace;

            save_in_func_arglist = in_func_arglist;
            in_func_arglist = TRUE;
            args = parameter_list(declinfo);
            in_func_arglist = save_in_func_arglist;

            cscope_current = nspace->parent;

            if (!CScope_IsEmptyNameSpace(nspace)) {
                declinfo->parameterScope = nspace;
            }
        } else {
            args = parameter_list(declinfo);
        }
    } else if (copts.cplusplus) {
        args = NULL;
        if (tk != ')') {
            CError_Error(ERR_ILLEGAL_FUNCTION_DEFINITION);
        }
    } else {
        identifier_list(declinfo);
        args = &oldstyle;
    }

    return args;
}

CLabel* findlabel(void) {
    CLabel* scan;

    for (scan = Labels; scan; scan = scan->next) {
        if (tkidentifier == scan->name) {
            return scan;
        }
    }

    return NULL;
}

CLabel* newlabel(void) {
    CLabel* label = lalloc(sizeof(CLabel));
    memclrw(label, sizeof(CLabel));

    label->name = label->uniquename = CParser_GetUniqueName();
    return label;
}

Statement* CFunc_AppendStatement(StatementType sttype) {
    Statement* stmt = lalloc(sizeof(Statement));

    stmt->next = NULL;
    stmt->type = sttype;
    stmt->value = curstmtvalue;
    stmt->flags = 0;
    stmt->sourceoffset = sourceoffset;
    stmt->sourcefilepath = sourcefilepath;
    stmt->dobjstack = cexcept_dobjstack;

    curstmt->next = stmt;
    curstmt = stmt;
    return stmt;
}

Statement* CFunc_InsertStatement(StatementType sttype, Statement* after) {
    Statement* stmt = lalloc(sizeof(Statement));

    stmt->next = after->next;
    after->next = stmt;
    stmt->type = sttype;
    stmt->value = after->value;
    stmt->flags = 0;
    stmt->sourceoffset = after->sourceoffset;
    stmt->sourcefilepath = after->sourcefilepath;
    stmt->dobjstack = after->dobjstack;

    return stmt;
}

Statement* CFunc_InsertBeforeStatement(StatementType sttype, Statement* before) {
    Statement* stmt = lalloc(sizeof(Statement));

    *stmt = *before;
    before->next = stmt;
    before->type = sttype;
    before->flags = 0;

    return before;
}

void CheckCLabels(void) {
    CLabel* scan;

    for (scan = Labels; scan; scan = scan->next) {
        if (!scan->stmt) {
            CError_Error(ERR_UNDEFINED_LABEL, scan->name->name);
        }
    }
}

Object* create_temp_object(Type* type) {
    Object* object = CParser_NewLocalDataObject(NULL, TRUE);
    object->name = CParser_GetUniqueName();
    object->type = type;
    CFunc_SetupLocalVarInfo(object);
    return object;
}

ENode* create_temp_node(Type* type) {
    ENode* node;

    if (cinit_tempnodefunc) {
        return cinit_tempnodefunc(type, FALSE);
    }

    node = CExpr_NewETEMPNode(type, FALSE);
    if (IS_TYPE_CLASS(type) && CClass_Destructor(TYPE_CLASS(type))) {
        node->data.temp.needs_dtor = TRUE;
    }
    return node;
}

ENode* create_temp_node2(Type* type) {
    ENode* node;

    if (cinit_tempnodefunc) {
        return cinit_tempnodefunc(type, TRUE);
    }

    node = CExpr_NewETEMPNode(type, FALSE);
    node->data.temp.needs_dtor = TRUE;
    return node;
}

static ENode* CFunc_DestroyReverse(ENode* expr, DtorTemp* list) {
    expr = makediadicnode(
        expr,
        CABI_DestroyObject(
            list->dtor, create_objectrefnode(list->object), CABIDestroyComplete, TRUE, FALSE
        ),
        ECOMMA
    );
    expr->rtype = &stvoid;
    if (list->next) {
        expr = CFunc_DestroyReverse(expr, list->next);
    }
    return expr;
}

static ENode* CFunc_TempTransDestroy(ENode* expr, DtorTemp* list, Boolean keepValue) {
    Object* tempobj;

    if (keepValue) {
        CError_ASSERT(
            738, !(IS_TYPE_CLASS(expr->rtype) && CClass_Destructor(TYPE_CLASS(expr->rtype)))
        );
        tempobj = create_temp_object(expr->rtype);
        expr = makediadicnode(create_objectnode(tempobj), expr, EASS);
    }

    expr = CFunc_DestroyReverse(expr, list);

    if (keepValue) {
        expr = makediadicnode(expr, create_objectnode(tempobj), ECOMMA);
        expr->rtype = tempobj->type;
    }

    return expr;
}

void CFunc_WarnUnused(void) {
    if (copts.warn_unusedvar) {
        ObjectList* list;
        for (list = locals; list; list = list->next) {
            if (!(list->object->flags & OBJECT_USED) &&
                !IsTempName(list->object->name) &&
                !(list->object->qual & Q_INLINE_DATA)) {
                CError_SetErrorToken(&list->object->u.var.info->deftoken);
                CError_Warning(ERR_VARIABLE_ARGUMENT_NOT_USED_FUNCTION, &list->object->name->name);
            }
        }
    }

    if (copts.warn_unusedarg) {
        ObjectList* list;
        for (list = arguments; list; list = list->next) {
            if (!(list->object->flags & OBJECT_USED) &&
                !IsTempName(list->object->name) &&
                list->object->name != this_name_node &&
                list->object->name != self_name_node) {
                CError_SetErrorToken(&symdecltoken);
                CError_Warning(ERR_VARIABLE_ARGUMENT_NOT_USED_FUNCTION, &list->object->name->name);
            }
        }
    }
}

void CFunc_CodeCleanup(Statement* stmt) {
    if (cscope_currentclass && cscope_currentclass->sominfo) {
        CSOM_InitSOMSelf(cscope_currentclass, stmt);
    }

    CFunc_WarnUnused();
    CExcept_ExceptionTansform(stmt);
}

static Boolean DestructorNeeded(ExceptionAction* ea, ExceptionAction* end) {
    while (ea) {
        if (CExcept_ActionNeedsDestruction(ea)) {
            return TRUE;
        }
        if (ea == end) {
            break;
        }
        ea = ea->prev;
    }

    return FALSE;
}

static Statement* DestructLocals(Statement* stmt, ExceptionAction* ea, ExceptionAction* end) {
    while (ea) {
        stmt = CExcept_ActionCleanup(ea, stmt);
        if (ea == end) {
            break;
        }
        ea = ea->prev;
    }

    return stmt;
}

static Boolean NeedsDestruction(Statement* stmt1, Statement* stmt2) {
    ExceptionAction* ea2;
    ExceptionAction* ea1;
    ExceptionAction* scan;

    ea1 = stmt1->dobjstack;
    ea2 = stmt2->dobjstack;

    for (scan = ea2; scan; scan = scan->prev) {
        if (scan == ea1) {
            return FALSE;
        }
    }

    while (ea1 && ea1 != ea2) {
        if (CExcept_ActionNeedsDestruction(ea1)) {
            return TRUE;
        }
        ea1 = ea1->prev;
    }

    return FALSE;
}

static ExceptionAction* FindLastNonCommonStackObj(Statement* stmt1, Statement* stmt2) {
    ExceptionAction* ea1;
    ExceptionAction* ea2;

    for (ea2 = stmt2->dobjstack; ea2; ea2 = ea2->prev) {
        for (ea1 = stmt1->dobjstack; ea1; ea1 = ea1->prev) {
            if (ea1->prev == ea2) {
                return ea1;
            }
        }
    }

    return NULL;
}

static void DestructorReturnTransform(Statement* stmt1, Statement* stmt2) {
    Statement* stmt;
    Object* tempobj;

    if (stmt1->dobjstack != stmt2->dobjstack && NeedsDestruction(stmt1, stmt2)) {
        stmt1 = DestructLocals(stmt1, stmt1->dobjstack, FindLastNonCommonStackObj(stmt1, stmt2));
    }

    if (DestructorNeeded(stmt2->dobjstack, NULL)) {
        if (CMach_GetFunctionResultClass(TYPE_FUNC(cscope_currentfunc->type)) != 1) {
            tempobj = create_temp_object(stmt2->expr->rtype);
            stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt1);
            stmt->expr = makediadicnode(create_objectnode(tempobj), stmt2->expr, EASS);
            stmt->sourceoffset = stmt2->sourceoffset;
            stmt->sourcefilepath = stmt2->sourcefilepath;
            DestructLocals(stmt, stmt2->dobjstack, NULL);
            stmt2->expr = create_objectnode(tempobj);
        } else {
            stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt1);
            stmt->expr = stmt2->expr;
            stmt->sourceoffset = stmt2->sourceoffset;
            stmt->sourcefilepath = stmt2->sourcefilepath;
            DestructLocals(stmt, stmt2->dobjstack, NULL);
            stmt2->expr = nullnode();
        }
    }
}

static Statement* DestructorIfTransform(Statement* stmt) {
    CLabel* label;
    Statement* newStmt;

    if (stmt->type == ST_IFGOTO) {
        stmt->type = ST_IFNGOTO;
    } else {
        stmt->type = ST_IFGOTO;
    }

    label = newlabel();
    newStmt =
        DestructLocals(stmt, stmt->dobjstack, FindLastNonCommonStackObj(stmt, stmt->label->stmt));
    newStmt = CFunc_InsertStatement(ST_GOTO, newStmt);
    newStmt->label = stmt->label;
    newStmt = CFunc_InsertStatement(ST_LABEL, newStmt);
    newStmt->label = label;
    label->stmt = newStmt;
    stmt->label = label;
    newStmt->dobjstack = stmt->dobjstack;
    return newStmt;
}

static inline Boolean CFunc_ScopeContains(ExceptionAction* from, ExceptionAction* to) {
    if (!to) {
        return TRUE;
    }
    while (from) {
        if (from == to) {
            return TRUE;
        }
        from = from->prev;
    }
    return FALSE;
}

static inline void CFunc_CheckJump(Statement* stmt, Statement* dest) {
    ExceptionAction* to = dest->dobjstack;

    if (stmt->dobjstack != to && !CFunc_ScopeContains(stmt->dobjstack, to)) {
        while (to) {
            if (CExcept_ActionNeedsDestruction(to) && to->type != EAT_ACTIVECATCHBLOCK) {
                CError_Warning(ERR_ILLEGAL_JUMP_PAST_INITIALIZER);
                break;
            }
            to = to->prev;
        }
    }
}

static inline Boolean CFunc_NeedsCleanup(Statement* stmt, Statement* dest) {
    ExceptionAction *from, *p;

    from = stmt->dobjstack;
    for (p = dest->dobjstack; p; p = p->prev) {
        if (p == from) {
            return FALSE;
        }
    }
    for (from = stmt->dobjstack, p = dest->dobjstack; from && from != p; from = from->prev) {
        if (CExcept_ActionNeedsDestruction(from)) {
            return TRUE;
        }
    }
    return FALSE;
}

static inline Boolean CFunc_AnyCleanup(ExceptionAction* p) {
    while (p) {
        if (CExcept_ActionNeedsDestruction(p)) {
            return TRUE;
        }
        if (!p) {
            break;
        }
        p = p->prev;
    }
    return FALSE;
}

static inline ExceptionAction* CFunc_CommonScope(Statement* stmt, ExceptionAction* to) {
    ExceptionAction* p;

    while (to) {
        for (p = stmt->dobjstack; p; p = p->prev) {
            if (p->prev == to) {
                return p;
            }
        }
        to = to->prev;
    }
    return NULL;
}

static inline Statement* CFunc_EmitCleanups(
    Statement* stmt, ExceptionAction* p, ExceptionAction* stop
) {
    for (; p; p = p->prev) {
        stmt = CExcept_ActionCleanup(p, stmt);
        if (p == stop) {
            break;
        }
    }
    return stmt;
}

static inline Statement* CFunc_EmitAllCleanups(Statement* stmt, ExceptionAction* p) {
    for (; p; p = p->prev) {
        stmt = CExcept_ActionCleanup(p, stmt);
        if (!p) {
            break;
        }
    }
    return stmt;
}

void CFunc_DestructorCleanup(Statement* first) {
    SwitchCase* c;
    Statement *stmt, *next;

    if (!copts.cplusplus) {
        return;
    }
    for (next = first; next; next = next->next) {
        switch (next->type) {
            case ST_SWITCH:
                CFunc_CheckJump(next, ((SwitchInfo*)next->label)->defaultlabel->stmt);
                for (c = ((SwitchInfo*)next->label)->cases; c; c = c->next) {
                    CFunc_CheckJump(next, c->label->stmt);
                }
                break;
            case ST_GOTO:
            case ST_IFGOTO:
            case ST_IFNGOTO:
                CFunc_CheckJump(next, next->label->stmt);
                break;
            case ST_NOP:
            case ST_LABEL:
            case ST_EXPRESSION:
            case ST_RETURN:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
            case ST_GOTOEXPR:
            case ST_ASM:
                break;
            default:
                CError_FATAL(1045);
                break;
        }
    }
    if (!cexcept_hasdobjects) {
        return;
    }
    stmt = first;
    for (;;) {
        if (!(next = stmt->next)) {
            if (stmt->type != ST_RETURN && stmt->dobjstack) {
                CFunc_EmitAllCleanups(stmt, stmt->dobjstack);
            }
            break;
        }
        switch (next->type) {
            case ST_GOTO:
                if (stmt->dobjstack != next->label->stmt->dobjstack &&
                    CFunc_NeedsCleanup(stmt, next->label->stmt)) {
                    CFunc_EmitCleanups(
                        stmt, stmt->dobjstack, CFunc_CommonScope(stmt, next->label->stmt->dobjstack)
                    );
                }
                stmt = next;
                break;
            case ST_RETURN:
                if (next->expr && CFunc_AnyCleanup(stmt->dobjstack)) {
                    DestructorReturnTransform(stmt, next);
                } else if (stmt->dobjstack) {
                    CFunc_EmitAllCleanups(stmt, stmt->dobjstack);
                }
                stmt = next;
                break;
            default:
                switch (stmt->type) {
                    case ST_NOP:
                    case ST_LABEL:
                    case ST_EXPRESSION:
                    case ST_IFGOTO:
                    case ST_IFNGOTO:
                    case ST_BEGINCATCH:
                    case ST_ENDCATCH:
                    case ST_ENDCATCHDTOR:
                    case ST_ASM:
                        if (stmt->dobjstack != next->dobjstack && CFunc_NeedsCleanup(stmt, next)) {
                            CFunc_EmitCleanups(
                                stmt, stmt->dobjstack, CFunc_CommonScope(stmt, next->dobjstack)
                            );
                        }
                        break;
                    case ST_GOTO:
                    case ST_SWITCH:
                    case ST_RETURN:
                    case ST_GOTOEXPR:
                        break;
                    default:
                        CError_FATAL(1109);
                        break;
                }
                switch (next->type) {
                    case ST_NOP:
                    case ST_LABEL:
                    case ST_EXPRESSION:
                    case ST_SWITCH:
                    case ST_BEGINCATCH:
                    case ST_ENDCATCH:
                    case ST_ENDCATCHDTOR:
                    case ST_ASM:
                        stmt = next;
                        break;
                    case ST_IFGOTO:
                    case ST_IFNGOTO:
                        if (next->dobjstack != next->label->stmt->dobjstack &&
                            CFunc_NeedsCleanup(next, next->label->stmt)) {
                            stmt = DestructorIfTransform(next);
                        } else {
                            stmt = next;
                        }
                        break;
                    default:
                        CError_FATAL(1138);
                        break;
                }
        }
    }
}

static void scancase(DeclThing* thing) {
    SwitchCase* swcase;
    Statement* stmt;
    CInt64 min;

    if (!thing->switchinfo) {
        CError_Error(ERR_ILLEGAL_USE_KEYWORD);
        return;
    }

    tk = lex();
    min = CExpr_IntConstConvert(
        thing->switchinfo->exprtype, thing->switchinfo->exprtype, CExpr_IntegralConstExpr()
    );
    for (swcase = thing->switchinfo->cases; swcase; swcase = swcase->next) {
        if (CInt64_GreaterEqual(swcase->min, min) && CInt64_LessEqual(swcase->min, min)) {
            CError_Error(ERR_CASE_CONSTANT_DEFINED_MORE_THAN_ONCE);
        }
    }

    stmt = CFunc_AppendStatement(ST_LABEL);
    stmt->label = newlabel();
    stmt->label->stmt = stmt;

    swcase = lalloc(sizeof(SwitchCase));
    swcase->min = min;
    swcase->label = stmt->label;
    swcase->next = thing->switchinfo->cases;
    thing->switchinfo->cases = swcase;

    if (tk != ':') {
        CError_ErrorSkip(ERR_COLON_EXPECTED);
    } else {
        tk = lex();
    }

    statement(thing);
}

static void CFunc_NameLocalStaticDataObject(Object* obj, char* str) {
    char buf[64];
    HashNameNode* name;

    if (!(cscope_currentfunc && (cscope_currentfunc->qual & Q_INLINE)) ||
        CParser_HasInternalLinkage(cscope_currentfunc)) {
        obj->name = CParser_AppendUniqueName(str);
    } else {
        sprintf(buf, "$localstatic%ld$", (long)cfunc_staticvarcount++);
        name = CMangler_GetLinkName(cscope_currentfunc);
        name = CParser_NameConcat(buf, name->name);
        name = CParser_NameConcat(str, name->name);
        obj->name = name;
        obj->qual |= Q_IMPLICIT_WEAK;
        obj->sclass = 0;
    }
}

static void sinit_insert_expr(ENode* expr) {
    Statement* stmt;

    if (!sinit_first_object) {
        sinit_first_object = CParser_NewCompilerDefDataObject();
        sinit_first_object->type = TYPE(&stsignedchar);
        sinit_first_object->sclass = TK_STATIC;
        CFunc_NameLocalStaticDataObject(sinit_first_object, "init");
        CInit_DeclareData(sinit_first_object, NULL, NULL, sinit_first_object->type->size);

        sinit_label = newlabel();
        stmt = CFunc_AppendStatement(ST_IFGOTO);
        stmt->expr = create_objectnode(sinit_first_object);
        stmt->label = sinit_label;
    }

    stmt = CFunc_AppendStatement(ST_EXPRESSION);
    stmt->expr = expr;
}

static void ainit_insert_expr(ENode* expr) {
    Statement* stmt;

    if (ainit_only_one) {
        if (ainit_expr) {
            stmt = CFunc_AppendStatement(ST_EXPRESSION);
            stmt->expr = ainit_expr;
        }
        ainit_expr = expr;
    } else {
        stmt = CFunc_AppendStatement(ST_EXPRESSION);
        stmt->expr = expr;
    }
}

static ENode* ainit_register_object(Type* type, Object* local, SInt32 offset, Boolean unused) {
    return CExcept_RegisterDestructorObject(
        local, offset, CClass_Destructor(TYPE_CLASS(type)), unused
    );
}

static void CFunc_LocalDataDeclarator(
    DeclInfo* di, TStreamElement* deftoken, Boolean isParameterList, Boolean isCondition
) {
    Object* aliasObject;
    NameSpace* globalNS;
    Object* object;
    NameSpaceObjectList* nsol;
    NameSpaceName* nsname;
    Statement* stmt;

    if (di->nspace) {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
    }

    CDecl_CompleteType(di->thetype);

    object = NULL;

    if ((nsol = CScope_FindName(cscope_current, di->name))) {
        switch (nsol->object->otype) {
            case OT_OBJECT:
                object = OBJECT(nsol->object);
                break;
            case OT_NAMESPACE:
                CError_Error(ERR_ILLEGAL_USE_NAMESPACE_NAME);
                return;
            case OT_ENUMCONST:
            case OT_TYPE:
                CError_Error(ERR_ILLEGAL_NAME_OVERLOADING);
                break;
            case OT_TYPETAG:
                break;
            default:
                CError_FATAL(1344);
        }
    }

    if (object) {
        CError_Error(ERR_OBJECT_REDEFINED, object);
    }

    if ((int)di->storageclass == TK_EXTERN) {
        object = NULL;
        globalNS = CScope_FindGlobalNS(cscope_current);
        if ((nsol = CScope_FindName(globalNS, di->name))) {
            switch (nsol->object->otype) {
                case OT_OBJECT:
                    object = OBJECT(nsol->object);
                    break;
                case OT_NAMESPACE:
                    CError_Error(ERR_ILLEGAL_USE_NAMESPACE_NAME);
                    return;
                case OT_ENUMCONST:
                case OT_TYPE:
                    CError_Error(ERR_ILLEGAL_NAME_OVERLOADING);
                    break;
                case OT_TYPETAG:
                    break;
                default:
                    CError_FATAL(1381);
            }
        }

        if (object) {
            if (!is_typesame(di->thetype, object->type) ||
                (di->qual &
                 (Q_CONST | Q_VOLATILE | Q_PASCAL | Q_IMPLICIT_WEAK | Q_WEAK | Q_ALIGNED_MASK)) !=
                    (object->qual &
                     (Q_CONST | Q_VOLATILE | Q_PASCAL | Q_IMPLICIT_WEAK | Q_WEAK |
                      Q_ALIGNED_MASK))) {
                CError_Error(
                    ERR_IDENTIFIER_REDECLARED_WAS_DECLARED_AS_NOW,
                    di->name->name,
                    object->type,
                    object->qual,
                    di->thetype,
                    di->qual
                );
            }
        } else {
            object = CParser_NewGlobalDataObject(di);
            object->nspace = globalNS;
        }

        CParser_NewAliasObject(object, 0);
        return;
    }

    if ((int)di->storageclass != TK_STATIC) {
        object = CParser_NewObject(di);
    } else {
        object = CParser_NewGlobalDataObject(di);
    }

    object->name = di->name;
    object->type = di->thetype;
    object->qual = di->qual;
    object->sclass = di->storageclass;

    switch (di->storageclass) {
        case TK_STATIC:
            if (isParameterList) {
                CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
                break;
            }
            if (isCondition) {
                CError_Error(ERR_ILLEGAL_INITIALIZATION);
            }

            if (CanCreateObject(di->thetype)) {
                CError_QualifierCheck(
                    di->qual &
                    ~(Q_CONST | Q_VOLATILE | Q_PASCAL | Q_IMPLICIT_WEAK | Q_WEAK | Q_ALIGNED_MASK)
                );
                CParser_NewAliasObject(object, 0);
                object->nspace = cscope_root;
                object->datatype = DDATA;
                CFunc_NameLocalStaticDataObject(object, object->name->name);

                if (copts.cplusplus) {
                    sinit_first_object = NULL;
                    CInit_InitializeStaticData(object, sinit_insert_expr);
                    if (sinit_first_object) {
                        stmt = CFunc_AppendStatement(ST_EXPRESSION);
                        stmt->expr = makediadicnode(
                            create_objectnode(sinit_first_object),
                            intconstnode(TYPE(&stsignedchar), 1),
                            EASS
                        );
                        stmt = CFunc_AppendStatement(ST_LABEL);
                        stmt->label = sinit_label;
                        sinit_label->stmt = stmt;
                    }
                } else {
                    CInit_InitializeData(object);
                }
            }
            break;

        case 0:
        case TK_AUTO:
        case TK_REGISTER:
            if (CanCreateObject(di->thetype)) {
                CError_QualifierCheck(
                    di->qual & ~(Q_CONST | Q_VOLATILE | Q_PASCAL | Q_ALIGNED_MASK)
                );
                object->datatype = DLOCAL;
                CFunc_SetupLocalVarInfo(object);

                object->u.var.info->deftoken = *deftoken;
                if (object->sclass == TK_REGISTER && isParameterList) {
                    object->u.var.info->usage = 100;
                }

                CScope_AddObject(cscope_current, object->name, OBJ_BASE(object));

                if (!isParameterList) {
                    if (IS_TYPE_SOM_CLASS(di->thetype)) {
                        CSOM_InitAutoClass(object);
                    } else {
                        CInit_InitializeAutoData(object, ainit_insert_expr, ainit_register_object);
                        if (object->type != di->thetype &&
                            (IS_TYPE_STRUCT(object->type) || IS_TYPE_CLASS(object->type))) {
                            CError_ASSERT(1478, !cscope_current->is_hash);
                            CError_ASSERT(
                                1479,
                                nsname = CScope_FindNameSpaceName(cscope_current, object->name)
                            );
                            CError_ASSERT(1480, nsname->first.object == OBJ_BASE(object));
                            CError_ASSERT(1481, !nsname->first.next);
                            nsname->name = CParser_AppendUniqueName(object->name->name);

                            aliasObject = CParser_NewAliasObject(object, 0);
                            aliasObject->type = di->thetype;
                        }
                    }
                }

                if (object->datatype == DLOCAL) {
                    ObjectList* list = lalloc(sizeof(ObjectList));
                    list->object = object;
                    list->next = locals;
                    locals = list;
                }

                IsCompleteType(di->thetype);
            }
            break;

        default:
            CError_FATAL(1504);
    }
}

static ENode* CFunc_ParseLocalDeclarationList(
    Boolean isParameterList, Boolean isCondition, Boolean isForInit, Boolean isStatement
) {
    Type* type;
    UInt32 qual;
    DeclInfo di;
    TStreamElement deftoken;

    ainit_expr = NULL;
    ainit_only_one = isCondition;

    while (isCondition || isdeclaration(copts.cplusplus, 0, 0, 0)) {
        CPrep_GetFileOffsetInfo2(&cparser_fileoffset, &sourceoffset, &sourcefilepath);

        memclrw(&di, sizeof(di));
        di.is_extern_c = cfunc_is_extern_c;
        CParser_GetDeclSpecs(&di, FALSE);

        if (IS_TYPE_TEMPLATE(di.thetype)) {
            CError_Error(ERR_ILLEGAL_TYPE);
            di.thetype = TYPE(&stsignedint);
        }

        type = di.thetype;
        qual = di.qual;

        if (tk != ';') {
            while (1) {
                deftoken = *CPrep_CurStreamElement();
                di.name = NULL;
                scandeclarator(&di);

                if (di.name) {
                    if (di.storageclass != TK_TYPEDEF) {
                        if (IS_TYPE_FUNC(di.thetype)) {
                            if (!CDecl_FunctionDeclarator(
                                    &di, CScope_FindGlobalNS(cscope_current), FALSE, FALSE
                                )) {
                                break;
                            }
                        } else {
                            CFunc_LocalDataDeclarator(&di, &deftoken, isParameterList, isCondition);
                        }
                    } else {
                        CDecl_TypedefDeclarator(&di);
                    }
                } else {
                    CError_Error(ERR_DECLARATOR_EXPECTED);
                }

                if (tk == ';') {
                    break;
                }

                if (tk != ',') {
                    if (!isCondition) {
                        CError_Error(ERR_SEMICOLON_EXPECTED);
                    }
                    break;
                }

                di.nspace = NULL;
                di.thetype = type;
                di.qual = qual;
                tk = lex();
            }
        } else {
            CParser_CheckAnonymousUnion(&di, TRUE);
        }

        if (isCondition) {
            break;
        }
        if (isStatement) {
            break;
        }
        tk = lex();
    }

    if (isCondition) {
        if (!ainit_expr) {
            if (!isForInit) {
                CError_Error(ERR_EXPRESSION_SYNTAX_ERROR);
                ainit_expr = nullnode();
            }
        } else {
            ainit_expr = checkreference(ainit_expr);
        }
    }

    return ainit_expr;
}

static void makeifstatement(
    ENode* expr, CLabel* label1, CLabel* label2, Boolean ifTrue, Boolean isLoopBranch
) {
    Statement* stmt;
    CLabel* nextlabel;

    if (!expr) {
        if (ifTrue) {
            stmt = CFunc_AppendStatement(ST_GOTO);
            stmt->label = label1;
            return;
        }
        return;
    }

    if (ENODE_IS(expr, ETYPCON) &&
        IS_TYPE_INT(expr->rtype) &&
        IS_TYPE_INT(expr->data.monadic->rtype) &&
        expr->rtype->size >= expr->data.monadic->rtype->size) {
        expr = expr->data.monadic;
    }

    if (isnotzero(expr)) {
        if (ifTrue) {
            stmt = CFunc_AppendStatement(ST_GOTO);
            stmt->label = label1;
            return;
        }
        return;
    }

    if (iszero(expr)) {
        if (!ifTrue) {
            stmt = CFunc_AppendStatement(ST_GOTO);
            stmt->label = label1;
            return;
        }
        return;
    }

    if (ENODE_IS(expr, ELOGNOT)) {
        makeifstatement(expr->data.monadic, label1, label2, !ifTrue, isLoopBranch);
        return;
    }

    if (ENODE_IS(expr, ELOR)) {
        nextlabel = newlabel();
        if (ifTrue) {
            makeifstatement(expr->data.diadic.left, label1, nextlabel, TRUE, isLoopBranch);
            nextlabel->stmt = CFunc_AppendStatement(ST_LABEL);
            nextlabel->stmt->label = nextlabel;
            makeifstatement(expr->data.diadic.right, label1, label2, TRUE, isLoopBranch);
            return;
        } else {
            makeifstatement(expr->data.diadic.left, label2, nextlabel, TRUE, isLoopBranch);
            nextlabel->stmt = CFunc_AppendStatement(ST_LABEL);
            nextlabel->stmt->label = nextlabel;
            makeifstatement(expr->data.diadic.right, label1, label2, FALSE, isLoopBranch);
            return;
        }
    }

    if (ENODE_IS(expr, ELAND)) {
        nextlabel = newlabel();
        if (ifTrue) {
            makeifstatement(expr->data.diadic.left, label2, nextlabel, FALSE, isLoopBranch);
            nextlabel->stmt = CFunc_AppendStatement(ST_LABEL);
            nextlabel->stmt->label = nextlabel;
            makeifstatement(expr->data.diadic.right, label1, label2, TRUE, isLoopBranch);
            return;
        } else {
            makeifstatement(expr->data.diadic.left, label1, nextlabel, FALSE, isLoopBranch);
            nextlabel->stmt = CFunc_AppendStatement(ST_LABEL);
            nextlabel->stmt->label = nextlabel;
            makeifstatement(expr->data.diadic.right, label1, label2, FALSE, isLoopBranch);
            return;
        }
    }

    stmt = CFunc_AppendStatement(ST_IFGOTO);
    stmt->label = label1;
    stmt->expr = expr;
    if (!ifTrue) {
        stmt->type = ST_IFNGOTO;
    }
    if (isLoopBranch) {
        stmt->flags |= StmtFlag_LoopBranch;
    }
}

static void CFunc_HasDtorTempCallBack(ENode* expr) {
    if (expr->data.temp.needs_dtor || expr->data.temp.uniqueid) {
        cfunc_hasdtortemp = TRUE;
    }
}

static void ifstatement(Boolean ifTrue, ENode* expr, CLabel* label, Boolean isLoopBranch) {
    Statement* stmt;
    CLabel* endlabel;

    if (expr && copts.cplusplus && copts.exceptions) {
        cfunc_hasdtortemp = FALSE;
        CExpr_SearchExprTree(expr, CFunc_HasDtorTempCallBack, 1, ETEMP);
        if (cfunc_hasdtortemp) {
            stmt = CFunc_AppendStatement(ifTrue ? ST_IFGOTO : ST_IFNGOTO);
            stmt->label = label;
            stmt->expr = expr;
            if (isLoopBranch) {
                stmt->flags |= StmtFlag_LoopBranch;
            }
            return;
        }
    }

    endlabel = newlabel();
    makeifstatement(expr, label, endlabel, ifTrue, isLoopBranch);
    endlabel->stmt = CFunc_AppendStatement(ST_LABEL);
    endlabel->stmt->label = endlabel;
}

Statement* CFunc_GenerateLoop(
    Statement* stmt,
    Type* type,
    ENode* lowerBound,
    ENode* upperBound,
    ENode* increment1,
    ENode* increment2,
    ENode* (*callback)(ENode*, ENode*)
) {
    ENode* cursor;
    ENode* cursor2;
    CLabel* label;
    ENode* ind;
    ENode* ind2;
    Statement* s;

    cursor = CExpr_NewETEMPNode(type, TRUE);
    ind = lalloc(sizeof(ENode));
    *ind = *cursor;
    ind = makemonadicnode(ind, EINDIRECT);
    ind->rtype = type;

    if (stmt) {
        s = stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
    } else {
        s = CFunc_AppendStatement(ST_EXPRESSION);
    }
    s->expr = makediadicnode(ind, lowerBound, EASS);

    if (increment2) {
        cursor2 = CExpr_NewETEMPNode(type, TRUE);
        ind = lalloc(sizeof(ENode));
        *ind = *cursor2;
        ind = makemonadicnode(ind, EINDIRECT);
        ind->rtype = type;

        if (stmt) {
            s = stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
        } else {
            s = CFunc_AppendStatement(ST_EXPRESSION);
        }
        s->expr = makediadicnode(ind, increment2, EASS);
    }

    label = newlabel();
    if (stmt) {
        s = stmt = CFunc_InsertStatement(ST_LABEL, stmt);
    } else {
        s = CFunc_AppendStatement(ST_LABEL);
    }
    s->label = label;
    label->stmt = s;

    if (callback) {
        ind = lalloc(sizeof(ENode));
        *ind = *cursor;
        ind = makemonadicnode(ind, EINDIRECT);
        ind->rtype = type;

        if (increment2) {
            ind2 = lalloc(sizeof(ENode));
            *ind2 = *cursor2;
            ind2 = makemonadicnode(ind2, EINDIRECT);
            ind2->rtype = type;
        } else {
            ind2 = NULL;
        }

        if ((ind = callback(ind, ind2))) {
            if (stmt) {
                s = stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
            } else {
                s = CFunc_AppendStatement(ST_EXPRESSION);
            }
            s->expr = ind;
        }
    }

    if (increment1) {
        ind = lalloc(sizeof(ENode));
        *ind = *cursor;
        ind = makemonadicnode(ind, EINDIRECT);
        ind->rtype = type;

        if (stmt) {
            s = stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
        } else {
            s = CFunc_AppendStatement(ST_EXPRESSION);
        }
        s->expr = makediadicnode(ind, increment1, EADDASS);

        if (increment2) {
            ind = lalloc(sizeof(ENode));
            *ind = *cursor2;
            ind = makemonadicnode(ind, EINDIRECT);
            ind->rtype = type;

            if (stmt) {
                s = stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
            } else {
                s = CFunc_AppendStatement(ST_EXPRESSION);
            }
            s->expr = makediadicnode(ind, increment1, EADDASS);
        }
    }

    ind = lalloc(sizeof(ENode));
    *ind = *cursor;
    ind = makemonadicnode(ind, EINDIRECT);
    ind->rtype = type;

    if (stmt) {
        s = stmt = CFunc_InsertStatement(ST_IFGOTO, stmt);
    } else {
        s = CFunc_AppendStatement(ST_IFGOTO);
    }
    s->expr = makediadicnode(ind, upperBound, ELESS);
    s->expr->rtype = TYPE(&stbool);
    s->label = label;
    s->flags = StmtFlag_LoopBranch;

    return stmt;
}

static Boolean checklabel(void) {
    HashNameNode* savename;
    short savesize;
    short token;

    savename = tkidentifier;
    savesize = tksize;
    token = lookahead();
    tkidentifier = savename;
    tksize = savesize;

    return token == ':';
}

static ENode* returnstatementadjust(ENode* expr, Type* type, UInt32 qual) {
    Object* object;
    ENode* expr2;
    ENode* objexpr;
    ObjectList* list;
    ENodeList* exprlist;

    for (list = arguments; list; list = list->next) {
        if (list->object->name == temp_argument_name) {
            break;
        }
    }

    CError_ASSERT(1907, list);

    object = list->object;
    if ((expr2 = CExpr_IsTempConstruction(expr, type, &objexpr)) && ENODE_IS(objexpr, ETEMP)) {
        *objexpr = *create_objectnode(object);
        return expr2;
    }

    if (IS_TYPE_CLASS(type)) {
        expr2 = create_objectnode(object);
        exprlist = lalloc(sizeof(ENodeList));
        exprlist->next = NULL;
        exprlist->node = expr;
        expr2 = CExpr_ConstructObject(
            TYPE_CLASS(type), expr2, exprlist, TRUE, TRUE, FALSE, TRUE, FALSE
        );
    } else {
        expr2 = makemonadicnode(create_objectnode(object), EINDIRECT);
        expr2->rtype = type;
        expr2 = makediadicnode(expr2, CExpr_AssignmentPromotion(expr, type, qual, TRUE), EASS);
    }

    return expr2;
}

static void CFunc_AutoResultCheck(ENode* expr) {
    while (1) {
        while (ENODE_IS(expr, ECOMMA)) {
            expr = expr->data.diadic.right;
        }

        switch ((int)expr->type) {
            case EOBJREF:
                if (expr->data.objref->datatype != DLOCAL) {
                    break;
                }
            case ETEMP:
                CError_Warning(ERR_FUNCTION_RESULT_POINTER_REFERENCE_AUTOMATIC_VARIABLE);
                break;
            case EADD:
            case ESUB:
                CFunc_AutoResultCheck(expr->data.diadic.left);
                expr = expr->data.diadic.right;
                continue;
        }
        break;
    }
}

static void statement(DeclThing* thing) {
    Statement* stmt;
    Statement* stmt2;
    CLabel* label;
    DeclBlock* block;
    ENode* expr;
    HashNameNode* name;
    DeclThing subthing;

    CPrep_GetFileOffsetInfo2(&cparser_fileoffset, &sourceoffset, &sourcefilepath);

    switch (tk) {
        case TK_RETURN:
            tk = lex();
            if ((thing->thetype == &stvoid && !copts.cplusplus) ||
                CClass_IsConstructor(cscope_currentfunc) ||
                CClass_IsDestructor(cscope_currentfunc)) {
                if (tk != ';') {
                    CError_Error(ERR_ILLEGAL_RETURN_VALUE_VOID_CONSTRUCTOR_DESTRUCTOR);
                    expression();
                }

                stmt = CFunc_AppendStatement(ST_RETURN);
                stmt->expr = NULL;
                CError_ResetErrorSkip();
                tk = lex();
                return;
            }

            if (tk == ';') {
                if (thing->thetype != &stvoid && (copts.pedantic || copts.cplusplus)) {
                    CError_Warning(ERR_RETURN_VALUE_EXPECTED);
                }

                stmt = CFunc_AppendStatement(ST_RETURN);
                stmt->expr = NULL;
                CError_ResetErrorSkip();
                tk = lex();
                return;
            }

            if (!copts.old_argmatch) {
                expr = s_expression();
            } else {
                expr = expression();
            }

            if (thing->thetype == &stvoid) {
                if (expr->rtype != &stvoid) {
                    CError_Error(ERR_ILLEGAL_RETURN_VALUE_VOID_CONSTRUCTOR_DESTRUCTOR);
                }

                stmt = CFunc_AppendStatement(ST_EXPRESSION);
                stmt->expr = expr;

                stmt = CFunc_AppendStatement(ST_RETURN);
                stmt->expr = NULL;
            } else {
                if (CMach_GetFunctionResultClass(TYPE_FUNC(cscope_currentfunc->type)) == 1) {
                    expr = returnstatementadjust(expr, thing->thetype, thing->qual);
                } else {
                    expr = CExpr_AssignmentPromotion(expr, thing->thetype, thing->qual, TRUE);
                }

                stmt = CFunc_AppendStatement(ST_RETURN);
                stmt->expr = expr;

                if (IS_TYPE_POINTER_ONLY(thing->thetype)) {
                    CFunc_AutoResultCheck(expr);
                }
            }
            break;

        case TK_CASE:
            scancase(thing);
            return;

        case TK_DEFAULT:
            if (!thing->switchinfo) {
                CError_Error(ERR_ILLEGAL_USE_KEYWORD);
                return;
            }

            if (lex() != ':') {
                CError_ErrorSkip(ERR_COLON_EXPECTED);
            } else {
                tk = lex();
            }

            if (thing->switchinfo->defaultlabel) {
                CError_ErrorSkip(ERR_DEFAULT_LABEL_DEFINED_MORE_THAN_ONCE);
            }

            stmt = CFunc_AppendStatement(ST_LABEL);
            stmt->label = newlabel();
            stmt->label->stmt = stmt;
            thing->switchinfo->defaultlabel = stmt->label;
            statement(thing);
            return;

        case TK_SWITCH:
            if (lex() != '(') {
                CError_ErrorSkip(ERR_LPAREN_EXPECTED);
            } else {
                tk = lex();
            }

            if (copts.cplusplus && !copts.ARMscoping && isdeclaration(1, 0, 0, '=')) {
                block = CFunc_NewDeclBlock();
                expr = CFunc_ParseLocalDeclarationList(FALSE, TRUE, FALSE, FALSE);
                if (CScope_IsEmptyNameSpace(cscope_current)) {
                    CFunc_RestoreBlock(block);
                    block = NULL;
                }
            } else {
                expr = expression();
                block = NULL;
            }

            stmt = CFunc_AppendStatement(ST_SWITCH);
            stmt->expr = CExpr_ConvertToIntegral(expr);

            if (tk != ')') {
                CError_ErrorSkip(ERR_RPAREN_EXPECTED);
            } else {
                tk = lex();
            }

            stmt->label = (CLabel*)lalloc(sizeof(SwitchInfo));
            ((SwitchInfo*)stmt->label)->defaultlabel = NULL;
            ((SwitchInfo*)stmt->label)->cases = NULL;
            ((SwitchInfo*)stmt->label)->exprtype = stmt->expr->rtype;

            label = newlabel();
            subthing = *thing;
            subthing.loopBreak = label;
            subthing.switchinfo = (SwitchInfo*)stmt->label;
            CFunc_CompoundStatement(&subthing);

            if (!subthing.switchinfo->defaultlabel) {
                subthing.switchinfo->defaultlabel = label;
            }

            if (!subthing.switchinfo->cases) {
                stmt->type = ST_EXPRESSION;
                stmt2 = lalloc(sizeof(Statement));
                *stmt2 = *stmt;
                stmt->next = stmt2;
                stmt2->type = ST_GOTO;
                stmt2->label = subthing.switchinfo->defaultlabel;
                stmt2->dobjstack = cexcept_dobjstack;
            }

            stmt = CFunc_AppendStatement(ST_LABEL);
            stmt->label = label;
            label->stmt = stmt;

            if (block) {
                CFunc_RestoreBlock(block);
            }
            return;

        case TK_GOTO:
            if ((tk = lex()) != TK_IDENTIFIER) {
                if (tk == '*' && !copts.ANSIstrict) {
                    tk = lex();
                    stmt = CFunc_AppendStatement(ST_GOTOEXPR);
                    stmt->expr = expression();
                    if (!IS_TYPE_POINTER_ONLY(stmt->expr->rtype)) {
                        CError_Error(ERR_ILLEGAL_TYPE);
                        stmt->expr = nullnode();
                        stmt->expr->rtype = TYPE(&void_ptr);
                    }
                } else {
                    CError_Error(ERR_IDENTIFIER_EXPECTED);
                    return;
                }
            } else {
                stmt = CFunc_AppendStatement(ST_GOTO);
                if (!(stmt->label = findlabel())) {
                    stmt->label = newlabel();
                    stmt->label->next = Labels;
                    Labels = stmt->label;
                    stmt->label->name = tkidentifier;
                }
                tk = lex();
            }
            break;

        case TK_BREAK:
            if (thing->loopBreak) {
                stmt = CFunc_AppendStatement(ST_GOTO);
                stmt->label = thing->loopBreak;
            } else {
                CError_Error(ERR_ILLEGAL_USE_KEYWORD);
            }
            tk = lex();
            break;

        case TK_CONTINUE:
            if (thing->loopContinue) {
                stmt = CFunc_AppendStatement(ST_GOTO);
                stmt->label = thing->loopContinue;
            } else {
                CError_Error(ERR_ILLEGAL_USE_KEYWORD);
            }
            tk = lex();
            break;

        case TK_FOR: {
            CLabel* forLabel1;
            CLabel* forLabel2;
            CLabel* forLabel3;
            ENode* forCond;
            ENode* forInc;

            if (lex() != '(') {
                CError_ErrorSkip(ERR_LPAREN_EXPECTED);
            } else {
                tk = lex();
            }

            block = NULL;
            if (tk != ';') {
                if (!copts.cplusplus || !isdeclaration(1, 0, 0, 0)) {
                    expr = expression();
                    CExpr_CheckUnusedExpression(expr);
                } else {
                    if (!copts.ARMscoping) {
                        block = CFunc_NewDeclBlock();
                    }
                    expr = CFunc_ParseLocalDeclarationList(FALSE, TRUE, TRUE, FALSE);
                    if (block && CScope_IsEmptyNameSpace(cscope_current)) {
                        CFunc_RestoreBlock(block);
                        block = NULL;
                    }
                }

                if (expr) {
                    stmt = CFunc_AppendStatement(ST_EXPRESSION);
                    stmt->expr = expr;
                }

                if (tk == ';') {
                    CError_ResetErrorSkip();
                } else {
                    CError_Error(ERR_SEMICOLON_EXPECTED);
                }
            } else {
                CError_ResetErrorSkip();
            }

            if ((tk = lex()) != ';') {
                if (copts.cplusplus && !copts.ARMscoping && isdeclaration(1, 0, 0, '=')) {
                    if (!block) {
                        block = CFunc_NewDeclBlock();
                    }
                    expr = CFunc_ParseLocalDeclarationList(FALSE, TRUE, FALSE, FALSE);
                    if (CScope_IsEmptyNameSpace(cscope_current)) {
                        CFunc_RestoreBlock(block);
                        block = NULL;
                    }
                } else {
                    expr = expression();
                    CExpr_CheckUnwantedAssignment(expr);
                }

                forCond = CExpr_ConvertToCondition(expr);

                if (tk == ';') {
                    CError_ResetErrorSkip();
                } else {
                    CError_Error(ERR_SEMICOLON_EXPECTED);
                }
            } else {
                CError_ResetErrorSkip();
                forCond = NULL;
            }

            if ((tk = lex()) != ')') {
                forInc = expression();
                CExpr_CheckUnusedExpression(forInc);
                if (tk == ')') {
                    CError_ResetErrorSkip();
                } else {
                    CError_Error(ERR_RPAREN_EXPECTED);
                }
            } else {
                CError_ResetErrorSkip();
                forInc = NULL;
            }

            if (copts.warn_possunwant) {
                spaceskip = FALSE;
                if ((tk = lex()) == ';' && !spaceskip) {
                    CError_Warning(ERR_POSSIBLE_UNWANTED_SEMICOLON);
                }
            } else {
                tk = lex();
            }

            if (forCond) {
                stmt = CFunc_AppendStatement(ST_GOTO);
                stmt->label = newlabel();
                label = stmt->label;
            } else {
                label = newlabel();
            }

            CFunc_LoopIncrement();

            stmt = CFunc_AppendStatement(ST_LABEL);
            stmt->label = newlabel();
            (forLabel1 = stmt->label)->stmt = stmt;

            forLabel2 = newlabel();
            forLabel3 = newlabel();

            subthing = *thing;
            subthing.loopContinue = forLabel3;
            subthing.loopBreak = forLabel2;

            if (tk != '{') {
                DeclBlock* b = CFunc_NewDeclBlock();
                CFunc_CompoundStatement(&subthing);
                CFunc_RestoreBlock(b);
            } else {
                CFunc_CompoundStatement(&subthing);
            }

            stmt = CFunc_AppendStatement(ST_LABEL);
            stmt->label = forLabel3;
            forLabel3->stmt = stmt;

            if (forInc) {
                stmt = CFunc_AppendStatement(ST_EXPRESSION);
                stmt->expr = forInc;
            }

            stmt = CFunc_AppendStatement(ST_LABEL);
            stmt->label = label;
            label->stmt = stmt;

            ifstatement(TRUE, forCond, forLabel1, TRUE);
            CFunc_LoopDecrement();

            stmt = CFunc_AppendStatement(ST_LABEL);
            stmt->label = forLabel2;
            forLabel2->stmt = stmt;

            if (block) {
                CFunc_RestoreBlock(block);
            }
            return;
        }

        case TK_DO: {
            CLabel* label1;
            CLabel* label2;
            CLabel* label3;

            CFunc_LoopIncrement();
            stmt = CFunc_AppendStatement(ST_LABEL);
            stmt->label = newlabel();
            (label1 = stmt->label)->stmt = stmt;

            label2 = newlabel();
            label3 = newlabel();

            subthing = *thing;
            subthing.loopContinue = label2;
            subthing.loopBreak = label3;

            tk = lex();
            CFunc_CompoundStatement(&subthing);

            stmt = CFunc_AppendStatement(ST_LABEL);
            stmt->label = label2;
            label2->stmt = stmt;

            if (tk != TK_WHILE) {
                CError_Error(ERR_ILLEGAL_TOKEN);
            }

            if (lex() != '(') {
                CError_ErrorSkip(ERR_LPAREN_EXPECTED);
            } else {
                tk = lex();
            }

            expr = CExpr_ConvertToCondition(expression());
            CExpr_CheckUnwantedAssignment(expr);

            if (tk != ')') {
                CError_ErrorSkip(ERR_RPAREN_EXPECTED);
            } else {
                tk = lex();
            }

            ifstatement(TRUE, expr, label1, TRUE);

            CFunc_LoopDecrement();

            stmt = CFunc_AppendStatement(ST_LABEL);
            stmt->label = label3;
            label3->stmt = stmt;
            break;
        }

        case TK_WHILE: {
            CLabel* label1;
            CLabel* label2;
            CLabel* label3;

            if ((tk = lex()) != '(') {
                CError_ErrorSkip(ERR_LPAREN_EXPECTED);
            } else {
                tk = lex();
            }

            if (copts.cplusplus && !copts.ARMscoping && isdeclaration(1, 0, 0, '=')) {
                block = CFunc_NewDeclBlock();
                expr = CFunc_ParseLocalDeclarationList(FALSE, TRUE, FALSE, FALSE);
                if (CScope_IsEmptyNameSpace(cscope_current)) {
                    CFunc_RestoreBlock(block);
                    block = NULL;
                }
            } else {
                expr = expression();
                block = NULL;
                CExpr_CheckUnwantedAssignment(expr);
            }
            expr = CExpr_ConvertToCondition(expr);

            if (tk != ')') {
                CError_ErrorSkip(ERR_RPAREN_EXPECTED);
            } else {
                if (copts.warn_possunwant) {
                    spaceskip = FALSE;
                    if ((tk = lex()) == ';' && !spaceskip) {
                        CError_Warning(ERR_POSSIBLE_UNWANTED_SEMICOLON);
                    }
                } else {
                    tk = lex();
                }
            }

            stmt = CFunc_AppendStatement(ST_GOTO);
            stmt->label = newlabel();
            label1 = stmt->label;

            CFunc_LoopIncrement();

            stmt = CFunc_AppendStatement(ST_LABEL);
            stmt->label = newlabel();
            (label2 = stmt->label)->stmt = stmt;

            label3 = newlabel();

            subthing = *thing;
            subthing.loopContinue = label1;
            subthing.loopBreak = label3;

            CFunc_CompoundStatement(&subthing);

            stmt = CFunc_AppendStatement(ST_LABEL);
            stmt->label = label1;
            label1->stmt = stmt;

            ifstatement(TRUE, expr, label2, TRUE);

            CFunc_LoopDecrement();

            stmt = CFunc_AppendStatement(ST_LABEL);
            stmt->label = label3;
            label3->stmt = stmt;

            if (block) {
                CFunc_RestoreBlock(block);
            }
            return;
        }

        case TK_IF: {
            CLabel* label1;

            if ((tk = lex()) != '(') {
                CError_ErrorSkip(ERR_LPAREN_EXPECTED);
            } else {
                tk = lex();
            }

            if (copts.cplusplus && !copts.ARMscoping && isdeclaration(1, 0, 0, '=')) {
                block = CFunc_NewDeclBlock();
                expr = CFunc_ParseLocalDeclarationList(FALSE, TRUE, FALSE, FALSE);
                if (CScope_IsEmptyNameSpace(cscope_current)) {
                    CFunc_RestoreBlock(block);
                    block = NULL;
                }
            } else {
                expr = expression();
                block = NULL;
                CExpr_CheckUnwantedAssignment(expr);
            }

            expr = CExpr_ConvertToCondition(expr);

            label1 = newlabel();
            ifstatement(FALSE, expr, label1, FALSE);

            if (tk != ')') {
                CError_ErrorSkip(ERR_RPAREN_EXPECTED);
            } else {
                if (copts.warn_possunwant) {
                    spaceskip = FALSE;
                    if ((tk = lex()) == ';' && !spaceskip) {
                        CError_Warning(ERR_POSSIBLE_UNWANTED_SEMICOLON);
                    }
                } else {
                    tk = lex();
                }
            }

            CFunc_CompoundStatement(thing);

            if (tk == TK_ELSE) {
                if (copts.warn_possunwant) {
                    spaceskip = FALSE;
                    if ((tk = lex()) == ';' && !spaceskip) {
                        CError_Warning(ERR_POSSIBLE_UNWANTED_SEMICOLON);
                    }
                } else {
                    tk = lex();
                }

                stmt = CFunc_AppendStatement(ST_GOTO);
                stmt->label = newlabel();
                label = stmt->label;

                stmt = CFunc_AppendStatement(ST_LABEL);
                stmt->label = label1;
                label1->stmt = stmt;

                CFunc_CompoundStatement(thing);

                label1 = label;
            }

            stmt = CFunc_AppendStatement(ST_LABEL);
            stmt->label = label1;
            label1->stmt = stmt;

            if (block) {
                CFunc_RestoreBlock(block);
            }
            return;
        }

        case '{':
            CFunc_CompoundStatement(thing);
            return;

        case TK_ASM:
            if (copts.cplusplus || !copts.ANSIstrict) {
                tk = lex();
                volatileasm = FALSE;

                if (tk == TK_VOLATILE ||
                    (tk == TK_IDENTIFIER && !strcmp(tkidentifier->name, "__volatile__"))) {
                    tk = lex();
                    volatileasm = TRUE;
                }

                if (tk == '(') {
                    InlineAsm_Assemble();
                    if (tk == ')') {
                        tk = lex();
                        break;
                    } else {
                        CError_Error(ERR_RPAREN_EXPECTED);
                        return;
                    }
                }

                if (tk == '{') {
                    InlineAsm_Assemble();
                    if (tk != '}') {
                        CError_Error(ERR_RBRACE_EXPECTED);
                        return;
                    }
                    if ((tk = lex()) == ';') {
                        tk = lex();
                    }
                    CError_ResetErrorSkip();
                    return;
                }

                CError_Error(ERR_LPAREN_EXPECTED);
                return;
            }
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            return;

        case TK_TRY:
            tk = lex();
            CExcept_ScanTryBlock(thing, FALSE);
            return;

        case TK_USING:
            if ((tk = lex()) == TK_NAMESPACE) {
                tk = lex();
                CScope_ParseUsingDirective(cscope_current);
            } else {
                CScope_ParseUsingDeclaration(cscope_current, ACCESSPUBLIC, FALSE);
            }
            return;

        case TK_NAMESPACE:
            if ((tk = lex()) != TK_IDENTIFIER) {
                CError_Error(ERR_IDENTIFIER_EXPECTED);
                return;
            }
            name = tkidentifier;
            if ((tk = lex()) != '=') {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                return;
            }

            CScope_ParseNameSpaceAlias(name);
            break;

        case ';':
            break;

        case TK_IDENTIFIER:
            if (checklabel()) {
                stmt = CFunc_AppendStatement(ST_LABEL);
                if ((stmt->label = findlabel())) {
                    if (stmt->label->stmt) {
                        CError_Error(ERR_LABEL_REDEFINED, tkidentifier->name);
                    }
                } else {
                    stmt->label = newlabel();
                    stmt->label->next = Labels;
                    Labels = stmt->label;
                    stmt->label->name = tkidentifier;
                }

                stmt->label->stmt = stmt;
                tk = lex();
                tk = lex();
                statement(thing);
                return;
            }
            tk = TK_IDENTIFIER;

        default:
            if (copts.cplusplus && isdeclaration(1, 0, 0, 0)) {
                CFunc_ParseLocalDeclarationList(FALSE, FALSE, FALSE, TRUE);
                tk = lex();
                return;
            }

            stmt = CFunc_AppendStatement(ST_EXPRESSION);
            stmt->expr = expression();
            CExpr_CheckUnusedExpression(stmt->expr);
    }

    if (tk == ';') {
        CPrep_TokenStreamFlush();
        tk = lex();
        CError_ResetErrorSkip();
    } else {
        CError_ErrorSkip(ERR_SEMICOLON_EXPECTED);
    }
}

void CFunc_CompoundStatement(DeclThing* thing) {
    DeclBlock* block;

    block = CFunc_NewDeclBlock();

    if (tk == '{') {
        tk = lex();
        if (!copts.cplusplus && isdeclaration(0, 0, 0, 0)) {
            CFunc_ParseLocalDeclarationList(FALSE, FALSE, FALSE, FALSE);
        }

        while (tk != '}') {
            statement(thing);
        }

        CPrep_GetFileOffsetInfo2(&cparser_fileoffset, &sourceoffset, &sourcefilepath);
        tk = lex();
    } else {
        statement(thing);
    }

    CFunc_RestoreBlock(block);
}

static void CFunc_InsertArgumentCopyConversion(
    Object* obj, Type* type1, Type* type2, Boolean byPointer
) {
    Object* newobj;
    Statement* stmt;
    ENode* expr;
    NameSpaceObjectList* nsol;
    ObjectList* list;

    newobj = lalloc(sizeof(Object));
    *newobj = *obj;
    newobj->type = type2;

    CFunc_SetupLocalVarInfo(newobj);

    obj->name = CParser_GetUniqueName();
    if (byPointer) {
        obj->type = CDecl_NewPointerType(type1);
    } else {
        obj->type = type1;
    }

    CError_ASSERT(
        2527,
        (nsol = CScope_FindName(cscope_current, newobj->name)) && nsol->object == OBJ_BASE(obj)
    );
    nsol->object = OBJ_BASE(newobj);

    list = lalloc(sizeof(ObjectList));
    list->object = newobj;
    list->next = locals;
    locals = list;

    stmt = CFunc_AppendStatement(ST_EXPRESSION);
    expr = create_objectnode(obj);
    if (byPointer) {
        expr->rtype = CDecl_NewPointerType(type1);
        expr = makemonadicnode(expr, EINDIRECT);
    }
    expr->rtype = type1;

    if (type1 != type2) {
        expr = promote(expr, type2);
    }

    stmt->expr = makediadicnode(create_objectnode(newobj), expr, EASS);
}

static void CFunc_AdjustOldStyleArgs(void) {
    ObjectList* list;

    for (list = arguments; list; list = list->next) {
        if (IS_TYPE_FLOAT(list->object->type) && list->object->type->size < stdouble.size) {
            CFunc_InsertArgumentCopyConversion(
                list->object, TYPE(&stdouble), list->object->type, FALSE
            );
        }
        if (CMach_PassResultInHiddenArg(list->object->type)) {
            list->object->type = CDecl_NewPointerType(list->object->type);
            TPTR_QUAL(list->object->type) = Q_REFERENCE;
        }
    }
}

void CFunc_SetupNewFuncArgs(Object* func, FuncArg* args) {
    Object* obj;
    ObjectList* newlist;

    arguments = NULL;

    if (args != &elipsis && args != &oldstyle) {
        newlist = NULL;

        while (args && args != &elipsis) {
            IsCompleteType(args->type);

            obj = CParser_NewLocalDataObject(NULL, FALSE);
            if (!args->name) {
                obj->name = no_name_node;
            } else {
                obj->name = args->name;
            }
            obj->type = args->type;
            obj->qual = args->qual;
            obj->sclass = args->sclass;

            CFunc_SetupLocalVarInfo(obj);

            if ((IS_TYPE_CLASS(obj->type) && CClass_ReferenceArgument(TYPE_CLASS(obj->type))) ||
                CMach_PassResultInHiddenArg(obj->type)) {
                obj->type = CDecl_NewPointerType(obj->type);
                TPTR_QUAL(obj->type) = Q_REFERENCE | Q_RESTRICT;
            }

            if (obj->name == no_name_node &&
                copts.ANSIstrict &&
                !copts.cplusplus &&
                !(func->qual & Q_MANGLE_NAME)) {
                CError_Error(ERR_ILLEGAL_FUNCTION_DEFINITION);
            }

            if (newlist) {
                newlist->next = lalloc(sizeof(ObjectList));
                newlist = newlist->next;
            } else {
                newlist = lalloc(sizeof(ObjectList));
                arguments = newlist;
            }

            newlist->next = NULL;
            newlist->object = obj;

            args = args->next;
        }
    }
}

static ObjectList* CFunc_CopyObjectList(const FuncArg* args) {
    ObjectList* list;
    ObjectList* last;
    Object* obj;

    list = NULL;

    while (args) {
        if (list) {
            last->next = lalloc(sizeof(ObjectList));
            last = last->next;
        } else {
            last = lalloc(sizeof(ObjectList));
            list = last;
        }

        obj = CParser_NewLocalDataObject(NULL, FALSE);
        obj->name = args->name;
        obj->type = args->type;
        obj->qual = args->qual;
        obj->sclass = args->sclass;
        CFunc_SetupLocalVarInfo(obj);

        last->object = obj;
        last->next = NULL;

        args = args->next;
    }

    return list;
}

static void SetupFunctionArguments(Object* func, DeclInfo* di, Statement* firstStmt) {
    ObjectList* list;
    Object* resultobj;
    Object* obj;
    Type* type;
    DeclInfo my_di;

    if (TYPE_FUNC(func->type)->args) {
        if (di->oldStyleParameters) {
            arguments = CFunc_CopyObjectList(di->parameterNames);
            while (1) {
                if (tk == '{') {
                    break;
                }

                memclrw(&my_di, sizeof(my_di));
                CParser_GetDeclSpecs(&my_di, FALSE);

                type = my_di.thetype;
                if (my_di.storageclass && my_di.storageclass != TK_REGISTER) {
                    CError_Error(ERR_ILLEGAL_FUNCTION_DEFINITION);
                }

                while (1) {
                    my_di.thetype = type;
                    my_di.name = NULL;
                    scandeclarator(&my_di);
                    if (!my_di.name) {
                        CError_Error(ERR_IDENTIFIER_EXPECTED);
                        break;
                    }

                    adjustargumenttype(&my_di);
                    IsCompleteType(my_di.thetype);

                    if ((obj = CFunc_IsInObjList(arguments, my_di.name))) {
                        if (obj->type) {
                            CError_Error(ERR_OBJECT_REDEFINED, obj);
                        }
                        obj->type = my_di.thetype;
                        obj->sclass = my_di.storageclass;
                        obj->qual = my_di.qual;
                    } else {
                        CError_Error(ERR_ILLEGAL_FUNCTION_DEFINITION);
                    }

                    if (tk != ',') {
                        break;
                    }
                    tk = lex();
                }

                if (tk != ';') {
                    CError_ErrorSkip(ERR_SEMICOLON_EXPECTED);
                } else {
                    tk = lex();
                }
            }

            for (list = arguments; list; list = list->next) {
                if (!list->object->type) {
                    list->object->type = TYPE(&stsignedint);
                }
            }
        } else {
            CFunc_SetupNewFuncArgs(func, TYPE_FUNC(func->type)->args);
        }
    }

    if (CMach_GetFunctionResultClass(TYPE_FUNC(func->type)) == 1) {
        resultobj = CParser_NewLocalDataObject(NULL, FALSE);
        resultobj->name = temp_argument_name;
        resultobj->type = CDecl_NewPointerType(TYPE_FUNC(func->type)->functype);
        CFunc_SetupLocalVarInfo(resultobj);

        list = lalloc(sizeof(ObjectList));
        list->object = resultobj;
        if (CABI_GetStructResultArgumentIndex(TYPE_FUNC(func->type))) {
            CError_ASSERT(2797, arguments);
            list->next = arguments->next;
            arguments->next = list;
        } else {
            list->next = arguments;
            arguments = list;
        }
    }

    for (list = arguments; list; list = list->next) {
        NameSpaceObjectList* nsol = CScope_InsertName(cscope_current, list->object->name);
        nsol->object = OBJ_BASE(list->object);
    }
}

NameSpace* CFunc_FuncGenSetup(Statement* stmt, Object* func) {
    NameSpace* nspace;
    DeclBlock* block;

    nspace = CScope_NewListNameSpace(NULL, FALSE);
    nspace->parent = cscope_current;
    cscope_current = nspace;

    arguments = NULL;
    locals = NULL;
    Labels = NULL;

    localcount = 0;
    cfunc_staticvarcount = 0;

    CExcept_Setup();

    memclrw(stmt, sizeof(Statement));
    curstmt = stmt;
    stmt->type = ST_NOP;
    stmt->value = curstmtvalue = 1;

    blockcount = 0;
    block = lalloc(sizeof(DeclBlock));
    memclrw(block, sizeof(DeclBlock));
    block->index = blockcount++;
    block->parent_nspace = cscope_current;

    currentblock = firstblock = block;

    return nspace;
}

CFuncSave* CFunc_GetGlobalCompilerState(void) {
    CFuncSave* state;

    if (!cscope_currentfunc && !cscope_currentclass && cscope_current == cscope_root) {
        return NULL;
    }

    locklheap();
    state = lalloc(sizeof(CFuncSave));

    CScope_SetNameSpaceScope(cscope_root, &state->scope);

    state->arguments = arguments;
    arguments = NULL;

    state->locals = locals;
    locals = NULL;

    state->labels = Labels;
    Labels = NULL;

    state->curstmt = curstmt;
    curstmt = NULL;

    state->firstblock = firstblock;
    firstblock = NULL;

    state->currentblock = currentblock;
    currentblock = NULL;

    state->cexcept_dobjstack = cexcept_dobjstack;
    cexcept_dobjstack = NULL;

    state->sinit_label = sinit_label;
    sinit_label = NULL;

    state->ainit_expr = ainit_expr;
    ainit_expr = NULL;

    state->ctor_chain = ctor_chain;
    ctor_chain = NULL;

    state->cinit_tempnodefunc = cinit_tempnodefunc;
    cinit_tempnodefunc = NULL;

    state->trychain = trychain;
    trychain = NULL;

    state->cparser_fileoffset = cparser_fileoffset;
    state->symdecltoken = symdecltoken;

    state->functionbodyoffset = functionbodyoffset;
    functionbodyoffset = 0;

    state->functionbodypath = functionbodypath;
    functionbodypath = NULL;

    state->symdecloffset = symdecloffset;
    symdecloffset = 0;

    state->symdeclend = symdeclend;

    state->sourceoffset = sourceoffset;
    sourceoffset = 0;

    state->sourcefilepath = sourcefilepath;
    sourcefilepath = NULL;

    state->curstmtvalue = curstmtvalue;
    curstmtvalue = 0;

    state->name_obj_check = name_obj_check;
    name_obj_check = NULL;

    state->check_arglist = check_arglist;
    check_arglist = NULL;

    state->blockcount = blockcount;
    blockcount = 0;

    state->localcount = localcount;
    localcount = 0;

    state->tk = tk;
    state->tkidentifier = tkidentifier;

    state->global_access = global_access;

    state->cexcept_hasdobjects = cexcept_hasdobjects;
    cexcept_hasdobjects = FALSE;

    state->sinit_first_object = sinit_first_object;
    sinit_first_object = NULL;

    state->ainit_only_one = ainit_only_one;
    ainit_only_one = FALSE;

    state->cfunc_is_extern_c = cfunc_is_extern_c;
    cfunc_is_extern_c = FALSE;

    state->temp_reference_init = temp_reference_init;

    return state;
}

void CFunc_SetGlobalCompilerState(CFuncSave* state) {
    if (state) {
        CScope_RestoreScope(&state->scope);

        arguments = state->arguments;
        locals = state->locals;
        Labels = state->labels;
        curstmt = state->curstmt;
        firstblock = state->firstblock;
        currentblock = state->currentblock;
        cexcept_dobjstack = state->cexcept_dobjstack;
        sinit_label = state->sinit_label;
        ainit_expr = state->ainit_expr;
        ctor_chain = state->ctor_chain;
        cinit_tempnodefunc = state->cinit_tempnodefunc;
        trychain = state->trychain;
        name_obj_check = state->name_obj_check;
        check_arglist = state->check_arglist;
        curstmtvalue = state->curstmtvalue;
        cparser_fileoffset = state->cparser_fileoffset;
        symdecltoken = state->symdecltoken;
        functionbodyoffset = state->functionbodyoffset;
        functionbodypath = state->functionbodypath;
        symdecloffset = state->symdecloffset;
        symdeclend = state->symdeclend;
        sourceoffset = state->sourceoffset;
        sourcefilepath = state->sourcefilepath;
        blockcount = state->blockcount;
        tk = state->tk;
        tkidentifier = state->tkidentifier;
        global_access = state->global_access;
        localcount = state->localcount;
        cexcept_hasdobjects = state->cexcept_hasdobjects;
        sinit_first_object = state->sinit_first_object;
        ainit_only_one = state->ainit_only_one;
        cfunc_is_extern_c = state->cfunc_is_extern_c;
        temp_reference_init = state->temp_reference_init;

        unlocklheap();
    }
}

void CFunc_Gen(Statement* stmt, Object* func, UInt8 oldStyleParameters) {
    Boolean isCovariant;
    CI_FuncData packed;

    if ((TYPE_FUNC(func->type)->flags & FUNC_COVARIANT) && !anyerrors) {
        CInline_PackIFunctionData(&packed, stmt, func);
        isCovariant = TRUE;
    } else {
        isCovariant = FALSE;
    }

    CInline_GenFunc(stmt, func, oldStyleParameters);

    if (isCovariant) {
        CClass_DefineCovariantFuncs(func, &packed);
    }
}

static void CFunc_CheckCtorInitializer(TypeClass* tclass, CtorChain* chain) {
    ObjMemberVar* ivar;
    CtorChain* scan;

    if (tclass->mode == CLASS_MODE_UNION) {
        return;
    }

    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
        if (IS_TYPE_REFERENCE(ivar->type)) {
            for (scan = chain; scan; scan = scan->next) {
                if (scan->what == CtorChain_MemberVar && scan->u.membervar == ivar) {
                    break;
                }
            }

            if (!scan) {
                CError_Error(ERR_AMPERSAND_REFERENCE_MEMBER_NOT_INITIALIZED, ivar->name->name);
            }
        } else if (CParser_IsConst(ivar->type, ivar->qual)) {
            for (scan = chain; scan; scan = scan->next) {
                if (scan->what == CtorChain_MemberVar && scan->u.membervar == ivar) {
                    break;
                }
            }

            if (!scan && !IS_TYPE_CLASS(ivar->type)) {
                CError_Error(ERR_CONST_MEMBER_NOT_INITIALIZED, ivar->name->name);
            }
        }
    }
}

void CFunc_CheckClassCtors(TypeClass* tclass) {
    CFunc_CheckCtorInitializer(tclass, NULL);
}

static void CFunc_ParseCtorInitializer(void) {
    Type* type;
    ClassList* base;
    TypeClass* tclass;
    VClassList* vbase;
    ENodeList* args;
    CtorChain* chain;
    CtorChain* previous;
    ENode* expr;
    ObjMemberVar* ivar;

    ctor_chain = NULL;
    if (tk != ':') {
        return;
    }
    do {
        tclass = NULL;
        tk = lex();
        switch (tk) {
            case TK_IDENTIFIER:
                for (ivar = cscope_currentclass->ivars; ivar != NULL; ivar = ivar->next) {
                    if (ivar->name == tkidentifier) {
                        if (lookahead() == '(') {
                            goto do_ivar;
                        }
                    }
                }
                for (base = cscope_currentclass->bases; base != NULL; base = base->next) {
                    if (base->base->classname == tkidentifier) {
                        if (lookahead() == '(') {
                            tclass = base->base;
                            tk = lex();
                        } else {
                            tkidentifier = base->base->classname;
                        }
                        break;
                    }
                }
                break;
            case TK_COLON_COLON:
                break;
            default:
                CError_Error(ERR_ILLEGAL_CTOR_INITIALIZER);
                return;
        }
        if (tclass == NULL) {
            tclass = CClass_GetQualifiedClass();
        }
        if (tclass != NULL) {
            for (vbase = cscope_currentclass->vbases; vbase != NULL; vbase = vbase->next) {
                if (vbase->base == tclass) {
                    break;
                }
            }
            if (vbase != NULL) {
                for (previous = ctor_chain; previous != NULL; previous = previous->next) {
                    if (previous->what == CtorChain_VBase && previous->u.vbase == vbase) {
                        CError_Error(ERR_ILLEGAL_CTOR_INITIALIZER);
                        return;
                    }
                }
                chain = lalloc(sizeof(CtorChain));
                chain->what = CtorChain_VBase;
                chain->u.vbase = vbase;
            } else {
                for (base = cscope_currentclass->bases; base != NULL; base = base->next) {
                    if (base->base == tclass) {
                        break;
                    }
                }
                if (base != NULL) {
                    for (previous = ctor_chain; previous != NULL; previous = previous->next) {
                        if (previous->what == CtorChain_Base && previous->u.base == base) {
                            CError_Error(ERR_ILLEGAL_CTOR_INITIALIZER);
                            return;
                        }
                    }
                    chain = lalloc(sizeof(CtorChain));
                    chain->what = CtorChain_Base;
                    chain->u.base = base;
                } else {
                    CError_Error(ERR_ILLEGAL_CTOR_INITIALIZER);
                    return;
                }
            }
        } else {
            for (ivar = cscope_currentclass->ivars; ivar != NULL; ivar = ivar->next) {
                if (ivar->name == tkidentifier) {
                    break;
                }
            }
            if (ivar != NULL) {
do_ivar:
                for (previous = ctor_chain; previous != NULL; previous = previous->next) {
                    if (previous->what == CtorChain_MemberVar && previous->u.membervar == ivar) {
                        CError_Error(ERR_ILLEGAL_CTOR_INITIALIZER);
                    }
                }
                chain = lalloc(sizeof(CtorChain));
                chain->what = CtorChain_MemberVar;
                chain->u.membervar = ivar;
            } else {
                CError_Error(ERR_ILLEGAL_CTOR_INITIALIZER);
                return;
            }
            tk = lex();
        }
        if (tk != '(') {
            CError_Error(ERR_LPAREN_EXPECTED);
            return;
        }
        tk = lex();
        args = CExpr_ScanExpressionList(TRUE);
        if (tk != ')') {
            CError_Error(ERR_RPAREN_EXPECTED);
            return;
        }
        switch (chain->what) {
            case CtorChain_Base:
                expr = CABI_MakeThisExpr(NULL, chain->u.base->offset);
                chain->objexpr = CExpr_ConstructObject(
                    chain->u.base->base, expr, args, TRUE, FALSE, FALSE, FALSE, TRUE
                );
                break;
            case CtorChain_VBase:
                expr = CABI_MakeThisExpr(chain->u.vbase->base, chain->u.vbase->offset);
                chain->objexpr = CExpr_ConstructObject(
                    chain->u.vbase->base, expr, args, TRUE, FALSE, FALSE, FALSE, TRUE
                );
                break;
            case CtorChain_MemberVar:
                expr = CABI_MakeThisExpr(cscope_currentclass, chain->u.membervar->offset);
                expr->flags = chain->u.membervar->qual & ENODE_FLAG_QUALS;
                type = chain->u.membervar->type;
                switch (type->type) {
                    case TYPECLASS:
                        chain->objexpr = CExpr_ConstructObject(
                            TYPE_CLASS(chain->u.membervar->type),
                            expr,
                            args,
                            TRUE,
                            TRUE,
                            FALSE,
                            TRUE,
                            TRUE
                        );
                        break;
                    case TYPEARRAY:
                        if (args) {
                            CError_Error(ERR_ILLEGAL_CTOR_INITIALIZER);
                            continue;
                        }
                        chain->objexpr = NULL;
                        break;
                    default: {
                        TypeBitfield* bitfieldType;
                        ENode* lhs;
                        if (!args) {
                            args = lalloc(sizeof(ENodeList));
                            args->next = NULL;
                            args->node = CExpr_DoExplicitConversion(
                                chain->u.membervar->type, chain->u.membervar->qual, NULL
                            );
                        }
                        if (args->next != NULL) {
                            CError_Error(ERR_ILLEGAL_CTOR_INITIALIZER);
                            return;
                        }
                        lhs = makemonadicnode(expr, EINDIRECT);
                        lhs->rtype = chain->u.membervar->type;
                        if (lhs->rtype->type == TYPEBITFIELD) {
                            bitfieldType = (TypeBitfield*)lhs->rtype;
                            lhs->data.monadic = makemonadicnode(lhs->data.monadic, EBITFIELD);
                            lhs->data.monadic->rtype = TYPE(bitfieldType);
                            lhs->rtype = bitfieldType->bitfieldtype;
                        }
                        chain->objexpr = makediadicnode(
                            lhs,
                            CExpr_AssignmentPromotion(args->node, lhs->rtype, lhs->flags, TRUE),
                            EASS
                        );
                        break;
                    }
                }
                break;
            default:
                CError_FATAL(3286);
        }
        chain->next = ctor_chain;
        ctor_chain = chain;
    } while ((tk = lex()) == ',');
}

static void CFunc_FunctionRedefinedCheck(Object* func) {
    if (TYPE_FUNC(func->type)->flags & FUNC_AUTO_GENERATED) {
        CError_Error(ERR_OBJECT_REDEFINED, func);
    }

    if ((TYPE_FUNC(func->type)->flags & FUNC_DEFINED) && func->datatype != DINLINEFUNC) {
        CError_Error(ERR_OBJECT_REDEFINED, func);
    }

    TYPE_FUNC(func->type)->flags |= FUNC_DEFINED;
}

static Object* CFunc_DeclareFuncName(char* str, HashNameNode* name) {
    Object* obj;
    Object* aliasobj;
    DeclInfo di;

    memclrw(&di, sizeof(di));
    di.name = name;
    di.storageclass = TK_STATIC;
    di.qual = Q_CONST;
    di.thetype = CDecl_NewArrayType(TYPE(&stchar), strlen(str) + 1);

    obj = CParser_NewGlobalDataObject(&di);
    aliasobj = CParser_NewAliasObject(obj, 0);
    obj->nspace = cscope_root;
    obj->datatype = DDATA;
    CFunc_NameLocalStaticDataObject(obj, obj->name->name);

    return aliasobj;
}

void CFunc_ParseFuncDef(
    Object* func,
    DeclInfo* di,
    TypeClass* tclass,
    Boolean is_method,
    Boolean is_static,
    NameSpace* nspace
) {
    Statement* beforebody;
    Boolean has_try;
    Object* nameobj_func;
    Object* nameobj_FUNCTION;
    Object* nameobj_pretty;
    CScopeSave scope;
    Statement* laststmt;
    Statement firstStmt;
    DeclThing thing;
    char* prettyname;

    nameobj_func = NULL;
    nameobj_FUNCTION = NULL;
    nameobj_pretty = NULL;
    prettyname = NULL;

    CError_ASSERT(3373, func->type->type == TYPEFUNC);

    CFunc_FunctionRedefinedCheck(func);
    CParser_UpdateObject(func, di);

    if (!is_method) {
        CScope_SetFunctionScope(func, &scope);
        if (tclass) {
            cscope_current = tclass->nspace;
        }
    } else {
        CScope_SetMethodScope(func, tclass, is_static, &scope);
    }

    if (nspace) {
        cscope_current = nspace;
    }

    if (cscope_currentclass) {
        CClass_MemberDef(func, cscope_currentclass);
    }

    cfunc_is_extern_c = di->is_extern_c;

    CError_ASSERT(3392, func->type->type == TYPEFUNC);
    if (di->oldStyleParameters && (func->qual & Q_ASM)) {
        CError_Error(ERR_ILLEGAL_TYPE_QUALIFIERS);
    }

    if (cparamblkptr->precompile == 1 && !(func->qual & Q_INLINE)) {
        CError_ErrorTerm(ERR_ILLEGAL_USE_PRECOMPILED_HEADER);
    }

    if (di->isType) {
        CError_Error(ERR_ILLEGAL_FUNCTION_DEFINITION);
    }

    CFunc_FuncGenSetup(&firstStmt, func);
    if (TYPE_FUNC(func->type)->functype->type != TYPEVOID) {
        IsCompleteType(TYPE_FUNC(func->type)->functype);
    }

    SetupFunctionArguments(func, di, &firstStmt);

    beforebody = curstmt;
    CPrep_ResetFileInfo(cparser_fileoffset.file);
    CPrep_GetFileOffsetInfo2(&cparser_fileoffset, &sourceoffset, &sourcefilepath);
    firstStmt.sourceoffset = functionbodyoffset = sourceoffset;
    firstStmt.sourcefilepath = functionbodypath = sourcefilepath;

    if (di->oldStyleParameters) {
        CFunc_AdjustOldStyleArgs();
    }

    if (di->parameterScope) {
        CScope_MergeNameSpace(cscope_current, di->parameterScope);
    }

    if (tk == TK_TRY) {
        tk = lex();
        has_try = TRUE;
    } else {
        has_try = FALSE;
    }

    if (CClass_IsConstructor(func)) {
        CError_ASSERT(3445, cscope_currentclass);
        CFunc_ParseCtorInitializer();
        CFunc_CheckCtorInitializer(cscope_currentclass, ctor_chain);
    }

    CPrep_TokenStreamFlush();

    if (!(func->qual & Q_ASM)) {
        if (tk == '{') {
            if (!has_try) {
                tk = lex();
            }
        } else {
            CError_ErrorSkip(ERR_LBRACE_EXPECTED);
            has_try = FALSE;
        }

        if (func->name) {
            nameobj_func = CFunc_DeclareFuncName(func->name->name, GetHashNameNode("__func__"));
            nameobj_FUNCTION =
                CFunc_DeclareFuncName(func->name->name, GetHashNameNode("__FUNCTION__"));
            prettyname = CError_GetObjectName(func);
            nameobj_pretty =
                CFunc_DeclareFuncName(prettyname, GetHashNameNode("__PRETTY_FUNCTION__"));
        }

        if (!copts.cplusplus) {
            CFunc_ParseLocalDeclarationList(FALSE, FALSE, FALSE, FALSE);
        }

        thing.switchinfo = NULL;
        thing.loopContinue = NULL;
        thing.loopBreak = NULL;
        thing.thetype = TYPE_FUNC(func->type)->functype;
        thing.qual = TYPE_FUNC(func->type)->qual;

        if (has_try) {
            CExcept_ScanTryBlock(&thing, CClass_IsConstructor(func) || CClass_IsDestructor(func));
            if (tk != 0) {
                CPrep_UnLex();
            }
            tk = '}';
        } else {
            while (tk != '}') {
                statement(&thing);
            }
        }

        if (curstmt->type != ST_RETURN && curstmt->type != ST_GOTO) {
            laststmt = curstmt;
            CPrep_GetFileOffsetInfo2(&cparser_fileoffset, &sourceoffset, &sourcefilepath);
            CFunc_AppendStatement(ST_RETURN);

            curstmt->dobjstack = NULL;
            curstmt->expr = NULL;

            if ((copts.cplusplus || copts.c9x) &&
                !strcmp(func->name->name, "main") &&
                TYPE_FUNC(func->type)->functype == TYPE(&stsignedint)) {
                curstmt->expr = intconstnode(TYPE(&stsignedint), 0);
            }

            if (laststmt->type == ST_EXPRESSION &&
                laststmt->expr->type == EFUNCCALL &&
                laststmt->expr->rtype == &stvoid &&
                (laststmt->expr->flags & ENODE_FLAG_VOLATILE)) {
                curstmt->flags |= StmtFlag_AfterNoReturn;
            }
        }

        CheckCLabels();

        if (nameobj_func && (nameobj_func->flags & OBJECT_USED)) {
            CInit_DeclareData(
                nameobj_func->u.alias.object, func->name->name, NULL, strlen(func->name->name) + 1
            );
        }
        if (nameobj_FUNCTION && (nameobj_FUNCTION->flags & OBJECT_USED)) {
            CInit_DeclareData(
                nameobj_FUNCTION->u.alias.object,
                func->name->name,
                NULL,
                strlen(func->name->name) + 1
            );
        }
        if (nameobj_pretty && (nameobj_pretty->flags & OBJECT_USED)) {
            CInit_DeclareData(
                nameobj_pretty->u.alias.object, prettyname, NULL, strlen(prettyname) + 1
            );
        }

        if (!fatalerrors) {
            if (CClass_IsConstructor(func)) {
                CABI_TransConstructor(func, beforebody, cscope_currentclass, NULL, has_try);
            }
            if (CClass_IsDestructor(func)) {
                CABI_TransDestructor(func, func, &firstStmt, cscope_currentclass, CABIDestroyBase);
            }

            CFunc_DestructorCleanup(&firstStmt);
            CFunc_CodeCleanup(&firstStmt);
            symdeclend = CPrep_GetFileOffsetInfo(&cparser_fileoffset);
            CFunc_Gen(&firstStmt, func, di->oldStyleParameters);
        }
    } else {
        if (tk == '{') {
            in_assembler = TRUE;
            tk = lex();
            in_assembler = FALSE;
        } else {
            CError_ErrorSkip(ERR_LBRACE_EXPECTED);
        }

        CFunc_ParseLocalDeclarationList(TRUE, FALSE, FALSE, FALSE);
        Assembler(func);
    }

    if (tk != '}') {
        CError_Error(ERR_RBRACE_EXPECTED);
    }

    CScope_RestoreScope(&scope);
}

void InitExpr_Register(ENode* expr, Object* object) {
    InitExpr* initexpr;
    InitExpr* scan;

    if (cparamblkptr->precompile == 1 &&
        object->sclass != TK_STATIC &&
        !(object->qual & (Q_IMPLICIT_WEAK | Q_WEAK))) {
        CError_Error(ERR_ILLEGAL_USE_PRECOMPILED_HEADER);
        return;
    }

    if (copts.suppress_init_code) {
        return;
    }

    initexpr = galloc(sizeof(InitExpr));
    initexpr->next = NULL;
    initexpr->object = object;
    initexpr->expr = CInline_CopyExpression(expr, CopyModeGlobal);

    if (init_expressions) {
        scan = init_expressions;
        while (scan->next) {
            scan = scan->next;
        }
        scan->next = initexpr;
    } else {
        init_expressions = initexpr;
    }
}

void CFunc_GenerateDummyFunction(Object* func) {
    NameSpace* nspace;
    Boolean saveDebugInfo;
    Statement firstStmt;

    if (anyerrors) {
        return;
    }

    nspace = CFunc_FuncGenSetup(&firstStmt, NULL);

    saveDebugInfo = copts.filesyminfo;
    copts.filesyminfo = FALSE;

    CFunc_CodeCleanup(&firstStmt);
    CFunc_Gen(&firstStmt, func, 0);

    cscope_current = nspace->parent;
    copts.filesyminfo = saveDebugInfo;
}

void CFunc_GenerateSingleExprFunc(Object* func, ENode* expr) {
    NameSpace* nspace;
    Boolean saveDebugInfo;
    Statement firstStmt;
    Statement* stmt;

    if (cparamblkptr->precompile == 1) {
        CError_Error(ERR_ILLEGAL_USE_PRECOMPILED_HEADER);
        return;
    }

    if (!anyerrors) {
        nspace = CFunc_FuncGenSetup(&firstStmt, func);

        saveDebugInfo = copts.filesyminfo;
        copts.filesyminfo = FALSE;

        stmt = CFunc_AppendStatement(ST_EXPRESSION);
        stmt->expr = expr;

        stmt = CFunc_AppendStatement(ST_RETURN);
        stmt->expr = NULL;

        CFunc_CodeCleanup(&firstStmt);
        CInline_GenFunc(&firstStmt, func, 0);

        cscope_current = nspace->parent;
        copts.filesyminfo = saveDebugInfo;
    }
}

void CFunc_GenerateDummyCtorFunc(Object* func, Object* real_ctor) {
    NameSpace* nspace;
    FuncArg* arg;
    ENodeList* list;
    ENode* expr;
    Boolean saveDebugInfo;
    Statement firstStmt;
    Statement* stmt;

    if (cparamblkptr->precompile == 1) {
        CError_Error(ERR_ILLEGAL_USE_PRECOMPILED_HEADER);
        return;
    }

    if (!anyerrors) {
        cscope_currentfunc = func;

        nspace = CFunc_FuncGenSetup(&firstStmt, func);

        saveDebugInfo = copts.filesyminfo;
        copts.filesyminfo = FALSE;

        CFunc_SetupNewFuncArgs(func, TYPE_FUNC(func->type)->args);

        expr = CExpr_NewENode(EFUNCCALL);
        expr->type = EFUNCCALL;
        expr->cost = 200;
        expr->rtype = TYPE(&void_ptr);
        expr->data.funccall.funcref = CExpr_MakeObjRefNode(real_ctor, FALSE);
        expr->data.funccall.functype = TYPE_FUNC(func->type);

        CError_ASSERT(3718, IS_TYPE_FUNC(real_ctor->type));
        CError_ASSERT(3719, TYPE_FUNC(real_ctor->type)->flags & FUNC_METHOD);
        CError_ASSERT(3720, arg = TYPE_FUNC(real_ctor->type)->args);
        CError_ASSERT(3722, arg = arg->next);
        CError_ASSERT(3723, arguments);

        list = lalloc(sizeof(ENodeList));
        expr->data.funccall.args = list;
        list->node = create_objectnode(arguments->object);

        if (TYPE_METHOD(real_ctor->type)->theclass->flags & CLASS_HAS_VBASES) {
            CError_ASSERT(3729, arg = arg->next);
            CError_ASSERT(3730, arguments->next);
            list->next = lalloc(sizeof(ENodeList));
            list = list->next;
            list->node = create_objectnode(arguments->next->object);
        }

        while (arg) {
            CError_ASSERT(3739, arg->dexpr);
            list->next = lalloc(sizeof(ENodeList));
            list = list->next;
            list->node = CExpr_GetDefaultArgument(expr->data.funccall.funcref, arg);
            arg = arg->next;
        }

        list->next = NULL;

        stmt = CFunc_AppendStatement(ST_RETURN);
        stmt->expr = expr;

        CFunc_CodeCleanup(&firstStmt);
        CInline_GenFunc(&firstStmt, func, 0);

        cscope_current = nspace->parent;
        cscope_currentfunc = NULL;
        copts.filesyminfo = saveDebugInfo;
    }
}

#pragma pack(2)
