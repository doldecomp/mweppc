#include "compiler/CTemplateNew.h"
#include "compiler/CBrowse.h"
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
#include "compiler/CTemplateClass.h"
#include "compiler/CTemplateTools.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/templates.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include <setjmp.h>
#include <string.h>

TemplClass* ctempl_templates;
TemplateFunction* ctempl_templatefuncs;
TemplStack* ctempl_curinstance;
static jmp_buf ctempl_parseparse;
static Boolean ctempl_scanfuncparams;

static TemplParam* CTempl_ParseParamList(NameSpace* nspace, UInt8 nindex);
static Boolean CTempl_GenClassInstance(TemplClassInst* inst, Boolean isExplicit);

void CTempl_Setup(void) {
    ctempl_curinstance = NULL;
    ctempl_templates = NULL;
    ctempl_templatefuncs = NULL;
    ctempl_instdepth = 0;
    ctempl_scanfuncparams = FALSE;
}

void CTempl_Cleanup(void) {}

// CTempl_MemberParseTempl and CTempl_ParseTemplateMember are unused (the original's file still has
// them); only CTempl_MemberParseLex has a use, inlined into CTempl_MemberParseTemplArgList
static short CTempl_MemberParseLex(void) {
    switch ((tk = lex())) {
        case ';':
        case '=':
        case '{':
        case '}':
            longjmp(ctempl_parseparse, 1);
    }

    return tk;
}

static void CTempl_MemberParseTemplArgList(void) {
    struct {
        char ch;
        char flag;
    } stack[256];
    int pos;

    stack[0].ch = '>';
    stack[0].flag = TRUE;
    pos = 0;
    while (1) {
        switch (CTempl_MemberParseLex()) {
            case '<':
                if (stack[pos].flag) {
                    if (pos >= 255) {
                        longjmp(ctempl_parseparse, 1);
                    }
                    stack[++pos].ch = '>';
                    stack[pos].flag = TRUE;
                }
                continue;
            case '(':
                if (pos >= 255) {
                    longjmp(ctempl_parseparse, 1);
                }
                stack[++pos].ch = ')';
                stack[pos].flag = FALSE;
                continue;
            case '[':
                if (pos >= 255) {
                    longjmp(ctempl_parseparse, 1);
                }
                stack[++pos].ch = ']';
                stack[pos].flag = FALSE;
                continue;
            case '>':
                if (!(stack[pos].flag)) {
                    continue;
                }
            case ')':
            case ']':
                break;
            default:
                continue;
        }

        if (tk != stack[pos].ch) {
            longjmp(ctempl_parseparse, 1);
        }
        if (--pos < 0) {
            break;
        }
    }
}

static Type* CTempl_MemberParseTempl(HashNameNode* name) {
    Type* type;

    type = CScope_GetTagType(cscope_current, name);
    if (!type || !IS_TEMPL_CLASS(type)) {
        longjmp(ctempl_parseparse, 1);
    }

    CTempl_MemberParseTemplArgList();
    if (lookahead() != TK_COLON_COLON) {
        return NULL;
    }
    CTempl_MemberParseLex();

    while (1) {
        switch (CTempl_MemberParseLex()) {
            case TK_IDENTIFIER:
                switch (lookahead()) {
                    case '(':
                    case ')':
                    case ';':
                    case '=':
                    case '[':
                        return type;
                    case TK_COLON_COLON:
                        CTempl_MemberParseLex();
                        continue;
                    default:
                        return NULL;
                }
                break;
            case '~':
            case TK_OPERATOR:
                return type;
            default:
                return NULL;
        }
    }
}

static Boolean CTempl_ParseTemplateMember(void) {
    HashNameNode* name;
    SInt32 state;

    CPrep_UnLex();
    CPrep_TokenStreamGetState(&state);

    if (setjmp(ctempl_parseparse) == 0) {
        while (1) {
            switch (CTempl_MemberParseLex()) {
                case TK_IDENTIFIER:
                    name = tkidentifier;
                    switch (lookahead()) {
                        case '<':
                            CTempl_MemberParseLex();
                            if (CTempl_MemberParseTempl(name)) {
                                CError_FATAL(228);
                                return TRUE;
                            }
                            break;
                        case TK_COLON_COLON:
                            CTempl_MemberParseLex();
                            while (1) {
                                if (CTempl_MemberParseLex() != TK_IDENTIFIER) {
                                    break;
                                }
                                if (lookahead() != TK_COLON_COLON) {
                                    break;
                                }
                                CTempl_MemberParseLex();
                            }
                            break;
                    }
                    break;
                case 0:
                    return FALSE;
                default:
                    continue;
            }
            break;
        }

        while (1) {
            switch (CTempl_MemberParseLex()) {
                case TK_IDENTIFIER:
                    name = tkidentifier;
                    if (CTempl_MemberParseLex() == '<') {
                        if (CTempl_MemberParseTempl(name)) {
                            CError_FATAL(265);
                            return TRUE;
                        }
                    }
                    break;
                case 0:
                    return FALSE;
                default:
                    continue;
            }
            break;
        }
    }

    CPrep_TokenStreamSetState(&state);
    tk = lex();
    return FALSE;
}

static Type* CTempl_ParseTemplArgType(UInt32* resultQual) {
    DeclInfo di;

    memclrw(&di, sizeof(di));
    CParser_GetDeclSpecs(&di, FALSE);

    if (di.storageclass) {
        CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
    }
    if (di.missingTypeSpecifier) {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
    }

    CError_QualifierCheck(
        di.qual & ~(Q_CONST | Q_VOLATILE | Q_PASCAL | Q_REFERENCE | Q_ALIGNED_MASK)
    );

    scandeclarator(&di);
    if (di.name) {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
    }

    CTemplTool_CheckTemplArgType(di.thetype);

    *resultQual = di.qual;
    return di.thetype;
}

static ENode* CTempl_ParseTemplArgExpr(Type* type, UInt32 qual) {
    ENode* expr;
    ENode* copy;

    disallowgreaterthan = TRUE;
    if (!copts.old_argmatch) {
        expr = assignment_expression();
    } else {
        expr = conv_assignment_expression();
    }
    disallowgreaterthan = FALSE;

    if (type &&
        !CTemplTool_IsTemplateArgumentDependentType(type) &&
        !IS_TYPE_TEMPLDEPEXPR(expr->rtype)) {
        expr = argumentpromotion(expr, type, qual, TRUE);
        if (IS_TYPE_POINTER_ONLY(type)) {
            if (ENODE_IS(expr, ETYPCON) && ENODE_IS(expr->data.monadic, EINTCONST)) {
                expr = expr->data.monadic;
            }
            if (ENODE_IS(expr, EINTCONST)) {
                expr->rtype = type;
            }
        }
    }

    if (!IS_TYPE_TEMPLDEPEXPR(expr->rtype)) {
        if (!copts.old_argmatch) {
            expr = pointer_generation(expr);
        }

        switch (expr->type) {
            case EINTCONST:
                break;
            case EOBJREF:
                if (CParser_HasInternalLinkage2(expr->data.objref)) {
                    CError_Error(ERR_TEMPLATE_NON_TYPE_ARGUMENT_OBJECTS_SHALL);
                }
                break;
            case EOBJLIST:
                CError_Error(ERR_AMBIGUOUS_ACCESS_OVERLOADED_FUNCTION);
                expr = nullnode();
                break;
            default:
                CError_Error(ERR_ILLEGAL_NON_TYPE_TEMPLATE_ARGUMENT);
                expr = nullnode();
        }

        copy = galloc(sizeof(ENode));
        *copy = *expr;
        return copy;
    } else {
        return CInline_CopyExpression(expr, CopyModeGlobal);
    }
}

static Type* CTempl_ParseTemplArgTempl(TemplParam* params) {
    NameResult pr;

    if ((tk != TK_IDENTIFIER && tk != TK_COLON_COLON) || !CScope_ParseDeclName(&pr) || !pr.type) {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        tk = lex();
        return NULL;
    }

    if (IS_TEMPL_CLASS(pr.type)) {
        if (params &&
            !CTemplTool_EqualParams(
                params->data.templparam.plist, TEMPL_CLASS(pr.type)->templ__params, FALSE
            )) {
            CError_Error(ERR_TEMPLATE_PARAMETER_MISMATCH);
            tk = lex();
            return NULL;
        }
    } else {
        if (!CTemplTool_IsTemplateArgumentDependentType(pr.type)) {
            CError_Error(ERR_ILLEGAL_TYPE);
        }
    }

    tk = lex();
    return pr.type;
}

static UInt8 CTempl_GetTemplateNestIndex(NameSpace* nspace) {
    UInt8 count = 0;

    while (nspace) {
        if (nspace->theclass && (nspace->theclass->flags & CLASS_IS_TEMPL_ANY)) {
            count++;
        }
        nspace = nspace->parent;
    }

    return count;
}

// params (the list so far) is not read
static TemplParam* CTempl_ParseParam(
    NameSpace* nspace, TemplParam* params, UInt16 index, UInt8 nindex
) {
    TemplParam* param;
    DeclInfo di;
    SInt32 savedState;
    short startToken;

    param = galloc(sizeof(TemplParam));
    memclrw(param, sizeof(TemplParam));

    param->pid.index = index;
    param->pid.nindex = nindex;

    CPrep_TokenStreamGetState(&savedState);

    startToken = tk;
    switch (tk) {
        case TK_CLASS:
        case TK_TYPENAME:
            if ((tk = lex()) == TK_IDENTIFIER) {
                param->name = tkidentifier;
                tk = lex();
            }

            switch (tk) {
                case ',':
                case '>':
                    break;
                case '=':
                    tk = lex();
                    param->data.typeparam.type =
                        CTempl_ParseTemplArgType(&param->data.typeparam.qual);
                    break;
                default:
                    param->name = NULL;
                    CPrep_TokenStreamSetState(&savedState);
                    tk = startToken;
                    goto defaultProc;
            }

            param->pid.type = TPT_TYPE;
            break;

        case TK_TEMPLATE:
            if ((tk = lex()) != '<') {
                CError_Error(ERR_LESS_EXPECTED);
                return NULL;
            }

            tk = lex();
            param->data.templparam.plist = CTempl_ParseParamList(nspace, 0);

            if (tk == '>') {
                tk = lex();
            } else {
                CError_Error(ERR_GREATER_EXPECTED);
            }

            if (tk == TK_CLASS) {
                tk = lex();
            } else {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }

            if (tk == TK_IDENTIFIER) {
                param->name = tkidentifier;
                tk = lex();
            }

            if (tk == '=') {
                tk = lex();
                param->data.templparam.defaultarg = CTempl_ParseTemplArgTempl(param);
            }

            param->pid.type = TPT_TEMPLATE;
            break;

        default:
defaultProc:
            memclrw(&di, sizeof(di));
            CParser_GetDeclSpecs(&di, FALSE);

            if (di.storageclass) {
                CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
            }

            CError_QualifierCheck(
                di.qual & ~(Q_CONST | Q_VOLATILE | Q_PASCAL | Q_REFERENCE | Q_ALIGNED_MASK)
            );

            scandeclarator(&di);

            switch (di.thetype->type) {
                case TYPEARRAY:
                    di.thetype = CDecl_NewPointerType(TPTR_TARGET(di.thetype));
                    break;
                case TYPEFUNC:
                    di.thetype = CDecl_NewPointerType(di.thetype);
                    break;
                case TYPEMEMBERPOINTER:
                    CError_Error(ERR_UNIMPLEMENTED_C_FEATURE);
                    di.thetype = TYPE(&stsignedint);
                    break;
                case TYPEINT:
                case TYPEENUM:
                case TYPETEMPLATE:
                case TYPEPOINTER:
                    break;
                default:
                    CError_Error(ERR_ILLEGAL_TEMPLATE_DECLARATION);
                    di.thetype = TYPE(&stsignedint);
            }

            di.thetype = CParser_RemoveTopMostQualifiers(di.thetype, &di.qual);
            param->name = di.name;
            param->data.paramdecl.type = di.thetype;
            param->data.paramdecl.qual = di.qual;
            if (tk == '=') {
                tk = lex();
                param->data.paramdecl.defaultarg = CTempl_ParseTemplArgExpr(di.thetype, di.qual);
            }

            param->pid.type = TPT_NONTYPE;
            break;
    }

    if (param->name) {
        CTemplTool_InsertTemplateParameter(nspace, param);
    }

    return param;
}

static TemplParam* CTempl_ParseParamList(NameSpace* nspace, UInt8 nindex) {
    TemplParam* params;
    TemplParam** ptr;
    TemplParam* param;
    TemplParam* scan;
    UInt16 index;

    params = NULL;
    index = 0;
    ptr = &params;

    while (1) {
        param = CTempl_ParseParam(nspace, params, index, nindex);
        if (!param) {
            break;
        }

        if (param->name) {
            for (scan = params; scan; scan = scan->next) {
                if (scan->name == param->name) {
                    CError_Error(ERR_IDENTIFIER_REDECLARED, param->name->name);
                }
            }
        }

        *ptr = param;
        ptr = &param->next;

        if (tk != ',') {
            break;
        }

        tk = lex();
        index++;
    }

    if (!params) {
        CError_Error(ERR_ILLEGAL_TEMPLATE_DECLARATION);
    }

    return params;
}

TemplArg* CTempl_ParseUncheckTemplArgs(TemplParam* params, Boolean is_global) {
    TemplArg* args;
    TemplArg* last;
    UInt16 index;

    if (tk != '<') {
        CError_Error(ERR_LESS_EXPECTED);
        return NULL;
    }

    if ((tk = lex()) == '>') {
        return NULL;
    }

    args = NULL;
    index = 0;
    while (1) {
        if (is_global) {
            if (args) {
                last->next = galloc(sizeof(TemplArg));
                last = last->next;
            } else {
                last = galloc(sizeof(TemplArg));
                args = last;
            }
        } else {
            if (args) {
                last->next = lalloc(sizeof(TemplArg));
                last = last->next;
            } else {
                last = galloc(sizeof(TemplArg));
                args = last;
            }
        }

        last->next = NULL;

        if (!params) {
            Type* type;
            UInt32 qual;
            Boolean isTemplateName; // NOTE: invented name

            last->pid.index = index;
            last->pid.nindex = 0;

            if ((type = CParser_ParseTypeID(&qual, &isTemplateName))) {
                if (isTemplateName) {
                    last->data.ttargtype = type;
                    last->pid.type = TPT_TEMPLATE;
                } else {
                    last->data.typeparam.type = type;
                    last->data.typeparam.qual = qual;
                    last->pid.type = TPT_TYPE;
                }
            } else {
                last->data.paramdecl.expr = CTempl_ParseTemplArgExpr(NULL, 0);
                last->pid.type = TPT_NONTYPE;
            }
        } else {
            last->pid = params->pid;
            switch (last->pid.type) {
                case TPT_TYPE:
                    last->data.typeparam.type =
                        CTempl_ParseTemplArgType(&last->data.typeparam.qual);
                    break;
                case TPT_NONTYPE:
                    last->data.paramdecl.expr = CTempl_ParseTemplArgExpr(NULL, 0);
                    break;
                case TPT_TEMPLATE:
                    if (!(last->data.ttargtype = CTempl_ParseTemplArgTempl(params))) {
                        return NULL;
                    }
                    break;
                default:
                    CError_FATAL(674);
            }
            params = params->next;
        }

        if (tk == '>') {
            return args;
        }

        if (tk != ',') {
            CError_Error(ERR_COMMA_EXPECTED);
            return NULL;
        }

        tk = lex();
        index++;
    }
}

