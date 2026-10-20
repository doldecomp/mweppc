#ifndef DRIVER_CWSECRETPLUGINCALLBACKS_H
#define DRIVER_CWSECRETPLUGINCALLBACKS_H

#include "compiler/common.h"
#include "driver/CWPlugins.h"

#ifdef __cplusplus
extern "C" {
#endif

extern CW_CALLBACK CWSecretAttachHandle(
    CWPluginContext context, Handle handle, CWMemHandle* memHandle
);
extern CW_CALLBACK CWSecretGetNamedPreferences(
    CWPluginContext context, const char* prefsname, Handle* prefsdata
);

#ifdef __cplusplus
}
#endif

#endif
