#include "compiler/CMangler.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CScope.h"
#include "compiler/CTemplateTools.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/templates.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "driver/COSToolsCLT.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

HashNameNode* constructor_name_node;
HashNameNode* destructor_name_node;
HashNameNode* asop_name_node;

static void CMangler_MangleClassName(TypeClass* tclass);
static void CMangler_MangleTypeAppend(Type* type, UInt32 qual);
static void CMangler_MangleArgs(FuncArg* args);

void CMangler_Setup(void) {
    constructor_name_node = GetHashNameNodeExport("__ct");
    destructor_name_node = GetHashNameNodeExport("__dt");
    asop_name_node = GetHashNameNodeExport("__as");
}

// The destructor variants' names. None is called: Basic and Array have no caller, and the others'
// only caller, CABI_AddLayeredDestructors, is itself never called.
HashNameNode* CMangler_BasicDtorName(void) {
    return GetHashNameNodeExport("__dtb");
}

HashNameNode* CMangler_VBaseDtorName(void) {
    return GetHashNameNodeExport("__dtv");
}

HashNameNode* CMangler_ArrayDtorName(void) {
    return GetHashNameNodeExport("__dta");
}

HashNameNode* CMangler_SDeleteDtorName(void) {
    return GetHashNameNodeExport("__dts");
}

HashNameNode* CMangler_DeleteDtorName(void) {
    return GetHashNameNodeExport("__dt");
}

char* CMangler_GetOperator(HashNameNode* name) {
    char* str;

    if (name == asop_name_node) {
        return "operator=";
    }

    str = name->name;
    if (!strcmp(str, "__nw")) {
        return "operator new";
    }
    if (!strcmp(str, "__dl")) {
        return "operator delete";
    }
    if (!strcmp(str, "__nwa")) {
        return "operator new[]";
    }
    if (!strcmp(str, "__dla")) {
        return "operator delete[]";
    }
    if (!strcmp(str, "__pl")) {
        return "operator+";
    }
    if (!strcmp(str, "__mi")) {
        return "operator-";
    }
    if (!strcmp(str, "__ml")) {
        return "operator*";
    }
    if (!strcmp(str, "__dv")) {
        return "operator/";
    }
    if (!strcmp(str, "__md")) {
        return "operator%";
    }
    if (!strcmp(str, "__er")) {
        return "operator^";
    }
    if (!strcmp(str, "__ad")) {
        return "operator&";
    }
    if (!strcmp(str, "__or")) {
        return "operator|";
    }
    if (!strcmp(str, "__co")) {
        return "operator~";
    }
    if (!strcmp(str, "__nt")) {
        return "operator!";
    }
    if (!strcmp(str, "__lt")) {
        return "operator<";
    }
    if (!strcmp(str, "__gt")) {
        return "operator>";
    }
    if (!strcmp(str, "__apl")) {
        return "operator+=";
    }
    if (!strcmp(str, "__ami")) {
        return "operator-=";
    }
    if (!strcmp(str, "__amu")) {
        return "operator*=";
    }
    if (!strcmp(str, "__adv")) {
        return "operator/=";
    }
    if (!strcmp(str, "__amd")) {
        return "operator%=";
    }
    if (!strcmp(str, "__aer")) {
        return "operator^=";
    }
    if (!strcmp(str, "__aad")) {
        return "operator&=";
    }
    if (!strcmp(str, "__aor")) {
        return "operator|=";
    }
    if (!strcmp(str, "__ls")) {
        return "operator<<";
    }
    if (!strcmp(str, "__rs")) {
        return "operator>>";
    }
    if (!strcmp(str, "__als")) {
        return "operator<<=";
    }
    if (!strcmp(str, "__ars")) {
        return "operator>>=";
    }
    if (!strcmp(str, "__eq")) {
        return "operator==";
    }
    if (!strcmp(str, "__ne")) {
        return "operator!=";
    }
    if (!strcmp(str, "__le")) {
        return "operator<=";
    }
    if (!strcmp(str, "__ge")) {
        return "operator>=";
    }
    if (!strcmp(str, "__aa")) {
        return "operator&&";
    }
    if (!strcmp(str, "__oo")) {
        return "operator||";
    }
    if (!strcmp(str, "__pp")) {
        return "operator++";
    }
    if (!strcmp(str, "__mm")) {
        return "operator--";
    }
    if (!strcmp(str, "__cm")) {
        return "operator,";
    }
    if (!strcmp(str, "__rm")) {
        return "operator->*";
    }
    if (!strcmp(str, "__rf")) {
        return "operator*";
    }
    if (!strcmp(str, "__cl")) {
        return "operator()";
    }
    if (!strcmp(str, "__vc")) {
        return "operator[]";
    }
    return NULL;
}

HashNameNode* CMangler_OperatorName(short token) {
    switch (token) {
        case TK_NEW:
            return GetHashNameNodeExport("__nw");
        case TK_DELETE:
            return GetHashNameNodeExport("__dl");
        case TK_NEW_ARRAY:
            return GetHashNameNodeExport("__nwa");
        case TK_DELETE_ARRAY:
            return GetHashNameNodeExport("__dla");
        case '+':
            return GetHashNameNodeExport("__pl");
        case '-':
            return GetHashNameNodeExport("__mi");
        case '*':
            return GetHashNameNodeExport("__ml");
        case '/':
            return GetHashNameNodeExport("__dv");
        case '%':
            return GetHashNameNodeExport("__md");
        case '^':
            return GetHashNameNodeExport("__er");
        case '&':
            return GetHashNameNodeExport("__ad");
        case '|':
            return GetHashNameNodeExport("__or");
        case '~':
            return GetHashNameNodeExport("__co");
        case '!':
            return GetHashNameNodeExport("__nt");
        case '=':
            return asop_name_node;
        case '<':
            return GetHashNameNodeExport("__lt");
        case '>':
            return GetHashNameNodeExport("__gt");
        case TK_ADD_ASSIGN:
            return GetHashNameNodeExport("__apl");
        case TK_SUB_ASSIGN:
            return GetHashNameNodeExport("__ami");
        case TK_MULT_ASSIGN:
            return GetHashNameNodeExport("__amu");
        case TK_DIV_ASSIGN:
            return GetHashNameNodeExport("__adv");
        case TK_MOD_ASSIGN:
            return GetHashNameNodeExport("__amd");
        case TK_XOR_ASSIGN:
            return GetHashNameNodeExport("__aer");
        case TK_AND_ASSIGN:
            return GetHashNameNodeExport("__aad");
        case TK_OR_ASSIGN:
            return GetHashNameNodeExport("__aor");
        case TK_SHL:
            return GetHashNameNodeExport("__ls");
        case TK_SHR:
            return GetHashNameNodeExport("__rs");
        case TK_SHL_ASSIGN:
            return GetHashNameNodeExport("__als");
        case TK_SHR_ASSIGN:
            return GetHashNameNodeExport("__ars");
        case TK_LOGICAL_EQ:
            return GetHashNameNodeExport("__eq");
        case TK_LOGICAL_NE:
            return GetHashNameNodeExport("__ne");
        case TK_LESS_EQUAL:
            return GetHashNameNodeExport("__le");
        case TK_GREATER_EQUAL:
            return GetHashNameNodeExport("__ge");
        case TK_LOGICAL_AND:
            return GetHashNameNodeExport("__aa");
        case TK_LOGICAL_OR:
            return GetHashNameNodeExport("__oo");
        case TK_INCREMENT:
            return GetHashNameNodeExport("__pp");
        case TK_DECREMENT:
            return GetHashNameNodeExport("__mm");
        case ',':
            return GetHashNameNodeExport("__cm");
        case TK_ARROW_STAR:
            return GetHashNameNodeExport("__rm");
        case TK_ARROW:
            return GetHashNameNodeExport("__rf");
        case '(':
            return GetHashNameNodeExport("__cl");
        case '[':
            return GetHashNameNodeExport("__vc");
        default:
            return NULL;
    }
}

HashNameNode* CMangler_VTableName(TypeClass* theclass) {
    HashNameNode* name;

    name_mangle_list.size = 0;
    AppendGListName(&name_mangle_list, "__vt__");
    CMangler_MangleClassName(theclass);
    AppendGListByte(&name_mangle_list, 0);
    COS_LockHandle(name_mangle_list.data);
    name = GetHashNameNodeExport(*name_mangle_list.data);
    COS_UnlockHandle(name_mangle_list.data);
    return name;
}

HashNameNode* CMangler_RTTIObjectName(Type* type, UInt32 qual) {
    HashNameNode* name;

    name_mangle_list.size = 0;
    AppendGListName(&name_mangle_list, "__RTTI__");
    CMangler_MangleTypeAppend(type, qual);
    AppendGListByte(&name_mangle_list, 0);
    COS_LockHandle(name_mangle_list.data);
    name = GetHashNameNodeExport(*name_mangle_list.data);
    COS_UnlockHandle(name_mangle_list.data);
    return name;
}

HashNameNode* CMangler_ThunkName(
    Object* vfunc, SInt32 this_delta, SInt32 return_delta, SInt32 ctoroffset
) {
    HashNameNode* linkname;
    HashNameNode* name;
    char buf[64];

    linkname = CMangler_GetLinkName(vfunc);
    name_mangle_list.size = 0;
    if (return_delta == 0) {
        if (ctoroffset < 0) {
            sprintf(buf, "@%ld@", -this_delta);
        } else {
            sprintf(buf, "@%ld@%ld@", -this_delta, ctoroffset);
        }
    } else {
        sprintf(buf, "@%ld@%ld@%ld@", -this_delta, ctoroffset, return_delta);
    }
    AppendGListName(&name_mangle_list, buf);
    AppendGListID(&name_mangle_list, linkname->name);
    COS_LockHandle(name_mangle_list.data);
    name = GetHashNameNodeExport(*name_mangle_list.data);
    COS_UnlockHandle(name_mangle_list.data);
    return name;
}

static void CMangler_CheckTemplateArguments(TemplArg* arg) {
    ENode* expr;

    while (arg) {
        if (arg->pid.type == TPT_NONTYPE) {
            expr = arg->data.paramdecl.expr;
            CError_ASSERT(360, expr);
            if (expr->rtype->type != TYPETEMPLDEPEXPR) {
                switch (expr->type) {
                    case EINTCONST:
                        break;
                    case EOBJREF:
                        CMangler_GetLinkName(expr->data.objref);
                        break;
                    default:
                        CError_FATAL(383);
                }
            }
        }
        arg = arg->next;
    }
}

static void CMangler_AppendTemplateArgumentList(TemplArg* arg) {
    ENode* expr;
    char buf[32];

    AppendGListByte(&name_mangle_list, '<');

    while (arg) {
        if (arg->pid.type == TPT_NONTYPE) {
            expr = arg->data.paramdecl.expr;
            CError_ASSERT(409, expr);
            if (expr->rtype->type != TYPETEMPLDEPEXPR) {
                switch (expr->type) {
                    case EINTCONST:
                        CInt64_PrintDec(buf, expr->data.intval);
                        AppendGListName(&name_mangle_list, buf);
                        break;
                    case EOBJREF:
                        AppendGListByte(&name_mangle_list, '&');
                        AppendGListName(
                            &name_mangle_list, CMangler_GetLinkName(expr->data.objref)->name
                        );
                        break;
                    default:
                        CError_FATAL(452);
                }
            } else {
                AppendGListByte(&name_mangle_list, 'T');
            }
        } else if (arg->pid.type == TPT_TYPE) {
            CMangler_MangleTypeAppend(arg->data.typeparam.type, arg->data.typeparam.qual);
        } else {
            CError_ASSERT(467, arg->pid.type == TPT_TEMPLATE);
            CMangler_MangleTypeAppend(arg->data.ttargtype, 0);
        }

        if (arg->next) {
            AppendGListByte(&name_mangle_list, ',');
        }
        arg = arg->next;
    }

    AppendGListByte(&name_mangle_list, '>');
}

HashNameNode* CMangler_TemplateInstanceName(HashNameNode* basename, TemplArg* args) {
    HashNameNode* name;

    // TODO: workaround: the original tests the inlined CMangler_CheckTemplateArguments' own copy of
    // `args`, where the plain call tests `args` itself; only a cast through void* found to keep the
    // copy
    CMangler_CheckTemplateArguments((TemplArg*)(void*)args);
    name_mangle_list.size = 0;
    AppendGListName(&name_mangle_list, basename->name);
    CMangler_AppendTemplateArgumentList(args);
    AppendGListByte(&name_mangle_list, 0);

    COS_LockHandle(name_mangle_list.data);
    name = GetHashNameNodeExport(*name_mangle_list.data);
    COS_UnlockHandle(name_mangle_list.data);
    return name;
}

