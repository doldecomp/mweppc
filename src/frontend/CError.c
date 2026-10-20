#include "compiler/CError.h"
#include "compiler/CClass.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CMangler.h"
#include "compiler/CObjC.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CTemplateNew.h"
#include "compiler/enode.h"
#include "compiler/InlineAsm.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/templates.h"
#include "compiler/types.h"
#include "compiler/Unmangle.h"
#include "driver/COSToolsCLT.h"
#include "driver/CWPlugins.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/DropInCompilerLinker.h"
#include "driver/Files.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct CErrorBuffer {
    char* start;
    char* end;
    UInt32 size;
    UInt32 remaining;
} CErrorBuffer;

TStreamElement* cerror_locktoken;
static TStreamElement* cerror_token;
static short cerror_errorcount;
static SInt32 cerror_lasterrorline;
char cerror_synchdata[32];
short cerror_synchoffset;
int CError_BreakPointcount;

static void CError_BufferAppendType(CErrorBuffer* eb, Type* ty, UInt32 qual);
static void CError_AppendObjectName(CErrorBuffer* eb, Object* obj);

void CError_Init(void) {
    cerror_errorcount = 0;
    cerror_lasterrorline = -1;
    cerror_token = NULL;
    cerror_locktoken = NULL;
}

void CError_SetErrorToken(TStreamElement* token) {
    if (token && token->tokenfile) {
        cerror_token = token;
    }
}

void CError_SetNullErrorToken(void) {
    cerror_token = (TStreamElement*)-1;
}

void CError_LockErrorPos(TStreamElement* token, TStreamElement** saved) {
    *saved = cerror_locktoken;
    if (token && token->tokenfile) {
        cerror_locktoken = token;
    }
}

void CError_UnlockErrorPos(TStreamElement** saved) {
    cerror_locktoken = *saved;
}

void CError_ResetErrorSkip(void) {
    cerror_lasterrorline = -1;
}

static void CError_GetErrorString(char* buf, short code) {
    CError_ASSERT(142, code >= ERR_ILLEGAL_CHARACTER_CONSTANT && code < ERR_MAX);
    COS_GetString(buf, 10000, code - 99);
}

static void CError_BufferInit(CErrorBuffer* eb, char* buf, SInt32 bufSize) {
    eb->start = eb->end = buf;
    eb->size = eb->remaining = bufSize - 1;
}

static void CError_BufferGrow(CErrorBuffer* eb, UInt32 amount) {
    char* newBuf;

    newBuf = lalloc(eb->size + amount);
    memcpy(newBuf, eb->start, eb->size);
    eb->start = newBuf;
    eb->end = newBuf + eb->size - eb->remaining;
    eb->size += amount;
    eb->remaining += amount;
}

static void CError_BufferAppendChar(CErrorBuffer* eb, char ch) {
    if (eb) {
        if (!eb->remaining) {
            CError_BufferGrow(eb, 256);
        }
        *(eb->end++) = ch;
        eb->remaining--;
    }
}

static void CError_BufferAppendString(CErrorBuffer* eb, const char* str) {
    UInt32 len;

    if (eb) {
        len = strlen(str);
        if (eb->remaining < len) {
            CError_BufferGrow(eb, len);
        }
        memcpy(eb->end, str, len);
        eb->end += len;
        eb->remaining -= len;
    }
}

static void CError_BufferTerminate(CErrorBuffer* eb) {
    if (eb->remaining == 0) {
        CError_BufferGrow(eb, 1);
    }
    *eb->end = 0;
    eb->remaining = 0;
}

static void CError_BufferAppendQualifier(CErrorBuffer* eb, UInt32 qual) {
    if (qual & Q_PASCAL) {
        CError_BufferAppendString(eb, "pascal ");
    }
    if (qual & Q_CONST) {
        CError_BufferAppendString(eb, "const ");
    }
    if (qual & Q_VOLATILE) {
        CError_BufferAppendString(eb, "volatile ");
    }
    if (qual & Q_EXPLICIT) {
        CError_BufferAppendString(eb, "explicit ");
    }
    if (qual & Q_RESTRICT) {
        CError_BufferAppendString(eb, "restrict ");
    }
}

static void CError_BufferAppendTemplArgExpr(CErrorBuffer* eb, ENode* node) {
    char buf[32];

    if (node) {
        switch (node->type) {
            case EINTCONST:
                CInt64_PrintDec(buf, node->data.intval);
                CError_BufferAppendString(eb, buf);
                return;
            case EOBJREF:
                CError_BufferAppendChar(eb, '&');
                CError_AppendObjectName(eb, node->data.objref);
                return;
            default:
                break;
        }
    }

    CError_BufferAppendString(eb, "{targ_expr}");
}

static void CError_BufferAppendTemplArg(CErrorBuffer* eb, TemplArg* targ) {
    switch (targ->pid.type) {
        case TPT_TYPE:
            CError_BufferAppendType(eb, targ->data.typeparam.type, targ->data.typeparam.qual);
            break;
        case TPT_NONTYPE:
            CError_BufferAppendTemplArgExpr(eb, targ->data.paramdecl.expr);
            break;
        case TPT_TEMPLATE:
            CError_BufferAppendType(eb, targ->data.ttargtype, 0);
            break;
        default:
            CError_FATAL(300);
    }
}

static void CError_BufferAppendTemplArgs(CErrorBuffer* eb, TemplArg* targs) {
    if (!targs) {
        return;
    }
    CError_BufferAppendChar(eb, '<');
    while (targs) {
        CError_BufferAppendTemplArg(eb, targs);
        if (targs->next) {
            CError_BufferAppendString(eb, ", ");
        }
        targs = targs->next;
    }
    CError_BufferAppendChar(eb, '>');
}

static void CError_BufferAppendNameSpace(CErrorBuffer* eb, NameSpace* nspace) {
    TemplArg* targs;

    while (nspace) {
        if (nspace->name) {
            CError_BufferAppendNameSpace(eb, nspace->parent);
            if (nspace->theclass) {
                CError_BufferAppendString(eb, nspace->theclass->classname->name);
                if (nspace->theclass->flags & CLASS_IS_TEMPL_INST) {
                    if (!(targs = TEMPL_CLASS_INST(nspace->theclass)->oargs)) {
                        targs = TEMPL_CLASS_INST(nspace->theclass)->inst_args;
                    }
                    CError_BufferAppendTemplArgs(eb, targs);
                }
            } else {
                CError_BufferAppendString(eb, nspace->name->name);
            }
            CError_BufferAppendString(eb, "::");
            return;
        }
        nspace = nspace->parent;
    }
}

static void CError_BufferAppendPType(CErrorBuffer* eb, Type* ty) {
    switch (ty->type) {
        case TYPEPOINTER:
            CError_BufferAppendPType(eb, TYPE_POINTER(ty)->target);
            if (TYPE_POINTER(ty)->qual & Q_REFERENCE) {
                CError_BufferAppendString(eb, "&");
            } else {
                CError_BufferAppendString(eb, "*");
            }
            CError_BufferAppendQualifier(eb, TYPE_POINTER(ty)->qual);
            return;
        case TYPEMEMBERPOINTER:
            CError_BufferAppendPType(eb, TYPE_MEMBER_POINTER(ty)->ty1);
            CError_BufferAppendType(eb, TYPE_MEMBER_POINTER(ty)->ty2, 0);
            CError_BufferAppendString(eb, "::*");
            CError_BufferAppendQualifier(eb, TYPE_MEMBER_POINTER(ty)->qual);
            return;
        default:
            break;
    }
}

static void CError_BufferAppendTemplDepType(CErrorBuffer* eb, TypeTemplDep* type) {
    char buf[64];
    switch (type->dtype) {
        case TEMPLDEP_ARGUMENT:
            if (type->u.pid.nindex) {
                sprintf(buf, "T%ld_%ld", (SInt32)type->u.pid.nindex, (SInt32)type->u.pid.index);
            } else {
                sprintf(buf, "T%ld", (SInt32)type->u.pid.index);
            }
            CError_BufferAppendString(eb, buf);
            break;
        case TEMPLDEP_QUALNAME:
            CError_BufferAppendTemplDepType(eb, type->u.qual.type);
            CError_BufferAppendString(eb, "::");
            CError_BufferAppendString(eb, type->u.qual.name->name);
            break;
        case TEMPLDEP_TEMPLATE:
            CError_BufferAppendType(eb, (Type*)type->u.templ.templ, 0);
            CError_BufferAppendTemplArgs(eb, type->u.templ.args);
            break;
        case TEMPLDEP_ARRAY:
            CError_BufferAppendType(eb, type->u.array.type, 0);
            CError_BufferAppendChar(eb, '[');
            CError_BufferAppendTemplArgExpr(eb, type->u.array.index);
            CError_BufferAppendChar(eb, ']');
            break;
        case TEMPLDEP_QUALTEMPL:
            CError_BufferAppendTemplDepType(eb, type->u.qualtempl.type);
            CError_BufferAppendString(eb, "::");
            CError_BufferAppendTemplArgs(eb, type->u.qualtempl.args);
            break;
        case TEMPLDEP_BITFIELD:
            CError_BufferAppendType(eb, type->u.bitfield.type, 0);
            CError_BufferAppendChar(eb, '[');
            CError_BufferAppendTemplArgExpr(eb, type->u.bitfield.size);
            CError_BufferAppendChar(eb, ']');
            break;
        default:
            CError_FATAL(463);
    }
}

