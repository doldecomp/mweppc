#include "compiler/TOC.h"
#include "compiler/BE_symbol.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
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
#include "compiler/CScope.h"
#include "compiler/DWARF.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/InlineAsm.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/InstrSelection.h"
#include "compiler/Intrinsics.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/Operands.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/StackFrame.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "driver/COSToolsCLT.h"
#include "driver/oslib.h"
#include <stdio.h>
#include <string.h>

ObjectList* toclist;
ObjectList* exceptionlist;
void* descriptorlist;
PoolEntry* floatconstpool;
PoolEntry* doubleconstpool;
ObjectList* floatconstlist;
PoolEntry* vectorconstpool;
ObjectList* vectorconstlist;
Object toc0;
Boolean no_descriptors;
CodeLabelList* codelabellist;
AnchorEntry* anchorlist;

static inline void estimate_func_param_size(ENode* node);

static inline int disables_optimizer(ENode* node) {
    ENode* funcref = node->data.funccall.funcref;
    if (ENODE_IS(funcref, EOBJREF)) {
        if (!strcmp(CMangler_GetLinkName(funcref->data.objref)->name, "__setjmp")) {
            return TRUE;
        }
        if (!strcmp(CMangler_GetLinkName(funcref->data.objref)->name, "__vec_setjmp")) {
            return TRUE;
        }
    }
    return FALSE;
}

void setupaddressing(void) {
    toclist = descriptorlist = floatconstlist = NULL;
    exceptionlist = NULL;
    vectorconstlist = NULL;
    vectorconstpool = NULL;
    floatconstpool = NULL;
    doubleconstpool = NULL;

    no_descriptors = TRUE;
    memclrw(&toc0, sizeof(toc0));
    toc0.name = GetHashNameNodeExport("TOC");
    anchorlist = NULL;
}

static inline void reference_anchor(Object* anchor) {
    VarInfo* vi;
    ObjectList* list;

    vi = Registers_GetVarInfo(anchor);
    vi->used = TRUE;
    vi->usage += copts.optimizesize ? 1 : curstmtvalue;
    if (vi->usage < 3) {
        vi->usage = 3;
    }
    for (list = toclist; list != NULL; list = list->next) {
        if (anchor == list->object) {
            return;
        }
    }
    list = galloc(sizeof(ObjectList));
    memclrw(list, sizeof(ObjectList));
    list->object = anchor;
    list->next = toclist;
    toclist = list;
}

void reset_anchor_varinfo(void) {
    ObjectList* list;
    AnchorEntry* anchor;
    BE_SymGroup* group;
    ObjGenSymbolLink* link;
    RegInfo* reginfo;

    for (list = toclist; list != NULL; list = list->next) {
        list->object->u.data.info = CodeGen_GetNewVarInfo();
    }

    for (anchor = anchorlist; anchor != NULL; anchor = anchor->next) {
        link = anchor->symbolLink;
        for (group = link->groups; group != NULL; group = group->next) {
            memclrw(group, sizeof(BE_SymGroup));
            group->limit = 3;
            link->tail = &link->groups;
            *link->tail = group;
            link->tail = &group->next;
            break;
        }
    }
    anchorlist = NULL;

    for (reginfo = reg_info_list; reginfo != NULL; reginfo = reginfo->next) {
        if (reginfo->object != NULL && reginfo->object->otype == OT_OBJECT) {
            reginfo->object->u.data.info = CodeGen_GetNewVarInfo();
        }
    }
}

static void add_anchor_reference(Object* anchor, Object* obj) {
    BE_SymGroup* other;
    AnchorEntry* entry;
    ObjectList* list;
    BE_SymGroup* group;
    BE_SymNode* sym;

    group = GetObjectExtension(obj);
    for (entry = anchorlist; entry != NULL; entry = entry->next) {
        if (anchor == entry->object) {
            if (group->count >= group->limit) {
                reference_anchor(anchor);
                group->full = TRUE;
                entry->full = group->full;
                return;
            }
            for (list = group->objects; list != NULL; list = list->next) {
                if (list->object == obj) {
                    return;
                }
            }
            list = galloc(sizeof(ObjectList));
            memclrw(list, sizeof(ObjectList));
            list->object = obj;
            list->next = group->objects;
            group->objects = list;
            group->count++;
            if (group->count == group->limit) {
                reference_anchor(anchor);
                group->full = TRUE;
                entry->full = group->full;
                sym = MakeSymbolTableEntry(obj);
                sym->elf.st_other |= 0x80;
                sym->section->symbolLink->symbol->elf.st_other &= ~0x40;
                for (other = entry->symbolLink->groups; other != NULL; other = other->next) {
                    if (other->base != 0) {
                        other->limit = 2;
                    } else {
                        other->limit = 1;
                    }
                    if (other->count >= other->limit && !other->full) {
                        reference_anchor(anchor);
                        other->full = TRUE;
                    }
                }
            }
            return;
        }
    }

    entry = galloc(sizeof(AnchorEntry));
    memclrw(entry, sizeof(AnchorEntry));
    entry->object = anchor;
    list = galloc(sizeof(ObjectList));
    memclrw(list, sizeof(ObjectList));
    list->object = obj;
    list->next = group->objects;
    entry->symbolLink = MakeSymbolTableEntry(obj)->section->symbolLink;
    if (entry->symbolLink->object != anchor) {
        CError_FATAL(538);
    }
    group->objects = list;
    group->count++;
    entry->next = anchorlist;
    anchorlist = entry;
}

Boolean used_anchor(Object* obj) {
    AnchorEntry* p;

    for (p = anchorlist; p != NULL; p = p->next) {
        if (obj == p->object) {
            if (!p->full) {
                return FALSE;
            }
            return TRUE;
        }
    }
    return FALSE;
}

