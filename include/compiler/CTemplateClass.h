#ifndef COMPILER_CTEMPLATECLASS_H
#define COMPILER_CTEMPLATECLASS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern TemplClass* CTemplClass_GetMasterTemplate(TemplClass* tmclass);
extern void CTemplClass_RegisterUsingDecl(
    TemplClass* tmclass, TypeTemplDep* type, AccessType access
);
extern void CTemplClass_RegisterFriend(TemplClass* tmclass, DeclInfo* di);
extern void CTemplClass_RegisterBaseClass(
    TemplClass* tmclass, Type* type, AccessType access, Boolean is_virtual
);
extern void CTemplClass_RegisterEnumType(TemplClass* tmclass, TypeEnum* enumtype);
extern void CTemplClass_RegisterEnumerator(
    TemplClass* tmclass, ObjEnumConst* objenumconst, ENode* initexpr
);
extern void CTemplClass_RegisterObjectInit(TemplClass* tmclass, Object* object, ENode* initexpr);
extern void CTemplClass_RegisterObjectDef(TemplClass* tmclass, ObjBase* refobj);
extern void CTemplClass_CompleteClass(TemplClass* templ, ClassLayout* de);
extern TemplClassInst* CTemplClass_GetInstance(
    TemplClass* tmclass, TemplArg* inst_args, TemplArg* oargs
);
extern TemplateMember* CTemplClass_DefineMember(
    TemplClass* tmclass, Object* object, FileOffsetInfo* foi, TokenStream* stream
);
extern void CTemplClass_ParsePartialSpecialization(
    TemplateScopeState* templateScope, TemplParam* params, short mode, SInt32* offset
);
extern void CTemplClass_ParseClass(
    TemplateScopeState* templateScope, TemplParam* params, short mode, SInt32* offset
);
extern Boolean CTemplClass_FindPartialTemplate(
    TemplArg* args, TemplClass** resultTempl, TemplArg** resultArgs
);
extern TemplClass* CTemplClass_DefineNestedClass(
    TemplClass* parent, HashNameNode* name, short mode
);
extern Boolean CTempl_InstantiateTemplateClass(TypeClass* tclass);

#ifdef __cplusplus
}
#endif

#endif
