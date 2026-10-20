#include "driver/CLFiles.h"
#include "driver/CLLoadAndCache.h"
#include "driver/cmdline.h"
#include "driver/Memory.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include <stddef.h>
#include <string.h>

File* File_New(void) {
    File* file;
    file = xmalloc(NULL, sizeof(File));
    if (!file) {
        return NULL;
    } else {
        memset(file, 0, sizeof(File));
        return file;
    }
}

void File_Free(File* file) {
    if (file) {
        if (file->textdata) {
            DisposeHandle(file->textdata);
            file->textdata = NULL;
        }
        if (file->objectdata) {
            DisposeHandle(file->objectdata);
            file->objectdata = NULL;
        }
        if (file->browsedata) {
            DisposeHandle(file->browsedata);
            file->browsedata = NULL;
        }
        xfree(file);
    }
}

Boolean Files_Initialize(Files* this) {
    OS_ASSERT(47, this != NULL);
    return TRUE;
}

Boolean Files_Terminate(Files* this) {
    File* file;
    File* next;

    OS_ASSERT(56, this != NULL);

    for (file = this->fileList; file; file = next) {
        next = file->next;
        File_Free(file);
    }

    return TRUE;
}

Boolean Files_AddFile(Files* this, File* file) {
    return Files_InsertFile(this, file, this->fileCount);
}

Boolean Files_InsertFile(Files* this, File* file, SInt32 position) {
    File** scan;

    OS_ASSERT(80, this != NULL);
    OS_ASSERT(81, file != NULL);

    if (position < 0) {
        position = 0;
    } else if (position > this->fileCount) {
        position = this->fileCount;
    }

    scan = &this->fileList;
    while (position-- > 0) {
        scan = &(*scan)->next;
    }

    file->filenum = this->fileCount++;
    file->next = *scan;
    *scan = file;
    return TRUE;
}

File* Files_GetFile(Files* this, SInt32 filenum) {
    File* file;

    OS_ASSERT(104, this != NULL);
    OS_ASSERT(105, filenum >= 0);

    file = this->fileList;
    while (file && file->filenum != filenum) {
        file = file->next;
    }
    return file;
}

File* Files_FindFile(Files* this, OSSpec* spec) {
    File* file;

    file = this->fileList;
    while (file && !OS_EqualSpec(&file->srcfss, spec)) {
        file = file->next;
    }
    return file;
}

int Files_Count(Files* this) {
    OS_ASSERT(127, this != NULL);

    return this->fileCount;
}

Boolean VFiles_Initialize(VFile** list) {
    *list = NULL;
    return TRUE;
}

void VFiles_Terminate(VFile** list) {
    VFile* next;

    while (*list) {
        next = (*list)->next;
        DisposeHandle((*list)->data);
        xfree(*list);
        list = &next;
    }

    // (the original's: `list` points at the local `next` by now, so the caller's head is left
    // dangling)
    *list = NULL;
}

VFile* VFile_New(const char* name, Handle data) {
    VFile* vf;

    vf = xmalloc(NULL, sizeof(VFile));
    if (!vf) {
        return NULL;
    }

    strncpy(vf->displayName, name, sizeof(vf->displayName));
    vf->displayName[sizeof(vf->displayName) - 1] = 0;

    vf->data = NewHandle(0);
    if (!vf->data || HandAndHand(data, vf->data)) {
        xfree(vf);
        return NULL;
    }

    FixTextHandle(vf->data);
    vf->next = NULL;
    return vf;
}

Boolean VFiles_Add(VFile** list, VFile* entry) {
    VFile** scan = list;
    while (*scan) {
        scan = &(*scan)->next;
    }
    *scan = entry;
    return TRUE;
}

VFile* VFiles_Find(VFile* list, const char* name) {
    VFile* scan = list;
    while (scan) {
        if (!strcmp(scan->displayName, name)) {
            break;
        }
        scan = scan->next;
    }
    return scan;
}
