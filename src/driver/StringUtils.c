#include "driver/StringUtils.h"
#include "compiler/win32.h"
#include "driver/Memory.h"
#include "driver/oslib.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char pfbuf[256];

StringPtr _pstrcpy(StringPtr dst, ConstStringPtr src) {
    memcpy(dst, src, src[0] + 1);
    return dst;
}

int pstrcmp(ConstStringPtr a, ConstStringPtr b) {
    int n;
    int d;

    if ((d = (n = *(a++)) - *(b++))) {
        return d;
    }

    while (n-- > 0) {
        if ((d = *(a++) != *(b++))) {
            return d;
        }
    }

    return 0;
}

int pstrchr(ConstStringPtr str, char find) {
    int idx = 0;

    while (idx++ < str[0]) {
        if (find == str[idx]) {
            return idx;
        }
    }

    return 0;
}

void __stdcall c2pstrcpy(StringPtr dst, const char* src) {
    int len = strlen(src);
    if (len > 255) {
        len = 255;
    }

    memmove(dst + 1, src, len);
    dst[0] = len;
}

void __stdcall p2cstrcpy(char* dst, ConstStringPtr src) {
    memcpy(dst, src + 1, src[0]);
    dst[src[0]] = 0;
}

char* mvprintf(char* mybuf, unsigned int len, const char* format, va_list va) {
    int maxlen;
    int ret;
    char* buf;

    OS_ASSERT(134, mybuf != NULL);

    maxlen = len - 1;
    buf = mybuf;
    ret = vsnprintf(mybuf, maxlen, format, va);
    if (ret < 0) {
        do {
            if (buf != mybuf) {
                free(buf);
            }
            maxlen <<= 1;
            buf = malloc(maxlen);
            if (!buf) {
                return strncpy(mybuf, "<out of memory>", len);
            }
            ret = vsnprintf(buf, maxlen, format, va);
        } while (ret < 0);
    } else if (ret > maxlen) {
        maxlen = ret + 1;
        buf = malloc(maxlen);
        if (!buf) {
            return strncpy(mybuf, "<out of memory>", len);
        }
        vsnprintf(buf, maxlen, format, va);
    }

    return buf;
}

char* mprintf(char* mybuf, unsigned int len, const char* format, ...) {
    char* ret;

    va_list ap;
    va_start(ap, format);
    ret = mvprintf(mybuf, len, format, ap);
    va_end(ap);

    return ret;
}

int HPrintF(Handle text, const char* format, ...) {
    char* buf;
    int ret;

    va_list ap;
    va_start(ap, format);
    buf = mvprintf(pfbuf, sizeof(pfbuf), format, ap);
    va_end(ap);

    ret = strlen(buf);
    if (PtrAndHand(buf, text, strlen(buf))) {
        return 0;
    }

    if (buf != pfbuf) {
        free(buf);
    }

    return ret;
}
