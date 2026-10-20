#ifndef COMPILER_COBJCMODERN_H
#define COMPILER_COBJCMODERN_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern ENode* CObjC_CheckModernSendMessage(TypeClass* tclass, ENode* expr);
extern ENode* CObjC_New(TypeClass* tclass);
extern ENode* CObjC_Delete(TypeClass* tclass, ENode* objexpr);

#ifdef __cplusplus
}
#endif

#endif
