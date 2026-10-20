#include "compiler/VectorArraysToRegs.h"
#include "compiler/BitVectors.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CompilerTools.h"
#include "compiler/objects.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Registers.h"
#include "compiler/types.h"
#include "compiler/UseDefChains.h"

typedef struct LocalVectorArray {
    struct LocalVectorArray* next;
    Object* object;
    unsigned int invalid : 1;
    SInt32 arraySize;
    SInt32 elementCount;
    int totalUses;
    int elements[1];
} LocalVectorArray;

typedef struct VectorPropInfo {
    UInt32* use;
    UInt32* def;
    UInt32* in;
    UInt32* out;
} VectorPropInfo;

typedef struct ADDI {
    PCode* instr;
    RegUseOrDef* list;
} ADDI;

int number_of_ADDIs;
ADDI* ADDIs;
VectorPropInfo* vectorpropinfo;
int* naddsinblock;
int* firstaddinblock;
Boolean converted_arrays;

static LocalVectorArray* scanforlocalvectorarrays(void) {
    LocalVectorArray* head;
    LocalVectorArray* array;
    ObjectList* list;
    int i;
    SInt32 arraySize;
    SInt32 elementCount;

    head = NULL;

    for (list = locals; list; list = list->next) {
        if (list->object &&
            !(IS_TYPE_POINTER(list->object->type) ? (TPTR_QUAL(list->object->type) & Q_VOLATILE)
                                                  : (list->object->qual & Q_VOLATILE)) &&
            list->object->type &&
            IS_TYPE_ARRAY(list->object->type) &&
            IS_TYPE_VECTOR(TPTR_TARGET(list->object->type))) {
            arraySize = list->object->type->size;
            elementCount = arraySize / 16;
            if (elementCount > 0 && elementCount <= 8) {
                array = oalloc(sizeof(int) * (elementCount - 1) + sizeof(LocalVectorArray));
                array->next = head;
                head = array;

                array->object = list->object;
                array->arraySize = arraySize;
                array->elementCount = arraySize / 16;
                array->totalUses = 0;
                array->invalid = FALSE;

                for (i = 0; i < elementCount; i++) {
                    array->elements[i] = 0;
                }
            }
        }
    }

    return head;
}

static LocalVectorArray* lookup_vector_array_object(LocalVectorArray* arrays, Object* object) {
    while (arrays) {
        if (arrays->object == object) {
            return arrays;
        }
        arrays = arrays->next;
    }
    return NULL;
}

static void scaninstructions(LocalVectorArray* arrays) {
    PCodeBlock* block;
    PCode* instr;
    int counter;
    int i;
    PCodeArg* op;
    LocalVectorArray* array;
    int element;

    naddsinblock = oalloc(sizeof(int) * pcblockcount);
    memclrw(naddsinblock, sizeof(int) * pcblockcount);

    firstaddinblock = oalloc(sizeof(int) * pcblockcount);
    memclrw(firstaddinblock, sizeof(int) * pcblockcount);

    number_of_ADDIs = 0;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        firstaddinblock[block->blockIndex] = number_of_ADDIs;
        counter = 0;
        for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
            if (!(instr->flags & fIsBranch) && instr->argCount) {
                op = instr->args;
                i = instr->argCount;
                while (i--) {
                    if (op->kind == PCOp_MEMORY && (RefType)op->arg == RefType_Local16) {
                        array = lookup_vector_array_object(arrays, op->data.mem.obj);
                        if (array && !array->invalid) {
                            if (instr->op != PC_ADDI) {
                                array->invalid = TRUE;
                            } else if (instr->args[0].data.reg.reg <
                                       n_real_registers[RegClass_GPR]) {
                                array->invalid = TRUE;
                            } else {
                                number_of_ADDIs++;
                                counter++;
                            }

                            if (!array->invalid) {
                                element = op->data.mem.offset / 16;
                                if (element < array->elementCount) {
                                    array->elements[element]++;
                                } else {
                                    array->invalid = TRUE;
                                }
                            }
                        }
                    }
                    op++;
                }
            }
        }
        naddsinblock[block->blockIndex] = counter;
    }
}

