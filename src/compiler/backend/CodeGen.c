#include "compiler/CodeGen.h"
#include "compiler/Alias.h"
#include "compiler/BE_elf.h"
#include "compiler/BE_symbol.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CObjC.h"
#include "compiler/Coloring.h"
#include "compiler/CompilerTools.h"
#include "compiler/COptimizer.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/DumpIR.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/GlobalOptimizer.h"
#include "compiler/InlineAsm.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/InstrSelection.h"
#include "compiler/Intrinsics.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/Operands.h"
#include "compiler/PCode.h"
#include "compiler/PCodeAssembly.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeListing.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/Peephole.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/Scheduler.h"
#include "compiler/SFPE_PPC_EABI.h"
#include "compiler/StackFrame.h"
#include "compiler/StructMoves.h"
#include "compiler/Switch.h"
#include "compiler/TOC.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "compiler/ValueNumbering.h"
#include "driver/CCompiler.h"
#include <stdio.h>
#include <string.h>

static Macro powcM;
static Macro __powcM;
static Macro ppc_cpu;
static Macro profM;
static Macro hostM;
static Macro bendM;
static Macro _ppc_M;
static Macro longI;
static Macro IEEED;
Macro vecM;
Macro altivecM;
static Macro optM;
static Macro cpuM;
static Macro eabiM;
Object* gFunction;
static ObjectList* temps;
static char target_cpu[38];
static char optimization_level[8];
CLabel* returnlabel;
CLabel* cleanreturnlabel;
Boolean needs_cleanup;
Statement* current_statement;
int has_catch_blocks;
int disable_optimizer;
SInt32 current_linenumber;
Boolean has_altivec_arrays;
short high_reg;
short low_reg;
short high_offset;
short low_offset;
short low_reg2;
short high_reg2;
Object* rt_ptr_glue;
Object* rt_cvt_fp2unsigned;
Object* rt_profile_entry;
Object* rt_profile_exit;
Object* rt_div2i;
Object* rt_div2u;
Object* rt_mod2i;
Object* rt_mod2u;
Object* rt_shr2i;
Object* rt_shr2u;
Object* rt_shl2i;
Object* rt_cvt_ull_dbl;
Object* rt_cvt_sll_dbl;
Object* rt_cvt_ull_flt;
Object* rt_cvt_sll_flt;
Object* rt_cvt_dbl_usll;
Object* rt_sfpe_d_neg;
Object* rt_sfpe_d_add;
Object* rt_sfpe_d_sub;
Object* rt_sfpe_d_mul;
Object* rt_sfpe_d_div;
Object* rt_sfpe_d_feq;
Object* rt_sfpe_d_fne;
Object* rt_sfpe_d_flt;
Object* rt_sfpe_d_fgt;
Object* rt_sfpe_d_fle;
Object* rt_sfpe_d_fge;
Object* rt_sfpe_d_dtof;
Object* rt_sfpe_d_dtoi;
Object* rt_sfpe_d_dtou;
Object* rt_sfpe_d_dtoll;
Object* rt_sfpe_d_dtoull;
Object* rt_sfpe_d_itod;
Object* rt_sfpe_d_utod;
Object* rt_sfpe_d_lltod;
Object* rt_sfpe_d_ulltod;
Object* rt_sfpe_f_neg;
Object* rt_sfpe_f_add;
Object* rt_sfpe_f_sub;
Object* rt_sfpe_f_mul;
Object* rt_sfpe_f_div;
Object* rt_sfpe_f_feq;
Object* rt_sfpe_f_fne;
Object* rt_sfpe_f_flt;
Object* rt_sfpe_f_fgt;
Object* rt_sfpe_f_fle;
Object* rt_sfpe_f_fge;
Object* rt_sfpe_f_ftod;
Object* rt_sfpe_f_ftoi;
Object* rt_sfpe_f_ftou;
Object* rt_sfpe_f_ftoll;
Object* rt_sfpe_f_ftoull;
Object* rt_sfpe_f_itof;
Object* rt_sfpe_f_utof;
Object* rt_sfpe_f_lltof;
Object* rt_sfpe_f_ulltof;

static heaperror_t saveheaperror = NULL;

enum {
    GPRLimit = 10,
    FPRLimit = 8,
    VRLimit = 13
};

static void CodeGen_heaperror(void);
void CodeGen_reportheapinfo(Boolean release_flag, char* name, char* text);

VarInfo* CodeGen_GetNewVarInfo(void) {
    VarInfo* vi;

    vi = lalloc(sizeof(VarInfo));
    memclrw(vi, sizeof(VarInfo));

    vi->deftoken = *CPrep_CurStreamElement();
    vi->varnumber = localcount++;

    return vi;
}

Object* maketemporary(Type* type) {
    ObjectList* list;
    Object* obj;

    for (list = temps; list; list = list->next) {
        obj = list->object;
        if (!(obj->u.var.info->flags & VarInfoFlag_TempInUse) && obj->type == type) {
            obj->u.var.info->flags |= VarInfoFlag_TempInUse;
            return obj;
        }
    }

    obj = lalloc(sizeof(Object));
    memclrw(obj, sizeof(Object));
    obj->otype = OT_OBJECT;
    obj->access = ACCESSPUBLIC;
    obj->datatype = DLOCAL;
    obj->type = type;
    obj->name = CParser_GetUniqueName();
    obj->u.var.info = CodeGen_GetNewVarInfo();
    obj->u.var.info->flags |= VarInfoFlag_TempInUse;
    assign_local_memory(obj);

    list = lalloc(sizeof(ObjectList));
    memclrw(list, sizeof(ObjectList));
    list->next = temps;
    list->object = obj;
    temps = list;

    return obj;
}

static void free_temporaries(void) {
    ObjectList* list;

    for (list = temps; list; list = list->next) {
        list->object->u.var.info->flags &= ~VarInfoFlag_TempInUse;
    }
}

void process_arguments(ArgumentProcessor func, Boolean hasVarargs) {
    short gpr = 3;
    short fpr = 1;
    short vr = 2;
    Type* type;
    ObjectList* list;

    for (list = arguments; list; list = list->next) {
        type = list->object->type;
        if ((TYPE_IS_8BYTES(type) ||
             (TYPE_IS_SOFT_FLOAT(type) &&
              type->size != 4 &&
              !copts.incompatible_sfpe_double_params)) &&
            (gpr % 2) == 0) {
            gpr++;
        }
        if ((IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) && !TYPE_IS_SOFT_FLOAT(type)) ||
            IS_TYPE_VEC2X32FLOAT(type)) {
            if (IS_TYPE_VEC2X32FLOAT(type) && copts.processor == CPU_PPCGEKKO) {
                func(list->object, (fpr <= FPRLimit) ? fpr : 0);
                fpr++;
            } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) && !TYPE_IS_SOFT_FLOAT(type)) {
                func(list->object, (fpr <= FPRLimit) ? fpr : 0);
                fpr++;
            }
        } else if (IS_TYPE_VECTOR(type)) {
            func(list->object, (vr <= VRLimit) ? vr : 0);
            vr++;
        } else {
            func(list->object, (gpr <= GPRLimit) ? gpr : 0);
            if (TYPE_FITS_IN_REGISTER(type) || TYPE_IS_SOFT_FLOAT(type)) {
                if (type->size <= 4) {
                    gpr += 1;
                } else {
                    gpr += 2;
                }
            } else {
                CError_FATAL(390);
                gpr += (type->size >> 2);
                if (type->size & 3) {
                    gpr++;
                }
            }
        }
    }

    last_argument_register[RegClass_GPR] = gpr - 1;
    last_argument_register[RegClass_FPR] = fpr - 1;
    last_argument_register[RegClass_VR] = vr - 1;
    if (hasVarargs) {
        move_varargs_to_memory();
    }
}

static void retain_argument_register(Object* obj, short reg) {
    VarInfo* vi = Registers_GetVarInfo(obj);
    Type* type = obj->type;

    if (reg && !vi->noregister && vi->used) {
        if (TYPE_FITS_IN_REGISTER(type) || TYPE_IS_SOFT_FLOAT(type)) {
            if (type->size <= 4) {
                retain_register(obj, RegClass_GPR, reg);
            } else if (reg < GPRLimit) {
                if (copts.littleendian) {
                    retain_GPR_pair(obj, reg, reg + 1);
                } else {
                    retain_GPR_pair(obj, reg + 1, reg);
                }
            }
        } else if (IS_TYPE_VEC2X32FLOAT(type)) {
            if (copts.processor == CPU_PPCELF) {
                retain_register(obj, RegClass_GPR, reg);
            } else if (copts.processor == CPU_PPCGEKKO) {
                retain_register(obj, RegClass_FPR, reg);
            } else {
                CError_FATAL(461);
            }
        } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
            retain_register(obj, RegClass_FPR, reg);
        } else if (IS_TYPE_VECTOR(type)) {
            retain_register(obj, RegClass_VR, reg);
        }
    }
}

static void allocate_local_vregs(void) {
    ObjectList* list;
    Object* obj;
    VarInfo* vi;

    for (list = exceptionlist; list; list = list->next) {
        obj = list->object;
        vi = Registers_GetVarInfo(obj);

        if (vi->used && !vi->noregister) {
            if (!OBJ_GET_TARGET_VOLATILE(obj) && !vi->reg) {
                assign_register_by_type(obj);
            }
        }
    }

    set_last_exception_registers();

    for (list = arguments; list; list = list->next) {
        obj = list->object;
        vi = Registers_GetVarInfo(obj);

        if (vi->used && !vi->noregister) {
            if (!OBJ_GET_TARGET_VOLATILE(obj) && !vi->reg) {
                assign_register_by_type(obj);
            }
        }
    }

    for (list = locals; list; list = list->next) {
        obj = list->object;
        if (!IsTempName(obj->name)) {
            vi = Registers_GetVarInfo(obj);

            if (vi->used && !vi->noregister) {
                if (!OBJ_GET_TARGET_VOLATILE(obj) && !vi->reg) {
                    assign_register_by_type(obj);
                }
            }
        }
    }

    open_fe_temp_registers();

    for (list = locals; list; list = list->next) {
        obj = list->object;
        if (IsTempName(obj->name)) {
            vi = Registers_GetVarInfo(obj);

            if (vi->used && !vi->noregister) {
                if (!OBJ_GET_TARGET_VOLATILE(obj) && !vi->reg) {
                    assign_register_by_type(obj);
                }
            }
        }
    }

    for (list = toclist; list; list = list->next) {
        obj = list->object;
        vi = Registers_GetVarInfo(obj);

        if (!vi->reg && vi->used && vi->usage > 1) {
            assign_register_by_type(obj);
        }
    }
}

static void allocate_local_GPRs(void) {
    ObjectList* list;
    Object* obj;
    Object* winning_obj;
    SInt32 winning_usage;
    VarInfo* vi;
    Type* type;

    while (assignable_registers[RegClass_GPR]) {
        winning_obj = NULL;
        winning_usage = -1;
        if (!(disable_optimizer & 2)) {
            for (list = arguments; list; list = list->next) {
                obj = list->object;
                vi = Registers_GetVarInfo(obj);
                type = obj->type;
                if (vi->flags & VarInfoFlag_AsmNamed) {
                    vi->usage = 100000;
                }
                if (!vi->reg && vi->used && !vi->noregister) {
                    if (!OBJ_GET_TARGET_VOLATILE(obj) &&
                        vi->usage >= winning_usage &&
                        vi->usage >= 2) {
                        if ((TYPE_FITS_IN_REGISTER(type) || TYPE_IS_SOFT_FLOAT(type)) &&
                            (!TYPE_IS_GPR_PAIR(type) || assignable_registers[RegClass_GPR] >= 2)) {
                            winning_obj = obj;
                            winning_usage = vi->usage;
                        }
                    }
                }
            }
        }
        if (!(disable_optimizer & 2)) {
            for (list = locals; list; list = list->next) {
                obj = list->object;
                vi = Registers_GetVarInfo(obj);
                type = obj->type;
                if (vi->flags & VarInfoFlag_AsmNamed) {
                    vi->usage = 100000;
                }
                if (!vi->reg && vi->used && !vi->noregister) {
                    if (!OBJ_GET_TARGET_VOLATILE(obj) &&
                        vi->usage >= winning_usage &&
                        vi->usage >= 2) {
                        if ((TYPE_FITS_IN_REGISTER(type) || TYPE_IS_SOFT_FLOAT(type)) &&
                            (!TYPE_IS_GPR_PAIR(type) || assignable_registers[RegClass_GPR] >= 2)) {
                            winning_obj = obj;
                            winning_usage = vi->usage;
                        }
                    }
                }
            }
        }
        for (list = toclist; list; list = list->next) {
            obj = list->object;
            vi = Registers_GetVarInfo(obj);
            if (vi->flags & VarInfoFlag_AsmNamed) {
                vi->usage = 100000;
            }
            if (!vi->reg && vi->used) {
                if (vi->usage >= winning_usage && vi->usage >= 3) {
                    winning_obj = obj;
                    winning_usage = vi->usage;
                }
            }
        }

        if (!winning_obj) {
            break;
        }

        assign_register_by_type(winning_obj);
        CError_ASSERT(701, Registers_GetVarInfo(winning_obj)->flags & VarInfoFlag_InReg);
    }
}

static void allocate_local_FPRs(void) {
    ObjectList* list;
    Object* obj;
    Object* winning_obj;
    SInt32 winning_usage;
    VarInfo* vi;
    Type* type;

    while (assignable_registers[RegClass_FPR]) {
        winning_obj = NULL;
        winning_usage = -1;
        if (!(disable_optimizer & 2)) {
            for (list = arguments; list; list = list->next) {
                obj = list->object;
                type = obj->type;
                vi = Registers_GetVarInfo(obj);
                if (vi->flags & VarInfoFlag_AsmNamed) {
                    vi->usage = 100000;
                }
                if (!vi->reg && vi->used && !vi->noregister) {
                    if (!OBJ_GET_TARGET_VOLATILE(obj) &&
                        vi->usage >= winning_usage &&
                        vi->usage >= 2) {
                        if ((IS_TYPE_VEC2X32FLOAT(type) && copts.processor == CPU_PPCGEKKO) ||
                            (type->type == TYPEFLOAT && !IS_TYPE_VEC2X32FLOAT(type))) {
                            winning_obj = obj;
                            winning_usage = vi->usage;
                        }
                    }
                }
            }
        }
        if (!(disable_optimizer & 2)) {
            for (list = locals; list; list = list->next) {
                obj = list->object;
                type = obj->type;
                vi = Registers_GetVarInfo(obj);
                if (vi->flags & VarInfoFlag_AsmNamed) {
                    vi->usage = 100000;
                }
                if (!vi->reg && vi->used && !vi->noregister) {
                    if (!OBJ_GET_TARGET_VOLATILE(obj) &&
                        vi->usage >= winning_usage &&
                        vi->usage >= 2) {
                        if ((IS_TYPE_VEC2X32FLOAT(type) && copts.processor == CPU_PPCGEKKO) ||
                            (obj->type->type == TYPEFLOAT && !IS_TYPE_VEC2X32FLOAT(obj->type))) {
                            winning_obj = obj;
                            winning_usage = vi->usage;
                        }
                    }
                }
            }
        }

        if (!winning_obj) {
            break;
        }

        assign_register_by_type(winning_obj);
        CError_ASSERT(785, Registers_GetVarInfo(winning_obj)->flags & VarInfoFlag_InReg);
    }
}

