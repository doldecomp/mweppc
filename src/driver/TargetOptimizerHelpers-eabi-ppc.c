// The back end's optimizer options. NOTE: invented file name (Wii's module
// "TargetOptimizerHelpers-eppc" in 2.x's spelling: no assert or literal names the original's)
#include "driver/TargetOptimizerHelpers-eabi-ppc.h"
#include "compiler/CParser.h"
#include "driver/pref_structs.h"
#include "driver/StringUtils.h"

int TargetSetOptFlags(short val, Boolean set) {
    switch (val) {
        case 'Pe':
            pBackEnd.peephole = set;
            break;
        case 'Sn':
            pBackEnd.schedule = FALSE;
            break;
        case 'Sh':
            pBackEnd.schedule = set;
            break;
        default:
            return FALSE;
    }

    return TRUE;
}

void TargetDisplayOptimizationOptions(Handle txt) {
    // (the panel numbers its processors as CompilerLinkerOptions.processor does; CPU_PPCELF prints
    // "e500")
    int p;

    HPrintF(txt, "\t- peephole optimizations %s\n", pBackEnd.peephole ? "on" : "off");
    if (!pBackEnd.schedule) {
        HPrintF(txt, "\t- no instruction scheduling\n");
    } else {
        HPrintF(
            txt,
            "\t- schedule for %s\n",
            ((p = pBackEnd.processor) == CPU_Generic) ? "generic PPC"
                : (p == CPU_PPC401)                   ? "401"
                : (p == CPU_PPC403)                   ? "403"
                : (p == CPU_PPC505)                   ? "505"
                : (p == CPU_PPC509)                   ? "509"
                : (p == CPU_PPC555)                   ? "555"
                : (p == CPU_PPC601)                   ? "601"
                : (p == CPU_PPC602)                   ? "602"
                : (p == CPU_PPC603)                   ? "603"
                : (p == CPU_PPC603e)                  ? "603e"
                : (p == CPU_PPC604)                   ? "604"
                : (p == CPU_PPC604e)                  ? "604e"
                : (p == CPU_PPC740)                   ? "740"
                : (p == CPU_PPC750)                   ? "750"
                : (p == CPU_PPC801)                   ? "801"
                : (p == CPU_PPC821)                   ? "821"
                : (p == CPU_PPC823)                   ? "823"
                : (p == CPU_PPC850)                   ? "850"
                : (p == CPU_PPC860)                   ? "860"
                : (p == CPU_PPC8240)                  ? "8240"
                : (p == CPU_PPC8260)                  ? "8260"
                : (p == CPU_PPC7400)                  ? "7400"
                : (p == CPU_PPCGEKKO)                 ? "GEKKO"
                : (p == CPU_PPCELF)                   ? "e500"
                : (p == CPU_PPC7450)                  ? "7450"
                                                      : "???"
        );
    }
}

void TargetSetPragmaOptimizationsToUnspecified(void) {}
