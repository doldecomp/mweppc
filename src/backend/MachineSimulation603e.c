#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Scheduler.h"

#define latency m603e_latency
#define initialize m603e_initialize
#define can_issue m603e_can_issue
#define issue m603e_issue
#define advance_clock m603e_advance_clock
#define serializes m603e_serializes

typedef enum Stage {
    BPU,
    IU,
    LSU1,
    LSU2,
    FPU1,
    FPU2,
    FPU3,
    SRU,
    NumStages
} Stage;

static struct {
    PCode* instr;

    int remaining;
} pipeline[NumStages];

enum {
    MaxEntries = 5
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
MachineInfo machine603e = {
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
    LSU1,  2,   1,   1,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    37,  37,  0,  0,  0,
    IU,    37,  37,  0,  0,  0,
    IU,    5,   5,   0,  0,  0,
    IU,    5,   5,   0,  0,  0,
    IU,    3,   3,   0,  0,  0,
    IU,    5,   5,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    3,   1,   0,  0,  0,
    IU,    3,   1,   0,  0,  0,
    IU,    3,   1,   0,  0,  0,
    IU,    3,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    SRU,   1,   1,   0,  0,  0,
    SRU,   1,   1,   0,  0,  0,
    SRU,   1,   1,   0,  0,  0,
    SRU,   1,   1,   0,  0,  0,
    SRU,   1,   1,   0,  0,  0,
    SRU,   1,   1,   0,  0,  0,
    SRU,   1,   1,   0,  0,  0,
    SRU,   1,   1,   0,  0,  0,
    SRU,   1,   1,   0,  0,  0,
    SRU,   2,   2,   0,  0,  0,
    SRU,   2,   2,   0,  0,  0,
    SRU,   2,   2,   0,  0,  0,
    SRU,   1,   1,   0,  0,  0,
    SRU,   1,   1,   0,  0,  1,
    SRU,   1,   1,   0,  0,  1,
    SRU,   1,   1,   0,  0,  1,
    SRU,   1,   1,   0,  0,  1,
    SRU,   1,   1,   0,  0,  0,
    SRU,   1,   1,   0,  0,  0,
    SRU,   1,   1,   0,  0,  0,
    SRU,   1,   1,   0,  0,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    SRU,   1,   1,   0,  0,  1,
    SRU,   1,   1,   0,  0,  1,
    SRU,   1,   1,   0,  0,  1,
    SRU,   1,   1,   0,  0,  1,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
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
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  4,   2,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  33,  33,  0,  0,  0,
    FPU1,  18,  18,  0,  0,  0,
    FPU1,  4,   2,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  4,   2,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  4,   2,   1,  1,  0,
    FPU1,  3,   1,   1,  1,  0,
    FPU1,  4,   2,   1,  1,  0,
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
    IU,    1,   1,   0,  0,  1,
    IU,    1,   1,   0,  0,  1,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  1,
    FPU1,  1,   1,   0,  0,  0,
    FPU1,  1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  1,
    IU,    1,   1,   0,  0,  1,
    IU,    1,   1,   0,  0,  1,
    IU,    1,   1,   0,  0,  1,
    IU,    1,   1,   0,  0,  0,
    IU,    1,   1,   0,  0,  1,
    IU,    1,   1,   0,  0,  1,
    IU,    0,   0,   0,  0,  0,
    IU,    0,   0,   0,  0,  0,
    IU,    0,   0,   0,  0,  0,
    IU,    0,   0,   0,  0,  0,
    IU,    0,   0,   0,  0,  0,
    IU,    0,   0,   0,  0,  0,
    IU,    0,   0,   0,  0,  0,
    IU,    0,   0,   0,  0,  0,
    IU,    0,   0,   0,  0,  0,
    IU,    0,   0,   0,  0,  0,
    IU,    0,   0,   0,  0,  0,
    IU,    0,   0,   0,  0,  0,
    IU,    0,   0,   0,  0,  0,
    IU,    0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
    BPU,   0,   0,   0,  0,  0,
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
    if (pipeline[stage].instr) {
        if (stage == IU) {
            switch (instr->op) {
                case PC_ADD:
                case PC_ADDC:
                case PC_ADDI:
                case PC_ADDIS:
                case PC_CMPI:
                case PC_CMP:
                case PC_CMPLI:
                case PC_CMPL:
                    if (is_dependent(instr, pipeline[IU].instr, RegClass_GPR)) {
                        return FALSE;
                    }
                    if (!pipeline[SRU].instr) {
                        return TRUE;
                    }
            }
        }
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
    if (stage == IU && pipeline[IU].instr) {
        stage = SRU;
    }
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
