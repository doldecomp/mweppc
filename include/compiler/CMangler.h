#ifndef COMPILER_CMANGLER_H
#define COMPILER_CMANGLER_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern HashNameNode* constructor_name_node;
extern HashNameNode* destructor_name_node;
extern HashNameNode* asop_name_node;

extern void CMangler_Setup(void);
extern HashNameNode* CMangler_VBaseDtorName(void);
extern HashNameNode* CMangler_SDeleteDtorName(void);
extern HashNameNode* CMangler_DeleteDtorName(void);
extern char* CMangler_GetOperator(HashNameNode* name);
extern HashNameNode* CMangler_OperatorName(short token);
extern HashNameNode* CMangler_VTableName(TypeClass* theclass);
extern HashNameNode* CMangler_RTTIObjectName(Type* type, UInt32 qual);
extern HashNameNode* CMangler_ThunkName(
    Object* vfunc, SInt32 this_delta, SInt32 return_delta, SInt32 ctoroffset
);
extern HashNameNode* CMangler_TemplateInstanceName(HashNameNode* basename, TemplArg* args);
extern void CMangler_MangleType(Type* type, UInt32 qual);
extern HashNameNode* CMangler_ConversionFuncName(Type* type, UInt32 qual);
extern HashNameNode* CMangler_GetCovariantFunctionName(Object* dobj, TypeClass* theclass);
extern HashNameNode* CMangler_GetLinkName(Object* obj);

#ifdef __cplusplus
}
#endif

#endif
