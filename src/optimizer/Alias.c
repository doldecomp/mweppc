#include "compiler/Alias.h"
#include "compiler/BitVectors.h"
#include "compiler/CClass.h"
#include "compiler/CError.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CopyPropagation.h"
#include "compiler/CParser.h"
#include "compiler/objects.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/types.h"
#include "compiler/UseDefChains.h"
#include "compiler/ValueNumbering.h"

void add_alias_member(Alias* parent, Alias* child);

static Alias* aliases;
static Alias* set_aliases;
static int n_aliases;
static int n_gathered_aliases;
static Alias* alias_hash[997];
Alias* worst_case_ptr;
#if VERSION >= VERSION_GC_2_5
Alias* worst_case_func;
#endif
Object worst_case_obj;

static TypePointer worst_case_memory_type = {TYPEARRAY, 0xFFFFFF, TYPE(&stchar)};
#if VERSION >= VERSION_GC_2_5
static TypePointer virtual_object_type = {TYPEARRAY, 0xFFFFFF, TYPE(&stchar)};
#endif

static Boolean is_safe_const(Object* obj) {
    Type* type;

    type = obj->type;
#if VERSION < VERSION_GC_2_5
    if (IS_TYPE_POINTER(type) && !IS_TYPE_ARRAY(type) && IS_TYPE_ARRAY(TPTR_TARGET(type))) {
        type = TPTR_TARGET(type);
    }
#endif
    while (IS_TYPE_ARRAY(type)) {
        type = TPTR_TARGET(type);
    }

    if (TYPE_FITS_IN_REGISTER(type) ||
        IS_TYPE_VECTOR(type) ||
        IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) ||
        IS_TYPE_STRUCT(type)) {
        return is_const_object(obj);
    }

    if (IS_TYPE_CLASS(type)) {
        return is_const_object(obj) && CClass_IsPODClass(TYPE_CLASS(type));
    }

    return FALSE;
}

#if VERSION < VERSION_GC_2_5
static inline Alias* make_alias_set(void);

Alias* make_distinct_virtual_alias(Object* obj, SInt32 offset, SInt32 size) {
    Object* object;
    Alias* alias;

    object = galloc(sizeof(Object));
    memclrw(object, sizeof(Object));
    object->otype = OT_OBJECT;
    object->type = TYPE(&worst_case_memory_type);
    object->datatype = DDATA;
    object->name = CParser_GetUniqueName();
    object->flags = obj->flags;
    object->qual = obj->qual;
#if VERSION == VERSION_GC_2_0P2
    return make_alias(object, 0x7FFFFF, 1);
#else
    alias = make_alias_set();
    add_alias_member(alias, make_alias(object, offset, size));
    return alias;
#endif
}
#else
Alias* make_distinct_virtual_alias(Object* obj) {
    Boolean is_readonly;
    Type* type;
    Object* object;
    HashNameNode* name;

    is_readonly = FALSE;
    type = obj->type;
    if (!IS_TYPE_POINTER(obj->type)) {
        return NULL;
    }

    if (!(CParser_GetTypeQualifiers(obj->type, obj->qual) & Q_RESTRICT)) {
        if (CParser_IsReadOnly(TPTR_TARGET(type), obj->qual)) {
            if (IS_TYPE_CLASS(type) && !CClass_IsPODClass(TYPE_CLASS(type))) {
                return NULL;
            }
            is_readonly = TRUE;
        } else {
            return NULL;
        }
    }

    name = CParser_GetUniqueName();
    object = lalloc(sizeof(Object));
    memclrw(object, sizeof(Object));
    object->otype = OT_OBJECT;
    object->type = TYPE(&virtual_object_type);
    object->datatype = DDATA;
    object->name = name;
    if (is_readonly) {
        object->qual = Q_CONST;
    }
    return make_alias(object, 0x7FFFFF, 1);
}
#endif

#if VERSION >= VERSION_GC_2_5
static inline Boolean same_object(Object* a, Object* b) {
    if (a == b) {
        return TRUE;
    }
    if (a->name == b->name &&
        a->nspace == b->nspace &&
        a->datatype == b->datatype &&
        (a->datatype == DFUNC || a->datatype == DDATA)) {
        return CMangler_GetLinkName(a) == CMangler_GetLinkName(b);
    }
    return FALSE;
}

static inline Boolean is_virtual_or_local_static(Object* obj) {
    if (obj->type == TYPE(&virtual_object_type)) {
        return TRUE;
    }
    if (obj->datatype == DDATA && obj->u.data.islocalstatic) {
        return TRUE;
    }
    return FALSE;
}
#endif

#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -16
#endif
void add_aliased_locals_to_worst_case(Alias* alias) {
    AliasMember* member;

    if (alias->type == AliasType_Set) {
        for (member = alias->parents; member; member = member->nextParent) {
            add_aliased_locals_to_worst_case(member->child);
        }
    } else if (alias->type == AliasType_Object || alias->type == AliasType_ObjectPart) {
#if VERSION < VERSION_GC_2_5
        if (alias->object->datatype == DLOCAL && !is_safe_const(alias->object)) {
            add_alias_member(worst_case_ptr, make_alias(alias->object, 0, 0));
        }
#else
        if (alias->object->datatype == DLOCAL) {
            add_alias_member(worst_case_ptr, make_alias(alias->object, 0, 0));
        } else if (is_virtual_or_local_static(alias->object) && !is_safe_const(alias->object)) {
            add_alias_member(worst_case_ptr, make_alias(alias->object, 0, 0));
        }
#endif
    } else {
        CError_FATAL(259);
    }
}

static inline UInt32 hash_alias(Object* object, int offset, int size) {
    return (UInt32)(object->name->hashval * offset * size) % 997;
}

static inline Alias* create_alias(
    AliasType type, Object* object, SInt32 offset, SInt32 size, Boolean addToHash
) {
    Alias* alias;
    UInt32 hash;

    alias = lalloc(sizeof(Alias));
    memclrw(alias, sizeof(Alias));
    alias->type = type;
    alias->index = n_aliases++;
    alias->next = aliases;
    aliases = alias;
    alias->object = object;
    alias->offset = offset;
    alias->size = size;

    if (addToHash) {
        hash = hash_alias(object, offset, size);
        alias->hashNext = alias_hash[hash];
        alias_hash[hash] = alias;
    }

    return alias;
}

static inline Alias* make_alias_set(void) {
    return create_alias(AliasType_Set, NULL, 0, 0, FALSE);
}

void initialize_aliases(void) {
    int i;

    memclrw(&worst_case_obj, sizeof(Object));
    worst_case_obj.otype = OT_OBJECT;
    worst_case_obj.type = TYPE(&worst_case_memory_type);
    worst_case_obj.datatype = DDATA;
    worst_case_obj.name = GetHashNameNodeExport("@worst_case@");

    aliases = NULL;
    set_aliases = NULL;
    n_aliases = 0;
    n_gathered_aliases = 0;
    for (i = 0; i < 997; i++) {
        alias_hash[i] = NULL;
    }

    worst_case_ptr = make_alias_set();
    add_alias_member(worst_case_ptr, make_alias(&worst_case_obj, 0, 0));
#if VERSION >= VERSION_GC_2_5
    worst_case_func = make_alias_set();
    add_alias_member(worst_case_func, make_alias(&worst_case_obj, 0, 0));
#endif
}

Alias* make_alias(Object* object, SInt32 offset, SInt32 size) {
    Alias* alias2;
    Alias* alias;

    if (!size) {
        offset = 0;
        size = object->type->size;
    }

    for (alias = alias_hash[hash_alias(object, offset, size)]; alias; alias = alias->hashNext) {
        if (alias->object == object && alias->offset == offset && alias->size == size) {
            goto found;
        }
    }
    alias = NULL;
found:
    if (!alias) {
        if (offset > 0 || size != object->type->size) {
            alias2 = make_alias(object, 0, object->type->size);
            alias = create_alias(AliasType_ObjectPart, object, offset, size, TRUE);
            add_alias_member(alias2, alias);
        } else {
            alias = create_alias(AliasType_Object, object, offset, size, TRUE);
        }

        switch (object->datatype) {
            case DLOCAL:
                break;
            default:
#if VERSION < VERSION_GC_2_5
                if (!is_safe_const(object)) {
                    add_alias_member(worst_case_ptr, make_alias(object, 0, 0));
                }
#else
                if (!is_safe_const(object)) {
                    if (!is_virtual_or_local_static(object)) {
                        add_alias_member(worst_case_ptr, make_alias(object, 0, 0));
                    } else {
                        add_alias_member(worst_case_func, make_alias(object, 0, 0));
                    }
                }
#endif
        }
    }

    if ((object->type->type != TYPEARRAY || object->type->size > 0) &&
        offset > object->type->size) {
        return NULL;
    } else {
        return alias;
    }
}

static inline Alias* create_alias_union(Alias* a, Alias* b) {
    if (a == b) {
        return a;
    }
#if VERSION >= VERSION_GC_2_5
    if ((a->type == AliasType_ObjectPart || a->type == AliasType_Object) &&
        (b->type == AliasType_ObjectPart || b->type == AliasType_Object) &&
        a->object == b->object) {
        return make_alias(a->object, 0, 0);
    }
#endif
    add_alias_member(worst_case_ptr, a);
    add_alias_member(worst_case_ptr, b);
    return worst_case_ptr;
}

