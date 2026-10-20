#include "compiler/UseDefChains.h"
#include "compiler/Alias.h"
#include "compiler/BitVectors.h"
#include "compiler/CompilerTools.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Registers.h"

int number_of_Defs;
UseOrDef* Defs;
RegUseOrDef** reg_Defs[RegClassMax];
int number_of_Uses;
UseOrDef* Uses;
RegUseOrDef** reg_Uses[RegClassMax];
UseDefInfo* usedefinfo;
ObjectUseDef* objectusedefs;
ObjectUseDef* objectusedeflist;
#if VERSION >= VERSION_GC_2_5
// used_virtual_registers when the chains were computed
SInt32 last_usedef_register[RegClassMax];
#endif

ObjectUseDef* findobjectusedef(Object* object) {
    ObjectUseDef* oud;

    oud = objectusedefs;
    while (oud) {
        if (object < oud->object) {
            oud = oud->leftchild;
        } else if (object > oud->object) {
            oud = oud->rightchild;
        } else {
            return oud;
        }
    }

    return NULL;
}

#if VERSION < VERSION_GC_2_5
static void addobjectusedef(Object* object) {
    ObjectUseDef** ptr;
    ObjectUseDef* list;

    ptr = &objectusedefs;
    while (*ptr) {
        if (object < (*ptr)->object) {
            ptr = &(*ptr)->leftchild;
        } else if (object > (*ptr)->object) {
            ptr = &(*ptr)->rightchild;
        } else {
            return;
        }
    }

    list = oalloc(sizeof(ObjectUseDef));
    list->leftchild = list->rightchild = NULL;
    list->object = object;
    list->uses = list->defs = NULL;
    list->next = objectusedeflist;
    objectusedeflist = list;
    *ptr = list;
}
#else
static inline void addobjectusedef(Object* object) {
    ObjectUseDef* list;
    ObjectUseDef** ptr;

    ptr = &objectusedefs;
    while ((list = *ptr)) {
        if (object < list->object) {
            ptr = &list->leftchild;
        } else if (object > list->object) {
            ptr = &list->rightchild;
        } else {
            return;
        }
    }

    list = oalloc(sizeof(ObjectUseDef));
    memclrw(list, sizeof(ObjectUseDef));
    list->object = object;
    list->next = objectusedeflist;
    objectusedeflist = list;
    *ptr = list;
}
#endif

static void addaliasusedef(Alias* alias) {
    AliasMember* member;

#if VERSION < VERSION_GC_2_5
    if (alias && alias != worst_case_ptr) {
#else
    if (alias && alias != worst_case_ptr && alias != worst_case_func) {
#endif
        if (alias->type == AliasType_Set) {
            for (member = alias->parents; member; member = member->nextParent) {
                addaliasusedef(member->child);
            }
        } else {
            addobjectusedef(alias->object);
        }
    }
}

static void addworstcase(void) {
    addobjectusedef(&worst_case_obj);
}

static void precomputememoryusedefs(void) {
    PCodeBlock* block;
    PCode* pcode;

    objectusedefs = objectusedeflist = NULL;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
            if (pcode->flags & (fIsRead | fIsWrite)) {
                if (pcode->alias) {
#if VERSION < VERSION_GC_2_5
                    if (pcode->alias->type == AliasType_Set ||
                        pcode->alias->size != nbytes_loaded_or_stored_by(pcode)) {
#else
                    if ((pcode->alias->type != AliasType_ObjectPart &&
                         pcode->alias->type != AliasType_Object) ||
                        pcode->alias->size != nbytes_loaded_or_stored_by(pcode)) {
#endif
                        pcode->flags |= fIsPtrOp;
                    }
                    addaliasusedef(pcode->alias);
                } else {
                    pcode->flags |= fIsPtrOp;
                }
            }
        }
    }

    addworstcase();
}

