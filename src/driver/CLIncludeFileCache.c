#include "driver/CLIncludeFileCache.h"
#include "driver/Memory.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"

typedef struct CacheEntry {
    struct CacheEntry* next;
    struct CacheEntry* prev;
    int locked;
    int precompiled;
    Handle text;
    OSSpec spec;
    UInt32 size;
    UInt32 when;
} CacheEntry;

enum {
    CACHE_SIZE = 0x800000
};

static CacheEntry* cachelist;
static CacheEntry* freelist;
static UInt32 availablecache;

static CacheEntry* makecacheentry(void) {
    CacheEntry* c;

    if (freelist) {
        c = freelist;
        freelist = c->next;
    } else {
        c = xmalloc("include file cache", sizeof(CacheEntry));
    }
    return c;
}

static void insertcacheentry(CacheEntry* c) {
    if (cachelist) {
        cachelist->prev = c;
    }
    c->next = cachelist;
    c->prev = NULL;
    cachelist = c;
}

static void deletecacheentry(CacheEntry* c) {
    if (c->next) {
        c->next->prev = c->prev;
    }
    if (c->prev) {
        c->prev->next = c->next;
    } else {
        cachelist = c->next;
    }
}

void InitializeIncludeCache(void) {
    cachelist = freelist = NULL;
    availablecache = CACHE_SIZE;
}

void CleanupIncludeCache(void) {
    CacheEntry* c;
    CacheEntry* c1;

    for (c = cachelist; c; c = c1) {
        c1 = c->next;
        if (c->text) {
            DisposeHandle(c->text);
        }
        xfree(c);
    }

    for (c = freelist; c; c = c1) {
        c1 = c->next;
        xfree(c);
    }

    InitializeIncludeCache();
}

void CacheIncludeFile(OSSpec* spec, Handle text, Boolean precompiled) {
    SInt32 size;
    CacheEntry* c;
    CacheEntry* lru;

    size = GetHandleSize(text);
    if (precompiled) {
        lru = NULL;
        for (c = cachelist; c; c = c->next) {
            if (c->precompiled) {
                lru = c;
                break;
            }
        }
        if (lru) {
            DisposeHandle(lru->text);
            lru->text = NULL;
            deletecacheentry(lru);
            lru->next = freelist;
            freelist = lru;
        }
    } else {
        if ((SInt32)size > (SInt32)CACHE_SIZE) {
            return;
        } else {
            while ((UInt32)size > availablecache) {
                lru = NULL;
                for (c = cachelist; c; c = c->next) {
                    if (!c->locked && !c->precompiled && (lru == NULL || c->when < lru->when)) {
                        lru = c;
                    }
                }

                if (lru) {
                    DisposeHandle(lru->text);
                    lru->text = NULL;
                    availablecache += lru->size;
                    deletecacheentry(lru);
                    lru->next = freelist;
                    freelist = lru;
                } else {
                    return;
                }
            }
        }
    }

    c = makecacheentry();
    c->locked = 1;
    c->precompiled = precompiled;
    c->spec = *spec;
    c->text = text;
    c->size = size;
    c->when = OS_GetMilliseconds();
    insertcacheentry(c);

    if (!precompiled) {
        availablecache -= size;
    }
}

Handle CachedIncludeFile(OSSpec* spec, Boolean* precompiled) {
    CacheEntry* c;

    for (c = cachelist; c; c = c->next) {
        if (OS_EqualSpec(&c->spec, spec)) {
            c->when = OS_GetMilliseconds();
            *precompiled = c->precompiled;
            c->locked++;
            return c->text;
        }
    }

    return NULL;
}

void FreeIncludeFile(Handle text) {
    CacheEntry* c;

    for (c = cachelist; c; c = c->next) {
        if (c->text == text) {
            c->locked--;
            return;
        }
    }

    DisposeHandle(text);
}
