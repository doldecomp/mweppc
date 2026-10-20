// The x86 runtime's setupargs.c: __argc and __argv for main, from the command line. 2.x links a revision later
// than lib/runtime's (September 1999): two quotes inside quotes are one quote, and a run of backslashes before a
// quote keeps half of them (Microsoft's rules); other backslashes are kept.
#include <ansi_parms.h>
#include <windef.h>
#include <winbase.h>
#include <stdlib.h>
#include <string.h>

#define WHITESPACE " \t\r\n\v"

char **__argv;
int __argc;

static char *__command_line = 0;

static void _FreeCommandLine(void)
{
    if (__command_line != NULL) {
        free(__command_line);
    }
    if ((__argv != NULL) && (__argc != 0)) {
        free(__argv);
    }
}

static void _MemFailure(void)
{
    static char *fakeargs[] = {NULL};
    __argv = fakeargs;
    __argc = 0;
}

void _SetupArgs(void)
{
    char *inptr;
    char *outptr;
    int maxargc = 1;
    int quoting;

    __argc = 0;
    __argv = (char **)malloc(sizeof(char *));
    if (__argv == NULL) {
        _MemFailure();
        return;
    }

    inptr = GetCommandLineA();
    outptr = __command_line = (char *)malloc(strlen(inptr) + 1);
    if (outptr == NULL) {
        _MemFailure();
        return;
    }

    while (strchr(WHITESPACE, *inptr) != NULL) {
        inptr++;
    }

    while (*inptr != '\0') {
        if (__argc + 1 >= maxargc) {
            char **newargs;
            maxargc += 16;
            newargs = (char **)realloc(__argv, maxargc * sizeof(char *));
            if (newargs == NULL) {
                break;
            }
            __argv = newargs;
        }

        __argv[__argc++] = outptr;
        quoting = 0;

        while (*inptr != '\0') {
            if (!quoting) {
                if (strchr(WHITESPACE, *inptr)) {
                    do {
                        inptr++;
                    } while (*inptr != '\0' && strchr(WHITESPACE, *inptr));
                    *outptr++ = '\0';
                    break;
                }
                if (*inptr == '"') {
                    inptr++;
                    quoting = 1;
                    continue;
                }
            } else {
                if (*inptr == '"' && inptr[1] == '"') {
                    *outptr++ = '"';
                    inptr += 2;
                    continue;
                }
                if (*inptr == '"') {
                    inptr++;
                    quoting = 0;
                    continue;
                }
            }

            if (*inptr == '\\' && inptr[1] == '\\') {
                int n = 2;
                while (inptr[n] == '\\') {
                    n++;
                }

                if (inptr[n] == '"') {
                    if ((n &= ~1) != 0) {
                        do {
                            *outptr++ = *inptr;
                            inptr += 2;
                            n -= 2;
                        } while (n != 0);
                    }
                } else {
                    while (n--) {
                        *outptr++ = *inptr++;
                    }
                }
                continue;
            }

            if (*inptr == '\\' && inptr[1] == '"') {
                *outptr++ = '"';
                inptr += 2;
                continue;
            }

            *outptr++ = *inptr++;
        }
    }
    *outptr = '\0';
    __argv[__argc] = NULL;
    atexit(_FreeCommandLine);
}
