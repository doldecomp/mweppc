#include "compiler/CRTTI.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CInit.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
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
#include <string.h>

typedef struct Offset {
    struct Offset* next;
    SInt32 offset;
} Offset;

static Offset* crtti_offsets;
static OLinkList* crtti_olinks;

static Object* CRTTI_ConstructTypeInfoObject(Type* type, UInt32 qual);

typedef struct RTTISubClassList {
    struct RTTISubClassList* next;
    TypeClass* base;
    SInt32 voffset;
} RTTISubClassList;

// NOTE: invented names: isprivate, isambiguous
typedef struct RTTIBaseList {
    struct RTTIBaseList* next;
    TypeClass* base;
    RTTISubClassList* subclasses;
    SInt32 voffset;
    short numsubclasses;
    // the base is reached only through a private base; it is reached at two offsets (an ambiguous
    // base)
    Boolean isprivate;
    Boolean isambiguous;
} RTTIBaseList;

// NOTE: invented names: tclass, tclassbase, list, voffset, isPrivate, ambiguous
static RTTIBaseList* CRTTI_CreateBaseList(
    TypeClass* tclass, TypeClass* tclassbase, RTTIBaseList* list, SInt32 voffset, Boolean isPrivate
) {
    RTTIBaseList* scan;
    ClassList* base;
    Boolean ambiguous;
    SInt32 newvoffset;

    if (tclass != tclassbase) {
        ambiguous = FALSE;

        for (scan = list; scan; scan = scan->next) {
            if (scan->base == tclassbase) {
                if (scan->voffset == voffset) {
                    if (!isPrivate) {
                        scan->isprivate = FALSE;
                    }
                    ambiguous = FALSE;
                } else {
                    scan->isambiguous = TRUE;
                    ambiguous = TRUE;
                }
                break;
            }
        }

        if (!scan || ambiguous) {
            scan = lalloc(sizeof(RTTIBaseList));
            memclrw(scan, sizeof(RTTIBaseList));

            scan->next = list;
            list = scan;
            scan->base = tclassbase;
            scan->voffset = voffset;
            scan->isprivate = isPrivate;
            scan->isambiguous = ambiguous;
        }
    }

    for (base = tclassbase->bases; base; base = base->next) {
        if (!base->is_virtual) {
            newvoffset = voffset + base->offset;
        } else {
            newvoffset = CClass_VirtualBaseOffset(tclass, base->base);
        }

        list = CRTTI_CreateBaseList(
            tclass, base->base, list, newvoffset, isPrivate || base->access == ACCESSPRIVATE
        );
    }

    return list;
}

// NOTE: invented names: tclass, baselist, tclassbase, voffset, unambiguous
static void CRTTI_CreateSubClassList(
    TypeClass* tclass,
    RTTIBaseList* baselist,
    TypeClass* tclassbase,
    SInt32 voffset,
    Boolean unambiguous
) {
    ClassList* base;
    RTTISubClassList* scan;
    SInt32 newvoffset;

    if (baselist->base != tclassbase) {
        for (scan = baselist->subclasses; scan; scan = scan->next) {
            if (scan->base == tclassbase && scan->voffset == voffset) {
                break;
            }
        }

        if (!scan) {
            scan = lalloc(sizeof(RTTISubClassList));
            scan->next = baselist->subclasses;
            baselist->subclasses = scan;

            scan->base = tclassbase;
            scan->voffset = voffset;
            baselist->numsubclasses++;
        }
    }

    for (base = tclassbase->bases; base; base = base->next) {
        if (base->access == ACCESSPUBLIC) {
            if (base->is_virtual) {
                if (!unambiguous) {
                    continue;
                }
                newvoffset = CClass_VirtualBaseOffset(tclass, base->base);
            } else {
                newvoffset = voffset + base->offset;
            }

            CRTTI_CreateSubClassList(tclass, baselist, base->base, newvoffset, unambiguous);
        }
    }
}

// NOTE: invented names: simpleCount, ambigCount
static Object* CRTTI_CreateBaseListObject(TypeClass* tclass) {
    SInt32* work2;
    OLinkList* refs;
    Object* object;
    SInt32* buf;
    SInt32 size;
    short simpleCount;
    short ambigCount;
    OLinkList* ref;
    RTTIBaseList* scan;
    short total;
    RTTISubClassList* subclass;
    SInt32* work;
    RTTIBaseList* baselist;

    baselist = CRTTI_CreateBaseList(tclass, tclass, NULL, 0, FALSE);
    if (!baselist) {
        return NULL;
    }
    simpleCount = 0;
    ambigCount = 0;
    total = 0;
    for (scan = baselist; scan; scan = scan->next) {
        if (scan->isambiguous || scan->isprivate) {
            CRTTI_CreateSubClassList(tclass, scan, scan->base, scan->voffset, !scan->isambiguous);
            if (scan->numsubclasses) {
                ambigCount++;
                total += scan->numsubclasses;
            }
        } else {
            simpleCount++;
        }
    }
    if (!simpleCount && !ambigCount) {
        return NULL;
    }
    size = (simpleCount + total) * 8 + ambigCount * 12 + 4;
    buf = lalloc(size);
    memclrw(buf, size);
    object = CParser_NewCompilerDefDataObject();
    object->name = CParser_GetUniqueName();
    object->type = CDecl_NewStructType(size, 4);
    object->qual = Q_CONST;
    object->sclass = TK_STATIC;
    refs = NULL;
    work = buf;
    if (simpleCount) {
        for (scan = baselist; scan; scan = scan->next) {
            if (!scan->isprivate && !scan->isambiguous) {
                ref = lalloc(sizeof(OLinkList));
                ref->next = refs;
                refs = ref;
                ref->obj = CRTTI_ConstructTypeInfoObject(TYPE(scan->base), 0);
                ref->offset = ((char*)work) - ((char*)buf);
                ref->addend = 0;
                work[1] = CTool_EndianConvertWord32(scan->voffset);
                work += 2;
            }
        }
    }
    if (ambigCount) {
        for (scan = baselist; scan; scan = scan->next) {
            if (scan->numsubclasses) {
                ref = lalloc(sizeof(OLinkList));
                ref->next = refs;
                refs = ref;
                ref->obj = CRTTI_ConstructTypeInfoObject(TYPE(scan->base), 0);
                ref->offset = ((char*)work) - ((char*)buf);
                ref->addend = 0;
                work[1] = CTool_EndianConvertWord32(scan->voffset | 0x80000000);
                work[2] = CTool_EndianConvertWord32(scan->numsubclasses);
                work2 = work + 3;
                for (subclass = scan->subclasses; subclass; subclass = subclass->next) {
                    ref = lalloc(sizeof(OLinkList));
                    ref->next = refs;
                    refs = ref;
                    ref->obj = CRTTI_ConstructTypeInfoObject(TYPE(subclass->base), 0);
                    ref->offset = ((char*)work2) - ((char*)buf);
                    ref->addend = 0;
                    work2[1] = CTool_EndianConvertWord32(subclass->voffset);
                    work2 += 2;
                }
                work = work2;
            }
        }
    }
    CInit_DeclareData(object, buf, refs, object->type->size);
    return object;
}

static Object* CRTTI_ConstructTypeInfoObject(Type* type, UInt32 qual) {
    Object* baselistobj;
    UInt32 data[2];
    HashNameNode* rttiname;
    OLinkList* refs;
    char* namestr;
    int namelen;
    Object* object;
    NameSpaceObjectList* list;
    TypePointer tptr_copy;
    TypeMemberPointer tmemptr_copy;
    Object* nameobj;

    switch (type->type) {
        case TYPEPOINTER:
            if (TPTR_QUAL(type) & (Q_CONST | Q_VOLATILE)) {
                tptr_copy = *TYPE_POINTER(type);
                tptr_copy.qual &= ~(Q_CONST | Q_VOLATILE);
                type = TYPE(&tptr_copy);
            }
            break;
        case TYPEMEMBERPOINTER:
            if (TYPE_MEMBER_POINTER(type)->qual & (Q_CONST | Q_VOLATILE)) {
                tmemptr_copy = *TYPE_MEMBER_POINTER(type);
                tmemptr_copy.qual &= ~(Q_CONST | Q_VOLATILE);
                type = TYPE(&tmemptr_copy);
            }
            break;
        default:
            qual = 0;
    }

    if (IS_TYPE_CLASS(type) && type->size == 0) {
        CDecl_CompleteType(type);
        if (!(TYPE_CLASS(type)->flags & CLASS_COMPLETED)) {
            CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, type, 0);
        }
    }

    rttiname = CMangler_RTTIObjectName(type, qual);
    list = CScope_FindName(cscope_root, rttiname);

    if (!list || (object = OBJECT(list->object))->otype != OT_OBJECT || object->datatype != DDATA) {
        namestr = CError_GetTypeName(type, qual, FALSE);
        namelen = strlen(namestr) + 1;
        nameobj = CInit_DeclareString(namestr, namelen, FALSE, FALSE);

        baselistobj = NULL;
        if (IS_TYPE_CLASS(type)) {
            baselistobj = CRTTI_CreateBaseListObject(TYPE_CLASS(type));
        }

        memclrw(data, sizeof(data));

        object = CParser_NewCompilerDefDataObject();
        object->name = rttiname;
        object->type = CDecl_NewStructType(sizeof(data), 4);
        object->qual = Q_CONST;
        object->sclass = TK_STATIC;

        refs = lalloc(sizeof(OLinkList));
        refs->next = NULL;
        refs->obj = nameobj;
        refs->offset = 0;
        refs->addend = 0;

        if (baselistobj) {
            refs->next = lalloc(sizeof(OLinkList));
            refs->next->next = NULL;
            refs->next->obj = baselistobj;
            refs->next->offset = 4;
            refs->next->addend = 0;
        }

        CScope_AddGlobalObject(object);
        CInit_DeclareData(object, data, refs, object->type->size);
    }

    return object;
}

static void CRTTI_ConstructVTableHeader(
    TypeClass* tclass1,
    TypeClass* tclass2,
    Object* typeinfoObj,
    char* data,
    SInt32 offset,
    SInt32 voffset
) {
    ClassList* base;
    Offset* o;
    OLinkList* olink;
    SInt32 stored; // NOTE: invented name
    SInt32 newoffset;
    SInt32 newvoffset;

    if (tclass2->vtable->owner == tclass2) {
        for (o = crtti_offsets; o; o = o->next) {
            if (o->offset == voffset) {
                break;
            }
        }

        if (!o) {
            o = lalloc(sizeof(Offset));
            o->next = crtti_offsets;
            o->offset = voffset;
            crtti_offsets = o;

            olink = lalloc(sizeof(OLinkList));
            olink->next = crtti_olinks;
            olink->obj = typeinfoObj;
            olink->offset = voffset;
            olink->addend = 0;
            crtti_olinks = olink;

            ((SInt32*)(data + voffset))[1] = CTool_EndianConvertWord32(-offset);
        } else {
            stored = ((SInt32*)(data + voffset))[1];
            CError_ASSERT(404, stored == (SInt32)CTool_EndianConvertWord32(-offset));
        }
    }

    for (base = tclass2->bases; base; base = base->next) {
        if (base->base->vtable) {
            if (base->is_virtual) {
                newoffset = CClass_VirtualBaseOffset(tclass1, base->base);
                newvoffset = CClass_VirtualBaseVTableOffset(tclass1, base->base);
            } else {
                newoffset = offset + base->offset;
                newvoffset = voffset + base->voffset;
            }

            CRTTI_ConstructVTableHeader(
                tclass1, base->base, typeinfoObj, data, newoffset, newvoffset
            );
        }
    }
}

OLinkList* CRTTI_ConstructVTableHeaders(TypeClass* tclass, void* data, OLinkList* links) {
    crtti_offsets = NULL;
    crtti_olinks = links;

    CRTTI_ConstructVTableHeader(
        tclass, tclass, CRTTI_ConstructTypeInfoObject(TYPE(tclass), 0), data, 0, 0
    );

    return crtti_olinks;
}

