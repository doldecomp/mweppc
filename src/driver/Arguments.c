#include "driver/Arguments.h"
#include "driver/CWParserPluginsPrivate.h"
#include "driver/FileHandles.h"
#include "driver/Generic.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include "driver/option_system.h"
#include "driver/oslib.h"
#include "driver/ParserErrors.h"
#include "driver/StringExtras.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char compat;
char* MAINOPTCHAR;
char* FIRSTARGCHAR;
char* SEPOPTSTR;
char SEP1;
char SEP2;
char SEP3;
char RESPFILECHAR;
char* RESPFILESTR;
static ArgToken* argtoks;
static int numargtoks;
static int maxargtoks;
unsigned char parserDebug;
static unsigned char in_response_file;
static int margc;
static int margind;
static char** margv;
static OSFileHandle respfilehandle;
static char* respfile;
static char* respfilestart;
static unsigned long respfilesize;
static int scantok;
PtrList linkargs;
PtrList prelinkargs;
PtrList postlinkargs;

static void Arg_AddToken(short val, char* text);
static void Arg_Setup(int argc, char** argv);
static void Arg_SkipRespFileWS(void);
static unsigned char Arg_OpenRespFile(const char* name);
static void Arg_CloseRespFile(void);
static char* Arg_GetRespFileToken(void);
static char* Arg_GetNext(unsigned char allow_resp);
static unsigned char Arg_GotMore(void);
static void Arg_Parse(void);
static void Arg_GrowArgs(PtrList* ta);
static void Arg_GrowArg(PtrList* ta, char* txt);
void Arg_FinishToolArgs(PtrList* ta);

static void Arg_AddToken(short val, char* text) {
    ArgToken* cur;
    ArgToken* prev;
    ArgToken* pprev;
    ArgToken* ppprev;
    ArgToken* pppprev;

    if (numargtoks > 0) {
        prev = &argtoks[numargtoks - 1];
    } else {
        prev = 0;
    }

    if (prev && prev->val == ATK_ARG && prev->text[0] == 0) {
        pprev = 0;
        ppprev = 0;
        pppprev = 0;
        if (numargtoks > 3) {
            pppprev = &argtoks[numargtoks - 4];
        }
        if (numargtoks > 2) {
            ppprev = &argtoks[numargtoks - 3];
        }
        if (numargtoks > 1) {
            pprev = &argtoks[numargtoks - 2];
        }

        if (pprev) {
            if ((int)val == ATK_ARG_END &&
                (pprev->val == ATK_COMMA || pprev->val == ATK_EQUALS) &&
                (ppprev->val != ATK_ARG || pppprev->val != ATK_OPTION)) {
                if (parserDebug) {
                    printf("Coalescing args with '%s'\n", Arg_GetTokenName(pprev));
                }
                val = pprev->val;
                numargtoks -= 2;
            } else if (pprev->val == ATK_ARG_END &&
                       ((int)val == ATK_COMMA || (int)val == ATK_EQUALS)) {
                if (parserDebug) {
                    printf("Coalescing args, removing '%s'\n", Arg_GetTokenName(pprev));
                }
                numargtoks -= 2;
            }
        }
    }

    if (numargtoks >= maxargtoks) {
        argtoks =
            (ArgToken*)xrealloc("argument list", argtoks, sizeof(ArgToken) * (maxargtoks + 16));
        maxargtoks += 16;
    }

    cur = &argtoks[numargtoks];
    cur->val = val;
    if (text) {
        cur->text = xstrdup(text);
    } else {
        cur->text = 0;
    }
    numargtoks++;
}

static void Arg_Setup(int argc, char** argv) {
    in_response_file = FALSE;
    respfile = 0;
    margc = argc;
    margv = argv;
    margind = 1;
}

static void Arg_SkipRespFileWS(void) {
restart:
    while (respfile[0] && isspace(respfile[0])) {
        ++respfile;
    }

    if (respfile[0] == 0x1A) {
        respfile = respfilestart + respfilesize - 1;
    }

    if (respfile[0] == '\\' && respfile[1] == '#') {
        ++respfile;
        return;
    }

    if (respfile[0] == '#') {
        if ((respfile > respfilestart) ? (respfile[-1] != '\\') : 1) {
            while (respfile[0] && respfile[0] != 10 && respfile[0] != 13) {
                ++respfile;
            }

            while (respfile[0] == 13 || respfile[0] == 10) {
                ++respfile;
            }

            goto restart;
        }
    }
}

static unsigned char Arg_OpenRespFile(const char* name) {
    struct OSSpec spec;
    int err;

    if ((err = OS_MakeFileSpec(name, &spec)) ||
        (err = OS_NewFileHandle(&spec, 0, FALSE, &respfilehandle)) ||
        (err = OS_AppendHandle(&respfilehandle.hand, "", 1)) ||
        (err = OS_LockFileHandle(&respfilehandle, &respfile, &respfilesize))) {
        CLPOSAlert(CLPStr74, (short)err, "response ", name);
        return FALSE;
    } else {
        respfilestart = respfile;
        Arg_SkipRespFileWS();
        in_response_file = TRUE;
        return TRUE;
    }
}

static void Arg_CloseRespFile(void) {
    in_response_file = FALSE;
    OS_FreeFileHandle(&respfilehandle);
}

static char* Arg_GetRespFileToken(void) {
    char* start;
    char* ptr;
    int quoting;

    quoting = FALSE;
    if (respfile[0] == 0) {
        return NULL;
    }

    start = ptr = respfile;
    while (respfile[0]) {
        if (!quoting && isspace(respfile[0])) {
            break;
        }

        if (respfile[0] == '"') {
            quoting = !quoting;
            respfile++;
        } else if (respfile[0] == '\\' && respfile[1] == '"') {
            *(ptr++) = '"';
            respfile += 2;
        } else {
            *(ptr++) = *(respfile++);
        }
    }

    if (respfile[0]) {
        Arg_SkipRespFileWS();
    }
    *ptr = 0;
    return start;
}

static char* Arg_GetNext(unsigned char allow_resp) {
    char* ret;
    int rfclen;
    char* rfcequ;

restart:
    if (!in_response_file) {
        rfclen = 1;
        rfcequ = 0;
        ret = margv[margind++];
        if ((rfcequ = strrchr(ret, '\r')) && rfcequ[1] == 0) {
            *rfcequ = 0;
        }
        if (ret[0] == '\\' && ret[1] == RESPFILECHAR) {
            ret++;
        } else if (allow_resp) {
            if (ret[0] == RESPFILECHAR ||
                ((RESPFILESTR[0] && !ustrncmp(ret, RESPFILESTR, rfclen = strlen(RESPFILESTR))) &&
                 ret[rfclen])) {
                rfcequ = strchr(ret + rfclen, '=');
                if (rfcequ) {
                    rfclen = (rfcequ + 1) - ret;
                }
                if (Arg_OpenRespFile(ret + rfclen)) {
                    goto restart;
                }
                ret = 0;
            }
        }
    } else {
        ret = Arg_GetRespFileToken();
        if (!ret) {
            Arg_CloseRespFile();
            goto restart;
        }
    }

    if (parserDebug) {
        fprintf(stderr, "Got arg = '%s'\n", ret ? ret : "<NULL>");
    }

    return ret;
}

static unsigned char Arg_GotMore(void) {
    if (!in_response_file) {
        return margind < margc;
    } else if (respfile[0]) {
        return TRUE;
    } else {
        return margind < margc;
    }
}

static void Arg_Parse(void) {
    unsigned char isOpt;
    unsigned char isList;
    char* arg;
    char* argstart;
    char buffer[4096];
    char* bufptr;
    char ch;

    isOpt = FALSE;
    isList = FALSE;
    while (Arg_GotMore()) {
        argstart = arg = Arg_GetNext(TRUE);
        if (!arg) {
            break;
        }

        bufptr = buffer;
        *bufptr = 0;
        isList = FALSE;

        if (arg[0] && arg[1] && strchr(MAINOPTCHAR, arg[0])) {
            if (isOpt || isList) {
                Arg_AddToken(ATK_ARG_END, 0);
            }
            *bufptr = arg[1];
            bufptr++;
            *bufptr = 0;
            arg += 2;
            isOpt = TRUE;
            isList = FALSE;
        } else {
            isOpt = FALSE;
        }

        while (arg && arg[0]) {
            ch = arg[0];
            if (ch == '\\' && (arg[1] == SEP1 || arg[1] == SEP2 || arg[1] == SEP3)) {
                ch = 0x80 | *(++arg);
            } else if (compat == COMPAT_DOS && ch == ':' && arg[1] == '\\') {
                ch |= 0x80;
            }

            if (ch != SEP1 && ch != SEP2 && ch != SEP3) {
                if ((ch & 0x7F) == SEP1 || (ch & 0x7F) == SEP2 || (ch & 0x7F) == SEP3) {
                    ch &= 0x7F;
                }
                *(bufptr++) = ch;
                if (bufptr >= &buffer[sizeof(buffer)]) {
                    CLPReportError(
                        CLPStr2, argstart, argstart + strlen(argstart) - 15, sizeof(buffer)
                    );
                }
                *bufptr = 0;
            } else {
                if (isOpt) {
                    Arg_AddToken(ATK_OPTION, buffer);
                    Arg_AddToken(ATK_ARG, buffer);
                } else {
                    Arg_AddToken(ATK_ARG, buffer);
                }

                Arg_AddToken(
                    (unsigned char)((ch == ',')
                                        ? ATK_COMMA
                                        : (((ch == '=') || (ch == SEP3)) ? ATK_EQUALS : ATK_END)),
                    0
                );

                bufptr = buffer;
                *bufptr = 0;

                if (ch == SEP1 || ch == SEP2 || ch == SEP3) {
                    isOpt = FALSE;
                }
                isList = TRUE;
            }

            arg++;
        }

        if (isOpt && bufptr > &buffer[0]) {
            Arg_AddToken(ATK_OPTION, buffer + (isList && strchr(MAINOPTCHAR, buffer[0])));
            Arg_AddToken(ATK_ARG, buffer + (isList && strchr(MAINOPTCHAR, buffer[0])) + 1);
        } else {
            Arg_AddToken(ATK_ARG, buffer);
            Arg_AddToken(ATK_ARG_END, 0);
        }
    }

    if (isOpt || isList) {
        Arg_AddToken(ATK_ARG_END, 0);
    }
    Arg_AddToken(ATK_END, 0);
}

void Arg_Init(int theargc, char** theargv) {
    int p;
    int x;

    maxargtoks = 0;
    numargtoks = 0;
    parserDebug = FALSE;
    if (theargc > 1 && !strcmp(theargv[1], "--parser-debug")) {
        parserDebug = TRUE;
        memmove(&theargv[1], &theargv[2], sizeof(char*) * (theargc - 1));
        theargc--;
    }

    if ((int)compat == COMPAT_DEFAULT) {
        MAINOPTCHAR = "-";
        FIRSTARGCHAR = "=";
        SEPOPTSTR = " ";
        SEP1 = ',';
        SEP2 = '=';
        SEP3 = '=';
        RESPFILECHAR = '@';
        RESPFILESTR = "";
    } else if ((int)compat == COMPAT_DOS) {
        MAINOPTCHAR = "/-";
        FIRSTARGCHAR = ":";
        SEPOPTSTR = ":";
        SEP1 = ',';
        SEP2 = '=';
        SEP3 = ':';
        RESPFILECHAR = '@';
        RESPFILESTR = "";
    } else if ((int)compat == COMPAT_CUSTOM) {
        if (!MAINOPTCHAR) {
            MAINOPTCHAR = "-";
        }
        if (!FIRSTARGCHAR) {
            FIRSTARGCHAR = "=";
        }
        if (!SEPOPTSTR) {
            SEPOPTSTR = " ";
        }
        if (!SEP1) {
            SEP1 = ',';
        }
        if (!SEP2) {
            SEP2 = '=';
        }
        if (!SEP3) {
            SEP3 = '=';
        }
        if (!RESPFILECHAR) {
            RESPFILECHAR = '@';
        }
        if (!RESPFILESTR) {
            RESPFILESTR = "";
        }
    } else {
        CLPFatalError("Unknown parser compatibility type (%d)\n", (int)compat);
    }

    if (parserDebug) {
        printf("Incoming arguments: \n");
        for (p = 0; p < theargc; p++) {
            printf("[%s] ", theargv[p]);
        }
        printf("\n");
    }

    Arg_Setup(theargc, theargv);
    Arg_Parse();
    Arg_Reset();

    if (parserDebug) {
        for (x = 0; x < numargtoks; x++) {
            printf("TOKEN:  '%s'\n", Arg_GetTokenName(&argtoks[x]));
        }
    }
}

void Arg_Terminate(void) {
    ArgToken* cur;

    while (numargtoks > 0) {
        cur = &argtoks[--numargtoks];
        if (cur->text) {
            free(cur->text);
        }
    }

    if (maxargtoks) {
        free(argtoks);
    }
    maxargtoks = 0;
}

void Arg_Reset(void) {
    scantok = 0;
}

void Arg_Stop(ArgToken* where) {
    ArgToken* cur;

    while (&argtoks[numargtoks] > where) {
        cur = &argtoks[--numargtoks];
        if (cur->text) {
            free(cur->text);
        }
        cur->val = ATK_END;
        cur->text = 0;
    }

    argtoks[numargtoks++].val = ATK_ARG_END;
    argtoks[numargtoks++].val = ATK_END;
    scantok = numargtoks - 2;
}

ArgToken* Arg_PeekToken(void) {
    if (scantok >= numargtoks) {
        return NULL;
    } else {
        return &argtoks[scantok];
    }
}

ArgToken* Arg_UsedToken(void) {
    if (scantok < numargtoks) {
        scantok++;
    }
    return Arg_PeekToken();
}

