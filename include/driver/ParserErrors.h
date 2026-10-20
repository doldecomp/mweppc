#ifndef DRIVER_PARSERERRORS_H
#define DRIVER_PARSERERRORS_H

#include "compiler/common.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
enum {
    CLPStr1 = 1,
    CLPStr2 = 2,
    CLPStr3 = 3,
    CLPStr4 = 4,
    CLPStr5 = 5,
    CLPStr6 = 6,
    CLPStr7 = 7,
    CLPStr8 = 8,
    CLPStr9 = 9,
    CLPStr10 = 10,
    CLPStr11 = 11,
    CLPStr12 = 12,
    CLPStr13 = 13,
    CLPStr14 = 14,
    CLPStr15 = 15,
    CLPStr16 = 16,
    CLPStr17 = 17,
    CLPStr18 = 18,
    CLPStr19_UnknownOptionX = 19,
    CLPStr20_UnknownOptionX_ExpectedOneOfX = 20,
    CLPStr21_OptionObsolete = 21,
    CLPStr22_OptionObsoleteWithHelp = 22,
    CLPStr23_OptionXSubstitutedWithX = 23,
    CLPStr24_OptionDeprecated = 24,
    CLPStr25_OptionDeprecatedWithHelp = 25,
    CLPStr26_OptionIgnored = 26,
    CLPStr27_OptionIgnoredWithText = 27,
    CLPStr28_WarningText = 28,
    CLPStr29_OptionHasNoEffect = 29,
    CLPStr30_OptionShouldNotBeSpecifiedMultipleTimes = 30,
    CLPStr31_OptionOverridesEffect = 31,
    CLPStr32_OptionOverridesEffectWithHelp = 32,
    CLPStr33_NoDefaultHandlerSetUpForX_Ignoring = 33,
    CLPStr34_ArgumentsExpected = 34,
    CLPStr35_TokenXNotExpected = 35,
    CLPStr36_UnexpectedAdditionalArgumentX = 36,
    CLPStr37 = 37,
    CLPStr38_NoHelpAvailableForOptionX = 38,
    CLPStr39 = 39,
    CLPStr40 = 40,
    CLPStr41 = 41,
    CLPStr42 = 42,
    CLPStr43 = 43,
    CLPStr44 = 44,
    CLPStr45 = 45,
    CLPStr46 = 46,
    CLPStr47 = 47,
    CLPStr48 = 48,
    CLPStr49 = 49,
    CLPStr50 = 50,
    CLPStr51 = 51,
    CLPStr52 = 52,
    CLPStr53 = 53,
    CLPStr54 = 54,
    CLPStr55 = 55,
    CLPStr56 = 56,
    CLPStr57 = 57,
    CLPStr58 = 58,
    CLPStr59 = 59,
    CLPStr60 = 60,
    CLPStr61 = 61,
    CLPStr62 = 62,
    CLPStr63 = 63,
    CLPStr64 = 64,
    CLPStr65 = 65,
    CLPStr66 = 66,
    CLPStr67 = 67,
    CLPStr68 = 68,
    CLPStr69 = 69,
    CLPStr70 = 70,
    CLPStr71 = 71,
    CLPStr72 = 72,
    CLPStr73 = 73,
    CLPStr74 = 74,
    CLPStr75 = 75,
    CLPStr76 = 76,
    CLPStr77 = 77,
    CLPStr78 = 78
};

// clang-format on

extern void CLPReportError_V(const char* format, va_list ap);
extern void CLPReportWarning_V(const char* format, va_list ap);
extern char* CLPGetErrorString(SInt16 errid, char* buffer);
extern void CLPReportError(VarArgShort errid, ...);
extern void CLPReportWarning(VarArgShort errid, ...);
extern void CLPReport(VarArgShort errid, ...);
extern void CLPOSAlert(SInt16 errid, VarArgShort err, ...);
extern void CLPStatus(VarArgShort errid, ...);
extern void CLPFatalError(const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif
