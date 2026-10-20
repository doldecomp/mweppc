#ifndef DRIVER_STRINGEXTRAS_H
#define DRIVER_STRINGEXTRAS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// The OS layer's string helpers, __stdcall (Wii's publics: strcatn@12, ustrcmp@8 ...)
extern char* __stdcall strcatn(char* d, const char* s, SInt32 max);
extern char* __stdcall strcpyn(char* d, const char* s, SInt32 len, SInt32 max);
extern int __stdcall ustrcmp(const char* src, const char* dst);
extern int __stdcall ustrncmp(const char* src, const char* dst, UInt32 len);

#ifdef __cplusplus
}
#endif

#endif