static void CMangler_MangleTypeName(char* str) {
    char buf[16];

    sprintf(buf, "%d", strlen(str));
    AppendGListName(&name_mangle_list, buf);
    AppendGListName(&name_mangle_list, str);
}

static void CMangler_MangleNameSpaceName(NameSpace* nspace, char* str) {
    char* stack[10];
    int stackp;
    NameSpace* scan;

    stack[0] = str;
    scan = nspace;
    stackp = 1;
    while (scan) {
        if (scan->name) {
            stack[stackp++] = scan->name->name;
            if (stackp >= 9) {
                break;
            }
        }
        scan = scan->parent;
    }

    if (stackp > 1) {
        AppendGListByte(&name_mangle_list, 'Q');
        AppendGListByte(&name_mangle_list, '0' + stackp);
    }

    while (--stackp >= 0) {
        CMangler_MangleTypeName(stack[stackp]);
    }
}

static void CMangler_MangleClassName(TypeClass* tclass) {
    if (!tclass->classname) {
        CMangler_MangleNameSpaceName(tclass->nspace->parent, "class");
    } else {
        CMangler_MangleNameSpaceName(tclass->nspace->parent, tclass->nspace->name->name);
    }
}

static void CMangler_MangleQualifier(UInt32 qual) {
    if (qual & Q_CONST) {
        AppendGListByte(&name_mangle_list, 'C');
    }
    if (qual & Q_VOLATILE) {
        AppendGListByte(&name_mangle_list, 'V');
    }
}

