#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Scheduler.h"

/* The statics, prefixed: every machine model has statics of these names */
#define latency m601_latency
#define initialize m601_initialize
#define can_issue m601_can_issue
#define issue m601_issue
#define advance_clock m601_advance_clock
#define serializes m601_serializes

// https://stuff.mit.edu/afs/sipb/contrib/doc/specs/ic/cpu/powerpc/mpc601.pdf
// https://www.nxp.com/docs/en/user-guide/MPC601UMAD.pdf

typedef enum Stage {
    IU, // Integer Unit
    FD, // FP Decode
    FPM, // FP Multiply
    FPA, // FP Add
    FWA, // FP Arithmetic Writeback
    BPU, // Branch Processing Unit
    NumStages,
    Serialize, // special form for instructions that use IU but are serialised
    Unsupported // instructions not supported by this processor
} Stage;

static struct {
    // the instruction currently in this pipeline stage
    PCode* instr;

    // how many cycles are left for this instruction to finish
    int remaining;
#ifdef __MWERKS__
} pipeline[NumStages];
#else
    // can_issue and issue use Unsupported's slot, past the array's end, which nothing clears: the
    // original's is the first entry of MachineSimulation821.c's completion buffers, which nothing else
    // touches while the 601 is the model (a #pragma scheduling that changes models within a file
    // shares it in the original)
} pipeline[Unsupported + 1];
#endif

static int latency(PCode* instr);
static void initialize(void);
static int can_issue(PCode* instr);
static void issue(PCode* instr);
static void advance_clock(void);
static int serializes(PCode* instr);

// clang-format off
MachineInfo machine601 = {
    2,
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
    char cycles[4];
    // clang-format off
} instruction_timing[OPCODE_MAX] = {
    BPU,          0,   0,   0,  0,   1, // PC_B
    BPU,          0,   0,   0,  0,   1, // PC_BL
    BPU,          0,   0,   0,  0,   1, // PC_BC
    BPU,          0,   0,   0,  0,   1, // PC_BCLR
    BPU,          0,   0,   0,  0,   1, // PC_BCCTR
    BPU,          0,   0,   0,  0,   1, // PC_BT
    BPU,          0,   0,   0,  0,   1, // PC_BTLR
    BPU,          0,   0,   0,  0,   1, // PC_BTCTR
    BPU,          0,   0,   0,  0,   1, // PC_BF
    BPU,          0,   0,   0,  0,   1, // PC_BFLR
    BPU,          0,   0,   0,  0,   1, // PC_BFCTR
    BPU,          0,   0,   0,  0,   1, // PC_BDNZ
    BPU,          0,   0,   0,  0,   1, // PC_BDNZT
    BPU,          0,   0,   0,  0,   1, // PC_BDNZF
    BPU,          0,   0,   0,  0,   1, // PC_BDZ
    BPU,          0,   0,   0,  0,   1, // PC_BDZT
    BPU,          0,   0,   0,  0,   1, // PC_BDZF
    BPU,          0,   0,   0,  0,   1, // PC_BLR
    BPU,          0,   0,   0,  0,   1, // PC_BCTR
    BPU,          0,   0,   0,  0,   1, // PC_BCTRL
    BPU,          0,   0,   0,  0,   1, // PC_BLRL
    IU,           2,   1,   0,  0,   0, // PC_LBZ
    IU,           2,   1,   0,  0,   0, // PC_LBZU
    IU,           2,   1,   0,  0,   0, // PC_LBZX
    IU,           2,   1,   0,  0,   0, // PC_LBZUX
    IU,           2,   1,   0,  0,   0, // PC_LHZ
    IU,           2,   1,   0,  0,   0, // PC_LHZU
    IU,           2,   1,   0,  0,   0, // PC_LHZX
    IU,           2,   1,   0,  0,   0, // PC_LHZUX
    IU,           2,   1,   0,  0,   0, // PC_LHA
    IU,           2,   1,   0,  0,   0, // PC_LHAU
    IU,           2,   1,   0,  0,   0, // PC_LHAX
    IU,           2,   1,   0,  0,   0, // PC_LHAUX
    IU,           2,   1,   0,  0,   0, // PC_LHBRX
    IU,           2,   1,   0,  0,   0, // PC_LWZ
    IU,           2,   1,   0,  0,   0, // PC_LWZU
    IU,           2,   1,   0,  0,   0, // PC_LWZX
    IU,           2,   1,   0,  0,   0, // PC_LWZUX
    IU,           2,   1,   0,  0,   0, // PC_LWBRX
    IU,           1,   1,   0,  0,   0, // PC_LMW
    IU,           1,   1,   0,  0,   0, // PC_STB
    IU,           1,   1,   0,  0,   0, // PC_STBU
    IU,           1,   1,   0,  0,   0, // PC_STBX
    IU,           1,   1,   0,  0,   0, // PC_STBUX
    IU,           1,   1,   0,  0,   0, // PC_STH
    IU,           1,   1,   0,  0,   0, // PC_STHU
    IU,           1,   1,   0,  0,   0, // PC_STHX
    IU,           1,   1,   0,  0,   0, // PC_STHUX
    IU,           1,   1,   0,  0,   0, // PC_STHBRX
    IU,           1,   1,   0,  0,   0, // PC_STW
    IU,           1,   1,   0,  0,   0, // PC_STWU
    IU,           1,   1,   0,  0,   0, // PC_STWX
    IU,           1,   1,   0,  0,   0, // PC_STWUX
    IU,           1,   1,   0,  0,   0, // PC_STWBRX
    IU,           1,   1,   0,  0,   0, // PC_STMW
    IU,           2,   1,   0,  0,   0, // PC_DCBF
    IU,           2,   1,   0,  0,   0, // PC_DCBST
    IU,           2,   1,   0,  0,   0, // PC_DCBT
    IU,           2,   1,   0,  0,   0, // PC_DCBTST
    IU,           2,   1,   0,  0,   0, // PC_DCBZ
    IU,           1,   1,   0,  0,   0, // PC_ADD
    IU,           1,   1,   0,  0,   0, // PC_ADDC
    IU,           1,   1,   0,  0,   0, // PC_ADDE
    IU,           1,   1,   0,  0,   0, // PC_ADDI
    IU,           1,   1,   0,  0,   0, // PC_ADDIC
    IU,           1,   1,   0,  0,   0, // PC_ADDICR
    IU,           1,   1,   0,  0,   0, // PC_ADDIS
    IU,           1,   1,   0,  0,   0, // PC_ADDME
    IU,           1,   1,   0,  0,   0, // PC_ADDZE
    IU,           36,  36,  0,  0,   0, // PC_DIVW
    IU,           36,  36,  0,  0,   0, // PC_DIVWU
    IU,           5,   5,   0,  0,   0, // PC_MULHW
    IU,           5,   5,   0,  0,   0, // PC_MULHWU
    IU,           5,   5,   0,  0,   0, // PC_MULLI
    IU,           5,   5,   0,  0,   0, // PC_MULLW
    IU,           1,   1,   0,  0,   0, // PC_NEG
    IU,           1,   1,   0,  0,   0, // PC_SUBF
    IU,           1,   1,   0,  0,   0, // PC_SUBFC
    IU,           1,   1,   0,  0,   0, // PC_SUBFE
    IU,           1,   1,   0,  0,   0, // PC_SUBFIC
    IU,           1,   1,   0,  0,   0, // PC_SUBFME
    IU,           1,   1,   0,  0,   0, // PC_SUBFZE
    IU,           3,   1,   0,  0,   0, // PC_CMPI
    IU,           3,   1,   0,  0,   0, // PC_CMP
    IU,           3,   1,   0,  0,   0, // PC_CMPLI
    IU,           3,   1,   0,  0,   0, // PC_CMPL
    IU,           1,   1,   0,  0,   0, // PC_ANDI
    IU,           1,   1,   0,  0,   0, // PC_ANDIS
    IU,           1,   1,   0,  0,   0, // PC_ORI
    IU,           1,   1,   0,  0,   0, // PC_ORIS
    IU,           1,   1,   0,  0,   0, // PC_XORI
    IU,           1,   1,   0,  0,   0, // PC_XORIS
    IU,           1,   1,   0,  0,   0, // PC_AND
    IU,           1,   1,   0,  0,   0, // PC_OR
    IU,           1,   1,   0,  0,   0, // PC_XOR
    IU,           1,   1,   0,  0,   0, // PC_NAND
    IU,           1,   1,   0,  0,   0, // PC_NOR
    IU,           1,   1,   0,  0,   0, // PC_EQV
    IU,           1,   1,   0,  0,   0, // PC_ANDC
    IU,           1,   1,   0,  0,   0, // PC_ORC
    IU,           1,   1,   0,  0,   0, // PC_EXTSB
    IU,           1,   1,   0,  0,   0, // PC_EXTSH
    IU,           1,   1,   0,  0,   0, // PC_CNTLZW
    IU,           1,   1,   0,  0,   0, // PC_RLWINM
    IU,           1,   1,   0,  0,   0, // PC_RLWNM
    IU,           1,   1,   0,  0,   0, // PC_RLWIMI
    IU,           1,   1,   0,  0,   0, // PC_SLW
    IU,           1,   1,   0,  0,   0, // PC_SRW
    IU,           1,   1,   0,  0,   0, // PC_SRAWI
    IU,           1,   1,   0,  0,   0, // PC_SRAW
    IU,           1,   1,   0,  0,   0, // PC_CRAND
    IU,           1,   1,   0,  0,   0, // PC_CRANDC
    IU,           1,   1,   0,  0,   0, // PC_CREQV
    IU,           1,   1,   0,  0,   0, // PC_CRNAND
    IU,           1,   1,   0,  0,   0, // PC_CRNOR
    IU,           1,   1,   0,  0,   0, // PC_CROR
    IU,           1,   1,   0,  0,   0, // PC_CRORC
    IU,           1,   1,   0,  0,   0, // PC_CRXOR
    IU,           1,   1,   0,  0,   0, // PC_MCRF
    IU,           4,   1,   0,  0,   0, // PC_MTXER
    IU,           4,   1,   0,  0,   0, // PC_MTCTR
    IU,           4,   1,   0,  0,   0, // PC_MTLR
    IU,           2,   1,   0,  0,   0, // PC_MTCRF
    IU,           1,   0,   0,  0,   0, // PC_MTMSR
    IU,           1,   0,   0,  0,   0, // PC_MTSPR
    IU,           1,   0,   0,  0,   0, // PC_MFMSR
    IU,           1,   0,   0,  0,   0, // PC_MFSPR
    IU,           1,   1,   0,  0,   0, // PC_MFXER
    IU,           1,   1,   0,  0,   0, // PC_MFCTR
    IU,           1,   1,   0,  0,   0, // PC_MFLR
    IU,           1,   1,   0,  0,   0, // PC_MFCR
    FD,           4,   1,   1,  1,   1, // PC_MFFS
    FD,           4,   1,   1,  1,   1, // PC_MTFSF
    Serialize,    1,   1,   0,  0,   1, // PC_EIEIO
    Serialize,    1,   1,   0,  0,   1, // PC_ISYNC
    Serialize,    1,   1,   0,  0,   1, // PC_SYNC
    Serialize,    0,   0,   0,  0,   1, // PC_RFI
    IU,           1,   1,   0,  0,   0, // PC_LI
    IU,           1,   1,   0,  0,   0, // PC_LIS
    IU,           1,   1,   0,  0,   0, // PC_MR
    IU,           1,   1,   0,  0,   0, // PC_NOP
    IU,           1,   1,   0,  0,   0, // PC_NOT
    IU,           3,   1,   0,  0,   0, // PC_LFS
    IU,           3,   1,   0,  0,   0, // PC_LFSU
    IU,           3,   1,   0,  0,   0, // PC_LFSX
    IU,           3,   1,   0,  0,   0, // PC_LFSUX
    IU,           3,   1,   0,  0,   0, // PC_LFD
    IU,           3,   1,   0,  0,   0, // PC_LFDU
    IU,           3,   1,   0,  0,   0, // PC_LFDX
    IU,           3,   1,   0,  0,   0, // PC_LFDUX
    IU,           1,   1,   0,  0,   0, // PC_STFS
    IU,           1,   1,   0,  0,   0, // PC_STFSU
    IU,           1,   1,   0,  0,   0, // PC_STFSX
    IU,           1,   1,   0,  0,   0, // PC_STFSUX
    IU,           1,   1,   0,  0,   0, // PC_STFD
    IU,           1,   1,   0,  0,   0, // PC_STFDU
    IU,           1,   1,   0,  0,   0, // PC_STFDX
    IU,           1,   1,   0,  0,   0, // PC_STFDUX
    FD,           4,   1,   1,  1,   1, // PC_FMR
    FD,           4,   1,   1,  1,   1, // PC_FABS
    FD,           4,   1,   1,  1,   1, // PC_FNEG
    FD,           4,   1,   1,  1,   1, // PC_FNABS
    FD,           4,   1,   1,  1,   1, // PC_FADD
    FD,           4,   1,   1,  1,   1, // PC_FADDS
    FD,           4,   1,   1,  1,   1, // PC_FSUB
    FD,           4,   1,   1,  1,   1, // PC_FSUBS
    FD,           5,   1,   1,  2,   1, // PC_FMUL
    FD,           4,   1,   1,  1,   1, // PC_FMULS
    FD,           31,  1,   1,  28,  1, // PC_FDIV
    FD,           17,  1,   1,  14,  1, // PC_FDIVS
    FD,           5,   1,   1,  2,   1, // PC_FMADD
    FD,           4,   1,   1,  1,   1, // PC_FMADDS
    FD,           5,   1,   1,  2,   1, // PC_FMSUB
    FD,           4,   1,   1,  1,   1, // PC_FMSUBS
    FD,           5,   1,   1,  2,   1, // PC_FNMADD
    FD,           4,   1,   1,  1,   1, // PC_FNMADDS
    FD,           5,   1,   1,  2,   1, // PC_FNMSUB
    FD,           4,   1,   1,  1,   1, // PC_FNMSUBS
    FD,           4,   1,   1,  1,   1, // PC_FRES
    FD,           4,   1,   1,  1,   1, // PC_FRSQRTE
    FD,           4,   1,   1,  1,   1, // PC_FSEL
    FD,           4,   1,   1,  1,   1, // PC_FRSP
    FD,           4,   1,   1,  1,   1, // PC_FCTIW
    FD,           4,   1,   1,  1,   1, // PC_FCTIWZ
    FD,           6,   1,   1,  1,   1, // PC_FCMPU
    FD,           6,   1,   1,  1,   1, // PC_FCMPO
    IU,           0,   0,   0,  0,   0, // PC_LWARX
    IU,           0,   0,   0,  0,   0, // PC_LSWI
    IU,           0,   0,   0,  0,   0, // PC_LSWX
    IU,           0,   0,   0,  0,   0, // PC_STFIWX
    IU,           0,   0,   0,  0,   0, // PC_STSWI
    IU,           0,   0,   0,  0,   0, // PC_STSWX
    IU,           0,   0,   0,  0,   0, // PC_STWCX
    IU,           0,   0,   0,  0,   0, // PC_ECIWX
    IU,           0,   0,   0,  0,   0, // PC_ECOWX
    IU,           0,   0,   0,  0,   0, // PC_DCBI
    IU,           0,   0,   0,  0,   0, // PC_ICBI
    IU,           0,   0,   0,  0,   0, // PC_MCRFS
    IU,           0,   0,   0,  0,   0, // PC_MCRXR
    IU,           0,   0,   0,  0,   0, // PC_MFTB
    IU,           0,   0,   0,  0,   0, // PC_MFSR
    IU,           0,   0,   0,  0,   0, // PC_MTSR
    IU,           0,   0,   0,  0,   0, // PC_MFSRIN
    IU,           0,   0,   0,  0,   0, // PC_MTSRIN
    IU,           0,   0,   0,  0,   0, // PC_MTFSB0
    IU,           0,   0,   0,  0,   0, // PC_MTFSB1
    IU,           0,   0,   0,  0,   0, // PC_MTFSFI
    Serialize,    0,   0,   0,  0,   0, // PC_SC
    IU,           0,   0,   0,  0,   0, // PC_FSQRT
    IU,           0,   0,   0,  0,   0, // PC_FSQRTS
    IU,           0,   0,   0,  0,   0, // PC_TLBIA
    IU,           0,   0,   0,  0,   0, // PC_TLBIE
    IU,           0,   0,   0,  0,   0, // PC_TLBLD
    IU,           0,   0,   0,  0,   0, // PC_TLBLI
    IU,           0,   0,   0,  0,   0, // PC_TLBSYNC
    Serialize,    0,   0,   0,  0,   0, // PC_TW
    Serialize,    0,   0,   0,  0,   0, // PC_TRAP
    Serialize,    0,   0,   0,  0,   0, // PC_TWI
    Serialize,    0,   0,   0,  0,   0, // PC_OPWORD
    IU,           0,   0,   0,  0,   0, // PC_MFROM
    IU,           0,   0,   0,  0,   0, // PC_DSA
    IU,           0,   0,   0,  0,   0, // PC_ESA
    IU,           0,   0,   0,  0,   0, // PC_DCCCI
    IU,           0,   0,   0,  0,   0, // PC_DCREAD
    IU,           0,   0,   0,  0,   0, // PC_ICBT
    IU,           0,   0,   0,  0,   0, // PC_ICCCI
    IU,           0,   0,   0,  0,   0, // PC_ICREAD
    IU,           0,   0,   0,  0,   0, // PC_RFCI
    IU,           0,   0,   0,  0,   0, // PC_TLBRE
    IU,           0,   0,   0,  0,   0, // PC_TLBSX
    IU,           0,   0,   0,  0,   0, // PC_TLBWE
    IU,           0,   0,   0,  0,   0, // PC_WRTEE
    IU,           0,   0,   0,  0,   0, // PC_WRTEEI
    IU,           0,   0,   0,  0,   0, // PC_MFDCR
    IU,           0,   0,   0,  0,   0, // PC_MTDCR
    Unsupported,  0,   0,   0,  0,   0, // PC_DCBA
    Unsupported,  0,   0,   0,  0,   0, // PC_ISELGT
    Unsupported,  0,   0,   0,  0,   0, // PC_ISELEQ
    Unsupported,  0,   0,   0,  0,   0, // PC_ISELLT
    Unsupported,  0,   0,   0,  0,   0, // PC_DSS
    Unsupported,  0,   0,   0,  0,   0, // PC_DSSALL
    Unsupported,  0,   0,   0,  0,   0, // PC_DST
    Unsupported,  0,   0,   0,  0,   0, // PC_DSTT
    Unsupported,  0,   0,   0,  0,   0, // PC_DSTST
    Unsupported,  0,   0,   0,  0,   0, // PC_DSTSTT
    Unsupported,  0,   0,   0,  0,   0, // PC_LVEBX
    Unsupported,  0,   0,   0,  0,   0, // PC_LVEHX
    Unsupported,  0,   0,   0,  0,   0, // PC_LVEWX
    Unsupported,  0,   0,   0,  0,   0, // PC_LVSL
    Unsupported,  0,   0,   0,  0,   0, // PC_LVSR
    Unsupported,  0,   0,   0,  0,   0, // PC_LVX
    Unsupported,  0,   0,   0,  0,   0, // PC_LVXL
    Unsupported,  0,   0,   0,  0,   0, // PC_STVEBX
    Unsupported,  0,   0,   0,  0,   0, // PC_STVEHX
    Unsupported,  0,   0,   0,  0,   0, // PC_STVEWX
    Unsupported,  0,   0,   0,  0,   0, // PC_STVX
    Unsupported,  0,   0,   0,  0,   0, // PC_STVXL
    Unsupported,  0,   0,   0,  0,   0, // PC_MFVSCR
    Unsupported,  0,   0,   0,  0,   0, // PC_MTVSCR
    Unsupported,  0,   0,   0,  0,   0, // PC_VADDCUW
    Unsupported,  0,   0,   0,  0,   0, // PC_VADDFP
    Unsupported,  0,   0,   0,  0,   0, // PC_VADDSBS
    Unsupported,  0,   0,   0,  0,   0, // PC_VADDSHS
    Unsupported,  0,   0,   0,  0,   0, // PC_VADDSWS
    Unsupported,  0,   0,   0,  0,   0, // PC_VADDUBM
    Unsupported,  0,   0,   0,  0,   0, // PC_VADDUBS
    Unsupported,  0,   0,   0,  0,   0, // PC_VADDUHM
    Unsupported,  0,   0,   0,  0,   0, // PC_VADDUHS
    Unsupported,  0,   0,   0,  0,   0, // PC_VADDUWM
    Unsupported,  0,   0,   0,  0,   0, // PC_VADDUWS
    Unsupported,  0,   0,   0,  0,   0, // PC_VAND
    Unsupported,  0,   0,   0,  0,   0, // PC_VANDC
    Unsupported,  0,   0,   0,  0,   0, // PC_VAVGSB
    Unsupported,  0,   0,   0,  0,   0, // PC_VAVGSH
    Unsupported,  0,   0,   0,  0,   0, // PC_VAVGSW
    Unsupported,  0,   0,   0,  0,   0, // PC_VAVGUB
    Unsupported,  0,   0,   0,  0,   0, // PC_VAVGUH
    Unsupported,  0,   0,   0,  0,   0, // PC_VAVGUW
    Unsupported,  0,   0,   0,  0,   0, // PC_VCFSX
    Unsupported,  0,   0,   0,  0,   0, // PC_VCFUX
    Unsupported,  0,   0,   0,  0,   0, // PC_VCMPBFP
    Unsupported,  0,   0,   0,  0,   0, // PC_VCMPEQFP
    Unsupported,  0,   0,   0,  0,   0, // PC_VCMPEQUB
    Unsupported,  0,   0,   0,  0,   0, // PC_VCMPEQUH
    Unsupported,  0,   0,   0,  0,   0, // PC_VCMPEQUW
    Unsupported,  0,   0,   0,  0,   0, // PC_VCMPGEFP
    Unsupported,  0,   0,   0,  0,   0, // PC_VCMPGTFP
    Unsupported,  0,   0,   0,  0,   0, // PC_VCMPGTSB
    Unsupported,  0,   0,   0,  0,   0, // PC_VCMPGTSH
    Unsupported,  0,   0,   0,  0,   0, // PC_VCMPGTSW
    Unsupported,  0,   0,   0,  0,   0, // PC_VCMPGTUB
    Unsupported,  0,   0,   0,  0,   0, // PC_VCMPGTUH
    Unsupported,  0,   0,   0,  0,   0, // PC_VCMPGTUW
    Unsupported,  0,   0,   0,  0,   0, // PC_VCTSXS
    Unsupported,  0,   0,   0,  0,   0, // PC_VCTUXS
    Unsupported,  0,   0,   0,  0,   0, // PC_VEXPTEFP
    Unsupported,  0,   0,   0,  0,   0, // PC_VLOGEFP
    Unsupported,  0,   0,   0,  0,   0, // PC_VMAXFP
    Unsupported,  0,   0,   0,  0,   0, // PC_VMAXSB
    Unsupported,  0,   0,   0,  0,   0, // PC_VMAXSH
    Unsupported,  0,   0,   0,  0,   0, // PC_VMAXSW
    Unsupported,  0,   0,   0,  0,   0, // PC_VMAXUB
    Unsupported,  0,   0,   0,  0,   0, // PC_VMAXUH
    Unsupported,  0,   0,   0,  0,   0, // PC_VMAXUW
    Unsupported,  0,   0,   0,  0,   0, // PC_VMINFP
    Unsupported,  0,   0,   0,  0,   0, // PC_VMINSB
    Unsupported,  0,   0,   0,  0,   0, // PC_VMINSH
    Unsupported,  0,   0,   0,  0,   0, // PC_VMINSW
    Unsupported,  0,   0,   0,  0,   0, // PC_VMINUB
    Unsupported,  0,   0,   0,  0,   0, // PC_VMINUH
    Unsupported,  0,   0,   0,  0,   0, // PC_VMINUW
    Unsupported,  0,   0,   0,  0,   0, // PC_VMRGHB
    Unsupported,  0,   0,   0,  0,   0, // PC_VMRGHH
    Unsupported,  0,   0,   0,  0,   0, // PC_VMRGHW
    Unsupported,  0,   0,   0,  0,   0, // PC_VMRGLB
    Unsupported,  0,   0,   0,  0,   0, // PC_VMRGLH
    Unsupported,  0,   0,   0,  0,   0, // PC_VMRGLW
    Unsupported,  0,   0,   0,  0,   0, // PC_VMULESB
    Unsupported,  0,   0,   0,  0,   0, // PC_VMULESH
    Unsupported,  0,   0,   0,  0,   0, // PC_VMULEUB
    Unsupported,  0,   0,   0,  0,   0, // PC_VMULEUH
    Unsupported,  0,   0,   0,  0,   0, // PC_VMULOSB
    Unsupported,  0,   0,   0,  0,   0, // PC_VMULOSH
    Unsupported,  0,   0,   0,  0,   0, // PC_VMULOUB
    Unsupported,  0,   0,   0,  0,   0, // PC_VMULOUH
    Unsupported,  0,   0,   0,  0,   0, // PC_VNOR
    Unsupported,  0,   0,   0,  0,   0, // PC_VOR
    Unsupported,  0,   0,   0,  0,   0, // PC_VPKPX
    Unsupported,  0,   0,   0,  0,   0, // PC_VPKSHSS
    Unsupported,  0,   0,   0,  0,   0, // PC_VPKSHUS
    Unsupported,  0,   0,   0,  0,   0, // PC_VPKSWSS
    Unsupported,  0,   0,   0,  0,   0, // PC_VPKSWUS
    Unsupported,  0,   0,   0,  0,   0, // PC_VPKUHUM
    Unsupported,  0,   0,   0,  0,   0, // PC_VPKUHUS
    Unsupported,  0,   0,   0,  0,   0, // PC_VPKUWUM
    Unsupported,  0,   0,   0,  0,   0, // PC_VPKUWUS
    Unsupported,  0,   0,   0,  0,   0, // PC_VREFP
    Unsupported,  0,   0,   0,  0,   0, // PC_VRFIM
    Unsupported,  0,   0,   0,  0,   0, // PC_VRFIN
    Unsupported,  0,   0,   0,  0,   0, // PC_VRFIP
    Unsupported,  0,   0,   0,  0,   0, // PC_VRFIZ
    Unsupported,  0,   0,   0,  0,   0, // PC_VRLB
    Unsupported,  0,   0,   0,  0,   0, // PC_VRLH
    Unsupported,  0,   0,   0,  0,   0, // PC_VRLW
    Unsupported,  0,   0,   0,  0,   0, // PC_VRSQRTEFP
    Unsupported,  0,   0,   0,  0,   0, // PC_VSL
    Unsupported,  0,   0,   0,  0,   0, // PC_VSLB
    Unsupported,  0,   0,   0,  0,   0, // PC_VSLH
    Unsupported,  0,   0,   0,  0,   0, // PC_VSLO
    Unsupported,  0,   0,   0,  0,   0, // PC_VSLW
    Unsupported,  0,   0,   0,  0,   0, // PC_VSPLTB
    Unsupported,  0,   0,   0,  0,   0, // PC_VSPLTH
    Unsupported,  0,   0,   0,  0,   0, // PC_VSPLTW
    Unsupported,  0,   0,   0,  0,   0, // PC_VSPLTISB
    Unsupported,  0,   0,   0,  0,   0, // PC_VSPLTISH
    Unsupported,  0,   0,   0,  0,   0, // PC_VSPLTISW
    Unsupported,  0,   0,   0,  0,   0, // PC_VSR
    Unsupported,  0,   0,   0,  0,   0, // PC_VSRAB
    Unsupported,  0,   0,   0,  0,   0, // PC_VSRAH
    Unsupported,  0,   0,   0,  0,   0, // PC_VSRAW
    Unsupported,  0,   0,   0,  0,   0, // PC_VSRB
    Unsupported,  0,   0,   0,  0,   0, // PC_VSRH
    Unsupported,  0,   0,   0,  0,   0, // PC_VSRO
    Unsupported,  0,   0,   0,  0,   0, // PC_VSRW
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUBCUW
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUBFP
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUBSBS
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUBSHS
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUBSWS
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUBUBM
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUBUBS
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUBUHM
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUBUHS
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUBUWM
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUBUWS
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUMSWS
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUM2SWS
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUM4SBS
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUM4SHS
    Unsupported,  0,   0,   0,  0,   0, // PC_VSUM4UBS
    Unsupported,  0,   0,   0,  0,   0, // PC_VUPKHPX
    Unsupported,  0,   0,   0,  0,   0, // PC_VUPKHSB
    Unsupported,  0,   0,   0,  0,   0, // PC_VUPKHSH
    Unsupported,  0,   0,   0,  0,   0, // PC_VUPKLPX
    Unsupported,  0,   0,   0,  0,   0, // PC_VUPKLSB
    Unsupported,  0,   0,   0,  0,   0, // PC_VUPKLSH
    Unsupported,  0,   0,   0,  0,   0, // PC_VXOR
    Unsupported,  0,   0,   0,  0,   0, // PC_VMADDFP
    Unsupported,  0,   0,   0,  0,   0, // PC_VMHADDSHS
    Unsupported,  0,   0,   0,  0,   0, // PC_VMHRADDSHS
    Unsupported,  0,   0,   0,  0,   0, // PC_VMLADDUHM
    Unsupported,  0,   0,   0,  0,   0, // PC_VMSUMMBM
    Unsupported,  0,   0,   0,  0,   0, // PC_VMSUMSHM
    Unsupported,  0,   0,   0,  0,   0, // PC_VMSUMSHS
    Unsupported,  0,   0,   0,  0,   0, // PC_VMSUMUBM
    Unsupported,  0,   0,   0,  0,   0, // PC_VMSUMUHM
    Unsupported,  0,   0,   0,  0,   0, // PC_VMSUMUHS
    Unsupported,  0,   0,   0,  0,   0, // PC_VNMSUBFP
    Unsupported,  0,   0,   0,  0,   0, // PC_VPERM
    Unsupported,  0,   0,   0,  0,   0, // PC_VSEL
    Unsupported,  0,   0,   0,  0,   0, // PC_VSLDOI
    Unsupported,  0,   0,   0,  0,   0, // PC_VMR
    Unsupported,  0,   0,   0,  0,   0, // PC_VMRP
    Unsupported,  0,   0,   0,  0,   0, // PC_PSQ_L
    Unsupported,  0,   0,   0,  0,   0, // PC_PSQ_LU
    Unsupported,  0,   0,   0,  0,   0, // PC_PSQ_LX
    Unsupported,  0,   0,   0,  0,   0, // PC_PSQ_LUX
    Unsupported,  0,   0,   0,  0,   0, // PC_PSQ_ST
    Unsupported,  0,   0,   0,  0,   0, // PC_PSQ_STU
    Unsupported,  0,   0,   0,  0,   0, // PC_PSQ_STX
    Unsupported,  0,   0,   0,  0,   0, // PC_PSQ_STUX
    Unsupported,  0,   0,   0,  0,   0, // PC_DCBZ_L
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_ADD
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_SUB
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_MUL
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_DIV
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_MADD
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_MSUB
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_NMADD
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_NMSUB
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_RES
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_SEL
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_ABS
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_NABS
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_NEG
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_MR
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_RSQRTE
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_CMPU0
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_CMPO0
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_CMPU1
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_CMPO1
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_MERGE00
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_MERGE01
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_MERGE10
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_MERGE11
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_SUM0
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_SUM1
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_MULS0
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_MULS1
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_MADDS0
    Unsupported,  0,   0,   0,  0,   0, // PC_PS_MADDS1
    IU,           0,   0,   0,  0,   0, // PC_SLE
    IU,           0,   0,   0,  0,   0, // PC_SLEQ
    IU,           0,   0,   0,  0,   0, // PC_SLIQ
    IU,           0,   0,   0,  0,   0, // PC_SLLIQ
    IU,           0,   0,   0,  0,   0, // PC_SLLQ
    IU,           0,   0,   0,  0,   0, // PC_SLQ
    IU,           0,   0,   0,  0,   0, // PC_SRAIQ
    IU,           0,   0,   0,  0,   0, // PC_SRAQ
    IU,           0,   0,   0,  0,   0, // PC_SRE
    IU,           0,   0,   0,  0,   0, // PC_SREA
    IU,           0,   0,   0,  0,   0, // PC_SREQ
    IU,           0,   0,   0,  0,   0, // PC_SRIQ
    IU,           0,   0,   0,  0,   0, // PC_SRLIQ
    IU,           0,   0,   0,  0,   0, // PC_SRLQ
    IU,           0,   0,   0,  0,   0, // PC_SRQ
    IU,           0,   0,   0,  0,   0, // PC_MASKG
    IU,           0,   0,   0,  0,   0, // PC_MASKIR
    IU,           0,   0,   0,  0,   0, // PC_LSCBX
    IU,           0,   0,   0,  0,   0, // PC_DIV
    IU,           0,   0,   0,  0,   0, // PC_DIVS
    IU,           0,   0,   0,  0,   0, // PC_DOZ
    IU,           0,   0,   0,  0,   0, // PC_MUL
    IU,           0,   0,   0,  0,   0, // PC_NABS
    IU,           0,   0,   0,  0,   0, // PC_ABS
    IU,           0,   0,   0,  0,   0, // PC_CLCS
    IU,           0,   0,   0,  0,   0, // PC_DOZI
    IU,           0,   0,   0,  0,   0, // PC_RLMI
    IU,           0,   0,   0,  0,   0, // PC_RRIB
};
// clang-format on