static Type* CRTTI_FindTypeInfoType(void) {
    NameSpace* nspace;
    NameSpaceObjectList* list;
    Type* type;

    if ((list = CScope_FindName(cscope_root, GetHashNameNodeExport("std"))) &&
        list->object->otype == OT_NAMESPACE) {
        nspace = OBJ_NAMESPACE(list->object)->nspace;
    } else {
        nspace = cscope_root;
    }

    type = CScope_GetLocalTagType(nspace, GetHashNameNodeExport("type_info"));
    if (type && IS_TYPE_CLASS(type) && type->size) {
        return type;
    }

    CError_Error(ERR_UNDEFINED_IDENTIFIER, "::std::type_info");
    return TYPE(&stchar);
}

ENode* CRTTI_ParseTypeID(void) {
    ENode* expr;
    Type* type;
    Type* typeinfoType;
    UInt32 qual;

    if (!copts.RTTI) {
        CError_Warning(ERR_RTTI_OPTION_DISABLED);
    }

    typeinfoType = CRTTI_FindTypeInfoType();

    if (lex() != '(') {
        CError_Error(ERR_LPAREN_EXPECTED);
        return nullnode();
    }

    tk = lex();
    if ((type = CParser_ParseTypeID(&qual, NULL))) {
        if (tk != ')') {
            CError_ErrorSkip(ERR_RPAREN_EXPECTED);
        } else {
            tk = lex();
        }

        if (IS_TYPE_REFERENCE(type)) {
            type = TPTR_TARGET(type);
        }
    } else {
        expr = s_expression();

        if (tk != ')') {
            CError_ErrorSkip(ERR_RPAREN_EXPECTED);
        } else {
            tk = lex();
        }

        type = expr->rtype;
        qual = ENODE_QUALS(expr);

        if (IS_TYPE_REFERENCE(type)) {
            type = TPTR_TARGET(type);
        }

        if (IS_TYPE_CLASS(type) && TYPE_CLASS(type)->vtable) {
            expr = funccallexpr(
                Rgtid_func,
                getnodeaddress(expr, FALSE),
                intconstnode(TYPE(&stsignedlong), TYPE_CLASS(type)->vtable->offset),
                NULL,
                NULL
            );
            expr->rtype = CDecl_NewPointerType(typeinfoType);

            expr = makemonadicnode(expr, EINDIRECT);
            expr->rtype = typeinfoType;
            expr->flags = ENODE_FLAG_CONST;
            return expr;
        }
    }

    expr = create_objectrefnode(CRTTI_ConstructTypeInfoObject(type, qual));
    expr = makemonadicnode(expr, EINDIRECT);
    expr->rtype = typeinfoType;
    expr->flags = ENODE_FLAG_CONST;
    return expr;
}

static void CRTTI_ConstCastQualCheck(UInt32 qual1, UInt32 qual2) {
    if (((qual1 & Q_CONST) && !(qual2 & Q_CONST)) ||
        ((qual1 & Q_VOLATILE) && !(qual2 & Q_VOLATILE))) {
        CError_Error(ERR_CONSTNESS_CASTED_AWAY);
    }
}

static void CRTTI_ConstCastCheck(Type* type1, UInt32 qual1, Type* type2, UInt32 qual2) {
    Boolean topLevel = TRUE; // NOTE: invented name
    if (IS_TYPE_REFERENCE(type2)) {
        type2 = TPTR_TARGET(type2);
        topLevel = FALSE;
    }

    while (1) {
        if (type1->type != type2->type) {
            break;
        }

        switch (type1->type) {
            case TYPEPOINTER:
                if (!topLevel) {
                    CRTTI_ConstCastQualCheck(TPTR_QUAL(type1), TPTR_QUAL(type2));
                }
                type1 = TPTR_TARGET(type1);
                type2 = TPTR_TARGET(type2);
                topLevel = FALSE;
                continue;

            case TYPEMEMBERPOINTER:
                if (!topLevel) {
                    CRTTI_ConstCastQualCheck(
                        TYPE_MEMBER_POINTER(type1)->qual, TYPE_MEMBER_POINTER(type2)->qual
                    );
                }
                type1 = TYPE_MEMBER_POINTER(type1)->ty1;
                type2 = TYPE_MEMBER_POINTER(type2)->ty1;
                topLevel = FALSE;
                continue;

            default:
                break;
        }

        break;
    }

    if (!topLevel && !IS_TYPE_FUNC(type1) && !IS_TYPE_FUNC(type2)) {
        CRTTI_ConstCastQualCheck(
            CParser_GetCVTypeQualifiers(type1, qual1), CParser_GetCVTypeQualifiers(type2, qual2)
        );
    }
}