static void CMangler_MangleTypeAppend(Type* type, UInt32 qual) {
    char buf[16];

    switch (type->type) {
        case TYPEVOID:
            CMangler_MangleQualifier(qual);
            AppendGListByte(&name_mangle_list, 'v');
            return;
        case TYPEINT:
        case TYPEFLOAT:
            CMangler_MangleQualifier(qual);
            switch (TYPE_INTEGRAL(type)->integral) {
                case IT_BOOL:
                    AppendGListByte(&name_mangle_list, 'b');
                    return;
                case IT_CHAR:
                    AppendGListByte(&name_mangle_list, 'c');
                    return;
                case IT_WCHAR_T:
                    AppendGListByte(&name_mangle_list, 'w');
                    return;
                case IT_UCHAR:
                    AppendGListName(&name_mangle_list, "Uc");
                    return;
                case IT_SCHAR:
                    AppendGListName(&name_mangle_list, "Sc");
                    return;
                case IT_SHORT:
                    AppendGListByte(&name_mangle_list, 's');
                    return;
                case IT_USHORT:
                    AppendGListName(&name_mangle_list, "Us");
                    return;
                case IT_INT:
                    AppendGListByte(&name_mangle_list, 'i');
                    return;
                case IT_UINT:
                    AppendGListName(&name_mangle_list, "Ui");
                    return;
                case IT_LONG:
                    AppendGListByte(&name_mangle_list, 'l');
                    return;
                case IT_ULONG:
                    AppendGListName(&name_mangle_list, "Ul");
                    return;
                case IT_LONGLONG:
                    AppendGListByte(&name_mangle_list, 'x');
                    return;
                case IT_ULONGLONG:
                    AppendGListName(&name_mangle_list, "Ux");
                    return;
                case IT_FLOAT:
                    AppendGListByte(&name_mangle_list, 'f');
                    return;
                case IT_SHORTDOUBLE:
                    AppendGListByte(&name_mangle_list, 'D');
                    return;
                case IT_DOUBLE:
                    AppendGListByte(&name_mangle_list, 'd');
                    return;
                case IT_LONGDOUBLE:
                    AppendGListByte(&name_mangle_list, 'r');
                    return;
                // the integral types after IT_LONGDOUBLE (IT_VEC2X32FLOAT is Gekko's paired single)
                // mangle as digits
                case IT_17:
                    AppendGListByte(&name_mangle_list, '0');
                    return;
                case IT_18:
                    AppendGListByte(&name_mangle_list, '1');
                    return;
                case IT_VEC2X32FLOAT:
                    AppendGListByte(&name_mangle_list, '2');
                    return;
                case IT_20:
                    AppendGListByte(&name_mangle_list, '3');
                    return;
                case IT_21:
                    AppendGListByte(&name_mangle_list, '4');
                    return;
                case IT_22:
                    AppendGListByte(&name_mangle_list, '5');
                    return;
                case IT_23:
                    AppendGListByte(&name_mangle_list, '6');
                    return;
                case IT_24:
                    AppendGListByte(&name_mangle_list, '7');
                    return;
                default:
                    CError_FATAL(619);
            }
        case TYPEENUM:
            CMangler_MangleQualifier(qual);
            if (!TYPE_ENUM(type)->enumname) {
                CMangler_MangleNameSpaceName(TYPE_ENUM(type)->nspace, "enum");
            } else {
                CMangler_MangleNameSpaceName(
                    TYPE_ENUM(type)->nspace, TYPE_ENUM(type)->enumname->name
                );
            }
            return;
        case TYPEPOINTER:
            CMangler_MangleQualifier(TYPE_POINTER(type)->qual);
            if (TYPE_POINTER(type)->qual & Q_REFERENCE) {
                AppendGListByte(&name_mangle_list, 'R');
            } else {
                AppendGListByte(&name_mangle_list, 'P');
            }
            CMangler_MangleTypeAppend(TYPE_POINTER(type)->target, qual);
            return;
        case TYPEMEMBERPOINTER:
            if (TYPE_MEMBER_POINTER(type)->ty2->type != TYPECLASS) {
                AppendGListName(&name_mangle_list, "3<T>");
                return;
            }
            CMangler_MangleQualifier(TYPE_MEMBER_POINTER(type)->qual);
            AppendGListByte(&name_mangle_list, 'M');
            CMangler_MangleClassName(TYPE_CLASS(TYPE_MEMBER_POINTER(type)->ty2));
            CMangler_MangleTypeAppend(TYPE_MEMBER_POINTER(type)->ty1, qual);
            return;
        case TYPEARRAY:
            AppendGListByte(&name_mangle_list, 'A');
            if (TYPE_POINTER(type)->target->size) {
                sprintf(buf, "%ld", type->size / TYPE_POINTER(type)->target->size);
                AppendGListName(&name_mangle_list, buf);
            } else {
                AppendGListByte(&name_mangle_list, '0');
            }
            AppendGListByte(&name_mangle_list, '_');
            CMangler_MangleTypeAppend(TYPE_POINTER(type)->target, qual);
            return;
        case TYPEFUNC:
            CMangler_MangleQualifier(qual);
            AppendGListByte(&name_mangle_list, 'F');
            CMangler_MangleArgs(TYPE_FUNC(type)->args);
            AppendGListByte(&name_mangle_list, '_');
            CMangler_MangleTypeAppend(TYPE_FUNC(type)->functype, TYPE_FUNC(type)->qual);
            return;
        case TYPESTRUCT:
            CMangler_MangleQualifier(qual);
            switch (TYPE_STRUCT(type)->stype) {
                case STRUCT_VECTOR_UCHAR:
                    AppendGListName(&name_mangle_list, "XUc");
                    return;
                case STRUCT_VECTOR_SCHAR:
                    AppendGListName(&name_mangle_list, "Xc");
                    return;
                case STRUCT_VECTOR_BCHAR:
                    AppendGListName(&name_mangle_list, "XC");
                    return;
                case STRUCT_VECTOR_USHORT:
                    AppendGListName(&name_mangle_list, "XUs");
                    return;
                case STRUCT_VECTOR_SSHORT:
                    AppendGListName(&name_mangle_list, "Xs");
                    return;
                case STRUCT_VECTOR_BSHORT:
                    AppendGListName(&name_mangle_list, "XS");
                    return;
                case STRUCT_VECTOR_UINT:
                    AppendGListName(&name_mangle_list, "XUi");
                    return;
                case STRUCT_VECTOR_SINT:
                    AppendGListName(&name_mangle_list, "Xi");
                    return;
                case STRUCT_VECTOR_BINT:
                    AppendGListName(&name_mangle_list, "XI");
                    return;
                case STRUCT_VECTOR_FLOAT:
                    AppendGListName(&name_mangle_list, "Xf");
                    return;
                case STRUCT_VECTOR_PIXEL:
                    AppendGListName(&name_mangle_list, "Xp");
                    return;
            }

            if (TYPE_STRUCT(type)->name && !IsTempName(TYPE_STRUCT(type)->name)) {
                CMangler_MangleTypeName(TYPE_STRUCT(type)->name->name);
            } else {
                switch (TYPE_STRUCT(type)->stype) {
                    case STRUCT_TYPE_STRUCT:
                        AppendGListName(&name_mangle_list, "struct");
                        return;
                    case STRUCT_TYPE_UNION:
                        AppendGListName(&name_mangle_list, "union");
                        return;
                    case STRUCT_TYPE_CLASS:
                        AppendGListName(&name_mangle_list, "class");
                        return;
                    default:
                        CError_FATAL(701);
                }
            }
            return;

        case TYPECLASS:
            CMangler_MangleQualifier(qual);
            CMangler_MangleClassName(TYPE_CLASS(type));
            return;

        case TYPETEMPLATE:
            AppendGListName(&name_mangle_list, "1T");
            return;

        default:
            CError_FATAL(716);
    }
}

void CMangler_MangleType(Type* type, UInt32 qual) {
    name_mangle_list.size = 0;
    CMangler_MangleTypeAppend(type, qual);
}

static void CMangler_MangleArgs(FuncArg* args) {
    TypePointer ptr;

    if (args) {
        if (args->type) {
            while (args) {
                if (args != &elipsis && args != &oldstyle) {
                    if (args->type->type == TYPEPOINTER) {
                        ptr = *TYPE_POINTER(args->type);
                        ptr.qual &= ~(Q_CONST | Q_VOLATILE);
                        CMangler_MangleTypeAppend(TYPE(&ptr), args->qual);
                    } else {
                        CMangler_MangleTypeAppend(args->type, 0);
                    }
                } else {
                    AppendGListByte(&name_mangle_list, 'e');
                }
                args = args->next;
            }
        } else {
            AppendGListByte(&name_mangle_list, 'e');
        }
    } else {
        AppendGListByte(&name_mangle_list, 'v');
    }
}

static void CMangler_MangleFunction(Object* obj, NameSpace* nspace) {
    TypeFunc* tfunc = TYPE_FUNC(obj->type);
    FuncArg* arg = tfunc->args;

    AppendGListName(&name_mangle_list, obj->name->name);
    if (obj->u.func.inst) {
        if (tfunc->flags & FUNC_CONVERSION) {
            CMangler_MangleTypeAppend(tfunc->functype, tfunc->qual);
        }
        CMangler_AppendTemplateArgumentList(obj->u.func.inst->args);
    }
    AppendGListName(&name_mangle_list, "__");
    while (nspace && nspace->name == NULL) {
        nspace = nspace->parent;
    }

    if (nspace) {
        CMangler_MangleNameSpaceName(nspace->parent, nspace->name->name);
        if (nspace->theclass) {
            if (obj->name == destructor_name_node) {
                AppendGListName(&name_mangle_list, "Fv");
                return;
            }
            if (arg) {
                if (obj->name == constructor_name_node) {
                    arg = arg->next;
                    if (arg && (nspace->theclass->flags & CLASS_HAS_VBASES)) {
                        arg = arg->next;
                    }
                } else {
                    if ((tfunc->flags & FUNC_METHOD) && !TYPE_METHOD(tfunc)->is_static) {
                        CMangler_MangleQualifier(arg->qual);
                        arg = arg->next;
                    }
                }
            }
        }
    }

    AppendGListByte(&name_mangle_list, 'F');
    CMangler_MangleArgs(arg);
    if (obj->u.func.inst && copts.new_mangler) {
        AppendGListByte(&name_mangle_list, '_');
        CMangler_MangleTypeAppend(tfunc->functype, tfunc->qual);
    }
}

HashNameNode* CMangler_ConversionFuncName(Type* type, UInt32 qual) {
    HashNameNode* name;

    if (CTemplTool_IsTemplateArgumentDependentType(type)) {
        return GetHashNameNodeExport("__op");
    }

    name_mangle_list.size = 0;
    AppendGListName(&name_mangle_list, "__op");
    CMangler_MangleTypeAppend(type, qual);
    AppendGListByte(&name_mangle_list, 0);

    COS_LockHandle(name_mangle_list.data);
    name = GetHashNameNodeExport(*name_mangle_list.data);
    COS_UnlockHandle(name_mangle_list.data);
    return name;
}

// (never called)
static HashNameNode* CMangler_MangleNameToUpper(char* str) {
    HashNameNode* name;

    name_mangle_list.size = 0;
    while (*str) {
        AppendGListByte(&name_mangle_list, toupper(*(str++)));
    }
    AppendGListByte(&name_mangle_list, 0);

    COS_LockHandle(name_mangle_list.data);
    name = GetHashNameNodeExport(*name_mangle_list.data);
    COS_UnlockHandle(name_mangle_list.data);
    return name;
}

static HashNameNode* CMangler_FunctionLinkName(Object* obj) {
    HashNameNode* name;
    NameSpace* nspace;

    if (obj->u.func.inst) {
        CMangler_CheckTemplateArguments(obj->u.func.inst->args);
    }

    for (nspace = obj->nspace; nspace; nspace = nspace->parent) {
        if (nspace->name) {
            break;
        }
    }

    name_mangle_list.size = 0;
    if (is_pascal_object(obj) && (!nspace || !nspace->theclass)) {
        return obj->name;
    }
    if ((obj->qual & Q_MANGLE_NAME) &&
        (strcmp("main", obj->name->name) || (obj->nspace != cscope_root))) {
        CMangler_MangleFunction(obj, nspace);
        AppendGListByte(&name_mangle_list, 0);
    } else {
        return obj->name;
    }

    COS_LockHandle(name_mangle_list.data);
    name = GetHashNameNodeExport(*name_mangle_list.data);
    COS_UnlockHandle(name_mangle_list.data);
    return name;
}

HashNameNode* CMangler_GetCovariantFunctionName(Object* dobj, TypeClass* theclass) {
    HashNameNode* name;

    name = CMangler_GetLinkName(dobj);
    name_mangle_list.size = 0;
    AppendGListName(&name_mangle_list, name->name);
    AppendGListName(&name_mangle_list, "@@");
    CMangler_MangleTypeAppend(TYPE(theclass), 0);
    AppendGListByte(&name_mangle_list, 0);

    COS_LockHandle(name_mangle_list.data);
    name = GetHashNameNodeExport(*name_mangle_list.data);
    COS_UnlockHandle(name_mangle_list.data);
    return name;
}

static HashNameNode* CMangler_DataLinkName(Object* obj) {
    NameSpace* nspace;
    HashNameNode* name;

    nspace = obj->nspace;
    while (nspace && nspace->name == NULL) {
        nspace = nspace->parent;
    }

    name_mangle_list.size = 0;
    if (!nspace) {
        return obj->name;
    }
    AppendGListData(&name_mangle_list, "", 0);
    AppendGListName(&name_mangle_list, obj->name->name);

    while (nspace && nspace->name == NULL) {
        nspace = nspace->parent;
    }
    if (nspace && (obj->qual & Q_MANGLE_NAME)) {
        AppendGListName(&name_mangle_list, "__");
        CMangler_MangleNameSpaceName(nspace->parent, nspace->name->name);
    }
    AppendGListByte(&name_mangle_list, 0);

    COS_LockHandle(name_mangle_list.data);
    name = GetHashNameNodeExport(*name_mangle_list.data);
    COS_UnlockHandle(name_mangle_list.data);
    return name;
}

HashNameNode* CMangler_GetLinkName(Object* obj) {
    while (obj->datatype == DALIAS) {
        obj = obj->u.alias.object;
    }

    switch (obj->datatype) {
        case DFUNC:
        case DVFUNC:
            if (!obj->u.func.linkname) {
                obj->u.func.linkname = CMangler_FunctionLinkName(obj);
            }
            return obj->u.func.linkname;
        case DDATA:
            if (!obj->u.data.linkname) {
                obj->u.data.linkname = CMangler_DataLinkName(obj);
            }
            return obj->u.data.linkname;
        case DINLINEFUNC:
            return CMangler_FunctionLinkName(obj);
        case DLOCAL:
        case DABSOLUTE:
        case DLABEL:
            return obj->name;
        default:
            CError_FATAL(1110);
            return NULL;
    }
}
