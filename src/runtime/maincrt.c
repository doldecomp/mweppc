#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <maincrt.c>
#else
#include <ansi_parms.h>
#include <WINDEF.H>
#include <WINBASE.H>
#include <crtl.h>
#include <stdlib.h>
#include "ThreadLocalData.h"

int main(int argc, char **argv);

extern int _MWCHandler(
    PEXCEPTION_RECORD exc, void *exc_stack, LPCONTEXT context, void *mystery);

typedef void (*InitFunction)(void);
__attribute__((section(".CRT$XCA"), used)) InitFunction __xc_a = 0;
__attribute__((section(".CRT$XCZ"), used)) InitFunction __xc_z = 0;

__attribute__((section(".tls"))) char _tls_start = 0;
__attribute__((section(".tls$ZZZ"))) char _tls_end = 0;
DWORD _tls_index = 0;
static PIMAGE_TLS_CALLBACK tls_callbacks[1] = {0};
__attribute__((used)) const IMAGE_TLS_DIRECTORY32 _tls_used = {
    (DWORD) &_tls_start, (DWORD) &_tls_end, &_tls_index, tls_callbacks, 0, 0
};
__asm__(".globl __tls_array\n.set __tls_array, 0x2c");

int _fltused = 0x9875;

void __main(void)
{
}

typedef struct ExceptionRegistration {
    struct ExceptionRegistration *next;
    void *handler;
} ExceptionRegistration;

void mainCRTStartup(void);
void mainCRTStartup(void)
{
    ExceptionRegistration registration;

    __asm__ volatile("movl %%fs:0, %0" : "=r"(registration.next));
    registration.handler = (void *) _MWCHandler;
    __asm__ volatile("movl %0, %%fs:0" : : "r"(&registration) : "memory");

    _CRTStartup();

    if (!_InitializeMainThreadData()) {
        exit(-1);
    }

    _SetupArgs();

    _RunInit();

    exit(main(__argc, __argv));
}
#endif
