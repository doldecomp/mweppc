#ifndef DRIVER_CLMAIN_H
#define DRIVER_CLMAIN_H

#include "compiler/common.h"
#include "driver/CLProj.h"
#include "driver/CLTarg.h"
#include "driver/cmdline.h"
#include "driver/CWPlugins.h"
#include "driver/Memory.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include "driver/pref_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

// The driver's state
typedef struct CLState { // NOTE: invented name (Wii names only the variable, clState)
    int argc;
    char** argv;
    OSType cpu;
    OSType os;
    OSType plugintype;
    OSType language;
    OSType parserstyle;
    OSSpec programSpec;
    const char* programName;
    SInt16 countWarnings;
    SInt16 countErrors;
    Boolean pluginDebug;
    Boolean userBreak;
    Boolean withholdWarnings;
    Boolean withholdErrors;
    OSSpec makefileSpec;
    OSPathSpec sbmPathSpec;
    OSHandle browseTableHandle;
    char* stdout_base;
    Boolean stdout_written;
} CLState;

extern int numPlugins;
extern struct CLPluginInfo* pluginInfo;
extern int numPanels;
extern const char** panelNames;
extern CWCommandLineArgs* panel_args;
extern CWCommandLineArgs* plugin_args;
extern Target* gTarg;
extern PCmdLine optsCmdLine;
extern PCmdLineEnvir optsEnvir;
extern PCmdLineCompiler optsCompiler;
extern PCmdLineLinker optsLinker;
extern CLState clState;
extern char cmdline_build_date[32];
extern char cmdline_build_time[32];

enum {
    Result_Success = 0,
    Result_Failed = 1,
    Result_Cancelled = 2
};

extern void Main_PassSpecialArgs(int* argc, char*** argv);
extern int Main_Initialize(int argc, char** argv);
extern int Main_Terminate(int exitcode);
extern int Main_Driver(void);

#ifdef __cplusplus
}
#endif

#endif
