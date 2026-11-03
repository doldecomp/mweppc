#include "compiler/CSOM.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CException.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/som.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "driver/COSToolsCLT.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    SInt32 zero;
    UInt32 sci;
    UInt32 instanceDataToken;
    SInt32 reserved[3];
    UInt32 tokens[1];
} somClassDataStructure;

enum {
    mtVirtualMethod = 0,
    mtProcedure = 1,
    mtNonStatic = 2,
    mtEmpty = 3,
    mtDToken = 4
};

enum {
    pdUByte = 0,
    pdSByte = 1,
    pdUHalf = 2,
    pdSHalf = 3,
    pdULong = 4,
    pdSLong = 5,
    pdVLong = 6,
    pdVoid = 7,
    pdSFlt = 8,
    pdDFlt = 9,
    pdLFlt = 10,
    pdVSAgg = 11,
    pdNPtr = 12,
    pdLPtr = 13,
    pdSAgg = 14,
    pdLAgg = 15
};

enum {
    fgShortOrChars = 1,
    fgShortFloats = 2,
    fgAnyFloats = 4,
    fgAnyNon4Bytes = 8
};

enum {
    cfSharedStrings = 1,
    cfTempClass = 2,
    cfProxyClass = 4,
    cfClassAllocate = 0x100,
    cfClassDeallocate = 0x200,
    cfClassInit = 0x400,
    cfClassUninit = 0x800
};

typedef struct {
    UInt32 majorVersion;
    UInt32 minorVersion;
    UInt32 flags;
    UInt16 dataAlignment;
    UInt16 classTokenCount;
    UInt16 numDirectParents;
    UInt16 numMetaClasses;
    UInt16 numOverriddenAncestors;
    UInt16 numMigratedMethods;
    UInt16 numSelectInherited;
    UInt16 numUnused;
    UInt16 dummy2a[4];
} somStaticClassCounts;

typedef UInt8 somSlotUsage;
typedef UInt8 somSignatureInfo;
typedef UInt16 somOverrideData;
typedef UInt16 somMigratedMethods;
typedef UInt16 somSelectedInherited;
typedef UInt32 somParentVersions;

typedef struct {
    UInt32 className;
    UInt32 instanceDataSize;
    UInt32 parentVersions;
    UInt32 ttSlotUsage;
    UInt32 signatureInfo;
    UInt32 methodNames;
    UInt32 overrideData;
    UInt32 migratedMethods;
    UInt32 selectedInherited;
    UInt32 unused;
    UInt32 dummy4b[4];
} somStaticClassDescription;

typedef struct somStaticClassInfo {
    UInt32 layoutVersion;
    UInt32 tokenTable;
    UInt32 overrideMethods;
    UInt32 specifiedAncestry;
    UInt32 DLLDesignator;
    UInt32 specialProcs;
    UInt32 runtimeClassInfo;
    SInt32 interesting;
    UInt32 actualAncestry;
    UInt32 extra[4];
    UInt32 classCounts;
    somStaticClassDescription classDescription;
} somStaticClassInfo;

CSOMStub* csom_stubs;
static HashNameNode* csom_initname;
static HashNameNode* csom_uninitname;
static HashNameNode* csom_envname;
static HashNameNode* csom_selfname;

static FuncArg SOMIDT_arg1 = {NULL, NULL, NULL, TYPE(&void_ptr), 0, 0, FALSE};

static TypeFunc SOMIDT_type = {TYPEFUNC, 0, &SOMIDT_arg1, NULL, TYPE(&void_ptr), 0, 0};

void CSOM_Setup(Boolean isPrecompiler) {
    if (!isPrecompiler) {
        csom_stubs = NULL;
    }

    csom_initname = GetHashNameNodeExport("somInit");
    csom_uninitname = GetHashNameNodeExport("somUninit");
    csom_envname = GetHashNameNodeExport("Environment");
    csom_selfname = GetHashNameNodeExport("__somself");
}

void CSOM_Cleanup(void) {
    CSOMStub* stub;

    if (cparamblkptr->precompile != 1) {
        for (stub = csom_stubs; stub; stub = stub->next) {
            switch (stub->kind) {
                case 0:
                    CodeGen_SOMStub(
                        stub->object,
                        rt_som_glue1,
                        stub->tclass->sominfo->classdataobject,
                        stub->offset
                    );
                    break;
                case 1:
                    CodeGen_SOMStub(
                        stub->object,
                        rt_som_glue2,
                        stub->tclass->sominfo->classdataobject,
                        stub->offset
                    );
                    break;
                case 2:
                    CodeGen_SOMStub(
                        stub->object,
                        rt_som_glue3,
                        stub->tclass->sominfo->classdataobject,
                        stub->offset
                    );
                    break;
                default:
                    CError_FATAL(132);
            }
        }
    }
}

static HashNameNode* CSOM_NameTranslate(HashNameNode* name) {
    if (name == constructor_name_node) {
        return csom_initname;
    }
    if (name == destructor_name_node) {
        return csom_uninitname;
    }
    return name;
}

static Type* CSOM_FindClassType(HashNameNode* name) {
    Type* type;

    type = CScope_GetTagType(cscope_current, name);
    if (!type) {
        CPrep_ErrorName(ERR_SOM_TYPE_UNDEFINED, name->name);
        return &stvoid;
    }

    return type;
}

static inline UInt16 CSOM_GetTokenTableIndex(const Object* object) {
    CError_ASSERT(173, IS_TYPE_METHOD(object->type));
    return TYPE_METHOD(object->type)->vtbl_index;
}

static SInt32 CSOM_GetTokenOffset(Object* object) {
    return 24 + 4 * CSOM_GetTokenTableIndex(object);
}

typedef struct TypeSig {
    UInt8 argCount;
    UInt8 flags;
    UInt8 returnType;
} TypeSig;

static int CSOM_GetTypeSig(TypeSig* sig, Type* type, Boolean isReturnType) {
    if (type->size > 4) {
        sig->flags |= fgAnyNon4Bytes;
    }

    switch (type->type) {
        case TYPEVOID:
            if (isReturnType) {
                return pdVoid;
            }
            break;
        case TYPEINT:
        case TYPEENUM:
            if (is_unsigned(type)) {
                switch (type->size) {
                    case 1:
                        sig->flags |= fgShortOrChars;
                        return pdUByte;
                    case 2:
                        sig->flags |= fgShortOrChars;
                        return pdUHalf;
                    case 4:
                        return pdULong;
                    case 8:
                        return pdVLong;
                }
            } else {
                switch (type->size) {
                    case 1:
                        sig->flags |= fgShortOrChars;
                        return pdSByte;
                    case 2:
                        sig->flags |= fgShortOrChars;
                        return pdSHalf;
                    case 4:
                        return pdSLong;
                    case 8:
                        return pdVLong;
                }
            }
            break;
        case TYPEFLOAT:
            sig->flags |= fgAnyFloats;
            switch (type->size) {
                case 4:
                    sig->flags |= fgShortFloats;
                    return pdSFlt;
                case 8:
                    return pdDFlt;
                case 12:
                case 16:
                    return pdLFlt;
            }
            break;
        case TYPEPOINTER:
            return pdNPtr;
        case TYPESTRUCT:
        case TYPECLASS:
            if (isReturnType) {
                if (type->size <= 2) {
                    sig->flags |= fgShortOrChars;
                    return pdVSAgg;
                } else if (type->size <= 4) {
                    return pdSAgg;
                } else {
                    return pdLAgg;
                }
            }
            break;
        default:
            break;
    }

    CError_Error(ERR_ILLEGAL_SOM_FUNCTION_PARAMETERS_RETURN_TYPE);
    return pdSLong;
}

