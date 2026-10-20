#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <delarrop.cpp>
#else
void operator delete[](void *ptr) throw()
{
	operator delete(ptr);
}
#endif