static void advance(int stageCount, int oldStage, int newStage) {
    PCode* instr = pipeline[oldStage].instr;
    int cycles = instruction_timing[instr->op].cycles[newStage - stageCount];
    pipeline[newStage].instr = instr;
    pipeline[newStage].remaining = cycles;
    pipeline[oldStage].instr = NULL;
}

static void complete_instruction(int stage) {
    pipeline[stage].instr = NULL;
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

    for (stage = 0; stage < NumStages; stage++) {
        pipeline[stage].instr = NULL;
    }
}

static int can_issue(PCode* instr) {
    int stage = instruction_timing[instr->op].stage;
    if (stage == Serialize) {
        stage = IU;
    }
    if (pipeline[stage].instr) {
        return FALSE;
    }
    return TRUE;
}

static void issue(PCode* instr) {
    int stage = instruction_timing[instr->op].stage;
    int cycles = instruction_timing[instr->op].cycles[IU];
    if (stage == Serialize) {
        stage = IU;
    }
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

    if (pipeline[IU].instr && pipeline[IU].remaining == 0) {
        complete_instruction(IU);
    }
    if (pipeline[FWA].instr && pipeline[FWA].remaining == 0) {
        complete_instruction(FWA);
    }
    if (pipeline[BPU].instr && pipeline[BPU].remaining == 0) {
        complete_instruction(BPU);
    }

    if (pipeline[FPA].instr && pipeline[FPA].remaining == 0 && !pipeline[FWA].instr) {
        advance(1, FPA, FWA);
    }
    if (pipeline[FPM].instr && pipeline[FPM].remaining == 0 && !pipeline[FPA].instr) {
        advance(1, FPM, FPA);
    }
    if (pipeline[FD].instr && pipeline[FD].remaining == 0 && !pipeline[FPM].instr) {
        advance(1, FD, FPM);
    }
}

static int serializes(PCode* instr) {
    int stage = instruction_timing[instr->op].stage;
    return stage == Serialize;
}
