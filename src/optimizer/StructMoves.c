// StructMoves.c: block moves, and small structs loaded into and stored from GPRs.
#include "compiler/StructMoves.h"
#include "compiler/CError.h"
#include "compiler/CodeGen.h"
#include "compiler/CParser.h"
#include "compiler/Operands.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/Registers.h"
#include "compiler/types.h"

void make_addressable(Operand* operand, SInt32 offset, int n) {
    int reg;

    if (operand->optype == OpndType_IndirectSymbol) {
        coerce_to_addressable(operand);
    }

    if (operand->optype != OpndType_IndirectGPR_ImmOffset ||
        (operand->immOffset + offset) > 0x7FFF) {
        reg = used_virtual_registers[RegClass_GPR]++;
        load_address(reg, operand);
        operand->optype = OpndType_IndirectGPR_ImmOffset;
        operand->reg = reg;
        operand->object = NULL;
        operand->immOffset = 0;
    }
}

static void load_displaced_address(Operand* opnd, SInt32 offset) {
    int reg;

    reg = used_virtual_registers[RegClass_GPR]++;
    if (opnd->optype == OpndType_IndirectSymbol) {
        coerce_to_addressable(opnd);
    }

    if (opnd->optype == OpndType_IndirectGPR_ImmOffset) {
        offset += opnd->immOffset;
        if (!FITS_IN_SHORT(offset)) {
            add_immediate(reg, opnd->reg, opnd->object, opnd->immOffset);
            emitpcode(PC_ADDI, reg, reg, 0, offset - opnd->immOffset);
        } else {
            add_immediate(reg, opnd->reg, opnd->object, offset);
        }
    } else if (opnd->optype == OpndType_IndirectGPR_Indexed) {
        emitpcode(PC_ADD, reg, opnd->reg, opnd->regOffset);
        emitpcode(PC_ADDI, reg, reg, 0, offset);
    } else {
        CError_FATAL(104);
    }

    opnd->optype = OpndType_IndirectGPR_ImmOffset;
    opnd->reg = reg;
    opnd->object = NULL;
    opnd->immOffset = 0;
}

// the width (4, 2 or 1 bytes) a move of LEN bytes aligned to ALIGN takes at a time
static inline SInt32 GetStep(SInt32 len, SInt32 align) { // NOTE: invented name
    SInt32 size = len;
    SInt32 step;
    if (!copts.misaligned_mem_access && align < 4) {
        if (align <= 0) {
            align = 1;
        }
        if (align < len) {
            size = align;
        }
    }
    if (size >= 4) {
        step = 4;
    } else if (size >= 2) {
        step = 2;
    } else {
        step = 1;
    }
    return step;
}

// the width a move of SIZE bytes takes at a time
static inline SInt32 GetStepForSize(int size) { // NOTE: invented name
    SInt32 step;
    if (size >= 4) {
        step = 4;
    } else if (size >= 2) {
        step = 2;
    } else {
        step = 1;
    }
    return step;
}

