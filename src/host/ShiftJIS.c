#include "host/ShiftJIS.h"
#include "compiler/common.h"
#include "host/ShiftJISTable.h"
#include <stdlib.h>
#include <string.h>

__declspec(dllimport) LPVOID __stdcall VirtualAlloc(
    LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect
);
__declspec(dllimport) void __stdcall SetLastError(DWORD dwErrCode);
__declspec(dllimport) void __stdcall ExitProcess(UINT uExitCode);

#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define PAGE_READWRITE 0x04

typedef struct ConvertedFile {
    char* name;
    unsigned char* data;
    UInt32 size;
} ConvertedFile;

typedef struct ConvertedHandle {
    HANDLE handle;
    int file;
    UInt32 position;
} ConvertedHandle;

static int enabled = -1;
static ConvertedFile* files;
static int file_count;
static int file_capacity;
static ConvertedHandle* handles;
static int handle_count;
static int handle_capacity;

static Boolean Enabled(void) {
    if (enabled < 0) {
        const char* value = getenv("MWCC_SJIS");
        enabled = value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
    }
    return enabled;
}

static void* Map(UInt32 size) {
    void* p = VirtualAlloc(NULL, size ? size : 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (p == NULL) {
        ExitProcess(1);
    }
    return p;
}

static void* Allocate(UInt32 size) {
    static unsigned char* pool;
    static UInt32 pool_left;
    void* p;

    size = (size + 7) & ~7u;
    if (size >= 0x10000) {
        return Map(size);
    }
    if (size > pool_left) {
        pool = Map(0x100000);
        pool_left = 0x100000;
    }
    p = pool;
    pool += size;
    pool_left -= size;
    return p;
}

static void* Grow(void* array, int count, int* capacity, UInt32 size) {
    void* grown;

    if (count < *capacity) {
        return array;
    }
    *capacity = *capacity ? *capacity * 2 : 64;
    grown = Allocate(*capacity * size);
    if (count) {
        memcpy(grown, array, count * size);
    }
    return grown;
}

static void Write(const char* text) {
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, strlen(text), &written, NULL);
}

static char* Digits(UInt32 value, UInt32 base, char* end) {
    *--end = '\0';
    do {
        *--end = "0123456789ABCDEF"[value % base];
        value /= base;
    } while (value != 0);
    return end;
}

static Boolean IsTextFile(const char* name) {
    static const char* const extensions[] = {
        "c",
        "cc",
        "cp",
        "cpp",
        "cxx",
        "c++",
        "h",
        "hh",
        "hp",
        "hpp",
        "hxx",
        "h++",
        "pch",
        "pch++",
        "inc"
    };
    const char* start;
    const char* end;
    const char* dot;
    UInt32 i;

    end = name + strlen(name);
    for (;;) {
        while (end > name && (end[-1] == '/' || end[-1] == '\\')) {
            end--;
        }
        start = end;
        while (start > name && start[-1] != '/' && start[-1] != '\\') {
            start--;
        }
        if (end - start == 1 && start[0] == '.' && start > name) {
            end = start;
            continue;
        }
        break;
    }
    if (start == name && end - start >= 2 && name[1] == ':') {
        start += 2;
    }
    if (end == start || (end - start == 2 && start[0] == '.' && start[1] == '.')) {
        return FALSE;
    }
    for (dot = end - 1; dot >= start && *dot != '.'; dot--) {}
    if (dot <= start) {
        return FALSE;
    }
    dot++;
    for (i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
        if (strlen(extensions[i]) == (UInt32)(end - dot) &&
            !memcmp(extensions[i], dot, end - dot)) {
            return TRUE;
        }
    }
    return FALSE;
}

static int CheckUTF8(const unsigned char* data, UInt32 size, UInt32* valid) {
    UInt32 i = 0;
    UInt32 start;
    unsigned char first;
    unsigned char next;

#define NEXT() \
    do { \
        if (++i >= size) { \
            *valid = start; \
            return -1; \
        } \
        next = data[i]; \
    } while (0)
#define CONTINUATION(length) \
    do { \
        NEXT(); \
        if ((signed char)next >= -64) { \
            *valid = start; \
            return length; \
        } \
    } while (0)

    while (i < size) {
        first = data[i];
        start = i;
        if (first < 0x80) {
            i++;
            continue;
        }
        if (first >= 0xC2 && first <= 0xDF) {
            CONTINUATION(1);
        } else if (first >= 0xE0 && first <= 0xEF) {
            NEXT();
            if (!((first == 0xE0 && next >= 0xA0 && next <= 0xBF) ||
                  (first >= 0xE1 && first <= 0xEC && next >= 0x80 && next <= 0xBF) ||
                  (first == 0xED && next >= 0x80 && next <= 0x9F) ||
                  (first >= 0xEE && next >= 0x80 && next <= 0xBF))) {
                *valid = start;
                return 1;
            }
            CONTINUATION(2);
        } else if (first >= 0xF0 && first <= 0xF4) {
            NEXT();
            if (!((first == 0xF0 && next >= 0x90 && next <= 0xBF) ||
                  (first >= 0xF1 && first <= 0xF3 && next >= 0x80 && next <= 0xBF) ||
                  (first == 0xF4 && next >= 0x80 && next <= 0x8F))) {
                *valid = start;
                return 1;
            }
            CONTINUATION(2);
            CONTINUATION(3);
        } else {
            *valid = start;
            return 1;
        }
        i++;
    }
#undef NEXT
#undef CONTINUATION
    *valid = size;
    return 0;
}

static UInt32 EncodeCodePoint(UInt32 c) {
    int low;
    int high;
    int middle;

    if (c <= 0x80) {
        return c;
    }
    if (c == 0xA5) {
        return 0x5C;
    }
    if (c == 0x203E) {
        return 0x7E;
    }
    if (c >= 0xFF61 && c <= 0xFF9F) {
        return c - 0xFF61 + 0xA1;
    }
    if (c == 0x2212) {
        c = 0xFF0D;
    }
    low = 0;
    high = sizeof(ShiftJIS_Table) / sizeof(ShiftJIS_Table[0]) - 1;
    while (low <= high) {
        middle = (low + high) / 2;
        if (ShiftJIS_Table[middle][0] == c) {
            return ShiftJIS_Table[middle][1];
        }
        if (ShiftJIS_Table[middle][0] < c) {
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    return 0;
}

static UInt32 Encode(const unsigned char* data, UInt32 size, unsigned char* out, Boolean* errors) {
    UInt32 length = 0;
    UInt32 i = 0;
    UInt32 c;
    UInt32 code;
    char digits[12];
    char* p;

    while (i < size) {
        c = data[i];
        if (c < 0x80) {
            i++;
        } else if (c < 0xE0) {
            c = ((c & 0x1F) << 6) | (data[i + 1] & 0x3F);
            i += 2;
        } else if (c < 0xF0) {
            c = ((c & 0x0F) << 12) | ((data[i + 1] & 0x3F) << 6) | (data[i + 2] & 0x3F);
            i += 3;
        } else {
            c = ((c & 0x07) << 18) | ((data[i + 1] & 0x3F) << 12) | ((data[i + 2] & 0x3F) << 6) |
                (data[i + 3] & 0x3F);
            i += 4;
        }
        code = EncodeCodePoint(c);
        if (code == 0 && c != 0) {
            *errors = TRUE;
            p = Digits(c, 10, digits + sizeof(digits));
            if (out) {
                out[length] = '&';
                out[length + 1] = '#';
                memcpy(out + length + 2, p, strlen(p));
                out[length + 2 + strlen(p)] = ';';
            }
            length += 3 + strlen(p);
        } else if (code > 0xFF) {
            if (out) {
                out[length] = code >> 8;
                out[length + 1] = code;
            }
            length += 2;
        } else {
            if (out) {
                out[length] = code;
            }
            length++;
        }
    }
    return length;
}

static ConvertedHandle* FindHandle(HANDLE handle) {
    int i;

    if (handle == INVALID_HANDLE_VALUE || handle == NULL) {
        return NULL;
    }
    for (i = 0; i < handle_count; i++) {
        if (handles[i].handle == handle) {
            return &handles[i];
        }
    }
    return NULL;
}

static ConvertedFile* FindFile(HANDLE handle) {
    ConvertedHandle* converted = FindHandle(handle);
    return converted != NULL && converted->file >= 0 ? &files[converted->file] : NULL;
}

static void ConvertFile(HANDLE handle, const char* name) {
    ConvertedHandle* converted;
    ConvertedFile* file;
    DWORD high;
    DWORD size;
    DWORD read;
    unsigned char* data;
    unsigned char* encoded;
    UInt32 length;
    UInt32 valid;
    UInt32 at;
    Boolean errors;
    int error;
    char number[12];
    int i;

    converted = FindHandle(handle);
    if (converted == NULL) {
        handles = Grow(handles, handle_count, &handle_capacity, sizeof(ConvertedHandle));
        converted = &handles[handle_count++];
    }
    converted->handle = handle;
    converted->file = -1;
    converted->position = 0;
    for (i = 0; i < file_count; i++) {
        if (!strcmp(files[i].name, name)) {
            converted->file = i;
            return;
        }
    }

    high = 0;
    size = GetFileSize(handle, &high);
    if (high != 0 || size == 0xFFFFFFFF) {
        return;
    }
    data = Allocate(size + 1);
    if (!ReadFile(handle, data, size, &read, NULL) || read != size) {
        Write("sjiswrap: Failed to read file ");
        Write(name);
        Write("\n");
        return;
    }
    data[size] = 0;
    error = CheckUTF8(data, size + 1, &valid);
    if (error != 0) {
        Write("sjiswrap: File ");
        Write(name);
        if (error > 0) {
            Write(" is not valid UTF-8: invalid utf-8 sequence of ");
            Write(Digits(error, 10, number + sizeof(number)));
            Write(" bytes from index ");
        } else {
            Write(" is not valid UTF-8: incomplete utf-8 byte sequence from index ");
        }
        Write(Digits(valid, 10, number + sizeof(number)));
        Write("\n");
        ExitProcess(1);
    }
    errors = FALSE;
    length = Encode(data, size + 1, NULL, &errors);
    if (errors) {
        Write("sjiswrap: File ");
        Write(name);
        Write(" contains Shift JIS encoding errors\n");
    }
    encoded = data;
    for (at = 0; at < size; at++) {
        if (data[at] >= 0x80) {
            encoded = Allocate(length);
            Encode(data, size + 1, encoded, &errors);
            break;
        }
    }

    files = Grow(files, file_count, &file_capacity, sizeof(ConvertedFile));
    file = &files[file_count++];
    file->name = Allocate(strlen(name) + 1);
    strcpy(file->name, name);
    file->data = encoded;
    file->size = length - 1;
    converted->file = file_count - 1;
}

HANDLE __stdcall ShiftJIS_CreateFileA(
    LPCSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile
) {
    HANDLE handle = CreateFileA(
        lpFileName,
        dwDesiredAccess,
        dwShareMode,
        lpSecurityAttributes,
        dwCreationDisposition,
        dwFlagsAndAttributes,
        hTemplateFile
    );

    if (!Enabled()) {
        return handle;
    }

    {
        DWORD error = GetLastError();
        if (handle != INVALID_HANDLE_VALUE &&
            handle != NULL &&
            dwDesiredAccess == GENERIC_READ &&
            IsTextFile(lpFileName)) {
            ConvertFile(handle, lpFileName);
        }
        SetLastError(error);
    }

    return handle;
}

BOOL __stdcall ShiftJIS_ReadFile(
    HANDLE hFile,
    LPVOID lpBuffer,
    DWORD nNumberOfBytesToRead,
    LPDWORD lpNumberOfBytesRead,
    LPOVERLAPPED lpOverlapped
) {
    ConvertedHandle* converted = FindHandle(hFile);
    ConvertedFile* file = FindFile(hFile);
    UInt32 count;

    if (file == NULL) {
        return ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
    }
    count = file->size - converted->position;
    if (count > nNumberOfBytesToRead) {
        count = nNumberOfBytesToRead;
    }
    memcpy(lpBuffer, file->data + converted->position, count);
    converted->position += count;
    if (lpNumberOfBytesRead != NULL) {
        *lpNumberOfBytesRead = count;
    }
    return TRUE;
}

DWORD __stdcall ShiftJIS_GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh) {
    ConvertedFile* file = FindFile(hFile);

    if (file == NULL) {
        return GetFileSize(hFile, lpFileSizeHigh);
    }
    return file->size;
}

DWORD __stdcall ShiftJIS_SetFilePointer(
    HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod
) {
    ConvertedHandle* converted = FindHandle(hFile);
    ConvertedFile* file = FindFile(hFile);
    long long distance;
    unsigned long long position;
    unsigned long long size;

    if (file == NULL) {
        return SetFilePointer(hFile, lDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod);
    }
    distance = (long long)lDistanceToMove |
        (long long)((unsigned long long)(lpDistanceToMoveHigh != NULL ? *lpDistanceToMoveHigh : 0)
                    << 32);
    size = file->size;
    switch (dwMoveMethod) {
        case FILE_BEGIN:
            position = distance;
            break;
        case FILE_CURRENT:
            position = converted->position;
            break;
        case FILE_END:
            position = size;
            break;
        default: {
            char number[12];
            Write("sjiswrap: SetFilePointer(): Unsupported move method 0x");
            Write(Digits(dwMoveMethod, 16, number + sizeof(number)));
            Write("\n");
            ExitProcess(1);
            return 0;
        }
    }
    if (dwMoveMethod != FILE_BEGIN) {
        if (distance < 0) {
            position = (unsigned long long)-distance > position ? 0 : position + distance;
        } else {
            position = position + distance < position ? ~0ULL : position + distance;
        }
    }
    if (position > size) {
        position = size;
    }
    converted->position = position;
    if (lpDistanceToMoveHigh != NULL) {
        *lpDistanceToMoveHigh = position >> 32;
    }
    return position;
}

BOOL __stdcall ShiftJIS_CloseHandle(HANDLE hObject) {
    ConvertedHandle* converted;

    if (Enabled() && (converted = FindHandle(hObject)) != NULL) {
        *converted = handles[--handle_count];
    }
    return CloseHandle(hObject);
}

BOOL __stdcall ShiftJIS_IsDBCSLeadByte(BYTE TestChar) {
    if (!Enabled()) {
        return IsDBCSLeadByte(TestChar);
    }
    return (TestChar & 0x80) != 0;
}
