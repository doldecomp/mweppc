#ifndef COMPILER_CBROWSE_H
#define COMPILER_CBROWSE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Boolean gForceSourceLoc;

extern void CBrowse_Setup(CompilerLinkerParamBlk* params);
extern void CBrowse_Finish(CompilerLinkerParamBlk* params);
extern void CBrowse_Cleanup(CompilerLinkerParamBlk* params);
extern void CBrowse_BeginClass(DeclInfo* di, GList* gl);
extern void CBrowse_AddClassMemberVar(ObjMemberVar* ivar, SInt32 startOffset, SInt32 endOffset);
extern void CBrowse_AddClassMemberFunction(Object* object, SInt32 startOffset, SInt32 endOffset);
extern void CBrowse_AddClassMemberData(Object* object, SInt32 startOffset, SInt32 endOffset);
extern void CBrowse_EndClass(SInt32 offset, GList* gl);
extern void CBrowse_BeginStruct(DeclInfo* di, TypeStruct* tstruct, GList* gl);
extern void CBrowse_AddStructMember(StructMember* member, SInt32 startOffset, SInt32 endOffset);
extern void CBrowse_EndStruct(SInt32 offset, GList* gl);
extern void CBrowse_NewTypedef(
    NameSpace* nspace,
    HashNameNode* name,
    CPrepFileInfo* file1,
    CPrepFileInfo* file2,
    SInt32 startOffset,
    SInt32 endOffset
);
extern void CBrowse_NewEnum(
    NameSpace* nspace,
    HashNameNode* name,
    CPrepFileInfo* file1,
    CPrepFileInfo* file2,
    SInt32 startOffset,
    SInt32 endOffset
);
extern void CBrowse_NewEnumConstant(
    NameSpace* nspace,
    HashNameNode* name,
    CPrepFileInfo* file1,
    CPrepFileInfo* file2,
    SInt32 startOffset,
    SInt32 endOffset
);
extern void CBrowse_NewFunction(
    Object* object, CPrepFileInfo* file1, CPrepFileInfo* file2, SInt32 startOffset, SInt32 endOffset
);
extern void CBrowse_NewData(
    Object* object, CPrepFileInfo* file1, CPrepFileInfo* file2, SInt32 startOffset, SInt32 endOffset
);
extern void CBrowse_NewMacro(
    Macro* macro, CPrepFileInfo* file, SInt32 startOffset, SInt32 endOffset
);
extern void CBrowse_NewTemplateClass(
    TemplClass* tmclass, CPrepFileInfo* file, SInt32 startOffset, SInt32 endOffset
);
extern void CBrowse_NewTemplateFunc(TemplateFunction* tmfunc);

#ifdef __cplusplus
}
#endif

#endif
