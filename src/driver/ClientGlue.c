#include "driver/ClientGlue.h"
#include "driver/CLErrors.h"
#include "driver/CLMain.h"
#include "driver/CLPlugins.h"
#include "driver/CWPlugins.h"
#include "driver/oslib.h"
#include "driver/Resources.h"
#include "driver/ResourceStrings.h"
#include <string.h>

int __stdcall RegisterResource(const char* name, SInt16 rsrcid, const char** list) {
    Handle h;

    if (list == NULL) {
        h = GetResource('STR#', rsrcid);
        if (h == NULL) {
            CLFatalError("Resource ('STR#',%d) '%s' not found in executable\n", rsrcid, name);
            return FALSE;
        }
        ReleaseResource(h);
        return TRUE;
    } else {
        return Res_AddResource(name, rsrcid, list);
    }
}

int __stdcall RegisterStaticPlugin(const BasePluginCallbacks* callbacks) {
    return Plugins_Add(Plugin_New(callbacks, NULL, NULL));
}

int __stdcall RegisterStaticCompilerLinkerPlugin(
    const BasePluginCallbacks* callbacks, const CompilerLinkerPluginCallbacks* cl_callbacks
) {
    return Plugins_Add(Plugin_New(callbacks, cl_callbacks, NULL));
}

int __stdcall RegisterStaticParserPlugin(
    const BasePluginCallbacks* callbacks, const ParserPluginCallbacks* pr_callbacks
) {
    return Plugins_Add(Plugin_New(callbacks, NULL, pr_callbacks));
}

void __stdcall SetBuildTarget(OSType cpu, OSType os) {
    clState.cpu = cpu;
    clState.os = os;
}

void __stdcall SetParserType(OSType plang) {
    clState.parserstyle = plang;
}

void __stdcall SetPluginType(OSType lang, OSType type) {
    clState.language = lang;
    clState.plugintype = type;
}

int __stdcall CmdLine_Initialize(
    int argc, char** argv, const char* builddate, const char* buildtime
) {
    strncpy(cmdline_build_date, builddate, sizeof(cmdline_build_date));
    strncpy(cmdline_build_time, buildtime, sizeof(cmdline_build_time));
    return Main_Initialize(argc, argv);
}

int __stdcall CmdLine_Driver(void) {
    return Main_Driver();
}

int __stdcall CmdLine_Terminate(int exitcode) {
    return Main_Terminate(exitcode);
}
