#ifndef DRIVER_MACSPECS_H
#define DRIVER_MACSPECS_H

#include "compiler/common.h"
#include "driver/oslib.h"
#include "driver/Files.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int __stdcall OS_OSSpec_To_FSSpec(const OSSpec* spec, FSSpec* fss);
extern int __stdcall OS_FSSpec_To_OSSpec(const FSSpec* fss, OSSpec* spec);
extern int __stdcall OS_GetRsrcOSSpec(const OSSpec* spec, OSSpec* rspec, Boolean create);

#ifdef __cplusplus
}
#endif

#endif
