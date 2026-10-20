#include "driver/Resources.h"
#include "compiler/win32.h"
#include "driver/Files.h"
#include "driver/MacSpecs.h"
#include "driver/Memory.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include "driver/StringUtils.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The Mac Resource Manager over in-memory maps. The big-endian fork's fields are byte-swapped as
// they are read and written.

#define SWAP16(x) ((((x) & 0xff00) >> 8) | (((x) & 0xff) << 8))
#define SWAP32(x) \
    (((((UInt32)(x)) & 0xff000000u) >> 24) | ((((UInt32)(x)) & 0x00ff0000u) >> 8) | \
     ((((UInt32)(x)) & 0x0000ff00u) << 8) | ((((UInt32)(x)) & 0x000000ffu) << 24))

// Fork Attributes
enum {
    mapReadOnly = 0x80,
    mapCompact = 0x40,
    mapChanged = 0x20
};

// Resources
enum {
    resSysHeap = 0x40,
    resPurgeable = 0x20,
    resLocked = 0x10,
    resProtected = 8,
    resPreload = 4,
    resChanged = 2
};

// NOTE: invented names: MemRsrcSysData, ResourceHeader, ResourceMapHeader, ResourceTypeItem,
// ResourceRefItem, ResourceNameItem, ResourceDataItem (the record types)
// The fork's on-disk records (mac68k-aligned).
#pragma pack(push, 2)
typedef struct {
    char sysdata[66];
    char type[4];
    char creator[4];
    char sysdata2[38];
    char appdata[128];
} MemRsrcSysData;

typedef struct { // Resource Header
    UInt32 data_offs;
    UInt32 map_offs;
    UInt32 data_len;
    UInt32 map_len;
} ResourceHeader;
typedef struct { // Resource Map
    ResourceHeader mem_hdr;
    Handle mem_next_map_handle;
    SInt16 mem_refnum;
    UInt16 fork_attr;
    UInt16 typelist_offs;
    UInt16 namelist_offs;
    UInt16 types_idx;
} ResourceMapHeader;
typedef struct { // Resource Type List Item
    OSType type;
    UInt16 rsrc_idx;
    UInt16 ref_list_offs;
} ResourceTypeItem;
typedef struct { // Reference List Item
    UInt16 id;
    UInt16 name_offs;
    unsigned char attr;
    unsigned char data_offs[3];
    Handle mem_rsrc_handle;
} ResourceRefItem;
typedef struct { // Name List Item
    unsigned char len;
} ResourceNameItem;
typedef struct { // Resource
    UInt32 len;
} ResourceDataItem;
#pragma pack(pop)

typedef struct MemRefList {
    UInt16 id;
    OSType type;
    StringPtr name;
    unsigned char attr;
    Handle hand;
    struct MemRefList* next;
} MemRefList;

typedef struct MemRsrcTypeList {
    OSType type;
    MemRefList* ref_list;
    struct MemRsrcTypeList* next;
} MemRsrcTypeList;

typedef struct MemRsrcMap {
    SInt16 refnum;
    UInt16 fork_attr;
    MemRsrcTypeList* type_list;
    MemRsrcSysData sys_data;
    struct MemRsrcMap* prev;
} MemRsrcMap;

typedef struct ResRef {
    OSSpec spec;
    int ref;
    struct ResRef* next;
} ResRef;

static OSErr resError;
static SInt16 cur_res_file;
Boolean ResLoad;
static MemRsrcMap* maplist;
static ResRef* resForkList;
static ResRef* resForkLast;
static Boolean UseResourceForkInfo;

static void ReadResourceFork(SInt16 ref, SInt8 permission, void* file_data, SInt32 file_size);
static void WriteEmptyResourceFork(SInt16 ref);
SInt16 __stdcall FSpOpenResFile(const FSSpec* spec, SInt8 permission);
void __stdcall CloseResFile(SInt16 refNum);

Boolean OS_UsingMacResourceForkInfo(void) {
    return UseResourceForkInfo;
}

// (no caller: the linker dropped it)
void OS_UseMacResourceForkInfo(Boolean which) {
    UseResourceForkInfo = which;
}

OSErr SystemInitResources(OSSpec* spec) {
    FSSpec prog;
    void* file_data;
    SInt32 file_len;

    maplist = NULL;
    cur_res_file = 0;

    if (!OS_LoadMacResourceFork(spec, &file_data, &file_len)) {
        ReadResourceFork(-1, fsRdPerm, file_data, file_len);
        cur_res_file = -1;
        resError = noErr;
        return noErr;
    } else {
        if (!OS_OSSpec_To_FSSpec(spec, &prog) && !FSpOpenResFile(&prog, fsRdPerm)) {
            resError = noErr;
            return noErr;
        } else {
            resError = fnfErr;
            return fnfErr;
        }
    }
}

// NOTE: invented name: unused
// (unused: never read, the original's).
static MemRsrcMap* NewResourceMap(SInt16 ref, UInt16 attr, MemRsrcTypeList* list, void* unused) {
    MemRsrcMap** scan;
    MemRsrcMap* nw;

    scan = &maplist;
    nw = malloc(sizeof(MemRsrcMap));

    if (!nw) {
        resError = memFullErr;
        return NULL;
    }

    nw->refnum = ref;
    nw->fork_attr = attr;
    nw->type_list = list;
    nw->prev = *scan;
    *scan = nw;
    return nw;
}

static MemRsrcTypeList* NewTypeListEntry(
    MemRsrcTypeList** scan, OSType type, MemRefList* list, MemRsrcTypeList* next
) {
    MemRsrcTypeList* nw;

    while (*scan) {
        scan = &(*scan)->next;
    }

    nw = malloc(sizeof(MemRsrcTypeList));
    if (!nw) {
        resError = memFullErr;
        return NULL;
    }

    nw->type = type;
    nw->ref_list = list;
    nw->next = next;
    *scan = nw;
    return nw;
}

static MemRefList* NewRefListEntry(
    MemRefList** scan, UInt16 id, OSType type, StringPtr name, unsigned char attr, Handle hand
) {
    MemRefList* prev;
    MemRefList* nw;
    char mname[256];

    prev = NULL;
    if (name) {
        p2cstrcpy(mname, name);
    } else {
        strcpy(mname, "(none)");
    }

    while (*scan && (*scan)->id != id) {
        prev = *scan;
        scan = &(*scan)->next;
    }

    nw = malloc(sizeof(MemRefList));
    if (!nw) {
        resError = memFullErr;
        return NULL;
    }

    nw->id = id;
    nw->type = type;
    nw->name = name;
    nw->attr = attr;
    nw->hand = hand;
    if (prev) {
        nw->next = prev->next;
        prev->next = nw;
    } else {
        nw->next = NULL;
    }
    *scan = nw;
    return nw;
}

static MemRsrcMap* FindResourceMap(SInt16 ref) {
    MemRsrcMap* list = maplist;

    while (list) {
        if (list->refnum == ref) {
            return list;
        }
        list = list->prev;
    }

    return NULL;
}

static MemRsrcMap* FindResourceMap2(SInt16 ref, MemRsrcMap** next) {
    MemRsrcMap* list = maplist;

    *next = NULL;
    while (list) {
        if (list->refnum == ref) {
            return list;
        }
        *next = list;
        list = list->prev;
    }

    return NULL;
}

static void DeleteResources(SInt16 refNum) {
    MemRsrcMap *rm, *rmnext;
    MemRsrcTypeList *mtyptmp, *mtyp;
    MemRefList *mrletmp, *mrle;

    rm = FindResourceMap2(refNum, &rmnext);
    if (!rm) {
        return;
    }

    mtyp = rm->type_list;
    while (mtyp) {
        mrle = mtyp->ref_list;
        while (mrle) {
            if (mrle->hand) {
                DisposeHandle(mrle->hand);
            }
            if (mrle->name) {
                free(mrle->name);
            }
            mrletmp = mrle;
            mrle = mrle->next;
            free(mrletmp);
        }
        mtyptmp = mtyp;
        mtyp = mtyp->next;
        free(mtyptmp);
    }

    if (rmnext) {
        rmnext->prev = rm->prev;
    } else {
        maplist = rm->prev;
    }
    free(rm);
}

#define RF_READ(buf, offset, count, failBranch) \
    do { \
        SInt32 __offs = (offset); \
        if (!file_data) { \
            if ((resError = SetFPos(ref, fsFromStart, __offs)) != noErr) { \
                goto failBranch; \
            } \
            if ((resError = FSRead(ref, (SInt32*)&(count), buf)) != noErr) { \
                goto failBranch; \
            } \
        } else { \
            if ((__offs + (count)) > file_size) { \
                goto failBranch; \
            } \
            memcpy(buf, ((unsigned char*)file_data) + __offs, (count)); \
        } \
    } while (0)

static void ReadResourceFork(SInt16 ref, SInt8 permission, void* file_data, SInt32 file_size) {
    MemRsrcMap* rm;
    ResourceHeader thdr;
    ResourceHeader* dhdr;
    MemRsrcSysData tsys;
    MemRsrcSysData* dsys;
    ResourceMapHeader* dmap;
    ResourceTypeItem* dtyp;
    ResourceRefItem* drle;
    ResourceNameItem* dnam;
    void* buffer;
    SInt32 msize;
    SInt32 cnt;
    SInt32 ref_offs;
    int rcnt;
    MemRsrcTypeList* mtyp;
    ResourceDataItem tent;
    SInt32 esize;
    SInt32 offs;
    Handle hand;
    unsigned char* name;
    unsigned char* nameptr;

    if (!file_data) {
        if ((resError = GetEOF(ref, &file_size)) != noErr) {
            goto mapFail;
        }
    }

    if (file_size < 90) {
        goto mapFail;
    }

    ref_offs = 0;
    cnt = sizeof(ResourceHeader);
    RF_READ(&thdr, ref_offs, cnt, mapFail);
    ref_offs += cnt;

    thdr.map_offs = SWAP32(thdr.map_offs);
    thdr.map_len = SWAP32(thdr.map_len);
    thdr.data_offs = SWAP32(thdr.data_offs);
    thdr.data_len = SWAP32(thdr.data_len);

    cnt = ((thdr.map_offs < thdr.data_offs) ? thdr.map_offs : thdr.data_offs) -
        sizeof(ResourceHeader);
    if ((size_t)cnt > sizeof(MemRsrcSysData)) {
        cnt = sizeof(MemRsrcSysData);
    }
    RF_READ(&tsys, ref_offs, cnt, mapFail);

    msize = thdr.map_len;
    buffer = malloc(msize);
    if (!buffer) {
        goto memFail;
    }

    ref_offs = thdr.map_offs;
    RF_READ(buffer, ref_offs, msize, mapFail);

    dhdr = &thdr;
    if ((dhdr->map_offs > (UInt32)file_size) ||
        ((dhdr->map_offs + dhdr->map_len) > (UInt32)file_size) ||
        (dhdr->data_offs > (UInt32)file_size) ||
        ((dhdr->data_offs + dhdr->data_len) > (UInt32)file_size) ||
        ((dhdr->map_offs < dhdr->data_offs)
             ? ((dhdr->map_offs + dhdr->map_len) > dhdr->data_offs)
             : ((dhdr->data_offs + dhdr->data_len) > dhdr->map_offs))) {
        goto mapFail;
    }

    dsys = &tsys;
    dmap = (ResourceMapHeader*)buffer;
    rm = NewResourceMap(ref, ((permission == fsRdPerm) ? mapReadOnly : 0) | mapCompact, NULL, NULL);
    if (!rm) {
        goto memFail;
    }

    rm->sys_data = *dsys;

    dmap->fork_attr = SWAP16(dmap->fork_attr);
    dmap->typelist_offs = SWAP16(dmap->typelist_offs);
    dmap->namelist_offs = SWAP16(dmap->namelist_offs);
    dmap->types_idx = SWAP16(dmap->types_idx);

    if ((dmap->typelist_offs + dhdr->map_offs) > (UInt32)file_size) {
        goto freeAndMapFail;
    }
    dtyp = (ResourceTypeItem*)(((unsigned char*)dmap) + dmap->typelist_offs + 2);
    if ((dmap->namelist_offs + dhdr->map_offs) > (UInt32)file_size) {
        goto freeAndMapFail;
    }
    dnam = (ResourceNameItem*)(((unsigned char*)dmap) + dmap->namelist_offs);
    if (dmap->types_idx != 0xFFFF) {
        for (cnt = 0; cnt <= dmap->types_idx; cnt++) {
            dtyp[cnt].type = SWAP32(dtyp[cnt].type);
            dtyp[cnt].rsrc_idx = SWAP16(dtyp[cnt].rsrc_idx);
            dtyp[cnt].ref_list_offs = SWAP16(dtyp[cnt].ref_list_offs);
            mtyp = NewTypeListEntry(&rm->type_list, dtyp[cnt].type, NULL, NULL);
            if (!mtyp) {
                goto freeAndReturn;
            }

            if ((dmap->typelist_offs + dtyp[cnt].ref_list_offs + dhdr->map_offs) >
                (UInt32)file_size) {
                goto freeAndMapFail;
            }

            drle = (ResourceRefItem*)(((unsigned char*)dtyp) + dtyp[cnt].ref_list_offs - 2);
            if (dtyp[cnt].rsrc_idx != 0xFFFF) {
                for (rcnt = 0; rcnt <= dtyp[cnt].rsrc_idx; rcnt++) {
                    drle[rcnt].id = SWAP16(drle[rcnt].id);
                    drle[rcnt].name_offs = SWAP16(drle[rcnt].name_offs);
                    ref_offs = (drle[rcnt].data_offs[0] << 16) | (drle[rcnt].data_offs[1] << 8) |
                        drle[rcnt].data_offs[2];
                    if ((ref_offs + dhdr->data_offs) > (UInt32)file_size) {
                        goto freeAndMapFail;
                    }
                    esize = 4;
                    offs = ref_offs + dhdr->data_offs;
                    RF_READ(&tent, offs, esize, freeAndMapFail);

                    offs += esize;
                    tent.len = SWAP32(tent.len);
                    if ((tent.len + ref_offs + dhdr->data_offs) > (UInt32)file_size) {
                        goto freeAndMapFail;
                    }

                    hand = NewHandle(tent.len);
                    if (!hand) {
                        goto freeAndMapFail;
                    }

                    HLock(hand);
                    // TODO: a workaround: the length read back as a signed count through a pun;
                    // the original's spelling is unknown
                    RF_READ(*hand, offs, *(SInt32*)&tent.len, freeAndMapFail);
                    HUnlock(hand);

                    if (drle[rcnt].name_offs != 0xFFFF) {
                        nameptr = (StringPtr)dnam + drle[rcnt].name_offs;
                        name = malloc(nameptr[0] + 1);
                        memcpy(name, nameptr, nameptr[0] + 1);
                    } else {
                        name = NULL;
                    }

                    if (!NewRefListEntry(
                            &mtyp->ref_list,
                            drle[rcnt].id,
                            dtyp[cnt].type,
                            name,
                            drle[rcnt].attr,
                            hand
                        )) {
                        goto freeAndReturn;
                    }
                }
            }
        }
    }

    free(buffer);
    resError = noErr;
    return;
freeAndMapFail:
    resError = mapReadErr;
freeAndReturn:
    DeleteResources(ref);
    if (buffer) {
        free(buffer);
    }
    return;
memFail:
    resError = memFullErr;
    return;
mapFail:
    resError = mapReadErr;
}

