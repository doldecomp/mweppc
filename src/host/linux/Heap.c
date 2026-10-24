#include "Heap.h"
#include "Linux.h"

#define ALIGNMENT 16
#define HEADER 16
#define CLASSES 4096
#define LARGE (CLASSES * ALIGNMENT)
#define CHUNK (1u << 20)
#define LIMIT 0x7FFFFFFFu

typedef struct Header {
    unsigned int capacity;
    unsigned int size;
} Header;

typedef struct Block {
    struct Block* next;
} Block;

static Block* free_lists[CLASSES];
static char* chunk;
static unsigned int chunk_left;

static void Zero(void* p, unsigned int size) {
    char* c = p;
    while (size--) {
        *c++ = 0;
    }
}

static void Copy(void* to, const void* from, unsigned int size) {
    char* t = to;
    const char* f = from;
    while (size--) {
        *t++ = *f++;
    }
}

void* Heap_MapAligned(unsigned int size, unsigned int alignment) {
    char* p;
    unsigned long start;
    unsigned long end;

    if (size > LIMIT || alignment > LIMIT) {
        return 0;
    }
    size = (size + 4095) & ~4095u;
    p = Linux_Map(size + alignment);
    if (Linux_MapFailed(p)) {
        return 0;
    }
    start = ((unsigned long)p + alignment - 1) & ~(unsigned long)(alignment - 1);
    end = start + size;
    if (start != (unsigned long)p) {
        Linux_Unmap(p, start - (unsigned long)p);
    }
    if ((unsigned long)p + size + alignment != end) {
        Linux_Unmap((void*)end, (unsigned long)p + size + alignment - end);
    }
    return (void*)start;
}

static Header* HeaderOf(void* p) {
    return (Header*)((char*)p - HEADER);
}

static unsigned int Capacity(unsigned int size) {
    return size ? (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1) : ALIGNMENT;
}

void* Heap_Alloc(unsigned int size, int zero) {
    unsigned int capacity;
    char* p;

    if (size > LIMIT) {
        return 0;
    }
    capacity = Capacity(size);
    if (capacity >= LARGE) {
        p = Heap_MapAligned(capacity + HEADER, 4096);
        if (p == 0) {
            return 0;
        }
        p += HEADER;
    } else if (free_lists[capacity / ALIGNMENT]) {
        p = (char*)free_lists[capacity / ALIGNMENT];
        free_lists[capacity / ALIGNMENT] = ((Block*)p)->next;
        if (zero) {
            Zero(p, capacity);
        }
    } else {
        if (chunk_left < capacity + HEADER) {
            chunk = Heap_MapAligned(CHUNK, 4096);
            if (chunk == 0) {
                return 0;
            }
            chunk_left = CHUNK;
        }
        p = chunk + HEADER;
        chunk += capacity + HEADER;
        chunk_left -= capacity + HEADER;
    }
    HeaderOf(p)->capacity = capacity;
    HeaderOf(p)->size = size;
    return p;
}

int Heap_Free(void* p) {
    unsigned int capacity;

    if (p == 0) {
        return 0;
    }
    capacity = HeaderOf(p)->capacity;
    if (capacity >= LARGE) {
        Linux_Unmap((char*)p - HEADER, (capacity + HEADER + 4095) & ~4095u);
    } else {
        ((Block*)p)->next = free_lists[capacity / ALIGNMENT];
        free_lists[capacity / ALIGNMENT] = p;
    }
    return 1;
}

void* Heap_Realloc(void* p, unsigned int size, int zero) {
    Header* header;
    void* q;

    if (p == 0) {
        return 0;
    }
    header = HeaderOf(p);
    if (size <= header->capacity) {
        if (zero && size > header->size) {
            Zero((char*)p + header->size, size - header->size);
        }
        header->size = size;
        return p;
    }
    q = Heap_Alloc(size, zero);
    if (q == 0) {
        return 0;
    }
    Copy(q, p, header->size);
    Heap_Free(p);
    return q;
}
