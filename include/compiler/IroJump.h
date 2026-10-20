#ifndef COMPILER_IROJUMP_H
#define COMPILER_IROJUMP_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Boolean IRO_DoJumpChaining(void);
extern void IRO_MakeReachable(IRONode* node);
extern Boolean IRO_RemoveUnreachable(void);
extern Boolean IRO_RemoveRedundantJumps(void);
extern Boolean IRO_RemoveLabels(void);

#ifdef __cplusplus
}
#endif

#endif
