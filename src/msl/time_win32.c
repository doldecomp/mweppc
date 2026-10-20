// MSL Win32's time.win32.c, and the function 2.x's localtime calls (a revision later than the updater's; see
// time_g.c): __to_gm_time's inverse, from GMT to local time
#include "msl/time_win32.h"
#include "ansi_prefix.Win32.h"
#include <time.win32.c>

// TODO: invented name (the later revision's source is not available)
int __cdecl subtract_time_zone_bias(int* time)
{
    TIME_ZONE_INFORMATION tzi;

    if (GetTimeZoneInformation(&tzi) == TIME_ZONE_ID_UNKNOWN) {
        return 0;
    }
    *time -= tzi.Bias * 60;
    return 1;
}
