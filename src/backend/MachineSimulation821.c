#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Scheduler.h"

/* The model's statics: every machine model has these names, so each prefixes its own */
#define latency m821_latency
#define initialize m821_initialize
#define can_issue m821_can_issue
#define issue m821_issue
#define advance_clock m821_advance_clock
#define serializes m821_serializes

// https://www.nxp.com/docs/en/user-guide/MPC821UM.pdf

// NOTE: invented names: IntegerUnit, FPUnit
// Stage2 is unused.
typedef enum Stage {
    BranchUnit,
    IntegerUnit,
    Stage2,
    LSU1, // Load/Store Unit
    LSU2,
    CRUnit, // Condition Register Unit
    NumStages,
    // the floating-point rows' stage: the 821 has no FPU, and this is past the end of `pipeline`,
    // so can_issue and issue reach beyond the array for an opcode with such a row (the original's;
    // with the misaligned table below, mfctr and mflr are among them)
    FPUnit
} Stage;

static struct {
    // the instruction currently in this pipeline stage
    PCode* instr;

    // how many cycles are left for this instruction to finish
    int remaining;
} pipeline[NumStages];

#ifndef __MWERKS__
// FPUnit's slot, past the end of `pipeline`, is in the original the variables that follow it in
// memory, SpillCode.c's: the instruction is rTEMP_for_PS_spill and rTEMP_for_VR_spill (its address's
// low and high halves), the cycles last_unused_vreg_before_spilling. Nothing here clears it, and
// SpillCode sets and clears the first two: an instruction of that row issues only while both are
// zero. (SpillCode sets all three before it reads them.)
extern short rTEMP_for_PS_spill;
extern short rTEMP_for_VR_spill;
extern int last_unused_vreg_before_spilling;
#endif

enum {
    MaxEntries = 6
};

static struct {
    // how many entries remain unused in the queue
    unsigned int free;

    // how many entries are currently used in the queue
    unsigned int used;

    // the index of the next instruction that will be retired
    unsigned int nextToRetire;

    // the index of the next free slot that will be used when an instruction is dispatched
    unsigned int nextFreeSlot;

    // circular array of entries in the completion queue
    struct {
        PCode* instr;
        int completed;
    } entries[MaxEntries];
} completionbuffers;

static int latency(PCode* instr);
static void initialize(void);
static int can_issue(PCode* instr);
static void issue(PCode* instr);
static void advance_clock(void);
static int serializes(PCode* instr);

// clang-format off
MachineInfo machine821 = {
    1,
    FALSE,
    0,
    &latency,
    &initialize,
    &can_issue,
    &issue,
    &advance_clock,
    &serializes,
    &default_uses_vpermute_unit
};
// clang-format on

static struct {
    // the initial stage for this instruction
    Stage stage;

    // the total amount of cycles required by this instruction
    char latency;

    // how long it takes to finish each stage
    char cycles[3];

