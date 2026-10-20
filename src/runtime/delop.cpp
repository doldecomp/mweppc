#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <delop.cpp>
#else
#include <stdlib.h>

void operator delete(void * p)
{
	free(p);
}
#endif
