#include "driver/CLSegs.h"
#include "driver/MemUtils.h"
#include "driver/oslib.h"
#include <string.h>

Segment* Segment_New(const char* name, UInt16 attrs) {
    Segment* seg = xmalloc(NULL, sizeof(Segment));
    strncpy(seg->name, name, sizeof(seg->name));
    seg->name[sizeof(seg->name) - 1] = 0;
    seg->attrs = attrs;
    return seg;
}

void Segment_Free(Segment* seg) {
    if (seg) {
        xfree(seg);
    }
}

Boolean Segments_Initialize(Segments* segs) {
    Segment* jump;
    Segment* main;
    UInt16 idx;

    OS_ASSERT(36, segs != NULL);

    memset(segs, 0, sizeof(Segments));
    segs->segsArray = NULL;

    jump = Segment_New("Jump Table", 0x28);
    Segments_AddSegment(segs, jump, &idx);
    OS_ASSERT(44, idx==0);

    main = Segment_New("Main", 0xFFFF);
    Segments_AddSegment(segs, main, &idx);
    OS_ASSERT(49, idx==1);

    return TRUE;
}

Boolean Segments_Terminate(Segments* segs) {
    UInt16 index;

    OS_ASSERT(57, segs != NULL);

    if (segs->segsArray) {
        for (index = 0; index < segs->segsCount; index++) {
            Segment_Free(segs->segsArray[index]);
        }
        xfree(segs->segsArray);
    }
    segs->segsArray = NULL;

    return TRUE;
}

static Boolean Segments_GrowSegments(Segments* segs, UInt16* index) {
    Segment** newArray;

    OS_ASSERT(78, segs != NULL);

    if (segs->segsCount >= segs->arraySize) {
        segs->arraySize += 20;
        newArray = xrealloc("segments", segs->segsArray, sizeof(Segment*) * segs->arraySize);
        segs->segsArray = newArray;
    }

    *index = segs->segsCount++;
    return TRUE;
}

Boolean Segments_AddSegment(Segments* segs, Segment* seg, UInt16* index) {
    UInt16 ni;

    if (Segments_GrowSegments(segs, &ni)) {
        segs->segsArray[ni] = seg;
        *index = ni;
        return TRUE;
    } else {
        return FALSE;
    }
}

Boolean Segments_InsertSegment(Segments* segs, UInt16 index, Segment* seg) {
    UInt16 ni;

    if (Segments_GrowSegments(segs, &ni)) {
        if (index > ni) {
            index = ni - 1;
        }
        memmove(
            &segs->segsArray[index + 1],
            &segs->segsArray[index],
            sizeof(Segment*) * (segs->segsCount - index)
        );
        segs->segsArray[index] = seg;
        return TRUE;
    } else {
        return FALSE;
    }
}

Boolean Segments_DeleteSegment(Segments* segs, UInt16 index) {
    if (index >= segs->segsCount) {
        return FALSE;
    }

    Segment_Free(segs->segsArray[index]);
    memmove(
        &segs->segsArray[index],
        &segs->segsArray[index + 1],
        sizeof(Segment*) * (segs->segsCount - index)
    );
    segs->segsCount--;
    return TRUE;
}

Segment* Segments_GetSegment(Segments* segs, UInt16 segnum) {
    OS_ASSERT(137, segs != NULL);

    if (segnum < segs->segsCount) {
        return segs->segsArray[segnum];
    } else {
        return NULL;
    }
}

UInt16 Segments_Count(const Segments* segs) {
    OS_ASSERT(147, segs != NULL);

    return segs->segsCount;
}
