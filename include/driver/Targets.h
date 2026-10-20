#ifndef DRIVER_TARGETS_H
#define DRIVER_TARGETS_H

#include "compiler/common.h"
#include "driver/oslib.h"
#include "driver/parser.h"

#ifdef __cplusplus
extern "C" {
#endif

extern ParserTool* pTool;

extern int SetParserToolInfo(ParserTool* tool);
extern Boolean ParserToolMatchesPlugin(OSType type, OSType lang, OSType cpu, OSType os);
extern Boolean ParserToolHandlesPanels(int numPanels, const char** panelNames);
extern Boolean SetupParserToolOptions(void);

#ifdef __cplusplus
}
#endif

#endif
