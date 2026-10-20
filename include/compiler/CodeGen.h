#ifndef COMPILER_CODEGEN_H
#define COMPILER_CODEGEN_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// the stack of #pragma interrupt settings (copts.interruptList)
struct InterruptList { // NOTE: invented name
    struct InterruptList* next;
    struct InterruptGenerationRecord* info;
};

extern Macro vecM;
extern Object* gFunction;
extern CLabel* cleanreturnlabel;
extern Boolean needs_cleanup;
extern Statement* current_statement;
extern int has_catch_blocks;
extern int disable_optimizer;
extern SInt32 current_linenumber;
extern Boolean has_altivec_arrays;
extern short high_reg;
extern short low_reg;
extern short high_offset;
extern short low_offset;
extern short low_reg2;
extern short high_reg2;
extern Object* rt_cvt_fp2unsigned;
extern Object* rt_div2i;
extern Object* rt_div2u;
extern Object* rt_mod2i;
extern Object* rt_mod2u;
extern Object* rt_shr2i;
extern Object* rt_shr2u;
extern Object* rt_shl2i;
extern Object* rt_cvt_ull_dbl;
extern Object* rt_cvt_sll_dbl;
extern Object* rt_cvt_ull_flt;
extern Object* rt_cvt_sll_flt;
extern Object* rt_cvt_dbl_usll;
// the soft-float (SFPE) helpers
extern Object* rt_sfpe_d_add;
extern Object* rt_sfpe_d_sub;
extern Object* rt_sfpe_d_mul;
extern Object* rt_sfpe_d_div;
extern Object* rt_sfpe_d_feq;
extern Object* rt_sfpe_d_fne;
extern Object* rt_sfpe_d_flt;
extern Object* rt_sfpe_d_fgt;
extern Object* rt_sfpe_d_fle;
extern Object* rt_sfpe_d_fge;
extern Object* rt_sfpe_d_dtof;
extern Object* rt_sfpe_d_dtoi;
extern Object* rt_sfpe_d_dtou;
extern Object* rt_sfpe_d_dtoll;
extern Object* rt_sfpe_d_dtoull;
extern Object* rt_sfpe_d_itod;
extern Object* rt_sfpe_d_utod;
extern Object* rt_sfpe_d_lltod;
extern Object* rt_sfpe_d_ulltod;
extern Object* rt_sfpe_f_add;
extern Object* rt_sfpe_f_sub;
extern Object* rt_sfpe_f_mul;
extern Object* rt_sfpe_f_div;
extern Object* rt_sfpe_f_feq;
extern Object* rt_sfpe_f_fne;
extern Object* rt_sfpe_f_flt;
extern Object* rt_sfpe_f_fgt;
extern Object* rt_sfpe_f_fle;
extern Object* rt_sfpe_f_fge;
extern Object* rt_sfpe_f_ftod;
extern Object* rt_sfpe_f_ftoi;
extern Object* rt_sfpe_f_ftou;
extern Object* rt_sfpe_f_ftoll;
extern Object* rt_sfpe_f_ftoull;
extern Object* rt_sfpe_f_itof;
extern Object* rt_sfpe_f_utof;
extern Object* rt_sfpe_f_lltof;
extern Object* rt_sfpe_f_ulltof;

extern VarInfo* CodeGen_GetNewVarInfo(void);
extern Object* maketemporary(Type* type);
typedef void (*ArgumentProcessor)(Object* obj, short reg);
extern void process_arguments(ArgumentProcessor func, Boolean hasVarargs);
extern void move_assigned_argument(Object* obj, short reg);
extern void assign_labels(Statement* stmt);
extern int is_paired_single_operand(PCodeArg op);
extern Boolean set_vr_reg_flags(void);
// oldStyleParameters: the function was defined with a K&R parameter list (CFunc_Gen's); passed on
// to assign_arguments_to_memory, which never reads it
extern void CodeGen_Generator(
    Statement* statements, Object* func, UInt8 oldStyleParameters, Boolean callOnModuleBind
);
extern void CodeGen_GenVDispatchThunk(
    Object* thunkobj, Object* func, SInt32 this_delta, SInt32 returnDelta, SInt32 ctoroffset
);
extern void CodeGen_SetupRuntimeObjects(void);
extern Boolean CodeGen_ReInitRuntimeObjects(Boolean is_precompiler);
extern Boolean CodeGen_IsPublicRuntimeObject(Object* obj);
extern void CodeGen_SOMStub(Object* stub, Object* glue, Object* classdata, SInt32 offset);
extern void CodeGen_ParseDeclSpec(HashNameNode* identifier, DeclInfo* declinfo);
extern void CodeGen_ParsePragma(HashNameNode* name);
extern void CodeGen_UpdateObject(Object* object);
extern void CodeGen_UpdateBackEndOptions(void);
extern SInt32 CodeGen_objc_method_self_offset(ObjCMethod* meth);
extern SInt32 CodeGen_objc_method_sel_offset(ObjCMethod* meth);
extern SInt32 CodeGen_objc_method_arg_offset(ObjCMethod* meth, ObjCMethodArg* arg);
extern SInt32 CodeGen_objc_method_args_size(ObjCMethod* meth);
extern InterruptGenerationRecord* CodeGen_FindInterruptGenerationRecord(SInt16 key);
extern ENode* CodeGen_HandleIntrinsicCall(Object* func, ENodeList* arg_exprs);
extern ENode* CodeGen_HandleTypeCast(ENode* expr, Type* type, UInt32 qual);
extern short CodeGen_AssignCheck(
    const ENode* expr, const Type* type, Boolean convert, Boolean checkAccess
);
extern Boolean CodeGen_CollapseVectorExpression(ENode* expr, MWVector128* vec, Type* type);
extern void CodeGen_InsertSpecialMacros(void);
extern char* CodeGen_ExpandSpecialMacro(Macro* macro);
extern ENode* CodeGen_CheckExpr(ENode* expr);
extern void CodeGen_InitialSanityCheck(void);

#ifdef __cplusplus
}
#endif

#endif
