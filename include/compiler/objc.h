#ifndef COMPILER_OBJC_H
#define COMPILER_OBJC_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// The Objective-C records: classes, categories, protocols, methods and selectors.

struct ObjCMethodArg {
    ObjCMethodArg* next;
    HashNameNode* selector;
    HashNameNode* name;
    Type* type;
    UInt32 qual;
};

struct ObjCMethodList {
    ObjCMethodList* next;
    ObjCMethod* method;
};

struct ObjCSelector {
    ObjCSelector* next;
    Object* selobject;
    HashNameNode* name;
    ObjCMethodList* methods;
};

struct ObjCMethod {
    ObjCMethod* next;
    Object* object;
    TypeFunc* functype;
    ObjCSelector* selector;
    Type* return_type;
    UInt32 return_qual;
    ObjCMethodArg* selector_args;
    Boolean has_valist;
    Boolean is_class_method;
    Boolean is_defined;
};

struct ObjCProtocol {
    ObjCProtocol* next;
    HashNameNode* name;
    ObjCProtocolList* protocols;
    ObjCMethod* methods;
    Object* object;
};

struct ObjCProtocolList {
    ObjCProtocolList* next;
    ObjCProtocol* protocol;
};

struct ObjCCategory {
    ObjCCategory* next;
    HashNameNode* name;
    ObjCProtocolList* protocols;
    ObjCMethod* methods;
};

// 2-byte packed: ObjCInfo is 0x1a bytes (is_implemented at 0x18)
#pragma pack(push, 2)
struct ObjCInfo {
    Object* classobject;
    Object* metaobject;
    Object* classrefobj;
    ObjCMethod* methods;
    ObjCProtocolList* protocols;
    ObjCCategory* categories;
    Boolean is_implemented;
};
#pragma pack(pop)

struct ObjCNamedArg {
    ObjCNamedArg* next;
    HashNameNode* name;
    ENode* expr;
};

#ifdef __cplusplus
}
#endif

#endif
