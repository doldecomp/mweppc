#ifndef DRIVER_CLERRORS_H
#define DRIVER_CLERRORS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CLStr1 = 1,
    CLStr2 = 2,
    CLStr3 = 3,
    CLStr4 = 4,
    CLStr5 = 5,
    CLStr6 = 6,
    CLStr7 = 7,
    CLStr8 = 8,
    CLStr9 = 9,
    CLStr10 = 10,
    CLStr11 = 11,
    CLStr12 = 12,
    CLStr13 = 13,
    CLStr14 = 14,
    CLStr15 = 15,
    CLStr16 = 16,
    CLStr17 = 17,
    CLStr18 = 18,
    CLStr19 = 19,
    CLStr20 = 20,
    CLStr21 = 21,
    CLStr22 = 22,
    CLStr23 = 23,
    CLStr24 = 24,
    CLStr25 = 25,
    CLStr26 = 26,
    CLStr27 = 27,
    CLStr28 = 28,
    CLStr29 = 29,
    CLStr30 = 30,
    CLStr31 = 31,
    CLStr32 = 32,
    CLStr33 = 33,
    CLStr34 = 34,
    CLStr35 = 35,
    CLStr36 = 36,
    CLStr37 = 37,
    CLStr38 = 38,
    CLStr39 = 39,
    CLStr40 = 40,
    CLStr41 = 41,
    CLStr42 = 42,
    CLStr43 = 43,
    CLStr44 = 44,
    CLStr45 = 45,
    CLStr46 = 46,
    CLStr47 = 47,
    CLStr48 = 48,
    CLStr49 = 49,
    CLStr50 = 50,
    CLStr51 = 51,
    CLStr52 = 52,
    CLStr53 = 53,
    CLStr54 = 54,
    CLStr55 = 55,
    CLStr56 = 56,
    CLStr57 = 57,
    CLStr58 = 58,
    CLStr59 = 59,
    CLStr60 = 60,
    CLStr61 = 61,
    CLStr62 = 62,
    CLStr63 = 63,
    CLStr64 = 64,
    CLStr65 = 65,
    CLStr66 = 66,
    CLStr67 = 67,
    CLStr68 = 68,
    CLStr69 = 69,
    CLStr70 = 70,
    CLStr71 = 71,
    CLStr72 = 72,
    CLStr73 = 73,
    CLStr74 = 74,
    CLStr75 = 75,
    CLStr76 = 76,
    CLStr77 = 77,
    CLStr78 = 78,
    CLStr79 = 79,
    CLStr80 = 80,
    CLStr81 = 81,
    CLStr82 = 82,
    CLStr83 = 83,
    CLStr84 = 84,
    CLStr85 = 85,
    CLStr86 = 86,
    CLStr87 = 87,
    CLStr88 = 88,
    CLStr89 = 89,
    CLStr90 = 90,
    CLStr91 = 91,
    CLStr92 = 92,
    CLStr93 = 93,
    CLStr94 = 94,
    CLStr95 = 95,
    CLStr96 = 96,
    CLStr97 = 97,
    CLStr98 = 98,
    CLStr99 = 99,
    CLStr100 = 100,
    CLStr101 = 101,
    CLStr102 = 102,
    CLStr103 = 103,
    CLStr104 = 104,
    CLStr105 = 105,
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
