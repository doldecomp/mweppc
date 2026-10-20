#include "driver/Generic.h"
#include "compiler/win32.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include "driver/StringExtras.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static OSSpec wildmatch;
static OSOpenedDir wilddirref;
static char wilddir[259];
static char wildname[255];
char STSbuf[260];

static int WildCardMatch(char* wild, char* name) {
    char next;
    char* prev;

    if (*name == 0) {
        return FALSE;
    }

    while (*wild != 0) {
        if (*wild == '*') {
            next = *++wild;
            prev = NULL;
            while (*name != 0) {
                if (tolower(*name) == tolower(next)) {
                    prev = name;
                }
                name++;
            }
            if (prev != NULL) {
                name = prev;
            }
            if (tolower(*name) != tolower(next)) {
                return FALSE;
            }
        } else if (*wild == '?' && *name != 0) {
            wild++;
            name++;
            if (*wild == 0 && *name != 0) {
                return FALSE;
            }
        } else if (tolower(*wild) == tolower(*name)) {
            wild++;
            name++;
        } else {
            return FALSE;
        }
    }

    return *name == 0 || (*name == OS_PATHSEP && name[1] == 0);
}

OSSpec* __stdcall OS_MatchPath(const char* path) {
    char filename[256];
    Boolean isfile;
    OSSpec spec;
    const char* nptr;

    if (path) {
        nptr = strrchr(path, OS_PATHSEP);
        if (!nptr) {
            nptr = path;
            strcpyn(wilddir, ".", -1, 260);
        } else {
            nptr = nptr + 1;
            strcpyn(wilddir, path, nptr - path, 260);
        }

        if (OS_MakePathSpec(0, wilddir, &spec.path)) {
            return 0;
        }

        strcpyn(wildname, nptr, -1, 256);
        if (OS_MakeNameSpec(wildname, &spec.name)) {
            return 0;
        }

        if (OS_OpenDir(&spec.path, &wilddirref)) {
            return 0;
        }
    }

    while (!OS_ReadDir(&wilddirref, &wildmatch, filename, &isfile)) {
        if (isfile && WildCardMatch(wildname, filename)) {
            return &wildmatch;
        }
    }

    OS_CloseDir(&wilddirref);
    return 0;
}

char* __stdcall OS_GetFileNamePtr(char* path) {
    char* ptr;
    ptr = strrchr(path, OS_PATHSEP);
    if (!ptr) {
        ptr = path;
    } else {
        ptr += 1;
    }
    return ptr;
}

char* __stdcall OS_GetDirName(const OSPathSpec* spec, char* buf, int size) {
    char* path;
    char* pptr;

    if (!spec || !buf || size <= 0) {
        return 0;
    }

    path = OS_PathSpecToString(spec, STSbuf, 260);
    pptr = path + strlen(path) - 1;
    if (pptr > path && *pptr == OS_PATHSEP) {
        *pptr = 0;
        --pptr;
    }

    while (pptr >= path && *pptr != OS_PATHSEP) {
        pptr--;
    }

    strncpy(buf, pptr, size - 1);
    buf[size - 1] = 0;
    return buf;
}

int __stdcall OS_MakeSpec2(const char* path, const char* filename, OSSpec* spec) {
    char bpath[260];
    char* eptr;
    int pthlen;
    int fnlen;

    if (!path) {
        path = "";
    }
    if (!filename) {
        filename = "";
    }

    fnlen = strlen(filename);
    pthlen = strlen(path);
    if (fnlen + pthlen + 1 > 259) {
        return ERROR_BUFFER_OVERFLOW;
    }

    strncpy(bpath, path, pthlen);
    eptr = bpath + pthlen;
    if (eptr[-1] != OS_PATHSEP) {
        *(eptr++) = OS_PATHSEP;
    }
    strcpy(eptr, filename);

    return OS_MakeSpec(bpath, spec, 0);
}

