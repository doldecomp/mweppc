#ifndef COMPILER_SCOPES_H
#define COMPILER_SCOPES_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct NameSpace {
    NameSpace* parent;
    HashNameNode* name;
    NameSpaceList* usings;
    TypeClass* theclass;
    NameSpaceName* tparams;
    union {
        NameSpaceName** hash;
        NameSpaceName* list;
    } data;
    UInt32 names;
    Boolean is_hash;
    Boolean is_global;
    Boolean is_unnamed;
    Boolean is_templ;
};
struct NameSpaceList {
    NameSpaceList* next;
    NameSpace* nspace;
};
struct NameSpaceObjectList {
    NameSpaceObjectList* next;
    ObjBase* object;
};
struct NameSpaceName {
    NameSpaceName* next;
    HashNameNode* name;
    NameSpaceObjectList first;
};

#ifdef __cplusplus
}
#endif

#endif
