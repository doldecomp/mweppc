#include "driver/CmdLineBuildDate.h"
#include "version.h"

// When each release's driver was built (its __TIME__ and __DATE__), which -version prints
#if VERSION <= VERSION_GC_2_0P2
const char* CMDLINE_BUILD_TIME = "15:14:48";
const char* CMDLINE_BUILD_DATE = "Sep 16 2002";
#elif VERSION == VERSION_GC_2_5
const char* CMDLINE_BUILD_TIME = "14:21:02";
const char* CMDLINE_BUILD_DATE = "Feb 20 2003";
#elif VERSION == VERSION_GC_2_6
const char* CMDLINE_BUILD_TIME = "14:19:11";
const char* CMDLINE_BUILD_DATE = "Jul 14 2003";
#else
const char* CMDLINE_BUILD_TIME = "17:19:15";
const char* CMDLINE_BUILD_DATE = "Jul 22 2004";
#endif
