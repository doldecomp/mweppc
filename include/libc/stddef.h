#ifndef MWCC_HOST_PROBE_STDDEF_H
#define MWCC_HOST_PROBE_STDDEF_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int size_t;
typedef int ptrdiff_t;
#ifndef NULL
#define NULL 0L
#endif
#ifdef __clang__
#define offsetof(type, member) __builtin_offsetof(type, member)
#else
#define offsetof(type, member) ((size_t)&(((type*)0)->member))
#endif

#ifdef __cplusplus
}
#endif

#endif
