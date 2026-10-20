#ifndef COMPILER_CDECL_H
#define COMPILER_CDECL_H

#include "compiler/common.h"
#include "compiler/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
struct ClassLayout {
    ObjBase** objlist;
    ObjMemberVar* vtable_ivar;
    UInt16 lex_order_count;
    // the lowest index in objlist of a virtual function whose vtable slot is not yet allocated
    // (CABI_AllocateVTable starts there).
    UInt16 firstVirtualSlot; // NOTE: invented name
    Boolean has_vtable;
};

// NOTE: invented names: resolvedObject, resolvedObjects, parameterNames, parameterScope,
// templateType, arrayBound, pendingClass, templateParameters, templateScope, templateFunction,
// requireTemplateClassMember, hasParameterNames, oldStyleParameters, isNewExpression,
// hasArrayDimension, missingTypeSpecifier, isType, parserOption, isConstructor,
// isFriendDeclaration, isNewTypeId, allowZeroLengthArray, allowTemplateArguments, hasTypename,
// isConversionOperator, isTypeID, allowTemplateName, isTemplateName, firstDeclaration,
// interruptflags, reginfo (the members no source names, from what their users do with them: all but
// thetype, qual, nspace, name, expltargs, operator_token, storageclass, section, exportflags,
// in_friend_decl, is_extern_c, has_expltargs, file, file2 and sourceoffset)
struct DeclInfo {
    Type* thetype;
    UInt32 qual;
    NameSpace* nspace;
    HashNameNode* name;
    Object* resolvedObject;
    NameSpaceObjectList* resolvedObjects;
    FuncArg* parameterNames;
    NameSpace* parameterScope;
    Type* templateType;
    ENode* arrayBound;
    TemplClass* pendingClass;
    TemplArg* expltargs;
    TemplParam* templateParameters;
    struct TemplateScopeState* templateScope;
    TemplateFunction* templateFunction;
    // CTempl_ExplicitSpecialization sets it; the specialization lookup clears it once matched
    UInt8 requireTemplateClassMember;
    SInt16 operator_token;
    SInt16 storageclass;
    // a #pragma section's pseudo-section id
    SInt16 section;
    UInt8 exportflags; // EXPORT_FLAGS_*
    Boolean hasParameterNames;
    Boolean oldStyleParameters;
    Boolean isNewExpression;
    Boolean hasArrayDimension;
    Boolean missingTypeSpecifier;
    Boolean isType;
    Boolean parserOption;
    Boolean isConstructor;
    Boolean in_friend_decl;
    Boolean isFriendDeclaration;
    Boolean is_extern_c;
    Boolean isNewTypeId;
    Boolean allowZeroLengthArray;
    Boolean allowTemplateArguments;
    Boolean has_expltargs;
    Boolean hasTypename;
    // the operator CDecl_ParseOperatorDecl parsed is a conversion function
    Boolean isConversionOperator;
    // CParser_ParseTypeID's (C++): `()` is an abstract function declarator, and a dependent
    // qualified name without typename is an error
    Boolean isTypeID;
    // CParser_ParseTypeID's caller accepts a template name without arguments ...
    Boolean allowTemplateName;
    // ... and CParser_GetDeclSpecs found one
    Boolean isTemplateName;
    CPrepFileInfo* file;
    CPrepFileInfo* file2;
    SInt32 sourceoffset;
    // the function's first declaration (CDecl_GetFunctionObject's firstDecl, which
    // IroPointerAnalysis' GetFunctionObjectFromDeclInfo keeps here): CDecl_FunctionDeclarator then
    // warns of a missing prototype
    Boolean firstDeclaration;
    // __declspec(interrupt ...)'s flags, as #pragma interrupt's.
    UInt8 interruptflags;
    // a global register variable's register (see Object.u.data.reginfo)
    ObjRegInfo reginfo;
};

// NOTE: invented names: unused, valid
struct BigDeclInfo {
    DeclInfo declinfo;
    DeclInfo declinfo2;
    // (unused is never read)
    Boolean unused;
    Boolean valid;
};
#pragma pack(pop)

extern AccessType global_access;

extern Type* CDecl_NewStructType(SInt32 size, SInt16 align);
extern Type* CDecl_NewArrayType(Type* type, SInt32 size);
extern Type* CDecl_NewPointerType(Type* type);
extern Type* CDecl_NewRefPointerType(Type* type);
extern TypeTemplDep* CDecl_NewTemplDepType(TypeTemplDepType tdt);
extern void CDecl_SetResultReg(TypeFunc* tfunc);
extern void CDecl_SetFuncFlags(TypeFunc* tfunc, UInt32 flags);
extern void CDecl_NewConvFuncType(DeclInfo* declinfo);
extern void CDecl_CompleteType(Type* type);
extern Boolean IsCompleteType(Type* type);
extern Boolean CanAllocObject(Type* type);
extern Boolean CanCreateObject(Type* type);
extern void makethetypepointer(DeclInfo* declinfo, UInt32 qual);
extern void CDecl_AddThisPointerArgument(TypeFunc* tfunc, TypeClass* tclass);
extern void CDecl_MakePTMFuncType(TypeFunc* tfunc);
extern void CDecl_AddArgument(TypeFunc* tfunc, Type* argtype);
extern Boolean CDecl_CheckArrayIntegr(Type* type);
extern void CDecl_ParseDirectFuncDecl(DeclInfo* declinfo);
extern void makememberpointertype(DeclInfo* declinfo, TypeClass* tclass, UInt32 qual);
extern void scandeclarator(DeclInfo* declinfo);
extern void conversion_type_name(DeclInfo* declinfo);
extern Object* CDecl_GetFunctionObject(
    DeclInfo* declinfo, NameSpace* nspace, Boolean* firstDecl, Boolean matchTemplate
);
extern void CDecl_TypedefDeclarator(DeclInfo* declinfo);
extern Boolean CDecl_FunctionDeclarator(
    DeclInfo* declinfo, NameSpace* nspace, Boolean allowDefinition, Boolean unused
);
extern void scandeclaratorlist(DeclInfo* declinfo);
extern void CDecl_ComputeUnderlyingEnumType(TypeEnum* tenum);
extern void scanenum(DeclInfo* declinfo);
extern void CDecl_ScanStructDeclarator(BigDeclInfo* bde);
extern void scanstruct(DeclInfo* declinfo, short structtype);
extern TypeMemberFunc* CDecl_MakeTypeMemberFunc(
    TypeFunc* tfunc, TypeClass* tclass, Boolean isStatic
);
extern void CDecl_PackDeclInfo(PackedDeclInfo* packed, DeclInfo* declinfo);
extern void CDecl_UnpackDeclInfo(DeclInfo* declinfo, PackedDeclInfo* packed);
extern void CDecl_AddFriend(TypeClass* tclass, Object* friendfunc, TypeClass* friendclass);
// FALSE for a copy constructor taking its class by value (the callers report it)
extern Boolean CDecl_CheckCtorIntegrity(FuncArg* args, TypeClass* tclass);
extern void CDecl_MakeVBaseList(TypeClass* tclass);
extern Boolean CDecl_CheckNewBase(TypeClass* tclass, TypeClass* baseclass, Boolean is_virtual);
extern TypeMemberFunc* CDecl_MakeDefaultDtorType(TypeClass* tclass, Boolean is_virtual);
extern void CDecl_CompleteClass(ClassLayout* decle, TypeClass* tclass);
extern TypeClass* CDecl_DefineClass(
    NameSpace* nspace,
    HashNameNode* name,
    TypeClass* tclass,
    short mode,
    Boolean asTypeName,
    Boolean addName
);
extern void CDecl_ParseClassDeclSpec(UInt8* declspec);
extern void CDecl_ParseClass(
    DeclInfo* declinfo, short mode, Boolean lexAfterBody, UInt8 class_declspec
);

#ifdef __cplusplus
}
#endif

#endif