static void CError_BufferAppendFuncArgs(CErrorBuffer* eb, TypeFunc* tfunc, Boolean isMethod) {
    FuncArg* arg;
    UInt32 qual;

    qual = 0;
    CError_BufferAppendChar(eb, '(');
    if ((arg = tfunc->args)) {
        if (isMethod) {
            qual = arg->qual;
            arg = arg->next;
            if (arg) {
                arg = arg->next;
            }
        } else if ((tfunc->flags & FUNC_METHOD) && !TYPE_METHOD(tfunc)->is_static) {
            qual = arg->qual;
            arg = arg->next;
            if (arg) {
                if ((tfunc->flags & FUNC_IS_DTOR) ||
                    ((tfunc->flags & FUNC_IS_CTOR) &&
                     (TYPE_METHOD(tfunc)->theclass->flags & CLASS_HAS_VBASES))) {
                    arg = arg->next;
                }
            }
        }

        while (arg) {
            if (arg == &elipsis || arg == &oldstyle) {
                CError_BufferAppendString(eb, "...");
                break;
            }

            CError_BufferAppendType(eb, arg->type, arg->qual);

            if ((arg = arg->next)) {
                CError_BufferAppendString(eb, ", ");
            } else {
                break;
            }
        }
    }
    CError_BufferAppendChar(eb, ')');
    if (qual) {
        CError_BufferAppendQualifier(eb, qual);
    }
}

static void CError_BufferAppendType(CErrorBuffer* eb, Type* ty, UInt32 qual) {
    Type* scan;
    Type* scan2;
    char buf[16];

    switch (ty->type) {
        case TYPEVOID:
            CError_BufferAppendQualifier(eb, qual);
            CError_BufferAppendString(eb, "void");
            return;
        case TYPEINT:
        case TYPEFLOAT:
            CError_BufferAppendQualifier(eb, qual);
            switch (TYPE_INTEGRAL(ty)->integral) {
                case IT_BOOL:
                    CError_BufferAppendString(eb, "bool");
                    return;
                case IT_CHAR:
                    CError_BufferAppendString(eb, "char");
                    return;
                case IT_UCHAR:
                    CError_BufferAppendString(eb, "unsigned char");
                    return;
                case IT_SCHAR:
                    CError_BufferAppendString(eb, "signed char");
                    return;
                case IT_WCHAR_T:
                    CError_BufferAppendString(eb, "wchar_t");
                    return;
                case IT_SHORT:
                    CError_BufferAppendString(eb, "short");
                    return;
                case IT_USHORT:
                    CError_BufferAppendString(eb, "unsigned short");
                    return;
                case IT_INT:
                    CError_BufferAppendString(eb, "int");
                    return;
                case IT_UINT:
                    CError_BufferAppendString(eb, "unsigned int");
                    return;
                case IT_LONG:
                    CError_BufferAppendString(eb, "long");
                    return;
                case IT_ULONG:
                    CError_BufferAppendString(eb, "unsigned long");
                    return;
                case IT_LONGLONG:
                    CError_BufferAppendString(eb, "long long");
                    return;
                case IT_ULONGLONG:
                    CError_BufferAppendString(eb, "unsigned long long");
                    return;
                case IT_FLOAT:
                    CError_BufferAppendString(eb, "float");
                    return;
                case IT_SHORTDOUBLE:
                    CError_BufferAppendString(eb, "short double");
                    return;
                case IT_DOUBLE:
                    CError_BufferAppendString(eb, "double");
                    return;
                case IT_LONGDOUBLE:
                    CError_BufferAppendString(eb, "long double");
                    return;
                case IT_VEC2X32FLOAT:
                    CError_BufferAppendString(eb, "__vec2x32float__");
                    return;
                default:
                    CError_FATAL(584);
            }
        case TYPEENUM:
            CError_BufferAppendQualifier(eb, qual);
            CError_BufferAppendNameSpace(eb, TYPE_ENUM(ty)->nspace);
            if (TYPE_ENUM(ty)->enumname) {
                CError_BufferAppendString(eb, TYPE_ENUM(ty)->enumname->name);
            } else {
                CError_BufferAppendString(eb, "{unnamed-enum}");
            }
            return;
        case TYPESTRUCT:
            CError_BufferAppendQualifier(eb, qual);
            switch (TYPE_STRUCT(ty)->stype) {
                case STRUCT_TYPE_STRUCT:
                    CError_BufferAppendString(eb, "struct ");
                    break;
                case STRUCT_TYPE_UNION:
                    CError_BufferAppendString(eb, "union ");
                    break;
                case STRUCT_VECTOR_UCHAR:
                case STRUCT_VECTOR_SCHAR:
                case STRUCT_VECTOR_BCHAR:
                case STRUCT_VECTOR_USHORT:
                case STRUCT_VECTOR_SSHORT:
                case STRUCT_VECTOR_BSHORT:
                case STRUCT_VECTOR_UINT:
                case STRUCT_VECTOR_SINT:
                case STRUCT_VECTOR_BINT:
                case STRUCT_VECTOR_FLOAT:
                case STRUCT_VECTOR_PIXEL:
                    break;
                default:
                    CError_FATAL(618);
            }
            if (TYPE_STRUCT(ty)->name) {
                CError_BufferAppendString(eb, TYPE_STRUCT(ty)->name->name);
            }
            return;
        case TYPECLASS:
            CError_BufferAppendQualifier(eb, qual);
            CError_BufferAppendNameSpace(eb, TYPE_CLASS(ty)->nspace->parent);
            if (TYPE_CLASS(ty)->classname) {
                CError_BufferAppendString(eb, TYPE_CLASS(ty)->classname->name);
                if (TYPE_CLASS(ty)->flags & CLASS_IS_TEMPL_INST) {
                    CError_BufferAppendTemplArgs(
                        eb,
                        TEMPL_CLASS_INST(ty)->oargs ? TEMPL_CLASS_INST(ty)->oargs
                                                    : TEMPL_CLASS_INST(ty)->inst_args
                    );
                }
            } else {
                CError_BufferAppendString(eb, "{unnamed-class}");
            }
            return;
        case TYPEPOINTER:
        case TYPEMEMBERPOINTER:
            scan = ty;
next_ptr:
            switch (scan->type) {
                case TYPEPOINTER:
                    scan = TYPE_POINTER(scan)->target;
                    goto next_ptr;
                case TYPEMEMBERPOINTER:
                    scan = TYPE_MEMBER_POINTER(scan)->ty1;
                    goto next_ptr;
                default:
                    break;
            }

            CError_BufferAppendQualifier(eb, qual);
            switch (scan->type) {
                case TYPEFUNC:
                    if (TYPE_FUNC(scan)->flags & FUNC_PASCAL) {
                        CError_BufferAppendString(eb, "pascal ");
                    }
                    CError_BufferAppendType(eb, TYPE_FUNC(scan)->functype, 0);
                    CError_BufferAppendString(eb, " (");
                    CError_BufferAppendPType(eb, ty);
                    CError_BufferAppendChar(eb, ')');
                    CError_BufferAppendFuncArgs(eb, TYPE_FUNC(scan), ty->type == TYPEMEMBERPOINTER);
                    return;
                case TYPEARRAY:
                    scan2 = scan;
                    while (scan->type == TYPEARRAY) {
                        scan = TYPE_POINTER(scan)->target;
                    }
                    CError_BufferAppendType(eb, scan, 0);
                    CError_BufferAppendString(eb, " (");
                    CError_BufferAppendPType(eb, ty);
                    CError_BufferAppendChar(eb, ')');
                    ty = scan2;
                    goto append_array_lengths;
                default:
                    CError_BufferAppendType(eb, scan, 0);
                    CError_BufferAppendChar(eb, ' ');
                    CError_BufferAppendPType(eb, ty);
                    return;
            }
            break;
        case TYPEFUNC:
            if (TYPE_FUNC(ty)->flags & FUNC_PASCAL) {
                CError_BufferAppendString(eb, "pascal ");
            }
            CError_BufferAppendQualifier(eb, qual);
            CError_BufferAppendType(eb, TYPE_FUNC(ty)->functype, 0);
            CError_BufferAppendChar(eb, ' ');
            CError_BufferAppendFuncArgs(eb, TYPE_FUNC(ty), FALSE);
            return;
        case TYPEARRAY:
            CError_BufferAppendQualifier(eb, qual);
            scan = ty;
            while (scan->type == TYPEARRAY) {
                scan = TYPE_POINTER(scan)->target;
            }
            CError_BufferAppendType(eb, scan, 0);
append_array_lengths:
            while (ty->type == TYPEARRAY) {
                CError_BufferAppendChar(eb, '[');
                if (ty->size != 0 && TYPE_POINTER(ty)->target->size != 0) {
                    sprintf(buf, "%ld", ty->size / TYPE_POINTER(ty)->target->size);
                    CError_BufferAppendString(eb, buf);
                }
                CError_BufferAppendChar(eb, ']');
                ty = TYPE_POINTER(ty)->target;
            }
            return;
        case TYPETEMPLATE:
            CError_BufferAppendQualifier(eb, qual);
            CError_BufferAppendTemplDepType(eb, TYPE_TEMPLATE(ty));
            return;
        case TYPETEMPLDEPEXPR:
            CError_BufferAppendString(eb, "T");
            return;
        case TYPEBITFIELD:
            sprintf(buf, "bitfield:%ld", (SInt32)TYPE_BITFIELD(ty)->bitlength);
            CError_BufferAppendString(eb, buf);
            return;
        default:
            CError_FATAL(752);
    }
}

char* CError_GetTypeName(Type* ty, UInt32 qual, Boolean useGlobalHeap) {
    CErrorBuffer eb;
    char buf[256];
    char* ptr;

    CError_BufferInit(&eb, buf, sizeof(buf));
    CError_BufferAppendType(&eb, ty, qual);
    CError_BufferTerminate(&eb);

    if (useGlobalHeap) {
        ptr = galloc(eb.size + 1);
    } else {
        ptr = lalloc(eb.size + 1);
    }

    return strcpy(ptr, eb.start);
}

static void CError_AppendUnqualFunctionName(
    CErrorBuffer* eb, NameSpace* nspace, HashNameNode* name, TypeFunc* tfunc
) {
    Boolean isCtorDtor = FALSE; // NOTE: invented name
    char* opname;

    if (nspace && nspace->theclass) {
        if (name == constructor_name_node) {
            CError_BufferAppendString(eb, nspace->theclass->classname->name);
            isCtorDtor = TRUE;
        } else if (name == destructor_name_node) {
            CError_BufferAppendChar(eb, '~');
            CError_BufferAppendString(eb, nspace->theclass->classname->name);
            isCtorDtor = TRUE;
        }
    }

    if (!isCtorDtor) {
        opname = CMangler_GetOperator(name);
        if (!opname) {
            if (tfunc && (tfunc->flags & FUNC_CONVERSION)) {
                CError_BufferAppendString(eb, "operator ");
                CError_BufferAppendType(eb, tfunc->functype, tfunc->qual);
            } else {
                CError_BufferAppendString(eb, name->name);
            }
        } else {
            CError_BufferAppendString(eb, opname);
        }
    }
}

