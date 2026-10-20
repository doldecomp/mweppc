#include "ansi_prefix.Win32.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void __assertion_failed(char const *condition, char const *filename, int lineno)
{
    fprintf(stderr, "Assertion (%s) failed in \"%s\" on line %d\n", condition, filename, lineno);
    abort();
}
