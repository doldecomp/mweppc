#ifndef COMPILER_CPREPROCESS_H
#define COMPILER_CPREPROCESS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void CPrep_PreprocessDumpNewLine(void);
extern void CPrep_PreprocessDumpFileInfo(Boolean newLine);
extern void CPrep_PreprocessDumpDirective(char* directive, char* name);
extern void CPrep_PreprocessDumpDirectiveEnd(void);
extern void CPrep_Preprocess(void);

#ifdef __cplusplus
}
#endif

#endif
