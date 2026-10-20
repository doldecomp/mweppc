#include "compiler/BitVector.h"
#include "compiler/CError.h"
#include "compiler/CompilerTools.h"
#include <string.h>

void Bv_AllocVector(BitVector** bv, UInt32 size) {
    size = (size / 32) + 1;
    *bv = oalloc(sizeof(BitVector) + sizeof(UInt32) * size);
    (*bv)->size = size;
    Bv_Clear(*bv);
}

// (nothing calls it: the linker drops it)
void Bv_AllocVectorLocal(BitVector** bv, UInt32 size) {
    UInt32 long_size = (size / 32) + 1;
    *bv = lalloc(sizeof(BitVector) + sizeof(UInt32) * long_size);
    (*bv)->size = long_size;
    Bv_Clear(*bv);
}

void Bv_ClearBit(UInt32 bit, BitVector* bv) {
    if ((bit / 32) < bv->size) {
        bv->data[bit / 32] &= ~(1 << (bit & 31));
    } else {
        CError_FATAL(73);
    }
}

void Bv_And(const BitVector* a, BitVector* b) {
    UInt32 i;
    for (i = 0; i < b->size; i++) {
        b->data[i] &= a->data[i];
    }
}

void Bv_Or(const BitVector* a, BitVector* b) {
    UInt32 i, len;

    len = a->size;
    if (b->size < len) {
        len = b->size;
    }

    for (i = 0; i < len; i++) {
        b->data[i] |= a->data[i];
    }
}

Boolean Bv_BitsInCommon(const BitVector* a, const BitVector* b) {
    UInt32 len;
    UInt32 i;

    len = a->size;
    if (b->size < len) {
        len = b->size;
    }

    for (i = 0; i < len; i++) {
        if (a->data[i] & b->data[i]) {
            return TRUE;
        }
    }

    return FALSE;
}

Boolean Bv_Compare(const BitVector* a, const BitVector* b) {
    UInt32 i;
    for (i = 0; i < a->size; i++) {
        if (a->data[i] != b->data[i]) {
            return FALSE;
        }
    }

    return TRUE;
}

void Bv_Minus(const BitVector* a, BitVector* b) {
    UInt32 i;
    for (i = 0; i < b->size; i++) {
        b->data[i] &= ~a->data[i];
    }
}

void Bv_Copy(const BitVector* src, BitVector* dst) {
    memcpy(dst->data, src->data, sizeof(UInt32) * dst->size);
}

void Bv_Clear(BitVector* bv) {
    memset(bv->data, 0, sizeof(UInt32) * bv->size);
}

void Bv_Set(BitVector* bv) {
    memset(bv->data, 0xFF, sizeof(UInt32) * bv->size);
}

Boolean Bv_IsSubset(const BitVector* a, const BitVector* b) {
    int i;

    for (i = 0; (UInt32)i < a->size; i++) {
        // (the original's: `i == b->size` still reads b's data, one word past its end)
        if (b->size < (UInt32)i) {
            if (a->data[i]) {
                return FALSE;
            }
        } else {
            if (a->data[i] & ~(b->data[i])) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

Boolean Bv_IsEmpty(const BitVector* bv) {
    UInt32 i;

    for (i = 0; i < bv->size; i++) {
        if (bv->data[i]) {
            return FALSE;
        }
    }

    return TRUE;
}
