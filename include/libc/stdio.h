#include "version.h"
#ifndef MWCC_HOST_PROBE_STDIO_H
#define MWCC_HOST_PROBE_STDIO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The MSL stdio functions the compiler links
// (lib/msl/MSL_Common/Include/cstdio, Win32 configuration).
typedef struct {
    unsigned int open_mode : 2;
    unsigned int io_mode : 3;
    unsigned int buffer_mode : 2;
    unsigned int file_kind : 3;
    unsigned int file_orientation : 2;
    unsigned int binary_io : 1;
} __file_modes;
typedef struct {
    unsigned int io_state : 3;
    unsigned int free_buffer : 1;
    unsigned char eof;
    unsigned char error;
} __file_state;
typedef unsigned long __file_handle;
typedef unsigned long fpos_t;
typedef struct _FILE FILE;
typedef void (*__idle_proc)(void);
typedef int (*__pos_proc)(__file_handle file, fpos_t* position, int mode, __idle_proc idle_proc);
typedef int (*__io_proc)(
    __file_handle file, unsigned char* buff, size_t* count, __idle_proc idle_proc
);
typedef int (*__close_proc)(__file_handle file);

// 0x54 bytes, at the native alignment cstdio sets
struct _FILE {
    __file_handle handle;
    __file_modes mode;
    __file_state state;
    unsigned char is_dynamically_allocated;
    unsigned char char_buffer;
    unsigned char char_buffer_overflow;
    unsigned char ungetc_buffer[2];
    unsigned short ungetwc_buffer[2];
    unsigned long position;
    unsigned char* buffer;
    unsigned long buffer_size;
    unsigned char* buffer_ptr;
    unsigned long buffer_len;
    unsigned long buffer_alignment;
    unsigned long saved_buffer_len;
    unsigned long buffer_pos;
    __pos_proc position_proc;
    __io_proc read_proc;
    __io_proc write_proc;
    __close_proc close_proc;
    __idle_proc idle_proc;
    struct _FILE* next_file_struct;
};

extern FILE __files[];
#define stdin (&__files[0])
#define stdout (&__files[1])
#define stderr (&__files[2])

int fflush(FILE* file);
FILE* freopen(const char* name, const char* mode, FILE* file);
size_t fread(void* ptr, size_t memb_size, size_t num_memb, FILE* file);
size_t fwrite(const void* ptr, size_t memb_size, size_t num_memb, FILE* file);
int _fseek(FILE* file, fpos_t offset, int file_mode);
int fseek(FILE* file, long offset, int mode);
long ftell(FILE* file);
void clearerr(FILE* file);
int __StringRead(void* isc, int ch, int action);
int printf(const char* format, ...);
int fprintf(FILE* file, const char* format, ...);
int sprintf(char* s, const char* format, ...);
int snprintf(char* s, size_t n, const char* format, ...);
int vfprintf(FILE* file, const char* format, char* arg);
int vsprintf(char* s, const char* format, char* arg);
int vsnprintf(char* s, size_t n, const char* format, char* arg);
int sscanf(const char* s, const char* format, ...);
FILE* fopen(const char* filename, const char* mode);
int fclose(FILE* file);
// MSL's buffering modes (its setvbuf flushes and drops the buffer for 0)
#define _IONBF 0
#define _IOLBF 1
#define _IOFBF 2

int setvbuf(FILE* file, char* buff, int mode, size_t size);
int fputc(int c, FILE* file);
int fputs(const char* s, FILE* file);

#ifdef __cplusplus
}
#endif

#endif
