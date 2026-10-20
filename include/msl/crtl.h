#ifndef MSL_CRTL_H
#define MSL_CRTL_H

// MSL Win32's crtl.h: the low-level file handle table (lib/msl/MSL_Win32/Include/crtl.h; Pro 6's
// is the same)
typedef struct {
    void* handle;
    char translate;
    char append;
} FileStruct;

#define NUM_HANDLES 256

extern FileStruct* _HandleTable[NUM_HANDLES];

#endif
