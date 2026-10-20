#include "compiler/CDecl.h"
#include "compiler/CABI.h"
#include "compiler/CBrowse.h"
#include "compiler/CClass.h"
#include "compiler/CError.h"
#include "compiler/CException.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CInline.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CObjC.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/CSOM.h"
#include "compiler/CTemplateClass.h"
#include "compiler/CTemplateFunc.h"
#include "compiler/CTemplateNew.h"
#include "compiler/CTemplateTools.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/templates.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include <string.h>

AccessType global_access;
FileOffsetInfo member_fileoffset;

static void scandirectdecl1(DeclInfo* declinfo);
void CheckDefaultArgs(FuncArg* args);

Type* CDecl_NewStructType(SInt32 size, SInt16 align) {
    TypeStruct* tstruct = galloc(sizeof(TypeStruct));
    memclrw(tstruct, sizeof(TypeStruct));

    tstruct->type = TYPESTRUCT;
    tstruct->size = size;
    tstruct->align = align;
    tstruct->stype = STRUCT_TYPE_STRUCT;

    return (Type*)tstruct;
}

Type* CDecl_NewArrayType(Type* type, SInt32 size) {
    TypePointer* tarray = galloc(sizeof(TypePointer));
    memclrw(tarray, sizeof(TypePointer));

    tarray->type = TYPEARRAY;
    tarray->size = size;
    tarray->target = type;
    tarray->qual = 0;

    return (Type*)tarray;
}

Type* CDecl_NewPointerType(Type* type) {
    TypePointer* tptr = galloc(sizeof(TypePointer));
    memclrw(tptr, sizeof(TypePointer));

    tptr->type = TYPEPOINTER;
    tptr->size = 4;
    tptr->target = type;

    return (Type*)tptr;
}

Type* CDecl_NewRefPointerType(Type* type) {
    TypePointer* tptr = galloc(sizeof(TypePointer));
    memclrw(tptr, sizeof(TypePointer));

    tptr->type = TYPEPOINTER;
    tptr->size = 4;
    tptr->target = type;
    tptr->qual = Q_REFERENCE;

    return (Type*)tptr;
}

TypeTemplDep* CDecl_NewTemplDepType(TypeTemplDepType tdt) {
    TypeTemplDep* t = galloc(sizeof(TypeTemplDep));
    memclrw(t, sizeof(TypeTemplDep));

    t->type = TYPETEMPLATE;
    t->size = 1;
    t->dtype = tdt;

    return t;
}

void CDecl_SetResultReg(TypeFunc* tfunc) {}

static void CDecl_SetFuncResultReg(TypeFunc* tfunc) {}

void CDecl_SetFuncFlags(TypeFunc* tfunc, UInt32 flags) {
    CDecl_SetResultReg(tfunc);
}

static void CDecl_ParseCPPFuncDecl(TypeFunc* tfunc) {
    for (;;) {
        if ((int)tk == TK_CONST) {
            if (tfunc->flags & FUNC_CONST) {
                CError_Warning(ERR_ILLEGAL_USE, "const");
            }
            tfunc->flags |= FUNC_CONST;
            tk = lex();
        } else if ((int)tk == TK_VOLATILE) {
            if (tfunc->flags & FUNC_VOLATILE) {
                CError_Warning(ERR_ILLEGAL_USE, "volatile");
            }
            tfunc->flags |= FUNC_VOLATILE;
            tk = lex();
        } else {
            break;
        }
    }

    if ((int)tk == TK_THROW) {
        CExcept_ScanExceptionSpecification(tfunc);
    }
}

void CDecl_NewConvFuncType(DeclInfo* declinfo) {
    TypeFunc* tfunc;

    if (tk != '(') {
        CError_Error(ERR_LPAREN_EXPECTED);
    } else {
        tk = lex();
    }

    if (tk == TK_VOID) {
        tk = lex();
    }

    if (tk != ')') {
        CError_Error(ERR_RPAREN_EXPECTED);
    } else {
        tk = lex();
    }

    tfunc = galloc(sizeof(TypeFunc));
    memclrw(tfunc, sizeof(TypeFunc));
    declinfo->name = CMangler_ConversionFuncName(declinfo->thetype, declinfo->qual);
    tfunc->type = TYPEFUNC;
    tfunc->functype = declinfo->thetype;
    tfunc->qual = declinfo->qual & (Q_CONST | Q_VOLATILE);
    tfunc->flags = FUNC_CONVERSION;
    declinfo->isType = FALSE;
    CDecl_SetFuncFlags(tfunc, 1);
    CDecl_ParseCPPFuncDecl(tfunc);

    declinfo->thetype = (Type*)tfunc;
    declinfo->qual &= ~(Q_CONST | Q_VOLATILE);
    declinfo->storageclass = 0;
}

void CDecl_CompleteType(Type* type) {
    switch (type->type) {
        case TYPEPOINTER:
            if ((TYPE_POINTER(type)->qual & Q_REFERENCE) &&
                IS_TYPE_CLASS(TYPE_POINTER(type)->target)) {
                type = TYPE_POINTER(type)->target;
                break;
            }
            return;
        case TYPEARRAY:
            do {
                type = TYPE_POINTER(type)->target;
            } while (IS_TYPE_ARRAY(type));
            if (IS_TYPE_CLASS(type)) {
                break;
            }
            return;
        case TYPECLASS:
            break;
        default:
            return;
    }

    if ((TYPE_CLASS(type)->flags & (CLASS_COMPLETED | CLASS_IS_TEMPL_INST)) ==
        CLASS_IS_TEMPL_INST) {
        CTempl_InstantiateTemplateClass(TYPE_CLASS(type));
    }
}

Boolean IsCompleteType(Type* type) {
    switch (type->type) {
        case TYPEVOID:
            CError_Error(ERR_ILLEGAL_USE_VOID);
            return FALSE;
        case TYPEFUNC:
            CError_Error(ERR_ILLEGAL_TYPE);
            return FALSE;
        case TYPESTRUCT:
            if (!type->size) {
                CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, type, 0);
                return FALSE;
            }
            return TRUE;
        case TYPECLASS:
            if (!(TYPE_CLASS(type)->flags & CLASS_COMPLETED) &&
                (!(TYPE_CLASS(type)->flags & CLASS_IS_TEMPL_INST) ||
                 !CTempl_InstantiateTemplateClass(TYPE_CLASS(type)))) {
                CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, type, 0);
                return FALSE;
            }
            return TRUE;
        default:
            if (!type->size) {
                CError_Error(ERR_DATA_TYPE_INCOMPLETE);
                return FALSE;
            }
            return TRUE;
    }
}

Boolean CanAllocObject(Type* type) {
    switch (type->type) {
        case TYPEVOID:
            CError_Error(ERR_ILLEGAL_USE_VOID);
            return FALSE;
        case TYPEFUNC:
            CError_Error(ERR_ILLEGAL_TYPE);
            return FALSE;
        case TYPECLASS:
            if (TYPE_CLASS(type)->flags & CLASS_ABSTRACT) {
                CError_AbstractClassError(TYPE_CLASS(type));
                return FALSE;
            }
        default:
            return TRUE;
    }
}

Boolean CanCreateObject(Type* type) {
    if (!CanAllocObject(type)) {
        return FALSE;
    }

    if (IS_TYPE_CLASS(type)) {
        if (TYPE_CLASS(type)->flags & CLASS_HANDLEOBJECT) {
            CError_Error(ERR_ILLEGAL_USE_HANDLEOBJECT);
            return FALSE;
        }
        if (TYPE_CLASS(type)->objcinfo) {
            CError_Error(ERR_ILLEGAL_USE_OBJECTIVE_C_OBJECT);
            return FALSE;
        }
    }

    return TRUE;
}

static Boolean CanCreateHandleMemberObject(Type* type) {
    while (IS_TYPE_ARRAY(type)) {
        type = TYPE_POINTER(type)->target;
    }

    if (!CanCreateObject(type)) {
        return FALSE;
    }

    if (IS_TYPE_CLASS(type)) {
        if (CClass_Destructor(TYPE_CLASS(type)) || CClass_Constructor(TYPE_CLASS(type))) {
            CError_Error(ERR_ILLEGAL_USE_HANDLEOBJECT);
            return FALSE;
        }
    }

    return TRUE;
}

void makethetypepointer(DeclInfo* declinfo, UInt32 qual) {
    declinfo->thetype = CDecl_NewPointerType(declinfo->thetype);
    TYPE_POINTER(declinfo->thetype)->qual = qual;
}

void CDecl_AddThisPointerArgument(TypeFunc* tfunc, TypeClass* tclass) {
    Type* ptype;
    FuncArg* arg;

    ptype = CDecl_NewPointerType(tclass->sominfo == NULL ? (Type*)tclass : &stvoid);
    TYPE_POINTER(ptype)->qual = Q_CONST;

    arg = CParser_NewFuncArg();
    arg->name = this_name_node;
    arg->type = ptype;
    if (tfunc->flags & FUNC_CONST) {
        arg->qual |= Q_CONST;
    }
    if (tfunc->flags & FUNC_VOLATILE) {
        arg->qual |= Q_VOLATILE;
    }
    arg->next = tfunc->args;
    tfunc->args = arg;
}

void CDecl_MakePTMFuncType(TypeFunc* tfunc) {
    Type* cvoidp;
    FuncArg* arg1;
    FuncArg* arg2;

    cvoidp = CDecl_NewPointerType(&stvoid);
    TYPE_POINTER(cvoidp)->qual = Q_CONST;

    arg1 = CParser_NewFuncArg();
    arg1->name = this_name_node;
    arg1->type = cvoidp;
    if (tfunc->flags & FUNC_CONST) {
        arg1->qual |= Q_CONST;
    }
    if (tfunc->flags & FUNC_VOLATILE) {
        arg1->qual |= Q_VOLATILE;
    }

    arg2 = CParser_NewFuncArg();
    arg2->name = this_name_node;
    arg2->type = cvoidp;
    arg2->qual = Q_CONST;

    arg1->next = tfunc->args;
    arg2->next = arg1;
    tfunc->args = arg2;
    tfunc->flags |= FUNC_PTM;
}

void CDecl_AddArgument(TypeFunc* tfunc, Type* argtype) {
    FuncArg* arg = CParser_NewFuncArg();
    arg->type = argtype;

    arg->next = tfunc->args;
    tfunc->args = arg;

    if (arg->next && arg->next->type == &stvoid) {
        arg->next = NULL;
    }
}

Boolean CDecl_CheckArrayIntegr(Type* type) {
    if (!IsCompleteType(type)) {
        return FALSE;
    }

    if (IS_TYPE_CLASS(type) && TYPE_CLASS(type)->sominfo) {
        CError_Error(ERR_SOM_CLASS_ARRAYS_NOT_SUPPORTED);
        return FALSE;
    }

    if (IS_TYPE_REFERENCE(type)) {
        CError_Error(ERR_ILLEGAL_AMPERSAND_REFERENCE);
        return FALSE;
    }

    return CanCreateObject(type);
}

static Boolean checkfuncintegr(Type* type) {
    if (IS_TYPE_VOID(type)) {
        return TRUE;
    }

    if (IS_TYPE_CLASS(type) && TYPE_CLASS(type)->sominfo) {
        CError_Error(ERR_FUNCTIONS_CANNOT_RETURN_SOM_CLASSES);
        return FALSE;
    }

    return CanCreateObject(type);
}

void CDecl_ParseDirectFuncDecl(DeclInfo* declinfo) {
    FuncArg* list;
    TypeFunc* tfunc;

    if (tk == ')') {
        if (!copts.cplusplus) {
            list = &oldstyle;
        } else {
            list = NULL;
        }
        tk = lex();
    } else {
        list = parameter_type_list(declinfo);
        if (tk != ')') {
            CError_ErrorSkip(ERR_RPAREN_EXPECTED);
        } else {
            tk = lex();
        }
    }

    tfunc = galloc(sizeof(TypeFunc));
    memclrw(tfunc, sizeof(TypeFunc));
    tfunc->type = TYPEFUNC;
    tfunc->args = list;
    if (declinfo->qual & Q_PASCAL) {
        declinfo->qual &= ~Q_PASCAL;
        tfunc->flags = FUNC_PASCAL;
    }

    if (copts.cplusplus) {
        CDecl_ParseCPPFuncDecl(tfunc);
        if (declinfo->storageclass == TK_TYPEDEF && tfunc->exspecs) {
            CError_Error(ERR_ILLEGAL_EXCEPTION_SPECIFICATION);
        }
    }

    scandirectdecl1(declinfo);
    if (!checkfuncintegr(declinfo->thetype)) {
        declinfo->thetype = &stvoid;
    }

    tfunc->functype = declinfo->thetype;
    tfunc->qual = declinfo->qual & (Q_CONST | Q_VOLATILE);
    declinfo->thetype = (Type*)tfunc;
    declinfo->qual &= ~(Q_CONST | Q_VOLATILE);
    declinfo->isType = FALSE;
}

static void scandirectdecl1(DeclInfo* declinfo) {
    Boolean unsized; // NOTE: invented name
    CInt64 len;
    ENode* expr;

    unsized = FALSE;
    if (tk == '[') {
        if ((tk = lex()) == ']') {
            len = cint64_zero;
            tk = lex();
            unsized = TRUE;
        } else {
            if (!declinfo->isNewExpression || declinfo->hasArrayDimension) {
                expr = CExpr_IntegralConstOrDepExpr();
                if (!ENODE_IS(expr, EINTCONST)) {
                    TypeTemplDep* ttempl;
                    if (tk != ']') {
                        CError_ErrorSkip(ERR_RBRACKET_EXPECTED);
                    } else {
                        tk = lex();
                    }
                    declinfo->hasArrayDimension = TRUE;
                    scandirectdecl1(declinfo);
                    if (!CDecl_CheckArrayIntegr(declinfo->thetype)) {
                        declinfo->thetype = (Type*)&stsignedchar;
                    }
                    ttempl = CDecl_NewTemplDepType(TEMPLDEP_ARRAY);
                    ttempl->u.array.type = declinfo->thetype;
                    ttempl->u.array.index = CInline_CopyExpression(expr, CopyModeGlobal);
                    declinfo->thetype = (Type*)ttempl;
                    return;
                }
                len = expr->data.intval;
                if (CInt64_IsNegative(&len)) {
                    CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
                    len = cint64_one;
                } else if (CInt64_IsZero(&len)) {
                    if (!copts.ANSIstrict && declinfo->allowZeroLengthArray) {
                        unsized = TRUE;
                    } else {
                        CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
                        len = cint64_one;
                    }
                }
            } else {
                len = cint64_one;
                expr = expression();
                if (IS_TYPE_INT(expr->rtype)) {
                    if (!ENODE_IS(expr, EINTCONST)) {
                        declinfo->arrayBound = expr;
                    } else {
                        len = expr->data.intval;
                    }
                } else {
                    CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
                }
            }

            if (tk != ']') {
                CError_ErrorSkip(ERR_RBRACKET_EXPECTED);
            } else {
                tk = lex();
            }
        }

        declinfo->hasArrayDimension = TRUE;
        scandirectdecl1(declinfo);

        if (!unsized && !CDecl_CheckArrayIntegr(declinfo->thetype)) {
            declinfo->thetype = (Type*)&stsignedchar;
        }

        if (!declinfo->thetype->size &&
            CTemplTool_IsTemplateArgumentDependentType(declinfo->thetype)) {
            TypeTemplDep* ttempl = CDecl_NewTemplDepType(TEMPLDEP_ARRAY);
            ttempl->u.array.type = declinfo->thetype;
            ttempl->u.array.index = CInline_CopyExpression(
                intconstnode((Type*)&stsignedint, CInt64_GetULong(&len)), CopyModeGlobal
            );
            declinfo->thetype = (Type*)ttempl;
            return;
        }

        declinfo->thetype =
            CDecl_NewArrayType(declinfo->thetype, declinfo->thetype->size * CInt64_GetULong(&len));
    } else if (tk == '(') {
        if (!copts.cplusplus ||
            !declinfo->name ||
            IS_TYPE_VOID(declinfo->thetype) ||
            CParser_TryParamList(declinfo->thetype->type != TYPECLASS)) {
            tk = lex();
            CDecl_ParseDirectFuncDecl(declinfo);
        }
    }
}

static void substitute_type(Type* type1, Type* type2) {
    SInt32 oldsize;

    while (1) {
        switch (type1->type) {
            case TYPEPOINTER:
                if (TYPE_POINTER(type1)->target == &stillegal) {
                    TYPE_POINTER(type1)->target = type2;
                    type1->size = 4;
                    return;
                }
                type1 = TYPE_POINTER(type1)->target;
                break;
            case TYPEMEMBERPOINTER:
                if (TYPE_MEMBER_POINTER(type1)->ty1 == &stillegal) {
                    TYPE_MEMBER_POINTER(type1)->ty1 = type2;
                    if (IS_TYPE_FUNC(type2)) {
                        CDecl_MakePTMFuncType(TYPE_FUNC(type2));
                        type1->size = 12;
                    } else {
                        type1->size = 4;
                    }
                    return;
                }
                type1 = TYPE_MEMBER_POINTER(type1)->ty1;
                break;
            case TYPEARRAY:
                if (TYPE_POINTER(type1)->target == &stillegal) {
                    if (!CDecl_CheckArrayIntegr(type2)) {
                        type2 = (Type*)&stsignedchar;
                    }
                    type1->size *= type2->size;
                    TYPE_POINTER(type1)->target = type2;
                    return;
                }
                oldsize = TYPE_POINTER(type1)->target->size;
                substitute_type(TYPE_POINTER(type1)->target, type2);
                if (oldsize != TYPE_POINTER(type1)->target->size && oldsize != 0) {
                    type1->size = TYPE_POINTER(type1)->target->size * (type1->size / oldsize);
                }
                return;
            case TYPEFUNC:
                if (TYPE_FUNC(type1)->functype == &stillegal) {
                    if (!checkfuncintegr(type2)) {
                        type2 = &stvoid;
                    }
                    TYPE_FUNC(type1)->functype = type2;
                    CDecl_SetFuncResultReg((TypeFunc*)type1);
                    return;
                }
                type1 = TYPE_FUNC(type1)->functype;
                break;
            case TYPETEMPLATE:
                if (TYPE_TEMPLATE(type1)->dtype == TEMPLDEP_ARRAY) {
                    if (TYPE_TEMPLATE(type1)->u.array.type == &stillegal) {
                        if (!CDecl_CheckArrayIntegr(type2)) {
                            type2 = (Type*)&stsignedchar;
                        }
                        TYPE_TEMPLATE(type1)->u.array.type = type2;
                        return;
                    }
                    type1 = TYPE_TEMPLATE(type1)->u.array.type;
                } else {
                    CError_Error(ERR_ILLEGAL_TYPE);
                    return;
                }
                break;
            default:
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                return;
        }
    }
}

static void scandecl(DeclInfo* declinfo) {
    Type* oldtype;
    Type* newtype;

    oldtype = declinfo->thetype;
    declinfo->thetype = &stillegal;
    scandeclarator(declinfo);

    if (tk != ')') {
        CError_ErrorSkip(ERR_RPAREN_EXPECTED);
    } else {
        tk = lex();
    }

    newtype = declinfo->thetype;
    if (newtype == &stillegal) {
        declinfo->thetype = oldtype;
        scandirectdecl1(declinfo);
        return;
    }

    declinfo->thetype = oldtype;
    scandirectdecl1(declinfo);
    substitute_type(newtype, declinfo->thetype);
    declinfo->thetype = newtype;
}

static Boolean CDecl_ParseOperatorDecl(DeclInfo* declinfo) {
    if (declinfo->operator_token) {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        return FALSE;
    }

    declinfo->operator_token = 0;
    if (!CParser_ParseOperatorName(
            &declinfo->operator_token, declinfo->parserOption && cscope_current->theclass, FALSE
        )) {
        return FALSE;
    }

    if (!declinfo->operator_token) {
        conversion_type_name(declinfo);
        tkidentifier = CMangler_ConversionFuncName(declinfo->thetype, declinfo->qual);
        declinfo->isConversionOperator = TRUE;
    }
    return TRUE;
}

static Boolean CDecl_IsEnumClassTypeOrRef(Type* type) {
    if (IS_TYPE_CLASS(type) || IS_TYPE_ENUM(type)) {
        return TRUE;
    }
    if (!IS_TYPE_REFERENCE(type)) {
        return FALSE;
    }
    type = TYPE_POINTER(type)->target;
    return IS_TYPE_CLASS(type) || IS_TYPE_ENUM(type);
}

// TODO: an invented helper (fakematch): the original computes this test to a value before
// branching, as an inlined int function's result is; the test written in place, as a ternary or
// any other spelling, compiles otherwise
static int IsSingleArg(FuncArg* arg) {
    return arg != &elipsis && arg->next == NULL;
}

// NOTE: invented names: declinfo, isMember, binary_operator
static Boolean CDecl_CheckOperatorType(DeclInfo* declinfo, Boolean isMember) {
    FuncArg* args;
    FuncArg* secondarg;
    Type* functype;
    short argCount;
    // TODO: a Boolean (dword stores, byte tests), tested through (Boolean) casts (fakematch): only
    // an int gets the original's register allocation (a Boolean spills argCount instead)
    int isMethod;

    if (!IS_TYPE_FUNC(declinfo->thetype)) {
        CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
        return FALSE;
    }

    functype = TYPE_FUNC(declinfo->thetype)->functype;
    args = TYPE_FUNC(declinfo->thetype)->args;
    if (args) {
        if (args != &elipsis && args != &oldstyle) {
            argCount = 1;
            if (args->dexpr) {
                switch (declinfo->operator_token) {
                    case TK_NEW:
                    case TK_DELETE:
                    case TK_NEW_ARRAY:
                    case TK_DELETE_ARRAY:
                        break;
                    default:
                        CError_Error(ERR_ILLEGAL_DEFAULT_ARGUMENTS);
                }
            }

            secondarg = args->next;
            if (secondarg) {
                if (IsSingleArg(secondarg)) {
                    argCount = 2;
                } else {
                    argCount = 3;
                }
                if (secondarg->dexpr) {
                    switch (declinfo->operator_token) {
                        case '(':
                        case TK_NEW:
                        case TK_DELETE:
                        case TK_NEW_ARRAY:
                        case TK_DELETE_ARRAY:
                            break;
                        default:
                            CError_Error(ERR_ILLEGAL_DEFAULT_ARGUMENTS);
                    }
                }
            }
        } else {
            argCount = 3;
        }
    } else {
        CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
        return FALSE;
    }

    isMethod = isMember &&
        IS_TYPEFUNC_METHOD(TYPE_FUNC(declinfo->thetype)) &&
        !TYPE_METHOD(declinfo->thetype)->is_static;

    switch (declinfo->operator_token) {
        case TK_NEW:
        case TK_NEW_ARRAY:
            if ((Boolean)isMethod ||
                !is_typesame(functype, TYPE(&void_ptr)) ||
                argCount < 1 ||
                args->type != CABI_GetSizeTType()) {
                CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
                return FALSE;
            }
            return TRUE;
        case TK_DELETE:
        case TK_DELETE_ARRAY:
            if ((Boolean)isMethod ||
                !IS_TYPE_VOID(functype) ||
                argCount < 1 ||
                !is_typesame(args->type, TYPE(&void_ptr))) {
                CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
                return FALSE;
            }
            return TRUE;
        case '=':
            if (!(Boolean)isMethod) {
                CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
                return FALSE;
            }
            break;
        case '(':
            if (!(Boolean)isMethod) {
                CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
                return FALSE;
            }
            return TRUE;
        case '[':
            if (!(Boolean)isMethod) {
                CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
                return FALSE;
            }
            break;
        case TK_ARROW:
            if (argCount != 1 || !(Boolean)isMethod) {
                CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
                return FALSE;
            }
            return TRUE;
        case TK_INCREMENT:
        case TK_DECREMENT:
            if (argCount == 2 && secondarg->type != TYPE(&stsignedint)) {
                CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
                return FALSE;
            }
            break;
    }

    if (isMember && !(Boolean)isMethod) {
        CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
        return FALSE;
    }

    switch (declinfo->operator_token) {
        case '&':
        case '*':
        case '+':
        case '-':
        case TK_INCREMENT:
        case TK_DECREMENT:
            if (argCount != 1) {
                goto binary_operator;
            }
        case '!':
        case '~':
            if (argCount == 1) {
                if (isMember || CDecl_IsEnumClassTypeOrRef(args->type)) {
                    return TRUE;
                }
            }
            break;
        case '%':
        case ',':
        case '/':
        case '<':
        case '=':
        case '>':
        case '[':
        case '^':
        case '|':
        case TK_MULT_ASSIGN:
        case TK_DIV_ASSIGN:
        case TK_MOD_ASSIGN:
        case TK_ADD_ASSIGN:
        case TK_SUB_ASSIGN:
        case TK_SHL_ASSIGN:
        case TK_SHR_ASSIGN:
        case TK_AND_ASSIGN:
        case TK_XOR_ASSIGN:
        case TK_OR_ASSIGN:
        case TK_LOGICAL_OR:
        case TK_LOGICAL_AND:
        case TK_LOGICAL_EQ:
        case TK_LOGICAL_NE:
        case TK_LESS_EQUAL:
        case TK_GREATER_EQUAL:
        case TK_SHL:
        case TK_SHR:
        case TK_ARROW:
        case TK_DOT_STAR:
        case TK_ARROW_STAR:
binary_operator:
            if (argCount == 2) {
                if (isMember ||
                    CDecl_IsEnumClassTypeOrRef(args->type) ||
                    CDecl_IsEnumClassTypeOrRef(secondarg->type)) {
                    return TRUE;
                }
            }
            break;
    }

    CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
    return FALSE;
}

