#include "driver/LicenseImports.h"

int lp_checkout(
    LPCODE_HANDLE* code,
    int policy,
    const char* feature,
    const char* version,
    int nlic,
    const char* licensePath,
    LP_HANDLE** handle
) {
    (void)code;
    (void)policy;
    (void)feature;
    (void)version;
    (void)nlic;
    (void)licensePath;
    *(int*)handle = 1234;
    return 0;
}

void lp_checkin(LP_HANDLE* handle) {
    (void)handle;
}

char* lp_errstring(LP_HANDLE* handle) {
    (void)handle;
    return "";
}
