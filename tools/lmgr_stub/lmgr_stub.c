// Stand-in for LMGR326B.dll (FlexLM), which the Pro 5 compilers import by ordinal and which checks a license on
// Windows: lp_checkout always succeeds. It does what wibo's built-in stub does.
int __cdecl lp_checkout(
    int a, int b, const char* c, const char* d, int e, const char* f, int* out
) {
    *out = 1234;
    return 0;
}

int __cdecl lp_checkin(void) {
    return 0;
}

const char* __cdecl lp_errstring(void) {
    return "";
}

int __stdcall DllMain(void* instance, unsigned long reason, void* reserved) {
    return 1;
}
