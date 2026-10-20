#ifndef COMPILER_SFPE_PPC_EABI_H
#define COMPILER_SFPE_PPC_EABI_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// The code generators InstrSelection calls for soft-float (-fp soft) operands, in its gen_* shape;
// the result is in a GPR (a pair for doubles).
extern void SFPE_gen_OP(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void SFPE_gen_FLOATCONST(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void SFPE_gen_MONMIN(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void SFPE_gen_ASS(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void SFPE_gen_POSTINCDEC(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void SFPE_gen_INDIRECT(ENode* expr, short outputReg, short outputRegHi, Operand* output);
extern void SFPE_gen_condition(ENode* expr, Operand* output, int branch);
extern void SFPE_gen_TYPCON(ENode* expr, short outputReg, short outputRegHi, Operand* output);

#ifdef __cplusplus
}
#endif

#endif
