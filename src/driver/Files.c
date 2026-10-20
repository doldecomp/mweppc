#include "driver/Files.h"
#include "driver/Generic.h"
#include "driver/MacFileTypes.h"
#include "driver/MacSpecs.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include "driver/Resources.h"
#include "driver/StringUtils.h"
#include <stdio.h>
#include <string.h>

typedef struct MacFileInfo {
    UInt32 ioFlCrDat;
    UInt32 ioFlMdDat;
    SInt32 ioFlLgLen;
    SInt32 ioFlPyLen;
    SInt32 ioFlRLgLen;
    SInt32 ioFlRPyLen;
    SInt32 ioFlStBlk;
    SInt32 ioFlRStBlk;
    SInt8 ioFlAttrib;
} MacFileInfo;

// (the Mac emulation's API, used before their definitions; no other unit calls them)
OSErr __stdcall FSpGetFInfo(const FSSpec* fss, FInfo* fndrInfo);
OSErr __stdcall FSpSetFInfo(const FSSpec* fss, const FInfo* fndrInfo);
OSErr __stdcall PBHSetFInfoSync(HParmBlkPtr paramBlock);

static int OS_GetFileParamInfo(const OSSpec* spec, MacFileInfo* mfi, FInfo* fi) {
    OSTime crtm;
    OSTime chtm;
    OSType ft;
    OSType fc;
    UInt32 ttime;
    UInt32 dsize;
    UInt32 rsize;
    OSSpec rsrc;
    int ref;
    int err;
    char buffer[32];
    UInt32 count;
    Boolean isopen;

    if (mfi) {
        OS_GetFileTime(spec, &crtm, &chtm);
        OS_TimeToMac(crtm, &ttime);
        mfi->ioFlCrDat = ttime;
        OS_TimeToMac(chtm, &ttime);
        mfi->ioFlMdDat = ttime;
    }

    err = OS_Open(spec, OSReadOnly, &ref);
    if (err) {
        return err;
    }

    count = sizeof(buffer);
    if (OS_Read(ref, buffer, &count)) {
        count = 0;
    }

    if (mfi) {
        mfi->ioFlAttrib = OS_IsDir(spec) ? ioDirMask : 0;
        OS_GetSize(ref, &dsize);
        mfi->ioFlLgLen = mfi->ioFlPyLen = dsize;
        mfi->ioFlStBlk = 0;
    }

    OS_Close(ref);

    OS_GetMacFileTypeMagic(buffer, count, &ft);
    fc = 'CWIE';
    if (OS_UsingMacResourceForkInfo() &&
        !OS_GetRsrcOSSpec(spec, &rsrc, FALSE) &&
        !OS_Status(&rsrc)) {
        isopen = OS_SearchMacResourceForkList(&rsrc, &ref);
        if (isopen || !OS_Open(&rsrc, 0, &ref)) {
            if (mfi) {
                OS_GetSize(ref, &rsize);
                mfi->ioFlRLgLen = mfi->ioFlRPyLen = rsize;
                mfi->ioFlRStBlk = 0;
            }
            OS_GetMacResourceForkCreatorAndType(ref, &fc, &ft);
            if (!isopen) {
                OS_Close(ref);
            }
        }
    } else {
        if (mfi) {
            mfi->ioFlRLgLen = mfi->ioFlRPyLen = 0;
            mfi->ioFlRStBlk = 0;
        }
    }

    fi->fdType = ft;
    fi->fdCreator = fc;
    fi->fdFlags = OS_IsLink(spec) ? kIsAlias : 0;
    fi->fdLocation.v = 0;
    fi->fdLocation.h = 0;
    fi->fdFldr = 0;
    return 0;
}

