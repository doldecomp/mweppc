#include "compiler/CParser.h"
#include "compiler/IroPointerAnalysis.h"
#include "compiler/IrOptimizer.h"
#include "compiler/BE_elf.h"
#include "compiler/CABI.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CInline.h"
#include "compiler/CInt64.h"
#include "compiler/CIRTransform.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CObjC.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/CSOM.h"
#include "compiler/CTemplateNew.h"
#include "compiler/CTemplateTools.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/templates.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "compiler/win32.h"
#include "driver/COSToolsCLT.h"
#include <setjmp.h>
#include <stdarg.h>
#include <string.h>

FileOffsetInfo cparser_fileoffset;
TStreamElement symdecltoken;
ParserTryBlock* trychain;
Boolean dont_set_references;
TypeStruct ptmstruct;
TypeStruct catchinfostruct;
Boolean in_assembler;
Boolean illegalimplicitconversion;
Boolean in_func_arglist;
NameSpaceName* newp_fobj;
NameSpaceName* newa_fobj;
NameSpaceName* delp_fobj;
NameSpaceName* dela_fobj;
Object* newh_func;
Object* delh_func;
Object* copy_func;
Object* clear_func;
Object* Rgtid_func;
Object* Rdync_func;
Object* rt_ptmf_cast;
Object* rt_ptmf_cmpr;
Object* rt_ptmf_test;
Object* rt_ptmf_call;
Object* rt_ptmf_scall;
Object* rt_ptmf_call4;
Object* rt_ptmf_scall4;
Object* rt_ptmf_null;
Object* rt_som_new;
Object* rt_som_newcheck;
Object* rt_som_check;
Object* rt_som_glue1;
Object* rt_som_glue2;
Object* rt_som_glue3;
Object* carr_func;
Object* cnar_func;
Object* darr_func;
Object* dnar_func;
Object* dnar3_func;
Object* Xgreg_func;
Object* Xthrw_func;
Object* Xicth_func;
Object* Xecth_func;
Object* Xunex_func;
CompilerLinkerOptions copts;
GList name_mangle_list;
HashNameNode* no_name_node;
HashNameNode* temp_argument_name;
HashNameNode* this_name_node;
HashNameNode* self_name_node;
HashNameNode* vptr_name_node;
CallbackAction* callbackactions;
Boolean fatalerrors;
Boolean anyerrors;
jmp_buf errorreturn;
char string[256];
static HashNameNode* uniquenamespacename;
static SInt32 uniqueid;

struct ClassAction {
    struct ClassAction* next;
    TypeClass* tclass;
};
static struct ClassAction* cparser_classactions;

struct ParentCleanup {
    struct ParentCleanup* next;
    TypeClass* tclass;
};
static struct ParentCleanup* cparser_parentcleanup;

struct SFuncList {
    struct SFuncList* next;
    Object* func;
    Object* obj;
    ENode* expr;
};
static struct SFuncList* cparser_sfunclist;

Type sttemplexpr = {TYPETEMPLDEPEXPR, 0};
Type stillegal = {TYPEILLEGAL, 1};
Type stvoid = {TYPEVOID, 0};
TypePointer void_ptr = {TYPEPOINTER, 0, &stvoid, 0};
TypeFunc rt_func = {TYPEFUNC, 0, NULL, NULL, &stvoid, 0, 0};

#ifndef __MWERKS__
extern TypeIntegral stcustomtype113;
extern TypeIntegral stcustomtype114;

typedef struct OriginalObject {
    const void* object;
    UInt32 size;
    UInt32 address;
} OriginalObject;

#if VERSION < VERSION_GC_2_5
#define ORIGINAL(object, v2_0, v2_5, v2_6, v2_7) {&(object), sizeof(object), v2_0}
#elif VERSION == VERSION_GC_2_5
#define ORIGINAL(object, v2_0, v2_5, v2_6, v2_7) {&(object), sizeof(object), v2_5}
#elif VERSION == VERSION_GC_2_6
#define ORIGINAL(object, v2_0, v2_5, v2_6, v2_7) {&(object), sizeof(object), v2_6}
#else
#define ORIGINAL(object, v2_0, v2_5, v2_6, v2_7) {&(object), sizeof(object), v2_7}
#endif

// clang-format off
static const OriginalObject original_objects[] = {
    ORIGINAL(stbool,                0x005AFE70, 0x005B0E90, 0x005B0E90, 0x005B0E90),
    ORIGINAL(stchar,                0x005AFE78, 0x005B0E98, 0x005B0E98, 0x005B0E98),
    ORIGINAL(stsignedchar,          0x005AFE80, 0x005B0EA0, 0x005B0EA0, 0x005B0EA0),
    ORIGINAL(stunsignedchar,        0x005AFE88, 0x005B0EA8, 0x005B0EA8, 0x005B0EA8),
    ORIGINAL(stwchar,               0x005AFE90, 0x005B0EB0, 0x005B0EB0, 0x005B0EB0),
    ORIGINAL(stsignedshort,         0x005AFE98, 0x005B0EB8, 0x005B0EB8, 0x005B0EB8),
    ORIGINAL(stunsignedshort,       0x005AFEA0, 0x005B0EC0, 0x005B0EC0, 0x005B0EC0),
    ORIGINAL(stsignedint,           0x005AFEA8, 0x005B0EC8, 0x005B0EC8, 0x005B0EC8),
    ORIGINAL(stunsignedint,         0x005AFEB0, 0x005B0ED0, 0x005B0ED0, 0x005B0ED0),
    ORIGINAL(stsignedlong,          0x005AFEB8, 0x005B0ED8, 0x005B0ED8, 0x005B0ED8),
    ORIGINAL(stunsignedlong,        0x005AFEC0, 0x005B0EE0, 0x005B0EE0, 0x005B0EE0),
    ORIGINAL(stsignedlonglong,      0x005AFEC8, 0x005B0EE8, 0x005B0EE8, 0x005B0EE8),
    ORIGINAL(stunsignedlonglong,    0x005AFED0, 0x005B0EF0, 0x005B0EF0, 0x005B0EF0),
    ORIGINAL(stfloat,               0x005AFED8, 0x005B0EF8, 0x005B0EF8, 0x005B0EF8),
    ORIGINAL(stshortdouble,         0x005AFEE0, 0x005B0F00, 0x005B0F00, 0x005B0F00),
    ORIGINAL(stdouble,              0x005AFEE8, 0x005B0F08, 0x005B0F08, 0x005B0F08),
    ORIGINAL(stlongdouble,          0x005AFEF0, 0x005B0F10, 0x005B0F10, 0x005B0F10),
    ORIGINAL(stcustomtype113,       0x005AFEF8, 0x005B0F18, 0x005B0F18, 0x005B0F18),
    ORIGINAL(stcustomtype114,       0x005AFF00, 0x005B0F20, 0x005B0F20, 0x005B0F20),
    ORIGINAL(stvec2x32float,        0x005AFF08, 0x005B0F28, 0x005B0F28, 0x005B0F28),
    ORIGINAL(stvectorunsignedchar,  0x005B03C0, 0x005B13E0, 0x005B13E0, 0x005B13E0),
    ORIGINAL(stvectorsignedchar,    0x005B03D4, 0x005B13F4, 0x005B13F4, 0x005B13F4),
    ORIGINAL(stvectorboolchar,      0x005B03E8, 0x005B1408, 0x005B1408, 0x005B1408),
    ORIGINAL(stvectorunsignedshort, 0x005B03FC, 0x005B141C, 0x005B141C, 0x005B141C),
    ORIGINAL(stvectorsignedshort,   0x005B0410, 0x005B1430, 0x005B1430, 0x005B1430),
    ORIGINAL(stvectorboolshort,     0x005B0424, 0x005B1444, 0x005B1444, 0x005B1444),
    ORIGINAL(stvectorunsignedlong,  0x005B0438, 0x005B1458, 0x005B1458, 0x005B1458),
    ORIGINAL(stvectorsignedlong,    0x005B044C, 0x005B146C, 0x005B146C, 0x005B146C),
    ORIGINAL(stvectorboollong,      0x005B0460, 0x005B1480, 0x005B1480, 0x005B1480),
    ORIGINAL(stvectorfloat,         0x005B0474, 0x005B1494, 0x005B1494, 0x005B1494),
    ORIGINAL(stvectorpixel,         0x005B0488, 0x005B14A8, 0x005B14A8, 0x005B14A8),
    ORIGINAL(stvector,              0x005B049C, 0x005B14BC, 0x005B14BC, 0x005B14BC),
    ORIGINAL(sttemplexpr,           0x005BA3B8, 0x005BB3D8, 0x005BB3D8, 0x005BB3D8),
    ORIGINAL(stillegal,             0x005BA3C0, 0x005BB3E0, 0x005BB3E0, 0x005BB3E0),
    ORIGINAL(stvoid,                0x005BA3C8, 0x005BB3E8, 0x005BB3E8, 0x005BB3E8),
    ORIGINAL(void_ptr,              0x005BA3D0, 0x005BB3F0, 0x005BB3F0, 0x005BB3F0),
    ORIGINAL(rt_func,               0x005BA3E0, 0x005BB400, 0x005BB400, 0x005BB400),
    ORIGINAL(ptmstruct,             0x005EA1BA, 0x005EB1F8, 0x005EB1F0, 0x005EF1F0),
    ORIGINAL(catchinfostruct,       0x005EA178, 0x005EB1B6, 0x005EB1AE, 0x005EF1AE),
    ORIGINAL(elipsis,               0x005E3158, 0x005E4178, 0x005E4170, 0x005E4170),
    ORIGINAL(oldstyle,              0x005E54B8, 0x005E63C8, 0x005E63C0, 0x005E63C0),
    ORIGINAL(cscope_currentfunc,    0x005E9CC8, 0x005EABF4, 0x005EABEC, 0x005EEBEC),
};
// clang-format on

#undef ORIGINAL

UInt32 CParser_OriginalAddress(const void* ptr) {
    const OriginalObject* scan;
    const char* p = ptr;

    for (scan = original_objects;
         scan < original_objects + sizeof(original_objects) / sizeof(original_objects[0]);
         scan++) {
        if (p >= (const char*)scan->object && p < (const char*)scan->object + scan->size) {
            return scan->address + (UInt32)(p - (const char*)scan->object);
        }
    }

    return (UInt32)ptr;
}

UInt8 CParser_UncheckedIntegral(Type* type) {
    switch (type->type) {
        case TYPEINT:
        case TYPEFLOAT:
            return TYPE_INTEGRAL(type)->integral;
        case TYPEENUM:
        case TYPESTRUCT:
        case TYPECLASS:
        case TYPEFUNC:
        case TYPEBITFIELD:
        case TYPEMEMBERPOINTER:
        case TYPEPOINTER:
        case TYPEARRAY:
        case TYPEOBJCID:
            return (UInt8)CParser_OriginalAddress(TYPE_POINTER(type)->target);
        case TYPETEMPLATE:
            return TYPE_INTEGRAL(type)->integral;
        default:
            if (CParser_OriginalAddress(type) != (UInt32)type) {
                return 0;
            }
            return TYPE_INTEGRAL(type)->integral;
    }
}
#endif

static void CParser_ParseDeclaration(DeclInfo* di);

Object* CParser_NewRTFunc(Type* rettype, HashNameNode* name, UInt8 kind, int argcount, ...) {
    Object* obj;
    FuncArg* args;
    FuncArg* arg;
    TypeFunc* tfunc;
    va_list va;

    args = NULL;
    if (argcount) {
        va_start(va, argcount);
        while (--argcount >= 0) {
            if (args) {
                arg->next = CParser_NewFuncArg();
                arg = arg->next;
            } else {
                args = arg = CParser_NewFuncArg();
            }
            arg->type = va_arg(va, Type*);
        }
        va_end(va);
    }

    obj = CParser_NewFunctionObject(NULL);

    tfunc = galloc(sizeof(TypeFunc));
    memclrw(tfunc, sizeof(TypeFunc));
    tfunc->type = TYPEFUNC;
    tfunc->functype = rettype;
    tfunc->args = args;
    CDecl_SetFuncFlags(tfunc, 0);

    obj->name = name;
    obj->type = TYPE(tfunc);
    if (kind == 1) {
        obj->qual = Q_MANGLE_NAME;
    }

    CodeGen_UpdateObject(obj);
    return obj;
}

Boolean CParser_IsPublicRuntimeObject(Object* obj) {
    if (newp_fobj->first.object == OBJ_BASE(obj) && !newp_fobj->first.next) {
        return TRUE;
    }
    if (newa_fobj->first.object == OBJ_BASE(obj) && !newa_fobj->first.next) {
        return TRUE;
    }
    if (delp_fobj->first.object == OBJ_BASE(obj) && !delp_fobj->first.next) {
        return TRUE;
    }
    if (dela_fobj->first.object == OBJ_BASE(obj) && !dela_fobj->first.next) {
        return TRUE;
    }
    return CodeGen_IsPublicRuntimeObject(obj);
}

Object* CParser_FindPublicRuntimeObject(HashNameNode* name) {
    NameSpaceObjectList* list = CScope_FindName(cscope_root, name);
    if (list &&
        list->object->otype == OT_OBJECT &&
        (!list->next || list->next->object->otype == OT_TYPETAG)) {
        return OBJECT(list->object);
    } else {
        return NULL;
    }
}

Boolean CParser_ReInitRuntimeObjects(Boolean is_precompiler) {
    if (!(newp_fobj = CScope_FindNameSpaceName(cscope_root, CMangler_OperatorName(TK_NEW)))) {
        return FALSE;
    }
    if (!(newa_fobj = CScope_FindNameSpaceName(cscope_root, CMangler_OperatorName(TK_NEW_ARRAY)))) {
        return FALSE;
    }
    if (!(delp_fobj = CScope_FindNameSpaceName(cscope_root, CMangler_OperatorName(TK_DELETE)))) {
        return FALSE;
    }
    if (!(dela_fobj =
              CScope_FindNameSpaceName(cscope_root, CMangler_OperatorName(TK_DELETE_ARRAY)))) {
        return FALSE;
    }

    newh_func->name = GetHashNameNodeExport("__new_hdl");
    delh_func->name = GetHashNameNodeExport("__del_hdl");
    copy_func->name = GetHashNameNodeExport("__copy");
    clear_func->name = GetHashNameNodeExport("__clear");
    Rgtid_func->name = GetHashNameNodeExport("__get_typeid");
    Rdync_func->name = GetHashNameNodeExport("__dynamic_cast");
    rt_ptmf_cast->name = GetHashNameNodeExport("__ptmf_cast");
    rt_ptmf_cmpr->name = GetHashNameNodeExport("__ptmf_cmpr");
    rt_ptmf_test->name = GetHashNameNodeExport("__ptmf_test");
    rt_ptmf_call->name = GetHashNameNodeExport("__ptmf_call");
    rt_ptmf_scall->name = GetHashNameNodeExport("__ptmf_scall");
    rt_ptmf_call4->name = GetHashNameNodeExport("__ptmf_call4");
    rt_ptmf_scall4->name = GetHashNameNodeExport("__ptmf_scall4");
    rt_ptmf_null->name = GetHashNameNodeExport("__ptmf_null");
    rt_som_new->name = GetHashNameNodeExport("__som_new");
    rt_som_newcheck->name = GetHashNameNodeExport("__som_check_new");
    rt_som_check->name = GetHashNameNodeExport("__som_check_ev");
    rt_som_glue1->name = GetHashNameNodeExport("_som_ptrgl4");
    rt_som_glue2->name = GetHashNameNodeExport("_som_ptrgl5");
    rt_som_glue3->name = GetHashNameNodeExport("_som_ptrgl_");
    carr_func->name = GetHashNameNodeExport("__construct_array");
    cnar_func->name = GetHashNameNodeExport("__construct_new_array");
    darr_func->name = GetHashNameNodeExport("__destroy_arr");
    dnar_func->name = GetHashNameNodeExport("__destroy_new_array");
    dnar3_func->name = GetHashNameNodeExport("__destroy_new_array3");
    Xgreg_func->name = GetHashNameNodeExport("__register_global_object");
    Xthrw_func->name = GetHashNameNodeExport("__throw");
    Xicth_func->name = GetHashNameNodeExport("__init__catch");
    Xecth_func->name = GetHashNameNodeExport("__end__catch");
    Xunex_func->name = GetHashNameNodeExport("__unexpected");

    CMangler_Setup();

    no_name_node = GetHashNameNodeExport("@no_name@");
    temp_argument_name = GetHashNameNodeExport("@temp_ptr@");
    this_name_node = GetHashNameNodeExport("this");
    self_name_node = GetHashNameNodeExport("self");
    vptr_name_node = GetHashNameNodeExport("__vptr$");

    CSOM_Setup(is_precompiler);
    return CodeGen_ReInitRuntimeObjects(is_precompiler);
}

static void CParser_SetupRuntimeObjects(void) {
    Type* sizet;
    ExceptSpecList* exspecs;
    Object* func;

    exspecs = galloc(sizeof(ExceptSpecList));
    memclrw(exspecs, sizeof(ExceptSpecList));

    sizet = CABI_GetSizeTType();

    func = CParser_NewRTFunc(TYPE(&void_ptr), CMangler_OperatorName(TK_NEW), 1, 1, sizet);
    CScope_AddGlobalObject(func);

    func = CParser_NewRTFunc(TYPE(&void_ptr), CMangler_OperatorName(TK_NEW_ARRAY), 1, 1, sizet);
    CScope_AddGlobalObject(func);

    func = CParser_NewRTFunc(TYPE(&stvoid), CMangler_OperatorName(TK_DELETE), 1, 1, &void_ptr);
    CError_ASSERT(379, IS_TYPE_FUNC(func->type));
    TYPE_FUNC(func->type)->exspecs = exspecs;
    CScope_AddGlobalObject(func);

    func =
        CParser_NewRTFunc(TYPE(&stvoid), CMangler_OperatorName(TK_DELETE_ARRAY), 1, 1, &void_ptr);
    CError_ASSERT(387, IS_TYPE_FUNC(func->type));
    TYPE_FUNC(func->type)->exspecs = exspecs;
    CScope_AddGlobalObject(func);

    newh_func = CParser_NewRTFunc(TYPE(&void_ptr), NULL, 0, 1, sizet);
    delh_func = CParser_NewRTFunc(TYPE(&stvoid), NULL, 0, 1, &void_ptr);

    Rgtid_func = CParser_NewRTFunc(TYPE(&void_ptr), NULL, 0, 2, &void_ptr, &stsignedlong);
    Rdync_func = CParser_NewRTFunc(
        TYPE(&void_ptr), NULL, 0, 5, &void_ptr, &stsignedlong, &void_ptr, &void_ptr, &stsignedshort
    );

    copy_func = CParser_NewRTFunc(TYPE(&void_ptr), NULL, 2, 3, &void_ptr, &void_ptr, sizet);
    clear_func = CParser_NewRTFunc(TYPE(&void_ptr), NULL, 2, 2, &void_ptr, sizet);

    rt_ptmf_cast =
        CParser_NewRTFunc(TYPE(&void_ptr), NULL, 2, 3, &stsignedlong, &void_ptr, &void_ptr);
    rt_ptmf_cmpr = CParser_NewRTFunc(TYPE(&stsignedlong), NULL, 2, 2, &void_ptr, &void_ptr);
    rt_ptmf_test = CParser_NewRTFunc(TYPE(&stsignedlong), NULL, 2, 1, &void_ptr);

    rt_ptmf_call = CParser_NewRTFunc(TYPE(&stvoid), NULL, 2, 0);
    rt_ptmf_scall = CParser_NewRTFunc(TYPE(&stvoid), NULL, 2, 0);
    rt_ptmf_call4 = CParser_NewRTFunc(TYPE(&stvoid), NULL, 2, 0);
    rt_ptmf_scall4 = CParser_NewRTFunc(TYPE(&stvoid), NULL, 2, 0);

    rt_ptmf_null = CParser_NewGlobalDataObject(NULL);
    rt_ptmf_null->type = &stvoid;

    rt_som_new =
        CParser_NewRTFunc(TYPE(&void_ptr), NULL, 2, 3, &void_ptr, &stsignedlong, &stsignedlong);
    rt_som_newcheck = CParser_NewRTFunc(TYPE(&stvoid), NULL, 0, 1, &void_ptr);
    rt_som_check = CParser_NewRTFunc(TYPE(&stvoid), NULL, 0, 1, &void_ptr);
    rt_som_glue1 = CParser_NewRTFunc(TYPE(&stvoid), NULL, 2, 0);
    rt_som_glue2 = CParser_NewRTFunc(TYPE(&stvoid), NULL, 2, 0);
    rt_som_glue3 = CParser_NewRTFunc(TYPE(&stvoid), NULL, 2, 0);

    carr_func =
        CParser_NewRTFunc(TYPE(&stvoid), NULL, 0, 5, &void_ptr, &void_ptr, &void_ptr, sizet, sizet);
    cnar_func = CParser_NewRTFunc(
        TYPE(&void_ptr), NULL, 0, 5, &void_ptr, &void_ptr, &void_ptr, sizet, sizet
    );
    darr_func = CParser_NewRTFunc(TYPE(&stvoid), NULL, 0, 4, &void_ptr, &void_ptr, sizet, sizet);
    dnar_func = CParser_NewRTFunc(TYPE(&stvoid), NULL, 0, 2, &void_ptr, &void_ptr);
    dnar3_func = CParser_NewRTFunc(
        TYPE(&stvoid), NULL, 0, 4, &void_ptr, &void_ptr, &void_ptr, &stsignedshort
    );

    Xgreg_func = CParser_NewRTFunc(TYPE(&void_ptr), NULL, 0, 3, &void_ptr, &void_ptr, &void_ptr);
    Xthrw_func = CParser_NewRTFunc(TYPE(&stvoid), NULL, 0, 3, &void_ptr, &void_ptr, &void_ptr);
    Xicth_func = CParser_NewRTFunc(TYPE(&stvoid), NULL, 0, 1, &void_ptr);
    Xecth_func = CParser_NewRTFunc(TYPE(&stvoid), NULL, 0, 1, &void_ptr);
    Xunex_func = CParser_NewRTFunc(TYPE(&stvoid), NULL, 0, 1, &void_ptr);

    CodeGen_SetupRuntimeObjects();
    CError_ASSERT(534, CParser_ReInitRuntimeObjects(FALSE));
}

void CParser_Setup(void) {
    CScope_Setup();

    name_mangle_list.data = NULL;
    if (InitGList(&name_mangle_list, 256)) {
        CError_NoMem();
    }

    void_ptr.size = 4;
    CError_Init();
    CInit_Init();
    CClass_Init();
    CIRTrans_Setup();
    CObjC_Setup();
    CInline_Init();

    in_assembler = FALSE;
    in_func_arglist = FALSE;
    CParser_SetUniqueID(1);
    dont_set_references = FALSE;

    copts.sideeffects = TRUE;
    cparser_classactions = NULL;
    name_obj_check = NULL;
    callbackactions = NULL;
    init_expressions = NULL;
    cparser_sfunclist = NULL;
    trychain = NULL;
    cparser_parentcleanup = NULL;

    memclrw(&cparser_fileoffset, sizeof(FileOffsetInfo));

    memclrw(&catchinfostruct, sizeof(TypeStruct));
    catchinfostruct.type = TYPESTRUCT;
    catchinfostruct.size = 24;
    catchinfostruct.stype = STRUCT_TYPE_STRUCT;
    catchinfostruct.align = 4;

    memclrw(&ptmstruct, sizeof(TypeStruct));
    ptmstruct.type = TYPESTRUCT;
    ptmstruct.size = 12;
    ptmstruct.stype = STRUCT_TYPE_STRUCT;
    ptmstruct.align = 4;

    CMach_Configure();
    CTempl_Setup();

    uniquenamespacename = NULL;
    disallowgreaterthan = FALSE;

    CParser_SetupRuntimeObjects();
}

void CParser_Cleanup(void) {
    CTempl_Cleanup();
    CIRTrans_Cleanup();
    CObjC_Cleanup();
    CScope_Cleanup();
    FreeGList(&name_mangle_list);
}

short GetPrec(short token) {
    switch (token) {
        case '%':
        case '*':
        case '/':
            return 11;
        case '+':
        case '-':
            return 10;
        case TK_SHL:
        case TK_SHR:
            return 9;
        case '<':
        case '>':
        case TK_LESS_EQUAL:
        case TK_GREATER_EQUAL:
            return 8;
        case TK_LOGICAL_EQ:
        case TK_LOGICAL_NE:
            return 7;
        case '&':
            return 6;
        case '^':
            return 5;
        case '|':
            return 4;
        case TK_LOGICAL_AND:
            return 3;
        case TK_LOGICAL_OR:
            return 2;
        default:
            return 0;
    }
}

Boolean CParser_ParseOperatorName(
    short* token, Boolean allowConversion, Boolean parseConversionType
) {
    HashNameNode* name;

    switch ((tk = lex())) {
        case TK_NEW:
        case TK_DELETE:
            if (lookahead() == '[') {
                lex();
                if (lex() != ']') {
                    CError_Error(ERR_RBRACKET_EXPECTED);
                }
                tk = (tk == TK_NEW) ? TK_NEW_ARRAY : TK_DELETE_ARRAY;
            }
            break;
        case '(':
            if ((tk = lex()) != ')') {
                CError_Error(ERR_ILLEGAL_OPERATOR);
                return FALSE;
            }
            tk = '(';
            break;
        case '[':
            if ((tk = lex()) != ']') {
                CError_Error(ERR_ILLEGAL_OPERATOR);
                return FALSE;
            }
            tk = '[';
            break;
    }

    if ((name = CMangler_OperatorName(tk))) {
        if (token) {
            *token = tk;
        }
        tk = lex();
        tkidentifier = name;
        return TRUE;
    }

    if (allowConversion) {
        if (parseConversionType) {
            DeclInfo declinfo;
            memclrw(&declinfo, sizeof(DeclInfo));
            conversion_type_name(&declinfo);
            tkidentifier = CMangler_ConversionFuncName(declinfo.thetype, declinfo.qual);
        }
        if (token) {
            *token = 0;
        }
        return TRUE;
    } else {
        CError_Error(ERR_ILLEGAL_OPERATOR);
        return FALSE;
    }
}

SInt32 CParser_GetUniqueID(void) {
    return uniqueid++;
}

void CParser_PrintUniqueID(char* buf) {
    SInt32 id;
    char mybuf[16];
    char* ptr;

    ptr = mybuf;
    id = CParser_GetUniqueID();
    while (id) {
        *(ptr++) = '0' + id % 10;
        id = id / 10;
    }

    while (ptr > mybuf) {
        *(buf++) = *(--ptr);
    }

    *buf = 0;
}

void CParser_SetUniqueID(SInt32 id) {
    uniqueid = id;
}

HashNameNode* CParser_GetUniqueName(void) {
    char buf[16];
    buf[0] = '@';
    CParser_PrintUniqueID(buf + 1);
    return GetHashNameNodeExport(buf);
}

HashNameNode* CParser_NameConcat(const char* a, const char* b) {
    char mybuf[256];
    char* buf;
    char* dst;
    int len;

    len = strlen(a) + strlen(b);
    if ((unsigned int)len > (sizeof(mybuf) - 1)) {
        dst = buf = lalloc(len + 1);
    } else {
        dst = buf = mybuf;
    }

    while (*a) {
        *(dst++) = *(a++);
    }
    while (*b) {
        *(dst++) = *(b++);
    }
    *dst = 0;

    return GetHashNameNodeExport(buf);
}

HashNameNode* CParser_AppendUniqueName(char* prefix) {
    char buf[256];
    char* dst;
    int i;

    dst = buf;
    for (i = 0; *prefix && i < 240; i++) {
        *(dst++) = *(prefix++);
    }
    *(dst++) = '$';

    CParser_PrintUniqueID(dst);
    return GetHashNameNodeExport(buf);
}

HashNameNode* CParser_AppendUniqueNameFile(char* prefix) {
    unsigned char filename[256];
    char buf[256];
    char* src;
    char* dst;
    char c;
    int i;
    int j;
    int len;

    dst = buf;
    for (i = 0; *prefix && i < 200; i++) {
        *(dst++) = *(prefix++);
    }
    *(dst++) = '$';

    CParser_PrintUniqueID(dst);

    while (*dst) {
        dst++;
        i++;
    }

    COS_FileGetFSSpecInfo(&cparamblkptr->sourcefile, NULL, NULL, filename);
    src = (char*)&filename[1];
    len = filename[0];
    for (j = 0; j < len && i < 255; j++, i++) {
        c = *(src++);
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9')) {
            c = '_';
        }
        *(dst++) = c;
    }

    dst[0] = 0;
    return GetHashNameNodeExport(buf);
}

static HashNameNode* CParser_GetUnnamedNameSpaceName(void) {
    unsigned char filename[256];
    char buf[256];
    char* src;
    char* dst;
    char c;
    int i;
    int len;

    if (!uniquenamespacename) {
        strcpy(buf, "@unnamed@");
        dst = buf + strlen(buf);

        COS_FileGetFSSpecInfo(&cparamblkptr->sourcefile, NULL, NULL, filename);
        src = (char*)&filename[1];
        len = filename[0];
        for (i = 0; i < len && dst < &buf[254]; i++) {
            c = *(src++);
            if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9')) {
                c = '_';
            }
            *(dst++) = c;
        }

        dst[0] = '@';
        dst[1] = 0;
        uniquenamespacename = GetHashNameNodeExport(buf);
    }

    return uniquenamespacename;
}

Boolean IsTempName(HashNameNode* name) {
    return name == NULL || (name->name[0] == '@') || (name->name[0] == '$');
}

static void CParser_SetCFMFlags(Object* object, DeclInfo* declinfo) {
    if (declinfo && declinfo->exportflags) {
        object->flags |= declinfo->exportflags;
    }

    if (object->datatype == DDATA) {
        if (copts.cfm_export) {
            object->flags |= OBJECT_EXPORT;
        }
        if (copts.cfm_internal) {
            object->flags |= OBJECT_INTERNAL;
        }
        return;
    }
    if (copts.cfm_internal) {
        object->flags |= OBJECT_INTERNAL;
        return;
    }
    if (copts.cfm_import) {
        object->flags |= OBJECT_IMPORT;
    }
    if (copts.cfm_export) {
        object->flags |= OBJECT_EXPORT;
    }
    if (copts.cfm_lib_export) {
        object->flags |= OBJECT_IMPORT | OBJECT_EXPORT;
    }
}

void CParser_UpdateObject(Object* object, DeclInfo* declinfo) {
    if (declinfo && declinfo->section) {
        object->section = ((volatile DeclInfo*)declinfo)->section;
    }
    if (declinfo && object->datatype == DDATA && !object->u.data.reginfo.reg) {
        object->u.data.reginfo = declinfo->reginfo;
    }
    if (declinfo &&
        (object->datatype == DFUNC || object->datatype == DVFUNC) &&
        !object->u.func.interruptflags) {
        object->u.func.interruptflags = declinfo->interruptflags;
    }

    CParser_SetCFMFlags(object, declinfo);
    CodeGen_UpdateObject(object);
}

Object* CParser_NewObject(DeclInfo* declinfo) {
    Object* object = galloc(sizeof(Object));
    memclrw(object, sizeof(Object));
    CParser_SetCFMFlags(object, declinfo);
    object->otype = OT_OBJECT;
    object->access = ACCESSPUBLIC;
    object->section = SECT_DEFAULT;
    return object;
}

Object* CParser_NewLocalDataObject(DeclInfo* declinfo, Boolean add_to_locals) {
    Object* object = lalloc(sizeof(Object));
    memclrw(object, sizeof(Object));
    object->otype = OT_OBJECT;
    object->access = ACCESSPUBLIC;
    object->datatype = DLOCAL;

    if (declinfo) {
        object->type = declinfo->thetype;
        object->name = declinfo->name;
        object->qual = declinfo->qual;
        object->sclass = declinfo->storageclass;
    }

    if (add_to_locals) {
        ObjectList* list = lalloc(sizeof(ObjectList));
        list->object = object;
        list->next = locals;
        locals = list;
    }

    return object;
}

