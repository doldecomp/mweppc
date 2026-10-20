#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Scheduler.h"

#define latency m601_latency
#define initialize m601_initialize
#define can_issue m601_can_issue
#define issue m601_issue
#define advance_clock m601_advance_clock
#define serializes m601_serializes

typedef enum Stage {
    IU,
    FD,
    FPM,
    FPA,
    FWA,
    BPU,
    NumStages,
    Serialize,
    Unsupported
} Stage;

static struct {
    PCode* instr;

    int remaining;
#ifdef __MWERKS__
} pipeline[NumStages];
#else
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
    Stage stage;

    char latency;

    char cycles[4];
    // clang-format off
} instruction_timing[OPCODE_MAX] = {
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    BPU,          0,   0,   0,  0,   1,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           36,  36,  0,  0,   0,
    IU,           36,  36,  0,  0,   0,
    IU,           5,   5,   0,  0,   0,
    IU,           5,   5,   0,  0,   0,
    IU,           5,   5,   0,  0,   0,
    IU,           5,   5,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           3,   1,   0,  0,   0,
    IU,           3,   1,   0,  0,   0,
    IU,           3,   1,   0,  0,   0,
    IU,           3,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           4,   1,   0,  0,   0,
    IU,           4,   1,   0,  0,   0,
    IU,           4,   1,   0,  0,   0,
    IU,           2,   1,   0,  0,   0,
    IU,           1,   0,   0,  0,   0,
    IU,           1,   0,   0,  0,   0,
    IU,           1,   0,   0,  0,   0,
    IU,           1,   0,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    FD,           4,   1,   1,  1,   1,
    FD,           4,   1,   1,  1,   1,
    Serialize,    1,   1,   0,  0,   1,
    Serialize,    1,   1,   0,  0,   1,
    Serialize,    1,   1,   0,  0,   1,
    Serialize,    0,   0,   0,  0,   1,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           3,   1,   0,  0,   0,
    IU,           3,   1,   0,  0,   0,
    IU,           3,   1,   0,  0,   0,
    IU,           3,   1,   0,  0,   0,
    IU,           3,   1,   0,  0,   0,
    IU,           3,   1,   0,  0,   0,
    IU,           3,   1,   0,  0,   0,
    IU,           3,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    IU,           1,   1,   0,  0,   0,
    FD,           4,   1,   1,  1,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           5,   1,   1,  2,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           31,  1,   1,  28,  1,
    FD,           17,  1,   1,  14,  1,
    FD,           5,   1,   1,  2,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           5,   1,   1,  2,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           5,   1,   1,  2,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           5,   1,   1,  2,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           4,   1,   1,  1,   1,
    FD,           6,   1,   1,  1,   1,
    FD,           6,   1,   1,  1,   1,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    Serialize,    0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    Serialize,    0,   0,   0,  0,   0,
    Serialize,    0,   0,   0,  0,   0,
    Serialize,    0,   0,   0,  0,   0,
    Serialize,    0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    Unsupported,  0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
    IU,           0,   0,   0,  0,   0,
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
