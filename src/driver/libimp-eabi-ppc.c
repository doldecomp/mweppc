#include "driver/libimp-eabi-ppc.h"
#include "driver/ClientGlue.h"
#include "driver/CWPlugins.h"
#include "driver/DropInCompilerLinker.h"
#include "driver/ELF_Lib_Import.h"

CWPLUGIN_ENTRY(PPCLibImport_GetDropInFlags)(const DropInFlags** flags, SInt32* flagsSize) {
    static const DropInFlags sFlags = {
        kCurrentDropInFlagsVersion,
        CWDROPINCOMPILERTYPE,
        DROPINCOMPILERLINKERAPIVERSION_8,
        kGeneratescode | kCompMultiTargAware,
        Lang_MISC,
        DROPINCOMPILERLINKERAPIVERSION_12
    };

    *flags = &sFlags;
    *flagsSize = sizeof(sFlags);
    return 0;
}

CWPLUGIN_ENTRY(PPCLibImport_GetTargetList)(const CWTargetList** targetList) {
    static SInt32 sCPU[1] = {'ePPC'};
    static SInt32 sOS[1] = {'EABI'};
    static CWTargetList sTargetList = {kCurrentCWTargetListVersion, 1, sCPU, 1, sOS};

    *targetList = &sTargetList;
    return 0;
}

CWPLUGIN_ENTRY(PPCLibImport_GetDropInName)(const char** dropinName) {
    static const char* sDropInName = "MW Lib Import PPC EABI";

    *dropinName = sDropInName;
    return 0;
}

CWPLUGIN_ENTRY(PPCLibImport_GetDisplayName)(const char** displayName) {
    static const char* sDisplayName = "MW Lib Import PPC EABI";

    *displayName = sDisplayName;
    return 0;
}

CWPLUGIN_ENTRY(PPCLibImport_GetDefaultMappingList)(const CWExtMapList** defaultMappingList) {
    // clang-format off
    static CWExtensionMapping sExtension[5] = {
        {0, ".elf", 0},
        {0, ".a", 0},
        {'ELF ', "", 0},
        {'ELF ', ".o", 0},
        {'MPLF', "", 0}
    };
    // clang-format on
    static CWExtMapList sExtensionMapList = {kCurrentCWExtMapListVersion, 5, sExtension};

    *defaultMappingList = &sExtensionMapList;
    return 0;
}

CWPLUGIN_ENTRY(PPCLibImport_GetPanelList)(const CWPanelList** panelList) {
    static CWPanelList sPanelList = {kCurrentCWPanelListVersion, 0, NULL};

    *panelList = &sPanelList;
    return 0;
}

static CWPLUGIN_ENTRY(PPCLibImport_GetVersionInfo)(const VersionInfo** versionInfo) {
    static const VersionInfo vi = {2, 4, 1, 32};

    *versionInfo = &vi;
    return 0;
}

static CWPLUGIN_ENTRY(PPCLibImport_GetFileTypeMappings)(const OSFileTypeMappingList** mappings) {
    // clang-format off
    static const OSFileTypeMapping ftmes[2] = {
        {'MPLF', "!<arch>\n", 8, 0, NULL},
        {'ELF ', "\x7F" "ELF", 4, 0, NULL}
    };
    // clang-format on
    static const OSFileTypeMappingList ftml = {2, ftmes};

    *mappings = &ftml;
    return 0;
}

static CWPLUGIN_ENTRY(PPCLibImport_GetObjectFlags)(const CWObjectFlags** objectFlags) {
    // clang-format off
    static const CWObjectFlags myFlags = {2, 0, NULL, NULL, NULL, "s", NULL, NULL, 0, 0, 0, 0, 0, 0, 'CWIE', 'TEXT',
                                          0, 0};
    // clang-format on

    *objectFlags = &myFlags;
    return 0;
}

// clang-format off
static BasePluginCallbacks machlibimport_cb = {
    ELFImport_main,
    PPCLibImport_GetDropInFlags,
    PPCLibImport_GetDisplayName,
    PPCLibImport_GetDropInName,
    PPCLibImport_GetPanelList,
    NULL,
    NULL,
    PPCLibImport_GetVersionInfo,
    PPCLibImport_GetFileTypeMappings
};
// clang-format on

// clang-format off
static CompilerLinkerPluginCallbacks machlibimport_cl_cb = {
    PPCLibImport_GetTargetList,
    PPCLibImport_GetDefaultMappingList,
    NULL,
    NULL,
    PPCLibImport_GetObjectFlags,
    NULL
};
// clang-format on

int RegisterStaticLibImporterPlugin(void) {
    return RegisterStaticCompilerLinkerPlugin(&machlibimport_cb, &machlibimport_cl_cb);
}

int RegisterLibImporterResources(void) {
    return TRUE;
}
