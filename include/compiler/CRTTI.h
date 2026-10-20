#ifndef COMPILER_CRTTI_H
#define COMPILER_CRTTI_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern OLinkList* CRTTI_ConstructVTableHeaders(TypeClass* tclass, void* data, OLinkList* links);
extern ENode* CRTTI_ParseTypeID(void);
extern ENode* CRTTI_Parse_dynamic_cast(void);
extern ENode* CRTTI_Parse_static_cast(void);
extern ENode* CRTTI_Parse_reinterpret_cast(void);
extern ENode* CRTTI_Parse_const_cast(void);

#ifdef __cplusplus
}
#endif

#endif
