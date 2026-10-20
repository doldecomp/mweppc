#ifndef DRIVER_WARNINGHELPERS_H
#define DRIVER_WARNINGHELPERS_H

#ifdef __cplusplus
extern "C" {
#endif

extern int SetWarningFlags(const char* opt, void* str, const char* param, int flags);
extern int DisplayWarningOptions(const char* opt, void* str, const char* param, int flags);

#ifdef __cplusplus
}
#endif

#endif
