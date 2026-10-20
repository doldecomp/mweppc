#include "driver/CLPlugins.h"
#include "compiler/win32.h"
#include "driver/CLErrors.h"
#include "driver/CLIO.h"
#include "driver/CLMain.h"
#include "driver/CLPluginRequests.h"
#include "driver/CLPrefs.h"
#include "driver/cmdline.h"
#include "driver/CWPlugins.h"
#include "driver/Files.h"
#include "driver/MacFileTypes.h"
#include "driver/MacSpecs.h"
#include "driver/Memory.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include "driver/StringExtras.h"
#include <stdio.h>
#include <string.h>

// (MWCC's built-in, a call to the runtime's _stack_alloc; no MSL header declares it)
extern void* alloca(unsigned int size);

// (GetToolVersionInfo, first in the file, calls it)
char* Plugin_GetVersionInfoASCII(Plugin* pl);

// The statics are declared in the original's .bss order and 2-byte packed, as in the original
// (MWCC lays out .bss with the packing in force at the end of the file)
#pragma pack(2)
static Boolean useToolVersion;
static VersionInfo toolVersion;
static ToolVersionInfo toolVersionInfo;

// Read the compiler's own version resource: its version numbers and company, product,
// description and copyright
static void GetToolVersionInfo(void) {
    DWORD handle;
    char path[260];
    UINT length;
    VS_FIXEDFILEINFO* fixed;
    char* value;
    char text[0x50];
    DWORD size;
    void* info;
    char* copy;
    char* source;
    int i;

    useToolVersion = FALSE;
    OS_SpecToString(&clState.programSpec, path, sizeof(path));
    size = GetFileVersionInfoSizeA(path, &handle);
    if (size == 0) {
        return;
    }
    info = alloca(size);
    if (!GetFileVersionInfoA(path, handle, size, info)) {
        return;
    }
    if (!VerQueryValueA(info, "\\", (LPVOID*)&fixed, &length)) {
        return;
    }
    useToolVersion = TRUE;
    toolVersion.major = fixed->dwProductVersionMS >> 16;
    toolVersion.minor = fixed->dwProductVersionMS;
    toolVersion.patch = fixed->dwProductVersionLS >> 16;
    toolVersion.build = fixed->dwProductVersionLS;
    if (VerQueryValueA(info, "\\StringFileInfo\\040904B0\\CompanyName", (LPVOID*)&value, &length)) {
        toolVersionInfo.company = xstrdup(value);
    } else {
        toolVersionInfo.company = NULL;
    }
    if (VerQueryValueA(info, "\\StringFileInfo\\040904B0\\ProductName", (LPVOID*)&value, &length)) {
        toolVersionInfo.product = xstrdup(value);
    } else {
        toolVersionInfo.product = NULL;
    }
    if (VerQueryValueA(
            info, "\\StringFileInfo\\040904B0\\FileDescription", (LPVOID*)&value, &length
        )) {
        toolVersionInfo.tool = xstrdup(value);
    } else {
        toolVersionInfo.tool = NULL;
    }
    if (VerQueryValueA(
            info, "\\StringFileInfo\\040904B0\\LegalCopyright", (LPVOID*)&value, &length
        )) {
        copy = toolVersionInfo.copyright = xmalloc(NULL, strlen(value) + 2);
        for (i = 0; value[i]; i++) {
            if (value[i] == (char)0xA9) {
                *copy++ = '(';
                *copy++ = 'c';
                *copy++ = ')';
            } else {
                *copy++ = value[i];
            }
        }
        *copy = 0;
    } else {
        toolVersionInfo.copyright = NULL;
    }
    if (VerQueryValueA(
            info, "\\StringFileInfo\\040904B0\\ProductVersion", (LPVOID*)&value, &length
        )) {
        source = value;
        while ((*source >= '0' && *source <= '9') || *source == '.' || *source == ' ') {
            source++;
        }
        if (source <= value + 2 || *source == 0) {
            snprintf(text, sizeof(text), "%s", Plugin_GetVersionInfoASCII(NULL));
        } else {
            snprintf(text, sizeof(text), "%s (%s)", Plugin_GetVersionInfoASCII(NULL), source);
        }
        toolVersionInfo.version = xstrdup(text);
    } else {
        toolVersionInfo.version = xstrdup("Version %%s");
    }
}

const ToolVersionInfo* Plugin_GetToolVersionInfo(void) {
    return useToolVersion ? &toolVersionInfo : NULL;
}

static const char* Plugin_GetDisplayName(Plugin* pl) {
    const char* name;
    OS_ASSERT(251, pl);

    if (pl->cb->GetDisplayName && pl->cb->GetDisplayName(&name) == 0) {
        return name;
    } else {
        return "(no name found)";
    }
}

