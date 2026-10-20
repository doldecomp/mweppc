#include "compiler/CPrepTokenizer.h"
#include "compiler/CError.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "driver/COSToolsCLT.h"
#include "driver/oslib.h"
#include <string.h>

static Boolean prepnextstringchar_foundnl;

StringPtr CPrep_SkipNewComment(StringPtr str);

short prepskipnextchar(void) {
    short c;
    StringPtr start;
    StringPtr p;

    start = (StringPtr)pos;
    p = (StringPtr)pos;

loop:
    switch ((c = *p)) {
        case 0:
            if (tokenstacklevel > 0 || p >= (StringPtr)prep_file_end) {
                nextcharpos = pos = (char*)p;
                return c;
            } else {
                pos = (char*)p++;
                nextcharpos = (char*)p;
                return c;
            }
        case TK_MACRO_NOEXPAND:
        case TK_MACRO_BOUNDARY:
            p++;
            goto loop;
        case ' ':
        case '\t':
        case LF:
        case '\v':
        case '\f':
            p++;
            spaceskip = TRUE;
            goto loop;
        case ';':
            pos = (char*)p++;
            if (tokenstacklevel <= 0 && in_assembler && copts.asmsemicolcomment) {
                spaceskip = TRUE;
                nextcharpos = (char*)CPrep_SkipNewComment(p);
                return CPrep_SkipNewCommentChar;
            }
            nextcharpos = (char*)p;
            return c;
        case '#':
            pos = (char*)p++;
            if (tokenstacklevel <= 0 && in_assembler && copts.asmpoundcomment) {
                spaceskip = TRUE;
                nextcharpos = (char*)CPrep_SkipNewComment(p);
                return CPrep_SkipNewCommentChar;
            }
            nextcharpos = (char*)p;
            return c;
        case '/':
            pos = (char*)p++;
            if (copts.multibyteaware) {
                while (p[0] == '\\' && !COS_IsMultiByte(start, p) && p[1] == CR) {
                    foundnl();
                    if (p[2] == LF) {
                        p += 3;
                    } else {
                        p += 2;
                    }
                }
            } else {
                while (p[0] == '\\' && p[1] == CR) {
                    foundnl();
                    if (p[2] == LF) {
                        p += 3;
                    } else {
                        p += 2;
                    }
                }
            }
            if (tokenstacklevel <= 0) {
                if (p[0] == '/' && (!copts.ANSIstrict || copts.cplusplus || copts.c9x)) {
                    spaceskip = TRUE;
                    nextcharpos = (char*)CPrep_SkipNewComment(p + 1);
                    return CPrep_SkipNewCommentChar;
                } else if (p[0] == '*') {
                    spaceskip = TRUE;
                    p++;
                    for (;;) {
                        if (!(c = *(p++))) {
                            if (tokenstacklevel > 0 || p >= (StringPtr)prep_file_end) {
                                CPrep_Error(ERR_UNTERMINATED_COMMENT);
                                nextcharpos = (char*)p - 1;
                                return c;
                            }
                            p[-1] = ' ';
                        }
                        if (c == '*') {
                            if (copts.multibyteaware) {
                                while (p[0] == '\\' && !COS_IsMultiByte(start, p) && p[1] == CR) {
                                    foundnl();
                                    if (p[2] == LF) {
                                        p += 3;
                                    } else {
                                        p += 2;
                                    }
                                }
                            } else {
                                while (p[0] == '\\' && p[1] == CR) {
                                    foundnl();
                                    if (p[2] == LF) {
                                        p += 3;
                                    } else {
                                        p += 2;
                                    }
                                }
                            }
                            if (*p == '/') {
                                p++;
                                goto loop;
                            }
                        }
                        if (c == CR) {
                            foundnl();
                        }
                    }
                }
            }
            nextcharpos = (char*)p;
            return c;
        case '%':
            pos = (char*)p++;
            if (*p == ':' && copts.cplusplus) {
                nextcharpos = (char*)p + 1;
                return '#';
            }
            nextcharpos = (char*)p;
            return c;
        case '\\':
            pos = (char*)p++;
mid_backslash:
            if (*p == CR) {
                if (!copts.multibyteaware || !COS_IsMultiByte(start, p - 1)) {
                    foundnl();
                    if (p[1] == LF) {
                        p += 2;
                    } else {
                        p += 1;
                    }
                    goto loop;
                }
            }
            nextcharpos = (char*)p;
            return c;
        case '?':
            pos = (char*)p++;
            if (copts.trigraphs && p[0] == '?') {
                switch (p[1]) {
                    case '=':
                        nextcharpos = (char*)p + 2;
                        return '#';
                    case '/':
                        c = '\\';
                        p += 2;
                        goto mid_backslash;
                    case '\'':
                        nextcharpos = (char*)p + 2;
                        return '^';
                    case '(':
                        nextcharpos = (char*)p + 2;
                        return '[';
                    case ')':
                        nextcharpos = (char*)p + 2;
                        return ']';
                    case '!':
                        nextcharpos = (char*)p + 2;
                        return '|';
                    case '<':
                        nextcharpos = (char*)p + 2;
                        return '{';
                    case '>':
                        nextcharpos = (char*)p + 2;
                        return '}';
                    case '-':
                        nextcharpos = (char*)p + 2;
                        return '~';
                }
            }
            nextcharpos = (char*)p;
            return c;
        default:
            pos = (char*)p++;
            nextcharpos = (char*)p;
            return c;
    }
}

short prepnextchar(void) {
    short c;
    StringPtr start;
    StringPtr p;

    start = (StringPtr)pos;
    p = (StringPtr)pos;

loop:
    switch ((c = *(p++))) {
        case 0:
            pos = (char*)p - 1;
            return c;
        case '/':
            if (copts.multibyteaware) {
                while (p[0] == '\\' && !COS_IsMultiByte(start, p) && p[1] == CR) {
                    foundnl();
                    if (p[2] == LF) {
                        p += 3;
                    } else {
                        p += 2;
                    }
                }
            } else {
                while (p[0] == '\\' && p[1] == CR) {
                    foundnl();
                    if (p[2] == LF) {
                        p += 3;
                    } else {
                        p += 2;
                    }
                }
            }
            if (p[0] == '/' && (!copts.ANSIstrict || copts.cplusplus)) {
                spaceskip = TRUE;
                pos = (char*)CPrep_SkipNewComment(p + 1);
                return CPrep_SkipNewCommentChar;
            } else if (p[0] == '*') {
                spaceskip = TRUE;
                p++;
                for (;;) {
                    if (!(c = *(p++))) {
                        if (tokenstacklevel > 0 || p >= (StringPtr)prep_file_end) {
                            pos = (char*)p - 1;
                            CPrep_Error(ERR_UNTERMINATED_COMMENT);
                            return c;
                        }
                        p[-1] = ' ';
                    }
                    if (c == '*') {
                        if (copts.multibyteaware) {
                            while (p[0] == '\\' && !COS_IsMultiByte(start, p) && p[1] == CR) {
                                foundnl();
                                if (p[2] == LF) {
                                    p += 3;
                                } else {
                                    p += 2;
                                }
                            }
                        } else {
                            while (p[0] == '\\' && p[1] == CR) {
                                foundnl();
                                if (p[2] == LF) {
                                    p += 3;
                                } else {
                                    p += 2;
                                }
                            }
                        }
                        if (*p == '/') {
                            break;
                        }
                    }
                    if (c == CR) {
                        foundnl();
                    }
                }
                pos = (char*)p + 1;
                return ' ';
            } else {
                pos = (char*)p;
                return c;
            }
        case '\\':
backslash:
            if (*p == CR) {
                if (!copts.multibyteaware || !COS_IsMultiByte(start, p - 1)) {
                    foundnl();
                    if (p[1] == LF) {
                        p += 2;
                    } else {
                        p += 1;
                    }
                    goto loop;
                }
            }
            pos = (char*)p;
            return c;
        case '%':
            if (*p == ':' && copts.cplusplus) {
                pos = (char*)p + 1;
                return '#';
            }
            pos = (char*)p;
            return c;
        case '?':
            if (copts.trigraphs && p[0] == '?') {
                switch (p[1]) {
                    case '=':
                        pos = (char*)p + 2;
                        return '#';
                    case '/':
                        c = '\\';
                        p += 2;
                        goto backslash;
                    case '\'':
                        pos = (char*)p + 2;
                        return '^';
                    case '(':
                        pos = (char*)p + 2;
                        return '[';
                    case ')':
                        pos = (char*)p + 2;
                        return ']';
                    case '!':
                        pos = (char*)p + 2;
                        return '|';
                    case '<':
                        pos = (char*)p + 2;
                        return '{';
                    case '>':
                        pos = (char*)p + 2;
                        return '}';
                    case '-':
                        pos = (char*)p + 2;
                        return '~';
                }
            }
            pos = (char*)p;
            return c;
        default:
            pos = (char*)p;
            return c;
    }
}

// NOTE: invented names: str, countNewlines
short prepnextstringchar(StringPtr str, Boolean countNewlines) {
    StringPtr p;
    short c;

    p = (StringPtr)pos;

loop:
    switch ((c = *(p++))) {
        case 0:
            pos = (char*)p - 1;
            return c;
        case '\\':
backslash:
            if (p[0] == CR && (!copts.multibyteaware || !COS_IsMultiByte(str, p - 1))) {
                if (countNewlines) {
                    foundnl();
                } else {
                    prepnextstringchar_foundnl = TRUE;
                }
                if (p[1] == LF) {
                    p += 2;
                } else {
                    p += 1;
                }
                goto loop;
            }
            pos = (char*)p;
            return c;
        case '?':
            if (copts.trigraphs && p[0] == '?') {
                switch (p[1]) {
                    case '=':
                        pos = (char*)p + 2;
                        return '#';
                    case '/':
                        c = '\\';
                        p += 2;
                        goto backslash;
                    case '\'':
                        pos = (char*)p + 2;
                        return '^';
                    case '(':
                        pos = (char*)p + 2;
                        return '[';
                    case ')':
                        pos = (char*)p + 2;
                        return ']';
                    case '!':
                        pos = (char*)p + 2;
                        return '|';
                    case '<':
                        pos = (char*)p + 2;
                        return '{';
                    case '>':
                        pos = (char*)p + 2;
                        return '}';
                    case '-':
                        pos = (char*)p + 2;
                        return '~';
                }
            }
            pos = (char*)p;
            return c;
        default:
            pos = (char*)p;
            return c;
    }
}

// NOTE: invented names: ch, countNewlines
void CPrep_MatchChar(char ch, Boolean countNewlines) {
    char* p;
    char* start;
    char c;
    Boolean haveBackslash;

    p = start = pos;
    haveBackslash = FALSE;

loop:
    if (!(c = *(p++))) {
        if (tokenstacklevel > 0 || p >= prep_file_end) {
            CPrep_Error(ERR_UNEXPECTED_END_FILE);
        } else if (ch == '"') {
            CPrep_Error(ERR_ILLEGAL_STRING_CONSTANT);
        } else if (ch == '\'') {
            CPrep_Error(ERR_ILLEGAL_CHARACTER_CONSTANT);
        }
        CPrep_Error(ERR_PREPROCESSOR_SYNTAX_ERROR);
        pos = p - 1;
        return;
    }

    if (c == CR) {
        if (!copts.multibyteaware || !COS_IsMultiByte(start, p - 1)) {
            if (countNewlines) {
                foundnl();
            }
            if (*p == LF) {
                p++;
            }
            CPrep_Error(ERR_UNEXPECTED_END_LINE);
            pos = p;
            return;
        }
        haveBackslash = FALSE;
        goto loop;
    }

    if (c == '?' && p[0] == '?' && copts.trigraphs) {
        switch (p[1]) {
            case '!':
            case '\'':
            case '(':
            case ')':
            case '-':
            case '<':
            case '=':
            case '>':
                p += 2;
                haveBackslash = FALSE;
                goto loop;
            case '/':
                p += 2;
                goto backslash;
        }
    }

    if (c == '\\' && !haveBackslash && (!copts.multibyteaware || !COS_IsMultiByte(start, p - 1))) {
backslash:
        if (*p == CR) {
            if (countNewlines) {
                foundnl();
            }
            if (*(++p) == LF) {
                p++;
            }
            haveBackslash = FALSE;
        } else {
            haveBackslash = TRUE;
        }
        goto loop;
    }

    if (c == ch && !haveBackslash) {
        pos = p;
        return;
    }
    haveBackslash = FALSE;
    goto loop;
}

// NOTE: invented names: start, ch, countNewlines
char* CPrep_MatchChar2(char* start, char ch, Boolean countNewlines) {
    char* p;
    char c;
    Boolean haveBackslash;

    p = start;
    haveBackslash = FALSE;

loop:
    if (!(c = *(p++))) {
        if (tokenstacklevel > 0 || p >= prep_file_end) {
            CPrep_Error(ERR_UNEXPECTED_END_FILE);
        } else if (ch == '"') {
            CPrep_Error(ERR_ILLEGAL_STRING_CONSTANT);
        } else if (ch == '\'') {
            CPrep_Error(ERR_ILLEGAL_CHARACTER_CONSTANT);
        }
        CPrep_Error(ERR_PREPROCESSOR_SYNTAX_ERROR);
        return p - 1;
    }

    if (c == CR) {
        if (!copts.multibyteaware || !COS_IsMultiByte(start, p - 1)) {
            if (countNewlines) {
                foundnl();
            }
            if (*p == LF) {
                p++;
            }
            CPrep_Error(ERR_UNEXPECTED_END_LINE);
            return p;
        }
        haveBackslash = FALSE;
        goto loop;
    }

    if (c == '?' && p[0] == '?' && copts.trigraphs) {
        switch (p[1]) {
            case '!':
            case '\'':
            case '(':
            case ')':
            case '-':
            case '<':
            case '=':
            case '>':
                p += 2;
                haveBackslash = FALSE;
                goto loop;
            case '/':
                p += 2;
                goto backslash;
        }
    }

    if (c == '\\' && !haveBackslash && (!copts.multibyteaware || !COS_IsMultiByte(start, p - 1))) {
backslash:
        if (*p == CR) {
            if (countNewlines) {
                foundnl();
            }
            if (*(++p) == LF) {
                p++;
            }
            haveBackslash = FALSE;
        } else {
            haveBackslash = TRUE;
        }
        goto loop;
    }

    if (c == ch && !haveBackslash) {
        return p;
    } else {
        haveBackslash = FALSE;
        goto loop;
    }
}

short prepcurchar(void) {
    char* save_pos;
    short t;

    save_pos = pos;
    t = prepnextchar();
    nextcharpos = pos;
    pos = save_pos;

    return t;
}

static short prepcurstringchar(StringPtr str) {
    char* save_pos;
    short t;

    save_pos = pos;
    prepnextstringchar_foundnl = FALSE;
    t = prepnextstringchar(str, FALSE);
    nextcharpos = pos;
    pos = save_pos;

    return t;
}

static void prepcurstringchar_skip(void) {
    pos = nextcharpos;
    if (prepnextstringchar_foundnl) {
        foundnl();
    }
}

char* ReadIdentifier(const char* p) {
    unsigned char buf[256];
    const char* start;
    int len;

    start = p;
    len = 0;
    for (;;) {
        if (cprep_idarray[(unsigned char)*p]) {
            if (len < 255) {
                buf[len++] = *p;
            }
            p++;
        } else {
            if (copts.multibyteaware) {
                if ((unsigned char)*p == '\\' && !COS_IsMultiByte(start, p) && p[1] == CR) {
                    if (p[2] == LF) {
                        p += 3;
                    } else {
                        p += 2;
                    }
                    foundnl();
                } else {
                    break;
                }
            } else {
                if ((unsigned char)*p == '\\' && p[1] == CR) {
                    if (p[2] == LF) {
                        p += 3;
                    } else {
                        p += 2;
                    }
                    foundnl();
                } else {
                    break;
                }
            }
        }
    }

    buf[len] = 0;
    tkidentifier = GetHashNameNodeExport((char*)buf);
    return (char*)p;
}

// NOTE: invented names: t, allowUnsigned
static short intsuffix(short t, Boolean allowUnsigned) {
    Boolean is_unsigned;
    Boolean is_long;
    Boolean is_longlong;

    is_unsigned = is_long = is_longlong = FALSE;
    switch (t) {
        case 'u':
        case 'U':
            is_unsigned = TRUE;
            pos = nextcharpos;
            t = prepcurchar();
            if (t == 'l' || t == 'L') {
                is_long = TRUE;
                pos = nextcharpos;
                t = *pos;
                if (copts.longlong) {
                    pos = nextcharpos;
                    t = prepcurchar();
                    if (t == 'l' || t == 'L') {
                        is_longlong = TRUE;
                        pos = nextcharpos;
                        t = *pos;
                    }
                }
            }
            break;
        case 'l':
        case 'L':
            is_long = TRUE;
            pos = nextcharpos;
            t = prepcurchar();
            if (t == 'u' || t == 'U') {
                is_unsigned = TRUE;
                pos = nextcharpos;
                t = *pos;
            } else if (copts.longlong && (t == 'l' || t == 'L')) {
                is_longlong = TRUE;
                pos = nextcharpos;
                t = prepcurchar();
                if (t == 'u' || t == 'U') {
                    is_unsigned = TRUE;
                    pos = nextcharpos;
                    t = *pos;
                }
            }
            break;
        case 'i':
        case 'I':
            if (copts.cpp_extensions && !copts.ANSIstrict && copts.longlong) {
                pos = nextcharpos;
                t = prepcurchar();
                if (t == '6') {
                    pos = nextcharpos;
                    t = prepcurchar();
                    if (t == '4') {
                        is_long = is_longlong = TRUE;
                        pos = nextcharpos;
                        t = *pos;
                    }
                }
            }
            break;
        case 'b':
        case 'B':
        case 'f':
        case 'F':
            if (InlineAsm_gccmode) {
                return IT_INT;
            }
            break;
    }

    if ((t >= 'a' && t <= 'z') || (t >= 'A' && t <= 'Z') || (t == '_') || (t >= '0' && t <= '9')) {
        CPrep_Error(ERR_ILLEGAL_TOKEN);
    }

    if (!is_longlong && !is_long) {
        if (!is_unsigned && CInt64_IsInRange(tkintconst, stsignedint.size)) {
            return IT_INT;
        }
        if ((allowUnsigned || is_unsigned) && CInt64_IsInURange(tkintconst, stunsignedint.size)) {
            return IT_UINT;
        }
    }
    if (!is_longlong) {
        if (!is_unsigned && CInt64_IsInRange(tkintconst, stsignedlong.size)) {
            return IT_LONG;
        }
        if (CInt64_IsInURange(tkintconst, stunsignedlong.size)) {
            return IT_ULONG;
        }
    }
    if (copts.longlong) {
        if (!is_unsigned && CInt64_IsInRange(tkintconst, stsignedlonglong.size)) {
            return IT_LONGLONG;
        }
        if (CInt64_IsInURange(tkintconst, stsignedlonglong.size)) {
            return IT_ULONGLONG;
        }
    }
    CError_Error(ERR_NUMBER_OUT_RANGE);
    tkintconst = cint64_zero;
    return IT_INT;
}

static short floatsuffix(short token) {
    short whichtype;

    switch (token) {
        case 'f':
        case 'F':
            pos = nextcharpos;
            token = *pos;
            whichtype = IT_FLOAT;
            if (stfloat.size != stlongdouble.size) {
                tkfloatconst = CMach_CalcFloatConvert(TYPE(&stfloat), tkfloatconst);
            }
            break;
        case 'l':
        case 'L':
            pos = nextcharpos;
            token = *pos;
            whichtype = IT_LONGDOUBLE;
            break;
        case 'd':
        case 'D':
            if (!copts.ANSIstrict) {
                pos = nextcharpos;
                token = *pos;
                whichtype = IT_DOUBLE;
                if (stdouble.size != stlongdouble.size) {
                    tkfloatconst = CMach_CalcFloatConvert(TYPE(&stdouble), tkfloatconst);
                }
            }
            break;
        default:
            if (copts.float_constants) {
                whichtype = IT_FLOAT;
                if (stfloat.size != stlongdouble.size) {
                    tkfloatconst = CMach_CalcFloatConvert(TYPE(&stfloat), tkfloatconst);
                }
            } else {
                whichtype = IT_DOUBLE;
                if (stdouble.size != stlongdouble.size) {
                    tkfloatconst = CMach_CalcFloatConvert(TYPE(&stdouble), tkfloatconst);
                }
            }
    }

    if ((token >= 'a' && token <= 'z') ||
        (token >= 'A' && token <= 'Z') ||
        (token == '_') ||
        (token >= '0' && token <= '9')) {
        CPrep_Error(ERR_ILLEGAL_TOKEN);
        whichtype = IT_DOUBLE;
    }

    return whichtype;
}

static short tohex(short token) {
    if (token >= '0' && token <= '9') {
        return token - '0';
    }
    if (token >= 'a' && token <= 'f') {
        return token - 'a' + 10;
    }
    if (token >= 'A' && token <= 'F') {
        return token - 'A' + 10;
    }
    return -1;
}

static SInt32 nextchar(StringPtr str) {
    short tmpchr;
    SInt32 chr_max;
    Boolean out_of_bounds;
    SInt32 chr;
    short i;
    short t;

    chr_max = 0xFF;
    if (widestring) {
        chr_max = 0xFFFF;
    }

    was_escchar = FALSE;
    t = prepnextstringchar(str, TRUE);

    if (t == '\\' && (!copts.multibyteaware || !COS_IsMultiByte(str, pos - 1))) {
        was_escchar = TRUE;
        switch ((t = prepnextstringchar(str, TRUE))) {
            case 'a':
                return 7;
            case 'b':
                return 8;
            case 't':
                return 9;
            case 'v':
                return 11;
            case 'f':
                return 12;
            case 'n':
                if (copts.mpwc_newline) {
                    return CR;
                } else {
                    return LF;
                }
            case 'r':
                if (copts.mpwc_newline) {
                    return LF;
                } else {
                    return CR;
                }
            case 'e':
                return 27;
            case 'x':
                chr = tohex(prepnextstringchar(str, TRUE));
                if (chr == -1) {
                    CPrep_Error(ERR_ILLEGAL_CHARACTER_CONSTANT);
                    return ' ';
                }
                out_of_bounds = FALSE;
                while (tohex((tmpchr = prepcurstringchar(str))) != -1) {
                    chr = (chr << 4) + tohex(tmpchr);
                    prepcurstringchar_skip();
                    if (chr & ~chr_max) {
                        out_of_bounds = TRUE;
                    }
                }
                if (out_of_bounds) {
                    CError_Error(ERR_ILLEGAL_CHARACTER_CONSTANT);
                }
                break;
            default:
                if (t >= '0' && t <= '7') {
                    out_of_bounds = FALSE;
                    i = 1;
                    chr = t - '0';
                    while (i < 3 && (tmpchr = prepcurstringchar(str)) >= '0' && tmpchr <= '7') {
                        chr = (chr << 3) + tmpchr - '0';
                        prepcurstringchar_skip();
                        if (chr & ~chr_max) {
                            out_of_bounds = TRUE;
                        }
                        i++;
                    }
                    if (out_of_bounds) {
                        CError_Error(ERR_ILLEGAL_CHARACTER_CONSTANT);
                    }
                } else {
                    chr = t;
                }
        }
    } else {
        chr = t;
    }

    if (chr_max == 0xFF) {
        return (char)(chr & chr_max);
    } else {
        return (chr & chr_max);
    }
}

StringPtr CPrep_SkipNewComment(StringPtr str) {
    StringPtr p;

    p = str;
    while (1) {
        switch (*(p++)) {
            case 0:
                if (tokenstacklevel > 0 || p >= (StringPtr)prep_file_end) {
                    CPrep_SkipNewCommentChar = 0;
                    return p - 1;
                }
                p[-1] = ' ';
                continue;

            case CR:
                CPrep_SkipNewCommentChar = CR;
                return p;

            case '\\':
                if (copts.multibyteaware && COS_IsMultiByte(str, p - 1)) {
                    continue;
                }

                if (*p == CR) {
                    ++p;
                    foundnl();
                }

                if (*p == LF) {
                    p++;
                }
                continue;

            case '"':
                if (InlineAsm_gccmode) {
                    CPrep_SkipNewCommentChar = CR;
                    return p - 1;
                }
                continue;
        }
    }
}

Boolean skipendoflinematch(StringPtr str, short token) {
    StringPtr start;

    start = str - 1;
    pos = (char*)start;

    while (1) {
        switch (*(str++)) {
            case 0:
            case CR:
                pos = (char*)str - 1;
                return FALSE;

            case '"':
                if (token == '"') {
                    pos = (char*)str;
                    return TRUE;
                }
                break;

            case '\'':
                if (token == '\'') {
                    pos = (char*)str;
                    return TRUE;
                }
                break;

            case '\\':
                if (copts.multibyteaware && COS_IsMultiByte(start, str - 1)) {
                    continue;
                }

                switch (*(str++)) {
                    case 0:
                        pos = (char*)str - 1;
                        return FALSE;

                    case CR:
                        foundnl();
                        if (*str == LF) {
                            str++;
                        }
                        break;
                }
                break;

            case '?':
                if (copts.trigraphs && str[0] == '?' && str[1] == '/' && str[2] == CR) {
                    foundnl();
                    str += 3;
                }
                break;
        }
    }
}

