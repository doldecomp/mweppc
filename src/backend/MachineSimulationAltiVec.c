// MachineSimulationAltiVec.c: the scheduler's model of the MPC7450
#include "compiler/CError.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Scheduler.h"

/* The model's statics: every machine model has these names, so each prefixes its own */
#define latency m7450_latency
#define initialize m7450_initialize
#define can_issue m7450_can_issue
#define issue m7450_issue
#define advance_clock m7450_advance_clock
#define serializes m7450_serializes
#define instruction_timing m7450_instruction_timing
#define completionbuffers m7450_completionbuffers

// https://www.nxp.com/docs/en/reference-manual/MPC7450UM.pdf

// (the names are the original's, from its table of the stages' names, stage_names)
typedef enum Stage {
    BPU, // Branch Prediction Unit

    CFX, // Multiple-Cycle Integer Unit
    CFX2,
    CFX3,

    SFX1, // Single-Cycle Integer Units
    SFX2,
    SFX3,

    LSU, // Load/Store Unit
    LSU2,
    LSU3,
    LSU4,

    FPU, // Floating-Point Unit
    FPU2,
    FPU3,
    FPU4,

    VECTOR, // Vector Simple Integer Unit

    V_PERM, // Vector Permute Unit
    V_PERM2,

    V_CFX, // Vector Complex Integer Unit
    V_CFX2,
    V_CFX3,
    V_CFX4,

    V_FPU, // Vector Floating-Point Unit
    V_FPU2,
    V_FPU3,
    V_FPU4,

    NumStages
} Stage;

// a floating-point instruction finished this cycle
static int fpu_completed; // NOTE: invented name

static struct {
    // the instruction currently in this pipeline stage
    PCode* instr;

    // how many cycles are left for this instruction to finish
    int remaining;
#ifdef __MWERKS__
} pipeline_altivec[NumStages];
#else
    // instruction_timing's rows past PC_PSQ_L (below) give stages up to 144: the original's slots for
    // them are other models' completion buffers, which nothing else touches while the MPC7450 is the
    // model (a #pragma scheduling that changes models within a file shares them in the original)
} pipeline_altivec[145];
#endif

// NOTE: invented names: InstrQueue, Dispatched, FIQ, GIQ, VIQ, FPIssued, GPRIssued, VRIssued,
// NumQueues (after the 7450 manual's queues)
// The dispatch and issue model's counters
enum {
    InstrQueue, // fetched instructions waiting for dispatch
    Dispatched, // instructions dispatched to the issue queues and not yet issued
    FIQ, // floating-point issue queue entries ready to issue (at most 2)
    GIQ, // general-purpose issue queue entries ready to issue (at most 6)
    VIQ, // vector issue queue entries ready to issue (at most 4)
    FPIssued, // floating-point instructions issued this cycle (1 at most)
    GPRIssued, // integer and load/store instructions issued this cycle (3 at most)
    VRIssued, // vector instructions issued this cycle (2 at most)
    NumQueues
};

static int fetchqueues[NumQueues];

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

// the stages' names (nothing reads them)
// clang-format off
static char* stage_names[NumStages] = { // NOTE: invented name
    "BPU", "CFX", "CFX2", "CFX3", "SFX1", "SFX2", "SFX3", "LSU", "LSU2", "LSU3", "LSU4", "FPU", "FPU2",
    "FPU3", "FPU4", "VECTOR", "V_PERM", "V_PERM2", "V_CFX", "V_CFX2", "V_CFX3", "V_CFX4", "V_FPU",
    "V_FPU2", "V_FPU3", "V_FPU4"
};
// clang-format on

static int latency(PCode* instr);
static void initialize(void);
static int can_issue(PCode* instr);
static void issue(PCode* instr);
static void advance_clock(void);
static int serializes(PCode* instr);
static int uses_vpermute_unit_altivec(PCode* instr);

// clang-format off
MachineInfo machine7450 = {
    6,
    TRUE,
    4,
    &latency,
    &initialize,
    &can_issue,
    &issue,
    &advance_clock,
    &serializes,
    &uses_vpermute_unit_altivec
};
// clang-format on

// clang-format off
static Stage finalstages[12] = {
    BPU, CFX3, SFX1, SFX2, SFX3, LSU4, FPU4, VECTOR, V_PERM2, V_CFX4, V_FPU4
};
// clang-format on

enum {
    NumPipelineUnits = 6
};
// clang-format off
static struct {
    Stage start, end;
} pipeline_units[NumPipelineUnits] = {
    CFX, CFX3,
    LSU, LSU4,
    FPU, FPU4,
    V_PERM, V_PERM2,
    V_CFX, V_CFX4,
    V_FPU, V_FPU4
};
// clang-format on

static struct {
    short index;

    // the initial stage for this instruction
    Stage stage;

    // the total amount of cycles required by this instruction
    char latency;

    // how long it takes to finish each stage
    char cycles[4];

    // does this instruction serialise?
    char serializes;

    char unused;
    // clang-format off
} instruction_timing[] = {
    0, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_B
    1, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BL
    2, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BC
    3, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BCLR
    4, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BCCTR
    5, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BT
    6, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BTLR
    7, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BTCTR
    8, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BF
    9, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BFLR
    10, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BFCTR
    11, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BDNZ
    12, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BDNZT
    13, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BDNZF
    14, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BDZ
    15, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BDZT
    16, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BDZF
    17, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BLR
    18, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BCTR
    19, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BCTRL
    20, BPU,      0,  0,  0,  0,  0, 0, 0, // PC_BLRL
    21, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LBZ
    22, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LBZU
    23, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LBZX
    24, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LBZUX
    25, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LHZ
    26, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LHZU
    27, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LHZX
    28, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LHZUX
    29, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LHA
    30, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LHAU
    31, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LHAX
    32, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LHAUX
    33, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LHBRX
    34, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LWZ
    35, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LWZU
    36, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LWZX
    37, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LWZUX
    38, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LWBRX
    39, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_LMW
    40, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STB
    41, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STBU
    42, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STBX
    43, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STBUX
    44, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STH
    45, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STHU
    46, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STHX
    47, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STHUX
    48, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STHBRX
    49, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STW
    50, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STWU
    51, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STWX
    52, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STWUX
    53, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STWBRX
    54, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_STMW
    55, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_DCBF
    56, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_DCBST
    57, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_DCBT
    58, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_DCBTST
    59, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_DCBZ
    60, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ADD
    61, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ADDC
    62, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ADDE
    63, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ADDI
    64, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ADDIC
    65, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ADDICR
    66, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ADDIS
    67, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ADDME
    68, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ADDZE
    69, CFX,     23, 23,  0,  0,  0, 0, 0, // PC_DIVW
    70, CFX,     23, 23,  0,  0,  0, 0, 0, // PC_DIVWU
    71, CFX,      4,  2,  2,  0,  0, 0, 0, // PC_MULHW
    72, CFX,      4,  2,  2,  0,  0, 0, 0, // PC_MULHWU
    73, CFX,      3,  1,  1,  1,  0, 0, 0, // PC_MULLI
    74, CFX,      4,  2,  2,  0,  0, 0, 0, // PC_MULLW
    75, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_NEG
    76, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_SUBF
    77, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_SUBFC
    78, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_SUBFE
    79, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_SUBFIC
    80, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_SUBFME
    81, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_SUBFZE
    82, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_CMPI
    83, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_CMP
    84, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_CMPLI
    85, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_CMPL
    86, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ANDI
    87, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ANDIS
    88, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ORI
    89, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ORIS
    90, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_XORI
    91, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_XORIS
    92, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_AND
    93, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_OR
    94, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_XOR
    95, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_NAND
    96, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_NOR
    97, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_EQV
    98, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ANDC
    99, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ORC
    100, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_EXTSB
    101, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_EXTSH
    102, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_CNTLZW
    103, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_RLWINM
    104, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_RLWNM
    105, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_RLWIMI
    106, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_SLW
    107, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_SRW
    108, SFX1,     2,  2,  0,  0,  0, 0, 0, // PC_SRAWI
    109, SFX1,     2,  2,  0,  0,  0, 0, 0, // PC_SRAW
    110, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_CRAND
    111, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_CRANDC
    112, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_CREQV
    113, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_CRNAND
    114, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_CRNOR
    115, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_CROR
    116, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_CRORC
    117, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_CRXOR
    118, CFX,      1,  1,  0,  0,  0, 1, 0, // PC_MCRF
    119, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_MTXER
    120, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_MTCTR
    121, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_MTLR
    122, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_MTCRF
    123, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_MTMSR
    124, CFX,      3,  3,  0,  0,  0, 1, 0, // PC_MTSPR
    125, CFX,      3,  2,  1,  0,  0, 0, 0, // PC_MFMSR
    126, CFX,      3,  3,  0,  0,  0, 1, 0, // PC_MFSPR
    127, CFX,      1,  1,  0,  0,  0, 1, 0, // PC_MFXER
    128, CFX,      1,  1,  0,  0,  0, 1, 0, // PC_MFCTR
    129, CFX,      1,  1,  0,  0,  0, 1, 0, // PC_MFLR
    130, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_MFCR
    131, FPU,      5,  5,  0,  0,  0, 1, 0, // PC_MFFS
    132, FPU,      5,  5,  0,  0,  0, 1, 0, // PC_MTFSF
    133, LSU,      3,  3,  0,  0,  0, 1, 0, // PC_EIEIO
    134, CFX,      1,  1,  0,  0,  0, 1, 0, // PC_ISYNC
    135, LSU,     35, 35,  0,  0,  0, 1, 0, // PC_SYNC
    136, CFX,      1,  1,  0,  0,  0, 1, 0, // PC_RFI
    137, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_LI
    138, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_LIS
    139, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_MR
    140, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_NOP
    141, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_NOT
    142, LSU,      4,  1,  1,  1,  1, 0, 0, // PC_LFS
    143, LSU,      4,  1,  1,  1,  1, 0, 0, // PC_LFSU
    144, LSU,      4,  1,  1,  1,  1, 0, 0, // PC_LFSX
    145, LSU,      4,  1,  1,  1,  1, 0, 0, // PC_LFSUX
    146, LSU,      4,  1,  1,  1,  1, 0, 0, // PC_LFD
    147, LSU,      4,  1,  1,  1,  1, 0, 0, // PC_LFDU
    148, LSU,      4,  1,  1,  1,  1, 0, 0, // PC_LFDX
    149, LSU,      4,  1,  1,  1,  1, 0, 0, // PC_LFDUX
    150, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_STFS
    151, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_STFSU
    152, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_STFSX
    153, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_STFSUX
    154, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_STFD
    155, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_STFDU
    156, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_STFDX
    157, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_STFDUX
    158, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FMR
    159, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FABS
    160, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FNEG
    161, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FNABS
    162, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FADD
    163, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FADDS
    164, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FSUB
    165, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FSUBS
    166, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FMUL
    167, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FMULS
    168, FPU,     35, 35,  0,  0,  0, 0, 0, // PC_FDIV
    169, FPU,     21, 21,  0,  0,  0, 0, 0, // PC_FDIVS
    170, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FMADD
    171, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FMADDS
    172, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FMSUB
    173, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FMSUBS
    174, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FNMADD
    175, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FNMADDS
    176, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FNMSUB
    177, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FNMSUBS
    178, FPU,     14, 14,  0,  0,  0, 0, 0, // PC_FRES
    179, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FRSQRTE
    180, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FSEL
    181, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FRSP
    182, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FCTIW
    183, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FCTIWZ
    184, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FCMPU
    185, FPU,      5,  1,  1,  1,  2, 0, 0, // PC_FCMPO
    186, LSU,      3,  3,  0,  0,  0, 1, 0, // PC_LWARX
    187, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LSWI
    188, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_LSWX
    189, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STFIWX
    190, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_STSWI
    191, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_STSWX
    192, LSU,      3,  1,  1,  1,  0, 1, 0, // PC_STWCX
    193, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_ECIWX
    194, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_ECOWX
    195, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_DCBI
    196, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_ICBI
    197, CFX,      5,  5,  0,  0,  0, 1, 0, // PC_MCRFS
    198, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_MCRXR
    199, CFX,      5,  5,  0,  0,  0, 0, 0, // PC_MFTB
    200, CFX,      4,  1,  3,  0,  0, 0, 0, // PC_MFSR
    201, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_MTSR
    202, CFX,      4,  1,  3,  0,  0, 0, 0, // PC_MFSRIN
    203, CFX,      2,  2,  0,  0,  0, 1, 0, // PC_MTSRIN
    204, FPU,      5,  5,  0,  0,  0, 1, 0, // PC_MTFSB0
    205, FPU,      5,  5,  0,  0,  0, 1, 0, // PC_MTFSB1
    206, FPU,      5,  5,  0,  0,  0, 0, 0, // PC_MTFSFI
    207, CFX,      1,  1,  0,  0,  0, 1, 0, // PC_SC
    208, FPU,      1,  1,  0,  0,  0, 0, 0, // PC_FSQRT
    209, FPU,      1,  1,  0,  0,  0, 0, 0, // PC_FSQRTS
    210, LSU,      1,  1,  0,  0,  0, 0, 0, // PC_TLBIA
    211, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_TLBIE
    212, LSU,      3,  3,  0,  0,  0, 0, 0, // PC_TLBLD
    213, LSU,      3,  3,  0,  0,  0, 1, 0, // PC_TLBLI
    214, LSU,      3,  3,  0,  0,  0, 1, 0, // PC_TLBSYNC
    215, SFX1,     2,  2,  0,  0,  0, 0, 0, // PC_TW
    216, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_TRAP
    217, SFX1,     2,  2,  0,  0,  0, 0, 0, // PC_TWI
    218, SFX1,     1,  1,  0,  0,  0, 1, 0, // PC_OPWORD
    219, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_MFROM
    220, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_DSA
    221, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ESA
    222, SFX1,     1,  0,  0,  0,  0, 0, 0, // PC_DCCCI
    223, SFX1,     1,  0,  0,  0,  0, 0, 0, // PC_DCREAD
    224, SFX1,     1,  0,  0,  0,  0, 0, 0, // PC_ICBT
    225, SFX1,     1,  0,  0,  0,  0, 0, 0, // PC_ICCCI
    226, SFX1,     1,  0,  0,  0,  0, 0, 0, // PC_ICREAD
    227, SFX1,     1,  0,  0,  0,  0, 0, 0, // PC_RFCI
    228, SFX1,     1,  0,  0,  0,  0, 0, 0, // PC_TLBRE
    229, SFX1,     1,  0,  0,  0,  0, 0, 0, // PC_TLBSX
    230, SFX1,     1,  0,  0,  0,  0, 0, 0, // PC_TLBWE
    231, SFX1,     1,  0,  0,  0,  0, 0, 0, // PC_WRTEE
    232, SFX1,     1,  0,  0,  0,  0, 0, 0, // PC_WRTEEI
    233, SFX1,     1,  0,  0,  0,  0, 0, 0, // PC_MFDCR
    234, SFX1,     1,  0,  0,  0,  0, 0, 0, // PC_MTDCR
    235, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_DCBA
    236, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ISELGT
    237, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ISELEQ
    238, SFX1,     1,  1,  0,  0,  0, 0, 0, // PC_ISELLT
    239, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_DSS
    240, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_DSSALL
    241, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_DST
    242, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_DSTT
    243, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_DSTST
    244, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_DSTSTT
    245, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LVEBX
    246, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LVEHX
    247, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LVEWX
    248, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LVSL
    249, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LVSR
    250, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LVX
    251, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_LVXL
    252, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STVEBX
    253, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STVEHX
    254, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STVEWX
    255, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STVX
    256, LSU,      3,  1,  1,  1,  0, 0, 0, // PC_STVXL
    257, V_FPU,    2,  2,  0,  0,  0, 1, 0, // PC_MFVSCR
    258, V_FPU,    2,  2,  0,  0,  0, 1, 0, // PC_MTVSCR
    259, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VADDCUW
    260, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VADDFP
    261, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VADDSBS
    262, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VADDSHS
    263, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VADDSWS
    264, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VADDUBM
    265, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VADDUBS
    266, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VADDUHM
    267, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VADDUHS
    268, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VADDUWM
    269, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VADDUWS
    270, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VAND
    271, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VANDC
    272, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VAVGSB
    273, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VAVGSH
    274, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VAVGSW
    275, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VAVGUB
    276, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VAVGUH
    277, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VAVGUW
    278, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VCFSX
    279, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VCFUX
    280, V_FPU,    2,  1,  1,  0,  0, 0, 0, // PC_VCMPBFP
    281, V_FPU,    2,  1,  1,  0,  0, 0, 0, // PC_VCMPEQFP
    282, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VCMPEQUB
    283, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VCMPEQUH
    284, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VCMPEQUW
    285, V_FPU,    2,  1,  1,  0,  0, 0, 0, // PC_VCMPGEFP
    286, V_FPU,    2,  1,  1,  0,  0, 0, 0, // PC_VCMPGTFP
    287, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VCMPGTSB
    288, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VCMPGTSH
    289, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VCMPGTSW
    290, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VCMPGTUB
    291, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VCMPGTUH
    292, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VCMPGTUW
    293, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VCTSXS
    294, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VCTUXS
    295, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VEXPTEFP
    296, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VLOGEFP
    297, V_FPU,    2,  1,  1,  0,  0, 0, 0, // PC_VMAXFP
    298, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VMAXSB
    299, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VMAXSH
    300, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VMAXSW
    301, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VMAXUB
    302, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VMAXUH
    303, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VMAXUW
    304, V_FPU,    2,  1,  1,  0,  0, 0, 0, // PC_VMINFP
    305, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VMINSB
    306, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VMINSH
    307, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VMINSW
    308, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VMINUB
    309, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VMINUH
    310, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VMINUW
    311, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VMRGHB
    312, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VMRGHH
    313, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VMRGHW
    314, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VMRGLB
    315, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VMRGLH
    316, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VMRGLW
    317, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMULESB
    318, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMULESH
    319, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMULEUB
    320, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMULEUH
    321, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMULOSB
    322, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMULOSH
    323, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMULOUB
    324, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMULOUH
    325, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VNOR
    326, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VOR
    327, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VPKPX
    328, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VPKSHSS
    329, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VPKSHUS
    330, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VPKSWSS
    331, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VPKSWUS
    332, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VPKUHUM
    333, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VPKUHUS
    334, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VPKUWUM
    335, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VPKUWUS
    336, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VREFP
    337, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VRFIM
    338, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VRFIN
    339, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VRFIP
    340, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VRFIZ
    341, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VRLB
    342, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VRLH
    343, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VRLW
    344, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VRSQRTEFP
    345, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VSL
    346, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSLB
    347, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSLH
    348, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VSLO
    349, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSLW
    350, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VSPLTB
    351, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VSPLTH
    352, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VSPLTW
    353, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VSPLTISB
    354, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VSPLTISH
    355, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VSPLTISW
    356, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VSR
    357, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSRAB
    358, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSRAH
    359, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSRAW
    360, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSRB
    361, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSRH
    362, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VSRO
    363, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSRW
    364, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSUBCUW
    365, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VSUBFP
    366, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSUBSBS
    367, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSUBSHS
    368, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSUBSWS
    369, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSUBUBM
    370, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSUBUBS
    371, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSUBUHM
    372, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSUBUHS
    373, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSUBUWM
    374, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSUBUWS
    375, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VSUMSWS
    376, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VSUM2SWS
    377, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VSUM4SBS
    378, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VSUM4SHS
    379, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VSUM4UBS
    380, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VUPKHPX
    381, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VUPKHSB
    382, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VUPKHSH
    383, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VUPKLPX
    384, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VUPKLSB
    385, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VUPKLSH
    386, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VXOR
    387, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VMADDFP
    388, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMHADDSHS
    389, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMHRADDSHS
    390, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMLADDUHM
    391, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMSUMMBM
    392, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMSUMSHM
    393, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMSUMSHS
    394, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMSUMUBM
    395, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMSUMUHM
    396, V_CFX,    4,  1,  1,  1,  1, 0, 0, // PC_VMSUMUHS
    397, V_FPU,    4,  1,  1,  1,  1, 0, 0, // PC_VNMSUBFP
    398, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VPERM
    399, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VSEL
    400, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VSLDOI
    401, VECTOR,   1,  1,  0,  0,  0, 0, 0, // PC_VMR
    402, V_PERM,   2,  1,  1,  0,  0, 0, 0, // PC_VMRP
    // (the original's table ends with this sentinel, PC_PSQ_L's row: the opcodes after it, the other
    // paired-single ones and the rest, read past the table's end)
#ifdef __MWERKS__
    -1, CFX,      1,  1,  0,  0,  0, 1, 0
#else
    -1, CFX,      1,  1,  0,  0,  0, 1, 0,
    // What the original reads past the table's end, its data that follows, as rows of the table:
    // the unit's string "MachineSimulationAltiVec.c" and the padding after it
    24909, 99,      104,  105,  110,  101,   83,  105,  109, // PC_PSQ_LU
    27765, 97,      116,  105,  111,  110,   65,  108,  116, // PC_PSQ_LX
    22121, 101,      99,   46,   99,    0,    0,    0,    0, // PC_PSQ_LUX
    // padding, then MachineSimulation7400.c's machine7400: 2, TRUE, 0 and its seven function
    // pointers, the original's addresses (they differ by version)
    0, CFX2,      0,    0,    0,    1,    0,    0,    0, // PC_PSQ_ST
#if VERSION < VERSION_GC_2_5
    0, BPU,       0,   80, -114,   87,    0,  112, -115, // PC_PSQ_STU
#elif VERSION == VERSION_GC_2_5
    0, BPU,       0,   16, -107,   87,    0,   48, -108, // PC_PSQ_STU
#elif VERSION == VERSION_GC_2_6
    0, BPU,       0, -128, -111,   87,    0,  -96, -112, // PC_PSQ_STU
#else
    0, BPU,       0, -128, -100,   87,    0,  -96, -101, // PC_PSQ_STU
#endif
#if VERSION < VERSION_GC_2_5
    87, 96,     -117,   87,    0,  -32, -118,   87,    0, // PC_PSQ_STX
#elif VERSION == VERSION_GC_2_5
    87, 32,     -110,   87,    0,  -96, -111,   87,    0, // PC_PSQ_STX
#elif VERSION == VERSION_GC_2_6
    87, 144,    -114,   87,    0,   16, -114,   87,    0, // PC_PSQ_STX
#else
    87, 144,    -103,   87,    0,   16, -103,   87,    0, // PC_PSQ_STX
#endif
#if VERSION < VERSION_GC_2_5
    -31504, 87,        0,  -48, -124,   87,    0,  -80, -124, // PC_PSQ_STUX
#elif VERSION == VERSION_GC_2_5
    -29776, 87,        0, -112, -117,   87,    0,  112, -117, // PC_PSQ_STUX
#elif VERSION == VERSION_GC_2_6
    -30688, 87,        0,    0, -120,   87,    0,  -32, -121, // PC_PSQ_STUX
#else
    -27872, 87,        0,    0, -109,   87,    0,  -32, -110, // PC_PSQ_STUX
#endif
    // the end of machine7400, then MachineSimulation7400.c's instruction_timing from its start, its
    // 7-byte rows read as 10-byte ones
    87, BPU,       0,    0,    0,    0,    0,    0,    0, // PC_DCBZ_L
    0, BPU,       0,    0,    0,    0,    0,    0,    0, // PC_PS_ADD
    0, BPU,       0,    0,    0,    0,    0,    0,    0, // PC_PS_SUB
    0, BPU,       0,    0,    0,    0,    0,    0,    0, // PC_PS_MUL
    0, BPU,       0,    0,    0,    0,    0,    0,    0, // PC_PS_DIV
    0, BPU,       0,    0,    0,    0,    0,    0,    0, // PC_PS_MADD
    0, BPU,       0,    0,    0,    0,    0,    0,    0, // PC_PS_MSUB
    0, BPU,       0,    0,    0,    0,    0,    0,    0, // PC_PS_NMADD
    0, BPU,       0,    0,    0,    0,    0,    0,    0, // PC_PS_NMSUB
    0, BPU,       0,    0,    0,    0,    0,    0,    0, // PC_PS_RES
    0, BPU,       0,    0,    0,    0,    0,    0,    0, // PC_PS_SEL
    0, BPU,       0,    0,    0,    0,    0,    0,    0, // PC_PS_ABS
    0, BPU,       0,    0,    0,    0,    0,    0,    0, // PC_PS_NABS
    0, BPU,       0,    0,    0,    0,    0,    0,    0, // PC_PS_NEG
    0, BPU,       0,    0,    0,    0,    0,    0,    3, // PC_PS_MR
    258, CFX,       0,    0,    0,    3,    2,    1,    1, // PC_PS_RSQRTE
    0, BPU,       3,    2,    1,    1,    0,    0,    0, // PC_PS_CMPU0
    515, CFX,       1,    0,    0,    0,    3,    2,    1, // PC_PS_CMPO0
    1, BPU,       0,    3,    2,    1,    1,    0,    0, // PC_PS_CMPU1
    768, CFX2,      1,    1,    0,    0,    0,    3,    2, // PC_PS_CMPO1
    257, BPU,       0,    0,    3,    2,    1,    1,    0, // PC_PS_MERGE00
    0, CFX3,      2,    1,    1,    0,    0,    0,    3, // PC_PS_MERGE01
    258, CFX,       0,    0,    0,    3,    2,    1,    1, // PC_PS_MERGE10
    0, BPU,       3,    2,    1,    1,    0,    0,    0, // PC_PS_MERGE11
    515, CFX,       1,    0,    0,    0,    3,    2,    1, // PC_PS_SUM0
    1, BPU,       0,    3,    2,    1,    1,    0,    0, // PC_PS_SUM1
    768, CFX2,      1,    1,    0,    0,    0,    3,    2, // PC_PS_MULS0
    257, BPU,       0,    0,    3,    2,    1,    1,    0, // PC_PS_MULS1
    0, CFX3,      2,    1,    1,    0,    0,    0,    3, // PC_PS_MADDS0
    258, CFX,       0,    0,    0,    3,    2,    1,    1, // PC_PS_MADDS1
    0, BPU,       3,    2,    1,    1,    0,    0,    0, // PC_SLE
    515, CFX,       1,    0,    0,    0,    3,    2,    1, // PC_SLEQ
    1, BPU,       0,    3,    2,    1,    1,    0,    0, // PC_SLIQ
    768, CFX2,      1,    1,    0,    0,    0,    3,    2, // PC_SLLIQ
    257, BPU,       0,    0,    3,    2,    1,    1,    0, // PC_SLLQ
    0, CFX3,      2,    1,    1,    0,    0,    0,    3, // PC_SLQ
    258, CFX,       0,    0,    0,    3,    2,    1,    1, // PC_SRAIQ
    0, BPU,       3,    2,    1,    1,    0,    0,    0, // PC_SRAQ
    515, CFX,       1,    0,    0,    0,    3,    3,    1, // PC_SRE
    2, BPU,       0,    3,    3,    1,    2,    0,    0, // PC_SREA
    768, CFX2,      1,    1,    0,    0,    0,    3,    2, // PC_SREQ
    257, BPU,       0,    0,    3,    3,    1,    2,    0, // PC_SRIQ
    0, CFX2,      1,    1,    0,    0,    0,    0,    2, // PC_SRLIQ
    257, BPU,       0,    0,    0,    2,    1,    1,    0, // PC_SRLQ
    0, BPU,       2,    1,    1,    0,    0,    0,    0, // PC_SRQ
    258, CFX,       0,    0,    0,    0,    2,    1,    1, // PC_MASKG
    0, BPU,       0,    2,    1,    1,    0,    0,    0, // PC_MASKIR
    512, CFX,       1,    0,    0,    0,    0,    2,    1, // PC_LSCBX
    1, BPU,       0,    0,    1,   19,   19,    0,    0, // PC_DIV
    0, CFX,      19,   19,    0,    0,    0,    0,    1, // PC_DIVS
    1285, BPU,       0,    0,    0,    1,    6,    5,    0, // PC_DOZ
    0, BPU,       1,    3,    3,    0,    0,    0,    0, // PC_MUL
    1281, SFX2,      0,    0,    0,    0,    2,    1,    1, // PC_NABS
    0, BPU,       0,    2,    1,    1,    0,    0,    0, // PC_ABS
    512, CFX,       1,    0,    0,    0,    0,    2,    1, // PC_CLCS
    1, BPU,       0,    0,    2,    1,    1,    0,    0, // PC_DOZI
    0, CFX2,      1,    1,    0,    0,    0,    0,    2, // PC_RLMI
    257, BPU,       0,    0,    0,    2,    3,    1,    0, // PC_RRIB
#endif
};
// clang-format on

static void complete_instruction(int stage);

static void advance(int firstStage, int oldStage, int newStage) {
    PCode* instr = pipeline_altivec[oldStage].instr;
    int cycles = instruction_timing[instr->op].cycles[newStage - firstStage];
    // the FPU's last stage takes a cycle less
    if (oldStage == FPU3 && newStage == FPU4) {
        cycles--;
    }
    pipeline_altivec[newStage].instr = instr;
    pipeline_altivec[newStage].remaining = cycles;
    pipeline_altivec[oldStage].instr = NULL;
    pipeline_altivec[oldStage].remaining = 0;
    if (cycles == 0) {
        complete_instruction(newStage);
    }
}

static void assign_completion_buffer(PCode* instr) {
    completionbuffers.used++;
    completionbuffers.free--;
    completionbuffers.entries[completionbuffers.nextFreeSlot].instr = instr;
    completionbuffers.entries[completionbuffers.nextFreeSlot].completed = FALSE;
    completionbuffers.nextFreeSlot = (completionbuffers.nextFreeSlot + 1) % MaxEntries;
}

static void complete_instruction(int stage) {
    PCode* instr = pipeline_altivec[stage].instr;
    int buf;
    if (stage == FPU4 && instr) {
        fpu_completed = TRUE;
    }
    buf = 0;
    while (buf < MaxEntries && completionbuffers.entries[buf].instr != instr) {
        buf++;
    }

    completionbuffers.entries[buf].completed = TRUE;
    pipeline_altivec[stage].instr = NULL;
}

static void retire_instruction(void) {
    completionbuffers.entries[completionbuffers.nextToRetire].instr = NULL;
    completionbuffers.entries[completionbuffers.nextToRetire].completed = FALSE;
    completionbuffers.used--;
    completionbuffers.free++;
    completionbuffers.nextToRetire = (completionbuffers.nextToRetire + 1) % MaxEntries;
}

static int latency(PCode* instr) {
    int cycles = instruction_timing[instr->op].latency;
    if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
        cycles += 1;
    }
    if (instr->op == PC_LMW || instr->op == PC_STMW) {
        cycles += instr->argCount - 2;
    }
    return cycles;
}

static void initialize(void) {
    int stage;
    int i;

    fetchqueues[InstrQueue] = 4;
    fetchqueues[Dispatched] = 3;
    fetchqueues[FIQ] = 2;
    fetchqueues[GIQ] = 3;
    fetchqueues[VIQ] = 2;
    fetchqueues[FPIssued] = 0;
    fetchqueues[GPRIssued] = 0;
    fetchqueues[VRIssued] = 0;

    for (stage = 0; stage < NumStages; stage++) {
        pipeline_altivec[stage].instr = NULL;
    }

    completionbuffers.free = MaxEntries;
    completionbuffers.used = 0;
    completionbuffers.nextToRetire = 0;
    completionbuffers.nextFreeSlot = 0;
    for (i = 0; i < MaxEntries; i++) {
        completionbuffers.entries[i].instr = NULL;
        completionbuffers.entries[i].completed = FALSE;
    }
}

static int can_issue(PCode* instr) {
    int stage;

    if (completionbuffers.free == 0) {
        return FALSE;
    }

    stage = instruction_timing[instr->op].stage;

    if (stage == SFX1) {
        int isClear1 = !pipeline_altivec[SFX1].instr;
        int isClear2 = !pipeline_altivec[SFX2].instr;
        if (!isClear1 && !isClear2) {
            return FALSE;
        }
    } else {
        if (pipeline_altivec[stage].instr) {
            return FALSE;
        }
    }

    // no more than three floating-point instructions in flight after one finished
    if (stage == FPU) {
        Stage s;
        int count = 0;
        for (s = FPU4; s >= FPU; s--) {
            if (pipeline_altivec[s].instr) {
                count++;
            }
        }
        if (count >= 3 && fpu_completed) {
            return FALSE;
        }
    }

    if (fetchqueues[Dispatched] <= 0) {
        return FALSE;
    }

    if (stage == FPU) {
        if (fetchqueues[FIQ] < 1 || fetchqueues[FPIssued] >= 1) {
            return FALSE;
        }
    } else if (stage >= VECTOR && stage <= V_FPU) {
        if (fetchqueues[VIQ] < 1 || fetchqueues[VRIssued] >= 2) {
            return FALSE;
        }
    } else if (stage != BPU) {
        if (fetchqueues[GIQ] < 1 || fetchqueues[GPRIssued] >= 3) {
            return FALSE;
        }
    }

    return TRUE;
}

static void issue(PCode* instr) {
    int stage = instruction_timing[instr->op].stage;
    int cycles = instruction_timing[instr->op].cycles[0];
    assign_completion_buffer(instr);

    CError_ASSERT(945, --fetchqueues[Dispatched] >= 0);

    if (stage == FPU) {
        fetchqueues[FIQ]--;
        fetchqueues[FPIssued]++;
    } else if (stage >= VECTOR && stage <= V_FPU) {
        fetchqueues[VIQ]--;
        fetchqueues[VRIssued]++;
    } else if (stage != BPU) {
        fetchqueues[GIQ]--;
        fetchqueues[GPRIssued]++;
    }

    fetchqueues[FIQ] =
        (fetchqueues[Dispatched] < fetchqueues[FIQ]) ? fetchqueues[Dispatched] : fetchqueues[FIQ];
    fetchqueues[GIQ] =
        (fetchqueues[Dispatched] < fetchqueues[GIQ]) ? fetchqueues[Dispatched] : fetchqueues[GIQ];
    fetchqueues[VIQ] =
        (fetchqueues[Dispatched] < fetchqueues[VIQ]) ? fetchqueues[Dispatched] : fetchqueues[VIQ];

    if (stage == SFX1) {
        if (!pipeline_altivec[SFX1].instr) {
            stage = SFX1;
        } else if (!pipeline_altivec[SFX2].instr) {
            stage = SFX2;
        } else if (!pipeline_altivec[SFX3].instr) {
            stage = SFX3;
        }
    }

    pipeline_altivec[stage].instr = instr;
    pipeline_altivec[stage].remaining = cycles;
}

static void advance_clock(void) {
    int num;
    int i;
    int unit;

    for (i = 0; i < NumStages; i++) {
        if (pipeline_altivec[i].instr && pipeline_altivec[i].remaining) {
            --pipeline_altivec[i].remaining;
        }
    }

    for (i = 0; i < 3; i++) {
        if (completionbuffers.used == 0) {
            break;
        }
        if (completionbuffers.entries[completionbuffers.nextToRetire].completed == 0) {
            break;
        }
        retire_instruction();
    }

    fpu_completed = FALSE;
    // every final stage but the last entry of finalstages, which is unused
    for (i = 0; (unsigned int)i < sizeof(finalstages) / sizeof(finalstages[0]) - 1; i++) {
        if (pipeline_altivec[finalstages[i]].instr &&
            pipeline_altivec[finalstages[i]].remaining == 0) {
            complete_instruction(finalstages[i]);
        }
    }

    // each unit's stages advance from its last to its first
    for (unit = 0; (unsigned int)unit < sizeof(pipeline_units) / sizeof(pipeline_units[0]);
         unit++) {
        Stage first;
        Stage current;
        first = pipeline_units[unit].start;
        for (current = pipeline_units[unit].end - 1; current >= first; current--) {
            if (pipeline_altivec[current].instr &&
                pipeline_altivec[current].remaining == 0 &&
                !pipeline_altivec[current + 1].instr) {
                advance(first, current, current + 1);
            }
        }
    }

    fetchqueues[FPIssued] = 0;
    fetchqueues[GPRIssued] = 0;
    fetchqueues[VRIssued] = 0;

#define CHEAP_MIN(a, b) (((a) < (b)) ? (a) : (b))
    num = 2 - fetchqueues[FIQ];
    num += 6 - fetchqueues[GIQ];
    num += 4 - fetchqueues[VIQ];
    num = (num > 3) ? 3 : num;
    num = (completionbuffers.free < (unsigned int)num) ? completionbuffers.free : num;
    num = (fetchqueues[InstrQueue] < num) ? fetchqueues[InstrQueue] : num;

    fetchqueues[Dispatched] += num;
    fetchqueues[InstrQueue] -= num;

    fetchqueues[FIQ] = CHEAP_MIN(fetchqueues[Dispatched], CHEAP_MIN(2, fetchqueues[FIQ] + num));
    fetchqueues[GIQ] = CHEAP_MIN(fetchqueues[Dispatched], CHEAP_MIN(6, fetchqueues[GIQ] + num));
    fetchqueues[VIQ] = CHEAP_MIN(fetchqueues[Dispatched], CHEAP_MIN(4, fetchqueues[VIQ] + num));

    CError_ASSERT(
        1078, fetchqueues[Dispatched] <= (fetchqueues[FIQ] + fetchqueues[GIQ] + fetchqueues[VIQ])
    );

    if (fetchqueues[InstrQueue] <= 8) {
        fetchqueues[InstrQueue] += 4;
    }
}

static int serializes(PCode* instr) {
    return instruction_timing[instr->op].serializes;
}

static int uses_vpermute_unit_altivec(PCode* instr) {
    return instruction_timing[instr->op].stage == V_PERM;
}
