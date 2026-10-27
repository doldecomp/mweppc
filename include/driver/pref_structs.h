#ifndef DRIVER_PREF_STRUCTS_H
#define DRIVER_PREF_STRUCTS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
typedef struct PCmdLine {
    SInt16 version;
    SInt16 state;
    SInt16 stages;
    SInt16 toDisk;
    SInt16 outNameOwner;
    Boolean dryRun;
    Boolean debugInfo;
    SInt16 verbose;
    Boolean showLines;
    Boolean timeWorking;
    Boolean noWarnings;
    Boolean warningsAreErrors;
    UInt16 maxErrors;
    UInt16 maxWarnings;
    SInt16 msgStyle;
    Boolean noWrapOutput;
    Boolean stderr2stdout;
    Boolean noCmdLineWarnings;
} PCmdLine;

enum {
    OptsCmdLineState_Unset = 0,
    OptsCmdLineState_NoAction = 1,
    OptsCmdLineState_Compile = 2,
    OptsCmdLineState_CompileAndLink = 3
};

enum {
    CmdLineStageMask_Pp = 1,
    CmdLineStageMask_Cg = 2,
    CmdLineStageMask_Ds = 4,
    CmdLineStageMask_Dp = 8
};

enum {
    CmdLineStage_Cg = 1,
    CmdLineStage_Pp = 2,
    CmdLineStage_Ds = 3
};

enum {
    OptsCompilerSbmState_Normal = 0,
    OptsCompilerSbmState_Rebuild = 1,
    OptsCompilerSbmState_LookUpOnly = 2,
    OptsCompilerSbmState_Clean = 3
};

typedef struct PCmdLineCompiler {
    SInt16 version;
    Boolean noSysPath;
    Boolean noFail;
    SInt16 includeSearch;
    char linkerName[64];
    char objFileExt[15];
    char brsFileExt[15];
    char ppFileExt[15];
    char disFileExt[15];
    char depFileExt[15];
    char pchFileExt[15];
    UInt32 objFileCreator;
    UInt32 objFileType;
    UInt32 brsFileCreator;
    UInt32 brsFileType;
    UInt32 ppFileCreator;
    UInt32 ppFileType;
    UInt32 disFileCreator;
    UInt32 disFileType;
    UInt32 depFileCreator;
    UInt32 depFileType;
    Boolean compileIgnored;
    Boolean relPathInOutputDir;
    char browserEnabled;
    Boolean depsOnlyUserFiles;
    char outMakefile[256];
    SInt8 forcePrecompile;
    Boolean ignoreMissingFiles;
    Boolean printHeaderNames;
    SInt8 sbmState;
    char sbmPath[256];
    Boolean canonicalIncludes;
    Boolean keepObjects;
} PCmdLineCompiler;

enum {
    IncludeSearch_Proj = 0,
    IncludeSearch_Source = 1,
    IncludeSearch_Explicit = 2,
    IncludeSearch_Include = 3
};

typedef struct PCmdLineLinker {
    SInt16 version;
    Boolean callPreLinker;
    Boolean callPostLinker;
    Boolean keepLinkerOutput;
    Boolean callLinker;
} PCmdLineLinker;

typedef struct PCmdLineEnvir {
    SInt16 version;
    SInt16 cols;
    SInt16 rows;
    Boolean underIDE;
} PCmdLineEnvir;

typedef struct PFrontEndC {
    SInt16 version;
    Boolean cplusplus;
    Boolean checkprotos;
    Boolean arm;
    Boolean trigraphs;
    Boolean onlystdkeywords;
    Boolean enumsalwaysint;
    Boolean mpwpointerstyle;
    unsigned char oldprefixname[32];
    Boolean ansistrict;
    Boolean mpwcnewline;
    Boolean wchar_type;
    Boolean enableexceptions;
    Boolean dontreusestrings;
    Boolean poolstrings;
    Boolean dontinline;
    Boolean useRTTI;
    Boolean multibyteaware;
    Boolean unsignedchars;
    Boolean autoinline;
    Boolean booltruefalse;
    Boolean direct_to_som;
    Boolean som_env_check;
    Boolean alwaysinline;
    SInt16 inlinelevel;
    Boolean ecplusplus;
    Boolean objective_c;
    Boolean defer_codegen;
    Boolean templateparser;
} PFrontEndC;

typedef struct PWarningC {
    SInt16 version;
    Boolean warn_illpragma;
    Boolean warn_emptydecl;
    Boolean warn_possunwant;
    Boolean warn_unusedvar;
    Boolean warn_unusedarg;
    Boolean warn_extracomma;
    Boolean pedantic;
    Boolean warningerrors;
    Boolean warn_hidevirtual;
    Boolean warn_implicitconv;
    Boolean warn_notinlined;
    Boolean warn_structclass;
} PWarningC;

typedef struct PGlobalOptimizer {
    SInt16 version;
    UInt8 optimizationlevel;
    UInt8 optfor;
    UInt8 reserved[8];
} PGlobalOptimizer;

typedef struct PBackEnd {
    SInt16 version;
    UInt8 structalignment;
    Boolean readonlystrings;
    Boolean pooldata;
    UInt8 unknown05;
    Boolean profiler;
    UInt8 unknown07;
    Boolean peephole;
    UInt8 unknown09[2];
    Boolean schedule;
    UInt8 unknown0c;
    Boolean common;
    UInt8 floatingpoint;
    Boolean use_lmw_stmw;
    SInt16 processor;
    UInt8 funcalign;
    Boolean fp_contract;
    UInt8 altivec;
    Boolean vrsave;
#if VERSION < VERSION_GC_2_0
    UInt8 unknown16[6];
#else
    UInt8 unknown16[22];
#endif
} PBackEnd;

typedef struct PLinker {
    SInt16 version;
    Boolean debugInfo;
    UInt8 symfullpath;
    Boolean map;
    Boolean suppresswarn;
    Boolean srec;
    Boolean mapunused;
    Boolean lcf;
    Boolean hascodeaddr;
    Boolean hasdataaddr;
    Boolean hassdataaddr;
    UInt8 unknown0c;
    Boolean hasstackaddr;
    Boolean hasheapaddr;
    Boolean hasromaddr;
    UInt32 codeaddr;
    UInt32 dataaddr;
    UInt32 sdataaddr;
    UInt32 sdata2addr;
    UInt32 stackaddr;
    UInt32 rambuffer;
    UInt32 romaddr;
    SInt16 sreclength;
    UInt8 sreceol;
    UInt8 unknown2f;
    char mainname[64];
    UInt32 heapaddr;
} PLinker;

typedef struct PDisassembler {
    SInt16 version;
    Boolean detail;
    Boolean debug;
    Boolean relocate;
    UInt8 unknown05;
    Boolean code;
    Boolean extended;
    Boolean binary;
    Boolean data;
    Boolean xtables;
    Boolean headers;
    Boolean tables;
} PDisassembler;

typedef struct PProject {
    SInt16 version;
    UInt8 unknown02[34];
    UInt32 heapsize;
    UInt32 stacksize;
    UInt8 endian;
    SInt16 sdatathreshold;
    SInt16 sdata2threshold;
    SInt16 codemodel;
    UInt8 unknown34[3];
    Boolean no_comment_records;
    Boolean strip;
    UInt8 partial;
    UInt8 resolved;
} PProject;

typedef struct PCLTExtras {
    SInt16 version;
    char mapfile[256];
    char srecfile[256];
} PCLTExtras;
#pragma pack(pop)

extern PCmdLine pCmdLine;
extern PCmdLineCompiler pCmdLineCompiler;
extern PBackEnd pBackEnd;
extern PLinker pLinker;
extern PDisassembler pDisassembler;
extern PProject pProject;
extern PCLTExtras pCLTExtras;
extern PFrontEndC pFrontEndC;
extern PWarningC pWarningC;
extern PGlobalOptimizer pGlobalOptimizer;

#ifdef __cplusplus
}
#endif

#endif