void skipendofline(void) {
    StringPtr start;
    StringPtr p;
    short ch;

    start = (StringPtr)pos;
    p = (StringPtr)pos;

    while (1) {
        switch (*(p++)) {
            case 0:
            case CR:
                pos = (char*)p - 1;
                return;

            case '"':
                if (!skipendoflinematch(p, '"')) {
                    return;
                }
                p = (StringPtr)pos;
                break;

            case '\'':
                if (!skipendoflinematch(p, '\'')) {
                    return;
                }
                p = (StringPtr)pos;
                break;

            case '/':
                if (copts.multibyteaware) {
                    while (*p == '\\' && !COS_IsMultiByte(start, p) && p[1] == CR) {
                        foundnl();
                        if (p[2] == LF) {
                            p += 3;
                        } else {
                            p += 2;
                        }
                    }
                } else {
                    while (*p == '\\' && p[1] == CR) {
                        foundnl();
                        if (p[2] == LF) {
                            p += 3;
                        } else {
                            p += 2;
                        }
                    }
                }

                if (*p == '/' && (!copts.ANSIstrict || copts.cplusplus)) {
                    // EOL comment
                    pos = (char*)CPrep_SkipNewComment(p + 1);
                    if (CPrep_SkipNewCommentChar == CR) {
                        pos--;
                    }
                    return;
                }

                if (*p == '*') {
                    // Block comment
                    pos = (char*)p - 1;
                    spaceskip = TRUE;
                    p++;
                    while (1) {
                        if ((ch = *(p++)) == 0) {
                            if (tokenstacklevel > 0 || p >= (StringPtr)prep_file_end) {
                                pos = (char*)p - 1;
                                CPrep_Error(ERR_UNTERMINATED_COMMENT);
                                return;
                            }
                            p[-1] = ' ';
                        }

                        if (ch == '*') {
                            if (copts.multibyteaware) {
                                while (*p == '\\' && !COS_IsMultiByte(start, p) && p[1] == CR) {
                                    foundnl();
                                    if (p[2] == LF) {
                                        p += 3;
                                    } else {
                                        p += 2;
                                    }
                                }
                            } else {
                                while (*p == '\\' && p[1] == CR) {
                                    foundnl();
                                    if (p[2] == LF) {
                                        p += 3;
                                    } else {
                                        p += 2;
                                    }
                                }
                            }

                            if (*p == '/') {
                                p++;
                                break;
                            }
                        }

                        if (ch == CR) {
                            foundnl();
                        }
                    }
                }
                break;

            case '\\':
                if (copts.multibyteaware && COS_IsMultiByte(start, p - 1)) {
                    continue;
                }

                if (*p == CR) {
                    foundnl();
                    if (p[1] == LF) {
                        p += 2;
                    } else {
                        p += 1;
                    }
                }
                break;

            case '?':
                if (copts.trigraphs && p[0] == '?' && p[1] == '/' && p[2] == CR) {
                    foundnl();
                    p += 3;
                }
                break;
        }
    }
}

// dead: nothing calls it
void CPrep_SkipAsmComment(void) {
    Boolean save_macrocheck = macrocheck;
    skipendofline();
    macrocheck = save_macrocheck;
}

static short tille(void) {
    return TK_ILLEGAL;
}

static short tcret(void) {
    newline();
    if (cprep_eoltokens) {
        return TK_EOL;
    } else {
        return 0;
    }
}

static short tapos(short _t) {
    SInt32 t;
    int t2;
    int i;
    StringPtr start;
    UInt32 maxvalue;
    int bytecount;
    CInt64 newBits;
    CInt64 val;

    start = (StringPtr)pos;
    t = nextchar((StringPtr)pos);
    if ((t == '\'' || t == 0 || t == CR) && !was_escchar) {
        CPrep_Error(ERR_ILLEGAL_CHARACTER_CONSTANT);
        tkintconst = cint64_zero;
        tksize = IT_INT;
        return TK_INTCONST;
    }

    t2 = nextchar(start);
    if (t2 == '\'' && !was_escchar) {
        if (widestring) {
            switch (stwchar.size) {
                case 1:
                    CInt64_SetULong(&tkintconst, t & 0xFF);
                    tksize = copts.cplusplus ? IT_SCHAR : IT_INT;
                    break;
                case 2:
                    CInt64_SetULong(&tkintconst, t & 0xFFFF);
                    tksize = IT_UINT;
                    break;
                case 4:
                    CInt64_SetULong(&tkintconst, (UInt32)t);
                    tksize = IT_UINT;
                    break;
                default:
                    CError_FATAL(1393);
            }
        } else {
            if (copts.unsigned_char) {
                t &= 0xFF;
            }
            CInt64_SetLong(&tkintconst, t);
            tksize = copts.cplusplus ? IT_SCHAR : IT_INT;
        }
    } else {
        if (widestring) {
            switch (stwchar.size) {
                case 1:
                    CInt64_SetULong(&val, 8);
                    maxvalue = 0xFF;
                    bytecount = 1;
                    break;
                case 2:
                    CInt64_SetULong(&val, 16);
                    maxvalue = 0xFFFF;
                    bytecount = 2;
                    break;
                case 4:
                    CInt64_SetULong(&val, 32);
                    maxvalue = 0xFFFFFFFF;
                    bytecount = 4;
                    break;
                default:
                    CError_FATAL(1431);
            }
        } else {
            CInt64_SetULong(&val, 8);
            maxvalue = 0xFF;
            bytecount = 1;
        }

        CInt64_SetULong(&tkintconst, t & maxvalue);
        i = bytecount;

        while (1) {
            if ((t2 == 0 || t2 == CR) && !was_escchar) {
                CPrep_Error(ERR_ILLEGAL_CHARACTER_CONSTANT);
                break;
            }

            CInt64_SetULong(&newBits, t2 & maxvalue);
            tkintconst = CInt64_Or(CInt64_Shl(tkintconst, val), newBits);
            i += bytecount;

            t2 = nextchar(start);
            if (t2 == '\'' && !was_escchar) {
                break;
            }

            if (i >= stunsignedlonglong.size) {
                CPrep_Error(ERR_ILLEGAL_CHARACTER_CONSTANT);
                break;
            }
        }

        if (i <= stunsignedint.size && stunsignedint.size < stunsignedlong.size) {
            tksize = IT_UINT;
        } else if (i <= stunsignedlong.size) {
            tksize = IT_ULONG;
        } else {
            tksize = IT_ULONGLONG;
        }
    }

    return TK_INTCONST;
}

static short tquot(short t) {
    Type* type;
    char* string_ptr;
    short c;
    StringPtr save_pos;
    SInt32 size;
    StringPtr start_pos;
    CInt64 ch64;

    start_pos = (StringPtr)pos;
    at_linestart = FALSE;
    if (cprep_nostring) {
        return '"';
    }

    string_ptr = *stringmem;
    ispascalstring = FALSE;
    size = 0;
    if (widestring) {
        type = TYPE(&stwchar);
    } else {
        type = TYPE(&stchar);
    }

    if (prepcurstringchar(start_pos) == '\\') {
        save_pos = (StringPtr)pos;
        prepcurstringchar_skip();
        if (prepcurstringchar(start_pos) == 'p') {
            prepcurstringchar_skip();
            ispascalstring = TRUE;
            string_ptr += type->size;
        } else {
            pos = (char*)save_pos;
        }
    }

    while (1) {
        c = nextchar(start_pos);
        if ((c == '"' || c == CR || c == 0) && !was_escchar) {
            if (c == 0 && (tokenstacklevel > 0 || pos >= prep_file_end)) {
                CPrep_Error(ERR_UNEXPECTED_END_FILE);
            } else if (c != '"') {
                CPrep_Error(ERR_ILLEGAL_STRING_CONSTANT);
            }
            break;
        }
        if ((size + type->size) >= maxstringsize) {
            SInt32 offset = string_ptr - *stringmem;
            if (!COS_ResizeHandle(stringmem, maxstringsize += 256)) {
                CError_NoMem();
            }
            string_ptr = *stringmem + offset;
        }
        if (type->size != 1) {
            CInt64_SetLong(&ch64, c);
            CMach_InitIntMem(type, ch64, string_ptr);
            string_ptr += type->size;
            size += type->size;
        } else {
            *string_ptr = c;
            string_ptr++;
            size++;
        }
    }

    if (ispascalstring) {
        if (size > 255 && type->size == 1) {
            CPrep_Error(ERR_STRING_TOO_LONG);
            size = 255;
        }
        CInt64_SetLong(&ch64, size / type->size);
        CMach_InitIntMem(type, ch64, *stringmem);
        size += type->size;
    } else {
        if ((size + 1) >= maxstringsize) {
            if (!COS_ResizeHandle(stringmem, maxstringsize += 256)) {
                CError_NoMem();
            }
        }
        CMach_InitIntMem(type, cint64_zero, *stringmem + size);
        size += type->size;
    }

    tksize = size;
    tkstring = galloc(size);
    memcpy(tkstring, *stringmem, tksize);

    if (widestring) {
        c = TK_STRING_WIDE;
    } else {
        c = TK_STRING;
    }
    return (short)c;
}

static short thash(void) {
    if (at_linestart) {
        preprocessor();
        return 0;
    }
    return '#';
}

static short tmult(void) {
    at_linestart = FALSE;
    if (prepcurchar() == '=') {
        pos = nextcharpos;
        return TK_MULT_ASSIGN;
    }
    return '*';
}

static short tcolo(void) {
    char t2;
    at_linestart = FALSE;
    if (copts.cplusplus) {
        t2 = prepcurchar();
        if (t2 == ':') {
            pos = nextcharpos;
            return TK_COLON_COLON;
        }
        if (t2 == '>' && !preprocessing_only) {
            pos = nextcharpos;
            return ']';
        }
    }
    return ':';
}

static short tless(short t) {
    at_linestart = FALSE;
    if ((t = prepcurchar()) == '=') {
        pos = nextcharpos;
        return TK_LESS_EQUAL;
    }
    if (t == '<') {
        pos = nextcharpos;
        if (prepcurchar() == '=') {
            pos = nextcharpos;
            return TK_SHL_ASSIGN;
        } else {
            return TK_SHL;
        }
    }
    if (t == '%' && copts.cplusplus && !preprocessing_only) {
        pos = nextcharpos;
        return '{';
    }
    if (t == ':' && copts.cplusplus && !preprocessing_only) {
        pos = nextcharpos;
        return '[';
    }
    return '<';
}

static short tequa(short t) {
    at_linestart = FALSE;
    if (prepcurchar() == '=') {
        pos = nextcharpos;
        return TK_LOGICAL_EQ;
    }
    return '=';
}

static short tgrea(short t) {
    at_linestart = FALSE;
    if ((t = prepcurchar()) == '=') {
        pos = nextcharpos;
        return TK_GREATER_EQUAL;
    }
    if (t == '>') {
        pos = nextcharpos;
        if (prepcurchar() == '=') {
            pos = nextcharpos;
            return TK_SHR_ASSIGN;
        } else {
            return TK_SHR;
        }
    }
    return '>';
}

static short tatsg(short t) {
    char save_idarray;
    char* start_pos = pos;

    at_linestart = FALSE;

    if (copts.objective_c || in_assembler) {
        save_idarray = cprep_idarray['@'];
        cprep_idarray['@'] = 1;
        pos = ReadIdentifier(pos - 1);
        cprep_idarray['@'] = save_idarray;

        if (copts.objective_c) {
            if (!strcmp("@interface", tkidentifier->name)) {
                return TK_AT_INTERFACE;
            }
            if (!strcmp("@implementation", tkidentifier->name)) {
                return TK_AT_IMPLEMENTATION;
            }
            if (!strcmp("@protocol", tkidentifier->name)) {
                return TK_AT_PROTOCOL;
            }
            if (!strcmp("@end", tkidentifier->name)) {
                return TK_AT_END;
            }
            if (!strcmp("@private", tkidentifier->name)) {
                return TK_AT_PRIVATE;
            }
            if (!strcmp("@protected", tkidentifier->name)) {
                return TK_AT_PROTECTED;
            }
            if (!strcmp("@public", tkidentifier->name)) {
                return TK_AT_PUBLIC;
            }
            if (!strcmp("@class", tkidentifier->name)) {
                return TK_AT_CLASS;
            }
            if (!strcmp("@selector", tkidentifier->name)) {
                return TK_AT_SELECTOR;
            }
            if (!strcmp("@encode", tkidentifier->name)) {
                return TK_AT_ENCODE;
            }
            if (!strcmp("@defs", tkidentifier->name)) {
                return TK_AT_DEFS;
            }
        }

        if (in_assembler) {
            return TK_IDENTIFIER;
        }

        pos = start_pos;
    }

    return '@';
}

static short tperc(short t) {
    char t2;
    at_linestart = FALSE;
    t2 = prepcurchar();
    if (t2 == '=') {
        pos = nextcharpos;
        return TK_MOD_ASSIGN;
    }
    if (t2 == '>' && copts.cplusplus && !preprocessing_only) {
        pos = nextcharpos;
        return '}';
    }
    return '%';
}

static short texcl(void) {
    at_linestart = FALSE;
    if (prepcurchar() == '=') {
        pos = nextcharpos;
        return TK_LOGICAL_NE;
    }
    return '!';
}

static short tplus(short t) {
    at_linestart = FALSE;
    if ((t = prepcurchar()) == '=') {
        pos = nextcharpos;
        return TK_ADD_ASSIGN;
    }
    if (t == '+') {
        pos = nextcharpos;
        return TK_INCREMENT;
    }
    return '+';
}

static short tminu(short t) {
    at_linestart = FALSE;
    if ((t = prepcurchar()) == '=') {
        pos = nextcharpos;
        return TK_SUB_ASSIGN;
    }
    if (t == '-') {
        pos = nextcharpos;
        return TK_DECREMENT;
    }
    if (t == '>') {
        pos = nextcharpos;
        if (copts.cplusplus && prepcurchar() == '*') {
            pos = nextcharpos;
            return TK_ARROW_STAR;
        }
        return TK_ARROW;
    }
    return '-';
}

static short torrr(short t) {
    at_linestart = FALSE;
    if ((t = prepcurchar()) == '=') {
        pos = nextcharpos;
        return TK_OR_ASSIGN;
    }
    if (t == '|') {
        pos = nextcharpos;
        return TK_LOGICAL_OR;
    }
    return '|';
}

static short tampe(short t) {
    at_linestart = FALSE;
    if ((t = prepcurchar()) == '=') {
        pos = nextcharpos;
        return TK_AND_ASSIGN;
    }
    if (t == '&') {
        pos = nextcharpos;
        return TK_LOGICAL_AND;
    }
    return '&';
}

static short tpowe(short t) {
    at_linestart = FALSE;
    if ((t = prepcurchar()) == '=') {
        pos = nextcharpos;
        return TK_XOR_ASSIGN;
    }
    return '^';
}

static short tdivi(short t) {
    at_linestart = FALSE;
    if ((t = prepcurchar()) == '=') {
        pos = nextcharpos;
        return TK_DIV_ASSIGN;
    }
    return '/';
}

static short tnumb(short t);

static short tzero(short t) {
    char* p;
    Boolean failed;
    Boolean floatFailed;

    if (InlineAsm_gccmode && InlineAsm_labelref) {
        return tnumb(t);
    }

    at_linestart = FALSE;

    p = pos - 1;
    t = prepnextchar();
    if (t == 'x' || t == 'X') {
        p = CInt64_ScanHexString(&tkintconst, pos, &failed);
        if (pos == p) {
            CError_Error(ERR_ILLEGAL_TOKEN);
        }
        pos = p;
    } else if (!copts.ANSIstrict && (t == 'b' || t == 'B')) {
        p = CInt64_ScanBinString(&tkintconst, pos, &failed);
        if (pos == p) {
            CError_Error(ERR_ILLEGAL_TOKEN);
        }
        pos = p;
    } else {
        while (t >= '0' && t <= '9') {
            t = prepnextchar();
        }

        switch (t) {
            case '.':
            case 'E':
            case 'e':
                pos = CMach_FloatScan(p, &tkfloatconst, &floatFailed);
                if (floatFailed) {
                    CPrep_Error(ERR_NUMBER_OUT_RANGE);
                }
                tksize = floatsuffix(prepcurchar());
                return TK_FLOATCONST;
        }

        pos = CInt64_ScanOctString(&tkintconst, p, &failed);
    }

    if (failed) {
        CPrep_Error(ERR_NUMBER_OUT_RANGE);
        tkintconst = cint64_zero;
    }

    tksize = intsuffix(prepcurchar(), TRUE);
    return TK_INTCONST;
}

static short tpoin(short t) {
    char* p;
    Boolean failed;

    at_linestart = FALSE;

    p = pos - 1;
    t = prepnextchar();

    if (t >= '0' && t <= '9') {
        pos = CMach_FloatScan(p, &tkfloatconst, &failed);
        if (failed) {
            CPrep_Error(ERR_NUMBER_OUT_RANGE);
        }
        tksize = floatsuffix(prepcurchar());
        return TK_FLOATCONST;
    }

    if (copts.cplusplus && t == '*') {
        return TK_DOT_STAR;
    }

    if (t == '.' && prepnextchar() == '.') {
        return TK_ELLIPSIS;
    }

    pos = p + 1;
    return '.';
}

