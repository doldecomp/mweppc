#include "driver/MemUtils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void* __stdcall xmalloc(const char* what, int size) {
    void* ret;

    ret = malloc(size ? size : 1);
    if (!ret) {
        fprintf(
            stderr,
            "*** Out of memory when allocating %d bytes%s%s",
            size,
            what ? " for " : "",
            what ? what : ""
        );
        exit(-23);
        return NULL;
    }

    return ret;
}

void* __stdcall xcalloc(const char* what, int size) {
    void* ret;

    ret = xmalloc(what, size);
    memset(ret, 0, size);
    return ret;
}

void* __stdcall xrealloc(const char* what, void* old, int size) {
    void* ret;

    ret = realloc(old, size ? size : 1);
    if (!ret) {
        fprintf(
            stderr,
            "*** Out of memory when resizing buffer to %d bytes%s%s",
            size,
            what ? " for " : "",
            what ? what : ""
        );
        exit(-23);
        return NULL;
    }

    return ret;
}

char* __stdcall xstrdup(const char* str) {
    return strcpy(xmalloc(NULL, strlen(str) + 1), str);
}

void __stdcall xfree(void* ptr) {
    if (ptr) {
        free(ptr);
    }
}
