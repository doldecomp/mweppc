#ifndef COMPILER_SCHEDULER_H
#define COMPILER_SCHEDULER_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*LatencyFunc)(PCode* instr);
typedef void (*InitializeFunc)(void);
typedef int (*CanIssueFunc)(PCode* instr);
typedef void (*IssueFunc)(PCode* instr);
typedef void (*AdvanceClockFunc)(void);
typedef int (*SerializesFunc)(PCode* instr);
typedef int (*UsesVPermuteUnitFunc)(PCode* instr);
typedef struct MachineInfo {
    int issue_width;
    int renames_registers;
    int branch_latency;
    LatencyFunc latency;
    InitializeFunc initialize;
    CanIssueFunc can_issue;
    IssueFunc issue;
    AdvanceClockFunc advance_clock;
    SerializesFunc serializes;
    UsesVPermuteUnitFunc uses_vpermute_unit;
} MachineInfo;

extern MachineInfo machine601;
extern MachineInfo machine603;
extern MachineInfo machine603e;
extern MachineInfo machine604;
extern MachineInfo machine7400;
extern MachineInfo machine7450;
extern MachineInfo machine750;
extern MachineInfo machine821;

extern void scheduleinstructions(Boolean includePrologEpilog);
extern int is_dependent(PCode* a, PCode* b, char rclass);
extern int uses_vpermute_unit(PCode* instr);
extern int default_uses_vpermute_unit(PCode* instr);

#ifdef __cplusplus
}
#endif

#endif
