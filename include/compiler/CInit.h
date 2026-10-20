#ifndef COMPILER_CINIT_H
#define COMPILER_CINIT_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*InitExprRegisterCB)(ENode* expr);
typedef void (*InsertExprCB)(ENode* expr);
typedef ENode* (*RegisterObjectCB)(Type* type, Object* obj, SInt32 offset, Boolean flag);
typedef ENode* (*TempNodeCB)(Type* type, Boolean flag);

// a relocation of initialized data: the word at `offset` holds the address of `obj` plus `addend`
struct OLinkList {
    struct OLinkList* next;
    struct Object* obj;
    SInt32 offset;
    SInt32 addend; // NOTE: invented name
};

#pragma pack(push, 2)
typedef struct PooledString {
    struct PooledString* next;
    Object* obj;
    int offset;
    char* data;
    SInt32 size;
    Boolean ispascal;
    Boolean iswide;
} PooledString;
#pragma pack(pop)

extern TempNodeCB cinit_tempnodefunc;

extern void CInit_Init(void);
extern Boolean CInit_RelocInitCheck(
    ENode* expr, Object** objptr, CInt64* valptr, Boolean allowLocal
);
extern void CInit_ExportConst(Object* obj);
extern Statement* CInit_ConstructClassArray(
    Statement* stmt, TypeClass* tclass, Object* ctor, Object* dtor, ENode* firstarg, SInt32 count
);
extern ENode* CInit_AutoObject(Object* obj, Type* type, UInt32 qual);
extern void CInit_InitializeAutoData(
    Object* obj, InsertExprCB insert_cb, RegisterObjectCB register_cb
);
extern void CInit_InitializeStaticData(Object* obj, InitExprRegisterCB cb);
extern void CInit_InitializeData(Object* obj);
extern Object* CInit_DeclareString(char* data, SInt32 size, Boolean ispascal, Boolean iswide);
extern PooledString* CInit_DeclarePooledString(char* data, SInt32 size, Boolean ispascal);
extern void CInit_RewriteString(ENode* expr, Boolean unused);
extern void CInit_DeclarePooledStrings(void);
extern void CInit_DeclareData(Object* obj, void* data, OLinkList* list, SInt32 size);
extern void CInit_DeclareReadOnlyData(Object* obj, void* data, OLinkList* list, SInt32 size);
extern void CInit_DefineTentativeData(void);

#ifdef __cplusplus
}
#endif

#endif
