#include "compiler/CObjC.h"
#include "compiler/CABI.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CMachine.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "driver/COSToolsCLT.h"
#include <stdio.h>
#include <string.h>

#pragma pack(push, 2)
typedef struct ObjCString {
    struct ObjCString* next;
    Object* object;
    char* str;
    Section section;
} ObjCString;

typedef struct ObjCCategoryEntry {
    struct ObjCCategoryEntry* next;
    ObjCCategory* category;
    Object* object;
} ObjCCategoryEntry;
#pragma pack(pop)

Type* cobjc_type_class;
Type* cobjc_type_id;
Type* cobjc_type_sel;
TypeClass* cobjc_currentclass;
ObjCSelector** cobjc_selhashtable;
BClassList* cobjc_classdefs;
ObjCProtocol* cobjc_protocols;
SInt32 cobjc_selrefcount;
SInt32 cobjc_classrefcount;
SInt32 cobjc_stringcount;
Boolean cobjc_encodemethod;
static ObjCCategoryEntry* cobjc_categories;
static ObjCString* cobjc_strings;

static void CObjC_EncodeType(Type* type, UInt32 qual, Boolean expandMembers);
static Object* CObjC_GetProtocolObject(ObjCProtocol* protocol);

void CObjC_Setup(void) {
    cobjc_type_class = NULL;
    cobjc_type_id = NULL;
    cobjc_type_sel = NULL;
    cobjc_currentclass = NULL;
    cobjc_selhashtable = NULL;
    cobjc_strings = NULL;
    cobjc_classdefs = NULL;
    cobjc_protocols = NULL;
    cobjc_categories = NULL;

    cobjc_selrefcount = 0;
    cobjc_classrefcount = 0;
    cobjc_stringcount = 0;

    cobjc_encodemethod = FALSE;
}

void CObjC_Cleanup(void) {}

static HashNameNode* CObjC_GetSelfName(void) {
    return self_name_node;
}

static char* CObjC_StringConcat(char* a, char* b, char* c) {
    char* buf;
    int len;

    len = 1;
    if (a) {
        len += strlen(a);
    }
    if (b) {
        len += strlen(b);
    }
    if (c) {
        len += strlen(c);
    }

    buf = galloc(len);
    len = 0;

    if (a) {
        strcpy(&buf[len], a);
        len += strlen(a);
    }
    if (b) {
        strcpy(&buf[len], b);
        len += strlen(b);
    }
    if (c) {
        strcpy(&buf[len], c);
        len += strlen(c);
    }
    buf[len] = 0;
    return buf;
}

static Object* CObjC_SectionString(char* str, Section section) {
    ObjCString* objcstr;
    Object* object;

    for (objcstr = cobjc_strings; objcstr; objcstr = objcstr->next) {
        if (objcstr->section == section && !strcmp(str, objcstr->str)) {
            return objcstr->object;
        }
    }

    object = CParser_NewCompilerDefDataObject();
    object->nspace = cscope_root;
    object->name = CParser_GetUniqueName();
    object->type = CDecl_NewArrayType(TYPE(&stchar), strlen(str) + 1);
    object->sclass = TK_STATIC;
    object->section = section;

    CInit_DeclareData(object, str, NULL, object->type->size);

    objcstr = galloc(sizeof(ObjCString));
    objcstr->next = cobjc_strings;
    cobjc_strings = objcstr;

    objcstr->str = str;
    objcstr->section = section;
    objcstr->object = object;

    return objcstr->object;
}

static ObjCProtocol* CObjC_FindProtocol(HashNameNode* name) {
    ObjCProtocol* prot;

    for (prot = cobjc_protocols; prot; prot = prot->next) {
        if (prot->name == name) {
            break;
        }
    }

    return prot;
}

static ObjCSelector* CObjC_FindSelector(HashNameNode* name) {
    ObjCSelector* sel;

    if (!cobjc_selhashtable) {
        return NULL;
    }

    for (sel = cobjc_selhashtable[name->hashval & 0x3FF]; sel; sel = sel->next) {
        if (sel->name == name) {
            break;
        }
    }

    return sel;
}

static ObjCSelector* CObjC_NewSelector(HashNameNode* name) {
    ObjCSelector* sel;
    ObjCSelector** ptr;

    if (!cobjc_selhashtable) {
        cobjc_selhashtable = galloc(sizeof(ObjCSelector*) * 0x400);
        memclrw(cobjc_selhashtable, sizeof(ObjCSelector*) * 0x400);
    }

    sel = galloc(sizeof(ObjCSelector));
    sel->selobject = NULL;
    sel->name = name;
    sel->methods = NULL;
    ptr = cobjc_selhashtable + (name->hashval & 0x3FF);
    sel->next = *ptr;
    *ptr = sel;
    return sel;
}

static ObjCSelector* CObjC_FindKeyArgSelector(ObjCNamedArg* arg) {
    HashNameNode* name;

    if (!arg->next && !arg->expr) {
        name = arg->name;
    } else {
        name_mangle_list.size = 0;
        while (arg) {
            if (arg->name) {
                AppendGListName(&name_mangle_list, arg->name->name);
            }
            AppendGListByte(&name_mangle_list, ':');
            arg = arg->next;
        }

        AppendGListByte(&name_mangle_list, 0);

        COS_LockHandle(name_mangle_list.data);
        name = GetHashNameNodeExport(*name_mangle_list.data);
        COS_UnlockHandle(name_mangle_list.data);
    }

    return CObjC_FindSelector(name);
}

static Boolean CObjC_IsSameType(Type* a, Type* b) {
    if (!copts.objc_strict && CObjC_IsCompatibleType(a, b)) {
        return TRUE;
    }

    return is_typesame(a, b);
}

static Boolean CObjC_IsSameMethod(ObjCMethod* a, ObjCMethod* b) {
    ObjCMethodArg* argA;
    ObjCMethodArg* argB;

    if (CObjC_IsSameType(a->return_type, b->return_type) &&
        a->return_qual == b->return_qual &&
        a->has_valist == b->has_valist) {
        argA = a->selector_args;
        argB = b->selector_args;

        while (1) {
            if (!argA) {
                return argB == NULL;
            }
            if (!argB) {
                return FALSE;
            }

            if (argA->selector != argB->selector || argA->qual != argB->qual) {
                return FALSE;
            }

            if (argA->type) {
                if (!argB->type || !CObjC_IsSameType(argB->type, argB->type)) {
                    return FALSE;
                }
            } else {
                if (argB->type) {
                    return FALSE;
                }
            }

            argA = argA->next;
            argB = argB->next;
        }
    }

    return FALSE;
}

static ObjCSelector* CObjC_MakeSelector(ObjCMethod* meth) {
    HashNameNode* selname;
    ObjCSelector* sel;
    ObjCMethodList* methlist;
    ObjCMethodArg* arg;

    if (!meth->selector_args->next && !meth->selector_args->type) {
        selname = meth->selector_args->selector;
    } else {
        name_mangle_list.size = 0;
        for (arg = meth->selector_args; arg; arg = arg->next) {
            if (arg->selector) {
                AppendGListName(&name_mangle_list, arg->selector->name);
            }
            AppendGListByte(&name_mangle_list, ':');
        }
        AppendGListByte(&name_mangle_list, 0);

        COS_LockHandle(name_mangle_list.data);
        selname = GetHashNameNodeExport(*name_mangle_list.data);
        COS_UnlockHandle(name_mangle_list.data);
    }

    if (!(sel = CObjC_FindSelector(selname))) {
        methlist = galloc(sizeof(ObjCMethodList));
        methlist->next = NULL;
        methlist->method = meth;

        sel = CObjC_NewSelector(selname);
        sel->methods = methlist;
    } else {
        for (methlist = sel->methods; methlist; methlist = methlist->next) {
            if (CObjC_IsSameMethod(methlist->method, meth)) {
                break;
            }
        }

        if (!methlist) {
            methlist = galloc(sizeof(ObjCMethodList));
            methlist->method = meth;
            methlist->next = sel->methods;
            sel->methods = methlist;
        }
    }

    meth->selector = sel;
    return sel;
}

static Object* CObjC_GetSelectorObject(ObjCSelector* sel) {
    Object* dataobj;
    Object* nameobj;

    OLinkList refs;
    char str[32];
    char buf[8];

    if (!sel->selobject) {
        nameobj = CObjC_SectionString(sel->name->name, SECT_OBJC_METH_VAR_NAMES);
        sprintf(str, "L_OBJC_SELECTOR_REFERENCES_%ld", cobjc_selrefcount++);

        dataobj = CParser_NewCompilerDefDataObject();
        dataobj->name = GetHashNameNodeExport(str);
        dataobj->sclass = TK_STATIC;
        dataobj->type = TYPE(&void_ptr);
        dataobj->section = SECT_OBJC_MESSAGE_REFS;

        if (CScope_GetLocalObject(cscope_root, dataobj->name)) {
            CError_Error(ERR_OBJECT_REDEFINED, dataobj);
        } else {
            CScope_AddGlobalObject(dataobj);
        }

        sel->selobject = dataobj;

        memclrw(buf, dataobj->type->size);
        refs.next = NULL;
        refs.obj = nameobj;
        refs.offset = 0;
        refs.addend = 0;
        CInit_DeclareData(dataobj, buf, &refs, dataobj->type->size);
    }

    return sel->selobject;
}

static Object* CObjC_GetClassRefObject(TypeClass* tclass) {
    Object* nameobj;
    Object* dataobj;

    char str[32];
    OLinkList refs;
    char buf[8];

    if (!tclass->objcinfo->classrefobj) {
        nameobj = CObjC_SectionString(tclass->classname->name, SECT_OBJC_CLASS_NAMES);
        sprintf(str, "L_OBJC_CLASS_REFERENCES_%ld", cobjc_classrefcount++);

        dataobj = CParser_NewCompilerDefDataObject();
        dataobj->name = GetHashNameNodeExport(str);
        dataobj->sclass = TK_STATIC;
        dataobj->type = TYPE(&void_ptr);
        dataobj->section = SECT_OBJC_CLS_REFS;

        if (CScope_GetLocalObject(cscope_root, dataobj->name)) {
            CError_Error(ERR_OBJECT_REDEFINED, dataobj);
        } else {
            CScope_AddGlobalObject(dataobj);
        }

        tclass->objcinfo->classrefobj = dataobj;

        memclrw(buf, dataobj->type->size);
        refs.next = NULL;
        refs.obj = nameobj;
        refs.offset = 0;
        refs.addend = 0;
        CInit_DeclareData(dataobj, buf, &refs, dataobj->type->size);
    }

    return tclass->objcinfo->classrefobj;
}

static Object* CObjC_MakeObject(char* prefix, char* suffix, SInt32 size) {
    Object* object = CParser_NewCompilerDefDataObject();
    object->name = CParser_NameConcat(prefix, suffix);
    object->type = CDecl_NewStructType(size, 4);
    CScope_AddObject(object->nspace, object->name, OBJ_BASE(object));
    return object;
}

static Object* CObjC_FindRTFunc(char* namestr, char* namestr2) {
    NameSpaceObjectList* list;
    HashNameNode* name;
    NameSpace* saveNSpace;
    Object* object;
    Boolean savecpp;

    savecpp = copts.cplusplus;
    name = GetHashNameNodeExport(namestr);

    if ((list = CScope_GetLocalObject(cscope_root, name))) {
        if (IS_TYPE_FUNC(OBJECT(list->object)->type)) {
            return OBJECT(list->object);
        }

        CError_Error(ERR_IDENTIFIER_REDECLARED, name);
    }

    copts.cplusplus = FALSE;

    saveNSpace = cscope_current;
    cscope_current = cscope_root;
    object = CParser_NewFunctionObject(NULL);
    cscope_current = saveNSpace;

    object->type = TYPE(&rt_func);
    object->name = name;
    if (!list) {
        CScope_AddObject(cscope_root, name, OBJ_BASE(object));
    }

    copts.cplusplus = savecpp;

    return object;
}

static Object* CObjC_FindSendMessageRTFunc(Boolean isSuper, Boolean isStret) {
    if (isSuper) {
        if (isStret) {
            return CObjC_FindRTFunc("objc_msgSendSuper_stret", "objc_msgSendSuper_stret");
        } else {
            return CObjC_FindRTFunc("objc_msgSendSuper", "objc_msgSendSuper");
        }
    } else {
        if (isStret) {
            return CObjC_FindRTFunc("objc_msgSend_stret", "objc_msgSend_stret");
        } else {
            return CObjC_FindRTFunc("objc_msgSend", "objc_msgSend");
        }
    }
}

typedef struct RawSymbols {
    UInt32 sel_ref_cnt;
    UInt32 refs;
    UInt16 cls_def_cnt;
    UInt16 cat_def_cnt;
    UInt32 offsets[1];
} RawSymbols;

void CObjC_GenerateModule(void) {
    int i;
    RawSymbols* symdata;
    SInt32 size;
    Object* object;
    OLinkList* refs;
    OLinkList* ref;
    int classCount;
    int categoryCount;
    ObjCCategoryEntry* catEntry;
    BClassList* classdef;
    UInt32 data[4];

    if (copts.objective_c || cobjc_classdefs) {
        for (classdef = cobjc_classdefs, classCount = 0; classdef; classdef = classdef->next) {
            classCount++;
        }
        for (catEntry = cobjc_categories, categoryCount = 0; catEntry; catEntry = catEntry->next) {
            categoryCount++;
        }

        size = sizeof(RawSymbols) + 4 * (classCount + categoryCount - 1);
        symdata = lalloc(size);
        memclrw(symdata, size);

        symdata->sel_ref_cnt = CTool_EndianConvertWord32(0);
        symdata->refs = CTool_EndianConvertWord32(0);
        symdata->sel_ref_cnt = CTool_EndianConvertWord16(0);
        symdata->cls_def_cnt = CTool_EndianConvertWord16(classCount);
        symdata->cat_def_cnt = CTool_EndianConvertWord16(categoryCount);

        refs = NULL;
        i = 0;
        for (classdef = cobjc_classdefs; classdef; classdef = classdef->next, i++) {
            ref = lalloc(sizeof(OLinkList));
            ref->next = refs;
            refs = ref;

            ref->obj = TYPE_CLASS(classdef->type)->objcinfo->classobject;
            ref->offset = ((char*)&symdata->offsets[i]) - ((char*)symdata);
            ref->addend = 0;
        }
        for (catEntry = cobjc_categories; catEntry; catEntry = catEntry->next, i++) {
            ref = lalloc(sizeof(OLinkList));
            ref->next = refs;
            refs = ref;

            ref->obj = catEntry->object;
            ref->offset = ((char*)&symdata->offsets[i]) - ((char*)symdata);
            ref->addend = 0;
        }

        object = CObjC_MakeObject("", "L_OBJC_SYMBOLS", size);
        object->sclass = TK_STATIC;
        object->section = SECT_OBJC_MSYMBOLS;
        CInit_DeclareData(object, symdata, refs, object->type->size);

        refs = NULL;

        data[0] = CTool_EndianConvertWord32(5);
        data[1] = CTool_EndianConvertWord32(16);
        data[2] = CTool_EndianConvertWord32(0);

        ref = lalloc(sizeof(OLinkList));
        ref->next = refs;
        refs = ref;
        ref->obj = CObjC_SectionString(CPrep_GetFileName(NULL, TRUE, FALSE), SECT_OBJC_CLASS_NAMES);
        ref->offset = 8;
        ref->addend = 0;

        data[3] = CTool_EndianConvertWord32(0);

        ref = lalloc(sizeof(OLinkList));
        ref->next = refs;
        refs = ref;
        ref->obj = object;
        ref->offset = 12;
        ref->addend = 0;

        object = CObjC_MakeObject("", "L_OBJC_MODULES", 16);
        object->sclass = TK_STATIC;
        object->section = SECT_OBJC_MODULE_INFO;
        CInit_DeclareData(object, data, refs, object->type->size);
    }
}

static TypeClass* CObjC_FindObjCClass(HashNameNode* name, Boolean reportMissing) {
    NameSpaceObjectList* list;

    list = CScope_FindName(cscope_root, name);
    if (!list ||
        list->object->otype != OT_TYPE ||
        !IS_TYPE_CLASS(OBJ_TYPE(list->object)->type) ||
        !TYPE_CLASS(OBJ_TYPE(list->object)->type)->objcinfo) {
        if (list || reportMissing) {
            CError_Error(ERR_NOT_OBJECTIVE_C_CLASS, name->name);
        }
        return NULL;
    } else {
        return TYPE_CLASS(OBJ_TYPE(list->object)->type);
    }
}

static Type* CObjC_FindObjCType(char* namestr, Boolean reportMissing) {
    NameSpaceObjectList* list;

    list = CScope_FindName(cscope_root, GetHashNameNodeExport(namestr));
    if (list && list->object->otype == OT_TYPE) {
        if (IS_TYPE_POINTER_ONLY(OBJ_TYPE(list->object)->type)) {
            return OBJ_TYPE(list->object)->type;
        }
        CError_Error(ERR_OBJECTIVE_C_TYPE_UNEXPECTED_TYPE, namestr);
    } else {
        if (reportMissing) {
            CError_Error(ERR_OBJECTIVE_C_TYPE_UNDEFINED_SHOULD_DEFINED, namestr);
        }
    }

    return NULL;
}

static Type* CObjC_GetObjCType_Class(Boolean reportMissing) {
    Type* type;

    if (cobjc_type_class) {
        return cobjc_type_class;
    }

    if (!(type = CObjC_FindObjCType("Class", reportMissing))) {
        return TYPE(&void_ptr);
    }

    return cobjc_type_class = type;
}

Type* CObjC_GetObjCType_id(Boolean reportMissing) {
    Type* type;

    if (cobjc_type_class) {
        return cobjc_type_id;
    }

    if (!(type = CObjC_FindObjCType("id", reportMissing))) {
        return TYPE(&void_ptr);
    }

    return cobjc_type_id = type;
}

Boolean CObjC_IsType_id(Type* type) {
    if (IS_TYPE_POINTER_ONLY(type)) {
        if (type == TYPE(&void_ptr)) {
            return FALSE;
        }

        type = TPTR_TARGET(type);
        if (type == TPTR_TARGET(CObjC_GetObjCType_id(FALSE))) {
            return TRUE;
        }
        if (type == TPTR_TARGET(CObjC_GetObjCType_Class(FALSE))) {
            return TRUE;
        }
    }

    return FALSE;
}

Boolean CObjC_IsCompatibleType(Type* a, Type* b) {
    Boolean a_is_id;
    Boolean b_is_id;

    if (IS_TYPE_POINTER_ONLY(a) && IS_TYPE_POINTER_ONLY(b)) {
        a_is_id = CObjC_IsType_id(a);
        b_is_id = CObjC_IsType_id(b);

        if (a_is_id && b_is_id) {
            return TRUE;
        }

        if (a_is_id && IS_TYPE_OBJC_CLASS(TPTR_TARGET(b))) {
            return TRUE;
        }

        if (b_is_id && IS_TYPE_OBJC_CLASS(TPTR_TARGET(a))) {
            return TRUE;
        }
    }

    return FALSE;
}

static Type* CObjC_FindObjCType_SEL(void) {
    Type* type;

    if (cobjc_type_sel) {
        return cobjc_type_sel;
    }

    if (!(type = CObjC_FindObjCType("SEL", TRUE))) {
        return TYPE(&void_ptr);
    }

    return cobjc_type_sel = type;
}

static Boolean CObjC_IsType_SEL(Type* type) {
    TypePointer* sel;

    if (!IS_TYPE_POINTER_ONLY(type)) {
        return FALSE;
    }

    type = TPTR_TARGET(type);

    sel = TYPE_POINTER(CObjC_FindObjCType_SEL());

    CError_ASSERT(847, IS_TYPE_POINTER_ONLY(sel));
    return type == sel->target;
}

static TypeClass* CObjC_NewObjCClass(HashNameNode* name) {
    NameSpaceObjectList* list;
    ObjCInfo* info;
    TypeClass* tclass;

    if ((list = CScope_FindName(cscope_root, name))) {
        if (list->object->otype != OT_TYPE ||
            !IS_TYPE_CLASS(tclass = TYPE_CLASS(OBJ_TYPE(list->object)->type)) ||
            !tclass->objcinfo) {
            CError_Error(ERR_IDENTIFIER_REDECLARED, name->name);
            return NULL;
        }

        return tclass;
    }

    info = galloc(sizeof(ObjCInfo));
    memclrw(info, sizeof(ObjCInfo));

    tclass = CDecl_DefineClass(cscope_root, name, NULL, CLASS_MODE_CLASS, TRUE, TRUE);
    tclass->flags |= CLASS_SINGLE_OBJECT;
    tclass->objcinfo = info;

    info->classobject = CObjC_MakeObject("L_OBJC_CLASS_", name->name, 40);
    info->classobject->section = SECT_OBJC_CLASS;

    info->metaobject = CObjC_MakeObject("L_OBJC_METACLASS_", name->name, 40);
    info->metaobject->section = SECT_OBJC_META_CLASS;

    return tclass;
}

static void CObjC_ParseTypeName(Type** type, UInt32* qual) {
    DeclInfo di;

    tk = lex();

    memclrw(&di, sizeof(di));
    CParser_GetDeclSpecs(&di, FALSE);
    scandeclarator(&di);

    if (tk != ')') {
        CError_ErrorSkip(ERR_RPAREN_EXPECTED);
    } else {
        tk = lex();
    }

    if (di.name) {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
    }

    *type = di.parserOption ? CObjC_GetObjCType_id(TRUE) : di.thetype;
    *qual = di.qual;
}

void CObjC_TranslateSelectorToken(void) {
    switch (tk) {
        case TK_CLASS:
            tkidentifier = GetHashNameNodeExport("class");
            tk = TK_IDENTIFIER;
            break;
        case TK_SELF:
            tkidentifier = GetHashNameNodeExport("self");
            tk = TK_IDENTIFIER;
            break;
        case TK_IN:
            tkidentifier = GetHashNameNodeExport("in");
            tk = TK_IDENTIFIER;
            break;
        case TK_BREAK:
            tkidentifier = GetHashNameNodeExport("break");
            tk = TK_IDENTIFIER;
            break;
        case TK_NEW:
            tkidentifier = GetHashNameNodeExport("new");
            tk = TK_IDENTIFIER;
            break;
        case TK_DELETE:
            tkidentifier = GetHashNameNodeExport("delete");
            tk = TK_IDENTIFIER;
            break;
    }
}

static ObjCMethod* CObjC_ParseMethod(Boolean is_global) {
    ObjCMethod* meth;
    ObjCMethodArg* arg;
    ObjCMethodArg** ptr;
    Boolean argflag;

    if (is_global) {
        meth = galloc(sizeof(ObjCMethod));
        memclrw(meth, sizeof(ObjCMethod));
    } else {
        meth = lalloc(sizeof(ObjCMethod));
        memclrw(meth, sizeof(ObjCMethod));
    }

    switch (tk) {
        case '+':
            meth->is_class_method = TRUE;
            break;
        case '-':
            meth->is_class_method = FALSE;
            break;
        default:
            CError_FATAL(976);
    }

    if ((tk = lex()) == '(') {
        CObjC_ParseTypeName(&meth->return_type, &meth->return_qual);
        CError_QualifierCheck(
            meth->return_qual & ~(Q_CONST | Q_VOLATILE | Q_BYCOPY | Q_BYREF | Q_ONEWAY)
        );
    } else {
        meth->return_type = CObjC_GetObjCType_id(TRUE);
    }

    ptr = &meth->selector_args;
    argflag = TRUE;
    while (1) {
        if (is_global) {
            arg = galloc(sizeof(ObjCMethodArg));
            memclrw(arg, sizeof(ObjCMethodArg));
        } else {
            arg = lalloc(sizeof(ObjCMethodArg));
            memclrw(arg, sizeof(ObjCMethodArg));
        }

        *ptr = arg;
        ptr = &arg->next;

        CObjC_TranslateSelectorToken();

        if (tk == TK_IDENTIFIER) {
            arg->selector = tkidentifier;
            if ((tk = lex()) != ':') {
                if (argflag) {
                    break;
                }

                CError_Error(ERR_COLON_EXPECTED);
                return NULL;
            }
        }

        if (tk != ':') {
            CError_Error(ERR_COLON_EXPECTED);
            return NULL;
        }

        if ((tk = lex()) == '(') {
            CObjC_ParseTypeName(&arg->type, &arg->qual);

            if (IS_TYPE_ARRAY(arg->type)) {
                arg->type = CDecl_NewPointerType(TPTR_TARGET(arg->type));
            }

            CError_QualifierCheck(
                arg->qual & ~(Q_CONST | Q_VOLATILE | Q_IN | Q_OUT | Q_INOUT | Q_BYCOPY | Q_BYREF)
            );
            if ((arg->qual & (Q_OUT | Q_INOUT)) && !IS_TYPE_POINTER_ONLY(arg->type)) {
                CError_QualifierCheck(arg->qual & (Q_OUT | Q_INOUT));
            }
        } else {
            arg->type = CObjC_GetObjCType_id(TRUE);
        }

        if (tk != TK_IDENTIFIER) {
            CObjC_TranslateSelectorToken();
            if (tk != TK_IDENTIFIER) {
                CError_Error(ERR_IDENTIFIER_EXPECTED);
                tkidentifier = no_name_node;
            }
        }

        arg->name = tkidentifier;

        if ((tk = lex()) == ',') {
            if ((tk = lex()) == TK_ELLIPSIS) {
                meth->has_valist = TRUE;
                tk = lex();
                break;
            }

            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        }

        CObjC_TranslateSelectorToken();
        if (tk != ':' && tk != TK_IDENTIFIER) {
            break;
        }

        argflag = FALSE;
    }

    return meth;
}

static ObjCMethod* CObjC_FindMethod(
    ObjCMethod* methods, ObjCMethod* wanted, Boolean checkSignature, Boolean quiet
) {
    ObjCMethod* meth;
    ObjCMethodArg* wanted_arg;
    ObjCMethodArg* meth_arg;

    for (meth = methods; meth; meth = meth->next) {
        if (wanted->is_class_method == meth->is_class_method) {
            wanted_arg = wanted->selector_args;
            meth_arg = meth->selector_args;
            while (1) {
                if (!wanted_arg || !meth_arg) {
                    if (wanted_arg) {
                        break;
                    }
                    if (meth_arg) {
                        break;
                    }

                    if (checkSignature) {
                        wanted_arg = wanted->selector_args;
                        meth_arg = meth->selector_args;
                        while (1) {
                            if (!wanted_arg) {
                                break;
                            }
                            if (wanted_arg->qual != meth_arg->qual) {
                                break;
                            }

                            if (wanted_arg->type) {
                                if (!meth_arg->type) {
                                    break;
                                }
                                if (!CObjC_IsSameType(wanted_arg->type, meth_arg->type)) {
                                    break;
                                }
                            } else {
                                if (meth_arg->type) {
                                    break;
                                }
                            }

                            meth_arg->name = wanted_arg->name;
                            wanted_arg = wanted_arg->next;
                            meth_arg = meth_arg->next;
                        }

                        if (wanted_arg ||
                            wanted->has_valist != meth->has_valist ||
                            wanted->return_qual != meth->return_qual ||
                            !CObjC_IsSameType(wanted->return_type, meth->return_type)) {
                            CError_Error(ERR_METHOD_REDECLARED, meth);
                        }
                    }

                    return meth;
                }

                if (wanted_arg->selector != meth_arg->selector) {
                    break;
                }
                if (!wanted_arg->type && meth_arg->type) {
                    break;
                }
                if (wanted_arg->type && !meth_arg->type) {
                    break;
                }

                wanted_arg = wanted_arg->next;
                meth_arg = meth_arg->next;
            }
        }
    }

    if (!quiet) {
        CError_Error(ERR_UNDEFINED_METHOD, wanted);
    }
    return NULL;
}

static ObjCMethod* CObjC_CloneMethod(ObjCMethod* meth) {
    ObjCMethod* copy;

    copy = galloc(sizeof(ObjCMethod));
    memclrw(copy, sizeof(ObjCMethod));

    copy->selector = meth->selector;
    copy->return_type = meth->return_type;
    copy->return_qual = meth->return_qual;
    copy->selector_args = meth->selector_args;
    copy->has_valist = meth->has_valist;
    copy->is_class_method = meth->is_class_method;
    copy->is_defined = FALSE;

    return copy;
}

static void CObjC_AddProtocolMethods(
    TypeClass* tclass, ObjCMethod** methods, ObjCProtocolList* protocols
) {
    ObjCMethod* meth;
    ObjCMethod* copy;

    while (protocols) {
        for (meth = protocols->protocol->methods; meth; meth = meth->next) {
            if (!CObjC_FindMethod(*methods, meth, TRUE, TRUE)) {
                copy = CObjC_CloneMethod(meth);
                copy->next = *methods;
                *methods = copy;
                if (tclass) {
                    CObjC_MakeSelector(copy);
                }
            }
        }
        protocols = protocols->next;
    }
}

static void CObjC_AppendArgument(TypeFunc* tfunc, HashNameNode* name, Type* type, UInt32 qual) {
    FuncArg* arg;

    if ((arg = tfunc->args)) {
        while (arg->next) {
            arg = arg->next;
        }
        arg->next = CParser_NewFuncArg();
        arg = arg->next;
    } else {
        tfunc->args = CParser_NewFuncArg();
        arg = tfunc->args;
    }

    arg->name = name;
    arg->type = type;
    arg->qual = qual;
}

static HashNameNode* CObjC_MangleMethodName(
    TypeClass* tclass, ObjCCategory* cat, ObjCMethod* meth
) {
    HashNameNode* name;
    ObjCMethodArg* arg;

    name_mangle_list.size = 0;

    AppendGListName(&name_mangle_list, "");

    if (meth->is_class_method) {
        AppendGListName(&name_mangle_list, "+[");
    } else {
        AppendGListName(&name_mangle_list, "-[");
    }

    AppendGListName(&name_mangle_list, tclass->classname->name);

    if (cat) {
        AppendGListByte(&name_mangle_list, '(');
        AppendGListName(&name_mangle_list, cat->name->name);
        AppendGListByte(&name_mangle_list, ')');
    }

    AppendGListByte(&name_mangle_list, ' ');

    for (arg = meth->selector_args; arg; arg = arg->next) {
        if (arg->selector) {
            AppendGListName(&name_mangle_list, arg->selector->name);
        }
        if (arg->type) {
            AppendGListByte(&name_mangle_list, ':');
        }
    }

    AppendGListID(&name_mangle_list, "]");

    COS_LockHandle(name_mangle_list.data);
    name = GetHashNameNodeExport(*name_mangle_list.data);
    COS_UnlockHandle(name_mangle_list.data);
    return name;
}

static TypeFunc* CObjC_GetMethodFuncType(ObjCMethod* meth) {
    ObjCMethodArg* metharg;
    TypeFunc* functype;
    FuncArg* funcarg;

    if (!meth->functype) {
        functype = galloc(sizeof(TypeFunc));
        memclrw(functype, sizeof(TypeFunc));

        functype->type = TYPEFUNC;
        functype->functype = meth->return_type;
        functype->qual = meth->return_qual;
        functype->flags = FUNC_OBJC_METHOD;
        CDecl_SetFuncFlags(functype, 1);

        CObjC_AppendArgument(
            functype, CObjC_GetSelfName(), TYPE(TYPE_POINTER(CObjC_GetObjCType_id(TRUE))), 0
        );
        CObjC_AppendArgument(functype, GetHashNameNodeExport("_cmd"), CObjC_FindObjCType_SEL(), 0);

        for (metharg = meth->selector_args; metharg; metharg = metharg->next) {
            if (metharg->type) {
                CObjC_AppendArgument(functype, metharg->name, metharg->type, metharg->qual);
            }
        }

        if (meth->has_valist) {
            for (funcarg = functype->args;; funcarg = funcarg->next) {
                if (!funcarg->next) {
                    funcarg->next = &elipsis;
                    break;
                }
            }
        }

        meth->functype = functype;
    }

    return meth->functype;
}

static Object* CObjC_GetMethodObject(TypeClass* tclass, ObjCCategory* cat, ObjCMethod* meth) {
    Object* object;
    Boolean saveCPP;
    NameSpace* saveNS;

    if (!meth->object) {
        saveCPP = copts.cplusplus;
        copts.cplusplus = FALSE;

        saveNS = cscope_current;
        cscope_current = cscope_root;

        object = CParser_NewFunctionObject(NULL);
        object->nspace = tclass->nspace;
        object->type = TYPE(CObjC_GetMethodFuncType(meth));
        object->name = CObjC_MangleMethodName(tclass, cat, meth);
        object->u.func.linkname = object->name;
        object->sclass = TK_STATIC;
        meth->object = object;

        cscope_current = saveNS;
        copts.cplusplus = saveCPP;
    }

    return meth->object;
}

static void CObjC_AddMethod(TypeClass* tclass) {
    ObjCMethod* meth;
    ObjCMethod* existing;

    if ((meth = CObjC_ParseMethod(TRUE))) {
        existing = CObjC_FindMethod(tclass->objcinfo->methods, meth, FALSE, TRUE);
        if (!existing) {
            meth->next = tclass->objcinfo->methods;
            tclass->objcinfo->methods = meth;
            CObjC_MakeSelector(meth);
        } else {
            if (copts.objc_strict || !CObjC_IsSameMethod(meth, existing)) {
                CError_Error(ERR_METHOD_REDECLARED, meth);
            }
        }
    }

    if (tk != ';') {
        CError_Error(ERR_SEMICOLON_EXPECTED);
    } else {
        tk = lex();
    }
}

static Boolean CObjC_IsSameProtocolList(ObjCProtocolList* a, ObjCProtocolList* b) {
    while (1) {
        if (!a) {
            return b == NULL;
        }

        if (!b || a->protocol != b->protocol) {
            return FALSE;
        }

        a = a->next;
        b = b->next;
    }
}

static ObjCProtocolList* CObjC_ParserProtocolList(void) {
    ObjCProtocolList* list;
    ObjCProtocolList* entry;
    ObjCProtocol* protocol;

    list = NULL;
    tk = lex();

    while (1) {
        if (tk != TK_IDENTIFIER) {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
            break;
        }

        if ((protocol = CObjC_FindProtocol(tkidentifier))) {
            for (entry = list; entry; entry = entry->next) {
                if (entry->protocol == protocol) {
                    break;
                }
            }

            if (!entry) {
                entry = galloc(sizeof(ObjCProtocolList));
                entry->next = list;
                entry->protocol = protocol;
                list = entry;
            } else {
                CError_Error(ERR_PROTOCOL_ALREADY_PROTOCOL_LIST, tkidentifier->name);
            }
        } else {
            CError_Error(ERR_PROTOCOL_UNDEFINED, tkidentifier->name);
        }

        if ((tk = lex()) == '>') {
            tk = lex();
            break;
        }

        if (tk != ',') {
            CError_Error(ERR_COMMA_EXPECTED);
            break;
        }

        tk = lex();
    }

    return list;
}

static void CObjC_AddClassMembers(TypeStruct* tstruct, TypeClass* tclass) {
    ObjMemberVar* ivar;
    StructMember* member;

    if (tclass->bases) {
        CObjC_AddClassMembers(tstruct, tclass->bases->base);
    }

    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
        member = galloc(sizeof(StructMember));
        memclrw(member, sizeof(StructMember));

        member->name = ivar->name;
        member->type = ivar->type;
        member->qual = ivar->qual;
        member->offset = ivar->offset;

        appendmember(tstruct, member);
    }
}

void CObjC_ParseDefs(TypeStruct* tstruct) {
    TypeClass* tclass;

    if ((tk = lex()) == '(') {
        if ((tk = lex()) == TK_IDENTIFIER) {
            if ((tclass = CObjC_FindObjCClass(tkidentifier, TRUE))) {
                if (tclass->flags & CLASS_COMPLETED) {
                    tstruct->size = tclass->size;
                    tstruct->align = tclass->align;
                    CObjC_AddClassMembers(tstruct, tclass);
                } else {
                    CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, tclass, 0);
                }
            }

            if ((tk = lex()) != ')') {
                CError_Error(ERR_RPAREN_EXPECTED);
            }

        } else {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
        }
    } else {
        CError_Error(ERR_LPAREN_EXPECTED);
    }
}

Type* CObjC_ParseID(void) {
    TypeObjCID* type;

    if ((tk = lex()) == '<') {
        type = galloc(sizeof(TypeObjCID));
        memclrw(type, sizeof(TypeObjCID));

        type->pointer = *TYPE_POINTER(CObjC_GetObjCType_id(TRUE));
        type->protocols = CObjC_ParserProtocolList();
        type->pointer.qual |= Q_IS_OBJC_ID;

        return TYPE(type);
    } else {
        return CObjC_GetObjCType_id(TRUE);
    }
}

Type* CObjC_ParseTypeProtocol(TypeClass* tclass) {
    CError_ASSERT(1526, tk == '<');
    CObjC_ParserProtocolList();
    return TYPE(tclass);
}

static void CObjC_ParseCategoryInterface(TypeClass* tclass) {
    ObjCCategory* cat;
    ObjCMethod* meth;

    if ((tk = lex()) != TK_IDENTIFIER) {
        CError_Error(ERR_IDENTIFIER_EXPECTED);
        return;
    }

    for (cat = tclass->objcinfo->categories; cat; cat = cat->next) {
        if (cat->name == tkidentifier) {
            CError_Error(ERR_CATEGORY_REDEFINED, tkidentifier->name);
            break;
        }
    }

    cat = galloc(sizeof(ObjCCategory));
    memclrw(cat, sizeof(ObjCCategory));

    cat->name = tkidentifier;

    if ((tk = lex()) != ')') {
        CError_Error(ERR_RPAREN_EXPECTED);
        return;
    }

    tk = lex();
    if (tk == '<') {
        cat->protocols = CObjC_ParserProtocolList();
        CObjC_AddProtocolMethods(tclass, &cat->methods, cat->protocols);
    }

    while (1) {
        switch (tk) {
            case '-':
            case '+':
                if ((meth = CObjC_ParseMethod(TRUE))) {
                    if (!CObjC_FindMethod(cat->methods, meth, FALSE, TRUE)) {
                        meth->next = cat->methods;
                        cat->methods = meth;
                        CObjC_MakeSelector(meth);
                    } else {
                        CError_Error(ERR_METHOD_REDECLARED, meth);
                    }
                }

                if (tk != ';') {
                    CError_Error(ERR_SEMICOLON_EXPECTED);
                } else {
                    tk = lex();
                }
                continue;

            case TK_AT_END:
                break;

            default:
                CParser_ParseGlobalDeclaration();
                continue;
        }

        break;
    }

    cat->next = tclass->objcinfo->categories;
    tclass->objcinfo->categories = cat;
}

static void CObjC_DefineMethod(TypeClass* tclass, ObjCCategory* category, ObjCMethod** methods) {
    ObjCMethod* meth;
    Object* object;
    ObjCMethod* parsed;
    FuncArg* funcarg;
    ObjCMethodArg* selarg;
    DeclInfo di;

    parsed = CObjC_ParseMethod(TRUE);
    if (!parsed) {
        return;
    }

    meth = CObjC_FindMethod(*methods, parsed, TRUE, TRUE);
    if (!meth) {
        meth = parsed;
        parsed->next = *methods;
        *methods = parsed;
        CObjC_MakeSelector(parsed);
    }

    if (meth->is_defined) {
        CError_Error(ERR_METHOD_REDEFINED, meth);
    }

    object = CObjC_GetMethodObject(tclass, category, meth);
    CError_ASSERT(1627, IS_TYPE_FUNC(object->type));

    if ((funcarg = TYPE_FUNC(object->type)->args)) {
        funcarg->type = CDecl_NewPointerType(TYPE(tclass));

        if ((funcarg = funcarg->next) && (funcarg = funcarg->next)) {
            selarg = parsed->selector_args;
            while (selarg && funcarg) {
                funcarg->name = selarg->name;
                funcarg->type = selarg->type;
                funcarg->qual |= selarg->qual;
                selarg = selarg->next;
                funcarg = funcarg->next;
            }
        }
    }

    if (tk == ';') {
        if (copts.objc_strict) {
            CError_Warning(ERR_LBRACE_EXPECTED);
        }
        tk = lex();
    }

    memclrw(&di, sizeof(di));
    CFunc_ParseFuncDef(object, &di, tclass, TRUE, meth->is_class_method, NULL);
    meth->is_defined = TRUE;

    tk = lex();
}

static void CObjC_EncodeTypeStruct(TypeStruct* tstruct, Boolean expandMembers) {
    StructMember* member;

    AppendGListByte(&name_mangle_list, (tstruct->stype == STRUCT_TYPE_UNION) ? '(' : '{');

    if (cobjc_encodemethod) {
        AppendGListByte(&name_mangle_list, '?');
    } else if (tstruct->name) {
        AppendGListName(&name_mangle_list, tstruct->name->name);
    }

    if (expandMembers) {
        AppendGListByte(&name_mangle_list, '=');
        for (member = tstruct->members; member; member = member->next) {
            CObjC_EncodeType(member->type, member->qual, TRUE);
        }
    }

    AppendGListByte(&name_mangle_list, (tstruct->stype == STRUCT_TYPE_UNION) ? ')' : '}');
}

static void CObjC_EncodeTypeClass(TypeClass* tclass, Boolean expandMembers) {
    ObjMemberVar* ivar;

    if (CClass_IsPODClass(tclass)) {
        AppendGListByte(&name_mangle_list, (tclass->mode == CLASS_MODE_UNION) ? '(' : '{');

        if (cobjc_encodemethod) {
            AppendGListByte(&name_mangle_list, '?');
        } else if (tclass->classname) {
            AppendGListName(&name_mangle_list, tclass->classname->name);
        }

        if (expandMembers) {
            AppendGListByte(&name_mangle_list, '=');
            for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
                CObjC_EncodeType(ivar->type, ivar->qual, TRUE);
            }
        }

        AppendGListByte(&name_mangle_list, (tclass->mode == CLASS_MODE_UNION) ? ')' : '}');
    } else {
        AppendGListByte(&name_mangle_list, '?');
    }
}

static void CObjC_EncodeTypeMethod(ObjCMethod* meth, Boolean expandMembers) {
    ObjCMethodArg* arg;

    char buf[16];

    if (meth->return_qual & Q_IN) {
        AppendGListByte(&name_mangle_list, 'n');
    }
    if (meth->return_qual & Q_OUT) {
        AppendGListByte(&name_mangle_list, 'o');
    }
    if (meth->return_qual & Q_INOUT) {
        AppendGListByte(&name_mangle_list, 'N');
    }
    if (meth->return_qual & Q_BYCOPY) {
        AppendGListByte(&name_mangle_list, 'O');
    }
    if (meth->return_qual & Q_ONEWAY) {
        AppendGListByte(&name_mangle_list, 'V');
    }

    if (meth->return_type) {
        CObjC_EncodeType(meth->return_type, 0, expandMembers);
    } else {
        AppendGListByte(&name_mangle_list, '@');
    }

    sprintf(buf, "%ld", CodeGen_objc_method_args_size(meth));
    AppendGListName(&name_mangle_list, buf);

    AppendGListByte(&name_mangle_list, '@');

    sprintf(buf, "%ld", CodeGen_objc_method_self_offset(meth));
    AppendGListName(&name_mangle_list, buf);

    AppendGListByte(&name_mangle_list, ':');

    sprintf(buf, "%ld", CodeGen_objc_method_sel_offset(meth));
    AppendGListName(&name_mangle_list, buf);

    for (arg = meth->selector_args; arg; arg = arg->next) {
        if (arg->type) {
            if (arg->qual & Q_CONST) {
                AppendGListByte(&name_mangle_list, 'r');
            }

            CObjC_EncodeType(arg->type, 0, expandMembers);

            sprintf(buf, "%ld", CodeGen_objc_method_arg_offset(meth, arg));
            AppendGListName(&name_mangle_list, buf);
        }
    }
}

static void CObjC_EncodeType(Type* type, UInt32 qual, Boolean expandMembers) {
    char buf[16];

    while (1) {
        switch (type->type) {
            case TYPEVOID:
                AppendGListByte(&name_mangle_list, 'v');
                return;

            case TYPEINT:
            case TYPEFLOAT:
                switch (TYPE_INTEGRAL(type)->integral) {
                    case IT_BOOL:
                        AppendGListByte(&name_mangle_list, 'C');
                        return;
                    case IT_CHAR:
                        AppendGListByte(&name_mangle_list, copts.unsigned_char ? 'C' : 'c');
                        return;
                    case IT_UCHAR:
                        AppendGListByte(&name_mangle_list, 'C');
                        return;
                    case IT_SCHAR:
                        AppendGListByte(&name_mangle_list, 'c');
                        return;
                    case IT_WCHAR_T:
                        AppendGListByte(&name_mangle_list, 'S');
                        return;
                    case IT_SHORT:
                        AppendGListByte(&name_mangle_list, 's');
                        return;
                    case IT_USHORT:
                        AppendGListByte(&name_mangle_list, 'S');
                        return;
                    case IT_INT:
                        AppendGListByte(&name_mangle_list, 'i');
                        return;
                    case IT_UINT:
                        AppendGListByte(&name_mangle_list, 'I');
                        return;
                    case IT_LONG:
                        AppendGListByte(&name_mangle_list, 'l');
                        return;
                    case IT_ULONG:
                        AppendGListByte(&name_mangle_list, 'L');
                        return;
                    case IT_LONGLONG:
                        AppendGListByte(&name_mangle_list, 'q');
                        return;
                    case IT_ULONGLONG:
                        AppendGListByte(&name_mangle_list, 'Q');
                        return;
                    case IT_FLOAT:
                        AppendGListByte(&name_mangle_list, 'f');
                        return;
                    case IT_SHORTDOUBLE:
                        AppendGListByte(&name_mangle_list, 'd');
                        return;
                    case IT_DOUBLE:
                        AppendGListByte(&name_mangle_list, 'd');
                        return;
                    case IT_LONGDOUBLE:
                        AppendGListByte(&name_mangle_list, 'D');
                        return;
                    default:
                        CError_FATAL(1841);
                }

            case TYPEENUM:
                type = TYPE_ENUM(type)->enumtype;
                continue;

            case TYPEPOINTER:
                if (CObjC_IsType_id(type)) {
                    AppendGListByte(&name_mangle_list, '@');
                    return;
                }
                if (CObjC_IsType_SEL(type)) {
                    AppendGListByte(&name_mangle_list, ':');
                    return;
                }
                type = TPTR_TARGET(type);
                if (type == TYPE(&stchar)) {
                    AppendGListByte(&name_mangle_list, '*');
                    return;
                }
                if (IS_TYPE_CLASS(type) && TYPE_CLASS(type)->objcinfo) {
                    AppendGListByte(&name_mangle_list, '@');
                    return;
                }
                AppendGListByte(&name_mangle_list, '^');
                expandMembers = cobjc_encodemethod;
                continue;

            case TYPEARRAY:
                AppendGListByte(&name_mangle_list, '[');
                if (TPTR_TARGET(type)->size) {
                    sprintf(buf, "%ld", type->size / TPTR_TARGET(type)->size);
                    AppendGListName(&name_mangle_list, buf);
                } else {
                    AppendGListByte(&name_mangle_list, '0');
                }
                CObjC_EncodeType(TPTR_TARGET(type), 0, TRUE);
                AppendGListByte(&name_mangle_list, ']');
                return;

            case TYPEBITFIELD:
                AppendGListByte(&name_mangle_list, 'b');
                sprintf(buf, "%ld", (SInt32)TYPE_BITFIELD(type)->bitlength);
                AppendGListName(&name_mangle_list, buf);
                return;

            case TYPESTRUCT:
                CObjC_EncodeTypeStruct(TYPE_STRUCT(type), expandMembers);
                return;

            case TYPECLASS:
                CObjC_EncodeTypeClass(TYPE_CLASS(type), expandMembers);
                return;

            case TYPEFUNC:
            case TYPETEMPLATE:
            case TYPEMEMBERPOINTER:
                AppendGListByte(&name_mangle_list, '?');
                return;

            default:
                CError_FATAL(1892);
        }
        break;
    }
}