static void allocate_local_VRs(void) {
    ObjectList* list;
    Object* obj;
    Object* winning_obj;
    SInt32 winning_usage;
    VarInfo* vi;

    while (assignable_registers[RegClass_VR]) {
        winning_obj = NULL;
        winning_usage = -1;
        if (!(disable_optimizer & 2)) {
            for (list = arguments; list; list = list->next) {
                obj = list->object;
                vi = Registers_GetVarInfo(obj);
                if (vi->flags & VarInfoFlag_AsmNamed) {
                    vi->usage = 100000;
                }
                if (!vi->reg && vi->used && !vi->noregister) {
                    if (!OBJ_GET_TARGET_VOLATILE(obj) &&
                        vi->usage >= winning_usage &&
                        vi->usage >= 2) {
                        if (IS_TYPE_VECTOR(obj->type)) {
                            winning_obj = obj;
                            winning_usage = vi->usage;
                        }
                    }
                }
            }
        }
        if (!(disable_optimizer & 2)) {
            for (list = locals; list; list = list->next) {
                obj = list->object;
                vi = Registers_GetVarInfo(obj);
                if (vi->flags & VarInfoFlag_AsmNamed) {
                    vi->usage = 100000;
                }
                if (!vi->reg && vi->used && !vi->noregister) {
                    if (!OBJ_GET_TARGET_VOLATILE(obj) &&
                        vi->usage >= winning_usage &&
                        vi->usage >= 2) {
                        if (IS_TYPE_VECTOR(obj->type)) {
                            winning_obj = obj;
                            winning_usage = vi->usage;
                        }
                    }
                }
            }
        }

        if (!winning_obj) {
            break;
        }

        assign_register_by_type(winning_obj);
        CError_ASSERT(849, Registers_GetVarInfo(winning_obj)->flags & VarInfoFlag_InReg);
    }
}

static void allocate_locals(void) {
    has_altivec_arrays = FALSE;

    if (!requires_frame && !optimizing) {
        process_arguments(retain_argument_register, FALSE);
    }

    if (optimizing) {
        allocate_local_vregs();
    } else {
        allocate_local_GPRs();
        if (!copts.soft_float) {
            allocate_local_FPRs();
        }
        allocate_local_VRs();
    }

    assign_locals_to_memory(locals);
}

#if VERSION < VERSION_GC_2_5
static Boolean is_pointer_to_const(Object* obj) {
    CError_ASSERT(895, IS_TYPE_POINTER(obj->type));
    return CParser_IsConst(TPTR_TARGET(obj->type), obj->qual);
}

static Boolean is_unique_pointer(Object* obj) {
    return (CParser_GetTypeQualifiers(obj->type, obj->qual) & Q_RESTRICT) != 0 ||
        is_pointer_to_const(obj);
}
#endif

#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT 8
#endif
void move_assigned_argument(Object* obj, short reg) {
    VarInfo* vi;
    Type* type;
    SInt32 bytesLeft;
    SInt32 offset;

    vi = Registers_GetVarInfo(obj);
    type = obj->type;
    CError_ASSERT(920, obj->datatype == DLOCAL);

    if (!vi->used) {
        return;
    }

    if (reg) {
        if (vi->reg) {
            if (TYPE_IS_8BYTES(type) || (TYPE_IS_SOFT_FLOAT(type) && type->size != 4)) {
                if (copts.littleendian) {
                    if (vi->reg != reg) {
                        emitpcode(PC_MR, vi->reg, reg);
                    }
                    if (reg < GPRLimit) {
                        CError_ASSERT(935, (vi->regHi != reg) && (vi->reg != (reg + 1)));
                        if (vi->regHi != (reg + 1)) {
                            emitpcode(PC_MR, vi->regHi, reg + 1);
                        }
                    } else {
                        CError_FATAL(941);
                        load_store_register(
                            PC_LWZ, vi->regHi, local_base_register(obj), obj, high_offset
                        );
                    }
                } else {
                    if (vi->regHi != reg) {
                        emitpcode(PC_MR, vi->regHi, reg);
                    }
                    if (reg < GPRLimit) {
                        CError_ASSERT(950, (vi->reg != reg) && (vi->regHi != (reg + 1)));
                        if (vi->reg != (reg + 1)) {
                            emitpcode(PC_MR, vi->reg, reg + 1);
                        }
                    } else {
                        CError_FATAL(956);
                        load_store_register(
                            PC_LWZ, vi->reg, local_base_register(obj), obj, low_offset
                        );
                    }
                }
            } else if (vi->reg != reg) {
                if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) && !TYPE_IS_SOFT_FLOAT(type)) {
                    emitpcode(PC_FMR, vi->reg, reg);
                } else if (IS_TYPE_VECTOR(type)) {
                    emitpcode(PC_VMR, vi->reg, reg);
                } else if (IS_TYPE_VEC2X32FLOAT(type) && copts.processor == CPU_PPCGEKKO) {
                    emitpcode(PC_PS_MR, vi->reg, reg);
                } else {
                    PCode* pc = makepcode(PC_MR, vi->reg, reg);
                    appendpcode(pclastblock, pc);
                    if (IS_TYPE_POINTER(type)) {
#if VERSION < VERSION_GC_2_5
                        if (is_unique_pointer(obj)) {
                            pc->alias = make_distinct_virtual_alias(obj, 0, 0);
                        }
#else
                        pc->alias = make_distinct_virtual_alias(obj);
#endif
                    }
                }
            }
        } else {
            if (IS_TYPE_POINTER(type) || IS_TYPE_4BYTES_MEMBERPOINTER(type)) {
                load_store_register(PC_STW, reg, local_base_register(obj), obj, 0);
            } else if (IS_TYPE_INT(type) || IS_TYPE_ENUM(type)) {
                switch (type->size) {
                    case 1:
                        load_store_register(PC_STB, reg, local_base_register(obj), obj, 0);
                        break;
                    case 2:
                        load_store_register(PC_STH, reg, local_base_register(obj), obj, 0);
                        break;
                    case 4:
                        load_store_register(PC_STW, reg, local_base_register(obj), obj, 0);
                        break;
                    case 8:
                        load_store_register(PC_STW, reg, local_base_register(obj), obj, 0);
                        if (reg < GPRLimit) {
                            load_store_register(PC_STW, reg + 1, local_base_register(obj), obj, 4);
                        }
                        break;
#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT 9
#endif
                    default:
                        CError_FATAL(1020);
                }
            } else if (TYPE_IS_SOFT_FLOAT(type)) {
                if (type->size == 4) {
                    load_store_register(PC_STW, reg, local_base_register(obj), obj, 0);
                } else {
                    load_store_register(PC_STW, reg, local_base_register(obj), obj, 0);
                    load_store_register(PC_STW, reg + 1, local_base_register(obj), obj, 4);
                }
            } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
                load_store_register(
                    (type->size == 4) ? PC_STFS : PC_STFD, reg, local_base_register(obj), obj, 0
                );
            } else if (IS_TYPE_VECTOR(type)) {
                load_store_register(PC_STVX, reg, local_base_register(obj), obj, 0);
            } else if (IS_TYPE_VEC2X32FLOAT(type) && copts.processor == CPU_PPCGEKKO) {
                gecko_load_store_register(reg, local_base_register(obj), obj, 0, 0, SPR_GQR0, 0);
            } else {
                bytesLeft = (11 - reg) * 4;
                if (bytesLeft > obj->type->size) {
                    bytesLeft = obj->type->size;
                }
                offset = 0;
                while (bytesLeft > 0) {
                    load_store_register(PC_STW, reg, local_base_register(obj), obj, offset);
                    reg++;
                    offset += 4;
                    bytesLeft -= 4;
                }
            }
        }
    } else {
        if (vi->reg) {
            if (IS_TYPE_POINTER(type) || IS_TYPE_4BYTES_MEMBERPOINTER(type)) {
                load_store_register(PC_LWZ, vi->reg, local_base_register(obj), obj, 0);
            } else if (TYPE_IS_SOFT_FLOAT(type)) {
                if (type->size == 4) {
                    load_store_register(PC_LWZ, vi->reg, local_base_register(obj), obj, 0);
                } else {
                    load_store_register(PC_LWZ, vi->reg, local_base_register(obj), obj, low_offset);
                    load_store_register(
                        PC_LWZ, vi->regHi, local_base_register(obj), obj, high_offset
                    );
                }
            } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
                load_store_register(
                    (type->size == 4) ? PC_LFS : PC_LFD, vi->reg, local_base_register(obj), obj, 0
                );
            } else if (IS_TYPE_VECTOR(type)) {
                load_store_register(PC_LVX, vi->reg, local_base_register(obj), obj, 0);
            } else if (IS_TYPE_VEC2X32FLOAT(type) && copts.processor == CPU_PPCGEKKO) {
                gecko_load_store_register(reg, local_base_register(obj), obj, 0, 0, SPR_GQR0, 1);
            } else {
                switch (type->size) {
                    case 1:
                        load_store_register(PC_LBZ, vi->reg, local_base_register(obj), obj, 0);
                        break;
                    case 2:
                        load_store_register(
                            is_unsigned(type) ? PC_LHZ : PC_LHA,
                            vi->reg,
                            local_base_register(obj),
                            obj,
                            0
                        );
                        break;
                    case 4:
                        load_store_register(PC_LWZ, vi->reg, local_base_register(obj), obj, 0);
                        break;
                    case 8:
                        load_store_register(
                            PC_LWZ, vi->regHi, local_base_register(obj), obj, high_offset
                        );
                        load_store_register(
                            PC_LWZ, vi->reg, local_base_register(obj), obj, low_offset
                        );
                        break;
                    default:
                        CError_FATAL(1120);
                }
            }
        } else if (!optimizing) {
            local_base_register(obj);
        }
    }
}

static void load_TOC_pointers(void) {
    Object* obj;
    VarInfo* vi;
    ObjectList* list;
    AnchorEntry* rec;
    BE_SymGroup* group;
    Operand opnd;

    memclrw(&opnd, sizeof(Operand));
    for (list = toclist; list; list = list->next) {
        obj = list->object;
        if ((vi = Registers_GetVarInfo(obj)) && (vi->flags & VarInfoFlag_InReg)) {
            switch (obj->datatype) {
                case DDATA:
                    if (used_anchor(obj) || !obj->u.data.reginfo.reg) {
                        symbol_operand(&opnd, obj);
                        if (opnd.optype) {
                            Coerce_to_register(&opnd, TYPE(&void_ptr), vi->reg);
                        }
                        if (opnd.reg != vi->reg) {
                            if ((opnd.flags & 0x8000) ||
                                (opnd.flags & 0x4000) ||
                                (opnd.flags & 0xc000)) {
                                if (copts.processor == CPU_PPCGEKKO) {
                                    emitpcode(PC_PS_MR, vi->reg, opnd.reg);
                                }
                            } else {
                                emitpcode(PC_MR, vi->reg, opnd.reg);
                            }
                        }
                    }
                    break;
                default:
                    CError_FATAL(1237);
            }
        }
    }

    for (rec = anchorlist; rec; rec = rec->next) {
        if (rec->full) {
            ObjGenSymbolLink* link = rec->symbolLink;
            if ((vi = Registers_GetVarInfo(rec->object)) && (vi->flags & VarInfoFlag_InReg)) {
                for (group = link->groups; group; group = group->next) {
                    if (group->full) {
                        if (!group->base) {
                            group->reg = vi->reg;
                        } else {
                            SInt32 reg = used_virtual_registers[RegClass_GPR]++;
                            emitpcode(PC_ADDIS, reg, vi->reg, 0, HIGH_PART(group->base));
                            group->reg = reg;
                        }
                    }
                }
            }
        }
    }
}

static int has_vararglist(Object* funcobj) {
    FuncArg* arg;

    arg = TYPE_FUNC(funcobj->type)->args;
    while (arg && arg != &elipsis) {
        arg = arg->next;
    }

    return (arg == &elipsis);
}

void assign_labels(Statement* stmt) {
    Statement* last;

    last = NULL;
    while (stmt) {
        if (stmt->type == ST_LABEL && !stmt->label->pclabel) {
            if (last && last->type == ST_LABEL) {
                stmt->label->pclabel = last->label->pclabel;
            } else {
                stmt->label->pclabel = makepclabel();
            }
        }

        last = stmt;
        stmt = stmt->next;
    }
}

static Boolean islaststatement(Statement* stmt) {
    for (stmt = stmt->next; stmt; stmt = stmt->next) {
        if (stmt->type > ST_LABEL) {
            return FALSE;
        }
    }
    return TRUE;
}

static void newstatement(SInt32 sourceoffset, SInt32 value, int isUnrolled) {
    PCodeBlock* block = pclastblock;

    pcloopweight = value;
    if (!block->pcodeCount) {
        block->loopWeight = value;
    }

    if (block->pcodeCount > 100) {
        branch_label(makepclabel());
    }

    if (isUnrolled) {
        block->flags |= fUnrolled;
    }
}

static void expressionstatement(ENode* expr) {
    Operand opnd;
    Type* type;

    memclrw(&opnd, sizeof(Operand));
    cgdispatch[expr->type](expr, 0, 0, &opnd);

    if (ENODE_IS(expr, EINDIRECT) && (opnd.flags & OpndFlags_Volatile)) {
        type = expr->rtype;
        if (TYPE_FITS_IN_REGISTER_2(type) || (TYPE_IS_SOFT_FLOAT(type) && type->size == 4)) {
            if (opnd.optype != OpndType_GPR) {
                Coerce_to_register(&opnd, type, 0);
            }
        } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
            if (opnd.optype != OpndType_FPR) {
                Coerce_to_fp_register(&opnd, type, 0);
            }
        } else if (IS_TYPE_VECTOR(type)) {
            if (opnd.optype != OpndType_VR) {
                Coerce_to_v_register(&opnd, type, 0);
            }
        } else if (IS_TYPE_VEC2X32FLOAT(type) && copts.processor == CPU_PPCGEKKO) {
            if (opnd.optype != OpndType_FPR) {
                Coerce_to_fp_register(&opnd, type, 0);
            }
        }
    }
}

static void labelstatement(CLabel* label) {
    if (!label->pclabel) {
        label->pclabel = makepclabel();
    }
    if (!label->pclabel->resolved) {
        branch_label(label->pclabel);
    }
    free_temporaries();
}

static void gotostatement(CLabel* label) {
    if (!label->pclabel) {
        label->pclabel = makepclabel();
    }
    branch_always(label->pclabel);
    free_temporaries();
}

static void gotoexpression(ENode* expr) {
    Operand opnd;
    CodeLabelList* list;

    memclrw(&opnd, sizeof(Operand));
    cgdispatch[expr->type](expr, 0, 0, &opnd);

    if (opnd.optype != OpndType_GPR) {
        Coerce_to_register(&opnd, TYPE(&void_ptr), 0);
    }
    CError_ASSERT(1450, opnd.optype == OpndType_GPR);

    for (list = codelabellist; list; list = list->next) {
        pcbranch(pclastblock, list->label->pclabel);
    }

    emitpcode(PC_MTCTR, opnd.reg);
    branch_indirect(NULL);
}

static void conditionalstatement(ENode* cond, CLabel* label, short truthy) {
    Operand opnd;
    memclrw(&opnd, sizeof(Operand));

    cond = evaluate_and_skip_comma(cond);
    if (!label->pclabel) {
        label->pclabel = makepclabel();
    }

    if (TYPE_IS_SOFT_FLOAT(cond->data.diadic.right->rtype) ||
        TYPE_IS_SOFT_FLOAT(cond->data.diadic.left->rtype)) {
        SFPE_gen_condition(cond, &opnd, FALSE);
    } else if (TYPE_IS_8BYTES(cond->data.diadic.right->rtype) ||
               TYPE_IS_8BYTES(cond->data.diadic.left->rtype)) {
        I8_gen_condition(cond, &opnd, FALSE);
    } else {
        gen_condition(cond, &opnd);
    }

    branch_conditional(opnd.reg, opnd.regOffset, truthy, label->pclabel);
    free_temporaries();
}

static void returnstatement(ENode* expr, Boolean dont_branch) {
    Operand opnd;
    Type* type;
    UInt32 qual;

    qual = 0;
    memclrw(&opnd, sizeof(Operand));

    if (expr) {
        type = expr->rtype;
        if (TYPE_FITS_IN_REGISTER(type) || TYPE_IS_SOFT_FLOAT(type)) {
            if (TYPE_IS_GPR_PAIR(type)) {
                cgdispatch[expr->type](expr, low_reg, high_reg, &opnd);
                coerce_to_register_pair(&opnd, type, low_reg, high_reg);
            } else {
                cgdispatch[expr->type](expr, 3, 0, &opnd);
                if (opnd.optype != OpndType_GPR) {
                    Coerce_to_register(&opnd, type, 3);
                }
                {
                    SInt32 reg = opnd.reg;
                    if (reg != 3) {
                        emitpcode(PC_MR, 3, reg);
                    }
                }
            }
        } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
            cgdispatch[expr->type](expr, 1, 0, &opnd);
            if (opnd.optype != OpndType_FPR) {
                Coerce_to_fp_register(&opnd, type, 1);
            }
            {
                SInt32 reg = opnd.reg;
                if (reg != 1) {
                    emitpcode(PC_FMR, 1, reg);
                }
            }
        } else if (IS_TYPE_VECTOR(type)) {
            cgdispatch[expr->type](expr, 2, 0, &opnd);
            if (opnd.optype != OpndType_VR) {
                Coerce_to_v_register(&opnd, type, 2);
            }
            {
                SInt32 reg = opnd.reg;
                if (reg != 2) {
                    emitpcode(PC_VMR, 2, reg);
                }
            }
        } else if (IS_TYPE_VEC2X32FLOAT(type)) {
            cgdispatch[expr->type](expr, 1, 0, &opnd);
            if (opnd.optype != OpndType_FPR) {
                Coerce_to_fp_register(&opnd, type, 1);
            }
            {
                SInt32 reg = opnd.reg;
                if (reg != 1) {
                    emitpcode(PC_PS_MR, 1, reg);
                }
            }
        } else if ((IS_TYPE_STRUCT(type) || IS_TYPE_CLASS(type)) &&
                   !Type_RequiresMemoryReturn(type)) {
            cgdispatch[expr->type](expr, 0, 0, &opnd);
            if (opnd.object) {
                qual = opnd.object->qual;
            }
            if (type->size > 4) {
                load_small_block_into_reg_pair(
                    low_reg, high_reg, &opnd, type, CMach_AllocationAlignment(type, qual)
                );
            } else {
                load_small_block_into_reg(3, &opnd, type, CMach_AllocationAlignment(type, qual));
                {
                    SInt32 reg = opnd.reg;
                    if (reg != 3) {
                        emitpcode(PC_MR, 3, reg);
                        opnd.reg = 3;
                    }
                }
            }
        } else {
            cgdispatch[expr->type](expr, 0, 0, &opnd);
        }
    }

    if (!dont_branch) {
        if (!returnlabel->pclabel) {
            returnlabel->pclabel = makepclabel();
        }
        branch_always(returnlabel->pclabel);
        free_temporaries();
    }
}

static void capturestackpointer(Object* obj) {
    branch_label(makepclabel());
    CError_ASSERT(1621, obj->datatype == DLOCAL);

    load_store_register(PC_STW, 1, local_base_register(obj), obj, 20);
    branch_label(makepclabel());
}

static void resetstackpointer(Object* obj) {
    PCode* pc;

    CError_ASSERT(1648, obj->datatype == DLOCAL);

    branch_label(makepclabel());

    load_store_register(PC_LWZ, 0, 1, NULL, 0);

    pc = makepcode(PC_LWZ, 1, local_base_register(obj), obj, 20);
    pcsetsideeffects(pc);
    appendpcode(pclastblock, pc);

    load_store_register(PC_STW, 0, 1, NULL, 0);

    branch_label(makepclabel());
}

static void callprofiler(char* name) {
    UInt32 masks[RegClassMax] = {0, 0, 0, 0, 0};
    Operand opnd;
    Object* obj;
    SInt32 offset;
    Object* indirectObj;

    memclrw(&opnd, sizeof(Operand));
    if (copts.poolstrings) {
        PooledString* pooled = CInit_DeclarePooledString(name, strlen(name) + 1, FALSE);
        obj = pooled->obj;
        offset = pooled->offset;
    } else {
        obj = CInit_DeclareString(name, strlen(name) + 1, FALSE, FALSE);
        offset = 0;
    }
    obj->section = GetCurrentSection(SECT_DATA);

    if ((indirectObj = createIndirect(obj, FALSE, TRUE))) {
        opnd.optype = OpndType_Symbol;
        opnd.object = indirectObj;
        indirect(&opnd, NULL);
    } else {
        opnd.optype = OpndType_Symbol;
        opnd.object = obj;
    }

    if (opnd.optype != OpndType_GPR) {
        Coerce_to_register(&opnd, TYPE(&void_ptr), 3);
    }
    if (opnd.optype != OpndType_GPR) {
        CError_FATAL(1722);
    } else {
        SInt32 reg = opnd.reg;
        if (reg != 3) {
            emitpcode(PC_MR, 3, reg);
            opnd.reg = 3;
        }
    }

    if (offset) {
        if (!FITS_IN_SHORT(offset)) {
            emitpcode(PC_ADDIS, 3, 3, 0, HIGH_PART(offset));
            if (LOW_PART(offset)) {
                emitpcode(PC_ADDI, 3, 3, 0, LOW_PART(offset));
            }
        } else {
            emitpcode(PC_ADDI, 3, 3, 0, offset);
        }
    }

    masks[RegClass_GPR] |= 1 << 3;
    branch_subroutine(rt_profile_entry, 1, masks);
}

static void exitprofiler(void) {
    UInt32 masks[RegClassMax] = {0, 0, 0, 0, 0};
    masks[RegClass_GPR] = (1 << 3) | (1 << 4);
    masks[RegClass_FPR] = 1 << 1;
    masks[RegClass_VR] = 1 << 2;
    branch_subroutine(rt_profile_exit, 1, masks);
}

int is_paired_single_operand(PCodeArg op) {
    if ((op.data.reg.effect & EffectPS1) ||
        (op.data.reg.effect & EffectPS0) ||
        (op.data.reg.effect & (EffectPS0 | EffectPS1))) {
        return TRUE;
    }
    return FALSE;
}

static inline UInt32 paired_single_mask(PCodeArg* op, UInt32* gprs, UInt32* fprs) {
    switch (op->arg) {
        case RegClass_GPR:
            if (gprs[op->data.reg.reg]) {
                return gprs[op->data.reg.reg] & (EffectPS0 | EffectPS1);
            }
            return 0;
        case RegClass_FPR:
            if (fprs[op->data.reg.reg]) {
                return fprs[op->data.reg.reg] & (EffectPS0 | EffectPS1);
            }
            break;
        default:
            break;
    }
    return 0;
}

Boolean set_vr_reg_flags(void) {
    UInt32* gprs;
    int i;
    UInt32* fprs;
    int j;
    int count = 0;
    int other = FALSE;
    PCodeBlock* block;
    PCodeBlock* block2;
    PCode* instr;
    UInt32 mask;

    gprs = lalloc(used_virtual_registers[RegClass_GPR] * 4);
    fprs = lalloc(used_virtual_registers[RegClass_FPR] * 4);
    memset(gprs, 0, used_virtual_registers[RegClass_GPR] * 4);
    memset(fprs, 0, used_virtual_registers[RegClass_FPR] * 4);

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
            switch (instr->op) {
                case PC_PSQ_L:
                case PC_PSQ_LU:
                case PC_PSQ_LX:
                case PC_PSQ_LUX:
                case PC_PSQ_ST:
                case PC_PSQ_STU:
                case PC_PSQ_STX:
                case PC_PSQ_STUX:
                    CError_ASSERT(
                        1829,
                        instr->args[3].kind == PCOp_IMMEDIATE &&
                            instr->args[0].kind == PCOp_REGISTER
                    );
                    if (instr->args[3].data.imm.value == 1) {
                        instr->args[0].data.reg.effect ^= EffectPS1;
                    }
                    break;
            }

            for (i = 0; i < instr->argCount; i++) {
                if (instr->args[i].kind == PCOp_REGISTER) {
                    if (is_paired_single_operand(instr->args[i])) {
                        if (instr->args[i].arg == RegClass_GPR) {
                            count++;
                            gprs[instr->args[i].data.reg.reg] |= instr->args[i].data.reg.effect;
                        } else if (instr->args[i].arg == RegClass_FPR) {
                            count++;
                            fprs[instr->args[i].data.reg.reg] |= instr->args[i].data.reg.effect;
                        } else {
                            CError_FATAL(1905);
                        }
                    } else if (instr->args[i].data.reg.effect & EffectSingle) {
                        other = TRUE;
                    }
                }
            }
        }
    }

    if (count) {
        for (block2 = pcbasicblocks; block2; block2 = block2->nextBlock) {
            for (instr = block2->firstPCode; instr; instr = instr->nextPCode) {
                for (j = 0; j < instr->argCount; j++) {
                    mask = paired_single_mask(&instr->args[j], gprs, fprs);
                    if (mask &&
                        !is_paired_single_operand(instr->args[j]) &&
                        (instr->args[j].data.reg.effect & EffectWrite)) {
                        instr->args[j].data.reg.effect |= mask;
                        instr->args[j].data.reg.effect |= EffectRead;
                        instr->flags &= ~fIsCSE;
                        instr->flags &= ~fIsMove;
                    }
                }
            }
        }
    }

    if (copts.disable_vector_frame) {
        return FALSE;
    }
    return (count || other) ? TRUE : FALSE;
}

void CodeGen_Generator(
    Statement* statements, Object* func, UInt8 oldStyleParameters, Boolean callOnModuleBind
) {
    Statement* stmt;
    Boolean has_varargs;
    PCodeBlock* entryBlock;
    SInt32 size;
    InlineAsm* ia;

    CodeGen_InitialSanityCheck();
    if (!saveheaperror) {
        saveheaperror = getheaperror();
        setheaperror(CodeGen_heaperror);
    }

    if (cparamblkptr->precompile == 1) {
        CError_Error(ERR_ILLEGAL_USE_PRECOMPILED_HEADER);
    }

    if (!func) {
        func = createstaticinitobject();
        declareinit(func);
    } else if (func && func->name) {
        PrintProgressFunction(func->name->name);
    }

    gFunction = func;
    has_varargs = has_vararglist(func);
    init_endian();
    init_stack_globals(func);
    assign_arguments_to_memory(func, oldStyleParameters, has_varargs);

    needs_cleanup = FALSE;
    disable_optimizer = FALSE;
    has_catch_blocks = FALSE;
    current_statement = NULL;
    current_linenumber = 0;
    precomputedoperands = NULL;
    switchtables = NULL;
    temps = NULL;
    initializeexceptiontables();
    returnlabel = cleanreturnlabel = newlabel();

    if (copts.debuglisting) {
        DumpIR(statements, func);
    }

    statements = COpt_Optimizer(func, statements);
    if (copts.debuglisting) {
        DumpIR(statements, func);
    }

    reset_anchor_varinfo();
    init_registers();
    expandTOCreferences(&statements->next);
    if (copts.debuglisting) {
        DumpIR(statements, func);
    }

    if (copts.profile) {
        makes_call = TRUE;
        requires_frame = TRUE;
    }

    initpcode();

    pclabel(prologue = makepcblock(), makepclabel());
    prologue->flags |= fIsProlog;

    pclabel(entryBlock = makepcblock(), makepclabel());
    pcbranch(prologue, entryBlock->labels);

    init_frame_sizes(has_varargs);
    allocate_locals();
    process_arguments(move_assigned_argument, has_varargs);

    if (copts.schedule_factor || copts.altivec_model) {
        branch_label(makepclabel());
    }

    load_TOC_pointers();

    if (copts.profile) {
        callprofiler(CMangler_GetLinkName(func)->name);
    }

    assign_labels(statements->next);
    open_temp_registers();

    for (stmt = statements->next; stmt; stmt = stmt->next) {
        current_statement = stmt;
        current_linenumber = stmt->sourceoffset;
        switch (stmt->type) {
            case ST_NOP:
                break;
            case ST_EXPRESSION:
                newstatement(
                    stmt->sourceoffset, stmt->value, (stmt->flags & StmtFlag_Unrolled) != 0
                );
                expressionstatement(stmt->expr);
                break;
            case ST_LABEL:
                labelstatement(stmt->label);
                break;
            case ST_IFGOTO:
                newstatement(
                    stmt->sourceoffset, stmt->value, (stmt->flags & StmtFlag_Unrolled) != 0
                );
                conditionalstatement(stmt->expr, stmt->label, TRUE);
                break;
            case ST_IFNGOTO:
                newstatement(
                    stmt->sourceoffset, stmt->value, (stmt->flags & StmtFlag_Unrolled) != 0
                );
                conditionalstatement(stmt->expr, stmt->label, FALSE);
                break;
            case ST_GOTOEXPR:
                newstatement(
                    stmt->sourceoffset, stmt->value, (stmt->flags & StmtFlag_Unrolled) != 0
                );
                gotoexpression(stmt->expr);
                break;
            case ST_GOTO:
                newstatement(
                    stmt->sourceoffset, stmt->value, (stmt->flags & StmtFlag_Unrolled) != 0
                );
                gotostatement(stmt->label);
                break;
            case ST_RETURN:
                newstatement(
                    stmt->sourceoffset, stmt->value, (stmt->flags & StmtFlag_Unrolled) != 0
                );
                returnstatement(stmt->expr, islaststatement(stmt));
                break;
            case ST_SWITCH:
                newstatement(
                    stmt->sourceoffset, stmt->value, (stmt->flags & StmtFlag_Unrolled) != 0
                );
                switchstatement(stmt->expr, (SwitchInfo*)stmt->label);
                break;
            case ST_BEGINCATCH:
                capturestackpointer(stmt->expr->data.objref);
                break;
            case ST_ENDCATCHDTOR:
                CError_ASSERT(2198, stmt->expr->data.objref->datatype == DLOCAL);
                add_immediate(
                    3, local_base_register(stmt->expr->data.objref), stmt->expr->data.objref, 0
                );
                {
                    UInt32 masks[RegClassMax] = {0, 0, 0, 0, 0};
                    masks[RegClass_GPR] |= 1 << 3;
                    branch_subroutine(Xecth_func, 1, masks);
                }
            case ST_ENDCATCH:
                resetstackpointer(stmt->expr->data.objref);
                break;
            case ST_ASM:
                if ((ia = (InlineAsm*)stmt->expr)) {
                    if (ia->flags & IAFlag_Directive) {
                        CError_FATAL(2218);
                    } else {
                        InlineAsm_TranslateIRtoPCode(stmt);
                    }
                }
                break;
            case ST_BEGINLOOP:
                CError_FATAL(2227);
                break;
            case ST_ENDLOOP:
                CError_FATAL(2231);
                break;
            default:
                CError_FATAL(2235);
        }
        check_temp_registers();
    }

    close_temp_registers();
    labelstatement(returnlabel);

    current_statement = NULL;

    epilogue = pclastblock;
    epilogue->flags |= fIsEpilogue;

    pccomputepredecessors();
    deleteunreachableblocks();

    if (copts.report_heap_info > 0) {
        CodeGen_reportheapinfo(FALSE, CMangler_GetLinkName(func)->name, "before optimization");
    }

    StackFrame_has_vectors = set_vr_reg_flags();

    if (copts.optimizationlevel > 0 && !disable_optimizer) {
        globallyoptimizepcode(func);
    } else {
        pclistblocks(CMangler_GetLinkName(func)->name, "INITIAL CODE");
    }

    if (copts.schedule_factor == 2) {
        if (copts.peephole) {
            peepholemergeblocks(func, FALSE);
        }
        if (copts.debuglisting) {
            pclistblocks_start_scheduler(CMangler_GetLinkName(func)->name, "BEFORE SCHEDULING");
        }
        scheduleinstructions(FALSE);
        if (copts.debuglisting) {
            pclistblocks_end_scheduler();
        }
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER INSTRUCTION SCHEDULING");
        }
    }

    if (copts.peephole) {
        if (copts.schedule_factor == 0 && copts.optimizationlevel > 1) {
            peepholemergeblocks(func, FALSE);
        }
        peepholeoptimizeforward(func);
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER PEEPHOLE FORWARD");
        }
    }

    colorinstructions(func);
    if (copts.debuglisting) {
        pclistblocks(CMangler_GetLinkName(func)->name, "AFTER REGISTER COLORING");
    }

    if (copts.optimizationlevel > 1 && !disable_optimizer) {
        removecommonsubexpressions(func, 1);
        if (removedcommonsubexpressions && copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER VALUE NUMBERING (POST COLORING)");
        }
    }

    compute_frame_sizes();
    generate_prologue(prologue, has_varargs);
    if (copts.profile) {
        exitprofiler();
    }
    generate_epilogue(epilogue, TRUE);

    if (copts.debuglisting) {
        pclistblocks(CMangler_GetLinkName(func)->name, "AFTER GENERATING EPILOGUE, PROLOGUE");
    }

    if (copts.peephole) {
        if (copts.schedule_factor) {
            peepholemergeblocks(func, TRUE);
            if (copts.debuglisting) {
                pclistblocks(CMangler_GetLinkName(func)->name, "AFTER MERGING EPILOGUE, PROLOGUE");
            }
        }
        peepholeoptimizepcode(func);
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER PEEPHOLE OPTIMIZATION");
        }
    }

    if (copts.schedule_factor) {
        if (copts.debuglisting) {
            pclistblocks_start_scheduler(CMangler_GetLinkName(func)->name, "BEFORE SCHEDULING");
        }
        scheduleinstructions(TRUE);
        if (copts.debuglisting) {
            pclistblocks_end_scheduler();
        }
        if (copts.debuglisting) {
            pclistblocks(
                CMangler_GetLinkName(func)->name, "FINAL CODE AFTER INSTRUCTION SCHEDULING"
            );
        }
    } else {
        if (copts.debuglisting) {
            pclistblocks(CMangler_GetLinkName(func)->name, "FINAL CODE");
        }
    }

    size = assemblefunction(func, NULL);
    dumpswitchtables(func);
    dumpcodelabels(func);
    if (copts.exceptions && requires_frame) {
        dumpexceptiontables(func, size);
    }

    if (copts.report_heap_info > 0) {
        CodeGen_reportheapinfo(FALSE, CMangler_GetLinkName(func)->name, "finished");
    }

    if (saveheaperror) {
        setheaperror(saveheaperror);
    }
}

static SInt8 thunk_this_register(Type* functype) {
    if (Type_RequiresMemoryReturn(functype)) {
        return 4;
    }
    return 3;
}

void CodeGen_GenVDispatchThunk(
    Object* thunkobj, Object* func, SInt32 this_delta, SInt32 returnDelta, SInt32 ctoroffset
) {
    Boolean save_debug;
    Boolean save_peephole;
    Boolean save_traceback;

    save_debug = copts.filesyminfo;
    save_peephole = copts.peephole;
    save_traceback = copts.traceback;
    ok_to_compress = FALSE;
    ok_for_global_registers = FALSE;

    CError_ASSERT(2413, !returnDelta);

    initpcode();
    makepcblock();

    if (this_delta != 0) {
        SInt32 reg = thunk_this_register(TYPE_FUNC(func->type)->functype);

        if (ctoroffset >= 0) {
            if (!FITS_IN_SHORT(ctoroffset)) {
                emitpcode(PC_ADDIS, 11, 0, 0, HIGH_PART(ctoroffset));
                if (LOW_PART(ctoroffset)) {
                    emitpcode(PC_ADDI, 11, 11, 0, LOW_PART(ctoroffset));
                }
            } else {
                emitpcode(PC_ADDI, 11, 0, 0, ctoroffset);
            }
            emitpcode(PC_LWZX, 11, reg, 11);
            emitpcode(PC_ADD, reg, reg, 11);
        }

        if (!FITS_IN_SHORT(this_delta)) {
            emitpcode(PC_ADDIS, reg, reg, 0, HIGH_PART(this_delta));
            if (LOW_PART(this_delta)) {
                emitpcode(PC_ADDI, reg, reg, 0, LOW_PART(this_delta));
            }
        } else {
            emitpcode(PC_ADDI, reg, reg, 0, this_delta);
        }
    }

    emitpcode(PC_B, 0, func);

    copts.filesyminfo = FALSE;
    copts.peephole = FALSE;
    copts.traceback = FALSE;
    assemblefunction(thunkobj, NULL);

    copts.filesyminfo = save_debug;
    copts.peephole = save_peephole;
    copts.traceback = save_traceback;
}

void CodeGen_SetupRuntimeObjects(void) {
    setupaddressing();
    rt_ptr_glue = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_cvt_fp2unsigned = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_profile_entry = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_profile_exit = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_div2i = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_div2u = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_mod2i = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_mod2u = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_shr2i = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_shr2u = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_shl2i = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_cvt_ull_dbl = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_cvt_sll_dbl = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_cvt_ull_flt = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_cvt_sll_flt = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_cvt_dbl_usll = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_neg = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_add = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_sub = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_mul = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_div = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_feq = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_fne = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_flt = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_fgt = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_fle = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_fge = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_dtof = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_dtoi = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_dtou = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_dtoll = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_dtoull = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_itod = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_utod = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_lltod = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_d_ulltod = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_neg = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_add = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_sub = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_mul = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_div = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_feq = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_fne = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_flt = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_fgt = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_fle = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_fge = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_ftod = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_ftoi = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_ftou = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_ftoll = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_ftoull = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_itof = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_utof = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_lltof = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    rt_sfpe_f_ulltof = CParser_NewRTFunc(&stvoid, NULL, 2, 0);
    Intrinsics_SetupRuntimeObjects();
}

Boolean CodeGen_ReInitRuntimeObjects(Boolean is_precompiler) {
    rt_ptr_glue->name = GetHashNameNodeExport("__ptr_glue");
    toc0.name = GetHashNameNodeExport("TOC");
    rt_cvt_fp2unsigned->name = GetHashNameNodeExport("__cvt_fp2unsigned");
    rt_profile_entry->name = GetHashNameNodeExport("__PROFILE_ENTRY");
    rt_profile_exit->name = GetHashNameNodeExport("__PROFILE_EXIT");
    rt_div2i->name = GetHashNameNodeExport("__div2i");
    rt_div2u->name = GetHashNameNodeExport("__div2u");
    rt_mod2i->name = GetHashNameNodeExport("__mod2i");
    rt_mod2u->name = GetHashNameNodeExport("__mod2u");
    rt_shr2i->name = GetHashNameNodeExport("__shr2i");
    rt_shr2u->name = GetHashNameNodeExport("__shr2u");
    rt_shl2i->name = GetHashNameNodeExport("__shl2i");
    rt_cvt_ull_dbl->name = GetHashNameNodeExport("__cvt_ull_dbl");
    rt_cvt_sll_dbl->name = GetHashNameNodeExport("__cvt_sll_dbl");
    rt_cvt_ull_flt->name = GetHashNameNodeExport("__cvt_ull_flt");
    rt_cvt_sll_flt->name = GetHashNameNodeExport("__cvt_sll_flt");
    rt_cvt_dbl_usll->name = GetHashNameNodeExport("__cvt_dbl_usll");
    rt_sfpe_d_neg->name = GetHashNameNodeExport("_d_neg");
    rt_sfpe_d_add->name = GetHashNameNodeExport("_d_add");
    rt_sfpe_d_sub->name = GetHashNameNodeExport("_d_sub");
    rt_sfpe_d_mul->name = GetHashNameNodeExport("_d_mul");
    rt_sfpe_d_div->name = GetHashNameNodeExport("_d_div");
    rt_sfpe_d_feq->name = GetHashNameNodeExport("_d_feq");
    rt_sfpe_d_fne->name = GetHashNameNodeExport("_d_fne");
    rt_sfpe_d_flt->name = GetHashNameNodeExport("_d_flt");
    rt_sfpe_d_fgt->name = GetHashNameNodeExport("_d_fgt");
    rt_sfpe_d_fle->name = GetHashNameNodeExport("_d_fle");
    rt_sfpe_d_fge->name = GetHashNameNodeExport("_d_fge");
    rt_sfpe_d_dtof->name = GetHashNameNodeExport("_d_dtof");
    rt_sfpe_d_dtoi->name = GetHashNameNodeExport("_d_dtoi");
    rt_sfpe_d_dtou->name = GetHashNameNodeExport("_d_dtou");
    rt_sfpe_d_dtoll->name = GetHashNameNodeExport("_d_dtoll");
    rt_sfpe_d_dtoull->name = GetHashNameNodeExport("_d_dtoull");
    rt_sfpe_d_itod->name = GetHashNameNodeExport("_d_itod");
    rt_sfpe_d_utod->name = GetHashNameNodeExport("_d_utod");
    rt_sfpe_d_lltod->name = GetHashNameNodeExport("_d_lltod");
    rt_sfpe_d_ulltod->name = GetHashNameNodeExport("_d_ulltod");
    rt_sfpe_f_neg->name = GetHashNameNodeExport("_f_neg");
    rt_sfpe_f_add->name = GetHashNameNodeExport("_f_add");
    rt_sfpe_f_sub->name = GetHashNameNodeExport("_f_sub");
    rt_sfpe_f_mul->name = GetHashNameNodeExport("_f_mul");
    rt_sfpe_f_div->name = GetHashNameNodeExport("_f_div");
    rt_sfpe_f_feq->name = GetHashNameNodeExport("_f_feq");
    rt_sfpe_f_fne->name = GetHashNameNodeExport("_f_fne");
    rt_sfpe_f_flt->name = GetHashNameNodeExport("_f_flt");
    rt_sfpe_f_fgt->name = GetHashNameNodeExport("_f_fgt");
    rt_sfpe_f_fle->name = GetHashNameNodeExport("_f_fle");
    rt_sfpe_f_fge->name = GetHashNameNodeExport("_f_fge");
    rt_sfpe_f_ftod->name = GetHashNameNodeExport("_f_ftod");
    rt_sfpe_f_ftoi->name = GetHashNameNodeExport("_f_ftoi");
    rt_sfpe_f_ftou->name = GetHashNameNodeExport("_f_ftou");
    rt_sfpe_f_ftoll->name = GetHashNameNodeExport("_f_ftoll");
    rt_sfpe_f_ftoull->name = GetHashNameNodeExport("_f_ftoull");
    rt_sfpe_f_itof->name = GetHashNameNodeExport("_f_itof");
    rt_sfpe_f_utof->name = GetHashNameNodeExport("_f_utof");
    rt_sfpe_f_lltof->name = GetHashNameNodeExport("_f_lltof");
    rt_sfpe_f_ulltof->name = GetHashNameNodeExport("_f_ulltof");
    CMach_ReInitRuntimeObjects();
    return Intrinsics_ReInitRuntimeObjects(is_precompiler);
}

Boolean CodeGen_IsPublicRuntimeObject(Object* obj) {
    return Intrinsics_IsPublicRuntimeObject(obj);
}

void CodeGen_SOMStub(Object* stub, Object* glue, Object* classdata, SInt32 offset) {
    Boolean save_debug;
    Boolean save_peephole;
    Boolean save_traceback;
    Object* indirectObj;
    Operand opnd;

    save_debug = copts.filesyminfo;
    save_peephole = copts.peephole;
    save_traceback = copts.traceback;

    ok_to_compress = FALSE;
    ok_for_global_registers = FALSE;
    memclrw(&opnd, sizeof(Operand));
    CError_ASSERT(2671, offset <= 0x7FFF);

    initpcode();
    makepcblock();

    classdata->section = GetCurrentSection(SECT_DATA);
    indirectObj = createIndirect(classdata, TRUE, TRUE);
    if (indirectObj) {
        opnd.optype = OpndType_Symbol;
        opnd.object = indirectObj;
        indirect(&opnd, NULL);
    } else {
        opnd.optype = OpndType_Symbol;
        opnd.object = classdata;
    }

    if (opnd.optype != OpndType_GPR) {
        Coerce_to_register(&opnd, TYPE(&void_ptr), 12);
    }

    if (opnd.optype != OpndType_GPR) {
        CError_FATAL(2705);
    } else {
        SInt32 reg = opnd.reg;
        if (reg != 12) {
            emitpcode(PC_MR, 12, reg);
        }
    }

    load_store_register(PC_LWZ, 12, 12, NULL, (short)offset);
    emitpcode(PC_B, 0, glue);

    copts.filesyminfo = FALSE;
    copts.peephole = FALSE;
    copts.traceback = FALSE;
    assemblefunction(stub, NULL);
    copts.filesyminfo = save_debug;
    copts.peephole = save_peephole;
    copts.traceback = save_traceback;
}

void CodeGen_ParseDeclSpec(HashNameNode* identifier, DeclInfo* declinfo) {
    if (!strcmp(identifier->name, "section")) {
        if (cscope_currentfunc) {
            PPCError_Warning(PPCERR_PRAGMA_IGNORED_IN_FUNCTION, "__declspec");
            return;
        }
        tk = plex();
        if (tk != TK_EOL) {
            if (tk == TK_STRING) {
                DoParseDeclSpec(tkstring, declinfo);
            } else {
                PPCError_Warning(PPCERR_DECLSPEC_SECTION_SYNTAX);
            }
            return;
        }
    } else if (!strcmp(identifier->name, "interrupt")) {
        tk = lookahead();
        if (tk != TK_EOL) {
            do {
                if (tk != TK_IDENTIFIER) {
                    break;
                }
                tk = lex();
                if (!strcmp("enable", tkidentifier->name)) {
                    declinfo->interruptflags |= Interrupt_Enable;
                } else if (!strcmp("SRR", tkidentifier->name)) {
                    declinfo->interruptflags |= Interrupt_SRR;
                } else if (!strcmp("DAR", tkidentifier->name)) {
                    declinfo->interruptflags |= Interrupt_DAR;
                } else if (!strcmp("DSISR", tkidentifier->name)) {
                    declinfo->interruptflags |= Interrupt_DSISR;
                } else if (!strcmp("fprs", tkidentifier->name)) {
                    declinfo->interruptflags |= Interrupt_FPRs;
                } else if (!strcmp("vrs", tkidentifier->name)) {
                    declinfo->interruptflags |= Interrupt_VRs;
                } else if (!strcmp("nowarn", tkidentifier->name)) {
                    declinfo->interruptflags |= Interrupt_NoWarn;
                } else {
                    PPCError_Error(PPCERR_DECLSPEC_INTERRUPT_SYNTAX);
                    return;
                }
                tk = lookahead();
            } while (tk != TK_EOL);
        }
        declinfo->interruptflags |= Interrupt_On;
        return;
    }

    CError_Error(ERR_ILLEGAL_TYPE_QUALIFIERS);
}

static void CodeGen_EOLCheck(void);

static void parse_section_pragma(void) {
    short t;

    int tok;
    short ok;
    UInt8 mode;
    UInt8 type;
    char* name;
    char* p;
    SectionRec* rec;

    ok = FALSE;
    rec = (SectionRec*)galloc(sizeof(*rec));
    memset(rec, 0, sizeof(*rec));
    t = notendofline();
    if (t == 1) {
        tok = plex();
    }
    while (t != 0) {
        name = NULL;
        if (tok == TK_IDENTIFIER) {
            name = tkidentifier->name;
        } else if (tok == TK_STRING) {
            name = tkstring;
        }
        if (name != NULL) {
            type = 0;
            mode = 0;
            if (memcmp(name, "code_type", sizeof("code_type")) == 0) {
                type = 1;
            } else if (memcmp(name, "data_type", sizeof("data_type")) == 0) {
                type = 2;
            } else if (memcmp(name, "const_type", sizeof("const_type")) == 0) {
                type = 4;
            } else if (memcmp(name, "sdata_type", sizeof("sdata_type")) == 0) {
                type = 8;
            } else if (memcmp(name, "sconst_type", sizeof("sconst_type")) == 0) {
                type = 0x10;
            } else if (memcmp(name, "all_types", sizeof("all_types")) == 0) {
                type = 0x1f;
            }
            switch (type) {
                case 1:
                case 2:
                case 4:
                case 8:
                case 0x10:
                case 0x1f:
                    if ((rec->flags & 2) != 0) {
                        PPCError_Error(PPCERR_SECTION_TYPE_AND_PERMISSION);
                        CodeGen_EOLCheck();
                        return;
                    }
                    if (rec->flags > 1) {
                        PPCError_Error(PPCERR_SECTION_PARAMETER_OUT_OF_ORDER, name);
                        CodeGen_EOLCheck();
                        return;
                    }
                    if (tok == TK_STRING) {
                        PPCError_Error(PPCERR_SECTION_EXPECTED_OBJECT_TYPE, name);
                        CodeGen_EOLCheck();
                        return;
                    }
                    rec->flags = 1;
                    ok = TRUE;
                    rec->typebits |= type;
                    break;
                default:
                    if (rec->flags == 0) {
                        for (p = name; *p != 0; p++) {
                            switch (*p) {
                                case 'R':
                                    rec->mode |= 2;
                                    mode = 2;
                                    break;
                                case 'W':
                                    rec->mode |= 3;
                                    mode = 2;
                                    break;
                                case 'X':
                                    rec->mode |= 6;
                                    mode = 2;
                                    break;
                                default:
                                    mode = 0;
                                    rec->mode = 0;
                                    break;
                            }
                            if (mode == 0) {
                                break;
                            }
                        }
                    }
                    if (mode == 0) {
                        if (memcmp(name, "data_mode", sizeof("data_mode")) == 0) {
                            mode = 0x10;
                        } else if (memcmp(name, "code_mode", sizeof("code_mode")) == 0) {
                            mode = 0x20;
                        }
                    }
                    if (mode == 0x10 || mode == 0x20) {
                        if ((rec->flags & mode) != 0) {
                            PPCError_Error(PPCERR_SECTION_PARAMETER_REPEATED, name);
                            CodeGen_EOLCheck();
                            return;
                        }
                        if (rec->flags > mode) {
                            PPCError_Error(PPCERR_SECTION_PARAMETER_OUT_OF_ORDER, name);
                            CodeGen_EOLCheck();
                            return;
                        }
                        if (tok == TK_STRING) {
                            PPCError_Error(PPCERR_SECTION_EXPECTED_ADDR_MODE_NAME, name);
                            CodeGen_EOLCheck();
                            return;
                        }
                        rec->flags |= mode;
                        if (notendofline() != 0 && plex() == '=') {
                            if (notendofline() != 0 && (tok = plex()) == TK_IDENTIFIER) {
                                if (mode == 0x10) {
                                    if (memcmp(tkidentifier->name, "far_abs", sizeof("far_abs")) ==
                                        0) {
                                        rec->dataMode = 1;
                                    } else if (memcmp(
                                                   tkidentifier->name,
                                                   "near_abs",
                                                   sizeof("near_abs")
                                               ) == 0) {
                                        rec->dataMode = 2;
                                    } else if (memcmp(
                                                   tkidentifier->name, "sda_rel", sizeof("sda_rel")
                                               ) == 0) {
                                        rec->dataMode = 5;
                                    } else if (memcmp(
                                                   tkidentifier->name, "pc_rel", sizeof("pc_rel")
                                               ) == 0) {
                                        PPCError_Error(
                                            PPCERR_ONLY_FOR_ADDR_MODE,
                                            tkidentifier->name,
                                            "code_mode"
                                        );
                                        CodeGen_EOLCheck();
                                        return;
                                    } else {
                                        PPCError_Error(PPCERR_SECTION_EXPECTED_ADDR_MODE, name);
                                        CodeGen_EOLCheck();
                                        return;
                                    }
                                } else {
                                    if (memcmp(
                                            tkidentifier->name, "near_abs", sizeof("near_abs")
                                        ) == 0) {
                                        rec->codeMode = 0xb;
                                    } else if (memcmp(
                                                   tkidentifier->name, "pc_rel", sizeof("pc_rel")
                                               ) == 0) {
                                        rec->codeMode = 0xa;
                                    } else if (memcmp(
                                                   tkidentifier->name, "far_abs", sizeof("far_abs")
                                               ) == 0) {
                                        rec->codeMode = 0xc;
                                    } else if (memcmp(
                                                   tkidentifier->name, "sda_rel", sizeof("sda_rel")
                                               ) == 0) {
                                        PPCError_Error(PPCERR_SDA_REL_NOT_SMALL_DATA);
                                        CodeGen_EOLCheck();
                                        return;
                                    } else {
                                        PPCError_Error(PPCERR_SECTION_EXPECTED_ADDR_MODE, "code");
                                        CodeGen_EOLCheck();
                                        return;
                                    }
                                }
                            } else {
                                PPCError_Error(PPCERR_SECTION_EXPECTED_UNQUOTED_ADDR_MODE, name);
                                CodeGen_EOLCheck();
                                return;
                            }
                        } else {
                            PPCError_Error(PPCERR_SECTION_EXPECTED, "'='");
                            CodeGen_EOLCheck();
                            return;
                        }
                    } else if (mode == 2) {
                        if ((rec->flags & 1) != 0) {
                            PPCError_Error(PPCERR_SECTION_TYPE_AND_PERMISSION);
                            CodeGen_EOLCheck();
                            return;
                        }
                        if (rec->flags > mode) {
                            PPCError_Error(PPCERR_SECTION_PARAMETER_OUT_OF_ORDER, name);
                            CodeGen_EOLCheck();
                            return;
                        }
                        if (tok == TK_STRING) {
                            PPCError_Error(PPCERR_SECTION_EXPECTED_PERMISSION, name);
                            CodeGen_EOLCheck();
                            return;
                        }
                        rec->flags |= 2;
                    } else {
                        if (rec->flags < 4) {
                            if (tok != TK_STRING) {
                                if (rec->flags == 0) {
                                    PPCError_Error(PPCERR_SECTION_EXPECTED_TYPE_OR_NAME);
                                } else {
                                    PPCError_Error(PPCERR_SECTION_EXPECTED_QUOTED_NAME);
                                }
                                CodeGen_EOLCheck();
                                return;
                            }
                            rec->sectionName = name;
                            rec->flags |= 4;
                        } else if (rec->flags < 8) {
                            if (tok != TK_STRING) {
                                PPCError_Error(PPCERR_SECTION_EXPECTED_BSS_NAME);
                                CodeGen_EOLCheck();
                                return;
                            }
                            rec->linkedSectionName = name;
                            rec->flags |= 8;
                        } else {
                            if (tok == TK_STRING) {
                                if ((rec->flags & 8) != 0) {
                                    PPCError_Error(PPCERR_SECTION_UNKNOWN_PARAMETER, name);
                                    CodeGen_EOLCheck();
                                    return;
                                }
                                PPCError_Error(PPCERR_SECTION_UNKNOWN_OR_MISPLACED_PARAMETER, name);
                                CodeGen_EOLCheck();
                                return;
                            }
                            PPCError_Error(PPCERR_SECTION_UNKNOWN_PARAMETER, name);
                            CodeGen_EOLCheck();
                            return;
                        }
                    }
                    break;
            }
        } else {
            CError_Error(ERR_ILLEGAL_PRAGMA);
            CodeGen_EOLCheck();
            return;
        }
        t = notendofline();
        if (t == 0) {
            break;
        }
        tok = plex();
    }
    if (rec->sectionName == NULL && ok == 0) {
        PPCError_Error(PPCERR_SECTION_EXPECTED_TYPE_OR_NAME);
        CodeGen_EOLCheck();
        return;
    }
    if (rec->sectionName != NULL && is_illegal_section_name(rec->sectionName) != 0) {
        PPCError_Error(PPCERR_INVALID_SECTION_NAME, rec->sectionName);
        return;
    }
    if (rec->linkedSectionName != NULL && is_illegal_section_name(rec->linkedSectionName) != 0) {
        PPCError_Error(PPCERR_INVALID_SECTION_NAME, rec->linkedSectionName);
        return;
    }
    if (rec->sectionName != NULL && is_illegal_abs_section_name(rec->sectionName) != 0) {
        PPCError_Error(PPCERR_SECTION_ADDRESS_DIGITS, rec->sectionName);
        return;
    }
    if (rec->linkedSectionName != NULL &&
        is_illegal_abs_section_name(rec->linkedSectionName) != 0) {
        PPCError_Error(PPCERR_SECTION_ADDRESS_DIGITS, rec->linkedSectionName);
        return;
    }
    Finish_Pragma_Section(rec);
    return;
}

static void CodeGen_EOLCheck(void) {
    short t;

    if (plex() != TK_EOL) {
        CPrep_Error(ERR_END_LINE_EXPECTED);
        do {
            t = plex();
        } while (t != TK_EOL && t != 0);
    }
}

static void schedule_for(int cpu) {
    CPrep_PushOption(OPT_OFFSET(scheduling), cpu);
    if (copts.schedule_factor == 0) {
        CPrep_PushOption(OPT_OFFSET(schedule_factor), 2);
    }
}

static void pragma_scheduling(void) {
    int cpu;

    tk = plex();
    if (tk == TK_IDENTIFIER) {
        if (!strcmp(tkidentifier->name, "vger")) {
            schedule_for(10);
            return;
        } else if (!strcmp(tkidentifier->name, "altivec")) {
            schedule_for(7);
            return;
        } else if (!strcmp(tkidentifier->name, "reset")) {
            CPrep_PopOption(OPT_OFFSET(scheduling));
            CPrep_PopOption(OPT_OFFSET(schedule_factor));
            return;
        } else if (!strcmp(tkidentifier->name, "off")) {
            CPrep_PushOption(OPT_OFFSET(schedule_factor), 0);
            return;
        } else if (!strcmp(tkidentifier->name, "once")) {
            CPrep_PushOption(OPT_OFFSET(schedule_factor), 1);
            return;
        } else if (!strcmp(tkidentifier->name, "twice")) {
            CPrep_PushOption(OPT_OFFSET(schedule_factor), 2);
            return;
        } else if (!strcmp(tkidentifier->name, "on")) {
            CPrep_PushOption(OPT_OFFSET(schedule_factor), 2);
            return;
        } else if (!copts.altivec_model) {
            if (!strcmp(tkidentifier->name, "603e")) {
                schedule_for(5);
                return;
            } else if (!strcmp(tkidentifier->name, "604e")) {
                schedule_for(6);
                return;
            } else if (!strcmp(tkidentifier->name, "PPC603e")) {
                schedule_for(5);
                return;
            } else if (!strcmp(tkidentifier->name, "PPC604e")) {
                schedule_for(6);
                return;
            } else if (!strcmp(tkidentifier->name, "PPC403GA")) {
                schedule_for(8);
                return;
            } else if (!strcmp(tkidentifier->name, "PPC403GB")) {
                schedule_for(8);
                return;
            } else if (!strcmp(tkidentifier->name, "PPC403GC")) {
                schedule_for(8);
                return;
            } else if (!strcmp(tkidentifier->name, "PPC403GCX")) {
                schedule_for(8);
                return;
            }
        } else {
            PPCError_Error(PPCERR_ALTIVEC_NEEDS_SCHEDULER);
            return;
        }
        CPrep_Error(ERR_ILLEGAL_PRAGMA);
        return;
    }

    if (tk == TK_INTCONST) {
        switch (CInt64_GetULong(&tkintconst)) {
            case 601:
                cpu = 1;
                break;
            case 603:
                cpu = 2;
                break;
            case 604:
                cpu = 3;
                break;
            case 750:
            case CPU_PPCGEKKO:
                cpu = 4;
                break;
            case 7400:
                cpu = 7;
                break;
            case 7450:
                cpu = 10;
                break;
            case 8240:
            case 8260:
                cpu = 5;
                break;
            case 401:
            case 403:
            case 505:
            case 509:
            case 555:
            case 602:
                cpu = 8;
                break;
            case 740:
                cpu = 4;
                break;
            case 801:
            case 821:
            case 823:
            case 850:
            case 860:
                cpu = 9;
                break;
            default:
                CPrep_Error(ERR_ILLEGAL_PRAGMA);
                return;
        }
        schedule_for(cpu);
        return;
    }

    if (copts.warn_illpragma) {
        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
    }
}

static void update_bc_range(Boolean compressing) {
    if (compressing) {
        copts.switch_tables = FALSE;
        if (copts.prepare_compress) {
            switch (copts.processor) {
                case CPU_PPC556:
                    copts.bc_range = 1024;
                case CPU_PPC565:
                    copts.bc_range = 2048;
                    break;
            }
        }
    } else {
        copts.switch_tables = 1;
        copts.bc_range = 0x10000;
    }
}

void CodeGen_ParsePragma(HashNameNode* name) {
    short t;
    SInt32 value;
    const char* str;
    InterruptGenerationRecord info;
    InterruptList* list;
    InterruptGenerationRecord* rec;

    if (!strcmp(name->name, "report_heap_info")) {
        t = plex();
        if (t == TK_INTCONST) {
            copts.report_heap_info = CInt64_GetULong(&tkintconst);
            if (copts.report_heap_info < 0) {
                copts.report_heap_info = 0;
                CError_Error(ERR_ILLEGAL_PRAGMA);
            }
        } else if (t == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "off")) {
                copts.report_heap_info = 0;
            } else if (!strcmp(tkidentifier->name, "on")) {
                copts.report_heap_info = 1;
            } else {
                CError_Error(ERR_ILLEGAL_PRAGMA);
                return;
            }
        } else {
            CError_Error(ERR_ILLEGAL_PRAGMA);
        }
        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "scheduling")) {
        pragma_scheduling();
        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "prepare_compress")) {
        if (plex() == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "off")) {
                CPrep_PushOption(OPT_OFFSET(prepare_compress), 0);
            } else if (!strcmp(tkidentifier->name, "on")) {
                CPrep_PushOption(OPT_OFFSET(prepare_compress), 1);
            } else if (!strcmp(tkidentifier->name, "reset")) {
                CPrep_PopOption(OPT_OFFSET(prepare_compress));
            } else {
                CError_Error(ERR_ILLEGAL_PRAGMA);
                return;
            }
        } else {
            CError_Error(ERR_ILLEGAL_PRAGMA);
        }
        update_bc_range(copts.prepare_compress);
        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "bc_range")) {
        t = plex();
        if (t == TK_INTCONST) {
            copts.bc_range = CInt64_GetULong(&tkintconst);
            if (copts.bc_range > 0x10000) {
                copts.bc_range = 0x10000;
                CError_Error(ERR_ILLEGAL_PRAGMA);
            }
        } else if (t == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "off")) {
                copts.bc_range = 0x10000;
            } else if (!strcmp(tkidentifier->name, "default")) {
                update_bc_range(copts.prepare_compress && !copts.switch_tables);
            } else {
                CError_Error(ERR_ILLEGAL_PRAGMA);
                return;
            }
        } else {
            CError_Error(ERR_ILLEGAL_PRAGMA);
        }
        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "gprfloatcopy")) {
        if (plex() == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "off")) {
                CPrep_PushOption(OPT_OFFSET(gprfloatcopy), 0);
            } else if (!strcmp(tkidentifier->name, "on")) {
                CPrep_PushOption(OPT_OFFSET(gprfloatcopy), 1);
            } else if (!strcmp(tkidentifier->name, "reset")) {
                CPrep_PopOption(OPT_OFFSET(gprfloatcopy));
            } else {
                CError_Error(ERR_ILLEGAL_PRAGMA);
                return;
            }
        } else {
            CError_Error(ERR_ILLEGAL_PRAGMA);
        }
        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "ppc_unroll_speculative")) {
        if (plex() == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "off")) {
                copts.unroll_speculative = FALSE;
            } else if (!strcmp(tkidentifier->name, "on")) {
                copts.unroll_speculative = TRUE;
            } else {
                CError_Error(ERR_ILLEGAL_PRAGMA);
                return;
            }
        } else {
            CError_Error(ERR_ILLEGAL_PRAGMA);
        }
        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "ppc_unroll_instructions_limit")) {
        t = plex();
        if (t == TK_INTCONST) {
            copts.unroll_instr_limit = CInt64_GetULong(&tkintconst);
            if (copts.unroll_instr_limit < 0) {
                copts.unroll_instr_limit = 0;
                CError_Error(ERR_ILLEGAL_PRAGMA);
            }
        } else if (t == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "off")) {
                copts.unroll_instr_limit = 0;
            } else if (!strcmp(tkidentifier->name, "on")) {
                copts.unroll_instr_limit = 70;
            } else {
                CError_Error(ERR_ILLEGAL_PRAGMA);
                return;
            }
        } else {
            CError_Error(ERR_ILLEGAL_PRAGMA);
        }
        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "gen_fsel")) {
        t = plex();
        if (t == TK_INTCONST) {
            if ((value = CInt64_GetULong(&tkintconst)) < 0) {
                copts.gen_fsel = 0;
                CError_Error(ERR_ILLEGAL_PRAGMA);
            } else if ((UInt32)value > 255) {
                copts.gen_fsel = 255;
                CError_Error(ERR_ILLEGAL_PRAGMA);
            } else {
                copts.gen_fsel = value;
            }
        } else if (t == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "off")) {
                copts.gen_fsel = 0;
            } else if (!strcmp(tkidentifier->name, "on")) {
                copts.gen_fsel = 10;
            } else if (!strcmp(tkidentifier->name, "always")) {
                copts.gen_fsel = 255;
            } else {
                CError_Error(ERR_ILLEGAL_PRAGMA);
                return;
            }
        } else {
            CError_Error(ERR_ILLEGAL_PRAGMA);
        }
        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "gen_isel")) {
        t = plex();
        if (t == TK_INTCONST) {
            if ((value = CInt64_GetULong(&tkintconst)) < 0) {
                copts.gen_isel = 0;
                CError_Error(ERR_ILLEGAL_PRAGMA);
            } else if ((UInt32)value > 255) {
                copts.gen_isel = 255;
                CError_Error(ERR_ILLEGAL_PRAGMA);
            } else {
                copts.gen_isel = value;
            }
        } else if (t == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "off")) {
                copts.gen_isel = 0;
            } else if (!strcmp(tkidentifier->name, "on")) {
                copts.gen_isel = 10;
            } else if (!strcmp(tkidentifier->name, "always")) {
                copts.gen_isel = 255;
            } else {
                CError_Error(ERR_ILLEGAL_PRAGMA);
                return;
            }
        } else {
            CError_Error(ERR_ILLEGAL_PRAGMA);
        }
        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "ppc_unroll_factor_limit")) {
        t = plex();
        if (t == TK_INTCONST) {
            copts.unroll_factor_limit = CInt64_GetULong(&tkintconst);
            if (copts.unroll_factor_limit < 0) {
                copts.unroll_factor_limit = 0;
                CError_Error(ERR_ILLEGAL_PRAGMA);
            }
        } else if (t == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "off")) {
                copts.unroll_factor_limit = 0;
            } else if (!strcmp(tkidentifier->name, "on")) {
                copts.unroll_factor_limit = 10;
            } else {
                CError_Error(ERR_ILLEGAL_PRAGMA);
                return;
            }
        } else {
            CError_Error(ERR_ILLEGAL_PRAGMA);
        }
        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "altivec_model")) {
        if (plex() == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "off")) {
                copts.altivec_model = 0;
            } else if (!strcmp(tkidentifier->name, "on")) {
                copts.altivec_model = 1;
            } else {
                CError_Error(ERR_ILLEGAL_PRAGMA);
                return;
            }
        } else {
            CError_Error(ERR_ILLEGAL_PRAGMA);
        }
        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "altivec_vrsave")) {
        if (plex() == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "off")) {
                CPrep_PushOption(OPT_OFFSET(altivec_vrsave), 0);
            } else if (!strcmp(tkidentifier->name, "on")) {
                CPrep_PushOption(OPT_OFFSET(altivec_vrsave), 1);
            } else if (!strcmp(tkidentifier->name, "allon")) {
                CPrep_PushOption(OPT_OFFSET(altivec_vrsave), 2);
            } else if (!strcmp(tkidentifier->name, "reset")) {
                CPrep_PopOption(OPT_OFFSET(altivec_vrsave));
            } else {
                CError_Error(ERR_ILLEGAL_PRAGMA);
                return;
            }
        } else {
            CError_Error(ERR_ILLEGAL_PRAGMA);
        }
        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "function_align")) {
        t = plex();
        if (t == TK_INTCONST) {
            value = CInt64_GetULong(&tkintconst);
            switch (value) {
                case 4:
                case 8:
                case 16:
                case 32:
                case 64:
                case 128:
                    CPrep_PushOption(OPT_OFFSET(function_align), value);
                    break;
                default:
                    PPCError_Warning(PPCERR_FUNCTION_ALIGN_EXPECTED_INT);
                    CodeGen_EOLCheck();
                    return;
            }
        } else if (t == TK_IDENTIFIER && !strcmp(tkidentifier->name, "reset")) {
            CPrep_PopOption(OPT_OFFSET(function_align));
        } else {
            PPCError_Warning(PPCERR_FUNCTION_ALIGN_EXPECTED_INT);
        }
        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "processor")) {
        if (cscope_currentfunc) {
            PPCError_Warning(PPCERR_PRAGMA_IGNORED_IN_FUNCTION, "pragma processor");
            return;
        }
        t = plex();
        if (t == TK_INTCONST) {
            switch (CInt64_GetULong(&tkintconst)) {
                case 401:
                    copts.processor = CPU_PPC401;
                    break;
                case 403:
                    copts.processor = CPU_PPC403;
                    break;
                case 505:
                    copts.processor = CPU_PPC505;
                    break;
                case 509:
                    copts.processor = CPU_PPC509;
                    break;
                case 555:
                    copts.processor = CPU_PPC555;
                    break;
                case 556:
                    copts.processor = CPU_PPC556;
                    break;
                case 565:
                    copts.processor = CPU_PPC565;
                    break;
                case 601:
                    copts.processor = CPU_PPC601;
                    break;
                case 602:
                    copts.processor = CPU_PPC602;
                    break;
                case 8240:
                    copts.processor = CPU_PPC8240;
                    break;
                case 8260:
                    copts.processor = CPU_PPC8260;
                    break;
                case 603:
                    copts.processor = CPU_PPC603;
                    break;
                case 604:
                    copts.processor = CPU_PPC604;
                    break;
                case 740:
                    copts.processor = CPU_PPC740;
                    break;
                case 750:
                    copts.processor = CPU_PPC750;
                    break;
                case 801:
                    copts.processor = CPU_PPC801;
                    break;
                case 821:
                    copts.processor = CPU_PPC821;
                    break;
                case 823:
                    copts.processor = CPU_PPC823;
                    break;
                case 850:
                    copts.processor = CPU_PPC850;
                    break;
                case 860:
                    copts.processor = CPU_PPC860;
                    break;
                case 7400:
                    copts.processor = CPU_PPC7400;
                    break;
                default:
                    PPCError_Warning(PPCERR_PROCESSOR_PRAGMA_SYNTAX);
                    CodeGen_EOLCheck();
                    return;
            }
        } else if (t == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "generic")) {
                copts.processor = CPU_Generic;
            } else if (!strcmp(tkidentifier->name, "603e")) {
                copts.processor = CPU_PPC603e;
            } else if (!strcmp(tkidentifier->name, "604e")) {
                copts.processor = CPU_PPC604e;
            } else if (!strcmp(tkidentifier->name, "PPC603e")) {
                copts.processor = CPU_PPC603e;
            } else if (!strcmp(tkidentifier->name, "PPC604e")) {
                copts.processor = CPU_PPC604e;
            } else if (!strcmp(tkidentifier->name, "gekko") ||
                       !strcmp(tkidentifier->name, "gecko")) {
                copts.processor = CPU_PPCGEKKO;
            } else {
                PPCError_Warning(PPCERR_PROCESSOR_PRAGMA_SYNTAX);
            }
        } else {
            PPCError_Warning(PPCERR_PROCESSOR_PRAGMA_SYNTAX);
        }

        update_bc_range(copts.prepare_compress && !copts.switch_tables);

        if ((str = CMach_GetCPU())) {
            CPrep_InsertSpecialMacro(&ppc_cpu, str);
        }

        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "min_struct_alignment")) {
        t = plex();
        if (t == TK_INTCONST) {
            value = CInt64_GetULong(&tkintconst);
            switch (value) {
                case 4:
                case 8:
                case 16:
                case 32:
                case 64:
                case 128:
                    CPrep_PushOption(OPT_OFFSET(min_struct_alignment), value);
                    break;
                default:
                    PPCError_Warning(PPCERR_MIN_STRUCT_ALIGNMENT_EXPECTED_INT);
                    CodeGen_EOLCheck();
                    return;
            }
        } else if (t == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "reset")) {
                CPrep_PopOption(OPT_OFFSET(min_struct_alignment));
            } else if (!strcmp(tkidentifier->name, "on")) {
                CPrep_PushOption(OPT_OFFSET(min_struct_alignment), 4);
            } else if (!strcmp(tkidentifier->name, "off")) {
                CPrep_PushOption(OPT_OFFSET(min_struct_alignment), 1);
            }
        } else {
            PPCError_Warning(PPCERR_FUNCTION_ALIGN_EXPECTED_INT);
        }

        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "section")) {
        if (cscope_currentfunc) {
            PPCError_Warning(PPCERR_PRAGMA_IGNORED_IN_FUNCTION, "pragma section");
            CodeGen_EOLCheck();
            return;
        }
        parse_section_pragma();
        return;
    }

    if (!strcmp(name->name, "rel109_offset")) {
        if (plex() == TK_INTCONST) {
            value = CInt64_GetULong(&tkintconst);
            if (value < 0 || value > 3) {
                PPCError_Warning(PPCERR_REL109_OFFSET_EXPECTED_INT);
                CodeGen_EOLCheck();
                return;
            }
            copts.rel109_offset = value;
            CodeGen_EOLCheck();
            return;
        }
        PPCError_Warning(PPCERR_REL109_OFFSET_EXPECTED_INT);
        CodeGen_EOLCheck();
        return;
    }

    if (!strcmp(name->name, "interrupt")) {
        memset(&info, 0, sizeof(info));
        info.kind = InterruptRecord_Interrupt;
        while (1) {
            if (plex() != TK_IDENTIFIER) {
                break;
            }
            if (!strcmp("enable", tkidentifier->name)) {
                info.flags |= Interrupt_Enable;
            } else if (!strcmp("SRR", tkidentifier->name)) {
                info.flags |= Interrupt_SRR;
            } else if (!strcmp("DAR", tkidentifier->name)) {
                info.flags |= Interrupt_DAR;
            } else if (!strcmp("DSISR", tkidentifier->name)) {
                info.flags |= Interrupt_DSISR;
            } else if (!strcmp("fprs", tkidentifier->name)) {
                info.flags |= Interrupt_FPRs;
            } else if (!strcmp("vrs", tkidentifier->name)) {
                info.flags |= Interrupt_VRs;
            } else if (!strcmp("nowarn", tkidentifier->name)) {
                info.flags |= Interrupt_NoWarn;
            } else if (!strcmp("on", tkidentifier->name)) {
                info.flags |= Interrupt_On;
                copts.interruptOptions = galloc(sizeof(InterruptGenerationRecord));
                memclrw(copts.interruptOptions, sizeof(InterruptGenerationRecord));
                rec = copts.interruptOptions;
                *rec = info;
                list = galloc(sizeof(InterruptList));
                list->next = copts.interruptList;
                copts.interruptList = list;
                list->info = rec;
                CodeGen_EOLCheck();
                return;
            } else if (!strcmp("off", tkidentifier->name)) {
                if (info.flags) {
                    CError_Error(ERR_ILLEGAL_PRAGMA);
                    CodeGen_EOLCheck();
                    return;
                }
                copts.interruptList = NULL;
                copts.interruptOptions = NULL;
                CodeGen_EOLCheck();
                return;
            } else if (!strcmp("reset", tkidentifier->name)) {
                if (info.flags) {
                    CError_Error(ERR_ILLEGAL_PRAGMA);
                    CodeGen_EOLCheck();
                    return;
                }
                if (copts.interruptList->next) {
                    copts.interruptOptions = copts.interruptList->next->info;
                } else {
                    copts.interruptList = NULL;
                    copts.interruptOptions = NULL;
                }
                CodeGen_EOLCheck();
                return;
            } else {
                CError_Error(ERR_ILLEGAL_PRAGMA);
                CodeGen_EOLCheck();
                return;
            }
        }
        CError_Error(ERR_ILLEGAL_PRAGMA);
        CodeGen_EOLCheck();
        return;
    }

    if (copts.warn_illpragma) {
        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
    }

    if (plex() != TK_EOL) {
        do {
            t = plex();
        } while (t != TK_EOL && t != 0);
    }
}

#define INTERRUPT_RECORD(sym, record) \
    if (!((record) = (sym)->interruptInfo)) { \
        (record) = galloc(sizeof(InterruptGenerationRecord)); \
        memclrw((record), sizeof(InterruptGenerationRecord)); \
        (sym)->interruptInfo = (record); \
        (record)->next = interrupt_generation_records; \
        interrupt_generation_records = (record); \
        (record)->id = --previous_ps_ID; \
        (record)->kind = InterruptRecord_Interrupt; \
    }

void CodeGen_UpdateObject(Object* object) {
    ObjRegInfo reginfo;
    RegInfo* scan;
    BE_SymNode* sym;
    InterruptGenerationRecord* record;
    InterruptGenerationRecord* funcRecord;

    if (object->datatype == DDATA && object->u.data.reginfo.reg) {
        reginfo = object->u.data.reginfo;
        for (scan = reg_info_list; scan; scan = scan->next) {
            if (reginfo.rclass == scan->reginfo.rclass) {
                if (reginfo.reg == scan->reginfo.reg) {
                    scan->object = object;
                }
                break;
            }
        }
    }

    if (!object->section) {
        if (!object->name) {
            return;
        }
        object->section = get_current_pseudo_section_info()->id;
    }

    if ((object->datatype == DFUNC || object->datatype == DVFUNC) &&
        object->u.func.interruptflags) {
        if (!object->name) {
            return;
        }
        object->qual |= Q_INTERRUPT;
        sym = MakeSymbolTableEntry(object);
        INTERRUPT_RECORD(sym, funcRecord);
        funcRecord->flags |= object->u.func.interruptflags;
    } else if (copts.interruptOptions && object->datatype != DDATA) {
        if (!object->name) {
            return;
        }
        object->qual |= Q_INTERRUPT;
        sym = MakeSymbolTableEntry(object);
        INTERRUPT_RECORD(sym, record);
        record->flags |= copts.interruptOptions->flags;
    }
}

void CodeGen_UpdateBackEndOptions(void) {
    copts.globaloptimizer = TRUE;
}

SInt32 CodeGen_objc_method_self_offset(ObjCMethod* meth) {
    SInt32 size;

    if (!meth->return_type) {
        size = 4;
    } else if (IS_TYPE_ARRAY(meth->return_type) ||
               IS_TYPE_NONVECTOR_STRUCT(meth->return_type) ||
               IS_TYPE_CLASS(meth->return_type) ||
               IS_TYPE_12BYTES_MEMBERPOINTER(meth->return_type)) {
        size = 8;
    } else {
        size = meth->return_type->size;
    }

    if (size == 0) {
        size = 1;
    }

    return (size + 3) & ~3;
}

SInt32 CodeGen_objc_method_sel_offset(ObjCMethod* meth) {
    return (CodeGen_objc_method_self_offset(meth) + 7) & ~3;
}

SInt32 CodeGen_objc_method_arg_offset(ObjCMethod* meth, ObjCMethodArg* arg) {
    SInt32 pos;
    ObjCMethodArg* scan;

    pos = CodeGen_objc_method_sel_offset(meth) + 4;
    for (scan = meth->selector_args; scan; scan = scan->next) {
        if (scan == arg) {
            return pos;
        }

        if (scan->type == NULL) {
            pos += 4;
        } else {
            pos += scan->type->size;
        }

        pos = (pos + 3) & ~3;
    }

    return 0;
}

SInt32 CodeGen_objc_method_args_size(ObjCMethod* meth) {
    SInt32 size;
    ObjCMethodArg* scan;

    size = CodeGen_objc_method_self_offset(meth);
    for (scan = meth->selector_args; scan; scan = scan->next) {
        if (scan->next == NULL && scan->type == NULL) {
            return size;
        }

        size = (size + 3) & ~3;
        if (scan->type == NULL) {
            size += 4;
        } else {
            size += scan->type->size;
        }
    }

    return size;
}

InterruptGenerationRecord* CodeGen_FindInterruptGenerationRecord(SInt16 key) {
    UInt16 id = key;
    InterruptGenerationRecord* entry = interrupt_generation_records;
    while (entry) {
        if (entry->id == id) {
            break;
        }
        entry = entry->next;
    }
    return entry;
}

ENode* CodeGen_HandleIntrinsicCall(Object* func, ENodeList* arg_exprs) {
    return Intrinsics_HandleIntrinsicCall(func, arg_exprs);
}

ENode* CodeGen_HandleTypeCast(ENode* expr, Type* type, UInt32 qual) {
    short flags;

    if (copts.altivec_model) {
        flags = qual & ENODE_FLAG_QUALS;
        if (IS_TYPE_STRUCT(type) && IS_TYPE_STRUCT(expr->rtype) && expr->flags == flags) {
            switch (TYPE_STRUCT(type)->stype) {
                case STRUCT_VECTOR_UCHAR:
                case STRUCT_VECTOR_SCHAR:
                case STRUCT_VECTOR_BCHAR:
                case STRUCT_VECTOR_USHORT:
                case STRUCT_VECTOR_SSHORT:
                case STRUCT_VECTOR_BSHORT:
                case STRUCT_VECTOR_UINT:
                case STRUCT_VECTOR_SINT:
                case STRUCT_VECTOR_BINT:
                case STRUCT_VECTOR_FLOAT:
                case STRUCT_VECTOR_PIXEL:
                    expr = makemonadicnode(expr, ETYPCON);
                    expr->rtype = type;
                    expr->flags = flags;
                    return expr;
            }
        }
    }

    return NULL;
}

short CodeGen_AssignCheck(
    const ENode* expr, const Type* type, Boolean convert, Boolean checkAccess
) {
    TypeStruct* targetType = TYPE_STRUCT(type);
    TypeStruct* exprType = TYPE_STRUCT(expr->rtype);
    SInt32 result;

    if (copts.altivec_model &&
        IS_TYPE_VECTOR(targetType) &&
        IS_TYPE_VECTOR(exprType) &&
        targetType->stype == exprType->stype) {
        result = CheckResult_Conversion;
    } else {
        result = CheckResult_NoMatch;
    }
    return result;
}

