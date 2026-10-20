#ifndef DRIVER_IO_H
#define DRIVER_IO_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void ShowTextHandle(const char* description, Handle text);
extern void ShowVersion(Boolean decorate);

#ifdef __cplusplus
}
#endif

#endif