void add_alias_member(Alias* parent, Alias* child) {
    AliasMember* member;

    if (parent == child) {
        return;
    }

    if (child->type == AliasType_Set) {
        for (member = child->parents; member; member = member->nextParent) {
            add_alias_member(parent, member->child);
        }
    } else {
#if VERSION < VERSION_GC_2_5
        if (parent == worst_case_ptr && child->type == AliasType_ObjectPart) {
#else
        if (parent->type == AliasType_Set && child->type == AliasType_ObjectPart) {
#endif
            child = make_alias(child->object, 0, 0);
        }

        for (member = parent->parents; member; member = member->nextParent) {
            if (member->child == child) {
                return;
            }
        }

        member = lalloc(sizeof(AliasMember));
        member->parent = parent;
        member->child = child;
        member->nextParent = parent->parents;
        parent->parents = member;
        member->nextChild = child->children;
        child->children = member;
    }
}

#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -48
#endif
static inline Alias* make_displ_alias(Alias* alias, SInt32 offset, SInt32 size) {
    Alias* a;

    if (size && alias->size != alias->object->type->size) {
        a = make_alias(alias->object, alias->offset + offset, size);
        if (!a) {
            add_alias_member(worst_case_ptr, alias);
            a = worst_case_ptr;
        }
    } else {
        a = make_alias(alias->object, 0, 0);
    }
    CError_ASSERT(948, a);
    return a;
}

static inline Boolean aliases_overlap(Alias* a, Alias* b) {
    return (
        a->offset == b->offset ||
        (a->offset > b->offset && a->offset < (b->offset + b->size)) ||
        (b->offset > a->offset && b->offset < (a->offset + a->size))
    );
}

#if VERSION == VERSION_GC_2_0P2
Boolean is_direct_small_data_access(PCode* pcode) {
    Alias* alias;

    alias = pcode->alias;
    return alias &&
        (alias->type == AliasType_Object || alias->type == AliasType_ObjectPart) &&
        alias->object->datatype != DLOCAL &&
        alias->object->name->name[0] != '@' &&
        pcode->argCount > 2 &&
        pcode->args[2].kind == PCOp_MEMORY &&
        (int)pcode->args[2].arg == RefType_Addr16;
}
#endif

static int is_address_load(PCode* pcode) {
    Object* obj;
    PCodeArg* op;

    switch (pcode->op) {
        case PC_LBZU:
        case PC_LBZUX:
        case PC_LHZU:
        case PC_LHZUX:
        case PC_LHAU:
        case PC_LHAUX:
        case PC_LWZU:
        case PC_LWZUX:
        case PC_STBU:
        case PC_STBUX:
        case PC_STHU:
        case PC_STHUX:
        case PC_STWU:
        case PC_STWUX:
            return TRUE;
        case PC_ADDI:
        case PC_ADDIS:
            if (pcode->args[0].data.reg.reg < n_real_registers[RegClass_GPR]) {
                op = &pcode->args[2];
                if (op->kind == PCOp_MEMORY) {
                    obj = op->data.mem.obj;
#if VERSION < VERSION_GC_2_5
                    if (obj->datatype == DLOCAL && !is_safe_const(obj)) {
#else
                    if (obj->datatype == DLOCAL) {
#endif
                        add_alias_member(worst_case_ptr, make_alias(obj, 0, 0));
                    }
                    return FALSE;
                }
            } else {
                return TRUE;
            }
            break;
        case PC_LI:
            op = &pcode->args[1];
            if (op->kind != PCOp_MEMORY) {
                return FALSE;
            }
            if (pcode->args[0].data.reg.reg < n_real_registers[RegClass_GPR]) {
#if VERSION < VERSION_GC_2_5
                obj = op->data.mem.obj;
                if (obj->datatype == DLOCAL && !is_safe_const(obj)) {
                    add_alias_member(worst_case_ptr, make_alias(obj, 0, 0));
                }
#else
                if (op->data.mem.obj->datatype == DLOCAL) {
                    add_alias_member(worst_case_ptr, make_alias(op->data.mem.obj, 0, 0));
                }
#endif
                return FALSE;
            }
            return TRUE;
        case PC_ADD:
            return TRUE;
        case PC_MR:
            return TRUE;
    }

    return FALSE;
}

static Alias* address_def_alias(PCode* pcode) {
    Object* object;
    SInt32 offset;
    Alias* alias;

    if (pcode->op == PC_LI) {
        if (pcode->args[1].kind != PCOp_MEMORY) {
            return NULL;
        }
        object = pcode->args[1].data.mem.obj;
        offset = pcode->args[1].data.imm.value;
        if (!(alias = pcode->alias)) {
            pcode->alias = alias = make_alias(object, offset, 1);
        }
        return alias;
    }

#if VERSION >= VERSION_GC_2_5
    if (pcode->argCount < 3 && pcode->op != PC_MR) {
        return NULL;
    }
#endif

    if (pcode->argCount > 2 && pcode->args[2].kind == PCOp_MEMORY) {
        object = pcode->args[2].data.mem.obj;
        offset = pcode->args[2].data.imm.value;
        if (pcode->flags & (fIsRead | fIsWrite)) {
            if (pcode->flags & fUpdatesPtr) {
                pcode->alias = make_alias(object, offset, nbytes_loaded_or_stored_by(pcode));
                alias = make_alias(object, offset, 1);
            } else {
                return NULL;
            }
        } else if (!pcode->alias) {
            pcode->alias = alias = make_alias(object, offset, 1);
        } else {
            alias = pcode->alias;
        }
#if VERSION < VERSION_GC_2_5
    } else {
        if ((pcode->flags & (fIsRead | fIsWrite)) &&
            (pcode->flags & fUpdatesPtr) &&
            (alias = pcode->alias) &&
            alias->type == AliasType_ObjectPart) {
            pcode->alias = make_alias(alias->object, 0, 0);
        }
        alias = pcode->alias;
    }
#else
    } else if (pcode->flags & (fIsRead | fIsWrite)) {
        if ((pcode->flags & fUpdatesPtr) &&
            (alias = pcode->alias) &&
            alias->type == AliasType_ObjectPart) {
            pcode->alias = make_alias(alias->object, 0, 0);
        }
        return NULL;
    } else {
        alias = pcode->alias;
    }
#endif

    return alias;
}

#if VERSION < VERSION_GC_2_5
static void add_escaping_locals(PCode* use_pcode, int reg, Alias* alias) {
    if (alias == worst_case_ptr) {
        return;
    }

    if (((use_pcode->flags & fIsWrite) &&
         use_pcode->args[0].kind == PCOp_REGISTER &&
         use_pcode->args[0].arg == RegClass_GPR &&
         use_pcode->args[0].data.reg.reg == reg) ||
        (!(use_pcode->flags & (fIsRead | fIsWrite)) &&
         use_pcode->args[0].data.reg.reg < n_real_registers[RegClass_GPR] &&
         !is_nonvolatile_register(RegClass_GPR, use_pcode->args[0].data.reg.reg))) {
        add_aliased_locals_to_worst_case(alias);
    }
}
#endif

#if VERSION < VERSION_GC_2_5
static void propagate_alias_to_use(PCode* use_pcode, int reg, Alias* alias) {
    short otherReg;
    int j;
    int i;
    SInt32 offset;
    SInt32 size;
    PCode* scan;
    PCodeArg* op;

    size = 1;
    offset = 0;
    if (use_pcode->flags & (fIsRead | fIsWrite)) {
        size = nbytes_loaded_or_stored_by(use_pcode);
    }

    if (use_pcode->op != PC_MR && use_pcode->argCount < 3) {
        return;
    }

    if (use_pcode->args[2].kind == PCOp_REGISTER && use_pcode->args[2].arg == RegClass_GPR) {
        if ((use_pcode->flags & (fIsRead | fIsWrite)) || use_pcode->op == PC_ADD) {
            otherReg = use_pcode->args[1].data.reg.reg;
            if ((int)otherReg == 0 && !(use_pcode->args[1].data.reg.effect & EffectRead)) {
                if (use_pcode->args[2].data.reg.reg == reg) {
                    offset = 0;
                }
            } else {
                if (otherReg == reg) {
                    otherReg = use_pcode->args[2].data.reg.reg;
                } else if (use_pcode->args[2].data.reg.reg != reg) {
                    return;
                }

                for (scan = use_pcode->prevPCode; scan; scan = scan->prevPCode) {
                    if (scan->op == PC_LI && scan->args[0].data.reg.reg == otherReg) {
                        break;
                    }

                    for (j = 0; j < scan->argCount; j++) {
                        if (scan->args[j].kind == PCOp_REGISTER &&
                            scan->args[j].arg == RegClass_GPR &&
                            scan->args[j].data.reg.reg == otherReg &&
                            (scan->args[j].data.reg.effect & EffectWrite)) {
                            scan = NULL;
                            break;
                        }
                    }

                    if (!scan) {
                        break;
                    }
                }

                if (scan) {
                    offset = scan->args[1].data.imm.value;
                } else {
                    offset = 0;
                    size = 0;
                }
            }
        } else {
            add_alias_member(worst_case_ptr, alias);
            alias = worst_case_ptr;
        }
    } else if (use_pcode->args[2].kind == PCOp_IMMEDIATE) {
        if (use_pcode->args[1].kind != PCOp_REGISTER ||
            use_pcode->args[1].arg != RegClass_GPR ||
            use_pcode->args[1].data.reg.reg != reg) {
            return;
        }

        if (use_pcode->args[1].data.reg.effect & EffectWrite) {
            offset = 0;
            size = 0;
        } else if (use_pcode->flags & (fIsRead | fIsWrite)) {
            offset = use_pcode->args[2].data.imm.value;
        } else {
            switch (use_pcode->op) {
                case PC_ADDIS:
                    offset = use_pcode->args[2].data.imm.value << 16;
                    break;
                case PC_ADDI:
                case PC_ADDICR:
                    offset = use_pcode->args[2].data.imm.value;
                    break;
                default:
                    add_alias_member(worst_case_ptr, alias);
                    return;
            }
        }
    } else if (use_pcode->args[2].kind == PCOp_MEMORY) {
        return;
    } else if (use_pcode->op != PC_MR) {
        for (i = 0; i < use_pcode->argCount; i++) {
            op = &use_pcode->args[i];
            if (op->kind == PCOp_REGISTER &&
                op->arg == RegClass_GPR &&
                (op->data.reg.effect & EffectWrite) &&
                op->data.reg.reg == reg) {
                add_alias_member(worst_case_ptr, alias);
                use_pcode->alias = worst_case_ptr;
                return;
            }
        }
        return;
    }

    if (alias->type == AliasType_Object || alias->type == AliasType_ObjectPart) {
        alias = make_displ_alias(alias, offset, size);
    }

    if (!use_pcode->alias) {
        if (use_pcode->op == PC_ADDI ||
            use_pcode->op == PC_ADD ||
            use_pcode->op == PC_ADDIS ||
            ((use_pcode->flags & (fIsRead | fIsWrite)) && (use_pcode->flags & fUpdatesPtr))) {
            recursive_propagation = TRUE;
        }
        use_pcode->alias = alias;
    } else if (use_pcode->alias == worst_case_ptr) {
        add_alias_member(worst_case_ptr, alias);
    } else if (use_pcode->alias == alias) {
        return;
    } else if ((use_pcode->alias->type == AliasType_Object ||
                use_pcode->alias->type == AliasType_ObjectPart) &&
               (alias->type == AliasType_Object || alias->type == AliasType_ObjectPart)) {
        if (alias->object == use_pcode->alias->object) {
            if (alias->offset != use_pcode->alias->offset ||
                alias->size != use_pcode->alias->size) {
                use_pcode->alias = make_alias(alias->object, 0, 0);
            }
        } else {
            use_pcode->alias = create_alias_union(alias, use_pcode->alias);
        }
    } else {
        use_pcode->alias = create_alias_union(alias, use_pcode->alias);
    }

    propagated_instructions = TRUE;
}

static int addresspropagatestouse(int candidateID, int useID) {
    PCode* candidate_pcode;
    PCode* use_pcode;
    Alias* alias;
    int reg;

    candidate_pcode = Candidates[candidateID].pcode;
    use_pcode = Uses[useID].pcode;

    if (candidate_pcode->op == PC_LWZ &&
        candidate_pcode->args[1].data.reg.reg == _CALLER_SP_ &&
        candidate_pcode->args[2].kind == PCOp_IMMEDIATE &&
        candidate_pcode->args[2].data.imm.value == 0) {
        return FALSE;
    }

    alias = address_def_alias(candidate_pcode);
    if (alias) {
        reg = candidate_pcode->args[0].data.reg.reg;
        if ((candidate_pcode->flags & (fIsRead | fIsWrite)) &&
            (candidate_pcode->flags & fUpdatesPtr)) {
            reg = candidate_pcode->args[1].data.reg.reg;
        }
        add_escaping_locals(use_pcode, reg, alias);
        propagate_alias_to_use(use_pcode, reg, alias);
    }

    return TRUE;
}
#else
static void propagate_alias_to_use(PCode* use_pcode, int reg, Alias* alias) {
    short otherReg;
    int j;
    int i;
    SInt32 offset;
    SInt32 size;
    PCode* scan;
    PCodeArg* op;

    size = 1;
    offset = 0;
    if (use_pcode->flags & (fIsRead | fIsWrite)) {
        size = nbytes_loaded_or_stored_by(use_pcode);
    }

    if (use_pcode->op != PC_MR) {
        if (use_pcode->argCount < 3) {
            return;
        }

        if (use_pcode->args[2].kind == PCOp_REGISTER && use_pcode->args[2].arg == RegClass_GPR) {
            if ((use_pcode->flags & (fIsRead | fIsWrite)) || use_pcode->op == PC_ADD) {
                otherReg = use_pcode->args[1].data.reg.reg;
                if ((int)otherReg == 0 && !(use_pcode->args[1].data.reg.effect & EffectRead)) {
                    if (use_pcode->args[2].data.reg.reg == reg) {
                        offset = 0;
                    }
                } else {
                    if (otherReg == reg) {
                        otherReg = use_pcode->args[2].data.reg.reg;
                    } else if (use_pcode->args[2].data.reg.reg != reg) {
                        return;
                    }

                    for (scan = use_pcode->prevPCode; scan; scan = scan->prevPCode) {
                        if (scan->op == PC_LI && scan->args[0].data.reg.reg == otherReg) {
                            break;
                        }

                        for (j = 0; j < scan->argCount; j++) {
                            if (scan->args[j].kind == PCOp_REGISTER &&
                                scan->args[j].arg == RegClass_GPR &&
                                scan->args[j].data.reg.reg == otherReg &&
                                (scan->args[j].data.reg.effect & EffectWrite)) {
                                scan = NULL;
                                break;
                            }
                        }

                        if (!scan) {
                            break;
                        }
                    }

                    if (scan) {
                        offset = scan->args[1].data.imm.value;
                    } else {
                        offset = 0;
                        size = 0;
                    }
                }
            } else {
                add_alias_member(worst_case_ptr, alias);
                alias = worst_case_ptr;
            }
        } else if (use_pcode->args[2].kind == PCOp_IMMEDIATE) {
            if (use_pcode->args[1].kind != PCOp_REGISTER ||
                use_pcode->args[1].arg != RegClass_GPR ||
                use_pcode->args[1].data.reg.reg != reg) {
                return;
            }

            if (use_pcode->args[1].data.reg.effect & EffectWrite) {
                offset = 0;
                size = 0;
            } else if (use_pcode->flags & (fIsRead | fIsWrite)) {
                offset = use_pcode->args[2].data.imm.value;
            } else {
                switch (use_pcode->op) {
                    case PC_ADDIS:
                        offset = use_pcode->args[2].data.imm.value << 16;
                        break;
                    case PC_ADDI:
                    case PC_ADDICR:
                        offset = use_pcode->args[2].data.imm.value;
                        break;
                    default:
                        add_alias_member(worst_case_ptr, alias);
                        return;
                }
            }
        } else if (use_pcode->args[2].kind == PCOp_MEMORY) {
            return;
        } else {
            for (i = 0; i < use_pcode->argCount; i++) {
                op = &use_pcode->args[i];
                if (op->kind == PCOp_REGISTER &&
                    op->arg == RegClass_GPR &&
                    (op->data.reg.effect & EffectWrite) &&
                    op->data.reg.reg == reg) {
                    add_alias_member(worst_case_ptr, alias);
                    use_pcode->alias = worst_case_ptr;
                    return;
                }
            }
            return;
        }
    }

    if (alias->type == AliasType_Object || alias->type == AliasType_ObjectPart) {
        alias = make_displ_alias(alias, offset, size);
    }

    if (!use_pcode->alias) {
        if (use_pcode->op == PC_ADDI ||
            use_pcode->op == PC_ADD ||
            use_pcode->op == PC_ADDIS ||
            use_pcode->op == PC_MR ||
            ((use_pcode->flags & (fIsRead | fIsWrite)) && (use_pcode->flags & fUpdatesPtr))) {
            recursive_propagation = TRUE;
        }
        use_pcode->alias = alias;
    } else if (use_pcode->alias == worst_case_ptr) {
        add_alias_member(worst_case_ptr, alias);
    } else if (use_pcode->alias == alias) {
        return;
    } else if ((use_pcode->alias->type == AliasType_Object ||
                use_pcode->alias->type == AliasType_ObjectPart) &&
               (alias->type == AliasType_Object || alias->type == AliasType_ObjectPart)) {
        if (alias->object == use_pcode->alias->object) {
            if (alias->offset != use_pcode->alias->offset ||
                alias->size != use_pcode->alias->size) {
                use_pcode->alias = make_alias(alias->object, 0, 0);
            }
        } else {
            use_pcode->alias = create_alias_union(alias, use_pcode->alias);
        }
    } else {
        use_pcode->alias = create_alias_union(alias, use_pcode->alias);
    }

    propagated_instructions = TRUE;
}

static int addresspropagatestouse(int candidateID, int useID) {
    int reg;
    int defID;
    PCode* candidate_pcode;
    PCode* use_pcode;
    Alias* alias;

    candidate_pcode = Candidates[candidateID].pcode;
    use_pcode = Uses[useID].pcode;

    if (candidate_pcode->op == PC_LWZ &&
        candidate_pcode->args[1].data.reg.reg == _CALLER_SP_ &&
        candidate_pcode->args[2].kind == PCOp_IMMEDIATE &&
        candidate_pcode->args[2].data.imm.value == 0) {
        return FALSE;
    }

    reg = candidate_pcode->args[0].data.reg.reg;
    if ((candidate_pcode->flags & (fIsRead | fIsWrite)) && (candidate_pcode->flags & fUpdatesPtr)) {
        reg = candidate_pcode->args[1].data.reg.reg;
    }

    defID = candidate_pcode->defID;
    if ((candidate_pcode->flags & fIsRead) && (candidate_pcode->flags & fUpdatesPtr)) {
        defID++;
    }

    if (!canidate_reaches_use(
            candidate_pcode,
            defID,
            reg,
            use_pcode,
            usedefinfo[use_pcode->block->blockIndex].defin,
            RegClass_GPR
        )) {
        return TRUE;
    }

    alias = address_def_alias(candidate_pcode);
    if (alias) {
        if (alias != worst_case_ptr &&
            (((use_pcode->flags & fIsWrite) &&
              use_pcode->args[0].kind == PCOp_REGISTER &&
              use_pcode->args[0].arg == RegClass_GPR &&
              use_pcode->args[0].data.reg.reg == reg) ||
             (!(use_pcode->flags & (fIsRead | fIsWrite)) &&
              use_pcode->args[0].data.reg.reg < n_real_registers[RegClass_GPR]))) {
            add_aliased_locals_to_worst_case(alias);
        }
        propagate_alias_to_use(use_pcode, reg, alias);
    }

    return TRUE;
}
#endif

static void finishpropagatealiases(int id) {
    propagated_instructions = TRUE;
}

// clang-format off
static Propagation alias_prop = {
    &is_address_load,
    &addresspropagatestouse,
    &finishpropagatealiases,
    "ALIAS",
    "ALIASES",
    "A%ld",
    0,
    TRUE
};
// clang-format on

static inline void propagatealiasinfo(Object* proc) {
    propagateinstructions(proc, &alias_prop, (copts.optimizationlevel >= 4) ? 4 : 1, TRUE);
}

#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT 0
static void gather_instruction_alias(PCode* pcode, UInt32* myvec) {
    Alias* alias_array[2];
    Alias* alias_choice;
    int aliases_idx;
    int i;
    PCodeArg* op;
    RegUseOrDef* list;
    PCode* defpcode;

#if VERSION == VERSION_GC_2_0P2
    if ((pcode->flags & fIsWrite) &&
        is_direct_small_data_access(pcode) &&
        !is_safe_const(pcode->alias->object)) {
        pcode->alias = worst_case_ptr;
    }
    if (pcode->alias &&
        (pcode->alias->type == AliasType_Object || pcode->alias->type == AliasType_ObjectPart) &&
        IS_TYPE_FLOAT(pcode->alias->object->type) &&
        (pcode->alias->object->qual & Q_INLINE_DATA) &&
        pcode->alias->object->name->name[0] == '@') {
        pcode->alias = worst_case_ptr;
    }
#endif
    if (!pcode->alias) {
        pcode->alias = worst_case_ptr;
    } else if (pcode->alias != worst_case_ptr) {
        if (!(pcode->argCount > 2 &&
              pcode->args[2].kind == PCOp_MEMORY &&
              pcode->args[1].kind == PCOp_REGISTER &&
              pcode->args[1].arg == RegClass_GPR)) {
            aliases_idx = 0;
            alias_choice = NULL;
            op = pcode->args;
            for (i = 0; i < pcode->argCount; i++, op++) {
                if ((!(pcode->flags & fIsWrite) || op != pcode->args) &&
                    op->kind == PCOp_REGISTER &&
                    op->arg == RegClass_GPR &&
                    (op->data.reg.effect & EffectRead)) {
                    alias_array[aliases_idx] = NULL;
                    if (aliases_idx >= 2) {
                        alias_choice = worst_case_ptr;
                        break;
                    }
                    alias_array[aliases_idx] = pcode->alias;

                    for (list = reg_Defs[op->arg][op->data.reg.reg]; list; list = list->next) {
                        if (bitvectorgetbit(list->id, myvec)) {
                            defpcode = Defs[list->id].pcode;
                            if (!defpcode->alias ||
                                !is_address_load(defpcode) ||
                                defpcode->alias == worst_case_ptr) {
                                alias_array[aliases_idx] = worst_case_ptr;
                                break;
                            }
                        }
                    }

                    aliases_idx++;
                }
            }

            if (!alias_choice) {
                switch (aliases_idx) {
                    case 2:
                        if (alias_array[0] == worst_case_ptr) {
                            if (alias_array[1] == worst_case_ptr) {
                                alias_choice = worst_case_ptr;
                            } else {
                                CError_ASSERT(1250, alias_array[1] == pcode->alias);
                            }
                        } else if (alias_array[1] != worst_case_ptr) {
#if VERSION != VERSION_GC_2_0P2
                            alias_choice = worst_case_ptr;
#endif
                        } else {
                            CError_ASSERT(1258, alias_array[0] == pcode->alias);
                        }
                        break;
                    case 1:
                        alias_choice = alias_array[0];
                        break;
                    default:
                        alias_choice = worst_case_ptr;
                }

                if (alias_choice == worst_case_ptr && pcode->alias != worst_case_ptr) {
                    pcode->flags |= fIsPtrOp;
                    if (pcode->alias->type == AliasType_Set) {
                        add_alias_member(worst_case_ptr, pcode->alias);
                    } else {
                        add_alias_member(worst_case_ptr, make_alias(pcode->alias->object, 0, 0));
                    }
                }

                if (alias_choice) {
                    pcode->alias = alias_choice;
                }
            }
        }

        if ((pcode->flags & (fIsRead | fIsWrite)) &&
            (pcode->flags & fUpdatesPtr) &&
            pcode->alias &&
            pcode->alias->type == AliasType_ObjectPart) {
            pcode->alias = make_alias(pcode->alias->object, 0, 0);
        }
    }

    if ((pcode->alias->type == AliasType_Object || pcode->alias->type == AliasType_ObjectPart) &&
        pcode->alias->size == nbytes_loaded_or_stored_by(pcode)) {
        pcode->flags &= ~fIsPtrOp;
    } else {
        pcode->flags |= fIsPtrOp;
    }
}
#else
static Alias* operand_alias(PCode* pcode, int index, UInt32* myvec) {
    RegUseOrDef* list;
    PCode* defpcode;
    Alias* alias;
    Alias* result;
    int count;
    PCodeArg* op;

    result = NULL;
    count = 0;
    op = &pcode->args[index];
    for (list = reg_Defs[op->arg][op->data.reg.reg]; list; list = list->next) {
        defpcode = Defs[list->id].pcode;
        if (canidate_reaches_use(
                defpcode, list->id, op->data.reg.reg, pcode, myvec, RegClass_GPR
            )) {
            alias = address_def_alias(defpcode);
            count++;
            if (alias) {
                if (count == 1) {
                    result = alias;
                } else if (!result) {
                    result = create_alias_union(worst_case_ptr, alias);
                } else {
                    result = create_alias_union(result, alias);
                }
            } else if (result && result != worst_case_ptr) {
                result = create_alias_union(worst_case_ptr, result);
            }
        }
    }

    return result;
}

static void gather_instruction_alias(PCode* pcode, UInt32* myvec) {
    Alias* alias_choice;
    Alias* alias;
    PCodeArg* op;
    PCodeArg* skip;
    int i;

    if (!pcode->alias) {
        pcode->alias = worst_case_ptr;
    } else if (pcode->alias != worst_case_ptr) {
        if (pcode->argCount < 3 || pcode->args[2].kind != PCOp_MEMORY) {
            alias_choice = NULL;
            op = pcode->args;
            skip = (pcode->flags & fIsWrite) ? pcode->args : pcode->args;
            for (i = 0; i < pcode->argCount; i++, op++) {
                if (op != skip &&
                    op->kind == PCOp_REGISTER &&
                    op->arg == RegClass_GPR &&
                    (op->data.reg.effect & EffectRead)) {
                    alias = operand_alias(pcode, i, myvec);
                    if (alias) {
                        if (!alias_choice) {
                            alias_choice = alias;
                        } else {
                            alias_choice = create_alias_union(alias_choice, alias);
                        }
                    }
                }
            }

            if (alias_choice == worst_case_ptr) {
                pcode->flags |= fIsPtrOp;
                pcode->alias = worst_case_ptr;
                add_alias_member(worst_case_ptr, pcode->alias);
            }
        }

        if ((pcode->flags & (fIsRead | fIsWrite)) &&
            (pcode->flags & fUpdatesPtr) &&
            (alias = pcode->alias) &&
            alias->type == AliasType_ObjectPart) {
            pcode->alias = make_alias(alias->object, 0, 0);
        }
    }

    if ((pcode->alias->type == AliasType_Object || pcode->alias->type == AliasType_ObjectPart) &&
        pcode->alias->size == nbytes_loaded_or_stored_by(pcode)) {
        pcode->flags &= ~fIsPtrOp;
    } else {
        pcode->flags |= fIsPtrOp;
    }
}
#endif

void gather_alias_info(void) {
    Alias* alias;
    AliasMember* member;
    AliasMember* member2;
    PCodeBlock* block;
    PCode* pcode;
    RegUseOrDef* list;
    UseOrDef* def;
    int defID;
    UInt32* myvec;

    if (coloring) {
        propagatealiasinfo(gFunction);
#if VERSION >= VERSION_GC_2_5
        add_alias_member(worst_case_func, worst_case_ptr);
#endif
        myvec = oalloc(4 * ((number_of_Defs + 31) >> 5));

        for (block = pcbasicblocks; block; block = block->nextBlock) {
            bitvectorcopy(myvec, usedefinfo[block->blockIndex].defin, number_of_Defs);

            for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
                if (pcode->flags & (fIsRead | fIsWrite)) {
                    gather_instruction_alias(pcode, myvec);
                } else if ((pcode->flags & fIsCall) && !pcode->alias) {
#if VERSION < VERSION_GC_2_5
                    pcode->alias = worst_case_ptr;
#else
                    pcode->alias = worst_case_func;
#endif
                }

                for (def = &Defs[defID = pcode->defID];
                     defID < number_of_Defs && def->pcode == pcode;
                     def++, defID++) {
                    if (def->v.kind == PCOp_REGISTER && def->v.arg == RegClass_GPR) {
                        for (list = reg_Defs[def->v.arg][def->v.u.reg]; list; list = list->next) {
                            bitvectorclearbit(list->id, myvec);
                        }
                    }
                    bitvectorsetbit(defID, myvec);
                }
            }
        }

        freeoheap();
    } else {
        for (block = pcbasicblocks; block; block = block->nextBlock) {
            for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
#if VERSION < VERSION_GC_2_5
                if ((pcode->flags & (fIsRead | fIsWrite | fIsCall)) && !pcode->alias) {
                    pcode->alias = worst_case_ptr;
                }
#else
                if (!pcode->alias) {
                    if (pcode->flags & (fIsRead | fIsWrite)) {
                        pcode->alias = worst_case_ptr;
                    } else if (pcode->flags & fIsCall) {
                        pcode->alias = worst_case_func;
                    }
                }
#endif
            }
        }
    }

    if (n_gathered_aliases != n_aliases) {
        for (alias = aliases; alias; alias = alias->next) {
            if (alias->type == AliasType_Set) {
                alias->membervec = lalloc(4 * ((n_aliases + 31) >> 5));
                bitvectorinitialize(alias->membervec, n_aliases, 0);
                for (member = alias->parents; member; member = member->nextParent) {
                    bitvectorsetbit(member->child->index, alias->membervec);
                    for (member2 = member->child->parents; member2; member2 = member2->nextParent) {
                        bitvectorsetbit(member2->child->index, alias->membervec);
                    }
                }
            }
        }
        n_gathered_aliases = n_aliases;
    }
}

