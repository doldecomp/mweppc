#ifndef MWCC_HOST_PROBE_SIGNAL_H
#define MWCC_HOST_PROBE_SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*__signal_func_ptr)(int);
#define SIGABRT 1
__signal_func_ptr signal(int signal, __signal_func_ptr signal_func);
int raise(int signal);

#ifdef __cplusplus
}
#endif

#endif
