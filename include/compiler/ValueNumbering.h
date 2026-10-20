#ifndef COMPILER_VALUENUMBERING_H
#define COMPILER_VALUENUMBERING_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int removedcommonsubexpressions;
extern int nextvaluenumber;

extern void killmemory(Alias* alias, PCode* newValue);
extern void removecommonsubexpressions(Object* proc, int moreAggressive);

#ifdef __cplusplus
}
#endif

#endif