static short tnumb(short t) {
    char* p;
    short ch;
    Boolean failed;
    Boolean floatFailed;

    at_linestart = FALSE;

    p = pos - 1;
    pos = CInt64_ScanDecString(&tkintconst, p, &failed);

    ch = prepcurchar();
    if (ch == '.' || ch == 'e' || ch == 'E') {
        pos = CMach_FloatScan(p, &tkfloatconst, &floatFailed);
        if (floatFailed) {
            CPrep_Error(ERR_NUMBER_OUT_RANGE);
        }
        tksize = floatsuffix(prepcurchar());
        return TK_FLOATCONST;
    }

    if (failed) {
        CPrep_Error(ERR_NUMBER_OUT_RANGE);
        tkintconst = cint64_zero;
    }

    tksize = intsuffix(prepcurchar(), FALSE);
    return TK_INTCONST;
}

static short tiden(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    } else {
        return TK_IDENTIFIER;
    }
}

static short tchrL(short t) {
    at_linestart = FALSE;

    if ((t = prepcurchar()) == '\'') {
        widestring = TRUE;
        pos = nextcharpos;
        t = tapos(t);
        widestring = FALSE;

        tksize = (copts.wchar_type && copts.cplusplus) ? IT_WCHAR_T : IT_USHORT;
        return t;
    }

    if (t == '"') {
        widestring = TRUE;
        pos = nextcharpos;
        t = tquot(t);
        widestring = FALSE;
        return t;
    }

    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }

    return TK_IDENTIFIER;
}

static short tchra(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("auto", tkidentifier->name)) {
        return TK_AUTO;
    }
    if ((copts.cplusplus || !copts.onlystdkeywords) && !strcmp("asm", tkidentifier->name)) {
        return TK_ASM;
    }
    if (copts.cplusplus && !in_assembler && !preprocessing_only) {
        if (!strcmp("and", tkidentifier->name)) {
            return TK_LOGICAL_AND;
        }
        if (!strcmp("and_eq", tkidentifier->name)) {
            return TK_AND_ASSIGN;
        }
    }
    return TK_IDENTIFIER;
}

static short tchrb(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("break", tkidentifier->name)) {
        return TK_BREAK;
    }
    if (copts.cplusplus) {
        if (copts.booltruefalse && !strcmp("bool", tkidentifier->name)) {
            return TK_BOOL;
        }
        if (!in_assembler && !preprocessing_only) {
            if (!strcmp("bitand", tkidentifier->name)) {
                return '&';
            }
            if (!strcmp("bitor", tkidentifier->name)) {
                return '|';
            }
        }
    }
    if (copts.objective_c) {
        if (!strcmp("bycopy", tkidentifier->name)) {
            return TK_BYCOPY;
        }
        if (!strcmp("byref", tkidentifier->name)) {
            return TK_BYREF;
        }
    }
    return TK_IDENTIFIER;
}

static short tchrc(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("case", tkidentifier->name)) {
        return TK_CASE;
    }
    if (!strcmp("char", tkidentifier->name)) {
        return TK_CHAR;
    }
    if (!strcmp("const", tkidentifier->name)) {
        return TK_CONST;
    }
    if (!strcmp("continue", tkidentifier->name)) {
        return TK_CONTINUE;
    }
    if (copts.cplusplus) {
        if (!strcmp("const_cast", tkidentifier->name)) {
            if (copts.ecplusplus) {
                CPrep_Warning(ERR_ILLEGAL_USE_C_FEATURE_EC);
            }
            return TK_CONST_CAST;
        }
        if (!strcmp("catch", tkidentifier->name)) {
            if (copts.ecplusplus) {
                CPrep_Warning(ERR_ILLEGAL_USE_C_FEATURE_EC);
            }
            return TK_CATCH;
        }
        if (!strcmp("class", tkidentifier->name)) {
            return TK_CLASS;
        }
        if (!in_assembler && !preprocessing_only) {
            if (!strcmp("compl", tkidentifier->name)) {
                return '~';
            }
        }
    }
    return TK_IDENTIFIER;
}

static short tchrd(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("default", tkidentifier->name)) {
        return TK_DEFAULT;
    }
    if (!strcmp("do", tkidentifier->name)) {
        return TK_DO;
    }
    if (!strcmp("double", tkidentifier->name)) {
        return TK_DOUBLE;
    }
    if (copts.cplusplus) {
        if (!strcmp("delete", tkidentifier->name)) {
            return TK_DELETE;
        }
        if (!strcmp("dynamic_cast", tkidentifier->name)) {
            if (copts.ecplusplus) {
                CPrep_Warning(ERR_ILLEGAL_USE_C_FEATURE_EC);
            }
            return TK_DYNAMIC_CAST;
        }
    }
    return TK_IDENTIFIER;
}

static short tchre(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("else", tkidentifier->name)) {
        return TK_ELSE;
    }
    if (!strcmp("enum", tkidentifier->name)) {
        return TK_ENUM;
    }
    if (!strcmp("extern", tkidentifier->name)) {
        return TK_EXTERN;
    }
    if (copts.cplusplus) {
        if (!strcmp("explicit", tkidentifier->name)) {
            return TK_EXPLICIT;
        }
        if (!strcmp("export", tkidentifier->name) && !copts.ecplusplus) {
            return TK_EXPORT;
        }
    }
    return TK_IDENTIFIER;
}

static short tchrf(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("float", tkidentifier->name)) {
        return TK_FLOAT;
    }
    if (!strcmp("for", tkidentifier->name)) {
        return TK_FOR;
    }
    if (copts.cplusplus) {
        if (!strcmp("friend", tkidentifier->name)) {
            return TK_FRIEND;
        }
        if (copts.booltruefalse && !strcmp("false", tkidentifier->name)) {
            return TK_FALSE;
        }
    }
    return TK_IDENTIFIER;
}

static short tchrg(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("goto", tkidentifier->name)) {
        return TK_GOTO;
    }
    return TK_IDENTIFIER;
}

static short tchri(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("if", tkidentifier->name)) {
        return TK_IF;
    }
    if (!strcmp("int", tkidentifier->name)) {
        return TK_INT;
    }
    if (copts.cplusplus || copts.c9x) {
        if (!strcmp("inline", tkidentifier->name)) {
            return TK_INLINE;
        }
    } else if (!copts.onlystdkeywords) {
        if (!strcmp("inline", tkidentifier->name)) {
            return TK_INLINE;
        }
    }
    if (copts.objective_c) {
        if (!strcmp("in", tkidentifier->name)) {
            return TK_IN;
        }
        if (!strcmp("inout", tkidentifier->name)) {
            return TK_INOUT;
        }
    }
    return TK_IDENTIFIER;
}

static short tchrl(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("long", tkidentifier->name)) {
        return TK_LONG;
    }
    return TK_IDENTIFIER;
}

static short tchrm(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (copts.cplusplus && !strcmp("mutable", tkidentifier->name)) {
        if (copts.ecplusplus) {
            CPrep_Warning(ERR_ILLEGAL_USE_C_FEATURE_EC);
        }
        return TK_MUTABLE;
    }
    return TK_IDENTIFIER;
}

static short tchrn(short t) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (copts.cplusplus && t == 'n') {
        if (!strcmp("new", tkidentifier->name)) {
            return TK_NEW;
        }
        if (!strcmp("namespace", tkidentifier->name)) {
            if (copts.ecplusplus) {
                CPrep_Warning(ERR_ILLEGAL_USE_C_FEATURE_EC);
            }
            return TK_NAMESPACE;
        }
        if (!in_assembler && !preprocessing_only) {
            if (!strcmp("not", tkidentifier->name)) {
                return '!';
            }
            if (!strcmp("not_eq", tkidentifier->name)) {
                return TK_LOGICAL_NE;
            }
        }
    }
    return TK_IDENTIFIER;
}

static short tchro(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (copts.cplusplus) {
        if (!strcmp("operator", tkidentifier->name)) {
            return TK_OPERATOR;
        }
        if (!in_assembler && !preprocessing_only) {
            if (!strcmp("or", tkidentifier->name)) {
                return TK_LOGICAL_OR;
            }
            if (!strcmp("or_eq", tkidentifier->name)) {
                return TK_OR_ASSIGN;
            }
        }
    }
    if (copts.objective_c) {
        if (!strcmp("out", tkidentifier->name)) {
            return TK_OUT;
        }
        if (!strcmp("oneway", tkidentifier->name)) {
            return TK_ONEWAY;
        }
    }
    return TK_IDENTIFIER;
}

