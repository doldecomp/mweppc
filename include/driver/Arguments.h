#ifndef DRIVER_ARGUMENTS_H
#define DRIVER_ARGUMENTS_H

#include "compiler/common.h"
#include "driver/cmdline.h"
#include "driver/option_system.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PtrList {
    int argc;
    int nargv;
    char** argv;
} PtrList;

enum {
    COMPAT_DEFAULT,
    COMPAT_DOS,
    COMPAT_CUSTOM
};

extern char compat;
extern char* MAINOPTCHAR;
extern char* SEPOPTSTR;
extern PtrList linkargs;
extern PtrList prelinkargs;
extern PtrList postlinkargs;

extern void Arg_Init(int theargc, char** theargv);
extern void Arg_Terminate(void);
extern void Arg_Reset(void);
extern ArgToken* Arg_PeekToken(void);
extern ArgToken* Arg_UsedToken(void);
extern int Arg_IsEmpty(void);
extern ArgToken* Arg_GetToken(void);
extern ArgToken* Arg_UndoToken(void);
extern const char* Arg_GetTokenName(ArgToken* tok);
extern const char* Arg_GetTokenText(ArgToken* tok, char* buffer, int maxlen, unsigned char warn);
extern void Arg_InitToolArgs(PtrList* ta);
extern void Arg_AddToToolArgs(PtrList* ta, short tokval, char* toktxt);
extern void Arg_ToolArgsForPlugin(PtrList* ta, struct CWCommandLineArgs* args);

#ifdef __cplusplus
}
#endif

#endif
