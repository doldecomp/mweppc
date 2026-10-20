#include "driver/WarningHelpers.h"
#include "driver/IO.h"
#include "driver/Memory.h"
#include "driver/option_system.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"
#include "driver/pref_structs.h"
#include "driver/StdTargetWarningHelpers-cc.h"
#include "driver/StringUtils.h"
#include <stdlib.h>

int SetWarningFlags(const char* opt, void* str, const char* param, int flags) {
    unsigned char* ptr;
    Boolean set;
    Boolean no;
    UInt16 flag;
    int minus;
    int bar;

    ptr = (unsigned char*)str;
    if (flags & PARAMPARSEFLAGS_NEGATED) {
        no = TRUE;
    } else {
        no = FALSE;
    }
    set = bar = (minus = no) ^ 1;

    while (*ptr) {
        if (*ptr == '+') {
            set = !no;
        } else if (*ptr == '-') {
            set = minus;
        } else if (*ptr == '|') {
            set = bar;
        } else {
            flag = (ptr[0] << 8) | ptr[1];

            if (set) {
                pCmdLine.noWarnings = FALSE;
            }

            switch (flag) {
                case 'Nw':
                    pCmdLine.noWarnings = set;
                    break;
                case 'Aw':
                    TargetSetWarningFlags(flag, set);
                    break;
                case 'Cw':
                    pCmdLine.noCmdLineWarnings = !set;
                    break;
                case 'We':
                    pCmdLine.warningsAreErrors = set;
                    TargetSetWarningFlags(flag, set);
                    break;
                default:
                    if (!TargetSetWarningFlags(flag, set)) {
                        CLPFatalError("Bad warning settings in %s (%c%c)\n", str, ptr[0], ptr[1]);
                    }
            }

            ++ptr;
        }

        ++ptr;
    }

    Parser_StorePanels(parseopts.context);

    return TRUE;
}

int DisplayWarningOptions(const char* opt, void* str, const char* param, int flags) {
    Handle h;

    h = NewHandle(0);
    if (!h) {
        exit(-23);
    }

    HPrintF(h, "Command-line warning options:\n");

    if (pCmdLine.noCmdLineWarnings) {
        HPrintF(h, "\t- no command-line warnings\n");
    } else {
        HPrintF(h, "\t- command-line warnings\n");
    }

    if (pCmdLine.warningsAreErrors) {
        HPrintF(h, "\t- warnings are errors\n");
    } else {
        HPrintF(h, "\t- warnings are not errors\n");
    }

    if (pCmdLine.noWarnings) {
        HPrintF(h, "\t- no warnings at all\n");
    }

    TargetDisplayWarningOptions(h);
    ShowTextHandle(NULL, h);
    DisposeHandle(h);
    return TRUE;
}
