#ifndef DRIVER_CLPREFS_H
#define DRIVER_CLPREFS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// A named preference panel (defined in CLPrefs.c)
typedef struct PrefPanel PrefPanel;

extern PrefPanel* PrefPanel_New(const char* name, void* initdata, SInt32 initdatasize);
extern Handle PrefPanel_GetHandle(PrefPanel* panel);
extern int PrefPanel_PutHandle(PrefPanel* panel, Handle data);
extern Boolean Prefs_AddPanel(PrefPanel* panel);
extern PrefPanel* Prefs_FindPanel(const char* name);

#ifdef __cplusplus
}
#endif

#endif