static char* CObjC_EncodeMethodTypeString(ObjCMethod* meth, int unused) {
    char* buf;

    cobjc_encodemethod = TRUE;

    name_mangle_list.size = 0;
    CObjC_EncodeTypeMethod(meth, TRUE);
    AppendGListByte(&name_mangle_list, 0);

    buf = galloc(name_mangle_list.size);
    memcpy(buf, *name_mangle_list.data, name_mangle_list.size);

    cobjc_encodemethod = FALSE;

    return buf;
}

static char* CObjC_EncodeTypeString(Type* type, UInt32 qual) {
    char* buf;

    name_mangle_list.size = 0;
    CObjC_EncodeType(type, qual, TRUE);
    AppendGListByte(&name_mangle_list, 0);

    buf = galloc(name_mangle_list.size);
    memcpy(buf, *name_mangle_list.data, name_mangle_list.size);

    return buf;
}

typedef struct RawIVar {
    UInt32 name;
    UInt32 typestring;
    UInt32 offset;
} RawIVar;
typedef struct RawIVarList {
    UInt32 count;
    RawIVar ivars[1];
} RawIVarList;

static Object* CObjC_DefineIVarListObject(TypeClass* tclass) {
    Object* object;
    RawIVarList* data;
    ObjMemberVar* ivar;
    OLinkList* refs;
    OLinkList* ref;
    RawIVar* rawvar;
    int i;
    SInt32 size;
    char* str;

    for (ivar = tclass->ivars, i = 0; ivar; ivar = ivar->next) {
        i++;
    }

    if (i) {
        size = sizeof(RawIVarList) + sizeof(RawIVar) * (i - 1);
        data = lalloc(size);
        memclrw(data, size);

        object = CObjC_MakeObject("L_OBJC_INSTANCE_VARIABLES_", tclass->classname->name, size);
        object->sclass = TK_STATIC;
        object->section = SECT_OBJC_INSTANCE_VARS;

        refs = NULL;
        data->count = CTool_EndianConvertWord32(i);

        for (ivar = tclass->ivars, rawvar = data->ivars; ivar; rawvar++, ivar = ivar->next) {
            ref = lalloc(sizeof(OLinkList));
            ref->next = refs;
            refs = ref;
            ref->obj = CObjC_SectionString(ivar->name->name, SECT_OBJC_METH_VAR_NAMES);
            ref->offset = ((char*)&rawvar->name) - ((char*)data);
            ref->addend = 0;

            str = CObjC_EncodeTypeString(ivar->type, ivar->qual);

            ref = lalloc(sizeof(OLinkList));
            ref->next = refs;
            refs = ref;
            ref->obj = CObjC_SectionString(str, SECT_OBJC_METH_VAR_TYPES);
            ref->offset = ((char*)&rawvar->typestring) - ((char*)data);
            ref->addend = 0;

            rawvar->offset = CTool_EndianConvertWord32(ivar->offset);
        }

        CInit_DeclareData(object, data, refs, object->type->size);
    } else {
        object = NULL;
    }

    return object;
}

typedef struct RawMethod {
    UInt32 name;
    UInt32 typestring;
    UInt32 offset;
} RawMethod;
typedef struct RawMethodList {
    UInt32 obsolete;
    UInt32 count;
    RawMethod methods[1];
} RawMethodList;

static Object* CObjC_DefineMethodListObject(
    TypeClass* tclass,
    ObjCCategory* cat,
    ObjCMethod* methods,
    char* prefix,
    char* suffix,
    Section sectionID,
    Boolean doing_class_methods
) {
    Object* object;
    RawMethodList* data;
    ObjCMethod* meth;
    OLinkList* refs;
    OLinkList* ref;
    RawMethod* rawmeth;
    int i;
    SInt32 size;

    for (meth = methods, i = 0; meth; meth = meth->next) {
        if (doing_class_methods == meth->is_class_method) {
            i++;
        }
    }

    if (i) {
        size = sizeof(RawMethodList) + sizeof(RawMethod) * (i - 1);
        data = lalloc(size);
        memclrw(data, size);

        object = CObjC_MakeObject(prefix, suffix, size);
        object->sclass = TK_STATIC;
        object->section = sectionID;

        refs = NULL;
        data->count = CTool_EndianConvertWord32(i);

        for (meth = methods, rawmeth = data->methods; meth; meth = meth->next) {
            if (doing_class_methods == meth->is_class_method) {
                ref = lalloc(sizeof(OLinkList));
                ref->next = refs;
                refs = ref;
                ref->obj =
                    CObjC_SectionString(meth->selector->name->name, SECT_OBJC_METH_VAR_NAMES);
                ref->offset = ((char*)&rawmeth->name) - ((char*)data);
                ref->addend = 0;

                ref = lalloc(sizeof(OLinkList));
                ref->next = refs;
                refs = ref;
                ref->obj = CObjC_SectionString(
                    CObjC_EncodeMethodTypeString(meth, 0), SECT_OBJC_METH_VAR_TYPES
                );
                ref->offset = ((char*)&rawmeth->typestring) - ((char*)data);
                ref->addend = 0;

                ref = lalloc(sizeof(OLinkList));
                ref->next = refs;
                refs = ref;
                ref->obj = CObjC_GetMethodObject(tclass, cat, meth);
                ref->offset = ((char*)&rawmeth->offset) - ((char*)data);
                ref->addend = 0;

                rawmeth++;
            }
        }

        CInit_DeclareData(object, data, refs, object->type->size);
    } else {
        object = NULL;
    }

    return object;
}

typedef struct RawProtocolList {
    UInt32 offset;
} RawProtocolList;
typedef struct RawProtocolListList {
    UInt32 next;
    UInt32 count;
    RawProtocolList lists[1];
} RawProtocolListList;

static Object* CObjC_DefineProtocolListObject(ObjCProtocolList* list, char* str) {
    Object* object;
    RawProtocolListList* data;
    ObjCProtocolList* scan;
    OLinkList* ref;
    OLinkList* refs;
    int i;
    int j;
    SInt32 size;

    for (scan = list, i = 0; scan; scan = scan->next) {
        i++;
    }

    if (i) {
        size = sizeof(RawProtocolListList) + sizeof(RawProtocolList) * (i - 1);
        data = lalloc(size);
        memclrw(data, size);

        object = CObjC_MakeObject("L_OBJC_PROTOCOLS_", str, size);
        object->sclass = TK_STATIC;
        object->section = SECT_OBJC_PROTOCOL;

        refs = NULL;
        data->count = CTool_EndianConvertWord32(i);

        for (scan = list, j = 0; scan; scan = scan->next, j++) {
            ref = lalloc(sizeof(OLinkList));
            ref->next = refs;
            refs = ref;
            ref->obj = CObjC_GetProtocolObject(scan->protocol);
            ref->offset = ((char*)&data->lists[j].offset) - ((char*)data);
            ref->addend = 0;
        }

        CInit_DeclareData(object, data, refs, object->type->size);
    } else {
        object = NULL;
    }

    return object;
}

typedef struct RawProtocolMethodList {
    UInt32 name;
    UInt32 typestring;
} RawProtocolMethodList;
typedef struct RawProtocolMethodListList {
    UInt32 count;
    RawProtocolMethodList methods[1];
} RawProtocolMethodListList;

static Object* CObjC_DefineProtocolMethodListObject(
    ObjCProtocol* protocol, char* name, Section sectionID, Boolean doing_class_methods
) {
    Object* object;
    ObjCMethod* meth;
    OLinkList* refs;
    OLinkList* ref;
    RawProtocolMethodList* rawmeth;
    RawProtocolMethodListList* data;
    int i;
    SInt32 size;

    for (meth = protocol->methods, i = 0; meth; meth = meth->next) {
        if (doing_class_methods == meth->is_class_method) {
            i++;
        }
    }

    if (i) {
        size = sizeof(RawProtocolMethodListList) + sizeof(RawProtocolMethodList) * (i - 1);
        data = lalloc(size);
        memclrw(data, size);

        object = CObjC_MakeObject(name, protocol->name->name, size);
        object->sclass = TK_STATIC;
        object->section = sectionID;

        refs = NULL;
        data->count = CTool_EndianConvertWord32(i);

        for (meth = protocol->methods, rawmeth = data->methods; meth; meth = meth->next) {
            if (doing_class_methods == meth->is_class_method) {
                ref = lalloc(sizeof(OLinkList));
                ref->next = refs;
                refs = ref;
                ref->obj =
                    CObjC_SectionString(meth->selector->name->name, SECT_OBJC_METH_VAR_NAMES);
                ref->offset = ((char*)&rawmeth->name) - ((char*)data);
                ref->addend = 0;

                ref = lalloc(sizeof(OLinkList));
                ref->next = refs;
                refs = ref;
                ref->obj = CObjC_SectionString(
                    CObjC_EncodeMethodTypeString(meth, 0), SECT_OBJC_METH_VAR_TYPES
                );
                ref->offset = ((char*)&rawmeth->typestring) - ((char*)data);
                ref->addend = 0;

                rawmeth++;
            }
        }

        CInit_DeclareData(object, data, refs, object->type->size);
    } else {
        object = NULL;
    }

    return object;
}

typedef struct RawProtocol {
    UInt32 isa;
    UInt32 protocol_name;
    UInt32 protocol_list;
    UInt32 instance_methods;
    UInt32 class_methods;
} RawProtocol;

static Object* CObjC_GetProtocolObject(ObjCProtocol* protocol) {
    TypeClass* protocolType;
    OLinkList* refs;
    OLinkList* ref;
    Object* object;
    char* str;
    RawProtocol data;

    refs = NULL;

    if (!protocol->object) {
        if ((protocolType = CObjC_FindObjCClass(GetHashNameNodeExport("Protocol"), TRUE))) {
            data.isa = CTool_EndianConvertWord32(0);

            ref = lalloc(sizeof(OLinkList));
            ref->next = refs;
            refs = ref;
            ref->obj = protocolType->objcinfo->classobject;
            ref->offset = 0;
            ref->addend = 0;

            data.protocol_name = CTool_EndianConvertWord32(0);

            ref = lalloc(sizeof(OLinkList));
            ref->next = refs;
            refs = ref;
            ref->obj = CObjC_SectionString(protocol->name->name, SECT_OBJC_CLASS_NAMES);
            ref->offset = 4;
            ref->addend = 0;

            data.protocol_list = CTool_EndianConvertWord32(0);

            if (protocol->protocols) {
                str = CObjC_StringConcat(protocol->name->name, "_PROTOCOLS", NULL);

                ref = lalloc(sizeof(OLinkList));
                ref->next = refs;
                refs = ref;
                ref->obj = CObjC_DefineProtocolListObject(protocol->protocols, str);
                ref->offset = 8;
                ref->addend = 0;
            }

            data.instance_methods = CTool_EndianConvertWord32(0);

            object = CObjC_DefineProtocolMethodListObject(
                protocol, "L_OBJC_PROTOCOL_INSTANCE_METHODS_", SECT_OBJC_CAT_INST_METH, FALSE
            );
            if (object) {
                ref = lalloc(sizeof(OLinkList));
                ref->next = refs;
                refs = ref;
                ref->obj = object;
                ref->offset = 0xC;
                ref->addend = 0;
            }

            data.class_methods = CTool_EndianConvertWord32(0);

            object = CObjC_DefineProtocolMethodListObject(
                protocol, "L_OBJC_PROTOCOL_CLASS_METHODS_", SECT_OBJC_CAT_CLS_METH, TRUE
            );
            if (object) {
                ref = lalloc(sizeof(OLinkList));
                ref->next = refs;
                refs = ref;
                ref->obj = object;
                ref->offset = 0x10;
                ref->addend = 0;
            }

            protocol->object = object =
                CObjC_MakeObject("L_OBJC_PROTOCOL_", protocol->name->name, sizeof(data));
            object->type = TYPE(protocolType);
            object->sclass = TK_STATIC;
            object->section = SECT_OBJC_PROTOCOL;
            if (object->type->size != sizeof(data)) {
                if (object->type->size == 0) {
                    object->type = CDecl_NewStructType(sizeof(data), 4);
                } else {
                    CError_FATAL(2262);
                }
            }

            CInit_DeclareData(object, &data, refs, object->type->size);
            object->type = TYPE(protocolType);
        }

        if (!protocol->object) {
            protocol->object = CObjC_SectionString(protocol->name->name, SECT_OBJC_PROTOCOL);
        }
    }

    return protocol->object;
}

static void CObjC_DefineClassObjects(TypeClass* tclass) {
    Object* metaobject;
    Object* classobject;
    Object* classnameobj;
    OLinkList* refs;
    Object* protocollistobj;
    OLinkList* ref;
    Object* object;
    UInt32 data[10];

    metaobject = tclass->objcinfo->metaobject;
    classobject = tclass->objcinfo->classobject;
    classnameobj = CObjC_SectionString(tclass->classname->name, SECT_OBJC_CLASS_NAMES);
    protocollistobj =
        CObjC_DefineProtocolListObject(tclass->objcinfo->protocols, tclass->classname->name);

    refs = NULL;

    data[0] = CTool_EndianConvertWord32(0);
    data[1] = CTool_EndianConvertWord32(0);

    if (tclass->bases) {
        ref = lalloc(sizeof(OLinkList));
        ref->next = refs;
        refs = ref;
        ref->obj = tclass->bases->base->objcinfo->metaobject;
        ref->offset = 4;
        ref->addend = 0;
    }

    ref = lalloc(sizeof(OLinkList));
    ref->next = refs;
    refs = ref;
    ref->obj = metaobject;
    ref->offset = 0;
    ref->addend = 0;

    data[2] = CTool_EndianConvertWord32(0);

    ref = lalloc(sizeof(OLinkList));
    ref->next = refs;
    refs = ref;
    ref->obj = classnameobj;
    ref->offset = 8;
    ref->addend = 0;

    data[3] = CTool_EndianConvertWord32(0);
    data[4] = CTool_EndianConvertWord32(2);
    data[5] = CTool_EndianConvertWord32(sizeof(data));
    data[6] = CTool_EndianConvertWord32(0);
    data[7] = CTool_EndianConvertWord32(0);

    object = CObjC_DefineMethodListObject(
        tclass,
        NULL,
        tclass->objcinfo->methods,
        "L_OBJC_CLASS_METHODS_",
        tclass->classname->name,
        SECT_OBJC_CLS_METH,
        TRUE
    );
    if (object) {
        ref = lalloc(sizeof(OLinkList));
        ref->next = refs;
        refs = ref;
        ref->obj = object;
        ref->offset = 0x1C;
        ref->addend = 0;
    }

    data[8] = CTool_EndianConvertWord32(0);
    data[9] = CTool_EndianConvertWord32(0);

    if (protocollistobj) {
        ref = lalloc(sizeof(OLinkList));
        ref->next = refs;
        refs = ref;
        ref->obj = protocollistobj;
        ref->offset = 0x24;
        ref->addend = 0;
    }

    CError_ASSERT(2367, metaobject->type->size == sizeof(data));
    CInit_DeclareData(metaobject, data, refs, metaobject->type->size);

    refs = NULL;

    data[0] = CTool_EndianConvertWord32(0);

    ref = lalloc(sizeof(OLinkList));
    ref->next = refs;
    refs = ref;
    ref->obj = metaobject;
    ref->offset = 0;
    ref->addend = 0;

    data[1] = CTool_EndianConvertWord32(0);

    if (tclass->bases) {
        ref = lalloc(sizeof(OLinkList));
        ref->next = refs;
        refs = ref;
        ref->obj = tclass->bases->base->objcinfo->classobject;
        ref->offset = 4;
        ref->addend = 0;
    }

    data[2] = CTool_EndianConvertWord32(0);

    ref = lalloc(sizeof(OLinkList));
    ref->next = refs;
    refs = ref;
    ref->obj = classnameobj;
    ref->offset = 8;
    ref->addend = 0;

    data[3] = CTool_EndianConvertWord32(0);
    data[4] = CTool_EndianConvertWord32(1);
    data[5] = CTool_EndianConvertWord32(tclass->size);
    data[6] = CTool_EndianConvertWord32(0);

    object = CObjC_DefineIVarListObject(tclass);
    if (object) {
        ref = lalloc(sizeof(OLinkList));
        ref->next = refs;
        refs = ref;
        ref->obj = object;
        ref->offset = 0x18;
        ref->addend = 0;
    }

    data[7] = CTool_EndianConvertWord32(0);

    object = CObjC_DefineMethodListObject(
        tclass,
        NULL,
        tclass->objcinfo->methods,
        "L_OBJC_INSTANCE_METHODS_",
        tclass->classname->name,
        SECT_OBJC_INST_METH,
        FALSE
    );
    if (object) {
        ref = lalloc(sizeof(OLinkList));
        ref->next = refs;
        refs = ref;
        ref->obj = object;
        ref->offset = 0x1C;
        ref->addend = 0;
    }

    data[8] = CTool_EndianConvertWord32(0);
    data[9] = CTool_EndianConvertWord32(0);

    if (protocollistobj) {
        ref = lalloc(sizeof(OLinkList));
        ref->next = refs;
        refs = ref;
        ref->obj = protocollistobj;
        ref->offset = 0x24;
        ref->addend = 0;
    }

    CError_ASSERT(2437, classobject->type->size == sizeof(data));
    CInit_DeclareData(classobject, data, refs, classobject->type->size);
}

static void CObjC_DefineCategoryObjects(TypeClass* tclass, ObjCCategory* cat) {
    OLinkList* refs;
    Object* categoryObj;
    char* namestr;
    OLinkList* ref;
    Object* object;
    ObjCCategoryEntry* entry;
    UInt32 data[5];

    namestr = CObjC_StringConcat(tclass->classname->name, "_", cat->name->name);

    categoryObj = CObjC_MakeObject("L_OBJC_CATEGORY_", namestr, sizeof(data));
    categoryObj->sclass = TK_STATIC;
    categoryObj->section = SECT_OBJC_CATEGORY;

    refs = NULL;

    data[0] = CTool_EndianConvertWord32(0);

    ref = lalloc(sizeof(OLinkList));
    ref->next = refs;
    refs = ref;
    ref->obj = CObjC_SectionString(cat->name->name, SECT_OBJC_CLASS_NAMES);
    ref->offset = 0;
    ref->addend = 0;

    data[1] = CTool_EndianConvertWord32(0);

    ref = lalloc(sizeof(OLinkList));
    ref->next = refs;
    refs = ref;
    ref->obj = CObjC_SectionString(tclass->classname->name, SECT_OBJC_CLASS_NAMES);
    ref->offset = 4;
    ref->addend = 0;

    data[2] = CTool_EndianConvertWord32(0);

    object = CObjC_DefineMethodListObject(
        tclass,
        cat,
        cat->methods,
        "L_OBJC_CATEGORY_INSTANCE_METHODS_",
        namestr,
        SECT_OBJC_CAT_INST_METH,
        FALSE
    );
    if (object) {
        ref = lalloc(sizeof(OLinkList));
        ref->next = refs;
        refs = ref;
        ref->obj = object;
        ref->offset = 8;
        ref->addend = 0;
    }

    data[3] = CTool_EndianConvertWord32(0);

    object = CObjC_DefineMethodListObject(
        tclass,
        cat,
        cat->methods,
        "L_OBJC_CATEGORY_CLASS_METHODS_",
        namestr,
        SECT_OBJC_CAT_CLS_METH,
        TRUE
    );
    if (object) {
        ref = lalloc(sizeof(OLinkList));
        ref->next = refs;
        refs = ref;
        ref->obj = object;
        ref->offset = 0xC;
        ref->addend = 0;
    }

    data[4] = CTool_EndianConvertWord32(0);

    if (cat->protocols) {
        ref = lalloc(sizeof(OLinkList));
        ref->next = refs;
        refs = ref;
        ref->obj = CObjC_DefineProtocolListObject(cat->protocols, namestr);
        ref->offset = 0x10;
        ref->addend = 0;
    }

    CInit_DeclareData(categoryObj, data, refs, categoryObj->type->size);

    entry = galloc(sizeof(ObjCCategoryEntry));
    entry->category = cat;
    entry->object = categoryObj;
    entry->next = cobjc_categories;
    cobjc_categories = entry;
}

static void CObjC_ParseCategoryImplementation(TypeClass* tclass) {
    ObjCCategory* cat;
    ObjCMethod* meth;

    if ((tk = lex()) != TK_IDENTIFIER) {
        CError_Error(ERR_IDENTIFIER_EXPECTED);
        return;
    }

    for (cat = tclass->objcinfo->categories; cat; cat = cat->next) {
        if (cat->name == tkidentifier) {
            break;
        }
    }

    if (!cat) {
        if (copts.objc_strict) {
            CError_Warning(ERR_CATEGORY_UNDEFINED, tkidentifier->name);
        }

        cat = galloc(sizeof(ObjCCategory));
        memclrw(cat, sizeof(ObjCCategory));

        cat->name = tkidentifier;
        cat->next = tclass->objcinfo->categories;
        tclass->objcinfo->categories = cat;
    }

    if ((tk = lex()) != ')') {
        CError_Error(ERR_RPAREN_EXPECTED);
        return;
    }

    tk = lex();
    while (1) {
        switch (tk) {
            case '-':
            case '+':
                CObjC_DefineMethod(tclass, cat, &cat->methods);
                continue;

            case TK_AT_END:
                break;

            default:
                CParser_ParseGlobalDeclaration();
                continue;
        }
        break;
    }

    for (meth = cat->methods; meth; meth = meth->next) {
        if (!meth->is_defined) {
            CError_Warning(ERR_METHOD_NOT_DEFINED, meth);
        }
    }

    CObjC_DefineCategoryObjects(tclass, cat);
}

static Boolean CObjC_IsSameMemberList(ObjMemberVar* a, ObjMemberVar* b) {
    while (1) {
        if (!a) {
            return b == NULL;
        }

        if (!b ||
            a->name != b->name ||
            a->qual != b->qual ||
            a->access != b->access ||
            !is_typesame(a->type, b->type)) {
            return FALSE;
        }

        a = a->next;
        b = b->next;
    }
}