static void move_block_via_load_store(Operand* dst, Operand* src, SInt32 len, SInt32 align) {
    SInt32 step;
    int size;
    SInt32 a;
    SInt32 pos;
    int floatReg;
    int reg;

    if (src->optype == OpndType_IndirectSymbol) {
        coerce_to_addressable(src);
    }
    if (dst->optype == OpndType_IndirectSymbol) {
        coerce_to_addressable(dst);
    }

    if (copts.use_fp && !copts.soft_float && len == 8) {
        floatReg = used_virtual_registers[RegClass_FPR]++;
        if (src->optype == OpndType_IndirectGPR_ImmOffset) {
            load_store_register(PC_LFD, floatReg, src->reg, src->object, src->immOffset);
            setpcodeflags(src->flags);
        } else if (src->optype == OpndType_IndirectGPR_Indexed) {
            emitpcode(PC_LFDX, floatReg, src->reg, src->regOffset);
            setpcodeflags(src->flags);
        } else {
            CError_FATAL(169);
        }

        if (dst->optype == OpndType_IndirectGPR_ImmOffset) {
            load_store_register(PC_STFD, floatReg, dst->reg, dst->object, dst->immOffset);
            setpcodeflags(dst->flags);
        } else if (dst->optype == OpndType_IndirectGPR_Indexed) {
            emitpcode(PC_STFDX, floatReg, dst->reg, dst->regOffset);
            setpcodeflags(dst->flags);
        } else {
            CError_FATAL(181);
        }
    } else {
        a = align;
        size = len;
        if (!copts.misaligned_mem_access && align < 4) {
            if (align <= 0) {
                a = 1;
            }
            if (a < len) {
                size = a;
            }
        }
        step = GetStepForSize(size);

        if (step != len) {
            if (dst->optype == OpndType_IndirectGPR_Indexed) {
                make_addressable(dst, len, 0);
            }
            if (src->optype == OpndType_IndirectGPR_Indexed) {
                make_addressable(src, len, 0);
            }
        }

        for (pos = 0; len != 0; len -= step, pos += step) {
            reg = used_virtual_registers[RegClass_GPR]++;
            if (src->optype == OpndType_IndirectGPR_ImmOffset) {
                load_store_register(
                    (step == 1)       ? PC_LBZ
                        : (step == 2) ? PC_LHZ
                                      : PC_LWZ,
                    reg,
                    src->reg,
                    src->object,
                    src->immOffset + pos
                );
                setpcodeflags(src->flags);
            } else if (src->optype == OpndType_IndirectGPR_Indexed) {
                emitpcode(
                    (step == 1)       ? PC_LBZX
                        : (step == 2) ? PC_LHZX
                                      : PC_LWZX,
                    reg,
                    src->reg,
                    src->regOffset
                );
                setpcodeflags(src->flags);
            } else {
                CError_FATAL(202);
            }

            if (dst->optype == OpndType_IndirectGPR_ImmOffset) {
                load_store_register(
                    (step == 1)       ? PC_STB
                        : (step == 2) ? PC_STH
                                      : PC_STW,
                    reg,
                    dst->reg,
                    dst->object,
                    dst->immOffset + pos
                );
                setpcodeflags(dst->flags);
            } else if (dst->optype == OpndType_IndirectGPR_Indexed) {
                emitpcode(
                    (step == 1)       ? PC_STBX
                        : (step == 2) ? PC_STHX
                                      : PC_STWX,
                    reg,
                    dst->reg,
                    dst->regOffset
                );
                setpcodeflags(dst->flags);
            } else {
                CError_FATAL(214);
            }
        }
    }
}

