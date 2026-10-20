// The plugin library's compiler and linker callbacks (Wii's module of the name). The binary has
// the ones its plugins call; the others (CWGetBuildSequenceNumber, CWSetTargetInfo,
// CWLoadObjectData, ...) have no data, and the linker dropped them. Built -inline off: its implicit
// copy-assignments (??4CWBrowseOptions, ??4CWTargetInfo_V8) are out of line.
#include "driver/CWPlugins.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/DropInCompilerLinker.h"
#include "driver/Files.h"
#include <string.h>

// (prefixed: CWParserPluginsPrivate.cpp has a static GetContext too, and function names are global)
#define GetContext DropInCompilerLinkerPrivate_GetContext

extern "C" {

static CWCompilerLinkerContext* GetContext(CWPluginContext context) {
    if (context &&
        (context->pluginType == CWDROPINCOMPILERTYPE ||
         context->pluginType == CWDROPINLINKERTYPE)) {
        return static_cast<CWCompilerLinkerContext*>(context);
    } else {
        return 0;
    }
}

CW_CALLBACK CWIsPrecompiling(CWPluginContext context, Boolean* isPrecompiling) {
    CWCompilerLinkerContext* cl;
    if (!isPrecompiling) {
        return cwErrInvalidParameter;
    }
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    *isPrecompiling = cl->precompile;
    return cwNoErr;
}

CW_CALLBACK CWIsAutoPrecompiling(CWPluginContext context, Boolean* isAutoPrecompiling) {
    CWCompilerLinkerContext* cl;
    if (!isAutoPrecompiling) {
        return cwErrInvalidParameter;
    }
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    *isAutoPrecompiling = cl->autoprecompile;
    return cwNoErr;
}

CW_CALLBACK CWIsPreprocessing(CWPluginContext context, Boolean* isPreprocessing) {
    CWCompilerLinkerContext* cl;
    if (!isPreprocessing) {
        return cwErrInvalidParameter;
    }
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    *isPreprocessing = cl->preprocess;
    return cwNoErr;
}

CW_CALLBACK CWIsGeneratingDebugInfo(CWPluginContext context, Boolean* isGenerating) {
    CWCompilerLinkerContext* cl;
    if (!isGenerating) {
        return cwErrInvalidParameter;
    }
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    *isGenerating = cl->debuginfo;
    return cwNoErr;
}

CW_CALLBACK CWIsCachingPrecompiledHeaders(CWPluginContext context, Boolean* isCaching) {
    CWCompilerLinkerContext* cl;
    if (!isCaching) {
        return cwErrInvalidParameter;
    }
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    *isCaching = cl->cachingPCH;
    return cwNoErr;
}

CW_CALLBACK CWGetBrowseOptions(CWPluginContext context, CWBrowseOptions* browseOptions) {
    CWCompilerLinkerContext* cl;
    if (!browseOptions) {
        return cwErrInvalidParameter;
    }
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    *browseOptions = cl->browseoptions;
    return cwNoErr;
}

CW_CALLBACK CWGetTargetInfo(CWPluginContext context, CWTargetInfo* targetInfo) {
    CWCompilerLinkerContext* cl;
    if (!targetInfo) {
        return cwErrInvalidParameter;
    }
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    if (cl->apiVersion >= 10) {
        *targetInfo = *cl->targetinfo;
    } else if (cl->apiVersion >= 8) {
        // (one copy of the version 8 layout)
        *(CWTargetInfo_V8*)targetInfo = *(CWTargetInfo_V8*)cl->targetinfo;
    } else {
        memset(targetInfo, 0, sizeof(CWTargetInfo));
        if (CWFileSpecNotEmpty(&cl->targetinfo_V7.outfile)) {
            targetInfo->outputType = linkOutputFile;
        } else {
            targetInfo->outputType = linkOutputNone;
        }
        targetInfo->outfile = cl->targetinfo_V7.outfile;
        targetInfo->symfile = cl->targetinfo_V7.symfile;
        targetInfo->runfile = cl->targetinfo_V7.outfile;
        targetInfo->linkType = cl->targetinfo_V7.linkType;
        targetInfo->canRun = cl->targetinfo_V7.canRun;
        targetInfo->canDebug = cl->targetinfo_V7.canDebug;
        targetInfo->debuggerCreator = cl->targetinfo_V7.debuggerCreator;
        targetInfo->runHelperCreator = cl->targetinfo_V7.runHelperCreator;
    }
    return cwNoErr;
}

CW_CALLBACK CWGetMainFileNumber(CWPluginContext context, SInt32* fileNumber) {
    CWCompilerLinkerContext* cl;
    if (!fileNumber) {
        return cwErrInvalidParameter;
    }
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    *fileNumber = cl->whichfile;
    return cwNoErr;
}

CW_CALLBACK CWGetMainFileID(CWPluginContext context, short* fileID) {
    CWCompilerLinkerContext* cl;
    if (!fileID) {
        return cwErrInvalidParameter;
    }
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    *fileID = cl->fileID;
    return cwNoErr;
}

CW_CALLBACK CWGetMainFileSpec(CWPluginContext context, CWFileSpec* fileSpec) {
    CWCompilerLinkerContext* cl;
    if (!fileSpec) {
        return cwErrInvalidParameter;
    }
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    *fileSpec = cl->sourcefile;
    return cwNoErr;
}

CW_CALLBACK CWGetMainFileText(CWPluginContext context, const char** text, SInt32* textLength) {
    CWCompilerLinkerContext* cl;
    if (!text) {
        return cwErrInvalidParameter;
    }
    if (!textLength) {
        return cwErrInvalidParameter;
    }
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    *text = cl->sourcetext;
    *textLength = cl->sourcetextsize;
    return cwNoErr;
}

CW_CALLBACK CWStoreObjectData(CWPluginContext context, SInt32 whichfile, CWObjectData* object) {
    CWCompilerLinkerContext* cl;
    if (!object) {
        return cwErrInvalidParameter;
    }
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    return cl->callbacks->cbStoreObjectData(cl, whichfile, object);
}

CW_CALLBACK CWDisplayLines(CWPluginContext context, SInt32 nlines) {
    CWCompilerLinkerContext* cl;
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    return cl->callbacks->cbDisplayLines(cl, nlines);
}

CW_CALLBACK CWCachePrecompiledHeader(
    CWPluginContext context, const CWFileSpec* filespec, CWMemHandle pchhandle
) {
    CWCompilerLinkerContext* cl;
    if (!filespec) {
        return cwErrInvalidParameter;
    }
    if (!pchhandle) {
        return cwErrInvalidParameter;
    }
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    return cl->callbacks->cbCachePrecompiledHeader(cl, filespec, pchhandle);
}

CW_CALLBACK CWGetPrecompiledHeaderSpec(
    CWPluginContext context, CWFileSpec* pchspec, const char* target
) {
    CWCompilerLinkerContext* cl;
    if (!pchspec) {
        return cwErrInvalidParameter;
    }
    if (!(cl = GetContext(context))) {
        return cwErrInvalidCallback;
    }
    return cl->callbacks->cbGetPrecompiledHeaderSpec(cl, pchspec, target);
}
}
