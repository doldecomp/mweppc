#include "compiler/CMachine.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CInline.h"
#include "compiler/CInt64.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/objects.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/ScanFloat.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include <stdio.h>
#include <string.h>
#ifndef __MWERKS__
#include "x87.h"
#endif

// clang-format off
TypeIntegral stbool = {TYPEINT, 1, IT_BOOL};
TypeIntegral stchar = {TYPEINT, 1, IT_CHAR};
TypeIntegral stsignedchar = {TYPEINT, 1, IT_SCHAR};
TypeIntegral stunsignedchar = {TYPEINT, 1, IT_UCHAR};
TypeIntegral stwchar = {TYPEINT, 2, IT_WCHAR_T};
TypeIntegral stsignedshort = {TYPEINT, 2, IT_SHORT};
TypeIntegral stunsignedshort = {TYPEINT, 2, IT_USHORT};
TypeIntegral stsignedint = {TYPEINT, 4, IT_INT};
TypeIntegral stunsignedint = {TYPEINT, 4, IT_UINT};
TypeIntegral stsignedlong = {TYPEINT, 4, IT_LONG};
TypeIntegral stunsignedlong = {TYPEINT, 4, IT_ULONG};
TypeIntegral stsignedlonglong = {TYPEINT, 8, IT_LONGLONG};
TypeIntegral stunsignedlonglong = {TYPEINT, 8, IT_ULONGLONG};
TypeIntegral stfloat = {TYPEFLOAT, 4, IT_FLOAT};
TypeIntegral stshortdouble = {TYPEFLOAT, 8, IT_SHORTDOUBLE};
TypeIntegral stdouble = {TYPEFLOAT, 8, IT_DOUBLE};
TypeIntegral stlongdouble = {TYPEFLOAT, 8, IT_LONGDOUBLE};
TypeIntegral stcustomtype113 = {TYPEINT, 8, IT_17};
TypeIntegral stcustomtype114 = {TYPEINT, 8, IT_18};
TypeIntegral stvec2x32float = {TYPEFLOAT, 8, IT_VEC2X32FLOAT};

static StructMember stVUC_unsignedchar15 = {NULL, (Type *) &stunsignedchar, NULL, 15, 0};
static StructMember stVUC_unsignedchar14 = {&stVUC_unsignedchar15, (Type *) &stunsignedchar, NULL, 14, 0};
static StructMember stVUC_unsignedchar13 = {&stVUC_unsignedchar14, (Type *) &stunsignedchar, NULL, 13, 0};
static StructMember stVUC_unsignedchar12 = {&stVUC_unsignedchar13, (Type *) &stunsignedchar, NULL, 12, 0};
static StructMember stVUC_unsignedchar11 = {&stVUC_unsignedchar12, (Type *) &stunsignedchar, NULL, 11, 0};
static StructMember stVUC_unsignedchar10 = {&stVUC_unsignedchar11, (Type *) &stunsignedchar, NULL, 10, 0};
static StructMember stVUC_unsignedchar9 = {&stVUC_unsignedchar10, (Type *) &stunsignedchar, NULL, 9, 0};
static StructMember stVUC_unsignedchar8 = {&stVUC_unsignedchar9, (Type *) &stunsignedchar, NULL, 8, 0};
static StructMember stVUC_unsignedchar7 = {&stVUC_unsignedchar8, (Type *) &stunsignedchar, NULL, 7, 0};
static StructMember stVUC_unsignedchar6 = {&stVUC_unsignedchar7, (Type *) &stunsignedchar, NULL, 6, 0};
static StructMember stVUC_unsignedchar5 = {&stVUC_unsignedchar6, (Type *) &stunsignedchar, NULL, 5, 0};
static StructMember stVUC_unsignedchar4 = {&stVUC_unsignedchar5, (Type *) &stunsignedchar, NULL, 4, 0};
static StructMember stVUC_unsignedchar3 = {&stVUC_unsignedchar4, (Type *) &stunsignedchar, NULL, 3, 0};
static StructMember stVUC_unsignedchar2 = {&stVUC_unsignedchar3, (Type *) &stunsignedchar, NULL, 2, 0};
static StructMember stVUC_unsignedchar1 = {&stVUC_unsignedchar2, (Type *) &stunsignedchar, NULL, 1, 0};
static StructMember stVUC_unsignedchar0 = {&stVUC_unsignedchar1, (Type *) &stunsignedchar, NULL, 0, 0};

static StructMember stVSC_signedchar15 = {NULL, (Type *) &stsignedchar, NULL, 15, 0};
static StructMember stVSC_signedchar14 = {&stVSC_signedchar15, (Type *) &stsignedchar, NULL, 14, 0};
static StructMember stVSC_signedchar13 = {&stVSC_signedchar14, (Type *) &stsignedchar, NULL, 13, 0};
static StructMember stVSC_signedchar12 = {&stVSC_signedchar13, (Type *) &stsignedchar, NULL, 12, 0};
static StructMember stVSC_signedchar11 = {&stVSC_signedchar12, (Type *) &stsignedchar, NULL, 11, 0};
static StructMember stVSC_signedchar10 = {&stVSC_signedchar11, (Type *) &stsignedchar, NULL, 10, 0};
static StructMember stVSC_signedchar9 = {&stVSC_signedchar10, (Type *) &stsignedchar, NULL, 9, 0};
static StructMember stVSC_signedchar8 = {&stVSC_signedchar9, (Type *) &stsignedchar, NULL, 8, 0};
static StructMember stVSC_signedchar7 = {&stVSC_signedchar8, (Type *) &stsignedchar, NULL, 7, 0};
static StructMember stVSC_signedchar6 = {&stVSC_signedchar7, (Type *) &stsignedchar, NULL, 6, 0};
static StructMember stVSC_signedchar5 = {&stVSC_signedchar6, (Type *) &stsignedchar, NULL, 5, 0};
static StructMember stVSC_signedchar4 = {&stVSC_signedchar5, (Type *) &stsignedchar, NULL, 4, 0};
static StructMember stVSC_signedchar3 = {&stVSC_signedchar4, (Type *) &stsignedchar, NULL, 3, 0};
static StructMember stVSC_signedchar2 = {&stVSC_signedchar3, (Type *) &stsignedchar, NULL, 2, 0};
static StructMember stVSC_signedchar1 = {&stVSC_signedchar2, (Type *) &stsignedchar, NULL, 1, 0};
static StructMember stVSC_signedchar0 = {&stVSC_signedchar1, (Type *) &stsignedchar, NULL, 0, 0};

static StructMember stVUS_unsignedshort7 = {NULL, (Type *) &stunsignedshort, NULL, 14, 0};
static StructMember stVUS_unsignedshort6 = {&stVUS_unsignedshort7, (Type *) &stunsignedshort, NULL, 12, 0};
static StructMember stVUS_unsignedshort5 = {&stVUS_unsignedshort6, (Type *) &stunsignedshort, NULL, 10, 0};
static StructMember stVUS_unsignedshort4 = {&stVUS_unsignedshort5, (Type *) &stunsignedshort, NULL, 8, 0};
static StructMember stVUS_unsignedshort3 = {&stVUS_unsignedshort4, (Type *) &stunsignedshort, NULL, 6, 0};
static StructMember stVUS_unsignedshort2 = {&stVUS_unsignedshort3, (Type *) &stunsignedshort, NULL, 4, 0};
static StructMember stVUS_unsignedshort1 = {&stVUS_unsignedshort2, (Type *) &stunsignedshort, NULL, 2, 0};
static StructMember stVUS_unsignedshort0 = {&stVUS_unsignedshort1, (Type *) &stunsignedshort, NULL, 0, 0};

static StructMember stVSS_signedshort7 = {NULL, (Type *) &stsignedshort, NULL, 14, 0};
static StructMember stVSS_signedshort6 = {&stVSS_signedshort7, (Type *) &stsignedshort, NULL, 12, 0};
static StructMember stVSS_signedshort5 = {&stVSS_signedshort6, (Type *) &stsignedshort, NULL, 10, 0};
static StructMember stVSS_signedshort4 = {&stVSS_signedshort5, (Type *) &stsignedshort, NULL, 8, 0};
static StructMember stVSS_signedshort3 = {&stVSS_signedshort4, (Type *) &stsignedshort, NULL, 6, 0};
static StructMember stVSS_signedshort2 = {&stVSS_signedshort3, (Type *) &stsignedshort, NULL, 4, 0};
static StructMember stVSS_signedshort1 = {&stVSS_signedshort2, (Type *) &stsignedshort, NULL, 2, 0};
static StructMember stVSS_signedshort0 = {&stVSS_signedshort1, (Type *) &stsignedshort, NULL, 0, 0};

static StructMember stVUL_unsignedlong3 = {NULL, (Type *) &stunsignedlong, NULL, 12, 0};
static StructMember stVUL_unsignedlong2 = {&stVUL_unsignedlong3, (Type *) &stunsignedlong, NULL, 8, 0};
static StructMember stVUL_unsignedlong1 = {&stVUL_unsignedlong2, (Type *) &stunsignedlong, NULL, 4, 0};
static StructMember stVUL_unsignedlong0 = {&stVUL_unsignedlong1, (Type *) &stunsignedlong, NULL, 0, 0};

static StructMember stVSL_signedlong3 = {NULL, (Type *) &stsignedlong, NULL, 12, 0};
static StructMember stVSL_signedlong2 = {&stVSL_signedlong3, (Type *) &stsignedlong, NULL, 8, 0};
static StructMember stVSL_signedlong1 = {&stVSL_signedlong2, (Type *) &stsignedlong, NULL, 4, 0};
static StructMember stVSL_signedlong0 = {&stVSL_signedlong1, (Type *) &stsignedlong, NULL, 0, 0};

static StructMember stVF_float3 = {NULL, (Type *) &stfloat, NULL, 12, 0};
static StructMember stVF_float2 = {&stVF_float3, (Type *) &stfloat, NULL, 8, 0};
static StructMember stVF_float1 = {&stVF_float2, (Type *) &stfloat, NULL, 4, 0};
static StructMember stVF_float0 = {&stVF_float1, (Type *) &stfloat, NULL, 0, 0};

TypeStruct stvectorunsignedchar = {TYPESTRUCT, 16, NULL, &stVUC_unsignedchar0, STRUCT_VECTOR_UCHAR, 16};
TypeStruct stvectorsignedchar = {TYPESTRUCT, 16, NULL, &stVSC_signedchar0, STRUCT_VECTOR_SCHAR, 16};
TypeStruct stvectorboolchar = {TYPESTRUCT, 16, NULL, &stVSC_signedchar0, STRUCT_VECTOR_BCHAR, 16};

TypeStruct stvectorunsignedshort = {TYPESTRUCT, 16, NULL, &stVUS_unsignedshort0, STRUCT_VECTOR_USHORT, 16};
TypeStruct stvectorsignedshort = {TYPESTRUCT, 16, NULL, &stVSS_signedshort0, STRUCT_VECTOR_SSHORT, 16};
TypeStruct stvectorboolshort = {TYPESTRUCT, 16, NULL, &stVSS_signedshort0, STRUCT_VECTOR_BSHORT, 16};

TypeStruct stvectorunsignedlong = {TYPESTRUCT, 16, NULL, &stVUL_unsignedlong0, STRUCT_VECTOR_UINT, 16};
TypeStruct stvectorsignedlong = {TYPESTRUCT, 16, NULL, &stVSL_signedlong0, STRUCT_VECTOR_SINT, 16};
TypeStruct stvectorboollong = {TYPESTRUCT, 16, NULL, &stVSL_signedlong0, STRUCT_VECTOR_BINT, 16};

TypeStruct stvectorfloat = {TYPESTRUCT, 16, NULL, &stVF_float0, STRUCT_VECTOR_FLOAT, 16};

TypeStruct stvectorpixel = {TYPESTRUCT, 16, NULL, &stVUS_unsignedshort0, STRUCT_VECTOR_PIXEL, 16};

TypeStruct stvector = {TYPESTRUCT, 16, NULL, NULL, STRUCT_VECTOR_UINT, 16};
// clang-format on

static SInt32 cmach_structoffset;
static UInt8 cmach_structalign;
static short cmach_curbfsize;
static short cmach_curbfbasesize;
static int cmach_curbfoffset;
#if VERSION == VERSION_GC_2_5
static int cmach_curbfintegral;
#endif

static short cmach_packsize[] = {1, 2, 4, 8, 16};

static SInt16 CMach_GetQualifiedTypeAlign(Type* type, Boolean qualified);
static SInt16 CMach_GetMemberAlignment(Type* type, SInt32 min_align, Boolean qualified);

void CMach_Configure(void) {}

int CMach_GetQUALalign(Type* type, UInt32 qualifiers) {
    int alignment = 0;

    if ((qualifiers = CParser_GetTypeQualifiers(type, qualifiers) & Q_ALIGNED_MASK) != 0) {
        if (qualifiers == Q_ALIGNED_1) {
            alignment = 1;
        } else if (qualifiers == Q_ALIGNED_2) {
            alignment = 2;
        } else if (qualifiers == Q_ALIGNED_4) {
            alignment = 4;
        } else if (qualifiers == Q_ALIGNED_8) {
            alignment = 8;
        } else if (qualifiers == Q_ALIGNED_16) {
            alignment = 16;
        } else if (qualifiers == Q_ALIGNED_32) {
            alignment = 32;
        } else if (qualifiers == Q_ALIGNED_64) {
            alignment = 64;
        } else if (qualifiers == Q_ALIGNED_128) {
            alignment = 128;
        } else if (qualifiers == Q_ALIGNED_256) {
            alignment = 256;
        } else if (qualifiers == Q_ALIGNED_512) {
            alignment = 512;
        } else if (qualifiers == Q_ALIGNED_1024) {
            alignment = 1024;
        } else if (qualifiers == Q_ALIGNED_2048) {
            alignment = 2048;
        } else if (qualifiers == Q_ALIGNED_4096) {
            alignment = 4096;
        } else if (qualifiers == Q_ALIGNED_8192) {
            alignment = 8192;
        } else {
            CError_FATAL(230);
        }
    }
    return alignment;
}

SInt32 CMach_ArgumentAlignment(Type* type) {
    char save_align_mode;
    UInt8 save_oldalignment;
    SInt32 align;

    save_align_mode = copts.structalignment;
    save_oldalignment = copts.oldalignment;
    copts.structalignment = AlignMode2_PPC;
    copts.oldalignment = FALSE;
    align = CMach_GetQualifiedTypeAlign(type, FALSE);
    copts.structalignment = save_align_mode;
    copts.oldalignment = save_oldalignment;

    if (type->type == TYPESTRUCT && !TYPE_STRUCT(type)->members) {
        return (TYPE_STRUCT(type)->align > align) ? TYPE_STRUCT(type)->align : align;
    }
    return align;
}

SInt32 CMach_AllocationAlignment(Type* type, UInt32 qual) {
    SInt32 align;
    SInt32 tmpalign;
    int argalign;

    tmpalign = CMach_GetQUALalign(type, qual);
    align = CMach_GetQualifiedTypeAlign(type, TRUE);
    if (tmpalign > align) {
        align = tmpalign;
    }
    if ((argalign = CMach_ArgumentAlignment(type)) > align) {
        align = argalign;
    }

    switch (type->type) {
        case TYPEVOID:
        case TYPEMEMBERPOINTER:
        case TYPEPOINTER:
            tmpalign = 4;
            break;
        default:
            tmpalign = 1;
    }
    if (tmpalign > align) {
        align = tmpalign;
    }

    if (copts.optimizationlevel > 0) {
        if (type->type == TYPEARRAY ||
            (type->type == TYPESTRUCT &&
             (TYPE_STRUCT(type)->stype < STRUCT_VECTOR_UCHAR ||
              TYPE_STRUCT(type)->stype > STRUCT_VECTOR_PIXEL)) ||
            type->type == TYPECLASS ||
            (type->type == TYPEMEMBERPOINTER && (UInt32)type->size == 12)) {
            return (copts.min_struct_alignment > align) ? copts.min_struct_alignment : align;
        }
    }

    return align;
}

CInt64 CMach_CalcIntDiadic(Type* type, CInt64 left, short op, CInt64 right) {
    if (is_unsigned(type)) {
        switch (type->size) {
            case 1:
                CInt64_ConvertUInt8(&left);
                CInt64_ConvertUInt8(&right);
                break;
            case 2:
                CInt64_ConvertUInt16(&left);
                CInt64_ConvertUInt16(&right);
                break;
            case 4:
                CInt64_ConvertUInt32(&left);
                CInt64_ConvertUInt32(&right);
                break;
            case 8:
                break;
            default:
                CError_FATAL(331);
        }

        switch (op) {
            case '*':
                left = CInt64_MulU(left, right);
                break;
            case '/':
                if (CInt64_IsZero(&right)) {
                    CError_Warning(ERR_DIVISION_BY_0);
                } else {
                    left = CInt64_DivU(left, right);
                }
                break;
            case '%':
                if (CInt64_IsZero(&right)) {
                    CError_Warning(ERR_DIVISION_BY_0);
                } else {
                    left = CInt64_ModU(left, right);
                }
                break;
            case '+':
                left = CInt64_Add(left, right);
                break;
            case '-':
                left = CInt64_Sub(left, right);
                break;
            case TK_SHL:
                left = CInt64_Shl(left, right);
                break;
            case TK_SHR:
                left = CInt64_ShrU(left, right);
                break;
            case '<':
                CInt64_SetLong(&left, CInt64_LessU(left, right));
                break;
            case '>':
                CInt64_SetLong(&left, CInt64_GreaterU(left, right));
                break;
            case TK_LESS_EQUAL:
                CInt64_SetLong(&left, CInt64_LessEqualU(left, right));
                break;
            case TK_GREATER_EQUAL:
                CInt64_SetLong(&left, CInt64_GreaterEqualU(left, right));
                break;
            case TK_LOGICAL_EQ:
                CInt64_SetLong(&left, CInt64_Equal(left, right));
                break;
            case TK_LOGICAL_NE:
                CInt64_SetLong(&left, CInt64_NotEqual(left, right));
                break;
            case '&':
                left = CInt64_And(left, right);
                break;
            case '^':
                left = CInt64_Xor(left, right);
                break;
            case '|':
                left = CInt64_Or(left, right);
                break;
            case TK_LOGICAL_AND:
                CInt64_SetLong(&left, (!CInt64_IsZero(&left) && !CInt64_IsZero(&right)));
                break;
            case TK_LOGICAL_OR:
                CInt64_SetLong(&left, (!CInt64_IsZero(&left) || !CInt64_IsZero(&right)));
                break;
            default:
                CError_Error(ERR_UNEXPECTED_TOKEN);
        }

        switch (type->size) {
            case 1:
                CInt64_ConvertUInt8(&left);
                break;
            case 2:
                CInt64_ConvertUInt16(&left);
                break;
            case 4:
                CInt64_ConvertUInt32(&left);
                break;
            case 8:
                break;
        }
    } else {
        switch (type->size) {
            case 1:
                CInt64_ConvertInt8(&left);
                CInt64_ConvertInt8(&right);
                break;
            case 2:
                CInt64_ConvertInt16(&left);
                CInt64_ConvertInt16(&right);
                break;
            case 4:
                CInt64_ConvertInt32(&left);
                CInt64_ConvertInt32(&right);
                break;
            case 8:
                break;
            default:
                CError_FATAL(393);
        }

        switch (op) {
            case '*':
                left = CInt64_Mul(left, right);
                break;
            case '/':
                if (CInt64_IsZero(&right)) {
                    CError_Warning(ERR_DIVISION_BY_0);
                } else {
                    left = CInt64_Div(left, right);
                }
                break;
            case '%':
                if (CInt64_IsZero(&right)) {
                    CError_Warning(ERR_DIVISION_BY_0);
                } else {
                    left = CInt64_Mod(left, right);
                }
                break;
            case '+':
                left = CInt64_Add(left, right);
                break;
            case '-':
                left = CInt64_Sub(left, right);
                break;
            case TK_SHL:
                left = CInt64_Shl(left, right);
                break;
            case TK_SHR:
                left = CInt64_Shr(left, right);
                break;
            case '<':
                CInt64_SetLong(&left, CInt64_Less(left, right));
                break;
            case '>':
                CInt64_SetLong(&left, CInt64_Greater(left, right));
                break;
            case TK_LESS_EQUAL:
                CInt64_SetLong(&left, CInt64_LessEqual(left, right));
                break;
            case TK_GREATER_EQUAL:
                CInt64_SetLong(&left, CInt64_GreaterEqual(left, right));
                break;
            case TK_LOGICAL_EQ:
                CInt64_SetLong(&left, CInt64_Equal(left, right));
                break;
            case TK_LOGICAL_NE:
                CInt64_SetLong(&left, CInt64_NotEqual(left, right));
                break;
            case '&':
                left = CInt64_And(left, right);
                break;
            case '^':
                left = CInt64_Xor(left, right);
                break;
            case '|':
                left = CInt64_Or(left, right);
                break;
            case TK_LOGICAL_AND:
                CInt64_SetLong(&left, (!CInt64_IsZero(&left) && !CInt64_IsZero(&right)));
                break;
            case TK_LOGICAL_OR:
                CInt64_SetLong(&left, (!CInt64_IsZero(&left) || !CInt64_IsZero(&right)));
                break;
            default:
                CError_Error(ERR_UNEXPECTED_TOKEN);
        }

        switch (type->size) {
            case 1:
                CInt64_ConvertInt8(&left);
                break;
            case 2:
                CInt64_ConvertInt16(&left);
                break;
            case 4:
                CInt64_ConvertInt32(&left);
                break;
            case 8:
                break;
        }
    }

    return left;
}

