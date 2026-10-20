#include "driver/ErrMgr.h"
#include <stdio.h>
#include <string.h>

static const char* getsyserr(SInt16 msgNbr) {
    // clang-format off
    switch (msgNbr) {
        case 0: return "No error";
        case -27: return "I/O call aborted by KillIO";
        case -34: return "Disk full";
        case -35: return "No such volume";
        case -36: return "Input/output error";
        case -37: return "Some component of filename is too long";
        case -39: return "End-of-file error";
        case -40: return "Tried to position before EOF";
        case -42: return "Too many files open";
        case -43: return "File or directory not found";
        case -48: return "Filename already exists on rename or create";
        case -50: return "Error in user parameter list";
        case -51: return "Bad reference number";
        case -52: return "Get file position error";
        case -54: return "Invalid file access or invalid permissions";
        case -61: return "Write permissions error";
        case -108: return "Out of available memory or buffer size exceeded";
        case -109: return "NULL or invalid address detected";
        case -113: return "invalid handle detected";
        case -120: return "Directory not found";
        case -128: return "User cancelled";
        case -192: return "Resource not found";
        case -1302: return "Not a file";
        case -1309: return "Seek position exceeds boundaries for filesystem";
        case -1310: return "File size too big for filesystem";
        case -188: return "Resource already in memory";
        case -189: return "Writing past end of file";
        case -190: return "Offset of Count out of bounds";
        case -193: return "Resource file not found";
        case -194: return "Could not add resource";
        case -195: return "Could not add reference";
        case -196: return "Could not remove resource";
        case -197: return "Could not remove reference";
        case -198: return "Resource attribute inconsistent with operation";
        case -199: return "Resource map corrupted or inconsistent with operation";
        case -32767: return "Feature not supported";
        default: return NULL;
    }
    // clang-format on
}

char* __stdcall GetSysErrText(SInt16 msgNbr, char* errMsg) {
    const char* txt;

    txt = getsyserr(msgNbr);
    if (!txt) {
        sprintf(errMsg, "Operating system error %ld", (long)(msgNbr & 0x7FFF));
    } else {
        strcpy(errMsg, txt);
    }

    return errMsg;
}