// 2.0 skips branches and takes a memory access through fIsPtrOp as indirect, where 2.5 and later
// ask its alias's kind; 2.5 and later also record each block's range of uses and definitions
static void precomputeusedefcounts(int includeMemory) { // NOTE: invented name: includeMemory
    PCodeBlock* block;
    PCode* pcode;
    PCodeArg* op;
    int i;
    ObjectUseDef* oud;

    number_of_Uses = number_of_Defs = 0;
#if VERSION >= VERSION_GC_2_5
    usedefinfo = oalloc(pcblockcount * sizeof(UseDefInfo));
    memclrw(usedefinfo, pcblockcount * sizeof(UseDefInfo));
#endif

    for (block = pcbasicblocks; block; block = block->nextBlock) {
#if VERSION >= VERSION_GC_2_5
        usedefinfo[block->blockIndex].usestart = number_of_Uses;
        usedefinfo[block->blockIndex].defstart = number_of_Defs;
#endif
        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
#if VERSION < VERSION_GC_2_5
            if (!(pcode->flags & fIsBranch) && pcode->argCount) {
#else
            if (pcode->argCount) {
#endif
                pcode->useID = number_of_Uses;
                pcode->defID = number_of_Defs;
                op = pcode->args;
                for (i = pcode->argCount; i--; op++) {
                    if (op->kind == PCOp_REGISTER &&
                        op->data.reg.reg >= n_real_registers[op->arg]) {
                        if (op->data.reg.effect & EffectRead) {
                            number_of_Uses++;
                        }
                        if (op->data.reg.effect & EffectWrite) {
                            number_of_Defs++;
                        }
                    }
                }

                if (includeMemory) {
                    if (pcode->flags & fIsRead) {
#if VERSION < VERSION_GC_2_5
                        if (pcode->flags & fIsPtrOp) {
#else
                        if (pcode->alias->type != AliasType_ObjectPart &&
                            pcode->alias->type != AliasType_Object) {
#endif
                            for (oud = objectusedeflist; oud; oud = oud->next) {
                                if (may_alias_object(pcode, oud->object)) {
                                    number_of_Uses++;
                                }
                            }
                        } else {
                            number_of_Uses++;
                        }
                    } else if (pcode->flags & fIsWrite) {
#if VERSION < VERSION_GC_2_5
                        if (pcode->flags & fIsPtrOp) {
#else
                        if (pcode->alias->type != AliasType_ObjectPart &&
                            pcode->alias->type != AliasType_Object) {
#endif
                            for (oud = objectusedeflist; oud; oud = oud->next) {
                                if (may_alias_object(pcode, oud->object)) {
                                    number_of_Defs++;
                                }
                            }
                        } else {
                            number_of_Defs++;
                        }
                    } else if (pcode->flags & fIsCall) {
                        for (oud = objectusedeflist; oud; oud = oud->next) {
                            if (may_alias_object(pcode, oud->object)) {
                                number_of_Uses++;
                                number_of_Defs++;
                            }
                        }
                    }
                }
            }
        }
#if VERSION >= VERSION_GC_2_5
        usedefinfo[block->blockIndex].usecount =
            number_of_Uses - usedefinfo[block->blockIndex].usestart;
        usedefinfo[block->blockIndex].defcount =
            number_of_Defs - usedefinfo[block->blockIndex].defstart;
#endif
    }
}

// NOTE: invented names: includeMemory, objUseDef
// 2.0 skips branches and takes a memory access through fIsPtrOp as indirect, as in
// precomputeusedefcounts; 2.5 and later also count each object's direct and indirect uses and
// definitions, a direct access being one that must alias the object
static void computeusedeflists(int includeMemory) {
    ObjectUseDef* oud;
    RegUseOrDef* list;
    PCodeBlock* block;
    PCode* pcode;
    PCodeArg* op;
    int i;
    UseOrDef* use;
    UseOrDef* def;
    int useID;
    int defID;
    ObjectUseDef* objUseDef;
    Object* object;
#if VERSION >= VERSION_GC_2_5
    RegClass rclass;
#endif

    Uses = oalloc(sizeof(UseOrDef) * number_of_Uses);
    Defs = oalloc(sizeof(UseOrDef) * number_of_Defs);

#if VERSION < VERSION_GC_2_5
    reg_Uses[RegClass_GPR] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[RegClass_GPR]);
    reg_Defs[RegClass_GPR] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[RegClass_GPR]);
    for (i = 0; i < used_virtual_registers[RegClass_GPR]; i++) {
        reg_Uses[RegClass_GPR][i] = NULL, reg_Defs[RegClass_GPR][i] = NULL;
    }

    reg_Uses[RegClass_FPR] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[RegClass_FPR]);
    reg_Defs[RegClass_FPR] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[RegClass_FPR]);
    for (i = 0; i < used_virtual_registers[RegClass_FPR]; i++) {
        reg_Uses[RegClass_FPR][i] = NULL, reg_Defs[RegClass_FPR][i] = NULL;
    }

    reg_Uses[RegClass_VR] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[RegClass_VR]);
    reg_Defs[RegClass_VR] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[RegClass_VR]);
    for (i = 0; i < used_virtual_registers[RegClass_VR]; i++) {
        reg_Uses[RegClass_VR][i] = NULL, reg_Defs[RegClass_VR][i] = NULL;
    }
#else
    // every register class (2.0: GPR, FPR and VR)
    for (rclass = 0; rclass < RegClassMax; rclass++) {
        reg_Uses[rclass] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[rclass]);
        reg_Defs[rclass] = oalloc(sizeof(RegUseOrDef*) * used_virtual_registers[rclass]);
        for (i = 0; i < used_virtual_registers[rclass]; i++) {
            reg_Uses[rclass][i] = NULL, reg_Defs[rclass][i] = NULL;
        }
    }
#endif

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
#if VERSION < VERSION_GC_2_5
            if (!(pcode->flags & fIsBranch) && pcode->argCount) {
#else
            if (pcode->argCount) {
#endif
                use = &Uses[useID = pcode->useID];
                def = &Defs[defID = pcode->defID];

                op = pcode->args;
                i = pcode->argCount;
                while (i--) {
                    if (op->kind == PCOp_REGISTER &&
                        op->data.reg.reg >= n_real_registers[op->arg]) {
                        if (op->data.reg.effect & EffectRead) {
                            use->pcode = pcode;
                            use->v.kind = op->kind;
                            use->v.arg = op->arg;
                            use->v.u.reg = op->data.reg.reg;
                            list = oalloc(sizeof(RegUseOrDef));
                            list->id = useID;
                            list->next = reg_Uses[op->arg][op->data.reg.reg];
                            reg_Uses[op->arg][op->data.reg.reg] = list;
                            use++;
                            useID++;
                        }
                        if (op->data.reg.effect & EffectWrite) {
                            def->pcode = pcode;
                            def->v.kind = op->kind;
                            def->v.arg = op->arg;
                            def->v.u.reg = op->data.reg.reg;
                            list = oalloc(sizeof(RegUseOrDef));
                            list->id = defID;
                            list->next = reg_Defs[op->arg][op->data.reg.reg];
                            reg_Defs[op->arg][op->data.reg.reg] = list;
                            def++;
                            defID++;
                        }
                    }
                    op++;
                }

                if (includeMemory) {
                    if (pcode->flags & fIsRead) {
#if VERSION < VERSION_GC_2_5
                        if (pcode->flags & fIsPtrOp) {
#else
                        if (pcode->alias->type != AliasType_ObjectPart &&
                            pcode->alias->type != AliasType_Object) {
#endif
                            for (oud = objectusedeflist; oud; oud = oud->next) {
                                if (may_alias_object(pcode, object = oud->object)) {
                                    use->pcode = pcode;
                                    use->v.kind = PCOp_MEMORY;
                                    use->v.arg = 1;
                                    use->v.u.object = object;
                                    list = oalloc(sizeof(RegUseOrDef));
                                    list->id = useID;
                                    list->next = oud->uses;
                                    oud->uses = list;
#if VERSION >= VERSION_GC_2_5
                                    oud->indirectuses++;
#endif
                                    use++;
                                    useID++;
                                }
                            }
                        } else {
                            object = pcode->alias->object;
#if VERSION >= VERSION_GC_2_5
                            objUseDef = findobjectusedef(object);
#endif
                            use->pcode = pcode;
                            use->v.kind = PCOp_MEMORY;
#if VERSION < VERSION_GC_2_5
                            use->v.arg = 0;
#endif
                            use->v.u.object = object;
#if VERSION >= VERSION_GC_2_5
                            if (must_alias_object(pcode, object)) {
                                use->v.arg = 0;
                                objUseDef->directuses++;
                            } else {
                                use->v.arg = 1;
                                objUseDef->indirectuses++;
                            }
#endif
                            list = oalloc(sizeof(RegUseOrDef));
                            list->id = useID;
#if VERSION < VERSION_GC_2_5
                            objUseDef = findobjectusedef(object);
#endif
                            list->next = objUseDef->uses;
                            objUseDef->uses = list;
                        }
                    } else if (pcode->flags & fIsWrite) {
#if VERSION < VERSION_GC_2_5
                        if (pcode->flags & fIsPtrOp) {
#else
                        if (pcode->alias->type != AliasType_ObjectPart &&
                            pcode->alias->type != AliasType_Object) {
#endif
                            for (oud = objectusedeflist; oud; oud = oud->next) {
                                if (may_alias_object(pcode, object = oud->object)) {
                                    def->pcode = pcode;
                                    def->v.kind = PCOp_MEMORY;
                                    def->v.arg = 1;
                                    def->v.u.object = object;
                                    list = oalloc(sizeof(RegUseOrDef));
                                    list->id = defID;
                                    list->next = oud->defs;
                                    oud->defs = list;
#if VERSION >= VERSION_GC_2_5
                                    oud->indirectdefs++;
#endif
                                    def++;
                                    defID++;
                                }
                            }
                        } else {
                            object = pcode->alias->object;
                            def->pcode = pcode;
                            def->v.kind = PCOp_MEMORY;
#if VERSION < VERSION_GC_2_5
                            def->v.arg = 0;
#endif
                            def->v.u.object = object;
                            list = oalloc(sizeof(RegUseOrDef));
                            list->id = defID;
                            objUseDef = findobjectusedef(object);
#if VERSION >= VERSION_GC_2_5
                            if (must_alias_object(pcode, object)) {
                                def->v.arg = 0;
                                objUseDef->directdefs++;
                            } else {
                                def->v.arg = 1;
                                objUseDef->indirectdefs++;
                            }
#endif
                            list->next = objUseDef->defs;
                            objUseDef->defs = list;
                        }
                    } else if (pcode->flags & fIsCall) {
                        for (oud = objectusedeflist; oud; oud = oud->next) {
                            if (may_alias_object(pcode, object = oud->object)) {
                                use->pcode = pcode;
                                use->v.kind = PCOp_MEMORY;
                                use->v.arg = 1;
                                use->v.u.object = object;
                                list = oalloc(sizeof(RegUseOrDef));
                                list->id = useID;
                                list->next = oud->uses;
                                oud->uses = list;
#if VERSION >= VERSION_GC_2_5
                                oud->indirectuses++;
#endif
                                use++;
                                useID++;

                                def->pcode = pcode;
                                def->v.kind = PCOp_MEMORY;
                                def->v.arg = 1;
                                def->v.u.object = object;
                                list = oalloc(sizeof(RegUseOrDef));
                                list->id = defID;
                                list->next = oud->defs;
                                oud->defs = list;
#if VERSION >= VERSION_GC_2_5
                                oud->indirectdefs++;
#endif
                                def++;
                                defID++;
                            }
                        }
                    }
                }
            }
        }
    }
}

#if VERSION >= VERSION_GC_2_5
static void allocateusedefinfo(void) {
    UseDefInfo* info;
    int i;

    init_compressed_bitvectors(number_of_Defs > number_of_Uses ? number_of_Defs : number_of_Uses);
    for (i = 0, info = usedefinfo; i < pcblockcount; i++, info++) {
        info->defgen = bitvectorcreate_range(info->defstart, info->defcount);
        info->defkill = bitvector_empty_compressed();
        info->defin = oalloc(4 * ((number_of_Defs + 31) >> 5));
        info->defout = oalloc(4 * ((number_of_Defs + 31) >> 5));
        info->usegen = bitvectorcreate_range(info->usestart, info->usecount);
        info->usekill = bitvector_empty_compressed();
        info->usein = oalloc(4 * ((number_of_Uses + 31) >> 5));
        info->useout = oalloc(4 * ((number_of_Uses + 31) >> 5));
    }
}

static void computelocalusedefinfo(void) {
    PCodeBlock* block;
    PCode* pcode;
    BitVectorRange* usegen;
    BitVectorRange* defgen;
    UInt32* defkill;
    UseOrDef* use;
    UseOrDef* def;
    UInt32* usekill;
    int useID;
    int defID;
    ObjectUseDef* oud;
    RegUseOrDef* list;
    UseDefInfo* info;

    defkill = oalloc(4 * ((number_of_Defs + 31) >> 5));
    usekill = oalloc(4 * ((number_of_Uses + 31) >> 5));

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        info = &usedefinfo[block->blockIndex];
        defgen = info->defgen;
        usegen = info->usegen;
        bitvectorinitialize_range(defgen, info->defstart, info->defcount, 0);
        bitvectorinitialize(defkill, number_of_Defs, 0);
        bitvectorinitialize_range(usegen, info->usestart, info->usecount, 0);
        bitvectorinitialize(usekill, number_of_Uses, 0);
        bitvectorinitialize(info->defin, number_of_Defs, 0);
        bitvectorinitialize(info->defout, number_of_Defs, 0);
        bitvectorinitialize(info->usein, number_of_Uses, 0);
        bitvectorinitialize(info->useout, number_of_Uses, 0);

        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
            if (pcode->argCount) {
                for (use = &Uses[useID = pcode->useID];
                     useID < number_of_Uses && use->pcode == pcode;
                     use++, useID++) {
                    bitvectorsetbit_range(useID, usegen);
                    if (use->v.kind == PCOp_REGISTER) {
                        for (list = reg_Defs[use->v.arg][use->v.u.reg]; list; list = list->next) {
                            if (bitvectorgetbit_range(list->id, defgen)) {
                                bitvectorclearbit_range(useID, usegen);
                            }
                        }
                    } else {
                        for (list = findobjectusedef(use->v.u.object)->defs; list;
                             list = list->next) {
                            if (must_alias_object(pcode, Defs[list->id].v.u.object)) {
                                if (Defs[list->id].v.arg == 0 &&
                                    bitvectorgetbit_range(list->id, defgen)) {
                                    bitvectorclearbit_range(useID, usegen);
                                }
                            }
                        }
                    }
                }

                for (def = &Defs[defID = pcode->defID];
                     defID < number_of_Defs && def->pcode == pcode;
                     def++, defID++) {
                    if (def->v.kind == PCOp_REGISTER) {
                        for (list = reg_Uses[def->v.arg][def->v.u.reg]; list; list = list->next) {
                            if (Uses[list->id].pcode->block != block) {
                                bitvectorsetbit(list->id, usekill);
                            }
                        }
                        for (list = reg_Defs[def->v.arg][def->v.u.reg]; list; list = list->next) {
                            if (Defs[list->id].pcode->block != block) {
                                bitvectorsetbit(list->id, defkill);
                            } else {
                                bitvectorclearbit_range(list->id, defgen);
                            }
                        }
                    } else if (def->v.arg == 0) {
                        oud = findobjectusedef(def->v.u.object);
                        for (list = oud->uses; list; list = list->next) {
                            if (may_alias_object(pcode, Uses[list->id].v.u.object)) {
                                if (Uses[list->id].pcode->block != block) {
                                    bitvectorsetbit(list->id, usekill);
                                }
                            }
                        }
                        for (list = oud->defs; list; list = list->next) {
                            if (must_alias_object(pcode, Defs[list->id].v.u.object)) {
                                if (Defs[list->id].pcode->block != block) {
                                    bitvectorsetbit(list->id, defkill);
                                } else {
                                    bitvectorclearbit_range(list->id, defgen);
                                }
                            }
                        }
                    }

                    bitvectorsetbit_range(defID, defgen);
                }
            }
        }

        info->defkill = bitvector_compress(defkill, number_of_Defs);
        info->usekill = bitvector_compress(usekill, number_of_Uses);
    }
}

// OUT = GEN | (IN & ~KILL) for a compressed KILL and a ranged GEN; whether OUT changed (NOTE:
// invented name)
static int updateusedefset(
    UInt32* out, UInt32* in, CompressedBitVector* kill, BitVectorRange* gen, int bit_count
) {
    int words;
    int runs;
    int genStart;
    int genLength;
    int changed;
    CompressedBitRun* run;
    int runStart;
    int runLength;
    int i;
    UInt32 bits;
    UInt32 old;

    words = (bit_count + 31) >> 5;
    runs = kill->nruns;
    genStart = gen->firstword;
    genLength = gen->nwords;
    changed = FALSE;
    run = NULL;
    if (runs) {
        run = kill->runs;
        runStart = run->firstword;
        runLength = run->nwords;
    }
    for (i = 0; i < words; i++) {
        old = out[i];
        bits = in[i];
        if (run) {
            if (i >= runStart + runLength) {
                if (--runs != 0) {
                    run = (CompressedBitRun*)&run->words[runLength];
                    runStart = run->firstword;
                    runLength = run->nwords;
                } else {
                    run = NULL;
                }
            }
            if (run && i >= runStart) {
                bits &= ~run->words[i - runStart];
            }
        }
        if (i >= genStart && i < genStart + genLength) {
            bits |= gen->words[i - genStart];
        }
        if (bits != old) {
            changed = TRUE;
            out[i] = bits;
        }
    }
    return changed;
}

