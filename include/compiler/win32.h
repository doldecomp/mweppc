#ifndef COMPILER_WIN32_H
#define COMPILER_WIN32_H

#ifdef __cplusplus
extern "C" {
#endif

#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_TOO_MANY_OPEN_FILES 4L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_INVALID_ACCESS 12L
#define ERROR_INVALID_DATA 13L
#define ERROR_OUTOFMEMORY 14L
#define ERROR_INVALID_DRIVE 15L
#define ERROR_CURRENT_DIRECTORY 16L
#define ERROR_WRITE_PROTECT 19L
#define ERROR_NOT_READY 21L
#define ERROR_CRC 23L
#define ERROR_BAD_LENGTH 24L
#define ERROR_SEEK 25L
#define ERROR_SECTOR_NOT_FOUND 27L
#define ERROR_OUT_OF_PAPER 28L
#define ERROR_WRITE_FAULT 29L
#define ERROR_READ_FAULT 30L
#define ERROR_GEN_FAILURE 31L
#define ERROR_SHARING_VIOLATION 32L
#define ERROR_LOCK_VIOLATION 33L
#define ERROR_SHARING_BUFFER_EXCEEDED 36L
#define ERROR_HANDLE_EOF 38L
#define ERROR_HANDLE_DISK_FULL 39L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_BUFFER_OVERFLOW 111L
#define ERROR_INVALID_NAME 123L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_DIRECTORY 267L

#define GENERIC_READ (0x80000000L)
#define GENERIC_WRITE (0x40000000L)
#define FILE_SHARE_READ 0x00000001
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INFINITE 0xFFFFFFFF
#define STILL_ACTIVE ((DWORD)0x00000103L)
#define GMEM_FIXED 0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_INVALID_HANDLE 0x8000

#define INVALID_HANDLE_VALUE ((HANDLE)(-1))
#define FILE_ATTRIBUTE_HIDDEN 0x00000002
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define STD_OUTPUT_HANDLE ((DWORD)(-11))
#define STD_ERROR_HANDLE ((DWORD)(-12))
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef short SHORT;
typedef long LONG;
typedef char CHAR;
typedef unsigned long ULONG_PTR;
typedef ULONG_PTR SIZE_T;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef BYTE* LPBYTE;
typedef DWORD* LPDWORD;
typedef LONG* PLONG;
typedef CHAR* LPSTR;
typedef const CHAR* LPCSTR;
typedef void* HANDLE;
typedef HANDLE HGLOBAL;
struct HINSTANCE__ {
    int unused;
};
typedef struct HINSTANCE__* HINSTANCE;
typedef HINSTANCE HMODULE;
struct HRSRC__ {
    int unused;
};
typedef struct HRSRC__* HRSRC;
#ifndef __va_list__
#define __va_list__
typedef char* va_list;
#endif

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

typedef struct _OVERLAPPED {
    DWORD Internal;
    DWORD InternalHigh;
    DWORD Offset;
    DWORD OffsetHigh;
    HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct _PROCESS_INFORMATION {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD dwProcessId;
    DWORD dwThreadId;
} PROCESS_INFORMATION, *PPROCESS_INFORMATION, *LPPROCESS_INFORMATION;

typedef struct _STARTUPINFOA {
    DWORD cb;
    LPSTR lpReserved;
    LPSTR lpDesktop;
    LPSTR lpTitle;
    DWORD dwX;
    DWORD dwY;
    DWORD dwXSize;
    DWORD dwYSize;
    DWORD dwXCountChars;
    DWORD dwYCountChars;
    DWORD dwFillAttribute;
    DWORD dwFlags;
    WORD wShowWindow;
    WORD cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
} STARTUPINFOA, *LPSTARTUPINFOA;

typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    CHAR cFileName[260];
    CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, *PWIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;

typedef struct _COORD {
    SHORT X;
    SHORT Y;
} COORD, *PCOORD;

typedef struct _SMALL_RECT {
    SHORT Left;
    SHORT Top;
    SHORT Right;
    SHORT Bottom;
} SMALL_RECT, *PSMALL_RECT;

typedef struct _CONSOLE_SCREEN_BUFFER_INFO {
    COORD dwSize;
    COORD dwCursorPosition;
    WORD wAttributes;
    SMALL_RECT srWindow;
    COORD dwMaximumWindowSize;
} CONSOLE_SCREEN_BUFFER_INFO, *PCONSOLE_SCREEN_BUFFER_INFO;

typedef BOOL(__stdcall* PHANDLER_ROUTINE)(DWORD CtrlType);
#define CTRL_C_EVENT 0
#define CTRL_BREAK_EVENT 1

struct _TIME_ZONE_INFORMATION;
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE hObject);
__declspec(dllimport) BOOL __stdcall CreateDirectoryA(
    LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes
);
__declspec(dllimport) HANDLE __stdcall CreateFileA(
    LPCSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile
);
__declspec(dllimport) BOOL __stdcall CreateProcessA(
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
);
__declspec(dllimport) BOOL __stdcall DeleteFileA(LPCSTR lpFileName);
__declspec(dllimport) BOOL __stdcall FileTimeToSystemTime(
    const FILETIME* lpFileTime, LPSYSTEMTIME lpSystemTime
);
__declspec(dllimport) BOOL __stdcall FindClose(HANDLE hFindFile);
__declspec(dllimport) HANDLE __stdcall FindFirstFileA(
    LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData
);
__declspec(dllimport) BOOL __stdcall FindNextFileA(
    HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData
);
typedef struct tagVS_FIXEDFILEINFO {
    DWORD dwSignature;
    DWORD dwStrucVersion;
    DWORD dwFileVersionMS;
    DWORD dwFileVersionLS;
    DWORD dwProductVersionMS;
    DWORD dwProductVersionLS;
    DWORD dwFileFlagsMask;
    DWORD dwFileFlags;
    DWORD dwFileOS;
    DWORD dwFileType;
    DWORD dwFileSubtype;
    DWORD dwFileDateMS;
    DWORD dwFileDateLS;
} VS_FIXEDFILEINFO;
DWORD __stdcall GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle);
BOOL __stdcall GetFileVersionInfoA(
    LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData
);
BOOL __stdcall VerQueryValueA(
    const void* pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, UINT* puLen
);
__declspec(dllimport) HRSRC __stdcall FindResourceA(HMODULE hModule, LPCSTR lpName, LPCSTR lpType);
__declspec(dllimport) DWORD __stdcall FormatMessageA(
    DWORD dwFlags,
    LPCVOID lpSource,
    DWORD dwMessageId,
    DWORD dwLanguageId,
    LPSTR lpBuffer,
    DWORD nSize,
    va_list* Arguments
);
__declspec(dllimport) LPSTR __stdcall GetCommandLineA(void);
__declspec(dllimport) BOOL __stdcall GetConsoleScreenBufferInfo(
    HANDLE hConsoleOutput, PCONSOLE_SCREEN_BUFFER_INFO lpConsoleScreenBufferInfo
);
__declspec(dllimport) DWORD __stdcall GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer);
__declspec(dllimport) BOOL __stdcall GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode);
__declspec(dllimport) DWORD __stdcall GetFileAttributesA(LPCSTR lpFileName);
__declspec(dllimport) DWORD __stdcall GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
__declspec(dllimport) BOOL __stdcall GetFileTime(
    HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime
);
__declspec(dllimport) DWORD __stdcall GetFullPathNameA(
    LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR* lpFilePart
);
__declspec(dllimport) DWORD __stdcall GetLastError(void);
__declspec(dllimport) HMODULE __stdcall GetModuleHandleA(LPCSTR lpModuleName);
__declspec(dllimport) HANDLE __stdcall GetStdHandle(DWORD nStdHandle);
__declspec(dllimport) UINT __stdcall GetSystemDirectoryA(LPSTR lpBuffer, UINT uSize);
__declspec(dllimport) void __stdcall GetSystemTime(LPSYSTEMTIME lpSystemTime);
__declspec(dllimport) DWORD __stdcall GetTickCount(void);
__declspec(dllimport) DWORD __stdcall GetTimeZoneInformation(
    struct _TIME_ZONE_INFORMATION* lpTimeZoneInformation
);
__declspec(dllimport) UINT __stdcall GetWindowsDirectoryA(LPSTR lpBuffer, UINT uSize);
__declspec(dllimport) HGLOBAL __stdcall GlobalAlloc(UINT uFlags, SIZE_T dwBytes);
__declspec(dllimport) UINT __stdcall GlobalFlags(HGLOBAL hMem);
__declspec(dllimport) HGLOBAL __stdcall GlobalFree(HGLOBAL hMem);
__declspec(dllimport) HGLOBAL __stdcall GlobalReAlloc(HGLOBAL hMem, SIZE_T dwBytes, UINT uFlags);
__declspec(dllimport) BOOL __stdcall IsDBCSLeadByte(BYTE TestChar);
__declspec(dllimport) DWORD __stdcall GetShortPathNameA(
    LPCSTR lpszLongPath, LPSTR lpszShortPath, DWORD cchBuffer
);
__declspec(dllimport) HGLOBAL __stdcall LoadResource(HMODULE hModule, HRSRC hResInfo);
__declspec(dllimport) LPVOID __stdcall LockResource(HGLOBAL hResData);
__declspec(dllimport) BOOL __stdcall ReadFile(
    HANDLE hFile,
    LPVOID lpBuffer,
    DWORD nNumberOfBytesToRead,
    LPDWORD lpNumberOfBytesRead,
    LPOVERLAPPED lpOverlapped
);
__declspec(dllimport) BOOL __stdcall RemoveDirectoryA(LPCSTR lpPathName);
__declspec(dllimport) BOOL __stdcall SetConsoleCtrlHandler(
    PHANDLER_ROUTINE HandlerRoutine, BOOL Add
);
__declspec(dllimport) BOOL __stdcall SetEndOfFile(HANDLE hFile);
__declspec(dllimport) BOOL __stdcall SetFileAttributesA(LPCSTR lpFileName, DWORD dwFileAttributes);
__declspec(dllimport) DWORD __stdcall SetFilePointer(
    HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod
);
__declspec(dllimport) BOOL __stdcall SetFileTime(
    HANDLE hFile,
    const FILETIME* lpCreationTime,
    const FILETIME* lpLastAccessTime,
    const FILETIME* lpLastWriteTime
);
__declspec(dllimport) BOOL __stdcall SetStdHandle(DWORD nStdHandle, HANDLE hHandle);
__declspec(dllimport) DWORD __stdcall SizeofResource(HMODULE hModule, HRSRC hResInfo);
__declspec(dllimport) BOOL __stdcall SystemTimeToFileTime(
    const SYSTEMTIME* lpSystemTime, LPFILETIME lpFileTime
);
__declspec(dllimport) DWORD __stdcall TlsAlloc(void);
__declspec(dllimport) BOOL __stdcall TlsFree(DWORD dwTlsIndex);
__declspec(dllimport) LPVOID __stdcall TlsGetValue(DWORD dwTlsIndex);
__declspec(dllimport) BOOL __stdcall TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue);
__declspec(dllimport) DWORD __stdcall WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
__declspec(dllimport) BOOL __stdcall WriteFile(
    HANDLE hFile,
    LPCVOID lpBuffer,
    DWORD nNumberOfBytesToWrite,
    LPDWORD lpNumberOfBytesWritten,
    LPOVERLAPPED lpOverlapped
);
#ifdef __cplusplus
}
#endif

#endif
