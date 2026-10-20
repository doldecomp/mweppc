#ifndef COMPILER_BITVECTORS_H
#define COMPILER_BITVECTORS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: invented names: BitVectorRange, firstword, nwords, words
// 2.5 and later: a vector of the bits [first, first + count), stored from word first >> 5
typedef struct BitVectorRange {
    SInt32 firstword;
    SInt32 nwords;
    UInt32 words[1];
} BitVectorRange;

// NOTE: invented names: CompressedBitRun, CompressedBitVector and their members firstword, nwords,
// words, nruns, runs
// 2.5 and later: a vector stored as its runs of nonzero words (bitvector_compress)
typedef struct CompressedBitRun {
    UInt16 firstword;
    UInt16 nwords;
    UInt32 words[1];
} CompressedBitRun;
typedef struct CompressedBitVector {
    UInt16 nwords; // this record's size in words: a multiple of 8 (1 for the empty vector)
    UInt16 nruns;
    CompressedBitRun runs[1];
} CompressedBitVector;

extern void bitvectorcopy(UInt32* dst, UInt32* src, SInt32 len);
extern int bitvectorchanged(UInt32* dst, UInt32* src, SInt32 len);
extern void bitvectorinitialize(UInt32* vec, SInt32 len, UInt32 initval);
extern void bitvectorintersect(UInt32* dst, UInt32* src, SInt32 len);
extern void bitvectorunion(UInt32* dst, UInt32* src, SInt32 len);
extern int bitvectorintersectionisempty(UInt32* a, UInt32* b, int len);

// 2.5 and later (Wii's names)
extern BitVectorRange* bitvectorcreate_range(SInt32 first, SInt32 count);
extern void bitvectorinitialize_range(
    BitVectorRange* range, SInt32 first, SInt32 count, UInt32 value
);
extern CompressedBitVector* bitvector_compress(UInt32* vec, int bitCount);
extern CompressedBitVector* bitvector_empty_compressed(void);
extern void init_compressed_bitvectors(int bitCount);

#define bitvectorgetbit(_bit, _vec) ((1 << ((_bit) & 31)) & (_vec)[(_bit) >> 5])
#define bitvectorsetbit(_bit, _vec) ((_vec)[(_bit) >> 5] |= 1 << ((_bit) & 31))
#define bitvectorclearbit(_bit, _vec) ((_vec)[(_bit) >> 5] &= ~(1 << ((_bit) & 31)))

// NOTE: invented names: bitvectorgetbit_range, bitvectorsetbit_range, bitvectorclearbit_range
// 2.5 and later: the bits of a BitVectorRange (outside the range: clear, and writes are ignored)
static inline char bitvectorgetbit_range(SInt32 bit, BitVectorRange* range) {
    int word = (bit >> 5) - range->firstword;
    if (word < 0 || word >= range->nwords) {
        return 0;
    }
    return (range->words[word] >> (bit & 31)) & 1;
}

static inline void bitvectorsetbit_range(SInt32 bit, BitVectorRange* range) {
    int word = (bit >> 5) - range->firstword;
    if (word >= 0 && word < range->nwords) {
        range->words[word] |= 1 << (bit & 31);
    }
}

static inline void bitvectorclearbit_range(SInt32 bit, BitVectorRange* range) {
    int word = (bit >> 5) - range->firstword;
    if (word >= 0 && word < range->nwords) {
        range->words[word] &= ~(1 << (bit & 31));
    }
}

#ifdef __cplusplus
}
#endif

#endif
