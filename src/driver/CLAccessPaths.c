#include "driver/CLAccessPaths.h"
#include "driver/CLIO.h"
#include "driver/cmdline.h"
#include "driver/Files.h"
#include "driver/Generic.h"
#include "driver/MacSpecs.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include <stdlib.h>
#include <string.h>

Path* Path_Init(const OSPathSpec* dir, Path* path) {
    path->spec = xmalloc(NULL, sizeof(OSPathSpec));
    *path->spec = *dir;
    path->recursive = NULL;
    path->dirlist = NULL;
    path->flags = 0;
    return path;
}

Path* Path_New(const OSPathSpec* dir) {
    Path* path;
    path = xmalloc(NULL, sizeof(Path));
    return Path_Init(dir, path);
}

void Path_Free(Path* path) {
    if (path) {
        if (path->spec) {
            xfree(path->spec);
        }
        if (path->recursive) {
            Paths_Terminate(path->recursive);
            xfree(path->recursive);
        }
        xfree(path);
    }
}

Boolean Paths_Initialize(Paths* paths) {
    OS_ASSERT(52, paths != NULL);

    memset(paths, 0, sizeof(Paths));
    paths->pathsArray = NULL;
    return TRUE;
}

Boolean Paths_Terminate(Paths* paths) {
    UInt16 index;

    OS_ASSERT(63, paths != NULL);

    if (paths->pathsArray) {
        for (index = 0; index < paths->pathsCount; index++) {
            Path_Free(paths->pathsArray[index]);
        }
        xfree(paths->pathsArray);
    }

    paths->pathsArray = NULL;
    return TRUE;
}

static Boolean Paths_GrowPaths(Paths* paths, UInt16* index) {
    Path** newArray;

    OS_ASSERT(84, paths != NULL);

    if (paths->pathsCount >= paths->arraySize) {
        paths->arraySize += 20;
        newArray = xrealloc("access paths", paths->pathsArray, sizeof(Path*) * paths->arraySize);
        paths->pathsArray = newArray;
    }

    *index = paths->pathsCount++;
    return TRUE;
}

Boolean Paths_AddPath(Paths* paths, Path* path) {
    UInt16 ni;

    if (Paths_GrowPaths(paths, &ni)) {
        paths->pathsArray[ni] = path;
        return TRUE;
    } else {
        return FALSE;
    }
}

Boolean Paths_InsertPath(Paths* paths, UInt16 index, Path* path) {
    UInt16 ni;

    if (Paths_GrowPaths(paths, &ni)) {
        if (index > ni) {
            index = (ni != 0) ? (ni - 1) : 0;
        }
        memmove(
            &paths->pathsArray[index + 1],
            &paths->pathsArray[index],
            sizeof(Path*) * (paths->pathsCount - index)
        );
        paths->pathsArray[index] = path;
        return TRUE;
    } else {
        return FALSE;
    }
}

Boolean Paths_RemovePath(Paths* paths, UInt16 index) {
    if (index >= paths->pathsCount) {
        return FALSE;
    }

    memmove(
        &paths->pathsArray[index],
        &paths->pathsArray[index + 1],
        sizeof(Path*) * (paths->pathsCount - index - 1)
    );
    paths->pathsCount--;
    return TRUE;
}

Boolean Paths_DeletePath(Paths* paths, UInt16 index) {
    if (index >= paths->pathsCount) {
        return FALSE;
    }

    Path_Free(paths->pathsArray[index]);
    return Paths_RemovePath(paths, index);
}

Path* Paths_GetPath(Paths* paths, UInt16 pathnum) {
    OS_ASSERT(151, paths != NULL);

    if (pathnum < paths->pathsCount) {
        return paths->pathsArray[pathnum];
    } else {
        return NULL;
    }
}

UInt16 Paths_Count(const Paths* paths) {
    OS_ASSERT(161, paths != NULL);

    return paths->pathsCount;
}

Boolean Paths_FindPath(const Paths* paths, const Path* path) {
    UInt16 idx;

    OS_ASSERT(169, paths != NULL);

    for (idx = 0; idx < paths->pathsCount; idx++) {
        if (paths->pathsArray[idx] == path) {
            return TRUE;
        }
    }

    return FALSE;
}

Path* Paths_FindPathSpec(const Paths* paths, const OSPathSpec* dir) {
    UInt16 idx;
    Path* path;

    OS_ASSERT(182, paths != NULL);

    for (idx = 0; idx < paths->pathsCount; idx++) {
        path = paths->pathsArray[idx];
        if (OS_EqualPathSpec(path->spec, dir)) {
            return path;
        }
    }

    return NULL;
}

static Boolean GatherRecurse(Paths* list, Path* path) {
    int err;
    OSOpenedDir rf;
    OSSpec ent;
    int count;
    char filename[256];
    Boolean isfile;
    char* nptr;
    char* eptr;
    UInt16 added;
    Path* nw;

    count = 0;
    if (CheckForUserBreak()) {
        return TRUE;
    }

    err = OS_OpenDir(path->spec, &rf);
    while (!err) {
        err = OS_ReadDir(&rf, &ent, filename, &isfile);
        if (!err) {
            if (!isfile) {
                nw = Path_New(&ent.path);
                nptr = OS_GetFileNamePtr(filename);
                eptr = nptr + strlen(nptr) - 1;
                if (eptr[0] == OS_PATHSEP) {
                    eptr--;
                }
                if (eptr > nptr && *nptr == '(' && *eptr == ')') {
                    continue;
                }

                if (!Paths_AddPath(list, nw)) {
                    break;
                }
                added = Paths_Count(list) - 1;
                if (!GatherRecurse(list, nw)) {
                    Paths_DeletePath(list, added);
                }
            } else {
                if (!(++count % 50) && CheckForUserBreak()) {
                    return FALSE;
                }
            }
        }
    }
    OS_CloseDir(&rf);
    return count > 0;
}

Boolean Paths_GatherRecurse(Path* path) {
    path->recursive = xcalloc(NULL, sizeof(Paths));
    GatherRecurse(path->recursive, path);
    return Paths_Count(path->recursive) != 0;
}

int Paths_CountRecurse(Paths* paths) {
    UInt16 idx;
    int mine;
    Path* path;

    mine = Paths_Count(paths);
    for (idx = 0; idx < mine; idx++) {
        path = Paths_GetPath(paths, idx);
        OS_ASSERT(336, path);

        if (path->recursive) {
            mine += Paths_CountRecurse(path->recursive);
        }
    }

    return mine;
}

static void CopyRecurseFSS(FSSpec** list, Paths* paths, UInt16* count) {
    UInt16 idx;
    OSSpec spec;
    Path* path;

    for (idx = 0; idx < Paths_Count(paths); idx++) {
        path = Paths_GetPath(paths, idx);
        OS_ASSERT(353, path && *count > 0);
        OS_MakeSpecWithPath(path->spec, NULL, FALSE, &spec);
        OS_ASSERT(355, OS_OSSpec_To_FSSpec(&spec, *list));
        (*list)++;
        (*count)--;
        if (path->recursive) {
            CopyRecurseFSS(list, path->recursive, count);
        }
    }
}

void Paths_CopyRecurseFSS(FSSpec* list, Paths* paths, UInt16 count) {
    CopyRecurseFSS(&list, paths, &count);
    OS_ASSERT(367, count == 0);
}
