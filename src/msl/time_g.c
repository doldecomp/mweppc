/* MSL's time.c as 2.x links it: the Pro 5.3 updater's, with time(), gmtime() and localtime() as a slightly later
 * revision has them (tools/download_tool.py patches them into lib/msl/extra/time.c, which comes first in the include
 * path). TODO: find that revision's time.c (Pro 6's MSL sources would settle it). */
#include "msl/time_win32.h"
#define adjust time_g_adjust
#define asciitime time_g_asciitime
#define clear_tm time_g_clear_tm
#define emit time_g_emit
#define leap_days time_g_leap_days
#define week_num time_g_week_num
#define ISO8601NewYear time_g_ISO8601NewYear
#define ISO8601Week time_g_ISO8601Week
#include "ansi_prefix.Win32.h"
#include <time.c>
