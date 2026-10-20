#ifndef DRIVER_CLINCLUDEFILECACHE_H
#define DRIVER_CLINCLUDEFILECACHE_H

#include "compiler/common.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void InitializeIncludeCache(void);
extern void CleanupIncludeCache(void);
extern void CacheIncludeFile(OSSpec* spec, Handle text, Boolean precompiled);
extern Handle CachedIncludeFile(OSSpec* spec, Boolean* precompiled);
extern void FreeIncludeFile(Handle text);

#ifdef __cplusplus
}
#endif

#endif
