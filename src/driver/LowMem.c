#include "driver/LowMem.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"

static OSErr memErr;

UInt32 LMGetTicks(void) {
    return (OS_GetMilliseconds() * 60) / 1000;
}

// (no caller: the linker dropped it)
UInt32 LMGetTime(void) {
    return OS_GetMilliseconds() / 1000;
}

OSErr LMGetMemErr(void) {
    return memErr;
}

// (no caller: the linker dropped it)
void LMSetMemErr(OSErr value) {
    memErr = value;
}
