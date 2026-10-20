#ifndef MWCC_HOST_PROBE_STRING_H
#define MWCC_HOST_PROBE_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The MSL string functions the compiler links (lib/msl/MSL_Common/Include/cstring). */
void* memset(void* dst, int val, size_t len);
void* memchr(const void* src, int val, size_t len);
int memcmp(const void* src1, const void* src2, size_t len);
void* memcpy(void* destination, const void* source, size_t size);
void* memmove(void* dst, const void* src, size_t len);
size_t strlen(const char* str);
char* strcpy(char* dst, const char* src);
char* strncpy(char* dst, const char* src, size_t len);
char* strcat(char* dst, const char* src);
char* strncat(char* dst, const char* src, size_t len);
int strcmp(const char* str1, const char* str2);
int strncmp(const char* str1, const char* str2, size_t len);
char* strchr(const char* str, int chr);
char* strrchr(const char* str, int chr);
char* strerror(int errnum);
char* strstr(const char* str, const char* pat);
char* strpbrk(const char* str, const char* set);

#ifdef __cplusplus
}
#endif

#endif
