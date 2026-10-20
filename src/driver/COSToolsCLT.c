#include "driver/COSToolsCLT.h"
#include "driver/CCompiler.h"
#include "driver/Files.h"
#include "driver/LowMem.h"
#include "driver/MacSpecs.h"
#include "driver/Memory.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include "driver/TextUtils.h"

static void COS_pstrcpy(StringPtr dst, ConstStringPtr src) {
    short cnt;
    for (cnt = src[0]; cnt >= 0; cnt--) {
        *(dst++) = *(src++);
    }
}

Handle COS_NewHandle(SInt32 byteCount) {
    return NewHandle(byteCount);
}

Handle COS_NewOSHandle(SInt32 logicalSize) {
    OSErr err;
    Handle h;

    if (systemHandles) {
        h = TempNewHandle(logicalSize, &err);
        if (!err) {
            return h;
        }
    }

    return NULL;
}

void COS_FreeHandle(Handle handle) {
    DisposeHandle(handle);
}

Boolean COS_ResizeHandle(Handle handle, SInt32 newSize) {
    SetHandleSize(handle, newSize);
    return LMGetMemErr() == noErr;
}

void COS_LockHandle(Handle handle) {
    HLock(handle);
}

void COS_LockHandleHi(Handle handle) {
    HLockHi(handle);
}

void COS_UnlockHandle(Handle handle) {
    HUnlock(handle);
}

SInt32 COS_GetTicks(void) {
    return LMGetTicks();
}

void COS_GetString(char* buffer, SInt16 strListID, SInt16 index) {
    GetIndString((StringPtr)buffer, strListID, index);
    p2cstr((StringPtr)buffer);
}

Boolean COS_IsMultiByte(const void* str, const void* pos) {
    return OS_IsMultiByte(str, pos);
}

OSErr COS_FileNew(const FSSpec* spec, SInt16* refNum, OSType creator, OSType fileType) {
    OSErr err;

    HDelete(spec->vRefNum, spec->parID, spec->name);
    err = HCreate(spec->vRefNum, spec->parID, spec->name, creator, fileType);
    if (!err) {
        err = HOpenDF(spec->vRefNum, spec->parID, spec->name, fsRdWrPerm, refNum);
    }
    return err;
}

OSErr COS_FileOpen(const FSSpec* spec, SInt16* refNum) {
    return HOpenDF(spec->vRefNum, spec->parID, spec->name, fsRdPerm, refNum);
}

OSErr COS_FileGetType(const FSSpec* spec, OSType* fileType) {
    OSErr err;
    FInfo fi;

    err = HGetFInfo(spec->vRefNum, spec->parID, spec->name, &fi);
    *fileType = fi.fdType;
    return err;
}

OSErr COS_FileGetSize(SInt16 refNum, SInt32* logEOF) {
    return GetEOF(refNum, logEOF);
}

OSErr COS_FileRead(SInt16 refNum, void* buffPtr, SInt32 count) {
    return FSRead(refNum, &count, buffPtr);
}

OSErr COS_FileWrite(SInt16 refNum, const void* buffPtr, SInt32 count) {
    return FSWrite(refNum, &count, buffPtr);
}

OSErr COS_FileGetPos(SInt16 refNum, SInt32* filePos) {
    return GetFPos(refNum, filePos);
}

OSErr COS_FileSetPos(SInt16 refNum, SInt32 filePos) {
    return SetFPos(refNum, fsFromStart, filePos);
}

OSErr COS_FileClose(SInt16 refNum) {
    return FSClose(refNum);
}

void COS_FileGetFSSpecInfo(const FSSpec* spec, SInt16* vRefNum, SInt32* dirID, StringPtr fileName) {
    if (vRefNum) {
        *vRefNum = spec->vRefNum;
    }
    if (dirID) {
        *dirID = spec->parID;
    }
    if (fileName) {
        COS_pstrcpy(fileName, spec->name);
    }
}

#ifdef __MWERKS__
static void COS_MakePath(SInt16 vRefNum, SInt32 dirID, StringPtr path) {
    FSSpec fss;
    OSSpec oss;
    fss.vRefNum = vRefNum;
    fss.parID = dirID;
    COS_pstrcpy(fss.name, path);
    if (!OS_FSSpec_To_OSSpec(&fss, &oss)) {
        OS_SpecToString(&oss, (char*)path, 260);
        c2pstr((char*)path);
    }
}
#else
static void COS_MakePath(SInt16 vRefNum, SInt32 dirID, StringPtr path) {
    struct {
        FSSpec fss;
        UInt8 gap[2];
        OSSpec oss;
    } frame;
    frame.fss.vRefNum = vRefNum;
    frame.fss.parID = dirID;
    COS_pstrcpy(frame.fss.name, path);
    if (!OS_FSSpec_To_OSSpec(&frame.fss, &frame.oss)) {
        OS_SpecToString(&frame.oss, (char*)path, 260);
        c2pstr((char*)path);
    }
}
#endif

void COS_FileGetPathName(char* buffer, const FSSpec* spec, SInt32* mdDat) {
    HParamBlockRec rec;

    if (mdDat) {
        rec.fileParam.ioNamePtr = (StringPtr)spec->name;
        rec.fileParam.ioVRefNum = spec->vRefNum;
        rec.fileParam.ioDirID = spec->parID;
        rec.fileParam.ioFDirIndex = 0;
        if (!PBHGetFInfoSync(&rec)) {
            *mdDat = rec.fileParam.ioFlMdDat;
        } else {
            *mdDat = 0;
        }
    }

    COS_pstrcpy((StringPtr)buffer, spec->name);
    COS_MakePath(spec->vRefNum, spec->parID, (StringPtr)buffer);
    p2cstr((StringPtr)buffer);
}
