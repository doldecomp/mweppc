#include "driver/CLMain.h"
#include "driver/CLErrors.h"
#include "driver/CLFileOps.h"
#include "driver/ClientGlue.h"
#include "driver/CLIO.h"
#include "driver/CLLicenses.h"
#include "driver/CLParserCallbacks_v1.h"
#include "driver/CLPluginRequests.h"
#include "driver/CLPlugins.h"
#include "driver/CLPrefs.h"
#include "driver/CLProj.h"
#include "driver/CLTarg.h"
#include "driver/CLToolExec.h"
#include "driver/cmdline.h"
#include "driver/CWPlugins.h"
#include "driver/Generic.h"
#include "driver/LowMem.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include "driver/pref_structs.h"
#include "driver/Resources.h"
#include "driver/StringExtras.h"
#include <setjmp.h>
#include <string.h>

jmp_buf exit_program;
int numPlugins;
CLPluginInfo* pluginInfo;
int numPanels;
const char** panelNames;
CWCommandLineArgs* panel_args;
CWCommandLineArgs* plugin_args;
Project mainProj;
Target* gTarg;
PCmdLine optsCmdLine;
PCmdLineEnvir optsEnvir;
PCmdLineCompiler optsCompiler;
PCmdLineLinker optsLinker;
CLState clState;
char cmdline_build_date[32];
char cmdline_build_time[32];
Project* gProj = &mainProj;

static CWPLUGIN_ENTRY(CLT_dummymain)(CWPluginContext context) {
    return cwNoErr;
}

static CWPLUGIN_ENTRY(CLT_GetDropInFlags)(const DropInFlags** flags, SInt32* flagsSize) {
    // clang-format off
    static const DropInFlags sFlags = {
        kCurrentDropInFlagsVersion,
        CWDROPINDRIVERTYPE,
        7,
        0,
        0,
        12
    };
    // clang-format on
    *flags = &sFlags;
    *flagsSize = sizeof(sFlags);
    return cwNoErr;
}

static CWPLUGIN_ENTRY(CLT_GetDropInName)(const char** dropinName) {
    static const char* sDropInName = "Command-Line Driver";
    *dropinName = sDropInName;
    return cwNoErr;
}

static CWPLUGIN_ENTRY(CLT_GetDisplayName)(const char** displayName) {
    static const char* sDisplayName = "Command-Line Driver";
    *displayName = sDisplayName;
    return cwNoErr;
}

static CWPLUGIN_ENTRY(CLT_GetPanelList)(const CWPanelList** panelList) {
    static const char* sPanelNames[4];
    // clang-format off
    static CWPanelList sPanelList = {
        kCurrentCWPanelListVersion,
        4,
        sPanelNames
    };
    // clang-format on

    if (clState.plugintype == CWDROPINCOMPILERTYPE) {
        sPanelNames[0] = "CmdLine Panel";
        sPanelNames[1] = "CmdLine Compiler Panel";
        sPanelNames[2] = "CmdLine Linker Panel";
        sPanelList.count = 3;
    } else {
        sPanelNames[0] = "CmdLine Panel";
        sPanelNames[1] = "CmdLine Linker Panel";
        sPanelList.count = 2;
    }
    *panelList = &sPanelList;
    return cwNoErr;
}

// (dead: the linker dropped it, its data stays)
static CWPLUGIN_ENTRY(CLT_GetTargetList)(const CWTargetList** targetList) {
    static SInt32 sCPU = targetCPUAny;
    static SInt32 sOS = targetOSAny;
    // clang-format off
    static CWTargetList sTargetList = {
        kCurrentCWTargetListVersion,
        1,
        &sCPU,
        1,
        &sOS
    };
    // clang-format on
    *targetList = &sTargetList;
    return cwNoErr;
}

static CWPLUGIN_ENTRY(CLT_GetVersionInfo)(const VersionInfo** versioninfo) {
    // clang-format off
    static const VersionInfo vi = {
        3, 0, 0, 0
    };
    // clang-format on
    *versioninfo = &vi;
    return cwNoErr;
}

static CWPLUGIN_ENTRY(CLT_GetFileTypeMappings)(const OSFileTypeMappingList** mappinglist) {
    // clang-format off
    static const OSFileTypeMapping ftmes[2] = {
        {'Brws', "DubL", 4, 0, 0},
        {'MMPr', "looc", 4, 0, 0}
    };
    static const OSFileTypeMappingList ftml = {
        2,
        ftmes
    };
    // clang-format on
    *mappinglist = &ftml;
    return cwNoErr;
}

// clang-format off
static BasePluginCallbacks clcb = {
    CLT_dummymain,
    CLT_GetDropInFlags,
    CLT_GetDisplayName,
    CLT_GetDropInName,
    CLT_GetPanelList,
    0,
    0,
    CLT_GetVersionInfo,
    CLT_GetFileTypeMappings
};
// clang-format on

static int RegisterStaticCmdLinePlugin(void) {
    return RegisterStaticPlugin(&clcb);
}

#include "CLErrors.r"

static int RegisterCmdLineResources(void) {
    return RegisterResource("Command-line strings", 12000, STR12000);
}

static Boolean special_debug(Boolean pre, char* arg) {
    if (pre) {
        SetupDebuggingTraps();
        return TRUE;
    } else {
        return FALSE;
    }
}

static Boolean special_plugin_debug(Boolean pre, char* arg) {
    if (pre) {
        clState.pluginDebug = TRUE;
        return TRUE;
    } else {
        return clState.pluginDebug;
    }
}

static Boolean special_stdout_base(Boolean pre, char* arg) {
    if (pre) {
        return TRUE;
    } else {
        return clState.stdout_base != NULL;
    }
}

// NOTE: invented name (after its option and its neighbours')
static Boolean special_license_file(Boolean pre, char* arg) {
    if (pre) {
        return TRUE;
    } else {
        return licenseFile != NULL;
    }
}

struct SpecialOption {
    const char* optname;
    char** argp;
    Boolean (*func)(Boolean a, char* b);
};
// clang-format off
static struct SpecialOption special_options[4] = {
    {"", NULL, special_debug},
    {"--use-license-file", &licenseFile, special_license_file},
    {"--plugin-debug", NULL, special_plugin_debug},
    {"--stdout", &clState.stdout_base, special_stdout_base}
};
// clang-format on

void Main_PreParse(int* argc, char*** argv) {
    int idx;
    struct SpecialOption* so;

    if (*argc <= 1) {
        return;
    }
    do {
        idx = 0;
        so = special_options;

        do {
            if (!strcmp(so->optname, (*argv)[1])) {
                if (so->argp) {
                    *so->argp = (*argv)[2];
                    so->func(TRUE, *so->argp);
                    (*argv)[1] = (*argv)[0];
                    (*argc)--;
                    (*argv)++;

                    (*argv)[1] = (*argv)[0];
                    (*argc)--;
                    (*argv)++;
                } else {
                    so->func(TRUE, NULL);
                    (*argv)[1] = (*argv)[0];
                    (*argc)--;
                    (*argv)++;
                }
                break;
            }
            idx++;
            so++;
        } while (idx < 4);
    } while (*argc > 1 && idx < 4);
}

void Main_PassSpecialArgs(int* argc, char*** argv) {
    int idx;
    struct SpecialOption* so;

    idx = 0;
    so = special_options;

    do {
        if (so->func(FALSE, NULL)) {
            AppendArgumentList(argc, argv, so->optname);
            if (so->argp) {
                AppendArgumentList(argc, argv, (*so->argp) ? *so->argp : "");
            }
        }
        idx++;
        so++;
    } while (idx < 4);
}

// (Main_Initialize's secret option and the Update*FromVersion functions' warned flags, at file
// scope)
static char secret[8];
static Boolean warnedLinker;
static Boolean warnedCompiler;
static Boolean warnedPCmdLine;
static int MainInitialized;

int Main_Initialize(int argc, char** argv) {
    char exename[260];

    OS_InitProgram(&argc, &argv);
    memset(&clState, 0, sizeof(CLState));
    // (spells "--debug" a character at a time, out of order, so the string is not in the binary)
    special_options[0].optname = secret;
    secret[7] = 0;
    secret[4] = 'b';
    secret[1] = '-';
    secret[5] = 'u';
    secret[2] = 'd';
    secret[0] = '-';
    secret[3] = 'e';
    secret[6] = 'g';

    Main_PreParse(&argc, &argv);
    clState.argc = argc;
    clState.argv = argv;

    strcpyn(exename, argv[0], strlen(argv[0]), 260);
    exename[259] = 0;
    if (strlen(exename) < 4 || ustrcmp(exename + strlen(exename) - 4, ".exe")) {
        strcatn(exename, ".exe", 260);
    }

    IO_Initialize();
    if (!RegisterCmdLineResources()) {
        CLFatalError("Could not initialize resource strings");
    }

    if (!OS_IsFullPath(argv[0])) {
        clState.programName = argv[0];
    } else {
        clState.programName = OS_GetFileNamePtr(argv[0]);
    }

    if (OS_FindProgram(exename, &clState.programSpec)) {
        CLReportError(CLStr2, exename);
    }

    Plugins_Init();
    if (!RegisterStaticCmdLinePlugin()) {
        CLFatalError("Could not initialize built-in plugins");
    }

    Proj_Initialize(gProj);
    SystemInitResources(&clState.programSpec);
    License_Initialize();

    MainInitialized = TRUE;
    return 0;
}

int Main_Terminate(int exitcode) {
    if (MainInitialized) {
        Plugins_Term();
        License_Terminate();
        Proj_Terminate(gProj);
        IO_Terminate();
        MainInitialized = FALSE;
    }
    return exitcode;
}

static int Main_ParseCommandLine(void) {
    Plugin* parser;
    CWCommandLineArgs myargs;
    int x, y;
    SInt32 vislang;

    if (!Plugins_GetPluginList(0, &numPlugins, &pluginInfo) ||
        !Plugins_GetPrefPanelUnion(0, &numPanels, &panelNames) ||
        !(panel_args = xcalloc(NULL, numPanels * sizeof(CWCommandLineArgs))) ||
        !(plugin_args = xcalloc(NULL, numPlugins * sizeof(CWCommandLineArgs)))) {
        CLFatalError("Out of memory during init\n");
    }

    parser = Plugins_GetParserForPlugin(
        0, clState.parserstyle, numPlugins, pluginInfo, gTarg->cpu, gTarg->os, numPanels, panelNames
    );
    if (!parser) {
        CLFatalError("Could not find a command-line parser!");
    }

    myargs.argc = clState.argc;
    myargs.argv = clState.argv;
    myargs.envp = NULL;

    if (!SendParserRequest(
            parser,
            gTarg,
            &myargs,
            gTarg->cpu,
            gTarg->os,
            numPlugins,
            pluginInfo,
            numPanels,
            panelNames,
            plugin_args,
            panel_args,
            cmdline_build_date,
            cmdline_build_time,
            Plugin_GetToolVersionInfo()
        )) {
        return CheckForUserBreak() ? Result_Cancelled : Result_Failed;
    }

    if (clState.pluginDebug) {
        for (y = 0; y < numPanels; y++) {
            CLPrintErr("Outgoing args for '%s':  ", panelNames[y]);
            for (x = 0; x < panel_args[y].argc; x++) {
                CLPrintErr("%s ", panel_args[y].argv[x]);
            }
            CLPrintErr("\n");
        }

        for (x = 0; x < numPlugins; x++) {
            vislang = pluginInfo[x].language ? pluginInfo[x].language : '----';
            CLPrintErr(
                "Outgoing args for '%c%c%c%c/%c%c%c%c':  ",
                (pluginInfo[x].plugintype & 0xFF000000) >> 24,
                (pluginInfo[x].plugintype & 0x00FF0000) >> 16,
                (pluginInfo[x].plugintype & 0x0000FF00) >> 8,
                (pluginInfo[x].plugintype & 0x000000FF),
                (vislang & 0xFF000000) >> 24,
                (vislang & 0x00FF0000) >> 16,
                (vislang & 0x0000FF00) >> 8,
                (vislang & 0x000000FF)
            );
            for (y = 1; y < plugin_args[x].argc; y++) {
                CLPrintErr("%s ", plugin_args[x].argv[y]);
            }
            CLPrintErr("\n");
        }
    }

    return Result_Success;
}

static int Main_SetupParamBlock(void) {
    PrefPanelsChangedCallback(NULL);

    if (optsCompiler.outMakefile[0]) {
        int err = OS_MakeFileSpec(optsCompiler.outMakefile, &clState.makefileSpec);
        if (err) {
            CLReportOSError(CLStr8, err, optsCompiler.outMakefile);
            return Result_Failed;
        }
        OS_Delete(&clState.makefileSpec);
    }

    if (optsCompiler.sbmPath[0]) {
        int err = OS_MakePathSpec(NULL, optsCompiler.sbmPath, &clState.sbmPathSpec);
        if (err) {
            CLReportOSError(CLStr23, err, optsCompiler.sbmPath);
            return Result_Failed;
        }
    } else {
        OS_GetCWD(&clState.sbmPathSpec);
    }

    return Result_Success;
}

