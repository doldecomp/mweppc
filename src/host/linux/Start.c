#include "ansi_prefix.Win32.h"
#include <ansi_parms.h>
#include <WINDEF.H>
#include <WINBASE.H>
#include <crtl.h>
#include <stdlib.h>
#include "Native.h"
#include "ThreadLocalData.h"

int main(int argc, char** argv);
void _SetupArgs(void);
void _RunInit(void);
extern int __argc;
extern char** __argv;

__attribute__((used)) void (*__xc_a[2])(void) = {0, 0};

__attribute__((noreturn, used)) void Native_Main(unsigned long* stack) {
    Native_argc = (int)stack[0];
    Native_argv = (char**)(stack + 1);
    Native_envp = Native_argv + Native_argc + 1;
    Native_InitializeWin32();

    _CRTStartup();
    if (!_InitializeMainThreadData()) {
        exit(-1);
    }
    _SetupArgs();
    _RunInit();
    exit(main(__argc, __argv));
}

__attribute__((naked, noreturn)) void _start(void) {
    __asm__("xorl %ebp, %ebp\n\t"
            "movl %esp, %eax\n\t"
            "andl $-16, %esp\n\t"
            "subl $12, %esp\n\t"
            "pushl %eax\n\t"
            "call Native_Main\n\t"
            "hlt");
}

__asm__(".section .rodata.native_resources,\"a\"\n"
        ".p2align 2\n"
        "native_resources:\n"
        ".incbin \"" NATIVE_RESOURCES "\"\n"
        "native_resources_end:\n"
        ".previous");
extern const unsigned char native_resources[];
extern const unsigned char native_resources_end[];

static unsigned int U32(const unsigned char* p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int)p[3] << 24;
}

static unsigned int U16(const unsigned char* p) {
    return p[0] | p[1] << 8;
}

static int Matches(const unsigned char* id, const char* want, unsigned int* size) {
    unsigned long number = (unsigned long)want;

    if (U16(id) == 0xFFFF) {
        *size = 4;
        if (number < 0x10000) {
            return U16(id + 2) == number;
        }
        if (want[0] == '#') {
            unsigned int n = 0;
            const char* c;
            for (c = want + 1; *c >= '0' && *c <= '9'; c++) {
                n = n * 10 + (*c - '0');
            }
            return *c == 0 && U16(id + 2) == n;
        }
        return 0;
    }
    {
        unsigned int i = 0;
        int same = number >= 0x10000;
        while (U16(id + i * 2) != 0) {
            if (same) {
                unsigned int c = U16(id + i * 2);
                unsigned int w = (unsigned char)want[i];
                if (c >= 'a' && c <= 'z') {
                    c -= 32;
                }
                if (w >= 'a' && w <= 'z') {
                    w -= 32;
                }
                if (w == 0 || c != w) {
                    same = 0;
                }
            }
            i++;
        }
        if (same && want[i] != 0) {
            same = 0;
        }
        *size = (i + 1) * 2;
        return same;
    }
}

int Native_FindResource(const char* type, const char* name, const void** data, unsigned int* size) {
    const unsigned char* p = native_resources;

    while (p + 8 <= native_resources_end) {
        unsigned int data_size = U32(p);
        unsigned int header_size = U32(p + 4);
        unsigned int type_size;
        unsigned int name_size;
        int type_matches = Matches(p + 8, type, &type_size);
        int name_matches = Matches(p + 8 + type_size, name, &name_size);
        if (data_size != 0 && type_matches && name_matches) {
            *data = p + header_size;
            *size = data_size;
            return 1;
        }
        p += (header_size + data_size + 3) & ~3u;
    }
    return 0;
}

unsigned int Native_ResourceSize(const void* data) {
    const unsigned char* p = native_resources;

    while (p + 8 <= native_resources_end) {
        unsigned int data_size = U32(p);
        unsigned int header_size = U32(p + 4);
        if (p + header_size == data) {
            return data_size;
        }
        p += (header_size + data_size + 3) & ~3u;
    }
    return 0;
}
