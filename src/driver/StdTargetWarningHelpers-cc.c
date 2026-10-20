#include "driver/StdTargetWarningHelpers-cc.h"
#include "driver/parser.h"
#include "driver/pref_structs.h"
#include "driver/StringUtils.h"

// The C warnings set only by pragma.
typedef struct PExtraWarningC { // NOTE: invented name
    UInt8 warn_largeargs;
    UInt8 warn_padding;
    UInt8 warn_resultnotused;
    UInt8 warn_ptr_int_conv;
    UInt8 warn_no_side_effect;
} PExtraWarningC;

PExtraWarningC pExtraWarningC = {0};
// clang-format off
Pragma warningPragmas[] = {
    {&pExtraWarningC.warn_largeargs, "warn_largeargs", 0},
    {&pExtraWarningC.warn_padding, "warn_padding", 0},
    {&pExtraWarningC.warn_resultnotused, "warn_resultnotused", 0},
    {&pExtraWarningC.warn_ptr_int_conv, "warn_ptr_int_conv", 0},
    {&pExtraWarningC.warn_no_side_effect, "warn_no_side_effect", 0},
    {0, 0, 0}
};
// clang-format on

int TargetSetWarningFlags(short val, Boolean set) {
    switch (val) {
        case 'Ip':
            pWarningC.warn_illpragma = set;
            break;
        case 'Ed':
            pWarningC.warn_emptydecl = set;
            break;
        case 'Pu':
            pWarningC.warn_possunwant = set;
            break;
        case 'Uv':
            pWarningC.warn_unusedvar = set;
            break;
        case 'Ua':
            pWarningC.warn_unusedarg = set;
            break;
        case 'Ec':
            pWarningC.warn_extracomma = set;
            break;
        case 'Pd':
            pWarningC.pedantic = set;
            break;
        case 'Hv':
            pWarningC.warn_hidevirtual = set;
            break;
        case 'Ic':
            pWarningC.warn_implicitconv = set;
            break;
        case 'Ni':
            pWarningC.warn_notinlined = set;
            break;
        case 'Sc':
            pWarningC.warn_structclass = set;
            break;
        case 'Pa':
            pExtraWarningC.warn_padding = set ? PR_ON : PR_OFF;
            break;
        case 'Nu':
            pExtraWarningC.warn_resultnotused = set ? PR_ON : PR_OFF;
            break;
        case 'Se':
            pExtraWarningC.warn_no_side_effect = set ? PR_ON : PR_OFF;
            break;
        case 'PI':
            pExtraWarningC.warn_ptr_int_conv = set ? PR_ON : PR_OFF;
            break;
        case 'La':
            pExtraWarningC.warn_largeargs = set ? PR_ON : PR_OFF;
            break;
        case 'We':
            pWarningC.warningerrors = set;
            break;
        case 'Cp':
            pFrontEndC.checkprotos = set;
            break;
        default:
            return FALSE;
    }

    return TRUE;
}

void TargetDisplayWarningOptions(Handle txt) {
    HPrintF(txt, "C language warning options:\n");
    if (pWarningC.warn_illpragma) {
        HPrintF(txt, "\t- illegal pragmas\n");
    }
    if (pWarningC.warn_emptydecl) {
        HPrintF(txt, "\t- empty declarations\n");
    }
    if (pWarningC.warn_possunwant) {
        HPrintF(txt, "\t- possible unwanted effects\n");
    }
    if (pWarningC.warn_unusedvar) {
        HPrintF(txt, "\t- unused variables\n");
    }
    if (pWarningC.warn_unusedarg) {
        HPrintF(txt, "\t- unused arguments\n");
    }
    if (pWarningC.warn_extracomma) {
        HPrintF(txt, "\t- extra commas\n");
    }
    if (pWarningC.pedantic) {
        HPrintF(txt, "\t- pedantic\n");
    }
    if (pWarningC.warn_hidevirtual) {
        HPrintF(txt, "\t- hidden virtual functions\n");
    }
    if (pWarningC.warn_implicitconv) {
        HPrintF(txt, "\t- implicit conversions\n");
    }
    if (pWarningC.warn_notinlined) {
        HPrintF(txt, "\t- 'inline' not performed\n");
    }
    if (pWarningC.warn_structclass) {
        HPrintF(txt, "\t- struct/class conflict\n");
    }
    if (pExtraWarningC.warn_largeargs == 1) {
        HPrintF(txt, "\t- large args passed to unprototyped functions\n");
    }
    if (pExtraWarningC.warn_padding == 1) {
        HPrintF(txt, "\t- padding added between struct members\n");
    }
    if (pExtraWarningC.warn_resultnotused == 1) {
        HPrintF(txt, "\t- result of non-void function call not used\n");
    }
    if (pExtraWarningC.warn_no_side_effect == 1) {
        HPrintF(txt, "\t- use of expressions as statements without side effects\n");
    }
    if (pExtraWarningC.warn_ptr_int_conv == 1) {
        HPrintF(txt, "\t- implicit integer/pointer conversions\n");
    }
    if (pFrontEndC.checkprotos) {
        HPrintF(txt, "\t- checking prototypes\n");
    }
    if (pWarningC.warningerrors) {
        HPrintF(txt, "\t- warnings are errors\n");
    } else {
        HPrintF(txt, "\t- warnings are not errors\n");
    }
}
