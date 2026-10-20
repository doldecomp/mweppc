#ifndef MWCC_HOST_PROBE_MSL_INTERNAL_H
#define MWCC_HOST_PROBE_MSL_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CatchInfo {
    void* location;
    void* typeinfo;
    void* dtor;
    void* sublocation;
    long pointercopy;
    void* stacktop;
} CatchInfo;

FILE* __find_unopened_file(void);
int __get_file_modes(const char* mode, __file_modes* modes);
int __open_file(const char* name, __file_modes mode, __file_handle* handle);
long double __strtold(
    int max_width,
    int (*ReadProc)(void*, int, int),
    void* ReadProcArg,
    int* chars_scanned,
    int* overflow
);
int read(int fildes, char* buf, int count);
char __throw_catch_compare(const char* throwtype, const char* catchtype, long* offset_result);
void __unexpected(CatchInfo* catchinfo);
void __destroy_global_chain(void);
void __stdio_atexit(void);
fpos_t _ftell(FILE* file);
int __leap_year(int year);
int __add(int* x, int y);
int __ladd(long* x, long y);
int __lmul(long* x, long y);
div_t __div(int x, int y);
int __mod(int x, int y);

int __sformatter(
    int (*ReadProc)(void*, int, int), void* ReadProcArg, const char* format_str, char* arg
);
void __time2tm(unsigned long time, void* tm);
void* alloc_g_SubBlock_split(void* ths, size_t size);
void* allocate_from_fixed_pools(size_t size);
void* _stack_alloc(void);
void time_g_clear_tm(struct tm* tm);
void* alloc_g_Block_subBlock(void* ths, size_t size);
void* alloc_g_link_new_block(size_t size);
char* __strerror(int errnum, char* str);
time_t __get_time(void);
int __to_gm_time(time_t* time);

#ifdef __cplusplus
}
#endif

#endif
