// The shell's callbacks for the command-line parser (built with -bool off).
#include "driver/CLParserCallbacks_v1.h"
#include "driver/CLAccessPaths.h"
#include "driver/CLDropinCallbacks_V10.h"
#include "driver/CLErrors.h"
#include "driver/CLFiles.h"
#include "driver/CLIO.h"
#include "driver/CLMain.h"
#include "driver/CLOverlays.h"
#include "driver/CLPrefs.h"
#include "driver/CLSegs.h"
#include "driver/CLTarg.h"
#include "driver/cmdline.h"
#include "driver/CWPlugins.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/Files.h"
#include "driver/Generic.h"
#include "driver/MacSpecs.h"
#include "driver/Memory.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include "driver/pref_structs.h"
#include <string.h>

extern "C" {

CW_CALLBACK UCBParserAddAccessPath(CWPluginContext context, const CWNewAccessPathInfo* api) {
    OSSpec oss;
    Paths* paths;
    Path* path;
    int err;
    int failed;

    if ((err = OS_FSSpec_To_OSSpec(&api->pathSpec, &oss))) {
        context->callbackOSError = OS_OSErrorToMacError(err);
        return cwErrInvalidParameter;
    }

    if (optsCmdLine.verbose > 2) {
        CLReport(CLStr77, " search path", OS_PathSpecToString(&oss.path, STSbuf, sizeof(STSbuf)));
    }

    if ((api->type & cwAccessPathFlagUser) == 0) {
        paths = &gTarg->sysPaths;
    } else {
        paths = &gTarg->userPaths;
    }
    path = Path_New(&oss.path);

    path->flags = api->type & cwAccessPathFlagKeepSystem;
    if (api->position >= 0) {
        failed = !Paths_InsertPath(paths, api->position, path);
    } else {
        failed = !Paths_AddPath(paths, path);
    }
    if (failed) {
        return cwErrRequestFailed;
    }

    if (api->recursive) {
        Paths_GatherRecurse(path);
        if (CheckForUserBreak()) {
            return cwErrUserCanceled;
        }
    }

    if (!(api->type & cwAccessPathFlagUser)) {
        static_cast<shellContextType*>(context->shellContext)->systemAccessPathsChanged = TRUE;
    } else {
        static_cast<shellContextType*>(context->shellContext)->userAccessPathsChanged = TRUE;
    }

    return cwNoErr;
}

CW_CALLBACK UCBParserSwapAccessPaths(CWPluginContext context) {
    Path* tmp;
    UInt16 idx;

    for (idx = 0; idx < Paths_Count(&gTarg->sysPaths); idx++) {
        tmp = Paths_GetPath(&gTarg->sysPaths, idx);
        if (!(tmp->flags & cwAccessPathFlagKeepSystem)) {
            Paths_AddPath(&gTarg->userPaths, tmp);
            Paths_RemovePath(&gTarg->sysPaths, idx);
            idx--;
        }
    }

    return cwNoErr;
}

int (*PrefPanelsChangedCallback)(const char*);

CW_CALLBACK UCBParserSetNamedPreferences(
    CWPluginContext context, const char* panelName, Handle paneldata
) {
    PrefPanel* panel;

    panel = Prefs_FindPanel(panelName);
    if (!panel) {
        panel = PrefPanel_New(panelName, *paneldata, GetHandleSize(paneldata));
        if (!panel || !Prefs_AddPanel(panel)) {
            return cwErrRequestFailed;
        }
    } else {
        if (!PrefPanel_PutHandle(panel, paneldata)) {
            return cwErrRequestFailed;
        }
    }

    if (PrefPanelsChangedCallback) {
        PrefPanelsChangedCallback(panelName);
    }

    return cwNoErr;
}

CW_CALLBACK UCBParserSetFileOutputName(
    CWPluginContext context, SInt32 position, short which, const char* outfilename
) {
    if (position < 0) {
        return cwErrInvalidParameter;
    }

    File* file = Files_GetFile(&gTarg->files, position);
    if (!file) {
        return cwErrUnknownFile;
    }

    if (file->outfileowner) {
        CLReportError(CLStr103, file->srcfilename, file->outfilename);
    }

    strcpy(file->outfilename, outfilename);
    if ((int)which == 1) {
        file->outfileowner = CmdLineStageMask_Cg;
    } else if ((int)which == 2) {
        file->outfileowner = CmdLineStageMask_Pp;
    } else if ((int)which == 3) {
        file->outfileowner = CmdLineStageMask_Ds;
    } else {
        return cwErrInvalidParameter;
    }

    return cwNoErr;
}

CW_CALLBACK UCBParserSetOutputFileDirectory(CWPluginContext context, const CWFileSpec* idefss) {
    OSSpec fss;
    int err;

    if ((err = OS_FSSpec_To_OSSpec(idefss, &fss))) {
        context->callbackOSError = OS_OSErrorToMacError(err);
        return cwErrInvalidParameter;
    }

    gTarg->outputDirectory = fss.path;
    context->outputFileDirectory = *idefss;

    return cwNoErr;
}

CW_CALLBACK UCBParserAddOverlay1Group(
    CWPluginContext context, const char* name, const CWAddr64* addr, SInt32* newGroupNumber
) {
    if (gTarg->linkmodel != LinkModel_Overlay1) {
        return cwErrInvalidCallback;
    }

    OvlGroup* grp;
    OvlAddr oaddr;
    oaddr.hi = addr->hi;
    oaddr.lo = addr->lo;

    grp = OvlGroup_New(name, oaddr);
    if (!grp) {
        return cwErrRequestFailed;
    }
    if (!Overlays_AddOvlGroup(&gTarg->linkage.overlays, grp, newGroupNumber)) {
        return cwErrRequestFailed;
    }

    ++context->numOverlayGroups;
    if (optsCmdLine.verbose > 2) {
        CLReport(CLStr79, name, oaddr.hi, oaddr.lo);
    }

    return cwNoErr;
}

CW_CALLBACK UCBParserAddOverlay1(
    CWPluginContext context, const char* name, SInt32 groupNumber, SInt32* newOverlayNumber
) {
    if (gTarg->linkmodel != LinkModel_Overlay1) {
        return cwErrInvalidCallback;
    }

    OvlGroup* grp;
    Overlay* ovl;

    grp = Overlays_GetOvlGroup(&gTarg->linkage.overlays, groupNumber);
    if (!grp) {
        return cwErrRequestFailed;
    }
    ovl = Overlay_New(name);
    if (!ovl) {
        return cwErrOutOfMemory;
    }
    if (!OvlGroup_AddOverlay(grp, ovl, newOverlayNumber)) {
        return cwErrOutOfMemory;
    }

    if (optsCmdLine.verbose > 2) {
        CLReport(CLStr78, name, grp->name);
    }

    return cwNoErr;
}

CW_CALLBACK UCBParserAddSegment(
    CWPluginContext context, const char* name, short attrs, SInt32* newSegmentNumber
) {
    if (gTarg->linkmodel != LinkModel_Segmented) {
        return cwErrInvalidCallback;
    }

    Segment* seg;
    UInt16 index;

    seg = Segment_New(name, attrs);
    if (!Segments_AddSegment(&gTarg->linkage.segs, seg, &index)) {
        return cwErrRequestFailed;
    }

    *newSegmentNumber = index;
    return cwNoErr;
}

CW_CALLBACK UCBParserSetSegment(
    CWPluginContext context, SInt32 segmentNumber, const char* name, short attrs
) {
    if (gTarg->linkmodel != LinkModel_Segmented) {
        return cwErrInvalidCallback;
    }

    Segment* seg = Segments_GetSegment(&gTarg->linkage.segs, segmentNumber);
    if (!seg) {
        return cwErrUnknownSegment;
    }

    strncpy(seg->name, name, sizeof(seg->name));
    seg->name[sizeof(seg->name) - 1] = 0;
    seg->attrs = attrs;

    return cwNoErr;
}

// clang-format off
static CWParserCallbacks parser_cb = {
    UCBParserAddAccessPath,
    UCBParserSwapAccessPaths,
    UCBParserSetNamedPreferences,
    UCBParserSetFileOutputName,
    UCBParserSetOutputFileDirectory,
    UCBParserAddOverlay1Group,
    UCBParserAddOverlay1,
    UCBParserAddSegment,
    UCBParserSetSegment,
    NULL
};
// clang-format on
}

CWParserContext::CWParserContext() : CWPluginPrivateContext(CWDROPINPARSERTYPE, -1) {
    args = NULL;
    cpu = os = 0;
    numPlugins = 0;
    plugins = NULL;
    numPanels = 0;
    panelNames = NULL;
    panel_args = NULL;
    plugin_args = NULL;
    callbacks = &parser_cb;
}

CWParserContext::~CWParserContext() {}
