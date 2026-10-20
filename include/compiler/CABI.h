#ifndef COMPILER_CABI_H
#define COMPILER_CABI_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum CABIDestroyMode {
    CABIDestroyBase = 0,
    CABIDestroyComplete = 1,
    CABIDestroyDelete = 2,
    CABIDestroySDelete = 3
} CABIDestroyMode;

extern short CABI_GetStructResultArgumentIndex(TypeFunc* tfunc);
extern Type* CABI_GetSizeTType(void);
extern Type* CABI_GetPtrDiffTType(void);
extern SInt16 CABI_StructSizeAlignValue(Type* type, SInt32 size);
extern void CABI_ReverseBitField(TypeBitfield* tbitfield);
extern void CABI_AddVTable(TypeClass* tclass);
extern SInt32 CABI_GetVTableOffset(TypeClass* tclass);
extern void CABI_LayoutClass(ClassLayout* layout, TypeClass* tclass);
extern void CABI_MakeDefaultArgConstructor(TypeClass* tclass, Object* func);
extern ENode* CABI_MakeThisExpr(TypeClass* tclass, SInt32 offset);
extern SInt32 CABI_GetCtorOffsetOffset(TypeClass* tclass, TypeClass* baseclass);
extern Object* CABI_ConstructorCallsNew(TypeClass* tclass);
typedef Statement* (*TransConstructorCallback)(
    Statement* stmt, TypeClass* tclass, TypeClass* base, SInt32 offset, Boolean flag
);
extern void CABI_TransConstructor(
    Object* obj,
    Statement* firstStmt,
    TypeClass* tclass,
    TransConstructorCallback callback,
    Boolean has_try
);
extern void CABI_MakeDefaultConstructor(TypeClass* tclass, Object* func);
extern void CABI_MakeDefaultCopyConstructor(TypeClass* tclass, Object* func);
extern void CABI_MakeDefaultAssignmentOperator(TypeClass* tclass, Object* func);
extern void CABI_TransDestructor(
    Object* dtor, Object* func, Statement* stmt, TypeClass* tclass, CABIDestroyMode mode
);
extern void CABI_MakeDefaultDestructor(TypeClass* tclass, Object* func);
extern Object* CABI_GetDestructorObject(Object* obj, CABIDestroyMode mode);
extern ENode* CABI_DestroyObject(
    Object* dtor, ENode* objexpr, CABIDestroyMode mode, Boolean nonvirtual, Boolean dealloc
);

#ifdef __cplusplus
}
#endif

#endif
