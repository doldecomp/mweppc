#ifndef COMPILER_CIRTRANSFORM_H
#define COMPILER_CIRTRANSFORM_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void CIRTrans_Setup(void);
extern void CIRTrans_Cleanup(void);
extern ENode* CIRTrans_TransformOpAss(ENode* expr);

#ifdef __cplusplus
}
#endif

#endif
