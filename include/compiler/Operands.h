#ifndef COMPILER_OPERANDS_H
#define COMPILER_OPERANDS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

enum OperandType {
    OpndType_GPR = 0,
    OpndType_GPR_ImmOffset = 1,
    OpndType_GPR_Indexed = 2,
    OpndType_GPRPair = 3,
    OpndType_Absolute = 4,
    OpndType_FPR = 5,
    OpndType_VR = 6,
    OpndType_CRField = 7,
    OpndType_Symbol = 8,
    OpndType_IndirectGPR_ImmOffset = 9,
    OpndType_IndirectGPR_Indexed = 10,
    OpndType_IndirectSymbol = 11,
};

enum {
    OpndFlags_Const = 0x40,
    OpndFlags_Volatile = 0x80
};

struct Operand {
    enum OperandType optype;
    SInt16 reg;
    SInt16 regHi;
    SInt16 regOffset;
    SInt32 immOffset;
    SInt32 immediate;
    struct Object* object;
    UInt32 flags;
};

extern Float one_point_zero;
extern Float int_to_float_cc;
extern Float uns_to_float_cc;

extern void load_immediate(short reg, SInt32 value);
extern void insert_load_immediate_after(short reg, SInt32 value);
extern void set_op_flags(Operand* op, ENode* expr);
extern void symbol_operand(Operand* op, Object* obj);
extern void indirect(Operand* op, ENode* expr);
extern void combine(Operand* opA, Operand* opB, short output_reg, Operand* opOut);
extern void coerce_to_addressable(Operand* op);
extern void Coerce_to_register(Operand* op, Type* type, short output_reg);
extern void coerce_to_register_pair(Operand* op, Type* type, short output_reg, short output_regHi);
extern void Coerce_to_fp_register(Operand* op, Type* type, short output_reg);
extern void Coerce_to_v_register(Operand* op, Type* type, short output_reg);
extern void vectorstore(short reg, Operand* op, Type* type);
extern void store(short reg, Operand* op, Type* type);
extern void store_pair(short reg, short regHi, Operand* op, Type* type);
extern void store_fp(short reg, Operand* op, Type* type);
extern void store_v(short reg, Operand* op, Type* type);
extern void extend32(Operand* op, Type* type, short output_reg);
extern void extend64(Operand* op, Type* type, short output_reg, short output_regHi);
extern void load_floating_constant(short reg, Type* type, Float* data);
extern void convert_integer_to_floating(Operand* op, Boolean is_single, short output_reg);
extern void convert_unsigned_to_floating(Operand* op, Boolean is_single, short output_reg);
extern void convert_floating_to_integer(Operand* op, short output_reg);
extern void convert_floating_to_unsigned(Operand* op, short output_reg);
extern void extract_bitfield(
    Operand* input_op, TypeBitfield* tbitfield, short output_reg, Operand* output_op
);
extern void insert_bitfield(short reg, Operand* op, TypeBitfield* tbitfield);
extern void load_address(short dest_reg, Operand* op);
extern void I8_extract_bitfield(
    Operand* input, TypeBitfield* tbitfield, short output_reg, short output_regHi, Operand* output
);
extern void I8_insert_bitfield(short reg, short regHi, Operand* op, TypeBitfield* tbitfield);

#ifdef __cplusplus
}
#endif

#endif