static void computeglobalusedefinfo(void) {
    PCLink* preds;
    UseDefInfo* udi;
    PCodeBlock* block;
    UInt32* defin;
    int i;
    int changed; // NOTE: invented name

    changed = TRUE;
    while (changed) {
        changed = FALSE;
        for (i = 0; i < pcblockcount; i++) {
            if ((block = depthfirstordering[i])) {
                udi = &usedefinfo[block->blockIndex];
                if ((preds = block->predecessors)) {
                    defin = udi->defin;
                    bitvectorcopy(
                        defin, usedefinfo[preds->block->blockIndex].defout, number_of_Defs
                    );
                    for (preds = preds->nextLink; preds; preds = preds->nextLink) {
                        bitvectorunion(
                            defin, usedefinfo[preds->block->blockIndex].defout, number_of_Defs
                        );
                    }
                }
                changed |= updateusedefset(
                    udi->defout, udi->defin, udi->defkill, udi->defgen, number_of_Defs
                );
            }
        }
    }
}

static void computeglobaldefuseinfo(void) {
    PCLink* succs;
    UseDefInfo* udi;
    PCodeBlock* block;
    UInt32* useout;
    int i;
    int changed; // NOTE: invented name

    changed = TRUE;
    while (changed) {
        changed = FALSE;
        i = pcblockcount;
        while (i) {
            if ((block = depthfirstordering[--i])) {
                udi = &usedefinfo[block->blockIndex];
                if ((succs = block->successors)) {
                    useout = udi->useout;
                    bitvectorcopy(
                        useout, usedefinfo[succs->block->blockIndex].usein, number_of_Uses
                    );
                    for (succs = succs->nextLink; succs; succs = succs->nextLink) {
                        bitvectorunion(
                            useout, usedefinfo[succs->block->blockIndex].usein, number_of_Uses
                        );
                    }
                }
                changed |= updateusedefset(
                    udi->usein, udi->useout, udi->usekill, udi->usegen, number_of_Uses
                );
            }
        }
    }
}

#else
// 2.0: every set is a full bit vector, and the global passes update them in place
static void allocateusedefinfo(void) {
    UseDefInfo* info;
    int i;

    usedefinfo = oalloc(pcblockcount * sizeof(UseDefInfo));
    for (i = 0, info = usedefinfo; i < pcblockcount; i++, info++) {
        info->defgen = oalloc(4 * ((number_of_Defs + 31) >> 5));
        info->defkill = oalloc(4 * ((number_of_Defs + 31) >> 5));
        info->defin = oalloc(4 * ((number_of_Defs + 31) >> 5));
        info->defout = oalloc(4 * ((number_of_Defs + 31) >> 5));
        info->usegen = oalloc(4 * ((number_of_Uses + 31) >> 5));
        info->usekill = oalloc(4 * ((number_of_Uses + 31) >> 5));
        info->usein = oalloc(4 * ((number_of_Uses + 31) >> 5));
        info->useout = oalloc(4 * ((number_of_Uses + 31) >> 5));
    }
}