static void CSOM_GetFuncSig(TypeFunc* tfunc, Boolean isMethod) {
    FuncArg* arg;
    Boolean pendingData;
    UInt8 work;
    TypeSig sig;

    sig.returnType = CSOM_GetTypeSig(&sig, tfunc->functype, TRUE);
    sig.flags = 0;
    sig.argCount = 0;

    for (arg = tfunc->args; arg; arg = arg->next) {
        if (arg == &elipsis || arg == &oldstyle || (++sig.argCount == 0)) {
            CError_Error(ERR_ILLEGAL_SOM_FUNCTION_PARAMETERS_RETURN_TYPE);
            break;
        }

        CSOM_GetTypeSig(&sig, arg->type, FALSE);
    }

    if (isMethod) {
        if ((arg = tfunc->args)) {
            if (TYPE_METHOD(tfunc)->is_static == 0) {
                arg = arg->next;
            }
            if (arg && CMach_GetFunctionResultClass(tfunc) != 0) {
                arg = arg->next;
            }
        }

        AppendGListByte(&name_mangle_list, sig.argCount);
        AppendGListByte(&name_mangle_list, (sig.flags << 4) | sig.returnType);
        if (sig.flags) {
            pendingData = FALSE;
            work = 0;
            while (arg) {
                work = (work << 4) | CSOM_GetTypeSig(&sig, arg->type, FALSE);
                if (pendingData) {
                    AppendGListByte(&name_mangle_list, work);
                    pendingData = FALSE;
                    work = 0;
                } else {
                    pendingData = TRUE;
                }
                arg = arg->next;
            }

            if (pendingData) {
                AppendGListByte(&name_mangle_list, work << 4);
            }
        }
    }
}

void CSOM_CheckFuncType(TypeFunc* tfunc) {
    CSOM_GetFuncSig(tfunc, FALSE);
}

static Object* CSOM_MakeObject(char* prefix, char* suffix, SInt32 size) {
    Object* object = CParser_NewCompilerDefDataObject();
    object->name = CParser_NameConcat(prefix, suffix);
    object->type = CDecl_NewStructType(size, 4);
    CScope_AddObject(object->nspace, object->name, OBJ_BASE(object));
    return object;
}

void CSOM_MakeSOMClass(TypeClass* tclass) {
    ClassList* base;

    for (base = tclass->bases; base; base = base->next) {
        if (!base->base->sominfo) {
            CError_Error(ERR_SOM_CLASSES_ONLY_INHERIT_FROM_OTHER);
            break;
        }
    }

    if (!tclass->sominfo) {
        SOMInfo* info = galloc(sizeof(SOMInfo));
        memclrw(info, sizeof(SOMInfo));
        tclass->sominfo = info;

        info->classdataobject = CSOM_MakeObject(tclass->classname->name, "ClassData", 28);
        info->classdataobject->flags |= OBJECT_EXPORT;
    }
}

static Boolean CSOM_IsTokenListFunc(Object* object) {
    if (IS_TYPE_FUNC(object->type) &&
        !(TYPE_FUNC(object->type)->flags & FUNC_OVERRIDE) &&
        !TYPE_METHOD(object->type)->is_static &&
        (!(object->qual & Q_INLINE) || object->datatype == DVFUNC)) {
        return TRUE;
    }

    return FALSE;
}

static Object** CSOM_GetLexicalOrderMethodArray(TypeClass* tclass, int* resultCount) {
    Object* object;
    int count;
    Object** array;
    CScopeObjectIterator iter;

    count = 0;
    CScope_InitObjectIterator(&iter, tclass->nspace);
    while (1) {
        if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
            break;
        }

        if (IS_TYPE_METHOD(object->type)) {
            if (CSOM_IsTokenListFunc(object)) {
                if (TYPE_METHOD(object->type)->vtbl_index > count) {
                    count = TYPE_METHOD(object->type)->vtbl_index;
                }
            } else {
                TYPE_METHOD(object->type)->vtbl_index = 0;
            }
        }
    }

    *resultCount = ++count;

    array = lalloc(sizeof(Object*) * count);
    memclrw(array, sizeof(Object*) * count);

    CScope_InitObjectIterator(&iter, tclass->nspace);
    while (1) {
        if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
            break;
        }

        if (CSOM_IsTokenListFunc(object)) {
            array[TYPE_METHOD(object->type)->vtbl_index] = object;
        }
    }

    return array;
}

void CSOM_ClassComplete(TypeClass* tclass) {
    Object* object;
    CScopeObjectIterator iter;
    SInt32 counter;
    SOMReleaseOrder* order;

    if (tclass->sominfo->order) {
        CScope_InitObjectIterator(&iter, tclass->nspace);
        while (1) {
            if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
                break;
            }

            if (CSOM_IsTokenListFunc(object)) {
                HashNameNode* name;

                name = CSOM_NameTranslate(object->name);
                for (order = tclass->sominfo->order, counter = 0; order;
                     order = order->next, counter++) {
                    if (order->name == name) {
                        order->state = SOMMS_Method;
                        TYPE_METHOD(object->type)->vtbl_index = counter;
                        break;
                    }
                }

                if (!order) {
                    CError_Error(ERR_INTRODUCED_METHOD_NOT_SPECIFIED_RELEASE_ORDER, object);
                }
            }
        }

        for (order = tclass->sominfo->order; order; order = order->next) {
            if (order->state == SOMMS_Deleted) {
                SOMReleaseOrder* order2;
                VClassList* vbase;
                for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
                    for (order2 = vbase->base->sominfo->order; order2; order2 = order2->next) {
                        if (order->name == order2->name && order2->state == SOMMS_Method) {
                            order->state = SOMMS_Migrated;
                            break;
                        }
                    }
                }
            }
        }
    } else {
        Object** array;
        int arrayCount;
        SInt32 i;

        array = CSOM_GetLexicalOrderMethodArray(tclass, &arrayCount);
        for (i = counter = 0; i < arrayCount; i++) {
            object = array[i];
            if (object) {
                if (counter == 0 && copts.pedantic) {
                    CError_Warning(ERR_SOM_CLASS_NO_RELEASE_ORDER_LIST);
                }
                TYPE_METHOD(object->type)->vtbl_index = counter++;
            }
        }
    }

    CScope_InitObjectIterator(&iter, tclass->nspace);
    while (1) {
        NameSpaceObjectList* nsol;
        if (!(nsol = CScope_NextObjectIteratorObjectList(&iter))) {
            break;
        }

        if (nsol->object->otype == OT_OBJECT &&
            nsol->next &&
            nsol->next->object->otype == OT_OBJECT) {
            while (nsol) {
                if (nsol->object->otype == OT_OBJECT &&
                    (!(OBJECT(nsol->object)->qual & Q_INLINE) ||
                     OBJECT(nsol->object)->datatype == DVFUNC)) {
                    CError_Error(ERR_ILLEGAL_SOM_FUNCTION_OVERLOAD, nsol->object);
                }
                nsol = nsol->next;
            }
        }
    }

    CScope_InitObjectIterator(&iter, tclass->nspace);
    while (1) {
        if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
            break;
        }

        if (!(object->qual & Q_INLINE)) {
            CError_ASSERT(529, IS_TYPE_FUNC(object->type));

            TYPE_FUNC(object->type)->flags |= FUNC_KEY_FUNCTION;
            tclass->action = CLASS_ACTION_KEY_FUNCTION;
            while (1) {
                if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
                    break;
                }

                if (IS_TYPE_FUNC(object->type)) {
                    TYPE_FUNC(object->type)->flags &= ~FUNC_KEY_FUNCTION;
                }
            }
            break;
        }
    }

    if (tclass->sominfo->oidl_callstyle == 0) {
        Type* envType;
        FuncArg* arg;
        envType = CSOM_FindClassType(csom_envname);

        CScope_InitObjectIterator(&iter, tclass->nspace);
        while (1) {
            if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
                break;
            }

            if (IS_TYPE_FUNC(object->type) &&
                TYPE_METHOD(object->type)->is_static == 0 &&
                !(TYPE_FUNC(object->type)->flags & FUNC_OVERRIDE) &&
                !((arg = TYPE_FUNC(object->type)->args) &&
                  (arg = arg->next) &&
                  IS_TYPE_POINTER_ONLY(arg->type) &&
                  TPTR_TARGET(arg->type) == envType)) {
                CError_Error(ERR_NEW_SOM_CALLSTYLE_METHOD_MUST_EXPLICIT, object);
            }
        }
    }

    if (tclass->action == CLASS_ACTION_NONE) {
        CError_Error(ERR_SOM_CLASS_MUST_ONE_NON_INLINE);
    }
}

#pragma pack(push, 2)
typedef struct SOMOverride {
    struct SOMOverride* next;
    Object* overrider;
    Object* overridden;
} SOMOverride;

typedef struct SOMAncestor {
    struct SOMAncestor* next;
    TypeClass* tclass;
    SOMOverride* overrides;
    Boolean isMetaClass;
    Boolean isDirectParent;
} SOMAncestor;

