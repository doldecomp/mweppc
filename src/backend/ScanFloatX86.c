#include "compiler/ScanFloat.h"
#include <errno.h>
#include <stdlib.h>

char* ScanFloat(const char* str, double* result, Boolean* failed) {
    char* end;

    errno = 0;
    *result = strtod(str, &end);
    *failed = errno != 0;
    return end;
}
