#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "driver/CCompiler.h"
#include "driver/COSToolsCLT.h"
#include <ctype.h>
#include <string.h>

// clang-format off
UInt32 bit_masks[32] = {
    0, 1, 3, 7,
    0xF, 0x1F, 0x3F, 0x7F,
    0xFF, 0x1FF, 0x3FF, 0x7FF,
    0xFFF, 0x1FFF, 0x3FFF, 0x7FFF,
    0xFFFF, 0x1FFFF, 0x3FFFF, 0x7FFFF,
    0xFFFFF, 0x1FFFFF, 0x3FFFFF, 0x7FFFFF,
    0xFFFFFF, 0x1FFFFFF, 0x3FFFFFF, 0x7FFFFFF,
    0xFFFFFFF, 0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF
};
// clang-format on

typedef struct HeapBlock {
    struct HeapBlock* next;
    Handle blockhandle;
    SInt32 blocksize;
    SInt32 blockfree;
} HeapBlock;

typedef struct HeapMem {
    HeapBlock* blocks;
    // the minimum size for a new block
    SInt32 allocsize;
    HeapBlock* curblock;
    char* curfreep;
    SInt32 curfree;
} HeapMem;

static HeapMem gheap;
static HeapMem lheap;
static HeapMem aheap;
static HeapMem oheap;
static HeapMem bheap;
static heaperror_t heaperror;
static short lheaplockcount;
long hash_name_id;
HashNameNode** name_hash_nodes;
void (*GListErrorProc)(void);

void CompilerGetCString(short index, char* string) {
    COS_GetString(string, 10100, index);
}

unsigned char* CTool_CtoPstr(char* cstr) {
    char* cp = cstr;
    int length = 0;
    int i;

    while (*cp) {
        ++length;
        ++cp;
    }

    for (i = length; i > 0; i--) {
        cp[0] = cp[-1];
        --cp;
    }

    cstr[0] = length;
    return (unsigned char*)cstr;
}

static void GListError(void) {
    if (GListErrorProc) {
        GListErrorProc();
    }
}

short InitGList(GList* gl, SInt32 size) {
    if ((gl->data = COS_NewOSHandle(size)) == 0) {
        if ((gl->data = COS_NewHandle(size)) == 0) {
            return -1;
        }
    }
    gl->size = 0;
    gl->growsize = size >> 1;
    gl->hndlsize = size;
    return 0;
}

void FreeGList(GList* gl) {
    if (gl->data != 0) {
        COS_FreeHandle(gl->data);
        gl->data = 0;
    }
    gl->size = gl->hndlsize = 0;
}

void LockGList(GList* gl) {
    COS_LockHandleHi(gl->data);
}

void UnlockGList(GList* gl) {
    COS_UnlockHandle(gl->data);
}

void ShrinkGList(GList* gl) {
    COS_ResizeHandle(gl->data, gl->hndlsize = gl->size);
}

void AppendGListData(GList* gl, const void* data, SInt32 size) {
    if ((gl->size + size) > gl->hndlsize) {
        if (!COS_ResizeHandle(gl->data, gl->hndlsize += size + gl->growsize)) {
            GListError();
        }
    }

    memcpy(*gl->data + gl->size, data, size);
    gl->size += size;
}

void AppendGListNoData(GList* gl, SInt32 size) {
    if ((gl->size + size) > gl->hndlsize) {
        if (!COS_ResizeHandle(gl->data, gl->hndlsize += size + gl->growsize)) {
            GListError();
        }
    }

    gl->size += size;
}

void AppendGListByte(GList* gl, SInt8 thebyte) {
    if ((gl->size + 1) > gl->hndlsize) {
        if (!COS_ResizeHandle(gl->data, gl->hndlsize += 1 + gl->growsize)) {
            GListError();
        }
    }

    (*gl->data)[gl->size++] = thebyte;
}

void AppendGListWord(GList* gl, SInt16 theword) {
    char* ptr;

    if ((gl->size + 2) > gl->hndlsize) {
        if (!COS_ResizeHandle(gl->data, gl->hndlsize += 2 + gl->growsize)) {
            GListError();
        }
    }

    ptr = *gl->data + gl->size;
    gl->size += 2;
    *(ptr++) = ((char*)&theword)[0];
    *(ptr++) = ((char*)&theword)[1];
}

