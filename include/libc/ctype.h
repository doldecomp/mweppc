#ifndef MWCC_HOST_PROBE_CTYPE_H
#define MWCC_HOST_PROBE_CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

/* The MSL ctype functions the compiler links (lib/msl/MSL_Common/Include/cctype). */
extern unsigned char __ctype_map[];
extern unsigned char __lower_map[];
int isalpha(int c);
int isprint(int c);
int toupper(int c);

// MSL's cctype defines isdigit and isspace inline. clang's copies are static: under the GNU89
// inline rules it compiles the compiler with, each unit would define them, as ctype.c does
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

// The compiler's units inline MSL's tolower (2.6 was built against Pro 6's MSL headers); the
// command-line driver's units (Generic, CLToolExec, StringExtras) call ctype.c's, as Pro 5's C
// headers declare it, and are built with -D_MSL_USE_INLINE=0.
// TODO: Pro 6's cctype is not at hand: the switch's name is MSL's later one, and what made the
// driver's library call tolower out of line (its own prefix file, older headers) is not known
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
