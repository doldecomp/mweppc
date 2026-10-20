#ifndef DRIVER_CLTOOLEXEC_H
#define DRIVER_CLTOOLEXEC_H

#include "compiler/common.h"
#include "driver/cmdline.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void AppendArgumentList(int* argc, char*** argv, const char* str);
extern int SetupTemporaries(void);
extern int DeleteTemporaries(void);
extern int ExecuteLinker(
    Plugin* plugin, SInt32 dropinflags, File* file, char* stdoutfile, char* stderrfile
);

#ifdef __cplusplus
}
#endif

#endif
