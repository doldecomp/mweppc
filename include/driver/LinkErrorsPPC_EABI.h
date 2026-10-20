#ifndef DRIVER_LINKERRORSPPC_EABI_H
#define DRIVER_LINKERRORSPPC_EABI_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void linkwarning(SInt32 code, ...);
extern void linkerror(SInt32 code, ...);
extern void linkfailed(SInt32 code, int status, ...);
extern void linkalert(int mode);

#ifdef __cplusplus
}
#endif

#endif
