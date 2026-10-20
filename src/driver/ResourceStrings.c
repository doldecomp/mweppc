// The string-list resources (STR#) the driver and its plugins register and look up by id. Nothing
// initializes or frees the table (Wii's module has no such functions either).
#include "driver/ResourceStrings.h"
#include <stdio.h>

typedef struct {
    const char* name;
    SInt16 rsrcid;
    const char** strings;
} Res;

static Res rlist[16];

int Res_AddResource(const char* name, SInt16 rsrcid, const char** strings) {
    int scan;

    for (scan = 0; scan < 16 && rlist[scan].rsrcid; scan++) {
        if (rsrcid == rlist[scan].rsrcid) {
            fprintf(stderr, "Resource %d is already added!\n", rsrcid);
            return FALSE;
        }
    }

    if (scan >= 16) {
        return FALSE;
    }

    rlist[scan].name = name;
    rlist[scan].rsrcid = rsrcid;
    rlist[scan].strings = strings;
    return TRUE;
}

const char* Res_GetResource(SInt16 rsrcid, SInt16 index) {
    int scan;
    int find;
    static char err[256];

    index--;
    for (scan = 0; scan < 16; scan++) {
        if (rsrcid == rlist[scan].rsrcid) {
            find = 0;
            if (index < 0) {
                snprintf(
                    err,
                    sizeof(err),
                    "[Illegal string index #%d in list '%s' (%d)]",
                    index,
                    rlist[scan].name,
                    rsrcid
                );
                return err;
            }
            while (find <= index) {
                if (!rlist[scan].strings[find]) {
                    snprintf(
                        err,
                        sizeof(err),
                        "[String #%d not found in resource '%s' (%d)]",
                        index + 1,
                        rlist[scan].name,
                        rsrcid
                    );
                    return err;
                }
                if (find == index) {
                    return rlist[scan].strings[find];
                }
                find++;
            }
        }
    }

    return NULL;
}
