#include "compiler/FunctionCalls.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CMachine.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/InstrSelection.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/Operands.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/Registers.h"
#include "compiler/StackFrame.h"
#include "compiler/StructMoves.h"
#include "compiler/types.h"

enum {
    AIF_PassInGPR = 1,
    AIF_PassInFPR = 2,
    AIF_PassOnStack = 4,
    AIF_ExtendTo32Bits = 8,
    AIF_ForceDoublePrecision = 0x10,
    AIF_PassInVR = 0x20,
    AIF_PassMask = AIF_PassInGPR | AIF_PassInFPR | AIF_PassOnStack | AIF_PassInVR
};

#pragma pack(push, 2)
typedef struct ArgInfo {
    struct ArgInfo* next;
    ENode* expr;
    Operand opnd;
    SInt32 offset;
    short gpr;
    short gprHi;
    short fpr;
    short vr;
    short evaluated;
    short flags;
} ArgInfo;
#pragma pack(pop)

static void branch_subroutine_indirect_ctr(Operand* addrOpnd, UInt32* used_regs);

static ArgInfo* make_arginfo(ENode* expr) {
    ArgInfo* info = lalloc(sizeof(ArgInfo));
    memclrw(info, sizeof(ArgInfo));

    info->next = NULL;
    info->expr = expr;
    info->offset = -1;
    info->gpr = -1;
    info->gprHi = -1;
    info->fpr = -1;
    info->vr = -1;
    info->evaluated = FALSE;
    info->flags = 0;

    return info;
}

static ArgInfo* analyze_arguments(
    ENode* funcref,
    ENodeList* arg_expr,
    FuncArg* arg,
    UInt32* used_regs,
    Boolean* resultHasFloats,
    char has_varargs
) {
    ArgInfo* infos;
    SInt32 displ;
    SInt32 arg_size;
    int gpr_counter;
    int fpr_counter;
    int vr_counter;
    Type* type;
    ArgInfo* info;
    RegClass rclass;
    Boolean spilledVectorFlag;

    infos = NULL;
    displ = 0;
    gpr_counter = 3;
    fpr_counter = 1;
    vr_counter = 2;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        used_regs[rclass] = 0;
    }
    *resultHasFloats = FALSE;

    while (arg_expr) {
        if (arg_expr->node == funcref) {
            arg_expr = arg_expr->next;
            arg = arg->next;
            continue;
        }

        type = arg_expr->node->rtype;
        if (infos) {
            info->next = make_arginfo(arg_expr->node);
            info = info->next;
        } else {
            infos = info = make_arginfo(arg_expr->node);
        }

        arg_size = 0;

        if ((TYPE_IS_8BYTES(type) ||
             (TYPE_IS_SOFT_FLOAT(type) &&
              type->size != 4 &&
              !copts.incompatible_sfpe_double_params)) &&
            (gpr_counter % 2) == 0) {
            gpr_counter++;
        }

        if (IS_TYPE_VECTOR(type)) {
            if (arg == &elipsis) {
                spilledVectorFlag = TRUE;
                info->flags |= AIF_PassOnStack;
            } else {
                spilledVectorFlag = FALSE;
                if (vr_counter <= 13) {
                    info->flags |= AIF_PassInVR;
                    info->vr = vr_counter;
                    used_regs[RegClass_VR] |= 1 << vr_counter;
                } else {
                    spilledVectorFlag = TRUE;
                    info->flags |= AIF_PassOnStack;
                }
            }

            if (spilledVectorFlag) {
                arg_size = 16;
            }
            vr_counter++;
        } else if (TYPE_IS_SOFT_FLOAT(type)) {
            if (type->size == 4) {
                if (gpr_counter <= 10) {
                    info->flags |= AIF_PassInGPR;
                    info->gpr = gpr_counter;
                    used_regs[RegClass_GPR] |= 1 << gpr_counter;
                } else {
                    info->flags |= AIF_PassOnStack;
                }
                arg_size = 4;
                gpr_counter++;
            } else {
                if (gpr_counter < 10) {
                    info->flags |= AIF_PassInGPR;
                    if (copts.littleendian) {
                        info->gpr = gpr_counter;
                        info->gprHi = gpr_counter + 1;
                    } else {
                        info->gpr = gpr_counter + 1;
                        info->gprHi = gpr_counter;
                    }
                    used_regs[RegClass_GPR] |= 1 << gpr_counter;
                    used_regs[RegClass_GPR] |= 1 << (gpr_counter + 1);
                } else {
                    info->flags |= AIF_PassOnStack;
                }
                arg_size = 8;
                gpr_counter += 2;
            }
        } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
            *resultHasFloats = TRUE;
            if (!arg || arg == &oldstyle || arg == &elipsis) {
                if (fpr_counter <= 8) {
                    info->flags |= AIF_PassInFPR;
                    info->fpr = fpr_counter;
                    used_regs[RegClass_FPR] |= 1 << fpr_counter;
                } else {
                    info->flags |= AIF_PassOnStack | AIF_ForceDoublePrecision;
                }
                arg_size = 8;
                fpr_counter++;
            } else {
                if (fpr_counter <= 8) {
                    info->flags |= AIF_PassInFPR;
                    info->fpr = fpr_counter;
                    used_regs[RegClass_FPR] |= 1 << fpr_counter;
                } else {
                    info->flags |= AIF_PassOnStack;
                }

                if (type->size == 4) {
                    arg_size = 4;
                } else {
                    arg_size = 8;
                }
                fpr_counter++;
            }
        } else if (TYPE_IS_8BYTES(type)) {
            if (gpr_counter <= 10) {
                info->flags |= AIF_PassInGPR;
                if (copts.littleendian) {
                    info->gpr = gpr_counter;
                    info->gprHi = gpr_counter + 1;
                } else {
                    info->gpr = gpr_counter + 1;
                    info->gprHi = gpr_counter;
                }
                used_regs[RegClass_GPR] |= 1 << gpr_counter;
                if ((gpr_counter + 1) <= 10) {
                    used_regs[RegClass_GPR] |= 1 << (gpr_counter + 1);
                }
            } else {
                info->flags |= AIF_PassOnStack;
            }

            arg_size = 8;
            gpr_counter += 2;
        } else if (TYPE_FITS_IN_REGISTER(type)) {
            if ((!arg || arg == &elipsis || arg == &oldstyle) && type->size < 4) {
                info->flags |= AIF_ExtendTo32Bits;
            }

            if (gpr_counter <= 10) {
                info->flags |= AIF_PassInGPR;
                info->gpr = gpr_counter;
                used_regs[RegClass_GPR] |= 1 << gpr_counter;
            } else {
                info->flags |= AIF_PassOnStack;
            }

            gpr_counter++;
            arg_size = 4;
        } else if (IS_TYPE_VEC2X32FLOAT(type) && copts.processor == CPU_PPCGEKKO) {
            if (fpr_counter <= 8) {
                info->flags |= AIF_PassInFPR;
                info->fpr = fpr_counter;
                used_regs[RegClass_FPR] |= 1 << fpr_counter;
            } else {
                info->flags |= AIF_PassOnStack;
            }
            arg_size = 8;
            fpr_counter++;
        } else if (IS_TYPE_ARRAY(type) ||
                   IS_TYPE_NONVECTOR_STRUCT(type) ||
                   IS_TYPE_CLASS(type) ||
                   IS_TYPE_12BYTES_MEMBERPOINTER(type)) {
            SInt32 gprs_needed;

            CError_FATAL(398);
            gprs_needed = (type->size >> 2) + ((type->size & 3) != 0);
            if (gpr_counter <= 10) {
                if ((gpr_counter + gprs_needed - 1) <= 10) {
                    info->flags |= AIF_PassInGPR;
                    info->gpr = gpr_counter;
                    used_regs[RegClass_GPR] |= ((1 << gprs_needed) - 1) << gpr_counter;
                } else {
                    info->flags |= AIF_PassInGPR | AIF_PassOnStack;
                    info->gpr = gpr_counter;
                    used_regs[RegClass_GPR] |= ((1 << (11 - gpr_counter)) - 1) << gpr_counter;
                }
            } else {
                info->flags |= AIF_PassOnStack;
            }

            gpr_counter += gprs_needed;
            arg_size = type->size;
        } else {
            CError_FATAL(421);
        }

        displ = set_out_param_displ(
            displ, type, info->flags & AIF_PassOnStack, &info->offset, arg_size
        );

        arg_expr = arg_expr->next;
        if (arg && arg != &elipsis && arg != &oldstyle) {
            arg = arg->next;
        }
    }

    update_out_param_size(displ);

    return infos;
}

static void pass_in_memory(ArgInfo* info) {
    Type* type;
    Operand opnd;

    type = info->expr->rtype;
    memclrw(&opnd, sizeof(Operand));

    if (TYPE_FITS_IN_REGISTER(type) || TYPE_IS_SOFT_FLOAT(type)) {
        if (TYPE_IS_8BYTES(type) || (TYPE_IS_SOFT_FLOAT(type) && type->size != 4)) {
            if (!info->evaluated) {
                GEN_NODE(info->expr, &info->opnd);
            }
            coerce_to_register_pair(&info->opnd, type, 0, 0);

            load_store_register(
                PC_STW,
                info->opnd.reg,
                1,
                NULL,
                low_offset + out_param_displ_to_offset(info->offset)
            );
            load_store_register(
                PC_STW,
                info->opnd.regHi,
                1,
                NULL,
                high_offset + out_param_displ_to_offset(info->offset)
            );
        } else {
            if (!info->evaluated) {
                GEN_NODE(info->expr, &info->opnd);
            }
            if (info->flags & AIF_ExtendTo32Bits) {
                extend32(&info->opnd, type, 0);
            }
            ENSURE_GPR(&info->opnd, type, 0);

            load_store_register(
                PC_STW, info->opnd.reg, 1, NULL, out_param_displ_to_offset(info->offset)
            );
        }
    } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
        if (!info->evaluated) {
            GEN_NODE(info->expr, &info->opnd);
        }
        ENSURE_FPR(&info->opnd, type, 0);

        if (type->size == 4 && !(info->flags & AIF_ForceDoublePrecision)) {
            load_store_register(
                PC_STFS, info->opnd.reg, 1, NULL, out_param_displ_to_offset(info->offset)
            );
        } else {
            load_store_register(
                PC_STFD, info->opnd.reg, 1, NULL, out_param_displ_to_offset(info->offset)
            );
        }
    } else if (IS_TYPE_VECTOR(type)) {
        if (!info->evaluated) {
            GEN_NODE(info->expr, &info->opnd);
        }
        ENSURE_VR(&info->opnd, type, 0);

        load_store_register(
            PC_STVX, info->opnd.reg, 1, NULL, out_param_displ_to_offset(info->offset)
        );
    } else if (IS_TYPE_VEC2X32FLOAT(type) && copts.processor == CPU_PPCGEKKO) {
        if (!info->evaluated) {
            GEN_NODE(info->expr, &info->opnd);
        }
        ENSURE_FPR(&info->opnd, type, 0);

        gecko_load_store_register(
            info->opnd.reg, 1, NULL, out_param_displ_to_offset(info->offset), 0, SPR_GQR0, 0
        );
    } else {
        opnd.optype = OpndType_IndirectGPR_ImmOffset;
        opnd.reg = 1;
        opnd.object = NULL;
        opnd.immOffset = out_param_displ_to_offset(info->offset);

        if (!info->evaluated) {
            GEN_NODE(info->expr, &info->opnd);
        }

        move_block(&opnd, &info->opnd, type->size, CMach_ArgumentAlignment(type));
    }
}

static void pass_in_register(ArgInfo* info) {
    Type* type;

    type = info->expr->rtype;

    if ((info->flags & AIF_PassMask) == AIF_PassInFPR && !IS_TYPE_VEC2X32FLOAT(type)) {
        if (!info->evaluated) {
            GEN_NODE_TO_REG(info->expr, info->fpr, 0, &info->opnd);
        }
        ENSURE_FPR(&info->opnd, type, info->fpr);
        if (info->opnd.reg != info->fpr) {
            emitpcode(PC_FMR, info->fpr, info->opnd.reg);
        }
    } else if ((info->flags & AIF_PassMask) == AIF_PassInVR) {
        if (!info->evaluated) {
            GEN_NODE_TO_REG(info->expr, info->vr, 0, &info->opnd);
        }
        ENSURE_VR(&info->opnd, type, info->vr);
        if (info->opnd.reg != info->vr) {
            emitpcode(PC_VMR, info->vr, info->opnd.reg);
        }
    } else if (IS_TYPE_VEC2X32FLOAT(type) && copts.processor == CPU_PPCGEKKO) {
        if (!info->evaluated) {
            GEN_NODE_TO_REG(info->expr, info->fpr, 0, &info->opnd);
        }
        ENSURE_FPR(&info->opnd, type, info->fpr);
        if (info->opnd.reg != info->fpr) {
            emitpcode(PC_PS_MR, info->fpr, info->opnd.reg);
        }
    } else if (TYPE_FITS_IN_REGISTER(type) || TYPE_IS_SOFT_FLOAT(type)) {
        if (TYPE_IS_8BYTES(type) || (TYPE_IS_SOFT_FLOAT(type) && type->size != 4)) {
            if (!info->evaluated) {
                GEN_NODE_TO_REG(info->expr, info->gpr, info->gprHi, &info->opnd);
            }
            coerce_to_register_pair(&info->opnd, type, info->gpr, info->gprHi);
            if (copts.littleendian) {
                if (info->gprHi > 10) {
                    load_store_register(
                        PC_STW,
                        info->opnd.regHi,
                        1,
                        NULL,
                        high_offset + out_param_displ_to_offset(info->offset)
                    );
                }
            } else {
                if (info->gpr > 10) {
                    load_store_register(
                        PC_STW,
                        info->opnd.reg,
                        1,
                        NULL,
                        low_offset + out_param_displ_to_offset(info->offset)
                    );
                }
            }
        } else {
            if (!info->evaluated) {
                GEN_NODE_TO_REG(info->expr, info->gpr, 0, &info->opnd);
            }
            if (info->flags & AIF_ExtendTo32Bits) {
                extend32(&info->opnd, type, info->gpr);
            }
            ENSURE_GPR(&info->opnd, type, info->gpr);
            if (info->opnd.reg != info->gpr) {
                emitpcode(PC_MR, info->gpr, info->opnd.reg);
            }
        }
    } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
        if (!info->evaluated) {
            GEN_NODE(info->expr, &info->opnd);
        }

        if (type->size != 4 && info->opnd.optype == OpndType_IndirectGPR_ImmOffset) {
            load_store_register(
                PC_LWZ, info->gpr, info->opnd.reg, info->opnd.object, info->opnd.immOffset
            );
            load_store_register(
                PC_LWZ, info->gpr + 1, info->opnd.reg, info->opnd.object, info->opnd.immOffset + 4
            );
        } else {
            ENSURE_FPR(&info->opnd, type, 0);
            load_store_register(
                PC_STFD, info->opnd.reg, 1, NULL, out_param_displ_to_offset(info->offset)
            );
            load_store_register(
                PC_LWZ, info->gpr, 1, NULL, out_param_displ_to_offset(info->offset)
            );
            load_store_register(
                PC_LWZ, info->gpr + 1, 1, NULL, out_param_displ_to_offset(info->offset) + 4
            );
        }
    } else if (IS_TYPE_VECTOR(type)) {
        if (!info->evaluated) {
            GEN_NODE(info->expr, &info->opnd);
        }

        if (info->opnd.optype == OpndType_IndirectGPR_ImmOffset) {
            load_store_register(
                PC_LWZ, info->gpr, info->opnd.reg, info->opnd.object, info->opnd.immOffset
            );
            load_store_register(
                PC_LWZ, info->gpr + 1, info->opnd.reg, info->opnd.object, info->opnd.immOffset + 4
            );
            if ((info->gpr + 2) < 10) {
                load_store_register(
                    PC_LWZ,
                    info->gpr + 2,
                    info->opnd.reg,
                    info->opnd.object,
                    info->opnd.immOffset + 8
                );
                load_store_register(
                    PC_LWZ,
                    info->gpr + 3,
                    info->opnd.reg,
                    info->opnd.object,
                    info->opnd.immOffset + 12
                );
            }
        } else {
            ENSURE_VR(&info->opnd, type, 0);
            load_store_register(
                PC_STVX, info->opnd.reg, 1, NULL, out_param_displ_to_offset(info->offset)
            );
            load_store_register(
                PC_LWZ, info->gpr, 1, NULL, out_param_displ_to_offset(info->offset)
            );
            load_store_register(
                PC_LWZ, info->gpr + 1, 1, NULL, out_param_displ_to_offset(info->offset) + 4
            );
            if ((info->gpr + 2) < 10) {
                load_store_register(
                    PC_LWZ, info->gpr + 2, 1, NULL, out_param_displ_to_offset(info->offset) + 8
                );
                load_store_register(
                    PC_LWZ, info->gpr + 3, 1, NULL, out_param_displ_to_offset(info->offset) + 12
                );
            }
        }
    } else {
        if (!info->evaluated) {
            GEN_NODE(info->expr, &info->opnd);
        }

        if (type->size <= 4) {
            if (info->opnd.optype == OpndType_IndirectSymbol) {
                coerce_to_addressable(&info->opnd);
            }

            if (info->opnd.optype == OpndType_IndirectGPR_ImmOffset) {
                load_store_register(
                    PC_LWZ, info->gpr, info->opnd.reg, info->opnd.object, info->opnd.immOffset
                );
            } else if (info->opnd.optype == OpndType_IndirectGPR_Indexed) {
                emitpcode(PC_LWZX, info->gpr, info->opnd.reg, info->opnd.regOffset);
            }
        } else {
            SInt32 gprs_needed = (type->size >> 2) + ((type->size & 3) != 0);
            SInt32 i;

            make_addressable(&info->opnd, gprs_needed * 4, 12);
            for (i = 0; i < gprs_needed; i++) {
                if (info->opnd.reg != (info->gpr + i)) {
                    load_store_register(
                        PC_LWZ,
                        info->gpr + i,
                        info->opnd.reg,
                        info->opnd.object,
                        info->opnd.immOffset + i * 4
                    );
                }
            }

            if (info->opnd.reg >= info->gpr && info->opnd.reg < (info->gpr + gprs_needed)) {
                load_store_register(
                    PC_LWZ,
                    info->opnd.reg,
                    info->opnd.reg,
                    info->opnd.object,
                    info->opnd.immOffset + (info->opnd.reg - info->gpr) * 4
                );
            }
        }
    }
}

static void pass_in_register_and_memory(ArgInfo* info) {
    Type* type;
    int gpr;
    SInt32 offset;

    type = info->expr->rtype;
    gpr = info->gpr;
    offset = 0;
    while (offset < type->size && gpr <= 10) {
        load_store_register(PC_LWZ, gpr, 1, NULL, offset + out_param_displ_to_offset(info->offset));
        gpr++;
        offset += 4;
    }
}

static Boolean needs_TOC_reload(Object* func) {
    return FALSE;
}

static void load_virtual_function(TypeClass* tclass, SInt32 offset, int reg, Operand* opnd) {
    if (tclass->flags & CLASS_HANDLEOBJECT) {
        load_store_register(PC_LWZ, 12, reg, NULL, 0);
        load_store_register(PC_LWZ, 12, 12, NULL, tclass->vtable->offset);
    } else {
        load_store_register(PC_LWZ, 12, reg, NULL, tclass->vtable->offset);
    }
    load_store_register(PC_LWZ, 12, 12, NULL, offset);
    opnd->optype = OpndType_GPR;
    opnd->reg = 12;
}

static void branch_subroutine_indirect(Object* func, Operand* addrOpnd, UInt32* used_regs) {
    if ((int)addrOpnd->reg != 12) {
        emitpcode(PC_MR, 12, addrOpnd->reg);
    }

    used_regs[RegClass_GPR] |= 1 << 12;
    branch_subroutine(func, 1, used_regs);
}

static void evaluate_nested_function_calls(ArgInfo* info) {
    ArgInfo* scan;

    scan = info->next;
    while (scan && !scan->expr->hascall) {
        scan = scan->next;
    }

    if (scan) {
        evaluate_nested_function_calls(scan);
    }

    if (info->expr->hascall) {
        GEN_NODE(info->expr, &info->opnd);
        info->evaluated = TRUE;
    }
}

void call_function(ENode* expr, Operand* output) {
    ArgInfo* infos;
    ENode* funcref = expr->data.funccall.funcref;
    Type* resultType = expr->data.funccall.functype->functype;
    ENode* node = NULL;
    char has_varargs;
    ArgInfo* info;
    Operand opnd;
    TypeClass* tclass;
    SInt32 vfOffset;
    UInt32 used_regs[RegClassMax] = {0};
    Boolean has_floats;
    FuncArg* arg;

    memclrw(&opnd, sizeof(Operand));

    has_varargs = FALSE;
    for (arg = expr->data.funccall.functype->args; arg; arg = arg->next) {
        if (arg == &elipsis) {
            has_varargs = TRUE;
            break;
        }
    }

    if (expr->data.funccall.functype->flags & FUNC_PTM) {
        if (Type_RequiresMemoryReturn(resultType)) {
            node = expr->data.funccall.args->next->node;
        } else {
            node = expr->data.funccall.args->node;
        }
    }

    infos = analyze_arguments(
        node,
        expr->data.funccall.args,
        expr->data.funccall.functype->args,
        used_regs,
        &has_floats,
        has_varargs
    );

    if (infos) {
        evaluate_nested_function_calls(infos);
    }

    if (funcref->hascall) {
        GEN_NODE_TO_GPR(funcref, &opnd, TYPE(&void_ptr), 0);
    } else if (node && node->hascall) {
        GEN_NODE_TO_GPR(node, &opnd, TYPE(&void_ptr), 0);
    }

    for (info = infos; info; info = info->next) {
        if (info->flags & AIF_PassOnStack) {
            pass_in_memory(info);
        }
    }
    for (info = infos; info; info = info->next) {
        if ((info->flags & AIF_PassMask) == (AIF_PassInGPR | AIF_PassOnStack)) {
            pass_in_register_and_memory(info);
        }
    }
    for (info = infos; info; info = info->next) {
        int passKind = info->flags & AIF_PassMask;
        if (passKind == AIF_PassInGPR || passKind == AIF_PassInFPR || passKind == AIF_PassInVR) {
            pass_in_register(info);
        }
    }

    if (has_varargs) {
        if (has_floats && !copts.soft_float) {
            emitpcode(PC_CREQV, 1, 2, 1, 2, 1, 2);
        } else {
            emitpcode(PC_CRXOR, 1, 2, 1, 2, 1, 2);
        }
    }

    if (funcref->type == EOBJREF) {
        if (CParser_IsVirtualFunction(funcref->data.objref, &tclass, &vfOffset)) {
            load_virtual_function(
                tclass,
                vfOffset,
                Type_RequiresMemoryReturn(resultType) ? Register4 : Register3,
                &opnd
            );
            branch_subroutine_indirect_ctr(&opnd, used_regs);
        } else if (node) {
            if (!node->hascall) {
                GEN_NODE_TO_REG(node, 12, 0, &opnd);
                ENSURE_GPR(&opnd, TYPE(&void_ptr), 12);
            }
            branch_subroutine_indirect(funcref->data.objref, &opnd, used_regs);
        } else if (ObjGen_GetAddressing(funcref->data.objref) == AM_CODE_FAR_ABS) {
            if (!funcref->hascall) {
                GEN_NODE_TO_REG(funcref, 12, 0, &opnd);
            }
            ENSURE_GPR(&opnd, TYPE(&void_ptr), 12);
            branch_subroutine_indirect_ctr(&opnd, used_regs);
        } else {
            branch_subroutine(
                funcref->data.objref, needs_TOC_reload(funcref->data.objref), used_regs
            );
        }
    } else {
        if (!funcref->hascall) {
            GEN_NODE_TO_REG(funcref, 12, 0, &opnd);
        }
        ENSURE_GPR(&opnd, TYPE(&void_ptr), 12);
        branch_subroutine_indirect_ctr(&opnd, used_regs);
    }

    if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(resultType) && !TYPE_IS_SOFT_FLOAT(resultType)) {
        output->optype = OpndType_FPR;
        output->reg = used_virtual_registers[RegClass_FPR]++;
        emitpcode(PC_FMR, output->reg, 1);
    } else if (IS_TYPE_VECTOR(resultType)) {
        output->optype = OpndType_VR;
        output->reg = used_virtual_registers[RegClass_VR]++;
        emitpcode(PC_VMR, output->reg, 2);
    } else if (IS_TYPE_VEC2X32FLOAT(resultType) && copts.processor == CPU_PPCGEKKO) {
        output->optype = OpndType_FPR;
        output->reg = used_virtual_registers[RegClass_FPR]++;
        emitpcode(PC_PS_MR, output->reg, 1);
    } else if (((IS_TYPE_STRUCT(resultType) || IS_TYPE_CLASS(resultType)) &&
                !Type_RequiresMemoryReturn(resultType)) ||
               TYPE_FITS_IN_REGISTER(resultType) ||
               TYPE_IS_SOFT_FLOAT(resultType)) {
        if (resultType->size > 4) {
            output->optype = OpndType_GPRPair;
            output->reg = used_virtual_registers[RegClass_GPR]++;
            output->regHi = used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_MR, output->reg, low_reg);
            emitpcode(PC_MR, output->regHi, high_reg);
        } else {
            output->optype = OpndType_GPR;
            output->reg = used_virtual_registers[RegClass_GPR]++;
            emitpcode(PC_MR, output->reg, 3);
        }
    } else {
        output->optype = OpndType_Absolute;
        output->immediate = 0;
    }
}

static void branch_subroutine_indirect_ctr(Operand* addrOpnd, UInt32* used_regs) {
    if ((int)addrOpnd->reg != 12) {
        emitpcode(PC_MR, 12, addrOpnd->reg);
    }

    emitpcode(PC_MTCTR, 12);
    used_regs[RegClass_GPR] |= 1 << 12;
    branch_subroutine_ctr(used_regs);
}
