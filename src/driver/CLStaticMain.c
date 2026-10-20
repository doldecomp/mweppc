#include "driver/cc-eabi-ppc-mw.h"
#include "driver/ClientGlue.h"
#include "driver/CmdLineBuildDate.h"
#include "driver/oslib.h"
#include "driver/ParserGlue-eabi-ppc-cc.h"
#include "driver/StaticParserGlue.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    // (`lang` takes the parser style too)
    OSType lang;
    OSType type;
    OSType cpu;
    OSType os;
    int ret;

    if (CmdLine_Initialize(argc, argv, CMDLINE_BUILD_DATE, CMDLINE_BUILD_TIME)) {
        exit(1);
    }

    if (!RegisterStaticParserResources() || !RegisterStaticTargetResources()) {
        fprintf(stderr, "\r\nFATAL ERROR:  Could not initialize resource strings\r\n");
        exit(1);
    }

    if (!RegisterStaticParserPlugins() || !RegisterStaticTargetPlugins()) {
        fprintf(stderr, "\r\nFATAL ERROR:  Could not initialize built-in plugins\r\n");
        exit(1);
    }

    if (!RegisterStaticParserToolInfo()) {
        fprintf(stderr, "\r\nFATAL ERROR:  Could not initialize options\r\n");
        exit(1);
    }

    GetStaticTarget(&cpu, &os);
    SetBuildTarget(cpu, os);
    GetStaticPluginType(&lang, &type);
    SetPluginType(lang, type);
    GetStaticParserPluginType(&lang);
    SetParserType(lang);

    ret = CmdLine_Driver();
    if (ret) {
        if (ret == 2) {
            fprintf(stderr, "\r\nUser break, cancelled...\r\n");
        } else {
            fprintf(stderr, "\r\nErrors caused tool to abort.\r\n");
        }
    }
    CmdLine_Terminate(ret);
    return ret;
}
