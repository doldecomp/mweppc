#ifndef DRIVER_DROPINCOMPILERLINKER_H
#define DRIVER_DROPINCOMPILERLINKER_H

#include "compiler/common.h"
#include "driver/CWPlugins.h"
#include "driver/Files.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

// The plugin SDK's DropInCompilerLinker.h: what a compiler or linker plugin exchanges with the
// shell, and the callbacks it calls there (DropInCompilerLinkerPrivate.cpp).

// the plugin API versions (DropInFlags.earliestCompatibleAPIVersion, newestAPIVersion)
#define DROPINCOMPILERLINKERAPIVERSION_8 8
#define DROPINCOMPILERLINKERAPIVERSION_12 12

// a compiler's requests
enum {
    reqCompile = 0,
    reqMakeParse,
    reqCompDisassemble,
    reqCheckSyntax,
    reqPreprocessForDebugger
};

// a linker's requests
enum {
    reqLink = 0,
    reqDisassemble,
    reqTargetInfo,
    reqPreRun
};

// CWTargetInfo.linkType
enum {
    exelinkageFlat,
    exelinkageSegmented,
    exelinkageOverlay1
};

// CWTargetInfo.outputType
enum {
    linkOutputNone,
    linkOutputFile,
    linkOutputDirectory
};

// The SDK's CWBrowseOptions: seven Booleans, then two longs at 8 and 12 (default alignment).
// CWGetBrowseOptions copies one with its implicit operator=.
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
// The SDK's CWObjectData: what CWStoreObjectData gives the shell (2-byte packed, 0x30 bytes).
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
// CWTargetInfo as plugin API versions 8 and 9 have it (no linkAgainstFile): the plugin library
// copies one with an implicit operator= of its own, so the SDK has it as a type of its own
typedef struct CWTargetInfo_V8 { // NOTE: invented name
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
// The SDK's CWTargetInfo, ending in linkAgainstFile (C++ units copy it with its implicit
// operator=: the members, then FSSpec's for the last one)

typedef struct CWTargetInfo {
    short outputType; // 0x00
    CWFileSpec outfile; // 0x02
    CWFileSpec symfile; // 0x48
    CWFileSpec runfile; // 0x8e
    SInt16 linkType; // 0xd4
    UInt8 canRun; // 0xd6
    UInt8 canDebug; // 0xd7
    UInt32 targetCPU; // 0xd8
    UInt32 targetOS; // 0xdc
    UInt32 outfileCreator; // 0xe0
    UInt32 outfileType; // 0xe4
    UInt32 debuggerCreator; // 0xe8
    UInt32 runHelperCreator; // 0xec
    CWFileSpec linkAgainstFile; // 0xf0
} CWTargetInfo;
// The target info of plugin API versions before 8 (a compiler/linker context keeps one)
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

// The shell's callbacks a compiler or linker plugin calls (2.6's has 21 entries: none of the Mac
// SDK's framework callbacks)
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
