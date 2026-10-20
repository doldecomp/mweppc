// The plugin side of the base callbacks, each checking the context and calling the shell through
// its CW_BasePluginCallbacks. The binary keeps the ones its plugins call; the linker dropped the
// others (they have no data).
#include "driver/CWPluginsPrivate.h"
#include "driver/CLDropinCallbacks_V10.h"
#include "driver/CWPlugins.h"
#include "driver/Files.h"

extern "C" {

// (inside extern "C" they keep their C names)
static Boolean ValidateContext(CWPluginContext context) {
    return (context && context->shellSignature == 'CWIE') &&
        !(context->request == reqInitialize ||
          context->request == reqTerminate ||
          context->request == reqIdle);
}

static Boolean ValidateInitTermContext(CWPluginContext context) {
    return context &&
        (context->shellSignature == 'CWIE') &&
        ((context->request == reqInitialize) ||
         (context->request == reqTerminate) ||
         (context->request == reqIdle));
}

static Boolean IsVCSContext(CWPluginContext context) {
    return context && (context->pluginType == CWDROPINVCSTYPE);
}

CW_CALLBACK CWGetPluginRequest(CWPluginContext context, SInt32* request) {
    if (!ValidateContext(context) && !ValidateInitTermContext(context)) {
        return cwErrInvalidParameter;
    }
    if (request == NULL) {
        return cwErrInvalidParameter;
    }

    *request = context->request;
    return cwNoErr;
}

CW_CALLBACK CWGetAPIVersion(CWPluginContext context, SInt32* version) {
    if (!ValidateContext(context) && !ValidateInitTermContext(context)) {
        return cwErrInvalidParameter;
    }
    if (version == NULL) {
        return cwErrInvalidParameter;
    }

    *version = context->apiVersion;
    return cwNoErr;
}

CW_CALLBACK CWGetProjectFile(CWPluginContext context, CWFileSpec* projectSpec) {
    if (IsVCSContext(context) || !ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    if (projectSpec == NULL) {
        return cwErrInvalidParameter;
    }

    *projectSpec = context->projectFile;
    return cwNoErr;
}

CW_CALLBACK CWGetTargetName(CWPluginContext context, char* name, short maxLength) {
    if (IsVCSContext(context) || !ValidateContext(context)) {
        return cwErrInvalidParameter;
    }

    if (context->apiVersion >= 8) {
        return context->callbacks->cbGetTargetName(context, name, maxLength);
    } else {
        return cwErrRequestFailed;
    }
}

CW_CALLBACK CWGetProjectFileCount(CWPluginContext context, SInt32* count) {
    if (IsVCSContext(context) || !ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    if (count == NULL) {
        return cwErrInvalidParameter;
    }

    *count = context->numFiles;
    return cwNoErr;
}

CW_CALLBACK CWGetFileInfo(
    CWPluginContext context,
    SInt32 whichfile,
    Boolean checkFileLocation,
    CWProjectFileInfo* fileinfo
) {
    if (IsVCSContext(context) || !ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    if (fileinfo == NULL) {
        return cwErrInvalidParameter;
    }

    return context->callbacks->cbGetFileInfo(context, whichfile, checkFileLocation, fileinfo);
}

CW_CALLBACK CWFindAndLoadFile(CWPluginContext context, const char* filename, CWFileInfo* fileinfo) {
    if (IsVCSContext(context) || !ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    if (filename == NULL) {
        return cwErrInvalidParameter;
    }
    if (fileinfo == NULL) {
        return cwErrInvalidParameter;
    }

    return context->callbacks->cbFindAndLoadFile(context, filename, fileinfo);
}

static CWResult EnsureCachedAccessPaths(CWPluginContext context) {
    if (!context->accessPathList) {
        CWResult res = context->callbacks->cbCacheAccessPathList(context);
        if (res) {
            return res;
        }
        if (!context->accessPathList) {
            return cwErrRequestFailed;
        }
    }
    return cwNoErr;
}

CW_CALLBACK CWGetAccessPathListInfo(CWPluginContext context, CWAccessPathListInfo* pathListInfo) {
    if (!ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    if (pathListInfo == NULL) {
        return cwErrInvalidParameter;
    }
    if (context->apiVersion < 10) {
        return cwErrInvalidCallback;
    }
    CWResult res = EnsureCachedAccessPaths(context);
    if (res) {
        return res;
    }
    pathListInfo->systemPathCount = context->accessPathList->systemPathCount;
    pathListInfo->userPathCount = context->accessPathList->userPathCount;
    pathListInfo->alwaysSearchUserPaths = context->accessPathList->alwaysSearchUserPaths;
    pathListInfo->convertPaths = context->accessPathList->convertPaths;
    return cwNoErr;
}

CW_CALLBACK CWGetFileText(
    CWPluginContext context,
    const CWFileSpec* filespec,
    const char** text,
    SInt32* textLength,
    short* filedatatype
) {
    if (!ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    if (!filespec) {
        return cwErrInvalidParameter;
    }
    if (!text) {
        return cwErrInvalidParameter;
    }
    if (!textLength) {
        return cwErrInvalidParameter;
    }
    if (!filedatatype) {
        return cwErrInvalidParameter;
    }
    return context->callbacks->cbGetFileText(context, filespec, text, textLength, filedatatype);
}

CW_CALLBACK CWReleaseFileText(CWPluginContext context, const char* text) {
    if (!ValidateContext(context)) {
        return cwErrInvalidParameter;
    }

    if (!text) {
        return cwNoErr;
    }
    return context->callbacks->cbReleaseFileText(context, text);
}

CW_CALLBACK CWReportMessage(
    CWPluginContext context,
    const CWMessageRef* msgRef,
    const char* line1,
    const char* line2,
    short errorlevel,
    SInt32 errorNumber
) {
    if (!ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    return context->callbacks->cbReportMessage(
        context, msgRef, line1, line2, errorlevel, errorNumber
    );
}

CW_CALLBACK CWAlert(
    CWPluginContext context, const char* msg1, const char* msg2, const char* msg3, const char* msg4
) {
    if (!ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    return context->callbacks->cbAlert(context, msg1, msg2, msg3, msg4);
}

CW_CALLBACK CWShowStatus(CWPluginContext context, const char* line1, const char* line2) {
    if (!ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    return context->callbacks->cbShowStatus(context, line1, line2);
}

CW_CALLBACK CWUserBreak(CWPluginContext context) {
    if (!ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    return context->callbacks->cbUserBreak(context);
}

CW_CALLBACK CWGetNamedPreferences(
    CWPluginContext context, const char* prefsname, CWMemHandle* prefsdata
) {
    if (!ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    if (!prefsname) {
        return cwErrInvalidParameter;
    }
    if (!prefsdata) {
        return cwErrInvalidParameter;
    }
    return context->callbacks->cbGetNamedPreferences(context, prefsname, prefsdata);
}

CW_CALLBACK CWSetModDate(
    CWPluginContext context, const CWFileSpec* filespec, CWFileTime* moddate, Boolean isGenerated
) {
    if (IsVCSContext(context) || !ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    if (!filespec) {
        return cwErrInvalidParameter;
    }
    return context->callbacks->cbSetModDate(context, filespec, moddate, isGenerated);
}

CW_CALLBACK CWAddProjectEntry(
    CWPluginContext context,
    const CWFileSpec* fileSpec,
    Boolean isGenerated,
    const CWNewProjectEntryInfo* projectEntryInfo,
    SInt32* whichfile
) {
    if (IsVCSContext(context) || !ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    if (!fileSpec) {
        return cwErrInvalidParameter;
    }
    if (context->apiVersion < 8) {
        return cwErrRequestFailed;
    }
    return context->callbacks->cbAddProjectEntry(
        context, fileSpec, isGenerated, projectEntryInfo, whichfile
    );
}

CW_CALLBACK CWCreateNewTextDocument(CWPluginContext context, const CWNewTextDocumentInfo* docinfo) {
    if (!ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    if (!docinfo) {
        return cwErrInvalidParameter;
    }
    return context->callbacks->cbCreateNewTextDocument(context, docinfo);
}

CW_CALLBACK CWAllocMemHandle(
    CWPluginContext context, SInt32 size, Boolean useTempMemory, CWMemHandle* handle
) {
    if (!ValidateContext(context) && !ValidateInitTermContext(context)) {
        return cwErrInvalidParameter;
    }
    if (!handle) {
        return cwErrInvalidParameter;
    }
    return context->callbacks->cbAllocMemHandle(context, size, useTempMemory, handle);
}

CW_CALLBACK CWFreeMemHandle(CWPluginContext context, CWMemHandle handle) {
    if (!ValidateContext(context) && !ValidateInitTermContext(context)) {
        return cwErrInvalidParameter;
    }
    return context->callbacks->cbFreeMemHandle(context, handle);
}

CW_CALLBACK CWLockMemHandle(
    CWPluginContext context, CWMemHandle handle, Boolean moveHi, void** ptr
) {
    if (!ValidateContext(context) && !ValidateInitTermContext(context)) {
        return cwErrInvalidParameter;
    }
    if (!ptr) {
        return cwErrInvalidParameter;
    }
    return context->callbacks->cbLockMemHandle(context, handle, moveHi, ptr);
}

CW_CALLBACK CWUnlockMemHandle(CWPluginContext context, CWMemHandle handle) {
    if (!ValidateContext(context) && !ValidateInitTermContext(context)) {
        return cwErrInvalidParameter;
    }
    return context->callbacks->cbUnlockMemHandle(context, handle);
}

CW_CALLBACK CWDonePluginRequest(CWPluginContext, CWResult resultCode) {
    return resultCode;
}

CW_CALLBACK CWCheckoutLicense(
    CWPluginContext context,
    const char* featureName,
    const char* licenseVersion,
    SInt32 flags,
    void* reserved,
    SInt32* cookie
) {
    if (!ValidateContext(context) && !ValidateInitTermContext(context)) {
        return cwErrInvalidParameter;
    }
    return context->callbacks->cbCheckoutLicense(
        context, featureName, licenseVersion, flags, reserved, cookie
    );
}

CW_CALLBACK CWCheckinLicense(CWPluginContext context, SInt32 cookie) {
    if (!ValidateContext(context) && !ValidateInitTermContext(context)) {
        return cwErrInvalidParameter;
    }
    return context->callbacks->cbCheckinLicense(context, cookie);
}
}