static ENode* CRTTI_ParseCast(DeclInfo* di) {
    ENode* expr;

    if (lex() != '<') {
        CError_Error(ERR_LESS_EXPECTED);
        return NULL;
    }

    tk = lex();

    memclrw(di, sizeof(DeclInfo));
    CParser_GetDeclSpecs(di, FALSE);
    scandeclarator(di);

    if (di->name) {
        CError_Error(ERR_ILLEGAL_TYPE_CAST);
    }

    if (tk != '>') {
        CError_Error(ERR_GREATER_EXPECTED);
        return NULL;
    }

    if (lex() != '(') {
        CError_Error(ERR_LPAREN_EXPECTED);
        return NULL;
    }

    tk = lex();
    expr = s_expression();
    if (!IS_TYPE_REFERENCE(di->thetype)) {
        expr = pointer_generation(expr);
    }

    if (tk != ')') {
        CError_Error(ERR_RPAREN_EXPECTED);
        return NULL;
    }

    tk = lex();
    return expr;
}

static void CRTTI_IncompleteCheck(Type* type) {
    if (IS_TYPE_POINTER_ONLY(type)) {
        type = TPTR_TARGET(type);
    }

    if (IS_TYPE_CLASS(type) && type->size == 0) {
        CDecl_CompleteType(type);
        if (!(TYPE_CLASS(type)->flags & CLASS_COMPLETED)) {
            CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, type, 0);
        }
    }
}

// inline: the original has no out-of-line copy of it (its switch table would be in the unit's data)
static inline Boolean CRTTI_IsSameType(Type* a, Type* b) {
    while (1) {
        if (a->type != b->type) {
            return FALSE;
        }

        switch (a->type) {
            case TYPEVOID:
                return TRUE;
            case TYPEINT:
            case TYPEFLOAT:
            case TYPEENUM:
            case TYPESTRUCT:
                return a == b;
            case TYPEPOINTER:
                a = TPTR_TARGET(a);
                b = TPTR_TARGET(b);
                continue;
            default:
                return is_typesame(a, b);
        }
    }
}

static ENode* CRTTI_UniversalCast(ENode* expr, Type* type, UInt32 qual, UInt8 mode) {
    // type/qual are the target type
    Boolean isSimpleCast;
    Boolean needsTypcon;
    Boolean failed;

    if (ENODE_IS(expr, EOBJLIST)) {
        return CExpr_AssignmentPromotion(expr, type, qual & (Q_CONST | Q_VOLATILE), TRUE);
    }

    isSimpleCast = needsTypcon = failed = FALSE;

    switch (type->type) {
        case TYPEINT:
        case TYPEENUM:
            if (mode == 2 && IS_TYPE_POINTER_ONLY(expr->rtype) && type != TYPE(&stbool)) {
                failed = TRUE;
            }
            break;

        case TYPEPOINTER:
            if (TPTR_QUAL(type) & Q_REFERENCE) {
                if (!CRTTI_IsSameType(TPTR_TARGET(type), expr->rtype) &&
                    mode == 2 &&
                    !(IS_TYPE_CLASS(TPTR_TARGET(type)) &&
                      IS_TYPE_CLASS(expr->rtype) &&
                      (CClass_IsBaseClass(
                           TYPE_CLASS(TPTR_TARGET(type)), TYPE_CLASS(expr->rtype), NULL, FALSE, TRUE
                       ) ||
                       CClass_IsBaseClass(
                           TYPE_CLASS(expr->rtype), TYPE_CLASS(TPTR_TARGET(type)), NULL, FALSE, TRUE
                       )))) {
                    failed = TRUE;
                }
            } else if (IS_TYPE_POINTER_ONLY(expr->rtype)) {
                if (mode == 3 ||
                    CRTTI_IsSameType(type, expr->rtype) ||
                    IS_TYPE_VOID(TPTR_TARGET(type)) ||
                    IS_TYPE_VOID(TPTR_TARGET(expr->rtype))) {
                    isSimpleCast = needsTypcon = TRUE;
                } else if (mode == 2 &&
                           !(IS_TYPE_CLASS(TPTR_TARGET(type)) &&
                             IS_TYPE_CLASS(TPTR_TARGET(expr->rtype)) &&
                             (CClass_IsBaseClass(
                                  TYPE_CLASS(TPTR_TARGET(type)),
                                  TYPE_CLASS(TPTR_TARGET(expr->rtype)),
                                  NULL,
                                  FALSE,
                                  TRUE
                              ) ||
                              CClass_IsBaseClass(
                                  TYPE_CLASS(TPTR_TARGET(expr->rtype)),
                                  TYPE_CLASS(TPTR_TARGET(type)),
                                  NULL,
                                  FALSE,
                                  TRUE
                              )))) {
                    failed = TRUE;
                }
            } else {
                if (IS_TYPE_ENUM(expr->rtype)) {
                    expr->rtype = TYPE_ENUM(expr->rtype)->enumtype;
                }

                if (IS_TYPE_INT(expr->rtype)) {
                    if (ENODE_IS(expr, EINTCONST) && CInt64_IsZero(&expr->data.intval)) {
                        isSimpleCast = TRUE;
                        break;
                    }

                    if (mode != 2) {
                        break;
                    }
                }

                if (IS_TYPE_CLASS(expr->rtype)) {
                    if (mode == 2) {
                        break;
                    }
                }

                failed = TRUE;
            }
            break;

        default:
            break;
    }

    if (failed) {
        CError_Error(
            ERR_ILLEGAL_EXPLICIT_CONVERSION_FROM, expr->rtype, ENODE_QUALS(expr), type, qual
        );
        return expr;
    }

    if (isSimpleCast) {
        if (needsTypcon &&
            ENODE_IS(expr, EINDIRECT) &&
            (copts.pointercast_lvalue || !copts.ANSIstrict)) {
            expr = makemonadicnode(expr, ETYPCON);
        }

        expr->rtype = type;
        expr->flags = qual & ENODE_FLAG_QUALS;
        return expr;
    }

    if (!copts.old_argmatch) {
        return CExpr_Convert(expr, type, qual, TRUE, TRUE);
    }

    return do_typecast(expr, type, qual);
}

ENode* CRTTI_Parse_dynamic_cast(void) {
    Boolean isRef;
    ENode* expr;
    TypeClass* srcclass;
    TypeClass* destclass;
    ENode* typeinfo;
    TypePointer* tptr;
    DeclInfo di;

    expr = CRTTI_ParseCast(&di);
    if (!expr) {
        return nullnode();
    }

    if (!copts.RTTI) {
        CError_Warning(ERR_RTTI_OPTION_DISABLED);
    }

    CRTTI_ConstCastCheck(expr->rtype, expr->flags, di.thetype, di.qual);
    if (!IS_TYPE_POINTER_ONLY(di.thetype)) {
        CError_Error(ERR_ILLEGAL_TYPE_CAST);
        return expr;
    }

    tptr = TYPE_POINTER(di.thetype);
    isRef = (tptr->qual & Q_REFERENCE) != 0;

    if (IS_TYPE_CLASS(tptr->target)) {
        destclass = TYPE_CLASS(tptr->target);
        CDecl_CompleteType(TYPE(destclass));
        if (!(destclass->flags & CLASS_COMPLETED)) {
            CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, destclass, 0);
            return expr;
        }
    } else if (!IS_TYPE_VOID(tptr->target)) {
        CError_Error(ERR_ILLEGAL_TYPE_CAST);
        return expr;
    } else {
        destclass = NULL;
    }

    if (isRef) {
        if (!IS_TYPE_CLASS(expr->rtype)) {
            CError_Error(ERR_ILLEGAL_TYPE_CAST);
            return expr;
        }

        srcclass = TYPE_CLASS(expr->rtype);
        if (destclass) {
            if (srcclass == destclass ||
                CClass_IsBaseClass(srcclass, destclass, NULL, FALSE, TRUE)) {
                return do_typecast(expr, di.thetype, di.qual);
            }
        }

        expr = getnodeaddress(expr, TRUE);
    } else {
        if (!IS_TYPE_POINTER_ONLY(expr->rtype) || !IS_TYPE_CLASS(TPTR_TARGET(expr->rtype))) {
            CError_Error(ERR_ILLEGAL_TYPE_CAST);
            return expr;
        }

        srcclass = TYPE_CLASS(TPTR_TARGET(expr->rtype));
        if (destclass) {
            if (srcclass == destclass ||
                CClass_IsBaseClass(srcclass, destclass, NULL, FALSE, TRUE)) {
                return do_typecast(expr, di.thetype, di.qual);
            }
        }
    }

    if (!(srcclass->flags & CLASS_COMPLETED)) {
        CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, srcclass, 0);
        return expr;
    }

    if (!srcclass->vtable) {
        CError_Error(ERR_ILLEGAL_TYPE_CAST);
        return expr;
    }

    if (srcclass->sominfo) {
        CError_Error(ERR_ILLEGAL_TYPE_CAST);
        return expr;
    }

    if (destclass) {
        typeinfo = create_objectrefnode(CRTTI_ConstructTypeInfoObject(TYPE(destclass), 0));
        if (destclass->sominfo) {
            CError_Error(ERR_ILLEGAL_TYPE_CAST);
            return expr;
        }
    } else {
        typeinfo = nullnode();
    }

    expr = CExpr_FuncCallSix(
        Rdync_func,
        expr,
        intconstnode(TYPE(&stsignedlong), srcclass->vtable->offset),
        typeinfo,
        create_objectrefnode(CRTTI_ConstructTypeInfoObject(TYPE(srcclass), 0)),
        intconstnode(TYPE(&stsignedshort), isRef),
        NULL
    );

    if (isRef) {
        expr->rtype = CDecl_NewPointerType(TYPE(destclass));
        expr = makemonadicnode(expr, EINDIRECT);
        expr->rtype = TYPE(destclass);
    } else {
        expr->rtype = di.thetype;
    }
    expr->flags = di.qual & ENODE_FLAG_QUALS;
    return expr;
}

ENode* CRTTI_Parse_static_cast(void) {
    ENode* expr;
    DeclInfo di;

    expr = CRTTI_ParseCast(&di);
    if (!expr) {
        return nullnode();
    }

    CRTTI_ConstCastCheck(expr->rtype, expr->flags, di.thetype, di.qual);

    if (IS_TYPE_REFERENCE(di.thetype)) {
        if (IS_TYPE_CLASS(expr->rtype) && CExpr_CanImplicitlyConvert(expr, di.thetype, di.qual)) {
            expr = CExpr_Convert(expr, di.thetype, di.qual, FALSE, TRUE);
            CError_ASSERT(959, IS_TYPE_POINTER_ONLY(expr->rtype));

            expr = makemonadicnode(expr, EINDIRECT);
            expr->rtype = TPTR_TARGET(di.thetype);
            expr->flags = di.qual & ENODE_FLAG_QUALS;
            return expr;
        }
    } else {
        if (CExpr_CanImplicitlyConvert(expr, di.thetype, di.qual)) {
            return CExpr_Convert(expr, di.thetype, di.qual, TRUE, TRUE);
        }
    }

    if (!IS_TYPE_VOID(di.thetype) &&
        !(IS_TYPE_POINTER_ONLY(expr->rtype) && IS_TYPE_VOID(TPTR_TARGET(expr->rtype)))) {
        CRTTI_IncompleteCheck(di.thetype);
        CRTTI_IncompleteCheck(expr->rtype);
    }

    return CRTTI_UniversalCast(expr, di.thetype, di.qual, 2);
}

