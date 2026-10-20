#include "compiler/FuncLevelAsmPPC.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CMangler.h"
#include "compiler/CodeGen.h"
#include "compiler/Coloring.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/DumpIR.h"
#include "compiler/InlineAsm.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/InlineAsmRegisters.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/PCode.h"
#include "compiler/PCodeAssembly.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeListing.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/StackFrame.h"
#include "compiler/Switch.h"
#include "compiler/TOC.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "driver/CCompiler.h"

static EntryPoint* entrypoints_head;
static EntryPoint** entrypoints_tail;

void setup_assembly_argument(Object* obj, short reg) {
    VarInfo* vi;
    Type* type;

    vi = Registers_GetVarInfo(obj);
    type = obj->type;
    vi->used = TRUE;

    if (!requires_frame) {
        if (is_register_object(obj)) {
            if (!reg) {
                CError_Error(ERR_COULD_NOT_ASSIGNED_REGISTER, obj->name->name);
            }

            if (TYPE_IS_SOFT_FLOAT(type)) {
                // -fp soft keeps a float in a GPR, a double in a pair
                if (type->size == 4) {
                    retain_register(obj, RegClass_GPR, reg);
                } else {
                    short regLo;
                    short regHi;
                    if (copts.littleendian) {
                        regLo = reg;
                        regHi = reg + 1;
                    } else {
                        regLo = reg + 1;
                        regHi = reg;
                    }
                    retain_GPR_pair(obj, regLo, regHi);
                }
                InlineAsm_InsertRegister(obj->name->name, RegClass_GPR, vi->reg, obj);
            } else if (TYPE_IS_8BYTES(type)) {
                short regLo;
                short regHi;
                if ((int)reg < 10) {
                    if (copts.littleendian) {
                        regLo = reg;
                        regHi = reg + 1;
                    } else {
                        regLo = reg + 1;
                        regHi = reg;
                    }
                    retain_GPR_pair(obj, regLo, regHi);
                    InlineAsm_InsertRegister(obj->name->name, RegClass_GPR, regLo, obj);
                }
            } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
                retain_register(obj, RegClass_FPR, reg);
                InlineAsm_InsertRegister(obj->name->name, RegClass_FPR, reg, obj);
            } else if (IS_TYPE_VECTOR(type)) {
                retain_register(obj, RegClass_VR, reg);
                InlineAsm_InsertRegister(obj->name->name, RegClass_VR, reg, obj);
            } else {
                retain_register(obj, RegClass_GPR, reg);
                InlineAsm_InsertRegister(obj->name->name, RegClass_GPR, reg, obj);
            }
        }
    } else {
        if (is_register_object(obj)) {
            vi = Registers_GetVarInfo(obj);
            if (!vi->reg) {
                assign_register_by_type(obj);
                if (!(vi->flags & VarInfoFlag_InReg)) {
                    CError_Error(ERR_COULD_NOT_ASSIGNED_REGISTER, obj->name->name);
                } else {
                    InlineAsm_InsertRegister(obj->name->name, vi->rclass, vi->reg, obj);
                }
            }
        }
    }
}

void assign_local_addresses(void) {
    VarInfo* vi;
    ObjectList* list;
    Object* object;

    for (list = locals; list; list = list->next) {
        vi = CodeGen_GetNewVarInfo();
        list->object->u.var.info = vi;
        list->object->flags |= OBJECT_USED;
        vi->used = TRUE;
    }

    for (list = locals; list; list = list->next) {
        object = list->object;
        if (is_register_object(object)) {
            vi = Registers_GetVarInfo(object);
            if (!vi->reg) {
                assign_register_by_type(object);
                if (!(vi->flags & VarInfoFlag_InReg)) {
                    CError_Error(ERR_COULD_NOT_ASSIGNED_REGISTER, object->name->name);
                } else {
                    InlineAsm_InsertRegister(object->name->name, vi->rclass, vi->reg, object);
                }
            }
        }
    }

    for (list = locals; list; list = list->next) {
        object = list->object;
        if (OBJECT_REG(object) == 0) {
            assign_local_memory(object);
        }
    }
}

static void FuncAsm_PreScanDirectives(void) {
    SInt32 directive;
    Boolean save_eoltokens;

    in_assembler = TRUE;
    save_eoltokens = cprep_eoltokens;
    cprep_eoltokens = TRUE;

    if (setjmp(InlineAsm_assemblererror) == 0) {
        InlineAsm_setjmp_valid = TRUE;
        while (tk == TK_IDENTIFIER && (directive = InlineAsm_IsDirective(AssemblerType_Function))) {
            InlineAsm_ProcessDirective(directive);

            if (tk == ';' || tk == TK_EOL) {
                CPrep_TokenStreamFlush();
                tk = lex();
            } else {
                InlineAsm_SyntaxError(ERR_END_LINE_EXPECTED);
            }

            if (directive == IADirective_FrAlloc) {
                requires_frame = TRUE;
                break;
            } else if (directive == IADirective_NoFrAlloc) {
                user_responsible_for_frame = TRUE;
                break;
            }
        }
    }

    in_assembler = FALSE;
    cprep_eoltokens = save_eoltokens;
}

static void FuncAsm_AddEntryPoint(Statement* stmt, PCodeBlock* block) {
    EntryPoint* ep;
    IAEntryPoint* ia_ep;

    ia_ep = (IAEntryPoint*)stmt->expr;
    ep = lalloc(sizeof(EntryPoint));
    memclrw(ep, sizeof(EntryPoint));

    ep->object = ia_ep->object;
    ep->block = block;

    *entrypoints_tail = ep;
    entrypoints_tail = &ep->next;

    block->flags |= fEntryPoint;
}

// NOTE: invented names: frame_generated, seen_frfree
void Assembler(Object* func) {
    PCodeBlock* block;
    Statement* stmt;
    Statement* scan;
    // the prologue and epilogue were made at the frfree
    Boolean frame_generated;
    Boolean seen_frfree;
    char* name;
    InlineAsm* ia;
    Boolean save_unusedvar;
    Boolean save_unusedarg;
    Boolean save_peephole;

    frame_generated = FALSE;
    seen_frfree = FALSE;
    InlineAsm_setjmp_valid = FALSE;

    init_endian();
    init_stack_globals(func);
    memclrw(asm_alloc_flags, sizeof(asm_alloc_flags));
    fralloc_parameter_area_size = 0;
    user_responsible_for_frame = FALSE;
    assembledinstructions = 0;

    entrypoints_head = NULL;
    switchtables = NULL;
    entrypoints_tail = &entrypoints_head;

    stmt = curstmt;

    if (func && func->name) {
        PrintProgressFunction(func->name->name);
    }

    CodeGen_InitialSanityCheck();

    if (func->qual & Q_INLINE) {
        PPCError_Warning(PPCERR_ASM_FUNCTION_NOT_INLINABLE);
    }

    CheckCLabels();

    if (fatalerrors) {
        return;
    }

    if (copts.filesyminfo) {
        CPrep_SetSourceFile(&cparser_fileoffset);
    }

    sm_section = update_section(func);

    initpcode();

    pclabel(prologue = makepcblock(), makepclabel());
    pclabel(block = makepcblock(), makepclabel());
    pcbranch(prologue, block->labels);

    reset_anchor_varinfo();
    InlineAsm_InitializePPC();
    FuncAsm_PreScanDirectives();

    disable_optimizer = 1;

    init_registers();
    assign_arguments_to_memory(func, 0, FALSE);
    init_frame_sizes(FALSE);

    if (copts.debuglisting) {
        DumpIR(stmt, func);
    }

    cprep_eoltokens = TRUE;
    in_assembler = TRUE;

    save_unusedvar = copts.warn_unusedvar;
    save_unusedarg = copts.warn_unusedarg;
    copts.warn_unusedvar = FALSE;
    copts.warn_unusedarg = FALSE;

    InlineAsm_ScanFunction('}');

    expandTOCreferences(&stmt->next);

    if (!anyerrors && copts.debuglisting) {
        DumpIR(stmt, func);
    }

    in_assembler = FALSE;
    cprep_eoltokens = FALSE;

    name = CMangler_GetLinkName(func)->name;
    func->flags |= OBJECT_DEFINED;

    if (fralloc_parameter_area_size) {
        update_out_param_size(fralloc_parameter_area_size);
    }
    if (!user_responsible_for_frame) {
        process_arguments(move_assigned_argument, FALSE);
    }

    branch_label(makepclabel());
    assign_labels(stmt->next);

    copts.warn_unusedvar = save_unusedvar;
    copts.warn_unusedarg = save_unusedarg;

    for (scan = stmt->next; scan; scan = scan->next) {
        current_statement = scan;
        switch (scan->type) {
            case ST_ASM:
                if ((ia = (InlineAsm*)scan->expr)) {
                    if (ia->flags & IAFlag_Directive) {
                        if (ia->opcode == IADirective_Entry) {
                            branch_label(makepclabel());
                            FuncAsm_AddEntryPoint(scan, pclastblock);
                        } else if (ia->opcode == IADirective_FrFree) {
                            if (seen_frfree) {
                                PPCError_Error(PPCERR_FRFREE_NOT_AT_END);
                            } else {
                                seen_frfree = TRUE;
                            }

                            asm_alloc_flags[3] = TRUE;
                            asm_alloc_flags[4] = TRUE;
                            branch_label(makepclabel());

                            (epilogue = pclastblock)->flags |= fIsEpilogue;

                            CheckCLabels();
                            if (fatalerrors) {
                                return;
                            }

                            pccomputepredecessors();
                            if (copts.debuglisting) {
                                pclistblocks(name, "[FUNCTION-LEVEL ASM] INITIAL CODE");
                            }
                            StackFrame_has_vectors = set_vr_reg_flags();
                            colorinstructions(func);
                            if (copts.debuglisting) {
                                pclistblocks(name, "[FUNCTION-LEVEL ASM] AFTER REGISTER COLORING");
                            }
                            compute_frame_sizes();
                            generate_prologue(prologue, FALSE);
                            generate_epilogue(epilogue = pclastblock, FALSE);
                            if (copts.debuglisting) {
                                pclistblocks(
                                    name, "[FUNCTION-LEVEL ASM] AFTER PROLOGUE/EPILOGUE CREATION"
                                );
                            }

                            frame_generated = TRUE;
                        }
                    } else {
                        branch_label(makepclabel());
                        asm_alloc_flags[6] = FALSE;
                        asm_alloc_flags[7] = FALSE;
                        InlineAsm_TranslateIRtoPCode(scan);
                        asm_alloc_flags[4] = FALSE;
                    }
                }
                break;
            case ST_LABEL:
                if (!scan->label->pclabel->resolved) {
                    branch_label(scan->label->pclabel);
                }
                break;
            default:
                CError_FATAL(530);
        }
    }

    current_statement = NULL;

    if (fatalerrors) {
        return;
    }
    CheckCLabels();
    if (fatalerrors) {
        return;
    }

    if (!frame_generated) {
        branch_label(makepclabel());

        (epilogue = pclastblock)->flags |= fIsEpilogue;

        pccomputepredecessors();
        StackFrame_has_vectors = set_vr_reg_flags();
        if (copts.debuglisting) {
            pclistblocks(name, "[FUNCTION-LEVEL ASM] INITIAL CODE");
        }

        if (!asm_alloc_flags[1]) {
            colorinstructions(func);
            if (fatalerrors) {
                return;
            }

            if (copts.debuglisting) {
                pclistblocks(name, "[FUNCTION-LEVEL ASM] AFTER REGISTER COLORING");
            }
        }

        compute_frame_sizes();
        if (asm_alloc_flags[1]) {
            no_frame_for_asm();
        }

        if (fatalerrors) {
            return;
        }

        if (!asm_alloc_flags[1]) {
            generate_prologue(prologue, FALSE);
            generate_epilogue(epilogue, !(asm_alloc_flags[6] || asm_alloc_flags[7]));
        }

        if (copts.debuglisting) {
            pclistblocks(name, "[FUNCTION-LEVEL ASM] AFTER PROLOGUE/EPILOGUE CREATION");
        }
    }

    if (fatalerrors) {
        return;
    }

    if (!asm_alloc_flags[1] && needs_frame()) {
        if (asm_alloc_flags[3]) {
            if (interrupt_routine_flags) {
                // an interrupt function returns with rfi; one the user did not write goes into
                // the epilogue
                if (!asm_alloc_flags[7]) {
                    PCodeBlock* save = pclastblock;
                    pclastblock = epilogue;
                    emitpcode(PC_RFI);
                    setpcodeflags(fIsVolatile);
                    pclastblock = save;
                    if (asm_alloc_flags[5]) {
                        PPCError_Warning(PPCERR_EXIT_AFTER_FRFREE, "rfi");
                    }
                } else if (!asm_alloc_flags[5]) {
                    PPCError_Warning(PPCERR_EXIT_AFTER_FRFREE, "rfi");
                }
            } else if (!asm_alloc_flags[5] || !asm_alloc_flags[6]) {
                PPCError_Warning(PPCERR_EXIT_AFTER_FRFREE, "blr");
            }
            if (asm_alloc_flags[8]) {
                PPCError_Warning(PPCERR_FRFREE_ONE_EXIT);
            }
        } else {
            PPCError_Warning(PPCERR_FRFREE_BEFORE_EXIT, interrupt_routine_flags ? "rfi" : "blr");
        }
    }

    func->section = sm_section;

    if (copts.filesyminfo) {
        symdeclend = CPrep_GetFileOffsetInfo(&cparser_fileoffset);
    }

    save_peephole = copts.peephole;
    copts.peephole = FALSE;
    assemblefunction(func, entrypoints_head);
    copts.peephole = save_peephole;

    if (copts.debuglisting) {
        pclistblocks(CMangler_GetLinkName(func)->name, "[FUNCTION-LEVEL ASM] FINAL CODE");
    }

    CFunc_WarnUnused();
    InlineAsm_setjmp_valid = FALSE;
}

void SetupAssembler(void) {
    InlineAsm_setjmp_valid = FALSE;
}

void CleanupAssembler(void) {}
