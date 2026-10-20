#ifndef DRIVER_OPTION_H
#define DRIVER_OPTION_H

#include "compiler/common.h"
#include "compiler/win32.h"
#include "driver/Arguments.h"
#include "driver/option_system.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OptionStackEntry OptionStackEntry;
enum {
    OSTACK_LIST = 1,
    OSTACK_OPTION = 2,
    OSTACK_PARAM = 4,
    OSTACK_SPELLED = 0x20,
    OSTACK_FOLLOWED = 0x40,
    OSTACK_STICKY = 0x80
};

extern char curopt[1024];

extern OptionStackEntry* Args_Pop(short flags);
extern void Args_Push(short flags, void* first, void* second);
extern void Args_AddToToolArgs(PtrList* ta);
extern void Options_Init(void);
extern OptionList* Options_GetOptions(void);
extern void Options_SortOptions(void);
extern int Options_AddList(OptionList* optlst);
extern int Option_ForTool(Option* opt, int which);
extern int Option_ThisTool(void);
extern int Option_ForThisTool(Option* opt);
extern int Option_AlsoPassedFromThisTool(Option* opt);
extern int Options_Parse(OptionList* options, int flags);
extern void Option_ParamError(short id, va_list ap);
extern void Option_ParamWarning(short id, va_list ap);
extern void Option_Warning(VarArgShort id, ...);
extern int Options_DisplayHelp(void);

#ifdef __cplusplus
}
#endif

#endif
