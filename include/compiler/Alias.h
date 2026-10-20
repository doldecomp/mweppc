#ifndef COMPILER_ALIAS_H
#define COMPILER_ALIAS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// 2-byte packing: an Alias is 0x2e bytes (the lalloc in create_alias), its byte-sized AliasType
// last
#pragma pack(push, 2)
typedef struct AliasMember AliasMember;

// NOTE: invented names: AliasType_Object, AliasType_ObjectPart, AliasType_Set, membervec
// An Alias is a whole object, a part of one (offset, size) or a set of aliases; `membervec` is a
// set's members as a bit vector over alias indices
typedef enum AliasType {
    AliasType_Object,
    AliasType_ObjectPart,
    AliasType_Set
} AliasType;

struct Alias {
    Alias* next;
    Alias* hashNext;
    AliasMember* children;
    AliasMember* parents;
    Object* object;
    SInt32 offset;
    SInt32 size;
    int valuenumber;
    PCode* valuepcode;
    UInt32* membervec;
    int index;
    AliasType type;
};

struct AliasMember {
    AliasMember* nextParent;
    AliasMember* nextChild;
    Alias* parent;
    Alias* child;
};
#pragma pack(pop)

extern Alias* worst_case_ptr;
#if VERSION >= VERSION_GC_2_5
extern Alias* worst_case_func;
#endif
extern Object worst_case_obj;

extern void initialize_aliases(void);
extern Alias* make_alias(Object* object, SInt32 offset, SInt32 size);
#if VERSION < VERSION_GC_2_5
extern Alias* make_distinct_virtual_alias(Object* obj, SInt32 offset, SInt32 size);
#else
extern Alias* make_distinct_virtual_alias(Object* obj);
#endif
extern void gather_alias_info(void);
extern Boolean may_alias(PCode* a, PCode* b);
#if VERSION == VERSION_GC_2_0P2
extern Boolean is_direct_small_data_access(PCode* pcode);
extern Boolean may_alias_scheduling(PCode* a, PCode* b);
extern void update_small_data_values(PCode* pcode);
#endif
#if VERSION < VERSION_GC_2_5
extern Boolean uniquely_aliases(PCode* a, PCode* b);
#endif
extern Boolean may_alias_object(PCode* pcode, Object* object);
#if VERSION >= VERSION_GC_2_5
extern Boolean must_alias_object(PCode* pcode, Object* object);
#endif
extern void initialize_alias_values(void);
extern void update_alias_value(Alias* alias, PCode* pcode);
extern void update_all_alias_values(void);

#ifdef __cplusplus
}
#endif

#endif
