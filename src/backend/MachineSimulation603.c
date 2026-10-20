#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Scheduler.h"

/* The model's statics: every machine model has these names, so each prefixes its own */
#define latency m603_latency
#define initialize m603_initialize
#define can_issue m603_can_issue
#define issue m603_issue
#define advance_clock m603_advance_clock
#define serializes m603_serializes

// (the stages as the 603e user's manual names them)
// https://www.nxp.com/docs/en/reference-manual/MPC603EUM.pdf

typedef enum Stage {
    BPU, // Branch Prediction Unit
    IU, // Integer Unit
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

enum {
    MaxEntries = 5
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
MachineInfo machine603 = {
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
    LSU1,  2,   1,   1,  0,  0, // PC_DCBF
    LSU1,  2,   1,   1,  0,  0, // PC_DCBST
    LSU1,  2,   1,   1,  0,  0, // PC_DCBT
    LSU1,  2,   1,   1,  0,  0, // PC_DCBTST
    LSU1,  2,   1,   1,  0,  0, // PC_DCBZ
    IU,    1,   1,   0,  0,  0, // PC_ADD
    IU,    1,   1,   0,  0,  0, // PC_ADDC
    IU,    1,   1,   0,  0,  0, // PC_ADDE
    IU,    1,   1,   0,  0,  0, // PC_ADDI
    IU,    1,   1,   0,  0,  0, // PC_ADDIC
    IU,    1,   1,   0,  0,  0, // PC_ADDICR
    IU,    1,   1,   0,  0,  0, // PC_ADDIS
    IU,    1,   1,   0,  0,  0, // PC_ADDME
    IU,    1,   1,   0,  0,  0, // PC_ADDZE
    IU,    37,  37,  0,  0,  0, // PC_DIVW
    IU,    37,  37,  0,  0,  0, // PC_DIVWU
    IU,    5,   5,   0,  0,  0, // PC_MULHW
    IU,    5,   5,   0,  0,  0, // PC_MULHWU
    IU,    3,   3,   0,  0,  0, // PC_MULLI
    IU,    5,   5,   0,  0,  0, // PC_MULLW
    IU,    1,   1,   0,  0,  0, // PC_NEG
    IU,    1,   1,   0,  0,  0, // PC_SUBF
    IU,    1,   1,   0,  0,  0, // PC_SUBFC
    IU,    1,   1,   0,  0,  0, // PC_SUBFE
    IU,    1,   1,   0,  0,  0, // PC_SUBFIC
    IU,    1,   1,   0,  0,  0, // PC_SUBFME
    IU,    1,   1,   0,  0,  0, // PC_SUBFZE
    IU,    3,   1,   0,  0,  0, // PC_CMPI
    IU,    3,   1,   0,  0,  0, // PC_CMP
    IU,    3,   1,   0,  0,  0, // PC_CMPLI
    IU,    3,   1,   0,  0,  0, // PC_CMPL
    IU,    1,   1,   0,  0,  0, // PC_ANDI
    IU,    1,   1,   0,  0,  0, // PC_ANDIS
    IU,    1,   1,   0,  0,  0, // PC_ORI
    IU,    1,   1,   0,  0,  0, // PC_ORIS
    IU,    1,   1,   0,  0,  0, // PC_XORI
    IU,    1,   1,   0,  0,  0, // PC_XORIS
    IU,    1,   1,   0,  0,  0, // PC_AND
    IU,    1,   1,   0,  0,  0, // PC_OR
    IU,    1,   1,   0,  0,  0, // PC_XOR
    IU,    1,   1,   0,  0,  0, // PC_NAND
    IU,    1,   1,   0,  0,  0, // PC_NOR
    IU,    1,   1,   0,  0,  0, // PC_EQV
    IU,    1,   1,   0,  0,  0, // PC_ANDC
    IU,    1,   1,   0,  0,  0, // PC_ORC
    IU,    1,   1,   0,  0,  0, // PC_EXTSB
    IU,    1,   1,   0,  0,  0, // PC_EXTSH
    IU,    1,   1,   0,  0,  0, // PC_CNTLZW
    IU,    1,   1,   0,  0,  0, // PC_RLWINM
    IU,    1,   1,   0,  0,  0, // PC_RLWNM
    IU,    1,   1,   0,  0,  0, // PC_RLWIMI
    IU,    1,   1,   0,  0,  0, // PC_SLW
    IU,    1,   1,   0,  0,  0, // PC_SRW
    IU,    1,   1,   0,  0,  0, // PC_SRAWI
    IU,    1,   1,   0,  0,  0, // PC_SRAW
    SRU,   1,   1,   0,  0,  0, // PC_CRAND
    SRU,   1,   1,   0,  0,  0, // PC_CRANDC
    SRU,   1,   1,   0,  0,  0, // PC_CREQV
    SRU,   1,   1,   0,  0,  0, // PC_CRNAND
    SRU,   1,   1,   0,  0,  0, // PC_CRNOR
    SRU,   1,   1,   0,  0,  0, // PC_CROR
    SRU,   1,   1,   0,  0,  0, // PC_CRORC
    SRU,   1,   1,   0,  0,  0, // PC_CRXOR
    SRU,   1,   1,   0,  0,  0, // PC_MCRF
    SRU,   2,   2,   0,  0,  0, // PC_MTXER
    SRU,   2,   2,   0,  0,  0, // PC_MTCTR
    SRU,   2,   2,   0,  0,  0, // PC_MTLR
    SRU,   1,   1,   0,  0,  0, // PC_MTCRF
    SRU,   1,   1,   0,  0,  1, // PC_MTMSR
    SRU,   1,   1,   0,  0,  1, // PC_MTSPR
    SRU,   1,   1,   0,  0,  1, // PC_MFMSR
    SRU,   1,   1,   0,  0,  1, // PC_MFSPR
    SRU,   1,   1,   0,  0,  0, // PC_MFXER
    SRU,   1,   1,   0,  0,  0, // PC_MFCTR
    SRU,   1,   1,   0,  0,  0, // PC_MFLR
    SRU,   1,   1,   0,  0,  0, // PC_MFCR
    FPU1,  3,   1,   1,  1,  0, // PC_MFFS
    FPU1,  3,   1,   1,  1,  0, // PC_MTFSF
    SRU,   1,   1,   0,  0,  1, // PC_EIEIO
    SRU,   1,   1,   0,  0,  1, // PC_ISYNC
    SRU,   1,   1,   0,  0,  1, // PC_SYNC
    SRU,   1,   1,   0,  0,  1, // PC_RFI
    IU,    1,   1,   0,  0,  0, // PC_LI
    IU,    1,   1,   0,  0,  0, // PC_LIS
    IU,    1,   1,   0,  0,  0, // PC_MR
    IU,    1,   1,   0,  0,  0, // PC_NOP
    IU,    1,   1,   0,  0,  0, // PC_NOT
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
    FPU1,  33,  33,  0,  0,  0, // PC_FDIV
    FPU1,  18,  18,  0,  0,  0, // PC_FDIVS
    FPU1,  4,   2,   1,  1,  0, // PC_FMADD
    FPU1,  3,   1,   1,  1,  0, // PC_FMADDS
    FPU1,  4,   2,   1,  1,  0, // PC_FMSUB
    FPU1,  3,   1,   1,  1,  0, // PC_FMSUBS
    FPU1,  4,   2,   1,  1,  0, // PC_FNMADD
    FPU1,  3,   1,   1,  1,  0, // PC_FNMADDS
    FPU1,  4,   2,   1,  1,  0, // PC_FNMSUB
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
    IU,    1,   1,   0,  0,  1, // PC_ECIWX
    IU,    1,   1,   0,  0,  1, // PC_ECOWX
    IU,    1,   1,   0,  0,  0, // PC_DCBI
    IU,    1,   1,   0,  0,  0, // PC_ICBI
    IU,    1,   1,   0,  0,  0, // PC_MCRFS
    IU,    1,   1,   0,  0,  0, // PC_MCRXR
    IU,    1,   1,   0,  0,  0, // PC_MFTB
    IU,    1,   1,   0,  0,  0, // PC_MFSR
    IU,    1,   1,   0,  0,  0, // PC_MTSR
    IU,    1,   1,   0,  0,  0, // PC_MFSRIN
    IU,    1,   1,   0,  0,  0, // PC_MTSRIN
    IU,    1,   1,   0,  0,  0, // PC_MTFSB0
    IU,    1,   1,   0,  0,  0, // PC_MTFSB1
    IU,    1,   1,   0,  0,  0, // PC_MTFSFI
    IU,    1,   1,   0,  0,  1, // PC_SC
    FPU1,  1,   1,   0,  0,  0, // PC_FSQRT
    FPU1,  1,   1,   0,  0,  0, // PC_FSQRTS
    IU,    1,   1,   0,  0,  0, // PC_TLBIA
    IU,    1,   1,   0,  0,  0, // PC_TLBIE
    IU,    1,   1,   0,  0,  0, // PC_TLBLD
    IU,    1,   1,   0,  0,  0, // PC_TLBLI
    IU,    1,   1,   0,  0,  0, // PC_TLBSYNC
    IU,    1,   1,   0,  0,  1, // PC_TW
    IU,    1,   1,   0,  0,  1, // PC_TRAP
    IU,    1,   1,   0,  0,  1, // PC_TWI
    IU,    1,   1,   0,  0,  1, // PC_OPWORD
    IU,    1,   1,   0,  0,  0, // PC_MFROM
    IU,    1,   1,   0,  0,  1, // PC_DSA
    IU,    1,   1,   0,  0,  1, // PC_ESA
    IU,    0,   0,   0,  0,  0, // PC_DCCCI
    IU,    0,   0,   0,  0,  0, // PC_DCREAD
    IU,    0,   0,   0,  0,  0, // PC_ICBT
    IU,    0,   0,   0,  0,  0, // PC_ICCCI
    IU,    0,   0,   0,  0,  0, // PC_ICREAD
    IU,    0,   0,   0,  0,  0, // PC_RFCI
    IU,    0,   0,   0,  0,  0, // PC_TLBRE
    IU,    0,   0,   0,  0,  0, // PC_TLBSX
    IU,    0,   0,   0,  0,  0, // PC_TLBWE
    IU,    0,   0,   0,  0,  0, // PC_WRTEE
    IU,    0,   0,   0,  0,  0, // PC_WRTEEI
    IU,    0,   0,   0,  0,  0, // PC_MFDCR
    IU,    0,   0,   0,  0,  0, // PC_MTDCR
    IU,    0,   0,   0,  0,  0, // PC_DCBA
    BPU,   0,   0,   0,  0,  0, // PC_ISELGT
    BPU,   0,   0,   0,  0,  0, // PC_ISELEQ
    BPU,   0,   0,   0,  0,  0, // PC_ISELLT
    BPU,   0,   0,   0,  0,  0, // PC_DSS
    BPU,   0,   0,   0,  0,  0, // PC_DSSALL
    BPU,   0,   0,   0,  0,  0, // PC_DST
    BPU,   0,   0,   0,  0,  0, // PC_DSTT
    BPU,   0,   0,   0,  0,  0, // PC_DSTST
    BPU,   0,   0,   0,  0,  0, // PC_DSTSTT
    BPU,   0,   0,   0,  0,  0, // PC_LVEBX
    BPU,   0,   0,   0,  0,  0, // PC_LVEHX
    BPU,   0,   0,   0,  0,  0, // PC_LVEWX
    BPU,   0,   0,   0,  0,  0, // PC_LVSL
    BPU,   0,   0,   0,  0,  0, // PC_LVSR
    BPU,   0,   0,   0,  0,  0, // PC_LVX
    BPU,   0,   0,   0,  0,  0, // PC_LVXL
    BPU,   0,   0,   0,  0,  0, // PC_STVEBX
    BPU,   0,   0,   0,  0,  0, // PC_STVEHX
    BPU,   0,   0,   0,  0,  0, // PC_STVEWX
    BPU,   0,   0,   0,  0,  0, // PC_STVX
    BPU,   0,   0,   0,  0,  0, // PC_STVXL
    BPU,   0,   0,   0,  0,  0, // PC_MFVSCR
    BPU,   0,   0,   0,  0,  0, // PC_MTVSCR
    BPU,   0,   0,   0,  0,  0, // PC_VADDCUW
    BPU,   0,   0,   0,  0,  0, // PC_VADDFP
    BPU,   0,   0,   0,  0,  0, // PC_VADDSBS
    BPU,   0,   0,   0,  0,  0, // PC_VADDSHS
    BPU,   0,   0,   0,  0,  0, // PC_VADDSWS
    BPU,   0,   0,   0,  0,  0, // PC_VADDUBM
    BPU,   0,   0,   0,  0,  0, // PC_VADDUBS
    BPU,   0,   0,   0,  0,  0, // PC_VADDUHM
    BPU,   0,   0,   0,  0,  0, // PC_VADDUHS
    BPU,   0,   0,   0,  0,  0, // PC_VADDUWM
    BPU,   0,   0,   0,  0,  0, // PC_VADDUWS
    BPU,   0,   0,   0,  0,  0, // PC_VAND
    BPU,   0,   0,   0,  0,  0, // PC_VANDC
    BPU,   0,   0,   0,  0,  0, // PC_VAVGSB
    BPU,   0,   0,   0,  0,  0, // PC_VAVGSH
    BPU,   0,   0,   0,  0,  0, // PC_VAVGSW
    BPU,   0,   0,   0,  0,  0, // PC_VAVGUB
    BPU,   0,   0,   0,  0,  0, // PC_VAVGUH
    BPU,   0,   0,   0,  0,  0, // PC_VAVGUW
    BPU,   0,   0,   0,  0,  0, // PC_VCFSX
    BPU,   0,   0,   0,  0,  0, // PC_VCFUX
    BPU,   0,   0,   0,  0,  0, // PC_VCMPBFP
    BPU,   0,   0,   0,  0,  0, // PC_VCMPEQFP
    BPU,   0,   0,   0,  0,  0, // PC_VCMPEQUB
    BPU,   0,   0,   0,  0,  0, // PC_VCMPEQUH
    BPU,   0,   0,   0,  0,  0, // PC_VCMPEQUW
    BPU,   0,   0,   0,  0,  0, // PC_VCMPGEFP
    BPU,   0,   0,   0,  0,  0, // PC_VCMPGTFP
    BPU,   0,   0,   0,  0,  0, // PC_VCMPGTSB
    BPU,   0,   0,   0,  0,  0, // PC_VCMPGTSH
    BPU,   0,   0,   0,  0,  0, // PC_VCMPGTSW
    BPU,   0,   0,   0,  0,  0, // PC_VCMPGTUB
    BPU,   0,   0,   0,  0,  0, // PC_VCMPGTUH
    BPU,   0,   0,   0,  0,  0, // PC_VCMPGTUW
    BPU,   0,   0,   0,  0,  0, // PC_VCTSXS
    BPU,   0,   0,   0,  0,  0, // PC_VCTUXS
    BPU,   0,   0,   0,  0,  0, // PC_VEXPTEFP
    BPU,   0,   0,   0,  0,  0, // PC_VLOGEFP
    BPU,   0,   0,   0,  0,  0, // PC_VMAXFP
    BPU,   0,   0,   0,  0,  0, // PC_VMAXSB
    BPU,   0,   0,   0,  0,  0, // PC_VMAXSH
    BPU,   0,   0,   0,  0,  0, // PC_VMAXSW
    BPU,   0,   0,   0,  0,  0, // PC_VMAXUB
    BPU,   0,   0,   0,  0,  0, // PC_VMAXUH
    BPU,   0,   0,   0,  0,  0, // PC_VMAXUW
    BPU,   0,   0,   0,  0,  0, // PC_VMINFP
    BPU,   0,   0,   0,  0,  0, // PC_VMINSB
    BPU,   0,   0,   0,  0,  0, // PC_VMINSH
    BPU,   0,   0,   0,  0,  0, // PC_VMINSW
    BPU,   0,   0,   0,  0,  0, // PC_VMINUB
    BPU,   0,   0,   0,  0,  0, // PC_VMINUH
    BPU,   0,   0,   0,  0,  0, // PC_VMINUW
    BPU,   0,   0,   0,  0,  0, // PC_VMRGHB
    BPU,   0,   0,   0,  0,  0, // PC_VMRGHH
    BPU,   0,   0,   0,  0,  0, // PC_VMRGHW
    BPU,   0,   0,   0,  0,  0, // PC_VMRGLB
    BPU,   0,   0,   0,  0,  0, // PC_VMRGLH
    BPU,   0,   0,   0,  0,  0, // PC_VMRGLW
    BPU,   0,   0,   0,  0,  0, // PC_VMULESB
    BPU,   0,   0,   0,  0,  0, // PC_VMULESH
    BPU,   0,   0,   0,  0,  0, // PC_VMULEUB
    BPU,   0,   0,   0,  0,  0, // PC_VMULEUH
    BPU,   0,   0,   0,  0,  0, // PC_VMULOSB
    BPU,   0,   0,   0,  0,  0, // PC_VMULOSH
    BPU,   0,   0,   0,  0,  0, // PC_VMULOUB
    BPU,   0,   0,   0,  0,  0, // PC_VMULOUH
    BPU,   0,   0,   0,  0,  0, // PC_VNOR
    BPU,   0,   0,   0,  0,  0, // PC_VOR
    BPU,   0,   0,   0,  0,  0, // PC_VPKPX
    BPU,   0,   0,   0,  0,  0, // PC_VPKSHSS
    BPU,   0,   0,   0,  0,  0, // PC_VPKSHUS
    BPU,   0,   0,   0,  0,  0, // PC_VPKSWSS
    BPU,   0,   0,   0,  0,  0, // PC_VPKSWUS
    BPU,   0,   0,   0,  0,  0, // PC_VPKUHUM
    BPU,   0,   0,   0,  0,  0, // PC_VPKUHUS
    BPU,   0,   0,   0,  0,  0, // PC_VPKUWUM
    BPU,   0,   0,   0,  0,  0, // PC_VPKUWUS
    BPU,   0,   0,   0,  0,  0, // PC_VREFP
    BPU,   0,   0,   0,  0,  0, // PC_VRFIM
    BPU,   0,   0,   0,  0,  0, // PC_VRFIN
    BPU,   0,   0,   0,  0,  0, // PC_VRFIP
    BPU,   0,   0,   0,  0,  0, // PC_VRFIZ
    BPU,   0,   0,   0,  0,  0, // PC_VRLB
    BPU,   0,   0,   0,  0,  0, // PC_VRLH
    BPU,   0,   0,   0,  0,  0, // PC_VRLW
    BPU,   0,   0,   0,  0,  0, // PC_VRSQRTEFP
    BPU,   0,   0,   0,  0,  0, // PC_VSL
    BPU,   0,   0,   0,  0,  0, // PC_VSLB
    BPU,   0,   0,   0,  0,  0, // PC_VSLH
    BPU,   0,   0,   0,  0,  0, // PC_VSLO
    BPU,   0,   0,   0,  0,  0, // PC_VSLW
    BPU,   0,   0,   0,  0,  0, // PC_VSPLTB
    BPU,   0,   0,   0,  0,  0, // PC_VSPLTH
    BPU,   0,   0,   0,  0,  0, // PC_VSPLTW
    BPU,   0,   0,   0,  0,  0, // PC_VSPLTISB
    BPU,   0,   0,   0,  0,  0, // PC_VSPLTISH
    BPU,   0,   0,   0,  0,  0, // PC_VSPLTISW
    BPU,   0,   0,   0,  0,  0, // PC_VSR
    BPU,   0,   0,   0,  0,  0, // PC_VSRAB
    BPU,   0,   0,   0,  0,  0, // PC_VSRAH
    BPU,   0,   0,   0,  0,  0, // PC_VSRAW
    BPU,   0,   0,   0,  0,  0, // PC_VSRB
    BPU,   0,   0,   0,  0,  0, // PC_VSRH
    BPU,   0,   0,   0,  0,  0, // PC_VSRO
    BPU,   0,   0,   0,  0,  0, // PC_VSRW
    BPU,   0,   0,   0,  0,  0, // PC_VSUBCUW
    BPU,   0,   0,   0,  0,  0, // PC_VSUBFP
    BPU,   0,   0,   0,  0,  0, // PC_VSUBSBS
    BPU,   0,   0,   0,  0,  0, // PC_VSUBSHS
    BPU,   0,   0,   0,  0,  0, // PC_VSUBSWS
    BPU,   0,   0,   0,  0,  0, // PC_VSUBUBM
    BPU,   0,   0,   0,  0,  0, // PC_VSUBUBS
    BPU,   0,   0,   0,  0,  0, // PC_VSUBUHM
    BPU,   0,   0,   0,  0,  0, // PC_VSUBUHS
    BPU,   0,   0,   0,  0,  0, // PC_VSUBUWM
    BPU,   0,   0,   0,  0,  0, // PC_VSUBUWS
    BPU,   0,   0,   0,  0,  0, // PC_VSUMSWS
    BPU,   0,   0,   0,  0,  0, // PC_VSUM2SWS
    BPU,   0,   0,   0,  0,  0, // PC_VSUM4SBS
    BPU,   0,   0,   0,  0,  0, // PC_VSUM4SHS
    BPU,   0,   0,   0,  0,  0, // PC_VSUM4UBS
    BPU,   0,   0,   0,  0,  0, // PC_VUPKHPX
    BPU,   0,   0,   0,  0,  0, // PC_VUPKHSB
    BPU,   0,   0,   0,  0,  0, // PC_VUPKHSH
    BPU,   0,   0,   0,  0,  0, // PC_VUPKLPX
    BPU,   0,   0,   0,  0,  0, // PC_VUPKLSB
    BPU,   0,   0,   0,  0,  0, // PC_VUPKLSH
    BPU,   0,   0,   0,  0,  0, // PC_VXOR
    BPU,   0,   0,   0,  0,  0, // PC_VMADDFP
    BPU,   0,   0,   0,  0,  0, // PC_VMHADDSHS
    BPU,   0,   0,   0,  0,  0, // PC_VMHRADDSHS
    BPU,   0,   0,   0,  0,  0, // PC_VMLADDUHM
    BPU,   0,   0,   0,  0,  0, // PC_VMSUMMBM
    BPU,   0,   0,   0,  0,  0, // PC_VMSUMSHM
    BPU,   0,   0,   0,  0,  0, // PC_VMSUMSHS
    BPU,   0,   0,   0,  0,  0, // PC_VMSUMUBM
    BPU,   0,   0,   0,  0,  0, // PC_VMSUMUHM
    BPU,   0,   0,   0,  0,  0, // PC_VMSUMUHS
    BPU,   0,   0,   0,  0,  0, // PC_VNMSUBFP
    BPU,   0,   0,   0,  0,  0, // PC_VPERM
    BPU,   0,   0,   0,  0,  0, // PC_VSEL
    BPU,   0,   0,   0,  0,  0, // PC_VSLDOI
    BPU,   0,   0,   0,  0,  0, // PC_VMR
    BPU,   0,   0,   0,  0,  0, // PC_VMRP
    BPU,   0,   0,   0,  0,  0, // PC_PSQ_L
    BPU,   0,   0,   0,  0,  0, // PC_PSQ_LU
    BPU,   0,   0,   0,  0,  0, // PC_PSQ_LX
    BPU,   0,   0,   0,  0,  0, // PC_PSQ_LUX
    BPU,   0,   0,   0,  0,  0, // PC_PSQ_ST
    BPU,   0,   0,   0,  0,  0, // PC_PSQ_STU
    BPU,   0,   0,   0,  0,  0, // PC_PSQ_STX
    BPU,   0,   0,   0,  0,  0, // PC_PSQ_STUX
    BPU,   0,   0,   0,  0,  0, // PC_DCBZ_L
    BPU,   0,   0,   0,  0,  0, // PC_PS_ADD
    BPU,   0,   0,   0,  0,  0, // PC_PS_SUB
    BPU,   0,   0,   0,  0,  0, // PC_PS_MUL
    BPU,   0,   0,   0,  0,  0, // PC_PS_DIV
    BPU,   0,   0,   0,  0,  0, // PC_PS_MADD
    BPU,   0,   0,   0,  0,  0, // PC_PS_MSUB
    BPU,   0,   0,   0,  0,  0, // PC_PS_NMADD
    BPU,   0,   0,   0,  0,  0, // PC_PS_NMSUB
    BPU,   0,   0,   0,  0,  0, // PC_PS_RES
    BPU,   0,   0,   0,  0,  0, // PC_PS_SEL
    BPU,   0,   0,   0,  0,  0, // PC_PS_ABS
    BPU,   0,   0,   0,  0,  0, // PC_PS_NABS
    BPU,   0,   0,   0,  0,  0, // PC_PS_NEG
    BPU,   0,   0,   0,  0,  0, // PC_PS_MR
    BPU,   0,   0,   0,  0,  0, // PC_PS_RSQRTE
    BPU,   0,   0,   0,  0,  0, // PC_PS_CMPU0
    BPU,   0,   0,   0,  0,  0, // PC_PS_CMPO0
    BPU,   0,   0,   0,  0,  0, // PC_PS_CMPU1
    BPU,   0,   0,   0,  0,  0, // PC_PS_CMPO1
    BPU,   0,   0,   0,  0,  0, // PC_PS_MERGE00
    BPU,   0,   0,   0,  0,  0, // PC_PS_MERGE01
    BPU,   0,   0,   0,  0,  0, // PC_PS_MERGE10
    BPU,   0,   0,   0,  0,  0, // PC_PS_MERGE11
    BPU,   0,   0,   0,  0,  0, // PC_PS_SUM0
    BPU,   0,   0,   0,  0,  0, // PC_PS_SUM1
    BPU,   0,   0,   0,  0,  0, // PC_PS_MULS0
    BPU,   0,   0,   0,  0,  0, // PC_PS_MULS1
    BPU,   0,   0,   0,  0,  0, // PC_PS_MADDS0
    BPU,   0,   0,   0,  0,  0, // PC_PS_MADDS1
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

    completionbuffers.free = 5;
    completionbuffers.used = 0;
    completionbuffers.nextToRetire = 0;
    completionbuffers.nextFreeSlot = 0;
    for (i = 0; i < MaxEntries; i++) {
        completionbuffers.entries[i].instr = NULL;
    }
}

static int can_issue(PCode* instr) {
    if (completionbuffers.free == 0) {
        return FALSE;
    }
    if (pipeline[instruction_timing[instr->op].stage].instr) {
        return FALSE;
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
    pipeline[stage].instr = instr;
    pipeline[stage].remaining = cycles;
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

    if (pipeline[IU].instr && pipeline[IU].remaining == 0) {
        complete_instruction(IU);
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
