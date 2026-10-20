#ifndef DRIVER_STRINGUTILS_H
#define DRIVER_STRINGUTILS_H

#include "compiler/common.h"
#include "compiler/win32.h"
#include "driver/oslib.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

extern StringPtr _pstrcpy(StringPtr dst, ConstStringPtr src);
extern int pstrcmp(ConstStringPtr a, ConstStringPtr b);
extern int pstrchr(ConstStringPtr str, char find);
extern void __stdcall c2pstrcpy(StringPtr dst, const char* src);
extern void __stdcall p2cstrcpy(char* dst, ConstStringPtr src);
extern char* mvprintf(char* mybuf, unsigned int len, const char* format, va_list va);
extern char* mprintf(char* mybuf, unsigned int len, const char* format, ...);
extern int HPrintF(Handle text, const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif
