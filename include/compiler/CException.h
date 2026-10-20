#ifndef COMPILER_CEXCEPTION_H
#define COMPILER_CEXCEPTION_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// a temporary whose destructor runs at the end of its full expression
typedef struct DtorTemp {
    struct DtorTemp* next;
    Object* object;
    Object* dtor;
    Object* temp;
} DtorTemp;

extern ExceptionAction* cexcept_dobjstack;
extern Boolean cexcept_hasdobjects;

extern void CExcept_Setup(void);
extern Boolean CExcept_CanThrowException(Object* object, Boolean isVirtualCall);
extern void CExcept_CheckStackRefs(ExceptionAction* actions);
extern void CExcept_CompareSpecifications(ExceptSpecList* a, ExceptSpecList* b);
extern Boolean CExcept_ActionCompare(ExceptionAction* a, ExceptionAction* b);
extern Boolean CExcept_ActionNeedsDestruction(ExceptionAction* action);
extern ENode* CExcept_RegisterDestructorObject(
    Object* local, SInt32 offset, Object* dtor, Boolean unused
);
extern void CExcept_RegisterLocalArray(
    Statement* stmt, Object* localarray, Object* dtor, SInt32 elements, SInt32 element_size
);
extern void CExcept_RegisterDeleteObject(
    Statement* stmt, Object* pointerobject, Object* deletefunc
);
extern void CExcept_Terminate(void);
extern void CExcept_Magic(void);
extern void CExcept_ArrayInit(void);
extern void CExcept_RegisterMember(
    Statement* stmt, Object* objectptr, SInt32 offset, Object* dtor, Object* cond, Boolean isMember
);
extern void CExcept_RegisterMemberArray(
    Statement* stmt,
    Object* objectptr,
    SInt32 offset,
    Object* dtor,
    SInt32 elements,
    SInt32 element_size
);
extern Statement* CExcept_ActionCleanup(ExceptionAction* ea, Statement* stmt);
extern void CExcept_ScanExceptionSpecification(TypeFunc* tfunc);
extern ENode* CExcept_ScanThrowExpression(void);
extern void CExcept_ScanTryBlock(DeclThing* dt, Boolean isCtorOrDtor);
extern void CExcept_ExceptionTansform(Statement* stmt);

#ifdef __cplusplus
}
#endif

#endif
