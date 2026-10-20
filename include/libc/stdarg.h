#ifndef _STDARG_H
#define _STDARG_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __va_list__
#define __va_list__
typedef char* va_list;
#endif

#ifdef __MWERKS__
#define __va_start(parm) \
    (va_list)((long)(&parm) + ((((long)(&parm + 1) - (long)(&parm)) + 3) / 4 * 4))
#define va_start(ap, parm) ap = __va_start(parm)
#define va_end(ap)
#define va_copy(dest, src) dest = src
#define _Align(type, mask) (sizeof(type) + (mask) & ~(mask))
#define va_arg(ap, type) (*(type*)(((ap) += _Align(type, 3)) - _Align(type, 3)))
#else
#define va_start(ap, parm) __builtin_va_start(ap, parm)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dest, src) __builtin_va_copy(dest, src)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#endif

#ifdef __cplusplus
}
#endif

#endif
