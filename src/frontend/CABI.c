#include "compiler/CABI.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CException.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CInline.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CScope.h"
#include "compiler/CSOM.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/tokens.h"
#include "compiler/types.h"

typedef struct OffsetList {
    struct OffsetList* next;
    SInt32 offset;
} OffsetList;

static OffsetList* trans_vtboffsets;
static BClassList cabi_pathroot;
static BClassList* cabi_pathcur;
static TypeClass* cabi_loop_class;
static Boolean cabi_loop_construct;

short CABI_GetStructResultArgumentIndex(TypeFunc* tfunc) {
    return 0;
}

Type* CABI_GetSizeTType(void) {
    return TYPE(&stunsignedlong);
}

Type* CABI_GetPtrDiffTType(void) {
    return TYPE(&stsignedlong);
}

SInt16 CABI_StructSizeAlignValue(Type* type, SInt32 size) {
    int align = CMach_GetTypeAlign(type);
    if (align <= 1) {
        return 0;
    } else {
        return (align - 1) & (align - ((size & (align - 1))));
    }
}

// NOTE: invented names: offset, length
void CABI_ReverseBitField(TypeBitfield* tbitfield) {
    int bits;
    UInt8 offset;
    UInt8 length;

    switch (tbitfield->bitfieldtype->size) {
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
            bits = 64;
            break;
        default:
            CError_FATAL(172);
    }

    length = tbitfield->bitlength;
    offset = tbitfield->offset;
    tbitfield->offset = (bits - offset) - length;
}

// NOTE: invented names: unused, tclass
static void CABI_AllocateZeroVTablePointer(void* unused, TypeClass* tclass) {
    ClassList* base;

    for (base = tclass->bases; base; base = base->next) {
        if (!base->is_virtual && base->base->vtable) {
            return;
        }
    }

    tclass->size += void_ptr.size;
}

static SInt32 CABI_GetBaseSize(TypeClass* tclass) {
    SInt32 size = tclass->size;

    if (copts.vbase_abi_v2 && tclass->vbases) {
        return tclass->vbases->offset;
    }

    return size;
}

static void CABI_AllocateBases(ClassLayout* layout, TypeClass* tclass) {
    Boolean prevEmpty; // NOTE: invented name
    TypeClass* baseclass;
    ClassList* base;
    VClassList* vbase;
    SInt32 size;

    prevEmpty = FALSE;
    size = tclass->size;

    for (base = tclass->bases; base; base = base->next) {
        if (!base->is_virtual) {
            baseclass = base->base;
            if (!(baseclass->flags & CLASS_EMPTY)) {
                base->offset = size + CMach_MemberAlignValue(TYPE(baseclass), size);
                if (copts.vbase_abi_v2) {
                    size = base->offset + CABI_GetBaseSize(baseclass);
                } else {
                    size = base->offset + baseclass->size;
                    for (vbase = baseclass->vbases; vbase; vbase = vbase->next) {
                        size -= vbase->base->size;
                    }
                }
                prevEmpty = FALSE;
            } else {
                if (prevEmpty) {
                    base->offset = ++size;
                } else {
                    base->offset = size;
                }
                prevEmpty = TRUE;
            }
        }
    }

    tclass->size = size;
}

static void CABI_AllocateVirtualBasePointers(ClassLayout* layout, TypeClass* tclass) {
    ClassList* base;
    SInt32 size;

    size = tclass->size;

    for (base = tclass->bases; base; base = base->next) {
        if (base->is_virtual) {
            base->offset = size + CMach_MemberAlignValue(TYPE(&void_ptr), size);
            size = base->offset + void_ptr.size;
        }
    }

    tclass->size = size;
}

static SInt32 CABI_GetMemberOffset(TypeClass* tclass, HashNameNode* name) {
    ObjMemberVar* ivar;

    if (!name) {
        return 0;
    }

    ivar = tclass->ivars;
    while (1) {
        if (ivar->name == name) {
            return ivar->offset;
        }

        if (!(ivar = ivar->next)) {
            CError_FATAL(362);
        }
    }
}

// NOTE: invented name: curSize
static void CABI_AllocateMembers(ClassLayout* layout, TypeClass* tclass) {
    ObjMemberVar* ivar;
    SInt32 initialSize;
    SInt32 maxSize;
    TypeClass* unionClass;
    SInt32 unionStart;
    Boolean inAnonUnion;
    Boolean removeNoNameIvars;

    removeNoNameIvars = FALSE;
    initialSize = maxSize = tclass->size;
    CMach_StructLayoutInitOffset(maxSize);

    unionClass = NULL;
    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
        if (!ivar->anonunion) {
            if (!(ivar->offset & 0x80000000)) {
                if (tclass->mode == CLASS_MODE_UNION) {
                    CMach_StructLayoutInitOffset(initialSize);
                }

                if (IS_TYPE_BITFIELD(ivar->type)) {
                    ivar->offset =
                        CMach_StructLayoutBitfield(TYPE_BITFIELD(ivar->type), ivar->qual);
                } else {
                    ivar->offset = CMach_StructLayoutGetOffset(ivar->type, ivar->qual);
                }

                if (tclass->mode == CLASS_MODE_UNION) {
                    SInt32 curSize = CMach_StructLayoutGetCurSize();
                    if (curSize > maxSize) {
                        maxSize = curSize;
                    }
                }

                unionClass = NULL;
            } else {
                CError_ASSERT(412, unionClass);
                ivar->offset = unionStart + CABI_GetMemberOffset(unionClass, ivar->name);
                if (!inAnonUnion) {
                    ivar->anonunion = TRUE;
                }
                inAnonUnion = FALSE;
            }

            if (ivar->name == no_name_node || ivar->name == NULL) {
                removeNoNameIvars = TRUE;
            }
        } else {
            CError_ASSERT(422, IS_TYPE_CLASS(ivar->type));

            if (tclass->mode == CLASS_MODE_UNION) {
                CMach_StructLayoutInitOffset(initialSize);
            }

            unionStart = CMach_StructLayoutGetOffset(ivar->type, ivar->qual);
            unionClass = TYPE_CLASS(ivar->type);
            inAnonUnion = TRUE;

            if (tclass->mode == CLASS_MODE_UNION) {
                SInt32 curSize = CMach_StructLayoutGetCurSize();
                if (curSize > maxSize) {
                    maxSize = curSize;
                }
            }

            removeNoNameIvars = TRUE;
        }

        if (layout->vtable_ivar == ivar) {
            tclass->vtable->offset = ivar->offset;
        }
    }

    if (removeNoNameIvars) {
        ObjMemberVar** ptr = &tclass->ivars;
        while (*ptr) {
            if ((*ptr)->name == NULL || (*ptr)->name == no_name_node) {
                *ptr = (*ptr)->next;
            } else {
                ptr = &(*ptr)->next;
            }
        }
    }

    if (tclass->mode == CLASS_MODE_UNION) {
        tclass->size = maxSize;
    } else {
        tclass->size = CMach_StructLayoutGetCurSize();
    }

    if (copts.reverse_bitfields) {
        for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
            if (IS_TYPE_BITFIELD(ivar->type)) {
                CABI_ReverseBitField(TYPE_BITFIELD(ivar->type));
            }
        }
    }
}

static void CABI_AllocateVirtualBases(ClassLayout* layout, TypeClass* tclass) {
    VClassList* vbase;
    SInt32 size;

    size = tclass->size;

    for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
        vbase->offset = size + CMach_MemberAlignValue(TYPE(vbase->base), size);
        size = vbase->offset + CABI_GetBaseSize(vbase->base);

        if (vbase->has_override) {
            size += CMach_MemberAlignValue(TYPE(&stunsignedlong), size) + 4;
        }
    }

    tclass->size = size;
}

static Boolean CABI_FindZeroDeltaVPtr(TypeClass* tclass) {
    ClassList* base;

    for (base = tclass->bases; base; base = base->next) {
        if (!base->is_virtual && base->base->vtable && !base->offset) {
            tclass->vtable->offset = base->base->vtable->offset;
            return TRUE;
        }
    }

    return FALSE;
}

static Object* CABI_FindZeroVirtualBaseMember(TypeClass* tclass, Object* obj) {
    NameSpaceObjectList* nsol;
    ClassList* base;
    Object* chk;

    for (base = tclass->bases; base; base = base->next) {
        if (!base->is_virtual && !base->offset && !base->voffset && base->base->vtable) {
            for (nsol = CScope_FindName(base->base->nspace, obj->name); nsol; nsol = nsol->next) {
                chk = OBJECT(nsol->object);
                if (chk->otype == OT_OBJECT &&
                    chk->datatype == DVFUNC &&
                    CClass_GetOverrideKind(TYPE_FUNC(chk->type), TYPE_FUNC(obj->type), FALSE) ==
                        1) {
                    return chk;
                }
            }

            if ((chk = CABI_FindZeroVirtualBaseMember(base->base, obj))) {
                return chk;
            }
        }
    }

    return NULL;
}

void CABI_AddVTable(TypeClass* tclass) {
    tclass->vtable = galloc(sizeof(VTable));
    memclrw(tclass->vtable, sizeof(VTable));
}

SInt32 CABI_GetVTableOffset(TypeClass* tclass) {
    return 0;
}

static SInt32 CABI_GetBaseVTableSize(TypeClass* tclass) {
    VClassList* vbase;
    SInt32 size;

    size = tclass->vtable->size;

    for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
        if (vbase->base->vtable) {
            size -= CABI_GetBaseVTableSize(vbase->base);
        }
    }

    return size;
}

static void CABI_ApplyClassFlags(Object* obj, UInt8 flags, Boolean unused) {
    if (flags & CLASS_EFLAGS_INTERNAL) {
        obj->flags |= OBJECT_INTERNAL;
    }
    if (flags & CLASS_EFLAGS_IMPORT) {
        obj->flags |= OBJECT_IMPORT;
    }
    if (flags & CLASS_EFLAGS_EXPORT) {
        obj->flags |= OBJECT_EXPORT;
    }
}

static void CABI_AllocateVTable(ClassLayout* layout, TypeClass* tclass) {
    SInt32 size;
    ObjBase* objbase;
    Object* obj;
    ObjMemberVar* ivar;
    ClassList* base;
    VClassList* vbase;
    int i;

    size = 0;

    if (!tclass->vtable) {
        CABI_AddVTable(tclass);
        layout->firstVirtualSlot = layout->lex_order_count - 1;
    }

    if (!CABI_FindZeroDeltaVPtr(tclass)) {
        ivar = galloc(sizeof(ObjMemberVar));
        memclrw(ivar, sizeof(ObjMemberVar));

        ivar->otype = OT_MEMBERVAR;
        ivar->access = ACCESSPUBLIC;
        ivar->name = vptr_name_node;
        ivar->type = TYPE(&void_ptr);
        layout->vtable_ivar = ivar;

        for (i = layout->firstVirtualSlot;; i--) {
            if (i < 0) {
                ivar->next = tclass->ivars;
                tclass->ivars = ivar;
                break;
            }

            CError_ASSERT(666, layout->objlist[i]);

            if (layout->objlist[i]->otype == OT_MEMBERVAR) {
                ivar->next = OBJ_MEMBER_VAR(layout->objlist[i])->next;
                OBJ_MEMBER_VAR(layout->objlist[i])->next = ivar;
                break;
            }
        }

        if (tclass->flags & (CLASS_SINGLE_OBJECT | CLASS_COM_OBJECT)) {
            size = void_ptr.size;
        } else {
            size = 8;
        }
    } else {
        layout->vtable_ivar = NULL;
    }

    for (base = tclass->bases; base; base = base->next) {
        if (base->base->vtable && !base->is_virtual) {
            base->voffset = size;
            if (copts.vbase_abi_v2) {
                size += CABI_GetBaseVTableSize(base->base);
            } else {
                size += base->base->vtable->size;
                for (vbase = base->base->vbases; vbase; vbase = vbase->next) {
                    if (vbase->base->vtable) {
                        size -= vbase->base->vtable->size;
                    }
                }
            }
        }
    }

    for (i = 0; i < layout->lex_order_count; i++) {
        CError_ASSERT(714, objbase = OBJ_BASE(layout->objlist[i]));

        if (objbase->otype == OT_OBJECT && OBJECT(objbase)->datatype == DVFUNC) {
            TypeMemberFunc* tmethod = TYPE_METHOD(OBJECT(objbase)->type);
            Object* baseobj = CABI_FindZeroVirtualBaseMember(tclass, OBJECT(objbase));

            if (baseobj) {
                tmethod->vtbl_index = TYPE_METHOD(baseobj->type)->vtbl_index;
            } else {
                tmethod->vtbl_index = size;
                size += 4;
            }
        }
    }

    for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
        if (vbase->base->vtable) {
            vbase->voffset = size;
            if (copts.vbase_abi_v2) {
                size += CABI_GetBaseVTableSize(vbase->base);
            } else {
                size += vbase->base->vtable->size;
            }
        }
    }

    obj = CParser_NewCompilerDefDataObject();
    CABI_ApplyClassFlags(obj, tclass->eflags, FALSE);

    obj->name = CMangler_VTableName(tclass);
    obj->type = CDecl_NewStructType(size, 4);
    obj->qual = Q_CONST;
    obj->nspace = tclass->nspace;
    switch (tclass->action) {
        case CLASS_ACTION_NONE:
            obj->sclass = TK_STATIC;
            obj->qual |= Q_IMPLICIT_WEAK;
            break;
    }

    CParser_UpdateObject(obj, NULL);

    tclass->vtable->object = obj;
    tclass->vtable->owner = tclass;
    tclass->vtable->size = size;
}

void CABI_LayoutClass(ClassLayout* layout, TypeClass* tclass) {
    char saveAlignMode = copts.structalignment;

    tclass->size = 0;
    if (!tclass->sominfo) {
        if (tclass->bases) {
            CABI_AllocateBases(layout, tclass);
        }
        if (tclass->flags & CLASS_HAS_VBASES) {
            CABI_AllocateVirtualBasePointers(layout, tclass);
        }
        if (layout->has_vtable) {
            CABI_AllocateVTable(layout, tclass);
        }
        CABI_AllocateMembers(layout, tclass);
        if (tclass->flags & CLASS_HAS_VBASES) {
            CABI_AllocateVirtualBases(layout, tclass);
        }
    } else {
        copts.structalignment = AlignMode2_PPC;
        CABI_AllocateMembers(layout, tclass);
    }

    tclass->align = CMach_GetClassAlign(tclass);
    if (tclass->size == 0) {
        tclass->size = 1;
        tclass->flags |= CLASS_EMPTY;
    } else {
        tclass->size += CABI_StructSizeAlignValue(TYPE(tclass), tclass->size);
    }

    tclass->flags |= CLASS_COMPLETED;

    copts.structalignment = saveAlignMode;
}

void CABI_MakeDefaultArgConstructor(TypeClass* tclass, Object* func) {
    DefArgCtorInfo* info;
    Boolean saveDebugInfo;
    ENodeList* argexprs;
    FuncArg* args;
    CScopeSave savedScope;
    Statement firstStmt;
    Statement returnStmt;

    CError_FATAL(860);

    if (anyerrors || func->access == ACCESSNONE) {
        return;
    }

    CError_ASSERT(866, info = func->u.func.defargdata);

    CABI_ApplyClassFlags(func, tclass->eflags, FALSE);

    CScope_SetFunctionScope(func, &savedScope);

    CFunc_FuncGenSetup(&firstStmt, func);

    saveDebugInfo = copts.filesyminfo;
    copts.filesyminfo = FALSE;

    CFunc_SetupNewFuncArgs(func, TYPE_FUNC(func->type)->args);

    if (tclass->flags & CLASS_HAS_VBASES) {
        arguments->next->object->name = no_name_node;
    }

    firstStmt.next = &returnStmt;

    memclrw(&returnStmt, sizeof(Statement));
    returnStmt.type = ST_RETURN;
    returnStmt.expr = lalloc(sizeof(ENode));
    returnStmt.expr->type = EFUNCCALL;
    returnStmt.expr->cost = 200;
    returnStmt.expr->flags = 0;
    returnStmt.expr->rtype = TYPE(&void_ptr);

    returnStmt.expr->data.funccall.funcref = CExpr_MakeObjRefNode(info->default_func, FALSE);
    returnStmt.expr->data.funccall.functype = TYPE_FUNC(info->default_func->type);
    args = TYPE_FUNC(info->default_func->type)->args;
    returnStmt.expr->data.funccall.args = lalloc(sizeof(ENodeList));
    argexprs = returnStmt.expr->data.funccall.args;

    argexprs->node = create_objectnode(arguments->object);

    if (tclass->flags & CLASS_HAS_VBASES) {
        args = args->next;
        argexprs->next = lalloc(sizeof(ENodeList));
        argexprs = argexprs->next;
        argexprs->node = create_objectnode(arguments->next->object);
    }

    args = args->next;
    argexprs->next = lalloc(sizeof(ENodeList));
    argexprs = argexprs->next;
    argexprs->node = CInline_CopyExpression(info->default_arg, CopyModeLocal);

    while ((args = args->next) && args->dexpr) {
        argexprs->next = lalloc(sizeof(ENodeList));
        argexprs = argexprs->next;
        argexprs->node = CInline_CopyExpression(args->dexpr, CopyModeLocal);
    }

    argexprs->next = NULL;

    CFunc_CodeCleanup(&firstStmt);
    CFunc_Gen(&firstStmt, func, 0);

    CScope_RestoreScope(&savedScope);
    copts.filesyminfo = saveDebugInfo;

    func->u.func.defargdata = NULL;
}

static Object* CABI_ThisArg(void) {
    CError_ASSERT(931, arguments && IS_TYPE_POINTER_ONLY(arguments->object->type));
    return arguments->object;
}

ENode* CABI_MakeThisExpr(TypeClass* tclass, SInt32 offset) {
    ENode* expr;

    if (tclass) {
        if (!tclass->sominfo) {
            expr = create_objectnode(CABI_ThisArg());
            if (tclass->flags & CLASS_HANDLEOBJECT) {
                expr = makemonadicnode(expr, EINDIRECT);
            }
        } else {
            expr = CSOM_SOMSelfObjectExpr(tclass);
        }
    } else {
        expr = create_objectnode(CABI_ThisArg());
        expr->rtype = TYPE(&void_ptr);
    }

    if (offset) {
        expr = makediadicnode(expr, intconstnode(TYPE(&stunsignedlong), offset), EADD);
    }

    return expr;
}

static Object* CABI_VArg(void) {
    CError_ASSERT(976, arguments && arguments->next && IS_TYPE_INT(arguments->next->object->type));
    return arguments->next->object;
}

static ENode* CABI_MakeVArgExpr(void) {
    return create_objectnode(CABI_VArg());
}

// NOTE: invented names: tclass, isCopyCtor
static ENode* CABI_MakeCopyConArgExpr(TypeClass* tclass, Boolean isCopyCtor) {
    ObjectList* args;

    CError_ASSERT(1000, args = arguments);
    CError_ASSERT(1001, args = args->next);
    if (isCopyCtor && (tclass->flags & CLASS_HAS_VBASES)) {
        CError_ASSERT(1002, args = args->next);
    }
    CError_ASSERT(1003, IS_TYPE_POINTER_ONLY(args->object->type));

    return create_objectnode(args->object);
}

static ENode* CABI_InitVBasePtr1(
    ENode* expr, TypeClass* tclass1, TypeClass* tclass2, TypeClass* tclass3, SInt32 offset
) {
    ClassList* base;
    SInt32 newOffset;
    OffsetList* list;

    for (base = tclass2->bases; base; base = base->next) {
        if (base->base == tclass3 && base->is_virtual) {
            newOffset = offset + base->offset;

            for (list = trans_vtboffsets; list; list = list->next) {
                if (newOffset == list->offset) {
                    break;
                }
            }

            if (!list) {
                list = lalloc(sizeof(OffsetList));
                list->offset = newOffset;
                list->next = trans_vtboffsets;
                trans_vtboffsets = list;

                expr = makediadicnode(
                    makemonadicnode(CABI_MakeThisExpr(NULL, newOffset), EINDIRECT), expr, EASS
                );
            }
        }

        if (!base->is_virtual) {
            newOffset = offset + base->offset;
        } else {
            newOffset = CClass_VirtualBaseOffset(tclass1, base->base);
        }

        expr = CABI_InitVBasePtr1(expr, tclass1, base->base, tclass3, newOffset);
    }

    return expr;
}

static Statement* CABI_InitVBasePtrs(Statement* stmt, TypeClass* tclass) {
    ENode* expr;
    VClassList* vbase;

    for (vbase = tclass->vbases, trans_vtboffsets = NULL; vbase; vbase = vbase->next) {
        expr = CABI_InitVBasePtr1(
            CABI_MakeThisExpr(NULL, vbase->offset), tclass, tclass, vbase->base, 0
        );
        stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
        stmt->expr = expr;
    }

    return stmt;
}

static OffsetList* CABI_GetVBasePath(TypeClass* tclass, TypeClass* baseclass) {
    ClassList* base;
    OffsetList* best;
    OffsetList* list;
    OffsetList* scan;
    short bestLength;
    short length;

    for (base = tclass->bases; base; base = base->next) {
        if (base->base == baseclass && base->is_virtual) {
            best = lalloc(sizeof(OffsetList));
            best->next = NULL;
            best->offset = base->offset;
            return best;
        }
    }

    best = NULL;

    for (base = tclass->bases; base; base = base->next) {
        if ((list = CABI_GetVBasePath(base->base, baseclass))) {
            for (scan = list->next, length = 1; scan; scan = scan->next) {
                length++;
            }

            if (base->is_virtual) {
                length++;
            }

            if (!best || length < bestLength) {
                if (base->is_virtual) {
                    best = lalloc(sizeof(OffsetList));
                    best->next = list;
                    best->offset = base->offset;
                } else {
                    best = list;
                    best->offset += base->offset;
                }
                bestLength = length;
            }
        }
    }

    return best;
}

static ENode* CABI_GetVBasePtr(TypeClass* tclass, TypeClass* baseclass) {
    OffsetList* path;
    ENode* expr;

    CError_ASSERT(1127, path = CABI_GetVBasePath(tclass, baseclass));

    expr = makemonadicnode(CABI_MakeThisExpr(NULL, path->offset), EINDIRECT);

    while ((path = path->next)) {
        if (path->offset) {
            expr = makediadicnode(expr, intconstnode(TYPE(&stunsignedlong), path->offset), EADD);
        }
        expr = makemonadicnode(expr, EINDIRECT);
    }

    return expr;
}

static SInt32 CABI_FindNVBase(TypeClass* tclass, TypeClass* baseclass, SInt32 offset) {
    ClassList* base;
    SInt32 baseOffset; // NOTE: invented name

    if (tclass == baseclass) {
        return offset;
    }

    for (base = tclass->bases; base; base = base->next) {
        if (!base->is_virtual &&
            (baseOffset = CABI_FindNVBase(base->base, baseclass, offset + base->offset)) >= 0) {
            return baseOffset;
        }
    }

    return -1;
}

SInt32 CABI_GetCtorOffsetOffset(TypeClass* tclass, TypeClass* baseclass) {
    SInt32 baseSize;
    SInt32 size;
    char saveAlignMode;

    size = CABI_GetBaseSize(tclass);
    if (baseclass) {
        baseSize = CABI_FindNVBase(tclass, baseclass, 0);
        CError_ASSERT(1178, baseSize >= 0);
        size -= baseSize;
    }

    saveAlignMode = copts.structalignment;
    if (tclass->eflags & CLASS_EFLAGS_STRUCT_ALIGN) {
        copts.structalignment = ((tclass->eflags & CLASS_EFLAGS_STRUCT_ALIGN) >> 4) - 1;
    }
    size += CMach_MemberAlignValue(TYPE(&stunsignedlong), size);
    copts.structalignment = saveAlignMode;

    return size;
}

static Statement* CABI_InitVBaseCtorOffsets(Statement* stmt, TypeClass* tclass) {
    VClassList* vbase;
    Object* tempObj;
    ENode* expr;
    ENode* vbaseptr;
    SInt32 vboffset;
    SInt32 ctorOffsetOffset;

    tempObj = NULL;

    for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
        if (vbase->has_override) {
            if (!tempObj) {
                tempObj = create_temp_object(TYPE(&void_ptr));
            }

            vboffset = CClass_VirtualBaseOffset(tclass, vbase->base);
            ctorOffsetOffset = CABI_GetCtorOffsetOffset(vbase->base, NULL);
            CError_ASSERT(1215, vbaseptr = CABI_GetVBasePtr(tclass, vbase->base));

            expr = makediadicnode(create_objectnode(tempObj), vbaseptr, EASS);
            stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
            stmt->expr = expr;

            expr = makediadicnode(
                create_objectnode(tempObj),
                intconstnode(TYPE(&stunsignedlong), ctorOffsetOffset),
                EADD
            );
            expr = makemonadicnode(expr, EINDIRECT);
            expr->rtype = TYPE(&stunsignedlong);
            expr = makediadicnode(
                expr,
                makediadicnode(CABI_MakeThisExpr(NULL, vboffset), create_objectnode(tempObj), ESUB),
                EASS
            );
            stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
            stmt->expr = expr;
        }
    }

    return stmt;
}