static void scandirectdeclarator(DeclInfo* declinfo, NameSpace* nspace) {
    HashNameNode* saveident;
    CScopeSave scopesave;
    Boolean isOperator; // NOTE: invented name

    if (nspace) {
        CScope_SetNameSpaceScope(nspace, &scopesave);
    }

    if (tk == '(') {
        if ((tk = lex()) == ')') {
            if (declinfo->isTypeID) {
                CDecl_ParseDirectFuncDecl(declinfo);
                if (nspace) {
                    CScope_RestoreScope(&scopesave);
                }
                return;
            } else {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                if (nspace) {
                    CScope_RestoreScope(&scopesave);
                }
                return;
            }
        }

        if (!((int)tk >= TK_AUTO && (int)tk <= TK_BYREF)) {
            if (!(tk == TK_IDENTIFIER && CScope_PossibleTypeName(tkidentifier))) {
                scandecl(declinfo);
                if (nspace) {
                    CScope_RestoreScope(&scopesave);
                }
                return;
            } else {
                saveident = tkidentifier;
                switch (lookahead()) {
                    case ')':
                    case ',':
                        break;
                    default:
                        tkidentifier = saveident;
                        scandecl(declinfo);
                        if (nspace) {
                            CScope_RestoreScope(&scopesave);
                        }
                        return;
                }
            }
        }

        if (declinfo->name) {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        }

        CDecl_ParseDirectFuncDecl(declinfo);
        if (nspace) {
            CScope_RestoreScope(&scopesave);
        }
        return;
    }

    if (nspace) {
        if (tk == TK_OPERATOR) {
            if (!CDecl_ParseOperatorDecl(declinfo)) {
                CScope_RestoreScope(&scopesave);
                return;
            }

            if (declinfo->isConversionOperator) {
                declinfo->nspace = nspace;
                declinfo->name = tkidentifier;
                if (nspace) {
                    CScope_RestoreScope(&scopesave);
                }

                if (tk == '(') {
                    tk = lex();
                    CDecl_ParseDirectFuncDecl(declinfo);
                    if (IS_TYPE_FUNC(declinfo->thetype)) {
                        TYPE_FUNC(declinfo->thetype)->flags |= FUNC_CONVERSION;
                    } else {
                        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    }
                } else {
                    CError_Error(ERR_LPAREN_EXPECTED);
                }
                return;
            }

            isOperator = TRUE;
        } else if (tk != TK_IDENTIFIER) {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
            CScope_RestoreScope(&scopesave);
            return;
        } else {
            isOperator = FALSE;
        }

        if (declinfo->name) {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            CScope_RestoreScope(&scopesave);
            return;
        }

        declinfo->nspace = nspace;
        declinfo->name = tkidentifier;
        if (!isOperator) {
            tk = lex();
        }
    } else if (tk == TK_IDENTIFIER) {
        if (declinfo->name) {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        }
        declinfo->name = tkidentifier;
        tk = lex();
    } else if (tk == TK_OPERATOR) {
        if (!CDecl_ParseOperatorDecl(declinfo)) {
            return;
        }
        declinfo->name = tkidentifier;
    }

    if (tk == '<' && declinfo->allowTemplateArguments) {
        declinfo->expltargs = CTempl_ParseUncheckTemplArgs(NULL, FALSE);
        declinfo->has_expltargs = TRUE;
        declinfo->allowTemplateArguments = FALSE;
        tk = lex();
    }

    scandirectdecl1(declinfo);

    if (nspace) {
        CScope_RestoreScope(&scopesave);
    }
}

void makememberpointertype(DeclInfo* declinfo, TypeClass* tclass, UInt32 qual) {
    TypeMemberPointer* tmemp;
    TypeFunc* tfunc;

    if (tclass->flags & CLASS_HANDLEOBJECT) {
        CError_Error(ERR_ILLEGAL_USE_HANDLEOBJECT);
        declinfo->thetype = (Type*)&stsignedint;
        return;
    }
    if (tclass->sominfo) {
        CError_Error(ERR_POINTER_TO_MEMBER_NOT_SUPPORTED_SOM);
        declinfo->thetype = (Type*)&stsignedint;
        return;
    }

    tmemp = galloc(sizeof(TypeMemberPointer));
    memclrw(tmemp, sizeof(TypeMemberPointer));
    tmemp->type = TYPEMEMBERPOINTER;
    tmemp->ty2 = (Type*)tclass;
    tmemp->qual = qual;

    if (IS_TYPE_FUNC(declinfo->thetype)) {
        tfunc = galloc(sizeof(TypeFunc));
        *tfunc = *TYPE_FUNC(declinfo->thetype);
        tmemp->ty1 = (Type*)tfunc;
        tmemp->size = 12;
        CDecl_MakePTMFuncType(tfunc);
    } else {
        tmemp->size = 4;
        tmemp->ty1 = declinfo->thetype;
    }
    declinfo->thetype = (Type*)tmemp;
}

// NOTE: invented names: declinfo, nspace, scanDeclarator
void CDecl_ScanPointer(DeclInfo* declinfo, NameSpace* nspace, Boolean scanDeclarator) {
    NameResult pr;
    UInt32 qual;

    while (1) {
        qual = (tk == '&') ? Q_REFERENCE : 0;

        for (tk = lex();; tk = lex()) {
            switch (tk) {
                case TK_CONST:
                    if (qual & Q_CONST) {
                        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    }
                    qual |= Q_CONST;
                    continue;
                case TK_VOLATILE:
                    if (qual & Q_VOLATILE) {
                        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    }
                    qual |= Q_VOLATILE;
                    continue;
                case TK_RESTRICT:
                    if (qual & Q_RESTRICT) {
                        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    }
                    qual |= Q_RESTRICT;
                    continue;
                default:
                    break;
            }
            break;
        }

        if (IS_TYPE_REFERENCE(declinfo->thetype) ||
            ((qual & Q_REFERENCE) && IS_TYPE_VOID(declinfo->thetype))) {
            CError_Error(ERR_ILLEGAL_AMPERSAND_REFERENCE);
            return;
        }

        if (nspace) {
            makememberpointertype(declinfo, nspace->theclass, qual);
            nspace = NULL;
        } else {
            makethetypepointer(declinfo, qual);
        }

        switch (tk) {
            case '*':
                continue;
            case '&':
                if (!copts.cplusplus) {
                    if (scanDeclarator) {
                        scandirectdeclarator(declinfo, NULL);
                    }
                    return;
                }
                continue;
            case TK_IDENTIFIER:
                if (!copts.cplusplus) {
                    break;
                }
                if (copts.cpp_extensions &&
                    cscope_current->theclass &&
                    cscope_current->theclass->classname == tkidentifier &&
                    lookahead() == TK_COLON_COLON) {
                    tk = lex();
                    tk = lex();
                    break;
                }
            case TK_COLON_COLON:
                if (CScope_ParseQualifiedNameSpace(&pr, TRUE, FALSE)) {
                    if ((nspace = pr.nspace)) {
                        if (nspace->theclass && tk == '*') {
                            continue;
                        }
                    } else {
                        if (pr.type && IS_TYPE_TEMPLATE(pr.type) && declinfo->templateParameters) {
                            if (CTempl_IsQualifiedMember(declinfo, pr.type, &nspace)) {
                                scandirectdeclarator(declinfo, nspace);
                                return;
                            }
                            declinfo->templateType = pr.type;
                            return;
                        }
                        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    }
                }
                break;
        }
        break;
    }

    if (scanDeclarator) {
        scandirectdeclarator(declinfo, nspace);
    }
}

static void CDecl_TemplatePTM(DeclInfo* declinfo, Type* type) {
    TypeMemberPointer* tmemp = galloc(sizeof(TypeMemberPointer));
    tmemp->type = TYPEMEMBERPOINTER;
    if (IS_TYPE_FUNC(declinfo->thetype)) {
        CDecl_MakePTMFuncType((TypeFunc*)declinfo->thetype);
        tmemp->size = 12;
    } else {
        tmemp->size = 4;
    }

    tmemp->ty1 = declinfo->thetype;
    tmemp->ty2 = type;
    tmemp->qual = 0;
    declinfo->thetype = (Type*)tmemp;
}

