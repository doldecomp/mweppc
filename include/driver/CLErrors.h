#ifndef DRIVER_CLERRORS_H
#define DRIVER_CLERRORS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// The command line's message numbers (STR12000, CLErrors.r)
enum {
    // "Could not get current working directory"
    CLStr1 = 1,
    // "Cannot find my executable '%s'"
    CLStr2 = 2,
    // "Could not initialize plugin '%s'"
    CLStr3 = 3,
    // "The %s '%s' requires functionality not present in the command-line driver."
    CLStr4 = 4,
    // "The command-line parser does not support these panels:"
    CLStr5 = 5,
    // "\t%s\n"
    CLStr6 = 6,
    // "Compiling function: '%s'"
    CLStr7 = 7,
    // "Could not write file '%s'"
    CLStr8 = 8,
    // "Could not write file '%s' in directory '%s'"
    CLStr9 = 9,
    // "Write error on output (errno=%ld)"
    CLStr10 = 10,
    // "Current working directory is too long"
    CLStr11 = 11,
    // "Unknown filetype '%c%c%c%c', defaulting to '%s'"
    CLStr12 = 12,
    // "%s:\ttype %s"
    CLStr13 = 13,
    // "Storing output for '%s' in '%s'"
    CLStr14 = 14,
    // "Writing text file '%s'"
    CLStr15 = 15,
    // "Writing %sobject file '%s'"
    CLStr16 = 16,
    // "Writing browse data '%s'"
    CLStr17 = 17,
    // "Could not write %s '%s' (error %ld)"
    CLStr18 = 18,
    // "Deleting temporary file '%s'"
    CLStr19 = 19,
    // "Could not resolve alias for '%s' (error %ld)"
    CLStr20 = 20,
    // "%s:\t'%s'%s"
    CLStr21 = 21,
    // "File '%s' has browse fileID %d"
    CLStr22 = 22,
    // "Can't locate directory '%s'"
    CLStr23 = 23,
    // "  %8.2f seconds to %s %s%s%s"
    CLStr24 = 24,
    // "  %8d lines compiled"
    CLStr25 = 25,
    // "  %8d %s code\n  %8d %s init'd data\n  %8d %s uninit'd data"
    CLStr26 = 26,
    // "  %8d total %s code\n  %8d total %s init'd data\n  %8d total %s uninit'd data"
    CLStr27 = 27,
    // "File '%s' is not compilable source, target object data, or command file; ignoring"
    CLStr28 = 28,
    // "All specified files were ignored"
    CLStr29 = 29,
    // "Compiling: '%s'"
    CLStr30 = 30,
    // "Compiling: '%s' with '%s'"
    CLStr31 = 31,
    // "Precompiling: '%s'"
    CLStr32 = 32,
    // "Precompiling: '%s' with '%s'"
    CLStr33 = 33,
    // "Preprocessing: '%s'"
    CLStr34 = 34,
    // "Preprocessing: '%s' with '%s'"
    CLStr35 = 35,
    // "Finding dependencies: '%s'"
    CLStr36 = 36,
    // "Finding dependencies: '%s' with '%s'"
    CLStr37 = 37,
    // "Importing: '%s'"
    CLStr38 = 38,
    // "Importing: '%s' with '%s'"
    CLStr39 = 39,
    // "Linking project"
    CLStr40 = 40,
    // "Linking project with '%s'"
    CLStr41 = 41,
    // "Pre-linking project"
    CLStr42 = 42,
    // "Pre-linking project with '%s'"
    CLStr43 = 43,
    // "Post-linking project"
    CLStr44 = 44,
    // "Post-linking project with '%s'"
    CLStr45 = 45,
    // "Disassembling: '%s'"
    CLStr46 = 46,
    // "Disassembling: '%s' with '%s'"
    CLStr47 = 47,
    // "Syntax checking: '%s'"
    CLStr48 = 48,
    // "Syntax checking: '%s' with '%s'"
    CLStr49 = 49,
    // "Getting target info from '%s'"
    CLStr50 = 50,
    // "Initializing '%s'"
    CLStr51 = 51,
    // "Terminating '%s'"
    CLStr52 = 52,
    // "'%s' cannot preprocess, skipping '%s'"
    CLStr53 = 53,
    // "'%s' cannot precompile, skipping '%s'"
    CLStr54 = 54,
    // "'%s' cannot generate code, skipping '%s'"
    CLStr55 = 55,
    // "'%s' has no object code to disassemble"
    CLStr56 = 56,
    // "'%s' cannot disassemble, skipping '%s'"
    CLStr57 = 57,
    // "Neither '%s' nor '%s' can disassemble, skipping '%s'"
    CLStr58 = 58,
    // "No precompiled header name given, '%s' assumed"
    CLStr59 = 59,
    // "Precompile target '%s' given on command line; source-specified name '%s' ignored"
    CLStr60 = 60,
    // "Writing precompiled %s file '%s'"
    CLStr61 = 61,
    // "Reading precompiled %s file '%s'"
    CLStr62 = 62,
    // "Cannot %s memory for %s"
    CLStr63 = 63,
    // "Files/directories must have length <= %ld characters;\n'%s' not accepted"
    CLStr64 = 64,
    // "Guessed linker name '%s' from compiler name '%s'"
    CLStr65 = 65,
    // "Can't find %s '%s' in path"
    CLStr66 = 66,
    // "Calling %s '%s'"
    CLStr67 = 67,
    // "Can't execute %s '%s' (%s)"
    CLStr68 = 68,
    // "%s '%s' returned with exit code %d"
    CLStr69 = 69,
    // "Too many errors printed, aborting program"
    CLStr70 = 70,
    // "Too many errors printed, suppressing errors for current file"
    CLStr71 = 71,
    // "Too many warnings printed, suppressing warnings for current file"
    CLStr72 = 72,
    // "No %s mapping matches '%s' (unrecognized file contents or filename extension); %s"
    CLStr73 = 73,
    // "No plugin or target matches the file '%s', ignoring"
    CLStr74 = 74,
    // "File '%s' cannot be handled by this tool, ignoring"
    CLStr75 = 75,
    // "File '%s' does not match the active target"
    CLStr76 = 76,
    // "Adding%s:\t'%s'"
    CLStr77 = 77,
    // "Creating new overlay '%s' in group '%s'"
    CLStr78 = 78,
    // "Creating new overlay group '%s' at addr %08X:%08X"
    CLStr79 = 79,
    // "File '%s' cannot be added to overlay group; define an overlay in the group first"
    CLStr80 = 80,
    // "Cannot create virtual export file '%s' (from '-export name,...')"
    CLStr81 = 81,
    // "Too many %s defined; at most %d%s is allowed"
    CLStr82 = 82,
    // "Loading preference panel '%s'"
    CLStr83 = 83,
    // "%s:"
    CLStr84 = 84,
    // "\t%s%s"
    CLStr85 = 85,
    // "Already defined %s search path; '%s' added after other paths"
    CLStr86 = 86,
    // "License check failed: %s"
    CLStr87 = 87,
    // "The plugin '%s' (version '%s') cannot be used:\n%s"
    CLStr88 = 88,
    // "Plugin '%s' has already been registered"
    CLStr89 = 89,
    // "Preferences for '%s' have already been registered"
    CLStr90 = 90,
    // "Preferences for '%s' not found"
    CLStr91 = 91,
    // "Some preferences needed by the plugin '%s' have not been registered"
    CLStr92 = 92,
    // "Could not load file '%s'"
    CLStr93 = 93,
    // "Could not find change current working directory to '%s'"
    CLStr94 = 94,
    // "Out of memory"
    CLStr95 = 95,
    // "The tool did not produce any output while %s the file '%s'"
    CLStr96 = 96,
    // "The filename '%s' is invalid"
    CLStr97 = 97,
    // "The %slinker for this target was not found"
    CLStr98 = 98,
    // "%s\n%s (OS error %d)"
    CLStr99 = 99,
    // "%s\n%s (error %d)"
    CLStr100 = 100,
    // "Note:  '%s' did not generate any browse information \nfor '%s'; no browser output generated"
    CLStr101 = 101,
    // "Source and specified output for the file '%s' are identical; no output will be generated"
    CLStr102 = 102,
    // "More than one output filename specified for '%s'; ignoring '%s'"
    CLStr103 = 103,
    // "Pref panel data for '%s' is out-of-date or invalid"
    CLStr104 = 104,
    // "Changing primary user access path to '%s'"
    CLStr105 = 105,
    // "The linker does not expect duplicate files; ignoring repeated '%s'"
    CLStr106 = 106
};

extern void CLReportError(VarArgShort errid, ...);
extern void CLReportWarning(VarArgShort errid, ...);
extern void CLReport(VarArgShort errid, ...);
extern void CLReportOSError(SInt16 errid, int err, ...);
extern void CLReportCError(SInt16 errid, int err_no, ...);
extern void CLInternalError(const char* file, int line, const char* format, ...);
#define DO_INTERNAL_ERROR(line, ...) CLInternalError(__FILE__, line, __VA_ARGS__)
extern void CLFatalError(const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif
