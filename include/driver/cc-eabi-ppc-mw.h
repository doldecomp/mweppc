#ifndef DRIVER_CC_EABI_PPC_MW_H
#define DRIVER_CC_EABI_PPC_MW_H

#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void GetStaticTarget(OSType* cpu, OSType* os);
extern void GetStaticPluginType(OSType* language, OSType* plugintype);
extern void GetStaticParserPluginType(OSType* style);
extern int RegisterStaticTargetPlugins(void);
extern int RegisterStaticTargetResources(void);

#ifdef __cplusplus
}
#endif

#endif
