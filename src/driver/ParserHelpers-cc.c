#include "driver/ParserHelpers-cc.h"
#include "driver/CWPlugins.h"
#include "driver/oslib.h"
#include "driver/parser.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"
#include "driver/ParserHelpers.h"
#include "driver/Targets.h"
#include <stdio.h>
#include <string.h>

Handle definesHandle;

int Opt_AddStringToDefines(const char* opt, void* str, const char* param, int flags) {
    AddStringToHandle(&definesHandle, (const char*)str);
    if (param) {
        AddStringToHandle(&definesHandle, param);
    }
    return TRUE;
}

// NOTE: invented names: name, value, line
int Opt_DefineSymbol(const char* name, const char* value) {
    char line[1024];

    if (pTool->LANG == Lang_C_CPP || pTool->LANG == 'Asm ' || pTool->LANG == Lang_Rez) {
        sprintf(line, "#define %s %s\n", name, value ? value : "1");
    } else if (pTool->LANG == Lang_Pascal) {
        sprintf(line, "{$definec %s %s}\n", name, value ? value : "1");
    } else {
        sprintf(line, "Option '-D|d' is not supported with this plugin");
        CLPReportError(CLPStr28_WarningText, line);
        return FALSE;
    }

    AddStringToHandle(&definesHandle, line);
    return TRUE;
}

int Opt_UndefineSymbol(const char* opt, void* var, const char* arg, int flags) {
    char line[300]; // NOTE: invented name

    if (pTool->LANG == Lang_C_CPP || pTool->LANG == 'Asm ') {
        sprintf(line, "#undef %s\n", arg);
    } else if (pTool->LANG == Lang_Pascal) {
        sprintf(line, "{$undefc %s}\n", arg);
    } else {
        sprintf(line, "Option -%s is not supported with this plugin", opt);
        CLPReportError(CLPStr28_WarningText, line);
        return FALSE;
    }

    AddStringToHandle(&definesHandle, line);
    return TRUE;
}

int Opt_AddPrefixFile(const char* opt, void* handle, const char* filename, int flags) {
    char line[300]; // NOTE: invented name
    Handle* storage;

    if (!handle) {
        storage = &definesHandle;
    } else {
        storage = (Handle*)handle;
    }

    if (!filename[0]) {
        return TRUE;
    }

    if (pTool->LANG == Lang_C_CPP || pTool->LANG == 'Asm ') {
        if (filename[0] == '<' && filename[strlen(filename) - 1] == '>') {
            snprintf(line, sizeof(line), "#include %s\n", filename);
        } else {
            snprintf(line, sizeof(line), "#include \"%s\"\n", filename);
        }
    } else if (pTool->LANG == Lang_Pascal) {
        snprintf(line, sizeof(line), "{$I+}\n{$I %s}\n{$I-}\n", filename);
    } else {
        sprintf(line, "Option -%s is not supported with this plugin", opt);
        CLPReportError(CLPStr28_WarningText, line);
        return FALSE;
    }

    AddStringToHandle(storage, line);
    return TRUE;
}

int SetupPragmas(const Pragma* pragmas) {
    const char* set;
    char on;
    char off;
    Boolean reverse;
    char line[300]; // NOTE: invented name

    while (pragmas->pragma) {
        if (pragmas->flags == PRAGMA_FLAGS_ONOFF || pragmas->flags == PRAGMA_FLAGS_REVERSED) {
            set = NULL;
            reverse = pragmas->flags == PRAGMA_FLAGS_REVERSED;
            on = !reverse ? PR_ON : PR_OFF;
            off = !reverse ? PR_OFF : PR_ON;
            if (*((char*)pragmas->value) == on) {
                set = "on";
            } else if (*((char*)pragmas->value) == off) {
                set = "off";
            } else if (*((char*)pragmas->value) == PR_AUTO) {
                set = "auto";
            } else if (*((char*)pragmas->value) == PR_RESET) {
                set = "reset";
            } else {
                OS_ASSERT(186, *((char *)pragmas->value) == PR_UNSET);
            }

            if (set) {
                snprintf(line, sizeof(line), "#pragma %s %s\n", pragmas->pragma, set);
                AddStringToHandle(&definesHandle, line);
            }
        } else {
            OS_ASSERT(195, !"Can't handle pragma");
        }
        pragmas++;
    }

    return TRUE;
}
