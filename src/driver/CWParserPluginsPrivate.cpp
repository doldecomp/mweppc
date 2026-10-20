// The CW plugin library's parser-plugin glue (Wii's module CWParserPluginsPrivate, with
// ??4CWCommandLineArgs, which only a C++ unit emits). Every function calls GetContext out of line:
// -inline off.
#include "driver/CWParserPluginsPrivate.h"
#include "driver/cmdline.h"
#include "driver/CWPlugins.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/Files.h"

extern "C" {

static CWParserContext* GetContext(CWPluginContext context) {
    if (context && (context->pluginType == CWDROPINPARSERTYPE)) {
        return static_cast<CWParserContext*>(context);
    } else {
        return NULL;
    }
}

CW_CALLBACK CWParserGetBuildDate(CWPluginContext context, const char** bdate, const char** btime) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (!bdate) {
        return cwErrInvalidParameter;
    }
    if (!btime) {
        return cwErrInvalidParameter;
    }
    *bdate = pc->build_date;
    *btime = pc->build_time;
    return cwNoErr;
}

CW_CALLBACK CWParserGetCommandLine(CWPluginContext context, CWCommandLineArgs** args) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (!args) {
        return cwErrInvalidParameter;
    }
    *args = pc->args;
    return cwNoErr;
}

CW_CALLBACK CWParserGetTargetInfo(CWPluginContext context, CWDataType* cpu, CWDataType* os) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (!cpu) {
        return cwErrInvalidParameter;
    }
    if (!os) {
        return cwErrInvalidParameter;
    }
    *cpu = pc->cpu;
    *os = pc->os;
    return cwNoErr;
}

CW_CALLBACK CWParserGetToolInfo(CWPluginContext context, const ToolVersionInfo** toolVersionInfo) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (!toolVersionInfo) {
        return cwErrInvalidParameter;
    }
    *toolVersionInfo = pc->build_tool;
    return cwNoErr;
}

CW_CALLBACK CWParserGetPlugins(
    CWPluginContext context, int* numPlugins, const CLPluginInfo** pluginInfo
) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (!numPlugins) {
        return cwErrInvalidParameter;
    }
    if (!pluginInfo) {
        return cwErrInvalidParameter;
    }
    *numPlugins = pc->numPlugins;
    *pluginInfo = pc->plugins;
    return cwNoErr;
}

CW_CALLBACK CWParserGetPanels(CWPluginContext context, int* numPanels, const char*** panelNames) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (!numPanels) {
        return cwErrInvalidParameter;
    }
    if (!panelNames) {
        return cwErrInvalidParameter;
    }
    *numPanels = pc->numPanels;
    *panelNames = pc->panelNames;
    return cwNoErr;
}

CW_CALLBACK CWParserStoreCommandLineForPanel(
    CWPluginContext context, int index, const CWCommandLineArgs* args
) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (index < 0 || index >= pc->numPanels) {
        return cwErrInvalidParameter;
    }
    if (!args) {
        return cwErrInvalidParameter;
    }
    pc->panel_args[index] = *args;
    return cwNoErr;
}

CW_CALLBACK CWParserStoreCommandLineForPlugin(
    CWPluginContext context, int index, const CWCommandLineArgs* args
) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (index < 0 || index >= pc->numPlugins) {
        return cwErrInvalidParameter;
    }
    if (!args) {
        return cwErrInvalidParameter;
    }
    pc->plugin_args[index] = *args;
    return cwNoErr;
}

CW_CALLBACK CWParserSetNamedPreferences(
    CWPluginContext context, const char* panelName, Handle paneldata
) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (!panelName) {
        return cwErrInvalidParameter;
    }
    return pc->callbacks->cbParserSetNamedPreferences(pc, panelName, paneldata);
}

CW_CALLBACK CWParserAddAccessPath(CWPluginContext context, const CWNewAccessPathInfo* api) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (!api) {
        return cwErrInvalidParameter;
    }
    return pc->callbacks->cbParserAddAccessPath(pc, api);
}

CW_CALLBACK CWParserSwapAccessPaths(CWPluginContext context) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    return pc->callbacks->cbParserSwapAccessPaths(pc);
}

CW_CALLBACK CWParserSetOutputFileDirectory(CWPluginContext context, const CWFileSpec* idefss) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (!idefss) {
        return cwErrInvalidParameter;
    }
    return pc->callbacks->cbParserSetOutputFileDirectory(pc, idefss);
}

CW_CALLBACK CWParserSetFileOutputName(
    CWPluginContext context, SInt32 position, short which, const char* outfilename
) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (!outfilename) {
        return cwErrInvalidParameter;
    }
    return pc->callbacks->cbParserSetFileOutputName(pc, position, which, outfilename);
}

CW_CALLBACK CWParserAddOverlay1Group(
    CWPluginContext context, const char* name, const CWAddr64* addr, SInt32* newGroupNumber
) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (!name) {
        return cwErrInvalidParameter;
    }
    if (!addr) {
        return cwErrInvalidParameter;
    }
    if (!newGroupNumber) {
        return cwErrInvalidParameter;
    }
    return pc->callbacks->cbParserAddOverlay1Group(pc, name, addr, newGroupNumber);
}

CW_CALLBACK CWParserAddOverlay1(
    CWPluginContext context, const char* name, SInt32 groupNumber, SInt32* newOverlayNumber
) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (!name) {
        return cwErrInvalidParameter;
    }
    if (!newOverlayNumber) {
        return cwErrInvalidParameter;
    }
    return pc->callbacks->cbParserAddOverlay1(pc, name, groupNumber, newOverlayNumber);
}

CW_CALLBACK CWParserAddSegment(
    CWPluginContext context, const char* name, short attrs, SInt32* newSegmentNumber
) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (!name) {
        return cwErrInvalidParameter;
    }
    if (!newSegmentNumber) {
        return cwErrInvalidParameter;
    }
    return pc->callbacks->cbParserAddSegment(pc, name, attrs, newSegmentNumber);
}

CW_CALLBACK CWParserSetSegment(
    CWPluginContext context, SInt32 segmentNumber, const char* name, short attrs
) {
    CWParserContext* pc;
    if (!(pc = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (!name) {
        return cwErrInvalidParameter;
    }
    return pc->callbacks->cbParserSetSegment(pc, segmentNumber, name, attrs);
}

CW_CALLBACK CWParserCreateVirtualFile(CWPluginContext context, const char* name, CWMemHandle text) {
    CWNewTextDocumentInfo info;
    info.documentname = name;
    info.text = text;
    info.markDirty = TRUE;
    return CWCreateNewTextDocument(context, &info);
}

CW_CALLBACK CWParserDisplayTextHandle(CWPluginContext context, const char* name, CWMemHandle text) {
    CWNewTextDocumentInfo info;
    info.documentname = name;
    info.text = text;
    info.markDirty = FALSE;
    return CWCreateNewTextDocument(context, &info);
}
}
