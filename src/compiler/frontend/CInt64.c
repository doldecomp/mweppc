#include "compiler/CInt64.h"
#include <string.h>
#ifndef __MWERKS__
#include "x87.h"
#endif

const CInt64 cint64_negone = {0xFFFFFFFF, 0xFFFFFFFF};
const CInt64 cint64_zero = {0, 0};
const CInt64 cint64_one = {0, 1};
const CInt64 cint64_max = {0x7FFFFFFF, 0xFFFFFFFF};
const CInt64 cint64_min = {0x80000000, 0};

#define SHIFT_LEFT_ONE(a, b) \
    do { \
        a <<= 1; \
        if (b & 0x80000000) { \
            a |= 1; \
        } \
        b <<= 1; \
    } while (0)

void CInt64_Init(void) {}

CInt64 CInt64_Not(CInt64 input) {
    CInt64 output;

    CInt64_SetLong(&output, CInt64_IsZero(&input));

    return output;
}

CInt64 CInt64_Inv(CInt64 input) {
    CInt64 output;
    output.hi = ~input.hi;
    output.lo = ~input.lo;
    return output;
}

CInt64 CInt64_Add(CInt64 lhs, CInt64 rhs) {
    if (lhs.lo & 0x80000000) {
        if (rhs.lo & 0x80000000) {
            lhs.lo += rhs.lo;
            lhs.hi += 1;
        } else {
            lhs.lo += rhs.lo;
            if (!(lhs.lo & 0x80000000)) {
                lhs.hi += 1;
            }
        }
    } else {
        if (rhs.lo & 0x80000000) {
            lhs.lo += rhs.lo;
            if (!(lhs.lo & 0x80000000)) {
                lhs.hi += 1;
            }
        } else {
            lhs.lo += rhs.lo;
        }
    }
    lhs.hi += rhs.hi;
    return lhs;
}

CInt64 CInt64_Neg(CInt64 input) {
    CInt64 result;
    result = CInt64_Add(CInt64_Inv(input), cint64_one);
    return result;
}

CInt64 CInt64_Sub(CInt64 lhs, CInt64 rhs) {
    lhs = CInt64_Add(lhs, CInt64_Neg(rhs));
    return lhs;
}

CInt64 CInt64_MulU(CInt64 lhs, CInt64 rhs) {
    CInt64 result;
    CInt64 work1;
    UInt32 bbbb;
    UInt32 cccc;
    UInt32 dddd;
    SInt32 aaaa;
    UInt32 eeee;

    aaaa = rhs.lo;
    result.lo = 0;
    result.hi = 0;
    bbbb = lhs.lo;
    cccc = rhs.hi;

    for (dddd = aaaa; bbbb != 0; dddd <<= 1, bbbb >>= 1) {
        if (bbbb & 1) {
            work1.hi = cccc;
            work1.lo = dddd;
            result = CInt64_Add(result, work1);
        }
        cccc <<= 1;
        if (dddd & 0x80000000) {
            cccc |= 1;
        }
    }

    eeee = lhs.hi;
    while (eeee != 0 && aaaa != 0) {
        if (eeee & 1) {
            result.hi += aaaa;
        }
        aaaa <<= 1;
        eeee >>= 1;
    }

    return result;
}

CInt64 CInt64_Mul(CInt64 lhs, CInt64 rhs) {
    if (CInt64_IsNegative(&rhs)) {
        if (CInt64_IsNegative(&lhs)) {
            return CInt64_MulU(CInt64_Neg(lhs), CInt64_Neg(rhs));
        }
        return CInt64_Neg(CInt64_MulU(lhs, CInt64_Neg(rhs)));
    }
    if (CInt64_IsNegative(&lhs)) {
        return CInt64_Neg(CInt64_MulU(CInt64_Neg(lhs), rhs));
    }
    return CInt64_MulU(lhs, rhs);
}

void CInt64_DivMod(const CInt64* lhs, const CInt64* rhs, CInt64* pDiv, CInt64* pMod) {
    Boolean bad;
    UInt32 workA;
    UInt32 workB;
    UInt32 leftA;
    UInt32 leftB;
    UInt32 rightA;
    UInt32 rightB;
    UInt32 outA;
    UInt32 outB;
    CInt64 work;
    int counter;

    bad = (rhs->hi == 0) && (rhs->lo == 0);
    if (bad) {
        return;
    }

    workA = 0;
    workB = 0;
    leftA = lhs->hi;
    leftB = lhs->lo;
    rightA = rhs->hi;
    rightB = rhs->lo;
    outA = 0;
    outB = 0;
    counter = 0;

    do {
        workA <<= 1;
        if (workB & 0x80000000) {
            workA |= 1;
        }
        workB <<= 1;
        if (leftA & 0x80000000) {
            workB |= 1;
        }
        leftA <<= 1;
        if (leftB & 0x80000000) {
            leftA |= 1;
        }
        leftB <<= 1;

        outA <<= 1;
        if (outB & 0x80000000) {
            outA |= 1;
        }
        outB <<= 1;

        if (workA <= rightA) {
            if (workA != rightA) {
                continue;
            }
            if (workB < rightB) {
                continue;
            }
        }

        outB |= 1;

        work.hi = workA;
        work.lo = workB;
        work = CInt64_Sub(work, *rhs);
        workA = work.hi;
        workB = work.lo;
    } while (++counter < 64);

    if (pDiv) {
        pDiv->hi = outA;
        pDiv->lo = outB;
    }
    if (pMod) {
        pMod->hi = workA;
        pMod->lo = workB;
    }
}

CInt64 CInt64_Div(CInt64 lhs, CInt64 rhs) {
    CInt64 result;
    if (CInt64_IsNegative(&rhs)) {
        rhs = CInt64_Neg(rhs);
        if (CInt64_IsNegative(&lhs)) {
            lhs = CInt64_Neg(lhs);
            CInt64_DivMod(&lhs, &rhs, &result, 0);
            return result;
        } else {
            CInt64_DivMod(&lhs, &rhs, &result, 0);
            return CInt64_Neg(result);
        }
    } else {
        if (CInt64_IsNegative(&lhs)) {
            lhs = CInt64_Neg(lhs);
            CInt64_DivMod(&lhs, &rhs, &result, 0);
            return CInt64_Neg(result);
        } else {
            CInt64_DivMod(&lhs, &rhs, &result, 0);
            return result;
        }
    }
}

CInt64 CInt64_DivU(CInt64 lhs, CInt64 rhs) {
    CInt64 result;
    CInt64_DivMod(&lhs, &rhs, &result, 0);
    return result;
}

CInt64 CInt64_Mod(CInt64 lhs, CInt64 rhs) {
    CInt64 result;
    if (CInt64_IsNegative(&lhs)) {
        lhs = CInt64_Neg(lhs);
        if (CInt64_IsNegative(&rhs)) {
            rhs = CInt64_Neg(rhs);
        }
        CInt64_DivMod(&lhs, &rhs, 0, &result);
        return CInt64_Neg(result);
    } else {
        if (CInt64_IsNegative(&rhs)) {
            rhs = CInt64_Neg(rhs);
        }
        CInt64_DivMod(&lhs, &rhs, 0, &result);
        return result;
    }
}

CInt64 CInt64_ModU(CInt64 lhs, CInt64 rhs) {
    CInt64 result;
    CInt64_DivMod(&lhs, &rhs, 0, &result);
    return result;
}

CInt64 CInt64_Shl(CInt64 lhs, CInt64 rhs) {
    UInt32 hi;
    UInt32 lo;
    int counter;

    if (rhs.hi == 0 && rhs.lo < 64) {
        hi = lhs.hi;
        lo = lhs.lo;
        for (counter = rhs.lo; counter != 0; --counter) {
            hi <<= 1;
            if (lo & 0x80000000) {
                hi |= 1;
            }
            lo <<= 1;
        }
        lhs.hi = hi;
        lhs.lo = lo;
    } else {
        lhs.hi = 0;
        lhs.lo = 0;
    }
    return lhs;
}

CInt64 CInt64_Shr(CInt64 lhs, CInt64 rhs) {
    SInt32 hi;
    UInt32 lo;
    int counter;

    if (rhs.hi == 0 && rhs.lo < 64) {
        hi = lhs.hi;
        lo = lhs.lo;
        for (counter = rhs.lo; counter != 0; --counter) {
            lo >>= 1;
            if (hi & 1) {
                lo |= 0x80000000;
            }
            hi >>= 1;
        }
        lhs.hi = hi;
        lhs.lo = lo;
    } else {
        if (CInt64_IsNegative(&lhs)) {
            lhs.hi = 0xFFFFFFFF;
            lhs.lo = 0xFFFFFFFF;
        } else {
            lhs.hi = 0;
            lhs.lo = 0;
        }
    }
    return lhs;
}

CInt64 CInt64_ShrU(CInt64 lhs, CInt64 rhs) {
    UInt32 hi;
    UInt32 lo;
    int counter;

    if (rhs.hi == 0 && rhs.lo < 64) {
        hi = lhs.hi;
        lo = lhs.lo;
        for (counter = rhs.lo; counter != 0; --counter) {
            lo >>= 1;
            if (hi & 1) {
                lo |= 0x80000000;
            }
            hi >>= 1;
        }
        lhs.hi = hi;
        lhs.lo = lo;
    } else {
        lhs.hi = 0;
        lhs.lo = 0;
    }
    return lhs;
}

int CInt64_UnsignedCompare(const CInt64* lhs, const CInt64* rhs) {
    if (lhs->hi == rhs->hi) {
        if (lhs->lo < rhs->lo) {
            return -1;
        } else if (lhs->lo > rhs->lo) {
            return 1;
        } else {
            return 0;
        }
    } else if ((UInt32)lhs->hi < (UInt32)rhs->hi) {
        return -1;
    } else {
        return 1;
    }
}

int CInt64_SignedCompare(const CInt64* lhs, const CInt64* rhs) {
    CInt64 lhs_;
    CInt64 rhs_;

    lhs_ = CInt64_Xor(*lhs, cint64_min);
    rhs_ = CInt64_Xor(*rhs, cint64_min);
    return CInt64_UnsignedCompare(&lhs_, &rhs_);
}

Boolean CInt64_Less(CInt64 lhs, CInt64 rhs) {
    return CInt64_SignedCompare(&lhs, &rhs) < 0;
}

Boolean CInt64_LessU(CInt64 lhs, CInt64 rhs) {
    return CInt64_UnsignedCompare(&lhs, &rhs) < 0;
}

Boolean CInt64_Greater(CInt64 lhs, CInt64 rhs) {
    return CInt64_SignedCompare(&lhs, &rhs) > 0;
}

Boolean CInt64_GreaterU(CInt64 lhs, CInt64 rhs) {
    return CInt64_UnsignedCompare(&lhs, &rhs) > 0;
}

Boolean CInt64_LessEqual(CInt64 lhs, CInt64 rhs) {
    return CInt64_SignedCompare(&lhs, &rhs) <= 0;
}

Boolean CInt64_LessEqualU(CInt64 lhs, CInt64 rhs) {
    return CInt64_UnsignedCompare(&lhs, &rhs) <= 0;
}

Boolean CInt64_GreaterEqual(CInt64 lhs, CInt64 rhs) {
    return CInt64_SignedCompare(&lhs, &rhs) >= 0;
}

Boolean CInt64_GreaterEqualU(CInt64 lhs, CInt64 rhs) {
    return CInt64_UnsignedCompare(&lhs, &rhs) >= 0;
}

Boolean CInt64_Equal(CInt64 lhs, CInt64 rhs) {
    return lhs.hi == rhs.hi && lhs.lo == rhs.lo;
}

Boolean CInt64_NotEqual(CInt64 lhs, CInt64 rhs) {
    return lhs.hi != rhs.hi || lhs.lo != rhs.lo;
}

Boolean CInt64_IsInRange(CInt64 value, short len) {
    CInt64 bound;

    if (CInt64_IsNegative(&value)) {
        switch (len) {
            case 1:
                bound.lo = 0xFFFFFF80;
                bound.hi = 0xFFFFFFFF;
                break;
            case 2:
                bound.lo = 0xFFFF8000;
                bound.hi = 0xFFFFFFFF;
                break;
            case 4:
                bound.lo = 0x80000000;
                bound.hi = 0xFFFFFFFF;
                break;
            case 8:
                return TRUE;
            default:
                return FALSE;
        }
        return CInt64_GreaterEqual(value, bound);
    } else {
        switch (len) {
            case 1:
                bound.lo = 0x7F;
                bound.hi = 0;
                break;
            case 2:
                bound.lo = 0x7FFF;
                bound.hi = 0;
                break;
            case 4:
                bound.lo = 0x7FFFFFFF;
                bound.hi = 0;
                break;
            case 8:
                return TRUE;
            default:
                return FALSE;
        }
        return CInt64_LessEqual(value, bound);
    }
}

Boolean CInt64_IsInURange(CInt64 value, short len) {
    SInt32 hi = value.hi;
    switch (len) {
        case 1:
            return hi == 0 && (value.lo & 0xFFFFFF00) == 0;
        case 2:
            return hi == 0 && (value.lo & 0xFFFF0000) == 0;
        case 4:
            return hi == 0;
        case 8:
            return TRUE;
        default:
            return FALSE;
    }
}

CInt64 CInt64_And(CInt64 lhs, CInt64 rhs) {
    lhs.hi &= rhs.hi;
    lhs.lo &= rhs.lo;
    return lhs;
}

CInt64 CInt64_Xor(CInt64 lhs, CInt64 rhs) {
    lhs.hi ^= rhs.hi;
    lhs.lo ^= rhs.lo;
    return lhs;
}

CInt64 CInt64_Or(CInt64 lhs, CInt64 rhs) {
    lhs.hi |= rhs.hi;
    lhs.lo |= rhs.lo;
    return lhs;
}

void CInt64_ConvertInt32(CInt64* i) {
    i->hi = (i->lo & 0x80000000) ? 0xFFFFFFFF : 0;
}

void CInt64_ConvertUInt32(CInt64* i) {
    i->hi = 0;
}

void CInt64_ConvertInt16(CInt64* i) {
    i->lo = (SInt16)i->lo;
    i->hi = (i->lo & 0x80000000) ? 0xFFFFFFFF : 0;
}

void CInt64_ConvertUInt16(CInt64* i) {
    i->lo = (UInt16)i->lo;
    i->hi = 0;
}

void CInt64_ConvertInt8(CInt64* i) {
    i->lo = (SInt8)i->lo;
    i->hi = (i->lo & 0x80000000) ? 0xFFFFFFFF : 0;
}

void CInt64_ConvertUInt8(CInt64* i) {
    i->lo = (UInt8)i->lo;
    i->hi = 0;
}

static Boolean powers_built;
static float next_power;
static float powers_of_two[65];

static inline float power_of_two(short n) {
    int i;

    if (!powers_built) {
        i = 0;
        next_power = 1.0f;
        do {
            powers_of_two[i++] = next_power;
            next_power += next_power;
        } while (i < 65);
        powers_built = TRUE;
    }
    return powers_of_two[n];
}

void CInt64_ConvertUFromLongDouble(CInt64* pResult, double value) {
    UInt32 hi, lo;
    float threshold;
    int bits;

    if (value <= 0.0) {
        pResult->hi = 0;
        pResult->lo = 0;
        return;
    }

    if (value >= power_of_two(64)) {
        pResult->hi = 0xFFFFFFFF;
        pResult->lo = 0xFFFFFFFF;
        return;
    }

    hi = lo = 0;
    for (bits = 63; bits >= 0; bits--) {
        hi <<= 1;
        if (lo & 0x80000000) {
            hi |= 1;
        }
        lo <<= 1;

        if ((threshold = power_of_two((short)bits)) <= value) {
            lo |= 1;
            value -= threshold;
        }
    }

    pResult->hi = hi;
    pResult->lo = lo;
}

void CInt64_ConvertFromLongDouble(CInt64* pResult, double value) {
    if (value < 0.0) {
        CInt64_ConvertUFromLongDouble(pResult, -value);
        *pResult = CInt64_Neg(*pResult);
    } else {
        CInt64_ConvertUFromLongDouble(pResult, value);
    }
}

double CInt64_ConvertUToLongDouble(const CInt64* value) {
#ifdef __MWERKS__
    Boolean bad;
    UInt32 work;
    int counter;
    double result;
    float one = 1.0f;

    bad = (value->hi == 0) && (value->lo == 0);
    if (bad) {
        return 0.0;
    } else {
        result = 0.0;

        work = value->hi;
        if (work != 0) {
            for (counter = 0; counter < 32; counter++) {
                result += result;
                if (work & 0x80000000) {
                    result += 1.0;
                }
                work <<= 1;
            }
        }

        counter = 0;
        work = value->lo;
        for (; counter < 32; counter++) {
            result += result;
            if (work & 0x80000000) {
                result += one;
            }
            work <<= 1;
        }

        return result;
    }
#else
    return X87_UInt64ToDouble(((unsigned long long)value->hi << 32) | value->lo);
#endif
}

double CInt64_ConvertToLongDouble(const CInt64* value) {
    CInt64 magnitude;
    if (CInt64_IsNegative(value)) {
        magnitude = CInt64_Neg(*value);
        return -CInt64_ConvertUToLongDouble(&magnitude);
    } else {
        return CInt64_ConvertUToLongDouble(value);
    }
}

char* CInt64_ScanOctString(CInt64* pResult, char* str, Boolean* pFail) {
    char ch;
    CInt64 addend;
    UInt32 hi;
    UInt32 lo;

    *pFail = FALSE;
    pResult->lo = 0;
    pResult->hi = 0;

    while ((ch = *str) >= '0' && ch <= '7') {
        hi = pResult->hi;
        lo = pResult->lo;
        if (hi & 0xE0000000) {
            *pFail = TRUE;
        }

        SHIFT_LEFT_ONE(hi, lo);
        SHIFT_LEFT_ONE(hi, lo);
        SHIFT_LEFT_ONE(hi, lo);

        pResult->hi = hi;
        pResult->lo = lo;

        CInt64_SetLong(&addend, ch - '0');
        *pResult = CInt64_Add(*pResult, addend);
        ++str;
    }

    return str;
}

char* CInt64_ScanDecString(CInt64* pResult, char* str, Boolean* pFail) {
    char ch;
    CInt64 addend;
    UInt32 hi;
    UInt32 lo;

    *pFail = FALSE;
    pResult->lo = 0;
    pResult->hi = 0;

    while ((ch = *str) >= '0' && ch <= '9') {
        hi = pResult->hi;
        lo = pResult->lo;
        if (hi & 0xE0000000) {
            *pFail = TRUE;
        }

        SHIFT_LEFT_ONE(hi, lo);
        addend.hi = hi;
        addend.lo = lo;
        SHIFT_LEFT_ONE(hi, lo);
        SHIFT_LEFT_ONE(hi, lo);
        pResult->hi = hi;
        pResult->lo = lo;

        if (CInt64_IsNegative(pResult)) {
            *pResult = CInt64_Add(*pResult, addend);
            if (!CInt64_IsNegative(pResult)) {
                *pFail = TRUE;
            }
        } else {
            *pResult = CInt64_Add(*pResult, addend);
        }

        CInt64_SetLong(&addend, ch - '0');
        if (CInt64_IsNegative(pResult)) {
            *pResult = CInt64_Add(*pResult, addend);
            if (!CInt64_IsNegative(pResult)) {
                *pFail = TRUE;
            }
        } else {
            *pResult = CInt64_Add(*pResult, addend);
        }

        ++str;
    }

    return str;
}

char* CInt64_ScanHexString(CInt64* pResult, char* str, Boolean* pFail) {
    char digit;
    CInt64 addend;
    UInt32 hi;
    UInt32 lo;

    *pFail = FALSE;
    pResult->lo = 0;
    pResult->hi = 0;

    for (;;) {
        digit = str[0];
        if (digit >= '0' && digit <= '9') {
            digit -= '0';
        } else if (digit >= 'A' && digit <= 'F') {
            digit -= 'A' - 10;
        } else if (digit >= 'a' && digit <= 'f') {
            digit -= 'a' - 10;
        } else {
            break;
        }

        ++str;
        hi = pResult->hi;
        lo = pResult->lo;

        if (hi & 0xF0000000) {
            *pFail = TRUE;
        }

        SHIFT_LEFT_ONE(hi, lo);
        SHIFT_LEFT_ONE(hi, lo);
        SHIFT_LEFT_ONE(hi, lo);
        SHIFT_LEFT_ONE(hi, lo);

        pResult->hi = hi;
        pResult->lo = lo;

        CInt64_SetLong(&addend, digit);
        *pResult = CInt64_Add(*pResult, addend);
    }

    return str;
}

char* CInt64_ScanBinString(CInt64* pResult, char* str, Boolean* pFail) {
    char digit;
    UInt32 hi;
    UInt32 lo;

    *pFail = FALSE;
    pResult->lo = 0;
    pResult->hi = 0;

    for (;;) {
        if (*str == '0') {
            digit = 0;
        } else if (*str == '1') {
            digit = 1;
        } else {
            break;
        }

        ++str;
        hi = pResult->hi;
        lo = pResult->lo;

        if (hi & 0x80000000) {
            *pFail = TRUE;
        }

        SHIFT_LEFT_ONE(hi, lo);

        pResult->hi = hi;
        pResult->lo = lo;

        if (digit == 1) {
            *pResult = CInt64_Add(*pResult, cint64_one);
        }
    }

    return str;
}

char* CInt64_ScanAsmNumber(CInt64* pResult, char* str, Boolean* pFail) {
    Boolean isMaybeBin;
    Boolean isOct;
    Boolean isMaybeDec;
    Boolean isBin;
    char* p;

    isMaybeBin = TRUE;
    isOct = *str == '0';
    isMaybeDec = TRUE;
    isBin = FALSE;
    p = str;

    for (;;) {
        if (*p == 0) {
            break;
        }

        if (strchr("01", p[0])) {
            if (isBin) {
                isMaybeBin = FALSE;
            }
        } else if (strchr("bB", p[0])) {
            isBin = TRUE;
            isMaybeDec = FALSE;
            isOct = FALSE;
        } else if (strchr("234567", p[0])) {
            isMaybeBin = FALSE;
        } else if (strchr("89", p[0])) {
            isOct = FALSE;
            isMaybeBin = FALSE;
        } else if (strchr("acdefACEDF", p[0])) {
            isMaybeDec = FALSE;
            isMaybeBin = FALSE;
        } else {
            break;
        }

        ++p;
    }

    if (isMaybeBin && isBin) {
        return CInt64_ScanBinString(pResult, str, pFail) + 1;
    } else if (p[0] == 'h' || p[0] == 'H') {
        return CInt64_ScanHexString(pResult, str, pFail) + 1;
    } else if (isOct) {
        return CInt64_ScanOctString(pResult, str, pFail);
    } else if (isMaybeDec) {
        return CInt64_ScanDecString(pResult, str, pFail);
    } else {
        *pFail = TRUE;
        return p;
    }
}

int CInt64_PrintDec(char* output, CInt64 value) {
    int length;
    CInt64 rem;
    CInt64 divisor;
    char buf[32];
    char* bufp;

    length = 0;
    if (CInt64_IsNegative(&value)) {
        value = CInt64_Neg(value);
        *output = '-';
        output++;
        length++;
    }

    if (!CInt64_IsZero(&value)) {
        divisor.lo = 10;
        divisor.hi = 0;

        bufp = buf;
        for (;;) {
            rem = CInt64_ModU(value, divisor);
            *(bufp++) = rem.lo + '0';
            value = CInt64_DivU(value, divisor);
            if (CInt64_IsZero(&value)) {
                break;
            }
        }

        while (--bufp >= buf) {
            *(output++) = *bufp;
            length++;
        }
    } else {
        *(output++) = '0';
        length++;
    }

    *output = 0;
    return length;
}

int CInt64_PrintHex(char* output, CInt64 value) {
    int length;
    CInt64 rem;
    CInt64 shift;
    CInt64 mask;
    char buf[32];
    char* bufp;

    length = 0;
    if (!CInt64_IsZero(&value)) {
        shift.lo = 4;
        shift.hi = 0;
        mask.lo = 0xF;
        mask.hi = 0;

        bufp = buf;
        for (;;) {
            rem = CInt64_And(value, mask);
            if ((SInt32)rem.lo >= 10) {
                *(bufp++) = rem.lo + 'A';
            } else {
                *(bufp++) = rem.lo + '0';
            }
            value = CInt64_ShrU(value, shift);
            if (CInt64_IsZero(&value)) {
                break;
            }
        }

        while (--bufp >= buf) {
            *(output++) = *bufp;
            length++;
        }
    } else {
        *(output++) = '0';
        length++;
    }

    *output = 0;
    return length;
}

int CInt64_PrintBin(char* output, CInt64 value) {
    int length;
    CInt64 rem;
    char buf[64];
    char* bufp;

    length = 0;
    if (!CInt64_IsZero(&value)) {
        bufp = buf;
        for (;;) {
            rem = CInt64_And(value, cint64_one);
            if (CInt64_Equal(rem, cint64_one)) {
                *(bufp++) = '1';
            } else {
                *(bufp++) = '0';
            }
            value = CInt64_ShrU(value, cint64_one);
            if (CInt64_IsZero(&value)) {
                break;
            }
        }

        while (--bufp >= buf) {
            *(output++) = *bufp;
            length++;
        }
    } else {
        *(output++) = '0';
        length++;
    }

    *output = 0;
    return length;
}
