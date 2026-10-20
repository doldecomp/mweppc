#ifndef COMPILER_BITVECTOR_H
#define COMPILER_BITVECTOR_H

#include "compiler/common.h"
#include "compiler/CError.h"

#ifdef __cplusplus
extern "C" {
#endif

// The optimizer's bit vectors (IroBitVect.c).
struct BitVector {
    UInt32 size; // in 32-bit words
    UInt32 data[0];
};

extern void Bv_AllocVector(BitVector** bv, UInt32 size);
extern void Bv_ClearBit(UInt32 bit, BitVector* bv);
extern void Bv_And(const BitVector* a, BitVector* b);
extern void Bv_Or(const BitVector* a, BitVector* b);
extern Boolean Bv_BitsInCommon(const BitVector* a, const BitVector* b);
extern Boolean Bv_Compare(const BitVector* a, const BitVector* b);
extern void Bv_Minus(const BitVector* a, BitVector* b);
extern void Bv_Copy(const BitVector* src, BitVector* dst);
extern void Bv_Clear(BitVector* bv);
extern void Bv_Set(BitVector* bv);
extern Boolean Bv_IsSubset(const BitVector* a, const BitVector* b);
extern Boolean Bv_IsEmpty(const BitVector* bv);

// Each file that uses it gets it inlined, asserting with this header's name; a call that is not
// inlined goes to the one copy the linker keeps.
HEADER_INLINE void Bv_SetBit(UInt32 bit, BitVector* bv) {
    if ((bit / 32) < bv->size) {
        bv->data[bit / 32] |= 1 << (bit & 31);
    } else {
        CError_FATAL(56);
    }
}

#define Bv_IsBitSet(_bit, _bv) \
    (((UInt32)((_bit) >> 5) < (_bv)->size) && ((_bv)->data[(_bit) >> 5] & (1 << ((_bit) & 31))))

#ifdef __cplusplus
}
#endif

#endif
