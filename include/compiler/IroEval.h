#ifndef COMPILER_IROEVAL_H
#define COMPILER_IROEVAL_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void IRO_InitializeIsAssociativeENodeTypeArray(void);
extern void IRO_TruncateValueToType(CInt64* val, Type* type);
// bitfieldType's bit length gives the mask
extern void IRO_TruncateBitfieldValueToType(CInt64* val, Type* type, TypeBitfield* bitfieldType);
extern void IRO_ConstantFolding(void);
extern Boolean IRO_EvaluateConditionals(void);
extern int IRO_SimplifyConditionals(void);
extern Boolean IRO_EvaluateDefinitePointers(Object* func);

#ifdef __cplusplus
}
#endif

#endif
