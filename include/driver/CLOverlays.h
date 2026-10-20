#ifndef DRIVER_CLOVERLAYS_H
#define DRIVER_CLOVERLAYS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OvlAddr {
    UInt32 lo;
    UInt32 hi;
} OvlAddr;
typedef struct Overlay {
    char name[256];
    SInt32* list;
    SInt32 cnt;
    SInt32 max;
    struct Overlay* next;
} Overlay;
typedef struct OvlGroup {
    char name[256];
    OvlAddr addr;
    Overlay* olys;
    Overlay* lastoly;
    int olycnt;
    struct OvlGroup* next;
} OvlGroup;
typedef struct Overlays {
    OvlGroup* groups;
    OvlGroup* lastgrp;
    SInt32 grpcnt;
} Overlays;
extern Boolean Overlays_Initialize(Overlays* overlays);
extern Boolean Overlays_Terminate(Overlays* overlays);
extern Boolean Overlays_AddOvlGroup(Overlays* overlays, OvlGroup* grp, SInt32* grpnum);
extern OvlGroup* Overlays_GetOvlGroup(Overlays* overlays, SInt32 grpnum);
extern SInt32 Overlays_CountGroups(Overlays* overlays);
extern Overlay* Overlays_GetOverlayInGroup(Overlays* overlays, SInt32 grpnum, SInt32 ovlnum);
extern SInt32 Overlays_GetFileInOverlay(
    Overlays* overlays, SInt32 grpnum, SInt32 ovlnum, SInt32 filnum
);
extern OvlGroup* OvlGroup_New(const char* name, OvlAddr addr);
extern Boolean OvlGroup_AddOverlay(OvlGroup* grp, Overlay* oly, SInt32* olynum);
extern Overlay* OvlGroup_GetOverlay(OvlGroup* grp, SInt32 olynum);
extern SInt32 OvlGroup_CountOverlays(OvlGroup* grp);
extern Overlay* Overlay_New(const char* name);
extern Boolean Overlay_AddFile(Overlay* oly, SInt32 filenum, SInt32* filnum);
extern SInt32 Overlay_CountFiles(Overlay* oly);

#ifdef __cplusplus
}
#endif

#endif