    // does this instruction serialise?
    char serializes;
    // clang-format off
} instruction_timing[OPCODE_MAX] = {
    // (the original's rows do not line up with the opcodes: from PC_LBZ on, each opcode gets the
    // row meant for the one before it, and from the floating-point instructions on the row meant
    // for one three or four after it; each comment names the opcode that indexes the row)
    BranchUnit,  0,   0,   0,  0,  0, // PC_B
    BranchUnit,  0,   0,   0,  0,  0, // PC_BL
    BranchUnit,  0,   0,   0,  0,  0, // PC_BC
    BranchUnit,  0,   0,   0,  0,  0, // PC_BCLR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BCCTR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BT
    BranchUnit,  0,   0,   0,  0,  0, // PC_BTLR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BTCTR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BF
    BranchUnit,  0,   0,   0,  0,  0, // PC_BFLR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BFCTR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BDNZ
    BranchUnit,  0,   0,   0,  0,  0, // PC_BDNZT
    BranchUnit,  0,   0,   0,  0,  0, // PC_BDNZF
    BranchUnit,  0,   0,   0,  0,  0, // PC_BDZ
    BranchUnit,  0,   0,   0,  0,  0, // PC_BDZT
    BranchUnit,  0,   0,   0,  0,  0, // PC_BDZF
    BranchUnit,  0,   0,   0,  0,  0, // PC_BLR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BCTR
    BranchUnit,  0,   0,   0,  0,  0, // PC_BCTRL
    BranchUnit,  0,   0,   0,  0,  0, // PC_BLRL
    BranchUnit,  0,   0,   0,  0,  0, // PC_LBZ
    LSU1,        2,   1,   1,  0,  0, // PC_LBZU
    LSU1,        2,   1,   1,  0,  0, // PC_LBZX
    LSU1,        2,   1,   1,  0,  0, // PC_LBZUX
    LSU1,        2,   1,   1,  0,  0, // PC_LHZ
    LSU1,        2,   1,   1,  0,  0, // PC_LHZU
    LSU1,        2,   1,   1,  0,  0, // PC_LHZX
    LSU1,        2,   1,   1,  0,  0, // PC_LHZUX
    LSU1,        2,   1,   1,  0,  0, // PC_LHA
    LSU1,        2,   1,   1,  0,  0, // PC_LHAU
    LSU1,        2,   1,   1,  0,  0, // PC_LHAX
    LSU1,        2,   1,   1,  0,  0, // PC_LHAUX
    LSU1,        2,   1,   1,  0,  0, // PC_LHBRX
    LSU1,        2,   1,   1,  0,  0, // PC_LWZ
    LSU1,        2,   1,   1,  0,  0, // PC_LWZU
    LSU1,        2,   1,   1,  0,  0, // PC_LWZX
    LSU1,        2,   1,   1,  0,  0, // PC_LWZUX
    LSU1,        2,   1,   1,  0,  0, // PC_LWBRX
    LSU1,        2,   1,   1,  0,  0, // PC_LMW
    LSU1,        2,   1,   1,  0,  0, // PC_STB
    LSU1,        2,   1,   1,  0,  0, // PC_STBU
    LSU1,        2,   1,   1,  0,  0, // PC_STBX
    LSU1,        2,   1,   1,  0,  0, // PC_STBUX
    LSU1,        2,   1,   1,  0,  0, // PC_STH
    LSU1,        2,   1,   1,  0,  0, // PC_STHU
    LSU1,        2,   1,   1,  0,  0, // PC_STHX
    LSU1,        2,   1,   1,  0,  0, // PC_STHUX
    LSU1,        2,   1,   1,  0,  0, // PC_STHBRX
    LSU1,        2,   1,   1,  0,  0, // PC_STW
    LSU1,        2,   1,   1,  0,  0, // PC_STWU
    LSU1,        2,   1,   1,  0,  0, // PC_STWX
    LSU1,        2,   1,   1,  0,  0, // PC_STWUX
    LSU1,        2,   1,   1,  0,  0, // PC_STWBRX
    LSU1,        2,   1,   1,  0,  0, // PC_STMW
    LSU1,        2,   1,   1,  0,  0, // PC_DCBF
    LSU1,        2,   1,   1,  0,  0, // PC_DCBST
    LSU1,        2,   1,   1,  0,  0, // PC_DCBT
    LSU1,        2,   1,   1,  0,  0, // PC_DCBTST
    LSU1,        2,   1,   1,  0,  0, // PC_DCBZ
    LSU1,        2,   1,   1,  0,  0, // PC_ADD
    IntegerUnit, 1,   1,   0,  0,  0, // PC_ADDC
    IntegerUnit, 1,   1,   0,  0,  0, // PC_ADDE
    IntegerUnit, 1,   1,   0,  0,  0, // PC_ADDI
    IntegerUnit, 1,   1,   0,  0,  0, // PC_ADDIC
    IntegerUnit, 1,   1,   0,  0,  0, // PC_ADDICR
    IntegerUnit, 1,   1,   0,  0,  0, // PC_ADDIS
    IntegerUnit, 1,   1,   0,  0,  0, // PC_ADDME
    IntegerUnit, 1,   1,   0,  0,  0, // PC_ADDZE
    IntegerUnit, 1,   1,   0,  0,  0, // PC_DIVW
    IntegerUnit, 37,  37,  0,  0,  0, // PC_DIVWU
    IntegerUnit, 37,  37,  0,  0,  0, // PC_MULHW
    IntegerUnit, 5,   5,   0,  0,  0, // PC_MULHWU
    IntegerUnit, 5,   5,   0,  0,  0, // PC_MULLI
    IntegerUnit, 3,   3,   0,  0,  0, // PC_MULLW
    IntegerUnit, 5,   5,   0,  0,  0, // PC_NEG
    IntegerUnit, 1,   1,   0,  0,  0, // PC_SUBF
    IntegerUnit, 1,   1,   0,  0,  0, // PC_SUBFC
    IntegerUnit, 1,   1,   0,  0,  0, // PC_SUBFE
    IntegerUnit, 1,   1,   0,  0,  0, // PC_SUBFIC
    IntegerUnit, 1,   1,   0,  0,  0, // PC_SUBFME
    IntegerUnit, 1,   1,   0,  0,  0, // PC_SUBFZE
    IntegerUnit, 1,   1,   0,  0,  0, // PC_CMPI
    IntegerUnit, 3,   1,   0,  0,  0, // PC_CMP
    IntegerUnit, 3,   1,   0,  0,  0, // PC_CMPLI
    IntegerUnit, 3,   1,   0,  0,  0, // PC_CMPL
    IntegerUnit, 3,   1,   0,  0,  0, // PC_ANDI
    IntegerUnit, 1,   1,   0,  0,  0, // PC_ANDIS
    IntegerUnit, 1,   1,   0,  0,  0, // PC_ORI
    IntegerUnit, 1,   1,   0,  0,  0, // PC_ORIS
    IntegerUnit, 1,   1,   0,  0,  0, // PC_XORI
    IntegerUnit, 1,   1,   0,  0,  0, // PC_XORIS
    IntegerUnit, 1,   1,   0,  0,  0, // PC_AND
    IntegerUnit, 1,   1,   0,  0,  0, // PC_OR
    IntegerUnit, 1,   1,   0,  0,  0, // PC_XOR
    IntegerUnit, 1,   1,   0,  0,  0, // PC_NAND
    IntegerUnit, 1,   1,   0,  0,  0, // PC_NOR
    IntegerUnit, 1,   1,   0,  0,  0, // PC_EQV
    IntegerUnit, 1,   1,   0,  0,  0, // PC_ANDC
    IntegerUnit, 1,   1,   0,  0,  0, // PC_ORC
    IntegerUnit, 1,   1,   0,  0,  0, // PC_EXTSB
    IntegerUnit, 1,   1,   0,  0,  0, // PC_EXTSH
    IntegerUnit, 1,   1,   0,  0,  0, // PC_CNTLZW
    IntegerUnit, 1,   1,   0,  0,  0, // PC_RLWINM
    IntegerUnit, 1,   1,   0,  0,  0, // PC_RLWNM
    IntegerUnit, 1,   1,   0,  0,  0, // PC_RLWIMI
    IntegerUnit, 1,   1,   0,  0,  0, // PC_SLW
    IntegerUnit, 1,   1,   0,  0,  0, // PC_SRW
    IntegerUnit, 1,   1,   0,  0,  0, // PC_SRAWI
    IntegerUnit, 1,   1,   0,  0,  0, // PC_SRAW
    IntegerUnit, 1,   1,   0,  0,  0, // PC_CRAND
    CRUnit,      1,   1,   0,  0,  0, // PC_CRANDC
    CRUnit,      1,   1,   0,  0,  0, // PC_CREQV
    CRUnit,      1,   1,   0,  0,  0, // PC_CRNAND
    CRUnit,      1,   1,   0,  0,  0, // PC_CRNOR
    CRUnit,      1,   1,   0,  0,  0, // PC_CROR
    CRUnit,      1,   1,   0,  0,  0, // PC_CRORC
    CRUnit,      1,   1,   0,  0,  0, // PC_CRXOR
    CRUnit,      1,   1,   0,  0,  0, // PC_MCRF
    CRUnit,      1,   1,   0,  0,  0, // PC_MTXER
    IntegerUnit, 1,   1,   0,  0,  0, // PC_MTCTR
    BranchUnit,  2,   2,   0,  0,  0, // PC_MTLR
    BranchUnit,  2,   2,   0,  0,  0, // PC_MTCRF
    IntegerUnit, 1,   1,   0,  0,  0, // PC_MTMSR
    IntegerUnit, 1,   1,   0,  0,  0, // PC_MTSPR
    IntegerUnit, 1,   1,   0,  0,  0, // PC_MFMSR
    IntegerUnit, 1,   1,   0,  0,  0, // PC_MFSPR
    IntegerUnit, 1,   1,   0,  0,  0, // PC_MFXER
    FPUnit,      3,   1,   1,  1,  0, // PC_MFCTR
    FPUnit,      3,   1,   1,  1,  0, // PC_MFLR
    IntegerUnit, 1,   1,   0,  0,  0, // PC_MFCR
    IntegerUnit, 1,   1,   0,  0,  0, // PC_MFFS
    IntegerUnit, 1,   1,   0,  0,  0, // PC_MTFSF
    IntegerUnit, 1,   1,   0,  0,  0, // PC_EIEIO
    IntegerUnit, 1,   1,   0,  0,  0, // PC_ISYNC
    IntegerUnit, 1,   1,   0,  0,  0, // PC_SYNC
    IntegerUnit, 1,   1,   0,  0,  0, // PC_RFI
    IntegerUnit, 1,   1,   0,  0,  0, // PC_LI
    LSU1,        2,   1,   1,  0,  0, // PC_LIS
    LSU1,        2,   1,   1,  0,  0, // PC_MR
    LSU1,        2,   1,   1,  0,  0, // PC_NOP
    LSU1,        2,   1,   1,  0,  0, // PC_NOT
    LSU1,        2,   1,   1,  0,  0, // PC_LFS
    LSU1,        2,   1,   1,  0,  0, // PC_LFSU
    LSU1,        2,   1,   1,  0,  0, // PC_LFSX
    LSU1,        2,   1,   1,  0,  0, // PC_LFSUX
    LSU1,        2,   1,   1,  0,  0, // PC_LFD
    LSU1,        2,   1,   1,  0,  0, // PC_LFDU
    LSU1,        2,   1,   1,  0,  0, // PC_LFDX
    LSU1,        2,   1,   1,  0,  0, // PC_LFDUX
    LSU1,        2,   1,   1,  0,  0, // PC_STFS
    LSU1,        2,   1,   1,  0,  0, // PC_STFSU
    LSU1,        2,   1,   1,  0,  0, // PC_STFSX
    LSU1,        2,   1,   1,  0,  0, // PC_STFSUX
    FPUnit,      3,   1,   1,  1,  0, // PC_STFD
    FPUnit,      3,   1,   1,  1,  0, // PC_STFDU
    FPUnit,      3,   1,   1,  1,  0, // PC_STFDX
    FPUnit,      3,   1,   1,  1,  0, // PC_STFDUX
    FPUnit,      3,   1,   1,  1,  0, // PC_FMR
    FPUnit,      3,   1,   1,  1,  0, // PC_FABS
    FPUnit,      3,   1,   1,  1,  0, // PC_FNEG
    FPUnit,      3,   1,   1,  1,  0, // PC_FNABS
    FPUnit,      4,   2,   1,  1,  0, // PC_FADD
    FPUnit,      3,   1,   1,  1,  0, // PC_FADDS
    FPUnit,      33,  33,  0,  0,  0, // PC_FSUB
    FPUnit,      18,  18,  0,  0,  0, // PC_FSUBS
    FPUnit,      4,   2,   1,  1,  0, // PC_FMUL
    FPUnit,      3,   1,   1,  1,  0, // PC_FMULS
    FPUnit,      4,   2,   1,  1,  0, // PC_FDIV
    FPUnit,      3,   1,   1,  1,  0, // PC_FDIVS
    FPUnit,      4,   2,   1,  1,  0, // PC_FMADD
    FPUnit,      3,   1,   1,  1,  0, // PC_FMADDS
    FPUnit,      4,   2,   1,  1,  0, // PC_FMSUB
    FPUnit,      3,   1,   1,  1,  0, // PC_FMSUBS
    FPUnit,      18,  18,  0,  0,  0, // PC_FNMADD
    FPUnit,      3,   1,   1,  1,  0, // PC_FNMADDS
    FPUnit,      3,   1,   1,  1,  0, // PC_FNMSUB
    FPUnit,      3,   1,   1,  1,  0, // PC_FNMSUBS
    FPUnit,      3,   1,   1,  1,  0, // PC_FRES
    FPUnit,      3,   1,   1,  1,  0, // PC_FRSQRTE
    FPUnit,      5,   1,   1,  1,  0, // PC_FSEL
    FPUnit,      5,   1,   1,  1,  0, // PC_FRSP
    LSU1,        1,   0,   0,  0,  0, // PC_FCTIW
    LSU1,        1,   0,   0,  0,  0, // PC_FCTIWZ
    LSU1,        1,   0,   0,  0,  0, // PC_FCMPU
    LSU1,        1,   0,   0,  0,  0, // PC_FCMPO
    LSU1,        1,   0,   0,  0,  0, // PC_LWARX
    LSU1,        1,   0,   0,  0,  0, // PC_LSWI
    LSU1,        1,   0,   0,  0,  0, // PC_LSWX
    IntegerUnit, 1,   0,   0,  0,  0, // PC_STFIWX
    IntegerUnit, 1,   0,   0,  0,  0, // PC_STSWI
    IntegerUnit, 1,   0,   0,  0,  0, // PC_STSWX
    IntegerUnit, 1,   0,   0,  0,  0, // PC_STWCX
    IntegerUnit, 1,   0,   0,  0,  0, // PC_ECIWX
    IntegerUnit, 1,   0,   0,  0,  0, // PC_ECOWX
    IntegerUnit, 1,   0,   0,  0,  0, // PC_DCBI
    IntegerUnit, 1,   0,   0,  0,  0, // PC_ICBI
    IntegerUnit, 1,   0,   0,  0,  0, // PC_MCRFS
    IntegerUnit, 1,   0,   0,  0,  0, // PC_MCRXR
    IntegerUnit, 1,   0,   0,  0,  0, // PC_MFTB
    IntegerUnit, 1,   0,   0,  0,  0, // PC_MFSR
    IntegerUnit, 1,   0,   0,  0,  0, // PC_MTSR
    IntegerUnit, 1,   0,   0,  0,  0, // PC_MFSRIN
    IntegerUnit, 1,   0,   0,  0,  0, // PC_MTSRIN
    IntegerUnit, 1,   0,   0,  0,  0, // PC_MTFSB0
    IntegerUnit, 1,   0,   0,  0,  0, // PC_MTFSB1
    IntegerUnit, 1,   0,   0,  0,  0, // PC_MTFSFI
    IntegerUnit, 1,   0,   0,  0,  1, // PC_SC
    IntegerUnit, 1,   0,   0,  0,  1, // PC_FSQRT
    IntegerUnit, 1,   0,   0,  0,  0, // PC_FSQRTS
    IntegerUnit, 1,   0,   0,  0,  0, // PC_TLBIA
    IntegerUnit, 1,   0,   0,  0,  0, // PC_TLBIE
    IntegerUnit, 1,   0,   0,  0,  0, // PC_TLBLD
    IntegerUnit, 1,   0,   0,  0,  0, // PC_TLBLI
    IntegerUnit, 1,   0,   0,  0,  0, // PC_TLBSYNC
    IntegerUnit, 1,   0,   0,  0,  0, // PC_TW
    IntegerUnit, 1,   0,   0,  0,  1, // PC_TRAP
    IntegerUnit, 1,   0,   0,  0,  1, // PC_TWI
    IntegerUnit, 1,   0,   0,  0,  1, // PC_OPWORD
    IntegerUnit, 1,   0,   0,  0,  1, // PC_MFROM
    IntegerUnit, 1,   0,   0,  0,  0, // PC_DSA
    IntegerUnit, 1,   0,   0,  0,  0, // PC_ESA
    IntegerUnit, 1,   0,   0,  0,  0, // PC_DCCCI
    IntegerUnit, 0,   0,   0,  0,  0, // PC_DCREAD
    IntegerUnit, 0,   0,   0,  0,  0, // PC_ICBT
    IntegerUnit, 0,   0,   0,  0,  0, // PC_ICCCI
    IntegerUnit, 0,   0,   0,  0,  0, // PC_ICREAD
    IntegerUnit, 0,   0,   0,  0,  0, // PC_RFCI
    IntegerUnit, 0,   0,   0,  0,  0, // PC_TLBRE
    IntegerUnit, 0,   0,   0,  0,  0, // PC_TLBSX
    IntegerUnit, 0,   0,   0,  0,  0, // PC_TLBWE
    IntegerUnit, 0,   0,   0,  0,  0, // PC_WRTEE
    IntegerUnit, 0,   0,   0,  0,  0, // PC_WRTEEI
    IntegerUnit, 0,   0,   0,  0,  0, // PC_MFDCR
    IntegerUnit, 0,   0,   0,  0,  0, // PC_MTDCR
    IntegerUnit, 0,   0,   0,  0,  0, // PC_DCBA
    IntegerUnit, 0,   0,   0,  0,  0, // PC_ISELGT
    BranchUnit,  0,   0,   0,  0,  0, // PC_ISELEQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_ISELLT
    BranchUnit,  0,   0,   0,  0,  0, // PC_DSS
    BranchUnit,  0,   0,   0,  0,  0, // PC_DSSALL
    BranchUnit,  0,   0,   0,  0,  0, // PC_DST
    BranchUnit,  0,   0,   0,  0,  0, // PC_DSTT
    BranchUnit,  0,   0,   0,  0,  0, // PC_DSTST
    BranchUnit,  0,   0,   0,  0,  0, // PC_DSTSTT
    BranchUnit,  0,   0,   0,  0,  0, // PC_LVEBX
    BranchUnit,  0,   0,   0,  0,  0, // PC_LVEHX
    BranchUnit,  0,   0,   0,  0,  0, // PC_LVEWX
    BranchUnit,  0,   0,   0,  0,  0, // PC_LVSL
    BranchUnit,  0,   0,   0,  0,  0, // PC_LVSR
    BranchUnit,  0,   0,   0,  0,  0, // PC_LVX
    BranchUnit,  0,   0,   0,  0,  0, // PC_LVXL
    BranchUnit,  0,   0,   0,  0,  0, // PC_STVEBX
    BranchUnit,  0,   0,   0,  0,  0, // PC_STVEHX
    BranchUnit,  0,   0,   0,  0,  0, // PC_STVEWX
    BranchUnit,  0,   0,   0,  0,  0, // PC_STVX
    BranchUnit,  0,   0,   0,  0,  0, // PC_STVXL
    BranchUnit,  0,   0,   0,  0,  0, // PC_MFVSCR
    BranchUnit,  0,   0,   0,  0,  0, // PC_MTVSCR
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDCUW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDSBS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDSHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDSWS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDUBM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDUBS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDUHM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDUHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDUWM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VADDUWS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VAND
    BranchUnit,  0,   0,   0,  0,  0, // PC_VANDC
    BranchUnit,  0,   0,   0,  0,  0, // PC_VAVGSB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VAVGSH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VAVGSW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VAVGUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VAVGUH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VAVGUW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCFSX
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCFUX
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPBFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPEQFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPEQUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPEQUH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPEQUW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGEFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGTFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGTSB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGTSH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGTSW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGTUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGTUH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCMPGTUW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCTSXS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VCTUXS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VEXPTEFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VLOGEFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMAXFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMAXSB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMAXSH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMAXSW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMAXUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMAXUH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMAXUW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMINFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMINSB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMINSH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMINSW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMINUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMINUH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMINUW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMRGHB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMRGHH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMRGHW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMRGLB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMRGLH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMRGLW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULESB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULESH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULEUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULEUH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULOSB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULOSH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULOUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMULOUH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VNOR
    BranchUnit,  0,   0,   0,  0,  0, // PC_VOR
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKPX
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKSHSS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKSHUS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKSWSS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKSWUS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKUHUM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKUHUS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKUWUM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPKUWUS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VREFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRFIM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRFIN
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRFIP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRFIZ
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRLB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRLH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRLW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VRSQRTEFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSL
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSLB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSLH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSLO
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSLW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSPLTB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSPLTH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSPLTW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSPLTISB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSPLTISH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSPLTISW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSR
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSRAB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSRAH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSRAW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSRB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSRH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSRO
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSRW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBCUW
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBSBS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBSHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBSWS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBUBM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBUBS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBUHM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBUHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBUWM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUBUWS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUMSWS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUM2SWS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUM4SBS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUM4SHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSUM4UBS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VUPKHPX
    BranchUnit,  0,   0,   0,  0,  0, // PC_VUPKHSB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VUPKHSH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VUPKLPX
    BranchUnit,  0,   0,   0,  0,  0, // PC_VUPKLSB
    BranchUnit,  0,   0,   0,  0,  0, // PC_VUPKLSH
    BranchUnit,  0,   0,   0,  0,  0, // PC_VXOR
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMADDFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMHADDSHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMHRADDSHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMLADDUHM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMSUMMBM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMSUMSHM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMSUMSHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMSUMUBM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMSUMUHM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMSUMUHS
    BranchUnit,  0,   0,   0,  0,  0, // PC_VNMSUBFP
    BranchUnit,  0,   0,   0,  0,  0, // PC_VPERM
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSEL
    BranchUnit,  0,   0,   0,  0,  0, // PC_VSLDOI
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMR
    BranchUnit,  0,   0,   0,  0,  0, // PC_VMRP
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_L
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_LU
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_LX
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_LUX
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_ST
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_STU
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_STX
    BranchUnit,  0,   0,   0,  0,  0, // PC_PSQ_STUX
    BranchUnit,  0,   0,   0,  0,  0, // PC_DCBZ_L
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_ADD
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_SUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MUL
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_DIV
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MADD
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MSUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_NMADD
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_NMSUB
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_RES
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_SEL
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_ABS
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_NABS
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_NEG
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MR
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_RSQRTE
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_CMPU0
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_CMPO0
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_CMPU1
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_CMPO1
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MERGE00
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MERGE01
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MERGE10
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MERGE11
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_SUM0
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_SUM1
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MULS0
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MULS1
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MADDS0
    BranchUnit,  0,   0,   0,  0,  0, // PC_PS_MADDS1
    BranchUnit,  0,   0,   0,  0,  0, // PC_SLE
    BranchUnit,  0,   0,   0,  0,  0, // PC_SLEQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SLIQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SLLIQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SLLQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SLQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SRAIQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SRAQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SRE
    BranchUnit,  0,   0,   0,  0,  0, // PC_SREA
    BranchUnit,  0,   0,   0,  0,  0, // PC_SREQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SRIQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SRLIQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SRLQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_SRQ
    BranchUnit,  0,   0,   0,  0,  0, // PC_MASKG
    BranchUnit,  0,   0,   0,  0,  0, // PC_MASKIR
    BranchUnit,  0,   0,   0,  0,  0, // PC_LSCBX
    BranchUnit,  0,   0,   0,  0,  0, // PC_DIV
    BranchUnit,  0,   0,   0,  0,  0, // PC_DIVS
    BranchUnit,  0,   0,   0,  0,  0, // PC_DOZ
    BranchUnit,  0,   0,   0,  0,  0, // PC_MUL
    BranchUnit,  0,   0,   0,  0,  0, // PC_NABS
    BranchUnit,  0,   0,   0,  0,  0, // PC_ABS
    BranchUnit,  0,   0,   0,  0,  0, // PC_CLCS
    BranchUnit,  0,   0,   0,  0,  0, // PC_DOZI
    BranchUnit,  0,   0,   0,  0,  0, // PC_RLMI
    BranchUnit,  0,   0,   0,  0,  0, // PC_RRIB
};
// clang-format on

static void advance(int firstStage, int oldStage, int newStage) {
    PCode* instr = pipeline[oldStage].instr;
    int cycles = instruction_timing[instr->op].cycles[newStage - firstStage];
    pipeline[newStage].instr = instr;
    pipeline[newStage].remaining = cycles;
    pipeline[oldStage].instr = NULL;
}

static void assign_completion_buffer(PCode* instr) {
    completionbuffers.used++;
    completionbuffers.free--;
    completionbuffers.entries[completionbuffers.nextFreeSlot].instr = instr;
    completionbuffers.entries[completionbuffers.nextFreeSlot].completed = FALSE;
    completionbuffers.nextFreeSlot = (completionbuffers.nextFreeSlot + 1) % MaxEntries;
}

static void complete_instruction(int stage) {
    PCode* instr = pipeline[stage].instr;
    int buf = 0;
    while (buf < MaxEntries && completionbuffers.entries[buf].instr != instr) {
        buf++;
    }

    completionbuffers.entries[buf].completed = TRUE;
    pipeline[stage].instr = NULL;
}

static void retire_instruction(void) {
    completionbuffers.entries[completionbuffers.nextToRetire].instr = NULL;
    completionbuffers.used--;
    completionbuffers.free++;
    completionbuffers.nextToRetire = (completionbuffers.nextToRetire + 1) % MaxEntries;
}

static int latency(PCode* instr) {
    int cycles = instruction_timing[instr->op].latency;
    if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
        cycles += 2;
    }
    if (instr->op == PC_LMW || instr->op == PC_STMW) {
        cycles += instr->argCount - 2;
    }
    return cycles;
}

static void initialize(void) {
    int stage;
    int i;

    for (stage = 0; stage < NumStages; stage++) {
        pipeline[stage].instr = NULL;
    }

    completionbuffers.free = MaxEntries;
    completionbuffers.used = 0;
    completionbuffers.nextToRetire = 0;
    completionbuffers.nextFreeSlot = 0;
    for (i = 0; i < MaxEntries; i++) {
        completionbuffers.entries[i].instr = NULL;
    }
}

static int can_issue(PCode* instr) {
    int stage;

    if (completionbuffers.free == 0) {
        return FALSE;
    }

    stage = instruction_timing[instr->op].stage;
#ifdef __MWERKS__
    if (pipeline[stage].instr) {
        return FALSE;
    }
#else
    if (stage == FPUnit ? rTEMP_for_PS_spill || rTEMP_for_VR_spill
                        : pipeline[stage].instr != NULL) {
        return FALSE;
    }
#endif

    if ((instr->flags & fIsWrite) &&
        pipeline[LSU2].instr &&
        (pipeline[LSU2].instr->flags & fIsWrite)) {
        return FALSE;
    }

    return TRUE;
}

static void issue(PCode* instr) {
    int stage = instruction_timing[instr->op].stage;
    int cycles = instruction_timing[instr->op].cycles[0];
    assign_completion_buffer(instr);
#ifdef __MWERKS__
    pipeline[stage].instr = instr;
    pipeline[stage].remaining = cycles;
#else
    if (stage == FPUnit) {
        rTEMP_for_PS_spill = (short)(UInt32)instr;
        rTEMP_for_VR_spill = (short)((UInt32)instr >> 16);
        last_unused_vreg_before_spilling = cycles;
    } else {
        pipeline[stage].instr = instr;
        pipeline[stage].remaining = cycles;
    }
#endif
}

static void advance_clock(void) {
    int stage;

    for (stage = 0; stage < NumStages; stage++) {
        if (pipeline[stage].instr && pipeline[stage].remaining) {
            --pipeline[stage].remaining;
        }
    }

    if (completionbuffers.used &&
        completionbuffers.entries[completionbuffers.nextToRetire].completed) {
        retire_instruction();
        if (completionbuffers.used &&
            completionbuffers.entries[completionbuffers.nextToRetire].completed) {
            retire_instruction();
        }
    }

    if (pipeline[IntegerUnit].instr && pipeline[IntegerUnit].remaining == 0) {
        complete_instruction(IntegerUnit);
    }
    if (pipeline[LSU2].instr && pipeline[LSU2].remaining == 0) {
        complete_instruction(LSU2);
    }
    if (pipeline[BranchUnit].instr && pipeline[BranchUnit].remaining == 0) {
        complete_instruction(BranchUnit);
    }

    if (pipeline[LSU1].instr && pipeline[LSU1].remaining == 0 && !pipeline[LSU2].instr) {
        advance(LSU1, LSU1, LSU2);
    }
}

static int serializes(PCode* instr) {
    return instruction_timing[instr->op].serializes;
}
