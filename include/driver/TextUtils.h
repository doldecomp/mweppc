#ifndef DRIVER_TEXTUTILS_H
#define DRIVER_TEXTUTILS_H

#include "compiler/common.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

// The Mac Text Utilities the command line emulates
extern StringPtr c2pstr(char* s);
extern char* __stdcall p2cstr(StringPtr s);
extern void __stdcall GetIndString(StringPtr theString, SInt16 strListID, SInt16 index);
extern char* __stdcall getindstring(char* theString, SInt16 strListID, SInt16 index);

#ifdef __cplusplus
}
#endif

#endif
