#ifndef COMPILER_SWITCH_H
#define COMPILER_SWITCH_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct SwitchCase {
    struct SwitchCase* next;
    struct CLabel* label;
    CInt64 min;
};

struct SwitchInfo {
    struct SwitchCase* cases;
    struct CLabel* defaultlabel;
    Type* exprtype;
};

extern ObjectList* switchtables;

extern void switchstatement(ENode* expr, struct SwitchInfo* info);
extern void dumpswitchtables(Object* funcobj);

#ifdef __cplusplus
}
#endif

#endif
