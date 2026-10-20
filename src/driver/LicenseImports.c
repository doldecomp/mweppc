#include "driver/LicenseImports.h"

#ifdef __MWERKS__
extern void* _imp__lp_checkout;
extern void* _imp__lp_checkin;
extern void* _imp__lp_errstring;

__declspec(naked) int lp_checkout(
    LPCODE_HANDLE* code,
    int policy,
    const char* feature,
    const char* version,
    int nlic,
    const char* licensePath,
    LP_HANDLE** handle
) {
    asm {
        jmp [_imp__lp_checkout]
    }
}

__declspec(naked) void lp_checkin(LP_HANDLE* handle) {
    asm {
        jmp [_imp__lp_checkin]
    }
}

__declspec(naked) char* lp_errstring(LP_HANDLE* handle) {
    asm {
        jmp [_imp__lp_errstring]
    }
}
#endif
