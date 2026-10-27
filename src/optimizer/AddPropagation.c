#include "compiler/AddPropagation.h"
#include "compiler/Alias.h"
#include "compiler/BitVectors.h"
#include "compiler/CError.h"
#include "compiler/CopyPropagation.h"
#include "compiler/CParser.h"
#include "compiler/objects.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Registers.h"
#include "compiler/StackFrame.h"
#include "compiler/UseDefChains.h"

int propagatedadds;

static int is_add(PCode* instr) {
    return (instr->op == PC_ADD ||
            (instr->op == PC_ADDI &&
             (instr->args[2].kind == PCOp_IMMEDIATE ||
              (instr->args[2].kind == PCOp_MEMORY &&
               (RefType)instr->args[2].arg == RefType_Local16)))) &&
        !(instr->args[0].data.reg.reg < n_real_registers[instr->args[0].arg]);
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -1
#endif
static int addpropagatestouse(int candidateID, int useID) {
    PCode* candidateInstr;
    PCode* useInstr;
    int srcReg;
    int destReg;
    int otherSrcReg;
    Object* object;
    SInt32 offset;
    int i;
    PCode* instr;
    PCodeArg* op;

    candidateInstr = Candidates[candidateID].pcode;
    useInstr = Uses[useID].pcode;
    destReg = candidateInstr->args[0].data.reg.reg;
    srcReg = candidateInstr->args[1].data.reg.reg;
    otherSrcReg = candidateInstr->args[1].data.reg.reg;

    if (!useInstr->block) {
        recursive_propagation = TRUE;
        return FALSE;
    }

    if (useInstr->flags & (fIsRead | fIsWrite)) {
        if (PCODE_FLAG_SET_F(useInstr) & fUpdatesPtr) {
            return FALSE;
        }
    } else if (useInstr->op == PC_ADDI) {
        if (useInstr->args[2].kind != PCOp_IMMEDIATE) {
            return FALSE;
        }
    } else if (useInstr->op != PC_MR) {
        return FALSE;
    }

    if (candidateInstr->op == PC_ADD) {
        if (useInstr->argCount < 3) {
            if (useInstr->op != PC_MR) {
                return FALSE;
            }
        } else if (useInstr->args[2].kind == PCOp_IMMEDIATE) {
            if (useInstr->args[2].data.imm.value != 0) {
                return FALSE;
            }
        } else if (useInstr->args[2].kind == PCOp_REGISTER) {
            if (useInstr->args[1].kind != PCOp_REGISTER ||
                useInstr->args[2].data.reg.reg != destReg ||
                useInstr->args[1].data.reg.reg != 0) {
                return FALSE;
            }
        } else {
            return FALSE;
        }

        object = NULL;
        offset = 0;
        if (candidateInstr->alias && candidateInstr->alias->type == AliasType_Object) {
            object = candidateInstr->alias->object;
            offset = candidateInstr->alias->offset;
        }
        otherSrcReg = candidateInstr->args[2].data.reg.reg;
    } else if (candidateInstr->op == PC_ADDI) {
        if (candidateInstr->alias && candidateInstr->alias->type == AliasType_Object) {
            object = candidateInstr->alias->object;
            offset = candidateInstr->alias->offset;
        } else if (candidateInstr->args[2].kind == PCOp_MEMORY) {
            object = candidateInstr->args[2].data.mem.obj;
            offset = candidateInstr->args[2].data.mem.offset;
        } else if (candidateInstr->args[2].kind == PCOp_IMMEDIATE) {
            object = NULL;
            offset = candidateInstr->args[2].data.imm.value;
        } else {
            return FALSE;
        }

        if (useInstr->argCount < 3) {
            if (useInstr->op != PC_MR) {
                return FALSE;
            }
        } else if (useInstr->args[2].kind == PCOp_IMMEDIATE) {
            offset += useInstr->args[2].data.imm.value;
            if (object) {
                if (object->datatype == DLOCAL) {
                    if (!can_add_displ_to_local(object, offset)) {
                        return FALSE;
                    }
                } else {
                    return FALSE;
                }
            } else {
                switch (useInstr->op) {
                    case PC_PSQ_L:
                    case PC_PSQ_LU:
                    case PC_PSQ_ST:
                    case PC_PSQ_STU:
                        if (offset < -0x800 || offset > 0x7FF) {
                            return FALSE;
                        }
                }
                if (!FITS_IN_SHORT(offset)) {
                    return FALSE;
                }
            }
        } else {
            return FALSE;
        }
    } else {
        CError_FATAL(258);
        return FALSE;
    }

    if ((useInstr->flags & fIsWrite) &&
        useInstr->args[0].kind == PCOp_REGISTER &&
        useInstr->args[0].arg == RegClass_GPR &&
        useInstr->args[0].data.reg.reg == destReg) {
        return FALSE;
    }

    if (useInstr->args[1].data.reg.reg != destReg) {
        return FALSE;
    }

    if (candidateInstr->block == useInstr->block && precedes(candidateInstr, useInstr)) {
        for (instr = candidateInstr->nextPCode; instr && instr != useInstr;
             instr = instr->nextPCode) {
            op = instr->args;
            i = instr->argCount;
            while (i--) {
                if (PC_OP_IS_WRITE_ANY_REGISTER(op, RegClass_GPR) &&
                    (op->data.reg.reg == srcReg || op->data.reg.reg == otherSrcReg)) {
                    return FALSE;
                }
                op++;
            }
        }
    } else {
        if (!bitvectorgetbit(candidateID, propinfo[useInstr->block->blockIndex].in)) {
            return FALSE;
        }

        for (instr = useInstr->block->firstPCode; instr; instr = instr->nextPCode) {
            if (instr == useInstr) {
                break;
            }

            op = instr->args;
            i = instr->argCount;
            while (i--) {
                if (PC_OP_IS_WRITE_ANY_REGISTER(op, RegClass_GPR) &&
                    (op->data.reg.reg == srcReg || op->data.reg.reg == otherSrcReg)) {
                    return FALSE;
                }
                op++;
            }
        }
    }

    if ((useInstr->flags & (fIsRead | fIsWrite | fOpFromMemory | fOpToMemory)) && object) {
        useInstr->alias = make_alias(object, offset, nbytes_loaded_or_stored_by(useInstr));
    }

    return TRUE;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -1
#endif
static void propagateandremoveadd(int id) {
    PCode* instr;
    RegUseOrDef* list;
    PCode* useInstr;
    PCodeBlock* block;
    int reg;
    int i;
    Candidate* candidate;

    candidate = Candidates + id;
    instr = Candidates[id].pcode;
    reg = instr->args[1].data.reg.reg;

    if (instr->op == PC_ADDI && instr->args[2].kind == PCOp_MEMORY) {
        instr->alias = make_alias(instr->args[2].data.mem.obj, instr->args[2].data.mem.offset, 1);
    }

    for (list = candidate->list; list; list = list->next) {
        useInstr = Uses[list->id].pcode;

        if (is_add(useInstr) && useInstr->args[0].data.reg.reg != reg) {
            for (i = 0; i < number_of_candidates; i++) {
                if (Candidates[i].pcode == useInstr) {
                    for (block = pcbasicblocks; block; block = block->nextBlock) {
                        bitvectorclearbit(i, propinfo[block->blockIndex].in);
                    }
                    break;
                }
            }
        }

        if (instr->op == PC_ADD) {
            if (useInstr->flags & (fIsRead | fIsWrite)) {
                if (useInstr->args[2].kind != PCOp_REGISTER) {
                    useInstr->op += 2;
                    useInstr->flags |= fIsPtrOp;
                }
                useInstr->args[1] = instr->args[1];
                useInstr->args[2] = instr->args[2];
                useInstr->alias = instr->alias;
            } else if (useInstr->op == PC_ADDI) {
                CError_ASSERT(392, useInstr->args[2].data.imm.value == 0);
                change_opcode(useInstr, PC_ADD);
                useInstr->args[1] = instr->args[1];
                useInstr->args[2] = instr->args[2];
                useInstr->alias = instr->alias;
            } else if (useInstr->op == PC_MR) {
                change_opcode(useInstr, PC_ADD);
                useInstr->flags = instr->flags;
                change_num_operands(useInstr, 3);
                useInstr->args[1] = instr->args[1];
                useInstr->args[2] = instr->args[2];
                useInstr->alias = instr->alias;
            } else {
                CError_FATAL(406);
            }
        } else if (useInstr->op == PC_MR) {
            change_opcode(useInstr, PC_ADDI);
            useInstr->flags = instr->flags;
            useInstr->alias = instr->alias;
            change_num_operands(useInstr, 3);
            useInstr->args[1] = instr->args[1];
            useInstr->args[2] = instr->args[2];
        } else {
            useInstr->args[1] = instr->args[1];
            if (instr->args[2].kind == PCOp_IMMEDIATE) {
                SInt32 newValue = useInstr->args[2].data.imm.value + instr->args[2].data.imm.value;
                useInstr->args[2] = instr->args[2];
                useInstr->args[2].data.imm.value = newValue;
                useInstr->flags |= fIsPtrOp;
            } else if (instr->args[2].kind == PCOp_MEMORY) {
                SInt32 newValue = useInstr->args[2].data.imm.value + instr->args[2].data.mem.offset;
                useInstr->args[2] = instr->args[2];
                useInstr->args[2].data.mem.offset = newValue;
                useInstr->flags &= ~fIsPtrOp;
                if (useInstr->flags & (fIsRead | fIsWrite | fOpFromMemory | fOpToMemory)) {
                    useInstr->alias = make_alias(
                        useInstr->args[2].data.mem.obj,
                        useInstr->args[2].data.mem.offset,
                        nbytes_loaded_or_stored_by(useInstr)
                    );
                }
            } else {
                CError_FATAL(436);
            }
        }
    }

    deletepcode(instr);
    propagatedadds = TRUE;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

// clang-format off
static Propagation add_prop = {
    &is_add,
    &addpropagatestouse,
    &propagateandremoveadd,
    "ADD",
    "ADDS",
    "a%ld",
    fSideEffects | fRecordBit,
    FALSE
};
// clang-format on

void propagateaddinstructions(Object* proc) {
    propagateinstructions(proc, &add_prop, (copts.optimizationlevel >= 4) ? 4 : 1, TRUE);
    propagatedadds = propagated_instructions;
}
