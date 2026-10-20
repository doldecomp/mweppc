#include "driver/CLLicenses.h"
#include "driver/CLErrors.h"
#include "driver/CLMain.h"
#include "driver/LicenseImports.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include <stdlib.h>
#include <string.h>

typedef struct LicenseSlot {
    LP_HANDLE* handle;
    SInt32 cookie;
} LicenseSlot;

static VENDORCODE* vendorcode;
static LicenseSlot license_slots[32];
static int license_slot_count;
static int license_id_counter;
char* licenseFile;

static int allocate_license_slot(LP_HANDLE* handle, int autoCheckin) {
    int slot;

    for (slot = 0; slot < license_slot_count; slot++) {
        if (license_slots[slot].cookie == 0) {
            break;
        }
    }
    if (slot >= 32) {
        License_AutoCheckin();
        slot = license_slot_count;
    }
    if (slot < 32) {
        license_id_counter++;
        license_slots[slot].handle = handle;
        license_slots[slot].cookie = autoCheckin ? -license_id_counter : license_id_counter;
        if (slot >= license_slot_count) {
            license_slot_count++;
        }
        return license_id_counter;
    }
    DO_INTERNAL_ERROR(97, "Out of license space");
    return 0;
}

static int find_license(SInt32 cookie, LP_HANDLE** handle) {
    int index;

    for (index = 0; index < license_slot_count; ++index) {
        if (cookie == license_slots[index].cookie || cookie == -license_slots[index].cookie) {
            *handle = license_slots[index].handle;
            return index;
        }
    }
    DO_INTERNAL_ERROR(117, "Searched license not found");
    return -1;
}

static int get_license_slot_values(int index, LP_HANDLE** handle, SInt32* cookie) {
    if (index >= 0 && index < license_slot_count && license_slots[index].cookie != 0) {
        *handle = license_slots[index].handle;
        *cookie = license_slots[index].cookie;
        return TRUE;
    }
    return FALSE;
}

static int delete_license(int index) {
    if (index >= 0 && index < license_slot_count) {
        license_slots[index].cookie = 0;
        license_slots[index].handle = NULL;
        if (index + 1 == license_slot_count) {
            for (; index >= 0 && license_slots[index].cookie == 0; --index) {
                --license_slot_count;
            }
        }
        return TRUE;
    }
    DO_INTERNAL_ERROR(154, "Deleted license not valid");
    return FALSE;
}

static int get_license_slot_count(void) {
    return license_slot_count;
}

void License_Initialize(void) {
    license_slot_count = 0;
    vendorcode = NULL;
}

void License_Terminate(void) {
    int index;
    LP_HANDLE* handle;
    SInt32 cookie;

    License_AutoCheckin();
    for (index = 0; index < get_license_slot_count(); index++) {
        if (get_license_slot_values(index, &handle, &cookie) && cookie > 0) {
            License_Checkin(cookie);
        }
    }
    if (vendorcode != NULL) {
        memset(vendorcode, 0, sizeof(VENDORCODE));
        vendorcode = NULL;
    }
}

SInt32 License_Checkout(
    const char* featureName, const char* licenseVersion, int autoCheckin, char* errorBuf
) {
    OSSpec alternatePath;
    char licensePath[256];
    LPCODE_HANDLE codeHandle;
    LP_HANDLE* licenseHandle;
    int result;
    int status;
    char* environmentPath;

    result = 0;
    licenseHandle = NULL;

    licensePath[0] = 0;
    if (licenseFile != NULL) {
        strcpy(licensePath, licenseFile);
    }
    if (licensePath[0] == 0) {
        environmentPath = getenv("LM_LICENSE_FILE");
        if (environmentPath != NULL) {
            strcpy(licensePath, environmentPath);
            if (OS_MakeFileSpec(licensePath, &alternatePath) != 0 ||
                OS_Status(&alternatePath) != 0) {
                if (OS_ResolveLink(&clState.programSpec, &alternatePath) == 0) {
                    OS_PathSpecToString(
                        &alternatePath.path, licensePath, sizeof(alternatePath.path.s)
                    );
                    strcat(licensePath, "license.dat");
                } else {
                    licensePath[0] = 0;
                }
            }
        }
    }
    if (licensePath[0] == 0) {
        OS_PathSpecToString(
            &clState.programSpec.path, licensePath, sizeof(clState.programSpec.path.s)
        );
        strcat(licensePath, "license.dat");
        if (OS_MakeFileSpec(licensePath, &alternatePath) != 0 || OS_Status(&alternatePath) != 0) {
            if (OS_ResolveLink(&clState.programSpec, &alternatePath) == 0) {
                OS_PathSpecToString(&alternatePath.path, licensePath, sizeof(alternatePath.path.s));
                strcat(licensePath, "license.dat");
            } else {
                licensePath[0] = 0;
            }
        }
    }
    if (licensePath[0] == 0) {
        strcpy(licensePath, "license.dat");
    }
    if (vendorcode == NULL) {
        vendorcode = xmalloc(NULL, sizeof(VENDORCODE));
        vendorcode->type = 4;
        vendorcode->data[0] = 0x4cf2d709;
        vendorcode->data[1] = 0xd64a5128;
        vendorcode->keys[1] = 0x463ed9b0;
        vendorcode->keys[0] = 0x745ce35b;
        vendorcode->keys[3] = 0xa610dfa5;
        vendorcode->keys[2] = 0xc304db20;
        vendorcode->flexlm_version = 6;
        vendorcode->flexlm_revision = 1;
        vendorcode->flexlm_patch[0] = 0;
        vendorcode->flexlm_patch[1] = 0;
        strncpy(vendorcode->behavior_ver, "06.0", sizeof("06.0"));
    }
    codeHandle.code = vendorcode;
    codeHandle.vendor = "metrowks";
    status = lp_checkout(
        &codeHandle, 0x101, featureName, licenseVersion, 1, licensePath, &licenseHandle
    );
    strcpy(errorBuf, "No failure");
    if (status == 0) {
        result = allocate_license_slot(licenseHandle, autoCheckin);
        if (result == 0) {
            strcpy(errorBuf, "Memory error:  Could not store license cookie");
            lp_checkin(licenseHandle);
        }
    } else {
        strcpy(errorBuf, lp_errstring(licenseHandle));
        lp_checkin(licenseHandle);
    }
    return result;
}

void License_Checkin(SInt32 cookie) {
    int index;
    LP_HANDLE* handle;

    if (0 < cookie) {
        index = find_license(cookie, &handle);
        if (index >= 0) {
            delete_license(index);
            lp_checkin(handle);
        }
    }
}

void License_AutoCheckin(void) {
    int index;
    LP_HANDLE* handle;
    SInt32 cookie;

    for (index = 0; index < get_license_slot_count(); index++) {
        if (get_license_slot_values(index, &handle, &cookie) && cookie < 0) {
            License_Checkin(cookie);
        }
    }
}
