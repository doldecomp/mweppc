#ifndef DRIVER_CLSEGS_H
#define DRIVER_CLSEGS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Segment {
    char name[32];
    UInt16 attrs;
} Segment;
typedef struct Segments {
    Segment** segsArray;
    UInt16 arraySize;
    UInt16 segsCount;
} Segments;
extern Segment* Segment_New(const char* name, UInt16 attrs);
extern Boolean Segments_Initialize(Segments* segs);
extern Boolean Segments_Terminate(Segments* segs);
extern Boolean Segments_AddSegment(Segments* segs, Segment* seg, UInt16* index);
extern Segment* Segments_GetSegment(Segments* segs, UInt16 segnum);
extern UInt16 Segments_Count(const Segments* segs);

#ifdef __cplusplus
}
#endif

#endif