typedef struct SOMMethod {
    struct SOMMethod* next;
    HashNameNode* name;
    union {
        Object* object;
        struct {
            UInt16 ancestorIndex;
            UInt16 methodIndex;
        } pair;
    } u;
    SOMMethodState state;
} SOMMethod;

typedef struct SOMGenerator {
    SOMMethod* methods;
    SOMAncestor* ancestors;
    Object* sciObj;
    Object* classAncestorsObj;
    Object* overrideProcsObj;
    Object* dlldFunc;
    Object* specialProcsObj;
    somStaticClassCounts counts;
    int overrideProcsCount;
    Boolean hasNew;
    Boolean hasDelete;
} SOMGenerator;
#pragma pack(pop)

static SOMAncestor* CSOM_FindAddAncestor(
    SOMGenerator* gen, TypeClass* tclass, TypeClass* ancestorClass, UInt16* resultIndex
) {
    SOMAncestor* ancestor;
    SOMAncestor* scan;
    UInt16 index;

    for (scan = gen->ancestors, index = 0; scan; scan = scan->next, index++) {
        if (scan->tclass == ancestorClass) {
            if (resultIndex) {
                *resultIndex = index;
            }
            return scan;
        }
    }

    if (gen->ancestors) {
        index = 1;
        ancestor = gen->ancestors;
        while (ancestor->next) {
            index++;
            ancestor = ancestor->next;
        }

        ancestor->next = lalloc(sizeof(SOMAncestor));
        memclrw(ancestor->next, sizeof(SOMAncestor));
        ancestor = ancestor->next;
    } else {
        index = 0;
        ancestor = lalloc(sizeof(SOMAncestor));
        memclrw(ancestor, sizeof(SOMAncestor));
        gen->ancestors = ancestor;
    }

    ancestor->tclass = ancestorClass;
    if (resultIndex) {
        *resultIndex = index;
    }
    return ancestor;
}

static void CSOM_GenerateOverrideIntroLists(SOMGenerator* gen, TypeClass* tclass) {
    Object* object;
    VClassList* vbase;
    ClassList* base;
    SOMMethod* method;
    SOMMethod** ptr;
    CScopeObjectIterator iter;
    Object* object2;
    CScopeObjectIterator iter2;
    SOMOverride* override;
    SOMReleaseOrder* order;
    SOMReleaseOrder* order2;
    SOMAncestor* ancestor;
    SInt32 index;
    UInt16 index2;
    Object** array;
    int arrayCount;
    SInt32 i;

    for (base = tclass->bases; base; base = base->next) {
        SOMAncestor* ancestor = CSOM_FindAddAncestor(gen, tclass, base->base, NULL);
        ancestor->isDirectParent = TRUE;
        gen->counts.numDirectParents++;
    }

    if (tclass->sominfo->metaclass && tclass->sominfo->metaclass->sominfo) {
        SOMAncestor* ancestor = CSOM_FindAddAncestor(gen, tclass, tclass->sominfo->metaclass, NULL);
        ancestor->isMetaClass = TRUE;
        gen->counts.numMetaClasses++;
    }

    CScope_InitObjectIterator(&iter, tclass->nspace);
    while (1) {
        if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
            break;
        }

        if (IS_TYPE_FUNC(object->type) && (TYPE_FUNC(object->type)->flags & FUNC_OVERRIDE)) {
            for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
                CScope_InitObjectIterator(&iter2, vbase->base->nspace);
                while (1) {
                    if (!(object2 = OBJECT(CScope_NextObjectIteratorObject(&iter2)))) {
                        break;
                    }

                    if (IS_TYPE_FUNC(object2->type) &&
                        object->name == object2->name &&
                        object2->datatype == DVFUNC &&
                        !(TYPE_FUNC(object2->type)->flags & FUNC_OVERRIDE) &&
                        CClass_GetOverrideKind(
                            TYPE_FUNC(object->type), TYPE_FUNC(object2->type), FALSE
                        ) != 0) {
                        ancestor = CSOM_FindAddAncestor(gen, tclass, vbase->base, NULL);
                        if ((override = ancestor->overrides)) {
                            override = lalloc(sizeof(SOMOverride));
                            memclrw(override, sizeof(SOMOverride));
                            override->next = ancestor->overrides;
                            ancestor->overrides = override;
                        } else {
                            override = lalloc(sizeof(SOMOverride));
                            memclrw(override, sizeof(SOMOverride));
                            ancestor->overrides = override;
                            gen->counts.numOverriddenAncestors++;
                        }
                        override->overrider = object;
                        override->overridden = object2;
                        break;
                    }
                }
            }
            gen->overrideProcsCount++;
        }
    }

    ptr = &gen->methods;
    if (tclass->sominfo->order) {
        for (order = tclass->sominfo->order, index = 0; order; order = order->next, index++) {
            method = lalloc(sizeof(SOMMethod));
            memclrw(method, sizeof(SOMMethod));
            *ptr = method;
            ptr = &method->next;

            method->name = order->name;
            method->state = order->state;
            switch (order->state) {
                case SOMMS_Migrated:
                    for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
                        for (order2 = vbase->base->sominfo->order, index2 = 0; order2;
                             order2 = order2->next, index2++) {
                            if (order->name == order2->name && order2->state == SOMMS_Method) {
                                CSOM_FindAddAncestor(
                                    gen, tclass, vbase->base, &method->u.pair.ancestorIndex
                                );
                                method->u.pair.methodIndex = index2;
                                break;
                            }
                        }

                        if (order2) {
                            break;
                        }
                    }
                    gen->counts.numMigratedMethods++;
                    break;

                case SOMMS_Method:
                    CScope_InitObjectIterator(&iter, tclass->nspace);
                    while (1) {
                        if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
                            break;
                        }

                        if (IS_TYPE_FUNC(object->type) &&
                            CSOM_NameTranslate(object->name) == order->name) {
                            CError_ASSERT(733, TYPE_METHOD(object->type)->vtbl_index == index);
                            method->u.object = object;
                            break;
                        }
                    }

                    CError_ASSERT(737, object != NULL);
                    break;

                default:
                    break;
            }

            gen->counts.classTokenCount++;
        }
    } else {
        array = CSOM_GetLexicalOrderMethodArray(tclass, &arrayCount);
        for (i = 0; arrayCount > i; i++) {
            object = array[i];
            if (object) {
                method = lalloc(sizeof(SOMMethod));
                memclrw(method, sizeof(SOMMethod));
                *ptr = method;
                ptr = &method->next;

                method->u.object = object;
                method->name = object->name;
                method->state = SOMMS_Method;
                gen->counts.classTokenCount++;
            }
        }
    }
}

static void CSOM_GenerateClassAncestors(SOMGenerator* gen, TypeClass* tclass) {
    SOMAncestor* ancestor;
    Object* object;
    OLinkList* relocs;
    int size;
    char* buf;

    if (gen->ancestors) {
        object = CSOM_MakeObject(tclass->classname->name, "ClassAncestors", 4);
        object->sclass = TK_STATIC;

        relocs = NULL;
        size = 0;
        for (ancestor = gen->ancestors; ancestor; ancestor = ancestor->next) {
            OLinkList* reloc = lalloc(sizeof(OLinkList));
            reloc->next = relocs;
            relocs = reloc;
            reloc->obj = ancestor->tclass->sominfo->classdataobject;
            reloc->offset = size;
            reloc->addend = 0;
            size += 4;
        }

        memclrw(buf = lalloc(size), size);
        object->type->size = size;
        CInit_DeclareData(object, buf, relocs, object->type->size);
        gen->classAncestorsObj = object;
    }
}

static void CSOM_GenerateOverrideProcs(SOMGenerator* gen, TypeClass* tclass) {
    SOMOverride* override;
    SOMAncestor* ancestor;
    Object* object;
    OLinkList* relocs;
    SInt32 size;
    SInt32 offset;
    char* buf;

    if (gen->overrideProcsCount) {
        size = gen->overrideProcsCount;
        size *= 4;
        object = CSOM_MakeObject(tclass->classname->name, "OverrideProcs", size);
        object->sclass = TK_STATIC;

        relocs = NULL;
        offset = 0;
        for (ancestor = gen->ancestors; ancestor; ancestor = ancestor->next) {
            if (ancestor->overrides) {
                for (override = ancestor->overrides; override; override = override->next) {
                    OLinkList* reloc = lalloc(sizeof(OLinkList));
                    reloc->next = relocs;
                    relocs = reloc;
                    reloc->obj = override->overrider;
                    reloc->offset = offset;
                    reloc->addend = 0;
                    offset += 4;
                }
            }
        }

        memclrw(buf = lalloc(size), size);
        CInit_DeclareData(object, buf, relocs, object->type->size);
        gen->overrideProcsObj = object;
    }
}

static Object* CSOM_GenerateOverrideData(SOMGenerator* gen) {
    SOMAncestor* ancestor;
    Object* object;
    short ancestorIndex;

    name_mangle_list.size = 0;
    for (ancestor = gen->ancestors, ancestorIndex = 0; ancestor;
         ancestor = ancestor->next, ancestorIndex++) {
        if (ancestor->overrides) {
            SOMOverride* override;
            short overrideCount;

            AppendGListWord(&name_mangle_list, CTool_EndianConvertWord16(ancestorIndex));

            override = ancestor->overrides;
            overrideCount = 0;
            while (override) {
                overrideCount++;
                override = override->next;
            }
            AppendGListWord(&name_mangle_list, CTool_EndianConvertWord16(overrideCount));

            for (override = ancestor->overrides; override; override = override->next) {
                AppendGListWord(
                    &name_mangle_list,
                    CTool_EndianConvertWord16(CSOM_GetTokenTableIndex(override->overridden))
                );
            }
        }
    }

    COS_LockHandle(name_mangle_list.data);
    object = CInit_DeclareString(*name_mangle_list.data, name_mangle_list.size, FALSE, FALSE);
    COS_UnlockHandle(name_mangle_list.data);
    return object;
}

static Object* CSOM_GenerateMigrateData(SOMGenerator* gen) {
    SOMMethod* method;
    Object* object;
    int index;

    name_mangle_list.size = 0;
    for (method = gen->methods, index = 0; method; method = method->next, index++) {
        if (method->state == SOMMS_Migrated) {
            AppendGListWord(
                &name_mangle_list, CTool_EndianConvertWord16(method->u.pair.ancestorIndex)
            );
            AppendGListWord(
                &name_mangle_list, CTool_EndianConvertWord16(method->u.pair.methodIndex)
            );
            AppendGListWord(&name_mangle_list, CTool_EndianConvertWord16(index));
        }
    }

    COS_LockHandle(name_mangle_list.data);
    object = CInit_DeclareString(*name_mangle_list.data, name_mangle_list.size, FALSE, FALSE);
    COS_UnlockHandle(name_mangle_list.data);
    return object;
}

static void CSOM_GenerateDLLDFunc(SOMGenerator* gen, TypeClass* tclass) {
    TypeFunc* tfunc;
    Object* object;

    tfunc = galloc(sizeof(TypeFunc));
    memclrw(tfunc, sizeof(TypeFunc));
    tfunc->type = TYPEFUNC;
    tfunc->functype = &stvoid;

    object = CParser_NewCompilerDefFunctionObject();
    object->type = TYPE(tfunc);
    object->sclass = TK_STATIC;
    object->name = CParser_NameConcat(tclass->classname->name, "DLLD");

    if (CScope_GetLocalObject(cscope_root, object->name)) {
        CError_Error(ERR_OBJECT_REDEFINED, object);
    }

    gen->dlldFunc = object;
    CFunc_GenerateDummyFunction(object);
}

static void CSOM_GenerateSpecialProcs(SOMGenerator* gen, TypeClass* tclass) {
    Object* object;
    Object* newFunc;
    Object* deleteFunc;
    OLinkList* relocs;
    SInt32 size;
    CScopeObjectIterator iter;
    char buf[16];

    newFunc = deleteFunc = NULL;
    CScope_InitObjectIterator(&iter, tclass->nspace);
    while (1) {
        if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
            break;
        }

        if (IS_TYPE_FUNC(object->type)) {
            if (object->name == CMangler_OperatorName(TK_NEW)) {
                newFunc = object;
                gen->hasNew = TRUE;
            } else if (object->name == CMangler_OperatorName(TK_DELETE)) {
                deleteFunc = object;
                gen->hasDelete = TRUE;
            }
        }
    }

    if (newFunc || deleteFunc) {
        object = CSOM_MakeObject(tclass->classname->name, "SpecialProcs", 4);
        object->sclass = TK_STATIC;

        relocs = NULL;
        size = 0;

        if (newFunc) {
            OLinkList* reloc = lalloc(sizeof(OLinkList));
            reloc->next = relocs;
            relocs = reloc;
            reloc->obj = newFunc;
            reloc->offset = size;
            reloc->addend = 0;
            size += 4;
        }

        if (deleteFunc) {
            OLinkList* reloc = lalloc(sizeof(OLinkList));
            reloc->next = relocs;
            relocs = reloc;
            reloc->obj = deleteFunc;
            reloc->offset = size;
            reloc->addend = 0;
            size += 4;
        }

        memclrw(buf, sizeof(buf));
        object->type->size = size;
        CInit_DeclareData(object, buf, relocs, object->type->size);
        gen->specialProcsObj = object;
    }
}

static Object* CSOM_GenerateParentVersion(SOMGenerator* gen) {
    SInt32 size;
    SOMAncestor* ancestor;
    UInt32* buf;
    SInt32 offset;

    size = 8 * (gen->counts.numDirectParents + gen->counts.numMetaClasses);
    buf = lalloc(size);

    for (ancestor = gen->ancestors, offset = 0; ancestor; ancestor = ancestor->next) {
        if (ancestor->isMetaClass || ancestor->isDirectParent) {
            buf[offset++] = CTool_EndianConvertWord32(ancestor->tclass->sominfo->majorversion);
            buf[offset++] = CTool_EndianConvertWord32(ancestor->tclass->sominfo->minorversion);
        }
    }

    return CInit_DeclareString((char*)buf, size, FALSE, FALSE);
}

static void CSOM_SetNibble(char* buf, SInt32 offset, UInt8 value) {
    buf += offset >> 1;
    if (offset & 1) {
        *buf = (*buf & 0xF0) | (value & 0xF);
    } else {
        *buf = (value << 4) | (*buf & 0xF);
    }
}

static Object* CSOM_GenerateSlotUsage(SOMGenerator* gen) {
    SInt32 size;
    SOMMethod* method;
    char* buf;
    int offset;

    size = (gen->counts.classTokenCount + 1) / 2;
    memclrw(buf = lalloc(size), size);

    for (method = gen->methods, offset = 0; method; method = method->next, offset++) {
        switch (method->state) {
            case SOMMS_Deleted:
            case SOMMS_Migrated:
                CSOM_SetNibble(buf, offset, mtEmpty);
                break;
            case SOMMS_Method:
                CSOM_SetNibble(buf, offset, mtVirtualMethod);
                break;
            default:
                CError_FATAL(1048);
        }
    }

    return CInit_DeclareString(buf, size, FALSE, FALSE);
}

static Object* CSOM_GenerateSignature(SOMGenerator* gen) {
    Object* object;
    SOMMethod* method;

    name_mangle_list.size = 0;

    for (method = gen->methods; method; method = method->next) {
        if (method->state == SOMMS_Method) {
            CSOM_GetFuncSig(TYPE_FUNC(method->u.object->type), TRUE);
        }
    }

    COS_LockHandle(name_mangle_list.data);
    object = CInit_DeclareString(*name_mangle_list.data, name_mangle_list.size, FALSE, FALSE);
    COS_UnlockHandle(name_mangle_list.data);
    return object;
}

static Object* CSOM_GenerateMethodNames(SOMGenerator* gen) {
    Object* object;
    SOMMethod* method;
    HashNameNode* name;

    name_mangle_list.size = 0;

    for (method = gen->methods; method; method = method->next) {
        if (method->name) {
            name = CSOM_NameTranslate(method->name);
            AppendGListID(&name_mangle_list, name->name);
        }
    }

    COS_LockHandle(name_mangle_list.data);
    object = CInit_DeclareString(*name_mangle_list.data, name_mangle_list.size, FALSE, FALSE);
    COS_UnlockHandle(name_mangle_list.data);
    return object;
}

static void CSOM_SetupClassCounts(
    SOMGenerator* gen, TypeClass* tclass, somStaticClassCounts* counts
) {
    gen->counts.majorVersion = tclass->sominfo->majorversion;
    gen->counts.minorVersion = tclass->sominfo->minorversion;
    gen->counts.flags = cfSharedStrings;
    if (gen->hasNew) {
        gen->counts.flags |= cfClassAllocate;
    }
    if (gen->hasDelete) {
        gen->counts.flags |= cfClassDeallocate;
    }

    switch (tclass->align) {
        case 1:
            gen->counts.dataAlignment = 0;
            break;
        case 2:
            gen->counts.dataAlignment = 1;
            break;
        case 4:
            gen->counts.dataAlignment = 2;
            break;
        case 8:
            gen->counts.dataAlignment = 3;
            break;
        default:
            gen->counts.dataAlignment = 4;
            break;
    }

    gen->counts.numSelectInherited = 0;

    memclrw(counts, sizeof(somStaticClassCounts));
    counts->majorVersion = CTool_EndianConvertWord32(gen->counts.majorVersion);
    counts->minorVersion = CTool_EndianConvertWord32(gen->counts.minorVersion);
    counts->flags = CTool_EndianConvertWord32(gen->counts.flags);
    counts->dataAlignment = CTool_EndianConvertWord16(gen->counts.dataAlignment);
    counts->classTokenCount = CTool_EndianConvertWord16(gen->counts.classTokenCount);
    counts->numDirectParents = CTool_EndianConvertWord16(gen->counts.numDirectParents);
    counts->numMetaClasses = CTool_EndianConvertWord16(gen->counts.numMetaClasses);
    counts->numOverriddenAncestors = CTool_EndianConvertWord16(gen->counts.numOverriddenAncestors);
    counts->numMigratedMethods = CTool_EndianConvertWord16(gen->counts.numMigratedMethods);
    counts->numSelectInherited = CTool_EndianConvertWord16(gen->counts.numSelectInherited);
}

static void CSOM_GenerateSCIObject(SOMGenerator* gen, TypeClass* tclass) {
    Object* object;
    OLinkList* relocs;
    OLinkList* reloc;
    somStaticClassInfo sci;
    somStaticClassCounts classCounts;

    object = CSOM_MakeObject(tclass->classname->name, "SCI", sizeof(sci));
    object->sclass = TK_STATIC;

    memclrw(&sci, sizeof(sci));
    sci.layoutVersion = CTool_EndianConvertWord32(70);

    relocs = NULL;
    reloc = lalloc(sizeof(OLinkList));
    reloc->next = relocs;
    relocs = reloc;
    reloc->obj = tclass->sominfo->classdataobject;
    reloc->offset = 4;
    reloc->addend = 0;

    if (gen->overrideProcsObj) {
        reloc = lalloc(sizeof(OLinkList));
        reloc->next = relocs;
        relocs = reloc;
        reloc->obj = gen->overrideProcsObj;
        reloc->offset = 8;
        reloc->addend = 0;
    }

    if (gen->classAncestorsObj) {
        reloc = lalloc(sizeof(OLinkList));
        reloc->next = relocs;
        relocs = reloc;
        reloc->obj = gen->classAncestorsObj;
        reloc->offset = 12;
        reloc->addend = 0;
    }

    if (gen->dlldFunc) {
        reloc = lalloc(sizeof(OLinkList));
        reloc->next = relocs;
        relocs = reloc;
        reloc->obj = gen->dlldFunc;
        reloc->offset = 16;
        reloc->addend = 0;
    }

    if (gen->specialProcsObj) {
        reloc = lalloc(sizeof(OLinkList));
        reloc->next = relocs;
        relocs = reloc;
        reloc->obj = gen->specialProcsObj;
        reloc->offset = 20;
        reloc->addend = 0;
    }

    CSOM_SetupClassCounts(gen, tclass, &classCounts);

    reloc = lalloc(sizeof(OLinkList));
    reloc->next = relocs;
    relocs = reloc;
    reloc->obj = CInit_DeclareString((char*)&classCounts, sizeof(classCounts), FALSE, FALSE);
    reloc->offset = 52;
    reloc->addend = 0;

    reloc = lalloc(sizeof(OLinkList));
    reloc->next = relocs;
    relocs = reloc;
    reloc->obj = CInit_DeclareString(
        tclass->classname->name, strlen(tclass->classname->name) + 1, FALSE, FALSE
    );
    reloc->offset = 56;
    reloc->addend = 0;

    sci.classDescription.instanceDataSize = CTool_EndianConvertWord32(tclass->size);

    reloc = lalloc(sizeof(OLinkList));
    reloc->next = relocs;
    relocs = reloc;
    reloc->obj = CSOM_GenerateParentVersion(gen);
    reloc->offset = 64;
    reloc->addend = 0;

    if (gen->methods) {
        reloc = lalloc(sizeof(OLinkList));
        reloc->next = relocs;
        relocs = reloc;
        reloc->obj = CSOM_GenerateSlotUsage(gen);
        reloc->offset = 68;
        reloc->addend = 0;

        reloc = lalloc(sizeof(OLinkList));
        reloc->next = relocs;
        relocs = reloc;
        reloc->obj = CSOM_GenerateSignature(gen);
        reloc->offset = 72;
        reloc->addend = 0;

        reloc = lalloc(sizeof(OLinkList));
        reloc->next = relocs;
        relocs = reloc;
        reloc->obj = CSOM_GenerateMethodNames(gen);
        reloc->offset = 76;
        reloc->addend = 0;
    }

    if (gen->overrideProcsObj) {
        reloc = lalloc(sizeof(OLinkList));
        reloc->next = relocs;
        relocs = reloc;
        reloc->obj = CSOM_GenerateOverrideData(gen);
        reloc->offset = 80;
        reloc->addend = 0;
    }

    if (gen->counts.numMigratedMethods) {
        reloc = lalloc(sizeof(OLinkList));
        reloc->next = relocs;
        relocs = reloc;
        reloc->obj = CSOM_GenerateMigrateData(gen);
        reloc->offset = 84;
        reloc->addend = 0;
    }

    sci.classDescription.selectedInherited = 0;

    CInit_DeclareData(object, &sci, relocs, object->type->size);
    gen->sciObj = object;
}

static void CSOM_GenerateClassDataObject(SOMGenerator* gen, TypeClass* tclass) {
    void* buf;
    OLinkList* relocs;
    OLinkList* reloc;
    SOMMethod* method;
    SInt32 size;

    relocs = NULL;
    for (size = 24, method = gen->methods; method; method = method->next) {
        if (method->state == SOMMS_Method) {
            reloc = lalloc(sizeof(OLinkList));
            reloc->next = relocs;
            relocs = reloc;
            reloc->obj = method->u.object;
            reloc->offset = size;
            reloc->addend = 0;
        }
        size += 4;
    }

    memclrw(buf = lalloc(size), size);

    reloc = lalloc(sizeof(OLinkList));
    reloc->next = relocs;
    relocs = reloc;
    reloc->obj = gen->sciObj;
    reloc->offset = 4;
    reloc->addend = 0;

    tclass->sominfo->classdataobject->type->size = size;
    CInit_DeclareData(
        tclass->sominfo->classdataobject, buf, relocs, tclass->sominfo->classdataobject->type->size
    );
}

void CSOM_GenerateClassStructures(TypeClass* tclass) {
    SOMGenerator gen;

    memclrw(&gen, sizeof(gen));
    CSOM_GenerateOverrideIntroLists(&gen, tclass);
    CSOM_GenerateClassAncestors(&gen, tclass);
    CSOM_GenerateOverrideProcs(&gen, tclass);
    CSOM_GenerateDLLDFunc(&gen, tclass);
    CSOM_GenerateSpecialProcs(&gen, tclass);
    CSOM_GenerateSCIObject(&gen, tclass);
    CSOM_GenerateClassDataObject(&gen, tclass);
}

static TypeClass* CSOM_GetCurrentSOMClass(void) {
    if (cscope_current->theclass && cscope_current->theclass->sominfo) {
        return cscope_current->theclass;
    }

    CError_Error(ERR_ILLEGAL_USE_PRAGMA_OUTSIDE_SOM_CLASS);
    return NULL;
}