int Arg_IsEmpty(void) {
    ArgToken* tok;

    tok = Arg_PeekToken();
    return (tok == 0 || tok->val == ATK_END);
}

ArgToken* Arg_GetToken(void) {
    ArgToken* ret;

    ret = Arg_PeekToken();
    if (ret) {
        scantok++;
    }
    return ret;
}

ArgToken* Arg_UndoToken(void) {
    if (scantok > 0) {
        scantok--;
        return Arg_PeekToken();
    } else {
        return NULL;
    }
}

const char* Arg_GetTokenName(ArgToken* tok) {
    if ((int)tok->val == ATK_ARG) {
        return tok->text;
    }

    return ((int)tok->val == ATK_OPTION)                               ? "option"
        : ((int)tok->val == ATK_COMMA)                                 ? "comma"
        : (((int)compat == COMPAT_DOS && (int)tok->val == ATK_EQUALS)) ? "colon or equals"
        : (((int)compat != COMPAT_DOS && (int)tok->val == ATK_EQUALS)) ? "equals"
        : ((int)tok->val == ATK_ARG_END)                               ? "end of argument"
        : ((int)tok->val == ATK_END)                                   ? "end of command line"
                                                                       : "<error>";
}

const char* Arg_GetTokenText(ArgToken* tok, char* buffer, int maxlen, unsigned char warn) {
    const char* ptr;
    char* bptr;
    int curlen;

    bptr = buffer;
    curlen = 0;
    if (tok->val == ATK_ARG || tok->val == ATK_OPTION) {
        ptr = tok->text;
    } else {
        ptr = Arg_GetTokenName(tok);
    }

    while (*ptr && curlen++ < maxlen) {
        *(bptr++) = *(ptr++);
    }

    if (curlen < maxlen) {
        bptr[0] = 0;
    } else {
        bptr[-1] = 0;
        if (warn) {
            CLPReportWarning(
                CLPStr56, buffer, ptr + strlen(ptr) - ((maxlen > 32) ? 32 : maxlen), maxlen
            );
        }
    }

    return buffer;
}

static void Arg_GrowArgs(PtrList* ta) {
    int len;

    if (!ta->argv || (ta->argc + 1) >= ta->nargv) {
        len = ta->nargv;
        ta->nargv += 16;
        ta->argv = xrealloc("argument list", ta->argv, sizeof(char*) * (ta->nargv + 1));
        while (len <= ta->nargv) {
            ta->argv[len++] = 0;
        }
    }

    ta->argc++;
}

static void Arg_GrowArg(PtrList* ta, char* txt) {
    char** ptr;
    int ptrlen;

    ptr = &ta->argv[ta->argc];

    if (*ptr == 0) {
        *ptr = xstrdup(txt);
    } else {
        ptrlen = strlen(*ptr);
        *ptr = xrealloc("command line", *ptr, ptrlen + strlen(txt) + 1);
        strcpy(*ptr + ptrlen, txt);
    }
}

void Arg_InitToolArgs(PtrList* ta) {
    ta->argc = 0;
    ta->nargv = 0;
    ta->argv = 0;
    Arg_GrowArgs(ta);
}

void Arg_AddToToolArgs(PtrList* ta, short tokval, char* toktxt) {
    switch (tokval) {
        case ATK_END:
            Arg_FinishToolArgs(ta);
            break;
        case ATK_ARG_END:
            if (ta->argv && ta->argv[ta->argc]) {
                Arg_GrowArgs(ta);
            }
            break;
        case ATK_ARG:
            Arg_GrowArg(ta, toktxt);
            break;
        case ATK_OPTION:
            Arg_GrowArg(ta, "-");
            break;
        case ATK_EQUALS:
            Arg_GrowArg(ta, "=");
            break;
        case ATK_COMMA:
            Arg_GrowArg(ta, ",");
            break;
        default:
            CLPFatalError(__FILE__, 787, "Unknown token (%d)", tokval);
            break;
    }
}

void Arg_FinishToolArgs(PtrList* ta) {
    Arg_GrowArgs(ta);
    ta->argv[ta->argc] = 0;
}

void Arg_ToolArgsForPlugin(PtrList* ta, struct CWCommandLineArgs* args) {
    args->argc = 1;
    args->argv = ta->argv;

    while (args->argv[args->argc]) {
        args->argc++;
    }

    args->envp = 0;
}

void Arg_FreeToolArgs(PtrList* ta) {
    int x;

    if (ta->argv) {
        for (x = 1; x < ta->argc; x++) {
            if (ta->argv[x]) {
                free(ta->argv[x]);
            }
        }
        free(ta->argv);
    }
}
