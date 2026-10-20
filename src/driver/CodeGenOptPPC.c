#include "driver/CodeGenOptPPC.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/InstrSelection.h"
#include "driver/CCompiler.h"
#include "driver/CWSecretPluginCallbacks.h"
#include "driver/pref_structs.h"

static short pref_versions;

void CodeGen_InitCompiler(void) {
    init_cgdispatch();
}

void CodeGen_TermCompiler(void) {}

void CodeGen_InitBackEndOptions(void) {
    PBackEnd pb;
    PProject pp;
    PLinker pl;
    Handle handle;

    CWSecretGetNamedPreferences(cparams.context, "PPC EABI CodeGen", &handle);
    pb = *((PBackEnd*)*handle);
    pref_versions = pb.version;

    CWSecretGetNamedPreferences(cparams.context, "PPC EABI Project", &handle);
    pp = *((PProject*)*handle);

    CWSecretGetNamedPreferences(cparams.context, "PPC EABI Linker", &handle);
    pl = *((PLinker*)*handle);

    cparamblkptr = &cparams;
    // (the original sets misaligned_mem_access and unsafe_global_reg_vars again below)
    copts.function_align = 16;
    copts.misaligned_mem_access = TRUE;
    copts.switch_tables = TRUE;
    copts.prepare_compress = FALSE;
    copts.min_struct_alignment = 4;
#if VERSION >= VERSION_GC_2_5
    // (2.0 has no old_style_bitfields option)
    copts.old_style_bitfields = FALSE;
#endif
    copts.unsafe_global_reg_vars = FALSE;
    copts.ignore_global_reg_vars = FALSE;
    copts.altivec_model = 0;
    copts.altivec_vrsave = TRUE;
    copts.cats = TRUE;
    copts.littleendian = !pp.endian;
    copts.readonly_strings = pb.readonlystrings;
    copts.misaligned_mem_access = FALSE;
    copts.unsafe_global_reg_vars = TRUE;
    copts.schedule_factor = 2;

    // Each older panel version numbers the processors its own way. The cases fall through, but each
    // switch reads pb.processor again, so the oldest mapping that knows the value wins (the
    // original's bug: a version 6 panel's 555, 4, comes out as a 601).
    switch (pb.version) {
        case 6:
        case 7:
        case 8:
        case 9:
            switch (pb.processor) {
                case 0:
                    copts.processor = CPU_PPC401;
                    break;
                case 1:
                    copts.processor = CPU_PPC403;
                    break;
                case 2:
                    copts.processor = CPU_PPC505;
                    break;
                case 3:
                    copts.processor = CPU_PPC509;
                    break;
                case 4:
                    copts.processor = CPU_PPC555;
                    break;
                case 5:
                    copts.processor = CPU_PPC601;
                    break;
                case 6:
                    copts.processor = CPU_PPC602;
                    break;
                case 7:
                    copts.processor = CPU_PPC603;
                    break;
                case 8:
                    copts.processor = CPU_PPC603e;
                    break;
                case 9:
                    copts.processor = CPU_PPC604;
                    break;
                case 10:
                    copts.processor = CPU_PPC604e;
                    break;
                case 11:
                    copts.processor = CPU_PPC740;
                    break;
                case 12:
                    copts.processor = CPU_PPC750;
                    break;
                case 13:
                    copts.processor = CPU_PPC801;
                    break;
                case 14:
                    copts.processor = CPU_PPC821;
                    break;
                case 15:
                    copts.processor = CPU_PPC823;
                    break;
                case 16:
                    copts.processor = CPU_PPC850;
                    break;
                case 17:
                    copts.processor = CPU_PPC860;
                    break;
                case 18:
                    copts.processor = CPU_PPC8260;
                    break;
                case 19:
                default:
                    copts.processor = CPU_Generic;
                    break;
            }
            // fall through
        case 5:
            switch (pb.processor) {
                case 0:
                    copts.processor = CPU_PPC401;
                    break;
                case 1:
                    copts.processor = CPU_PPC403;
                    break;
                case 2:
                    copts.processor = CPU_PPC505;
                    break;
                case 3:
                    copts.processor = CPU_PPC509;
                    break;
                case 4:
                    copts.processor = CPU_PPC601;
                    break;
                case 5:
                    copts.processor = CPU_PPC602;
                    break;
                case 6:
                    copts.processor = CPU_PPC603;
                    break;
                case 7:
                    copts.processor = CPU_PPC603e;
                    break;
                case 8:
                    copts.processor = CPU_PPC604;
                    break;
                case 9:
                    copts.processor = CPU_PPC604e;
                    break;
                case 10:
                    copts.processor = CPU_PPC740;
                    break;
                case 11:
                    copts.processor = CPU_PPC750;
                    break;
                case 12:
                    copts.processor = CPU_PPC801;
                    break;
                case 13:
                    copts.processor = CPU_PPC821;
                    break;
                case 14:
                    copts.processor = CPU_PPC823;
                    break;
                case 15:
                    copts.processor = CPU_PPC850;
                    break;
                case 16:
                    copts.processor = CPU_PPC860;
                    break;
                case 17:
                default:
                    copts.processor = CPU_Generic;
                    break;
            }
            // fall through
        case 4:
            switch (pb.processor) {
                case 0:
                    copts.processor = CPU_PPC401;
                    break;
                case 1:
                    copts.processor = CPU_PPC403;
                    break;
                case 2:
                    copts.processor = CPU_PPC505;
                    break;
                case 3:
                    copts.processor = CPU_PPC509;
                    break;
                case 4:
                    copts.processor = CPU_PPC601;
                    break;
                case 5:
                    copts.processor = CPU_PPC602;
                    break;
                case 6:
                    copts.processor = CPU_PPC603;
                    break;
                case 7:
                    copts.processor = CPU_PPC603e;
                    break;
                case 8:
                    copts.processor = CPU_PPC604;
                    break;
                case 9:
                    copts.processor = CPU_PPC604e;
                    break;
                case 10:
                    copts.processor = CPU_PPC740;
                    break;
                case 11:
                    copts.processor = CPU_PPC750;
                    break;
                case 12:
                    copts.processor = CPU_PPC801;
                    break;
                case 13:
                    copts.processor = CPU_PPC821;
                    break;
                case 14:
                    copts.processor = CPU_PPC823;
                    break;
                case 15:
                    copts.processor = CPU_PPC860;
                    break;
                case 16:
                default:
                    copts.processor = CPU_PPC850;
                    break;
            }
            // fall through
        case 3:
            switch (pb.processor) {
                case 0:
                    copts.processor = CPU_PPC401;
                    break;
                case 1:
                    copts.processor = CPU_PPC403;
                    break;
                case 2:
                    copts.processor = CPU_PPC505;
                    break;
                case 3:
                    copts.processor = CPU_PPC509;
                    break;
                case 4:
                    copts.processor = CPU_PPC601;
                    break;
                case 5:
                    copts.processor = CPU_PPC602;
                    break;
                case 6:
                    copts.processor = CPU_PPC603;
                    break;
                case 7:
                    copts.processor = CPU_PPC604;
                    break;
                case 8:
                    copts.processor = CPU_PPC740;
                    break;
                case 9:
                    copts.processor = CPU_PPC750;
                    break;
                case 10:
                    copts.processor = CPU_PPC801;
                    break;
                case 11:
                    copts.processor = CPU_PPC821;
                    break;
                case 12:
                    copts.processor = CPU_PPC823;
                    break;
                case 13:
                    copts.processor = CPU_PPC860;
                    break;
                case 14:
                default:
                    copts.processor = CPU_Generic;
                    break;
            }
            // fall through
        case 1:
        case 2:
            switch (pb.processor) {
                case 0:
                    copts.processor = CPU_PPC401;
                    break;
                case 1:
                    copts.processor = CPU_PPC403;
                    break;
                case 2:
                    copts.processor = CPU_PPC505;
                    break;
                case 3:
                    copts.processor = CPU_PPC509;
                    break;
                case 4:
                    copts.processor = CPU_PPC601;
                    break;
                case 5:
                    copts.processor = CPU_PPC602;
                    break;
                case 6:
                    copts.processor = CPU_PPC603;
                    break;
                case 7:
                    copts.processor = CPU_PPC604;
                    break;
                case 8:
                    copts.processor = CPU_PPC740;
                    break;
                case 9:
                    copts.processor = CPU_PPC750;
                    break;
                case 10:
                    copts.processor = CPU_PPC801;
                    break;
                case 11:
                    copts.processor = CPU_PPC821;
                    break;
                case 12:
                    copts.processor = CPU_PPC860;
                    break;
                case 13:
                    copts.processor = CPU_Generic;
                    break;
            }
            break;
        default:
            copts.processor = pb.processor;
    }

    switch (copts.processor) {
        case CPU_PPC601:
            copts.scheduling = 1;
            break;
        case CPU_PPC603:
            copts.scheduling = 2;
            break;
        case CPU_PPC603e:
        case CPU_PPC8240:
        case CPU_PPC8260:
            copts.scheduling = 5;
            break;
        case CPU_PPC604:
            copts.scheduling = 3;
            break;
        case CPU_PPC604e:
            copts.scheduling = 6;
            break;
        case CPU_PPC740:
        case CPU_PPC750:
        case CPU_PPCGEKKO:
            copts.scheduling = 4;
            break;
        case CPU_PPC7400:
            copts.scheduling = 7;
            break;
        case CPU_PPC7450:
            copts.scheduling = 10;
            break;
        case CPU_PPC602:
            copts.scheduling = 8;
            break;
        default:
            copts.scheduling = 8;
            break;
    }

    if (pb.schedule) {
        copts.schedule_factor = 2;
    } else {
        copts.schedule_factor = 0;
    }

    copts.gen_isel = FALSE;
    copts.gen_fsel = FALSE;
    copts.peephole = pb.peephole;
    copts.structalignment = pb.structalignment;
    copts.function_align = 4 << pb.funcalign;
    copts.pool_data = pb.pooldata;
    copts.sdata2_threshold = pp.sdata2threshold;
    copts.sdata_threshold = pp.sdatathreshold;
    copts.no_comment_records = pp.no_comment_records;
    copts.dwarf_relocations = TRUE;
    copts.dwarf_siblings = TRUE;
    copts.common = pb.common;
    copts.absolutepath = pl.symfullpath;
    copts.use_lmw_stmw = pb.use_lmw_stmw;
    copts.rel109_offset = 0;
    copts.use_fp = FALSE;
    copts.soft_float = FALSE;
    copts.fp_contract = FALSE;
    if (pb.floatingpoint) {
        copts.use_fp = TRUE;
        if (pb.floatingpoint == 1) {
            copts.soft_float = TRUE;
        } else {
            copts.fp_contract = pb.fp_contract;
        }
    }
    if (copts.processor == CPU_PPCGEKKO) {
        copts.ibm_stackframe = TRUE;
        copts.gecko_float_typecons = FALSE;
        copts.disable_vector_frame = FALSE;
    } else {
        copts.ibm_stackframe = FALSE;
    }
    copts.altivec_model = pb.altivec;
    if (pb.vrsave) {
        copts.altivec_vrsave = TRUE;
    } else {
        copts.altivec_vrsave = FALSE;
    }
    copts.debuglisting = FALSE;
    copts.unroll_speculative = TRUE;
    copts.unroll_instr_limit = 70;
    copts.unroll_factor_limit = 10;
    copts.opt_bcc_lr_ctr = TRUE;
    copts.optimizewithasm = TRUE;
    copts.strengthreductionstrict = TRUE;
    copts.bc_range = 0x10000;
    copts.gprfloatcopy = FALSE;
}

void Test_Version_Numbers(void) {}
