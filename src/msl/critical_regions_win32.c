// MSL Win32's critical_regions.win32.c, built multithreaded: 2.x links __begin_critical_region and
// __end_critical_region, which the file compiles only under _MWMT
#define _MWMT 1
#include "ansi_prefix.Win32.h"
#include <critical_regions.win32.c>
