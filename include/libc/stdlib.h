#ifndef MWCC_HOST_PROBE_STDLIB_H
#define MWCC_HOST_PROBE_STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The MSL stdlib functions the compiler links (lib/msl/MSL_Common/Include/cstdlib).
typedef int (*_compare_function)(const void*, const void*);
typedef struct {
    int quot;
    int rem;
} div_t;

void* realloc(void* ptr, size_t size);
int abs(int n);
div_t div(int numerator, int denominator);
void qsort(
    void* table_base, size_t num_members, size_t member_size, _compare_function compare_members
);
void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void abort(void);
void exit(int status);
char* getenv(const char* name);
long strtol(const char* nptr, char** endptr, int base);
int atoi(const char* str);
double strtod(const char* str, char** end);

#ifdef __cplusplus
}
#endif

#endif
