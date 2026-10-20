#include "compiler/Unmangle.h"
#include <setjmp.h>
#include <string.h>

typedef struct UnmangleBuffer {
    char* buf;
    size_t remaining;
} UnmangleBuffer;

static const char* Unmangle_Type(jmp_buf errorbuf, UnmangleBuffer* ub, const char* p);

static void Unmangle_Error(jmp_buf errorbuf) {
    longjmp(errorbuf, 1);
}

static void Unmangle_BufferInit(UnmangleBuffer* ub, char* buf, size_t size) {
    ub->buf = buf;
    ub->remaining = size - 1;
}

static void Unmangle_BufferAppendChar(UnmangleBuffer* ub, char ch) {
    if (ub && ub->remaining) {
        *(ub->buf++) = ch;
        ub->remaining--;
    }
}

static void Unmangle_BufferAppendString(UnmangleBuffer* ub, const char* str) {
    size_t len;

    if (ub) {
        len = strlen(str);
        if (ub->remaining < len) {
            len = ub->remaining;
        }

        memcpy(ub->buf, str, len);
        ub->buf += len;
        ub->remaining -= len;
    }
}

static void Unmangle_BufferTerminate(UnmangleBuffer* ub) {
    ub->buf[0] = 0;
    ub->remaining = 0;
}

static const char* Unmangle_TemplateParams(jmp_buf errorbuf, UnmangleBuffer* ub, const char* p) {
    const char* p2;

    Unmangle_BufferAppendChar(ub, '<');

    while (1) {
        switch (*p) {
            case '&':
            case '=':
                if (*(p++) == '&') {
                    Unmangle_BufferAppendChar(ub, '&');
                }

                while (1) {
                    switch (*p) {
                        case ',':
                        case '>':
                            break;
                        case 0:
                            Unmangle_Error(errorbuf);
                            break;
                        default:
                            Unmangle_BufferAppendChar(ub, *(p++));
                            continue;
                    }
                    break;
                }
                break;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                p2 = p + 1;
                while (1) {
                    if (*p2 == 0) {
                        Unmangle_Error(errorbuf);
                    }

                    if (*p2 == ',' || *p2 == '>') {
                        do {
                            Unmangle_BufferAppendChar(ub, *(p++));
                        } while (*p != '>' && *p != ',');
                        break;
                    } else if (!(*p2 >= '0' && *p2 <= '9')) {
                        p = Unmangle_Type(errorbuf, ub, p);
                        break;
                    }

                    p2++;
                }
                break;

            default:
                p = Unmangle_Type(errorbuf, ub, p);
        }

        if (*p == '>') {
            break;
        }

        if (*(p++) != ',') {
            Unmangle_Error(errorbuf);
        }

        Unmangle_BufferAppendString(ub, ", ");
    }

    Unmangle_BufferAppendChar(ub, '>');
    return p + 1;
}

static const char* Unmangle_SpecialName(jmp_buf errorbuf, UnmangleBuffer* ub, const char* p) {
    const char* what;
    Boolean isConversion = FALSE; // NOTE: invented name

    switch (*(p++)) {
        case 'a':
            switch (*(p++)) {
                case 'a':
                    switch (*(p++)) {
                        case '_':
                            p--;
                            what = "operator &&";
                            break;
                        case 'd':
                            what = "operator &=";
                            break;
                        default:
                            return NULL;
                    }
                    break;
                case 'd':
                    switch (*(p++)) {
                        case '_':
                            p--;
                            what = "operator &";
                            break;
                        case 'v':
                            what = "operator /=";
                            break;
                        default:
                            return NULL;
                    }
                    break;
                case 'e':
                    switch (*(p++)) {
                        case 'r':
                            what = "operator ^=";
                            break;
                        default:
                            return NULL;
                    }
                    break;
                case 'l':
                    switch (*(p++)) {
                        case 's':
                            what = "operator <<=";
                            break;
                        default:
                            return NULL;
                    }
                    break;
                case 'm':
                    switch (*(p++)) {
                        case 'd':
                            what = "operator %=";
                            break;
                        case 'i':
                            what = "operator -=";
                            break;
                        case 'u':
                            what = "operator *=";
                            break;
                        default:
                            return NULL;
                    }
                    break;
                case 'o':
                    switch (*(p++)) {
                        case 'r':
                            what = "operator |=";
                            break;
                        default:
                            return NULL;
                    }
                    break;
                case 'p':
                    switch (*(p++)) {
                        case 'l':
                            what = "operator +=";
                            break;
                        default:
                            return NULL;
                    }
                    break;
                case 'r':
                    switch (*(p++)) {
                        case 's':
                            what = "operator >>=";
                            break;
                        default:
                            return NULL;
                    }
                    break;
                case 's':
                    what = "operator =";
                    break;
                default:
                    return NULL;
            }
            break;
        case 'c':
            switch (*(p++)) {
                case 'l':
                    what = "operator ()";
                    break;
                case 'm':
                    what = "operator ,";
                    break;
                case 'o':
                    what = "operator ~";
                    break;
                case 't':
                    what = "!";
                    break;
                default:
                    return NULL;
            }
            break;
        case 'd':
            switch (*(p++)) {
                case 'l':
                    if (*p == 'a') {
                        p++;
                        what = "operator delete[]";
                    } else {
                        what = "operator delete";
                    }
                    break;
                case 't':
                    what = "~";
                    break;
                case 'v':
                    what = "operator /";
                    break;
                default:
                    return NULL;
            }
            break;
        case 'e':
            switch (*(p++)) {
                case 'q':
                    what = "operator ==";
                    break;
                case 'r':
                    what = "operator ^";
                    break;
                default:
                    return NULL;
            }
            break;
        case 'g':
            switch (*(p++)) {
                case 'e':
                    what = "operator >=";
                    break;
                case 't':
                    what = "operator >";
                    break;
                default:
                    return NULL;
            }
            break;
        case 'l':
            switch (*(p++)) {
                case 'e':
                    what = "operator <=";
                    break;
                case 's':
                    what = "operator <<";
                    break;
                case 't':
                    what = "operator <";
                    break;
                default:
                    return NULL;
            }
            break;
        case 'm':
            switch (*(p++)) {
                case 'd':
                    what = "operator %";
                    break;
                case 'i':
                    what = "operator -";
                    break;
                case 'l':
                    what = "operator *";
                    break;
                case 'm':
                    what = "operator --";
                    break;
                default:
                    return NULL;
            }
            break;
        case 'n':
            switch (*(p++)) {
                case 'e':
                    what = "operator !=";
                    break;
                case 't':
                    what = "operator !";
                    break;
                case 'w':
                    if (*p == 'a') {
                        p++;
                        what = "operator new[]";
                    } else {
                        what = "operator new";
                    }
                    break;
                default:
                    return NULL;
            }
            break;
        case 'o':
            switch (*(p++)) {
                case 'r':
                    what = "operator |";
                    break;
                case 'o':
                    what = "operator ||";
                    break;
                case 'p':
                    Unmangle_BufferAppendString(ub, "operator ");
                    p = Unmangle_Type(errorbuf, ub, p);
                    isConversion = TRUE;
                    break;
                default:
                    return NULL;
            }
            break;
        case 'p':
            switch (*(p++)) {
                case 'l':
                    what = "operator +";
                    break;
                case 'p':
                    what = "operator ++";
                    break;
                default:
                    return NULL;
            }
            break;
        case 'r':
            switch (*(p++)) {
                case 'f':
                    what = "operator ->";
                    break;
                case 's':
                    what = "operator >>";
                    break;
                case 'm':
                    what = "operator ->*";
                    break;
                default:
                    return NULL;
            }
            break;
        case 'v':
            switch (*(p++)) {
                case 'c':
                    what = "operator []";
                    break;
                default:
                    return NULL;
            }
            break;
        default:
            return NULL;
    }

    if (!isConversion) {
        if (*p) {
            Unmangle_BufferAppendString(ub, what);
            if (*p == '<') {
                p = Unmangle_TemplateParams(errorbuf, ub, p + 1);
            }
            if (!(*(p++) == '_' && *(p++) == '_')) {
                return NULL;
            }
        }
    } else {
        if (!(*(p++) == '_' && *(p++) == '_')) {
            return NULL;
        }
    }

    Unmangle_BufferTerminate(ub);
    return p;
}

// NOTE: invented names: errorbuf, ub, p, len, withTemplateArgs
static const char* Unmangle_ClassName(
    jmp_buf errorbuf, UnmangleBuffer* ub, const char* p, size_t len, Boolean withTemplateArgs
) {
    char ch;
    const char* p2;

    while (len-- != 0) {
        if ((ch = *(p++)) == 0) {
            Unmangle_Error(errorbuf);
        }
        if (ch == '<') {
            p2 = Unmangle_TemplateParams(errorbuf, withTemplateArgs ? ub : NULL, p);
            len -= (p2 - p);
            if (len == 0) {
                return p2;
            }
            Unmangle_Error(errorbuf);
        } else {
            Unmangle_BufferAppendChar(ub, ch);
        }
    }

    return p;
}

// NOTE: invented names: isStructor, isDestructor
// isStructor, isDestructor: the name is a constructor's or destructor's (the last class name is
// appended again), and a destructor's (with a `~`)
static const char* Unmangle_QClassName(
    jmp_buf errorbuf, UnmangleBuffer* ub, const char* p, Boolean isStructor, Boolean isDestructor
) {
    const char* lastPiece;
    size_t len;
    size_t lastLen;
    int i;
    char ch;
    int count;

    if (*p == 'Q') {
        p++;
        count = *(p++) - '0';
        if (count < 1 || count > 9) {
            Unmangle_Error(errorbuf);
        }
    } else {
        count = 1;
    }

    for (i = 0; i < count; i++) {
        if (i && ub) {
            Unmangle_BufferAppendString(ub, "::");
        }

        if (*p < '0' || *p > '9') {
            Unmangle_Error(errorbuf);
        }

        len = 0;
        while ((ch = *p) >= '0' && ch <= '9') {
            len = len * 10 + ch - '0';
            p++;
        }

        if (len == 0) {
            Unmangle_Error(errorbuf);
        }

        lastPiece = p;
        lastLen = len;
        p = Unmangle_ClassName(errorbuf, ub, p, len, TRUE);
    }

    if (isStructor && ub) {
        if (isDestructor) {
            Unmangle_BufferAppendString(ub, "::~");
        } else {
            Unmangle_BufferAppendString(ub, "::");
        }
        Unmangle_ClassName(errorbuf, ub, lastPiece, lastLen, FALSE);
    }

    return p;
}

static const char* Unmangle_Modifiers(UnmangleBuffer* ub, const char* p) {
    const char* what;
    Boolean isFirst = TRUE;

    while (1) {
        switch (*p) {
            case 'U':
                what = "unsigned";
                break;
            case 'C':
                what = "const";
                break;
            case 'V':
                what = "volatile";
                break;
            case 'S':
                what = "signed";
                break;
            default:
                return p;
        }

        if (!isFirst) {
            Unmangle_BufferAppendChar(ub, ' ');
        }
        Unmangle_BufferAppendString(ub, what);

        isFirst = FALSE;
        p++;
    }
}

static const char* Unmangle_PointerType(jmp_buf errorbuf, UnmangleBuffer* ub, const char* p) {
    const char* modifiers;

    modifiers = p;
    p = Unmangle_Modifiers(NULL, p);
    if (modifiers == p) {
        modifiers = NULL;
    }

    switch (*p) {
        case 'P':
            p = Unmangle_PointerType(errorbuf, ub, p + 1);
            if (ub) {
                Unmangle_BufferAppendChar(ub, '*');
                if (modifiers) {
                    Unmangle_Modifiers(ub, modifiers);
                }
            }
            return p;
        case 'R':
            p = Unmangle_PointerType(errorbuf, ub, p + 1);
            if (ub) {
                Unmangle_BufferAppendChar(ub, '&');
                if (modifiers) {
                    Unmangle_Modifiers(ub, modifiers);
                }
            }
            return p;
        case 'M':
            p = Unmangle_QClassName(errorbuf, ub, p + 1, FALSE, FALSE);
            if (ub) {
                Unmangle_BufferAppendString(ub, "::*");
                if (modifiers) {
                    Unmangle_Modifiers(ub, modifiers);
                }
            }
            return p;
        default:
            return p;
    }
}

static const char* Unmangle_ArrayType(
    jmp_buf errorbuf, UnmangleBuffer* ub, const char* p, const char* pointer
) {
    const char* digits;

    digits = ++p;

    while (1) {
        switch (*(p++)) {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                continue;
            case '_':
                if (*p == 'A') {
                    p++;
                    continue;
                }
                break;
            default:
                Unmangle_Error(errorbuf);
        }
        break;
    }

    p = Unmangle_Type(errorbuf, ub, p);

    if (ub) {
        if (pointer) {
            Unmangle_BufferAppendString(ub, " (");
            Unmangle_PointerType(errorbuf, ub, pointer);
            Unmangle_BufferAppendChar(ub, ')');
        }

        Unmangle_BufferAppendChar(ub, '[');

        while (1) {
            switch (*digits) {
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                    Unmangle_BufferAppendChar(ub, *(digits++));
                    continue;
                case '_':
                    if (digits[1] == 'A') {
                        Unmangle_BufferAppendString(ub, "][");
                        digits += 2;
                        continue;
                    }
                    break;
                default:
                    Unmangle_Error(errorbuf);
            }
            break;
        }

        Unmangle_BufferAppendChar(ub, ']');
    }

    return p;
}

static const char* Unmangle_FunctionType(
    jmp_buf errorbuf, UnmangleBuffer* ub, const char* p, const char* pointer
) {
    const char* returnType;

    returnType = ++p;

    while (*p != '_') {
        p = Unmangle_Type(errorbuf, NULL, p);
    }

    p = Unmangle_Type(errorbuf, ub, p + 1);

    if (ub) {
        if (pointer) {
            Unmangle_BufferAppendString(ub, " (");
            Unmangle_PointerType(errorbuf, ub, pointer);
            Unmangle_BufferAppendChar(ub, ')');
        }

        Unmangle_BufferAppendChar(ub, '(');

        if (*returnType != '_') {
            while (1) {
                returnType = Unmangle_Type(errorbuf, ub, returnType);
                if (*returnType == '_') {
                    break;
                }
                Unmangle_BufferAppendString(ub, ", ");
            }
        }

        Unmangle_BufferAppendChar(ub, ')');
    }

    return p;
}

static const char* Unmangle_Pointer(
    jmp_buf errorbuf, UnmangleBuffer* ub, const char* p, const char* modifiers, char c
) {
    const char* pointer;
    const char* rest; // NOTE: invented name

    pointer = p;
    rest = Unmangle_PointerType(errorbuf, NULL, p);
    switch (*rest) {
        case 'A':
            if (modifiers) {
                pointer = modifiers;
            }
            return Unmangle_ArrayType(errorbuf, ub, rest, pointer);
        case 'F':
            if (modifiers) {
                pointer = modifiers;
            }
            return Unmangle_FunctionType(errorbuf, ub, rest, pointer);
        default:
            if (c == 'M') {
                p = Unmangle_QClassName(errorbuf, NULL, p + 1, FALSE, FALSE);
                p = Unmangle_Type(errorbuf, ub, p);
                if (ub) {
                    Unmangle_BufferAppendChar(ub, ' ');
                    Unmangle_QClassName(errorbuf, ub, pointer + 1, FALSE, FALSE);
                    Unmangle_BufferAppendString(ub, "::*");
                    if (modifiers) {
                        Unmangle_Modifiers(ub, modifiers);
                    }
                }
            } else {
                p = Unmangle_Type(errorbuf, ub, p + 1);
                if (ub) {
                    Unmangle_BufferAppendChar(ub, c);
                    if (modifiers) {
                        Unmangle_Modifiers(ub, modifiers);
                    }
                }
            }
            return p;
    }
}

static const char* Unmangle_Vector(
    jmp_buf errorbuf, UnmangleBuffer* ub, const char* p, const char* modifiers
) {
    const char* p2 = p + 2;
    switch (p[1]) {
        case 'U':
            switch (p[2]) {
                case 'c':
                    p2++;
                    Unmangle_BufferAppendString(ub, "vector unsigned char");
                    break;
                case 's':
                    p2++;
                    Unmangle_BufferAppendString(ub, "vector unsigned short");
                    break;
                case 'i':
                    p2++;
                    Unmangle_BufferAppendString(ub, "vector unsigned int");
                    break;
            }
            break;
        case 'c':
            Unmangle_BufferAppendString(ub, "vector signed char");
            break;
        case 'C':
            Unmangle_BufferAppendString(ub, "vector bool char");
            break;
        case 's':
            Unmangle_BufferAppendString(ub, "vector signed short");
            break;
        case 'S':
            Unmangle_BufferAppendString(ub, "vector bool short");
            break;
        case 'i':
            Unmangle_BufferAppendString(ub, "vector signed int");
            break;
        case 'I':
            // (the original's literal has a trailing space)
            Unmangle_BufferAppendString(ub, "vector bool int ");
            break;
        case 'f':
            Unmangle_BufferAppendString(ub, "vector float");
            break;
        case 'p':
            Unmangle_BufferAppendString(ub, "vector pixel");
            break;
        default:
            p2 = p + 1;
    }

    return p2;
}

static const char* Unmangle_Type(jmp_buf errorbuf, UnmangleBuffer* ub, const char* p) {
    const char* modifiers;
    const char* what;

    modifiers = p;
    p = Unmangle_Modifiers(NULL, p);
    if (modifiers == p) {
        modifiers = NULL;
    }

    switch (*p) {
        case 'b':
            what = "bool";
            break;
        case 'v':
            what = "void";
            break;
        case 'c':
            what = "char";
            break;
        case 'w':
            what = "wchar_t";
            break;
        case 's':
            what = "short";
            break;
        case 'i':
            what = "int";
            break;
        case 'l':
            what = "long";
            break;
        case 'x':
            what = "long long";
            break;
        case 'f':
            what = "float";
            break;
        case 'D':
            what = "short double";
            break;
        case 'd':
            what = "double";
            break;
        case 'r':
            what = "long double";
            break;
        case 'X':
            return Unmangle_Vector(errorbuf, ub, p, modifiers);
        case 'P':
            return Unmangle_Pointer(errorbuf, ub, p, modifiers, '*');
        case 'R':
            return Unmangle_Pointer(errorbuf, ub, p, modifiers, '&');
        case 'M':
            return Unmangle_Pointer(errorbuf, ub, p, modifiers, 'M');
        case 'A':
            return Unmangle_ArrayType(errorbuf, ub, p, NULL);
        case 'F':
            return Unmangle_FunctionType(errorbuf, ub, p, NULL);
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case 'Q':
            if (modifiers) {
                Unmangle_Modifiers(ub, modifiers);
                Unmangle_BufferAppendChar(ub, ' ');
            }
            return Unmangle_QClassName(errorbuf, ub, p, FALSE, FALSE);
        default:
            Unmangle_Error(errorbuf);
    }

    if (modifiers) {
        Unmangle_Modifiers(ub, modifiers);
        Unmangle_BufferAppendChar(ub, ' ');
    }
    Unmangle_BufferAppendString(ub, what);

    return p + 1;
}

static void Unmangle_FuncArgList(jmp_buf errorbuf, UnmangleBuffer* ub, const char* p) {
    Boolean is_volatile;
    Boolean is_const;

    is_volatile = is_const = FALSE;

    if (*p == 'C') {
        p++;
        is_const = TRUE;
    }
    if (*p == 'V') {
        p++;
        is_volatile = TRUE;
    }

    if (*(p++) != 'F') {
        Unmangle_Error(errorbuf);
    }

    Unmangle_BufferAppendChar(ub, '(');

    if (!(p[0] == 'v' && p[1] == 0)) {
        while (1) {
            if (*p == 'e') {
                if (p[1]) {
                    Unmangle_Error(errorbuf);
                }
                Unmangle_BufferAppendString(ub, "...");
                break;
            }

            if (*p == '_') {
                p = Unmangle_Type(errorbuf, NULL, p + 1);
                if (p[0]) {
                    Unmangle_Error(errorbuf);
                }
                break;
            }

            p = Unmangle_Type(errorbuf, ub, p);
            if (!*p) {
                break;
            }

            if (*p != '_') {
                Unmangle_BufferAppendChar(ub, ',');
            }
        }
    }

    Unmangle_BufferAppendChar(ub, ')');

    if (is_const) {
        Unmangle_BufferAppendString(ub, " const");
    }
    if (is_volatile) {
        Unmangle_BufferAppendString(ub, " volatile");
    }

    Unmangle_BufferTerminate(ub);
}

void MWUnmangleClassName(const char* input, char* output, size_t maxLen) {
    UnmangleBuffer ub;
    jmp_buf errorbuf;
    const char* p;
    Boolean hasTemplateArgs; // NOTE: invented name

    if (input[0] == 'Q' && (input[1] >= '1' && input[1] <= '9')) {
        if (setjmp(errorbuf) == 0) {
            Unmangle_BufferInit(&ub, output, maxLen);
            Unmangle_QClassName(errorbuf, &ub, input, FALSE, FALSE);
            Unmangle_BufferTerminate(&ub);
            return;
        }
    }

    p = input;
    hasTemplateArgs = FALSE;
    while (1) {
        switch (*p) {
            case 0:
                break;
            case '<':
                hasTemplateArgs = TRUE;
            default:
                p++;
                continue;
        }
        break;
    }

    if (hasTemplateArgs) {
        if (setjmp(errorbuf) == 0) {
            Unmangle_BufferInit(&ub, output, maxLen);
            Unmangle_ClassName(errorbuf, &ub, input, p - input, TRUE);
            Unmangle_BufferTerminate(&ub);
            return;
        }
    }

    strncpy(output, input, maxLen);
    output[maxLen - 1] = 0;
}

void MWUnmangle(const char* input, char* output, size_t maxLen) {
    jmp_buf errorbuf;
    UnmangleBuffer ub_internal;
    UnmangleBuffer ub;
    char mybuf[256];
    const char* p;
    Boolean hasSeparator; // NOTE: invented name
    char ch;

    switch (*input) {
        case '.':
            if (""[0] == '.') {
                input++;
            }
            break;
        case '_':
            if (""[0] == '_') {
                input++;
            }
            switch (input[1]) {
                case '%':
                case '#':
                case '@':
                    input += 2;
                    break;
            }
            break;
    }

    if (setjmp(errorbuf) == 0) {
        Unmangle_BufferInit(&ub_internal, mybuf, sizeof(mybuf));
        Unmangle_BufferInit(&ub, output, maxLen);

        if (!(input[0] == '_' &&
              input[1] == '_' &&
              (p = Unmangle_SpecialName(errorbuf, &ub_internal, input + 2)))) {
            hasSeparator = FALSE;
            Unmangle_BufferInit(&ub_internal, mybuf, sizeof(mybuf));
            for (p = input; *p == '_'; p++) {
                Unmangle_BufferAppendChar(&ub_internal, '_');
            }

            while ((ch = *(p++))) {
                if (ch == '_' && *p == '_') {
                    while (*(++p) == '_') {
                        Unmangle_BufferAppendChar(&ub_internal, '_');
                    }
                    hasSeparator = TRUE;
                    break;
                }
                Unmangle_BufferAppendChar(&ub_internal, ch);
            }

            Unmangle_BufferTerminate(&ub_internal);

            if (!hasSeparator) {
                Unmangle_BufferAppendString(&ub, mybuf);
                Unmangle_BufferTerminate(&ub);
                return;
            }
        }

        switch (*p) {
            case 'F':
                if (mybuf[1] != 0 || (mybuf[0] != '!' && mybuf[0] != '~')) {
                    Unmangle_BufferAppendString(&ub, mybuf);
                    Unmangle_FuncArgList(errorbuf, &ub, p);
                    return;
                }
                break;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case 'Q':
                if (mybuf[1] == 0 && (mybuf[0] == '!' || mybuf[0] == '~')) {
                    p = Unmangle_QClassName(errorbuf, &ub, p, TRUE, mybuf[0] == '~');
                } else {
                    p = Unmangle_QClassName(errorbuf, &ub, p, FALSE, FALSE);
                    Unmangle_BufferAppendString(&ub, "::");
                    Unmangle_BufferAppendString(&ub, mybuf);
                    if (*p == 0) {
                        Unmangle_BufferTerminate(&ub);
                        return;
                    }
                }
                Unmangle_FuncArgList(errorbuf, &ub, p);
                return;
        }
    }

    strncpy(output, input, maxLen);
    output[maxLen - 1] = 0;
}
