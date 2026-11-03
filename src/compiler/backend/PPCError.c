#include "compiler/PPCError.h"
#include "compiler/CError.h"
#include "compiler/CParser.h"
#include "compiler/InlineAsm.h"
#include "driver/COSToolsCLT.h"
#include <stdarg.h>

static void PPCError_GetErrorString(char* str, short code) {
    CError_ASSERT(40, (int)code >= 100 && (int)code < PPCERR_MAX);

    COS_GetString(str, 10001, code - 99);
}

static void PPCError_VAErrorMessage(int code, va_list list, Boolean isTerminal, Boolean isWarning) {
    char format[256];

    PPCError_GetErrorString(format, code);
    CError_ErrorMessageVA(code + 10001, format, list, isTerminal, isWarning);
}

void PPCError_Error(int code, ...) {
    va_list list;

    if (trychain) {
        longjmp(trychain->jmpbuf, 1);
    }

    va_start(list, code);
    PPCError_VAErrorMessage(code, list, FALSE, FALSE);
    va_end(list);

    if (in_assembler) {
        AssemblerError();
    }
}

void PPCError_Warning(int code, ...) {
    va_list list;

    if (trychain) {
        return;
    }

    va_start(list, code);
    PPCError_VAErrorMessage(code, list, FALSE, TRUE);
    va_end(list);
}

void PPCError_Message(char* format, ...) {
    va_list list;

    if (trychain || copts.supress_warnings) {
        return;
    }

    va_start(list, format);
    CError_ErrorMessageVA(10221, format, list, FALSE, TRUE);
    va_end(list);
}

void PPCError_ErrorTerm(VarArgShort code, ...) {
    va_list list;

    if (trychain) {
        longjmp(trychain->jmpbuf, 1);
    }

    va_start(list, code);
    PPCError_VAErrorMessage(code, list, TRUE, FALSE);
    va_end(list);

    if (in_assembler) {
        AssemblerError();
    }

    longjmp(errorreturn, 1);
}