#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -81
#endif
static Boolean may_alias_alias(Alias* a, Alias* b) {
    switch ((a->type * 3) + b->type) {
        case (AliasType_Object * 3) + AliasType_Object:
            return a == b;
        case (AliasType_Object * 3) + AliasType_ObjectPart:
        case (AliasType_ObjectPart * 3) + AliasType_Object:
#if VERSION < VERSION_GC_2_5
            return a->object == b->object;
#else
            return same_object(a->object, b->object);
#endif
        case (AliasType_ObjectPart * 3) + AliasType_ObjectPart:
#if VERSION < VERSION_GC_2_5
            return (a->object == b->object) && aliases_overlap(a, b);
#else
            return same_object(a->object, b->object) && aliases_overlap(a, b);
#endif
        case (AliasType_Object * 3) + AliasType_Set:
        case (AliasType_ObjectPart * 3) + AliasType_Set:
            return bitvectorgetbit(a->index, b->membervec) != 0;
        case (AliasType_Set * 3) + AliasType_Object:
        case (AliasType_Set * 3) + AliasType_ObjectPart:
            return bitvectorgetbit(b->index, a->membervec) != 0;
        case (AliasType_Set * 3) + AliasType_Set:
            return (a == b) || !bitvectorintersectionisempty(a->membervec, b->membervec, n_aliases);
        default:
            CError_FATAL(1560);
            return TRUE;
    }
}

