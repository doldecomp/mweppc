#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Scheduler.h"

#define latency m7400_latency
#define initialize m7400_initialize
#define can_issue m7400_can_issue
#define issue m7400_issue
#define advance_clock m7400_advance_clock
#define serializes m7400_serializes

typedef enum Stage {
    BPU,
    IU1,
    IU2,

    LSU1,
    LSU2,

    FPU1,
    FPU2,
    FPU3,

    SRU,
    VSIU,
    VPU,

    VCIU1,
    VCIU2,
    VCIU3,

    VFPU1,
    VFPU2,
    VFPU3,
    VFPU4,

    NumStages
} Stage;

static struct {
    PCode* instr;

    int remaining;
} pipeline[NumStages];

static PCode* iu1_completed_instruction;
static PCode* iu2_completed_instruction;

enum {
    MaxEntries = 8
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
    Stage stage;

    char latency;

    char cycles[4];

    char serializes;
    // clang-format off
} instruction_timing[OPCODE_MAX] = {
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   3,   1,   2,  0,  0,  0,
    LSU1,   3,   1,   2,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   3,   1,   2,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU1,    19,  19,  0,  0,  0,  0,
    IU1,    19,  19,  0,  0,  0,  0,
    IU1,    5,   5,   0,  0,  0,  0,
    IU1,    6,   5,   0,  0,  0,  0,
    IU1,    3,   3,   0,  0,  0,  0,
    IU1,    5,   5,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    3,   1,   0,  0,  0,  0,
    IU2,    3,   1,   0,  0,  0,  0,
    IU2,    3,   1,   0,  0,  0,  0,
    IU2,    3,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    2,   2,   0,  0,  0,  1,
    SRU,    2,   2,   0,  0,  0,  1,
    SRU,    2,   2,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    2,   2,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  0,
    SRU,    3,   3,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    2,   2,   0,  0,  0,  1,
    SRU,    3,   3,   0,  0,  0,  1,
    SRU,    2,   2,   0,  0,  0,  1,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   4,   2,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   31,  31,  0,  0,  0,  0,
    FPU1,   17,  17,  0,  0,  0,  0,
    FPU1,   4,   2,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   4,   2,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   4,   2,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   4,   2,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   10,  10,  0,  0,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    FPU1,   3,   1,   1,  1,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  1,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  1,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  1,
    SRU,    1,   1,   0,  0,  0,  0,
    SRU,    3,   3,   0,  0,  0,  0,
    SRU,    2,   2,   0,  0,  0,  1,
    SRU,    3,   3,   0,  0,  0,  0,
    SRU,    2,   2,   0,  0,  0,  1,
    FPU1,   1,   1,   0,  0,  0,  0,
    FPU1,   1,   1,   0,  0,  0,  0,
    FPU1,   1,   1,   0,  0,  0,  0,
    SRU,    2,   2,   0,  0,  0,  1,
    FPU1,   1,   1,   0,  0,  0,  0,
    FPU1,   1,   1,   0,  0,  0,  0,
    LSU1,   1,   1,   0,  0,  0,  0,
    LSU1,   1,   1,   0,  0,  0,  0,
    LSU1,   1,   1,   0,  0,  0,  0,
    LSU1,   1,   1,   0,  0,  0,  0,
    LSU1,   1,   1,   0,  0,  0,  1,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  1,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   0,   0,  0,  0,  0,
    IU2,    1,   0,   0,  0,  0,  0,
    IU2,    1,   0,   0,  0,  0,  0,
    IU2,    1,   0,   0,  0,  0,  0,
    IU2,    1,   0,   0,  0,  0,  0,
    IU2,    1,   0,   0,  0,  0,  0,
    IU2,    1,   0,   0,  0,  0,  0,
    IU2,    1,   0,   0,  0,  0,  0,
    IU2,    1,   0,   0,  0,  0,  0,
    IU2,    1,   0,   0,  0,  0,  0,
    IU2,    1,   0,   0,  0,  0,  0,
    IU2,    1,   0,   0,  0,  0,  0,
    IU2,    1,   0,   0,  0,  0,  0,
    LSU1,   3,   1,   2,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    IU2,    1,   1,   0,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    LSU1,   2,   1,   1,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  1,
    VSIU,   1,   1,   0,  0,  0,  1,
    VSIU,   1,   1,   0,  0,  0,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VCIU1,  3,   1,   1,  1,  0,  0,
    VFPU1,  4,   1,   1,  1,  1,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    VSIU,   1,   1,   0,  0,  0,  0,
    VPU,    1,   1,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
    BPU,    0,   0,   0,  0,  0,  0,
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
