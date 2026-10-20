#ifndef MWCC_HOST_PROBE_ERRNO_H
#define MWCC_HOST_PROBE_ERRNO_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ThreadLocalData {
    struct _ThreadLocalData* next;
    int errno;
    unsigned long random_next;
    unsigned char* strtok_n;
    unsigned char* strtok_s;
    void* thread_handle;
    struct tm gmtime_tm;
    char asctime_result[26];
    char tmpnam_temp_name[512];
} _ThreadLocalData;

_ThreadLocalData* _GetThreadLocalData(void);

#define errno (_GetThreadLocalData()->errno)

#ifdef __cplusplus
}
#endif

#endif
