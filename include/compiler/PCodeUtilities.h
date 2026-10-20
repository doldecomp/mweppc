#ifndef COMPILER_PCODEUTILITIES_H
#define COMPILER_PCODEUTILITIES_H

#include "compiler/common.h"
#include "compiler/PCodeInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void pcsetrecordbit(PCode* pc);
extern void pcsetsideeffects(PCode* pc);
extern void pcsetlinkbit(PCode* pc);
extern void branch_label(PCodeLabel* label);
extern void branch_conditional(short crfield, short compareop, short sense, PCodeLabel* label);
extern void branch_always(PCodeLabel* label);
extern void branch_decrement_always(Opcode opcode, PCodeLabel* label);
extern void branch_indirect(Object* obj);
extern int branch_count_volatiles(void);
extern PCodeArg* branch_record_volatiles(PCodeArg* arglist, UInt32* masks);
extern void branch_subroutine(Object* obj, short add_nop, UInt32* masks);
extern void branch_subroutine_ctr(UInt32* masks);
extern void add_immediate(short dest_reg, short base_reg, Object* obj, SInt16 offset);
extern PCode* add_immediate_lo(
    short dest_reg, short base_reg, Object* obj, SInt16 offset, char add_to_block
);
extern PCode* op_absolute_ha(
    short dest_reg, short base_reg, Object* obj, short offset, char add_to_block
);
extern void load_store_register(
    Opcode opcode, short dest_reg, short base_reg, Object* obj, SInt32 offset
);
/* Gekko paired singles */
extern Opcode Select_Gecko_Load_Store(Object* obj, int offset, int load);
extern void gecko_load_store_register(
    short dest_reg, short base_reg, Object* obj, SInt32 offset, SInt32 w, SInt32 i, int load
);

#ifdef __cplusplus
}
#endif

#endif
