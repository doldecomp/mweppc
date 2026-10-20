#ifndef DRIVER_ERRMGR_H
#define DRIVER_ERRMGR_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern char* __stdcall GetSysErrText(SInt16 msgNbr, char* errMsg);

#ifdef __cplusplus
}
#endif

#endif
