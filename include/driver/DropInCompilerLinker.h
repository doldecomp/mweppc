#ifndef DRIVER_DROPINCOMPILERLINKER_H
#define DRIVER_DROPINCOMPILERLINKER_H

#include "compiler/common.h"
#include "driver/CWPlugins.h"
#include "driver/Files.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DROPINCOMPILERLINKERAPIVERSION_8 8
#define DROPINCOMPILERLINKERAPIVERSION_12 12

enum {
    reqCompile = 0,
    reqMakeParse,
    reqCompDisassemble,
    reqCheckSyntax,
    reqPreprocessForDebugger
};

enum {
    reqLink = 0,
    reqDisassemble,
    reqTargetInfo,
    reqPreRun
};

enum {
    exelinkageFlat,
    exelinkageSegmented,
    exelinkageOverlay1
};

enum {
    linkOutputNone,
    linkOutputFile,
    linkOutputDirectory
};

typedef struct CWBrowseOptions {
    UInt8 recordClasses;
    UInt8 recordEnums;
    UInt8 recordMacros;
    UInt8 recordTypedefs;
    UInt8 recordConstants;
    UInt8 recordTemplates;
    UInt8 recordUndefinedFuncs;
    SInt32 reserved1;
    SInt32 reserved2;
} CWBrowseOptions;
#pragma pack(push, 2)
typedef UInt32 CWDependencyTag;

typedef struct CWObjectData {
    CWMemHandle objectdata;
    CWMemHandle browsedata;
    SInt32 reserved1;
    SInt32 codesize;
    SInt32 udatasize;
    SInt32 idatasize;
    SInt32 compiledlines;
    Boolean interfaceChanged;
    SInt32 reserved2;
    void* compilecontext;
    struct CWDependencyInfo* dependencies;
    short dependencyCount;
    CWFileSpec* objectfile;
} CWObjectData;
typedef struct CWTargetInfo_V8 {
    short outputType;
    CWFileSpec outfile;
    CWFileSpec symfile;
    CWFileSpec runfile;
    SInt16 linkType;
    UInt8 canRun;
    UInt8 canDebug;
    UInt32 targetCPU;
    UInt32 targetOS;
    UInt32 outfileCreator;
    UInt32 outfileType;
    UInt32 debuggerCreator;
    UInt32 runHelperCreator;
} CWTargetInfo_V8;

typedef struct CWTargetInfo {
    short outputType;
    CWFileSpec outfile;
    CWFileSpec symfile;
    CWFileSpec runfile;
    SInt16 linkType;
    UInt8 canRun;
    UInt8 canDebug;
    UInt32 targetCPU;
    UInt32 targetOS;
    UInt32 outfileCreator;
    UInt32 outfileType;
    UInt32 debuggerCreator;
    UInt32 runHelperCreator;
    CWFileSpec linkAgainstFile;
} CWTargetInfo;
typedef struct CWTargetInfoV7 {
    CWFileSpec outfile;
    CWFileSpec symfile;
    short linkType;
    Boolean canRun;
    Boolean canDebug;
    Boolean useRunHelperApp;
    char reserved1;
    CWDataType debuggerCreator;
    CWDataType runHelperCreator;
    SInt32 reserved2[2];
} CWTargetInfoV7;
typedef struct CWEnvVarInfo {
    char* name;
    char* value;
} CWEnvVarInfo;
#pragma pack(pop)

typedef struct CWCompilerLinkerCallbacks {
    CW_CALLBACK_PTR (*cbCachePrecompiledHeader)(CWPluginContext, const CWFileSpec*, CWMemHandle);
    CW_CALLBACK_PTR (*cbLoadObjectData)(CWPluginContext, SInt32, CWMemHandle*);
    CW_CALLBACK_PTR (*cbStoreObjectData)(CWPluginContext, SInt32, CWObjectData*);
    CW_CALLBACK_PTR (*cbFreeObjectData)(CWPluginContext, SInt32, CWMemHandle);
    CW_CALLBACK_PTR (*cbDisplayLines)(CWPluginContext, SInt32);
    CW_CALLBACK_PTR (*cbBeginSubCompile)(CWPluginContext, SInt32, CWPluginContext*);
    CW_CALLBACK_PTR (*cbEndSubCompile)(CWPluginContext);
    CW_CALLBACK_PTR (*cbGetPrecompiledHeaderSpec)(CWPluginContext, CWFileSpec*, const char*);
    CW_CALLBACK_PTR (*cbGetResourceFile)(CWPluginContext, CWFileSpec*);
    CW_CALLBACK_PTR (*cbPutResourceFile)(CWPluginContext, const char*, const char*, CWFileSpec*);
    CW_CALLBACK_PTR (*cbLookUpUnit)(CWPluginContext, const char*, Boolean, const void**, SInt32*);
    CW_CALLBACK_PTR (*cbSBMfiles)(CWPluginContext, short);
    CW_CALLBACK_PTR (*cbStoreUnit)(CWPluginContext, const char*, CWMemHandle, CWDependencyTag);
    CW_CALLBACK_PTR (*cbReleaseUnit)(CWPluginContext, void*);
    CW_CALLBACK_PTR (*cbUnitNameToFileName)(CWPluginContext, const char*, char*);
    CW_CALLBACK_PTR (*cbOSErrorMessage)(CWPluginContext, const char*, OSErr);
    CW_CALLBACK_PTR (*cbOSAlert)(CWPluginContext, const char*, OSErr);
    CW_CALLBACK_PTR (*cbGetModifiedFiles)(CWPluginContext, SInt32*, const SInt32**);
    CW_CALLBACK_PTR (*cbGetSuggestedObjectFileSpec)(CWPluginContext, SInt32, CWFileSpec*);
    CW_CALLBACK_PTR (*cbGetStoredObjectFileSpec)(CWPluginContext, SInt32, CWFileSpec*);
    CW_CALLBACK_PTR (*cbGetRuntimeSettings)(CWPluginContext);
} CWCompilerLinkerCallbacks;

extern CW_CALLBACK CWGetTargetInfo(CWPluginContext context, CWTargetInfo* targetInfo);
extern CW_CALLBACK CWStoreObjectData(
    CWPluginContext context, SInt32 whichfile, CWObjectData* object
);
extern CW_CALLBACK CWDisplayLines(CWPluginContext context, SInt32 nlines);
extern CW_CALLBACK CWCachePrecompiledHeader(
    CWPluginContext context, const CWFileSpec* filespec, CWMemHandle pchhandle
);
extern CW_CALLBACK CWIsPrecompiling(CWPluginContext context, Boolean* isPrecompiling);
extern CW_CALLBACK CWIsAutoPrecompiling(CWPluginContext context, Boolean* isAutoPrecompiling);
extern CW_CALLBACK CWIsPreprocessing(CWPluginContext context, Boolean* isPreprocessing);
extern CW_CALLBACK CWIsGeneratingDebugInfo(CWPluginContext context, Boolean* isGenerating);
extern CW_CALLBACK CWIsCachingPrecompiledHeaders(CWPluginContext context, Boolean* isCaching);
extern CW_CALLBACK CWGetBrowseOptions(CWPluginContext context, CWBrowseOptions* browseOptions);
extern CW_CALLBACK CWGetMainFileNumber(CWPluginContext context, SInt32* fileNumber);
extern CW_CALLBACK CWGetMainFileID(CWPluginContext context, short* fileID);
extern CW_CALLBACK CWGetMainFileSpec(CWPluginContext context, CWFileSpec* fileSpec);
extern CW_CALLBACK CWGetMainFileText(
    CWPluginContext context, const char** text, SInt32* textLength
);
extern CW_CALLBACK CWGetPrecompiledHeaderSpec(
    CWPluginContext context, CWFileSpec* pchspec, const char* target
);

#ifdef __cplusplus
}
#endif

#endif