static int Main_ResolveProject(void) {
    SInt32 startTime;
    SInt32 endTime;
    int err;

    if (optsCmdLine.state == OptsCmdLineState_Unset ||
        optsCmdLine.state == OptsCmdLineState_NoAction) {
        return Result_Success;
    }

    startTime = LMGetTicks();
    err = CompileFilesInProject();
    if (err) {
        return err;
    }

    if (optsCmdLine.state == OptsCmdLineState_CompileAndLink) {
        err = LinkProject();
        if (err) {
            return err;
        }
    }

    if (!DeleteTemporaries()) {
        return Result_Failed;
    }

    endTime = LMGetTicks();

    if (optsCmdLine.timeWorking) {
        CLReport(CLStr24, (endTime - startTime) / 60.0, "resolve", "", "project", "");
    }

    return Result_Success;
}

static int UpdatePCmdLineFromVersion(PCmdLine* given, PCmdLine* target) {
    SInt16 version = given->version;
    *target = *given;

    if (clState.pluginDebug && version < 0x1002 && !warnedPCmdLine) {
        CLReportWarning(CLStr104, "CmdLine Panel");
        warnedPCmdLine = TRUE;
    }

    if (version == 0x1000) {
        target->stderr2stdout = TRUE;
        version++;
    }

    if (version == 0x1001) {
        target->noCmdLineWarnings = FALSE;
        version++;
    }

    if (version < 0x1000 || version > 0x1002) {
        CLReportError(CLStr104, "CmdLine Panel");
        return FALSE;
    } else {
        target->version = 0x1002;
        return TRUE;
    }
}

static int UpdatePCmdLineEnvirFromVersion(PCmdLineEnvir* given, PCmdLineEnvir* target) {
    SInt16 version = given->version;
    *target = *given;

    if ((clState.pluginDebug && version < 0x1000) || version > 0x1000) {
        CLReportError(CLStr104, "CmdLine Environment");
        return FALSE;
    } else {
        target->version = 0x1000;
        return TRUE;
    }
}

static int UpdatePCmdLineCompilerFromVersion(PCmdLineCompiler* given, PCmdLineCompiler* target) {
    SInt16 version = given->version;
    *target = *given;

    if (clState.pluginDebug && version < 0x1005 && !warnedCompiler) {
        CLReportWarning(CLStr104, "CmdLine Compiler Panel");
        warnedCompiler = TRUE;
    }

    if (version == 0x1001) {
        target->ignoreMissingFiles = FALSE;
        target->printHeaderNames = FALSE;
        version++;
    }

    if (version == 0x1002) {
        target->sbmState = 0;
        target->sbmPath[0] = 0;
        version++;
    }

    if (version == 0x1003) {
        target->canonicalIncludes = FALSE;
        version++;
    }

    if (version == 0x1004) {
        target->keepObjects = FALSE;
        version++;
    }

    if (version <= 0x1000 || version > 0x1005) {
        CLReportError(CLStr104, "CmdLine Compiler Panel");
        return FALSE;
    } else {
        target->version = 0x1005;
        return TRUE;
    }
}

static int UpdatePCmdLineLinkerFromVersion(PCmdLineLinker* given, PCmdLineLinker* target) {
    SInt16 version = given->version;
    *target = *given;

    if (clState.pluginDebug && version < 0x1002 && !warnedLinker) {
        CLReportWarning(CLStr104, "CmdLine Linker Panel");
        warnedLinker = TRUE;
    }

    if (version == 0x1000) {
        target->callPreLinker = TRUE;
        target->callPostLinker = TRUE;
        target->keepLinkerOutput = TRUE;
        version++;
    }

    if (version == 0x1001) {
        target->callLinker = TRUE;
        version++;
    }

    if (version < 0x1000 || version > 0x1002) {
        CLReportError(CLStr104, "CmdLine Linker Panel");
        return FALSE;
    } else {
        target->version = 0x1002;
        return TRUE;
    }
}

static int UpdatePrefPanels(const char* name) {
    PrefPanel* panel;
    Handle h;

    if (!name || !ustrcmp(name, "CmdLine Panel")) {
        if ((panel = Prefs_FindPanel("CmdLine Panel")) && (h = PrefPanel_GetHandle(panel))) {
            if (name) {
                if (!UpdatePCmdLineFromVersion((PCmdLine*)*h, &optsCmdLine)) {
                    return FALSE;
                }
            }
        } else {
            CLReportError(CLStr91, "CmdLine Panel");
            return FALSE;
        }
    }

    if (!name || !ustrcmp(name, "CmdLine Environment")) {
        if ((panel = Prefs_FindPanel("CmdLine Environment")) && (h = PrefPanel_GetHandle(panel))) {
            if (name) {
                if (!UpdatePCmdLineEnvirFromVersion((PCmdLineEnvir*)*h, &optsEnvir)) {
                    return FALSE;
                }
            }
        } else {
            CLReportError(CLStr91, "CmdLine Environment");
            return FALSE;
        }
    }

    if (!name || !ustrcmp(name, "CmdLine Compiler Panel")) {
        if ((panel = Prefs_FindPanel("CmdLine Compiler Panel")) &&
            (h = PrefPanel_GetHandle(panel))) {
            if (name) {
                if (!UpdatePCmdLineCompilerFromVersion((PCmdLineCompiler*)*h, &optsCompiler)) {
                    return FALSE;
                }
            }
        } else {
            CLReportError(CLStr91, "CmdLine Compiler Panel");
            return FALSE;
        }
    }

    if (!name || !ustrcmp(name, "CmdLine Linker Panel")) {
        if ((panel = Prefs_FindPanel("CmdLine Linker Panel")) && (h = PrefPanel_GetHandle(panel))) {
            if (name) {
                if (!UpdatePCmdLineLinkerFromVersion((PCmdLineLinker*)*h, &optsLinker)) {
                    return FALSE;
                }
            }
        } else {
            CLReportError(CLStr91, "CmdLine Linker Panel");
            return FALSE;
        }
    }

    return TRUE;
}

static int SetupCmdLinePrefPanels(void) {
    int ret;

    PrefPanelsChangedCallback = UpdatePrefPanels;
    ret = Prefs_AddPanel(PrefPanel_New("CmdLine Environment", &optsEnvir, sizeof(PCmdLineEnvir)));
    ret |=
        (Prefs_AddPanel(PrefPanel_New("CmdLine Panel", NULL, sizeof(PCmdLine))) &&
         Prefs_AddPanel(PrefPanel_New("CmdLine Compiler Panel", NULL, sizeof(PCmdLineCompiler))) &&
         Prefs_AddPanel(PrefPanel_New("CmdLine Linker Panel", NULL, sizeof(PCmdLineLinker))));

    return ret;
}

static int Main_SetupContext(void) {
    gTarg = Target_New("default", clState.cpu, clState.os, clState.language);
    Target_Add(&gProj->targets, gTarg);

    Plugins_AddFileTypeMappingsForTarget(0, 0, gTarg->cpu, gTarg->os);

    gTarg->linker = Plugins_GetLinker(0, gTarg->cpu, gTarg->os);
    if (gTarg->linker) {
        gTarg->linkerDropinFlags = Plugin_GetDropInFlags(gTarg->linker)->dropinflags;
    } else {
        if (Plugins_GetLinker(0, '****', '****')) {
            CLFatalError("A linker is compiled in, but does not match the static target!\n");
        }
        gTarg->linkerDropinFlags = 0;
    }

    gTarg->preLinker = Plugins_GetPreLinker(0, gTarg->cpu, gTarg->os);
    if (gTarg->preLinker) {
        gTarg->preLinkerDropinFlags = Plugin_GetDropInFlags(gTarg->preLinker)->dropinflags;
    } else {
        if (Plugins_GetPreLinker(0, '****', '****')) {
            CLFatalError("A pre-linker is compiled in, but does not match the static target!\n");
        }
        gTarg->preLinkerDropinFlags = 0;
    }

    gTarg->postLinker = Plugins_GetPostLinker(0, gTarg->cpu, gTarg->os);
    if (gTarg->postLinker) {
        gTarg->postLinkerDropinFlags = Plugin_GetDropInFlags(gTarg->postLinker)->dropinflags;
    } else {
        if (Plugins_GetPostLinker(0, '****', '****')) {
            CLFatalError("A post-linker is compiled in, but does not match the static target!\n");
        }
        gTarg->postLinkerDropinFlags = 0;
    }

    if (clState.plugintype == CWDROPINLINKERTYPE) {
        if (!gTarg->preLinker && !gTarg->linker && !gTarg->postLinker) {
            CLFatalError("The linker plugin was not found!");
        }
    }

    gTarg->linkmodel = LinkModel_Flat;
    return TRUE;
}

int Main_Driver(void) {
    volatile int result;

    result = setjmp(exit_program);
    if (!result) {
        if (!SetupCmdLinePrefPanels()) {
            CLFatalError("Could not initialize preferences");
        }

        Main_SetupContext();
        if (!(result = Main_ParseCommandLine())) {
            if (!(result = Main_SetupParamBlock())) {
                if (!(result = Main_ResolveProject())) {
                    // TODO: the original keeps this test for a body that generates nothing
                    // (probably a debug macro compiled to nothing); the plain assignment, an empty
                    // block or `;` drop it
                    (void)0;
                }
            }
        }
    } else {
        result = Result_Failed;
    }

    return result;
}
