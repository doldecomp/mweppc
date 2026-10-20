#ifndef COMPILER_PPCERROR_H
#define COMPILER_PPCERROR_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: invented names: every message below (no source names the messages; from their texts)
// the back end's messages (STR# 10001, from cc-eabi-ppc.r)
enum {
    // "ambiguous use of local variable(%n) and assembler register(%n) name"
    PPCERR_LOCAL_NAMED_LIKE_REGISTER = 100,
    // "ambiguous use of argument(%n) and assembler register(%n) name"
    PPCERR_ARGUMENT_NAMED_LIKE_REGISTER = 101,
    // "all registers are explictly used, can't color virtual '%u' registers"
    PPCERR_ALL_REGISTERS_USED = 102,
    // "parameter count to AltiVec intrinsic '%u' is %i, expected %i"
    PPCERR_ALTIVEC_ARGUMENT_COUNT = 103,
    // "invalid parameter to AltiVec intrinsic '%u', %u( %t ) is not allowed"
    PPCERR_ALTIVEC_INVALID_ARGUMENT = 104,
    // "invalid parameters to AltiVec intrinsic '%u', %u( %t,%t ) is not allowed"
    PPCERR_ALTIVEC_INVALID_ARGUMENTS2 = 105,
    // "invalid parameters to AltiVec intrinsic '%u', %u( %t, %t, %t ) is not allowed"
    PPCERR_ALTIVEC_INVALID_ARGUMENTS3 = 106,
    // "invalid parameter type to AltiVec intrinsic '%u'"
    PPCERR_ALTIVEC_INVALID_ARGUMENT_TYPE = 107,
    // "invalid constant parameter to AltiVec intrinsic '%u',\n'%u' last argument only accepts %i-bit constants"
    PPCERR_ALTIVEC_INVALID_CONSTANT = 108,
    // "label displacement is too far (must be within 32K bytes)"
    PPCERR_LABEL_TOO_FAR = 109,
    // "too few initializers for '%t'"
    PPCERR_TOO_FEW_VECTOR_INITIALIZERS = 110,
    // "too many initializers for '%t'"
    PPCERR_TOO_MANY_VECTOR_INITIALIZERS = 111,
    // "illegal initialization of AltiVec vector data"
    PPCERR_ILLEGAL_VECTOR_INITIALIZATION = 112,
    // "out of range for legal initialization of AltiVec '%t'"
    PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE = 113,
    // "illegal initialization or cast of AltiVec vector data"
    PPCERR_ILLEGAL_VECTOR_INIT_OR_CAST = 114,
    // "use of AltiVec Model requires AltiVec-capable scheduler"
    PPCERR_ALTIVEC_NEEDS_SCHEDULER = 115,
    // "can't allocate AltiVec stack pointer, all registers already allocated (try optimize level 1 and higher)"
    PPCERR_NO_ALTIVEC_STACK_POINTER = 116,
    // "'%u' register not applicable to this processor"
    PPCERR_REGISTER_NOT_ON_PROCESSOR = 117,
    // "only difference expressions are allowed on object\n (%u %u %u is not allowed)"
    PPCERR_ASM_ONLY_OBJECT_DIFFERENCE = 118,
    // "'%u' expressions is not allowed on an object\n(only %u +/- constant expression is allowed)"
    PPCERR_ASM_ONLY_OBJECT_PLUS_CONSTANT = 119,
    // "'%u' operator is not allowed with an object\n(only constant expression + %u is allowed)"
    PPCERR_ASM_ONLY_CONSTANT_PLUS_OBJECT = 120,
    // "only 2 objects are allowed in assembler expressions (%u, %u, %u)"
    PPCERR_ASM_TOO_MANY_OBJECTS = 121,
    // "illegal object reference in constant expression (%u)"
    PPCERR_ASM_OBJECT_IN_CONSTANT = 122,
    // "illegal use of object difference (%u-%u)"
    PPCERR_ASM_ILLEGAL_OBJECT_DIFFERENCE = 123,
    // "illegal can't mix labels and objects in expression (%u-%u)"
    PPCERR_ASM_LABEL_AND_OBJECT = 124,
    // "illegal use of label (%u), can only use label difference in this context"
    PPCERR_ASM_ILLEGAL_LABEL = 125,
    // "illegal expression only one of {ha16, hi16, lo16, @h, @ha, @l} is allowed"
    PPCERR_ASM_MULTIPLE_RELOCATIONS = 126,
    // "illegal use of register pair (%u) use %u@hiword or %u@loword"
    PPCERR_ASM_REGISTER_PAIR = 127,
    // "cannot redefine uninitialized pooled data unless original definition is in common section"
    PPCERR_POOLED_DATA_REDEFINED = 128,
    // "identifier '%n' has already been defined in the '%n' section"
    PPCERR_DEFINED_IN_OTHER_SECTION = 129,
    // "floating point type was checked in %u at line %i"
    PPCERR_FLOAT_CHECKED_AT = 130,
    // "symbol '%n' would generate floating point instructions\n(HW floating point is off)"
    PPCERR_SYMBOL_NEEDS_FLOAT = 131,
    // "processor settings don't support hardware floating point instructions"
    PPCERR_NO_HARDWARE_FLOAT = 132,
    // "invalid operand values: %i must be >= %i"
    PPCERR_OPERAND_TOO_SMALL = 133,
    // "invalid operand values: %i + %i must be <= 32"
    PPCERR_OPERANDS_OVER_32 = 134,
    // "pragma section expected valid %u addressing mode"
    PPCERR_SECTION_EXPECTED_ADDR_MODE = 135,
    // "pragma section expected unquoted %u addressing mode"
    PPCERR_SECTION_EXPECTED_UNQUOTED_ADDR_MODE = 136,
    // "pragma section expected %u"
    PPCERR_SECTION_EXPECTED = 137,
    // "pragma section expected unquoted object type; found \"%u\""
    PPCERR_SECTION_EXPECTED_OBJECT_TYPE = 138,
    // "pragma section expected unquoted identifier for address mode; found \"%u\""
    PPCERR_SECTION_EXPECTED_ADDR_MODE_NAME = 139,
    // "invalid section name '%u'"
    PPCERR_INVALID_SECTION_NAME = 140,
    // "initialized sections must have uniquely named uninitialized data sections;\n'%u' is already defined for another section"
    PPCERR_BSS_SECTION_ALREADY_USED = 141,
    // "'sda_rel' addressing mode can only be used with the PPC EABI defined small data sections"
    PPCERR_SDA_REL_NOT_SMALL_DATA = 142,
    // "only use the initialized section name to refer to the section\n'%u' is the name of an uninitialized data section"
    PPCERR_SECTION_NAME_IS_BSS = 143,
    // "section '%u' already has uninitialized data section '%u'"
    PPCERR_SECTION_HAS_BSS = 144,
    // "sections used for data must have an uninitialized data section"
    PPCERR_DATA_SECTION_NEEDS_BSS = 145,
    // "section '%u' must have an uninitialized data section for object '%n'"
    PPCERR_SECTION_NEEDS_BSS_FOR_OBJECT = 146,
    // "unknown section name '%u'"
    PPCERR_UNKNOWN_SECTION_NAME = 147,
    // "pragma section expected a quoted section name"
    PPCERR_SECTION_EXPECTED_QUOTED_NAME = 148,
    // "'%u' parameter for pragma section is out of order"
    PPCERR_SECTION_PARAMETER_OUT_OF_ORDER = 149,
    // "'%u' parameter for pragma section is repeated"
    PPCERR_SECTION_PARAMETER_REPEATED = 150,
    // "unknown parameter '%u' for pragma section"
    PPCERR_SECTION_UNKNOWN_PARAMETER = 151,
    // "'%u' is only valid for '%u' addressing mode"
    PPCERR_ONLY_FOR_ADDR_MODE = 152,
    // "pragma section expected an object type or access permission and/or a quoted section name"
    PPCERR_SECTION_EXPECTED_TYPE_OR_NAME = 153,
    // "unknown or possibly out of order parameter '%u' for pragma section"
    PPCERR_SECTION_UNKNOWN_OR_MISPLACED_PARAMETER = 154,
    // "pragma section expected a quoted uninitialized section name\nor the unquoted identifiers 'data_mode', 'code_mode', 'R', 'RW', 'RX' or 'RWX'"
    PPCERR_SECTION_EXPECTED_BSS_NAME = 155,
    // "this pragma is ignored when it occurs within a function"
    PPCERR_PRAGMA_IGNORED_IN_FUNCTION = 156,
    // "'%u' object '%n' is being put into section '%u' with access permission '%u';\nsection's access permission will be changed to '%u'"
    PPCERR_SECTION_PERMISSION_CHANGED = 157,
    // "pragma section expected an unquoted access permission; found \"%u\""
    PPCERR_SECTION_EXPECTED_PERMISSION = 158,
    // "pragma section expected object type or access permission, but not both"
    PPCERR_SECTION_TYPE_AND_PERMISSION = 159,
    // "pragma rel109_offset expected an integer (0, 1, 2 or 3)"
    PPCERR_REL109_OFFSET_EXPECTED_INT = 160,
    // "pragma function_align expected an integer (4, 8, 16, 32, 64 or 128)"
    PPCERR_FUNCTION_ALIGN_EXPECTED_INT = 161,
    // "address part of section name '%u' is allowed 1 - 8 hexidecimal digits"
    PPCERR_SECTION_ADDRESS_DIGITS = 162,
    // "interrupt function '%o' is bigger than 256 bytes (%i bytes big)"
    PPCERR_INTERRUPT_FUNCTION_TOO_BIG = 163,
    // "too many #pragma section directives (max is %i)"
    PPCERR_TOO_MANY_SECTION_PRAGMAS = 164,
    // "EPPC Processor preference panel is incompatible with this compiler"
    PPCERR_INCOMPATIBLE_PROCESSOR_PANEL = 165,
    // "illegal forward label or undefined symbol (%u) in constant expression"
    PPCERR_ASM_UNDEFINED_SYMBOL = 166,
    // "%u was not assigned to a register (try using register qualifier)"
    PPCERR_ASM_NOT_IN_REGISTER = 167,
    // "@hiword can't be used in this context (only works on variables not registers)"
    PPCERR_ASM_ILLEGAL_HIWORD = 168,
    // "floating point constants are not allowed if floating point is off."
    PPCERR_FLOAT_CONSTANT_WITHOUT_FLOAT = 169,
    // "possible unintended use of address of %u in constant expression\nUse 'la' or 'las' simplified mnemonics to load addresses"
    PPCERR_ASM_ADDRESS_IN_CONSTANT = 170,
    // "expected a register name here"
    PPCERR_ASM_EXPECTED_REGISTER = 171,
    // "out of registers for local variable %u\nTry using optimization level 1 or greater"
    PPCERR_OUT_OF_REGISTERS_FOR_LOCAL = 172,
    // "function level assembler can not be inlined.\nUse 'asm { instr... }' blocks inside an inline function."
    PPCERR_ASM_FUNCTION_NOT_INLINABLE = 173,
    // "PCode cannot access the global variable %o, because it has been stored in the TOC."
    PPCERR_PCODE_GLOBAL_IN_TOC = 174,
    // "PCode cannot use long long."
    PPCERR_PCODE_LONG_LONG = 175,
    // "PCode function cannot take a variable argument list."
    PPCERR_PCODE_VARARGS = 176,
    // "illegal use of label or object in a absolute branch."
    PPCERR_ASM_ABSOLUTE_BRANCH_TARGET = 177,
    // "AltiVec PIM requires warning for volatile parameter to %u."
    PPCERR_ALTIVEC_VOLATILE_ARGUMENT = 178,
    // "Mach-O can't load a float constant directly. Use addis rX,picbase,ha16(float_const_var); lfd fX,lo16(float_const_var)(rX)"
    PPCERR_MACHO_FLOAT_CONSTANT = 179,
    // "Illegal addressing mode (32 bit). Try constructing an address using addis rX,rPIC,ha16(var); lwz rX,lo16(var)(rX)"
    PPCERR_MACHO_32BIT_ADDRESSING = 180,
    // "%o cannot be an interrupt function with variable argument list"
    PPCERR_INTERRUPT_FUNCTION_VARARGS = 181,
    // "incoming parameter area > 32K"
    PPCERR_INCOMING_PARAMETERS_TOO_BIG = 182,
    // "outgoing parameter area > 32K"
    PPCERR_OUTGOING_PARAMETERS_TOO_BIG = 183,
    // "N/A"
    PPCERR_NOT_APPLICABLE = 184,
    // "when there is a frame, it is best to have frfree just before the %u"
    PPCERR_FRFREE_BEFORE_EXIT = 185,
    // "when there is a frame, it is best to have only one explicit exit\n(try branching to a label just before the frfree)"
    PPCERR_FRFREE_ONE_EXIT = 186,
    // "when there is a frame, it is generally best to have a %u just after the frfree"
    PPCERR_EXIT_AFTER_FRFREE = 187,
    // "frfree directive is only allowed at the end of a function. To exit from the middle of a function, branch to a frfree at the end."
    PPCERR_FRFREE_NOT_AT_END = 188,
    // "Mach-O 'pic' model is only supported if 'dynamic' is on."
    PPCERR_MACHO_PIC_NEEDS_DYNAMIC = 189,
    // "Function '%u' contains no instructions."
    PPCERR_FUNCTION_HAS_NO_INSTRUCTIONS = 190,
    // "pragma min_struct_alignment expected an integer (4, 8, 16, 32, 64 or 128)."
    PPCERR_MIN_STRUCT_ALIGNMENT_EXPECTED_INT = 191,
    // "correct syntax is __declspec(section \"sect_name\") where sect_name is a quoted string."
    PPCERR_DECLSPEC_SECTION_SYNTAX = 192,
    // "global register variable declarations are ignored when they occur within a function."
    PPCERR_GLOBAL_REGISTER_IN_FUNCTION = 193,
    // "global register variable declarations must be variables of storage class static, extern or register."
    PPCERR_GLOBAL_REGISTER_STORAGE_CLASS = 194,
    // "global register variable declaration syntax is storage_class type var_name asm (\"xn\");\nstorage_class is extern, static or register, \"xn\" is quoted, 'x' should be r,f or v,\nand 'n' should be a nonvolatile register number."
    PPCERR_GLOBAL_REGISTER_SYNTAX = 195,
    // "global register variable declaration supports %i registers beginning with %u%i for this register class."
    PPCERR_GLOBAL_REGISTER_RANGE = 196,
    // "global register variable assigned to '%u%i' can not be reassigned to '%u%i'."
    PPCERR_GLOBAL_REGISTER_REASSIGNED = 197,
    // "global register variable '%n' already is assigned to '%u%i'."
    PPCERR_GLOBAL_REGISTER_ALREADY_ASSIGNED = 198,
    // "gaps between assigned global register variables (nothing at '%u%i') are not permitted."
    PPCERR_GLOBAL_REGISTER_GAP = 199,
    // "global register variable is assigned to register class '%u' but should be '%u'."
    PPCERR_GLOBAL_REGISTER_WRONG_CLASS = 200,
    // "global register variables can only use single registers (cannot be long longs or software fp doubles)."
    PPCERR_GLOBAL_REGISTER_NOT_SINGLE = 201,
    // "global register variable '%o' cannot be assigned %u%i because that register has already been assigned to something else."
    PPCERR_GLOBAL_REGISTER_TAKEN = 202,
    // "global and static register variables cannot have the same name: '%n'."
    PPCERR_GLOBAL_REGISTER_NAME_CLASH = 203,
    // "global register variables must be variables; '%n' isn't a variable."
    PPCERR_GLOBAL_REGISTER_NOT_VARIABLE = 204,
    // "type cannot be made into a global register variable;\nonly scalers, doubles, floats and vectors are supported."
    PPCERR_GLOBAL_REGISTER_BAD_TYPE = 205,
    // "cannot declare global register variables after code has been generated."
    PPCERR_GLOBAL_REGISTER_AFTER_CODE = 206,
    // "correct syntax is __declspec(interrupt flags) where optional flags can be 1 or more of following space separated list:\nenable SRR DAR DSISR fprs vrs nowarn."
    PPCERR_DECLSPEC_INTERRUPT_SYNTAX = 207,
    // "pragma processor expected an integer, such as 603, or a string (no quotes), such as 603e."
    PPCERR_PROCESSOR_PRAGMA_SYNTAX = 208,
    // "pragma prepare_compress is ignored if an appropriate processor is not passed to pragma processor."
    PPCERR_PREPARE_COMPRESS_IGNORED = 209,
    // "intrinsic %u argument %i is not constant"
    PPCERR_INTRINSIC_ARGUMENT_NOT_CONSTANT = 210,
    // "intrinsic %u argument %i constant value value %i is not in range %i..%i using %i"
    PPCERR_INTRINSIC_ARGUMENT_OUT_OF_RANGE = 211,
    // "inline asm instruction '%u', defines reserved register r%i.\nMixing assembler and C code depends on the inline assembler not modifying the reserved registers directly.\nUse function level assembler if you need to directly modify the reserved registers."
    PPCERR_ASM_DEFINES_RESERVED_REGISTER = 212,
    // "out of range shift value (%i) will be masked to (%i)."
    PPCERR_SHIFT_OUT_OF_RANGE = 213,
    // "function prototype requires an ellipsis for variable argument macro va_start."
    PPCERR_VA_START_NEEDS_ELLIPSIS = 214,
    // "illegal use of frame pointer (r%i) in statement level assembler"
    PPCERR_ASM_FRAME_POINTER = 215,
    // "use pragma %u after pragma prepare_compress"
    PPCERR_PRAGMA_NEEDS_PREPARE_COMPRESS = 216,
    // "branch instruction needs to branch %i but range is %i ... %i"
    PPCERR_BRANCH_OUT_OF_RANGE = 217,
    // "currently cannot mix compressed and uncompressed executable sections in the same process image"
    PPCERR_MIXED_COMPRESSED_SECTIONS = 218,
    // "function consumes too many registers; use global optimization level >= 1 and/or reduce inlining"
    PPCERR_TOO_MANY_REGISTERS = 219,
    PPCERR_MAX = 220
};

extern void PPCError_Error(int code, ...);
extern void PPCError_Warning(int code, ...);
extern void PPCError_Message(char* format, ...);
extern void PPCError_ErrorTerm(VarArgShort code, ...);

#ifdef __cplusplus
}
#endif

#endif
