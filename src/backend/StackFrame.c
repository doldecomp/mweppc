#include "compiler/StackFrame.h"
#include "compiler/BE_symbol.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CMachine.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CScope.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/types.h"
#include <stdio.h>
#include <string.h>

// The EABI stack frame: the back chain at 0(r1), LR saved at 4 in the caller's frame, an 8-byte
// linkage area, register save slots sized per class (a Gekko paired single takes 16 bytes),
// `#pragma interrupt` routines (compute_interrupt_frame, save_interrupt_registers,
// restore_interrupt_registers), the "<valocal>" area a varargs function's register arguments go to,
// and `__rtos_change_stack` (fixup_rtos_stacks).

#define ALIGN(thing, alignment) (~((alignment) - 1) & ((thing) + (alignment) - 1))
#define ALIGN_REMAINDER(thing, alignment) (ALIGN(thing, alignment) - (thing))

// NOTE: invented names: ObjClass, ObjClass_Local, ObjClass_LargeNear, ObjClass_LargeFar,
// ObjClassMax
// where assign_local_memory puts a local (its uid): within the frame's first 32K, at 0x8000 on
// (small objects past it) or at 0x10000 on
typedef enum ObjClass {
    ObjClass_Local,
    ObjClass_LargeNear,
    ObjClass_LargeFar,
    ObjClassMax
} ObjClass;

#pragma pack(push, 2)
// The traceback table generate_traceback builds: the XCOFF traceback table, with funcsize and
// namelen written big-endian (NOTE: member names after AIX <sys/debug.h>'s where they correspond,
// descriptive otherwise)
typedef struct Traceback {
    UInt8 zero[4]; // (the word of zeros that marks the table)
    UInt8 version;
    UInt8 lang; // (0 C, 9 C++)

    UInt8 globallink : 1;
    UInt8 is_eprol : 1;
    UInt8 has_tboff : 1; // set to 1
    UInt8 int_proc : 1;
    UInt8 has_ctl : 1;
    UInt8 tocless : 1;
    UInt8 fp_present : 1;
    UInt8 log_abort : 1;

    UInt8 int_hndl : 1;
    UInt8 name_present : 1; // set to 1
    UInt8 has_dynamic_stack : 1; // set to 1 if dynamic_stack
    UInt8 cl_dis_inv : 3;
    UInt8 uses_CRs : 1; // set to 1 if CRs used
    UInt8 needs_link_register : 1; // set to 1 if link register used

    UInt8 has_frame_size : 1; // set to 1 if frame_size is nonzero
    UInt8 fixup : 1; // set to 0
    UInt8 used_FPRs : 6; // stores non-volatile FPRs used

    UInt8 spare : 1; // set to 0
    UInt8 has_vec_info : 1; // set to 1 if VRs or vrsave used
    UInt8 used_GPRs : 6; // stores non-volatile GPRs used

    UInt8 fixedparms;
    UInt8 floatparms : 7;
    UInt8 parmsonstk : 1;

    SInt32 funcsize;
    SInt16 namelen;
    char name[0];
} Traceback;

typedef struct TracebackExtra {
    UInt8 used_VRs : 6;
    UInt8 has_vrsave_mask : 1;
    UInt8 is_varargs : 1;
    UInt8 vec_arg_count : 7;
    UInt8 has_vrsave_mask_or_used_VRs : 1;
} TracebackExtra;
#pragma pack(pop)

// a __rtos_change_stack's `stwu r1`
typedef struct FixupRTOSStack { // NOTE: invented name
    struct FixupRTOSStack* next;
    PCode* stwu;
} FixupRTOSStack;

Boolean requires_frame;
Boolean makes_call;
Boolean uses_globals;
Boolean dynamic_stack;
Boolean large_stack;
Boolean StackFrame_has_vectors; // the function keeps paired singles in its nonvolatile FPRs

// the statics (allocated in reverse declaration order)
static SInt32 out_param_alignment;
static SInt32 in_parameter_size;
static SInt32 in_param_alignment;
static SInt32 frame_alignment;
static SInt32 alloca_alignment;
static SInt32 linkage_area_size;
static SInt32 parameter_area_size;
static SInt32 parameter_area_size_estimate;
static SInt32 local_data_size;
static SInt32 local_data_limit;
static SInt32 large_data_near_size;
static SInt32 large_data_far_size;
static Boolean frame_sizes_computed; // NOTE: invented name
static ObjectList* local_objects[ObjClassMax];
static ObjectList* local_objects_tail[ObjClassMax];
static SInt32 non_volatile_save_offset[RegClassMax];
// NOTE: invented names: interrupt_class_offset, r11_save_offset, r0_save_offset,
// interrupt_saved_count, interrupt_saved_mask, save_slot_size, interrupt_save_offset,
// interrupt_save_size, interrupt_save_align, interrupt_spr_offset
// the save area of a `#pragma interrupt` routine
static SInt32
    interrupt_class_offset[RegClassMax]; // each class's save slots, from the start of the area
static SInt32 r11_save_offset; // where r11 and r0 are saved first (-1: r11 is not)
static SInt32 r0_save_offset;
static SInt32 interrupt_saved_count[RegClassMax];
static UInt32 interrupt_saved_mask[RegClassMax];
static SInt32 save_slot_size[RegClassMax]; // a saved register's slot (FPR: 16 with paired singles)
static SInt32 interrupt_save_offset; // the area's offset in the frame
static SInt32 interrupt_save_size;
static SInt32 interrupt_save_align;
static SInt32 interrupt_spr_offset; // where SRR0/SRR1, DAR and DSISR go in the area
static SInt32 VRSAVE_save_offset;
static SInt32 LR_save_offset;
static SInt32 nonvolatile_save_size;
static UInt32 vrsave_mask;
static SInt32 frame_size;
static SInt32 frame_size_estimate;
static SInt32 genuine_frame_size;
static Object* dummylocal;
static Boolean dynamic_align_stack;
static Boolean has_varargs;
static Boolean compressing_data_area;
static PCode* setup_caller_sp;
static PCode* align_instr1;
static PCode* align_instr2;
static short vrsave_register;
static PCode* loadvrsave;
static PCode* storevrsave;
Object* dummyvaparam;
Object* dummyvalocal; // the "<valocal>" area
// the function's `#pragma interrupt` record. TODO: Wii's name, for a flags word there; it is out of
// COMMON hash order here, so the original's name may differ
InterruptGenerationRecord* interrupt_routine_flags;
// the `stwu r1` of each __rtos_change_stack, patched with the outgoing parameter area
FixupRTOSStack* fixup_rtos_stacks;

static void insert_local_object(UInt8 oclass, Object* obj);
static void compress_data_area(void);
static UInt32 align_bits(UInt32 value, UInt8 bitcount);
static Boolean use_helper_function(char rclass);
static Boolean need_link_register(void);
static void call_helper_function(char* name, char rclass, int effect);
static void save_nonvolatile_FPRs(int reg, SInt32 offset);
static void save_nonvolatile_VRs(int reg, SInt32 offset);
static void restore_nonvolatile_FPRs(int reg, SInt32 offset);
static void restore_nonvolatile_VRs(int reg, SInt32 offset);
static void save_nonvolatile_GPRs(int reg, SInt32 offset);
static void restore_nonvolatile_GPRs(int reg, SInt32 offset);
static void do_allocate_dynamic_stack_space(
    Boolean isConstantSize, int sizeReg, int backChainReg, SInt32 size
);
static void allocate_valocal(void);
static void save_interrupt_registers(void);
static void restore_interrupt_registers(void);
static void compute_interrupt_frame(void);
void update_out_param_align(int align);
void update_in_param_align(int align);

void init_stack_globals(Object* funcobj) {
    RegClass rclass;
    ObjClass oclass;

    requires_frame = FALSE;
    makes_call = FALSE;
    uses_globals = FALSE;
    dynamic_stack = FALSE;
    large_stack = FALSE;
    StackFrame_has_vectors = FALSE;
    vrsave_register = -1;
    dynamic_align_stack = FALSE;
    compressing_data_area = FALSE;
    align_instr1 = NULL;
    align_instr2 = NULL;
    setup_caller_sp = NULL;
    dummyvaparam = NULL;
    loadvrsave = NULL;
    storevrsave = NULL;
    fixup_rtos_stacks = NULL;
    local_data_size = 0;
    local_data_limit = 0x2000;
    large_data_near_size = 0;
    large_data_far_size = 0;
    frame_size_estimate = 0;
    in_parameter_size = 0;
    parameter_area_size = 0;
    parameter_area_size_estimate = 0;
    frame_alignment = 8;
    out_param_alignment = 8;
    in_param_alignment = 8;
    alloca_alignment = 0;
    has_varargs = FALSE;
    frame_sizes_computed = FALSE;
    linkage_area_size = 0;
    frame_size = 0;
    genuine_frame_size = 0;
    nonvolatile_save_size = -1;
    VRSAVE_save_offset = -1;
    LR_save_offset = -1;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        non_volatile_save_offset[rclass] = -1;
    }

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        interrupt_class_offset[rclass] = -1;
        interrupt_saved_count[rclass] = 0;
        interrupt_saved_mask[rclass] = 0;
    }
    interrupt_save_align = 0;
    interrupt_save_offset = -1;
    save_slot_size[RegClass_SPR] = 0;
    save_slot_size[RegClass_CRFIELD] = 0;
    save_slot_size[RegClass_VR] = 16;
    save_slot_size[RegClass_GPR] = 4;
    save_slot_size[RegClass_FPR] = 8;
    r11_save_offset = -1;
    r0_save_offset = -1;
    interrupt_save_size = 0;
    linkage_area_size = 0;

    dummyvalocal = NULL;
    interrupt_routine_flags = NULL;
    dummyvaparam = NULL;
    dummylocal = NULL;

    for (oclass = 0; oclass < ObjClassMax; oclass++) {
        local_objects[oclass] = NULL;
        local_objects_tail[oclass] = NULL;
    }

    if (funcobj->qual & Q_INTERRUPT) {
        PPC_EABI_SetSection(funcobj, 0, FALSE, TRUE);
        interrupt_routine_flags = GetInterruptRoutineFlags(funcobj);
    }
}

void init_frame_sizes(Boolean has_varargs) {
    ObjectList* scan;
    Object* obj;
    SInt32 fixed_size; // NOTE: invented name
    SInt32 align;

    fixed_size = in_parameter_size + 8 + parameter_area_size_estimate;
    frame_size_estimate = fixed_size + 2460;
    for (scan = locals; scan; scan = scan->next) {
        obj = scan->object;
        align = CMach_AllocationAlignment(obj->type, obj->qual);
        frame_size_estimate = ALIGN(frame_size_estimate, align);
        frame_size_estimate += obj->type->size;
    }

    if (interrupt_routine_flags) {
        requires_frame = TRUE;
        frame_size_estimate += 512;
    }

    if (frame_size_estimate > 0x8000) {
        dynamic_stack = TRUE;
        large_stack = TRUE;
        requires_frame = TRUE;
    }

    local_data_limit = 0x8000 - (fixed_size + 2460);

    if (dynamic_stack) {
        requires_frame = TRUE;
        dummylocal = galloc(sizeof(Object));
        memclrw(dummylocal, sizeof(Object));
        dummylocal->type = (Type*)&stvoid;
        dummylocal->otype = OT_OBJECT;
        dummylocal->name = GetHashNameNode("<dummy>");
        dummylocal->datatype = DLOCAL;
        dummylocal->u.var.info = CodeGen_GetNewVarInfo();
        dummylocal->u.var.info->flags |= VarInfoFlag_HasMemory;
        dummylocal->u.var.info->noregister = TRUE;
    }

    if (dynamic_stack) {
        // r31 is the frame pointer, which a global register variable must not have taken
        if (n_real_registers[RegClass_GPR] > 31 &&
            reg_state[RegClass_GPR][31] == RegState_Retained) {
            PPCError_ErrorTerm(PPCERR_ASM_FRAME_POINTER, 31);
        }
        retain_register(NULL, RegClass_GPR, 31);
        _FP_ = 31;
    } else {
        _FP_ = 1;
    }
}

void assign_local_memory(Object* obj) {
    short align;
    VarInfo* vi;

    align = CMach_AllocationAlignment(obj->type, obj->qual);
    if (!compressing_data_area && (obj->u.var.info->flags & VarInfoFlag_HasMemory)) {
        return;
    }

    update_frame_align(align);
    if (local_data_size +
            (ALIGN_REMAINDER(local_data_size, align) + ALIGN(obj->type->size, align)) <
        local_data_limit) {
        local_data_size = ALIGN(local_data_size, align);
        vi = Registers_GetVarInfo(obj);
        vi->flags &= ~VarInfoFlag_InReg;
        vi->flags |= VarInfoFlag_HasMemory;
        obj->u.var.uid = local_data_size;
        local_data_size += ALIGN(obj->type->size, align);
        insert_local_object(ObjClass_Local, obj);
    } else if (compressing_data_area || obj->type->size <= 32) {
        large_data_near_size = ALIGN(large_data_near_size, align);
        vi = Registers_GetVarInfo(obj);
        vi->flags &= ~VarInfoFlag_InReg;
        vi->flags |= VarInfoFlag_HasMemory;
        obj->u.var.uid = 0x8000 + large_data_near_size;
        large_data_near_size += ALIGN(obj->type->size, align);
        insert_local_object(ObjClass_LargeNear, obj);
    } else {
        large_data_far_size = ALIGN(large_data_far_size, align);
        vi = Registers_GetVarInfo(obj);
        vi->flags &= ~VarInfoFlag_InReg;
        vi->flags |= VarInfoFlag_HasMemory;
        obj->u.var.uid = 0x10000 + large_data_far_size;
        large_data_far_size += ALIGN(obj->type->size, align);
        insert_local_object(ObjClass_LargeFar, obj);
    }
}

void assign_locals_to_memory(ObjectList* first) {
    ObjectList* list;
    Object* obj;
    int i;

    for (i = 1; i < 1024; i <<= 1) {
        for (list = first; list; list = list->next) {
            obj = list->object;
            if (Registers_GetVarInfo(obj)->used) {
                if ((Registers_GetVarInfo(obj) ? Registers_GetVarInfo(obj)->reg : 0) == 0) {
                    if (obj->type->size <= i) {
                        assign_local_memory(obj);
                    }
                }
            }
        }
    }

    for (list = first; list; list = list->next) {
        obj = list->object;
        if (Registers_GetVarInfo(obj)->used) {
            if ((Registers_GetVarInfo(obj) ? Registers_GetVarInfo(obj)->reg : 0) == 0) {
                assign_local_memory(list->object);
            }
        }

        if (obj->type &&
            IS_TYPE_ARRAY(obj->type) &&
            IS_TYPE_VECTOR(TYPE_POINTER(obj->type)->target)) {
            has_altivec_arrays = TRUE;
        }
    }
}

void compute_frame_sizes(void) {
    RegClass rclass;
    SInt32 altivec_size;
    SInt32 altivec_offset;
    SInt32 delta;

    save_slot_size[RegClass_FPR] = StackFrame_has_vectors ? 16 : 8;
    CError_ASSERT(913, alloca_alignment == 0 || alloca_alignment == frame_alignment);

    if (interrupt_routine_flags) {
        if (!(interrupt_routine_flags->flags & Interrupt_VRs) || !copts.altivec_model) {
            save_slot_size[RegClass_VR] = 0;
        }
        if (!(interrupt_routine_flags->flags & Interrupt_FPRs) ||
            copts.soft_float ||
            !copts.use_fp) {
            save_slot_size[RegClass_FPR] = 0;
        }
        compute_interrupt_frame();
        if (interrupt_save_size) {
            local_data_size += interrupt_save_size;
        }
    }

    update_asm_nonvolatile_registers();
    LR_save_offset = 4;
    non_volatile_save_offset[RegClass_FPR] =
        -(save_slot_size[RegClass_FPR] * used_nonvolatile_registers[RegClass_FPR]);
    non_volatile_save_offset[RegClass_GPR] =
        -(save_slot_size[RegClass_GPR] * used_nonvolatile_registers[RegClass_GPR] -
          non_volatile_save_offset[RegClass_FPR]);
    nonvolatile_save_size = -non_volatile_save_offset[RegClass_GPR];
    if (used_nonvolatile_registers[RegClass_CRFIELD] && !interrupt_routine_flags) {
        nonvolatile_save_size += 4;
        non_volatile_save_offset[RegClass_CRFIELD] = non_volatile_save_offset[RegClass_GPR] - 4;
    }
    VRSAVE_save_offset = -1;
    non_volatile_save_offset[RegClass_VR] = -1;

    if (copts.altivec_model) {
        if (vrsave_mask) {
            VRSAVE_save_offset = non_volatile_save_offset[RegClass_GPR] - 4;
            if (used_nonvolatile_registers[RegClass_CRFIELD] && !interrupt_routine_flags) {
                VRSAVE_save_offset -= 4;
            }
            nonvolatile_save_size += 4;
        }
        altivec_size = used_nonvolatile_registers[RegClass_VR] * 16;
        if (altivec_size > 0) {
            nonvolatile_save_size = ALIGN(nonvolatile_save_size + altivec_size, frame_alignment);
        }
    }

    if (parameter_area_size) {
        requires_frame = TRUE;
    }

    compress_data_area();
    if (interrupt_save_size) {
        update_frame_align(interrupt_save_align);
        local_data_size = ALIGN(local_data_size, frame_alignment);
        local_data_size += interrupt_save_size;
    }
    local_data_size = ALIGN(local_data_size, frame_alignment);
    nonvolatile_save_size = ALIGN(nonvolatile_save_size, frame_alignment);
    if (!requires_frame && (local_data_size + nonvolatile_save_size) <= 0) {
        CError_ASSERT(1021, !dynamic_align_stack);
        linkage_area_size = 0;
        frame_size = 0;
        genuine_frame_size = local_data_size + nonvolatile_save_size;
    } else {
        requires_frame = TRUE;
        if (parameter_area_size < 0) {
            parameter_area_size = 0;
        }
        parameter_area_size = ALIGN(parameter_area_size + 8, frame_alignment) - 8;
        if (large_stack) {
            CError_ASSERT(1035, !large_data_far_size);
            large_data_near_size += parameter_area_size;
            parameter_area_size = 0;
        }
        linkage_area_size = 8;
        frame_size =
            nonvolatile_save_size + (altivec_offset = parameter_area_size + 8 + local_data_size);
        if (copts.altivec_model && used_nonvolatile_registers[RegClass_VR]) {
            non_volatile_save_offset[RegClass_VR] = altivec_offset;
        }
        frame_size += ALIGN_REMAINDER(frame_size, 16);
        frame_size = ALIGN(frame_size, frame_alignment);
        genuine_frame_size = frame_size;
    }

    if (interrupt_routine_flags) {
        CError_ASSERT(1061, requires_frame);
        delta = parameter_area_size + 8 + local_data_size - interrupt_save_size;
        interrupt_save_offset += delta;
        for (rclass = 0; rclass < RegClassMax; rclass++) {
            if (interrupt_saved_count[rclass] > 0) {
                interrupt_class_offset[rclass] += delta;
            }
        }
        if (r0_save_offset != -1) {
            r0_save_offset += interrupt_class_offset[RegClass_GPR];
        }
        if (r11_save_offset != -1) {
            r11_save_offset += interrupt_class_offset[RegClass_GPR];
        }
    }

    if (!large_stack && frame_size > 0x7FFF) {
        CError_ErrorTerm(ERR_LOCAL_DATA_32K);
    }
    frame_sizes_computed = TRUE;
}

// NOTE: invented names: scratchReg, callerReg
static void allocate_new_frame(int scratchReg, int callerReg) {
    if (dynamic_align_stack) {
        emitpcode(PC_RLWINM, scratchReg, 1, 0, align_bits(frame_alignment, 1), 31);
        if (frame_size > 0x7FFF) {
            CError_FATAL(1151);
        } else {
            if (frame_size) {
                emitpcode(PC_SUBFIC, scratchReg, scratchReg, -frame_size);
            } else {
                emitpcode(PC_SUBFIC, scratchReg, scratchReg, -genuine_frame_size);
            }

            if (callerReg) {
                emitpcode(PC_MR, callerReg, 1);
            }

            emitpcode(PC_STWUX, 1, 1, scratchReg);
        }
    } else {
        if (frame_size > 0x7FFF) {
            CError_FATAL(1182);
        } else {
            emitpcode(PC_STWU, 1, 1, 0, -frame_size);
        }

        if (callerReg) {
            emitpcode(PC_MR, callerReg, 1);
        }
    }
}

void generate_prologue(PCodeBlock* block, Boolean has_varargs) {
    short vrsave_low;
    SInt32 vrbase;
    PCodeBlock* save_block;
    short vrsave_high;
    short reg;
    SInt32 offset;
    Statement stmt;
    char callerReg;
    FixupRTOSStack* fixup;
    Boolean needs_lr;
    SInt32 size;
    Statement* save_statement;

    save_block = pclastblock;
    needs_lr = need_link_register();
    reg = 1;
    offset = 0;
    save_statement = current_statement;
    stmt.sourceoffset = functionbodyoffset;
    current_statement = &stmt;
    pclastblock = block;

    if (fixup_rtos_stacks) {
        for (fixup = fixup_rtos_stacks; fixup; fixup = fixup->next) {
            size = ALIGN(parameter_area_size + 8, frame_alignment);
            CError_ASSERT(1223, fixup->stwu->args[2].kind == PCOp_IMMEDIATE);
            fixup->stwu->args[2].data.imm.value = -size;
        }
    }

    if (setup_caller_sp && setup_caller_sp->block) {
        CError_ASSERT(
            1241,
            !(setup_caller_sp->op == PC_MR &&
              setup_caller_sp->args[1].kind == PCOp_REGISTER &&
              setup_caller_sp->args[1].arg == RegClass_GPR &&
              setup_caller_sp->args[1].data.reg.reg == _FP_)
        );
        _CALLER_SP_ = setup_caller_sp->args[0].data.reg.reg;
        deletepcode(setup_caller_sp);
        setup_caller_sp = NULL;
    } else if (_CALLER_SP_ != _FP_) {
        _CALLER_SP_ = -1;
    }

    if (align_instr1 && align_instr1->block) {
        deletepcode(align_instr1);
        align_instr1 = NULL;
    }
    if (align_instr2 && align_instr2->block) {
        deletepcode(align_instr2);
        align_instr2 = NULL;
    }
    if (loadvrsave && loadvrsave->block) {
        deletepcode(loadvrsave);
        loadvrsave = NULL;
    }
    if (storevrsave && storevrsave->block) {
        deletepcode(storevrsave);
        storevrsave = NULL;
    }

    if (interrupt_routine_flags) {
        save_interrupt_registers();
        if (frame_size) {
            if (dynamic_align_stack) {
                reg = 11;
                offset = 0;
            } else {
                reg = 1;
                offset = frame_size;
            }
        }
    } else if (frame_size) {
        if (dynamic_align_stack) {
            reg = 12;
            offset = 0;
        } else {
            reg = 1;
            offset = frame_size;
        }
        if (!offset) {
            callerReg = 12;
        } else {
            callerReg = 0;
        }
        allocate_new_frame(11, callerReg);
        if (needs_lr) {
            emitpcode(PC_MFLR, 0);
            emitpcode(PC_STW, 0, reg, 0, offset + 4);
        }
    }

    if (used_nonvolatile_registers[RegClass_FPR]) {
        save_nonvolatile_FPRs(reg, offset);
    }
    if (used_nonvolatile_registers[RegClass_GPR]) {
        save_nonvolatile_GPRs(reg, offset);
    }

    if (_CALLER_SP_ > 0 && _CALLER_SP_ != _FP_) {
        emitpcode(PC_MR, _CALLER_SP_, 12);
        reg = _CALLER_SP_;
        offset = 0;
    }

    if (used_nonvolatile_registers[RegClass_CRFIELD] && !interrupt_routine_flags) {
        emitpcode(PC_MFCR, 0);
        emitpcode(PC_STW, 0, reg, 0, non_volatile_save_offset[RegClass_CRFIELD] + offset);
    }

    if (frame_size) {
        if (vrsave_mask) {
            emitpcode(PC_MFSPR, 0, 256);
            emitpcode(PC_STW, 0, reg, 0, VRSAVE_save_offset + offset);
            vrsave_register = 0;
        }
    } else {
        CError_ASSERT(1369, !dynamic_align_stack);
        if (vrsave_mask) {
            emitpcode(PC_MFSPR, vrsave_register, 256);
        }
    }

    if (vrsave_mask) {
        vrsave_high = vrsave_mask >> 16;
        vrsave_low = vrsave_mask;
        if (vrsave_mask == 0xFFFFFFFF) {
            emitpcode(PC_LI, 0, -1);
        } else {
            if (vrsave_high) {
                emitpcode(PC_ORIS, 0, vrsave_register, (UInt16)vrsave_high);
            }
            if (vrsave_low) {
                emitpcode(PC_ORI, 0, 0, (UInt16)vrsave_low);
            }
        }
        emitpcode(PC_MTSPR, 256, 0);
    }

    if (used_nonvolatile_registers[RegClass_VR]) {
        save_nonvolatile_VRs(1, 0);
    }

    if (dynamic_stack) {
        emitpcode(PC_MR, 31, 1);
    }

    if (large_stack) {
        do_allocate_dynamic_stack_space(TRUE, 11, 0, large_data_near_size);
    }

    block->flags |= fIsProlog;
    pclastblock = save_block;
    current_statement = save_statement;
}

void generate_epilogue(PCodeBlock* block, Boolean add_blr) {
    PCodeBlock* save_block;
    Boolean needs_lr;
    Statement* save_statement;
    Statement stmt;
    int reg;
    SInt32 offset;

    save_block = pclastblock;
    needs_lr = need_link_register();
    save_statement = current_statement;
    if (!save_statement) {
        stmt.sourceoffset = current_linenumber;
        current_statement = &stmt;
    }
    pclastblock = block;

    if (used_nonvolatile_registers[RegClass_VR]) {
        restore_nonvolatile_VRs(_FP_, 0);
    }

    if (dynamic_align_stack) {
        if (interrupt_routine_flags && dynamic_stack) {
            emitpcode(PC_MR, 8, _FP_);
            setpcodeflags(fSideEffects);
        }
        load_store_register(PC_LWZ, 10, 1, NULL, 0);
        setpcodeflags(fSideEffects);
        reg = 10;
        offset = 0;
    } else {
        if (dynamic_stack) {
            emitpcode(PC_MR, 10, _FP_);
            reg = 10;
        } else {
            reg = 1;
        }
        offset = frame_size;
    }

    if (used_nonvolatile_registers[RegClass_CRFIELD]) {
        load_store_register(
            PC_LWZ, 12, reg, NULL, non_volatile_save_offset[RegClass_CRFIELD] + offset
        );
        emitpcode(PC_MTCRF, 255, 12);
    }

    if (vrsave_mask) {
        if (!requires_frame) {
            emitpcode(PC_MTSPR, 256, vrsave_register);
        } else {
            emitpcode(PC_LWZ, 11, reg, 0, VRSAVE_save_offset + offset);
            emitpcode(PC_MTSPR, 256, 11);
        }
    }

    if (used_nonvolatile_registers[RegClass_FPR]) {
        restore_nonvolatile_FPRs(reg, offset);
    }
    if (used_nonvolatile_registers[RegClass_GPR]) {
        restore_nonvolatile_GPRs(reg, offset);
    }

    if (interrupt_routine_flags) {
        if (interrupt_routine_flags && dynamic_align_stack && dynamic_stack) {
            emitpcode(PC_MR, 10, 8);
            setpcodeflags(fSideEffects);
        }
        restore_interrupt_registers();
        if (add_blr) {
            emitpcode(PC_RFI);
            setpcodeflags(fIsVolatile);
        }
    } else {
        if (dynamic_align_stack) {
            if (needs_lr) {
                load_store_register(PC_LWZ, 0, reg, NULL, 4);
                emitpcode(PC_MTLR, 0);
            }
            emitpcode(PC_MR, 1, reg);
            setpcodeflags(fSideEffects);
        } else if (dynamic_stack) {
            if (needs_lr) {
                load_store_register(PC_LWZ, 10, 1, NULL, 0);
                setpcodeflags(fSideEffects);
                load_store_register(PC_LWZ, 0, 10, NULL, 4);
                emitpcode(PC_MR, 1, 10);
                setpcodeflags(fSideEffects);
                emitpcode(PC_MTLR, 0);
            } else {
                load_store_register(PC_LWZ, 1, 1, NULL, 0);
                setpcodeflags(fSideEffects);
            }
        } else if (frame_size > 0) {
            if (needs_lr) {
                emitpcode(PC_LWZ, 0, 1, 0, frame_size + 4);
                emitpcode(PC_MTLR, 0);
            }
            emitpcode(PC_ADDI, 1, 1, 0, frame_size);
            setpcodeflags(fSideEffects);
        }

        if (add_blr) {
            emitpcode(PC_BLR);
            setpcodeflags(fIsVolatile);
        }
    }

    block->flags |= fIsEpilogue;
    pclastblock = save_block;
    current_statement = save_statement;
}

static void load_base_offset(int dest_reg, int base_reg, SInt32 offset) {
    if (offset) {
        emitpcode(PC_ADDI, dest_reg, base_reg, 0, offset);
    } else {
        emitpcode(PC_MR, dest_reg, base_reg);
    }
}

// TODO: the register number written `32L - i`: the original converts i once for `32 - i` and the
// offset arithmetic (a long); its spelling is not known
static void save_nonvolatile_FPRs(int reg, SInt32 offset) {
    short i;

    offset += non_volatile_save_offset[RegClass_FPR];

    if (!use_helper_function(RegClass_FPR)) {
        for (i = 1; i <= used_nonvolatile_registers[RegClass_FPR]; i++) {
            if (StackFrame_has_vectors) {
                // a paired single's second half goes 8 bytes after its double
                if (used_nonvolatile_registers[RegClass_FPR] * save_slot_size[RegClass_FPR] +
                        offset <
                    0x1000) {
                    emitpcode(
                        PC_STFD,
                        32L - i,
                        reg,
                        NULL,
                        offset +
                            (used_nonvolatile_registers[RegClass_FPR] - i) *
                                save_slot_size[RegClass_FPR]
                    );
                    setpcodeflags(fIsVolatile);
                    emitpcode(
                        PC_PSQ_ST,
                        32L - i,
                        reg,
                        0,
                        offset + 8 +
                            (used_nonvolatile_registers[RegClass_FPR] - i) *
                                save_slot_size[RegClass_FPR],
                        0,
                        SPR_GQR0
                    );
                } else {
                    emitpcode(
                        PC_LI,
                        0,
                        offset + 8 +
                            (used_nonvolatile_registers[RegClass_FPR] - i) *
                                save_slot_size[RegClass_FPR]
                    );
                    setpcodeflags(fIsVolatile);
                    emitpcode(
                        PC_STFD,
                        32L - i,
                        reg,
                        NULL,
                        offset +
                            (used_nonvolatile_registers[RegClass_FPR] - i) *
                                save_slot_size[RegClass_FPR]
                    );
                    setpcodeflags(fIsVolatile);
                    emitpcode(PC_PSQ_STX, 32L - i, reg, 0, 0, SPR_GQR0);
                }
            } else {
                emitpcode(
                    PC_STFD,
                    32L - i,
                    reg,
                    NULL,
                    offset + (used_nonvolatile_registers[RegClass_FPR] - i) * 8
                );
            }
            setpcodeflags(fIsVolatile);
        }
    } else {
        load_base_offset(11, reg, offset + used_nonvolatile_registers[RegClass_FPR] * 8);
        call_helper_function("_savefpr_%d", RegClass_FPR, EffectRead);
    }
}

// TODO: the register number written `32L - i`: the original converts i once for `32 - i` and the
// offset arithmetic (a long); its spelling is not known
static void save_nonvolatile_VRs(int reg, SInt32 offset) {
    short i;

    offset += non_volatile_save_offset[RegClass_VR];

    if (!use_helper_function(RegClass_VR)) {
        for (i = 1; i <= used_nonvolatile_registers[RegClass_VR]; i++) {
            emitpcode(PC_LI, 0, offset + (used_nonvolatile_registers[RegClass_VR] - i) * 16);
            emitpcode(PC_STVX, 32L - i, reg, 0);
            setpcodeflags(fIsVolatile);
        }
    } else {
        load_base_offset(0, reg, offset + used_nonvolatile_registers[RegClass_VR] * 16);
        call_helper_function("_savevr%d", RegClass_VR, EffectRead);
    }
}

// TODO: the register number written `32L - i`: the original converts i once for `32 - i` and the
// offset arithmetic (a long); its spelling is not known
static void restore_nonvolatile_FPRs(int reg, SInt32 offset) {
    short i;

    offset += non_volatile_save_offset[RegClass_FPR];

    if (!use_helper_function(RegClass_FPR)) {
        for (i = 1; i <= used_nonvolatile_registers[RegClass_FPR]; i++) {
            if (StackFrame_has_vectors) {
                if (used_nonvolatile_registers[RegClass_FPR] * save_slot_size[RegClass_FPR] +
                        offset <
                    0x1000) {
                    emitpcode(
                        PC_PSQ_L,
                        32L - i,
                        reg,
                        0,
                        offset + 8 +
                            (used_nonvolatile_registers[RegClass_FPR] - i) *
                                save_slot_size[RegClass_FPR],
                        0,
                        SPR_GQR0
                    );
                    setpcodeflags(fIsVolatile);
                    load_store_register(
                        PC_LFD,
                        32L - i,
                        reg,
                        NULL,
                        offset +
                            (used_nonvolatile_registers[RegClass_FPR] - i) *
                                save_slot_size[RegClass_FPR]
                    );
                    setpcodeflags(fIsVolatile);
                } else {
                    emitpcode(
                        PC_LI,
                        0,
                        offset + 8 +
                            (used_nonvolatile_registers[RegClass_FPR] - i) *
                                save_slot_size[RegClass_FPR]
                    );
                    setpcodeflags(fIsVolatile);
                    emitpcode(PC_PSQ_LX, 32L - i, reg, 0, 0, SPR_GQR0);
                    setpcodeflags(fIsVolatile);
                    load_store_register(
                        PC_LFD,
                        32L - i,
                        reg,
                        NULL,
                        offset +
                            (used_nonvolatile_registers[RegClass_FPR] - i) *
                                save_slot_size[RegClass_FPR]
                    );
                    setpcodeflags(fIsVolatile);
                }
            } else {
                load_store_register(
                    PC_LFD,
                    32L - i,
                    reg,
                    NULL,
                    offset + (used_nonvolatile_registers[RegClass_FPR] - i) * 8
                );
                setpcodeflags(fIsVolatile);
            }
        }
    } else {
        load_base_offset(11, reg, offset + used_nonvolatile_registers[RegClass_FPR] * 8);
        call_helper_function("_restfpr_%d", RegClass_FPR, EffectWrite);
    }
}

// TODO: the register number written `32L - i`: the original converts i once for `32 - i` and the
// offset arithmetic (a long); its spelling is not known
static void restore_nonvolatile_VRs(int reg, SInt32 offset) {
    short i;

    offset += non_volatile_save_offset[RegClass_VR];

    if (!use_helper_function(RegClass_VR)) {
        for (i = 1; i <= used_nonvolatile_registers[RegClass_VR]; i++) {
            emitpcode(PC_LI, 0, offset + (used_nonvolatile_registers[RegClass_VR] - i) * 16);
            setpcodeflags(fIsVolatile);
            emitpcode(PC_LVX, 32L - i, reg, 0);
            setpcodeflags(fIsVolatile);
        }
    } else {
        load_base_offset(0, reg, offset + used_nonvolatile_registers[RegClass_VR] * 16);
        call_helper_function("_restvr%d", RegClass_VR, EffectWrite);
    }
}

static void save_nonvolatile_GPRs(int reg, SInt32 offset) {
    int i;

    offset += non_volatile_save_offset[RegClass_GPR];

    if (!use_helper_function(RegClass_GPR)) {
        SInt32 n;
        if (copts.use_lmw_stmw &&
            !copts.littleendian &&
            (((n = used_nonvolatile_registers[RegClass_GPR]) > 4) ||
             (copts.optimizesize && (n > 1)))) {
            emitpcode(PC_STMW, n - 1, 32 - n, reg, 0, offset);
        } else {
            for (i = 1; i <= used_nonvolatile_registers[RegClass_GPR]; i++) {
                emitpcode(
                    PC_STW,
                    32 - i,
                    reg,
                    0,
                    offset +
                        (used_nonvolatile_registers[RegClass_GPR] - i) *
                            save_slot_size[RegClass_GPR]
                );
            }
        }
    } else {
        load_base_offset(11, reg, offset + used_nonvolatile_registers[RegClass_GPR] * 4);
        call_helper_function("_savegpr_%d", RegClass_GPR, EffectRead);
    }
}

static void restore_nonvolatile_GPRs(int reg, SInt32 offset) {
    int i;

    offset += non_volatile_save_offset[RegClass_GPR];

    if (!use_helper_function(RegClass_GPR)) {
        SInt32 n;
        if (copts.use_lmw_stmw &&
            !copts.littleendian &&
            (((n = used_nonvolatile_registers[RegClass_GPR]) > 4) ||
             (copts.optimizesize && (n > 1)))) {
            emitpcode(PC_LMW, n - 1, 32 - n, reg, 0, offset);
            setpcodeflags(fIsVolatile);
        } else {
            for (i = 1; i <= used_nonvolatile_registers[RegClass_GPR]; i++) {
                emitpcode(
                    PC_LWZ,
                    32 - i,
                    reg,
                    0,
                    offset + (used_nonvolatile_registers[RegClass_GPR] - i) * 4
                );
                setpcodeflags(fIsVolatile);
            }
        }
    } else {
        load_base_offset(11, reg, offset + used_nonvolatile_registers[RegClass_GPR] * 4);
        call_helper_function("_restgpr_%d", RegClass_GPR, EffectWrite);
    }
}

// NOTE: invented names: sizeReg, backChainReg
static void do_allocate_dynamic_stack_space(
    Boolean isConstantSize, int sizeReg, int backChainReg, SInt32 size
) {
    load_store_register(PC_LWZ, backChainReg, 1, NULL, 0);
    if (isConstantSize) {
        size = ALIGN(size, frame_alignment);
        if (size < 0x8000) {
            emitpcode(PC_STWU, backChainReg, 1, 0, -size);
        } else {
            emitpcode(PC_LIS, sizeReg, 0, HIGH_PART(-size));
            if (LOW_PART(-size)) {
                emitpcode(PC_ADDI, sizeReg, sizeReg, 0, LOW_PART(-size));
            }
            emitpcode(PC_STWUX, backChainReg, 1, sizeReg);
            setpcodeflags(fIsVolatile | fSideEffects);
        }
    } else {
        emitpcode(PC_STWUX, backChainReg, 1, sizeReg);
        setpcodeflags(fIsVolatile | fSideEffects);
    }
}

// NOTE: invented names: resultReg, backChainReg
void allocate_dynamic_stack_space(
    Boolean isConstantSize, int resultReg, int backChainReg, SInt32 size
) {
    if (copts.altivec_model) {
        update_frame_align(16);
    }
    do_allocate_dynamic_stack_space(isConstantSize, resultReg, backChainReg, size);
    add_immediate(resultReg, 1, dummylocal, 0);
}

// __rtos_change_stack moves the stack pointer to REG, keeping room for the outgoing parameters
// (the size is only known once the frame is, so generate_prologue patches it)
void change_dynamic_stack_space(int reg) {
    SInt32 size;
    FixupRTOSStack* fixup;

    size = ALIGN(parameter_area_size + 8, frame_alignment);
    fixup = lalloc(sizeof(FixupRTOSStack));
    emitpcode(PC_STWU, 1, reg, 0, -size);
    setpcodeflags(fSideEffects);
    fixup->stwu = pclastblock->lastPCode;
    fixup->next = fixup_rtos_stacks;
    fixup_rtos_stacks = fixup;
    emitpcode(PC_MR, 1, reg);
    setpcodeflags(fSideEffects);
}

char* generate_traceback(SInt32 funcsize, char* funcname, SInt32* tbsize, Object* func) {
    char* work;
    short namelen;
    Traceback* buf;
    SInt32 bufsize;
    TracebackExtra* extra;
    Boolean is_varargs;
    UInt8 vec_count;
    FuncArg *args, *scan, *arg;
    Type* type;

    namelen = strlen(funcname);
    bufsize = ALIGN(
        sizeof(Traceback) + namelen + (dynamic_stack ? 1 : 0) +
            ((used_nonvolatile_registers[RegClass_VR] || vrsave_mask) ? sizeof(TracebackExtra) : 0),
        4
    );
    buf = lalloc(bufsize);
    memclrw(buf, bufsize);

    buf->version = 0;
    buf->lang = copts.cplusplus ? 9 : 0;
    buf->has_tboff = TRUE;
    buf->name_present = TRUE;
    if (dynamic_stack) {
        buf->has_dynamic_stack = TRUE;
    }
    if (used_nonvolatile_registers[RegClass_CRFIELD]) {
        buf->uses_CRs = TRUE;
    }
    if (need_link_register()) {
        buf->needs_link_register = TRUE;
    }
    if (frame_size) {
        buf->has_frame_size = TRUE;
    }
    buf->used_FPRs = used_nonvolatile_registers[RegClass_FPR];
    buf->used_GPRs = used_nonvolatile_registers[RegClass_GPR];
    buf->fixup = 0;
    buf->spare = 0;
    buf->has_vec_info = (used_nonvolatile_registers[RegClass_VR] || vrsave_mask) ? 1 : 0;
    // funcsize and namelen are written big-endian through the cursor
    work = (char*)&buf->funcsize;
    *((SInt32*)work) = CTool_EndianConvertWord32(funcsize);
    *((SInt16*)(work + 4)) = CTool_EndianConvertWord16(namelen);
    work += 6;
    strcpy(work, funcname);
    work += namelen;
    if (dynamic_stack) {
        *(work++) = 31;
    }

    if (vrsave_mask || used_nonvolatile_registers[RegClass_VR]) {
        extra = (TracebackExtra*)work;
        vec_count = 0;
        scan = TYPE_FUNC(func->type)->args;
        args = scan;
        while (scan && scan != &elipsis) {
            scan = scan->next;
        }
        is_varargs = scan == &elipsis;
        for (arg = args; arg; arg = arg->next) {
            if ((type = arg->type) && IS_TYPE_VECTOR(type)) {
                vec_count++;
            }
        }
        extra->used_VRs = used_nonvolatile_registers[RegClass_VR];
        extra->has_vrsave_mask = vrsave_mask != 0;
        extra->is_varargs = is_varargs;
        extra->vec_arg_count = vec_count;
        extra->has_vrsave_mask_or_used_VRs = vrsave_mask || used_nonvolatile_registers[RegClass_VR];
    }

    *tbsize = bufsize;
    return (char*)buf;
}

void check_dynamic_aligned_frame(void) {
    PCode* pc;

    if (used_nonvolatile_registers[RegClass_VR]) {
        update_frame_align(16);
        requires_frame = TRUE;
    } else if (need_link_register() &&
               interrupt_routine_flags &&
               (interrupt_routine_flags->flags & Interrupt_VRs)) {
        update_frame_align(16);
        requires_frame = TRUE;
    }

    if (frame_alignment > in_param_alignment) {
        dynamic_align_stack = TRUE;
        requires_frame = TRUE;
        CError_ASSERT(2215, _CALLER_SP_ != _FP_);
    } else {
        dynamic_align_stack = FALSE;
        if (setup_caller_sp && setup_caller_sp->block) {
            pc = makepcode(PC_MR, _CALLER_SP_, _FP_);
            insertpcodebefore(setup_caller_sp, pc);
            deletepcode(setup_caller_sp);
            setup_caller_sp = pc;
        }
        _CALLER_SP_ = _FP_;
    }

    vrsave_mask = 0;
    if (copts.altivec_model) {
        vrsave_mask = colored_vrs_as_vrsave(pcbasicblocks);
        if (!requires_frame && vrsave_mask) {
            vrsave_register = 11;
            loadvrsave = makepcode(PC_LWZ, 11, 1, 0, -4);
            appendpcode(prologue, loadvrsave);
            storevrsave = makepcode(PC_STW, 11, 1, 0, -4);
            appendpcode(epilogue, storevrsave);
        }
    }
}

void move_varargs_to_memory(void) {
    short reg;
    PCodeLabel* skip;
    PCodeLabel* save;

    has_varargs = TRUE;
    dummyvaparam = galloc(sizeof(Object));
    memclrw(dummyvaparam, sizeof(Object));

    dummyvaparam->type = TYPE(&stvoid);
    dummyvaparam->otype = OT_OBJECT;
    dummyvaparam->name = GetHashNameNode("<vaparam>");
    dummyvaparam->datatype = DLOCAL;
    dummyvaparam->u.var.info = CodeGen_GetNewVarInfo();
    dummyvaparam->u.var.uid = in_parameter_size;
    dummyvaparam->u.var.info->noregister = TRUE;
    Registers_GetVarInfo(dummyvaparam)->flags =
        (Registers_GetVarInfo(dummyvaparam)->flags & ~VarInfoFlag_ParamArea) |
        VarInfoFlag_ParamArea;
    local_base_register(dummyvaparam);

    // the FPR arguments are stored only when the caller set CR bit 6
    if (!copts.soft_float && copts.use_fp) {
        skip = makepclabel();
        save = makepclabel();
        emitpcode(PC_BF, 1, 2, skip);
        pcbranch(pclastblock, skip);
        pcbranch(pclastblock, save);
        branch_label(save);
        for (reg = 1; (int)reg <= 8; reg++) {
            emitpcode(
                PC_STFD, reg, local_base_register(dummyvalocal), dummyvalocal, (reg - 1) * 8 + 32
            );
            setpcodeflags(fIsPtrOp | fIsArgInit);
        }
        branch_label(skip);
    }

    for (reg = 3; (int)reg <= 10; reg++) {
        emitpcode(PC_STW, reg, local_base_register(dummyvalocal), dummyvalocal, (reg - 3) * 4);
        setpcodeflags(fIsPtrOp | fIsArgInit);
    }
    local_base_register(dummyvaparam);
}

void assign_arguments_to_memory(Object* func, UInt8 oldStyleParameters, Boolean hasVarargs) {
    SInt32 pos;
    ObjectList* list;
    Object* obj;
    Type* type;
    short gpr;
    short fpr;
    short vr;
    int chk;
    Boolean inMemory; // NOTE: invented name

    pos = 0;
    gpr = 3;
    fpr = 1;
    vr = 2;
    if (hasVarargs) {
        allocate_valocal();
    }

    for (list = arguments; list; list = list->next) {
        obj = list->object;
        type = obj->type;
        if (!IS_TYPE_VECTOR(type)) {
            inMemory = TRUE;
            if ((TYPE_IS_8BYTES(type) ||
                 (TYPE_IS_SOFT_FLOAT(type) &&
                  type->size != 4 &&
                  !copts.incompatible_sfpe_double_params)) &&
                (gpr % 2) == 0) {
                gpr++;
            }
            if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) && !TYPE_IS_SOFT_FLOAT(type)) {
                if (fpr <= 8) {
                    inMemory = FALSE;
                }
                fpr++;
            } else {
                if (gpr <= 10) {
                    inMemory = FALSE;
                }
                if (TYPE_IS_GPR_PAIR(type)) {
                    gpr += 2;
                } else if (TYPE_FITS_IN_REGISTER(type) ||
                           (TYPE_IS_SOFT_FLOAT(type) && type->size == 4)) {
                    gpr++;
                } else {
                    gpr += type->size >> 2;
                    if (type->size & 3) {
                        gpr++;
                    }
                }
            }

            if (inMemory) {
                obj->datatype = DLOCAL;
                obj->u.var.info = CodeGen_GetNewVarInfo();
                if (TYPE_IS_8BYTES(type) ||
                    (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) && type->size != 4)) {
                    pos = ALIGN(pos, 8);
                } else {
                    pos = ALIGN(pos, 4);
                }
                if (IS_TYPE_ARRAY(type) ||
                    IS_TYPE_NONVECTOR_STRUCT(type) ||
                    IS_TYPE_CLASS(type) ||
                    IS_TYPE_12BYTES_MEMBERPOINTER(type)) {
                    chk = CMach_ArgumentAlignment(type);
                    if (chk > 4) {
                        pos = ALIGN(pos, chk);
                        update_in_param_align(chk);
                    }
                }
                obj->u.var.uid = pos;
                Registers_GetVarInfo(obj)->flags =
                    (Registers_GetVarInfo(obj)->flags & ~VarInfoFlag_ParamArea) |
                    VarInfoFlag_ParamArea;
                if (!copts.littleendian &&
                    (IS_TYPE_INT(obj->type) || IS_TYPE_ENUM(obj->type)) &&
                    obj->type->size < 4) {
                    obj->u.var.uid += 4 - obj->type->size;
                }
                pos += obj->type->size;
            } else {
                obj->datatype = DLOCAL;
                obj->u.var.info = CodeGen_GetNewVarInfo();
                obj->u.var.uid = 0;
                assign_local_memory(obj);
            }
        } else {
            obj->u.var.info = CodeGen_GetNewVarInfo();
            obj->u.var.uid = 0;
            obj->datatype = DLOCAL;
            inMemory = TRUE;
            if (vr <= 13) {
                inMemory = FALSE;
            }
            if (inMemory) {
                pos = ALIGN(pos + 8, 16) - 8;
                obj->u.var.uid = pos;
                pos += 16;
                update_in_param_align(16);
                Registers_GetVarInfo(obj)->flags =
                    (Registers_GetVarInfo(obj)->flags & ~VarInfoFlag_ParamArea) |
                    VarInfoFlag_ParamArea;
            } else {
                assign_local_memory(obj);
                Registers_GetVarInfo(obj)->flags =
                    Registers_GetVarInfo(obj)->flags & ~VarInfoFlag_ParamArea;
            }
            vr++;
        }
    }

    in_parameter_size = (in_parameter_size < pos) ? pos : in_parameter_size;
    CError_ASSERT(2542, !dummyvaparam);
}

// Stores an outgoing argument's displacement in OUTVAR: DISPL for one in registers; for one on the
// stack, DISPL aligned for its type, returning the displacement after its SIZE bytes. NOTE:
// invented parameter names
SInt32 set_out_param_displ(
    SInt32 displ, Type* type, Boolean passOnStack, SInt32* outvar, SInt32 size
) {
    int argAlign;

    if (!passOnStack) {
        *outvar = displ;
        return displ;
    }

    if (TYPE_IS_8BYTES(type) || (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) && type->size != 4)) {
        update_out_param_align(8);
        displ = ALIGN(displ, 8);
    } else {
        displ = ALIGN(displ, 4);
    }

    if (IS_TYPE_VECTOR(type)) {
        update_out_param_align(16);
        displ = ALIGN(displ + 8, 16) - 8;
    } else if (IS_TYPE_ARRAY(type) ||
               IS_TYPE_NONVECTOR_STRUCT(type) ||
               IS_TYPE_CLASS(type) ||
               IS_TYPE_12BYTES_MEMBERPOINTER(type)) {
        argAlign = CMach_ArgumentAlignment(type);
        if (argAlign > 4) {
            displ = ALIGN(displ + 8, argAlign) - 8;
            update_in_param_align(argAlign);
        }
    }

    *outvar = displ;
    displ += size;
    return displ;
}

SInt32 out_param_displ_to_offset(SInt32 displ) {
    return displ + 8;
}

Boolean needs_frame(void) {
    return (frame_size > 0) || requires_frame;
}

void update_out_param_size(SInt32 size) {
    if (size < 0) {
        size = 0;
    }
    if (parameter_area_size < size) {
        parameter_area_size = size;
    }
}

void estimate_out_param_size(SInt32 size) {
    if (parameter_area_size_estimate < size) {
        parameter_area_size_estimate = size;
    }
}

void update_out_param_align(int align) {
    if (out_param_alignment < align) {
        out_param_alignment = align;
    }
    update_frame_align(align);
}

void update_in_param_align(int align) {
    if (in_param_alignment < align) {
        in_param_alignment = align;
    }
}

void update_frame_align(int align) {
    if (frame_alignment < align) {
        frame_alignment = align;
    }
}

// TODO: localsbase and parametersbase written out here: with the calls, -inline auto inlines this
// function into local_offset_lo and the others, which call it in the original
SInt32 local_offset_32(Object* obj) {
    short align;
    SInt32 offset;
    SInt32 base;

    if (obj->u.var.info->flags & VarInfoFlag_ParamArea) {
        align = CMach_ArgumentAlignment(obj->type);
    } else {
        align = CMach_AllocationAlignment(obj->type, obj->qual);
    }

    offset = obj->u.var.uid;
    if (offset > 0x7FFF) {
        offset = 0x8000 - offset - ALIGN(obj->type->size, align);
    }

    if (obj->u.var.info->flags & VarInfoFlag_ParamArea) {
        if (local_base_register(obj) == _FP_ && frame_size) {
            base = genuine_frame_size + 8;
        } else {
            base = 8;
        }
        return offset + base;
    }

    if (large_stack) {
        base = 0;
    } else if (frame_sizes_computed) {
        base = parameter_area_size;
    } else {
        base = parameter_area_size_estimate;
    }
    if (frame_size || dynamic_align_stack) {
        base += linkage_area_size;
    } else {
        base -= genuine_frame_size;
    }
    return offset + base;
}

SInt32 local_offset_lo(Object* obj, SInt32 offset) {
    SInt32 combo = offset + local_offset_32(obj);
    return LOW_PART(combo);
}

SInt32 local_offset_ha(Object* obj, SInt32 offset) {
    SInt32 combo = offset + local_offset_32(obj);
    return HIGH_PART(combo);
}

SInt32 local_offset_16(Object* obj) {
    SInt32 offset32 = local_offset_32(obj);
    SInt32 offset16 = (SInt16)offset32;
    CError_ASSERT(2799, offset32 == offset16);
    return offset16;
}

Boolean local_is_16bit_offset(Object* obj) {
    SInt32 offset32 = local_offset_32(obj);
    SInt16 offset16 = (SInt16)offset32;
    return offset32 == offset16;
}

int local_base_register(Object* obj) {
    SInt32 reg;

    if (obj->u.var.info->flags & VarInfoFlag_ParamArea) {
        if (coloring && _CALLER_SP_ == -1) {
            reg = used_virtual_registers[RegClass_GPR]++;
            _CALLER_SP_ = reg;
            setup_caller_sp = makepcode(PC_LWZ, _CALLER_SP_, 1, 0, 0);
            appendpcode(prologue, setup_caller_sp);
        }
        return _CALLER_SP_;
    }
    return _FP_;
}

static UInt32 align_bits(UInt32 value, UInt8 bitcount) {
    UInt32 base = bitcount ? 32 : 31;
    switch (value) {
        case 0x0002:
            return base - 1;
        case 0x0004:
            return base - 2;
        case 0x0008:
            return base - 3;
        case 0x0010:
            return base - 4;
        case 0x0020:
            return base - 5;
        case 0x0040:
            return base - 6;
        case 0x0080:
            return base - 7;
        case 0x0100:
            return base - 8;
        case 0x0200:
            return base - 9;
        case 0x0400:
            return base - 10;
        case 0x0800:
            return base - 11;
        case 0x1000:
            return base - 12;
        case 0x2000:
            return base - 13;
        default:
            CError_FATAL(2891);
            return base - 4;
    }
}

Boolean is_large_frame(void) {
    CError_ASSERT(2906, frame_size != -1);
    return large_stack;
}

void no_frame_for_asm(void) {
    frame_size = 0;
}

Boolean can_add_displ_to_local(Object* obj, int displ) {
    if (obj->datatype != DLOCAL) {
        return FALSE;
    }

    if (local_offset_32(obj) == (short)local_offset_32(obj)) {
        if ((displ + local_offset_32(obj)) == (short)(displ + local_offset_32(obj))) {
            return TRUE;
        }
    }

    return FALSE;
}

SInt32 get_alloca_alignment(void) {
    SInt32 align = frame_alignment;
    if (copts.altivec_model) {
        align = ALIGN(align, 16);
    }

    if (!alloca_alignment) {
        alloca_alignment = align;
    } else {
        CError_ASSERT(2962, alloca_alignment == align);
    }

    return align_bits(align, 0);
}

static Boolean use_helper_function(char rclass) {
    if (copts.no_register_save_helpers) {
        return FALSE;
    }

    switch (rclass) {
        case RegClass_GPR:
            if (copts.use_lmw_stmw && !copts.littleendian) {
                return FALSE;
            }
            return (used_nonvolatile_registers[RegClass_GPR] > 4) ||
                (copts.optimizesize && used_nonvolatile_registers[RegClass_GPR] > 2);
            break;
        case RegClass_FPR:
            if (StackFrame_has_vectors) {
                return FALSE;
            }
            return (used_nonvolatile_registers[RegClass_FPR] > 3) ||
                (copts.optimizesize && used_nonvolatile_registers[RegClass_FPR] > 2);
            break;
        case RegClass_VR:
            return (used_nonvolatile_registers[RegClass_VR] > 3) ||
                (copts.optimizesize && used_nonvolatile_registers[RegClass_VR] > 2);
            break;
        default:
            CError_FATAL(2999);
            return FALSE;
    }
}

static Boolean need_link_register(void) {
    if (makes_call) {
        return TRUE;
    }

    return use_helper_function(RegClass_FPR) ||
        use_helper_function(RegClass_GPR) ||
        use_helper_function(RegClass_VR);
}

static void call_helper_function(char* name, char rclass, int effect) {
    char str[32];
    Object* func;
    NameSpace* save_scope;
    PCode* pc;
    int extra_args;
    PCodeArg* arg;
    short i;

    extra_args = 1;
    if (rclass == RegClass_VR) {
        extra_args = 2;
    }

    sprintf(str, name, 32 - used_nonvolatile_registers[rclass]);

    save_scope = cscope_current;
    cscope_current = cscope_root;
    func = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    cscope_current = save_scope;

    func->name = GetHashNameNodeExport(str);
    MakeSymbolTableEntry(func);

    pc = makepcode(PC_BL, extra_args + used_nonvolatile_registers[rclass], func, 0);
    for (i = 1, arg = &pc->args[1]; i <= used_nonvolatile_registers[rclass]; i++, arg++) {
        arg->kind = PCOp_REGISTER;
        arg->arg = rclass;
        arg->data.reg.reg = n_real_registers[rclass] - i;
        arg->data.reg.effect = effect;
    }

    if (rclass == RegClass_VR) {
        arg[1].kind = PCOp_REGISTER;
        arg[1].arg = RegClass_GPR;
        arg[1].data.reg.reg = 12;
        arg[1].data.reg.effect = EffectWrite;
        arg[2].kind = PCOp_REGISTER;
        arg[2].arg = RegClass_GPR;
        arg[2].data.reg.reg = 0;
        arg[2].data.reg.effect = EffectRead;
    } else {
        arg[1].kind = PCOp_REGISTER;
        arg[1].arg = RegClass_GPR;
        arg[1].data.reg.reg = 11;
        arg[1].data.reg.effect = EffectRead;
    }

    appendpcode(pclastblock, pc);
    setpcodeflags(fSideEffects);
}

static SInt32 nearest_power_of_two(SInt32 n) {
    SInt32 power = 1;
    do {
        power <<= 1;
    } while (power && power < n);

    CError_ASSERT(3070, power != 0);
    return power;
}

static void compress_data_area(void) {
    SInt32 param_size; // NOTE: invented name
    ObjectList* list;
    Object* obj;
    PCodeBlock* block;
    PCode* pc;
    int i;

    compressing_data_area = TRUE;

    if (large_stack) {
        param_size = 0;
    } else if (parameter_area_size < 0) {
        param_size = 0;
    } else {
        param_size = parameter_area_size;
    }
    local_data_limit = 0x8000 -
        ALIGN(ALIGN(param_size + interrupt_save_size + 8, frame_alignment) + nonvolatile_save_size +
                  in_parameter_size,
              frame_alignment);

    if (local_objects_tail[ObjClass_Local]) {
        if (local_objects[ObjClass_LargeNear]) {
            local_objects_tail[ObjClass_Local]->next = local_objects[ObjClass_LargeNear];
            local_objects_tail[ObjClass_Local] = local_objects_tail[ObjClass_LargeNear];
        }
        if (local_objects[ObjClass_LargeFar]) {
            local_objects_tail[ObjClass_Local]->next = local_objects[ObjClass_LargeFar];
            local_objects_tail[ObjClass_Local] = local_objects_tail[ObjClass_LargeFar];
        }
    } else if (local_objects_tail[ObjClass_LargeNear]) {
        local_objects[ObjClass_Local] = local_objects[ObjClass_LargeNear];
        local_objects_tail[ObjClass_Local] = local_objects_tail[ObjClass_LargeNear];
        if (local_objects[ObjClass_LargeFar]) {
            local_objects_tail[ObjClass_Local]->next = local_objects[ObjClass_LargeFar];
            local_objects_tail[ObjClass_Local] = local_objects_tail[ObjClass_LargeFar];
        }
    } else {
        local_objects[ObjClass_Local] = local_objects[ObjClass_LargeFar];
        local_objects_tail[ObjClass_Local] = local_objects_tail[ObjClass_LargeFar];
    }

    for (list = local_objects[ObjClass_Local]; list; list = list->next) {
        Registers_GetVarInfo(list->object)->used = FALSE;
    }

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (pc = block->firstPCode; pc; pc = pc->nextPCode) {
            for (i = 0; i < pc->argCount; i++) {
                if (pc->args[i].kind == PCOp_MEMORY &&
                    pc->args[i].data.mem.obj &&
                    pc->args[i].data.mem.obj->datatype == DLOCAL) {
                    Registers_GetVarInfo(pc->args[i].data.mem.obj)->used = TRUE;
                }
            }
        }
    }

    local_data_size = 0;
    large_data_near_size = 0;
    large_data_far_size = 0;

    for (list = local_objects[ObjClass_Local]; list; list = list->next) {
        obj = list->object;
        if (Registers_GetVarInfo(obj)->used) {
            assign_local_memory(obj);
        }
    }
}

static void insert_local_object(UInt8 oclass, Object* obj) {
    ObjectList* list;

    if (!compressing_data_area) {
        list = lalloc(sizeof(ObjectList));
        memclrw(list, sizeof(ObjectList));
        list->object = obj;
        if (!local_objects[oclass]) {
            local_objects[oclass] = list;
        }
        if (local_objects_tail[oclass]) {
            local_objects_tail[oclass]->next = list;
        }
        local_objects_tail[oclass] = list;
    }
}

// a varargs function's register arguments are stored in "<valocal>", the first local: 8 GPRs, and
// 8 FPRs after them when the FPU is used
static void allocate_valocal(void) { // NOTE: invented name
    int size = 0;

    if (copts.soft_float || !copts.use_fp) {
        size += 0x20;
    } else {
        size += 0x60;
    }
    dummyvalocal = lalloc(sizeof(Object));
    memclrw(dummyvalocal, sizeof(Object));
    dummyvalocal->type = CDecl_NewArrayType(TYPE(&stchar), size);
    dummyvalocal->otype = OT_OBJECT;
    dummyvalocal->name = GetHashNameNode("<valocal>");
    dummyvalocal->datatype = DLOCAL;
    dummyvalocal->u.var.uid = 0;
    dummyvalocal->u.var.info = CodeGen_GetNewVarInfo();
    CError_ASSERT(3218, local_data_size == 0);
    assign_local_memory(dummyvalocal);
}

// NOTE: invented names: save_interrupt_registers, offset
// a `#pragma interrupt` routine's prologue: the frame, r0 and r11 (and the frame's realignment),
// CTR, XER, CR, LR and the MSR, the scratch registers it uses, and SRR0/SRR1, DAR and DSISR when
// asked for
static void save_interrupt_registers(void) {
    SInt32 offset;
    int i;
    int reg;

    emitpcode(PC_STWU, 1, 1, 0, -frame_size);
    setpcodeflags(fIsPtrOp | fIsArgInit);
    if (dynamic_align_stack) {
        emitpcode(PC_STW, 0, 1, 0, r0_save_offset);
        setpcodeflags(fIsPtrOp | fIsArgInit);
        emitpcode(PC_STW, 11, 1, 0, r11_save_offset);
        setpcodeflags(fIsPtrOp | fIsArgInit);
        emitpcode(PC_RLWINM, 0, 1, 0, align_bits(frame_alignment, 1), 31);
        setpcodeflags(fIsArgInit);
        emitpcode(PC_NEG, 0, 0);
        setpcodeflags(fIsArgInit);
        emitpcode(PC_ADDI, 11, 1, 0, frame_size);
        setpcodeflags(fIsArgInit);
        emitpcode(PC_STWUX, 11, 1, 0);
        setpcodeflags(fIsArgInit);
        emitpcode(PC_LWZ, 0, 11, 0, r0_save_offset - frame_size);
        setpcodeflags(fIsArgInit);
        emitpcode(PC_STW, 0, 1, 0, r0_save_offset);
        setpcodeflags(fIsArgInit);
        emitpcode(PC_LWZ, 0, 11, 0, r11_save_offset - frame_size);
        setpcodeflags(fIsArgInit);
        emitpcode(PC_STW, 0, 1, 0, r11_save_offset);
        setpcodeflags(fIsArgInit);
    } else if (r11_save_offset != -1) {
        emitpcode(PC_STW, 0, 1, 0, r0_save_offset);
        setpcodeflags(fIsArgInit);
        emitpcode(PC_STW, 11, 1, 0, r11_save_offset);
        setpcodeflags(fIsPtrOp | fIsArgInit);
    } else {
        emitpcode(PC_STW, 0, 1, 0, r0_save_offset);
        setpcodeflags(fIsArgInit);
    }

    offset = interrupt_save_offset;
    emitpcode(PC_MFCTR, 0);
    emitpcode(PC_STW, 0, 1, 0, offset);
    emitpcode(PC_MFXER, 0);
    emitpcode(PC_STW, 0, 1, 0, offset + 4);
    emitpcode(PC_MFCR, 0);
    emitpcode(PC_STW, 0, 1, 0, offset + 8);
    emitpcode(PC_MFLR, 0);
    emitpcode(PC_STW, 0, 1, 0, offset + 12);
    if (interrupt_saved_count[RegClass_VR] || interrupt_saved_count[RegClass_FPR]) {
        emitpcode(PC_MFMSR, 0);
        offset += 16;
        emitpcode(PC_STW, 0, 1, 0, offset);
    }

    if (interrupt_saved_count[RegClass_GPR]) {
        offset = interrupt_class_offset[RegClass_GPR];
        for (i = 0; i < n_scratch_registers[RegClass_GPR]; i++) {
            reg = scratch_registers[RegClass_GPR][i];
            if (reg != 0 &&
                (reg != 11 || r11_save_offset == -1) &&
                (interrupt_saved_mask[RegClass_GPR] & (1 << reg))) {
                emitpcode(PC_STW, reg, 1, 0, offset + i * save_slot_size[RegClass_GPR]);
            }
        }
    }

    if (interrupt_saved_count[RegClass_VR]) {
        // make AltiVec available
        emitpcode(PC_MFMSR, 0);
        emitpcode(PC_ORIS, 0, 0, 0x200);
        emitpcode(PC_MTMSR, 0);
        setpcodeflags(fIsVolatile | fSideEffects);
        emitpcode(PC_SYNC);
        offset = interrupt_class_offset[RegClass_VR];
        for (i = 0; i < n_scratch_registers[RegClass_VR]; i++) {
            reg = scratch_registers[RegClass_VR][i];
            if (interrupt_saved_mask[RegClass_VR] & (1 << reg)) {
                emitpcode(PC_LI, 0, offset);
                emitpcode(PC_STVX, reg, 1, 0);
                offset += save_slot_size[RegClass_VR];
            }
        }
    }

    if (interrupt_saved_count[RegClass_FPR]) {
        // make the FPU available
        emitpcode(PC_MFMSR, 0);
        emitpcode(PC_ORI, 0, 0, 0x2000);
        emitpcode(PC_MTMSR, 0);
        setpcodeflags(fIsVolatile | fSideEffects);
        emitpcode(PC_SYNC);
        offset = interrupt_class_offset[RegClass_FPR];
        for (i = 0; i < n_scratch_registers[RegClass_FPR]; i++) {
            reg = scratch_registers[RegClass_FPR][i];
            if (interrupt_saved_mask[RegClass_FPR] & (1 << reg)) {
                emitpcode(PC_STFD, reg, 1, 0, offset + i * save_slot_size[RegClass_FPR]);
            }
        }
    }

    offset = interrupt_save_offset + interrupt_spr_offset;
    if (interrupt_routine_flags->flags & Interrupt_SRR) {
        emitpcode(PC_MFSPR, 0, 26);
        emitpcode(PC_STW, 0, 1, 0, offset);
        emitpcode(PC_MFSPR, 0, 27);
        emitpcode(PC_STW, 0, 1, 0, offset + 4);
        offset += 8;
    }
    if (interrupt_routine_flags->flags & Interrupt_DAR) {
        emitpcode(PC_MFSPR, 0, 19);
        emitpcode(PC_STW, 0, 1, 0, offset);
        offset += 4;
    }
    if (interrupt_routine_flags->flags & Interrupt_DSISR) {
        emitpcode(PC_MFSPR, 0, 18);
        emitpcode(PC_STW, 0, 1, 0, offset);
    }
    if (interrupt_routine_flags->flags & Interrupt_Enable) {
        // enable interrupts (EE) and keep RI
        emitpcode(PC_MFMSR, 0);
        emitpcode(PC_ORI, 0, 0, 0x8002);
        emitpcode(PC_MTMSR, 0);
    }
}

// NOTE: invented names: restore_interrupt_registers, offset
// a `#pragma interrupt` routine's epilogue, save_interrupt_registers in reverse
static void restore_interrupt_registers(void) {
    int base;
    SInt32 offset;
    int i;
    int reg;

    if (interrupt_routine_flags->flags & Interrupt_Enable) {
        // clear EE and RI again
        emitpcode(PC_MFMSR, 0);
        emitpcode(PC_ADDIS, 11, 0, 0, 1);
        emitpcode(PC_ADDI, 11, 11, 0, -0x7FFE);
        emitpcode(PC_ANDC, 0, 0, 11);
        emitpcode(PC_MTMSR, 0);
    }

    base = 1;
    if (dynamic_stack) {
        base = 11;
        emitpcode(PC_MR, base, 10);
        setpcodeflags(fSideEffects);
    }

    offset = interrupt_save_offset + interrupt_spr_offset;
    if (interrupt_routine_flags->flags & Interrupt_SRR) {
        emitpcode(PC_LWZ, 0, base, 0, offset);
        emitpcode(PC_MTSPR, 26, 0);
        emitpcode(PC_LWZ, 0, base, 0, offset + 4);
        emitpcode(PC_MTSPR, 27, 0);
        offset += 8;
    }
    if (interrupt_routine_flags->flags & Interrupt_DAR) {
        emitpcode(PC_LWZ, 0, base, 0, offset);
        emitpcode(PC_MTSPR, 19, 0);
        offset += 4;
    }
    if (interrupt_routine_flags->flags & Interrupt_DSISR) {
        emitpcode(PC_LWZ, 0, base, 0, offset);
        emitpcode(PC_MTSPR, 18, 0);
    }

    if (interrupt_saved_count[RegClass_FPR]) {
        offset = interrupt_class_offset[RegClass_FPR];
        for (i = 0; i < n_scratch_registers[RegClass_FPR]; i++) {
            reg = scratch_registers[RegClass_FPR][i];
            if (interrupt_saved_mask[RegClass_FPR] & (1 << reg)) {
                emitpcode(PC_LFD, reg, base, 0, offset);
                offset += save_slot_size[RegClass_FPR];
            }
        }
    }

    if (interrupt_saved_count[RegClass_VR]) {
        offset = interrupt_class_offset[RegClass_VR];
        for (i = 0; i < n_scratch_registers[RegClass_VR]; i++) {
            reg = scratch_registers[RegClass_VR][i];
            if (interrupt_saved_mask[RegClass_VR] & (1 << reg)) {
                emitpcode(PC_LI, 0, offset);
                emitpcode(PC_LVX, reg, base, 0);
                offset += save_slot_size[RegClass_VR];
            }
        }
    }

    if (interrupt_saved_count[RegClass_GPR]) {
        offset = interrupt_class_offset[RegClass_GPR];
        for (i = 0; i < n_scratch_registers[RegClass_GPR]; i++) {
            reg = scratch_registers[RegClass_GPR][i];
            if (interrupt_saved_mask[RegClass_GPR] & (1 << reg)) {
                if (reg != 0 && (reg != 11 || r11_save_offset == -1)) {
                    emitpcode(PC_LWZ, reg, base, 0, offset);
                }
                offset += save_slot_size[RegClass_GPR];
            }
        }
    }

    offset = interrupt_save_offset;
    emitpcode(PC_LWZ, 0, base, 0, offset);
    emitpcode(PC_MTCTR, 0);
    emitpcode(PC_LWZ, 0, base, 0, offset + 4);
    emitpcode(PC_MTXER, 0);
    emitpcode(PC_LWZ, 0, base, 0, offset + 8);
    emitpcode(PC_MTCRF, 255, 0);
    emitpcode(PC_LWZ, 0, base, 0, offset + 12);
    emitpcode(PC_MTLR, 0);
    if (interrupt_saved_count[RegClass_VR] || interrupt_saved_count[RegClass_FPR]) {
        offset += 16;
        emitpcode(PC_LWZ, 0, base, 0, offset);
        emitpcode(PC_MTMSR, 0);
    }

    if (r11_save_offset != -1) {
        emitpcode(PC_LWZ, 0, base, 0, r0_save_offset);
        setpcodeflags(fIsArgInit);
        emitpcode(PC_LWZ, 11, base, 0, r11_save_offset);
    } else {
        emitpcode(PC_LWZ, 0, base, 0, r0_save_offset);
    }
    setpcodeflags(fIsArgInit);

    if (dynamic_align_stack || dynamic_stack) {
        emitpcode(PC_LWZ, 1, 1, 0, 0);
    } else {
        emitpcode(PC_ADDI, 1, 1, 0, frame_size);
    }
    setpcodeflags(fSideEffects | fIsArgInit);
}

// lays out a `#pragma interrupt` routine's save area: every scratch register when it calls out
// (they may all change), else those its code writes, r0 and (for a moved or realigned frame) r11
// always
static void compute_interrupt_frame(void) { // NOTE: invented name
    RegClass rclass;
    int count;
    int i;
    SInt32 offset;
    SInt32 size;
    int reg;
    PCodeBlock* block;
    PCode* pc;

    if (need_link_register()) {
        for (rclass = 0; rclass < RegClassMax; rclass++) {
            if (save_slot_size[rclass] > 0) {
                interrupt_saved_count[rclass] = n_scratch_registers[rclass];
                interrupt_saved_mask[rclass] = -1;
            }
        }
        count = 0;
        offset = 0;
        size = save_slot_size[RegClass_GPR];
        for (i = 0; i < n_scratch_registers[RegClass_GPR]; i++) {
            reg = scratch_registers[RegClass_GPR][i];
            if (reg == 0) {
                r0_save_offset = offset;
            } else if (reg == 11 &&
                       (dynamic_stack ||
                        dynamic_align_stack ||
                        (interrupt_routine_flags->flags & Interrupt_Enable))) {
                r11_save_offset = offset;
            }
            offset += size;
            count++;
        }
    } else {
        if (dynamic_stack ||
            dynamic_align_stack ||
            (interrupt_routine_flags->flags & Interrupt_Enable)) {
            interrupt_saved_mask[RegClass_GPR] = (1 << 11) | (1 << 0);
        } else {
            interrupt_saved_mask[RegClass_GPR] = 1 << 0;
        }
        for (block = pcbasicblocks; block; block = block->nextBlock) {
            for (pc = block->firstPCode; pc; pc = pc->nextPCode) {
                for (i = 0; i < pc->argCount; i++) {
                    if (pc->args[i].kind == PCOp_REGISTER &&
                        (pc->args[i].data.reg.effect & EffectWrite)) {
                        interrupt_saved_mask[pc->args[i].arg] |= 1 << pc->args[i].data.reg.reg;
                    }
                }
            }
        }
        for (rclass = 0; rclass < RegClassMax; rclass++) {
            if (save_slot_size[rclass] > 0) {
                count = 0;
                for (i = 0; i < n_scratch_registers[rclass]; i++) {
                    reg = scratch_registers[rclass][i];
                    if (interrupt_saved_mask[rclass] & (1 << reg)) {
                        if (rclass == RegClass_GPR) {
                            if (reg == 0) {
                                r0_save_offset = count * save_slot_size[rclass];
                            } else if (reg == 11 &&
                                       (dynamic_stack ||
                                        dynamic_align_stack ||
                                        (interrupt_routine_flags->flags & Interrupt_Enable))) {
                                r11_save_offset = count * save_slot_size[rclass];
                            }
                        }
                        interrupt_saved_count[rclass]++;
                        count++;
                    }
                }
            }
        }
    }

    interrupt_save_offset = 0;
    interrupt_save_size = 16;
    if (interrupt_saved_count[RegClass_VR] || interrupt_saved_count[RegClass_FPR]) {
        interrupt_save_size += 4;
    }
    interrupt_spr_offset = interrupt_save_size;
    if (interrupt_routine_flags->flags & Interrupt_SRR) {
        interrupt_save_size += 8;
    }
    if (interrupt_routine_flags->flags & Interrupt_DAR) {
        interrupt_save_size += 4;
    }
    if (interrupt_routine_flags->flags & Interrupt_DSISR) {
        interrupt_save_size += 4;
    }
    interrupt_save_size = ALIGN(interrupt_save_size, 4);

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        if (save_slot_size[rclass] > 0 && (rclass != RegClass_VR || copts.altivec_model)) {
            // TODO: the round-up written out, reading save_slot_size at each use (ALIGN or a local
            // compiles otherwise)
            update_frame_align(save_slot_size[rclass]);
            interrupt_save_size =
                (interrupt_save_size + save_slot_size[rclass] - 1) & ~(save_slot_size[rclass] - 1);
            if (interrupt_save_align < save_slot_size[rclass]) {
                interrupt_save_align = save_slot_size[rclass];
            }
            interrupt_class_offset[rclass] = interrupt_save_size;
            interrupt_save_size += interrupt_saved_count[rclass] * save_slot_size[rclass];
        }
    }

    update_frame_align(interrupt_save_align);
    local_data_size = ALIGN(local_data_size, frame_alignment);
    interrupt_save_size = ALIGN(interrupt_save_size, frame_alignment);
}

// The unit's statics are 2-byte packed, as in the original (MWCC lays out .bss with the packing in
// force at the end of the file)
#pragma pack(2)
