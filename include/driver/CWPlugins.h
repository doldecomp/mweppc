#ifndef DRIVER_CWPLUGINS_H
#define DRIVER_CWPLUGINS_H

#include "compiler/common.h"
#include "driver/Files.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

// (opaque to plugins: CWPluginsPrivate.h defines it)
typedef struct CWPluginPrivateContext CWPluginPrivateContext;
typedef struct CWPluginPrivateContext* CWPluginContext;

// What a static plugin describes itself with, and the driver asks it through (the plugin SDK's
// CWPlugins.h and DropInCompilerLinker.h, and the driver's own plugin records).

typedef UInt32 CWDataType;
#define CWFOURCHAR(a, b, c, d) \
    (((CWDataType)(a) << 24) | ((CWDataType)(b) << 16) | ((CWDataType)(c) << 8) | (CWDataType)(d))
typedef struct CWMemHandlePrivateStruct* CWMemHandle;
// (the SDK's, for a Mac FSSpec)
#define CWFileSpecNotEmpty(specPtr) ((specPtr)->name[0] != 0)
typedef SInt32 CWResult;

// A plugin entry point and a callback (the SDK's, for Win32)
#define CWPLUGIN_ENTRY(function_name) short(__stdcall function_name)
#define CW_CALLBACK CWResult __stdcall
#define CW_CALLBACK_PTR(function_name) CWResult(__stdcall function_name)

// The request every plugin gets (a compiler's and a linker's others are DropInCompilerLinker.h's)
enum {
    reqInitialize = -2,
    reqTerminate = -1,
    reqIdle = -100
};

enum {
    CWDROPINLINKERTYPE = 'Link',
    CWDROPINCOMPILERTYPE = 'Comp',
    CWDROPINPARSERTYPE = 'Pars'
};
#define CWDROPINVCSTYPE 'VCS '
// (a macro in 2.x, not one of the enumerators above)
#define CWDROPINDRIVERTYPE 'cldr'

enum {
    CWDROPINANYTYPE = '****'
};

enum {
    targetCPUAny = '****',
    targetOSAny = '****',
    Lang_Any = '****'
};

// (CompilerMapping.h: C's spelling, plain constants)
#define Lang_C_CPP 'c++ '
#define Lang_Pascal 'pasc'
#define Lang_Rez 'rez '
#define Lang_Java 'java'
#define Lang_MISC '\?\?\?\?' // ('????', escaped from the trigraphs)

// (CWPluginErrors.h; cwErrLastCommonError makes the enumerators 16-bit constants)
enum {
    // common errors for all plugins
    cwNoErr,
    cwErrUserCanceled,
    cwErrRequestFailed,
    cwErrInvalidParameter,
    cwErrInvalidCallback,
    cwErrInvalidMPCallback,
    cwErrOSError,
    cwErrOutOfMemory,
    cwErrFileNotFound,
    cwErrUnknownFile,
    cwErrSilent,
    cwErrCantSetAttribute,
    cwErrStringBufferOverflow,
    cwErrDirectoryNotFound,
    cwErrLastCommonError = 512,

    // compiler/linker errors
    cwErrUnknownSegment,
    cwErrSBMNotFound,
    cwErrObjectFileNotStored,
    cwErrLicenseCheckFailed,
    cwErrFileSpecNotSpecified,
    cwErrFileSpecInvalid,
    cwErrLastCompilerLinkerError = 1024
};

// message types, the errorlevel of the message callbacks
enum {
    messagetypeInfo,
    messagetypeWarning,
    messagetypeError
};

// DropInFlags.dropinflags (the name is 2.6's own, from CLToolExec_ExecuteLinker's assert
// "dropinflags & isExecutableTool")
enum {
    isExecutableTool = 1
};

// a compiler's DropInFlags.dropinflags
enum {
    kGeneratescode = 1L << 31,
    kGeneratesrsrcs = 1L << 30,
    kCanpreprocess = 1L << 29,
    kCanprecompile = 1L << 28,
    kIspascal = 1L << 27,
    kCanimport = 1L << 26,
    kCandisassemble = 1L << 25,
    kCompAllowDupFileNames = 1L << 23,
    kCompMultiTargAware = 1L << 22,
    kCompUsesTargetStorage = 1L << 20,
    kCompEmitsOwnBrSymbols = 1L << 19,
    kCompAlwaysReload = 1L << 18,
    kCompRequiresProjectBuildStartedMsg = 1L << 17,
    kCompRequiresTargetBuildStartedMsg = 1L << 16,
    kCompRequiresSubProjectBuildStartedMsg = 1L << 15,
    kCompRequiresFileListBuildStartedMsg = 1L << 14
};

