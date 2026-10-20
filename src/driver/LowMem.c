#include "driver/LowMem.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"

static OSErr memErr;

UInt32 LMGetTicks(void) {
    return (OS_GetMilliseconds() * 60) / 1000;
}

UInt32 LMGetTime(void) {
    return OS_GetMilliseconds() / 1000;
}

OSErr LMGetMemErr(void) {
    return memErr;
}

void LMSetMemErr(OSErr value) {
    memErr = value;
}