Boolean may_alias(PCode* a, PCode* b) {
    return may_alias_alias(a->alias, b->alias);
}

#if VERSION == VERSION_GC_2_0P2
static inline Boolean is_pointer_like_load(PCode* pcode) {
    Alias* alias;

    if (pcode->flags & fIsWrite) {
        return FALSE;
    }
    if (is_direct_small_data_access(pcode)) {
        return !is_safe_const(pcode->alias->object);
    }
    alias = pcode->alias;
    return alias &&
        alias->type == AliasType_Object &&
        alias->object->datatype != DLOCAL &&
        alias->object->name->name[0] == '@' &&
        !IS_TYPE_FLOAT(alias->object->type) &&
        pcode->argCount > 2 &&
        pcode->args[2].kind == PCOp_MEMORY &&
        (int)pcode->args[2].arg == RefType_Addr16;
}

static inline Boolean is_small_data_load(PCode* pcode) {
    Alias* alias;

    alias = pcode->alias;
    return !(pcode->flags & fIsWrite) &&
        alias &&
        alias->type != AliasType_Set &&
        alias->object->datatype != DLOCAL &&
        alias->object->name->name[0] != '@' &&
        alias->object->type->size <= copts.sdata_threshold;
}

static inline Boolean is_direct_small_data_store(PCode* pcode) {
    return (pcode->flags & fIsWrite) &&
        pcode->alias == worst_case_ptr &&
        pcode->argCount > 2 &&
        pcode->args[2].kind == PCOp_MEMORY &&
        (int)pcode->args[2].arg == RefType_Addr16;
}

Boolean may_alias_scheduling(PCode* a, PCode* b) {
    if ((is_small_data_load(a) && is_direct_small_data_store(b)) ||
        (is_small_data_load(b) && is_direct_small_data_store(a))) {
        return TRUE;
    }
    return may_alias_alias(
        is_pointer_like_load(a) ? worst_case_ptr : a->alias,
        is_pointer_like_load(b) ? worst_case_ptr : b->alias
    );
}
#endif

#if VERSION < VERSION_GC_2_5
Boolean uniquely_aliases(PCode* a, PCode* b) {
    SInt32 size;

    if (may_alias_alias(a->alias, b->alias) &&
        a->alias &&
        b->alias &&
        a->alias->type != AliasType_Set &&
        b->alias->type != AliasType_Set &&
        b->alias->size == (size = a->alias->size) &&
        size == nbytes_loaded_or_stored_by(a) &&
        size == nbytes_loaded_or_stored_by(b)) {
        return TRUE;
    }
    return FALSE;
}
#endif

Boolean may_alias_object(PCode* pcode, Object* object) {
    return may_alias_alias(pcode->alias, make_alias(object, 0, 0));
}

#if VERSION >= VERSION_GC_2_5
Boolean must_alias_object(PCode* pcode, Object* object) {
    if ((pcode->alias->type == AliasType_ObjectPart || pcode->alias->type == AliasType_Object) &&
        pcode->alias->object == object &&
        pcode->alias->size == object->type->size &&
        pcode->alias->size == nbytes_loaded_or_stored_by(pcode)) {
        return TRUE;
    }
    return FALSE;
}
#endif

