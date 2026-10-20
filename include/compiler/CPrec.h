#ifndef COMPILER_CPREC_H
#define COMPILER_CPREC_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern char* precomp_target_str;

extern void SetupPrecompiler(Boolean isPrecompiling);
extern void CleanupPrecompiler(void);
extern void PreComp_StaticData(Object* obj, const void* data, OLinkList* links, SInt32 size);
extern void PrecompilerWrite(void);
extern void PrecompilerRead(short refnum, void* buffer);

#ifdef __cplusplus
}
#endif

#endif
