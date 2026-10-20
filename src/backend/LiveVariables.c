#include "compiler/LiveVariables.h"
#include "compiler/BitVectors.h"
#include "compiler/CMachine.h"
#include "compiler/Coloring.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/objects.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/types.h"

LiveInfo* liveinfo;

static void allocateliveinfo(void) {
    UInt32 regs;
    LiveInfo* info;
    int i;

    regs = used_virtual_registers[coloring_class];

    liveinfo = oalloc(sizeof(LiveInfo) * pcblockcount);
    for (i = 0, info = liveinfo; i < pcblockcount; i++, info++) {
        bitvectorinitialize(info->use = oalloc(4 * ((regs + 31) >> 5)), regs, 0);
        bitvectorinitialize(info->def = oalloc(4 * ((regs + 31) >> 5)), regs, 0);
        bitvectorinitialize(info->in = oalloc(4 * ((regs + 31) >> 5)), regs, 0);
        bitvectorinitialize(info->out = oalloc(4 * ((regs + 31) >> 5)), regs, 0);
    }
}

static void computelocalusedef(void) {
    LiveInfo* info;
    PCodeBlock* block;
    PCode* instr;
    UInt32* use;
    UInt32* def;
    PCodeArg* op;
    int i;

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        info = &liveinfo[block->blockIndex];
        use = info->use;
        def = info->def;

        for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
            op = instr->args;
            i = instr->argCount;
            while (i--) {
                if (PC_OP_IS_READ_ANY_REGISTER(op, coloring_class) &&
                    !bitvectorgetbit(op->data.reg.reg, def)) {
                    bitvectorsetbit(op->data.reg.reg, use);
                }
                op++;
            }

            op = instr->args;
            i = instr->argCount;
            while (i--) {
                if (PC_OP_IS_WRITE_ANY_REGISTER(op, coloring_class) &&
                    !bitvectorgetbit(op->data.reg.reg, use)) {
                    bitvectorsetbit(op->data.reg.reg, def);
                }
                op++;
            }
        }
    }
}

static void computeglobalinout(void) {
    PCLink* link;
    UInt32 regs;
    LiveInfo* info;
    UInt32* use;
    UInt32* def;
    UInt32* in;
    UInt32* out;
    int bitvecsize;
    SInt32 blockIndex;
    int changed; // NOTE: invented name
    PCodeBlock* block;
    UInt32 val;
    SInt32 i;

    regs = used_virtual_registers[coloring_class];
    bitvecsize = (regs + 31) >> 5;
    changed = TRUE;
    while (changed) {
        changed = FALSE;
        blockIndex = pcblockcount;
        while (blockIndex) {
            if ((block = depthfirstordering[--blockIndex])) {
                info = &liveinfo[block->blockIndex];
                if ((link = block->successors)) {
                    out = info->out;
                    bitvectorcopy(out, liveinfo[link->block->blockIndex].in, regs);
                    for (link = link->nextLink; link; link = link->nextLink) {
                        bitvectorunion(out, liveinfo[link->block->blockIndex].in, regs);
                    }
                }

                out = info->out;
                in = info->in;
                use = info->use;
                def = info->def;
                for (i = 0; i < bitvecsize; i++) {
                    val = *use | (*out & ~*def);
                    if (val != *in) {
                        *in = val;
                        changed = TRUE;
                    }
                    in++;
                    out++;
                    use++;
                    def++;
                }
            }
        }
    }
}

void computelivevariables(Object* proc) {
    Type* returnType;
    PCodeBlock* block;
    RegInfo* reginfo;
    int i;

    returnType = TYPE_FUNC(proc->type)->functype;
    computedepthfirstordering();
    allocateliveinfo();
    computelocalusedef();

    // (the global register variables are live at the end of the last block)
    i = pcblockcount;
    do {
        block = depthfirstordering[--i];
    } while (!block);
    for (reginfo = reg_info_list; reginfo; reginfo = reginfo->next) {
        if (coloring_class == reginfo->reginfo.rclass && reginfo->object) {
            bitvectorsetbit(reginfo->reginfo.reg, liveinfo[block->blockIndex].use);
        }
    }

    if (coloring_class == RegClass_GPR && TYPE_FITS_IN_REGISTER(returnType)) {
        bitvectorsetbit(3, liveinfo[epilogue->blockIndex].use);
        if (TYPE_IS_8BYTES(returnType)) {
            bitvectorsetbit(4, liveinfo[pclastblock->blockIndex].use);
        }
    } else if (coloring_class == RegClass_GPR &&
               (IS_TYPE_STRUCT(returnType) || IS_TYPE_CLASS(returnType)) &&
               !Type_RequiresMemoryReturn(returnType)) {
        // (a small struct comes back in r3/r4)
        bitvectorsetbit(3, liveinfo[epilogue->blockIndex].use);
        if (returnType->size > 4) {
            bitvectorsetbit(4, liveinfo[pclastblock->blockIndex].use);
        }
    } else if (coloring_class == RegClass_GPR &&
               copts.soft_float &&
               IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(returnType)) {
        // (without an FPU a float comes back in r3, a double in r3/r4)
        bitvectorsetbit(3, liveinfo[epilogue->blockIndex].use);
        if (returnType->size == 8) {
            bitvectorsetbit(4, liveinfo[pclastblock->blockIndex].use);
        }
    } else if (coloring_class == RegClass_FPR && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(returnType)) {
        bitvectorsetbit(1, liveinfo[epilogue->blockIndex].use);
    } else if (coloring_class == RegClass_FPR && IS_TYPE_VEC2X32FLOAT(returnType)) {
        bitvectorsetbit(1, liveinfo[epilogue->blockIndex].use);
    } else if (coloring_class == RegClass_VR && IS_TYPE_VECTOR(returnType)) {
        bitvectorsetbit(2, liveinfo[epilogue->blockIndex].use);
    }

    computeglobalinout();
}

int dead(PCode* instr, RegClass rclass, UInt32* vec) {
    PCodeArg* op;
    int i;

    if (instr->flags & (fIsBranch | fIsWrite | fIsCall | fIsVolatile | fSideEffects)) {
        return FALSE;
    }
    if (instr->block->flags & (fIsProlog | fIsEpilogue)) {
        return FALSE;
    }

    op = instr->args;
    i = instr->argCount;
    while (i--) {
        if (op->kind == PCOp_REGISTER &&
            (op->data.reg.effect & EffectWrite) &&
            (rclass != op->arg || bitvectorgetbit(op->data.reg.reg, vec))) {
            return FALSE;
        }
        op++;
    }

    if (copts.optimizationlevel <= 0) {
        return FALSE;
    }
    return TRUE;
}
