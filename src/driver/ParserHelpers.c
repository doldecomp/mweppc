#include "driver/ParserHelpers.h"
#include "compiler/win32.h"
#include "driver/CWPlugins.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/Generic.h"
#include "driver/IO.h"
#include "driver/MacSpecs.h"
#include "driver/Memory.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"
#include "driver/Projects.h"
#include "driver/Utils.h"
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STSbuf ParserHelpers_STSbuf

SInt16 lastStage;
Boolean dashIMinusMovesPaths;
Boolean usedDashIMinus;
Boolean namingSysPaths;
static char STSbuf[260];

int FindFileInPath(const char* filename, OSSpec* fss) {
    CWFileInfo fi;
    CWResult result;

    fi.fullsearch = TRUE;
    fi.dependencyType = cwNoDependency;
    fi.suppressload = TRUE;
    fi.isdependentoffile = kCurrentCompiledFile;

    result = CWFindAndLoadFile(parseopts.context, filename, &fi);
    if (!result) {
        OS_FSSpec_To_OSSpec(&fi.filespec, fss);
        return TRUE;
    } else {
        return FALSE;
    }
}

char* GetEnvVar(const char* name, Boolean warn, const char** match) {
    const char* nptr;
    const char* last;
    char* ret;

    nptr = name;
    last = name;
    while (*nptr) {
        ret = getenv(nptr);
        if (ret) {
            if (match) {
                *match = nptr;
            }
            return ret;
        }
        last = nptr;
        nptr = &nptr[1 + strlen(nptr)];
    }

    if (warn) {
        CLPReportWarning(CLPStr52, last);
    }
    *match = 0;
    return NULL;
}

static Boolean MatchesExtension(const char* list, const char* filename) {
    char* fn;
    const char* eptr;
    const char* ptr;

    fn = OS_GetFileNamePtr((char*)filename);
    eptr = strrchr(fn, '.');
    if (!eptr) {
        return FALSE;
    }
    if (!list) {
        return TRUE;
    }

    ptr = eptr;
    while (*list) {
        if (*list == '|' && !*ptr) {
            return TRUE;
        }

        if (my_tolower(*list) == my_tolower(*ptr)) {
            list++;
            ptr++;
        } else {
            while (*list && *list != '|') {
                list++;
            }
            if (*list) {
                list++;
            }
            ptr = eptr;
        }
    }

    return *list == 0 && *ptr == 0;
}

int Opt_AddAccessPath(const char* opt, void* var, const char* arg, int flags) {
    OSPathSpec spec;
    int err;
    const char* path;

    path = arg ? arg : opt;
    if (!path) {
        return FALSE;
    }
    if (!path[0]) {
        return TRUE;
    }

    if (strlen(path) >= 260) {
        CLPReportError(CLPStr13, path + strlen(path) - 32, 260);
        return FALSE;
    }

    err = OS_MakePathSpec(0, path, &spec);
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
        CLPReportWarning(CLPStr45, path);
        return TRUE;
    } else if (err != 0) {
        CLPOSAlert(CLPStr45, err, path);
        return TRUE;
    }

    if (!AddAccessPath(&spec, namingSysPaths, 0, var ? TRUE : FALSE)) {
        return FALSE;
    } else {
        return TRUE;
    }
}

void ListParseMessage(
    void (*errprint)(const char*, va_list), const char* envvar, VarArgShort id, ...
) {
    char buf[1024];
    va_list ap;

    if (envvar && envvar[0]) {
        sprintf(buf, "In environment variable '%s':\n", envvar);
    } else {
        buf[0] = 0;
    }

    va_start(ap, id);
    CLPGetErrorString(id, buf + strlen(buf));
    errprint(buf, ap);
    va_end(ap);
}

int AddAccessPathList(
    const char* list,
    char sep1,
    char sep2,
    int source,
    char* text,
    Boolean system,
    SInt32 position,
    Boolean recursive
) {
    char path[260];
    OSPathSpec spec;
    int err;
    int type;
    Boolean recurse;
    char* ptr;

    type = ((system == 1) ? (Boolean)1 : (Boolean)0) | ((source == 0) ? 0 : 2);
    recurse = (list[0] == '+');
    if (recurse) {
        ++list;
    }

    if ((sep1 == ':' || sep2 == ':') &&
        my_isalpha(*list) &&
        list[1] == ':' &&
        !OS_MakePathSpec(0, list, &spec)) {
        return AddAccessPath(&spec, type, position, recurse ^ recursive);
    }

    if (!strchr(list, sep1)) {
        sep1 = sep2;
    }

    while (*list) {
        ptr = path;
        while (*list && *list != sep1 && (ptr + 1) < &path[260]) {
            *(ptr++) = *(list++);
        }
        *ptr = 0;

        if ((ptr + 1) >= &path[260]) {
            ListParseMessage(
                CLPReportError_V,
                (source == 1) ? text : NULL,
                CLPStr9,
                path,
                path + strlen(path) - 16,
                260
            );
            return FALSE;
        }

        err = OS_MakePathSpec(0, path, &spec);
        if (err) {
            ListParseMessage(CLPReportWarning_V, (source == 1) ? text : NULL, CLPStr45, path);
        } else {
            AddAccessPath(&spec, type, position, recurse ^ recursive);
        }

        if (*list) {
            ++list;
        }
        recurse = *list == '+';
        if (recurse) {
            ++list;
        }
    }

    return TRUE;
}

int Opt_FindAndAddFile(const char* opt, void* var, const char* arg, int flags) {
    OSSpec spec;
    int err;
    Boolean isfile;
    OSSpec* match;
    const char* path;

    path = arg ? arg : opt;
    if (!path) {
        return FALSE;
    }
    if (!*path) {
        return TRUE;
    }

    parseopts.userSpecifiedFiles++;
    err = OS_MakeSpec(path, &spec, &isfile);
    if (!err) {
        err = OS_Status(&spec);
    }

    if (!err && !isfile) {
        CLPReportError(CLPStr47, path);
        parseopts.unusedFiles++;
        return FALSE;
    } else if (err && err != ERROR_FILE_NOT_FOUND && !strpbrk(path, "*?")) {
        CLPOSAlert(CLPStr44, err, path);
        parseopts.unusedFiles++;
        return FALSE;
    } else if (err && (match = OS_MatchPath(path))) {
        do {
            if (var &&
                !MatchesExtension(
                    (const char*)var, OS_NameSpecToString(&match->name, STSbuf, sizeof(STSbuf))
                )) {
                CLPReportWarning(
                    CLPStr76, OS_SpecToStringRelative(match, 0, STSbuf, sizeof(STSbuf)), var
                );
            }
            if (!AddFileToProject(match, lastStage, parseopts.lastoutputname, TRUE, -1)) {
                parseopts.unusedFiles++;
                return FALSE;
            }
            parseopts.lastoutputname[0] = 0;
        } while ((match = OS_MatchPath(0)));
        return TRUE;
    } else if (err && parseopts.alwaysUsePaths) {
        err = FindFileInPath(path, &spec) ? 0 : ERROR_FILE_NOT_FOUND;
    }

    if (err) {
        CLPReportError(CLPStr44, path);
        parseopts.unusedFiles++;
        return FALSE;
    }

    if (var && !MatchesExtension((const char*)var, path)) {
        CLPReportWarning(CLPStr76, path, var);
    }

    if (!AddFileToProject(&spec, lastStage, parseopts.lastoutputname, TRUE, -1)) {
        parseopts.unusedFiles++;
        return FALSE;
    } else {
        parseopts.lastoutputname[0] = 0;
        return TRUE;
    }
}

int Opt_AddUnixLibraryFile(const char* opt, void* var, const char* name) {
    OSSpec spec;
    Boolean failed;
    const char* eptr;
    const char* eend;
    char libname[64];

    if (strlen(name) >= 56) {
        CLPReportError(CLPStr13, name + strlen(name) - 32, 64);
        return FALSE;
    }

    failed = TRUE;
    if (!var) {
        var = ".lib|.a";
    }

    eptr = (const char*)var;
    while (eptr && *eptr) {
        eend = eptr;
        while (*eend && *eend != '|') {
            ++eend;
        }

        sprintf(libname, "lib%s%*.*s", name, eend - eptr, eend - eptr, eptr);
        if (FindFileInPath(libname, &spec)) {
            failed = FALSE;
            break;
        }

        if (*eend) {
            eptr = eend + 1;
        } else {
            eptr = eend;
        }
    }

    if (failed) {
        failed = !FindFileInPath(name, &spec);
        if (failed) {
            CLPReportError(CLPStr49, name, var, name);
        }
    }

    if (!failed) {
        if (!AddFileToProject(&spec, 0, 0, TRUE, -1)) {
            parseopts.unusedFiles++;
            failed = TRUE;
        }
    }

    return !failed;
}

void AddStringLenToHandle(Handle* h, const char* str, int len) {
    SInt32 old;

    if (!*h) {
        if ((*h = NewHandle(len + 1))) {
            HLock(*h);
            memcpy(**h, str, len);
            (**h)[len] = 0;
            HUnlock(*h);
        } else {
            exit(-23);
        }
    } else {
        old = GetHandleSize(*h) - 1;
        SetHandleSize(*h, old + len + 1);
        if (MemError() == noErr) {
            HLock(*h);
            memcpy(**h + old, str, len);
            (**h)[old + len] = 0;
            HUnlock(*h);
        } else {
            exit(-23);
        }
    }
}

void AddStringToHandle(Handle* h, const char* str) {
    AddStringLenToHandle(h, str, strlen(str));
}

int Opt_PrintVersion(const char* opt, void* var, const char* arg, int flags) {
    ShowVersion(FALSE);
    return TRUE;
}

void GetFirstSourceFilenameBase(char* buffer, char* defaul) {
    int cnt;
    OSSpec spec;
    char compiler[32];
    int x;
    char* ext;

    cnt = GetFileCount();
    for (x = 0; x < cnt; x++) {
        if (GetFileInfo(x, &spec, compiler)) {
            if (compiler[0]) {
                if (!strstr(compiler, "Lib")) {
                    OS_NameSpecToString(&spec.name, buffer, 256);
                    ext = strrchr(buffer, '.');
                    if (ext) {
                        *ext = 0;
                    }
                    memmove(buffer, buffer, strlen(buffer) + 1);
                    return;
                }
            }
        }
    }

    strcpy(buffer, defaul);
}

int Opt_SavePrefs(const char* opt, void* var, const char* arg, int flags) {
    Parser_StorePanels(parseopts.context);
    return TRUE;
}

int ParseNumber(const char* arg, Boolean emit_error, SInt32* ret, const char** endptr) {
    char* end;

    if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X')) {
        *ret = strtol(arg + 2, &end, 16);
    } else if (arg[0] == '0') {
        *ret = strtol(arg + 1, &end, 8);
    } else {
        *ret = strtol(arg, &end, 10);
    }

    if (endptr) {
        *endptr = end;
    }

    if (*end && emit_error) {
        CLPReportError(CLPStr5, "", arg);
        return FALSE;
    }

    return TRUE;
}

int Opt_MaybeMoveAccessPaths(const char* opt, void* var, const char* arg, int flags) {
    if (dashIMinusMovesPaths && !usedDashIMinus) {
        MoveSystemPathsIntoUserList();
    }
    return TRUE;
}
