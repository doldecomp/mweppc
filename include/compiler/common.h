#ifndef COMPILER_COMMON_H
#define COMPILER_COMMON_H
#include "version.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Every record of the program, for use through pointers; each is defined in its owner's header. */
struct CInlineBackendOptions;
struct MachineInfo;
struct DwarfSym;
typedef struct TemplStack TemplStack;
typedef struct Alias Alias;
typedef struct ArgMatch ArgMatch;
typedef struct IAEffect IAEffect;
typedef struct IAMnemonic IAMnemonic;
typedef struct IAEffects IAEffects;
typedef struct BClassList BClassList;
typedef struct BE_SymNode BE_SymNode;
typedef struct Conversion Conversion;
typedef struct BitVector BitVector;
typedef struct CPrepFileInfo CPrepFileInfo;
typedef struct BufferUpdate BufferUpdate;
typedef struct TStreamElement TStreamElement;
typedef struct ClassFriend ClassFriend;
typedef struct DeclInfo DeclInfo;
typedef struct ExceptionAction ExceptionAction;
typedef struct CGList CGList;
typedef struct CI_SwitchCase CI_SwitchCase;
typedef struct CI_FuncData CI_FuncData;
typedef struct CI_Var CI_Var;
typedef struct CInt64 CInt64;
typedef struct CLabel CLabel;
typedef struct BasicInductionVar BasicInductionVar;
typedef struct CNameNode CNameNode;
typedef struct OLinkList OLinkList;
typedef struct CompilerLinkerOptions CompilerLinkerOptions;
typedef struct ParserTryBlock ParserTryBlock;
typedef struct ObjType ObjType;
typedef struct ObjTypeTag ObjTypeTag;
typedef struct ObjMemberVarPath ObjMemberVarPath;
typedef struct CI_Action CI_Action;
typedef struct CompilerLinkerParamBlk CompilerLinkerParamBlk;
typedef struct NameResult NameResult;
typedef struct CScopeSave CScopeSave;
typedef struct TemplArg TemplArg;
typedef struct Candidate Candidate;
typedef struct ClassLayout ClassLayout;
typedef struct ClassList ClassList;
typedef struct TemplPartialSpec TemplPartialSpec;
typedef struct PropInfo PropInfo;
typedef struct InductionVar InductionVar;
typedef struct UseOrDef UseOrDef;
typedef struct RegUseOrDef RegUseOrDef;
typedef struct ObjectUseDef ObjectUseDef;
typedef struct InstrList InstrList;
typedef struct BlockList BlockList;
typedef struct ComparisonValues ComparisonValues;
typedef struct ConIteratorList ConIteratorList;
typedef struct ConIterator ConIterator;
typedef struct ConversionIterator ConversionIterator;
typedef struct CtorChain CtorChain;
typedef struct DWInfo DWInfo;
typedef struct DefArgCtorInfo DefArgCtorInfo;
typedef struct DwarfFixup DwarfFixup;
typedef struct DwarfFunctionState DwarfFunctionState;
typedef struct DwarfLocationOperand DwarfLocationOperand;
typedef struct DwarfNode DwarfNode;
typedef struct DwarfRef DwarfRef;
typedef struct DwarfStateList DwarfStateList;
typedef struct DwarfSymbol DwarfSymbol;
typedef struct ENode ENode;
typedef struct ENodeList ENodeList;
typedef struct TemplateAction TemplateAction;
typedef struct EMemberInfo EMemberInfo;
typedef struct PCode PCode;
typedef struct ExceptSpecList ExceptSpecList;
typedef struct PCodeBlock PCodeBlock;
typedef struct EntryPoint EntryPoint;
typedef struct ExceptionTableHeader ExceptionTableHeader;
typedef struct FileOffsetInfo FileOffsetInfo;
typedef struct PCodeArg PCodeArg;
typedef struct Float Float;
typedef struct FuncArg FuncArg;
typedef struct GList GList;
typedef struct HashNameNode HashNameNode;
typedef struct SwitchInfo SwitchInfo;
typedef struct IROAddrRecord IROAddrRecord;
typedef struct IRODef IRODef;
typedef struct IROElmList IROElmList;
typedef struct IROExpr IROExpr;
typedef struct IROLinear IROLinear;
typedef struct IROList IROList;
typedef struct IROListNode IROListNode;
typedef struct IROLoop IROLoop;
typedef struct IROLoopInd IROLoopInd;
typedef struct IRONode IRONode;
typedef struct IROUse IROUse;
typedef struct CI_Statement CI_Statement;
typedef struct IARegister IARegister;
typedef struct CI_Switch CI_Switch;
typedef struct InlineXRef InlineXRef;
typedef struct InterruptGenerationRecord InterruptGenerationRecord;
typedef struct InterruptList InterruptList;
typedef struct CScopeNSIterator CScopeNSIterator;
typedef struct Loop Loop;
typedef struct Macro Macro;
typedef struct TokenStack TokenStack;
typedef struct FuncMatchArgs FuncMatchArgs;
typedef struct BigDeclInfo BigDeclInfo;
typedef struct NameSpace NameSpace;
typedef struct NameSpaceList NameSpaceList;
typedef struct NameSpaceName NameSpaceName;
typedef struct NameSpaceObjectList NameSpaceObjectList;
typedef struct PackedDeclInfo PackedDeclInfo;
typedef struct ObjBase ObjBase;
typedef struct ObjCCategory ObjCCategory;
typedef struct ObjCInfo ObjCInfo;
typedef struct ObjCMethod ObjCMethod;
typedef struct ObjCMethodArg ObjCMethodArg;
typedef struct ObjCMethodList ObjCMethodList;
typedef struct ObjCNamedArg ObjCNamedArg;
typedef struct ObjCProtocol ObjCProtocol;
typedef struct ObjCProtocolList ObjCProtocolList;
typedef struct ObjCSelector ObjCSelector;
typedef struct ObjEnumConst ObjEnumConst;
typedef struct ObjGenRelocation ObjGenRelocation;
typedef struct ObjGenSection ObjGenSection;
typedef struct ObjGenSymbolLink ObjGenSymbolLink;
typedef struct ObjMemberVar ObjMemberVar;
typedef struct ObjNameSpace ObjNameSpace;
typedef struct Object Object;
typedef struct ObjectList ObjectList;
typedef struct Operand Operand;
typedef struct PCLink PCLink;
typedef struct PCodeLabel PCodeLabel;
typedef struct InitExpr InitExpr;
typedef struct PendingObject PendingObject;
typedef struct TemplParam TemplParam;
typedef struct TemplateFriend TemplateFriend;
typedef struct TemplateMember TemplateMember;
typedef struct TokenStream TokenStream;
typedef struct QualNode QualNode;
typedef struct IROAssign IROAssign;
typedef struct TypeBitfield TypeBitfield;
typedef struct SOMInfo SOMInfo;
typedef struct NameSpaceLookupList NameSpaceLookupList;
typedef struct CScopeObjectIterator CScopeObjectIterator;
typedef struct SectionAttributeNode SectionAttributeNode;
typedef struct SectionRec SectionRec;
typedef struct SectionSymbolAttributes SectionSymbolAttributes;
typedef struct Statement Statement;
typedef struct DeclThing DeclThing;
typedef struct StructMember StructMember;
typedef struct SwitchCase SwitchCase;
typedef struct CodeLabelList CodeLabelList;
typedef struct TemplParamID TemplParamID;
typedef struct PSpecList PSpecList;
typedef struct TypeDeduce TypeDeduce;
typedef struct TemplateFunction TemplateFunction;
typedef struct DefAction DefAction;
typedef struct DeduceInfo DeduceInfo;
typedef struct ObjectTemplated ObjectTemplated;
typedef struct TemplateScopeState TemplateScopeState;
typedef struct TemplFuncInstance TemplFuncInstance;
typedef struct Type Type;
typedef struct TypeClass TypeClass;
typedef struct TemplClass TemplClass;
typedef struct TemplClassInst TemplClassInst;
typedef struct TypeEnum TypeEnum;
typedef struct TypeFunc TypeFunc;
typedef struct TypeIntegral TypeIntegral;
typedef struct TypeMemberFunc TypeMemberFunc;
typedef struct TypeMemberPointer TypeMemberPointer;
typedef struct TypePointer TypePointer;
typedef struct TypeObjCID TypeObjCID;
typedef struct TypeList TypeList;
typedef struct TypeStruct TypeStruct;
typedef struct TypeTemplDep TypeTemplDep;
typedef struct VClassList VClassList;
typedef struct VTable VTable;
typedef struct VarInfo VarInfo;
typedef struct VarRecord VarRecord;
typedef struct WeirdOperand WeirdOperand;
typedef union ENodeUnion ENodeUnion;
typedef union FloatStorage FloatStorage;
typedef union MWVector128 MWVector128;

#define TRUE 1
#define FALSE 0
#ifndef NULL
#define NULL 0
#endif

// A function a header (or IroPointerAnalysisADTs.c, which IroPointerAnalysis.c includes) defines
// inline. clang compiles the compiler's C with GNU89's inline rules, which make each an external
// definition: one in every unit that includes the header, and in IroPointerAnalysisADTs.c ones that
// use the file's statics, which C does not allow. There the copies are static.
#ifdef __MWERKS__
#define HEADER_INLINE inline
#else
#define HEADER_INLINE static inline
#endif
typedef unsigned char UInt8;
typedef signed char SInt8;
typedef unsigned short UInt16;
typedef short SInt16;
typedef unsigned long UInt32;
typedef long SInt32;
typedef unsigned char Boolean;

// The last named parameter of a variadic function that takes a 16-bit number (an opcode, a message
// ID, an OS error) before its arguments. C promotes a short there, so va_start on it is undefined:
// MSL's va_start handles it, and clang's is given an int (callers pass 16-bit values, and a use
// that could see more converts it back).
#ifdef __MWERKS__
typedef short VarArgShort;
#else
typedef int VarArgShort;
#endif

// The Mac Memory Manager's pointer and handle, shared by the compiler and the driver's MacEmul layer
typedef char* Ptr;
typedef Ptr* Handle;

typedef enum TemplParamType {
    TPT_TYPE = 0,
    TPT_NONTYPE,
    TPT_TEMPLATE
} TemplParamType;

struct TemplParamID {
    UInt16 index;
    UInt8 nindex;
    TemplParamType type;
};

typedef enum AccessType {
    ACCESSPUBLIC,
    ACCESSPRIVATE,
    ACCESSPROTECTED,
    ACCESSNONE
} AccessType;
// Object and type qualifiers (Object, DeclInfo and the types' qual). Q_IMPLICIT_WEAK marks what
// every unit may define (thunks, an inline function's local statics, a static const member
// initialized in its class): the backend gives it a weak symbol, as it does Q_WEAK. Q_TENTATIVE
// marks a C tentative definition. Q_ALIGNED_ values are a field, under Q_ALIGNED_MASK.
enum {
    Q_CONST = 1,
    Q_VOLATILE = 2,
    Q_CV = Q_CONST | Q_VOLATILE,
    Q_ASM = 4,
    Q_PASCAL = 8,
    Q_INLINE = 0x10,
    Q_REFERENCE = 0x20,
    Q_EXPLICIT = 0x40,
    Q_MUTABLE = 0x80,
    Q_VIRTUAL = 0x100,
    Q_FRIEND = 0x200,
    Q_IN = 0x400,
    Q_OUT = 0x800,
    Q_INOUT = 0x1000,
    Q_BYCOPY = 0x2000,
    Q_BYREF = 0x4000,
    Q_ONEWAY = 0x8000,
    Q_INLINE_DATA = 0x10000,
    Q_IMPLICIT_WEAK = 0x20000,
    Q_WEAK = 0x40000,
    Q_MANGLE_NAME = 0x80000,
    Q_IS_OBJC_ID = 0x100000,
    Q_RESTRICT = 0x200000,
    Q_IS_TEMPLATED = 0x400000,
    Q_TENTATIVE = 0x1000000,
    Q_ALIGNED_1 = 0x2000000,
    Q_ALIGNED_2 = 0x4000000,
    Q_ALIGNED_4 = 0x6000000,
    Q_ALIGNED_8 = 0x8000000,
    Q_ALIGNED_16 = 0xA000000,
    Q_ALIGNED_32 = 0xC000000,
    Q_ALIGNED_64 = 0x10000000,
    Q_ALIGNED_128 = 0x12000000,
    Q_ALIGNED_256 = 0x14000000,
    Q_ALIGNED_512 = 0x16000000,
    Q_ALIGNED_1024 = 0x18000000,
    Q_ALIGNED_2048 = 0x1A000000,
    Q_ALIGNED_4096 = 0x1C000000,
    Q_ALIGNED_8192 = 0x1E000000,
    Q_ALIGNED_MASK = 0x1E000000
};

// DeclInfo.exportflags: __declspec(internal), dllimport, dllexport
enum {
    EXPORT_FLAGS_INTERNAL = 0x10,
    EXPORT_FLAGS_IMPORT = 0x20,
    EXPORT_FLAGS_EXPORT = 0x40
};
#define FITS_IN_SHORT(value) ((value) == ((short)(value)))
#define FITS_IN_USHORT(value) ((value) == ((unsigned short)(value)))
#define FITS_IN_HI_SHORT(value) ((value) == (value & 0xFFFF0000))
#define FITS_IN_SHORT2(value) (((short)(value)) == (value))
#define HIGH_PART(value) ((short)(((value) >> 16) + (((value) >> 15) & 1)))
#define LOW_PART(value) ((short)(value))

// An interrupt function: CodeGen sets it under the interrupt options, and StackFrameEABI then adds
// its interrupt record.
#define Q_INTERRUPT 0x80000000
#pragma pack(push, 2)
struct HashNameNode {
    struct HashNameNode* next;
    SInt32 id;
    SInt16 hashval;
    char name[2];
};
// The register a global register variable lives in (`register int x __asm("r13")`): reg is 0 when
// none.
typedef struct ObjRegInfo { // NOTE: invented name
    char rclass;
    char reg;
} ObjRegInfo;

// The register classes: a signed char-sized enum (RegClass_Invalid is -1)
typedef enum RegClass {
    RegClass_Invalid = -1,
    RegClass_SPR = 0,
    RegClass_CRFIELD = 1,
    RegClass_VR = 2,
    RegClass_FPR = 3,
    RegClass_GPR = 4,
    RegClassMax = 5,
    // 6 and 7: the inline assembler's own operand classes, past RegClassMax.
    // 6 is a condition register bit (lt, gt, eq, so, un).
    RegClass_CRBIT = 6, // NOTE: invented name
    RegClass_DCR = 7
} RegClass;

struct CInt64 {
    SInt32 hi;
    UInt32 lo;
};
// The words come first so that an initializer gives them: the original's initialized doubles (Operands.c's
// one_point_zero, int_to_float_cc, uns_to_float_cc) are big-endian word pairs in its data.
union FloatStorage {
    int words[2];
    double value;
};
struct Float {
    FloatStorage data;
};
union MWVector128 {
    UInt8 uc[16];
    UInt16 us[8];
    UInt32 ul[4];
    float f[4];
};
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif
