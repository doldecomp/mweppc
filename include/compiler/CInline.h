#ifndef COMPILER_CINLINE_H
#define COMPILER_CINLINE_H

#include "compiler/common.h"
#include "compiler/CFunc.h"
#include "compiler/tokens.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)

struct CInlineBackendOptions {
    SInt16 processor;
    UInt8 scheduling;
    UInt8 schedule_factor;
    UInt8 debuglisting;
    UInt8 profile;
    UInt8 traceback;
    UInt8 disable_registers;
    UInt8 fp_contract;
    UInt8 no_register_save_helpers;
    UInt8 unroll_speculative;
    SInt16 unroll_instr_limit;
    SInt16 unroll_factor_limit;
    UInt8 altivec_model;
    UInt8 altivec_vrsave;
    UInt8 function_align;
    UInt8 cats;
    UInt8 force_cats;
    SInt32 sdata_threshold;
    SInt32 sdata2_threshold;
    UInt8 pool_data;
    UInt8 common;
    UInt8 use_fp;
    UInt8 soft_float;
    UInt8 codegen_pic;
    UInt8 full_dwarf;
    UInt8 no_comment_records;
    UInt8 rel109_offset;
    struct ObjGenSection* textSection;
    struct ObjGenSection* dataSection;
    struct ObjGenSection* constSection;
    struct ObjGenSection* sdataSection;
    struct ObjGenSection* sconstSection;
    SInt32 sectionHeaderTableSize;
    struct SectionSymbolAttributes** sectionHeaderTable;
    struct InterruptList* interruptList;
    struct InterruptGenerationRecord* interruptOptions;
    UInt8 incompatible_return_small_structs;
    UInt8 create_file_object;
    UInt8 incompatible_sfpe_double_params;
    UInt8 ibm_stackframe;
    UInt8 gen_isel;
    UInt8 gen_fsel;
    UInt8 gecko_float_typecons;
#if VERSION >= VERSION_GC_2_0
    UInt8 disable_vector_frame;
#endif
    UInt8 opt_bcc_lr_ctr;
    UInt8 use_lmw_stmw;
    UInt8 report_heap_info;
    UInt8 misaligned_mem_access;
    UInt8 switch_tables;
    UInt8 prepare_compress;
    UInt8 min_struct_alignment;
    UInt8 unsafe_global_reg_vars;
    UInt8 ignore_global_reg_vars;
    UInt32 bc_range;
    UInt8 gprfloatcopy;
#if VERSION >= VERSION_GC_2_5
    UInt8 old_style_bitfields;
#endif
    UInt8 optimizationlevel;
    UInt8 optimizesize;
    UInt8 optimizewithasm;
    UInt8 commonsubs;
    UInt8 loopinvariants;
    UInt8 propagation;
    UInt8 deadstore;
    UInt8 strengthreduction;
    UInt8 strengthreductionstrict;
    UInt8 deadcode;
    UInt8 lifetimes;
    UInt8 unrolling;
    UInt8 vectorizeloops;
    UInt8 multiplepasses;
    UInt8 opt_pointer_analysis;
    UInt8 opt_pointer_analysis_mode;
    UInt8 unrollfactor;
    UInt8 unrollinstrfactor;
};

struct CI_Var {
    HashNameNode* name;
    Type* type;
    UInt32 qual;
    UInt8 sflags;
    SInt8 used;
    SInt8 unmodified;
};

struct CI_SwitchCase {
    short labelID;
    CInt64 min;
#if VERSION < VERSION_GC_2_0
    CInt64 max;
#endif
};

struct CI_Switch {
    ENode* expr;
    Type* exprtype;
    short defaultlabelID;
    short numcases;
    CI_SwitchCase cases[1];
};

struct CI_Statement {
    StatementType type;
    UInt8 flags;
    UInt16 value;
    SInt32 sourceoffset;
    HashNameNode* sourcefilepath;
    ExceptionAction* dobjstack;
    union {
        SInt16 statementnum;
        ENode* expr;
        struct {
            ENode* expr;
            SInt16 statementnum;
        } ifgoto;
        CI_Switch* switchdata;
        struct {
            void* data;
            SInt32 size;
        } asmdata;
    } u;
};

struct CI_FuncData {
    short numarguments;
    CI_Var* arguments;
    short numlocals;
    CI_Var* locals;
    short numstatements;
    CI_Statement* statements;
    struct CInlineBackendOptions options;
    FileOffsetInfo fileoffset;
    SInt32 symdecloffset;
    SInt32 functionbodyoffset;
    HashNameNode* functionbodypath;
    SInt32 symdeclend;
    UInt8 can_inline;
};

typedef enum CI_ActionType {
    CI_ActionInlineFunc = 0,
    CI_ActionMemberFunc = 1,
    CI_ActionTemplateFunc = 2,
    CI_ActionDefaultFunc = 3
} CI_ActionType;

struct CI_Action {
    struct CI_Action* next;
    Object* obj;
    union {
        struct {
            FileOffsetInfo fileoffset;
            TokenStream stream;
            TypeClass* tclass;
        } inlinefunc;
        struct {
            TemplClass* templ;
            TemplClassInst* inst;
            TemplateMember* tmemb;
        } memberfunc;
        struct {
            TemplateFunction* func;
            TemplFuncInstance* inst;
        } templatefunc;
    } u;
    CI_ActionType actiontype;
};

typedef enum CInlineCopyMode {
    CopyModeLocal,
    CopyModeGlobal,
    CopyModePack,
    CopyModeUnpack,
    CopyModeExpand
} CInlineCopyMode;

#pragma pack(pop)

extern CI_Action* cinline_tactionlist;

extern void CInline_Init(void);
extern SInt32 CInline_GetLocalID(Object* object);
extern Boolean CInline_ExpressionHasSideEffect(ENode* expr);
extern ENode* CInline_CopyExpression(ENode* expr, CInlineCopyMode mode);
extern void CInline_SerializeStatement(Statement* stmt);
extern Object* CInline_GetLocalObj(SInt32 id, Boolean inlineCopy);
extern SInt16 CInline_GetStatementNumber(Statement* first, Statement* stmt);
extern void CInline_PackIFunctionData(CI_FuncData* funcdata, Statement* stmt, Object* object);
extern void CInline_UnpackIFunctionData(
    Object* object, CI_FuncData* funcdata, Statement* firstStmt
);
extern void CInline_AddInlineFunctionAction(
    Object* object,
    TypeClass* tclass,
    FileOffsetInfo* fileoffset,
    TokenStream* stream,
    Boolean isTemplate
);
extern void CInline_AddTemplateFunctionAction(Object* object, void* func, void* inst);
extern void CInline_ObjectAddrRef(Object* object);
extern void CInline_GenFunc(Statement* stmt, Object* object, UInt8 oldStyleParameters);
extern Boolean CInline_CanFreeLHeap(void);
extern Boolean CInline_GenerateDeferredFuncs(void);
extern void CInline_Finish(void);

#ifdef __cplusplus
}
#endif

#endif