void CSOM_PragmaReleaseOrder(void) {
    TypeClass* tclass;
    SOMReleaseOrder* firstOrder;
    SOMReleaseOrder* order;
    SOMReleaseOrder** ptr;
    Boolean noParen;
    short token;

    if (!(tclass = CSOM_GetCurrentSOMClass())) {
        return;
    }

    token = CPrep_PragmaLex(FALSE);
    if (token != '(') {
        if (token != TK_IDENTIFIER) {
            CPrep_Error(ERR_LPAREN_EXPECTED);
            return;
        }

        if (!strcmp(tkidentifier->name, "list")) {
            token = CPrep_PragmaLex(FALSE);
            if (token != TK_IDENTIFIER) {
                CPrep_Error(ERR_IDENTIFIER_EXPECTED);
                return;
            }
        }

        noParen = TRUE;
    } else {
        noParen = FALSE;
        token = CPrep_PragmaLex(FALSE);
    }

    firstOrder = NULL;
    if (noParen || token != ')') {
        ptr = &firstOrder;
        while (1) {
            if (token != TK_IDENTIFIER) {
                CPrep_Error(ERR_IDENTIFIER_EXPECTED);
                return;
            }

            for (order = firstOrder; order; order = order->next) {
                if (order->name == tkidentifier) {
                    CError_Error(ERR_IDENTIFIER_REDECLARED, tkidentifier->name);
                    return;
                }
            }

            order = galloc(sizeof(SOMReleaseOrder));
            *ptr = order;
            ptr = &order->next;

            order->next = NULL;
            order->name = tkidentifier;
            order->state = SOMMS_Deleted;

            if (noParen) {
                token = CPrep_PragmaLex(TRUE);
                if (!token) {
                    break;
                }
            } else {
                token = CPrep_PragmaLex(FALSE);
                if (token == ')') {
                    break;
                }
            }

            if (token != ',') {
                CPrep_Error(ERR_COMMA_EXPECTED);
                return;
            }

            token = CPrep_PragmaLex(noParen);
        }
    }

    tclass->sominfo->order = firstOrder;
}

void CSOM_PragmaClassVersion(void) {
    Type* type;

    if (CPrep_PragmaLex(FALSE) != '(') {
        CPrep_Error(ERR_LPAREN_EXPECTED);
        return;
    }

    if (CPrep_PragmaLex(FALSE) != TK_IDENTIFIER) {
        CPrep_Error(ERR_IDENTIFIER_EXPECTED);
        return;
    }

    type = CScope_GetTagType(cscope_current, tkidentifier);
    if (!(type && IS_TYPE_CLASS(type) && TYPE_CLASS(type)->sominfo)) {
        CPrep_ErrorName(ERR_NOT_SOM_CLASS, tkidentifier->name);
        return;
    }

    if (CPrep_PragmaLex(FALSE) != ',') {
        CPrep_Error(ERR_COMMA_EXPECTED);
        return;
    }

    if (CPrep_PragmaLex(FALSE) != TK_INTCONST) {
        CPrep_Error(ERR_ILLEGAL_PRAGMA);
        return;
    }

    TYPE_CLASS(type)->sominfo->majorversion = CInt64_GetULong(&tkintconst);

    if (CPrep_PragmaLex(FALSE) != ',') {
        CPrep_Error(ERR_COMMA_EXPECTED);
        return;
    }

    if (CPrep_PragmaLex(FALSE) != TK_INTCONST) {
        CPrep_Error(ERR_ILLEGAL_PRAGMA);
        return;
    }

    TYPE_CLASS(type)->sominfo->minorversion = CInt64_GetULong(&tkintconst);

    if (CPrep_PragmaLex(FALSE) != ')') {
        CPrep_Error(ERR_RPAREN_EXPECTED);
        return;
    }
}

void CSOM_PragmaMetaClass(void) {
    Type* type;
    Type* type2;

    if (CPrep_PragmaLex(FALSE) != '(') {
        CPrep_Error(ERR_LPAREN_EXPECTED);
        return;
    }

    if (CPrep_PragmaLex(FALSE) != TK_IDENTIFIER) {
        CPrep_Error(ERR_IDENTIFIER_EXPECTED);
        return;
    }

    type = CScope_GetTagType(cscope_current, tkidentifier);
    if (!(type && IS_TYPE_CLASS(type) && TYPE_CLASS(type)->sominfo)) {
        CPrep_ErrorName(ERR_NOT_SOM_CLASS, tkidentifier->name);
        return;
    }

    if (CPrep_PragmaLex(FALSE) != ',') {
        CPrep_Error(ERR_COMMA_EXPECTED);
        return;
    }

    if (CPrep_PragmaLex(FALSE) != TK_IDENTIFIER) {
        CPrep_Error(ERR_IDENTIFIER_EXPECTED);
        return;
    }

    type2 = CScope_GetTagType(cscope_current, tkidentifier);
    if (!(type2 && IS_TYPE_CLASS(type2) && TYPE_CLASS(type2)->sominfo)) {
        CPrep_ErrorName(ERR_NOT_SOM_CLASS, tkidentifier->name);
        return;
    }

    TYPE_CLASS(type)->sominfo->metaclass = TYPE_CLASS(type2);

    if (CPrep_PragmaLex(FALSE) != ')') {
        CPrep_Error(ERR_RPAREN_EXPECTED);
        return;
    }
}

void CSOM_PragmaCallStyle(void) {
    TypeClass* tclass;

    if (!(tclass = CSOM_GetCurrentSOMClass())) {
        return;
    }

    if (CPrep_PragmaLex(FALSE) != TK_IDENTIFIER) {
        CPrep_Error(ERR_IDENTIFIER_EXPECTED);
        return;
    }

    if (!strcmp(tkidentifier->name, "IDL")) {
        tclass->sominfo->oidl_callstyle = FALSE;
        return;
    }

    if (!strcmp(tkidentifier->name, "OIDL")) {
        tclass->sominfo->oidl_callstyle = TRUE;
        return;
    }

    CPrep_Error(ERR_ILLEGAL_PRAGMA);
}

void CSOM_FixNewDeleteFunctype(TypeFunc* tfunc) {
    FuncArg* arg = CParser_NewFuncArg();
    arg->name = GetHashNameNodeExport("__theclass");
    arg->type = CDecl_NewPointerType(CSOM_FindClassType(GetHashNameNodeExport("SOMClass")));
    arg->next = tfunc->args;
    tfunc->args = arg;
}

static Object* CSOM_FindRTFunc(char* namestr, char* sig) {
    NameSpaceObjectList* nsol;
    Object* object;
    FuncArg* arg;

    if ((nsol = CScope_GetLocalObject(cscope_root, GetHashNameNodeExport(namestr))) &&
        nsol->object->otype == OT_OBJECT) {
        object = OBJECT(nsol->object);
        if (IS_TYPE_FUNC(object->type) &&
            *(sig++) == 'p' &&
            IS_TYPE_POINTER_ONLY(TYPE_FUNC(object->type)->functype)) {
            for (arg = TYPE_FUNC(object->type)->args; arg; arg = arg->next) {
                switch (*(sig++)) {
                    case 'p':
                        if (!IS_TYPE_POINTER_ONLY(arg->type)) {
                            break;
                        }
                        continue;
                    case 'i':
                        if (arg->type != TYPE(&stsignedint)) {
                            break;
                        }
                        continue;
                    case 'I':
                        if (arg->type != TYPE(&stunsignedint)) {
                            break;
                        }
                        continue;
                    case 'l':
                        if (arg->type != TYPE(&stsignedlong)) {
                            break;
                        }
                        continue;
                    case 'L':
                        if (arg->type != TYPE(&stunsignedlong)) {
                            break;
                        }
                        continue;
                }
                break;
            }

            if (arg == NULL && *sig == 0) {
                return object;
            }
        }

        CError_Error(ERR_SOM_RUNTIME_FUNCTION_UNEXPECTED_TYPE, namestr);
    } else {
        CError_Error(ERR_SOM_RUNTIME_FUNCTION_NOT_DEFINED_SHOULD, namestr);
    }

    return NULL;
}

static ENode* CSOM_MakeTempCondition(ENode* left, ENode* cond, ENode* expr1, ENode* right) {
    ENode* expr;

    expr = lalloc(sizeof(ENode));
    expr->type = ECOND;
    expr->cost = 0;
    expr->flags = 0;
    expr->rtype = &stvoid;
    expr->data.cond.cond = cond;
    expr->data.cond.expr1 = expr1;
    expr->data.cond.expr2 = nullnode();
    expr->data.cond.expr2->rtype = &stvoid;

    if (left) {
        expr = makediadicnode(left, expr, ECOMMA);
    }

    if (right) {
        expr = makediadicnode(expr, right, ECOMMA);
        expr->rtype = right->rtype;
    }

    return expr;
}

