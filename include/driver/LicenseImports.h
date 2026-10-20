#ifndef DRIVER_LICENSEIMPORTS_H
#define DRIVER_LICENSEIMPORTS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// The FLEXlm 6 client API the driver calls in LMGR326B.dll (lmclient.h's and lmpolicy.h's)

// A vendor's encryption seeds and keys (lmclient.h's VENDORCODE5, 40 bytes)
typedef struct VENDORCODE {
    short type;
    UInt32 data[2];
    UInt32 keys[4];
    short flexlm_version;
    short flexlm_revision;
    char flexlm_patch[2];
    char behavior_ver[5];
} VENDORCODE;

// lmpolicy.h's code handle: the vendor code and the vendor daemon's name. TODO: the type's and
// members' names are not checked against FLEXlm's lmpolicy.h (not at hand)
typedef struct LPCODE_HANDLE {
    VENDORCODE* code;
    const char* vendor;
} LPCODE_HANDLE;

// (opaque: lmpolicy.h's checked-out license)
typedef struct LP_HANDLE LP_HANDLE;

extern int lp_checkout(
    LPCODE_HANDLE* code,
    int policy,
    const char* feature,
    const char* version,
    int nlic,
    const char* licensePath,
    LP_HANDLE** handle
);
extern void lp_checkin(LP_HANDLE* handle);
extern char* lp_errstring(LP_HANDLE* handle);

#ifdef __cplusplus
}
#endif

#endif
