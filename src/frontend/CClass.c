#include "compiler/CClass.h"
#include "compiler/CABI.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CInline.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CObjC.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CRTTI.h"
#include "compiler/CScope.h"
#include "compiler/CSOM.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/tokens.h"
#include "compiler/types.h"

#pragma pack(2)
typedef struct OVClassBase {
    struct OVClassBase* next;
    struct OVClass* ovclass;
    Boolean is_virtual;
} OVClassBase;

typedef struct OVFunc {
    struct OVFunc* next;
    Object* obj;
    struct OVClass* overriderclass;
    struct OVFunc* overrider;
    struct OVFunc* overrider2;
} OVFunc;

typedef struct OVClass {
    TypeClass* tclass;
    OVFunc* vfuncs;
    OVClassBase* bases;
    SInt32 offset;
    SInt32 voffset;
    Boolean alloced_vtable;
} OVClass;

typedef struct ThunkList {
    struct ThunkList* next;
    Object* thunkobj;
    Object* obj;
    SInt32 this_delta;
    SInt32 return_delta;
    SInt32 ctoroffset;
} ThunkList;

static TypeClass* main_class;
static ThunkList* cclass_thunklist;
static TypeClass* cclass_isbase_mostderived;
static SInt32 cclass_isbase_foundoffset;
static Boolean cclass_isambigbase;
static short cclass_founddepth;
static char* vtable_object_data;
static SInt32 vtable_data_size;
static OLinkList* vtable_object_links;
static TypeClass* cclass_vbase;
static OVClass* cclass_ovbase;
static OVClass* cclass_root;
static Object* found_pure;
static Boolean check_pures;
static Object* cclass_dominator_vobject;
static SInt32 cclass_dominator_voffset;
static Object* cclass_dominator_oobject;
static TypeClass* cclass_dominator_oclass;
static SInt32 cclass_dominator_ooffset;
static Object* cclass_dominator_eobject;

void CClass_Init(void) {
    cclass_thunklist = NULL;
}

void CClass_GenThunks(void) {
    ThunkList* list;

    for (list = cclass_thunklist; list; list = list->next) {
        list->thunkobj->flags |= OBJECT_DEFINED;
        CodeGen_GenVDispatchThunk(
            list->thunkobj, list->obj, list->this_delta, list->return_delta, list->ctoroffset
        );
    }
}

static Object* CClass_ThunkObject(
    Object* obj, SInt32 this_delta, SInt32 return_delta, SInt32 ctoroffset
) {
    Object* thunkobj;
    ThunkList* list;

    CInline_ObjectAddrRef(obj);
    for (list = cclass_thunklist; list; list = list->next) {
        if (obj == list->obj &&
            this_delta == list->this_delta &&
            return_delta == list->return_delta &&
            ctoroffset == list->ctoroffset) {
            return list->thunkobj;
        }
    }

    thunkobj = CParser_NewCompilerDefFunctionObject();
    thunkobj->name = CMangler_ThunkName(obj, this_delta, return_delta, ctoroffset);
    thunkobj->type = TYPE(&rt_func);
    thunkobj->sclass = TK_EXTERN;
    thunkobj->qual = Q_IMPLICIT_WEAK;
    thunkobj->u.func.linkname = thunkobj->name;

    list = galloc(sizeof(ThunkList));
    list->thunkobj = thunkobj;
    list->obj = obj;
    list->this_delta = this_delta;
    list->return_delta = return_delta;
    list->ctoroffset = ctoroffset;
    list->next = cclass_thunklist;
    cclass_thunklist = list;

    return thunkobj;
}

static Boolean CClass_IsZeroOffsetClass(TypeClass* a, TypeClass* b) {
    while (1) {
        if (a == b) {
            return TRUE;
        }

        if (!a->bases || a->bases->is_virtual) {
            return FALSE;
        }

        a = a->bases->base;
    }
}

static UInt8 CClass_IsCovariantResult(
    Type* a, UInt32 qualA, Type* b, UInt32 qualB, Boolean errorflag
) {
    TypeClass* tclassA;
    TypeClass* tclassB;

    if (IS_TYPE_POINTER_ONLY(a) &&
        IS_TYPE_POINTER_ONLY(b) &&
        TPTR_QUAL(a) == TPTR_QUAL(b) &&
        !CParser_IsMoreCVQualified(qualB, qualA) &&
        IS_TYPE_CLASS(TPTR_TARGET(a)) &&
        IS_TYPE_CLASS(TPTR_TARGET(b))) {
        tclassA = TYPE_CLASS(TPTR_TARGET(a));
        tclassB = TYPE_CLASS(TPTR_TARGET(b));
        if (tclassA == tclassB ||
            CClass_IsBaseClass(tclassB, tclassA, NULL, errorflag, errorflag)) {
            if (!CClass_IsZeroOffsetClass(tclassB, tclassA)) {
                return 2;
            } else {
                return 1;
            }
        }
    }

    return 0;
}

UInt8 CClass_GetOverrideKind(TypeFunc* a, TypeFunc* b, Boolean errorflag) {
    if (!a->args || !b->args) {
        return 0;
    }

    if ((a->flags & FUNC_CALL_CONV_MASK) != (b->flags & FUNC_CALL_CONV_MASK) ||
        !is_arglistsame(a->args->next, b->args->next) ||
        a->args->qual != b->args->qual) {
        return 0;
    }

    if (!is_typesame(a->functype, b->functype)) {
        switch (CClass_IsCovariantResult(a->functype, a->qual, b->functype, b->qual, errorflag)) {
            case 0:
                if (errorflag) {
                    CError_Error(ERR_DERIVED_FUNCTION_DIFFERS_FROM_VIRTUAL_BASE);
                }
                return 0;
            case 1:
                return 1;
            case 2:
                return 2;
            default:
                CError_FATAL(225);
        }
    }

    return 1;
}

Boolean CClass_IsEmpty(TypeClass* tclass) {
    ClassList* base;

    if (tclass->ivars) {
        return FALSE;
    }

    for (base = tclass->bases; base; base = base->next) {
        if (CClass_IsEmpty(base->base) == FALSE) {
            return FALSE;
        }
    }

    return TRUE;
}

Boolean CClass_IsNonStaticMemberFunc(TypeFunc* tfunc) {
    return IS_TYPEFUNC_NONSTATIC_METHOD(tfunc) || (tfunc->flags & FUNC_PTM);
}

Object* CClass_DefaultConstructor(TypeClass* tclass) {
    NameSpaceObjectList* nsol;
    Object* object;

    for (nsol = CScope_FindName(tclass->nspace, constructor_name_node); nsol; nsol = nsol->next) {
        object = OBJECT(nsol->object);
        if (object->otype == OT_OBJECT && IS_TYPE_FUNC(object->type)) {
            if ((tclass->flags & CLASS_HAS_VBASES) && !tclass->sominfo) {
                if (TYPE_FUNC(object->type)->args->next &&
                    !TYPE_FUNC(object->type)->args->next->next) {
                    return object;
                }
            } else {
                if (!TYPE_FUNC(object->type)->args->next) {
                    return object;
                }
            }
        }
    }

    return NULL;
}

Object* CClass_DummyDefaultConstructor(TypeClass* tclass) {
    Object* ctor;
    FuncArg* args;
    NameSpaceObjectList* nsol;
    HashNameNode* name;
    TypeMemberFunc* tmethod;
    Object* object;
    ObjectList list;

    ctor = NULL;

    for (nsol = CScope_FindName(tclass->nspace, constructor_name_node); nsol; nsol = nsol->next) {
        object = OBJECT(nsol->object);
        if (object->otype == OT_OBJECT && IS_TYPE_FUNC(object->type)) {
            CError_ASSERT(305, args = TYPE_FUNC(object->type)->args);
            args = args->next;
            if ((tclass->flags & CLASS_HAS_VBASES) && !tclass->sominfo) {
                CError_ASSERT(309, args);
                args = args->next;
            }
            CError_ASSERT(312, args);

            if (args->dexpr) {
                if (ctor) {
                    list.next = NULL;
                    list.object = object;
                    CError_OverloadedFunctionError(ctor, &list);
                    break;
                }
                ctor = object;
            }
        }
    }

    if (!ctor) {
        CError_Error(ERR_CLASS_NO_DEFAULT_CONSTRUCTOR);
        return NULL;
    }

    name = GetHashNameNodeExport("__defctor");
    if ((nsol = CScope_FindName(tclass->nspace, name))) {
        CError_ASSERT(
            339, nsol->object->otype == OT_OBJECT && IS_TYPE_FUNC(OBJECT(nsol->object)->type)
        );
        return OBJECT(nsol->object);
    }

    tmethod = galloc(sizeof(TypeMemberFunc));
    memclrw(tmethod, sizeof(TypeMemberFunc));

    tmethod->type = TYPEFUNC;
    tmethod->functype = TYPE(&void_ptr);
    tmethod->flags = FUNC_METHOD;
    tmethod->theclass = tclass;
    CDecl_SetFuncFlags(TYPE_FUNC(tmethod), 0);

    if ((tclass->flags & CLASS_HAS_VBASES) && !tclass->sominfo) {
        CDecl_AddArgument(TYPE_FUNC(tmethod), TYPE(&stsignedshort));
    }

    CDecl_AddThisPointerArgument(TYPE_FUNC(tmethod), tclass);

    object = CParser_NewCompilerDefFunctionObject();
    object->type = TYPE(tmethod);
    object->qual = Q_INLINE | Q_MANGLE_NAME;
    object->nspace = tclass->nspace;
    object->name = name;

    CScope_AddObject(tclass->nspace, name, OBJ_BASE(object));
    CParser_RegisterDummyCtorFunction(object, ctor);
    return object;
}

ENode* CClass_DefaultConstructorCall(
    TypeClass* tclass,
    TypeClass* derived,
    ENode* objexpr,
    SInt32 varg,
    Boolean checkAccess,
    Boolean isVirtualBase,
    Boolean* errorflag
) {
    NameSpaceObjectList* nsol;
    Object* ctor;
    Object* object;
    FuncArg* args;
    ENodeList* argexprs;
    ENode* expr;
    BClassList* path;
    BClassList list1;
    ObjectList objlist;
    BClassList list2;
    short founddepth;
    Boolean isambigbase;

    *errorflag = FALSE;

    if (!(nsol = CScope_FindName(tclass->nspace, constructor_name_node))) {
        return NULL;
    }

    ctor = NULL;
    do {
        object = OBJECT(nsol->object);
        if (object->otype == OT_OBJECT && IS_TYPE_FUNC(object->type)) {
            CError_ASSERT(397, args = TYPE_FUNC(object->type)->args);

            args = args->next;

            if ((tclass->flags & CLASS_HAS_VBASES) && !tclass->sominfo) {
                CError_ASSERT(401, args);
                args = args->next;
            }

            if (!args || args->dexpr) {
                if (ctor) {
                    objlist.next = NULL;
                    objlist.object = object;
                    CError_OverloadedFunctionError(ctor, &objlist);
                    break;
                }
                ctor = object;
            }
        }
    } while ((nsol = nsol->next));

    if (!ctor) {
        *errorflag = TRUE;
        return NULL;
    }

    if (checkAccess) {
        if (derived) {
            if (isVirtualBase) {
                if ((path = CClass_GetBasePath(derived, tclass, &founddepth, &isambigbase))) {
                    list1 = *path;
                } else {
                    goto skipCheck;
                }
            } else {
                list1.next = &list2;
                list1.type = TYPE(derived);
                list2.next = NULL;
                list2.type = TYPE(tclass);
            }
        } else {
            list1.next = NULL;
            list1.type = TYPE(tclass);
        }
        CClass_CheckPathAccess(&list1, ctor, ctor->access);
    }

skipCheck:
    if ((tclass->flags & CLASS_HAS_VBASES) && !tclass->sominfo) {
        argexprs = lalloc(sizeof(ENodeList));
        argexprs->next = NULL;
        argexprs->node = intconstnode(TYPE(&stsignedshort), varg);
    } else {
        argexprs = NULL;
    }

    CError_ASSERT(471, IS_TYPE_POINTER_ONLY(objexpr->rtype));

    objexpr = makemonadicnode(objexpr, EINDIRECT);
    objexpr->rtype = TYPE(tclass);

    list1.next = NULL;
    list1.type = TYPE(tclass);

    expr = CExpr_GenericFuncCall(
        &list1, objexpr, FALSE, ctor, NULL, NULL, argexprs, FALSE, FALSE, FALSE
    );

    if (ENODE_IS2(expr, EFUNCCALL, EFUNCCALLP)) {
        expr->rtype = CDecl_NewPointerType(TYPE(tclass));
    }

    return expr;
}

Object* CClass_AssignmentOperator(TypeClass* tclass) {
    NameSpaceObjectList* nsol;
    Object* object;
    FuncArg* args;

    for (nsol = CScope_FindName(tclass->nspace, asop_name_node); nsol; nsol = nsol->next) {
        object = OBJECT(nsol->object);
        if (object->otype == OT_OBJECT &&
            IS_TYPE_FUNC(object->type) &&
            (args = TYPE_FUNC(object->type)->args) &&
            args->next &&
            !args->next->next) {
            if (IS_TYPE_CLASS(args->next->type) && TYPE_CLASS(args->next->type) == tclass) {
                return object;
            }

            if (IS_TYPE_REFERENCE(args->next->type) &&
                TPTR_TARGET(args->next->type) == TYPE(tclass)) {
                return object;
            }
        }
    }

    return NULL;
}

Object* CClass_CopyConstructor(TypeClass* tclass) {
    NameSpaceObjectList* nsol;
    Object* object;
    FuncArg* args;

    if (tclass->sominfo) {
        return NULL;
    }

    for (nsol = CScope_FindName(tclass->nspace, constructor_name_node); nsol; nsol = nsol->next) {
        object = OBJECT(nsol->object);
        if (object->otype == OT_OBJECT && IS_TYPE_FUNC(object->type)) {
            CError_ASSERT(532, args = TYPE_FUNC(object->type)->args);

            args = args->next;

            if (tclass->flags & CLASS_HAS_VBASES) {
                CError_ASSERT(536, args);
                args = args->next;
            }

            if (args &&
                args != &elipsis &&
                (!args->next || args->next->dexpr) &&
                IS_TYPE_REFERENCE(args->type) &&
                TPTR_TARGET(args->type) == TYPE(tclass)) {
                return object;
            }
        }
    }

    return NULL;
}

NameSpaceObjectList* CClass_MemberObject(TypeClass* tclass, HashNameNode* name) {
    NameSpaceObjectList* nsol;

    if ((nsol = CScope_FindName(tclass->nspace, name)) && nsol->object->otype == OT_OBJECT) {
        return nsol;
    }

    return NULL;
}

NameSpaceObjectList* CClass_Constructor(TypeClass* tclass) {
    NameSpaceObjectList* nsol;

    if ((nsol = CScope_FindName(tclass->nspace, constructor_name_node)) &&
        nsol->object->otype == OT_OBJECT &&
        IS_TYPE_FUNC(OBJECT(nsol->object)->type)) {
        return nsol;
    }

    return NULL;
}

Object* CClass_Destructor(TypeClass* tclass) {
    NameSpaceObjectList* nsol;

    for (nsol = CScope_FindName(tclass->nspace, destructor_name_node); nsol; nsol = nsol->next) {
        if (nsol->object->otype == OT_OBJECT && IS_TYPE_FUNC(OBJECT(nsol->object)->type)) {
            return OBJECT(nsol->object);
        }
    }

    return NULL;
}

Boolean CClass_IsConstructor(Object* obj) {
    return obj && IS_TYPE_FUNC(obj->type) && (TYPE_FUNC(obj->type)->flags & FUNC_IS_CTOR);
}

Boolean CClass_IsDestructor(Object* obj) {
    return obj && IS_TYPE_FUNC(obj->type) && (TYPE_FUNC(obj->type)->flags & FUNC_IS_DTOR);
}

Boolean CClass_IsPODClass(TypeClass* tclass) {
    NameSpaceObjectList* nsol;
    Object* object;
    ObjMemberVar* ivar;
    Type* type;

    if (tclass->vtable || tclass->bases || CClass_Destructor(tclass)) {
        return FALSE;
    }

    for (nsol = CClass_Constructor(tclass); nsol; nsol = nsol->next) {
        if (nsol->object->otype == OT_OBJECT &&
            IS_TYPE_FUNC(OBJECT(nsol->object)->type) &&
            !(TYPE_FUNC(OBJECT(nsol->object)->type)->flags & FUNC_AUTO_GENERATED)) {
            return FALSE;
        }
    }

    object = CClass_AssignmentOperator(tclass);
    if (object && !(TYPE_FUNC(object->type)->flags & FUNC_AUTO_GENERATED)) {
        return FALSE;
    }

    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
        if (ivar->access != ACCESSPUBLIC) {
            return FALSE;
        }

        type = ivar->type;
        while (IS_TYPE_ARRAY(type)) {
            type = TPTR_TARGET(type);
        }

        switch (type->type) {
            case TYPECLASS:
                if (!CClass_IsPODClass(TYPE_CLASS(type))) {
                    return FALSE;
                }
                break;
            case TYPEMEMBERPOINTER:
                return FALSE;
            case TYPEPOINTER:
                if (TPTR_QUAL(type) & Q_REFERENCE) {
                    return FALSE;
                }
                break;
            default:
                break;
        }
    }

    return TRUE;
}

Boolean CClass_IsTrivialCopyClass(TypeClass* tclass) {
    Object* object;
    ClassList* base;
    ObjMemberVar* ivar;
    Type* type;

    if (tclass->vtable || tclass->vbases || CClass_Destructor(tclass)) {
        return FALSE;
    }

    object = CClass_CopyConstructor(tclass);
    if (object &&
        IS_TYPE_FUNC(object->type) &&
        !(TYPE_FUNC(object->type)->flags & FUNC_AUTO_GENERATED)) {
        return FALSE;
    }

    for (base = tclass->bases; base; base = base->next) {
        if (!CClass_IsTrivialCopyClass(base->base)) {
            return FALSE;
        }
    }

    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
        type = ivar->type;
        while (IS_TYPE_ARRAY(type)) {
            type = TPTR_TARGET(type);
        }

        if (IS_TYPE_CLASS(type) && !CClass_IsTrivialCopyClass(TYPE_CLASS(type))) {
            return FALSE;
        }
    }

    return TRUE;
}

Boolean CClass_IsTrivialCopyAssignClass(TypeClass* tclass) {
    Object* object;
    ObjMemberVar* ivar;
    Type* type;

    if (tclass->vtable || tclass->bases || CClass_Destructor(tclass)) {
        return FALSE;
    }

    object = CClass_AssignmentOperator(tclass);
    if (object && !(TYPE_FUNC(object->type)->flags & FUNC_AUTO_GENERATED)) {
        return FALSE;
    }

    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
        type = ivar->type;
        while (IS_TYPE_ARRAY(type)) {
            type = TPTR_TARGET(type);
        }

        if (IS_TYPE_REFERENCE(type)) {
            return FALSE;
        }
        if (CParser_IsConst(type, ivar->qual)) {
            return FALSE;
        }
        if (IS_TYPE_CLASS(type) && !CClass_IsTrivialCopyAssignClass(TYPE_CLASS(type))) {
            return FALSE;
        }
    }

    return TRUE;
}

Boolean CClass_ReferenceArgument(TypeClass* tclass) {
    if ((tclass->flags & (CLASS_COMPLETED | CLASS_IS_TEMPL_INST)) == CLASS_IS_TEMPL_INST) {
        CDecl_CompleteType(TYPE(tclass));
    }

    if (copts.simple_class_byval) {
        return !CClass_IsTrivialCopyClass(tclass);
    }

    return CClass_Destructor(tclass) != NULL || CClass_CopyConstructor(tclass) != NULL;
}

BClassList* CClass_GetPathCopy(BClassList* path, Boolean is_global) {
    BClassList* last;
    BClassList* copy;

    if (!path) {
        return NULL;
    }

    copy = last = is_global ? galloc(sizeof(BClassList)) : lalloc(sizeof(BClassList));
    last->next = path->next;
    last->type = path->type;

    while ((path = path->next)) {
        last->next = is_global ? galloc(sizeof(BClassList)) : lalloc(sizeof(BClassList));
        last = last->next;
        last->next = path->next;
        last->type = path->type;
    }

    return copy;
}

BClassList* CClass_AppendPath(BClassList* dest, BClassList* src) {
    BClassList* last;

    if (!(last = dest)) {
        return src;
    }

    while (last->next) {
        last = last->next;
    }

    while (src && src->type == last->type) {
        src = src->next;
    }

    last->next = src;
    return dest;
}

static AccessType CClass_GetPathAccess(BClassList* path) {
    AccessType result;
    ClassList* base;
    TypeClass* tclass;

    CError_ASSERT(930, path);

    result = ACCESSPUBLIC;

    while (1) {
        tclass = TYPE_CLASS(path->type);
        if (!(path = path->next)) {
            break;
        }

        for (base = tclass->bases; base; base = base->next) {
            if (base->base == TYPE_CLASS(path->type)) {
                break;
            }
        }

        CError_ASSERT(939, base);

        switch (base->access) {
            case ACCESSPUBLIC:
                break;
            case ACCESSPROTECTED:
                if (result == ACCESSPUBLIC) {
                    result = ACCESSPROTECTED;
                }
                break;
            case ACCESSPRIVATE:
                if (result == ACCESSPUBLIC || result == ACCESSPROTECTED) {
                    result = ACCESSPRIVATE;
                } else {
                    return ACCESSNONE;
                }
                break;
            case ACCESSNONE:
                return ACCESSNONE;
            default:
                CError_FATAL(960);
        }
    }

    return result;
}

Boolean CClass_IsMoreAccessiblePath(BClassList* path1, BClassList* path2) {
    switch (CClass_GetPathAccess(path2)) {
        case ACCESSPUBLIC:
            return FALSE;
        case ACCESSNONE:
            return TRUE;
        default:
            CError_FATAL(978);
        case ACCESSPROTECTED:
            switch (CClass_GetPathAccess(path1)) {
                case ACCESSPUBLIC:
                    return TRUE;
                case ACCESSPRIVATE:
                case ACCESSPROTECTED:
                case ACCESSNONE:
                    return FALSE;
                default:
                    CError_FATAL(987);
            }
        case ACCESSPRIVATE:
            switch (CClass_GetPathAccess(path1)) {
                case ACCESSPUBLIC:
                case ACCESSPROTECTED:
                    return TRUE;
                case ACCESSPRIVATE:
                case ACCESSNONE:
                    return FALSE;
                default:
                    CError_FATAL(997);
            }
    }

    return FALSE;
}

static BClassList* CClass_GetBasePathRec(TypeClass* a, TypeClass* b, SInt32 offset, short depth) {
    BClassList* checkpath;
    BClassList* bestpath;
    BClassList* newpath;
    ClassList* base;
    SInt32 newOffset;

    for (base = a->bases, bestpath = NULL; base; base = base->next) {
        if (base->is_virtual) {
            newOffset = CClass_VirtualBaseOffset(cclass_isbase_mostderived, base->base);
        } else {
            newOffset = offset + base->offset;
        }

        if (base->base == b) {
            if (cclass_founddepth && newOffset != cclass_isbase_foundoffset) {
                cclass_isambigbase = TRUE;
                return NULL;
            }

            cclass_isbase_foundoffset = newOffset;
            cclass_founddepth = depth;

            bestpath = lalloc(sizeof(BClassList));
            bestpath->next = NULL;
            bestpath->type = TYPE(b);
        } else if ((checkpath = CClass_GetBasePathRec(base->base, b, newOffset, depth + 1))) {
            newpath = lalloc(sizeof(BClassList));
            newpath->next = checkpath;
            newpath->type = TYPE(base->base);
            if (!bestpath || CClass_IsMoreAccessiblePath(newpath, bestpath)) {
                bestpath = newpath;
            }
        }
    }

    return bestpath;
}

BClassList* CClass_GetBasePath(
    TypeClass* a, TypeClass* b, short* founddepth, Boolean* isambigbase
) {
    BClassList* path;
    BClassList* result;

    if ((a->flags & (CLASS_COMPLETED | CLASS_IS_TEMPL_INST)) == CLASS_IS_TEMPL_INST) {
        CDecl_CompleteType(TYPE(a));
    }
    if ((b->flags & (CLASS_COMPLETED | CLASS_IS_TEMPL_INST)) == CLASS_IS_TEMPL_INST) {
        CDecl_CompleteType(TYPE(b));
    }

    cclass_founddepth = 0;
    cclass_isbase_mostderived = a;
    cclass_isbase_foundoffset = -1;
    cclass_isambigbase = FALSE;

    if ((path = CClass_GetBasePathRec(a, b, 0, 1))) {
        *founddepth = cclass_founddepth;
        *isambigbase = cclass_isambigbase;

        result = lalloc(sizeof(BClassList));
        result->next = path;
        result->type = TYPE(a);
        return result;
    } else {
        *isambigbase = FALSE;
        return NULL;
    }
}

Boolean CClass_IsBaseClass(
    TypeClass* a, TypeClass* b, short* founddepth, Boolean pathcheckflag, Boolean ambigerrorflag
) {
    BClassList* path;
    short depth;
    Boolean isambigbase;

    if ((path = CClass_GetBasePath(a, b, &depth, &isambigbase))) {
        if (isambigbase && ambigerrorflag) {
            CError_Error(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
        }

        if (pathcheckflag) {
            CClass_CheckPathAccess(path, NULL, ACCESSPUBLIC);
        }

        if (founddepth) {
            *founddepth = depth;
        }

        return TRUE;
    }

    return FALSE;
}

TypeClass* CClass_GetQualifiedClass(void) {
    DeclInfo di;

    memclrw(&di, sizeof(di));
    CParser_GetDeclSpecs(&di, FALSE);

    if (IS_TYPE_CLASS(di.thetype)) {
        return TYPE_CLASS(di.thetype);
    }

    return NULL;
}

ENode* CClass_AccessPathCast(BClassList* path, ENode* expr, Boolean reverse) {
    ClassList* base;
    SInt32 offset;

    while (path && path->next) {
        base = TYPE_CLASS(path->type)->bases;
        while (1) {
            if (base->base == TYPE_CLASS(path->next->type)) {
                break;
            }
            CError_ASSERT(1157, base = base->next);
        }

        if (base->is_virtual) {
            if (reverse) {
                CError_Error(ERR_ILLEGAL_TYPE_CAST);
                break;
            }

            if (!base->base->sominfo) {
                offset = base->offset;
                if (offset && !canadd(expr, offset)) {
                    expr = makediadicnode(expr, intconstnode(TYPE(&stunsignedlong), offset), EADD);
                    optimizecomm(expr);
                }

                expr->rtype = CDecl_NewPointerType(CDecl_NewPointerType(TYPE(base->base)));
                expr = makemonadicnode(expr, EINDIRECT);
                expr->rtype = expr->data.monadic->rtype;
            }
        } else {
            offset = base->offset;
            if (offset) {
                if (reverse) {
                    offset = -offset;
                }
                if (!canadd(expr, offset)) {
                    expr = makediadicnode(expr, intconstnode(TYPE(&stunsignedlong), offset), EADD);
                    optimizecomm(expr);
                }
            }
        }

        path = path->next;
    }

    return expr;
}

ENode* CClass_ClassPointerCast(
    ENode* expr,
    TypeClass* a,
    TypeClass* b,
    Boolean typconflag,
    Boolean ambigerrorflag,
    Boolean pathcheckflag
) {
    BClassList* path;
    Boolean reverse;
    short depth;
    Boolean isambigbase;

    reverse = FALSE;

    if (a != b) {
        if (!(path = CClass_GetBasePath(a, b, &depth, &isambigbase))) {
            CError_ASSERT(1216, typconflag);
            if ((path = CClass_GetBasePath(b, a, &depth, &isambigbase))) {
                reverse = TRUE;
                goto doCast;
            }
        } else {
doCast:
            if (isambigbase && ambigerrorflag) {
                CError_Error(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
            }
            if (pathcheckflag) {
                CClass_CheckPathAccess(path, NULL, ACCESSPUBLIC);
            }
            if (!(a->flags & CLASS_SINGLE_OBJECT) && !b->sominfo) {
                expr = CClass_AccessPathCast(path, expr, reverse);
            }
        }
    }

    if (typconflag && ENODE_IS(expr, EINDIRECT) && IS_TYPE_POINTER_ONLY(expr->rtype)) {
        expr = makemonadicnode(expr, ETYPCON);
    }

    return expr;
}

ENode* CClass_DirectBasePointerCast(ENode* expr, TypeClass* a, TypeClass* b) {
    ClassList* base;
    VClassList* vbase;
    BClassList* path;
    BClassList list1;
    BClassList list2;
    short depth;
    Boolean isambigbase;

    for (base = a->bases; base; base = base->next) {
        if (base->base == b) {
            list1.next = &list2;
            list1.type = TYPE(a);
            list2.next = NULL;
            list2.type = TYPE(b);
            return CClass_AccessPathCast(&list1, expr, FALSE);
        }
    }

    for (vbase = a->vbases; vbase; vbase = vbase->next) {
        if (vbase->base == b) {
            CError_ASSERT(1273, path = CClass_GetBasePath(a, b, &depth, &isambigbase));
            return CClass_AccessPathCast(path, expr, FALSE);
        }
    }

    CError_FATAL(1277);
    return expr;
}

SInt32 CClass_GetPathOffset(BClassList* path) {
    SInt32 offset;
    ClassList* base;

    offset = 0;

    while (path->next) {
        if (path->type != path->next->type) {
            for (base = TYPE_CLASS(path->type)->bases; base; base = base->next) {
                if (base->base == TYPE_CLASS(path->next->type)) {
                    break;
                }
            }

            if (!base) {
                CError_Error(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
                return offset;
            }
        }

        if (base->is_virtual) {
            return -1;
        }
        offset += base->offset;

        path = path->next;
    }

    return offset;
}

Boolean CClass_ClassDominates(TypeClass* tclass, TypeClass* baseclass) {
    ClassList* base;

    for (base = tclass->bases; base; base = base->next) {
        if (base->base == baseclass || CClass_ClassDominates(base->base, baseclass)) {
            return TRUE;
        }
    }

    return FALSE;
}

SInt32 CClass_VirtualBaseOffset(TypeClass* tclass, TypeClass* baseclass) {
    VClassList* vbase;

    for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
        if (vbase->base == baseclass) {
            return vbase->offset;
        }
    }

    CError_FATAL(1342);
    return 0;
}

SInt32 CClass_VirtualBaseVTableOffset(TypeClass* tclass, TypeClass* baseclass) {
    VClassList* vbase;

    for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
        if (vbase->base == baseclass) {
            return vbase->voffset;
        }
    }

    CError_FATAL(1359);
    return 0;
}

SInt32 CClass_GetMemberOffset(TypeClass* tclass, HashNameNode* name, ObjMemberVar** resultIvar) {
    ObjMemberVar* ivar;
    ClassList* base;
    SInt32 offset;
    SInt32 baseOffset;
    Boolean foundflag;

    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
        if (ivar->name == name) {
            if (resultIvar) {
                *resultIvar = ivar;
            }
            return ivar->offset;
        }
    }

    for (base = tclass->bases, foundflag = FALSE; base; base = base->next) {
        switch ((baseOffset = CClass_GetMemberOffset(base->base, name, resultIvar))) {
            case -3:
            case -2:
                return baseOffset;
            case -1:
                break;
            default:
                if (base->is_virtual) {
                    return -3;
                }
                if (foundflag) {
                    return -2;
                }
                foundflag = TRUE;
                offset = base->offset + baseOffset;
        }
    }

    return foundflag ? offset : -1;
}

Boolean CClass_OverridesBaseMember(TypeClass* tclass, HashNameNode* name, Object* obj) {
    NameSpaceObjectList* nsol;
    ClassList* base;
    Boolean result;
    Object* object;

    result = FALSE;
    for (base = tclass->bases; base; base = base->next) {
        if (base->base->vtable) {
            for (nsol = CScope_FindName(base->base->nspace, name); nsol; nsol = nsol->next) {
                object = OBJECT(nsol->object);
                if (object->otype == OT_OBJECT &&
                    object->datatype == DVFUNC &&
                    CClass_GetOverrideKind(TYPE_FUNC(object->type), TYPE_FUNC(obj->type), TRUE)) {
                    result = TRUE;
                }
            }

            if (CClass_OverridesBaseMember(base->base, name, obj)) {
                result = TRUE;
            }
        }
    }

    return result;
}

static OVClass* CClass_FindOVClass(OVClass* ovclass, TypeClass* tclass, SInt32 offset) {
    OVClassBase* ovbase;

    if (ovclass->tclass == tclass && ovclass->offset == offset) {
        return ovclass;
    }

    for (ovbase = ovclass->bases; ovbase; ovbase = ovbase->next) {
        if ((ovclass = CClass_FindOVClass(ovbase->ovclass, tclass, offset))) {
            return ovclass;
        }
    }

    return NULL;
}

static OVClass* CClass_BuildOVClassTree(
    OVClass* root, TypeClass* tclass, SInt32 offset, SInt32 voffset
) {
    ClassList* base;
    Object* object;
    OVClass* tree;
    OVFunc* ovfunc;
    OVClassBase* ovbase;
    SInt32 vboffset;
    SInt32 vbvoffset;
    CScopeObjectIterator iter;

    tree = lalloc(sizeof(OVClass));
    memclrw(tree, sizeof(OVClass));

    tree->tclass = tclass;
    tree->offset = offset;
    tree->voffset = voffset;
    if (!root) {
        root = tree;
    }

    CScope_InitObjectIterator(&iter, tclass->nspace);
    while (1) {
        if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
            break;
        }

        if (object->datatype == DVFUNC) {
            ovfunc = lalloc(sizeof(OVFunc));
            memclrw(ovfunc, sizeof(OVFunc));

            ovfunc->next = tree->vfuncs;
            ovfunc->obj = object;
            tree->vfuncs = ovfunc;
        }
    }

    for (base = tclass->bases; base; base = base->next) {
        if (base->base->vtable || base->base->sominfo) {
            ovbase = lalloc(sizeof(OVClassBase));
            memclrw(ovbase, sizeof(OVClassBase));

            if (base->is_virtual) {
                vboffset = CClass_VirtualBaseOffset(main_class, base->base);
                if (!(ovbase->ovclass = CClass_FindOVClass(root, base->base, vboffset))) {
                    vbvoffset = CClass_VirtualBaseVTableOffset(main_class, base->base);
                    ovbase->ovclass =
                        CClass_BuildOVClassTree(root, base->base, vboffset, vbvoffset);
                }
                ovbase->is_virtual = TRUE;
            } else {
                ovbase->ovclass = CClass_BuildOVClassTree(
                    root, base->base, offset + base->offset, voffset + base->voffset
                );
                ovbase->is_virtual = FALSE;
            }

            ovbase->next = tree->bases;
            tree->bases = ovbase;
        }
    }

    return tree;
}

static Boolean CClass_IsBaseOf(OVClass* a, OVClass* b) {
    OVClassBase* ovbase;

    for (ovbase = b->bases; ovbase; ovbase = ovbase->next) {
        if ((ovbase->ovclass->tclass == a->tclass && ovbase->ovclass->offset == a->offset) ||
            CClass_IsBaseOf(a, ovbase->ovclass)) {
            if (!cclass_ovbase && ovbase->is_virtual) {
                cclass_ovbase = ovbase->ovclass;
            }
            return TRUE;
        }
    }

    return FALSE;
}

static void CClass_FindOVFunc(OVClass* a, OVClass* b, OVFunc* func) {
    OVFunc* scan;
    OVClassBase* ovbase;
    UInt8 overrideKind;

    if (CClass_IsBaseOf(b, a)) {
        for (scan = a->vfuncs; scan; scan = scan->next) {
            if ((func->obj->name == scan->obj->name) &&
                (overrideKind = CClass_GetOverrideKind(
                     TYPE_FUNC(func->obj->type), TYPE_FUNC(scan->obj->type), FALSE
                 ))) {
                if (func->overriderclass) {
                    if (func->overriderclass->tclass == a->tclass ||
                        CClass_ClassDominates(func->overriderclass->tclass, a->tclass)) {
                        return;
                    }
                    if (!CClass_ClassDominates(a->tclass, func->overriderclass->tclass)) {
                        func->overrider2 = scan;
                        return;
                    }
                }

                if (a == cclass_root) {
                    TYPE_FUNC(scan->obj->type)->flags |= FUNC_OVERRIDE;
                    if (overrideKind == 2) {
                        TYPE_FUNC(scan->obj->type)->flags |= FUNC_COVARIANT;
                    }
                }

                func->overriderclass = a;
                func->overrider = scan;
                func->overrider2 = NULL;
                return;
            }
        }

        for (ovbase = a->bases; ovbase; ovbase = ovbase->next) {
            CClass_FindOVFunc(ovbase->ovclass, b, func);
        }
    }
}

static TypeList* CClass_GetCoVariantClassList(
    TypeList* list, TypeClass* tclass, Object* func, Boolean isTopLevel
) {
    Object* object;
    TypeList* scan;
    Type* type;
    ClassList* base;
    CScopeObjectIterator iter;

    if (!isTopLevel) {
        CScope_InitObjectIterator(&iter, tclass->nspace);
        while (1) {
            if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
                break;
            }

            if (object->name == func->name &&
                object->datatype == DVFUNC &&
                CClass_GetOverrideKind(TYPE_FUNC(object->type), TYPE_FUNC(func->type), FALSE) ==
                    2) {
                CError_ASSERT(
                    1686,
                    IS_TYPE_POINTER_ONLY(TYPE_FUNC(object->type)->functype) &&
                        IS_TYPE_CLASS(TPTR_TARGET(TYPE_FUNC(object->type)->functype))
                );

                type = TPTR_TARGET(TYPE_FUNC(object->type)->functype);
                for (scan = list; scan; scan = scan->next) {
                    if (scan->type == type) {
                        break;
                    }
                }

                if (!scan) {
                    scan = lalloc(sizeof(TypeList));
                    scan->type = type;
                    scan->next = list;
                    list = scan;
                }
            }
        }
    }

    for (base = tclass->bases; base; base = base->next) {
        if (base->base->vtable) {
            list = CClass_GetCoVariantClassList(list, base->base, func, FALSE);
        }
    }

    return list;
}

static TypeMemberFunc* CClass_GetCovariantType(TypeMemberFunc* tmethod, Type* type) {
    TypePointer* tptr;
    TypeMemberFunc* result;

    CError_ASSERT(1724, IS_TYPE_METHOD(tmethod) && IS_TYPE_POINTER_ONLY(tmethod->functype));

    tptr = galloc(sizeof(TypePointer));
    *tptr = *TYPE_POINTER(tmethod->functype);
    tptr->target = type;

    result = galloc(sizeof(TypeMemberFunc));
    *result = *tmethod;
    result->flags &= ~(FUNC_OVERRIDE | FUNC_COVARIANT);
    result->functype = TYPE(tptr);
    return result;
}

static Object* CClass_FindCovariantFunction(Object* func, Type* type) {
    NameSpaceObjectList* nsol;

    nsol = CScope_FindName(
        TYPE_METHOD(func->type)->theclass->nspace,
        CMangler_GetCovariantFunctionName(func, TYPE_CLASS(type))
    );

    CError_ASSERT(1754, nsol && !nsol->next && nsol->object->otype == OT_OBJECT);

    return OBJECT(nsol->object);
}

static ObjectList* CClass_DeclareCovariantFuncs(ObjectList* list, Object* func, TypeClass* tclass) {
    Object* newfunc;
    HashNameNode* name;
    TypeList* types;
    ObjectList* newlist;

    for (types = CClass_GetCoVariantClassList(NULL, tclass, func, TRUE); types;
         types = types->next) {
        name = CMangler_GetCovariantFunctionName(func, TYPE_CLASS(types->type));

        newfunc = galloc(sizeof(Object));
        memclrw(newfunc, sizeof(Object));

        newfunc->otype = OT_OBJECT;
        newfunc->datatype = DFUNC;
        newfunc->section = func->section;
        newfunc->nspace = func->nspace;
        newfunc->name = name;
        newfunc->type = TYPE(CClass_GetCovariantType(TYPE_METHOD(func->type), types->type));
        newfunc->qual = func->qual & ~Q_INLINE;
        newfunc->sclass = func->sclass;
        newfunc->u.func.linkname = name;

        newlist = lalloc(sizeof(ObjectList));
        newlist->object = newfunc;
        newlist->next = list;
        list = newlist;
    }

    return list;
}

void CClass_DefineCovariantFuncs(Object* method, CI_FuncData* ifuncdata) {
    TypeList* types;
    Object* covariantFunc;
    Statement* stmt;
    NameSpace* nspace;
    Type* tptr;
    Statement firstStmt;

    for (types =
             CClass_GetCoVariantClassList(NULL, TYPE_METHOD(method->type)->theclass, method, TRUE);
         types;
         types = types->next) {
        covariantFunc = CClass_FindCovariantFunction(method, types->type);
        tptr = CDecl_NewPointerType(types->type);

        nspace = CFunc_FuncGenSetup(&firstStmt, covariantFunc);
        CInline_UnpackIFunctionData(covariantFunc, ifuncdata, &firstStmt);

        for (stmt = &firstStmt; stmt; stmt = stmt->next) {
            if (stmt->type == ST_RETURN && stmt->expr) {
                stmt->expr =
                    CExpr_AssignmentPromotion(stmt->expr, tptr, ENODE_QUALS(stmt->expr), FALSE);
            }
        }

        CFunc_Gen(&firstStmt, covariantFunc, 0);
        cscope_current = nspace->parent;
    }
}

static void CClass_OverrideOVClassTree(OVClass* ovclass) {
    OVClassBase* ovbase;
    OVFunc* ovfunc;

    if (cclass_root != ovclass) {
        for (ovfunc = ovclass->vfuncs; ovfunc; ovfunc = ovfunc->next) {
            CClass_FindOVFunc(cclass_root, ovclass, ovfunc);
        }
    }

    for (ovbase = ovclass->bases; ovbase; ovbase = ovbase->next) {
        CClass_OverrideOVClassTree(ovbase->ovclass);
    }
}

static void CClass_AllocVTableRec(OVClass* ovclass) {
    OVFunc* ovfunc;
    Object* object;
    SInt32 vtbl_offset;
    SInt32 this_delta;
    SInt32 ctoroffset;
    OVClassBase* ovbase;
    OLinkList* link;

    if (ovclass->alloced_vtable) {
        return;
    }

    for (ovfunc = ovclass->vfuncs; ovfunc; ovfunc = ovfunc->next) {
        vtbl_offset = ovclass->voffset + TYPE_METHOD(ovfunc->obj->type)->vtbl_index +
            CABI_GetVTableOffset(ovclass->tclass);
        CError_ASSERT(1867, vtbl_offset < vtable_data_size);

        if (!(vtable_object_data[vtbl_offset])) {
            if (ovfunc->overrider) {
                object = ovfunc->overrider->obj;
                if ((TYPE_FUNC(object->type)->flags & FUNC_COVARIANT) &&
                    CClass_GetOverrideKind(
                        TYPE_FUNC(ovfunc->obj->type), TYPE_FUNC(object->type), FALSE
                    ) == 2) {
                    CError_ASSERT(
                        1887,
                        IS_TYPE_POINTER_ONLY(TYPE_FUNC(ovfunc->obj->type)->functype) &&
                            IS_TYPE_CLASS(TPTR_TARGET(TYPE_FUNC(ovfunc->obj->type)->functype))
                    );
                    object = CClass_FindCovariantFunction(
                        object, TPTR_TARGET(TYPE_FUNC(ovfunc->obj->type)->functype)
                    );
                }

                if ((this_delta = ovfunc->overriderclass->offset - ovclass->offset)) {
                    if (!(TYPE_FUNC(object->type)->flags & FUNC_PURE)) {
                        cclass_ovbase = NULL;
                        CError_ASSERT(1899, CClass_IsBaseOf(ovclass, ovfunc->overriderclass));

                        if (cclass_ovbase && (main_class->flags & CLASS_VBASE_CTOR_OFFSET)) {
                            ctoroffset = ovclass->offset - cclass_ovbase->offset;
                            ctoroffset =
                                CABI_GetCtorOffsetOffset(cclass_ovbase->tclass, NULL) - ctoroffset;
                            CError_ASSERT(1906, ctoroffset > 0);
                            object = CClass_ThunkObject(object, this_delta, 0, ctoroffset);
                        } else {
                            object = CClass_ThunkObject(object, this_delta, 0, -1);
                        }
                    }
                }
            } else {
                object = ovfunc->obj;
            }

            if (!(TYPE_FUNC(object->type)->flags & FUNC_PURE)) {
                link = lalloc(sizeof(OLinkList));
                link->next = vtable_object_links;
                link->obj = object;
                link->offset = vtbl_offset;
                link->addend = 0;
                vtable_object_links = link;
                vtable_object_data[vtbl_offset] = 1;
            }
        }
    }

    ovclass->alloced_vtable = TRUE;

    for (ovbase = ovclass->bases; ovbase; ovbase = ovbase->next) {
        CClass_AllocVTableRec(ovbase->ovclass);
    }
}

static Object* CClass_CheckClass(OVClass* ovclass, Boolean errorflag) {
    Object* object;
    Object* check;
    OVFunc* ovfunc;
    OVClassBase* ovbase;

    object = NULL;

    for (ovfunc = ovclass->vfuncs; ovfunc; ovfunc = ovfunc->next) {
        if (ovfunc->overrider2 && errorflag) {
            CError_Error(
                ERR_CLASS_MORE_THAN_ONE_FINAL_OVERRIDER,
                main_class,
                0,
                ovfunc->obj,
                ovfunc->overrider->obj,
                ovfunc->overrider2->obj
            );
        }

        if (!object) {
            check = ovfunc->overrider ? ovfunc->overrider->obj : ovfunc->obj;
            if (TYPE_FUNC(check->type)->flags & FUNC_PURE) {
                object = check;
            }
        }
    }

    for (ovbase = ovclass->bases; ovbase; ovbase = ovbase->next) {
        if (object) {
            CClass_CheckClass(ovbase->ovclass, errorflag);
        } else {
            object = CClass_CheckClass(ovbase->ovclass, errorflag);
        }
    }

    return object;
}

static void CClass_AllocVTable(TypeClass* tclass) {
    OVClass* ovclass;

    main_class = tclass;
    ovclass = CClass_BuildOVClassTree(NULL, tclass, 0, 0);
    cclass_root = ovclass;
    CClass_OverrideOVClassTree(ovclass);
    CClass_CheckClass(ovclass, TRUE);
    CClass_AllocVTableRec(ovclass);
}

static Object* CClass_CheckVirtuals(TypeClass* tclass) {
    OVClass* ovclass;

    main_class = tclass;
    ovclass = CClass_BuildOVClassTree(NULL, tclass, 0, 0);
    cclass_root = ovclass;
    CClass_OverrideOVClassTree(ovclass);
    return CClass_CheckClass(ovclass, FALSE);
}

static void CClass_CheckVirtualBaseOverrides(OVClass* a, OVClass* b, Boolean isVirtualBase) {
    OVFunc* ovfunc;
    VClassList* vbase;
    OVClassBase* ovbase;

    if (isVirtualBase) {
        for (ovfunc = b->vfuncs; ovfunc; ovfunc = ovfunc->next) {
            if (ovfunc->overrider) {
                cclass_ovbase = NULL;
                CError_ASSERT(2040, CClass_IsBaseOf(b, ovfunc->overriderclass));

                if (cclass_ovbase) {
                    for (vbase = a->tclass->vbases; vbase; vbase = vbase->next) {
                        if (vbase->base == cclass_ovbase->tclass) {
                            break;
                        }
                    }

                    CError_ASSERT(2047, vbase);
                    vbase->has_override = TRUE;
                }
            }
        }
    }

    for (ovbase = b->bases; ovbase; ovbase = ovbase->next) {
        CClass_CheckVirtualBaseOverrides(a, ovbase->ovclass, isVirtualBase || ovbase->is_virtual);
    }
}

static void CClass_CheckHideVirtual(OVClass* a, OVClass* b) {
    OVClassBase* ovbase;
    OVFunc* ovfunc;
    Object* foundObject;
    Boolean isAlias;
    Object* object;
    CScopeObjectIterator iter;

    if (a != b) {
        for (ovfunc = b->vfuncs; ovfunc; ovfunc = ovfunc->next) {
            if (ovfunc->overriderclass != a) {
                foundObject = NULL;
                isAlias = FALSE;
                CScope_InitObjectIterator(&iter, a->tclass->nspace);

                while (1) {
                    if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
                        break;
                    }

                    if (object->name == ovfunc->obj->name && IS_TYPE_FUNC(object->type)) {
                        if (object->datatype != DALIAS) {
                            if (!(TYPE_FUNC(object->type)->flags & FUNC_OVERRIDE)) {
                                foundObject = object;
                            }
                        } else {
                            if (object->u.alias.object == ovfunc->obj) {
                                isAlias = TRUE;
                                break;
                            }
                        }
                    }
                }

                if (foundObject && !isAlias) {
                    CError_Warning(ERR_HIDES_INHERITED_VIRTUAL_FUNCTION, foundObject, ovfunc->obj);
                }
            }
        }
    }

    for (ovbase = b->bases; ovbase; ovbase = ovbase->next) {
        CClass_CheckHideVirtual(a, ovbase->ovclass);
    }
}

void CClass_CheckOverrides(TypeClass* tclass) {
    OVClass* tree;
    Object* object;
    ObjectList* objlist;
    ClassList* base;
    VClassList* vbase;
    SInt32 i;
    CScopeObjectIterator iter;

    i = 0;
    for (base = tclass->bases; base; base = base->next) {
        base->offset = i;
        base->voffset = i;
        i++;
    }
    for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
        vbase->offset = i;
        vbase->voffset = i;
        i++;
    }

    main_class = tclass;
    tree = CClass_BuildOVClassTree(NULL, tclass, 0, 0);
    cclass_root = tree;

    CClass_OverrideOVClassTree(tree);
    if (CClass_CheckClass(tree, FALSE)) {
        tclass->flags |= CLASS_ABSTRACT;
    }

    if (copts.warn_hidevirtual) {
        CClass_CheckHideVirtual(tree, tree);
    }

    if (tclass->flags & CLASS_VBASE_CTOR_OFFSET) {
        CClass_CheckVirtualBaseOverrides(tree, tree, FALSE);
    }

    objlist = NULL;
    CScope_InitObjectIterator(&iter, tclass->nspace);
    while (1) {
        if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
            break;
        }

        if (object->datatype == DVFUNC) {
            CError_ASSERT(2175, IS_TYPE_FUNC(object->type));
            if (TYPE_FUNC(object->type)->flags & FUNC_COVARIANT) {
                objlist = CClass_DeclareCovariantFuncs(objlist, object, tclass);
            }
        }
    }

    while (objlist) {
        CScope_AddObject(tclass->nspace, objlist->object->name, OBJ_BASE(objlist->object));
        objlist = objlist->next;
    }

    for (base = tclass->bases; base; base = base->next) {
        base->offset = 0;
        base->voffset = 0;
    }
    for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
        vbase->offset = 0;
        vbase->voffset = 0;
    }
}

static void CClass_FindDominator(
    TypeClass* tclass1,
    SInt32 offset1,
    Object* object1,
    TypeClass* tclass2,
    SInt32 offset2,
    TypeClass* tclass3
) {
    Object* object;
    ClassList* base;
    CScopeObjectIterator iter;

    CScope_InitObjectIterator(&iter, tclass1->nspace);
    while (1) {
        if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
            break;
        }

        if (object->name == cclass_dominator_vobject->name &&
            object->datatype == DVFUNC &&
            CClass_GetOverrideKind(
                TYPE_FUNC(cclass_dominator_vobject->type), TYPE_FUNC(object->type), FALSE
            )) {
            if (object == cclass_dominator_vobject && offset1 == cclass_dominator_voffset) {
                if (object1) {
                    if (cclass_dominator_oobject && cclass_dominator_ooffset != offset2) {
                        if (CClass_ClassDominates(cclass_dominator_oclass, tclass2)) {
                            return;
                        }
                        if (!CClass_ClassDominates(tclass2, cclass_dominator_oclass)) {
                            cclass_dominator_eobject = object1;
                            cclass_vbase = tclass3;
                            return;
                        }
                    }

                    cclass_dominator_oobject = object1;
                    cclass_dominator_ooffset = offset2;
                    cclass_dominator_oclass = tclass2;
                    cclass_dominator_eobject = NULL;
                    cclass_vbase = tclass3;
                }
                return;
            } else {
                if (!object1) {
                    object1 = object;
                    tclass2 = tclass1;
                    offset2 = offset1;
                }
                break;
            }
        }
    }

    for (base = tclass1->bases; base; base = base->next) {
        if (base->base->vtable) {
            if (!base->is_virtual) {
                CClass_FindDominator(
                    base->base, offset1 + base->offset, object1, tclass2, offset2, tclass3
                );
            } else {
                SInt32 vboffset = CClass_VirtualBaseOffset(main_class, base->base);
                CClass_FindDominator(base->base, vboffset, object1, tclass2, offset2, base->base);
            }
        }
    }
}

static void CClass_ConstructVTable(
    TypeClass* tclass, SInt32 voffset, SInt32 offset, TypeClass* baseclass
) {
    Object* object;
    Object* thunkobject;
    SInt32 newoffset;
    SInt32 newvoffset;
    ClassList* base;
    SInt32 thunkoffset;
    OLinkList* olink;
    CScopeObjectIterator iter;

    CScope_InitObjectIterator(&iter, tclass->nspace);
    while (1) {
        if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
            break;
        }

        if (object->datatype == DVFUNC) {
            newoffset =
                voffset + TYPE_METHOD(object->type)->vtbl_index + CABI_GetVTableOffset(tclass);
            CError_ASSERT(2288, newoffset < vtable_data_size);

            if (!vtable_object_data[newoffset]) {
                cclass_dominator_vobject = object;
                cclass_dominator_voffset = offset;
                cclass_dominator_oobject = NULL;
                cclass_dominator_ooffset = offset;
                CClass_FindDominator(main_class, 0, NULL, NULL, 0, NULL);

                if (cclass_dominator_oobject) {
                    if (cclass_dominator_eobject) {
                        CError_Error(
                            ERR_CLASS_MORE_THAN_ONE_FINAL_OVERRIDER,
                            main_class,
                            0,
                            cclass_dominator_vobject,
                            cclass_dominator_oobject,
                            cclass_dominator_eobject
                        );
                    }
                } else {
                    cclass_dominator_oobject = object;
                    cclass_vbase = NULL;
                }

                vtable_object_data[newoffset] = 1;

                if (!(TYPE_FUNC(cclass_dominator_oobject->type)->flags & FUNC_PURE)) {
                    if (!check_pures) {
                        thunkobject = cclass_dominator_oobject;
                        if ((thunkoffset = cclass_dominator_ooffset - offset)) {
                            if (cclass_vbase && (main_class->flags & CLASS_VBASE_CTOR_OFFSET)) {
                                thunkobject = CClass_ThunkObject(
                                    cclass_dominator_oobject,
                                    thunkoffset,
                                    0,
                                    CABI_GetCtorOffsetOffset(cclass_vbase, tclass)
                                );
                            } else {
                                thunkobject = CClass_ThunkObject(
                                    cclass_dominator_oobject, thunkoffset, 0, -1
                                );
                            }
                        }

                        olink = lalloc(sizeof(OLinkList));
                        olink->next = vtable_object_links;
                        olink->obj = thunkobject;
                        olink->offset = newoffset;
                        olink->addend = 0;
                        vtable_object_links = olink;
                    }
                } else {
                    found_pure = cclass_dominator_oobject;
                }
            }
        }
    }

    for (base = tclass->bases; base; base = base->next) {
        if (base->base->vtable) {
            if (base->is_virtual) {
                newoffset = CClass_VirtualBaseOffset(main_class, base->base);
                newvoffset = CClass_VirtualBaseVTableOffset(main_class, base->base);
                CClass_ConstructVTable(base->base, newvoffset, newoffset, base->base);
            } else {
                CClass_ConstructVTable(
                    base->base, voffset + base->voffset, offset + base->offset, baseclass
                );
            }
        }
    }
}

void CClass_ClassDefaultFuncAction(TypeClass* tclass) {}

void CClass_ClassAction(TypeClass* tclass) {
    if (tclass->sominfo) {
        CSOM_GenerateClassStructures(tclass);
        return;
    }

    if (tclass->vtable) {
        vtable_data_size = tclass->vtable->size;
        vtable_object_data = lalloc(vtable_data_size);
        memclrw(vtable_object_data, vtable_data_size);

        main_class = tclass;
        vtable_object_links = NULL;
        found_pure = NULL;
        check_pures = FALSE;

        CClass_AllocVTable(tclass);

        memclrw(vtable_object_data, vtable_data_size);
        if (copts.RTTI && !(tclass->flags & (CLASS_SINGLE_OBJECT | CLASS_COM_OBJECT))) {
            vtable_object_links =
                CRTTI_ConstructVTableHeaders(tclass, vtable_object_data, vtable_object_links);
        }

        CError_ASSERT(2492, tclass->vtable->object->type->size == tclass->vtable->size);
        CInit_DeclareData(
            tclass->vtable->object, vtable_object_data, vtable_object_links, tclass->vtable->size
        );
    }
}

void CClass_MakeStaticActionClass(TypeClass* tclass) {
    if (tclass->vtable) {
        tclass->vtable->object->sclass = TK_STATIC;
        tclass->vtable->object->qual |= Q_IMPLICIT_WEAK;
        if (!(tclass->vtable->object->flags & OBJECT_REFERENCED)) {
            CParser_NewCallBackAction(tclass->vtable->object, tclass);
        } else if (cparamblkptr->precompile != 1) {
            CParser_NewClassAction(tclass);
        }
    }
}

Object* CClass_CheckPures(TypeClass* tclass) {
    return CClass_CheckVirtuals(tclass);
}

void CClass_MemberDef(Object* obj, TypeClass* tclass) {
    switch (tclass->action) {
        case CLASS_ACTION_NONE:
        case CLASS_ACTION_JAVA:
            return;
        case CLASS_ACTION_KEY_FUNCTION:
            if (IS_TYPE_FUNC(obj->type) && (TYPE_FUNC(obj->type)->flags & FUNC_KEY_FUNCTION)) {
                if (obj->qual & Q_INLINE) {
                    if (tclass->sominfo) {
                        CError_Error(ERR_SOM_CLASS_MUST_ONE_NON_INLINE);
                    }
                    tclass->action = CLASS_ACTION_NONE;
                    CClass_MakeStaticActionClass(tclass);
                    return;
                }
                if (cparamblkptr->precompile != 1) {
                    CParser_NewClassAction(tclass);
                }
            }
            return;
        case CLASS_ACTION_UNUSED:
            CError_FATAL(2682);
            return;
        default:
            CError_FATAL(2701);
            return;
    }
}

Object* CClass_ThisSelfObject(void) {
    ObjectList* list;

    if (cscope_currentfunc && cscope_currentclass) {
        if (cscope_currentclass->objcinfo) {
            for (list = arguments; list; list = list->next) {
                if (list->object->name == self_name_node) {
                    return list->object;
                }
            }
            CError_Error(ERR_ILLEGAL_USE_SELF);
        } else {
            if (cscope_is_member_func) {
                for (list = arguments; list; list = list->next) {
                    if (list->object->name == this_name_node) {
                        return list->object;
                    }
                }
            }
            CError_Error(ERR_ILLEGAL_USE_THIS);
        }
    }

    CError_Error(copts.cplusplus ? ERR_ILLEGAL_USE_THIS : ERR_ILLEGAL_USE_SELF);
    return NULL;
}

ENode* CClass_CreateThisSelfExpr(void) {
    Object* object;
    ENode* expr;

    if (!(object = CClass_ThisSelfObject())) {
        return NULL;
    }

    expr = create_objectrefnode(object);
    expr->rtype = CDecl_NewPointerType(TYPE(cscope_currentclass));
    expr = makemonadicnode(expr, EINDIRECT);
    expr->data.monadic->rtype = CDecl_NewPointerType(expr->rtype);

    return expr;
}

static AccessType CClass_BaseMemberAccess(BClassList* path, AccessType access) {
    ClassList* base;

    if (path->next) {
        access = CClass_BaseMemberAccess(path->next, access);
        switch (access) {
            case ACCESSPRIVATE:
            case ACCESSNONE:
                return ACCESSNONE;
            default:
                break;
        }

        for (base = TYPE_CLASS(path->type)->bases;; base = base->next) {
            if (!base) {
                return ACCESSNONE;
            }

            if (base->base == TYPE_CLASS(path->next->type)) {
                switch (base->access) {
                    case ACCESSNONE:
                        access = ACCESSNONE;
                        break;
                    case ACCESSPROTECTED:
                        if (access == ACCESSPUBLIC) {
                            access = ACCESSPROTECTED;
                        }
                        break;
                    case ACCESSPRIVATE:
                        if (access == ACCESSPRIVATE) {
                            access = ACCESSNONE;
                        } else {
                            access = ACCESSPRIVATE;
                        }
                        break;
                    case ACCESSPUBLIC:
                        break;
                }
                break;
            }
        }
    }

    return access;
}

static Boolean CClass_CanAccess(BClassList* path, AccessType access) {
    AccessType access2;
    BClassList* scan;
    BClassList* next;
    TypeClass* prevclass;
    TypeClass* tclass;
    ClassList* base;
    ClassFriend* cfriend;

    tclass = TYPE_CLASS(path->type);
    access2 = access;
    if ((scan = path->next)) {
        if (access2 != ACCESSPRIVATE) {
            prevclass = tclass;
            while (scan) {
                for (base = prevclass->bases; base; base = base->next) {
                    if (base->base == TYPE_CLASS(scan->type)) {
                        break;
                    }
                }

                if (!base) {
                    return FALSE;
                }

                switch (base->access) {
                    case ACCESSNONE:
                        access2 = ACCESSNONE;
                        break;
                    case ACCESSPROTECTED:
                        if (access2 == ACCESSPUBLIC) {
                            access2 = ACCESSPROTECTED;
                        }
                        break;
                    case ACCESSPRIVATE:
                        if (access2 == ACCESSPRIVATE) {
                            access2 = ACCESSNONE;
                        } else {
                            access2 = ACCESSPRIVATE;
                        }
                        break;
                    case ACCESSPUBLIC:
                        break;
                }

                prevclass = TYPE_CLASS(scan->type);
                scan = scan->next;
            }
        } else {
            access2 = ACCESSNONE;
        }
    }

    if (access2 == ACCESSPUBLIC) {
        return TRUE;
    }

    if (access2 != ACCESSNONE) {
        if (cscope_currentclass == tclass) {
            return TRUE;
        }
        if (cobjc_currentclass == tclass) {
            return TRUE;
        }

        for (cfriend = tclass->friends; cfriend; cfriend = cfriend->next) {
            if (cfriend->isclass) {
                if (cfriend->u.theclass == cscope_currentclass) {
                    return TRUE;
                }
            } else {
                if (cfriend->u.obj == cscope_currentfunc) {
                    return TRUE;
                }
            }
        }
    }

    for (scan = path; scan->next; scan = scan->next) {
        if (CClass_CanAccess(scan->next, access)) {
            if ((next = scan->next->next) || access != ACCESSPUBLIC) {
                scan->next->next = NULL;
                if (CClass_CanAccess(path, ACCESSPUBLIC)) {
                    scan->next->next = next;
                    return TRUE;
                }
                scan->next->next = next;
            }
        }
    }

    return FALSE;
}

void CClass_CheckPathAccess(BClassList* path, Object* obj, AccessType access) {
    if (!CClass_CanAccess(path, access)) {
        if (path != NULL && obj != NULL) {
            CError_Error(ERR_ILLEGAL_ACCESS_FROM_TO_PROTECTED_PRIVATE_MEMBER, path->type, 0, obj);
        } else {
            CError_Error(ERR_ILLEGAL_ACCESS_PROTECTED_PRIVATE_MEMBER);
        }
    }
}

static BClassList* CClass_PathCleanup(BClassList* path, TypeClass* tclass) {
    BClassList* first;
    ClassList* base;

    first = path;

    while (1) {
        if (!path->next) {
            if (!tclass) {
                return first;
            }
            return path->type == TYPE(tclass) ? first : NULL;
        }

        if (path->type != path->next->type) {
            for (base = TYPE_CLASS(path->type)->bases; base; base = base->next) {
                if (base->base == TYPE_CLASS(path->next->type)) {
                    break;
                }
            }

            if (base) {
                path = path->next;
            } else {
                first = path = path->next;
            }
        } else {
            path->next = path->next->next;
        }
    }
}

void CClass_CheckStaticAccess(BClassList* path, TypeClass* tclass, AccessType access) {
    ClassFriend* cfriend;

    if (path) {
        path = CClass_PathCleanup(path, tclass);
        if (path && path->next) {
            if (!CClass_CanAccess(path, access)) {
                CError_Error(ERR_ILLEGAL_ACCESS_PROTECTED_PRIVATE_MEMBER);
            }
            return;
        }
    }

    switch (access) {
        case ACCESSPUBLIC:
            return;
        case ACCESSPRIVATE:
        case ACCESSPROTECTED:
            if (tclass == cscope_currentclass) {
                return;
            }

            for (cfriend = tclass->friends; cfriend; cfriend = cfriend->next) {
                if (cfriend->isclass) {
                    if (cfriend->u.theclass == cscope_currentclass) {
                        return;
                    }
                } else {
                    if (cfriend->u.obj == cscope_currentfunc) {
                        return;
                    }
                }
            }
        case ACCESSNONE:
            CError_Warning(ERR_ILLEGAL_ACCESS_PROTECTED_PRIVATE_MEMBER);
            return;
        default:
            CError_FATAL(3013);
    }
}

void CClass_CheckObjectAccess(BClassList* path, Object* obj) {
    short depth;
    Boolean isambigbase;

    if (obj->nspace && obj->nspace->theclass) {
        if (!path && cscope_currentclass) {
            path = CClass_GetBasePath(
                cscope_currentclass, obj->nspace->theclass, &depth, &isambigbase
            );
        }

        CClass_CheckStaticAccess(path, obj->nspace->theclass, obj->access);
    }
}

void CClass_CheckEnumAccess(BClassList* path, ObjEnumConst* objec) {
    if (path) {
        if ((path = CClass_PathCleanup(path, NULL))) {
            if (!CClass_CanAccess(path, objec->access)) {
                CError_Error(ERR_ILLEGAL_ACCESS_PROTECTED_PRIVATE_MEMBER);
            }
            return;
        }
    }

    if (objec->access != ACCESSPUBLIC &&
        IS_TYPE_ENUM(objec->type) &&
        TYPE_ENUM(objec->type)->nspace &&
        TYPE_ENUM(objec->type)->nspace->theclass) {
        CClass_CheckStaticAccess(NULL, TYPE_ENUM(objec->type)->nspace->theclass, objec->access);
    }
}

static Type* CClass_PointerTypeCopy(Type* type) {
    TypePointer* copy;
    TypeMemberPointer* mcopy;

    switch (type->type) {
        case TYPEPOINTER:
        case TYPEARRAY:
            copy = galloc(sizeof(TypePointer));
            *copy = *TYPE_POINTER(type);
            copy->target = CClass_PointerTypeCopy(copy->target);
            return TYPE(copy);
        case TYPEMEMBERPOINTER:
            mcopy = galloc(sizeof(TypeMemberPointer));
            *mcopy = *TYPE_MEMBER_POINTER(type);
            return TYPE(mcopy);
        default:
            return type;
    }
}

Type* CClass_CombineClassAccessQualifiers(
    Type* type, UInt32 qual1, UInt32 qual2, UInt32* outflags
) {
    Type* inner;

    qual2 = qual2 & (Q_CONST | Q_VOLATILE);
    if (qual1 & Q_MUTABLE) {
        qual2 &= ~Q_CONST;
    }
    qual1 = qual1 & (Q_CONST | Q_VOLATILE);

    inner = type;
    while (IS_TYPE_ARRAY(inner)) {
        inner = TPTR_TARGET(inner);
    }

    switch (inner->type) {
        case TYPEMEMBERPOINTER:
        case TYPEPOINTER:
            if (qual2) {
                type = CClass_PointerTypeCopy(type);
                inner = type;
                while (IS_TYPE_ARRAY(inner)) {
                    inner = TPTR_TARGET(inner);
                }

                switch (inner->type) {
                    case TYPEPOINTER:
                        TPTR_QUAL(inner) |= qual2;
                        break;
                    case TYPEMEMBERPOINTER:
                        TYPE_MEMBER_POINTER(inner)->qual |= qual2;
                        break;
                    default:
                        CError_FATAL(3125);
                }
            }
            break;
        default:
            qual1 |= qual2;
    }

    *outflags = qual1;
    return type;
}

static void CClass_OptimizeBitFieldAccess(Type** ptype, SInt32* poffset) {
    TypeBitfield* bitfield;
    Type* innertype;
    TypeBitfield* newtype;
    short i;
    short lastBit;
    short firstBit;

    bitfield = TYPE_BITFIELD(*ptype);
    innertype = bitfield->bitfieldtype;
    if (bitfield->bitlength == 8) {
        switch (bitfield->offset) {
            case 0:
                i = 0;
                break;
            case 8:
                i = 1;
                break;
            case 16:
                i = 2;
                break;
            case 24:
                i = 3;
                break;
            case 32:
                i = 4;
                break;
            case 40:
                i = 5;
                break;
            case 48:
                i = 6;
                break;
            case 56:
                i = 7;
                break;
            default:
                i = -1;
        }
        if (i >= 0) {
            if (innertype->size != 1) {
                if (is_unsigned(bitfield->bitfieldtype)) {
                    *ptype = TYPE(&stunsignedchar);
                } else {
                    *ptype = TYPE(&stsignedchar);
                }
            } else {
                *ptype = innertype;
            }
            *poffset += i;
            return;
        }
    }
    if (bitfield->bitlength == 16) {
        switch (bitfield->offset) {
            case 0:
                i = 0;
                break;
            case 16:
                i = 2;
                break;
            case 32:
                i = 4;
                break;
            case 48:
                i = 6;
                break;
            default:
                i = -1;
        }
        if (i >= 0) {
            if (innertype->size != stsignedshort.size) {
                if (is_unsigned(innertype)) {
                    *ptype = TYPE(&stunsignedshort);
                } else {
                    *ptype = TYPE(&stsignedshort);
                }
            } else {
                *ptype = innertype;
            }
            *poffset += i;
            return;
        }
    }
    if (bitfield->bitlength == 32 && bitfield->offset == 0) {
        if (innertype->size != stsignedlong.size) {
            if (is_unsigned(innertype)) {
                *ptype = TYPE(&stunsignedlong);
            } else {
                *ptype = TYPE(&stsignedlong);
            }
        } else {
            *ptype = innertype;
        }
        return;
    }
    if (bitfield->size == stsignedchar.size) {
        return;
    }
    firstBit = bitfield->offset;
    lastBit = firstBit + bitfield->bitlength - 1;
    if (bitfield->bitlength < 8 && (firstBit & 0xFFF8) == (lastBit & 0xFFF8)) {
        newtype = galloc(sizeof(TypeBitfield));
        *newtype = *TYPE_BITFIELD(*ptype);
        *ptype = TYPE(newtype);
        i = 0;
        if (newtype->offset >= 8) {
            i = 1;
        }
        if (newtype->offset >= 16) {
            i = 2;
        }
        if (newtype->offset >= 24) {
            i = 3;
        }
        if (newtype->offset >= 32) {
            i = 4;
        }
        if (newtype->offset >= 40) {
            i = 5;
        }
        if (newtype->offset >= 48) {
            i = 6;
        }
        if (newtype->offset >= 56) {
            i = 7;
        }
        *poffset += i;
        newtype->offset -= 8 * i;
        newtype->bitfieldtype =
            is_unsigned(innertype) ? TYPE(&stunsignedchar) : TYPE(&stsignedchar);
        newtype->size = newtype->bitfieldtype->size;
        return;
    }
    if (bitfield->size == stsignedshort.size) {
        return;
    }
    if (bitfield->bitlength < 16 && (firstBit & 0xFFF0) == (lastBit & 0xFFF0)) {
        newtype = galloc(sizeof(TypeBitfield));
        *newtype = *TYPE_BITFIELD(*ptype);
        *ptype = TYPE(newtype);
        i = 0;
        if (newtype->offset >= 16) {
            i = stsignedshort.size;
        }
        if (newtype->offset >= 32) {
            i = 4;
        }
        if (newtype->offset >= 48) {
            i = 6;
        }
        *poffset += i;
        newtype->offset -= 8 * i;
        newtype->bitfieldtype =
            is_unsigned(innertype) ? TYPE(&stunsignedshort) : TYPE(&stsignedshort);
        newtype->size = newtype->bitfieldtype->size;
        return;
    }
}

ENode* CClass_AccessMember(ENode* classexpr, Type* type, UInt32 qual, SInt32 offset) {
    Type* innertype;
    UInt32 flags;

    innertype = NULL;

    if (IS_TYPE_CLASS(classexpr->rtype) &&
        (TYPE_CLASS(classexpr->rtype)->flags & CLASS_HANDLEOBJECT)) {
        classexpr = makemonadicnode(classexpr, EINDIRECT);
        classexpr->data.monadic->rtype = CDecl_NewPointerType(classexpr->rtype);
    }

    if (IS_TYPE_BITFIELD(type)) {
        innertype = TYPE_BITFIELD(type)->bitfieldtype;
        CClass_OptimizeBitFieldAccess(&type, &offset);
    }

    if (offset && !canadd(classexpr->data.monadic, offset)) {
        classexpr->data.monadic = makediadicnode(
            classexpr->data.monadic, intconstnode(TYPE(&stunsignedlong), offset), EADD
        );
        optimizecomm(classexpr->data.monadic);
    }

    if (innertype) {
        if (IS_TYPE_BITFIELD(type)) {
            classexpr->data.monadic = makemonadicnode(classexpr->data.monadic, EBITFIELD);
            classexpr->data.monadic->rtype = type;
            classexpr->rtype = TYPE_BITFIELD(type)->bitfieldtype;
        } else {
            classexpr->rtype = type;
        }
    } else {
        classexpr->rtype = type;
    }

    classexpr->rtype =
        CClass_CombineClassAccessQualifiers(classexpr->rtype, qual, ENODE_QUALS(classexpr), &flags);
    classexpr->flags = flags;
    return classexpr;
}