// a linker's DropInFlags.dropinflags
enum {
    cantDisassemble = 1L << 31,
    isPostLinker = 1L << 30,
    linkAllowDupFileNames = 1L << 29,
    linkMultiTargAware = 1L << 28,
    isPreLinker = 1L << 27,
    linkerUsesTargetStorage = 1L << 26,
    linkerUnmangles = 1L << 25,
    magicCapLinker = 1L << 24,
    linkAlwaysReload = 1L << 23,
    linkRequiresProjectBuildStartedMsg = 1L << 22,
    linkRequiresTargetBuildStartedMsg = 1L << 21,
    linkRequiresSubProjectBuildStartedMsg = 1L << 20,
    linkRequiresFileListBuildStartedMsg = 1L << 19,
    linkRequiresTargetLinkStartedMsg = 1L << 18,
    linkerWantsPreRunRequest = 1L << 17
};

#define kCurrentDropInFlagsVersion 2
#define kCurrentCWPanelListVersion 1
#define kCurrentCWFamilyListVersion 1
#define kCurrentCWExtMapListVersion 1
#define kCurrentCWTargetListVersion 1
#define kCurrentCWHelpInfoVersion 1

#pragma pack(push, 2)
// (the version 1 resource: DropInFlags without newestAPIVersion)
typedef struct DropInFlagsV1 {
    SInt16 rsrcversion;
    CWDataType dropintype;
    UInt16 earliestCompatibleAPIVersion;
    UInt32 dropinflags;
    SInt32 edit_language;
} DropInFlagsV1;

typedef struct DropInFlags {
    SInt16 rsrcversion;
    CWDataType dropintype;
    UInt16 earliestCompatibleAPIVersion;
    UInt32 dropinflags;
    // (2.x: signed, not the SDK's CWDataType)
    SInt32 edit_language;
    UInt16 newestAPIVersion;
} DropInFlags;

// CWExtensionMapping.flags (the SDK's CompilerMapping.h)
enum {
    kPrecompile = 1L << 31,
    kLaunchable = 1L << 30,
    kRsrcfile = 1L << 29,
    kIgnored = 1L << 28
};

typedef struct CWExtensionMapping {
    CWDataType type;
    char extension[32];
    UInt32 flags;
} CWExtensionMapping;

typedef struct CWExtMapList {
    SInt16 version;
    SInt16 nMappings;
    CWExtensionMapping* mappings;
} CWExtMapList;

typedef struct CWPanelList {
    SInt16 version;
    SInt16 count;
    const char** names;
} CWPanelList;

typedef struct CWHelpInfo {
    SInt16 version;
    const char* helpFileName;
} CWHelpInfo;

typedef struct CWObjectFlags {
    SInt16 version;
    UInt32 flags;
    const char* objFileExt;
    const char* brsFileExt;
    const char* ppFileExt;
    const char* disFileExt;
    const char* depFileExt;
    const char* pchFileExt;
    CWDataType objFileCreator;
    CWDataType objFileType;
    CWDataType brsFileCreator;
    CWDataType brsFileType;
    CWDataType ppFileCreator;
    CWDataType ppFileType;
    CWDataType disFileCreator;
    CWDataType disFileType;
    CWDataType depFileCreator;
    CWDataType depFileType;
} CWObjectFlags;

typedef struct CWAccessPathListInfo {
    SInt32 systemPathCount;
    SInt32 userPathCount;
    Boolean alwaysSearchUserPaths;
    Boolean convertPaths;
} CWAccessPathListInfo;
typedef struct CWNewTextDocumentInfo {
    const char* documentname;
    CWMemHandle text;
    Boolean markDirty;
} CWNewTextDocumentInfo;
typedef struct CWNewProjectEntryInfo {
    SInt32 position;
    SInt32 segment;
    SInt32 overlayGroup;
    SInt32 overlay;
    const char* groupPath;
    Boolean mergeintooutput;
    Boolean weakimport;
    Boolean initbefore;
} CWNewProjectEntryInfo;

