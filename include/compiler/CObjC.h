#ifndef COMPILER_COBJC_H
#define COMPILER_COBJC_H

#include "compiler/common.h"
#include "compiler/objc.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Type* cobjc_type_class;
extern Type* cobjc_type_id;
extern Type* cobjc_type_sel;
extern TypeClass* cobjc_currentclass;
extern ObjCSelector** cobjc_selhashtable;
extern BClassList* cobjc_classdefs;
extern ObjCProtocol* cobjc_protocols;
extern SInt32 cobjc_selrefcount;
extern SInt32 cobjc_classrefcount;
extern SInt32 cobjc_stringcount;

extern void CObjC_Setup(void);
extern void CObjC_Cleanup(void);
extern void CObjC_GenerateModule(void);
extern Type* CObjC_GetObjCType_id(Boolean reportMissing);
extern Boolean CObjC_IsType_id(Type* type);
extern Boolean CObjC_IsCompatibleType(Type* a, Type* b);
extern void CObjC_TranslateSelectorToken(void);
extern void CObjC_ParseDefs(TypeStruct* tstruct);
extern Type* CObjC_ParseID(void);
extern Type* CObjC_ParseTypeProtocol(TypeClass* tclass);
extern void CObjC_ParseInterface(void);
extern void CObjC_ParseImplementation(void);
extern void CObjC_ParseProtocol(void);
extern void CObjC_ParseClassDeclaration(void);
extern ENode* CObjC_MakeSendMsgExpr(
    ENode* objexpr,
    TypeClass* tclass,
    ObjCNamedArg* namedArgs,
    ENodeList* unnamedArgs,
    UInt8 calltype,
    Boolean isSuper
);
extern ENode* CObjC_ParseMessageExpression(void);
extern ENode* CObjC_ParseEncodeExpression(void);
extern ENode* CObjC_ParseAtExpression(void);
extern ENode* CObjC_ParseProtocolExpression(void);
extern ENode* CObjC_ParseSelectorExpression(void);

#ifdef __cplusplus
}
#endif

#endif
