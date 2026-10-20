#ifndef COMPILER_EXCEPTIONS_H
#define COMPILER_EXCEPTIONS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ExceptionActionType {
    EAT_NOP,
    EAT_DESTROYLOCAL,
    EAT_DESTROYLOCALCOND,
    EAT_DESTROYLOCALOFFSET,
    EAT_DESTROYLOCALPOINTER,
    EAT_DESTROYLOCALARRAY,
    EAT_DESTROYPARTIALARRAY,
    EAT_DESTROYMEMBER,
    EAT_DESTROYMEMBERCOND,
    EAT_DESTROYMEMBERARRAY,
    EAT_DELETEPOINTER,
    EAT_DELETELOCALPOINTER,
    EAT_DELETEPOINTERCOND,
    EAT_CATCHBLOCK,
    EAT_ACTIVECATCHBLOCK,
    EAT_SPECIFICATION,
    EAT_TERMINATE,
    EAT_DESTROYBASE,
    EAT_NACTIONS
} ExceptionActionType;

#pragma pack(push, 2)
struct ExceptionAction {
    struct ExceptionAction* prev;
    union {
        struct {
            Object* local;
            Object* dtor;
        } destroy_local;
        struct {
            Object* local;
            Object* cond;
            Object* dtor;
        } destroy_local_cond;
        struct {
            Object* local;
            Object* dtor;
            SInt32 offset;
        } destroy_local_offset;
        struct {
            Object* pointer;
            Object* dtor;
        } destroy_local_pointer;
        struct {
            Object* localarray;
            Object* dtor;
            SInt32 elements;
            SInt32 element_size;
        } destroy_local_array;
        struct {
            Object* arraypointer;
            Object* arraycounter;
            Object* dtor;
            Object* element_size;
        } destroy_partial_array;
        struct {
            Object* objectptr;
            Object* dtor;
            SInt32 offset;
        } destroy_base;
        struct {
            Object* objectptr;
            Object* dtor;
            SInt32 offset;
        } destroy_member;
        struct {
            Object* objectptr;
            Object* cond;
            Object* dtor;
            SInt32 offset;
        } destroy_member_cond;
        struct {
            Object* objectptr;
            Object* dtor;
            SInt32 offset;
            SInt32 elements;
            SInt32 element_size;
        } destroy_member_array;
        struct {
            Object* pointerobject;
            Object* deletefunc;
        } delete_pointer;
        struct {
            Object* pointerobject;
            Object* deletefunc;
            Object* cond;
        } delete_pointer_cond;
        struct {
            Object* catch_object;
            Object* catch_info_object;
            CLabel* catch_label;
            Object* catch_typeid;
            Type* catch_type;
            UInt32 catch_qual;
        } catch_block;
        struct {
            Object* catch_info_object;
            Boolean call_dtor;
        } active_catch_block;
        struct {
            SInt32 unexp_ids;
            Object** unexp_id;
            CLabel* unexp_label;
            Object* unexp_info_object;
        } specification;
    } data;
    ExceptionActionType type;
};
#pragma pack(pop)

extern void initializeexceptiontables(void);
extern int countexceptionactionregisters(ExceptionAction* actions);
extern void noteexceptionactionregisters(ExceptionAction* actions, PCodeArg* ops);
extern void recordexceptionactions(PCode* instr, ExceptionAction* actions);
extern void dumpexceptiontables(Object* function, SInt32 codesize);

#ifdef __cplusplus
}
#endif

#endif