// 2.0 asks its alias analysis about two PCodes: the use against each definition, the definition
// against each use and definition of its object
static void computelocalusedefinfo(void) {
    PCodeBlock* block;
    PCode* pcode;
    UInt32* defgen;
    UInt32* defkill;
    UInt32* usegen;
    UInt32* usekill;
    UseOrDef* use;
    UseOrDef* def;
    int useID;
    int defID;
    ObjectUseDef* oud;
    RegUseOrDef* list;
    UseDefInfo* info;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        info = &usedefinfo[block->blockIndex];
        defgen = info->defgen;
        defkill = info->defkill;
        usegen = info->usegen;
        usekill = info->usekill;
        bitvectorinitialize(defgen, number_of_Defs, 0);
        bitvectorinitialize(defkill, number_of_Defs, 0);
        bitvectorinitialize(usegen, number_of_Uses, 0);
        bitvectorinitialize(usekill, number_of_Uses, 0);
        bitvectorinitialize(info->defin, number_of_Defs, 0);
        bitvectorinitialize(info->defout, number_of_Defs, 0);
        bitvectorinitialize(info->usein, number_of_Uses, 0);
        bitvectorinitialize(info->useout, number_of_Uses, 0);

        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
            if (!(pcode->flags & fIsBranch) && pcode->argCount) {
                for (use = &Uses[useID = pcode->useID];
                     useID < number_of_Uses && use->pcode == pcode;
                     use++, useID++) {
                    bitvectorsetbit(useID, usegen);
                    if (use->v.kind == PCOp_REGISTER) {
                        for (list = reg_Defs[use->v.arg][use->v.u.reg]; list; list = list->next) {
                            if (bitvectorgetbit(list->id, defgen)) {
                                bitvectorclearbit(useID, usegen);
                            }
                        }
                    } else {
                        for (list = findobjectusedef(use->v.u.object)->defs; list;
                             list = list->next) {
                            if (uniquely_aliases(pcode, Defs[list->id].pcode)) {
                                if (Defs[list->id].v.arg == 0 &&
                                    bitvectorgetbit(list->id, defgen)) {
                                    bitvectorclearbit(useID, usegen);
                                }
                            }
                        }
                    }
                }

                for (def = &Defs[defID = pcode->defID];
                     defID < number_of_Defs && def->pcode == pcode;
                     def++, defID++) {
                    if (def->v.kind == PCOp_REGISTER) {
                        for (list = reg_Uses[def->v.arg][def->v.u.reg]; list; list = list->next) {
                            if (Uses[list->id].pcode->block != block) {
                                bitvectorsetbit(list->id, usekill);
                            }
                        }
                        for (list = reg_Defs[def->v.arg][def->v.u.reg]; list; list = list->next) {
                            if (Defs[list->id].pcode->block != block) {
                                bitvectorsetbit(list->id, defkill);
                            } else {
                                bitvectorclearbit(list->id, defgen);
                            }
                        }
                    } else if (def->v.arg == 0) {
                        oud = findobjectusedef(def->v.u.object);
                        for (list = oud->uses; list; list = list->next) {
                            if (may_alias(pcode, Uses[list->id].pcode)) {
                                if (Uses[list->id].pcode->block != block) {
                                    bitvectorsetbit(list->id, usekill);
                                }
                            }
                        }
                        for (list = oud->defs; list; list = list->next) {
                            if (uniquely_aliases(pcode, Defs[list->id].pcode)) {
                                if (Defs[list->id].pcode->block != block) {
                                    bitvectorsetbit(list->id, defkill);
                                } else {
                                    bitvectorclearbit(list->id, defgen);
                                }
                            }
                        }
                    }

                    bitvectorsetbit(defID, defgen);
                }
            }
        }
    }
}

static void computeglobalusedefinfo(void) {
    UInt32* defgen;
    PCLink* preds;
    UseDefInfo* udi;
    PCodeBlock* block;
    UInt32* defin;
    int i;
    int changed; // NOTE: invented name
    int bitvecsize;
    UInt32* defkill;
    UInt32 val;
    UInt32* defout;
    int j;

    bitvecsize = (number_of_Defs + 31) >> 5;
    changed = TRUE;
    while (changed) {
        changed = FALSE;
        for (i = 0; i < pcblockcount; i++) {
            if ((block = depthfirstordering[i])) {
                udi = &usedefinfo[block->blockIndex];
                if ((preds = block->predecessors)) {
                    defin = udi->defin;
                    bitvectorcopy(
                        defin, usedefinfo[preds->block->blockIndex].defout, number_of_Defs
                    );
                    for (preds = preds->nextLink; preds; preds = preds->nextLink) {
                        bitvectorunion(
                            defin, usedefinfo[preds->block->blockIndex].defout, number_of_Defs
                        );
                    }
                }

                defout = udi->defout;
                defin = udi->defin;
                defgen = udi->defgen;
                defkill = udi->defkill;
                for (j = 0; j < bitvecsize; j++) {
                    val = *defgen | (*defin & ~*defkill);
                    if (val != *defout) {
                        *defout = val;
                        changed = TRUE;
                    }
                    defin++;
                    defout++;
                    defkill++;
                    defgen++;
                }
            }
        }
    }
}

