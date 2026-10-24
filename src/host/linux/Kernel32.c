#include "ansi_prefix.Win32.h"
#include <WINDEF.H>
#include <WINBASE.H>
#include "Heap.h"
#include "Linux.h"
#include "Native.h"

#define PATH_SIZE 4096
#define INVALID_SET_FILE_POINTER ((DWORD)0xFFFFFFFF)
#define INVALID_FILE_ATTRIBUTES ((DWORD)0xFFFFFFFF)

typedef struct _COORD {
    SHORT X;
    SHORT Y;
} COORD;

typedef struct _SMALL_RECT {
    SHORT Left;
    SHORT Top;
    SHORT Right;
    SHORT Bottom;
} SMALL_RECT;

typedef struct _CONSOLE_SCREEN_BUFFER_INFO {
    COORD dwSize;
    COORD dwCursorPosition;
    WORD wAttributes;
    SMALL_RECT srWindow;
    COORD dwMaximumWindowSize;
} CONSOLE_SCREEN_BUFFER_INFO, *PCONSOLE_SCREEN_BUFFER_INFO;

typedef BOOL(WINAPI* PHANDLER_ROUTINE)(DWORD CtrlType);
#define HANDLE_COUNT 8190
#define ERROR_NO_MORE_FILES_ 18
#define ERROR_NOT_SUPPORTED_ 50
#define ERROR_FILE_EXISTS_ 80
#define ERROR_INSUFFICIENT_BUFFER_ 122
#define ERROR_MOD_NOT_FOUND_ 126
#define ERROR_RESOURCE_DATA_NOT_FOUND_ 1812
#define ERROR_RESOURCE_TYPE_NOT_FOUND_ 1813
#define ERROR_DISK_FULL_ 112
#define ERROR_OPERATION_ABORTED_ 995
#define ERROR_READ_FAULT_ 30
#define ERROR_BROKEN_PIPE_ 109
#define ERROR_NEGATIVE_SEEK_ 131

int Native_argc;
char** Native_argv;
char** Native_envp;

static DWORD last_error;

