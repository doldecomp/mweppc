#ifndef DRIVER_CLPLUGINREQUESTS_H
#define DRIVER_CLPLUGINREQUESTS_H

#include "compiler/common.h"
#include "driver/cmdline.h"
#include "driver/CWPlugins.h"
#include "driver/DropInCompilerLinker.h"
#include "driver/MacFileTypes.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Boolean SendParserRequest(
    Plugin* plugin,
    Target* target,
    CWCommandLineArgs* args,
    OSType cpu,
    OSType os,
    int numPlugins,
    const CLPluginInfo* pluginInfo,
    int numPanels,
    const char** panelNames,
    CWCommandLineArgs* plugin_args,
    CWCommandLineArgs* panel_args,
    const char* build_date,
    const char* build_time,
    const ToolVersionInfo* build_tool
);
extern Boolean SendCompilerRequest(Plugin* plugin, File* file, SInt16 stage);
extern Boolean SendTargetInfoRequest(Target* targ, Plugin* linker, SInt32 dropinflags);
extern Boolean SendLinkerRequest(Plugin* plugin, SInt32 dropinflags, CWTargetInfo* targetInfo);
extern Boolean SendDisassemblerRequest(Plugin* linker, File* file);
extern Boolean SendInitOrTermRequest(Plugin* plugin, Boolean reqIsInitialize);
extern void AddFileTypeMappingList(OSFileTypeMappings** list, const OSFileTypeMappingList* entry);

#ifdef __cplusplus
}
#endif

#endif