CInt64 CMach_CalcIntMonadic(Type* type, short op, CInt64 val) {
    if (is_unsigned(type)) {
        switch (type->size) {
            case 1:
                CInt64_ConvertUInt8(&val);
                break;
            case 2:
                CInt64_ConvertUInt16(&val);
                break;
            case 4:
                CInt64_ConvertUInt32(&val);
                break;
            case 8:
                break;
            default:
                CError_FATAL(452);
        }

        switch (op) {
            case '-':
                val = CInt64_Neg(val);
                break;
            case '~':
                val = CInt64_Inv(val);
                break;
            case '!':
                val = CInt64_Not(val);
                break;
            default:
                CError_Error(ERR_UNEXPECTED_TOKEN);
        }

        switch (type->size) {
            case 1:
                CInt64_ConvertUInt8(&val);
                break;
            case 2:
                CInt64_ConvertUInt16(&val);
                break;
            case 4:
                CInt64_ConvertUInt32(&val);
                break;
            case 8:
                break;
        }
    } else {
        switch (type->size) {
            case 1:
                CInt64_ConvertInt8(&val);
                break;
            case 2:
                CInt64_ConvertInt16(&val);
                break;
            case 4:
                CInt64_ConvertInt32(&val);
                break;
            case 8:
                break;
            default:
                CError_FATAL(482);
        }

        switch (op) {
            case '-':
                val = CInt64_Neg(val);
                break;
            case '~':
                val = CInt64_Inv(val);
                break;
            case '!':
                val = CInt64_Not(val);
                break;
            default:
                CError_Error(ERR_UNEXPECTED_TOKEN);
        }

        switch (type->size) {
            case 1:
                CInt64_ConvertInt8(&val);
                break;
            case 2:
                CInt64_ConvertInt16(&val);
                break;
            case 4:
                CInt64_ConvertInt32(&val);
                break;
            case 8:
                break;
        }
    }

    return val;
}

CInt64 CMach_CalcIntConvertFromFloat(Type* type, Float fval) {
    CInt64 result;

    if ((type->type == TYPEINT || type->type == TYPEENUM) && (type->size == 8)) {
        if (is_unsigned(type)) {
            CInt64_ConvertUFromLongDouble(&result, fval.data.value);
        } else {
            CInt64_ConvertFromLongDouble(&result, fval.data.value);
        }
    } else {
#ifdef __MWERKS__
        if (is_unsigned(type)) {
            CInt64_SetULong(&result, fval.data.value);
        } else {
            CInt64_SetLong(&result, fval.data.value);
        }
#else
        if (is_unsigned(type)) {
            CInt64_SetULong(&result, (UInt32)X87_TruncateToInt64(fval.data.value));
        } else {
            CInt64_SetLong(&result, X87_TruncateToInt32(fval.data.value));
        }
#endif
    }

    return result;
}

void CMach_InitIntMem(Type* type, CInt64 val, void* mem) {
    UInt8 ch;
    UInt16 sh;
    UInt32 lg;

    switch (type->type) {
        case TYPEINT:
            switch (type->size) {
                case 1:
                    ch = (UInt8)val.lo;
                    memcpy(mem, &ch, 1);
                    break;
                case 2:
                    sh = val.lo;
                    sh = CTool_EndianConvertWord16(sh);
                    memcpy(mem, &sh, 2);
                    break;
                case 4:
                    lg = CTool_EndianConvertWord32(val.lo);
                    memcpy(mem, &lg, 4);
                    break;
                case 8:
                    CTool_EndianConvertWord64(val, mem);
                    break;
                default:
                    CError_FATAL(570);
            }
            break;
        default:
            CError_FATAL(574);
    }
}

void CMach_InitVectorMem(Type* type, MWVector128 val, void* mem, Boolean endian_convert) {
    unsigned char uc[16];
    unsigned short us[8];
    unsigned int ul[4];
    float f[4];
    int i;

    switch (type->type) {
        case TYPESTRUCT:
            switch (TYPE_STRUCT(type)->stype) {
                case STRUCT_VECTOR_UCHAR:
                case STRUCT_VECTOR_SCHAR:
                case STRUCT_VECTOR_BCHAR:
                    for (i = 0; i < 16; i++) {
                        if (!copts.littleendian) {
                            uc[i] = val.uc[i];
                        } else {
                            uc[i] = val.uc[15 - i];
                        }
                    }
                    memcpy(mem, uc, 16);
                    break;
                case STRUCT_VECTOR_USHORT:
                case STRUCT_VECTOR_SSHORT:
                case STRUCT_VECTOR_BSHORT:
                case STRUCT_VECTOR_PIXEL:
                    for (i = 0; i < 8; i++) {
                        if (!copts.littleendian) {
                            us[i] = val.us[i];
                        } else {
                            us[i] = val.us[7 - i];
                        }
                        if (endian_convert) {
                            us[i] = CTool_EndianConvertWord16(us[i]);
                        }
                    }
                    memcpy(mem, us, 16);
                    break;
                case STRUCT_VECTOR_UINT:
                case STRUCT_VECTOR_SINT:
                case STRUCT_VECTOR_BINT:
                    for (i = 0; i < 4; i++) {
                        if (!copts.littleendian) {
                            ul[i] = val.ul[i];
                        } else {
                            ul[i] = val.ul[3 - i];
                        }
                        if (endian_convert) {
                            ul[i] = CTool_EndianConvertWord32(ul[i]);
                        }
                    }
                    memcpy(mem, ul, 16);
                    break;
                case STRUCT_VECTOR_FLOAT:
                    for (i = 0; i < 4; i++) {
                        if (!copts.littleendian) {
                            f[i] = val.f[i];
                        } else {
                            f[i] = val.f[3 - i];
                        }
                    }
                    memcpy(mem, f, 16);
                    break;
                default:
                    CError_FATAL(659);
            }
            break;
        default:
            CError_FATAL(663);
    }
}

Float CMach_CalcFloatDiadic(Type* type, Float left, short op, Float right) {
    switch (op) {
        case '+':
#ifdef __MWERKS__
            left.data.value += right.data.value;
#else
            left.data.value = X87_Arithmetic(left.data.value, X87_ADD, right.data.value);
#endif
            break;
        case '-':
#ifdef __MWERKS__
            left.data.value -= right.data.value;
#else
            left.data.value = X87_Arithmetic(left.data.value, X87_SUB, right.data.value);
#endif
            break;
        case '*':
#ifdef __MWERKS__
            left.data.value *= right.data.value;
#else
            left.data.value = X87_Arithmetic(left.data.value, X87_MUL, right.data.value);
#endif
            break;
        case '/':
#ifdef __MWERKS__
            left.data.value /= right.data.value;
#else
            left.data.value = X87_Arithmetic(left.data.value, X87_DIV, right.data.value);
#endif
            break;
        default:
            CError_FATAL(683);
    }

    return CMach_CalcFloatConvert(type, left);
}

Float CMach_CalcFloatMonadic(Type* type, short op, Float fval) {
    if (op != '-') {
        CError_FATAL(696);
    }

    fval.data.value = -fval.data.value;
    return CMach_CalcFloatConvert(type, fval);
}

Boolean CMach_CalcFloatDiadicBool(Type* type, Float left, short op, Float right) {
    switch (op) {
        case TK_LOGICAL_EQ:
            return left.data.value == right.data.value;
        case TK_LOGICAL_NE:
            return left.data.value != right.data.value;
        case TK_LESS_EQUAL:
            return left.data.value <= right.data.value;
        case TK_GREATER_EQUAL:
            return left.data.value >= right.data.value;
        case '>':
            return left.data.value > right.data.value;
        case '<':
            return left.data.value < right.data.value;
        default:
            CError_FATAL(718);
            return FALSE;
    }
}

char* CMach_FloatScan(char* input, Float* result, Boolean* fail) {
    double resultval;
    char* outpos;

    if (!(outpos = ScanFloat(input, &resultval, fail))) {
        CError_ErrorTerm(ERR_NUMBER_OUT_RANGE);
    }

    if (*fail) {
        result->data.value = 0.0;
    } else {
        result->data.value = resultval;
    }

    return outpos;
}

Float CMach_CalcFloatConvertFromInt(Type* type, CInt64 val) {
    Float result;

    if ((type->type == TYPEINT || type->type == TYPEENUM) && (type->size == 8)) {
        if (is_unsigned(type)) {
            result.data.value = CInt64_ConvertUToLongDouble(&val);
        } else {
            result.data.value = CInt64_ConvertToLongDouble(&val);
        }
    } else {
        if (is_unsigned(type)) {
            result.data.value = val.lo;
        } else {
            result.data.value = (SInt32)val.lo;
        }
    }

    return result;
}

Float CMach_CalcFloatConvert(Type* type, Float fval) {
    switch (type->size) {
        case 4:
            fval.data.value = (float)fval.data.value;
            break;
        case 8:
            fval.data.value = (double)fval.data.value;
            break;
        case 10:
        case 12:
            break;
        default:
            CError_FATAL(805);
    }
    return fval;
}

Boolean CMach_FloatIsZero(Float fval) {
    return fval.data.value == 0.0;
}

Boolean CMach_FloatIsOne(Float fval) {
    return fval.data.value == 1.0;
}

Boolean CMach_FloatIsNegOne(Float fval) {
    return fval.data.value == -1.0;
}

void CMach_InitFloatMem(Type* type, Float val, void* mem) {
    float f;
    double d;

    if (type->type == TYPEFLOAT) {
        switch (type->size) {
            case 4:
                f = val.data.value;
                memcpy(mem, &f, 4);
                CTool_EndianConvertMem(mem, 4);
                return;
            case 8:
                d = val.data.value;
                memcpy(mem, &d, 8);
                CTool_EndianConvertMem(mem, 8);
                return;
        }
    }

    CError_FATAL(870);
}

void CMach_PrintFloat(char* buf, Float val) {
    sprintf(buf, "%g", val.data.value);
}

void CMach_PragmaParams(void) {
    if (copts.warn_illpragma) {
        CError_Warning(ERR_ILLEGAL_PRAGMA, 0);
    }

    while (notendofline()) {
        lex();
    }
}

void CMach_AdjustFuntionArgs(void) {}

#define CMACH_POWER_ALIGNMENT 0

static SInt32 CMach_GetPPCTypeAlign(Type* type, Boolean align_double, Boolean qualified) {
    ClassList* base;
    ObjMemberVar* ivar;
    StructMember* member;
    SInt32 best;
    SInt32 ivarAlign;
    SInt32 qualAlign;

    SInt32 align = CMach_GetQualifiedTypeAlign(type, qualified);
    if (CMACH_POWER_ALIGNMENT && align <= 8) {
        while (type->type == TYPEARRAY) {
            type = TYPE_POINTER(type)->target;
        }

        if (align_double) {
            if (type->type == TYPEFLOAT && type->size > 4 && align < 8) {
                return 8;
            }
        } else if (align == 8) {
            if (type->type == TYPECLASS) {
                best = 4;
                for (base = TYPE_CLASS(type)->bases; base; base = base->next) {
                    if (base->base->align > best) {
                        best = base->base->align;
                    }
                }
                for (ivar = TYPE_CLASS(type)->ivars; ivar; ivar = ivar->next) {
                    ivarAlign = CMach_GetPPCTypeAlign(ivar->type, FALSE, qualified);
                    if (ivarAlign > best) {
                        best = ivarAlign;
                    }
                    if (qualified) {
                        qualAlign = CMach_GetQUALalign(ivar->type, ivar->qual);
                        if (qualAlign > best) {
                            best = qualAlign;
                        }
                    }
                }
                return best;
            }
            if (type->type == TYPESTRUCT) {
                best = 4;
                for (member = TYPE_STRUCT(type)->members; member; member = member->next) {
                    ivarAlign = CMach_GetPPCTypeAlign(member->type, FALSE, qualified);
                    if (ivarAlign > best) {
                        best = ivarAlign;
                    }
                    if (qualified) {
                        qualAlign = CMach_GetQUALalign(member->type, member->qual);
                        if (qualAlign > best) {
                            best = qualAlign;
                        }
                    }
                }
                return best;
            }
        }
    }

    return align;
}

static SInt16 CMach_GetQualifiedStructAlign(const TypeStruct* tstruct, Boolean qualified) {
    StructMember* member;
    SInt32 best;
    SInt32 align;
    Boolean isFirst;

    if (tstruct->stype >= STRUCT_VECTOR_UCHAR && tstruct->stype <= STRUCT_VECTOR_PIXEL) {
        return 16;
    }

    switch (copts.structalignment) {
        case AlignMode3_1Byte:
        case AlignMode8_Packed:
            return 1;
        case AlignMode0_Mac68k:
            return 2;
        case AlignMode1_Mac68k4byte:
            return (tstruct->size <= 2) ? 2 : 4;
    }

    if (tstruct->size <= 1) {
        return 1;
    }

    best = 1;
    switch (copts.structalignment) {
        default:
            CError_FATAL(1032);
        case AlignMode4_2Byte:
        case AlignMode5_4Byte:
        case AlignMode6_8Byte:
        case AlignMode7_16Byte:
            for (member = tstruct->members; member; member = member->next) {
                align = CMach_GetQualifiedTypeAlign(member->type, qualified);
                if (align > best) {
                    best = align;
                }
                if (qualified) {
                    align = CMach_GetQUALalign(member->type, member->qual);
                    if (align > best) {
                        best = align;
                    }
                }
            }
            return best;
        case AlignMode2_PPC:
            if (copts.oldalignment) {
                for (member = tstruct->members; member; member = member->next) {
                    align = CMach_GetQualifiedTypeAlign(member->type, qualified);
                    if (align > best) {
                        best = align;
                    }
                    if (qualified) {
                        align = CMach_GetQUALalign(member->type, member->qual);
                        if (align > best) {
                            best = align;
                        }
                    }
                }
            } else if (TYPE_STRUCT(tstruct)->stype == STRUCT_TYPE_UNION) {
                for (member = tstruct->members; member; member = member->next) {
                    align = CMach_GetPPCTypeAlign(member->type, TRUE, qualified);
                    if (align > best) {
                        best = align;
                    }
                    if (qualified) {
                        align = CMach_GetQUALalign(member->type, member->qual);
                        if (align > best) {
                            best = align;
                        }
                    }
                }
            } else {
                isFirst = TRUE;
                for (member = tstruct->members; member; member = member->next) {
                    align = CMach_GetPPCTypeAlign(member->type, isFirst || (best >= 8), qualified);
                    if (align > best) {
                        best = align;
                    }
                    if (qualified) {
                        align = CMach_GetQUALalign(member->type, member->qual);
                        if (align > best) {
                            best = align;
                        }
                    }
                    isFirst = FALSE;
                }
            }
            return best;
    }
}

UInt16 CMach_GetStructAlign(TypeStruct* str) {
    return CMach_GetQualifiedStructAlign(str, TRUE);
}

static SInt16 CMach_GetQualifiedClassAlign(TypeClass* tclass, Boolean qualified) {
    ClassList* base;
    ObjMemberVar* ivar;
    SInt32 best;
    SInt32 align;
    Boolean isFirst;

    switch (copts.structalignment) {
        case AlignMode3_1Byte:
        case AlignMode8_Packed:
            return 1;
        case AlignMode0_Mac68k:
            return 2;
        case AlignMode1_Mac68k4byte:
            return (tclass->size <= 2) ? 2 : 4;
    }
    if (tclass->size <= 1) {
        return 1;
    }
    best = 1;
    switch (copts.structalignment) {
        default:
            CError_FATAL(1155);
        case AlignMode4_2Byte:
        case AlignMode5_4Byte:
        case AlignMode6_8Byte:
        case AlignMode7_16Byte:
            for (base = tclass->bases; base; base = base->next) {
                if ((align = base->base->align) > best) {
                    best = align;
                }
            }
            for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
                align = CMach_GetMemberAlignment(
                    ivar->type,
                    qualified ? CMach_GetQUALalign(ivar->type, ivar->qual) : 0,
                    qualified
                );
                if (align > best) {
                    best = align;
                }
            }
            return best;
        case AlignMode2_PPC:
            best = 1;
            if (copts.oldalignment) {
                for (base = tclass->bases; base; base = base->next) {
                    if ((align = base->base->align) > best) {
                        best = align;
                    }
                }
                for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
                    align = CMach_GetMemberAlignment(
                        ivar->type,
                        qualified ? CMach_GetQUALalign(ivar->type, ivar->qual) : 0,
                        qualified
                    );
                    if (align > best) {
                        best = align;
                    }
                }
            } else {
                isFirst = TRUE;
                for (base = tclass->bases; base; base = base->next) {
                    if ((align = base->base->align) > best) {
                        best = align;
                    }
                    isFirst = FALSE;
                }
                if (tclass->mode == CLASS_MODE_UNION) {
                    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
                        align = CMach_GetPPCTypeAlign(ivar->type, TRUE, qualified);
                        if (align > best) {
                            best = align;
                        }
                        if (qualified) {
                            align = CMach_GetQUALalign(ivar->type, ivar->qual);
                            if (align > best) {
                                best = align;
                            }
                        }
                    }
                } else {
                    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
                        if (ivar->offset && align != 8) {
                            isFirst = FALSE;
                        }
                        align =
                            CMach_GetPPCTypeAlign(ivar->type, isFirst || (best >= 8), qualified);
                        if (align > best) {
                            best = align;
                        }
                        if (qualified) {
                            align = CMach_GetQUALalign(ivar->type, ivar->qual);
                            if (align > best) {
                                best = align;
                            }
                        }
                    }
                }
            }
            return best;
    }
}

SInt16 CMach_GetClassAlign(TypeClass* tclass) {
    return CMach_GetQualifiedClassAlign(tclass, TRUE);
}

static SInt16 CMach_GetWinTypeAlign(Type* type) {
    int packoffs = copts.structalignment - 3;
    int align = cmach_packsize[packoffs];
    if (type->size < align) {
        align = type->size;
    }
    return align;
}

static SInt16 CMach_GetWinMinimizeAlign(SInt16 align) {
    int packoffs = copts.structalignment - 3;
    SInt32 minimum = cmach_packsize[packoffs];
    if (minimum < align) {
        align = minimum;
    }
    return align;
}

static SInt16 CMach_GetQualifiedTypeAlign(Type* type, Boolean qualified) {
    Boolean isWin;
    SInt16 align;

    if (type->type == TYPESTRUCT &&
        TYPE_STRUCT(type)->stype >= STRUCT_VECTOR_UCHAR &&
        TYPE_STRUCT(type)->stype <= STRUCT_VECTOR_PIXEL) {
        return 16;
    }

    switch (copts.structalignment) {
        case AlignMode3_1Byte:
        case AlignMode8_Packed:
            return 1;
        case AlignMode4_2Byte:
        case AlignMode5_4Byte:
        case AlignMode6_8Byte:
        case AlignMode7_16Byte:
            isWin = TRUE;
            break;
        default:
            isWin = FALSE;
            break;
    }

restart:
    switch (type->type) {
        case TYPEVOID:
            return 0;
        case TYPEFUNC:
            return 0;
        case TYPEENUM:
            type = TYPE_ENUM(type)->enumtype;
        case TYPEINT:
            if (isWin) {
                return CMach_GetWinTypeAlign(type);
            }
            if (type->size == 1) {
                return 1;
            }
            if (copts.structalignment != AlignMode0_Mac68k && type->size >= 8) {
                return 8;
            }
            if (copts.oldalignment && type->size == 8) {
                return 8;
            }
            if (copts.structalignment != AlignMode0_Mac68k && type->size >= 4) {
                return 4;
            }
            return 2;
        case TYPEFLOAT:
            if (isWin) {
                return CMach_GetWinTypeAlign(type);
            }
            switch (copts.structalignment) {
                case AlignMode0_Mac68k:
                    return 2;
                case AlignMode1_Mac68k4byte:
                    return 4;
                case AlignMode2_PPC:
                    if (type->size > 4) {
                        return 8;
                    }
                    return 4;
                default:
                    CError_FATAL(1352);
            }
        case TYPEMEMBERPOINTER:
        case TYPEPOINTER:
            if (isWin) {
                return CMach_GetWinTypeAlign(type);
            }
            if (copts.structalignment == AlignMode0_Mac68k) {
                return 2;
            } else {
                return 4;
            }
        case TYPEARRAY:
            if (copts.alignarraymembers) {
                if (isWin) {
                    return CMach_GetWinTypeAlign(type);
                }
                if (type->size == 1) {
                    return 1;
                }
                if (copts.structalignment == AlignMode0_Mac68k || type->size <= 2) {
                    return 2;
                }
                if (copts.structalignment == AlignMode1_Mac68k4byte || type->size < 8) {
                    return 4;
                }
                align = CMach_GetQualifiedTypeAlign(TYPE_POINTER(type)->target, qualified);
                if (align > 4) {
                    return align;
                } else {
                    return 4;
                }
            } else {
                type = TYPE_POINTER(type)->target;
                goto restart;
            }
        case TYPESTRUCT:
            if (qualified) {
                align = TYPE_STRUCT(type)->align;
            } else {
                align = CMach_GetQualifiedStructAlign(TYPE_STRUCT(type), qualified);
            }
            if (isWin) {
                return CMach_GetWinMinimizeAlign(align);
            } else {
                return align;
            }
        case TYPECLASS:
            if (qualified) {
                align = TYPE_CLASS(type)->align;
            } else {
                align = CMach_GetQualifiedClassAlign(TYPE_CLASS(type), qualified);
            }
            if (isWin) {
                return CMach_GetWinMinimizeAlign(align);
            } else {
                return align;
            }
        case TYPEBITFIELD:
            type = TYPE_BITFIELD(type)->bitfieldtype;
            goto restart;
        case TYPETEMPLATE:
            return 1;
        default:
            CError_FATAL(1398);
            return 0;
    }
}

SInt16 CMach_GetTypeAlign(Type* type) {
    return CMach_GetQualifiedTypeAlign(type, TRUE);
}

static SInt16 CMach_GetMemberAlignment(Type* type, SInt32 min_align, Boolean qualified) {
    SInt32 align;

    align = CMach_GetQualifiedTypeAlign(type, qualified);
    if (align < 1) {
        align = 1;
    }

    if (IS_TYPE_VECTOR(type) && align < 16) {
        align = 16;
    }

    switch (copts.structalignment) {
        case AlignMode8_Packed:
            align = 1;
            break;
        case AlignMode0_Mac68k:
            if (align > 2) {
                align = 2;
            }
            break;
        case AlignMode1_Mac68k4byte:
            if (align > 4) {
                align = 4;
            }
            break;
        case AlignMode2_PPC:
            if (!copts.oldalignment) {
                align = CMach_GetPPCTypeAlign(
                    type, cmach_structoffset == 0 || (cmach_structalign >= 8), qualified
                );
            }
            if (min_align > align) {
                align = min_align;
            }
            break;
    }

    if (align > cmach_structalign) {
        cmach_structalign = align;
    }
    return align;
}

