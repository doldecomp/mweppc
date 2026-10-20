#ifndef DRIVER_MACFILETYPES_H
#define DRIVER_MACFILETYPES_H

#include "compiler/common.h"
#include "driver/CWPlugins.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OSFileTypeMappings {
    const OSFileTypeMappingList* mappingList;
    struct OSFileTypeMappings* next;
} OSFileTypeMappings;
extern void __stdcall OS_AddFileTypeMappingList(
    OSFileTypeMappings** list, const OSFileTypeMappingList* entry
);
extern int __stdcall OS_SetMacFileType(const OSSpec* spec, OSType mactype);
extern Boolean __stdcall OS_GetMacFileTypeMagic(const char* buffer, int count, OSType* mactype);
extern int __stdcall OS_GetMacFileType(const OSSpec* spec, OSType* mactype);
extern int __stdcall OS_SetMacFileCreatorAndType(
    const OSSpec* spec, OSType creator, OSType mactype
);

#ifdef __cplusplus
}
#endif

#endif
