#ifndef DRIVER_CLPLUGINS_H
#define DRIVER_CLPLUGINS_H

#include "compiler/common.h"
#include "driver/cmdline.h"
#include "driver/CWParserPluginsPrivate.h"
#include "driver/CWPlugins.h"
#include "driver/Files.h"
#include "driver/MacFileTypes.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Plugin {
    BasePluginCallbacks* cb;
    CompilerLinkerPluginCallbacks* cl_cb;
    ParserPluginCallbacks* pr_cb;
    CWPluginContext context;
    char* cached_ascii_version;
    struct Plugin* next;
};

extern const ToolVersionInfo* Plugin_GetToolVersionInfo(void);
extern const char* Plugin_GetDropInName(Plugin* pl);
extern DropInFlags* Plugin_GetDropInFlags(Plugin* pl);
extern OSType Plugin_GetPluginType(Plugin* pl);
extern const CWObjectFlags* Plugin_CL_GetObjectFlags(Plugin* pl);
extern Boolean Plugin_CL_MatchesTarget(Plugin* pl, OSType cpu, OSType os, Boolean exact);
extern Boolean Plugin_CL_WriteObjectFile(
    Plugin* pl, FSSpec* srcfss, FSSpec* outfss, OSType creator, OSType type, Handle data
);
extern Boolean Plugin_CL_GetCompilerMapping(
    Plugin* pl, OSType type, const char* ext, UInt32* flags
);
extern Plugin* Plugin_New(
    const BasePluginCallbacks* cb,
    const CompilerLinkerPluginCallbacks* cl_cb,
    const ParserPluginCallbacks* pr_cb
);
extern Boolean Plugin_VerifyPanels(Plugin* pl);
extern void Plugins_Init(void);
extern void Plugins_Term(void);
extern int Plugins_Add(Plugin* pl);
extern Plugin* Plugins_CL_MatchTarget(
    Plugin* list, OSType cpu, OSType os, OSType type, OSType lang
);
extern Plugin* Plugins_GetPluginForFile(
    Plugin* list,
    OSType plugintype,
    OSType cpu,
    OSType os,
    OSType type,
    const char* ext,
    OSType lang
);
extern Plugin* Plugins_GetLinker(Plugin* list, OSType cpu, OSType os);
extern Plugin* Plugins_GetPreLinker(Plugin* list, OSType cpu, OSType os);
extern Plugin* Plugins_GetPostLinker(Plugin* list, OSType cpu, OSType os);
extern Plugin* Plugins_GetParserForPlugin(
    Plugin* list,
    OSType style,
    int numPlugins,
    CLPluginInfo* plugins,
    OSType cpu,
    OSType os,
    int numPanels,
    const char** panelNames
);
extern int Plugins_GetPluginList(Plugin* list, int* numPlugins, CLPluginInfo** pluginInfo);
extern int Plugins_GetPrefPanelUnion(Plugin* list, int* numPanels, const char*** panelNames);
extern int Plugins_AddFileTypeMappingsForTarget(
    Plugin* list, OSFileTypeMappings** mlist, OSType cpu, OSType os
);
extern SInt16 Plugin_Call(Plugin* pl, void* context);

#ifdef __cplusplus
}
#endif

#endif