static void computeglobaldefuseinfo(void) {
    PCLink* succs;
    UseDefInfo* udi;
    PCodeBlock* block;
    UInt32* useout;
    int i;
    int changed; // NOTE: invented name
    int bitvecsize;
    UInt32* usegen;
    UInt32* usekill;
    UInt32* usein;
    UInt32 val;
    int j;

    bitvecsize = (number_of_Uses + 31) >> 5;
    changed = TRUE;
    while (changed) {
        changed = FALSE;
        i = pcblockcount;
        while (i) {
            if ((block = depthfirstordering[--i])) {
                udi = &usedefinfo[block->blockIndex];
                if ((succs = block->successors)) {
                    useout = udi->useout;
                    bitvectorcopy(
                        useout, usedefinfo[succs->block->blockIndex].usein, number_of_Uses
                    );
                    for (succs = succs->nextLink; succs; succs = succs->nextLink) {
                        bitvectorunion(
                            useout, usedefinfo[succs->block->blockIndex].usein, number_of_Uses
                        );
                    }
                }

                useout = udi->useout;
                usein = udi->usein;
                usegen = udi->usegen;
                usekill = udi->usekill;
                for (j = 0; j < bitvecsize; j++) {
                    val = *usegen | (*useout & ~*usekill);
                    if (val != *usein) {
                        *usein = val;
                        changed = TRUE;
                    }
                    usein++;
                    useout++;
                    usekill++;
                    usegen++;
                }
            }
        }
    }
}
#endif

void computeusedefchains(int includeMemory) { // NOTE: invented name: includeMemory
#if VERSION >= VERSION_GC_2_5
    RegClass rclass;
#endif

    if (includeMemory) {
        gather_alias_info();
        precomputememoryusedefs();
    }

    precomputeusedefcounts(includeMemory);
    computeusedeflists(includeMemory);
    allocateusedefinfo();
    computelocalusedefinfo();
    computedepthfirstordering();
    computeglobalusedefinfo();
    computeglobaldefuseinfo();

#if VERSION >= VERSION_GC_2_5
    for (rclass = 0; rclass < RegClassMax; rclass++) {
        last_usedef_register[rclass] = used_virtual_registers[rclass];
    }
#endif
}

#if VERSION >= VERSION_GC_2_5
// whether B follows A in A's block (2.0 has it in CopyPropagation.c)
int precedes(PCode* a, PCode* b) {
    for (a = a->nextPCode; a; a = a->nextPCode) {
        if (a == b) {
            return TRUE;
        }
    }

    return FALSE;
}

// whether no definition of register REG of class RCLASS lies on the path from the definition
// CANDIDATE (number DEFID) to USE, given the definitions DEFVEC reaching USE's block (the name's
// spelling is the original's)
int canidate_reaches_use(
    PCode* candidate, int defID, int reg, PCode* use, UInt32* defvec, RegClass rclass
) {
    PCodeBlock* block;
    PCode* pcode;
    PCodeArg* op;
    int i;

    block = use->block;
    if (candidate->block == block && precedes(candidate, use)) {
        for (pcode = candidate->nextPCode; pcode && pcode != use; pcode = pcode->nextPCode) {
            op = pcode->args;
            i = pcode->argCount;
            while (i--) {
                if (op->kind == PCOp_REGISTER &&
                    op->arg == rclass &&
                    (op->data.reg.effect & EffectWrite) &&
                    op->data.reg.reg == reg) {
                    return FALSE;
                }
                op++;
            }
        }
    } else {
        if (!bitvectorgetbit(defID, defvec)) {
            return FALSE;
        }
        for (pcode = block->firstPCode; pcode; pcode = pcode->nextPCode) {
            if (pcode == use) {
                break;
            }
            op = pcode->args;
            i = pcode->argCount;
            while (i--) {
                if (op->kind == PCOp_REGISTER &&
                    op->arg == rclass &&
                    (op->data.reg.effect & EffectWrite) &&
                    op->data.reg.reg == reg) {
                    return FALSE;
                }
                op++;
            }
        }
    }

    return TRUE;
}
#endif