static void computeaddilist(LocalVectorArray* arrays) {
    RegUseOrDef* node;
    RegUseOrDef* l;
    PCodeBlock* block;
    PCode* instr;
    RegUseOrDef* list;
    ADDI* addi;
    UInt32* vec;
    LocalVectorArray* array;
    UseOrDef* def;
    int defID;
    UseOrDef* use;
    int useID;
    PCodeArg* op;
    int reg;

    ADDIs = oalloc(sizeof(ADDI) * number_of_ADDIs);
    memclrw(ADDIs, sizeof(ADDI) * number_of_ADDIs);

    vec = oalloc(4 * ((number_of_Uses + 31) >> 5));

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        if (naddsinblock[block->blockIndex]) {
            bitvectorcopy(vec, usedefinfo[block->blockIndex].useout, number_of_Uses);
            addi = &ADDIs[firstaddinblock[block->blockIndex] + naddsinblock[block->blockIndex] - 1];
            for (instr = block->lastPCode; instr; instr = instr->prevPCode) {
                if (!(instr->flags & fIsBranch) && instr->argCount) {
                    if (instr->op == PC_ADDI) {
                        op = &instr->args[2];
                        reg = instr->args[0].data.reg.reg;
                        if (reg >= n_real_registers[RegClass_GPR] &&
                            op->kind == PCOp_MEMORY &&
                            (RefType)op->arg == RefType_Local16) {
                            array = lookup_vector_array_object(arrays, op->data.mem.obj);
                            if (array && !array->invalid) {
                                addi->instr = instr;
                                addi->list = NULL;
                                for (list = reg_Uses[RegClass_GPR][reg]; list; list = list->next) {
                                    if (bitvectorgetbit(list->id, vec)) {
                                        node = oalloc(sizeof(RegUseOrDef));
                                        node->id = list->id;
                                        node->next = addi->list;
                                        addi->list = node;
                                    }
                                }
                                addi--;
                            }
                        }
                    }

                    for (def = &Defs[defID = instr->defID];
                         defID < number_of_Defs && def->pcode == instr;
                         def++, defID++) {
                        if (def->v.kind == PCOp_REGISTER) {
                            for (l = reg_Uses[def->v.arg][def->v.u.reg]; l; l = l->next) {
                                bitvectorclearbit(l->id, vec);
                            }
                        }
                    }

                    for (use = &Uses[useID = instr->useID];
                         useID < number_of_Uses && use->pcode == instr;
                         use++, useID++) {
                        if (use->v.kind == PCOp_REGISTER) {
                            bitvectorsetbit(useID, vec);
                        }
                    }
                }
            }
        }
    }
}

static void allocatevectorpropinfo(void) {
    VectorPropInfo* info;
    int i;

    vectorpropinfo = oalloc(sizeof(VectorPropInfo) * pcblockcount);
    for (i = 0, info = vectorpropinfo; i < pcblockcount; i++, info++) {
        info->use = oalloc(4 * ((number_of_ADDIs + 31) >> 5));
        info->def = oalloc(4 * ((number_of_ADDIs + 31) >> 5));
        info->in = oalloc(4 * ((number_of_ADDIs + 31) >> 5));
        info->out = oalloc(4 * ((number_of_ADDIs + 31) >> 5));
    }
}

static void computelocalvectorpropinfo(LocalVectorArray* arrays) {
    PCodeArg* op;
    int addi_i;
    ADDI* addi;
    PCodeArg* addiop;
    LocalVectorArray* array;
    VectorPropInfo* info;
    UInt32* usevec;
    UInt32* defvec;
    PCodeBlock* block;
    int i;
    PCode* instr;
    int index;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        info = &vectorpropinfo[block->blockIndex];
        usevec = info->use;
        defvec = info->def;
        bitvectorinitialize(usevec, number_of_ADDIs, 0);
        bitvectorinitialize(defvec, number_of_ADDIs, 0);
        index = firstaddinblock[block->blockIndex];

        for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
            if (!(instr->flags & fIsBranch) && instr->argCount) {
                op = instr->args;
                i = instr->argCount;
                while (i--) {
                    if (op->kind == PCOp_REGISTER &&
                        op->arg == RegClass_GPR &&
                        (op->data.reg.effect & EffectWrite)) {
                        for (addi_i = 0, addi = ADDIs; addi_i < number_of_ADDIs; addi_i++, addi++) {
                            if (addi->instr &&
                                addi->instr->args[0].arg == op->arg &&
                                addi->instr->args[0].data.reg.reg == op->data.reg.reg) {
                                if (addi->instr->block == block) {
                                    bitvectorclearbit(addi_i, usevec);
                                } else {
                                    bitvectorsetbit(addi_i, defvec);
                                }
                            }
                        }
                    }
                    op++;
                }

                if (instr->op == PC_ADDI) {
                    addiop = &instr->args[2];
                    if (addiop->kind == PCOp_MEMORY && (RefType)addiop->arg == RefType_Local16) {
                        array = lookup_vector_array_object(arrays, addiop->data.mem.obj);
                        if (array && !array->invalid) {
                            bitvectorsetbit(index, usevec);
                            index++;
                        }
                    }
                }
            }
        }
    }
}

