#ifndef DRIVER_CLIENTGLUE_H
#define DRIVER_CLIENTGLUE_H

#include "compiler/common.h"
#include "driver/CWPlugins.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

// The clients' interface to the driver (__stdcall, as Wii's publics are decorated)
extern int __stdcall RegisterResource(const char* name, SInt16 rsrcid, const char** list);
extern int __stdcall RegisterStaticPlugin(const BasePluginCallbacks* callbacks);
extern int __stdcall RegisterStaticCompilerLinkerPlugin(
    const BasePluginCallbacks* callbacks, const CompilerLinkerPluginCallbacks* cl_callbacks
);
extern int __stdcall RegisterStaticParserPlugin(
    const BasePluginCallbacks* callbacks, const ParserPluginCallbacks* pr_callbacks
);
extern void __stdcall SetBuildTarget(OSType cpu, OSType os);
extern void __stdcall SetParserType(OSType plang);
extern void __stdcall SetPluginType(OSType lang, OSType type);
extern int __stdcall CmdLine_Initialize(
    int argc, char** argv, const char* builddate, const char* buildtime
);
extern int __stdcall CmdLine_Driver(void);
extern int __stdcall CmdLine_Terminate(int exitcode);

#ifdef __cplusplus
}
#endif

#endif