Boolean is_pooled_object(Object* obj) {
    Object* anchor;
    int reg;

    if (!used_anchor(obj) && obj->u.data.reginfo.reg) {
        return FALSE;
    }
    if (obj->datatype == DDATA && !is_16bitdata(obj) && copts.pool_data) {
        anchor = GetDataPoolRoot(obj);
        if (anchor && obj->toc && used_anchor(obj->toc)) {
            reg = Registers_GetVarInfo(obj->toc) ? Registers_GetVarInfo(obj->toc)->reg : 0;
            if (reg) {
                CError_ASSERT(586, anchor == obj->toc);
                if (GetObjectExtension(obj)->full) {
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

Object* createIndirect(Object* obj, Boolean newVarInfo, Boolean reference) {
    Object* anchor;

    CError_ASSERT(708, obj->section);
    MakeSymbolTableEntry(obj);
    if (obj->datatype == DDATA && !is_16bitdata(obj) && copts.pool_data) {
        if ((anchor = GetDataPoolRoot(obj))) {
            add_anchor_reference(anchor, obj);
        }
    }
    return NULL;
}

Object* createfloatconstant(Type* type, Float* data) {
    ObjectList* list;
    UInt32* value;
    Object* obj;
    UInt32* check;
    Type* objtype;
    Float* copy;
    union {
        double d;
        float f[2];
    } pair;

    if (!copts.use_fp) {
        PPCError_ErrorTerm(PPCERR_FLOAT_CONSTANT_WITHOUT_FLOAT);
    }
    if (IS_TYPE_VEC2X32FLOAT(type)) {
        pair.f[0] = data->data.value;
        pair.f[1] = data->data.value;
        data->data.value = pair.d;
    }

    value = (UInt32*)data;
    for (list = floatconstlist; list; list = list->next) {
        objtype = list->object->type;
        check = (UInt32*)list->object->u.data.u.floatconst;
        if (objtype == type && check[0] == value[0] && check[1] == value[1]) {
            return list->object;
        }
    }

    obj = galloc(sizeof(Object));
    memclrw(obj, sizeof(Object));
    obj->otype = OT_OBJECT;
    obj->type = type;
    obj->name = CParser_GetUniqueName();
    obj->toc = NULL;
    obj->u.data.info = NULL;
    obj->u.data.linkname = obj->name;
    obj->sclass = TK_STATIC;
#if VERSION == VERSION_GC_2_0P2
    obj->qual = Q_INLINE_DATA;
#else
    obj->qual = Q_CONST | Q_INLINE_DATA;
#endif
    obj->datatype = DDATA;
    PPC_EABI_SetSection(obj, type->size, TRUE, TRUE);
    MakeSymbolTableEntry(obj);

    obj->flags |= OBJECT_REFERENCED;

    obj->u.data.u.floatconst = galloc(sizeof(Float));
    copy = obj->u.data.u.floatconst;
    *copy = *data;

    list = galloc(sizeof(ObjectList));
    memclrw(list, sizeof(ObjectList));
    list->object = obj;
    list->next = floatconstlist;
    floatconstlist = list;

    if (!copts.soft_float) {
        ObjGen_DeclareFloatConst(obj);
    }
    return obj;
}

void DeclarePooledConstants(void) {
    PoolEntry* entry;
    char* buffer;
    SInt32 fsize;
    SInt32 dsize;
    SInt32 vsize;

    fsize = 0;
    for (entry = floatconstpool; entry; entry = entry->next) {
        fsize += 4;
    }

    if (fsize) {
        floatconstpool->object->type = CDecl_NewArrayType(TYPE(&stfloat), fsize);
        buffer = galloc(fsize);
        for (entry = floatconstpool; entry; entry = entry->next) {
            memcpy(buffer + entry->offset, entry->buffer, 4);
        }
        CInit_DeclareReadOnlyData(floatconstpool->object, buffer, NULL, fsize);
    }

    dsize = 0;
    for (entry = doubleconstpool; entry; entry = entry->next) {
        dsize += 8;
    }

    if (dsize) {
        doubleconstpool->object->type = CDecl_NewArrayType(TYPE(&stdouble), dsize);
        buffer = galloc(dsize);
        for (entry = doubleconstpool; entry; entry = entry->next) {
            memcpy(buffer + entry->offset, entry->buffer, 8);
        }
        CInit_DeclareReadOnlyData(doubleconstpool->object, buffer, NULL, dsize);
    }

    vsize = 0;
    for (entry = vectorconstpool; entry; entry = entry->next) {
        vsize += 16;
    }

    if (vsize) {
        vectorconstpool->object->type = CDecl_NewArrayType(TYPE(&stvectorsignedlong), vsize);
        buffer = galloc(vsize);
        for (entry = vectorconstpool; entry; entry = entry->next) {
            memcpy(buffer + entry->offset, entry->buffer, 16);
        }
        CInit_DeclareReadOnlyData(vectorconstpool->object, buffer, NULL, vsize);
    }
}

static void RewriteVectorConst(ENode* expr) {
    PoolEntry* entry;
    Object* object;
    SInt32 offset;
    ENode* inner;
    UInt8 data[16];

    CMach_InitVectorMem(expr->rtype, expr->data.vector128val, data, TRUE);

    if (cparamblkptr->precompile == 1) {
        CError_Error(ERR_ILLEGAL_USE_PRECOMPILED_HEADER);
    }

    for (entry = vectorconstpool; entry; entry = entry->next) {
        if (!memcmp(entry->buffer, data, 16)) {
            break;
        }
    }

    if (!entry) {
        if (vectorconstpool) {
            object = vectorconstpool->object;
            offset = vectorconstpool->offset + 16;
            vectorconstpool->object->type->size += 16;
        } else {
            DeclInfo di;
            memclrw(&di, sizeof(di));
            di.thetype = CDecl_NewArrayType(TYPE(&stvectorsignedlong), 16);
            di.name = GetHashNameNodeExport("@vectorBase0");
            di.qual = Q_CONST;
            di.storageclass = TK_STATIC;
            di.is_extern_c = TRUE;
            if (copts.sdata2_threshold > 16) {
                di.section = GetCurrentSection(SECT_SCONST_TYPE);
            } else {
                di.section = GetCurrentSection(SECT_CONST_TYPE);
            }
            object = CParser_NewGlobalDataObject(&di);
            object->nspace = cscope_root;
            offset = 0;
        }

        entry = galloc(sizeof(PoolEntry));
        entry->next = vectorconstpool;
        vectorconstpool = entry;
        entry->object = object;
        entry->offset = offset;
        entry->buffer = galloc(16);
        memcpy(entry->buffer, data, 16);
    }

    if (entry->offset) {
        inner = makediadicnode(
            create_objectrefnode(entry->object),
            intconstnode(TYPE(&stunsignedlong), entry->offset),
            EADD
        );
    } else {
        inner = create_objectrefnode(entry->object);
    }

    expr->type = EINDIRECT;
    expr->cost = 1;
    expr->flags |= ENODE_FLAG_CONST;
    expr->data.monadic = inner;
}

static Object* createcodelabel(CLabel* label) {
    CodeLabelList* list;
    Object* obj;

    for (list = codelabellist; list; list = list->next) {
        if (list->label == label) {
            return list->object;
        }
    }

    obj = galloc(sizeof(Object));
    memclrw(obj, sizeof(Object));
    obj->otype = OT_OBJECT;
    obj->type = (Type*)&void_ptr;
    obj->name = label->uniquename;
    obj->toc = NULL;
    obj->u.data.info = NULL;
    obj->sclass = TK_STATIC;
    obj->qual = Q_CONST;
    obj->datatype = DDATA;
    obj->flags |= OBJECT_REFERENCED | OBJECT_DEFINED;
    obj->section = GetCurrentSection(SECT_SDATA_TYPE);

    list = galloc(sizeof(CodeLabelList));
    memclrw(list, sizeof(CodeLabelList));
    list->object = obj;
    list->label = label;
    list->next = codelabellist;
    codelabellist = list;

    return obj;
}

void dumpcodelabels(Object* func) {
    CodeLabelList* list;

    for (list = codelabellist; list; list = list->next) {
        ObjGen_DeclareCodeLabel(list->object, list->label->pclabel->block->codeOffset, func);
    }
}

static void referenceexception(Object* obj) {
    ObjectList* list;

    if (obj == NULL) {
        return;
    }
    if (obj->otype != OT_OBJECT) {
        return;
    }
    if (obj->datatype != DLOCAL) {
        return;
    }
    for (list = exceptionlist; list; list = list->next) {
        if (list->object == obj) {
            return;
        }
    }

    list = lalloc(sizeof(ObjectList));
    memclrw(list, sizeof(ObjectList));
    list->object = obj;
    list->next = exceptionlist;
    exceptionlist = list;
}

static inline ENodeType invert_relop(ENodeType nodetype) {
    switch (nodetype) {
        case ELESS:
            return EGREATEREQU;
        case EGREATER:
            return ELESSEQU;
        case ELESSEQU:
            return EGREATER;
        case EGREATEREQU:
            return ELESS;
        case EEQU:
            return ENOTEQU;
        case ENOTEQU:
            return EEQU;
        default:
            return nodetype;
    }
}

static ENode* COND_to_COMPARE(ENode* cond, ENode* expr1, ENode* expr2) {
    SInt32 val1;
    SInt32 val2;
    SInt32 condval;
    ENodeType invop;

    while (expr1->type == ETYPCON && TYPE_FITS_IN_REGISTER(expr1->rtype)) {
        expr1 = expr1->data.monadic;
    }
    while (expr2->type == ETYPCON && TYPE_FITS_IN_REGISTER(expr2->rtype)) {
        expr2 = expr2->data.monadic;
    }

    if (expr1->type != EINTCONST ||
        !TYPE_FITS_IN_REGISTER(expr1->rtype) ||
        !CInt64_IsInRange(expr1->data.intval, 4)) {
        return NULL;
    }
    if (expr2->type != EINTCONST ||
        !TYPE_FITS_IN_REGISTER(expr2->rtype) ||
        !CInt64_IsInRange(expr2->data.intval, 4)) {
        return NULL;
    }

    val1 = expr1->data.intval.lo;
    val2 = expr2->data.intval.lo;
    condval = 0;
    switch (val1) {
        case 1:
            if (val2 != 0) {
                return NULL;
            }
            break;
        case 0:
            condval = 1;
            if (val2 != 1) {
                return NULL;
            }
            break;
        default:
            return NULL;
    }

    while (cond->type == ELOGNOT) {
        condval = (condval + 1) & 1;
        cond = cond->data.monadic;
    }

    if (condval) {
        invop = invert_relop(cond->type);
        if (invop == cond->type) {
            return NULL;
        }
        cond->type = invop;
    }

    return cond;
}

static ENode* comparewithzero(ENode* expr) {
    ENode* compare;
    ENode* zero;
    ENode* inner;

    compare = lalloc(sizeof(ENode));
    memclrw(compare, sizeof(ENode));
    zero = lalloc(sizeof(ENode));
    memclrw(zero, sizeof(ENode));

    for (inner = expr; inner->type == EFORCELOAD || inner->type == ETYPCON;
         inner = inner->data.monadic) {
        if (!TYPE_FITS_IN_REGISTER(expr->rtype)) {
            break;
        }
    }
    if (inner->type == ELOGNOT ||
        inner->type == ELAND ||
        inner->type == ELOR ||
        (inner->type >= ELESS && inner->type <= ENOTEQU)) {
        expr = inner;
    }

    while (expr->type == EFORCELOAD || expr->type == ECOMMA) {
        if (!TYPE_FITS_IN_REGISTER(expr->rtype)) {
            break;
        }
        if (expr->type == ECOMMA) {
            expr->data.diadic.right = comparewithzero(expr->data.diadic.right);
            return expr;
        }
        expr = expr->data.monadic;
    }

    if (expr->type == ECOND && TYPE_FITS_IN_REGISTER(expr->rtype)) {
        inner = COND_to_COMPARE(expr->data.cond.cond, expr->data.cond.expr1, expr->data.cond.expr2);
        if (inner) {
            expr = inner;
        }
    }

    if (expr->type >= ELESS && expr->type <= ENOTEQU) {
        return expr;
    }

    if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype)) {
        static Float float0 = {0.0};

        zero->type = EFLOATCONST;
        zero->cost = 0;
        zero->rtype = (expr->rtype->size == 4) ? (Type*)&stfloat : (Type*)&stdouble;
        zero->data.floatval = float0;
    } else {
        zero->type = EINTCONST;
        zero->cost = 0;
        if (TYPE_IS_8BYTES(expr->rtype)) {
            zero->rtype = (Type*)&stsignedlonglong;
        } else {
            zero->rtype = (Type*)&stsignedint;
        }
        zero->data.intval.lo = 0;
        zero->data.intval.hi = 0;
    }

    compare->type = ENOTEQU;
    compare->cost = expr->cost;
    compare->rtype = (Type*)&stsignedint;
    compare->data.diadic.left = expr;
    compare->data.diadic.right = zero;
    return compare;
}

static inline void rewritefunctioncallreturningstruct(ENode* expr) {
    ENode* ret_expr;
    ENode* copy;

    ret_expr = expr->data.funccall.args->node;

    copy = lalloc(sizeof(ENode));
    memclrw(copy, sizeof(ENode));

    *copy = *expr;
    expr->type = ECOMMA;
    expr->data.diadic.left = copy;
    expr->data.diadic.right = ret_expr;
}

static void rewritestrcpy(ENode* expr) {
    ENode* int_expr;
    ENodeList* list;
    SInt32 size;

    int_expr = lalloc(sizeof(ENode));
    memclrw(int_expr, sizeof(ENode));
    int_expr->type = EINTCONST;
    int_expr->cost = 0;
    int_expr->flags = 0;
    int_expr->rtype = (Type*)&stunsignedlong;
    size = expr->data.funccall.args->next->node->data.string.size;
    int_expr->data.intval.lo = size;
    int_expr->data.intval.hi = size < 0 ? 0xffffffffU : 0U;

    list = lalloc(sizeof(ENodeList));
    memclrw(list, sizeof(ENodeList));
    list->next = NULL;
    list->node = int_expr;
    expr->data.funccall.args->next->next = list;
    expr->data.funccall.funcref->data.objref = __memcpy_object;
}

static inline SInt32 magnitude(Type* type) {
    if (IS_TYPE_FLOAT(type)) {
        return type->size * 4;
    } else if (is_unsigned(type)) {
        return (type->size * 2) + 1;
    } else {
        return type->size * 2;
    }
}

static inline Type* promote_type(Type* type) {
    if (IS_TYPE_ENUM(type)) {
        type = TYPE_ENUM(type)->enumtype;
    }
    if (TYPE_INTEGRAL(type)->integral > stsignedint.integral) {
        return type;
    } else {
        return (Type*)&stsignedint;
    }
}

static Type* common_type(Type* type1, Type* type2) {
    Type* swap;

    if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type1) || IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type2)) {
        return TYPE_INTEGRAL(type1)->integral > TYPE_INTEGRAL(type2)->integral ? type1 : type2;
    }

    type1 = promote_type(type1);
    type2 = promote_type(type2);
    if (type1 != type2) {
        if (TYPE_INTEGRAL(type1)->integral < TYPE_INTEGRAL(type2)->integral) {
            swap = type1;
            type1 = type2;
            type2 = swap;
        }

        if (type1->size == type2->size && !is_unsigned(type1) && is_unsigned(type2)) {
            if (type1 == (Type*)&stsignedlong) {
                type1 = (Type*)&stunsignedlong;
            } else {
                CError_ASSERT(1845, type1 == (Type*)&stsignedlonglong);
                type1 = (Type*)&stunsignedlonglong;
            }
        }
    }

    return type1;
}

static void rewrite_opassign(ENode* expr, ENodeType exprtype) {
    ENode* left_sub;
    ENode* right;
    Type* left_type;
    Type* right_type;
    ENode* cast;
    ENode* new_expr;
    Type* commontype;
    Type* promo_left;
    Type* promo_right;

    left_sub = expr->data.diadic.left->data.monadic;
    right = expr->data.diadic.right;
    left_type = expr->data.diadic.left->rtype;
    right_type = expr->data.diadic.right->rtype;

    new_expr = lalloc(sizeof(ENode));
    memclrw(new_expr, sizeof(ENode));
    new_expr->type = exprtype;
    new_expr->rtype = left_type;
    new_expr->data.diadic.left = expr->data.diadic.left;
    new_expr->data.diadic.right = right;

    expr->type = EASS;
    expr->data.diadic.left = left_sub;
    expr->data.diadic.right = new_expr;

    if (left_sub->type != EOBJREF) {
        ENode* define;
        ENode* reuse;

        define = lalloc(sizeof(ENode));
        memclrw(define, sizeof(ENode));
        define->type = EDEFINE;
        define->rtype = left_type;

        reuse = lalloc(sizeof(ENode));
        memclrw(reuse, sizeof(ENode));
        reuse->type = EREUSE;
        reuse->rtype = left_type;
        reuse->data.monadic = define;

        if (left_sub->type != EBITFIELD) {
            define->data.monadic = expr->data.diadic.left;
            expr->data.diadic.left = define;
            new_expr->data.diadic.left->data.diadic.left = reuse;
        } else {
            ENode* copy;
            define->data.monadic = left_sub->data.diadic.left;
            left_sub->data.diadic.left = define;

            copy = lalloc(sizeof(ENode));
            *copy = *left_sub;
            copy->data.diadic.left = reuse;
            new_expr->data.diadic.left->data.diadic.left = copy;
        }
    }

    switch (exprtype) {
        case EADD:
        case ESUB:
            if (IS_TYPE_POINTER(left_type)) {
                break;
            }
            if (right->type == EINTCONST &&
                TYPE_FITS_IN_REGISTER(left_type) &&
                !TYPE_IS_8BYTES(right_type)) {
                break;
            }
        case EAND:
        case EXOR:
        case EOR:
            if (left_type == right_type) {
                break;
            }
        case EMUL:
        case EDIV:
        case EMODULO:
            commontype = common_type(left_type, right_type);
            if (left_type != commontype) {
                cast = lalloc(sizeof(ENode));
                memclrw(cast, sizeof(ENode));
                cast->type = ETYPCON;
                cast->rtype = left_type;
                cast->data.monadic = expr->data.diadic.right;
                expr->data.diadic.right = cast;

                cast = lalloc(sizeof(ENode));
                memclrw(cast, sizeof(ENode));
                cast->type = ETYPCON;
                cast->rtype = commontype;
                cast->data.monadic = new_expr->data.diadic.left;
                new_expr->data.diadic.left = cast;
            }
            if (right_type != commontype) {
                cast = lalloc(sizeof(ENode));
                memclrw(cast, sizeof(ENode));
                cast->type = ETYPCON;
                cast->rtype = commontype;
                cast->data.monadic = new_expr->data.diadic.right;
                new_expr->data.diadic.right = cast;
            }
            new_expr->rtype = commontype;
            break;

        case ESHL:
        case ESHR:
            promo_left = promote_type(left_type);
            promo_right = promote_type(right_type);
            if (left_type != promo_left) {
                cast = lalloc(sizeof(ENode));
                memclrw(cast, sizeof(ENode));
                cast->type = ETYPCON;
                cast->rtype = left_type;
                cast->data.monadic = expr->data.diadic.right;
                expr->data.diadic.right = cast;

                cast = lalloc(sizeof(ENode));
                memclrw(cast, sizeof(ENode));
                cast->type = ETYPCON;
                cast->rtype = promo_left;
                cast->data.monadic = new_expr->data.diadic.left;
                new_expr->data.diadic.left = cast;
            }
            if (right_type != promo_right) {
                if (new_expr->data.diadic.right->type == EINTCONST &&
                    promo_right == (Type*)&stsignedint) {
                    new_expr->data.diadic.right->rtype = (Type*)&stsignedint;
                } else {
                    cast = lalloc(sizeof(ENode));
                    memclrw(cast, sizeof(ENode));
                    cast->type = ETYPCON;
                    cast->rtype = promo_right;
                    cast->data.monadic = new_expr->data.diadic.right;
                    new_expr->data.diadic.right = cast;
                }
            }
            new_expr->rtype = promo_left;
            break;
        default:
            break;
    }
}

static void rewrite_preincdec(ENode* expr) {
    ENode* subexpr;
    Type* type;
    ENode* new_expr;

    subexpr = expr->data.monadic;
    type = expr->rtype;
    CError_ASSERT(2092, subexpr->type == EINDIRECT);

    new_expr = lalloc(sizeof(ENode));
    memclrw(new_expr, sizeof(ENode));

    if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
        new_expr->type = EFLOATCONST;
        new_expr->cost = 0;
        new_expr->rtype = type;
        new_expr->data.floatval = one_point_zero;
    } else if (IS_TYPE_POINTER(type)) {
        new_expr->type = EINTCONST;
        new_expr->cost = 0;
        new_expr->rtype = (Type*)&stunsignedlong;
        new_expr->data.intval.hi = 0;
        new_expr->data.intval.lo = TPTR_TARGET(type)->size;
    } else {
        new_expr->type = EINTCONST;
        new_expr->cost = 0;
        new_expr->rtype = type;
        new_expr->data.intval.hi = 0;
        new_expr->data.intval.lo = 1;
    }

    expr->type = (expr->type == EPREDEC) ? ESUBASS : EADDASS;
    expr->data.diadic.left = subexpr;
    expr->data.diadic.right = new_expr;
}

// clang-format off
UInt8 lvslBytes[16][16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11,
    0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13,
    0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
    0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A,
    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,
    0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C,
    0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D,
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E
};

UInt8 lvsrBytes[16][16] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
    0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D,
    0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C,
    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,
    0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A,
    0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13,
    0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};
// clang-format on

typedef union signed_vec {
    SInt8 sc[16];
    SInt16 ss[8];
    SInt32 sl[4];
} signed_vec;

static void swap_vector_elements(signed_vec* vec, Type* type) {
    union {
        SInt32 lg;
        SInt16 sh;
        SInt8 ch[4];
    } in, out;
    int i;
    TypeStruct* ts;

    ts = TYPE_STRUCT(type);
    if (IS_TYPE_VECTOR(type)) {
        switch (ts->type) {
            case TYPESTRUCT:
                switch (ts->stype) {
                    case STRUCT_VECTOR_UCHAR:
                    case STRUCT_VECTOR_SCHAR:
                    case STRUCT_VECTOR_BCHAR:
                        break;
                    case STRUCT_VECTOR_USHORT:
                    case STRUCT_VECTOR_SSHORT:
                    case STRUCT_VECTOR_BSHORT:
                    case STRUCT_VECTOR_PIXEL:
                        for (i = 0; i < 8; i++) {
                            in.sh = vec->ss[i];
                            out.ch[0] = in.ch[1];
                            out.ch[1] = in.ch[0];
                            vec->ss[i] = out.sh;
                        }
                        break;
                    case STRUCT_VECTOR_UINT:
                    case STRUCT_VECTOR_SINT:
                    case STRUCT_VECTOR_BINT:
                    case STRUCT_VECTOR_FLOAT:
                        for (i = 0; i < 4; i++) {
                            in.lg = vec->sl[i];
                            out.ch[0] = in.ch[3];
                            out.ch[1] = in.ch[2];
                            out.ch[2] = in.ch[1];
                            out.ch[3] = in.ch[0];
                            vec->sl[i] = out.lg;
                        }
                        break;
                    default:
                        CError_FATAL(2222);
                }
                break;
            default:
                CError_FATAL(2226);
        }
    }
}

Boolean canoptimizevectorconst(MWVector128* vecp, Type* type, COVCResult* result) {
    MWVector128 vec;
    SInt16 first16;
    int i;
    union {
        SInt32 lg;
        SInt16 sh;
        SInt8 ch[4];
    } conv16, conv32;
    signed_vec* v;
    SInt8* p8;
    SInt8 first8;
    SInt16* p16;
    SInt32* p32;
    char allSame;
    SInt32 first32;
    char ci;
    UInt32 l0, l1, l2, l3;

    if (IS_TYPE_VECTOR(type)) {
        vec = *vecp;
        v = (signed_vec*)&vec;
        swap_vector_elements((signed_vec*)&vec, type);

        p8 = (SInt8*)&vec;
        first8 = p8[0];
        allSame = TRUE;
        for (i = 1; allSame && i < 16; i++) {
            allSame = first8 == p8[i];
        }

        if (allSame && first8 < 16 && first8 > -17) {
            if (result) {
                result->op1 = PC_VSPLTISB;
                result->op2 = -1;
                result->arg = first8;
            }
            return TRUE;
        }

        p16 = (SInt16*)v;
        first16 = p16[0];
        allSame = TRUE;
        for (i = 1; allSame && i < 8; i++) {
            allSame = first16 == p16[i];
        }

        conv16.sh = first16;
        if (allSame && conv16.ch[0] == 0 && conv16.ch[1] < 16 && conv16.ch[1] >= 0) {
            if (result) {
                result->op1 = PC_VSPLTISH;
                result->op2 = -1;
                result->arg = conv16.ch[1];
            }
            return TRUE;
        }

        if (allSame && conv16.ch[0] == -1 && (conv16.ch[1] & 0xF0) == 0xF0) {
            if (result) {
                result->op1 = PC_VSPLTISH;
                result->op2 = -1;
                result->arg = conv16.ch[1];
            }
            return TRUE;
        }

        p32 = (SInt32*)v;
        first32 = p32[0];
        allSame = TRUE;
        for (i = 1; allSame && i < 4; i++) {
            allSame = first32 == p32[i];
        }

        conv32.lg = first32;
        if (allSame &&
            conv32.ch[0] == 0 &&
            conv32.ch[1] == 0 &&
            conv32.ch[2] == 0 &&
            conv32.ch[3] < 16 &&
            conv32.ch[3] >= 0) {
            if (result) {
                result->op1 = PC_VSPLTISW;
                result->op2 = -1;
                result->arg = conv32.ch[3];
            }
            return TRUE;
        }

        if (allSame &&
            conv32.ch[0] == -1 &&
            conv32.ch[1] == -1 &&
            conv32.ch[2] == -1 &&
            (conv32.ch[3] & 0xF0) == 0xF0) {
            if (result) {
                result->op1 = PC_VSPLTISW;
                result->op2 = -1;
                result->arg = conv32.ch[3];
            }
            return TRUE;
        }

        l0 = v->sl[0];
        l1 = v->sl[1];
        l2 = v->sl[2];
        l3 = v->sl[3];
        for (ci = 0; ci < 16; ci++) {
            UInt32* l;
            UInt32* r;

            l = (UInt32*)lvslBytes[(SInt8)ci];
            r = (UInt32*)lvsrBytes[(SInt8)ci];
            if (l0 == l[0] && l1 == l[1] && l2 == l[2] && l3 == l[3]) {
                if (result) {
                    result->op1 = -1;
                    result->op2 = PC_LVSL;
                    result->arg = ci;
                }
                return TRUE;
            }
            if (l0 == r[0] && l1 == r[1] && l2 == r[2] && l3 == r[3]) {
                if (result) {
                    result->op1 = -1;
                    result->op2 = PC_LVSR;
                    result->arg = ci;
                }
                return TRUE;
            }
        }
    }

    return FALSE;
}

static Boolean DetectCondSideAffect(ENode* expr) {
    switch (expr->type) {
        case EMUL:
        case EMULV:
        case EDIV:
        case EMODULO:
        case EADDV:
        case ESUBV:
        case EADD:
        case ESUB:
        case ESHL:
        case ESHR:
        case EAND:
        case EXOR:
        case EOR:
        case ECOMMA:
        case EPMODULO:
        case EROTL:
        case EROTR:
        case EBCLR:
        case EBTST:
        case EBSET:
            if (DetectCondSideAffect(expr->data.diadic.left)) {
                return TRUE;
            }
            return DetectCondSideAffect(expr->data.diadic.right);
        case EINDIRECT: {
            if (expr->data.monadic->type == EINDIRECT) {
                return TRUE;
            }
            if (expr->data.monadic->type == EOBJREF) {
                Object* obj = expr->data.monadic->data.objref;
                if (obj->datatype != DLOCAL && obj->datatype != DDATA) {
                    return TRUE;
                }
                if (IS_TYPE_POINTER(obj->type)) {
                    return TRUE;
                }
                if (Registers_GetVarInfo(obj)->noregister) {
                    return TRUE;
                }
                return FALSE;
            }
            return TRUE;
        }
        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
        case EFORCELOAD:
        case ETYPCON:
        case EBITFIELD:
            return DetectCondSideAffect(expr->data.monadic);
        case EINTCONST:
        case EFLOATCONST:
        case ESTRINGCONST:
        case EOBJREF:
        case EVECTOR128CONST:
            return FALSE;
        case EPOSTINC:
        case EPOSTDEC:
        case EPREINC:
        case EPREDEC:
        case ELESS:
        case EGREATER:
        case ELESSEQU:
        case EGREATEREQU:
        case EEQU:
        case ENOTEQU:
        case ELAND:
        case ELOR:
        case EASS:
        case EMULASS:
        case EDIVASS:
        case EMODASS:
        case EADDASS:
        case ESUBASS:
        case ESHLASS:
        case ESHRASS:
        case EANDASS:
        case EXORASS:
        case EORASS:
        case ECOND:
        case EFUNCCALL:
        case EFUNCCALLP:
        case EMFPOINTER:
        case ENULLCHECK:
        case EPRECOMP:
        case EDEFINE:
        case EREUSE:
        case EASSBLK:
        case ECONDASS:
            return TRUE;
        default:
            CError_FATAL(2586);
            return TRUE;
    }
}

static UInt8 WeightandSumOps(ENode* expr) {
    UInt32 score;

    switch (expr->type) {
        case ECOND:
        case ECONDASS:
            score = WeightandSumOps(expr->data.cond.cond);
            score += WeightandSumOps(expr->data.cond.expr1);
            score += WeightandSumOps(expr->data.cond.expr2);
            break;
        case EMUL:
        case EMULV:
        case EMULASS:
            score = 10;
            score += WeightandSumOps(expr->data.diadic.left);
            score += WeightandSumOps(expr->data.diadic.right);
            break;
        case EDIV:
        case EMODULO:
        case EDIVASS:
        case EMODASS:
            score = 20;
            score += WeightandSumOps(expr->data.diadic.left);
            score += WeightandSumOps(expr->data.diadic.right);
            break;
        case EADDV:
        case ESUBV:
        case EADD:
        case ESUB:
        case ESHL:
        case ESHR:
        case ELESS:
        case EGREATER:
        case ELESSEQU:
        case EGREATEREQU:
        case EEQU:
        case ENOTEQU:
        case EAND:
        case EXOR:
        case EOR:
        case ELAND:
        case ELOR:
        case EASS:
        case EADDASS:
        case ESUBASS:
        case ESHLASS:
        case ESHRASS:
        case EANDASS:
        case EXORASS:
        case EORASS:
        case ECOMMA:
        case EPMODULO:
        case EROTL:
        case EROTR:
        case EBCLR:
        case EBTST:
        case EBSET:
            score = 1;
            score += WeightandSumOps(expr->data.diadic.left);
            score += WeightandSumOps(expr->data.diadic.right);
            break;
        case EPOSTINC:
        case EPOSTDEC:
        case EPREINC:
        case EPREDEC:
        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
            score = 1;
            score += WeightandSumOps(expr->data.monadic);
            break;
        case EINDIRECT:
            if (expr->data.monadic->type == EOBJREF) {
                Object* obj = expr->data.monadic->data.objref;
                if (obj->datatype == DLOCAL && !Registers_GetVarInfo(obj)->noregister) {
                    return 0;
                }
            }
        case EFORCELOAD:
        case ETYPCON:
        case EBITFIELD:
            score = WeightandSumOps(expr->data.monadic);
            break;
        case EOBJREF:
            score = 0;
            break;
        case EINTCONST:
        case EFLOATCONST:
        case ESTRINGCONST:
        case EVECTOR128CONST:
            score = 0;
            break;
        case EFUNCCALL:
        case EFUNCCALLP:
            score = 5;
            break;
        default:
            score = 255;
    }

    if (score >= 255) {
        score = 255;
    }
    return (UInt8)score;
}

Boolean TOC_use_fsel(ENode* expr) {
    ENode* left;
    ENode* right;
    Type* rtype;
    int score1;
    int score2;

    left = expr->data.cond.expr1;
    right = expr->data.cond.expr2;
    rtype = expr->rtype;

    if (copts.soft_float && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(rtype)) {
        return FALSE;
    }
    if (!copts.peephole) {
        return FALSE;
    }
    if (!copts.gen_fsel) {
        return FALSE;
    }
    if (left->hascall) {
        return FALSE;
    }
    if (right->hascall) {
        return FALSE;
    }
    if (!IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(rtype)) {
        return FALSE;
    }
    if (!IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(left->rtype)) {
        return FALSE;
    }
    if (!IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(right->rtype)) {
        return FALSE;
    }

    if (expr->data.cond.cond->type < ELESS || expr->data.cond.cond->type > ENOTEQU) {
        return FALSE;
    }
    if (!IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->data.cond.cond->data.diadic.right->rtype)) {
        return FALSE;
    }
    if (expr->data.cond.cond->type == ELOGNOT ||
        expr->data.cond.cond->type == ELAND ||
        expr->data.cond.cond->type == ELOR) {
        return FALSE;
    }

    if (expr->type == ECONDASS) {
        if (left->type != EINDIRECT) {
            return FALSE;
        }
        if (left->data.monadic->type != EOBJREF) {
            return FALSE;
        }
    }

    if (DetectCondSideAffect(left)) {
        return FALSE;
    }
    if (DetectCondSideAffect(right)) {
        return FALSE;
    }

    if (expr->type == ECONDASS) {
        score1 = 1;
    } else {
        score1 = WeightandSumOps(left);
    }
    score2 = WeightandSumOps(right);

    if (score1 > copts.gen_fsel) {
        return FALSE;
    } else if (score2 > copts.gen_fsel) {
        return FALSE;
    } else {
        return TRUE;
    }
}

Boolean TOC_use_isel(ENode* expr, Boolean force) {
    int opt;
    ENode* left;
    ENode* right;
    Type* rtype;
    Object* obj;
    int score1;
    int score2;

    left = expr->data.cond.expr1;
    right = expr->data.cond.expr2;
    rtype = expr->rtype;
    if (force) {
        opt = 10;
    } else {
        opt = copts.gen_isel;
    }

    if (!opt) {
        return FALSE;
    }
    if (!copts.peephole) {
        return FALSE;
    }
    if (left->hascall) {
        return FALSE;
    }
    if (right->hascall) {
        return FALSE;
    }
    if (!TYPE_FITS_IN_REGISTER(rtype)) {
        return FALSE;
    }
    if (!TYPE_FITS_IN_REGISTER(left->rtype)) {
        return FALSE;
    }
    if (!TYPE_FITS_IN_REGISTER(right->rtype)) {
        return FALSE;
    }

    if (expr->data.cond.cond->type < ELESS || expr->data.cond.cond->type > ENOTEQU) {
        return FALSE;
    }
    if (TYPE_IS_8BYTES(rtype)) {
        return FALSE;
    }

    if (force) {
        Type* t = expr->data.cond.cond->data.diadic.right->rtype;
        if (!TYPE_FITS_IN_REGISTER(t)) {
            return FALSE;
        }
        if (TYPE_IS_8BYTES(t)) {
            return FALSE;
        }
    }

    if (expr->type == ECONDASS) {
        if (left->type != EINDIRECT) {
            return FALSE;
        }
        if (left->data.monadic->type != EOBJREF) {
            return FALSE;
        }
        if (force) {
            obj = left->data.monadic->data.objref;
            if (obj->datatype != DLOCAL) {
                return FALSE;
            }
            if ((Registers_GetVarInfo(obj) ? Registers_GetVarInfo(obj)->reg : 0) == 0) {
                return FALSE;
            }
            if ((RegClass)obj->u.var.info->rclass != RegClass_GPR) {
                return FALSE;
            }
        }
    }

    if (DetectCondSideAffect(left)) {
        return FALSE;
    }
    if (DetectCondSideAffect(right)) {
        return FALSE;
    }

    if (expr->type == ECONDASS) {
        score1 = 1;
    } else {
        score1 = WeightandSumOps(left);
    }
    score2 = WeightandSumOps(right);

    if (score1 > opt) {
        return FALSE;
    } else if (score2 > opt) {
        return FALSE;
    } else {
        return TRUE;
    }
}

SInt32 GetSizeSkip(ENode* expr) {
    SInt32 size;

    if (expr->type == EASS) {
        expr = expr->data.diadic.right;
    }
    if (expr->type == ETYPCON &&
        expr->data.monadic->rtype->size < expr->rtype->size &&
        !IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->data.monadic->rtype) &&
        is_unsigned(expr->data.monadic->rtype) == is_unsigned(expr->rtype)) {
        size = expr->data.monadic->rtype->size;
    } else {
        size = expr->rtype->size;
    }
    return size;
}

void Optimize64bitMath(ENode* expr) {
    ENode* left;
    ENode* right;
    SInt32 leftsize;
    SInt32 rightsize;
    SInt32 totalsize;
    int unsignedflag;
    ENode* operand;

    CError_ASSERT(2953, TYPE_IS_8BYTES(expr->rtype));

    left = expr->data.diadic.left;
    right = expr->data.diadic.right;
    if (left->type == EASS) {
        operand = left->data.diadic.right;
    } else {
        operand = left;
    }
    if (operand->type == ETYPCON &&
        operand->data.monadic->rtype->size < operand->rtype->size &&
        !IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(operand->data.monadic->rtype) &&
        is_unsigned(operand->data.monadic->rtype) == is_unsigned(operand->rtype)) {
        leftsize = operand->data.monadic->rtype->size;
    } else {
        leftsize = operand->rtype->size;
    }
    if (right->type == EASS) {
        operand = right->data.diadic.right;
    } else {
        operand = right;
    }
    if (operand->type == ETYPCON &&
        operand->data.monadic->rtype->size < operand->rtype->size &&
        !IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(operand->data.monadic->rtype) &&
        is_unsigned(operand->data.monadic->rtype) == is_unsigned(operand->rtype)) {
        rightsize = operand->data.monadic->rtype->size;
    } else {
        rightsize = operand->rtype->size;
    }
    totalsize = leftsize + rightsize;
    unsignedflag = (is_unsigned(left->rtype) && is_unsigned(right->rtype)) ? 1 : 0;

    switch (totalsize) {
        case 2:
        case 3:
        case 4:
            if (unsignedflag) {
                left->rtype = (Type*)&stunsignedint;
                right->rtype = (Type*)&stunsignedint;
            } else {
                left->rtype = (Type*)&stsignedint;
                right->rtype = (Type*)&stsignedint;
            }
            break;
        case 5:
        case 6:
        case 8:
        case 9:
        case 10:
        case 12:
            if (expr->type != ESUB || leftsize >= rightsize) {
                if (leftsize < 4) {
                    if (unsignedflag) {
                        left->rtype = (Type*)&stunsignedint;
                    } else {
                        left->rtype = (Type*)&stsignedint;
                    }
                } else if (left->type == ETYPCON) {
                    int outerUnsigned = is_unsigned(expr->data.diadic.left->rtype);
                    int innerUnsigned = is_unsigned(left->data.monadic->rtype);
                    if (!IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(left->data.monadic->rtype) &&
                        innerUnsigned == outerUnsigned) {
                        expr->data.diadic.left = left->data.monadic;
                    }
                }
                if (rightsize < 4) {
                    if (unsignedflag) {
                        right->rtype = (Type*)&stunsignedint;
                    } else {
                        right->rtype = (Type*)&stsignedint;
                    }
                } else if (right->type == ETYPCON) {
                    int outerUnsigned = is_unsigned(expr->data.diadic.right->rtype);
                    int innerUnsigned = is_unsigned(right->data.monadic->rtype);
                    if (!IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(right->data.monadic->rtype) &&
                        innerUnsigned == outerUnsigned) {
                        expr->data.diadic.right = right->data.monadic;
                    }
                }
            }
            break;
        case 16:
            break;
        default:
            CError_FATAL(3059);
    }
}

static void expandTOCexpression(ENode* expr, Type* type, int ignored) {
    Object* funcCopy;
    ENode* cond;
    ENode* tmpexpr;
    Object* obj;
    ENode* newexpr;
    ENodeList* list;

    expr->ignored = ignored;
    switch (expr->type) {
        case EINTCONST:
            expr->hascall = FALSE;
            break;
        case EFLOATCONST:
            if (!copts.soft_float) {
                obj = createfloatconstant(expr->rtype, &expr->data.floatval);
                uses_globals = TRUE;
                if (is_16bitdata(obj)) {
                    tmpexpr = lalloc(sizeof(ENode));
                    memclrw(tmpexpr, sizeof(ENode));
                    tmpexpr->type = EOBJREF;
                    tmpexpr->cost = 0;
                    tmpexpr->data.objref = obj;
                    tmpexpr->rtype = CDecl_NewPointerType(expr->rtype);

                    expr->type = EINDIRECT;
                    expr->cost = 1;
                    expr->data.monadic = tmpexpr;
                } else {
                    tmpexpr = lalloc(sizeof(ENode));
                    memclrw(tmpexpr, sizeof(ENode));
                    tmpexpr->type = EOBJREF;
                    tmpexpr->cost = 0;
                    tmpexpr->data.objref = obj;
                    tmpexpr->rtype = expr->rtype;

                    expr->type = EINDIRECT;
                    expr->cost = 1;
                    expr->data.monadic = tmpexpr;
                    createIndirect(obj, FALSE, TRUE);
                }
            }
            expr->hascall = FALSE;
            break;
        case EVECTOR128CONST:
            if (!canoptimizevectorconst(&expr->data.vector128val, expr->rtype, NULL)) {
                uses_globals = TRUE;
                RewriteVectorConst(expr);
                expandTOCexpression(expr, NULL, FALSE);
            }
            break;
        case ESTRINGCONST:
            uses_globals = TRUE;
            CInit_RewriteString(expr, TRUE);
            expandTOCexpression(expr, NULL, FALSE);
            break;
        case EOBJREF:
            obj = expr->data.objref;
            CError_ASSERT(3287, obj->datatype != DALIAS);
            DWARF1_AddToFunctionRefList(obj);
            if (obj->datatype == DFUNC || obj->datatype == DVFUNC) {
                uses_globals = TRUE;
            }
            if (obj->datatype == DDATA && !is_16bitdata(obj)) {
                uses_globals = TRUE;
                createIndirect(obj, FALSE, TRUE);
            }
            expr->hascall = FALSE;
            break;
        case ECONDASS:
            ignored = expr->ignored = FALSE;
        case ECOND:
            if (!ENODE_IS_RANGE(expr->data.cond.cond, ELESS, ENOTEQU)) {
                expr->data.cond.cond = comparewithzero(expr->data.cond.cond);
            }
            expandTOCexpression(expr->data.cond.expr1, NULL, ignored);
            expandTOCexpression(expr->data.cond.expr2, NULL, ignored);
            if (TOC_use_fsel(expr)) {
                cond = expr->data.cond.cond;
                if (ENODE_IS(cond->data.diadic.right, EFLOATCONST) &&
                    CMach_FloatIsZero(cond->data.diadic.right->data.floatval)) {
                    expandTOCexpression(cond->data.diadic.left, NULL, FALSE);
                } else if (ENODE_IS(cond->data.diadic.left, EFLOATCONST) &&
                           CMach_FloatIsZero(cond->data.diadic.left->data.floatval)) {
                    expandTOCexpression(cond->data.diadic.right, NULL, FALSE);
                } else {
                    expandTOCexpression(expr->data.cond.cond, NULL, FALSE);
                }
            } else {
                expandTOCexpression(expr->data.cond.cond, NULL, FALSE);
            }
            expr->hascall = expr->data.cond.cond->hascall | expr->data.cond.expr1->hascall |
                expr->data.cond.expr2->hascall;
            break;
        case EFUNCCALL:
        case EFUNCCALLP:
            if (is_intrinsic_function_call(expr)) {
                expr->hascall = FALSE;
                if ((Intrinsics)expr->data.funccall.funcref->data.objref->u.func.u.intrinsicid ==
                    Intrinsic_alloca) {
                    if (copts.altivec_model) {
                        update_frame_align(16);
                    }
                    dynamic_stack = TRUE;
                    requires_frame = TRUE;
                }
                if ((Intrinsics)expr->data.funccall.funcref->data.objref->u.func.u.intrinsicid ==
                        Intrinsic_rtos_change_stack ||
                    (Intrinsics)expr->data.funccall.funcref->data.objref->u.func.u.intrinsicid ==
                        Intrinsic_rtos_pop_stack) {
                    dynamic_stack = TRUE;
                    requires_frame = TRUE;
                } else if ((Intrinsics)expr->data.funccall.funcref->data.objref->u.func.u
                               .intrinsicid == Intrinsic_strcpy) {
                    if (expr->data.funccall.args->next->node->type == ESTRINGCONST) {
                        rewritestrcpy(expr);
                    } else {
                        requires_frame = TRUE;
                        makes_call = TRUE;
                        expr->hascall = TRUE;
                    }
                } else if ((Intrinsics)expr->data.funccall.funcref->data.objref->u.func.u
                               .intrinsicid == Intrinsic_memcpy) {
                    if (expr->data.funccall.args->next->next->node->type != EINTCONST) {
                        requires_frame = TRUE;
                        makes_call = TRUE;
                        expr->hascall = TRUE;
                    }
                }
            } else {
                requires_frame = TRUE;
                makes_call = TRUE;
                expr->hascall = TRUE;
            }

            if (disables_optimizer(expr)) {
                disable_optimizer |= 1;
                if (copts.disable_registers) {
                    disable_optimizer |= 2;
                }
            }

            tmpexpr = expr->data.funccall.funcref;
            if (ENODE_IS(tmpexpr, EINDIRECT) && IS_TYPE_FUNC(tmpexpr->rtype)) {
                *tmpexpr = *tmpexpr->data.monadic;
            }
            if (ENODE_IS(tmpexpr, ETYPCON) &&
                ENODE_IS(tmpexpr->data.monadic, EOBJREF) &&
                tmpexpr->data.monadic->data.objref->datatype == DFUNC) {
                *tmpexpr = *tmpexpr->data.monadic;
            }

            if (ENODE_IS(expr->data.funccall.funcref, EOBJREF)) {
                expr->data.funccall.funcref->hascall = FALSE;
                if (expr->data.funccall.funcref->data.objref->datatype == DVFUNC &&
                    (expr->data.funccall.funcref->flags & ENODE_FLAG_NONVIRTUAL)) {
                    funcCopy = galloc(sizeof(Object));
                    *funcCopy = *expr->data.funccall.funcref->data.objref;
                    funcCopy->datatype = DFUNC;
                    expr->data.funccall.funcref->data.objref = funcCopy;
                }
            } else {
                expandTOCexpression(expr->data.funccall.funcref, NULL, FALSE);
            }

            for (list = expr->data.funccall.args; list; list = list->next) {
                expandTOCexpression(list->node, NULL, FALSE);
            }

            if (expr->hascall) {
                estimate_func_param_size(expr);
            }

            if (is_intrinsic_function_call(expr)) {
                for (list = expr->data.funccall.args; list; list = list->next) {
                    expr->hascall |= list->node->hascall;
                }
            }

            if (Type_RequiresMemoryReturn(TYPE_FUNC(expr->data.funccall.functype)->functype)) {
                rewritefunctioncallreturningstruct(expr);
            }

            break;
        case ECOMMA:
            expandTOCexpression(expr->data.diadic.left, NULL, TRUE);
            expandTOCexpression(expr->data.diadic.right, NULL, ignored);
            expr->hascall = expr->data.diadic.left->hascall | expr->data.diadic.right->hascall;
            break;
        case ELAND:
        case ELOR:
            if (!ENODE_IS(expr->data.diadic.left, ELOGNOT) &&
                !ENODE_IS2(expr->data.diadic.left, ELAND, ELOR) &&
                !ENODE_IS_RANGE(expr->data.diadic.left, ELESS, ENOTEQU)) {
                expr->data.diadic.left = comparewithzero(expr->data.diadic.left);
            }
            if (!ENODE_IS(expr->data.diadic.right, ELOGNOT) &&
                !ENODE_IS2(expr->data.diadic.right, ELAND, ELOR) &&
                !ENODE_IS_RANGE(expr->data.diadic.right, ELESS, ENOTEQU)) {
                expr->data.diadic.right = comparewithzero(expr->data.diadic.right);
            }
            expandTOCexpression(expr->data.diadic.left, NULL, FALSE);
            expandTOCexpression(expr->data.diadic.right, NULL, FALSE);
            expr->hascall = expr->data.diadic.left->hascall | expr->data.diadic.right->hascall;
            break;
        case EDIVASS:
            if (!IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype) &&
                IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->data.diadic.right->rtype)) {
                uses_globals = TRUE;
            }
            rewrite_opassign(expr, EDIV);
            goto opassign_common;
        case EMULASS:
            if (!IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype) &&
                IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->data.diadic.right->rtype)) {
                uses_globals = TRUE;
            }
            rewrite_opassign(expr, EMUL);
            goto opassign_common;
        case EADDASS:
            if (!IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype) &&
                IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->data.diadic.right->rtype)) {
                uses_globals = TRUE;
            }
            rewrite_opassign(expr, EADD);
            goto opassign_common;
        case ESUBASS:
            if (!IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype) &&
                IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->data.diadic.right->rtype)) {
                uses_globals = TRUE;
            }
            rewrite_opassign(expr, ESUB);
            goto opassign_common;
        case EMODASS:
            if (!IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype) &&
                IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->data.diadic.right->rtype)) {
                uses_globals = TRUE;
            }
            rewrite_opassign(expr, EMODULO);
            goto opassign_common;
        case ESHLASS:
            rewrite_opassign(expr, ESHL);
            goto opassign_common;
        case ESHRASS:
            rewrite_opassign(expr, ESHR);
            goto opassign_common;
        case EANDASS:
            rewrite_opassign(expr, EAND);
            goto opassign_common;
        case EXORASS:
            rewrite_opassign(expr, EXOR);
            goto opassign_common;
        case EORASS:
            rewrite_opassign(expr, EOR);
            goto opassign_common;
        case EASS:
            if (ENODE_IS(expr->data.diadic.left, EINDIRECT)) {
                expr->data.diadic.left = expr->data.diadic.left->data.monadic;
            }
