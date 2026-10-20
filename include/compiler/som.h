#ifndef COMPILER_SOM_H
#define COMPILER_SOM_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)

typedef enum SOMMethodState {
    SOMMS_Deleted,
    SOMMS_Method,
    SOMMS_Migrated
} SOMMethodState;

typedef struct SOMReleaseOrder {
    struct SOMReleaseOrder* next;
    HashNameNode* name;
    SOMMethodState state;
} SOMReleaseOrder;

struct SOMInfo {
    TypeClass* metaclass;
    Object* classdataobject;
    SOMReleaseOrder* order;
    UInt32 majorversion;
    UInt32 minorversion;
    UInt8 oidl_callstyle;
};

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif
