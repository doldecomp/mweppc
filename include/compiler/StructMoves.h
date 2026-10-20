#ifndef COMPILER_STRUCTMOVES_H
#define COMPILER_STRUCTMOVES_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void move_block(Operand* dst, Operand* src, SInt32 len, SInt32 align);
extern void make_addressable(Operand* operand, SInt32 offset, int n);
extern void load_small_block_into_reg(short dstReg, Operand* srcOpnd, Type* type, SInt32 align);
extern void load_small_block_into_reg_pair(
    short dstRegLo, short dstRegHi, Operand* srcOpnd, Type* type, SInt32 align
);
extern void store_small_block_from_reg(short srcReg, Operand* dstOpnd, Type* type, SInt32 align);
extern void store_small_block_from_reg_pair(
    short srcRegLo, short srcRegHi, Operand* dstOpnd, Type* type, SInt32 align
);

#ifdef __cplusplus
}
#endif

#endif