static Statement* CABI_InitVTablePtrs(
    Statement* stmt,
    Object* vtableObj,
    TypeClass* tclass,
    TypeClass* baseclass,
    SInt32 offset,
    SInt32 voffset
) {
    ClassList* base;
    BClassList* orig_pathcur;
    SInt32 newVOffset;
    OffsetList* offsetList;
    SInt32 vtblOffset;
    ENode* vtblExpr;
    SInt32 newOffset;
    ENode* pathExpr;
    BClassList path;

    if (baseclass->vtable->owner == baseclass) {
        vtblOffset = offset + baseclass->vtable->offset;
        for (offsetList = trans_vtboffsets; offsetList; offsetList = offsetList->next) {
            if (vtblOffset == offsetList->offset) {
                break;
            }
        }

        if (!offsetList) {
            offsetList = lalloc(sizeof(OffsetList));
            offsetList->offset = vtblOffset;
            offsetList->next = trans_vtboffsets;
            trans_vtboffsets = offsetList;

            vtblExpr = create_objectrefnode(vtableObj);
            vtblExpr->rtype = TYPE(&void_ptr);
            if (voffset + CABI_GetVTableOffset(baseclass)) {
                vtblExpr = makediadicnode(
                    vtblExpr,
                    intconstnode(TYPE(&stunsignedlong), voffset + CABI_GetVTableOffset(baseclass)),
                    EADD
                );
            }

            pathExpr = CClass_AccessPathCast(&cabi_pathroot, CABI_MakeThisExpr(tclass, 0), FALSE);
            if (baseclass->vtable->offset != 0 && !canadd(pathExpr, baseclass->vtable->offset)) {
                pathExpr = makediadicnode(
                    pathExpr, intconstnode(TYPE(&stunsignedlong), baseclass->vtable->offset), EADD
                );
                optimizecomm(pathExpr);
            }

            stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
            stmt->expr = makediadicnode(makemonadicnode(pathExpr, EINDIRECT), vtblExpr, EASS);
        }
    }

    for (base = baseclass->bases; base; base = base->next) {
        if (base->base->vtable) {
            orig_pathcur = cabi_pathcur;

            cabi_pathcur->next = &path;
            path.next = NULL;
            path.type = TYPE(base->base);
            cabi_pathcur = &path;

            if (base->is_virtual) {
                newOffset = CClass_VirtualBaseOffset(tclass, base->base);
                newVOffset = CClass_VirtualBaseVTableOffset(tclass, base->base);
            } else {
                newOffset = offset + base->offset;
                newVOffset = voffset + base->voffset;
            }

            stmt = CABI_InitVTablePtrs(stmt, vtableObj, tclass, base->base, newOffset, newVOffset);

            cabi_pathcur = orig_pathcur;
        }
    }

    return stmt;
}

static Boolean CABI_IsOperatorNew(Object* obj) {
    return obj->otype == OT_OBJECT &&
        IS_TYPE_FUNC(obj->type) &&
        TYPE_FUNC(obj->type)->args &&
        TYPE_FUNC(obj->type)->args->type == CABI_GetSizeTType() &&
        TYPE_FUNC(obj->type)->args->next == NULL;
}

Object* CABI_ConstructorCallsNew(TypeClass* tclass) {
    NameSpaceObjectList* nsol;
    NameResult pr;

    if (!tclass->sominfo && (tclass->flags & CLASS_HANDLEOBJECT)) {
        if (CScope_FindClassMemberObject(tclass, &pr, CMangler_OperatorName(TK_NEW))) {
            if (pr.object) {
                if (CABI_IsOperatorNew(OBJECT(pr.object))) {
                    return OBJECT(pr.object);
                }
            } else {
                for (nsol = pr.objects; nsol; nsol = nsol->next) {
                    if (CABI_IsOperatorNew(OBJECT(nsol->object))) {
                        return OBJECT(nsol->object);
                    }
                }
            }
        }

        return newh_func;
    }

    return NULL;
}

