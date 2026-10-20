#ifndef DRIVER_HELP_H
#define DRIVER_HELP_H

#include "driver/option_system.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int Help_Option(OptionList* lst, Option* opt, int subprint, const char* keyword);
extern void Help_Options(OptionList* lst, int subprint, const char* keyword);
extern void Help_Usage(void);
extern void Help_Init(void);
extern void Help_Line(char ch);
extern void Help_Term(void);

#ifdef __cplusplus
}
#endif

#endif
