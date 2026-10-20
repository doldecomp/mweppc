#include "driver/Targets.h"
#include "driver/CWPlugins.h"
#include "driver/Option.h"
#include "driver/oslib.h"
#include "driver/parser.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"
#include "driver/StringExtras.h"

ParserTool* pTool;

int SetParserToolInfo(ParserTool* tool) {
    pTool = tool;

    OPTION_ASSERT(16, pTool->toolInfo && (parseopts.toolVersion || pTool->copyright));

    return TRUE;
}

Boolean ParserToolMatchesPlugin(OSType type, OSType lang, OSType cpu, OSType os) {
    if (!pTool) {
        CLPFatalError("No options loaded for command line\n");
        return FALSE;
    }

    if ((type == CWDROPINANYTYPE || pTool->TYPE == CWDROPINANYTYPE || pTool->TYPE == type) &&
        (lang == Lang_Any || pTool->LANG == Lang_Any || pTool->LANG == lang) &&
        (cpu == targetCPUAny || pTool->CPU == targetCPUAny || pTool->CPU == cpu) &&
        (os == targetOSAny || pTool->OS == targetOSAny || pTool->OS == os)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

Boolean ParserToolHandlesPanels(int numPanels, const char** panelNames) {
    int idx;
    int scan;

    if (!pTool) {
        CLPFatalError("No options loaded for command line\n");
    }

    for (idx = 0; idx < numPanels; idx++) {
        for (scan = 0; scan < pTool->numPrefPanels; scan++) {
            if (!ustrcmp(pTool->prefPanels[scan], panelNames[idx])) {
                break;
            }
        }

        if (scan >= pTool->numPrefPanels) {
            break;
        }
    }

    if (idx >= numPanels) {
        return TRUE;
    } else {
        return FALSE;
    }
}

Boolean SetupParserToolOptions(void) {
    int idx;

    Options_Init();
    for (idx = 0; idx < pTool->numOptionLists; idx++) {
        Options_AddList(pTool->optionLists[idx]);
    }
    Options_SortOptions();

    return TRUE;
}
