#include "driver/IO.h"
#include "driver/Arguments.h"
#include "driver/CWParserPluginsPrivate.h"
#include "driver/CWPlugins.h"
#include "driver/CWSecretPluginCallbacks.h"
#include "driver/Generic.h"
#include "driver/Memory.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"
#include "driver/StringUtils.h"
#include "driver/Targets.h"
#include <stdio.h>
#include <stdlib.h>

void ShowTextHandle(const char* description, Handle text) {
    CWMemHandle mh;

    if (!text) {
        return;
    }

    if (description) {
        CLPStatus(CLPStr71, description);
    }
    CWSecretAttachHandle(parseopts.context, text, &mh);
    CWParserDisplayTextHandle(parseopts.context, description, mh);
}

void ShowVersion(Boolean decorate) {
    char* vplugin;
    char* valtplugin;
    char* vdriver;
    char* vparser;
    const char* bdate;
    const char* btime;
    Handle txt;
    int x;

    vdriver = 0;
    vparser = 0;
    vplugin = 0;
    valtplugin = 0;
    if (parseopts.printedVersion) {
        return;
    }

    txt = NewHandle(0);
    if (!txt) {
        fprintf(stderr, "\n*** Out of memory\n");
        exit(-23);
    }

    for (x = 0; x < parseopts.numPlugins; x++) {
        if (parseopts.plugins[x].plugintype == CWDROPINDRIVERTYPE) {
            vdriver = parseopts.plugins[x].version;
        } else if (parseopts.plugins[x].plugintype == CWDROPINPARSERTYPE) {
            vparser = parseopts.plugins[x].version;
        } else if (pTool->TYPE == parseopts.plugins[x].plugintype) {
            if (!vplugin && pTool->LANG == parseopts.plugins[x].language) {
                vplugin = parseopts.plugins[x].version;
            } else {
                valtplugin = parseopts.plugins[x].version;
            }
        }
    }

    CWParserGetBuildDate(parseopts.context, &bdate, &btime);
    HPrintF(txt, "\n");
    if (parseopts.toolVersion) {
        HPrintF(
            txt,
            "%s.\n%s, %s\nAll rights reserved.\n%s\n",
            pTool->toolInfo,
            parseopts.toolVersion->copyright,
            parseopts.toolVersion->company,
            parseopts.toolVersion->version
        );
    } else {
        HPrintF(
            txt,
            "%s.\nCopyright (c)%s Metrowerks, Inc.\nAll rights reserved.\n%s\n",
            pTool->toolInfo,
            pTool->copyright,
            vplugin          ? vplugin
                : valtplugin ? valtplugin
                             : ""
        );
    }

    HPrintF(txt, "Runtime Built: %s %s\n", bdate, btime);
    HPrintF(txt, "\n");

    if (decorate) {
        HPrintF(
            txt,
            "Please enter '%s %chelp' for information about options.\n\n",
            OS_GetFileNamePtr(parseopts.args->argv[0]),
            MAINOPTCHAR[0]
        );
    }

    ShowTextHandle(0, txt);
    DisposeHandle(txt);

    parseopts.printedVersion = TRUE;
}