// NOTE: invented names: funcobj, dtorfunc
void CABI_TransConstructor(
    Object* obj,
    Statement* firstStmt,
    TypeClass* tclass,
    TransConstructorCallback callback,
    Boolean has_try
) {
    Statement* stmt;
    Object* funcobj;
    Object* dtorfunc;
    CLabel* label;
    ENode* expr;
    ClassList* base;
    VClassList* vbase;
    ObjMemberVar* ivar;
    Type* type;
    CtorChain* chain;
    Boolean errorflag;

    stmt = firstStmt;

    if ((funcobj = CABI_ConstructorCallsNew(tclass))) {
        label = newlabel();

        stmt = CFunc_InsertStatement(ST_IFGOTO, firstStmt);
        stmt->expr = CABI_MakeThisExpr(NULL, 0);
        stmt->label = label;

        expr = funccallexpr(
            funcobj, intconstnode(CABI_GetSizeTType(), tclass->size), NULL, NULL, NULL
        );
        expr = makediadicnode(CABI_MakeThisExpr(NULL, 0), expr, EASS);
        stmt = CFunc_InsertStatement(ST_IFGOTO, stmt);
        stmt->expr = expr;
        stmt->label = label;

        stmt = CFunc_InsertStatement(ST_RETURN, stmt);
        stmt->expr = NULL;

        stmt = CFunc_InsertStatement(ST_LABEL, stmt);
        stmt->label = label;
        label->stmt = stmt;
    }

    if (has_try) {
        for (stmt = firstStmt;; stmt = stmt->next) {
            CError_ASSERT(1408, stmt);
            if (stmt->type == ST_BEGINCATCH) {
                break;
            }
        }
    }

    if (!tclass->sominfo) {
        if (tclass->flags & CLASS_HAS_VBASES) {
            label = newlabel();

            stmt = CFunc_InsertStatement(ST_IFGOTO, stmt);
            stmt->expr =
                CExpr_New_EEQU_Node(CABI_MakeVArgExpr(), intconstnode(TYPE(&stsignedshort), 0));
            stmt->label = label;

            stmt = CABI_InitVBasePtrs(stmt, tclass);

            for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
                if (!callback) {
                    for (chain = ctor_chain; chain; chain = chain->next) {
                        if (chain->what == CtorChain_VBase && chain->u.vbase == vbase) {
                            break;
                        }
                    }

                    if (chain) {
                        stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
                        stmt->expr = chain->objexpr;
                    } else {
                        expr = CClass_DefaultConstructorCall(
                            vbase->base,
                            tclass,
                            CABI_MakeThisExpr(NULL, vbase->offset),
                            0,
                            TRUE,
                            TRUE,
                            &errorflag
                        );
                        if (expr) {
                            stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
                            stmt->expr = expr;
                        } else if (errorflag) {
                            CError_Error(
                                ERR_CANNOT_CONSTRUCT_BASE_CLASS, tclass, 0, vbase->base, 0
                            );
                        }
                    }
                } else {
                    stmt = callback(stmt, tclass, vbase->base, vbase->offset, TRUE);
                }

                if ((funcobj = CClass_Destructor(vbase->base))) {
                    CExcept_RegisterMember(
                        stmt, CABI_ThisArg(), vbase->offset, funcobj, CABI_VArg(), FALSE
                    );
                }
            }

            stmt = CFunc_InsertStatement(ST_LABEL, stmt);
            stmt->label = label;
            label->stmt = stmt;
        }

        for (base = tclass->bases; base; base = base->next) {
            if (base->is_virtual) {
                continue;
            }

            if (!callback) {
                for (chain = ctor_chain; chain; chain = chain->next) {
                    if (chain->what == CtorChain_Base && chain->u.base == base) {
                        break;
                    }
                }

                if (chain) {
                    stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
                    stmt->expr = chain->objexpr;
                } else {
                    expr = CClass_DefaultConstructorCall(
                        base->base,
                        tclass,
                        CABI_MakeThisExpr(NULL, base->offset),
                        0,
                        TRUE,
                        FALSE,
                        &errorflag
                    );
                    if (expr) {
                        stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
                        stmt->expr = expr;
                    } else if (errorflag) {
                        CError_Error(ERR_CANNOT_CONSTRUCT_BASE_CLASS, tclass, 0, base->base, 0);
                    }
                }
            } else {
                stmt = callback(stmt, tclass, base->base, base->offset, TRUE);
            }

            if ((funcobj = CClass_Destructor(base->base))) {
                CExcept_RegisterMember(stmt, CABI_ThisArg(), base->offset, funcobj, NULL, FALSE);
            }
        }

        if (tclass->vtable && tclass->vtable->object && tclass->vtable->owner == tclass) {
            cabi_pathroot.next = NULL;
            cabi_pathroot.type = TYPE(tclass);
            cabi_pathcur = &cabi_pathroot;
            trans_vtboffsets = NULL;

            stmt = CABI_InitVTablePtrs(stmt, tclass->vtable->object, tclass, tclass, 0, 0);
        }
    }

    if (!tclass->sominfo && (tclass->flags & CLASS_VBASE_CTOR_OFFSET)) {
        stmt = CABI_InitVBaseCtorOffsets(stmt, tclass);
    }

    if (!callback) {
        for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
            for (chain = ctor_chain; chain; chain = chain->next) {
                if (chain->what == CtorChain_MemberVar && chain->u.membervar == ivar) {
                    if (IS_TYPE_ARRAY(ivar->type)) {
                        chain = NULL;
                    }
                    break;
                }
            }

            if (chain) {
                stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
                stmt->expr = chain->objexpr;
                type = ivar->type;
                switch (type->type) {
                    case TYPEARRAY:
                        do {
                            type = TPTR_TARGET(type);
                        } while (IS_TYPE_ARRAY(type));
                        if (IS_TYPE_CLASS(type)) {
                            if ((funcobj = CClass_Destructor(TYPE_CLASS(type)))) {
                                CError_ASSERT(1560, type->size);
                                CExcept_RegisterMemberArray(
                                    stmt,
                                    CABI_ThisArg(),
                                    ivar->offset,
                                    funcobj,
                                    ivar->type->size / type->size,
                                    type->size
                                );
                            }
                        }
                        break;
                    case TYPECLASS:
                        if ((funcobj = CClass_Destructor(TYPE_CLASS(type)))) {
                            CExcept_RegisterMember(
                                stmt, CABI_ThisArg(), ivar->offset, funcobj, NULL, TRUE
                            );
                        }
                        break;
                    default:
                        break;
                }
            } else {
                type = ivar->type;
                switch (type->type) {
                    case TYPEARRAY:
                        do {
                            type = TPTR_TARGET(type);
                        } while (IS_TYPE_ARRAY(type));
                        if (IS_TYPE_CLASS(type) && CClass_Constructor(TYPE_CLASS(type))) {
                            if ((funcobj = CClass_DefaultConstructor(TYPE_CLASS(type))) ||
                                (funcobj = CClass_DummyDefaultConstructor(TYPE_CLASS(type)))) {
                                dtorfunc = CClass_Destructor(TYPE_CLASS(type));
                                if (dtorfunc) {
                                    dtorfunc =
                                        CABI_GetDestructorObject(dtorfunc, CABIDestroyComplete);
                                }

                                stmt = CInit_ConstructClassArray(
                                    stmt,
                                    TYPE_CLASS(type),
                                    funcobj,
                                    dtorfunc,
                                    CABI_MakeThisExpr(tclass, ivar->offset),
                                    ivar->type->size / type->size
                                );

                                if (dtorfunc) {
                                    CExcept_RegisterMemberArray(
                                        stmt,
                                        CABI_ThisArg(),
                                        ivar->offset,
                                        dtorfunc,
                                        ivar->type->size / type->size,
                                        type->size
                                    );
                                }
                            } else {
                                CError_Error(
                                    ERR_CANNOT_CONSTRUCT_DIRECT_MEMBER, tclass, 0, ivar->name->name
                                );
                            }
                        }
                        break;
                    case TYPECLASS:
                        expr = CClass_DefaultConstructorCall(
                            TYPE_CLASS(type),
                            NULL,
                            CABI_MakeThisExpr(tclass, ivar->offset),
                            1,
                            TRUE,
                            FALSE,
                            &errorflag
                        );
                        if (expr) {
                            stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
                            stmt->expr = expr;
                        } else if (errorflag) {
                            CError_Error(
                                ERR_CANNOT_CONSTRUCT_DIRECT_MEMBER, tclass, 0, ivar->name->name
                            );
                        }

                        if ((funcobj = CClass_Destructor(TYPE_CLASS(type)))) {
                            if (!expr) {
                                stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
                                stmt->expr = nullnode();
                            }
                            CExcept_RegisterMember(
                                stmt, CABI_ThisArg(), ivar->offset, funcobj, NULL, TRUE
                            );
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    } else {
        stmt = callback(stmt, tclass, NULL, 0, TRUE);
    }

    if (!tclass->sominfo) {
        for (stmt = firstStmt->next; stmt; stmt = stmt->next) {
            if (stmt->type == ST_RETURN) {
                CError_ASSERT(1661, !stmt->expr);
                stmt->expr = CABI_MakeThisExpr(NULL, 0);
            }
        }
    }
}

void CABI_MakeDefaultConstructor(TypeClass* tclass, Object* func) {
    Boolean saveDebugInfo;
    CScopeSave savedScope;
    Statement firstStmt;
    Statement returnStmt;

    if (anyerrors || func->access == ACCESSNONE) {
        return;
    }

    CABI_ApplyClassFlags(func, tclass->eflags, FALSE);

    CScope_SetFunctionScope(func, &savedScope);

    CFunc_FuncGenSetup(&firstStmt, func);

    saveDebugInfo = copts.filesyminfo;
    copts.filesyminfo = FALSE;

    CFunc_SetupNewFuncArgs(func, TYPE_FUNC(func->type)->args);

    ctor_chain = NULL;
    if (tclass->flags & CLASS_HAS_VBASES) {
        arguments->next->object->name = CParser_GetUniqueName();
    }

    firstStmt.next = &returnStmt;

    memclrw(&returnStmt, sizeof(Statement));
    returnStmt.type = ST_RETURN;

    CABI_TransConstructor(func, &firstStmt, tclass, NULL, FALSE);

    CFunc_CodeCleanup(&firstStmt);
    CFunc_Gen(&firstStmt, func, 0);

    CScope_RestoreScope(&savedScope);
    copts.filesyminfo = saveDebugInfo;
}

static ENode* CABI_AssignObject(TypeClass* tclass, ENode* expr1, ENode* expr2) {
    Object* assignfunc;
    FuncArg* arg;

    assignfunc = CClass_AssignmentOperator(tclass);
    if (!assignfunc) {
        expr1 = makemonadicnode(expr1, EINDIRECT);
        expr1->rtype = TYPE(tclass);
        return makediadicnode(expr1, expr2, EASS);
    }

    CError_ASSERT(
        1731,
        IS_TYPE_FUNC(assignfunc->type) &&
            (arg = TYPE_FUNC(assignfunc->type)->args) &&
            (arg = arg->next)
    );

    expr2 = argumentpromotion(expr2, arg->type, arg->qual, TRUE);
    return funccallexpr(assignfunc, expr1, expr2, NULL, NULL);
}

static Type* CABI_FindIntegralSizeType(SInt32 size) {
    if (stunsignedchar.size == size) {
        return TYPE(&stunsignedchar);
    }
    if (stunsignedshort.size == size) {
        return TYPE(&stunsignedshort);
    }
    if (stunsignedint.size == size) {
        return TYPE(&stunsignedint);
    }
    if (stunsignedlong.size == size) {
        return TYPE(&stunsignedlong);
    }
    if (stunsignedlonglong.size == size) {
        return TYPE(&stunsignedlonglong);
    }

    CError_FATAL(1756);
    return NULL;
}

// 2-byte packing for the struct and, left on to the end of the file, for the unit's statics, as in
// the original (MWCC lays out .bss with the packing in force at the end of the file)
#pragma pack(2)
typedef struct CopyRegion {
    struct CopyRegion* next;
    Type* type;
    SInt32 start;
    SInt32 end;
    Boolean flag;
} CopyRegion;

// NOTE: invented names: regions, type, start, isBitwise
static CopyRegion* CABI_AppendCopyRegion(
    CopyRegion* regions, Type* type, SInt32 start, Boolean isBitwise
) {
    CopyRegion* region;
    SInt32 end;

    if (IS_TYPE_BITFIELD(type)) {
        type = TYPE_BITFIELD(type)->bitfieldtype;
    }

    end = start + type->size;

    if (isBitwise) {
        for (region = regions; region; region = region->next) {
            if (region->flag) {
                if (region->start <= start && region->end >= end) {
                    return regions;
                }

                if (region->start >= start && region->end <= end) {
                    region->type = type;
                    region->start = start;
                    region->end = end;
                    for (region = region->next; region; region = region->next) {
                        if (region->start >= start && region->end <= end) {
                            region->end = region->start;
                        }
                    }
                    return regions;
                }
            }
        }
    }

    if (regions) {
        region = regions;
        while (region->next) {
            region = region->next;
        }
        region->next = lalloc(sizeof(CopyRegion));
        region = region->next;
    } else {
        regions = region = lalloc(sizeof(CopyRegion));
    }

    region->next = NULL;
    region->type = type;
    region->start = start;
    region->end = end;
    region->flag = isBitwise;

    return regions;
}

// NOTE: invented names: dest, src
static ENode* CABI_ClassInitLoopCallBack(ENode* dest, ENode* src) {
    ENodeList* list;

    src = makemonadicnode(src, EINDIRECT);
    src->rtype = TYPE(cabi_loop_class);

    if (cabi_loop_construct) {
        list = lalloc(sizeof(ENodeList));
        memclrw(list, sizeof(ENodeList));
        list->node = src;
        return CExpr_ConstructObject(cabi_loop_class, dest, list, TRUE, TRUE, FALSE, TRUE, TRUE);
    } else {
        return CABI_AssignObject(cabi_loop_class, dest, src);
    }
}

// NOTE: invented names: stmt, tclass, baseclass, offset, isCopyCtor, checkConstRef, dtor
static Statement* CABI_CopyConAssignCB(
    Statement* stmt, TypeClass* tclass, TypeClass* baseclass, SInt32 offset, Boolean isCopyCtor
) {
    ENode* expr2;
    ENode* startExpr;
    ENode* endExpr;
    ENodeList* list;
    ObjMemberVar* ivar;
    CopyRegion* regions;
    Type* type;
    ENode* expr;
    SInt32 i;
    SInt32 count;
    SInt32 off;
    Boolean checkConstRef;
    Object* dtor;

    if (baseclass) {
        if (baseclass->flags & CLASS_EMPTY) {
            if ((isCopyCtor && !CClass_CopyConstructor(baseclass)) ||
                (!isCopyCtor && !CClass_AssignmentOperator(baseclass))) {
                return stmt;
            }
        }

        expr = CABI_MakeCopyConArgExpr(tclass, isCopyCtor);
        expr->rtype = TYPE(baseclass);
        expr->data.monadic = CClass_DirectBasePointerCast(expr->data.monadic, tclass, baseclass);

        stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
        if (isCopyCtor) {
            list = lalloc(sizeof(ENodeList));
            list->next = NULL;
            list->node = expr;

            stmt->expr = CExpr_ConstructObject(
                baseclass, CABI_MakeThisExpr(NULL, offset), list, TRUE, FALSE, FALSE, FALSE, TRUE
            );
        } else {
            stmt->expr = CABI_AssignObject(
                baseclass,
                CClass_DirectBasePointerCast(CABI_MakeThisExpr(NULL, 0), tclass, baseclass),
                expr
            );
        }
    } else {
        checkConstRef = !isCopyCtor;
        for (ivar = tclass->ivars, regions = NULL; ivar; ivar = ivar->next) {
            if (ivar->name == vptr_name_node) {
                continue;
            }

            type = ivar->type;
            if (checkConstRef) {
                if (CParser_IsConst(type, ivar->qual) || IS_TYPE_REFERENCE(type) != 0) {
                    CError_Error(ERR_IMPLICIT_COPY_ASSIGNMENT_CONST_REF_MEMBER, tclass, 0);
                    checkConstRef = FALSE;
                }
            }

            switch (type->type) {
                case TYPEARRAY:
                    while (IS_TYPE_ARRAY(type)) {
                        type = TPTR_TARGET(type);
                    }
                    if (!IS_TYPE_CLASS(type)) {
                        regions = CABI_AppendCopyRegion(regions, ivar->type, ivar->offset, TRUE);
                        break;
                    }
                case TYPECLASS:
                    if (isCopyCtor) {
                        if (CClass_CopyConstructor(TYPE_CLASS(type)) ||
                            CClass_CopyConstructor(TYPE_CLASS(type))) {
                            regions =
                                CABI_AppendCopyRegion(regions, ivar->type, ivar->offset, FALSE);
                            break;
                        }
                    } else {
                        if (!CClass_IsTrivialCopyAssignClass(TYPE_CLASS(type))) {
                            regions =
                                CABI_AppendCopyRegion(regions, ivar->type, ivar->offset, FALSE);
                            break;
                        }
                    }
                default:
                    regions = CABI_AppendCopyRegion(regions, ivar->type, ivar->offset, TRUE);
            }
        }

        for (; regions; regions = regions->next) {
            if (regions->start >= regions->end) {
                continue;
            }

            type = regions->type;
            expr = CABI_MakeCopyConArgExpr(tclass, isCopyCtor);
            expr->rtype = type;

            if (!canadd(expr->data.monadic, regions->start)) {
                expr->data.monadic = makediadicnode(
                    expr->data.monadic, intconstnode(TYPE(&stunsignedlong), regions->start), EADD
                );
                optimizecomm(expr->data.monadic);
            }

            if (!regions->flag) {
                if (IS_TYPE_ARRAY(type)) {
                    while (IS_TYPE_ARRAY(type)) {
                        type = TPTR_TARGET(type);
                    }

                    CError_ASSERT(1969, IS_TYPE_CLASS(type));

                    if (type->size) {
                        count = regions->type->size / type->size;
                        if (count > 4) {
                            startExpr = CABI_MakeThisExpr(tclass, regions->start);
                            endExpr =
                                CABI_MakeThisExpr(tclass, regions->start + regions->type->size);
                            expr = CABI_MakeCopyConArgExpr(tclass, isCopyCtor)->data.monadic;
                            if (!canadd(expr, regions->start)) {
                                expr = makediadicnode(
                                    expr, intconstnode(TYPE(&stunsignedlong), regions->start), EADD
                                );
                                optimizecomm(expr);
                            }

                            cabi_loop_class = TYPE_CLASS(type);
                            cabi_loop_construct = isCopyCtor;
                            stmt = CFunc_GenerateLoop(
                                stmt,
                                CDecl_NewPointerType(type),
                                startExpr,
                                endExpr,
                                intconstnode(TYPE(&stunsignedlong), type->size),
                                expr,
                                CABI_ClassInitLoopCallBack
                            );
                        } else {
                            for (i = 0, off = regions->start; i < count; i++, off += type->size) {
                                expr = CABI_MakeCopyConArgExpr(tclass, isCopyCtor);
                                expr->rtype = type;

                                if (!canadd(expr->data.monadic, off)) {
                                    expr->data.monadic = makediadicnode(
                                        expr->data.monadic,
                                        intconstnode(TYPE(&stunsignedlong), off),
                                        EADD
                                    );
                                    optimizecomm(expr->data.monadic);
                                }

                                stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
                                if (isCopyCtor) {
                                    list = lalloc(sizeof(ENodeList));
                                    memclrw(list, sizeof(ENodeList));
                                    list->node = expr;
                                    stmt->expr = CExpr_ConstructObject(
                                        TYPE_CLASS(type),
                                        CABI_MakeThisExpr(tclass, off),
                                        list,
                                        TRUE,
                                        TRUE,
                                        FALSE,
                                        TRUE,
                                        TRUE
                                    );
                                } else {
                                    stmt->expr = CABI_AssignObject(
                                        TYPE_CLASS(type), CABI_MakeThisExpr(tclass, off), expr
                                    );
                                }
                            }
                        }

                        if (isCopyCtor && (dtor = CClass_Destructor(TYPE_CLASS(type)))) {
                            CExcept_RegisterMemberArray(
                                stmt, CABI_ThisArg(), regions->start, dtor, count, type->size
                            );
                        }
                    }
                } else {
                    CError_ASSERT(2030, IS_TYPE_CLASS(type));

                    stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);

                    if (isCopyCtor) {
                        list = lalloc(sizeof(ENodeList));
                        memclrw(list, sizeof(ENodeList));
                        list->node = expr;
                        stmt->expr = CExpr_ConstructObject(
                            TYPE_CLASS(type),
                            CABI_MakeThisExpr(tclass, regions->start),
                            list,
                            TRUE,
                            TRUE,
                            FALSE,
                            TRUE,
                            TRUE
                        );

                        if ((dtor = CClass_Destructor(TYPE_CLASS(type)))) {
                            CExcept_RegisterMember(
                                stmt, CABI_ThisArg(), regions->start, dtor, NULL, TRUE
                            );
                        }
                    } else {
                        stmt->expr = CABI_AssignObject(
                            TYPE_CLASS(type), CABI_MakeThisExpr(tclass, regions->start), expr
                        );
                    }
                }
            } else {
                if (IS_TYPE_ARRAY(type)) {
                    if (type->size > 1 && ((regions->start & 1) || (type->size & 1))) {
                        stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
                        stmt->expr = funccallexpr(
                            copy_func,
                            CABI_MakeThisExpr(tclass, regions->start),
                            getnodeaddress(expr, FALSE),
                            intconstnode(CABI_GetSizeTType(), type->size),
                            NULL
                        );
                        continue;
                    }

                    type = CDecl_NewStructType(type->size, CMach_GetTypeAlign(type));
                    expr->rtype = type;
                }

                expr2 = makemonadicnode(CABI_MakeThisExpr(tclass, regions->start), EINDIRECT);
                expr2->rtype = type;
                stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
                stmt->expr = makediadicnode(expr2, expr, EASS);
            }
        }
    }

    return stmt;
}

void CABI_MakeDefaultCopyConstructor(TypeClass* tclass, Object* func) {
    Boolean saveDebugInfo;
    CScopeSave savedScope;
    Statement firstStmt;
    Statement returnStmt;

    if (anyerrors || func->access == ACCESSNONE) {
        return;
    }

    CABI_ApplyClassFlags(func, tclass->eflags, FALSE);

    CScope_SetFunctionScope(func, &savedScope);

    CFunc_FuncGenSetup(&firstStmt, func);

    saveDebugInfo = copts.filesyminfo;
    copts.filesyminfo = FALSE;

    CFunc_SetupNewFuncArgs(func, TYPE_FUNC(func->type)->args);

    ctor_chain = NULL;
    if (tclass->flags & CLASS_HAS_VBASES) {
        arguments->next->object->name = CParser_GetUniqueName();
    }

    firstStmt.next = &returnStmt;

    memclrw(&returnStmt, sizeof(Statement));
    returnStmt.type = ST_RETURN;

    CABI_TransConstructor(func, &firstStmt, tclass, CABI_CopyConAssignCB, FALSE);

    CFunc_CodeCleanup(&firstStmt);
    CFunc_Gen(&firstStmt, func, 0);

    CScope_RestoreScope(&savedScope);
    copts.filesyminfo = saveDebugInfo;
}

void CABI_MakeDefaultAssignmentOperator(TypeClass* tclass, Object* func) {
    Boolean saveDebugInfo;
    Statement* stmt;
    ClassList* base;
    VClassList* vbase;
    ENode* expr1;
    ENode* expr2;
    CScopeSave savedScope;
    Statement firstStmt;

    if (anyerrors || func->access == ACCESSNONE) {
        return;
    }

    CABI_ApplyClassFlags(func, tclass->eflags, FALSE);

    CScope_SetFunctionScope(func, &savedScope);

    CFunc_FuncGenSetup(&firstStmt, func);

    saveDebugInfo = copts.filesyminfo;
    copts.filesyminfo = FALSE;

    CFunc_SetupNewFuncArgs(func, TYPE_FUNC(func->type)->args);

    stmt = curstmt;

    if (tclass->mode == CLASS_MODE_UNION ||
        (copts.block_assign && CClass_IsTrivialCopyAssignClass(tclass))) {
        expr1 = makemonadicnode(CABI_MakeThisExpr(tclass, 0), EINDIRECT);
        expr1->rtype = TYPE(tclass);

        expr2 = CABI_MakeCopyConArgExpr(tclass, FALSE);
        expr2->rtype = TYPE(tclass);

        stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
        stmt->expr = makediadicnode(expr1, expr2, EASS);
    } else {
        for (vbase = tclass->vbases; vbase; vbase = vbase->next) {
            stmt = CABI_CopyConAssignCB(stmt, tclass, vbase->base, vbase->offset, FALSE);
        }

        for (base = tclass->bases; base; base = base->next) {
            if (!base->is_virtual) {
                stmt = CABI_CopyConAssignCB(stmt, tclass, base->base, base->offset, FALSE);
            }
        }

        stmt = CABI_CopyConAssignCB(stmt, tclass, NULL, 0, FALSE);
    }

    stmt = CFunc_InsertStatement(ST_RETURN, stmt);
    stmt->expr = CABI_MakeThisExpr(NULL, 0);

    CFunc_CodeCleanup(&firstStmt);
    CFunc_Gen(&firstStmt, func, 0);

    CScope_RestoreScope(&savedScope);
    copts.filesyminfo = saveDebugInfo;
}

static Statement* CABI_DestroyMembers(Statement* stmt, ObjMemberVar* ivars, TypeClass* tclass) {
    Type* type;
    Object* dtor;
    ENode* expr;

    for (; ivars; ivars = ivars->next) {
        type = ivars->type;

        if (IS_TYPE_ARRAY(type)) {
            while (IS_TYPE_ARRAY(type)) {
                type = TPTR_TARGET(type);
            }

            if (IS_TYPE_CLASS(type) && (dtor = CClass_Destructor(TYPE_CLASS(type)))) {
                dtor = CABI_GetDestructorObject(dtor, CABIDestroyComplete);
                stmt = CABI_DestroyMembers(stmt, ivars->next, tclass);
                expr = create_objectrefnode(dtor);
                expr->flags |= ENODE_FLAG_NONVIRTUAL;

                stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
                stmt->expr = funccallexpr(
                    darr_func,
                    CABI_MakeThisExpr(tclass, ivars->offset),
                    expr,
                    intconstnode(TYPE(&stsignedlong), type->size),
                    intconstnode(TYPE(&stsignedlong), ivars->type->size / type->size)
                );
                return stmt;
            }
        } else {
            if (IS_TYPE_CLASS(type) && (dtor = CClass_Destructor(TYPE_CLASS(type)))) {
                stmt = CABI_DestroyMembers(stmt, ivars->next, tclass);

                stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
                stmt->expr = CABI_DestroyObject(
                    dtor, CABI_MakeThisExpr(tclass, ivars->offset), CABIDestroyComplete, TRUE, FALSE
                );
                return stmt;
            }
        }
    }

    return stmt;
}

static Statement* CABI_DestroyBases(Statement* stmt, ClassList* bases) {
    ClassList* base;
    Object* dtor;
    SInt32 count;
    SInt32 i;

    base = bases;
    count = 0;
    while (base) {
        base = base->next;
        count++;
    }

    while (count > 0) {
        base = bases;
        i = 1;
        while (i < count) {
            i++;
            base = base->next;
        }

        if (!base->is_virtual && (dtor = CClass_Destructor(base->base))) {
            stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
            stmt->expr = CABI_DestroyObject(
                dtor, CABI_MakeThisExpr(NULL, base->offset), CABIDestroyBase, TRUE, FALSE
            );
        }

        count--;
    }

    return stmt;
}

static Statement* CABI_DestroyVBases(Statement* stmt, VClassList* vbases) {
    Object* dtor;

    for (; vbases; vbases = vbases->next) {
        if ((dtor = CClass_Destructor(vbases->base))) {
            stmt = CABI_DestroyVBases(stmt, vbases->next);
            stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
            stmt->expr = CABI_DestroyObject(
                dtor, CABI_MakeThisExpr(NULL, vbases->offset), CABIDestroyBase, TRUE, FALSE
            );
            break;
        }
    }

    return stmt;
}

// NOTE: invented names: dtor, func, stmt, tclass, mode (dtor and func are unused), includeSize
void CABI_TransDestructor(
    Object* dtor, Object* func, Statement* stmt, TypeClass* tclass, CABIDestroyMode mode
) {
    CLabel* label2;
    // a null this skips it all; a positive short argument frees the object as well
    Boolean doDelete;
    CLabel* label;
    Statement* scan;
    Statement* next;
    // the vtable pointers and the base classes
    Boolean doBases;
    Boolean doVBases;
    Boolean doMembers;
    CLabel* label3;
    Object* dealloc;
    Boolean includeSize;

    if (tclass->sominfo) {
        doDelete = doBases = doVBases = FALSE;
        doMembers = TRUE;
    } else {
        doDelete = doBases = doMembers = doVBases = TRUE;
    }

    label = newlabel();

    for (scan = stmt; scan; scan = next) {
        if (scan->type == ST_RETURN) {
            CError_ASSERT(2334, !scan->expr);
            scan->type = ST_GOTO;
            scan->label = label;
        }

        if ((next = scan->next) && next->type == ST_RETURN && !next->next) {
            CError_ASSERT(2339, !next->expr);
            scan->next = NULL;
            break;
        }
    }

    scan = stmt;

    if (doDelete) {
        label2 = newlabel();
        scan = CFunc_InsertStatement(ST_IFNGOTO, scan);
        scan->expr = CABI_MakeThisExpr(NULL, 0);
        scan->label = label2;
    }

    if (doBases && tclass->vtable && tclass->vtable->object && tclass->vtable->owner == tclass) {
        cabi_pathroot.next = NULL;
        cabi_pathroot.type = TYPE(tclass);
        cabi_pathcur = &cabi_pathroot;
        trans_vtboffsets = NULL;
        scan = CABI_InitVTablePtrs(scan, tclass->vtable->object, tclass, tclass, 0, 0);
    }

    if (!tclass->sominfo && (tclass->flags & CLASS_VBASE_CTOR_OFFSET)) {
        CABI_InitVBaseCtorOffsets(scan, tclass);
    }

    scan = stmt;
    while (scan->next) {
        scan = scan->next;
    }

    scan = CFunc_InsertStatement(ST_LABEL, scan);
    scan->label = label;
    scan->dobjstack = NULL;
    label->stmt = scan;

    if (doMembers && !(tclass->flags & CLASS_HANDLEOBJECT)) {
        scan = CABI_DestroyMembers(scan, tclass->ivars, tclass);
    }

    if (doBases && tclass->bases) {
        scan = CABI_DestroyBases(scan, tclass->bases);
    }

    if (doVBases && (tclass->flags & CLASS_HAS_VBASES)) {
        label3 = newlabel();
        scan = CFunc_InsertStatement(ST_IFNGOTO, scan);
        scan->expr = CABI_MakeVArgExpr();
        scan->label = label3;

        scan = CABI_DestroyVBases(scan, tclass->vbases);

        scan = CFunc_InsertStatement(ST_LABEL, scan);
        scan->label = label3;
        label3->stmt = scan;
    }

    if (doDelete) {
        scan = CFunc_InsertStatement(ST_IFGOTO, scan);
        scan->expr =
            CExpr_New_ELESSEQU_Node(CABI_MakeVArgExpr(), intconstnode(TYPE(&stsignedshort), 0));
        scan->label = label2;

        scan = CFunc_InsertStatement(ST_EXPRESSION, scan);
        dealloc = CParser_FindDeallocationObject(TYPE(tclass), NULL, FALSE, FALSE, &includeSize);
        if (includeSize) {
            scan->expr = funccallexpr(
                dealloc,
                CABI_MakeThisExpr(NULL, 0),
                intconstnode(CABI_GetSizeTType(), tclass->size),
                NULL,
                NULL
            );
        } else {
            scan->expr = funccallexpr(dealloc, CABI_MakeThisExpr(NULL, 0), NULL, NULL, NULL);
        }

        scan = CFunc_InsertStatement(ST_LABEL, scan);
        scan->label = label2;
        label2->stmt = scan;
    }

    scan = CFunc_InsertStatement(ST_RETURN, scan);
    if (tclass->sominfo) {
        scan->expr = NULL;
    } else {
        scan->expr = CABI_MakeThisExpr(NULL, 0);
    }
}

void CABI_MakeDefaultDestructor(TypeClass* tclass, Object* func) {
    Boolean saveDebugInfo;
    CScopeSave savedScope;
    Statement firstStmt;
    Statement returnStmt;

    if (anyerrors || func->access == ACCESSNONE) {
        return;
    }

    CABI_ApplyClassFlags(func, tclass->eflags, FALSE);

    CScope_SetFunctionScope(func, &savedScope);

    CFunc_FuncGenSetup(&firstStmt, func);

    saveDebugInfo = copts.filesyminfo;
    copts.filesyminfo = FALSE;

    CFunc_SetupNewFuncArgs(func, TYPE_FUNC(func->type)->args);

    firstStmt.next = &returnStmt;

    memclrw(&returnStmt, sizeof(Statement));
    returnStmt.type = ST_RETURN;

    CFunc_CodeCleanup(&firstStmt);

    CABI_TransDestructor(func, func, &firstStmt, tclass, CABIDestroyBase);

    CFunc_Gen(&firstStmt, func, 0);

    CScope_RestoreScope(&savedScope);
    copts.filesyminfo = saveDebugInfo;
}

// NOTE: invented name: dtor
static void CABI_CreateLayeredDestructor(
    TypeClass* tclass, Object* dtor, Object* func, CABIDestroyMode mode
) {
    Boolean saveDebugInfo;
    CScopeSave savedScope;
    Statement firstStmt;
    Statement returnStmt;

    CError_FATAL(2524);

    CScope_SetFunctionScope(func, &savedScope);

    CFunc_FuncGenSetup(&firstStmt, func);

    saveDebugInfo = copts.filesyminfo;
    copts.filesyminfo = FALSE;

    CFunc_SetupNewFuncArgs(func, TYPE_FUNC(func->type)->args);

    firstStmt.next = &returnStmt;

    memclrw(&returnStmt, sizeof(Statement));
    returnStmt.type = ST_RETURN;

    CFunc_CodeCleanup(&firstStmt);

    CABI_TransDestructor(dtor, func, &firstStmt, tclass, mode);

    CFunc_Gen(&firstStmt, func, 0);

    CScope_RestoreScope(&savedScope);
    copts.filesyminfo = saveDebugInfo;
}

void CABI_MakeLayeredDestructor(TypeClass* tclass, Object* func) {
    Object* dtor;
    CABIDestroyMode mode;

    CError_FATAL(2557);

    if (anyerrors || func->access == ACCESSNONE) {
        return;
    }

    if ((dtor = CClass_Destructor(tclass))) {
        if (CABI_GetDestructorObject(dtor, CABIDestroyBase) == func) {
            mode = CABIDestroyBase;
        } else if (CABI_GetDestructorObject(dtor, CABIDestroyDelete) == func) {
            mode = CABIDestroyDelete;
        } else if (CABI_GetDestructorObject(dtor, CABIDestroySDelete) == func) {
            mode = CABIDestroySDelete;
        } else if (CABI_GetDestructorObject(dtor, CABIDestroyComplete) == func) {
            mode = CABIDestroyComplete;
        } else {
            CError_FATAL(2567);
        }
    }

    CABI_CreateLayeredDestructor(tclass, dtor, func, mode);
}

Object* CABI_GetDestructorObject(Object* obj, CABIDestroyMode mode) {
    return obj;
}

static void CABI_AddLayeredDestructor(
    TypeClass* tclass, Object* dtor, HashNameNode* name, Boolean is_virtual
) {
    Object* func;

    CError_FATAL(2667);

    func = CParser_NewFunctionObject(NULL);
    func->nspace = dtor->nspace;
    func->name = name;
    func->type = TYPE(CDecl_MakeDefaultDtorType(tclass, is_virtual));
    func->qual = Q_IMPLICIT_WEAK | Q_MANGLE_NAME;
    func->qual |= Q_INLINE;
    CABI_ApplyClassFlags(func, tclass->eflags, TRUE);

    CError_ASSERT(2678, IS_TYPE_FUNC(func->type));
    TYPE_FUNC(func->type)->flags |= FUNC_AUTO_GENERATED;

    if (dtor->datatype == DVFUNC) {
        func->datatype = DVFUNC;
        CMangler_GetLinkName(func);
        func->datatype = DFUNC;
    }

    CScope_AddObject(func->nspace, func->name, OBJ_BASE(func));
}

void CABI_AddLayeredDestructors(TypeClass* tclass) {
    Object* dtor;

    CError_FATAL(2707);

    if ((dtor = CClass_Destructor(tclass))) {
        CABI_AddLayeredDestructor(tclass, dtor, CMangler_DeleteDtorName(), TRUE);
        CABI_AddLayeredDestructor(tclass, dtor, CMangler_SDeleteDtorName(), TRUE);
        if (tclass->vbases) {
            CABI_AddLayeredDestructor(tclass, dtor, CMangler_VBaseDtorName(), FALSE);
        }
    }
}

ENode* CABI_DestroyObject(
    Object* dtor, ENode* objexpr, CABIDestroyMode mode, Boolean nonvirtual, Boolean dealloc
) {
    ENode* expr;
    short varg; // NOTE: invented name
    ENodeList* list;

    switch (mode) {
        case CABIDestroyDelete:
        case CABIDestroySDelete:
            if (dealloc) {
                varg = 1;
            } else {
                varg = -1;
            }
            break;
        case CABIDestroyComplete:
            varg = -1;
            break;
        case CABIDestroyBase:
            varg = 0;
            break;
        default:
            CError_FATAL(2791);
    }

    expr = lalloc(sizeof(ENode));
    expr->type = EFUNCCALL;
    expr->cost = 200;
    expr->flags = 0;
    expr->rtype = &stvoid;
    expr->data.funccall.funcref = create_objectrefnode(dtor);
    if (nonvirtual) {
        expr->data.funccall.funcref->flags |= ENODE_FLAG_NONVIRTUAL;
    }
    expr->data.funccall.functype = TYPE_FUNC(dtor->type);
    dtor->flags |= OBJECT_USED;

    list = lalloc(sizeof(ENodeList));
    list->node = objexpr;
    expr->data.funccall.args = list;
    list->next = lalloc(sizeof(ENodeList));
    list = list->next;
    list->next = NULL;
    list->node = intconstnode(TYPE(&stsignedshort), varg);
    return expr;
}
