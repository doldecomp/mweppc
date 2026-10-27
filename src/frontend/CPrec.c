#include "compiler/CPrec.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CInline.h"
#include "compiler/CMachine.h"
#include "compiler/CObjC.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CScope.h"
#include "compiler/CSOM.h"
#include "compiler/CTemplateNew.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/InlineAsm.h"
#include "compiler/objc.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/som.h"
#include "compiler/templates.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "driver/CCompiler.h"
#include "driver/COSToolsCLT.h"
#include "driver/CWPlugins.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/DropInCompilerLinker.h"
#include "driver/Files.h"
#include "driver/oslib.h"
#include <stdio.h>
#include <string.h>

#pragma pack(push, 2)
typedef struct StaticData {
    struct StaticData* next;
    Object* object;
    void* buffer;
    OLinkList* links;
    SInt32 size;
} StaticData;

typedef struct Header {
    UInt32 magic;
    UInt16 version;
    UInt16 x6;
    char target;
    Boolean check_header_flags;
    Boolean cplusplus;
    UInt32 xC;
    UInt32 x10;
    UInt32 x14;
    UInt32 x18;
    UInt32 x1C;
    UInt32 x20;
    UInt32 x24;
    UInt32 rawsize;
    UInt32 rawdatasize;
    UInt32 rawdataoffset;
    UInt32 compressedPatchCount;
    UInt32 compressedPatchSize;
    UInt32 compressedPatchOffset;
    UInt32 builtinPatchSize;
    UInt32 builtinPatchOffset;
    UInt32 tokenStreamPatchSize;
    UInt32 tokenStreamPatchOffset;
    UInt32 root_names;
    NameSpaceList* usings;
    TemplClass* ctempl_templates;
    CSOMStub* csom_stubs;
    StaticData* cprec_staticdata;
    UInt32 uniqueID;
    CallbackAction* callbackactions;
    Type* cobjc_type_class;
    Type* cobjc_type_id;
    Type* cobjc_type_sel;
    struct ObjCSelector** cobjc_selhashtable;
    struct BClassList* cobjc_classdefs;
    ObjCProtocol* cobjc_protocols;
    UInt32 cobjc_selrefcount;
    UInt32 cobjc_classrefcount;
    UInt32 cobjc_stringcount;
    InitExpr* init_expressions;
    CI_Action* cinline_tactionlist;
    TemplateFunction* ctempl_templatefuncs;
    UInt32 x9C;
    UInt32 xA0;
    UInt32 xA4;
    UInt32 xA8;
    UInt32 xAC;
    UInt32 xB0;
    UInt32 xB4;
    UInt32 xB8;
    UInt32 xBC;
    UInt32 xC0;
    UInt32 xC4;
    UInt32 xC8;
    UInt32 xCC;
    UInt32 xD0;
    UInt32 xD4;
    UInt32 xD8;
    UInt32 xDC;
    UInt32 xE0;
    UInt32 xE4;
    HashNameNode* nametable[0x800];
    Macro* macrotable[0x800];
    NameSpaceName* root_nametable[0x400];
} Header;

typedef struct Patch {
    struct Patch* next;
    SInt32 offset;
} Patch;

typedef struct AddrPatch {
    struct AddrPatch* next;
    void* addr;
    void* value;
} AddrPatch;

typedef struct BuiltIn {
    void* target;
    SInt32 idx;
    Patch* patches;
} BuiltIn;

typedef struct TokenPatch {
    struct TokenPatch* next;
    TStreamElement* tokens;
    SInt32 count;
} TokenPatch;

typedef struct PointerHash {
    struct PointerHash* next;
    TypePointer* tptr;
    TypePointer* prec_tptr;
} PointerHash;

static short cprec_refnum;
static GList cprec_glist;
static Header* cprec_header;
static AddrPatch** cprec_addrhash;
static Patch* cprec_patch_list;
static BuiltIn* cprec_builtin;
static PointerHash** cprec_pointerhash;
static StaticData* cprec_staticdata;
static TokenPatch* cprec_tokenpatches;
static void** cprec_builtin_array;
static int cprec_builtins;
static SInt32 cprec_offset;
static SInt32 cprec_zero_offset;
static void* cprec_buffer;
static void* cprec_rawbuffer;
static OSErr cprec_ioerror;
static Boolean cprec_dowrite;
static Boolean cprec_exportargnames;
static SInt32 flushmax;
char* precomp_target_str;
#pragma pack(pop)

struct Macro** macrohashtable;

static NameSpace* CPrec_GetNameSpacePatch(NameSpace* nspace);
static ObjEnumConst* CPrec_GetObjEnumConstPatch(ObjEnumConst* obj);
static Object* CPrec_GetObjectPatch(Object* obj);
static ObjBase* CPrec_GetObjBasePatch(ObjBase* obj);
static ObjMemberVar* CPrec_GetObjMemberVarPatch(ObjMemberVar* ivar);
static Type* CPrec_GetTypePatch(Type* type);
static ENode* CPrec_GetExpressionPatch(ENode* expr);
static ObjCMethod* CPrec_GetObjCMethodPatch(ObjCMethod* meth);
static ObjCProtocolList* CPrec_GetObjCProtocolListPatch(ObjCProtocolList* lst);
static TemplArg* CPrec_GetTemplateArgPatch(TemplArg* arg);
static NameSpaceObjectList* CPrec_GetNameSpaceObjectListPatch(NameSpaceObjectList* nsol);
static OSErr CPrec_FlushBufferCheck(void);

void SetupPrecompiler(Boolean isPrecompiling) {
    cprec_refnum = 0;
    cprec_glist.data = NULL;
    cprec_header = NULL;
    cprec_staticdata = NULL;
    cprec_ioerror = noErr;
}

void CleanupPrecompiler(void) {
    if (cprec_refnum) {
        COS_FileClose(cprec_refnum);
        cprec_refnum = 0;
    }

    if (cprec_glist.data) {
        FreeGList(&cprec_glist);
    }
}

static OLinkList* CPrec_OLinkListCopy(OLinkList* list) {
    OLinkList* copy;

    if (!list) {
        return NULL;
    }

    copy = galloc(sizeof(OLinkList));
    *copy = *list;
    copy->next = CPrec_OLinkListCopy(copy->next);
    return copy;
}

void PreComp_StaticData(Object* obj, const void* data, OLinkList* links, SInt32 size) {
    StaticData* entry;

    if (obj->sclass != TK_STATIC && !(obj->qual & (Q_IMPLICIT_WEAK | Q_WEAK))) {
        CError_Error(ERR_ILLEGAL_USE_PRECOMPILED_HEADER);
    }

    entry = galloc(sizeof(StaticData));
    entry->object = obj;
    entry->size = size;

    entry->next = cprec_staticdata;
    cprec_staticdata = entry;

    if (data) {
        entry->buffer = galloc(obj->type->size);
        memcpy(entry->buffer, data, obj->type->size);
    } else {
        entry->buffer = NULL;
    }

    entry->links = CPrec_OLinkListCopy(links);
}

static void CPrec_InitAddressHashTable(void) {
    cprec_addrhash = lalloc(0x4000 * sizeof(AddrPatch*));
    memclrw(cprec_addrhash, 0x4000 * sizeof(AddrPatch*));
}

static void CPrec_InitPointerHashTable(void) {
    cprec_pointerhash = lalloc(0x400 * sizeof(PointerHash*));
    memclrw(cprec_pointerhash, 0x400 * sizeof(PointerHash*));
}

static int CPrec_AddressHashVal(void* addr) {
    union {
        SInt32 v;
        UInt8 b[4];
    } u;
    u.v = (SInt32)addr;
    return ((UInt32)addr + u.b[0] + u.b[1] + u.b[2] + u.b[3]) & 0x3FFF;
}

static AddrPatch* CPrec_FindAddrPatch(void* addr) {
    AddrPatch* scan;

    for (scan = cprec_addrhash[CPrec_AddressHashVal(addr)]; scan; scan = scan->next) {
        if (scan->addr == addr) {
            return scan;
        }
    }

    return NULL;
}

static AddrPatch* CPrec_NewAddrPatch(void* addr, void* value) {
    AddrPatch** loc;
    AddrPatch* patch;

    loc = cprec_addrhash + CPrec_AddressHashVal(addr);
    patch = lalloc(sizeof(AddrPatch));
    patch->addr = addr;
    patch->value = value;
    patch->next = *loc;
    *loc = patch;
    return patch;
}

static void CPrec_SetupBuiltInArray(void) {
    int filled;
    int total;
    Boolean counting;
    void** array;

#define REG_BUILTIN(a) \
    if (!counting) { \
        array[filled++] = (a); \
    } else { \
        total++; \
    }

    for (filled = total = 0, counting = TRUE;;) {
        REG_BUILTIN(cscope_root);
        REG_BUILTIN(&stvoid);
        REG_BUILTIN(&stbool);
        REG_BUILTIN(&stchar);
        REG_BUILTIN(&stsignedchar);
        REG_BUILTIN(&stunsignedchar);
        REG_BUILTIN(&stwchar);
        REG_BUILTIN(&stsignedshort);
        REG_BUILTIN(&stunsignedshort);
        REG_BUILTIN(&stsignedint);
        REG_BUILTIN(&stunsignedint);
        REG_BUILTIN(&stsignedlong);
        REG_BUILTIN(&stunsignedlong);
        REG_BUILTIN(&stsignedlonglong);
        REG_BUILTIN(&stunsignedlonglong);
        REG_BUILTIN(&stfloat);
        REG_BUILTIN(&stshortdouble);
        REG_BUILTIN(&stdouble);
        REG_BUILTIN(&stlongdouble);
        REG_BUILTIN(&elipsis);
        REG_BUILTIN(&oldstyle);
        REG_BUILTIN(&stillegal);
        REG_BUILTIN(&sttemplexpr);
        REG_BUILTIN(&stvoid);
        REG_BUILTIN(&void_ptr);
        REG_BUILTIN(&rt_func);
        REG_BUILTIN(&catchinfostruct);
        REG_BUILTIN(newh_func);
        REG_BUILTIN(delh_func);
        REG_BUILTIN(copy_func);
        REG_BUILTIN(clear_func);
        REG_BUILTIN(Rgtid_func);
        REG_BUILTIN(Rdync_func);
        REG_BUILTIN(rt_ptmf_cast);
        REG_BUILTIN(rt_ptmf_cmpr);
        REG_BUILTIN(rt_ptmf_test);
        REG_BUILTIN(rt_ptmf_call);
        REG_BUILTIN(rt_ptmf_scall);
        REG_BUILTIN(rt_ptmf_null);
        REG_BUILTIN(rt_som_glue1);
        REG_BUILTIN(rt_som_glue2);
        REG_BUILTIN(rt_som_glue3);
        REG_BUILTIN(rt_som_check);
        REG_BUILTIN(rt_som_new);
        REG_BUILTIN(rt_som_newcheck);
        REG_BUILTIN(rt_ptmf_call4);
        REG_BUILTIN(rt_ptmf_scall4);
        REG_BUILTIN(carr_func);
        REG_BUILTIN(cnar_func);
        REG_BUILTIN(darr_func);
        REG_BUILTIN(dnar_func);
        REG_BUILTIN(dnar3_func);
        REG_BUILTIN(Xgreg_func);
        REG_BUILTIN(Xthrw_func);
        REG_BUILTIN(Xicth_func);
        REG_BUILTIN(Xecth_func);
        REG_BUILTIN(Xunex_func);
        REG_BUILTIN(&stvectorunsignedchar);
        REG_BUILTIN(&stvectorsignedchar);
        REG_BUILTIN(&stvectorboolchar);
        REG_BUILTIN(&stvectorunsignedshort);
        REG_BUILTIN(&stvectorsignedshort);
        REG_BUILTIN(&stvectorboolshort);
        REG_BUILTIN(&stvectorunsignedlong);
        REG_BUILTIN(&stvectorsignedlong);
        REG_BUILTIN(&stvectorboollong);
        REG_BUILTIN(&stvectorfloat);
        REG_BUILTIN(&stvectorpixel);
        REG_BUILTIN(&stvec2x32float);

        if (counting) {
            array = lalloc(sizeof(void*) * total);
            cprec_builtin_array = array;
            cprec_builtins = total;
            counting = FALSE;
        } else {
            return;
        }
    }
}

