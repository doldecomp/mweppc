#include "driver/CLOverlays.h"
#include "driver/CLErrors.h"
#include "driver/MemUtils.h"
#include "driver/oslib.h"
#include <string.h>

void OvlGroup_Delete(OvlGroup* grp);
void Overlay_Delete(Overlay* oly);
SInt32 Overlay_GetFile(Overlay* oly, SInt32 filnum);

Boolean Overlays_Initialize(Overlays* this) {
    OvlGroup* grp;
    Overlay* ovl;
    OvlAddr addr;
    SInt32 idx;

    OS_ASSERT(24, this);

    this->groups = NULL;
    this->lastgrp = NULL;
    this->grpcnt = 0;
    addr.lo = addr.hi = 0;

    grp = OvlGroup_New("main_application", addr);
    if (!grp) {
        return FALSE;
    }

    ovl = Overlay_New("MAIN");
    if (!ovl) {
        return FALSE;
    }

    OvlGroup_AddOverlay(grp, ovl, &idx);
    OS_ASSERT(42, idx==0);

    Overlays_AddOvlGroup(this, grp, &idx);
    OS_ASSERT(45, idx==0);

    return TRUE;
}

Boolean Overlays_Terminate(Overlays* this) {
    OvlGroup* grp;
    OvlGroup* nxtgrp;

    OS_ASSERT(54, this);

    for (grp = this->groups; grp; grp = nxtgrp) {
        nxtgrp = grp->next;
        OvlGroup_Delete(grp);
        xfree(grp);
    }

    this->groups = NULL;
    return TRUE;
}

Boolean Overlays_AddOvlGroup(Overlays* this, OvlGroup* grp, SInt32* grpnum) {
    OS_ASSERT(70, this);
    OS_ASSERT(71, grp);

    if (this->groups == NULL) {
        this->groups = grp;
    } else {
        this->lastgrp->next = grp;
    }
    this->lastgrp = grp;

    if (grpnum) {
        *grpnum = this->grpcnt;
    }
    this->grpcnt++;

    return TRUE;
}

OvlGroup* Overlays_GetOvlGroup(Overlays* this, SInt32 grpnum) {
    OvlGroup* grp;
    SInt32 cnt = 0;

    OS_ASSERT(93, this);

    grp = this->groups;
    while (grp && cnt < grpnum) {
        grp = grp->next;
        cnt++;
    }

    if (cnt == grpnum) {
        return grp;
    } else {
        return NULL;
    }
}

SInt32 Overlays_CountGroups(Overlays* this) {
    OvlGroup* scan;
    int num = 0;

    OS_ASSERT(112, this);

    scan = this->groups;
    while (scan) {
        num++;
        scan = scan->next;
    }

    return num;
}

// (no caller: the linker dropped it; the asserts' lines after it show the file has it)
Boolean Overlays_AddFileToOverlay(Overlays* this, SInt32 grpnum, SInt32 ovlnum, SInt32 filenum) {
    Overlay* oly;

    OS_ASSERT(130, this);

    oly = Overlays_GetOverlayInGroup(this, grpnum, ovlnum);
    if (oly) {
        return Overlay_AddFile(oly, filenum, NULL);
    } else {
        return FALSE;
    }
}

Overlay* Overlays_GetOverlayInGroup(Overlays* this, SInt32 grpnum, SInt32 ovlnum) {
    OvlGroup* grp;

    OS_ASSERT(144, this);

    grp = Overlays_GetOvlGroup(this, grpnum);
    if (grp) {
        return OvlGroup_GetOverlay(grp, ovlnum);
    } else {
        return NULL;
    }
}

SInt32 Overlays_GetFileInOverlay(Overlays* this, SInt32 grpnum, SInt32 ovlnum, SInt32 filnum) {
    Overlay* oly;

    OS_ASSERT(160, this);

    oly = Overlays_GetOverlayInGroup(this, grpnum, ovlnum);
    if (oly) {
        return Overlay_GetFile(oly, filnum);
    } else {
        return -1;
    }
}

OvlGroup* OvlGroup_New(const char* name, OvlAddr addr) {
    OvlGroup* grp;

    OS_ASSERT(175, name);

    grp = xmalloc(NULL, sizeof(OvlGroup));
    if (grp) {
        strncpy(grp->name, name, sizeof(grp->name));
        grp->name[sizeof(grp->name) - 1] = 0;
        grp->addr = addr;
        grp->olys = grp->lastoly = NULL;
        grp->olycnt = 0;
        grp->next = NULL;
    } else {
        DO_INTERNAL_ERROR(188, "Could not allocate %s", "overlay group");
    }
    return grp;
}

void OvlGroup_Delete(OvlGroup* grp) {
    Overlay* scan;
    Overlay* next;

    OS_ASSERT(197, grp);

    for (scan = grp->olys; scan; scan = next) {
        next = scan->next;
        Overlay_Delete(scan);
        xfree(scan);
    }

    grp->olys = NULL;
}

Boolean OvlGroup_AddOverlay(OvlGroup* this, Overlay* oly, SInt32* olynum) {
    OS_ASSERT(211, this);
    OS_ASSERT(212, oly);

    if (!this->lastoly) {
        this->olys = oly;
    } else {
        this->lastoly->next = oly;
    }
    this->lastoly = oly;

    if (olynum) {
        *olynum = this->olycnt;
    }
    this->olycnt++;

    return TRUE;
}

Overlay* OvlGroup_GetOverlay(OvlGroup* this, SInt32 olynum) {
    Overlay* oly;
    SInt32 cnt = 0;

    OS_ASSERT(234, this);

    oly = this->olys;
    while (oly && cnt < olynum) {
        oly = oly->next;
        cnt++;
    }

    if (cnt == olynum) {
        return oly;
    } else {
        return NULL;
    }
}

SInt32 OvlGroup_CountOverlays(OvlGroup* this) {
    Overlay* scan;
    int num = 0;

    OS_ASSERT(254, this);

    scan = this->olys;
    while (scan) {
        num++;
        scan = scan->next;
    }

    return num;
}

Overlay* Overlay_New(const char* name) {
    Overlay* oly;

    oly = xmalloc(NULL, sizeof(Overlay));
    if (oly) {
        strncpy(oly->name, name, sizeof(oly->name));
        oly->name[sizeof(oly->name) - 1] = 0;
        oly->list = NULL;
        oly->cnt = oly->max = 0;
        oly->next = NULL;
    } else {
        DO_INTERNAL_ERROR(281, "Could not allocate %s", "overlay");
    }
    return oly;
}

void Overlay_Delete(Overlay* oly) {
    OS_ASSERT(288, oly);

    if (oly->list) {
        xfree(oly->list);
    }
    oly->list = NULL;
}

Boolean Overlay_AddFile(Overlay* oly, SInt32 filenum, SInt32* filnum) {
    OS_ASSERT(296, oly);

    if (oly->cnt >= oly->max) {
        oly->max += 16;
        oly->list = xrealloc("overlay file list", oly->list, sizeof(SInt32) * oly->max);
    }
    oly->list[oly->cnt] = filenum;

    if (filnum) {
        *filnum = oly->cnt;
    }
    oly->cnt++;

    return TRUE;
}

SInt32 Overlay_GetFile(Overlay* oly, SInt32 filnum) {
    OS_ASSERT(314, oly);

    if (filnum < oly->cnt) {
        return oly->list[filnum];
    } else {
        return -1;
    }
}

SInt32 Overlay_CountFiles(Overlay* oly) {
    OS_ASSERT(323, oly);

    return oly->cnt;
}