static unsigned int Length(const char* s) {
    unsigned int n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static void CopyBytes(void* to, const void* from, unsigned int size) {
    char* t = to;
    const char* f = from;
    while (size--) {
        *t++ = *f++;
    }
}

static void Fill(void* to, int c, unsigned int size) {
    char* t = to;
    while (size--) {
        *t++ = c;
    }
}

static int Same(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int Lower(int c) {
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int SameIgnoringCase(const char* a, const char* b) {
    while (*a && Lower((unsigned char)*a) == Lower((unsigned char)*b)) {
        a++;
        b++;
    }
    return Lower((unsigned char)*a) == Lower((unsigned char)*b);
}

static void Append(char* to, const char* from) {
    unsigned int n = Length(to);
    while (*from && n + 1 < PATH_SIZE) {
        to[n++] = *from++;
    }
    to[n] = 0;
}

static void AppendLong(char* to, const char* from, unsigned int size) {
    unsigned int n = Length(to);
    while (*from && n + 1 < size) {
        to[n++] = *from++;
    }
    to[n] = 0;
}

static int SameIgnoringCasePrefix(const char* a, const char* b, unsigned int count) {
    while (count--) {
        if (Lower((unsigned char)*a++) != Lower((unsigned char)*b++)) {
            return 0;
        }
    }
    return 1;
}

static void AppendN(char* to, const char* from, unsigned int count) {
    unsigned int n = Length(to);
    while (count-- && *from && n + 1 < PATH_SIZE) {
        to[n++] = *from++;
    }
    to[n] = 0;
}

void WINAPI SetLastError(DWORD dwErrCode) {
    last_error = dwErrCode;
}

DWORD WINAPI GetLastError(void) {
    return last_error;
}

static DWORD ErrorFromErrno(long error) {
    switch (error < 0 ? -error : error) {
        case 0:
            return ERROR_SUCCESS;
        case LINUX_EACCES:
            return ERROR_ACCESS_DENIED;
        case LINUX_EEXIST:
            return ERROR_ALREADY_EXISTS;
        case LINUX_ENOENT:
            return ERROR_FILE_NOT_FOUND;
        case LINUX_ENOTDIR:
            return ERROR_PATH_NOT_FOUND;
        case LINUX_ENOMEM:
            return ERROR_NOT_ENOUGH_MEMORY;
        case LINUX_ENOSPC:
            return ERROR_DISK_FULL_;
        case LINUX_EINVAL:
            return ERROR_INVALID_PARAMETER;
        case LINUX_EINTR:
            return ERROR_OPERATION_ABORTED_;
        case LINUX_EIO:
            return ERROR_READ_FAULT_;
        case LINUX_EPIPE:
            return ERROR_BROKEN_PIPE_;
        case LINUX_ESPIPE:
            return ERROR_INVALID_PARAMETER;
        default:
            return ERROR_NOT_SUPPORTED_;
    }
}

static void SetErrno(long error) {
    last_error = ErrorFromErrno(error);
}

static char cwd[PATH_SIZE];
static void Absolute(const char* host, char* absolute);

static void CurrentDirectory(void) {
    long n = Linux_GetCwd(cwd, sizeof(cwd));
    if (n < 0) {
        cwd[0] = '/';
        cwd[1] = 0;
    }
}

static void Normalize(char* path) {
    static char copy[PATH_SIZE];
    const char* names[PATH_SIZE / 2];
    unsigned int lengths[PATH_SIZE / 2];
    unsigned int count = 0;
    unsigned int i;
    int absolute;
    int trailing = 0;
    const char* p;
    const char* start;

    if (path[0] == 0) {
        return;
    }
    CopyBytes(copy, path, Length(path) + 1);
    absolute = copy[0] == '/';
    for (p = copy; *p;) {
        while (*p == '/') {
            p++;
        }
        if (*p == 0) {
            break;
        }
        start = p;
        while (*p && *p != '/') {
            p++;
        }
        names[count] = start;
        lengths[count] = p - start;
        trailing = *p == '/';
        count++;
    }
    {
        unsigned int out = 0;
        for (i = 0; i < count; i++) {
            if (lengths[i] == 1 && names[i][0] == '.') {
                if (i + 1 == count) {
                    trailing = 1;
                }
                continue;
            }
            if (lengths[i] == 2 &&
                names[i][0] == '.' &&
                names[i][1] == '.' &&
                out > 0 &&
                !(lengths[out - 1] == 2 && names[out - 1][0] == '.' && names[out - 1][1] == '.')) {
                out--;
                if (i + 1 == count) {
                    trailing = 1;
                }
                continue;
            }
            names[out] = names[i];
            lengths[out] = lengths[i];
            out++;
        }
        count = out;
    }
    if (absolute) {
        unsigned int skip = 0;
        while (
            skip < count && lengths[skip] == 2 && names[skip][0] == '.' && names[skip][1] == '.') {
            skip++;
        }
        for (i = skip; i < count; i++) {
            names[i - skip] = names[i];
            lengths[i - skip] = lengths[i];
        }
        count -= skip;
    }
    if (count > 0 &&
        lengths[count - 1] == 2 &&
        names[count - 1][0] == '.' &&
        names[count - 1][1] == '.') {
        trailing = 0;
    }
    path[0] = 0;
    if (absolute) {
        Append(path, "/");
    }
    for (i = 0; i < count; i++) {
        if (i > 0) {
            Append(path, "/");
        }
        AppendN(path, names[i], lengths[i]);
    }
    if (trailing && count > 0) {
        Append(path, "/");
    }
    if (path[0] == 0) {
        Append(path, ".");
    }
}

typedef struct Directory {
    struct Directory* next;
    char* path;
    char* names;
    int missing;
} Directory;

static Directory* directories;

static void ForgetDirectories(void) {
    directories = 0;
}

static void* Allocate(unsigned int size) {
    void* p = Heap_MapAligned(size ? size : 1, 4096);
    if (p == 0) {
        Linux_Exit(1);
    }
    return p;
}

static char* cache_memory;
static unsigned int cache_used;
static unsigned int cache_size;

static void* CacheAllocate(unsigned int size) {
    void* p;

    size = (size + 7) & ~7u;
    if (cache_used + size > cache_size) {
        cache_size = size > (1u << 20) ? size : (1u << 20);
        cache_memory = Allocate(cache_size);
        cache_used = 0;
    }
    p = cache_memory + cache_used;
    cache_used += size;
    return p;
}

static Directory* ReadDirectory(const char* path) {
    Directory* d;
    static char buffer[32768];
    char* names;
    unsigned int size = 0;
    unsigned int capacity = 4096;
    long fd;
    long n;
    long offset;

    for (d = directories; d; d = d->next) {
        if (Same(d->path, path)) {
            return d;
        }
    }
    d = CacheAllocate(sizeof(Directory));
    d->path = CacheAllocate(Length(path) + 1);
    CopyBytes(d->path, path, Length(path) + 1);
    d->names = 0;
    d->missing = 0;
    d->next = directories;
    directories = d;
    fd = Linux_Open(path, LINUX_O_RDONLY | LINUX_O_DIRECTORY | LINUX_O_CLOEXEC, 0);
    if (fd < 0) {
        d->missing = 1;
        return d;
    }
    names = Allocate(capacity);
    while ((n = Linux_GetDents(fd, buffer, sizeof(buffer))) > 0) {
        for (offset = 0; offset < n;) {
            unsigned short record = *(unsigned short*)(buffer + offset + 16);
            const char* name = buffer + offset + 19;
            unsigned int length = Length(name);
            offset += record;
            if ((length == 1 && name[0] == '.') ||
                (length == 2 && name[0] == '.' && name[1] == '.')) {
                continue;
            }
            if (size + length + 2 > capacity) {
                char* grown = Allocate(capacity * 2);
                CopyBytes(grown, names, size);
                Linux_Unmap(names, capacity);
                names = grown;
                capacity *= 2;
            }
            CopyBytes(names + size, name, length + 1);
            size += length + 1;
        }
    }
    Linux_Close(fd);
    names[size] = 0;
    d->names = CacheAllocate(size + 1);
    CopyBytes(d->names, names, size + 1);
    Linux_Unmap(names, capacity);
    return d;
}

static int Exists(const char* path) {
    LinuxStat st;
    return Linux_Stat(path, &st) == 0;
}

static char program_path[PATH_SIZE];
static char program_alias[PATH_SIZE];

static void ProgramPath(void) {
    long n;

    if (program_path[0] != 0) {
        return;
    }
    n = Linux_ReadLink("/proc/self/exe", program_path, sizeof(program_path) - 1);
    if (n > 0) {
        program_path[n] = 0;
    } else {
        Absolute(Native_argv[0], program_path);
    }
    program_alias[0] = 0;
    Append(program_alias, program_path);
    n = Length(program_alias);
    if (n < 4 || !SameIgnoringCase(program_alias + n - 4, ".exe")) {
        Append(program_alias, ".exe");
    }
}

static void FindHostPath(const char* windows, char* host);

static void HostPath(const char* windows, char* host) {
    static char absolute[PATH_SIZE];
    unsigned int n;

    FindHostPath(windows, host);
    n = Length(host);
    if (n >= 4 && SameIgnoringCase(host + n - 4, ".exe")) {
        ProgramPath();
        Absolute(host, absolute);
        Normalize(absolute);
        if (Same(absolute, program_alias) && !Exists(host)) {
            host[0] = 0;
            Append(host, program_path);
        }
    }
}

static void FindHostPath(const char* windows, char* host) {
    static char path[PATH_SIZE];
    static char found[PATH_SIZE];
    char* in;
    char* out;
    const char* p;
    int following;

    path[0] = 0;
    Append(path, windows);
    for (in = path; *in; in++) {
        if (*in == '\\') {
            *in = '/';
        }
    }
    in = path;
    if (in[0] == '/' && in[1] == '/' && in[2] == '?' && in[3] == '/') {
        in += 4;
    }
    if ((in[0] == 'z' || in[0] == 'Z' || in[0] == 'c' || in[0] == 'C') &&
        in[1] == ':' &&
        in[2] == '/') {
        in += 2;
    }
    for (out = path, p = in; *p;) {
        const char* start = p;
        const char* end;
        while (*p && *p != '/') {
            p++;
        }
        end = p;
        if (!(end - start == 1 && start[0] == '.') &&
            !(end - start == 2 && start[0] == '.' && start[1] == '.')) {
            while (end > start && end[-1] == '.') {
                end--;
            }
        }
        while (start < end) {
            *out++ = *start++;
        }
        if (*p == '/') {
            *out++ = '/';
            p++;
        }
    }
    *out = 0;
    Normalize(path);
    if (path[0] == 0 || Exists(path)) {
        host[0] = 0;
        Append(host, path[0] != 0 ? path : ".");
        return;
    }
    found[0] = 0;
    Append(found, ".");
    following = 1;
    for (p = path; *p || p == path;) {
        static char name[PATH_SIZE];
        static char next[PATH_SIZE];
        const char* start;

        if (*p == '/' && p == path) {
            next[0] = 0;
            Append(next, "/");
            p++;
            found[0] = 0;
            Append(found, next);
            if (*p == 0) {
                break;
            }
            continue;
        }
        start = p;
        while (*p && *p != '/') {
            p++;
        }
        name[0] = 0;
        AppendN(name, start, p - start);
        next[0] = 0;
        Append(next, found);
        if (next[Length(next) - 1] != '/') {
            Append(next, "/");
        }
        Append(next, name);
        if (following && !Exists(next) && name[0] != 0 && !Same(name, ".") && !Same(name, "..")) {
            Directory* d = ReadDirectory(found);
            following = 0;
            if (!d->missing) {
                const char* candidate;
                for (candidate = d->names; *candidate; candidate += Length(candidate) + 1) {
                    if (SameIgnoringCase(candidate, name)) {
                        following = 1;
                        next[0] = 0;
                        Append(next, found);
                        if (next[Length(next) - 1] != '/') {
                            Append(next, "/");
                        }
                        Append(next, candidate);
                        break;
                    }
                }
            }
        }
        found[0] = 0;
        Append(found, next);
        if (*p == '/') {
            p++;
            if (*p == 0) {
                Append(found, "/");
                break;
            }
        } else {
            break;
        }
    }
    host[0] = 0;
    Append(host, found);
}

static void WindowsPath(const char* host, char* windows) {
    char* p;

    windows[0] = 0;
    if (host[0] == '/') {
        Append(windows, "Z:");
    }
    Append(windows, host);
    Normalize(windows + (host[0] == '/' ? 2 : 0));
    for (p = windows; *p; p++) {
        if (*p == '/') {
            *p = '\\';
        }
    }
}

static void Absolute(const char* host, char* absolute) {
    absolute[0] = 0;
    if (host[0] == '/') {
        Append(absolute, host);
        return;
    }
    CurrentDirectory();
    Append(absolute, cwd);
    if (host[0] != 0) {
        if (absolute[Length(absolute) - 1] != '/') {
            Append(absolute, "/");
        }
        Append(absolute, host);
    }
}

enum {
    HANDLE_FREE,
    HANDLE_FILE,
    HANDLE_DIRECTORY
};

typedef struct HandleEntry {
    int kind;
    int fd;
    int append_only;
    int pipe;
    int close_fd;
    LinuxOffset position;
} HandleEntry;

static HandleEntry handles[HANDLE_COUNT];
static unsigned int next_handle;
static HANDLE std_handles[3];

static HANDLE NewHandle(int kind, int fd) {
    unsigned int i;

    for (i = next_handle; i < HANDLE_COUNT; i++) {
        if (handles[i].kind == HANDLE_FREE) {
            break;
        }
    }
    if (i == HANDLE_COUNT) {
        for (i = 0; i < HANDLE_COUNT && handles[i].kind != HANDLE_FREE; i++) {}
        if (i == HANDLE_COUNT) {
            return INVALID_HANDLE_VALUE;
        }
    } else {
        next_handle = i + 1;
    }
    handles[i].kind = kind;
    handles[i].fd = fd;
    handles[i].append_only = 0;
    handles[i].close_fd = 1;
    handles[i].position = 0;
    handles[i].pipe =
        fd >= 0 && Linux_LSeek(fd, 0, LINUX_SEEK_CUR, &handles[i].position) == -LINUX_ESPIPE;
    if (handles[i].pipe || handles[i].position < 0) {
        handles[i].position = 0;
    }
    return (HANDLE)((i + 1) << 2);
}

static HandleEntry* Lookup(HANDLE h) {
    unsigned long v = (unsigned long)h;
    if (v == 0 || (v & 3) != 0 || (v >> 2) - 1 >= HANDLE_COUNT) {
        return 0;
    }
    if (handles[(v >> 2) - 1].kind == HANDLE_FREE) {
        return 0;
    }
    return &handles[(v >> 2) - 1];
}

HANDLE WINAPI GetStdHandle(DWORD nStdHandle) {
    switch ((long)nStdHandle) {
        case -10:
            return std_handles[0];
        case -11:
            return std_handles[1];
        case -12:
            return std_handles[2];
    }
    return INVALID_HANDLE_VALUE;
}

BOOL WINAPI SetStdHandle(DWORD nStdHandle, HANDLE hHandle) {
    switch ((long)nStdHandle) {
        case -10:
            std_handles[0] = hHandle;
            return TRUE;
        case -11:
            std_handles[1] = hHandle;
            return TRUE;
        case -12:
            std_handles[2] = hHandle;
            return TRUE;
    }
    return FALSE;
}

BOOL WINAPI CloseHandle(HANDLE hObject) {
    HandleEntry* entry = Lookup(hObject);

    if (entry == 0) {
        last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    if (entry->close_fd && entry->fd >= 0) {
        Linux_Close(entry->fd);
    }
    entry->kind = HANDLE_FREE;
    return TRUE;
}

BOOL WINAPI WriteFile(
    HANDLE hFile,
    LPCVOID lpBuffer,
    DWORD nNumberOfBytesToWrite,
    LPDWORD lpNumberOfBytesWritten,
    LPOVERLAPPED lpOverlapped
) {
    HandleEntry* entry = Lookup(hFile);
    DWORD total = 0;
    long error = 0;
    long n;

    if (entry == 0 || entry->kind != HANDLE_FILE) {
        last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    if (lpOverlapped == 0 && lpNumberOfBytesWritten == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    if (lpNumberOfBytesWritten) {
        *lpNumberOfBytesWritten = 0;
    }
    while (total < nNumberOfBytesToWrite) {
        if (entry->append_only || entry->pipe) {
            n = Linux_Write(
                entry->fd, (const char*)lpBuffer + total, nNumberOfBytesToWrite - total
            );
        } else {
            n = Linux_PWrite(
                entry->fd,
                (const char*)lpBuffer + total,
                nNumberOfBytesToWrite - total,
                entry->position + total
            );
        }
        if (n == -LINUX_EINTR) {
            continue;
        }
        if (n < 0) {
            error = n;
            SetErrno(n);
            break;
        }
        if (n == 0) {
            break;
        }
        total += n;
    }
    if (!entry->append_only && !entry->pipe) {
        entry->position += total;
    }
    if (lpNumberOfBytesWritten) {
        *lpNumberOfBytesWritten = total;
    }
    return error == 0;
}

BOOL WINAPI ReadFile(
    HANDLE hFile,
    LPVOID lpBuffer,
    DWORD nNumberOfBytesToRead,
    LPDWORD lpNumberOfBytesRead,
    LPOVERLAPPED lpOverlapped
) {
    HandleEntry* entry = Lookup(hFile);
    DWORD total = 0;
    long n = 0;

    if (entry == 0 || entry->kind != HANDLE_FILE) {
        last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    if (lpOverlapped == 0 && lpNumberOfBytesRead == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    if (lpNumberOfBytesRead) {
        *lpNumberOfBytesRead = 0;
    }
    if (entry->append_only || entry->pipe) {
        do {
            n = Linux_Read(entry->fd, lpBuffer, nNumberOfBytesToRead);
        } while (n == -LINUX_EINTR);
        if (n < 0) {
            SetErrno(n);
            return FALSE;
        }
        if (lpNumberOfBytesRead) {
            *lpNumberOfBytesRead = n;
        }
        return TRUE;
    }
    while (total < nNumberOfBytesToRead) {
        n = Linux_PRead(
            entry->fd,
            (char*)lpBuffer + total,
            nNumberOfBytesToRead - total,
            entry->position + total
        );
        if (n == -LINUX_EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        total += n;
    }
    entry->position += total;
    if (n < 0) {
        SetErrno(n);
        if (lpNumberOfBytesRead) {
            *lpNumberOfBytesRead = total;
        }
        return FALSE;
    }
    if (lpNumberOfBytesRead) {
        *lpNumberOfBytesRead = total;
    }
    return TRUE;
}

DWORD WINAPI
SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) {
    HandleEntry* entry = Lookup(hFile);
    LinuxOffset position = 0;
    long result;

    if (entry == 0 || entry->kind != HANDLE_FILE) {
        last_error = ERROR_INVALID_HANDLE;
        return INVALID_SET_FILE_POINTER;
    }
    if (dwMoveMethod == FILE_BEGIN) {
        position = lDistanceToMove;
    } else if (dwMoveMethod == FILE_CURRENT) {
        position = entry->position + lDistanceToMove;
    } else if (dwMoveMethod == FILE_END) {
        result = Linux_LSeek(entry->fd, lDistanceToMove, LINUX_SEEK_END, &position);
        if (result < 0) {
            last_error = result == -LINUX_EINVAL ? ERROR_NEGATIVE_SEEK_ : ERROR_INVALID_PARAMETER;
            return INVALID_SET_FILE_POINTER;
        }
    }
    if (position < 0) {
        last_error = ERROR_NEGATIVE_SEEK_;
        return INVALID_SET_FILE_POINTER;
    }
    entry->position = position;
    if (lpDistanceToMoveHigh) {
        *lpDistanceToMoveHigh = (LONG)((unsigned long long)position >> 32);
    }
    return (DWORD)position;
}

DWORD WINAPI GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh) {
    HandleEntry* entry = Lookup(hFile);
    LinuxOffset size;
    long result;

    if (entry == 0 || entry->kind != HANDLE_FILE) {
        last_error = ERROR_INVALID_HANDLE;
        return INVALID_FILE_SIZE;
    }
    result = Linux_LSeek(entry->fd, 0, LINUX_SEEK_END, &size);
    if (result < 0) {
        if (lpFileSizeHigh) {
            *lpFileSizeHigh = 0;
        }
        SetErrno(result);
        return INVALID_FILE_SIZE;
    }
    if (lpFileSizeHigh) {
        *lpFileSizeHigh = (DWORD)((unsigned long long)size >> 32);
    }
    return (DWORD)size;
}

BOOL WINAPI SetEndOfFile(HANDLE hFile) {
    HandleEntry* entry = Lookup(hFile);
    long result;

    if (entry == 0 || entry->kind != HANDLE_FILE) {
        last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    result = Linux_FTruncate(entry->fd, entry->position);
    if (result < 0) {
        SetErrno(result);
        return FALSE;
    }
    return TRUE;
}

#define UNIX_EPOCH_IN_FILETIME 116444736000000000ULL

static void ToFileTime(long long seconds, long long nanoseconds, FILETIME* ft) {
    unsigned long long t =
        (unsigned long long)(seconds * 10000000LL + nanoseconds / 100) + UNIX_EPOCH_IN_FILETIME;
    ft->dwLowDateTime = (DWORD)t;
    ft->dwHighDateTime = (DWORD)(t >> 32);
}

BOOL WINAPI GetFileTime(
    HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime
) {
    HandleEntry* entry = Lookup(hFile);
    LinuxStat st;
    long result;

    if (entry == 0 || entry->kind != HANDLE_FILE) {
        last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    result = Linux_FStat(entry->fd, &st);
    if (result < 0) {
        SetErrno(result);
        return FALSE;
    }
    if (lpCreationTime) {
        ToFileTime(st.ctime, st.ctime_nsec, lpCreationTime);
    }
    if (lpLastAccessTime) {
        ToFileTime(st.atime, st.atime_nsec, lpLastAccessTime);
    }
    if (lpLastWriteTime) {
        ToFileTime(st.mtime, st.mtime_nsec, lpLastWriteTime);
    }
    return TRUE;
}

BOOL WINAPI SetFileTime(
    HANDLE hFile,
    const FILETIME* lpCreationTime,
    const FILETIME* lpLastAccessTime,
    const FILETIME* lpLastWriteTime
) {
    HandleEntry* entry = Lookup(hFile);
    long times[4];
    unsigned long long t;
    long result;

    if (entry == 0 || entry->kind != HANDLE_FILE) {
        last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    times[0] = 0;
    times[1] = (1l << 30) - 2;
    times[2] = 0;
    times[3] = (1l << 30) - 2;
    if (lpLastAccessTime &&
        !((lpLastAccessTime->dwLowDateTime == 0 && lpLastAccessTime->dwHighDateTime == 0) ||
          (lpLastAccessTime->dwLowDateTime == 0xFFFFFFFF &&
           lpLastAccessTime->dwHighDateTime == 0xFFFFFFFF))) {
        t = ((unsigned long long)lpLastAccessTime->dwHighDateTime << 32 |
             lpLastAccessTime->dwLowDateTime) -
            UNIX_EPOCH_IN_FILETIME;
        times[0] = (long)(t / 10000000);
        times[1] = (long)(t % 10000000) * 100;
    }
    if (lpLastWriteTime &&
        !((lpLastWriteTime->dwLowDateTime == 0 && lpLastWriteTime->dwHighDateTime == 0) ||
          (lpLastWriteTime->dwLowDateTime == 0xFFFFFFFF &&
           lpLastWriteTime->dwHighDateTime == 0xFFFFFFFF))) {
        t = ((unsigned long long)lpLastWriteTime->dwHighDateTime << 32 |
             lpLastWriteTime->dwLowDateTime) -
            UNIX_EPOCH_IN_FILETIME;
        times[2] = (long)(t / 10000000);
        times[3] = (long)(t % 10000000) * 100;
    }
    (void)lpCreationTime;
    result = Linux_Call4(LINUX_SYS_utimensat, entry->fd, 0, (long)times, 0);
    if (result < 0) {
        SetErrno(result);
        return FALSE;
    }
    return TRUE;
}

#define FILE_STATUS_MISSING 0
#define FILE_STATUS_FILE 1
#define FILE_STATUS_DIRECTORY 2
#define FILE_STATUS_OTHER 3

static long Status(const char* path) {
    LinuxStat st;
    long result = Linux_Stat(path, &st);
    if (result < 0) {
        return result;
    }
    if ((st.mode & LINUX_S_IFMT) == LINUX_S_IFREG) {
        return FILE_STATUS_FILE;
    }
    if ((st.mode & LINUX_S_IFMT) == LINUX_S_IFDIR) {
        return FILE_STATUS_DIRECTORY;
    }
    return FILE_STATUS_OTHER;
}

HANDLE WINAPI CreateFileA(
    LPCSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile
) {
    static char host[PATH_SIZE];
    long status;
    int exists;
    int directory;
    int create = 0;
    int truncate = 0;
    int flags = LINUX_O_CLOEXEC;
    int read;
    int write;
    long fd;
    HANDLE handle;

    (void)dwShareMode;
    (void)lpSecurityAttributes;
    (void)hTemplateFile;
    if (lpFileName == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return INVALID_HANDLE_VALUE;
    }
    HostPath(lpFileName, host);
    status = Status(host);
    exists = status > 0;
    directory = status == FILE_STATUS_DIRECTORY;
    if ((dwFlagsAndAttributes & FILE_ATTRIBUTE_DIRECTORY) && !directory) {
        last_error = ERROR_INVALID_PARAMETER;
        return INVALID_HANDLE_VALUE;
    }
    if (directory &&
        (!(dwFlagsAndAttributes & FILE_FLAG_BACKUP_SEMANTICS) ||
         (dwFlagsAndAttributes & FILE_FLAG_DELETE_ON_CLOSE))) {
        last_error = ERROR_ACCESS_DENIED;
        return INVALID_HANDLE_VALUE;
    }
    switch (dwCreationDisposition) {
        case CREATE_NEW:
            if (exists) {
                last_error = ERROR_FILE_EXISTS_;
                return INVALID_HANDLE_VALUE;
            }
            create = 1;
            flags |= LINUX_O_CREAT | LINUX_O_EXCL;
            break;
        case CREATE_ALWAYS:
            if (directory) {
                last_error = ERROR_ACCESS_DENIED;
                return INVALID_HANDLE_VALUE;
            }
            create = 1;
            truncate = exists;
            flags |= LINUX_O_CREAT;
            break;
        case OPEN_ALWAYS:
            if (!exists) {
                create = 1;
                flags |= LINUX_O_CREAT;
            } else if (directory) {
                last_error = ERROR_ACCESS_DENIED;
                return INVALID_HANDLE_VALUE;
            }
            break;
        case OPEN_EXISTING:
            if (!exists) {
                last_error = ERROR_FILE_NOT_FOUND;
                return INVALID_HANDLE_VALUE;
            }
            break;
        case TRUNCATE_EXISTING:
            if (!exists) {
                last_error = ERROR_FILE_NOT_FOUND;
                return INVALID_HANDLE_VALUE;
            }
            if (directory) {
                last_error = ERROR_ACCESS_DENIED;
                return INVALID_HANDLE_VALUE;
            }
            truncate = 1;
            break;
        default:
            last_error = ERROR_INVALID_PARAMETER;
            return INVALID_HANDLE_VALUE;
    }
    if (truncate) {
        flags |= LINUX_O_TRUNC;
    }
    read = (dwDesiredAccess & (GENERIC_READ | GENERIC_EXECUTE | GENERIC_ALL | FILE_READ_DATA)) != 0;
    write =
        (dwDesiredAccess & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0;
    if (directory) {
        flags |= LINUX_O_RDONLY | LINUX_O_DIRECTORY;
    } else {
        int need_write = write || truncate || create;
        int need_read = read || !need_write;
        flags |= need_write && need_read ? LINUX_O_RDWR
            : need_write                 ? LINUX_O_WRONLY
                                         : LINUX_O_RDONLY;
    }
    fd = Linux_Open(host, flags, 0666);
    if (fd < 0) {
        SetErrno(fd);
        return INVALID_HANDLE_VALUE;
    }
    if (create && !exists) {
        ForgetDirectories();
    }
    handle = NewHandle(directory ? HANDLE_DIRECTORY : HANDLE_FILE, fd);
    if (handle == INVALID_HANDLE_VALUE) {
        Linux_Close(fd);
        last_error = ERROR_TOO_MANY_OPEN_FILES;
        return INVALID_HANDLE_VALUE;
    }
    if ((dwCreationDisposition == OPEN_ALWAYS || dwCreationDisposition == CREATE_ALWAYS) &&
        exists) {
        last_error = ERROR_ALREADY_EXISTS;
    }
    return handle;
}

BOOL WINAPI DeleteFileA(LPCSTR lpFileName) {
    static char host[PATH_SIZE];
    long result;

    if (lpFileName == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    HostPath(lpFileName, host);
    result = Linux_Unlink(host);
    ForgetDirectories();
    if (result < 0) {
        SetErrno(result);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI CreateDirectoryA(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes) {
    static char host[PATH_SIZE];
    long result;

    (void)lpSecurityAttributes;
    if (lpPathName == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    HostPath(lpPathName, host);
    result = Linux_Mkdir(host, 0755);
    ForgetDirectories();
    if (result < 0) {
        SetErrno(result);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI RemoveDirectoryA(LPCSTR lpPathName) {
    static char host[PATH_SIZE];
    long result;

    if (lpPathName == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    HostPath(lpPathName, host);
    result = Linux_Rmdir(host);
    ForgetDirectories();
    if (result < 0) {
        SetErrno(result);
        return FALSE;
    }
    return TRUE;
}

DWORD WINAPI GetFileAttributesA(LPCSTR lpFileName) {
    static char host[PATH_SIZE];
    long status;
    unsigned int n;

    if (lpFileName == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return INVALID_FILE_ATTRIBUTES;
    }
    HostPath(lpFileName, host);
    n = Length(host);
    if (n >= 12 && Same(host + n - 12, "/license.dat")) {
        return FILE_ATTRIBUTE_NORMAL;
    }
    status = Status(host);
    if (status < 0) {
        SetErrno(status);
        return INVALID_FILE_ATTRIBUTES;
    }
    if (status == FILE_STATUS_FILE) {
        return FILE_ATTRIBUTE_NORMAL;
    }
    if (status == FILE_STATUS_DIRECTORY) {
        return FILE_ATTRIBUTE_DIRECTORY;
    }
    last_error = ERROR_FILE_NOT_FOUND;
    return INVALID_FILE_ATTRIBUTES;
}

BOOL WINAPI SetFileAttributesA(LPCSTR lpFileName, DWORD dwFileAttributes) {
    (void)dwFileAttributes;
    if (lpFileName == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    return TRUE;
}

DWORD WINAPI
GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR* lpFilePart) {
    static char host[PATH_SIZE];
    static char absolute[PATH_SIZE];
    static char windows[PATH_SIZE];
    unsigned int n;
    int separator_end;
    int last_slash = -1;
    unsigned int i;

    if (lpFilePart) {
        *lpFilePart = 0;
    }
    if (lpFileName == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return 0;
    }
    n = Length(lpFileName);
    separator_end = n > 0 && (lpFileName[n - 1] == '\\' || lpFileName[n - 1] == '/');
    HostPath(lpFileName, host);
    Absolute(host, absolute);
    WindowsPath(absolute, windows);
    n = Length(windows);
    if (separator_end && n > 0 && windows[n - 1] != '\\') {
        Append(windows, "\\");
        n++;
    }
    if (nBufferLength == 0) {
        last_error = ERROR_INSUFFICIENT_BUFFER_;
        return n + 1;
    }
    if (lpBuffer == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return 0;
    }
    if (nBufferLength < n + 1) {
        last_error = ERROR_INSUFFICIENT_BUFFER_;
        return n + 1;
    }
    CopyBytes(lpBuffer, windows, n + 1);
    if (lpFilePart && n > 0 && windows[n - 1] != '\\') {
        for (i = 0; i < n; i++) {
            if (windows[i] == '\\') {
                last_slash = i;
            }
        }
        if ((unsigned int)(last_slash + 1) < n) {
            *lpFilePart = lpBuffer + last_slash + 1;
        }
    }
    return n;
}

DWORD WINAPI GetShortPathNameA(LPCSTR lpszLongPath, LPSTR lpszShortPath, DWORD cchBuffer) {
    static char host[PATH_SIZE];
    static char absolute[PATH_SIZE];
    static char windows[PATH_SIZE];
    unsigned int n;

    if (lpszLongPath == 0 || lpszShortPath == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return 0;
    }
    HostPath(lpszLongPath, host);
    Absolute(host, absolute);
    WindowsPath(absolute, windows);
    n = Length(windows);
    if (cchBuffer < n + 1) {
        last_error = ERROR_INSUFFICIENT_BUFFER_;
        return n + 1;
    }
    CopyBytes(lpszShortPath, windows, n + 1);
    return n;
}

DWORD WINAPI GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer) {
    static char windows[PATH_SIZE];
    unsigned int n;

    CurrentDirectory();
    WindowsPath(cwd, windows);
    n = Length(windows);
    if (nBufferLength == 0) {
        return n + 1;
    }
    if (lpBuffer == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return 0;
    }
    if (nBufferLength < n + 1) {
        last_error = ERROR_INSUFFICIENT_BUFFER_;
        return n + 1;
    }
    CopyBytes(lpBuffer, windows, n + 1);
    return n;
}

UINT WINAPI GetSystemDirectoryA(LPSTR lpBuffer, UINT uSize) {
    static const char directory[] = "C:\\Windows\\System32";
    if (lpBuffer == 0) {
        return 0;
    }
    if (uSize < sizeof(directory)) {
        return sizeof(directory);
    }
    CopyBytes(lpBuffer, directory, sizeof(directory));
    return sizeof(directory) - 1;
}

UINT WINAPI GetWindowsDirectoryA(LPSTR lpBuffer, UINT uSize) {
    static const char directory[] = "C:\\Windows";
    if (lpBuffer == 0) {
        return 0;
    }
    if (uSize < sizeof(directory)) {
        return sizeof(directory);
    }
    CopyBytes(lpBuffer, directory, sizeof(directory));
    return sizeof(directory) - 1;
}

typedef struct FindEntry {
    char* name;
    int directory;
} FindEntry;

typedef struct FindSearch {
    int single;
    unsigned int count;
    unsigned int next;
    FindEntry* entries;
    char* paths;
} FindSearch;

#define FIND_COUNT 256
static FindSearch* searches[FIND_COUNT];
static unsigned int next_search = 1;

static int WildcardMatch(const char* pattern, const char* text) {
    unsigned int p = 0;
    unsigned int t = 0;
    int star = -1;
    unsigned int match = 0;
    unsigned int pn = Length(pattern);
    unsigned int tn = Length(text);

    while (t < tn) {
        if (p < pn && pattern[p] == '?') {
            p++;
            t++;
        } else if (p < pn && pattern[p] == '*') {
            star = p++;
            match = t;
        } else if (p < pn && Lower((unsigned char)pattern[p]) == Lower((unsigned char)text[t])) {
            p++;
            t++;
        } else if (star >= 0) {
            p = star + 1;
            t = ++match;
        } else {
            return 0;
        }
    }
    while (p < pn && pattern[p] == '*') {
        p++;
    }
    return p == pn;
}

static void RealName(const char* host, char* name) {
    static char link[PATH_SIZE];
    static char path[PATH_SIZE];
    const char* last;
    long n;
    int depth;

    path[0] = 0;
    Append(path, host);
    for (depth = 0; depth < 40; depth++) {
        n = Linux_ReadLink(path, link, sizeof(link) - 1);
        if (n <= 0) {
            break;
        }
        link[n] = 0;
        if (link[0] == '/') {
            path[0] = 0;
            Append(path, link);
        } else {
            char* slash = 0;
            char* c;
            for (c = path; *c; c++) {
                if (*c == '/') {
                    slash = c;
                }
            }
            if (slash) {
                slash[1] = 0;
            } else {
                path[0] = 0;
            }
            Append(path, link);
        }
        Normalize(path);
    }
    Normalize(path);
    last = path;
    {
        const char* c;
        unsigned int length = Length(path);
        while (length > 1 && path[length - 1] == '/') {
            path[--length] = 0;
        }
        for (c = path; *c; c++) {
            if (*c == '/' && c[1]) {
                last = c + 1;
            }
        }
    }
    name[0] = 0;
    Append(name, last);
}

static void FillFindData(
    const char* host, const char* name, int directory, LPWIN32_FIND_DATAA data
) {
    LinuxStat st;
    unsigned int n;

    Fill(data, 0, sizeof(*data));
    if (host[0] && Linux_Stat(host, &st) == 0) {
        data->dwFileAttributes = directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        if (!directory && (st.mode & LINUX_S_IFMT) == LINUX_S_IFREG) {
            data->nFileSizeHigh = (DWORD)((unsigned long long)st.size >> 32);
            data->nFileSizeLow = (DWORD)st.size;
        }
        ToFileTime(st.ctime, st.ctime_nsec, &data->ftCreationTime);
        ToFileTime(st.atime, st.atime_nsec, &data->ftLastAccessTime);
        ToFileTime(st.mtime, st.mtime_nsec, &data->ftLastWriteTime);
    } else {
        data->dwFileAttributes = directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    }
    n = Length(name);
    if (n > MAX_PATH - 1) {
        n = MAX_PATH - 1;
    }
    CopyBytes(data->cFileName, name, n);
    data->cFileName[n] = 0;
}

static HANDLE NewSearch(FindSearch* search) {
    unsigned int n;
    unsigned int i = next_search;

    for (n = 1; n < FIND_COUNT; n++) {
        if (searches[i] == 0) {
            searches[i] = search;
            next_search = i + 1 < FIND_COUNT ? i + 1 : 1;
            return (HANDLE)i;
        }
        i = i + 1 < FIND_COUNT ? i + 1 : 1;
    }
    last_error = ERROR_TOO_MANY_OPEN_FILES;
    return INVALID_HANDLE_VALUE;
}

HANDLE WINAPI FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData) {
    static char input[PATH_SIZE];
    static char directory[PATH_SIZE];
    static char host_directory[PATH_SIZE];
    static char host[PATH_SIZE];
    static char name[PATH_SIZE];
    static char entry_path[PATH_SIZE];
    const char* file_part;
    int last_slash = -1;
    unsigned int i;
    unsigned int n;
    long status;
    FindSearch* search;

    if (lpFindFileData == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return INVALID_HANDLE_VALUE;
    }
    if (lpFileName == 0 || lpFileName[0] == 0) {
        last_error = ERROR_PATH_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    input[0] = 0;
    Append(input, lpFileName);
    n = Length(input);
    for (i = 0; i < n; i++) {
        if (input[i] == '/') {
            input[i] = '\\';
        }
        if (input[i] == '\\') {
            last_slash = i;
        }
    }
    if (input[n - 1] == '\\') {
        last_error = ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    directory[0] = 0;
    if (last_slash < 0) {
        Append(directory, ".");
        file_part = input;
    } else {
        AppendN(directory, input, last_slash);
        file_part = input + last_slash + 1;
        if (directory[0] == 0) {
            Append(directory, "\\");
        } else if (last_slash == 2 && input[1] == ':') {
            directory[0] = 0;
            AppendN(directory, input, last_slash + 1);
        }
    }
    for (i = 0; directory[i]; i++) {
        if (directory[i] == '*' || directory[i] == '?') {
            last_error = ERROR_INVALID_NAME;
            return INVALID_HANDLE_VALUE;
        }
    }
    HostPath(directory, host_directory);
    status = Status(host_directory);
    if (status < 0) {
        SetErrno(status);
        return INVALID_HANDLE_VALUE;
    }
    if (status != FILE_STATUS_DIRECTORY) {
        last_error = ERROR_PATH_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    search = CacheAllocate(sizeof(FindSearch));
    Fill(search, 0, sizeof(*search));
    for (i = 0; file_part[i]; i++) {
        if (file_part[i] == '*' || file_part[i] == '?') {
            break;
        }
    }
    if (file_part[i] == 0) {
        HostPath(input, host);
        status = Status(host);
        if (status < 0) {
            SetErrno(status);
            return INVALID_HANDLE_VALUE;
        }
        RealName(host, name);
        if (name[0] == 0 || Same(name, ".") || Same(name, "..")) {
            name[0] = 0;
            Append(name, file_part);
        }
        FillFindData(host, name, status == FILE_STATUS_DIRECTORY, lpFindFileData);
        last_error = ERROR_SUCCESS;
        search->single = 1;
        return NewSearch(search);
    }
    {
        Directory* d;
        const char* candidate;
        unsigned int count = 0;
        unsigned int k = 0;

        ForgetDirectories();
        d = ReadDirectory(host_directory);
        if (d->names == 0) {
            last_error = ERROR_ACCESS_DENIED;
            return INVALID_HANDLE_VALUE;
        }
        for (candidate = d->names; *candidate; candidate += Length(candidate) + 1) {
            count++;
        }
        search->entries = CacheAllocate((count + 2) * sizeof(FindEntry));
        if (WildcardMatch(file_part, ".")) {
            search->entries[k].name = ".";
            search->entries[k++].directory = 1;
        }
        if (WildcardMatch(file_part, "..")) {
            search->entries[k].name = "..";
            search->entries[k++].directory = 1;
        }
        for (candidate = d->names; *candidate; candidate += Length(candidate) + 1) {
            if (WildcardMatch(file_part, candidate)) {
                entry_path[0] = 0;
                Append(entry_path, host_directory);
                Append(entry_path, "/");
                Append(entry_path, candidate);
                search->entries[k].name = (char*)candidate;
                search->entries[k++].directory = Status(entry_path) == FILE_STATUS_DIRECTORY;
            }
        }
        search->count = k;
        search->paths = CacheAllocate(Length(host_directory) + 1);
        CopyBytes(search->paths, host_directory, Length(host_directory) + 1);
    }
    if (search->count == 0) {
        last_error = ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    entry_path[0] = 0;
    Append(entry_path, search->paths);
    Append(entry_path, "/");
    Append(entry_path, search->entries[0].name);
    FillFindData(entry_path, search->entries[0].name, search->entries[0].directory, lpFindFileData);
    search->next = 1;
    last_error = ERROR_SUCCESS;
    return NewSearch(search);
}

BOOL WINAPI FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData) {
    static char entry_path[PATH_SIZE];
    unsigned long i = (unsigned long)hFindFile;
    FindSearch* search;

    if (lpFindFileData == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    if (i == 0 || i >= FIND_COUNT || searches[i] == 0) {
        last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    search = searches[i];
    if (search->single || search->next >= search->count) {
        last_error = ERROR_NO_MORE_FILES_;
        return FALSE;
    }
    entry_path[0] = 0;
    Append(entry_path, search->paths);
    Append(entry_path, "/");
    Append(entry_path, search->entries[search->next].name);
    FillFindData(
        entry_path,
        search->entries[search->next].name,
        search->entries[search->next].directory,
        lpFindFileData
    );
    search->next++;
    return TRUE;
}

BOOL WINAPI FindClose(HANDLE hFindFile) {
    unsigned long i = (unsigned long)hFindFile;

    if (i == 0 || i >= FIND_COUNT || searches[i] == 0) {
        last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    searches[i] = 0;
    return TRUE;
}

HGLOBAL WINAPI GlobalAlloc(UINT uFlags, SIZE_T dwBytes) {
    void* p = Heap_Alloc(dwBytes, (uFlags & GMEM_ZEROINIT) != 0);
    if (p == 0) {
        last_error = ERROR_NOT_ENOUGH_MEMORY;
    }
    return p;
}

HGLOBAL WINAPI GlobalReAlloc(HGLOBAL hMem, SIZE_T dwBytes, UINT uFlags) {
    void* p = Heap_Realloc(hMem, dwBytes, (uFlags & GMEM_ZEROINIT) != 0);
    if (p == 0) {
        last_error = ERROR_NOT_ENOUGH_MEMORY;
    }
    return p;
}

HGLOBAL WINAPI GlobalFree(HGLOBAL hMem) {
    return Heap_Free(hMem) ? 0 : hMem;
}

UINT WINAPI GlobalFlags(HGLOBAL hMem) {
    (void)hMem;
    return 0;
}

LPVOID WINAPI
VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect) {
    void* p;

    (void)flAllocationType;
    (void)flProtect;
    if (lpAddress != 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return 0;
    }
    p = Heap_MapAligned(dwSize, 0x10000);
    if (p == 0) {
        last_error = ERROR_NOT_ENOUGH_MEMORY;
    }
    return p;
}

static void EnvironmentEntry(const char* entry, char* out, unsigned int* size) {
    static char converted[PATH_SIZE * 4];
    static char host[PATH_SIZE];
    static char absolute[PATH_SIZE];
    static char windows[PATH_SIZE];
    const char* value = entry;
    unsigned int name_length;
    unsigned int n;

    while (*value && *value != '=') {
        value++;
    }
    name_length = value - entry;
    converted[0] = 0;
    if (*value == '=') {
        value++;
        if (name_length == 4 && SameIgnoringCasePrefix(entry, "PATH", 4)) {
            const char* p = value;
            char separator = 0;
            const char* c;
            for (c = value; *c; c++) {
                if (*c == ';') {
                    separator = ';';
                }
            }
            if (!separator) {
                separator = ':';
            }
            while (1) {
                const char* start = p;
                while (*p && *p != separator) {
                    p++;
                }
                host[0] = 0;
                AppendN(host, start, p - start);
                if (host[0]) {
                    int looks_windows = 0;
                    for (c = host; *c; c++) {
                        if (*c == '\\') {
                            looks_windows = 1;
                        }
                    }
                    if (Length(host) >= 2 && host[1] == ':' && host[0] != '/') {
                        looks_windows = 1;
                    }
                    if (looks_windows) {
                        char* w;
                        for (w = host; *w; w++) {
                            if (*w == '/') {
                                *w = '\\';
                            }
                        }
                        AppendLong(converted, host, sizeof(converted));
                    } else {
                        WindowsPath(host, windows);
                        AppendLong(converted, windows, sizeof(converted));
                    }
                }
                if (*p == 0) {
                    break;
                }
                AppendLong(converted, ";", sizeof(converted));
                p++;
            }
            if (converted[0] == 0) {
                AppendLong(converted, value, sizeof(converted));
            }
        } else if ((name_length == 3 && SameIgnoringCasePrefix(entry, "TMP", 3)) ||
                   (name_length == 4 && SameIgnoringCasePrefix(entry, "TEMP", 4))) {
            const char* c;
            int looks_windows = 0;
            for (c = value; *c; c++) {
                if (*c == '\\') {
                    looks_windows = 1;
                }
            }
            if (Length(value) >= 2 && value[1] == ':' && value[0] != '/') {
                looks_windows = 1;
            }
            if (looks_windows) {
                char* w;
                AppendLong(converted, value, sizeof(converted));
                for (w = converted; *w; w++) {
                    if (*w == '/') {
                        *w = '\\';
                    }
                }
            } else {
                WindowsPath(value, windows);
                AppendLong(converted, windows, sizeof(converted));
            }
        } else {
            AppendLong(converted, value, sizeof(converted));
        }
    }
    n = name_length + (entry[name_length] == '=' ? 1 + Length(converted) : 0);
    if (out) {
        CopyBytes(out + *size, entry, name_length);
        if (entry[name_length] == '=') {
            out[*size + name_length] = '=';
            CopyBytes(out + *size + name_length + 1, converted, Length(converted));
        }
        out[*size + n] = 0;
    }
    *size += n + 1;
}

static char* environment_strings;
static unsigned int environment_size;

static const char* Variable(const char* name) {
    char** e;
    unsigned int n = Length(name);

    for (e = Native_envp; *e; e++) {
        if (SameIgnoringCasePrefix(*e, name, n) && (*e)[n] == '=') {
            return *e + n + 1;
        }
    }
    return 0;
}

static void Environment(void) {
    static char tmp[PATH_SIZE];
    static char temp[PATH_SIZE];
    const char* directory = Variable("TMPDIR");
    char** e;
    unsigned int size = 0;
    int add_tmp = Variable("TMP") == 0;
    int add_temp = Variable("TEMP") == 0;

    if (directory == 0 || directory[0] == 0) {
        directory = "/tmp";
    }
    tmp[0] = 0;
    Append(tmp, "TMP=");
    Append(tmp, directory);
    temp[0] = 0;
    Append(temp, "TEMP=");
    Append(temp, directory);
    for (e = Native_envp; *e; e++) {
        EnvironmentEntry(*e, 0, &size);
    }
    if (add_tmp) {
        EnvironmentEntry(tmp, 0, &size);
    }
    if (add_temp) {
        EnvironmentEntry(temp, 0, &size);
    }
    environment_strings = Allocate(size + 1);
    environment_size = size + 1;
    size = 0;
    for (e = Native_envp; *e; e++) {
        EnvironmentEntry(*e, environment_strings, &size);
    }
    if (add_tmp) {
        EnvironmentEntry(tmp, environment_strings, &size);
    }
    if (add_temp) {
        EnvironmentEntry(temp, environment_strings, &size);
    }
    environment_strings[size] = 0;
}

LPSTR WINAPI GetEnvironmentStrings(void) {
    char* block;

    if (environment_strings == 0) {
        Environment();
    }
    block = Heap_Alloc(environment_size, 0);
    CopyBytes(block, environment_strings, environment_size);
    return block;
}

BOOL WINAPI FreeEnvironmentStringsA(LPSTR lpszEnvironmentBlock) {
    Heap_Free(lpszEnvironmentBlock);
    return TRUE;
}

static char* command_line;

LPSTR WINAPI GetCommandLineA(void) {
    static char windows[PATH_SIZE];
    char* line;
    unsigned int size;
    int pass;
    int i;

    if (command_line != 0) {
        return command_line;
    }
    ProgramPath();
    WindowsPath(program_alias, windows);
    line = 0;
    size = 0;
    for (pass = 0; pass < 2; pass++) {
        unsigned int out = 0;
        for (i = 0; i < Native_argc; i++) {
            const char* arg = i == 0 ? windows : Native_argv[i];
            const char* c;
            int quote = 0;
            int backslashes = 0;
            if (i != 0) {
                if (line) {
                    line[out] = ' ';
                }
                out++;
            }
            for (c = arg; *c; c++) {
                if (*c == '"' || *c == ' ' || *c == '\t' || *c == '\n') {
                    quote = 1;
                }
            }
            if (quote) {
                if (line) {
                    line[out] = '"';
                }
                out++;
            }
            for (c = arg;; c++) {
                int j;
                if (*c == '\\') {
                    backslashes++;
                    continue;
                }
                for (j = 0; j < backslashes; j++) {
                    if (line) {
                        line[out] = '\\';
                    }
                    out++;
                    if (*c == 0 || *c == '"') {
                        if (line) {
                            line[out] = '\\';
                        }
                        out++;
                    }
                }
                backslashes = 0;
                if (*c == 0) {
                    break;
                }
                if (*c == '"') {
                    if (line) {
                        line[out] = '\\';
                    }
                    out++;
                }
                if (line) {
                    line[out] = *c;
                }
                out++;
            }
            if (quote) {
                if (line) {
                    line[out] = '"';
                }
                out++;
            }
        }
        if (pass == 0) {
            size = out + 1;
            line = Allocate(size);
        } else {
            line[out] = 0;
        }
    }
    command_line = Heap_Alloc(size, 1);
    CopyBytes(command_line, line, size);
    return command_line;
}

void WINAPI ExitProcess(UINT uExitCode) {
    Linux_Exit(uExitCode);
}

BOOL WINAPI SetConsoleCtrlHandler(PHANDLER_ROUTINE HandlerRoutine, BOOL Add) {
    (void)HandlerRoutine;
    (void)Add;
    return TRUE;
}

BOOL WINAPI GetConsoleScreenBufferInfo(
    HANDLE hConsoleOutput, PCONSOLE_SCREEN_BUFFER_INFO lpConsoleScreenBufferInfo
) {
    (void)hConsoleOutput;
    if (lpConsoleScreenBufferInfo == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    Fill(lpConsoleScreenBufferInfo, 0, sizeof(*lpConsoleScreenBufferInfo));
    lpConsoleScreenBufferInfo->dwSize.X = 80;
    lpConsoleScreenBufferInfo->dwSize.Y = 25;
    lpConsoleScreenBufferInfo->srWindow.Right = 79;
    lpConsoleScreenBufferInfo->srWindow.Bottom = 24;
    lpConsoleScreenBufferInfo->dwMaximumWindowSize.X = 80;
    lpConsoleScreenBufferInfo->dwMaximumWindowSize.Y = 25;
    return TRUE;
}

LANGID WINAPI GetSystemDefaultLangID(void) {
    return 0;
}

BOOL WINAPI IsDBCSLeadByte(BYTE TestChar) {
    (void)TestChar;
    return FALSE;
}

#define TLS_COUNT 64
static LPVOID tls_values[TLS_COUNT];
static unsigned char tls_used[TLS_COUNT];

DWORD WINAPI TlsAlloc(void) {
    DWORD i;
    for (i = 0; i < TLS_COUNT; i++) {
        if (!tls_used[i]) {
            tls_used[i] = 1;
            tls_values[i] = 0;
            return i;
        }
    }
    return TLS_OUT_OF_INDEXES;
}

BOOL WINAPI TlsFree(DWORD dwTlsIndex) {
    if (dwTlsIndex >= TLS_COUNT || !tls_used[dwTlsIndex]) {
        last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    tls_used[dwTlsIndex] = 0;
    return TRUE;
}

LPVOID WINAPI TlsGetValue(DWORD dwTlsIndex) {
    if (dwTlsIndex >= TLS_COUNT) {
        last_error = ERROR_INVALID_PARAMETER;
        return 0;
    }
    last_error = ERROR_SUCCESS;
    return tls_values[dwTlsIndex];
}

BOOL WINAPI TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue) {
    if (dwTlsIndex >= TLS_COUNT) {
        last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    tls_values[dwTlsIndex] = lpTlsValue;
    return TRUE;
}

void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
    Fill(lpCriticalSection, 0, sizeof(*lpCriticalSection));
}

void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
    (void)lpCriticalSection;
}

void WINAPI EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
    (void)lpCriticalSection;
}

void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
    (void)lpCriticalSection;
}

BOOL WINAPI CreateProcessA(
    LPCSTR lpApplicationName,
    LPSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles,
    DWORD dwCreationFlags,
    LPVOID lpEnvironment,
    LPCSTR lpCurrentDirectory,
    LPSTARTUPINFOA lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation
) {
    (void)lpApplicationName;
    (void)lpCommandLine;
    (void)lpProcessAttributes;
    (void)lpThreadAttributes;
    (void)bInheritHandles;
    (void)dwCreationFlags;
    (void)lpEnvironment;
    (void)lpCurrentDirectory;
    (void)lpStartupInfo;
    (void)lpProcessInformation;
    last_error = ERROR_NOT_SUPPORTED_;
    return FALSE;
}

DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
    (void)hHandle;
    (void)dwMilliseconds;
    last_error = ERROR_INVALID_HANDLE;
    return WAIT_FAILED;
}

BOOL WINAPI GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode) {
    (void)hProcess;
    (void)lpExitCode;
    last_error = ERROR_INVALID_HANDLE;
    return FALSE;
}

DWORD WINAPI GetTickCount(void) {
    LinuxTimespec64 ts;
    Linux_ClockGetTime(1, &ts);
    return (DWORD)(ts.sec * 1000 + ts.nsec / 1000000);
}

static int IsLeap(long long year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static void ToSystemTime(long long seconds, LPSYSTEMTIME st) {
    static const int days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    long long days = seconds / 86400;
    long long rest = seconds % 86400;
    long long year = 1970;
    int month = 0;

    if (rest < 0) {
        rest += 86400;
        days--;
    }
    st->wDayOfWeek = (WORD)((days % 7 + 11) % 7);
    while (days < 0) {
        year--;
        days += IsLeap(year) ? 366 : 365;
    }
    while (days >= (IsLeap(year) ? 366 : 365)) {
        days -= IsLeap(year) ? 366 : 365;
        year++;
    }
    while (days >= days_in_month[month] + (month == 1 && IsLeap(year))) {
        days -= days_in_month[month] + (month == 1 && IsLeap(year));
        month++;
    }
    st->wYear = (WORD)year;
    st->wMonth = (WORD)(month + 1);
    st->wDay = (WORD)(days + 1);
    st->wHour = (WORD)(rest / 3600);
    st->wMinute = (WORD)(rest / 60 % 60);
    st->wSecond = (WORD)(rest % 60);
    st->wMilliseconds = 0;
}

static long long Now(void) {
    LinuxTimespec64 ts;
    Linux_ClockGetTime(0, &ts);
    return ts.sec;
}

void WINAPI GetSystemTime(LPSYSTEMTIME lpSystemTime) {
    if (lpSystemTime) {
        ToSystemTime(Now(), lpSystemTime);
    }
}

void WINAPI GetLocalTime(LPSYSTEMTIME lpSystemTime) {
    GetSystemTime(lpSystemTime);
}

DWORD WINAPI GetTimeZoneInformation(LPTIME_ZONE_INFORMATION lpTimeZoneInformation) {
    if (lpTimeZoneInformation == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return TIME_ZONE_ID_INVALID;
    }
    Fill(lpTimeZoneInformation, 0, sizeof(*lpTimeZoneInformation));
    return TIME_ZONE_ID_STANDARD;
}

BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME* lpSystemTime, LPFILETIME lpFileTime) {
    static const int days_before[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    long long year;
    long long days;

    if (lpSystemTime == 0 ||
        lpFileTime == 0 ||
        lpSystemTime->wMonth < 1 ||
        lpSystemTime->wMonth > 12 ||
        lpSystemTime->wYear < 1601) {
        last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    year = lpSystemTime->wYear;
    days = (year - 1601) * 365 + (year - 1601) / 4 - (year - 1601) / 100 + (year - 1601) / 400;
    days += days_before[lpSystemTime->wMonth - 1] + (lpSystemTime->wMonth > 2 && IsLeap(year));
    days += lpSystemTime->wDay - 1;
    {
        unsigned long long t = ((unsigned long long)days * 86400 + lpSystemTime->wHour * 3600 +
                                lpSystemTime->wMinute * 60 + lpSystemTime->wSecond) *
                10000000ULL +
            lpSystemTime->wMilliseconds * 10000ULL;
        lpFileTime->dwLowDateTime = (DWORD)t;
        lpFileTime->dwHighDateTime = (DWORD)(t >> 32);
    }
    return TRUE;
}

BOOL WINAPI FileTimeToSystemTime(const FILETIME* lpFileTime, LPSYSTEMTIME lpSystemTime) {
    unsigned long long t;

    if (lpFileTime == 0 || lpSystemTime == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    t = (unsigned long long)lpFileTime->dwHighDateTime << 32 | lpFileTime->dwLowDateTime;
    ToSystemTime((long long)(t / 10000000ULL) - 11644473600LL, lpSystemTime);
    lpSystemTime->wMilliseconds = (WORD)(t / 10000 % 1000);
    return TRUE;
}

static const char* const messages[] = {
    "No error information",
    "Operation not permitted",
    "No such file or directory",
    "No such process",
    "Interrupted system call",
    "I/O error",
    "No such device or address",
    "Argument list too long",
    "Exec format error",
    "Bad file descriptor",
    "No child process",
    "Resource temporarily unavailable",
    "Out of memory",
    "Permission denied",
    "Bad address",
    "Block device required",
    "Resource busy",
    "File exists",
    "Cross-device link",
    "No such device",
    "Not a directory",
    "Is a directory",
    "Invalid argument",
    "Too many open files in system",
    "No file descriptors available",
    "Not a tty",
    "Text file busy",
    "File too large",
    "No space left on device",
    "Invalid seek",
    "Read-only file system",
    "Too many links",
    "Broken pipe",
    "Domain error",
    "Result not representable",
    "Resource deadlock would occur",
    "Filename too long",
    "No locks available",
    "Function not implemented",
    "Directory not empty",
};

DWORD WINAPI FormatMessageA(
    DWORD dwFlags,
    LPCVOID lpSource,
    DWORD dwMessageId,
    DWORD dwLanguageId,
    LPSTR lpBuffer,
    DWORD nSize,
    va_list* Arguments
) {
    const char* message;
    unsigned int length;

    (void)lpSource;
    (void)dwLanguageId;
    (void)Arguments;
    if (!(dwFlags & 0x100) &&
        !(dwFlags & 0x2000) &&
        !(dwFlags & 0x800) &&
        !(dwFlags & 0x400) &&
        (dwFlags & FORMAT_MESSAGE_FROM_SYSTEM)) {
        message = dwMessageId < sizeof(messages) / sizeof(messages[0]) ? messages[dwMessageId]
                                                                       : messages[0];
        length = Length(message);
        if (lpBuffer == 0 || nSize == 0) {
            last_error = ERROR_INSUFFICIENT_BUFFER_;
            return 0;
        }
        if (nSize <= length) {
            CopyBytes(lpBuffer, message, nSize);
            lpBuffer[nSize - 1] = 0;
            last_error = ERROR_INSUFFICIENT_BUFFER_;
            return 0;
        }
        CopyBytes(lpBuffer, message, length + 1);
        return length;
    }
    if (lpBuffer && nSize > 0) {
        lpBuffer[0] = 0;
    }
    last_error = ERROR_CALL_NOT_IMPLEMENTED;
    return 0;
}

#define MODULE ((HMODULE)0x00400000)

HMODULE WINAPI GetModuleHandleA(LPCSTR lpModuleName) {
    static char windows[PATH_SIZE];

    if (lpModuleName == 0) {
        return MODULE;
    }
    ProgramPath();
    WindowsPath(program_alias, windows);
    if (SameIgnoringCase(windows, lpModuleName)) {
        return MODULE;
    }
    last_error = ERROR_MOD_NOT_FOUND_;
    return 0;
}

HRSRC WINAPI FindResourceA(HMODULE hModule, LPCSTR lpName, LPCSTR lpType) {
    const void* data;
    unsigned int size;

    if (hModule != MODULE) {
        last_error = ERROR_RESOURCE_DATA_NOT_FOUND_;
        return 0;
    }
    if (!Native_FindResource(lpType, lpName, &data, &size)) {
        last_error = ERROR_RESOURCE_TYPE_NOT_FOUND_;
        return 0;
    }
    return (HRSRC)data;
}

HGLOBAL WINAPI LoadResource(HMODULE hModule, HRSRC hResInfo) {
    (void)hModule;
    if (hResInfo == 0) {
        last_error = ERROR_RESOURCE_DATA_NOT_FOUND_;
    }
    return (HGLOBAL)hResInfo;
}

LPVOID WINAPI LockResource(HGLOBAL hResData) {
    return hResData;
}

DWORD WINAPI SizeofResource(HMODULE hModule, HRSRC hResInfo) {
    (void)hModule;
    return Native_ResourceSize(hResInfo);
}

static unsigned int ReadU16(const unsigned char* p) {
    return p[0] | p[1] << 8;
}

typedef struct VersionBlock {
    unsigned int total;
    unsigned int value_length;
    unsigned int type;
    const unsigned char* key;
    unsigned int key_length;
    unsigned char* value;
    unsigned int value_bytes;
    unsigned char* children;
    unsigned int children_bytes;
} VersionBlock;

static int ParseVersionBlock(unsigned char* block, unsigned int available, VersionBlock* out) {
    unsigned char* end;
    unsigned char* cursor;
    unsigned char* children;

    if (available < 6) {
        return 0;
    }
    out->total = ReadU16(block);
    out->value_length = ReadU16(block + 2);
    out->type = ReadU16(block + 4);
    if (out->total == 0 || out->total > available) {
        return 0;
    }
    end = block + out->total;
    cursor = block + 6;
    out->key = cursor;
    out->key_length = 0;
    while (cursor + 2 <= end) {
        unsigned int c = ReadU16(cursor);
        cursor += 2;
        if (c == 0) {
            break;
        }
        out->key_length++;
    }
    cursor = block + 6 + (out->key_length + 1) * 2;
    if (cursor > end) {
        return 0;
    }
    cursor = block + ((cursor - block + 3) & ~3);
    out->value_bytes = 0;
    if (out->value_length) {
        out->value_bytes = out->type == 1 ? out->value_length * 2 : out->value_length;
        if (cursor + out->value_bytes > end) {
            return 0;
        }
    }
    children = block + ((cursor + out->value_bytes - block + 3) & ~3);
    if (children > end) {
        children = end;
    }
    out->value = out->value_length ? cursor : 0;
    out->children = children;
    out->children_bytes = end - children;
    return 1;
}

static int QueryVersionBlock(
    unsigned char* block,
    unsigned int available,
    const char* path,
    unsigned char** value,
    unsigned int* length,
    unsigned int* type
) {
    VersionBlock view;
    VersionBlock child;
    unsigned char* cursor;
    unsigned char* end;
    unsigned int segment = 0;
    unsigned int i;

    if (!ParseVersionBlock(block, available, &view)) {
        return 0;
    }
    while (*path == '\\') {
        path++;
    }
    if (*path == 0) {
        *value = view.value_bytes ? view.value : 0;
        *length = view.type == 1 ? view.value_length : view.value_bytes;
        *type = view.type;
        return 1;
    }
    while (path[segment] && path[segment] != '\\') {
        segment++;
    }
    cursor = view.children;
    end = view.children + view.children_bytes;
    while (cursor + 6 <= end) {
        if (!ParseVersionBlock(cursor, end - cursor, &child) || child.total == 0) {
            break;
        }
        if (child.key_length == segment) {
            for (i = 0; i < segment; i++) {
                if (Lower(ReadU16(child.key + i * 2) & 0xFF) != Lower((unsigned char)path[i])) {
                    break;
                }
            }
            if (i == segment &&
                QueryVersionBlock(cursor, child.total, path + segment, value, length, type)) {
                return 1;
            }
        }
        if (((child.total + 3) & ~3u) == 0 || cursor + ((child.total + 3) & ~3u) > end) {
            break;
        }
        cursor += (child.total + 3) & ~3u;
    }
    return 0;
}

static int VersionResource(const void** data, unsigned int* size) {
    return Native_FindResource((LPCSTR)16, (LPCSTR)1, data, size);
}

DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle) {
    const void* data;
    unsigned int size;

    (void)lptstrFilename;
    if (lpdwHandle) {
        *lpdwHandle = 0;
    }
    if (!VersionResource(&data, &size)) {
        return 0;
    }
    return size;
}

BOOL WINAPI GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    const void* data;
    unsigned int size;

    (void)lptstrFilename;
    (void)dwHandle;
    if (lpData == 0 || dwLen == 0) {
        last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    if (!VersionResource(&data, &size)) {
        return FALSE;
    }
    if (size > dwLen) {
        last_error = ERROR_INSUFFICIENT_BUFFER_;
        return FALSE;
    }
    CopyBytes(lpData, data, size);
    Fill((char*)lpData + size, 0, dwLen - size);
    return TRUE;
}

BOOL WINAPI VerQueryValueA(const void* pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    unsigned char* value = 0;
    unsigned int length = 0;
    unsigned int type = 0;
    unsigned int i;

    if (lpSubBlock == 0 || pBlock == 0 || ReadU16(pBlock) < 6) {
        return FALSE;
    }
    if (!QueryVersionBlock(
            (unsigned char*)pBlock, ReadU16(pBlock), lpSubBlock, &value, &length, &type
        )) {
        return FALSE;
    }
    if (type == 1 && value) {
        for (i = 0; i < length; i++) {
            value[i] = value[i * 2];
        }
        value[length] = 0;
        if (lplpBuffer) {
            *lplpBuffer = value;
        }
        if (puLen) {
            *puLen = length;
        }
        return TRUE;
    }
    if (lplpBuffer) {
        *lplpBuffer = value;
    }
    if (puLen) {
        *puLen = length;
    }
    return TRUE;
}

void Native_InitializeWin32(void) {
    unsigned long ignore[5] = {1, 0, 0, 0, 0};
    Linux_Call4(LINUX_SYS_rt_sigaction, 13, (long)ignore, 0, 8);
    std_handles[0] = NewHandle(HANDLE_FILE, 0);
    std_handles[1] = NewHandle(HANDLE_FILE, 1);
    std_handles[2] = NewHandle(HANDLE_FILE, 2);
    handles[0].close_fd = handles[1].close_fd = handles[2].close_fd = 0;
    handles[0].append_only = handles[1].append_only = handles[2].append_only = 1;
    last_error = 0;
}