void AppendGListTargetEndianWord(GList* gl, UInt16 theword) {
    char* ptr;

    if ((gl->size + 2) > gl->hndlsize) {
        if (!COS_ResizeHandle(gl->data, gl->hndlsize += 2 + gl->growsize)) {
            GListError();
        }
    }

    ptr = *gl->data + gl->size;
    gl->size += 2;
    theword = CTool_EndianConvertWord16(theword);
    *(ptr++) = ((char*)&theword)[0];
    *(ptr++) = ((char*)&theword)[1];
}

void AppendGListLong(GList* gl, SInt32 theword) {
    char* ptr;

    if ((gl->size + 4) > gl->hndlsize) {
        if (!COS_ResizeHandle(gl->data, gl->hndlsize += 4 + gl->growsize)) {
            GListError();
        }
    }

    ptr = *gl->data + gl->size;
    gl->size += 4;
    *(ptr++) = ((char*)&theword)[0];
    *(ptr++) = ((char*)&theword)[1];
    *(ptr++) = ((char*)&theword)[2];
    *(ptr++) = ((char*)&theword)[3];
}

void AppendGListTargetEndianLong(GList* gl, UInt32 theword) {
    char* ptr;

    if ((gl->size + 4) > gl->hndlsize) {
        if (!COS_ResizeHandle(gl->data, gl->hndlsize += 4 + gl->growsize)) {
            GListError();
        }
    }

    ptr = *gl->data + gl->size;
    gl->size += 4;
    theword = CTool_EndianConvertWord32(theword);
    *(ptr++) = ((char*)&theword)[0];
    *(ptr++) = ((char*)&theword)[1];
    *(ptr++) = ((char*)&theword)[2];
    *(ptr++) = ((char*)&theword)[3];
}

void AppendGListID(GList* gl, const char* name) {
    size_t n = strlen(name) + 1;
    if ((gl->size + n) > gl->hndlsize) {
        if (!COS_ResizeHandle(gl->data, gl->hndlsize += n + gl->growsize)) {
            GListError();
        }
    }

    memcpy(*gl->data + gl->size, name, n);
    gl->size += n;
}

void AppendGListName(GList* gl, const char* name) {
    size_t n = strlen(name);
    if ((gl->size + n) > gl->hndlsize) {
        if (!COS_ResizeHandle(gl->data, gl->hndlsize += n + gl->growsize)) {
            GListError();
        }
    }

    memcpy(*gl->data + gl->size, name, n);
    gl->size += n;
}

void RemoveGListData(GList* gl, SInt32 size) {
    gl->size -= size;
    if (gl->hndlsize > (gl->size + gl->growsize * 2)) {
        COS_ResizeHandle(gl->data, gl->hndlsize = gl->size + gl->growsize);
    }
}

SInt16 GetGListByte(GList* gl) {
    return (*gl->data)[gl->size++];
}

SInt16 GetGListWord(GList* gl) {
    char* ptr;
    union {
        unsigned char bytes[2];
        short s;
    } data;

    // TODO: the Mac compiler's stabs give this a local `short n`, not a union (unused: no code to
    // check a spelling against)
    ptr = *gl->data + gl->size;
    gl->size += 2;
    data.bytes[0] = ptr[0];
    data.bytes[1] = ptr[1];
    return data.s;
}

SInt32 GetGListLong(GList* gl) {
    char* ptr;
    union {
        unsigned char bytes[4];
        long l;
    } data;

    // TODO: the Mac compiler's stabs give this a local `long n`, not a union (unused: no code to
    // check a spelling against)
    ptr = *gl->data + gl->size;
    gl->size += 4;
    data.bytes[0] = ptr[0];
    data.bytes[1] = ptr[1];
    data.bytes[2] = ptr[2];
    data.bytes[3] = ptr[3];
    return data.l;
}

// NOTE: invented names: len, src, dst
short GetGListID(GList* gl, char* name) {
    short len;
    char* src;
    char* dst;

    len = 1;
    src = *gl->data + gl->size;
    dst = name;
    while (*src) {
        *(dst++) = *(src++);
        len++;
    }

    *dst = *src;
    gl->size += len;
    return len;
}

void GetGListData(GList* gl, char* where, SInt32 size) {
    memcpy(where, *gl->data + gl->size, size);
    gl->size += size;
}

static UInt32 hashpjw(const char* p) {
    UInt32 h = 0;
    UInt32 g;

    while (*p) {
        h = (h << 4) + *p;
        g = h & 0xF0000000;
        if (g) {
            h ^= (g >> 24);
            h ^= g;
        }
        p++;
    }

    return h % 0xFFFFFD;
}