SInt16 CMach_MemberAlignValue(Type* type, SInt32 offset) {
    SInt16 align = CMach_GetMemberAlignment(type, 0, TRUE);
    if ((SInt32)align <= 1) {
        return 0;
    }

    return (align - 1) & (align - (offset & (align - 1)));
}

static SInt16 MemberAlignValue(Type* type, SInt32 offset, SInt32 min_align) {
    SInt16 align = CMach_GetMemberAlignment(type, min_align, TRUE);
    return (align - 1) & (align - (offset & (align - 1)));
}

void CMach_StructLayoutInitOffset(SInt32 offset) {
    cmach_structoffset = offset;
    cmach_structalign = 4;
    cmach_curbfsize = 0;
#if VERSION >= VERSION_GC_2_5
    if (!copts.old_style_bitfields) {
        cmach_curbfbasesize = 0;
        cmach_curbfoffset = 0;
#if VERSION == VERSION_GC_2_5
        cmach_curbfintegral = 0;
#endif
    }
#else
    cmach_curbfbasesize = 0;
    cmach_curbfoffset = 0;
#endif
}

SInt32 CMach_StructLayoutGetCurSize(void) {
    int alignment = 0;
    int padding;

#if VERSION >= VERSION_GC_2_5
    if (!copts.old_style_bitfields && cmach_curbfsize != 0) {
#else
    if (cmach_curbfsize != 0) {
#endif
        switch (copts.structalignment) {
            case AlignMode3_1Byte:
            case AlignMode8_Packed:
                alignment = 8;
                break;
            case AlignMode0_Mac68k:
            case AlignMode4_2Byte:
                alignment = 16;
                break;
        }
        if (alignment != 0) {
            padding = cmach_curbfbasesize * 8 - cmach_curbfsize;
            if (padding > 0) {
                cmach_structoffset -= padding / alignment;
            }
        }
        cmach_curbfsize = 0;
    }
    return cmach_structoffset;
}

SInt32 CMach_StructLayoutGetOffset(Type* type, UInt32 qual) {
    SInt32 align;
    SInt32 offset;
    int unused;
    UInt32 qualifiers;

#if VERSION >= VERSION_GC_2_5
    if (!copts.old_style_bitfields && cmach_curbfsize) {
#else
    if (cmach_curbfsize) {
#endif
        unused = cmach_curbfbasesize * 8 - cmach_curbfsize;
        if (unused >= 0) {
            cmach_structoffset -= unused / 8;
        }
    }

    qualifiers = CParser_GetTypeQualifiers(type, qual);

    cmach_curbfsize = 0;
    align = MemberAlignValue(type, cmach_structoffset, CMach_GetQUALalign(type, qualifiers));
    offset = cmach_structoffset + align;
    cmach_structoffset = offset + type->size;
    return offset;
}

static SInt32 CMach_StructLayoutNewBitfieldUnit(TypeBitfield* tbitfield, SInt16 basesize) {
    if (!tbitfield->bitlength) {
        return cmach_structoffset;
    }
    cmach_curbfsize = tbitfield->bitlength;
    cmach_curbfbasesize = basesize;
    cmach_curbfoffset = cmach_structoffset;
    cmach_structoffset += basesize;
    tbitfield->offset = 0;
    return cmach_curbfoffset;
}

static SInt32 CMach_StructLayoutAppendBitfield(TypeBitfield* tbitfield, SInt16 basesize) {
    tbitfield->offset = cmach_curbfsize;
    cmach_curbfsize += tbitfield->bitlength;
    cmach_curbfbasesize = basesize;
    cmach_curbfoffset = cmach_structoffset;
    cmach_structoffset += basesize;
    return cmach_curbfoffset;
}

static int CMach_BitfieldPadding(int offset, int align) {
    if (align && (offset & (align - 1))) {
        return align - (offset & (align - 1));
    }
    return 0;
}

static SInt32 CMach_StructLayoutPackedBitfield(
    SInt16 basesize, TypeBitfield* tbitfield, int unit_align, int qual_align
) {
    int padding;
    int unitsize;
    int unitbits;
    Boolean unnamedzero;
    int offset;
    int bitpos;
    int unitpadding;
    int available;
    int u;

    padding = 0;
    unitsize = basesize;
    unitbits = unitsize * 8;
    unnamedzero = tbitfield->bitlength == 0 && tbitfield->suppressAlignment != 0;
    if (unitbits < tbitfield->bitlength) {
        CError_FATAL(1624);
    }
    if (unit_align && (cmach_structoffset & (unit_align - 1))) {
        padding = unit_align - (cmach_structoffset & (unit_align - 1));
    }
    if (!cmach_structoffset || unnamedzero || qual_align) {
        if (unnamedzero) {
            cmach_curbfsize = 0;
            if (unit_align < (u = unitsize) && basesize) {
                if ((unitsize = (unitsize - 1) & cmach_structoffset)) {
                    padding = u - unitsize;
                }
            }
        }
        cmach_structoffset += padding;
        return CMach_StructLayoutNewBitfieldUnit(tbitfield, basesize);
    }
    if (cmach_curbfsize) {
        if (cmach_curbfbasesize == basesize) {
            if (cmach_curbfsize + tbitfield->bitlength <= unitbits) {
                cmach_structoffset = cmach_curbfoffset;
                return CMach_StructLayoutAppendBitfield(tbitfield, basesize);
            }
            cmach_structoffset += padding;
            return CMach_StructLayoutNewBitfieldUnit(tbitfield, basesize);
        }
        offset = cmach_curbfsize / 8 + cmach_curbfoffset;
        bitpos = cmach_curbfsize % 8;
    } else {
        offset = cmach_structoffset;
        bitpos = 0;
    }
    if (basesize && (offset & (unitsize - 1))) {
        unitpadding = unitsize - (offset & (unitsize - 1));
    } else {
        unitpadding = 0;
    }
    if (!unitpadding) {
        unitpadding = unitsize;
    }
    available = unitpadding * 8 - bitpos;
    if (available - tbitfield->bitlength >= 0) {
        offset = offset + unitpadding - unitsize;
        if (offset < 0) {
            CError_FATAL(1669);
        }
        cmach_curbfsize = unitbits - available;
        cmach_structoffset = offset;
        return CMach_StructLayoutAppendBitfield(tbitfield, basesize);
    }
    cmach_structoffset = bitpos ? offset + 1 : offset;
    cmach_structoffset += CMach_BitfieldPadding(cmach_structoffset, unit_align);
    return CMach_StructLayoutNewBitfieldUnit(tbitfield, basesize);
}

#if VERSION == VERSION_GC_2_5
SInt32 CMach_StructLayoutBitfield(TypeBitfield* tbitfield, UInt32 qual) {
    SInt16 align;
    SInt16 padding_at_start;
    SInt16 basesize;
    SInt16 basesize_bits;
    SInt16 unit_align;
    Boolean old_style;
    Boolean inunit;
    Boolean overflow;
    Boolean newunit;
    Boolean fits;
    Boolean grow;
    SInt16 bits;
    int bitlength;
    int byte;
    int end;
    SInt16 required_alignment;
    int room;
    int partial;
    int extra;
    TypeIntegral* type;

    padding_at_start = 0;
    required_alignment = 0;
    tbitfield->declsize = tbitfield->bitfieldtype->size;
    tbitfield->x0e = -1;
    align = CMach_GetQUALalign(tbitfield->bitfieldtype, qual);
    if (align < tbitfield->bitfieldtype->size) {
        align = 0;
    }

    switch (tbitfield->bitfieldtype->size) {
        case 1:
            basesize = 1;
            basesize_bits = 8;
            required_alignment = 0;
            break;
        case 2:
            basesize = 2;
            basesize_bits = 16;
            required_alignment = 2;
            break;
        case 4:
            if (copts.structalignment != AlignMode0_Mac68k &&
                copts.structalignment != AlignMode4_2Byte) {
                required_alignment = 4;
            } else {
                required_alignment = 2;
            }
            basesize = 4;
            basesize_bits = 32;
            break;
        case 8:
            basesize = 8;
            basesize_bits = 64;
            required_alignment = 8;
            break;
        default:
            CError_FATAL(1745);
    }

    switch (copts.structalignment) {
        case AlignMode3_1Byte:
        case AlignMode8_Packed:
            required_alignment = 0;
            break;
    }

    unit_align = required_alignment;
    if (align > required_alignment) {
        unit_align = align;
    }

    if (unit_align && (cmach_structoffset & (unit_align - 1))) {
        padding_at_start = unit_align - (cmach_structoffset & (unit_align - 1));
    }

    old_style = copts.old_style_bitfields;
    if (!old_style && !cmach_curbfsize && cmach_structoffset && !align && padding_at_start) {
        cmach_curbfbasesize = basesize - padding_at_start;
        cmach_curbfoffset = cmach_structoffset - cmach_curbfbasesize;
        bits = cmach_curbfbasesize * 8;
        if ((SInt16)(basesize_bits - bits - tbitfield->bitlength) >= 0) {
            cmach_curbfsize = bits;
            inunit = TRUE;
        } else {
            inunit = FALSE;
        }
    } else {
        inunit = cmach_curbfsize != 0;
    }

    if (!inunit) {
        cmach_structoffset += padding_at_start;
        if (!tbitfield->bitlength) {
            cmach_curbfintegral = TYPE_INTEGRAL_UNCHECKED(tbitfield->bitfieldtype);
            return cmach_structoffset;
        }
        cmach_curbfsize = tbitfield->bitlength;
        cmach_curbfbasesize = basesize;
        cmach_curbfoffset = cmach_structoffset;
        cmach_structoffset += basesize;
        tbitfield->offset = 0;
        cmach_curbfintegral = TYPE_INTEGRAL_UNCHECKED(tbitfield->bitfieldtype);
        return cmach_curbfoffset;
    }

    overflow =
        tbitfield->bitlength == 0 || (cmach_curbfsize + tbitfield->bitlength) > basesize_bits;
    if (!old_style) {
        newunit = overflow || cmach_curbfbasesize > basesize;
    } else {
        newunit = overflow || cmach_curbfbasesize != basesize;
    }

    if (newunit) {
        if (!old_style &&
            cmach_curbfbasesize > basesize &&
            tbitfield->bitlength &&
            cmach_curbfbasesize * 8 - cmach_curbfsize >= tbitfield->bitlength) {
            byte = cmach_curbfsize / 8;
            room = basesize - (byte + cmach_curbfoffset) % basesize;
            end = cmach_curbfsize - byte * 8 + tbitfield->bitlength;
            if (end / 8 + ((end % 8) ? 1 : 0) > room) {
                cmach_structoffset =
                    cmach_curbfoffset + byte + ((cmach_curbfsize % 8) ? 1 : 0) - padding_at_start;
                fits = FALSE;
            } else {
                fits = TRUE;
            }
        } else {
            fits = FALSE;
        }
        if (fits) {
            tbitfield->size = cmach_curbfbasesize;
            type = galloc(sizeof(TypeIntegral));
            type->type = TYPEINT;
            type->size = cmach_curbfbasesize;
            type->integral = cmach_curbfintegral;
            tbitfield->bitfieldtype = TYPE(type);
            tbitfield->offset = cmach_curbfsize;
            cmach_curbfsize += tbitfield->bitlength;
            return cmach_curbfoffset;
        }
        cmach_structoffset += padding_at_start;
        cmach_curbfsize = 0;
        cmach_curbfbasesize = basesize;
        if (!tbitfield->bitlength) {
            cmach_curbfintegral = TYPE_INTEGRAL_UNCHECKED(tbitfield->bitfieldtype);
            return cmach_structoffset;
        }
        cmach_curbfoffset = cmach_structoffset;
        cmach_structoffset += basesize;
    } else {
        extra = 0;
        if (!old_style && cmach_curbfbasesize < basesize) {
            end = tbitfield->bitlength + cmach_curbfsize;
            partial = (end % 8) ? 1 : 0;
            room = cmach_curbfoffset % basesize;
            if (end / 8 + partial + room > basesize) {
                extra = room * 8 - cmach_curbfsize;
            }
            grow = TRUE;
        } else {
            grow = FALSE;
        }
        if (grow) {
            if (extra) {
                cmach_curbfsize += extra;
                cmach_structoffset = cmach_curbfsize / 8 + cmach_curbfoffset;
                cmach_curbfsize = 0;
            } else {
                cmach_structoffset -= cmach_curbfbasesize;
            }
            cmach_curbfbasesize = basesize;
            if (!tbitfield->bitlength) {
                cmach_curbfintegral = TYPE_INTEGRAL_UNCHECKED(tbitfield->bitfieldtype);
                return cmach_structoffset;
            }
            cmach_curbfoffset = cmach_structoffset;
            cmach_structoffset += basesize;
        }
    }
    tbitfield->offset = cmach_curbfsize;
    cmach_curbfsize += tbitfield->bitlength;
    cmach_curbfintegral = TYPE_INTEGRAL_UNCHECKED(tbitfield->bitfieldtype);
    return cmach_curbfoffset;
}
#else
#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT (-105 + CError_LINE_DELTA)
#endif
SInt32 CMach_StructLayoutBitfield(TypeBitfield* tbitfield, UInt32 qual) {
    SInt16 align;
    SInt16 padding_at_start;
    SInt16 basesize_bits;
    SInt16 required_alignment;
    SInt16 basesize;
    SInt16 unit_align;

    padding_at_start = 0;
    required_alignment = 0;
    align = CMach_GetQUALalign(tbitfield->bitfieldtype, qual);
#if VERSION >= VERSION_GC_2_5
    if (align < tbitfield->bitfieldtype->size) {
#else
    if (align <= tbitfield->bitfieldtype->size) {
#endif
        align = 0;
    }

    switch (tbitfield->bitfieldtype->size) {
        case 1:
            basesize = 1;
            basesize_bits = 8;
            required_alignment = 0;
            break;
        case 2:
            basesize = 2;
            basesize_bits = 16;
            required_alignment = 2;
            break;
        case 4:
            if (copts.structalignment != AlignMode0_Mac68k &&
                copts.structalignment != AlignMode4_2Byte) {
                required_alignment = 4;
            } else {
                required_alignment = 2;
            }
            basesize = 4;
            basesize_bits = 32;
            break;
        case 8:
            basesize = 8;
            basesize_bits = 64;
            required_alignment = 8;
            break;
        default:
            CError_FATAL(1731);
    }

    switch (copts.structalignment) {
        case AlignMode3_1Byte:
        case AlignMode8_Packed:
            required_alignment = 0;
            break;
    }

    unit_align = required_alignment;
    if (align > required_alignment) {
        unit_align = align;
    }

#if VERSION >= VERSION_GC_2_6
    if (!copts.old_style_bitfields) {
        return CMach_StructLayoutPackedBitfield(basesize, tbitfield, unit_align, align);
    }
#endif

    if (unit_align && (cmach_structoffset & (unit_align - 1))) {
        padding_at_start = unit_align - (cmach_structoffset & (unit_align - 1));
    }

#if VERSION < VERSION_GC_2_5
    if (!cmach_curbfsize && cmach_structoffset && !align && padding_at_start) {
        cmach_curbfbasesize = basesize - padding_at_start;
        if ((SInt16)(basesize_bits - cmach_curbfbasesize * 8 - tbitfield->bitlength) >= 0) {
            cmach_curbfsize = cmach_curbfbasesize * 8;
        }
    }
#endif

    if (!cmach_curbfsize) {
        cmach_structoffset += padding_at_start;
        if (!tbitfield->bitlength) {
            return cmach_structoffset;
        }
        cmach_curbfsize = tbitfield->bitlength;
        cmach_curbfbasesize = basesize;
        cmach_curbfoffset = cmach_structoffset;
        cmach_structoffset += basesize;
        tbitfield->offset = 0;
        return cmach_curbfoffset;
    } else {
#if VERSION >= VERSION_GC_2_5
        if (!tbitfield->bitlength ||
            (cmach_curbfsize + tbitfield->bitlength) > basesize_bits ||
            cmach_curbfbasesize != basesize) {
#else
        if (!tbitfield->bitlength ||
            (cmach_curbfsize + tbitfield->bitlength) > basesize_bits ||
            cmach_curbfbasesize > basesize) {
#endif
            cmach_structoffset += padding_at_start;
            cmach_curbfsize = 0;
            cmach_curbfbasesize = basesize;
            if (!tbitfield->bitlength) {
                return cmach_structoffset;
            }
            cmach_curbfoffset = cmach_structoffset;
            cmach_structoffset += basesize;
        }
#if VERSION < VERSION_GC_2_5
        else if (cmach_curbfbasesize < basesize) {
            cmach_structoffset -= cmach_curbfbasesize;
            cmach_curbfbasesize = basesize;
            if (!tbitfield->bitlength) {
                return cmach_structoffset;
            }
            cmach_curbfoffset = cmach_structoffset;
            cmach_structoffset += basesize;
        }
#endif
        tbitfield->offset = cmach_curbfsize;
        cmach_curbfsize += tbitfield->bitlength;
        return cmach_curbfoffset;
    }
}
#endif

UInt8 CMach_GetFunctionResultClass(TypeFunc* functype) {
    switch (functype->functype->type) {
        case TYPESTRUCT:
            if (functype->functype->type == TYPESTRUCT &&
                TYPE_STRUCT(functype->functype)->stype >= STRUCT_VECTOR_UCHAR &&
                TYPE_STRUCT(functype->functype)->stype <= STRUCT_VECTOR_PIXEL) {
                return FALSE;
            }
        case TYPECLASS:
        case TYPEMEMBERPOINTER:
            if (Type_RequiresMemoryReturn(functype->functype)) {
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }
}

Boolean Type_RequiresMemoryReturn(Type* type) {
    SInt32 stype;
    switch (type->type) {
        case TYPESTRUCT: {
            stype = TYPE_STRUCT(type)->stype;
            if (stype >= STRUCT_VECTOR_UCHAR && stype <= STRUCT_VECTOR_PIXEL) {
                return FALSE;
            }
            if (type->size <= 8 && !copts.incompatible_sfpe_double_params) {
                return FALSE;
            }
            return TRUE;
        }
        case TYPECLASS:
            if (type->size <= 8 &&
                (Boolean)(CClass_Constructor(TYPE_CLASS(type)) == NULL) &&
                !copts.incompatible_sfpe_double_params) {
                return FALSE;
            }
            return TRUE;
        case TYPEMEMBERPOINTER:
            if (type->size == 4) {
                return FALSE;
            }
            return TRUE;
        default:
            return FALSE;
    }
}

Boolean CMach_PassResultInHiddenArg(Type* type) {
    switch (type->type) {
        case TYPESTRUCT:
            if (IS_TYPE_VECTOR(type)) {
                return FALSE;
            }
        case TYPECLASS:
            return TRUE;
        case TYPEMEMBERPOINTER:
            if (type->size == 4) {
                return FALSE;
            }
            return TRUE;
        default:
            return FALSE;
    }
}

const char* CMach_GetCPU(void) {
    switch (copts.processor) {
        case CPU_PPC401:
            return "__PPC401__";
        case CPU_PPC403:
            return "__PPC403__";
        case CPU_PPC505:
            return "__PPC505__";
        case CPU_PPC509:
            return "__PPC509__";
        case CPU_PPC555:
            return "__PPC555__";
        case CPU_PPC556:
            return "__PPC556__";
        case CPU_PPC565:
            return "__PPC565__";
        case CPU_PPC601:
            return "__PPC601__";
        case CPU_PPC602:
            return "__PPC602__";
        case CPU_PPC603:
            return "__PPC603__";
        case CPU_PPC603e:
            return "__PPC603e__";
        case CPU_PPC604:
            return "__PPC604__";
        case CPU_PPC604e:
            return "__PPC604e__";
        case CPU_PPC740:
            return "__PPC740__";
        case CPU_PPC750:
            return "__PPC750__";
        case CPU_PPC801:
            return "__PPC801__";
        case CPU_PPC821:
            return "__PPC821__";
        case CPU_PPC823:
            return "__PPC823__";
        case CPU_PPC850:
            return "__PPC850__";
        case CPU_PPC860:
            return "__PPC860__";
        case CPU_PPC7400:
            return "__PPC7400__";
        case CPU_PPC7450:
            return "__PPC7450__";
        case CPU_PPC8240:
            return "__PPC8240__";
        case CPU_PPC8260:
            return "__PPC8260__";
        case CPU_PPCGEKKO:
            return "__PPCGEKKO__";
        case CPU_PPCELF:
            return "__PPCe500__";
        default:
            return NULL;
    }
}

Boolean CMach_FloatIsPowerOf2(Float flt) {
    return (flt.data.value == 2.0) ||
        (flt.data.value == 4.0) ||
        (flt.data.value == 8.0) ||
        (flt.data.value == 16.0) ||
        (flt.data.value == 32.0) ||
        (flt.data.value == 64.0) ||
        (flt.data.value == 128.0) ||
        (flt.data.value == 256.0) ||
        (flt.data.value == 512.0) ||
        (flt.data.value == 1024.0);
}

Boolean CMach_IsUnsignedType(Type* type) {
    switch (type->type) {
        case TYPEINT:
            switch (TYPE_INTEGRAL(type)->integral) {
                case IT_BOOL:
                case IT_UCHAR:
                case IT_WCHAR_T:
                case IT_USHORT:
                case IT_UINT:
                case IT_ULONG:
                case IT_ULONGLONG:
                    return TRUE;
                case IT_CHAR:
                    return copts.unsigned_char;
                default:
                    return FALSE;
            }
        case TYPEPOINTER:
            return TRUE;
        default:
            return FALSE;
    }
}

Type* CMach_GetCustomType(short token, short size, short signedness) {
    Type* type;

    switch (token) {
        case TK_CUSTOM_TYPE_113:
            type = (Type*)&stcustomtype113;
            break;
        case TK_CUSTOM_TYPE_114:
            type = (Type*)&stcustomtype114;
            break;
        case TK_UU_VEC2X32FLOAT_UU:
            type = (Type*)&stvec2x32float;
            break;
        default:
            type = NULL;
            break;
    }
    if (type == NULL || size != 0 || signedness != 0) {
        CError_Error(ERR_ILLEGAL_TYPE);
        return (Type*)&stsignedint;
    }
    return type;
}

#if VERSION < VERSION_GC_2_6
#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT (-110 + CError_LINE_DELTA)
#endif
CInt64 CMach_CustomIntConvert(Type* type, Type* oldtype, CInt64 value) {
    CInt64 result;
    union {
        float f;
        SInt32 l;
    } u;

    if (type == TYPE(&stcustomtype113)) {
        if (oldtype == TYPE(&stvec2x32float) ||
            oldtype == TYPE(&stfloat) ||
            oldtype == TYPE(&stdouble) ||
            oldtype == TYPE(&stlongdouble) ||
            oldtype == TYPE(&stshortdouble)) {
            CInt64_ConvertFromLongDouble(&result, (float)CInt64_ConvertToLongDouble(&value));
        } else {
            result = value;
        }
    } else if (type == TYPE(&stcustomtype114)) {
        if (oldtype == TYPE(&stvec2x32float) ||
            oldtype == TYPE(&stfloat) ||
            oldtype == TYPE(&stdouble) ||
            oldtype == TYPE(&stlongdouble) ||
            oldtype == TYPE(&stshortdouble)) {
            CInt64_ConvertFromLongDouble(&result, (float)CInt64_ConvertToLongDouble(&value));
            result.hi = result.lo;
        } else {
            result.lo = value.lo;
            result.hi = result.lo;
        }
    } else if (type == TYPE(&stvec2x32float)) {
        if (oldtype == TYPE(&stcustomtype113)) {
            u.f = CInt64_ConvertToLongDouble(&value);
            CInt64_SetLong(&result, u.l);
        } else if (oldtype == TYPE(&stcustomtype114)) {
            result = value;
        } else if (oldtype == TYPE(&stfloat) ||
                   oldtype == TYPE(&stdouble) ||
                   oldtype == TYPE(&stlongdouble) ||
                   oldtype == TYPE(&stshortdouble)) {
            result.lo = value.lo;
            result.hi = result.lo;
        } else {
            u.f = CInt64_ConvertToLongDouble(&value);
            CInt64_SetLong(&result, u.l);
            result.hi = result.lo;
        }
    } else if (oldtype == TYPE(&stcustomtype113)) {
        if (type == TYPE(&stcustomtype113) || type == TYPE(&stcustomtype114)) {
            result = value;
        } else if (type == TYPE(&stvec2x32float) ||
                   type == TYPE(&stfloat) ||
                   type == TYPE(&stdouble) ||
                   type == TYPE(&stlongdouble) ||
                   type == TYPE(&stshortdouble)) {
            CInt64_ConvertFromLongDouble(&result, (float)CInt64_ConvertToLongDouble(&value));
        } else {
            CInt64_SetLong(&result, value.lo);
        }
    } else if (oldtype == TYPE(&stcustomtype114)) {
        if (type == TYPE(&stcustomtype113) || type == TYPE(&stcustomtype114)) {
            return value;
        }
        if (type == TYPE(&stvec2x32float)) {
            u.f = value.lo;
            CInt64_SetLong(&result, u.l);
            result.hi = result.lo;
        } else if (type == TYPE(&stfloat) ||
                   type == TYPE(&stdouble) ||
                   type == TYPE(&stlongdouble) ||
                   type == TYPE(&stshortdouble)) {
            u.f = CInt64_ConvertToLongDouble(&value);
            CInt64_SetLong(&result, u.l);
        } else {
            CInt64_SetLong(&result, value.lo);
        }
    } else if (oldtype == TYPE(&stvec2x32float)) {
        if (type == TYPE(&stcustomtype113) ||
            type == TYPE(&stcustomtype114) ||
            type == TYPE(&stvec2x32float)) {
            return value;
        }
        CInt64_ConvertFromLongDouble(&result, value.lo);
    } else {
        CError_FATAL(2248);
    }
    return result;
}
#else
CInt64 CMach_CustomIntConvert(Type* type, Type* oldtype, CInt64 value) {
    CInt64 result;
    result.lo = 0;
    result.hi = 0;
    return result;
}
#endif

Float CMach_FloatReciprocal(Float flt) {
    flt.data.value = 1.0 / flt.data.value;
    return flt;
}

SInt32 CMach_RoundedSizeOf(Object* object) {
    SInt32 size = object->type->size;
    SInt32 align = CMach_GetTypeAlign(object->type) - 1;
    return (size + align) & ~align;
}

void CMach_ReInitRuntimeObjects(void) {
    HashNameNode* e0 = GetHashNameNodeExport("[0]");
    HashNameNode* e1 = GetHashNameNodeExport("[1]");
    HashNameNode* e2 = GetHashNameNodeExport("[2]");
    HashNameNode* e3 = GetHashNameNodeExport("[3]");
    HashNameNode* e4 = GetHashNameNodeExport("[4]");
    HashNameNode* e5 = GetHashNameNodeExport("[5]");
    HashNameNode* e6 = GetHashNameNodeExport("[6]");
    HashNameNode* e7 = GetHashNameNodeExport("[7]");
    HashNameNode* e8 = GetHashNameNodeExport("[8]");
    HashNameNode* e9 = GetHashNameNodeExport("[9]");
    HashNameNode* e10 = GetHashNameNodeExport("[10]");
    HashNameNode* e11 = GetHashNameNodeExport("[11]");
    HashNameNode* e12 = GetHashNameNodeExport("[12]");
    HashNameNode* e13 = GetHashNameNodeExport("[13]");
    HashNameNode* e14 = GetHashNameNodeExport("[14]");
    HashNameNode* e15 = GetHashNameNodeExport("[15]");
    HashNameNode* vuc = GetHashNameNodeExport("vector unsigned char");
    HashNameNode* vus = GetHashNameNodeExport("vector unsigned short");
    HashNameNode* vui = GetHashNameNodeExport("vector unsigned int");
    HashNameNode* vsc = GetHashNameNodeExport("vector signed char");
    HashNameNode* vss = GetHashNameNodeExport("vector signed short");
    HashNameNode* vsi = GetHashNameNodeExport("vector signed int");
    HashNameNode* vbc = GetHashNameNodeExport("vector bool char");
    HashNameNode* vbs = GetHashNameNodeExport("vector bool short");
    HashNameNode* vbi = GetHashNameNodeExport("vector bool int");
    HashNameNode* vf = GetHashNameNodeExport("vector float");
    HashNameNode* vp = GetHashNameNodeExport("vector pixel");

    stvectorunsignedchar.name = vuc;
    stvectorunsignedshort.name = vus;
    stvectorunsignedlong.name = vui;
    stvectorsignedchar.name = vsc;
    stvectorsignedshort.name = vss;
    stvectorsignedlong.name = vsi;
    stvectorboolchar.name = vbc;
    stvectorboolshort.name = vbs;
    stvectorboollong.name = vbi;
    stvectorfloat.name = vf;
    stvectorpixel.name = vp;

    stVUC_unsignedchar0.name = e0;
    stVUC_unsignedchar1.name = e1;
    stVUC_unsignedchar2.name = e2;
    stVUC_unsignedchar3.name = e3;
    stVUC_unsignedchar4.name = e4;
    stVUC_unsignedchar5.name = e5;
    stVUC_unsignedchar6.name = e6;
    stVUC_unsignedchar7.name = e7;
    stVUC_unsignedchar8.name = e8;
    stVUC_unsignedchar9.name = e9;
    stVUC_unsignedchar10.name = e10;
    stVUC_unsignedchar11.name = e11;
    stVUC_unsignedchar12.name = e12;
    stVUC_unsignedchar13.name = e13;
    stVUC_unsignedchar14.name = e14;
    stVUC_unsignedchar15.name = e15;
    stVSC_signedchar0.name = e0;
    stVSC_signedchar1.name = e1;
    stVSC_signedchar2.name = e2;
    stVSC_signedchar3.name = e3;
    stVSC_signedchar4.name = e4;
    stVSC_signedchar5.name = e5;
    stVSC_signedchar6.name = e6;
    stVSC_signedchar7.name = e7;
    stVSC_signedchar8.name = e8;
    stVSC_signedchar9.name = e9;
    stVSC_signedchar10.name = e10;
    stVSC_signedchar11.name = e11;
    stVSC_signedchar12.name = e12;
    stVSC_signedchar13.name = e13;
    stVSC_signedchar14.name = e14;
    stVSC_signedchar15.name = e15;

    stVUS_unsignedshort0.name = e0;
    stVUS_unsignedshort1.name = e1;
    stVUS_unsignedshort2.name = e2;
    stVUS_unsignedshort3.name = e3;
    stVUS_unsignedshort4.name = e4;
    stVUS_unsignedshort5.name = e5;
    stVUS_unsignedshort6.name = e6;
    stVUS_unsignedshort7.name = e7;
    stVSS_signedshort0.name = e0;
    stVSS_signedshort1.name = e1;
    stVSS_signedshort2.name = e2;
    stVSS_signedshort3.name = e3;
    stVSS_signedshort4.name = e4;
    stVSS_signedshort5.name = e5;
    stVSS_signedshort6.name = e6;
    stVSS_signedshort7.name = e7;

    stVUL_unsignedlong0.name = e0;
    stVUL_unsignedlong1.name = e1;
    stVUL_unsignedlong2.name = e2;
    stVUL_unsignedlong3.name = e3;
    stVSL_signedlong0.name = e0;
    stVSL_signedlong1.name = e1;
    stVSL_signedlong2.name = e2;
    stVSL_signedlong3.name = e3;

    stVF_float0.name = e0;
    stVF_float1.name = e1;
    stVF_float2.name = e2;
    stVF_float3.name = e3;
}

#define CMACH_SKIP_REGISTER_ARGUMENT() \
    do { \
        if ((tk = lex()) != ')') { \
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR); \
            return; \
        } \
        tk = lex(); \
        return; \
    } while (0)

#pragma sym off
#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT (-68 + CError_LINE_DELTA)
#endif
#if VERSION == VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT (42 + CError_LINE_DELTA)
#endif
void CMach_ParseGlobalRegisterVariable(DeclInfo* declinfo) {
    ObjRegInfo reginfo;
    int is_static;
    int count;
    char* regname;
    Type* type;
    RegClass typeclass;
    RegClass regclass;
    int reg;
    char name1[2];
    char name2[2];
    RegInfo* scan;
    int first;

    count = 0;
    is_static = declinfo->storageclass == TK_STATIC;
    type = declinfo->thetype;
    if ((tk = lex()) != '(') {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        return;
    }
    tk = lex();
    if (tk != TK_EOL) {
        if (!ok_for_global_registers) {
            PPCError_Error(PPCERR_GLOBAL_REGISTER_AFTER_CODE);
            CMACH_SKIP_REGISTER_ARGUMENT();
        }
        if (cscope_currentfunc) {
            PPCError_Warning(PPCERR_GLOBAL_REGISTER_IN_FUNCTION);
            CMACH_SKIP_REGISTER_ARGUMENT();
        }

        if (TYPE_FITS_IN_REGISTER(type) || TYPE_IS_SOFT_FLOAT(type)) {
            if (TYPE_IS_GPR_PAIR(type)) {
                PPCError_Error(PPCERR_GLOBAL_REGISTER_NOT_SINGLE);
                CMACH_SKIP_REGISTER_ARGUMENT();
            }
            typeclass = RegClass_GPR;
        } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) || IS_TYPE_VEC2X32FLOAT(type)) {
            typeclass = RegClass_FPR;
        } else if (IS_TYPE_VECTOR(type)) {
            typeclass = RegClass_VR;
        } else {
            PPCError_Error(PPCERR_GLOBAL_REGISTER_BAD_TYPE);
            CMACH_SKIP_REGISTER_ARGUMENT();
        }

        if (declinfo->storageclass &&
            ((int)declinfo->storageclass < TK_REGISTER ||
             (int)declinfo->storageclass > TK_EXTERN)) {
            PPCError_Error(PPCERR_GLOBAL_REGISTER_STORAGE_CLASS);
            CMACH_SKIP_REGISTER_ARGUMENT();
        }

        if (tk == TK_STRING) {
            regname = tkstring;
            switch (regname[0]) {
                case 'r':
                    regclass = RegClass_GPR;
                    break;
                case 'f':
                    regclass = RegClass_FPR;
                    break;
                case 'v':
                    regclass = RegClass_VR;
                    break;
                default:
                    PPCError_Error(PPCERR_GLOBAL_REGISTER_SYNTAX);
                    CMACH_SKIP_REGISTER_ARGUMENT();
            }

            if (typeclass != regclass) {
                name1[0] = GetRegisterClassName(typeclass);
                name1[1] = 0;
                name2[0] = GetRegisterClassName(regclass);
                name2[1] = 0;
                PPCError_Error(PPCERR_GLOBAL_REGISTER_WRONG_CLASS, name2, name1);
                CMACH_SKIP_REGISTER_ARGUMENT();
            }

            if (regname[1]) {
                count = sscanf(regname + 1, "%i", &reg);
            }
            if (count == -1 || count == 0) {
                PPCError_Error(PPCERR_GLOBAL_REGISTER_SYNTAX);
                CMACH_SKIP_REGISTER_ARGUMENT();
            }

            first = GetABIFirstNonVolatile(regclass);
            if (reg < first || reg >= first + num_register_variables[regclass]) {
                name2[0] = GetRegisterClassName(regclass);
                name2[1] = 0;
                PPCError_Error(
                    PPCERR_GLOBAL_REGISTER_RANGE, num_register_variables[regclass], name2, first
                );
                CMACH_SKIP_REGISTER_ARGUMENT();
            }
        } else {
            PPCError_Error(PPCERR_GLOBAL_REGISTER_SYNTAX);
            CMACH_SKIP_REGISTER_ARGUMENT();
        }
    } else {
        PPCError_Error(PPCERR_GLOBAL_REGISTER_SYNTAX);
        CMACH_SKIP_REGISTER_ARGUMENT();
    }

    reginfo.reg = reg;
    reginfo.rclass = regclass;
    if (declinfo->reginfo.rclass == reginfo.rclass && declinfo->reginfo.reg == reginfo.reg) {
        return;
    }
    if (declinfo->reginfo.reg) {
        name1[0] = GetRegisterClassName(declinfo->reginfo.rclass);
        name1[1] = 0;
        name2[0] = GetRegisterClassName(reginfo.rclass);
        name2[1] = 0;
        PPCError_Error(
            PPCERR_GLOBAL_REGISTER_REASSIGNED, name1, declinfo->reginfo.reg, name2, reginfo.reg
        );
        CMACH_SKIP_REGISTER_ARGUMENT();
    }

    for (scan = reg_info_list; scan; scan = scan->next) {
        if (reginfo.rclass == scan->reginfo.rclass) {
            CError_ASSERT(2491, declinfo->name && &declinfo->name->name[0]);
            if (declinfo->name == scan->name && declinfo->nspace == scan->nspace) {
                if (is_static != scan->is_static) {
                    PPCError_Error(PPCERR_GLOBAL_REGISTER_NAME_CLASH, declinfo->name->name);
                } else if (reginfo.reg != scan->reginfo.reg) {
                    name1[0] = GetRegisterClassName(scan->reginfo.rclass);
                    name1[1] = 0;
                    name2[0] = GetRegisterClassName(reginfo.rclass);
                    name2[1] = 0;
                    PPCError_Error(
                        PPCERR_GLOBAL_REGISTER_REASSIGNED,
                        name1,
                        scan->reginfo.reg,
                        name2,
                        reginfo.reg
                    );
                } else {
                    declinfo->reginfo = reginfo;
                }
                CMACH_SKIP_REGISTER_ARGUMENT();
            }
            if (scan->name && reginfo.reg == scan->reginfo.reg) {
                name1[0] = GetRegisterClassName(scan->reginfo.rclass);
                name1[1] = 0;
                PPCError_Error(
                    PPCERR_GLOBAL_REGISTER_ALREADY_ASSIGNED,
                    scan->name->name,
                    name1,
                    scan->reginfo.reg
                );
                CMACH_SKIP_REGISTER_ARGUMENT();
            }
        }
    }

    scan = galloc(sizeof(RegInfo));
    memclrw(scan, sizeof(RegInfo));
    declinfo->reginfo = reginfo;
    scan->reginfo = declinfo->reginfo;
    scan->name = declinfo->name;
    scan->object = NULL;
    scan->nspace = declinfo->nspace;
    scan->is_static = is_static;
    scan->next = reg_info_list;
    reg_info_list = scan;
    CMACH_SKIP_REGISTER_ARGUMENT();
}

#pragma sym reset

void CMach_GetBackEndOptions(struct CInlineBackendOptions* options) {
    options->processor = copts.processor;
    options->scheduling = copts.scheduling;
    options->schedule_factor = copts.schedule_factor;
    options->debuglisting = copts.debuglisting;
    options->profile = copts.profile;
    options->traceback = copts.traceback;
    options->disable_registers = copts.disable_registers;
    options->fp_contract = copts.fp_contract;
    options->no_register_save_helpers = copts.no_register_save_helpers;
    options->unroll_speculative = copts.unroll_speculative;
    options->unroll_instr_limit = copts.unroll_instr_limit;
    options->unroll_factor_limit = copts.unroll_factor_limit;
    options->altivec_model = copts.altivec_model;
    options->altivec_vrsave = copts.altivec_vrsave;
    options->function_align = copts.function_align;
    options->cats = copts.cats;
    options->force_cats = copts.force_cats;
    options->sdata_threshold = copts.sdata_threshold;
    options->sdata2_threshold = copts.sdata2_threshold;
    options->pool_data = copts.pool_data;
    options->common = copts.common;
    options->use_lmw_stmw = copts.use_lmw_stmw;
    options->codegen_pic = copts.codegen_pic;
    options->no_comment_records = copts.no_comment_records;
    options->rel109_offset = copts.rel109_offset;
    options->interruptList = copts.interruptList;
    options->interruptOptions = copts.interruptOptions;
    options->incompatible_return_small_structs = copts.incompatible_return_small_structs;
    options->create_file_object = copts.create_file_object;
    options->incompatible_sfpe_double_params = copts.incompatible_sfpe_double_params;
    options->ibm_stackframe = copts.ibm_stackframe;
    options->gen_isel = copts.gen_isel;
    options->gen_fsel = copts.gen_fsel;
    options->gecko_float_typecons = copts.gecko_float_typecons;
#if VERSION >= VERSION_GC_2_0
    options->disable_vector_frame = copts.disable_vector_frame;
#endif
    options->opt_bcc_lr_ctr = copts.opt_bcc_lr_ctr;
    options->optimizationlevel = copts.optimizationlevel;
    options->optimizesize = copts.optimizesize;
    options->optimizewithasm = copts.optimizewithasm;
    options->commonsubs = copts.commonsubs;
    options->loopinvariants = copts.loopinvariants;
    options->propagation = copts.propagation;
    options->deadstore = copts.deadstore;
    options->strengthreduction = copts.strengthreduction;
    options->strengthreductionstrict = copts.strengthreductionstrict;
    options->deadcode = copts.deadcode;
    options->lifetimes = copts.lifetimes;
    options->unrolling = copts.unrolling;
    options->vectorizeloops = copts.vectorizeloops;
    options->multiplepasses = copts.multiplepasses;
    options->opt_pointer_analysis = copts.opt_pointer_analysis;
    options->opt_pointer_analysis_mode = copts.opt_pointer_analysis_mode;
    options->unrollfactor = copts.unrollfactor;
    options->unrollinstrfactor = copts.unrollinstrfactor;
}

void CMach_SetBackEndOptions(struct CInlineBackendOptions* options) {
    copts.processor = options->processor;
    copts.scheduling = options->scheduling;
    copts.schedule_factor = options->schedule_factor;
    copts.debuglisting = options->debuglisting;
    copts.profile = options->profile;
    copts.traceback = options->traceback;
    copts.disable_registers = options->disable_registers;
    copts.fp_contract = options->fp_contract;
    copts.no_register_save_helpers = options->no_register_save_helpers;
    copts.unroll_speculative = options->unroll_speculative;
    copts.unroll_instr_limit = options->unroll_instr_limit;
    copts.unroll_factor_limit = options->unroll_factor_limit;
    copts.altivec_model = options->altivec_model;
    copts.altivec_vrsave = options->altivec_vrsave;
    copts.function_align = options->function_align;
    copts.cats = options->cats;
    copts.force_cats = options->force_cats;
    copts.sdata_threshold = options->sdata_threshold;
    copts.sdata2_threshold = options->sdata2_threshold;
    copts.pool_data = options->pool_data;
    copts.common = options->common;
    copts.use_lmw_stmw = options->use_lmw_stmw;
    copts.codegen_pic = options->codegen_pic;
    copts.no_comment_records = options->no_comment_records;
    copts.rel109_offset = options->rel109_offset;
    copts.interruptList = options->interruptList;
    copts.interruptOptions = options->interruptOptions;
    copts.incompatible_return_small_structs = options->incompatible_return_small_structs;
    copts.create_file_object = options->create_file_object;
    copts.incompatible_sfpe_double_params = options->incompatible_sfpe_double_params;
    copts.ibm_stackframe = options->ibm_stackframe;
    copts.gen_isel = options->gen_isel;
    copts.gen_fsel = options->gen_fsel;
    copts.gecko_float_typecons = options->gecko_float_typecons;
#if VERSION >= VERSION_GC_2_0
    copts.disable_vector_frame = options->disable_vector_frame;
#endif
    copts.opt_bcc_lr_ctr = options->opt_bcc_lr_ctr;
    copts.optimizationlevel = options->optimizationlevel;
    copts.optimizesize = options->optimizesize;
    copts.optimizewithasm = options->optimizewithasm;
    copts.commonsubs = options->commonsubs;
    copts.loopinvariants = options->loopinvariants;
    copts.propagation = options->propagation;
    copts.deadstore = options->deadstore;
    copts.strengthreduction = options->strengthreduction;
    copts.strengthreductionstrict = options->strengthreductionstrict;
    copts.deadcode = options->deadcode;
    copts.lifetimes = options->lifetimes;
    copts.unrolling = options->unrolling;
    copts.vectorizeloops = options->vectorizeloops;
    copts.multiplepasses = options->multiplepasses;
    copts.opt_pointer_analysis = options->opt_pointer_analysis;
    copts.opt_pointer_analysis_mode = options->opt_pointer_analysis_mode;
    copts.unrollfactor = options->unrollfactor;
    copts.unrollinstrfactor = options->unrollinstrfactor;
}

#pragma pack(2)
