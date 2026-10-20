#include "driver/CLPrefs.h"
#include "driver/CLErrors.h"
#include "driver/CLIO.h"
#include "driver/CLMain.h"
#include "driver/Memory.h"
#include "driver/MemUtils.h"
#include "driver/oslib.h"
#include "driver/StringExtras.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PrefPanel {
    char* name;
    Handle data;
    Handle workHandle;
    PrefPanel* next;
};

static PrefPanel* panellist;

PrefPanel* PrefPanel_New(const char* name, void* initdata, SInt32 initdatasize) {
    PrefPanel* pnl;
    pnl = xmalloc(NULL, sizeof(PrefPanel));
    if (!pnl) {
        return pnl;
    }

    pnl->name = xstrdup(name);
    pnl->data = NewHandle(initdatasize);
    if (!pnl->data) {
        fprintf(stderr, "\n*** Out of memory\n");
        exit(-23);
    }

    pnl->workHandle = NULL;

    HLock(pnl->data);
    if (initdata) {
        memcpy(*pnl->data, initdata, initdatasize);
    } else {
        memset(*pnl->data, 0, initdatasize);
    }
    HUnlock(pnl->data);

    pnl->next = NULL;
    return pnl;
}

Handle PrefPanel_GetHandle(PrefPanel* panel) {
    UInt32 len = GetHandleSize(panel->data);
    if (!panel->workHandle || !*panel->workHandle) {
        panel->workHandle = NewHandle(len);
    } else {
        SetHandleSize(panel->workHandle, len);
    }

    OS_ASSERT(60, MemError()==noErr);

    HLock(panel->data);
    HLock(panel->workHandle);
    memcpy(*panel->workHandle, *panel->data, len);
    HUnlock(panel->workHandle);
    HUnlock(panel->data);
    return panel->workHandle;
}

int PrefPanel_PutHandle(PrefPanel* panel, Handle data) {
    UInt32 len = GetHandleSize(data);
    if (data != panel->workHandle) {
        if (!panel->workHandle) {
            panel->workHandle = NewHandle(len);
        } else {
            SetHandleSize(panel->workHandle, len);
        }
        if (MemError() != noErr) {
            return FALSE;
        }

        HLock(panel->workHandle);
        HLock(data);
        memcpy(*panel->workHandle, *data, len);
        HUnlock(data);
        HUnlock(panel->workHandle);
    }

    HLock(panel->data);
    HLock(panel->workHandle);
    memcpy(*panel->data, *panel->workHandle, len);
    HUnlock(panel->workHandle);
    HUnlock(panel->data);
    return TRUE;
}

void Prefs_Initialize(void) {
    panellist = NULL;
}

void Prefs_Terminate(void) {
    PrefPanel* scan;
    PrefPanel* next;

    for (scan = panellist; scan; scan = next) {
        xfree(scan->name);
        DisposeHandle(scan->data);
        next = scan->next;
        xfree(scan);
    }
}

Boolean Prefs_AddPanel(PrefPanel* panel) {
    PrefPanel** scan;

    for (scan = &panellist; *scan; scan = &(*scan)->next) {
        if (!strcmp((*scan)->name, panel->name)) {
            CLReportError(CLStr90, panel->name);
            return FALSE;
        }
    }

    if (clState.pluginDebug) {
        CLPrint("Defining/adding pref panel '%s'\n", panel->name);
    }

    *scan = panel;
    return TRUE;
}

PrefPanel* Prefs_FindPanel(const char* name) {
    PrefPanel* scan;

    for (scan = panellist; scan; scan = scan->next) {
        if (!ustrcmp(scan->name, name)) {
            break;
        }
    }

    return scan;
}
