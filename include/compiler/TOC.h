#ifndef COMPILER_TOC_H
#define COMPILER_TOC_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
struct CodeLabelList {
    struct CodeLabelList* next;
    struct Object* object;
    struct CLabel* label;
};

typedef struct PoolEntry {
    struct PoolEntry* next;
    Object* object;
    SInt32 offset;
    void* buffer;
} PoolEntry;

typedef struct AnchorEntry {
    struct AnchorEntry* next;
    Object* object;
    struct ObjGenSymbolLink* symbolLink;
    Boolean full;
} AnchorEntry;

typedef struct COVCResult {
    short op1;
    short op2;
    char arg;
} COVCResult;
#pragma pack(pop)

extern ObjectList* toclist;
extern ObjectList* exceptionlist;
extern Object toc0;
extern CodeLabelList* codelabellist;
extern AnchorEntry* anchorlist;

extern void setupaddressing(void);
extern void reset_anchor_varinfo(void);
extern Boolean used_anchor(Object* obj);
extern Boolean is_pooled_object(Object* obj);
extern Object* createIndirect(Object* obj, Boolean newVarInfo, Boolean reference);
extern Object* createfloatconstant(Type* type, Float* data);
extern void DeclarePooledConstants(void);
extern void dumpcodelabels(Object* func);
extern Boolean canoptimizevectorconst(MWVector128* vecp, Type* type, COVCResult* result);
extern Boolean TOC_use_fsel(ENode* expr);
extern Boolean TOC_use_isel(ENode* expr, Boolean force);
extern SInt32 GetSizeSkip(ENode* expr);
extern void expandTOCreferences(Statement** stmts);
extern Object* createstaticinitobject(void);

#ifdef __cplusplus
}
#endif

#endif