static void GetResourceSizes(
    MemRsrcMap* rm,
    SInt32* data_size,
    SInt32* name_size,
    SInt32* reflist_size,
    SInt32* typelist_size
) {
    MemRsrcTypeList* mtyp;
    MemRefList* mrle;

    *data_size = *name_size = *reflist_size = *typelist_size = 0;

    mtyp = rm->type_list;
    while (mtyp) {
        mrle = mtyp->ref_list;
        while (mrle) {
            *name_size += mrle->name == NULL ? 0 : (mrle->name[0] + 1);
            *data_size += (mrle->hand == NULL ? 0 : GetHandleSize(mrle->hand)) + 4;
            *reflist_size += 12;
            mrle = mrle->next;
        }
        *typelist_size += 8;
        mtyp = mtyp->next;
    }
}

static SInt32 GetResourceTypesCount(MemRsrcMap* dm) {
    SInt32 count; // NOTE: invented name
    MemRsrcTypeList* mtyp;

    count = 0;
    mtyp = dm->type_list;
    while (mtyp) {
        ++count;
        mtyp = mtyp->next;
    }

    return count;
}

static SInt32 GetResourceEntriesCount(MemRsrcTypeList* mtyp) {
    SInt32 count; // NOTE: invented name
    MemRefList* mrle;

    count = 0;
    mrle = mtyp->ref_list;
    while (mrle) {
        ++count;
        mrle = mrle->next;
    }

    return count;
}

#define RF_HANDLE_INSERT(hand, what, offset, size) \
    do { \
        SInt32 __offs = (offset); \
        SInt32 __len = (size); \
        if ((SInt32)(__offs + __len) > GetHandleSize(hand)) { \
            resError = mapReadErr; \
            return; \
        } else { \
            memcpy(*(hand) + __offs, (what), __len); \
        } \
    } while (0)

static void WriteResourceFork(SInt16 ref) {
    MemRsrcMap* rm;
    ResourceHeader dhdr;
    ResourceHeader thdr;
    MemRsrcSysData tsys;
    ResourceMapHeader dmap;
    ResourceMapHeader tmap;
    Handle hand_ref;
    SInt32 reflist_size, typelist_size;
    SInt32 name_size, data_size;
    SInt32 reflist_offs, typelist_offs;
    SInt32 name_offs, data_offs;
    MemRsrcTypeList* mtyp;
    ResourceTypeItem dtyp;
    ResourceTypeItem ttyp;
    MemRefList* mrle;
    ResourceRefItem drle;
    ResourceRefItem trle;
    ResourceDataItem dent;
    ResourceDataItem tent;
    SInt32 sz;

    rm = FindResourceMap(ref);
    if (!rm) {
        resError = resFNotFound;
        return;
    }

    if (rm->refnum == -1) {
        resError = resFNotFound;
        return;
    }

    if (rm->fork_attr & mapReadOnly) {
        resError = resAttrErr;
        return;
    }

    if (!(rm->fork_attr & mapChanged)) {
        return;
    }

    GetResourceSizes(rm, &data_size, &name_size, &reflist_size, &typelist_size);
    dhdr.data_offs = sizeof(dhdr) + sizeof(tsys);
    dhdr.data_len = data_size;
    dhdr.map_offs = dhdr.data_len + dhdr.data_offs;
    dhdr.map_len = name_size + reflist_size + typelist_size + sizeof(dmap);

    hand_ref = NewHandle(dhdr.map_offs + dhdr.map_len);
    if (!hand_ref) {
        resError = MemError();
        return;
    }

    HLock(hand_ref);
    memset(*hand_ref, 0, dhdr.map_offs + dhdr.map_len);
    thdr = dhdr;
    thdr.data_offs = SWAP32(thdr.data_offs);
    thdr.map_offs = SWAP32(thdr.map_offs);
    thdr.data_len = SWAP32(thdr.data_len);
    thdr.map_len = SWAP32(thdr.map_len);
    RF_HANDLE_INSERT(hand_ref, &thdr, 0, sizeof(thdr));

    tsys = rm->sys_data;
    RF_HANDLE_INSERT(hand_ref, &tsys, sizeof(thdr), sizeof(tsys));

    dmap.mem_hdr = thdr;
    dmap.mem_next_map_handle = NULL;
    dmap.mem_refnum = 0;
    dmap.fork_attr = rm->fork_attr & ~resChanged; // (the original's bug: mapChanged is meant)
    dmap.typelist_offs = sizeof(dmap) - 2;
    dmap.namelist_offs = typelist_size + reflist_size + sizeof(dmap);
    dmap.types_idx = GetResourceTypesCount(rm) - 1;
    tmap = dmap;
    tmap.fork_attr = SWAP16(tmap.fork_attr);
    tmap.typelist_offs = SWAP16(tmap.typelist_offs);
    tmap.namelist_offs = SWAP16(tmap.namelist_offs);
    tmap.types_idx = SWAP16(tmap.types_idx);
    RF_HANDLE_INSERT(hand_ref, &tmap, dhdr.map_offs, sizeof(tmap));

    data_offs = name_offs = 0;
    reflist_offs = 0;
    typelist_offs = 0;
    mtyp = rm->type_list;
    while (mtyp) {
        dtyp.type = mtyp->type;
        dtyp.rsrc_idx = GetResourceEntriesCount(mtyp) - 1;
        dtyp.ref_list_offs = typelist_size + reflist_offs + 2;
        ttyp = dtyp;
        ttyp.type = SWAP32(ttyp.type);
        ttyp.rsrc_idx = SWAP16(ttyp.rsrc_idx);
        ttyp.ref_list_offs = SWAP16(ttyp.ref_list_offs);
        RF_HANDLE_INSERT(
            hand_ref, &ttyp, (dhdr.map_offs + dmap.typelist_offs + 2) + typelist_offs, sizeof(ttyp)
        );

        mrle = mtyp->ref_list;
        while (mrle) {
            OS_ASSERT(943, reflist_offs < dmap.namelist_offs);

            drle.id = mrle->id;
            drle.name_offs = (mrle->name) ? name_offs : -1;
            drle.attr = mrle->attr & ~resChanged;
            drle.data_offs[0] = (data_offs >> 16) & 0xFF;
            drle.data_offs[1] = (data_offs >> 8) & 0xFF;
            drle.data_offs[2] = data_offs & 0xFF;
            drle.mem_rsrc_handle = NULL;
            trle = drle;
            trle.id = SWAP16(trle.id);
            trle.name_offs = SWAP16(trle.name_offs);
            RF_HANDLE_INSERT(
                hand_ref,
                &trle,
                (dhdr.map_offs + dmap.typelist_offs + 2) + typelist_size + reflist_offs,
                sizeof(trle)
            );

            reflist_offs += sizeof(trle);
            if (mrle->name) {
                if (dhdr.map_offs < dhdr.data_offs) {
                    OS_ASSERT(963, name_offs + dmap.namelist_offs + dhdr.map_offs < dhdr.data_offs);
                }
                RF_HANDLE_INSERT(
                    hand_ref,
                    mrle->name,
                    dhdr.map_offs + dmap.namelist_offs + drle.name_offs,
                    mrle->name[0] + 1
                );
                name_offs += mrle->name[0] + 1;
            }

            OS_ASSERT(970, data_offs < dhdr.data_len);
            OS_ASSERT(971, mrle->hand!=NULL);
            if (dhdr.map_offs > dhdr.data_offs) {
                OS_ASSERT(973, data_offs + dhdr.data_offs < dhdr.map_offs);
            }

            HLock(mrle->hand);
            dent.len = GetHandleSize(mrle->hand);
            tent = dent;
            tent.len = SWAP32(tent.len);
            RF_HANDLE_INSERT(hand_ref, &tent, dhdr.data_offs + data_offs, sizeof(tent));
            data_offs += sizeof(tent);
            RF_HANDLE_INSERT(hand_ref, *mrle->hand, dhdr.data_offs + data_offs, dent.len);
            data_offs += dent.len;
            HUnlock(mrle->hand);

            mrle = mrle->next;
        }

        typelist_offs += sizeof(ttyp);
        mtyp = mtyp->next;
    }

    sz = GetHandleSize(hand_ref);
    if ((resError = SetFPos(ref, fsFromStart, 0)) != noErr) {
        return;
    }
    if ((resError = SetEOF(ref, sz)) != noErr) {
        return;
    }
    if ((resError = FSWrite(ref, &sz, *hand_ref)) != noErr) {
        return;
    }
    HUnlock(hand_ref);
    DisposeHandle(hand_ref);
}

static void WriteEmptyResourceFork(SInt16 ref) {
    ResourceHeader dhdr;
    MemRsrcSysData dsys;
    ResourceMapHeader dmap;
    Handle hand_ref;
    SInt32 sz;

    dhdr.map_offs = SWAP32(sizeof(dhdr) + sizeof(dsys));
    dhdr.map_len = SWAP32(sizeof(dmap));
    dhdr.data_offs = SWAP32(sizeof(dhdr) + sizeof(dsys) + sizeof(dmap));
    dhdr.data_len = 0;

    memset(&dsys, 0, sizeof(dsys));
    memset(&dsys.creator, 0, sizeof(dsys.creator));
    memset(&dsys.type, 0, sizeof(dsys.type));

    dmap.mem_hdr = dhdr;
    dmap.mem_next_map_handle = NULL;
    dmap.mem_refnum = 0;
    dmap.fork_attr = 0;
    dmap.typelist_offs = SWAP16(sizeof(dmap) - 2);
    dmap.namelist_offs = SWAP16(sizeof(dmap) - 2);
    dmap.types_idx = 0xFFFF;

    hand_ref = NewHandle(sizeof(dhdr) + sizeof(dsys) + sizeof(dmap));
    if (!hand_ref) {
        resError = MemError();
        return;
    }

    HLock(hand_ref);
    memset(*hand_ref, 0, sizeof(dhdr) + sizeof(dsys) + sizeof(dmap));
    RF_HANDLE_INSERT(hand_ref, &dhdr, 0, sizeof(dhdr));
    RF_HANDLE_INSERT(hand_ref, &dsys, sizeof(dhdr), sizeof(dsys));
    RF_HANDLE_INSERT(hand_ref, &dmap, sizeof(dhdr) + sizeof(dsys), sizeof(dmap));

    sz = GetHandleSize(hand_ref);
    if ((resError = SetFPos(ref, fsFromStart, 0)) != noErr) {
        return;
    }
    if ((resError = SetEOF(ref, sz)) != noErr) {
        return;
    }
    if ((resError = FSWrite(ref, &sz, *hand_ref)) != noErr) {
        return;
    }
    HUnlock(hand_ref);
    DisposeHandle(hand_ref);
}

static MemRsrcTypeList* FindResourceType1(MemRsrcMap* rm, OSType theType) {
    MemRsrcTypeList* mtyp;

    mtyp = rm->type_list;
    while (mtyp) {
        if (mtyp->type == theType) {
            return mtyp;
        }
        mtyp = mtyp->next;
    }

    return NULL;
}

static MemRefList* FindResourceTypeAndID1(MemRsrcMap* rm, OSType theType, SInt16 theID) {
    MemRsrcTypeList* mtyp = FindResourceType1(rm, theType);
    MemRefList* mref;

    if (mtyp) {
        mref = mtyp->ref_list;
        while (mref) {
            if (mref->id == theID) {
                return mref;
            }
            mref = mref->next;
        }
    }

    return NULL;
}

static MemRefList* FindResourceTypeAndID(OSType theType, SInt16 theID) {
    MemRsrcMap* rm = FindResourceMap(cur_res_file);
    MemRefList* mref;

    while (rm) {
        mref = FindResourceTypeAndID1(rm, theType, theID);
        if (mref) {
            return mref;
        }
        rm = rm->prev;
    }

    return NULL;
}

void OS_AddMacResourceForkRef(int ref, const OSSpec* spec) {
    ResRef* nw;

    nw = malloc(sizeof(ResRef));
    if (nw) {
        nw->spec = *spec;
        nw->ref = ref;
        nw->next = NULL;
        if (resForkLast) {
            resForkLast->next = nw;
        } else {
            resForkList = nw;
        }
        resForkLast = nw;
    }
}

void OS_RemoveMacResourceForkRef(int ref) {
    ResRef* lst;
    ResRef* prev;

    lst = resForkList;
    prev = NULL;
    while (lst && lst->ref != ref) {
        prev = lst;
        lst = lst->next;
    }

    if (lst) {
        if (prev) {
            prev->next = lst->next;
            if (!prev->next) {
                resForkLast = prev;
            }
        } else {
            resForkList = lst->next;
            if (!resForkList) {
                resForkLast = NULL;
            }
        }
        free(lst);
    }
}

OSSpec* OS_GetMacResourceForkFromRef(int ref) {
    ResRef* lst;

    for (lst = resForkList; lst; lst = lst->next) {
        if (lst->ref == ref) {
            return &lst->spec;
        }
    }

    return NULL;
}

Boolean OS_SearchMacResourceForkList(const OSSpec* rspec, int* ref) {
    ResRef* lst;

    for (lst = resForkList; lst; lst = lst->next) {
        if (OS_EqualSpec(&lst->spec, rspec)) {
            *ref = lst->ref;
            return TRUE;
        }
    }

    return FALSE;
}

int OS_SetMacResourceForkCreatorAndType(int ref, OSType creator, OSType type) {
    int err;
    UInt32 buf[2];
    UInt32 sz;
    MemRsrcMap* rm;

    if (!OS_GetMacResourceForkFromRef(ref)) {
        sz = 8;
        buf[0] = SWAP32(type);
        buf[1] = SWAP32(creator);
        err = OS_Seek(ref, OSSeekAbs, 0x52);
        if (!err) {
            err = OS_Write(ref, buf, &sz);
            if (!err && sz == 8) {
                err = 0;
            }
        }
        return err;
    }

    // (Win32 error codes)
    rm = FindResourceMap(OS_RefToMac(ref));
    if (!rm) {
        return ERROR_FILE_NOT_FOUND;
    }
    if (rm->fork_attr & mapReadOnly) {
        return ERROR_INVALID_ACCESS;
    }
    rm->sys_data.creator[0] = (creator >> 24) & 0xFF;
    rm->sys_data.creator[1] = (creator >> 16) & 0xFF;
    rm->sys_data.creator[2] = (creator >> 8) & 0xFF;
    rm->sys_data.creator[3] = creator & 0xFF;
    rm->sys_data.type[0] = (type >> 24) & 0xFF;
    rm->sys_data.type[1] = (type >> 16) & 0xFF;
    rm->sys_data.type[2] = (type >> 8) & 0xFF;
    rm->sys_data.type[3] = type & 0xFF;
    rm->fork_attr |= mapChanged;
    return 0;
}

int OS_GetMacResourceForkCreatorAndType(int ref, OSType* creator, OSType* type) {
    int err;
    UInt32 buf[2];
    UInt32 sz;
    MemRsrcMap* rm;

    if (!OS_GetMacResourceForkFromRef(ref)) {
        sz = 8;
        err = OS_Seek(ref, OSSeekAbs, 0x52);
        if (!err) {
            err = OS_Read(ref, buf, &sz);
            if (!err && sz == 8) {
                err = 0;
                buf[0] = SWAP32(buf[0]);
                buf[1] = SWAP32(buf[1]);
                *type = buf[0];
                *creator = buf[1];
            }
        }
        return err;
    }

    rm = FindResourceMap(OS_RefToMac(ref));
    if (!rm) {
        return ERROR_FILE_NOT_FOUND;
    }
    *creator = (rm->sys_data.creator[0] << 24) | (rm->sys_data.creator[1] << 16) |
        (rm->sys_data.creator[2] << 8) | rm->sys_data.creator[3];
    *type = (rm->sys_data.type[0] << 24) | (rm->sys_data.type[1] << 16) |
        (rm->sys_data.type[2] << 8) | rm->sys_data.type[3];
    return 0;
}

void OS_CleanupMacResourceForkDir(const OSPathSpec* dir) {
    OS_Rmdir(dir);
}

// (no caller: the linker dropped it, but its literals stay in the unit's .data)
OSErr OS_MacDumpResourceFork(SInt16 ref, Boolean dumpContents) {
    MemRsrcMap* rm;
    MemRsrcTypeList* tl;
    MemRefList* rl;
    char name[256];
    unsigned char* ptr;
    SInt32 idx;
    SInt32 size;
    char safe[16];
    int spaces;

    rm = FindResourceMap(ref);
    if (!rm) {
        fprintf(stderr, "Could not find resource fork for ref = %d\n", ref);
        return fnfErr;
    }

    printf("Fork attributes = %04X\n", rm->fork_attr);
    printf("Creator = '%4.4s';  Type = '%4.4s'\n", rm->sys_data.creator, rm->sys_data.type);
    printf("Types:\n");

    for (tl = rm->type_list; tl; tl = tl->next) {
        printf(
            "'%c%c%c%c':\n",
            (int)((tl->type & 0xFF000000) >> 24),
            (int)((tl->type & 0x00FF0000) >> 16),
            (int)((tl->type & 0x0000FF00) >> 8),
            (int)(tl->type & 0x000000FF)
        );
        for (rl = tl->ref_list; rl; rl = rl->next) {
            if (rl->type != tl->type) {
                printf(
                    "!!! RefList type '%c%c%c%c' does not match TypeList type !!!\n",
                    (int)((rl->type & 0xFF000000) >> 24),
                    (int)((rl->type & 0x00FF0000) >> 16),
                    (int)((rl->type & 0x0000FF00) >> 8),
                    (int)(rl->type & 0x000000FF)
                );
            }

            if (rl->name) {
                p2cstrcpy(name, rl->name);
            } else {
                strcpy(name, "<none>");
            }

            printf("\tID = %d '%s'\n", rl->id, name);
            printf("\tAttributes: ");
            if (rl->attr & resSysHeap) {
                printf("SysHeap ");
            }
            if (rl->attr & resPurgeable) {
                printf("Purgeable ");
            }
            if (rl->attr & resLocked) {
                printf("Locked ");
            }
            if (rl->attr & resProtected) {
                printf("Protected ");
            }
            if (rl->attr & resPreload) {
                printf("Preload ");
            }
            if (rl->attr & resChanged) {
                printf("Changed ");
            }
            printf("\n");

            if (dumpContents) {
                size = GetHandleSize(rl->hand);
                HLock(rl->hand);
                ptr = (unsigned char*)*rl->hand;
                idx = 0;
                printf("Contents:");

                while (idx < size) {
                    if ((idx % 16) == 0) {
                        printf("\n%08X: ", (unsigned int)idx);
                        memset(safe, ' ', 16);
                    }
                    printf("%02X ", ptr[idx]);
                    if (isprint(ptr[idx])) {
                        safe[idx & 15] = ptr[idx];
                    } else {
                        safe[idx & 15] = '.';
                    }

                    if (((idx % 16) == 15) || (idx + 1) == size) {
                        spaces = (((idx & 15) + 15) & ~15) - (idx & 15);
                        while (spaces--) {
                            printf("   ");
                        }
                        printf(" %16.16s", safe);
                    }

                    ++idx;
                }

                printf("\n");
            }
        }
    }

    printf("\n");
    CloseResFile(ref);
    return noErr;
}

OSErr __stdcall FSpOpenRF(const FSSpec* fss, SInt8 permission, SInt16* refNum) {
    OSErr err;
    int oserr;
    FSSpec rfss;
    OSSpec spec, rspec;
    Boolean create;

    create = (permission != fsRdPerm);
    oserr = OS_FSSpec_To_OSSpec(fss, &spec);
    if (oserr) {
        return OS_OSErrorToMacError(oserr);
    }

    oserr = OS_GetRsrcOSSpec(&spec, &rspec, create);
    if (oserr) {
        return OS_OSErrorToMacError(oserr);
    }

    oserr = OS_OSSpec_To_FSSpec(&rspec, &rfss);
    if (oserr) {
        return OS_OSErrorToMacError(oserr);
    }

    if (OS_Status(&rspec) && create) {
        HCreate(rfss.vRefNum, rfss.parID, rfss.name, 'CWIE', 'rsrc');
    }

    err = HOpen(rfss.vRefNum, rfss.parID, rfss.name, permission, refNum);
    if (!err) {
        OS_AddMacResourceForkRef(OS_MacToRef(*refNum), &rspec);
    }

    return err;
}

SInt16 __stdcall FSpOpenResFile(const FSSpec* spec, SInt8 permission) {
    SInt16 ref;
    SInt32 size;

    if (permission != fsRdPerm) {
        FSpCreate(spec, 'CWIE', 'TEXT', -1);
    }

    resError = FSpOpenRF(spec, (permission == fsWrPerm) ? (SInt8)fsRdWrPerm : permission, &ref);
    if (!resError) {
        GetEOF(ref, &size);
        if (size == 0 && permission != fsRdPerm) {
            WriteEmptyResourceFork(ref);
        }
        ReadResourceFork(ref, permission, NULL, 0);
        if (resError) {
            FSClose(ref);
            CloseResFile(ref);
            ref = -1;
        }
        cur_res_file = ref;
    } else {
        ref = -1;
    }

    return ref;
}

Handle GetResource(OSType theType, SInt16 theID) {
    MemRefList* mref;

    mref = FindResourceTypeAndID(theType, theID);
    if (mref) {
        resError = noErr;
        return mref->hand;
    } else {
        resError = ResLoad ? resNotFound : noErr;
        return NULL;
    }
}

void __stdcall ReleaseResource(Handle theResource) {
    resError = noErr;
}

void __stdcall CloseResFile(SInt16 refNum) {
    MemRsrcMap* rm = FindResourceMap(refNum);

    if (rm && refNum != -1) {
        WriteResourceFork(refNum);
        DeleteResources(refNum);
        FSClose(refNum);
    }

    if (maplist) {
        cur_res_file = maplist->refnum;
    } else {
        cur_res_file = 0;
    }
}