static TemplArg* CTempl_ParseTemplArgs(TemplClass** resultTempl, TemplArg** resultArgs) {
    TemplParam* param;
    TemplParam* params;
    TemplArg* args;
    TemplArg** ptr;
    TemplArg* arg;

    params = (*resultTempl)->templ__params;
    *resultArgs = NULL;

    if (tk != '<') {
        CError_Error(ERR_LESS_EXPECTED);
        return NULL;
    }

    tk = lex();

    param = params;
    args = NULL;
    ptr = &args;

    while (param) {
        arg = galloc(sizeof(TemplArg));
        memclrw(arg, sizeof(TemplArg));

        *ptr = arg;
        ptr = &arg->next;

        arg->pid = param->pid;

        if (tk != '>') {
            switch (arg->pid.type) {
                case TPT_TYPE: {
                    // TODO: fakematch: only the pointer variable reproduces the original's code
                    // here; the direct argument the other callers pass does not
                    UInt32* q = &arg->data.typeparam.qual;
                    arg->data.typeparam.type = CTempl_ParseTemplArgType(q);
                    break;
                }

                case TPT_NONTYPE:
                    if (CTemplTool_IsTemplateArgumentDependentType(param->data.paramdecl.type)) {
                        Type* type;
                        UInt32 qual;

                        type = CTemplTool_DeduceArgDepType(
                            args, param->data.paramdecl.type, param->data.paramdecl.qual, &qual
                        );
                        arg->data.paramdecl.expr = CTempl_ParseTemplArgExpr(type, qual);
                    } else {
                        arg->data.paramdecl.expr = CTempl_ParseTemplArgExpr(
                            param->data.paramdecl.type, param->data.paramdecl.qual
                        );
                    }
                    break;

                case TPT_TEMPLATE:
                    if (!(arg->data.ttargtype = CTempl_ParseTemplArgTempl(param))) {
                        return NULL;
                    }
                    break;

                default:
                    CError_FATAL(742);
            }

            if (tk != '>') {
                if (tk != ',') {
                    CError_Error(ERR_ILLEGAL_TEMPLATE_ARGUMENTS);
                    return NULL;
                }

                if ((tk = lex()) == '>') {
                    CError_Error(ERR_ILLEGAL_TEMPLATE_ARGUMENTS);
                    return NULL;
                }
            }
        } else {
            switch (arg->pid.type) {
                case TPT_TYPE:
                    if (!param->data.typeparam.type) {
                        CError_Error(ERR_ILLEGAL_TEMPLATE_ARGUMENTS);
                        return NULL;
                    }

                    arg->data.typeparam.type = param->data.typeparam.type;
                    arg->data.typeparam.qual = param->data.typeparam.qual;

                    if (CTemplTool_IsTemplateArgumentDependentType(param->data.typeparam.type)) {
                        TypeDeduce deduce;
                        memclrw(&deduce, sizeof(deduce));
                        deduce.tmclass = *resultTempl;
                        deduce.inst = NULL;
                        deduce.params = params;
                        deduce.args = args;

                        arg->data.typeparam.qual = param->data.typeparam.qual;
                        arg->data.typeparam.type = CTemplTool_DeduceTypeCopy(
                            &deduce, arg->data.typeparam.type, &arg->data.typeparam.qual
                        );
                    }
                    break;

                case TPT_NONTYPE:
                    if (!param->data.paramdecl.defaultarg) {
                        CError_Error(ERR_ILLEGAL_TEMPLATE_ARGUMENTS);
                        return NULL;
                    }

                    if (IS_TYPE_TEMPLDEPEXPR(param->data.paramdecl.defaultarg->rtype)) {
                        TypeDeduce deduce;
                        memclrw(&deduce, sizeof(deduce));
                        deduce.tmclass = *resultTempl;
                        deduce.inst = NULL;
                        deduce.params = params;
                        deduce.args = args;

                        arg->data.paramdecl.expr = CInline_CopyExpression(
                            CTemplTool_DeduceExpr(&deduce, param->data.paramdecl.defaultarg),
                            CopyModeGlobal
                        );
                    } else {
                        arg->data.paramdecl.expr = param->data.paramdecl.defaultarg;
                    }
                    break;

                case TPT_TEMPLATE:
                    if (!param->data.templparam.defaultarg) {
                        CError_Error(ERR_ILLEGAL_TEMPLATE_ARGUMENTS);
                        return NULL;
                    }

                    if (IS_TEMPL_CLASS(param->data.templparam.defaultarg)) {
                        arg->data.ttargtype = param->data.templparam.defaultarg;
                        break;
                    }

                    if (CTemplTool_IsTemplateArgumentDependentType(
                            param->data.templparam.defaultarg
                        )) {
                        CError_Error(ERR_UNIMPLEMENTED_C_FEATURE);
                        return NULL;
                    }

                    CError_FATAL(817);

                default:
                    CError_FATAL(820);
            }
        }

        param = param->next;
    }

    if (tk != '>') {
        CError_Error(ERR_GREATER_EXPECTED);
        return NULL;
    }

    if ((*resultTempl)->pspecs) {
        return args;
    }

    return args;
}

Type* CTempl_ParseTemplTemplParam(TypeTemplDep* type) {
    TemplArg* args;
    Type* newType;

    tk = lex();
    if (!(args = CTempl_ParseUncheckTemplArgs(NULL, TRUE))) {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        return TYPE(&stsignedint);
    }

    newType = TYPE(CDecl_NewTemplDepType(TEMPLDEP_QUALTEMPL));
    TYPE_TEMPLATE(newType)->u.qualtempl.type = type;
    TYPE_TEMPLATE(newType)->u.qualtempl.args = args;
    return newType;
}

Type* CTempl_ClassGetType(TemplClass* templ) {
    TemplClass* owner;
    TemplArg* ownerArgs;
    TemplArg* args;
    TemplClass* pspecOwner;
    Type* type;

    pspecOwner = (owner = templ)->pspec_owner;
    if (pspecOwner) {
        owner = pspecOwner;
    }

    if (!(args = CTempl_ParseTemplArgs(&owner, &ownerArgs))) {
        return &stvoid;
    }

    if ((type = CTemplTool_IsDependentTemplate(owner, args))) {
        return type;
    }

    return TYPE(CTemplClass_GetInstance(owner, args, ownerArgs));
}

static void CTempl_SetupClassParamNameSpace(TemplateScopeState* templateScope, TypeClass* tclass) {
    cscope_current = tclass->nspace;
}

Boolean CTempl_IsQualifiedMember(DeclInfo* di, Type* type, NameSpace** resultnspace) {
    TemplClass* templ;

    if (TYPE_TEMPLATE(type)->dtype == TEMPLDEP_QUALNAME &&
        (templ =
             TEMPL_CLASS(CTemplTool_GetSelfRefTemplate(TYPE(TYPE_TEMPLATE(type)->u.qual.type))))) {
        *resultnspace = templ->theclass.nspace;
        CError_ASSERT(948, di->templateScope && (*resultnspace)->theclass);

        CTempl_SetupClassParamNameSpace(di->templateScope, (*resultnspace)->theclass);
        di->templateScope = NULL;
        return TRUE;
    }

    return FALSE;
}

// NOTE: invented names: unused1, unused2, unused3, func
// unused: nothing calls it (the original's file still has it)
static void* CTempl_ParseMemberFunction(int unused1, int unused2, int unused3, Object* func) {
    CError_ASSERT(974, TYPE_FUNC(func->type)->flags & FUNC_METHOD);

    return NULL;
}

static void CTempl_ParseMember(
    TemplParam* params, TemplClass* templ, DeclInfo* di, SInt32* startOffset
) {
    Object* object;
    NameSpaceObjectList* nsol;
    TokenStream stream;
    Boolean firstDecl; // NOTE: invented name
    CPrepFileInfo* file;
    SInt32 offset;
    Boolean saveForceLoc;
    TemplateMember* member;

    if (templ->theclass.flags & CLASS_IS_TEMPL) {
        di->thetype = CTemplTool_ResolveMemberSelfRefs(templ, di->thetype, &di->qual);
    }

    if (IS_TYPE_FUNC(di->thetype)) {
        if (!(object = CDecl_GetFunctionObject(di, NULL, &firstDecl, FALSE))) {
            CError_Error(ERR_UNDEFINED_IDENTIFIER, di->name->name);
            return;
        }

        if (tk != '{' && tk != TK_TRY && tk != ':') {
            if (tk != ';') {
                CError_Error(ERR_SEMICOLON_EXPECTED);
            } else {
                tk = lex();
            }
            return;
        }

        if ((TYPE_FUNC(object->type)->flags & FUNC_DEFINED) &&
            (!(TYPE_FUNC(object->type)->flags & FUNC_IS_TEMPL) ||
             object->u.func.u.templ->instances)) {
            CError_Error(ERR_OBJECT_REDEFINED, object);
        }

        TYPE_FUNC(object->type)->flags |= FUNC_IS_TEMPL_INSTANCE | FUNC_DEFINED;

        CPrep_StreamGetBlock(&stream, NULL, 1);
        saveForceLoc = gForceSourceLoc;
        gForceSourceLoc = TRUE;
        CPrep_BrowserFilePosition(&file, &offset);
        gForceSourceLoc = saveForceLoc;

        if (file && file->recordbrowseinfo && *startOffset >= 0 && offset > *startOffset) {
            CBrowse_NewFunction(object, file, file, *startOffset, offset + 1);
        }
    } else {
        if (!(nsol = CScope_GetLocalObject(templ->theclass.nspace, di->name))) {
            CError_Error(ERR_UNDEFINED_IDENTIFIER, di->name->name);
            return;
        }

        object = OBJECT(nsol->object);
        if (object->otype != OT_OBJECT) {
            CError_Error(ERR_IDENTIFIER_REDECLARED, di->name->name);
            return;
        }

        if (!is_typesame(di->thetype, object->type) ||
            (object->qual & (Q_CONST | Q_VOLATILE | Q_PASCAL)) !=
                (di->qual & (Q_CONST | Q_VOLATILE | Q_PASCAL))) {
            CError_Error(
                ERR_IDENTIFIER_REDECLARED_WAS_DECLARED_AS_NOW,
                CError_GetObjectName(object),
                object->type,
                object->qual,
                di->thetype,
                di->qual
            );
            return;
        }

        CError_ASSERT(1070, object->datatype == DDATA);

        CPrep_StreamGetSemicolon(&stream, NULL);
    }

    if (stream.tokens) {
        if (IS_TEMPL_FUNC(object->type) != 0) {
            if (!CTemplTool_EqualParams(object->u.func.u.templ->params, params, FALSE)) {
                CError_Error(ERR_TEMPLATE_PARAMETER_MISMATCH);
            }

            object->u.func.u.templ->stream = stream;
        } else {
            if (!(templ->theclass.flags & CLASS_IS_TEMPL)) {
                CError_Error(ERR_UNIMPLEMENTED_C_FEATURE);
                return;
            }

            member = CTemplClass_DefineMember(templ, object, &cparser_fileoffset, &stream);
            if (templ->templ__params) {
                if (!CTemplTool_EqualParams(templ->templ__params, params, FALSE)) {
                    CError_Error(ERR_TEMPLATE_PARAMETER_MISMATCH);
                } else {
                    member->params = params;
                }
            }
        }
    }
}

// NOTE: invented names: di, params, tclass, access, isStatic
static TemplateFunction* CTempl_DeclareTemplateFunction(
    DeclInfo* di, TemplParam* params, TypeClass* tclass, AccessType access, Boolean isStatic
) {
    TemplateFunction* templ;
    Object* object;

    if (tclass) {
        CError_ASSERT(1122, cscope_current->theclass);
        di->thetype = TYPE(
            CDecl_MakeTypeMemberFunc(TYPE_FUNC(di->thetype), cscope_current->theclass, isStatic)
        );
    } else {
        access = ACCESSPUBLIC;
    }

    templ = galloc(sizeof(TemplateFunction));
    memclrw(templ, sizeof(TemplateFunction));

    templ->next = ctempl_templatefuncs;
    ctempl_templatefuncs = templ;

    templ->params = params;
    templ->name = di->name;
    templ->deftoken = symdecltoken;

    object = CParser_NewFunctionObject(NULL);
    object->access = access;
    object->name = di->name;
    object->u.func.linkname = CParser_GetUniqueName();
    object->type = di->thetype;
    object->qual = di->qual | Q_MANGLE_NAME;
    object->sclass = di->storageclass;
    TYPE_FUNC(object->type)->flags |= FUNC_IS_TEMPL;
    object->u.func.u.templ = templ;

    if (di->qual & Q_INLINE) {
        object->sclass = TK_STATIC;
    }

    templ->tfunc = object;
    CScope_AddObject(cscope_current, di->name, OBJ_BASE(object));

    return templ;
}

static void CTempl_ParseTemplateFunction(
    TemplateFunction* templ, TypeClass* tclass, SInt32* startOffset
) {
    Object* object;
    CPrepFileInfo* file;
    SInt32 offset;
    Boolean saveForceLoc;

    object = templ->tfunc;

    if (tk == '{' || tk == ':' || tk == TK_TRY) {
        if (tclass) {
            object->qual |= Q_INLINE;
            object->sclass = TK_STATIC;
        }

        if (TYPE_FUNC(object->type)->flags & FUNC_DEFINED) {
            CError_Error(ERR_OBJECT_REDEFINED, object);
        }

        TYPE_FUNC(object->type)->flags |= FUNC_DEFINED | FUNC_IS_TEMPL_INSTANCE;

        CPrep_StreamGetBlock(&templ->stream, NULL, 0);

        if (lookahead() == ';') {
            tk = lex();
        } else {
            tk = ';';
        }

        saveForceLoc = gForceSourceLoc;
        gForceSourceLoc = TRUE;
        CPrep_BrowserFilePosition(&file, &offset);
        gForceSourceLoc = saveForceLoc;

        if (file && *startOffset >= 0 && offset > *startOffset) {
            templ->srcfile = file;
            templ->startoffset = *startOffset;
            templ->endoffset = offset + 1;
            if (cparamblkptr->browseoptions.recordTemplates && file->recordbrowseinfo) {
                CBrowse_NewTemplateFunc(templ);
            }
        }

    } else {
        if (tk != ';') {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        }
    }
}

static HashNameNode* CTempl_FindConversionFuncName(TypeClass* tclass, Type* type, UInt32 qual) {
    Object* object;
    CScopeObjectIterator iter;

    CScope_InitObjectIterator(&iter, tclass->nspace);
    do {
        if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
            return NULL;
        }
    } while (!IS_TYPE_FUNC(object->type) ||
             !(TYPE_FUNC(object->type)->flags & FUNC_CONVERSION) ||
             (TYPE_FUNC(object->type)->qual & (Q_CONST | Q_VOLATILE)) !=
                 (qual & (Q_CONST | Q_VOLATILE)) ||
             !is_typesame(TYPE_FUNC(object->type)->functype, type));

    return object->name;
}

static void CTempl_ParseConversionFunctionTemplate(
    DeclInfo* di,
    TemplateScopeState* templateScope,
    TemplParam* params,
    TypeClass* tclass,
    SInt32* startOffset,
    AccessType access
) {
    TemplateFunction* templfunc;

    tk = lex();
    if (!tclass) {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        return;
    }

    tclass->flags |= CLASS_IS_CONVERTIBLE;
    CError_QualifierCheck(di->qual & ~Q_INLINE);

    conversion_type_name(di);
    CDecl_NewConvFuncType(di);

    if (cscope_current->is_templ) {
        CError_ASSERT(1260, cscope_current == templateScope->scope);
        cscope_current = templateScope->scope->parent;
    }

    templfunc = CTempl_DeclareTemplateFunction(di, params, tclass, access, FALSE);
    CTempl_ParseTemplateFunction(templfunc, tclass, startOffset);
}

