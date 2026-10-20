// The callbacks only Metrowerks' own plugins call, through the shell's cbInternal entries. There is
// no CWSecretPeekHandle (nothing calls it; Wii's module lacks it too).
#include "driver/CWSecretPluginCallbacks.h"
#include "driver/CWPlugins.h"
#include "driver/CWPluginsPrivate.h"

// (CWPluginsPrivate.cpp has a ValidateContext too; config/'s function names are unique across units)
#define ValidateContext CWSecretPluginCallbacks_ValidateContext

extern "C" {

typedef CW_CALLBACK_PTR (*cbSecretAttachHandleType)(CWPluginContext, Handle, CWMemHandle*);
typedef CW_CALLBACK_PTR (*cbSecretDetachHandleType)(CWPluginContext, CWMemHandle, Handle*);

static Boolean ValidateContext(CWPluginContext context) {
    return context && (context->shellSignature == 'CWIE');
}

CW_CALLBACK CWSecretAttachHandle(CWPluginContext context, Handle handle, CWMemHandle* memHandle) {
    if (!ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    return ((cbSecretAttachHandleType)context->callbacks->cbInternal[0])(
        context, handle, memHandle
    );
}

CW_CALLBACK CWSecretDetachHandle(CWPluginContext context, CWMemHandle memHandle, Handle* handle) {
    if (!ValidateContext(context)) {
        return cwErrInvalidParameter;
    }
    return ((cbSecretDetachHandleType)context->callbacks->cbInternal[1])(
        context, memHandle, handle
    );
}

CW_CALLBACK CWSecretGetNamedPreferences(
    CWPluginContext context, const char* prefsname, Handle* prefsdata
) {
    if (!prefsdata) {
        return cwErrInvalidParameter;
    }

    CWMemHandle memHandle;
    CWResult res = CWGetNamedPreferences(context, prefsname, &memHandle);
    if (!res) {
        res = CWSecretDetachHandle(context, memHandle, prefsdata);
    }
    return res;
}
}
