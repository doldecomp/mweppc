#include "driver/MacFileTypes.h"
#include "driver/CWPlugins.h"
#include "driver/MacSpecs.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include <stdlib.h>
#include <string.h>

static OSFileTypeMappings* defaultList;
static OSFileTypeMappings** fmList = &defaultList;
int(__stdcall* __OS_ExtendedGetMacFileTypeHook)(const OSSpec*, OSType*);

void __stdcall OS_AddFileTypeMappingList(
    OSFileTypeMappings** list, const OSFileTypeMappingList* entry
) {
    OSFileTypeMappings** scan;

    if (!list) {
        list = fmList;
    }

    scan = list;
    while (*scan) {
        scan = &(*scan)->next;
    }

    *scan = malloc(sizeof(OSFileTypeMappings));
    OS_ASSERT(40, *scan != NULL);
    (*scan)->mappingList = entry;
    (*scan)->next = 0;
}

void __stdcall OS_MacType_To_OSType(OSType mactype, uOSTypePair* type) {
    OSFileTypeMappings* list;
    const OSFileTypeMappingList* scan;
    int idx;

    for (list = *fmList; list; list = list->next) {
        scan = list->mappingList;
        for (idx = 0; idx < scan->numMappings; idx++) {
            if (scan->mappings[idx].mactype == mactype) {
                memset(type, 0, sizeof(*type));
                return;
            }
        }
    }

    *type = OS_TEXTTYPE;
}

int __stdcall OS_SetMacFileType(const OSSpec* spec, OSType mactype) {
    uOSTypePair type;
    OS_MacType_To_OSType(mactype, &type);
    return OS_SetFileType(spec, &type);
}

Boolean __stdcall OS_GetMacFileTypeMagic(const char* buffer, int count, OSType* mactype) {
    OSFileTypeMappings* list;
    const OSFileTypeMappingList* scan;
    int idx;

    *mactype = 0;

    for (list = *fmList; list; list = list->next) {
        scan = list->mappingList;
        for (idx = 0; idx < scan->numMappings; idx++) {
            if (scan->mappings[idx].length <= count &&
                scan->mappings[idx].magic &&
                !memcmp(buffer, scan->mappings[idx].magic, scan->mappings[idx].length)) {
                *mactype = scan->mappings[idx].mactype;
                return TRUE;
            }
        }
    }

    return FALSE;
}

int __stdcall OS_GetMacFileType(const OSSpec* spec, OSType* mactype) {
    int ref;
    int err;
    char buffer[32];
    UInt32 count;
    UInt32 flen;
    OSSpec rsrc;
    UInt32 rsize;

    err = OS_Open(spec, OSReadOnly, &ref);
    if (err) {
        return err;
    }

    OS_GetSize(ref, &flen);
    count = sizeof(buffer);
    err = OS_Read(ref, buffer, &count);
    OS_Close(ref);

    if (!err && count) {
        if (OS_GetMacFileTypeMagic(buffer, count, mactype)) {
            return 0;
        }
    }

    if (!__OS_ExtendedGetMacFileTypeHook || !__OS_ExtendedGetMacFileTypeHook(spec, mactype)) {
        if (flen == 0) {
            if (!OS_GetRsrcOSSpec(spec, &rsrc, FALSE)) {
                int ref;
                if (!OS_Open(&rsrc, OSReadOnly, &ref)) {
                    OS_GetSize(ref, &rsize);
                    OS_Close(ref);
                    if (rsize) {
                        *mactype = 'rsrc';
                        return 0;
                    }
                }
            }
        }

        *mactype = 'TEXT';
    }

    return 0;
}

int __stdcall OS_SetMacFileCreatorAndType(const OSSpec* spec, OSType creator, OSType mactype) {
    return OS_SetMacFileType(spec, mactype);
}
