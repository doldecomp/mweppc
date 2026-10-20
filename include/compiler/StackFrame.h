#ifndef COMPILER_STACKFRAME_H
#define COMPILER_STACKFRAME_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Boolean requires_frame;
extern Boolean makes_call;
extern Boolean uses_globals;
extern Boolean dynamic_stack;
extern Boolean StackFrame_has_vectors;
extern Object* dummyvaparam;
extern Object* dummyvalocal;
extern struct InterruptGenerationRecord* interrupt_routine_flags;

extern void init_stack_globals(Object* funcobj);
extern void init_frame_sizes(Boolean has_varargs);
extern void assign_local_memory(Object* obj);
extern void assign_locals_to_memory(ObjectList* first);
extern void compute_frame_sizes(void);
extern void generate_prologue(PCodeBlock* block, Boolean has_varargs);
extern void generate_epilogue(PCodeBlock* block, Boolean add_blr);
extern void allocate_dynamic_stack_space(
    Boolean isConstantSize, int resultReg, int backChainReg, SInt32 size
);
extern void change_dynamic_stack_space(int reg);
extern char* generate_traceback(SInt32 funcsize, char* funcname, SInt32* tbsize, Object* func);
extern void check_dynamic_aligned_frame(void);
extern void move_varargs_to_memory(void);
extern void assign_arguments_to_memory(Object* func, UInt8 oldStyleParameters, Boolean hasVarargs);
extern SInt32 set_out_param_displ(
    SInt32 displ, Type* type, Boolean passOnStack, SInt32* outvar, SInt32 size
);
extern SInt32 out_param_displ_to_offset(SInt32 displ);
extern Boolean needs_frame(void);
extern void update_out_param_size(SInt32 size);
extern void estimate_out_param_size(SInt32 size);
extern void update_frame_align(int align);
extern SInt32 local_offset_32(Object* obj);
extern SInt32 local_offset_lo(Object* obj, SInt32 offset);
extern SInt32 local_offset_ha(Object* obj, SInt32 offset);
extern SInt32 local_offset_16(Object* obj);
extern Boolean local_is_16bit_offset(Object* obj);
extern int local_base_register(Object* obj);
extern void no_frame_for_asm(void);
extern Boolean can_add_displ_to_local(Object* obj, int displ);
extern SInt32 get_alloca_alignment(void);
#ifdef __cplusplus
}
#endif

#endif
