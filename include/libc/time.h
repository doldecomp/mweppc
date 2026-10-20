#ifndef MWCC_HOST_PROBE_TIME_H
#define MWCC_HOST_PROBE_TIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long time_t;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

typedef unsigned long clock_t;

clock_t clock(void);
time_t time(time_t* timer);
struct tm* gmtime(const time_t* timer);
struct tm* localtime(const time_t* timer);
time_t mktime(struct tm* timeptr);
size_t strftime(char* str, size_t max_size, const char* format_str, const struct tm* timeptr);

#ifdef __cplusplus
}
#endif

#endif
