#include "driver/CLErrors.h"
#include "compiler/win32.h"
#include "driver/CLIO.h"
#include "driver/CWPlugins.h"
#include "driver/MsDos.h"
#include "driver/StringUtils.h"
#include "driver/TextUtils.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* CLGetErrorString(SInt16 errid, char* buffer) {
    getindstring(buffer, 12000, errid);
    strcat(buffer, "\n");
    return buffer;
}

static void CLMessageReporter(int flags, SInt16 errid, va_list va) {
    static char stmsg[256];
    static char stbuf[256];
    char* ptr;

    CLGetErrorString(errid, stmsg);
    ptr = mvprintf(stbuf, sizeof(stbuf), stmsg, va);

    CLStyledMessageDispatch(
        NULL,
        NULL,
        0,
        (flags == messagetypeError)         ? Msg_Error
            : (flags == messagetypeWarning) ? Msg_Warning
                                            : Msg_Status,
        "%",
        ptr
    );

    if (ptr != stbuf) {
        free(ptr);
    }
}

void CLReportError(VarArgShort errid, ...) {
    va_list va;
    va_start(va, errid);
    CLMessageReporter(messagetypeError, errid, va);
    va_end(va);
}

void CLReportWarning(VarArgShort errid, ...) {
    va_list va;
    va_start(va, errid);
    CLMessageReporter(messagetypeWarning, errid, va);
    va_end(va);
}

void CLReport(VarArgShort errid, ...) {
    va_list va;
    va_start(va, errid);
    CLMessageReporter(messagetypeInfo, errid, va);
    va_end(va);
}

void CLReportOSError(SInt16 errid, int err, ...) {
    char* txt;
    const char* oserr;
    char mybuf[256];
    char myerr[256];
    va_list va;

    va_start(va, err);
    txt = mvprintf(mybuf, sizeof(mybuf), CLGetErrorString(errid, myerr), va);
    va_end(va);

    oserr = OS_GetErrText(err);
    CLReportError(CLStr99, txt, oserr, err);

    if (txt != mybuf) {
        free(txt);
    }
}

void CLReportCError(SInt16 errid, int err_no, ...) {
    char* txt;
    const char* serr;
    char mybuf[256];
    char myerr[256];
    va_list va;

    va_start(va, err_no);
    txt = mvprintf(mybuf, sizeof(mybuf), CLGetErrorString(errid, myerr), va);
    va_end(va);

    serr = strerror(err_no);
    CLReportError(CLStr100, txt, serr, err_no);

    if (txt != mybuf) {
        free(txt);
    }
}

void CLInternalError(const char* file, int line, const char* format, ...) {
    char mybuf[256];
    char* txt;
    va_list va;

    va_start(va, format);
    txt = mvprintf(mybuf, sizeof(mybuf), format, va);
    va_end(va);
    CLPrintErr("INTERNAL ERROR [%s:%d]:\n%s\n", file, line, txt);

    if (txt != mybuf) {
        free(txt);
    }
}

void CLFatalError(const char* format, ...) {
    char mybuf[256];
    char* txt;
    va_list va;

    va_start(va, format);
    txt = mvprintf(mybuf, sizeof(mybuf), format, va);
    va_end(va);
    CLPrintErr("FATAL ERROR:\n%s\n", txt);

    if (txt != mybuf) {
        free(txt);
    }

    exit(-123);
}
