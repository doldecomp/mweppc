#ifndef COMPILER_TYPES_H
#define COMPILER_TYPES_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TypeType {
    TYPEVOID = 0,
    TYPEINT,
    TYPEFLOAT,
    TYPEENUM,
    TYPESTRUCT,
    TYPECLASS,
    TYPEFUNC,
    TYPEBITFIELD,
    TYPELABEL,
    TYPETEMPLATE,
    TYPEMEMBERPOINTER,
    TYPEPOINTER,
    TYPEARRAY,
    TYPEOBJCID,
    TYPETEMPLDEPEXPR,
    TYPEILLEGAL = -1
} TypeType;

#pragma pack(push, 2)

// Common fields across all kinds of types
struct Type {
    TypeType type;
    SInt32 size;
};

typedef enum IntegralType {
    IT_BOOL = 0,
    IT_CHAR = 1,
    IT_SCHAR = 2,
    IT_UCHAR = 3,
    IT_WCHAR_T = 4,
    IT_SHORT = 5,
    IT_USHORT = 6,
    IT_INT = 7,
    IT_UINT = 8,
    IT_LONG = 9,
    IT_ULONG = 10,
    IT_LONGLONG = 11,
    IT_ULONGLONG = 12,
    IT_FLOAT = 13,
    IT_SHORTDOUBLE = 14,
    IT_DOUBLE = 15,
    IT_LONGDOUBLE = 16,
    // 17 to 24: a target's custom types. CMachine's stcustomtype113/114 are IT_17/IT_18: 8 bytes,
    // kept in a GPR pair like a long long, but register copies and inline assembly treat them as
    // paired singles
    // NOTE: placeholder names (nothing names them)
    IT_17 = 17,
    IT_18 = 18,
    // Gekko's paired single, __vec2x32float__
    IT_VEC2X32FLOAT = 19, // NOTE: invented name (after its keyword)
    IT_20 = 20,
    IT_21 = 21,
    IT_22 = 22,
    IT_23 = 23,
    IT_24 = 24
} IntegralType;

struct TypeIntegral {
    TypeType type;
    SInt32 size;
    unsigned char integral;
};

struct TypeEnum {
    TypeType type;
    SInt32 size;
    NameSpace* nspace;
    ObjEnumConst* enumlist;
    Type* enumtype;
    HashNameNode* enumname;
};

struct TypeStruct {
    TypeType type;
    SInt32 size;
    HashNameNode* name;
    StructMember* members;
    char stype;
    SInt16 align;
};

struct StructMember {
    StructMember* next;
    Type* type;
    HashNameNode* name;
    SInt32 offset;
    UInt32 qual;
};

enum {
    STRUCT_TYPE_STRUCT = 0,
    STRUCT_TYPE_UNION = 1,
    STRUCT_TYPE_CLASS = 2,
    STRUCT_TYPE_MAX = 3,
    STRUCT_VECTOR_UCHAR = 4,
    STRUCT_VECTOR_SCHAR = 5,
    STRUCT_VECTOR_BCHAR = 6,
    STRUCT_VECTOR_USHORT = 7,
    STRUCT_VECTOR_SSHORT = 8,
    STRUCT_VECTOR_BSHORT = 9,
    STRUCT_VECTOR_UINT = 10,
    STRUCT_VECTOR_SINT = 11,
    STRUCT_VECTOR_BINT = 12,
    STRUCT_VECTOR_FLOAT = 13,
    STRUCT_VECTOR_PIXEL = 14
};

struct ClassList {
    ClassList* next;
    TypeClass* base;
    SInt32 offset;
    SInt32 voffset;
    AccessType access;
    Boolean is_virtual;
};

struct VClassList {
    VClassList* next;
    TypeClass* base;
    // offset within the class instance
    SInt32 offset;
    // offset within the vtable
    SInt32 voffset;
    Boolean has_override;
};

struct ClassFriend {
    ClassFriend* next;
    union {
        TypeClass* theclass;
        Object* obj;
    } u;
    Boolean isclass;
};

struct BClassList {
    BClassList* next;
    Type* type;
};

struct VTable {
    Object* object;
    TypeClass* owner;
    SInt32 offset;
    SInt32 size;
};