static void computeglobalvectorpropinfo(void) {
    PCLink* preds;
    VectorPropInfo* info;
    PCodeBlock* block;
    UInt32* usevec;
    UInt32* defvec;
    UInt32* invec;
    UInt32* outvec;
    int bitvecsize;
    int blockIndex;
    int i;
    int changed;
    UInt32 val;
    int j;

    bitvecsize = (number_of_ADDIs + 31) >> 5;
    changed = TRUE;
    info = &vectorpropinfo[pcbasicblocks->blockIndex];
    bitvectorinitialize(info->in, number_of_ADDIs, 0);
    bitvectorcopy(info->out, info->use, number_of_ADDIs);

    for (block = pcbasicblocks->nextBlock; block; block = block->nextBlock) {
        info = &vectorpropinfo[block->blockIndex];
        outvec = info->out;
        defvec = info->def;
        for (i = 0; i < bitvecsize; outvec++, defvec++, i++) {
            *outvec = ~*defvec;
        }
    }

    while (changed) {
        changed = FALSE;
        for (blockIndex = 0; blockIndex < pcblockcount; blockIndex++) {
            if (depthfirstordering[blockIndex]) {
                info = &vectorpropinfo[depthfirstordering[blockIndex]->blockIndex];
                if ((preds = depthfirstordering[blockIndex]->predecessors)) {
                    invec = info->in;
                    bitvectorcopy(
                        invec, vectorpropinfo[preds->block->blockIndex].out, number_of_ADDIs
                    );
                    for (preds = preds->nextLink; preds; preds = preds->nextLink) {
                        bitvectorintersect(
                            invec, vectorpropinfo[preds->block->blockIndex].out, number_of_ADDIs
                        );
                    }
                }

                outvec = info->out;
                invec = info->in;
                usevec = info->use;
                defvec = info->def;
                for (j = 0; j < bitvecsize; j++) {
                    val = *usevec | (*invec & ~*defvec);
                    if (val != *outvec) {
                        *outvec = val;
                        changed = TRUE;
                    }
                    invec++;
                    outvec++;
                    defvec++;
                    usevec++;
                }
            }
        }
    }
}

static int checkvectorstoreorload(int addiID, int useID) {
    PCode* addiInstr;
    UseOrDef* use;
    int reg;

    addiInstr = ADDIs[addiID].instr;
    use = Uses + useID;
    if (!addiInstr) {
        return FALSE;
    }

    if ((reg = addiInstr->args[0].data.reg.reg) < n_real_registers[RegClass_GPR]) {
        return FALSE;
    }

    if (use->pcode->op != PC_LVX && use->pcode->op != PC_STVX) {
        return FALSE;
    }

    if (use->pcode->args[1].kind != PCOp_REGISTER ||
        use->pcode->args[1].arg != RegClass_GPR ||
        use->pcode->args[1].data.reg.reg != 0) {
        return FALSE;
    }

    if (use->pcode->args[2].data.reg.reg != reg) {
        return FALSE;
    }
    return TRUE;
}

static int checkalluses(LocalVectorArray* arrays, int addiID) {
    RegUseOrDef* list;
    PCode* instr;
    LocalVectorArray* array;

    instr = ADDIs[addiID].instr;
    for (list = ADDIs[addiID].list; list; list = list->next) {
        if (list && !checkvectorstoreorload(addiID, list->id)) {
            array = lookup_vector_array_object(arrays, instr->args[2].data.mem.obj);
            array->invalid = TRUE;
            return FALSE;
        }
    }

    return TRUE;
}

