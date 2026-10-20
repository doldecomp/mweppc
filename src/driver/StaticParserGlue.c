#include "driver/StaticParserGlue.h"
#include "driver/ClientGlue.h"
#include "driver/CWPlugins.h"
#include "driver/ParserFace.h"

// clang-format off
static BasePluginCallbacks cb = {
    parser_main,
    CWParser_GetDropInFlags,
    CWParser_GetDisplayName,
    CWParser_GetDropInName,
    CWParser_GetPanelList,
    NULL,
    NULL,
    CWParser_GetVersionInfo,
    NULL
};
// clang-format on

// clang-format off
static ParserPluginCallbacks pr_cb = {
    Parser_SupportsPlugin,
    Parser_SupportsPanels
};
// clang-format on

int RegisterStaticParserPlugins(void) {
    return RegisterStaticParserPlugin(&cb, &pr_cb);
}

#include "ParserErrors.r"

int RegisterStaticParserResources(void) {
    return RegisterResource("Parser Strings", 12010, STR12010);
}
