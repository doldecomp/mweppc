#include "driver/CLWriteObjectFile.h"
#include "driver/CLBrowser.h"
#include "driver/CLErrors.h"
#include "driver/CLIO.h"
#include "driver/CLMain.h"
#include "driver/CLPlugins.h"
#include "driver/cmdline.h"
#include "driver/CWPlugins.h"
#include "driver/Files.h"
#include "driver/Generic.h"
#include "driver/MacSpecs.h"
#include "driver/oslib.h"
#include "driver/pref_structs.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __MWERKS__
int WriteObjectFile(File* file, OSType maccreator, OSType mactype) {
    FSSpec srcfss;
    FSSpec outfss;

    OS_ASSERT(22, file->objectdata && file->compiler);

    OS_OSSpec_To_FSSpec(&file->outfss, &outfss);
    OS_OSSpec_To_FSSpec(&file->srcfss, &srcfss);

    if (optsCmdLine.verbose) {
        CLReport(
            CLStr16,
            (file->tempOnDisk & CmdLineStageMask_Cg) ? "temporary " : "",
            OS_SpecToStringRelative(&file->outfss, NULL, STSbuf, sizeof(STSbuf))
        );
    }

    if (!Plugin_CL_WriteObjectFile(
            file->compiler, &srcfss, &outfss, maccreator, mactype, file->objectdata
        )) {
        return FALSE;
    } else {
        return TRUE;
    }
}
#else
// An output name of 64 characters or more overruns outfss.name (OS_OSSpec_To_FSSpec copies up to
// 255), a source name of 64 or 65 srcfss.name. The original's frame has outfss 0x48 bytes after
// srcfss and ends 2 bytes after outfss: an output name of up to 73 characters is written whole
// (past 65 over the saved registers), a longer one overwrites the return address and crashes the
// original after the object is written. The tail keeps the rest of a name here.
int WriteObjectFile(File* file, OSType maccreator, OSType mactype) {
    struct {
        FSSpec srcfss;
        UInt8 gap[2];
        FSSpec outfss;
        UInt8 tail[192];
    } frame;

    OS_ASSERT(22, file->objectdata && file->compiler);

    OS_OSSpec_To_FSSpec(&file->outfss, &frame.outfss);
    OS_OSSpec_To_FSSpec(&file->srcfss, &frame.srcfss);

    if (optsCmdLine.verbose) {
        CLReport(
            CLStr16,
            (file->tempOnDisk & CmdLineStageMask_Cg) ? "temporary " : "",
            OS_SpecToStringRelative(&file->outfss, NULL, STSbuf, sizeof(STSbuf))
        );
    }

    if (!Plugin_CL_WriteObjectFile(
            file->compiler, &frame.srcfss, &frame.outfss, maccreator, mactype, file->objectdata
        )) {
        return FALSE;
    } else {
        return TRUE;
    }
}
#endif

int WriteBrowseData(File* file, OSType maccreator, OSType mactype) {
    OSHandle browsehandle;
    OSSpec outfss;
    const CWObjectFlags* cof;

    cof = Plugin_CL_GetObjectFlags(file->compiler);
    outfss = file->outfss;

    OS_NameSpecSetExtension(
        &outfss.name, optsCompiler.brsFileExt[0] ? optsCompiler.brsFileExt : cof->brsFileExt
    );
    if (optsCmdLine.verbose) {
        CLReport(CLStr17, OS_SpecToStringRelative(&outfss, NULL, STSbuf, sizeof(STSbuf)));
    }

    if (!Browser_PackBrowseFile(file->browsedata, &clState.browseTableHandle, &browsehandle)) {
        return FALSE;
    }

    if (!WriteBinaryHandleToFile(&outfss, maccreator, mactype, &browsehandle)) {
        return FALSE;
    }
    return TRUE;
}
