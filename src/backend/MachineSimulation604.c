#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Scheduler.h"

/* The model's statics: every machine model has these names, so each prefixes its own */
#define latency m604_latency
#define initialize m604_initialize
#define can_issue m604_can_issue
#define issue m604_issue
#define advance_clock m604_advance_clock
#define serializes m604_serializes

// https://archive.org/details/bitsavers_motorolaPosManualNov94_22719504

typedef enum Stage {
    SCIU, // Single-Cycle Integer Unit 1
    SCIU2, // Single-Cycle Integer Unit 2
    MCIU, // Multiple-Cycle Integer Unit
    FPU1, // Floating Point Unit
    FPU2,
    FPU3,
    LSU1, // Load/Store Unit
    LSU2,
    BPU, // Branch Prediction Unit
    NumStages
} Stage;

static struct {
    // the instruction currently in this pipeline stage
    PCode* instr;

    // how many cycles are left for this instruction to finish
    int remaining;
} pipeline[NumStages];

static PCode* sciu_completed_instruction;
static PCode* sciu2_completed_instruction;

enum {
    MaxEntries = 16
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
MachineInfo machine604 = {
    4,
    TRUE,
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
    BPU,   0,   0,   0,  0,  1, // PC_B
    BPU,   0,   0,   0,  0,  1, // PC_BL
    BPU,   0,   0,   0,  0,  1, // PC_BC
    BPU,   0,   0,   0,  0,  1, // PC_BCLR
    BPU,   0,   0,   0,  0,  1, // PC_BCCTR
    BPU,   0,   0,   0,  0,  1, // PC_BT
    BPU,   0,   0,   0,  0,  1, // PC_BTLR
    BPU,   0,   0,   0,  0,  1, // PC_BTCTR
    BPU,   0,   0,   0,  0,  1, // PC_BF
    BPU,   0,   0,   0,  0,  1, // PC_BFLR
    BPU,   0,   0,   0,  0,  1, // PC_BFCTR
    BPU,   0,   0,   0,  0,  1, // PC_BDNZ
    BPU,   0,   0,   0,  0,  1, // PC_BDNZT
    BPU,   0,   0,   0,  0,  1, // PC_BDNZF
    BPU,   0,   0,   0,  0,  1, // PC_BDZ
    BPU,   0,   0,   0,  0,  1, // PC_BDZT
    BPU,   0,   0,   0,  0,  1, // PC_BDZF
    BPU,   0,   0,   0,  0,  1, // PC_BLR
    BPU,   0,   0,   0,  0,  1, // PC_BCTR
    BPU,   0,   0,   0,  0,  1, // PC_BCTRL
    BPU,   0,   0,   0,  0,  1, // PC_BLRL
    LSU1,  2,   1,   1,  0,  0, // PC_LBZ
    LSU1,  2,   1,   1,  0,  0, // PC_LBZU
    LSU1,  2,   1,   1,  0,  0, // PC_LBZX
    LSU1,  2,   1,   1,  0,  0, // PC_LBZUX
    LSU1,  2,   1,   1,  0,  0, // PC_LHZ
    LSU1,  2,   1,   1,  0,  0, // PC_LHZU
    LSU1,  2,   1,   1,  0,  0, // PC_LHZX
    LSU1,  2,   1,   1,  0,  0, // PC_LHZUX
    LSU1,  2,   1,   1,  0,  0, // PC_LHA
    LSU1,  2,   1,   1,  0,  0, // PC_LHAU
    LSU1,  2,   1,   1,  0,  0, // PC_LHAX
    LSU1,  2,   1,   1,  0,  0, // PC_LHAUX
    LSU1,  2,   1,   1,  0,  0, // PC_LHBRX
    LSU1,  2,   1,   1,  0,  0, // PC_LWZ
    LSU1,  2,   1,   1,  0,  0, // PC_LWZU
    LSU1,  2,   1,   1,  0,  0, // PC_LWZX
    LSU1,  2,   1,   1,  0,  0, // PC_LWZUX
    LSU1,  2,   1,   1,  0,  0, // PC_LWBRX
    LSU1,  2,   1,   1,  0,  0, // PC_LMW
    LSU1,  3,   1,   1,  0,  0, // PC_STB
    LSU1,  3,   1,   1,  0,  0, // PC_STBU
    LSU1,  3,   1,   1,  0,  0, // PC_STBX
    LSU1,  3,   1,   1,  0,  0, // PC_STBUX
    LSU1,  3,   1,   1,  0,  0, // PC_STH
    LSU1,  3,   1,   1,  0,  0, // PC_STHU
    LSU1,  3,   1,   1,  0,  0, // PC_STHX
    LSU1,  3,   1,   1,  0,  0, // PC_STHUX
    LSU1,  3,   1,   1,  0,  0, // PC_STHBRX
    LSU1,  3,   1,   1,  0,  0, // PC_STW
    LSU1,  3,   1,   1,  0,  0, // PC_STWU
    LSU1,  3,   1,   1,  0,  0, // PC_STWX
    LSU1,  3,   1,   1,  0,  0, // PC_STWUX
    LSU1,  3,   1,   1,  0,  0, // PC_STWBRX
    LSU1,  2,   1,   1,  0,  0, // PC_STMW
    LSU1,  2,   1,   1,  0,  0, // PC_DCBF
    LSU1,  2,   1,   1,  0,  0, // PC_DCBST
    LSU1,  2,   1,   1,  0,  0, // PC_DCBT
    LSU1,  2,   1,   1,  0,  0, // PC_DCBTST
    LSU1,  2,   1,   1,  0,  0, // PC_DCBZ
    SCIU,  1,   1,   0,  0,  0, // PC_ADD
    SCIU,  1,   1,   0,  0,  0, // PC_ADDC
    SCIU,  1,   1,   0,  0,  0, // PC_ADDE
    SCIU,  1,   1,   0,  0,  0, // PC_ADDI
    SCIU,  1,   1,   0,  0,  0, // PC_ADDIC
    SCIU,  1,   1,   0,  0,  0, // PC_ADDICR
    SCIU,  1,   1,   0,  0,  0, // PC_ADDIS
    SCIU,  1,   1,   0,  0,  0, // PC_ADDME
    SCIU,  1,   1,   0,  0,  0, // PC_ADDZE
    MCIU,  20,  20,  0,  0,  0, // PC_DIVW
    MCIU,  20,  20,  0,  0,  0, // PC_DIVWU
    MCIU,  4,   4,   0,  0,  0, // PC_MULHW
    MCIU,  4,   4,   0,  0,  0, // PC_MULHWU
    MCIU,  3,   3,   0,  0,  0, // PC_MULLI
    MCIU,  4,   4,   0,  0,  0, // PC_MULLW
    SCIU,  1,   1,   0,  0,  0, // PC_NEG
    SCIU,  1,   1,   0,  0,  0, // PC_SUBF
    SCIU,  1,   1,   0,  0,  0, // PC_SUBFC
    SCIU,  1,   1,   0,  0,  0, // PC_SUBFE
    SCIU,  1,   1,   0,  0,  0, // PC_SUBFIC
    SCIU,  1,   1,   0,  0,  0, // PC_SUBFME
    SCIU,  1,   1,   0,  0,  0, // PC_SUBFZE
    SCIU,  3,   1,   0,  0,  0, // PC_CMPI
    SCIU,  3,   1,   0,  0,  0, // PC_CMP
    SCIU,  3,   1,   0,  0,  0, // PC_CMPLI
    SCIU,  3,   1,   0,  0,  0, // PC_CMPL
    SCIU,  1,   1,   0,  0,  0, // PC_ANDI
    SCIU,  1,   1,   0,  0,  0, // PC_ANDIS
    SCIU,  1,   1,   0,  0,  0, // PC_ORI
    SCIU,  1,   1,   0,  0,  0, // PC_ORIS
    SCIU,  1,   1,   0,  0,  0, // PC_XORI
    SCIU,  1,   1,   0,  0,  0, // PC_XORIS
    SCIU,  1,   1,   0,  0,  0, // PC_AND
    SCIU,  1,   1,   0,  0,  0, // PC_OR
    SCIU,  1,   1,   0,  0,  0, // PC_XOR
    SCIU,  1,   1,   0,  0,  0, // PC_NAND
    SCIU,  1,   1,   0,  0,  0, // PC_NOR
    SCIU,  1,   1,   0,  0,  0, // PC_EQV
    SCIU,  1,   1,   0,  0,  0, // PC_ANDC
    SCIU,  1,   1,   0,  0,  0, // PC_ORC
    SCIU,  1,   1,   0,  0,  0, // PC_EXTSB
    SCIU,  1,   1,   0,  0,  0, // PC_EXTSH
    SCIU,  1,   1,   0,  0,  0, // PC_CNTLZW
    SCIU,  1,   1,   0,  0,  0, // PC_RLWINM
    SCIU,  1,   1,   0,  0,  0, // PC_RLWNM
    SCIU,  1,   1,   0,  0,  0, // PC_RLWIMI
    SCIU,  1,   1,   0,  0,  0, // PC_SLW
    SCIU,  1,   1,   0,  0,  0, // PC_SRW
    SCIU,  1,   1,   0,  0,  0, // PC_SRAWI
    SCIU,  1,   1,   0,  0,  0, // PC_SRAW
    BPU,   1,   1,   0,  0,  0, // PC_CRAND
    BPU,   1,   1,   0,  0,  0, // PC_CRANDC
    BPU,   1,   1,   0,  0,  0, // PC_CREQV
    BPU,   1,   1,   0,  0,  0, // PC_CRNAND
    BPU,   1,   1,   0,  0,  0, // PC_CRNOR
    BPU,   1,   1,   0,  0,  0, // PC_CROR
    BPU,   1,   1,   0,  0,  0, // PC_CRORC
    BPU,   1,   1,   0,  0,  0, // PC_CRXOR
    BPU,   1,   1,   0,  0,  0, // PC_MCRF
    MCIU,  1,   1,   0,  0,  0, // PC_MTXER
    MCIU,  1,   1,   0,  0,  0, // PC_MTCTR
    MCIU,  1,   1,   0,  0,  0, // PC_MTLR
    MCIU,  1,   1,   0,  0,  0, // PC_MTCRF
    MCIU,  1,   1,   0,  0,  0, // PC_MTMSR
    MCIU,  1,   1,   0,  0,  0, // PC_MTSPR
    MCIU,  1,   1,   0,  0,  0, // PC_MFMSR
    MCIU,  1,   1,   0,  0,  0, // PC_MFSPR
    MCIU,  3,   3,   0,  0,  0, // PC_MFXER
    MCIU,  3,   3,   0,  0,  0, // PC_MFCTR
    MCIU,  3,   3,   0,  0,  0, // PC_MFLR
    MCIU,  3,   3,   0,  0,  0, // PC_MFCR
    FPU1,  3,   1,   1,  1,  0, // PC_MFFS
    FPU1,  3,   1,   1,  1,  0, // PC_MTFSF
    LSU1,  1,   0,   0,  0,  1, // PC_EIEIO
    LSU1,  1,   0,   0,  0,  1, // PC_ISYNC
    LSU1,  1,   0,   0,  0,  1, // PC_SYNC
    LSU1,  1,   1,   0,  0,  1, // PC_RFI
    SCIU,  1,   1,   0,  0,  0, // PC_LI
    SCIU,  1,   1,   0,  0,  0, // PC_LIS
    SCIU,  1,   1,   0,  0,  0, // PC_MR
    SCIU,  1,   1,   0,  0,  0, // PC_NOP
    SCIU,  1,   1,   0,  0,  0, // PC_NOT
    LSU1,  3,   1,   1,  0,  0, // PC_LFS
    LSU1,  3,   1,   1,  0,  0, // PC_LFSU
    LSU1,  3,   1,   1,  0,  0, // PC_LFSX
    LSU1,  3,   1,   1,  0,  0, // PC_LFSUX
    LSU1,  3,   1,   1,  0,  0, // PC_LFD
    LSU1,  3,   1,   1,  0,  0, // PC_LFDU
    LSU1,  3,   1,   1,  0,  0, // PC_LFDX
    LSU1,  3,   1,   1,  0,  0, // PC_LFDUX
    LSU1,  3,   1,   1,  0,  0, // PC_STFS
    LSU1,  3,   1,   1,  0,  0, // PC_STFSU
    LSU1,  3,   1,   1,  0,  0, // PC_STFSX
    LSU1,  3,   1,   1,  0,  0, // PC_STFSUX
    LSU1,  3,   1,   1,  0,  0, // PC_STFD
    LSU1,  3,   1,   1,  0,  0, // PC_STFDU
    LSU1,  3,   1,   1,  0,  0, // PC_STFDX
    LSU1,  3,   1,   1,  0,  0, // PC_STFDUX
    FPU1,  3,   1,   1,  1,  0, // PC_FMR
    FPU1,  3,   1,   1,  1,  0, // PC_FABS
    FPU1,  3,   1,   1,  1,  0, // PC_FNEG
    FPU1,  3,   1,   1,  1,  0, // PC_FNABS
    FPU1,  3,   1,   1,  1,  0, // PC_FADD
    FPU1,  3,   1,   1,  1,  0, // PC_FADDS
    FPU1,  3,   1,   1,  1,  0, // PC_FSUB
    FPU1,  3,   1,   1,  1,  0, // PC_FSUBS
    FPU1,  3,   1,   1,  1,  0, // PC_FMUL
    FPU1,  3,   1,   1,  1,  0, // PC_FMULS
    FPU1,  32,  32,  0,  0,  0, // PC_FDIV
    FPU1,  18,  18,  0,  0,  0, // PC_FDIVS
    FPU1,  3,   1,   1,  1,  0, // PC_FMADD
    FPU1,  3,   1,   1,  1,  0, // PC_FMADDS
    FPU1,  3,   1,   1,  1,  0, // PC_FMSUB
    FPU1,  3,   1,   1,  1,  0, // PC_FMSUBS
    FPU1,  3,   1,   1,  1,  0, // PC_FNMADD
    FPU1,  3,   1,   1,  1,  0, // PC_FNMADDS
    FPU1,  3,   1,   1,  1,  0, // PC_FNMSUB
    FPU1,  3,   1,   1,  1,  0, // PC_FNMSUBS
    FPU1,  18,  18,  0,  0,  0, // PC_FRES
    FPU1,  3,   1,   1,  1,  0, // PC_FRSQRTE
    FPU1,  3,   1,   1,  1,  0, // PC_FSEL
    FPU1,  3,   1,   1,  1,  0, // PC_FRSP
    FPU1,  3,   1,   1,  1,  0, // PC_FCTIW
    FPU1,  3,   1,   1,  1,  0, // PC_FCTIWZ
    FPU1,  5,   1,   1,  1,  0, // PC_FCMPU
    FPU1,  5,   1,   1,  1,  0, // PC_FCMPO
    LSU1,  1,   1,   0,  0,  0, // PC_LWARX
    LSU1,  1,   1,   0,  0,  0, // PC_LSWI
    LSU1,  1,   1,   0,  0,  0, // PC_LSWX
    LSU1,  1,   1,   0,  0,  0, // PC_STFIWX
    LSU1,  1,   1,   0,  0,  0, // PC_STSWI
    LSU1,  1,   1,   0,  0,  0, // PC_STSWX
    LSU1,  1,   1,   0,  0,  0, // PC_STWCX
    MCIU,  1,   1,   0,  0,  1, // PC_ECIWX
    MCIU,  1,   1,   0,  0,  1, // PC_ECOWX
    MCIU,  1,   1,   0,  0,  0, // PC_DCBI
    MCIU,  1,   1,   0,  0,  0, // PC_ICBI
    MCIU,  1,   1,   0,  0,  0, // PC_MCRFS
    MCIU,  1,   1,   0,  0,  0, // PC_MCRXR
    MCIU,  1,   1,   0,  0,  0, // PC_MFTB
    MCIU,  1,   1,   0,  0,  0, // PC_MFSR
    MCIU,  1,   1,   0,  0,  0, // PC_MTSR
    MCIU,  1,   1,   0,  0,  0, // PC_MFSRIN
    MCIU,  1,   1,   0,  0,  0, // PC_MTSRIN
    MCIU,  1,   1,   0,  0,  0, // PC_MTFSB0
    MCIU,  1,   1,   0,  0,  0, // PC_MTFSB1
    MCIU,  1,   1,   0,  0,  0, // PC_MTFSFI
    MCIU,  1,   1,   0,  0,  1, // PC_SC
    FPU1,  1,   1,   0,  0,  0, // PC_FSQRT
    FPU1,  1,   1,   0,  0,  0, // PC_FSQRTS
    MCIU,  1,   1,   0,  0,  0, // PC_TLBIA
    MCIU,  1,   1,   0,  0,  0, // PC_TLBIE
    MCIU,  1,   1,   0,  0,  0, // PC_TLBLD
    MCIU,  1,   1,   0,  0,  0, // PC_TLBLI
    MCIU,  1,   1,   0,  0,  0, // PC_TLBSYNC
    MCIU,  1,   1,   0,  0,  1, // PC_TW
    MCIU,  1,   1,   0,  0,  1, // PC_TRAP
    MCIU,  1,   1,   0,  0,  1, // PC_TWI
    MCIU,  1,   1,   0,  0,  1, // PC_OPWORD
    MCIU,  1,   1,   0,  0,  0, // PC_MFROM
    MCIU,  1,   1,   0,  0,  1, // PC_DSA
    MCIU,  1,   1,   0,  0,  1, // PC_ESA
    MCIU,  0,   0,   0,  0,  0, // PC_DCCCI
    MCIU,  0,   0,   0,  0,  0, // PC_DCREAD
    MCIU,  0,   0,   0,  0,  0, // PC_ICBT
    MCIU,  0,   0,   0,  0,  0, // PC_ICCCI
    MCIU,  0,   0,   0,  0,  0, // PC_ICREAD
    MCIU,  0,   0,   0,  0,  0, // PC_RFCI
    MCIU,  0,   0,   0,  0,  0, // PC_TLBRE
    MCIU,  0,   0,   0,  0,  0, // PC_TLBSX
    MCIU,  0,   0,   0,  0,  0, // PC_TLBWE
    MCIU,  0,   0,   0,  0,  0, // PC_WRTEE
    MCIU,  0,   0,   0,  0,  0, // PC_WRTEEI
    MCIU,  0,   0,   0,  0,  0, // PC_MFDCR
    MCIU,  0,   0,   0,  0,  0, // PC_MTDCR
    MCIU,  0,   0,   0,  0,  0, // PC_DCBA
    SCIU,  0,   0,   0,  0,  0, // PC_ISELGT
    SCIU,  0,   0,   0,  0,  0, // PC_ISELEQ
    SCIU,  0,   0,   0,  0,  0, // PC_ISELLT
    SCIU,  0,   0,   0,  0,  0, // PC_DSS
    SCIU,  0,   0,   0,  0,  0, // PC_DSSALL
    SCIU,  0,   0,   0,  0,  0, // PC_DST
    SCIU,  0,   0,   0,  0,  0, // PC_DSTT
    SCIU,  0,   0,   0,  0,  0, // PC_DSTST
    SCIU,  0,   0,   0,  0,  0, // PC_DSTSTT
    SCIU,  0,   0,   0,  0,  0, // PC_LVEBX
    SCIU,  0,   0,   0,  0,  0, // PC_LVEHX
    SCIU,  0,   0,   0,  0,  0, // PC_LVEWX
    SCIU,  0,   0,   0,  0,  0, // PC_LVSL
    SCIU,  0,   0,   0,  0,  0, // PC_LVSR
    SCIU,  0,   0,   0,  0,  0, // PC_LVX
    SCIU,  0,   0,   0,  0,  0, // PC_LVXL
    SCIU,  0,   0,   0,  0,  0, // PC_STVEBX
    SCIU,  0,   0,   0,  0,  0, // PC_STVEHX
    SCIU,  0,   0,   0,  0,  0, // PC_STVEWX
    SCIU,  0,   0,   0,  0,  0, // PC_STVX
    SCIU,  0,   0,   0,  0,  0, // PC_STVXL
    SCIU,  0,   0,   0,  0,  0, // PC_MFVSCR
    SCIU,  0,   0,   0,  0,  0, // PC_MTVSCR
    SCIU,  0,   0,   0,  0,  0, // PC_VADDCUW
    SCIU,  0,   0,   0,  0,  0, // PC_VADDFP
    SCIU,  0,   0,   0,  0,  0, // PC_VADDSBS
    SCIU,  0,   0,   0,  0,  0, // PC_VADDSHS
    SCIU,  0,   0,   0,  0,  0, // PC_VADDSWS
    SCIU,  0,   0,   0,  0,  0, // PC_VADDUBM
    SCIU,  0,   0,   0,  0,  0, // PC_VADDUBS
    SCIU,  0,   0,   0,  0,  0, // PC_VADDUHM
    SCIU,  0,   0,   0,  0,  0, // PC_VADDUHS
    SCIU,  0,   0,   0,  0,  0, // PC_VADDUWM
    SCIU,  0,   0,   0,  0,  0, // PC_VADDUWS
    SCIU,  0,   0,   0,  0,  0, // PC_VAND
    SCIU,  0,   0,   0,  0,  0, // PC_VANDC
    SCIU,  0,   0,   0,  0,  0, // PC_VAVGSB
    SCIU,  0,   0,   0,  0,  0, // PC_VAVGSH
    SCIU,  0,   0,   0,  0,  0, // PC_VAVGSW
    SCIU,  0,   0,   0,  0,  0, // PC_VAVGUB
    SCIU,  0,   0,   0,  0,  0, // PC_VAVGUH
    SCIU,  0,   0,   0,  0,  0, // PC_VAVGUW
    SCIU,  0,   0,   0,  0,  0, // PC_VCFSX
    SCIU,  0,   0,   0,  0,  0, // PC_VCFUX
    SCIU,  0,   0,   0,  0,  0, // PC_VCMPBFP
    SCIU,  0,   0,   0,  0,  0, // PC_VCMPEQFP
    SCIU,  0,   0,   0,  0,  0, // PC_VCMPEQUB
    SCIU,  0,   0,   0,  0,  0, // PC_VCMPEQUH
    SCIU,  0,   0,   0,  0,  0, // PC_VCMPEQUW
    SCIU,  0,   0,   0,  0,  0, // PC_VCMPGEFP
    SCIU,  0,   0,   0,  0,  0, // PC_VCMPGTFP
    SCIU,  0,   0,   0,  0,  0, // PC_VCMPGTSB
    SCIU,  0,   0,   0,  0,  0, // PC_VCMPGTSH
    SCIU,  0,   0,   0,  0,  0, // PC_VCMPGTSW
    SCIU,  0,   0,   0,  0,  0, // PC_VCMPGTUB
    SCIU,  0,   0,   0,  0,  0, // PC_VCMPGTUH
    SCIU,  0,   0,   0,  0,  0, // PC_VCMPGTUW
    SCIU,  0,   0,   0,  0,  0, // PC_VCTSXS
    SCIU,  0,   0,   0,  0,  0, // PC_VCTUXS
    SCIU,  0,   0,   0,  0,  0, // PC_VEXPTEFP
    SCIU,  0,   0,   0,  0,  0, // PC_VLOGEFP
    SCIU,  0,   0,   0,  0,  0, // PC_VMAXFP
    SCIU,  0,   0,   0,  0,  0, // PC_VMAXSB
    SCIU,  0,   0,   0,  0,  0, // PC_VMAXSH
    SCIU,  0,   0,   0,  0,  0, // PC_VMAXSW
    SCIU,  0,   0,   0,  0,  0, // PC_VMAXUB
    SCIU,  0,   0,   0,  0,  0, // PC_VMAXUH
    SCIU,  0,   0,   0,  0,  0, // PC_VMAXUW
    SCIU,  0,   0,   0,  0,  0, // PC_VMINFP
    SCIU,  0,   0,   0,  0,  0, // PC_VMINSB
    SCIU,  0,   0,   0,  0,  0, // PC_VMINSH
    SCIU,  0,   0,   0,  0,  0, // PC_VMINSW
    SCIU,  0,   0,   0,  0,  0, // PC_VMINUB
    SCIU,  0,   0,   0,  0,  0, // PC_VMINUH
    SCIU,  0,   0,   0,  0,  0, // PC_VMINUW
    SCIU,  0,   0,   0,  0,  0, // PC_VMRGHB
    SCIU,  0,   0,   0,  0,  0, // PC_VMRGHH
    SCIU,  0,   0,   0,  0,  0, // PC_VMRGHW
    SCIU,  0,   0,   0,  0,  0, // PC_VMRGLB
    SCIU,  0,   0,   0,  0,  0, // PC_VMRGLH
    SCIU,  0,   0,   0,  0,  0, // PC_VMRGLW
    SCIU,  0,   0,   0,  0,  0, // PC_VMULESB
    SCIU,  0,   0,   0,  0,  0, // PC_VMULESH
    SCIU,  0,   0,   0,  0,  0, // PC_VMULEUB
    SCIU,  0,   0,   0,  0,  0, // PC_VMULEUH
    SCIU,  0,   0,   0,  0,  0, // PC_VMULOSB
    SCIU,  0,   0,   0,  0,  0, // PC_VMULOSH
    SCIU,  0,   0,   0,  0,  0, // PC_VMULOUB
    SCIU,  0,   0,   0,  0,  0, // PC_VMULOUH
    SCIU,  0,   0,   0,  0,  0, // PC_VNOR
    SCIU,  0,   0,   0,  0,  0, // PC_VOR
    SCIU,  0,   0,   0,  0,  0, // PC_VPKPX
    SCIU,  0,   0,   0,  0,  0, // PC_VPKSHSS
    SCIU,  0,   0,   0,  0,  0, // PC_VPKSHUS
    SCIU,  0,   0,   0,  0,  0, // PC_VPKSWSS
    SCIU,  0,   0,   0,  0,  0, // PC_VPKSWUS
    SCIU,  0,   0,   0,  0,  0, // PC_VPKUHUM
    SCIU,  0,   0,   0,  0,  0, // PC_VPKUHUS
    SCIU,  0,   0,   0,  0,  0, // PC_VPKUWUM
    SCIU,  0,   0,   0,  0,  0, // PC_VPKUWUS
    SCIU,  0,   0,   0,  0,  0, // PC_VREFP
    SCIU,  0,   0,   0,  0,  0, // PC_VRFIM
    SCIU,  0,   0,   0,  0,  0, // PC_VRFIN
    SCIU,  0,   0,   0,  0,  0, // PC_VRFIP
    SCIU,  0,   0,   0,  0,  0, // PC_VRFIZ
    SCIU,  0,   0,   0,  0,  0, // PC_VRLB
    SCIU,  0,   0,   0,  0,  0, // PC_VRLH
    SCIU,  0,   0,   0,  0,  0, // PC_VRLW
    SCIU,  0,   0,   0,  0,  0, // PC_VRSQRTEFP
    SCIU,  0,   0,   0,  0,  0, // PC_VSL
    SCIU,  0,   0,   0,  0,  0, // PC_VSLB
    SCIU,  0,   0,   0,  0,  0, // PC_VSLH
    SCIU,  0,   0,   0,  0,  0, // PC_VSLO
    SCIU,  0,   0,   0,  0,  0, // PC_VSLW
    SCIU,  0,   0,   0,  0,  0, // PC_VSPLTB
    SCIU,  0,   0,   0,  0,  0, // PC_VSPLTH
    SCIU,  0,   0,   0,  0,  0, // PC_VSPLTW
    SCIU,  0,   0,   0,  0,  0, // PC_VSPLTISB
    SCIU,  0,   0,   0,  0,  0, // PC_VSPLTISH
    SCIU,  0,   0,   0,  0,  0, // PC_VSPLTISW
    SCIU,  0,   0,   0,  0,  0, // PC_VSR
    SCIU,  0,   0,   0,  0,  0, // PC_VSRAB
    SCIU,  0,   0,   0,  0,  0, // PC_VSRAH
    SCIU,  0,   0,   0,  0,  0, // PC_VSRAW
    SCIU,  0,   0,   0,  0,  0, // PC_VSRB
    SCIU,  0,   0,   0,  0,  0, // PC_VSRH
    SCIU,  0,   0,   0,  0,  0, // PC_VSRO
    SCIU,  0,   0,   0,  0,  0, // PC_VSRW
    SCIU,  0,   0,   0,  0,  0, // PC_VSUBCUW
    SCIU,  0,   0,   0,  0,  0, // PC_VSUBFP
    SCIU,  0,   0,   0,  0,  0, // PC_VSUBSBS
    SCIU,  0,   0,   0,  0,  0, // PC_VSUBSHS
    SCIU,  0,   0,   0,  0,  0, // PC_VSUBSWS
    SCIU,  0,   0,   0,  0,  0, // PC_VSUBUBM
    SCIU,  0,   0,   0,  0,  0, // PC_VSUBUBS
    SCIU,  0,   0,   0,  0,  0, // PC_VSUBUHM
    SCIU,  0,   0,   0,  0,  0, // PC_VSUBUHS
    SCIU,  0,   0,   0,  0,  0, // PC_VSUBUWM
    SCIU,  0,   0,   0,  0,  0, // PC_VSUBUWS
    SCIU,  0,   0,   0,  0,  0, // PC_VSUMSWS
    SCIU,  0,   0,   0,  0,  0, // PC_VSUM2SWS
    SCIU,  0,   0,   0,  0,  0, // PC_VSUM4SBS
    SCIU,  0,   0,   0,  0,  0, // PC_VSUM4SHS
    SCIU,  0,   0,   0,  0,  0, // PC_VSUM4UBS
    SCIU,  0,   0,   0,  0,  0, // PC_VUPKHPX
    SCIU,  0,   0,   0,  0,  0, // PC_VUPKHSB
    SCIU,  0,   0,   0,  0,  0, // PC_VUPKHSH
    SCIU,  0,   0,   0,  0,  0, // PC_VUPKLPX
    SCIU,  0,   0,   0,  0,  0, // PC_VUPKLSB
    SCIU,  0,   0,   0,  0,  0, // PC_VUPKLSH
    SCIU,  0,   0,   0,  0,  0, // PC_VXOR
    SCIU,  0,   0,   0,  0,  0, // PC_VMADDFP
    SCIU,  0,   0,   0,  0,  0, // PC_VMHADDSHS
    SCIU,  0,   0,   0,  0,  0, // PC_VMHRADDSHS
    SCIU,  0,   0,   0,  0,  0, // PC_VMLADDUHM
    SCIU,  0,   0,   0,  0,  0, // PC_VMSUMMBM
    SCIU,  0,   0,   0,  0,  0, // PC_VMSUMSHM
    SCIU,  0,   0,   0,  0,  0, // PC_VMSUMSHS
    SCIU,  0,   0,   0,  0,  0, // PC_VMSUMUBM
    SCIU,  0,   0,   0,  0,  0, // PC_VMSUMUHM
    SCIU,  0,   0,   0,  0,  0, // PC_VMSUMUHS
    SCIU,  0,   0,   0,  0,  0, // PC_VNMSUBFP
    SCIU,  0,   0,   0,  0,  0, // PC_VPERM
    SCIU,  0,   0,   0,  0,  0, // PC_VSEL
    SCIU,  0,   0,   0,  0,  0, // PC_VSLDOI
    SCIU,  0,   0,   0,  0,  0, // PC_VMR
    SCIU,  0,   0,   0,  0,  0, // PC_VMRP
    SCIU,  0,   0,   0,  0,  0, // PC_PSQ_L
    SCIU,  0,   0,   0,  0,  0, // PC_PSQ_LU
    SCIU,  0,   0,   0,  0,  0, // PC_PSQ_LX
    SCIU,  0,   0,   0,  0,  0, // PC_PSQ_LUX
    SCIU,  0,   0,   0,  0,  0, // PC_PSQ_ST
    SCIU,  0,   0,   0,  0,  0, // PC_PSQ_STU
    SCIU,  0,   0,   0,  0,  0, // PC_PSQ_STX
    SCIU,  0,   0,   0,  0,  0, // PC_PSQ_STUX
    SCIU,  0,   0,   0,  0,  0, // PC_DCBZ_L
    SCIU,  0,   0,   0,  0,  0, // PC_PS_ADD
    SCIU,  0,   0,   0,  0,  0, // PC_PS_SUB
    SCIU,  0,   0,   0,  0,  0, // PC_PS_MUL
    SCIU,  0,   0,   0,  0,  0, // PC_PS_DIV
    SCIU,  0,   0,   0,  0,  0, // PC_PS_MADD
    SCIU,  0,   0,   0,  0,  0, // PC_PS_MSUB
    SCIU,  0,   0,   0,  0,  0, // PC_PS_NMADD
    SCIU,  0,   0,   0,  0,  0, // PC_PS_NMSUB
    SCIU,  0,   0,   0,  0,  0, // PC_PS_RES
    SCIU,  0,   0,   0,  0,  0, // PC_PS_SEL
    SCIU,  0,   0,   0,  0,  0, // PC_PS_ABS
    SCIU,  0,   0,   0,  0,  0, // PC_PS_NABS
    SCIU,  0,   0,   0,  0,  0, // PC_PS_NEG
    SCIU,  0,   0,   0,  0,  0, // PC_PS_MR
    SCIU,  0,   0,   0,  0,  0, // PC_PS_RSQRTE
    SCIU,  0,   0,   0,  0,  0, // PC_PS_CMPU0
    SCIU,  0,   0,   0,  0,  0, // PC_PS_CMPO0
    SCIU,  0,   0,   0,  0,  0, // PC_PS_CMPU1
    SCIU,  0,   0,   0,  0,  0, // PC_PS_CMPO1
    SCIU,  0,   0,   0,  0,  0, // PC_PS_MERGE00
    SCIU,  0,   0,   0,  0,  0, // PC_PS_MERGE01
    SCIU,  0,   0,   0,  0,  0, // PC_PS_MERGE10
    SCIU,  0,   0,   0,  0,  0, // PC_PS_MERGE11
    SCIU,  0,   0,   0,  0,  0, // PC_PS_SUM0
    SCIU,  0,   0,   0,  0,  0, // PC_PS_SUM1
    SCIU,  0,   0,   0,  0,  0, // PC_PS_MULS0
    SCIU,  0,   0,   0,  0,  0, // PC_PS_MULS1
    SCIU,  0,   0,   0,  0,  0, // PC_PS_MADDS0
    SCIU,  0,   0,   0,  0,  0, // PC_PS_MADDS1
    SCIU,  0,   0,   0,  0,  0, // PC_SLE
    SCIU,  0,   0,   0,  0,  0, // PC_SLEQ
    SCIU,  0,   0,   0,  0,  0, // PC_SLIQ
    SCIU,  0,   0,   0,  0,  0, // PC_SLLIQ
    SCIU,  0,   0,   0,  0,  0, // PC_SLLQ
    SCIU,  0,   0,   0,  0,  0, // PC_SLQ
    SCIU,  0,   0,   0,  0,  0, // PC_SRAIQ
    SCIU,  0,   0,   0,  0,  0, // PC_SRAQ
    SCIU,  0,   0,   0,  0,  0, // PC_SRE
    SCIU,  0,   0,   0,  0,  0, // PC_SREA
    SCIU,  0,   0,   0,  0,  0, // PC_SREQ
    SCIU,  0,   0,   0,  0,  0, // PC_SRIQ
    SCIU,  0,   0,   0,  0,  0, // PC_SRLIQ
    SCIU,  0,   0,   0,  0,  0, // PC_SRLQ
    SCIU,  0,   0,   0,  0,  0, // PC_SRQ
    SCIU,  0,   0,   0,  0,  0, // PC_MASKG
    SCIU,  0,   0,   0,  0,  0, // PC_MASKIR
    SCIU,  0,   0,   0,  0,  0, // PC_LSCBX
    SCIU,  0,   0,   0,  0,  0, // PC_DIV
    SCIU,  0,   0,   0,  0,  0, // PC_DIVS
    SCIU,  0,   0,   0,  0,  0, // PC_DOZ
    SCIU,  0,   0,   0,  0,  0, // PC_MUL
    SCIU,  0,   0,   0,  0,  0, // PC_NABS
    SCIU,  0,   0,   0,  0,  0, // PC_ABS
    SCIU,  0,   0,   0,  0,  0, // PC_CLCS
    SCIU,  0,   0,   0,  0,  0, // PC_DOZI
    SCIU,  0,   0,   0,  0,  0, // PC_RLMI
    SCIU,  0,   0,   0,  0,  0, // PC_RRIB
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

    if (stage == SCIU) {
        sciu_completed_instruction = instr;
    } else if (stage == SCIU2) {
        sciu2_completed_instruction = instr;
    }
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

    sciu_completed_instruction = NULL;
    sciu2_completed_instruction = NULL;
}

static int can_issue(PCode* instr) {
    PCode* check;
    int stage = instruction_timing[instr->op].stage;

    if (completionbuffers.free == 0) {
        return FALSE;
    }

    if (stage == SCIU) {
        int isClear1 = !pipeline[SCIU].instr;
        int isClear2 = !pipeline[SCIU2].instr;
        if (!isClear1 && !isClear2) {
            return FALSE;
        }
        if (isClear1 && isClear2) {
            return TRUE;
        }

        if (isClear1) {
            check = pipeline[SCIU2].instr;
        } else {
            check = pipeline[SCIU].instr;
        }

        if (is_dependent(instr, check, RegClass_GPR)) {
            return FALSE;
        }
        if (is_dependent(instr, sciu_completed_instruction, RegClass_GPR)) {
            return FALSE;
        }
        if (is_dependent(instr, sciu2_completed_instruction, RegClass_GPR)) {
            return FALSE;
        }
    } else {
        if (pipeline[stage].instr) {
            return FALSE;
        }
    }

    return TRUE;
}

static void issue(PCode* instr) {
    int stage = instruction_timing[instr->op].stage;
    int cycles = instruction_timing[instr->op].cycles[0];
    if (stage == SCIU && pipeline[SCIU].instr) {
        stage = SCIU2;
    }
    assign_completion_buffer(instr);
    pipeline[stage].instr = instr;
    pipeline[stage].remaining = cycles;
}

static void advance_clock(void) {
    int stage;

    sciu_completed_instruction = NULL;
    sciu2_completed_instruction = NULL;

    for (stage = 0; stage < NumStages; stage++) {
        if (pipeline[stage].instr && pipeline[stage].remaining) {
            --pipeline[stage].remaining;
        }
    }

    // retire up to five instructions a cycle (`stage` doubles as the count)
    for (stage = 0; stage < 5; stage++) {
        if (completionbuffers.used == 0) {
            break;
        }
        if (completionbuffers.entries[completionbuffers.nextToRetire].completed == FALSE) {
            break;
        }
        retire_instruction();
    }

    if (pipeline[SCIU].instr && pipeline[SCIU].remaining == 0) {
        complete_instruction(SCIU);
    }
    if (pipeline[SCIU2].instr && pipeline[SCIU2].remaining == 0) {
        complete_instruction(SCIU2);
    }
    if (pipeline[MCIU].instr && pipeline[MCIU].remaining == 0) {
        complete_instruction(MCIU);
    }
    if (pipeline[LSU2].instr && pipeline[LSU2].remaining == 0) {
        complete_instruction(LSU2);
    }
    if (pipeline[FPU3].instr && pipeline[FPU3].remaining == 0) {
        complete_instruction(FPU3);
    }
    if (pipeline[BPU].instr && pipeline[BPU].remaining == 0) {
        complete_instruction(BPU);
    }

    if (pipeline[FPU1].instr &&
        pipeline[FPU1].remaining == 0 &&
        (pipeline[FPU1].instr->op == PC_FDIV || pipeline[FPU1].instr->op == PC_FDIVS)) {
        complete_instruction(FPU1);
    }

    if (pipeline[FPU2].instr && pipeline[FPU2].remaining == 0 && !pipeline[FPU3].instr) {
        advance(FPU1, FPU2, FPU3);
    }
    if (pipeline[FPU1].instr && pipeline[FPU1].remaining == 0 && !pipeline[FPU2].instr) {
        advance(FPU1, FPU1, FPU2);
    }
    if (pipeline[LSU1].instr && pipeline[LSU1].remaining == 0 && !pipeline[LSU2].instr) {
        advance(LSU1, LSU1, LSU2);
    }
}

static int serializes(PCode* instr) {
    return instruction_timing[instr->op].serializes;
}
