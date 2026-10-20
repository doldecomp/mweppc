#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Scheduler.h"

#define latency m821_latency
#define initialize m821_initialize
#define can_issue m821_can_issue
#define issue m821_issue
#define advance_clock m821_advance_clock
#define serializes m821_serializes

typedef enum Stage {
    BranchUnit,
    IntegerUnit,
    Stage2,
    LSU1,
    LSU2,
    CRUnit,
    NumStages,
    FPUnit
} Stage;

static struct {
    PCode* instr;

    int remaining;
} pipeline[NumStages];

#ifndef __MWERKS__
extern short rTEMP_for_PS_spill;
extern short rTEMP_for_VR_spill;
extern int last_unused_vreg_before_spilling;
#endif

enum {
    MaxEntries = 6
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
MachineInfo machine821 = {
    1,
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
    Stage stage;

    char latency;

    char cycles[3];

    char serializes;
    // clang-format off
} instruction_timing[OPCODE_MAX] = {
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 37,  37,  0,  0,  0,
    IntegerUnit, 37,  37,  0,  0,  0,
    IntegerUnit, 5,   5,   0,  0,  0,
    IntegerUnit, 5,   5,   0,  0,  0,
    IntegerUnit, 3,   3,   0,  0,  0,
    IntegerUnit, 5,   5,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 3,   1,   0,  0,  0,
    IntegerUnit, 3,   1,   0,  0,  0,
    IntegerUnit, 3,   1,   0,  0,  0,
    IntegerUnit, 3,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    CRUnit,      1,   1,   0,  0,  0,
    CRUnit,      1,   1,   0,  0,  0,
    CRUnit,      1,   1,   0,  0,  0,
    CRUnit,      1,   1,   0,  0,  0,
    CRUnit,      1,   1,   0,  0,  0,
    CRUnit,      1,   1,   0,  0,  0,
    CRUnit,      1,   1,   0,  0,  0,
    CRUnit,      1,   1,   0,  0,  0,
    CRUnit,      1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    BranchUnit,  2,   2,   0,  0,  0,
    BranchUnit,  2,   2,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    IntegerUnit, 1,   1,   0,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    LSU1,        2,   1,   1,  0,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      4,   2,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      33,  33,  0,  0,  0,
    FPUnit,      18,  18,  0,  0,  0,
    FPUnit,      4,   2,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      4,   2,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      4,   2,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      4,   2,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      18,  18,  0,  0,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      3,   1,   1,  1,  0,
    FPUnit,      5,   1,   1,  1,  0,
    FPUnit,      5,   1,   1,  1,  0,
    LSU1,        1,   0,   0,  0,  0,
    LSU1,        1,   0,   0,  0,  0,
    LSU1,        1,   0,   0,  0,  0,
    LSU1,        1,   0,   0,  0,  0,
    LSU1,        1,   0,   0,  0,  0,
    LSU1,        1,   0,   0,  0,  0,
    LSU1,        1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  1,
    IntegerUnit, 1,   0,   0,  0,  1,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  1,
    IntegerUnit, 1,   0,   0,  0,  1,
    IntegerUnit, 1,   0,   0,  0,  1,
    IntegerUnit, 1,   0,   0,  0,  1,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 1,   0,   0,  0,  0,
    IntegerUnit, 0,   0,   0,  0,  0,
    IntegerUnit, 0,   0,   0,  0,  0,
    IntegerUnit, 0,   0,   0,  0,  0,
    IntegerUnit, 0,   0,   0,  0,  0,
    IntegerUnit, 0,   0,   0,  0,  0,
    IntegerUnit, 0,   0,   0,  0,  0,
    IntegerUnit, 0,   0,   0,  0,  0,
    IntegerUnit, 0,   0,   0,  0,  0,
    IntegerUnit, 0,   0,   0,  0,  0,
    IntegerUnit, 0,   0,   0,  0,  0,
    IntegerUnit, 0,   0,   0,  0,  0,
    IntegerUnit, 0,   0,   0,  0,  0,
    IntegerUnit, 0,   0,   0,  0,  0,
    IntegerUnit, 0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
    BranchUnit,  0,   0,   0,  0,  0,
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
#ifdef __MWERKS__
    if (pipeline[stage].instr) {
        return FALSE;
    }
#else
    if (stage == FPUnit ? rTEMP_for_PS_spill || rTEMP_for_VR_spill
                        : pipeline[stage].instr != NULL) {
        return FALSE;
    }
#endif

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
#ifdef __MWERKS__
    pipeline[stage].instr = instr;
    pipeline[stage].remaining = cycles;
#else
    if (stage == FPUnit) {
        rTEMP_for_PS_spill = (short)(UInt32)instr;
        rTEMP_for_VR_spill = (short)((UInt32)instr >> 16);
        last_unused_vreg_before_spilling = cycles;
    } else {
        pipeline[stage].instr = instr;
        pipeline[stage].remaining = cycles;
    }
#endif
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

    if (pipeline[IntegerUnit].instr && pipeline[IntegerUnit].remaining == 0) {
        complete_instruction(IntegerUnit);
    }
    if (pipeline[LSU2].instr && pipeline[LSU2].remaining == 0) {
        complete_instruction(LSU2);
    }
    if (pipeline[BranchUnit].instr && pipeline[BranchUnit].remaining == 0) {
        complete_instruction(BranchUnit);
    }

    if (pipeline[LSU1].instr && pipeline[LSU1].remaining == 0 && !pipeline[LSU2].instr) {
        advance(LSU1, LSU1, LSU2);
    }
}

static int serializes(PCode* instr) {
    return instruction_timing[instr->op].serializes;
}
