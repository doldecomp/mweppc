#ifndef DRIVER_CLWRITEOBJECTFILE_H
#define DRIVER_CLWRITEOBJECTFILE_H

#include "driver/cmdline.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int WriteObjectFile(File* file, OSType maccreator, OSType mactype);

extern int WriteBrowseData(File* file, OSType maccreator, OSType mactype);

#ifdef __cplusplus
}
#endif

#endif