static int OS_SetFileParamInfo(const OSSpec* spec, MacFileInfo* mfi, FInfo* fi) {
    OSTime crtm;
    OSTime chtm;
    OSSpec rspec;
    int ref;
    Boolean isopen;

    OS_MacToTime(mfi->ioFlCrDat, &crtm);
    OS_MacToTime(mfi->ioFlMdDat, &chtm);
    OS_SetFileTime(spec, &crtm, &chtm);

    OS_SetMacFileType(spec, fi->fdType);

    if (OS_UsingMacResourceForkInfo() &&
        !OS_GetRsrcOSSpec(spec, &rspec, FALSE) &&
        !OS_Status(&rspec)) {
        isopen = OS_SearchMacResourceForkList(&rspec, &ref);
        if (!isopen && OS_Open(&rspec, OSReadWrite, &ref)) {
            return 0;
        }
        OS_SetMacResourceForkCreatorAndType(ref, fi->fdCreator, fi->fdType);
        if (!isopen) {
            OS_Close(ref);
        }
    }

    return 0;
}

OSErr __stdcall HCreate(
    SInt16 volume, SInt32 dirid, ConstStringPtr fileName, OSType creator, OSType type
) {
    FSSpec fss;
    OSErr __err;

    __err = FSMakeFSSpec(volume, dirid, fileName, &fss);
    if (__err && __err != fnfErr) {
        return __err;
    }
    return FSpCreate(&fss, creator, type, -1);
}

OSErr __stdcall HOpen(
    SInt16 vRefNum, SInt32 dirID, ConstStringPtr fileName, SInt8 permission, SInt16* refNum
) {
    FSSpec fss;
    OSErr __err;

    __err = FSMakeFSSpec(vRefNum, dirID, fileName, &fss);
    if (__err && __err != fnfErr) {
        return __err;
    }
    return FSpOpenDF(&fss, permission, refNum);
}

OSErr __stdcall HOpenDF(
    SInt16 vRefNum, SInt32 dirID, ConstStringPtr fileName, SInt8 permission, SInt16* refNum
) {
    FSSpec fss;
    OSErr __err;

    __err = FSMakeFSSpec(vRefNum, dirID, fileName, &fss);
    if (__err && __err != fnfErr) {
        return __err;
    }
    return FSpOpenDF(&fss, permission, refNum);
}

OSErr __stdcall HDelete(SInt16 vRefNum, SInt32 dirID, ConstStringPtr fileName) {
    FSSpec fss;
    OSErr __err;

    __err = FSMakeFSSpec(vRefNum, dirID, fileName, &fss);
    if (__err && __err != fnfErr) {
        return __err;
    }
    return FSpDelete(&fss);
}

OSErr __stdcall HGetFInfo(SInt16 vRefNum, SInt32 dirID, ConstStringPtr fileName, FInfo* fndrInfo) {
    FSSpec fss;
    OSErr __err;

    __err = FSMakeFSSpec(vRefNum, dirID, fileName, &fss);
    if (__err && __err != fnfErr) {
        return __err;
    }
    return FSpGetFInfo(&fss, fndrInfo);
}

OSErr __stdcall FSpCreate(
    const FSSpec* fss, OSType creator, OSType fileType, ScriptCode scriptTag
) {
    int err;
    OSSpec spec;
    FInfo finfo;

    err = OS_FSSpec_To_OSSpec(fss, &spec);
    if (err) {
        return OS_OSErrorToMacError(err);
    }

    if (!OS_Status(&spec)) {
        return dupFNErr;
    }

    err = OS_Create(&spec, &OS_TEXTTYPE);
    if (err) {
        return OS_OSErrorToMacError(err);
    }

    memset(&finfo, 0, sizeof(FInfo));
    finfo.fdType = fileType;
    finfo.fdCreator = creator;
    FSpSetFInfo(fss, &finfo);
    return 0;
}

OSErr __stdcall FSpOpenDF(const FSSpec* fss, SInt8 permission, SInt16* refNum) {
    static OSOpenMode MacOpens[5] = {OSReadWrite, OSReadOnly, OSWrite, OSReadWrite, OSReadWrite};

    OSSpec spec;
    int err;
    int ref;

    *refNum = 0;

    err = OS_FSSpec_To_OSSpec(fss, &spec);
    if (err) {
        return OS_OSErrorToMacError(err);
    }

    if (OS_IsDir(&spec)) {
        return dirNFErr;
    }

    err = OS_Open(&spec, MacOpens[permission], &ref);
    if (err) {
        return OS_OSErrorToMacError(err);
    }

    *refNum = OS_RefToMac(ref);
    return 0;
}