static void convert_array_to_register(LocalVectorArray* arrays, int addiID) {
    ADDI* addi;
    int newReg;
    RegUseOrDef* list;
    PCode* useInstr;
    LocalVectorArray* array;
    int element;

    addi = ADDIs + addiID;

    if (!addi->instr) {
        return;
    }

    array = lookup_vector_array_object(arrays, addi->instr->args[2].data.mem.obj);
    if (!array || array->invalid) {
        return;
    }

    element = addi->instr->args[2].data.mem.offset / 16;
    if (element > array->elementCount) {
        return;
    }

    newReg = array->elements[element];
    for (list = addi->list; list; list = list->next) {
        useInstr = Uses[list->id].pcode;
        if (useInstr->op == PC_LVX) {
            converted_arrays = TRUE;
            change_opcode(useInstr, PC_VMR);
            change_num_operands(useInstr, 2);
            useInstr->args[1].kind = PCOp_REGISTER;
            useInstr->args[1].arg = RegClass_VR;
            useInstr->args[1].data.reg.reg = newReg;
            useInstr->args[1].data.reg.effect = EffectRead;
        } else if (useInstr->op == PC_STVX) {
            converted_arrays = TRUE;
            change_opcode(useInstr, PC_VMR);
            change_num_operands(useInstr, 2);
            useInstr->args[1] = useInstr->args[0];
            useInstr->args[0].kind = PCOp_REGISTER;
            useInstr->args[0].arg = RegClass_VR;
            useInstr->args[0].data.reg.reg = newReg;
            useInstr->args[0].data.reg.effect = EffectWrite;
        } else {
            CError_FATAL(661);
        }
    }
    deletepcode(addi->instr);
}

static void convert_arrays_to_registers(LocalVectorArray* arrays) {
    SInt32 i;
    int counter;
    LocalVectorArray* best;
    LocalVectorArray* array;
    LocalVectorArray** ptr;
    SInt32 score;

    for (i = 0; i < number_of_ADDIs; i++) {
        checkalluses(arrays, i);
    }

    counter = 0;
    ptr = &arrays;
    array = *ptr;
    while (array) {
        if (array->invalid) {
            *ptr = array->next;
            array = *ptr;
            continue;
        }

        counter += array->elementCount;

        for (i = 0; i < array->elementCount; i++) {
            array->totalUses += array->elements[i];
        }

        array = array->next;
    }

    if (arrays) {
        while (counter > 32) {
            score = 0;
            best = NULL;
            for (array = arrays; array; array = array->next) {
                if (best) {
                    if (array->totalUses < score) {
                        score = array->totalUses;
                        best = array;
                    }
                } else {
                    best = array;
                    score = array->totalUses;
                }
            }

            if (!best) {
                break;
            }

            if (best == arrays) {
                arrays = best->next;
            } else {
                for (array = arrays; array; array = array->next) {
                    if (array->next == best) {
                        array->next = best->next;
                        break;
                    }
                }
            }

            counter -= best->elementCount;
        }

        if (!(array = arrays)) {
            return;
        }

        while (array) {
            for (i = 0; i < array->elementCount; i++) {
                array->elements[i] = used_virtual_registers[RegClass_VR]++;
            }
            array = array->next;
        }

        if (arrays) {
            for (i = 0; i < number_of_ADDIs; i++) {
                convert_array_to_register(arrays, i);
            }
        }
    }
}

int vectorarraystoregs(void) {
    LocalVectorArray* arrays;

    converted_arrays = FALSE;
    if ((arrays = scanforlocalvectorarrays())) {
        scaninstructions(arrays);
        if (number_of_ADDIs > 0) {
            computeusedefchains(FALSE);
            computeaddilist(arrays);
            allocatevectorpropinfo();
            computelocalvectorpropinfo(arrays);
            computedepthfirstordering();
            computeglobalvectorpropinfo();
            convert_arrays_to_registers(arrays);
        }
    }

    freeoheap();
    return converted_arrays;
}