int __stdcall OS_MakeSpecWithPath(
    OSPathSpec* path, const char* filename, Boolean noRelative, OSSpec* spec
) {
    Boolean relpath;
    char buf[260];
    char* mptr;
    char* eptr;

    relpath = filename && strpbrk(filename, "/\\:");

    if (!filename) {
        if (path) {
            spec->path = *path;
        } else {
            OS_GetCWD(&spec->path);
        }
        return OS_MakeNameSpec("", &spec->name);
    } else {
        if ((!noRelative || !relpath) && !OS_IsFullPath(filename)) {
            if (path) {
                OS_PathSpecToString(path, buf, 260);
            } else {
                buf[0] = 0;
            }

            mptr = &buf[259] - strlen(filename);
            eptr = &buf[strlen(buf)];
            strcpy((eptr > mptr) ? mptr : eptr, filename);
            return OS_MakeSpec(buf, spec, 0);
        } else {
            return OS_MakeSpec(filename, spec, 0);
        }
    }
}

int __stdcall OS_NameSpecChangeExtension(OSNameSpec* spec, const char* ext, Boolean append) {
    char name[256];
    char* per;

    OS_NameSpecToString(spec, name, 260);
    if (!append) {
        per = strrchr(name, '.');
        if (!per) {
            per = name + strlen(name);
        }
    } else {
        per = name + strlen(name);
        if (ext[0] != '.') {
            *per = '.';
            per++;
            *per = 0;
        }
    }

    if (strlen(name) + strlen(ext) > 256) {
        per = name + 255 - strlen(ext);
    }

    strcpy(per, ext);
    return OS_MakeNameSpec(name, spec);
}

int __stdcall OS_NameSpecSetExtension(OSNameSpec* spec, const char* ext) {
    char name[256];
    char* per;

    OS_NameSpecToString(spec, name, 260);
    if (ext[0] != '.') {
        per = strrchr(name, '.');
        if (!per) {
            per = name + strlen(name);
        }

        if (ext[0]) {
            if (strlen(name) + 1 >= 256) {
                per[-1] = '.';
            } else {
                per[0] = '.';
                per++;
            }
        }
    } else {
        per = name + strlen(name);
    }

    if (strlen(name) + strlen(ext) > 256) {
        per = name + 255 - strlen(ext);
    }

    strcpy(per, ext);
    return OS_MakeNameSpec(name, spec);
}

char* __stdcall OS_CompactPaths(char* buf, const char* path, const char* name, int size) {
    int plen;
    int nlen;
    char* ptr;
    int bidx;
    int excess;

    plen = path ? strlen(path) : 0;
    nlen = name ? strlen(name) : 0;

    if (plen + nlen + 1 <= size) {
        sprintf(buf, "%s%s", path ? path : "", name ? name : "");
        return buf;
    }

    ptr = buf;
    bidx = 0;
    excess = plen + nlen - size;

    while (plen > 0 && nlen > 0) {
        if (plen > 0) {
            *(ptr++) = *(path++);
            plen--;
        } else if (nlen > 0) {
            *(ptr++) = *(name++);
            nlen--;
        }

        if (++bidx == (size / 2) - 2) {
            ptr[0] = '.';
            ptr[1] = '.';
            ptr[2] = '.';
            ptr += 3;
            bidx += 3;
            if (plen > 0) {
                plen -= excess / 2;
                if (plen < 0) {
                    name -= plen;
                    nlen += plen;
                    plen = 0;
                }
            } else {
                name += excess / 2;
                nlen -= excess / 2;
            }
        }
    }

    *ptr = 0;
    return buf;
}

char* __stdcall OS_SpecToStringRelative(
    const OSSpec* spec, const OSPathSpec* cwdspec, char* path, int size
) {
    char fullbuf[260];
    char* full;
    char cwdbuf[260];
    char* cwd;
    OSPathSpec mycwdspec;
    char* pptr;

    full = fullbuf;
    cwd = cwdbuf;
    OS_SpecToString(spec, full, 260);

    if (!size) {
        size = 260;
    }

    if (!path) {
        path = malloc(size);
        if (!path) {
            return 0;
        }
    }

    if (!cwdspec) {
        OS_GetCWD(&mycwdspec);
        cwdspec = &mycwdspec;
    }

    if (!OS_PathSpecToString(cwdspec, cwdbuf, 260)) {
        memcpy(path, fullbuf, size - 1);
        path[size - 1] = 0;
        return path;
    }

    while (*cwd && tolower(*full) == tolower(*cwd)) {
        full++;
        cwd++;
    }

    if ((size_t)(cwd - cwdbuf) < (strlen(fullbuf) / 2) && (strlen(cwd) > (strlen(fullbuf) / 2))) {
        memcpy(path, fullbuf, size - 1);
        path[size - 1] = 0;
        return path;
    }

    while (cwd > cwdbuf) {
        full--;
        cwd--;
        if (*cwd == OS_PATHSEP) {
            break;
        }
    }

    if (cwd == cwdbuf) {
        strncpy(path, full, size - 1);
        path[size - 1] = 0;
    } else {
        if (*(++cwd)) {
            pptr = path;
            while (*cwd) {
                if (*cwd == OS_PATHSEP) {
                    pptr += sprintf(pptr, "..\\");
                }
                ++cwd;
            }
            strcpy(pptr, full + 1);
        } else {
            strncpy(path, full + 1, size - 1);
            path[size - 1] = 0;
        }
    }

    return path;
}

int __stdcall OS_FindFileInPath(const char* filename, const char* plist, OSSpec* spec) {
    const char* next;
    char path[260];
    int err;

    while (plist && *plist) {
        next = strchr(plist, ';');
        if (!next) {
            next = strpbrk(plist, ";,");
        }

        if (!next) {
            next = plist + strlen(plist);
        }

        strcpyn(path, plist, next - plist, 259);
        if (!OS_MakeSpec2(path, filename, spec)) {
            if (!OS_Status(spec)) {
                return 0;
            }
        }

        plist = *next ? next + 1 : NULL;
    }

    err = OS_MakeFileSpec(filename, spec);
    if (!err) {
        err = OS_Status(spec);
        if (!err) {
            return 0;
        }
    }
    return err;
}

int __stdcall OS_FindProgram(const char* filename, OSSpec* spec) {
    char exename[260];
    char dir[260];
    int err;
    char* plist;

    strncpy(exename, filename, 260);
    exename[259] = 0;
    if (strlen(exename) < 4 || ustrcmp(exename + strlen(exename) - 4, ".exe")) {
        strcatn(exename, ".exe", 260);
    }

    if (!strchr(exename, OS_PATHSEP)) {
        if (GetSystemDirectoryA(dir, 260)) {
            if (!OS_MakeSpec2(dir, exename, spec)) {
                if (!(err = OS_Status(spec))) {
                    return err;
                }
            }
        }
        if (GetWindowsDirectoryA(dir, 260)) {
            if (!OS_MakeSpec2(dir, exename, spec)) {
                if (!(err = OS_Status(spec))) {
                    return err;
                }
            }
        }
        plist = getenv("PATH");
        if (!OS_FindFileInPath(exename, plist, spec)) {
            return 0;
        }
    }

    err = OS_MakeFileSpec(exename, spec);
    if (!err) {
        err = OS_Status(spec);
    }
    return err;
}

int __stdcall OS_CopyHandle(OSHandle* hand, OSHandle* copy) {
    int err;
    UInt32 sz;
    void* from;
    void* to;

    err = OS_GetHandleSize(hand, &sz);
    if (!err) {
        err = OS_NewHandle(sz, copy);
        if (!err) {
            from = OS_LockHandle(hand);
            to = OS_LockHandle(copy);
            memcpy(to, from, sz);
            OS_UnlockHandle(hand);
            OS_UnlockHandle(copy);
            return 0;
        }
    }
    OS_FreeHandle(copy);
    return err;
}

int __stdcall OS_AppendHandle(OSHandle* hand, const void* data, UInt32 len) {
    int err;
    UInt32 sz;
    void* buffer;

    err = OS_GetHandleSize(hand, &sz);
    if (!err) {
        err = OS_ResizeHandle(hand, sz + len);
        if (!err) {
            buffer = OS_LockHandle(hand);
            if (buffer) {
                memcpy((unsigned char*)buffer + sz, data, len);
                OS_UnlockHandle(hand);
                return 0;
            }
        }
    }
    return err;
}