static void CTempl_ParseFunctionOrMemberTemplate(
    TemplateScopeState* templateScope,
    TemplParam* params,
    TypeClass* tclass,
    SInt32* startOffset,
    AccessType access,
    Boolean allowTemplateArguments
) {
    NameSpaceObjectList* nsol;
    TemplParam* param;
    TemplateFunction* templfunc;
    Object* object;
    Boolean disallowCVFlag;
    TypeClass* tclass2;
    Type* type;
    UInt32 qual;
    DeclInfo di;

    for (param = params; param; param = param->next) {
        switch (param->pid.type) {
            case TPT_TYPE:
                if (param->data.typeparam.type) {
                    CError_Error(ERR_ILLEGAL_USE_DEFAULT_TEMPLATE_ARGUMENT);
                    param->data.typeparam.type = NULL;
                }
                break;
            case TPT_NONTYPE:
                if (param->data.paramdecl.defaultarg) {
                    CError_Error(ERR_ILLEGAL_USE_DEFAULT_TEMPLATE_ARGUMENT);
                    param->data.paramdecl.defaultarg = NULL;
                }
                break;
            case TPT_TEMPLATE:
                if (param->data.templparam.defaultarg) {
                    CError_Error(ERR_ILLEGAL_USE_DEFAULT_TEMPLATE_ARGUMENT);
                    param->data.templparam.defaultarg = NULL;
                }
                break;
            default:
                CError_FATAL(1317);
        }
    }

    disallowCVFlag = FALSE;
    ctempl_scanfuncparams = TRUE;

    memclrw(&di, sizeof(di));
    di.allowTemplateArguments = allowTemplateArguments;

    if (tk == TK_OPERATOR) {
        CTempl_ParseConversionFunctionTemplate(
            &di, templateScope, params, tclass, startOffset, access
        );
        return;
    }

    CParser_GetDeclSpecs(&di, TRUE);
    if (tk == ';' && IS_TEMPL_CLASS(di.thetype)) {
        return;
    }

    if (di.resolvedObject || di.resolvedObjects) {
        if (di.resolvedObjects) {
            di.resolvedObject = OBJECT(di.resolvedObjects->object);
            CError_ASSERT(1342, di.resolvedObject->otype == OT_OBJECT);
        }

        CError_ASSERT(1344, IS_TYPE_FUNC(OBJECT(di.resolvedObject)->type));

        if (TYPE_FUNC(OBJECT(di.resolvedObject)->type)->flags & FUNC_CONVERSION) {
            di.thetype = TYPE_FUNC(OBJECT(di.resolvedObject)->type)->functype;
            di.qual |= TYPE_FUNC(OBJECT(di.resolvedObject)->type)->qual;
            di.nspace = OBJECT(di.resolvedObject)->nspace;
            di.name = OBJECT(di.resolvedObject)->name;
        } else if (TYPE_FUNC(OBJECT(di.resolvedObject)->type)->flags & FUNC_IS_CTOR) {
            di.thetype = TYPE(&void_ptr);
            di.nspace = OBJECT(di.resolvedObject)->nspace;
            di.name = OBJECT(di.resolvedObject)->name;
        } else {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            return;
        }

        if ((tk = lex()) == '(') {
            tk = lex();
            CDecl_ParseDirectFuncDecl(&di);
            if (IS_TYPE_FUNC(di.thetype)) {
                goto skipPastStuff;
            }
        } else {
            CError_Error(ERR_LPAREN_EXPECTED);
        }
    }

    if (di.storageclass) {
        if (tclass) {
            if ((int)di.storageclass == TK_STATIC) {
                disallowCVFlag = TRUE;
            } else {
                CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
            }
            di.storageclass = 0;
        } else {
            if ((int)di.storageclass != TK_STATIC && (int)di.storageclass != TK_EXTERN) {
                CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
                di.storageclass = 0;
            }
        }
    }

    CError_QualifierCheck(
        di.qual &
        ~(Q_CONST | Q_VOLATILE | Q_ASM | Q_PASCAL | Q_INLINE | Q_EXPLICIT | Q_IMPLICIT_WEAK |
          Q_WEAK | Q_ALIGNED_MASK | Q_INTERRUPT)
    );

    if (tk == TK_OPERATOR && di.parserOption) {
        CTempl_ParseConversionFunctionTemplate(
            &di, templateScope, params, tclass, startOffset, access
        );
        return;
    }

    if (!di.hasTypename) {
        if (tclass && IS_TYPE_CLASS(di.thetype) && TYPE_CLASS(di.thetype) == tclass && tk == '(') {
            CError_ASSERT(1418, cscope_current == tclass->nspace);
            CError_QualifierCheck(di.qual & ~(Q_INLINE | Q_EXPLICIT));

            di.thetype = TYPE(&void_ptr);
            di.isConstructor = TRUE;

            tk = lex();
            CDecl_ParseDirectFuncDecl(&di);

            if (IS_TYPE_FUNC(di.thetype)) {
                if (TYPE_FUNC(di.thetype)->args &&
                    !TYPE_FUNC(di.thetype)->args->next &&
                    TYPE_FUNC(di.thetype)->args->type == TYPE(tclass)) {
                    CError_Error(ERR_ILLEGAL_COPY_CONSTRUCTOR);
                    TYPE_FUNC(di.thetype)->args = NULL;
                }

                if (tclass->flags & CLASS_HAS_VBASES) {
                    CDecl_AddArgument(TYPE_FUNC(di.thetype), TYPE(&stsignedshort));
                }

                TYPE_FUNC(di.thetype)->flags |= FUNC_IS_CTOR;
                di.name = constructor_name_node;

                templateScope->scope->tparams = NULL;

                templfunc =
                    CTempl_DeclareTemplateFunction(&di, params, tclass, access, disallowCVFlag);
                CTempl_ParseTemplateFunction(templfunc, tclass, startOffset);

                return;
            } else {
                CError_Error(ERR_ILLEGAL_CONSTRUCTOR_DESTRUCTOR_DECLARATION);
            }
        }

        if (IS_TYPE_TEMPLATE(di.thetype)) {
            if (tk == '(' &&
                TYPE_TEMPLATE(di.thetype)->dtype == TEMPLDEP_QUALNAME &&
                (tclass2 = TYPE_CLASS(
                     CTemplTool_GetSelfRefTemplate(TYPE(TYPE_TEMPLATE(di.thetype)->u.qual.type))
                 )) &&
                TYPE_TEMPLATE(di.thetype)->u.qual.name == tclass2->classname) {
                if (tclass) {
                    CError_Error(ERR_ILLEGAL_TEMPLATE_DECLARATION);
                }

                di.thetype = TYPE(&void_ptr);
                di.isConstructor = TRUE;

                CTempl_SetupClassParamNameSpace(templateScope, tclass2);

                tk = lex();
                CDecl_ParseDirectFuncDecl(&di);

                if (IS_TYPE_FUNC(di.thetype)) {
                    di.name = constructor_name_node;
                    if (tclass2->flags & CLASS_HAS_VBASES) {
                        CDecl_AddArgument(TYPE_FUNC(di.thetype), TYPE(&stsignedshort));
                    }

                    TYPE_FUNC(di.thetype)->flags |= FUNC_IS_CTOR;
                    CTempl_ParseMember(params, TEMPL_CLASS(tclass2), &di, startOffset);
                    return;
                }

                CError_Error(ERR_ILLEGAL_CONSTRUCTOR_DESTRUCTOR_DECLARATION);
                return;
            }

            if (tk == TK_COLON_COLON &&
                TYPE_TEMPLATE(di.thetype)->dtype == TEMPLDEP_TEMPLATE &&
                (tclass2 = TYPE_CLASS(CTemplTool_GetSelfRefTemplate(di.thetype)))) {
                if (tclass) {
                    CError_Error(ERR_ILLEGAL_TEMPLATE_DECLARATION);
                }

                if ((tk = lex()) == '~') {
                    if ((tk = lex()) != TK_IDENTIFIER ||
                        tkidentifier != tclass2->classname ||
                        (tk = lex()) != '(') {
                        if (tk == '<') {
                            DeclInfo di2;

                            CPrep_UnLex();
                            tk = TK_IDENTIFIER;
                            tkidentifier = tclass2->classname;

                            memclrw(&di2, sizeof(di2));
                            CParser_GetDeclSpecs(&di2, FALSE);
                            if (tk != '(') {
                                CError_Error(ERR_ILLEGAL_CONSTRUCTOR_DESTRUCTOR_DECLARATION);
                            }

                            if (di2.thetype != TYPE(tclass2) &&
                                (!IS_TYPE_TEMPLATE(di2.thetype) ||
                                 CTemplTool_IsTemplate(TYPE_TEMPLATE(di2.thetype)) !=
                                     TEMPL_CLASS(tclass2))) {
                                CError_Error(ERR_ILLEGAL_CONSTRUCTOR_DESTRUCTOR_DECLARATION);
                            }
                        } else {
                            CError_Error(ERR_ILLEGAL_CONSTRUCTOR_DESTRUCTOR_DECLARATION);
                        }
                    }

                    di.thetype = TYPE(&void_ptr);

                    CTempl_SetupClassParamNameSpace(templateScope, tclass2);
                    tk = lex();
                    CDecl_ParseDirectFuncDecl(&di);

                    if (IS_TYPE_FUNC(di.thetype)) {
                        if (tclass2->sominfo) {
                            di.qual |= Q_VIRTUAL;
                        } else {
                            CDecl_AddArgument(TYPE_FUNC(di.thetype), TYPE(&stsignedshort));
                        }

                        di.name = destructor_name_node;
                        TYPE_FUNC(di.thetype)->flags |= FUNC_IS_DTOR;
                        CTempl_ParseMember(params, TEMPL_CLASS(tclass2), &di, startOffset);
                        return;
                    }

                    CError_Error(ERR_ILLEGAL_CONSTRUCTOR_DESTRUCTOR_DECLARATION);
                    return;
                }

                if (tk == TK_OPERATOR) {
                    CTempl_SetupClassParamNameSpace(templateScope, tclass2);
                    if (CMangler_OperatorName((tk = lex()))) {
                        CError_Error(ERR_IMPLICIT_INT_NO_LONGER_SUPPORTED_C);
                        return;
                    }

                    CError_QualifierCheck(di.qual & ~(Q_INLINE | Q_VIRTUAL));
                    conversion_type_name(&di);

                    type = di.thetype;
                    qual = di.qual;
                    CDecl_NewConvFuncType(&di);

                    if (CTemplTool_IsTemplateArgumentDependentType(type)) {
                        di.name = CTempl_FindConversionFuncName(tclass2, type, qual);
                        if (!di.name) {
                            CError_Error(ERR_NOT_STRUCT_UNION_CLASS_MEMBER, "conversion function");
                            return;
                        }
                    }
                    CTempl_ParseMember(params, TEMPL_CLASS(tclass2), &di, startOffset);
                    return;
                }

                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                return;
            }

            if (TYPE_TEMPLATE(di.thetype)->dtype == TEMPLDEP_QUALNAME && !di.isType) {
                CError_Warning(ERR_TYPENAME_MISSING_TEMPLATE_ARGUMENT_DEPENDENT_QUALIFIED);
            }
        }
    }

    di.templateParameters = params;
    di.templateScope = templateScope;
    scandeclarator(&di);
    ctempl_scanfuncparams = FALSE;

skipPastStuff:
    if (cscope_current->is_templ) {
        CError_ASSERT(1589, cscope_current == templateScope->scope);
        templateScope->scope->tparams = NULL;
    }

    if (!di.name) {
        CError_Error(ERR_ILLEGAL_TEMPLATE_DECLARATION);
        return;
    }

    if (di.nspace) {
        if (di.nspace->theclass) {
            if (tclass) {
                CError_Error(ERR_ILLEGAL_TEMPLATE_DECLARATION);
            }
            CTempl_ParseMember(params, TEMPL_CLASS(di.nspace->theclass), &di, startOffset);
            return;
        }

        if (!IS_TYPE_FUNC(di.thetype)) {
            CError_Error(ERR_ILLEGAL_TEMPLATE_DECLARATION);
            return;
        }

        CScope_FindName(di.nspace, di.name);
    } else {
        if (!IS_TYPE_FUNC(di.thetype)) {
            CError_Error(ERR_ILLEGAL_TEMPLATE_DECLARATION);
            return;
        }

        CScope_FindName(cscope_current, di.name);
    }

    nsol = CScope_FindName(di.nspace ? di.nspace : cscope_current, di.name);
    while (nsol) {
        object = OBJECT(nsol->object);
        if (object->otype == OT_OBJECT && IS_TEMPL_FUNC(object->type)) {
            templfunc = CTemplTool_GetFuncTempl(object);
            if (CTemplTool_EqualParams(templfunc->params, params, FALSE) &&
                is_typesame(object->type, di.thetype)) {
                if (tk != ';' && templfunc->stream.tokens) {
                    CError_Error(ERR_TEMPLATE_REDEFINED);
                }

                if (tk == '{' || tk == ':' || tk == TK_TRY) {
                    CError_ASSERT(1654, CTemplTool_EqualParams(templfunc->params, params, TRUE));
                }

                if (di.qual & Q_INLINE) {
                    object->qual |= Q_INLINE;
                }
                TYPE_FUNC(object->type)->args = TYPE_FUNC(di.thetype)->args;
                break;
            }
        }
        nsol = nsol->next;
    }

    if (!nsol) {
        if (di.nspace) {
            CError_Error(ERR_ILLEGAL_TEMPLATE_DECLARATION);
        }
        templfunc = CTempl_DeclareTemplateFunction(&di, params, tclass, access, disallowCVFlag);
    }

    CTempl_ParseTemplateFunction(templfunc, tclass, startOffset);
}

static void CTempl_ExplicitInstantiation(void) {
    Boolean instantiate; // NOTE: invented name
    short saveToken;
    Object* object;
    DeclInfo di;

    memclrw(&di, sizeof(di));
    di.allowTemplateArguments = TRUE;
    instantiate = TRUE;

    if (tk == TK_IDENTIFIER && !strcmp(tkidentifier->name, "__dont_instantiate")) {
        instantiate = FALSE;
        tk = lex();
    }

    switch (tk) {
        case TK_STRUCT:
        case TK_UNION:
        case TK_CLASS:
            memclrw(&di, sizeof(di));
            CParser_GetDeclSpecs(&di, FALSE);
            if (tk == ';') {
                if (IS_TEMPL_CLASS_INST(di.thetype)) {
                    CTempl_InstantiateTemplateClass(TYPE_CLASS(di.thetype));
                    if ((TYPE_CLASS(di.thetype)->flags & CLASS_COMPLETED) &&
                        !(TYPE_CLASS(di.thetype)->eflags & CLASS_EFLAGS_IMPORT)) {
                        if (instantiate) {
                            CTempl_GenClassInstance(TEMPL_CLASS_INST(di.thetype), TRUE);
                        } else {
                            TEMPL_CLASS_INST(di.thetype)->is_extern = TRUE;
                        }
                    } else {
                        CError_Error(
                            ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, TYPE_CLASS(di.thetype), 0
                        );
                    }
                } else {
                    CError_Error(ERR_ILLEGAL_EXPLICIT_TEMPLATE_INSTANTIATION);
                }
                return;
            }
            break;
        default:
            memclrw(&di, sizeof(di));
            CParser_GetDeclSpecs(&di, FALSE);
    }

    di.allowTemplateArguments = TRUE;

    scandeclarator(&di);

    saveToken = tk;

    di.templateFunction = NULL;
    if (di.name &&
        IS_TYPE_FUNC(di.thetype) &&
        (object = CDecl_GetFunctionObject(&di, di.nspace, NULL, TRUE)) &&
        IS_TYPE_FUNC(object->type) &&
        object->u.func.inst &&
        di.templateFunction) {
        if (!instantiate) {
            object->u.func.inst->is_extern = TRUE;
        } else {
            CTempl_GenFuncInstance(di.templateFunction, object->u.func.inst, TRUE);
        }
    } else {
        CError_Error(ERR_ILLEGAL_EXPLICIT_TEMPLATE_INSTANTIATION);
    }

    if (saveToken != ';') {
        CError_Error(ERR_SEMICOLON_EXPECTED);
    }
}

static void CTempl_ExplicitSpecialization(void) {
    Boolean hasParams; // NOTE: invented name
    TemplParam* params;
    int counter;
    DeclInfo di;
    TemplateScopeState templateScope;

    hasParams = FALSE;
    counter = 1;
    // the (int) comparison is the front end's token-compare idiom (CExpr, CScope and CDecl's too)
    while ((int)tk == TK_TEMPLATE) {
        if ((tk = lex()) != '<') {
            CError_Error(ERR_LESS_EXPECTED);
            break;
        }

        if ((tk = lex()) != '>') {
            if (!hasParams) {
                templateScope.scope = cscope_current;
                templateScope.linkedNamespace = NULL;
                cscope_current->tparams = NULL;
                hasParams = TRUE;
            }

            params = CTempl_ParseParamList(templateScope.scope, counter);
            if (tk != '>') {
                CError_Error(ERR_GREATER_EXPECTED);
            }
        } else {
            if (hasParams) {
                CError_Error(ERR_ILLEGAL_EXPLICIT_TEMPLATE_SPECIALIZATION);
            }
        }

        counter++;
        tk = lex();
    }

    if (hasParams) {
        SInt32 startOffset = -1;
        CTempl_ParseFunctionOrMemberTemplate(
            &templateScope, params, NULL, &startOffset, ACCESSPUBLIC, TRUE
        );
        return;
    }

    memclrw(&di, sizeof(di));
    di.requireTemplateClassMember = counter;
    di.allowTemplateArguments = TRUE;
    CParser_GetDeclSpecs(&di, TRUE);

    if (tk == ';') {
        if (IS_TEMPL_CLASS_INST(di.thetype)) {
            TEMPL_CLASS_INST(di.thetype)->is_specialized = TRUE;
        } else {
            CError_Error(ERR_ILLEGAL_EXPLICIT_TEMPLATE_SPECIALIZATION);
        }
    } else {
        scandeclaratorlist(&di);
        if ((tk != ';' && tk != '}') || di.requireTemplateClassMember) {
            CError_Error(ERR_ILLEGAL_EXPLICIT_TEMPLATE_SPECIALIZATION);
        }
    }

    if (hasParams) {
        templateScope.scope->tparams = NULL;
    }
}

void CTempl_Parse(TemplClass* templ, AccessType access) {
    TemplParam* params;
    UInt8 i;
    short mode;
    Boolean isPartial; // NOTE: invented name
    CScopeSave savedScope;
    TemplateScopeState templateScope;
    SInt32 startOffset;
    SInt32 savedState;
    UInt8 classDeclSpec;

    startOffset = CPrep_BrowserFileOffset();
    CScope_GetScope(&savedScope);

    if ((tk = lex()) != '<') {
        if (templ) {
            CError_Error(ERR_ILLEGAL_EXPLICIT_TEMPLATE_INSTANTIATION);
        }
        CTempl_ExplicitInstantiation();
        CScope_RestoreScope(&savedScope);
        return;
    }

    if ((tk = lex()) == '>') {
        if (templ) {
            CError_Error(ERR_ILLEGAL_EXPLICIT_TEMPLATE_SPECIALIZATION);
        }
        tk = lex();
        CTempl_ExplicitSpecialization();
        CScope_RestoreScope(&savedScope);
        return;
    }

    templateScope.scope = cscope_current;
    templateScope.linkedNamespace = NULL;
    cscope_current->tparams = NULL;
    i = CTempl_GetTemplateNestIndex(templateScope.scope);

    while (1) {
        params = CTempl_ParseParamList(templateScope.scope, i);
        if (tk != '>') {
            CError_Error(ERR_GREATER_EXPECTED);
        }

        if ((tk = lex()) != TK_TEMPLATE) {
            break;
        }

        if (templ) {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        }

        if ((tk = lex()) != '<') {
            CError_Error(ERR_LESS_EXPECTED);
        } else {
            tk = lex();
        }

        i++;
    }

    switch (tk) {
        case TK_CLASS:
            mode = CLASS_MODE_CLASS;
            break;
        case TK_UNION:
            mode = CLASS_MODE_UNION;
            break;
        case TK_STRUCT:
            mode = CLASS_MODE_STRUCT;
            break;
        default:
            mode = -1;
    }

    if (mode >= 0) {
        classDeclSpec = 0;
        isPartial = FALSE;

        CPrep_TokenStreamGetState(&savedState);
        if ((tk = lex()) == TK_UU_DECLSPEC) {
            CDecl_ParseClassDeclSpec(&classDeclSpec);
        }

        if (tk == TK_IDENTIFIER) {
            if ((tk = lex()) == '<') {
                if (setjmp(ctempl_parseparse) == 0) {
                    CTempl_MemberParseTemplArgList();
                    isPartial = TRUE;
                    tk = lex();
                }
            }

            switch (tk) {
                case ':':
                case ';':
                case '{':
                    CPrep_TokenStreamSetCurState(&savedState);
                    if (isPartial) {
                        CTemplClass_ParsePartialSpecialization(
                            &templateScope, params, mode, &startOffset
                        );
                    } else {
                        CTemplClass_ParseClass(&templateScope, params, mode, &startOffset);
                    }
                    goto done;
            }
        }

        CPrep_TokenStreamSetCurState(&savedState);
    }

    CTempl_ParseFunctionOrMemberTemplate(
        &templateScope, params, TYPE_CLASS(templ), &startOffset, access, FALSE
    );

done:
    templateScope.scope->tparams = NULL;
    CScope_RestoreScope(&savedScope);
}

void CTempl_ParseInstanceScopeFunction(Object* funcobj, TemplClassInst* inst, TypeClass* tclass) {
    TemplParam* params;
    NameSpace* nspace;
    TemplateMember* member;
    Object* parent;
    DeclInfo di;
    CScopeSave savedScope;
    TemplStack stack;

    params = inst->templ->templ__params;
    if (funcobj->qual & Q_IS_TEMPLATED) {
        for (member = CTemplClass_GetMasterTemplate(inst->templ)->members,
            parent = OBJECT_TEMPL(funcobj)->parent;
             member;
             member = member->next) {
            if (member->object == parent) {
                if (member->params) {
                    params = member->params;
                }
                break;
            }
        }
    }

    CTemplTool_PushInstance(&stack, NULL, funcobj);
    nspace = CTemplTool_InsertTemplateArgumentNameSpace(params, inst, &savedScope);
    if (tclass) {
        cscope_current = tclass->nspace;
    }

    memclrw(&di, sizeof(di));
    CFunc_ParseFuncDef(funcobj, &di, NULL, FALSE, FALSE, tclass ? cscope_current : NULL);

    CTemplTool_RemoveTemplateArgumentNameSpace(nspace, inst, &savedScope);
    CTemplTool_PopInstance(&stack);
}

// NOTE: invented names: templ, inst, isExplicit
Boolean CTempl_GenFuncInstance(
    TemplateFunction* templ, TemplFuncInstance* inst, Boolean isExplicit
) {
    Boolean saveDebugInfo;
    NameSpace* nspace;
    SInt32 streamState;
    DeclInfo di;
    TemplStack stack;

    if (inst->is_extern && !isExplicit) {
        return FALSE;
    }

    while (1) {
        if (templ->stream.tokens) {
            break;
        }
        if (!templ->original) {
            break;
        }
        templ = templ->original;
    }

    if (!templ->stream.tokens) {
        if (isExplicit) {
            CError_SetErrorToken(&templ->deftoken);
            CError_Error(ERR_CANNOT_INSTANTIATE, inst->object);
        }
        return FALSE;
    }

    inst->is_instantiated = TRUE;

    CPrep_StreamInsert(&templ->stream, &streamState);

    saveDebugInfo = copts.filesyminfo;
    if (copts.nosyminline || !templ->deftoken.tokenfile) {
        copts.filesyminfo = FALSE;
    }

    CError_ASSERT(2112, (tk = lex()) == '{' || tk == ':' || tk == TK_TRY);

    symdecltoken = *CPrep_CurStreamElement();

    if (copts.filesyminfo) {
        CPrep_NewFileOffsetInfo(&cparser_fileoffset, &templ->deftoken);
        symdecloffset = cparser_fileoffset.tokenline;
    }

    // an explicit instantiation (isExplicit) gets external linkage; an implicit one is weak
    if (inst->object->sclass != TK_STATIC) {
        if (isExplicit) {
            inst->object->sclass = TK_EXTERN;
        } else {
            inst->object->qual |= Q_WEAK;
        }
    }

    memclrw(&di, sizeof(di));
    di.file2 = templ->srcfile;
    di.file = CPrep_BrowserCurrentFile();
    di.sourceoffset = templ->startoffset;

    CTemplTool_PushInstance(&stack, NULL, inst->object);
    CTemplTool_MergeArgNames(TYPE_FUNC(templ->tfunc->type), TYPE_FUNC(inst->object->type));

    nspace = CTemplTool_SetupTemplateArgumentNameSpace(templ->params, inst->args, FALSE);
    nspace->parent = inst->object->nspace;
    inst->object->nspace = nspace;

    CTemplTool_SetupOuterTemplateArgumentNameSpace(nspace);
    CFunc_ParseFuncDef(inst->object, &di, NULL, FALSE, FALSE, nspace);
    CTemplTool_RemoveOuterTemplateArgumentNameSpace(nspace);

    inst->object->nspace = nspace->parent;

    CTemplTool_PopInstance(&stack);

    CPrep_StreamRemove(&templ->stream, &streamState);
    copts.filesyminfo = saveDebugInfo;

    if (di.file->recordbrowseinfo) {
        CBrowse_NewFunction(inst->object, di.file, di.file2, di.sourceoffset, templ->endoffset);
    }

    return TRUE;
}

// NOTE: invented names: templ, inst, tmemb, object, isExplicit
void CTempl_InstantiateMember(
    TemplClass* templ,
    TemplClassInst* inst,
    TemplateMember* tmemb,
    Object* object,
    Boolean isExplicit
) {
    Boolean saveDebugInfo;
    NameSpace* nspace;
    Boolean saveSourceLoc;

    CScopeSave savedScope;
    SInt32 savedState;
    DeclInfo di;
    TemplStack stack;

    CTemplTool_PushInstance(&stack, NULL, object);
    nspace = CTemplTool_InsertTemplateArgumentNameSpace(
        tmemb->params ? tmemb->params : templ->templ__params, inst, &savedScope
    );
    CPrep_StreamInsert(&tmemb->stream, &savedState);

    saveSourceLoc = gForceSourceLoc;
    gForceSourceLoc = TRUE;
    symdecltoken.tokenoffset = tmemb->startoffset;
    tk = lex();

    symdecltoken = *CPrep_CurStreamElement();

    saveDebugInfo = copts.filesyminfo;
    if (copts.filesyminfo) {
        CPrep_NewFileOffsetInfo(&cparser_fileoffset, &symdecltoken);
        symdecloffset = cparser_fileoffset.tokenline;
    }

    // an explicit instantiation (isExplicit) clears the storage class; an implicit one is weak
    if (object->sclass != TK_STATIC) {
        if (isExplicit) {
            object->sclass = 0;
        } else {
            object->qual |= Q_WEAK;
        }
    }

    memclrw(&di, sizeof(di));
    di.file2 = tmemb->srcfile;
    di.file = CPrep_BrowserCurrentFile();
    di.sourceoffset = tmemb->startoffset;

    switch (object->datatype) {
        case DFUNC:
        case DVFUNC:
            CTemplTool_MergeArgNames(TYPE_FUNC(tmemb->object->type), TYPE_FUNC(object->type));
            CFunc_ParseFuncDef(object, &di, TYPE_CLASS(inst), FALSE, FALSE, NULL);
            break;

        case DDATA:
            CDecl_CompleteType(object->type);
            CInit_InitializeData(object);
            break;

        default:
            CError_FATAL(2227);
    }

    CTemplTool_PopInstance(&stack);
    CTemplTool_RemoveTemplateArgumentNameSpace(nspace, inst, &savedScope);
    CPrep_StreamRemove(&tmemb->stream, &savedState);
    copts.filesyminfo = saveDebugInfo;
    gForceSourceLoc = saveSourceLoc;
}

// NOTE: invented names: inst, objtempl, isExplicit
static Boolean CTempl_GenMemberInstance(
    TemplClassInst* inst, ObjectTemplated* objtempl, Boolean isExplicit
) {
    TemplateMember* member;
    Object* parent = objtempl->parent;

    for (member = CTemplClass_GetMasterTemplate(inst->templ)->members; member;
         member = member->next) {
        if (member->object == parent) {
            CTempl_InstantiateMember(inst->templ, inst, member, OBJECT(objtempl), isExplicit);
            return TRUE;
        }
    }

    if (isExplicit) {
        CError_Warning(ERR_CANNOT_INSTANTIATE, objtempl);
    }
    return FALSE;
}

// NOTE: invented names: inst, isExplicit
static Boolean CTempl_GenClassInstance(TemplClassInst* inst, Boolean isExplicit) {
    Object* object;
    Boolean result;
    CScopeObjectIterator iter;

    result = FALSE;

    if (!isExplicit && inst->is_extern) {
        return FALSE;
    }

    CScope_InitObjectIterator(&iter, inst->theclass.nspace);
    while ((object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
        if (IS_TYPE_FUNC(object->type) && object->datatype != DALIAS) {
            if ((isExplicit || (object->flags & OBJECT_REFERENCED)) &&
                !(TYPE_FUNC(object->type)->flags & (FUNC_DEFINED | FUNC_AUTO_GENERATED)) &&
                CTempl_GenMemberInstance(inst, OBJECT_TEMPL(object), isExplicit) &&
                (TYPE_FUNC(object->type)->flags & FUNC_DEFINED)) {
                result = TRUE;
            }
        } else {
            if (!inst->static_instantiated &&
                object->datatype == DDATA &&
                !(object->qual & Q_INLINE_DATA) &&
                !(object->flags & OBJECT_DEFINED) &&
                CTempl_GenMemberInstance(inst, OBJECT_TEMPL(object), isExplicit)) {
                result = TRUE;
            }
        }
    }

    inst->static_instantiated = TRUE;
    return result;
}

Boolean CTempl_Instantiate(void) {
    Boolean result = FALSE;
    TemplClass* templ;
    TemplClassInst* inst;
    TemplPartialSpec* pspec;
    TemplateFunction* templf;
    TemplFuncInstance* instf;

    for (templ = ctempl_templates; templ; templ = templ->next) {
        for (inst = templ->instances; inst; inst = inst->next) {
            if ((inst->theclass.flags & CLASS_IS_TEMPL_INST) &&
                !inst->is_specialized &&
                CTempl_GenClassInstance(inst, FALSE)) {
                result = TRUE;
            }
        }

        for (pspec = templ->pspecs; pspec; pspec = pspec->next) {
            for (inst = pspec->templ->instances; inst; inst = inst->next) {
                if ((inst->theclass.flags & CLASS_IS_TEMPL_INST) &&
                    !inst->is_specialized &&
                    CTempl_GenClassInstance(inst, FALSE)) {
                    result = TRUE;
                }
            }
        }
    }

    for (templf = ctempl_templatefuncs; templf; templf = templf->next) {
        for (instf = templf->instances; instf; instf = instf->next) {
            if (!instf->is_instantiated &&
                !instf->is_specialized &&
                (instf->object->flags & OBJECT_REFERENCED) &&
                !(TYPE_FUNC(instf->object->type)->flags & FUNC_DEFINED)) {
                instf->is_instantiated = TRUE;
                if (CTempl_GenFuncInstance(templf, instf, FALSE)) {
                    result = TRUE;
                }
            }
        }
    }

    return result;
}

Boolean CTempl_InlineFunctionCheck(Object* funcobj) {
    TemplClassInst* inst;
    TemplateMember* member;
    Object* parent;

    CError_ASSERT(2422, IS_TYPE_FUNC(funcobj->type) && (funcobj->qual & Q_IS_TEMPLATED));

    if (!(TYPE_FUNC(funcobj->type)->flags & FUNC_DEFINED)) {
        inst = TEMPL_CLASS_INST(TYPE_METHOD(funcobj->type)->theclass);
        if (!inst->is_specialized) {
            parent = OBJECT_TEMPL(funcobj)->parent;
            if (parent->qual & Q_INLINE) {
                for (member = CTemplClass_GetMasterTemplate(inst->templ)->members; member;
                     member = member->next) {
                    funcobj->qual |= Q_INLINE;
                    if (member->object == parent) {
                        CTemplTool_MergeArgNames(
                            TYPE_FUNC(member->object->type), TYPE_FUNC(funcobj->type)
                        );
                        CInline_AddInlineFunctionAction(
                            funcobj, TYPE_CLASS(inst), &member->fileoffset, &member->stream, FALSE
                        );
                        return TRUE;
                    }
                }
            }
        }
    }

    return FALSE;
}

// The unit's statics are 2-byte packed, as in the original (MWCC lays out .bss with the packing in
// force at the end of the file)
#pragma pack(2)
