#include "driver/OptimizerHelpers.h"
#include "driver/IO.h"
#include "driver/Memory.h"
#include "driver/Option.h"
#include "driver/option_system.h"
#include "driver/parser.h"
#include "driver/ParserErrors.h"
#include "driver/pref_structs.h"
#include "driver/StringUtils.h"
#include "driver/TargetOptimizerHelpers-eabi-ppc.h"
#include <stdlib.h>
#include <string.h>

Boolean setOldOptimizerOptions;

// The old optimizer settings' panel.
typedef struct PIROptimizer { // NOTE: invented name
    Boolean commonsubs;
    Boolean loopinvariants;
    Boolean propagation;
    Boolean deadstore;
    Boolean strengthreduction;
    Boolean deadcode;
    Boolean lifetimes;
} PIROptimizer;

static PIROptimizer pIrOptimizer = {FALSE};

// clang-format off
Pragma irPragmas[] = {
    {&pIrOptimizer.commonsubs, "opt_common_subs", 0},
    {&pIrOptimizer.loopinvariants, "opt_loop_invariants", 0},
    {&pIrOptimizer.propagation, "opt_propagation", 0},
    {&pIrOptimizer.deadstore, "opt_dead_assignments", 0},
    {&pIrOptimizer.deadcode, "opt_dead_code", 0},
    {&pIrOptimizer.strengthreduction, "opt_strength_reduction", 0},
    {&pIrOptimizer.lifetimes, "opt_lifetimes", 0},
    {0, 0, 0}
};
// clang-format on

int SetPragmaOptimizationsToUnspecified(const char* opt, void* str, const char* param, int flags) {
    if (setOldOptimizerOptions) {
        Option_Warning(CLPStr62);
    }

    memset(&pIrOptimizer, 0, sizeof(pIrOptimizer));
    TargetSetPragmaOptimizationsToUnspecified();
    return TRUE;
}

int SetOptFlags(const char* opt, void* str, const char* param, int flags) {
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
    // TODO: a workaround, as in SetWarningFlags: `no` and `no ^ 1` copied into two ints that the
    // '-' and '|' cases read; the original's spelling is unknown
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

            if ((flag >= 'G0' && flag <= 'G4') || (flag == 'Gs') || (flag == 'Gp')) {
                SetPragmaOptimizationsToUnspecified(opt, NULL, NULL, 0);
            }

            switch (flag) {
                case 'Cs':
                    pIrOptimizer.commonsubs = set ? PR_ON : PR_OFF;
                    setOldOptimizerOptions = TRUE;
                    break;
                case 'Li':
                    pIrOptimizer.loopinvariants = set ? PR_ON : PR_OFF;
                    setOldOptimizerOptions = TRUE;
                    break;
                case 'Pr':
                    pIrOptimizer.propagation = set ? PR_ON : PR_OFF;
                    setOldOptimizerOptions = TRUE;
                    break;
                case 'Ds':
                    pIrOptimizer.deadstore = set ? PR_ON : PR_OFF;
                    setOldOptimizerOptions = TRUE;
                    break;
                case 'Sr':
                    pIrOptimizer.strengthreduction = set ? PR_ON : PR_OFF;
                    setOldOptimizerOptions = TRUE;
                    break;
                case 'Dc':
                    pIrOptimizer.deadcode = set ? PR_ON : PR_OFF;
                    setOldOptimizerOptions = TRUE;
                    break;
                case 'Lt':
                    pIrOptimizer.lifetimes = set ? PR_ON : PR_OFF;
                    setOldOptimizerOptions = TRUE;
                    break;
                case 'G0':
                    pGlobalOptimizer.optimizationlevel = set ? 0 : 0;
                    break;
                case 'G1':
                    pGlobalOptimizer.optimizationlevel = set ? 1 : 0;
                    break;
                case 'G2':
                    pGlobalOptimizer.optimizationlevel = set ? 2 : 0;
                    break;
                case 'G3':
                    pGlobalOptimizer.optimizationlevel = set ? 3 : 0;
                    break;
                case 'G4':
                    pGlobalOptimizer.optimizationlevel = set ? 4 : 0;
                    break;
                // TODO: the arms are byte constants (plain 1 and 0 compile otherwise): probably a
                // byte-sized speed/size enum whose names are lost
                case 'Gs':
                    pGlobalOptimizer.optfor = set ? (Boolean)1 : (Boolean)0;
                    break;
                case 'Gp':
                    pGlobalOptimizer.optfor = set ? (Boolean)0 : (Boolean)1;
                    break;
                default:
                    if (!TargetSetOptFlags(flag, set)) {
                        CLPFatalError("Bad optimizer settings in %s (%c%c)\n", str, ptr[0], ptr[1]);
                    }
            }

            ++ptr;
        }

        ++ptr;
    }

    return TRUE;
}

int DisplayOptimizationOptions(const char* opt, void* str, const char* param, int flags) {
    SInt32 oldsize;
    Handle h;

    h = NewHandle(0);
    if (!h) {
        exit(-23);
    }

    HPrintF(h, "\t- global optimizer level %d\n", pGlobalOptimizer.optimizationlevel);
    HPrintF(h, "\t- global optimize for %s\n", (pGlobalOptimizer.optfor == 0) ? "speed" : "size");

    oldsize = GetHandleSize(h);
    if (pIrOptimizer.commonsubs) {
        HPrintF(
            h,
            "\t- common subexpression elimination %s\n",
            (pIrOptimizer.commonsubs == 1) ? "on" : "off"
        );
    }
    if (pIrOptimizer.loopinvariants) {
        HPrintF(h, "\t- loop invariants %s\n", (pIrOptimizer.loopinvariants == 1) ? "on" : "off");
    }
    if (pIrOptimizer.propagation) {
        HPrintF(h, "\t- constant propagation %s\n", (pIrOptimizer.propagation == 1) ? "on" : "off");
    }
    if (pIrOptimizer.deadstore) {
        HPrintF(h, "\t- dead store elimination %s\n", (pIrOptimizer.deadstore == 1) ? "on" : "off");
    }
    if (pIrOptimizer.deadcode) {
        HPrintF(h, "\t- dead code elimination %s\n", (pIrOptimizer.deadcode == 1) ? "on" : "off");
    }
    if (pIrOptimizer.strengthreduction) {
        HPrintF(
            h, "\t- strength reduction %s\n", (pIrOptimizer.strengthreduction == 1) ? "on" : "off"
        );
    }
    if (pIrOptimizer.lifetimes) {
        HPrintF(h, "\t- variable lifetimes %s\n", (pIrOptimizer.lifetimes == 1) ? "on" : "off");
    }

    if (oldsize == GetHandleSize(h)) {
        HPrintF(h, "\t- no extra global optimizations\n");
    }

    HPrintF(h, "Backend-specific optimizer options:\n");

    oldsize = GetHandleSize(h);
    TargetDisplayOptimizationOptions(h);
    if (oldsize == GetHandleSize(h)) {
        HPrintF(h, "\t- no extra backend-specific optimizations\n");
    }

    ShowTextHandle(NULL, h);
    DisposeHandle(h);
    return TRUE;
}
