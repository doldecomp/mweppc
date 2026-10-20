#include "driver/CLIO.h"
#include "compiler/win32.h"
#include "driver/CLErrors.h"
#include "driver/CLMain.h"
#include "driver/CLPlugins.h"
#include "driver/cmdline.h"
#include "msl/crtl.h"
#include "driver/CWPlugins.h"
#include "driver/FileHandles.h"
#include "driver/Generic.h"
#include "driver/MacFileTypes.h"
#include "driver/Memory.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include "driver/StringUtils.h"
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Boolean ioPiping;
static Boolean ioInHelp;
static SInt32 lastPrintingPlugin;
static Boolean validSrc;
static Boolean validErr;
static OSSpec srcSpec;
static OSSpec errSpec;
static Boolean srcIsErr;
static Boolean newSrc;
static Boolean newErr;
static char arrowBuf[256];
static Boolean printedAnything;
static int ioLineNum;

// (used before their definitions; nothing outside calls them)
Boolean IO_HelpTerminate(void);
void InitWorking(void);
void TermWorking(void);
void CLPrintType(SInt16 msgtype, const char* format, ...);

static BOOL __stdcall catchinterrupt(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        CLPrintErr("\nUser break, cancelled...\n");
        clState.userBreak = TRUE;
        return TRUE;
    }
    return FALSE;
}

static void SetupConsoleInfo(void) {
    HANDLE console;
    CONSOLE_SCREEN_BUFFER_INFO info;

    console = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleCtrlHandler(catchinterrupt, TRUE);
    ioPiping = FALSE;
    if (!GetConsoleScreenBufferInfo(console, &info)) {
        optsEnvir.rows = 0;
        optsEnvir.cols = 80;
    } else {
        optsEnvir.rows = info.dwSize.Y;
        optsEnvir.cols = info.dwSize.X;
        if (optsEnvir.cols > 256) {
            optsEnvir.cols = 256;
        }
    }
    // The driver writes its own "\r\n" line ends: no text translation on the standard handles
    _HandleTable[0]->translate = 0;
    _HandleTable[1]->translate = 0;
    _HandleTable[2]->translate = 0;
}

// (a deliberate crash on abort, for the debugger: volatile, so that the store is kept)
static void Crash(void) {
    *((volatile unsigned char*)NULL) = 0;
}

void SetupDebuggingTraps(void) {
    signal(SIGABRT, (__signal_func_ptr)Crash);
}

Boolean IO_Initialize(void) {
    ioPiping = FALSE;
    ioInHelp = FALSE;
    ioLineNum = 0;
    SetupConsoleInfo();

    if (ioPiping) {
        setvbuf(stdout, NULL, _IOFBF, 0x1000);
        setvbuf(stderr, NULL, _IOFBF, 0x1000);
    } else {
        setvbuf(stdout, NULL, _IOLBF, 0x1000);
        setvbuf(stderr, NULL, _IOLBF, 0x1000);
    }

    InitWorking();
    return TRUE;
}

Boolean IO_Terminate(void) {
    if (ioInHelp) {
        IO_HelpTerminate();
    }
    TermWorking();
    return TRUE;
}

Boolean IO_HelpInitialize(void) {
    ioInHelp = TRUE;
    return TRUE;
}

Boolean IO_HelpTerminate(void) {
    ioInHelp = FALSE;
    return TRUE;
}

