#ifndef DRIVER_CLIO_H
#define DRIVER_CLIO_H

#include "compiler/common.h"
#include "driver/cmdline.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    Msg_Note = 1,
    Msg_Warning = 2,
    Msg_Error = 3,
    Msg_Alert = 4,
    Msg_Status = 5
};

typedef struct MessageRef {
    OSSpec sourcefile;
    OSSpec errorfile;
    char* sourceline;
    SInt32 linenumber;
    SInt32 tokenoffset;
    SInt16 tokenlength;
    SInt32 selectionoffset;
    SInt16 selectionlength;
} MessageRef;

extern void SetupDebuggingTraps(void);
extern Boolean IO_Initialize(void);
extern Boolean IO_Terminate(void);
extern Boolean ShowHandle(Handle text, UInt32 size, Boolean decorate);
extern Boolean WriteHandleToFile(
    OSSpec* spec, Handle text, UInt32 size, OSType creator, OSType type
);
extern Boolean WriteBinaryHandleToFile(
    OSSpec* spec, OSType maccreator, OSType mactype, OSHandle* text
);
extern Boolean AppendHandleToFile(
    OSSpec* spec, Handle text, UInt32 size, OSType maccreator, OSType mactype
);
extern void ShowWorking(int x);
extern Boolean CheckForUserBreak(void);
extern void CLPrint(const char* format, ...);
extern void CLPrintErr(const char* format, ...);
extern SInt16 CLStyledMessageDispatch(
    Plugin* plugin, MessageRef* ref, SInt32 errorNumber, SInt16 msgType, const char* format, ...
);

#ifdef __cplusplus
}
#endif

#endif
