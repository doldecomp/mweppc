#ifndef X87_H
#define X87_H

typedef struct X87Extended {
    unsigned char kind;
    unsigned char sign;
    int exponent;
    unsigned long long significand;
} X87Extended;

enum {
    X87_ZERO,
    X87_FINITE,
    X87_INFINITY,
    X87_NAN
};

#define X87_INDEFINITE 0xFFF8000000000000ULL
#define X87_QUIET_BIT 0x0008000000000000ULL

static inline unsigned long long X87_Bits(double value) {
    union {
        double d;
        unsigned long long u;
    } u;
    u.d = value;
    return u.u;
}

static inline double X87_Double(unsigned long long bits) {
    union {
        double d;
        unsigned long long u;
    } u;
    u.u = bits;
    return u.d;
}

static inline int X87_LeadingZeros(unsigned long long value) {
    int n = 0;
    while (!(value & 0x8000000000000000ULL)) {
        value <<= 1;
        n++;
    }
    return n;
}

static inline X87Extended X87_Make(int sign, int exponent, unsigned long long significand) {
    X87Extended x;
    x.kind = significand ? X87_FINITE : X87_ZERO;
    x.sign = sign;
    x.exponent = exponent;
    x.significand = significand;
    if (significand) {
        int shift = X87_LeadingZeros(significand);
        x.significand <<= shift;
        x.exponent -= shift;
    }
    return x;
}

static inline X87Extended X87_FromUInt64(unsigned long long value) {
    return X87_Make(0, 63, value);
}

static inline X87Extended X87_FromInt64(long long value) {
    return value < 0 ? X87_Make(1, 63, 0 - (unsigned long long)value)
                     : X87_Make(0, 63, (unsigned long long)value);
}

static inline X87Extended X87_FromDouble(double value) {
    unsigned long long bits = X87_Bits(value);
    int sign = (int)(bits >> 63);
    int biased = (int)(bits >> 52) & 0x7FF;
    unsigned long long fraction = bits & 0x000FFFFFFFFFFFFFULL;
    X87Extended x;

    if (biased == 0x7FF) {
        x.kind = fraction ? X87_NAN : X87_INFINITY;
        x.sign = sign;
        x.exponent = 0;
        x.significand = bits;
        return x;
    }
    if (biased == 0) {
        return X87_Make(sign, -1074 + 63, fraction);
    }
    return X87_Make(sign, biased - 1023 + 11, fraction | 0x0010000000000000ULL);
}

typedef struct X87Wide {
    int sign;
    int exponent;
    unsigned long long hi, lo;
    int sticky;
} X87Wide;

static inline void X87_ShiftLeft1(X87Wide* w) {
    w->hi = (w->hi << 1) | (w->lo >> 63);
    w->lo <<= 1;
    w->exponent--;
}

static inline void X87_ShiftRight1(X87Wide* w, unsigned carry) {
    w->sticky |= (int)(w->lo & 1);
    w->lo = (w->lo >> 1) | (w->hi << 63);
    w->hi = (w->hi >> 1) | ((unsigned long long)carry << 63);
    w->exponent++;
}

static inline X87Extended X87_Round64(X87Wide w) {
    unsigned long long significand;
    int round, rest;

    if (!w.hi && !w.lo) {
        return X87_Make(w.sign, 0, 0);
    }
    while (!(w.hi & 0x8000000000000000ULL)) {
        X87_ShiftLeft1(&w);
    }
    significand = w.hi;
    round = (int)(w.lo >> 63);
    rest = (w.lo << 1) != 0 || w.sticky;
    if (round && (rest || (significand & 1))) {
        significand++;
        if (!significand) {
            significand = 0x8000000000000000ULL;
            w.exponent++;
        }
    }
    return X87_Make(w.sign, w.exponent, significand);
}

static inline void X87_Multiply64(
    unsigned long long a, unsigned long long b, unsigned long long* hi, unsigned long long* lo
) {
    unsigned long long a0 = a & 0xFFFFFFFF, a1 = a >> 32;
    unsigned long long b0 = b & 0xFFFFFFFF, b1 = b >> 32;
    unsigned long long p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    unsigned long long middle = (p00 >> 32) + (p01 & 0xFFFFFFFF) + (p10 & 0xFFFFFFFF);

    *lo = (middle << 32) | (p00 & 0xFFFFFFFF);
    *hi = p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32);
}

static inline X87Extended X87_MulFinite(X87Extended a, X87Extended b) {
    X87Wide w;
    w.sign = a.sign ^ b.sign;
    w.exponent = a.exponent + b.exponent + 1;
    w.sticky = 0;
    X87_Multiply64(a.significand, b.significand, &w.hi, &w.lo);
    return X87_Round64(w);
}

