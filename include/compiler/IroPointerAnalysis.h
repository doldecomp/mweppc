#ifndef COMPILER_IROPOINTERANALYSIS_H
#define COMPILER_IROPOINTERANALYSIS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void PointerAnalysis_Setup(void);
extern void PointerAnalysis_Cleanup(void);
extern void IRO_AnalyzePointers(Object* function);
extern Boolean PointerAnalysis_IsLinearNodePointerExprDefinite(Object* proc, IROLinear* nd);
extern void PointerAnalysis_LookupLinearNodePointerExpr(
    Object* proc, IROLinear* indirect, IROListNode** list
);
extern void PointerAnalysis_GetFunctionKills(Object* proc, IROLinear* funccall, ObjectList** list);
extern void PointerAnalysis_GetFunctionDependencies(
    Object* proc, IROLinear* funccall, ObjectList** list
);
extern void PointerAnalysis_PragmaMode(void);
extern void PointerAnalysis_ParseEntryPointsToSpecifier(DeclInfo* di);
extern void PointerAnalysis_ParseExitPointsToSpecifier(DeclInfo* di);
extern void PointerAnalysis_ParseFunctionModifiesSpecifier(DeclInfo* di);

#ifdef __cplusplus
}
#endif

#endif
