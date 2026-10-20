#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <delop.cpp>
#else
/* clang: delop.cpp's operator delete, without MSL's <new> (MWCC's C++ configuration). */
#include <stdlib.h>

void operator delete(void * p)
{
	free(p);
}
#endif
