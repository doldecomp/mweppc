#include "driver/ToolHelpers-cc.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"
#include "driver/ParserHelpers.h"
#include "driver/pref_structs.h"
#include "driver/Projects.h"
#include "driver/ToolHelpers.h"
#include <string.h>

int Opt_DummyLinkerRoutine(const char* opt, void* var, const char* arg, int flags) {
    CLPFatalError("Calling linker option '%s'\n", opt);
    return FALSE;
}

// NOTE: invented names: name, val
// (no caller: the linker dropped it, but its string stays in the unit's .data)
int Opt_DummyLinkerSettingRoutine(const char* name, const char* val) {
    CLPFatalError("Calling linker settings option '%s'='%s'\n", name, val ? val : "");
    return FALSE;
}

void FinishCompilerTool(void) {
    SInt32 numfiles;

    if (parseopts.lastoutputname[0]) {
        numfiles = GetFileCount();

        if (pCmdLine.stages == CmdLineStageMask_Dp) {
            strcpy(pCmdLineCompiler.outMakefile, parseopts.lastoutputname);
        } else if (outputOrdering == OutputOrdering_Before) {
            if (parseopts.possibleFiles > 0 || parseopts.userSpecifiedFiles > 0) {
                CLPReportError(CLPStr41, parseopts.lastoutputname);
            } else {
                CLPReportError(CLPStr42, parseopts.lastoutputname);
            }
        } else {
            SetFileOutputName(numfiles - 1, lastStage, parseopts.lastoutputname);
        }

        parseopts.lastoutputname[0] = 0;
    }

    if (setOutputDirectory) {
        pCmdLineCompiler.relPathInOutputDir = FALSE;
    }
}
