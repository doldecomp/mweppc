#ifndef DRIVER_CWPARSERPLUGINSPRIVATE_H
#define DRIVER_CWPARSERPLUGINSPRIVATE_H

#include "compiler/common.h"
#include "driver/cmdline.h"
#include "driver/Files.h"
#include "driver/CWPlugins.h"

#ifdef __cplusplus
extern "C" {
#endif

// A tool's command line (the plugin SDK's)
struct CWCommandLineArgs {
    int argc;
    char** argv;
    char** envp;
};
#pragma pack(push, 2)
// The tool's version information: what CWParserGetToolInfo stores in parseopts.toolVersion
struct ToolVersionInfo {
    char* company;
    char* product;
    char* tool;
    char* copyright;
    char* version;
};
#pragma pack(pop)
extern CW_CALLBACK CWParserGetBuildDate(
    CWPluginContext context, const char** bdate, const char** btime
);
extern CW_CALLBACK CWParserGetCommandLine(CWPluginContext context, CWCommandLineArgs** args);
extern CW_CALLBACK CWParserGetToolInfo(
    CWPluginContext context, const ToolVersionInfo** toolVersionInfo
);
extern CW_CALLBACK CWParserGetTargetInfo(CWPluginContext context, CWDataType* cpu, CWDataType* os);
extern CW_CALLBACK CWParserGetPlugins(
    CWPluginContext context, int* numPlugins, const CLPluginInfo** pluginInfo
);
extern CW_CALLBACK CWParserGetPanels(
    CWPluginContext context, int* numPanels, const char*** panelNames
);
extern CW_CALLBACK CWParserStoreCommandLineForPlugin(
    CWPluginContext context, int index, const CWCommandLineArgs* args
);
extern CW_CALLBACK CWParserSetNamedPreferences(
    CWPluginContext context, const char* panelName, Handle paneldata
);
extern CW_CALLBACK CWParserAddAccessPath(CWPluginContext context, const CWNewAccessPathInfo* api);
extern CW_CALLBACK CWParserSetOutputFileDirectory(
    CWPluginContext context, const CWFileSpec* idefss
);
extern CW_CALLBACK CWParserSetFileOutputName(
    CWPluginContext context, SInt32 position, short which, const char* outfilename
);
extern CW_CALLBACK CWParserSwapAccessPaths(CWPluginContext context);
extern CW_CALLBACK CWParserCreateVirtualFile(
    CWPluginContext context, const char* name, CWMemHandle text
);
extern CW_CALLBACK CWParserDisplayTextHandle(
    CWPluginContext context, const char* name, CWMemHandle text
);
// (no live caller: only Projects.c's dead functions call them)
extern CW_CALLBACK CWParserAddOverlay1Group(
    CWPluginContext context, const char* name, const CWAddr64* addr, SInt32* newGroupNumber
);
extern CW_CALLBACK CWParserAddOverlay1(
    CWPluginContext context, const char* name, SInt32 groupNumber, SInt32* newOverlayNumber
);
extern CW_CALLBACK CWParserAddSegment(
    CWPluginContext context, const char* name, short attrs, SInt32* newSegmentNumber
);
extern CW_CALLBACK CWParserSetSegment(
    CWPluginContext context, SInt32 segmentNumber, const char* name, short attrs
);

#ifdef __cplusplus
}
#endif

#endif