Object* CParser_NewGlobalDataObject(DeclInfo* declinfo) {
    Object* object = galloc(sizeof(Object));
    memclrw(object, sizeof(Object));
    object->otype = OT_OBJECT;
    object->access = ACCESSPUBLIC;
    object->section = SECT_DEFAULT;
    object->datatype = DDATA;
    object->nspace = cscope_current;

    if (declinfo) {
        object->type = declinfo->thetype;
        object->name = declinfo->name;
        object->qual = declinfo->qual;
        object->sclass = declinfo->storageclass;
        if (copts.cplusplus && !declinfo->is_extern_c) {
            object->qual |= Q_MANGLE_NAME;
        }
    }

    CParser_UpdateObject(object, declinfo);
    return object;
}

Object* CParser_NewCompilerDefDataObject(void) {
    Object* object = galloc(sizeof(Object));
    memclrw(object, sizeof(Object));
    object->otype = OT_OBJECT;
    object->access = ACCESSPUBLIC;
    object->section = SECT_DEFAULT;
    object->datatype = DDATA;
    object->nspace = cscope_root;

    return object;
}

Object* CParser_NewFunctionObject(DeclInfo* declinfo) {
    Object* object = galloc(sizeof(Object));
    memclrw(object, sizeof(Object));
    object->otype = OT_OBJECT;
    object->access = ACCESSPUBLIC;
    object->section = SECT_DEFAULT;
    object->datatype = DFUNC;
    object->nspace = cscope_current;

    if (declinfo) {
        object->type = declinfo->thetype;
        object->name = declinfo->name;
        object->qual = declinfo->qual;
        object->sclass = declinfo->storageclass;
        if (copts.cplusplus && !declinfo->is_extern_c) {
            object->qual |= Q_MANGLE_NAME;
        }
    }

    CParser_UpdateObject(object, declinfo);
    return object;
}

Object* CParser_NewCompilerDefFunctionObject(void) {
    Object* object = galloc(sizeof(Object));
    memclrw(object, sizeof(Object));
    object->otype = OT_OBJECT;
    object->access = ACCESSPUBLIC;
    object->section = SECT_DEFAULT;
    object->datatype = DFUNC;
    object->nspace = cscope_root;
    return object;
}

Object* CParser_NewAliasObject(Object* object, SInt32 offset) {
    Object* alias = galloc(sizeof(Object));
    *alias = *object;
    alias->datatype = DALIAS;
    alias->u.alias.object = object;
    alias->u.alias.member = NULL;
    alias->u.alias.offset = offset;
    CScope_AddObject(cscope_current, alias->name, OBJ_BASE(alias));
    return alias;
}

FuncArg* CParser_NewFuncArg(void) {
    FuncArg* arg = galloc(sizeof(FuncArg));
    memclrw(arg, sizeof(FuncArg));
    return arg;
}

Type* atomtype(void) {
    switch (tksize) {
        default:
            CError_FATAL(1145);
        case ATOM_VOID:
            return &stvoid;
        case ATOM_CHAR:
            return TYPE(&stchar);
        case ATOM_WCHAR:
            return TYPE(&stwchar);
        case ATOM_UCHAR:
            return TYPE(&stunsignedchar);
        case ATOM_SHORT:
            return TYPE(&stsignedshort);
        case ATOM_USHORT:
            return TYPE(&stunsignedshort);
        case ATOM_INT:
            return TYPE(&stsignedint);
        case ATOM_UINT:
            return TYPE(&stunsignedint);
        case ATOM_LONG:
            return TYPE(&stsignedlong);
        case ATOM_ULONG:
            return TYPE(&stunsignedlong);
        case ATOM_LONGLONG:
            return TYPE(&stsignedlonglong);
        case ATOM_ULONGLONG:
            return TYPE(&stunsignedlonglong);
        case ATOM_FLOAT:
            return TYPE(&stfloat);
        case ATOM_SHORTDOUBLE:
            return TYPE(&stshortdouble);
        case ATOM_DOUBLE:
            return TYPE(&stdouble);
        case ATOM_LONGDOUBLE:
            return TYPE(&stlongdouble);
    }
}

Object* CParser_FindDeallocationObject(
    Type* type, FuncArg* args, Boolean isArray, Boolean isGlobal, Boolean* includeSize
) {
    NameSpaceObjectList* list;
    NameSpaceObjectList* scan;
    NameSpaceObjectList mylist;
    NameResult pr;
    Boolean first_time;
    Boolean retry_flag;
    Object* obj;
    Type* sizet;

    list = NULL;
    *includeSize = FALSE;
    if (IS_TYPE_CLASS(type) && !isGlobal) {
        HashNameNode* name;
        name = (isArray && copts.array_new_delete) ? dela_fobj->name : delp_fobj->name;
        if (CScope_FindClassMemberObject(TYPE_CLASS(type), &pr, name)) {
            if ((obj = OBJECT(pr.object))) {
                mylist.next = NULL;
                mylist.object = pr.object;
                list = &mylist;
            } else {
                CError_ASSERT(1202, pr.objects);
                list = pr.objects;
            }
        } else if (TYPE_CLASS(type)->flags & CLASS_HANDLEOBJECT) {
            CError_ASSERT(1210, !args && !isArray);
            return delh_func;
        }
    }

    first_time = TRUE;
    retry_flag = isArray;
    while (1) {
        if (!args) {
            for (scan = list; scan; scan = scan->next) {
                obj = OBJECT(scan->object);
                if (obj->otype == OT_OBJECT &&
                    IS_TYPE_FUNC(obj->type) &&
                    TYPE_FUNC(obj->type)->args &&
                    !TYPE_FUNC(obj->type)->args->next &&
                    is_typesame(TYPE_FUNC(obj->type)->args->type, TYPE(&void_ptr))) {
                    return obj;
                }
            }

            CError_ASSERT(1231, first_time);

            sizet = CABI_GetSizeTType();
            for (scan = list; scan; scan = scan->next) {
                obj = OBJECT(scan->object);
                if (obj->otype == OT_OBJECT &&
                    IS_TYPE_FUNC(obj->type) &&
                    TYPE_FUNC(obj->type)->args &&
                    TYPE_FUNC(obj->type)->args->next &&
                    !TYPE_FUNC(obj->type)->args->next->next &&
                    is_typesame(TYPE_FUNC(obj->type)->args->type, TYPE(&void_ptr)) &&
                    TYPE_FUNC(obj->type)->args->next->type == sizet) {
                    *includeSize = TRUE;
                    return obj;
                }
            }
        } else {
            for (scan = list; scan; scan = scan->next) {
                obj = OBJECT(scan->object);
                if (obj->otype == OT_OBJECT &&
                    IS_TYPE_FUNC(obj->type) &&
                    TYPE_FUNC(obj->type)->args &&
                    TYPE_FUNC(obj->type)->args->next &&
                    is_arglistsame(TYPE_FUNC(obj->type)->args->next, args)) {
                    *includeSize = TRUE;
                    return obj;
                }
            }

            if (!first_time) {
                return NULL;
            }
        }

        if (list) {
            CError_Warning(ERR_NO_MATCHING_DEALLOCATION_FUNCTION, type, 0);
        }

        list = (retry_flag && copts.array_new_delete) ? &dela_fobj->first : &delp_fobj->first;
        first_time = FALSE;
    }
}

static Boolean oldstylecompatible(FuncArg* arg) {
    if (copts.ignore_oldstyle) {
        return TRUE;
    }

    while (arg) {
        if (arg == &elipsis) {
            return FALSE;
        }
        switch (arg->type->type) {
            case TYPEINT:
                if (TYPE_INTEGRAL(arg->type)->integral < IT_INT) {
                    return FALSE;
                }
                break;
            case TYPEFLOAT:
                if (TYPE_INTEGRAL(arg->type)->integral < IT_DOUBLE) {
                    return FALSE;
                }
                break;
            default:
                break;
        }
        arg = arg->next;
    }

    return TRUE;
}

static Boolean is_arglistequal(FuncArg* a, FuncArg* b) {
    if (a == &oldstyle) {
        if (b == &oldstyle) {
            return TRUE;
        } else {
            return oldstylecompatible(b);
        }
    } else {
        if (b == &oldstyle) {
            return oldstylecompatible(a);
        }
    }

    while (1) {
        if (a == &elipsis || b == &elipsis) {
            return TRUE;
        }

        if (!a) {
            if (!b) {
                return TRUE;
            }
            return FALSE;
        }
        if (!b) {
            return FALSE;
        }

        if (copts.mpwc_relax && !copts.cplusplus) {
            if (!is_typeequal(a->type, b->type)) {
                return FALSE;
            }
        } else {
            if (!is_typesame(a->type, b->type)) {
                return FALSE;
            }
        }

        if (a->type->type == TYPEPOINTER && a->qual != b->qual) {
            return FALSE;
        }

        a = a->next;
        b = b->next;
    }
}

short is_memberpointerequal(Type* a, Type* b) {
    FuncArg* arg_a;
    FuncArg* arg_b;

    if (a->type != b->type) {
        return FALSE;
    }
    if (!IS_TYPE_FUNC(a)) {
        return is_typeequal(a, b);
    }

    if (!is_typesame(TYPE_FUNC(a)->functype, TYPE_FUNC(b)->functype)) {
        return FALSE;
    }

    if ((TYPE_FUNC(a)->flags & FUNC_CALL_CONV_MASK) !=
        (TYPE_FUNC(b)->flags & FUNC_CALL_CONV_MASK)) {
        return FALSE;
    }

    CError_ASSERT(1345, arg_a = TYPE_FUNC(a)->args);
    CError_ASSERT(1346, arg_b = TYPE_FUNC(b)->args);

    if (TYPE_FUNC(a)->flags & FUNC_PTM) {
        CError_ASSERT(1351, arg_a = arg_a->next);
    }

    if (TYPE_FUNC(b)->flags & FUNC_PTM) {
        CError_ASSERT(1355, arg_b = arg_b->next);
    }

    if (arg_a->qual != arg_b->qual) {
        return FALSE;
    }

    arg_a = arg_a->next;
    arg_b = arg_b->next;
    return is_arglistsame(arg_a, arg_b);
}

short is_typeequal(Type* a, Type* b) {
restart:
    if (a->type != b->type) {
        return FALSE;
    }
    switch (a->type) {
        case TYPEVOID:
            return TRUE;
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEENUM:
        case TYPECLASS:
            return a == b;
        case TYPESTRUCT:
            return a == b;
        case TYPEPOINTER:
            if (TYPE_POINTER(a)->target == &stvoid || TYPE_POINTER(b)->target == &stvoid) {
                return TRUE;
            }
            a = TYPE_POINTER(a)->target;
            b = TYPE_POINTER(b)->target;
            if (copts.mpwc_relax && !copts.cplusplus) {
                return TRUE;
            }
            goto restart;
        case TYPEMEMBERPOINTER:
            if (TYPE_MEMBER_POINTER(a)->ty2 != TYPE_MEMBER_POINTER(b)->ty2) {
                return FALSE;
            }
            return is_memberpointerequal(TYPE_MEMBER_POINTER(a)->ty1, TYPE_MEMBER_POINTER(b)->ty1);
        case TYPEARRAY:
            if (a->size != 0 && b->size != 0 && a->size != b->size) {
                return FALSE;
            }
            a = TYPE_POINTER(a)->target;
            b = TYPE_POINTER(b)->target;
            goto restart;
        case TYPEFUNC:
            if (copts.cplusplus || !copts.cpp_extensions) {
                if (copts.mpwc_relax && !copts.cplusplus) {
                    if (!is_typeequal(TYPE_FUNC(a)->functype, TYPE_FUNC(b)->functype)) {
                        return FALSE;
                    }
                } else {
                    if (!is_typesame(TYPE_FUNC(a)->functype, TYPE_FUNC(b)->functype)) {
                        return FALSE;
                    }
                }
                if ((TYPE_FUNC(a)->flags & FUNC_CALL_CONV_MASK) !=
                    (TYPE_FUNC(b)->flags & FUNC_CALL_CONV_MASK)) {
                    return FALSE;
                }
            }
            return is_arglistequal(TYPE_FUNC(a)->args, TYPE_FUNC(b)->args);
        case TYPETEMPLATE:
            return CTemplTool_TemplDepTypeCompare(TYPE_TEMPLATE(a), TYPE_TEMPLATE(b));
        default:
            CError_FATAL(1441);
            return FALSE;
    }
}

short iscpp_typeequal(Type* a, Type* b) {
restart:
    if (a->type != b->type) {
        return 0;
    }
    switch (a->type) {
        case TYPEVOID:
            return 1;
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEENUM:
        case TYPECLASS:
            return a == b;
        case TYPESTRUCT:
            return a == b;
        case TYPEPOINTER:
            if (TYPE_POINTER(b)->target == &stvoid) {
                if (TYPE_POINTER(a)->target == &stvoid) {
                    return 1;
                } else {
                    return -1;
                }
            }
            if (TYPE_POINTER(a)->target == &stvoid) {
                illegalimplicitconversion = TRUE;
                return 0;
            }
            a = TYPE_POINTER(a)->target;
            b = TYPE_POINTER(b)->target;
            goto restart;
        case TYPEMEMBERPOINTER:
            if (TYPE_MEMBER_POINTER(a)->ty2 != TYPE_MEMBER_POINTER(b)->ty2) {
                return 0;
            }
            return is_memberpointerequal(TYPE_MEMBER_POINTER(a)->ty1, TYPE_MEMBER_POINTER(b)->ty1);
        case TYPEARRAY:
            if (a->size != 0 && b->size != 0 && a->size != b->size) {
                return 0;
            }
            a = TYPE_POINTER(a)->target;
            b = TYPE_POINTER(b)->target;
            goto restart;
        case TYPEFUNC:
            if (!is_typesame(TYPE_FUNC(a)->functype, TYPE_FUNC(b)->functype)) {
                return 0;
            }
            if ((TYPE_FUNC(a)->flags & FUNC_CALL_CONV_MASK) !=
                (TYPE_FUNC(b)->flags & FUNC_CALL_CONV_MASK)) {
                return 0;
            }
            return is_arglistequal(TYPE_FUNC(a)->args, TYPE_FUNC(b)->args);
        case TYPETEMPLATE:
            return CTemplTool_TemplDepTypeCompare(TYPE_TEMPLATE(a), TYPE_TEMPLATE(b));
        default:
            CError_FATAL(1500);
            return 0;
    }
}

short CParser_CompareArgLists(FuncArg* a, FuncArg* b) {
    Boolean old_argmatch_used;

    old_argmatch_used = FALSE;
    if (a == &oldstyle) {
        if (b == &oldstyle) {
            return 1;
        } else {
            return 2;
        }
    }
    if (b == &oldstyle) {
        return 2;
    }

    while (1) {
        if (a == &elipsis) {
            if (b != &elipsis) {
                return 0;
            }
            break;
        }
        if (b == &elipsis) {
            return 0;
        }

        if (a == NULL) {
            if (b) {
                return 0;
            }
            break;
        }
        if (b == NULL) {
            return 0;
        }

        if (a->type->type == TYPEPOINTER) {
            if (IS_TYPEPOINTER_REFERENCE(TYPE_POINTER(a->type))) {
                if (IS_TYPE_REFERENCE(b->type)) {
                    if (!is_typesame(
                            TYPE_POINTER(a->type)->target, TYPE_POINTER(b->type)->target
                        )) {
                        return 0;
                    }
                    if ((a->qual & (Q_CONST | Q_VOLATILE)) != (b->qual & (Q_CONST | Q_VOLATILE))) {
                        return 0;
                    }
                } else {
                    if (!copts.old_argmatch) {
                        return 0;
                    }
                    if (!is_typesame(TYPE_POINTER(a->type)->target, b->type)) {
                        return 0;
                    }
                    if (b->type->type == TYPEPOINTER &&
                        (a->qual & (Q_CONST | Q_VOLATILE)) != (b->qual & (Q_CONST | Q_VOLATILE))) {
                        return 0;
                    }
                    old_argmatch_used = TRUE;
                }
            } else {
                if (b->type->type == TYPEPOINTER) {
                    if (IS_TYPEPOINTER_REFERENCE(TYPE_POINTER(b->type))) {
                        if (!copts.old_argmatch) {
                            return 0;
                        }
                        if (!is_typesame(a->type, TYPE_POINTER(b->type)->target)) {
                            return 0;
                        }
                        if (a->type->type == TYPEPOINTER &&
                            (a->qual & (Q_CONST | Q_VOLATILE)) !=
                                (b->qual & (Q_CONST | Q_VOLATILE))) {
                            return 0;
                        }
                        old_argmatch_used = TRUE;
                    } else {
                        if (!is_typesame(
                                TYPE_POINTER(a->type)->target, TYPE_POINTER(b->type)->target
                            )) {
                            return 0;
                        }
                        if ((a->qual & (Q_CONST | Q_VOLATILE)) !=
                            (b->qual & (Q_CONST | Q_VOLATILE))) {
                            return 0;
                        }
                    }
                } else {
                    return 0;
                }
            }
        } else {
            if (b->type->type == TYPEPOINTER) {
                if (IS_TYPEPOINTER_REFERENCE(TYPE_POINTER(b->type))) {
                    if (!copts.old_argmatch) {
                        return 0;
                    }
                    if (!is_typesame(a->type, TYPE_POINTER(b->type)->target)) {
                        return 0;
                    }
                    old_argmatch_used = TRUE;
                } else {
                    return 0;
                }
            } else {
                if (!is_typesame(a->type, b->type)) {
                    return 0;
                }
            }
        }

        a = a->next;
        b = b->next;
    }

    if (old_argmatch_used) {
        return 2;
    }
    return 1;
}