ENode* CSOM_New(TypeClass* tclass) {
    Object* newFunc;
    ENode* notExpr;
    ENode* tempExpr;
    ENode* checkExpr;
    ENode* expr;

    if (tk == '(') {
        if ((tk = lex()) == ')') {
            tk = lex();
        } else {
            CError_Error(ERR_NO_PARAMETERS_ALLOWED_SOM_CLASS_CONSTRUCTORS);
        }
    }

    if (!copts.som_env_check || !copts.som_call_optimize) {
        newFunc = CSOM_FindRTFunc("somNewObjectInstance", "ppll");
        if (!newFunc) {
            return nullnode();
        }
    } else {
        newFunc = rt_som_new;
    }

    expr = funccallexpr(
        newFunc,
        create_objectrefnode(tclass->sominfo->classdataobject),
        intconstnode(TYPE(&stunsignedlong), tclass->sominfo->majorversion),
        intconstnode(TYPE(&stunsignedlong), tclass->sominfo->minorversion),
        NULL
    );
    expr->rtype = CDecl_NewPointerType(TYPE(tclass));

    if (copts.som_env_check && !copts.som_call_optimize) {
        tempExpr = CExpr_GetETEMPCopy(expr);
        checkExpr = funccallexpr(rt_som_newcheck, nullnode(), NULL, NULL, NULL);
        notExpr = makemonadicnode(expr, ELOGNOT);
        notExpr->rtype = CParser_GetBoolType();
        expr = CSOM_MakeTempCondition(NULL, notExpr, checkExpr, tempExpr);
    }

    return expr;
}

ENode* CSOM_Delete(TypeClass* tclass, ENode* objExpr) {
    Object* func;

    if ((func = CSOM_FindRTFunc("somReleaseObjectReference", "pp"))) {
        return funccallexpr(func, objExpr, NULL, NULL, NULL);
    }

    return nullnode();
}

void CSOM_InitAutoClass(Object* object) {
    Type* type;
    Statement* stmt;
    Object* func;

    if ((func = CSOM_FindRTFunc("somReleaseObjectReference", "pp"))) {
        type = object->type;
        object->type = CDecl_NewPointerType(type);
        TPTR_QUAL(object->type) = Q_REFERENCE;

        stmt = CFunc_AppendStatement(ST_EXPRESSION);
        stmt->expr = makediadicnode(create_objectnode2(object), CSOM_New(TYPE_CLASS(type)), EASS);
        CExcept_RegisterDeleteObject(stmt, object, func);
    }
}