static inline X87Extended X87_DivFinite(X87Extended a, X87Extended b) {
    X87Wide w;
    unsigned long long remainder = a.significand, divisor = b.significand;
    unsigned carry = 0, bit;
    int i;

    w.sign = a.sign ^ b.sign;
    w.exponent = a.exponent - b.exponent;
    w.hi = w.lo = 0;
    for (i = 0; i < 128; i++) {
        bit = carry || remainder >= divisor;
        if (bit) {
            remainder -= divisor;
        }
        w.hi = (w.hi << 1) | (w.lo >> 63);
        w.lo = (w.lo << 1) | bit;
        carry = (unsigned)(remainder >> 63);
        remainder <<= 1;
    }
    w.sticky = remainder != 0 || carry;
    return X87_Round64(w);
}

static inline X87Extended X87_AddFinite(X87Extended a, X87Extended b) {
    X87Wide w;
    unsigned long long bhi, blo;
    int sticky = 0, shift;

    if (b.exponent > a.exponent || (b.exponent == a.exponent && b.significand > a.significand)) {
        X87Extended t = a;
        a = b;
        b = t;
    }
    shift = a.exponent - b.exponent;
    bhi = b.significand;
    blo = 0;
    if (shift >= 128) {
        sticky = 1;
        bhi = blo = 0;
    } else if (shift >= 64) {
        blo = bhi >> (shift - 64);
        sticky = shift > 64 && (bhi << (128 - shift)) != 0;
        bhi = 0;
    } else if (shift > 0) {
        blo = bhi << (64 - shift);
        bhi >>= shift;
    }
    w.sign = a.sign;
    w.exponent = a.exponent;
    w.hi = a.significand;
    w.lo = 0;
    w.sticky = 0;
    if (a.sign == b.sign) {
        unsigned long long lo = w.lo + blo;
        unsigned long long hi = w.hi + bhi + (lo < blo);
        unsigned carry = hi < w.hi || (hi == w.hi && lo < blo);
        w.hi = hi;
        w.lo = lo;
        w.sticky = sticky;
        if (carry) {
            X87_ShiftRight1(&w, 1);
        }
    } else {
        unsigned long long lo = w.lo - blo - (unsigned long long)sticky;
        unsigned long long borrow = w.lo < blo || (w.lo == blo && sticky);
        w.hi = w.hi - bhi - borrow;
        w.lo = lo;
        w.sticky = sticky;
        if (!w.hi && !w.lo) {
            return X87_Make(0, 0, 0);
        }
    }
    return X87_Round64(w);
}

static inline double X87_ToDouble(X87Extended x) {
    unsigned long long sign = (unsigned long long)x.sign << 63;
    unsigned long long significand, kept, rest, half;
    int exponent, shift;

    switch (x.kind) {
        case X87_ZERO:
            return X87_Double(sign);
        case X87_INFINITY:
            return X87_Double(sign | 0x7FF0000000000000ULL);
        case X87_NAN:
            return X87_Double(x.significand);
    }
    exponent = x.exponent;
    significand = x.significand;
    shift = 11;
    if (exponent < -1022) {
        shift += -1022 - exponent;
        exponent = -1022;
    }
    if (shift >= 64) {
        kept = 0;
        rest = shift == 64 ? significand : 0;
        half = shift == 64 ? 0x8000000000000000ULL : 0;
        if (shift > 64) {
            return X87_Double(sign);
        }
    } else {
        kept = significand >> shift;
        rest = significand & ((1ULL << shift) - 1);
        half = 1ULL << (shift - 1);
    }
    if (rest > half || (rest == half && (kept & 1))) {
        kept++;
    }
    if (kept & 0x0020000000000000ULL) {
        kept >>= 1;
        exponent++;
    }
    if (!(kept & 0x0010000000000000ULL)) {
        return X87_Double(sign | kept);
    }
    if (exponent > 1023) {
        return X87_Double(sign | 0x7FF0000000000000ULL);
    }
    return X87_Double(
        sign | ((unsigned long long)(exponent + 1023) << 52) | (kept & 0x000FFFFFFFFFFFFFULL)
    );
}

static inline double X87_PropagateNaN(X87Extended a, X87Extended b) {
    unsigned long long x = a.significand | X87_QUIET_BIT, y = b.significand;
    unsigned long long mask = 0x000FFFFFFFFFFFFFULL;

    if (a.kind != X87_NAN) {
        return X87_Double(y | X87_QUIET_BIT);
    }
    if (b.kind != X87_NAN || (x & mask) > (y & mask)) {
        return X87_Double(x);
    }
    if ((x & mask) < (y & mask)) {
        return X87_Double(y | X87_QUIET_BIT);
    }
    return X87_Double(x < y ? x : y);
}

enum {
    X87_ADD,
    X87_SUB,
    X87_MUL,
    X87_DIV
};

static inline double X87_Arithmetic(double left, int op, double right) {
    X87Extended a = X87_FromDouble(left), b = X87_FromDouble(right);
    int sign;

    if (a.kind == X87_NAN || b.kind == X87_NAN) {
        return X87_PropagateNaN(a, b);
    }
    if (op == X87_SUB) {
        b.sign ^= 1;
        op = X87_ADD;
    }
    sign = a.sign ^ b.sign;
    switch (op) {
        case X87_ADD:
            if (a.kind == X87_INFINITY || b.kind == X87_INFINITY) {
                if (a.kind == X87_INFINITY && b.kind == X87_INFINITY && a.sign != b.sign) {
                    return X87_Double(X87_INDEFINITE);
                }
                return X87_ToDouble(a.kind == X87_INFINITY ? a : b);
            }
            if (a.kind == X87_ZERO && b.kind == X87_ZERO) {
                return X87_Double((unsigned long long)(a.sign & b.sign) << 63);
            }
            if (a.kind == X87_ZERO) {
                return X87_ToDouble(b);
            }
            if (b.kind == X87_ZERO) {
                return X87_ToDouble(a);
            }
            return X87_ToDouble(X87_AddFinite(a, b));
        case X87_MUL:
            if (a.kind == X87_INFINITY || b.kind == X87_INFINITY) {
                if (a.kind == X87_ZERO || b.kind == X87_ZERO) {
                    return X87_Double(X87_INDEFINITE);
                }
                return X87_Double(((unsigned long long)sign << 63) | 0x7FF0000000000000ULL);
            }
            if (a.kind == X87_ZERO || b.kind == X87_ZERO) {
                return X87_Double((unsigned long long)sign << 63);
            }
            return X87_ToDouble(X87_MulFinite(a, b));
        default:
            if (a.kind == X87_INFINITY) {
                if (b.kind == X87_INFINITY) {
                    return X87_Double(X87_INDEFINITE);
                }
                return X87_Double(((unsigned long long)sign << 63) | 0x7FF0000000000000ULL);
            }
            if (b.kind == X87_INFINITY) {
                return X87_Double((unsigned long long)sign << 63);
            }
            if (b.kind == X87_ZERO) {
                if (a.kind == X87_ZERO) {
                    return X87_Double(X87_INDEFINITE);
                }
                return X87_Double(((unsigned long long)sign << 63) | 0x7FF0000000000000ULL);
            }
            if (a.kind == X87_ZERO) {
                return X87_Double((unsigned long long)sign << 63);
            }
            return X87_ToDouble(X87_DivFinite(a, b));
    }
}

static inline X87Extended X87_Add(X87Extended a, X87Extended b) {
    if (a.kind == X87_ZERO) {
        return b.kind == X87_ZERO ? X87_Make(a.sign & b.sign, 0, 0) : b;
    }
    return b.kind == X87_ZERO ? a : X87_AddFinite(a, b);
}

static inline X87Extended X87_Mul(X87Extended a, X87Extended b) {
    if (a.kind == X87_ZERO || b.kind == X87_ZERO) {
        return X87_Make(a.sign ^ b.sign, 0, 0);
    }
    return X87_MulFinite(a, b);
}

static inline X87Extended X87_Div(X87Extended a, X87Extended b) {
    if (a.kind == X87_ZERO) {
        return X87_Make(a.sign ^ b.sign, 0, 0);
    }
    return X87_DivFinite(a, b);
}

static inline int X87_Less(X87Extended a, X87Extended b) {
    int magnitude;
    if (a.kind == X87_ZERO && b.kind == X87_ZERO) {
        return 0;
    }
    if (a.kind == X87_ZERO || b.kind == X87_ZERO) {
        return a.kind == X87_ZERO ? !b.sign : a.sign;
    }
    if (a.sign != b.sign) {
        return a.sign;
    }
    magnitude = a.exponent != b.exponent ? a.exponent < b.exponent : a.significand < b.significand;
    return a.sign ? !magnitude && (a.exponent != b.exponent || a.significand != b.significand)
                  : magnitude;
}

static inline long long X87_RoundToInteger(X87Extended x) {
    unsigned long long magnitude, rest, half;
    int shift;

    if (x.kind == X87_ZERO) {
        return 0;
    }
    shift = 63 - x.exponent;
    if (shift <= 0) {
        magnitude = x.significand;
    } else if (shift >= 65) {
        magnitude = 0;
    } else {
        magnitude = shift == 64 ? 0 : x.significand >> shift;
        rest = shift == 64 ? x.significand : x.significand & ((1ULL << shift) - 1);
        half = 1ULL << (shift - 1);
        if (rest > half || (rest == half && (magnitude & 1))) {
            magnitude++;
        }
    }
    return x.sign ? -(long long)magnitude : (long long)magnitude;
}

static inline int X87_TruncateToInt32(double value) {
    if (!(value > -2147483649.0 && value < 2147483648.0)) {
        return (int)0x80000000U;
    }
    return (int)value;
}

static inline long long X87_TruncateToInt64(double value) {
    if (!(value >= -9223372036854775808.0 && value < 9223372036854775808.0)) {
        return (long long)0x8000000000000000ULL;
    }
    return (long long)value;
}

static inline double X87_UInt64ToDouble(unsigned long long value) {
    return value ? X87_ToDouble(X87_FromUInt64(value)) : 0.0;
}

#endif