static void CPrec_SetupBuiltIn(void) {
    int x;

    CPrec_SetupBuiltInArray();
    cprec_builtin = lalloc(sizeof(BuiltIn) * cprec_builtins);
    memclrw(cprec_builtin, sizeof(BuiltIn) * cprec_builtins);

    for (x = 0; x < cprec_builtins; x++) {
        cprec_builtin[x].target = cprec_builtin_array[x];
        cprec_builtin[x].idx = ~x;
        CPrec_NewAddrPatch(cprec_builtin[x].target, (void*)cprec_builtin[x].idx);
    }
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static void CPrec_NewPointerPatch(void* src, void* ptr) {
    if (cprec_dowrite) {
        Patch* patch = lalloc(sizeof(Patch));
        patch->offset = (SInt32)src;
        CError_ASSERT(523, (patch->offset & 0x80000001) == 0);

        if ((SInt32)ptr < 0) {
            ptr = (void*)~((SInt32)ptr);
            CError_ASSERT(529, (SInt32)ptr < cprec_builtins);

            patch->next = cprec_builtin[(int)ptr].patches;
            cprec_builtin[(int)ptr].patches = patch;
            ptr = NULL;
        } else {
            patch->next = cprec_patch_list;
            cprec_patch_list = patch;
        }

        src = (void*)((char*)src - cprec_zero_offset);
        CError_ASSERT(541, (SInt32)src >= 0 && (SInt32)src <= cprec_glist.size);
        *((void**)(*cprec_glist.data + (SInt32)src)) = ptr;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static void CPrec_ExistingPointerPatch(void* src, void* ptr) {
    SInt32 key = (SInt32)ptr;
    if (cprec_dowrite) {
        AddrPatch* addrPatch;
        Patch* patch;

        CError_ASSERT(559, addrPatch = CPrec_FindAddrPatch((void*)key));

        patch = lalloc(sizeof(Patch));
        patch->offset = (SInt32)src;
        patch->next = cprec_patch_list;
        cprec_patch_list = patch;

        CError_ASSERT(564, (patch->offset & 0x80000001) == 0);

        src = (void*)((char*)src - cprec_zero_offset);
        CError_ASSERT(568, (SInt32)src >= 0 && (SInt32)src <= cprec_glist.size);
        *((void**)(*cprec_glist.data + (SInt32)src)) = addrPatch->value;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static void CPrec_NamePatch(void* src, HashNameNode* name) {
    name->id = 1;
    CPrec_ExistingPointerPatch(src, name);
}

static SInt32 CPrec_AppendAlign(void) {
    if (cprec_dowrite) {
        while (cprec_offset & 3) {
            AppendGListByte(&cprec_glist, 0);
            ++cprec_offset;
        }
    }

    return cprec_offset;
}

static UInt32 CPrec_AppendByte(UInt8 v) {
    if (cprec_dowrite) {
        AppendGListByte(&cprec_glist, v);
    }
    return cprec_offset++;
}

static UInt32 CPrec_AppendWord16(UInt16 v) {
    UInt32 offset;
    if (cprec_dowrite) {
        AppendGListWord(&cprec_glist, v);
    }
    offset = cprec_offset;
    cprec_offset += 2;
    return offset;
}

static UInt32 CPrec_AppendWord32(UInt32 v) {
    UInt32 offset;
    if (cprec_dowrite) {
        AppendGListLong(&cprec_glist, v);
    }
    offset = cprec_offset;
    cprec_offset += 4;
    return offset;
}

static UInt32 CPrec_AppendPointer(void* v) {
    UInt32 offset;
    if (cprec_dowrite) {
        AppendGListLong(&cprec_glist, (SInt32)v);
    }
    offset = cprec_offset;
    cprec_offset += 4;
    return offset;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static UInt32 CPrec_AppendPointerPatch(void* v) {
    AddrPatch* addrPatch;

    if (v) {
        CError_ASSERT(660, addrPatch = CPrec_FindAddrPatch(v));

        if (cprec_dowrite) {
            Patch* patch = lalloc(sizeof(Patch));
            patch->offset = cprec_offset;
            patch->next = cprec_patch_list;
            cprec_patch_list = patch;
            CError_ASSERT(667, (patch->offset & 0x80000001) == 0);
        }

        return CPrec_AppendPointer(addrPatch->value);
    } else {
        return CPrec_AppendPointer(NULL);
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static void CPrec_AppendNamePatch(HashNameNode* name) {
    if (name) {
        CPrec_AppendPointerPatch(name);
        name->id = 1;
    }
}

static void CPrec_AppendString(const char* str) {
    int len = strlen(str) + 1;
    if (cprec_dowrite) {
        AppendGListData(&cprec_glist, str, len);
    }
    cprec_offset += len;
}

static void CPrec_AppendData(const void* data, int len) {
    if (cprec_dowrite) {
        AppendGListData(&cprec_glist, data, len);
    }
    cprec_offset += len;
}

static void CPrec_RawMemPatch(void* source, const void* data, int len) {
    SInt32 ptr = CPrec_AppendAlign();
    CPrec_AppendData(data, len);
    CPrec_NewPointerPatch(source, (void*)ptr);
}

static void CPrec_DumpNameTable(void) {
    HashNameNode* name;
    int i;
    HashNameNode* p;
    SInt32 next;

    if (cprec_dowrite) {
        i = 0;
        do {
            name = name_hash_nodes[i];
            while (name && name->id == 0) {
                name = name->next;
            }

            if (name) {
                p = (void*)CPrec_AppendAlign();
                cprec_header->nametable[i] = p;

                while (1) {
                    CPrec_NewAddrPatch(name, p);
                    CPrec_AppendPointer(NULL);
                    CPrec_AppendWord32(0);
                    CPrec_AppendWord16(name->hashval);
                    CPrec_AppendString(name->name);

                    name = name->next;
                    while (name && name->id == 0) {
                        name = name->next;
                    }

                    if (!name) {
                        break;
                    }

                    CPrec_AppendAlign();
                    next = cprec_offset;
                    CPrec_NewPointerPatch(&p->next, (void*)next);
                    p = (HashNameNode*)next;
                }
            }
        } while (++i < 0x800);
    } else {
        i = 0;
        do {
            if ((name = name_hash_nodes[i])) {
                p = (void*)CPrec_AppendAlign();
                while (1) {
                    CPrec_NewAddrPatch(name, p);
                    CPrec_AppendPointer(NULL);
                    CPrec_AppendWord32(0);
                    CPrec_AppendWord16(name->hashval);
                    CPrec_AppendString(name->name);

                    name = name->next;
                    if (!name) {
                        break;
                    }

                    CPrec_AppendAlign();
                    next = cprec_offset;
                    CPrec_NewPointerPatch(&p->next, (void*)next);
                    p = (HashNameNode*)next;
                }
            }
        } while (++i < 0x800);
    }
}

static void CPrec_DumpMacroTable(void) {
    Macro* macro;
    SInt32 j;
    SInt32 i;
    SInt32 next;
    SInt32 p;

    i = 0;
    do {
        for (macro = macrohashtable[i]; macro; macro = macro->next) {
            if (macro->text) {
                CPrec_NewAddrPatch(macro->text, (void*)cprec_offset);
                CPrec_AppendString(macro->text);
            }
        }
    } while (++i < 0x800);

    i = 0;
    do {
        if (!(macro = macrohashtable[i])) {
            continue;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        p = next;
        if (cprec_dowrite) {
            cprec_header->macrotable[i] = (Macro*)next;
        }
        for (;;) {
            CPrec_AppendPointer(NULL);
            CPrec_AppendNamePatch(macro->name);
            CPrec_AppendPointerPatch(macro->text);
            CPrec_AppendWord16(macro->nargs);
            CPrec_AppendByte(macro->is_special);
            CPrec_AppendByte(macro->isExpanding);

            for (j = 1; j < (macro->nargs & 0x7FFF); j++) {
                CPrec_AppendNamePatch(macro->names[j - 1]);
            }

            macro = macro->next;
            if (!macro) {
                break;
            }
            CPrec_AppendAlign();
            next = cprec_offset;
            CPrec_NewPointerPatch((void*)p, (void*)next);
            p = next;
        }
    } while (++i < 0x800);
}

static BClassList* CPrec_GetClassAccessPatch(BClassList* path) {
    AddrPatch* addrPatch;
    BClassList* first;
    BClassList* current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(path))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(path, first = current = (void*)CPrec_AppendAlign());

    while (1) {
        CPrec_AppendData(path, sizeof(BClassList));
        CPrec_NewPointerPatch(&current->type, CPrec_GetTypePatch(path->type));
        if (!path->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (BClassList*)next;
        path = path->next;
    }

    return first;
}

static int CPrec_PointerHash(TypePointer* type) {
    Type* target;
    int work;
    FuncArg* arg;

    work = type->qual;
    target = type->target;

restart:
    switch (target->type) {
        case TYPECLASS:
            if (TYPE_CLASS(target)->classname) {
                work += TYPE_CLASS(target)->classname->hashval;
            }
            break;
        case TYPEENUM:
            if (TYPE_ENUM(target)->enumname) {
                work += TYPE_ENUM(target)->enumname->hashval;
            }
            work += 3;
            target = TYPE_ENUM(target)->enumtype;
        case TYPEINT:
        case TYPEFLOAT:
            work += TYPE_INTEGRAL(target)->integral;
            break;
        case TYPEPOINTER:
            work += TYPE_POINTER(target)->qual;
            target = TYPE_POINTER(target)->target;
            goto restart;
        case TYPEARRAY:
            work += target->size;
            target = TYPE_POINTER(target)->target;
            goto restart;
        case TYPEFUNC:
            work += TYPE_FUNC(target)->functype->type;
            work += TYPE_FUNC(target)->functype->size;
            for (arg = TYPE_FUNC(target)->args; arg; arg = arg->next) {
                if (arg->type) {
                    work += arg->type->type;
                    work += arg->type->size;
                }
            }
            break;
        default:
            break;
    }

    work += target->type + target->size;
    return (work + (work >> 24) + (work >> 16) + (work >> 8)) & 0x3FF;
}

static TypePointer* CPrec_GetTypePointerPatch(TypePointer* tptr) {
    SInt32 p;
    int hash;
    PointerHash* phash;

    if (tptr->qual & Q_IS_OBJC_ID) {
        CPrec_NewAddrPatch(tptr, (void*)(p = CPrec_AppendAlign()));
        CPrec_AppendData(tptr, sizeof(TypeObjCID));
        if (TYPE_OBJC_ID(tptr)->protocols) {
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeObjCID, protocols)),
                CPrec_GetObjCProtocolListPatch(TYPE_OBJC_ID(tptr)->protocols)
            );
        }
    } else {
        if (!copts.faster_pch_gen && cprec_dowrite && tptr->size > 0) {
            hash = CPrec_PointerHash(tptr);
            for (phash = cprec_pointerhash[hash]; phash; phash = phash->next) {
                TypePointer* scan1 = tptr;
                TypePointer* scan2 = phash->tptr;
                do {
                    if (scan1->type != scan2->type ||
                        scan1->size != scan2->size ||
                        scan1->qual != scan2->qual) {
                        break;
                    }
                    scan1 = TYPE_POINTER(TPTR_TARGET(scan1));
                    scan2 = TYPE_POINTER(TPTR_TARGET(scan2));
                    if (scan1->type != TYPEPOINTER || scan1->type != TYPEARRAY) {
                        if (scan1 == scan2) {
                            return phash->prec_tptr;
                        }
                        break;
                    }
                } while (1);
            }
            CPrec_NewAddrPatch(tptr, (void*)(p = CPrec_AppendAlign()));
            phash = lalloc(sizeof(PointerHash));
            phash->tptr = tptr;
            phash->prec_tptr = (TypePointer*)p;
            phash->next = cprec_pointerhash[hash];
            cprec_pointerhash[hash] = phash;
        } else {
            CPrec_NewAddrPatch(tptr, (void*)(p = CPrec_AppendAlign()));
        }
        CPrec_AppendData(tptr, sizeof(TypePointer));
    }
    CPrec_NewPointerPatch(
        (void*)(p + offsetof(TypePointer, target)), CPrec_GetTypePatch(tptr->target)
    );
    return (TypePointer*)p;
}

static TypeEnum* CPrec_GetTypeEnumPatch(TypeEnum* type) {
    SInt32 p;

    CPrec_NewAddrPatch(type, (void*)(p = CPrec_AppendAlign()));
    CPrec_AppendData(type, sizeof(TypeEnum));

    if (type->nspace) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(TypeEnum, nspace)), CPrec_GetNameSpacePatch(type->nspace)
        );
    }
    if (type->enumlist) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(TypeEnum, enumlist)), CPrec_GetObjEnumConstPatch(type->enumlist)
        );
    }

    CPrec_NewPointerPatch(
        (void*)(p + offsetof(TypeEnum, enumtype)), CPrec_GetTypePatch(type->enumtype)
    );
    if (type->enumname) {
        CPrec_NamePatch((void*)(p + offsetof(TypeEnum, enumname)), type->enumname);
    }

    return (TypeEnum*)p;
}

static TypeBitfield* CPrec_GetTypeBitfieldPatch(TypeBitfield* type) {
    SInt32 p;

    CPrec_NewAddrPatch(type, (void*)(p = CPrec_AppendAlign()));
    if (cprec_dowrite) {
        AppendGListData(&cprec_glist, type, sizeof(TypeBitfield));
    }
    cprec_offset += sizeof(TypeBitfield);
    CPrec_NewPointerPatch(
        (void*)(p + offsetof(TypeBitfield, bitfieldtype)), CPrec_GetTypePatch(type->bitfieldtype)
    );

    return (TypeBitfield*)p;
}

static TypeStruct* CPrec_GetTypeStructPatch(TypeStruct* tstruct) {
    StructMember* member;
    TypeStruct* p;
    SInt32 next;
    StructMember* current;

    CPrec_NewAddrPatch(tstruct, current = (void*)(p = (void*)CPrec_AppendAlign()));
    CPrec_AppendData(tstruct, sizeof(TypeStruct));

    if (tstruct->name) {
        CPrec_NamePatch(&p->name, tstruct->name);
    }

    if ((member = tstruct->members)) {
        current = (void*)CPrec_AppendAlign();
        CPrec_NewPointerPatch(&p->members, current);

        while (1) {
            CPrec_AppendData(member, sizeof(StructMember));
            CPrec_NewPointerPatch(&current->type, CPrec_GetTypePatch(member->type));
            CPrec_NamePatch(&current->name, member->name);

            if (!member->next) {
                break;
            }

            CPrec_AppendAlign();
            next = cprec_offset;
            CPrec_NewPointerPatch(&current->next, (void*)next);
            current = (StructMember*)next;

            member = member->next;
        }
    }

    return p;
}

static ExceptSpecList* CPrec_GetExceptSpecPatch(ExceptSpecList* exspec) {
    ExceptSpecList* first;
    ExceptSpecList* current;
    SInt32 next;

    first = current = (void*)CPrec_AppendAlign();
    while (exspec) {
        CPrec_AppendData(exspec, sizeof(ExceptSpecList));
        if (exspec->type) {
            CPrec_NewPointerPatch(&current->type, CPrec_GetTypePatch(exspec->type));
        }

        if (!exspec->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (ExceptSpecList*)next;
        exspec = exspec->next;
    }

    return first;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static FuncArg* CPrec_GetArgListPatch(FuncArg* lst, Boolean includeNames) {
    SInt32 first;
    SInt32 current;
    SInt32 next;
    AddrPatch* addrPatch;

    if ((addrPatch = CPrec_FindAddrPatch(lst))) {
        return addrPatch->value;
    }

    first = current = CPrec_AppendAlign();
    while (1) {
        if (!includeNames) {
            lst->name = NULL;
        }
        CPrec_AppendData(lst, sizeof(FuncArg));
        if (includeNames && lst->name) {
            CPrec_NamePatch((void*)(current + offsetof(FuncArg, name)), lst->name);
        }

        if (lst->dexpr) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(FuncArg, dexpr)), CPrec_GetExpressionPatch(lst->dexpr)
            );
        }
        if (lst->type) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(FuncArg, type)), CPrec_GetTypePatch(lst->type)
            );
        } else {
            CError_FATAL(1183);
        }

        if (!lst->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(lst->next))) {
            CPrec_NewPointerPatch((void*)(current + offsetof(FuncArg, next)), addrPatch->value);
            break;
        }

        next = CPrec_AppendAlign();
        CPrec_NewPointerPatch((void*)(current + offsetof(FuncArg, next)), (void*)next);
        current = next;
        lst = lst->next;
        CPrec_NewAddrPatch(lst, (void*)current);
    }

    return (FuncArg*)first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static TypeFunc* CPrec_GetTypeFuncPatch(TypeFunc* type) {
    SInt32 p;

    CPrec_NewAddrPatch(type, (void*)(p = CPrec_AppendAlign()));
    CPrec_AppendData(type, (type->flags & FUNC_METHOD) ? sizeof(TypeMemberFunc) : sizeof(TypeFunc));

    CPrec_NewPointerPatch(
        (void*)(p + offsetof(TypeFunc, functype)), CPrec_GetTypePatch(type->functype)
    );
    if (type->args) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(TypeFunc, args)),
            CPrec_GetArgListPatch(type->args, (type->flags & FUNC_IS_TEMPL_ANY) != 0)
        );
    }
    if (type->exspecs) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(TypeFunc, exspecs)), CPrec_GetExceptSpecPatch(type->exspecs)
        );
    }
    if (type->flags & FUNC_METHOD) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(TypeMemberFunc, theclass)),
            CPrec_GetTypePatch((Type*)TYPE_METHOD(type)->theclass)
        );
    }

    return (TypeFunc*)p;
}

static TypeMemberPointer* CPrec_GetTypeMemberPointerPatch(TypeMemberPointer* type) {
    SInt32 p;

    CPrec_NewAddrPatch(type, (void*)(p = CPrec_AppendAlign()));
    CPrec_AppendData(type, sizeof(TypeMemberPointer));

    CPrec_NewPointerPatch(
        (void*)(p + offsetof(TypeMemberPointer, ty1)), CPrec_GetTypePatch(type->ty1)
    );
    CPrec_NewPointerPatch(
        (void*)(p + offsetof(TypeMemberPointer, ty2)), CPrec_GetTypePatch(type->ty2)
    );

    return (TypeMemberPointer*)p;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static TypeTemplDep* CPrec_GetTypeTemplDepPatch(TypeTemplDep* type) {
    SInt32 p;

    CPrec_NewAddrPatch(type, (void*)(p = CPrec_AppendAlign()));
    CPrec_AppendData(type, sizeof(TypeTemplDep));

    switch (type->dtype) {
        case TEMPLDEP_ARGUMENT:
            break;
        case TEMPLDEP_QUALNAME:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.qual.type)),
                CPrec_GetTypeTemplDepPatch(type->u.qual.type)
            );
            CPrec_NamePatch((void*)(p + offsetof(TypeTemplDep, u.qual.name)), type->u.qual.name);
            break;
        case TEMPLDEP_TEMPLATE:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.templ.templ)),
                CPrec_GetTypePatch((Type*)type->u.templ.templ)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.templ.args)),
                CPrec_GetTemplateArgPatch(type->u.templ.args)
            );
            break;
        case TEMPLDEP_ARRAY:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.array.type)),
                CPrec_GetTypePatch(type->u.array.type)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.array.index)),
                CPrec_GetExpressionPatch(type->u.array.index)
            );
            break;
        case TEMPLDEP_QUALTEMPL:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.qualtempl.type)),
                CPrec_GetTypeTemplDepPatch(type->u.qualtempl.type)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.qualtempl.args)),
                CPrec_GetTemplateArgPatch(type->u.qualtempl.args)
            );
            break;
        case TEMPLDEP_BITFIELD:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.bitfield.type)),
                CPrec_GetTypePatch(type->u.bitfield.type)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.bitfield.size)),
                CPrec_GetExpressionPatch(type->u.bitfield.size)
            );
            break;
        default:
            CError_FATAL(1311);
    }

    return (TypeTemplDep*)p;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static ClassList* CPrec_GetClassListPatch(ClassList* cl) {
    AddrPatch* addrPatch;
    ClassList* first;
    ClassList* current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(cl))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(cl, first = current = (void*)CPrec_AppendAlign());

    do {
        CPrec_AppendData(cl, sizeof(ClassList));
        CPrec_NewPointerPatch(&current->base, CPrec_GetTypePatch((Type*)cl->base));

        if (!cl->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(cl->next))) {
            CPrec_NewPointerPatch(&current->next, addrPatch->value);
            break;
        } else {
            CPrec_AppendAlign();
            next = cprec_offset;
            CPrec_NewPointerPatch(&current->next, (void*)next);
            current = (ClassList*)next;
            cl = cl->next;
        }
    } while (1);

    return first;
}

static VClassList* CPrec_GetVClassListPatch(VClassList* vcl) {
    VClassList* first;
    SInt32 next;
    VClassList* current;

    first = current = (void*)CPrec_AppendAlign();
    for (;;) {
        CPrec_AppendData(vcl, sizeof(VClassList));
        CPrec_NewPointerPatch(&current->base, CPrec_GetTypePatch((Type*)vcl->base));
        if (!vcl->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (VClassList*)next;
        vcl = vcl->next;
    }
    return first;
}

static ClassFriend* CPrec_GetClassFriendPatch(ClassFriend* cf) {
    ClassFriend* first;
    ClassFriend* current;
    SInt32 next;
    first = current = (void*)CPrec_AppendAlign();

    do {
        CPrec_AppendData(cf, sizeof(ClassFriend));
        if (cf->isclass) {
            CPrec_NewPointerPatch(&current->u.theclass, CPrec_GetTypePatch(TYPE(cf->u.theclass)));
        } else {
            CPrec_NewPointerPatch(&current->u.obj, CPrec_GetObjectPatch(cf->u.obj));
        }

        if (!cf->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (ClassFriend*)next;
        cf = cf->next;
    } while (1);

    return first;
}

static BClassList* CPrec_GetBClassListPatch(BClassList* bcl) {
    BClassList* first;
    SInt32 next;
    BClassList* current;
    first = current = (void*)CPrec_AppendAlign();

    for (;;) {
        CPrec_AppendData(bcl, sizeof(BClassList));
        CPrec_NewPointerPatch(&current->type, CPrec_GetTypePatch((Type*)bcl->type));

        if (!bcl->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (BClassList*)next;
        bcl = bcl->next;
    }

    return first;
}

static VTable* CPrec_GetVTablePatch(VTable* vtbl) {
    VTable* p = (void*)CPrec_AppendAlign();
    CPrec_AppendData(vtbl, sizeof(VTable));

    if (vtbl->object) {
        CPrec_NewPointerPatch(&p->object, CPrec_GetObjectPatch(vtbl->object));
    }
    if (vtbl->owner) {
        CPrec_NewPointerPatch(&p->owner, CPrec_GetTypePatch((Type*)vtbl->owner));
    }

    return p;
}

static SOMReleaseOrder* CPrec_GetSOMReleaseOrderPatch(SOMReleaseOrder* sro) {
    SInt32 first;
    SInt32 current;
    SInt32 next;
    first = current = CPrec_AppendAlign();

    for (;;) {
        CPrec_AppendData(sro, sizeof(SOMReleaseOrder));
        CPrec_NamePatch((void*)(current + offsetof(SOMReleaseOrder, name)), sro->name);
        if (!sro->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(SOMReleaseOrder, next)), (void*)next);
        current = next;
        sro = sro->next;
    }

    return (SOMReleaseOrder*)first;
}

static SOMInfo* CPrec_GetSOMInfoPatch(SOMInfo* som) {
    SOMInfo* p = (void*)CPrec_AppendAlign();
    CPrec_AppendData(som, sizeof(SOMInfo));

    if (som->metaclass) {
        CPrec_NewPointerPatch(&p->metaclass, CPrec_GetTypePatch((Type*)som->metaclass));
    }
    if (som->classdataobject) {
        CPrec_NewPointerPatch(&p->classdataobject, CPrec_GetObjectPatch(som->classdataobject));
    }
    if (som->order) {
        CPrec_NewPointerPatch(&p->order, CPrec_GetSOMReleaseOrderPatch(som->order));
    }

    return p;
}

static ObjCMethodArg* CPrec_GetObjCMethodArgPatch(ObjCMethodArg* arg) {
    ObjCMethodArg* first;
    ObjCMethodArg* current;
    SInt32 next;
    first = current = (void*)CPrec_AppendAlign();

    do {
        CPrec_AppendData(arg, sizeof(ObjCMethod));
        if (arg->selector) {
            CPrec_NamePatch(&current->selector, arg->selector);
        }
        if (arg->name) {
            CPrec_NamePatch(&current->name, arg->name);
        }
        if (arg->type) {
            CPrec_NewPointerPatch(&current->type, CPrec_GetTypePatch(arg->type));
        }

        if (!arg->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (ObjCMethodArg*)next;
        arg = arg->next;
    } while (1);

    return first;
}

static ObjCMethodList* CPrec_GetObjCMethodListPatch(ObjCMethodList* lst) {
    AddrPatch* addrPatch;
    SInt32 first;
    SInt32 current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(lst))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(lst, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(lst, sizeof(ObjCMethodList));
        if (lst->method) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCMethodList, method)),
                CPrec_GetObjCMethodPatch(lst->method)
            );
        }

        if (!lst->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(lst->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCMethodList, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(ObjCMethodList, next)), (void*)next);
            current = next;
            lst = lst->next;
            CPrec_NewAddrPatch(lst, (void*)current);
        }
    } while (1);

    return (ObjCMethodList*)first;
}

static ObjCSelector* CPrec_GetObjCSelectorPatch(ObjCSelector* sel) {
    AddrPatch* addrPatch;
    SInt32 current;
    SInt32 first;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(sel))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(sel, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(sel, sizeof(ObjCSelector));
        if (sel->selobject) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCSelector, selobject)),
                CPrec_GetObjectPatch(sel->selobject)
            );
        }
        CPrec_NamePatch((void*)(current + offsetof(ObjCSelector, name)), sel->name);
        if (sel->methods) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCSelector, methods)),
                CPrec_GetObjCMethodListPatch(sel->methods)
            );
        }

        if (!sel->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(sel->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCSelector, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(ObjCSelector, next)), (void*)next);
            current = next;
            sel = sel->next;
            CPrec_NewAddrPatch(sel, (void*)current);
        }
    } while (1);

    return (ObjCSelector*)first;
}

static ObjCMethod* CPrec_GetObjCMethodPatch(ObjCMethod* meth) {
    AddrPatch* addrPatch;
    SInt32 current;
    SInt32 first;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(meth))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(meth, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(meth, sizeof(ObjCMethod));
        if (meth->object) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCMethod, object)), CPrec_GetObjectPatch(meth->object)
            );
        }
        if (meth->functype) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCMethod, functype)),
                CPrec_GetTypePatch((Type*)meth->functype)
            );
        }
        if (meth->selector) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCMethod, selector)),
                CPrec_GetObjCSelectorPatch(meth->selector)
            );
        }
        if (meth->return_type) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCMethod, return_type)),
                CPrec_GetTypePatch(meth->return_type)
            );
        }
        if (meth->selector_args) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCMethod, selector_args)),
                CPrec_GetObjCMethodArgPatch(meth->selector_args)
            );
        }

        if (!meth->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(meth->next))) {
            CPrec_NewPointerPatch((void*)(current + offsetof(ObjCMethod, next)), addrPatch->value);
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(ObjCMethod, next)), (void*)next);
            current = next;
            meth = meth->next;
            CPrec_NewAddrPatch(meth, (void*)current);
        }
    } while (1);

    return (ObjCMethod*)first;
}

static ObjCProtocol* CPrec_GetObjCProtocolPatch(ObjCProtocol* prot) {
    AddrPatch* addrPatch;
    SInt32 current;
    SInt32 first;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(prot))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(prot, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(prot, sizeof(ObjCProtocol));
        CPrec_NamePatch((void*)(current + offsetof(ObjCProtocol, name)), prot->name);
        if (prot->protocols) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCProtocol, protocols)),
                CPrec_GetObjCProtocolListPatch(prot->protocols)
            );
        }
        if (prot->methods) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCProtocol, methods)),
                CPrec_GetObjCMethodPatch(prot->methods)
            );
        }
        if (prot->object) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCProtocol, object)),
                CPrec_GetObjectPatch(prot->object)
            );
        }

        if (!prot->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(prot->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCProtocol, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(ObjCProtocol, next)), (void*)next);
            current = next;
            prot = prot->next;
            CPrec_NewAddrPatch(prot, (void*)current);
        }
    } while (1);

    return (ObjCProtocol*)first;
}

static ObjCProtocolList* CPrec_GetObjCProtocolListPatch(ObjCProtocolList* lst) {
    AddrPatch* addrPatch;
    ObjCProtocolList* first;
    ObjCProtocolList* current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(lst))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(lst, first = current = (void*)CPrec_AppendAlign());

    do {
        CPrec_AppendData(lst, sizeof(ObjCProtocolList));
        CPrec_NewPointerPatch(&current->protocol, CPrec_GetObjCProtocolPatch(lst->protocol));

        if (!lst->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (ObjCProtocolList*)next;
        lst = lst->next;
    } while (1);

    return first;
}

static ObjCCategory* CPrec_GetObjCCategoryPatch(ObjCCategory* cat) {
    AddrPatch* addrPatch;
    SInt32 current;
    SInt32 first;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(cat))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(cat, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(cat, sizeof(ObjCCategory));
        CPrec_NamePatch((void*)(current + offsetof(ObjCCategory, name)), cat->name);
        if (cat->protocols) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCCategory, protocols)),
                CPrec_GetObjCProtocolListPatch(cat->protocols)
            );
        }
        if (cat->methods) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCCategory, methods)),
                CPrec_GetObjCMethodPatch(cat->methods)
            );
        }

        if (!cat->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(cat->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCCategory, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(ObjCCategory, next)), (void*)next);
            current = next;
            cat = cat->next;
            CPrec_NewAddrPatch(cat, (void*)current);
        }
    } while (1);

    return (ObjCCategory*)first;
}

static ObjCInfo* CPrec_GetObjCInfoPatch(ObjCInfo* info) {
    ObjCInfo* p = (void*)CPrec_AppendAlign();
    CPrec_AppendData(info, sizeof(ObjCInfo));

    if (info->classobject) {
        CPrec_NewPointerPatch(&p->classobject, CPrec_GetObjectPatch(info->classobject));
    }
    if (info->metaobject) {
        CPrec_NewPointerPatch(&p->metaobject, CPrec_GetObjectPatch(info->metaobject));
    }
    if (info->classrefobj) {
        CPrec_NewPointerPatch(&p->classrefobj, CPrec_GetObjectPatch(info->classrefobj));
    }
    if (info->methods) {
        CPrec_NewPointerPatch(&p->methods, CPrec_GetObjCMethodPatch(info->methods));
    }
    if (info->protocols) {
        CPrec_NewPointerPatch(&p->protocols, CPrec_GetObjCProtocolListPatch(info->protocols));
    }
    if (info->categories) {
        CPrec_NewPointerPatch(&p->categories, CPrec_GetObjCCategoryPatch(info->categories));
    }

    return p;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static TemplArg* CPrec_GetTemplateArgPatch(TemplArg* arg) {
    TemplArg* first;
    TemplArg* current;
    SInt32 next;
    first = current = (void*)CPrec_AppendAlign();

    do {
        CPrec_AppendData(arg, sizeof(TemplArg));
        switch (arg->pid.type) {
            case TPT_TYPE:
                if (arg->data.typeparam.type) {
                    CPrec_NewPointerPatch(
                        &current->data.typeparam.type, CPrec_GetTypePatch(arg->data.typeparam.type)
                    );
                }
                break;
            case TPT_NONTYPE:
                if (arg->data.paramdecl.expr) {
                    CPrec_NewPointerPatch(
                        &current->data.paramdecl.expr,
                        CPrec_GetExpressionPatch(arg->data.paramdecl.expr)
                    );
                }
                break;
            case TPT_TEMPLATE:
                if (arg->data.typeparam.type) {
                    CPrec_NewPointerPatch(
                        &current->data.typeparam.type, CPrec_GetTypePatch(arg->data.typeparam.type)
                    );
                }
                break;
            default:
                CError_FATAL(1895);
        }

        if (!arg->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (TemplArg*)next;
        arg = arg->next;
    } while (1);

    return first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static TemplParam* CPrec_GetTemplateParamPatch(TemplParam* param) {
    SInt32 first;
    SInt32 current;
    SInt32 next;
    first = current = CPrec_AppendAlign();

    do {
        CPrec_AppendData(param, sizeof(TemplParam));
        if (param->name) {
            CPrec_NamePatch((void*)(current + offsetof(TemplParam, name)), param->name);
        }

        switch (param->pid.type) {
            case TPT_TYPE:
                if (param->data.typeparam.type) {
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(TemplParam, data.typeparam.type)),
                        CPrec_GetTypePatch(param->data.typeparam.type)
                    );
                }
                break;
            case TPT_NONTYPE:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplParam, data.paramdecl.type)),
                    CPrec_GetTypePatch(param->data.paramdecl.type)
                );
                if (param->data.paramdecl.defaultarg) {
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(TemplParam, data.paramdecl.defaultarg)),
                        CPrec_GetExpressionPatch(param->data.paramdecl.defaultarg)
                    );
                }
                break;
            case TPT_TEMPLATE:
                if (param->data.templparam.plist) {
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(TemplParam, data.templparam.plist)),
                        CPrec_GetTemplateParamPatch(param->data.templparam.plist)
                    );
                }
                CError_ASSERT(1969, !param->data.templparam.defaultarg);
                break;
            default:
                CError_FATAL(1974);
        }

        if (!param->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(TemplParam, next)), (void*)next);
        current = next;
        param = param->next;
    } while (1);

    return (TemplParam*)first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static TStreamElement* CPrec_GetTStreamPatch(TStreamElement* tokens, SInt32 count) {
    TStreamElement elem;
    SInt32 first;
    SInt32 current;
    TStreamElement* scan;
    SInt32 x;

    scan = tokens;
    x = 0;
    while (x < count) {
        elem = *scan;
        memclrw(scan, sizeof(TStreamElement));

        switch (scan->tokentype = elem.tokentype) {
            case TK_IDENTIFIER:
                scan->data.tkidentifier = elem.data.tkidentifier;
                break;
            case TK_INTCONST:
                scan->subtype = elem.subtype;
                scan->data.tkintconst = elem.data.tkintconst;
                break;
            case TK_FLOATCONST:
                scan->subtype = elem.subtype;
                scan->data.tkfloatconst = elem.data.tkfloatconst;
                break;
            case TK_STRING:
            case TK_STRING_WIDE:
                scan->subtype = elem.subtype;
                scan->data.tkstring = elem.data.tkstring;
                break;
        }
        x++;
        scan++;
    }

    first = current = CPrec_AppendAlign();
    CPrec_AppendData(tokens, count * sizeof(TStreamElement));

    if (cprec_dowrite) {
        TokenPatch* tp = lalloc(sizeof(TokenPatch));
        tp->tokens = (TStreamElement*)current;
        tp->count = count;
        tp->next = cprec_tokenpatches;
        cprec_tokenpatches = tp;
    }

    scan = tokens;
    x = 0;
    while (x < count) {
        switch (scan->tokentype) {
            case TK_IDENTIFIER:
                CPrec_NamePatch(
                    (void*)(current + offsetof(TStreamElement, data.tkidentifier)),
                    scan->data.tkidentifier
                );
                break;
            case TK_INTCONST:
            case TK_FLOATCONST:
                break;
            case TK_STRING:
            case TK_STRING_WIDE:
                CPrec_RawMemPatch(
                    (void*)(current + offsetof(TStreamElement, data.tkstring.data)),
                    scan->data.tkstring.data,
                    scan->data.tkstring.size
                );
                break;
            case TK_EOL:
                break;
            default:
                if (scan->tokentype < 0) {
                    CError_FATAL(2079);
                }
        }
        x++;
        scan++;
        current += sizeof(TStreamElement);
    }

    return (TStreamElement*)first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static TemplFuncInstance* CPrec_GetTemplFuncInstancePatch(TemplFuncInstance* inst) {
    AddrPatch* addrPatch;
    SInt32 first;
    SInt32 current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(inst))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(inst, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(inst, sizeof(TemplFuncInstance));
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(TemplFuncInstance, object)),
            CPrec_GetObjectPatch(inst->object)
        );
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(TemplFuncInstance, args)),
            CPrec_GetTemplateArgPatch(inst->args)
        );

        if (!inst->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(inst->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplFuncInstance, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplFuncInstance, next)), (void*)next
            );
            current = next;
            inst = inst->next;
            CPrec_NewAddrPatch(inst, (void*)current);
        }
    } while (1);

    return (TemplFuncInstance*)first;
}

static TemplateMember* CPrec_GetTemplateMemberPatch(TemplateMember* memb) {
    TemplateMember* first;
    SInt32 next;
    TemplateMember* current;
    first = current = (void*)CPrec_AppendAlign();

    do {
        memclrw(&memb->fileoffset, sizeof(FileOffsetInfo));
        memb->srcfile = NULL;
        memb->startoffset = 0;
        memb->endoffset = 0;

        CPrec_AppendData(memb, sizeof(TemplateMember));
        if (memb->params) {
            CPrec_NewPointerPatch(&current->params, CPrec_GetTemplateParamPatch(memb->params));
        }
        CPrec_NewPointerPatch(&current->object, CPrec_GetObjectPatch(memb->object));
        if (memb->stream.firsttoken) {
            CPrec_NewPointerPatch(
                &current->stream.firsttoken,
                CPrec_GetTStreamPatch(memb->stream.firsttoken, memb->stream.tokens)
            );
        }

        if (!memb->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (TemplateMember*)next;
        memb = memb->next;
    } while (1);

    return first;
}

static TemplPartialSpec* CPrec_GetTemplPartialSpecPatch(TemplPartialSpec* pspec) {
    TemplPartialSpec* first;
    TemplPartialSpec* current;
    SInt32 next;
    first = current = (void*)CPrec_AppendAlign();

    do {
        CPrec_AppendData(pspec, sizeof(TemplPartialSpec));
        if (pspec->templ) {
            CPrec_NewPointerPatch(&current->templ, CPrec_GetTypePatch((Type*)pspec->templ));
        }
        if (pspec->args) {
            CPrec_NewPointerPatch(&current->args, CPrec_GetTemplateArgPatch(pspec->args));
        }

        if (!pspec->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (TemplPartialSpec*)next;
        pspec = pspec->next;
    } while (1);

    return first;
}

static TemplateFriend* CPrec_GetTemplateFriendPatch(TemplateFriend* frnd) {
    TemplateFriend* p;

    memclrw(&frnd->fileoffset, sizeof(FileOffsetInfo));
    p = (void*)CPrec_AppendAlign();
    CPrec_AppendData(frnd, sizeof(TemplateFriend));

    if (frnd->decl.thetype) {
        CPrec_NewPointerPatch(&p->decl.thetype, CPrec_GetTypePatch(frnd->decl.thetype));
    }
    if (frnd->decl.nspace) {
        CPrec_NewPointerPatch(&p->decl.nspace, CPrec_GetNameSpacePatch(frnd->decl.nspace));
    }
    if (frnd->decl.name) {
        CPrec_NamePatch(&p->decl.name, frnd->decl.name);
    }
    if (frnd->decl.expltargs) {
        CPrec_NewPointerPatch(&p->decl.expltargs, CPrec_GetTemplateArgPatch(frnd->decl.expltargs));
    }
    if (frnd->stream.firsttoken) {
        CPrec_NewPointerPatch(
            &p->stream.firsttoken,
            CPrec_GetTStreamPatch(frnd->stream.firsttoken, frnd->stream.tokens)
        );
    }

    return p;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static TemplateAction* CPrec_GetTemplateActionPatch(TemplateAction* act) {
    SInt32 first;
    SInt32 current;
    SInt32 next;

    first = current = CPrec_AppendAlign();
    for (;;) {
        memclrw(&act->source_ref, sizeof(act->source_ref));
        CPrec_AppendData(act, sizeof(*act));
        switch (act->type) {
            case TAT_NESTEDCLASS:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.tclasstype)),
                    CPrec_GetTypePatch(TYPE(act->u.tclasstype))
                );
                break;
            case TAT_ENUMTYPE:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.enumtype)),
                    CPrec_GetTypePatch(TYPE(act->u.enumtype))
                );
                break;
            case TAT_FRIEND:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.tfriend)),
                    CPrec_GetTemplateFriendPatch(act->u.tfriend)
                );
                break;
            case TAT_ENUMERATOR:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.enumerator.objenumconst)),
                    CPrec_GetObjEnumConstPatch(act->u.enumerator.objenumconst)
                );
                if (act->u.enumerator.initexpr) {
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(TemplateAction, u.enumerator.initexpr)),
                        CPrec_GetExpressionPatch(act->u.enumerator.initexpr)
                    );
                }
                break;
            case TAT_BASE:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.base.type)),
                    CPrec_GetTypePatch(act->u.base.type)
                );
                if (act->u.base.insert_after) {
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(TemplateAction, u.base.insert_after)),
                        CPrec_GetClassListPatch(act->u.base.insert_after)
                    );
                }
                break;
            case TAT_OBJECTINIT:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.objectinit.object)),
                    CPrec_GetObjectPatch(act->u.objectinit.object)
                );
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.objectinit.initexpr)),
                    CPrec_GetExpressionPatch(act->u.objectinit.initexpr)
                );
                break;
            case TAT_USINGDECL:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.usingdecl.type)),
                    CPrec_GetTypeTemplDepPatch(act->u.usingdecl.type)
                );
                break;
            case TAT_OBJECTDEF:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.refobj)),
                    CPrec_GetObjBasePatch(act->u.refobj)
                );
                break;
            default:
                CError_FATAL(2426);
                break;
        }
        if (act->next == NULL) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)current, (void*)next);
        current = next;
        act = act->next;
    }
    return (TemplateAction*)first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static TemplateFunction* CPrec_GetTemplateFunctionPatch(TemplateFunction* tf) {
    AddrPatch* addrPatch;
    SInt32 first;
    SInt32 current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(tf))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(tf, (void*)(first = current = CPrec_AppendAlign()));

    do {
        memclrw(&tf->deftoken, sizeof(TStreamElement));
        tf->srcfile = NULL;
        tf->startoffset = 0;
        tf->endoffset = 0;

        CPrec_AppendData(tf, sizeof(TemplateFunction));
        if (tf->original) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplateFunction, original)),
                CPrec_GetTemplateFunctionPatch(tf->original)
            );
        }
        CPrec_NamePatch((void*)(current + offsetof(TemplateFunction, name)), tf->name);
        if (tf->params) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplateFunction, params)),
                CPrec_GetTemplateParamPatch(tf->params)
            );
        }
        if (tf->stream.firsttoken) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplateFunction, stream.firsttoken)),
                CPrec_GetTStreamPatch(tf->stream.firsttoken, tf->stream.tokens)
            );
        }
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(TemplateFunction, tfunc)), CPrec_GetObjectPatch(tf->tfunc)
        );
        if (tf->instances) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplateFunction, instances)),
                CPrec_GetTemplFuncInstancePatch(tf->instances)
            );
        }

        if (!tf->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(tf->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplateFunction, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(TemplateFunction, next)), (void*)next);
            current = next;
            tf = tf->next;
            CPrec_NewAddrPatch(tf, (void*)current);
        }
    } while (1);

    return (TemplateFunction*)first;
}

static TypeClass* CPrec_GetTypeClassPatch(TypeClass* tclass) {
    SInt32 first;
    SInt32 current;
    SInt32 next;
    SInt32 nextInst;
    AddrPatch* addrPatch;
    Boolean hasNextTempl;
    Boolean hasNextTemplInst;
    HashNameNode* name;

    first = current = CPrec_AppendAlign();

    for (;;) {
        hasNextTempl = hasNextTemplInst = FALSE;
        CPrec_NewAddrPatch(tclass, (void*)current);

        if (tclass->flags & CLASS_IS_TEMPL) {
            CPrec_AppendData(tclass, sizeof(TemplClass));
            if (TEMPL_CLASS(tclass)->next) {
                hasNextTempl = TRUE;
            }
            if (TEMPL_CLASS(tclass)->templ_parent) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, templ_parent)),
                    CPrec_GetTypePatch((Type*)TEMPL_CLASS(tclass)->templ_parent)
                );
            }
            if (TEMPL_CLASS(tclass)->inst_parent) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, inst_parent)),
                    CPrec_GetTypePatch((Type*)TEMPL_CLASS(tclass)->inst_parent)
                );
            }
            if (TEMPL_CLASS(tclass)->templ__params) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, templ__params)),
                    CPrec_GetTemplateParamPatch(TEMPL_CLASS(tclass)->templ__params)
                );
            }
            if (TEMPL_CLASS(tclass)->members) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, members)),
                    CPrec_GetTemplateMemberPatch(TEMPL_CLASS(tclass)->members)
                );
            }
            if (TEMPL_CLASS(tclass)->instances) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, instances)),
                    CPrec_GetTypePatch((Type*)TEMPL_CLASS(tclass)->instances)
                );
            }
            if (TEMPL_CLASS(tclass)->pspec_owner) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, pspec_owner)),
                    CPrec_GetTypePatch((Type*)TEMPL_CLASS(tclass)->pspec_owner)
                );
            }
            if (TEMPL_CLASS(tclass)->pspecs) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, pspecs)),
                    CPrec_GetTemplPartialSpecPatch(TEMPL_CLASS(tclass)->pspecs)
                );
            }
            if (TEMPL_CLASS(tclass)->actions) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, actions)),
                    CPrec_GetTemplateActionPatch(TEMPL_CLASS(tclass)->actions)
                );
            }
        } else if (tclass->flags & CLASS_IS_TEMPL_INST) {
            CPrec_AppendData(tclass, sizeof(TemplClassInst));
            if (TEMPL_CLASS_INST(tclass)->next) {
                hasNextTemplInst = TRUE;
            }
            if (TEMPL_CLASS_INST(tclass)->parent) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClassInst, parent)),
                    CPrec_GetTypePatch((Type*)TEMPL_CLASS_INST(tclass)->parent)
                );
            }
            if (TEMPL_CLASS_INST(tclass)->templ) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClassInst, templ)),
                    CPrec_GetTypePatch((Type*)TEMPL_CLASS_INST(tclass)->templ)
                );
            }
            if (TEMPL_CLASS_INST(tclass)->inst_args) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClassInst, inst_args)),
                    CPrec_GetTemplateArgPatch(TEMPL_CLASS_INST(tclass)->inst_args)
                );
            }
            if (TEMPL_CLASS_INST(tclass)->oargs) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClassInst, oargs)),
                    CPrec_GetTemplateArgPatch(TEMPL_CLASS_INST(tclass)->oargs)
                );
            }
        } else {
            CPrec_AppendData(tclass, sizeof(TypeClass));
        }

        if (tclass->nspace) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, nspace)),
                CPrec_GetNameSpacePatch(tclass->nspace)
            );
        }
        if ((name = tclass->classname)) {
            name->id = 1;
            CPrec_ExistingPointerPatch((void*)(current + offsetof(TypeClass, classname)), name);
        }
        if (tclass->bases) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, bases)),
                CPrec_GetClassListPatch(tclass->bases)
            );
        }
        if (tclass->vbases) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, vbases)),
                CPrec_GetVClassListPatch(tclass->vbases)
            );
        }
        if (tclass->ivars) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, ivars)),
                CPrec_GetObjMemberVarPatch(tclass->ivars)
            );
        }
        if (tclass->friends) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, friends)),
                CPrec_GetClassFriendPatch(tclass->friends)
            );
        }
        if (tclass->vtable) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, vtable)), CPrec_GetVTablePatch(tclass->vtable)
            );
        }
        if (tclass->sominfo) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, sominfo)),
                CPrec_GetSOMInfoPatch(tclass->sominfo)
            );
        }
        if (tclass->objcinfo) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, objcinfo)),
                CPrec_GetObjCInfoPatch(tclass->objcinfo)
            );
        }

        if (hasNextTempl) {
            if (!(addrPatch = CPrec_FindAddrPatch(TEMPL_CLASS(tclass)->next))) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, next)),
                    (void*)(next = CPrec_AppendAlign())
                );
                current = next;
                tclass = (TypeClass*)TEMPL_CLASS(tclass)->next;
                continue;
            }
            CPrec_NewPointerPatch((void*)(current + offsetof(TemplClass, next)), addrPatch->value);
        }

        if (hasNextTemplInst) {
            if (!(addrPatch = CPrec_FindAddrPatch(TEMPL_CLASS_INST(tclass)->next))) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClassInst, next)),
                    (void*)(nextInst = CPrec_AppendAlign())
                );
                current = nextInst;
                tclass = (TypeClass*)TEMPL_CLASS_INST(tclass)->next;
                continue;
            }
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplClassInst, next)), addrPatch->value
            );
        }
        break;
    }

    return (TypeClass*)first;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static Type* CPrec_GetTypePatch(Type* type) {
    AddrPatch* addrPatch;

    if ((addrPatch = CPrec_FindAddrPatch(type))) {
        return addrPatch->value;
    }

    switch (type->type) {
        case TYPEPOINTER:
        case TYPEARRAY:
            return (Type*)CPrec_GetTypePointerPatch(TYPE_POINTER(type));
        case TYPEENUM:
            return (Type*)CPrec_GetTypeEnumPatch(TYPE_ENUM(type));
        case TYPEBITFIELD:
            return (Type*)CPrec_GetTypeBitfieldPatch(TYPE_BITFIELD(type));
        case TYPESTRUCT:
            return (Type*)CPrec_GetTypeStructPatch(TYPE_STRUCT(type));
        case TYPEFUNC:
            return (Type*)CPrec_GetTypeFuncPatch(TYPE_FUNC(type));
        case TYPEMEMBERPOINTER:
            return (Type*)CPrec_GetTypeMemberPointerPatch(TYPE_MEMBER_POINTER(type));
        case TYPETEMPLATE:
            return (Type*)CPrec_GetTypeTemplDepPatch(TYPE_TEMPLATE(type));
        case TYPECLASS:
            return (Type*)CPrec_GetTypeClassPatch(TYPE_CLASS(type));
        case TYPEVOID:
        case TYPEINT:
        case TYPEFLOAT:
        case TYPELABEL:
        case TYPEOBJCID:
        case TYPETEMPLDEPEXPR:
        default:
            CError_FATAL(2812);
            return NULL;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static ExceptionAction* CPrec_GetExceptionPatch(ExceptionAction* exc) {
    SInt32 first;
    SInt32 current;
    SInt32 next;

    first = current = CPrec_AppendAlign();
    for (;;) {
        CPrec_AppendData(exc, sizeof(ExceptionAction));
        switch (exc->type) {
            case EAT_DESTROYLOCAL:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_local.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_local.dtor)
                );
                break;
            case EAT_DESTROYLOCALCOND:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_local_cond.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_local_cond.dtor)
                );
                break;
            case EAT_DESTROYLOCALOFFSET:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_local_offset.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_local_offset.dtor)
                );
                break;
            case EAT_DESTROYLOCALPOINTER:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_local_pointer.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_local_pointer.dtor)
                );
                break;
            case EAT_DESTROYLOCALARRAY:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_local_array.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_local_array.dtor)
                );
                break;
            case EAT_DESTROYPARTIALARRAY:
                break;
            case EAT_DESTROYMEMBER:
            case EAT_DESTROYBASE:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_member.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_member.dtor)
                );
                break;
            case EAT_DESTROYMEMBERCOND:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_member_cond.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_member_cond.dtor)
                );
                break;
            case EAT_DESTROYMEMBERARRAY:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_member_array.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_member_array.dtor)
                );
                break;
            case EAT_DELETEPOINTER:
            case EAT_DELETELOCALPOINTER:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.delete_pointer.deletefunc)),
                    CPrec_GetObjectPatch(exc->data.delete_pointer.deletefunc)
                );
                break;
            case EAT_DELETEPOINTERCOND:
                CPrec_NewPointerPatch(
                    (void*)(current +
                            offsetof(ExceptionAction, data.delete_pointer_cond.deletefunc)),
                    CPrec_GetObjectPatch(exc->data.delete_pointer_cond.deletefunc)
                );
                break;
            case EAT_CATCHBLOCK:
                if (exc->data.catch_block.catch_typeid) {
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(ExceptionAction, data.catch_block.catch_typeid)),
                        CPrec_GetObjectPatch(exc->data.catch_block.catch_typeid)
                    );
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(ExceptionAction, data.catch_block.catch_type)),
                        CPrec_GetTypePatch(exc->data.catch_block.catch_type)
                    );
                }
                break;
            case EAT_ACTIVECATCHBLOCK:
                break;
            case EAT_SPECIFICATION:
                if (exc->data.specification.unexp_id) {
                    int x;
                    SInt32 ptrs;
                    CPrec_AppendAlign();
                    ptrs = cprec_offset;
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(ExceptionAction, data.specification.unexp_id)),
                        (void*)ptrs
                    );
                    CPrec_AppendData(
                        exc->data.specification.unexp_id,
                        sizeof(Object*) * exc->data.specification.unexp_ids
                    );
                    for (x = 0; x < exc->data.specification.unexp_ids; x++) {
                        CPrec_NewPointerPatch(
                            (void*)(ptrs + x * sizeof(Object*)),
                            CPrec_GetObjectPatch(exc->data.specification.unexp_id[x])
                        );
                    }
                }
                break;
            case EAT_TERMINATE:
                break;
            default:
                CError_FATAL(2921);
        }

        if (!exc->prev) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(ExceptionAction, prev)), (void*)next);
        current = next;
        exc = exc->prev;
    }
    return (ExceptionAction*)first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static ENodeList* CPrec_GetExpressionListPatch(ENodeList* lst) {
    SInt32 first;
    SInt32 current;
    SInt32 next;
    first = current = CPrec_AppendAlign();

    for (;;) {
        CPrec_AppendData(lst, sizeof(ENodeList));
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(ENodeList, node)), CPrec_GetExpressionPatch(lst->node)
        );

        if (!lst->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(ENodeList, next)), (void*)next);
        current = next;
        lst = lst->next;
    }

    return (ENodeList*)first;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static EMemberInfo* CPrec_GetEMemberInfoPatch(EMemberInfo* emember) {
    EMemberInfo* p;

    CError_FATAL(2969);

    p = (void*)CPrec_AppendAlign();
    CPrec_AppendData(emember, sizeof(EMemberInfo));

    if (emember->path) {
        CPrec_NewPointerPatch(&p->path, CPrec_GetClassAccessPatch(emember->path));
    }
    if (emember->expr) {
        CPrec_NewPointerPatch(&p->expr, CPrec_GetExpressionPatch(emember->expr));
    }
    CError_ASSERT(2984, !emember->templargs);
    CPrec_NewPointerPatch(&p->list, CPrec_GetNameSpaceObjectListPatch(emember->list));

    return p;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static ENode* CPrec_GetExpressionPatch(ENode* expr) {
    SInt32 p;

    if (ENODE_IS(expr, ETEMPLDEP) && expr->data.templdep.subtype == TDE_SOURCEREF) {
        expr->data.templdep.u.sourceref.token = NULL;
    }

    p = CPrec_AppendAlign();
    CPrec_AppendData(expr, sizeof(ENode));

    CPrec_NewPointerPatch((void*)(p + offsetof(ENode, rtype)), CPrec_GetTypePatch(expr->rtype));

    switch (expr->type) {
        case EPOSTINC:
        case EPOSTDEC:
        case EPREINC:
        case EPREDEC:
        case EINDIRECT:
        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
        case EFORCELOAD:
        case ETYPCON:
        case EBITFIELD:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.monadic)),
                CPrec_GetExpressionPatch(expr->data.monadic)
            );
            break;
        case EMUL:
        case EMULV:
        case EDIV:
        case EMODULO:
        case EADDV:
        case ESUBV:
        case EADD:
        case ESUB:
        case ESHL:
        case ESHR:
        case ELESS:
        case EGREATER:
        case ELESSEQU:
        case EGREATEREQU:
        case EEQU:
        case ENOTEQU:
        case EAND:
        case EXOR:
        case EOR:
        case ELAND:
        case ELOR:
        case EASS:
        case EMULASS:
        case EDIVASS:
        case EMODASS:
        case EADDASS:
        case ESUBASS:
        case ESHLASS:
        case ESHRASS:
        case EANDASS:
        case EXORASS:
        case EORASS:
        case ECOMMA:
        case EPMODULO:
        case EROTL:
        case EROTR:
        case EBCLR:
        case EBTST:
        case EBSET:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.diadic.left)),
                CPrec_GetExpressionPatch(expr->data.diadic.left)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.diadic.right)),
                CPrec_GetExpressionPatch(expr->data.diadic.right)
            );
            break;
        case ECOND:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.cond.cond)),
                CPrec_GetExpressionPatch(expr->data.cond.cond)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.cond.expr1)),
                CPrec_GetExpressionPatch(expr->data.cond.expr1)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.cond.expr2)),
                CPrec_GetExpressionPatch(expr->data.cond.expr2)
            );
            break;
        case ESTRINGCONST:
            CPrec_RawMemPatch(
                (void*)(p + offsetof(ENode, data.string.data)),
                expr->data.string.data,
                expr->data.string.size
            );
            break;
        case EOBJREF:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.objref)), CPrec_GetObjectPatch(expr->data.objref)
            );
            break;
        case EOBJLIST:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.objlist.list)),
                CPrec_GetNameSpaceObjectListPatch(expr->data.objlist.list)
            );
            CError_ASSERT(3059, !expr->data.objlist.templargs);
            if (expr->data.objlist.name) {
                CPrec_NamePatch(
                    (void*)(p + offsetof(ENode, data.objlist.name)), expr->data.objlist.name
                );
            }
            break;
        case EMFPOINTER:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.diadic.left)),
                CPrec_GetExpressionPatch(expr->data.diadic.left)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.diadic.right)),
                CPrec_GetExpressionPatch(expr->data.diadic.right)
            );
            break;
        case ENULLCHECK:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.nullcheck.nullcheckexpr)),
                CPrec_GetExpressionPatch(expr->data.nullcheck.nullcheckexpr)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.nullcheck.condexpr)),
                CPrec_GetExpressionPatch(expr->data.nullcheck.condexpr)
            );
            break;
        case ETEMP:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.temp.type)),
                CPrec_GetTypePatch(expr->data.temp.type)
            );
            break;
        case EFUNCCALL:
        case EFUNCCALLP:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.funccall.funcref)),
                CPrec_GetExpressionPatch(expr->data.funccall.funcref)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.funccall.functype)),
                CPrec_GetTypePatch(TYPE(expr->data.funccall.functype))
            );
            if (expr->data.funccall.args) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(ENode, data.funccall.args)),
                    CPrec_GetExpressionListPatch(expr->data.funccall.args)
                );
            }
            break;
        case EMEMBER:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.emember)),
                CPrec_GetEMemberInfoPatch(expr->data.emember)
            );
            break;
        case ETEMPLDEP:
            switch (expr->data.templdep.subtype) {
                case TDE_PARAM:
                    break;
                case TDE_SIZEOF:
                case TDE_ALIGNOF:
                    CPrec_NewPointerPatch(
                        (void*)(p + offsetof(ENode, data.templdep.u.typeexpr.type)),
                        CPrec_GetTypePatch(expr->data.templdep.u.typeexpr.type)
                    );
                    break;
                case TDE_CAST:
                    if (expr->data.templdep.u.cast.args) {
                        CPrec_NewPointerPatch(
                            (void*)(p + offsetof(ENode, data.templdep.u.cast.args)),
                            CPrec_GetExpressionListPatch(expr->data.templdep.u.cast.args)
                        );
                    }
                    CPrec_NewPointerPatch(
                        (void*)(p + offsetof(ENode, data.templdep.u.cast.type)),
                        CPrec_GetTypePatch(expr->data.templdep.u.cast.type)
                    );
                    break;
                case TDE_QUALNAME:
                    CPrec_NamePatch(
                        (void*)(p + offsetof(ENode, data.templdep.u.qual.name)),
                        expr->data.templdep.u.qual.name
                    );
                    CPrec_NewPointerPatch(
                        (void*)(p + offsetof(ENode, data.templdep.u.qual.type)),
                        CPrec_GetTypePatch(TYPE(expr->data.templdep.u.qual.type))
                    );
                    break;
                case TDE_OBJ:
                    CPrec_NewPointerPatch(
                        (void*)(p + offsetof(ENode, data.templdep.u.obj)),
                        CPrec_GetObjectPatch(expr->data.templdep.u.obj)
                    );
                    break;
                case TDE_SOURCEREF:
                    CPrec_NewPointerPatch(
                        (void*)(p + offsetof(ENode, data.templdep.u.sourceref.expr)),
                        CPrec_GetExpressionPatch(expr->data.templdep.u.sourceref.expr)
                    );
                    break;
                case TDE_ADDRESS_OF:
                    CPrec_NewPointerPatch(
                        (void*)(p + offsetof(ENode, data.templdep.u.monadic)),
                        CPrec_GetExpressionPatch(expr->data.templdep.u.monadic)
                    );
                    break;
                default:
                    CError_FATAL(3152);
            }
            break;
        case EINTCONST:
        case EFLOATCONST:
        case EPRECOMP:
        case EARGOBJ:
        case ELOCOBJ:
        case ELABEL:
        case EINSTRUCTION:
        case EVECTOR128CONST:
            break;
        default:
            CError_FATAL(3158);
    }

    return (ENode*)p;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static CI_Switch* CPrec_GetSwitchInfoPatch(CI_Switch* si) {
    SInt32 p = CPrec_AppendAlign();
    CPrec_AppendData(si, sizeof(CI_Switch) + sizeof(CI_SwitchCase) * (si->numcases - 1));

    CPrec_NewPointerPatch(
        (void*)(p + offsetof(CI_Switch, expr)), CPrec_GetExpressionPatch(si->expr)
    );
    CPrec_NewPointerPatch(
        (void*)(p + offsetof(CI_Switch, exprtype)), CPrec_GetTypePatch(si->exprtype)
    );
    return (CI_Switch*)p;
}

static InlineAsm* CPrec_GetInlineAsmPatch(InlineAsm* ia, SInt32 size) {
    SInt32 p;
    SInt32 index;
    SInt32 offset;
    Object* object;

    p = CPrec_AppendAlign();
    CPrec_AppendData(ia, size);

    for (index = 0;; index++) {
        object = InlineAsm_GetObjectOffset(ia, index, &offset);
        if (!object) {
            break;
        }

        CPrec_NewPointerPatch((void*)(p + offset), CPrec_GetObjectPatch(object));
    }

    return (InlineAsm*)p;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT (-51 + CError_LINE_DELTA)
#endif
static CI_Statement* CPrec_GetStatementPatch(CI_Statement* stmt, short count) {
    short i;
    SInt32 first;
    SInt32 current;

    for (i = 0; i < count; i++) {
        stmt[i].sourcefilepath = NULL;
        stmt[i].sourceoffset = -1;
    }

    first = current = CPrec_AppendAlign();
    CPrec_AppendData(stmt, sizeof(CI_Statement) * count);

    for (i = 0; i < count; i++) {
#if VERSION >= VERSION_GC_2_5
        if (stmt->sourcefilepath) {
            CPrec_NamePatch(
                (void*)(current + offsetof(CI_Statement, sourcefilepath)), stmt->sourcefilepath
            );
        }
#endif
        if (stmt->dobjstack) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(CI_Statement, dobjstack)),
                CPrec_GetExceptionPatch(stmt->dobjstack)
            );
        }

        switch (stmt->type) {
            case ST_EXPRESSION:
            case ST_RETURN:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
                if (stmt->u.expr) {
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(CI_Statement, u.expr)),
                        CPrec_GetExpressionPatch(stmt->u.expr)
                    );
                }
                break;
            case ST_IFGOTO:
            case ST_IFNGOTO:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(CI_Statement, u.ifgoto.expr)),
                    CPrec_GetExpressionPatch(stmt->u.ifgoto.expr)
                );
                break;
            case ST_SWITCH:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(CI_Statement, u.switchdata)),
                    CPrec_GetSwitchInfoPatch(stmt->u.switchdata)
                );
                break;
            case ST_ASM:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(CI_Statement, u.asmdata.data)),
                    CPrec_GetInlineAsmPatch(stmt->u.asmdata.data, stmt->u.asmdata.size)
                );
                break;
            case ST_NOP:
            case ST_LABEL:
            case ST_GOTO:
                break;
            default:
                CError_FATAL(3328);
        }

        current += sizeof(CI_Statement);
        stmt++;
    }

    return (CI_Statement*)first;
}

static CI_Var* CPrec_GetLocObjectPatch(CI_Var* obj, short count) {
    SInt32 first;
    SInt32 current;
    short i;

    first = current = CPrec_AppendAlign();
    CPrec_AppendData(obj, sizeof(CI_Var) * count);

    for (i = 0; i < count; i++) {
        CPrec_NamePatch((void*)(current + offsetof(CI_Var, name)), obj->name);
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(CI_Var, type)), CPrec_GetTypePatch(obj->type)
        );
        current += sizeof(CI_Var);
        obj++;
    }

    return (CI_Var*)first;
}

static CI_FuncData* CPrec_GetInlineFuncPatch(CI_FuncData* ifunc) {
    CI_FuncData* p;

    memclrw(&ifunc->fileoffset, sizeof(FileOffsetInfo));
    ifunc->symdecloffset = 0;
    ifunc->functionbodyoffset = 0;
    ifunc->functionbodypath = NULL;
    ifunc->symdeclend = 0;

    p = (void*)CPrec_AppendAlign();
    CPrec_AppendData(ifunc, sizeof(CI_FuncData));

    if (ifunc->numarguments) {
        CPrec_NewPointerPatch(
            &p->arguments, CPrec_GetLocObjectPatch(ifunc->arguments, ifunc->numarguments)
        );
    }
    if (ifunc->numlocals) {
        CPrec_NewPointerPatch(&p->locals, CPrec_GetLocObjectPatch(ifunc->locals, ifunc->numlocals));
    }
    if (ifunc->numstatements) {
        CPrec_NewPointerPatch(
            &p->statements, CPrec_GetStatementPatch(ifunc->statements, ifunc->numstatements)
        );
    }

    return p;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static ObjEnumConst* CPrec_GetObjEnumConstPatch(ObjEnumConst* obj) {
    AddrPatch* addrPatch;
    SInt32 first;
    SInt32 current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(obj))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(obj, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(obj, sizeof(ObjEnumConst));
        if (cprec_dowrite) {
            CError_ASSERT(3416, obj->access != 255);
            obj->access = 255;
        }
        CPrec_NamePatch((void*)(current + offsetof(ObjEnumConst, name)), obj->name);
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(ObjEnumConst, type)), CPrec_GetTypePatch(obj->type)
        );

        if (!obj->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(obj->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjEnumConst, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(ObjEnumConst, next)), (void*)next);
            current = next;
            obj = obj->next;
            CPrec_NewAddrPatch(obj, (void*)current);
        }
    } while (1);

    return (ObjEnumConst*)first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static ObjType* CPrec_GetObjTypePatch(ObjType* obj) {
    AddrPatch* addrPatch;
    SInt32 p;

    if ((addrPatch = CPrec_FindAddrPatch(obj))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(obj, (void*)(p = CPrec_AppendAlign()));
    CPrec_AppendData(obj, sizeof(ObjType));
    CPrec_NewPointerPatch((void*)(p + offsetof(ObjType, type)), CPrec_GetTypePatch(obj->type));
    return (ObjType*)p;
}

static ObjTypeTag* CPrec_GetObjTypeTagPatch(ObjTypeTag* obj) {
    AddrPatch* addrPatch;
    SInt32 p;

    if ((addrPatch = CPrec_FindAddrPatch(obj))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(obj, (void*)(p = CPrec_AppendAlign()));
    CPrec_AppendData(obj, sizeof(ObjTypeTag));
    CPrec_NewPointerPatch((void*)(p + offsetof(ObjTypeTag, type)), CPrec_GetTypePatch(obj->type));
    return (ObjTypeTag*)p;
}

static ObjMemberVar* CPrec_GetObjMemberVarPatch(ObjMemberVar* ivar) {
    AddrPatch* addrPatch;
    SInt32 first;
    SInt32 current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(ivar))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(ivar, (void*)(first = current = CPrec_AppendAlign()));

    while (1) {
        if (ivar->has_path) {
            CPrec_AppendData(ivar, sizeof(ObjMemberVarPath));
            if (OBJ_MEMBER_VAR_PATH(ivar)->path) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ObjMemberVarPath, path)),
                    CPrec_GetClassAccessPatch(OBJ_MEMBER_VAR_PATH(ivar)->path)
                );
            }
        } else {
            CPrec_AppendData(ivar, sizeof(ObjMemberVar));
        }

        if (ivar->name) {
            CPrec_NamePatch((void*)(current + offsetof(ObjMemberVar, name)), ivar->name);
        }
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(ObjMemberVar, type)), CPrec_GetTypePatch(ivar->type)
        );

        if (!ivar->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(ivar->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjMemberVar, next)), addrPatch->value
            );
            break;
        }

        next = CPrec_AppendAlign();
        CPrec_NewPointerPatch((void*)(current + offsetof(ObjMemberVar, next)), (void*)next);

        current = next;
        ivar = ivar->next;
        CPrec_NewAddrPatch(ivar, (void*)current);
    }

    return (ObjMemberVar*)first;
}

static DefArgCtorInfo* CPrec_GetDefArgCtorInfoPatch(DefArgCtorInfo* dac) {
    DefArgCtorInfo* p = (DefArgCtorInfo*)CPrec_AppendAlign();
    CPrec_AppendData(dac, sizeof(DefArgCtorInfo));
    CPrec_NewPointerPatch(&p->default_func, CPrec_GetObjectPatch(dac->default_func));
    CPrec_NewPointerPatch(&p->default_arg, CPrec_GetExpressionPatch(dac->default_arg));
    return p;
}

static InlineXRef* CPrec_GetInlineXRefPatch(InlineXRef* ix) {
    SInt32 first;
    SInt32 current;
    SInt32 next;
    first = current = CPrec_AppendAlign();

    for (;;) {
        CPrec_AppendData(ix, sizeof(InlineXRef) + sizeof(ix->xref[0]) * (ix->numxrefs - 1));
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(InlineXRef, object)), CPrec_GetObjectPatch(ix->object)
        );

        if (!ix->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(InlineXRef, next)), (void*)next);
        current = next;
        ix = ix->next;
    }

    return (InlineXRef*)first;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static Object* CPrec_GetObjectPatch(Object* obj) {
    AddrPatch* addrPatch;
    SInt32 p;

    if (CWUserBreak(cparams.context) != cwNoErr) {
        CError_UserBreak();
    }

    if ((addrPatch = CPrec_FindAddrPatch(obj))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(obj, (void*)(p = CPrec_AppendAlign()));

    obj->toc = NULL;
    obj->debugInfo.symbol = NULL;

    if ((obj->qual & Q_IS_TEMPLATED) && obj->datatype != DALIAS) {
        CPrec_AppendData(obj, sizeof(ObjectTemplated));
        CPrec_NewPointerPatch(
            &((ObjectTemplated*)p)->parent, CPrec_GetObjectPatch(((ObjectTemplated*)obj)->parent)
        );
    } else {
        CPrec_AppendData(obj, sizeof(Object));
    }

    if (obj->nspace) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(Object, nspace)), CPrec_GetNameSpacePatch(obj->nspace)
        );
    }
    if (obj->name) {
        CPrec_NamePatch((void*)(p + offsetof(Object, name)), obj->name);
    }
    CPrec_NewPointerPatch((void*)(p + offsetof(Object, type)), CPrec_GetTypePatch(obj->type));

    switch (obj->datatype) {
        case DABSOLUTE:
            break;

        case DLOCAL:
            CError_FATAL(3647);
            break;

        case DFUNC:
        case DVFUNC:
            if (IS_TEMPL_FUNC(obj->type)) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(Object, u.func.u.templ)),
                    CPrec_GetTemplateFunctionPatch(obj->u.func.u.templ)
                );
            } else if ((obj->qual & Q_INLINE) && obj->u.func.u.ifuncdata) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(Object, u.func.u.ifuncdata)),
                    CPrec_GetInlineFuncPatch(obj->u.func.u.ifuncdata)
                );
            }

            if (obj->u.func.defargdata) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(Object, u.func.defargdata)),
                    CPrec_GetDefArgCtorInfoPatch(obj->u.func.defargdata)
                );
            }
            if (obj->u.func.linkname) {
                CPrec_NamePatch(
                    (void*)(p + offsetof(Object, u.func.linkname)), obj->u.func.linkname
                );
            }
            if (obj->u.func.inst) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(Object, u.func.inst)),
                    CPrec_GetTemplFuncInstancePatch(obj->u.func.inst)
                );
            }
            break;

        case DDATA:
            CError_ASSERT(3689, !obj->u.data.info);
            if (obj->qual & Q_INLINE_DATA) {
                switch (obj->type->type) {
                    case TYPEINT:
                    case TYPEENUM:
                    case TYPEPOINTER:
                        break;
                    case TYPEFLOAT:
                        CPrec_RawMemPatch(
                            (void*)(p + offsetof(Object, u.data.u.floatconst)),
                            obj->u.data.u.floatconst,
                            sizeof(Float)
                        );
                        break;
                    default:
                        CError_FATAL(3705);
                }
            }
            if (obj->u.data.linkname) {
                CPrec_NamePatch(
                    (void*)(p + offsetof(Object, u.data.linkname)), obj->u.data.linkname
                );
            }
            break;

        case DINLINEFUNC:
            CPrec_RawMemPatch(
                (void*)(p + offsetof(Object, u.ifunc.data)), obj->u.ifunc.data, obj->u.ifunc.size
            );
            if (obj->u.ifunc.xrefs) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(Object, u.ifunc.xrefs)),
                    CPrec_GetInlineXRefPatch(obj->u.ifunc.xrefs)
                );
            }
            break;

        case DALIAS:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(Object, u.alias.object)),
                CPrec_GetObjectPatch(obj->u.alias.object)
            );
            if (obj->u.alias.member) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(Object, u.alias.member)),
                    CPrec_GetClassAccessPatch(obj->u.alias.member)
                );
            }
            break;

        default:
            CError_FATAL(3744);
    }

    if (cprec_dowrite) {
        obj->datatype = -1;
    }

    return (Object*)p;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static ObjBase* CPrec_GetObjBasePatch(ObjBase* obj) {
    switch (obj->otype) {
        default:
            CError_FATAL(3761);
        case OT_ENUMCONST:
            return (ObjBase*)CPrec_GetObjEnumConstPatch((ObjEnumConst*)obj);
        case OT_TYPE:
            return (ObjBase*)CPrec_GetObjTypePatch((ObjType*)obj);
        case OT_TYPETAG:
            return (ObjBase*)CPrec_GetObjTypeTagPatch((ObjTypeTag*)obj);
        case OT_NAMESPACE: {
            AddrPatch* addrPatch;
            ObjNameSpace* p;
            ObjNameSpace* q;

            if ((addrPatch = CPrec_FindAddrPatch(obj))) {
                p = addrPatch->value;
            } else {
                q = p = (void*)CPrec_AppendAlign();
                CPrec_NewAddrPatch(obj, q);
                CPrec_AppendData(obj, sizeof(ObjNameSpace));
                CPrec_NewPointerPatch(
                    &p->nspace, CPrec_GetNameSpacePatch(((ObjNameSpace*)obj)->nspace)
                );
            }
            return (ObjBase*)p;
        }
        case OT_MEMBERVAR:
            return (ObjBase*)CPrec_GetObjMemberVarPatch((ObjMemberVar*)obj);
        case OT_OBJECT:
            return (ObjBase*)CPrec_GetObjectPatch((Object*)obj);
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static NameSpaceObjectList* CPrec_GetNameSpaceObjectListPatch(NameSpaceObjectList* nsol) {
    AddrPatch* addrPatch;
    SInt32 first;
    SInt32 current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(nsol))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(nsol, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(nsol, sizeof(NameSpaceObjectList));
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(NameSpaceObjectList, object)),
            CPrec_GetObjBasePatch(nsol->object)
        );

        if (!nsol->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(nsol->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(NameSpaceObjectList, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(NameSpaceObjectList, next)), (void*)next
            );
            current = next;
            nsol = nsol->next;
            CPrec_NewAddrPatch(nsol, (void*)current);
        }
    } while (1);

    return (NameSpaceObjectList*)first;
}

static NameSpaceName* CPrec_GetNameSpaceNamePatch(NameSpaceName* nsn, Boolean flushBuffer) {
    AddrPatch* addrPatch;
    SInt32 first;
    SInt32 current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(nsn))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(nsn, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(nsn, sizeof(NameSpaceName));
        CPrec_NamePatch((void*)(current + offsetof(NameSpaceName, name)), nsn->name);
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(NameSpaceName, first.object)),
            CPrec_GetObjBasePatch(nsn->first.object)
        );
        if (nsn->first.next) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(NameSpaceName, first.next)),
                CPrec_GetNameSpaceObjectListPatch(nsn->first.next)
            );
        }

        if (!nsn->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(nsn->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(NameSpaceName, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(NameSpaceName, next)), (void*)next);
            current = next;
            nsn = nsn->next;
            CPrec_NewAddrPatch(nsn, (void*)current);
        }
    } while (!flushBuffer || !cprec_dowrite || CPrec_FlushBufferCheck() == noErr);

    return (NameSpaceName*)first;
}

static NameSpaceList* CPrec_GetNameSpaceListPatch(NameSpaceList* nsl) {
    SInt32 first;
    SInt32 current;
    SInt32 next;
    first = current = CPrec_AppendAlign();

    for (;;) {
        CPrec_AppendData(nsl, sizeof(NameSpaceList));
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(NameSpaceList, nspace)), CPrec_GetNameSpacePatch(nsl->nspace)
        );

        if (!nsl->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(NameSpaceList, next)), (void*)next);
        current = next;
        nsl = nsl->next;
    }

    return (NameSpaceList*)first;
}

