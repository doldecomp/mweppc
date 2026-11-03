#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Scheduler.h"

#define latency m604_latency
#define initialize m604_initialize
#define can_issue m604_can_issue
#define issue m604_issue
#define advance_clock m604_advance_clock
#define serializes m604_serializes

typedef enum Stage {
    SCIU,
    SCIU2,
    MCIU,
    FPU1,
    FPU2,
    FPU3,
    LSU1,
    LSU2,
    BPU,
    NumStages
} Stage;

static struct {
    PCode* instr;

    int remaining;
} pipeline[NumStages];

static PCode* sciu_completed_instruction;
static PCode* sciu2_completed_instruction;

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
    Stage stage;

    char latency;

    char cycles[3];

    char serializes;
    // clang-format off
} instruction_timing[OPCODE_MAX] = {
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    BPU,   0,   0,   0,  0,  1,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    LSU1,  2,   1,   1,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    MCIU,  20,  20,  0,  0,  0,
    MCIU,  20,  20,  0,  0,  0,
    MCIU,  4,   4,   0,  0,  0,
    MCIU,  4,   4,   0,  0,  0,
    MCIU,  3,   3,   0,  0,  0,
    MCIU,  4,   4,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  3,   1,   0,  0,  0,
    SCIU,  3,   1,   0,  0,  0,
    SCIU,  3,   1,   0,  0,  0,
    SCIU,  3,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    BPU,   1,   1,   0,  0,  0,
    BPU,   1,   1,   0,  0,  0,
    BPU,   1,   1,   0,  0,  0,
    BPU,   1,   1,   0,  0,  0,
    BPU,   1,   1,   0,  0,  0,
    BPU,   1,   1,   0,  0,  0,
    BPU,   1,   1,   0,  0,  0,
    BPU,   1,   1,   0,  0,  0,
    BPU,   1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  3,   3,   0,  0,  0,
    MCIU,  3,   3,   0,  0,  0,
    MCIU,  3,   3,   0,  0,  0,
    MCIU,  3,   3,   0,  0,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    LSU1,  1,   0,   0,  0,  1,
    LSU1,  1,   0,   0,  0,  1,
    LSU1,  1,   0,   0,  0,  1,
    LSU1,  1,   1,   0,  0,  1,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    SCIU,  1,   1,   0,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    LSU1,  3,   1,   1,  0,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  32,  32,  0,  0,  0,
    FPU1,  18,  18,  0,  0,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  18,  18,  0,  0,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  5,   1,   1,  1,  0,
    FPU1,  5,   1,   1,  1,  0,
    LSU1,  1,   1,   0,  0,  0,
    LSU1,  1,   1,   0,  0,  0,
    LSU1,  1,   1,   0,  0,  0,
    LSU1,  1,   1,   0,  0,  0,
    LSU1,  1,   1,   0,  0,  0,
    LSU1,  1,   1,   0,  0,  0,
    LSU1,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  1,
    MCIU,  1,   1,   0,  0,  1,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  1,
    FPU1,  1,   1,   0,  0,  0,
    FPU1,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  1,
    MCIU,  1,   1,   0,  0,  1,
    MCIU,  1,   1,   0,  0,  1,
    MCIU,  1,   1,   0,  0,  1,
    MCIU,  1,   1,   0,  0,  0,
    MCIU,  1,   1,   0,  0,  1,
    MCIU,  1,   1,   0,  0,  1,
    MCIU,  0,   0,   0,  0,  0,
    MCIU,  0,   0,   0,  0,  0,
    MCIU,  0,   0,   0,  0,  0,
    MCIU,  0,   0,   0,  0,  0,
    MCIU,  0,   0,   0,  0,  0,
    MCIU,  0,   0,   0,  0,  0,
    MCIU,  0,   0,   0,  0,  0,
    MCIU,  0,   0,   0,  0,  0,
    MCIU,  0,   0,   0,  0,  0,
    MCIU,  0,   0,   0,  0,  0,
    MCIU,  0,   0,   0,  0,  0,
    MCIU,  0,   0,   0,  0,  0,
    MCIU,  0,   0,   0,  0,  0,
    MCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
    SCIU,  0,   0,   0,  0,  0,
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
