#ifndef DRIVER_OSLIB_H
#define DRIVER_OSLIB_H

#include "compiler/common.h"
#include "compiler/win32.h"

// The command line's OS abstraction layer: the types its units (Generic, MsDos, FileHandles,
// MacFileTypes, MacSpecs, MemUtils, StringExtras, StringUtils) share. They are the Win32 build's: a
// path is MAX_PATH long and a name 256, a handle is GlobalAlloc memory with no separate capacity,
// and an opened directory keeps FindFirstFile's data. Each unit's header declares its functions.

#ifdef __cplusplus
extern "C" {
#endif

#define OS_PATHSEP '\\'

// (MSL's assert.c: every assert of the compiler and the driver reports through it)
extern void __assertion_failed(char const* condition, char const* filename, int lineno);

// (reports through __assertion_failed; an && condition is computed whole before the test)
#define OS_ASSERT(line, cond) \
    if ((cond) == 0) { \
        __assertion_failed(#cond, __FILE__, line); \
    }

// (the command line parser's asserts are MSL's assert(): the expression form, with the original's
// __LINE__ written out; the conditions keep the original's spelling, parentheses included)
#define OPTION_ASSERT(line, cond) ((cond) ? (void)0 : __assertion_failed(#cond, __FILE__, line))

/* (the Mac types the layer speaks in) */
typedef SInt16 OSErr;
typedef SInt16 ScriptCode;
typedef UInt32 OSType;
typedef unsigned char* StringPtr;
typedef const unsigned char* ConstStringPtr;
typedef unsigned char Str255[256];

// (a Win32 FILETIME under the layer's own tag, which Wii's CLDropinCallbacks_V10 names in its
// OSTime::operator=)
typedef struct OSTime {
    UInt32 dwLowDateTime;
    UInt32 dwHighDateTime;
} OSTime;

enum {
    noErr = 0,
    abortErr = -27,
    dskFulErr = -34,
    nsvErr = -35,
    ioErr = -36,
    bdNamErr = -37,
    eofErr = -39,
    tmfoErr = -42,
    fnfErr = -43,
    dupFNErr = -48,
    paramErr = -50,
    rfNumErr = -51,
    permErr = -54,
    wPrErr = -61,
    memFullErr = -108,
    nilHandleErr = -109,
    memLockedErr = -117,
    dirNFErr = -120,
    userCanceledErr = -128,
    inputOutOfBounds = -190,
    resNotFound = -192,
    resFNotFound = -193,
    addResFailed = -194,
    rmvResFailed = -196,
    resAttrErr = -198,
    mapReadErr = -199,
    notAFileErr = -1302,
    fsDataTooBigErr = -1310
};

// NOTE: uOSTypePair, OSOpenMode, OSSeekMode, OSFileHandle and OSOpenedDir are invented names: no
// symbol (Mac or Wii) names them
typedef struct uOSTypePair {
    int perm;
} uOSTypePair;

// (enums passed as bytes)
typedef UInt8 OSOpenMode;
enum {
    OSReadOnly,
    OSWrite,
    OSReadWrite,
    OSAppend
};

typedef UInt8 OSSeekMode;
enum {
    OSSeekRel,
    OSSeekAbs,
    OSSeekEnd
};

typedef struct OSPathSpec {
    char s[260];
} OSPathSpec;

typedef struct OSNameSpec {
    char s[256];
} OSNameSpec;

typedef struct OSSpec {
    OSPathSpec path;
    OSNameSpec name;
} OSSpec;

typedef struct OSHandle {
    void* addr;
    UInt32 used;
} OSHandle;

typedef struct OSFileHandle {
    OSSpec spec;
    OSHandle hand;
    Boolean loaded;
    Boolean changed;
    Boolean writeable;
} OSFileHandle;

typedef struct OSOpenedDir {
    HANDLE dir;
    LPWIN32_FIND_DATAA data;
    OSPathSpec spec;
} OSOpenedDir;

#ifdef __cplusplus
}
#endif

#endif
