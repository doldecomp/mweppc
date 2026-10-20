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
    UInt16 firstVirtualSlot;
    Boolean has_vtable;
};

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
    UInt8 requireTemplateClassMember;
    SInt16 operator_token;
    SInt16 storageclass;
    SInt16 section;
    UInt8 exportflags;
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
    Boolean isConversionOperator;
    Boolean isTypeID;
    Boolean allowTemplateName;
    Boolean isTemplateName;
    CPrepFileInfo* file;
    CPrepFileInfo* file2;
    SInt32 sourceoffset;
    Boolean firstDeclaration;
    UInt8 interruptflags;
    ObjRegInfo reginfo;
};

struct BigDeclInfo {
    DeclInfo declinfo;
    DeclInfo declinfo2;
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
