#ifndef COMPILER_CMACHINE_H
#define COMPILER_CMACHINE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern TypeIntegral stbool;
extern TypeIntegral stchar;
extern TypeIntegral stsignedchar;
extern TypeIntegral stunsignedchar;
extern TypeIntegral stsignedshort;
extern TypeIntegral stunsignedshort;
extern TypeIntegral stsignedint;
extern TypeIntegral stunsignedint;
extern TypeIntegral stsignedlong;
extern TypeIntegral stunsignedlong;
extern TypeIntegral stsignedlonglong;
extern TypeIntegral stunsignedlonglong;
extern TypeIntegral stfloat;
extern TypeIntegral stshortdouble;
extern TypeIntegral stdouble;
extern TypeIntegral stlongdouble;
extern TypeIntegral stwchar;
extern TypeIntegral stvec2x32float;
extern TypeStruct stvectorunsignedchar;
extern TypeStruct stvectorsignedchar;
extern TypeStruct stvectorboolchar;
extern TypeStruct stvectorunsignedshort;
extern TypeStruct stvectorsignedshort;
extern TypeStruct stvectorboolshort;
extern TypeStruct stvectorunsignedlong;
extern TypeStruct stvectorsignedlong;
extern TypeStruct stvectorboollong;
extern TypeStruct stvectorfloat;
extern TypeStruct stvectorpixel;
extern TypeStruct stvector;

extern void CMach_Configure(void);
extern SInt32 CMach_ArgumentAlignment(Type* type);
extern SInt32 CMach_AllocationAlignment(Type* type, UInt32 qual);
extern CInt64 CMach_CalcIntDiadic(Type* type, CInt64 left, short op, CInt64 right);
extern CInt64 CMach_CalcIntMonadic(Type* type, short op, CInt64 val);
extern CInt64 CMach_CalcIntConvertFromFloat(Type* type, Float fval);
extern void CMach_InitIntMem(Type* type, CInt64 val, void* mem);
extern void CMach_InitVectorMem(Type* type, MWVector128 val, void* mem, Boolean endian_convert);
extern Float CMach_CalcFloatDiadic(Type* type, Float left, short op, Float right);
extern Float CMach_CalcFloatMonadic(Type* type, short op, Float fval);
extern Boolean CMach_CalcFloatDiadicBool(Type* type, Float left, short op, Float right);
extern char* CMach_FloatScan(char* input, Float* result, Boolean* fail);
extern Float CMach_CalcFloatConvertFromInt(Type* type, CInt64 val);
extern Float CMach_CalcFloatConvert(Type* type, Float fval);
extern Boolean CMach_FloatIsZero(Float fval);
extern Boolean CMach_FloatIsOne(Float fval);
extern Boolean CMach_FloatIsNegOne(Float fval);
extern void CMach_InitFloatMem(Type* type, Float val, void* mem);
extern void CMach_PrintFloat(char* buf, Float val);
extern void CMach_PragmaParams(void);
extern UInt16 CMach_GetStructAlign(TypeStruct* str);
extern SInt16 CMach_GetClassAlign(TypeClass* tclass);
extern SInt16 CMach_GetTypeAlign(Type* type);
extern SInt16 CMach_MemberAlignValue(Type* type, SInt32 offset);
extern void CMach_StructLayoutInitOffset(SInt32 offset);
extern SInt32 CMach_StructLayoutGetCurSize(void);
extern SInt32 CMach_StructLayoutGetOffset(Type* type, UInt32 qual);
extern SInt32 CMach_StructLayoutBitfield(TypeBitfield* tbitfield, UInt32 qual);
extern UInt8 CMach_GetFunctionResultClass(TypeFunc* functype);
extern Boolean Type_RequiresMemoryReturn(Type* type);
extern Boolean CMach_PassResultInHiddenArg(Type* type);
extern const char* CMach_GetCPU(void);
extern Boolean CMach_FloatIsPowerOf2(Float flt);
extern Boolean CMach_IsUnsignedType(Type* type);
extern Type* CMach_GetCustomType(short token, short size, short signedness);
extern CInt64 CMach_CustomIntConvert(Type* type, Type* oldtype, CInt64 value);
extern Float CMach_FloatReciprocal(Float flt);
extern SInt32 CMach_RoundedSizeOf(Object* object);
extern void CMach_ReInitRuntimeObjects(void);
extern void CMach_ParseGlobalRegisterVariable(DeclInfo* declinfo);
extern void CMach_GetBackEndOptions(struct CInlineBackendOptions* options);
extern void CMach_SetBackEndOptions(struct CInlineBackendOptions* options);

#ifdef __cplusplus
}
#endif

#endif
