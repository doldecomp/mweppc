#ifndef HOST_SHIFTJIS_H
#define HOST_SHIFTJIS_H

#include "compiler/win32.h"

#ifdef __cplusplus
extern "C" {
#endif

HANDLE __stdcall ShiftJIS_CreateFileA(
    LPCSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile
);
BOOL __stdcall ShiftJIS_ReadFile(
    HANDLE hFile,
    LPVOID lpBuffer,
    DWORD nNumberOfBytesToRead,
    LPDWORD lpNumberOfBytesRead,
    LPOVERLAPPED lpOverlapped
);
DWORD __stdcall ShiftJIS_GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
DWORD __stdcall ShiftJIS_SetFilePointer(
    HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod
);
BOOL __stdcall ShiftJIS_CloseHandle(HANDLE hObject);
BOOL __stdcall ShiftJIS_IsDBCSLeadByte(BYTE TestChar);

#ifdef __cplusplus
}
#endif

#endif
