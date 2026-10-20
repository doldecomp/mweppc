#include "driver/MsDos.h"
#include "compiler/win32.h"
#include "driver/oslib.h"
#include "driver/StringExtras.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char unused_zeroes[0x8000] = {0};

int __stdcall OS_EqualNameSpec(const OSNameSpec* a, const OSNameSpec* b);

static char errtext[256];
static char* cygdrive_prefix;
static char intbuf[260];

const char* __stdcall OS_GetErrText(int err) {
    char* eol;

    if (err != (int)0xfafafafa) {
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0, errtext, sizeof(errtext), NULL);
    } else {
        strcpy(errtext, "Unknown process spawning error");
    }
    eol = errtext + strlen(errtext) - 2;
    if ((errtext < eol) && (*eol == '\r') && (eol[1] == '\n')) {
        *eol = '\0';
    }
    return errtext;
}

int __stdcall OS_InitProgram(int* pArgc, char*** pArgv) {
    cygdrive_prefix = getenv("MW_CYGDRIVE_PREFIX");
    return 0;
}

uOSTypePair OS_TEXTTYPE;

int __stdcall OS_Create(const OSSpec* spec, const uOSTypePair* type) {
    HANDLE h;

    if (OS_SpecToString(spec, intbuf, sizeof(intbuf)) == NULL) {
        return ERROR_BUFFER_OVERFLOW;
    }
    h = CreateFileA(
        intbuf,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (h == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    CloseHandle(h);
    return OS_SetFileType(spec, type);
}

int __stdcall OS_Status(const OSSpec* spec) {
    DWORD attr;

    if (OS_SpecToString(spec, intbuf, sizeof(intbuf)) == NULL) {
        return ERROR_BUFFER_OVERFLOW;
    }
    attr = GetFileAttributesA(intbuf);
    if (attr == 0xffffffff) {
        return GetLastError();
    }
    return 0;
}

int __stdcall OS_SetFileType(const OSSpec* spec, const uOSTypePair* type) {
    return 0;
}

int __stdcall OS_GetFileTime(const OSSpec* spec, OSTime* crtm, OSTime* chtm) {
    DWORD err;
    FILETIME mtime;
    FILETIME ctime;
    int ref;

    err = OS_Open(spec, OSReadOnly, &ref);
    if (err != 0) {
        return err;
    }
    if (!GetFileTime((HANDLE)ref, &ctime, NULL, &mtime)) {
        err = GetLastError();
    } else {
        err = 0;
    }
    OS_Close(ref);
    if (chtm != NULL) {
        chtm->dwLowDateTime = mtime.dwLowDateTime;
        chtm->dwHighDateTime = mtime.dwHighDateTime;
    }
    if (crtm != NULL) {
        crtm->dwLowDateTime = ctime.dwLowDateTime;
        crtm->dwHighDateTime = ctime.dwHighDateTime;
    }
    return err;
}

int __stdcall OS_SetFileTime(const OSSpec* spec, const OSTime* crtm, const OSTime* chtm) {
    DWORD err;
    FILETIME* pmtime;
    FILETIME* pctime;
    int ref;
    FILETIME mtime;
    FILETIME ctime;

    if (crtm != NULL) {
        ctime.dwLowDateTime = crtm->dwLowDateTime;
        ctime.dwHighDateTime = crtm->dwHighDateTime;
    }
    if (chtm != NULL) {
        mtime.dwLowDateTime = chtm->dwLowDateTime;
        mtime.dwHighDateTime = chtm->dwHighDateTime;
    }
    err = OS_Open(spec, OSWrite, &ref);
    if (err != 0) {
        return err;
    }
    pmtime = chtm ? &mtime : NULL;
    pctime = crtm ? &ctime : NULL;
    if (!SetFileTime((HANDLE)ref, pctime, NULL, pmtime)) {
        err = GetLastError();
    } else {
        err = 0;
    }
    OS_Close(ref);
    return err;
}

int __stdcall OS_Open(const OSSpec* spec, OSOpenMode mode, int* ref) {
    // clang-format off
    static DWORD modetrans[4] = {
        GENERIC_READ,
        GENERIC_WRITE,
        GENERIC_READ | GENERIC_WRITE,
        GENERIC_WRITE
    };
    // clang-format on
    if (OS_SpecToString(spec, intbuf, sizeof(intbuf)) == NULL) {
        return ERROR_BUFFER_OVERFLOW;
    }
    *ref = (int)CreateFileA(
        intbuf, modetrans[mode], FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if ((HANDLE)*ref == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    if (mode == OSAppend) {
        if (SetFilePointer((HANDLE)*ref, 0, NULL, FILE_END) == 0xffffffff) {
            return GetLastError();
        }
    }
    return 0;
}

int __stdcall OS_Write(int ref, const void* buffer, UInt32* length) {
    static char zeroes[32] = {0};
    HANDLE h;
    DWORD end;
    SInt32 pos;
    SInt32 chunk;
    DWORD written;

    h = (HANDLE)ref;
    pos = SetFilePointer(h, 0, NULL, FILE_CURRENT);
    if (pos == -1 || (end = GetFileSize(h, NULL)) == 0xffffffff) {
        return GetLastError();
    }
    if ((DWORD)pos > end) {
        if (SetFilePointer(h, end, NULL, FILE_BEGIN) == 0xffffffff) {
            return GetLastError();
        }
        while ((DWORD)pos > end) {
            chunk = pos - end > sizeof(zeroes) ? sizeof(zeroes) : pos - end;
            if (!WriteFile(h, zeroes, chunk, &written, NULL)) {
                return GetLastError();
            }
            if (written < (DWORD)chunk) {
                *length = 0;
                return 0;
            }
            end = end + chunk;
        }
    }
    if (!WriteFile(h, buffer, *length, length, NULL)) {
        return GetLastError();
    }
    return 0;
}

int __stdcall OS_Read(int ref, void* buffer, UInt32* length) {
    if (!ReadFile((HANDLE)ref, buffer, *length, length, NULL)) {
        return GetLastError();
    }
    return 0;
}

int __stdcall OS_Seek(int ref, OSSeekMode how, SInt32 offset) {
    // clang-format off
    static DWORD howtrans[3] = {
        FILE_CURRENT,
        FILE_BEGIN,
        FILE_END
    };
    // clang-format on
    if (SetFilePointer((HANDLE)ref, offset, NULL, howtrans[how]) == 0xffffffff) {
        return GetLastError();
    }
    return 0;
}

int __stdcall OS_Tell(int ref, SInt32* offset) {
    *offset = SetFilePointer((HANDLE)ref, 0, NULL, FILE_CURRENT);
    if (*offset == -1) {
        return GetLastError();
    }
    return 0;
}

int __stdcall OS_Close(int ref) {
    if (!CloseHandle((HANDLE)ref)) {
        return GetLastError();
    }
    return 0;
}

int __stdcall OS_GetSize(int ref, UInt32* length) {
    *length = GetFileSize((HANDLE)ref, NULL);
    if (*length == 0xffffffff) {
        return GetLastError();
    }
    return 0;
}

int __stdcall OS_SetSize(int ref, UInt32 size) {
    DWORD pos;

    pos = SetFilePointer((HANDLE)ref, 0, NULL, FILE_CURRENT);
    if (pos == 0xffffffff ||
        SetFilePointer((HANDLE)ref, size, NULL, FILE_BEGIN) == 0xffffffff ||
        !SetEndOfFile((HANDLE)ref) ||
        SetFilePointer((HANDLE)ref, pos, NULL, FILE_BEGIN) == 0xffffffff) {
        return GetLastError();
    }
    return 0;
}

int __stdcall OS_Delete(const OSSpec* spec) {
    if (OS_SpecToString(spec, intbuf, sizeof(intbuf)) == NULL) {
        return ERROR_BUFFER_OVERFLOW;
    }
    if (!DeleteFileA(intbuf)) {
        return GetLastError();
    }
    return 0;
}

int __stdcall OS_Mkdir(const OSSpec* spec) {
    if (OS_SpecToString(spec, intbuf, sizeof(intbuf)) == NULL) {
        return ERROR_BUFFER_OVERFLOW;
    }
    if (!CreateDirectoryA(intbuf, NULL)) {
        return GetLastError();
    }
    return 0;
}

int __stdcall OS_Rmdir(const OSPathSpec* spec) {
    if (OS_PathSpecToString(spec, intbuf, sizeof(intbuf)) == NULL) {
        return ERROR_BUFFER_OVERFLOW;
    }
    if (!RemoveDirectoryA(intbuf)) {
        return GetLastError();
    }
    return 0;
}

static void ensure_trailing_backslash(char* path) {
    path += strlen(path);
    if (path[-1] != OS_PATHSEP) {
        *path = OS_PATHSEP;
        path[1] = '\0';
    }
}

int __stdcall OS_GetCWD(OSPathSpec* spec) {
    if (!GetCurrentDirectoryA(sizeof(spec->s), spec->s)) {
        return GetLastError();
    }
    ensure_trailing_backslash(spec->s);
    return 0;
}

static DWORD redirect_standard_handle_to_file(HANDLE* saved, int which, const char* filename) {
    HANDLE old;
    HANDLE h;
    SECURITY_ATTRIBUTES sa;

    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    old = GetStdHandle(which);
    h = CreateFileA(
        filename, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if (h == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    if (!SetStdHandle(which, h)) {
        return GetLastError();
    }
    *saved = old;
    return 0;
}

int __stdcall OS_Execute(
    OSSpec* spec,
    char** argv,
    char** envp,
    const char* stdoutfile,
    const char* stderrfile,
    int* exitcode
) {
    UInt32 size;
    HANDLE savedout;
    HANDLE savederr;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char* cmdline;
    char** arg;
    UInt32 pos;
    BOOL ok;

    size = 0;
    for (arg = argv; *arg != NULL; arg++) {
        size += strlen(*arg) + 3;
    }
    cmdline = malloc(size);
    if (cmdline == NULL) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    for (arg = argv, pos = 0; *arg != NULL; arg++) {
        if (strchr(*arg, ' ') != NULL) {
            cmdline[pos] = '"';
            pos++;
            strcpy(cmdline + pos, *arg);
            pos += strlen(*arg);
            cmdline[pos] = '"';
            pos++;
        } else if (strchr(*arg, '"') != NULL) {
            char* ch;
            for (ch = *arg; *ch != '\0'; ch++, pos++) {
                if (*ch == '"') {
                    cmdline[pos] = '\\';
                    cmdline[pos + 1] = '"';
                    pos++;
                } else {
                    cmdline[pos] = *ch;
                }
            }
        } else {
            strcpy(cmdline + pos, *arg);
            pos += strlen(*arg);
        }
        cmdline[pos] = ' ';
        pos++;
    }
    cmdline[pos] = '\0';
    if (stdoutfile != NULL) {
        DWORD err = redirect_standard_handle_to_file(&savedout, STD_OUTPUT_HANDLE, stdoutfile);
        if (err != 0) {
            return err;
        }
    }
    if (stderrfile != NULL) {
        DWORD err = redirect_standard_handle_to_file(&savederr, STD_ERROR_HANDLE, stderrfile);
        if (err != 0) {
            if (stdoutfile != NULL) {
                SetStdHandle(STD_OUTPUT_HANDLE, savedout);
            }
            return err;
        }
    }
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.lpTitle = "Linking";
    if (OS_SpecToString(spec, intbuf, sizeof(intbuf)) == NULL) {
        return ERROR_BUFFER_OVERFLOW;
    }
    ok = CreateProcessA(intbuf, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    if (stdoutfile != NULL) {
        SetStdHandle(STD_OUTPUT_HANDLE, savedout);
    }
    if (stderrfile != NULL) {
        SetStdHandle(STD_ERROR_HANDLE, savederr);
    }
    if (!ok) {
        return GetLastError();
    }
    free(cmdline);
    WaitForSingleObject(pi.hProcess, INFINITE);
    if (GetExitCodeProcess(pi.hProcess, (LPDWORD)exitcode) != 0) {
        if (*exitcode == STILL_ACTIVE) {
            fprintf(stderr, "?\?? OS_Exec: process still active ?\??\n");
            return 0xfafafafa;
        }
        return 0;
    }
    return GetLastError();
}

char* __stdcall OS_GetDirPtr(char* path) {
    if (*path == OS_PATHSEP && path[1] == OS_PATHSEP) {
        char* sep = strchr(path + 2, OS_PATHSEP);
        if (sep == NULL) {
            sep = path + strlen(path);
        }
        return sep;
    }
    if (isalpha(*path) && path[1] == ':') {
        return path + 2;
    }
    if (*path == OS_PATHSEP) {
        return path;
    }
    return path;
}

int OS_CanonPath(const char* src, char* dst) {
    const char* read;
    char* write;
    const char* rest;
    char ch;

    if (strlen(src) > 259) {
        return ERROR_BUFFER_OVERFLOW;
    }
    if (dst == NULL) {
        dst = (char*)src;
    }

    read = src;
    write = dst;

    if (cygdrive_prefix != NULL && ustrncmp(cygdrive_prefix, src, strlen(cygdrive_prefix)) == 0) {
        rest = src + strlen(cygdrive_prefix);
        if (*rest == '/' || *rest == '\\') {
            rest++;
        }
        if (isalpha(*rest) && (rest[1] == '/' || rest[1] == '\\')) {
            dst[0] = toupper(*rest);
            dst[1] = ':';
            dst[2] = '\\';
            write += 3;
            rest += 2;
            read = rest;
        }
    }

    if (read[0] == '/' && read[1] == '/' && isalpha(read[2]) && (read[3] == '/' || read[3] == 0)) {
        write[0] = read[2];
        write[1] = ':';
        write += 2;
        read += 3;
    } else if (ustrncmp(read, "/cygdrive/", 10) == 0) {
        if (isalpha(read[10]) && (read[11] == '/' || read[11] == 0)) {
            write[0] = read[10];
            write[1] = ':';
            write += 2;
            read += 11;
        }
    }
    if (isalpha(*read) && read[1] == ':') {
        write[0] = toupper(*read);
        write[1] = ':';
        write += 2;
        read += 2;
    }

    while ((ch = *read) != 0) {
        if (ch == '/') {
            *write = '\\';
        } else {
            *write = ch;
        }
        write++;
        read++;
    }
    *write = 0;
    return 0;
}

int __stdcall OS_IsFullPath(const char* path) {
    int cyg;
    int prefixed;
    int dos;
    int unc;
    int rooted;
    int drive;
    int matched;
    int slashes;
    int letter;

    cyg = TRUE;
    prefixed = TRUE;
    dos = TRUE;
    unc = path[0] == '\\' && path[1] == '\\';
    if (!unc) {
        rooted = FALSE;
        drive = FALSE;
        if (isalpha(path[0]) && path[1] == ':') {
            drive = TRUE;
        }
        if (drive && path[2] == '\\') {
            rooted = TRUE;
        }
        if (!rooted) {
            dos = FALSE;
        }
    }
    if (!dos) {
        matched = FALSE;
        if (cygdrive_prefix && ustrncmp(path, cygdrive_prefix, strlen(cygdrive_prefix) == 0)) {
            matched = TRUE;
        }
        if (!matched) {
            prefixed = FALSE;
        }
    }
    if (!prefixed) {
        rooted = FALSE;
        letter = FALSE;
        slashes = path[0] == '/' && path[1] == '/';
        if (slashes && isalpha(path[2])) {
            letter = TRUE;
        }
        if (letter && path[3] == '/') {
            rooted = TRUE;
        }
        if (!rooted) {
            cyg = FALSE;
        }
    }
    return !!cyg;
}

int __stdcall OS_EqualPath(const char* a, const char* b) {
    return ustrcmp(a, b) == 0;
}

int __stdcall OS_MakeSpec(const char* path, OSSpec* spec, Boolean* isfile) {
    const char* ptr;
    char canon[260];
    char full[260];
    char* name;
    char* end;
    int attr;
    int err;
    int len;

    spec->name.s[0] = spec->path.s[0] = 0;
    if ((err = OS_CanonPath(path, canon)) != 0) {
        return err;
    }
    if (GetFullPathNameA(canon, sizeof(full), full, &name) == 0) {
        return GetLastError();
    }

    end = full + strlen(full) - 1;
    ptr = strrchr(path, '\\');
    if (ptr) {
        ptr++;
    } else {
        ptr = path;
    }
    if (memcmp(ptr, "..", 3) && memcmp(ptr, ".", 2) && ptr[strlen(ptr) - 1] == '.' && *end != '.') {
        *++end = '.';
        end[1] = 0;
    }

    attr = GetFileAttributesA(full);
    if (attr != -1) {
        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            if (isfile) {
                *isfile = FALSE;
            }
            name = NULL;
        } else {
            if (isfile) {
                *isfile = TRUE;
            }
        }
        err = 0;
    } else {
        err = GetLastError();
        if (err && err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) {
            return err;
        }
        if (err != ERROR_PATH_NOT_FOUND) {
            err = 0;
        }
        if (isfile) {
            *isfile = TRUE;
        }
    }

    if (!name) {
        name = end + 1;
        if (name[-1] == '.') {
            name--;
        }
        if (name[-1] != '\\') {
            *name = '\\';
            name++;
            *name = 0;
        }
    }

    len = name - full;
    if (len >= (int)sizeof(spec->path.s)) {
        spec->path.s[0] = 0;
        return ERROR_BUFFER_OVERFLOW;
    }
    memcpy(spec->path.s, full, len);
    spec->path.s[len] = 0;

    len = strlen(name);
    if (len >= (int)sizeof(spec->name.s)) {
        spec->name.s[0] = 0;
        return ERROR_BUFFER_OVERFLOW;
    }
    memcpy(spec->name.s, name, len + 1);
    return err;
}

int __stdcall OS_MakeFileSpec(const char* path, OSSpec* spec) {
    Boolean isfile;
    int err;

    err = OS_MakeSpec(path, spec, &isfile);
    if (err) {
        return err;
    }
    if (!isfile) {
        return ERROR_ACCESS_DENIED;
    }
    return 0;
}

int __stdcall OS_MakePathSpec(const char* vol, const char* dir, OSPathSpec* spec) {
    OSSpec fullspec;
    Boolean isfile;
    char path[324];
    UInt32 vlen, dlen;
    int err;
    dlen = (dir != NULL) ? strlen(dir) : 0;
    vlen = (vol != NULL) ? strlen(vol) : 0;
    if (vlen + dlen + 2 > sizeof(path)) {
        return ERROR_BUFFER_OVERFLOW;
    }
    if (vol != NULL) {
        if (vol[0] == '\0') {
            if (dir == NULL) {
                dir = "\\";
            }
            strcpy(path, dir);
        } else if (vol[1] == '\0') {
            sprintf(path, "%s:%s", vol, (dir != NULL) ? dir : "\\");
        } else if (dir != NULL) {
            sprintf(path, "%s%s%s", vol, (dir[0] == '\\') ? "" : "\\", dir);
        } else {
            sprintf(path, "%s", vol);
        }
    } else {
        if (dir != NULL) {
            strcpy(path, dir);
        } else {
            strcpy(path, ".");
        }
    }
    err = OS_MakeSpec(path, &fullspec, &isfile);
    strcpy(spec->s, fullspec.path.s);
    if (err == 0 && OS_Status(&fullspec) != 0) {
        err = ERROR_PATH_NOT_FOUND;
        isfile = FALSE;
    }
    if (err != 0) {
        return err;
    }
    if (isfile) {
        return ERROR_DIRECTORY;
    }
    return 0;
}

int __stdcall OS_MakeNameSpec(const char* name, OSNameSpec* spec) {
    int len;

    len = strlen(name);
    spec->s[0] = 0;
    if (len > 255) {
        return ERROR_BUFFER_OVERFLOW;
    }
    if (strchr(name, '\\') != NULL) {
        return ERROR_ACCESS_DENIED;
    }
    if (strpbrk(name, "<>:\"/\\|") != NULL) {
        return ERROR_INVALID_NAME;
    }
    memcpy(spec->s, name, len + 1);
    return 0;
}

char* __stdcall OS_SpecToString(const OSSpec* spec, char* path, int size) {
    int nlen;
    int plen;

    if (size == 0) {
        size = sizeof(spec->path.s);
    }
    if (path == NULL) {
        path = (char*)malloc(size);
        if (path == NULL) {
            return NULL;
        }
    }
    plen = strlen(spec->path.s);
    nlen = strlen(spec->name.s);
    if (plen + nlen >= size) {
        if (plen >= size) {
            nlen = 0;
            plen = size - 1;
        } else {
            nlen = (size - plen) - 1;
        }
    }
    memcpy(path, spec->path.s, plen);
    memcpy(path + plen, spec->name.s, nlen);
    path[plen + nlen] = 0;
    return path;
}

char* __stdcall OS_PathSpecToString(const OSPathSpec* pspec, char* path, int size) {
    int len;

    if (size == 0) {
        size = sizeof(pspec->s);
    }
    if (path == NULL) {
        path = (char*)malloc(size);
        if (path == NULL) {
            return NULL;
        }
    }
    len = strlen(pspec->s);
    if (len >= size) {
        len = size - 1;
    }
    memcpy(path, pspec->s, len);
    path[len] = 0;
    return path;
}

char* __stdcall OS_NameSpecToString(const OSNameSpec* nspec, char* name, int size) {
    int len;

    if (size == 0) {
        size = sizeof(nspec->s);
    }
    if (name == NULL) {
        name = malloc(size);
        if (name == NULL) {
            return NULL;
        }
    }
    len = strlen(nspec->s);
    if (len >= size) {
        len = size - 1;
    }
    memcpy(name, nspec->s, len);
    name[len] = 0;
    return name;
}

int __stdcall OS_EqualSpec(const OSSpec* a, const OSSpec* b) {
    return OS_EqualPathSpec(&a->path, &b->path) && OS_EqualNameSpec(&a->name, &b->name);
}

int __stdcall OS_EqualPathSpec(const OSPathSpec* a, const OSPathSpec* b) {
    return OS_EqualPath(a->s, b->s);
}

int __stdcall OS_EqualNameSpec(const OSNameSpec* a, const OSNameSpec* b) {
    return OS_EqualPath(a->s, b->s);
}

int __stdcall OS_IsDir(const OSSpec* spec) {
    DWORD attr;
    int len;

    if (OS_SpecToString(spec, intbuf, sizeof(intbuf)) == NULL) {
        return ERROR_BUFFER_OVERFLOW;
    }
    len = strlen(intbuf);
    if (intbuf[len - 1] == '\\') {
        intbuf[len - 1] = 0;
    }
    attr = GetFileAttributesA(intbuf);
    if (attr == 0xffffffff) {
        return FALSE;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

int __stdcall OS_IsFile(const OSSpec* spec) {
    int len;
    DWORD attr;
    if (OS_SpecToString(spec, intbuf, sizeof(intbuf)) == NULL) {
        return ERROR_BUFFER_OVERFLOW;
    }
    len = strlen(intbuf);
    if (intbuf[len - 1] == '\\') {
        intbuf[len - 1] = 0;
    }
    attr = GetFileAttributesA(intbuf);
    if (attr == 0xffffffff) {
        return FALSE;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

int __stdcall OS_IsLink(const OSSpec* spec) {
    return FALSE;
}

int __stdcall OS_ResolveLink(const OSSpec* link, OSSpec* target) {
    *target = *link;
    return 0;
}

int __stdcall OS_OpenDir(const OSPathSpec* spec, OSOpenedDir* ref) {
    char pattern[260];

    ref->data = malloc(sizeof(WIN32_FIND_DATAA));
    if (ref->data == NULL) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    ref->spec = *spec;
    strcpy(pattern, spec->s);
    strcat(pattern, "*");
    ref->dir = FindFirstFileA(pattern, ref->data);
    if (ref->dir == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    return 0;
}

int __stdcall OS_ReadDir(OSOpenedDir* ref, OSSpec* spec, char* filename, Boolean* isfile) {
    DWORD attr;
    LPWIN32_FIND_DATAA fd;
    char name[260];
    int len;
    int plen;

    fd = ref->data;
    for (;;) {
        if (ref->dir == INVALID_HANDLE_VALUE) {
            return ERROR_FILE_NOT_FOUND;
        }
        len = strlen(fd->cFileName);
        if (len + 1 < 256) {
            memcpy(name, fd->cFileName, len);
        } else {
            len = strlen(fd->cAlternateFileName);
            if (len > 256) {
                len = 255;
            }
            memcpy(name, fd->cAlternateFileName, len);
        }
        name[len] = 0;
        attr = fd->dwFileAttributes;
        if (!FindNextFileA(ref->dir, fd)) {
            OS_CloseDir(ref);
        }
        if (!memcmp(name, ".", 2)) {
            continue;
        }
        if (!memcmp(name, "..", 3)) {
            continue;
        }
        attr &= FILE_ATTRIBUTE_DIRECTORY;
        if (strlen(ref->spec.s) + len + !!attr >= 260) {
            continue;
        }
        if (attr) {
            plen = strlen(ref->spec.s);
            memcpy(spec->path.s, ref->spec.s, plen);
            memcpy(spec->path.s + plen, name, len);
            spec->path.s[plen + len] = '\\';
            spec->path.s[plen + len + 1] = 0;
            spec->name.s[0] = 0;
            *isfile = FALSE;
        } else {
            strcpy(spec->path.s, ref->spec.s);
            memcpy(spec->name.s, name, len + 1);
            *isfile = TRUE;
        }
        memcpy(filename, name, len + 1);
        return 0;
    }
}

int __stdcall OS_CloseDir(OSOpenedDir* ref) {
    if (ref->dir != INVALID_HANDLE_VALUE) {
        if (!FindClose(ref->dir)) {
            return GetLastError();
        }
        if (ref->data != NULL) {
            free(ref->data);
        }
        ref->data = NULL;
        ref->dir = INVALID_HANDLE_VALUE;
    }
    return 0;
}

UInt32 OS_GetMilliseconds(void) {
    return GetTickCount();
}

void __stdcall OS_GetTime(OSTime* p) {
    SYSTEMTIME st;
    FILETIME ft;

    GetSystemTime(&st);
    SystemTimeToFileTime(&st, &ft);
    *p = *(OSTime*)&ft;
}

int __stdcall OS_NewHandle(UInt32 size, OSHandle* hand) {
    size_t len;

    hand->addr = GlobalAlloc(GMEM_FIXED, size);
    hand->used = len = size;
    if (hand->addr == NULL) {
        return GetLastError();
    }
    memset(hand->addr, 0, len);
    return 0;
}

int __stdcall OS_ResizeHandle(OSHandle* hand, UInt32 size) {
    HGLOBAL h;

    h = GlobalReAlloc(hand->addr, size, GMEM_MOVEABLE);
    if (h == NULL) {
        hand->addr = NULL;
        hand->used = 0;
        return GetLastError();
    }
    if (size > hand->used) {
        memset((char*)h + hand->used, 0, size - hand->used);
    }
    hand->addr = h;
    hand->used = size;
    return 0;
}

void* __stdcall OS_LockHandle(OSHandle* hand) {
    UINT flags;

    flags = GlobalFlags(hand->addr);
    if (flags != GMEM_INVALID_HANDLE) {
        return hand->addr;
    }
    return NULL;
}

void __stdcall OS_UnlockHandle(OSHandle* hand) {
    return;
}

int __stdcall OS_FreeHandle(OSHandle* hand) {
    if (GlobalFree(hand->addr)) {
        return GetLastError();
    }
    hand->addr = NULL;
    hand->used = 0;
    return 0;
}

int __stdcall OS_GetHandleSize(OSHandle* hand, UInt32* size) {
    UINT flags;

    flags = GlobalFlags(hand->addr);
    if (flags != GMEM_INVALID_HANDLE) {
        *size = hand->used;
        return 0;
    }
    *size = 0;
    return ERROR_NOT_ENOUGH_MEMORY;
}

void __stdcall OS_InvalidateHandle(OSHandle* hand) {
    hand->addr = NULL;
    hand->used = 0;
}

Boolean __stdcall OS_ValidHandle(OSHandle* hand) {
    return hand != NULL && hand->addr != NULL;
}

int __stdcall OS_OSErrorToMacError(int err) {
    switch (err) {
        case 0:
            return noErr;
        case ERROR_FILE_NOT_FOUND:
            return fnfErr;
        case ERROR_PATH_NOT_FOUND:
        case ERROR_DIRECTORY:
            return dirNFErr;
        case ERROR_TOO_MANY_OPEN_FILES:
            return tmfoErr;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            return permErr;
        case ERROR_INVALID_HANDLE:
            return rfNumErr;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
        case ERROR_SHARING_BUFFER_EXCEEDED:
            return memFullErr;
        case ERROR_INVALID_DATA:
        case ERROR_CURRENT_DIRECTORY:
        case ERROR_BAD_LENGTH:
        case ERROR_INVALID_PARAMETER:
            return paramErr;
        case ERROR_INVALID_DRIVE:
            return nsvErr;
        case ERROR_WRITE_PROTECT:
            return wPrErr;
        case ERROR_NOT_READY:
        case ERROR_CRC:
        case ERROR_SEEK:
        case ERROR_SECTOR_NOT_FOUND:
        case ERROR_OUT_OF_PAPER:
        case ERROR_WRITE_FAULT:
        case ERROR_READ_FAULT:
        case ERROR_GEN_FAILURE:
            return ioErr;
        case ERROR_HANDLE_EOF:
            return eofErr;
        case ERROR_HANDLE_DISK_FULL:
            return dskFulErr;
        case ERROR_ALREADY_EXISTS:
            return dupFNErr;
        case ERROR_INVALID_NAME:
            return bdNamErr;
    }
    return err | 0xffff8000;
}

static SInt32 monthdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

void __stdcall OS_TimeToMac(OSTime sectm, UInt32* secs) {
    SYSTEMTIME st;
    FILETIME ft;
    SInt32 days;
    SInt32 year;

    ft.dwLowDateTime = sectm.dwLowDateTime;
    ft.dwHighDateTime = sectm.dwHighDateTime;
    FileTimeToSystemTime(&ft, &st);

    year = st.wYear - 1904;
    days = year * 365;
    days += (year + 3) / 4 - (year + 4) / 100 + (year - 296) / 400;

    if (st.wYear % 4 == 0 && (st.wYear % 100 != 0 || st.wYear % 400 == 0)) {
        monthdays[1] = 29;
    } else {
        monthdays[1] = 28;
    }

    if (st.wMonth > 12 || st.wMonth == 0) {
        st.wMonth = 1;
    }

    while (--st.wMonth != 0) {
        days += monthdays[st.wMonth - 1];
    }

    days += st.wDay - 1;
    *secs = days * 86400 + st.wHour * 3600 + st.wMinute * 60 + st.wSecond;
}

void __stdcall OS_MacToTime(UInt32 secs, OSTime* sectm) {
    SYSTEMTIME st;
    UInt32 left = secs;
    UInt32 days, year;
    SInt32 yday, month;
    SInt32 greg;

    memset(&st, 0, sizeof(st));
    st.wSecond = secs % 60;
    left /= 60;
    st.wMinute = left % 60;
    left /= 60;
    st.wHour = left % 24;
    left /= 24;
    days = left;
    yday = days % 365;
    year = days / 365;
    st.wYear = year + 1904;
    greg = year - 296;
    yday -= (year + 3) / 4 - (year + 4) / 100 + greg / 400;
    if (st.wYear % 4) {
        monthdays[1] = 28;
    } else {
        monthdays[1] = 29;
    }
    for (month = 0; yday >= monthdays[month]; month++) {
        yday -= monthdays[month];
    }
    st.wMonth = month + 1;
    st.wDay = yday + 1;
    SystemTimeToFileTime(&st, (FILETIME*)sectm);
}

SInt16 __stdcall OS_RefToMac(int ref) {
    if (ref == -1 || (long)ref < 0) {
        return 0;
    }
    OS_ASSERT(1642, (long)ref < 0xffff);
    return ref + 1;
}

int OS_MacToRef(SInt16 refnum) {
    if (refnum != 0) {
        return refnum - 1;
    }
    return -1;
}

int __stdcall OS_LoadMacResourceFork(const OSSpec* spec, void** file_data, SInt32* file_len) {
    HMODULE mod;
    HRSRC rsrc;
    HGLOBAL h;
    void* data;

    if (OS_SpecToString(spec, intbuf, sizeof(intbuf)) == NULL) {
        return ERROR_BUFFER_OVERFLOW;
    }
    mod = GetModuleHandleA(intbuf);
    if (mod == NULL) {
        return GetLastError();
    }
    rsrc = FindResourceA(mod, "#101", "MACRSRC");
    if (rsrc == NULL) {
        return GetLastError();
    }
    h = LoadResource(mod, rsrc);
    if (h == NULL) {
        return GetLastError();
    }
    data = LockResource(h);
    if (data == NULL) {
        return GetLastError();
    }
    *file_data = data;
    *file_len = SizeofResource(mod, rsrc);
    return 0;
}

Boolean __stdcall OS_IsMultiByte(const char* str, const char* pos) {
    const char* p = str;
    while (p <= pos) {
        Boolean lead = IsDBCSLeadByte(*p);
        if (!lead && GetLastError() == ERROR_INVALID_PARAMETER) {
            return FALSE;
        }
        if (lead) {
            if (p == pos || p + 1 == pos) {
                return TRUE;
            }
            p += 2;
        } else {
            p += 1;
        }
    }
    return FALSE;
}