static NameSpace* CPrec_GetNameSpacePatch(NameSpace* nspace) {
    SInt32 p;
    AddrPatch* addrPatch;

    if ((addrPatch = CPrec_FindAddrPatch(nspace))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(nspace, (void*)(p = CPrec_AppendAlign()));
    CPrec_AppendData(nspace, sizeof(NameSpace));

    if (nspace->parent) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(NameSpace, parent)), CPrec_GetNameSpacePatch(nspace->parent)
        );
    }
    if (nspace->name) {
        CPrec_NamePatch((void*)(p + offsetof(NameSpace, name)), nspace->name);
    }
    if (nspace->usings) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(NameSpace, usings)), CPrec_GetNameSpaceListPatch(nspace->usings)
        );
    }
    if (nspace->theclass) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(NameSpace, theclass)), CPrec_GetTypePatch((Type*)nspace->theclass)
        );
    }

    if (nspace->is_hash) {
        SInt32 hash;
        SInt32 i;
        hash = CPrec_AppendAlign();
        CPrec_NewPointerPatch((void*)(p + offsetof(NameSpace, data.hash)), (void*)hash);
        CPrec_AppendData(nspace->data.hash, sizeof(NameSpaceName*) * 1024);
        for (i = 0; i < 1024; i++) {
            if (nspace->data.hash[i]) {
                CPrec_NewPointerPatch(
                    (void*)(hash + i * sizeof(NameSpaceName*)),
                    CPrec_GetNameSpaceNamePatch(nspace->data.hash[i], FALSE)
                );
            }
        }
    } else if (nspace->data.list) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(NameSpace, data.list)),
            CPrec_GetNameSpaceNamePatch(nspace->data.list, FALSE)
        );
    }

    return (NameSpace*)p;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static void CPrec_DumpRootNameSpace(void) {
    NameSpaceList* nslist;
    NameSpaceName* nsname;
    SInt32 i;

    CError_ASSERT(3972, cscope_root->is_hash);

    if (cscope_root->usings) {
        nslist = CPrec_GetNameSpaceListPatch(cscope_root->usings);
        if (cprec_dowrite) {
            cprec_header->usings = nslist;
        }
    }

    if (cprec_dowrite) {
        cprec_header->root_names = cscope_root->names;
    }

    for (i = 0; i < 0x400; i++) {
        if (cscope_root->data.hash[i]) {
            nsname = CPrec_GetNameSpaceNamePatch(cscope_root->data.hash[i], TRUE);
            if (cprec_dowrite) {
                if (cprec_ioerror != noErr) {
                    return;
                }
                cprec_header->root_nametable[i] = nsname;
            }
        }
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static CSOMStub* CPrec_GetSOMPatch(CSOMStub* stub) {
    CSOMStub* first;
    SInt32 next;
    CSOMStub* current;

    first = current = (void*)CPrec_AppendAlign();
    while (1) {
        CPrec_AppendData(stub, sizeof(CSOMStub));

        CPrec_NewPointerPatch(&current->object, CPrec_GetObjectPatch(stub->object));
        CPrec_NewPointerPatch(&current->tclass, CPrec_GetTypePatch(TYPE(stub->tclass)));

        if (!stub->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (CSOMStub*)next;
        stub = stub->next;
    }

    return first;
}

static OLinkList* CPrec_GetOLinkPatch(OLinkList* olink) {
    SInt32 first;
    SInt32 current;
    SInt32 next;

    first = current = CPrec_AppendAlign();
    for (;;) {
        CPrec_AppendData(olink, sizeof(OLinkList));

        CPrec_NewPointerPatch(
            (void*)(current + offsetof(OLinkList, obj)), CPrec_GetObjectPatch(olink->obj)
        );

        if (!olink->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(OLinkList, next)), (void*)next);
        current = next;
        olink = olink->next;
    }

    return (OLinkList*)first;
}

static StaticData* CPrec_GetStaticDataPatch(StaticData* sd) {
    SInt32 current;
    SInt32 first;
    SInt32 next;

    first = current = CPrec_AppendAlign();
    while (1) {
        CPrec_AppendData(sd, sizeof(StaticData));

        CPrec_NewPointerPatch(
            (void*)(current + offsetof(StaticData, object)), CPrec_GetObjectPatch(sd->object)
        );
        if (sd->buffer) {
            CPrec_RawMemPatch(
                (void*)(current + offsetof(StaticData, buffer)), sd->buffer, sd->object->type->size
            );
        }
        if (sd->links) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(StaticData, links)), CPrec_GetOLinkPatch(sd->links)
            );
        }

        if (!sd->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(StaticData, next)), (void*)next);
        current = next;
        sd = sd->next;
    }

    return (StaticData*)first;
}

static CallbackAction* CPrec_GetCallbackPatch(CallbackAction* ca) {
    CallbackAction* first;
    SInt32 next;
    CallbackAction* current;

    first = current = (void*)CPrec_AppendAlign();
    while (1) {
        CPrec_AppendData(ca, sizeof(CallbackAction));

        CPrec_NewPointerPatch(&current->obj, CPrec_GetObjectPatch(ca->obj));
        CPrec_NewPointerPatch(&current->tclass, CPrec_GetTypePatch(TYPE(ca->tclass)));

        if (!ca->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (CallbackAction*)next;
        ca = ca->next;
    }

    return first;
}

static ObjCSelector** CPrec_GetSelHashTablePatch(ObjCSelector** table) {
    SInt32 first;
    int i;
    SInt32 current;
    ObjCSelector* sel;

    CPrec_AppendAlign();
    first = current = cprec_offset;
    CPrec_AppendData(table, sizeof(ObjCSelector*) * 0x400);

    for (i = 0; (unsigned int)i < 0x400U; i++, current += 4) {
        if ((sel = table[i])) {
            CPrec_NewPointerPatch((void*)current, CPrec_GetObjCSelectorPatch(sel));
        }
    }

    return (ObjCSelector**)first;
}

static InitExpr* CPrec_GetIExpressionPatch(InitExpr* initexpr) {
    InitExpr* first;
    SInt32 next;
    InitExpr* current;

    first = current = (void*)CPrec_AppendAlign();
    while (1) {
        CPrec_AppendData(initexpr, sizeof(InitExpr));

        CPrec_NewPointerPatch(&current->object, CPrec_GetObjectPatch(initexpr->object));
        CPrec_NewPointerPatch(&current->expr, CPrec_GetExpressionPatch(initexpr->expr));

        if (!initexpr->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (InitExpr*)next;
        initexpr = initexpr->next;
    }

    return first;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static CI_Action* CPrec_GetInlineActionPatch(CI_Action* act) {
    CI_Action* first;
    SInt32 next;
    CI_Action* current;

    first = current = (void*)CPrec_AppendAlign();
    while (1) {
        if (act->actiontype == CI_ActionInlineFunc) {
            memclrw(&act->u.inlinefunc.fileoffset, sizeof(FileOffsetInfo));
        }

        CPrec_AppendData(act, sizeof(CI_Action));

        CPrec_NewPointerPatch(&current->obj, CPrec_GetObjectPatch(act->obj));

        switch (act->actiontype) {
            case CI_ActionInlineFunc:
                if (act->u.inlinefunc.stream.firsttoken) {
                    CPrec_NewPointerPatch(
                        &current->u.inlinefunc.stream.firsttoken,
                        CPrec_GetTStreamPatch(
                            act->u.inlinefunc.stream.firsttoken, act->u.inlinefunc.stream.tokens
                        )
                    );
                }
                if (act->u.inlinefunc.tclass) {
                    CPrec_NewPointerPatch(
                        &current->u.inlinefunc.tclass,
                        CPrec_GetTypePatch(TYPE(act->u.inlinefunc.tclass))
                    );
                }
                break;

            case CI_ActionMemberFunc:
                CPrec_NewPointerPatch(
                    &current->u.memberfunc.templ, CPrec_GetTypePatch(TYPE(act->u.memberfunc.templ))
                );
                CPrec_NewPointerPatch(
                    &current->u.memberfunc.inst, CPrec_GetTypePatch(TYPE(act->u.memberfunc.inst))
                );
                CPrec_NewPointerPatch(
                    &current->u.memberfunc.tmemb,
                    CPrec_GetTemplateMemberPatch(act->u.memberfunc.tmemb)
                );
                break;

            case CI_ActionTemplateFunc:
                CPrec_NewPointerPatch(
                    &current->u.templatefunc.func,
                    CPrec_GetTemplateFunctionPatch(act->u.templatefunc.func)
                );
                CPrec_NewPointerPatch(
                    &current->u.templatefunc.inst,
                    CPrec_GetTemplFuncInstancePatch(act->u.templatefunc.inst)
                );
                break;

            case CI_ActionDefaultFunc:
                break;

            default:
                CError_FATAL(4244);
        }

        if (!act->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (CI_Action*)next;
        act = act->next;
    }

    return first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static void CPrec_GenerateBuiltinPatches(void) {
    Patch* scan;
    int x;
    int y;

    for (x = 0; x < cprec_builtins; x++) {
        y = 0;
        for (scan = cprec_builtin[x].patches; scan; scan = scan->next) {
            ++y;
        }

        if (y) {
            CPrec_AppendWord32(y);
            CPrec_AppendWord32(x);
            for (scan = cprec_builtin[x].patches; scan; scan = scan->next) {
                CPrec_AppendWord32(scan->offset);
            }
        }
    }

    CPrec_AppendWord32(0);
}

static void CPrec_GenerateTokenStreamPatches(void) {
    TokenPatch* scan;

    for (scan = cprec_tokenpatches; scan; scan = scan->next) {
        CPrec_AppendWord32((UInt32)scan->tokens);
        CPrec_AppendWord32((UInt32)scan->count);
    }
    CPrec_AppendWord32(0);
}

static OSErr CPrec_CompressWrite(const char* data, SInt32 size) {
    char buf[2048 + 256];
    OSErr err;
    int bufpos = 0;
    int blockstart;
    int c;
    const char* p = data;
    const char* end = data + size;

    for (;;) {
        if (p < end) {
            if (!*p) {
                c = 224;
                while (!*p && p < end && c < 256) {
                    c++;
                    p++;
                }
                buf[bufpos++] = c - 1;
            } else {
                blockstart = bufpos++;
                c = 0;
                while (p < end && c < 224) {
                    if (!p[0] && !p[1]) {
                        break;
                    } else {
                        buf[bufpos++] = *(p++);
                        c++;
                    }
                }
                buf[blockstart] = c - 1;
            }
        }

        if (p >= end || bufpos > 2048) {
            if ((err = COS_FileWrite(cprec_refnum, buf, bufpos))) {
                return err;
            }

            if (p >= end) {
                break;
            } else {
                bufpos = 0;
            }
        }
    }

    return noErr;
}

static OSErr CPrec_FlushRawBuffer(void) {
    OSErr err;

    if (cprec_dowrite) {
        CPrec_AppendAlign();
        cprec_zero_offset += cprec_glist.size;
        COS_LockHandle(cprec_glist.data);
        err = CPrec_CompressWrite(*cprec_glist.data, cprec_glist.size);
        COS_UnlockHandle(cprec_glist.data);
        cprec_glist.size = 0;

        return err;
    } else {
        return noErr;
    }
}

static OSErr CPrec_FlushBufferCheck(void) {
    OSErr err;

    if (cprec_glist.size > flushmax) {
        flushmax = cprec_glist.size;
    }

    if (cprec_glist.size > 10000) {
        if ((err = CPrec_FlushRawBuffer())) {
            return cprec_ioerror = err;
        }
    }

    return noErr;
}

static void CPrec_AppendBigEndianWord32(SInt32 v) {
    union {
        SInt32 l;
        UInt8 b[4];
    } u;
    u.l = v;
    CPrec_AppendByte(u.b[3]);
    CPrec_AppendByte(u.b[2]);
    CPrec_AppendByte(u.b[1]);
    CPrec_AppendByte(u.b[0]);
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static int CPrec_CompressPatches(void) {
    Patch* scan;
    int count;
    SInt32 value;
    SInt32 last;
    SInt32 delta;

    cprec_glist.size = 0;

    scan = cprec_patch_list;
    count = last = 0;
    while (scan) {
        CError_ASSERT(4406, (scan->offset & 0x80000001) == 0);

        delta = scan->offset - last;
        value = scan->offset;
        if (delta >= -128 && delta <= 126) {
            CPrec_AppendByte((delta >> 1) | 0x80);
        } else {
            CPrec_AppendBigEndianWord32(value);
        }

        last = scan->offset;
        scan = scan->next;
        count++;
    }

    return count;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static OSErr CPrec_DumpColorSymbolTable(Boolean doWrite) {
    OSErr err;

    freelheap();

    CPrec_InitAddressHashTable();
    CPrec_InitPointerHashTable();

    cprec_patch_list = NULL;
    cprec_tokenpatches = NULL;
    cprec_offset = 0;
    cprec_zero_offset = 0;
    cprec_dowrite = doWrite;
    cprec_ioerror = noErr;

    CPrec_SetupBuiltIn();
    CPrec_AppendWord32(0);

    CPrec_DumpNameTable();
    if ((err = CPrec_FlushRawBuffer()) != noErr) {
        return err;
    }

    CPrec_DumpMacroTable();
    if ((err = CPrec_FlushRawBuffer()) != noErr) {
        return err;
    }

    CPrec_DumpRootNameSpace();
    if (doWrite) {
        if (cprec_ioerror != noErr) {
            return cprec_ioerror;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (ctempl_templates) {
        TemplClass* p = TEMPL_CLASS(CPrec_GetTypePatch(TYPE(ctempl_templates)));
        if (doWrite) {
            cprec_header->ctempl_templates = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (ctempl_templatefuncs) {
        TemplateFunction* p = CPrec_GetTemplateFunctionPatch(ctempl_templatefuncs);
        if (doWrite) {
            cprec_header->ctempl_templatefuncs = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (csom_stubs) {
        CSOMStub* p = CPrec_GetSOMPatch(csom_stubs);
        if (doWrite) {
            cprec_header->csom_stubs = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cprec_staticdata) {
        StaticData* p = CPrec_GetStaticDataPatch(cprec_staticdata);
        if (doWrite) {
            cprec_header->cprec_staticdata = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (callbackactions) {
        CallbackAction* p = CPrec_GetCallbackPatch(callbackactions);
        if (doWrite) {
            cprec_header->callbackactions = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cobjc_type_class) {
        Type* p = CPrec_GetTypePatch(cobjc_type_class);
        if (doWrite) {
            cprec_header->cobjc_type_class = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cobjc_type_id) {
        Type* p = CPrec_GetTypePatch(cobjc_type_id);
        if (doWrite) {
            cprec_header->cobjc_type_id = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cobjc_type_sel) {
        Type* p = CPrec_GetTypePatch(cobjc_type_sel);
        if (doWrite) {
            cprec_header->cobjc_type_sel = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cobjc_selhashtable) {
        ObjCSelector** p = CPrec_GetSelHashTablePatch(cobjc_selhashtable);
        if (doWrite) {
            cprec_header->cobjc_selhashtable = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cobjc_classdefs) {
        struct BClassList* p =
            (struct BClassList*)CPrec_GetBClassListPatch((BClassList*)cobjc_classdefs);
        if (doWrite) {
            cprec_header->cobjc_classdefs = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cobjc_protocols) {
        ObjCProtocol* p = CPrec_GetObjCProtocolPatch(cobjc_protocols);
        if (doWrite) {
            cprec_header->cobjc_protocols = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (init_expressions) {
        InitExpr* p = CPrec_GetIExpressionPatch(init_expressions);
        if (doWrite) {
            cprec_header->init_expressions = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cinline_tactionlist) {
        CI_Action* p = CPrec_GetInlineActionPatch(cinline_tactionlist);
        if (doWrite) {
            cprec_header->cinline_tactionlist = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    return noErr;
}

static OSErr CPrec_FileAlign(short refnum, SInt32* len) {
    char buf[8];
    SInt32 n;
    OSErr err;

    if ((*len & 7) == 0) {
        return noErr;
    }

    n = 8 - (*len & 7);
    memclrw(buf, 8);
    err = COS_FileWrite(refnum, buf, n);
    *len += n;
    return err;
}

static OSErr CPrec_WriteFile(void) {
    SInt32 offset;
    char str[128];
    HashNameNode* name;
    int i;
    OSErr err;

    if (InitGList(&cprec_glist, 0x40000)) {
        CError_NoMem();
    }

    CompilerGetCString(10, str);
    CWShowStatus(cparams.context, str, "");
    CPrep_RemoveSpecialMacros();

    i = 0;
    do {
        for (name = name_hash_nodes[i]; name; name = name->next) {
            name->id = 0;
        }
    } while (++i < 0x800);

    if ((err = CPrec_DumpColorSymbolTable(FALSE)) != noErr) {
        return err;
    }

    CompilerGetCString(11, str);
    CWShowStatus(cparams.context, str, "");

    cprec_header = galloc(sizeof(Header));
    memclrw(cprec_header, sizeof(Header));

    cprec_header->magic = 0xBEEFFACE;
    #if VERSION < VERSION_GC_2_0
    cprec_header->version = 1047;
#else
    cprec_header->version = 5145;
#endif
    cprec_header->target = 2;
    cprec_header->check_header_flags = copts.checkprecompflags;
    cprec_header->cplusplus = copts.cplusplus;
    cprec_header->uniqueID = CParser_GetUniqueID();
    cprec_header->cobjc_selrefcount = cobjc_selrefcount;
    cprec_header->cobjc_classrefcount = cobjc_classrefcount;
    cprec_header->cobjc_stringcount = cobjc_stringcount;

    if ((err = COS_FileWrite(cprec_refnum, cprec_header, sizeof(Header))) != noErr) {
        return err;
    }

    offset = sizeof(Header);

    if ((err = CPrec_DumpColorSymbolTable(TRUE)) != noErr) {
        return err;
    }

    cprec_header->rawsize = cprec_offset;
    cprec_header->rawdataoffset = offset;

    if ((err = COS_FileGetPos(cprec_refnum, &offset)) != noErr) {
        return err;
    }

    cprec_header->rawdatasize = offset - cprec_header->rawdataoffset;

    cprec_header->compressedPatchCount = CPrec_CompressPatches();
    cprec_header->compressedPatchSize = cprec_glist.size;
    cprec_header->compressedPatchOffset = offset;

    if (cprec_header->compressedPatchCount) {
        if ((err = COS_FileWrite(cprec_refnum, *cprec_glist.data, cprec_glist.size)) != noErr) {
            return err;
        }
        offset += cprec_glist.size;
    }

    if ((err = CPrec_FileAlign(cprec_refnum, &offset)) != noErr) {
        return err;
    }

    cprec_glist.size = 0;
    CPrec_GenerateBuiltinPatches();
    cprec_header->builtinPatchSize = cprec_glist.size;
    cprec_header->builtinPatchOffset = offset;

    if ((err = COS_FileWrite(cprec_refnum, *cprec_glist.data, cprec_glist.size)) != noErr) {
        return err;
    }
    offset += cprec_glist.size;

    if (cprec_tokenpatches) {
        cprec_glist.size = 0;
        CPrec_GenerateTokenStreamPatches();
        cprec_header->tokenStreamPatchSize = cprec_glist.size;
        cprec_header->tokenStreamPatchOffset = offset;

        if ((err = COS_FileWrite(cprec_refnum, *cprec_glist.data, cprec_glist.size)) != noErr) {
            return err;
        }
        offset += cprec_glist.size;
    }

    if ((err = COS_FileSetPos(cprec_refnum, 0)) != noErr) {
        return err;
    }

    if ((err = COS_FileWrite(cprec_refnum, cprec_header, sizeof(Header))) != noErr) {
        return err;
    }

    return noErr;
}

void PrecompilerWrite(void) {
    OSErr err;
    short strindex;
    CWFileSpec spec;
    char str[128];

    spec = cparamblkptr->sourcefile;
    if (CWGetPrecompiledHeaderSpec(cparamblkptr->context, &spec, precomp_target_str) != cwNoErr) {
        return;
    }

    strindex = 3;
    err = COS_FileNew(&spec, &cprec_refnum, copts.appltype, copts.headtype);
    if (err == noErr) {
        strindex = 4;
        err = CPrec_WriteFile();
    }
    CleanupPrecompiler();

    if (err != noErr) {
        CompilerGetCString(strindex, str);
        sprintf(string, str, err);
        CWReportMessage(cparamblkptr->context, NULL, string, NULL, messagetypeError, 0);
    } else {
        CWFileTime time = 0;
        CWSetModDate(cparamblkptr->context, &spec, &time, TRUE);
    }
}

static void CPrec_ReadData(SInt32 offset, void* buffer, SInt32 size) {
    if (COS_FileSetPos(cprec_refnum, offset) != noErr ||
        COS_FileRead(cprec_refnum, buffer, size) != noErr) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
}

static void CPrec_ReadRawBuffer(void) {
    UInt8* buffer;
    UInt8* work;
    UInt8* end;
    UInt32 size;
    int ch;

    if (!cprec_buffer) {
        size = cprec_header->rawsize + (cprec_header->rawsize >> 7) + 64;
        buffer = galloc(size);
        cprec_rawbuffer = buffer;

        work = buffer + size - cprec_header->rawdatasize;
        CPrec_ReadData(cprec_header->rawdataoffset, work, cprec_header->rawdatasize);
    } else {
        buffer = galloc(cprec_header->rawsize);
        cprec_rawbuffer = buffer;
        work = (UInt8*)cprec_buffer + cprec_header->rawdataoffset;
    }

    end = work + cprec_header->rawdatasize;

    while (work < end) {
        if ((ch = *(work++)) >= 0xE0) {
            ch -= 0xE0;
            do {
                *(buffer++) = 0;
            } while (--ch >= 0);
        } else {
            do {
                *(buffer++) = *(work++);
            } while (--ch >= 0);
        }
    }

    if (work != end || buffer != (UInt8*)cprec_rawbuffer + cprec_header->rawsize) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
}

static void CPrec_RelocateRawBuffer(void) {
    UInt8* patches;
    UInt32 offset;
    UInt32 dest;
    UInt8* patch;
    SInt32 count;
    union {
        UInt32 l;
        UInt8 b[4];
    } var;

    if ((count = cprec_header->compressedPatchCount)) {
        if (!cprec_buffer) {
            patches = lalloc(cprec_header->compressedPatchSize);
            CPrec_ReadData(
                cprec_header->compressedPatchOffset, patches, cprec_header->compressedPatchSize
            );
        } else {
            patches = (UInt8*)cprec_buffer + cprec_header->compressedPatchOffset;
        }

        offset = 0;
        patch = patches;
        dest = (UInt32)cprec_rawbuffer;
        do {
            if (!(*patch & 0x80)) {
                var.b[3] = patch[0];
                var.b[2] = patch[1];
                var.b[1] = patch[2];
                var.b[0] = patch[3];
                patch += 4;
                offset = var.l;
            } else {
                offset += (SInt8)(*patch << 1);
                patch++;
            }
            *((UInt32*)(dest + offset)) += dest;
        } while (--count > 0);

        freelheap();

        patches += cprec_header->compressedPatchSize;
        if (patch != patches) {
            CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
        }
    }
}

static void CPrec_RelocateBuiltins(void) {
    UInt32 count;
    void* builtin;
    UInt32* patches;
    UInt8* buffer;

    if (cprec_header->builtinPatchSize) {
        CPrec_SetupBuiltInArray();
        if (!cprec_buffer) {
            patches = lalloc(cprec_header->builtinPatchSize);
            CPrec_ReadData(
                cprec_header->builtinPatchOffset, patches, cprec_header->builtinPatchSize
            );
        } else {
            patches = (UInt32*)((UInt8*)cprec_buffer + cprec_header->builtinPatchOffset);
        }

        buffer = cprec_rawbuffer;
        while (1) {
            if (!(count = *(patches++))) {
                break;
            }

            builtin = cprec_builtin_array[*(patches++)];
            do {
                *((void**)(buffer + *(patches++))) = builtin;
            } while (--count);
        }
    }
}

static void CPrec_RelocateTokenStreams(void) {
    TStreamElement* tokens;
    UInt32 i;
    UInt32 count;
    CPrepFileInfo* file;
    SInt32 pos;
    UInt32* patches;
    UInt8* buffer;
    UInt32 offset;

    if (cprec_header->tokenStreamPatchSize) {
        CPrep_GetPrepPos(&file, &pos);

        if (!cprec_buffer) {
            patches = lalloc(cprec_header->tokenStreamPatchSize);
            CPrec_ReadData(
                cprec_header->tokenStreamPatchOffset, patches, cprec_header->tokenStreamPatchSize
            );
        } else {
            patches = (UInt32*)((UInt8*)cprec_buffer + cprec_header->tokenStreamPatchOffset);
        }

        buffer = cprec_rawbuffer;
        while (1) {
            if (!(offset = *(patches++))) {
                break;
            }

            tokens = (TStreamElement*)(buffer + offset);
            count = *(patches++);
            for (i = 0; i < count; i++, tokens++) {
                tokens->tokenfile = file;
                tokens->tokenoffset = pos;
            }
        }
    }
}

static void CPrec_RelocateMacroTable(void) {
    int i;
    int j;
    int count;
    UInt8* buffer;
    Macro** prec_table;
    Macro** table;
    Macro* macro;
    UInt32 offset;

    buffer = cprec_rawbuffer;
    prec_table = cprec_header->macrotable;
    table = macrohashtable;

    i = 0;
    do {
        for (macro = *table; macro; macro = macro->next) {
            macro->name = GetHashNameNodeExport(macro->name->name);
            count = macro->nargs & 0x7FFF;
            for (j = 1; j < count; j++) {
                macro->names[j - 1] = GetHashNameNodeExport(macro->names[j - 1]->name);
            }
        }

        if ((offset = (UInt32)*prec_table)) {
            if (*table) {
                macro = (Macro*)(buffer + offset);
                while (macro->next) {
                    macro = macro->next;
                }
                macro->next = *table;
            }
            *table = (Macro*)(buffer + offset);
        }

        prec_table++;
        table++;
    } while (++i < 0x800);
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
void PrecompilerRead(short refnum, void* buffer) {
    Header* header;
    SInt32 offset;
    UInt8* base;
    SInt32 index;
    SInt32 bucket;
    HashNameNode* name;
    SInt32* offsets;
    HashNameNode** hashEntries;
    StaticData* record;

    cprec_refnum = refnum;
    cprec_buffer = buffer;
    CPrep_RemoveSpecialMacros();
    if (!CScope_IsEmptySymTable()) {
        CError_ErrorTerm(ERR_ILLEGAL_USE_PRECOMPILED_HEADER);
    }

    if (cprec_buffer == NULL) {
        cprec_header = galloc(sizeof(Header));
        header = cprec_header;
        if (COS_FileSetPos(cprec_refnum, 0) != 0 ||
            COS_FileRead(cprec_refnum, header, sizeof(*header)) != 0) {
            CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
        }
    } else {
        header = cprec_buffer;
        cprec_header = header;
    }

    if (cprec_header->magic != 0xBEEFFACE) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
    #if VERSION < VERSION_GC_2_0
    if (cprec_header->version != 1047) {
#else
    if (cprec_header->version != 5145) {
#endif
        CError_ErrorTerm(ERR_ILLEGAL_PRECOMPILED_HEADER_VERSION);
    }
    if (cprec_header->target != 2) {
        CError_ErrorTerm(ERR_ILLEGAL_PRECOMPILED_HEADER_COMPILER_FLAGS_TARGET);
    }

    copts.checkprecompflags = cprec_header->check_header_flags;
    CPrec_ReadRawBuffer();
    CPrec_RelocateRawBuffer();
    CPrec_RelocateBuiltins();

    hashEntries = name_hash_nodes;
    offsets = (SInt32*)cprec_header->nametable;
    base = cprec_rawbuffer;
    index = 0;
    do {
        offset = *offsets;
        if (offset != 0) {
            *hashEntries = (HashNameNode*)(base + offset);
        } else {
            *hashEntries = NULL;
        }
        offsets++;
        hashEntries++;
    } while (++index < 0x800);

    bucket = 0;
    do {
        name = name_hash_nodes[bucket];
        if (name) {
            do {
                name->id = -1;
                name = name->next;
            } while (name != NULL);
        }
    } while (++bucket < 0x800);

    CPrec_RelocateMacroTable();
    CPrec_RelocateTokenStreams();

    CError_ASSERT(5048, cscope_root->is_hash != 0);
    {
        SInt32 namespaceOffset;
        UInt8* namespaceBase;
        SInt32 namespaceIndex;
        SInt32* namespaceOffsets;
        NameSpaceName** namespaceEntries;

        cscope_root->names = cprec_header->root_names, namespaceEntries = cscope_root->data.hash;
        namespaceOffsets = (SInt32*)cprec_header->root_nametable;
        namespaceBase = cprec_rawbuffer;
        namespaceIndex = 0;
        do {
            namespaceOffset = *namespaceOffsets;
            if (namespaceOffset != 0) {
                *namespaceEntries = (NameSpaceName*)(namespaceBase + namespaceOffset);
            } else {
                *namespaceEntries = NULL;
            }
            namespaceOffsets++;
            namespaceEntries++;
        } while (++namespaceIndex < 0x400);
    }

    if (!cprec_header->usings) {
        cscope_root->usings = NULL;
    } else {
        cscope_root->usings =
            (struct NameSpaceList*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->usings);
    }

    if (!cprec_header->ctempl_templates) {
        ctempl_templates = NULL;
    } else {
        ctempl_templates =
            (TemplClass*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->ctempl_templates);
    }

    if (!cprec_header->ctempl_templatefuncs) {
        ctempl_templatefuncs = NULL;
    } else {
        ctempl_templatefuncs = (TemplateFunction*)((UInt8*)cprec_rawbuffer +
                                                   (UInt32)cprec_header->ctempl_templatefuncs);
    }

    if (!cprec_header->csom_stubs) {
        csom_stubs = NULL;
    } else {
        csom_stubs = (CSOMStub*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->csom_stubs);
    }

    if (!cprec_header->cprec_staticdata) {
        cprec_staticdata = NULL;
    } else {
        cprec_staticdata =
            (StaticData*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->cprec_staticdata);
    }

    if (!cprec_header->callbackactions) {
        callbackactions = NULL;
    } else {
        callbackactions =
            (CallbackAction*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->callbackactions);
    }

    if (!cprec_header->cobjc_type_class) {
        cobjc_type_class = NULL;
    } else {
        cobjc_type_class =
            (Type*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->cobjc_type_class);
    }

    if (!cprec_header->cobjc_type_id) {
        cobjc_type_id = NULL;
    } else {
        cobjc_type_id = (Type*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->cobjc_type_id);
    }

    if (!cprec_header->cobjc_type_sel) {
        cobjc_type_sel = NULL;
    } else {
        cobjc_type_sel = (Type*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->cobjc_type_sel);
    }

    if (!cprec_header->cobjc_selhashtable) {
        cobjc_selhashtable = NULL;
    } else {
        cobjc_selhashtable = (struct ObjCSelector**)((UInt8*)cprec_rawbuffer +
                                                     (UInt32)cprec_header->cobjc_selhashtable);
    }

    if (!cprec_header->cobjc_classdefs) {
        cobjc_classdefs = NULL;
    } else {
        cobjc_classdefs =
            (struct BClassList*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->cobjc_classdefs);
    }

    if (!cprec_header->cobjc_protocols) {
        cobjc_protocols = NULL;
    } else {
        cobjc_protocols =
            (struct ObjCProtocol*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->cobjc_protocols);
    }

    if (!cprec_header->init_expressions) {
        init_expressions = NULL;
    } else {
        init_expressions =
            (struct InitExpr*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->init_expressions);
    }

    if (!cprec_header->cinline_tactionlist) {
        cinline_tactionlist = NULL;
    } else {
        cinline_tactionlist =
            (CI_Action*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->cinline_tactionlist);
    }

    CParser_SetUniqueID(cprec_header->uniqueID);
    cobjc_selrefcount = cprec_header->cobjc_selrefcount;
    cobjc_classrefcount = cprec_header->cobjc_classrefcount;
    cobjc_stringcount = cprec_header->cobjc_stringcount;

    cprec_refnum = 0;
    if (cprec_glist.data != NULL) {
        FreeGList(&cprec_glist);
    }
    cscope_current = cscope_root;
    if (!CParser_ReInitRuntimeObjects(TRUE)) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
    CPrep_InsertSpecialMacros();

    if (cparamblkptr->precompile != 1) {
        record = cprec_staticdata;
        cprec_staticdata = NULL;
        for (; record != NULL; record = record->next) {
            CInit_DeclareData(record->object, record->buffer, record->links, record->size);
        }
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#pragma pack(2)