void scandeclarator(DeclInfo* declinfo) {
    NameResult pr;
    NameSpace* nspace;

    switch (tk) {
        case '&':
            if (!copts.cplusplus) {
                break;
            }
        case '*':
            CDecl_ScanPointer(declinfo, NULL, TRUE);
            if (tk == TK_ASM) {
                CMach_ParseGlobalRegisterVariable(declinfo);
            }
            if (tk == TK_UU_ATTRIBUTE_UU) {
                CParser_ParseAttribute(NULL, declinfo);
            }
            return;
        case TK_IDENTIFIER:
            if (!copts.cplusplus) {
                break;
            }
        case TK_COLON_COLON:
            if (CScope_ParseQualifiedNameSpace(&pr, TRUE, FALSE)) {
                nspace = pr.nspace;
                if (nspace) {
                    if (nspace->theclass && tk == '*') {
                        CDecl_ScanPointer(declinfo, nspace, TRUE);
                    } else {
                        scandirectdeclarator(declinfo, nspace);
                    }
                    return;
                }
                if (pr.type && IS_TYPE_TEMPLATE(pr.type)) {
                    if (declinfo->templateParameters &&
                        CTempl_IsQualifiedMember(declinfo, pr.type, &nspace)) {
                        scandirectdeclarator(declinfo, nspace);
                        return;
                    } else if (declinfo->templateParameters && tk == TK_OPERATOR) {
                        declinfo->templateType = pr.type;
                        return;
                    } else if ((tk = lex()) == TK_COLON_COLON && (tk = lex()) == '*') {
                        CDecl_TemplatePTM(declinfo, pr.type);
                        tk = lex();
                        break;
                    } else if (declinfo->templateParameters) {
                        declinfo->templateType = pr.type;
                        return;
                    }
                }
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            break;
    }

    scandirectdeclarator(declinfo, NULL);
    if (tk == TK_ASM) {
        CMach_ParseGlobalRegisterVariable(declinfo);
    }
    if (tk == TK_UU_ATTRIBUTE_UU) {
        CParser_ParseAttribute(NULL, declinfo);
    }
}

void conversion_type_name(DeclInfo* declinfo) {
    DeclInfo subdeclinfo;
    NameResult pr;

    memclrw(&subdeclinfo, sizeof(DeclInfo));
    CParser_GetDeclSpecs(&subdeclinfo, FALSE);

    switch (tk) {
        case '&':
        case '*':
            CDecl_ScanPointer(&subdeclinfo, NULL, FALSE);
            break;
        case TK_IDENTIFIER:
        case TK_COLON_COLON:
            if (CScope_ParseQualifiedNameSpace(&pr, FALSE, FALSE)) {
                NameSpace* nspace = pr.nspace;
                if (nspace && nspace->theclass && tk == '*') {
                    CDecl_ScanPointer(&subdeclinfo, nspace, FALSE);
                } else {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                }
            }
            break;
    }

    declinfo->name = subdeclinfo.name;
    declinfo->thetype = subdeclinfo.thetype;
    declinfo->qual |= subdeclinfo.qual;
}

static void scaninlinefunc(Object* obj) {
    short array[256];
    short count;
    CInt64 val;

    if (tk == '{') {
        tk = lex();
        count = 0;
        while (1) {
            if (count >= 256) {
                CError_Error(ERR_ILLEGAL_FUNCTION_DEFINITION);
                count = 255;
            }
            val = CExpr_IntegralConstExpr();
            array[count++] = CInt64_GetULong(&val);
            if (tk != '}') {
                if (tk != ',') {
                    CError_Error(ERR_COMMA_EXPECTED);
                }
                tk = lex();
            } else {
                tk = lex();
                break;
            }
        }
    } else {
        val = CExpr_IntegralConstExpr();
        array[0] = CInt64_GetULong(&val);
        count = 1;
    }

    obj->datatype = DINLINEFUNC;
    obj->u.ifunc.size = count * 2;
    obj->u.ifunc.data = galloc(obj->u.ifunc.size);
    obj->u.ifunc.xrefs = NULL;
    memcpy(obj->u.ifunc.data, array, obj->u.ifunc.size);

    if (tk != ';') {
        CError_Error(ERR_SEMICOLON_EXPECTED);
    }
}

// NOTE: invented names: OverloadMode_NonMember, OverloadMode_StaticMember, OverloadMode_Member,
// OverloadMode_MemberWithoutThis
// What CDecl_OverloadFunctionObject does with a method found under the name.
typedef enum {
    // a non-member declaration: any method is an error
    OverloadMode_NonMember,
    // a static member declaration: only static methods are candidates
    OverloadMode_StaticMember,
    // a non-static member declaration (with its this argument): only non-static methods
    OverloadMode_Member,
    // a member's out-of-class declaration without its this argument: a non-static method is
    // compared past its own, and skipped when that is cv-qualified
    OverloadMode_MemberWithoutThis
} OverloadMode;

// NOTE: invented names: list, declinfo, isNew, mode, matchTemplate
static Object* CDecl_OverloadFunctionObject(
    NameSpaceObjectList* list,
    DeclInfo* declinfo,
    Boolean* isNew,
    OverloadMode mode,
    Boolean matchTemplate
) {
    NameSpaceObjectList* scan;
    TypeFunc* scanfunc;
    TypeFunc* tfunc;
    FuncArg* args;
    FuncArg* scanargs;
    Object* obj;
    Boolean hasTemplate;
    short compareresult;

    if (isNew) {
        *isNew = FALSE;
    }

    tfunc = (TypeFunc*)declinfo->thetype;
    args = tfunc->args;
    hasTemplate = FALSE;
    for (scan = list; scan; scan = scan->next) {
        obj = OBJECT(scan->object);
        if (obj->otype != OT_OBJECT) {
            continue;
        }

        scanfunc = TYPE_FUNC(obj->type);
        if (!IS_TYPE_FUNC(scanfunc)) {
            continue;
        }

        scanargs = scanfunc->args;
        if (scanfunc->flags & FUNC_IS_TEMPL) {
            hasTemplate = TRUE;
        }

        if (IS_TYPEFUNC_METHOD(scanfunc)) {
            switch (mode) {
                case OverloadMode_NonMember:
                    CError_Error(ERR_ILLEGAL_FUNCTION_OVERLOADING);
                    break;
                case OverloadMode_StaticMember:
                    if (!TYPE_METHOD(scanfunc)->is_static) {
                        continue;
                    }
                    break;
                case OverloadMode_Member:
                    if (TYPE_METHOD(scanfunc)->is_static) {
                        continue;
                    }
                    break;
                case OverloadMode_MemberWithoutThis:
                    if (!TYPE_METHOD(scanfunc)->is_static) {
                        if (scanargs->qual & Q_CV) {
                            continue;
                        }
                        scanargs = scanargs->next;
                    }
                    break;
            }
        } else {
            if (mode) {
                CError_Error(ERR_ILLEGAL_FUNCTION_OVERLOADING);
            }
        }

        compareresult = CParser_CompareArgLists(args, scanargs);
        if (compareresult == 1) {
            if (scanfunc->flags & FUNC_CONVERSION) {
                if (!(tfunc->flags & FUNC_CONVERSION)) {
                    CError_Error(ERR_ILLEGAL_FUNCTION_OVERLOADING);
                    break;
                }
                if (!is_typesame(tfunc->functype, scanfunc->functype)) {
                    continue;
                }
                if ((tfunc->qual & (Q_CONST | Q_PASCAL)) !=
                    (scanfunc->qual & (Q_CONST | Q_PASCAL))) {
                    continue;
                }
                if ((tfunc->flags & FUNC_CALL_CONV_MASK) !=
                    (scanfunc->flags & FUNC_CALL_CONV_MASK)) {
                    CError_Error(ERR_ILLEGAL_FUNCTION_OVERLOADING);
                    break;
                }
                if (tfunc->exspecs || scanfunc->exspecs) {
                    CExcept_CompareSpecifications(tfunc->exspecs, scanfunc->exspecs);
                }
                return obj;
            }

            if (tfunc->flags & FUNC_CONVERSION) {
                CError_Error(ERR_ILLEGAL_FUNCTION_OVERLOADING);
                break;
            }

            if (!is_typesame(tfunc->functype, scanfunc->functype) ||
                ((tfunc->qual & (Q_CONST | Q_PASCAL)) != (scanfunc->qual & (Q_CONST | Q_PASCAL))) ||
                ((tfunc->flags & FUNC_CALL_CONV_MASK) != (scanfunc->flags & FUNC_CALL_CONV_MASK))) {
                CError_Error(ERR_ILLEGAL_FUNCTION_OVERLOADING);
                break;
            }

            if (tfunc->exspecs || scanfunc->exspecs) {
                if (obj->name != newp_fobj->name &&
                    obj->name != newa_fobj->name &&
                    obj->name != delp_fobj->name &&
                    obj->name != dela_fobj->name) {
                    CExcept_CompareSpecifications(tfunc->exspecs, scanfunc->exspecs);
                }
            }

            return obj;
        } else if (compareresult == 2) {
            CError_Error(ERR_ILLEGAL_FUNCTION_OVERLOADING);
            break;
        }
    }

    if (hasTemplate && (matchTemplate || declinfo->requireTemplateClassMember)) {
        if ((obj = CTempl_TemplateFunctionCheck(declinfo, list))) {
            return obj;
        }
    }

    if (!isNew) {
        CError_Error(ERR_ILLEGAL_FUNCTION_OVERLOADING);
        return NULL;
    }

    if (declinfo->nspace) {
        CError_Error(ERR_NAME_NOT_BEEN_DECLARED_NAMESPACE_CLASS);
    }

    *isNew = TRUE;
    obj = CParser_NewFunctionObject(declinfo);
    CheckDefaultArgs(TYPE_FUNC(obj->type)->args);

    if (tfunc->flags & FUNC_PASCAL) {
        for (scan = list; scan; scan = scan->next) {
            if (scan->object->otype == OT_OBJECT && IS_TYPE_FUNC(OBJECT(scan->object)->type)) {
                if (TYPE_FUNC(OBJECT(scan->object)->type)->flags & FUNC_PASCAL) {
                    CError_Error(ERR_PASCAL_FUNCTION_CANNOT_OVERLOADED);
                }
            }
        }
    }

    if (copts.cplusplus && declinfo->is_extern_c) {
        for (scan = list; scan; scan = scan->next) {
            Object* scanobj = OBJECT(scan->object);
            if (scanobj->otype == OT_OBJECT && !(scanobj->qual & Q_MANGLE_NAME)) {
                CError_Error(ERR_ILLEGAL_FUNCTION_OVERLOADING);
            }
        }
    }

    CScope_AddObject(cscope_current, declinfo->name, OBJ_BASE(obj));
    if (cscope_current->theclass &&
        (cscope_current->theclass->flags & CLASS_IS_TEMPL) &&
        CTemplTool_IsTemplateArgumentDependentType(declinfo->thetype)) {
        CTemplClass_RegisterObjectDef(TEMPL_CLASS(cscope_current->theclass), OBJ_BASE(obj));
    }

    return obj;
}

// TODO: one walker per loop and the cached dexpr give the original's registers; the plain body
// (one do/while over both lists, the same control flow) compiles otherwise
void MergeDefaultArgs(FuncArg* a, FuncArg* b) {
    FuncArg* other;
    ENode* defaultExpr;
    FuncArg* mergeArgs;
    FuncArg* clearOtherAgain;
    FuncArg* clearArgs;
    FuncArg* clearOther;
    FuncArg* arg;
    FuncArg* clearArgsAgain;
    FuncArg* mergeOther;

    if (a == &oldstyle || b == &oldstyle) {
        return;
    }

    arg = a;
    other = b;
    while (arg != NULL && other != NULL) {
        if (arg->dexpr != NULL) {
            while (other != NULL) {
                if (other->dexpr != NULL) {
                    for (clearArgs = a; clearArgs != NULL; clearArgs = clearArgs->next) {
                        clearArgs->dexpr = NULL;
                    }
                    for (clearOther = b; clearOther != NULL; clearOther = clearOther->next) {
                        clearOther->dexpr = NULL;
                    }
                    CError_Error(ERR_ILLEGAL_DEFAULT_ARGUMENTS);
                    return;
                }
                other = other->next;
            }
            break;
        }
        if (other->dexpr != NULL) {
            for (;;) {
                arg = arg->next;
                other = other->next;
                if (arg == NULL || arg == &elipsis) {
                    break;
                }
                if (!(((defaultExpr = arg->dexpr) == NULL || other->dexpr == NULL) &&
                      (defaultExpr != NULL || other->dexpr != NULL))) {
                    for (clearArgsAgain = a; clearArgsAgain != NULL;
                         clearArgsAgain = clearArgsAgain->next) {
                        clearArgsAgain->dexpr = NULL;
                    }
                    for (clearOtherAgain = b; clearOtherAgain != NULL;
                         clearOtherAgain = clearOtherAgain->next) {
                        clearOtherAgain->dexpr = NULL;
                    }
                    CError_Error(ERR_ILLEGAL_DEFAULT_ARGUMENTS);
                    return;
                }
            }
            break;
        }
        arg = arg->next;
        other = other->next;
    }
    mergeArgs = a;
    mergeOther = b;
    while (mergeArgs != NULL && mergeOther != NULL) {
        if (mergeOther->dexpr != NULL) {
            mergeArgs->dexpr = mergeOther->dexpr;
        } else {
            mergeOther->dexpr = mergeArgs->dexpr;
        }
        mergeArgs = mergeArgs->next;
        mergeOther = mergeOther->next;
    }
}

void CheckDefaultArgs(FuncArg* args) {
    FuncArg* scan;

    scan = args;
    while (scan && !scan->dexpr) {
        scan = scan->next;
    }

    while (scan && scan != &elipsis && scan != &oldstyle) {
        if (!scan->dexpr) {
            scan = args;
            while (scan) {
                scan->dexpr = NULL;
                scan = scan->next;
            }
            CError_Error(ERR_ILLEGAL_DEFAULT_ARGUMENTS);
            return;
        }
        scan = scan->next;
    }
}

// A redeclaration's parameter written as an array where the earlier one was a pointer, or the
// reverse, takes the earlier one's is_array.
// NOTE: invented name (no public in Wii's symbols)
static void CDecl_MergeArrayArgFlags(FuncArg* a, FuncArg* b) {
    if (!b) {
        return;
    }

    while (a && a != &elipsis && a != &oldstyle && b && b != &elipsis && b != &oldstyle) {
        if ((IS_TYPE_POINTER_ONLY(a->type) && b->is_array) ||
            (a->is_array && IS_TYPE_POINTER_ONLY(b->type))) {
            b->is_array = a->is_array;
        }
        a = a->next;
        b = b->next;
    }
}

// NOTE: invented names: obj, declinfo, isNew
static void CDecl_FuncRedeclCheck(Object* obj, DeclInfo* declinfo, Boolean isNew) {
    if (declinfo->storageclass == TK_STATIC && obj->sclass != TK_STATIC) {
        if (copts.cplusplus) {
            CError_Error(ERR_INCONSISTENT_LINKAGE_EXTERN_OBJECT_REDECLARED_AS);
        } else {
            obj->sclass = TK_STATIC;
        }
    }

    obj->qual |= declinfo->qual;
    if (isNew) {
        CheckDefaultArgs(TYPE_FUNC(obj->type)->args);
    } else {
        MergeDefaultArgs(TYPE_FUNC(obj->type)->args, TYPE_FUNC(declinfo->thetype)->args);
    }

    if (!declinfo->oldStyleParameters) {
        CDecl_MergeArrayArgFlags(TYPE_FUNC(obj->type)->args, TYPE_FUNC(declinfo->thetype)->args);
        TYPE_FUNC(obj->type)->args = TYPE_FUNC(declinfo->thetype)->args;
    }
}

// NOTE: invented names: declinfo, nspace, firstDecl, matchTemplate, newtype, isNew
Object* CDecl_GetFunctionObject(
    DeclInfo* declinfo, NameSpace* nspace, Boolean* firstDecl, Boolean matchTemplate
) {
    NameSpace* nspace2;
    Type* type;
    Object* obj;
    NameSpaceObjectList* list;
    // TODO: the union stands in for however the original wrote this local: as a TypeMemberFunc
    // with its TypeFunc part copied through a cast pointer, MWCC computes the address into a
    // register first, where the original stores straight into the local
    union {
        TypeMemberFunc method;
        TypeFunc func;
    } newtype;
    Boolean mismatch;
    Boolean isNew;

    mismatch = FALSE;
    if (firstDecl) {
        *firstDecl = FALSE;
    }

    nspace2 = declinfo->nspace;
    if (!nspace2) {
        nspace2 = cscope_current;
    }

    CError_QualifierCheck(
        declinfo->qual &
        ~(Q_ALIGNED_MASK | Q_WEAK | Q_IMPLICIT_WEAK | Q_INLINE | Q_PASCAL | Q_ASM | Q_VOLATILE |
          Q_CONST | Q_INTERRUPT)
    );
    switch (TYPE_FUNC(declinfo->thetype)->functype->type) {
        case TYPEFUNC:
        case TYPEARRAY:
            CError_Error(ERR_ILLEGAL_FUNCTION_RETURN_TYPE);
            TYPE_FUNC(declinfo->thetype)->functype = TYPE(&stsignedint);
            break;
        default:
            break;
    }

    if (nspace2->theclass) {
        CError_ASSERT(2018, declinfo->name);
        if (!nspace2->theclass->size) {
            CDecl_CompleteType(TYPE(nspace2->theclass));
        }
        if (!(list = CScope_GetLocalObject(nspace2, declinfo->name))) {
            CError_Error(ERR_UNDEFINED_IDENTIFIER, declinfo->name->name);
            return NULL;
        }

        obj = OBJECT(list->object);
        type = obj->type;
        if (!IS_TYPE_FUNC(type)) {
            CError_Error(
                ERR_IDENTIFIER_REDECLARED_WAS_DECLARED_AS_NOW,
                CError_GetObjectName(obj),
                type,
                obj->qual,
                declinfo->thetype,
                declinfo->qual
            );
            return NULL;
        }

        if (declinfo->has_expltargs) {
            return CTempl_TemplateFunctionCheck(declinfo, list);
        }

        if (declinfo->requireTemplateClassMember ||
            (list->next && list->next->object->otype == OT_OBJECT)) {
            if (TYPE_FUNC(declinfo->thetype)->flags & (FUNC_CONST | FUNC_VOLATILE)) {
                // a method type of the declaration's own gets the this argument
                TypeMemberFunc* tmeth = galloc(sizeof(TypeMemberFunc));
                memclrw(tmeth, sizeof(TypeMemberFunc));
                *TYPE_FUNC(tmeth) = *TYPE_FUNC(declinfo->thetype);
                tmeth->theclass = nspace2->theclass;
                tmeth->flags |= FUNC_METHOD;
                CDecl_AddThisPointerArgument(TYPE_FUNC(tmeth), nspace2->theclass);
                declinfo->thetype = TYPE(tmeth);
                obj = CDecl_OverloadFunctionObject(
                    list, declinfo, NULL, OverloadMode_Member, matchTemplate
                );
                if (!obj) {
                    return NULL;
                }
            } else {
                obj = CDecl_OverloadFunctionObject(
                    list, declinfo, NULL, OverloadMode_MemberWithoutThis, matchTemplate
                );
                if (!obj) {
                    return NULL;
                }
                if (!TYPE_METHOD(obj->type)->is_static) {
                    CDecl_AddThisPointerArgument(TYPE_FUNC(declinfo->thetype), nspace2->theclass);
                }
            }
        } else {
            if (TYPE_METHOD(type)->is_static) {
                if (nspace2->theclass->sominfo) {
                    CSOM_FixNewDeleteFunctype(TYPE_FUNC(declinfo->thetype));
                }
            } else {
                CDecl_AddThisPointerArgument(TYPE_FUNC(declinfo->thetype), nspace2->theclass);
            }

            if (copts.cpp_extensions) {
                declinfo->qual |= obj->qual & (Q_PASCAL | Q_CONST);
                TYPE_FUNC(declinfo->thetype)->qual |=
                    TYPE_FUNC(obj->type)->qual & (Q_PASCAL | Q_CONST);
                TYPE_FUNC(declinfo->thetype)->flags |=
                    TYPE_FUNC(obj->type)->flags & (FUNC_FLAGS_4000000 | FUNC_FLAGS_10000000);
            }

            if (!is_typesame(declinfo->thetype, obj->type) ||
                (declinfo->qual & (Q_PASCAL | Q_CONST)) != (obj->qual & (Q_PASCAL | Q_CONST))) {
                newtype.method = *TYPE_METHOD(obj->type);
                newtype.func = *TYPE_FUNC(declinfo->thetype);
                newtype.method.flags |= FUNC_METHOD;
                CError_Error(
                    ERR_IDENTIFIER_REDECLARED_WAS_DECLARED_AS_NOW,
                    CError_GetObjectName(obj),
                    obj->type,
                    obj->qual,
                    &newtype.method,
                    declinfo->qual
                );
            }

            if (TYPE_FUNC(declinfo->thetype)->exspecs || TYPE_FUNC(obj->type)->exspecs) {
                CExcept_CompareSpecifications(
                    TYPE_FUNC(declinfo->thetype)->exspecs, TYPE_FUNC(obj->type)->exspecs
                );
            }
        }

        CDecl_FuncRedeclCheck(obj, declinfo, FALSE);
        if (declinfo->requireTemplateClassMember) {
            if (obj->nspace->theclass && !(obj->nspace->theclass->flags & CLASS_IS_TEMPL_INST)) {
                CError_Error(ERR_ILLEGAL_EXPLICIT_TEMPLATE_SPECIALIZATION);
            }
            declinfo->requireTemplateClassMember = FALSE;
        }
    } else {
        if (TYPE_FUNC(declinfo->thetype)->flags & (FUNC_VOLATILE | FUNC_CONST)) {
            CError_Error(ERR_ILLEGAL_CV_FUNCTION_QUALIFIER_SEQUENCE);
        }

        if (declinfo->operator_token && !CDecl_CheckOperatorType(declinfo, FALSE)) {
            return NULL;
        }

        list = CScope_GetLocalObject(nspace2, declinfo->name);
        if (declinfo->has_expltargs) {
            return CTempl_TemplateFunctionCheck(declinfo, list);
        }

        if (list) {
            if (copts.cplusplus) {
                obj = CDecl_OverloadFunctionObject(
                    list, declinfo, &isNew, OverloadMode_NonMember, matchTemplate
                );
                if (!obj) {
                    return NULL;
                }
                if (firstDecl) {
                    *firstDecl = isNew;
                }
                if (nspace) {
                    obj->nspace = nspace;
                }
            } else {
                obj = OBJECT(list->object);
                if (!is_typesame(declinfo->thetype, obj->type) ||
                    (declinfo->qual & (Q_CONST | Q_PASCAL)) != (obj->qual & (Q_CONST | Q_PASCAL))) {
                    CError_Error(
                        ERR_IDENTIFIER_REDECLARED_WAS_DECLARED_AS_NOW,
                        CError_GetObjectName(obj),
                        obj->type,
                        obj->qual,
                        declinfo->thetype,
                        declinfo->qual
                    );
                    mismatch = TRUE;
                    if (!IS_TYPE_FUNC(obj->type)) {
                        return NULL;
                    }
                }
            }

            if (!mismatch && firstDecl) {
                CDecl_FuncRedeclCheck(obj, declinfo, *firstDecl);
            }
        } else {
            if (declinfo->nspace) {
                CError_Error(ERR_NAME_NOT_BEEN_DECLARED_NAMESPACE_CLASS);
            }

            if (declinfo->has_expltargs) {
                if (declinfo->name) {
                    CError_Error(ERR_UNDEFINED_IDENTIFIER, declinfo->name->name);
                } else {
                    CError_Error(ERR_ILLEGAL_FUNCTION_DEFINITION);
                }
            }

            obj = CParser_NewFunctionObject(declinfo);
            if (nspace) {
                obj->nspace = nspace;
            }
            if (firstDecl) {
                *firstDecl = TRUE;
            } else {
                CError_Error(ERR_ILLEGAL_FUNCTION_DEFINITION);
            }

            CheckDefaultArgs(TYPE_FUNC(obj->type)->args);
            CScope_AddObject(nspace2, declinfo->name, OBJ_BASE(obj));
        }
    }

    return obj;
}

void CDecl_TypedefDeclarator(DeclInfo* declinfo) {
    NameSpaceObjectList* list;
    ObjType* objt;
    NameSpace* nspace;

    nspace = declinfo->nspace;
    if (!nspace) {
        nspace = cscope_current;
    }

    CError_QualifierCheck(
        declinfo->qual & ~(Q_ALIGNED_MASK | Q_REFERENCE | Q_PASCAL | Q_VOLATILE | Q_CONST)
    );
    if (declinfo->missingTypeSpecifier || declinfo->hasParameterNames) {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
    }
    if (declinfo->operator_token) {
        CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
    }

    objt = NULL;
    list = CScope_FindName(nspace, declinfo->name);
    if (list) {
        switch (list->object->otype) {
            case OT_TYPE:
                objt = OBJ_TYPE(list->object);
                break;
            case OT_TYPETAG:
                break;
            case OT_NAMESPACE:
                CError_Error(ERR_ILLEGAL_USE_NAMESPACE_NAME);
                return;
            case OT_ENUMCONST:
            case OT_OBJECT:
                CError_Error(ERR_ILLEGAL_NAME_OVERLOADING);
                return;
            default:
                CError_FATAL(2221);
        }
    }

    if (objt) {
        const UInt32 mask = Q_ALIGNED_MASK | Q_REFERENCE | Q_PASCAL | Q_VOLATILE | Q_CONST;
        if (!is_typesame(objt->type, declinfo->thetype) ||
            (objt->qual & mask) != (declinfo->qual & mask)) {
            CError_Error(
                ERR_IDENTIFIER_REDECLARED_WAS_DECLARED_AS_NOW,
                declinfo->name->name,
                objt->type,
                objt->qual,
                declinfo->thetype,
                declinfo->qual
            );
        } else if (!copts.cplusplus && (copts.pedantic || copts.ANSIstrict)) {
            if (copts.pedantic) {
                CError_Warning(ERR_IDENTIFIER_REDECLARED, declinfo->name->name);
            } else {
                CError_Error(ERR_IDENTIFIER_REDECLARED, declinfo->name->name);
            }
        }
    } else {
        objt = galloc(sizeof(ObjType));
        memclrw(objt, sizeof(ObjType));
        objt->otype = OT_TYPE;
        objt->access = ACCESSPUBLIC;
        objt->type = declinfo->thetype;
        objt->qual = declinfo->qual;
        CScope_AddObject(nspace, declinfo->name, OBJ_BASE(objt));

        if (nspace->theclass &&
            (nspace->theclass->flags & CLASS_IS_TEMPL) &&
            CTemplTool_IsTemplateArgumentDependentType(declinfo->thetype)) {
            CTemplClass_RegisterObjectDef(TEMPL_CLASS(nspace->theclass), OBJ_BASE(objt));
        }

        if (copts.cplusplus) {
            if (IS_TYPE_CLASS(declinfo->thetype) &&
                IsTempName(TYPE_CLASS(declinfo->thetype)->classname)) {
                TYPE_CLASS(declinfo->thetype)->classname = declinfo->name;
                TYPE_CLASS(declinfo->thetype)->nspace->name = declinfo->name;
            }
            if (IS_TYPE_ENUM(declinfo->thetype) &&
                IsTempName(TYPE_ENUM(declinfo->thetype)->enumname)) {
                TYPE_ENUM(declinfo->thetype)->enumname = declinfo->name;
            }
        }

        if (cparamblkptr->browseoptions.recordTypedefs && declinfo->file->recordbrowseinfo) {
            CBrowse_NewTypedef(
                nspace,
                declinfo->name,
                declinfo->file,
                declinfo->file2,
                declinfo->sourceoffset,
                CPrep_BrowserFileOffset()
            );
        }
    }
}

// NOTE: invented names: declinfo, access, isMember, sametype
static void CDecl_DataDeclarator(DeclInfo* declinfo, AccessType access, Boolean isMember) {
    NameSpaceObjectList* list;
    Object* obj;
    NameSpace* nspace;
    Boolean sametype;
    ENode* expr;

    nspace = declinfo->nspace;
    if (!nspace) {
        nspace = cscope_current;
    }

    CError_QualifierCheck(
        declinfo->qual &
        ~(Q_ALIGNED_MASK | Q_WEAK | Q_IMPLICIT_WEAK | Q_PASCAL | Q_VOLATILE | Q_CONST)
    );
    if (declinfo->missingTypeSpecifier || declinfo->hasParameterNames) {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
    }
    if (declinfo->operator_token) {
        CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
    }

    obj = NULL;
    list = CScope_FindName(nspace, declinfo->name);
    if (list) {
        switch (list->object->otype) {
            case OT_OBJECT:
                obj = OBJECT(list->object);
                if (isMember) {
                    CError_Error(ERR_IDENTIFIER_REDECLARED, declinfo->name->name);
                }
                break;
            case OT_TYPETAG:
                break;
            case OT_NAMESPACE:
                CError_Error(ERR_ILLEGAL_USE_NAMESPACE_NAME);
                return;
            case OT_ENUMCONST:
            case OT_TYPE:
                CError_Error(ERR_ILLEGAL_NAME_OVERLOADING);
                break;
            case OT_MEMBERVAR:
                CError_Error(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
                break;
            default:
                CError_FATAL(2346);
        }
    }

    if (copts.cplusplus) {
        if (!isMember) {
            CDecl_CompleteType(declinfo->thetype);
        }
        switch (declinfo->storageclass) {
            case TK_EXTERN:
                if (tk == '=' || tk == '(') {
                    declinfo->storageclass = 0;
                }
                break;
            case 0:
                if (CParser_IsConst(declinfo->thetype, declinfo->qual)) {
                    if ((!obj && !nspace->theclass) ||
                        (obj && obj->sclass != TK_EXTERN && !obj->nspace->theclass)) {
                        declinfo->storageclass = TK_STATIC;
                    }
                }
                break;
        }
    } else {
        if (declinfo->storageclass == TK_EXTERN && tk == '=') {
            declinfo->storageclass = 0;
        }
    }

    if (IS_TYPE_ARRAY(declinfo->thetype) &&
        !declinfo->thetype->size &&
        !declinfo->storageclass &&
        tk != '=') {
        declinfo->storageclass = TK_EXTERN;
    }

    if (obj) {
        if ((!obj->type->size || !declinfo->thetype->size) &&
            IS_TYPE_ARRAY(declinfo->thetype) &&
            IS_TYPE_ARRAY(obj->type)) {
            sametype = is_typesame(
                TYPE_POINTER(declinfo->thetype)->target, TYPE_POINTER(obj->type)->target
            );
        } else {
            sametype = is_typesame(declinfo->thetype, obj->type);
        }

        if (!sametype ||
            (obj->qual & (Q_PASCAL | Q_VOLATILE | Q_CONST)) !=
                (declinfo->qual & (Q_PASCAL | Q_VOLATILE | Q_CONST))) {
            CError_Error(
                ERR_IDENTIFIER_REDECLARED_WAS_DECLARED_AS_NOW,
                CError_GetObjectName(obj),
                obj->type,
                obj->qual,
                declinfo->thetype,
                declinfo->qual
            );
        }

        if (obj->qual & Q_INLINE_DATA) {
            if (tk == ',' || tk == ';') {
                return;
            }
            CError_Error(ERR_OBJECT_REDEFINED, obj);
        }

        if (declinfo->storageclass != TK_EXTERN) {
            if (obj->sclass != TK_EXTERN &&
                declinfo->storageclass &&
                obj->sclass != declinfo->storageclass) {
                CError_Error(ERR_OBJECT_REDEFINED, obj);
            }

            if (sametype) {
                obj->sclass = declinfo->storageclass;
                obj->qual |= declinfo->qual;
                if (declinfo->thetype->size) {
                    obj->type = declinfo->thetype;
                }
            }

            CParser_UpdateObject(obj, declinfo);
        } else {
            isMember = TRUE;
        }
    } else {
        if (declinfo->nspace) {
            CError_Error(ERR_NAME_NOT_BEEN_DECLARED_NAMESPACE_CLASS);
        }
        if (IS_TYPE_CLASS(declinfo->thetype) && TYPE_CLASS(declinfo->thetype)->sominfo) {
            CError_Error(ERR_GLOBAL_SOM_CLASS_OBJECTS_NOT_SUPPORTED);
        }
        if (!CanCreateObject(declinfo->thetype)) {
            declinfo->thetype = TYPE(&stsignedint);
        }

        obj = CParser_NewGlobalDataObject(declinfo);
        obj->access = access;
        CScope_AddObject(nspace, declinfo->name, OBJ_BASE(obj));

        if (nspace->theclass &&
            (nspace->theclass->flags & CLASS_IS_TEMPL) &&
            CTemplTool_IsTemplateArgumentDependentType(declinfo->thetype)) {
            CTemplClass_RegisterObjectDef(TEMPL_CLASS(nspace->theclass), OBJ_BASE(obj));
        }

        if (isMember && nspace->theclass && cparamblkptr->browseoptions.recordClasses) {
            CBrowse_AddClassMemberData(
                obj, CPrep_BrowserTokenOffset(&member_fileoffset) + 1, CPrep_BrowserFileOffset()
            );
        }
    }

    if (!isMember) {
        if (declinfo->nspace) {
            CScopeSave save;
            CScope_SetNameSpaceScope(declinfo->nspace, &save);
            CInit_InitializeData(obj);
            CScope_RestoreScope(&save);

            if (declinfo->requireTemplateClassMember &&
                obj->nspace->theclass &&
                (TYPE_CLASS(obj->nspace->theclass)->flags & CLASS_IS_TEMPL_INST)) {
                declinfo->requireTemplateClassMember = FALSE;
            }
        } else {
            CInit_InitializeData(obj);
        }

        if (declinfo->file->recordbrowseinfo && obj->sclass != TK_EXTERN) {
            CBrowse_NewData(
                obj,
                declinfo->file,
                declinfo->file2,
                declinfo->sourceoffset,
                CPrep_BrowserFileOffset()
            );
        }
    } else if (tk == '=') {
        tk = lex();
        expr = CExpr_IntegralConstOrDepExpr();
        if (IS_TYPE_TEMPLATE(obj->type) || !ENODE_IS(expr, EINTCONST)) {
            CError_ASSERT(2491, nspace->theclass && (nspace->theclass->flags & CLASS_IS_TEMPL));
            CTemplClass_RegisterObjectInit(TEMPL_CLASS(nspace->theclass), obj, expr);
        } else if ((obj->qual & Q_CONST) && IS_TYPE_INT_OR_ENUM(obj->type)) {
            obj->u.data.u.intconst = expr->data.intval;
            obj->qual |= Q_INLINE_DATA | Q_IMPLICIT_WEAK;
        } else {
            CError_Error(ERR_ILLEGAL_STATIC_CONST_MEMBER_INITIALIZATION, obj->name->name);
        }
    }
}

// NOTE: invented names: declinfo, nspace, allowDefinition, unused, firstDecl
Boolean CDecl_FunctionDeclarator(
    DeclInfo* declinfo, NameSpace* nspace, Boolean allowDefinition, Boolean unused
) {
    Object* obj;
    Boolean firstDecl;

    obj = CDecl_GetFunctionObject(declinfo, nspace, &firstDecl, FALSE);
    if (obj) {
        if (declinfo->hasParameterNames ||
            tk == '{' ||
            tk == TK_TRY ||
            (declinfo->isConstructor && tk == ':') ||
            (!copts.cplusplus && isdeclaration(0, 0, 0, 0))) {
            if (!allowDefinition || cscope_currentfunc) {
                CError_Error(ERR_ILLEGAL_FUNCTION_DEFINITION);
                if (cscope_currentfunc) {
                    return FALSE;
                }
            }

            if (obj->nspace == cscope_root && !strcmp(obj->name->name, "main")) {
                if (obj->sclass == TK_STATIC ||
                    (copts.ANSIstrict && TYPE_FUNC(obj->type)->functype != (Type*)&stsignedint)) {
                    CError_Error(ERR_MAIN_NOT_DEFINED_AS_EXTERNAL_INT);
                }
            } else if (copts.checkprotos && (firstDecl || declinfo->firstDeclaration)) {
                if (obj->sclass != TK_STATIC &&
                    !(obj->qual & Q_INLINE) &&
                    !obj->nspace->is_unnamed) {
                    CError_Warning(ERR_FUNCTION_NO_PROTOTYPE);
                }
            }

            CFunc_ParseFuncDef(obj, declinfo, NULL, FALSE, FALSE, NULL);
            if (declinfo->file->recordbrowseinfo) {
                CBrowse_NewFunction(
                    obj,
                    declinfo->file,
                    declinfo->file2,
                    declinfo->sourceoffset,
                    CPrep_BrowserFileOffset()
                );
            }

            if (copts.cplusplus && lookahead() == ';') {
                tk = lex();
            }
            return FALSE;
        }
    }

    return TRUE;
}

// NOTE: invented names: declinfo, parseDeclarator
static void CDecl_ParseSpecialMember(DeclInfo* declinfo, Boolean parseDeclarator) {
    Object* obj;
    NameSpace* savedNSpace;

    if (!(obj = declinfo->resolvedObject)) {
        CError_ASSERT(2609, declinfo->resolvedObjects);
        obj = OBJECT(declinfo->resolvedObjects->object);
        CError_ASSERT(2611, obj->otype == OT_OBJECT);
    }

    if (!obj->nspace->theclass) {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        return;
    }

    if (IS_TYPE_FUNC(obj->type)) {
        if (TYPE_FUNC(obj->type)->flags & (FUNC_IS_CTOR | FUNC_IS_DTOR)) {
            if (obj->nspace->theclass->sominfo) {
                declinfo->thetype = TYPE(&stvoid);
            } else {
                declinfo->thetype = TYPE(&void_ptr);
            }
            declinfo->nspace = obj->nspace;
            declinfo->name = obj->name;
            if (TYPE_FUNC(obj->type)->flags & FUNC_IS_CTOR) {
                declinfo->isConstructor = TRUE;
            }

            if ((tk = lex()) == '(') {
                tk = lex();

                savedNSpace = cscope_current;
                cscope_current = obj->nspace;
                CDecl_ParseDirectFuncDecl(declinfo);
                cscope_current = savedNSpace;

                if (IS_TYPE_FUNC(declinfo->thetype)) {
                    if (TYPE_FUNC(obj->type)->flags & FUNC_IS_CTOR) {
                        if ((obj->nspace->theclass->flags & CLASS_HAS_VBASES) &&
                            !obj->nspace->theclass->sominfo) {
                            CDecl_AddArgument(TYPE_FUNC(declinfo->thetype), TYPE(&stsignedshort));
                        }
                    } else {
                        if (!obj->nspace->theclass->sominfo) {
                            CDecl_AddArgument(TYPE_FUNC(declinfo->thetype), TYPE(&stsignedshort));
                        }
                    }
                    if (parseDeclarator) {
                        CDecl_FunctionDeclarator(declinfo, NULL, TRUE, TRUE);
                    }
                } else {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                }
            } else {
                CError_Error(ERR_LPAREN_EXPECTED);
            }
            return;
        } else if (TYPE_FUNC(obj->type)->flags & FUNC_CONVERSION) {
            CError_FATAL(2668);

            declinfo->thetype = TYPE_FUNC(obj->type)->functype;
            declinfo->qual |= TYPE_FUNC(obj->type)->qual;
            declinfo->nspace = obj->nspace;
            declinfo->name = obj->name;

            if ((tk = lex()) == '(') {
                tk = lex();
                CDecl_ParseDirectFuncDecl(declinfo);
                if (IS_TYPE_FUNC(declinfo->thetype)) {
                    TYPE_FUNC(declinfo->thetype)->flags |= FUNC_CONVERSION;
                    if (parseDeclarator) {
                        CDecl_FunctionDeclarator(declinfo, NULL, TRUE, TRUE);
                    }
                } else {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                }
            } else {
                CError_Error(ERR_LPAREN_EXPECTED);
            }
            return;
        } else {
            declinfo->thetype = TYPE(&stsignedint);
            declinfo->nspace = obj->nspace;
            declinfo->name = obj->name;

            if ((tk = lex()) == '(') {
                tk = lex();

                savedNSpace = cscope_current;
                cscope_current = obj->nspace;
                CDecl_ParseDirectFuncDecl(declinfo);
                cscope_current = savedNSpace;

                if (IS_TYPE_FUNC(declinfo->thetype)) {
                    if (parseDeclarator) {
                        CDecl_FunctionDeclarator(declinfo, NULL, TRUE, TRUE);
                    }
                    return;
                }
            } else {
                CError_Error(ERR_LPAREN_EXPECTED);
            }
        }
    }

    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
}

void CDecl_ScanDeclarator(DeclInfo* declinfo) {
    if (declinfo->resolvedObjects || declinfo->resolvedObject) {
        CDecl_ParseSpecialMember(declinfo, FALSE);
        CDecl_GetFunctionObject(declinfo, NULL, NULL, TRUE);
        return;
    }

    if (IS_TYPE_FUNC(declinfo->thetype)) {
        TypeFunc* copy = galloc(sizeof(TypeFunc));
        *copy = *TYPE_FUNC(declinfo->thetype);
        declinfo->thetype = TYPE(copy);
    }
    scandeclarator(declinfo);
    if (!declinfo->name) {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        return;
    }

    if (declinfo->storageclass && declinfo->storageclass != TK_EXTERN) {
        CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
    }

    if (IS_TYPE_FUNC(declinfo->thetype)) {
        CDecl_GetFunctionObject(declinfo, NULL, NULL, TRUE);
        return;
    }

    if (declinfo->missingTypeSpecifier || declinfo->hasParameterNames) {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
    }

    if (declinfo->operator_token) {
        CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
    }

    if ((declinfo->qual &
         ~(Q_ALIGNED_MASK | Q_WEAK | Q_IMPLICIT_WEAK | Q_PASCAL | Q_VOLATILE | Q_CONST)) ||
        (declinfo->storageclass == TK_TYPEDEF &&
         (declinfo->qual & ~(Q_ALIGNED_MASK | Q_REFERENCE | Q_PASCAL | Q_VOLATILE | Q_CONST)))) {
        CError_Error(ERR_ILLEGAL_TYPE_QUALIFIERS);
    }
}

void scandeclaratorlist(DeclInfo* declinfo) {
    CScopeSave savescope;
    Type* type;
    UInt32 qual;
    Boolean isFirst;

    if (declinfo->resolvedObjects || declinfo->resolvedObject) {
        CDecl_ParseSpecialMember(declinfo, TRUE);
        return;
    }

    CScope_GetScope(&savescope);
    CError_ASSERT(2772, declinfo->thetype);

    isFirst = TRUE;
    while (1) {
        type = declinfo->thetype;
        qual = declinfo->qual;
        declinfo->nspace = NULL;
        declinfo->operator_token = 0;
        if (IS_TYPE_FUNC(type)) {
            declinfo->thetype = galloc(sizeof(TypeFunc));
            *TYPE_FUNC(declinfo->thetype) = *TYPE_FUNC(type);
        }
        declinfo->name = NULL;
        scandeclarator(declinfo);
        if (!declinfo->name) {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            break;
        }

        if (declinfo->storageclass != TK_TYPEDEF) {
            if (IS_TYPE_FUNC(declinfo->thetype)) {
                if (!CDecl_FunctionDeclarator(declinfo, NULL, isFirst, TRUE)) {
                    return;
                }
            } else {
                CDecl_DataDeclarator(declinfo, ACCESSPUBLIC, FALSE);
            }
        } else {
            CDecl_TypedefDeclarator(declinfo);
        }

        CScope_RestoreScope(&savescope);
        declinfo->thetype = type;
        declinfo->qual = qual;

        if (tk != ',') {
            break;
        }
        tk = lex();
        isFirst = FALSE;
    }

    if (tk != ';') {
        CError_Error(ERR_SEMICOLON_EXPECTED);
    }
}

static TypeIntegral* CDecl_FindSignedType(short size) {
    if (stsignedchar.size == size) {
        return &stsignedchar;
    }
    if (stsignedshort.size == size) {
        return &stsignedshort;
    }
    if (stsignedint.size == size) {
        return &stsignedint;
    }
    if (stsignedlong.size == size) {
        return &stsignedlong;
    }
    if (copts.longlong && copts.longlong_enums && stsignedlonglong.size == size) {
        return &stsignedlonglong;
    }
    return &stsignedlong;
}

static TypeIntegral* CDecl_FindUnsignedType(short size) {
    if (stunsignedchar.size == size) {
        return &stunsignedchar;
    }
    if (stunsignedshort.size == size) {
        return &stunsignedshort;
    }
    if (stunsignedint.size == size) {
        return &stunsignedint;
    }
    if (stunsignedlong.size == size) {
        return &stunsignedlong;
    }
    if (copts.longlong && copts.longlong_enums && stunsignedlonglong.size == size) {
        return &stunsignedlonglong;
    }
    return &stunsignedlong;
}

static TypeIntegral* CDecl_IterateIntegralEnumType(int* iter) { // NOTE: invented name: iter
    switch (*iter) {
        case 0:
            *iter = 1;
            return &stsignedchar;
        case 1:
            if (stsignedshort.size > stsignedchar.size) {
                *iter = 2;
                return &stsignedshort;
            }
        case 2:
            if (stsignedint.size > stsignedshort.size) {
                *iter = 3;
                return &stsignedint;
            }
        case 3:
            if (stsignedlong.size > stsignedint.size) {
                *iter = 4;
                return &stsignedlong;
            }
        case 4:
            *iter = 5;
            if (stsignedlonglong.size > stsignedlong.size &&
                copts.longlong &&
                copts.longlong_enums) {
                return &stsignedlonglong;
            }
        default:
            return NULL;
    }
}

static TypeIntegral* CDecl_IterateUIntegralEnumType(int* iter) { // NOTE: invented name: iter
    switch (*iter) {
        case 0:
            *iter = 1;
            return &stunsignedchar;
        case 1:
            if (stunsignedshort.size > stunsignedchar.size) {
                *iter = 2;
                return &stunsignedshort;
            }
        case 2:
            if (stunsignedint.size > stunsignedshort.size) {
                *iter = 3;
                return &stunsignedint;
            }
        case 3:
            if (stunsignedlong.size > stunsignedint.size) {
                *iter = 4;
                return &stunsignedlong;
            }
        case 4:
            *iter = 5;
            if (stunsignedlonglong.size > stunsignedlong.size &&
                copts.longlong &&
                copts.longlong_enums) {
                return &stunsignedlonglong;
            }
        default:
            return NULL;
    }
}

static TypeEnum* CDecl_OldParseEnumList(TypeEnum* tenum, HashNameNode* name) {
    AccessType access;
    Boolean is_signed;
    Boolean next_overflows;
    ObjEnumConst* oec;
    ObjEnumConst* last;
    Boolean overflowed;
    CInt64 val;
    CInt64 minimum;
    CInt64 maximum;
    CInt64 next;
    Type* basetype;
    Type* basetype2;
    CPrepFileInfo* fileinfo;
    SInt32 offset;

    if (!tenum) {
        tenum = galloc(sizeof(TypeEnum));
        memclrw(tenum, sizeof(TypeEnum));
        tenum->type = TYPEENUM;
        tenum->nspace = cscope_current;

        if (name) {
            tenum->enumname = name;
            CScope_DefineTypeTag(cscope_current, name, TYPE(tenum));
        }

        if (!cscope_current->is_global) {
            do {
                tenum->nspace = tenum->nspace->parent;
            } while (!tenum->nspace->is_global);
            if (tenum->enumname) {
                tenum->enumname = CParser_AppendUniqueNameFile(tenum->enumname->name);
            }
        }
    }

    if (cscope_current->theclass && (cscope_current->theclass->flags & CLASS_IS_TEMPL)) {
        CTemplClass_RegisterEnumType(TEMPL_CLASS(cscope_current->theclass), tenum);
    }

    access = cscope_current->theclass ? global_access : ACCESSPUBLIC;
    last = NULL;
    val = cint64_zero;
    minimum = cint64_zero;
    maximum = cint64_zero;
    next_overflows = FALSE;
    if (copts.enumsalwaysint) {
        basetype = TYPE(&stsignedint);
        is_signed = TRUE;
    } else {
        basetype = TYPE(&stunsignedchar);
        is_signed = FALSE;
    }

    tk = lex();
    if (!copts.cplusplus || tk != '}') {
        do {
            if (tk != TK_IDENTIFIER) {
                if (tk == '}') {
                    if (copts.cpp_extensions) {
                        break;
                    }
                    if (!copts.warn_extracomma) {
                        break;
                    }
                }
                CError_Warning(ERR_IDENTIFIER_EXPECTED);
                break;
            }

            oec = galloc(sizeof(ObjEnumConst));
            memclrw(oec, sizeof(ObjEnumConst));
            oec->otype = OT_ENUMCONST;
            oec->access = access;
            oec->type = TYPE(tenum);
            oec->name = tkidentifier;
            CPrep_BrowserFilePosition(&fileinfo, &offset);
            overflowed = FALSE;
            if ((tk = lex()) == '=') {
                tk = lex();
                val = CExpr_IntegralConstExprType(&basetype2);
                if (!CInt64_IsNegative(&val) || is_unsigned(basetype2)) {
                    if (CInt64_GreaterU(val, minimum)) {
                        minimum = val;
                        overflowed = TRUE;
                    }
                } else {
                    if (CInt64_Less(val, maximum)) {
                        maximum = val;
                        overflowed = TRUE;
                    }
                    if (!is_signed) {
                        basetype = TYPE(&stsignedchar);
                        is_signed = TRUE;
                    }
                }
                next_overflows = FALSE;
            } else {
                if (next_overflows) {
                    CError_Error(ERR_NUMBER_OUT_RANGE);
                }

                if (!is_signed || !CInt64_IsNegative(&val)) {
                    if (CInt64_GreaterU(val, minimum)) {
                        minimum = val;
                        overflowed = TRUE;
                    }
                } else {
                    if (CInt64_Less(val, maximum)) {
                        maximum = val;
                        overflowed = TRUE;
                    }
                }
            }

            if (copts.enumsalwaysint) {
                if (copts.ANSIstrict) {
                    if (!CInt64_IsInRange(val, stsignedint.size)) {
                        CError_Error(ERR_NUMBER_OUT_RANGE);
                    }
                } else {
                    if (!CInt64_IsInRange(val, stsignedint.size) &&
                        !CInt64_IsInURange(val, stunsignedint.size)) {
                        CError_Error(ERR_NUMBER_OUT_RANGE);
                    }
                }
            } else if (is_signed) {
                switch (basetype->size) {
                    case 1:
                        if (CInt64_IsInRange(minimum, 1) && CInt64_IsInRange(maximum, 1)) {
                            break;
                        }
                        basetype = TYPE(CDecl_FindSignedType(2));
                    case 2:
                        if (CInt64_IsInRange(minimum, 2) && CInt64_IsInRange(maximum, 2)) {
                            break;
                        }
                        basetype = TYPE(CDecl_FindSignedType(4));
                    case 4:
                        if (CInt64_IsInRange(minimum, 4) && CInt64_IsInRange(maximum, 4)) {
                            break;
                        }
                        basetype = TYPE(CDecl_FindSignedType(8));
                        if (basetype->size != 8) {
                            if (!copts.ANSIstrict &&
                                CInt64_IsInRange(maximum, 4) &&
                                CInt64_IsInURange(minimum, 4)) {
                                break;
                            }
                            if (overflowed) {
                                CError_Error(ERR_NUMBER_OUT_RANGE);
                            }
                            break;
                        }
                    case 8:
                        if (CInt64_Equal(val, minimum) && CInt64_IsNegative(&val)) {
                            CError_Error(ERR_NUMBER_OUT_RANGE);
                        }
                        break;
                    default:
                        CError_FATAL(3071);
                }
            } else {
                switch (basetype->size) {
                    case 1:
                        if (CInt64_IsInURange(minimum, 1)) {
                            break;
                        }
                        basetype = TYPE(CDecl_FindUnsignedType(2));
                    case 2:
                        if (CInt64_IsInURange(minimum, 2)) {
                            break;
                        }
                        basetype = TYPE(CDecl_FindUnsignedType(4));
                    case 4:
                        if (CInt64_IsInURange(minimum, 4)) {
                            break;
                        }
                        basetype = TYPE(CDecl_FindUnsignedType(8));
                        if (basetype->size != 8) {
                            if (overflowed) {
                                CError_Error(ERR_NUMBER_OUT_RANGE);
                            }
                            break;
                        }
                    case 8:
                        break;
                    default:
                        CError_FATAL(3099);
                }
            }

            tenum->size = basetype->size;
            tenum->enumtype = basetype;
            oec->val = val;
            CScope_AddObject(cscope_current, oec->name, OBJ_BASE(oec));

            if (last) {
                last->next = oec;
                last = oec;
            } else {
                last = oec;
                tenum->enumlist = oec;
            }

            if (cparamblkptr->browseoptions.recordEnums) {
                CPrepFileInfo* f = CPrep_BrowserCurrentFile();
                if (f->recordbrowseinfo) {
                    CBrowse_NewEnumConstant(
                        cscope_current, oec->name, f, fileinfo, offset, CPrep_BrowserFileOffset()
                    );
                }
            }

            next = CInt64_Add(val, cint64_one);
            if (is_signed) {
                if (CInt64_IsNegative(&next) && !CInt64_IsNegative(&val)) {
                    next_overflows = TRUE;
                }
            } else {
                if (CInt64_IsZero(&next)) {
                    next_overflows = TRUE;
                }
            }
            val = next;

            if (tk != ',') {
                break;
            }
            tk = lex();
        } while (1);
    }

    tenum->size = basetype->size;
    tenum->enumtype = basetype;

    for (oec = tenum->enumlist; oec; oec = oec->next) {
        oec->type = TYPE(tenum);
    }

    if (tk != '}') {
        CError_ErrorSkip(ERR_RBRACE_EXPECTED);
    } else {
        tk = lex();
    }

    return tenum;
}

static Type* CDecl_MaxType(Type* a, Type* b) {
    if (a->size > b->size) {
        return a;
    }
    if (b->size > a->size) {
        return b;
    }
    if (is_unsigned(b)) {
        return b;
    } else {
        return a;
    }
}

void CDecl_ComputeUnderlyingEnumType(TypeEnum* tenum) {
    ObjEnumConst* oec;
    ObjEnumConst* oec2;
    Type* basetype;
    CInt64 minimum;
    CInt64 maximum;
    int iter; // NOTE: invented name
    CInt64 val;

    if (!copts.enumsalwaysint) {
        for (oec2 = tenum->enumlist; oec2; oec2 = oec2->next) {
            if (CInt64_IsNegative(&oec2->val) && !is_unsigned(oec2->type)) {
                break;
            }
        }

        if (oec2) {
            minimum = cint64_zero;
            maximum = cint64_zero;
            for (oec = tenum->enumlist; oec; oec = oec->next) {
                if (CInt64_IsNegative(&oec->val) && !is_unsigned(oec->type)) {
                    if (CInt64_Less(oec->val, minimum)) {
                        minimum = oec->val;
                    }
                } else {
                    if (CInt64_GreaterU(oec->val, maximum)) {
                        maximum = oec->val;
                    }
                }
            }

            if (CInt64_IsNegative(&maximum)) {
                CError_Error(ERR_NUMBER_OUT_RANGE);
            }

            iter = 0;
            do {
                basetype = TYPE(CDecl_IterateIntegralEnumType(&iter));
                if (!basetype) {
                    basetype = TYPE(&stsignedlong);
                    CError_Error(ERR_NUMBER_OUT_RANGE);
                    break;
                }

                if (CInt64_IsInRange(maximum, basetype->size) &&
                    CInt64_IsInRange(minimum, basetype->size)) {
                    break;
                }
                if (basetype->size == stsignedlong.size &&
                    !copts.ANSIstrict &&
                    CInt64_IsInRange(minimum, basetype->size) &&
                    CInt64_IsInURange(maximum, basetype->size)) {
                    break;
                }
            } while (1);
        } else {
            val = cint64_zero;

            for (oec = tenum->enumlist; oec; oec = oec->next) {
                if (CInt64_GreaterU(oec->val, val)) {
                    val = oec->val;
                }
            }

            iter = 0;
            do {
                basetype = TYPE(CDecl_IterateUIntegralEnumType(&iter));
                if (!basetype) {
                    basetype = TYPE(&stunsignedlong);
                    CError_Error(ERR_NUMBER_OUT_RANGE);
                    break;
                }
                if (CInt64_IsInURange(val, basetype->size)) {
                    break;
                }
            } while (1);
        }
    } else {
        basetype = TYPE(&stsignedint);
    }

    tenum->size = basetype->size;
    tenum->enumtype = basetype;
    for (oec = tenum->enumlist; oec; oec = oec->next) {
        oec->type = TYPE(tenum);
    }
}

static Type* CDecl_FindUnderlyingType(short size, CInt64* minimum, CInt64* maximum) {
    if (CInt64_IsZero(minimum)) {
        if (size <= stsignedchar.size && CInt64_IsInURange(*maximum, stunsignedchar.size)) {
            return TYPE(&stunsignedchar);
        }
        if (size <= stsignedshort.size && CInt64_IsInURange(*maximum, stunsignedshort.size)) {
            return TYPE(&stunsignedshort);
        }
        if (size <= stsignedint.size && CInt64_IsInURange(*maximum, stunsignedint.size)) {
            return TYPE(&stunsignedint);
        }
        if (size <= stsignedlong.size && CInt64_IsInURange(*maximum, stunsignedlong.size)) {
            return TYPE(&stunsignedlong);
        }
        if (size <= stsignedlonglong.size &&
            copts.longlong &&
            copts.longlong_enums &&
            CInt64_IsInURange(*maximum, stunsignedlonglong.size)) {
            return TYPE(&stunsignedlonglong);
        }
    } else {
        if (size <= stsignedchar.size &&
            CInt64_IsInRange(*minimum, stsignedchar.size) &&
            CInt64_IsInRange(*maximum, stsignedchar.size)) {
            return TYPE(&stsignedchar);
        }
        if (size <= stsignedshort.size &&
            CInt64_IsInRange(*minimum, stsignedshort.size) &&
            CInt64_IsInRange(*maximum, stsignedshort.size)) {
            return TYPE(&stsignedshort);
        }
        if (size <= stsignedint.size &&
            CInt64_IsInRange(*minimum, stsignedint.size) &&
            CInt64_IsInRange(*maximum, stsignedint.size)) {
            return TYPE(&stsignedint);
        }
        if (size <= stsignedlong.size &&
            CInt64_IsInRange(*minimum, stsignedlong.size) &&
            CInt64_IsInRange(*maximum, stsignedlong.size)) {
            return TYPE(&stsignedlong);
        }
        if (size <= stsignedlonglong.size &&
            copts.longlong &&
            copts.longlong_enums &&
            CInt64_IsInRange(*minimum, stsignedlonglong.size) &&
            CInt64_IsInRange(*maximum, stsignedlonglong.size)) {
            return TYPE(&stsignedlonglong);
        }
        if (!copts.ANSIstrict &&
            size <= stsignedlong.size &&
            CInt64_IsInRange(*minimum, stsignedlong.size) &&
            CInt64_IsInURange(*maximum, stunsignedlong.size)) {
            return TYPE(&stsignedlong);
        }
    }

    return NULL;
}

static TypeEnum* CDecl_ParseEnumList(TypeEnum* tenum, HashNameNode* name) {
    Type* basetype;
    AccessType access;
    TemplClass* tmclass;
    ObjEnumConst* oec;
    Boolean has_template_value;
    Boolean overflowed;
    Boolean is_first;
    CInt64 val;
    CInt64 minimum;
    CInt64 maximum;
    CPrepFileInfo* fileinfo;
    SInt32 offset;
    ENode* expr;
    Type* underlying; // NOTE: invented name
    ObjEnumConst* last;

    if (!tenum) {
        tenum = galloc(sizeof(TypeEnum));
        memclrw(tenum, sizeof(TypeEnum));
        tenum->type = TYPEENUM;
        tenum->nspace = cscope_current;

        if (name) {
            tenum->enumname = name;
            CScope_DefineTypeTag(cscope_current, name, TYPE(tenum));
        }

        if (!cscope_current->is_global) {
            while (!(tenum->nspace = tenum->nspace->parent)->is_global) {}
            if (tenum->enumname) {
                tenum->enumname = CParser_AppendUniqueNameFile(tenum->enumname->name);
            }
        }
    }

    if (cscope_current->theclass && (cscope_current->theclass->flags & CLASS_IS_TEMPL)) {
        tmclass = TEMPL_CLASS(cscope_current->theclass);
        CTemplClass_RegisterEnumType(tmclass, tenum);
    } else {
        tmclass = NULL;
    }

    access = cscope_current->theclass ? global_access : ACCESSPUBLIC;
    last = NULL;
    is_first = TRUE;
    has_template_value = FALSE;
    val = cint64_zero;
    minimum = cint64_zero;
    maximum = cint64_zero;
    basetype = copts.enumsalwaysint ? TYPE(&stsignedint) : TYPE(&stsignedchar);
    tenum->size = basetype->size;
    tenum->enumtype = basetype;

    do {
        if ((tk = lex()) != TK_IDENTIFIER) {
            if (tk == '}') {
                if (is_first) {
                    if (copts.cplusplus) {
                        break;
                    }
                } else {
                    if (!copts.warn_extracomma) {
                        break;
                    }
                    if (copts.c9x) {
                        break;
                    }
                    if (copts.cpp_extensions) {
                        break;
                    }
                }
                CError_Warning(ERR_IDENTIFIER_EXPECTED);
            } else {
                CError_Error(ERR_IDENTIFIER_EXPECTED);
            }
            break;
        }

        oec = galloc(sizeof(ObjEnumConst));
        memclrw(oec, sizeof(ObjEnumConst));
        oec->otype = OT_ENUMCONST;
        oec->access = access;
        oec->name = tkidentifier;
        CPrep_BrowserFilePosition(&fileinfo, &offset);
        overflowed = FALSE;
        if ((tk = lex()) == '=') {
            tk = lex();
            if (tmclass) {
                expr = CExpr_IntegralConstOrDepExpr();
                if (ENODE_IS(expr, EINTCONST)) {
                    val = expr->data.intval;
                    basetype = expr->rtype;
                    has_template_value = FALSE;
                } else {
                    val = cint64_zero;
                    basetype = TYPE(tenum);
                    CTemplClass_RegisterEnumerator(tmclass, oec, expr);
                    has_template_value = TRUE;
                }
            } else {
                val = CExpr_IntegralConstExprType(&basetype);
                has_template_value = FALSE;
            }
        } else if (has_template_value) {
            CTemplClass_RegisterEnumerator(tmclass, oec, NULL);
        } else if (!is_first) {
            if (is_unsigned(basetype)) {
                val = CInt64_Add(val, cint64_one);
                if (CInt64_IsZero(&val)) {
                    overflowed = TRUE;
                }
            } else if (!CInt64_IsNegative(&val)) {
                val = CInt64_Add(val, cint64_one);
                if (CInt64_IsNegative(&val)) {
                    overflowed = TRUE;
                }
            } else {
                val = CInt64_Add(val, cint64_one);
            }
        }

        if (!has_template_value) {
            if (copts.enumsalwaysint) {
                if (!CInt64_IsInRange(val, stsignedint.size) &&
                    (copts.ANSIstrict || !CInt64_IsInURange(val, stunsignedint.size))) {
                    overflowed = TRUE;
                }
                basetype = TYPE(&stsignedint);
            } else if (CInt64_IsNegative(&val) && !is_unsigned(basetype)) {
                if (CInt64_Less(val, minimum)) {
                    minimum = val;
                    if ((underlying = CDecl_FindUnderlyingType(tenum->size, &minimum, &maximum))) {
                        tenum->size = underlying->size;
                        tenum->enumtype = underlying;
                    } else {
                        overflowed = TRUE;
                    }
                }
            } else {
                if (CInt64_GreaterU(val, maximum)) {
                    maximum = val;
                    if ((underlying = CDecl_FindUnderlyingType(tenum->size, &minimum, &maximum))) {
                        tenum->size = underlying->size;
                        tenum->enumtype = underlying;
                    } else {
                        overflowed = TRUE;
                    }
                }
            }
        }

        if (overflowed) {
            CError_Error(ERR_NUMBER_OUT_RANGE);
        }

        oec->val = val;
        oec->type = basetype;
        CScope_AddObject(cscope_current, oec->name, OBJ_BASE(oec));

        if (last) {
            last->next = oec;
            last = oec;
        } else {
            tenum->enumlist = last = oec;
        }

        if (cparamblkptr->browseoptions.recordEnums) {
            CPrepFileInfo* f = CPrep_BrowserCurrentFile();
            if (f->recordbrowseinfo) {
                CBrowse_NewEnumConstant(
                    cscope_current, oec->name, f, fileinfo, offset, CPrep_BrowserFileOffset()
                );
            }
        }

        is_first = FALSE;
    } while (tk == ',');

    for (oec = tenum->enumlist; oec; oec = oec->next) {
        oec->type = TYPE(tenum);
    }

    if (tk != '}') {
        CError_ErrorSkip(ERR_RBRACE_EXPECTED);
    } else {
        tk = lex();
    }

    return tenum;
}

void scanenum(DeclInfo* declinfo) {
    HashNameNode* name;
    Type* type;
    NameResult pr;

#ifndef __MWERKS__
    // A name that is not an identifier (`enum ::E {` of a defined enum) reaches define_enum with
    // `name` unset, and its error message prints name->name. The original keeps `name` in the
    // register its caller passed declinfo in, so it prints the bytes at declinfo + 10: the same here.
    name = (HashNameNode*)declinfo;
#endif

    if (tk == '{') {
        declinfo->thetype = TYPE(CDecl_ParseEnumList(NULL, NULL));
        TYPE_ENUM(declinfo->thetype)->enumname = CParser_AppendUniqueNameFile("@enum");
        return;
    }

    if (tk == TK_IDENTIFIER) {
        name = tkidentifier;
        if (lookahead() == '{') {
            type = CScope_GetLocalTagType(cscope_current, name);
            if (type) {
                lex();
define_enum: // NOTE: invented name
                if (type->size || !IS_TYPE_ENUM(type)) {
                    CError_Error(ERR_IDENTIFIER_REDECLARED, name->name);
                    declinfo->thetype = TYPE(CDecl_ParseEnumList(NULL, NULL));
                    return;
                }
                declinfo->thetype = TYPE(CDecl_ParseEnumList(TYPE_ENUM(type), NULL));
            } else {
                lex();
                declinfo->thetype = TYPE(CDecl_ParseEnumList(NULL, name));
            }

            if (cparamblkptr->browseoptions.recordEnums && declinfo->file->recordbrowseinfo) {
                CBrowse_NewEnum(
                    cscope_current,
                    TYPE_ENUM(declinfo->thetype)->enumname,
                    declinfo->file,
                    declinfo->file2,
                    declinfo->sourceoffset,
                    CPrep_BrowserFileOffset()
                );
            }
            return;
        } else {
            CError_ASSERT(3916, !copts.cplusplus || tk != ';');
            tkidentifier = name;
        }
    }

    if (CScope_ParseElaborateName(&pr)) {
        if ((type = pr.type)) {
            if (!IS_TYPE_ENUM(type)) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            if ((tk = lex()) == '{') {
                goto define_enum;
            }
            declinfo->thetype = type;
            return;
        } else {
            CError_ASSERT(3930, pr.name);
            if ((tk = lex()) == '{') {
                declinfo->thetype = TYPE(CDecl_ParseEnumList(NULL, pr.name));
                return;
            } else {
                CError_Error(ERR_UNDEFINED_IDENTIFIER, pr.name->name);
            }
        }
    } else {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
    }

    declinfo->thetype = TYPE(&stsignedint);
}

void CDecl_ScanStructDeclarator(BigDeclInfo* bde) {
    ENode* expr;
    short val;
    short bits;
    Boolean is_bitfield;
    TypeTemplDep* ttempl;
    TypeBitfield* tbitfield;
    Type* type;

    bde->declinfo2 = bde->declinfo;
    bde->declinfo2.name = NULL;
    bde->declinfo2.operator_token = 0;
    bde->valid = FALSE;
    is_bitfield = FALSE;

    if (tk == ':') {
        bde->declinfo2.name = no_name_node;
        is_bitfield = TRUE;
    } else {
        bde->declinfo2.allowZeroLengthArray = TRUE;
        scandeclarator(&bde->declinfo2);
        if (!bde->declinfo2.name) {
            CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
            return;
        }

        if ((!copts.ANSIstrict || copts.c9x) &&
            !bde->declinfo2.thetype->size &&
            IS_TYPE_ARRAY(bde->declinfo2.thetype)) {
            if ((int)bde->declinfo2.storageclass != TK_STATIC &&
                (int)bde->declinfo2.storageclass != TK_TYPEDEF) {
                type = TYPE_POINTER(bde->declinfo2.thetype)->target;
                while (IS_TYPE_ARRAY(type)) {
                    type = TYPE_POINTER(type)->target;
                }
                if (!IsCompleteType(type)) {
                    return;
                }
                if (tk != ';' || lookahead() != '}') {
                    CError_Error(ERR_DATA_TYPE_INCOMPLETE);
                    return;
                }
            }
        } else {
            if ((int)bde->declinfo2.storageclass != TK_STATIC &&
                (int)bde->declinfo2.storageclass != TK_TYPEDEF) {
                if (!IS_TYPE_FUNC(bde->declinfo2.thetype) &&
                    !IsCompleteType(bde->declinfo2.thetype)) {
                    return;
                }
            }
        }

        if (IS_TYPE_CLASS(bde->declinfo2.thetype) && TYPE_CLASS(bde->declinfo2.thetype)->sominfo) {
            CError_Error(ERR_SOM_CLASSES_CANNOT_CLASS_MEMBERS);
            return;
        }

        if (tk != ':') {
            goto not_a_bitfield;
        }
    }

    if (!IS_TYPE_INT_OR_ENUM(bde->declinfo2.thetype)) {
        if (CTemplTool_IsTemplateArgumentDependentType(bde->declinfo2.thetype)) {
            goto parse_width;
        }
        CError_Error(ERR_ILLEGAL_BITFIELD_DECLARATION);
        bde->declinfo2.thetype = TYPE(&stunsignedint);
    } else if (copts.ANSIstrict && !copts.cplusplus) {
        if (bde->declinfo2.thetype != TYPE(&stsignedint) &&
            bde->declinfo2.thetype != TYPE(&stunsignedint)) {
            CError_Error(ERR_ILLEGAL_BITFIELD_DECLARATION);
            bde->declinfo2.thetype = TYPE(&stunsignedint);
        }
    }

    switch (bde->declinfo2.thetype->size) {
        case 1:
            bits = 8;
            break;
        case 2:
            bits = 16;
            break;
        case 4:
            bits = 32;
            break;
        case 8:
            // long long bitfields (#pragma has8bytebitfields)
            if (copts.has8bytebitfields) {
                bits = 64;
                break;
            }
        default:
            CError_Error(ERR_ILLEGAL_BITFIELD_DECLARATION);
            return;
    }
parse_width: // NOTE: invented name
    tk = lex();
    expr = CExpr_IntegralConstOrDepExpr();
    if (!ENODE_IS(expr, EINTCONST)) {
        ttempl = CDecl_NewTemplDepType(TEMPLDEP_BITFIELD);
        ttempl->u.bitfield.type = bde->declinfo2.thetype;
        ttempl->u.bitfield.size = CInline_CopyExpression(expr, CopyModeGlobal);
        bde->declinfo2.thetype = TYPE(ttempl);
        bde->valid = TRUE;
        return;
    }
    val = CInt64_GetULong(&expr->data.intval);
    if (is_bitfield) {
        if (val < 0 || val > bits) {
            CError_Error(ERR_ILLEGAL_BITFIELD_DECLARATION);
            return;
        }
    } else {
        if (val <= 0 || val > bits) {
            CError_Error(ERR_ILLEGAL_BITFIELD_DECLARATION);
            return;
        }
    }

    tbitfield = galloc(sizeof(TypeBitfield));
    memclrw(tbitfield, sizeof(TypeBitfield));
    tbitfield->type = TYPEBITFIELD;
    tbitfield->size = bde->declinfo2.thetype->size;
    tbitfield->bitfieldtype = bde->declinfo2.thetype;
    tbitfield->bitlength = val;
    tbitfield->suppressAlignment = is_bitfield;
    bde->declinfo2.thetype = TYPE(tbitfield);

    if (tk == TK_UU_ATTRIBUTE_UU) {
        CParser_ParseAttribute(NULL, &bde->declinfo2);
    }

not_a_bitfield:
    bde->valid = TRUE;
}

static void CDecl_LayoutStruct(TypeStruct* tstruct) {
    StructMember* member;
    SInt32 unionSize;
    StructMember* innermember;
    SInt32 innerbase;
    StructMember* newmember;
    StructMember** memberp;
    TypeBitfield* bf;
    SInt32 size;
    // also set while an anonymous member's first inner member is to take its place
    Boolean removeNoNames;
    SInt32 curSize; // NOTE: invented name

    unionSize = 0;
    removeNoNames = FALSE;
    CMach_StructLayoutInitOffset(0);
    for (member = tstruct->members; member; member = member->next) {
        if (tstruct->stype == STRUCT_TYPE_UNION) {
            CMach_StructLayoutInitOffset(0);
        }

        if (IS_TYPE_BITFIELD(member->type)) {
            member->offset = CMach_StructLayoutBitfield(TYPE_BITFIELD(member->type), member->qual);
        } else {
            member->offset = CMach_StructLayoutGetOffset(member->type, member->qual);
        }

        if (tstruct->stype == STRUCT_TYPE_UNION) {
            curSize = CMach_StructLayoutGetCurSize();
            if (curSize > unionSize) {
                unionSize = curSize;
            }
        }

        if (member->name == no_name_node) {
            removeNoNames = TRUE;
        }

        if (!member->name) {
            CError_ASSERT(4129, IS_TYPE_STRUCT(member->type));
            innerbase = member->offset;
            innermember = TYPE_STRUCT(member->type)->members;
            removeNoNames = TRUE;
            while (innermember) {
                if (ismember(tstruct, innermember->name)) {
                    CError_Error(ERR_STRUCT_UNION_CLASS_MEMBER_REDEFINED, innermember->name->name);
                }
                if (removeNoNames) {
                    member->type = innermember->type;
                    member->name = innermember->name;
                    member->qual = innermember->qual;
                    member->offset = innerbase + innermember->offset;
                } else {
                    newmember = galloc(sizeof(StructMember));
                    memclrw(newmember, sizeof(StructMember));
                    newmember->next = member->next;
                    newmember->type = innermember->type;
                    newmember->name = innermember->name;
                    newmember->qual = innermember->qual | Q_WEAK;
                    newmember->offset = innerbase + innermember->offset;
                    member->next = newmember;
                    member = newmember;
                }
                if (copts.reverse_bitfields && IS_TYPE_BITFIELD(member->type)) {
                    bf = galloc(sizeof(TypeBitfield));
                    *bf = *TYPE_BITFIELD(member->type);
                    CABI_ReverseBitField(bf);
                    member->type = TYPE(bf);
                }
                removeNoNames = FALSE;
                innermember = innermember->next;
            }
            removeNoNames = TRUE;
        }
    }

    if (removeNoNames) {
        memberp = &tstruct->members;
        while (*memberp) {
            if ((*memberp)->name == no_name_node || !(*memberp)->name) {
                *memberp = (*memberp)->next;
            } else {
                memberp = &(*memberp)->next;
            }
        }
    }

    if (tstruct->stype == STRUCT_TYPE_UNION) {
        size = unionSize;
    } else {
        size = CMach_StructLayoutGetCurSize();
    }
    tstruct->size = size;
    tstruct->align = CMach_GetStructAlign(tstruct);
    tstruct->size = size + CABI_StructSizeAlignValue(TYPE(tstruct), size);

    if (copts.reverse_bitfields) {
        for (member = tstruct->members; member; member = member->next) {
            if (IS_TYPE_BITFIELD(member->type)) {
                CABI_ReverseBitField(TYPE_BITFIELD(member->type));
            }
        }
    }

    if (copts.warn_padding && tstruct->stype != STRUCT_TYPE_UNION) {
        StructMember* prev;

        member = tstruct->members;
        prev = NULL;
        while (member) {
            if (prev && (prev->offset + prev->type->size) < member->offset) {
                CError_Warning(
                    ERR_PAD_BYTES_INSERTED_AFTER_DATA_MEMBER,
                    member->offset - (prev->offset + prev->type->size),
                    prev->name->name
                );
            }
            prev = member;
            member = member->next;
        }

        if (prev && (prev->offset + prev->type->size) < tstruct->size) {
            CError_Warning(
                ERR_PAD_BYTES_INSERTED_AFTER_DATA_MEMBER,
                tstruct->size - (prev->offset + prev->type->size),
                prev->name->name
            );
        }
    }
}

// NOTE: invented names: tstruct, addToBrowse
static SInt32 scanstructdeclarationlist(TypeStruct* tstruct, Boolean addToBrowse) {
    SInt32 offset;
    StructMember* member;
    BigDeclInfo bde;

    offset = -1;
    memclrw(&bde, sizeof(BigDeclInfo));

    if (tk == TK_AT_DEFS) {
        CPrep_NewFileOffsetInfo(&member_fileoffset, NULL);
        CObjC_ParseDefs(tstruct);
        if ((tk = lex()) != '}') {
            CError_Error(ERR_RBRACE_EXPECTED);
        }
    } else {
        do {
            CPrep_NewFileOffsetInfo(&member_fileoffset, NULL);
            memclrw(&bde.declinfo, sizeof(DeclInfo));
            CParser_GetDeclSpecs(&bde.declinfo, FALSE);
            if (bde.declinfo.storageclass || bde.declinfo.hasParameterNames) {
                CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
                tstruct->members = NULL;
                return -1;
            }

            if (tk != ';') {
                while (1) {
                    CDecl_ScanStructDeclarator(&bde);
                    if (!CanCreateObject(bde.declinfo2.thetype)) {
                        CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
                        bde.valid = FALSE;
                    }

                    if (bde.declinfo2.operator_token) {
                        CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
                        bde.valid = FALSE;
                    }

                    if (bde.valid) {
                        if (bde.declinfo2.name == no_name_node ||
                            !ismember(tstruct, bde.declinfo2.name)) {
                            member = galloc(sizeof(StructMember));
                            memclrw(member, sizeof(StructMember));
                            member->type = bde.declinfo2.thetype;
                            member->name = bde.declinfo2.name;
                            member->qual = bde.declinfo2.qual;
                            appendmember(tstruct, member);

                            if (addToBrowse) {
                                CBrowse_AddStructMember(
                                    member,
                                    CPrep_BrowserTokenOffset(&member_fileoffset) + 1,
                                    CPrep_BrowserFileOffset()
                                );
                            }
                        } else {
                            CError_Error(
                                ERR_STRUCT_UNION_CLASS_MEMBER_REDEFINED, bde.declinfo2.name->name
                            );
                        }
                    }

                    if (tk != ',') {
                        break;
                    }
                    tk = lex();
                }
            } else if (!copts.ANSIstrict && IS_TYPE_STRUCT(bde.declinfo.thetype)) {
                member = galloc(sizeof(StructMember));
                memclrw(member, sizeof(StructMember));
                member->type = bde.declinfo.thetype;
                appendmember(tstruct, member);
            } else {
                CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
            }

            if (tk != ';') {
                tstruct->members = NULL;
                CError_Error(ERR_SEMICOLON_EXPECTED);
                return -1;
            }

            CPrep_TokenStreamFlush();
        } while ((tk = lex()) != '}');
        CDecl_LayoutStruct(tstruct);
    }

    if (addToBrowse) {
        offset = CPrep_BrowserFileOffset();
        if (tk == ';') {
            offset++;
        }
    }

    tk = lex();
    return offset;
}

static TypeStruct* CDecl_DefineStruct(HashNameNode* name, short stype) {
    TypeStruct* tstruct;

    tstruct = galloc(sizeof(TypeStruct));
    memclrw(tstruct, sizeof(TypeStruct));

    tstruct->type = TYPESTRUCT;
    tstruct->align = 1;
    tstruct->stype = stype;
    if (name) {
        tstruct->name = name;
        CScope_DefineTypeTag(
            (in_func_arglist && !copts.cplusplus) ? cscope_root : cscope_current,
            name,
            (Type*)tstruct
        );
    }

    return tstruct;
}

void scanstruct(DeclInfo* declinfo, short structtype) {
    Type* type;
    HashNameNode* name;
    TypeStruct typecopy;
    Boolean add_to_browse;
    GList gl;
    SInt32 offset;

    if (copts.cplusplus) {
        CDecl_ParseClass(declinfo, structtype, TRUE, 0);
        return;
    }

    if (tk == TK_IDENTIFIER) {
        name = tkidentifier;
        type = CScope_GetTagType(cscope_current, name);
        if (type) {
            if (IS_TYPE_CLASS(type)) {
                CDecl_ParseClass(declinfo, structtype, TRUE, 0);
                return;
            }

            tk = lex();
            if (!CScope_GetLocalTagType(cscope_current, name) && (tk == ';' || tk == '{')) {
                type = (Type*)CDecl_DefineStruct(name, structtype);
            }

            if (!IS_TYPE_STRUCT(type) || TYPE_STRUCT(type)->stype != structtype) {
                CError_Error(ERR_STRUCT_UNION_ENUM_CLASS_TAG_REDEFINED, name->name);
                declinfo->thetype = type;
                return;
            }

            if (tk != '{') {
                declinfo->thetype = type;
                return;
            }

            if (type->size) {
                CError_Error(ERR_STRUCT_UNION_ENUM_CLASS_TAG_REDEFINED, name->name);
                type = (Type*)CDecl_DefineStruct(NULL, structtype);
            }
        } else {
            type = (Type*)CDecl_DefineStruct(name, structtype);
            if ((tk = lex()) != '{') {
                declinfo->thetype = type;
                return;
            }
        }
    } else if (tk != '{') {
        CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
        declinfo->thetype = (Type*)&stsignedint;
        return;
    } else {
        type = (Type*)CDecl_DefineStruct(NULL, structtype);
    }

    if ((add_to_browse =
             cparamblkptr->browseoptions.recordClasses && declinfo->file->recordbrowseinfo)) {
        CBrowse_BeginStruct(declinfo, TYPE_STRUCT(type), &gl);
    }

    typecopy = *TYPE_STRUCT(type);
    tk = lex();
    offset = scanstructdeclarationlist(&typecopy, add_to_browse);
    *TYPE_STRUCT(type) = typecopy;
    declinfo->thetype = type;

    if (add_to_browse) {
        CBrowse_EndStruct(offset, &gl);
    }
}

static void InlineFunctionObject(Object* obj, TypeClass* tclass) {
    TokenStream stream;
    CPrepFileInfo* file;

    obj->qual |= Q_INLINE;
    TYPE_FUNC(obj->type)->flags |= FUNC_DEFINED;

    CPrep_StreamGetBlock(&stream, NULL, 1);
    if (stream.tokens) {
        if (IS_TYPEFUNC_METHOD(TYPE_FUNC(obj->type)) &&
            (TYPE_METHOD(obj->type)->theclass->flags & CLASS_IS_TEMPL)) {
            TYPE_FUNC(obj->type)->flags |= FUNC_IS_TEMPL_INSTANCE;
            CTemplClass_DefineMember(
                TEMPL_CLASS(TYPE_METHOD(obj->type)->theclass), obj, &member_fileoffset, &stream
            );
        } else {
            CInline_AddInlineFunctionAction(obj, tclass, &member_fileoffset, &stream, FALSE);
        }
    }

    file = CPrep_BrowserCurrentFile();
    if (file->recordbrowseinfo) {
        CBrowse_NewFunction(
            obj,
            file,
            member_fileoffset.file,
            CPrep_BrowserTokenOffset(&member_fileoffset) + 1,
            CPrep_BrowserFileOffset()
        );
    }

    if (lookahead() == ';') {
        tk = lex();
    } else {
        tk = ';';
    }
}

void CDecl_ExtractClassExportFlags(DeclInfo* declinfo, UInt8 flags) {
    if (flags & CLASS_EFLAGS_INTERNAL) {
        declinfo->exportflags |= EXPORT_FLAGS_INTERNAL;
    }
    if (flags & CLASS_EFLAGS_IMPORT) {
        declinfo->exportflags |= EXPORT_FLAGS_IMPORT;
    }
    if (flags & CLASS_EFLAGS_EXPORT) {
        declinfo->exportflags |= EXPORT_FLAGS_EXPORT;
    }
}

// NOTE: invented names: tfunc, tclass, isStatic
TypeMemberFunc* CDecl_MakeTypeMemberFunc(TypeFunc* tfunc, TypeClass* tclass, Boolean isStatic) {
    TypeFunc* method;

    method = galloc(sizeof(TypeMemberFunc));
    memclrw(method, sizeof(TypeMemberFunc));
    *method = *tfunc;
    TYPE_METHOD(method)->theclass = tclass;
    TYPE_METHOD(method)->is_static = isStatic;
    method->flags |= FUNC_METHOD;
    if (!isStatic) {
        CDecl_AddThisPointerArgument(method, tclass);
    }

    if ((isStatic || (tfunc->flags & (FUNC_IS_CTOR | FUNC_IS_DTOR))) &&
        (tfunc->flags & (FUNC_CONST | FUNC_VOLATILE))) {
        CError_Error(ERR_ILLEGAL_CV_FUNCTION_QUALIFIER_SEQUENCE);
    }

    return TYPE_METHOD(method);
}

static void CDecl_MakeFunctionVirtual(TypeClass* tclass, Object* func) {
    if (is_pascal_object(func)) {
        CError_Error(ERR_VIRTUAL_FUNCTIONS_CANNOT_PASCAL_FUNCTIONS);
    }
    if (tclass->mode == CLASS_MODE_UNION) {
        CError_Error(ERR_ILLEGAL_VIRTUAL_FUNCTION_UNION, func);
    }
    func->datatype = DVFUNC;
}

// NOTE: invented names: decle, tclass, declinfo, access, allowPure, isConversion, allowDefinition,
// isStatic, isNew
static void CDecl_AddFunctionMember(
    ClassLayout* decle,
    TypeClass* tclass,
    DeclInfo* declinfo,
    AccessType access,
    Boolean allowPure,
    Boolean isConversion,
    Boolean allowDefinition,
    Boolean isStatic
) {
    NameSpaceObjectList* list;
    Object* obj;
    TypeMemberFunc* tfunc;
    Boolean isVirtual;
    Boolean isNew;

    if (IS_TYPE_ARRAY(TYPE_FUNC(declinfo->thetype)->functype) ||
        IS_TYPE_FUNC(TYPE_FUNC(declinfo->thetype)->functype)) {
        CError_Error(ERR_ILLEGAL_FUNCTION_RETURN_TYPE);
        TYPE_FUNC(declinfo->thetype)->functype = (Type*)&stsignedint;
    }

    if (tclass->sominfo) {
        CSOM_CheckFuncType(TYPE_FUNC(declinfo->thetype));
    }

    isVirtual = FALSE;
    if (declinfo->qual & Q_VIRTUAL) {
        declinfo->qual &= ~Q_VIRTUAL;
        isVirtual = TRUE;
        allowPure = TRUE;
    }

    if ((list = CScope_FindName(tclass->nspace, declinfo->name))) {
        if (list->object->otype != OT_TYPETAG) {
            if (list->object->otype != OT_OBJECT || !IS_TYPE_FUNC(OBJECT(list->object)->type)) {
                CError_Error(ERR_STRUCT_UNION_CLASS_MEMBER_REDEFINED, declinfo->name->name);
            }
        } else {
            list = NULL;
        }
    }

    if (!IS_TYPEFUNC_METHOD(TYPE_FUNC(declinfo->thetype))) {
        tfunc = CDecl_MakeTypeMemberFunc(TYPE_FUNC(declinfo->thetype), tclass, isStatic);
        declinfo->thetype = (Type*)tfunc;
    } else {
        tfunc = TYPE_METHOD(declinfo->thetype);
        CError_ASSERT(4644, !tclass->sominfo);
    }

    CDecl_ExtractClassExportFlags(declinfo, tclass->eflags);

    CError_ASSERT(4662, cscope_current == tclass->nspace);

    if (list) {
        obj = CDecl_OverloadFunctionObject(
            list,
            declinfo,
            &isNew,
            isStatic ? OverloadMode_StaticMember : OverloadMode_Member,
            FALSE
        );
        if (!obj) {
            return;
        }
        if (isNew) {
            tfunc->vtbl_index = ++decle->lex_order_count;
        } else {
            CError_Error(ERR_STRUCT_UNION_CLASS_MEMBER_REDEFINED, CError_GetObjectName(obj));
        }
    } else {
        tfunc->vtbl_index = ++decle->lex_order_count;
        obj = CParser_NewFunctionObject(declinfo);
        if ((tclass->flags & CLASS_IS_TEMPL) &&
            CTemplTool_IsTemplateArgumentDependentType(declinfo->thetype)) {
            CTemplClass_RegisterObjectDef(TEMPL_CLASS(tclass), OBJ_BASE(obj));
        }
        CScope_AddObject(tclass->nspace, declinfo->name, OBJ_BASE(obj));
    }

    obj->access = access;
    CheckDefaultArgs(TYPE_FUNC(obj->type)->args);

    if (isConversion) {
        tfunc->flags |= FUNC_CONVERSION;
        tclass->flags |= CLASS_IS_CONVERTIBLE;
    }

    if (isVirtual) {
        CDecl_MakeFunctionVirtual(tclass, obj);
        decle->has_vtable = TRUE;
    }

    if ((allowPure || isVirtual) && allowDefinition && (tk == '=')) {
        if ((tk = lex()) == TK_INTCONST) {
            if (!CInt64_IsZero(&tkintconst)) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            tfunc->flags |= FUNC_PURE;
            tclass->flags = tclass->flags | CLASS_ABSTRACT;
            tk = lex();
        } else {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        }
    }

    if (allowDefinition &&
        ((tk == '{') || (tk == TK_TRY) || ((tk == ':') && CClass_IsConstructor(obj)))) {
        if (declinfo->isType) {
            CError_Error(ERR_ILLEGAL_FUNCTION_DEFINITION);
        }
        InlineFunctionObject(obj, NULL);
    }

    if (cparamblkptr->browseoptions.recordClasses) {
        CBrowse_AddClassMemberFunction(
            obj, CPrep_BrowserTokenOffset(&member_fileoffset) + 1, CPrep_BrowserFileOffset()
        );
    }
}

static Boolean CDecl_IsAccessDeclaration(TypeClass* tclass, AccessType access) {
    SInt32 state;
    Boolean qualified; // NOTE: invented name

    CPrep_TokenStreamGetState(&state);
    qualified = FALSE;

restart:
    switch (tk) {
        case TK_IDENTIFIER:
            if ((tk = lex()) != ';') {
                break;
            }
        case TK_OPERATOR:
            CPrep_TokenStreamSetCurState(&state);
            if (qualified) {
                CScope_ParseUsingDeclaration(tclass->nspace, access, TRUE);
                return TRUE;
            }
            return FALSE;
        default:
            CPrep_TokenStreamSetCurState(&state);
            return FALSE;
    }

    switch (tk) {
        case TK_COLON_COLON:
            qualified = TRUE;
            tk = lex();
            goto restart;
        case '<':
            tk = lex();
            while (1) {
                switch (tk) {
                    case 0:
                    case ';':
                    case '{':
                    case '}':
                        CPrep_TokenStreamSetCurState(&state);
                        return FALSE;
                    case '>':
                        if ((tk = lex()) == TK_COLON_COLON) {
                            qualified = TRUE;
                            tk = lex();
                            goto restart;
                        }
                    default:
                        tk = lex();
                }
            }
    }

    CPrep_TokenStreamSetCurState(&state);
    return FALSE;
}

void CDecl_PackDeclInfo(PackedDeclInfo* packed, DeclInfo* declinfo) {
    packed->thetype = declinfo->thetype;
    packed->qual = declinfo->qual;
    packed->nspace = declinfo->nspace;
    packed->name = declinfo->name;
    packed->expltargs = CTemplTool_MakeGlobalTemplArgCopy(declinfo->expltargs);
    packed->storageclass = declinfo->storageclass;
    packed->section = declinfo->section;
    packed->exportflags = declinfo->exportflags;
    packed->has_expltargs = declinfo->has_expltargs;
}

void CDecl_UnpackDeclInfo(DeclInfo* declinfo, PackedDeclInfo* packed) {
    memclrw(declinfo, sizeof(DeclInfo));
    declinfo->thetype = packed->thetype;
    declinfo->qual = packed->qual;
    declinfo->nspace = packed->nspace;
    declinfo->name = packed->name;
    declinfo->expltargs = packed->expltargs;
    declinfo->storageclass = packed->storageclass;
    declinfo->section = packed->section;
    declinfo->exportflags = packed->exportflags;
    declinfo->has_expltargs = packed->has_expltargs;
}

void CDecl_AddFriend(TypeClass* tclass, Object* friendfunc, TypeClass* friendclass) {
    ClassFriend* scan;
    ClassFriend* newfriend;

    if (friendfunc) {
        for (scan = tclass->friends; scan; scan = scan->next) {
            if (!scan->isclass && scan->u.obj == friendfunc) {
                break;
            }
        }
        if (!scan) {
            newfriend = galloc(sizeof(ClassFriend));
            memclrw(newfriend, sizeof(ClassFriend));
            newfriend->next = tclass->friends;
            tclass->friends = newfriend;
            newfriend->u.obj = friendfunc;
            newfriend->isclass = FALSE;
        }
    }

    if (friendclass) {
        for (scan = tclass->friends; scan; scan = scan->next) {
            if (scan->isclass && scan->u.theclass == friendclass) {
                break;
            }
        }
        if (!scan) {
            newfriend = galloc(sizeof(ClassFriend));
            memclrw(newfriend, sizeof(ClassFriend));
            newfriend->next = tclass->friends;
            tclass->friends = newfriend;
            newfriend->u.theclass = friendclass;
            newfriend->isclass = TRUE;
        }
    }
}

static void CDecl_ParseFriendDecl(TypeClass* tclass) {
    DeclInfo declinfo;
    DeclInfo declinfo_copy;
    Boolean is_templ;
    Boolean is_class_key;
    Boolean firstDecl; // NOTE: invented name
    CScopeSave save;
    Object* obj;
    NameSpace* nspace;

    is_templ = (tclass->flags & CLASS_IS_TEMPL) != 0;
    is_class_key = ((int)tk == TK_CLASS || (int)tk == TK_STRUCT || (int)tk == TK_UNION);
    memclrw(&declinfo, sizeof(DeclInfo));

    declinfo.in_friend_decl = TRUE;
    CParser_GetDeclSpecs(&declinfo, TRUE);
    if (declinfo.storageclass) {
        CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
        declinfo.storageclass = 0;
    }
    declinfo.in_friend_decl = FALSE;

    if (tk == ';') {
        if (!is_class_key) {
            CError_Error(ERR_ILLEGAL_FRIEND_DECLARATION);
        }

        if (IS_TYPE_CLASS(declinfo.thetype)) {
            if (!(TYPE_CLASS(declinfo.thetype)->flags & CLASS_IS_TEMPL) ||
                CParser_CheckTemplateClassUsage(TEMPL_CLASS(declinfo.thetype), TRUE)) {
                if (!is_templ) {
                    CDecl_AddFriend(tclass, NULL, TYPE_CLASS(declinfo.thetype));
                } else {
                    CTemplClass_RegisterFriend(TEMPL_CLASS(tclass), &declinfo);
                }
            }
        } else {
            if (IS_TYPE_TEMPLATE(declinfo.thetype) && is_templ) {
                CTemplClass_RegisterFriend(TEMPL_CLASS(tclass), &declinfo);
            } else {
                CError_Error(ERR_ILLEGAL_FRIEND_DECLARATION);
            }
        }
    } else {
        if (declinfo.resolvedObjects || declinfo.resolvedObject) {
            CDecl_ParseSpecialMember(&declinfo, FALSE);
            if (!declinfo.name) {
                return;
            }
            obj = CDecl_GetFunctionObject(&declinfo, NULL, &firstDecl, FALSE);
            if (obj) {
                CDecl_AddFriend(tclass, obj, NULL);
            }
            if (tk != ';') {
                CError_Error(ERR_SEMICOLON_EXPECTED);
            } else {
                tk = lex();
            }
            return;
        } else {
            nspace = CScope_FindGlobalNS(cscope_current);
            declinfo_copy = declinfo;
            while (1) {
                declinfo = declinfo_copy;
                declinfo.isFriendDeclaration = TRUE;
                declinfo.allowTemplateArguments = TRUE;
                scandeclarator(&declinfo);

                if (IS_TYPE_FUNC(declinfo.thetype)) {
                    if (!is_templ) {
                        CScope_SetNameSpaceScope(nspace, &save);
                        obj = CDecl_GetFunctionObject(&declinfo, NULL, &firstDecl, FALSE);
                        CScope_RestoreScope(&save);

                        if (obj) {
                            CDecl_AddFriend(tclass, obj, NULL);
                            if (!declinfo.nspace && tk == '{') {
                                InlineFunctionObject(obj, tclass);
                            } else {
                                if (!obj->sclass) {
                                    obj->sclass = TK_EXTERN;
                                }
                            }
                        }
                    } else {
                        CTemplClass_RegisterFriend(TEMPL_CLASS(tclass), &declinfo);
                    }
                } else {
                    CError_Error(ERR_ILLEGAL_FRIEND_DECLARATION);
                }

                if (tk != ',') {
                    break;
                }
                tk = lex();
            }
        }
    }

    if (tk == ';') {
        tk = lex();
    } else {
        CError_Error(ERR_SEMICOLON_EXPECTED);
    }
}

static ObjMemberVar* CDecl_InstanceDataDeclarator(
    ClassLayout* decle,
    TypeClass* tclass,
    Type* type,
    UInt32 qual,
    HashNameNode* name,
    AccessType access
) {
    NameSpaceObjectList* list;
    ObjMemberVar* ivar;
    ObjMemberVar* scan;

    if (name && (list = CScope_FindName(tclass->nspace, name))) {
        switch (list->object->otype) {
            case OT_NAMESPACE:
                CError_Error(ERR_ILLEGAL_USE_NAMESPACE_NAME);
                return NULL;
            case OT_ENUMCONST:
            case OT_TYPE:
            case OT_OBJECT:
                CError_Error(ERR_ILLEGAL_NAME_OVERLOADING);
                return NULL;
            case OT_MEMBERVAR:
                CError_Error(ERR_IDENTIFIER_REDECLARED, name->name);
                return NULL;
            case OT_TYPETAG:
                break;
            default:
                CError_FATAL(5054);
        }
    }

    ivar = galloc(sizeof(ObjMemberVar));
    memclrw(ivar, sizeof(ObjMemberVar));
    ivar->otype = OT_MEMBERVAR;
    ivar->access = access;
    ivar->name = name;
    ivar->type = type;
    ivar->qual = qual;
    if (!tclass->sominfo) {
        decle->lex_order_count++;
    }

    if ((scan = tclass->ivars)) {
        while (scan->next) {
            scan = scan->next;
        }
        scan->next = ivar;
    } else {
        tclass->ivars = ivar;
    }

    if (name && name != no_name_node) {
        CScope_AddObject(tclass->nspace, name, OBJ_BASE(ivar));
        if ((tclass->flags & CLASS_IS_TEMPL) && CTemplTool_IsTemplateArgumentDependentType(type)) {
            CTemplClass_RegisterObjectDef(TEMPL_CLASS(tclass), OBJ_BASE(ivar));
        }
        if (cparamblkptr->browseoptions.recordClasses) {
            CBrowse_AddClassMemberVar(
                ivar, CPrep_BrowserTokenOffset(&member_fileoffset) + 1, CPrep_BrowserFileOffset()
            );
        }
    }

    return ivar;
}

// FALSE for a copy constructor that takes its class by value; the callers report the error
Boolean CDecl_CheckCtorIntegrity(FuncArg* args, TypeClass* tclass) {
    if (args && args->type == TYPE(tclass)) {
        if (!args->next || args->next->dexpr) {
            args->type = &stvoid;
            return FALSE;
        }
    }
    return TRUE;
}

static void CDecl_ParseClassMembers(ClassLayout* decle, TypeClass* tclass, short mode) {
    AccessType access;
    Boolean resolveSelfRefs;
    BigDeclInfo bde;
    DeclInfo declinfo;
    ObjMemberVar* ivar;
    ObjMemberVar* scanivar;
    Type* memberType; // NOTE: invented name
    UInt32 declspecQual;
    UInt8 exportflags;
    SInt16 section;
    Boolean isCtorName;
    short t;
    UInt32 qual;

    resolveSelfRefs = (tclass->flags & CLASS_IS_TEMPL) && TEMPL_CLASS(tclass)->pspec_owner;
    memclrw(&bde, sizeof(BigDeclInfo));

    access = (mode == CLASS_MODE_CLASS) ? ACCESSPRIVATE : ACCESSPUBLIC;
    global_access = access;

restart:
    while (tk != '}') {
        CPrep_NewFileOffsetInfo(&member_fileoffset, NULL);
        declspecQual = 0;
        qual = 0;
        exportflags = 0;
        section = 0;

        if (tk == TK_TEMPLATE) {
            NameSpace* nspace = cscope_current;

            if (!nspace->theclass || !(nspace->theclass->flags & CLASS_IS_TEMPL)) {
                for (; nspace; nspace = nspace->parent) {
                    if (!nspace->name && !nspace->theclass && nspace->parent && !nspace->is_templ) {
                        CError_Error(ERR_LOCAL_CLASSES_SHALL_NOT_MEMBER_TEMPLATES);
                        break;
                    }
                }
            }

            CTempl_Parse(TEMPL_CLASS(tclass), access);
            tk = lex();
            continue;
        }

restart2:
        isCtorName = FALSE;
        switch (tk) {
            case TK_UU_DECLSPEC:
                if ((tk = lex()) != '(') {
                    CError_Error(ERR_LPAREN_EXPECTED);
                }
                memclrw(&declinfo, sizeof(DeclInfo));
                CParser_ParseDeclSpec(&declinfo, TRUE);
                declspecQual |= declinfo.qual;
                exportflags |= declinfo.exportflags;
                section = declinfo.section;
                if ((tk = lex()) != ')') {
                    CError_Error(ERR_RPAREN_EXPECTED);
                }
                tk = lex();
                goto restart2;
            case TK_PRIVATE:
                global_access = access = ACCESSPRIVATE;
                goto check_access;
            case TK_PROTECTED:
                global_access = access = ACCESSPROTECTED;
                goto check_access;
            case TK_PUBLIC:
                global_access = access = ACCESSPUBLIC;
check_access:
                if (qual || exportflags) {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                }
                if ((tk = lex()) != ':') {
                    CError_Error(ERR_COLON_EXPECTED);
                } else {
                    tk = lex();
                }
                goto restart;
            case TK_EXPLICIT:
                CError_QualifierCheck(qual & Q_EXPLICIT);
                qual |= Q_EXPLICIT;
                tk = lex();
                goto restart2;
            case TK_INLINE:
                CError_QualifierCheck(qual & Q_INLINE);
                qual |= Q_INLINE;
                tk = lex();
                goto restart2;
            case TK_ASM:
                CError_QualifierCheck(qual & Q_ASM);
                qual |= Q_ASM;
                tk = lex();
                goto restart2;
            case TK_VIRTUAL:
                CError_QualifierCheck(qual & Q_VIRTUAL);
                qual |= Q_VIRTUAL;
                tk = lex();
                goto restart2;
            case TK_IDENTIFIER:
                while (1) {
                    if (tkidentifier == tclass->classname) {
                        t = lookahead();
                        tkidentifier = tclass->classname;
                        isCtorName = TRUE;
                        if (copts.cpp_extensions && t == TK_COLON_COLON) {
                            lex();
                            tk = lex();
                            if (tk == TK_IDENTIFIER) {
                                continue;
                            }
                            if (tk == '~') {
                                goto restart2;
                            }
                            CError_Error(ERR_IDENTIFIER_EXPECTED);
                        }

                        if (t == '(') {
redo_thing:
                            CError_QualifierCheck(qual & ~(Q_EXPLICIT | Q_INLINE | Q_ASM));
                            memclrw(&bde.declinfo2, sizeof(DeclInfo));
                            if (tclass->sominfo) {
                                bde.declinfo2.thetype = &stvoid;
                            } else {
                                bde.declinfo2.thetype = TYPE(&void_ptr);
                            }

                            bde.declinfo2.qual = qual;
                            bde.declinfo2.exportflags = exportflags;
                            bde.declinfo2.section = section;
                            bde.declinfo2.isConstructor = TRUE;
                            scandeclarator(&bde.declinfo2);
                            if (IS_TYPE_FUNC(bde.declinfo2.thetype)) {
                                if (resolveSelfRefs) {
                                    bde.declinfo2.thetype = CTemplTool_ResolveMemberSelfRefs(
                                        TEMPL_CLASS(tclass),
                                        bde.declinfo2.thetype,
                                        &bde.declinfo2.qual
                                    );
                                }
                                if (tclass->sominfo) {
                                    if (TYPE_FUNC(bde.declinfo2.thetype)->args) {
                                        CError_Error(
                                            ERR_NO_PARAMETERS_ALLOWED_SOM_CLASS_CONSTRUCTORS
                                        );
                                    }
                                    bde.declinfo2.qual |= Q_VIRTUAL;
                                } else {
                                    if (!CDecl_CheckCtorIntegrity(
                                            TYPE_FUNC(bde.declinfo2.thetype)->args, tclass
                                        )) {
                                        CError_Error(ERR_ILLEGAL_COPY_CONSTRUCTOR);
                                    }
                                    if (tclass->flags & CLASS_HAS_VBASES) {
                                        CDecl_AddArgument(
                                            TYPE_FUNC(bde.declinfo2.thetype), TYPE(&stsignedshort)
                                        );
                                    }
                                    bde.declinfo2.qual &= ~Q_VIRTUAL;
                                }
                                TYPE_FUNC(bde.declinfo2.thetype)->flags |= FUNC_IS_CTOR;
                                bde.declinfo2.name = constructor_name_node;
                                bde.declinfo2.qual |= declspecQual;
                                CDecl_AddFunctionMember(
                                    decle, tclass, &bde.declinfo2, access, FALSE, FALSE, TRUE, FALSE
                                );
                            } else {
                                CError_Error(ERR_ILLEGAL_CONSTRUCTOR_DESTRUCTOR_DECLARATION);
                            }
                            if (tk == ';') {
                                tk = lex();
                            } else {
                                CError_Error(ERR_SEMICOLON_EXPECTED);
                            }
                            goto restart;
                        }
                    }
                    if (!qual && CDecl_IsAccessDeclaration(tclass, access)) {
                        tk = lex();
                        goto restart;
                    }
                    break;
                }
                break;
            case TK_USING:
                CError_QualifierCheck(qual);
                tk = lex();
                CScope_ParseUsingDeclaration(tclass->nspace, access, FALSE);
                tk = lex();
                goto restart;
            case '~':
                if ((tk = lex()) != TK_IDENTIFIER || tkidentifier != tclass->classname) {
                    CError_Error(ERR_ILLEGAL_CONSTRUCTOR_DESTRUCTOR_DECLARATION);
                    goto restart;
                }
                CError_QualifierCheck(qual & ~(Q_VIRTUAL | Q_INLINE | Q_ASM));
                if (tclass->flags & CLASS_IS_TEMPL_ANY) {
                    t = lookahead();
                    tkidentifier = tclass->classname;
                    if (t == '<') {
                        memclrw(&bde.declinfo, sizeof(DeclInfo));
                        CParser_GetDeclSpecs(&bde.declinfo, FALSE);
                        if (tk != '(' ||
                            bde.declinfo.thetype != TYPE(tclass) ||
                            bde.declinfo.nspace) {
                            CError_Error(ERR_ILLEGAL_CONSTRUCTOR_DESTRUCTOR_DECLARATION);
                            goto restart;
                        }
                        CPrep_UnLex();
                        tk = TK_IDENTIFIER;
                        tkidentifier = tclass->classname;
                    }
                }
                memclrw(&bde.declinfo2, sizeof(DeclInfo));
                bde.declinfo2.qual = qual;
                bde.declinfo2.exportflags = exportflags;
                bde.declinfo2.section = section;
                if (tclass->sominfo) {
                    bde.declinfo2.thetype = &stvoid;
                } else {
                    bde.declinfo2.thetype = TYPE(&void_ptr);
                }
                scandeclarator(&bde.declinfo2);
                if (IS_TYPE_FUNC(bde.declinfo2.thetype) &&
                    !TYPE_FUNC(bde.declinfo2.thetype)->args) {
                    if (!CScope_FindName(tclass->nspace, destructor_name_node)) {
                        if (tclass->sominfo) {
                            bde.declinfo2.qual |= Q_VIRTUAL;
                        } else {
                            CDecl_AddArgument(
                                TYPE_FUNC(bde.declinfo2.thetype), TYPE(&stsignedshort)
                            );
                        }
                        bde.declinfo2.name = destructor_name_node;
                        TYPE_FUNC(bde.declinfo2.thetype)->flags |= FUNC_IS_DTOR;
                        bde.declinfo2.qual |= declspecQual;
                        CDecl_AddFunctionMember(
                            decle, tclass, &bde.declinfo2, access, TRUE, FALSE, TRUE, FALSE
                        );
                    } else {
                        CError_Error(
                            ERR_STRUCT_UNION_CLASS_MEMBER_REDEFINED,
                            CError_GetFunctionName(tclass->nspace, destructor_name_node, NULL)
                        );
                    }
                } else {
                    CError_Error(ERR_ILLEGAL_TYPE);
                }
                if (tk == ';') {
                    tk = lex();
                } else {
                    CError_Error(ERR_SEMICOLON_EXPECTED);
                }
                goto restart;
            case TK_OPERATOR:
                if (CMangler_OperatorName(lookahead())) {
                    memclrw(&bde.declinfo, sizeof(DeclInfo));
                    bde.declinfo.thetype = TYPE(&stsignedint);
                    goto after_various_things;
                } else {
                    tk = lex();
                    CError_QualifierCheck(qual & ~(Q_VIRTUAL | Q_INLINE | Q_ASM));
                    memclrw(&bde.declinfo2, sizeof(DeclInfo));
                    bde.declinfo2.qual = qual;
                    bde.declinfo2.exportflags = exportflags;
                    bde.declinfo2.section = section;
                    conversion_type_name(&bde.declinfo2);
                    bde.declinfo2.qual |= declspecQual;
                    memberType = bde.declinfo2.thetype;
                    CDecl_NewConvFuncType(&bde.declinfo2);
                    if ((tclass->flags & CLASS_IS_TEMPL) &&
                        CTemplTool_IsTemplateArgumentDependentType(memberType)) {
                        bde.declinfo2.name = CParser_GetUniqueName();
                    }
                    CDecl_AddFunctionMember(
                        decle, tclass, &bde.declinfo2, access, TRUE, TRUE, TRUE, FALSE
                    );
                    if (tk == ';') {
                        tk = lex();
                    } else {
                        CError_Error(ERR_SEMICOLON_EXPECTED);
                    }
                    goto restart;
                }
            case TK_FRIEND:
                tk = lex();
                CDecl_ParseFriendDecl(tclass);
                goto restart;
        }

        CError_QualifierCheck(qual & Q_EXPLICIT);
        global_access = access;
        memclrw(&bde.declinfo, sizeof(DeclInfo));
        bde.declinfo.qual = qual;
        bde.declinfo.exportflags = exportflags;
        bde.declinfo.section = section;
        CParser_GetDeclSpecs(&bde.declinfo, FALSE);

        if (isCtorName &&
            tk == '(' &&
            (tclass->flags & CLASS_IS_TEMPL_ANY) &&
            bde.declinfo.thetype == TYPE(tclass) &&
            !bde.declinfo.nspace) {
            CPrep_UnLex();
            tk = TK_IDENTIFIER;
            tkidentifier = tclass->classname;
            qual = bde.declinfo.qual;
            goto redo_thing;
        }

after_various_things:
        switch (bde.declinfo.storageclass) {
            case 0:
            case TK_STATIC:
            case TK_TYPEDEF:
            case TK_MUTABLE:
                break;
            default:
                CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
                bde.declinfo.storageclass = 0;
        }

        if (tk != ';') {
            while (1) {
                CDecl_ScanStructDeclarator(&bde);
                if (resolveSelfRefs) {
                    bde.declinfo2.thetype = CTemplTool_ResolveMemberSelfRefs(
                        TEMPL_CLASS(tclass), bde.declinfo2.thetype, &bde.declinfo2.qual
                    );
                }
                if (bde.declinfo2.nspace) {
                    CError_Error(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
                }
                if (bde.declinfo2.operator_token) {
                    if (bde.declinfo.storageclass == TK_MUTABLE) {
                        CError_QualifierCheck(Q_MUTABLE);
                    }
                    // reused: TRUE for operator new and delete, which are static members
                    isCtorName = FALSE;
                    switch (bde.declinfo2.operator_token) {
                        case TK_NEW:
                        case TK_NEW_ARRAY:
                            CError_QualifierCheck(bde.declinfo2.qual & Q_VIRTUAL);
                            isCtorName = TRUE;
                            break;
                        case TK_DELETE:
                        case TK_DELETE_ARRAY:
                            CError_QualifierCheck(bde.declinfo2.qual & Q_VIRTUAL);
                            isCtorName = TRUE;
                            break;
                        default:
                            if (bde.declinfo2.storageclass == TK_STATIC) {
                                CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
                            }
                            if (tclass->sominfo) {
                                CError_Error(ERR_ILLEGAL_OPERATOR_DECLARATION);
                            }
                    }

                    bde.declinfo2.storageclass = 0;
                    if (IS_TYPE_FUNC(bde.declinfo2.thetype)) {
                        bde.declinfo2.qual |= declspecQual;
                        CDecl_AddFunctionMember(
                            decle,
                            tclass,
                            &bde.declinfo2,
                            access,
                            !isCtorName,
                            FALSE,
                            TRUE,
                            isCtorName
                        );
                        CDecl_CheckOperatorType(&bde.declinfo2, TRUE);
                        if (tclass->sominfo) {
                            CSOM_FixNewDeleteFunctype(TYPE_FUNC(bde.declinfo2.thetype));
                        }
                    } else {
                        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    }
                } else if (bde.valid) {
                    if (bde.declinfo2.name == constructor_name_node ||
                        bde.declinfo2.name == destructor_name_node) {
                        CError_Error(ERR_ILLEGAL_CONSTRUCTOR_DESTRUCTOR_DECLARATION);
                    }
                    switch (bde.declinfo2.storageclass) {
                        case TK_TYPEDEF:
                            CError_QualifierCheck(bde.declinfo2.qual & Q_VIRTUAL);
                            CDecl_TypedefDeclarator(&bde.declinfo2);
                            break;
                        case TK_STATIC:
                            CError_QualifierCheck(bde.declinfo2.qual & Q_VIRTUAL);
                            if (tclass->sominfo) {
                                CError_Error(ERR_NO_STATIC_MEMBERS_ALLOWED_SOM_CLASSES);
                            }
                            if (IS_TYPE_FUNC(bde.declinfo2.thetype)) {
                                bde.declinfo2.qual |= declspecQual;
                                bde.declinfo2.storageclass = 0;
                                if (bde.declinfo2.name == tclass->classname) {
                                    CError_Error(ERR_ILLEGAL_CONSTRUCTOR_DESTRUCTOR_DECLARATION);
                                }
                                CDecl_AddFunctionMember(
                                    decle, tclass, &bde.declinfo2, access, FALSE, FALSE, TRUE, TRUE
                                );
                            } else {
                                CDecl_ExtractClassExportFlags(&bde.declinfo2, tclass->eflags);
                                bde.declinfo2.storageclass = 0;
                                CDecl_DataDeclarator(&bde.declinfo2, access, TRUE);
                            }
                            break;
                        case 0:
                        case TK_MUTABLE:
                            if (IS_TYPE_FUNC(bde.declinfo2.thetype)) {
                                if (bde.declinfo2.name == tclass->classname) {
                                    CError_Error(ERR_ILLEGAL_CONSTRUCTOR_DESTRUCTOR_DECLARATION);
                                }
                                if (bde.declinfo.storageclass == TK_MUTABLE) {
                                    CError_QualifierCheck(Q_MUTABLE);
                                }
                                bde.declinfo2.qual |= declspecQual;
                                CDecl_AddFunctionMember(
                                    decle, tclass, &bde.declinfo2, access, TRUE, FALSE, TRUE, FALSE
                                );
                            } else {
                                CDecl_CompleteType(bde.declinfo2.thetype);
                                CanCreateObject(bde.declinfo2.thetype);
                                CError_QualifierCheck(bde.declinfo2.qual & (Q_VIRTUAL | Q_INLINE));
                                if (bde.declinfo2.storageclass == TK_MUTABLE) {
                                    bde.declinfo2.qual |= Q_MUTABLE;
                                }
                                CDecl_InstanceDataDeclarator(
                                    decle,
                                    tclass,
                                    bde.declinfo2.thetype,
                                    bde.declinfo2.qual,
                                    bde.declinfo2.name,
                                    access
                                );
                            }
                            break;
                        default:
                            CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
                    }
                }

                if (tk != ',') {
                    break;
                }
                tk = lex();
            }
        } else if (CParser_IsAnonymousUnion(&bde.declinfo, TRUE)) {
            if ((ivar = CDecl_InstanceDataDeclarator(
                     decle, tclass, bde.declinfo.thetype, 0, NULL, access
                 ))) {
                ivar->anonunion = TRUE;
            }
            for (scanivar = TYPE_CLASS(bde.declinfo.thetype)->ivars; scanivar;
                 scanivar = scanivar->next) {
                memberType = scanivar->type;
                if (IS_TYPE_BITFIELD(memberType) && copts.reverse_bitfields) {
                    TypeBitfield* newtype = galloc(sizeof(TypeBitfield));
                    *newtype = *TYPE_BITFIELD(memberType);
                    CABI_ReverseBitField(newtype);
                    memberType = TYPE(newtype);
                }
                if ((ivar = CDecl_InstanceDataDeclarator(
                         decle, tclass, memberType, scanivar->qual, scanivar->name, access
                     ))) {
                    ivar->offset = scanivar->offset | 0x80000000;
                }
            }
        }

        if (tk != ';') {
            CError_Error(ERR_SEMICOLON_EXPECTED);
            return;
        }
        CPrep_TokenStreamFlush();
        tk = lex();
    }
}

static VClassList* AddVBaseToList(TypeClass* tclass, TypeClass* baseclass) {
    VClassList* scan;
    VClassList* vbase;

    for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
        if (vbase->base == baseclass) {
            return NULL;
        }
    }

    vbase = galloc(sizeof(VClassList));
    memclrw(vbase, sizeof(VClassList));
    vbase->base = baseclass;

    if ((scan = tclass->vbases)) {
        while (scan->next) {
            scan = scan->next;
        }
        scan->next = vbase;
    } else {
        tclass->vbases = vbase;
    }

    return vbase;
}

void CDecl_MakeVBaseList(TypeClass* tclass) {
    ClassList* base;
    VClassList* vbase;
    VClassList* new_vbase;
    SInt32 offset;

    if (copts.vbase_ctor_offset) {
        tclass->flags |= CLASS_VBASE_CTOR_OFFSET;
    }

    for (base = tclass->bases, offset = tclass->size; base; base = base->next) {
        for (vbase = base->base->vbases; vbase; vbase = vbase->next) {
            if ((new_vbase = AddVBaseToList(tclass, vbase->base))) {
                new_vbase->offset = offset + CMach_MemberAlignValue(TYPE(vbase->base), offset);
                offset = new_vbase->offset + vbase->base->size;
            }
        }

        if (base->is_virtual && (new_vbase = AddVBaseToList(tclass, base->base))) {
            new_vbase->offset = offset + CMach_MemberAlignValue(TYPE(base->base), offset);
            offset = new_vbase->offset + base->base->size;
        }
    }
}

Boolean CDecl_CheckNewBase(TypeClass* tclass, TypeClass* baseclass, Boolean is_virtual) {
    ClassList* scan;

    if (tclass == baseclass) {
        CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
        return FALSE;
    }

    if (!(baseclass->flags & CLASS_COMPLETED)) {
        CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, baseclass, 0);
        return FALSE;
    }

    if (baseclass->flags & CLASS_SINGLE_OBJECT) {
        if (is_virtual || tclass->bases) {
            CError_Error(ERR_ILLEGAL_USE_HANDLEOBJECT);
            return FALSE;
        }
        tclass->flags |= CLASS_SINGLE_OBJECT;
    }

    if (baseclass->flags & CLASS_HANDLEOBJECT) {
        if (is_virtual || tclass->bases) {
            CError_Error(ERR_ILLEGAL_USE_HANDLEOBJECT);
            return FALSE;
        }
        tclass->flags |= CLASS_HANDLEOBJECT;
    }

    if (baseclass->sominfo) {
        if (!is_virtual) {
            CError_Error(ERR_SOM_CLASSES_INHERTIANCE_MUST_VIRTUAL);
        }
        CSOM_MakeSOMClass(tclass);
    } else if (tclass->sominfo) {
        CError_Error(ERR_SOM_CLASSES_ONLY_INHERIT_FROM_OTHER);
    }

    if (tclass->bases &&
        (tclass->flags & CLASS_SINGLE_OBJECT) &&
        (tclass->flags & CLASS_SINGLE_OBJECT)) {
        CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
        return FALSE;
    }

    if (copts.ecplusplus && (is_virtual || tclass->bases)) {
        CError_Error(ERR_ILLEGAL_USE_C_FEATURE_EC);
        return FALSE;
    }

    for (scan = tclass->bases; scan; scan = scan->next) {
        if (scan->base == baseclass) {
            CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
            return FALSE;
        }
    }

    if (baseclass->flags & CLASS_COM_OBJECT) {
        tclass->flags |= CLASS_COM_OBJECT;
    }
    if (baseclass->flags & CLASS_IS_CONVERTIBLE) {
        tclass->flags |= CLASS_IS_CONVERTIBLE;
    }
    if (baseclass->flags & CLASS_HAS_VBASES) {
        tclass->flags |= CLASS_HAS_VBASES;
    }

    if (is_virtual) {
        tclass->flags |= CLASS_HAS_VBASES;
    }

    return TRUE;
}

static void CDecl_ParseBaseClassList(TypeClass* tclass, short mode, Boolean is_templ) {
    Boolean is_virtual;
    AccessType access;
    NameResult pr;
    ClassList* base;
    ObjType* inherited_type;
    ClassList* scan;
    TypeClass* baseclass;

    do {
        if (mode == CLASS_MODE_CLASS) {
            access = ACCESSPRIVATE;
        } else {
            access = ACCESSPUBLIC;
        }

        is_virtual = FALSE;
        if ((tk = lex()) == TK_VIRTUAL) {
            tk = lex();
            is_virtual = TRUE;
        }

        switch (tk) {
            case TK_PRIVATE:
                access = ACCESSPRIVATE;
                tk = lex();
                break;
            case TK_PUBLIC:
                access = ACCESSPUBLIC;
                tk = lex();
                break;
            case TK_PROTECTED:
                if (!copts.ARMconform) {
                    access = ACCESSPROTECTED;
                    tk = lex();
                }
                break;
        }

        if (tk == TK_VIRTUAL) {
            if (is_virtual) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            is_virtual = TRUE;
            tk = lex();
        }

        if (CScope_ParseDeclName(&pr)) {
            if (!pr.type) {
                if (!pr.name) {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                } else if (tk == TK_IDENTIFIER && pr.name == tkidentifier) {
                    goto special_parsing;
                }
                CError_Error(ERR_UNDEFINED_IDENTIFIER, tkidentifier->name);
                continue;
            }

            CDecl_CompleteType(pr.type);
            if (is_templ && CTemplTool_IsTemplateArgumentDependentType(pr.type)) {
                if (!IS_TYPE_CLASS(pr.type) ||
                    !(TYPE_CLASS(pr.type)->flags & CLASS_IS_TEMPL) ||
                    CParser_CheckTemplateClassUsage(TEMPL_CLASS(pr.type), TRUE)) {
                    CTemplClass_RegisterBaseClass(TEMPL_CLASS(tclass), pr.type, access, is_virtual);
                    if (is_virtual) {
                        tclass->flags |= CLASS_HAS_VBASES;
                    }
                }
                continue;
            }

            if (!IS_TYPE_CLASS(pr.type) || (TYPE_CLASS(pr.type)->flags & CLASS_IS_TEMPL)) {
                CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
                continue;
            }
            baseclass = TYPE_CLASS(pr.type);
        } else {
special_parsing:
            // the pseudo base classes
            if (!strcmp(tkidentifier->name, "__somobject")) {
                if (!is_virtual) {
                    CError_Error(ERR_SOM_CLASSES_INHERTIANCE_MUST_VIRTUAL);
                }
                CSOM_MakeSOMClass(tclass);
                tk = lex();
                break;
            }
            if (!strcmp(tkidentifier->name, "__javaobject")) {
                tk = lex();
                tclass->action = CLASS_ACTION_JAVA;
                break;
            }

            CError_Error(ERR_UNDEFINED_IDENTIFIER, tkidentifier->name);
            continue;
        }

        if (CDecl_CheckNewBase(tclass, baseclass, is_virtual)) {
            base = galloc(sizeof(ClassList));
            memclrw(base, sizeof(ClassList));
            base->base = baseclass;
            base->access = access;
            base->is_virtual = is_virtual;
            if ((scan = tclass->bases)) {
                while (scan->next) {
                    scan = scan->next;
                }
                scan->next = base;
            } else {
                tclass->bases = base;
            }
        }
    } while ((tk = lex()) == ',');

    if (tclass->flags & CLASS_HAS_VBASES) {
        CDecl_MakeVBaseList(tclass);
    }

    if (copts.def_inherited && tclass->bases && !tclass->bases->next) {
        inherited_type = galloc(sizeof(ObjType));
        memclrw(inherited_type, sizeof(ObjType));
        inherited_type->otype = OT_TYPE;
        inherited_type->access = ACCESSPUBLIC;
        inherited_type->type = TYPE(tclass->bases->base);
        CScope_AddObject(
            tclass->nspace, GetHashNameNodeExport("inherited"), OBJ_BASE(inherited_type)
        );
    }
}

static AccessType getaccesstype(AccessType a, AccessType b, AccessType c) {
    if (a == ACCESSNONE || b == ACCESSNONE || b == ACCESSPRIVATE) {
        return ACCESSNONE;
    }

    if (c == ACCESSPUBLIC && b != ACCESSPUBLIC) {
        return ACCESSNONE;
    }

    return ACCESSPUBLIC;
}

static TypeMemberFunc* CDecl_MakeDefaultCtorType(TypeClass* tclass) {
    TypeMemberFunc* tmeth = galloc(sizeof(TypeMemberFunc));
    memclrw(tmeth, sizeof(TypeMemberFunc));
    tmeth->type = TYPEFUNC;
    tmeth->functype = TYPE(&void_ptr);
    tmeth->flags = FUNC_IS_CTOR | FUNC_AUTO_GENERATED | FUNC_METHOD;
    tmeth->theclass = tclass;
    CDecl_SetFuncFlags(TYPE_FUNC(tmeth), 1);

    if (tclass->flags & CLASS_HAS_VBASES) {
        CDecl_AddArgument(TYPE_FUNC(tmeth), TYPE(&stsignedshort));
    }

    CDecl_AddThisPointerArgument(TYPE_FUNC(tmeth), tclass);
    return tmeth;
}

// TODO: the original calls this empty function rather than inlining it, as if its body were a
// disabled one too long to inline; dont_inline stands in for that body
#pragma dont_inline on
static void CDecl_AddDefArgConstructor(TypeClass* tclass) {}
#pragma dont_inline reset

static void CDecl_AddMemberFunctionObject(
    TypeClass* tclass, HashNameNode* name, TypeMemberFunc* tmeth, AccessType access
) {
    Object* obj = CParser_NewCompilerDefFunctionObject();
    obj->name = name;
    obj->type = TYPE(tmeth);
    obj->qual = Q_MANGLE_NAME;
    obj->access = access;
    obj->nspace = tclass->nspace;
    obj->qual |= Q_INLINE;
    CScope_AddObject(tclass->nspace, obj->name, OBJ_BASE(obj));
}

static void CDecl_AddDefaultConstructor(ClassLayout* decle, TypeClass* tclass) {
    ClassList* base;
    ObjMemberVar* ivar;
    Boolean has_ctor;

    if (CClass_Constructor(tclass)) {
        CDecl_AddDefArgConstructor(tclass);
        return;
    }

    has_ctor = FALSE;

    if (tclass->flags & CLASS_HAS_VBASES) {
        has_ctor = TRUE;
    }
    if (decle->has_vtable) {
        has_ctor = TRUE;
    }

    for (base = tclass->bases; base; base = base->next) {
        if (CClass_Constructor(base->base)) {
            has_ctor = TRUE;
        }
    }

    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
        Type* type = ivar->type;
        while (IS_TYPE_ARRAY(type)) {
            type = TYPE_POINTER(type)->target;
        }
        if (IS_TYPE_CLASS(type) && CClass_Constructor(TYPE_CLASS(type))) {
            has_ctor = TRUE;
        }
    }

    if (has_ctor) {
        CDecl_AddMemberFunctionObject(
            tclass, constructor_name_node, CDecl_MakeDefaultCtorType(tclass), ACCESSPUBLIC
        );
    }
}

