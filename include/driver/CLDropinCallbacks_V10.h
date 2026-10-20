#ifndef DRIVER_CLDROPINCALLBACKS_V10_H
#define DRIVER_CLDROPINCALLBACKS_V10_H

#include "compiler/common.h"
#include "driver/cmdline.h"
#include "driver/CWPlugins.h"
#include "driver/Files.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
typedef struct shellContextType {
    struct Plugin* plugin;
    Boolean userAccessPathsChanged;
    Boolean systemAccessPathsChanged;
} shellContextType;
#pragma pack(pop)

#pragma pack(push, 2)
struct IDEAccessPathList {
    SInt32 userPathCount;
    struct IDEAccessPath* userPaths;
    SInt32 systemPathCount;
    struct IDEAccessPath* systemPaths;
    Boolean alwaysSearchUserPaths;
    Boolean convertPaths;
};
struct IDEAccessPath {
    CWFileSpec pathSpec;
    Boolean recursive;
    SInt32 subdirectoryCount;
    CWFileSpec* subdirectories;
};
#pragma pack(pop)

extern CW_CALLBACK UCBFindAndLoadFile(
    CWPluginContext context, const char* infilename, CWFileInfo* fileinfo
);
extern CW_CALLBACK UCBGetFileText(
    CWPluginContext context,
    const CWFileSpec* filespec,
    const char** text,
    SInt32* textLength,
    short* filedatatype
);
extern CW_CALLBACK UCBReleaseFileText(CWPluginContext context, const char* text);
extern CW_CALLBACK UCBSecretAttachHandle(
    CWPluginContext context, Handle handle, CWMemHandle* memHandle
);
extern CW_CALLBACK UCBSecretDetachHandle(
    CWPluginContext context, CWMemHandle memHandle, Handle* handle
);

#ifdef __cplusplus
}
#endif

#endif