static SInt16 PHash(const unsigned char* string) {
    SInt16 i;
    SInt16 hashval;
    UInt8 u;

    if ((hashval = *(string++))) {
        i = hashval;
        u = 0;
        while (i > 0) {
            u = (u >> 3) | (u << 5);
            u += *(string++);
            --i;
        }

        hashval = (hashval << 8) | u;
    }

    return hashval & 0x7FF;
}

SInt16 CHash(const char* string) {
    SInt16 i;
    SInt16 hashval;
    UInt8 u;

    if ((hashval = (strlen(string) & 0xFF))) {
        i = hashval;
        u = 0;
        while (i > 0) {
            u = (u >> 3) | (u << 5);
            u += *(string++);
            --i;
        }
        hashval = (hashval << 8) | u;
    }

    return hashval & 0x7FF;
}

HashNameNode* GetHashNameNode(const char* name) {
    HashNameNode* node;
    SInt16 hashval; // NOTE: invented name

    if ((node = name_hash_nodes[hashval = CHash(name)]) == NULL) {
        name_hash_nodes[hashval] = node = galloc(strlen(name) + sizeof(HashNameNode));
        node->next = NULL;
        node->id = hash_name_id++;
        node->hashval = hashval;
        strcpy(node->name, name);
        return node;
    } else {
        for (;;) {
            if (strcmp(name, node->name) == 0) {
                if (node->id < 0) {
                    node->id = hash_name_id++;
                }
                return node;
            }

            if (node->next == NULL) {
                node->next = galloc(strlen(name) + sizeof(HashNameNode));
                node = node->next;
                node->next = NULL;
                node->id = hash_name_id++;
                node->hashval = hashval;
                strcpy(node->name, name);
                return node;
            } else {
                node = node->next;
            }
        }
    }
}

HashNameNode* GetHashNameNodeHash(const char* name, SInt16 hashval) {
    HashNameNode* node;

    if ((node = name_hash_nodes[hashval]) == NULL) {
        node = galloc(strlen(name) + sizeof(HashNameNode));
        name_hash_nodes[hashval] = node;
        node->next = NULL;
        node->id = hash_name_id++;
        node->hashval = hashval;
        strcpy(node->name, name);
        return node;
    } else {
        for (;;) {
            if (strcmp(name, node->name) == 0) {
                return node;
            }

            if (node->next == NULL) {
                node->next = galloc(strlen(name) + sizeof(HashNameNode));
                node = node->next;
                node->next = NULL;
                node->id = hash_name_id++;
                node->hashval = hashval;
                strcpy(node->name, name);
                return node;
            } else {
                node = node->next;
            }
        }
    }
}

HashNameNode* GetHashNameNodeHash2(const char* name, SInt16 hashval) {
    HashNameNode* node;

    if ((node = name_hash_nodes[hashval]) == NULL) {
        node = galloc(strlen(name) + sizeof(HashNameNode));
        name_hash_nodes[hashval] = node;
        node->next = NULL;
        node->id = -1;
        node->hashval = hashval;
        strcpy(node->name, name);
        return node;
    } else {
        for (;;) {
            if (strcmp(name, node->name) == 0) {
                return node;
            }

            if (node->next == NULL) {
                node->next = galloc(strlen(name) + sizeof(HashNameNode));
                node = node->next;
                node->next = NULL;
                node->id = -1;
                node->hashval = hashval;
                strcpy(node->name, name);
                return node;
            } else {
                node = node->next;
            }
        }
    }
}

HashNameNode* GetHashNameNodeExport(const char* name) {
    HashNameNode* node;
    SInt16 hashval; // NOTE: invented name

    if ((node = name_hash_nodes[hashval = CHash(name)]) == NULL) {
        node = galloc(strlen(name) + sizeof(HashNameNode));
        name_hash_nodes[hashval] = node;
        node->next = NULL;
        node->id = -1;
        node->hashval = hashval;
        strcpy(node->name, name);
        return node;
    } else {
        for (;;) {
            if (strcmp(name, node->name) == 0) {
                return node;
            }

            if (node->next == NULL) {
                node->next = galloc(strlen(name) + sizeof(HashNameNode));
                node = node->next;
                node->next = NULL;
                node->id = -1;
                node->hashval = hashval;
                strcpy(node->name, name);
                return node;
            } else {
                node = node->next;
            }
        }
    }
}

SInt32 GetHashNameNodeExportID(HashNameNode* node) {
    if (node->id < 0) {
        node->id = hash_name_id++;
    }
    return node->id;
}

HashNameNode* GetHashNameNodeByID(SInt32 id) {
    HashNameNode* node;
    short i;

    for (i = 0; i < 2048; i++) {
        for (node = name_hash_nodes[i]; node; node = node->next) {
            if (id == node->id) {
                return node;
            }
        }
    }

    return NULL;
}

void NameHashExportReset(void) {
    HashNameNode* node;
    short i;

    for (i = 0; i < 2048; i++) {
        node = name_hash_nodes[i];
        while (node) {
            node->id = -1;
            node = node->next;
        }
    }

    hash_name_id = 1;
}

void NameHashWriteNameTable(GList* glist) {
    HashNameNode* node;
    HashNameNode** nodes;
    short i;
    SInt32 n;

    nodes = galloc(sizeof(HashNameNode*) * hash_name_id);

    for (i = 0; i < 2048; i++) {
        node = name_hash_nodes[i];
        while (node) {
            if (node->id > 0) {
                nodes[node->id] = node;
            }
            node = node->next;
        }
    }

    for (n = 1; n < hash_name_id; n++) {
        node = nodes[n];
        AppendGListWord(glist, node->hashval);
        AppendGListID(glist, node->name);
    }

    if (glist->size & 1) {
        AppendGListByte(glist, 0);
    }
}

void NameHashWriteTargetEndianNameTable(GList* glist) {
    HashNameNode* node;
    HashNameNode** nodes;
    short i;
    SInt32 n;

    nodes = galloc(sizeof(HashNameNode*) * hash_name_id);
    memclrw(nodes, sizeof(HashNameNode*) * hash_name_id);

    for (i = 0; i < 2048; i++) {
        node = name_hash_nodes[i];
        while (node) {
            if (node->id > 0) {
                nodes[node->id] = node;
            }
            node = node->next;
        }
    }

    for (n = 1; n < hash_name_id; n++) {
        node = nodes[n];
        if (node == NULL) {
            AppendGListTargetEndianWord(glist, 0);
            AppendGListID(glist, "");
        } else {
            AppendGListTargetEndianWord(glist, node->hashval);
            AppendGListID(glist, node->name);
        }
    }

    if (glist->size & 1) {
        AppendGListByte(glist, 0);
    }
}

void InitNameHash(void) {
    name_hash_nodes = galloc(2048 * sizeof(HashNameNode*));
    memclrw(name_hash_nodes, 2048 * sizeof(HashNameNode*));
    hash_name_id = 1;
}

SInt32 CTool_TotalHeapSize(void) {
    HeapBlock* blockp;
    SInt32 size = 0;

    for (blockp = gheap.blocks; blockp; blockp = blockp->next) {
        size += blockp->blocksize;
    }
    for (blockp = lheap.blocks; blockp; blockp = blockp->next) {
        size += blockp->blocksize;
    }
    for (blockp = aheap.blocks; blockp; blockp = blockp->next) {
        size += blockp->blocksize;
    }
    for (blockp = oheap.blocks; blockp; blockp = blockp->next) {
        size += blockp->blocksize;
    }
    for (blockp = bheap.blocks; blockp; blockp = blockp->next) {
        size += blockp->blocksize;
    }

    return size;
}

static void getheapinfo(HeapInfo* result, HeapMem* heap) {
    HeapBlock* block;

    result->allocsize = heap->allocsize;
    for (block = heap->blocks; block; block = block->next) {
        result->total_size += block->blocksize - sizeof(HeapBlock);
        result->total_free += block->blockfree;
        result->blocks++;
        if (block->blockfree > result->largest_free_block) {
            result->largest_free_block = block->blockfree;
        }
    }

    result->average_block_size = result->total_size / result->blocks;
    result->average_block_free = result->total_free / result->blocks;
}

void CTool_GetHeapInfo(HeapInfo* result, unsigned char heapID) {
    memclrw(result, sizeof(HeapInfo));

    switch (heapID) {
        case 0:
            getheapinfo(result, &gheap);
            break;
        case 1:
            getheapinfo(result, &lheap);
            break;
        case 2:
            getheapinfo(result, &aheap);
            break;
        case 3:
            getheapinfo(result, &oheap);
            break;
        case 4:
            getheapinfo(result, &bheap);
            break;
        case 5:
            getheapinfo(result, &gheap);
            getheapinfo(result, &lheap);
            getheapinfo(result, &aheap);
            getheapinfo(result, &oheap);
            getheapinfo(result, &bheap);
            break;
    }
}

static void MoreHeapSpace(HeapMem* heapp, SInt32 size) {
    HeapBlock* blockp;
    Handle hndl;

    blockp = heapp->blocks;
    if (blockp) {
        heapp->curblock->blockfree = heapp->curfree;
        while (blockp) {
            if (blockp->blockfree >= size) {
                goto gotBlock;
            }
            blockp = blockp->next;
        }
    }

    // create new block
    size += heapp->allocsize;
    if (systemHandles) {
        hndl = COS_NewOSHandle(size << 1);
        if (hndl != 0) {
            goto createdTempDoubleBlock;
        }
        hndl = COS_NewOSHandle(size);
        if (hndl != 0) {
            goto createdBlock;
        }
    }
    hndl = COS_NewHandle(size);
    if (hndl != 0) {
        goto createdBlock;
    }

    if (heaperror) {
        heaperror();
    }
    return;

createdTempDoubleBlock:
    size <<= 1;
    goto createdBlock;
createdBlock:
    COS_LockHandleHi(hndl);
    blockp = (HeapBlock*)*hndl;
    blockp->next = heapp->blocks;
    heapp->blocks = blockp;
    blockp->blockhandle = hndl;
    blockp->blocksize = size;
    blockp->blockfree = size - sizeof(HeapBlock);
gotBlock:
    heapp->curblock = blockp;
    heapp->curfree = blockp->blockfree;
    heapp->curfreep = ((char*)blockp) + blockp->blocksize - blockp->blockfree;
}

short initheaps(heaperror_t heaperrorproc) {
    heaperror = NULL;
    lheaplockcount = 0;

    memclrw(&gheap, sizeof(HeapMem));
    memclrw(&lheap, sizeof(HeapMem));
    memclrw(&aheap, sizeof(HeapMem));
    memclrw(&oheap, sizeof(HeapMem));
    memclrw(&bheap, sizeof(HeapMem));

    gheap.allocsize = 0x38000;
    lheap.allocsize = 0x10000;
    aheap.allocsize = 0x4000;
    oheap.allocsize = 0x40000;
    bheap.allocsize = 0x4000;

    MoreHeapSpace(&gheap, 0);
    MoreHeapSpace(&lheap, 0);
    MoreHeapSpace(&aheap, 0);
    MoreHeapSpace(&oheap, 0);
    MoreHeapSpace(&bheap, 0);

    gheap.allocsize = 0x8000;
    lheap.allocsize = 0x8000;

    heaperror = heaperrorproc;

    if (!gheap.curblock ||
        !lheap.curblock ||
        !aheap.curblock ||
        !oheap.curblock ||
        !bheap.curblock) {
        return -1;
    } else {
        return 0;
    }
}

short initgheap(heaperror_t heaperrorproc) {
    heaperror = NULL;
    lheaplockcount = 0;

    memclrw(&gheap, sizeof(HeapMem));
    memclrw(&lheap, sizeof(HeapMem));
    memclrw(&aheap, sizeof(HeapMem));
    memclrw(&oheap, sizeof(HeapMem));
    memclrw(&bheap, sizeof(HeapMem));

    gheap.allocsize = 0x38000;
    MoreHeapSpace(&gheap, 0);
    gheap.allocsize = 0x8000;

    heaperror = heaperrorproc;

    if (!gheap.curblock) {
        return -1;
    } else {
        return 0;
    }
}

heaperror_t getheaperror(void) {
    return heaperror;
}

void setheaperror(heaperror_t heaperrorproc) {
    heaperror = heaperrorproc;
}

static void relheap(HeapMem* heapp) {
    HeapBlock* blockp;
    Handle hndl;

    blockp = heapp->blocks;
    while (blockp) {
        hndl = blockp->blockhandle;
        blockp = blockp->next;
        COS_FreeHandle(hndl);
    }

    memclrw(heapp, sizeof(HeapMem));
}

void releaseheaps(void) {
    relheap(&gheap);
    relheap(&lheap);
    relheap(&aheap);
    relheap(&oheap);
    relheap(&bheap);
}

void releasegheap(void) {
    relheap(&gheap);
}

void releaseoheap(void) {
    relheap(&gheap);
    oheap.allocsize = 0x40000;
    MoreHeapSpace(&oheap, 0);
}

void* galloc(SInt32 size) { // NOTE: invented name: size
    char* cp;

    size = (size & ~7) + 8;
    if (gheap.curfree < size) {
        MoreHeapSpace(&gheap, size);
    }

    gheap.curfree -= size;
    cp = gheap.curfreep;
    gheap.curfreep += size;
    return cp;
}

void* lalloc(SInt32 size) { // NOTE: invented name: size
    char* cp;

    size = (size & ~7) + 8;
    if (lheap.curfree < size) {
        MoreHeapSpace(&lheap, size);
    }

    lheap.curfree -= size;
    cp = lheap.curfreep;
    lheap.curfreep += size;
    return cp;
}

void* aalloc(SInt32 size) { // NOTE: invented name: size
    char* cp;

    size = (size & ~7) + 8;
    if (aheap.curfree < size) {
        MoreHeapSpace(&aheap, size);
    }

    aheap.curfree -= size;
    cp = aheap.curfreep;
    aheap.curfreep += size;
    return cp;
}

void* oalloc(SInt32 size) { // NOTE: invented name: size
    char* cp;

    size = (size & ~7) + 8;
    if (oheap.curfree < size) {
        MoreHeapSpace(&oheap, size);
    }

    oheap.curfree -= size;
    cp = oheap.curfreep;
    oheap.curfreep += size;
    return cp;
}

void* balloc(SInt32 size) { // NOTE: invented name: size
    char* cp;

    size = (size & ~7) + 8;
    if (bheap.curfree < size) {
        MoreHeapSpace(&bheap, size);
    }

    bheap.curfree -= size;
    cp = bheap.curfreep;
    bheap.curfreep = cp + size;
    return cp;
}

void locklheap(void) {
    lheaplockcount++;
}

void unlocklheap(void) {
    if (lheaplockcount > 0) {
        --lheaplockcount;
    }
}

// TODO (workaround): the original's freelheap, freeaheap and freeoheap are compiled as with symbols
// off, the rest of the unit with them on; this region stands in for whatever did that
#pragma sym off
void freelheap(void) {
    HeapBlock* blockp;

    if (lheaplockcount == 0) {
        blockp = lheap.blocks;
        lheap.curblock = blockp;
        lheap.curfreep = ((char*)blockp) + sizeof(HeapBlock);
        lheap.curfree = blockp->blocksize - sizeof(HeapBlock);

        while (blockp) {
            blockp->blockfree = blockp->blocksize - sizeof(HeapBlock);
            blockp = blockp->next;
        }
    }
}

void freeaheap(void) {
    HeapBlock* blockp;

    blockp = aheap.blocks;
    aheap.curblock = blockp;
    aheap.curfreep = ((char*)blockp) + sizeof(HeapBlock);
    aheap.curfree = blockp->blocksize - sizeof(HeapBlock);

    while (blockp) {
        blockp->blockfree = blockp->blocksize - sizeof(HeapBlock);
        blockp = blockp->next;
    }
}

void freeoheap(void) {
    HeapBlock* blockp;

    blockp = oheap.blocks;
    oheap.curblock = blockp;
    oheap.curfreep = ((char*)blockp) + sizeof(HeapBlock);
    oheap.curfree = blockp->blocksize - sizeof(HeapBlock);

    while (blockp) {
        blockp->blockfree = blockp->blocksize - sizeof(HeapBlock);
        blockp = blockp->next;
    }
}

#pragma sym reset
void freebheap(void) {
    HeapBlock* blockp;

    blockp = bheap.blocks;
    bheap.curblock = blockp;
    bheap.curfreep = ((char*)blockp) + sizeof(HeapBlock);
    bheap.curfree = blockp->blocksize - sizeof(HeapBlock);

    while (blockp) {
        blockp->blockfree = blockp->blocksize - sizeof(HeapBlock);
        blockp = blockp->next;
    }
}

char* ScanHex(char* string, UInt32* result, Boolean* overflow) {
    short c;
    UInt32 value; // NOTE: invented name

    value = 0;
    *overflow = FALSE;

    for (;;) {
        c = *string;
        if (c >= '0' && c <= '9') {
            c -= '0';
        } else if (c >= 'A' && c <= 'F') {
            c -= ('A' - 10);
        } else if (c >= 'a' && c <= 'f') {
            c -= ('a' - 10);
        } else {
            break;
        }

        if (value & 0xF0000000) {
            *overflow = TRUE;
        }
        value = (value << 4) | c;
        ++string;
    }

    *result = value;
    return string;
}

char* ScanOct(char* string, UInt32* result, Boolean* overflow) {
    short c;
    UInt32 value; // NOTE: invented name

    value = 0;
    *overflow = FALSE;

    for (;;) {
        c = *string - '0';
        if (c >= 0 && c <= 9) {
            if (c >= 8) {
                *overflow = TRUE;
                ++string;
                break;
            }
        } else {
            break;
        }

        if (value & 0xE0000000) {
            *overflow = TRUE;
        }
        value = (value << 3) | c;
        ++string;
    }

    *result = value;
    return string;
}

