#ifndef DRIVER_TOOLHELPERS_H
#define DRIVER_TOOLHELPERS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OutputOrdering_None = 0,
    OutputOrdering_After = 1,
    OutputOrdering_Before = 2
};

extern char linkerOutputFilename[264];
extern Boolean setLinkerOutputFilename;
extern Boolean setOutputDirectory;
extern SInt16 outputOrdering;

extern int Opt_HandleOutputName(const char* opt, void* var, const char* filename, int flags);
extern int ValidateToolState(Boolean mustHaveFiles);
extern int Opt_IncreaseVerbosity(const char* opt, void* var, const char* arg, int flags);
extern int Opt_SetStage(const char* opt, void* str, const char* arg, int flags);

#ifdef __cplusplus
}
#endif

#endif
