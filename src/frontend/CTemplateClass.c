#include "compiler/CTemplateClass.h"
#include "compiler/CABI.h"
#include "compiler/CBrowse.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CInline.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/CTemplateFunc.h"
#include "compiler/CTemplateNew.h"
#include "compiler/CTemplateTools.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/templates.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include <stdlib.h>

TemplClass* CTemplClass_GetMasterTemplate(TemplClass* tmclass) {
    if (tmclass->inst_parent) {
        tmclass = TEMPL_CLASS(tmclass->theclass.nspace->theclass);
        CError_ASSERT(42, tmclass->theclass.flags & CLASS_IS_TEMPL);
    }

    return tmclass;
}

static void CTemplClass_SetupActionErrorRef(TemplateAction* action, TStreamElement** saved) {
    CError_ResetErrorSkip();
    CError_LockErrorPos(&action->source_ref, saved);
}

static void CTemplClass_RestoreActionErrorRef(TStreamElement** saved) {
    CError_ResetErrorSkip();
    CError_UnlockErrorPos(saved);
}

static void CTemplClass_AppendTemplateAction(TemplClass* tmclass, TemplateAction* action) {
    TemplateAction* last;

    action->source_ref = *CPrep_CurStreamElement();

    if ((last = tmclass->actions)) {
        while (last->next) {
            last = last->next;
        }
        last->next = action;
    } else {
        tmclass->actions = action;
    }
}

static DefAction* CTemplClass_NewDefAction(TypeDeduce* deduce, TemplateAction* action) {
    DefAction* defAction = lalloc(sizeof(DefAction));
    defAction->next = deduce->defActions;
    defAction->action = action;
    deduce->defActions = defAction;
    return defAction;
}

static void CTemplClass_InsertTemplateAction(TemplClass* tmclass, TemplateAction* action) {
    action->source_ref = *CPrep_CurStreamElement();
    action->next = tmclass->actions;
    tmclass->actions = action;
}

void CTemplClass_RegisterUsingDecl(TemplClass* tmclass, TypeTemplDep* type, AccessType access) {
    TemplateAction* action;

    action = galloc(sizeof(TemplateAction));
    memclrw(action, sizeof(TemplateAction));

    action->type = TAT_USINGDECL;
    action->u.usingdecl.type = type;
    action->u.usingdecl.access = access;

    CTemplClass_AppendTemplateAction(tmclass, action);
}

void CTemplClass_RegisterFriend(TemplClass* tmclass, DeclInfo* di) {
    TemplateFriend* tfriend;
    TemplateAction* action;

    tfriend = galloc(sizeof(TemplateFriend));
    memclrw(tfriend, sizeof(TemplateFriend));

    if (tk == '{' && IS_TYPE_FUNC(di->thetype)) {
        di->qual |= Q_INLINE;
        TYPE_FUNC(di->thetype)->flags |= FUNC_DEFINED | FUNC_IS_TEMPL_INSTANCE;
        tfriend->fileoffset = cparser_fileoffset;

        CPrep_StreamGetBlock(&tfriend->stream, NULL, 1);

        if (lookahead() == ';') {
            tk = lex();
        } else {
            tk = ';';
        }
    }

    CDecl_PackDeclInfo(&tfriend->decl, di);

    action = galloc(sizeof(TemplateAction));
    memclrw(action, sizeof(TemplateAction));

    action->type = TAT_FRIEND;
    action->u.tfriend = tfriend;

    CTemplClass_AppendTemplateAction(tmclass, action);
}

void CTemplClass_RegisterBaseClass(
    TemplClass* tmclass, Type* type, AccessType access, Boolean is_virtual
) {
    TemplateAction* action;
    ClassList* insert_after;

    if ((insert_after = tmclass->theclass.bases)) {
        while (insert_after->next) {
            insert_after = insert_after->next;
        }
    }

    action = galloc(sizeof(TemplateAction));
    memclrw(action, sizeof(TemplateAction));

    action->type = TAT_BASE;
    action->u.base.type = type;
    action->u.base.insert_after = insert_after;
    action->u.base.access = access;
    action->u.base.is_virtual = is_virtual;

    CTemplClass_InsertTemplateAction(tmclass, action);
}

void CTemplClass_RegisterEnumType(TemplClass* tmclass, TypeEnum* enumtype) {
    TemplateAction* action;

    action = galloc(sizeof(TemplateAction));
    memclrw(action, sizeof(TemplateAction));

    action->type = TAT_ENUMTYPE;
    action->u.enumtype = enumtype;

    CTemplClass_AppendTemplateAction(tmclass, action);
}

void CTemplClass_RegisterEnumerator(
    TemplClass* tmclass, ObjEnumConst* objenumconst, ENode* initexpr
) {
    TemplateAction* action;

    action = galloc(sizeof(TemplateAction));
    memclrw(action, sizeof(TemplateAction));

    action->type = TAT_ENUMERATOR;
    action->u.enumerator.objenumconst = objenumconst;
    action->u.enumerator.initexpr =
        initexpr ? CInline_CopyExpression(initexpr, CopyModeGlobal) : NULL;

    CTemplClass_AppendTemplateAction(tmclass, action);
}

void CTemplClass_RegisterObjectInit(TemplClass* tmclass, Object* object, ENode* initexpr) {
    TemplateAction* action;

    action = galloc(sizeof(TemplateAction));
    memclrw(action, sizeof(TemplateAction));

    action->type = TAT_OBJECTINIT;
    action->u.objectinit.object = object;
    action->u.objectinit.initexpr = CInline_CopyExpression(initexpr, CopyModeGlobal);

    CTemplClass_AppendTemplateAction(tmclass, action);
}

void CTemplClass_RegisterObjectDef(TemplClass* tmclass, ObjBase* refobj) {
    TemplateAction* action;

    action = galloc(sizeof(TemplateAction));
    memclrw(action, sizeof(TemplateAction));

    action->type = TAT_OBJECTDEF;
    action->u.refobj = refobj;

    CTemplClass_AppendTemplateAction(tmclass, action);
}

void CTemplClass_CompleteClass(TemplClass* templ, ClassLayout* de) {
    templ->lex_order_count = de->lex_order_count;
    if (de->has_vtable) {
        templ->flags |= TEMPLCLASS_HAS_VTABLE;
    }
    templ->theclass.flags |= CLASS_COMPLETED;
}

static TemplClassInst* CTemplClass_NewInstance(
    TemplClass* templ, TemplArg* inst_args, TemplArg* oargs
) {
    TemplClassInst* inst;
    ObjTypeTag* tag;
    NameSpace* nspace;
    HashNameNode* name;

    CError_ASSERT(288, !templ->pspec_owner);

    inst = galloc(sizeof(TemplClassInst));
    memclrw(inst, sizeof(TemplClassInst));

    inst->next = templ->instances;
    templ->instances = inst;

    if (templ->templ__params) {
        name = CMangler_TemplateInstanceName(templ->theclass.classname, oargs ? oargs : inst_args);
    } else {
        name = templ->theclass.classname;
    }

    inst->inst_args = inst_args;
    inst->oargs = oargs;
    inst->parent = templ->inst_parent;

    nspace = CScope_NewListNameSpace(name, TRUE);
    nspace->theclass = TYPE_CLASS(inst);
    if (templ->templ_parent && templ->inst_parent) {
        nspace->parent = TYPE_CLASS(templ->inst_parent)->nspace;
    } else {
        NameSpace* scan = templ->theclass.nspace->parent;
        while (scan->is_templ) {
            scan = scan->parent;
        }
        nspace->parent = scan;
    }

    inst->theclass.type = TYPECLASS;
    inst->theclass.flags = CLASS_IS_TEMPL_INST;
    inst->theclass.nspace = nspace;
    inst->theclass.classname = templ->theclass.classname;
    inst->theclass.mode = templ->theclass.mode;
    inst->theclass.eflags = templ->theclass.eflags;
    inst->templ = templ;

    tag = galloc(sizeof(ObjTypeTag));
    memclrw(tag, sizeof(ObjTypeTag));

    tag->otype = OT_TYPETAG;
    tag->access = ACCESSPUBLIC;
    tag->type = TYPE(inst);
    CScope_AddObject(nspace, templ->theclass.classname, OBJ_BASE(tag));

    return inst;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -3
#endif
TemplClassInst* CTemplClass_GetInstance(TemplClass* tmclass, TemplArg* inst_args, TemplArg* oargs) {
    TemplClassInst* inst;

    for (inst = tmclass->instances; inst; inst = inst->next) {
        CError_ASSERT(356, !oargs);

        if (CTemplTool_EqualArgs(inst_args, inst->oargs ? inst->oargs : inst->inst_args)) {
            return inst;
        }
    }

    return CTemplClass_NewInstance(tmclass, inst_args, oargs);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

TemplateMember* CTemplClass_DefineMember(
    TemplClass* tmclass, Object* object, FileOffsetInfo* foi, TokenStream* stream
) {
    TemplateMember* member;

    for (member = tmclass->members; member; member = member->next) {
        if (member->object == object) {
            CError_Error(ERR_OBJECT_REDEFINED, object);
            return member;
        }
    }

    member = galloc(sizeof(TemplateMember));
    memclrw(member, sizeof(TemplateMember));
    member->next = tmclass->members;
    tmclass->members = member;

    member->params = NULL;
    member->object = object;
    member->fileoffset = *foi;
    member->stream = *stream;

    return member;
}

static void CTemplClass_ParseBody(TemplClass* templ, short mode, SInt32* offset) {
    DeclInfo di;

    templ->align = copts.structalignment;

    memclrw(&di, sizeof(di));
    di.file = CPrep_BrowserCurrentFile();
    CPrep_BrowserFilePosition(&di.file2, &di.sourceoffset);
    di.sourceoffset = *offset;
    di.pendingClass = templ;

    CDecl_ParseClass(&di, mode, TRUE, 0);

    if (tk == TK_UU_ATTRIBUTE_UU) {
        CParser_ParseAttribute(di.thetype, NULL);
    }
    if (tk != ';') {
        CError_Error(ERR_SEMICOLON_EXPECTED);
    }

    CBrowse_NewTemplateClass(templ, di.file2, di.sourceoffset, CPrep_BrowserFileOffset() + 1);
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -3
#endif
void CTemplClass_ParsePartialSpecialization(
    TemplateScopeState* templateScope, TemplParam* params, short mode, SInt32* offset
) {
    TemplClass* tmclass;
    Type* type;
    NameSpace* nspace;
    TemplArg* args;
    TemplPartialSpec* pspec;
    TemplClass* templ;
    TemplArg* arg;
    TemplParam* param;

    nspace = templateScope->scope;

    if ((tk = lex()) != TK_IDENTIFIER) {
        CError_Error(ERR_IDENTIFIER_EXPECTED);
        return;
    }

    if (!(type = CScope_GetLocalTagType(nspace, tkidentifier))) {
        CError_Error(ERR_UNDEFINED_IDENTIFIER, tkidentifier->name);
        return;
    }

    if (!IS_TEMPL_CLASS(type)) {
        CError_Error(ERR_STRUCT_UNION_ENUM_CLASS_TAG_REDEFINED, tkidentifier->name);
        return;
    }

    tmclass = TEMPL_CLASS(type);
    if ((tk = lex()) != '<') {
        CError_FATAL(472);
    }

    for (param = params; param; param = param->next) {
        switch (param->pid.type) {
            case TPT_TYPE:
                if (!param->data.typeparam.type) {
                    continue;
                }
                CError_Error(ERR_ILLEGAL_PARTIAL_SPECIALIZATION);
                break;

            case TPT_NONTYPE:
                if (!param->data.paramdecl.defaultarg) {
                    continue;
                }
                CError_Error(ERR_ILLEGAL_PARTIAL_SPECIALIZATION);
                break;

            case TPT_TEMPLATE:
                if (!param->data.templparam.defaultarg) {
                    continue;
                }
                CError_Error(ERR_ILLEGAL_PARTIAL_SPECIALIZATION);
                break;

            default:
                CError_FATAL(504);
        }

        break;
    }

    args = CTempl_ParseUncheckTemplArgs(tmclass->templ__params, FALSE);
    tk = lex();

    arg = args;
    param = tmclass->templ__params;
    while (1) {
        if (!arg) {
            if (!param) {
                break;
            }
            CError_Error(ERR_ILLEGAL_PARTIAL_SPECIALIZATION);
            return;
        }
        if (!param) {
            CError_Error(ERR_ILLEGAL_PARTIAL_SPECIALIZATION);
            return;
        }
        if (param->pid.type != arg->pid.type) {
            CError_Error(ERR_ILLEGAL_PARTIAL_SPECIALIZATION);
            return;
        }
        arg = arg->next;
        param = param->next;
    }

    if (CTemplTool_IsSameTemplate(tmclass->templ__params, args)) {
        CError_Error(ERR_ILLEGAL_PARTIAL_SPECIALIZATION);
    }

    for (pspec = tmclass->pspecs; pspec; pspec = pspec->next) {
        if (CTemplTool_EqualParams(pspec->templ->templ__params, params, FALSE) &&
            CTemplTool_EqualArgs(pspec->args, args)) {
            break;
        }
    }

    if (!pspec) {
        templ = galloc(sizeof(TemplClass));
        memclrw(templ, sizeof(TemplClass));

        templ->templ__params = params;
        CDecl_DefineClass(
            nspace, tmclass->theclass.classname, TYPE_CLASS(templ), mode, FALSE, FALSE
        );

        templ->theclass.flags = CLASS_IS_TEMPL;
        templ->pspec_owner = tmclass;

        pspec = galloc(sizeof(TemplPartialSpec));
        memclrw(pspec, sizeof(TemplPartialSpec));

        pspec->templ = templ;
        pspec->args = CTemplTool_MakeGlobalTemplArgCopy(args);
        pspec->next = tmclass->pspecs;
        tmclass->pspecs = pspec;
    } else {
        if ((pspec->templ->theclass.flags & CLASS_COMPLETED) && tk != ';') {
            CError_Error(
                ERR_STRUCT_UNION_ENUM_CLASS_TAG_REDEFINED, tmclass->theclass.classname->name
            );
            return;
        }

        if (tk == ':' || tk == '{') {
            CTemplTool_EqualParams(pspec->templ->templ__params, params, TRUE);
        }
    }

    switch (tk) {
        case ':':
        case '{':
            CTemplClass_ParseBody(pspec->templ, mode, offset);
            break;
        case ';':
            break;
        default:
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

void CTemplClass_ParseClass(
    TemplateScopeState* templateScope, TemplParam* params, short mode, SInt32* offset
) {
    TemplClass* templ;
    NameSpace* nspace;
    Type* type;
    UInt8 classDeclSpec = 0;

    nspace = templateScope->scope;
    if ((tk = lex()) == TK_UU_DECLSPEC) {
        CDecl_ParseClassDeclSpec(&classDeclSpec);
    }

    if (tk != TK_IDENTIFIER) {
        CError_Error(ERR_IDENTIFIER_EXPECTED);
        return;
    }

    type = CScope_GetLocalTagType(nspace, tkidentifier);
    if (!type) {
        templ = galloc(sizeof(TemplClass));
        memclrw(templ, sizeof(TemplClass));

        templ->next = ctempl_templates;
        ctempl_templates = templ;

        templ->templ__params = params;
        CDecl_DefineClass(nspace, tkidentifier, TYPE_CLASS(templ), mode, FALSE, TRUE);
        templ->theclass.flags = CLASS_IS_TEMPL;
        templ->theclass.eflags = classDeclSpec;

        tk = lex();
        if (nspace->theclass && (nspace->theclass->flags & CLASS_IS_TEMPL)) {
            TemplateAction* action;

            templ->templ_parent = TEMPL_CLASS(nspace->theclass);

            action = galloc(sizeof(TemplateAction));
            memclrw(action, sizeof(TemplateAction));

            action->type = TAT_NESTEDCLASS;
            action->u.tclasstype = templ;
            CTemplClass_AppendTemplateAction(templ->templ_parent, action);
        }
    } else {
        if (!IS_TEMPL_CLASS(type)) {
            CError_Error(ERR_STRUCT_UNION_ENUM_CLASS_TAG_REDEFINED, tkidentifier->name);
            return;
        }

        templ = TEMPL_CLASS(type);
        if (!CTemplTool_EqualParams(templ->templ__params, params, FALSE)) {
            CError_Error(
                ERR_STRUCT_UNION_ENUM_CLASS_TAG_REDEFINED, templ->theclass.classname->name
            );
            return;
        }

        CTemplTool_MergeDefaultArgs(templ->templ__params, params);
        templ->theclass.eflags |= classDeclSpec;

        tk = lex();

        if ((templ->theclass.flags & CLASS_COMPLETED) && tk != ';') {
            CError_Error(
                ERR_STRUCT_UNION_ENUM_CLASS_TAG_REDEFINED, templ->theclass.classname->name
            );
            return;
        }

        if (tk != ';') {
            CTemplTool_EqualParams(templ->templ__params, params, TRUE);
        }
    }

    switch (tk) {
        case ':':
        case '{':
            CTemplClass_ParseBody(templ, mode, offset);
            break;
        case ';':
            break;
        default:
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
    }
}

static Boolean CTemplClass_TypeParamCompare(TemplArg* arg, Type* type, UInt32 qual) {
    return is_typesame(type, arg->data.typeparam.type) && arg->data.typeparam.qual == qual;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -11
#endif
static TemplArg* CTemplClass_PartialTemplateArgMatch(
    TemplPartialSpec* pspec, TemplArg* args, Boolean buildArgs
) {
    TemplArg* argA;
    TemplArg* argB;
    int i;
    int index;
    DeduceInfo info;

    if (!CTemplTool_InitDeduceInfo(&info, pspec->templ->templ__params, NULL, TRUE)) {
        return NULL;
    }

    argA = pspec->args;
    argB = args;
    i = 0;
    while (1) {
        if (!argA) {
            if (argB) {
                return NULL;
            }

            for (i = 0; i < info.maxCount; i++) {
                if (!info.args[i].is_deduced) {
                    return NULL;
                }
            }

            return buildArgs ? CTemplTool_MakeTemplArgList(&info) : args;
        }

        if (!argB) {
            return NULL;
        }

        if (argA->pid.type != argB->pid.type) {
            return NULL;
        }

        switch (argA->pid.type) {
            case TPT_TYPE:
                if (CTemplTool_IsTemplateArgumentDependentType(argA->data.typeparam.type)) {
                    if (!CTempl_DeduceType(
                            argA->data.typeparam.type,
                            argA->data.typeparam.qual,
                            argB->data.typeparam.type,
                            argB->data.typeparam.qual,
                            info.args,
                            FALSE,
                            FALSE
                        )) {
                        return NULL;
                    }
                } else {
                    if (!is_typesame(argA->data.typeparam.type, argB->data.typeparam.type) ||
                        argA->data.typeparam.qual != argB->data.typeparam.qual) {
                        return NULL;
                    }
                }
                break;

            case TPT_NONTYPE:
                if (CTemplTool_IsTemplateArgumentDependentExpression(argA->data.paramdecl.expr)) {
                    index = CTempl_GetTemplateArgumentExpressionIndex(argA);
                    CError_ASSERT(800, index >= 0);

                    if (info.args[index].is_deduced) {
                        if (!CTemplTool_EqualExprTypes(
                                argB->data.paramdecl.expr, info.args[index].data.paramdecl.expr
                            )) {
                            return NULL;
                        }
                    } else {
                        info.args[index].data.paramdecl.expr = argB->data.paramdecl.expr;
                        info.args[index].pid.type = TPT_NONTYPE;
                        info.args[index].is_deduced = TRUE;
                    }
                } else {
                    if (!CTemplTool_EqualExprTypes(
                            argB->data.paramdecl.expr, argA->data.paramdecl.expr
                        )) {
                        return NULL;
                    }
                }
                break;

            case TPT_TEMPLATE:
                if (CTemplTool_IsTemplateArgumentDependentType(argA->data.ttargtype)) {
                    if (!CTempl_DeduceType(
                            argA->data.ttargtype,
                            0,
                            argB->data.ttargtype,
                            0,
                            info.args,
                            FALSE,
                            FALSE
                        )) {
                        return NULL;
                    }
                } else {
                    if (!is_typesame(argA->data.ttargtype, argB->data.ttargtype)) {
                        return NULL;
                    }
                }
                break;

            default:
                CError_FATAL(841);
        }

        argA = argA->next;
        argB = argB->next;
        i++;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -11
#endif
static Boolean CTemplClass_PartialClassIsAtLeastAsSpecialized(
    TemplPartialSpec* pspec1, TemplPartialSpec* pspec2
) {
    TemplArg* argA;
    TemplArg* argB;
    int i;
    DeduceInfo info;

    if (!CTemplTool_InitDeduceInfo(&info, pspec2->templ->templ__params, NULL, TRUE)) {
        return FALSE;
    }

    argA = pspec1->args;
    argB = pspec2->args;

    while (1) {
        if (!argA) {
            CError_ASSERT(867, !argB);

            for (i = 0; i < info.maxCount; i++) {
                if (!info.args[i].is_deduced) {
                    return FALSE;
                }
            }

            return TRUE;
        }

        CError_ASSERT(876, argB);
        CError_ASSERT(877, argA->pid.type == argB->pid.type);

        switch (argA->pid.type) {
            case TPT_TYPE:
                if (!CTempl_DeduceType(
                        argB->data.typeparam.type,
                        argB->data.typeparam.qual,
                        argA->data.typeparam.type,
                        argA->data.typeparam.qual,
                        info.args,
                        FALSE,
                        FALSE
                    )) {
                    return FALSE;
                }
                break;

            case TPT_NONTYPE:
                if (CTemplTool_IsTemplateArgumentDependentExpression(argB->data.paramdecl.expr)) {
                    i = CTempl_GetTemplateArgumentExpressionIndex(argB);
                    CError_ASSERT(918, i >= 0);

                    if (info.args[i].is_deduced) {
                        if (argA->data.paramdecl.expr) {
                            if (!info.args[i].data.paramdecl.expr ||
                                !CTemplTool_EqualExprTypes(
                                    argA->data.paramdecl.expr, info.args[i].data.paramdecl.expr
                                )) {
                                return FALSE;
                            }
                        } else {
                            if (info.args[i].data.paramdecl.expr ||
                                argA->pid.index != info.args[i].pid.index) {
                                return FALSE;
                            }
                        }
                    } else {
                        info.args[i].data.paramdecl.expr = argA->data.paramdecl.expr;
                        info.args[i].pid.index = argA->pid.index;
                        info.args[i].pid.type = TPT_NONTYPE;
                        info.args[i].is_deduced = TRUE;
                    }
                } else {
                    if (!argA->data.paramdecl.expr ||
                        !CTemplTool_EqualExprTypes(
                            argA->data.paramdecl.expr, argB->data.paramdecl.expr
                        )) {
                        return FALSE;
                    }
                }
                break;

            case TPT_TEMPLATE:
                if (!CTempl_DeduceType(
                        argB->data.ttargtype, 0, argA->data.ttargtype, 0, info.args, FALSE, FALSE
                    )) {
                    return FALSE;
                }
                break;

            default:
                CError_FATAL(966);
        }

        argA = argA->next;
        argB = argB->next;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static Boolean CTemplClass_PartialClassIsMoreSpecialized(
    TemplPartialSpec* pspec1, TemplPartialSpec* pspec2
) {
    return CTemplClass_PartialClassIsAtLeastAsSpecialized(pspec1, pspec2) &&
        !CTemplClass_PartialClassIsAtLeastAsSpecialized(pspec2, pspec1);
}

struct PSpecList {
    PSpecList* next;
    TemplPartialSpec* pspec;
};

static PSpecList* CTemplClass_FindMostSpecializedPartialSpecializations(PSpecList* list) {
    PSpecList** array;
    PSpecList* scan;
    int i;
    int j;
    int count;

    count = 0;
    scan = list;
    while (scan) {
        count++;
        scan = scan->next;
    }

    array = lalloc(sizeof(PSpecList*) * count);
    for (i = 0, scan = list; scan; scan = scan->next) {
        array[i++] = scan;
    }

    for (i = 0; i < count; i++) {
        if (array[i]) {
            for (j = 0; j < count; j++) {
                if (array[j] &&
                    i != j &&
                    CTemplClass_PartialClassIsMoreSpecialized(array[i]->pspec, array[j]->pspec)) {
                    array[j] = NULL;
                }
            }
        }
    }

    for (i = 0, list = NULL; i < count; i++) {
        if (array[i]) {
            if (!list) {
                list = array[i];
            } else {
                array[j]->next = array[i];
            }
            array[i]->next = NULL;
            j = i;
        }
    }

    return list;
}

Boolean CTemplClass_FindPartialTemplate(
    TemplArg* args, TemplClass** resultTempl, TemplArg** resultArgs
) {
    TemplPartialSpec* pspec;
    PSpecList* list;
    TemplClassInst* inst;

    for (inst = (*resultTempl)->instances; inst; inst = inst->next) {
        if (inst->is_instantiated || inst->is_specialized) {
            if (CTemplTool_EqualArgs(args, inst->oargs ? inst->oargs : inst->inst_args)) {
                return FALSE;
            }
        }
    }

    list = NULL;
    for (pspec = (*resultTempl)->pspecs; pspec; pspec = pspec->next) {
        if (CTemplClass_PartialTemplateArgMatch(pspec, args, FALSE)) {
            PSpecList* entry = lalloc(sizeof(PSpecList));
            entry->next = list;
            entry->pspec = pspec;
            list = entry;
        }
    }

    if (list) {
        if (list->next) {
            list = CTemplClass_FindMostSpecializedPartialSpecializations(list);
            if (list->next) {
                CError_Error(ERR_AMBIGUOUS_USE_PARTIAL_SPECIALIZATION);
            }
        }

        if (!list->pspec->templ->templ__params) {
            *resultTempl = list->pspec->templ;
            *resultArgs = NULL;
            return TRUE;
        }

        *resultTempl = list->pspec->templ;
        *resultArgs = CTemplClass_PartialTemplateArgMatch(list->pspec, args, TRUE);
        return *resultArgs != NULL;
    } else {
        return FALSE;
    }
}

TemplClass* CTemplClass_DefineNestedClass(TemplClass* parent, HashNameNode* name, short mode) {
    TemplateAction* action;
    TemplClass* templ;

    templ = galloc(sizeof(TemplClass));
    memclrw(templ, sizeof(TemplClass));

    templ->next = ctempl_templates;
    ctempl_templates = templ;

    templ->templ_parent = parent;
    templ->templ__params = NULL;
    CDecl_DefineClass(parent->theclass.nspace, name, TYPE_CLASS(templ), mode, FALSE, TRUE);

    templ->theclass.flags = CLASS_IS_TEMPL;
    templ->align = copts.structalignment;

    action = galloc(sizeof(TemplateAction));
    memclrw(action, sizeof(TemplateAction));

    action->type = TAT_NESTEDCLASS;
    action->u.tclasstype = templ;
    CTemplClass_AppendTemplateAction(parent, action);

    return templ;
}

static void CTemplClass_CopyNestedClass(TypeDeduce* deduce, TemplClass* templ) {
    ObjTypeTag* tag;

    tag = galloc(sizeof(ObjTypeTag));
    memclrw(tag, sizeof(ObjTypeTag));

    tag->otype = OT_TYPETAG;
    tag->access = ACCESSPUBLIC;

    if (!templ->templ__params) {
        TemplClassInst* inst = CTemplClass_NewInstance(templ, NULL, NULL);
        inst->parent = deduce->inst;
        inst->theclass.nspace->parent = deduce->inst->theclass.nspace;

        tag->type = TYPE(inst);
    } else {
        TemplClass* copy = galloc(sizeof(TemplClass));
        memclrw(copy, sizeof(TemplClass));

        copy->next = ctempl_templates;
        ctempl_templates = copy;

        copy->theclass = templ->theclass;
        copy->templ_parent = deduce->tmclass;
        copy->inst_parent = deduce->inst;
        copy->templ__params = templ->templ__params;
        copy->members = NULL;
        copy->instances = NULL;
        copy->pspecs = NULL;
        copy->actions = templ->actions;
        copy->lex_order_count = templ->lex_order_count;
        copy->align = templ->align;
        copy->flags = templ->flags;

        tag->type = TYPE(copy);
    }

    CScope_AddObject(deduce->inst->theclass.nspace, templ->theclass.classname, OBJ_BASE(tag));
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -11
#endif
static void CTemplClass_CopyBaseClasses(
    TypeDeduce* deduce, TemplClassInst* inst, TemplClass* templ
) {
    TemplateAction* action;
    ClassList* newBase;
    ClassList* instbase;
    ClassList* base;
    ClassList* scan;
    UInt32 qual = 0;
    UInt32* pqual;

    for (base = templ->theclass.bases; base; base = base->next) {
        newBase = galloc(sizeof(ClassList));
        *newBase = *base;
        newBase->next = NULL;

        if ((scan = inst->theclass.bases)) {
            while (1) {
                if (scan->base == newBase->base) {
                    CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
                    break;
                }
                if (!scan->next) {
                    scan->next = newBase;
                    break;
                }
                scan = scan->next;
            }
        } else {
            inst->theclass.bases = newBase;
        }
    }

    for (action = deduce->tmclass->actions; action; action = action->next) {
        if (action->type == TAT_BASE) {
            TStreamElement* save;

            CTemplClass_SetupActionErrorRef(action, &save);

            newBase = galloc(sizeof(ClassList));
            memclrw(newBase, sizeof(ClassList));

            newBase->base =
                TYPE_CLASS(CTemplTool_DeduceTypeCopy(deduce, action->u.base.type, pqual = &qual));
            newBase->access = action->u.base.access;
            newBase->is_virtual = action->u.base.is_virtual;

            if (IS_TYPE_CLASS(newBase->base)) {
                if (newBase->base->size == 0) {
                    CDecl_CompleteType(TYPE(newBase->base));
                    IsCompleteType(TYPE(newBase->base));
                }

                if (CDecl_CheckNewBase(TYPE_CLASS(inst), newBase->base, newBase->is_virtual)) {
                    if (action->u.base.insert_after) {
                        base = templ->theclass.bases;
                        instbase = inst->theclass.bases;
                        while (1) {
                            CError_ASSERT(1233, base && instbase);

                            if (base == action->u.base.insert_after) {
                                newBase->next = instbase->next;
                                instbase->next = newBase;
                                break;
                            }

                            base = base->next;
                            instbase = instbase->next;
                        }
                    } else {
                        newBase->next = inst->theclass.bases;
                        inst->theclass.bases = newBase;
                    }
                }
            } else {
                CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
            }

            CTemplClass_RestoreActionErrorRef(&save);
        }
    }

    if (inst->theclass.flags & CLASS_HAS_VBASES) {
        CDecl_MakeVBaseList(TYPE_CLASS(inst));
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static void CTemplClass_CopyEnum(TypeDeduce* deduce, TemplateAction* action) {
    TypeEnum* srcenum;
    TypeEnum* destenum;
    ObjEnumConst** destptr;
    ObjEnumConst* src;
    TemplateAction* scanaction;

    srcenum = action->u.enumtype;
    destenum = galloc(sizeof(TypeEnum));
    memclrw(destenum, sizeof(TypeEnum));

    destenum->type = TYPEENUM;
    destenum->size = srcenum->size;
    destenum->nspace = deduce->inst->theclass.nspace;
    destenum->enumtype = srcenum->enumtype;
    destenum->enumname = srcenum->enumname;

    if (destenum->enumname) {
        CScope_DefineTypeTag(destenum->nspace, destenum->enumname, TYPE(destenum));
    }

    src = srcenum->enumlist;
    destptr = &destenum->enumlist;
    while (src) {
        ObjEnumConst* dest;

        dest = galloc(sizeof(ObjEnumConst));
        *dest = *src;

        *destptr = dest;
        dest->next = NULL;
        dest->type = TYPE(destenum);
        CScope_AddObject(deduce->inst->theclass.nspace, dest->name, OBJ_BASE(dest));

        src = src->next;
        destptr = &(*destptr)->next;
    }

    for (scanaction = deduce->tmclass->actions; scanaction; scanaction = scanaction->next) {
        if (scanaction->type == TAT_ENUMERATOR &&
            (src = scanaction->u.enumerator.objenumconst)->type == TYPE(srcenum)) {
            CTemplClass_NewDefAction(deduce, action)->enumtype = destenum;
            return;
        }
    }
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -11
#endif
static void CTemplClass_CompleteEnumType(
    TypeDeduce* deduce, TemplateAction* action, TypeEnum* destenum
) {
    ObjEnumConst* src;
    TypeEnum* srcenum;
    ObjEnumConst* dest;
    TemplateAction* scanaction;
    ENode* expr;

    srcenum = action->u.enumtype;

    for (scanaction = deduce->tmclass->actions; scanaction; scanaction = scanaction->next) {
        if (scanaction->type == TAT_ENUMERATOR) {
            src = scanaction->u.enumerator.objenumconst;
            if (src->type == TYPE(srcenum)) {
                TStreamElement* save;

                CTemplClass_SetupActionErrorRef(scanaction, &save);

                dest = destenum->enumlist;
                while (dest) {
                    if (dest->name == src->name) {
                        break;
                    }
                    dest = dest->next;
                }

                CError_ASSERT(1343, dest);

                if (scanaction->u.enumerator.initexpr) {
                    expr = CTemplTool_DeduceExpr(deduce, scanaction->u.enumerator.initexpr);
                    if (!ENODE_IS(expr, EINTCONST)) {
                        CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
                        CTemplClass_RestoreActionErrorRef(&save);
                        break;
                    }
                } else {
                    CError_ASSERT(1358, expr);
                    expr->data.intval = CInt64_Add(expr->data.intval, cint64_one);
                }

                dest->val = expr->data.intval;
                dest->type = expr->rtype;
                CTemplClass_RestoreActionErrorRef(&save);
            }
        }
    }

    CDecl_ComputeUnderlyingEnumType(destenum);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static void CTemplClass_CopyObjMemberVarPath(TypeDeduce* deduce, ObjMemberVarPath* ivar) {
    ObjMemberVarPath* copy;

    copy = galloc(sizeof(ObjMemberVarPath));
    *copy = *ivar;

    if (copy->path && copy->path->type == TYPE(deduce->tmclass)) {
        copy->path = CClass_GetPathCopy(copy->path, TRUE);
        copy->path->type = TYPE(deduce->inst);
    }

    CScope_AddObject(deduce->inst->theclass.nspace, copy->name, OBJ_BASE(copy));
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -11
#endif
static void CTemplClass_CopyIVars(TypeDeduce* deduce, TemplClassInst* inst, TemplClass* templ) {
    ObjMemberVar* src;
    ObjMemberVar* dest;
    ObjMemberVar** destptr;
    TemplateAction* scanaction;
    DefAction* defAction;

    src = templ->theclass.ivars;
    destptr = &inst->theclass.ivars;

    while (src) {
        CError_ASSERT(1408, !src->has_path);

        dest = galloc(sizeof(ObjMemberVar));
        *dest = *src;

        for (scanaction = deduce->tmclass->actions; scanaction; scanaction = scanaction->next) {
            if (scanaction->type == TAT_OBJECTDEF && scanaction->u.refobj == OBJ_BASE(src)) {
                defAction = CTemplClass_NewDefAction(deduce, scanaction);
                defAction->refobj = OBJ_BASE(dest);
                break;
            }
        }

        if (!scanaction) {
            dest->type = CTemplTool_DeduceTypeCopy(deduce, dest->type, &dest->qual);
            if (dest->type->size == 0) {
                CDecl_CompleteType(dest->type);
                IsCompleteType(dest->type);
            }
        }

        if (dest->name && dest->name != no_name_node) {
            CScope_AddObject(inst->theclass.nspace, dest->name, OBJ_BASE(dest));
        }

        *destptr = dest;
        destptr = &dest->next;
        src = src->next;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -11
#endif
static void CTemplClass_CopyObjType(TypeDeduce* deduce, ObjType* src, HashNameNode* name) {
    TemplateAction* scanaction;
    NameSpaceObjectList* list;
    NameSpaceObjectList* newlist;
    ObjType* dest;

    for (scanaction = deduce->tmclass->actions; scanaction; scanaction = scanaction->next) {
        if (scanaction->type == TAT_OBJECTDEF && scanaction->u.refobj == OBJ_BASE(src)) {
            break;
        }
    }

    dest = galloc(sizeof(ObjType));
    *dest = *src;

    if (scanaction) {
        CTemplClass_NewDefAction(deduce, scanaction)->refobj = OBJ_BASE(dest);
    } else {
        dest->type = CTemplTool_DeduceTypeCopy(deduce, dest->type, &dest->qual);
    }

    if ((list = CScope_FindName(deduce->inst->theclass.nspace, name)) &&
        list->object->otype == OT_TYPETAG) {
        CError_ASSERT(1481, list->next == NULL);

        newlist = galloc(sizeof(NameSpaceObjectList));
        newlist->object = list->object;
        newlist->next = NULL;

        list->object = OBJ_BASE(dest);
        list->next = newlist;
        return;
    } else {
        CScope_AddObject(deduce->inst->theclass.nspace, name, OBJ_BASE(dest));
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static void CTemplClass_CopyObjTypeTag(TypeDeduce* deduce, ObjTypeTag* src, HashNameNode* name) {
    UInt32 qual = 0;
    ObjTypeTag* dest = galloc(sizeof(ObjTypeTag));

    *dest = *src;
    dest->type = CTemplTool_DeduceTypeCopy(deduce, dest->type, &qual);

    CScope_AddObject(deduce->inst->theclass.nspace, name, OBJ_BASE(dest));
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -11
#endif
static void CTemplClass_CopyMemberTemplate(TypeDeduce* deduce, Object* src) {
    TemplateFunction* desttempl;
    TemplateFunction* srctempl;
    TemplateAction* action;
    Object* dest;
    DefAction* defAction;

    srctempl = src->u.func.u.templ;
    CError_ASSERT(1527, srctempl && srctempl->params);

    for (action = deduce->tmclass->actions; action; action = action->next) {
        if (action->type == TAT_OBJECTDEF && action->u.refobj == OBJ_BASE(src)) {
            break;
        }
    }

    desttempl = galloc(sizeof(TemplateFunction));
    *desttempl = *srctempl;

    desttempl->next = ctempl_templatefuncs;
    ctempl_templatefuncs = desttempl;

    desttempl->original = srctempl;

    dest = galloc(sizeof(Object));
    *dest = *src;

    dest->u.func.u.templ = desttempl;
    dest->nspace = deduce->inst->theclass.nspace;

    CError_ASSERT(1552, !deduce->processingArgument);
    deduce->processingArgument = TRUE;
    deduce->nindex = srctempl->params->pid.nindex;

    if (action) {
        defAction = CTemplClass_NewDefAction(deduce, action);
        defAction->refobj = OBJ_BASE(dest);
    } else {
        dest->type = CTemplTool_DeduceTypeCopy(deduce, dest->type, &dest->qual);
    }

    deduce->processingArgument = FALSE;

    CError_ASSERT(1564, IS_TYPE_FUNC(dest->type));

    TYPE_FUNC(dest->type)->flags |= FUNC_IS_TEMPL;

    if ((TYPE_FUNC(dest->type)->flags & FUNC_IS_CTOR) && deduce->hasNewVBases && !action) {
        FuncArg* arg;
        CError_ASSERT(1571, TYPE_FUNC(dest->type)->args);
        arg = CParser_NewFuncArg();
        arg->type = TYPE(&stsignedshort);
        arg->next = TYPE_FUNC(dest->type)->args->next;
        TYPE_FUNC(dest->type)->args->next = arg;
    }

    CScope_AddObject(deduce->inst->theclass.nspace, dest->name, OBJ_BASE(dest));
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -11
#endif
static void CTemplClass_CopyObject(TypeDeduce* deduce, Object* src) {
    ObjectTemplated* dest;
    TemplateAction* action;
    Boolean inClassScope;
    DefAction* defAction;

    inClassScope = TRUE;
    if (src->nspace != deduce->tmclass->theclass.nspace) {
        CError_ASSERT(1598, src->datatype == DALIAS);
        inClassScope = FALSE;
    }

    if (IS_TEMPL_FUNC(src->type)) {
        CTemplClass_CopyMemberTemplate(deduce, src);
        return;
    }

    for (action = deduce->tmclass->actions; action; action = action->next) {
        if (action->type == TAT_OBJECTDEF && action->u.refobj == OBJ_BASE(src)) {
            break;
        }
    }

    dest = galloc(sizeof(ObjectTemplated));
    dest->object = *src;

    if (action) {
        defAction = CTemplClass_NewDefAction(deduce, action);
        defAction->refobj = OBJ_BASE(dest);
    } else {
        dest->object.type =
            CTemplTool_DeduceTypeCopy(deduce, dest->object.type, &dest->object.qual);
    }

    if (inClassScope) {
        dest->object.nspace = deduce->inst->theclass.nspace;
    }

    dest->object.qual |= Q_IS_TEMPLATED;
    dest->parent = src;

    if (IS_TYPE_FUNC(dest->object.type)) {
        TYPE_FUNC(dest->object.type)->flags &= ~FUNC_DEFINED;
    }

    switch (dest->object.datatype) {
        case DDATA:
            dest->object.u.data.linkname = NULL;
            for (action = deduce->tmclass->actions; action; action = action->next) {
                if (action->type == TAT_OBJECTINIT && action->u.objectinit.object == src) {
                    defAction = CTemplClass_NewDefAction(deduce, action);
                    defAction->refobj = OBJ_BASE(dest);
                    break;
                }
            }
            break;

        case DABSOLUTE:
            break;

        case DFUNC:
        case DVFUNC:
            dest->object.u.func.linkname = NULL;
            CError_ASSERT(1661, IS_TYPE_FUNC(dest->object.type));
            CError_ASSERT(1662, !dest->object.u.func.u.templ && !dest->object.u.func.defargdata);

            if ((TYPE_FUNC(dest->object.type)->flags & FUNC_IS_CTOR) &&
                deduce->hasNewVBases &&
                !action) {
                FuncArg* arg;
                CError_ASSERT(1668, TYPE_FUNC(dest->object.type)->args);
                arg = CParser_NewFuncArg();
                arg->type = TYPE(&stsignedshort);
                arg->next = TYPE_FUNC(dest->object.type)->args->next;
                TYPE_FUNC(dest->object.type)->args->next = arg;
            }

            if (TYPE_FUNC(dest->object.type)->flags & FUNC_CONVERSION) {
                CError_ASSERT(1676, IS_TYPE_FUNC(src->type));
                if (CTemplTool_IsTemplateArgumentDependentType(TYPE_FUNC(src->type)->functype)) {
                    CError_ASSERT(1679, action);
                    return;
                }
            }

            break;

        case DINLINEFUNC:
            break;

        case DALIAS:
            if (dest->object.u.alias.member &&
                dest->object.u.alias.member->type == TYPE(deduce->tmclass)) {
                dest->object.u.alias.member = CClass_GetPathCopy(dest->object.u.alias.member, TRUE);
                dest->object.u.alias.member->type = TYPE(deduce->inst);
            }
            break;

        case DLOCAL:
        case DEXPR:
            CError_FATAL(1699);

        default:
            CError_FATAL(1702);
    }

    CScope_AddObject(deduce->inst->theclass.nspace, dest->object.name, OBJ_BASE(dest));
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -11
#endif
static void CTemplClass_CompleteObject(
    TypeDeduce* deduce, TemplateAction* action, ObjBase* refobj
) {
    if (refobj->otype == OT_MEMBERVAR) {
        OBJ_MEMBER_VAR(refobj)->type = CTemplTool_DeduceTypeCopy(
            deduce, OBJ_MEMBER_VAR(refobj)->type, &OBJ_MEMBER_VAR(refobj)->qual
        );

        if (OBJ_MEMBER_VAR(refobj)->type->size == 0) {
            CDecl_CompleteType(OBJ_MEMBER_VAR(refobj)->type);
            if (!copts.experimental ||
                OBJ_MEMBER_VAR(refobj)->next ||
                OBJ_MEMBER_VAR(refobj)->type->size != 0 ||
                !IS_TYPE_ARRAY(OBJ_MEMBER_VAR(refobj)->type)) {
                IsCompleteType(OBJ_MEMBER_VAR(refobj)->type);
            }
        }
        return;
    } else if (refobj->otype == OT_TYPE) {
        OBJ_TYPE(refobj)->type =
            CTemplTool_DeduceTypeCopy(deduce, OBJ_TYPE(refobj)->type, &OBJ_TYPE(refobj)->qual);
        return;
    } else {
        Object* dest;
        Object* src;

        CError_ASSERT(1748, refobj->otype == OT_OBJECT);

        dest = OBJECT(refobj);
        src = OBJECT(action->u.refobj);

        if (IS_TEMPL_FUNC(src->type)) {
            TemplateFunction* templ = src->u.func.u.templ;
            CError_ASSERT(1758, templ);
            CError_ASSERT(1759, !deduce->processingArgument);

            deduce->processingArgument = TRUE;
            deduce->nindex = templ->params->pid.nindex;
            dest->type = CTemplTool_DeduceTypeCopy(deduce, dest->type, &dest->qual);
            deduce->processingArgument = FALSE;

            CError_ASSERT(1764, IS_TYPE_FUNC(dest->type));
            TYPE_FUNC(dest->type)->flags |= FUNC_IS_TEMPL;
            return;
        } else {
            dest->type = CTemplTool_DeduceTypeCopy(deduce, dest->type, &dest->qual);
            dest->qual |= Q_IS_TEMPLATED;

            if (IS_TYPE_FUNC(dest->type)) {
                TYPE_FUNC(dest->type)->flags &= ~FUNC_DEFINED;
            }

            switch (dest->datatype) {
                case DFUNC:
                case DVFUNC:
                    CError_ASSERT(1780, IS_TYPE_FUNC(dest->type));
                    if (TYPE_FUNC(dest->type)->flags & FUNC_CONVERSION) {
                        CError_ASSERT(1783, IS_TYPE_FUNC(dest->type));
                        if (CTemplTool_IsTemplateArgumentDependentType(
                                TYPE_FUNC(src->type)->functype
                            )) {
                            dest->name = CMangler_ConversionFuncName(
                                TYPE_FUNC(dest->type)->functype, TYPE_FUNC(dest->type)->qual
                            );
                            CScope_AddObject(
                                deduce->inst->theclass.nspace, dest->name, OBJ_BASE(dest)
                            );
                        }
                    }

                    if ((TYPE_FUNC(dest->type)->flags & FUNC_IS_CTOR) && deduce->hasNewVBases) {
                        FuncArg* arg;
                        CError_ASSERT(1797, TYPE_FUNC(dest->type)->args);
                        arg = CParser_NewFuncArg();
                        arg->type = TYPE(&stsignedshort);
                        arg->next = TYPE_FUNC(dest->type)->args->next;
                        TYPE_FUNC(dest->type)->args->next = arg;
                    }

                    break;
            }
        }
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static void CTemplClass_CompleteObjectInit(
    TypeDeduce* deduce, TemplateAction* action, Object* object
) {
    ENode* expr = CTemplTool_DeduceExpr(deduce, action->u.objectinit.initexpr);

    if (ENODE_IS(expr, EINTCONST) &&
        (object->qual & Q_CONST) &&
        IS_TYPE_INT_OR_ENUM(object->type)) {
        object->u.data.u.intconst = expr->data.intval;
        object->qual |= Q_INLINE_DATA | Q_IMPLICIT_WEAK;
    } else {
        CError_Error(ERR_ILLEGAL_STATIC_CONST_MEMBER_INITIALIZATION, object->name->name);
    }
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -11
#endif
static void CTemplClass_CopyNameSpace(TypeDeduce* deduce, TemplClassInst* inst, TemplClass* templ) {
    NameSpaceName* nsname;
    NameSpaceObjectList* list;

    CError_ASSERT(1841, !templ->theclass.nspace->is_hash);

    for (nsname = templ->theclass.nspace->data.list; nsname; nsname = nsname->next) {
        for (list = &nsname->first; list; list = list->next) {
            switch (list->object->otype) {
                case OT_ENUMCONST:
                    break;
                case OT_MEMBERVAR:
                    if (OBJ_MEMBER_VAR(list->object)->has_path) {
                        CTemplClass_CopyObjMemberVarPath(deduce, OBJ_MEMBER_VAR_PATH(list->object));
                    }
                    break;
                case OT_TYPE:
                    CTemplClass_CopyObjType(deduce, OBJ_TYPE(list->object), nsname->name);
                    break;
                case OT_TYPETAG:
                    break;
                case OT_NAMESPACE:
                    CError_FATAL(1865);
                case OT_OBJECT:
                    CTemplClass_CopyObject(deduce, OBJECT(list->object));
                    break;
                default:
                    CError_FATAL(1872);
            }
        }
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -11
#endif
static void CTemplClass_CopyUsingDecl(TypeDeduce* deduce, TypeTemplDep* type, AccessType access) {
    TypeClass* tclass;
    UInt32 qual = 0;

    CError_ASSERT(1889, IS_TYPE_TEMPLATE(type) && type->dtype == TEMPLDEP_QUALNAME);

    tclass = TYPE_CLASS(CTemplTool_DeduceTypeCopy(deduce, TYPE(type->u.qual.type), &qual));
    if (!IS_TYPE_CLASS(tclass)) {
        CError_Error(ERR_ILLEGAL_USE_TEMPLATE_ARGUMENT_DEPENDENT_TYPE, type->u.qual.name->name);
        return;
    }

    CDecl_CompleteType(TYPE(tclass));
    CScope_AddClassUsingDeclaration(TYPE_CLASS(deduce->inst), tclass, type->u.qual.name, access);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -11
#endif
static void CTemplClass_CopyFriend(TypeDeduce* deduce, TemplateFriend* tfriend) {
    TemplArg* arg;
    Object* funcobj;
    NameSpace* nspace;
    DeclInfo di;
    CScopeSave saveScope;
    Boolean firstDecl;

    CDecl_UnpackDeclInfo(&di, &tfriend->decl);

    if (CTemplTool_IsTemplateArgumentDependentType(di.thetype)) {
        di.thetype = CTemplTool_DeduceTypeCopy(deduce, di.thetype, &di.qual);
    }

    if (di.expltargs) {
        di.expltargs = CTemplTool_MakeGlobalTemplArgCopy(di.expltargs);
        for (arg = di.expltargs; arg; arg = arg->next) {
            switch (arg->pid.type) {
                case TPT_TYPE:
                    if (CTemplTool_IsTemplateArgumentDependentType(arg->data.typeparam.type)) {
                        arg->data.typeparam.type = CTemplTool_DeduceTypeCopy(
                            deduce, arg->data.typeparam.type, &arg->data.typeparam.qual
                        );
                    }
                    break;
                case TPT_NONTYPE:
                    if (CTemplTool_IsTemplateArgumentDependentExpression(
                            arg->data.paramdecl.expr
                        )) {
                        arg->data.paramdecl.expr =
                            CTemplTool_DeduceExpr(deduce, arg->data.paramdecl.expr);
                    }
                    break;
                case TPT_TEMPLATE:
                default:
                    CError_FATAL(1941);
            }
        }
    }

    if (IS_TYPE_FUNC(di.thetype)) {
        nspace = CScope_FindGlobalNS(deduce->inst->theclass.nspace);
        CScope_SetNameSpaceScope(nspace, &saveScope);
        funcobj = CDecl_GetFunctionObject(&di, NULL, &firstDecl, FALSE);
        CScope_RestoreScope(&saveScope);

        if (funcobj) {
            CDecl_AddFriend(TYPE_CLASS(deduce->inst), funcobj, NULL);
            if (tfriend->stream.tokens) {
                CInline_AddInlineFunctionAction(
                    funcobj, TYPE_CLASS(deduce->inst), &tfriend->fileoffset, &tfriend->stream, FALSE
                );
            }
        } else {
            CError_Error(ERR_ILLEGAL_FRIEND_DECLARATION);
        }
    } else {
        CError_ASSERT(1974, IS_TYPE_CLASS(di.thetype));
        CDecl_AddFriend(TYPE_CLASS(deduce->inst), NULL, TYPE_CLASS(di.thetype));
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -11
#endif
Boolean CTempl_InstantiateTemplateClass(TypeClass* tclass) {
    TemplClassInst* inst;
    ParserTryBlock* tryBlock;
    TemplateAction* action;
    DefAction* defAction;
    UInt8 saveAlignMode;

    TemplStack stack;
    CScopeSave saveScope;
    TemplClass* templ;
    ClassLayout layout;
    TypeDeduce deduce;
    TStreamElement* saveErrorRef;
    TemplArg* inst_args;

    CError_ASSERT(2000, tclass->flags & CLASS_IS_TEMPL_INST);

    if (tclass->flags & CLASS_COMPLETED) {
        return TRUE;
    }

    inst = TEMPL_CLASS_INST(tclass);
    if (inst->is_specialized) {
        return FALSE;
    }

    templ = inst->templ;
    if (!(templ->flags & TEMPLCLASS_IS_SPECIALIZATION)) {
        templ = CTemplClass_GetMasterTemplate(templ);
    }

    if (templ->pspecs && CTemplClass_FindPartialTemplate(inst->inst_args, &templ, &inst_args)) {
        CError_ASSERT(2024, !inst->oargs);
        inst->templ = templ;
        inst->oargs = inst->inst_args;
        inst->inst_args = inst_args;
    }

    if (!(templ->theclass.flags & CLASS_COMPLETED)) {
        return FALSE;
    }

    if (inst->is_instantiated) {
        return FALSE;
    }

    inst->is_instantiated = TRUE;
    CScope_SetClassScope(tclass, &saveScope);
    CTemplTool_PushInstance(&stack, tclass, NULL);

    tryBlock = trychain;
    trychain = NULL;

    memclrw(&deduce, sizeof(deduce));
    deduce.tmclass = templ;
    deduce.inst = inst;
    deduce.params = templ->templ__params;
    deduce.args = inst->inst_args;

    CError_ASSERT(2056, !templ->theclass.sominfo);
    CError_ASSERT(2058, !templ->theclass.objcinfo);
    CError_ASSERT(2060, !templ->theclass.vtable);

    inst->theclass.flags |= templ->theclass.flags &
        (CLASS_ABSTRACT | CLASS_SINGLE_OBJECT | CLASS_HAS_VBASES | CLASS_IS_CONVERTIBLE |
         CLASS_COM_OBJECT);

    CTemplClass_CopyBaseClasses(&deduce, inst, templ);

    deduce.hasNewVBases = (inst->theclass.flags & CLASS_HAS_VBASES) != 0 &&
        (templ->theclass.flags & CLASS_HAS_VBASES) == 0;

    for (action = templ->actions; action; action = action->next) {
        switch (action->type) {
            case TAT_NESTEDCLASS:
                CTemplClass_SetupActionErrorRef(action, &saveErrorRef);
                CTemplClass_CopyNestedClass(&deduce, action->u.tclasstype);
                CTemplClass_RestoreActionErrorRef(&saveErrorRef);
                break;
            case TAT_ENUMTYPE:
                CTemplClass_SetupActionErrorRef(action, &saveErrorRef);
                CTemplClass_CopyEnum(&deduce, action);
                CTemplClass_RestoreActionErrorRef(&saveErrorRef);
                break;
            case TAT_FRIEND:
            case TAT_ENUMERATOR:
            case TAT_BASE:
            case TAT_OBJECTINIT:
            case TAT_USINGDECL:
            case TAT_OBJECTDEF:
                break;
            default:
                CError_FATAL(2105);
        }
    }

    CTemplClass_CopyIVars(&deduce, inst, templ);
    CTemplClass_CopyNameSpace(&deduce, inst, templ);

    CError_ASSERT(2116, !templ->theclass.friends);

    for (action = templ->actions; action; action = action->next) {
        switch (action->type) {
            case TAT_NESTEDCLASS:
                break;
            case TAT_ENUMTYPE:
                for (defAction = deduce.defActions; defAction; defAction = defAction->next) {
                    if (defAction->action == action) {
                        CTemplClass_SetupActionErrorRef(action, &saveErrorRef);
                        CTemplClass_CompleteEnumType(&deduce, action, defAction->enumtype);
                        CTemplClass_RestoreActionErrorRef(&saveErrorRef);
                        break;
                    }
                }
                break;
            case TAT_FRIEND:
                CTemplClass_SetupActionErrorRef(action, &saveErrorRef);
                CTemplClass_CopyFriend(&deduce, action->u.tfriend);
                CTemplClass_RestoreActionErrorRef(&saveErrorRef);
                break;
            case TAT_ENUMERATOR:
                break;
            case TAT_BASE:
                break;
            case TAT_OBJECTINIT:
                for (defAction = deduce.defActions;; defAction = defAction->next) {
                    CError_ASSERT(2147, defAction);
                    if (defAction->action == action) {
                        CTemplClass_SetupActionErrorRef(action, &saveErrorRef);
                        CTemplClass_CompleteObjectInit(&deduce, action, OBJECT(defAction->refobj));
                        CTemplClass_RestoreActionErrorRef(&saveErrorRef);
                        break;
                    }
                }
                break;
            case TAT_USINGDECL:
                CTemplClass_SetupActionErrorRef(action, &saveErrorRef);
                CTemplClass_CopyUsingDecl(
                    &deduce, action->u.usingdecl.type, action->u.usingdecl.access
                );
                CTemplClass_RestoreActionErrorRef(&saveErrorRef);
                break;
            case TAT_OBJECTDEF:
                for (defAction = deduce.defActions;; defAction = defAction->next) {
                    CError_ASSERT(2167, defAction);
                    if (defAction->action == action) {
                        CTemplClass_SetupActionErrorRef(action, &saveErrorRef);
                        CTemplClass_CompleteObject(&deduce, action, defAction->refobj);
                        CTemplClass_RestoreActionErrorRef(&saveErrorRef);
                        break;
                    }
                }
                break;
            default:
                break;
        }
    }

    memclrw(&layout, sizeof(layout));
    layout.lex_order_count = templ->lex_order_count;
    layout.has_vtable = (templ->flags & TEMPLCLASS_HAS_VTABLE) != 0;

    saveAlignMode = copts.structalignment;
    copts.structalignment = templ->align;
    CDecl_CompleteClass(&layout, TYPE_CLASS(inst));
    copts.structalignment = saveAlignMode;

    if (templ->theclass.align > inst->theclass.align) {
        inst->theclass.align = templ->theclass.align;
        inst->theclass.size += CABI_StructSizeAlignValue(TYPE(inst), inst->theclass.size);
    }

    CTemplTool_PopInstance(&stack);
    CScope_RestoreScope(&saveScope);
    trychain = tryBlock;

    return TRUE;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif
