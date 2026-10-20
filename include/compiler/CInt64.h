#ifndef COMPILER_CINT64_H
#define COMPILER_CINT64_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const CInt64 cint64_negone;
extern const CInt64 cint64_zero;
extern const CInt64 cint64_one;
extern const CInt64 cint64_max;
extern const CInt64 cint64_min;

HEADER_INLINE Boolean CInt64_IsNegative(const CInt64* n) {
    return (n->hi & 0x80000000) != 0;
}
HEADER_INLINE UInt32 CInt64_GetULong(const CInt64* n) {
    return n->lo;
}
HEADER_INLINE void CInt64_SetLong(CInt64* pN, SInt32 n) {
    pN->lo = n;
    pN->hi = (n < 0) ? 0xFFFFFFFF : 0;
}
HEADER_INLINE void CInt64_SetULong(CInt64* pN, UInt32 n) {
    pN->lo = n;
    pN->hi = 0;
}

HEADER_INLINE Boolean CInt64_IsZero(CInt64* n) {
    return n->hi == 0 && n->lo == 0;
}
HEADER_INLINE Boolean CInt64_IsOne(CInt64* n) {
    return n->hi == 0 && n->lo == 1;
}

extern void CInt64_Init(void);
extern CInt64 CInt64_Not(CInt64 input);
extern CInt64 CInt64_Inv(CInt64 input);
extern CInt64 CInt64_Add(CInt64 lhs, CInt64 rhs);
extern CInt64 CInt64_Neg(CInt64 input);
extern CInt64 CInt64_Sub(CInt64 lhs, CInt64 rhs);
extern CInt64 CInt64_MulU(CInt64 lhs, CInt64 rhs);
extern CInt64 CInt64_Mul(CInt64 lhs, CInt64 rhs);
extern CInt64 CInt64_Div(CInt64 lhs, CInt64 rhs);
extern CInt64 CInt64_DivU(CInt64 lhs, CInt64 rhs);
extern CInt64 CInt64_Mod(CInt64 lhs, CInt64 rhs);
extern CInt64 CInt64_ModU(CInt64 lhs, CInt64 rhs);
extern CInt64 CInt64_Shl(CInt64 lhs, CInt64 rhs);
extern CInt64 CInt64_Shr(CInt64 lhs, CInt64 rhs);
extern CInt64 CInt64_ShrU(CInt64 lhs, CInt64 rhs);
extern Boolean CInt64_Less(CInt64 lhs, CInt64 rhs);
extern Boolean CInt64_LessU(CInt64 lhs, CInt64 rhs);
extern Boolean CInt64_Greater(CInt64 lhs, CInt64 rhs);
extern Boolean CInt64_GreaterU(CInt64 lhs, CInt64 rhs);
extern Boolean CInt64_LessEqual(CInt64 lhs, CInt64 rhs);
extern Boolean CInt64_LessEqualU(CInt64 lhs, CInt64 rhs);
extern Boolean CInt64_GreaterEqual(CInt64 lhs, CInt64 rhs);
extern Boolean CInt64_GreaterEqualU(CInt64 lhs, CInt64 rhs);
extern Boolean CInt64_Equal(CInt64 lhs, CInt64 rhs);
extern Boolean CInt64_NotEqual(CInt64 lhs, CInt64 rhs);
extern Boolean CInt64_IsInRange(CInt64 value, short len);
extern Boolean CInt64_IsInURange(CInt64 value, short len);
extern CInt64 CInt64_And(CInt64 lhs, CInt64 rhs);
extern CInt64 CInt64_Xor(CInt64 lhs, CInt64 rhs);
extern CInt64 CInt64_Or(CInt64 lhs, CInt64 rhs);
extern void CInt64_ConvertInt32(CInt64* i);
extern void CInt64_ConvertUInt32(CInt64* i);
extern void CInt64_ConvertInt16(CInt64* i);
extern void CInt64_ConvertUInt16(CInt64* i);
extern void CInt64_ConvertInt8(CInt64* i);
extern void CInt64_ConvertUInt8(CInt64* i);
extern void CInt64_ConvertUFromLongDouble(CInt64* pResult, double value);
extern void CInt64_ConvertFromLongDouble(CInt64* pResult, double value);
extern double CInt64_ConvertUToLongDouble(const CInt64* value);
extern double CInt64_ConvertToLongDouble(const CInt64* value);
extern char* CInt64_ScanOctString(CInt64* pResult, char* str, Boolean* pFail);
extern char* CInt64_ScanDecString(CInt64* pResult, char* str, Boolean* pFail);
extern char* CInt64_ScanHexString(CInt64* pResult, char* str, Boolean* pFail);
extern char* CInt64_ScanBinString(CInt64* pResult, char* str, Boolean* pFail);
extern int CInt64_PrintDec(char* output, CInt64 value);

#ifdef __cplusplus
}
#endif

#endif
