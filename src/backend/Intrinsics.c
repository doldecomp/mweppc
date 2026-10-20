#include "compiler/Intrinsics.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CScope.h"
#include "compiler/enode.h"
#include "compiler/FunctionCalls.h"
#include "compiler/InstrSelection.h"
#include "compiler/objects.h"
#include "compiler/Operands.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/scopes.h"
#include "compiler/StackFrame.h"
#include "compiler/StructMoves.h"
#include "compiler/types.h"

Object* __memcpy_object;
static Object* intrinsics[MaxIntrinsics];
static Object* cur_intrinsic_object;

static void va_info_intrinsic(short opcode, ENode* expr, short outputReg, Operand* output);

static TypePointer stvectorunsignedchar_ptr = {TYPEPOINTER, 4, TYPE(&stvectorunsignedchar), 0};
static TypePointer stvectorsignedchar_ptr = {TYPEPOINTER, 4, TYPE(&stvectorsignedchar), 0};
static TypePointer stvectorboolchar_ptr = {TYPEPOINTER, 4, TYPE(&stvectorboolchar), 0};
static TypePointer stvectorunsignedshort_ptr = {TYPEPOINTER, 4, TYPE(&stvectorunsignedshort), 0};
static TypePointer stvectorsignedshort_ptr = {TYPEPOINTER, 4, TYPE(&stvectorsignedshort), 0};
static TypePointer stvectorboolshort_ptr = {TYPEPOINTER, 4, TYPE(&stvectorboolshort), 0};
static TypePointer stvectorunsignedlong_ptr = {TYPEPOINTER, 4, TYPE(&stvectorunsignedlong), 0};
static TypePointer stvectorsignedlong_ptr = {TYPEPOINTER, 4, TYPE(&stvectorsignedlong), 0};
static TypePointer stvectorboollong_ptr = {TYPEPOINTER, 4, TYPE(&stvectorboollong), 0};
static TypePointer stvectorfloat_ptr = {TYPEPOINTER, 4, TYPE(&stvectorfloat), 0};
static TypePointer stvectorpixel_ptr = {TYPEPOINTER, 4, TYPE(&stvectorpixel), 0};
static TypePointer stunsignedchar_ptr = {TYPEPOINTER, 4, TYPE(&stunsignedchar), 0};
static TypePointer stsignedchar_ptr = {TYPEPOINTER, 4, TYPE(&stsignedchar), 0};
static TypePointer stunsignedshort_ptr = {TYPEPOINTER, 4, TYPE(&stunsignedshort), 0};
static TypePointer stsignedshort_ptr = {TYPEPOINTER, 4, TYPE(&stsignedshort), 0};
static TypePointer stunsignedlong_ptr = {TYPEPOINTER, 4, TYPE(&stunsignedlong), 0};
static TypePointer stsignedlong_ptr = {TYPEPOINTER, 4, TYPE(&stsignedlong), 0};
static TypePointer stunsignedint_ptr = {TYPEPOINTER, 4, TYPE(&stunsignedint), 0};
static TypePointer stsignedint_ptr = {TYPEPOINTER, 4, TYPE(&stsignedint), 0};
static TypePointer stfloat_ptr = {TYPEPOINTER, 4, TYPE(&stfloat), 0};

typedef struct TypeTable22 {
    Type* rtype;
    Type* arg1;
    int opcode1;
    int opcode2;
} TypeTable22;

typedef struct TypeTable11 {
    Type* rtype;
    int opcode;
} TypeTable11;

typedef struct TypeTable21 {
    Type* rtype;
    Type* arg1;
    int opcode;
} TypeTable21;

typedef struct TypeTable31 {
    Type* rtype;
    Type* arg1;
    Type* arg2;
    int opcode;
} TypeTable31;

typedef struct TypeTable41 {
    Type* rtype;
    Type* arg1;
    Type* arg2;
    Type* arg3;
    int opcode;
} TypeTable41;

// clang-format off
static TypeTable31 vector_add_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VADDUBM,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VADDUBM,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VADDUBM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VADDUBM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VADDUBM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VADDUBM,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VADDUHM,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VADDUHM,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VADDUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VADDUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VADDUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VADDUHM,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VADDUWM,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VADDUWM,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VADDUWM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VADDUWM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VADDUWM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VADDUWM,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VADDFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_addc_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VADDCUW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_adds_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VADDUBS,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VADDUBS,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VADDUBS,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VADDSBS,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VADDSBS,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VADDSBS,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VADDUHS,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VADDUHS,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VADDUHS,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VADDSHS,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VADDSHS,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VADDSHS,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VADDUWS,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VADDUWS,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VADDUWS,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VADDSWS,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VADDSWS,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VADDSWS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_and_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VAND,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VAND,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VAND,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VAND,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VAND,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VAND,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), PC_VAND,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VAND,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VAND,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VAND,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VAND,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VAND,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VAND,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VAND,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VAND,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VAND,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VAND,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VAND,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VAND,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VAND,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VAND,
    TYPE(&stvectorfloat), TYPE(&stvectorboollong), TYPE(&stvectorfloat), PC_VAND,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorboollong), PC_VAND,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VAND,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_andc_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VANDC,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VANDC,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VANDC,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VANDC,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VANDC,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VANDC,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), PC_VANDC,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VANDC,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VANDC,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VANDC,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VANDC,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VANDC,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VANDC,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VANDC,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VANDC,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VANDC,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VANDC,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VANDC,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VANDC,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VANDC,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VANDC,
    TYPE(&stvectorfloat), TYPE(&stvectorboollong), TYPE(&stvectorfloat), PC_VANDC,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorboollong), PC_VANDC,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VANDC,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_avg_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VAVGUB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VAVGSB,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VAVGUH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VAVGSH,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VAVGUW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VAVGSW,
    NULL, NULL, NULL, 0
};
static TypeTable21 vector_ceil_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VRFIP,
    NULL, NULL, 0
};
static TypeTable31 vector_cmpb_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPBFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_cmpeq_type_table[] = {
    TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPEQUB,
    TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPEQUB,
    TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPEQUH,
    TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPEQUH,
    TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPEQUW,
    TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPEQUW,
    TYPE(&stvectorboollong), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPEQFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_cmpge_type_table[] = {
    TYPE(&stvectorboollong), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGEFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_cmpgt_type_table[] = {
    TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stvectorboollong), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGTFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_ctf_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), PC_VCFUX,
    TYPE(&stvectorfloat), TYPE(&stvectorsignedlong), TYPE(&stsignedint), PC_VCFSX,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_cts_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorfloat), TYPE(&stsignedint), PC_VCTSXS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_ctu_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorfloat), TYPE(&stsignedint), PC_VCTUXS,
    NULL, NULL, NULL, 0
};
static TypeTable21 vector_dss_type_table[] = {
    TYPE(&stvoid), TYPE(&stsignedint), PC_DSS,
    NULL, NULL, 0
};
static TypeTable11 vector_dssall_type_table[] = {
    TYPE(&stvoid), PC_DSSALL,
    NULL, 0
};
static TypeTable41 vector_datastream_type_table[] = {
    TYPE(&stvoid), TYPE(&stvectorunsignedchar_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stvectorsignedchar_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stvectorboolchar_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stvectorunsignedshort_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stvectorsignedshort_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stvectorboolshort_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stvectorpixel_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stvectorunsignedlong_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stvectorsignedlong_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stvectorboollong_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stvectorfloat_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stunsignedchar_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stsignedchar_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stunsignedshort_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stsignedshort_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stunsignedint_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stsignedint_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stunsignedlong_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stsignedlong_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    TYPE(&stvoid), TYPE(&stfloat_ptr), TYPE(&stsignedint), TYPE(&stsignedint), PC_DST,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable21 vector_expte_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VEXPTEFP,
    NULL, NULL, 0
};
static TypeTable21 vector_floor_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VRFIM,
    NULL, NULL, 0
};
static TypeTable31 vector_load_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stvectorunsignedchar_ptr), PC_LVX,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedchar_ptr), PC_LVX,
    TYPE(&stvectorsignedchar), TYPE(&stsignedint), TYPE(&stvectorsignedchar_ptr), PC_LVX,
    TYPE(&stvectorsignedchar), TYPE(&stsignedint), TYPE(&stsignedchar_ptr), PC_LVX,
    TYPE(&stvectorboolchar), TYPE(&stsignedint), TYPE(&stvectorboolchar_ptr), PC_LVX,
    TYPE(&stvectorunsignedshort), TYPE(&stsignedint), TYPE(&stvectorunsignedshort_ptr), PC_LVX,
    TYPE(&stvectorunsignedshort), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_LVX,
    TYPE(&stvectorsignedshort), TYPE(&stsignedint), TYPE(&stvectorsignedshort_ptr), PC_LVX,
    TYPE(&stvectorsignedshort), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_LVX,
    TYPE(&stvectorboolshort), TYPE(&stsignedint), TYPE(&stvectorboolshort_ptr), PC_LVX,
    TYPE(&stvectorpixel), TYPE(&stsignedint), TYPE(&stvectorpixel_ptr), PC_LVX,
    TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stvectorunsignedlong_ptr), PC_LVX,
    TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedint_ptr), PC_LVX,
    TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedlong_ptr), PC_LVX,
    TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stvectorsignedlong_ptr), PC_LVX,
    TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedint_ptr), PC_LVX,
    TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedlong_ptr), PC_LVX,
    TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stvectorboollong_ptr), PC_LVX,
    TYPE(&stvectorfloat), TYPE(&stsignedint), TYPE(&stvectorfloat_ptr), PC_LVX,
    TYPE(&stvectorfloat), TYPE(&stsignedint), TYPE(&stfloat_ptr), PC_LVX,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_loade_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedchar_ptr), PC_LVEBX,
    TYPE(&stvectorsignedchar), TYPE(&stsignedint), TYPE(&stsignedchar_ptr), PC_LVEBX,
    TYPE(&stvectorunsignedshort), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_LVEHX,
    TYPE(&stvectorsignedshort), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_LVEHX,
    TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedint_ptr), PC_LVEWX,
    TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedlong_ptr), PC_LVEWX,
    TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedint_ptr), PC_LVEWX,
    TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedlong_ptr), PC_LVEWX,
    TYPE(&stvectorfloat), TYPE(&stsignedint), TYPE(&stfloat_ptr), PC_LVEWX,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_loadl_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stvectorunsignedchar_ptr), PC_LVXL,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedchar_ptr), PC_LVXL,
    TYPE(&stvectorsignedchar), TYPE(&stsignedint), TYPE(&stvectorsignedchar_ptr), PC_LVXL,
    TYPE(&stvectorsignedchar), TYPE(&stsignedint), TYPE(&stsignedchar_ptr), PC_LVXL,
    TYPE(&stvectorboolchar), TYPE(&stsignedint), TYPE(&stvectorboolchar_ptr), PC_LVXL,
    TYPE(&stvectorunsignedshort), TYPE(&stsignedint), TYPE(&stvectorunsignedshort_ptr), PC_LVXL,
    TYPE(&stvectorunsignedshort), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_LVXL,
    TYPE(&stvectorsignedshort), TYPE(&stsignedint), TYPE(&stvectorsignedshort_ptr), PC_LVXL,
    TYPE(&stvectorsignedshort), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_LVXL,
    TYPE(&stvectorboolshort), TYPE(&stsignedint), TYPE(&stvectorboolshort_ptr), PC_LVXL,
    TYPE(&stvectorpixel), TYPE(&stsignedint), TYPE(&stvectorpixel_ptr), PC_LVXL,
    TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stvectorunsignedlong_ptr), PC_LVXL,
    TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedint_ptr), PC_LVXL,
    TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedlong_ptr), PC_LVXL,
    TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stvectorsignedlong_ptr), PC_LVXL,
    TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedint_ptr), PC_LVXL,
    TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedlong_ptr), PC_LVXL,
    TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stvectorboollong_ptr), PC_LVXL,
    TYPE(&stvectorfloat), TYPE(&stsignedint), TYPE(&stvectorfloat_ptr), PC_LVXL,
    TYPE(&stvectorfloat), TYPE(&stsignedint), TYPE(&stfloat_ptr), PC_LVXL,
    NULL, NULL, NULL, 0
};
static TypeTable21 vector_loge_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VLOGEFP,
    NULL, NULL, 0
};
static TypeTable31 vector_lvsl_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedchar_ptr), PC_LVSL,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stsignedchar_ptr), PC_LVSL,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_LVSL,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_LVSL,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedint_ptr), PC_LVSL,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedlong_ptr), PC_LVSL,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stsignedint_ptr), PC_LVSL,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stsignedlong_ptr), PC_LVSL,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stfloat_ptr), PC_LVSL,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_lvsr_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedchar_ptr), PC_LVSR,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stsignedchar_ptr), PC_LVSR,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_LVSR,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_LVSR,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedint_ptr), PC_LVSR,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedlong_ptr), PC_LVSR,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stsignedint_ptr), PC_LVSR,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stsignedlong_ptr), PC_LVSR,
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stfloat_ptr), PC_LVSR,
    NULL, NULL, NULL, 0
};
static TypeTable41 vector_madd_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VMADDFP,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable41 vector_madds_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMHADDSHS,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable31 vector_max_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMAXUB,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VMAXUB,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VMAXUB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VMAXSB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VMAXSB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VMAXSB,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMAXUH,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VMAXUH,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VMAXUH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMAXSH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VMAXSH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VMAXSH,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VMAXUW,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VMAXUW,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VMAXUW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VMAXSW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VMAXSW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VMAXSW,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VMAXFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_mergeh_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMRGHB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VMRGHB,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), PC_VMRGHB,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMRGHH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMRGHH,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VMRGHH,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorpixel), PC_VMRGHH,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VMRGHW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VMRGHW,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VMRGHW,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VMRGHW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_mergel_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMRGLB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VMRGLB,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), PC_VMRGLB,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMRGLH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMRGLH,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VMRGLH,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorpixel), PC_VMRGLH,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VMRGLW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VMRGLW,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VMRGLW,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VMRGLW,
    NULL, NULL, NULL, 0
};
static TypeTable11 vector_mfvscr_type_table[] = {
    TYPE(&stvectorunsignedshort), PC_MFVSCR,
    NULL, 0
};
static TypeTable31 vector_min_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMINUB,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VMINUB,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VMINUB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VMINSB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VMINSB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VMINSB,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMINUH,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VMINUH,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VMINUH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMINSH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VMINSH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VMINSH,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VMINUW,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VMINUW,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VMINUW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VMINSW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VMINSW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VMINSW,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VMINFP,
    NULL, NULL, NULL, 0
};
static TypeTable41 vector_mladd_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMLADDUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMLADDUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMLADDUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMLADDUHM,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable41 vector_mradds_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMHRADDSHS,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable41 vector_msum_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedlong), PC_VMSUMUBM,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedlong), PC_VMSUMUHM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorsignedlong), PC_VMSUMMBM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedlong), PC_VMSUMSHM,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable41 vector_msums_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedlong), PC_VMSUMUHS,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedlong), PC_VMSUMSHS,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable21 vector_mtvscr_type_table[] = {
    TYPE(&stvoid), TYPE(&stvectorunsignedchar), PC_MTVSCR,
    TYPE(&stvoid), TYPE(&stvectorsignedchar), PC_MTVSCR,
    TYPE(&stvoid), TYPE(&stvectorboolchar), PC_MTVSCR,
    TYPE(&stvoid), TYPE(&stvectorunsignedshort), PC_MTVSCR,
    TYPE(&stvoid), TYPE(&stvectorsignedshort), PC_MTVSCR,
    TYPE(&stvoid), TYPE(&stvectorboolshort), PC_MTVSCR,
    TYPE(&stvoid), TYPE(&stvectorpixel), PC_MTVSCR,
    TYPE(&stvoid), TYPE(&stvectorunsignedlong), PC_MTVSCR,
    TYPE(&stvoid), TYPE(&stvectorsignedlong), PC_MTVSCR,
    TYPE(&stvoid), TYPE(&stvectorboollong), PC_MTVSCR,
    NULL, NULL, 0
};
static TypeTable31 vector_mule_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMULEUB,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VMULESB,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMULEUH,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMULESH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_mulo_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMULOUB,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VMULOSB,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMULOUH,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMULOSH,
    NULL, NULL, NULL, 0
};
static TypeTable41 vector_nmsub_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VNMSUBFP,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable31 vector_nor_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VNOR,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VNOR,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), PC_VNOR,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VNOR,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VNOR,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VNOR,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VNOR,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VNOR,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VNOR,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VNOR,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_or_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VOR,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VOR,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VOR,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VOR,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VOR,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VOR,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), PC_VOR,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VOR,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VOR,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VOR,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VOR,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VOR,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VOR,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VOR,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VOR,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VOR,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VOR,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VOR,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VOR,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VOR,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VOR,
    TYPE(&stvectorfloat), TYPE(&stvectorboollong), TYPE(&stvectorfloat), PC_VOR,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorboollong), PC_VOR,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VOR,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_pack_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VPKUHUM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VPKUHUM,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VPKUHUM,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VPKUWUM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VPKUWUM,
    TYPE(&stvectorboolshort), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VPKUWUM,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_packpx_type_table[] = {
    TYPE(&stvectorpixel), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VPKPX,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_packs_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VPKUHUS,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VPKSHSS,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VPKUWUS,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VPKSWSS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_packsu_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VPKUHUS,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VPKSHUS,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VPKUWUS,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VPKSWUS,
    NULL, NULL, NULL, 0
};
static TypeTable41 vector_perm_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VPERM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), PC_VPERM,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VPERM,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedchar), PC_VPERM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedchar), PC_VPERM,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedchar), PC_VPERM,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorunsignedchar), PC_VPERM,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedchar), PC_VPERM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedchar), PC_VPERM,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedchar), PC_VPERM,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorunsignedchar), PC_VPERM,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable21 vector_re_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VREFP,
    NULL, NULL, 0
};
static TypeTable31 vector_rl_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VRLB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), PC_VRLB,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VRLH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedshort), PC_VRLH,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VRLW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedlong), PC_VRLW,
    NULL, NULL, NULL, 0
};
static TypeTable21 vector_round_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VRFIN,
    NULL, NULL, 0
};
static TypeTable21 vector_rsqrte_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VRSQRTEFP,
    NULL, NULL, 0
};
static TypeTable41 vector_sel_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSEL,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VSEL,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), PC_VSEL,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VSEL,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VSEL,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), PC_VSEL,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VSEL,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VSEL,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedshort), PC_VSEL,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VSEL,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VSEL,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VSEL,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VSEL,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VSEL,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedlong), PC_VSEL,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VSEL,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VSEL,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VSEL,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorunsignedlong), PC_VSEL,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorboollong), PC_VSEL,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable31 vector_sl_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSLB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), PC_VSLB,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VSLH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedshort), PC_VSLH,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VSLW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedlong), PC_VSLW,
    NULL, NULL, NULL, 0
};
static TypeTable41 vector_sld_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stsignedint), PC_VSLDOI,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stsignedint), PC_VSLDOI,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stsignedint), PC_VSLDOI,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stsignedint), PC_VSLDOI,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stsignedint), PC_VSLDOI,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), PC_VSLDOI,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stsignedint), PC_VSLDOI,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stsignedint), PC_VSLDOI,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable31 vector_sll_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSL,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedshort), PC_VSL,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedlong), PC_VSL,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), PC_VSL,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedshort), PC_VSL,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedlong), PC_VSL,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VSL,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedshort), PC_VSL,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedlong), PC_VSL,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedchar), PC_VSL,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VSL,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedlong), PC_VSL,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedchar), PC_VSL,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedshort), PC_VSL,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedlong), PC_VSL,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedchar), PC_VSL,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VSL,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedlong), PC_VSL,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorunsignedchar), PC_VSL,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorunsignedshort), PC_VSL,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorunsignedlong), PC_VSL,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedchar), PC_VSL,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedshort), PC_VSL,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VSL,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedchar), PC_VSL,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedshort), PC_VSL,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedlong), PC_VSL,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedchar), PC_VSL,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedshort), PC_VSL,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VSL,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_slo_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSLO,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorsignedchar), PC_VSLO,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), PC_VSLO,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VSLO,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedchar), PC_VSLO,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorsignedchar), PC_VSLO,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedchar), PC_VSLO,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedchar), PC_VSLO,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorunsignedchar), PC_VSLO,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorsignedchar), PC_VSLO,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedchar), PC_VSLO,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorsignedchar), PC_VSLO,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedchar), PC_VSLO,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedchar), PC_VSLO,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorunsignedchar), PC_VSLO,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorsignedchar), PC_VSLO,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_splat_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stsignedint), PC_VSPLTB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stsignedint), PC_VSPLTB,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stsignedint), PC_VSPLTB,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stsignedint), PC_VSPLTH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stsignedint), PC_VSPLTH,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stsignedint), PC_VSPLTH,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stsignedint), PC_VSPLTH,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), PC_VSPLTW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stsignedint), PC_VSPLTW,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stsignedint), PC_VSPLTW,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stsignedint), PC_VSPLTW,
    NULL, NULL, NULL, 0
};
static TypeTable21 vector_splat_s8_type_table[] = {
    TYPE(&stvectorsignedchar), TYPE(&stsignedint), PC_VSPLTISB,
    NULL, NULL, 0
};
static TypeTable21 vector_splat_s16_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stsignedint), PC_VSPLTISH,
    NULL, NULL, 0
};
static TypeTable21 vector_splat_s32_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stsignedint), PC_VSPLTISW,
    NULL, NULL, 0
};
static TypeTable21 vector_splat_u8_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), PC_VSPLTISB,
    NULL, NULL, 0
};
static TypeTable21 vector_splat_u16_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stsignedint), PC_VSPLTISH,
    NULL, NULL, 0
};
static TypeTable21 vector_splat_u32_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stsignedint), PC_VSPLTISW,
    NULL, NULL, 0
};
static TypeTable31 vector_sr_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSRB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), PC_VSRB,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VSRH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedshort), PC_VSRH,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VSRW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedlong), PC_VSRW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_sra_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSRAB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), PC_VSRAB,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VSRAH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedshort), PC_VSRAH,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VSRAW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedlong), PC_VSRAW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_srl_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSR,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedshort), PC_VSR,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedlong), PC_VSR,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), PC_VSR,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedshort), PC_VSR,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedlong), PC_VSR,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VSR,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedshort), PC_VSR,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedlong), PC_VSR,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedchar), PC_VSR,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VSR,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedlong), PC_VSR,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedchar), PC_VSR,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedshort), PC_VSR,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedlong), PC_VSR,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedchar), PC_VSR,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VSR,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedlong), PC_VSR,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorunsignedchar), PC_VSR,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorunsignedshort), PC_VSR,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorunsignedlong), PC_VSR,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedchar), PC_VSR,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedshort), PC_VSR,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VSR,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedchar), PC_VSR,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedshort), PC_VSR,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedlong), PC_VSR,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedchar), PC_VSR,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedshort), PC_VSR,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VSR,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_sro_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSRO,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorsignedchar), PC_VSRO,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), PC_VSRO,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VSRO,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedchar), PC_VSRO,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorsignedchar), PC_VSRO,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedchar), PC_VSRO,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedchar), PC_VSRO,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorunsignedchar), PC_VSRO,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorsignedchar), PC_VSRO,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedchar), PC_VSRO,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorsignedchar), PC_VSRO,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedchar), PC_VSRO,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedchar), PC_VSRO,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorunsignedchar), PC_VSRO,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorsignedchar), PC_VSRO,
    NULL, NULL, NULL, 0
};
static TypeTable41 vector_st_type_table[] = {
    TYPE(&stvoid), TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stvectorunsignedchar_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedchar_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorsignedchar), TYPE(&stsignedint), TYPE(&stvectorsignedchar_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorsignedchar), TYPE(&stsignedint), TYPE(&stsignedchar_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorunsignedshort), TYPE(&stsignedint), TYPE(&stvectorunsignedshort_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorunsignedshort), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorsignedshort), TYPE(&stsignedint), TYPE(&stvectorsignedshort_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorsignedshort), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stvectorunsignedlong_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedint_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedlong_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stvectorsignedlong_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedint_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedlong_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorfloat), TYPE(&stsignedint), TYPE(&stvectorfloat_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorfloat), TYPE(&stsignedint), TYPE(&stfloat_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorpixel), TYPE(&stsignedint), TYPE(&stvectorpixel_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorpixel), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorpixel), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorboolchar), TYPE(&stsignedint), TYPE(&stvectorboolchar_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorboolchar), TYPE(&stsignedint), TYPE(&stunsignedchar_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorboolchar), TYPE(&stsignedint), TYPE(&stsignedchar_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorboolshort), TYPE(&stsignedint), TYPE(&stvectorboolshort_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorboolshort), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorboolshort), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stvectorboollong_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stunsignedint_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stunsignedlong_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stsignedint_ptr), PC_STVX,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stsignedlong_ptr), PC_STVX,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable41 vector_ste_type_table[] = {
    TYPE(&stvoid), TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedchar_ptr), PC_STVEBX,
    TYPE(&stvoid), TYPE(&stvectorsignedchar), TYPE(&stsignedint), TYPE(&stsignedchar_ptr), PC_STVEBX,
    TYPE(&stvoid), TYPE(&stvectorunsignedshort), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_STVEHX,
    TYPE(&stvoid), TYPE(&stvectorsignedshort), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_STVEHX,
    TYPE(&stvoid), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedint_ptr), PC_STVEWX,
    TYPE(&stvoid), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedlong_ptr), PC_STVEWX,
    TYPE(&stvoid), TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedint_ptr), PC_STVEWX,
    TYPE(&stvoid), TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedlong_ptr), PC_STVEWX,
    TYPE(&stvoid), TYPE(&stvectorfloat), TYPE(&stsignedint), TYPE(&stfloat_ptr), PC_STVEWX,
    TYPE(&stvoid), TYPE(&stvectorpixel), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_STVEHX,
    TYPE(&stvoid), TYPE(&stvectorpixel), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_STVEHX,
    TYPE(&stvoid), TYPE(&stvectorboolchar), TYPE(&stsignedint), TYPE(&stsignedchar_ptr), PC_STVEBX,
    TYPE(&stvoid), TYPE(&stvectorboolchar), TYPE(&stsignedint), TYPE(&stunsignedchar_ptr), PC_STVEBX,
    TYPE(&stvoid), TYPE(&stvectorboolshort), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_STVEHX,
    TYPE(&stvoid), TYPE(&stvectorboolshort), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_STVEHX,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stunsignedint_ptr), PC_STVEWX,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stunsignedlong_ptr), PC_STVEWX,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stsignedint_ptr), PC_STVEWX,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stsignedlong_ptr), PC_STVEWX,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable41 vector_stl_type_table[] = {
    TYPE(&stvoid), TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stvectorunsignedchar_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedchar_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorsignedchar), TYPE(&stsignedint), TYPE(&stvectorsignedchar_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorsignedchar), TYPE(&stsignedint), TYPE(&stsignedchar_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorunsignedshort), TYPE(&stsignedint), TYPE(&stvectorunsignedshort_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorunsignedshort), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorsignedshort), TYPE(&stsignedint), TYPE(&stvectorsignedshort_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorsignedshort), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stvectorunsignedlong_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedint_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedlong_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stvectorsignedlong_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedint_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedlong_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorfloat), TYPE(&stsignedint), TYPE(&stvectorfloat_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorfloat), TYPE(&stsignedint), TYPE(&stfloat_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorpixel), TYPE(&stsignedint), TYPE(&stvectorpixel_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorpixel), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorpixel), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorboolchar), TYPE(&stsignedint), TYPE(&stvectorboolchar_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorboolchar), TYPE(&stsignedint), TYPE(&stunsignedchar_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorboolchar), TYPE(&stsignedint), TYPE(&stsignedchar_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorboolshort), TYPE(&stsignedint), TYPE(&stvectorboolshort_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorboolshort), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorboolshort), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stvectorboollong_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stunsignedlong_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stunsignedint_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stsignedlong_ptr), PC_STVXL,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stsignedint_ptr), PC_STVXL,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable31 vector_sub_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSUBUBM,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VSUBUBM,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VSUBUBM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VSUBUBM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VSUBUBM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VSUBUBM,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VSUBUHM,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VSUBUHM,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VSUBUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VSUBUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VSUBUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VSUBUHM,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VSUBUWM,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VSUBUWM,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VSUBUWM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VSUBUWM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VSUBUWM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VSUBUWM,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VSUBFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_subc_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VSUBCUW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_subs_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSUBUBS,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VSUBUBS,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VSUBUBS,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VSUBSBS,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VSUBSBS,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VSUBSBS,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VSUBUHS,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VSUBUHS,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VSUBUHS,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VSUBSHS,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VSUBSHS,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VSUBSHS,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VSUBUWS,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VSUBUWS,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VSUBUWS,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VSUBSWS,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VSUBSWS,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VSUBSWS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_sum4s_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedlong), PC_VSUM4UBS,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedlong), PC_VSUM4SBS,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedlong), PC_VSUM4SHS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_sum2s_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VSUM2SWS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_sums_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VSUMSWS,
    NULL, NULL, NULL, 0
};
static TypeTable21 vector_trunc_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VRFIZ,
    NULL, NULL, 0
};
static TypeTable31 vector_unpack2sh_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMRGHB,
    TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMRGHH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_unpack2sl_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMRGLB,
    TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMRGLH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_unpack2uh_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMRGHB,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMRGHH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_unpack2ul_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMRGLB,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMRGLH,
    NULL, NULL, NULL, 0
};
static TypeTable21 vector_unpackh_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedchar), PC_VUPKHSB,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolchar), PC_VUPKHSB,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorpixel), PC_VUPKHPX,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedshort), PC_VUPKHSH,
    TYPE(&stvectorboollong), TYPE(&stvectorboolshort), PC_VUPKHSH,
    NULL, NULL, 0
};
static TypeTable21 vector_unpackl_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedchar), PC_VUPKLSB,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolchar), PC_VUPKLSB,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorpixel), PC_VUPKLPX,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedshort), PC_VUPKLSH,
    TYPE(&stvectorboollong), TYPE(&stvectorboolshort), PC_VUPKLSH,
    NULL, NULL, 0
};
static TypeTable31 vector_xor_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VXOR,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VXOR,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VXOR,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VXOR,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VXOR,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VXOR,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), PC_VXOR,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VXOR,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VXOR,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VXOR,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VXOR,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VXOR,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VXOR,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VXOR,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VXOR,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VXOR,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VXOR,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VXOR,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VXOR,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VXOR,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VXOR,
    TYPE(&stvectorfloat), TYPE(&stvectorboollong), TYPE(&stvectorfloat), PC_VXOR,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorboollong), PC_VXOR,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VXOR,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_all_eq_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorpixel), TYPE(&stvectorpixel), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPEQFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_all_ge_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGEFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_all_gt_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGTFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_all_in_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPBFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_all_le_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGEFP,
    NULL, NULL, NULL, 0
};
// clang-format on
// clang-format off
static TypeTable31 vector_all_lt_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGTFP
};
static TypeTable21 vector_all_nan_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorfloat), PC_VCMPEQFP,
    NULL, NULL, 0
};
static TypeTable31 vector_all_ne_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorpixel), TYPE(&stvectorpixel), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPEQFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_all_nge_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGEFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_all_ngt_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGTFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_all_nle_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGEFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_all_nlt_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGTFP,
    NULL, NULL, NULL, 0
};
static TypeTable21 vector_all_numeric_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorfloat), PC_VCMPEQFP,
    NULL, NULL, 0
};
static TypeTable31 vector_any_eq_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorpixel), TYPE(&stvectorpixel), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPEQFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_any_ge_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGEFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_any_gt_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGTFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_any_le_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGEFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_any_lt_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGTFP,
    NULL, NULL, NULL, 0
};
static TypeTable21 vector_any_nan_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorfloat), PC_VCMPEQFP,
    NULL, NULL, 0
};
static TypeTable31 vector_any_ne_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), PC_VCMPEQUB,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorpixel), TYPE(&stvectorpixel), PC_VCMPEQUH,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VCMPEQUW,
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPEQFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_any_nge_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGEFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_any_ngt_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGTFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_any_nle_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGEFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_any_nlt_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGTFP,
    NULL, NULL, NULL, 0
};
static TypeTable21 vector_any_numeric_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorfloat), PC_VCMPEQFP,
    NULL, NULL, 0
};
static TypeTable31 vector_any_out_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPBFP,
    NULL, NULL, NULL, 0
};
// clang-format on
// clang-format off
static TypeTable21 vector_step_type_table[] = {
    TYPE(&stsignedint), TYPE(&stvectorunsignedchar), PC_BDZF,
    TYPE(&stsignedint), TYPE(&stvectorsignedchar), PC_BDZF,
    TYPE(&stsignedint), TYPE(&stvectorboolchar), PC_BDZF,
    TYPE(&stsignedint), TYPE(&stvectorunsignedshort), PC_BF,
    TYPE(&stsignedint), TYPE(&stvectorsignedshort), PC_BF,
    TYPE(&stsignedint), TYPE(&stvectorboolshort), PC_BF,
    TYPE(&stsignedint), TYPE(&stvectorunsignedlong), PC_BCCTR,
    TYPE(&stsignedint), TYPE(&stvectorsignedlong), PC_BCCTR,
    TYPE(&stsignedint), TYPE(&stvectorboollong), PC_BCCTR,
    TYPE(&stsignedint), TYPE(&stvectorfloat), PC_BCCTR,
    TYPE(&stsignedint), TYPE(&stvectorpixel), PC_BF,
    NULL, NULL, 0
};
static TypeTable31 vector_vaddubm_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VADDUBM,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VADDUBM,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VADDUBM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VADDUBM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VADDUBM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VADDUBM,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vadduhm_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VADDUHM,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VADDUHM,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VADDUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VADDUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VADDUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VADDUHM,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vadduwm_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VADDUWM,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VADDUWM,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VADDUWM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VADDUWM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VADDUWM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VADDUWM,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vaddfp_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VADDFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vaddubs_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VADDUBS,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VADDUBS,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VADDUBS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vaddsbs_type_table[] = {
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VADDSBS,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VADDSBS,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VADDSBS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vadduhs_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VADDUHS,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VADDUHS,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VADDUHS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vaddshs_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VADDSHS,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VADDSHS,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VADDSHS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vadduws_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VADDUWS,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VADDUWS,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VADDUWS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vaddsws_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VADDSWS,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VADDSWS,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VADDSWS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vavgub_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VAVGUB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vavgsb_type_table[] = {
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VAVGSB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vavguh_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VAVGUH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vavgsh_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VAVGSH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vavguw_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VAVGUW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vavgsw_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VAVGSW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vcmpequb_type_table[] = {
    TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPEQUB,
    TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPEQUB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vcmpequh_type_table[] = {
    TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPEQUH,
    TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPEQUH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vcmpequw_type_table[] = {
    TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPEQUW,
    TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPEQUW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vcmpeqfp_type_table[] = {
    TYPE(&stvectorboollong), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPEQFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vcmpgtub_type_table[] = {
    TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VCMPGTUB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vcmpgtsb_type_table[] = {
    TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VCMPGTSB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vcmpgtuh_type_table[] = {
    TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VCMPGTUH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vcmpgtsh_type_table[] = {
    TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VCMPGTSH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vcmpgtuw_type_table[] = {
    TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VCMPGTUW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vcmpgtsw_type_table[] = {
    TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VCMPGTSW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vcmpgtfp_type_table[] = {
    TYPE(&stvectorboollong), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VCMPGTFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vcfux_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), PC_VCFUX,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vcfsx_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorsignedlong), TYPE(&stsignedint), PC_VCFSX,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_lvebx_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedchar_ptr), PC_LVEBX,
    TYPE(&stvectorsignedchar), TYPE(&stsignedint), TYPE(&stsignedchar_ptr), PC_LVEBX,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_lvehx_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_LVEHX,
    TYPE(&stvectorsignedshort), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_LVEHX,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_lvewx_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedint_ptr), PC_LVEWX,
    TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedlong_ptr), PC_LVEWX,
    TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedint_ptr), PC_LVEWX,
    TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedlong_ptr), PC_LVEWX,
    TYPE(&stvectorfloat), TYPE(&stsignedint), TYPE(&stfloat_ptr), PC_LVEWX,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmaxub_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMAXUB,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VMAXUB,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VMAXUB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmaxsb_type_table[] = {
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VMAXSB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VMAXSB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VMAXSB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmaxuh_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMAXUH,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VMAXUH,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VMAXUH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmaxsh_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMAXSH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VMAXSH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VMAXSH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmaxuw_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VMAXUW,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VMAXUW,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VMAXUW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmaxsw_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VMAXSW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VMAXSW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VMAXSW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmaxfp_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VMAXFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmrghb_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMRGHB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VMRGHB,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), PC_VMRGHB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmrghh_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMRGHH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMRGHH,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VMRGHH,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorpixel), PC_VMRGHH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmrghw_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VMRGHW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VMRGHW,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VMRGHW,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VMRGHW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmrglb_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMRGLB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VMRGLB,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), PC_VMRGLB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmrglh_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMRGLH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMRGLH,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VMRGLH,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stvectorpixel), PC_VMRGLH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmrglw_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VMRGLW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VMRGLW,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VMRGLW,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VMRGLW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vminub_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMINUB,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VMINUB,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VMINUB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vminsb_type_table[] = {
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VMINSB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VMINSB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VMINSB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vminuh_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMINUH,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VMINUH,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VMINUH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vminsh_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMINSH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VMINSH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VMINSH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vminuw_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VMINUW,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VMINUW,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VMINUW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vminsw_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VMINSW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VMINSW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VMINSW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vminfp_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VMINFP,
    NULL, NULL, NULL, 0
};
static TypeTable41 vector_vmsumubm_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedlong), PC_VMSUMUBM,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable41 vector_vmsumuhm_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedlong), PC_VMSUMUHM,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable41 vector_vmsummbm_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorsignedlong), PC_VMSUMMBM,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable41 vector_vmsumshm_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedlong), PC_VMSUMSHM,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable41 vector_vmsumuhs_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedlong), PC_VMSUMUHS,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable41 vector_vmsumshs_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedlong), PC_VMSUMSHS,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmuleub_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMULEUB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmulesb_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VMULESB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmuleuh_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMULEUH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmulesh_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMULESH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmuloub_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VMULOUB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmulosb_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VMULOSB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmulouh_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VMULOUH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vmulosh_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VMULOSH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vpkuhum_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VPKUHUM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VPKUHUM,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), PC_VPKUHUM,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vpkuwum_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VPKUWUM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VPKUWUM,
    TYPE(&stvectorboolshort), TYPE(&stvectorboollong), TYPE(&stvectorboollong), PC_VPKUWUM,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vpkuhus_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VPKUHUS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vpkshss_type_table[] = {
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VPKSHSS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vpkuwus_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VPKUWUS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vpkswss_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VPKSWSS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vpkshus_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VPKSHUS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vpkswus_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VPKSWUS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vrlb_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VRLB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), PC_VRLB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vrlh_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VRLH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedshort), PC_VRLH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vrlw_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VRLW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedlong), PC_VRLW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vslb_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSLB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), PC_VSLB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vslh_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VSLH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedshort), PC_VSLH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vslw_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VSLW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedlong), PC_VSLW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vspltb_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stsignedint), PC_VSPLTB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stsignedint), PC_VSPLTB,
    TYPE(&stvectorboolchar), TYPE(&stvectorboolchar), TYPE(&stsignedint), PC_VSPLTB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsplth_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stsignedint), PC_VSPLTH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stsignedint), PC_VSPLTH,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolshort), TYPE(&stsignedint), PC_VSPLTH,
    TYPE(&stvectorpixel), TYPE(&stvectorpixel), TYPE(&stsignedint), PC_VSPLTH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vspltw_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), PC_VSPLTW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stsignedint), PC_VSPLTW,
    TYPE(&stvectorboollong), TYPE(&stvectorboollong), TYPE(&stsignedint), PC_VSPLTW,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stsignedint), PC_VSPLTW,
    NULL, NULL, NULL, 0
};
static TypeTable21 vector_vspltisb_type_table[] = {
    TYPE(&stvectorsignedchar), TYPE(&stsignedint), PC_VSPLTISB,
    NULL, NULL, 0
};
static TypeTable21 vector_vspltish_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stsignedint), PC_VSPLTISH,
    NULL, NULL, 0
};
static TypeTable21 vector_vspltisw_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stsignedint), PC_VSPLTISW,
    NULL, NULL, 0
};
static TypeTable31 vector_vsrb_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSRB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), PC_VSRB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsrh_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VSRH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedshort), PC_VSRH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsrw_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VSRW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedlong), PC_VSRW,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsrab_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSRAB,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorunsignedchar), PC_VSRAB,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsrah_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VSRAH,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorunsignedshort), PC_VSRAH,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsraw_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VSRAW,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorunsignedlong), PC_VSRAW,
    NULL, NULL, NULL, 0
};
static TypeTable41 vector_stvebx_type_table[] = {
    TYPE(&stvoid), TYPE(&stvectorunsignedchar), TYPE(&stsignedint), TYPE(&stunsignedchar_ptr), PC_STVEBX,
    TYPE(&stvoid), TYPE(&stvectorsignedchar), TYPE(&stsignedint), TYPE(&stsignedchar_ptr), PC_STVEBX,
    TYPE(&stvoid), TYPE(&stvectorboolchar), TYPE(&stsignedint), TYPE(&stsignedchar_ptr), PC_STVEBX,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable41 vector_stvehx_type_table[] = {
    TYPE(&stvoid), TYPE(&stvectorunsignedshort), TYPE(&stsignedint), TYPE(&stunsignedshort_ptr), PC_STVEHX,
    TYPE(&stvoid), TYPE(&stvectorsignedshort), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_STVEHX,
    TYPE(&stvoid), TYPE(&stvectorboolshort), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_STVEHX,
    TYPE(&stvoid), TYPE(&stvectorpixel), TYPE(&stsignedint), TYPE(&stsignedshort_ptr), PC_STVEHX,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable41 vector_stvewx_type_table[] = {
    TYPE(&stvoid), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedint_ptr), PC_STVEWX,
    TYPE(&stvoid), TYPE(&stvectorunsignedlong), TYPE(&stsignedint), TYPE(&stunsignedlong_ptr), PC_STVEWX,
    TYPE(&stvoid), TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedint_ptr), PC_STVEWX,
    TYPE(&stvoid), TYPE(&stvectorsignedlong), TYPE(&stsignedint), TYPE(&stsignedlong_ptr), PC_STVEWX,
    TYPE(&stvoid), TYPE(&stvectorfloat), TYPE(&stsignedint), TYPE(&stfloat_ptr), PC_STVEWX,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stsignedint_ptr), PC_STVEWX,
    TYPE(&stvoid), TYPE(&stvectorboollong), TYPE(&stsignedint), TYPE(&stsignedlong_ptr), PC_STVEWX,
    NULL, NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsububm_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSUBUBM,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VSUBUBM,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VSUBUBM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VSUBUBM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VSUBUBM,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VSUBUBM,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsubuhm_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VSUBUHM,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VSUBUHM,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VSUBUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VSUBUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VSUBUHM,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VSUBUHM,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsubuwm_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VSUBUWM,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VSUBUWM,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VSUBUWM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VSUBUWM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VSUBUWM,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VSUBUWM,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VSUBFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsubfp_type_table[] = {
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VSUBFP,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsububs_type_table[] = {
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), PC_VSUBUBS,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), PC_VSUBUBS,
    TYPE(&stvectorunsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorunsignedchar), PC_VSUBUBS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsubsbs_type_table[] = {
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VSUBSBS,
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), PC_VSUBSBS,
    TYPE(&stvectorsignedchar), TYPE(&stvectorboolchar), TYPE(&stvectorsignedchar), PC_VSUBSBS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsubuhs_type_table[] = {
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), PC_VSUBUHS,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), PC_VSUBUHS,
    TYPE(&stvectorunsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorunsignedshort), PC_VSUBUHS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsubshs_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VSUBSHS,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), PC_VSUBSHS,
    TYPE(&stvectorsignedshort), TYPE(&stvectorboolshort), TYPE(&stvectorsignedshort), PC_VSUBSHS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsubuws_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), PC_VSUBUWS,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), PC_VSUBUWS,
    TYPE(&stvectorunsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorunsignedlong), PC_VSUBUWS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsubsws_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VSUBSWS,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), PC_VSUBSWS,
    TYPE(&stvectorsignedlong), TYPE(&stvectorboollong), TYPE(&stvectorsignedlong), PC_VSUBSWS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsum4ubs_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorunsignedchar), TYPE(&stvectorunsignedlong), PC_VSUM4UBS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsum4sbs_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedchar), TYPE(&stvectorsignedlong), PC_VSUM4SBS,
    NULL, NULL, NULL, 0
};
static TypeTable31 vector_vsum4shs_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedshort), TYPE(&stvectorsignedlong), PC_VSUM4SHS,
    NULL, NULL, NULL, 0
};
static TypeTable21 vector_vupkhsb_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedchar), PC_VUPKHSB,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolchar), PC_VUPKHSB,
    NULL, NULL, 0
};
static TypeTable21 vector_vupklsb_type_table[] = {
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedchar), PC_VUPKLSB,
    TYPE(&stvectorboolshort), TYPE(&stvectorboolchar), PC_VUPKLSB,
    NULL, NULL, 0
};
static TypeTable21 vector_vupkhpx_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorpixel), PC_VUPKHPX,
    NULL, NULL, 0
};
static TypeTable21 vector_vupklpx_type_table[] = {
    TYPE(&stvectorunsignedlong), TYPE(&stvectorpixel), PC_VUPKLPX,
    NULL, NULL, 0
};
static TypeTable21 vector_vupkhsh_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedshort), PC_VUPKHSH,
    TYPE(&stvectorboollong), TYPE(&stvectorboolshort), PC_VUPKHSH,
    NULL, NULL, 0
};
static TypeTable21 vector_vupklsh_type_table[] = {
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedshort), PC_VUPKLSH,
    TYPE(&stvectorboollong), TYPE(&stvectorboolshort), PC_VUPKLSH,
    NULL, NULL, 0
};
static TypeTable22 vector_abs_type_table[] = {
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VSUBUBM, PC_VMAXSB,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VSUBUHM, PC_VMAXSH,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VSUBUWM, PC_VMAXSW,
    TYPE(&stvectorfloat), TYPE(&stvectorfloat), PC_VANDC, PC_B,
    NULL, NULL, 0, 0
};
static TypeTable22 vector_abss_type_table[] = {
    TYPE(&stvectorsignedchar), TYPE(&stvectorsignedchar), PC_VSUBSBS, PC_VMAXSB,
    TYPE(&stvectorsignedshort), TYPE(&stvectorsignedshort), PC_VSUBSHS, PC_VMAXSH,
    TYPE(&stvectorsignedlong), TYPE(&stvectorsignedlong), PC_VSUBSWS, PC_VMAXSW,
    NULL, NULL, 0, 0
};
static void* typeTable[] = {
    vector_add_type_table,
    vector_addc_type_table,
    vector_adds_type_table,
    vector_and_type_table,
    vector_andc_type_table,
    vector_avg_type_table,
    vector_ceil_type_table,
    vector_cmpb_type_table,
    vector_cmpeq_type_table,
    vector_cmpge_type_table,
    vector_cmpge_type_table,
    vector_cmpgt_type_table,
    vector_cmpgt_type_table,
    vector_ctf_type_table,
    vector_cts_type_table,
    vector_ctu_type_table,
    vector_dss_type_table,
    vector_dssall_type_table,
    vector_datastream_type_table,
    vector_datastream_type_table,
    vector_datastream_type_table,
    vector_datastream_type_table,
    vector_expte_type_table,
    vector_floor_type_table,
    vector_load_type_table,
    vector_loade_type_table,
    vector_loadl_type_table,
    vector_loge_type_table,
    vector_lvsl_type_table,
    vector_lvsr_type_table,
    vector_madd_type_table,
    vector_madds_type_table,
    vector_max_type_table,
    vector_mergeh_type_table,
    vector_mergel_type_table,
    vector_mfvscr_type_table,
    vector_min_type_table,
    vector_mladd_type_table,
    vector_mradds_type_table,
    vector_msum_type_table,
    vector_msums_type_table,
    vector_mtvscr_type_table,
    vector_mule_type_table,
    vector_mulo_type_table,
    vector_nmsub_type_table,
    vector_nor_type_table,
    vector_or_type_table,
    vector_pack_type_table,
    vector_packpx_type_table,
    vector_packs_type_table,
    vector_packsu_type_table,
    vector_perm_type_table,
    vector_re_type_table,
    vector_rl_type_table,
    vector_round_type_table,
    vector_rsqrte_type_table,
    vector_sel_type_table,
    vector_sl_type_table,
    vector_sld_type_table,
    vector_sll_type_table,
    vector_slo_type_table,
    vector_splat_type_table,
    vector_splat_s8_type_table,
    vector_splat_s16_type_table,
    vector_splat_s32_type_table,
    vector_splat_u8_type_table,
    vector_splat_u16_type_table,
    vector_splat_u32_type_table,
    vector_sr_type_table,
    vector_sra_type_table,
    vector_srl_type_table,
    vector_sro_type_table,
    vector_st_type_table,
    vector_ste_type_table,
    vector_stl_type_table,
    vector_sub_type_table,
    vector_subc_type_table,
    vector_subs_type_table,
    vector_sum4s_type_table,
    vector_sum2s_type_table,
    vector_sums_type_table,
    vector_trunc_type_table,
    vector_unpack2sh_type_table,
    vector_unpack2sl_type_table,
    vector_unpack2uh_type_table,
    vector_unpack2ul_type_table,
    vector_unpackh_type_table,
    vector_unpackl_type_table,
    vector_xor_type_table,
    vector_all_eq_type_table,
    vector_all_ge_type_table,
    vector_all_gt_type_table,
    vector_all_in_type_table,
    vector_all_le_type_table,
    vector_all_lt_type_table,
    vector_all_nan_type_table,
    vector_all_ne_type_table,
    vector_all_nge_type_table,
    vector_all_ngt_type_table,
    vector_all_nle_type_table,
    vector_all_nlt_type_table,
    vector_all_numeric_type_table,
    vector_any_eq_type_table,
    vector_any_ge_type_table,
    vector_any_gt_type_table,
    vector_any_le_type_table,
    vector_any_lt_type_table,
    vector_any_nan_type_table,
    vector_any_ne_type_table,
    vector_any_nge_type_table,
    vector_any_ngt_type_table,
    vector_any_nle_type_table,
    vector_any_nlt_type_table,
    vector_any_numeric_type_table,
    vector_any_out_type_table,
    vector_vaddubm_type_table,
    vector_vadduhm_type_table,
    vector_vadduwm_type_table,
    vector_vaddfp_type_table,
    vector_addc_type_table,
    vector_vaddubs_type_table,
    vector_vaddsbs_type_table,
    vector_vadduhs_type_table,
    vector_vaddshs_type_table,
    vector_vadduws_type_table,
    vector_vaddsws_type_table,
    vector_and_type_table,
    vector_andc_type_table,
    vector_vavgub_type_table,
    vector_vavgsb_type_table,
    vector_vavguh_type_table,
    vector_vavgsh_type_table,
    vector_vavguw_type_table,
    vector_vavgsw_type_table,
    vector_ceil_type_table,
    vector_cmpb_type_table,
    vector_vcmpequb_type_table,
    vector_vcmpequh_type_table,
    vector_vcmpequw_type_table,
    vector_vcmpeqfp_type_table,
    vector_cmpge_type_table,
    vector_vcmpgtub_type_table,
    vector_vcmpgtsb_type_table,
    vector_vcmpgtuh_type_table,
    vector_vcmpgtsh_type_table,
    vector_vcmpgtuw_type_table,
    vector_vcmpgtsw_type_table,
    vector_vcmpgtfp_type_table,
    vector_vcfux_type_table,
    vector_vcfsx_type_table,
    vector_cts_type_table,
    vector_ctu_type_table,
    vector_expte_type_table,
    vector_floor_type_table,
    vector_load_type_table,
    vector_lvebx_type_table,
    vector_lvehx_type_table,
    vector_lvewx_type_table,
    vector_loadl_type_table,
    vector_loge_type_table,
    vector_madd_type_table,
    vector_madds_type_table,
    vector_vmaxub_type_table,
    vector_vmaxsb_type_table,
    vector_vmaxuh_type_table,
    vector_vmaxsh_type_table,
    vector_vmaxuw_type_table,
    vector_vmaxsw_type_table,
    vector_vmaxfp_type_table,
    vector_vmrghb_type_table,
    vector_vmrghh_type_table,
    vector_vmrghw_type_table,
    vector_vmrglb_type_table,
    vector_vmrglh_type_table,
    vector_vmrglw_type_table,
    vector_vminub_type_table,
    vector_vminsb_type_table,
    vector_vminuh_type_table,
    vector_vminsh_type_table,
    vector_vminuw_type_table,
    vector_vminsw_type_table,
    vector_vminfp_type_table,
    vector_mladd_type_table,
    vector_mradds_type_table,
    vector_vmsumubm_type_table,
    vector_vmsumuhm_type_table,
    vector_vmsummbm_type_table,
    vector_vmsumshm_type_table,
    vector_vmsumuhs_type_table,
    vector_vmsumshs_type_table,
    vector_vmuleub_type_table,
    vector_vmulesb_type_table,
    vector_vmuleuh_type_table,
    vector_vmulesh_type_table,
    vector_vmuloub_type_table,
    vector_vmulosb_type_table,
    vector_vmulouh_type_table,
    vector_vmulosh_type_table,
    vector_nmsub_type_table,
    vector_nor_type_table,
    vector_or_type_table,
    vector_vpkuhum_type_table,
    vector_vpkuwum_type_table,
    vector_packpx_type_table,
    vector_vpkuhus_type_table,
    vector_vpkshss_type_table,
    vector_vpkuwus_type_table,
    vector_vpkswss_type_table,
    vector_vpkshus_type_table,
    vector_vpkswus_type_table,
    vector_perm_type_table,
    vector_re_type_table,
    vector_vrlb_type_table,
    vector_vrlh_type_table,
    vector_vrlw_type_table,
    vector_round_type_table,
    vector_rsqrte_type_table,
    vector_sel_type_table,
    vector_vslb_type_table,
    vector_vslh_type_table,
    vector_vslw_type_table,
    vector_sld_type_table,
    vector_sll_type_table,
    vector_slo_type_table,
    vector_vspltb_type_table,
    vector_vsplth_type_table,
    vector_vspltw_type_table,
    vector_vspltisb_type_table,
    vector_vspltish_type_table,
    vector_vspltisw_type_table,
    vector_vsrb_type_table,
    vector_vsrh_type_table,
    vector_vsrw_type_table,
    vector_vsrab_type_table,
    vector_vsrah_type_table,
    vector_vsraw_type_table,
    vector_srl_type_table,
    vector_sro_type_table,
    vector_st_type_table,
    vector_stvebx_type_table,
    vector_stvehx_type_table,
    vector_stvewx_type_table,
    vector_stl_type_table,
    vector_vsububm_type_table,
    vector_vsubuhm_type_table,
    vector_vsubuwm_type_table,
    vector_vsubfp_type_table,
    vector_subc_type_table,
    vector_vsububs_type_table,
    vector_vsubsbs_type_table,
    vector_vsubuhs_type_table,
    vector_vsubshs_type_table,
    vector_vsubuws_type_table,
    vector_vsubsws_type_table,
    vector_vsum4ubs_type_table,
    vector_vsum4sbs_type_table,
    vector_vsum4shs_type_table,
    vector_sum2s_type_table,
    vector_sums_type_table,
    vector_trunc_type_table,
    vector_vupkhsb_type_table,
    vector_vupklsb_type_table,
    vector_vupkhpx_type_table,
    vector_vupklpx_type_table,
    vector_vupkhsh_type_table,
    vector_vupklsh_type_table,
    vector_xor_type_table,
    vector_abs_type_table,
    vector_abss_type_table,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
// clang-format on

int is_intrinsic_function_call(ENode* funccall) {
    ENode* funcref = funccall->data.funccall.funcref;
    return ENODE_IS(funcref, EOBJREF) &&
        funcref->data.objref->datatype == DFUNC &&
        (TYPE_FUNC(funcref->data.objref->type)->flags & FUNC_INTRINSIC);
}

static void abs_intrinsic(ENode* expr, short outputReg, Operand* output) {
    int signReg;
    int resultReg;
    Operand op;

    memclrw(&op, sizeof(op));
    GEN_NODE_TO_GPR(expr, &op, expr->rtype, 0);

    signReg = ALLOC_GPR();
    emitpcode(PC_SRAWI, signReg, op.reg, 31);

    resultReg = outputReg ? outputReg : ALLOC_GPR();
    emitpcode(PC_XOR, resultReg, signReg, op.reg);
    emitpcode(PC_SUBF, resultReg, signReg, resultReg);

    output->optype = OpndType_GPR;
    output->reg = resultReg;
}

static void setflm_intrinsic(ENode* expr, short outputReg, Operand* output, int resultIgnored) {
    int reg;
    Operand op;

    memclrw(&op, sizeof(op));
    GEN_NODE_TO_FPR(expr, &op, expr->rtype, 0);

    if (!resultIgnored) {
        output->optype = OpndType_FPR;
        reg = (outputReg && outputReg != op.reg) ? outputReg : ALLOC_FPR();
        emitpcode(PC_MFFS, output->reg = reg);
    }

    emitpcode(PC_MTFSF, 255, op.reg);
}

static void alloca_intrinsic(ENode* expr, short outputReg, Operand* output) {
    SInt32 reg;
    Operand op;

    memclrw(&op, sizeof(op));

    if (ENODE_IS(expr, EINTCONST)) {
        reg = outputReg ? outputReg : ALLOC_GPR();
        allocate_dynamic_stack_space(TRUE, reg, ALLOC_GPR(), CInt64_GetULong(&expr->data.intval));
    } else {
        GEN_NODE_TO_GPR(expr, &op, expr->rtype, 0);
        reg = outputReg ? outputReg : ALLOC_GPR();

        emitpcode(PC_NEG, reg, op.reg);
        emitpcode(PC_RLWINM, reg, reg, 0, 0, get_alloca_alignment());
        allocate_dynamic_stack_space(FALSE, reg, ALLOC_GPR(), 0);
    }

    output->optype = OpndType_GPR;
    output->reg = reg;
}

static void load_bytereversed_intrinsic(
    short opcode, ENode* expr1, ENode* expr2, short outputReg, Operand* output
) {
    SInt32 reg;
    Operand op1;
    Operand op2;

    reg = outputReg ? outputReg : ALLOC_GPR();
    memclrw(&op1, sizeof(op1));
    memclrw(&op2, sizeof(op2));

    if (ENODE_IS(expr2, EINTCONST) && expr2->data.intval.lo == 0) {
        GEN_NODE(expr1, &op1);
        if (op1.optype == OpndType_GPR_Indexed) {
            emitpcode(opcode, reg, op1.reg, op1.regOffset);
        } else {
            ENSURE_GPR(&op1, expr1->rtype, 0);
            emitpcode(opcode, reg, 0, op1.reg);
        }
    } else {
        GEN_NODE_TO_GPR(expr1, &op1, expr1->rtype, 0);
        GEN_NODE_TO_GPR(expr2, &op2, expr2->rtype, 0);
        emitpcode(opcode, reg, op1.reg, op2.reg);
    }

    setpcodeflags(
        fSideEffects | ((expr1->flags | expr2->flags | output->flags) & (fIsConst | fIsVolatile))
    );
    output->optype = OpndType_GPR;
    output->reg = reg;
}

static void store_bytereversed_intrinsic(short opcode, ENode* expr1, ENode* expr2, ENode* expr3) {
    Operand op1;
    Operand op2;
    Operand op3;

    memclrw(&op1, sizeof(op1));
    memclrw(&op2, sizeof(op2));
    memclrw(&op3, sizeof(op3));

    if (ENODE_IS(expr3, EINTCONST) && expr3->data.intval.lo == 0) {
        GEN_NODE_TO_GPR(expr1, &op1, expr1->rtype, 0);
        GEN_NODE(expr2, &op2);
        if (op2.optype == OpndType_GPR_Indexed) {
            emitpcode(opcode, op1.reg, op2.reg, op2.regOffset);
        } else {
            ENSURE_GPR(&op2, expr2->rtype, 0);
            emitpcode(opcode, op1.reg, 0, op2.reg);
        }
    } else {
        GEN_NODE_TO_GPR(expr1, &op1, expr1->rtype, 0);
        GEN_NODE_TO_GPR(expr2, &op2, expr2->rtype, 0);
        GEN_NODE_TO_GPR(expr3, &op3, expr3->rtype, 0);
        emitpcode(opcode, op1.reg, op2.reg, op3.reg);
    }

    setpcodeflags(fSideEffects | ((op1.flags | op2.flags | op3.flags) & (fIsConst | fIsVolatile)));
}

static void data_cache_block_intrinsic(short opcode, ENode* expr1, ENode* expr2) {
    Operand op1;
    Operand op2;

    memclrw(&op1, sizeof(op1));
    memclrw(&op2, sizeof(op2));

    if (ENODE_IS(expr2, EINTCONST) && expr2->data.intval.lo == 0) {
        GEN_NODE(expr1, &op1);
        if (op1.optype == OpndType_GPR_Indexed) {
            emitpcode(opcode, op1.reg, op1.regOffset);
        } else {
            ENSURE_GPR(&op1, expr1->rtype, 0);
            emitpcode(opcode, 0, op1.reg);
        }
    } else {
        GEN_NODE_TO_GPR(expr1, &op1, expr1->rtype, 0);
        GEN_NODE_TO_GPR(expr2, &op2, expr2->rtype, 0);
        emitpcode(opcode, op1.reg, op2.reg);
    }
}

static void memcpy_intrinsic(
    ENode* destexpr, ENode* srcexpr, SInt32 size, Boolean ignored, Operand* output
) {
    UInt32 qual;
    Operand destOp;
    Operand srcOp;

    qual = 0;

    memclrw(&destOp, sizeof(destOp));
    memclrw(&srcOp, sizeof(srcOp));

    GEN_NODE(srcexpr, &srcOp);
    indirect(&srcOp, srcexpr);

    GEN_NODE(destexpr, output);
    destOp = *output;
    indirect(&destOp, destexpr);

    if (destOp.object) {
        qual = destOp.object->qual;
    }

    move_block(
        &destOp,
        &srcOp,
        size,
        destexpr->rtype ? CMach_AllocationAlignment(destexpr->rtype, qual) : 1
    );
}

static SInt32 checkconstintarg(ENode* expr, char* name, SInt32 min, SInt32 max, int argnum) {
    SInt32 value;

    if (!ENODE_IS(expr, EINTCONST)) {
        PPCError_ErrorTerm(PPCERR_INTRINSIC_ARGUMENT_NOT_CONSTANT, name, 3);
    }

    value = expr->data.intval.lo;

    if (value < min) {
        PPCError_Warning(
            PPCERR_INTRINSIC_ARGUMENT_OUT_OF_RANGE, name, argnum, value, min, max, min
        );
        return min;
    }

    if (value > max) {
        PPCError_Warning(
            PPCERR_INTRINSIC_ARGUMENT_OUT_OF_RANGE, name, argnum, value, min, max, value & max
        );
        value = value & max;
    }

    return value;
}

static void rlwimi_intrinsic(
    ENode* expr1,
    ENode* expr2,
    ENode* expr3,
    ENode* expr4,
    ENode* expr5,
    short outputReg,
    Operand* output
) {
    SInt32 sh;
    SInt32 mb;
    SInt32 me;
    Operand op1;
    Operand op2;
    int reg;
    char* name = "__rlwimi";

    memclrw(&op1, sizeof(op1));
    memclrw(&op2, sizeof(op2));

    sh = checkconstintarg(expr3, name, 0, 31, 3);
    mb = checkconstintarg(expr4, name, 0, 31, 4);
    me = checkconstintarg(expr5, name, 0, 31, 5);

    GEN_NODE_TO_GPR(expr1, &op1, expr1->rtype, 0);

    GEN_NODE(expr2, &op2);
    if (copts.optimizationlevel > 1) {
        reg = ALLOC_GPR();
        emitpcode(PC_MR, reg, op1.reg);
        op1.reg = reg;
    }
    ENSURE_GPR(&op2, expr2->rtype, 0);

    emitpcode(PC_RLWIMI, op1.reg, op2.reg, sh, mb, me);

    output->optype = OpndType_GPR;
    output->reg = op1.reg;
}

static void rlwinm_intrinsic(
    ENode* expr1, ENode* expr2, ENode* expr3, ENode* expr4, short outputReg, Operand* output
) {
    char* name = "__rlwinm";
    short reg;
    short sh;
    short mb;
    short me;
    Operand op1;

    memclrw(&op1, sizeof(op1));
    sh = checkconstintarg(expr2, name, 0, 31, 2);
    mb = checkconstintarg(expr3, name, 0, 31, 3);
    me = checkconstintarg(expr4, name, 0, 31, 4);

    GEN_NODE_TO_GPR(expr1, &op1, expr1->rtype, 0);

    reg = outputReg ? outputReg : ALLOC_GPR();
    emitpcode(PC_RLWINM, reg, op1.reg, sh, mb, me);

    output->optype = OpndType_GPR;
    output->reg = reg;
}

static void rlwnm_intrinsic(
    ENode* expr1, ENode* expr2, ENode* expr3, ENode* expr4, short outputReg, Operand* output
) {
    short reg;
    short mb;
    short me;
    char* name = "__rlwnm";
    Operand op1;
    Operand op2;

    memclrw(&op1, sizeof(op1));
    memclrw(&op2, sizeof(op2));
    mb = checkconstintarg(expr3, name, 0, 31, 3);
    me = checkconstintarg(expr4, name, 0, 31, 4);

    GEN_NODE_TO_GPR(expr1, &op1, expr1->rtype, 0);
    GEN_NODE_TO_GPR(expr2, &op2, expr2->rtype, 0);

    reg = outputReg ? outputReg : ALLOC_GPR();
    emitpcode(PC_RLWNM, reg, op1.reg, op2.reg, mb, me);

    output->optype = OpndType_GPR;
    output->reg = reg;
}

#pragma push
#pragma dont_inline on
static Boolean promotable_types_are_equal(Type* a, Type* b) {
    TypeIntegral* bi;

    if (a == b) {
        return TRUE;
    }

    bi = TYPE_INTEGRAL(b);
    if (a == TYPE(&stsignedint)) {
        if (bi == &stunsignedint ||
            bi == &stsignedchar ||
            bi == &stunsignedchar ||
            bi == &stsignedshort ||
            bi == &stunsignedshort ||
            bi == &stsignedlong ||
            bi == &stunsignedlong ||
            bi == &stbool) {
            return TRUE;
        }
    }

    return FALSE;
}

#pragma pop

static int Intrinsics_VerifyParameterCount(int wantedCount, ENodeList* args, HashNameNode* name) {
    int count;
    ENodeList* scan;

    count = 0;
    scan = args;
    while (scan) {
        scan = scan->next;
        count++;
    }

    if (count != wantedCount) {
        PPCError_Error(PPCERR_ALTIVEC_ARGUMENT_COUNT, name->name, count, wantedCount);
        return FALSE;
    }

    return TRUE;
}

static ENode* Intrinsics_CreateIntrinsicFunc(Object* func, ENodeList* args, Type* rtype) {
    TypeFunc* tfunc;
    ENodeList* scan;
    ENode* expr;

    tfunc = TYPE_FUNC(func->type);
    CError_ASSERT(3769, IS_TYPE_FUNC(tfunc));

    for (scan = args; scan; scan = scan->next) {
        if (IS_TYPE_ARRAY(scan->node->rtype)) {
            scan->node = CExpr_PointerGeneration(scan->node);
        }
    }

    expr = lalloc(sizeof(ENode));
    expr->type = EFUNCCALL;
    expr->cost = 4;
    expr->rtype = rtype;
    expr->ignored = FALSE;
    expr->flags = tfunc->qual & ENODE_FLAG_QUALS;
    expr->data.funccall.funcref = create_objectrefnode(func);
    expr->data.funccall.args = args;
    expr->data.funccall.functype = tfunc;
    return CExpr_AdjustFunctionCall(expr);
}

static Type* Intrinsics_Verify1VectorArg2Ops(Intrinsics id, ENodeList* args, HashNameNode* name) {
    ENode* arg1;
    TypeTable22* table;
    Type* typeB;
    Type* typeA;

    arg1 = args->node;

    for (table = typeTable[id - Intrinsic_vec_add]; table->rtype; table++) {
        typeB = arg1->rtype;
        typeA = table->arg1;
        if (IS_TYPE_POINTER(typeA) && IS_TYPE_POINTER(typeB)) {
            typeA = TPTR_TARGET(typeA);
            typeB = TPTR_TARGET(typeB);
        }
        if (promotable_types_are_equal(typeA, typeB)) {
            break;
        }
    }

    if (!table->rtype) {
        PPCError_Error(PPCERR_ALTIVEC_INVALID_ARGUMENT, name->name, name->name, arg1->rtype, 0);
        return NULL;
    }
    return table->rtype;
}

static Type* Intrinsics_VerifyNoVectorArgs(Intrinsics id, HashNameNode* name) {
    TypeTable11* table = typeTable[id - Intrinsic_vec_add];
    return table->rtype;
}

static Type* Intrinsics_Verify1VectorArg(Intrinsics id, ENodeList* args, HashNameNode* name) {
    ENode* arg1;
    TypeTable21* table;
    Type* typeB;
    Type* typeA;
    Type* rtype;

    arg1 = args->node;

    for (table = typeTable[id - Intrinsic_vec_add]; table->rtype; table++) {
        typeB = arg1->rtype;
        typeA = table->arg1;
        if (IS_TYPE_POINTER(typeA) && IS_TYPE_POINTER(typeB)) {
            typeA = TPTR_TARGET(typeA);
            typeB = TPTR_TARGET(typeB);
        }
        if (promotable_types_are_equal(typeA, typeB)) {
            break;
        }
    }

    switch (id) {
        case Intrinsic_vec_splat_s8:
        case Intrinsic_vec_splat_s16:
        case Intrinsic_vec_splat_s32:
        case Intrinsic_vec_splat_u8:
        case Intrinsic_vec_splat_u16:
        case Intrinsic_vec_splat_u32:
        case Intrinsic_vec_vspltisb:
        case Intrinsic_vec_vspltish:
        case Intrinsic_vec_vspltisw:
            if (ENODE_IS(arg1, EINTCONST)) {
                SInt32 val = arg1->data.intval.lo;
                if (val > 15 || val < -16) {
                    PPCError_Error(PPCERR_ALTIVEC_INVALID_CONSTANT, name->name, name->name, 5);
                    return NULL;
                }
            } else {
                PPCError_Error(PPCERR_ALTIVEC_INVALID_CONSTANT, name->name, name->name, 5);
                return NULL;
            }
            break;
        case Intrinsic_vec_dss:
            if (ENODE_IS(arg1, EINTCONST)) {
                SInt32 val = arg1->data.intval.lo;
                if (val > 3 || val < 0) {
                    PPCError_Error(PPCERR_ALTIVEC_INVALID_CONSTANT, name->name, name->name, 2);
                    return NULL;
                }
            } else {
                PPCError_Error(PPCERR_ALTIVEC_INVALID_CONSTANT, name->name, name->name, 2);
                return NULL;
            }
            break;
        default:
            break;
    }

    rtype = table->rtype;
    if (!rtype) {
        PPCError_Error(PPCERR_ALTIVEC_INVALID_ARGUMENT, name->name, name->name, arg1->rtype, 0);
        return NULL;
    }
    return rtype;
}

static Type* Intrinsics_Verify2VectorArgs(Intrinsics id, ENodeList* args, HashNameNode* name) {
    ENode* arg1;
    ENode* arg2;
    TypeTable31* table;
    Type* typeB1;
    Type* typeB2;
    Type* typeA1;
    Type* typeA2;

    arg1 = args->node;
    arg2 = args->next->node;
    table = typeTable[id - Intrinsic_vec_add];

    switch (id) {
        case Intrinsic_vec_ctf:
        case Intrinsic_vec_cts:
        case Intrinsic_vec_ctu:
        case Intrinsic_vec_splat:
        case Intrinsic_vec_vcfux:
        case Intrinsic_vec_vcfsx:
        case Intrinsic_vec_vctsxs:
        case Intrinsic_vec_vctuxs:
        case Intrinsic_vec_vspltb:
        case Intrinsic_vec_vsplth:
        case Intrinsic_vec_vspltw:
            if (ENODE_IS(arg2, EINTCONST)) {
                if (arg2->data.intval.lo > 31 || arg2->data.intval.hi < 0) {
                    PPCError_Error(PPCERR_ALTIVEC_INVALID_CONSTANT, name->name, name->name, 5);
                    return NULL;
                }
            } else {
                PPCError_Error(PPCERR_ALTIVEC_INVALID_CONSTANT, name->name, name->name, 5);
                return NULL;
            }
            break;
        default:
            break;
    }

    for (; table->rtype; table++) {
        typeB1 = arg1->rtype;
        typeB2 = arg2->rtype;
        typeA1 = table->arg1;
        typeA2 = table->arg2;
        if (IS_TYPE_POINTER(typeA1) && IS_TYPE_POINTER(typeB1)) {
            typeA1 = TPTR_TARGET(typeA1);
            typeB1 = TPTR_TARGET(typeB1);
        }
        if (IS_TYPE_POINTER(typeA2) && IS_TYPE_POINTER(typeB2)) {
            typeA2 = TPTR_TARGET(typeA2);
            typeB2 = TPTR_TARGET(typeB2);
        }
        if (promotable_types_are_equal(typeA1, typeB1) &&
            promotable_types_are_equal(typeA2, typeB2)) {
            break;
        }
    }

    if (!table->rtype) {
        PPCError_Error(
            PPCERR_ALTIVEC_INVALID_ARGUMENTS2,
            name->name,
            name->name,
            arg1->rtype,
            0,
            arg2->rtype,
            0
        );
        return NULL;
    }

    switch (id) {
        case Intrinsic_vec_ld:
        case Intrinsic_vec_lde:
        case Intrinsic_vec_ldl:
            if (arg2->flags & ENODE_FLAG_VOLATILE) {
                PPCError_Warning(PPCERR_ALTIVEC_VOLATILE_ARGUMENT, name->name);
            }
        default:
            break;
    }

    return table->rtype;
}

static Type* Intrinsics_Verify3VectorArgs(Intrinsics id, ENodeList* args, HashNameNode* name) {
    ENode* arg1;
    ENode* arg2;
    ENode* arg3;
    TypeTable41* table;
    Type* typeB1;
    Type* typeB2;
    Type* typeB3;
    Type* typeA1;
    Type* typeA2;
    Type* typeA3;

    arg1 = args->node;
    arg2 = args->next->node;
    arg3 = args->next->next->node;

    for (table = typeTable[id - Intrinsic_vec_add]; table->rtype; table++) {
        typeB1 = arg1->rtype;
        typeB2 = arg2->rtype;
        typeB3 = arg3->rtype;
        typeA1 = table->arg1;
        typeA2 = table->arg2;
        typeA3 = table->arg3;
        if (IS_TYPE_POINTER(typeA1) && IS_TYPE_POINTER(typeB1)) {
            typeA1 = TPTR_TARGET(typeA1);
            typeB1 = TPTR_TARGET(typeB1);
        }
        if (IS_TYPE_POINTER(typeA2) && IS_TYPE_POINTER(typeB2)) {
            typeA2 = TPTR_TARGET(typeA2);
            typeB2 = TPTR_TARGET(typeB2);
        }
        if (IS_TYPE_POINTER(typeA3) && IS_TYPE_POINTER(typeB3)) {
            typeA3 = TPTR_TARGET(typeA3);
            typeB3 = TPTR_TARGET(typeB3);
        }
        if (promotable_types_are_equal(typeA1, typeB1) &&
            promotable_types_are_equal(typeA2, typeB2) &&
            promotable_types_are_equal(typeA3, typeB3)) {
            break;
        }
    }

    switch (id) {
        case Intrinsic_vec_dst:
        case Intrinsic_vec_dstst:
        case Intrinsic_vec_dststt:
        case Intrinsic_vec_dstt:
            if (ENODE_IS(arg3, EINTCONST)) {
                SInt32 val = arg3->data.intval.lo;
                if (val > 3 || val < 0) {
                    PPCError_Error(PPCERR_ALTIVEC_INVALID_CONSTANT, name->name, name->name, 2);
                    return NULL;
                }
            } else {
                PPCError_Error(PPCERR_ALTIVEC_INVALID_CONSTANT, name->name, name->name, 2);
                return NULL;
            }
            break;
        case Intrinsic_vec_sld:
        case Intrinsic_vec_vsldoi:
            if (ENODE_IS(arg3, EINTCONST)) {
                if (arg3->data.intval.lo > 15 || arg3->data.intval.hi < 0) {
                    PPCError_Error(PPCERR_ALTIVEC_INVALID_CONSTANT, name->name, name->name, 4);
                    return NULL;
                }
            } else {
                PPCError_Error(PPCERR_ALTIVEC_INVALID_CONSTANT, name->name, name->name, 4);
                return NULL;
            }
            break;
        default:
            break;
    }

    if (!table->rtype) {
        PPCError_Error(
            PPCERR_ALTIVEC_INVALID_ARGUMENTS3,
            name->name,
            name->name,
            arg1->rtype,
            0,
            arg2->rtype,
            0,
            arg3->rtype,
            0
        );
        return NULL;
    }

    switch (id) {
        case Intrinsic_vec_st:
        case Intrinsic_vec_ste:
        case Intrinsic_vec_stl:
            if (arg3->flags & ENODE_FLAG_VOLATILE) {
                PPCError_Warning(PPCERR_ALTIVEC_VOLATILE_ARGUMENT, name->name);
            }
        default:
            break;
    }

    return table->rtype;
}

static short Intrinsics_FindOpcodeNoArgs(Intrinsics id, ENode* funccall) {
    TypeTable11* table = typeTable[id - Intrinsic_vec_add];
    return table->opcode;
}

static short Intrinsics_FindOpcode1Arg(Intrinsics id, ENode* funccall, ENode* arg1) {
    TypeTable21* table;
    Type* typeB;
    Type* typeA;

    for (table = typeTable[id - Intrinsic_vec_add]; table->rtype; table++) {
        typeB = arg1->rtype;
        typeA = table->arg1;
        if (IS_TYPE_POINTER(typeA) && IS_TYPE_POINTER(typeB)) {
            typeA = TPTR_TARGET(typeA);
            typeB = TPTR_TARGET(typeB);
        }
        if (promotable_types_are_equal(typeA, typeB)) {
            break;
        }
    }

    CError_ASSERT(4133, table->rtype);
    return table->opcode;
}

static short Intrinsics_FindOpcode2Args(Intrinsics id, ENode* funccall, ENode* arg1, ENode* arg2) {
    TypeTable31* table;
    Type* typeB1;
    Type* typeB2;
    Type* typeA1;
    Type* typeA2;

    for (table = typeTable[id - Intrinsic_vec_add]; table->rtype; table++) {
        typeB1 = arg1->rtype;
        typeB2 = arg2->rtype;
        typeA1 = table->arg1;
        typeA2 = table->arg2;
        if (IS_TYPE_POINTER(typeA1) && IS_TYPE_POINTER(typeB1)) {
            typeA1 = TPTR_TARGET(typeA1);
            typeB1 = TPTR_TARGET(typeB1);
        }
        if (IS_TYPE_POINTER(typeA2) && IS_TYPE_POINTER(typeB2)) {
            typeA2 = TPTR_TARGET(typeA2);
            typeB2 = TPTR_TARGET(typeB2);
        }
        if (promotable_types_are_equal(typeA1, typeB1) &&
            promotable_types_are_equal(typeA2, typeB2)) {
            break;
        }
    }

    CError_ASSERT(4172, table->rtype);
    return table->opcode;
}

static short Intrinsics_FindOpcode3Args(
    Intrinsics id, ENode* funccall, ENode* arg1, ENode* arg2, ENode* arg3
) {
    TypeTable41* table;
    Type* typeB1;
    Type* typeB2;
    Type* typeB3;
    Type* typeA1;
    Type* typeA2;
    Type* typeA3;

    for (table = typeTable[id - Intrinsic_vec_add]; table->rtype; table++) {
        typeB1 = arg1->rtype;
        typeB2 = arg2->rtype;
        typeB3 = arg3->rtype;
        typeA1 = table->arg1;
        typeA2 = table->arg2;
        typeA3 = table->arg3;
        if (IS_TYPE_POINTER(typeA1) && IS_TYPE_POINTER(typeB1)) {
            typeA1 = TPTR_TARGET(typeA1);
            typeB1 = TPTR_TARGET(typeB1);
        }
        if (IS_TYPE_POINTER(typeA2) && IS_TYPE_POINTER(typeB2)) {
            typeA2 = TPTR_TARGET(typeA2);
            typeB2 = TPTR_TARGET(typeB2);
        }
        if (IS_TYPE_POINTER(typeA3) && IS_TYPE_POINTER(typeB3)) {
            typeA3 = TPTR_TARGET(typeA3);
            typeB3 = TPTR_TARGET(typeB3);
        }
        if (promotable_types_are_equal(typeA1, typeB1) &&
            promotable_types_are_equal(typeA2, typeB2) &&
            promotable_types_are_equal(typeA3, typeB3)) {
            break;
        }
    }

    CError_ASSERT(4219, table->rtype);
    return table->opcode;
}

static void vector_intrinsic_no_args(short opcode) {
    emitpcode(opcode);
}

static void vector_intrinsic_mfvscr(short outputReg, short opcode, Operand* output) {
    short reg;

    reg = outputReg ? outputReg : ALLOC_VR();

    emitpcode(opcode, reg);

    output->optype = OpndType_VR;
    output->reg = reg;
}

static void vector_intrinsic_1arg(ENode* arg1, short outputReg, Operand* output, short opcode) {
    Operand op1;
    short reg;

    memclrw(&op1, sizeof(op1));
    GEN_NODE_TO_VR(arg1, &op1, arg1->rtype, 0);

    reg = outputReg ? outputReg : ALLOC_VR();

    emitpcode(opcode, reg, op1.reg);

    output->optype = OpndType_VR;
    output->reg = reg;
}

static void vector_intrinsic_splats(ENode* arg1, short outputReg, Operand* output, short opcode) {
    Operand op1;
    short reg;

    memclrw(&op1, sizeof(op1));
    CError_ASSERT(4281, ENODE_IS(arg1, EINTCONST));
    GEN_NODE(arg1, &op1);

    reg = outputReg ? outputReg : ALLOC_VR();

    emitpcode(opcode, reg, op1.immediate);

    output->optype = OpndType_VR;
    output->reg = reg;
}

static void vector_intrinsic_splatu8(ENode* arg1, short outputReg, Operand* output, short opcode) {
    Operand op1;
    short reg;

    memclrw(&op1, sizeof(op1));
    CError_ASSERT(4305, ENODE_IS(arg1, EINTCONST));
    GEN_NODE(arg1, &op1);

    reg = outputReg ? outputReg : ALLOC_VR();

    emitpcode(opcode, reg, op1.immediate);

    output->optype = OpndType_VR;
    output->reg = reg;
}

static void vector_intrinsic_splatu16(ENode* arg1, short outputReg, Operand* output, short opcode) {
    Operand op1;
    short reg;

    memclrw(&op1, sizeof(op1));
    CError_ASSERT(4329, ENODE_IS(arg1, EINTCONST));
    GEN_NODE(arg1, &op1);

    reg = outputReg ? outputReg : ALLOC_VR();

    emitpcode(opcode, reg, op1.immediate);

    output->optype = OpndType_VR;
    output->reg = reg;
}

static void vector_intrinsic_splatu32(ENode* arg1, short outputReg, Operand* output, short opcode) {
    Operand op1;
    short reg;

    memclrw(&op1, sizeof(op1));
    CError_ASSERT(4353, ENODE_IS(arg1, EINTCONST));
    GEN_NODE(arg1, &op1);

    reg = outputReg ? outputReg : ALLOC_VR();

    emitpcode(opcode, reg, op1.immediate);

    output->optype = OpndType_VR;
    output->reg = reg;
}

static void vector_intrinsic_dss(ENode* arg1) {
    Operand op1;

    memclrw(&op1, sizeof(op1));
    CError_ASSERT(4376, ENODE_IS(arg1, EINTCONST));
    GEN_NODE(arg1, &op1);

    emitpcode(PC_DSS, op1.immediate, 0);
}

static void vector_intrinsic_2args(
    ENode* arg1, ENode* arg2, short outputReg, Operand* output, short opcode
) {
    Operand op1;
    Operand op2;
    short reg;

    memclrw(&op1, sizeof(op1));
    memclrw(&op2, sizeof(op2));
    GEN_NODE_TO_VR(arg1, &op1, arg1->rtype, 0);
    GEN_NODE_TO_VR(arg2, &op2, arg2->rtype, 0);

    reg = outputReg ? outputReg : ALLOC_VR();

    emitpcode(opcode, reg, op1.reg, op2.reg);

    output->optype = OpndType_VR;
    output->reg = reg;
}

static void vector_intrinsic_2args1const(
    ENode* arg1, ENode* arg2, short outputReg, Operand* output, short opcode
) {
    Operand op1;
    Operand op2;
    short reg;

    memclrw(&op1, sizeof(op1));
    memclrw(&op2, sizeof(op2));
    CError_ASSERT(4421, ENODE_IS(arg2, EINTCONST));
    GEN_NODE_TO_VR(arg1, &op1, arg1->rtype, 0);
    GEN_NODE(arg2, &op2);

    reg = outputReg ? outputReg : ALLOC_VR();

    emitpcode(opcode, reg, op1.reg, op2.immediate);

    output->optype = OpndType_VR;
    output->reg = reg;
}

static void vector_intrinsic_3args(
    ENode* arg1, ENode* arg2, ENode* arg3, short outputReg, Operand* output, short opcode
) {
    Operand op1;
    Operand op2;
    Operand op3;
    short reg;

    memclrw(&op1, sizeof(op1));
    memclrw(&op2, sizeof(op2));
    memclrw(&op3, sizeof(op3));
    GEN_NODE_TO_VR(arg1, &op1, arg1->rtype, 0);
    GEN_NODE_TO_VR(arg2, &op2, arg2->rtype, 0);
    GEN_NODE_TO_VR(arg3, &op3, arg3->rtype, 0);

    reg = outputReg ? outputReg : ALLOC_VR();

    emitpcode(opcode, reg, op1.reg, op2.reg, op3.reg);

    output->optype = OpndType_VR;
    output->reg = reg;
}

static void vector_intrinsic_datastream(ENode* arg1, ENode* arg2, ENode* arg3, short opcode) {
    Operand op1;
    Operand op2;

    memclrw(&op1, sizeof(op1));
    memclrw(&op2, sizeof(op2));
    CError_ASSERT(4473, ENODE_IS(arg3, EINTCONST));
    GEN_NODE_TO_GPR(arg1, &op1, arg1->rtype, 0);
    GEN_NODE_TO_GPR(arg2, &op2, arg2->rtype, 0);

    switch (opcode) {
        case PC_DST:
        case PC_DSTST:
            emitpcode(opcode, op1.reg, op2.reg, arg3->data.intval.lo, 0);
            break;
        case PC_DSTT:
        case PC_DSTSTT:
            emitpcode(opcode, op1.reg, op2.reg, arg3->data.intval.lo);
            break;
        default:
            CError_FATAL(4491);
    }
}

static void vector_intrinsic_sld(
    ENode* arg1, ENode* arg2, ENode* arg3, short outputReg, Operand* output, short opcode
) {
    Operand op1;
    Operand op2;
    short reg;

    memclrw(&op1, sizeof(op1));
    memclrw(&op2, sizeof(op2));
    CError_ASSERT(4507, ENODE_IS(arg3, EINTCONST));
    GEN_NODE_TO_VR(arg1, &op1, arg1->rtype, 0);
    GEN_NODE_TO_VR(arg2, &op2, arg2->rtype, 0);
    reg = outputReg ? outputReg : ALLOC_VR();

    emitpcode(opcode, reg, op1.reg, op2.reg, arg3->data.intval.lo);
    output->optype = OpndType_VR;
    output->reg = reg;
}

static void vector_intrinsic_load(
    ENode* arg1, ENode* arg2, short outputReg, Operand* output, short opcode
) {
    Operand op1;
    Operand op2;
    short reg;

    memclrw(&op1, sizeof(op1));
    memclrw(&op2, sizeof(op2));

    GEN_NODE(arg1, &op1);
    if (op1.optype == OpndType_Absolute && op1.immediate == 0) {
        op1.optype = OpndType_GPR;
        op1.reg = 0;
    } else {
        ENSURE_GPR(&op1, arg1->rtype, 0);
    }

    GEN_NODE(arg2, &op2);
    if (op2.optype == OpndType_Absolute && op2.immediate == 0 && op1.reg != 0) {
        op2 = op1;
        op1.optype = OpndType_GPR;
        op1.reg = 0;
    } else {
        ENSURE_GPR(&op2, arg2->rtype, 0);
    }

    reg = outputReg ? outputReg : ALLOC_VR();

    emitpcode(opcode, reg, op1.reg, op2.reg);
    output->optype = OpndType_VR;
    output->reg = reg;
}

static void vector_intrinsic_store(
    ENode* arg1, ENode* arg2, ENode* arg3, short outputReg, Operand* output, short opcode
) {
    Operand op1;
    Operand op2;
    Operand op3;

    memclrw(&op1, sizeof(op1));
    memclrw(&op2, sizeof(op2));
    memclrw(&op3, sizeof(op3));

    GEN_NODE_TO_VR(arg1, &op1, arg1->rtype, 0);

    GEN_NODE(arg2, &op2);
    if (op2.optype == OpndType_Absolute && op2.immediate == 0) {
        op2.optype = OpndType_GPR;
        op2.reg = 0;
    } else {
        ENSURE_GPR(&op2, arg2->rtype, 0);
    }

    GEN_NODE(arg3, &op3);
    if (op3.optype == OpndType_Absolute && op3.immediate == 0 && op2.reg != 0) {
        op3 = op2;
        op2.optype = OpndType_GPR;
        op2.reg = 0;
    } else {
        ENSURE_GPR(&op3, arg3->rtype, 0);
    }

    emitpcode(opcode, op1.reg, op2.reg, op3.reg);
    output->optype = OpndType_VR;
    output->reg = op1.reg;
}

static void vector_intrinsic_abs(
    Intrinsics id, ENode* funccall, ENode* arg1, short outputReg, Operand* output
) {
    TypeTable22* table;
    Type* typeB;
    Type* typeA;
    short reg1;
    short reg2;
    short resultReg;
    Operand op1;

    for (table = typeTable[id - Intrinsic_vec_add]; table->rtype; table++) {
        typeB = arg1->rtype;
        typeA = table->arg1;
        if (IS_TYPE_POINTER(typeA) && IS_TYPE_POINTER(typeB)) {
            typeA = TPTR_TARGET(typeA);
            typeB = TPTR_TARGET(typeB);
        }
        if (promotable_types_are_equal(typeA, typeB)) {
            break;
        }
    }

    CError_ASSERT(4645, table->rtype);

    reg1 = ALLOC_VR();
    reg2 = ALLOC_VR();
    resultReg = outputReg ? outputReg : ALLOC_VR();

    memclrw(&op1, sizeof(op1));
    GEN_NODE_TO_VR(arg1, &op1, arg1->rtype, 0);

    if (arg1->rtype == TYPE(&stvectorfloat)) {
        emitpcode(PC_VSPLTISW, reg1, -1);
        emitpcode(PC_VSLW, reg2, reg1, reg1);
        emitpcode(table->opcode1, resultReg, op1.reg, reg2);
    } else {
        emitpcode(PC_VSPLTISB, reg1, 0);
        emitpcode(table->opcode1, reg2, reg1, op1.reg);
        emitpcode(table->opcode2, resultReg, op1.reg, reg2);
    }

    output->optype = OpndType_VR;
    output->reg = resultReg;
}

static void vector_intrinsic_abss(
    Intrinsics id, ENode* funccall, ENode* arg1, short outputReg, Operand* output
) {
    TypeTable22* table;
    Type* typeB;
    Type* typeA;
    short zeroReg;
    short negatedReg;
    short resultReg;
    Operand op1;

    for (table = typeTable[id - Intrinsic_vec_add]; table->rtype; table++) {
        typeB = arg1->rtype;
        typeA = table->arg1;
        if (IS_TYPE_POINTER(typeA) && IS_TYPE_POINTER(typeB)) {
            typeA = TPTR_TARGET(typeA);
            typeB = TPTR_TARGET(typeB);
        }
        if (promotable_types_are_equal(typeA, typeB)) {
            break;
        }
    }

    CError_ASSERT(4711, table->rtype);

    zeroReg = ALLOC_VR();
    negatedReg = ALLOC_VR();
    resultReg = outputReg ? outputReg : ALLOC_VR();

    memclrw(&op1, sizeof(op1));
    GEN_NODE_TO_VR(arg1, &op1, arg1->rtype, 0);

    emitpcode(PC_VSPLTISB, zeroReg, 0);
    emitpcode(table->opcode1, negatedReg, zeroReg, op1.reg);
    emitpcode(table->opcode2, resultReg, op1.reg, negatedReg);

    output->optype = OpndType_VR;
    output->reg = resultReg;
}

static void vector_intrinsic_mtvscr(ENode* arg1, Operand* output, short opcode) {
    Operand op1;

    memclrw(&op1, sizeof(op1));
    GEN_NODE_TO_VR(arg1, &op1, arg1->rtype, 0);
    emitpcode(opcode, op1.reg);
}

static void vector_predicate_2args(
    ENode* arg1, ENode* arg2, short outputReg, Operand* output, short opcode, Intrinsics id
) {
    Operand op1;
    Operand op2;
    short resultReg;
    short leftReg;
    short rightReg;
    short cond;

    memclrw(&op1, sizeof(op1));
    memclrw(&op2, sizeof(op2));

    GEN_NODE_TO_VR(arg1, &op1, arg1->rtype, 0);
    GEN_NODE_TO_VR(arg2, &op2, arg2->rtype, 0);

    resultReg = ALLOC_VR();
    leftReg = op1.reg;
    rightReg = op2.reg;
    if (((id == Intrinsic_vec_all_ge || id == Intrinsic_vec_any_ge) &&
         arg1->rtype != TYPE(&stvectorfloat)) ||
        ((id == Intrinsic_vec_all_le || id == Intrinsic_vec_any_le) &&
         arg1->rtype == TYPE(&stvectorfloat)) ||
        id == Intrinsic_vec_all_lt ||
        id == Intrinsic_vec_any_lt ||
        id == Intrinsic_vec_any_nle ||
        id == Intrinsic_vec_all_nle ||
        id == Intrinsic_vec_all_nlt ||
        id == Intrinsic_vec_any_nlt) {
        rightReg = op1.reg;
        leftReg = op2.reg;
    }
    emitpcode(opcode, resultReg, leftReg, rightReg);
    pcsetrecordbit(pclastblock->lastPCode);

    if (arg1->rtype == TYPE(&stvectorfloat)) {
        switch (id) {
            case Intrinsic_vec_all_eq:
            case Intrinsic_vec_all_ge:
            case Intrinsic_vec_all_gt:
            case Intrinsic_vec_all_le:
            case Intrinsic_vec_all_lt:
                cond = ELESS;
                break;
            case Intrinsic_vec_any_ne:
            case Intrinsic_vec_any_nge:
            case Intrinsic_vec_any_ngt:
            case Intrinsic_vec_any_nle:
            case Intrinsic_vec_any_nlt:
                cond = EGREATEREQU;
                break;
            case Intrinsic_vec_all_in:
            case Intrinsic_vec_all_ne:
            case Intrinsic_vec_all_nge:
            case Intrinsic_vec_all_ngt:
            case Intrinsic_vec_all_nle:
            case Intrinsic_vec_all_nlt:
                cond = EEQU;
                break;
            case Intrinsic_vec_any_eq:
            case Intrinsic_vec_any_ge:
            case Intrinsic_vec_any_gt:
            case Intrinsic_vec_any_le:
            case Intrinsic_vec_any_lt:
            case Intrinsic_vec_any_out:
                cond = ENOTEQU;
                break;
            default:
                CError_FATAL(4833);
        }
    } else {
        switch (id) {
            case Intrinsic_vec_all_eq:
            case Intrinsic_vec_all_gt:
            case Intrinsic_vec_all_lt:
                cond = ELESS;
                break;
            case Intrinsic_vec_any_eq:
            case Intrinsic_vec_any_gt:
            case Intrinsic_vec_any_lt:
                cond = ENOTEQU;
                break;
            case Intrinsic_vec_all_ge:
            case Intrinsic_vec_all_le:
            case Intrinsic_vec_all_ne:
                cond = EEQU;
                break;
            case Intrinsic_vec_any_ge:
            case Intrinsic_vec_any_le:
            case Intrinsic_vec_any_ne:
                cond = EGREATEREQU;
                break;
            default:
                CError_FATAL(4862);
        }
    }

    output->optype = OpndType_CRField;
    output->reg = 6;
    output->regOffset = cond;
}

static void vector_predicate_1arg(
    ENode* arg1, short outputReg, Operand* output, short opcode, Intrinsics id
) {
    Operand op1;
    short reg;
    short cond;

    memclrw(&op1, sizeof(op1));

    GEN_NODE_TO_VR(arg1, &op1, arg1->rtype, 0);

    reg = ALLOC_VR();
    emitpcode(opcode, reg, op1.reg, op1.reg);
    pcsetrecordbit(pclastblock->lastPCode);

    switch (id) {
        case Intrinsic_vec_all_numeric:
            cond = ELESS;
            break;
        case Intrinsic_vec_any_nan:
            cond = EGREATEREQU;
            break;
        case Intrinsic_vec_all_nan:
            cond = EEQU;
            break;
        case Intrinsic_vec_any_numeric:
            cond = ENOTEQU;
            break;
        default:
            CError_FATAL(4906);
    }

    output->optype = OpndType_CRField;
    output->reg = 6;
    output->regOffset = cond;
}

ENode* Intrinsics_HandleIntrinsicCall(Object* func, ENodeList* args) {
    ENode* callexpr;
    Type* rtype;
    Intrinsics id;

    callexpr = NULL;

    if (copts.altivec_model) {
        id = func->u.func.u.intrinsicid;
        switch (id) {
            case Intrinsic_vec_dst:
            case Intrinsic_vec_dstst:
            case Intrinsic_vec_dststt:
            case Intrinsic_vec_dstt:
            case Intrinsic_vec_madd:
            case Intrinsic_vec_madds:
            case Intrinsic_vec_mladd:
            case Intrinsic_vec_mradds:
            case Intrinsic_vec_msum:
            case Intrinsic_vec_msums:
            case Intrinsic_vec_nmsub:
            case Intrinsic_vec_perm:
            case Intrinsic_vec_sel:
            case Intrinsic_vec_sld:
            case Intrinsic_vec_st:
            case Intrinsic_vec_ste:
            case Intrinsic_vec_stl:
            case Intrinsic_vec_vmaddfp:
            case Intrinsic_vec_vmhaddshs:
            case Intrinsic_vec_vmladduhm:
            case Intrinsic_vec_vmhraddshs:
            case Intrinsic_vec_vmsumubm:
            case Intrinsic_vec_vmsumuhm:
            case Intrinsic_vec_vmsummbm:
            case Intrinsic_vec_vmsumshm:
            case Intrinsic_vec_vmsumuhs:
            case Intrinsic_vec_vmsumshs:
            case Intrinsic_vec_vnmsubfp:
            case Intrinsic_vec_vperm:
            case Intrinsic_vec_vsel:
            case Intrinsic_vec_vsldoi:
            case Intrinsic_vec_stvx:
            case Intrinsic_vec_stvebx:
            case Intrinsic_vec_stvehx:
            case Intrinsic_vec_stvewx:
            case Intrinsic_vec_stvxl:
                if (Intrinsics_VerifyParameterCount(3, args, func->name)) {
                    if ((rtype = Intrinsics_Verify3VectorArgs(id, args, func->name))) {
                        callexpr = Intrinsics_CreateIntrinsicFunc(func, args, rtype);
                    }
                }
                break;
            case Intrinsic_vec_add:
            case Intrinsic_vec_addc:
            case Intrinsic_vec_adds:
            case Intrinsic_vec_and:
            case Intrinsic_vec_andc:
            case Intrinsic_vec_avg:
            case Intrinsic_vec_cmpb:
            case Intrinsic_vec_cmpeq:
            case Intrinsic_vec_cmpge:
            case Intrinsic_vec_cmple:
            case Intrinsic_vec_cmpgt:
            case Intrinsic_vec_cmplt:
            case Intrinsic_vec_ctf:
            case Intrinsic_vec_cts:
            case Intrinsic_vec_ctu:
            case Intrinsic_vec_ld:
            case Intrinsic_vec_lde:
            case Intrinsic_vec_ldl:
            case Intrinsic_vec_lvsl:
            case Intrinsic_vec_lvsr:
            case Intrinsic_vec_max:
            case Intrinsic_vec_mergeh:
            case Intrinsic_vec_mergel:
            case Intrinsic_vec_min:
            case Intrinsic_vec_mule:
            case Intrinsic_vec_mulo:
            case Intrinsic_vec_nor:
            case Intrinsic_vec_or:
            case Intrinsic_vec_pack:
            case Intrinsic_vec_packpx:
            case Intrinsic_vec_packs:
            case Intrinsic_vec_packsu:
            case Intrinsic_vec_rl:
            case Intrinsic_vec_sl:
            case Intrinsic_vec_sll:
            case Intrinsic_vec_slo:
            case Intrinsic_vec_splat:
            case Intrinsic_vec_sr:
            case Intrinsic_vec_sra:
            case Intrinsic_vec_srl:
            case Intrinsic_vec_sro:
            case Intrinsic_vec_sub:
            case Intrinsic_vec_subc:
            case Intrinsic_vec_subs:
            case Intrinsic_vec_sum4s:
            case Intrinsic_vec_sum2s:
            case Intrinsic_vec_sums:
            case Intrinsic_vec_unpack2sh:
            case Intrinsic_vec_unpack2sl:
            case Intrinsic_vec_unpack2uh:
            case Intrinsic_vec_unpack2ul:
            case Intrinsic_vec_xor:
            case Intrinsic_vec_all_eq:
            case Intrinsic_vec_all_ge:
            case Intrinsic_vec_all_gt:
            case Intrinsic_vec_all_in:
            case Intrinsic_vec_all_le:
            case Intrinsic_vec_all_lt:
            case Intrinsic_vec_all_ne:
            case Intrinsic_vec_all_nge:
            case Intrinsic_vec_all_ngt:
            case Intrinsic_vec_all_nle:
            case Intrinsic_vec_all_nlt:
            case Intrinsic_vec_any_eq:
            case Intrinsic_vec_any_ge:
            case Intrinsic_vec_any_gt:
            case Intrinsic_vec_any_le:
            case Intrinsic_vec_any_lt:
            case Intrinsic_vec_any_ne:
            case Intrinsic_vec_any_nge:
            case Intrinsic_vec_any_ngt:
            case Intrinsic_vec_any_nle:
            case Intrinsic_vec_any_nlt:
            case Intrinsic_vec_any_out:
            case Intrinsic_vec_vaddubm:
            case Intrinsic_vec_vadduhm:
            case Intrinsic_vec_vadduwm:
            case Intrinsic_vec_vaddfp:
            case Intrinsic_vec_vaddcuw:
            case Intrinsic_vec_vaddubs:
            case Intrinsic_vec_vaddsbs:
            case Intrinsic_vec_vadduhs:
            case Intrinsic_vec_vaddshs:
            case Intrinsic_vec_vadduws:
            case Intrinsic_vec_vaddsws:
            case Intrinsic_vec_vand:
            case Intrinsic_vec_vandc:
            case Intrinsic_vec_vavgub:
            case Intrinsic_vec_vavgsb:
            case Intrinsic_vec_vavguh:
            case Intrinsic_vec_vavgsh:
            case Intrinsic_vec_vavguw:
            case Intrinsic_vec_vavgsw:
            case Intrinsic_vec_vcmpbfp:
            case Intrinsic_vec_vcmpequb:
            case Intrinsic_vec_vcmpequh:
            case Intrinsic_vec_vcmpequw:
            case Intrinsic_vec_vcmpeqfp:
            case Intrinsic_vec_vcmpgefp:
            case Intrinsic_vec_vcmpgtub:
            case Intrinsic_vec_vcmpgtsb:
            case Intrinsic_vec_vcmpgtuh:
            case Intrinsic_vec_vcmpgtsh:
            case Intrinsic_vec_vcmpgtuw:
            case Intrinsic_vec_vcmpgtsw:
            case Intrinsic_vec_vcmpgtfp:
            case Intrinsic_vec_vcfux:
            case Intrinsic_vec_vcfsx:
            case Intrinsic_vec_vctsxs:
            case Intrinsic_vec_vctuxs:
            case Intrinsic_vec_lvx:
            case Intrinsic_vec_lvebx:
            case Intrinsic_vec_lvehx:
            case Intrinsic_vec_lvewx:
            case Intrinsic_vec_lvxl:
            case Intrinsic_vec_vmaxub:
            case Intrinsic_vec_vmaxsb:
            case Intrinsic_vec_vmaxuh:
            case Intrinsic_vec_vmaxsh:
            case Intrinsic_vec_vmaxuw:
            case Intrinsic_vec_vmaxsw:
            case Intrinsic_vec_vmaxfp:
            case Intrinsic_vec_vmrghb:
            case Intrinsic_vec_vmrghh:
            case Intrinsic_vec_vmrghw:
            case Intrinsic_vec_vmrglb:
            case Intrinsic_vec_vmrglh:
            case Intrinsic_vec_vmrglw:
            case Intrinsic_vec_vminub:
            case Intrinsic_vec_vminsb:
            case Intrinsic_vec_vminuh:
            case Intrinsic_vec_vminsh:
            case Intrinsic_vec_vminuw:
            case Intrinsic_vec_vminsw:
            case Intrinsic_vec_vminfp:
            case Intrinsic_vec_vmuleub:
            case Intrinsic_vec_vmulesb:
            case Intrinsic_vec_vmuleuh:
            case Intrinsic_vec_vmulesh:
            case Intrinsic_vec_vmuloub:
            case Intrinsic_vec_vmulosb:
            case Intrinsic_vec_vmulouh:
            case Intrinsic_vec_vmulosh:
            case Intrinsic_vec_vnor:
            case Intrinsic_vec_vor:
            case Intrinsic_vec_vpkuhum:
            case Intrinsic_vec_vpkuwum:
            case Intrinsic_vec_vpkpx:
            case Intrinsic_vec_vpkuhus:
            case Intrinsic_vec_vpkshss:
            case Intrinsic_vec_vpkuwus:
            case Intrinsic_vec_vpkswss:
            case Intrinsic_vec_vpkshus:
            case Intrinsic_vec_vpkswus:
            case Intrinsic_vec_vrlb:
            case Intrinsic_vec_vrlh:
            case Intrinsic_vec_vrlw:
            case Intrinsic_vec_vslb:
            case Intrinsic_vec_vslh:
            case Intrinsic_vec_vslw:
            case Intrinsic_vec_vsl:
            case Intrinsic_vec_vslo:
            case Intrinsic_vec_vspltb:
            case Intrinsic_vec_vsplth:
            case Intrinsic_vec_vspltw:
            case Intrinsic_vec_vsrb:
            case Intrinsic_vec_vsrh:
            case Intrinsic_vec_vsrw:
            case Intrinsic_vec_vsrab:
            case Intrinsic_vec_vsrah:
            case Intrinsic_vec_vsraw:
            case Intrinsic_vec_vsr:
            case Intrinsic_vec_vsro:
            case Intrinsic_vec_vsububm:
            case Intrinsic_vec_vsubuhm:
            case Intrinsic_vec_vsubuwm:
            case Intrinsic_vec_vsubfp:
            case Intrinsic_vec_vsubcuw:
            case Intrinsic_vec_vsububs:
            case Intrinsic_vec_vsubsbs:
            case Intrinsic_vec_vsubuhs:
            case Intrinsic_vec_vsubshs:
            case Intrinsic_vec_vsubuws:
            case Intrinsic_vec_vsubsws:
            case Intrinsic_vec_vsum4ubs:
            case Intrinsic_vec_vsum4sbs:
            case Intrinsic_vec_vsum4shs:
            case Intrinsic_vec_vsum2sws:
            case Intrinsic_vec_vsumsws:
            case Intrinsic_vec_vxor:
                if (Intrinsics_VerifyParameterCount(2, args, func->name)) {
                    if ((rtype = Intrinsics_Verify2VectorArgs(id, args, func->name))) {
                        callexpr = Intrinsics_CreateIntrinsicFunc(func, args, rtype);
                    }
                }
                break;
            case Intrinsic_vec_ceil:
            case Intrinsic_vec_dss:
            case Intrinsic_vec_expte:
            case Intrinsic_vec_floor:
            case Intrinsic_vec_loge:
            case Intrinsic_vec_mtvscr:
            case Intrinsic_vec_re:
            case Intrinsic_vec_round:
            case Intrinsic_vec_rsqrte:
            case Intrinsic_vec_splat_s8:
            case Intrinsic_vec_splat_s16:
            case Intrinsic_vec_splat_s32:
            case Intrinsic_vec_splat_u8:
            case Intrinsic_vec_splat_u16:
            case Intrinsic_vec_splat_u32:
            case Intrinsic_vec_trunc:
            case Intrinsic_vec_unpackh:
            case Intrinsic_vec_unpackl:
            case Intrinsic_vec_all_nan:
            case Intrinsic_vec_all_numeric:
            case Intrinsic_vec_any_nan:
            case Intrinsic_vec_any_numeric:
            case Intrinsic_vec_vrfip:
            case Intrinsic_vec_vexptefp:
            case Intrinsic_vec_vrfim:
            case Intrinsic_vec_vlogefp:
            case Intrinsic_vec_vrefp:
            case Intrinsic_vec_vrfin:
            case Intrinsic_vec_vrsqrtefp:
            case Intrinsic_vec_vspltisb:
            case Intrinsic_vec_vspltish:
            case Intrinsic_vec_vspltisw:
            case Intrinsic_vec_vrfiz:
            case Intrinsic_vec_vupkhsb:
            case Intrinsic_vec_vupklsb:
            case Intrinsic_vec_vupkhpx:
            case Intrinsic_vec_vupklpx:
            case Intrinsic_vec_vupkhsh:
            case Intrinsic_vec_vupklsh:
                if (Intrinsics_VerifyParameterCount(1, args, func->name)) {
                    if ((rtype = Intrinsics_Verify1VectorArg(id, args, func->name))) {
                        callexpr = Intrinsics_CreateIntrinsicFunc(func, args, rtype);
                    }
                }
                break;
            case Intrinsic_vec_dssall:
            case Intrinsic_vec_mfvscr:
                if (Intrinsics_VerifyParameterCount(0, args, func->name)) {
                    rtype = Intrinsics_VerifyNoVectorArgs(id, func->name);
                    if (rtype) {
                        callexpr = Intrinsics_CreateIntrinsicFunc(func, args, rtype);
                    }
                }
                break;
            case Intrinsic_vec_abs:
            case Intrinsic_vec_abss:
                if (Intrinsics_VerifyParameterCount(1, args, func->name)) {
                    if ((rtype = Intrinsics_Verify1VectorArg2Ops(id, args, func->name))) {
                        callexpr = Intrinsics_CreateIntrinsicFunc(func, args, rtype);
                    }
                }
                break;
            default:
                break;
        }
    }

    return callexpr;
}

static void rtos_change_stack_intrinsic(ENode* expr) {
    Operand op;

    memclrw(&op, sizeof(op));
    GEN_NODE_TO_GPR(expr, &op, expr->rtype, 0);
    change_dynamic_stack_space(op.reg);
}

void call_intrinsic_function(ENode* funccall, short outputReg, Operand* output) {
    ENodeList* args;
    Object* object;
    Intrinsics id;
    short op;
    short reg;

    // clang-format off
    static short opcode[MaxIntrinsics] = {  PC_EIEIO,
                                            PC_SYNC,
                                            PC_ISYNC,   0, 0,
                                            PC_FABS,
                                            PC_FNABS,   0, 0,
                                            PC_CNTLZW,
                                            PC_LHBRX,
                                            PC_LWBRX,
                                            PC_STHBRX,
                                            PC_STWBRX,
                                            PC_DCBF,
                                            PC_DCBT,
                                            PC_DCBST,
                                            PC_DCBTST,
                                            PC_DCBZ,
                                            PC_MULHW,
                                            PC_MULHWU,
                                            PC_DIVW,
                                            PC_DIVWU,
                                            PC_FMADD,
                                            PC_FMSUB,
                                            PC_FNMADD,
                                            PC_FNMSUB,
                                            PC_FMADDS,
                                            PC_FMSUBS,
                                            PC_FNMADDS,
                                            PC_FNMSUBS,
                                            PC_MFFS,
                                            PC_FRES,
                                            PC_FRSQRTE,
                                            PC_FSEL,    0, 0,
                                            PC_RLWIMI,
                                            PC_RLWINM,
                                            PC_RLWNM,
                                            PC_FABS,
                                            PC_FNABS};
    // clang-format on

    args = funccall->data.funccall.args;
    object = funccall->data.funccall.funcref->data.objref;
    id = object->u.func.u.intrinsicid;
    cur_intrinsic_object = object;

    switch (id) {
        case Intrinsic_eieio:
        case Intrinsic_sync:
        case Intrinsic_isync:
            appendpcode(pclastblock, makepcode(opcode[id]));
            output->optype = OpndType_Absolute;
            break;
        case Intrinsic_abs:
        case Intrinsic_labs:
            abs_intrinsic(args->node, outputReg, output);
            break;
        case Intrinsic_fabs:
        case Intrinsic_fnabs:
        case Intrinsic_fres:
        case Intrinsic_frsqrte:
        case Intrinsic_fabsf:
        case Intrinsic_fnabsf:
            if ((!copts.use_fp || copts.soft_float) &&
                ENODE_IS(funccall->data.funccall.funcref, EOBJREF)) {
                PPCError_ErrorTerm(
                    PPCERR_SYMBOL_NEEDS_FLOAT, CMangler_GetLinkName(intrinsics[id])->name
                );
                break;
            }
            fp_unary_operator(opcode[id], args->node, outputReg, output);
            break;
        case Intrinsic_setflm:
            if ((!copts.use_fp || copts.soft_float) &&
                ENODE_IS(funccall->data.funccall.funcref, EOBJREF)) {
                PPCError_ErrorTerm(
                    PPCERR_SYMBOL_NEEDS_FLOAT, CMangler_GetLinkName(intrinsics[id])->name
                );
                break;
            }
            setflm_intrinsic(args->node, outputReg, output, funccall->ignored);
            break;
        case Intrinsic_alloca:
            alloca_intrinsic(args->node, outputReg, output);
            break;
        case Intrinsic_cntlzw:
            unary_operator(PC_CNTLZW, args->node, outputReg, output);
            break;
        case Intrinsic_lhbrx:
        case Intrinsic_lwbrx:
            load_bytereversed_intrinsic(
                opcode[id], args->node, args->next->node, outputReg, output
            );
            break;
        case Intrinsic_sthbrx:
        case Intrinsic_stwbrx:
            store_bytereversed_intrinsic(
                opcode[id], args->node, args->next->node, args->next->next->node
            );
            output->optype = OpndType_Absolute;
            break;
        case Intrinsic_dcbf:
        case Intrinsic_dcbt:
        case Intrinsic_dcbst:
        case Intrinsic_dcbtst:
        case Intrinsic_dcbz:
            data_cache_block_intrinsic(opcode[id], args->node, args->next->node);
            output->optype = OpndType_Absolute;
            break;
        case Intrinsic_mulhw:
        case Intrinsic_mulhwu:
        case Intrinsic_divw:
        case Intrinsic_divwu:
            binary_operator(opcode[id], args->node, args->next->node, outputReg, output);
            break;
        case Intrinsic_fmadd:
        case Intrinsic_fmsub:
        case Intrinsic_fnmadd:
        case Intrinsic_fnmsub:
        case Intrinsic_fmadds:
        case Intrinsic_fmsubs:
        case Intrinsic_fnmadds:
        case Intrinsic_fnmsubs:
        case Intrinsic_fsel:
            if ((!copts.use_fp || copts.soft_float) &&
                ENODE_IS(funccall->data.funccall.funcref, EOBJREF)) {
                PPCError_ErrorTerm(
                    PPCERR_SYMBOL_NEEDS_FLOAT, CMangler_GetLinkName(intrinsics[id])->name
                );
                break;
            }
            fp_multiply_add(
                opcode[id], args->node, args->next->node, args->next->next->node, outputReg, output
            );
            break;
        case Intrinsic_mffs:
            if ((!copts.use_fp || copts.soft_float) &&
                ENODE_IS(funccall->data.funccall.funcref, EOBJREF)) {
                PPCError_ErrorTerm(
                    PPCERR_SYMBOL_NEEDS_FLOAT, CMangler_GetLinkName(intrinsics[id])->name
                );
                break;
            }
            reg = outputReg ? outputReg : ALLOC_FPR();
            emitpcode(PC_MFFS, output->reg = reg);
            output->optype = OpndType_FPR;
            break;
        case Intrinsic_strcpy:
            call_function(funccall, output);
            break;
        case Intrinsic_memcpy:
            if (ENODE_IS(args->next->next->node, EINTCONST)) {
                memcpy_intrinsic(
                    args->node,
                    args->next->node,
                    args->next->next->node->data.intval.lo,
                    funccall->ignored,
                    output
                );
            } else {
                call_function(funccall, output);
            }
            break;
        case Intrinsic_rlwimi:
            rlwimi_intrinsic(
                args->node,
                args->next->node,
                args->next->next->node,
                args->next->next->next->node,
                args->next->next->next->next->node,
                outputReg,
                output
            );
            break;
        case Intrinsic_rlwinm:
            rlwinm_intrinsic(
                args->node,
                args->next->node,
                args->next->next->node,
                args->next->next->next->node,
                outputReg,
                output
            );
            break;
        case Intrinsic_rlwnm:
            rlwnm_intrinsic(
                args->node,
                args->next->node,
                args->next->next->node,
                args->next->next->next->node,
                outputReg,
                output
            );
            break;
        case Intrinsic_vec_madd:
        case Intrinsic_vec_madds:
        case Intrinsic_vec_mladd:
        case Intrinsic_vec_mradds:
        case Intrinsic_vec_msum:
        case Intrinsic_vec_msums:
        case Intrinsic_vec_nmsub:
        case Intrinsic_vec_perm:
        case Intrinsic_vec_sel:
        case Intrinsic_vec_vmaddfp:
        case Intrinsic_vec_vmhaddshs:
        case Intrinsic_vec_vmladduhm:
        case Intrinsic_vec_vmhraddshs:
        case Intrinsic_vec_vmsumubm:
        case Intrinsic_vec_vmsumuhm:
        case Intrinsic_vec_vmsummbm:
        case Intrinsic_vec_vmsumshm:
        case Intrinsic_vec_vmsumuhs:
        case Intrinsic_vec_vmsumshs:
        case Intrinsic_vec_vnmsubfp:
        case Intrinsic_vec_vperm:
        case Intrinsic_vec_vsel:
            op = Intrinsics_FindOpcode3Args(
                id, funccall, args->node, args->next->node, args->next->next->node
            );
            vector_intrinsic_3args(
                args->node, args->next->node, args->next->next->node, outputReg, output, op
            );
            break;
        case Intrinsic_vec_sld:
        case Intrinsic_vec_vsldoi:
            op = Intrinsics_FindOpcode3Args(
                id, funccall, args->node, args->next->node, args->next->next->node
            );
            vector_intrinsic_sld(
                args->node, args->next->node, args->next->next->node, outputReg, output, op
            );
            break;
        case Intrinsic_vec_add:
        case Intrinsic_vec_addc:
        case Intrinsic_vec_adds:
        case Intrinsic_vec_and:
        case Intrinsic_vec_andc:
        case Intrinsic_vec_avg:
        case Intrinsic_vec_cmpb:
        case Intrinsic_vec_cmpeq:
        case Intrinsic_vec_cmpge:
        case Intrinsic_vec_cmpgt:
        case Intrinsic_vec_max:
        case Intrinsic_vec_mergeh:
        case Intrinsic_vec_mergel:
        case Intrinsic_vec_min:
        case Intrinsic_vec_mule:
        case Intrinsic_vec_mulo:
        case Intrinsic_vec_nor:
        case Intrinsic_vec_or:
        case Intrinsic_vec_pack:
        case Intrinsic_vec_packpx:
        case Intrinsic_vec_packs:
        case Intrinsic_vec_packsu:
        case Intrinsic_vec_rl:
        case Intrinsic_vec_sl:
        case Intrinsic_vec_sll:
        case Intrinsic_vec_slo:
        case Intrinsic_vec_sr:
        case Intrinsic_vec_sra:
        case Intrinsic_vec_srl:
        case Intrinsic_vec_sro:
        case Intrinsic_vec_sub:
        case Intrinsic_vec_subc:
        case Intrinsic_vec_subs:
        case Intrinsic_vec_sum4s:
        case Intrinsic_vec_sum2s:
        case Intrinsic_vec_sums:
        case Intrinsic_vec_unpack2sh:
        case Intrinsic_vec_unpack2sl:
        case Intrinsic_vec_unpack2uh:
        case Intrinsic_vec_unpack2ul:
        case Intrinsic_vec_xor:
        case Intrinsic_vec_vaddubm:
        case Intrinsic_vec_vadduhm:
        case Intrinsic_vec_vadduwm:
        case Intrinsic_vec_vaddfp:
        case Intrinsic_vec_vaddcuw:
        case Intrinsic_vec_vaddubs:
        case Intrinsic_vec_vaddsbs:
        case Intrinsic_vec_vadduhs:
        case Intrinsic_vec_vaddshs:
        case Intrinsic_vec_vadduws:
        case Intrinsic_vec_vaddsws:
        case Intrinsic_vec_vand:
        case Intrinsic_vec_vandc:
        case Intrinsic_vec_vavgub:
        case Intrinsic_vec_vavgsb:
        case Intrinsic_vec_vavguh:
        case Intrinsic_vec_vavgsh:
        case Intrinsic_vec_vavguw:
        case Intrinsic_vec_vavgsw:
        case Intrinsic_vec_vcmpbfp:
        case Intrinsic_vec_vcmpequb:
        case Intrinsic_vec_vcmpequh:
        case Intrinsic_vec_vcmpequw:
        case Intrinsic_vec_vcmpeqfp:
        case Intrinsic_vec_vcmpgefp:
        case Intrinsic_vec_vcmpgtub:
        case Intrinsic_vec_vcmpgtsb:
        case Intrinsic_vec_vcmpgtuh:
        case Intrinsic_vec_vcmpgtsh:
        case Intrinsic_vec_vcmpgtuw:
        case Intrinsic_vec_vcmpgtsw:
        case Intrinsic_vec_vcmpgtfp:
        case Intrinsic_vec_vmaxub:
        case Intrinsic_vec_vmaxsb:
        case Intrinsic_vec_vmaxuh:
        case Intrinsic_vec_vmaxsh:
        case Intrinsic_vec_vmaxuw:
        case Intrinsic_vec_vmaxsw:
        case Intrinsic_vec_vmaxfp:
        case Intrinsic_vec_vmrghb:
        case Intrinsic_vec_vmrghh:
        case Intrinsic_vec_vmrghw:
        case Intrinsic_vec_vmrglb:
        case Intrinsic_vec_vmrglh:
        case Intrinsic_vec_vmrglw:
        case Intrinsic_vec_vminub:
        case Intrinsic_vec_vminsb:
        case Intrinsic_vec_vminuh:
        case Intrinsic_vec_vminsh:
        case Intrinsic_vec_vminuw:
        case Intrinsic_vec_vminsw:
        case Intrinsic_vec_vminfp:
        case Intrinsic_vec_vmuleub:
        case Intrinsic_vec_vmulesb:
        case Intrinsic_vec_vmuleuh:
        case Intrinsic_vec_vmulesh:
        case Intrinsic_vec_vmuloub:
        case Intrinsic_vec_vmulosb:
        case Intrinsic_vec_vmulouh:
        case Intrinsic_vec_vmulosh:
        case Intrinsic_vec_vnor:
        case Intrinsic_vec_vor:
        case Intrinsic_vec_vpkuhum:
        case Intrinsic_vec_vpkuwum:
        case Intrinsic_vec_vpkpx:
        case Intrinsic_vec_vpkuhus:
        case Intrinsic_vec_vpkshss:
        case Intrinsic_vec_vpkuwus:
        case Intrinsic_vec_vpkswss:
        case Intrinsic_vec_vpkshus:
        case Intrinsic_vec_vpkswus:
        case Intrinsic_vec_vrlb:
        case Intrinsic_vec_vrlh:
        case Intrinsic_vec_vrlw:
        case Intrinsic_vec_vslb:
        case Intrinsic_vec_vslh:
        case Intrinsic_vec_vslw:
        case Intrinsic_vec_vsl:
        case Intrinsic_vec_vslo:
        case Intrinsic_vec_vsrb:
        case Intrinsic_vec_vsrh:
        case Intrinsic_vec_vsrw:
        case Intrinsic_vec_vsrab:
        case Intrinsic_vec_vsrah:
        case Intrinsic_vec_vsraw:
        case Intrinsic_vec_vsr:
        case Intrinsic_vec_vsro:
        case Intrinsic_vec_vsububm:
        case Intrinsic_vec_vsubuhm:
        case Intrinsic_vec_vsubuwm:
        case Intrinsic_vec_vsubfp:
        case Intrinsic_vec_vsubcuw:
        case Intrinsic_vec_vsububs:
        case Intrinsic_vec_vsubsbs:
        case Intrinsic_vec_vsubuhs:
        case Intrinsic_vec_vsubshs:
        case Intrinsic_vec_vsubuws:
        case Intrinsic_vec_vsubsws:
        case Intrinsic_vec_vsum4ubs:
        case Intrinsic_vec_vsum4sbs:
        case Intrinsic_vec_vsum4shs:
        case Intrinsic_vec_vsum2sws:
        case Intrinsic_vec_vsumsws:
        case Intrinsic_vec_vxor:
            op = Intrinsics_FindOpcode2Args(id, funccall, args->node, args->next->node);
            vector_intrinsic_2args(args->node, args->next->node, outputReg, output, op);
            break;
        case Intrinsic_vec_ceil:
        case Intrinsic_vec_expte:
        case Intrinsic_vec_floor:
        case Intrinsic_vec_loge:
        case Intrinsic_vec_re:
        case Intrinsic_vec_round:
        case Intrinsic_vec_rsqrte:
        case Intrinsic_vec_trunc:
        case Intrinsic_vec_unpackh:
        case Intrinsic_vec_unpackl:
        case Intrinsic_vec_vrfip:
        case Intrinsic_vec_vexptefp:
        case Intrinsic_vec_vrfim:
        case Intrinsic_vec_vlogefp:
        case Intrinsic_vec_vrefp:
        case Intrinsic_vec_vrfin:
        case Intrinsic_vec_vrsqrtefp:
        case Intrinsic_vec_vrfiz:
        case Intrinsic_vec_vupkhsb:
        case Intrinsic_vec_vupklsb:
        case Intrinsic_vec_vupkhpx:
        case Intrinsic_vec_vupklpx:
        case Intrinsic_vec_vupkhsh:
        case Intrinsic_vec_vupklsh:
            op = Intrinsics_FindOpcode1Arg(id, funccall, args->node);
            vector_intrinsic_1arg(args->node, outputReg, output, op);
            break;
        case Intrinsic_vec_ctf:
        case Intrinsic_vec_cts:
        case Intrinsic_vec_ctu:
        case Intrinsic_vec_splat:
        case Intrinsic_vec_vcfux:
        case Intrinsic_vec_vcfsx:
        case Intrinsic_vec_vctsxs:
        case Intrinsic_vec_vctuxs:
        case Intrinsic_vec_vspltb:
        case Intrinsic_vec_vsplth:
        case Intrinsic_vec_vspltw:
            op = Intrinsics_FindOpcode2Args(id, funccall, args->node, args->next->node);
            vector_intrinsic_2args1const(args->node, args->next->node, outputReg, output, op);
            break;
        case Intrinsic_vec_splat_s8:
        case Intrinsic_vec_splat_s16:
        case Intrinsic_vec_splat_s32:
        case Intrinsic_vec_vspltisb:
        case Intrinsic_vec_vspltish:
        case Intrinsic_vec_vspltisw:
            op = Intrinsics_FindOpcode1Arg(id, funccall, args->node);
            vector_intrinsic_splats(args->node, outputReg, output, op);
            break;
        case Intrinsic_vec_splat_u8:
            op = Intrinsics_FindOpcode1Arg(id, funccall, args->node);
            vector_intrinsic_splatu8(args->node, outputReg, output, op);
            break;
        case Intrinsic_vec_splat_u16:
            op = Intrinsics_FindOpcode1Arg(id, funccall, args->node);
            vector_intrinsic_splatu16(args->node, outputReg, output, op);
            break;
        case Intrinsic_vec_splat_u32:
            op = Intrinsics_FindOpcode1Arg(id, funccall, args->node);
            vector_intrinsic_splatu32(args->node, outputReg, output, op);
            break;
        case Intrinsic_vec_mtvscr:
            vector_intrinsic_mtvscr(args->node, output, PC_MTVSCR);
            break;
        case Intrinsic_vec_mfvscr:
            op = Intrinsics_FindOpcodeNoArgs(id, funccall);
            vector_intrinsic_mfvscr(outputReg, op, output);
            break;
        case Intrinsic_vec_dss:
            vector_intrinsic_dss(args->node);
            break;
        case Intrinsic_vec_dssall:
            op = Intrinsics_FindOpcodeNoArgs(id, funccall);
            vector_intrinsic_no_args(op);
            break;
        case Intrinsic_vec_dst:
        case Intrinsic_vec_dstst:
        case Intrinsic_vec_dststt:
        case Intrinsic_vec_dstt:
            switch (id) {
                case Intrinsic_vec_dst:
                    op = PC_DST;
                    break;
                case Intrinsic_vec_dstst:
                    op = PC_DSTST;
                    break;
                case Intrinsic_vec_dststt:
                    op = PC_DSTSTT;
                    break;
                case Intrinsic_vec_dstt:
                    op = PC_DSTT;
                    break;
                default:
                    break;
            }
            vector_intrinsic_datastream(args->node, args->next->node, args->next->next->node, op);
            break;
        case Intrinsic_vec_ld:
        case Intrinsic_vec_lde:
        case Intrinsic_vec_ldl:
        case Intrinsic_vec_lvsl:
        case Intrinsic_vec_lvsr:
        case Intrinsic_vec_lvx:
        case Intrinsic_vec_lvebx:
        case Intrinsic_vec_lvehx:
        case Intrinsic_vec_lvewx:
        case Intrinsic_vec_lvxl:
            op = Intrinsics_FindOpcode2Args(id, funccall, args->node, args->next->node);
            vector_intrinsic_load(args->node, args->next->node, outputReg, output, op);
            break;
        case Intrinsic_vec_st:
        case Intrinsic_vec_ste:
        case Intrinsic_vec_stl:
        case Intrinsic_vec_stvx:
        case Intrinsic_vec_stvebx:
        case Intrinsic_vec_stvehx:
        case Intrinsic_vec_stvewx:
        case Intrinsic_vec_stvxl:
            op = Intrinsics_FindOpcode3Args(
                id, funccall, args->node, args->next->node, args->next->next->node
            );
            vector_intrinsic_store(
                args->node, args->next->node, args->next->next->node, outputReg, output, op
            );
            break;
        case Intrinsic_vec_all_eq:
        case Intrinsic_vec_all_ge:
        case Intrinsic_vec_all_gt:
        case Intrinsic_vec_all_in:
        case Intrinsic_vec_all_le:
        case Intrinsic_vec_all_lt:
        case Intrinsic_vec_all_ne:
        case Intrinsic_vec_all_nge:
        case Intrinsic_vec_all_ngt:
        case Intrinsic_vec_all_nle:
        case Intrinsic_vec_all_nlt:
        case Intrinsic_vec_any_eq:
        case Intrinsic_vec_any_ge:
        case Intrinsic_vec_any_gt:
        case Intrinsic_vec_any_le:
        case Intrinsic_vec_any_lt:
        case Intrinsic_vec_any_ne:
        case Intrinsic_vec_any_nge:
        case Intrinsic_vec_any_ngt:
        case Intrinsic_vec_any_nle:
        case Intrinsic_vec_any_nlt:
        case Intrinsic_vec_any_out:
            op = Intrinsics_FindOpcode2Args(id, funccall, args->node, args->next->node);
            vector_predicate_2args(args->node, args->next->node, outputReg, output, op, id);
            break;
        case Intrinsic_vec_all_nan:
        case Intrinsic_vec_all_numeric:
        case Intrinsic_vec_any_nan:
        case Intrinsic_vec_any_numeric:
            op = Intrinsics_FindOpcode1Arg(id, funccall, args->node);
            vector_predicate_1arg(args->node, outputReg, output, op, id);
            break;
        case Intrinsic_vec_abs:
            vector_intrinsic_abs(id, funccall, args->node, outputReg, output);
            break;
        case Intrinsic_vec_abss:
            vector_intrinsic_abss(id, funccall, args->node, outputReg, output);
            break;
        case Intrinsic_vec_cmple:
        case Intrinsic_vec_cmplt:
            op = Intrinsics_FindOpcode2Args(id, funccall, args->node, args->next->node);
            vector_intrinsic_2args(args->next->node, args->node, outputReg, output, op);
            break;
        case Intrinsic_rtos_change_stack:
            rtos_change_stack_intrinsic(args->node);
            break;
        case Intrinsic_rtos_pop_stack:
            emitpcode(PC_LWZ, 1, 1, 0, 0);
            setpcodeflags(fSideEffects);
            break;
        case Intrinsic_va_setup:
        case Intrinsic_builtin_va_info:
            va_info_intrinsic(opcode[id], args->node, outputReg, output);
            break;
        default:
            CError_FATAL(6190);
    }
}

void Intrinsics_SetupRuntimeObjects(void) {
    static TypePointer char_ptr = {TYPEPOINTER, 4, TYPE(&stchar), 0};
    Boolean savecpp;
    int i;

    savecpp = copts.cplusplus;
    copts.cplusplus = FALSE;
    for (i = 0; i < MaxIntrinsics; i++) {
        intrinsics[i] = NULL;
    }
    // clang-format off
    intrinsics[Intrinsic_eieio] = CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("__eieio"), 0, 0);
    intrinsics[Intrinsic_sync] = CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("__sync"), 0, 0);
    intrinsics[Intrinsic_isync] = CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("__isync"), 0, 0);
    intrinsics[Intrinsic_abs] = CParser_NewRTFunc(TYPE(&stsignedint), GetHashNameNodeExport("__abs"), 0, 1, &stsignedint);
    intrinsics[Intrinsic_labs] = CParser_NewRTFunc(TYPE(&stsignedlong), GetHashNameNodeExport("__labs"), 0, 1, &stsignedlong);
    intrinsics[Intrinsic_fabs] = CParser_NewRTFunc(TYPE(&stdouble), GetHashNameNodeExport("__fabs"), 0, 1, &stdouble);
    intrinsics[Intrinsic_fnabs] = CParser_NewRTFunc(TYPE(&stdouble), GetHashNameNodeExport("__fnabs"), 0, 1, &stdouble);
    intrinsics[Intrinsic_setflm] = CParser_NewRTFunc(TYPE(&stdouble), GetHashNameNodeExport("__setflm"), 0, 1, &stdouble);
    intrinsics[Intrinsic_frsqrte] = CParser_NewRTFunc(TYPE(&stdouble), GetHashNameNodeExport("__frsqrte"), 0, 1, &stdouble);
    intrinsics[Intrinsic_alloca] = CParser_NewRTFunc(TYPE(&void_ptr), GetHashNameNodeExport("__alloca"), 0, 1, &stunsignedint);
    intrinsics[Intrinsic_cntlzw] = CParser_NewRTFunc(TYPE(&stsignedint), GetHashNameNodeExport("__cntlzw"), 0, 1, &stunsignedint);
    intrinsics[Intrinsic_lhbrx] =
        CParser_NewRTFunc(TYPE(&stunsignedint), GetHashNameNodeExport("__lhbrx"), 0, 2, &void_ptr, &stsignedint);
    intrinsics[Intrinsic_lwbrx] =
        CParser_NewRTFunc(TYPE(&stunsignedint), GetHashNameNodeExport("__lwbrx"), 0, 2, &void_ptr, &stsignedint);
    intrinsics[Intrinsic_sthbrx] =
        CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("__sthbrx"), 0, 3, &stunsignedshort, &void_ptr, &stsignedint);
    intrinsics[Intrinsic_stwbrx] =
        CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("__stwbrx"), 0, 3, &stunsignedint, &void_ptr, &stsignedint);
    intrinsics[Intrinsic_dcbf] = CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("__dcbf"), 0, 2, &void_ptr, &stsignedint);
    intrinsics[Intrinsic_dcbt] = CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("__dcbt"), 0, 2, &void_ptr, &stsignedint);
    intrinsics[Intrinsic_dcbst] = CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("__dcbst"), 0, 2, &void_ptr, &stsignedint);
    intrinsics[Intrinsic_dcbtst] = CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("__dcbtst"), 0, 2, &void_ptr, &stsignedint);
    intrinsics[Intrinsic_dcbz] = CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("__dcbz"), 0, 2, &void_ptr, &stsignedint);
    intrinsics[Intrinsic_mulhw] =
        CParser_NewRTFunc(TYPE(&stsignedint), GetHashNameNodeExport("__mulhw"), 0, 2, &stsignedint, &stsignedint);
    intrinsics[Intrinsic_mulhwu] =
        CParser_NewRTFunc(TYPE(&stunsignedint), GetHashNameNodeExport("__mulhwu"), 0, 2, &stunsignedint, &stunsignedint);
    intrinsics[Intrinsic_divw] =
        CParser_NewRTFunc(TYPE(&stsignedint), GetHashNameNodeExport("__divw"), 0, 2, &stsignedint, &stsignedint);
    intrinsics[Intrinsic_divwu] =
        CParser_NewRTFunc(TYPE(&stsignedint), GetHashNameNodeExport("__divwu"), 0, 2, &stsignedint, &stsignedint);
    intrinsics[Intrinsic_fmadd] =
        CParser_NewRTFunc(TYPE(&stdouble), GetHashNameNodeExport("__fmadd"), 0, 3, &stdouble, &stdouble, &stdouble);
    intrinsics[Intrinsic_fmsub] =
        CParser_NewRTFunc(TYPE(&stdouble), GetHashNameNodeExport("__fmsub"), 0, 3, &stdouble, &stdouble, &stdouble);
    intrinsics[Intrinsic_fnmadd] =
        CParser_NewRTFunc(TYPE(&stdouble), GetHashNameNodeExport("__fnmadd"), 0, 3, &stdouble, &stdouble, &stdouble);
    intrinsics[Intrinsic_fnmsub] =
        CParser_NewRTFunc(TYPE(&stdouble), GetHashNameNodeExport("__fnmsub"), 0, 3, &stdouble, &stdouble, &stdouble);
    intrinsics[Intrinsic_fsel] =
        CParser_NewRTFunc(TYPE(&stdouble), GetHashNameNodeExport("__fsel"), 0, 3, &stdouble, &stdouble, &stdouble);
    intrinsics[Intrinsic_fmadds] =
        CParser_NewRTFunc(TYPE(&stfloat), GetHashNameNodeExport("__fmadds"), 0, 3, &stfloat, &stfloat, &stfloat);
    intrinsics[Intrinsic_fmsubs] =
        CParser_NewRTFunc(TYPE(&stfloat), GetHashNameNodeExport("__fmsubs"), 0, 3, &stfloat, &stfloat, &stfloat);
    intrinsics[Intrinsic_fnmadds] =
        CParser_NewRTFunc(TYPE(&stfloat), GetHashNameNodeExport("__fnmadds"), 0, 3, &stfloat, &stfloat, &stfloat);
    intrinsics[Intrinsic_fnmsubs] =
        CParser_NewRTFunc(TYPE(&stfloat), GetHashNameNodeExport("__fnmsubs"), 0, 3, &stfloat, &stfloat, &stfloat);
    intrinsics[Intrinsic_mffs] = CParser_NewRTFunc(TYPE(&stdouble), GetHashNameNodeExport("__mffs"), 0, 0);
    intrinsics[Intrinsic_fres] = CParser_NewRTFunc(TYPE(&stfloat), GetHashNameNodeExport("__fres"), 0, 1, &stfloat);
    intrinsics[Intrinsic_fabsf] = CParser_NewRTFunc(TYPE(&stfloat), GetHashNameNodeExport("__fabsf"), 0, 1, &stfloat);
    intrinsics[Intrinsic_fnabsf] = CParser_NewRTFunc(TYPE(&stfloat), GetHashNameNodeExport("__fnabsf"), 0, 1, &stfloat);
    intrinsics[Intrinsic_strcpy] = CParser_NewRTFunc(TYPE(&char_ptr), GetHashNameNodeExport("__strcpy"), 0, 2, &char_ptr, &char_ptr);
    TYPE_FUNC(intrinsics[Intrinsic_strcpy]->type)->args->next->qual |= Q_CONST;
    intrinsics[Intrinsic_memcpy] =
        CParser_NewRTFunc(TYPE(&void_ptr), GetHashNameNodeExport("__memcpy"), 0, 3, &void_ptr, &void_ptr, &stunsignedlong);
    __memcpy_object = intrinsics[Intrinsic_memcpy];
    TYPE_FUNC(intrinsics[Intrinsic_memcpy]->type)->args->next->qual |= Q_CONST;
    intrinsics[Intrinsic_rlwimi] = CParser_NewRTFunc(TYPE(&stsignedint), GetHashNameNodeExport("__rlwimi"), 0, 5, &stsignedint,
                                       &stsignedint, &stsignedint, &stsignedint, &stsignedint);
    intrinsics[Intrinsic_rlwinm] = CParser_NewRTFunc(TYPE(&stsignedint), GetHashNameNodeExport("__rlwinm"), 0, 4, &stsignedint,
                                       &stsignedint, &stsignedint, &stsignedint);
    intrinsics[Intrinsic_rlwnm] = CParser_NewRTFunc(TYPE(&stsignedint), GetHashNameNodeExport("__rlwnm"), 0, 4, &stsignedint,
                                       &stsignedint, &stsignedint, &stsignedint);
    intrinsics[Intrinsic_vec_add] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_add"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_addc] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_addc"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_adds] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_adds"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_and] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_and"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_andc] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_andc"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_avg] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_avg"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_ceil] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_ceil"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_cmpb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_cmpb"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_cmpeq] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_cmpeq"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_cmpge] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_cmpge"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_cmple] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_cmple"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_cmpgt] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_cmpgt"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_cmplt] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_cmplt"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_ctf] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_ctf"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_cts] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_cts"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_ctu] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_ctu"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_dss] = CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("vec_dss"), 0, 1, &stsignedint);
    intrinsics[Intrinsic_vec_dssall] = CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("vec_dssall"), 0, 0);
    intrinsics[Intrinsic_vec_dst] =
        CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("vec_dst"), 0, 3, TYPE(&stvector), &stsignedint, &stsignedint);
    intrinsics[Intrinsic_vec_dstst] =
        CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("vec_dstst"), 0, 3, TYPE(&stvector), &stsignedint, &stsignedint);
    intrinsics[Intrinsic_vec_dststt] =
        CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("vec_dststt"), 0, 3, TYPE(&stvector), &stsignedint, &stsignedint);
    intrinsics[Intrinsic_vec_dstt] =
        CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("vec_dstt"), 0, 3, TYPE(&stvector), &stsignedint, &stsignedint);
    intrinsics[Intrinsic_vec_expte] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_expte"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_floor] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_floor"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_ld] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_ld"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_lde] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_lde"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_ldl] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_ldl"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_loge] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_loge"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_lvsl] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_lvsl"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_lvsr] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_lvsr"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_madd] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_madd"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_madds] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_madds"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_max] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_max"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_mergeh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_mergeh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_mergel] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_mergel"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_mfvscr] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_mfvscr"), 0, 0);
    intrinsics[Intrinsic_vec_min] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_min"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_mladd] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_mladd"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_mradds] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_mradds"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_msum] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_msum"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_msums] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_msums"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_mtvscr] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_mtvscr"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_mule] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_mule"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_mulo] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_mulo"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_nmsub] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_nmsub"), 0, 2, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_nor] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_nor"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_or] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_or"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_pack] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_pack"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_packpx] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_packpx"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_packs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_packs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_packsu] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_packsu"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_perm] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_perm"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_re] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_re"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_rl] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_rl"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_round] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_round"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_rsqrte] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_rsqrte"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_sel] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_sel"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_sl] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_sl"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_sld] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_sld"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_sll] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_sll"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_slo] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_slo"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_splat] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_splat"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_splat_s8] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_splat_s8"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_splat_s16] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_splat_s16"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_splat_s32] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_splat_s32"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_splat_u8] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_splat_u8"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_splat_u16] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_splat_u16"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_splat_u32] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_splat_u32"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_sr] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_sr"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_sra] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_sra"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_srl] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_srl"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_sro] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_sro"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_st] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_st"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_ste] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_ste"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_stl] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_stl"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_sub] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_sub"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_subc] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_subc"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_subs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_subs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_sum4s] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_sum4s"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_sum2s] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_sum2s"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_sums] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_sums"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_trunc] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_trunc"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_unpack2sh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_unpack2sh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_unpack2sl] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_unpack2sl"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_unpack2uh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_unpack2uh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_unpack2ul] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_unpack2ul"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_unpackh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_unpackh"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_unpackl] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_unpackl"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_xor] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_xor"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_all_eq] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_all_eq"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_all_ge] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_all_ge"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_all_gt] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_all_gt"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_all_in] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_all_in"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_all_le] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_all_le"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_all_lt] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_all_lt"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_all_nan] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_all_nan"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_all_ne] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_all_ne"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_all_nge] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_all_nge"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_all_ngt] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_all_ngt"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_all_nle] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_all_nle"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_all_nlt] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_all_nlt"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_all_numeric] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_all_numeric"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_any_eq] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_any_eq"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_any_ge] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_any_ge"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_any_gt] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_any_gt"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_any_le] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_any_le"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_any_lt] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_any_lt"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_any_nan] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_any_nan"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_any_ne] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_any_ne"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_any_nge] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_any_nge"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_any_ngt] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_any_ngt"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_any_nle] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_any_nle"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_any_nlt] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_any_nlt"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_any_numeric] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_any_numeric"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_any_out] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_any_out"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vaddubm] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vaddubm"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vadduhm] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vadduhm"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vadduwm] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vadduwm"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vaddfp] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vaddfp"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vaddcuw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vaddcuw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vaddubs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vaddubs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vaddsbs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vaddsbs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vadduhs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vadduhs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vaddshs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vaddshs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vadduws] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vadduws"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vaddsws] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vaddsws"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vand] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vand"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vandc] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vandc"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vavgub] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vavgub"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vavgsb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vavgsb"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vavguh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vavguh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vavgsh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vavgsh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vavguw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vavguw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vavgsw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vavgsw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vrfip] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vrfip"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcmpbfp] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcmpbfp"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcmpequb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcmpequb"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcmpequh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcmpequh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcmpequw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcmpequw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcmpeqfp] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcmpeqfp"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcmpgefp] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcmpgefp"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcmpgtub] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcmpgtub"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcmpgtsb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcmpgtsb"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcmpgtuh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcmpgtuh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcmpgtsh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcmpgtsh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcmpgtuw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcmpgtuw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcmpgtsw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcmpgtsw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcmpgtfp] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcmpgtfp"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcfux] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcfux"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vcfsx] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vcfsx"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vctsxs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vctsxs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vctuxs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vctuxs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vexptefp] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vexptefp"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vrfim] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vrfim"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_lvx] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_lvx"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_lvebx] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_lvebx"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_lvehx] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_lvehx"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_lvewx] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_lvewx"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_lvxl] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_lvxl"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vlogefp] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vlogefp"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmaddfp] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmaddfp"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmhaddshs] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmhaddshs"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmaxub] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmaxub"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmaxsb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmaxsb"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmaxuh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmaxuh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmaxsh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmaxsh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmaxuw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmaxuw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmaxsw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmaxsw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmaxfp] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmaxfp"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmrghb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmrghb"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmrghh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmrghh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmrghw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmrghw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmrglb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmrglb"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmrglh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmrglh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmrglw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmrglw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vminub] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vminub"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vminsb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vminsb"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vminuh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vminuh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vminsh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vminsh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vminuw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vminuw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vminsw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vminsw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vminfp] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vminfp"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmladduhm] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmladduhm"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmhraddshs] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmhraddshs"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmsumubm] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmsumubm"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmsumuhm] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmsumuhm"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmsummbm] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmsummbm"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmsumshm] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmsumshm"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmsumuhs] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmsumuhs"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmsumshs] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmsumshs"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmuleub] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmuleub"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmulesb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmulesb"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmuleuh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmuleuh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmulesh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmulesh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmuloub] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmuloub"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmulosb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmulosb"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmulouh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmulouh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vmulosh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vmulosh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vnmsubfp] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vnmsubfp"), 0, 2, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vnor] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vnor"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vor] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vor"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vpkuhum] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vpkuhum"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vpkuwum] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vpkuwum"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vpkpx] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vpkpx"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vpkuhus] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vpkuhus"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vpkshss] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vpkshss"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vpkuwus] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vpkuwus"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vpkswss] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vpkswss"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vpkshus] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vpkshus"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vpkswus] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vpkswus"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vperm] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vperm"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vrefp] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vrefp"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vrlb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vrlb"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vrlh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vrlh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vrlw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vrlw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vrfin] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vrfin"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vrsqrtefp] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vrsqrtefp"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsel] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsel"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vslb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vslb"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vslh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vslh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vslw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vslw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsldoi] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsldoi"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsl] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsl"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vslo] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vslo"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vspltb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vspltb"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsplth] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsplth"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vspltw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vspltw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vspltisb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vspltisb"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vspltish] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vspltish"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vspltisw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vspltisw"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsrb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsrb"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsrh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsrh"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsrw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsrw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsrab] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsrab"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsrah] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsrah"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsraw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsraw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsr] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsr"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsro] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsro"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_stvx] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_stvx"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_stvebx] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_stvebx"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_stvehx] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_stvehx"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_stvewx] =
        CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_stvewx"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_stvxl] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_stvxl"), 0, 3, TYPE(&stvector), TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsububm] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsububm"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsubuhm] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsubuhm"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsubuwm] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsubuwm"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsubfp] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsubfp"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsubcuw] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsubcuw"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsububs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsububs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsubsbs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsubsbs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsubuhs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsubuhs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsubshs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsubshs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsubuws] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsubuws"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsubsws] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsubsws"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsum4ubs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsum4ubs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsum4sbs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsum4sbs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsum4shs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsum4shs"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsum2sws] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsum2sws"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vsumsws] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vsumsws"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_vrfiz] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vrfiz"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vupkhsb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vupkhsb"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vupklsb] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vupklsb"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vupkhpx] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vupkhpx"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vupklpx] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vupklpx"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vupkhsh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vupkhsh"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vupklsh] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vupklsh"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_vxor] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_vxor"), 0, 2, TYPE(&stvector), TYPE(&stvector));
    intrinsics[Intrinsic_vec_abs] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_abs"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_vec_abss] = CParser_NewRTFunc(TYPE(&stvector), GetHashNameNodeExport("vec_abss"), 0, 1, TYPE(&stvector));
    intrinsics[Intrinsic_rtos_change_stack] = CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("__rtos_change_stack"), 0, 1, &void_ptr);
    intrinsics[Intrinsic_rtos_pop_stack] = CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("__rtos_pop_stack"), 0, 0);
    intrinsics[Intrinsic_va_setup] = CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("__va_setup"), 0, 1, &void_ptr);
    intrinsics[Intrinsic_builtin_va_info] = CParser_NewRTFunc(&stvoid, GetHashNameNodeExport("__builtin_va_info"), 0, 1, &void_ptr);
    // clang-format on
    for (i = 0; i < MaxIntrinsics; i++) {
        CError_ASSERT(6644, intrinsics[i]);
        intrinsics[i]->u.func.u.intrinsicid = i;
        TYPE_FUNC(intrinsics[i]->type)->flags |= FUNC_INTRINSIC;
        CScope_AddGlobalObject(intrinsics[i]);
    }
    copts.cplusplus = savecpp;
}

