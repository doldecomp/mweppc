#ifndef COMPILER_CTEMPLATENEW_H
#define COMPILER_CTEMPLATENEW_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern TemplClass* ctempl_templates;
extern TemplateFunction* ctempl_templatefuncs;
extern TemplStack* ctempl_curinstance;

extern void CTempl_Setup(void);
extern void CTempl_Cleanup(void);
extern TemplArg* CTempl_ParseUncheckTemplArgs(TemplParam* params, Boolean is_global);
extern Type* CTempl_ParseTemplTemplParam(TypeTemplDep* type);
extern Type* CTempl_ClassGetType(TemplClass* templ);
extern Boolean CTempl_IsQualifiedMember(DeclInfo* di, Type* type, NameSpace** resultnspace);
extern void CTempl_Parse(TemplClass* templ, AccessType access);
extern void CTempl_ParseInstanceScopeFunction(
    Object* funcobj, TemplClassInst* inst, TypeClass* tclass
);
extern Boolean CTempl_GenFuncInstance(
    TemplateFunction* templ, TemplFuncInstance* inst, Boolean isExplicit
);
extern void CTempl_InstantiateMember(
    TemplClass* templ,
    TemplClassInst* inst,
    TemplateMember* tmemb,
    Object* object,
    Boolean isExplicit
);
extern Boolean CTempl_Instantiate(void);
extern Boolean CTempl_InlineFunctionCheck(Object* funcobj);

#ifdef __cplusplus
}
#endif

#endif