static void CSOM_FindIntroClassOffset(
    TypeClass* tclass, Object* func, TypeClass** resultClass, SInt32* resultOffset
) {
    Object* scan;
    VClassList* vbase;
    CScopeObjectIterator iter;

    if (!(TYPE_FUNC(func->type)->flags & FUNC_OVERRIDE)) {
        CScope_InitObjectIterator(&iter, tclass->nspace);
        while (1) {
            if (!(scan = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
                break;
            }

            if (scan == func) {
                *resultClass = tclass;
                *resultOffset = CSOM_GetTokenOffset(scan);
                return;
            }
        }

        for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
            CScope_InitObjectIterator(&iter, vbase->base->nspace);
            while (1) {
                if (!(scan = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
                    break;
                }

                if (scan == func) {
                    *resultClass = vbase->base;
                    *resultOffset = CSOM_GetTokenOffset(scan);
                    return;
                }
            }
        }
    } else {
        for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
            CScope_InitObjectIterator(&iter, vbase->base->nspace);
            while (1) {
                if (!(scan = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
                    break;
                }

                if (scan->name == func->name) {
                    if (IS_TYPE_FUNC(scan->type) &&
                        scan->datatype == DVFUNC &&
                        !(TYPE_FUNC(scan->type)->flags & FUNC_OVERRIDE) &&
                        CClass_GetOverrideKind(
                            TYPE_FUNC(func->type), TYPE_FUNC(scan->type), FALSE
                        )) {
                        *resultClass = vbase->base;
                        *resultOffset = CSOM_GetTokenOffset(scan);
                        return;
                    }
                    break;
                }
            }
        }
    }

    CError_FATAL(1731);
}

static ENode* CSOM_ComputeSOMSelf(TypeClass* tclass, ENode* selfExpr) {
    ENode* expr;
    Object obj;

    expr = create_objectrefnode(tclass->sominfo->classdataobject);
    expr = makediadicnode(expr, intconstnode(TYPE(&stsignedlong), 8), EADD);
    expr->rtype = CDecl_NewPointerType(TYPE(&SOMIDT_type));
    expr = makemonadicnode(expr, EINDIRECT);

    memclrw(&obj, sizeof(Object));
    obj.otype = OT_OBJECT;
    obj.type = TYPE(&SOMIDT_type);
    obj.name = no_name_node;
    obj.datatype = DFUNC;
    selfExpr = funccallexpr(&obj, selfExpr, NULL, NULL, NULL);

    CError_ASSERT(1761, ENODE_IS(selfExpr, EFUNCCALL));

    selfExpr->data.funccall.funcref = expr;

    return selfExpr;
}

ENode* CSOM_SOMSelfObjectExpr(TypeClass* tclass) {
    ObjectList* list;
    Object* obj;

    for (list = locals; list; list = list->next) {
        if (list->object->name == csom_selfname) {
            return create_objectnode(list->object);
        }
    }

    obj = CParser_NewLocalDataObject(NULL, TRUE);
    obj->name = csom_selfname;
    obj->type = CDecl_NewPointerType(TYPE(tclass));
    CFunc_SetupLocalVarInfo(obj);
    return create_objectnode(obj);
}

void CSOM_InitSOMSelf(TypeClass* tclass, Statement* stmt) {
    ObjectList* list;
    HashNameNode* name;
    ENode* selfExpr;
    Statement* newStmt;

    name = GetHashNameNodeExport("__somself");
    for (list = locals; list; list = list->next) {
        if (list->object->name == name) {
            selfExpr = CClass_CreateThisSelfExpr();
            CError_ASSERT(1811, selfExpr);

            selfExpr = CSOM_ComputeSOMSelf(tclass, selfExpr);

            newStmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
            newStmt->expr = makediadicnode(create_objectnode(list->object), selfExpr, EASS);
            break;
        }
    }
}

ENode* CSOM_EnvCheck(ENode* funccall, ENodeList* checkArg) {
    ENodeList* arg;
    ENodeList* arg2;
    ENode* resultTemp;
    ENode* objTemp;
    ENode* checkExpr;
    Type* returnType;

    returnType = funccall->rtype;
    CError_ASSERT(1842, arg = funccall->data.funccall.args);

    if (arg == checkArg) {
        CError_ASSERT(1845, arg = arg->next);
    }

    CError_ASSERT(1847, arg2 = arg->next);

    if (arg2 == checkArg) {
        CError_ASSERT(1850, arg2 = arg2->next);
    }

    CError_ASSERT(1852, IS_TYPE_POINTER_ONLY(arg2->node->rtype));

    if (!IS_TYPE_VOID(funccall->data.funccall.functype->functype)) {
        if (checkArg) {
            if (ENODE_IS(checkArg->node, ETEMP)) {
                if (checkArg->node->data.temp.uniqueid == 0) {
                    checkArg->node->data.temp.uniqueid = CParser_GetUniqueID();
                }

                resultTemp = lalloc(sizeof(ENode));
                *resultTemp = *checkArg->node;
                resultTemp->data.temp.needs_dtor = FALSE;
            } else {
                resultTemp = CExpr_GetETEMPCopy(checkArg->node);
            }
        } else {
            resultTemp = CExpr_GetETEMPCopy(funccall);
        }
    } else {
        resultTemp = NULL;
    }

    if (!ENODE_IS(arg2->node, EOBJREF)) {
        if (ENODE_IS_INDIRECT_TO(arg2->node, EOBJREF) &&
            arg2->node->data.monadic->data.objref->datatype == DLOCAL) {
            objTemp = lalloc(sizeof(ENode));
            *objTemp = *arg2->node;
        } else {
            objTemp = CExpr_GetETEMPCopy(arg2->node);
        }
    } else {
        objTemp = lalloc(sizeof(ENode));
        *objTemp = *arg2->node;
    }

    if (copts.som_call_optimize) {
        ENode* checkCall;
        checkCall = funccallexpr(rt_som_check, objTemp, NULL, NULL, NULL);
        funccall = makediadicnode(funccall, checkCall, ECOMMA);
        if (resultTemp) {
            funccall = makediadicnode(funccall, resultTemp, ECOMMA);
        }
    } else {
        checkExpr = lalloc(sizeof(ENode));
        *checkExpr = *objTemp;
        checkExpr = makemonadicnode(checkExpr, EINDIRECT);
        checkExpr->rtype = TYPE(&stsignedlong);

        funccall = CSOM_MakeTempCondition(
            funccall, checkExpr, funccallexpr(rt_som_check, objTemp, NULL, NULL, NULL), resultTemp
        );
    }

    funccall->rtype = returnType;
    return funccall;
}

static Boolean CSOM_CanUseGlueCall(TypeFunc* tfunc) {
    int gprCounter;
    int fprCounter;
    FuncArg* arg;

    gprCounter = 8;
    fprCounter = 13;
    if (CMach_GetFunctionResultClass(tfunc) != 0) {
        gprCounter--;
    }

    for (arg = tfunc->args; arg; arg = arg->next) {
        if (arg == &elipsis || arg == &oldstyle) {
            return FALSE;
        }

        switch (arg->type->type) {
            case TYPEINT:
            case TYPEENUM:
            case TYPEPOINTER:
                if (--gprCounter < 0) {
                    return FALSE;
                }
                break;
            case TYPEFLOAT:
                if (--fprCounter < 0) {
                    return FALSE;
                }
                break;
            default:
                return FALSE;
        }
    }

    return TRUE;
}

static char* CSOM_AppendString(char* dst, char* src) {
    char ch;
    while ((ch = *(src++))) {
        *(dst++) = ch;
    }
    return dst;
}

static ENode* CSOM_SOMGlueCall(TypeClass* tclass, SInt32 offset, Object* object) {
    UInt32 bufsize;
    char* buf;
    UInt8 funcResultClass;
    char* ptr;
    Object* stubObj;
    CSOMStub* stub;
    ENode* expr;
    char mybuf[256];
    char numberbuf[16];

    for (stub = csom_stubs; stub; stub = stub->next) {
        if (stub->tclass == tclass && stub->offset == offset) {
            break;
        }
    }

    if (!stub) {
        funcResultClass = CMach_GetFunctionResultClass(TYPE_FUNC(object->type));

        bufsize = strlen(tclass->sominfo->classdataobject->name->name) + 32;
        if (bufsize > sizeof(mybuf)) {
            buf = lalloc(bufsize);
        } else {
            buf = mybuf;
        }

        ptr = CSOM_AppendString(buf, "__glue_");
        if (tclass->sominfo->oidl_callstyle == 0) {
            if (funcResultClass == 0) {
                *(ptr++) = '4';
            } else {
                *(ptr++) = '5';
            }
        } else {
            *(ptr++) = '_';
        }
        *(ptr++) = '_';

        sprintf(numberbuf, "%ld", (long)strlen(tclass->sominfo->classdataobject->name->name));
        ptr = CSOM_AppendString(ptr, numberbuf);
        ptr = CSOM_AppendString(ptr, tclass->sominfo->classdataobject->name->name);
        *(ptr++) = '_';
        sprintf(numberbuf, "%ld", offset);
        ptr = CSOM_AppendString(ptr, numberbuf);
        *ptr = 0;

        stubObj = CParser_NewCompilerDefFunctionObject();
        stubObj->nspace = cscope_root;
        stubObj->name = GetHashNameNodeExport(buf);
        stubObj->u.func.linkname = stubObj->name;
        stubObj->type = object->type;
        stubObj->qual = object->qual | Q_IMPLICIT_WEAK;
        stubObj->flags = OBJECT_INTERNAL;
        CScope_AddObject(stubObj->nspace, stubObj->name, OBJ_BASE(stubObj));

        stub = galloc(sizeof(CSOMStub));
        stub->next = csom_stubs;
        stub->object = stubObj;
        stub->tclass = tclass;
        stub->offset = offset;
        csom_stubs = stub;

        if (tclass->sominfo->oidl_callstyle == 0) {
            if (funcResultClass == 0) {
                stub->kind = 0;
            } else {
                stub->kind = 1;
            }
        } else {
            stub->kind = 2;
        }
    }

    expr = create_objectrefnode(stub->object);
    expr->rtype = CDecl_NewPointerType(object->type);
    return expr;
}

ENode* CSOM_MemberVarAccess(BClassList* path, ObjMemberVar* ivar, ENode* thisExpr) {
    if (!thisExpr) {
        if (!cscope_currentfunc ||
            !cscope_currentclass ||
            !cscope_is_member_func ||
            !(thisExpr = CClass_CreateThisSelfExpr())) {
            CError_Error(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
            return NULL;
        }
    }

    CError_ASSERT(2069, ENODE_IS(thisExpr, EINDIRECT));

    thisExpr = thisExpr->data.monadic;

    if (path->next == NULL &&
        cscope_currentclass == TYPE_CLASS(path->type) &&
        ENODE_IS(thisExpr, EOBJREF) &&
        thisExpr->data.objref->name == this_name_node) {
        thisExpr = CSOM_SOMSelfObjectExpr(cscope_currentclass);
    } else {
        CClass_CheckPathAccess(path, NULL, ivar->access);
        if (ivar->has_path) {
            path = OBJ_MEMBER_VAR_PATH(ivar)->path;
        }
        while (path->next) {
            path = path->next;
        }
        thisExpr = CSOM_ComputeSOMSelf(TYPE_CLASS(path->type), thisExpr);
    }

    thisExpr = makemonadicnode(thisExpr, EINDIRECT);
    thisExpr->rtype = path->type;
    return CClass_AccessMember(thisExpr, ivar->type, ivar->qual, ivar->offset);
}

ENode* CSOM_MethodAccess(BClassList* path, Object* func, Boolean isQualified) {
    TypeClass* tclass;
    TypeClass* tclass2;
    TypeClass* tclass3;
    SInt32 offset;
    SInt32 counter;
    ENode* indirectExpr;
    ENode* expr;
    ENode* classref;
    ClassList* base;
    Object* resolveFunc;

    CError_ASSERT(2107, path != NULL);

    tclass = TYPE_CLASS(path->type);
    if (path->next) {
        path = path->next;
    }
    tclass2 = TYPE_CLASS(path->type);

    if (isQualified) {
        counter = 0;
        if (tclass != tclass2) {
            for (base = tclass->bases; base; base = base->next) {
                counter++;
                if (base->base == tclass2) {
                    break;
                }
            }

            if (!base) {
                CError_Error(ERR_SOM_CLASS_ACCESS_QUALIFICATION_ONLY_ALLOWED);
            }
        }

        CSOM_FindIntroClassOffset(tclass2, func, &tclass3, &offset);
        classref = create_objectrefnode(tclass3->sominfo->classdataobject);
        indirectExpr = makediadicnode(classref, intconstnode(TYPE(&stsignedlong), offset), EADD);
        indirectExpr = makemonadicnode(indirectExpr, EINDIRECT);
        indirectExpr->rtype = CDecl_NewPointerType(func->type);

        resolveFunc = CSOM_FindRTFunc("somParentNumResolve", "ppip");
        if (!resolveFunc) {
            return nullnode();
        }

        expr = funccallexpr(
            resolveFunc,
            create_objectrefnode(tclass->sominfo->classdataobject),
            intconstnode(TYPE(&stsignedint), counter),
            indirectExpr,
            NULL
        );
        expr->rtype = indirectExpr->rtype;
        if (copts.som_env_check && tclass3->sominfo->oidl_callstyle == 0) {
            expr->flags |= ENODE_FLAG_SOM_ENV_CHECK;
        }
    } else {
        CSOM_FindIntroClassOffset(tclass2, func, &tclass3, &offset);
        if (copts.som_call_optimize && CSOM_CanUseGlueCall(TYPE_FUNC(func->type))) {
            return CSOM_SOMGlueCall(tclass3, offset, func);
        }

        classref = create_objectrefnode(tclass3->sominfo->classdataobject);
        expr = makediadicnode(classref, intconstnode(TYPE(&stsignedlong), offset), EADD);
        expr = makemonadicnode(expr, EINDIRECT);
        expr->rtype = CDecl_NewPointerType(func->type);
        if (copts.som_env_check && tclass3->sominfo->oidl_callstyle == 0) {
            expr->flags |= ENODE_FLAG_SOM_ENV_CHECK;
        }
    }

    return expr;
}
