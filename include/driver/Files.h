#ifndef DRIVER_FILES_H
#define DRIVER_FILES_H

#include "compiler/common.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)

typedef struct FSSpec {
    SInt16 vRefNum;
    SInt32 parID;
    UInt8 name[64];
} FSSpec;
typedef struct FSSpec CWFileSpec;

typedef struct Point {
    SInt16 v;
    SInt16 h;
} Point;

enum {
    fsCurPerm = 0x00,
    fsRdPerm = 0x01,
    fsWrPerm = 0x02,
    fsRdWrPerm = 0x03,
    fsRdWrShPerm = 0x04,
    fsRdDenyPerm = 0x10,
    fsWrDenyPerm = 0x20
};
enum {
    fsAtMark = 0,
    fsFromStart = 1,
    fsFromLEOF = 2,
    fsFromMark = 3
};
enum {
    ioDirMask = 0x10
};
enum {
    kIsAlias = 0x8000
};

typedef struct FInfo {
    OSType fdType;
    OSType fdCreator;
    UInt16 fdFlags;
    Point fdLocation;
    SInt16 fdFldr;
} FInfo;

typedef struct HFileParam {
    void* qLink;
    SInt16 qType;
    SInt16 ioTrap;
    char* ioCmdAddr;
    void* ioCompletion;
    SInt16 ioResult;
    StringPtr ioNamePtr;
    SInt16 ioVRefNum;
    SInt16 ioFRefNum;
    SInt8 ioFVersNum;
    SInt8 filler1;
    SInt16 ioFDirIndex;
    SInt8 ioFlAttrib;
    SInt8 ioFlVersNum;
    FInfo ioFlFndrInfo;
    SInt32 ioDirID;
    UInt16 ioFlStBlk;
    SInt32 ioFlLgLen;
    SInt32 ioFlPyLen;
    UInt16 ioFlRStBlk;
    SInt32 ioFlRLgLen;
    SInt32 ioFlRPyLen;
    UInt32 ioFlCrDat;
    UInt32 ioFlMdDat;
} HFileParam;

typedef union HParamBlockRec {
    HFileParam fileParam;
} HParamBlockRec;
typedef HParamBlockRec* HParmBlkPtr;
#pragma pack(pop)

extern OSErr __stdcall HCreate(
    SInt16 volume, SInt32 dirid, ConstStringPtr fileName, OSType creator, OSType type
);
extern OSErr __stdcall HOpen(
    SInt16 vRefNum, SInt32 dirID, ConstStringPtr fileName, SInt8 permission, SInt16* refNum
);
extern OSErr __stdcall HOpenDF(
    SInt16 vRefNum, SInt32 dirID, ConstStringPtr fileName, SInt8 permission, SInt16* refNum
);
extern OSErr __stdcall HDelete(SInt16 vRefNum, SInt32 dirID, ConstStringPtr fileName);
extern OSErr __stdcall HGetFInfo(
    SInt16 vRefNum, SInt32 dirID, ConstStringPtr fileName, FInfo* fndrInfo
);
extern OSErr __stdcall FSpCreate(
    const FSSpec* fss, OSType creator, OSType fileType, ScriptCode scriptTag
);
extern OSErr __stdcall FSpOpenDF(const FSSpec* fss, SInt8 permission, SInt16* refNum);
extern OSErr __stdcall FSpDelete(const FSSpec* fss);
extern OSErr __stdcall FSRead(SInt16 fRefNum, SInt32* Size, void* Buffer);
extern OSErr __stdcall FSWrite(SInt16 fRefNum, SInt32* Size, const void* Buffer);
extern OSErr __stdcall FSClose(SInt16 fRefNum);
extern OSErr __stdcall GetEOF(SInt16 fRefNum, SInt32* curEOF);
extern OSErr __stdcall SetEOF(SInt16 fRefNum, SInt32 newEOF);
extern OSErr __stdcall GetFPos(SInt16 fRefNum, SInt32* pos);
extern OSErr __stdcall SetFPos(SInt16 fRefNum, SInt16 posMode, SInt32 posOffset);
extern OSErr __stdcall PBHGetFInfoSync(HParmBlkPtr paramBlock);
extern OSErr __stdcall FSMakeFSSpec(
    SInt16 vRefNum, SInt32 dirID, ConstStringPtr pathName, FSSpec* fss
);

#ifdef __cplusplus
}
#endif

#endif
