// MachineSimulation7400.c: the scheduler's model of the MPC7400
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Scheduler.h"

/* The model's statics: every machine model has these names, so each prefixes its own */
#define latency m7400_latency
#define initialize m7400_initialize
#define can_issue m7400_can_issue
#define issue m7400_issue
#define advance_clock m7400_advance_clock
#define serializes m7400_serializes

// https://www.nxp.com/docs/en/reference-manual/MPC7410UM.pdf

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
    VSIU, // Vector Simple Integer Unit
    VPU, // AltiVec Permute Unit

    VCIU1, // Vector Complex Integer Unit
    VCIU2,
    VCIU3,

    VFPU1, // Vector Floating-Point Unit
    VFPU2,
    VFPU3,
    VFPU4,

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
    MaxEntries = 8
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
static int uses_vpermute_unit_7400(PCode* instr);

// clang-format off
MachineInfo machine7400 = {
    2,
    TRUE,
    0,
    &latency,
    &initialize,
    &can_issue,
    &issue,
    &advance_clock,
    &serializes,
    &uses_vpermute_unit_7400
};
// clang-format on

static struct {
    // the initial stage for this instruction
    Stage stage;

    // the total amount of cycles required by this instruction
    char latency;

    // how long it takes to finish each stage
    char cycles[4];

    // does this instruction serialise?
    char serializes;
    // clang-format off
} instruction_timing[OPCODE_MAX] = {
    BPU,    0,   0,   0,  0,  0,  0, // PC_B
    BPU,    0,   0,   0,  0,  0,  0, // PC_BL
    BPU,    0,   0,   0,  0,  0,  0, // PC_BC
    BPU,    0,   0,   0,  0,  0,  0, // PC_BCLR
    BPU,    0,   0,   0,  0,  0,  0, // PC_BCCTR
    BPU,    0,   0,   0,  0,  0,  0, // PC_BT
    BPU,    0,   0,   0,  0,  0,  0, // PC_BTLR
    BPU,    0,   0,   0,  0,  0,  0, // PC_BTCTR
    BPU,    0,   0,   0,  0,  0,  0, // PC_BF
    BPU,    0,   0,   0,  0,  0,  0, // PC_BFLR
    BPU,    0,   0,   0,  0,  0,  0, // PC_BFCTR
    BPU,    0,   0,   0,  0,  0,  0, // PC_BDNZ
    BPU,    0,   0,   0,  0,  0,  0, // PC_BDNZT
    BPU,    0,   0,   0,  0,  0,  0, // PC_BDNZF
    BPU,    0,   0,   0,  0,  0,  0, // PC_BDZ
    BPU,    0,   0,   0,  0,  0,  0, // PC_BDZT
    BPU,    0,   0,   0,  0,  0,  0, // PC_BDZF
    BPU,    0,   0,   0,  0,  0,  0, // PC_BLR
    BPU,    0,   0,   0,  0,  0,  0, // PC_BCTR
    BPU,    0,   0,   0,  0,  0,  0, // PC_BCTRL
    BPU,    0,   0,   0,  0,  0,  0, // PC_BLRL
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LBZ
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LBZU
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LBZX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LBZUX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LHZ
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LHZU
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LHZX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LHZUX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LHA
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LHAU
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LHAX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LHAUX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LHBRX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LWZ
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LWZU
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LWZX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LWZUX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LWBRX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LMW
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STB
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STBU
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STBX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STBUX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STH
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STHU
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STHX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STHUX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STHBRX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STW
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STWU
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STWX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STWUX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STWBRX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STMW
    LSU1,   3,   1,   2,  0,  0,  0, // PC_DCBF
    LSU1,   3,   1,   2,  0,  0,  0, // PC_DCBST
    LSU1,   2,   1,   1,  0,  0,  0, // PC_DCBT
    LSU1,   2,   1,   1,  0,  0,  0, // PC_DCBTST
    LSU1,   3,   1,   2,  0,  0,  0, // PC_DCBZ
    IU2,    1,   1,   0,  0,  0,  0, // PC_ADD
    IU2,    1,   1,   0,  0,  0,  0, // PC_ADDC
    IU2,    1,   1,   0,  0,  0,  0, // PC_ADDE
    IU2,    1,   1,   0,  0,  0,  0, // PC_ADDI
    IU2,    1,   1,   0,  0,  0,  0, // PC_ADDIC
    IU2,    1,   1,   0,  0,  0,  0, // PC_ADDICR
    IU2,    1,   1,   0,  0,  0,  0, // PC_ADDIS
    IU2,    1,   1,   0,  0,  0,  0, // PC_ADDME
    IU2,    1,   1,   0,  0,  0,  0, // PC_ADDZE
    IU1,    19,  19,  0,  0,  0,  0, // PC_DIVW
    IU1,    19,  19,  0,  0,  0,  0, // PC_DIVWU
    IU1,    5,   5,   0,  0,  0,  0, // PC_MULHW
    IU1,    6,   5,   0,  0,  0,  0, // PC_MULHWU
    IU1,    3,   3,   0,  0,  0,  0, // PC_MULLI
    IU1,    5,   5,   0,  0,  0,  0, // PC_MULLW
    IU2,    1,   1,   0,  0,  0,  0, // PC_NEG
    IU2,    1,   1,   0,  0,  0,  0, // PC_SUBF
    IU2,    1,   1,   0,  0,  0,  0, // PC_SUBFC
    IU2,    1,   1,   0,  0,  0,  0, // PC_SUBFE
    IU2,    1,   1,   0,  0,  0,  0, // PC_SUBFIC
    IU2,    1,   1,   0,  0,  0,  0, // PC_SUBFME
    IU2,    1,   1,   0,  0,  0,  0, // PC_SUBFZE
    IU2,    3,   1,   0,  0,  0,  0, // PC_CMPI
    IU2,    3,   1,   0,  0,  0,  0, // PC_CMP
    IU2,    3,   1,   0,  0,  0,  0, // PC_CMPLI
    IU2,    3,   1,   0,  0,  0,  0, // PC_CMPL
    IU2,    1,   1,   0,  0,  0,  0, // PC_ANDI
    IU2,    1,   1,   0,  0,  0,  0, // PC_ANDIS
    IU2,    1,   1,   0,  0,  0,  0, // PC_ORI
    IU2,    1,   1,   0,  0,  0,  0, // PC_ORIS
    IU2,    1,   1,   0,  0,  0,  0, // PC_XORI
    IU2,    1,   1,   0,  0,  0,  0, // PC_XORIS
    IU2,    1,   1,   0,  0,  0,  0, // PC_AND
    IU2,    1,   1,   0,  0,  0,  0, // PC_OR
    IU2,    1,   1,   0,  0,  0,  0, // PC_XOR
    IU2,    1,   1,   0,  0,  0,  0, // PC_NAND
    IU2,    1,   1,   0,  0,  0,  0, // PC_NOR
    IU2,    1,   1,   0,  0,  0,  0, // PC_EQV
    IU2,    1,   1,   0,  0,  0,  0, // PC_ANDC
    IU2,    1,   1,   0,  0,  0,  0, // PC_ORC
    IU2,    1,   1,   0,  0,  0,  0, // PC_EXTSB
    IU2,    1,   1,   0,  0,  0,  0, // PC_EXTSH
    IU2,    1,   1,   0,  0,  0,  0, // PC_CNTLZW
    IU2,    1,   1,   0,  0,  0,  0, // PC_RLWINM
    IU2,    1,   1,   0,  0,  0,  0, // PC_RLWNM
    IU2,    1,   1,   0,  0,  0,  0, // PC_RLWIMI
    IU2,    1,   1,   0,  0,  0,  0, // PC_SLW
    IU2,    1,   1,   0,  0,  0,  0, // PC_SRW
    IU2,    1,   1,   0,  0,  0,  0, // PC_SRAWI
    IU2,    1,   1,   0,  0,  0,  0, // PC_SRAW
    SRU,    1,   1,   0,  0,  0,  1, // PC_CRAND
    SRU,    1,   1,   0,  0,  0,  1, // PC_CRANDC
    SRU,    1,   1,   0,  0,  0,  1, // PC_CREQV
    SRU,    1,   1,   0,  0,  0,  1, // PC_CRNAND
    SRU,    1,   1,   0,  0,  0,  1, // PC_CRNOR
    SRU,    1,   1,   0,  0,  0,  1, // PC_CROR
    SRU,    1,   1,   0,  0,  0,  1, // PC_CRORC
    SRU,    1,   1,   0,  0,  0,  1, // PC_CRXOR
    SRU,    1,   1,   0,  0,  0,  1, // PC_MCRF
    SRU,    2,   2,   0,  0,  0,  1, // PC_MTXER
    SRU,    2,   2,   0,  0,  0,  1, // PC_MTCTR
    SRU,    2,   2,   0,  0,  0,  1, // PC_MTLR
    SRU,    1,   1,   0,  0,  0,  1, // PC_MTCRF
    SRU,    1,   1,   0,  0,  0,  1, // PC_MTMSR
    SRU,    2,   2,   0,  0,  0,  1, // PC_MTSPR
    SRU,    1,   1,   0,  0,  0,  0, // PC_MFMSR
    SRU,    3,   3,   0,  0,  0,  1, // PC_MFSPR
    SRU,    1,   1,   0,  0,  0,  1, // PC_MFXER
    SRU,    1,   1,   0,  0,  0,  1, // PC_MFCTR
    SRU,    1,   1,   0,  0,  0,  1, // PC_MFLR
    SRU,    1,   1,   0,  0,  0,  1, // PC_MFCR
    FPU1,   3,   1,   1,  1,  0,  0, // PC_MFFS
    FPU1,   3,   1,   1,  1,  0,  0, // PC_MTFSF
    SRU,    1,   1,   0,  0,  0,  1, // PC_EIEIO
    SRU,    2,   2,   0,  0,  0,  1, // PC_ISYNC
    SRU,    3,   3,   0,  0,  0,  1, // PC_SYNC
    SRU,    2,   2,   0,  0,  0,  1, // PC_RFI
    IU2,    1,   1,   0,  0,  0,  0, // PC_LI
    IU2,    1,   1,   0,  0,  0,  0, // PC_LIS
    IU2,    1,   1,   0,  0,  0,  0, // PC_MR
    IU2,    1,   1,   0,  0,  0,  0, // PC_NOP
    IU2,    1,   1,   0,  0,  0,  0, // PC_NOT
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LFS
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LFSU
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LFSX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LFSUX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LFD
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LFDU
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LFDX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LFDUX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STFS
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STFSU
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STFSX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STFSUX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STFD
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STFDU
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STFDX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STFDUX
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FMR
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FABS
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FNEG
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FNABS
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FADD
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FADDS
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FSUB
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FSUBS
    FPU1,   4,   2,   1,  1,  0,  0, // PC_FMUL
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FMULS
    FPU1,   31,  31,  0,  0,  0,  0, // PC_FDIV
    FPU1,   17,  17,  0,  0,  0,  0, // PC_FDIVS
    FPU1,   4,   2,   1,  1,  0,  0, // PC_FMADD
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FMADDS
    FPU1,   4,   2,   1,  1,  0,  0, // PC_FMSUB
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FMSUBS
    FPU1,   4,   2,   1,  1,  0,  0, // PC_FNMADD
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FNMADDS
    FPU1,   4,   2,   1,  1,  0,  0, // PC_FNMSUB
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FNMSUBS
    FPU1,   10,  10,  0,  0,  0,  0, // PC_FRES
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FRSQRTE
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FSEL
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FRSP
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FCTIW
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FCTIWZ
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FCMPU
    FPU1,   3,   1,   1,  1,  0,  0, // PC_FCMPO
    LSU1,   2,   1,   1,  0,  0,  1, // PC_LWARX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LSWI
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LSWX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STFIWX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STSWI
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STSWX
    LSU1,   2,   1,   1,  0,  0,  1, // PC_STWCX
    IU2,    1,   1,   0,  0,  0,  0, // PC_ECIWX
    IU2,    1,   1,   0,  0,  0,  0, // PC_ECOWX
    IU2,    1,   1,   0,  0,  0,  0, // PC_DCBI
    IU2,    1,   1,   0,  0,  0,  0, // PC_ICBI
    SRU,    1,   1,   0,  0,  0,  1, // PC_MCRFS
    SRU,    1,   1,   0,  0,  0,  1, // PC_MCRXR
    SRU,    1,   1,   0,  0,  0,  0, // PC_MFTB
    SRU,    3,   3,   0,  0,  0,  0, // PC_MFSR
    SRU,    2,   2,   0,  0,  0,  1, // PC_MTSR
    SRU,    3,   3,   0,  0,  0,  0, // PC_MFSRIN
    SRU,    2,   2,   0,  0,  0,  1, // PC_MTSRIN
    FPU1,   1,   1,   0,  0,  0,  0, // PC_MTFSB0
    FPU1,   1,   1,   0,  0,  0,  0, // PC_MTFSB1
    FPU1,   1,   1,   0,  0,  0,  0, // PC_MTFSFI
    SRU,    2,   2,   0,  0,  0,  1, // PC_SC
    FPU1,   1,   1,   0,  0,  0,  0, // PC_FSQRT
    FPU1,   1,   1,   0,  0,  0,  0, // PC_FSQRTS
    LSU1,   1,   1,   0,  0,  0,  0, // PC_TLBIA
    LSU1,   1,   1,   0,  0,  0,  0, // PC_TLBIE
    LSU1,   1,   1,   0,  0,  0,  0, // PC_TLBLD
    LSU1,   1,   1,   0,  0,  0,  0, // PC_TLBLI
    LSU1,   1,   1,   0,  0,  0,  1, // PC_TLBSYNC
    IU2,    1,   1,   0,  0,  0,  0, // PC_TW
    IU2,    1,   1,   0,  0,  0,  0, // PC_TRAP
    IU2,    1,   1,   0,  0,  0,  0, // PC_TWI
    IU2,    1,   1,   0,  0,  0,  1, // PC_OPWORD
    IU2,    1,   1,   0,  0,  0,  0, // PC_MFROM
    IU2,    1,   1,   0,  0,  0,  0, // PC_DSA
    IU2,    1,   1,   0,  0,  0,  0, // PC_ESA
    IU2,    1,   0,   0,  0,  0,  0, // PC_DCCCI
    IU2,    1,   0,   0,  0,  0,  0, // PC_DCREAD
    IU2,    1,   0,   0,  0,  0,  0, // PC_ICBT
    IU2,    1,   0,   0,  0,  0,  0, // PC_ICCCI
    IU2,    1,   0,   0,  0,  0,  0, // PC_ICREAD
    IU2,    1,   0,   0,  0,  0,  0, // PC_RFCI
    IU2,    1,   0,   0,  0,  0,  0, // PC_TLBRE
    IU2,    1,   0,   0,  0,  0,  0, // PC_TLBSX
    IU2,    1,   0,   0,  0,  0,  0, // PC_TLBWE
    IU2,    1,   0,   0,  0,  0,  0, // PC_WRTEE
    IU2,    1,   0,   0,  0,  0,  0, // PC_WRTEEI
    IU2,    1,   0,   0,  0,  0,  0, // PC_MFDCR
    IU2,    1,   0,   0,  0,  0,  0, // PC_MTDCR
    LSU1,   3,   1,   2,  0,  0,  0, // PC_DCBA
    IU2,    1,   1,   0,  0,  0,  0, // 236
    IU2,    1,   1,   0,  0,  0,  0, // 237
    IU2,    1,   1,   0,  0,  0,  0, // 238
    LSU1,   2,   1,   1,  0,  0,  0, // PC_DSS
    LSU1,   2,   1,   1,  0,  0,  0, // PC_DSSALL
    LSU1,   2,   1,   1,  0,  0,  0, // PC_DST
    LSU1,   2,   1,   1,  0,  0,  0, // PC_DSTT
    LSU1,   2,   1,   1,  0,  0,  0, // PC_DSTST
    LSU1,   2,   1,   1,  0,  0,  0, // PC_DSTSTT
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LVEBX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LVEHX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LVEWX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LVSL
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LVSR
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LVX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_LVXL
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STVEBX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STVEHX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STVEWX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STVX
    LSU1,   2,   1,   1,  0,  0,  0, // PC_STVXL
    VSIU,   1,   1,   0,  0,  0,  1, // PC_MFVSCR
    VSIU,   1,   1,   0,  0,  0,  1, // PC_MTVSCR
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VADDCUW
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VADDFP
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VADDSBS
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VADDSHS
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VADDSWS
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VADDUBM
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VADDUBS
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VADDUHM
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VADDUHS
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VADDUWM
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VADDUWS
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VAND
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VANDC
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VAVGSB
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VAVGSH
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VAVGSW
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VAVGUB
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VAVGUH
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VAVGUW
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VCFSX
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VCFUX
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VCMPBFP
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VCMPEQFP
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VCMPEQUB
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VCMPEQUH
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VCMPEQUW
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VCMPGEFP
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VCMPGTFP
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VCMPGTSB
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VCMPGTSH
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VCMPGTSW
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VCMPGTUB
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VCMPGTUH
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VCMPGTUW
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VCTSXS
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VCTUXS
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VEXPTEFP
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VLOGEFP
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VMAXFP
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VMAXSB
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VMAXSH
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VMAXSW
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VMAXUB
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VMAXUH
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VMAXUW
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VMINFP
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VMINSB
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VMINSH
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VMINSW
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VMINUB
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VMINUH
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VMINUW
    VPU,    1,   1,   0,  0,  0,  0, // PC_VMRGHB
    VPU,    1,   1,   0,  0,  0,  0, // PC_VMRGHH
    VPU,    1,   1,   0,  0,  0,  0, // PC_VMRGHW
    VPU,    1,   1,   0,  0,  0,  0, // PC_VMRGLB
    VPU,    1,   1,   0,  0,  0,  0, // PC_VMRGLH
    VPU,    1,   1,   0,  0,  0,  0, // PC_VMRGLW
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMULESB
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMULESH
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMULEUB
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMULEUH
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMULOSB
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMULOSH
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMULOUB
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMULOUH
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VNOR
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VOR
    VPU,    1,   1,   0,  0,  0,  0, // PC_VPKPX
    VPU,    1,   1,   0,  0,  0,  0, // PC_VPKSHSS
    VPU,    1,   1,   0,  0,  0,  0, // PC_VPKSHUS
    VPU,    1,   1,   0,  0,  0,  0, // PC_VPKSWSS
    VPU,    1,   1,   0,  0,  0,  0, // PC_VPKSWUS
    VPU,    1,   1,   0,  0,  0,  0, // PC_VPKUHUM
    VPU,    1,   1,   0,  0,  0,  0, // PC_VPKUHUS
    VPU,    1,   1,   0,  0,  0,  0, // PC_VPKUWUM
    VPU,    1,   1,   0,  0,  0,  0, // PC_VPKUWUS
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VREFP
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VRFIM
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VRFIN
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VRFIP
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VRFIZ
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VRLB
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VRLH
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VRLW
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VRSQRTEFP
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSL
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSLB
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSLH
    VPU,    1,   1,   0,  0,  0,  0, // PC_VSLO
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSLW
    VPU,    1,   1,   0,  0,  0,  0, // PC_VSPLTB
    VPU,    1,   1,   0,  0,  0,  0, // PC_VSPLTH
    VPU,    1,   1,   0,  0,  0,  0, // PC_VSPLTW
    VPU,    1,   1,   0,  0,  0,  0, // PC_VSPLTISB
    VPU,    1,   1,   0,  0,  0,  0, // PC_VSPLTISH
    VPU,    1,   1,   0,  0,  0,  0, // PC_VSPLTISW
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSR
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSRAB
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSRAH
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSRAW
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSRB
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSRH
    VPU,    1,   1,   0,  0,  0,  0, // PC_VSRO
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSRW
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSUBCUW
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VSUBFP
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSUBSBS
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSUBSHS
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSUBSWS
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSUBUBM
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSUBUBS
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSUBUHM
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSUBUHS
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSUBUWM
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSUBUWS
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VSUMSWS
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VSUM2SWS
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VSUM4SBS
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VSUM4SHS
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VSUM4UBS
    VPU,    1,   1,   0,  0,  0,  0, // PC_VUPKHPX
    VPU,    1,   1,   0,  0,  0,  0, // PC_VUPKHSB
    VPU,    1,   1,   0,  0,  0,  0, // PC_VUPKHSH
    VPU,    1,   1,   0,  0,  0,  0, // PC_VUPKLPX
    VPU,    1,   1,   0,  0,  0,  0, // PC_VUPKLSB
    VPU,    1,   1,   0,  0,  0,  0, // PC_VUPKLSH
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VXOR
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VMADDFP
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMHADDSHS
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMHRADDSHS
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMLADDUHM
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMSUMMBM
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMSUMSHM
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMSUMSHS
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMSUMUBM
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMSUMUHM
    VCIU1,  3,   1,   1,  1,  0,  0, // PC_VMSUMUHS
    VFPU1,  4,   1,   1,  1,  1,  0, // PC_VNMSUBFP
    VPU,    1,   1,   0,  0,  0,  0, // PC_VPERM
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VSEL
    VPU,    1,   1,   0,  0,  0,  0, // PC_VSLDOI
    VSIU,   1,   1,   0,  0,  0,  0, // PC_VMR
    VPU,    1,   1,   0,  0,  0,  0, // PC_VMRP
    BPU,    0,   0,   0,  0,  0,  0, // PC_PSQ_L
    BPU,    0,   0,   0,  0,  0,  0, // PC_PSQ_LU
    BPU,    0,   0,   0,  0,  0,  0, // PC_PSQ_LX
    BPU,    0,   0,   0,  0,  0,  0, // PC_PSQ_LUX
    BPU,    0,   0,   0,  0,  0,  0, // PC_PSQ_ST
    BPU,    0,   0,   0,  0,  0,  0, // PC_PSQ_STU
    BPU,    0,   0,   0,  0,  0,  0, // PC_PSQ_STX
    BPU,    0,   0,   0,  0,  0,  0, // PC_PSQ_STUX
    BPU,    0,   0,   0,  0,  0,  0, // PC_DCBZ_L
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_ADD
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_SUB
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_MUL
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_DIV
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_MADD
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_MSUB
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_NMADD
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_NMSUB
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_RES
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_SEL
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_ABS
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_NABS
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_NEG
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_MR
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_RSQRTE
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_CMPU0
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_CMPO0
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_CMPU1
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_CMPO1
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_MERGE00
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_MERGE01
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_MERGE10
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_MERGE11
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_SUM0
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_SUM1
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_MULS0
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_MULS1
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_MADDS0
    BPU,    0,   0,   0,  0,  0,  0, // PC_PS_MADDS1
    BPU,    0,   0,   0,  0,  0,  0, // PC_SLE
    BPU,    0,   0,   0,  0,  0,  0, // PC_SLEQ
    BPU,    0,   0,   0,  0,  0,  0, // PC_SLIQ
    BPU,    0,   0,   0,  0,  0,  0, // PC_SLLIQ
    BPU,    0,   0,   0,  0,  0,  0, // PC_SLLQ
    BPU,    0,   0,   0,  0,  0,  0, // PC_SLQ
    BPU,    0,   0,   0,  0,  0,  0, // PC_SRAIQ
    BPU,    0,   0,   0,  0,  0,  0, // PC_SRAQ
    BPU,    0,   0,   0,  0,  0,  0, // PC_SRE
    BPU,    0,   0,   0,  0,  0,  0, // PC_SREA
    BPU,    0,   0,   0,  0,  0,  0, // PC_SREQ
    BPU,    0,   0,   0,  0,  0,  0, // PC_SRIQ
    BPU,    0,   0,   0,  0,  0,  0, // PC_SRLIQ
    BPU,    0,   0,   0,  0,  0,  0, // PC_SRLQ
    BPU,    0,   0,   0,  0,  0,  0, // PC_SRQ
    BPU,    0,   0,   0,  0,  0,  0, // PC_MASKG
    BPU,    0,   0,   0,  0,  0,  0, // PC_MASKIR
    BPU,    0,   0,   0,  0,  0,  0, // PC_LSCBX
    BPU,    0,   0,   0,  0,  0,  0, // PC_DIV
    BPU,    0,   0,   0,  0,  0,  0, // PC_DIVS
    BPU,    0,   0,   0,  0,  0,  0, // PC_DOZ
    BPU,    0,   0,   0,  0,  0,  0, // PC_MUL
    BPU,    0,   0,   0,  0,  0,  0, // PC_NABS
    BPU,    0,   0,   0,  0,  0,  0, // PC_ABS
    BPU,    0,   0,   0,  0,  0,  0, // PC_CLCS
    BPU,    0,   0,   0,  0,  0,  0, // PC_DOZI
    BPU,    0,   0,   0,  0,  0,  0, // PC_RLMI
    BPU,    0,   0,   0,  0,  0,  0, // PC_RRIB
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
    } else if (stage == VFPU1 || stage == VCIU1 || stage == VSIU || stage == VPU) {
        PCode* check;
        int isVpuClear = !pipeline[VPU].instr;
        int isVFpuClear = !pipeline[VFPU1].instr;
        int isVCiuClear = !pipeline[VCIU1].instr;
        int isVSiuClear = !pipeline[VSIU].instr;

        if (stage == VPU) {
            if (!isVpuClear) {
                return FALSE;
            }

            if (!isVFpuClear) {
                check = pipeline[VFPU1].instr;
            } else if (!isVCiuClear) {
                check = pipeline[VCIU1].instr;
            } else if (!isVSiuClear) {
                check = pipeline[VSIU].instr;
            } else {
                check = NULL;
            }

            if (is_dependent(instr, check, RegClass_VR)) {
                return FALSE;
            }
        } else {
            if (!isVFpuClear || !isVCiuClear || !isVSiuClear) {
                return FALSE;
            }

            if (!isVpuClear && is_dependent(instr, pipeline[VPU].instr, RegClass_VR)) {
                return FALSE;
            }
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
    if (pipeline[VPU].instr && pipeline[VPU].remaining == 0) {
        complete_instruction(VPU);
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
    if (pipeline[VSIU].instr && pipeline[VSIU].remaining == 0) {
        complete_instruction(VSIU);
    }
    if (pipeline[VCIU3].instr && pipeline[VCIU3].remaining == 0) {
        complete_instruction(VCIU3);
    }
    if (pipeline[VFPU4].instr && pipeline[VFPU4].remaining == 0) {
        complete_instruction(VFPU4);
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

    if (pipeline[VCIU2].instr && pipeline[VCIU2].remaining == 0 && !pipeline[VCIU3].instr) {
        advance(VCIU1, VCIU2, VCIU3);
    }
    if (pipeline[VCIU1].instr && pipeline[VCIU1].remaining == 0 && !pipeline[VCIU2].instr) {
        advance(VCIU1, VCIU1, VCIU2);
    }

    if (pipeline[VFPU3].instr && pipeline[VFPU3].remaining == 0 && !pipeline[VFPU4].instr) {
        advance(VFPU1, VFPU3, VFPU4);
    }
    if (pipeline[VFPU2].instr && pipeline[VFPU2].remaining == 0 && !pipeline[VFPU3].instr) {
        advance(VFPU1, VFPU2, VFPU3);
    }
    if (pipeline[VFPU1].instr && pipeline[VFPU1].remaining == 0 && !pipeline[VFPU2].instr) {
        advance(VFPU1, VFPU1, VFPU2);
    }
}

static int serializes(PCode* instr) {
    return instruction_timing[instr->op].serializes;
}

static int uses_vpermute_unit_7400(PCode* instr) {
    return instruction_timing[instr->op].stage == VPU;
}
