#ifndef DRIVER_CLLOADANDCACHE_H
#define DRIVER_CLLOADANDCACHE_H

#include "compiler/common.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

extern short FixTextHandle(Handle txt);
extern int LoadAndCacheFile(OSSpec* spec, Handle* texthandle, Boolean* precomp);
extern void CopyFileText(Handle src, char** text, SInt32* textsize);

#ifdef __cplusplus
}
#endif

#endif