static void CError_AppendFunctionName(
    CErrorBuffer* eb, NameSpace* nspace, HashNameNode* name, TemplArg* templargs, TypeFunc* tfunc
) {
    while (nspace->is_templ && nspace->parent) {
        nspace = nspace->parent;
    }

    CError_BufferAppendNameSpace(eb, nspace);
    CError_AppendUnqualFunctionName(eb, nspace, name, tfunc);
    CError_BufferAppendTemplArgs(eb, templargs);
    if (tfunc) {
        if (!templargs && (tfunc->flags & FUNC_IS_TEMPL)) {
            CError_BufferAppendString(eb, "<...>");
        }
        CError_BufferAppendFuncArgs(eb, tfunc, FALSE);
    } else {
        CError_BufferAppendString(eb, "()");
    }
}

static void CError_AppendObjectName(CErrorBuffer* eb, Object* obj) {
    if (obj->type->type == TYPEFUNC) {
        CError_AppendFunctionName(
            eb,
            obj->nspace,
            obj->name,
            obj->u.func.inst ? obj->u.func.inst->args : NULL,
            TYPE_FUNC(obj->type)
        );
    } else {
        CError_BufferAppendNameSpace(eb, obj->nspace);
        CError_BufferAppendString(eb, obj->name->name);
    }
}

static void CError_AppendMethodName(CErrorBuffer* eb, ObjCMethod* meth) {
    ObjCMethodArg* arg;

    CError_BufferAppendChar(eb, meth->is_class_method ? '+' : '-');
    CError_BufferAppendChar(eb, '(');
    CError_BufferAppendType(eb, meth->return_type, meth->return_qual);
    CError_BufferAppendChar(eb, ')');
    for (arg = meth->selector_args; arg; arg = arg->next) {
        if (arg->selector) {
            CError_BufferAppendString(eb, arg->selector->name);
        }
        if (arg->type) {
            CError_BufferAppendString(eb, ":(");
            CError_BufferAppendType(eb, arg->type, arg->qual);
            CError_BufferAppendChar(eb, ')');
        }
    }
    if (meth->has_valist) {
        CError_BufferAppendString(eb, ",...");
    }
}

char* CError_GetQualifiedName(NameSpace* nspace, HashNameNode* name) {
    CErrorBuffer eb;
    char buf[256];
    char* ptr;

    CError_BufferInit(&eb, buf, sizeof(buf));
    CError_BufferAppendNameSpace(&eb, nspace);
    CError_BufferAppendString(&eb, name->name);
    CError_BufferTerminate(&eb);

    ptr = lalloc(eb.size + 1);
    return strcpy(ptr, eb.start);
}

char* CError_GetFunctionName(NameSpace* nspace, HashNameNode* name, TypeFunc* tfunc) {
    CErrorBuffer eb;
    char buf[256];
    char* ptr;

    CError_BufferInit(&eb, buf, sizeof(buf));
    CError_AppendFunctionName(&eb, nspace, name, 0, tfunc);
    CError_BufferTerminate(&eb);

    ptr = lalloc(eb.size + 1);
    return strcpy(ptr, eb.start);
}

char* CError_GetObjectName(Object* obj) {
    CErrorBuffer eb;
    char buf[256];
    char* ptr;

    CError_BufferInit(&eb, buf, sizeof(buf));
    CError_AppendObjectName(&eb, obj);
    CError_BufferTerminate(&eb);

    ptr = lalloc(eb.size + 1);
    return strcpy(ptr, eb.start);
}

char* CError_GetNameString(NameSpace* nspace, HashNameNode* operatorName) {
    CErrorBuffer eb;
    char buf[256];
    char* opStr;

    CError_ASSERT(973, operatorName);

    opStr = CMangler_GetOperator(operatorName);
    if (!opStr) {
        opStr = operatorName->name;
    }

    if (nspace && nspace->name) {
        CError_BufferInit(&eb, buf, sizeof(buf));
        CError_BufferAppendNameSpace(&eb, nspace);
        CError_BufferAppendString(&eb, opStr);
        CError_BufferTerminate(&eb);
        return strcpy(lalloc(eb.size + 1), eb.start);
    } else {
        return opStr;
    }
}

