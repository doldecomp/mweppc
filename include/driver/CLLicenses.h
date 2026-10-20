#ifndef DRIVER_CLLICENSES_H
#define DRIVER_CLLICENSES_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern char* licenseFile;

extern void License_Initialize(void);
extern void License_Terminate(void);
extern SInt32 License_Checkout(
    const char* featureName, const char* licenseVersion, int autoCheckin, char* errorBuf
);
extern void License_Checkin(SInt32 cookie);
extern void License_AutoCheckin(void);

#ifdef __cplusplus
}
#endif

#endif