OSErr __stdcall FSpDelete(const FSSpec* fss) {
    OSSpec spec;
    OSSpec rspec;
    int err;

    err = OS_FSSpec_To_OSSpec(fss, &spec);
    if (err) {
        return OS_OSErrorToMacError(err);
    }

    err = OS_Delete(&spec);
    if (!OS_GetRsrcOSSpec(&spec, &rspec, FALSE)) {
        err = OS_Delete(&rspec);
        OS_CleanupMacResourceForkDir(&rspec.path);
    }
    return OS_OSErrorToMacError(err);
}

OSErr __stdcall FSpGetFInfo(const FSSpec* fss, FInfo* fndrInfo) {
    int err;
    OSSpec spec;
    FInfo fi;

    err = OS_FSSpec_To_OSSpec(fss, &spec);
    if (err) {
        return OS_OSErrorToMacError(err);
    }

    err = OS_GetFileParamInfo(&spec, 0, &fi);
    if (err) {
        return OS_OSErrorToMacError(err);
    }

    memcpy(fndrInfo, &fi, sizeof(FInfo));
    return 0;
}

OSErr __stdcall FSpSetFInfo(const FSSpec* fss, const FInfo* fndrInfo) {
    HParamBlockRec hpb;
    OSErr err;
    unsigned char fname[256];

    _pstrcpy(fname, fss->name);
    hpb.fileParam.ioVRefNum = fss->vRefNum;
    hpb.fileParam.ioDirID = fss->parID;
    hpb.fileParam.ioNamePtr = fname;
    hpb.fileParam.ioFDirIndex = 0;

    err = PBHGetFInfoSync(&hpb);
    if (!err) {
        hpb.fileParam.ioFlFndrInfo = *fndrInfo;
        err = PBHSetFInfoSync(&hpb);
    }
    return err;
}

// (no caller: the linker dropped it, but its "" stays in the unit's .data)
OSErr __stdcall HGetVol(StringPtr volName, SInt16* vRefNum, SInt32* dirID) {
    OSSpec spec;
    int err;
    FSSpec fss;
    char vn[256];

    if (volName && volName[0]) {
        p2cstrcpy(vn, volName);
        err = OS_MakePathSpec(vn, 0, &spec.path);
        if (err) {
            return OS_OSErrorToMacError(err);
        }
    } else {
        err = OS_MakePathSpec(0, 0, &spec.path);
        if (err) {
            return OS_OSErrorToMacError(err);
        }
    }

    OS_MakeNameSpec("", &spec.name);
    err = OS_OSSpec_To_FSSpec(&spec, &fss);
    if (err) {
        return OS_OSErrorToMacError(err);
    }

    *vRefNum = fss.vRefNum;
    *dirID = fss.parID;
    return 0;
}

OSErr __stdcall FSRead(SInt16 fRefNum, SInt32* Size, void* Buffer) {
    if (fRefNum == 0) {
        return rfNumErr;
    }

    return OS_OSErrorToMacError(OS_Read(OS_MacToRef(fRefNum), Buffer, (UInt32*)Size));
}

OSErr __stdcall FSWrite(SInt16 fRefNum, SInt32* Size, const void* Buffer) {
    if (fRefNum == 0) {
        return rfNumErr;
    }

    return OS_OSErrorToMacError(OS_Write(OS_MacToRef(fRefNum), Buffer, (UInt32*)Size));
}

OSErr __stdcall FSClose(SInt16 fRefNum) {
    OSErr err;
    OSSpec* rspec;
    int ref;
    UInt32 size;
    int oerr;

    if (fRefNum == 0) {
        return rfNumErr;
    }

    ref = OS_MacToRef(fRefNum);
    rspec = OS_GetMacResourceForkFromRef(ref);
    if (rspec) {
        oerr = OS_GetSize(ref, &size);
        err = OS_OSErrorToMacError(OS_Close(ref));
        if (!oerr && !size) {
            OS_Delete(rspec);
            OS_CleanupMacResourceForkDir(&rspec->path);
        }
        OS_RemoveMacResourceForkRef(ref);
    } else {
        err = OS_OSErrorToMacError(OS_Close(ref));
    }

    return err;
}

OSErr __stdcall GetEOF(SInt16 fRefNum, SInt32* curEOF) {
    if (fRefNum == 0) {
        return rfNumErr;
    }

    return OS_OSErrorToMacError(OS_GetSize(OS_MacToRef(fRefNum), (UInt32*)curEOF));
}

OSErr __stdcall SetEOF(SInt16 fRefNum, SInt32 newEOF) {
    if (fRefNum == 0) {
        return rfNumErr;
    }

    return OS_OSErrorToMacError(OS_SetSize(OS_MacToRef(fRefNum), newEOF));
}

OSErr __stdcall GetFPos(SInt16 fRefNum, SInt32* pos) {
    if (fRefNum == 0) {
        return rfNumErr;
    }

    return OS_OSErrorToMacError(OS_Tell(OS_MacToRef(fRefNum), pos));
}

// (read only by PBWriteSync, which has no caller and which the linker dropped; the table stays in
// the unit's .data)
static OSSeekMode MacSeek[4] = {OSSeekAbs, OSSeekAbs, OSSeekEnd, OSSeekRel};

OSErr __stdcall SetFPos(SInt16 fRefNum, SInt16 posMode, SInt32 posOffset) {
    SInt32 pos;
    SInt32 npos;
    UInt32 size;
    int err;
    int ref;
    int mode;

    ref = OS_MacToRef(fRefNum);
    if (fRefNum == 0) {
        return rfNumErr;
    }

    posMode &= 3;
    mode = posMode;
    if (mode) {
        err = OS_Tell(ref, &pos);
        if (err) {
            return OS_OSErrorToMacError(err);
        }

        err = OS_GetSize(ref, &size);
        if (err) {
            return OS_OSErrorToMacError(err);
        }

        if (mode == fsFromStart) {
            npos = posOffset;
        } else if (mode == fsFromLEOF) {
            npos = size + posOffset;
        } else if (mode == fsFromMark) {
            npos = pos + posOffset;
        } else {
            return paramErr;
        }

        err = OS_Seek(ref, OSSeekAbs, npos);
        if (err) {
            return OS_OSErrorToMacError(err);
        }

        if ((UInt32)npos > size) { // (an unsigned compare: the original's)
            OS_Seek(ref, OSSeekAbs, pos);
            return eofErr;
        }
    }

    return 0;
}

OSErr __stdcall PBHGetFInfoSync(HParmBlkPtr paramBlock) {
    HFileParam fp;
    FSSpec fss;
    OSErr err;
    int oserr;
    OSSpec spec;
    MacFileInfo mfi;
    FInfo fi;

    memcpy(&fp, &paramBlock->fileParam, sizeof(HFileParam));
    err = FSMakeFSSpec(
        fp.ioVRefNum, fp.ioDirID, (fp.ioFDirIndex != -1) ? fp.ioNamePtr : (StringPtr) "", &fss
    );
    if (!err) {
        oserr = OS_FSSpec_To_OSSpec(&fss, &spec);
        if (!oserr) {
            OS_GetFileParamInfo(&spec, &mfi, &fi);
            fp.ioFlCrDat = mfi.ioFlCrDat;
            fp.ioFlMdDat = mfi.ioFlMdDat;
            fp.ioFlLgLen = mfi.ioFlLgLen;
            fp.ioFlPyLen = mfi.ioFlPyLen;
            fp.ioFlRLgLen = mfi.ioFlRLgLen;
            fp.ioFlRPyLen = mfi.ioFlRPyLen;
            fp.ioFlAttrib = mfi.ioFlAttrib;
            memcpy(&fp.ioFlFndrInfo, &fi, sizeof(FInfo));
        } else {
            memset(&fp, 0, sizeof(fp));
        }
    }

    fp.ioResult = err;
    memcpy(&paramBlock->fileParam, &fp, sizeof(HFileParam));
    return err;
}

OSErr __stdcall PBHSetFInfoSync(HParmBlkPtr paramBlock) {
    HFileParam fp;
    FInfo* pfi;
    FSSpec fss;
    OSErr err;
    int oserr;
    OSSpec spec;
    MacFileInfo mfi;
    FInfo fi;

    pfi = &fp.ioFlFndrInfo;
    memcpy(&fp, &paramBlock->fileParam, sizeof(HFileParam));
    err = FSMakeFSSpec(fp.ioVRefNum, fp.ioDirID, fp.ioNamePtr, &fss);
    if (!err) {
        oserr = OS_FSSpec_To_OSSpec(&fss, &spec);
        if (!oserr) {
            memcpy(&fi, pfi, sizeof(FInfo));
            memset(&mfi, 0, sizeof(mfi));
            mfi.ioFlCrDat = fp.ioFlCrDat;
            mfi.ioFlMdDat = fp.ioFlMdDat;
            oserr = OS_SetFileParamInfo(&spec, &mfi, &fi);
        }
        err = OS_OSErrorToMacError(oserr);
    }

    fp.ioResult = err;
    memcpy(&paramBlock->fileParam, &fp, sizeof(HFileParam));
    return err;
}

OSErr __stdcall FSMakeFSSpec(SInt16 vRefNum, SInt32 dirID, ConstStringPtr pathName, FSSpec* fss) {
    OSSpec spec;
    int err;
    char path[260];
    FSSpec dirfss; // NOTE: invented name
    int idx;
    char* pptr;
    char vol[64];
    char* nmptr;
    int len;

    fss->vRefNum = 0;
    fss->parID = 0;

    if (!vRefNum && !dirID) {
        err = OS_GetCWD(&spec.path);
        if (err) {
            fss->name[0] = 0;
            return OS_OSErrorToMacError(err);
        }
        OS_PathSpecToString(&spec.path, path, sizeof(path));
    } else {
        OS_ASSERT(840, vRefNum!=0);
        dirfss.vRefNum = vRefNum;
        dirfss.parID = dirID == 0 ? 2 : dirID;
        dirfss.name[0] = 0;
        err = OS_FSSpec_To_OSSpec(&dirfss, &spec);
        if (err) {
            fss->name[0] = 0;
            return OS_OSErrorToMacError(err);
        }
        OS_PathSpecToString(&spec.path, path, sizeof(path));
    }

    if (pstrchr(pathName, ':')) {
        idx = 1;
        if (pathName[1] != ':') {
            while (idx <= pathName[0]) {
                if (pathName[idx] == ':') {
                    break;
                }
                vol[idx - 1] = pathName[idx];
                idx++;
                // (a volume name has at most 63 characters)
                if (idx >= 64) {
                    fss->name[0] = 0;
                    return nsvErr;
                }
            }
            vol[idx - 1] = 0;
            idx++;
            if (!OS_MakePathSpec(vol, 0, &spec.path)) {
                OS_PathSpecToString(&spec.path, path, sizeof(path));
            } else {
                c2pstrcpy(fss->name, vol);
                return nsvErr;
            }
        }

        pptr = path + strlen(path);
        while (idx <= pathName[0]) {
            if (pathName[idx] == ':') {
                if (idx < pathName[0] && pathName[idx + 1] == ':') {
                    // (the Win32 separator)
                    do {
                        pptr += sprintf(pptr, "..\\");
                        idx++;
                    } while (idx < pathName[0] && pathName[idx + 1] == ':');
                } else {
                    *(pptr++) = OS_PATHSEP;
                }
            } else {
                *(pptr++) = pathName[idx];
            }
            ++idx;
        }
        *pptr = 0;
    } else {
        p2cstrcpy(path + strlen(path), pathName);
    }

    err = OS_MakeSpec(path, &spec, 0);
    if (!err) {
        OS_OSSpec_To_FSSpec(&spec, fss);
        return OS_OSErrorToMacError(OS_Status(&spec));
    } else {
        nmptr = OS_GetFileNamePtr(path);
        len = strlen(nmptr);
        if ((size_t)len >= sizeof(fss->name)) {
            len = 63;
        }
        fss->name[0] = len;
        memcpy(&fss->name[1], nmptr, len);
        return OS_OSErrorToMacError(err);
    }
}
