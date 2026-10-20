#ifndef DRIVER_CLPROJ_H
#define DRIVER_CLPROJ_H

#include "compiler/common.h"
#include "driver/cmdline.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Project {
    Target* targets;
    OSSpec projectDirectory;
} Project;

extern Boolean Proj_Initialize(Project* proj);
extern Boolean Proj_Terminate(Project* proj);

#ifdef __cplusplus
}
#endif

#endif