static Boolean SendHandleToFile(FILE* file, Handle text, SInt32 size) {
    Boolean ret;
    char* ptr;
    char* lineEnd;
    char* end;

    if (file != stdout && file != stderr) {
        setvbuf(file, NULL, _IOFBF, 0x1000);
    }

    HLock(text);
    ptr = *text;
    if (size && ptr[size - 1] == 0) {
        size--;
    }

    if (size <= 0) {
        ret = TRUE;
    } else {
        end = *text + size;
        while (ptr < end) {
            lineEnd = ptr;
            while (lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r') {
                lineEnd++;
            }

            if (lineEnd - ptr) {
                if (fwrite(ptr, lineEnd - ptr, 1, file) != 1) {
                    CLReportCError(CLStr10, errno);
                    ret = FALSE;
                    goto fail;
                }
            }

            if (CheckForUserBreak()) {
                ret = TRUE;
                goto fail;
            }

            if (fwrite("\r\n", strlen("\r\n"), 1, file) != 1) {
                CLReportCError(CLStr10, errno);
                ret = FALSE;
                goto fail;
            }

            if (lineEnd < end && *(lineEnd++) == '\r' && *lineEnd == '\n') {
                ++lineEnd;
            }
            ptr = lineEnd;
        }
        ret = TRUE;
    }

fail:
    HUnlock(text);
    fflush(file);

    if (file != stdout && file != stderr) {
        setvbuf(file, NULL, _IONBF, 0x1000);
    }

    return ret;
}

void FixHandleForIDE(Handle text, UInt32 size) {
    char* ptr;

    HLock(text);
    for (ptr = *text; ptr < (*text + size); ptr++) {
        if (*ptr == '\r') {
            *ptr = '\n';
        }
    }
    HUnlock(text);
}

Boolean ShowHandle(Handle text, UInt32 size, Boolean decorate) {
    static const char* const border = "===============\n";

    if (decorate) {
        fprintf(stdout, border);
    }
    if (!SendHandleToFile(stdout, text, size)) {
        return FALSE;
    }
    if (decorate) {
        fprintf(stdout, border);
    }
    fflush(stdout);

    return TRUE;
}

Boolean WriteHandleToFile(OSSpec* spec, Handle text, UInt32 size, OSType creator, OSType type) {
    FILE* file;
    char path[256];

    // (the original passes a full path's size, 260, for this 256-byte buffer)
    OS_SpecToString(spec, path, sizeof(OSPathSpec));
    file = fopen(path, "w+b");
    if (!file) {
        CLReportCError(CLStr8, errno, path);
        return FALSE;
    }

    SendHandleToFile(file, text, size);
    fclose(file);

    OS_SetMacFileCreatorAndType(spec, creator, type);
    return TRUE;
}

Boolean WriteBinaryHandleToFile(OSSpec* spec, OSType maccreator, OSType mactype, OSHandle* text) {
    int err;
    OSFileHandle file;

    if ((err = OS_NewFileHandle(spec, text, TRUE, &file)) ||
        (err = OS_FreeFileHandle(&file)) ||
        (err = OS_SetMacFileCreatorAndType(spec, maccreator, mactype))) {
        CLReportOSError(CLStr8, err, OS_SpecToString(spec, STSbuf, sizeof(STSbuf)));
        return FALSE;
    }

    return TRUE;
}

Boolean AppendHandleToFile(
    OSSpec* spec, Handle text, UInt32 size, OSType maccreator, OSType mactype
) {
    FILE* file;
    char path[256];

    // (the original passes a full path's size, 260, for this 256-byte buffer)
    OS_SpecToString(spec, path, sizeof(OSPathSpec));
    file = fopen(path, "a+b");
    if (!file) {
        CLReportCError(CLStr8, errno, path);
        return FALSE;
    }

    SendHandleToFile(file, text, size);
    fclose(file);

    OS_SetMacFileCreatorAndType(spec, maccreator, mactype);
    return TRUE;
}

void InitWorking(void) {}

void ShowWorking(int x) {}

void TermWorking(void) {}

static void ProgressFunction(const char* functionname) {
    if (optsCmdLine.verbose) {
        CLReport(CLStr7, functionname);
    }
}

Boolean CheckForUserBreak(void) {
    ShowWorking(4);
    return clState.userBreak;
}

// NOTE: invented name
typedef struct {
    char* buffer;
    int pos;
    int maxlen;
    SInt16 col;
    Boolean original;
    char* newline;
} TextBuffer;

