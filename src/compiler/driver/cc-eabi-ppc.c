#include "driver/cc-eabi-ppc.h"
#include "driver/CCompiler.h"
#include "driver/ClientGlue.h"
#include "driver/CWPlugins.h"
#include "driver/DropInCompilerLinker.h"

CWPLUGIN_ENTRY(CWPlugin_GetDropInFlags)(const DropInFlags** flags, SInt32* flagsSize) {
    static const DropInFlags sFlags = {
        kCurrentDropInFlagsVersion,
        CWDROPINCOMPILERTYPE,
        DROPINCOMPILERLINKERAPIVERSION_8,
        kGeneratescode | kCanpreprocess | kCanprecompile,
        Lang_C_CPP,
        DROPINCOMPILERLINKERAPIVERSION_12
    };

    *flags = &sFlags;
    *flagsSize = sizeof(sFlags);
    return 0;
}

CWPLUGIN_ENTRY(CWPlugin_GetTargetList)(const CWTargetList** targetList) {
    static SInt32 sCPU[1] = {'ePPC'};
    static SInt32 sOS[1] = {'EABI'};
    static CWTargetList sTargetList = {kCurrentCWTargetListVersion, 1, sCPU, 1, sOS};

    *targetList = &sTargetList;
    return 0;
}

CWPLUGIN_ENTRY(CWPlugin_GetDropInName)(const char** dropinName) {
    static const char* sDropInName = "MW C/C++ PPC EABI";
    *dropinName = sDropInName;
    return 0;
}

CWPLUGIN_ENTRY(CWPlugin_GetDisplayName)(const char** displayName) {
    static const char* sDisplayName = "Metrowerks C/C++";
    *displayName = sDisplayName;
    return 0;
}

CWPLUGIN_ENTRY(CWPlugin_GetDefaultMappingList)(const CWExtMapList** defaultMappingList) {
    // clang-format off
    static CWExtensionMapping sExtension[10] = {
        {'TEXT', ".c", 0},
        {'TEXT', ".c++", 0},
        {'TEXT', ".cc", 0},
        {'TEXT', ".cp", 0},
        {'TEXT', ".cpp", 0},
        {'TEXT', ".h", kIgnored},
        {'TEXT', ".h++", kIgnored},
        {'TEXT', ".hpp", kIgnored},
        {'TEXT', ".pch", kPrecompile},
        {'TEXT', ".pch++", kPrecompile}
    };
    // clang-format on
    static CWExtMapList sExtensionMapList = {kCurrentCWExtMapListVersion, 10, sExtension};

    *defaultMappingList = &sExtensionMapList;
    return 0;
}

CWPLUGIN_ENTRY(CWPlugin_GetPanelList)(const CWPanelList** panelList) {
    // clang-format off
    static const char* sPanelNames[4] = {"C/C++ Compiler", "C/C++ Warnings", "PPC EABI CodeGen",
                                         "EPPC Global Optimizer"};
    // clang-format on
    static CWPanelList sPanelList = {kCurrentCWPanelListVersion, 4, sPanelNames};

    *panelList = &sPanelList;
    return 0;
}

static CWPLUGIN_ENTRY(CWPlugin_GetVersionInfo)(const VersionInfo** versionInfo) {
    static const VersionInfo vi = {2, 4, 1, 32};

    *versionInfo = &vi;
    return 0;
}

static CWPLUGIN_ENTRY(CWPlugin_GetFileTypeMappings)(const OSFileTypeMappingList** mappings) {
    // clang-format off
    static const OSFileTypeMapping ftmes[2] = {
        {'MMCH', "\xBE\xEF\xFA\xCE", 4, 0, NULL},
        {'MMCH', "\xCE\xFA\xEF\xBE", 4, 0, NULL}
    };
    // clang-format on
    static const OSFileTypeMappingList ftml = {2, ftmes};

    *mappings = &ftml;
    return 0;
}

// clang-format off
static BasePluginCallbacks plugin_cb = {
    MWC_main,
    CWPlugin_GetDropInFlags,
    CWPlugin_GetDisplayName,
    CWPlugin_GetDropInName,
    CWPlugin_GetPanelList,
    NULL,
    NULL,
    CWPlugin_GetVersionInfo,
    CWPlugin_GetFileTypeMappings
};
// clang-format on