static void CObjC_ParseInstanceVariables(TypeClass* tclass, Boolean checkOnly) {
    AccessType access;
    ObjMemberVar* first;
    ObjMemberVar* ivar;
    ObjMemberVar* scan;
    BigDeclInfo bdi;

    tk = lex();
    access = ACCESSPROTECTED;

    for (first = NULL;; tk = lex()) {
        if (tk == '}') {
            tk = lex();
            break;
        }

        switch (tk) {
            case TK_AT_PRIVATE:
                access = ACCESSPRIVATE;
                continue;

            case TK_AT_PROTECTED:
                access = ACCESSPROTECTED;
                continue;

            case TK_AT_PUBLIC:
                access = ACCESSPUBLIC;
                continue;
        }

        memclrw(&bdi, sizeof(bdi));
        CParser_GetDeclSpecs(&bdi.declinfo, FALSE);
        if (bdi.declinfo.storageclass || bdi.declinfo.hasParameterNames) {
            CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
            return;
        }

        if (tk != ';') {
            while (1) {
                CDecl_ScanStructDeclarator(&bdi);
                if (!CanCreateObject(bdi.declinfo2.thetype)) {
                    CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
                    bdi.valid = FALSE;
                }
                if (bdi.declinfo2.operator_token) {
                    CError_Error(ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION);
                    bdi.valid = FALSE;
                }
                if (bdi.declinfo.missingTypeSpecifier) {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                }

                if (bdi.valid) {
                    for (scan = first; scan; scan = scan->next) {
                        if (scan->name == bdi.declinfo2.name) {
                            break;
                        }
                    }

                    if (scan || bdi.declinfo2.name == no_name_node) {
                        CError_Error(
                            ERR_STRUCT_UNION_CLASS_MEMBER_REDEFINED, bdi.declinfo2.name->name
                        );
                    } else {
                        ivar = galloc(sizeof(ObjMemberVar));
                        memclrw(ivar, sizeof(ObjMemberVar));

                        ivar->otype = OT_MEMBERVAR;
                        ivar->access = access;
                        ivar->type = bdi.declinfo2.thetype;
                        ivar->name = bdi.declinfo2.name;
                        ivar->qual = bdi.declinfo2.qual;

                        if ((scan = first)) {
                            while (scan->next) {
                                scan = scan->next;
                            }
                            scan->next = ivar;
                        } else {
                            first = ivar;
                        }

                        if (!checkOnly) {
                            CScope_AddObject(tclass->nspace, ivar->name, OBJ_BASE(ivar));
                        }
                    }
                }

                if (tk != ',') {
                    break;
                }

                tk = lex();
            }
        }

        if (tk != ';') {
            CError_Error(ERR_SEMICOLON_EXPECTED);
            break;
        }
    }

    if (checkOnly) {
        if (!CObjC_IsSameMemberList(tclass->ivars, first)) {
            CError_Error(ERR_INSTANCE_VARIABLE_LIST_DOES_NOT_MATCH);
        }
    } else {
        tclass->ivars = first;
    }
}

static void CObjC_ParseInterfaceImplementation(void) {
    TypeClass* tclass;
    Boolean isInterface;
    Boolean hasInterface;
    ClassList* base;
    TypeClass* tclassbase;
    ObjCProtocolList* protlist;
    ObjCMethod* meth;
    BClassList* classdef;

    isInterface = tk == TK_AT_INTERFACE;

    if ((tk = lex()) != TK_IDENTIFIER) {
        CError_Error(ERR_IDENTIFIER_EXPECTED);
        return;
    }

    if (!(tclass = CObjC_NewObjCClass(tkidentifier))) {
        return;
    }

    if ((tk = lex()) == '(') {
        if (isInterface) {
            CObjC_ParseCategoryInterface(tclass);
        } else {
            CObjC_ParseCategoryImplementation(tclass);
        }
        return;
    }

    hasInterface = (tclass->flags & CLASS_COMPLETED) != 0;
    if (hasInterface && isInterface) {
        CError_Error(ERR_STRUCT_UNION_ENUM_CLASS_TAG_REDEFINED, tclass->classname->name);
        return;
    }

    if (tk == ':') {
        if ((tk = lex()) != TK_IDENTIFIER) {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
            return;
        }

        if ((tclassbase = CObjC_FindObjCClass(tkidentifier, TRUE))) {
            if (tclassbase->flags & CLASS_COMPLETED) {
                if (!hasInterface) {
                    base = galloc(sizeof(ClassList));
                    memclrw(base, sizeof(ClassList));

                    base->base = tclassbase;
                    base->access = ACCESSPUBLIC;
                    tclass->bases = base;
                } else {
                    if (!tclass->bases || tclass->bases->base != tclassbase) {
                        CError_Error(ERR_SUPER_CLASS_DOES_NOT_MATCH_INTERFACE);
                    }
                }
            } else {
                CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, tclassbase, 0);
            }
        }

        tk = lex();
    }

    if (tk == '<') {
        protlist = CObjC_ParserProtocolList();
        if (hasInterface) {
            if (!CObjC_IsSameProtocolList(protlist, tclass->objcinfo->protocols)) {
                CError_Error(ERR_PROTOCOL_LIST_DOES_NOT_MATCH_INTERFACE);
            }
        } else {
            tclass->objcinfo->protocols = protlist;
            CObjC_AddProtocolMethods(
                tclass, &tclass->objcinfo->methods, tclass->objcinfo->protocols
            );
        }
    }

    if (tk == '{') {
        CObjC_ParseInstanceVariables(tclass, hasInterface);
    }

    if (!hasInterface) {
        ClassLayout layout;
        memclrw(&layout, sizeof(layout));
        CABI_LayoutClass(&layout, tclass);
    }

    cobjc_currentclass = tclass;

    while (1) {
        switch (tk) {
            case '+':
            case '-':
                if (isInterface) {
                    CObjC_AddMethod(tclass);
                } else {
                    CObjC_DefineMethod(tclass, NULL, &tclass->objcinfo->methods);
                }
                continue;

            case TK_AT_END:
                break;

            default:
                CParser_ParseGlobalDeclaration();
                continue;
        }

        break;
    }

    cobjc_currentclass = NULL;

    if (!isInterface) {
        for (meth = tclass->objcinfo->methods; meth; meth = meth->next) {
            if (!meth->is_defined) {
                CError_Warning(ERR_METHOD_NOT_DEFINED, meth);
            }
        }

        CObjC_DefineClassObjects(tclass);

        classdef = galloc(sizeof(BClassList));
        classdef->next = cobjc_classdefs;
        classdef->type = TYPE(tclass);
        cobjc_classdefs = classdef;
    }
}

void CObjC_ParseInterface(void) {
    CObjC_ParseInterfaceImplementation();
}

void CObjC_ParseImplementation(void) {
    CObjC_ParseInterfaceImplementation();
}

void CObjC_ParseProtocol(void) {
    ObjCProtocol* protocol;
    ObjCMethod* meth;

    if ((tk = lex()) != TK_IDENTIFIER) {
        CError_Error(ERR_IDENTIFIER_EXPECTED);
        return;
    }

    if ((protocol = CObjC_FindProtocol(tkidentifier))) {
        CError_Error(ERR_PROTOCOL_REDEFINED, protocol->name->name);
    }

    protocol = galloc(sizeof(ObjCProtocol));
    memclrw(protocol, sizeof(ObjCProtocol));

    protocol->name = tkidentifier;

    if ((tk = lex()) == '<') {
        protocol->protocols = CObjC_ParserProtocolList();
        CObjC_AddProtocolMethods(NULL, &protocol->methods, protocol->protocols);
    }

    protocol->next = cobjc_protocols;
    cobjc_protocols = protocol;

    while (1) {
        switch (tk) {
            case '-':
            case '+':
                if ((meth = CObjC_ParseMethod(TRUE))) {
                    if (!CObjC_FindMethod(protocol->methods, meth, FALSE, TRUE)) {
                        meth->next = protocol->methods;
                        protocol->methods = meth;
                        CObjC_MakeSelector(meth);
                    }
                }

                if (tk != ';') {
                    CError_Error(ERR_SEMICOLON_EXPECTED);
                } else {
                    tk = lex();
                }
                continue;

            case TK_AT_END:
                break;

            default:
                CParser_ParseGlobalDeclaration();
                continue;
        }

        break;
    }
}

void CObjC_ParseClassDeclaration(void) {
    while (1) {
        if ((tk = lex()) != TK_IDENTIFIER) {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
            return;
        }

        CObjC_NewObjCClass(tkidentifier);

        if ((tk = lex()) != ',') {
            if (tk != ';') {
                CError_Error(ERR_SEMICOLON_EXPECTED);
            }
            break;
        }
    }
}

static inline void CObjC_CoerceMethodArgs(
    ObjCMethod* meth, ObjCNamedArg* namedArgs, ENodeList* unnamedArgs
) {
    ObjCMethodArg* metharg;
    ObjCNamedArg* arg;

    if (namedArgs->expr) {
        metharg = meth->selector_args;
        arg = namedArgs;
        while (1) {
            arg->expr = argumentpromotion(arg->expr, metharg->type, metharg->qual, TRUE);
            arg = arg->next;
            if (!arg) {
                break;
            }
            metharg = metharg->next;
            CError_ASSERT(3004, metharg);
        }
    }

    while (unnamedArgs) {
        unnamedArgs->node = CExpr_VarArgPromotion(unnamedArgs->node, TRUE);
        unnamedArgs = unnamedArgs->next;
    }
}

static Boolean CObjC_SelectorCompare(ObjCMethod* method, ObjCNamedArg* args, Boolean has_varargs) {
    ObjCMethodArg* metharg;
    ObjCNamedArg* arg;

    if (has_varargs && !method->has_valist) {
        return FALSE;
    }

    metharg = method->selector_args;
    arg = args;
    do {
        if (metharg->selector != arg->name) {
            return FALSE;
        }
        if (!metharg->type && arg->expr) {
            return FALSE;
        }
        if (metharg->type && !arg->expr) {
            return FALSE;
        }

        arg = arg->next;
        if (!arg) {
            if (metharg->next) {
                return FALSE;
            } else {
                return TRUE;
            }
        }

    } while ((metharg = metharg->next));

    return FALSE;
}

ENode* CObjC_MakeSendMsgExpr(
    ENode* objexpr,
    TypeClass* tclass,
    ObjCNamedArg* namedArgs,
    ENodeList* unnamedArgs,
    UInt8 calltype,
    Boolean isSuper
) {
    ObjCMethod* meth;
    ObjCSelector* sel;
    ENode* callexpr;
    ObjCMethodList* methlist;
    TypeClass* scanclass;
    Object* sendMsgFunc;
    TypeFunc* methodtype;
    ObjCNamedArg* namedArg;
    ENodeList* arg;
    ObjCCategory* cat;
    ObjCProtocolList* protlist;

    meth = NULL;

    if (tclass) {
        scanclass = tclass;
        while (1) {
            CError_ASSERT(3112, scanclass->objcinfo);

            for (cat = scanclass->objcinfo->categories; cat; cat = cat->next) {
                for (meth = cat->methods; meth; meth = meth->next) {
                    switch (calltype) {
                        case 0:
                            if (meth->is_class_method) {
                                continue;
                            }
                            break;
                        case 1:
                            if (!meth->is_class_method) {
                                continue;
                            }
                            break;
                        case 2:
                            break;
                    }

                    if (CObjC_SelectorCompare(meth, namedArgs, unnamedArgs != NULL)) {
                        CObjC_CoerceMethodArgs(meth, namedArgs, unnamedArgs);
                        sel = CObjC_MakeSelector(meth);
                        break;
                    }
                }

                if (meth) {
                    break;
                }
            }

            if (meth) {
                break;
            }

            for (meth = scanclass->objcinfo->methods; meth; meth = meth->next) {
                if (CObjC_SelectorCompare(meth, namedArgs, unnamedArgs != NULL)) {
                    CObjC_CoerceMethodArgs(meth, namedArgs, unnamedArgs);
                    sel = CObjC_MakeSelector(meth);
                    break;
                }
            }

            if (meth) {
                break;
            }

            if (!scanclass->bases) {
                CError_Warning(ERR_RECEIVER_CANNOT_HANDLE_MESSAGE);
                break;
            }

            scanclass = scanclass->bases->base;
        }
    }

    if (!meth) {
        if (IS_TYPE_POINTER_ONLY(objexpr->rtype) && (TPTR_QUAL(objexpr->rtype) & Q_IS_OBJC_ID)) {
            for (protlist = TYPE_OBJC_ID(objexpr->rtype)->protocols; protlist;
                 protlist = protlist->next) {
                for (meth = protlist->protocol->methods; meth; meth = meth->next) {
                    if (CObjC_SelectorCompare(meth, namedArgs, unnamedArgs != NULL)) {
                        CObjC_CoerceMethodArgs(meth, namedArgs, unnamedArgs);
                        sel = CObjC_MakeSelector(meth);
                        break;
                    }
                }
            }

            if (!meth) {
                CError_Warning(ERR_RECEIVER_CANNOT_HANDLE_MESSAGE);
            }
        }

        if (!meth) {
            if ((sel = CObjC_FindKeyArgSelector(namedArgs))) {
                for (methlist = sel->methods; methlist; methlist = methlist->next) {
                    if (!methlist->method->is_class_method) {
                        if (meth) {
                            break;
                        }
                        meth = methlist->method;
                    }
                }

                if (!meth) {
                    for (methlist = sel->methods; methlist; methlist = methlist->next) {
                        if (methlist->method->is_class_method) {
                            if (meth) {
                                break;
                            }
                            meth = methlist->method;
                        }
                    }
                }

                if (meth) {
                    if (methlist) {
                        CError_Warning(
                            ERR_AMBIGUOUS_MESSAGE_SELECTOR_USED_ALSO_HAD, meth, methlist->method
                        );
                    }

                    if (CObjC_SelectorCompare(meth, namedArgs, unnamedArgs != NULL)) {
                        CObjC_CoerceMethodArgs(meth, namedArgs, unnamedArgs);
                    } else {
                        CError_Warning(ERR_RECEIVER_CANNOT_HANDLE_MESSAGE);
                    }
                } else {
                    CError_Error(ERR_UNKNOWN_MESSAGE_SELECTOR);
                }
            } else {
                CError_Error(ERR_UNKNOWN_MESSAGE_SELECTOR);
            }
        }
    }

    if (meth) {
        methodtype = CObjC_GetMethodFuncType(meth);
        sendMsgFunc =
            CObjC_FindSendMessageRTFunc(isSuper, CMach_GetFunctionResultClass(methodtype) > 0);

        if (isSuper) {
            ENode* temp;
            ENode* indirect;
            ENode* ass;
            ENode* tempCopy;

            temp = CExpr_NewETEMPNode(CDecl_NewStructType(8, 4), TRUE);

            indirect = makemonadicnode(temp, EINDIRECT);
            indirect->rtype = TYPE(&void_ptr);

            objexpr = makediadicnode(indirect, objexpr, EASS);

            tempCopy = lalloc(sizeof(ENode));
            *tempCopy = *temp;

            indirect = makediadicnode(tempCopy, intconstnode(TYPE(&stunsignedlong), 4), EADD);
            indirect = makemonadicnode(indirect, EINDIRECT);
            indirect->rtype = TYPE(&void_ptr);

            ass =
                makediadicnode(indirect, create_objectrefnode(tclass->objcinfo->metaobject), EASS);

            objexpr = makediadicnode(objexpr, ass, ECOMMA);

            tempCopy = lalloc(sizeof(ENode));
            *tempCopy = *temp;

            objexpr = makediadicnode(objexpr, tempCopy, ECOMMA);
            objexpr->rtype = temp->rtype;
        }

        callexpr = lalloc(sizeof(ENode));
        callexpr->type = EFUNCCALL;
        callexpr->cost = 200;
        callexpr->rtype = meth->return_type;
        callexpr->flags = meth->return_qual & ENODE_FLAG_QUALS;
        callexpr->data.funccall.funcref = create_objectrefnode(sendMsgFunc);
        callexpr->data.funccall.functype = methodtype;
        CError_ASSERT(3286, IS_TYPE_FUNC(callexpr->data.funccall.functype));

        callexpr->data.funccall.args = arg = lalloc(sizeof(ENodeList));
        arg->node = objexpr;

        arg = arg->next = lalloc(sizeof(ENodeList));
        arg->node = create_objectnode(CObjC_GetSelectorObject(sel));

        for (namedArg = namedArgs; namedArg; namedArg = namedArg->next) {
            if (namedArg->expr) {
                arg = arg->next = lalloc(sizeof(ENodeList));
                arg->node = namedArg->expr;
            }
        }

        arg->next = unnamedArgs;
        callexpr = CExpr_AdjustFunctionCall(callexpr);
    } else {
        callexpr = nullnode();
    }

    return callexpr;
}

ENode* CObjC_ParseMessageExpression(void) {
    ENode* objexpr;
    TypeClass* tclass;
    Boolean isSuper;
    ENodeList* unnamedArgs;
    ObjCNamedArg* namedArgs;
    ObjCNamedArg* lastNamedArg;
    ENode* callexpr;
    UInt8 calltype;
    NameSpaceObjectList* list;

    isSuper = FALSE;
    tclass = NULL;

    switch ((tk = lex())) {
        case TK_IDENTIFIER:
            if (!strcmp(tkidentifier->name, "super")) {
                case TK_SUPER:
                    objexpr = CClass_CreateThisSelfExpr();
                    if (!objexpr || !cscope_currentclass->bases) {
                        CError_Error(ERR_ILLEGAL_USE_SUPER);
                        objexpr = nullnode();
                    } else {
                        tclass = cscope_currentclass->bases->base;
                        isSuper = TRUE;
                    }

                    calltype = !cscope_is_member_func ? (UInt8)1 : (UInt8)0;
                    tk = lex();
                    break;
            }

            if ((list = CScope_FindName(cscope_root, tkidentifier)) &&
                list->object->otype == OT_TYPE) {
                tclass = TYPE_CLASS(OBJ_TYPE(list->object)->type);
                if (IS_TYPE_CLASS(tclass) && tclass->objcinfo) {
                    calltype = 1;
                    objexpr = create_objectrefnode(tclass->objcinfo->classobject);
                    tk = lex();
                    break;
                }
            }

            tclass = NULL;

        default:
            objexpr = expression();
            if (CObjC_IsType_id(objexpr->rtype)) {
                calltype = 2;
                break;
            }

            if (!IS_TYPE_POINTER_ONLY(objexpr->rtype) ||
                !IS_TYPE_OBJC_CLASS(TPTR_TARGET(objexpr->rtype))) {
                CError_Error(ERR_ILLEGAL_MESSAGE_RECEIVER);
                objexpr = nullnode();
                calltype = 2;
                break;
            }

            if (cscope_currentclass == TYPE_CLASS(TPTR_TARGET(objexpr->rtype)) &&
                !cscope_is_member_func &&
                ENODE_IS_INDIRECT_TO(objexpr, EOBJREF) &&
                objexpr->data.monadic->data.objref->name == self_name_node) {
                calltype = 1;
            } else {
                calltype = 0;
            }

            tclass = TYPE_CLASS(TPTR_TARGET(objexpr->rtype));
    }

    lastNamedArg = lalloc(sizeof(ObjCNamedArg));
    memclrw(lastNamedArg, sizeof(ObjCNamedArg));

    namedArgs = lastNamedArg;
    unnamedArgs = NULL;

    while (1) {
        CObjC_TranslateSelectorToken();

        if (tk == TK_IDENTIFIER) {
            lastNamedArg->name = tkidentifier;
            if ((tk = lex()) == ']' && namedArgs == lastNamedArg) {
                break;
            }
        }

        if (tk != ':') {
            CError_Error(ERR_EXPRESSION_SYNTAX_ERROR);
            return nullnode();
        }

        tk = lex();
        lastNamedArg->expr = assignment_expression();

        if (tk == ']') {
            break;
        }

        if (tk == ',') {
            tk = lex();
            unnamedArgs = CExpr_ScanExpressionList(FALSE);
            break;
        }

        lastNamedArg->next = lalloc(sizeof(ObjCNamedArg));
        memclrw(lastNamedArg->next, sizeof(ObjCNamedArg));
        lastNamedArg = lastNamedArg->next;
    }

    callexpr = CObjC_MakeSendMsgExpr(objexpr, tclass, namedArgs, unnamedArgs, calltype, isSuper);
    tk = lex();
    return callexpr;
}

ENode* CObjC_ParseEncodeExpression(void) {
    ENode* expr;
    char* str;
    Type* type;
    UInt32 qual;

    if ((tk = lex()) != '(') {
        CError_Error(ERR_LPAREN_EXPECTED);
        return nullnode();
    }

    CObjC_ParseTypeName(&type, &qual);
    str = CObjC_EncodeTypeString(type, qual);

    expr = lalloc(sizeof(ENode));
    expr->type = ESTRINGCONST;
    expr->cost = 0;
    expr->flags = 0;
    expr->rtype = CDecl_NewArrayType(TYPE(&stchar), strlen(str) + 1);
    expr->data.string.size = expr->rtype->size;
    expr->data.string.data = str;
    expr->data.string.ispascal = FALSE;

    expr = makemonadicnode(expr, EINDIRECT);
    expr->data.monadic->rtype = CDecl_NewPointerType(expr->rtype);

    return expr;
}

ENode* CObjC_ParseAtExpression(void) {
    TypeClass* strclass;
    OLinkList* refs;
    OLinkList* ref;
    Object* object;
    ENode* expr;
    UInt32 data[3];
    char buf[16];

    if ((tk = lex()) == TK_STRING) {
        if ((strclass = CObjC_FindObjCClass(GetHashNameNodeExport("NSConstantString"), TRUE))) {
            refs = NULL;

            data[0] = CTool_EndianConvertWord32(0);
            ref = lalloc(sizeof(OLinkList));
            ref->next = refs;
            refs = ref;
            ref->obj = strclass->objcinfo->classobject;
            ref->offset = 0;
            ref->addend = 0;

            data[1] = CTool_EndianConvertWord32(0);

            ref = lalloc(sizeof(OLinkList));
            ref->next = refs;
            refs = ref;
            ref->obj = CInit_DeclareString(tkstring, tksize, FALSE, FALSE);
            ref->offset = 4;
            ref->addend = 0;

            data[2] = CTool_EndianConvertWord32(tksize - 1);

            sprintf(buf, "%ld", cobjc_stringcount++);

            object = CObjC_MakeObject("L_NSConstantString_", buf, sizeof(data));
            object->type = TYPE(strclass);
            object->sclass = TK_STATIC;
            object->section = SECT_OBJC_CSTRING_OBJECT;
            CInit_DeclareData(object, data, refs, object->type->size);

            expr = create_objectrefnode(object);
        } else {
            expr = nullnode();
        }

        tk = lex();
    } else {
        CError_Error(ERR_ILLEGAL_STRING_CONSTANT);
        expr = nullnode();
    }

    return expr;
}

ENode* CObjC_ParseProtocolExpression(void) {
    ObjCProtocol* protocol;
    ENode* expr;

    if ((tk = lex()) == '(') {
        if ((tk = lex()) == TK_IDENTIFIER) {
            if ((protocol = CObjC_FindProtocol(tkidentifier))) {
                expr = create_objectrefnode(CObjC_GetProtocolObject(protocol));

                if ((tk = lex()) != ')') {
                    CError_Error(ERR_RPAREN_EXPECTED);
                } else {
                    tk = lex();
                }

                return expr;
            }
        } else {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
        }
    } else {
        CError_Error(ERR_LPAREN_EXPECTED);
    }

    return nullnode();
}

ENode* CObjC_ParseSelectorExpression(void) {
    HashNameNode* name;
    ObjCSelector* selector;
    ENode* expr;

    if ((tk = lex()) == '(') {
        name_mangle_list.size = 0;
        while (1) {
            tk = lex();

            if (tk == TK_IDENTIFIER) {
                AppendGListName(&name_mangle_list, tkidentifier->name);
                tk = lex();
            }

            if (tk == ')') {
                if (name_mangle_list.size == 0) {
                    CError_Error(ERR_IDENTIFIER_EXPECTED);
                }
                tk = lex();
                break;
            }

            if (tk == ':') {
                AppendGListByte(&name_mangle_list, ':');
            } else {
                CError_Error(ERR_RPAREN_EXPECTED);
                break;
            }
        }

        AppendGListByte(&name_mangle_list, 0);

        COS_LockHandle(name_mangle_list.data);
        name = GetHashNameNodeExport(*name_mangle_list.data);
        COS_UnlockHandle(name_mangle_list.data);

        if (!(selector = CObjC_FindSelector(name))) {
            selector = CObjC_NewSelector(name);
        }

        expr = create_objectnode(CObjC_GetSelectorObject(selector));
        expr->rtype = CObjC_FindObjCType_SEL();

        return expr;
    } else {
        CError_Error(ERR_LPAREN_EXPECTED);
        return nullnode();
    }
}