static TypeMemberFunc* CDecl_MakeCopyCtorType(TypeClass* tclass, Boolean is_const) {
    FuncArg* arg;
    TypeMemberFunc* tmeth = galloc(sizeof(TypeMemberFunc));
    memclrw(tmeth, sizeof(TypeMemberFunc));
    tmeth->type = TYPEFUNC;
    tmeth->functype = TYPE(&void_ptr);
    tmeth->flags = FUNC_IS_CTOR | FUNC_AUTO_GENERATED | FUNC_METHOD;
    tmeth->theclass = tclass;
    CDecl_SetFuncFlags(TYPE_FUNC(tmeth), 1);

    arg = CParser_NewFuncArg();
    if (is_const) {
        arg->qual = Q_CONST;
    }
    arg->type = CDecl_NewRefPointerType(TYPE(tclass));
    tmeth->args = arg;

    if (tclass->flags & CLASS_HAS_VBASES) {
        CDecl_AddArgument(TYPE_FUNC(tmeth), TYPE(&stsignedshort));
    }

    CDecl_AddThisPointerArgument(TYPE_FUNC(tmeth), tclass);
    return tmeth;
}

static void CDecl_AddDefaultCopyConstructor(ClassLayout* decle, TypeClass* tclass) {
    ClassList* base;
    ObjMemberVar* ivar;
    Object* obj;
    AccessType access;
    Boolean has_copyctor;
    Boolean is_const;
    FuncArg* arg;

    if (CClass_CopyConstructor(tclass)) {
        return;
    }

    access = ACCESSPUBLIC;
    is_const = TRUE;
    has_copyctor = FALSE;

    if (CClass_Constructor(tclass)) {
        has_copyctor = TRUE;
    }
    if ((tclass->flags & CLASS_HAS_VBASES) || decle->has_vtable) {
        has_copyctor = TRUE;
    }

    for (base = tclass->bases; base; base = base->next) {
        if ((obj = CClass_CopyConstructor(base->base))) {
            has_copyctor = TRUE;
            access = getaccesstype(access, obj->access, ACCESSPRIVATE);
            arg = TYPE_FUNC(obj->type)->args->next;
            if (base->base->flags & CLASS_HAS_VBASES) {
                arg = arg->next;
            }
            if (!(arg->qual & Q_CONST)) {
                is_const = FALSE;
            }
        }
    }

    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
        Type* type = ivar->type;
        while (IS_TYPE_ARRAY(type)) {
            type = TYPE_POINTER(type)->target;
        }
        if (IS_TYPE_CLASS(type) && (obj = CClass_CopyConstructor(TYPE_CLASS(type)))) {
            has_copyctor = TRUE;
            access = getaccesstype(access, obj->access, ACCESSPUBLIC);
            arg = TYPE_FUNC(obj->type)->args->next;
            if (TYPE_CLASS(type)->flags & CLASS_HAS_VBASES) {
                arg = arg->next;
            }
            if (!(arg->qual & Q_CONST)) {
                is_const = FALSE;
            }
        }
    }

    if (has_copyctor) {
        CDecl_AddMemberFunctionObject(
            tclass, constructor_name_node, CDecl_MakeCopyCtorType(tclass, is_const), access
        );
    }
}

