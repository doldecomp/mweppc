#include "compiler/InlineAsmRegistersPPC.h"
#include "compiler/CFunc.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/InlineAsm.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/InlineAsmRegisters.h"
#include "compiler/objects.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/types.h"
#include <stdio.h>
#include <string.h>

#pragma pool_strings on

#pragma pack(push, 2)
typedef struct IARegInfo {
    struct IARegInfo* next;
    UInt32 cpus;
    IARegister reg;
} IARegInfo;

typedef struct AsmRegister {
    char* name;
    RegClass rclass;
    SInt32 num;
} AsmRegister;

typedef struct AsmSpr {
    char* name;
    SInt32 num;
    UInt32 mask;
} AsmSpr;

typedef struct AsmDcr {
    SInt32 num;
    char* name;
    UInt32 mask;
} AsmDcr;
#pragma pack(pop)

static IARegInfo* hashedsprs[64];
static IARegInfo* hasheddcrs[64];
static IARegister unusedregisters[2];

// clang-format off
static AsmRegister asm_registers[] = {
    "r0", RegClass_GPR, 0,
    "r1", RegClass_GPR, 1,
    "r2", RegClass_GPR, 2,
    "r3", RegClass_GPR, 3,
    "r4", RegClass_GPR, 4,
    "r5", RegClass_GPR, 5,
    "r6", RegClass_GPR, 6,
    "r7", RegClass_GPR, 7,
    "r8", RegClass_GPR, 8,
    "r9", RegClass_GPR, 9,
    "r10", RegClass_GPR, 10,
    "r11", RegClass_GPR, 11,
    "r12", RegClass_GPR, 12,
    "r13", RegClass_GPR, 13,
    "r14", RegClass_GPR, 14,
    "r15", RegClass_GPR, 15,
    "r16", RegClass_GPR, 16,
    "r17", RegClass_GPR, 17,
    "r18", RegClass_GPR, 18,
    "r19", RegClass_GPR, 19,
    "r20", RegClass_GPR, 20,
    "r21", RegClass_GPR, 21,
    "r22", RegClass_GPR, 22,
    "r23", RegClass_GPR, 23,
    "r24", RegClass_GPR, 24,
    "r25", RegClass_GPR, 25,
    "r26", RegClass_GPR, 26,
    "r27", RegClass_GPR, 27,
    "r28", RegClass_GPR, 28,
    "r29", RegClass_GPR, 29,
    "r30", RegClass_GPR, 30,
    "r31", RegClass_GPR, 31,
    "gpr0", RegClass_GPR, 0,
    "gpr1", RegClass_GPR, 1,
    "gpr2", RegClass_GPR, 2,
    "gpr3", RegClass_GPR, 3,
    "gpr4", RegClass_GPR, 4,
    "gpr5", RegClass_GPR, 5,
    "gpr6", RegClass_GPR, 6,
    "gpr7", RegClass_GPR, 7,
    "gpr8", RegClass_GPR, 8,
    "gpr9", RegClass_GPR, 9,
    "gpr10", RegClass_GPR, 10,
    "gpr11", RegClass_GPR, 11,
    "gpr12", RegClass_GPR, 12,
    "gpr13", RegClass_GPR, 13,
    "gpr14", RegClass_GPR, 14,
    "gpr15", RegClass_GPR, 15,
    "gpr16", RegClass_GPR, 16,
    "gpr17", RegClass_GPR, 17,
    "gpr18", RegClass_GPR, 18,
    "gpr19", RegClass_GPR, 19,
    "gpr20", RegClass_GPR, 20,
    "gpr21", RegClass_GPR, 21,
    "gpr22", RegClass_GPR, 22,
    "gpr23", RegClass_GPR, 23,
    "gpr24", RegClass_GPR, 24,
    "gpr25", RegClass_GPR, 25,
    "gpr26", RegClass_GPR, 26,
    "gpr27", RegClass_GPR, 27,
    "gpr28", RegClass_GPR, 28,
    "gpr29", RegClass_GPR, 29,
    "gpr30", RegClass_GPR, 30,
    "gpr31", RegClass_GPR, 31,
    "rtoc", RegClass_GPR, 2,
    "RTOC", RegClass_GPR, 2,
    "sp", RegClass_GPR, 1,
    "SP", RegClass_GPR, 1,
    "rsp", RegClass_GPR, 1,
    "RSP", RegClass_GPR, 1,
    "f0", RegClass_FPR, 0,
    "f1", RegClass_FPR, 1,
    "f2", RegClass_FPR, 2,
    "f3", RegClass_FPR, 3,
    "f4", RegClass_FPR, 4,
    "f5", RegClass_FPR, 5,
    "f6", RegClass_FPR, 6,
    "f7", RegClass_FPR, 7,
    "f8", RegClass_FPR, 8,
    "f9", RegClass_FPR, 9,
    "f10", RegClass_FPR, 10,
    "f11", RegClass_FPR, 11,
    "f12", RegClass_FPR, 12,
    "f13", RegClass_FPR, 13,
    "f14", RegClass_FPR, 14,
    "f15", RegClass_FPR, 15,
    "f16", RegClass_FPR, 16,
    "f17", RegClass_FPR, 17,
    "f18", RegClass_FPR, 18,
    "f19", RegClass_FPR, 19,
    "f20", RegClass_FPR, 20,
    "f21", RegClass_FPR, 21,
    "f22", RegClass_FPR, 22,
    "f23", RegClass_FPR, 23,
    "f24", RegClass_FPR, 24,
    "f25", RegClass_FPR, 25,
    "f26", RegClass_FPR, 26,
    "f27", RegClass_FPR, 27,
    "f28", RegClass_FPR, 28,
    "f29", RegClass_FPR, 29,
    "f30", RegClass_FPR, 30,
    "f31", RegClass_FPR, 31,
    "fp0", RegClass_FPR, 0,
    "fp1", RegClass_FPR, 1,
    "fp2", RegClass_FPR, 2,
    "fp3", RegClass_FPR, 3,
    "fp4", RegClass_FPR, 4,
    "fp5", RegClass_FPR, 5,
    "fp6", RegClass_FPR, 6,
    "fp7", RegClass_FPR, 7,
    "fp8", RegClass_FPR, 8,
    "fp9", RegClass_FPR, 9,
    "fp10", RegClass_FPR, 10,
    "fp11", RegClass_FPR, 11,
    "fp12", RegClass_FPR, 12,
    "fp13", RegClass_FPR, 13,
    "fp14", RegClass_FPR, 14,
    "fp15", RegClass_FPR, 15,
    "fp16", RegClass_FPR, 16,
    "fp17", RegClass_FPR, 17,
    "fp18", RegClass_FPR, 18,
    "fp19", RegClass_FPR, 19,
    "fp20", RegClass_FPR, 20,
    "fp21", RegClass_FPR, 21,
    "fp22", RegClass_FPR, 22,
    "fp23", RegClass_FPR, 23,
    "fp24", RegClass_FPR, 24,
    "fp25", RegClass_FPR, 25,
    "fp26", RegClass_FPR, 26,
    "fp27", RegClass_FPR, 27,
    "fp28", RegClass_FPR, 28,
    "fp29", RegClass_FPR, 29,
    "fp30", RegClass_FPR, 30,
    "fp31", RegClass_FPR, 31,
    "v0", RegClass_VR, 0,
    "v1", RegClass_VR, 1,
    "v2", RegClass_VR, 2,
    "v3", RegClass_VR, 3,
    "v4", RegClass_VR, 4,
    "v5", RegClass_VR, 5,
    "v6", RegClass_VR, 6,
    "v7", RegClass_VR, 7,
    "v8", RegClass_VR, 8,
    "v9", RegClass_VR, 9,
    "v10", RegClass_VR, 10,
    "v11", RegClass_VR, 11,
    "v12", RegClass_VR, 12,
    "v13", RegClass_VR, 13,
    "v14", RegClass_VR, 14,
    "v15", RegClass_VR, 15,
    "v16", RegClass_VR, 16,
    "v17", RegClass_VR, 17,
    "v18", RegClass_VR, 18,
    "v19", RegClass_VR, 19,
    "v20", RegClass_VR, 20,
    "v21", RegClass_VR, 21,
    "v22", RegClass_VR, 22,
    "v23", RegClass_VR, 23,
    "v24", RegClass_VR, 24,
    "v25", RegClass_VR, 25,
    "v26", RegClass_VR, 26,
    "v27", RegClass_VR, 27,
    "v28", RegClass_VR, 28,
    "v29", RegClass_VR, 29,
    "v30", RegClass_VR, 30,
    "v31", RegClass_VR, 31,
    "vr0", RegClass_VR, 0,
    "vr1", RegClass_VR, 1,
    "vr2", RegClass_VR, 2,
    "vr3", RegClass_VR, 3,
    "vr4", RegClass_VR, 4,
    "vr5", RegClass_VR, 5,
    "vr6", RegClass_VR, 6,
    "vr7", RegClass_VR, 7,
    "vr8", RegClass_VR, 8,
    "vr9", RegClass_VR, 9,
    "vr10", RegClass_VR, 10,
    "vr11", RegClass_VR, 11,
    "vr12", RegClass_VR, 12,
    "vr13", RegClass_VR, 13,
    "vr14", RegClass_VR, 14,
    "vr15", RegClass_VR, 15,
    "vr16", RegClass_VR, 16,
    "vr17", RegClass_VR, 17,
    "vr18", RegClass_VR, 18,
    "vr19", RegClass_VR, 19,
    "vr20", RegClass_VR, 20,
    "vr21", RegClass_VR, 21,
    "vr22", RegClass_VR, 22,
    "vr23", RegClass_VR, 23,
    "vr24", RegClass_VR, 24,
    "vr25", RegClass_VR, 25,
    "vr26", RegClass_VR, 26,
    "vr27", RegClass_VR, 27,
    "vr28", RegClass_VR, 28,
    "vr29", RegClass_VR, 29,
    "vr30", RegClass_VR, 30,
    "vr31", RegClass_VR, 31,
    "cr0", RegClass_CRFIELD, 0,
    "cr1", RegClass_CRFIELD, 1,
    "cr2", RegClass_CRFIELD, 2,
    "cr3", RegClass_CRFIELD, 3,
    "cr4", RegClass_CRFIELD, 4,
    "cr5", RegClass_CRFIELD, 5,
    "cr6", RegClass_CRFIELD, 6,
    "cr7", RegClass_CRFIELD, 7,
    "crf0", RegClass_CRFIELD, 0,
    "crf1", RegClass_CRFIELD, 1,
    "crf2", RegClass_CRFIELD, 2,
    "crf3", RegClass_CRFIELD, 3,
    "crf4", RegClass_CRFIELD, 4,
    "crf5", RegClass_CRFIELD, 5,
    "crf6", RegClass_CRFIELD, 6,
    "crf7", RegClass_CRFIELD, 7,
    "lt", RegClass_CRBIT, 0,
    "gt", RegClass_CRBIT, 1,
    "eq", RegClass_CRBIT, 2,
    "so", RegClass_CRBIT, 3,
    "un", RegClass_CRBIT, 3,
    "LT", RegClass_CRBIT, 0,
    "GT", RegClass_CRBIT, 1,
    "EQ", RegClass_CRBIT, 2,
    "SO", RegClass_CRBIT, 3,
    "UN", RegClass_CRBIT, 3,
    NULL, 0, 0
};
// clang-format on

// clang-format off
static AsmSpr asm_sprs[] = {
    "xer", 0x1, 0xFFFFF,
    "lr", 0x8, 0xFFFFF,
    "ctr", 0x9, 0xFFFFF,
    "mq", 0x0, 0x1,
    "rtcu", 0x4, 0x1,
    "rtcl", 0x5, 0x1,
    "dsisr", 0x12, 0xFF83F,
    "dar", 0x13, 0xFF83F,
    "dec", 0x16, 0xFF83F,
    "sdr1", 0x19, 0xFE00F,
    "srr0", 0x1A, 0xFFFFF,
    "srr1", 0x1B, 0xFFFFF,
    "eie", 0x50, 0x1830,
    "eid", 0x51, 0x1830,
    "nri", 0x52, 0x1830,
    "cmpa", 0x90, 0x1830,
    "cmpb", 0x91, 0x1830,
    "cmpc", 0x92, 0x1830,
    "cmpd", 0x93, 0x1830,
    "icr", 0x94, 0x30,
    "ecr", 0x94, 0x1800,
    "der", 0x95, 0x1830,
    "counta", 0x96, 0x1830,
    "countb", 0x97, 0x1830,
    "cmpe", 0x98, 0x1830,
    "cmpf", 0x99, 0x1830,
    "cmpg", 0x9A, 0x1830,
    "cmph", 0x9B, 0x1830,
    "lctrl1", 0x9C, 0x1830,
    "lctrl2", 0x9D, 0x1830,
    "ictrl", 0x9E, 0x1830,
    "bar", 0x9F, 0x1830,
    "vrsave", 0x100, 0x40000000,
    "sprg0", 0x110, 0xFFFFF,
    "sprg1", 0x111, 0xFFFFF,
    "sprg2", 0x112, 0xFFFFF,
    "sprg3", 0x113, 0xFFFFF,
    "ear", 0x11A, 0xFE7CF,
    "tbl", 0x11C, 0xFF83F,
    "tbu", 0x11D, 0xFF83F,
    "tbl_write", 0x11C, 0xFF83F,
    "tbu_write", 0x11D, 0xFF83F,
    "pvr", 0x11F, 0xFFFFF,
    "ibat0u", 0x210, 0xFE7CF,
    "mi_gra", 0x210, 0x1000,
    "ibat0l", 0x211, 0xFE7CF,
    "ibat1u", 0x212, 0xFE7CF,
    "ibat1l", 0x213, 0xFE7CF,
    "ibat2u", 0x214, 0xFE7CF,
    "ibat2l", 0x215, 0xFE7CF,
    "ibat3u", 0x216, 0xFE7CF,
    "ibat3l", 0x217, 0xFE7CF,
    "dbat0u", 0x218, 0xFE7CE,
    "l2u_gra", 0x218, 0x1000,
    "dbat0l", 0x219, 0xFE7CE,
    "dbat1u", 0x21A, 0xFE7CE,
    "dbat1l", 0x21B, 0xFE7CE,
    "dbat2u", 0x21C, 0xFE7CE,
    "dbat2l", 0x21D, 0xFE7CE,
    "dbat3u", 0x21E, 0xFE7CE,
    "dbat3l", 0x21F, 0xFE7CE,
    "ic_cst", 0x230, 0x30,
    "iccst", 0x230, 0x800,
    "bbcmcr", 0x230, 0x1000,
    "ic_adr", 0x231, 0x30,
    "icadr", 0x231, 0x800,
    "ic_dat", 0x232, 0x30,
    "icdat", 0x232, 0x800,
    "dc_cst", 0x238, 0x30,
    "l2u_mcr", 0x238, 0x1000,
    "dc_adr", 0x239, 0x30,
    "dc_dat", 0x23A, 0x30,
    "dpdr", 0x276, 0x1830,
    "dpir", 0x277, 0x30,
    "immr", 0x27E, 0x30,
    "mi_ctr", 0x310, 0x30,
    "mi_rba0", 0x310, 0x1000,
    "mi_rba1", 0x311, 0x1000,
    "mi_rba2", 0x312, 0x1000,
    "mi_ap", 0x312, 0x30,
    "mi_epn", 0x313, 0x30,
    "mi_rba3", 0x313, 0x1000,
    "mi_twc", 0x315, 0x30,
    "mi_l1dl2p", 0x315, 0x30,
    "mi_rpn", 0x316, 0x30,
    "md_ctr", 0x318, 0x30,
    "l2u_rba0", 0x318, 0x1000,
    "l2u_rba1", 0x319, 0x1000,
    "m_casid", 0x319, 0x30,
    "md_ap", 0x31A, 0x30,
    "l2u_rba2", 0x31A, 0x1000,
    "l2u_rba3", 0x31B, 0x1000,
    "md_epn", 0x31B, 0x30,
    "m_twb", 0x31C, 0x30,
    "md_l1p", 0x31C, 0x30,
    "md_twc", 0x31D, 0x30,
    "md_l1dl2p", 0x31D, 0x30,
    "md_rpn", 0x31E, 0x30,
    "m_tw", 0x31F, 0x30,
    "m_save", 0x31F, 0x30,
    "mi_dbcam", 0x330, 0x10,
    "mi_cam", 0x330, 0x20,
    "mi_ra0", 0x330, 0x1000,
    "mi_ra1", 0x331, 0x1000,
    "mi_dbram0", 0x331, 0x10,
    "mi_ram0", 0x331, 0x20,
    "mi_dbram1", 0x332, 0x10,
    "mi_ram1", 0x332, 0x20,
    "mi_ra2", 0x332, 0x1000,
    "mi_ra3", 0x333, 0x1000,
    "md_dbcam", 0x338, 0x10,
    "md_cam", 0x338, 0x20,
    "l2u_ra0", 0x338, 0x1000,
    "l2u_ra1", 0x339, 0x1000,
    "md_dbram0", 0x339, 0x10,
    "md_ram0", 0x339, 0x20,
    "md_dbram1", 0x33A, 0x10,
    "md_ram1", 0x33A, 0x20,
    "l2u_ra2", 0x33A, 0x1000,
    "l2u_ra3", 0x33B, 0x1000,
    "gqr0", 0x390, 0x20000000,
    "gqr1", 0x391, 0x20000000,
    "gqr2", 0x392, 0x20000000,
    "gqr3", 0x393, 0x20000000,
    "gqr4", 0x394, 0x20000000,
    "gqr5", 0x395, 0x20000000,
    "gqr6", 0x396, 0x20000000,
    "gqr7", 0x397, 0x20000000,
    "hid_g", 0x398, 0x20000000,
    "wpar", 0x399, 0x20000000,
    "dma_u", 0x39A, 0x20000000,
    "dma_l", 0x39B, 0x20000000,
    "ummcr2", 0x3A0, 0x4000,
    "ubamr", 0x3A7, 0x4000,
    "ummcr0", 0x3A8, 0xE000,
    "upmc1", 0x3A9, 0xE000,
    "upmc2", 0x3AA, 0xE000,
    "usia", 0x3AB, 0x2000,
    "usiar", 0x3AB, 0x4000,
    "ummcr1", 0x3AC, 0xE000,
    "upmc3", 0x3AD, 0xE000,
    "upmc4", 0x3AE, 0xE000,
    "zpr", 0x3B0, 0x200,
    "mmcr2", 0x3B0, 0x4000,
    "pid", 0x3B1, 0x200,
    "bamr", 0x3B7, 0x4000,
    "mmcr0", 0x3B8, 0xE008,
    "pmc1", 0x3B9, 0xE008,
    "sgr", 0x3B9, 0x240,
    "pmc2", 0x3BA, 0xE008,
    "dcwr", 0x3BA, 0x240,
    "sia", 0x3BB, 0xE008,
    "siar", 0x3BB, 0x4000,
    "sler", 0x3BB, 0x40,
    "mmcr1", 0x3BC, 0xE000,
    "pmc3", 0x3BD, 0xE000,
    "pmc4", 0x3BE, 0xE000,
    "sda", 0x3BF, 0x8,
    "tbhu", 0x3CC, 0x240,
    "tblu", 0x3CD, 0x240,
    "dmiss", 0x3D0, 0x10006,
    "dcmp", 0x3D1, 0x10006,
    "hash1", 0x3D2, 0x10006,
    "hash2", 0x3D3, 0x10006,
    "icdbdr", 0x3D3, 0x7C0,
    "imiss", 0x3D4, 0x10006,
    "esr", 0x3D4, 0x7C0,
    "icmp", 0x3D5, 0x10006,
    "dear", 0x3D5, 0x7C0,
    "rpa", 0x3D6, 0x10006,
    "evpr", 0x3D6, 0x7C0,
    "cdbcr", 0x3D7, 0x7C0,
    "tsr", 0x3D8, 0x7C0,
    "tcr", 0x3D8, 0x2,
    "tcr", 0x3DA, 0x7C0,
    "ibr", 0x3DA, 0x2,
    "pit", 0x3DB, 0x7C0,
    "esasrr", 0x3DB, 0x2,
    "tbhi", 0x3DC, 0x7C0,
    "tblo", 0x3DD, 0x7C0,
    "srr2", 0x3DE, 0x7C0,
    "sebr", 0x3DE, 0x2,
    "srr3", 0x3DF, 0x7C0,
    "ser", 0x3DF, 0x2,
    "hid0", 0x3F0, 0x1E00F,
    "dbsr", 0x3F0, 0x80,
    "hid1", 0x3F1, 0x1E007,
    "hid2", 0x3F2, 0x1,
    "iabr", 0x3F2, 0x1E00F,
    "dbcr", 0x3F2, 0x7C0,
    "hid2", 0x3F3, 0x10000,
    "iac1", 0x3F4, 0x80,
    "iac", 0x3F4, 0x40,
    "dabr", 0x3F5, 0xE009,
    "iac2", 0x3F5, 0x80,
    "hid5", 0x3F5, 0x1,
    "dac1", 0x3F6, 0x80,
    "dac", 0x3F6, 0x40,
    "msscr0", 0x3F6, 0x4000,
    "dac2", 0x3F7, 0x80,
    "l2cr", 0x3F9, 0xE000,
    "dccr", 0x3FA, 0x7C0,
    "iccr", 0x3FB, 0x7C0,
    "ictc", 0x3FB, 0xE000,
    "pbl1", 0x3FC, 0x80,
    "thrm1", 0x3FC, 0xE000,
    "pbu1", 0x3FD, 0x80,
    "thrm2", 0x3FD, 0xE000,
    "fpecr", 0x3FE, 0x1800,
    "pbl2", 0x3FE, 0x80,
    "thrm3", 0x3FE, 0xE000,
    "pir", 0x3FF, 0x4008,
    "hid15", 0x3FF, 0x1,
    "pbu2", 0x3FF, 0x80,
    NULL, 0, 0
};
// clang-format on

// clang-format off
UInt32 spr_cpus[1024] = {
    0x1, 0xFFFFF, 0, 0, 0x1, 0x1, 0, 0,
    0xFFFFF, 0xFFFFF, 0, 0, 0, 0, 0, 0,
    0, 0, 0xFF83F, 0xFF83F, 0, 0, 0xFF83F, 0,
    0, 0xFE00F, 0xFFFFF, 0xFFFFF, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0x1830, 0x1830, 0x1830, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0x1830, 0x1830, 0x1830, 0x1830, 0x1830, 0x1830, 0x1830, 0x1830,
    0x1830, 0x1830, 0x1830, 0x1830, 0x1830, 0x1830, 0x1830, 0x1830,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0x40000000, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0xFFFFF, 0xFFFFF, 0xFFFFF, 0xFFFFF, 0, 0, 0, 0,
    0, 0, 0xFE7CF, 0, 0xFF83F, 0xFF83F, 0, 0xFFFFF,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0xFF7CF, 0xFE7CF, 0xFE7CF, 0xFE7CF, 0xFE7CF, 0xFE7CF, 0xFE7CF, 0xFE7CF,
    0xFF7CE, 0xFE7CE, 0xFE7CE, 0xFE7CE, 0xFE7CE, 0xFE7CE, 0xFE7CE, 0xFE7CE,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0x1830, 0x1830, 0x1830, 0, 0, 0, 0, 0,
    0x1030, 0x30, 0x30, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0x1830, 0x30,
    0, 0, 0, 0, 0, 0, 0x30, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0x1030, 0x1000, 0x1030, 0x1030, 0, 0x30, 0x30, 0,
    0x1030, 0x1030, 0x1030, 0x1030, 0x30, 0x30, 0x30, 0x30,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0x1030, 0x1030, 0x1030, 0x1000, 0, 0, 0, 0,
    0x1030, 0x1030, 0x1030, 0x1000, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0x20000000, 0x20000000, 0x20000000, 0x20000000, 0x20000000, 0x20000000, 0x20000000, 0x20000000,
    0x20000000, 0x20000000, 0x20000000, 0x20000000, 0, 0, 0, 0,
    0x4000, 0, 0, 0, 0, 0, 0, 0x4000,
    0xE000, 0xE000, 0xE000, 0xE000, 0xE000, 0xE000, 0xE000, 0,
    0x4200, 0x200, 0, 0, 0, 0, 0, 0x4000,
    0xE008, 0xE248, 0xE248, 0xE048, 0xE000, 0xE000, 0xE000, 0x8,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0x240, 0x240, 0, 0,
    0x10006, 0x10006, 0x10006, 0x107C6, 0x107C6, 0x107C6, 0x107C6, 0x7C0,
    0x7C2, 0, 0x7C2, 0x7C2, 0x7C0, 0x7C0, 0x7C2, 0x7C2,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0x1E08F, 0x1E007, 0x1E7CF, 0x10000, 0xC0, 0xE089, 0x40C0, 0x80,
    0, 0xE000, 0x7C0, 0xE7C0, 0xE080, 0xE080, 0xF880, 0xE089
};
// clang-format on

// clang-format off
static AsmDcr asm_dcrs[] = {
    0x40, "exisr", 0x80,
    0x42, "exier", 0x80,
    0x70, "brh0", 0x400,
    0x71, "brh1", 0x400,
    0x72, "brh2", 0x400,
    0x73, "brh3", 0x400,
    0x74, "brh4", 0x400,
    0x75, "brh5", 0x400,
    0x76, "brh6", 0x400,
    0x77, "brh7", 0x400,
    0x80, "br0", 0x80,
    0x80, "brcr0", 0x40,
    0x81, "br1", 0x80,
    0x81, "brcr1", 0x40,
    0x82, "br2", 0x80,
    0x82, "brcr2", 0x40,
    0x83, "br3", 0x80,
    0x83, "brcr3", 0x40,
    0x84, "br4", 0x80,
    0x84, "brcr4", 0x40,
    0x85, "br5", 0x80,
    0x85, "brcr5", 0x40,
    0x86, "br6", 0x80,
    0x86, "brcr6", 0x40,
    0x87, "br7", 0x80,
    0x87, "brcr7", 0x40,
    0x90, "bear", 0x7C0,
    0x91, "besr", 0x80,
    0x91, "besr0", 0x40,
    0xA0, "iocr", 0x7C0,
    0xA1, "pmcr0", 0x40,
    0xC0, "dmacr0", 0x80,
    0xC1, "dmact0", 0x80,
    0xC2, "dmada0", 0x80,
    0xC3, "dmasa0", 0x80,
    0xC4, "dmacc0", 0x80,
    0xC8, "dmacr1", 0x80,
    0xC9, "dmact1", 0x80,
    0xCA, "dmada1", 0x80,
    0xCB, "dmasa1", 0x80,
    0xCC, "dmacc1", 0x80,
    0xD0, "dmacr2", 0x300,
    0xD1, "dmact2", 0x300,
    0xD2, "dmada2", 0x300,
    0xD3, "dmasa2", 0x300,
    0xD4, "dmacc2", 0x300,
    0xD8, "dmacr3", 0x300,
    0xD9, "dmact3", 0x300,
    0xDA, "dmada3", 0x300,
    0xDB, "dmasa3", 0x300,
    0xDC, "dmacc3", 0x300,
    0xE0, "dmasr", 0x80,
    0, NULL, 0
};
// clang-format on

static void InlineAsm_InsertSPR(char* name, short num, UInt32 cpus) {
    IARegInfo** ptr;
    IARegInfo* info;

    ptr = hashedsprs + (CHash(name) & 63);
    info = lalloc(sizeof(IARegInfo));
    info->cpus = cpus;
    info->reg.name = name;
    info->reg.rclass = RegClass_SPR;
    info->reg.num = num;
    info->reg.object = NULL;

    info->next = *ptr;
    *ptr = info;
}

static void InlineAsm_InsertRegisterDCR(char* name, UInt32 cpus, short num) {
    IARegInfo** ptr;
    IARegInfo* info;

    ptr = hasheddcrs + (CHash(name) & 63);
    info = lalloc(sizeof(IARegInfo));
    info->cpus = cpus;
    info->reg.name = name;
    info->reg.rclass = RegClass_DCR;
    info->reg.num = num;
    info->reg.object = NULL;

    info->next = *ptr;
    *ptr = info;
}

IARegister* InlineAsm_LookupRegisterPPCName(HashNameNode* name) {
    IALookupResult result;
    IARegister* reg;
    Type* type;

    if (InlineAsm_LookupSymbol(name, &result) &&
        result.object &&
        is_register_object(result.object)) {
        type = result.object->type;
        if ((reg = InlineAsm_LookupRegisterPPC(name->name)) && reg->object == result.object) {
            return reg;
        }

        if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) || IS_TYPE_VEC2X32FLOAT(type)) {
            InlineAsm_InsertRegister(name->name, RegClass_FPR, 0, result.object);
        } else if (IS_TYPE_VECTOR(type)) {
            InlineAsm_InsertRegister(name->name, RegClass_VR, 0, result.object);
        } else {
            InlineAsm_InsertRegister(name->name, RegClass_GPR, 0, result.object);
        }
    }

    return InlineAsm_LookupRegisterPPC(name->name);
}

IARegister* InlineAsm_LookupRegisterPPC(char* name) {
    IARegInfo* scan;
    IARegister* reg;
    IARegister* check;
    char buf[40];

    if ((check = InlineAsm_LookupRegister(name))) {
        return check;
    }

    reg = NULL;
    if (strlen(name) < 40) {
        CToLowercase(name, buf);
    } else {
        return NULL;
    }

    for (scan = hashedsprs[CHash(buf) & 63]; scan; scan = scan->next) {
        check = &scan->reg;
        if (!strcmp(check->name, buf)) {
            if (cpu == CPUMask_Generic) {
                if ((cpu & CPUFLAG_LOW_MASK) == ((cpu & CPUFLAG_LOW_MASK) & scan->cpus)) {
                    return check;
                }
            } else {
                if (scan->cpus & cpu) {
                    return check;
                }
            }
            reg = check;
        }
    }

    if (reg) {
        if (copts.warn_possunwant) {
            PPCError_Warning(PPCERR_REGISTER_NOT_ON_PROCESSOR, name);
        }
        return reg;
    }

    if (!strncmp("spr", buf, 3)) {
        static IARegister thespr;
        Boolean overflow;
        UInt32 result;
        ScanDec(buf + 3, &result, &overflow);
        if (overflow || result > 1024) {
            PPCError_Error(PPCERR_REGISTER_NOT_ON_PROCESSOR, name);
            return NULL;
        }

        thespr.name = NULL;
        thespr.rclass = RegClass_SPR;
        thespr.num = result;
        thespr.object = NULL;

        if (copts.warn_possunwant) {
            if (cpu == CPUMask_Generic) {
                if ((cpu & CPUFLAG_LOW_MASK) != ((cpu & CPUFLAG_LOW_MASK) & spr_cpus[result])) {
                    PPCError_Warning(PPCERR_REGISTER_NOT_ON_PROCESSOR, name);
                }
            } else {
                if (!(cpu & spr_cpus[result])) {
                    PPCError_Warning(PPCERR_REGISTER_NOT_ON_PROCESSOR, name);
                }
            }
        }

        return &thespr;
    }

    return NULL;
}

IARegister* InlineAsm_LookupDCRRegister(char* name) {
    IARegInfo* scan;
    IARegister* check;
    IARegister* reg;
    char buf[40];

    if (strlen(name) < 40) {
        CToLowercase(name, buf);
    } else {
        return NULL;
    }

    reg = NULL;
    for (scan = hasheddcrs[CHash(buf) & 63]; scan; scan = scan->next) {
        check = &scan->reg;
        if (!strcmp(check->name, buf)) {
            if (cpu == CPUMask_Generic) {
                if ((cpu & CPUFLAG_LOW_MASK) == ((cpu & CPUFLAG_LOW_MASK) & scan->cpus)) {
                    return check;
                }
            } else {
                if (scan->cpus & cpu) {
                    return check;
                }
            }
            reg = check;
        }
    }

    if (reg) {
        if (copts.warn_possunwant) {
            PPCError_Warning(PPCERR_REGISTER_NOT_ON_PROCESSOR, name);
        }
        return reg;
    }

    if (!strncmp("dcr", buf, 3)) {
        static IARegister thespr;
        Boolean overflow;
        UInt32 result;
        ScanDec(buf + 3, &result, &overflow);
        if (overflow || result > 1024) {
            PPCError_Error(PPCERR_REGISTER_NOT_ON_PROCESSOR, name);
            return NULL;
        }

        thespr.name = NULL;
        thespr.rclass = RegClass_DCR;
        thespr.num = result;
        thespr.object = NULL;
        return &thespr;
    }

    return NULL;
}

void InlineAsm_InitializeRegistersPPC(void) {
    AsmRegister* asmreg;
    AsmSpr* asmspr;
    AsmDcr* asmdcr;
    HashNameNode* name;
    Object* obj;
    ObjectList* list;
    char buf[20];
    SInt32 i;

    setup_diagnostic_reg_strings();

    for (i = 0; i < 64; i++) {
        hashedsprs[i] = NULL;
    }
    for (i = 0; i < 64; i++) {
        hasheddcrs[i] = NULL;
    }

    for (asmreg = asm_registers; asmreg->name; asmreg++) {
        name = GetHashNameNodeExport(asmreg->name);
        for (list = arguments; list; list = list->next) {
            obj = list->object;
            if (obj && obj->name == name) {
                switch (asmreg->rclass) {
                    case RegClass_SPR:
                    case RegClass_CRFIELD:
                    case RegClass_VR:
                    case RegClass_FPR:
                    case RegClass_GPR:
                        sprintf(buf, register_class_format[asmreg->rclass], asmreg->num);
                        break;
                    case RegClass_CRBIT:
                        sprintf(buf, "crbit_%ld", asmreg->num);
                        break;
                    case RegClass_DCR:
                        sprintf(buf, "DCR%ld", asmreg->num);
                        break;
                    default:
                        sprintf(buf, "{?}%ld", asmreg->num);
                        break;
                }
                PPCError_Warning(PPCERR_LOCAL_NAMED_LIKE_REGISTER, obj->name->name, buf);
            }
        }
        for (list = locals; list; list = list->next) {
            obj = list->object;
            if (obj && obj->name == name) {
                switch (asmreg->rclass) {
                    case RegClass_SPR:
                    case RegClass_CRFIELD:
                    case RegClass_VR:
                    case RegClass_FPR:
                    case RegClass_GPR:
                        sprintf(buf, register_class_format[asmreg->rclass], asmreg->num);
                        break;
                    case RegClass_CRBIT:
                        sprintf(buf, "crbit_%ld", asmreg->num);
                        break;
                    case RegClass_DCR:
                        sprintf(buf, "DCR%ld", asmreg->num);
                        break;
                    default:
                        sprintf(buf, "{?}%ld", asmreg->num);
                        break;
                }
                PPCError_Warning(PPCERR_LOCAL_NAMED_LIKE_REGISTER, obj->name->name, buf);
            }
        }

        InlineAsm_InsertRegister(asmreg->name, asmreg->rclass, asmreg->num, NULL);
    }

    for (asmspr = asm_sprs; asmspr->name; asmspr++) {
        InlineAsm_InsertSPR(asmspr->name, asmspr->num, asmspr->mask);
    }

    for (asmdcr = asm_dcrs; asmdcr->name; asmdcr++) {
        InlineAsm_InsertRegisterDCR(asmdcr->name, asmdcr->num, asmdcr->mask);
    }
}
