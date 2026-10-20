#include "compiler/CError.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Scheduler.h"

#define latency m7450_latency
#define initialize m7450_initialize
#define can_issue m7450_can_issue
#define issue m7450_issue
#define advance_clock m7450_advance_clock
#define serializes m7450_serializes
#define instruction_timing m7450_instruction_timing
#define completionbuffers m7450_completionbuffers

typedef enum Stage {
    BPU,

    CFX,
    CFX2,
    CFX3,

    SFX1,
    SFX2,
    SFX3,

    LSU,
    LSU2,
    LSU3,
    LSU4,

    FPU,
    FPU2,
    FPU3,
    FPU4,

    VECTOR,

    V_PERM,
    V_PERM2,

    V_CFX,
    V_CFX2,
    V_CFX3,
    V_CFX4,

    V_FPU,
    V_FPU2,
    V_FPU3,
    V_FPU4,

    NumStages
} Stage;

static int fpu_completed;

static struct {
    PCode* instr;

    int remaining;
#ifdef __MWERKS__
} pipeline_altivec[NumStages];
#else
} pipeline_altivec[145];
#endif

enum {
    InstrQueue,
    Dispatched,
    FIQ,
    GIQ,
    VIQ,
    FPIssued,
    GPRIssued,
    VRIssued,
    NumQueues
};

static int fetchqueues[NumQueues];

enum {
    MaxEntries = 16
};

static struct {
    unsigned int free;

    unsigned int used;

    unsigned int nextToRetire;

    unsigned int nextFreeSlot;

    struct {
        PCode* instr;
        int completed;
    } entries[MaxEntries];
} completionbuffers;

// clang-format off
static char* stage_names[NumStages] = {
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

    Stage stage;

    char latency;

    char cycles[4];

    char serializes;

    char unused;
    // clang-format off
} instruction_timing[] = {
    0, BPU,      0,  0,  0,  0,  0, 0, 0,
    1, BPU,      0,  0,  0,  0,  0, 0, 0,
    2, BPU,      0,  0,  0,  0,  0, 0, 0,
    3, BPU,      0,  0,  0,  0,  0, 0, 0,
    4, BPU,      0,  0,  0,  0,  0, 0, 0,
    5, BPU,      0,  0,  0,  0,  0, 0, 0,
    6, BPU,      0,  0,  0,  0,  0, 0, 0,
    7, BPU,      0,  0,  0,  0,  0, 0, 0,
    8, BPU,      0,  0,  0,  0,  0, 0, 0,
    9, BPU,      0,  0,  0,  0,  0, 0, 0,
    10, BPU,      0,  0,  0,  0,  0, 0, 0,
    11, BPU,      0,  0,  0,  0,  0, 0, 0,
    12, BPU,      0,  0,  0,  0,  0, 0, 0,
    13, BPU,      0,  0,  0,  0,  0, 0, 0,
    14, BPU,      0,  0,  0,  0,  0, 0, 0,
    15, BPU,      0,  0,  0,  0,  0, 0, 0,
    16, BPU,      0,  0,  0,  0,  0, 0, 0,
    17, BPU,      0,  0,  0,  0,  0, 0, 0,
    18, BPU,      0,  0,  0,  0,  0, 0, 0,
    19, BPU,      0,  0,  0,  0,  0, 0, 0,
    20, BPU,      0,  0,  0,  0,  0, 0, 0,
    21, LSU,      3,  1,  1,  1,  0, 0, 0,
    22, LSU,      3,  1,  1,  1,  0, 0, 0,
    23, LSU,      3,  1,  1,  1,  0, 0, 0,
    24, LSU,      3,  1,  1,  1,  0, 0, 0,
    25, LSU,      3,  1,  1,  1,  0, 0, 0,
    26, LSU,      3,  1,  1,  1,  0, 0, 0,
    27, LSU,      3,  1,  1,  1,  0, 0, 0,
    28, LSU,      3,  1,  1,  1,  0, 0, 0,
    29, LSU,      3,  1,  1,  1,  0, 0, 0,
    30, LSU,      3,  1,  1,  1,  0, 0, 0,
    31, LSU,      3,  1,  1,  1,  0, 0, 0,
    32, LSU,      3,  1,  1,  1,  0, 0, 0,
    33, LSU,      3,  1,  1,  1,  0, 0, 0,
    34, LSU,      3,  1,  1,  1,  0, 0, 0,
    35, LSU,      3,  1,  1,  1,  0, 0, 0,
    36, LSU,      3,  1,  1,  1,  0, 0, 0,
    37, LSU,      3,  1,  1,  1,  0, 0, 0,
    38, LSU,      3,  1,  1,  1,  0, 0, 0,
    39, LSU,      3,  3,  0,  0,  0, 0, 0,
    40, LSU,      3,  1,  1,  1,  0, 0, 0,
    41, LSU,      3,  1,  1,  1,  0, 0, 0,
    42, LSU,      3,  1,  1,  1,  0, 0, 0,
    43, LSU,      3,  1,  1,  1,  0, 0, 0,
    44, LSU,      3,  1,  1,  1,  0, 0, 0,
    45, LSU,      3,  1,  1,  1,  0, 0, 0,
    46, LSU,      3,  1,  1,  1,  0, 0, 0,
    47, LSU,      3,  1,  1,  1,  0, 0, 0,
    48, LSU,      3,  1,  1,  1,  0, 0, 0,
    49, LSU,      3,  1,  1,  1,  0, 0, 0,
    50, LSU,      3,  1,  1,  1,  0, 0, 0,
    51, LSU,      3,  1,  1,  1,  0, 0, 0,
    52, LSU,      3,  1,  1,  1,  0, 0, 0,
    53, LSU,      3,  1,  1,  1,  0, 0, 0,
    54, LSU,      3,  3,  0,  0,  0, 0, 0,
    55, LSU,      3,  3,  0,  0,  0, 0, 0,
    56, LSU,      3,  3,  0,  0,  0, 0, 0,
    57, LSU,      3,  1,  1,  1,  0, 0, 0,
    58, LSU,      3,  1,  1,  1,  0, 0, 0,
    59, LSU,      3,  1,  1,  1,  0, 0, 0,
    60, SFX1,     1,  1,  0,  0,  0, 0, 0,
    61, SFX1,     1,  1,  0,  0,  0, 0, 0,
    62, SFX1,     1,  1,  0,  0,  0, 0, 0,
    63, SFX1,     1,  1,  0,  0,  0, 0, 0,
    64, SFX1,     1,  1,  0,  0,  0, 0, 0,
    65, SFX1,     1,  1,  0,  0,  0, 0, 0,
    66, SFX1,     1,  1,  0,  0,  0, 0, 0,
    67, SFX1,     1,  1,  0,  0,  0, 0, 0,
    68, SFX1,     1,  1,  0,  0,  0, 0, 0,
    69, CFX,     23, 23,  0,  0,  0, 0, 0,
    70, CFX,     23, 23,  0,  0,  0, 0, 0,
    71, CFX,      4,  2,  2,  0,  0, 0, 0,
    72, CFX,      4,  2,  2,  0,  0, 0, 0,
    73, CFX,      3,  1,  1,  1,  0, 0, 0,
    74, CFX,      4,  2,  2,  0,  0, 0, 0,
    75, SFX1,     1,  1,  0,  0,  0, 0, 0,
    76, SFX1,     1,  1,  0,  0,  0, 0, 0,
    77, SFX1,     1,  1,  0,  0,  0, 0, 0,
    78, SFX1,     1,  1,  0,  0,  0, 0, 0,
    79, SFX1,     1,  1,  0,  0,  0, 0, 0,
    80, SFX1,     1,  1,  0,  0,  0, 0, 0,
    81, SFX1,     1,  1,  0,  0,  0, 0, 0,
    82, SFX1,     1,  1,  0,  0,  0, 0, 0,
    83, SFX1,     1,  1,  0,  0,  0, 0, 0,
    84, SFX1,     1,  1,  0,  0,  0, 0, 0,
    85, SFX1,     1,  1,  0,  0,  0, 0, 0,
    86, SFX1,     1,  1,  0,  0,  0, 0, 0,
    87, SFX1,     1,  1,  0,  0,  0, 0, 0,
    88, SFX1,     1,  1,  0,  0,  0, 0, 0,
    89, SFX1,     1,  1,  0,  0,  0, 0, 0,
    90, SFX1,     1,  1,  0,  0,  0, 0, 0,
    91, SFX1,     1,  1,  0,  0,  0, 0, 0,
    92, SFX1,     1,  1,  0,  0,  0, 0, 0,
    93, SFX1,     1,  1,  0,  0,  0, 0, 0,
    94, SFX1,     1,  1,  0,  0,  0, 0, 0,
    95, SFX1,     1,  1,  0,  0,  0, 0, 0,
    96, SFX1,     1,  1,  0,  0,  0, 0, 0,
    97, SFX1,     1,  1,  0,  0,  0, 0, 0,
    98, SFX1,     1,  1,  0,  0,  0, 0, 0,
    99, SFX1,     1,  1,  0,  0,  0, 0, 0,
    100, SFX1,     1,  1,  0,  0,  0, 0, 0,
    101, SFX1,     1,  1,  0,  0,  0, 0, 0,
    102, SFX1,     1,  1,  0,  0,  0, 0, 0,
    103, SFX1,     1,  1,  0,  0,  0, 0, 0,
    104, SFX1,     1,  1,  0,  0,  0, 0, 0,
    105, SFX1,     1,  1,  0,  0,  0, 0, 0,
    106, SFX1,     1,  1,  0,  0,  0, 0, 0,
    107, SFX1,     1,  1,  0,  0,  0, 0, 0,
    108, SFX1,     2,  2,  0,  0,  0, 0, 0,
    109, SFX1,     2,  2,  0,  0,  0, 0, 0,
    110, CFX,      2,  2,  0,  0,  0, 1, 0,
    111, CFX,      2,  2,  0,  0,  0, 1, 0,
    112, CFX,      2,  2,  0,  0,  0, 1, 0,
    113, CFX,      2,  2,  0,  0,  0, 1, 0,
    114, CFX,      2,  2,  0,  0,  0, 1, 0,
    115, CFX,      2,  2,  0,  0,  0, 1, 0,
    116, CFX,      2,  2,  0,  0,  0, 1, 0,
    117, CFX,      2,  2,  0,  0,  0, 1, 0,
    118, CFX,      1,  1,  0,  0,  0, 1, 0,
    119, CFX,      2,  2,  0,  0,  0, 1, 0,
    120, CFX,      2,  2,  0,  0,  0, 1, 0,
    121, CFX,      2,  2,  0,  0,  0, 1, 0,
    122, SFX1,     1,  1,  0,  0,  0, 0, 0,
    123, CFX,      2,  2,  0,  0,  0, 1, 0,
    124, CFX,      3,  3,  0,  0,  0, 1, 0,
    125, CFX,      3,  2,  1,  0,  0, 0, 0,
    126, CFX,      3,  3,  0,  0,  0, 1, 0,
    127, CFX,      1,  1,  0,  0,  0, 1, 0,
    128, CFX,      1,  1,  0,  0,  0, 1, 0,
    129, CFX,      1,  1,  0,  0,  0, 1, 0,
    130, CFX,      2,  2,  0,  0,  0, 1, 0,
    131, FPU,      5,  5,  0,  0,  0, 1, 0,
    132, FPU,      5,  5,  0,  0,  0, 1, 0,
    133, LSU,      3,  3,  0,  0,  0, 1, 0,
    134, CFX,      1,  1,  0,  0,  0, 1, 0,
    135, LSU,     35, 35,  0,  0,  0, 1, 0,
    136, CFX,      1,  1,  0,  0,  0, 1, 0,
    137, SFX1,     1,  1,  0,  0,  0, 0, 0,
    138, SFX1,     1,  1,  0,  0,  0, 0, 0,
    139, SFX1,     1,  1,  0,  0,  0, 0, 0,
    140, SFX1,     1,  1,  0,  0,  0, 0, 0,
    141, SFX1,     1,  1,  0,  0,  0, 0, 0,
    142, LSU,      4,  1,  1,  1,  1, 0, 0,
    143, LSU,      4,  1,  1,  1,  1, 0, 0,
    144, LSU,      4,  1,  1,  1,  1, 0, 0,
    145, LSU,      4,  1,  1,  1,  1, 0, 0,
    146, LSU,      4,  1,  1,  1,  1, 0, 0,
    147, LSU,      4,  1,  1,  1,  1, 0, 0,
    148, LSU,      4,  1,  1,  1,  1, 0, 0,
    149, LSU,      4,  1,  1,  1,  1, 0, 0,
    150, LSU,      3,  3,  0,  0,  0, 0, 0,
    151, LSU,      3,  3,  0,  0,  0, 0, 0,
    152, LSU,      3,  3,  0,  0,  0, 0, 0,
    153, LSU,      3,  3,  0,  0,  0, 0, 0,
    154, LSU,      3,  3,  0,  0,  0, 0, 0,
    155, LSU,      3,  3,  0,  0,  0, 0, 0,
    156, LSU,      3,  3,  0,  0,  0, 0, 0,
    157, LSU,      3,  3,  0,  0,  0, 0, 0,
    158, FPU,      5,  1,  1,  1,  2, 0, 0,
    159, FPU,      5,  1,  1,  1,  2, 0, 0,
    160, FPU,      5,  1,  1,  1,  2, 0, 0,
    161, FPU,      5,  1,  1,  1,  2, 0, 0,
    162, FPU,      5,  1,  1,  1,  2, 0, 0,
    163, FPU,      5,  1,  1,  1,  2, 0, 0,
    164, FPU,      5,  1,  1,  1,  2, 0, 0,
    165, FPU,      5,  1,  1,  1,  2, 0, 0,
    166, FPU,      5,  1,  1,  1,  2, 0, 0,
    167, FPU,      5,  1,  1,  1,  2, 0, 0,
    168, FPU,     35, 35,  0,  0,  0, 0, 0,
    169, FPU,     21, 21,  0,  0,  0, 0, 0,
    170, FPU,      5,  1,  1,  1,  2, 0, 0,
    171, FPU,      5,  1,  1,  1,  2, 0, 0,
    172, FPU,      5,  1,  1,  1,  2, 0, 0,
    173, FPU,      5,  1,  1,  1,  2, 0, 0,
    174, FPU,      5,  1,  1,  1,  2, 0, 0,
    175, FPU,      5,  1,  1,  1,  2, 0, 0,
    176, FPU,      5,  1,  1,  1,  2, 0, 0,
    177, FPU,      5,  1,  1,  1,  2, 0, 0,
    178, FPU,     14, 14,  0,  0,  0, 0, 0,
    179, FPU,      5,  1,  1,  1,  2, 0, 0,
    180, FPU,      5,  1,  1,  1,  2, 0, 0,
    181, FPU,      5,  1,  1,  1,  2, 0, 0,
    182, FPU,      5,  1,  1,  1,  2, 0, 0,
    183, FPU,      5,  1,  1,  1,  2, 0, 0,
    184, FPU,      5,  1,  1,  1,  2, 0, 0,
    185, FPU,      5,  1,  1,  1,  2, 0, 0,
    186, LSU,      3,  3,  0,  0,  0, 1, 0,
    187, LSU,      3,  1,  1,  1,  0, 0, 0,
    188, LSU,      3,  3,  0,  0,  0, 0, 0,
    189, LSU,      3,  1,  1,  1,  0, 0, 0,
    190, LSU,      3,  3,  0,  0,  0, 0, 0,
    191, LSU,      3,  3,  0,  0,  0, 0, 0,
    192, LSU,      3,  1,  1,  1,  0, 1, 0,
    193, LSU,      3,  1,  1,  1,  0, 0, 0,
    194, LSU,      3,  1,  1,  1,  0, 0, 0,
    195, LSU,      3,  3,  0,  0,  0, 0, 0,
    196, LSU,      3,  1,  1,  1,  0, 0, 0,
    197, CFX,      5,  5,  0,  0,  0, 1, 0,
    198, CFX,      2,  2,  0,  0,  0, 1, 0,
    199, CFX,      5,  5,  0,  0,  0, 0, 0,
    200, CFX,      4,  1,  3,  0,  0, 0, 0,
    201, CFX,      2,  2,  0,  0,  0, 1, 0,
    202, CFX,      4,  1,  3,  0,  0, 0, 0,
    203, CFX,      2,  2,  0,  0,  0, 1, 0,
    204, FPU,      5,  5,  0,  0,  0, 1, 0,
    205, FPU,      5,  5,  0,  0,  0, 1, 0,
    206, FPU,      5,  5,  0,  0,  0, 0, 0,
    207, CFX,      1,  1,  0,  0,  0, 1, 0,
    208, FPU,      1,  1,  0,  0,  0, 0, 0,
    209, FPU,      1,  1,  0,  0,  0, 0, 0,
    210, LSU,      1,  1,  0,  0,  0, 0, 0,
    211, LSU,      3,  1,  1,  1,  0, 0, 0,
    212, LSU,      3,  3,  0,  0,  0, 0, 0,
    213, LSU,      3,  3,  0,  0,  0, 1, 0,
    214, LSU,      3,  3,  0,  0,  0, 1, 0,
    215, SFX1,     2,  2,  0,  0,  0, 0, 0,
    216, SFX1,     1,  1,  0,  0,  0, 0, 0,
    217, SFX1,     2,  2,  0,  0,  0, 0, 0,
    218, SFX1,     1,  1,  0,  0,  0, 1, 0,
    219, SFX1,     1,  1,  0,  0,  0, 0, 0,
    220, SFX1,     1,  1,  0,  0,  0, 0, 0,
    221, SFX1,     1,  1,  0,  0,  0, 0, 0,
    222, SFX1,     1,  0,  0,  0,  0, 0, 0,
    223, SFX1,     1,  0,  0,  0,  0, 0, 0,
    224, SFX1,     1,  0,  0,  0,  0, 0, 0,
    225, SFX1,     1,  0,  0,  0,  0, 0, 0,
    226, SFX1,     1,  0,  0,  0,  0, 0, 0,
    227, SFX1,     1,  0,  0,  0,  0, 0, 0,
    228, SFX1,     1,  0,  0,  0,  0, 0, 0,
    229, SFX1,     1,  0,  0,  0,  0, 0, 0,
    230, SFX1,     1,  0,  0,  0,  0, 0, 0,
    231, SFX1,     1,  0,  0,  0,  0, 0, 0,
    232, SFX1,     1,  0,  0,  0,  0, 0, 0,
    233, SFX1,     1,  0,  0,  0,  0, 0, 0,
    234, SFX1,     1,  0,  0,  0,  0, 0, 0,
    235, LSU,      3,  1,  1,  1,  0, 0, 0,
    236, SFX1,     1,  1,  0,  0,  0, 0, 0,
    237, SFX1,     1,  1,  0,  0,  0, 0, 0,
    238, SFX1,     1,  1,  0,  0,  0, 0, 0,
    239, LSU,      3,  1,  1,  1,  0, 0, 0,
    240, LSU,      3,  1,  1,  1,  0, 0, 0,
    241, LSU,      3,  1,  1,  1,  0, 0, 0,
    242, LSU,      3,  1,  1,  1,  0, 0, 0,
    243, LSU,      3,  1,  1,  1,  0, 0, 0,
    244, LSU,      3,  1,  1,  1,  0, 0, 0,
    245, LSU,      3,  1,  1,  1,  0, 0, 0,
    246, LSU,      3,  1,  1,  1,  0, 0, 0,
    247, LSU,      3,  1,  1,  1,  0, 0, 0,
    248, LSU,      3,  1,  1,  1,  0, 0, 0,
    249, LSU,      3,  1,  1,  1,  0, 0, 0,
    250, LSU,      3,  1,  1,  1,  0, 0, 0,
    251, LSU,      3,  1,  1,  1,  0, 0, 0,
    252, LSU,      3,  1,  1,  1,  0, 0, 0,
    253, LSU,      3,  1,  1,  1,  0, 0, 0,
    254, LSU,      3,  1,  1,  1,  0, 0, 0,
    255, LSU,      3,  1,  1,  1,  0, 0, 0,
    256, LSU,      3,  1,  1,  1,  0, 0, 0,
    257, V_FPU,    2,  2,  0,  0,  0, 1, 0,
    258, V_FPU,    2,  2,  0,  0,  0, 1, 0,
    259, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    260, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    261, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    262, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    263, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    264, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    265, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    266, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    267, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    268, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    269, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    270, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    271, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    272, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    273, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    274, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    275, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    276, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    277, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    278, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    279, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    280, V_FPU,    2,  1,  1,  0,  0, 0, 0,
    281, V_FPU,    2,  1,  1,  0,  0, 0, 0,
    282, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    283, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    284, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    285, V_FPU,    2,  1,  1,  0,  0, 0, 0,
    286, V_FPU,    2,  1,  1,  0,  0, 0, 0,
    287, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    288, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    289, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    290, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    291, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    292, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    293, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    294, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    295, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    296, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    297, V_FPU,    2,  1,  1,  0,  0, 0, 0,
    298, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    299, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    300, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    301, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    302, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    303, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    304, V_FPU,    2,  1,  1,  0,  0, 0, 0,
    305, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    306, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    307, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    308, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    309, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    310, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    311, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    312, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    313, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    314, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    315, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    316, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    317, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    318, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    319, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    320, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    321, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    322, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    323, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    324, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    325, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    326, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    327, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    328, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    329, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    330, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    331, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    332, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    333, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    334, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    335, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    336, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    337, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    338, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    339, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    340, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    341, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    342, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    343, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    344, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    345, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    346, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    347, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    348, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    349, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    350, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    351, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    352, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    353, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    354, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    355, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    356, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    357, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    358, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    359, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    360, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    361, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    362, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    363, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    364, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    365, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    366, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    367, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    368, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    369, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    370, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    371, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    372, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    373, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    374, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    375, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    376, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    377, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    378, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    379, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    380, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    381, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    382, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    383, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    384, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    385, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    386, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    387, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    388, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    389, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    390, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    391, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    392, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    393, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    394, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    395, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    396, V_CFX,    4,  1,  1,  1,  1, 0, 0,
    397, V_FPU,    4,  1,  1,  1,  1, 0, 0,
    398, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    399, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    400, V_PERM,   2,  1,  1,  0,  0, 0, 0,
    401, VECTOR,   1,  1,  0,  0,  0, 0, 0,
    402, V_PERM,   2,  1,  1,  0,  0, 0, 0,
#ifdef __MWERKS__
    -1, CFX,      1,  1,  0,  0,  0, 1, 0
#else
    -1, CFX,      1,  1,  0,  0,  0, 1, 0,
    24909, 99,      104,  105,  110,  101,   83,  105,  109,
    27765, 97,      116,  105,  111,  110,   65,  108,  116,
    22121, 101,      99,   46,   99,    0,    0,    0,    0,
    0, CFX2,      0,    0,    0,    1,    0,    0,    0,
#if VERSION < VERSION_GC_2_5
    0, BPU,       0,   80, -114,   87,    0,  112, -115,
#elif VERSION == VERSION_GC_2_5
    0, BPU,       0,   16, -107,   87,    0,   48, -108,
#elif VERSION == VERSION_GC_2_6
    0, BPU,       0, -128, -111,   87,    0,  -96, -112,
#else
    0, BPU,       0, -128, -100,   87,    0,  -96, -101,
#endif
#if VERSION < VERSION_GC_2_5
    87, 96,     -117,   87,    0,  -32, -118,   87,    0,
#elif VERSION == VERSION_GC_2_5
    87, 32,     -110,   87,    0,  -96, -111,   87,    0,
#elif VERSION == VERSION_GC_2_6
    87, 144,    -114,   87,    0,   16, -114,   87,    0,
#else
    87, 144,    -103,   87,    0,   16, -103,   87,    0,
#endif
#if VERSION < VERSION_GC_2_5
    -31504, 87,        0,  -48, -124,   87,    0,  -80, -124,
#elif VERSION == VERSION_GC_2_5
    -29776, 87,        0, -112, -117,   87,    0,  112, -117,
#elif VERSION == VERSION_GC_2_6
    -30688, 87,        0,    0, -120,   87,    0,  -32, -121,
#else
    -27872, 87,        0,    0, -109,   87,    0,  -32, -110,
#endif
    87, BPU,       0,    0,    0,    0,    0,    0,    0,
    0, BPU,       0,    0,    0,    0,    0,    0,    0,
    0, BPU,       0,    0,    0,    0,    0,    0,    0,
    0, BPU,       0,    0,    0,    0,    0,    0,    0,
    0, BPU,       0,    0,    0,    0,    0,    0,    0,
    0, BPU,       0,    0,    0,    0,    0,    0,    0,
    0, BPU,       0,    0,    0,    0,    0,    0,    0,
    0, BPU,       0,    0,    0,    0,    0,    0,    0,
    0, BPU,       0,    0,    0,    0,    0,    0,    0,
    0, BPU,       0,    0,    0,    0,    0,    0,    0,
    0, BPU,       0,    0,    0,    0,    0,    0,    0,
    0, BPU,       0,    0,    0,    0,    0,    0,    0,
    0, BPU,       0,    0,    0,    0,    0,    0,    0,
    0, BPU,       0,    0,    0,    0,    0,    0,    0,
    0, BPU,       0,    0,    0,    0,    0,    0,    3,
    258, CFX,       0,    0,    0,    3,    2,    1,    1,
    0, BPU,       3,    2,    1,    1,    0,    0,    0,
    515, CFX,       1,    0,    0,    0,    3,    2,    1,
    1, BPU,       0,    3,    2,    1,    1,    0,    0,
    768, CFX2,      1,    1,    0,    0,    0,    3,    2,
    257, BPU,       0,    0,    3,    2,    1,    1,    0,
    0, CFX3,      2,    1,    1,    0,    0,    0,    3,
    258, CFX,       0,    0,    0,    3,    2,    1,    1,
    0, BPU,       3,    2,    1,    1,    0,    0,    0,
    515, CFX,       1,    0,    0,    0,    3,    2,    1,
    1, BPU,       0,    3,    2,    1,    1,    0,    0,
    768, CFX2,      1,    1,    0,    0,    0,    3,    2,
    257, BPU,       0,    0,    3,    2,    1,    1,    0,
    0, CFX3,      2,    1,    1,    0,    0,    0,    3,
    258, CFX,       0,    0,    0,    3,    2,    1,    1,
    0, BPU,       3,    2,    1,    1,    0,    0,    0,
    515, CFX,       1,    0,    0,    0,    3,    2,    1,
    1, BPU,       0,    3,    2,    1,    1,    0,    0,
    768, CFX2,      1,    1,    0,    0,    0,    3,    2,
    257, BPU,       0,    0,    3,    2,    1,    1,    0,
    0, CFX3,      2,    1,    1,    0,    0,    0,    3,
    258, CFX,       0,    0,    0,    3,    2,    1,    1,
    0, BPU,       3,    2,    1,    1,    0,    0,    0,
    515, CFX,       1,    0,    0,    0,    3,    3,    1,
    2, BPU,       0,    3,    3,    1,    2,    0,    0,
    768, CFX2,      1,    1,    0,    0,    0,    3,    2,
    257, BPU,       0,    0,    3,    3,    1,    2,    0,
    0, CFX2,      1,    1,    0,    0,    0,    0,    2,
    257, BPU,       0,    0,    0,    2,    1,    1,    0,
    0, BPU,       2,    1,    1,    0,    0,    0,    0,
    258, CFX,       0,    0,    0,    0,    2,    1,    1,
    0, BPU,       0,    2,    1,    1,    0,    0,    0,
    512, CFX,       1,    0,    0,    0,    0,    2,    1,
    1, BPU,       0,    0,    1,   19,   19,    0,    0,
    0, CFX,      19,   19,    0,    0,    0,    0,    1,
    1285, BPU,       0,    0,    0,    1,    6,    5,    0,
    0, BPU,       1,    3,    3,    0,    0,    0,    0,
    1281, SFX2,      0,    0,    0,    0,    2,    1,    1,
    0, BPU,       0,    2,    1,    1,    0,    0,    0,
    512, CFX,       1,    0,    0,    0,    0,    2,    1,
    1, BPU,       0,    0,    2,    1,    1,    0,    0,
    0, CFX2,      1,    1,    0,    0,    0,    0,    2,
    257, BPU,       0,    0,    0,    2,    3,    1,    0,
#endif
};
// clang-format on

static void complete_instruction(int stage);

static void advance(int firstStage, int oldStage, int newStage) {
    PCode* instr = pipeline_altivec[oldStage].instr;
    int cycles = instruction_timing[instr->op].cycles[newStage - firstStage];
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
    for (i = 0; (unsigned int)i < sizeof(finalstages) / sizeof(finalstages[0]) - 1; i++) {
        if (pipeline_altivec[finalstages[i]].instr &&
            pipeline_altivec[finalstages[i]].remaining == 0) {
            complete_instruction(finalstages[i]);
        }
    }

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