struct TypeClass {
    TypeType type;
    SInt32 size;
    NameSpace* nspace;
    HashNameNode* classname;
    ClassList* bases;
    VClassList* vbases;
    ObjMemberVar* ivars;
    ClassFriend* friends;
    VTable* vtable;
    SOMInfo* sominfo;
    ObjCInfo* objcinfo;
    UInt16 flags;
    SInt8 mode;
    SInt8 action;
    SInt16 align;
    UInt8 eflags;
};

// TypeClass::mode
enum {
    CLASS_MODE_STRUCT = 0,
    CLASS_MODE_UNION = 1,
    CLASS_MODE_CLASS = 2
};

// TypeClass::flags
enum {
    // class is a HandleObject
    CLASS_HANDLEOBJECT = 1,
    CLASS_COMPLETED = 2,
    CLASS_ABSTRACT = 8,
    // inherits from SingleObject/SingleInheritance
    CLASS_SINGLE_OBJECT = 0x10,
    CLASS_HAS_VBASES = 0x20,
    // defines at least one conversion function
    CLASS_IS_CONVERTIBLE = 0x40,
    // is a TemplClass
    CLASS_IS_TEMPL = 0x100,
    // is a TemplClassInst
    CLASS_IS_TEMPL_INST = 0x800,
    CLASS_IS_TEMPL_ANY = CLASS_IS_TEMPL | CLASS_IS_TEMPL_INST,
    // is 0 bytes (forced to 1 byte for ABI reasons)
    CLASS_EMPTY = 0x1000,
    // inherits from __comobject
    CLASS_COM_OBJECT = 0x2000,
    // made under `#pragma vbase_ctor_offset`: its constructors set its virtual bases' constructor
    // offsets (CDecl_MakeVBaseList, CABI_InitVBaseCtorOffsets)
    CLASS_VBASE_CTOR_OFFSET = 0x8000 // NOTE: invented name (after the pragma)
};

// TypeClass::eflags
enum {
    CLASS_EFLAGS_INTERNAL = 1,
    CLASS_EFLAGS_IMPORT = 2,
    CLASS_EFLAGS_EXPORT = 4,
    // the struct alignment mode (copts.structalignment) at the class's definition, plus one, which
    // CABI restores while laying the class out
    CLASS_EFLAGS_STRUCT_ALIGN = 0xF0 // NOTE: invented name
};

// NOTE: invented names: CLASS_ACTION_NONE, CLASS_ACTION_KEY_FUNCTION, CLASS_ACTION_UNUSED,
// CLASS_ACTION_JAVA
// TypeClass::action: where and when the class's vtable is emitted
enum {
    // no key function: the vtable is a weak static where it is needed
    CLASS_ACTION_NONE = 0,
    // emitted with the definition of the key function (the first non-inline virtual function,
    // FUNC_KEY_FUNCTION); one defined inline after all makes it CLASS_ACTION_NONE (CClass_MemberDef)
    CLASS_ACTION_KEY_FUNCTION = 1,
    // nothing sets it (CClass_MemberDef fails on it)
    CLASS_ACTION_UNUSED = 2,
    // __javaobject: none
    CLASS_ACTION_JAVA = 3
};

struct ExceptSpecList {
    ExceptSpecList* next;
    Type* type;
    UInt32 qual;
};

struct FuncArg {
    FuncArg* next;
    HashNameNode* name;
    ENode* dexpr;
    Type* type;
    UInt32 qual;
    SInt16 sclass;
    Boolean is_array;
};

struct TypeFunc {
    TypeType type;
    SInt32 size;
    FuncArg* args;
    ExceptSpecList* exspecs;
    Type* functype;
    UInt32 qual;
    UInt32 flags;
};

