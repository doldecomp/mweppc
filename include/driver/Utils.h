#ifndef DRIVER_UTILS_H
#define DRIVER_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

extern int my_tolower(char c);
extern int my_isdigit(char c);
extern int my_isalpha(char c);
extern int my_isalnum(char c);
extern int my_isxdigit(char c);
extern char* Utils_SpellList(char* list, char* buffer, char opts);
extern int Utils_CompareOptionString(const char* a, const char* b, int cased, int sticky);

#ifdef __cplusplus
}
#endif

#endif