void initialize_alias_values(void) {
    Alias* alias;

    for (alias = aliases; alias; alias = alias->next) {
        alias->valuenumber = nextvaluenumber++;
        alias->valuepcode = NULL;
    }
}

#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -140
#endif
void update_alias_value(Alias* alias, PCode* pcode) {
    AliasMember* member2;
    AliasMember* member3;
    AliasMember* member;

    switch (alias->type) {
        case AliasType_Object:
            killmemory(alias, pcode);
            for (member2 = alias->children; member2; member2 = member2->nextChild) {
                CError_ASSERT(1723, member2->parent->type == AliasType_Set);
                killmemory(member2->parent, NULL);
            }
            for (member2 = alias->parents; member2; member2 = member2->nextParent) {
                CError_ASSERT(1728, member2->child->type == AliasType_ObjectPart);
                killmemory(member2->child, NULL);
                for (member3 = member2->child->children; member3; member3 = member3->nextChild) {
                    if (member3->parent != alias) {
                        CError_ASSERT(1734, member3->parent->type == AliasType_Set);
                        killmemory(member3->parent, NULL);
                    }
                }
            }
            break;

        case AliasType_ObjectPart:
            killmemory(alias, pcode);
            for (member2 = alias->children; member2; member2 = member2->nextChild) {
                killmemory(member2->parent, NULL);
                if (member2->parent->type == AliasType_Object) {
                    for (member3 = member2->parent->parents; member3;
                         member3 = member3->nextParent) {
                        if (member3->child != alias && aliases_overlap(alias, member3->child)) {
                            killmemory(member3->child, NULL);
                        }
                    }
                }
            }
            break;

        case AliasType_Set:
            killmemory(alias, NULL);
            for (member = alias->parents; member; member = member->nextParent) {
                killmemory(member->child, NULL);
                for (member2 = member->child->children; member2; member2 = member2->nextChild) {
                    if (member2->parent != alias) {
                        killmemory(member2->parent, NULL);
                    }
                }
                for (member3 = member->child->parents; member3; member3 = member3->nextParent) {
                    killmemory(member3->child, NULL);
                    for (member2 = member3->child->children; member2;
                         member2 = member2->nextChild) {
                        if (member2->parent != member->child) {
                            killmemory(member2->parent, NULL);
                        }
                    }
                }
            }
            break;
    }
}

void update_all_alias_values(void) {
    Alias* alias;

    for (alias = aliases; alias; alias = alias->next) {
        killmemory(alias, NULL);
    }
}

#if VERSION == VERSION_GC_2_0P2
void update_small_data_values(PCode* pcode) {
    Alias* alias;

    alias = pcode->alias;
    if (!alias ||
        alias->type != AliasType_Object ||
        alias->object->datatype == DLOCAL ||
        alias->object->name->name[0] == '@') {
        return;
    }

    for (alias = aliases; alias; alias = alias->next) {
        if ((alias->type == AliasType_Object || alias->type == AliasType_ObjectPart) &&
            alias->object->datatype != DLOCAL &&
            alias->object->name->name[0] != '@' &&
            alias != pcode->alias &&
            alias->object->type->size <= copts.sdata_threshold) {
            killmemory(alias, NULL);
        }
    }
}
#endif