char* ScanDec(char* string, UInt32* result, Boolean* overflow) {
    short c;
    UInt32 value; // NOTE: invented name

    value = 0;
    *overflow = FALSE;

    for (;;) {
        c = *string - '0';
        if (c >= 0 && c <= 9) {
            if (value >= 0x19999999 && (value > 0x19999999 || c > 5)) {
                *overflow = TRUE;
            }
        } else {
            break;
        }

        value = (value << 3) + (value << 1) + c;
        ++string;
    }

    *result = value;
    return string;
}

static char* UnmangleClassname(
    char* name,
    char** classname,
    short* classnamesize,
    char** out,
    short* outsize,
    Boolean addclassname
) {
    char* cp;
    short i;
    short n;
    short os;

    *classnamesize = 0;
    if (*(name++) != '_' || *(name++) != '_') {
        return name;
    }

    n = 0;
    while (name[0] >= '0' && name[0] <= '9') {
        n = (n * 10) + name[0] - '0';
        ++name;
    }

    if (n > 0) {
        *classname = name;
        *classnamesize = n;

        if (addclassname) {
            cp = *out;
            os = *outsize;
            for (i = 0; i < n && os < 255; i++, os++) {
                *(cp++) = name[i];
            }
            if (os < 255) {
                *(cp++) = ':';
                os++;
            }
            if (os < 255) {
                *(cp++) = ':';
                os++;
            }
            *out = cp;
            *outsize = os;
        }
    }

    return name + n;
}

static void UnmangleAppend(char* name, char namesize, char** out, short* outsize) {
    char* cp;
    short i;
    short os;

    cp = *out;
    os = *outsize;

    for (i = 0; i < namesize && os < 255; i++, os++) {
        *(cp++) = name[i];
    }

    *out = cp;
    *outsize = os;
}

void OldUnmangle(char* name, char* out, Boolean full) {
    short n;
    short namesize;
    short classnamesize;
    char* classname;
    char* cptr;

    if (*name == '.') {
        ++name;
    }

    namesize = 0;
    cptr = name;
    if (name[0] == '_' && name[1] == '_') {
        if (name[2] == 'c' && name[3] == 't') {
            cptr = UnmangleClassname(&name[4], &classname, &classnamesize, &out, &namesize, full);
            if (classnamesize == 0) {
                goto invalid;
            }
            UnmangleAppend(classname, classnamesize, &out, &namesize);
        } else if (name[2] == 'd' && name[3] == 't') {
            cptr = UnmangleClassname(&name[4], &classname, &classnamesize, &out, &namesize, full);
            if (classnamesize == 0) {
                goto invalid;
            }
            if (namesize <= 255) {
                *(out++) = '~';
                namesize++;
            }
            UnmangleAppend(classname, classnamesize, &out, &namesize);
        } else if (name[2] == 'n' && name[3] == 'w') {
            cptr = UnmangleClassname(&name[4], &classname, &classnamesize, &out, &namesize, full);
            // (the original's bug: 15 characters from the 12 of "operator new")
            UnmangleAppend("operator new", 15, &out, &namesize);
        } else if (name[2] == 'd' && name[3] == 'l') {
            cptr = UnmangleClassname(&name[4], &classname, &classnamesize, &out, &namesize, full);
            UnmangleAppend("operator delete", 15, &out, &namesize);
        } else {
invalid:
            strncpy(out, name, 256);
            return;
        }
    } else {
        n = 0;
        while (*cptr) {
            ++cptr;
            ++n;
            if (cptr[0] == '_' && cptr[1] == '_') {
                cptr = UnmangleClassname(cptr, &classname, &classnamesize, &out, &namesize, full);
                break;
            }
        }
        UnmangleAppend(name, n, &out, &namesize);
    }

    if (full && cptr[0] == 'F') {
        if (cptr[1] == 'v') {
            UnmangleAppend("(void)", 6, &out, &namesize);
        } else {
            UnmangleAppend("(...)", 5, &out, &namesize);
        }
    }

    *out = 0;
}

short hash(char* str) { // NOTE: invented name: str
    char hash = 0;

    while (*str) {
        hash = hash << 1;
        hash ^= *(str++);
    }

    return hash & 0x7F;
}

void memclr(void* ptr, SInt32 size) {
    memset(ptr, 0, size);
}