static CWPLUGIN_ENTRY(CmdLine_GetObjectFlags)(const CWObjectFlags** objectFlags) {
    // clang-format off
    static const CWObjectFlags myFlags = {2,      0x80000000, "o",    ".b",   "i",    "s",    "d",    "mch",
                                          'CWIE', 'ELF ',     'CWIE', 'BRWS', 'CWIE', 'TEXT', 'CWIE', 'TEXT',
                                          'CWIE', 'TEXT'};
    // clang-format on

    *objectFlags = &myFlags;
    return 0;
}

// clang-format off
static CompilerLinkerPluginCallbacks plugin_cl_cb = {
    CWPlugin_GetTargetList,
    CWPlugin_GetDefaultMappingList,
    NULL,
    NULL,
    CmdLine_GetObjectFlags,
    NULL
};
// clang-format on

static CWPLUGIN_ENTRY(Linker_GetDropInFlags)(const DropInFlags** flags, SInt32* flagsSize) {
    static const DropInFlags sFlags = {
        kCurrentDropInFlagsVersion,
        CWDROPINLINKERTYPE,
        DROPINCOMPILERLINKERAPIVERSION_8,
        1,
        '****',
        DROPINCOMPILERLINKERAPIVERSION_12
    };

    *flags = &sFlags;
    *flagsSize = sizeof(sFlags);
    return 0;
}

static CWPLUGIN_ENTRY(Linker_GetDropInName)(const char** dropinName) {
    const char* name = "mwldeppc";

    *dropinName = name;
    return 0;
}

static CWPLUGIN_ENTRY(Linker_GetDisplayName)(const char** displayName) {
    const char* name = "Linker Tool Stub";

    *displayName = name;
    return 0;
}

static CWPLUGIN_ENTRY(Linker_GetPanelList)(const CWPanelList** panelList) {
    static const char* sPanelNames[2] = {"PPC EABI Linker", "PPC EABI Project"};
    static CWPanelList sPanelList = {kCurrentCWPanelListVersion, 2, sPanelNames};

    *panelList = &sPanelList;
    return 0;
}

static CWPLUGIN_ENTRY(Linker_GetTargetList)(const CWTargetList** targetList) {
    static SInt32 sCPU[1] = {'ePPC'};
    static SInt32 sOS[1] = {'EABI'};
    static CWTargetList sTargetList = {kCurrentCWTargetListVersion, 1, sCPU, 1, sOS};

    *targetList = &sTargetList;
    return 0;
}

static CWPLUGIN_ENTRY(Linker_GetDefaultMappingList)(const CWExtMapList** defaultMappingList) {
    // clang-format off
    static CWExtensionMapping sExtension[4] = {
        {0, ".doc", kIgnored},
        {'TEXT', ".lcf", 0},
        {'TEXT', "", kIgnored},
        {0, ".bin", kRsrcfile}
    };
    // clang-format on
    static CWExtMapList sExtensionMapList = {kCurrentCWExtMapListVersion, 4, sExtension};

    *defaultMappingList = &sExtensionMapList;
    return 0;
}

// clang-format off
static BasePluginCallbacks lk_cb = {
    NULL,
    Linker_GetDropInFlags,
    Linker_GetDisplayName,
    Linker_GetDropInName,
    Linker_GetPanelList,
    NULL,
    NULL,
    CWPlugin_GetVersionInfo,
    NULL
};
// clang-format on

// clang-format off
static CompilerLinkerPluginCallbacks lk_cl_cb = {
    Linker_GetTargetList,
    Linker_GetDefaultMappingList,
    NULL,
    NULL,
    NULL,
    NULL
};
// clang-format on

int RegisterStaticCompilerPlugin(void) {
    return RegisterStaticCompilerLinkerPlugin(&plugin_cb, &plugin_cl_cb) &&
        RegisterStaticCompilerLinkerPlugin(&lk_cb, &lk_cl_cb);
}

#include "cc-eabi-ppc.r"

int RegisterCompilerResources(void) {
    return RegisterResource("Compiler Errors", 10000, STR10000) &&
        RegisterResource("Compiler Strings", 10100, STR10100) &&
        RegisterResource("PPC Compiler Errors", 10001, STR10001) &&
        RegisterResource("Linker Errors", 11001, STR11001);
}
