#ifndef DRIVER_LOWMEM_H
#define DRIVER_LOWMEM_H

#include "compiler/common.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

extern UInt32 LMGetTicks(void);
extern OSErr LMGetMemErr(void);

#ifdef __cplusplus
}
#endif

#endif