// NOTE: invented names: FUNC_KEY_FUNCTION, FUNC_OVERRIDE, FUNC_PTM, FUNC_AUTO_INLINE,
// FUNC_TEMPLATE_ACTION, FUNC_COVARIANT
// TypeFunc::flags. TODO: FUNC_FLAGS_4000000 and FUNC_FLAGS_10000000 are placeholders: nothing sets
// them, a redeclaration copies them (CDecl_GetFunctionObject); 0x10000000 is inside
// FUNC_CALL_CONV_MASK, so it is probably a calling convention's bit
enum {
    FUNC_PASCAL = 1,
    // does the function have a body?
    FUNC_DEFINED = 2,
    // the class's key function (TypeClass::action)
    FUNC_KEY_FUNCTION = 4,
    // abstract
    FUNC_PURE = 8,
    FUNC_METHOD = 0x10,
    // overrides a base class's virtual function
    FUNC_OVERRIDE = 0x20,
    // func that's like "operator SomeOtherType()"
    FUNC_CONVERSION = 0x40,
    // a pointer-to-member function's type: its this arguments are explicit (CDecl_MakePTMFuncType)
    FUNC_PTM = 0x80,
    // an auto-generated ctor, dtor or assignment operator
    FUNC_AUTO_GENERATED = 0x100,
    FUNC_INTRINSIC = 0x200,
    FUNC_NOTHROW = 0x400,
    // chosen for inlining by -inline auto
    FUNC_AUTO_INLINE = 0x800,
    FUNC_IS_CTOR = 0x1000,
    FUNC_IS_DTOR = 0x2000,
    FUNC_OBJC_METHOD = 0x4000,
    FUNC_CONST = 0x8000,
    FUNC_VOLATILE = 0x10000,
    FUNC_FLAGS_CV = FUNC_CONST | FUNC_VOLATILE,
    // the definition of a template
    FUNC_IS_TEMPL = 0x100000,
    // its inline action waits on the template action list (CInline_AddInlineFunctionAction)
    FUNC_TEMPLATE_ACTION = 0x200000,
    // a covariant override (its return type differs from the overridden one's)
    FUNC_COVARIANT = 0x400000,
    // a concrete instance of a template
    FUNC_IS_TEMPL_INSTANCE = 0x800000,
    FUNC_IS_TEMPL_ANY = FUNC_IS_TEMPL | FUNC_IS_TEMPL_INSTANCE,
    FUNC_FLAGS_4000000 = 0x4000000,
    FUNC_FLAGS_10000000 = 0x10000000
};
#define FUNC_CALL_CONV_MASK (0xF0000000 | FUNC_PASCAL)

struct TypeMemberFunc {
    TypeType type;
    SInt32 size;
    FuncArg* args;
    ExceptSpecList* exspecs;
    Type* functype;
    UInt32 qual;
    UInt32 flags;
    TypeClass* theclass;
    // index of this method within the vtable
    SInt32 vtbl_index;
    // ID used for tracking this method within browse data
    SInt32 funcid;
    Boolean is_static;
};

struct TypeBitfield {
    TypeType type;
    SInt32 size;
    Type* bitfieldtype;
    // offset in bits
    char offset;
    // size in bits
    char bitlength;
    // set for an unnamed bitfield
    char suppressAlignment; // NOTE: invented name
#if VERSION == VERSION_GC_2_5
    // 2.5: two more members, which its bitfield layout (CMach_StructLayoutBitfield) sets to the
    // declared type's size and to -1
    // TODO: invented names: declsize, x0e (no reader of either is known, so their roles are
    // guesses)
    char declsize;
    char x0e;
#endif
};

typedef enum TypeTemplDepType {
    TEMPLDEP_ARGUMENT,
    TEMPLDEP_QUALNAME,
    TEMPLDEP_TEMPLATE,
    TEMPLDEP_ARRAY,
    TEMPLDEP_QUALTEMPL,
    TEMPLDEP_BITFIELD
} TypeTemplDepType;

struct TypeTemplDep {
    TypeType type;
    SInt32 size;
    TypeTemplDepType dtype;
    union {
        TemplParamID pid;
        struct {
            TypeTemplDep* type;
            HashNameNode* name;
        } qual;
        struct {
            TemplClass* templ;
            TemplArg* args;
        } templ;
        struct {
            Type* type;
            ENode* index;
        } array;
        struct {
            TypeTemplDep* type;
            TemplArg* args;
        } qualtempl;
        struct {
            Type* type;
            ENode* size;
        } bitfield;
    } u;
};

struct TypeMemberPointer {
    TypeType type;
    SInt32 size;
    Type* ty1;
    Type* ty2;
    UInt32 qual;
};

// Used for TYPEPOINTER and TYPEARRAY
struct TypePointer {
    TypeType type;
    SInt32 size;
    Type* target;
    UInt32 qual;
};

// Always has Q_IS_OBJC_ID set
struct TypeObjCID {
    TypePointer pointer;
    ObjCProtocolList* protocols;
    // NOTE: placeholder name: nothing reads or writes it (TypeObjCID is 0x14 bytes)
    short x12;
};

struct TypeList {
    TypeList* next;
    Type* type;
};

#pragma pack(pop)

#define TYPE(ty) ((Type*)(ty))
#define TYPE_INTEGRAL(ty) ((TypeIntegral*)(ty))
#define TYPE_ENUM(ty) ((TypeEnum*)(ty))
#define TYPE_STRUCT(ty) ((TypeStruct*)(ty))
#define TYPE_CLASS(ty) ((TypeClass*)(ty))
#define TYPE_FUNC(ty) ((TypeFunc*)(ty))
#define TYPE_METHOD(ty) ((TypeMemberFunc*)(ty))
#define TYPE_BITFIELD(ty) ((TypeBitfield*)(ty))
#define TYPE_TEMPLATE(ty) ((TypeTemplDep*)(ty))
#define TYPE_MEMBER_POINTER(ty) ((TypeMemberPointer*)(ty))
#define TYPE_POINTER(ty) ((TypePointer*)(ty))
#define TPTR_TARGET(ty) (TYPE_POINTER(ty)->target)
#define TPTR_QUAL(ty) (TYPE_POINTER(ty)->qual)
#define TYPE_OBJC_ID(ty) ((TypeObjCID*)(ty))

#define IS_TYPE_VOID(ty) ((ty)->type == TYPEVOID)
#define IS_TYPE_INT(ty) ((ty)->type == TYPEINT)
#define IS_TYPE_ENUM(ty) ((ty)->type == TYPEENUM)
#define IS_TYPE_INT_OR_ENUM(ty) (IS_TYPE_INT(ty) || IS_TYPE_ENUM(ty))
#define IS_TYPE_FLOAT(ty) ((ty)->type == TYPEFLOAT)
#define IS_TYPE_INT_OR_FLOAT(ty) (IS_TYPE_INT(ty) || IS_TYPE_FLOAT(ty))
#define IS_TYPE_STRUCT(ty) ((ty)->type == TYPESTRUCT)
#define IS_TYPE_CLASS(ty) ((ty)->type == TYPECLASS)
#define IS_TYPE_SOM_CLASS(ty) ((ty)->type == TYPECLASS && TYPE_CLASS(ty)->sominfo)
#define IS_TYPE_OBJC_CLASS(ty) ((ty)->type == TYPECLASS && TYPE_CLASS(ty)->objcinfo)
#define IS_TYPE_FUNC(ty) ((ty)->type == TYPEFUNC)
#define IS_TYPEFUNC_METHOD(ty) ((ty)->flags & FUNC_METHOD)
#define IS_TYPE_METHOD(ty) (IS_TYPE_FUNC(ty) && IS_TYPEFUNC_METHOD(TYPE_FUNC(ty)))
#define IS_TYPE_NONMETHOD(ty) (IS_TYPE_FUNC(ty) && !IS_TYPEFUNC_METHOD(TYPE_FUNC(ty)))
#define IS_TYPE_NONSTATIC_METHOD(ty) (IS_TYPE_METHOD(ty) && !TYPE_METHOD(ty)->is_static)
#define IS_TYPE_STATIC_METHOD(ty) (IS_TYPE_METHOD(ty) && TYPE_METHOD(ty)->is_static)
#define IS_TYPEFUNC_NONSTATIC_METHOD(ty) (IS_TYPEFUNC_METHOD(ty) && !TYPE_METHOD(ty)->is_static)
#define IS_TYPE_TEMPLATE(ty) ((ty)->type == TYPETEMPLATE)
#define IS_TYPE_POINTER(ty) ((ty)->type == TYPEPOINTER || (ty)->type == TYPEARRAY)
#define IS_TYPE_POINTER_ONLY(ty) ((ty)->type == TYPEPOINTER)
#define IS_TYPE_REFERENCE(ty) ((ty)->type == TYPEPOINTER && (TYPE_POINTER(ty)->qual & Q_REFERENCE))
#define IS_TYPEPOINTER_REFERENCE(ty) ((ty)->qual & Q_REFERENCE)
#define IS_TYPE_ARRAY(ty) ((ty)->type == TYPEARRAY)
#define IS_TYPE_BITFIELD(ty) ((ty)->type == TYPEBITFIELD)
#define IS_TYPE_TEMPLDEPEXPR(ty) ((ty)->type == TYPETEMPLDEPEXPR)
#define IS_TYPESTRUCT_VECTOR(ty) \
    ((ty)->stype >= STRUCT_VECTOR_UCHAR && (ty)->stype <= STRUCT_VECTOR_PIXEL)
#define IS_TYPE_VECTOR(ty) ((ty)->type == TYPESTRUCT && IS_TYPESTRUCT_VECTOR(TYPE_STRUCT(ty)))
#define IS_TYPE_NONVECTOR_STRUCT(ty) \
    ((ty)->type == TYPESTRUCT && !IS_TYPESTRUCT_VECTOR(TYPE_STRUCT(ty)))
#define IS_TYPE_MEMBERPOINTER(ty) ((ty)->type == TYPEMEMBERPOINTER)
#define IS_TYPE_4BYTES_MEMBERPOINTER(ty) (((ty)->type == TYPEMEMBERPOINTER) && ((ty)->size == 4u))
#define IS_TYPE_12BYTES_MEMBERPOINTER(ty) (((ty)->type == TYPEMEMBERPOINTER) && ((ty)->size == 12u))
#define TYPE_FITS_IN_REGISTER(ty) \
    (((ty)->type == TYPEINT) || \
     ((ty)->type == TYPEENUM) || \
     (IS_TYPE_POINTER(ty) && ((ty)->type != TYPEARRAY)) || \
     IS_TYPE_4BYTES_MEMBERPOINTER(ty))
#define TYPE_IS_8BYTES(ty) \
    ((((ty)->type == TYPEINT) || ((ty)->type == TYPEENUM)) && ((ty)->size == 8))
#define TYPE_FITS_IN_REGISTER_2(ty) \
    (((ty)->type == TYPEINT) || \
     ((ty)->type == TYPEENUM) || \
     IS_TYPE_POINTER(ty) || \
     IS_TYPE_4BYTES_MEMBERPOINTER(ty))
// NOTE: invented names: IS_TYPE_VEC2X32FLOAT, IS_TYPE_FLOAT_NOT_VEC2X32FLOAT
// Gekko's paired single (__vec2x32float__)
#define IS_TYPE_VEC2X32FLOAT(ty) \
    (IS_TYPE_FLOAT(ty) && TYPE_INTEGRAL(ty)->integral == IT_VEC2X32FLOAT)
#define IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(ty) ((ty)->type == TYPEFLOAT && !IS_TYPE_VEC2X32FLOAT(ty))
// a float that -fp soft keeps in GPRs (never the paired single)
// NOTE: invented name
#define TYPE_IS_SOFT_FLOAT(ty) (copts.soft_float && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(ty))
// a value held in a GPR pair: a long long, or a soft-float double
// NOTE: invented name
#define TYPE_IS_GPR_PAIR(ty) (TYPE_IS_8BYTES(ty) || (TYPE_IS_SOFT_FLOAT(ty) && (ty)->size != 4))

#define OBJ_GET_TARGET_VOLATILE(obj) \
    (IS_TYPE_POINTER((obj)->type) ? (TYPE_POINTER((obj)->type)->qual & Q_VOLATILE) \
                                  : ((obj)->qual & Q_VOLATILE))
#define OBJ_GET_TARGET_CONST(obj) \
    (IS_TYPE_POINTER((obj)->type) ? (TYPE_POINTER((obj)->type)->qual & Q_CONST) \
                                  : ((obj)->qual & Q_CONST))

#ifdef __cplusplus
}
#endif

#endif
