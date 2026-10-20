#ifndef COMPILER_REGISTERS_H
#define COMPILER_REGISTERS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    RegisterInvalid = -1,
    Register0 = 0,
    Register2 = 2,
    Register3 = 3,
    Register4 = 4,
    RegisterMax = 32
};

enum {
    RegState_Free = 0,
    RegState_Retained = 1,
    RegState_Reserved = 2
};

extern SInt32 used_virtual_registers[RegClassMax];
extern SInt32 used_nonvolatile_registers[RegClassMax];
extern SInt32 assignable_registers[RegClassMax];
extern SInt32 n_real_registers[RegClassMax];
extern SInt32 n_scratch_registers[RegClassMax];
extern SInt32 scratch_registers[RegClassMax][RegisterMax];
extern SInt32 n_nonvolatile_registers[RegClassMax];
extern SInt32 nonvolatile_registers[RegClassMax][RegisterMax];
extern UInt8 reg_state[RegClassMax][RegisterMax];
extern SInt32 last_temporary_register[RegClassMax];
extern char* register_class_name[RegClassMax];
extern char* register_class_format[RegClassMax];
extern int coloring;
extern int optimizing;

extern void init_registers(void);
extern void assign_register_to_variable(Object* obj, RegClass rclass);
extern int available_registers(RegClass rclass);
extern UInt32 volatile_registers(RegClass rclass);
extern short obtain_nonvolatile_register(RegClass rclass);
extern void open_temp_registers(void);
extern void check_temp_registers(void);
extern void close_temp_registers(void);

#ifdef __cplusplus
}
#endif

#endif