static short tchrp(short t) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!copts.onlystdkeywords && !strcmp("pascal", tkidentifier->name)) {
        return TK_PASCAL;
    }
    if (copts.cplusplus && t == 'p') {
        if (!strcmp("private", tkidentifier->name)) {
            return TK_PRIVATE;
        }
        if (!strcmp("protected", tkidentifier->name)) {
            return TK_PROTECTED;
        }
        if (!strcmp("public", tkidentifier->name)) {
            return TK_PUBLIC;
        }
    }
    return TK_IDENTIFIER;
}

static short tchrr(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("register", tkidentifier->name)) {
        return TK_REGISTER;
    }
    if (!strcmp("return", tkidentifier->name)) {
        return TK_RETURN;
    }
    if (copts.cplusplus && !strcmp("reinterpret_cast", tkidentifier->name)) {
        if (copts.ecplusplus) {
            CPrep_Warning(ERR_ILLEGAL_USE_C_FEATURE_EC);
        }
        return TK_REINTERPRET_CAST;
    }
    if (copts.c9x && !strcmp("restrict", tkidentifier->name)) {
        return TK_RESTRICT;
    }
    return TK_IDENTIFIER;
}

static short tchrs(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("short", tkidentifier->name)) {
        return TK_SHORT;
    }
    if (!strcmp("signed", tkidentifier->name)) {
        return TK_SIGNED;
    }
    if (!strcmp("sizeof", tkidentifier->name)) {
        return TK_SIZEOF;
    }
    if (!strcmp("static", tkidentifier->name)) {
        return TK_STATIC;
    }
    if (!strcmp("struct", tkidentifier->name)) {
        return TK_STRUCT;
    }
    if (!strcmp("switch", tkidentifier->name)) {
        return TK_SWITCH;
    }
    if (copts.cplusplus && !strcmp("static_cast", tkidentifier->name)) {
        if (copts.ecplusplus) {
            CPrep_Warning(ERR_ILLEGAL_USE_C_FEATURE_EC);
        }
        return TK_STATIC_CAST;
    }
    return TK_IDENTIFIER;
}

static short tchrt(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("typedef", tkidentifier->name)) {
        return TK_TYPEDEF;
    }
    if (copts.gcc_extensions && !strcmp("typeof", tkidentifier->name)) {
        return TK_UU_TYPEOF_UU;
    }
    if (copts.cplusplus) {
        if (!strcmp("this", tkidentifier->name)) {
            return TK_THIS;
        }
        if (copts.booltruefalse && !strcmp("true", tkidentifier->name)) {
            return TK_TRUE;
        }
        if (!strcmp("template", tkidentifier->name)) {
            if (copts.ecplusplus) {
                CPrep_Warning(ERR_ILLEGAL_USE_C_FEATURE_EC);
            }
            return TK_TEMPLATE;
        }
        if (!strcmp("try", tkidentifier->name)) {
            if (copts.ecplusplus) {
                CPrep_Warning(ERR_ILLEGAL_USE_C_FEATURE_EC);
            }
            return TK_TRY;
        }
        if (!strcmp("throw", tkidentifier->name)) {
            if (copts.ecplusplus) {
                CPrep_Warning(ERR_ILLEGAL_USE_C_FEATURE_EC);
            }
            return TK_THROW;
        }
        if (!strcmp("typeid", tkidentifier->name)) {
            if (copts.ecplusplus) {
                CPrep_Warning(ERR_ILLEGAL_USE_C_FEATURE_EC);
            }
            return TK_TYPEID;
        }
        if (!strcmp("typename", tkidentifier->name)) {
            if (copts.ecplusplus) {
                CPrep_Warning(ERR_ILLEGAL_USE_C_FEATURE_EC);
            }
            return TK_TYPENAME;
        }
    }
    return TK_IDENTIFIER;
}

static short tchru(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("union", tkidentifier->name)) {
        return TK_UNION;
    }
    if (!strcmp("unsigned", tkidentifier->name)) {
        return TK_UNSIGNED;
    }
    if (copts.cplusplus && !strcmp("using", tkidentifier->name)) {
        if (copts.ecplusplus) {
            CPrep_Warning(ERR_ILLEGAL_USE_C_FEATURE_EC);
        }
        return TK_USING;
    }
    return TK_IDENTIFIER;
}

static short tchrv(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("void", tkidentifier->name)) {
        return TK_VOID;
    }
    if (!strcmp("volatile", tkidentifier->name)) {
        return TK_VOLATILE;
    }
    if (copts.cplusplus && !strcmp("virtual", tkidentifier->name)) {
        return TK_VIRTUAL;
    }
    return TK_IDENTIFIER;
}

static short tchrw(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }
    if (!strcmp("while", tkidentifier->name)) {
        return TK_WHILE;
    }
    if (copts.cplusplus && copts.wchar_type && !strcmp("wchar_t", tkidentifier->name)) {
        return TK_WCHAR_T;
    }
    return TK_IDENTIFIER;
}

static short tchrx(short t) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }

    if (copts.cplusplus && t == 'x' && !in_assembler && !preprocessing_only) {
        if (!strcmp("xor", tkidentifier->name)) {
            return '^';
        }
        if (!strcmp("xor_eq", tkidentifier->name)) {
            return TK_XOR_ASSIGN;
        }
    }

    return TK_IDENTIFIER;
}

static short tchr_(void) {
    at_linestart = FALSE;
    pos = ReadIdentifier(pos - 1);
    if (macrocheck && macrotest()) {
        return 0;
    }

    if (!strcmp("__stdcall", tkidentifier->name)) {
        return TK_UU_STDCALL;
    }
    if (!strcmp("__cdecl", tkidentifier->name)) {
        return TK_UU_CDECL;
    }
    if (!strcmp("__fastcall", tkidentifier->name)) {
        return TK_UU_FASTCALL;
    }
    if (!strcmp("__declspec", tkidentifier->name)) {
        return TK_UU_DECLSPEC;
    }
    if (!strcmp("__asm", tkidentifier->name)) {
        return TK_ASM;
    }
    if (!strcmp("__asm__", tkidentifier->name)) {
        return TK_ASM;
    }
    if (!strcmp("__inline", tkidentifier->name)) {
        return TK_INLINE;
    }
    if (!strcmp("__inline__", tkidentifier->name)) {
        return TK_INLINE;
    }
    if (!strcmp("__restrict", tkidentifier->name)) {
        return TK_RESTRICT;
    }
    if (!strcmp("__restrict__", tkidentifier->name)) {
        return TK_RESTRICT;
    }
    if (!strcmp("__attribute__", tkidentifier->name)) {
        return TK_UU_ATTRIBUTE_UU;
    }
    if (!strcmp("__far", tkidentifier->name)) {
        return TK_UU_FAR;
    }
    if (!strcmp("__alignof__", tkidentifier->name)) {
        return TK_UU_ALIGNOF_UU;
    }
    if (!strcmp("__typeof__", tkidentifier->name)) {
        return TK_UU_TYPEOF_UU;
    }
    if (copts.c9x) {
        if (!strcmp("_Bool", tkidentifier->name)) {
            return TK_BOOL;
        }
        if (!strcmp("_Complex", tkidentifier->name)) {
            return TK_U_COMPLEX;
        }
        if (!strcmp("_Imaginary", tkidentifier->name)) {
            return TK_U_IMAGINARY;
        }
    }
    // (Gekko's paired-single vector type)
    if (!strcmp("__vec2x32float__", tkidentifier->name)) {
        return TK_UU_VEC2X32FLOAT_UU;
    }
    if (copts.altivec_model && !strcmp("__vector", tkidentifier->name)) {
        return TK_UU_VECTOR;
    }
    return TK_IDENTIFIER;
}

static short token(short t) {
    at_linestart = FALSE;
    return t;
}

static short tdoll(short t) {
    at_linestart = FALSE;
    return t;
}

static short tisid(void) {
    if (!tokenstacklevel) {
        CError_Error(ERR_ILLEGAL_TOKEN);
    }
    return 0;
}

static short tnull(short t) {
    if (tokenstacklevel > 0 || pos >= prep_file_end) {
        at_linestart = FALSE;
        return t;
    } else {
        pos[-1] = -64;
        return TK_ILLEGAL;
    }
}

static short t0x1a(void) {
    if (*pos == 0) {
        return 0;
    } else {
        return TK_ILLEGAL;
    }
}

typedef short (*TokenizePtr)(short);
#define F(func) ((TokenizePtr)(&func))

// clang-format off
static TokenizePtr cprep_tokenize[256] = {
        F(tnull), F(tille), F(tille), F(tille), F(tisid), F(tisid), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tcret), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(t0x1a), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(texcl), F(tquot), F(thash), F(tdoll), F(tperc), F(tampe), F(tapos),
        F(token), F(token), F(tmult), F(tplus), F(token), F(tminu), F(tpoin), F(tdivi),
        F(tzero), F(tnumb), F(tnumb), F(tnumb), F(tnumb), F(tnumb), F(tnumb), F(tnumb),
        F(tnumb), F(tnumb), F(tcolo), F(token), F(tless), F(tequa), F(tgrea), F(token),
        F(tatsg), F(tiden), F(tiden), F(tiden), F(tiden), F(tiden), F(tiden), F(tiden),
        F(tiden), F(tiden), F(tiden), F(tiden), F(tchrL), F(tiden), F(tchrn), F(tiden),
        F(tchrp), F(tiden), F(tiden), F(tiden), F(tiden), F(tiden), F(tiden), F(tiden),
        F(tchrx), F(tiden), F(tiden), F(token), F(token), F(token), F(tpowe), F(tchr_),
        F(token), F(tchra), F(tchrb), F(tchrc), F(tchrd), F(tchre), F(tchrf), F(tchrg),
        F(tiden), F(tchri), F(tiden), F(tiden), F(tchrl), F(tchrm), F(tchrn), F(tchro),
        F(tchrp), F(tiden), F(tchrr), F(tchrs), F(tchrt), F(tchru), F(tchrv), F(tchrw),
        F(tchrx), F(tiden), F(tiden), F(token), F(torrr), F(token), F(token), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille),
        F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille), F(tille)
};
// clang-format on

short lookahead(void) {
    SInt32 state;
    short t;

    CPrep_TokenStreamGetState(&state);
    t = lex();
    CPrep_TokenStreamSetState(&state);

    return t;
}

short lookahead_noeol(void) {
    SInt32 state;
    short t;

    CPrep_TokenStreamGetState(&state);
    do {
        t = lex();
    } while (t == TK_EOL);
    CPrep_TokenStreamSetState(&state);

    return t;
}

typedef struct StringChunk {
    struct StringChunk* next;
    char* data;
    SInt32 offset;
    SInt32 size;
} StringChunk;

static void CPrep_StringConCat(Boolean isWide) { // NOTE: invented name: isWide
    StringChunk* chunks;
    StringChunk* chunk;
    char* buffer;
    char* saveString;
    SInt32 saveSize;
    SInt32 offset;
    Boolean savePascalString;
    TStreamElement* elem;
    short ch;

    saveString = tkstring;
    saveSize = tksize;
    savePascalString = ispascalstring;

    cprep_strconcat = TRUE;

    chunks = NULL;
    offset = tksize;

    while (1) {
        ch = lookahead_noeol();
        if (ch != TK_STRING && ch != TK_STRING_WIDE) {
            ispascalstring = savePascalString;
            if (chunks) {
                buffer = galloc(tksize = offset);
                tkstring = (char*)buffer;

                memcpy(buffer, saveString, saveSize);
                for (chunk = chunks; chunk; chunk = chunk->next) {
                    memcpy(buffer + chunk->offset, chunk->data, chunk->size);
                }

                if (savePascalString) {
                    if (offset > 256) {
                        CError_Error(ERR_STRING_TOO_LONG);
                        offset = 256;
                    }
                    buffer[0] = offset - 1;
                } else if (isWide) {
                    CMach_InitIntMem(TYPE(&stwchar), cint64_zero, buffer + offset - stwchar.size);
                } else {
                    CMach_InitIntMem(TYPE(&stchar), cint64_zero, buffer + offset - stchar.size);
                }

                elem = ts_current - 1;
                elem->data.tkstring.data = buffer;
                elem->data.tkstring.size = offset;
                elem->subtype = ispascalstring;
                cprep_strconcat = FALSE;
                return;
            }
            tkstring = saveString;
            tksize = saveSize;
            cprep_strconcat = FALSE;
            return;
        }

        do {
            ch = lex();
            ts_current--;
            memmove(ts_current, ts_current + 1, sizeof(TStreamElement) * ts_preread_elements);
        } while (ch == TK_EOL);

        chunk = lalloc(sizeof(StringChunk));
        chunk->next = chunks;
        chunk->data = tkstring;
        chunk->size = tksize - 1;
        chunk->offset = offset - 1;
        chunks = chunk;

        if (ispascalstring) {
            chunk->data++;
        }
        if (savePascalString) {
            chunk->offset++;
        }

        if (ch == TK_STRING_WIDE) {
            chunk->size--;
        }
        if (isWide) {
            chunk->offset--;
        }

        offset += chunk->size;
    }
}

short lex(void) {
    short t;

    while (1) {
        if (ts_preread_elements <= 0) {
            break;
        }

        t = ts_current->tokentype;
        if (t < 0) {
            switch (t) {
                case TK_INTCONST:
                    tkintconst = ts_current->data.tkintconst;
                    tksize = ts_current->subtype;
                    break;
                case TK_FLOATCONST:
                    tkfloatconst = ts_current->data.tkfloatconst;
                    tksize = ts_current->subtype;
                    break;
                case TK_IDENTIFIER:
                    tkidentifier = ts_current->data.tkidentifier;
                    break;
                case TK_STRING:
                case TK_STRING_WIDE:
                    tkstring = ts_current->data.tkstring.data;
                    tksize = ts_current->data.tkstring.size;
                    ispascalstring = ts_current->subtype;
                    break;
                case TK_EOL:
                    nlflag = TRUE;
                    if (!cprep_eoltokens) {
                        ts_current++;
                        ts_preread_elements--;
                        continue;
                    }
                    break;
                default:
                    CError_FATAL(3022);
            }
        }

        ts_current++;
        ts_preread_elements--;
        return t;
    }

    while (1) {
        t = prepskipnextchar();

        ts_current->tokenfile = prep_file;
        if (tokenstacklevel > 0) {
            ts_current->tokenoffset = tokenstack[0].pos - prep_file_start;
            macropos = pos;
        } else {
            ts_current->tokenoffset = pos - prep_file_start;
        }
        ts_current->tokenline = linenumber;
        pos = nextcharpos;

        t = cprep_tokenize[t](t);
        if (t != 0) {
            if (ts_current >= ts_last) {
                CPrep_TSBufferGrow(1024);
            }
            ts_current->tokentype = t;

            if ((t = ts_current->tokentype) < 0) {
                switch (t) {
                    case TK_INTCONST:
                        ts_current->data.tkintconst = tkintconst;
                        ts_current->subtype = tksize;
                        break;
                    case TK_FLOATCONST:
                        ts_current->data.tkfloatconst = tkfloatconst;
                        ts_current->subtype = tksize;
                        break;
                    case TK_IDENTIFIER:
                        ts_current->data.tkidentifier = tkidentifier;
                        break;
                    case TK_STRING:
                    case TK_STRING_WIDE:
                        ts_current->data.tkstring.data = tkstring;
                        ts_current->data.tkstring.size = tksize;
                        ts_current->subtype = ispascalstring;
                        if (!cprep_strconcat) {
                            ts_current++;
                            CPrep_StringConCat(t == TK_STRING_WIDE);
                            return t;
                        }
                        break;
                    case TK_ILLEGAL:
                        // (the illegal token is reported and dropped, not returned)
                        ts_current++;
                        CError_Error(ERR_ILLEGAL_TOKEN);
                        ts_current--;
                        goto skipped;
                }
            }

            ts_current++;
            return t;
        }

skipped:
        if (*pos == 0) {
            if (tokenstacklevel > 0) {
                poptokenseq();
            } else if (tokenstacklevel > 0 || pos >= prep_file_end) {
                if (filesp > 0) {
                    popfile();
                } else {
                    return 0;
                }
            } else {
                ts_current++;
                CError_ErrorTerm(ERR_ILLEGAL_TOKEN);
            }
        }
    }
}

short plex(void) {
    short t;

    while (1) {
        t = prepskipnextchar();
        pos = nextcharpos;
        if ((t = cprep_tokenize[t](t))) {
            return t;
        }

        if (!*pos) {
            if (tokenstacklevel > 0) {
                poptokenseq();
            } else if (filesp > 0) {
                popfile();
            } else {
                break;
            }
        }
    }

    return 0;
}

short lexidentifier(void) {
    short t;
    char* save_pos;

    t = prepskipnextchar();
    pos = nextcharpos;
    at_linestart = FALSE;
    if ((t >= 'a' && t <= 'z') || (t >= 'A' && t <= 'Z') || (t == '_')) {
        pos = ReadIdentifier(pos - 1);
        return TK_IDENTIFIER;
    } else if (t == '.') {
        save_pos = pos;
        if (prepnextchar() == '.' && prepnextchar() == '.') {
            return TK_ELLIPSIS;
        } else {
            pos = save_pos;
        }
    }
    return t;
}