static void move_block_via_load_store_sequence(
    Operand* dst, Operand* src, SInt32 len, SInt32 align
) {
    SInt32 pos;
    SInt32 i;
    SInt32 step;

    pos = 0;
    make_addressable(dst, len, 0);
    make_addressable(src, len, 0);

    if (copts.use_fp && !copts.soft_float && (align % 8) == 0) {
        while (len >= 16) {
            int reg1 = used_virtual_registers[RegClass_FPR]++;
            int reg2 = used_virtual_registers[RegClass_FPR]++;
            load_store_register(PC_LFD, reg1, src->reg, src->object, src->immOffset + pos);
            setpcodeflags(src->flags);
            load_store_register(PC_LFD, reg2, src->reg, src->object, src->immOffset + pos + 8);
            setpcodeflags(src->flags);

            load_store_register(PC_STFD, reg1, dst->reg, dst->object, dst->immOffset + pos);
            setpcodeflags(dst->flags);
            load_store_register(PC_STFD, reg2, dst->reg, dst->object, dst->immOffset + pos + 8);
            setpcodeflags(dst->flags);

            pos += 16;
            len -= 16;
        }
    }

    while (len >= 8) {
        if (copts.use_fp && !copts.soft_float && (align % 8) == 0) {
            int reg = used_virtual_registers[RegClass_FPR]++;

            load_store_register(PC_LFD, reg, src->reg, src->object, src->immOffset + pos);
            setpcodeflags(src->flags);

            load_store_register(PC_STFD, reg, dst->reg, dst->object, dst->immOffset + pos);
            setpcodeflags(dst->flags);

            pos += 8;
            len -= 8;
        } else {
            step = GetStep(len, align);

            for (i = 0; i < 8; i += (step * 2)) {
                int reg1 = used_virtual_registers[RegClass_GPR]++;
                int reg2 = used_virtual_registers[RegClass_GPR]++;

                load_store_register(
                    (step == 1)       ? PC_LBZ
                        : (step == 2) ? PC_LHZ
                                      : PC_LWZ,
                    reg1,
                    src->reg,
                    src->object,
                    src->immOffset + pos
                );
                setpcodeflags(src->flags);

                load_store_register(
                    (step == 1)       ? PC_LBZ
                        : (step == 2) ? PC_LHZ
                                      : PC_LWZ,
                    reg2,
                    src->reg,
                    src->object,
                    src->immOffset + pos + step
                );
                setpcodeflags(src->flags);

                load_store_register(
                    (step == 1)       ? PC_STB
                        : (step == 2) ? PC_STH
                                      : PC_STW,
                    reg1,
                    dst->reg,
                    dst->object,
                    dst->immOffset + pos
                );
                setpcodeflags(dst->flags);

                load_store_register(
                    (step == 1)       ? PC_STB
                        : (step == 2) ? PC_STH
                                      : PC_STW,
                    reg2,
                    dst->reg,
                    dst->object,
                    dst->immOffset + pos + step
                );
                setpcodeflags(dst->flags);

                pos += (step * 2);
                len -= (step * 2);
            }
        }
    }

    while (len) {
        int reg;

        step = GetStep(len, align);
        reg = used_virtual_registers[RegClass_GPR]++;

        load_store_register(
            (step == 1)       ? PC_LBZ
                : (step == 2) ? PC_LHZ
                              : PC_LWZ,
            reg,
            src->reg,
            src->object,
            src->immOffset + pos
        );
        setpcodeflags(src->flags);

        load_store_register(
            (step == 1)       ? PC_STB
                : (step == 2) ? PC_STH
                              : PC_STW,
            reg,
            dst->reg,
            dst->object,
            dst->immOffset + pos
        );
        setpcodeflags(dst->flags);

        len -= step;
        pos += step;
    }
}

// one load and store of WIDTH bytes at POS
// NOTE: invented name
static inline void copy_chunk(Operand* dst, Operand* src, int width, int pos) {
    int reg;
    // TODO: a workaround: the original stores this copy of WIDTH before it allocates the register,
    // which the parameter alone does not give
    int w = width;

    reg = used_virtual_registers[RegClass_GPR]++;
    load_store_register((w == 1) ? PC_LBZ : (w == 2) ? PC_LHZ : PC_LWZ, reg, src->reg, NULL, pos);
    setpcodeflags(src->flags);
    load_store_register((w == 1) ? PC_STB : (w == 2) ? PC_STH : PC_STW, reg, dst->reg, NULL, pos);
    setpcodeflags(dst->flags);
}

static void move_block_via_inline_loop(Operand* dst, Operand* src, SInt32 len, SInt32 align) {
    PCodeLabel* label;
    SInt32 pos;
    SInt32 step;
    SInt32 step2;
    int reg1;
    int reg2;
    SInt32 remainder;
    SInt32 width;
    int size;
    SInt32 a;

    label = makepclabel();

    step = GetStep(len, align);

    load_displaced_address(dst, -step);
    load_displaced_address(src, -step);

    CError_ASSERT(381, (len / step) != 0);

    step2 = step * 2;
    load_immediate(reg1 = used_virtual_registers[RegClass_GPR]++, len / step2);
    emitpcode(PC_MTCTR, reg1);
    branch_label(label);

    reg1 = used_virtual_registers[RegClass_GPR]++;
    reg2 = used_virtual_registers[RegClass_GPR]++;

    load_store_register(
        (step == 1)       ? PC_LBZ
            : (step == 2) ? PC_LHZ
                          : PC_LWZ,
        reg1,
        src->reg,
        NULL,
        step
    );
    setpcodeflags(src->flags);
    load_store_register(
        (step == 1)       ? PC_LBZU
            : (step == 2) ? PC_LHZU
                          : PC_LWZU,
        reg2,
        src->reg,
        NULL,
        step2
    );
    setpcodeflags(src->flags);
    load_store_register(
        (step == 1)       ? PC_STB
            : (step == 2) ? PC_STH
                          : PC_STW,
        reg1,
        dst->reg,
        NULL,
        step
    );
    setpcodeflags(dst->flags);
    load_store_register(
        (step == 1)       ? PC_STBU
            : (step == 2) ? PC_STHU
                          : PC_STWU,
        reg2,
        dst->reg,
        NULL,
        step2
    );
    setpcodeflags(dst->flags);

    branch_decrement_always(PC_BDNZ, label);

    for (remainder = len & (step2 - 1), pos = step; remainder != 0;
         remainder -= width, pos += width) {
        a = align;
        size = remainder;
        if (!copts.misaligned_mem_access && align < 4) {
            if (align <= 0) {
                a = 1;
            }
            if (a < remainder) {
                size = a;
            }
        }
        width = GetStepForSize(size);
        copy_chunk(dst, src, width, pos);
    }
}

void move_block(Operand* dst, Operand* src, SInt32 len, SInt32 align) {
    Operand myDst;

    myDst = *dst;

    CError_ASSERT(448, myDst.optype >= OpndType_IndirectGPR_ImmOffset);
    CError_ASSERT(450, src->optype >= OpndType_IndirectGPR_ImmOffset);

    if (len == 1 || len == 2 || len == 4) {
        move_block_via_load_store(&myDst, src, len, align);
    } else if (copts.use_fp && !copts.soft_float && len == 8 && align == 8) {
        move_block_via_load_store(&myDst, src, len, align);
    } else if (len <= 16 || (!copts.optimizesize && len <= 64)) {
        move_block_via_load_store_sequence(&myDst, src, len, align);
    } else {
        move_block_via_inline_loop(&myDst, src, len, align);
    }
}

static void load_word_of_small_struct(
    short dstReg, short srcReg, Operand* opnd, SInt32 offset, SInt32 len, SInt32 align
) {
    short pieceReg; // NOTE: invented name
    short resultReg;
    short extra = 0;

    switch (len) {
        case 1:
            pieceReg = used_virtual_registers[RegClass_GPR]++;
            load_store_register(PC_LBZ, pieceReg, srcReg, opnd->object, offset);
            setpcodeflags(opnd->flags);
            emitpcode(PC_RLWINM, dstReg, pieceReg, 24, 0, 7);
            setpcodeflags(opnd->flags);
            break;
        case 2:
        case 3:
            pieceReg = used_virtual_registers[RegClass_GPR]++;
            resultReg = (dstReg != srcReg) ? dstReg : used_virtual_registers[RegClass_GPR]++;
            if (align > 1) {
                load_store_register(PC_LHZ, pieceReg, srcReg, opnd->object, offset);
                extra += 2;
                setpcodeflags(opnd->flags);
                emitpcode(PC_RLWINM, resultReg, pieceReg, 16, 0, 15);
                setpcodeflags(opnd->flags);
            } else {
                load_store_register(PC_LBZ, pieceReg, srcReg, opnd->object, offset);
                setpcodeflags(opnd->flags);
                emitpcode(PC_RLWINM, resultReg, pieceReg, 24, 0, 7);
                setpcodeflags(opnd->flags);

                load_store_register(PC_LBZ, pieceReg, srcReg, opnd->object, offset + 1);
                extra += 2;
                setpcodeflags(opnd->flags);
                emitpcode(PC_RLWIMI, resultReg, pieceReg, 16, 8, 15);
                setpcodeflags(opnd->flags);
            }
            if (len == 3) {
                load_store_register(PC_LBZ, pieceReg, srcReg, opnd->object, offset + extra);
                setpcodeflags(opnd->flags);
                emitpcode(PC_RLWIMI, resultReg, pieceReg, 8, 16, 23);
                setpcodeflags(opnd->flags);
            }
            if (resultReg != dstReg) {
                emitpcode(PC_MR, dstReg, resultReg);
            }
            break;
        case 4:
            if (align > 2) {
                load_store_register(PC_LWZ, dstReg, srcReg, opnd->object, offset);
                setpcodeflags(opnd->flags);
            } else if (align > 1) {
                pieceReg = used_virtual_registers[RegClass_GPR]++;
                resultReg = (dstReg != srcReg) ? dstReg : used_virtual_registers[RegClass_GPR]++;
                load_store_register(PC_LHZ, pieceReg, srcReg, opnd->object, offset);
                setpcodeflags(opnd->flags);
                emitpcode(PC_RLWINM, resultReg, pieceReg, 16, 0, 15);
                setpcodeflags(opnd->flags);

                load_store_register(PC_LHZ, pieceReg, srcReg, opnd->object, offset + 2);
                setpcodeflags(opnd->flags);
                emitpcode(PC_RLWIMI, resultReg, pieceReg, 0, 16, 31);
                setpcodeflags(opnd->flags);
                if (resultReg != dstReg) {
                    emitpcode(PC_MR, dstReg, resultReg);
                }
            } else {
                pieceReg = used_virtual_registers[RegClass_GPR]++;
                resultReg = (dstReg != srcReg) ? dstReg : used_virtual_registers[RegClass_GPR]++;
                load_store_register(PC_LBZ, pieceReg, srcReg, opnd->object, offset);
                setpcodeflags(opnd->flags);
                emitpcode(PC_RLWINM, resultReg, pieceReg, 24, 0, 7);
                setpcodeflags(opnd->flags);

                load_store_register(PC_LBZ, pieceReg, srcReg, opnd->object, offset + 1);
                setpcodeflags(opnd->flags);
                emitpcode(PC_RLWIMI, resultReg, pieceReg, 16, 8, 15);
                setpcodeflags(opnd->flags);

                load_store_register(PC_LBZ, pieceReg, srcReg, opnd->object, offset + 2);
                setpcodeflags(opnd->flags);
                emitpcode(PC_RLWIMI, resultReg, pieceReg, 8, 16, 23);
                setpcodeflags(opnd->flags);

                load_store_register(PC_LBZ, pieceReg, srcReg, opnd->object, offset + 3);
                setpcodeflags(opnd->flags);
                emitpcode(PC_RLWIMI, resultReg, pieceReg, 0, 24, 31);
                setpcodeflags(opnd->flags);
                if (resultReg != dstReg) {
                    emitpcode(PC_MR, dstReg, resultReg);
                }
            }
            break;
    }
}

