#ifndef DRIVER_CLFILEOPS_H
#define DRIVER_CLFILEOPS_H

#include "compiler/common.h"
#include "driver/cmdline.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int GetOutputFile(File* file, SInt16 stage);
extern int StoreObjectFile(File* file);
extern int CompileFilesInProject(void);
extern int LinkProject(void);

#ifdef __cplusplus
}
#endif

#endif
