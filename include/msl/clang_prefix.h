#ifndef MSL_CLANG_PREFIX_H
#define MSL_CLANG_PREFIX_H

#define __cstdarg__

#ifndef __va_list__
#define __va_list__
typedef char* va_list;
#endif

#define va_start(ap, parm) __builtin_va_start(ap, parm)
#define __va_start(parm) \
    ({ \
        va_list __ap; \
        __builtin_va_start(__ap, parm); \
        __ap; \
    })
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dest, src) __builtin_va_copy(dest, src)
#define va_arg(ap, type) __builtin_va_arg(ap, type)

#ifdef MWCC_LINUX
__attribute__((weak)) unsigned long long __stdcall Int64ShllMod32(
    unsigned long long, unsigned long
);
__attribute__((weak)) long long __stdcall Int64ShraMod32(long long, unsigned long);
__attribute__((weak)) unsigned long long __stdcall Int64ShrlMod32(
    unsigned long long, unsigned long
);
__attribute__((weak)) double scalbn(double, int);
#endif

#endif
