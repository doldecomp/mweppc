#ifndef MSL_CLANG_PREFIX_H
#define MSL_CLANG_PREFIX_H

// The clang build's prefix for MSL and the runtime's C (-include: tools/mw/clang.py). MSL's x86
// cstdarg finds the variable arguments from the address of the last parameter, which is where the
// caller pushed them only in MWCC's code: clang's are its builtins. These take the place of
// cstdarg's definitions (its include guard keeps it out) and of va_list.h's type.
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

#endif