static Object* CheckRuntimeObject(char* name) {
    NameSpaceName* nsname = CScope_FindNameSpaceName(cscope_root, GetHashNameNodeExport(name));

    if (!nsname) {
        return NULL;
    }

    if (nsname->first.object && nsname->first.next == NULL) {
        return OBJECT(nsname->first.object);
    }

    return NULL;
}

Boolean Intrinsics_ReInitRuntimeObjects(Boolean isPrecompiler) {
    if (isPrecompiler) {
        if (!(intrinsics[Intrinsic_eieio] = CheckRuntimeObject("__eieio"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_sync] = CheckRuntimeObject("__sync"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_isync] = CheckRuntimeObject("__isync"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_abs] = CheckRuntimeObject("__abs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_labs] = CheckRuntimeObject("__labs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_fabs] = CheckRuntimeObject("__fabs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_fnabs] = CheckRuntimeObject("__fnabs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_setflm] = CheckRuntimeObject("__setflm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_frsqrte] = CheckRuntimeObject("__frsqrte"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_alloca] = CheckRuntimeObject("__alloca"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_cntlzw] = CheckRuntimeObject("__cntlzw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_lhbrx] = CheckRuntimeObject("__lhbrx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_lwbrx] = CheckRuntimeObject("__lwbrx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_sthbrx] = CheckRuntimeObject("__sthbrx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_stwbrx] = CheckRuntimeObject("__stwbrx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_dcbf] = CheckRuntimeObject("__dcbf"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_dcbt] = CheckRuntimeObject("__dcbt"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_dcbst] = CheckRuntimeObject("__dcbst"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_dcbtst] = CheckRuntimeObject("__dcbtst"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_dcbz] = CheckRuntimeObject("__dcbz"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_mulhw] = CheckRuntimeObject("__mulhw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_mulhwu] = CheckRuntimeObject("__mulhwu"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_divw] = CheckRuntimeObject("__divw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_divwu] = CheckRuntimeObject("__divwu"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_fmadd] = CheckRuntimeObject("__fmadd"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_fmsub] = CheckRuntimeObject("__fmsub"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_fnmadd] = CheckRuntimeObject("__fnmadd"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_fnmsub] = CheckRuntimeObject("__fnmsub"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_fsel] = CheckRuntimeObject("__fsel"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_fmadds] = CheckRuntimeObject("__fmadds"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_fmsubs] = CheckRuntimeObject("__fmsubs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_fnmadds] = CheckRuntimeObject("__fnmadds"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_fnmsubs] = CheckRuntimeObject("__fnmsubs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_mffs] = CheckRuntimeObject("__mffs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_fres] = CheckRuntimeObject("__fres"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_fabsf] = CheckRuntimeObject("__fabsf"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_fnabsf] = CheckRuntimeObject("__fnabsf"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_strcpy] = CheckRuntimeObject("__strcpy"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_rlwimi] = CheckRuntimeObject("__rlwimi"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_rlwinm] = CheckRuntimeObject("__rlwinm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_rlwnm] = CheckRuntimeObject("__rlwnm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_add] = CheckRuntimeObject("vec_add"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_addc] = CheckRuntimeObject("vec_addc"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_adds] = CheckRuntimeObject("vec_adds"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_and] = CheckRuntimeObject("vec_and"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_andc] = CheckRuntimeObject("vec_andc"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_avg] = CheckRuntimeObject("vec_avg"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_ceil] = CheckRuntimeObject("vec_ceil"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_cmpb] = CheckRuntimeObject("vec_cmpb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_cmpeq] = CheckRuntimeObject("vec_cmpeq"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_cmpge] = CheckRuntimeObject("vec_cmpge"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_cmple] = CheckRuntimeObject("vec_cmple"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_cmpgt] = CheckRuntimeObject("vec_cmpgt"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_cmplt] = CheckRuntimeObject("vec_cmplt"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_ctf] = CheckRuntimeObject("vec_ctf"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_cts] = CheckRuntimeObject("vec_cts"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_ctu] = CheckRuntimeObject("vec_ctu"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_expte] = CheckRuntimeObject("vec_expte"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_floor] = CheckRuntimeObject("vec_floor"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_ld] = CheckRuntimeObject("vec_ld"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_lde] = CheckRuntimeObject("vec_lde"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_ldl] = CheckRuntimeObject("vec_ldl"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_loge] = CheckRuntimeObject("vec_loge"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_lvsl] = CheckRuntimeObject("vec_lvsl"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_lvsr] = CheckRuntimeObject("vec_lvsr"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_madd] = CheckRuntimeObject("vec_madd"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_madds] = CheckRuntimeObject("vec_madds"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_max] = CheckRuntimeObject("vec_max"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_mergeh] = CheckRuntimeObject("vec_mergeh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_mergel] = CheckRuntimeObject("vec_mergel"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_mfvscr] = CheckRuntimeObject("vec_mfvscr"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_min] = CheckRuntimeObject("vec_min"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_mladd] = CheckRuntimeObject("vec_mladd"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_mradds] = CheckRuntimeObject("vec_mradds"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_msum] = CheckRuntimeObject("vec_msum"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_msums] = CheckRuntimeObject("vec_msums"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_mtvscr] = CheckRuntimeObject("vec_mtvscr"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_mule] = CheckRuntimeObject("vec_mule"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_mulo] = CheckRuntimeObject("vec_mulo"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_nmsub] = CheckRuntimeObject("vec_nmsub"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_nor] = CheckRuntimeObject("vec_nor"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_or] = CheckRuntimeObject("vec_or"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_pack] = CheckRuntimeObject("vec_pack"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_packpx] = CheckRuntimeObject("vec_packpx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_packs] = CheckRuntimeObject("vec_packs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_packsu] = CheckRuntimeObject("vec_packsu"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_perm] = CheckRuntimeObject("vec_perm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_re] = CheckRuntimeObject("vec_re"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_rl] = CheckRuntimeObject("vec_rl"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_round] = CheckRuntimeObject("vec_round"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_rsqrte] = CheckRuntimeObject("vec_rsqrte"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_sel] = CheckRuntimeObject("vec_sel"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_sl] = CheckRuntimeObject("vec_sl"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_sld] = CheckRuntimeObject("vec_sld"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_sll] = CheckRuntimeObject("vec_sll"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_slo] = CheckRuntimeObject("vec_slo"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_splat] = CheckRuntimeObject("vec_splat"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_splat_s8] = CheckRuntimeObject("vec_splat_s8"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_splat_s16] = CheckRuntimeObject("vec_splat_s16"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_splat_s32] = CheckRuntimeObject("vec_splat_s32"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_splat_u8] = CheckRuntimeObject("vec_splat_u8"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_splat_u16] = CheckRuntimeObject("vec_splat_u16"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_splat_u32] = CheckRuntimeObject("vec_splat_u32"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_sr] = CheckRuntimeObject("vec_sr"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_sra] = CheckRuntimeObject("vec_sra"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_srl] = CheckRuntimeObject("vec_srl"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_sro] = CheckRuntimeObject("vec_sro"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_st] = CheckRuntimeObject("vec_st"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_ste] = CheckRuntimeObject("vec_ste"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_stl] = CheckRuntimeObject("vec_stl"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_sub] = CheckRuntimeObject("vec_sub"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_subc] = CheckRuntimeObject("vec_subc"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_subs] = CheckRuntimeObject("vec_subs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_sum4s] = CheckRuntimeObject("vec_sum4s"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_sum2s] = CheckRuntimeObject("vec_sum2s"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_sums] = CheckRuntimeObject("vec_sums"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_trunc] = CheckRuntimeObject("vec_trunc"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_unpack2sh] = CheckRuntimeObject("vec_unpack2sh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_unpack2sl] = CheckRuntimeObject("vec_unpack2sl"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_unpack2uh] = CheckRuntimeObject("vec_unpack2uh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_unpack2ul] = CheckRuntimeObject("vec_unpack2ul"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_unpackh] = CheckRuntimeObject("vec_unpackh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_unpackl] = CheckRuntimeObject("vec_unpackl"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_xor] = CheckRuntimeObject("vec_xor"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_all_eq] = CheckRuntimeObject("vec_all_eq"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_all_ge] = CheckRuntimeObject("vec_all_ge"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_all_gt] = CheckRuntimeObject("vec_all_gt"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_all_in] = CheckRuntimeObject("vec_all_in"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_all_le] = CheckRuntimeObject("vec_all_le"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_all_lt] = CheckRuntimeObject("vec_all_lt"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_all_nan] = CheckRuntimeObject("vec_all_nan"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_all_ne] = CheckRuntimeObject("vec_all_ne"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_all_nge] = CheckRuntimeObject("vec_all_nge"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_all_ngt] = CheckRuntimeObject("vec_all_ngt"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_all_nle] = CheckRuntimeObject("vec_all_nle"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_all_nlt] = CheckRuntimeObject("vec_all_nlt"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_all_numeric] = CheckRuntimeObject("vec_all_numeric"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_any_eq] = CheckRuntimeObject("vec_any_eq"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_any_ge] = CheckRuntimeObject("vec_any_ge"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_any_gt] = CheckRuntimeObject("vec_any_gt"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_any_le] = CheckRuntimeObject("vec_any_le"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_any_lt] = CheckRuntimeObject("vec_any_lt"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_any_nan] = CheckRuntimeObject("vec_any_nan"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_any_ne] = CheckRuntimeObject("vec_any_ne"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_any_nge] = CheckRuntimeObject("vec_any_nge"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_any_ngt] = CheckRuntimeObject("vec_any_ngt"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_any_nle] = CheckRuntimeObject("vec_any_nle"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_any_nlt] = CheckRuntimeObject("vec_any_nlt"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_any_numeric] = CheckRuntimeObject("vec_any_numeric"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_any_out] = CheckRuntimeObject("vec_any_out"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vaddubm] = CheckRuntimeObject("vec_vaddubm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vadduhm] = CheckRuntimeObject("vec_vadduhm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vadduwm] = CheckRuntimeObject("vec_vadduwm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vaddfp] = CheckRuntimeObject("vec_vaddfp"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vaddcuw] = CheckRuntimeObject("vec_vaddcuw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vaddubs] = CheckRuntimeObject("vec_vaddubs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vaddsbs] = CheckRuntimeObject("vec_vaddubs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vadduhs] = CheckRuntimeObject("vec_vadduhs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vaddshs] = CheckRuntimeObject("vec_vadduhs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vadduws] = CheckRuntimeObject("vec_vadduws"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vaddsws] = CheckRuntimeObject("vec_vadduws"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vand] = CheckRuntimeObject("vec_vand"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vandc] = CheckRuntimeObject("vec_vandc"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vavgub] = CheckRuntimeObject("vec_vavgub"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vavgsb] = CheckRuntimeObject("vec_vavgsb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vavguh] = CheckRuntimeObject("vec_vavguh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vavgsh] = CheckRuntimeObject("vec_vavgsh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vavguw] = CheckRuntimeObject("vec_vavguw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vavgsw] = CheckRuntimeObject("vec_vavgsw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vrfip] = CheckRuntimeObject("vec_vrfip"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcmpbfp] = CheckRuntimeObject("vec_vcmpbfp"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcmpequb] = CheckRuntimeObject("vec_vcmpequb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcmpequh] = CheckRuntimeObject("vec_vcmpequh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcmpequw] = CheckRuntimeObject("vec_vcmpequw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcmpeqfp] = CheckRuntimeObject("vec_vcmpeqfp"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcmpgefp] = CheckRuntimeObject("vec_vcmpgefp"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcmpgtub] = CheckRuntimeObject("vec_vcmpgtub"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcmpgtsb] = CheckRuntimeObject("vec_vcmpgtsb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcmpgtuh] = CheckRuntimeObject("vec_vcmpgtuh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcmpgtsh] = CheckRuntimeObject("vec_vcmpgtsh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcmpgtuw] = CheckRuntimeObject("vec_vcmpgtuw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcmpgtsw] = CheckRuntimeObject("vec_vcmpgtsw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcmpgtfp] = CheckRuntimeObject("vec_vcmpgtfp"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcfux] = CheckRuntimeObject("vec_vcfux"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vcfsx] = CheckRuntimeObject("vec_vcfsx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vctsxs] = CheckRuntimeObject("vec_vctsxs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vctuxs] = CheckRuntimeObject("vec_vctuxs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vexptefp] = CheckRuntimeObject("vec_vexptefp"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vrfim] = CheckRuntimeObject("vec_vrfim"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_lvx] = CheckRuntimeObject("vec_lvx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_lvebx] = CheckRuntimeObject("vec_lvebx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_lvehx] = CheckRuntimeObject("vec_lvehx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_lvewx] = CheckRuntimeObject("vec_lvewx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_lvxl] = CheckRuntimeObject("vec_lvxl"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vlogefp] = CheckRuntimeObject("vec_vlogefp"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaddfp] = CheckRuntimeObject("vec_vmaddfp"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmhaddshs] = CheckRuntimeObject("vec_vmhaddshs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaxub] = CheckRuntimeObject("vec_vmaxub"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaxsb] = CheckRuntimeObject("vec_vmaxsb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaxuh] = CheckRuntimeObject("vec_vmaxuh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaxsh] = CheckRuntimeObject("vec_vmaxsh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaxuw] = CheckRuntimeObject("vec_vmaxuw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaxsw] = CheckRuntimeObject("vec_vmaxsw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaxfp] = CheckRuntimeObject("vec_vmaxfp"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmrghb] = CheckRuntimeObject("vec_vmrghb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmrghh] = CheckRuntimeObject("vec_vmrghh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmrghw] = CheckRuntimeObject("vec_vmrghw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmrglb] = CheckRuntimeObject("vec_vmrglb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmrglh] = CheckRuntimeObject("vec_vmrglh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmrglw] = CheckRuntimeObject("vec_vmrglw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaxub] = CheckRuntimeObject("vec_vminub"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaxsb] = CheckRuntimeObject("vec_vminsb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaxuh] = CheckRuntimeObject("vec_vminuh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaxsh] = CheckRuntimeObject("vec_vminsh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaxuw] = CheckRuntimeObject("vec_vminuw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaxsw] = CheckRuntimeObject("vec_vminsw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmaxfp] = CheckRuntimeObject("vec_vminfp"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmladduhm] = CheckRuntimeObject("vec_vmladduhm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmhraddshs] = CheckRuntimeObject("vec_vmhraddshs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmsumubm] = CheckRuntimeObject("vec_vmsumubm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmsumuhm] = CheckRuntimeObject("vec_vmsumuhm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmsummbm] = CheckRuntimeObject("vec_vmsummbm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmsumshm] = CheckRuntimeObject("vec_vmsumshm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmsumuhs] = CheckRuntimeObject("vec_vmsumuhs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmsumshs] = CheckRuntimeObject("vec_vmsumshs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmuleub] = CheckRuntimeObject("vec_vmuleub"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmulesb] = CheckRuntimeObject("vec_vmulesb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmuleuh] = CheckRuntimeObject("vec_vmuleuh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmulesh] = CheckRuntimeObject("vec_vmulesh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmuloub] = CheckRuntimeObject("vec_vmuloub"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmulosb] = CheckRuntimeObject("vec_vmulosb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmulouh] = CheckRuntimeObject("vec_vmulouh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vmulosh] = CheckRuntimeObject("vec_vmulosh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vnmsubfp] = CheckRuntimeObject("vec_vnmsubfp"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vnor] = CheckRuntimeObject("vec_vnor"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vor] = CheckRuntimeObject("vec_vor"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vpkuhum] = CheckRuntimeObject("vec_vpkuhum"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vpkuwum] = CheckRuntimeObject("vec_vpkuwum"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vpkpx] = CheckRuntimeObject("vec_vpkpx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vpkuhus] = CheckRuntimeObject("vec_vpkuhus"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vpkshss] = CheckRuntimeObject("vec_vpkshss"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vpkuwus] = CheckRuntimeObject("vec_vpkuwus"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vpkswss] = CheckRuntimeObject("vec_vpkswss"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vpkshus] = CheckRuntimeObject("vec_vpkshus"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vpkswus] = CheckRuntimeObject("vec_vpkswus"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vperm] = CheckRuntimeObject("vec_vperm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vrefp] = CheckRuntimeObject("vec_vrefp"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vrlb] = CheckRuntimeObject("vec_vrlb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vrlh] = CheckRuntimeObject("vec_vrlh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vrlw] = CheckRuntimeObject("vec_vrlw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vrfin] = CheckRuntimeObject("vec_vrfin"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vrsqrtefp] = CheckRuntimeObject("vec_vrsqrtefp"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsel] = CheckRuntimeObject("vec_vsel"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vslb] = CheckRuntimeObject("vec_vslb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vslh] = CheckRuntimeObject("vec_vslh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vslw] = CheckRuntimeObject("vec_vslw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsldoi] = CheckRuntimeObject("vec_vsldoi"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsl] = CheckRuntimeObject("vec_vsl"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vslo] = CheckRuntimeObject("vec_vslo"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vspltb] = CheckRuntimeObject("vec_vspltb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsplth] = CheckRuntimeObject("vec_vsplth"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vspltw] = CheckRuntimeObject("vec_vspltw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vspltisb] = CheckRuntimeObject("vec_vspltisb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vspltish] = CheckRuntimeObject("vec_vspltish"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vspltisw] = CheckRuntimeObject("vec_vspltisw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsrb] = CheckRuntimeObject("vec_vsrb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsrh] = CheckRuntimeObject("vec_vsrh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsrw] = CheckRuntimeObject("vec_vsrw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsrab] = CheckRuntimeObject("vec_vsrab"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsrah] = CheckRuntimeObject("vec_vsrah"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsraw] = CheckRuntimeObject("vec_vsraw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsr] = CheckRuntimeObject("vec_vsr"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsro] = CheckRuntimeObject("vec_vsro"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_stvx] = CheckRuntimeObject("vec_stvx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_stvebx] = CheckRuntimeObject("vec_stvebx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_stvehx] = CheckRuntimeObject("vec_stvehx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_stvewx] = CheckRuntimeObject("vec_stvewx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_stvxl] = CheckRuntimeObject("vec_stvxl"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsububm] = CheckRuntimeObject("vec_vsububm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsubuhm] = CheckRuntimeObject("vec_vsubuhm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsubuwm] = CheckRuntimeObject("vec_vsubuwm"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsubfp] = CheckRuntimeObject("vec_vsubfp"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsubcuw] = CheckRuntimeObject("vec_vsubcuw"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsububs] = CheckRuntimeObject("vec_vsububs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsubsbs] = CheckRuntimeObject("vec_vsubsbs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsubuhs] = CheckRuntimeObject("vec_vsubuhs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsubshs] = CheckRuntimeObject("vec_vsubshs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsubuws] = CheckRuntimeObject("vec_vsubuws"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsubsws] = CheckRuntimeObject("vec_vsubsws"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsum4ubs] = CheckRuntimeObject("vec_vsum4ubs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsum4sbs] = CheckRuntimeObject("vec_vsum4sbs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsum4shs] = CheckRuntimeObject("vec_vsum4shs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsum2sws] = CheckRuntimeObject("vec_vsum2sws"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vsumsws] = CheckRuntimeObject("vec_vsumsws"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vrfiz] = CheckRuntimeObject("vec_vrfiz"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vupkhsb] = CheckRuntimeObject("vec_vupkhsb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vupklsb] = CheckRuntimeObject("vec_vupklsb"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vupkhpx] = CheckRuntimeObject("vec_vupkhpx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vupklpx] = CheckRuntimeObject("vec_vupklpx"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vupkhsh] = CheckRuntimeObject("vec_vupkhsh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vupklsh] = CheckRuntimeObject("vec_vupklsh"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_vxor] = CheckRuntimeObject("vec_vxor"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_abs] = CheckRuntimeObject("vec_abs"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_vec_abss] = CheckRuntimeObject("vec_abss"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_rtos_change_stack] =
                  CheckRuntimeObject("__rtos_change_stack"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_rtos_pop_stack] = CheckRuntimeObject("__rtos_pop_stack"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_va_setup] = CheckRuntimeObject("__va_setup"))) {
            return FALSE;
        }
        if (!(intrinsics[Intrinsic_builtin_va_info] = CheckRuntimeObject("__builtin_va_info"))) {
            return FALSE;
        }
    }
    return TRUE;
}

Boolean Intrinsics_IsPublicRuntimeObject(Object* object) {
    int i;

    for (i = 0; i < MaxIntrinsics; i++) {
        if (object == intrinsics[i]) {
            return TRUE;
        }
    }

    return FALSE;
}

static void va_info_intrinsic(short opcode, ENode* expr, short outputReg, Operand* output) {
    Operand op;
    SInt32 reg;
    union {
        struct {
            UInt8 gprs;
            UInt8 fprs;
        } bytes;
        UInt16 halfword;
    } counts;

    if (!dummyvaparam || !dummyvalocal) {
        PPCError_ErrorTerm(PPCERR_VA_START_NEEDS_ELLIPSIS);
    }

    memclrw(&op, sizeof(Operand));
    GEN_NODE_TO_GPR(expr, &op, expr->rtype, 0);

    reg = ALLOC_GPR();
    counts.bytes.gprs = last_argument_register[RegClass_GPR] - 2;
    counts.bytes.fprs = last_argument_register[RegClass_FPR];
    if (copts.littleendian) {
        emitpcode(PC_LI, reg, CTool_EndianConvertWord16(counts.halfword));
    } else {
        emitpcode(PC_LIS, reg, 0, CTool_EndianConvertWord16(counts.halfword));
    }
    emitpcode(PC_STW, reg, op.reg, 0, 0);

    reg = ALLOC_GPR();
    add_immediate(reg, local_base_register(dummyvaparam), dummyvaparam, 0);
    emitpcode(PC_STW, reg, op.reg, 0, 4);

    reg = ALLOC_GPR();
    add_immediate(reg, local_base_register(dummyvalocal), dummyvalocal, 0);
    emitpcode(PC_STW, reg, op.reg, 0, 8);

    output->optype = OpndType_GPR;
    output->reg = reg;
}
