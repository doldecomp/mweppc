#ifndef MWCC_HOST_PROBE_CTYPE_H
#define MWCC_HOST_PROBE_CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char __ctype_map[];
extern unsigned char __lower_map[];
int isalpha(int c);
int isprint(int c);
int toupper(int c);

#ifdef __MWERKS__
#define MSL_CTYPE_INLINE inline
#else
#define MSL_CTYPE_INLINE static inline
#endif
MSL_CTYPE_INLINE int isdigit(int c) {
    return __ctype_map[(int)(unsigned char)(c)] & 0x10;
}
MSL_CTYPE_INLINE int isspace(int c) {
    return __ctype_map[(int)(unsigned char)(c)] & 0x06;
}

#ifndef _MSL_USE_INLINE
#define _MSL_USE_INLINE 1
#endif
#if _MSL_USE_INLINE
MSL_CTYPE_INLINE int tolower(int c) {
    return ((c == -1) ? -1 : ((int)__lower_map[(int)(unsigned char)(c)]));
}
#else
int tolower(int c);
#endif

#ifdef __cplusplus
}
#endif

#endif