void load_small_block_into_reg(short dstReg, Operand* srcOpnd, Type* type, SInt32 align) {
    int reg;
    short finalReg;
    short hiReg; // NOTE: invented name
    SInt32 absAddress;

    coerce_to_addressable(srcOpnd);

    if (srcOpnd->optype == OpndType_IndirectGPR_Indexed) {
        reg = used_virtual_registers[RegClass_GPR]++;
        load_address(reg, srcOpnd);
        srcOpnd->optype = OpndType_IndirectGPR_ImmOffset;
        srcOpnd->reg = reg;
        srcOpnd->object = NULL;
        srcOpnd->immOffset = 0;
    }

    if (copts.misaligned_mem_access) {
        align = 4;
    }

    switch (srcOpnd->optype) {
        case OpndType_GPRPair:
            return;
        case OpndType_GPR:
            return;
        case OpndType_GPR_ImmOffset:
            finalReg = dstReg ? dstReg : used_virtual_registers[RegClass_GPR]++;
            add_immediate(finalReg, srcOpnd->reg, srcOpnd->object, srcOpnd->immOffset);
            break;
        case OpndType_GPR_Indexed:
            finalReg = dstReg ? dstReg : used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_ADD, finalReg, srcOpnd->reg, srcOpnd->regOffset);
            break;
        case OpndType_Absolute:
            finalReg = dstReg ? dstReg : used_virtual_registers[RegClass_GPR]++;
            absAddress = srcOpnd->immediate;
            if (FITS_IN_SHORT(absAddress)) {
                emitpcode(PC_LI, finalReg, absAddress);
            } else {
                hiReg = finalReg;
                if (copts.optimizationlevel > 1 && LOW_PART(absAddress)) {
                    hiReg = used_virtual_registers[RegClass_GPR]++;
                }
                emitpcode(PC_LIS, hiReg, 0, HIGH_PART(absAddress));
                if (LOW_PART(absAddress)) {
                    emitpcode(PC_ADDI, finalReg, hiReg, 0, LOW_PART(absAddress));
                }
            }
            break;
        case OpndType_IndirectGPR_ImmOffset:
            finalReg = dstReg ? dstReg : used_virtual_registers[RegClass_GPR]++;
            load_word_of_small_struct(
                finalReg, srcOpnd->reg, srcOpnd, srcOpnd->immOffset, type->size, align
            );
            break;
        default:
            CError_FATAL(633);
    }

    srcOpnd->optype = OpndType_GPR;
    srcOpnd->reg = finalReg;
}

void load_small_block_into_reg_pair(
    short dstRegLo, short dstRegHi, Operand* srcOpnd, Type* type, SInt32 align
) {
    int reg;
    short finalRegLo;
    short finalRegHi;
    short tmpRegLo;
    short tmpRegHi;
    short hiReg; // NOTE: invented name
    SInt32 absAddress;

    finalRegHi = -1;
    coerce_to_addressable(srcOpnd);

    if (srcOpnd->optype == OpndType_IndirectGPR_Indexed) {
        reg = used_virtual_registers[RegClass_GPR]++;
        load_address(reg, srcOpnd);
        srcOpnd->optype = OpndType_IndirectGPR_ImmOffset;
        srcOpnd->reg = reg;
        srcOpnd->object = NULL;
        srcOpnd->immOffset = 0;
    }

    if (copts.misaligned_mem_access) {
        align = 4;
    }

    switch (srcOpnd->optype) {
        case OpndType_GPRPair:
            if (dstRegLo != 0 && dstRegHi == 0) {
                dstRegHi = used_virtual_registers[RegClass_GPR]++;
            }
            if (dstRegHi != 0 && dstRegLo == 0) {
                dstRegLo = used_virtual_registers[RegClass_GPR]++;
            }

            if (srcOpnd->reg != dstRegLo || srcOpnd->regHi != dstRegHi) {
                tmpRegLo = dstRegLo ? dstRegLo : srcOpnd->reg;
                tmpRegHi = dstRegHi ? dstRegHi : srcOpnd->regHi;

                if (tmpRegLo != srcOpnd->reg) {
                    if (tmpRegLo == srcOpnd->regHi) {
                        CError_ASSERT(684, tmpRegLo != tmpRegHi);
                        emitpcode(PC_MR, tmpRegHi, srcOpnd->regHi);
                        emitpcode(PC_MR, tmpRegLo, srcOpnd->reg);
                    } else {
                        emitpcode(PC_MR, tmpRegLo, srcOpnd->reg);
                        if (srcOpnd->regHi != tmpRegHi) {
                            emitpcode(PC_MR, tmpRegHi, srcOpnd->regHi);
                        }
                    }
                } else if (tmpRegHi != srcOpnd->regHi) {
                    if (tmpRegHi == srcOpnd->reg) {
                        CError_ASSERT(698, tmpRegLo != tmpRegHi);
                        emitpcode(PC_MR, tmpRegLo, srcOpnd->reg);
                        emitpcode(PC_MR, tmpRegHi, srcOpnd->regHi);
                    } else {
                        emitpcode(PC_MR, tmpRegHi, srcOpnd->regHi);
                        if (srcOpnd->reg != tmpRegLo) {
                            emitpcode(PC_MR, tmpRegLo, srcOpnd->reg);
                        }
                    }
                }
            }

            finalRegLo = srcOpnd->reg;
            finalRegHi = srcOpnd->regHi;
            break;
        case OpndType_GPR:
            CError_FATAL(715);
            break;
        case OpndType_GPR_ImmOffset:
            CError_FATAL(718);
            break;
        case OpndType_GPR_Indexed:
            CError_FATAL(721);
            break;
        case OpndType_Absolute:
            finalRegLo = dstRegLo ? dstRegLo : used_virtual_registers[RegClass_GPR]++;
            absAddress = srcOpnd->immediate;
            if (FITS_IN_SHORT(absAddress)) {
                emitpcode(PC_LI, finalRegLo, absAddress);
            } else {
                hiReg = finalRegLo;
                if (copts.optimizationlevel > 1 && LOW_PART(absAddress)) {
                    hiReg = used_virtual_registers[RegClass_GPR]++;
                }
                emitpcode(PC_LIS, hiReg, 0, HIGH_PART(absAddress));
                if (LOW_PART(absAddress)) {
                    emitpcode(PC_ADDI, finalRegLo, hiReg, 0, LOW_PART(absAddress));
                }
            }

            finalRegHi = dstRegHi ? dstRegHi : used_virtual_registers[RegClass_GPR]++;
            if (is_unsigned(type) || absAddress >= 0) {
                load_immediate(finalRegHi, 0);
            } else {
                load_immediate(finalRegHi, -1);
            }
            break;
        case OpndType_IndirectGPR_ImmOffset:
            finalRegLo = dstRegLo ? dstRegLo : used_virtual_registers[RegClass_GPR]++;
            finalRegHi = dstRegHi ? dstRegHi : used_virtual_registers[RegClass_GPR]++;
            if (srcOpnd->reg == finalRegHi) {
                if (srcOpnd->reg == finalRegLo) {
                    CError_FATAL(753);
                } else {
                    load_word_of_small_struct(
                        finalRegLo,
                        srcOpnd->reg,
                        srcOpnd,
                        srcOpnd->immOffset + low_offset,
                        type->size - 4,
                        align
                    );
                    load_word_of_small_struct(
                        finalRegHi,
                        srcOpnd->reg,
                        srcOpnd,
                        srcOpnd->immOffset + high_offset,
                        4,
                        align
                    );
                }
            } else {
                load_word_of_small_struct(
                    finalRegHi, srcOpnd->reg, srcOpnd, srcOpnd->immOffset + high_offset, 4, align
                );
                load_word_of_small_struct(
                    finalRegLo,
                    srcOpnd->reg,
                    srcOpnd,
                    srcOpnd->immOffset + low_offset,
                    type->size - 4,
                    align
                );
            }
            break;
        default:
            CError_FATAL(764);
    }

    if (finalRegHi == -1) {
        CError_FATAL(768);
    } else {
        srcOpnd->optype = OpndType_GPRPair;
        srcOpnd->reg = finalRegLo;
        srcOpnd->regHi = finalRegHi;
    }
}