static TypeMemberFunc* CDecl_MakeAssignmentOperatorType(TypeClass* tclass, Boolean is_const) {
    FuncArg* arg;
    TypeMemberFunc* tmeth = galloc(sizeof(TypeMemberFunc));
    memclrw(tmeth, sizeof(TypeMemberFunc));
    tmeth->type = TYPEFUNC;
    tmeth->functype = CDecl_NewRefPointerType(TYPE(tclass));
    tmeth->flags = FUNC_AUTO_GENERATED | FUNC_METHOD;
    tmeth->theclass = tclass;
    CDecl_SetFuncFlags(TYPE_FUNC(tmeth), 1);

    arg = CParser_NewFuncArg();
    if (is_const) {
        arg->qual = Q_CONST;
    }
    arg->type = CDecl_NewRefPointerType(TYPE(tclass));
    tmeth->args = arg;

    CDecl_AddThisPointerArgument(TYPE_FUNC(tmeth), tclass);
    return tmeth;
}

static void CDecl_AddDefaultAssignmentOperator(ClassLayout* decle, TypeClass* tclass) {
    ClassList* base;
    ObjMemberVar* ivar;
    Object* obj;
    AccessType access;
    Boolean is_const;
    Boolean has_ass;
    DeclInfo declinfo;

    is_const = TRUE;
    has_ass = FALSE;
    access = ACCESSPUBLIC;

    if (!CClass_AssignmentOperator(tclass)) {
        if (!copts.old_argmatch) {
            has_ass = TRUE;
        }

        if ((tclass->flags & CLASS_HAS_VBASES) ||
            decle->has_vtable ||
            CClass_MemberObject(tclass, asop_name_node)) {
            has_ass = TRUE;
        }

        for (base = tclass->bases; base; base = base->next) {
            if ((obj = CClass_AssignmentOperator(base->base))) {
                has_ass = TRUE;
                access = getaccesstype(access, obj->access, ACCESSPRIVATE);
                if (!(TYPE_FUNC(obj->type)->args->next->qual & Q_CONST)) {
                    is_const = FALSE;
                }
            }
        }

        for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
            Type* type = ivar->type;
            while (IS_TYPE_ARRAY(type)) {
                type = TYPE_POINTER(type)->target;
            }
            if (IS_TYPE_CLASS(type) && (obj = CClass_AssignmentOperator(TYPE_CLASS(type)))) {
                has_ass = TRUE;
                access = getaccesstype(access, obj->access, ACCESSPUBLIC);
                if (!(TYPE_FUNC(obj->type)->args->next->qual & Q_CONST)) {
                    is_const = FALSE;
                }
            }
        }
    }

    if (has_ass) {
        memclrw(&declinfo, sizeof(DeclInfo));
        declinfo.qual |= Q_INLINE;
        declinfo.thetype = (Type*)CDecl_MakeAssignmentOperatorType(tclass, is_const);
        declinfo.name = asop_name_node;
        CDecl_AddFunctionMember(decle, tclass, &declinfo, access, TRUE, FALSE, FALSE, FALSE);
    }
}

