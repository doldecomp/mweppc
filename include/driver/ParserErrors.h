#ifndef DRIVER_PARSERERRORS_H
#define DRIVER_PARSERERRORS_H

#include "compiler/common.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: invented names: every message below
// The parser's messages: STR 12010 of the resources, each named by its index (and some by its
// text).
// clang-format off
enum {
    // "Option missing"
    CLPStr1 = 1,
    // "Command-line argument '%.15s...%.15s' is much too long, maximum accepted length is %d characters"
    CLPStr2 = 2,
    // "Parameter for '%s' missing"
    CLPStr3 = 3,
    // "Numeric constant '%s' overflowed 32-bit range"
    CLPStr4 = 4,
    // "Invalid %sconstant '%s'"
    CLPStr5 = 5,
    // "Numeric constant %lu is out of the legal range %lu - %lu"
    CLPStr6 = 6,
    // "Numeric constant %lu is out of the legal range %lu - %lu; clipping to %lu"
    CLPStr7 = 7,
    // "Four-char type '%s' is too long, expected maximum four characters"
    CLPStr8 = 8,
    // "String constant '%.16s...%.16s' too long, expected maximum %ld characters"
    CLPStr9 = 9,
    // "Identifier '%s' cannot start with a digit"
    CLPStr10 = 10,
    // "Identifier '%s' cannot contain the character '%c'"
    CLPStr11 = 11,
    // "Expected 'on' or 'off', but got '%s'"
    CLPStr12 = 12,
    // "Filename or directory '...%32s' is too long, expected maximum %ld characters"
    CLPStr13 = 13,
    // "Directory '...%32s' combined with filename '...%32s' is too long, maximum path is %ld characters"
    CLPStr14 = 14,
    // "File '%s' does not exist"
    CLPStr15 = 15,
    // "'%s' is a directory or volume, filename expected"
    CLPStr16 = 16,
    // "'%s' does not refer to a directory"
    CLPStr17 = 17,
    // "File path '%s' not accepted\n(%s)"
    CLPStr18 = 18,
    // "Unknown option '%s'"
    CLPStr19_UnknownOptionX = 19,
    // "Unknown option '%s'; expected one of '%s'"
    CLPStr20_UnknownOptionX_ExpectedOneOfX = 20,
    // "Option is obsolete"
    CLPStr21_OptionObsolete = 21,
    // "Option is obsolete; %s"
    CLPStr22_OptionObsoleteWithHelp = 22,
    // "Option '%s' substituted with %s"
    CLPStr23_OptionXSubstitutedWithX = 23,
    // "Option deprecated; accepted this time"
    CLPStr24_OptionDeprecated = 24,
    // "Option deprecated, accepted this time; use '%s' instead"
    CLPStr25_OptionDeprecatedWithHelp = 25,
    // "Option ignored"
    CLPStr26_OptionIgnored = 26,
    // "Option ignored;\n%s"
    CLPStr27_OptionIgnoredWithText = 27,
    // "%s"
    CLPStr28_WarningText = 28,
    // "Option has no effect on this target"
    CLPStr29_OptionHasNoEffect = 29,
    // "Option should not be specified multiple times"
    CLPStr30_OptionShouldNotBeSpecifiedMultipleTimes = 30,
    // "Option overrides the effect of '%s'; only one of '%s' should be specified"
    CLPStr31_OptionOverridesEffect = 31,
    // "Option overrides the effect of '%s'; only one of '%s' should be specified;\n%s"
    CLPStr32_OptionOverridesEffectWithHelp = 32,
    // "No default handler set up for '%s', ignoring"
    CLPStr33_NoDefaultHandlerSetUpForX_Ignoring = 33,
    // "Argument(s) expected"
    CLPStr34_ArgumentsExpected = 34,
    // "Token '%s' not expected"
    CLPStr35_TokenXNotExpected = 35,
    // "Unexpected additional argument '%s'"
    CLPStr36_UnexpectedAdditionalArgumentX = 36,
    // "Encountered extraneous arguments"
    CLPStr37 = 37,
    // "No help available for option '%s'"
    CLPStr38_NoHelpAvailableForOptionX = 38,
    // "Empty (zero-length) argument not accepted"
    CLPStr39 = 39,
    // "Variable name missing"
    CLPStr40 = 40,
    // "Only one output filename allowed; '%s' not accepted"
    CLPStr41 = 41,
    // "Output filenames found without source; '%s' not accepted"
    CLPStr42 = 42,
    // "Multiple outputs expected, output is ambiguous; '%s' not accepted"
    CLPStr43 = 43,
    // "Specified file '%s' not found"
    CLPStr44 = 44,
    // "Specified directory '%s' not found"
    CLPStr45 = 45,
    // "'%s' is a filename, not a directory; not accepted"
    CLPStr46 = 46,
    // "'%s' is a directory, not a filename; not accepted"
    CLPStr47 = 47,
    // "Library search found shared library '%s' instead of export library '%s.LIB'"
    CLPStr48 = 48,
    // "No library file found matching 'lib%s{%s}' or '%s'"
    CLPStr49 = 49,
    // "Redudant use of '%c%s%s'; linker always searches library paths;\ntypically one finds, e.g., libXXX.a with '%c%sXXX'"
    CLPStr50 = 50,
    // "Empty filename in library list ignored"
    CLPStr51 = 51,
    // "Environment variable '%s' not found"
    CLPStr52 = 52,
    // "In environment variable '%s':"
    CLPStr53 = 53,
    // "Option name expected for 'opt'"
    CLPStr54 = 54,
    // "Token '%s' ignored"
    CLPStr55 = 55,
    // "Token '%s[...%s]' was truncated to %d characters"
    CLPStr56 = 56,
    // "Expected %s, but got %s"
    CLPStr57 = 57,
    // "No output expected from this process, ignoring output name '%s'"
    CLPStr58 = 58,
    // "Only one output directory allowed, '%s' not accepted"
    CLPStr59 = 59,
    // "Storing browse info for '%s' in '%s'"
    CLPStr60 = 60,
    // "All linker output will be created in the same directory; ignoring path for '%s'"
    CLPStr61 = 61,
    // "Option overrides previously specified optimizations"
    CLPStr62 = 62,
    // "Could not initialize command line parser"
    CLPStr63 = 63,
    // "Output filename '%s' is invalid"
    CLPStr64 = 64,
    // "Truncated output filename '%s' to %d characters"
    CLPStr65 = 65,
    // "Output directory '%s' cannot be found"
    CLPStr66 = 66,
    // "Cannot add access path '%s'"
    CLPStr67 = 67,
    // "Cannot set preferences for '%s'"
    CLPStr68 = 68,
    // "Some of the files specified could not be found;  build aborted"
    CLPStr69 = 69,
    // "Nothing to do:  no source or object files specified"
    CLPStr70 = 70,
    // "Contents of %s:"
    CLPStr71 = 71,
    // "Overlays not supported on this target"
    CLPStr72 = 72,
    // "Segments not supported on this target"
    CLPStr73 = 73,
    // "Cannot open %s file '%s'"
    CLPStr74 = 74,
    // ""
    CLPStr75 = 75,
    // "The file '%s' may be ignored unless it has one of the following\nextensions: \"%s\""
    CLPStr76 = 76,
    // "Not using non-text file '%s' as optional argument"
    CLPStr77 = 77,
    // "Cannot redirect stream to '%s' (%s)"
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