opassign_common:
            expandTOCexpression(expr->data.diadic.left, NULL, FALSE);
            expandTOCexpression(expr->data.diadic.right, NULL, FALSE);
            expr->hascall = expr->data.diadic.left->hascall | expr->data.diadic.right->hascall;
            break;
        case EEQU:
        case ENOTEQU:
            if (ENODE_IS(expr->data.diadic.right, EINTCONST) &&
                expr->data.diadic.right->data.intval.lo == 0 &&
                expr->data.diadic.right->data.intval.hi == 0) {
                for (tmpexpr = expr->data.diadic.left; ENODE_IS2(tmpexpr, EFORCELOAD, ETYPCON);
                     tmpexpr = tmpexpr->data.monadic) {
                    if (!TYPE_FITS_IN_REGISTER(tmpexpr->rtype)) {
                        break;
                    }
                }
                if (ENODE_IS(tmpexpr, ELOGNOT) &&
                    TYPE_FITS_IN_REGISTER(tmpexpr->data.monadic->rtype)) {
                    if (ENODE_IS(expr, EEQU)) {
                        expr->type = ENOTEQU;
                    } else {
                        expr->type = EEQU;
                    }
                    expr->data.diadic.left = tmpexpr->data.monadic;
                    expandTOCexpression(expr, NULL, FALSE);
                    break;
                }
                if (ENODE_IS(tmpexpr, EEQU)) {
                    if (ENODE_IS(expr, EEQU)) {
                        tmpexpr->type = ENOTEQU;
                    }
                    *expr = *tmpexpr;
                    expandTOCexpression(expr, NULL, FALSE);
                    break;
                }
                if (ENODE_IS(tmpexpr, ENOTEQU)) {
                    if (ENODE_IS(expr, EEQU)) {
                        tmpexpr->type = EEQU;
                    }
                    *expr = *tmpexpr;
                    expandTOCexpression(expr, NULL, FALSE);
                    break;
                }
                if (ENODE_IS(tmpexpr, ECOND)) {
                    newexpr = COND_to_COMPARE(
                        tmpexpr->data.cond.cond, tmpexpr->data.cond.expr1, tmpexpr->data.cond.expr2
                    );
                    if (newexpr) {
                        *tmpexpr = *newexpr;
                        expandTOCexpression(expr, NULL, FALSE);
                        break;
                    }
                }
            }
        case ELESS:
        case EGREATER:
        case ELESSEQU:
        case EGREATEREQU:
            if (IS_TYPE_INT_OR_ENUM(expr->data.diadic.right->rtype) &&
                IS_TYPE_INT_OR_ENUM(expr->data.diadic.left->rtype) &&
                ENODE_IS(expr->data.diadic.left, EINTCONST) &&
                !ENODE_IS(expr->data.diadic.right, EINTCONST)) {
                tmpexpr = expr->data.diadic.left;
                expr->data.diadic.left = expr->data.diadic.right;
                expr->data.diadic.right = tmpexpr;
                switch (expr->type) {
                    case ELESS:
                        expr->type = EGREATER;
                        break;
                    case EGREATER:
                        expr->type = ELESS;
                        break;
                    case ELESSEQU:
                        expr->type = EGREATEREQU;
                        break;
                    case EGREATEREQU:
                        expr->type = ELESSEQU;
                        break;
                    default:
                        break;
                }
            }
        case EDIV:
            if (ENODE_IS(expr, EDIV) &&
                ENODE_IS(expr->data.diadic.right, EFLOATCONST) &&
                CMach_FloatIsPowerOf2(expr->data.diadic.right->data.floatval)) {
                expr->type = EMUL;
                expr->data.diadic.right->data.floatval =
                    CMach_FloatReciprocal(expr->data.diadic.right->data.floatval);
            }
        case EMODULO:
        case ESHL:
        case ESHR:
            expandTOCexpression(expr->data.diadic.left, NULL, FALSE);
            expandTOCexpression(expr->data.diadic.right, NULL, FALSE);
            expr->hascall = expr->data.diadic.left->hascall | expr->data.diadic.right->hascall;
            if (TYPE_IS_8BYTES(expr->rtype)) {
                if (ENODE_IS2(expr, ESHL, ESHR) && !ENODE_IS(expr->data.diadic.right, EINTCONST)) {
                    expr->hascall = TRUE;
                    requires_frame = TRUE;
                    makes_call = TRUE;
                }
                if (ENODE_IS2(expr, EDIV, EMODULO)) {
                    if (ENODE_IS(expr->data.diadic.right, EINTCONST)) {
                        if (I8_log2n(
                                ((long long)expr->data.diadic.right->data.intval.hi << 32) +
                                expr->data.diadic.right->data.intval.lo
                            ) <= 0) {
                            expr->hascall = TRUE;
                            requires_frame = TRUE;
                            makes_call = TRUE;
                        }
                    } else {
                        expr->hascall = TRUE;
                        requires_frame = TRUE;
                        makes_call = TRUE;
                    }
                }
            }
            if (copts.soft_float &&
                IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype) &&
                ENODE_IS2(expr, EDIV, EMUL)) {
                expr->hascall = TRUE;
                requires_frame = TRUE;
                makes_call = TRUE;
            }
            if ((copts.soft_float &&
                 IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->data.diadic.right->rtype)) ||
                (copts.soft_float &&
                 IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->data.diadic.left->rtype))) {
                if (ENODE_IS(expr, ELESS) ||
                    ENODE_IS(expr, EGREATER) ||
                    ENODE_IS(expr, ELESSEQU) ||
                    ENODE_IS(expr, EGREATEREQU) ||
                    ENODE_IS(expr, EEQU) ||
                    ENODE_IS(expr, ENOTEQU)) {
                    expr->hascall = TRUE;
                    requires_frame = TRUE;
                    makes_call = TRUE;
                }
            }
            break;
        case ESUB:
            if (ENODE_IS(expr->data.diadic.right, EINTCONST)) {
                expr->type = EADD;
                expr->data.diadic.right->data.intval =
                    CInt64_Neg(expr->data.diadic.right->data.intval);
            }
        case EMUL:
        case EADD:
        case EAND:
        case EXOR:
        case EOR:
            expandTOCexpression(expr->data.diadic.left, type, FALSE);
            expandTOCexpression(expr->data.diadic.right, type, FALSE);
            expr->hascall = expr->data.diadic.left->hascall | expr->data.diadic.right->hascall;
            if (ENODE_IS3(expr, EMUL, EADD, ESUB) && TYPE_IS_8BYTES(expr->rtype)) {
                Optimize64bitMath(expr);
            }
            if (type) {
                tmpexpr = expr->data.diadic.left;
                if (ENODE_IS(tmpexpr, ETYPCON) &&
                    IS_TYPE_INT_OR_ENUM(tmpexpr->rtype) &&
                    IS_TYPE_INT_OR_ENUM(tmpexpr->data.monadic->rtype) &&
                    tmpexpr->data.monadic->rtype->size >= type->size &&
                    !TYPE_IS_8BYTES(tmpexpr->data.monadic->rtype)) {
                    expr->data.diadic.left = tmpexpr->data.monadic;
                }

                tmpexpr = expr->data.diadic.right;
                if (ENODE_IS(tmpexpr, ETYPCON) &&
                    IS_TYPE_INT_OR_ENUM(tmpexpr->rtype) &&
                    IS_TYPE_INT_OR_ENUM(tmpexpr->data.monadic->rtype) &&
                    tmpexpr->data.monadic->rtype->size >= type->size &&
                    !TYPE_IS_8BYTES(tmpexpr->data.monadic->rtype)) {
                    expr->data.diadic.right = tmpexpr->data.monadic;
                }

                expr->rtype = type;
            }
            if (copts.soft_float &&
                IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype) &&
                ENODE_IS3(expr, EMUL, EADD, ESUB)) {
                expr->hascall = TRUE;
                requires_frame = TRUE;
                makes_call = TRUE;
            }
            break;

        case ETYPCON:
            tmpexpr = expr->data.monadic;
            if ((IS_TYPE_INT_OR_ENUM(expr->rtype) && expr->rtype->size < 4) &&
                IS_TYPE_INT_OR_ENUM(tmpexpr->rtype) &&
                !TYPE_IS_8BYTES(tmpexpr->rtype)) {
                expandTOCexpression(tmpexpr, expr->rtype, FALSE);
            } else {
                expandTOCexpression(tmpexpr, NULL, expr->rtype->type == TYPEVOID);
            }

            if (ENODE_IS(tmpexpr, EINTCONST) && IS_TYPE_INT_OR_ENUM(expr->rtype)) {
                if (is_unsigned(expr->rtype)) {
                    switch (expr->rtype->size) {
                        case 4:
                            CInt64_ConvertUInt32(&tmpexpr->data.intval);
                            break;
                        case 2:
                            CInt64_ConvertUInt16(&tmpexpr->data.intval);
                            break;
                        case 1:
                            CInt64_ConvertUInt8(&tmpexpr->data.intval);
                            break;
                        case 8:
                            break;
                        default:
                            CError_FATAL(3816);
                    }
                } else {
                    switch (expr->rtype->size) {
                        case 4:
                            CInt64_ConvertInt32(&tmpexpr->data.intval);
                            break;
                        case 2:
                            CInt64_ConvertInt16(&tmpexpr->data.intval);
                            break;
                        case 1:
                            CInt64_ConvertInt8(&tmpexpr->data.intval);
                            break;
                        case 8:
                            break;
                        default:
                            CError_FATAL(3833);
                    }
                }
                *expr = *tmpexpr;
                break;
            }

            expr->hascall = tmpexpr->hascall;
            if (IS_TYPE_INT_OR_ENUM(tmpexpr->rtype) &&
                IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype)) {
                uses_globals = TRUE;
            }

            if ((TYPE_IS_8BYTES(tmpexpr->rtype) && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype)) ||
                (TYPE_IS_8BYTES(expr->rtype) && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(tmpexpr->rtype))) {
                uses_globals = TRUE;
                expr->hascall = TRUE;
                requires_frame = TRUE;
                makes_call = TRUE;
            }

            if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(tmpexpr->rtype)) {
                if (is_unsigned(expr->rtype) && expr->rtype->size == 4) {
                    expr->hascall = TRUE;
                    requires_frame = TRUE;
                    makes_call = TRUE;
                } else {
                    uses_globals = TRUE;
                }
            }

            if ((copts.soft_float && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(tmpexpr->rtype)) ||
                (copts.soft_float && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype))) {
                expr->hascall = TRUE;
                requires_frame = TRUE;
                makes_call = TRUE;
            }

            if (IS_TYPE_VECTOR(expr->rtype) && !IS_TYPE_VECTOR(tmpexpr->rtype)) {
                PPCError_Error(PPCERR_ILLEGAL_VECTOR_INIT_OR_CAST);
            }
            break;

        case EPOSTINC:
            if (!expr->ignored) {
                if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->data.monadic->rtype) &&
                    is_unsigned(expr->rtype)) {
                    uses_globals = TRUE;
                }
                expandTOCexpression(expr->data.monadic, NULL, FALSE);
                expr->hascall = expr->data.monadic->hascall;
                if (copts.soft_float && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype)) {
                    expr->hascall = TRUE;
                    requires_frame = TRUE;
                    makes_call = TRUE;
                }
                break;
            }
            expr->type = EPREINC;
        case EPREINC:
            if (copts.soft_float && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype)) {
                expr->hascall = TRUE;
                requires_frame = TRUE;
                makes_call = TRUE;
            }
            rewrite_preincdec(expr);
            rewrite_opassign(expr, EADD);
            goto opassign_common;

        case EPOSTDEC:
            if (!expr->ignored) {
                if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->data.monadic->rtype) &&
                    is_unsigned(expr->rtype)) {
                    uses_globals = TRUE;
                }
                expandTOCexpression(expr->data.monadic, NULL, FALSE);
                expr->hascall = expr->data.monadic->hascall;
                if (copts.soft_float && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype)) {
                    expr->hascall = TRUE;
                    requires_frame = TRUE;
                    makes_call = TRUE;
                }
                break;
            }
            expr->type = EPREDEC;
        case EPREDEC:
            if (copts.soft_float && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(expr->rtype)) {
                expr->hascall = TRUE;
                requires_frame = TRUE;
                makes_call = TRUE;
            }
            rewrite_preincdec(expr);
            rewrite_opassign(expr, ESUB);
            goto opassign_common;

        case ELOGNOT:
            if (!ENODE_IS(expr->data.monadic, ELOGNOT) &&
                !ENODE_IS2(expr->data.monadic, ELAND, ELOR) &&
                !ENODE_IS_RANGE(expr->data.monadic, ELESS, ENOTEQU)) {
                expr->data.monadic = comparewithzero(expr->data.monadic);
            }
            type = NULL;
        case EMONMIN:
        case EBINNOT:
            expandTOCexpression(tmpexpr = expr->data.monadic, type, FALSE);
            expr->hascall = tmpexpr->hascall;
            if (type && ENODE_IS(tmpexpr = expr->data.monadic, ETYPCON)) {
                if (IS_TYPE_INT_OR_ENUM(tmpexpr->rtype) &&
                    IS_TYPE_INT_OR_ENUM(tmpexpr->data.monadic->rtype)) {
                    if (tmpexpr->data.monadic->rtype->size >= type->size) {
                        expr->data.monadic = tmpexpr->data.monadic;
                        expr->rtype = type;
                    }
                }
            }
            break;

        case EINDIRECT:
        case EFORCELOAD:
        case EBITFIELD:
            expandTOCexpression(newexpr = expr->data.monadic, NULL, FALSE);
            expr->hascall = newexpr->hascall;
            break;

        case EDEFINE:
            expandTOCexpression(tmpexpr = expr->data.monadic, NULL, FALSE);
            expr->hascall = tmpexpr->hascall;
            break;

        case EREUSE:
            expr->hascall = expr->data.monadic->hascall;
            break;

        case ENULLCHECK:
            expandTOCexpression(expr->data.diadic.left, NULL, FALSE);
            expandTOCexpression(expr->data.diadic.right, NULL, FALSE);
            expr->hascall = expr->data.diadic.left->hascall | expr->data.diadic.right->hascall;
            break;

        case EPRECOMP:
            expr->hascall = FALSE;
            break;

        case ELABEL:
            obj = createcodelabel(expr->data.label);
            newexpr = lalloc(sizeof(ENode));
            memclrw(newexpr, sizeof(ENode));
            newexpr->type = EOBJREF;
            newexpr->cost = 0;
            newexpr->data.objref = obj;
            newexpr->rtype = CDecl_NewPointerType(obj->type);

            expr->type = EINDIRECT;
            expr->cost = 1;
            expr->data.monadic = newexpr;
            expr->hascall = FALSE;
            break;
        default:
            break;
    }
}

static void checkexceptionreferences(ExceptionAction* action) {
    for (; action; action = action->prev) {
        switch (action->type) {
            case EAT_DESTROYLOCAL:
                referenceexception(action->data.destroy_local.local);
                break;
            case EAT_DESTROYLOCALCOND:
                referenceexception(action->data.destroy_local_cond.local);
                referenceexception(action->data.destroy_local_cond.cond);
                break;
            case EAT_DESTROYLOCALOFFSET:
                referenceexception(action->data.destroy_local_offset.local);
                break;
            case EAT_DESTROYLOCALPOINTER:
                referenceexception(action->data.destroy_local_pointer.pointer);
                break;
            case EAT_DESTROYLOCALARRAY:
                referenceexception(action->data.destroy_local_array.localarray);
                break;
            case EAT_DESTROYBASE:
                referenceexception(action->data.destroy_base.objectptr);
                break;
            case EAT_DESTROYPARTIALARRAY:
                referenceexception(action->data.destroy_partial_array.arraypointer);
                referenceexception(action->data.destroy_partial_array.arraycounter);
                referenceexception(action->data.destroy_partial_array.element_size);
                break;
            case EAT_DESTROYMEMBER:
                referenceexception(action->data.destroy_member.objectptr);
                break;
            case EAT_DESTROYMEMBERCOND:
                referenceexception(action->data.destroy_member_cond.objectptr);
                referenceexception(action->data.destroy_member_cond.cond);
                break;
            case EAT_DESTROYMEMBERARRAY:
                referenceexception(action->data.destroy_member_array.objectptr);
                break;
            case EAT_DELETEPOINTER:
            case EAT_DELETELOCALPOINTER:
                referenceexception(action->data.delete_pointer.pointerobject);
                break;
            case EAT_DELETEPOINTERCOND:
                referenceexception(action->data.delete_pointer_cond.pointerobject);
                referenceexception(action->data.delete_pointer_cond.cond);
                break;
            case EAT_CATCHBLOCK:
                referenceexception(action->data.catch_block.catch_object);
                referenceexception(action->data.catch_block.catch_info_object);
                break;
            case EAT_ACTIVECATCHBLOCK:
                referenceexception(action->data.active_catch_block.catch_info_object);
                break;
            default:
                break;
        }
    }
}

void expandTOCreferences(Statement** stmts) {
    Statement* stmt;
    IAOperand* op;
    int i;
    InlineAsm* ia;
    VarInfo* vi;

    codelabellist = NULL;
    exceptionlist = NULL;

    for (stmt = *stmts; stmt; stmt = stmt->next) {
        curstmtvalue = stmt->value;
        if (stmt->flags & StmtFlag_KeepLabel) {
            has_catch_blocks = TRUE;
            dynamic_stack = TRUE;
            requires_frame = TRUE;
        }

        switch (stmt->type) {
            case ST_EXPRESSION:
                expandTOCexpression(stmt->expr, NULL, TRUE);
                if (stmt->expr->type == ETYPCON && IS_TYPE_VOID(stmt->expr->rtype)) {
                    stmt->expr = stmt->expr->data.monadic;
                }
                break;
            case ST_GOTOEXPR:
                expandTOCexpression(stmt->expr, NULL, FALSE);
                break;
            case ST_IFGOTO:
            case ST_IFNGOTO:
                if (stmt->expr->type < ELESS || stmt->expr->type > ENOTEQU) {
                    stmt->expr = comparewithzero(stmt->expr);
                }
                expandTOCexpression(stmt->expr, NULL, FALSE);
                break;
            case ST_RETURN:
                if (!stmt->expr) {
                    continue;
                }
                expandTOCexpression(
                    stmt->expr,
                    NULL,
                    (IS_TYPE_ARRAY(stmt->expr->rtype) ||
                     IS_TYPE_NONVECTOR_STRUCT(stmt->expr->rtype) ||
                     IS_TYPE_CLASS(stmt->expr->rtype) ||
                     IS_TYPE_12BYTES_MEMBERPOINTER(stmt->expr->rtype)) &&
                        !((IS_TYPE_STRUCT(stmt->expr->rtype) || IS_TYPE_CLASS(stmt->expr->rtype)) &&
                          !Type_RequiresMemoryReturn(stmt->expr->rtype))
                );
                break;
            case ST_SWITCH:
                uses_globals = TRUE;
                expandTOCexpression(stmt->expr, NULL, FALSE);
                break;
            case ST_ENDCATCHDTOR:
                requires_frame = TRUE;
                makes_call = TRUE;
                break;
            case ST_ASM:
                if ((ia = (InlineAsm*)stmt->expr)) {
                    if (ia->flags & IAFlag_Directive) {
                        if (ia->opcode == IADirective_FrFree) {
                            requires_frame = TRUE;
                        }
                    } else {
                        for (i = 0, op = ia->args; i < ia->argcount; i++, op++) {
                            if (op->type == IAOpnd_Reg) {
                                if (!op->u.reg.object) {
                                    if (op->u.reg.effect & EffectWrite) {
                                        asm_used_register(op->u.reg.rclass, op->u.reg.num);
                                        if ((RegClass)op->u.reg.rclass == RegClass_GPR &&
                                            op->u.reg.num == 1) {
                                            dynamic_stack = TRUE;
                                            requires_frame = TRUE;
                                        }
                                    }
                                } else if ((vi = Registers_GetVarInfo(op->u.reg.object))) {
                                    vi->flags |= VarInfoFlag_AsmNamed;
                                    vi->used = TRUE;
                                    vi->usage++;
                                }
                            } else if (op->type == IAOpnd_GlobalObj ||
                                       op->type == IAOpnd_LocalObj) {
                                switch (op->u.obj.obj->datatype) {
                                    case DDATA:
                                        uses_globals = TRUE;
                                    case DLOCAL:
                                        if ((vi = Registers_GetVarInfo(op->u.obj.obj))) {
                                            vi->used = TRUE;
                                            vi->usage++;
                                        }
                                        break;
                                }
                            }
                        }

                        if (ia->flags & IAFlag_Call) {
                            makes_call = TRUE;
                        }
                    }
                }
                break;
            default:
                break;
        }

        checkexceptionreferences(stmt->dobjstack);
    }
}

Boolean needdescriptor(void) {
    return FALSE;
}

Object* createstaticinitobject(void) {
    Str255 fname;
    char buf[100];
    Object* obj;
    TypeFunc* tfunc;
    char* p;

    COS_FileGetFSSpecInfo(&cparamblkptr->sourcefile, NULL, NULL, fname);
    sprintf(buf, "__sinit_%*.*s", -fname[0], fname[0], &fname[1]);
    for (p = &buf[1]; *p; p++) {
        if (*p == '.') {
            *p = '_';
        }
    }
    tfunc = galloc(sizeof(TypeFunc));
    memclrw(tfunc, sizeof(TypeFunc));
    tfunc->type = TYPEFUNC;
    tfunc->functype = &stvoid;
    tfunc->args = NULL;
    tfunc->flags = FUNC_DEFINED;
    obj = galloc(sizeof(Object));
    memclrw(obj, sizeof(Object));
    obj->otype = OT_OBJECT;
    obj->type = (Type*)tfunc;
    obj->name = GetHashNameNodeExport(buf);
    obj->sclass = TK_STATIC;
    obj->datatype = DFUNC;
    PPC_EABI_SetSection(obj, 4, TRUE, TRUE);
    MakeSymbolTableEntry(obj);
    return obj;
}

static inline void estimate_func_param_size(ENode* node) {
    SInt32 work;
    ENodeList* list;
    SInt32 align;

    work = 0;
    for (list = node->data.funccall.args; list; list = list->next) {
        align = ~7 & (CMach_ArgumentAlignment(list->node->rtype) + 7);
        work += ~(align - 1) & (list->node->rtype->size + align - 1);
    }

    estimate_out_param_size(work);
}
