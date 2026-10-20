#ifndef VERSION_H
#define VERSION_H

// The GameCube compilers of the 2.x codegen line (C/C++ 2.4.x, built by the CodeWarrior Pro 6 x86
// compiler), in release order. configure.py passes -DVERSION=<index>. The sources are 2.6's; a
// version that differs tests VERSION. GC_2_0p2 is not a release but the compiler SpongeBob: Battle
// for Bikini Bottom's code shows it was built with, made from the sources alone (config/GC_2_0p2):
// 2.0 with floating-point literals that are not const (TOC.c), the memory rules that code shows and
// two of 2.5's alias changes (Alias.c, Scheduler.c).
#define VERSION_GC_2_0 0
#define VERSION_GC_2_0P2 1
#define VERSION_GC_2_5 2
#define VERSION_GC_2_6 3
#define VERSION_GC_2_7 4

#ifndef VERSION
#define VERSION VERSION_GC_2_6
#endif

#endif
