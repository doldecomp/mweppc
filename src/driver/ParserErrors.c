#include "driver/ParserErrors.h"
#include "driver/CWPlugins.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/MsDos.h"
#include "driver/ParserFace.h"
#include "driver/TextUtils.h"
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>

static char errorbuf[1024];

void CLPReportError_V(const char* format, va_list ap) {
    vsprintf(errorbuf, format, ap);
    CWReportMessage(parseopts.context, NULL, errorbuf, NULL, messagetypeError, 0);
    parseopts.hadErrors = TRUE;
}

void CLPReportWarning_V(const char* format, va_list ap) {
    vsprintf(errorbuf, format, ap);
    CWReportMessage(parseopts.context, NULL, errorbuf, NULL, messagetypeWarning, 0);
}

void CLPReport_V(const char* format, va_list ap) {
    vsprintf(errorbuf, format, ap);
    CWReportMessage(parseopts.context, NULL, errorbuf, NULL, messagetypeInfo, 0);
}

void CLPStatus_V(const char* format, va_list ap) {
    vsprintf(errorbuf, format, ap);
    CWShowStatus(parseopts.context, errorbuf, NULL);
}

void CLPAlert_V(const char* format, va_list ap) {
    vsprintf(errorbuf, format, ap);
    CWAlert(parseopts.context, errorbuf, NULL, NULL, NULL);
    parseopts.hadErrors = TRUE;
}

void CLPOSAlert_V(const char* format, SInt32 err, va_list ap) {
    vsprintf(errorbuf, format, ap);
    CWAlert(parseopts.context, errorbuf, "Operating system error:", OS_GetErrText(err), NULL);
}

char* CLPGetErrorString(SInt16 errid, char* buffer) {
    getindstring(buffer, 12010, errid);
    return buffer;
}

void CLPReportError(VarArgShort errid, ...) {
    char format[256];
    va_list va;

    CLPGetErrorString(errid, format);
    va_start(va, errid);
    CLPReportError_V(format, va);
    va_end(va);
}

void CLPReportWarning(VarArgShort errid, ...) {
    char format[256];
    va_list va;

    CLPGetErrorString(errid, format);
    va_start(va, errid);
    CLPReportWarning_V(format, va);
    va_end(va);
}

void CLPReport(VarArgShort errid, ...) {
    char format[256];
    va_list va;

    CLPGetErrorString(errid, format);
    va_start(va, errid);
    CLPReport_V(format, va);
    va_end(va);
}

void CLPAlert(VarArgShort errid, ...) {
    char format[256];
    va_list va;

    CLPGetErrorString(errid, format);
    va_start(va, errid);
    CLPAlert_V(format, va);
    va_end(va);
}

void CLPOSAlert(SInt16 errid, VarArgShort err, ...) {
    char format[256];
    va_list va;

    CLPGetErrorString(errid, format);
    va_start(va, err);
    CLPOSAlert_V(format, (SInt16)err, va);
    va_end(va);
}

void CLPProgress(VarArgShort errid, ...) {
    char format[256];
    va_list va;

    CLPGetErrorString(errid, format);
    va_start(va, errid);
    CLPStatus_V(format, va);
    va_end(va);
}

void CLPStatus(VarArgShort errid, ...) {
    char format[256];
    va_list va;

    CLPGetErrorString(errid, format);
    va_start(va, errid);
    CLPStatus_V(format, va);
    va_end(va);
}

void CLPFatalError(const char* format, ...) {
    va_list va;

    va_start(va, format);
    CLPAlert_V(format, va);
    va_end(va);
    longjmp(exit_plugin, -123);
}
