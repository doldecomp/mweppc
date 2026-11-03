#include "compiler/Registers.h"
#include "compiler/objects.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"

SInt32 used_virtual_registers[RegClassMax];
SInt32 used_nonvolatile_registers[RegClassMax];
SInt32 assignable_registers[RegClassMax];
SInt32 n_real_registers[RegClassMax];
SInt32 n_scratch_registers[RegClassMax];
SInt32 scratch_registers[RegClassMax][RegisterMax];
SInt32 n_nonvolatile_registers[RegClassMax];
SInt32 nonvolatile_registers[RegClassMax][RegisterMax];
UInt8 reg_state[RegClassMax][RegisterMax];
SInt32 first_temporary_register[RegClassMax];
SInt32 last_temporary_register[RegClassMax];
char* register_class_name[RegClassMax];
char* register_class_format[RegClassMax];
int coloring;
int optimizing;

void init_registers(void) {
    RegClass rclass;
    int j;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        for (j = 0; j < RegisterMax; j++) {
            reg_state[rclass][j] = RegState_Free;
        }
    }

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        used_nonvolatile_registers[rclass] = 0;
    }

    optimizing = TRUE;

    init_target_registers();
    for (rclass = 0; rclass < RegClassMax; rclass++) {
        used_virtual_registers[rclass] = n_real_registers[rclass];
    }

    coloring = TRUE;
}

void assign_register_to_variable(Object* obj, RegClass rclass) {
    VarInfo* vi;
    short reg;

    vi = Registers_GetVarInfo(obj);
    if (optimizing) {
        reg = used_virtual_registers[rclass]++;
    } else {
        reg = obtain_nonvolatile_register(rclass);
        retain_register(obj, rclass, reg);
    }

    vi->flags |= VarInfoFlag_InReg;
    vi->rclass = rclass;
    vi->reg = reg;
}

int available_registers(RegClass rclass) {
    int i;
    int count = 0;

    for (i = 0; i < n_real_registers[rclass]; i++) {
        if (reg_state[rclass][i] == RegState_Free) {
            count++;
        }
    }

    return count;
}

UInt32 volatile_registers(RegClass rclass) {
    UInt32 mask = 0;
    int i;
    int reg;

    for (i = 0; i < n_scratch_registers[rclass]; i++) {
        reg = scratch_registers[rclass][i];
        if (reg_state[rclass][reg] == RegState_Free) {
            mask |= 1 << reg;
        }
    }

    return mask;
}

short obtain_nonvolatile_register(RegClass rclass) {
    int best = -1;

    while (used_nonvolatile_registers[rclass] < n_nonvolatile_registers[rclass]) {
        int reg = nonvolatile_registers[rclass][used_nonvolatile_registers[rclass]++];
        if (reg_state[rclass][reg] == RegState_Free) {
            best = reg;
            break;
        }
    }

    return best;
}

void open_temp_registers(void) {
    RegClass rclass;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        int r = used_virtual_registers[rclass];
        first_temporary_register[rclass] = last_temporary_register[rclass] = r;
    }
}

void check_temp_registers(void) {
    RegClass rclass;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        if (used_virtual_registers[rclass] > 0x7FFF) {
            PPCError_ErrorTerm(PPCERR_TOO_MANY_REGISTERS);
        }
    }

    if (optimizing) {
        return;
    }
    for (rclass = 0; rclass < RegClassMax; rclass++) {
        int r = used_virtual_registers[rclass];
        if (r > last_temporary_register[rclass]) {
            last_temporary_register[rclass] = r;
        }
        if (r > 256) {
            used_virtual_registers[rclass] = first_temporary_register[rclass];
        }
    }
}

void close_temp_registers(void) {
    RegClass rclass;

    for (rclass = 0; rclass < RegClassMax; rclass++) {
        if (used_virtual_registers[rclass] < last_temporary_register[rclass]) {
            used_virtual_registers[rclass] = last_temporary_register[rclass];
        } else {
            last_temporary_register[rclass] = used_virtual_registers[rclass];
        }
    }
}
