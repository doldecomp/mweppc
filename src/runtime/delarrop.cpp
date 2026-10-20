#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <delarrop.cpp>
#else
/* clang: delarrop.cpp's operator delete[], without MSL's <new> (MWCC's C++ configuration). */
void operator delete[](void *ptr) throw()
{
	operator delete(ptr);
}
#endif