// NOTE: invented names: appendText, appendChar, StartLine, newLine, WrapText (nothing in the binary
// names these helpers)
inline void appendText(TextBuffer* f, const char* str) {
    int len = strlen(str);

    if (f->pos + len >= f->maxlen) {
        f->maxlen = f->maxlen * 2 + len;
        if (f->original) {
            char* oldbuf;
            oldbuf = f->buffer;
            f->buffer = xmalloc("message buffer", f->maxlen);
            memcpy(f->buffer, oldbuf, f->pos);
        } else {
            f->buffer = xrealloc("message buffer", f->original ? NULL : f->buffer, f->maxlen);
        }
        f->original = FALSE;
    }

    memcpy(f->buffer + f->pos, str, len);
    f->pos += len;
    f->col += len;
}

inline void appendChar(TextBuffer* f, char c) {
    if (f->pos >= f->maxlen) {
        f->maxlen <<= 1;
        if (f->original) {
            char* oldbuf = f->buffer;
            f->buffer = xmalloc("message buffer", f->maxlen);
            memcpy(f->buffer, oldbuf, f->pos);
        } else {
            f->buffer = xrealloc("message buffer", f->buffer, f->maxlen);
        }
        f->original = FALSE;
    }

    f->buffer[f->pos++] = c;
    f->col++;
}

static void StartLine(TextBuffer* f) {
    f->col = 0;
    if (!optsEnvir.underIDE && f->newline) {
        appendText(f, f->newline);
    }
}

inline void newLine(TextBuffer* f) {
    if (f->newline) {
        appendChar(f, '\n');
    } else {
        appendChar(f, ' ');
    }
    StartLine(f);
}

static void WrapText(TextBuffer* f) {
    int wrapped;
    char wrapbuf[256];
    char c;

    wrapped = 0;
    if (optsCmdLine.noWrapOutput ||
        !f->newline ||
        strlen(f->newline) > (size_t)(optsEnvir.cols / 2)) {
        return;
    }

    while (wrapped < (optsEnvir.cols - 1) && (size_t)f->col > strlen(f->newline)) {
        c = f->buffer[f->pos - 1];
        if (c == ' ') {
            break;
        }
        if (c == '/') {
            break;
        }
        if (c == '-') {
            break;
        }
        wrapbuf[wrapped] = c;
        wrapped++;
        f->pos--;
        f->col--;
    }

    if ((size_t)f->col <= strlen(f->newline)) {
        while (f->col < (optsEnvir.cols - 1) && wrapped > 0) {
            appendChar(f, wrapbuf[--wrapped]);
        }
    }

    newLine(f);
    while (wrapped > 0 && f->maxlen > 0) {
        appendChar(f, wrapbuf[--wrapped]);
    }
}

static char* IO_VFormatText(
    char* buffer, SInt32 size, char* newline, const char* format, va_list ap
) {
    TextBuffer f;
    char* arg;

    f.buffer = buffer;
    f.pos = 0;
    f.maxlen = size;
    f.col = 0;
    f.original = TRUE;
    f.newline = newline;
    StartLine(&f);

    while (*format && f.maxlen > 0) {
        if (*format == '%') {
            arg = va_arg(ap, char*);
            while (*arg && f.maxlen > 0) {
                if (*arg == '\r' || *arg == '\n') {
                    newLine(&f);
                    arg++;
                } else if (*arg == '\t') {
                    if (f.maxlen > 0) {
                        do {
                            appendChar(&f, ' ');
                        } while (f.col & 7);
                    }
                    arg++;
                } else {
                    appendChar(&f, *(arg++));
                    if (!optsCmdLine.noWrapOutput && f.newline && f.col >= optsEnvir.cols - 1) {
                        WrapText(&f);
                    }
                }
            }
            format++;
        } else if (*format == '\r' || *format == '\n') {
            format++;
            newLine(&f);
        } else {
            appendChar(&f, *(format++));
            if (!optsCmdLine.noWrapOutput && f.col >= optsEnvir.cols - 1) {
                WrapText(&f);
            }
        }
    }

    if (f.newline) {
        if ((size_t)(int)f.col == strlen(f.newline)) {
            f.pos -= f.col;
            f.col = 0;
        }
    }

    appendChar(&f, 0);

    return f.buffer;
}

char* IO_FormatText(char* buffer, SInt32 size, char* newline, const char* format, ...) {
    va_list ap;
    char* ret;

    va_start(ap, format);
    ret = IO_VFormatText(buffer, size, newline, format, ap);
    va_end(ap);

    return ret;
}

// The paging prompt for the help output: its pointer is in .data, but nothing in the binary reads
// it.
// NOTE: invented name
static const char* pagePrompt = "\n[Press enter for next page, 'q'+enter to quit]\r";

void CLPrintDispatch(int unused, SInt16 msgtype, const char* message) {
    FILE* out;
    const char* ptr;
    const char* nptr;

    if (optsCmdLine.stderr2stdout == 1 ||
        optsCmdLine.stderr2stdout == 2 ||
        msgtype == Msg_Note ||
        msgtype == Msg_Status) {
        out = stdout;
    } else if (msgtype == Msg_Warning || msgtype == Msg_Error || msgtype == Msg_Alert) {
        out = stderr;
    } else {
        OS_ASSERT(847, 0);
    }

    if (!printedAnything && !ioPiping) {
        printedAnything = TRUE;
        ioLineNum = 0;
    }

    ptr = message;
    while (*ptr) {
        nptr = ptr;
        while (*nptr && *nptr != '\n' && *nptr != '\r') {
            ++nptr;
        }

        if (nptr - ptr) {
            if (fwrite(ptr, nptr - ptr, 1, out) != 1) {
                clState.userBreak = TRUE;
            }
        }

        if (*nptr) {
            ioLineNum++;
            fwrite("\r\n", strlen("\r\n"), 1, out);
            if (*nptr == '\r') {
                nptr++;
            }
            if (*nptr == '\n') {
                nptr++;
            }
            if (nptr[0] && !nptr[1] && (nptr[0] == '\n' || nptr[0] == '\r')) {
                nptr++;
            }
        }

        ptr = nptr;
    }

    if (out == stdout && optsCmdLine.verbose > 1) {
        fflush(out);
    }
}

// (a static; Projects.c defines a global GetFileInfo)
#define GetFileInfo CLIO_GetFileInfo

static void GetFileInfo(MessageRef* ref) {
    if (!ref) {
        return;
    }
    srcIsErr = OS_EqualSpec(&ref->sourcefile, &ref->errorfile);
    if (!validSrc || !OS_EqualSpec(&ref->sourcefile, &srcSpec)) {
        newSrc = TRUE;
        srcSpec = ref->sourcefile;
        validSrc = TRUE;
    } else {
        newSrc = FALSE;
    }
    if (!validErr || !OS_EqualSpec(&ref->errorfile, &errSpec)) {
        newErr = TRUE;
        errSpec = ref->errorfile;
        validErr = TRUE;
    } else {
        newErr = FALSE;
    }
}

static char* Arrows(MessageRef* ref) {
    int start;
    int len;

    arrowBuf[0] = 0;
    start = ref->tokenoffset;
    start %= optsEnvir.cols;
    if (start >= 0 && (size_t)start < sizeof(arrowBuf)) {
        len = ref->tokenlength;
        if ((size_t)(len + start) > sizeof(arrowBuf)) {
            len = sizeof(arrowBuf) - start;
        }
        if (len == 0) {
            len = 1;
        }
        memset(arrowBuf, ' ', start);
        memset(arrowBuf + start, '^', len);
        arrowBuf[start + len] = 0;
    }

    return arrowBuf;
}

static Boolean IsLikelyAnImporter(Plugin* plugin) {
    if (Plugin_CL_GetObjectFlags(plugin)->flags & 0x80000000) {
        return FALSE;
    }

    if (plugin ==
        Plugins_CL_MatchTarget(NULL, gTarg->cpu, gTarg->os, clState.plugintype, clState.language)) {
        return FALSE;
    }
    return TRUE;
}

// clang-format off
static const char* msgnames[6] = {
    "",
    "Note",
    "Warning",
    "Error",
    "Alert",
    "Status"
};
// clang-format on

static char* GuessTool(Plugin* plugin) {
    if (plugin) {
        DropInFlags* df = Plugin_GetDropInFlags(plugin);
        switch (df->dropintype) {
            case CWDROPINDRIVERTYPE:
                return "Driver";
            case CWDROPINPARSERTYPE:
                return "Usage";
            case CWDROPINCOMPILERTYPE:
                if (df->edit_language == Lang_C_CPP || df->edit_language == Lang_Pascal) {
                    return "Compiler";
                } else if (df->edit_language == 'Asm ') {
                    return "Assembler";
                } else if (IsLikelyAnImporter(plugin)) {
                    return "Importer";
                } else {
                    return "Compiler";
                }
            case CWDROPINLINKERTYPE:
                return "Linker";
        }
    }
    return "Driver";
}

static char* GuessDoing(Plugin* plugin) {
    if (plugin) {
        DropInFlags* df = Plugin_GetDropInFlags(plugin);
        switch (df->dropintype) {
            case CWDROPINPARSERTYPE:
                return "parsing";
            case CWDROPINCOMPILERTYPE:
                return "compiling";
            case CWDROPINLINKERTYPE:
                return "linking";
        }
    }
    return "processing";
}

static void styledMessage_MPW(
    Plugin* plugin,
    MessageRef* ref,
    SInt32 errorNumber,
    SInt16 msgType,
    const char* format,
    va_list va
) {
    char msgBuf[256];
    char* msgPtr;

    msgPtr = IO_VFormatText(msgBuf, sizeof(msgBuf), "#   ", format, va);
    if (msgType != Msg_Status) {
        CLPrintType(
            msgType, "### %s %s %s:\n", clState.programName, GuessTool(plugin), msgnames[msgType]
        );
    }

    if (ref) {
        CLPrintType(msgType, "#\t%s\n", ref->sourceline);
        CLPrintType(msgType, "#\t%s\n", Arrows(ref));
    }

    CLPrintDispatch(0, msgType, msgPtr);

    if (ref) {
        CLPrintType(msgType, "#----------------------------------------------------------\n");
        CLPrintType(
            msgType,
            "    File \"%s\"; Line %d\n",
            OS_SpecToString(&ref->errorfile, STSbuf, sizeof(STSbuf)),
            ref->linenumber
        );
        GetFileInfo(ref);
        if (!srcIsErr) {
            CLPrintType(
                msgType,
                "#\twhile %s \"%s\"\n",
                GuessDoing(plugin),
                OS_SpecToString(&ref->sourcefile, STSbuf, sizeof(STSbuf))
            );
        }
        CLPrintType(msgType, "#----------------------------------------------------------\n");
    }

    if (msgPtr != msgBuf) {
        free(msgPtr);
    }
}

static void styledMessage_Default(
    Plugin* plugin,
    MessageRef* ref,
    SInt32 errorNumber,
    SInt16 msgType,
    const char* format,
    va_list va
) {
    char lineBuf[324];
    char msgBuf[256];
    char* msgPtr;
    char* ptr;
    SInt16 cols;

    msgPtr = IO_VFormatText(msgBuf, sizeof(msgBuf), "#   ", format, va);
    if (ref) {
        CLPrintType(msgType, "### %s %s:\n", clState.programName, GuessTool(plugin));
    } else if (msgType != Msg_Status) {
        CLPrintType(
            msgType, "### %s %s %s:\n", clState.programName, GuessTool(plugin), msgnames[msgType]
        );
    }

    if (ref) {
        GetFileInfo(ref);
        if (newErr || (newSrc && srcIsErr)) {
            if (srcIsErr) {
                sprintf(
                    lineBuf,
                    "#%8s: %s\n",
                    "File",
                    OS_SpecToStringRelative(&errSpec, NULL, STSbuf, sizeof(STSbuf))
                );
            } else {
                sprintf(
                    lineBuf,
                    "#%8s: %s\n",
                    "In",
                    OS_SpecToStringRelative(&errSpec, NULL, STSbuf, sizeof(STSbuf))
                );
            }
            CLPrintDispatch(0, msgType, lineBuf);
        }
        if (newSrc && !srcIsErr) {
            sprintf(
                lineBuf,
                "# %7s: %s\n",
                "From",
                OS_SpecToStringRelative(&srcSpec, NULL, STSbuf, sizeof(STSbuf))
            );
            CLPrintDispatch(0, msgType, lineBuf);
        }
        if (newErr || newSrc) {
            ptr = &lineBuf[2];
            while (*ptr && *ptr != '\n') {
                *(ptr++) = '-';
            }
            *ptr = 0;
            if (ptr - lineBuf >= (cols = optsEnvir.cols) - 1) {
                lineBuf[cols - 1] = 0;
            }
            strcat(lineBuf, "\n");
            CLPrintDispatch(0, msgType, lineBuf);
        }
        CLPrintType(msgType, "#%8d: %s\n", ref->linenumber, ref->sourceline);
        CLPrintType(msgType, "#%8s: %s\n", msgnames[msgType], Arrows(ref));
    }

    CLPrintDispatch(0, msgType, msgPtr);
    if (msgPtr != msgBuf) {
        free(msgPtr);
    }
}

// clang-format off
static const char* msgnames_gcc[6] = {
    "",
    " note",
    " warning",
    "",
    " alert",
    " status"
};
// clang-format on

static void styledMessage_Terse(
    Plugin* plugin,
    MessageRef* ref,
    SInt32 errorNumber,
    SInt16 msgType,
    const char* format,
    va_list va
) {
    char* msg;
    char* ptr;
    char msgBuf[256];
    char* msgPtr;
    char header[256];
    char* headPtr;
    char errBuf[256];
    char* errPtr;

    if (ref) {
        if (msgnames_gcc[msgType][0]) {
            headPtr = mprintf(
                header,
                sizeof(header),
                "%s:%d:%s: ",
                OS_SpecToStringRelative(&ref->errorfile, NULL, STSbuf, sizeof(STSbuf)),
                ref->linenumber,
                msgnames_gcc[msgType]
            );
        } else {
            headPtr = mprintf(
                header,
                sizeof(header),
                "%s:%d: ",
                OS_SpecToStringRelative(&ref->errorfile, NULL, STSbuf, sizeof(STSbuf)),
                ref->linenumber
            );
        }
    } else if (msgType != Msg_Status) {
        headPtr = mprintf(header, sizeof(header), "%s: ", clState.programName);
    } else {
        header[0] = 0;
        headPtr = header;
    }

    msgPtr = IO_VFormatText(msgBuf, sizeof(msgBuf), headPtr, format, va);
    // (the original's: a #warning or #error message without a ref reads through NULL here)
    if ((strstr(msgPtr, "#warning") || strstr(msgPtr, "#error")) && ref->sourceline[0]) {
        errPtr = IO_FormatText(errBuf, sizeof(errBuf), headPtr, "%", ref->sourceline);
    } else {
        errPtr = NULL;
    }

    if (headPtr != header) {
        free(headPtr);
    }

    msg = msgPtr;
    while (*msg) {
        ptr = msg;
        while (*ptr && *ptr != '\n') {
            ptr++;
        }

        CLPrintType(msgType, "%.*s\n", ptr - msg, msg);
        if (errPtr) {
            CLPrintType(msgType, "%s\n", errPtr);
            if (errPtr != errBuf) {
                free(errPtr);
            }
            errPtr = NULL;
        }

        if (*ptr) {
            msg = ptr + 1;
        } else {
            msg = ptr;
        }
    }

    if (msgPtr != msgBuf) {
        free(msgPtr);
    }
}

static void styledMessage_IDE(
    Plugin* plugin,
    MessageRef* ref,
    SInt32 errorNumber,
    SInt16 msgType,
    const char* format,
    va_list va
) {
    char msgBuf[256];
    char* msgPtr;
    SInt16 oldCols;

    oldCols = optsEnvir.cols;
    msgPtr = IO_VFormatText(msgBuf, sizeof(msgBuf), "           ", format, va);
    optsEnvir.cols = oldCols;

    CLPrintType(msgType, "%8s : %s\n", msgnames[msgType], msgPtr + 11);
    if (msgPtr != msgBuf) {
        free(msgPtr);
    }

    if (ref) {
        msgPtr = IO_FormatText(msgBuf, sizeof(msgBuf), "\t", "%", ref->sourceline);
        CLPrintType(
            msgType,
            "%s line %d%s\n",
            OS_SpecToStringRelative(&ref->errorfile, NULL, STSbuf, sizeof(STSbuf)),
            ref->linenumber,
            msgPtr
        );
        if (msgPtr != msgBuf) {
            free(msgPtr);
        }
    }
}

static void styledMessage_Parseable(
    Plugin* plugin,
    MessageRef* ref,
    SInt32 errorNumber,
    SInt16 msgType,
    const char* format,
    va_list va
) {
    char msgBuf[256];
    char* msgPtr;

    CLPrintType(msgType, "%s|%s|%s\n", clState.programName, GuessTool(plugin), msgnames[msgType]);
    if (ref) {
        CLPrintType(
            msgType,
            "(%s|%d|%d|%d|%d|%d)\n",
            OS_SpecToString(&ref->errorfile, STSbuf, sizeof(STSbuf)),
            ref->linenumber,
            ref->tokenoffset,
            ref->tokenlength,
            ref->selectionoffset,
            ref->selectionlength
        );
        CLPrintType(msgType, "=%s\n", ref->sourceline);
    }

    msgPtr = IO_VFormatText(msgBuf, sizeof(msgBuf), ">", format, va);
    CLPrintType(msgType, "%s\n", msgPtr);
    if (msgPtr != msgBuf) {
        free(msgPtr);
    }
}

void CLPrintType(SInt16 msgtype, const char* format, ...) {
    char printbuffer[256];
    char* ptr;
    va_list va;

    va_start(va, format);
    ptr = mvprintf(printbuffer, sizeof(printbuffer), format, va);
    va_end(va);
    CLPrintDispatch(0, msgtype, ptr);
    if (ptr != printbuffer) {
        free(ptr);
    }
}

void CLPrint(const char* format, ...) {
    char printbuffer[256];
    char* ptr;
    va_list va;

    va_start(va, format);
    ptr = mvprintf(printbuffer, sizeof(printbuffer), format, va);
    va_end(va);
    CLPrintDispatch(0, Msg_Note, ptr);
    if (ptr != printbuffer) {
        free(ptr);
    }
}

void CLPrintWarning(const char* format, ...) {
    char printbuffer[256];
    char* ptr;
    va_list va;

    va_start(va, format);
    ptr = mvprintf(printbuffer, sizeof(printbuffer), format, va);
    va_end(va);
    CLPrintDispatch(0, Msg_Warning, ptr);
    if (ptr != printbuffer) {
        free(ptr);
    }
}

