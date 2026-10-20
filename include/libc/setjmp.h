#ifndef MWCC_HOST_PROBE_SETJMP_H
#define MWCC_HOST_PROBE_SETJMP_H

#ifdef __cplusplus
extern "C" {
#endif

/* MSL's x86 setjmp (lib/msl/MSL_Common/Include/csetjmp, __INTEL__). */
typedef int jmp_buf[10];
#ifdef __MWERKS__
int _Setjmp(jmp_buf env);
#else
// clang knows setjmp returns twice only by its name: it must not keep values in registers or reuse
// stack slots across the call
__attribute__((returns_twice)) int _Setjmp(jmp_buf env);
#endif
void longjmp(jmp_buf env, int val);
#define setjmp(env) _Setjmp(env)

#ifdef __cplusplus
}
#endif

#endif