TypeMemberFunc* CDecl_MakeDefaultDtorType(TypeClass* tclass, Boolean is_virtual) {
    TypeMemberFunc* tmeth = galloc(sizeof(TypeMemberFunc));
    memclrw(tmeth, sizeof(TypeMemberFunc));
    tmeth->type = TYPEFUNC;
    tmeth->functype = (Type*)&void_ptr;
    tmeth->flags = FUNC_IS_DTOR | FUNC_AUTO_GENERATED | FUNC_METHOD;
    tmeth->theclass = tclass;
    CDecl_SetFuncFlags(TYPE_FUNC(tmeth), 1);
    if (is_virtual) {
        CDecl_AddArgument(TYPE_FUNC(tmeth), TYPE(&stsignedshort));
    }
    CDecl_AddThisPointerArgument(TYPE_FUNC(tmeth), tclass);
    return tmeth;
}

static void CDecl_AddDefaultDestructor(ClassLayout* decle, TypeClass* tclass) {
    ClassList* base;
    ObjMemberVar* ivar;
    Object* obj;
    AccessType access;
    Boolean has_dtor;
    Boolean is_virtual;
    DeclInfo declinfo;

    if (CClass_Destructor(tclass)) {
        return;
    }

    has_dtor = FALSE;
    is_virtual = FALSE;
    access = ACCESSPUBLIC;

    for (base = tclass->bases; base; base = base->next) {
        if ((obj = CClass_Destructor(base->base))) {
            has_dtor = TRUE;
            if (obj->datatype == DVFUNC) {
                is_virtual = TRUE;
            }
            access = getaccesstype(access, obj->access, ACCESSPRIVATE);
        }
    }

    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
        Type* type = ivar->type;
        while (IS_TYPE_ARRAY(type)) {
            type = TYPE_POINTER(type)->target;
        }
        if (IS_TYPE_CLASS(type) && (obj = CClass_Destructor(TYPE_CLASS(type)))) {
            has_dtor = TRUE;
            access = getaccesstype(access, obj->access, ACCESSPUBLIC);
        }
    }

    if (has_dtor) {
        memclrw(&declinfo, sizeof(DeclInfo));
        declinfo.qual |= Q_INLINE;
        if (is_virtual) {
            declinfo.qual |= Q_VIRTUAL;
        }

        declinfo.thetype = (Type*)CDecl_MakeDefaultDtorType(tclass, TRUE);
        declinfo.name = destructor_name_node;
        CDecl_AddFunctionMember(decle, tclass, &declinfo, access, TRUE, FALSE, FALSE, FALSE);
    }
}

