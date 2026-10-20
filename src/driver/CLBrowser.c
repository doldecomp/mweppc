#include "driver/CLBrowser.h"
#include "driver/CLErrors.h"
#include "driver/Memory.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    UInt16 fileID;
    char* filepath;
} BrowseTableEntry;

typedef struct {
    UInt16 fileID;
    char unused[12];
    UInt16 filename_length;
    char filepath[1];
} BrowseFileRecord;

typedef struct {
    char magic_word[4];
    char version;
    char big_endian;
    char padding[16];
    UInt16 file_data_count;
    UInt32 browse_data_offset;
    UInt32 browse_data_length;
    UInt32 file_data_offset;
    UInt32 file_data_length;
} BrowseFileHeader;

static void GetBrowseTableInfoAndLock(
    OSHandle* browsetable, BrowseTableEntry** scan, SInt32* num, SInt32* size
) {
    UInt32 tsize;

    OS_GetHandleSize(browsetable, &tsize);
    if (scan) {
        *scan = OS_LockHandle(browsetable);
    }
    if (num) {
        *num = tsize / sizeof(BrowseTableEntry);
    }
    if (size) {
        *size = tsize;
    }
}

int Browser_Initialize(OSHandle* browsetableptr) {
    int err = OS_NewHandle(0, browsetableptr);
    if (err) {
        CLReportOSError(63, err, "allocate", "browse file table");
        return FALSE;
    } else {
        return TRUE;
    }
}

static int Destroy(OSHandle* browsetable) {
    BrowseTableEntry* scan;
    SInt32 num;

    GetBrowseTableInfoAndLock(browsetable, &scan, &num, NULL);
    while (num--) {
        xfree(scan->filepath);
        scan++;
    }
    OS_UnlockHandle(browsetable);
    return TRUE;
}

int Browser_Terminate(OSHandle* browsetableptr) {
    if (!Destroy(browsetableptr)) {
        return FALSE;
    } else {
        OS_FreeHandle(browsetableptr);
        return TRUE;
    }
}

int Browser_SearchFile(OSHandle* browsetable, const char* fullpath, SInt16* ID) {
    BrowseTableEntry* scan;
    SInt32 cnt;
    SInt32 idx;
    SInt32 size;
    Boolean found = FALSE;

    OS_ASSERT(114, OS_IsFullPath(fullpath));
    OS_ASSERT(115, browsetable!=NULL);

    GetBrowseTableInfoAndLock(browsetable, &scan, &cnt, &size);
    for (idx = 0; idx < cnt; idx++, scan++) {
        if (OS_EqualPath(fullpath, scan->filepath)) {
            found = TRUE;
            break;
        }
    }

    if (found) {
        *ID = scan->fileID;
    } else {
        *ID = 0;
    }

    OS_UnlockHandle(browsetable);
    return found;
}

int Browser_SearchAndAddFile(OSHandle* browsetable, const char* fullpath, SInt16* ID) {
    BrowseTableEntry* scan;
    SInt32 cnt;
    SInt32 size;
    char* pathptr;

    if (!Browser_SearchFile(browsetable, fullpath, ID)) {
        pathptr = xstrdup(fullpath);
        GetBrowseTableInfoAndLock(browsetable, &scan, &cnt, &size);
        OS_UnlockHandle(browsetable);
        if (OS_ResizeHandle(browsetable, sizeof(BrowseTableEntry) * (cnt + 1))) {
            fprintf(stderr, "\n*** Out of memory\n");
            exit(-23);
        }

        scan = (BrowseTableEntry*)(((char*)OS_LockHandle(browsetable)) + size);
        scan->filepath = pathptr;
        scan->fileID = cnt + 1;
        OS_UnlockHandle(browsetable);

        *ID = scan->fileID;
        return -1;
    } else {
        return 1;
    }
}

static SInt32 CalcDiskSpaceRequirements(BrowseTableEntry* mem, int num) {
    SInt32 space = 0;
    while (num--) {
        space += 0x10;
        space += (strlen(mem->filepath) + 7) & ~7;
        mem++;
    }
    return space;
}

static int ConvertMemToDisk(BrowseTableEntry* mem, BrowseFileRecord* disk, int num) {
    int slen;
    int blen;

    while (num--) {
        disk->fileID = mem->fileID;
        memset(disk->unused, 0, sizeof(disk->unused));
        slen = strlen(mem->filepath);
        blen = (slen + 7) & ~7;
        disk->filename_length = slen;
        memset(disk->filepath, 0, blen);
        strncpy(disk->filepath, mem->filepath, slen);
        mem++;
        disk = (BrowseFileRecord*)(((unsigned char*)disk) + 0x10 + blen);
    }

    return TRUE;
}

int Browser_PackBrowseFile(Handle browsedata, OSHandle* browsetable, OSHandle* browsefileptr) {
    OSHandle h;
    char* ptr;
    BrowseFileHeader header;
    BrowseTableEntry* scan;
    SInt32 entries;
    UInt32 datasize;
    UInt32 tableoffs;
    UInt32 tablesize;
    UInt32 totalsize;
    SInt32 a_long = 1;
    int err;
    void* bptr;

    OS_ASSERT(253, browsedata!=NULL);
    OS_ASSERT(254, browsetable!=NULL);

    datasize = GetHandleSize(browsedata);
    tableoffs = (datasize + sizeof(header) + 7) & ~7;

    GetBrowseTableInfoAndLock(browsetable, &scan, &entries, NULL);
    tablesize = CalcDiskSpaceRequirements(scan, entries);
    OS_UnlockHandle(browsetable);

    totalsize = tablesize + tableoffs;

    memcpy(header.magic_word, "DubL", 4);
    header.version = 1;
    header.big_endian = *((char*)&a_long) == 0;
    memset(header.padding, 0, sizeof(header.padding));

    header.browse_data_offset = sizeof(header);
    header.browse_data_length = datasize;
    header.file_data_offset = tableoffs;
    header.file_data_length = tablesize;
    header.file_data_count = entries;

    if ((err = OS_NewHandle(totalsize, &h)), *browsefileptr = h, err) {
        fprintf(stderr, "\n*** Out of memory\n");
        exit(-23);
    }

    ptr = OS_LockHandle(&h);
    memcpy(ptr, &header, sizeof(header));

    HLock(browsedata);
    memcpy(&ptr[sizeof(header)], *browsedata, datasize);
    memset((char*)((int)ptr + sizeof(header) + datasize), 0, tableoffs - sizeof(header) - datasize);
    HUnlock(browsedata);

    GetBrowseTableInfoAndLock(browsetable, &scan, &entries, NULL);
    ConvertMemToDisk(scan, bptr = ptr + tableoffs, entries);
    memset((char*)bptr + tablesize, 0, totalsize - tableoffs - tablesize);
    OS_UnlockHandle(browsetable);
    OS_UnlockHandle(&h);
    return TRUE;
}
