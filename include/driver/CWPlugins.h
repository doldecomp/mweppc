#ifndef DRIVER_CWPLUGINS_H
#define DRIVER_CWPLUGINS_H

#include "compiler/common.h"
#include "driver/Files.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CWPluginPrivateContext CWPluginPrivateContext;
typedef struct CWPluginPrivateContext* CWPluginContext;

typedef UInt32 CWDataType;
#define CWFOURCHAR(a, b, c, d) \
    (((CWDataType)(a) << 24) | ((CWDataType)(b) << 16) | ((CWDataType)(c) << 8) | (CWDataType)(d))
typedef struct CWMemHandlePrivateStruct* CWMemHandle;
#define CWFileSpecNotEmpty(specPtr) ((specPtr)->name[0] != 0)
typedef SInt32 CWResult;

#define CWPLUGIN_ENTRY(function_name) short(__stdcall function_name)
#define CW_CALLBACK CWResult __stdcall
#define CW_CALLBACK_PTR(function_name) CWResult(__stdcall function_name)

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
#define CWDROPINDRIVERTYPE 'cldr'

enum {
    CWDROPINANYTYPE = '****'
};

enum {
    targetCPUAny = '****',
    targetOSAny = '****',
    Lang_Any = '****'
};

#define Lang_C_CPP 'c++ '
#define Lang_Pascal 'pasc'
#define Lang_Rez 'rez '
#define Lang_Java 'java'
#define Lang_MISC '\?\?\?\?'

enum {
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

    cwErrUnknownSegment,
    cwErrSBMNotFound,
    cwErrObjectFileNotStored,
    cwErrLicenseCheckFailed,
    cwErrFileSpecNotSpecified,
    cwErrFileSpecInvalid,
    cwErrLastCompilerLinkerError = 1024
};

enum {
    messagetypeInfo,
    messagetypeWarning,
    messagetypeError
};

enum {
    isExecutableTool = 1
};

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
    SInt32 edit_language;
    UInt16 newestAPIVersion;
} DropInFlags;

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

typedef enum CWDependencyType {
    cwNoDependency,
    cwNormalDependency,
    cwInterfaceDependency
} CWDependencyType;

#define kCurrentCompiledFile -1L

enum {
    cwFileTypeUnknown,
    cwFileTypeText,
    cwFileTypePrecompiledHeader
};

typedef char CWFileName[32];
typedef UInt32 CWFileTime;

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

typedef struct CWMessageRef {
    CWFileSpec sourcefile;
    SInt32 linenumber;
    short tokenoffset;
    short tokenlength;
    SInt32 selectionoffset;
    SInt32 selectionlength;
} CWMessageRef;

typedef struct CWRelativePath {
    short version;
    unsigned char pathType;
    unsigned char pathFormat;
    char userDefinedTree[256];
    char pathString[512];
} CWRelativePath;

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

enum {
    cwAccessPathFlagUser = 1,
    cwAccessPathFlagKeepSystem = 2
};
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

typedef struct VersionInfo {
    UInt16 major;
    UInt16 minor;
    UInt16 patch;
    UInt16 build;
} VersionInfo;

typedef struct CWTargetList {
    SInt16 version;
    SInt16 cpuCount;
    SInt32* cpus;
    SInt16 osCount;
    SInt32* oss;
} CWTargetList;

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