static void CDecl_SetupClassLayout(ClassLayout* decle, TypeClass* tclass, ObjBase** objbuf) {
    SInt32 index;
    SInt32 i;
    Object* obj;
    CScopeObjectIterator iter;
    ObjMemberVar* ivar;
    SInt32 bufsize;

    if (decle->lex_order_count > 32) {
        bufsize = decle->lex_order_count * sizeof(ObjBase*);
        objbuf = lalloc(bufsize);
    } else {
        bufsize = 32 * sizeof(ObjBase*);
    }
    memclrw(objbuf, bufsize);
    decle->objlist = objbuf;

    CScope_InitObjectIterator(&iter, tclass->nspace);
    index = 0;
    while (1) {
        obj = OBJECT(CScope_NextObjectIteratorObject(&iter));
        if (!obj) {
            break;
        }

        if (!IS_TYPE_FUNC(obj->type)) {
            continue;
        }
        if (!IS_TYPEFUNC_METHOD(TYPE_FUNC(obj->type))) {
            continue;
        }
        if (obj->datatype == DALIAS) {
            continue;
        }

        if ((i = TYPE_METHOD(obj->type)->vtbl_index) > 0) {
            --i;
            CError_ASSERT(6432, i < decle->lex_order_count && !objbuf[i]);
            objbuf[i] = OBJ_BASE(obj);
            index++;
            if (obj->datatype != DVFUNC &&
                !TYPE_METHOD(obj->type)->is_static &&
                CClass_OverridesBaseMember(tclass, obj->name, obj)) {
                CDecl_MakeFunctionVirtual(tclass, obj);
            }

            if (obj->datatype == DVFUNC) {
                decle->has_vtable = TRUE;
                if (!tclass->vtable) {
                    CABI_AddVTable(tclass);
                    decle->firstVirtualSlot = i;
                } else {
                    if (i < decle->firstVirtualSlot) {
                        decle->firstVirtualSlot = i;
                    }
                }
            } else if (TYPE_FUNC(obj->type)->flags & FUNC_PURE) {
                CError_Error(ERR_PURE_FUNCTION_NOT_VIRTUAL, obj);
                TYPE_FUNC(obj->type)->flags &= ~FUNC_PURE;
            }
        } else {
            CError_ASSERT(6481, i == 0);
        }

        if (!tclass->sominfo) {
            TYPE_METHOD(obj->type)->vtbl_index = 0;
        }
    }

    if (!tclass->action) {
        for (i = 0; i < decle->lex_order_count; i++) {
            Object* method = OBJECT(objbuf[i]); // NOTE: invented name
            if (method &&
                method->datatype == DVFUNC &&
                !(method->qual & Q_INLINE) &&
                !(TYPE_FUNC(method->type)->flags & FUNC_PURE)) {
                tclass->action = CLASS_ACTION_KEY_FUNCTION;
                TYPE_FUNC(method->type)->flags |= FUNC_KEY_FUNCTION;
                break;
            }
        }
    }

    if (!tclass->sominfo) {
        for (i = 0, ivar = tclass->ivars; i < decle->lex_order_count; i++) {
            if (!objbuf[i]) {
                CError_ASSERT(6518, ivar);
                objbuf[i] = OBJ_BASE(ivar);
                ivar = ivar->next;
                index++;
            }
        }
        CError_ASSERT(6524, ivar == NULL);
    }

    CError_ASSERT(6527, index == decle->lex_order_count);
}

void CDecl_CompleteClass(ClassLayout* decle, TypeClass* tclass) {
    ClassList* base;
    // TODO: assigned in the loop's test and never read (fakematch: a block-local copy or testing
    // base->base->vtable gives other registers)
    TypeClass* baseclass;
    ObjBase* buf[32];

    for (base = tclass->bases; base; base = base->next) {
        if ((baseclass = base->base)->vtable) {
            decle->has_vtable = TRUE;
        }
    }

    if (!tclass->sominfo) {
        CDecl_AddDefaultDestructor(decle, tclass);
        CDecl_AddDefaultAssignmentOperator(decle, tclass);
        CDecl_AddDefaultConstructor(decle, tclass);
        CDecl_AddDefaultCopyConstructor(decle, tclass);
    }

    CDecl_SetupClassLayout(decle, tclass, buf);
    if (decle->has_vtable) {
        CClass_CheckOverrides(tclass);
    }
    CABI_LayoutClass(decle, tclass);
    if (tclass->sominfo) {
        CSOM_ClassComplete(tclass);
    }

    if ((tclass->flags & CLASS_IS_TEMPL_INST) && !TEMPL_CLASS_INST(tclass)->is_specialized) {
        tclass->action = CLASS_ACTION_NONE;
    }

    if (!tclass->action) {
        CClass_MakeStaticActionClass(tclass);
    }
    CClass_ClassDefaultFuncAction(tclass);
}

// NOTE: invented names: nspace, name, tclass, mode, asTypeName, addName
TypeClass* CDecl_DefineClass(
    NameSpace* nspace,
    HashNameNode* name,
    TypeClass* tclass,
    short mode,
    Boolean asTypeName,
    Boolean addName
) {
    NameSpace* mynspace;
    ObjType* objtype;

    if (!tclass && nspace->theclass && (nspace->theclass->flags & CLASS_IS_TEMPL)) {
        CError_ASSERT(6625, !asTypeName);
        return TYPE_CLASS(CTemplClass_DefineNestedClass(TEMPL_CLASS(nspace->theclass), name, mode));
    }

    mynspace = CScope_NewListNameSpace(name, TRUE);
    if (!tclass) {
        tclass = galloc(sizeof(TypeClass));
        memclrw(tclass, sizeof(TypeClass));
    }

    tclass->type = TYPECLASS;
    tclass->align = 1;
    tclass->mode = mode;
    tclass->action = CLASS_ACTION_NONE;
    if (name) {
        tclass->classname = name;
        if (addName) {
            if (asTypeName) {
                objtype = galloc(sizeof(ObjType));
                memclrw(objtype, sizeof(ObjType));
                objtype->otype = OT_TYPE;
                objtype->access = ACCESSPUBLIC;
                objtype->type = (Type*)tclass;
                CScope_AddObject(nspace, name, OBJ_BASE(objtype));
            } else {
                CScope_DefineTypeTag(nspace, name, (Type*)tclass);
            }
        }
        CScope_DefineTypeTag(mynspace, name, (Type*)tclass);

        if (cscope_currentfunc) {
            mynspace->name = CParser_AppendUniqueNameFile(name->name);
        }

        if (copts.direct_to_som && nspace == cscope_root && !strcmp(name->name, "SOMObject")) {
            CSOM_MakeSOMClass(tclass);
        }
    } else {
        tclass->classname = CParser_AppendUniqueNameFile("@class");
        mynspace->name = tclass->classname;
    }

    tclass->nspace = mynspace;
    mynspace->theclass = tclass;
    mynspace->parent = nspace;
    if (!nspace->is_global) {
        CParser_RegisterNonGlobalClass(tclass);
    }

    return tclass;
}

void CDecl_ParseClassDeclSpec(UInt8* declspec) {
    DeclInfo declinfo;

    if ((tk = lex()) == '(') {
        memclrw(&declinfo, sizeof(DeclInfo));
        CParser_ParseDeclSpec(&declinfo, TRUE);
        if (declinfo.exportflags & EXPORT_FLAGS_INTERNAL) {
            *declspec |= CLASS_EFLAGS_INTERNAL;
        }
        if (declinfo.exportflags & EXPORT_FLAGS_IMPORT) {
            *declspec |= CLASS_EFLAGS_IMPORT;
        }
        if (declinfo.exportflags & EXPORT_FLAGS_EXPORT) {
            *declspec |= CLASS_EFLAGS_EXPORT;
        }
        if ((tk = lex()) != ')') {
            CError_Error(ERR_RPAREN_EXPECTED);
        } else {
            tk = lex();
        }
    } else {
        CError_Error(ERR_LPAREN_EXPECTED);
    }
}

static TemplClass* CDecl_SpecializeTemplateClass(TemplClass* tmclass) {
    tmclass->theclass.nspace->names = 0;
    tmclass->theclass.nspace->data.list = NULL;
    tmclass->theclass.ivars = NULL;
    tmclass->theclass.bases = NULL;
    tmclass->theclass.vbases = NULL;
    tmclass->theclass.friends = NULL;
    tmclass->theclass.vtable = NULL;
    tmclass->members = NULL;
    tmclass->instances = NULL;
    tmclass->pspec_owner = NULL;
    tmclass->pspecs = NULL;
    tmclass->actions = NULL;
    tmclass->lex_order_count = 0;
    tmclass->align = 0;
    tmclass->flags = TEMPLCLASS_IS_SPECIALIZATION;
    return tmclass;
}

// NOTE: invented names: declinfo, mode, lexAfterBody, class_declspec
void CDecl_ParseClass(DeclInfo* declinfo, short mode, Boolean lexAfterBody, UInt8 class_declspec) {
    TypeClass* tclass;
    HashNameNode* classname;
    Type* search;
    short t;
    NameSpace* nspace;
    NameResult pr;
    ClassLayout decle;
    CScopeSave scopesave;
    GList gl;
    FileOffsetInfo offsetsave;
    SInt32 offset;
    Boolean is_templ;
    Boolean add_to_browse;
#ifndef __MWERKS__
    // A qualified name (`struct ::E` of an enum or a union) reaches tagtype_search with `classname`
    // unset, and the error messages there print it. The original's is a register its callers left
    // a heap pointer in (CParser_GetDeclSpecs's): at file scope the name it gives is empty, as here.
    static HashNameNode unset_classname;

    classname = &unset_classname;
#endif

    memclrw(&decle, sizeof(ClassLayout));
    if (declinfo->pendingClass) {
        tclass = TYPE_CLASS(declinfo->pendingClass);
    } else {
        if (tk == TK_UU_DECLSPEC) {
            CDecl_ParseClassDeclSpec(&class_declspec);
        }

        switch (tk) {
            case ':':
            case '{':
                tclass = CDecl_DefineClass(cscope_current, NULL, NULL, mode, FALSE, TRUE);
                tclass->eflags |= class_declspec;
                break;
            case TK_IDENTIFIER:
                classname = tkidentifier;
                if (!declinfo->in_friend_decl &&
                    !declinfo->isNewTypeId &&
                    ((t = lookahead()) == ':' || t == ';' || t == '{')) {
                    tk = lex();
                    search = CScope_GetLocalTagType(cscope_current, classname);
                    if (search) {
tagtype_search:
                        if (!IS_TYPE_CLASS(search)) {
                            if ((IS_TYPE_TEMPLATE(search) || IS_TYPE_STRUCT(search)) &&
                                tk != '{' &&
                                tk != ':') {
                                declinfo->thetype = search;
                                return;
                            }
                            if (!IS_TYPE_TEMPLATE(search) ||
                                !(tclass = TYPE_CLASS(CTemplTool_GetSelfRefTemplate(search)))) {
                                CError_Error(
                                    ERR_STRUCT_UNION_ENUM_CLASS_TAG_REDEFINED, classname->name
                                );
                                tclass = CDecl_DefineClass(
                                    cscope_current, NULL, NULL, mode, FALSE, TRUE
                                );
                                break;
                            }
                        } else {
                            tclass = TYPE_CLASS(search);
                        }
                        if (tclass->mode != mode) {
                            if ((mode == CLASS_MODE_CLASS && tclass->mode == CLASS_MODE_STRUCT) ||
                                (mode == CLASS_MODE_STRUCT && tclass->mode == CLASS_MODE_CLASS)) {
                                if (copts.warn_structclass) {
                                    CError_Warning(ERR_INCONSISTENT_USE_CLASS_STRUCT_KEYWORDS);
                                }
                            } else {
                                CError_Error(ERR_STRUCT_UNION_ENUM_CLASS_TAG_REDEFINED, classname);
                            }
                        }
                        tclass->eflags |= class_declspec;
                        break;
                    } else {
                        tclass =
                            CDecl_DefineClass(cscope_current, classname, NULL, mode, FALSE, TRUE);
                        tclass->eflags |= class_declspec;
                        break;
                    }
                } else {
                    tkidentifier = classname;
                }
            default:
                if (!CScope_ParseElaborateName(&pr)) {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    declinfo->thetype = TYPE(&stsignedint);
                    return;
                }

                tk = lex();
                if ((search = pr.type)) {
                    goto tagtype_search;
                }

                CError_ASSERT(6869, pr.name);

                tclass = CDecl_DefineClass(
                    CScope_FindNonClassNonFunctionNS(cscope_current),
                    pr.name,
                    NULL,
                    mode,
                    FALSE,
                    TRUE
                );
                tclass->eflags |= class_declspec;
        }
    }

    declinfo->thetype = TYPE(tclass);
    if (tk != ':' && tk != '{') {
        return;
    }

    if (declinfo->in_friend_decl) {
        CError_Error(ERR_ILLEGAL_FRIEND_DECLARATION);
    }

    if (tclass->flags & CLASS_COMPLETED) {
        if ((tclass->flags & CLASS_IS_TEMPL) &&
            !TEMPL_CLASS(tclass)->instances &&
            !(TEMPL_CLASS(tclass)->flags & TEMPLCLASS_IS_SPECIALIZATION)) {
            tclass = TYPE_CLASS(CDecl_SpecializeTemplateClass(TEMPL_CLASS(tclass)));
        } else {
            CError_Error(ERR_STRUCT_UNION_ENUM_CLASS_TAG_REDEFINED, tclass->classname->name);
            tclass = CDecl_DefineClass(cscope_current, NULL, NULL, mode, FALSE, TRUE);
        }
    }

    for (nspace = cscope_current; nspace; nspace = nspace->parent) {
        if (nspace == tclass->nspace) {
            CError_Error(ERR_STRUCT_UNION_ENUM_CLASS_TAG_REDEFINED, tclass->classname->name);
            tclass = CDecl_DefineClass(cscope_current, NULL, NULL, mode, FALSE, TRUE);
            break;
        }
    }

    is_templ = (tclass->flags & CLASS_IS_TEMPL) != 0;
    if (tclass->flags & CLASS_IS_TEMPL_INST) {
        TEMPL_CLASS_INST(tclass)->is_instantiated = TRUE;
        if (!declinfo->pendingClass) {
            TEMPL_CLASS_INST(tclass)->is_specialized = TRUE;
        }
    }

    CError_ASSERT(6939, copts.structalignment >= 0 && copts.structalignment <= 14);

    tclass->eflags |= ((copts.structalignment + 1) << 4) & CLASS_EFLAGS_STRUCT_ALIGN;
    if (tk == ':') {
        CDecl_ParseBaseClassList(tclass, mode, is_templ);
    }

    CScope_SetClassDefScope(tclass, &scopesave);
    if (tk == '{') {
        tk = lex();
        if ((add_to_browse =
                 cparamblkptr->browseoptions.recordClasses && declinfo->file->recordbrowseinfo)) {
            offsetsave = member_fileoffset;
            CBrowse_BeginClass(declinfo, &gl);
        }
        CDecl_ParseClassMembers(&decle, tclass, mode);
        offset = CPrep_BrowserFileOffset();
        if (lexAfterBody) {
            tk = lex();
        }
        if (add_to_browse) {
            member_fileoffset = offsetsave;
            if (lexAfterBody && tk == ';') {
                CPrep_BrowserFileOffset();
            }
            CBrowse_EndClass(offset, &gl);
        }
    } else {
        CError_Error(ERR_LBRACE_EXPECTED);
    }

    if (is_templ) {
        CTemplClass_CompleteClass(TEMPL_CLASS(tclass), &decle);
    } else {
        CDecl_CompleteClass(&decle, tclass);
    }
    CScope_RestoreScope(&scopesave);
}
