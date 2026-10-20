#ifndef COMPILER_CERROR_H
#define COMPILER_CERROR_H

#include <stdarg.h>
#include "compiler/common.h"

// The line an assert passes is its LINE plus CError_LINE_SHIFT: where an earlier release's source
// file had other lines above a point, the file redefines the shift from there on for that release
#define CError_LINE_SHIFT 0

#define CError_ASSERT(line, cond) \
    do { \
        if (!(cond)) { \
            CError_Internal(__FILE__, (line) + CError_LINE_SHIFT); \
        } \
    } while (0)
// a bare call, without CError_ASSERT's do/while (0)
#define CError_FATAL(line) CError_Internal(__FILE__, (line) + CError_LINE_SHIFT)

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: invented names: every message below (no source names the messages; from their texts in
// cc-eabi-ppc.r)
// The compiler's messages, by the numbers its message table (and every report) uses.
// clang-format off
enum {
    ERR_ILLEGAL_CHARACTER_CONSTANT = 100,                 // illegal character constant
    ERR_ILLEGAL_STRING_CONSTANT = 101,                    // illegal string constant
    ERR_UNEXPECTED_END_FILE = 102,                        // unexpected end of file
    ERR_UNTERMINATED_COMMENT = 103,                       // unterminated comment
    ERR_UNDEFINED_PREPROCESSOR_DIRECTIVE = 104,           // undefined preprocessor directive
    ERR_ILLEGAL_TOKEN = 105,                              // illegal token
    ERR_STRING_TOO_LONG = 106,                            // string too long
    ERR_IDENTIFIER_EXPECTED = 107,                        // identifier expected
    ERR_MACRO_REDEFINED = 108,                            // macro '%u' redefined
    ERR_ILLEGAL_ARGUMENT_LIST = 109,                      // illegal argument list
    ERR_TOO_MANY_MACRO_ARGUMENTS = 110,                   // too many macro arguments
    ERR_MACROS_TOO_COMPLEX = 111,                         // macro(s) too complex
    ERR_UNEXPECTED_END_LINE = 112,                        // unexpected end of line
    ERR_END_LINE_EXPECTED = 113,                          // end of line expected
    ERR_LPAREN_EXPECTED = 114,                            // '(' expected
    ERR_RPAREN_EXPECTED = 115,                            // ')' expected
    ERR_COMMA_EXPECTED = 116,                             // ',' expected
    ERR_PREPROCESSOR_SYNTAX_ERROR = 117,                  // preprocessor syntax error
    ERR_PRECEDING_IF_MISSING = 118,                       // preceding #if is missing
    ERR_UNTERMINATED_IF_MACRO = 119,                      // unterminated #if / macro
    ERR_UNEXPECTED_TOKEN = 120,                           // unexpected token
    ERR_DECLARATION_SYNTAX_ERROR = 121,                   // declaration syntax error
    ERR_IDENTIFIER_REDECLARED = 122,                      // identifier '%u' redeclared
    ERR_SEMICOLON_EXPECTED = 123,                         // ';' expected
    ERR_ILLEGAL_CONSTANT_EXPRESSION = 124,                // illegal constant expression
    ERR_RBRACKET_EXPECTED = 125,                          // ']' expected
    ERR_ILLEGAL_USE_VOID = 126,                           // illegal use of 'void'
    ERR_ILLEGAL_FUNCTION_DEFINITION = 127,                // illegal function definition
    ERR_ILLEGAL_FUNCTION_RETURN_TYPE = 128,               // illegal function return type
    ERR_ILLEGAL_ARRAY_DEFINITION = 129,                   // illegal array definition
    ERR_RBRACE_EXPECTED = 130,                            // '}' expected
    ERR_ILLEGAL_STRUCT_UNION_ENUM_CLASS_DEFINITION = 131, // illegal struct/union/enum/class definition
    ERR_STRUCT_UNION_ENUM_CLASS_TAG_REDEFINED = 132,      // struct/union/enum/class tag '%u' redefined
    ERR_STRUCT_UNION_CLASS_MEMBER_REDEFINED = 133,        // struct/union/class member '%u' redefined
    ERR_DECLARATOR_EXPECTED = 134,                        // declarator expected
    ERR_LBRACE_EXPECTED = 135,                            // '{' expected
    ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS = 136,  // illegal use of incomplete struct/union/class '%t'
    ERR_STRUCT_UNION_CLASS_SIZE_EXCEEDS_32K = 137,        // struct/union/class size exceeds 32K
    ERR_ILLEGAL_BITFIELD_DECLARATION = 138,               // illegal bitfield declaration
    ERR_DIVISION_BY_0 = 139,                              // division by 0
    ERR_UNDEFINED_IDENTIFIER = 140,                       // undefined identifier '%u'
    ERR_EXPRESSION_SYNTAX_ERROR = 141,                    // expression syntax error
    ERR_NOT_LVALUE = 142,                                 // not an lvalue
    ERR_ILLEGAL_OPERATION = 143,                          // illegal operation
    ERR_ILLEGAL_OPERAND = 144,                            // illegal operand
    ERR_DATA_TYPE_INCOMPLETE = 145,                       // data type is incomplete
    ERR_ILLEGAL_TYPE = 146,                               // illegal type
    ERR_TOO_MANY_INITIALIZERS = 147,                      // too many initializers
    ERR_POINTER_ARRAY_REQUIRED = 148,                     // pointer/array required
    ERR_NOT_STRUCT_UNION_CLASS = 149,                     // not a struct/union/class
    ERR_NOT_STRUCT_UNION_CLASS_MEMBER = 150,              // '%u' is not a struct/union/class member
    ERR_FILE_CANNOT_OPENED = 151,                         // the file '%u' cannot be opened
    ERR_ILLEGAL_INSTRUCTION_PROCESSOR = 152,              // illegal instruction for this processor
    ERR_ILLEGAL_OPERANDS_PROCESSOR = 153,                 // illegal operands for this processor
    ERR_NUMBER_OUT_RANGE = 154,                           // number is out of range
    ERR_ILLEGAL_ADDRESSING_MODE = 155,                    // illegal addressing mode
    ERR_ILLEGAL_DATA_SIZE = 156,                          // illegal data size
    ERR_ILLEGAL_REGISTER_LIST = 157,                      // illegal register list
    ERR_BRANCH_OUT_RANGE = 158,                           // branch out of range
    ERR_UNDEFINED_LABEL = 159,                            // undefined label '%u'
    ERR_REFERENCE_LABEL_OUT_RANGE = 160,                  // reference to label '%u' is out of range
    ERR_CALL_NON_FUNCTION = 161,                          // call of non-function
    ERR_FUNCTION_CALL_DOES_NOT_MATCH_PROTOTYPE = 162,     // function call does not match prototype
    ERR_ILLEGAL_USE_REGISTER_VARIABLE = 163,              // illegal use of register variable
    ERR_ILLEGAL_TYPE_CAST = 164,                          // illegal type cast
    ERR_FUNCTION_ALREADY_STACKFRAME = 165,                // function already has a stackframe
    ERR_FUNCTION_NO_INITIALIZED_STACKFRAME = 166,         // function has no initialized stackframe
    ERR_VALUE_NOT_STORED_REGISTER = 167,                  // value is not stored in register
    ERR_FUNCTION_NESTING_TOO_COMPLEX = 168,               // function nesting too complex
    ERR_ILLEGAL_USE_KEYWORD = 169,                        // illegal use of keyword
    ERR_COLON_EXPECTED = 170,                             // ':' expected
    ERR_LABEL_REDEFINED = 171,                            // label '%u' redefined
    ERR_CASE_CONSTANT_DEFINED_MORE_THAN_ONCE = 172,       // case constant defined more than once
    ERR_DEFAULT_LABEL_DEFINED_MORE_THAN_ONCE = 173,       // default label defined more than once
    ERR_ILLEGAL_INITIALIZATION = 174,                     // illegal initialization
    ERR_ILLEGAL_USE_INLINE_FUNCTION = 175,                // illegal use of inline function
    ERR_ILLEGAL_TYPE_QUALIFIERS = 176,                    // illegal type qualifier(s)
    ERR_ILLEGAL_STORAGE_CLASS = 177,                      // illegal storage class
    ERR_FUNCTION_NO_PROTOTYPE = 178,                      // function has no prototype
    ERR_ILLEGAL_ASSIGNMENT_CONSTANT = 179,                // illegal assignment to constant
    ERR_ILLEGAL_USE_PRECOMPILED_HEADER = 180,             // illegal use of precompiled header
    ERR_ILLEGAL_DATA_PRECOMPILED_HEADER = 181,            // illegal data in precompiled header
    ERR_VARIABLE_ARGUMENT_NOT_USED_FUNCTION = 182,        // variable / argument '%u' is not used in function
    ERR_ILLEGAL_USE_DIRECT_PARAMETERS = 183,              // illegal use of direct parameters
    ERR_RETURN_VALUE_EXPECTED = 184,                      // return value expected
    ERR_VARIABLE_NOT_INITIALIZED_BEFORE_BEING_USED = 185, // variable '%u' is not initialized before being used
    ERR_ILLEGAL_PRAGMA = 186,                             // illegal #pragma
    ERR_ILLEGAL_ACCESS_PROTECTED_PRIVATE_MEMBER = 187,    // illegal access to protected/private member
    ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER = 188, // ambiguous access to class/struct/union member
    ERR_ILLEGAL_USE_THIS = 189,                           // illegal use of 'this'
    ERR_UNIMPLEMENTED_C_FEATURE = 190,                    // unimplemented C++ feature
    ERR_ILLEGAL_USE_HANDLEOBJECT = 191,                   // illegal use of 'HandleObject'
    ERR_ILLEGAL_ACCESS_QUALIFIER = 192,                   // illegal access qualifier
    ERR_ILLEGAL_OPERATOR_DECLARATION = 193,               // illegal 'operator' declaration
    ERR_ILLEGAL_USE_ABSTRACT_CLASS = 194,                 // illegal use of abstract class ('%o')
    ERR_ILLEGAL_USE_PURE_FUNCTION = 195,                  // illegal use of pure function
    ERR_ILLEGAL_AMPERSAND_REFERENCE = 196,                // illegal '&' reference
    ERR_ILLEGAL_FUNCTION_OVERLOADING = 197,               // illegal function overloading
    ERR_ILLEGAL_OPERATOR_OVERLOADING = 198,               // illegal operator overloading
    ERR_AMBIGUOUS_ACCESS_OVERLOADED_FUNCTION = 199,       // ambiguous access to overloaded function
    ERR_ILLEGAL_ACCESS_USING_DECLARATION = 200,           // illegal access/using declaration
    ERR_ILLEGAL_FRIEND_DECLARATION = 201,                 // illegal 'friend' declaration
    ERR_ILLEGAL_INLINE_FUNCTION_DEFINITION = 202,         // illegal 'inline' function definition
    ERR_CLASS_NO_DEFAULT_CONSTRUCTOR = 203,               // class has no default constructor
    ERR_ILLEGAL_OPERATOR = 204,                           // illegal operator
    ERR_ILLEGAL_DEFAULT_ARGUMENTS = 205,                  // illegal default argument(s)
    ERR_POSSIBLE_UNWANTED_SEMICOLON = 206,                // possible unwanted ';'
    ERR_POSSIBLE_UNWANTED_ASSIGNMENT = 207,               // possible unwanted assignment
    ERR_POSSIBLE_UNWANTED_COMPARE = 208,                  // possible unwanted compare
    ERR_ILLEGAL_IMPLICIT_CONVERSION_FROM = 209,           // illegal implicit conversion from '%t' to / '%t'
    ERR_LOCAL_DATA_32K = 210,                             // local data >32k
    ERR_ILLEGAL_JUMP_PAST_INITIALIZER = 211,              // illegal jump past initializer
    ERR_ILLEGAL_CTOR_INITIALIZER = 212,                   // illegal ctor initializer
    ERR_CANNOT_CONSTRUCT_BASE_CLASS = 213,                // cannot construct %t's base class '%t'
    ERR_CANNOT_CONSTRUCT_DIRECT_MEMBER = 214,             // cannot construct %t's direct member '%u'
    ERR_IF_NESTING_OVERFLOW = 215,                        // #if nesting overflow
    ERR_ILLEGAL_EMPTY_DECLARATION = 216,                  // illegal empty declaration
    ERR_ILLEGAL_IMPLICIT_ENUM_CONVERSION_FROM = 217,      // illegal implicit enum conversion from '%t' to / '%t'
    ERR_ILLEGAL_USE_PRAGMA_PARAMETER = 218,               // illegal use of #pragma parameter
    ERR_VIRTUAL_FUNCTIONS_CANNOT_PASCAL_FUNCTIONS = 219,  // virtual functions cannot be pascal functions
    // illegal implicit const/volatile pointer conversion from '%t' to / '%t'
    ERR_ILLEGAL_IMPLICIT_CONST_VOLATILE_POINTER_CONVERSION = 220,
    ERR_ILLEGAL_USE_NON_STATIC_MEMBER = 221,      // illegal use of non-static member
    ERR_ILLEGAL_PRECOMPILED_HEADER_VERSION = 222, // illegal precompiled header version
    // illegal precompiled header compiler flags or target
    ERR_ILLEGAL_PRECOMPILED_HEADER_COMPILER_FLAGS_TARGET = 223,
    ERR_CONST_AMPERSAND_VARIABLE_NEEDS_INITIALIZER = 224, // 'const' or '&' variable needs initializer
    ERR_HIDES_INHERITED_VIRTUAL_FUNCTION = 225,           // '%o' hides inherited virtual function '%o'
    ERR_PASCAL_FUNCTION_CANNOT_OVERLOADED = 226,          // pascal function cannot be overloaded
    // derived function differs from virtual base function in return type only
    ERR_DERIVED_FUNCTION_DIFFERS_FROM_VIRTUAL_BASE = 227,
    // non-const '&' reference initialized to temporary
    ERR_NON_CONST_AMPERSAND_REFERENCE_INITIALIZED_TEMPORARY = 228,
    ERR_ILLEGAL_TEMPLATE_DECLARATION = 229, // illegal template declaration
    ERR_LESS_EXPECTED = 230,                // '<' expected
    ERR_GREATER_EXPECTED = 231,             // '>' expected
    ERR_ILLEGAL_TEMPLATE_ARGUMENTS = 232,   // illegal template argument(s)
    ERR_CANNOT_INSTANTIATE = 233,           // cannot instantiate '%o'
    ERR_TEMPLATE_REDEFINED = 234,           // template redefined
    ERR_TEMPLATE_PARAMETER_MISMATCH = 235,  // template parameter mismatch
    // cannot pass const/volatile data object to non-const/volatile member function
    ERR_CANNOT_PASS_CONST_VOLATILE_DATA_OBJECT = 236,
    ERR_PRECEDING_PRAGMA_PUSH_MISSING = 237,              // preceding '#pragma push' is missing
    ERR_ILLEGAL_EXPLICIT_TEMPLATE_INSTANTIATION = 238,    // illegal explicit template instantiation
    ERR_ILLEGAL_COPY_CONSTRUCTOR = 239,                   // illegal X::X(X) copy constructor
    ERR_FUNCTION_DEFINED_INLINE_AFTER_BEING_CALLED = 240, // function defined 'inline' after being called
    ERR_ILLEGAL_CONSTRUCTOR_DESTRUCTOR_DECLARATION = 241, // illegal constructor/destructor declaration
    ERR_CATCH_EXPECTED = 242,                             // 'catch' expected
    ERR_INCLUDE_NESTING_OVERFLOW = 243,                   // #include nesting overflow
    ERR_CANNOT_CONVERT = 244,                             // cannot convert / '%t' to / '%t'
    ERR_TYPE_MISMATCH = 245,                              // type mismatch / '%t' and / '%t'
    ERR_CLASS_TYPE_EXPECTED = 246,                        // class type expected
    ERR_ILLEGAL_EXPLICIT_CONVERSION_FROM = 247,           // illegal explicit conversion from '%t' to / '%t'
    ERR_FUNCTION_CALL_STAR_DOES_NOT_MATCH = 248,          // function call '*' does not match
    // identifier '%u' redeclared / was declared as: '%t' / now declared as: '%t'
    ERR_IDENTIFIER_REDECLARED_WAS_DECLARED_AS_NOW = 249,
    ERR_CANNOT_THROW_CLASS_AMBIGUOUS_BASE_CLASS = 250, // cannot throw class with ambiguous base class ('%u')
    // class '%t': '%o' has more than one final overrider: / '%o' / and '%o'
    ERR_CLASS_MORE_THAN_ONE_FINAL_OVERRIDER = 251,
    ERR_EXCEPTION_HANDLING_OPTION_DISABLED = 252,         // exception handling option is disabled
    ERR_CANNOT_DELETE_POINTER_CONST = 253,                // cannot delete pointer to const
    ERR_CANNOT_DESTROY_CONST_OBJECT = 254,                // cannot destroy const object
    ERR_CONST_MEMBER_NOT_INITIALIZED = 255,               // const member '%u' is not initialized
    ERR_AMPERSAND_REFERENCE_MEMBER_NOT_INITIALIZED = 256, // '&' reference member '%u' is not initialized
    ERR_RTTI_OPTION_DISABLED = 257,                       // RTTI option is disabled
    ERR_CONSTNESS_CASTED_AWAY = 258,                      // constness casted away
    // illegal const/volatile '&' reference initialization
    ERR_ILLEGAL_CONST_VOLATILE_AMPERSAND_REFERENCE_INITIALIZATION = 259,
    // inconsistent linkage: 'extern' object redeclared as 'static'
    ERR_INCONSISTENT_LINKAGE_EXTERN_OBJECT_REDECLARED_AS = 260,
    ERR_UNKNOWN_ASSEMBLER_INSTRUCTION_MNEMONIC = 261, // unknown assembler instruction mnemonic
    ERR_LOCAL_DATA_224_BYTES = 262,                   // local data > 224 bytes
    ERR_COULD_NOT_ASSIGNED_REGISTER = 263,            // '%u' could not be assigned to a register
    ERR_ILLEGAL_EXCEPTION_SPECIFICATION = 264,        // illegal exception specification
    ERR_EXCEPTION_SPECIFICATION_LIST_MISMATCH = 265,  // exception specification list mismatch
    // the parameter(s) of the '%n' function must be immediate value(s)
    ERR_PARAMETERS_FUNCTION_MUST_IMMEDIATE_VALUES = 266,
    ERR_SOM_CLASSES_ONLY_INHERIT_FROM_OTHER = 267,   // SOM classes can only inherit from other SOM based classes
    ERR_SOM_CLASSES_INHERTIANCE_MUST_VIRTUAL = 268,  // SOM classes inhertiance must be virtual
    ERR_SOM_CLASS_DATA_MEMBERS_MUST_PRIVATE = 269,   // SOM class data members must be private
    ERR_ILLEGAL_SOM_FUNCTION_OVERLOAD = 270,         // illegal SOM function overload '%o'
    ERR_NO_STATIC_MEMBERS_ALLOWED_SOM_CLASSES = 271, // no static members allowed in SOM classes
    ERR_NO_PARAMETERS_ALLOWED_SOM_CLASS_CONSTRUCTORS = 272, // no parameters allowed in SOM class constructors
    ERR_ILLEGAL_SOM_FUNCTION_PARAMETERS_RETURN_TYPE = 273,  // illegal SOM function parameters or return type
    // SOM runtime function '%u' not defined (should be defined in somobj.hh)
    ERR_SOM_RUNTIME_FUNCTION_NOT_DEFINED_SHOULD = 274,
    ERR_SOM_RUNTIME_FUNCTION_UNEXPECTED_TYPE = 275, // SOM runtime function '%u' has unexpected type
    ERR_NOT_SOM_CLASS = 276,                        // '%u' is not a SOM class
    ERR_ILLEGAL_USE_PRAGMA_OUTSIDE_SOM_CLASS = 277, // illegal use of #pragma outside of SOM class definition
    // introduced method '%o' is not specified in release order list
    ERR_INTRODUCED_METHOD_NOT_SPECIFIED_RELEASE_ORDER = 278,
    // SOM class access qualification only allowed to direct parent or own class
    ERR_SOM_CLASS_ACCESS_QUALIFICATION_ONLY_ALLOWED = 279,
    ERR_SOM_CLASS_MUST_ONE_NON_INLINE = 280, // SOM class must have one non-inline member function
    ERR_SOM_TYPE_UNDEFINED = 281,            // SOM type '%u' undefined
    // new SOM callstyle method '%o' must have explicit 'Environment *' parameter
    ERR_NEW_SOM_CALLSTYLE_METHOD_MUST_EXPLICIT = 282,
    ERR_FUNCTIONS_CANNOT_RETURN_SOM_CLASSES = 283,    // functions cannot return SOM classes
    ERR_FUNCTIONS_CANNOT_SOM_CLASS_ARGUMENTS = 284,   // functions cannot have SOM class arguments
    ERR_ASSIGNMENT_NOT_SUPPORTED_SOM_CLASSES = 285,   // assignment is not supported for SOM classes
    ERR_SIZEOF_NOT_SUPPORTED_SOM_CLASSES = 286,       // sizeof() is not supported for SOM classes
    ERR_SOM_CLASSES_CANNOT_CLASS_MEMBERS = 287,       // SOM classes	cannot be class members
    ERR_GLOBAL_SOM_CLASS_OBJECTS_NOT_SUPPORTED = 288, // global SOM class objects are not supported
    ERR_SOM_CLASS_ARRAYS_NOT_SUPPORTED = 289,         // SOM class arrays are not supported
    ERR_POINTER_TO_MEMBER_NOT_SUPPORTED_SOM = 290,    // 'pointer to member' is not supported for SOM classes
    ERR_SOM_CLASS_NO_RELEASE_ORDER_LIST = 291,        // SOM class has no release order list
    ERR_NOT_OBJECTIVE_C_CLASS = 292,                  // '%u' is not an Objective-C class
    ERR_METHOD_REDECLARED = 293,                      // method '%m' redeclared
    ERR_UNDEFINED_METHOD = 294,                       // undefined method '%m'
    ERR_CLASS_REDECLARED = 295,                       // class '%u' redeclared
    ERR_CLASS_REDEFINED = 296,                        // class '%u' redefined
    // Objective-C type '%u' is undefined (should be defined in objc.h)
    ERR_OBJECTIVE_C_TYPE_UNDEFINED_SHOULD_DEFINED = 297,
    ERR_OBJECTIVE_C_TYPE_UNEXPECTED_TYPE = 298, // Objective-C type '%u' has unexpected type
    ERR_METHOD_NOT_DEFINED = 299,               // method '%m' not defined
    ERR_METHOD_REDEFINED = 300,                 // method '%m' redefined
    ERR_ILLEGAL_USE_SELF = 301,                 // illegal use of 'self'
    ERR_ILLEGAL_USE_SUPER = 302,                // illegal use of 'super'
    ERR_ILLEGAL_MESSAGE_RECEIVER = 303,         // illegal message receiver
    ERR_RECEIVER_CANNOT_HANDLE_MESSAGE = 304,   // receiver cannot handle this message
    ERR_AMBIGUOUS_MESSAGE_SELECTOR_USED_ALSO_HAD = 305, // ambiguous message selector / used: '%m' / also had: '%m'
    ERR_UNKNOWN_MESSAGE_SELECTOR = 306,                 // unknown message selector
    ERR_ILLEGAL_USE_OBJECTIVE_C_OBJECT = 307,           // illegal use of Objective-C object
    ERR_PROTOCOL_REDEFINED = 308,                       // protocol '%u' redefined
    ERR_PROTOCOL_UNDEFINED = 309,                       // protocol '%u' is undefined
    ERR_PROTOCOL_ALREADY_PROTOCOL_LIST = 310,           // protocol '%u' is already in protocol list
    ERR_CATEGORY_REDEFINED = 311,                       // category '%u' redefined
    ERR_CATEGORY_UNDEFINED = 312,                       // category '%u' is undefined
    ERR_ILLEGAL_USE = 313,                              // illegal use of '%u'
    ERR_TEMPLATE_TOO_COMPLEX_RECURSIVE = 314,           // template too complex or recursive
    // illegal return value in void/constructor/destructor function
    ERR_ILLEGAL_RETURN_VALUE_VOID_CONSTRUCTOR_DESTRUCTOR = 315,
    // assigning a non-int numeric value to an unprototyped function
    ERR_ASSIGNING_NON_INT_NUMERIC_VALUE_UNPROTOTYPED = 316,
    ERR_IMPLICIT_ARITHMETIC_CONVERSION_FROM = 317,   // implicit arithmetic conversion from '%t' to '%t'
    ERR_PREPROCESSOR_ERROR_DIRECTIVE = 318,          // preprocessor #error directive
    ERR_AMBIGUOUS_ACCESS_NAME_FOUND = 319,           // ambiguous access to name found '%u' and '%u'
    ERR_ILLEGAL_NAMESPACE = 320,                     // illegal namespace
    ERR_ILLEGAL_USE_NAMESPACE_NAME = 321,            // illegal use of namespace name
    ERR_ILLEGAL_NAME_OVERLOADING = 322,              // illegal name overloading
    ERR_INSTANCE_VARIABLE_LIST_DOES_NOT_MATCH = 323, // instance variable list does not match @interface
    ERR_PROTOCOL_LIST_DOES_NOT_MATCH_INTERFACE = 324, // protocol list does not match @interface
    ERR_SUPER_CLASS_DOES_NOT_MATCH_INTERFACE = 325,   // super class does not match @interface
    // function result is a pointer/reference to an automatic variable
    ERR_FUNCTION_RESULT_POINTER_REFERENCE_AUTOMATIC_VARIABLE = 326,
    // cannot allocate initialized objects in the scratchpad
    ERR_CANNOT_ALLOCATE_INITIALIZED_OBJECTS_SCRATCHPAD = 327,
    ERR_ILLEGAL_CLASS_MEMBER_ACCESS = 328,                  // illegal class member access
    ERR_DATA_OBJECT_REDEFINED = 329,                        // data object '%o' redefined
    ERR_ILLEGAL_ACCESS_LOCAL_VARIABLE_FROM_OTHER = 330,     // illegal access to local variable from other function
    ERR_ILLEGAL_IMPLICIT_MEMBER_POINTER_CONVERSION = 331,   // illegal implicit member pointer conversion
    ERR_TYPENAME_REDEFINED = 332,                           // typename redefined
    ERR_OBJECT_REDEFINED = 333,                             // object '%o' redefined
    ERR_MAIN_NOT_DEFINED_AS_EXTERNAL_INT = 334,             // 'main' not defined as external 'int main()' function
    ERR_ILLEGAL_EXPLICIT_TEMPLATE_SPECIALIZATION = 335,     // illegal explicit template specialization
    ERR_NAME_NOT_BEEN_DECLARED_NAMESPACE_CLASS = 336,       // name has not been declared in namespace/class
    ERR_PREPROCESSOR_WARNING_DIRECTIVE = 337,               // preprocessor #warning directive
    ERR_ILLEGAL_USE_ASM_INLINE_FUNCTION = 338,              // illegal use of asm inline function
    ERR_ILLEGAL_USE_C_FEATURE_EC = 339,                     // illegal use of C++ feature in EC++
    ERR_ILLEGAL_USE_TEMPLATE_ARGUMENT_DEPENDENT_TYPE = 340, // illegal use template argument dependent type 'T::%u'
    ERR_ILLEGAL_USE_ALLOCA_FUNCTION_ARGUMENT = 341,         // illegal use of alloca() in function argument
    ERR_INLINE_FUNCTION_CALL_NOT_INLINED = 342,             // inline function call '%o' not inlined
    ERR_INCONSISTENT_USE_CLASS_STRUCT_KEYWORDS = 343,       // inconsistent use of 'class' and 'struct' keywords
    ERR_ILLEGAL_PARTIAL_SPECIALIZATION = 344,               // illegal partial specialization
    ERR_ILLEGAL_PARTIAL_SPECIALIZATION_ARGUMENT_LIST = 345, // illegal partial specialization argument list
    ERR_AMBIGUOUS_USE_PARTIAL_SPECIALIZATION = 346,         // ambiguous use of partial specialization
    ERR_LOCAL_CLASSES_SHALL_NOT_MEMBER_TEMPLATES = 347,     // local classes shall not have member templates
    ERR_ILLEGAL_TEMPLATE_ARGUMENT_DEPENDENT_EXPRESSION = 348, // illegal template argument dependent expression
    ERR_IMPLICIT_INT_NO_LONGER_SUPPORTED_C = 349,             // implicit 'int' is no longer supported in C++
    ERR_PAD_BYTES_INSERTED_AFTER_DATA_MEMBER = 350,           // %i pad byte(s) inserted after data member '%u'
    ERR_PURE_FUNCTION_NOT_VIRTUAL = 351,                      // pure function '%o' is not virtual
    ERR_ILLEGAL_VIRTUAL_FUNCTION_UNION = 352,                 // illegal virtual function '%o' in 'union'
    ERR_CANNOT_PASS_VOID_FUNCTION_PARAMETER = 353,            // cannot pass 'void' or 'function' parameter
    ERR_ILLEGAL_STATIC_CONST_MEMBER_INITIALIZATION = 354,     // illegal static const member '%u' initialization
    // 'typename' is missing in template argument dependent qualified type
    ERR_TYPENAME_MISSING_TEMPLATE_ARGUMENT_DEPENDENT_QUALIFIED = 355,
    ERR_MORE_THAN_ONE_EXPRESSION_NON_CLASS = 356, // more than one expression in non-class type conversion
    // template non-type argument objects shall have external linkage
    ERR_TEMPLATE_NON_TYPE_ARGUMENT_OBJECTS_SHALL = 357,
    ERR_ILLEGAL_INLINE_ASSEMBLY_OPERAND = 358,    // illegal inline assembly operand: %u
    ERR_ILLEGAL_UNSUPPORTED_ATTRIBUTE = 359,      // illegal or unsupported __attribute__
    ERR_CANNOT_CREATE_OBJECT_FILE = 360,          // cannot create object file '%f'
    ERR_ERROR_WRITING_OBJECT_FILE = 361,          // error writing to object file '%f'
    ERR_PRINTF_FORMAT_MISMATCH = 362,             // printf-family format string doesn't match arguments
    ERR_SCANF_FORMAT_MISMATCH = 363,              // scanf-family format string doesn't match arguments
    ERR_ALIGNOF_NOT_SUPPORTED_SOM_CLASSES = 364,  // __alignof__() is not supported for SOM classes
    ERR_ILLEGAL_MACRO_ARGUMENT_NAME = 365,        // illegal macro argument name '%u'
    ERR_CASE_EMPTY_RANGE_VALUES = 366,            // case has an empty range of values
    ERR_LONG_LONG_SWITCH_NOT_SUPPORTED = 367,     // 'long long' switch() is not supported
    ERR_LONG_LONG_CASE_RANGE_NOT_SUPPORTED = 368, // 'long long' case range is not supported
    ERR_EXPRESSION_NO_SIDE_EFFECT = 369,          // expression has no side effect
    ERR_RESULT_FUNCTION_CALL_NOT_USED = 370,      // result of function call is not used
    ERR_ILLEGAL_NON_TYPE_TEMPLATE_ARGUMENT = 371, // illegal non-type template argument
    ERR_ILLEGAL_USE_ABSTRACT_CLASS_TYPE = 372,    // illegal use of abstract class ('%t')
    ERR_ILLEGAL_TEMPLATE_PREFIX = 373,        // illegal use of 'template' prefix
    ERR_TEMPLATE_PARAMETER_ARGUMENT_LIST_MISMATCH = 374, // template parameter/argument list mismatch
    ERR_NO_MATCHING_DEALLOCATION_FUNCTION = 375,  // cannot find matching deallocation function for '%t'
    ERR_ILLEGAL_OPERAND_TYPE = 376,               // illegal operand '%t'
    ERR_ILLEGAL_OPERANDS_TYPES = 377,             // illegal operands '%t' %u '%t'
    ERR_ILLEGAL_USE_DEFAULT_TEMPLATE_ARGUMENT = 378, // illegal use of default template-argument
    ERR_ILLEGAL_UUID_SYNTAX = 379,                // illegal UUID syntax
    ERR_UUIDOF_NOT_SUPPORTED_SOM_CLASSES = 380,   // __uuidof() is not supported for SOM classes
    ERR_ILLEGAL_ACCESS_FROM_TO_PROTECTED_PRIVATE_MEMBER = 381,           // illegal access from '%t' to protected/private member '%o'
    ERR_INTEGRAL_TYPE_TOO_SMALL_FOR_POINTER = 382, // integral type is not large enough to hold pointer
    ERR_UNKNOWN_X86_ASSEMBLER_MNEMONIC = 383,     // unknown x86 assembler instruction mnemonic
    ERR_ILLEGAL_CV_FUNCTION_QUALIFIER_SEQUENCE = 384, // illegal use of const/volatile function qualifier sequence
    ERR_ILLEGAL_OPTIMIZATION_LEVEL_LIMITED = 385, // illegal optimization level for this limited version of CodeWarrior
    ERR_NO_UUID_DEFINED = 386,                    // no UUID defined for type '%t'
    ERR_IMPLICIT_COPY_ASSIGNMENT_CONST_REF_MEMBER = 387, // using implicit copy assigment for class with const or reference member ('%t')
    ERR_UNIMPLEMENTED_ASSEMBLER_INSTRUCTION = 388, // unimplemented assembler instruction/directive
    ERR_DLLIMPORT_OVERRIDE_APPLICATION_SCOPE = 389, // override of dllimport function '%u' only has application scope
    ERR_ILLEGAL_INLINE_OPERAND_COMBINATION = 390, // illegal combination of operands in inline statement at line %i
    ERR_ILLEGAL_INLINE_OPERAND = 391,             // illegal operand in inline statement at line %i
    ERR_FUNCTION_CALL_AMBIGUOUS = 392,            // function call '*' is ambiguous
    ERR_MAX = 393
};
// clang-format on

extern TStreamElement* cerror_locktoken;

extern void CError_Init(void);
extern void CError_SetErrorToken(TStreamElement* token);
extern void CError_LockErrorPos(TStreamElement* token, TStreamElement** saved);
extern void CError_UnlockErrorPos(TStreamElement** saved);
extern void CError_ResetErrorSkip(void);
extern char* CError_GetTypeName(Type* ty, UInt32 qual, Boolean useGlobalHeap);
extern char* CError_GetQualifiedName(NameSpace* nspace, HashNameNode* name);
extern char* CError_GetFunctionName(NameSpace* nspace, HashNameNode* name, TypeFunc* tfunc);
extern char* CError_GetObjectName(Object* obj);
extern char* CError_GetNameString(NameSpace* nspace, HashNameNode* operatorName);
extern void CError_ErrorMessage(int errTable, char* str, Boolean isTerminal, Boolean isWarning);
extern void CError_ErrorMessageVA(
    int code, const char* format, va_list list, Boolean isTerminal, Boolean isWarning
);
extern void CError_Error(int code, ...);
extern void CError_ErrorTerm(short code);
extern void CError_ErrorSkip(int code, ...);
extern void CError_ErrorFuncCall(short code, NameSpaceObjectList* args, ENodeList* argNodes);
extern void CError_OverloadedFunctionError(Object* obj, ObjectList* olst);
extern void CError_AbstractClassError(TypeClass* tclass);
extern void CError_Warning(int code, ...);
extern void CError_Internal(char* filename, int line);
extern void CError_NoMem(void);
extern void CError_UserBreak(void);
extern void CError_CannotOpen(void);
extern void CError_QualifierCheck(UInt32 qual);

#ifdef __cplusplus
}
#endif

#endif