ENode* CRTTI_Parse_reinterpret_cast(void) {
    ENode* expr;
    Type* origtype;
    DeclInfo di;

    expr = CRTTI_ParseCast(&di);
    if (!expr) {
        return nullnode();
    }

    CRTTI_ConstCastCheck(expr->rtype, expr->flags, di.thetype, di.qual);

    if (IS_TYPE_REFERENCE(di.thetype)) {
        expr = CExpr_LValue(expr, FALSE, TRUE);
        if (!ENODE_IS(expr, EINDIRECT)) {
            return expr;
        }

        expr->data.monadic->rtype = CDecl_NewPointerType(expr->rtype);
        expr = expr->data.monadic;
        origtype = di.thetype;
        di.thetype = CDecl_NewPointerType(TPTR_TARGET(di.thetype));
    } else {
        origtype = NULL;
    }

    switch (di.thetype->type) {
        case TYPEINT:
            switch (expr->rtype->type) {
                case TYPEMEMBERPOINTER:
                case TYPEPOINTER:
                    expr = do_typecast(expr, di.thetype, di.qual);
                    break;
                default:
                    CError_Error(ERR_ILLEGAL_TYPE_CAST);
            }
            break;
        case TYPEPOINTER:
            switch (expr->rtype->type) {
                case TYPEINT:
                case TYPEENUM:
                    if (origtype) {
                        di.thetype = origtype;
                    }
                    expr = do_typecast(expr, di.thetype, di.qual);
                    break;
                case TYPEPOINTER:
                    expr = makemonadicnode(expr, ETYPCON);
                    expr->rtype = di.thetype;
                    expr->flags = di.qual & ENODE_FLAG_QUALS;
                    break;
                default:
                    CError_Error(ERR_ILLEGAL_TYPE_CAST);
            }
            break;
        case TYPEMEMBERPOINTER:
            if (IS_TYPE_MEMBERPOINTER(expr->rtype)) {
                if (IS_TYPE_FUNC(TYPE_MEMBER_POINTER(di.thetype)->ty1)) {
                    if (IS_TYPE_FUNC(TYPE_MEMBER_POINTER(expr->rtype)->ty1)) {
                        expr->rtype = di.thetype;
                        expr->flags = di.qual & ENODE_FLAG_QUALS;
                        break;
                    }
                } else {
                    if (!IS_TYPE_FUNC(TYPE_MEMBER_POINTER(expr->rtype)->ty1)) {
                        expr->rtype = di.thetype;
                        expr->flags = di.qual & ENODE_FLAG_QUALS;
                        break;
                    }
                }
            }
            expr = do_typecast(expr, di.thetype, di.qual);
            break;
        default:
            CError_Error(ERR_ILLEGAL_TYPE_CAST);
    }

    if (origtype && IS_TYPE_POINTER_ONLY(expr->rtype)) {
        expr = makemonadicnode(expr, EINDIRECT);
        expr->rtype = TPTR_TARGET(di.thetype);
    }

    return expr;
}

ENode* CRTTI_Parse_const_cast(void) {
    DeclInfo di;
    ENode* expr;

    if (!(expr = CRTTI_ParseCast(&di))) {
        return nullnode();
    }

    if (IS_TYPE_POINTER_ONLY(di.thetype)) {
        if (TPTR_QUAL(di.thetype) & Q_REFERENCE) {
            if (!iscpp_typeequal(TPTR_TARGET(di.thetype), expr->rtype)) {
                CError_Error(ERR_ILLEGAL_TYPE_CAST);
            }

            if (ENODE_IS(expr, EINDIRECT)) {
                expr->rtype = TPTR_TARGET(di.thetype);
                expr->flags = di.qual & ENODE_FLAG_QUALS;
            } else {
                CError_Error(ERR_NOT_LVALUE);
            }
        } else {
            if (!iscpp_typeequal(di.thetype, expr->rtype)) {
                CError_Error(ERR_ILLEGAL_TYPE_CAST);
            }

            expr = do_typecast(expr, di.thetype, di.qual);
        }
    } else if (IS_TYPE_MEMBERPOINTER(di.thetype)) {
        if (!iscpp_typeequal(di.thetype, expr->rtype)) {
            CError_Error(ERR_ILLEGAL_TYPE_CAST);
        }

        expr = do_typecast(expr, di.thetype, di.qual);
    } else {
        if (!is_typesame(di.thetype, expr->rtype)) {
            CError_Error(ERR_ILLEGAL_TYPE_CAST);
        } else {
            expr = do_typecast(expr, di.thetype, di.qual);
        }
    }

    return expr;
}
