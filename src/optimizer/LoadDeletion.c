#include "compiler/LoadDeletion.h"
#include "compiler/CopyPropagation.h"
#include "compiler/CParser.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/Registers.h"

int deletedloads;

// a load of an immediate (li or an AltiVec splat) into a virtual register
static int is_load(PCode* instr) {
    return (instr->op == PC_LI ||
            instr->op == PC_VSPLTISB ||
            instr->op == PC_VSPLTISH ||
            instr->op == PC_VSPLTISW) &&
        !(instr->args[0].data.reg.reg < n_real_registers[instr->args[0].arg]);
}

static int loadpropagatestouse(int candidateID, int useID) {
    return FALSE;
}

static void deleteload(int id) {
    Candidate* candidate;

    candidate = Candidates + id;
    if (candidate->list || (candidate->pcode->flags & fSideEffects)) {
        return;
    }
    if (candidate->pcode->args[0].data.reg.reg < n_real_registers[candidate->pcode->args[0].arg]) {
        return;
    }

    deletepcode(candidate->pcode);
    deletedloads = TRUE;
}

// clang-format off
static Propagation load_immediate_prop = {
    &is_load,
    &loadpropagatestouse,
    &deleteload,
    "LOAD IMMEDIATE",
    "LOAD_IMMEDIATES",
    "l%ld",
    fSideEffects | fRecordBit,
    FALSE
};
// clang-format on

void deletedeadloads(Object* proc) {
    propagateinstructions(
        proc, &load_immediate_prop, (copts.optimizationlevel >= 4) ? 4 : 1, FALSE
    );
    deletedloads = propagated_instructions;
}