// NOTE: invented names: errTable, str, isTerminal, isWarning
void CError_ErrorMessage(int errTable, char* str, Boolean isTerminal, Boolean isWarning) {
    CWMessageRef myref;
    TStreamElement* token;
    int messagetype;
    CPrepFileInfo* tokenfile;
    char buf[128];
    CWMessageRef* ref;
    short tokensize;

    if (CWDisplayLines(cparamblkptr->context, lines) != cwNoErr) {
        CError_UserBreak();
    }

    if (!in_assembler && !isTerminal && cerror_lasterrorline == lines) {
        if (cerror_errorcount++ >= 50) {
            if (cerror_errorcount > 60) {
                longjmp(errorreturn, 1);
            }
            tk = lex();
            cerror_errorcount = 0;
            if (tk == 0) {
                CompilerGetCString(1, buf);
                CWReportMessage(cparamblkptr->context, NULL, buf, NULL, messagetypeError, errTable);
                longjmp(errorreturn, 1);
            }
        }
    } else {
        if (!isWarning) {
            cerror_lasterrorline = lines;
            cerror_errorcount = 0;
        }
        if (copts.warningerrors) {
            isWarning = FALSE;
        }

        if (cerror_token) {
            token = cerror_token;
        } else if (cerror_locktoken) {
            token = cerror_locktoken;
        } else {
            token = NULL;
        }
        if ((SInt32)token == -1) {
            ref = NULL;
        } else {
            CPrep_GetTokenContext(
                token,
                &tokenfile,
                &myref.selectionoffset,
                &tokensize,
                &myref.linenumber,
                buf,
                &myref.tokenoffset,
                &myref.tokenlength,
                cerror_synchdata,
                &cerror_synchoffset
            );
            myref.selectionlength = tokensize;
            myref.sourcefile = tokenfile->textfile;
            ref = &myref;
        }
        messagetype = isWarning ? messagetypeWarning : messagetypeError;
        if (!isWarning) {
            fatalerrors = anyerrors = TRUE;
        }
        if (CWReportMessage(cparamblkptr->context, ref, str, buf, messagetype, errTable) !=
            cwNoErr) {
            longjmp(errorreturn, 1);
        }
    }

    cerror_token = NULL;
}

static void CError_BufferAppendTemplateStack(CErrorBuffer* eb) {
    TemplStack* stack[64];
    TemplStack* scan;
    int index;
    int indent;
    int count;

    scan = ctempl_curinstance;
    for (count = 0; scan && count < 64; count++) {
        stack[count] = scan;
        scan = scan->next;
    }

    for (index = count - 1; index >= 0; index--) {
        CError_BufferAppendChar(eb, '\n');
        for (indent = index; indent < count; indent++) {
            CError_BufferAppendChar(eb, ' ');
        }
        CError_BufferAppendString(eb, "(instantiating: '");
        if (stack[index]->is_func) {
            CError_AppendObjectName(eb, stack[index]->u.func);
        } else {
            CError_BufferAppendType(eb, (Type*)stack[index]->u.theclass, 0);
        }
        CError_BufferAppendString(eb, "')");
    }

    CError_BufferTerminate(eb);
}

// NOTE: invented names: code, format, list, isTerminal, isWarning
void CError_ErrorMessageVA(
    int code, const char* format, va_list list, Boolean isTerminal, Boolean isWarning
) {
    CErrorBuffer eb;
    char buf[256];
    char unmangleBuf[256];
    SInt32 moddate;
    Type* type;
    UInt32 qual;
    const char* p;
    FSSpec* spec;
    HashNameNode* file;
    CError_BufferInit(&eb, buf, sizeof(buf));

    p = format;
    do {
        switch (p[0]) {
            case 0:
                break;
            case '%':
                switch (p[1]) {
                    case 'n':
                        MWUnmangle(va_arg(list, const char*), unmangleBuf, sizeof(unmangleBuf));
                        CError_BufferAppendString(&eb, unmangleBuf);
                        p += 2;
                        continue;
                    case 'u':
                        CError_BufferAppendString(&eb, va_arg(list, const char*));
                        p += 2;
                        continue;
                    case 'o':
                        CError_AppendObjectName(&eb, va_arg(list, Object*));
                        p += 2;
                        continue;
                    case 'm':
                        CError_AppendMethodName(&eb, va_arg(list, ObjCMethod*));
                        p += 2;
                        continue;
                    case 't':
                        type = va_arg(list, Type*);
                        qual = va_arg(list, UInt32);
                        CError_BufferAppendType(&eb, type, qual);
                        p += 2;
                        continue;
                    case '%':
                        CError_BufferAppendChar(&eb, '%');
                        p += 2;
                        continue;
                    case 'i':
                        sprintf(unmangleBuf, "%ld", va_arg(list, SInt32));
                        CError_BufferAppendString(&eb, unmangleBuf);
                        p += 2;
                        continue;
                    case 'f':
                        spec = va_arg(list, FSSpec*);
                        file = CTool_GetPathName(spec, &moddate);
                        CError_BufferAppendString(&eb, file->name);
                        p += 2;
                        continue;
                    default:
                        CError_FATAL(1174);
                }
                break;
            default:
                CError_BufferAppendChar(&eb, *(p++));
                continue;
        }
        break;
    } while (1);

    CError_BufferAppendTemplateStack(&eb);
    CError_ErrorMessage(code, eb.start, isTerminal, isWarning);
}

// NOTE: invented names: code, list, isTerminal, isWarning
static void CError_VAErrorMessage(int code, va_list list, Boolean isTerminal, Boolean isWarning) {
    char buf[256];

    CError_GetErrorString(buf, code);
    CError_ErrorMessageVA(code + 10000, buf, list, isTerminal, isWarning);
}