void CLPrintErr(const char* format, ...) {
    char printbuffer[256];
    char* ptr;
    va_list va;

    va_start(va, format);
    ptr = mvprintf(printbuffer, sizeof(printbuffer), format, va);
    va_end(va);
    CLPrintDispatch(0, Msg_Error, ptr);
    if (ptr != printbuffer) {
        free(ptr);
    }
}

static void FixupMessageRef(MessageRef* ref) {
    char* bol;
    char* eol;
    int len;
    int scan;

    if (ref->sourceline && *ref->sourceline) {
        bol = ref->sourceline + ref->tokenoffset;
        eol = ref->sourceline + ref->tokenoffset + ref->tokenlength - 1;
        if (eol < bol) {
            eol = bol;
        }
        while (bol > ref->sourceline && bol[-1] != '\r') {
            bol--;
        }
        while (*eol && *eol != '\r' && *eol != '\n') {
            eol++;
        }

        len = eol - bol;
        ref->sourceline = xmalloc("text buffer", len + 1);
        strncpy(ref->sourceline, bol, len);
        ref->sourceline[len] = 0;

        for (scan = 0; scan < len; scan++) {
            if (ref->sourceline[scan] < ' ' || ref->sourceline[scan] >= 127) {
                ref->sourceline[scan] = ' ';
            }
        }
    } else {
        ref->sourceline = NULL;
    }
}

SInt16 CLStyledMessageDispatch(
    Plugin* plugin, MessageRef* ref, SInt32 errorNumber, SInt16 msgType, const char* format, ...
) {
    va_list va;
    MessageRef myref;
    UInt32 ptype;

    if (msgType == Msg_Warning) {
        if (plugin) {
            ptype = Plugin_GetPluginType(plugin);
        } else {
            ptype = CWDROPINDRIVERTYPE;
        }
        if (optsCmdLine.noWarnings) {
            return 0;
        }
        if ((ptype == CWDROPINDRIVERTYPE || ptype == CWDROPINPARSERTYPE) &&
            optsCmdLine.noCmdLineWarnings) {
            return 0;
        }
        if (optsCmdLine.warningsAreErrors) {
            msgType = Msg_Error;
        }
    }

    if (msgType == Msg_Error && clState.withholdErrors) {
        return 0;
    }
    if (msgType == Msg_Warning && clState.withholdWarnings) {
        return 0;
    }

    if (ref) {
        myref = *ref;
        FixupMessageRef(&myref);
    }

    va_start(va, format);
    if ((ptype = optsCmdLine.msgStyle) == 2) {
        styledMessage_MPW(plugin, ref ? &myref : NULL, errorNumber, msgType, format, va);
    } else if (ptype == 1) {
        styledMessage_Terse(plugin, ref ? &myref : NULL, errorNumber, msgType, format, va);
    } else if (ptype == 3) {
        styledMessage_IDE(plugin, ref ? &myref : NULL, errorNumber, msgType, format, va);
    } else if (ptype == 4) {
        styledMessage_Parseable(plugin, ref ? &myref : NULL, errorNumber, msgType, format, va);
    } else {
        styledMessage_Default(plugin, ref ? &myref : NULL, errorNumber, msgType, format, va);
    }
    va_end(va);

    if (ref && myref.sourceline) {
        xfree(myref.sourceline);
    }

    if (msgType == Msg_Error && optsCmdLine.maxErrors) {
        if (++clState.countErrors >= optsCmdLine.maxErrors) {
            clState.withholdErrors = TRUE;
            if (!optsCompiler.noFail) {
                CLReport(CLStr70);
                clState.userBreak = TRUE;
            } else {
                CLReport(CLStr71);
            }
        }
    }

    if (msgType == Msg_Warning && optsCmdLine.maxWarnings) {
        if (++clState.countWarnings >= optsCmdLine.maxWarnings) {
            clState.withholdWarnings = TRUE;
            CLReport(CLStr72);
        }
    }

    return msgType;
}
