#ifndef COMPILER_CPARSER_H
#define COMPILER_CPARSER_H

#include "compiler/common.h"
#include "compiler/enode.h"
#include "compiler/tokens.h"
#include "driver/CWPlugins.h"
#include "driver/oslib.h"
#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

// 2-byte packed: CompilerLinkerOptions.bc_range is at 0x5e, inlinemaxsize at 0x6e (0x6c in 2.0)
#pragma pack(push, 2)
// NOTE: invented names: AlignMode0_Mac68k, AlignMode1_Mac68k4byte, AlignMode2_PPC,
// AlignMode3_1Byte, AlignMode4_2Byte, AlignMode5_4Byte, AlignMode6_8Byte, AlignMode7_16Byte,
// AlignMode8_Packed (numbered by value)
// CompilerLinkerOptions.structalignment: the `#pragma options align=` modes and pack(1..16)
enum {
    AlignMode0_Mac68k,
    AlignMode1_Mac68k4byte,
    AlignMode2_PPC,
    AlignMode3_1Byte,
    AlignMode4_2Byte,
    AlignMode5_4Byte,
    AlignMode6_8Byte,
    AlignMode7_16Byte,
    AlignMode8_Packed
};
// CompilerLinkerOptions.processor
enum {
    CPU_PPC401,
    CPU_PPC403,
    CPU_PPC505,
    CPU_PPC509,
    CPU_PPC555,
    CPU_PPC601,
    CPU_PPC602,
    CPU_PPC603,
    CPU_PPC603e,
    CPU_PPC604,
    CPU_PPC604e,
    CPU_PPC740,
    CPU_PPC750,
    CPU_PPC801,
    CPU_PPC821,
    CPU_PPC823,
    CPU_PPC850,
    CPU_PPC860,
    CPU_PPC8240,
    CPU_PPC8260,
    CPU_Generic,
    CPU_PPC7400,
    CPU_PPCGEKKO,
    CPU_PPCELF,
    CPU_PPC7450,
    CPU_PPC556,
    CPU_PPC565
};

// NOTE: invented names: sdata_threshold, sdata2_threshold, common, use_fp, soft_float, full_dwarf,
// no_comment_records, sectionHeaderTableSize, sectionHeaderTable, dwarf_siblings,
// dwarf_relocations
// The compiler's options. A member no other evidence names has the name of the pragma that sets it
// (CPrep's compileroptions table gives every pragma's offset).
struct CompilerLinkerOptions {
    UInt8 littleendian;
    SInt16 processor;
    SInt8 scheduling;
    UInt8 schedule_factor;
    unsigned char debuglisting;
    char profile;
    Boolean traceback;
    char disable_registers;
    UInt8 fp_contract;
    UInt8 no_register_save_helpers;
    Boolean unroll_speculative;
    SInt16 unroll_instr_limit;
    SInt16 unroll_factor_limit;
    UInt8 altivec_model;
    UInt8 altivec_vrsave;
    UInt8 function_align;
    char cats;
    char force_cats;
    SInt32 sdata_threshold; // the EABI panel's sdata threshold
    int sdata2_threshold; // the EABI panel's sdata2 threshold
    UInt8 pool_data;
    UInt8 common; // the EABI panel's common setting
    char use_fp; // any floating point: the panel's -fp other than none
    // -fp soft: floating point in GPRs, through the SFPE helpers
    Boolean soft_float;
    // read by op_absolute_ha. No pragma of the compileroptions table has this offset and only
    // CMachine's inline-options restore writes it: there is no `#pragma pic`, so it stays 0.
    UInt8 codegen_pic;
    // set by InitDwarf (DWARF.c: the entries' sibling patches go into the .debug section rather
    // than a function's own list).
    UInt8 full_dwarf;
    // from the "PPC EABI Project" panel's byte 0x37; when set, the object file gets no .comment
    // records of the symbols' alignment and no CodeWarrior version record.
    char no_comment_records;
    // (#pragma rel109_offset, 0..3; signed: emit_relocation sign-extends it)
    SInt8 rel109_offset;
    // the sections `#pragma section` selects: code, then data, const, sdata and sconst (the section
    // record's typebits 2, 4, 8 and 0x10)
    struct ObjGenSection* textSection;
    struct ObjGenSection* dataSection;
    struct ObjGenSection* constSection;
    struct ObjGenSection* sdataSection;
    struct ObjGenSection* sconstSection;
    // each section's attributes, by section header index (CodeGen_PragmaPush, CodeGen_PragmaPop).
    SInt32 sectionHeaderTableSize;
    struct SectionSymbolAttributes** sectionHeaderTable;
    struct InterruptList* interruptList;
    struct InterruptGenerationRecord* interruptOptions;
    UInt8 incompatible_return_small_structs;
    Boolean create_file_object;
    UInt8 incompatible_sfpe_double_params;
    UInt8 ibm_stackframe;
    UInt8 gen_isel;
    UInt8 gen_fsel;
    UInt8 gecko_float_typecons;
    UInt8 disable_vector_frame;
    UInt8 opt_bcc_lr_ctr;
    UInt8 use_lmw_stmw;
    char report_heap_info;
    UInt8 misaligned_mem_access;
    UInt8 switch_tables;
    UInt8 prepare_compress;
    char min_struct_alignment;
    UInt8 unsafe_global_reg_vars;
    UInt8 ignore_global_reg_vars;
    // (#pragma bc_range)
    UInt32 bc_range;
    UInt8 gprfloatcopy;
#if VERSION >= VERSION_GC_2_5
    UInt8 old_style_bitfields;
#endif
    Boolean asmsemicolcomment;
    Boolean asmpoundcomment;
#if VERSION >= VERSION_GC_2_5
    // TODO: nothing reads or writes offsets 0x66 and 0x67 and no pragma names them, so their name
    // and type are unknown (2.0 has neither these bytes nor old_style_bitfields: every later member
    // is 2 bytes lower)
    UInt8 unknown66[2];
#endif
    StringPtr oldprefixname;
    SInt16 inlinelevel;
    SInt32 inlinemaxsize;
    SInt32 inlinemaxtotalsize;
    UInt8 inline_bottom_up;
    UInt8 cplusplus;
    UInt8 ecplusplus;
    UInt8 objective_c;
    SInt8 objc_strict;
    char ARMconform;
    char ARMscoping;
    Boolean checkprotos;
    char trigraphs;
    UInt8 onlystdkeywords;
    UInt8 enumsalwaysint;
    UInt8 ANSIstrict;
    UInt8 mpwc_relax;
    UInt8 mpwc_newline;
    Boolean ignore_oldstyle;
    Boolean cpp_extensions;
    UInt8 pointercast_lvalue;
    char RTTI;
    UInt8 delete_exception;
    Boolean test_dummy;
    Boolean oldalignment;
    UInt8 unsigned_char;
    char multibyteaware;
    UInt8 auto_inline;
    UInt8 defer_codegen;
    UInt8 direct_to_som;
    UInt8 som_env_check;
    UInt8 som_call_optimize;
    UInt8 booltruefalse;
    Boolean old_enum_mangler;
    UInt8 longlong;
    UInt8 longlong_enums;
    Boolean no_tfuncinline;
    UInt8 always_vdispatch;
    char flat_include;
    char syspath_once;
    char always_import;
    UInt8 simple_class_byval;
    char wchar_type;
    UInt8 vbase_ctor_offset;
    char vbase_abi_v2;
    char def_inherited;
    UInt8 template_patch;
    UInt8 template_friends;
    Boolean faster_pch_gen;
    UInt8 array_new_delete;
    UInt8 dollar_identifiers;
    Boolean def_inline_tfuncs;
    UInt8 arg_dep_lookup;
    char simple_prepdump;
    char line_prepdump;
    UInt8 fullpath_prepdump;
    Boolean old_mtemplparser;
    UInt8 suppress_init_code;
    Boolean reverse_bitfields;
    UInt8 c9x;
    Boolean float_constants;
    Boolean no_static_dtors;
    Boolean longlong_prepeval;
    UInt8 const_strings;
    Boolean dumpir;
    UInt8 experimental;
    UInt8 gcc_extensions;
    Boolean stdc_fp_contract;
    Boolean stdc_fenv_access;
    Boolean stdc_cx_limitedr;
    UInt8 old_argmatch;
    char optEH;
    Boolean optEH2;
    Boolean new_mangler;
    Boolean fullpath_file;
    Boolean microsoft_EH;
    // (#pragma no-ansi-lib-builtin and, inverted, stdc_libfuncname_redefined)
    Boolean no_ansi_lib_builtin;
    char warningerrors;
    char pedantic;
    UInt8 checkprecompflags;
    Boolean supress_warnings;
    Boolean warn_illpragma;
    UInt8 warn_emptydecl;
    Boolean warn_possunwant;
    char warn_unusedvar;
    char warn_unusedarg;
    UInt8 warn_extracomma;
    Boolean warn_hidevirtual;
    char warn_largeargs;
    Boolean warn_implicitconv;
    UInt8 warn_notinlined;
    SInt8 warn_structclass;
    Boolean warn_padding;
    Boolean warn_no_side_effect;
    Boolean warn_resultnotused;
    UInt8 warn_ptr_int_conv;
    UInt8 warn_uninitializedvar;
    signed char structalignment;
    UInt8 alignarraymembers;
    SInt8 dont_reuse_strings;
    UInt8 poolstrings;
    UInt8 explicit_zero_data;
    UInt8 readonly_strings;
    UInt8 exceptions;
    Boolean dddummy;
    UInt8 dontinline;
    UInt8 alwaysinline;
    char peephole;
    Boolean globaloptimizer;
    UInt8 sideeffects;
    UInt8 cfm_internal;
    UInt8 cfm_import;
    UInt8 cfm_export;
    UInt8 cfm_lib_export;
    char nosyminline;
    char force_active;
    unsigned char block_assign;
    Boolean has8bytebitfields;
    SInt8 optimizationlevel;
    UInt8 optimizesize;
    UInt8 optimizewithasm;
    // (CCompiler.c copies the driver's flag)
    UInt8 crippled;
    UInt8 commonsubs;
    UInt8 loopinvariants;
    UInt8 propagation;
    UInt8 deadstore;
    UInt8 strengthreduction;
    UInt8 strengthreductionstrict;
    UInt8 deadcode;
    UInt8 lifetimes;
    UInt8 logopts;
    UInt8 unrolling;
    Boolean vectorizeloops;
    UInt8 multiplepasses;
    UInt8 opt_pointer_analysis;
    UInt8 opt_pointer_analysis_mode;
    signed char unrollfactor;
    SInt8 unrollinstrfactor;
    UInt8 filesyminfo;
    // TODO: nothing reads or writes offset 0xfd and no pragma names it: name and type unknown
    UInt8 unknownfd;
    // DWARF entries get sibling attributes (the driver sets it).
    char dwarf_siblings;
    // DWARF references are written as relocations (the driver sets it).
    UInt8 dwarf_relocations;
    // (the path written to debug info is the full one)
    Boolean absolutepath;
    CWDataType appltype;
    CWDataType headtype;
    CWDataType texttype;
    struct HashNameNode* cursegmentname;
};
#pragma pack(pop)

// a tentative parse; an error raised while trychain points at it longjmps back with the state
// restored
struct ParserTryBlock {
    struct ParserTryBlock* next;
    jmp_buf jmpbuf;
    NameSpace* cscope_current;
    struct TypeClass* cscope_currentclass;
    Object* cscope_currentfunc;
    TemplStack* ctempl_curinstance;
    struct TStreamElement* cerror_locktoken;
    Boolean cscope_is_member_func;
};

typedef struct CallbackAction {
    struct CallbackAction* next;
    Object* obj;
    struct TypeClass* tclass;
} CallbackAction;

extern FileOffsetInfo cparser_fileoffset;
extern struct TStreamElement symdecltoken;
extern ParserTryBlock* trychain;
extern TypeStruct ptmstruct;
extern TypeStruct catchinfostruct;
extern Boolean in_assembler;
extern Boolean illegalimplicitconversion;
extern Boolean in_func_arglist;
extern NameSpaceName* newp_fobj;
extern NameSpaceName* newa_fobj;
extern NameSpaceName* delp_fobj;
extern NameSpaceName* dela_fobj;
extern Object* newh_func;
extern Object* delh_func;
extern Object* copy_func;
extern Object* clear_func;
extern Object* Rgtid_func;
extern Object* Rdync_func;
extern Object* rt_ptmf_cast;
extern Object* rt_ptmf_cmpr;
extern Object* rt_ptmf_test;
extern Object* rt_ptmf_call;
extern Object* rt_ptmf_scall;
extern Object* rt_ptmf_call4;
extern Object* rt_ptmf_scall4;
extern Object* rt_ptmf_null;
extern Object* rt_som_new;
extern Object* rt_som_newcheck;
extern Object* rt_som_check;
extern Object* rt_som_glue1;
extern Object* rt_som_glue2;
extern Object* rt_som_glue3;
extern Object* carr_func;
extern Object* cnar_func;
extern Object* darr_func;
extern Object* dnar_func;
extern Object* dnar3_func;
extern Object* Xgreg_func;
extern Object* Xthrw_func;
extern Object* Xicth_func;
extern Object* Xecth_func;
extern Object* Xunex_func;
extern CompilerLinkerOptions copts;
extern GList name_mangle_list;
extern HashNameNode* no_name_node;
extern HashNameNode* temp_argument_name;
extern HashNameNode* this_name_node;
extern HashNameNode* self_name_node;
extern HashNameNode* vptr_name_node;
extern CallbackAction* callbackactions;
extern Boolean fatalerrors;
extern Boolean anyerrors;
extern jmp_buf errorreturn;
extern char string[256];

extern Type sttemplexpr;
extern Type stillegal;
extern Type stvoid;
extern TypePointer void_ptr;
extern TypeFunc rt_func;

// The original's code uses a few addresses of the compiler's own data as data: it reads a type's
// integral byte without checking that the type is integral (for most other kinds that byte is the
// low byte of a pointer: TypePointer's target, TypeFunc's args, ...), and prints a global's address
// into a name. clang's build places the static objects elsewhere, so it reads these through the
// original executable's addresses (CParser_OriginalAddress). Heap addresses are the host's: the
// clang build allocates as the original does, and gets the original's under the same host.
#ifdef __MWERKS__
#define ORIGINAL_ADDRESS(ptr) ((UInt32)(ptr))
#define TYPE_INTEGRAL_UNCHECKED(ty) (TYPE_INTEGRAL(ty)->integral)
#else
#define ORIGINAL_ADDRESS(ptr) CParser_OriginalAddress(ptr)
#define TYPE_INTEGRAL_UNCHECKED(ty) CParser_UncheckedIntegral(TYPE(ty))
extern UInt32 CParser_OriginalAddress(const void* ptr);
extern UInt8 CParser_UncheckedIntegral(Type* type);
#endif

// the basic types of the lexer's atoms (atomtype: tksize names one)
enum {
    ATOM_VOID = 1,
    ATOM_CHAR,
    ATOM_UCHAR,
    ATOM_WCHAR,
    ATOM_SHORT,
    ATOM_USHORT,
    ATOM_INT,
    ATOM_UINT,
    ATOM_LONG,
    ATOM_ULONG,
    ATOM_LONGLONG,
    ATOM_ULONGLONG,
    ATOM_FLOAT,
    ATOM_SHORTDOUBLE,
    ATOM_DOUBLE,
    ATOM_LONGDOUBLE
};

// kind: 0 an intrinsic, 1 a function whose name is mangled, 2 a runtime helper
extern Object* CParser_NewRTFunc(Type* rettype, HashNameNode* name, UInt8 kind, int argcount, ...);
extern Boolean CParser_IsPublicRuntimeObject(Object* obj);
extern Boolean CParser_ReInitRuntimeObjects(Boolean is_precompiler);
extern void CParser_Setup(void);
extern void CParser_Cleanup(void);
extern short GetPrec(short token);
extern Boolean CParser_ParseOperatorName(
    short* token, Boolean allowConversion, Boolean parseConversionType
);
extern SInt32 CParser_GetUniqueID(void);
extern void CParser_PrintUniqueID(char* buf);
extern void CParser_SetUniqueID(SInt32 id);
extern HashNameNode* CParser_GetUniqueName(void);
extern HashNameNode* CParser_NameConcat(const char* a, const char* b);
extern HashNameNode* CParser_AppendUniqueName(char* prefix);
extern HashNameNode* CParser_AppendUniqueNameFile(char* prefix);
extern Boolean IsTempName(HashNameNode* name);
extern void CParser_UpdateObject(Object* object, DeclInfo* declinfo);
extern Object* CParser_NewObject(DeclInfo* declinfo);
extern Object* CParser_NewLocalDataObject(DeclInfo* declinfo, Boolean add_to_locals);
extern Object* CParser_NewGlobalDataObject(DeclInfo* declinfo);
extern Object* CParser_NewCompilerDefDataObject(void);
extern Object* CParser_NewFunctionObject(DeclInfo* declinfo);
extern Object* CParser_NewCompilerDefFunctionObject(void);
extern Object* CParser_NewAliasObject(Object* object, SInt32 offset);
extern FuncArg* CParser_NewFuncArg(void);
extern Type* atomtype(void);
extern Object* CParser_FindDeallocationObject(
    Type* type, FuncArg* args, Boolean isArray, Boolean isGlobal, Boolean* includeSize
);
extern short is_memberpointerequal(Type* a, Type* b);
extern short is_typeequal(Type* a, Type* b);
extern short iscpp_typeequal(Type* a, Type* b);
extern short CParser_CompareArgLists(FuncArg* a, FuncArg* b);
extern Boolean is_arglistsame(FuncArg* a, FuncArg* b);
extern short is_typesame(Type* a, Type* b);
extern Type* CParser_GetBoolType(void);
extern Type* CParser_GetWCharType(void);
extern short CParser_GetOperator(ENodeType t);
extern Boolean CParser_IsMoreCVQualified(UInt32 a, UInt32 b);
extern Boolean CParser_IsSameOrMoreCVQualified(UInt32 a, UInt32 b);
extern Boolean is_unsigned(Type* type);
extern StructMember* ismember(TypeStruct* tstruct, HashNameNode* name);
extern void appendmember(TypeStruct* tstruct, StructMember* member);
extern Boolean isdeclaration(
    UInt8 scanDeclarator, UInt8 requireName, UInt8 isNewExpression, short token
);
extern Boolean islookaheaddeclaration(void);
extern Type* CParser_ParseTypeID(UInt32* qual, Boolean* isTemplateName);
extern Boolean CParser_TryParamList(Boolean stopAtComma);
extern Type* CParser_RemoveTopMostQualifiers(Type* type, UInt32* qual);
extern UInt32 CParser_GetTypeQualifiers(Type* type, UInt32 qual);
extern UInt32 CParser_GetCVTypeQualifiers(Type* type, UInt32 qual);
#if VERSION >= VERSION_GC_2_5
extern Boolean CParser_IsReadOnly(Type* type, UInt32 qual);
#endif
extern Boolean CParser_IsConst(Type* type, UInt32 qual);
extern Boolean CParser_IsVolatile(Type* type, UInt32 qual);
extern Boolean is_const_object(Object* obj);
extern Boolean is_volatile_object(Object* obj);
extern Boolean CParserIsConstExpr(ENode* expr);
extern Boolean CParserIsVolatileExpr(ENode* expr);
extern Boolean CParser_HasInternalLinkage(const Object* obj);
extern Boolean CParser_HasInternalLinkage2(const Object* obj);
extern Boolean CParser_IsVirtualFunction(Object* obj, TypeClass** tclass, SInt32* index);
extern Boolean is_pascal_object(Object* obj);
extern void CParser_ParseAttribute(Type* type, DeclInfo* declinfo);
extern void CParser_ParseDeclSpec(DeclInfo* declinfo, Boolean unused);
extern Boolean CParser_CheckTemplateClassUsage(TemplClass* tmclass, Boolean reportError);
extern void CParser_GetDeclSpecs(DeclInfo* di, Boolean resolveMembers);
extern void CParser_RegisterNonGlobalClass(TypeClass* tclass);
extern void CParser_RegisterSingleExprFunction(Object* func, ENode* expr);
extern void CParser_RegisterDummyCtorFunction(Object* func, Object* obj);
extern Boolean CParser_IsAnonymousUnion(DeclInfo* di, Boolean allowStruct);
extern void CParser_CheckAnonymousUnion(DeclInfo* di, Boolean isLocal);
extern void CParser_NewCallBackAction(Object* obj, TypeClass* tclass);
extern void CParser_NewClassAction(TypeClass* tclass);
extern void CParser_CallBackAction(Object* obj);
extern Object* CParser_ParseObject(void);
extern void CParser_ParseGlobalDeclaration(void);
extern void cparser(void);

#ifdef __cplusplus
}
#endif

#endif
