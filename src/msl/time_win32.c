#include "msl/time_win32.h"
#include "ansi_prefix.Win32.h"
#include <time.win32.c>

int __cdecl subtract_time_zone_bias(int* time)
{
    TIME_ZONE_INFORMATION tzi;

    if (GetTimeZoneInformation(&tzi) == TIME_ZONE_ID_UNKNOWN) {
        return 0;
    }
    *time -= tzi.Bias * 60;
    return 1;
}
