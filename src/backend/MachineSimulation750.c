#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Scheduler.h"

/* The model's statics: every machine model has these names, so each prefixes its own */
#define latency m750_latency
#define initialize m750_initialize
#define can_issue m750_can_issue
#define issue m750_issue
#define advance_clock m750_advance_clock
#define serializes m750_serializes

// https://www.nxp.com/docs/en/reference-manual/MPC750UM.pdf

typedef enum Stage {
    BPU, // Branch Prediction Unit
    IU1, // Integer Unit 1
    IU2, // Integer Unit 2

    LSU1, // Load/Store Unit
    LSU2,

    FPU1, // Floating Point Unit
    FPU2,
    FPU3,

    SRU, // System Register Unit

    NumStages
} Stage;

static struct {
    // the instruction currently in this pipeline stage
    PCode* instr;

    // how many cycles are left for this instruction to finish
    int remaining;
} pipeline[NumStages];

static PCode* iu1_completed_instruction;
static PCode* iu2_completed_instruction;

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
MachineInfo machine750 = {
    2,
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
    LSU1,  2,   1,   1,  0,  0, // PC_STB
    LSU1,  2,   1,   1,  0,  0, // PC_STBU
    LSU1,  2,   1,   1,  0,  0, // PC_STBX
    LSU1,  2,   1,   1,  0,  0, // PC_STBUX
    LSU1,  2,   1,   1,  0,  0, // PC_STH
    LSU1,  2,   1,   1,  0,  0, // PC_STHU
    LSU1,  2,   1,   1,  0,  0, // PC_STHX
    LSU1,  2,   1,   1,  0,  0, // PC_STHUX
    LSU1,  2,   1,   1,  0,  0, // PC_STHBRX
    LSU1,  2,   1,   1,  0,  0, // PC_STW
    LSU1,  2,   1,   1,  0,  0, // PC_STWU
    LSU1,  2,   1,   1,  0,  0, // PC_STWX
    LSU1,  2,   1,   1,  0,  0, // PC_STWUX
    LSU1,  2,   1,   1,  0,  0, // PC_STWBRX
    LSU1,  2,   1,   1,  0,  0, // PC_STMW
    LSU1,  3,   1,   2,  0,  0, // PC_DCBF
    LSU1,  3,   1,   2,  0,  0, // PC_DCBST
    LSU1,  2,   1,   1,  0,  0, // PC_DCBT
    LSU1,  2,   1,   1,  0,  0, // PC_DCBTST
    LSU1,  3,   1,   2,  0,  0, // PC_DCBZ
    IU2,   1,   1,   0,  0,  0, // PC_ADD
    IU2,   1,   1,   0,  0,  0, // PC_ADDC
    IU2,   1,   1,   0,  0,  0, // PC_ADDE
    IU2,   1,   1,   0,  0,  0, // PC_ADDI
    IU2,   1,   1,   0,  0,  0, // PC_ADDIC
    IU2,   1,   1,   0,  0,  0, // PC_ADDICR
    IU2,   1,   1,   0,  0,  0, // PC_ADDIS
    IU2,   1,   1,   0,  0,  0, // PC_ADDME
    IU2,   1,   1,   0,  0,  0, // PC_ADDZE
    IU1,   19,  19,  0,  0,  0, // PC_DIVW
    IU1,   19,  19,  0,  0,  0, // PC_DIVWU
    IU1,   5,   5,   0,  0,  0, // PC_MULHW
    IU1,   6,   5,   0,  0,  0, // PC_MULHWU
    IU1,   3,   3,   0,  0,  0, // PC_MULLI
    IU1,   5,   5,   0,  0,  0, // PC_MULLW
    IU2,   1,   1,   0,  0,  0, // PC_NEG
    IU2,   1,   1,   0,  0,  0, // PC_SUBF
    IU2,   1,   1,   0,  0,  0, // PC_SUBFC
    IU2,   1,   1,   0,  0,  0, // PC_SUBFE
    IU2,   1,   1,   0,  0,  0, // PC_SUBFIC
    IU2,   1,   1,   0,  0,  0, // PC_SUBFME
    IU2,   1,   1,   0,  0,  0, // PC_SUBFZE
    IU2,   3,   1,   0,  0,  0, // PC_CMPI
    IU2,   3,   1,   0,  0,  0, // PC_CMP
    IU2,   3,   1,   0,  0,  0, // PC_CMPLI
    IU2,   3,   1,   0,  0,  0, // PC_CMPL
    IU2,   1,   1,   0,  0,  0, // PC_ANDI
    IU2,   1,   1,   0,  0,  0, // PC_ANDIS
    IU2,   1,   1,   0,  0,  0, // PC_ORI
    IU2,   1,   1,   0,  0,  0, // PC_ORIS
    IU2,   1,   1,   0,  0,  0, // PC_XORI
    IU2,   1,   1,   0,  0,  0, // PC_XORIS
    IU2,   1,   1,   0,  0,  0, // PC_AND
    IU2,   1,   1,   0,  0,  0, // PC_OR
    IU2,   1,   1,   0,  0,  0, // PC_XOR
    IU2,   1,   1,   0,  0,  0, // PC_NAND
    IU2,   1,   1,   0,  0,  0, // PC_NOR
    IU2,   1,   1,   0,  0,  0, // PC_EQV
    IU2,   1,   1,   0,  0,  0, // PC_ANDC
    IU2,   1,   1,   0,  0,  0, // PC_ORC
    IU2,   1,   1,   0,  0,  0, // PC_EXTSB
    IU2,   1,   1,   0,  0,  0, // PC_EXTSH
    IU2,   1,   1,   0,  0,  0, // PC_CNTLZW
    IU2,   1,   1,   0,  0,  0, // PC_RLWINM
    IU2,   1,   1,   0,  0,  0, // PC_RLWNM
    IU2,   1,   1,   0,  0,  0, // PC_RLWIMI
    IU2,   1,   1,   0,  0,  0, // PC_SLW
    IU2,   1,   1,   0,  0,  0, // PC_SRW
    IU2,   1,   1,   0,  0,  0, // PC_SRAWI
    IU2,   1,   1,   0,  0,  0, // PC_SRAW
    SRU,   1,   1,   0,  0,  1, // PC_CRAND
    SRU,   1,   1,   0,  0,  1, // PC_CRANDC
    SRU,   1,   1,   0,  0,  1, // PC_CREQV
    SRU,   1,   1,   0,  0,  1, // PC_CRNAND
    SRU,   1,   1,   0,  0,  1, // PC_CRNOR
    SRU,   1,   1,   0,  0,  1, // PC_CROR
    SRU,   1,   1,   0,  0,  1, // PC_CRORC
    SRU,   1,   1,   0,  0,  1, // PC_CRXOR
    SRU,   1,   1,   0,  0,  1, // PC_MCRF
    SRU,   2,   2,   0,  0,  1, // PC_MTXER
    SRU,   2,   2,   0,  0,  1, // PC_MTCTR
    SRU,   2,   2,   0,  0,  1, // PC_MTLR
    SRU,   1,   1,   0,  0,  1, // PC_MTCRF
    SRU,   1,   1,   0,  0,  0, // PC_MTMSR
    SRU,   1,   1,   0,  0,  1, // PC_MTSPR
    SRU,   1,   1,   0,  0,  1, // PC_MFMSR
    SRU,   1,   1,   0,  0,  1, // PC_MFSPR
    SRU,   1,   1,   0,  0,  1, // PC_MFXER
    SRU,   1,   1,   0,  0,  1, // PC_MFCTR
    SRU,   1,   1,   0,  0,  1, // PC_MFLR
    SRU,   1,   1,   0,  0,  1, // PC_MFCR
    FPU1,  3,   1,   1,  1,  0, // PC_MFFS
    FPU1,  3,   1,   1,  1,  0, // PC_MTFSF
    SRU,   1,   1,   0,  0,  1, // PC_EIEIO
    SRU,   2,   2,   0,  0,  1, // PC_ISYNC
    SRU,   3,   3,   0,  0,  1, // PC_SYNC
    SRU,   1,   1,   0,  0,  1, // PC_RFI
    IU2,   1,   1,   0,  0,  0, // PC_LI
    IU2,   1,   1,   0,  0,  0, // PC_LIS
    IU2,   1,   1,   0,  0,  0, // PC_MR
    IU2,   1,   1,   0,  0,  0, // PC_NOP
    IU2,   1,   1,   0,  0,  0, // PC_NOT
    LSU1,  2,   1,   1,  0,  0, // PC_LFS
    LSU1,  2,   1,   1,  0,  0, // PC_LFSU
    LSU1,  2,   1,   1,  0,  0, // PC_LFSX
    LSU1,  2,   1,   1,  0,  0, // PC_LFSUX
    LSU1,  2,   1,   1,  0,  0, // PC_LFD
    LSU1,  2,   1,   1,  0,  0, // PC_LFDU
    LSU1,  2,   1,   1,  0,  0, // PC_LFDX
    LSU1,  2,   1,   1,  0,  0, // PC_LFDUX
    LSU1,  2,   1,   1,  0,  0, // PC_STFS
    LSU1,  2,   1,   1,  0,  0, // PC_STFSU
    LSU1,  2,   1,   1,  0,  0, // PC_STFSX
    LSU1,  2,   1,   1,  0,  0, // PC_STFSUX
    LSU1,  2,   1,   1,  0,  0, // PC_STFD
    LSU1,  2,   1,   1,  0,  0, // PC_STFDU
    LSU1,  2,   1,   1,  0,  0, // PC_STFDX
    LSU1,  2,   1,   1,  0,  0, // PC_STFDUX
    FPU1,  3,   1,   1,  1,  0, // PC_FMR
    FPU1,  3,   1,   1,  1,  0, // PC_FABS
    FPU1,  3,   1,   1,  1,  0, // PC_FNEG
    FPU1,  3,   1,   1,  1,  0, // PC_FNABS
    FPU1,  3,   1,   1,  1,  0, // PC_FADD
    FPU1,  3,   1,   1,  1,  0, // PC_FADDS
    FPU1,  3,   1,   1,  1,  0, // PC_FSUB
    FPU1,  3,   1,   1,  1,  0, // PC_FSUBS
    FPU1,  4,   2,   1,  1,  0, // PC_FMUL
    FPU1,  3,   1,   1,  1,  0, // PC_FMULS
    FPU1,  31,  31,  0,  0,  0, // PC_FDIV
    FPU1,  17,  17,  0,  0,  0, // PC_FDIVS
    FPU1,  4,   2,   1,  1,  0, // PC_FMADD
    FPU1,  3,   1,   1,  1,  0, // PC_FMADDS
    FPU1,  4,   2,   1,  1,  0, // PC_FMSUB
    FPU1,  3,   1,   1,  1,  0, // PC_FMSUBS
    FPU1,  4,   2,   1,  1,  0, // PC_FNMADD
    FPU1,  3,   1,   1,  1,  0, // PC_FNMADDS
    FPU1,  4,   2,   1,  1,  0, // PC_FNMSUB
    FPU1,  3,   1,   1,  1,  0, // PC_FNMSUBS
    FPU1,  10,  10,  0,  0,  0, // PC_FRES
    FPU1,  3,   1,   1,  1,  0, // PC_FRSQRTE
    FPU1,  3,   1,   1,  1,  0, // PC_FSEL
    FPU1,  3,   1,   1,  1,  0, // PC_FRSP
    FPU1,  3,   1,   1,  1,  0, // PC_FCTIW
    FPU1,  3,   1,   1,  1,  0, // PC_FCTIWZ
    FPU1,  3,   1,   1,  1,  0, // PC_FCMPU
    FPU1,  3,   1,   1,  1,  0, // PC_FCMPO
    LSU1,  1,   1,   0,  0,  0, // PC_LWARX
    LSU1,  1,   1,   0,  0,  0, // PC_LSWI
    LSU1,  1,   1,   0,  0,  0, // PC_LSWX
    LSU1,  1,   1,   0,  0,  0, // PC_STFIWX
    LSU1,  1,   1,   0,  0,  0, // PC_STSWI
    LSU1,  1,   1,   0,  0,  0, // PC_STSWX
    LSU1,  1,   1,   0,  0,  0, // PC_STWCX
    IU1,   1,   1,   0,  0,  1, // PC_ECIWX
    IU1,   1,   1,   0,  0,  1, // PC_ECOWX
    IU1,   1,   1,   0,  0,  0, // PC_DCBI
    IU1,   1,   1,   0,  0,  0, // PC_ICBI
    IU1,   1,   1,   0,  0,  0, // PC_MCRFS
    IU1,   1,   1,   0,  0,  0, // PC_MCRXR
    IU1,   1,   1,   0,  0,  0, // PC_MFTB
    IU1,   1,   1,   0,  0,  0, // PC_MFSR
    IU1,   1,   1,   0,  0,  0, // PC_MTSR
    IU1,   1,   1,   0,  0,  0, // PC_MFSRIN
    IU1,   1,   1,   0,  0,  0, // PC_MTSRIN
    IU1,   1,   1,   0,  0,  0, // PC_MTFSB0
    IU1,   1,   1,   0,  0,  0, // PC_MTFSB1
    IU1,   1,   1,   0,  0,  0, // PC_MTFSFI
    IU1,   1,   1,   0,  0,  1, // PC_SC
    FPU1,  1,   1,   0,  0,  0, // PC_FSQRT
    FPU1,  1,   1,   0,  0,  0, // PC_FSQRTS
    IU1,   1,   1,   0,  0,  0, // PC_TLBIA
    IU1,   1,   1,   0,  0,  0, // PC_TLBIE
    IU1,   1,   1,   0,  0,  0, // PC_TLBLD
    IU1,   1,   1,   0,  0,  0, // PC_TLBLI
    IU1,   1,   1,   0,  0,  0, // PC_TLBSYNC
    IU1,   1,   1,   0,  0,  1, // PC_TW
    IU1,   1,   1,   0,  0,  1, // PC_TRAP
    IU1,   1,   1,   0,  0,  1, // PC_TWI
    IU1,   1,   1,   0,  0,  1, // PC_OPWORD
    IU1,   1,   1,   0,  0,  0, // PC_MFROM
    IU1,   1,   1,   0,  0,  1, // PC_DSA
    IU1,   1,   1,   0,  0,  1, // PC_ESA
    IU1,   0,   0,   0,  0,  0, // PC_DCCCI
    IU1,   0,   0,   0,  0,  0, // PC_DCREAD
    IU1,   0,   0,   0,  0,  0, // PC_ICBT
    IU1,   0,   0,   0,  0,  0, // PC_ICCCI
    IU1,   0,   0,   0,  0,  0, // PC_ICREAD
    IU1,   0,   0,   0,  0,  0, // PC_RFCI
    IU1,   0,   0,   0,  0,  0, // PC_TLBRE
    IU1,   0,   0,   0,  0,  0, // PC_TLBSX
    IU1,   0,   0,   0,  0,  0, // PC_TLBWE
    IU1,   0,   0,   0,  0,  0, // PC_WRTEE
    IU1,   0,   0,   0,  0,  0, // PC_WRTEEI
    IU1,   0,   0,   0,  0,  0, // PC_MFDCR
    IU1,   0,   0,   0,  0,  0, // PC_MTDCR
    IU1,   0,   0,   0,  0,  0, // PC_DCBA
    FPU1,  0,   0,   0,  0,  0, // PC_ISELGT
    FPU1,  0,   0,   0,  0,  0, // PC_ISELEQ
    FPU1,  0,   0,   0,  0,  0, // PC_ISELLT
    FPU1,  0,   0,   0,  0,  0, // PC_DSS
    FPU1,  0,   0,   0,  0,  0, // PC_DSSALL
    FPU1,  0,   0,   0,  0,  0, // PC_DST
    FPU1,  0,   0,   0,  0,  0, // PC_DSTT
    FPU1,  0,   0,   0,  0,  0, // PC_DSTST
    FPU1,  0,   0,   0,  0,  0, // PC_DSTSTT
    FPU1,  0,   0,   0,  0,  0, // PC_LVEBX
    FPU1,  0,   0,   0,  0,  0, // PC_LVEHX
    FPU1,  0,   0,   0,  0,  0, // PC_LVEWX
    FPU1,  0,   0,   0,  0,  0, // PC_LVSL
    FPU1,  0,   0,   0,  0,  0, // PC_LVSR
    FPU1,  0,   0,   0,  0,  0, // PC_LVX
    FPU1,  0,   0,   0,  0,  0, // PC_LVXL
    FPU1,  0,   0,   0,  0,  0, // PC_STVEBX
    FPU1,  0,   0,   0,  0,  0, // PC_STVEHX
    FPU1,  0,   0,   0,  0,  0, // PC_STVEWX
    FPU1,  0,   0,   0,  0,  0, // PC_STVX
    FPU1,  0,   0,   0,  0,  0, // PC_STVXL
    FPU1,  0,   0,   0,  0,  0, // PC_MFVSCR
    FPU1,  0,   0,   0,  0,  0, // PC_MTVSCR
    FPU1,  0,   0,   0,  0,  0, // PC_VADDCUW
    FPU1,  0,   0,   0,  0,  0, // PC_VADDFP
    FPU1,  0,   0,   0,  0,  0, // PC_VADDSBS
    FPU1,  0,   0,   0,  0,  0, // PC_VADDSHS
    FPU1,  0,   0,   0,  0,  0, // PC_VADDSWS
    FPU1,  0,   0,   0,  0,  0, // PC_VADDUBM
    FPU1,  0,   0,   0,  0,  0, // PC_VADDUBS
    FPU1,  0,   0,   0,  0,  0, // PC_VADDUHM
    FPU1,  0,   0,   0,  0,  0, // PC_VADDUHS
    FPU1,  0,   0,   0,  0,  0, // PC_VADDUWM
    FPU1,  0,   0,   0,  0,  0, // PC_VADDUWS
    FPU1,  0,   0,   0,  0,  0, // PC_VAND
    FPU1,  0,   0,   0,  0,  0, // PC_VANDC
    FPU1,  0,   0,   0,  0,  0, // PC_VAVGSB
    FPU1,  0,   0,   0,  0,  0, // PC_VAVGSH
    FPU1,  0,   0,   0,  0,  0, // PC_VAVGSW
    FPU1,  0,   0,   0,  0,  0, // PC_VAVGUB
    FPU1,  0,   0,   0,  0,  0, // PC_VAVGUH
    FPU1,  0,   0,   0,  0,  0, // PC_VAVGUW
    FPU1,  0,   0,   0,  0,  0, // PC_VCFSX
    FPU1,  0,   0,   0,  0,  0, // PC_VCFUX
    FPU1,  0,   0,   0,  0,  0, // PC_VCMPBFP
    FPU1,  0,   0,   0,  0,  0, // PC_VCMPEQFP
    FPU1,  0,   0,   0,  0,  0, // PC_VCMPEQUB
    FPU1,  0,   0,   0,  0,  0, // PC_VCMPEQUH
    FPU1,  0,   0,   0,  0,  0, // PC_VCMPEQUW
    FPU1,  0,   0,   0,  0,  0, // PC_VCMPGEFP
    FPU1,  0,   0,   0,  0,  0, // PC_VCMPGTFP
    FPU1,  0,   0,   0,  0,  0, // PC_VCMPGTSB
    FPU1,  0,   0,   0,  0,  0, // PC_VCMPGTSH
    FPU1,  0,   0,   0,  0,  0, // PC_VCMPGTSW
    FPU1,  0,   0,   0,  0,  0, // PC_VCMPGTUB
    FPU1,  0,   0,   0,  0,  0, // PC_VCMPGTUH
    FPU1,  0,   0,   0,  0,  0, // PC_VCMPGTUW
    FPU1,  0,   0,   0,  0,  0, // PC_VCTSXS
    FPU1,  0,   0,   0,  0,  0, // PC_VCTUXS
    FPU1,  0,   0,   0,  0,  0, // PC_VEXPTEFP
    FPU1,  0,   0,   0,  0,  0, // PC_VLOGEFP
    FPU1,  0,   0,   0,  0,  0, // PC_VMAXFP
    FPU1,  0,   0,   0,  0,  0, // PC_VMAXSB
    FPU1,  0,   0,   0,  0,  0, // PC_VMAXSH
    FPU1,  0,   0,   0,  0,  0, // PC_VMAXSW
    FPU1,  0,   0,   0,  0,  0, // PC_VMAXUB
    FPU1,  0,   0,   0,  0,  0, // PC_VMAXUH
    FPU1,  0,   0,   0,  0,  0, // PC_VMAXUW
    FPU1,  0,   0,   0,  0,  0, // PC_VMINFP
    FPU1,  0,   0,   0,  0,  0, // PC_VMINSB
    FPU1,  0,   0,   0,  0,  0, // PC_VMINSH
    FPU1,  0,   0,   0,  0,  0, // PC_VMINSW
    FPU1,  0,   0,   0,  0,  0, // PC_VMINUB
    FPU1,  0,   0,   0,  0,  0, // PC_VMINUH
    FPU1,  0,   0,   0,  0,  0, // PC_VMINUW
    FPU1,  0,   0,   0,  0,  0, // PC_VMRGHB
    FPU1,  0,   0,   0,  0,  0, // PC_VMRGHH
    FPU1,  0,   0,   0,  0,  0, // PC_VMRGHW
    FPU1,  0,   0,   0,  0,  0, // PC_VMRGLB
    FPU1,  0,   0,   0,  0,  0, // PC_VMRGLH
    FPU1,  0,   0,   0,  0,  0, // PC_VMRGLW
    FPU1,  0,   0,   0,  0,  0, // PC_VMULESB
    FPU1,  0,   0,   0,  0,  0, // PC_VMULESH
    FPU1,  0,   0,   0,  0,  0, // PC_VMULEUB
    FPU1,  0,   0,   0,  0,  0, // PC_VMULEUH
    FPU1,  0,   0,   0,  0,  0, // PC_VMULOSB
    FPU1,  0,   0,   0,  0,  0, // PC_VMULOSH
    FPU1,  0,   0,   0,  0,  0, // PC_VMULOUB
    FPU1,  0,   0,   0,  0,  0, // PC_VMULOUH
    FPU1,  0,   0,   0,  0,  0, // PC_VNOR
    FPU1,  0,   0,   0,  0,  0, // PC_VOR
    FPU1,  0,   0,   0,  0,  0, // PC_VPKPX
    FPU1,  0,   0,   0,  0,  0, // PC_VPKSHSS
    FPU1,  0,   0,   0,  0,  0, // PC_VPKSHUS
    FPU1,  0,   0,   0,  0,  0, // PC_VPKSWSS
    FPU1,  0,   0,   0,  0,  0, // PC_VPKSWUS
    FPU1,  0,   0,   0,  0,  0, // PC_VPKUHUM
    FPU1,  0,   0,   0,  0,  0, // PC_VPKUHUS
    FPU1,  0,   0,   0,  0,  0, // PC_VPKUWUM
    FPU1,  0,   0,   0,  0,  0, // PC_VPKUWUS
    FPU1,  0,   0,   0,  0,  0, // PC_VREFP
    FPU1,  0,   0,   0,  0,  0, // PC_VRFIM
    FPU1,  0,   0,   0,  0,  0, // PC_VRFIN
    FPU1,  0,   0,   0,  0,  0, // PC_VRFIP
    FPU1,  0,   0,   0,  0,  0, // PC_VRFIZ
    FPU1,  0,   0,   0,  0,  0, // PC_VRLB
    FPU1,  0,   0,   0,  0,  0, // PC_VRLH
    FPU1,  0,   0,   0,  0,  0, // PC_VRLW
    FPU1,  0,   0,   0,  0,  0, // PC_VRSQRTEFP
    FPU1,  0,   0,   0,  0,  0, // PC_VSL
    FPU1,  0,   0,   0,  0,  0, // PC_VSLB
    FPU1,  0,   0,   0,  0,  0, // PC_VSLH
    FPU1,  0,   0,   0,  0,  0, // PC_VSLO
    FPU1,  0,   0,   0,  0,  0, // PC_VSLW
    FPU1,  0,   0,   0,  0,  0, // PC_VSPLTB
    FPU1,  0,   0,   0,  0,  0, // PC_VSPLTH
    FPU1,  0,   0,   0,  0,  0, // PC_VSPLTW
    FPU1,  0,   0,   0,  0,  0, // PC_VSPLTISB
    FPU1,  0,   0,   0,  0,  0, // PC_VSPLTISH
    FPU1,  0,   0,   0,  0,  0, // PC_VSPLTISW
    FPU1,  0,   0,   0,  0,  0, // PC_VSR
    FPU1,  0,   0,   0,  0,  0, // PC_VSRAB
    FPU1,  0,   0,   0,  0,  0, // PC_VSRAH
    FPU1,  0,   0,   0,  0,  0, // PC_VSRAW
    FPU1,  0,   0,   0,  0,  0, // PC_VSRB
    FPU1,  0,   0,   0,  0,  0, // PC_VSRH
    FPU1,  0,   0,   0,  0,  0, // PC_VSRO
    FPU1,  0,   0,   0,  0,  0, // PC_VSRW
    FPU1,  0,   0,   0,  0,  0, // PC_VSUBCUW
    FPU1,  0,   0,   0,  0,  0, // PC_VSUBFP
    FPU1,  0,   0,   0,  0,  0, // PC_VSUBSBS
    FPU1,  0,   0,   0,  0,  0, // PC_VSUBSHS
    FPU1,  0,   0,   0,  0,  0, // PC_VSUBSWS
    FPU1,  0,   0,   0,  0,  0, // PC_VSUBUBM
    FPU1,  0,   0,   0,  0,  0, // PC_VSUBUBS
    FPU1,  0,   0,   0,  0,  0, // PC_VSUBUHM
    FPU1,  0,   0,   0,  0,  0, // PC_VSUBUHS
    FPU1,  0,   0,   0,  0,  0, // PC_VSUBUWM
    FPU1,  0,   0,   0,  0,  0, // PC_VSUBUWS
    FPU1,  0,   0,   0,  0,  0, // PC_VSUMSWS
    FPU1,  0,   0,   0,  0,  0, // PC_VSUM2SWS
    FPU1,  0,   0,   0,  0,  0, // PC_VSUM4SBS
    FPU1,  0,   0,   0,  0,  0, // PC_VSUM4SHS
    FPU1,  0,   0,   0,  0,  0, // PC_VSUM4UBS
    FPU1,  0,   0,   0,  0,  0, // PC_VUPKHPX
    FPU1,  0,   0,   0,  0,  0, // PC_VUPKHSB
    FPU1,  0,   0,   0,  0,  0, // PC_VUPKHSH
    FPU1,  0,   0,   0,  0,  0, // PC_VUPKLPX
    FPU1,  0,   0,   0,  0,  0, // PC_VUPKLSB
    FPU1,  0,   0,   0,  0,  0, // PC_VUPKLSH
    FPU1,  0,   0,   0,  0,  0, // PC_VXOR
    FPU1,  0,   0,   0,  0,  0, // PC_VMADDFP
    FPU1,  0,   0,   0,  0,  0, // PC_VMHADDSHS
    FPU1,  0,   0,   0,  0,  0, // PC_VMHRADDSHS
    FPU1,  0,   0,   0,  0,  0, // PC_VMLADDUHM
    FPU1,  0,   0,   0,  0,  0, // PC_VMSUMMBM
    FPU1,  0,   0,   0,  0,  0, // PC_VMSUMSHM
    FPU1,  0,   0,   0,  0,  0, // PC_VMSUMSHS
    FPU1,  0,   0,   0,  0,  0, // PC_VMSUMUBM
    FPU1,  0,   0,   0,  0,  0, // PC_VMSUMUHM
    FPU1,  0,   0,   0,  0,  0, // PC_VMSUMUHS
    FPU1,  0,   0,   0,  0,  0, // PC_VNMSUBFP
    FPU1,  0,   0,   0,  0,  0, // PC_VPERM
    FPU1,  0,   0,   0,  0,  0, // PC_VSEL
    FPU1,  0,   0,   0,  0,  0, // PC_VSLDOI
    FPU1,  0,   0,   0,  0,  0, // PC_VMR
    FPU1,  0,   0,   0,  0,  0, // PC_VMRP
    LSU1,  2,   1,   1,  0,  0, // PC_PSQ_L
    LSU1,  2,   1,   1,  0,  0, // PC_PSQ_LU
    LSU1,  2,   1,   1,  0,  0, // PC_PSQ_LX
    LSU1,  2,   1,   1,  0,  0, // PC_PSQ_LUX
    LSU1,  2,   1,   1,  0,  0, // PC_PSQ_ST
    LSU1,  2,   1,   1,  0,  0, // PC_PSQ_STU
    LSU1,  2,   1,   1,  0,  0, // PC_PSQ_STX
    LSU1,  2,   1,   1,  0,  0, // PC_PSQ_STUX
    LSU1,  2,   0,   0,  0,  1, // PC_DCBZ_L
    FPU1,  3,   1,   1,  1,  0, // PC_PS_ADD
    FPU1,  3,   1,   1,  1,  0, // PC_PS_SUB
    FPU1,  3,   1,   1,  1,  0, // PC_PS_MUL
    FPU1,  3,   1,   1,  1,  0, // PC_PS_DIV
    FPU1,  3,   1,   1,  1,  0, // PC_PS_MADD
    FPU1,  3,   1,   1,  1,  0, // PC_PS_MSUB
    FPU1,  3,   1,   1,  1,  0, // PC_PS_NMADD
    FPU1,  3,   1,   1,  1,  0, // PC_PS_NMSUB
    FPU1,  3,   1,   1,  1,  0, // PC_PS_RES
    FPU1,  3,   1,   1,  1,  0, // PC_PS_SEL
    FPU1,  3,   1,   1,  1,  0, // PC_PS_ABS
    FPU1,  3,   1,   1,  1,  0, // PC_PS_NABS
    FPU1,  3,   1,   1,  1,  0, // PC_PS_NEG
    FPU1,  3,   1,   1,  1,  0, // PC_PS_MR
    FPU1,  3,   1,   1,  1,  0, // PC_PS_RSQRTE
    FPU1,  3,   1,   1,  1,  0, // PC_PS_CMPU0
    FPU1,  3,   1,   1,  1,  0, // PC_PS_CMPO0
    FPU1,  3,   1,   1,  1,  0, // PC_PS_CMPU1
    FPU1,  3,   1,   1,  1,  0, // PC_PS_CMPO1
    FPU1,  3,   1,   1,  1,  0, // PC_PS_MERGE00
    FPU1,  3,   1,   1,  1,  0, // PC_PS_MERGE01
    FPU1,  3,   1,   1,  1,  0, // PC_PS_MERGE10
    FPU1,  3,   1,   1,  1,  0, // PC_PS_MERGE11
    FPU1,  3,   1,   1,  1,  0, // PC_PS_SUM0
    FPU1,  3,   1,   1,  1,  0, // PC_PS_SUM1
    FPU1,  3,   1,   1,  1,  0, // PC_PS_MULS0
    FPU1,  3,   1,   1,  1,  0, // PC_PS_MULS1
    FPU1,  3,   1,   1,  1,  0, // PC_PS_MADDS0
    FPU1,  3,   1,   1,  1,  0, // PC_PS_MADDS1
    BPU,   0,   0,   0,  0,  0, // PC_SLE
    BPU,   0,   0,   0,  0,  0, // PC_SLEQ
    BPU,   0,   0,   0,  0,  0, // PC_SLIQ
    BPU,   0,   0,   0,  0,  0, // PC_SLLIQ
    BPU,   0,   0,   0,  0,  0, // PC_SLLQ
    BPU,   0,   0,   0,  0,  0, // PC_SLQ
    BPU,   0,   0,   0,  0,  0, // PC_SRAIQ
    BPU,   0,   0,   0,  0,  0, // PC_SRAQ
    BPU,   0,   0,   0,  0,  0, // PC_SRE
    BPU,   0,   0,   0,  0,  0, // PC_SREA
    BPU,   0,   0,   0,  0,  0, // PC_SREQ
    BPU,   0,   0,   0,  0,  0, // PC_SRIQ
    BPU,   0,   0,   0,  0,  0, // PC_SRLIQ
    BPU,   0,   0,   0,  0,  0, // PC_SRLQ
    BPU,   0,   0,   0,  0,  0, // PC_SRQ
    BPU,   0,   0,   0,  0,  0, // PC_MASKG
    BPU,   0,   0,   0,  0,  0, // PC_MASKIR
    BPU,   0,   0,   0,  0,  0, // PC_LSCBX
    BPU,   0,   0,   0,  0,  0, // PC_DIV
    BPU,   0,   0,   0,  0,  0, // PC_DIVS
    BPU,   0,   0,   0,  0,  0, // PC_DOZ
    BPU,   0,   0,   0,  0,  0, // PC_MUL
    BPU,   0,   0,   0,  0,  0, // PC_NABS
    BPU,   0,   0,   0,  0,  0, // PC_ABS
    BPU,   0,   0,   0,  0,  0, // PC_CLCS
    BPU,   0,   0,   0,  0,  0, // PC_DOZI
    BPU,   0,   0,   0,  0,  0, // PC_RLMI
    BPU,   0,   0,   0,  0,  0, // PC_RRIB
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

    if (stage == IU1) {
        iu1_completed_instruction = instr;
    } else if (stage == IU2) {
        iu2_completed_instruction = instr;
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

    iu1_completed_instruction = NULL;
    iu2_completed_instruction = NULL;
}

static int can_issue(PCode* instr) {
    int stage;

    if (completionbuffers.free == 0) {
        return FALSE;
    }

    stage = instruction_timing[instr->op].stage;

    if (stage == IU2) {
        PCode* check;
        int isClear1 = !pipeline[IU1].instr;
        int isClear2 = !pipeline[IU2].instr;
        if (!isClear1 && !isClear2) {
            return FALSE;
        }
        if (isClear1 && isClear2) {
            return TRUE;
        }

        if (!isClear1) {
            check = pipeline[IU1].instr;
        } else {
            check = pipeline[IU2].instr;
        }

        if (is_dependent(instr, check, RegClass_GPR)) {
            return FALSE;
        }
        if (is_dependent(instr, iu1_completed_instruction, RegClass_GPR)) {
            return FALSE;
        }
        if (is_dependent(instr, iu2_completed_instruction, RegClass_GPR)) {
            return FALSE;
        }
    } else {
        if (pipeline[stage].instr) {
            return FALSE;
        }
    }

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
    if (stage == IU2 && !pipeline[IU1].instr) {
        stage = IU1;
    }
    pipeline[stage].instr = instr;
    pipeline[stage].remaining = cycles;
}

static void advance_clock(void) {
    int stage;

    iu1_completed_instruction = NULL;
    iu2_completed_instruction = NULL;

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

    if (pipeline[IU1].instr && pipeline[IU1].remaining == 0) {
        complete_instruction(IU1);
    }
    if (pipeline[LSU2].instr && pipeline[LSU2].remaining == 0) {
        complete_instruction(LSU2);
    }
    if (pipeline[FPU3].instr && pipeline[FPU3].remaining == 0) {
        complete_instruction(FPU3);
    }
    if (pipeline[SRU].instr && pipeline[SRU].remaining == 0) {
        complete_instruction(SRU);
    }
    if (pipeline[BPU].instr && pipeline[BPU].remaining == 0) {
        complete_instruction(BPU);
    }
    if (pipeline[IU2].instr && pipeline[IU2].remaining == 0) {
        complete_instruction(IU2);
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
