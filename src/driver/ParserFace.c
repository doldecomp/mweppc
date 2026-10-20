#include "driver/ParserFace.h"
#include "driver/Arguments.h"
#include "driver/cmdline.h"
#include "driver/CWParserPluginsPrivate.h"
#include "driver/CWPlugins.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/CWSecretPluginCallbacks.h"
#include "driver/Memory.h"
#include "driver/Option.h"
#include "driver/ParserErrors.h"
#include "driver/pref_structs.h"
#include "driver/StringExtras.h"
#include "driver/Targets.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

const char* failedCallback;
jmp_buf exit_plugin;
ParseOptsType parseopts;

static CWResult SetupParamBlock(CWPluginContext context) {
    CWResult result;
    int x;
    Handle h;
    PCmdLineEnvir cle;

    memset(&parseopts, 0, sizeof(parseopts));
    parseopts.context = context;

    result = CWSecretGetNamedPreferences(context, "CmdLine Environment", &h);
    if (result) {
        return result;
    }

    cle = **((PCmdLineEnvir**)h);
    parseopts.underIDE = cle.underIDE;
    parseopts.ioRows = cle.rows;
    parseopts.ioCols = cle.cols;

    result = CWParserGetCommandLine(context, &parseopts.args);
    if (result) {
        return result;
    }

    result = CWParserGetToolInfo(context, &parseopts.toolVersion);
    if (result) {
        return result;
    }

    result = CWParserGetTargetInfo(context, &parseopts.cpu, &parseopts.os);
    if (result) {
        return result;
    }

    result = CWParserGetPanels(context, &parseopts.numPanels, &parseopts.panelNames);
    if (result) {
        return result;
    }

    result = CWParserGetPlugins(context, &parseopts.numPlugins, &parseopts.plugins);
    if (result) {
        return result;
    }

    parseopts.passingArgs = FALSE;
    for (x = 0; x < parseopts.numPlugins; x++) {
        if (parseopts.plugins[x].storeCommandLine) {
            parseopts.passingArgs = TRUE;
        }
    }

    return 0;
}

static CWResult SetupOptions(CWPluginContext context) {
    if (!pTool) {
        return 2;
    } else {
        SetupParserToolOptions();
        return 0;
    }
}

static CWResult Parse(CWPluginContext context) {
    CWResult result;

    parseopts.success = TRUE;
    parseopts.currentSegment = 1;
    parseopts.currentOverlayGroup = 0;
    parseopts.currentOverlay = 0;

    Arg_InitToolArgs(&linkargs);
    Arg_InitToolArgs(&prelinkargs);
    Arg_InitToolArgs(&postlinkargs);

    if (pTool->PreParse) {
        parseopts.success &= pTool->PreParse();
    }

    Arg_Init(parseopts.args->argc, parseopts.args->argv);
    parseopts.noOptions = Arg_IsEmpty();
    parseopts.success &= Options_Parse(Options_GetOptions(), 1) && !parseopts.hadErrors;
    Arg_Reset();

    result = Parser_StorePanels(context);
    if (result) {
        return result;
    }

    if (pTool->MidParse && parseopts.success) {
        parseopts.success &= pTool->MidParse();
    }

    result = Parser_StorePanels(context);
    if (result) {
        return result;
    }

    if (parseopts.showHelp && parseopts.success) {
        parseopts.success &= Options_DisplayHelp();
    }

    Arg_Reset();
    if (parseopts.success) {
        parseopts.success &= Options_Parse(Options_GetOptions(), 0) && !parseopts.hadErrors;
    }

    if (pTool->PostParse && parseopts.success) {
        parseopts.success &= pTool->PostParse();
    }

    Arg_Terminate();

    return (parseopts.success && !parseopts.hadErrors) ? cwNoErr : cwErrRequestFailed;
}

Handle Parser_FindPrefPanel(const char* name) {
    int idx;
    Handle h;

    for (idx = 0; idx < pTool->numPrefDataPanels; idx++) {
        if (!ustrcmp(name, pTool->prefDataPanels[idx].name)) {
            h = NewHandle(pTool->prefDataPanels[idx].size);
            if (!h) {
                return NULL;
            }

            HLock(h);
            memcpy(*h, pTool->prefDataPanels[idx].ptr, pTool->prefDataPanels[idx].size);
            HUnlock(h);
            return h;
        }
    }

    return NULL;
}

CWResult Parser_StorePanels(CWPluginContext context) {
    int idx;
    CWResult result;
    const char* name;
    Handle h;

    for (idx = 0; idx < parseopts.numPanels; idx++) {
        name = parseopts.panelNames[idx];
        h = Parser_FindPrefPanel(name);
        if (h) {
            result = CWParserSetNamedPreferences(parseopts.context, name, h);
            if (result) {
                CLPReportError(CLPStr68, name);
                return result;
            }
        }
    }

    return cwNoErr;
}

static CWResult StoreResults(CWPluginContext context) {
    int idx;
    CWResult result;
    CWCommandLineArgs args;
    UInt32 id;
    UInt32 lg;

    result = Parser_StorePanels(context);
    if (result) {
        return result;
    }

    for (idx = 0; idx < parseopts.numPlugins; idx++) {
        if (parseopts.plugins[idx].plugintype == CWDROPINLINKERTYPE &&
            parseopts.plugins[idx].storeCommandLine) {
            if (parseopts.plugins[idx].dropinflags & isPreLinker) {
                Arg_ToolArgsForPlugin(&prelinkargs, &args);
            } else if (parseopts.plugins[idx].dropinflags & isPostLinker) {
                Arg_ToolArgsForPlugin(&postlinkargs, &args);
            } else {
                Arg_ToolArgsForPlugin(&linkargs, &args);
            }
            result = CWParserStoreCommandLineForPlugin(parseopts.context, idx, &args);
            if (result) {
                return result;
            }
        } else if (parseopts.plugins[idx].storeCommandLine) {
            id = parseopts.plugins[idx].plugintype;
            lg = parseopts.plugins[idx].language;
            fprintf(
                stderr,
                "*** No support for %c%c%c%c/%c%c%c%c tool\n",
                (int)((id & 0xFF000000) >> 24),
                (int)((id & 0x00FF0000) >> 16),
                (int)((id & 0x0000FF00) >> 8),
                (int)(id & 0x000000FF),
                (int)((lg & 0xFF000000) >> 24),
                (int)((lg & 0x00FF0000) >> 16),
                (int)((lg & 0x0000FF00) >> 8),
                (int)(lg & 0x000000FF)
            );
        }
    }

    return cwNoErr;
}

CWPLUGIN_ENTRY(CWParser_GetDropInFlags)(const DropInFlags** flags, SInt32* flagsSize) {
    static const DropInFlags sFlags = {
        kCurrentDropInFlagsVersion, CWDROPINPARSERTYPE, 7, 0, 'Seep', 12
    };
    *flags = &sFlags;
    *flagsSize = sizeof(sFlags);
    return 0;
}

CWPLUGIN_ENTRY(CWParser_GetDropInName)(const char** dropinName) {
    static const char* sDropInName = "Command-Line Parser";
    *dropinName = sDropInName;
    return 0;
}

CWPLUGIN_ENTRY(CWParser_GetDisplayName)(const char** displayName) {
    static const char* sDisplayName = "Command-Line Parser";
    *displayName = sDisplayName;
    return 0;
}

CWPLUGIN_ENTRY(CWParser_GetPanelList)(const CWPanelList** panelList) {
    static CWPanelList sPanelList = {kCurrentCWFamilyListVersion, 0, 0};
    *panelList = &sPanelList;
    return 0;
}

CWPLUGIN_ENTRY(CWParser_GetTargetList)(const CWTargetList** targetList) {
    static SInt32 sCPU = '****';
    static SInt32 sOS = '****';
    static CWTargetList sTargetList = {kCurrentCWTargetListVersion, 1, &sCPU, 1, &sOS};
    *targetList = &sTargetList;
    return 0;
}

CWPLUGIN_ENTRY(CWParser_GetVersionInfo)(const VersionInfo** versioninfo) {
    static const VersionInfo vi = {1, 1, 0, 0};
    *versioninfo = &vi;
    return 0;
}

CWPLUGIN_ENTRY(Parser_SupportsPlugin)(
    const CLPluginInfo* pluginfo, CWDataType cpu, CWDataType os, Boolean* isSupported
) {
    *isSupported = ParserToolMatchesPlugin(pluginfo->plugintype, pluginfo->language, cpu, os);
    return 0;
}

CWPLUGIN_ENTRY(Parser_SupportsPanels)(
    int numPanels, const char** panelNames, Boolean* isSupported
) {
    *isSupported = ParserToolHandlesPanels(numPanels, panelNames);
    return 0;
}

CWPLUGIN_ENTRY(parser_main)(CWPluginContext context) {
    CWResult result = cwNoErr;
    SInt32 request;

    CWGetPluginRequest(context, &request);
    result = setjmp(exit_plugin);
    if (result == 0) {
        switch (request) {
            case reqInitialize:
                result = 0;
                break;
            case reqTerminate:
                break;
            case 1:
                result = SetupParamBlock(context);
                if (!result) {
                    result = Parser_StorePanels(context);
                }
                break;
            case 2:
                result = SetupParamBlock(context);
                if (!result) {
                    result = SetupOptions(context);
                    if (!result) {
                        result = Parse(context);
                        if (!result) {
                            result = StoreResults(context);
                        }
                    }
                }
                break;
        }
    } else {
        if (failedCallback && result != 1) {
            fprintf(stderr, "Unexpected error in %s [%d]\n", failedCallback, (int)result);
        }
    }
    CWDonePluginRequest(context, result);

    return result;
}
