#ifndef COMPILER_CCLASS_H
#define COMPILER_CCLASS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void CClass_Init(void);
extern void CClass_GenThunks(void);
extern UInt8 CClass_GetOverrideKind(TypeFunc* a, TypeFunc* b, Boolean errorflag);
extern Boolean CClass_IsEmpty(TypeClass* tclass);
extern Object* CClass_DefaultConstructor(TypeClass* tclass);
extern Object* CClass_DummyDefaultConstructor(TypeClass* tclass);
extern ENode* CClass_DefaultConstructorCall(
    TypeClass* tclass,
    TypeClass* derived,
    ENode* objexpr,
    SInt32 varg,
    Boolean checkAccess,
    Boolean isVirtualBase,
    Boolean* errorflag
);
extern Object* CClass_AssignmentOperator(TypeClass* tclass);
extern Object* CClass_CopyConstructor(TypeClass* tclass);
extern NameSpaceObjectList* CClass_MemberObject(TypeClass* tclass, HashNameNode* name);
extern NameSpaceObjectList* CClass_Constructor(TypeClass* tclass);
extern Object* CClass_Destructor(TypeClass* tclass);
extern Boolean CClass_IsConstructor(Object* obj);
extern Boolean CClass_IsDestructor(Object* obj);
extern Boolean CClass_IsPODClass(TypeClass* tclass);
extern Boolean CClass_IsTrivialCopyClass(TypeClass* tclass);
extern Boolean CClass_IsTrivialCopyAssignClass(TypeClass* tclass);
extern Boolean CClass_ReferenceArgument(TypeClass* tclass);
extern BClassList* CClass_GetPathCopy(BClassList* path, Boolean is_global);
extern BClassList* CClass_AppendPath(BClassList* dest, BClassList* src);
extern Boolean CClass_IsMoreAccessiblePath(BClassList* path1, BClassList* path2);
extern BClassList* CClass_GetBasePath(
    TypeClass* a, TypeClass* b, short* founddepth, Boolean* isambigbase
);
extern Boolean CClass_IsBaseClass(
    TypeClass* a, TypeClass* b, short* founddepth, Boolean pathcheckflag, Boolean ambigerrorflag
);
extern TypeClass* CClass_GetQualifiedClass(void);
extern ENode* CClass_AccessPathCast(BClassList* path, ENode* expr, Boolean reverse);
extern ENode* CClass_ClassPointerCast(
    ENode* expr,
    TypeClass* a,
    TypeClass* b,
    Boolean typconflag,
    Boolean ambigerrorflag,
    Boolean pathcheckflag
);
extern ENode* CClass_DirectBasePointerCast(ENode* expr, TypeClass* a, TypeClass* b);
extern SInt32 CClass_GetPathOffset(BClassList* path);
extern Boolean CClass_ClassDominates(TypeClass* tclass, TypeClass* baseclass);
extern SInt32 CClass_VirtualBaseOffset(TypeClass* tclass, TypeClass* baseclass);
extern SInt32 CClass_VirtualBaseVTableOffset(TypeClass* tclass, TypeClass* baseclass);
extern Boolean CClass_OverridesBaseMember(TypeClass* tclass, HashNameNode* name, Object* obj);
extern void CClass_DefineCovariantFuncs(Object* method, CI_FuncData* ifuncdata);
extern void CClass_CheckOverrides(TypeClass* tclass);
extern void CClass_ClassDefaultFuncAction(TypeClass* tclass);
extern void CClass_ClassAction(TypeClass* tclass);
extern void CClass_MakeStaticActionClass(TypeClass* tclass);
extern Object* CClass_CheckPures(TypeClass* tclass);
extern void CClass_MemberDef(Object* obj, TypeClass* tclass);
extern Object* CClass_ThisSelfObject(void);
extern ENode* CClass_CreateThisSelfExpr(void);
extern void CClass_CheckPathAccess(BClassList* path, Object* obj, AccessType access);
extern void CClass_CheckStaticAccess(BClassList* path, TypeClass* tclass, AccessType access);
extern void CClass_CheckObjectAccess(BClassList* path, Object* obj);
extern void CClass_CheckEnumAccess(BClassList* path, ObjEnumConst* objec);
extern Type* CClass_CombineClassAccessQualifiers(
    Type* type, UInt32 qual1, UInt32 qual2, UInt32* outflags
);
extern ENode* CClass_AccessMember(ENode* classexpr, Type* type, UInt32 qual, SInt32 offset);

#ifdef __cplusplus
}
#endif

#endif