const char* Plugin_GetDropInName(Plugin* pl) {
    const char* name;
    OS_ASSERT(263, pl);

    if (pl->cb->GetDropInName && pl->cb->GetDropInName(&name) == 0) {
        return name;
    } else {
        return "(no name found)";
    }
}

const VersionInfo* Plugin_GetVersionInfo(Plugin* pl) {
    static const VersionInfo fakeversion = {0, 0, 0, 0};
    const VersionInfo* vi;
    OS_ASSERT(276, pl);

    if (pl->cb->GetVersionInfo && pl->cb->GetVersionInfo(&vi) == 0) {
        return vi;
    } else {
        return &fakeversion;
    }
}

char* Plugin_GetVersionInfoASCII(Plugin* pl) {
    const VersionInfo* vi;
    char* buffer;
    char vernum[16];
    char* bptr;

    if (!pl && useToolVersion) {
        vi = &toolVersion;
    } else if (!pl) {
        return NULL;
    } else {
        vi = Plugin_GetVersionInfo(pl);
    }

    buffer = xmalloc("version info", 64);
    if (vi->major | vi->minor | vi->patch | vi->build != 0) {
        bptr = vernum;
        bptr += sprintf(bptr, "%u", vi->major);
        bptr += sprintf(bptr, ".%u", vi->minor);
        if (vi->patch) {
            bptr += sprintf(bptr, ".%u", vi->patch);
        }
        if (vi->build) {
            bptr += sprintf(bptr, " build %u", vi->build);
        }
        sprintf(buffer, "Version %s", vernum);
        return buffer;
    } else {
        return NULL;
    }
}

DropInFlags* Plugin_GetDropInFlags(Plugin* pl) {
    static DropInFlags retdf;
    const DropInFlags* df;
    SInt32 dfsize;

    OS_ASSERT(329, pl);

    if (pl->cb->GetDropInFlags && pl->cb->GetDropInFlags(&df, &dfsize) == 0) {
        memset(&retdf, 0, sizeof(retdf));
        memcpy(&retdf, df, dfsize);
        return &retdf;
    } else {
        return NULL;
    }
}

OSType Plugin_GetPluginType(Plugin* pl) {
    DropInFlags* df;

    OS_ASSERT(345, pl);

    df = Plugin_GetDropInFlags(pl);
    if (df) {
        return df->dropintype;
    } else {
        return 'NONE';
    }
}

const CWTargetList* Plugin_CL_GetTargetList(Plugin* pl) {
    static const CWTargetList faketl = {kCurrentCWTargetListVersion, 0, NULL, 0, NULL};
    const CWTargetList* tl;

    OS_ASSERT(358, pl);
    OS_ASSERT(359, pl->cl_cb != NULL);

    if (pl->cl_cb->GetTargetList && pl->cl_cb->GetTargetList(&tl) == 0) {
        return tl;
    } else {
        return &faketl;
    }
}

const CWPanelList* Plugin_GetPanelList(Plugin* pl) {
    static CWPanelList fakepnl = {kCurrentCWPanelListVersion, 0, NULL};
    const CWPanelList* pnl;

    OS_ASSERT(381, pl);

    if (pl->cb->GetPanelList && pl->cb->GetPanelList(&pnl) == 0) {
        return pnl;
    } else {
        return &fakepnl;
    }
}

const CWExtMapList* Plugin_CL_GetExtMapList(Plugin* pl) {
    static CWExtMapList fakeel = {kCurrentCWExtMapListVersion, 0, NULL};
    const CWExtMapList* el;

    OS_ASSERT(401, pl);
    OS_ASSERT(402, pl->cl_cb != NULL);

    if (pl->cl_cb->GetDefaultMappingList && pl->cl_cb->GetDefaultMappingList(&el) == 0) {
        return el;
    } else {
        return &fakeel;
    }
}

const OSFileTypeMappingList* Plugin_GetFileTypeMappingList(Plugin* pl) {
    const OSFileTypeMappingList* ftml;

    OS_ASSERT(422, pl);

    if (pl->cb->GetFileTypeMappings && pl->cb->GetFileTypeMappings(&ftml) == 0) {
        return ftml;
    } else {
        return NULL;
    }
}

