#include "compiler/ScanFloat.h"
#include <errno.h>
#include <stdlib.h>

// an out-of-range value is reported through errno (which, as the original was built, is not the
// errno the linked strtod sets: <errno.h>)
char* ScanFloat(const char* str, double* result, Boolean* failed) {
    char* end;

    errno = 0;
    *result = strtod(str, &end);
    *failed = errno != 0;
    return end;
}
