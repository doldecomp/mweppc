#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <maincrt.c>
#else
/* clang: the console program's entry point, as lib/runtime's maincrt.c starts the program, without
 * MWCC's C++ exception tables (the compiler is built without exceptions), and what MWCC's runtime
 * objects give a program: the thread-local storage directory (ctls.obj) and the bounds of the
 * static initializers (ccinit.obj). */
#include <ansi_parms.h>
#include <WINDEF.H>
#include <WINBASE.H>
#include <crtl.h>
#include <stdlib.h>
#include "ThreadLocalData.h"

int main(int argc, char **argv);

extern int _MWCHandler(
    PEXCEPTION_RECORD exc, void *exc_stack, LPCONTEXT context, void *mystery);

/* The bounds of the static initializers, pointers in .CRT$XC* (none in the compiler): _RunInit
 * (runinit.c) calls each from the one after __xc_a to the null __xc_z. */
typedef void (*InitFunction)(void);
__attribute__((section(".CRT$XCA"), used)) InitFunction __xc_a = 0;
__attribute__((section(".CRT$XCZ"), used)) InitFunction __xc_z = 0;

/* The thread-local storage directory (__declspec(thread): MSL's per-thread state). The loader
 * copies .tls, the variables between _tls_start and _tls_end, for each thread, and sets
 * _tls_index. */
__attribute__((section(".tls"))) char _tls_start = 0;
__attribute__((section(".tls$ZZZ"))) char _tls_end = 0;
DWORD _tls_index = 0;
static PIMAGE_TLS_CALLBACK tls_callbacks[1] = {0};
__attribute__((used)) const IMAGE_TLS_DIRECTORY32 _tls_used = {
    (DWORD) &_tls_start, (DWORD) &_tls_end, &_tls_index, tls_callbacks, 0, 0
};
/* (where the Microsoft-ABI objects find the thread information block's table of the blocks) */
__asm__(".globl __tls_array\n.set __tls_array, 0x2c");

/* Defined in a program that uses floating point: the Microsoft-ABI objects refer to it, as
 * Microsoft's compiler's do for its C library. */
int _fltused = 0x9875;

/* The MinGW-ABI main calls it first: MinGW's runtime runs the static constructors there.
 * _RunInit runs the program's. */
void __main(void)
{
}

/* The exception registration record mainCRTStartup puts first: _MWCHandler reports a Win32
 * exception nothing handles. */
typedef struct ExceptionRegistration {
    struct ExceptionRegistration *next;
    void *handler;
} ExceptionRegistration;

void mainCRTStartup(void);
void mainCRTStartup(void)
{
    ExceptionRegistration registration;

    // Set up exception handler
    __asm__ volatile("movl %%fs:0, %0" : "=r"(registration.next));
    registration.handler = (void *) _MWCHandler;
    __asm__ volatile("movl %0, %%fs:0" : : "r"(&registration) : "memory");

    // Perform library initialization
    _CRTStartup();

    // Initialize the thread local data structure.
    if (!_InitializeMainThreadData()) {
        // Initialization failed.
        exit(-1);
    }

    // Set up the command line arguments
    _SetupArgs();

    // Run the init code (which runs static C++ constructors)
    _RunInit();

    // Call the main program and exit with the status it returns
    exit(main(__argc, __argv));
}
#endif