// CWFileInfo.dependencyType
typedef enum CWDependencyType {
    cwNoDependency,
    cwNormalDependency,
    cwInterfaceDependency
} CWDependencyType;

// CWFileInfo.isdependentoffile
#define kCurrentCompiledFile -1L

// CWFileInfo.filedatatype
enum {
    cwFileTypeUnknown,
    cwFileTypeText,
    cwFileTypePrecompiledHeader
};

// (the SDK's Mac OS types: 2.x's CWFileSpec is an FSSpec)
typedef char CWFileName[32];
typedef UInt32 CWFileTime;

// What CWFindAndLoadFile returns
typedef struct CWFileInfo {
    Boolean fullsearch;
    char dependencyType;
    SInt32 isdependentoffile;
    Boolean suppressload;
    Boolean padding;
    const char* filedata;
    SInt32 filedatalength;
    short filedatatype;
    short fileID;
    CWFileSpec filespec;
    Boolean alreadyincluded;
    Boolean recordbrowseinfo;
} CWFileInfo;

typedef struct CWProjectSegmentInfo {
    char name[32];
    short attributes;
} CWProjectSegmentInfo;

typedef struct CWAddr64 {
    SInt32 lo;
    SInt32 hi;
} CWAddr64;

typedef struct CWOverlay1GroupInfo {
    char name[256];
    CWAddr64 address;
    SInt32 numoverlays;
} CWOverlay1GroupInfo;

typedef struct CWOverlay1Info {
    char name[256];
    SInt32 numfiles;
} CWOverlay1Info;

typedef struct CWOverlay1FileInfo {
    SInt32 whichfile;
} CWOverlay1FileInfo;

// Where a message points in a source file
typedef struct CWMessageRef {
    CWFileSpec sourcefile;
    SInt32 linenumber;
    short tokenoffset;
    short tokenlength;
    SInt32 selectionoffset;
    SInt32 selectionlength;
} CWMessageRef;

// What CWResolveRelativePath resolves
typedef struct CWRelativePath {
    short version;
    unsigned char pathType;
    unsigned char pathFormat;
    char userDefinedTree[256];
    char pathString[512];
} CWRelativePath;

// What CWGetFileInfo returns (with the Mac OS host's filetype and filecreator)
typedef struct CWProjectFileInfo {
    CWFileSpec filespec;
    CWFileTime moddate;
    short segment;
    Boolean hasobjectcode;
    Boolean hasresources;
    Boolean isresourcefile;
    Boolean weakimport;
    Boolean initbefore;
    Boolean gendebug;
    CWFileTime objmoddate;
    CWFileName dropinname;
    short fileID;
    Boolean recordbrowseinfo;
    Boolean reserved;
    OSType filetype;
    OSType filecreator;
    Boolean hasunitdata;
    Boolean mergeintooutput;
    UInt32 unitdatadependencytag;
} CWProjectFileInfo;

// NOTE: invented names: cwAccessPathFlagUser, cwAccessPathFlagKeepSystem
// CWNewAccessPathInfo.type's bits: a user path (cwUserPath), and a system path that stays one when
// the paths are swapped (CLParserCallbacks_v1's UCBParserSwapAccessPaths)
enum {
    cwAccessPathFlagUser = 1,
    cwAccessPathFlagKeepSystem = 2
};
// (the SDK's enum CWAccessPathType; 2.x's member is a byte)
enum {
    cwSystemPath,
    cwUserPath
};
typedef UInt8 CWAccessPathType;
typedef struct CWNewAccessPathInfo {
    CWFileSpec pathSpec;
    SInt32 position;
    Boolean recursive;
    CWAccessPathType type;
} CWNewAccessPathInfo;

typedef struct OSFileTypeMapping {
    OSType mactype;
    const char* magic;
    char length;
    char executable;
    const char* mimetype;
} OSFileTypeMapping;

typedef struct OSFileTypeMappingList {
    SInt16 numMappings;
    const struct OSFileTypeMapping* mappings;
} OSFileTypeMappingList;

// A plugin's version (GetVersionInfo)
typedef struct VersionInfo {
    UInt16 major;
    UInt16 minor;
    UInt16 patch;
    UInt16 build;
} VersionInfo;

// A plugin's supported CPUs and operating systems (GetTargetList)
typedef struct CWTargetList {
    SInt16 version;
    SInt16 cpuCount;
    SInt32* cpus; // (2.x: signed, not the SDK's CWDataType)
    SInt16 osCount;
    SInt32* oss;
} CWTargetList;

