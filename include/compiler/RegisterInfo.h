#ifndef COMPILER_REGISTERINFO_H
#define COMPILER_REGISTERINFO_H

#include "compiler/common.h"
#include "compiler/Registers.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
typedef struct RegInfo {
    struct RegInfo* next;
    Object* object;
    HashNameNode* name;
    ObjRegInfo reginfo;
    struct NameSpace* nspace;
    char is_static;
} RegInfo;
#pragma pack(pop)

extern RegInfo* reg_info_list;
extern UInt8 ok_for_global_registers;
extern SInt32 num_register_variables[RegClassMax];
extern short last_exception_register[RegClassMax];
extern short last_argument_register[RegClassMax];
extern short first_fe_temporary_register[RegClassMax];
extern SInt16 _FP_;
extern SInt16 _CALLER_SP_;
extern char* special_register_names[RegClassMax][32];
extern short spr_to_sysreg[4];

extern void asm_used_register(RegClass rclass, short reg);
extern void retain_register(Object* obj, RegClass rclass, short reg);
extern void retain_GPR_pair(Object* obj, short reg, short regHi);
extern int is_register_object(Object* obj);
extern ObjRegInfo* Registers_GetRegInfo(Object* obj);
extern int is_global_registers_variable(Object* obj);
extern int GetABIFirstNonVolatile(RegClass rclass);
extern char GetRegisterClassName(RegClass rclass);
extern void setup_global_registers(void);
extern void finish_global_registers(void);
extern void setup_diagnostic_reg_strings(void);
extern void init_target_registers(void);
extern void assign_register_by_type(Object* obj);
extern void open_fe_temp_registers(void);
extern void set_last_exception_registers(void);
extern VarInfo* Registers_GetVarInfo(Object* obj);
extern UInt32 colored_vrs_as_vrsave(PCodeBlock* block);
extern void save_before_coloring_nonvolatile_registers(RegClass rclass);
extern void reset_nonvolatile_registers(RegClass rclass);
#if VERSION < VERSION_GC_2_5
extern int is_nonvolatile_register(RegClass rclass, int reg);
#endif
extern void init_endian(void);
extern void update_asm_nonvolatile_registers(void);

#define OBJECT_REG(obj) (Registers_GetVarInfo((obj)) ? (Registers_GetVarInfo((obj))->reg) : 0)
#define OBJECT_REG_HI(obj) (Registers_GetVarInfo((obj)) ? (Registers_GetVarInfo((obj))->regHi) : 0)
#define ALLOC_GPR() (used_virtual_registers[RegClass_GPR]++)
#define ALLOC_FPR() (used_virtual_registers[RegClass_FPR]++)
#define ALLOC_VR() (used_virtual_registers[RegClass_VR]++)

#ifdef __cplusplus
}
#endif

#endif