Boolean is_arglistsame(FuncArg* a, FuncArg* b) {
    if (a == &oldstyle) {
        if (b == &oldstyle) {
            return TRUE;
        } else {
            return oldstylecompatible(b);
        }
    } else {
        if (b == &oldstyle) {
            return oldstylecompatible(a);
        }
    }

    while (1) {
        if (!a || !b || a == &elipsis || b == &elipsis) {
            return a == b;
        }

        if (a->type->type == TYPEPOINTER) {
            if (b->type->type != TYPEPOINTER ||
                (a->qual & (Q_CONST | Q_VOLATILE)) != (b->qual & (Q_CONST | Q_VOLATILE)) ||
                !is_typesame(TYPE_POINTER(a->type)->target, TYPE_POINTER(b->type)->target) ||
                IS_TYPEPOINTER_REFERENCE(TYPE_POINTER(a->type)) !=
                    IS_TYPEPOINTER_REFERENCE(TYPE_POINTER(b->type))) {
                return FALSE;
            }
        } else {
            if (!is_typesame(a->type, b->type)) {
                return FALSE;
            }
        }

        a = a->next;
        b = b->next;
    }
}

short is_typesame(Type* a, Type* b) {
restart:
    if (a->type != b->type) {
        if (IS_TYPE_TEMPLATE(a) && IS_TYPE_CLASS(b) && (TYPE_CLASS(b)->flags & CLASS_IS_TEMPL)) {
            return CTemplTool_IsSameTemplateType(b, a);
        }
        if (IS_TYPE_TEMPLATE(b) && IS_TYPE_CLASS(a) && (TYPE_CLASS(a)->flags & CLASS_IS_TEMPL)) {
            return CTemplTool_IsSameTemplateType(a, b);
        }
        return FALSE;
    }

    switch (a->type) {
        case TYPEVOID:
            return TRUE;
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEENUM:
        case TYPECLASS:
            return a == b;
        case TYPETEMPLATE:
            return CTemplTool_TemplDepTypeCompare(TYPE_TEMPLATE(a), TYPE_TEMPLATE(b));
        case TYPESTRUCT:
            return a == b;
        case TYPEPOINTER:
            if ((TYPE_POINTER(a)->qual & (Q_RESTRICT | Q_REFERENCE | Q_CONST | Q_VOLATILE)) !=
                (TYPE_POINTER(b)->qual & (Q_RESTRICT | Q_REFERENCE | Q_CONST | Q_VOLATILE))) {
                return FALSE;
            }
            a = TYPE_POINTER(a)->target;
            b = TYPE_POINTER(b)->target;
            goto restart;
        case TYPEMEMBERPOINTER:
            if (!is_typesame(TYPE_MEMBER_POINTER(a)->ty2, TYPE_MEMBER_POINTER(b)->ty2)) {
                return FALSE;
            }
            if ((TYPE_MEMBER_POINTER(a)->qual &
                 (Q_RESTRICT | Q_REFERENCE | Q_CONST | Q_VOLATILE)) !=
                (TYPE_MEMBER_POINTER(b)->qual &
                 (Q_RESTRICT | Q_REFERENCE | Q_CONST | Q_VOLATILE))) {
                return FALSE;
            }
            return is_memberpointerequal(TYPE_MEMBER_POINTER(a)->ty1, TYPE_MEMBER_POINTER(b)->ty1);
        case TYPEARRAY:
            if (a->size != b->size) {
                return FALSE;
            }
            a = TYPE_POINTER(a)->target;
            b = TYPE_POINTER(b)->target;
            goto restart;
        case TYPEFUNC:
            if (!is_typesame(TYPE_FUNC(a)->functype, TYPE_FUNC(b)->functype)) {
                return FALSE;
            }
            if (TYPE_FUNC(a)->qual != TYPE_FUNC(b)->qual) {
                return FALSE;
            }
            if ((TYPE_FUNC(a)->flags & FUNC_CALL_CONV_MASK) !=
                (TYPE_FUNC(b)->flags & FUNC_CALL_CONV_MASK)) {
                return FALSE;
            }
            return is_arglistsame(TYPE_FUNC(a)->args, TYPE_FUNC(b)->args);
        default:
            CError_FATAL(1709);
            return FALSE;
    }
}

Type* CParser_GetBoolType(void) {
    if (copts.cplusplus && copts.booltruefalse) {
        return TYPE(&stbool);
    } else {
        return TYPE(&stsignedint);
    }
}

Type* CParser_GetWCharType(void) {
    if (copts.cplusplus && copts.wchar_type) {
        return TYPE(&stwchar);
    } else {
        return TYPE(&stunsignedshort);
    }
}

short CParser_GetOperator(ENodeType t) {
    switch (t) {
        default:
            CError_FATAL(1748);
        case EMONMIN:
            return '-';
        case EBINNOT:
            return '~';
        case ELOGNOT:
            return '!';
        case EADD:
            return '+';
        case ESUB:
            return '-';
        case EMUL:
            return '*';
        case EDIV:
            return '/';
        case EMODULO:
            return '%';
        case EAND:
            return '&';
        case EXOR:
            return '^';
        case EOR:
            return '|';
        case ESHL:
            return TK_SHL;
        case ESHR:
            return TK_SHR;
        case ELESS:
            return '<';
        case EGREATER:
            return '>';
        case ELESSEQU:
            return TK_LESS_EQUAL;
        case EGREATEREQU:
            return TK_GREATER_EQUAL;
        case EEQU:
            return TK_LOGICAL_EQ;
        case ENOTEQU:
            return TK_LOGICAL_NE;
    }
}

Boolean CParser_IsMoreCVQualified(UInt32 a, UInt32 b) {
    if ((a & Q_CONST) && !(b & Q_CONST)) {
        return ((a & Q_VOLATILE) || (b & Q_VOLATILE) == 0);
    } else if ((a & Q_VOLATILE) && !(b & Q_VOLATILE)) {
        return ((a & Q_CONST) || (b & Q_CONST) == 0);
    }
    return FALSE;
}

Boolean CParser_IsSameOrMoreCVQualified(UInt32 a, UInt32 b) {
    if ((a & (Q_CONST | Q_VOLATILE)) == (b & (Q_CONST | Q_VOLATILE))) {
        return TRUE;
    }

    if ((a & Q_CONST) && !(b & Q_CONST)) {
        return ((a & Q_VOLATILE) || (b & Q_VOLATILE) == 0);
    } else if ((a & Q_VOLATILE) && !(b & Q_VOLATILE)) {
        return ((a & Q_CONST) || (b & Q_CONST) == 0);
    }
    return FALSE;
}

Boolean is_unsigned(Type* type) {
    if (IS_TYPE_ENUM(type)) {
        type = TYPE_ENUM(type)->enumtype;
    }

    return CMach_IsUnsignedType(type);
}

StructMember* ismember(TypeStruct* tstruct, HashNameNode* name) {
    StructMember* member;

    for (member = tstruct->members; member; member = member->next) {
        if (member->name == name) {
            return member;
        }
    }

    return NULL;
}

void appendmember(TypeStruct* tstruct, StructMember* member) {
    StructMember* last;

    if (!tstruct->members) {
        tstruct->members = member;
        return;
    }

    for (last = tstruct->members; last->next; last = last->next) {}
    last->next = member;
}

static void CParser_InsertTryBlock(ParserTryBlock* block) {
    block->cscope_current = cscope_current;
    block->cscope_currentclass = cscope_currentclass;
    block->cscope_currentfunc = cscope_currentfunc;
    block->ctempl_curinstance = ctempl_curinstance;
    block->cerror_locktoken = cerror_locktoken;
    block->cscope_is_member_func = cscope_is_member_func;
    block->next = trychain;
    trychain = block;
}

static void CParser_RemoveTryBlock(ParserTryBlock* block) {
    cscope_current = block->cscope_current;
    cscope_currentclass = block->cscope_currentclass;
    cscope_currentfunc = block->cscope_currentfunc;
    ctempl_curinstance = block->ctempl_curinstance;
    cerror_locktoken = block->cerror_locktoken;
    cscope_is_member_func = block->cscope_is_member_func;
    trychain = block->next;
}

static Boolean TryIsDeclaration(
    Boolean scanDeclarator, Boolean requireName, Boolean isNewExpression, short token
) {
    Boolean result;
    struct ParserTryBlock tryblock;
    DeclInfo declinfo;

    switch (tk) {
        case TK_IDENTIFIER:
        case TK_COLON_COLON:
            break;
        case TK_VOID:
        case TK_CHAR:
        case TK_SHORT:
        case TK_INT:
        case TK_LONG:
        case TK_FLOAT:
        case TK_DOUBLE:
        case TK_SIGNED:
        case TK_UNSIGNED:
        case TK_CUSTOM_TYPE_113:
        case TK_CUSTOM_TYPE_114:
        case TK_UU_VEC2X32FLOAT_UU:
        case TK_CUSTOM_TYPE_116:
        case TK_CUSTOM_TYPE_117:
        case TK_CUSTOM_TYPE_118:
        case TK_CUSTOM_TYPE_119:
        case TK_CUSTOM_TYPE_11A:
        case TK_BOOL:
        case TK_WCHAR_T:
            if (lookahead() != '(') {
                return TRUE;
            }
            break;
        default:
            return TRUE;
    }

    result = FALSE;
    CParser_InsertTryBlock(&tryblock);
    if (setjmp(tryblock.jmpbuf) == 0) {
        memclrw(&declinfo, sizeof(DeclInfo));
        CParser_GetDeclSpecs(&declinfo, FALSE);
        if (!(IS_TYPE_TEMPLATE(declinfo.thetype) &&
              TYPE_TEMPLATE(declinfo.thetype)->dtype == TEMPLDEP_QUALNAME &&
              !declinfo.hasTypename &&
              !declinfo.isType)) {
            if (scanDeclarator) {
                declinfo.isNewExpression = isNewExpression;
                scandeclarator(&declinfo);
                if (!(requireName && declinfo.name)) {
                    if (!token) {
                        if (tk == ';' ||
                            tk == ',' ||
                            tk == '=' ||
                            tk == '(' ||
                            tk == ')' ||
                            tk == '>') {
                            result = TRUE;
                        }
                    } else {
                        result = (tk == token);
                    }
                }
            } else {
                result = TRUE;
            }
        }
    }

    CParser_RemoveTryBlock(&tryblock);
    return result;
}

Boolean isdeclaration(UInt8 scanDeclarator, UInt8 requireName, UInt8 isNewExpression, short token) {
    SInt32 state;

    if (!((int)tk >= TK_AUTO && (int)tk <= TK_BYREF) &&
        (int)tk != TK_COLON_COLON &&
        !(tk == TK_IDENTIFIER && CScope_PossibleTypeName(tkidentifier))) {
        if (!(tk == TK_IDENTIFIER &&
              copts.altivec_model &&
              !strcmp(tkidentifier->name, "vector"))) {
            return FALSE;
        }
    } else {
        if (!copts.cplusplus) {
            return TRUE;
        }
    }

    CPrep_TokenStreamGetState(&state);
    if (TryIsDeclaration(scanDeclarator, requireName, isNewExpression, token)) {
        CPrep_TokenStreamSetCurState(&state);
        return TRUE;
    } else {
        CPrep_TokenStreamSetCurState(&state);
        return FALSE;
    }
}

Boolean islookaheaddeclaration(void) {
    SInt32 state;

    CPrep_TokenStreamGetState(&state);
    tk = lex();
    if (!((int)tk >= TK_AUTO && (int)tk <= TK_BYREF) &&
        (int)tk != TK_COLON_COLON &&
        !(tk == TK_IDENTIFIER && CScope_PossibleTypeName(tkidentifier))) {
        if (!(tk == TK_IDENTIFIER &&
              copts.altivec_model &&
              !strcmp(tkidentifier->name, "vector"))) {
            CPrep_TokenStreamSetCurState(&state);
            return FALSE;
        }
    } else {
        if (!copts.cplusplus) {
            CPrep_TokenStreamSetCurState(&state);
            return TRUE;
        }
    }

    if (TryIsDeclaration(TRUE, TRUE, FALSE, ')')) {
        CPrep_TokenStreamSetCurState(&state);
        return TRUE;
    } else {
        CPrep_TokenStreamSetCurState(&state);
        return FALSE;
    }
}

Type* CParser_ParseTypeID(UInt32* qual, Boolean* isTemplateName) {
    struct ParserTryBlock tryblock;
    SInt32 state;
    DeclInfo di;

    memclrw(&di, sizeof(DeclInfo));
    CPrep_TokenStreamGetState(&state);
    CParser_InsertTryBlock(&tryblock);

    if (setjmp(tryblock.jmpbuf) == 0) {
        if (copts.cplusplus) {
            di.isTypeID = TRUE;
        }

        if (isTemplateName) {
            di.allowTemplateName = TRUE;
            *isTemplateName = FALSE;
        }

        CParser_GetDeclSpecs(&di, FALSE);
        if (di.isTemplateName &&
            IS_TYPE_CLASS(di.thetype) &&
            (TYPE_CLASS(di.thetype)->flags & CLASS_IS_TEMPL)) {
            CParser_RemoveTryBlock(&tryblock);
            *qual = di.qual;
            *isTemplateName = TRUE;
            return di.thetype;
        }

        if (isTemplateName &&
            IS_TYPE_TEMPLATE(di.thetype) &&
            TYPE_TEMPLATE(di.thetype)->dtype == TEMPLDEP_ARGUMENT &&
            TYPE_TEMPLATE(di.thetype)->u.pid.type == TPT_TEMPLATE) {
            CParser_RemoveTryBlock(&tryblock);
            *qual = di.qual;
            *isTemplateName = TRUE;
            return di.thetype;
        }

        scandeclarator(&di);
        if (!di.name) {
            CParser_RemoveTryBlock(&tryblock);
            *qual = di.qual;
            return di.thetype;
        }
    }

    CPrep_TokenStreamSetCurState(&state);
    CParser_RemoveTryBlock(&tryblock);
    return NULL;
}

Boolean CParser_TryFuncDecl(void) {
    Boolean result;
    SInt32 state;
    DeclInfo di;
    struct ParserTryBlock tryblock;

    result = FALSE;
    CPrep_TokenStreamGetState(&state);
    CParser_InsertTryBlock(&tryblock);

    if (setjmp(tryblock.jmpbuf) == 0) {
        memclrw(&di, sizeof(DeclInfo));
        di.thetype = &stvoid;
        scandeclarator(&di);

        if (IS_TYPE_FUNC(di.thetype)) {
            result = TRUE;
        }
    }

    CParser_RemoveTryBlock(&tryblock);
    CPrep_TokenStreamSetCurState(&state);
    return result;
}

Boolean CParser_TryParamList(Boolean stopAtComma) {
    struct ParserTryBlock tryblock;
    SInt32 state;
    Boolean result;

    result = FALSE;
    CPrep_TokenStreamGetState(&state);

    switch ((tk = lex())) {
        case ')':
        case TK_ELLIPSIS:
            result = TRUE;
            break;
        default:
            CParser_InsertTryBlock(&tryblock);
            if (setjmp(tryblock.jmpbuf) == 0) {
                if (CFunc_ParseFakeArgList(stopAtComma) || tk == ')') {
                    result = TRUE;
                }
            }
            CParser_RemoveTryBlock(&tryblock);
            break;
    }

    CPrep_TokenStreamSetCurState(&state);
    return result;
}

Type* CParser_RemoveTopMostQualifiers(Type* type, UInt32* qual) {
    switch (type->type) {
        case TYPEARRAY:
            TYPE_POINTER(type)->target =
                CParser_RemoveTopMostQualifiers(TYPE_POINTER(type)->target, qual);
            return type;
        case TYPEPOINTER:
            if (TYPE_POINTER(type)->qual & Q_CONST) {
                TypePointer* newtype = galloc(sizeof(TypePointer));
                *newtype = *TYPE_POINTER(type);
                newtype->qual = 0;
                return TYPE(newtype);
            }
            return type;
        case TYPEMEMBERPOINTER:
            if (TYPE_MEMBER_POINTER(type)->qual & Q_CONST) {
                TypeMemberPointer* newtype = galloc(sizeof(TypeMemberPointer));
                *newtype = *TYPE_MEMBER_POINTER(type);
                newtype->qual = 0;
                return TYPE(newtype);
            }
            return type;
        default:
            *qual = 0;
            return type;
    }
}

UInt32 CParser_GetTypeQualifiers(Type* type, UInt32 qual) {
    while (IS_TYPE_ARRAY(type)) {
        type = TYPE_POINTER(type)->target;
    }

    switch (type->type) {
        case TYPEPOINTER:
            qual = TYPE_POINTER(type)->qual;
            break;
        case TYPEMEMBERPOINTER:
            qual = TYPE_MEMBER_POINTER(type)->qual;
            break;
        default:
            break;
    }

    return qual;
}

UInt32 CParser_GetCVTypeQualifiers(Type* type, UInt32 qual) {
    while (IS_TYPE_ARRAY(type)) {
        type = TYPE_POINTER(type)->target;
    }

    switch (type->type) {
        case TYPEPOINTER:
            qual = TYPE_POINTER(type)->qual;
            break;
        case TYPEMEMBERPOINTER:
            qual = TYPE_MEMBER_POINTER(type)->qual;
            break;
        default:
            break;
    }

    return qual & (Q_CONST | Q_VOLATILE);
}

#if VERSION >= VERSION_GC_2_5
static Boolean CParser_ClassHasMutableMember(TypeClass* tclass) {
    ClassList* base;
    ObjMemberVar* ivar;
    Type* type;
    UInt32 qual;

    for (base = tclass->bases; base; base = base->next) {
        if (CParser_ClassHasMutableMember(base->base)) {
            return TRUE;
        }
    }

    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
        type = ivar->type;
        qual = ivar->qual;
        while (type->type == TYPEARRAY) {
            type = TYPE_POINTER(type)->target;
        }
        switch (type->type) {
            case TYPEPOINTER:
                qual = TYPE_POINTER(type)->qual;
                break;
            case TYPEMEMBERPOINTER:
                qual = TYPE_MEMBER_POINTER(type)->qual;
                break;
            default:
                break;
        }
        if (qual & Q_MUTABLE) {
            return TRUE;
        }
        if (type->type == TYPECLASS && CParser_ClassHasMutableMember(TYPE_CLASS(type))) {
            return TRUE;
        }
    }

    return FALSE;
}

Boolean CParser_IsReadOnly(Type* type, UInt32 qual) {
    int result;
    while (type->type == TYPEARRAY) {
        type = TYPE_POINTER(type)->target;
    }
    switch (type->type) {
        case TYPEPOINTER:
            qual = TYPE_POINTER(type)->qual;
            break;
        case TYPEMEMBERPOINTER:
            qual = TYPE_MEMBER_POINTER(type)->qual;
            break;
        default:
            break;
    }
    if (!(qual & Q_CONST)) {
        return FALSE;
    }
    result = TRUE;
    if (type->type == TYPECLASS && (!CParser_ClassHasMutableMember(TYPE_CLASS(type))) == FALSE) {
        result = FALSE;
    }
    return result;
}
#endif

Boolean CParser_IsConst(Type* type, UInt32 qual) {
    while (IS_TYPE_ARRAY(type)) {
        type = TYPE_POINTER(type)->target;
    }

    switch (type->type) {
        case TYPEPOINTER:
            qual = TYPE_POINTER(type)->qual;
            break;
        case TYPEMEMBERPOINTER:
            qual = TYPE_MEMBER_POINTER(type)->qual;
            break;
        default:
            break;
    }

    return (qual & Q_CONST) != 0;
}

Boolean CParser_IsVolatile(Type* type, UInt32 qual) {
    while (IS_TYPE_ARRAY(type)) {
        type = TYPE_POINTER(type)->target;
    }

    switch (type->type) {
        case TYPEPOINTER:
            qual = TYPE_POINTER(type)->qual;
            break;
        case TYPEMEMBERPOINTER:
            qual = TYPE_MEMBER_POINTER(type)->qual;
            break;
        default:
            break;
    }

    return (qual & Q_VOLATILE) != 0;
}

Boolean is_const_object(Object* obj) {
    return CParser_IsConst(obj->type, obj->qual);
}

Boolean is_volatile_object(Object* obj) {
    return CParser_IsVolatile(obj->type, obj->qual);
}

Boolean CParserIsConstExpr(ENode* expr) {
    return CParser_IsConst(expr->rtype, expr->flags & ENODE_FLAG_QUALS);
}

Boolean CParserIsVolatileExpr(ENode* expr) {
    return CParser_IsVolatile(expr->rtype, expr->flags & ENODE_FLAG_QUALS);
}

Boolean CParser_HasInternalLinkage(const Object* obj) {
    NameSpace* nspace;

    for (nspace = obj->nspace; nspace; nspace = nspace->parent) {
        if (nspace->is_unnamed) {
            return TRUE;
        }
    }

    if (obj->datatype == DLOCAL) {
        return TRUE;
    }
    if (obj->qual & (Q_IMPLICIT_WEAK | Q_WEAK)) {
        return FALSE;
    }
    if (obj->sclass == TK_STATIC) {
        return TRUE;
    }

    if (obj->qual & Q_INLINE) {
        ((Object*)obj)->qual |= Q_IMPLICIT_WEAK;
    }
    return FALSE;
}

Boolean CParser_HasInternalLinkage2(const Object* obj) {
    if (obj->datatype == DLOCAL) {
        return TRUE;
    }
    if (obj->qual & (Q_IMPLICIT_WEAK | Q_WEAK)) {
        return FALSE;
    }
    if (obj->sclass == TK_STATIC) {
        return TRUE;
    }

    if (obj->qual & Q_INLINE) {
        ((Object*)obj)->qual |= Q_IMPLICIT_WEAK;
    }
    return FALSE;
}

Boolean CParser_IsVirtualFunction(Object* obj, TypeClass** tclass, SInt32* index) {
    if (obj->datatype == DVFUNC) {
        *tclass = TYPE_METHOD(obj->type)->theclass;
        *index = TYPE_METHOD(obj->type)->vtbl_index;
        return TRUE;
    }

    return FALSE;
}

Boolean is_pascal_object(Object* obj) {
    return IS_TYPE_FUNC(obj->type) && (TYPE_FUNC(obj->type)->flags & FUNC_PASCAL);
}

Boolean is_cfm_type(Type* type) {
    return FALSE;
}

Boolean CParser_IsVTableObject(Object* obj) {
    return obj->datatype == DDATA &&
        obj->nspace &&
        obj->nspace->theclass &&
        obj->nspace->theclass->vtable &&
        obj->nspace->theclass->vtable->object == obj;
}

static Type* getthetype(short token, short size, short signedness) {
    switch (token) {
        case 0:
        case TK_INT:
            if (signedness == 1) {
                switch (size) {
                    case 1:
                        return TYPE(&stunsignedshort);
                    case 2:
                        return TYPE(&stunsignedlong);
                    case 3:
                        return TYPE(&stunsignedlonglong);
                    default:
                        return TYPE(&stunsignedint);
                }
            } else {
                switch (size) {
                    case 1:
                        return TYPE(&stsignedshort);
                    case 2:
                        return TYPE(&stsignedlong);
                    case 3:
                        return TYPE(&stsignedlonglong);
                    default:
                        return TYPE(&stsignedint);
                }
            }
        case TK_BOOL:
            return TYPE(&stbool);
        case TK_WCHAR_T:
            return TYPE(&stwchar);
        case TK_CHAR:
            switch (signedness) {
                case 1:
                    return TYPE(&stunsignedchar);
                default:
                    return TYPE(&stchar);
                case -1:
                    return TYPE(&stsignedchar);
            }
        case TK_DOUBLE:
            switch (size) {
                case 1:
                    return TYPE(&stshortdouble);
                case 2:
                    return TYPE(&stlongdouble);
                default:
                    return TYPE(&stdouble);
            }
        case TK_FLOAT:
            return TYPE(&stfloat);
        case TK_VOID:
            return TYPE(&stvoid);
        case TK_CUSTOM_TYPE_113:
        case TK_CUSTOM_TYPE_114:
        case TK_UU_VEC2X32FLOAT_UU:
        case TK_CUSTOM_TYPE_116:
        case TK_CUSTOM_TYPE_117:
        case TK_CUSTOM_TYPE_118:
        case TK_CUSTOM_TYPE_119:
        case TK_CUSTOM_TYPE_11A:
            return CMach_GetCustomType(token, size, signedness);
        default:
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            return TYPE(&stvoid);
    }
}

void TypedefDeclInfo(DeclInfo* declinfo, Type* type, UInt32 qual) {
    if (type->type == TYPEPOINTER) {
        if (IS_TYPEPOINTER_REFERENCE(TYPE_POINTER(type))) {
            declinfo->thetype = type;
            declinfo->qual &= ~(Q_RESTRICT | Q_REFERENCE | Q_VOLATILE | Q_CONST);
            declinfo->qual |= qual;
            return;
        }

        declinfo->thetype = galloc(sizeof(TypePointer));
        *TYPE_POINTER(declinfo->thetype) = *TYPE_POINTER(type);
        TYPE_POINTER(declinfo->thetype)->qual |=
            declinfo->qual & (Q_RESTRICT | Q_REFERENCE | Q_VOLATILE | Q_CONST);
        declinfo->qual &= ~(Q_RESTRICT | Q_REFERENCE | Q_VOLATILE | Q_CONST);
        declinfo->qual |= qual & (Q_ALIGNED_MASK | Q_REFERENCE | Q_PASCAL | Q_VOLATILE | Q_CONST);
    } else if (type->type == TYPEMEMBERPOINTER) {
        declinfo->thetype = galloc(sizeof(TypeMemberPointer));
        *TYPE_MEMBER_POINTER(declinfo->thetype) = *TYPE_MEMBER_POINTER(type);
        TYPE_MEMBER_POINTER(declinfo->thetype)->qual |=
            declinfo->qual & (Q_RESTRICT | Q_REFERENCE | Q_VOLATILE | Q_CONST);
        declinfo->qual &= ~(Q_RESTRICT | Q_REFERENCE | Q_VOLATILE | Q_CONST);
        declinfo->qual |= qual & (Q_ALIGNED_MASK | Q_REFERENCE | Q_PASCAL | Q_VOLATILE | Q_CONST);
    } else {
        declinfo->thetype = type;
        declinfo->qual |= qual & (Q_ALIGNED_MASK | Q_REFERENCE | Q_PASCAL | Q_VOLATILE | Q_CONST);
        if (IS_TYPE_ARRAY(declinfo->thetype) && !declinfo->thetype->size) {
            declinfo->thetype = galloc(sizeof(TypePointer));
            *TYPE_POINTER(declinfo->thetype) = *TYPE_POINTER(type);
        }
    }
    declinfo->isType = TRUE;
}

static void CParser_ParseAttributeFunctionSummary(DeclInfo* declinfo) {
    Boolean entryAllowed;

    if ((tk = lex()) != '(') {
        CError_Error(ERR_LPAREN_EXPECTED);
        return;
    }

    entryAllowed = TRUE;
    tk = lookahead();
    while (tk == TK_IDENTIFIER) {
        if (entryAllowed && !strcmp(tkidentifier->name, "entry_points_to")) {
            PointerAnalysis_ParseEntryPointsToSpecifier(declinfo);
        } else if (!strcmp(tkidentifier->name, "exit_points_to")) {
            PointerAnalysis_ParseExitPointsToSpecifier(declinfo);
            entryAllowed = FALSE;
        } else if (!strcmp(tkidentifier->name, "function_modifies")) {
            PointerAnalysis_ParseFunctionModifiesSpecifier(declinfo);
            entryAllowed = FALSE;
        } else {
            lex();
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            return;
        }

        tk = lookahead();
        if (tk == ',') {
            lex();
            tk = lookahead();
        }
    }

    lex();
    if (tk != ')') {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        return;
    }
}

#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -48
#endif
void CParser_ParseAttribute(Type* type, DeclInfo* declinfo) {
    CInt64 val64;
    SInt32 val;

    do {
        if ((tk = lex()) != '(') {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            return;
        }
        if ((tk = lex()) != '(') {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            return;
        }
        do {
            if ((tk = lex()) != TK_IDENTIFIER) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                return;
            }

            if (!strcmp(tkidentifier->name, "aligned") ||
                !strcmp(tkidentifier->name, "__aligned__")) {
                if ((tk = lex()) != '(') {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    return;
                }

                tk = lex();
                val64 = CExpr_IntegralConstExpr();
                switch ((val = CInt64_GetULong(&val64))) {
                    case 1:
                    case 2:
                    case 4:
                    case 8:
                    case 0x10:
                    case 0x20:
                    case 0x40:
                    case 0x80:
                    case 0x100:
                    case 0x200:
                    case 0x400:
                    case 0x800:
                    case 0x1000:
                    case 0x2000:
                        break;
                    default:
                        CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
                        return;
                }
                if (type) {
                    if (IS_TYPE_STRUCT(type)) {
                        if (val > TYPE_STRUCT(type)->align) {
                            TYPE_STRUCT(type)->align = val;
                            type->size += CABI_StructSizeAlignValue(type, type->size);
                        }
                    } else if (IS_TYPE_CLASS(type)) {
                        if (val > TYPE_CLASS(type)->align) {
                            TYPE_CLASS(type)->align = val;
                            type->size += CABI_StructSizeAlignValue(type, type->size);
                        }
                    } else {
                        CError_Error(ERR_NOT_STRUCT_UNION_CLASS);
                    }
                } else if (declinfo) {
                    declinfo->qual &= ~Q_ALIGNED_MASK;
                    switch (val) {
                        case 1:
                            declinfo->qual |= Q_ALIGNED_1;
                            break;
                        case 2:
                            declinfo->qual |= Q_ALIGNED_2;
                            break;
                        case 4:
                            declinfo->qual |= Q_ALIGNED_4;
                            break;
                        case 8:
                            declinfo->qual |= Q_ALIGNED_8;
                            break;
                        case 16:
                            declinfo->qual |= Q_ALIGNED_16;
                            break;
                        case 32:
                            declinfo->qual |= Q_ALIGNED_32;
                            break;
                        case 64:
                            declinfo->qual |= Q_ALIGNED_64;
                            break;
                        case 128:
                            declinfo->qual |= Q_ALIGNED_128;
                            break;
                        case 256:
                            declinfo->qual |= Q_ALIGNED_256;
                            break;
                        case 512:
                            declinfo->qual |= Q_ALIGNED_512;
                            break;
                        case 1024:
                            declinfo->qual |= Q_ALIGNED_1024;
                            break;
                        case 2048:
                            declinfo->qual |= Q_ALIGNED_2048;
                            break;
                        case 4096:
                            declinfo->qual |= Q_ALIGNED_4096;
                            break;
                        case 8192:
                            declinfo->qual |= Q_ALIGNED_8192;
                            break;
                        default:
                            CError_FATAL(2828);
                            break;
                    }
                } else {
                    CError_Error(ERR_ILLEGAL_UNSUPPORTED_ATTRIBUTE);
                }

                if (tk != ')') {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    return;
                }
            } else if (!strcmp(tkidentifier->name, "nothrow") ||
                       !strcmp(tkidentifier->name, "__nothrow__")) {
                if (declinfo && declinfo->thetype && IS_TYPE_FUNC(declinfo->thetype)) {
                    TYPE_FUNC(declinfo->thetype)->flags |= FUNC_NOTHROW;
                } else {
                    CError_Error(ERR_ILLEGAL_UNSUPPORTED_ATTRIBUTE);
                }
            } else if (!strcmp(tkidentifier->name, "section") ||
                       !strcmp(tkidentifier->name, "__section__")) {
                if ((tk = lex()) != '(') {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    return;
                }
                if ((tk = lex()) != TK_STRING) {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    return;
                }
                if (declinfo) {
                    DoParseDeclSpec(tkstring, declinfo);
                } else {
                    CError_Error(ERR_ILLEGAL_TYPE_QUALIFIERS);
                }
                if ((tk = lex()) != ')') {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    return;
                }
            } else if (!strcmp("function_summary", tkidentifier->name)) {
                CParser_ParseAttributeFunctionSummary(declinfo);
            } else {
                CError_Error(ERR_ILLEGAL_UNSUPPORTED_ATTRIBUTE);
            }
        } while ((tk = lex()) == ',');

        if (tk != ')') {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            return;
        }
        if ((tk = lex()) != ')') {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            return;
        }
        tk = lex();
    } while ((int)tk == TK_UU_ATTRIBUTE_UU);

    if ((int)tk == TK_ASM && declinfo) {
        CMach_ParseGlobalRegisterVariable(declinfo);
    }
}

static void CParser_ParseTypeOf(DeclInfo* declinfo) {
    DeclInfo subdi;
    ENode* expr;

    if ((tk = lex()) == '(' && islookaheaddeclaration()) {
        tk = lex();

        memclrw(&subdi, sizeof(DeclInfo));
        CParser_GetDeclSpecs(&subdi, FALSE);
        scandeclarator(&subdi);
        if (subdi.name) {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        }

        if (tk != ')') {
            CError_ErrorSkip(ERR_RPAREN_EXPECTED);
        } else {
            tk = lex();
        }

        TypedefDeclInfo(declinfo, subdi.thetype, subdi.qual);
    } else {
        expr = unary_expression();
        if (ENODE_IS(expr, EINDIRECT) && ENODE_IS(expr->data.monadic, EBITFIELD)) {
            CError_Error(ERR_ILLEGAL_OPERAND);
        }
        TypedefDeclInfo(declinfo, expr->rtype, expr->flags & ENODE_FLAG_QUALS);
    }
}

void CParser_ParseDeclSpec(DeclInfo* declinfo, Boolean unused) {
    if ((tk = lex()) != TK_IDENTIFIER) {
        if (tk != TK_EXPORT) {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
            return;
        }
        declinfo->exportflags |= OBJECT_EXPORT;
        return;
    }
    if (!strcmp("internal", tkidentifier->name)) {
        declinfo->exportflags |= OBJECT_INTERNAL;
        return;
    }
    if (!strcmp("import", tkidentifier->name) || !strcmp("dllimport", tkidentifier->name)) {
        declinfo->exportflags |= OBJECT_IMPORT;
        return;
    }
    if (!strcmp("export", tkidentifier->name) || !strcmp("dllexport", tkidentifier->name)) {
        declinfo->exportflags |= OBJECT_EXPORT;
        return;
    }
    if (!strcmp("lib_export", tkidentifier->name)) {
        declinfo->exportflags |= OBJECT_IMPORT | OBJECT_EXPORT;
        return;
    }
    if (!strcmp("weak", tkidentifier->name)) {
        declinfo->qual |= Q_WEAK;
        return;
    }
    CodeGen_ParseDeclSpec(tkidentifier, declinfo);
}

static int CParser_GetVectorDeclSpec(Type** type) {
    tk = lex();
    switch (tk) {
        case TK_CHAR:
            tk = lex();
            switch (tk) {
                case TK_BOOL:
                    *type = TYPE(&stvectorboolchar);
                    tk = lex();
                    return TRUE;
                case TK_UNSIGNED:
                    *type = TYPE(&stvectorunsignedchar);
                    tk = lex();
                    return TRUE;
                case TK_SIGNED:
                    *type = TYPE(&stvectorsignedchar);
                    tk = lex();
                    return TRUE;
                case TK_IDENTIFIER:
                    if (tkidentifier == GetHashNameNode("bool")) {
                        *type = TYPE(&stvectorboolchar);
                        tk = lex();
                        return TRUE;
                    }
                default:
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            break;
        case TK_SIGNED:
            tk = lex();
            switch (tk) {
                case TK_CHAR:
                    *type = TYPE(&stvectorsignedchar);
                    tk = lex();
                    return TRUE;
                case TK_SHORT:
                    *type = TYPE(&stvectorsignedshort);
                    tk = lex();
                    if (tk == TK_INT) {
                        tk = lex();
                    }
                    return TRUE;
                case TK_LONG:
                    *type = TYPE(&stvectorsignedlong);
                    tk = lex();
                    if (tk == TK_INT) {
                        tk = lex();
                    }
                    return TRUE;
                case TK_INT:
                    tk = lex();
                    switch (tk) {
                        case TK_SHORT:
                            *type = TYPE(&stvectorsignedshort);
                            tk = lex();
                            return TRUE;
                        case TK_LONG:
                            *type = TYPE(&stvectorsignedlong);
                            tk = lex();
                            return TRUE;
                        default:
                            *type = TYPE(&stvectorsignedlong);
                            return TRUE;
                    }
                default:
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            break;
        case TK_UNSIGNED:
            tk = lex();
            switch (tk) {
                case TK_CHAR:
                    *type = TYPE(&stvectorunsignedchar);
                    tk = lex();
                    return TRUE;
                case TK_SHORT:
                    *type = TYPE(&stvectorunsignedshort);
                    tk = lex();
                    if (tk == TK_INT) {
                        tk = lex();
                    }
                    return TRUE;
                case TK_LONG:
                    *type = TYPE(&stvectorunsignedlong);
                    tk = lex();
                    if (tk == TK_INT) {
                        tk = lex();
                    }
                    return TRUE;
                case TK_INT:
                    tk = lex();
                    switch (tk) {
                        case TK_SHORT:
                            *type = TYPE(&stvectorunsignedshort);
                            tk = lex();
                            return TRUE;
                        case TK_LONG:
                            *type = TYPE(&stvectorunsignedlong);
                            tk = lex();
                            return TRUE;
                        default:
                            *type = TYPE(&stvectorunsignedlong);
                            return TRUE;
                    }
                default:
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            break;
        case TK_BOOL:
            tk = lex();
            switch (tk) {
                case TK_CHAR:
                    *type = TYPE(&stvectorboolchar);
                    tk = lex();
                    return TRUE;
                case TK_SHORT:
                    *type = TYPE(&stvectorboolshort);
                    tk = lex();
                    if (tk == TK_INT) {
                        tk = lex();
                    }
                    return TRUE;
                case TK_LONG:
                    *type = TYPE(&stvectorboollong);
                    tk = lex();
                    if (tk == TK_INT) {
                        tk = lex();
                    }
                    return TRUE;
                case TK_INT:
                    tk = lex();
                    switch (tk) {
                        case TK_SHORT:
                            *type = TYPE(&stvectorboolshort);
                            tk = lex();
                            return TRUE;
                        case TK_LONG:
                            *type = TYPE(&stvectorboollong);
                            tk = lex();
                            return TRUE;
                        default:
                            *type = TYPE(&stvectorboollong);
                            return TRUE;
                    }
                default:
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            break;
        case TK_SHORT:
            tk = lex();
            switch (tk) {
                case TK_BOOL:
                    *type = TYPE(&stvectorboolshort);
                    tk = lex();
                    if (tk == TK_INT) {
                        tk = lex();
                    }
                    return TRUE;
                case TK_SIGNED:
                    *type = TYPE(&stvectorsignedshort);
                    tk = lex();
                    if (tk == TK_INT) {
                        tk = lex();
                    }
                    return TRUE;
                case TK_UNSIGNED:
                    *type = TYPE(&stvectorunsignedshort);
                    tk = lex();
                    if (tk == TK_INT) {
                        tk = lex();
                    }
                    return TRUE;
                case TK_INT:
                    tk = lex();
                    switch (tk) {
                        case TK_BOOL:
                            *type = TYPE(&stvectorboolshort);
                            tk = lex();
                            if (tk == TK_INT) {
                                tk = lex();
                            }
                            return TRUE;
                        case TK_SIGNED:
                            *type = TYPE(&stvectorsignedshort);
                            tk = lex();
                            return TRUE;
                        case TK_UNSIGNED:
                            *type = TYPE(&stvectorunsignedshort);
                            tk = lex();
                            return TRUE;
                        case TK_IDENTIFIER:
                            if (tkidentifier == GetHashNameNode("bool")) {
                                *type = TYPE(&stvectorboolshort);
                                tk = lex();
                                if (tk == TK_INT) {
                                    tk = lex();
                                }
                                return TRUE;
                            }
                        default:
                            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    }
                    break;
                case TK_IDENTIFIER:
                    if (tkidentifier == GetHashNameNode("bool")) {
                        *type = TYPE(&stvectorboolshort);
                        tk = lex();
                        if (tk == TK_INT) {
                            tk = lex();
                        }
                        return TRUE;
                    }
                default:
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            break;
        case TK_LONG:
            tk = lex();
            switch (tk) {
                case TK_BOOL:
                    *type = TYPE(&stvectorboollong);
                    tk = lex();
                    if (tk == TK_INT) {
                        tk = lex();
                    }
                    return TRUE;
                case TK_SIGNED:
                    *type = TYPE(&stvectorsignedlong);
                    tk = lex();
                    if (tk == TK_INT) {
                        tk = lex();
                    }
                    return TRUE;
                case TK_UNSIGNED:
                    *type = TYPE(&stvectorunsignedlong);
                    tk = lex();
                    if (tk == TK_INT) {
                        tk = lex();
                    }
                    return TRUE;
                case TK_INT:
                    tk = lex();
                    switch (tk) {
                        case TK_BOOL:
                            *type = TYPE(&stvectorboollong);
                            tk = lex();
                            return TRUE;
                        case TK_SIGNED:
                            *type = TYPE(&stvectorsignedlong);
                            tk = lex();
                            return TRUE;
                        case TK_UNSIGNED:
                            *type = TYPE(&stvectorunsignedlong);
                            tk = lex();
                            return TRUE;
                        case TK_IDENTIFIER:
                            if (tkidentifier == GetHashNameNode("bool")) {
                                *type = TYPE(&stvectorboollong);
                                tk = lex();
                                return TRUE;
                            }
                        default:
                            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    }
                    break;
                case TK_IDENTIFIER:
                    if (tkidentifier == GetHashNameNode("bool")) {
                        *type = TYPE(&stvectorboollong);
                        tk = lex();
                        if (tk == TK_INT) {
                            tk = lex();
                        }
                        return TRUE;
                    }
                default:
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            break;
        case TK_INT:
            tk = lex();
            switch (tk) {
                case TK_BOOL:
                    tk = lex();
                    switch (tk) {
                        case TK_SHORT:
                            *type = TYPE(&stvectorboolshort);
                            tk = lex();
                            return TRUE;
                        case TK_LONG:
                            *type = TYPE(&stvectorboollong);
                            tk = lex();
                            return TRUE;
                        default:
                            *type = TYPE(&stvectorboollong);
                            return TRUE;
                    }
                case TK_SIGNED:
                    tk = lex();
                    switch (tk) {
                        case TK_SHORT:
                            *type = TYPE(&stvectorsignedshort);
                            tk = lex();
                            return TRUE;
                        case TK_LONG:
                            *type = TYPE(&stvectorsignedlong);
                            tk = lex();
                            return TRUE;
                        default:
                            *type = TYPE(&stvectorsignedlong);
                            return TRUE;
                    }
                case TK_UNSIGNED:
                    tk = lex();
                    switch (tk) {
                        case TK_SHORT:
                            *type = TYPE(&stvectorunsignedshort);
                            tk = lex();
                            return TRUE;
                        case TK_LONG:
                            *type = TYPE(&stvectorunsignedlong);
                            tk = lex();
                            return TRUE;
                        default:
                            *type = TYPE(&stvectorunsignedlong);
                            return TRUE;
                    }
                case TK_SHORT:
                    tk = lex();
                    switch (tk) {
                        case TK_BOOL:
                            *type = TYPE(&stvectorboolshort);
                            tk = lex();
                            return TRUE;
                        case TK_SIGNED:
                            *type = TYPE(&stvectorsignedshort);
                            tk = lex();
                            return TRUE;
                        case TK_UNSIGNED:
                            *type = TYPE(&stvectorunsignedshort);
                            tk = lex();
                            return TRUE;
                        case TK_IDENTIFIER:
                            if (tkidentifier == GetHashNameNode("bool")) {
                                *type = TYPE(&stvectorboolshort);
                                tk = lex();
                                return TRUE;
                            }
                        default:
                            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    }
                    break;
                case TK_LONG:
                    tk = lex();
                    switch (tk) {
                        case TK_BOOL:
                            *type = TYPE(&stvectorboollong);
                            tk = lex();
                            return TRUE;
                        case TK_SIGNED:
                            *type = TYPE(&stvectorsignedlong);
                            tk = lex();
                            return TRUE;
                        case TK_UNSIGNED:
                            *type = TYPE(&stvectorunsignedlong);
                            tk = lex();
                            return TRUE;
                        case TK_IDENTIFIER:
                            if (tkidentifier == GetHashNameNode("bool")) {
                                *type = TYPE(&stvectorboollong);
                                tk = lex();
                                return TRUE;
                            }
                    }
                case TK_IDENTIFIER:
                    if (tkidentifier == GetHashNameNode("bool")) {
                        tk = lex();
                        switch (tk) {
                            case TK_LONG:
                                *type = TYPE(&stvectorboollong);
                                tk = lex();
                                return TRUE;
                            case TK_SHORT:
                                *type = TYPE(&stvectorboolshort);
                                tk = lex();
                                return TRUE;
                            default:
                                *type = TYPE(&stvectorboolshort);
                                return TRUE;
                        }
                    }
                default:
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            break;
        case TK_FLOAT:
            *type = TYPE(&stvectorfloat);
            tk = lex();
            return TRUE;
        case TK_IDENTIFIER:
            if (tkidentifier == GetHashNameNode("pixel") ||
                tkidentifier == GetHashNameNode("__pixel")) {
                *type = TYPE(&stvectorpixel);
                tk = lex();
                return TRUE;
            }
            if (tkidentifier == GetHashNameNode("bool")) {
                tk = lex();
                switch (tk) {
                    case TK_CHAR:
                        *type = TYPE(&stvectorboolchar);
                        tk = lex();
                        return TRUE;
                    case TK_SHORT:
                        *type = TYPE(&stvectorboolshort);
                        tk = lex();
                        if (tk == TK_INT) {
                            tk = lex();
                        }
                        return TRUE;
                    case TK_LONG:
                        *type = TYPE(&stvectorboollong);
                        tk = lex();
                        if (tk == TK_INT) {
                            tk = lex();
                        }
                        return TRUE;
                    case TK_INT:
                        tk = lex();
                        switch (tk) {
                            case TK_SHORT:
                                *type = TYPE(&stvectorboolshort);
                                tk = lex();
                                return TRUE;
                            case TK_LONG:
                                *type = TYPE(&stvectorboollong);
                                tk = lex();
                                return TRUE;
                            default:
                                *type = TYPE(&stvectorboollong);
                                return TRUE;
                        }
                }
            }
        default:
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
    }

    return FALSE;
}

Boolean CParser_CheckTemplateClassUsage(TemplClass* tmclass, Boolean reportError) {
    NameSpace* nspace;

    if (tmclass->templ__params) {
        nspace = cscope_current;
        while (1) {
            if (!nspace) {
                if (reportError) {
                    CError_Error(ERR_LESS_EXPECTED);
                }
                return FALSE;
            }
            if (nspace->theclass == TYPE_CLASS(tmclass)) {
                break;
            }
            nspace = nspace->parent;
        }
    }

    return TRUE;
}

static Boolean CParser_IsAltiVecPrefix(void) {
    HashNameNode* save = tkidentifier;

    switch (lookahead()) {
        case TK_CHAR:
        case TK_SHORT:
        case TK_INT:
        case TK_LONG:
        case TK_FLOAT:
        case TK_SIGNED:
        case TK_UNSIGNED:
        case TK_BOOL:
            return TRUE;
        case TK_IDENTIFIER:
            if (!strcmp(tkidentifier->name, "bool") ||
                !strcmp(tkidentifier->name, "pixel") ||
                !strcmp(tkidentifier->name, "__pixel")) {
                return TRUE;
            }
    }

    tkidentifier = save;
    return FALSE;
}

void CParser_GetDeclSpecs(DeclInfo* di, Boolean resolveMembers) {
    short typesize;
    short signedness;
    short typetoken;
    Boolean no_declspecs;
    Boolean check_implicit_int;
    SInt32 state;
    NameResult pr;

    di->file = CPrep_BrowserCurrentFile();
    CPrep_BrowserFilePosition(&di->file2, &di->sourceoffset);

    no_declspecs = TRUE;
    check_implicit_int = copts.cplusplus;
    typesize = signedness = typetoken = 0;

restart:
    switch (tk) {
        case TK_AUTO:
        case TK_REGISTER:
        case TK_STATIC:
        case TK_EXTERN:
        case TK_TYPEDEF:
        case TK_MUTABLE:
            if (di->storageclass) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            di->storageclass = tk;
            break;
        case TK_CONST:
            if (di->thetype) {
                if (di->thetype->type == TYPEPOINTER) {
                    if (TYPE_POINTER(di->thetype)->qual & Q_CONST) {
                        CError_QualifierCheck(Q_CONST);
                    }
                    TYPE_POINTER(di->thetype)->qual |= Q_CONST;
                    break;
                } else if (di->thetype->type == TYPEMEMBERPOINTER) {
                    if (TYPE_MEMBER_POINTER(di->thetype)->qual & Q_CONST) {
                        CError_QualifierCheck(Q_CONST);
                    }
                    TYPE_MEMBER_POINTER(di->thetype)->qual |= Q_CONST;
                    break;
                }
            }
            if (di->qual & Q_CONST) {
                CError_QualifierCheck(Q_CONST);
            }
            di->qual |= Q_CONST;
            break;
        case TK_VOLATILE:
            if (di->thetype) {
                if (di->thetype->type == TYPEPOINTER) {
                    if (TYPE_POINTER(di->thetype)->qual & Q_VOLATILE) {
                        CError_QualifierCheck(Q_VOLATILE);
                    }
                    TYPE_POINTER(di->thetype)->qual |= Q_VOLATILE;
                    break;
                } else if (di->thetype->type == TYPEMEMBERPOINTER) {
                    if (TYPE_MEMBER_POINTER(di->thetype)->qual & Q_VOLATILE) {
                        CError_QualifierCheck(Q_VOLATILE);
                    }
                    TYPE_MEMBER_POINTER(di->thetype)->qual |= Q_VOLATILE;
                    break;
                }
            }
            if (di->qual & Q_VOLATILE) {
                CError_QualifierCheck(Q_VOLATILE);
            }
            di->qual |= Q_VOLATILE;
            break;
        case TK_PASCAL:
            if (di->qual & Q_PASCAL) {
                CError_QualifierCheck(Q_PASCAL);
            }
            di->qual |= Q_PASCAL;
            break;
        case TK_EXPLICIT:
            CError_QualifierCheck(di->qual & Q_EXPLICIT);
            di->qual |= Q_EXPLICIT;
            break;
        case TK_VIRTUAL:
            CError_QualifierCheck(di->qual & Q_VIRTUAL);
            di->qual |= Q_VIRTUAL;
            break;
        case TK_IN:
            CError_QualifierCheck(di->qual & Q_IN);
            di->qual |= Q_IN;
            break;
        case TK_OUT:
            CError_QualifierCheck(di->qual & Q_OUT);
            di->qual |= Q_OUT;
            break;
        case TK_INOUT:
            CError_QualifierCheck(di->qual & Q_INOUT);
            di->qual |= Q_INOUT;
            break;
        case TK_BYCOPY:
            CError_QualifierCheck(di->qual & Q_BYCOPY);
            di->qual |= Q_BYCOPY;
            break;
        case TK_BYREF:
            CError_QualifierCheck(di->qual & Q_BYREF);
            di->qual |= Q_BYREF;
            break;
        case TK_ONEWAY:
            CError_QualifierCheck(di->qual & Q_ONEWAY);
            di->qual |= Q_ONEWAY;
            break;
        case TK_UU_DECLSPEC:
            if ((tk = lex()) != '(') {
                CError_Error(ERR_LPAREN_EXPECTED);
            }
            CParser_ParseDeclSpec(di, FALSE);
            if ((tk = lex()) != ')') {
                CError_Error(ERR_RPAREN_EXPECTED);
            }
            break;
        case TK_ASM:
            if (di->qual & Q_ASM) {
                CError_QualifierCheck(Q_ASM);
            }
            di->qual |= Q_ASM;
            break;
        case TK_INLINE:
            if (di->qual & Q_INLINE) {
                CError_QualifierCheck(Q_INLINE);
            }
            di->qual |= Q_INLINE;
            break;
        case TK_SHORT:
            if (typesize ||
                (typetoken && (int)typetoken != TK_INT && (int)typetoken != TK_DOUBLE)) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            typesize = 1;
            break;
        case TK_LONG:
            if (copts.longlong) {
                if (typetoken && (int)typetoken != TK_INT && (int)typetoken != TK_DOUBLE) {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                }
                if (typesize) {
                    if (typesize != 2 || typetoken == TK_DOUBLE) {
                        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    }
                    typesize = 3;
                } else {
                    typesize = 2;
                }
            } else {
                if (typesize ||
                    (typetoken && (int)typetoken != TK_INT && (int)typetoken != TK_DOUBLE)) {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                }
                typesize = 2;
            }
            break;
        case TK_CUSTOM_TYPE_113:
        case TK_CUSTOM_TYPE_114:
        case TK_UU_VEC2X32FLOAT_UU:
        case TK_CUSTOM_TYPE_116:
        case TK_CUSTOM_TYPE_117:
        case TK_CUSTOM_TYPE_118:
        case TK_CUSTOM_TYPE_119:
        case TK_CUSTOM_TYPE_11A:
            if (typetoken) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            typetoken = tk;
            break;
        case TK_SIGNED:
            if (signedness ||
                (typetoken && (int)typetoken != TK_INT && (int)typetoken != TK_CHAR)) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            signedness = -1;
            break;
        case TK_UNSIGNED:
            if (signedness ||
                (typetoken && (int)typetoken != TK_INT && (int)typetoken != TK_CHAR)) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            signedness = 1;
            break;
        case TK_VOID:
            if (typetoken || typesize || signedness) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            typetoken = TK_VOID;
            break;
        case TK_FLOAT:
            if (typetoken || typesize || signedness) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            typetoken = TK_FLOAT;
            break;
        case TK_BOOL:
            if (typetoken || typesize) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            typetoken = TK_BOOL;
            break;
        case TK_CHAR:
            if (typetoken || typesize) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            typetoken = TK_CHAR;
            break;
        case TK_WCHAR_T:
            if (typetoken || typesize || signedness) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            typetoken = TK_WCHAR_T;
            break;
        case TK_INT:
            if (typetoken) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            typetoken = TK_INT;
            break;
        case TK_DOUBLE:
            if (typetoken || signedness) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            typetoken = TK_DOUBLE;
            break;
        case TK_STRUCT:
            if (typetoken || signedness || typesize) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            tk = lex();
            scanstruct(di, STRUCT_TYPE_STRUCT);
            if (tk == TK_UU_ATTRIBUTE_UU) {
                CParser_ParseAttribute(di->thetype, NULL);
            }
            if (!((int)tk != TK_CONST &&
                  (int)tk != TK_VOLATILE &&
                  (int)tk != TK_UU_FAR &&
                  (int)tk != TK_UU_DECLSPEC)) {
                typetoken = -1;
                goto restart;
            }
            return;
        case TK_CLASS:
            if (typetoken || signedness || typesize) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            tk = lex();
            CDecl_ParseClass(di, CLASS_MODE_CLASS, TRUE, 0);
            if (tk == TK_UU_ATTRIBUTE_UU) {
                CParser_ParseAttribute(di->thetype, NULL);
            }
            if (!((int)tk != TK_CONST &&
                  (int)tk != TK_VOLATILE &&
                  (int)tk != TK_UU_FAR &&
                  (int)tk != TK_UU_DECLSPEC)) {
                typetoken = -1;
                goto restart;
            }
            return;
        case TK_UNION:
            if (typetoken || signedness || typesize) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            tk = lex();
            scanstruct(di, STRUCT_TYPE_UNION);
            if (tk == TK_UU_ATTRIBUTE_UU) {
                CParser_ParseAttribute(di->thetype, NULL);
            }
            if (!((int)tk != TK_CONST &&
                  (int)tk != TK_VOLATILE &&
                  (int)tk != TK_UU_FAR &&
                  (int)tk != TK_UU_DECLSPEC)) {
                typetoken = -1;
                goto restart;
            }
            return;
        case TK_ENUM:
            if (typetoken || signedness || typesize) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            tk = lex();
            scanenum(di);
            if (tk == TK_UU_ATTRIBUTE_UU) {
                CParser_ParseAttribute(di->thetype, NULL);
            }
            if (!((int)tk != TK_CONST &&
                  (int)tk != TK_VOLATILE &&
                  (int)tk != TK_UU_FAR &&
                  (int)tk != TK_UU_DECLSPEC)) {
                typetoken = -1;
                goto restart;
            }
            return;
        case TK_TYPENAME:
            if (typetoken || signedness || typesize || di->hasTypename) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            di->hasTypename = TRUE;
            tk = lex();
            if (tk != TK_COLON_COLON && tk != TK_IDENTIFIER) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                break;
            }
            goto some_shared_label;
        case TK_COLON_COLON:
            if (typetoken || signedness || typesize) {
                goto switchDefault;
            }
            goto some_shared_label;
        case TK_UU_VECTOR:
            if (typetoken || signedness || typesize) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
handle_vector:
            if (CParser_GetVectorDeclSpec(&di->thetype)) {
                if (tk == TK_CONST) {
                    if (di->qual == 0) {
                        di->qual |= Q_CONST;
                        tk = lex();
                    } else {
                        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    }
                }
                if (tk == TK_VOLATILE) {
                    if (di->qual == 0) {
                        di->qual |= Q_VOLATILE;
                        tk = lex();
                    } else {
                        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    }
                }
                return;
            }
            break;
        case TK_UU_TYPEOF_UU:
            if (typetoken || signedness || typesize) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            CParser_ParseTypeOf(di);
            typetoken = -1;
            goto bailOut;
        case TK_IDENTIFIER:
            if (copts.altivec_model &&
                !typetoken &&
                !signedness &&
                !typesize &&
                !strcmp(tkidentifier->name, "vector")) {
                if (CParser_IsAltiVecPrefix()) {
                    goto handle_vector;
                }
            }
            if (!typetoken && !signedness && !typesize) {
                if (copts.objective_c && !strcmp(tkidentifier->name, "id")) {
                    di->thetype = CObjC_ParseID();
                    typetoken = -1;
                    goto bailOut;
                }
some_shared_label:
                CPrep_TokenStreamGetState(&state);
                if (CScope_ParseDeclName(&pr)) {
                    if (pr.type) {
                        if (IS_TYPE_TEMPLATE(pr.type)) {
                            switch (TYPE_TEMPLATE(pr.type)->dtype) {
                                case TEMPLDEP_ARGUMENT:
                                    switch (TYPE_TEMPLATE(pr.type)->u.pid.type) {
                                        case TPT_TYPE:
                                            break;
                                        case TPT_NONTYPE:
                                            CError_Error(
                                                ERR_ILLEGAL_TEMPLATE_ARGUMENT_DEPENDENT_EXPRESSION
                                            );
                                            pr.type = TYPE(&stsignedint);
                                            break;
                                        case TPT_TEMPLATE:
                                            CError_Error(ERR_LESS_EXPECTED);
                                            pr.type = TYPE(&stsignedint);
                                            break;
                                        default:
                                            CError_FATAL(4159);
                                    }
                                    break;
                                case TEMPLDEP_QUALNAME:
                                    if (!di->hasTypename && !pr.is_type && di->isTypeID) {
                                        CError_Error(
                                            ERR_TYPENAME_MISSING_TEMPLATE_ARGUMENT_DEPENDENT_QUALIFIED
                                        );
                                    }
                                    break;
                                case TEMPLDEP_TEMPLATE:
                                case TEMPLDEP_ARRAY:
                                case TEMPLDEP_QUALTEMPL:
                                case TEMPLDEP_BITFIELD:
                                    break;
                                default:
                                    CError_FATAL(4186);
                            }
                        }

                        if (IS_TYPE_CLASS(pr.type) &&
                            (TYPE_CLASS(pr.type)->flags & CLASS_IS_TEMPL)) {
                            if (!CParser_CheckTemplateClassUsage(TEMPL_CLASS(pr.type), FALSE)) {
                                if (di->allowTemplateName) {
                                    if (di->qual) {
                                        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                                    }
                                    di->thetype = pr.type;
                                    di->isTemplateName = TRUE;
                                    tk = lex();
                                    return;
                                } else {
                                    CError_Error(ERR_LESS_EXPECTED);
                                    pr.type = TYPE(&stsignedint);
                                }
                            }
                        }

                        TypedefDeclInfo(di, pr.type, pr.qual);
                        di->isType = pr.is_type;
                        typetoken = -1;
                        tk = lex();
                        if (tk == '<' &&
                            copts.objective_c &&
                            IS_TYPE_CLASS(di->thetype) &&
                            TYPE_CLASS(di->thetype)->objcinfo) {
                            di->thetype = CObjC_ParseTypeProtocol(TYPE_CLASS(di->thetype));
                        }
                        goto bailOut;
                    } else if (pr.objects) {
                        if (pr.is_qualified) {
                            if (resolveMembers &&
                                (OBJECT(pr.objects->object)->nspace == pr.nspace ||
                                 di->in_friend_decl)) {
                                di->resolvedObjects = pr.objects;
                                if (IS_TYPE_FUNC(OBJECT(di->resolvedObjects->object)->type) &&
                                    ((TYPE_FUNC(OBJECT(di->resolvedObjects->object)->type)->flags &
                                      FUNC_IS_CTOR) |
                                     FUNC_IS_DTOR)) {
                                    check_implicit_int = FALSE;
                                }
                            } else {
                                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                            }
                        }
                    } else if (pr.object) {
                        switch (pr.object->otype) {
                            case OT_OBJECT:
                                if (pr.is_qualified) {
                                    if (resolveMembers &&
                                        (OBJECT(pr.object)->nspace == pr.nspace ||
                                         di->in_friend_decl)) {
                                        di->resolvedObject = OBJECT(pr.object);
                                        if (IS_TYPE_FUNC(OBJECT(di->resolvedObject)->type) &&
                                            ((TYPE_FUNC(OBJECT(di->resolvedObject)->type)->flags &
                                              FUNC_IS_CTOR) |
                                             FUNC_IS_DTOR)) {
                                            check_implicit_int = FALSE;
                                        }
                                    } else {
                                        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                                    }
                                }
                                break;
                            case OT_ENUMCONST:
                            case OT_MEMBERVAR:
                                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                                break;
                            default:
                                CError_FATAL(4267);
                        }
                    } else if (pr.name) {
                        if (copts.cplusplus) {
                            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                        }
                    } else if (pr.is_conv_operator) {
                        CPrep_TokenStreamSetState(&state);
                        CPrep_UnLex();
                        tk = lex();
                        check_implicit_int = FALSE;
                    } else {
                        CError_FATAL(4284);
                    }
                }
            }
        default:
switchDefault:
            if (!typetoken && !signedness && !typesize) {
                di->parserOption = TRUE;
                if (check_implicit_int) {
                    if (!di->storageclass && !di->qual && !di->exportflags) {
                        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    } else {
                        CError_Warning(ERR_IMPLICIT_INT_NO_LONGER_SUPPORTED_C);
                    }
                }
            }
            if (typetoken >= 0) {
                di->thetype = getthetype(typetoken, typesize, signedness);
            }
            if (no_declspecs) {
                di->missingTypeSpecifier = TRUE;
            }
            if (tk == TK_UU_ATTRIBUTE_UU) {
                CParser_ParseAttribute(NULL, di);
            }
            return;
        case ';':
            if (!typetoken && !signedness && !typesize && copts.warn_emptydecl) {
                CError_Warning(ERR_ILLEGAL_EMPTY_DECLARATION);
            }
            if (typetoken >= 0) {
                di->thetype = getthetype(typetoken, typesize, signedness);
            }
            return;
    }

    tk = lex();
bailOut:
    no_declspecs = FALSE;
    goto restart;
}

void CParser_RegisterNonGlobalClass(TypeClass* tclass) {
    struct ParentCleanup* p = lalloc(sizeof(struct ParentCleanup));
    p->next = cparser_parentcleanup;
    p->tclass = tclass;
    cparser_parentcleanup = p;
}

void CParser_RegisterSingleExprFunction(Object* func, ENode* expr) {
    struct SFuncList* p = lalloc(sizeof(struct SFuncList));
    p->next = cparser_sfunclist;
    p->func = func;
    p->obj = NULL;
    p->expr = expr;
    cparser_sfunclist = p;
}

void CParser_RegisterDummyCtorFunction(Object* func, Object* obj) {
    struct SFuncList* p = lalloc(sizeof(struct SFuncList));
    p->next = cparser_sfunclist;
    p->func = func;
    p->obj = obj;
    p->expr = NULL;
    cparser_sfunclist = p;
}

static void CParser_FreeLocalHeap(void) {
    struct SFuncList* s;
    struct ParentCleanup* p;

    while (cparser_sfunclist) {
        s = cparser_sfunclist;
        cparser_sfunclist = s->next;
        if (s->expr) {
            CFunc_GenerateSingleExprFunc(s->func, s->expr);
        } else {
            CFunc_GenerateDummyCtorFunc(s->func, s->obj);
        }
    }

    if (cparser_parentcleanup) {
        if (!CInline_CanFreeLHeap()) {
            return;
        }

        for (p = cparser_parentcleanup; p; p = p->next) {
            while (!p->tclass->nspace->parent->is_global) {
                p->tclass->nspace->parent = p->tclass->nspace->parent->parent;
            }
        }

        cparser_parentcleanup = NULL;
    }

    freelheap();
}

static void CParser_GlobalCleanup(Boolean runClassActions) {
    Boolean working;

    do {
        CParser_FreeLocalHeap();
        working = FALSE;

        if (runClassActions) {
            if (cparser_classactions) {
                CClass_ClassAction(cparser_classactions->tclass);
                cparser_classactions = cparser_classactions->next;
                working = TRUE;
            } else if (CTempl_Instantiate()) {
                working = TRUE;
            }
        }

        while (CInline_GenerateDeferredFuncs()) {
            CParser_FreeLocalHeap();
            working = TRUE;
        }
    } while (working);
}

Boolean CParser_IsAnonymousUnion(DeclInfo* di, Boolean allowStruct) {
    return IS_TYPE_CLASS(di->thetype) &&
        ((TYPE_CLASS(di->thetype)->mode == CLASS_MODE_UNION ||
          (allowStruct && copts.cpp_extensions))) &&
        IsTempName(TYPE_CLASS(di->thetype)->classname);
}

void CParser_CheckAnonymousUnion(DeclInfo* di, Boolean isLocal) {
    ObjMemberVar* ivar;
    Object* obj;
    Object* ivar_obj;

    if (!CParser_IsAnonymousUnion(di, FALSE)) {
        if (copts.warn_emptydecl) {
            switch (di->thetype->type) {
                case TYPEENUM:
                case TYPESTRUCT:
                case TYPECLASS:
                    if (!di->storageclass && !di->qual) {
                        break;
                    }
                default:
                    CError_Warning(ERR_ILLEGAL_EMPTY_DECLARATION);
            }
        }
        return;
    }

    if (!isLocal && di->storageclass != TK_STATIC) {
        CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
    }

    if (isLocal && di->storageclass != TK_STATIC) {
        obj = CParser_NewLocalDataObject(di, TRUE);
        obj->name = CParser_GetUniqueName();
        CFunc_SetupLocalVarInfo(obj);
        obj->u.var.info->noregister = TRUE;
    } else {
        obj = CParser_NewGlobalDataObject(di);
        obj->name = CParser_GetUniqueName();
        obj->nspace = cscope_root;
        obj->sclass = TK_STATIC;
        CInit_DeclareData(obj, NULL, NULL, obj->type->size);
    }

    for (ivar = TYPE_CLASS(di->thetype)->ivars; ivar; ivar = ivar->next) {
        ivar_obj = galloc(sizeof(Object));
        *ivar_obj = *obj;
        ivar_obj->name = ivar->name;
        ivar_obj->type = ivar->type;
        ivar_obj->qual = ivar->qual;
        ivar_obj->datatype = DALIAS;
        ivar_obj->u.alias.object = obj;
        ivar_obj->u.alias.offset = ivar->offset;
        ivar_obj->u.alias.member = NULL;
        CScope_AddObject(cscope_current, ivar_obj->name, OBJ_BASE(ivar_obj));
    }
}

void CParser_NewCallBackAction(Object* obj, TypeClass* tclass) {
    CallbackAction* act = galloc(sizeof(CallbackAction));
    act->next = callbackactions;
    act->obj = obj;
    act->tclass = tclass;
    callbackactions = act;
    obj->flags |= OBJECT_LAZY;
}

void CParser_NewClassAction(TypeClass* tclass) {
    struct ClassAction* act = galloc(sizeof(struct ClassAction));
    act->next = cparser_classactions;
    act->tclass = tclass;
    cparser_classactions = act;
}

void CParser_CallBackAction(Object* obj) {
    CallbackAction* act;

    for (act = callbackactions; act; act = act->next) {
        if (act->obj == obj) {
            CParser_NewClassAction(act->tclass);
            return;
        }
    }

    CError_FATAL(4601);
}

static Object* CParser_FindOverloadFunc(NameSpaceObjectList* list, TypeFunc* tfunc) {
    while (list) {
        if (list->object->otype == OT_OBJECT && IS_TYPE_FUNC(OBJECT(list->object)->type)) {
            if (CParser_CompareArgLists(tfunc->args, TYPE_FUNC(OBJECT(list->object)->type)->args) ==
                1) {
                return OBJECT(list->object);
            }
        }
        list = list->next;
    }

    return NULL;
}

Object* CParser_ParseObject(void) {
    DeclInfo di;
    NameResult pr;
    NameSpaceObjectList* list;

    memclrw(&di, sizeof(DeclInfo));
    CParser_GetDeclSpecs(&di, TRUE);
    scandeclarator(&di);

    if (di.name &&
        (list = CScope_FindObjectList(&pr, di.name)) &&
        list->object->otype == OT_OBJECT) {
        if (IS_TYPE_FUNC(di.thetype)) {
            return CParser_FindOverloadFunc(list, TYPE_FUNC(di.thetype));
        }

        if (is_typesame(di.thetype, OBJECT(list->object)->type) &&
            OBJECT(list->object)->qual == di.qual) {
            return OBJECT(list->object);
        }

        CError_Error(
            ERR_IDENTIFIER_REDECLARED_WAS_DECLARED_AS_NOW,
            CError_GetObjectName(OBJECT(list->object)),
            OBJECT(list->object)->type,
            OBJECT(list->object)->qual,
            di.thetype,
            di.qual
        );
    }

    return NULL;
}

void CParser_ParseGlobalDeclaration(void) {
    DeclInfo di;

    if (tk) {
        CPrep_NewFileOffsetInfo(&cparser_fileoffset, NULL);
        symdecloffset = cparser_fileoffset.tokenline;
        symdecltoken = *CPrep_CurStreamElement();

        memclrw(&di, sizeof(DeclInfo));
        CParser_GetDeclSpecs(&di, TRUE);
        if ((int)di.storageclass == TK_REGISTER || (int)di.storageclass == TK_AUTO) {
            CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
            di.storageclass = 0;
        }

        if (tk != ';') {
            scandeclaratorlist(&di);
        } else {
            CParser_CheckAnonymousUnion(&di, FALSE);
        }

        tk = lex();
    } else {
        CError_Error(ERR_UNEXPECTED_END_FILE);
    }
}

static void CParser_ParseLinkageSpecification(DeclInfo* di) {
    UInt32 qual;
    UInt8 is_extern_c;

    if (!strcmp(tkstring, "C") || !strcmp(tkstring, "Objective C")) {
        qual = 0;
        is_extern_c = TRUE;
    } else if (!strcmp(tkstring, "C++")) {
        qual = 0;
        is_extern_c = FALSE;
    } else if (!strcmp(tkstring, "Pascal")) {
        qual = Q_PASCAL;
        is_extern_c = TRUE;
    } else {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        qual = 0;
        is_extern_c = TRUE;
    }

    if ((tk = lex()) == '{') {
        while (1) {
            if ((tk = lex()) == 0) {
                CError_Error(ERR_RBRACE_EXPECTED);
                break;
            }

            if (tk == '}') {
                break;
            }

            CPrep_NewFileOffsetInfo(&cparser_fileoffset, NULL);
            symdecloffset = cparser_fileoffset.tokenline;
            symdecltoken = *CPrep_CurStreamElement();

            memclrw(di, sizeof(DeclInfo));
            di->is_extern_c = is_extern_c;
            di->qual = qual;
            CParser_ParseDeclaration(di);
        }
    } else if (tk == TK_EXTERN && copts.cpp_extensions && lookahead() == TK_STRING) {
        tk = lex();
        CParser_ParseLinkageSpecification(di);
        return;
    } else {
        memclrw(di, sizeof(DeclInfo));
        di->is_extern_c = is_extern_c;
        di->qual = qual;
        CParser_GetDeclSpecs(di, TRUE);

        if (di->storageclass != TK_TYPEDEF) {
            if (di->storageclass && copts.pedantic) {
                CError_Warning(ERR_ILLEGAL_STORAGE_CLASS);
            }
            if (!di->storageclass) {
                di->storageclass = TK_EXTERN;
            }
        }
        if (copts.cpp_extensions) {
            di->missingTypeSpecifier = FALSE;
        }
        if (tk != ';') {
            scandeclaratorlist(di);
        }
    }
}

static void CParser_ParseNameSpace(DeclInfo* di) {
    NameSpace* nspace;
    ObjNameSpace* objns;
    HashNameNode* name;
    Boolean isUnnamed;
    CScopeSave save;
    NameSpaceObjectList* list;
    NameSpaceList* nsl;

    if ((tk = lex()) == TK_IDENTIFIER) {
        name = tkidentifier;
        isUnnamed = FALSE;
        if ((tk = lex()) == '=') {
            CScope_ParseNameSpaceAlias(name);
            return;
        }
    } else {
        if (tk != '{') {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
            return;
        }
        name = CParser_GetUnnamedNameSpaceName();
        isUnnamed = TRUE;
    }

    nspace = cscope_current;
    if (!(list = CScope_FindName(nspace, name))) {
        objns = galloc(sizeof(ObjNameSpace));
        memclrw(objns, sizeof(ObjNameSpace));
        objns->otype = OT_NAMESPACE;
        objns->access = ACCESSPUBLIC;
        if (isUnnamed) {
            nspace = CScope_NewListNameSpace(name, TRUE);
            nspace->is_unnamed = TRUE;
            nsl = galloc(sizeof(NameSpaceList));
            nsl->next = cscope_current->usings;
            nsl->nspace = nspace;
            cscope_current->usings = nsl;
        } else {
            nspace = CScope_NewHashNameSpace(name);
            if (cscope_current->is_unnamed) {
                nspace->is_unnamed = TRUE;
            }
        }

        nspace->parent = cscope_current;
        objns->nspace = nspace;
        CScope_AddObject(cscope_current, name, OBJ_BASE(objns));
    } else {
        if (list->object->otype != OT_NAMESPACE) {
            CError_Error(ERR_ILLEGAL_NAMESPACE);
        } else {
            nspace = OBJ_NAMESPACE(list->object)->nspace;
        }
    }

    if (tk != '{') {
        CError_Error(ERR_LBRACE_EXPECTED);
        return;
    }

    CScope_SetNameSpaceScope(nspace, &save);
    while (1) {
        if ((tk = lex()) == 0) {
            CError_Error(ERR_RBRACE_EXPECTED);
            break;
        }

        if (tk == '}') {
            break;
        }

        CPrep_NewFileOffsetInfo(&cparser_fileoffset, NULL);
        symdecloffset = cparser_fileoffset.tokenline;
        symdecltoken = *CPrep_CurStreamElement();

        memclrw(di, sizeof(DeclInfo));
        CParser_ParseDeclaration(di);
    }
    CScope_RestoreScope(&save);
}

static void CParser_ParseDeclaration(DeclInfo* di) {
    switch (tk) {
        case TK_AT_INTERFACE:
            CObjC_ParseInterface();
            return;
        case TK_AT_IMPLEMENTATION:
            CObjC_ParseImplementation();
            return;
        case TK_AT_PROTOCOL:
            CObjC_ParseProtocol();
            return;
        case TK_AT_CLASS:
            CObjC_ParseClassDeclaration();
            return;
        case TK_NAMESPACE:
            CParser_ParseNameSpace(di);
            return;
        case TK_EXPORT:
            CError_Error(ERR_UNIMPLEMENTED_C_FEATURE);
            if ((tk = lex()) != TK_TEMPLATE) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                return;
            }
        case TK_TEMPLATE:
            CTempl_Parse(NULL, 0);
            return;
        case TK_USING:
            if ((tk = lex()) == TK_NAMESPACE) {
                tk = lex();
                CScope_ParseUsingDirective(cscope_current);
            } else {
                CScope_ParseUsingDeclaration(cscope_current, 0, FALSE);
            }
            return;
        case TK_EXTERN:
            if (copts.cplusplus) {
                di->storageclass = TK_EXTERN;
                if ((tk = lex()) == TK_STRING) {
                    CParser_ParseLinkageSpecification(di);
                    return;
                }
            }
        default:
            CParser_GetDeclSpecs(di, TRUE);
            if ((Boolean)(di->storageclass == TK_AUTO)) {
                CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
                di->storageclass = 0;
            }
            if (tk != ';') {
                scandeclaratorlist(di);
                if (di->storageclass == TK_REGISTER && !di->reginfo.reg) {
                    CError_Error(ERR_ILLEGAL_STORAGE_CLASS);
                    di->storageclass = 0;
                }
            } else {
                CParser_CheckAnonymousUnion(di, FALSE);
            }
            CParser_GlobalCleanup(FALSE);
            return;
    }
}

void cparser(void) {
    DeclInfo di;

    if (copts.crippled && copts.optimizationlevel > 1) {
        CError_Warning(ERR_ILLEGAL_OPTIMIZATION_LEVEL_LIMITED);
        copts.optimizationlevel = 1;
        CodeGen_UpdateOptimizerOptions();
        CodeGen_UpdateBackEndOptions();
    }

    if ((tk = lex())) {
        do {
            CPrep_NewFileOffsetInfo(&cparser_fileoffset, NULL);
            symdecloffset = cparser_fileoffset.tokenline;
            symdecltoken = *CPrep_CurStreamElement();

            memclrw(&di, sizeof(DeclInfo));
            CParser_ParseDeclaration(&di);
        } while (tk && (tk = lex()));
    } else {
        if (!copts.cplusplus && copts.ANSIstrict) {
            CError_Error(ERR_UNEXPECTED_END_FILE);
        }
    }

    CInit_DefineTentativeData();
    copts.defer_codegen = FALSE;
    CParser_GlobalCleanup(TRUE);

    if (cparamblkptr->precompile != 1) {
        CInline_Finish();
        CParser_GlobalCleanup(TRUE);
    }

    CClass_GenThunks();
    if (cparamblkptr->precompile != 1) {
        CObjC_GenerateModule();
    }

    CSOM_Cleanup();
    CInit_DefineTentativeData();
}