// What the driver tells the parser about each plugin (Plugins_GetPluginList)
typedef struct CLPluginInfo {
    CWDataType plugintype;
    CWDataType language;
    SInt32 dropinflags;
    char* version;
    Boolean storeCommandLine;
} CLPluginInfo;
#pragma pack(pop)

struct CWFamilyList;
struct CWUnmangleInfo;
struct CWCompilerBrSymbolInfo;

typedef struct BasePluginCallbacks {
    CWPLUGIN_ENTRY (*main)(struct CWPluginPrivateContext* context);
    CWPLUGIN_ENTRY (*GetDropInFlags)(const DropInFlags** flags, SInt32* flagsSize);
    CWPLUGIN_ENTRY (*GetDisplayName)(const char** displayName);
    CWPLUGIN_ENTRY (*GetDropInName)(const char** dropInName);
    CWPLUGIN_ENTRY (*GetPanelList)(const CWPanelList** panelList);
    CWPLUGIN_ENTRY (*GetFamilyList)(const struct CWFamilyList** familyList);
    CWPLUGIN_ENTRY (*GetHelpInfo)(const CWHelpInfo** helpInfo);
    CWPLUGIN_ENTRY (*GetVersionInfo)(const VersionInfo** versionInfo);
    CWPLUGIN_ENTRY (*GetFileTypeMappings)(const OSFileTypeMappingList** mappingList);
} BasePluginCallbacks;

typedef struct CompilerLinkerPluginCallbacks {
    CWPLUGIN_ENTRY (*GetTargetList)(const struct CWTargetList** targetList);
    CWPLUGIN_ENTRY (*GetDefaultMappingList)(const CWExtMapList** mappings);
    CWPLUGIN_ENTRY (*Unmangle)(struct CWUnmangleInfo* unmangleInfo);
    CWPLUGIN_ENTRY (*BrSymbolEntryPoint)(struct CWCompilerBrSymbolInfo* symbolInfo);
    CWPLUGIN_ENTRY (*GetObjectFlags)(const CWObjectFlags** objectFlags);
    CWPLUGIN_ENTRY (*WriteObjectFile)(
        const CWFileSpec* srcfss,
        const CWFileSpec* outfss,
        CWDataType creator,
        CWDataType type,
        Handle data
    );
} CompilerLinkerPluginCallbacks;

// The shell's callbacks a parser plugin calls (2.6's has a tenth entry it leaves unset)
struct CWParserCallbacks {
    CW_CALLBACK_PTR (*cbParserAddAccessPath)(CWPluginContext, const CWNewAccessPathInfo*);
    CW_CALLBACK_PTR (*cbParserSwapAccessPaths)(CWPluginContext);
    CW_CALLBACK_PTR (*cbParserSetNamedPreferences)(CWPluginContext, const char*, Handle);
    CW_CALLBACK_PTR (*cbParserSetFileOutputName)(CWPluginContext, SInt32, short, const char*);
    CW_CALLBACK_PTR (*cbParserSetOutputFileDirectory)(CWPluginContext, const CWFileSpec*);
    CW_CALLBACK_PTR (*cbParserAddOverlay1Group)(
        CWPluginContext, const char*, const CWAddr64*, SInt32*
    );
    CW_CALLBACK_PTR (*cbParserAddOverlay1)(CWPluginContext, const char*, SInt32, SInt32*);
    CW_CALLBACK_PTR (*cbParserAddSegment)(CWPluginContext, const char*, short, SInt32*);
    CW_CALLBACK_PTR (*cbParserSetSegment)(CWPluginContext, SInt32, const char*, short);
    void* reserved;
};

typedef struct ParserPluginCallbacks {
    CWPLUGIN_ENTRY (*SupportsPlugin)(
        const CLPluginInfo* pluginfo, CWDataType cpu, CWDataType os, Boolean* isSupported
    );
    CWPLUGIN_ENTRY (*SupportsPanels)(int numPanels, const char** panelNames, Boolean* isSupported);
} ParserPluginCallbacks;

typedef struct CWIDEInfo {
    unsigned short majorVersion;
    unsigned short minorVersion;
    unsigned short bugFixVersion;
    unsigned short buildVersion;
    unsigned short dropinAPIVersion;
} CWIDEInfo;

#ifdef __cplusplus
}
#endif

#endif
