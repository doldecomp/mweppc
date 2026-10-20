#define default_new_handler NewHand_default_new_handler
#include "ansi_prefix.Win32.h"
#ifdef __MWERKS__
#include <NewHand.cpp>
#endif
/* (clang: the new handler throws bad_alloc, and the build is without exceptions: src/runtime/newop.cpp
 * calls terminate() where the throw would) */