void memclrw(void* ptr, SInt32 size) {
    memset(ptr, 0, size);
}

// NOTE: invented names: src, dst
void CToLowercase(char* src, char* dst) {
    while ((*dst++ = (char)tolower(*src++)) != 0) {}
}

short getbit(SInt32 mask) { // NOTE: invented name: mask
    switch (mask) {
        case 0:
            return -1;
        case 1:
            return 0;
        case 2:
            return 1;
        case 4:
            return 2;
        case 8:
            return 3;
        case 0x10:
            return 4;
        case 0x20:
            return 5;
        case 0x40:
            return 6;
        case 0x80:
            return 7;
        case 0x100:
            return 8;
        case 0x200:
            return 9;
        case 0x400:
            return 10;
        case 0x800:
            return 11;
        case 0x1000:
            return 12;
        case 0x2000:
            return 13;
        case 0x4000:
            return 14;
        case 0x8000:
            return 15;
        case 0x10000:
            return 16;
        case 0x20000:
            return 17;
        case 0x40000:
            return 18;
        case 0x80000:
            return 19;
        case 0x100000:
            return 20;
        case 0x200000:
            return 21;
        case 0x400000:
            return 22;
        case 0x800000:
            return 23;
        case 0x1000000:
            return 24;
        case 0x2000000:
            return 25;
        case 0x4000000:
            return 26;
        case 0x8000000:
            return 27;
        case 0x10000000:
            return 28;
        case 0x20000000:
            return 29;
        case 0x40000000:
            return 30;
        case 0x80000000:
            return 31;
        default:
            return -2;
    }
}

UInt16 CTool_EndianConvertWord16(UInt16 theword) {
    union {
        UInt16 w;
        UInt8 b[2];
    } in, out;
    if (copts.littleendian) {
        return theword;
    }
    in.w = theword;
    out.b[0] = in.b[1];
    out.b[1] = in.b[0];
    return out.w;
}

UInt32 CTool_EndianConvertWord32(UInt32 theword) {
    union {
        UInt32 w;
        UInt8 b[4];
    } in, out;
    if (copts.littleendian) {
        return theword;
    }
    in.w = theword;
    out.b[0] = in.b[3];
    out.b[1] = in.b[2];
    out.b[2] = in.b[1];
    out.b[3] = in.b[0];
    return out.w;
}

// NOTE: invented names: front, back
void CTool_EndianConvertMem(void* data, short len) {
    UInt8* front;
    UInt8* back;
    UInt8 val;

    if (copts.littleendian) {
        return;
    }
    front = data;
    back = (UInt8*)data + len;
    for (;;) {
        back--;
        if (back <= front) {
            break;
        }
        val = *back;
        *back = *front;
        *front = val;
        front++;
    }
}

void CTool_EndianConvertWord64(CInt64 ci, char* result) {
    UInt32 buf[2];
    if (!copts.littleendian) {
        buf[0] = CTool_EndianConvertWord32(ci.hi);
        buf[1] = CTool_EndianConvertWord32(ci.lo);
    } else {
        buf[0] = CTool_EndianConvertWord32(ci.lo);
        buf[1] = CTool_EndianConvertWord32(ci.hi);
    }
    memcpy(result, buf, 8);
}

UInt16 CTool_EndianConvertInPlaceWord16Ptr(UInt16* x) {
    *x = CTool_EndianConvertWord16(*x);
    return *x;
}

UInt32 CTool_EndianConvertInPlaceWord32Ptr(UInt32* x) {
    *x = CTool_EndianConvertWord32(*x);
    return *x;
}

void CTool_EndianConvertVector128(void) {}

HashNameNode* CTool_GetPathName(const struct FSSpec* fss, SInt32* moddateptr) {
    char name[256];
    COS_FileGetPathName(name, fss, moddateptr);
    return GetHashNameNodeExport(name);
}

int strcat_safe(char* dest, const char* src, SInt32 len) {
    SInt32 dest_len;
    char ch;

    dest_len = strlen(dest);
    dest += dest_len;
    len -= dest_len;
    if (len < 0) {
        return 1;
    }

    while (len != 0) {
        ch = *(dest++) = *(src++);
        if (ch == 0) {
            break;
        }
        --len;
    }

    if (len == 0 && dest[-1]) {
        dest[-1] = 0;
        return 1;
    } else {
        return 0;
    }
}

// The unit's statics are 2-byte packed, as in the original (MWCC lays out .bss with the packing in
// force at the end of the file)
#pragma pack(2)