void CError_Error(int code, ...) {
    va_list va;

    if (trychain) {
        longjmp(trychain->jmpbuf, 1);
    }

    va_start(va, code);
    CError_VAErrorMessage(code, va, FALSE, FALSE);
    va_end(va);

    if (in_assembler && !preprocessing_only) {
        AssemblerError();
    }
}

void CError_ErrorTerm(short code) {
    CError_GetErrorString(string, code);
    CError_ErrorMessage(code + 10000, string, FALSE, FALSE);
    longjmp(errorreturn, 1);
}

void CError_ErrorSkip(int code, ...) {
    va_list va;

    if (trychain) {
        longjmp(trychain->jmpbuf, 1);
    }

    va_start(va, code);
    CError_VAErrorMessage(code, va, FALSE, FALSE);
    va_end(va);

    if (tk != ';' && tk != ')' && tk != '}' && tk != ',' && tk != ']') {
        tk = lex();
    }
}

void CError_ErrorFuncCall(short code, NameSpaceObjectList* args, ENodeList* argNodes) {
    CErrorBuffer eb;
    char buf[256];
    char* p;
    ENodeList* argscan;

    if (trychain) {
        longjmp(trychain->jmpbuf, 1);
    }

    CError_GetErrorString(string, code);
    CError_BufferInit(&eb, buf, sizeof(buf));

    while (args && args->object->otype != OT_OBJECT) {
        args = args->next;
    }
    CError_ASSERT(1268, args);

    p = string;
    do {
        switch (*p) {
            case 0:
                goto exit_main_loop;
            case '*':
                if (OBJECT(args->object)->type->type == TYPEFUNC) {
                    CError_AppendUnqualFunctionName(
                        &eb,
                        OBJECT(args->object)->nspace,
                        OBJECT(args->object)->name,
                        TYPE_FUNC(OBJECT(args->object)->type)
                    );
                    if (TYPE_FUNC(OBJECT(args->object)->type)->flags & FUNC_METHOD) {
                        if (TYPE_FUNC(OBJECT(args->object)->type)->flags & FUNC_IS_CTOR) {
                            if (TYPE_METHOD(OBJECT(args->object)->type)->theclass->flags &
                                CLASS_HAS_VBASES) {
                                if (argNodes) {
                                    argNodes = argNodes->next;
                                }
                            }
                        }
                    }
                } else {
                    CError_BufferAppendString(&eb, OBJECT(args->object)->name->name);
                }
                CError_BufferAppendChar(&eb, '(');
                argscan = argNodes;
                while (argscan) {
                    CError_BufferAppendType(
                        &eb, argscan->node->rtype, argscan->node->flags & ENODE_FLAG_QUALS
                    );
                    if ((argscan = argscan->next)) {
                        CError_BufferAppendString(&eb, ", ");
                    } else {
                        break;
                    }
                }
                CError_BufferAppendChar(&eb, ')');
                break;
            default:
                CError_BufferAppendChar(&eb, *p);
        }
        p++;
    } while (1);

exit_main_loop:
    while (args) {
        if (args->object->otype == OT_OBJECT) {
            CError_BufferAppendChar(&eb, '\n');
            CError_BufferAppendChar(&eb, '\'');
            CError_AppendObjectName(&eb, (Object*)args->object);
            CError_BufferAppendChar(&eb, '\'');
        }
        args = args->next;
    }

    CError_BufferAppendTemplateStack(&eb);
    CError_ErrorMessage(10000 + code, eb.start, FALSE, FALSE);
}

void CError_OverloadedFunctionError2(Object* obj, ObjectList* olst, ENodeList* argNodes) {
    // no caller in the compiler
    NameSpaceObjectList first;
    NameSpaceObjectList* current;

    first.object = (ObjBase*)obj;
    current = &first;
    while (olst) {
        current->next = lalloc(sizeof(NameSpaceObjectList));
        current = current->next;
        current->object = (ObjBase*)olst->object;
        olst = olst->next;
    }
    current->next = NULL;

    CError_ErrorFuncCall(ERR_FUNCTION_CALL_AMBIGUOUS, &first, argNodes);
}

void CError_OverloadedFunctionError(Object* obj, ObjectList* olst) {
    CErrorBuffer eb;
    char buf[256];

    if (trychain) {
        longjmp(trychain->jmpbuf, 1);
    }

    CError_GetErrorString(string, ERR_AMBIGUOUS_ACCESS_OVERLOADED_FUNCTION);
    CError_BufferInit(&eb, buf, sizeof(buf));
    CError_BufferAppendString(&eb, string);

    if (obj) {
        CError_BufferAppendChar(&eb, '\n');
        CError_BufferAppendChar(&eb, '\'');
        CError_AppendObjectName(&eb, obj);
        CError_BufferAppendChar(&eb, '\'');
    }
    while (olst) {
        CError_BufferAppendChar(&eb, '\n');
        CError_BufferAppendChar(&eb, '\'');
        CError_AppendObjectName(&eb, olst->object);
        CError_BufferAppendChar(&eb, '\'');
        olst = olst->next;
    }
    CError_BufferAppendTemplateStack(&eb);
    CError_ErrorMessage(10199, eb.start, FALSE, FALSE);
}

void CError_AbstractClassError(TypeClass* tclass) {
    Object* result = CClass_CheckPures(tclass);
    if (!result) {
        CError_Error(ERR_ILLEGAL_USE_ABSTRACT_CLASS_TYPE, tclass, 0);
    } else {
        CError_Error(ERR_ILLEGAL_USE_ABSTRACT_CLASS, result);
    }
}

void CError_Warning(int code, ...) {
    va_list va;
    if (trychain || copts.supress_warnings) {
        return;
    }

    va_start(va, code);
    CError_VAErrorMessage(code, va, FALSE, TRUE);
    va_end(va);
}

void CError_BreakPoint(const char* a, const char* b) {
    if (!a || !strcmp(a, b)) {
        CError_BreakPointcount++;
    }
}

void CError_Internal(char* filename, int line) {
    char format[128]; // NOTE: invented name
    CompilerGetCString(5, format);
    sprintf(string, format, filename, line);
    CError_ErrorMessage(10001, string, TRUE, FALSE);
    longjmp(errorreturn, 1);
    CError_BreakPoint(NULL, NULL);
}

void CError_ExpressionTooComplex(void) {
    CompilerGetCString(6, string);
    CError_ErrorMessage(10002, string, TRUE, FALSE);
    longjmp(errorreturn, 1);
}

void CError_NoMem(void) {
    cprep_nomem_exit = TRUE;
    longjmp(errorreturn, 1);
}

void CError_UserBreak(void) {
    CompilerGetCString(8, string);
    longjmp(errorreturn, 1);
}

void CError_CannotOpen(void) {
    CompilerGetCString(9, string);
    CWReportMessage(cparamblkptr->context, NULL, string, NULL, messagetypeError, 0);
    longjmp(errorreturn, 1);
}

void CError_QualifierCheck(UInt32 qual) {
    if (qual) {
        Boolean anything = FALSE;

        if (qual & Q_CONST) {
            CError_Error(ERR_ILLEGAL_USE, "const");
            anything = TRUE;
        }
        if (qual & Q_VOLATILE) {
            CError_Error(ERR_ILLEGAL_USE, "volatile");
            anything = TRUE;
        }
        if (qual & Q_RESTRICT) {
            CError_Error(ERR_ILLEGAL_USE, "restrict");
            anything = TRUE;
        }
        if (qual & Q_ASM) {
            CError_Error(ERR_ILLEGAL_USE, "asm");
            anything = TRUE;
        }
        if (qual & Q_PASCAL) {
            CError_Error(ERR_ILLEGAL_USE, "pascal");
            anything = TRUE;
        }
        if (qual & Q_INLINE) {
            CError_Error(ERR_ILLEGAL_USE, "inline");
            anything = TRUE;
        }
        if (qual & Q_REFERENCE) {
            CError_Error(ERR_ILLEGAL_USE, "& reference type");
            anything = TRUE;
        }
        if (qual & Q_EXPLICIT) {
            CError_Error(ERR_ILLEGAL_USE, "explicit");
            anything = TRUE;
        }
        if (qual & Q_MUTABLE) {
            CError_Error(ERR_ILLEGAL_USE, "mutable");
            anything = TRUE;
        }
        if (qual & Q_VIRTUAL) {
            CError_Error(ERR_ILLEGAL_USE, "virtual");
            anything = TRUE;
        }
        if (qual & Q_FRIEND) {
            CError_Error(ERR_ILLEGAL_USE, "friend");
            anything = TRUE;
        }
        if (qual & Q_IN) {
            CError_Error(ERR_ILLEGAL_USE, "in");
            anything = TRUE;
        }
        if (qual & Q_OUT) {
            CError_Error(ERR_ILLEGAL_USE, "out");
            anything = TRUE;
        }
        if (qual & Q_INOUT) {
            CError_Error(ERR_ILLEGAL_USE, "inout");
            anything = TRUE;
        }
        if (qual & Q_BYCOPY) {
            CError_Error(ERR_ILLEGAL_USE, "bycopy");
            anything = TRUE;
        }
        if (qual & Q_BYREF) {
            CError_Error(ERR_ILLEGAL_USE, "byref");
            anything = TRUE;
        }
        if (qual & Q_ONEWAY) {
            CError_Error(ERR_ILLEGAL_USE, "oneway");
            anything = TRUE;
        }
        if (qual & Q_INTERRUPT) {
            CError_Error(ERR_ILLEGAL_USE, "__declspec(interrupt)");
            anything = TRUE;
        }
        if (qual & Q_ALIGNED_MASK) {
            CError_Error(ERR_ILLEGAL_USE, "__attribute__((aligned(?)))");
            anything = TRUE;
        }

        if (!anything) {
            CError_Error(ERR_ILLEGAL_TYPE_QUALIFIERS);
        }
    }
}

// The unit's statics are 2-byte packed, as in the original (MWCC lays out .bss with the packing in
// force at the end of the file)
#pragma pack(2)