static void store_word_of_small_struct(
    short srcReg, short dstReg, Operand* opnd, SInt32 offset, SInt32 len, SInt32 align
) {
    short pieceReg; // NOTE: invented name
    short extra = 0;

    switch (len) {
        case 1:
            pieceReg = used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_RLWINM, pieceReg, srcReg, 8, 24, 31);
            setpcodeflags(opnd->flags);
            load_store_register(PC_STB, pieceReg, dstReg, opnd->object, offset);
            setpcodeflags(opnd->flags);
            break;
        case 2:
        case 3:
            if (align > 1) {
                pieceReg = used_virtual_registers[RegClass_GPR]++;
                emitpcode(PC_RLWINM, pieceReg, srcReg, 16, 16, 31);
                setpcodeflags(opnd->flags);
                load_store_register(PC_STH, pieceReg, dstReg, opnd->object, offset);
                extra += 2;
                setpcodeflags(opnd->flags);
            } else {
                pieceReg = used_virtual_registers[RegClass_GPR]++;
                emitpcode(PC_RLWINM, pieceReg, srcReg, 8, 24, 31);
                setpcodeflags(opnd->flags);
                load_store_register(PC_STB, pieceReg, dstReg, opnd->object, offset);
                setpcodeflags(opnd->flags);

                emitpcode(PC_RLWINM, pieceReg, srcReg, 16, 24, 31);
                setpcodeflags(opnd->flags);
                load_store_register(PC_STB, pieceReg, dstReg, opnd->object, offset + 1);
                extra += 2;
                setpcodeflags(opnd->flags);
            }
            if (len == 3) {
                emitpcode(PC_RLWINM, pieceReg, srcReg, 24, 24, 31);
                setpcodeflags(opnd->flags);
                load_store_register(PC_STB, pieceReg, dstReg, opnd->object, offset + extra);
                setpcodeflags(opnd->flags);
            }
            break;
        case 4:
            if (align > 2) {
                load_store_register(PC_STW, srcReg, dstReg, opnd->object, offset);
                setpcodeflags(opnd->flags);
            } else if (align > 1) {
                pieceReg = used_virtual_registers[RegClass_GPR]++;
                emitpcode(PC_RLWINM, pieceReg, srcReg, 16, 16, 31);
                setpcodeflags(opnd->flags);
                load_store_register(PC_STH, pieceReg, dstReg, opnd->object, offset);
                setpcodeflags(opnd->flags);

                load_store_register(PC_STH, srcReg, dstReg, opnd->object, offset + 2);
                setpcodeflags(opnd->flags);
            } else {
                pieceReg = used_virtual_registers[RegClass_GPR]++;
                emitpcode(PC_RLWINM, pieceReg, srcReg, 8, 24, 31);
                setpcodeflags(opnd->flags);
                load_store_register(PC_STB, pieceReg, dstReg, opnd->object, offset);
                setpcodeflags(opnd->flags);

                emitpcode(PC_RLWINM, pieceReg, srcReg, 16, 24, 31);
                setpcodeflags(opnd->flags);
                load_store_register(PC_STB, pieceReg, dstReg, opnd->object, offset + 1);
                setpcodeflags(opnd->flags);

                emitpcode(PC_RLWINM, pieceReg, srcReg, 24, 24, 31);
                setpcodeflags(opnd->flags);
                load_store_register(PC_STB, pieceReg, dstReg, opnd->object, offset + 2);
                setpcodeflags(opnd->flags);

                load_store_register(PC_STB, srcReg, dstReg, opnd->object, offset + 3);
                setpcodeflags(opnd->flags);
            }
            break;
    }
}

void store_small_block_from_reg(short srcReg, Operand* dstOpnd, Type* type, SInt32 align) {
    short addrReg; // NOTE: invented name

    coerce_to_addressable(dstOpnd);

    if (dstOpnd->optype == OpndType_IndirectGPR_Indexed) {
        addrReg = used_virtual_registers[RegClass_GPR]++;
        load_address(addrReg, dstOpnd);
        dstOpnd->optype = OpndType_IndirectGPR_ImmOffset;
        dstOpnd->reg = addrReg;
        dstOpnd->object = NULL;
        dstOpnd->immOffset = 0;
    }

    if (copts.misaligned_mem_access) {
        align = 4;
    }

    store_word_of_small_struct(
        srcReg, dstOpnd->reg, dstOpnd, dstOpnd->immOffset, type->size, align
    );
}

void store_small_block_from_reg_pair(
    short srcRegLo, short srcRegHi, Operand* dstOpnd, Type* type, SInt32 align
) {
    short addrReg; // NOTE: invented name

    coerce_to_addressable(dstOpnd);

    if (dstOpnd->optype == OpndType_IndirectGPR_Indexed) {
        addrReg = used_virtual_registers[RegClass_GPR]++;
        load_address(addrReg, dstOpnd);
        dstOpnd->optype = OpndType_IndirectGPR_ImmOffset;
        dstOpnd->reg = addrReg;
        dstOpnd->object = NULL;
        dstOpnd->immOffset = 0;
    }

    if (copts.misaligned_mem_access) {
        align = 4;
    }

    store_word_of_small_struct(
        srcRegLo, dstOpnd->reg, dstOpnd, dstOpnd->immOffset + low_offset, type->size - 4, align
    );
    store_word_of_small_struct(
        srcRegHi, dstOpnd->reg, dstOpnd, dstOpnd->immOffset + high_offset, 4, align
    );
}
