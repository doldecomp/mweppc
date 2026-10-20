#ifndef MWCC_HOST_PROBE_ERRNO_H
#define MWCC_HOST_PROBE_ERRNO_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// MSL's errno (cerrno, Win32 multithreaded: a field of the thread-local data). 2.6's sources were
// compiled against Pro 6's MSL headers, whose _ThreadLocalData (ThreadLocalData.h, 2000/08/19)
// starts with a `next` link and so has errno at offset 4, but linked with the older MSL in lib/msl,
// whose errno is the first word (its strtod and stdio store errors at offset 0). So every errno the
// compiler's own code reads or clears is the library's random_next: a bug of the original's build,
// reproduced by declaring Pro 6's layout here.
typedef struct _ThreadLocalData {
    struct _ThreadLocalData* next;
    int errno;
    unsigned long random_next;
    unsigned char* strtok_n;
    unsigned char* strtok_s;
    void* thread_handle;
    struct tm gmtime_tm;
    char asctime_result[26];
    char tmpnam_temp_name[512]; // L_tmpnam (Win32)
} _ThreadLocalData;

_ThreadLocalData* _GetThreadLocalData(void);

#define errno (_GetThreadLocalData()->errno)

#ifdef __cplusplus
}
#endif

#endif