Boolean CodeGen_CollapseVectorExpression(ENode* expr, MWVector128* vec, Type* type) {
    Boolean result;
    int i;
    ENode* escan;
    CInt64 uc_elem;
    Float fv;
    Float splat_fv;
    Float float_fv;
    CInt64 ul_splat;
    CInt64 us_splat;
    CInt64 uc_splat;
    CInt64 ul_first;
    CInt64 ul_elem;
    CInt64 us_first;
    CInt64 us_elem;
    CInt64 uc_first;

    result = FALSE;
    for (i = 0; i < 4; i++) {
        vec->ul[i] = 0;
    }

    if (ENODE_IS(expr, ECOMMA)) {
        i = 0;
        escan = expr;
        while (ENODE_IS(escan, ECOMMA)) {
            i++;
            escan = escan->data.diadic.left;
        }
        switch (TYPE_STRUCT(type)->stype) {
            case STRUCT_VECTOR_UCHAR:
            case STRUCT_VECTOR_SCHAR:
            case STRUCT_VECTOR_BCHAR:
                if (i < 15) {
                    PPCError_Error(PPCERR_TOO_FEW_VECTOR_INITIALIZERS, type, 0);
                } else if (i > 15) {
                    PPCError_Error(PPCERR_TOO_MANY_VECTOR_INITIALIZERS, type, 0);
                } else {
                    escan = expr;
                    i = 15;
                    while (ENODE_IS(escan, ECOMMA)) {
                        ENode* e = escan->data.diadic.right;
                        uc_elem = e->data.intval;
                        if (!ENODE_IS(e, EINTCONST)) {
                            PPCError_Error(PPCERR_ILLEGAL_VECTOR_INITIALIZATION);
                            break;
                        }

                        if (copts.pedantic) {
                            if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_UCHAR) {
                                if (!CInt64_IsInURange(uc_elem, 1)) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            } else if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_BCHAR) {
                                if (uc_elem.lo != 0 && uc_elem.lo != 255) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            } else {
                                if (!CInt64_IsInRange(uc_elem, 1)) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            }
                        }

                        vec->uc[i] = (UInt8)uc_elem.lo;
                        escan = escan->data.diadic.left;
                        i--;
                    }

                    if (ENODE_IS(escan, EINTCONST)) {
                        uc_first = escan->data.intval;
                        if (copts.pedantic) {
                            if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_UCHAR) {
                                if (!CInt64_IsInURange(uc_first, 1)) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            } else if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_BCHAR) {
                                if (uc_first.lo != 0 && uc_first.lo != 255) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            } else {
                                if (!CInt64_IsInRange(uc_first, 1)) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            }
                        }
                        vec->uc[0] = (UInt8)uc_first.lo;
                    } else {
                        PPCError_Error(PPCERR_ILLEGAL_VECTOR_INITIALIZATION);
                        break;
                    }
                    result = TRUE;
                }
                break;

            case STRUCT_VECTOR_USHORT:
            case STRUCT_VECTOR_SSHORT:
            case STRUCT_VECTOR_BSHORT:
            case STRUCT_VECTOR_PIXEL:
                if (i < 7) {
                    PPCError_Error(PPCERR_TOO_FEW_VECTOR_INITIALIZERS, type, 0);
                } else if (i > 7) {
                    PPCError_Error(PPCERR_TOO_MANY_VECTOR_INITIALIZERS, type, 0);
                } else {
                    escan = expr;
                    i = 7;
                    while (ENODE_IS(escan, ECOMMA)) {
                        ENode* e = escan->data.diadic.right;
                        us_elem = e->data.intval;
                        if (!ENODE_IS(e, EINTCONST)) {
                            PPCError_Error(PPCERR_ILLEGAL_VECTOR_INITIALIZATION);
                            break;
                        }

                        if (copts.pedantic) {
                            if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_USHORT ||
                                TYPE_STRUCT(type)->stype == STRUCT_VECTOR_PIXEL) {
                                if (!CInt64_IsInURange(us_elem, 2)) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            } else if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_BSHORT) {
                                if (us_elem.lo != 0 && us_elem.lo != 0xFFFF) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            } else {
                                if (!CInt64_IsInRange(us_elem, 2)) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            }
                        }

                        vec->us[i] = (UInt16)e->data.intval.lo;
                        escan = escan->data.diadic.left;
                        i--;
                    }

                    if (ENODE_IS(escan, EINTCONST)) {
                        us_first = escan->data.intval;
                        if (copts.pedantic) {
                            if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_USHORT ||
                                TYPE_STRUCT(type)->stype == STRUCT_VECTOR_PIXEL) {
                                if (!CInt64_IsInURange(us_first, 2)) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            } else if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_BSHORT) {
                                if (us_first.lo != 0 && us_first.lo != 0xFFFF) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            } else {
                                if (!CInt64_IsInRange(us_first, 2)) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            }
                        }
                        vec->us[0] = (UInt16)us_first.lo;
                    } else {
                        PPCError_Error(PPCERR_ILLEGAL_VECTOR_INITIALIZATION);
                        break;
                    }
                    result = TRUE;
                }
                break;

            case STRUCT_VECTOR_UINT:
            case STRUCT_VECTOR_SINT:
            case STRUCT_VECTOR_BINT:
                if (i < 3) {
                    PPCError_Error(PPCERR_TOO_FEW_VECTOR_INITIALIZERS, type, 0);
                } else if (i > 3) {
                    PPCError_Error(PPCERR_TOO_MANY_VECTOR_INITIALIZERS, type, 0);
                } else {
                    escan = expr;
                    i = 3;
                    while (ENODE_IS(escan, ECOMMA)) {
                        ENode* e = escan->data.diadic.right;
                        ul_elem = e->data.intval;
                        if (!ENODE_IS(e, EINTCONST)) {
                            PPCError_Error(PPCERR_ILLEGAL_VECTOR_INITIALIZATION);
                            break;
                        }

                        if (copts.pedantic) {
                            if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_UINT) {
                                if (!CInt64_IsInURange(ul_elem, 4)) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            } else if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_BINT) {
                                if (ul_elem.lo != 0 && ul_elem.lo != (UInt32)-1) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            } else {
                                if (!CInt64_IsInRange(ul_elem, 4)) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            }
                        }

                        vec->ul[i] = e->data.intval.lo;
                        escan = escan->data.diadic.left;
                        i--;
                    }

                    if (ENODE_IS(escan, EINTCONST)) {
                        ul_first = escan->data.intval;
                        if (copts.pedantic) {
                            if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_UINT) {
                                if (!CInt64_IsInURange(ul_first, 4)) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            } else if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_BINT) {
                                if (ul_first.lo != 0 && ul_first.lo != (UInt32)-1) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            } else {
                                if (!CInt64_IsInRange(ul_first, 4)) {
                                    PPCError_Warning(
                                        PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0
                                    );
                                }
                            }
                        }
                        vec->ul[0] = ul_first.lo;
                    } else {
                        PPCError_Error(PPCERR_ILLEGAL_VECTOR_INITIALIZATION);
                        break;
                    }
                    result = TRUE;
                }
                break;

            case STRUCT_VECTOR_FLOAT:
                if (i < 3) {
                    PPCError_Error(PPCERR_TOO_FEW_VECTOR_INITIALIZERS, type, 0);
                } else if (i > 3) {
                    PPCError_Error(PPCERR_TOO_MANY_VECTOR_INITIALIZERS, type, 0);
                } else {
                    escan = expr;
                    i = 3;
                    while (ENODE_IS(escan, ECOMMA)) {
                        if (ENODE_IS(escan->data.diadic.right, EFLOATCONST)) {
                            fv = escan->data.diadic.right->data.floatval;
                        } else if (ENODE_IS(escan->data.diadic.right, EINTCONST)) {
                            fv = CMach_CalcFloatConvertFromInt(
                                escan->data.diadic.right->rtype,
                                escan->data.diadic.right->data.intval
                            );
                        } else {
                            PPCError_Error(PPCERR_ILLEGAL_VECTOR_INITIALIZATION);
                            break;
                        }

                        CMach_InitFloatMem(TYPE(&stfloat), fv, (UInt8*)&vec->f[i]);
                        escan = escan->data.diadic.left;
                        i--;
                    }

                    if (ENODE_IS(escan, EFLOATCONST)) {
                        fv = escan->data.floatval;
                    } else if (ENODE_IS(escan, EINTCONST)) {
                        fv = CMach_CalcFloatConvertFromInt(escan->rtype, escan->data.intval);
                    } else {
                        PPCError_Error(PPCERR_ILLEGAL_VECTOR_INITIALIZATION);
                        break;
                    }

                    CMach_InitFloatMem(TYPE(&stfloat), fv, (UInt8*)&vec->f[0]);
                    result = TRUE;
                }
                break;
        }
    } else if (ENODE_IS(expr, EINTCONST)) {
        i = 0;
        switch (TYPE_STRUCT(type)->stype) {
            case STRUCT_VECTOR_UCHAR:
            case STRUCT_VECTOR_SCHAR:
            case STRUCT_VECTOR_BCHAR: {
                uc_splat = expr->data.intval;
                if (copts.pedantic) {
                    if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_UCHAR) {
                        if (!CInt64_IsInURange(uc_splat, 1)) {
                            PPCError_Warning(PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0);
                        }
                    } else {
                        if (!CInt64_IsInRange(uc_splat, 1)) {
                            PPCError_Warning(PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0);
                        }
                    }
                }
                while (i < 16) {
                    vec->uc[i++] = (UInt8)uc_splat.lo;
                }
                result = TRUE;
                break;
            }
            case STRUCT_VECTOR_USHORT:
            case STRUCT_VECTOR_SSHORT:
            case STRUCT_VECTOR_BSHORT:
            case STRUCT_VECTOR_PIXEL: {
                us_splat = expr->data.intval;
                if (copts.pedantic) {
                    if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_USHORT ||
                        TYPE_STRUCT(type)->stype == STRUCT_VECTOR_PIXEL) {
                        if (!CInt64_IsInURange(us_splat, 2)) {
                            PPCError_Warning(PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0);
                        }
                    } else {
                        if (!CInt64_IsInRange(us_splat, 2)) {
                            PPCError_Warning(PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0);
                        }
                    }
                }
                while (i < 8) {
                    vec->us[i++] = (UInt16)us_splat.lo;
                }
                result = TRUE;
                break;
            }
            case STRUCT_VECTOR_UINT:
            case STRUCT_VECTOR_SINT:
            case STRUCT_VECTOR_BINT: {
                ul_splat = expr->data.intval;
                if (copts.pedantic) {
                    if (TYPE_STRUCT(type)->stype == STRUCT_VECTOR_UINT) {
                        if (!CInt64_IsInURange(ul_splat, 4)) {
                            PPCError_Warning(PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0);
                        }
                    } else {
                        if (!CInt64_IsInRange(ul_splat, 4)) {
                            PPCError_Warning(PPCERR_VECTOR_INITIALIZER_OUT_OF_RANGE, type, 0);
                        }
                    }
                }
                while (i < 4) {
                    vec->ul[i++] = ul_splat.lo;
                }
                result = TRUE;
                break;
            }
            case STRUCT_VECTOR_FLOAT: {
                if (!CInt64_IsInRange(expr->data.intval, 4)) {
                    PPCError_Error(PPCERR_ILLEGAL_VECTOR_INITIALIZATION);
                    break;
                }
                splat_fv = CMach_CalcFloatConvertFromInt(expr->rtype, expr->data.intval);
                for (; i < 4; i++) {
                    CMach_InitFloatMem(TYPE(&stfloat), splat_fv, (UInt8*)&vec->f[i]);
                }
                result = TRUE;
                break;
            }
            default:
                PPCError_Error(PPCERR_ILLEGAL_VECTOR_INITIALIZATION);
        }
    } else if (ENODE_IS(expr, EFLOATCONST)) {
        switch (TYPE_STRUCT(type)->stype) {
            case STRUCT_VECTOR_UCHAR:
            case STRUCT_VECTOR_SCHAR:
            case STRUCT_VECTOR_BCHAR:
            case STRUCT_VECTOR_USHORT:
            case STRUCT_VECTOR_SSHORT:
            case STRUCT_VECTOR_BSHORT:
            case STRUCT_VECTOR_UINT:
            case STRUCT_VECTOR_SINT:
            case STRUCT_VECTOR_BINT:
            case STRUCT_VECTOR_PIXEL:
            default:
                PPCError_Error(PPCERR_ILLEGAL_VECTOR_INITIALIZATION);
                break;
            case STRUCT_VECTOR_FLOAT: {
                int j = 0;
                float_fv = expr->data.floatval;
                CMach_InitFloatMem(TYPE(&stfloat), float_fv, (UInt8*)&vec->f[0]);
                while (j < 4) {
                    CMach_InitFloatMem(TYPE(&stfloat), float_fv, (UInt8*)&vec->f[j]);
                    j++;
                }
                result = TRUE;
                break;
            }
        }
    } else if (ENODE_IS2(expr, EINDIRECT, EFUNCCALL)) {
        if (!IS_TYPE_STRUCT(expr->rtype)) {
            PPCError_Error(PPCERR_ILLEGAL_VECTOR_INITIALIZATION);
        }
    } else if (!ENODE_IS(expr, EVECTOR128CONST)) {
        PPCError_Error(PPCERR_ILLEGAL_VECTOR_INITIALIZATION);
    }

    return result;
}

void CodeGen_InsertSpecialMacros(void) {
    const char* str;

    CPrep_InsertSpecialMacro(&vecM, "__VEC__");
    CPrep_InsertSpecialMacro(&altivecM, "__ALTIVEC__");
    CPrep_InsertSpecialMacro(&powcM, "powerc");
    CPrep_InsertSpecialMacro(&__powcM, "__powerc");
    CPrep_InsertSpecialMacro(&hostM, "__POWERPC__");
    CPrep_InsertSpecialMacro(&_ppc_M, "__ppc__");
    CPrep_InsertSpecialMacro(&cpuM, "__TARGET_CPU__");

    if (!copts.littleendian) {
        CPrep_InsertSpecialMacro(&bendM, "__BIG_ENDIAN__");
    } else {
        CPrep_InsertSpecialMacro(&bendM, "__LITTLE_ENDIAN__");
    }

    if ((str = CMach_GetCPU())) {
        CPrep_InsertSpecialMacro(&ppc_cpu, str);
    }

    CPrep_InsertSpecialMacro(&profM, "__profile__");
    CPrep_InsertSpecialMacro(&longI, "__fourbyteints__");
    CPrep_InsertSpecialMacro(&IEEED, "__IEEEdoubles__");

    if (copts.optimizationlevel > 0) {
        CPrep_InsertSpecialMacro(&optM, "__OPTIMIZE__");
    }

    CPrep_InsertSpecialMacro(&eabiM, "__PPC_EABI__");
}

char* CodeGen_ExpandSpecialMacro(Macro* macro) {
    const char* cpu;
    char* p;

    if (macro == &vecM) {
        return "10205";
    }
    if (macro == &altivecM) {
        return "100000000";
    }
    if (macro == &powcM) {
        return "1";
    }
    if (macro == &__powcM) {
        return "1";
    }
    if (macro == &hostM) {
        return "1";
    }
    if (macro == &bendM) {
        return "1";
    }
    if (macro == &_ppc_M) {
        return "1";
    }
    if (CMach_GetCPU() && macro == &ppc_cpu) {
        return "1";
    }
    if (macro == &cpuM) {
        cpu = CMach_GetCPU();
        p = target_cpu;
        *p++ = '"';
        while (*cpu) {
            if (*cpu != '_') {
                *p++ = *cpu;
                if (p >= target_cpu + sizeof(target_cpu)) {
                    break;
                }
            }
            cpu++;
        }
        p[0] = '"';
        p[1] = 0;
        return target_cpu;
    }
    if (macro == &profM) {
        return copts.profile ? "1" : "0";
    }
    if (macro == &longI) {
        return "1";
    }
    if (macro == &IEEED) {
        return "1";
    }
    if (macro == &optM) {
        sprintf(optimization_level, "%d", copts.optimizationlevel);
        return optimization_level;
    }

    if (macro == &eabiM) {
        return "1";
    }
    CError_FATAL(5079);
    return "0";
}

ENode* CodeGen_CheckExpr(ENode* expr) {
    ENode* right;
    SInt32 bits;
    SInt32 count;

    switch (expr->type) {
        case ETYPCON:
            if (IS_TYPE_VEC2X32FLOAT(expr->rtype)) {
                if (!(expr->data.monadic->rtype->type == TYPEFLOAT &&
                      !IS_TYPE_VEC2X32FLOAT(expr->data.monadic->rtype)) &&
                    expr->data.monadic->rtype->type != TYPEINT &&
                    expr->data.monadic->rtype->type != TYPEENUM) {
                    CError_Error(ERR_ILLEGAL_OPERATION);
                    return nullnode();
                }
            } else if (IS_TYPE_VEC2X32FLOAT(expr->data.monadic->rtype)) {
                CError_Error(ERR_ILLEGAL_OPERATION);
                return nullnode();
            }
            break;
        case ESHL:
        case ESHR:
        case ESHLASS:
        case ESHRASS:
            right = expr->data.diadic.right;
            if (right->type == EINTCONST) {
                bits = expr->data.diadic.left->rtype->size * 8;
                count = right->data.intval.lo;
                if (count < 0 || count >= bits) {
                    PPCError_Warning(PPCERR_SHIFT_OUT_OF_RANGE, count, (bits - 1) & count);
                }
            }
            break;
        case EMUL:
        case EDIV:
        case EADD:
        case ESUB:
            if (expr->rtype == (Type*)&stvec2x32float) {
                if (!IS_TYPE_VEC2X32FLOAT(expr->data.diadic.left->rtype)) {
                    CError_Error(ERR_ILLEGAL_OPERATION);
                    return nullnode();
                }
                if (!IS_TYPE_VEC2X32FLOAT(expr->data.diadic.right->rtype) &&
                    expr->data.diadic.right->type != ETYPCON) {
                    CError_Error(ERR_ILLEGAL_OPERATION);
                    return nullnode();
                }
            }
            break;
        case EMULV:
        case EMODULO:
        case EADDV:
        case ESUBV:
        case ELESS:
        case EGREATER:
        case ELESSEQU:
        case EGREATEREQU:
        case EEQU:
        case ENOTEQU:
        case EAND:
        case EXOR:
        case EOR:
        case ELAND:
        case ELOR:
        case EMULASS:
        case EDIVASS:
        case EMODASS:
        case EADDASS:
        case ESUBASS:
        case EANDASS:
        case EXORASS:
        case EORASS:
            if (IS_TYPE_VEC2X32FLOAT(expr->data.diadic.left->rtype)) {
                CError_Error(ERR_ILLEGAL_OPERATION);
                return nullnode();
            }
            if (IS_TYPE_VEC2X32FLOAT(expr->data.diadic.right->rtype)) {
                CError_Error(ERR_ILLEGAL_OPERATION);
                return nullnode();
            }
            break;
        default:
            break;
    }
    return expr;
}

void CodeGen_reportheapinfo(Boolean release_flag, char* name, char* text) {
    HeapInfo optimizer;
    HeapInfo all;

    CTool_GetHeapInfo(&optimizer, 3);
    CTool_GetHeapInfo(&all, 5);

    if (release_flag) {
        releaseoheap();
    }

    PPCError_Message(
        "%n:%u HEAP STATUS\n"
        "    optimizer: %i blocks, used: %i, free: %i, total: %i, largest free: %i\n"
        "          all: %i blocks, used: %i, free: %i, total: %i, largest free: %i",
        name,
        text,
        optimizer.blocks,
        optimizer.total_size - optimizer.total_free,
        optimizer.total_free,
        optimizer.total_size,
        optimizer.largest_free_block,
        all.blocks,
        all.total_size - all.total_free,
        all.total_free,
        all.total_size,
        all.largest_free_block
    );
}

static void CodeGen_heaperror(void) {
    CodeGen_reportheapinfo(TRUE, "?", "out of memory");
    if (saveheaperror) {
        setheaperror(saveheaperror);
        saveheaperror();
    }
}

void CodeGen_InitialSanityCheck(void) {
    ok_to_compress = FALSE;
    ok_for_global_registers = FALSE;
}