const CWObjectFlags* Plugin_CL_GetObjectFlags(Plugin* pl) {
    static const CWObjectFlags fake = {2, 0, "", "", "", "", "", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const CWObjectFlags* cof;

    OS_ASSERT(434, pl);
    OS_ASSERT(435, pl->cl_cb != NULL);

    if (pl->cl_cb->GetObjectFlags && pl->cl_cb->GetObjectFlags(&cof) == 0) {
        return cof;
    } else if (Plugin_GetDropInFlags(pl)->dropintype == CWDROPINCOMPILERTYPE) {
        return NULL;
    } else {
        return &fake;
    }
}

Boolean Plugin_MatchesName(Plugin* pl, const char* name) {
    const char* pname = Plugin_GetDropInName(pl);
    return strcmp(pname, name) == 0;
}

Boolean Plugin_CL_MatchesTarget(Plugin* pl, OSType cpu, OSType os, Boolean exact) {
    const CWTargetList* tl;
    int idx;
    int ix;

    tl = Plugin_CL_GetTargetList(pl);

    for (idx = 0; idx < tl->cpuCount; idx++) {
        if (cpu == targetCPUAny ||
            cpu == (OSType)tl->cpus[idx] ||
            (tl->cpus[idx] == targetCPUAny && !exact)) {
            for (ix = 0; ix < tl->osCount; ix++) {
                if (os == targetOSAny ||
                    os == (OSType)tl->oss[ix] ||
                    (tl->oss[ix] == targetOSAny && !exact)) {
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

static Boolean CL_MatchesExtMapping(
    CWExtensionMapping* map, OSType type, const char* ext, Boolean exact
) {
    if (exact && type && map->type && type == map->type) {
        return TRUE;
    }

    if ((((!map->extension[0] && !exact) || (!ext[0] && !exact)) && map->type == type) ||
        (((!map->type && !exact) || (!type && !exact)) && !ustrcmp(map->extension, ext)) ||
        (!ustrcmp(map->extension, ext) && map->type == type)) {
        return TRUE;
    }

    return FALSE;
}

Boolean Plugin_CL_MatchesFileType(Plugin* pl, OSType type, const char* extension, Boolean exact) {
    const CWExtMapList* el;
    int idx;

    el = Plugin_CL_GetExtMapList(pl);

    for (idx = 0; idx < el->nMappings; idx++) {
        if (CL_MatchesExtMapping(&el->mappings[idx], type, extension, exact)) {
            return TRUE;
        }
    }

    return FALSE;
}

Boolean Plugin_MatchesType(Plugin* pl, OSType type, OSType lang, Boolean exact) {
    const DropInFlags* df;

    df = Plugin_GetDropInFlags(pl);
    if (df->dropintype == type || type == CWDROPINANYTYPE) {
        if ((OSType)df->edit_language == lang ||
            lang == Lang_Any ||
            (df->edit_language == Lang_Any && !exact)) {
            return TRUE;
        }
    }

    return FALSE;
}

Boolean Plugin_Pr_MatchesPlugin(Plugin* pl, CLPluginInfo* pluginfo, OSType cpu, OSType os) {
    Boolean isSupported;

    OS_ASSERT(566, pl->pr_cb != NULL);

    if (pl->pr_cb->SupportsPlugin &&
        pl->pr_cb->SupportsPlugin(pluginfo, cpu, os, &isSupported) == 0) {
        return isSupported;
    } else {
        return FALSE;
    }
}

Boolean Plugin_Pr_MatchesPanels(Plugin* pl, int numPanels, const char** panelNames) {
    Boolean isSupported;

    OS_ASSERT(578, pl->pr_cb != NULL);

    if (pl->pr_cb->SupportsPanels &&
        pl->pr_cb->SupportsPanels(numPanels, panelNames, &isSupported) == 0) {
        return isSupported;
    } else {
        return FALSE;
    }
}

Boolean Plugin_CL_WriteObjectFile(
    Plugin* pl, FSSpec* srcfss, FSSpec* outfss, OSType creator, OSType type, Handle data
) {
    OSSpec outoss;

    OS_ASSERT(590, pl->cl_cb != NULL);
    OS_ASSERT(591, data != NULL && srcfss != NULL && outfss != NULL);

    if (pl->cl_cb->WriteObjectFile) {
        return pl->cl_cb->WriteObjectFile(srcfss, outfss, creator, type, data) == 0;
    }

    OS_FSSpec_To_OSSpec(outfss, &outoss);
    return WriteBinaryHandleToFile(&outoss, creator, type, OS_PeekMacHandle(data));
}

Boolean Plugin_CL_GetCompilerMapping(Plugin* pl, OSType type, const char* ext, UInt32* flags) {
    const CWExtMapList* el;
    int idx;
    int pick;

    el = Plugin_CL_GetExtMapList(pl);
    pick = -1;
    for (idx = 0; idx < el->nMappings; idx++) {
        if (CL_MatchesExtMapping(&el->mappings[idx], type, ext, FALSE)) {
            pick = idx;
            if (CL_MatchesExtMapping(&el->mappings[idx], type, ext, TRUE)) {
                break;
            }
        }
    }

    if (pick < 0) {
        *flags = 0;
        return FALSE;
    } else {
        *flags = el->mappings[pick].flags;
        return TRUE;
    }
}

static Boolean SupportedPlugin(Plugin* pl, const char** reason) {
    static const SInt32 DropInFlagsSize[3] = {0, sizeof(DropInFlagsV1), sizeof(DropInFlags)};
    const DropInFlags* df;
    const CWObjectFlags* cof;
    SInt32 dfsize;

    *reason = "";
    if (pl->cb->GetDropInFlags == NULL) {
        *reason = "GetDropInFlags callback not found";
        return FALSE;
    }
    if (pl->cb->GetDropInFlags(&df, &dfsize)) {
        *reason = "GetDropInFlags callback failed";
        return FALSE;
    }
    if (df->dropintype != CWDROPINCOMPILERTYPE &&
        df->dropintype != CWDROPINLINKERTYPE &&
        df->dropintype != CWDROPINPARSERTYPE &&
        df->dropintype != CWDROPINDRIVERTYPE) {
        *reason = "The plugin type is not supported by this driver";
        return FALSE;
    }
    if (df->earliestCompatibleAPIVersion > 11) {
        *reason = "The plugin's earliest compatible API version is too new for this driver";
        return FALSE;
    }
    if (df->earliestCompatibleAPIVersion > 1 && df->newestAPIVersion < 11 && clState.pluginDebug) {
        CLPrintErr(
            "%s's newest compatible API version is probably too old for this driver\n",
            Plugin_GetDropInName(pl)
        );
    }
    if (dfsize != DropInFlagsSize[df->rsrcversion]) {
        *reason = "The plugin's DropInFlags has an unexpected size";
        return FALSE;
    }
    if (!(df->dropinflags & isExecutableTool) && !pl->cb->main) {
        *reason = "The plugin has no entry point";
        return FALSE;
    }
    if ((df->dropinflags & isExecutableTool) && pl->cb->main) {
        *reason = "The executable tool stub has an entry point";
        return FALSE;
    }

    if (pl->cl_cb) {
        if (pl->cl_cb->GetObjectFlags == NULL && df->dropintype == CWDROPINCOMPILERTYPE) {
            *reason = "GetObjectFlags callback not found in compiler plugin";
            return FALSE;
        }

        cof = Plugin_CL_GetObjectFlags(pl);
        if (cof->version < 2 || cof->flags & 0x7FFFFFFF) {
            *reason = "The object flags data is out-of-date or invalid";
            return FALSE;
        }
    }

    return TRUE;
}

static Boolean VerifyPanels(Plugin* pl) {
    const CWPanelList* pls;
    Boolean failed;
    int idx;

    failed = FALSE;

    if (pl->cb->GetPanelList && pl->cb->GetPanelList(&pls) == 0) {
        for (idx = 0; idx < pls->count; idx++) {
            if (Prefs_FindPanel(pls->names[idx]) == NULL) {
                CLReportError(CLStr91, pls->names[idx]);
                failed = TRUE;
            }
        }
    }

    return !failed;
}

Plugin* Plugin_New(
    const BasePluginCallbacks* cb,
    const CompilerLinkerPluginCallbacks* cl_cb,
    const ParserPluginCallbacks* pr_cb
) {
    Plugin* p;

    p = xmalloc(NULL, sizeof(Plugin));
    if (!p) {
        return NULL;
    }
    if (!cb) {
        return NULL;
    }

    p->cb = xmalloc(NULL, sizeof(BasePluginCallbacks));
    if (!p->cb) {
        return NULL;
    }
    *p->cb = *cb;

    if (cl_cb) {
        p->cl_cb = xmalloc(NULL, sizeof(CompilerLinkerPluginCallbacks));
        if (!p->cl_cb) {
            return NULL;
        }
        *p->cl_cb = *cl_cb;
    } else {
        p->cl_cb = NULL;
    }

    if (pr_cb) {
        p->pr_cb = xmalloc(NULL, sizeof(ParserPluginCallbacks));
        if (!p->pr_cb) {
            return NULL;
        }
        *p->pr_cb = *pr_cb;
    } else {
        p->pr_cb = NULL;
    }

    p->context = NULL;
    p->next = NULL;
    return p;
}

void Plugin_Free(Plugin* pl) {
    if (pl) {
        if (pl->cb) {
            xfree(pl->cb);
        }
        if (pl->cl_cb) {
            xfree(pl->cl_cb);
        }
        if (pl->pr_cb) {
            xfree(pl->pr_cb);
        }
        xfree(pl);
    }
}

Boolean Plugin_VerifyPanels(Plugin* pl) {
    if (!VerifyPanels(pl)) {
        CLReportError(CLStr92, Plugin_GetDropInName(pl));
        return FALSE;
    } else {
        return TRUE;
    }
}

static Plugin* pluginlist;

void Plugins_Init(void) {
    pluginlist = NULL;
    GetToolVersionInfo();
}

void Plugins_Term(void) {
    Plugin* scan;
    Plugin* next;

    scan = pluginlist;
    while (scan) {
        next = scan->next;
        SendInitOrTermRequest(scan, FALSE);
        Plugin_Free(scan);
        scan = next;
    }

    if (toolVersionInfo.company) {
        xfree(toolVersionInfo.company);
    }
    if (toolVersionInfo.product) {
        xfree(toolVersionInfo.product);
    }
    if (toolVersionInfo.tool) {
        xfree(toolVersionInfo.tool);
    }
    if (toolVersionInfo.copyright) {
        xfree(toolVersionInfo.copyright);
    }
    if (toolVersionInfo.version) {
        xfree(toolVersionInfo.version);
    }
}

int Plugins_Add(Plugin* pl) {
    Plugin** scan;
    const char* dropinname;
    const DropInFlags* df;
    const CWTargetList* tl;
    const CWExtMapList* el;
    const CWPanelList* pnl;
    const char* failreason;
    SInt32 vislang;
    SInt16 step;

    dropinname = Plugin_GetDropInName(pl);
    pl->cached_ascii_version = Plugin_GetVersionInfoASCII(pl);

    if (!SupportedPlugin(pl, &failreason)) {
        CLReportError(CLStr88, dropinname, pl->cached_ascii_version, failreason);
        return FALSE;
    }

    scan = &pluginlist;
    while (*scan) {
        if (Plugin_MatchesName(*scan, dropinname)) {
            CLReportError(CLStr89, dropinname);
            return FALSE;
        }
        scan = &(*scan)->next;
    }

    *scan = pl;

    df = Plugin_GetDropInFlags(pl);
    if (!(df->dropinflags & isExecutableTool) && !SendInitOrTermRequest(pl, TRUE)) {
        CLReportError(CLStr3, dropinname);
        return FALSE;
    }

    if (df->dropintype == CWDROPINCOMPILERTYPE && df->dropinflags & 0x17C000) {
        CLReportError(CLStr4, "compiler", dropinname);
    }
    if (df->dropintype == CWDROPINLINKERTYPE && df->dropinflags & 0x5FE4000) {
        CLReportError(CLStr4, "linker", dropinname);
    }

    if (clState.pluginDebug) {
        vislang = df->edit_language ? df->edit_language : '----';
        CLPrint("Added plugin '%s', version '%s'\n", dropinname, pl->cached_ascii_version);
        CLPrint(
            "Type: '%c%c%c%c';  Lang: '%c%c%c%c';  API range: %d-%d\n",
            (df->dropintype & 0xFF000000) >> 24,
            (df->dropintype & 0x00FF0000) >> 16,
            (df->dropintype & 0x0000FF00) >> 8,
            (df->dropintype & 0x000000FF),
            (vislang & 0xFF000000) >> 24,
            (vislang & 0x00FF0000) >> 16,
            (vislang & 0x0000FF00) >> 8,
            (vislang & 0x000000FF),
            df->earliestCompatibleAPIVersion,
            df->newestAPIVersion
        );

        if (pl->cl_cb) {
            tl = Plugin_CL_GetTargetList(pl);
            CLPrint("Target CPUs: ");
            for (step = 0; step < tl->cpuCount; step++) {
                UInt32 cpu = tl->cpus[step];
                CLPrint(
                    "'%c%c%c%c', ",
                    (cpu & 0xFF000000) >> 24,
                    (cpu & 0x00FF0000) >> 16,
                    (cpu & 0x0000FF00) >> 8,
                    (cpu & 0x000000FF)
                );
            }
            CLPrint("\nTarget OSes: ");
            for (step = 0; step < tl->osCount; step++) {
                UInt32 os = tl->oss[step];
                CLPrint(
                    "'%c%c%c%c', ",
                    (os & 0xFF000000) >> 24,
                    (os & 0x00FF0000) >> 16,
                    (os & 0x0000FF00) >> 8,
                    (os & 0x000000FF)
                );
            }
            CLPrint("\n");

            el = Plugin_CL_GetExtMapList(pl);
            CLPrint("File mappings:\n");
            for (step = 0; step < el->nMappings; step++) {
                if (el->mappings[step].type) {
                    CLPrint(
                        "\tFile type: '%c%c%c%c'  Extension: '%s'\n",
                        (el->mappings[step].type & 0xFF000000) >> 24,
                        (el->mappings[step].type & 0x00FF0000) >> 16,
                        (el->mappings[step].type & 0x0000FF00) >> 8,
                        (el->mappings[step].type & 0x000000FF),
                        el->mappings[step].extension
                    );
                } else {
                    CLPrint("\tFile type: <none>  Extension: '%s'\n", el->mappings[step].extension);
                }
            }
        }

        pnl = Plugin_GetPanelList(pl);
        CLPrint("Pref panels needed:\n");
        for (step = 0; step < pnl->count; step++) {
            CLPrint("\t'%s'\n", pnl->names[step]);
        }

        CLPrint("Dropin flags:\n");
        if (df->dropinflags & isExecutableTool) {
            CLPrint("\texecutable tool,\n");
        }
        if (df->dropintype == CWDROPINCOMPILERTYPE) {
            if (df->dropinflags & kGeneratescode) {
                CLPrint("\tgenerates code,\n");
            }
            if (df->dropinflags & kGeneratesrsrcs) {
                CLPrint("\tgenerates resources, \n");
            }
            if (df->dropinflags & kCanpreprocess) {
                CLPrint("\tcan preprocess, \n");
            }
            if (df->dropinflags & kCanprecompile) {
                CLPrint("\tcan precompile, \n");
            }
            if (df->dropinflags & kIspascal) {
                CLPrint("\tis Pascal, \n");
            }
            if (df->dropinflags & kCanimport) {
                CLPrint("\tcan import, \n");
            }
            if (df->dropinflags & kCandisassemble) {
                CLPrint("\tcan disassemble, \n");
            }
            if (df->dropinflags & kCompAllowDupFileNames) {
                CLPrint("\tallow duplicate filenames, \n");
            }
            if (df->dropinflags & kCompMultiTargAware) {
                CLPrint("\tallow multiple targets, \n");
            }
            if (df->dropinflags & kCompUsesTargetStorage) {
                CLPrint("\tuses target storage, \n");
            }
            if (df->dropinflags & kCompEmitsOwnBrSymbols) {
                CLPrint("\temits own browser symbols, \n");
            }
            if (df->dropinflags & kCompAlwaysReload) {
                CLPrint("\tshould be always reloaded, \n");
            }
            if (df->dropinflags & kCompRequiresProjectBuildStartedMsg) {
                CLPrint("\trequires project build started msg, \n");
            }
            if (df->dropinflags & kCompRequiresTargetBuildStartedMsg) {
                CLPrint("\trequires target build started msg, \n");
            }
            if (df->dropinflags & kCompRequiresSubProjectBuildStartedMsg) {
                CLPrint("\trequires subproject build started msg, \n");
            }
            if (df->dropinflags & kCompRequiresFileListBuildStartedMsg) {
                CLPrint("\trequires file list build started msg, \n");
            }
        }

        if (df->dropintype == CWDROPINLINKERTYPE) {
            if (df->dropinflags & cantDisassemble) {
                CLPrint("\tcan't disassemble, \n");
            }
            if (df->dropinflags & isPostLinker) {
                CLPrint("\tis a post-linker, \n");
            }
            if (df->dropinflags & linkAllowDupFileNames) {
                CLPrint("\tallow duplicate filenames, \n");
            }
            if (df->dropinflags & linkMultiTargAware) {
                CLPrint("\tallow multiple targets, \n");
            }
            if (df->dropinflags & isPreLinker) {
                CLPrint("\tis a pre-linker, \n");
            }
            if (df->dropinflags & linkerUsesTargetStorage) {
                CLPrint("\tuses target storage, \n");
            }
            if (df->dropinflags & linkerUnmangles) {
                CLPrint("\tsupports unmangling, \n");
            }
            if (df->dropinflags & magicCapLinker) {
                CLPrint("\tis Magic Cap linker, \n");
            }
            if (df->dropinflags & linkAlwaysReload) {
                CLPrint("\tshould be always reloaded, \n");
            }
            if (df->dropinflags & linkRequiresProjectBuildStartedMsg) {
                CLPrint("\trequires project build started msg, \n");
            }
            if (df->dropinflags & linkRequiresTargetBuildStartedMsg) {
                CLPrint("\trequires target build started msg, \n");
            }
            if (df->dropinflags & linkRequiresSubProjectBuildStartedMsg) {
                CLPrint("\trequires subproject build started msg, \n");
            }
            if (df->dropinflags & linkRequiresFileListBuildStartedMsg) {
                CLPrint("\trequires file list build started msg, \n");
            }
            if (df->dropinflags & linkRequiresTargetLinkStartedMsg) {
                CLPrint("\trequires target link started msg, \n");
            }
            if (df->dropinflags & linkerWantsPreRunRequest) {
                CLPrint("\twants pre-run request, \n");
            }
        }

        CLPrint("\n");
    }

    return TRUE;
}

Plugin* Plugins_CL_MatchTarget(Plugin* list, OSType cpu, OSType os, OSType type, OSType lang) {
    Plugin* scan;
    Plugin* pick;

    scan = list ? list : pluginlist;
    pick = NULL;
    while (scan) {
        if (scan->cl_cb) {
            if (Plugin_CL_MatchesTarget(scan, cpu, os, FALSE) &&
                Plugin_MatchesType(scan, type, lang, FALSE)) {
                pick = scan;
                if (Plugin_CL_MatchesTarget(scan, cpu, os, TRUE) &&
                    Plugin_MatchesType(scan, type, lang, TRUE)) {
                    break;
                }
            }
        }
        scan = scan->next;
    }

    return pick;
}

Plugin* Plugins_GetPluginForFile(
    Plugin* list,
    OSType plugintype,
    OSType cpu,
    OSType os,
    OSType type,
    const char* ext,
    OSType lang
) {
    Plugin* scan;
    Plugin* pick;

    scan = list ? list : pluginlist;
    pick = NULL;
    while (scan) {
        if (Plugin_MatchesType(scan, plugintype, lang, TRUE) && scan->cl_cb) {
            if (Plugin_CL_MatchesTarget(scan, cpu, os, FALSE) &&
                Plugin_CL_MatchesFileType(scan, type, ext, FALSE)) {
                pick = scan;
                if (Plugin_CL_MatchesTarget(scan, cpu, os, TRUE) &&
                    Plugin_CL_MatchesFileType(scan, type, ext, TRUE)) {
                    break;
                }
            }
        }
        scan = scan->next;
    }

    return pick;
}

Plugin* Plugins_GetLinker(Plugin* list, OSType cpu, OSType os) {
    Plugin* scan;
    Plugin* pick;

    scan = list ? list : pluginlist;
    pick = NULL;
    while (scan) {
        if (Plugin_MatchesType(scan, CWDROPINLINKERTYPE, Lang_Any, TRUE)) {
            if (!(Plugin_GetDropInFlags(scan)->dropinflags & (isPreLinker | isPostLinker))) {
                if (Plugin_CL_MatchesTarget(scan, cpu, os, FALSE)) {
                    pick = scan;
                    if (Plugin_CL_MatchesTarget(scan, cpu, os, TRUE)) {
                        break;
                    }
                }
            }
        }
        scan = scan->next;
    }

    return pick;
}

Plugin* Plugins_GetPreLinker(Plugin* list, OSType cpu, OSType os) {
    Plugin* scan;
    Plugin* pick;

    scan = list ? list : pluginlist;
    pick = NULL;
    while (scan) {
        if (Plugin_MatchesType(scan, CWDROPINLINKERTYPE, Lang_Any, TRUE)) {
            if (Plugin_GetDropInFlags(scan)->dropinflags & isPreLinker) {
                if (Plugin_CL_MatchesTarget(scan, cpu, os, FALSE)) {
                    pick = scan;
                    if (Plugin_CL_MatchesTarget(scan, cpu, os, TRUE)) {
                        break;
                    }
                }
            }
        }
        scan = scan->next;
    }

    return pick;
}

Plugin* Plugins_GetPostLinker(Plugin* list, OSType cpu, OSType os) {
    Plugin* scan;
    Plugin* pick;

    scan = list ? list : pluginlist;
    pick = NULL;
    while (scan) {
        if (Plugin_MatchesType(scan, CWDROPINLINKERTYPE, Lang_Any, TRUE)) {
            if (Plugin_GetDropInFlags(scan)->dropinflags & isPostLinker) {
                if (Plugin_CL_MatchesTarget(scan, cpu, os, FALSE)) {
                    pick = scan;
                    if (Plugin_CL_MatchesTarget(scan, cpu, os, TRUE)) {
                        break;
                    }
                }
            }
        }
        scan = scan->next;
    }

    return pick;
}

Plugin* Plugins_GetParserForPlugin(
    Plugin* list,
    OSType style,
    int numPlugins,
    CLPluginInfo* plugins,
    OSType cpu,
    OSType os,
    int numPanels,
    const char** panelNames
) {
    Plugin* scan;
    Plugin* pick;
    Boolean allpanels;
    Boolean hasplugintype;
    Boolean* supports;

    scan = list ? list : pluginlist;
    pick = NULL;
    supports = alloca(numPanels);

    while (scan) {
        if (Plugin_MatchesType(scan, CWDROPINPARSERTYPE, style, TRUE)) {
            int idx;
            int pnl;
            hasplugintype = FALSE;
            for (idx = 0; idx < numPlugins; idx++) {
                if (plugins[idx].plugintype != CWDROPINPARSERTYPE &&
                    plugins[idx].plugintype != CWDROPINDRIVERTYPE &&
                    !plugins[idx].storeCommandLine) {
                    if (Plugin_Pr_MatchesPlugin(scan, &plugins[idx], cpu, os)) {
                        hasplugintype = TRUE;
                    }
                }
            }

            if (hasplugintype) {
                pick = scan;
                allpanels = TRUE;
                for (pnl = 0; pnl < numPanels; pnl++) {
                    allpanels &=
                        (supports[pnl] = Plugin_Pr_MatchesPanels(scan, 1, &panelNames[pnl]));
                }

                if (allpanels) {
                    break;
                }
            }
        }
        scan = scan->next;
    }

    if (pick && !allpanels) {
        int idx;
        CLReport(CLStr5);

        for (idx = 0; idx < numPanels; idx++) {
            if (!supports[idx]) {
                CLReport(CLStr6, panelNames[idx]);
            }
        }
    }

    return pick;
}

int Plugins_GetPluginList(Plugin* list, int* numPlugins, CLPluginInfo** pluginInfo) {
    Plugin* scan;
    int idx;
    const DropInFlags* df;
    const VersionInfo* vi;

    scan = list ? list : pluginlist;
    idx = 0;
    while (scan) {
        idx++;
        scan = scan->next;
    }

    *numPlugins = idx;
    *pluginInfo = xmalloc(NULL, sizeof(CLPluginInfo) * idx);
    if (!*pluginInfo) {
        return FALSE;
    }

    scan = list ? list : pluginlist;
    idx = 0;
    while (scan) {
        df = Plugin_GetDropInFlags(scan);
        vi = Plugin_GetVersionInfo(scan);
        OS_ASSERT(1270, df != NULL);
        OS_ASSERT(1271, vi != NULL);
        (*pluginInfo)[idx].plugintype = df->dropintype;
        (*pluginInfo)[idx].language = df->edit_language;
        (*pluginInfo)[idx].dropinflags = df->dropinflags;
        (*pluginInfo)[idx].version = scan->cached_ascii_version;
        (*pluginInfo)[idx].storeCommandLine = (df->dropinflags & 1) != 0;
        scan = scan->next;
        idx++;
    }

    return TRUE;
}

int Plugins_GetPrefPanelUnion(Plugin* list, int* numPanels, const char*** panelNames) {
    Plugin* scan;
    int tempNum;
    int idx;
    const char** tempPanels;

    scan = list ? list : pluginlist;

    tempNum = 0;
    while (scan) {
        const CWPanelList* pl;
        pl = Plugin_GetPanelList(scan);
        tempNum += pl->count;
        scan = scan->next;
    }

    tempPanels = xmalloc("plugin preference union", sizeof(const char*) * tempNum);

    idx = 0;
    scan = list ? list : pluginlist;
    while (scan) {
        const CWPanelList* pl;
        int step;

        pl = Plugin_GetPanelList(scan);
        for (step = 0; step < pl->count; step++) {
            int cmp;
            for (cmp = 0; cmp < idx; cmp++) {
                if (!ustrcmp(tempPanels[cmp], pl->names[step])) {
                    break;
                }
            }

            if (cmp >= idx) {
                tempPanels[idx++] = pl->names[step];
            }
        }

        scan = scan->next;
    }

    *panelNames = xmalloc(NULL, idx * sizeof(const char*));
    if (!*panelNames) {
        return FALSE;
    }

    *panelNames = xrealloc("plugin preference union", tempPanels, idx * sizeof(const char*));
    *numPanels = idx;
    return TRUE;
}

int Plugin_AddFileTypeMappings(Plugin* pl, OSFileTypeMappings** mlist) {
    const OSFileTypeMappingList* ftml;

    ftml = Plugin_GetFileTypeMappingList(pl);
    if (!ftml) {
        return TRUE;
    } else {
        AddFileTypeMappingList(NULL, ftml);
        return TRUE;
    }
}

int Plugins_AddFileTypeMappingsForTarget(
    Plugin* list, OSFileTypeMappings** mlist, OSType cpu, OSType os
) {
    if (!list) {
        list = pluginlist;
    }

    while (list) {
        if (!list->cl_cb || Plugin_CL_MatchesTarget(list, cpu, os, FALSE)) {
            Plugin_AddFileTypeMappings(list, mlist);
        }
        list = list->next;
    }

    return TRUE;
}

SInt16 Plugin_Call(Plugin* pl, void* context) {
    if (pl->cb->main) {
        return pl->cb->main(context);
    } else {
        return cwErrRequestFailed;
    }
}
