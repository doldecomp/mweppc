#include "compiler/BE_symbol.h"
#include "compiler/BE_elf.h"
#include "compiler/CError.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/ELF_Endian.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/RegisterInfo.h"
#include "compiler/TOC.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include <string.h>

BE_SymbolTable TheSymbolTable;

static BE_SymNode* last_section_symbol;

BE_SymNode* First(void) {
    return TheSymbolTable.current = TheSymbolTable.first;
}

BE_SymNode* Next(void) {
    CError_ASSERT(58, TheSymbolTable.current != NULL);
    return TheSymbolTable.current = TheSymbolTable.current->next;
}

BE_SymNode* AddSymbol(
    BE_SymNode* node,
    SInt32 size,
    SInt32 qual,
    SInt16 sclass,
    SInt32 alignment,
    UInt8 flags,
    ObjGenSection* section
) {
    UInt8 binding;
    UInt8 linkageKind;
    Boolean exported;

    linkageKind = 0;
    exported = FALSE;
    if (qual & Q_IMPLICIT_WEAK) {
        binding = STB_WEAK;
        linkageKind = 13;
    } else if (qual & Q_WEAK) {
        binding = STB_WEAK;
        linkageKind = 14;
    } else if (sclass == TK_STATIC) {
        binding = STB_LOCAL;
    } else {
        binding = STB_GLOBAL;
    }
    if (binding == STB_GLOBAL && (flags & OBJECT_EXPORT)) {
        exported = TRUE;
    }
    node->elf.st_info = ELF32_ST_INFO(binding, STT_OBJECT);
    node->elf.st_size = size;
    node->elf.st_shndx = SHN_UNDEF;
    node->section = section;
    node->alignment = alignment;
    node->linkageKind = linkageKind;
    node->linkageFlags = 0;
    if (copts.force_active || exported) {
        node->linkageFlags |= 8;
    }
    return node;
}

BE_SymNode* AddObject(Object* obj, SInt32 size, ObjGenSection* section) {
    BE_SymNode* node;
    SInt16 sclass;

    if (obj->type->type == TYPEFUNC) {
        return AddFunction(obj, size, section);
    }
    node = MakeSymbolTableEntry(obj);
    if (CParser_HasInternalLinkage(obj)) {
        sclass = TK_STATIC;
    } else {
        sclass = TK_EXTERN;
    }
    return AddSymbol(
        node,
        size,
        obj->qual,
        sclass,
        CMach_AllocationAlignment(obj->type, obj->qual),
        obj->flags,
        section
    );
}

BE_SymNode* AddFunction(Object* obj, SInt32 size, ObjGenSection* section) {
    UInt8 binding;
    UInt8 linkageKind;
    Boolean exported;
    BE_SymNode* node;

    linkageKind = 0;
    exported = FALSE;
    if (CParser_HasInternalLinkage(obj)) {
        binding = STB_LOCAL;
    } else {
        binding = STB_GLOBAL;
    }
    if (obj->qual & Q_IMPLICIT_WEAK) {
        binding = STB_WEAK;
        linkageKind = 13;
    } else if (obj->qual & Q_WEAK) {
        binding = STB_WEAK;
        linkageKind = 14;
    }
    if (binding == STB_GLOBAL && (obj->flags & OBJECT_EXPORT)) {
        exported = TRUE;
    }
    node = MakeSymbolTableEntry(obj);
    node->elf.st_size = size;
    node->elf.st_shndx = SHN_UNDEF;
    node->section = section;
    node->alignment = copts.function_align;
    node->linkageKind = linkageKind;
    node->linkageFlags = 0;
    if (copts.force_active || exported) {
        node->linkageFlags |= 8;
    }
    node->elf.st_info = ELF32_ST_INFO(binding, STT_FUNC);
    return node;
}

BE_SymNode* AddEntry(Object* obj, ObjGenSection* section) {
    UInt8 binding;
    Boolean exported;
    BE_SymNode* node;

    exported = FALSE;
    if (CParser_HasInternalLinkage(obj)) {
        binding = STB_LOCAL;
    } else {
        binding = STB_GLOBAL;
    }
    if (binding == STB_GLOBAL && (obj->flags & OBJECT_EXPORT)) {
        exported = TRUE;
    }
    node = MakeSymbolTableEntry(obj);
    node->elf.st_info = ELF32_ST_INFO(binding, STT_NOTYPE);
    node->elf.st_size = 0;
    node->elf.st_shndx = SHN_UNDEF;
    node->section = section;
    node->alignment = 4;
    node->linkageKind = 0;
    node->linkageFlags = 0;
    if (copts.force_active || exported) {
        node->linkageFlags |= 8;
    }
    return node;
}

#if VERSION >= VERSION_GC_2_7
static inline UInt32 BE_SymbolHash(HashNameNode* name) {
    UInt32 hash;
    size_t i;
    size_t length;

    length = strlen(name->name);
    hash = length;
    for (i = 0; i < length; i++) {
        hash = ((hash << 5) ^ (hash >> 27)) ^ name->name[i];
    }
    return (hash ^ (hash >> 10) ^ (hash >> 20)) & 0xFFF;
}
#endif

BE_SymNode* AddSymbolTableEntry(HashNameNode* name) {
    BE_SymNode* node;
#if VERSION >= VERSION_GC_2_7
    UInt32 hash;

    hash = BE_SymbolHash(name);
#endif

    node = galloc(sizeof(BE_SymNode));
    memset(node, 0, sizeof(BE_SymNode));
    node->name = name;
#if VERSION >= VERSION_GC_2_7
    node->hashNext = TheSymbolTable.hash[hash];
    TheSymbolTable.hash[hash] = node;
#endif
    if (TheSymbolTable.first != NULL) {
        TheSymbolTable.current->next = node;
    } else {
        TheSymbolTable.first = node;
    }
    TheSymbolTable.current = node;
    return node;
}

void InitELFSymbols(void) {
    memset(&TheSymbolTable, 0, sizeof(TheSymbolTable));
    last_section_symbol = NULL;
}

BE_SymNode* InorderFirst(void) {
    return TheSymbolTable.inorder;
}

static inline BE_SymNode* FindSymbolTableEntry(HashNameNode* name, SInt16 sclass) {
    BE_SymNode* node;
#if VERSION >= VERSION_GC_2_7
    for (node = TheSymbolTable.hash[BE_SymbolHash(name)]; node != NULL; node = node->hashNext) {
#else
    for (node = TheSymbolTable.first; node != NULL; node = node->next) {
#endif
        if (name == node->name && ((sclass == TK_STATIC) == (node->sclass == TK_STATIC))) {
            return node;
        }
    }
    return NULL;
}

#if VERSION >= VERSION_GC_2_7
BE_SymNode* MakeNamedSymbolTableEntry(HashNameNode* name, SInt16 sclass)
#else
static inline BE_SymNode* MakeNamedSymbolTableEntry(HashNameNode* name, SInt16 sclass)
#endif
{
    BE_SymNode* node;
    UInt8 binding;

    node = FindSymbolTableEntry(name, sclass);
    if (node != NULL) {
        return node;
    }
    if (sclass == TK_STATIC) {
        binding = STB_LOCAL;
    } else {
        binding = STB_GLOBAL;
    }
#if VERSION >= VERSION_GC_2_7
    node = AddSymbolTableEntry(name);
#else
    node = galloc(sizeof(BE_SymNode));
    memset(node, 0, sizeof(BE_SymNode));
    node->name = name;
    if (TheSymbolTable.first != NULL) {
        TheSymbolTable.current->next = node;
    } else {
        TheSymbolTable.first = node;
    }
    TheSymbolTable.current = node;
#endif
    node->elf.st_info = ELF32_ST_INFO(binding, STT_NOTYPE);
    node->sclass = sclass;
    node->section = ELFfile.null_section;
    return node;
}

BE_SymNode* MakeSymbolTableEntry(Object* obj) {
    HashNameNode* name;
    int sclass;

    name = CMangler_GetLinkName(obj);
    if (CParser_HasInternalLinkage(obj)) {
        sclass = TK_STATIC;
    } else {
        sclass = TK_EXTERN;
    }
    return MakeNamedSymbolTableEntry(name, sclass);
}

BE_SymNode* elf_declare_symbol(ObjGenSection* section) {
    BE_SymNode* node;
    BE_SymNode* next;

    if (section->sym != NULL) {
        return section->sym;
    }
    node = galloc(sizeof(BE_SymNode));
    memset(node, 0, sizeof(BE_SymNode));
    node->name = GetHashNameNode(section->name);
    node->sclass = TK_STATIC;
    if (last_section_symbol == NULL) {
        last_section_symbol = TheSymbolTable.first;
    }
    next = last_section_symbol->next;
    last_section_symbol->next = node;
    node->next = next;
    last_section_symbol = node;
    section->sym = node;
    return node;
}

BE_SymNode* CreateDataPoolAnchorSymbol(ObjGenSection* section, Object* anchor) {
    char* buffer;
    Object* obj;
    BE_SymGroup* group;
    BE_SymNode* node;

    section->symbolLink->firstObject = anchor;
    buffer = galloc(strlen(section->name) + 5);
    strcpy(buffer, "..");
    strcat(buffer, section->name);
    strcat(buffer, ".0");
    obj = galloc(sizeof(Object));
    section->symbolLink->object = obj;
    memclrw(obj, sizeof(Object));
    obj->type = (Type*)&void_ptr;
    obj->otype = OT_OBJECT;
    obj->name = GetHashNameNodeExport(buffer);
    obj->datatype = DDATA;
    obj->sclass = TK_STATIC;
    obj->section = anchor->section;
    obj->u.var.info = CodeGen_GetNewVarInfo();
    obj->u.var.uid = 0;
    group = galloc(sizeof(BE_SymGroup));
    memclrw(group, sizeof(BE_SymGroup));
    group->limit = 3;
    section->symbolLink->tail = &section->symbolLink->groups;
    *section->symbolLink->tail = group;
    section->symbolLink->tail = &group->next;
    node = AddSymbolTableEntry(obj->name);
    node->elf.st_info = ELF32_ST_INFO(STB_LOCAL, STT_NOTYPE);
    node->elf.st_shndx = SHN_UNDEF;
    node->section = section;
    node->alignment = 1;
    node->sclass = TK_STATIC;
    node->elf.st_other |= 0x40;
    node->linkageFlags |= 0x10;
    return node;
}

static BE_SymGroup* FindOrAddObjectExtension(ObjGenSymbolLink* link, SInt32 offset) {
    BE_SymGroup* group;
    SInt32 base;

    for (group = link->groups; group != NULL; group = group->next) {
        if (group->base - 0x8000 <= offset && offset < group->base + 0x8000) {
            return group;
        }
    }
    for (base = 0x10000;; base += 0x10000) {
        if (base - 0x8000 <= offset && offset < base + 0x8000) {
            group = galloc(sizeof(BE_SymGroup));
            memclrw(group, sizeof(BE_SymGroup));
            group->base = base;
            group->limit = 4;
            *link->tail = group;
            link->tail = &group->next;
            return group;
        }
    }
}

BE_SymGroup* GetObjectExtension(Object* obj) {
    BE_SymNode* node;
    SInt32 offset;
    BE_SymGroup* group;

    node = MakeSymbolTableEntry(obj);
    offset = node->elf.st_value;
    for (group = node->section->symbolLink->groups; group != NULL; group = group->next) {
        if (group->base - 0x8000 <= offset && offset < group->base + 0x8000) {
            return group;
        }
    }
    CError_FATAL(471);
    return NULL;
}

static inline BE_SymGroup* FindUsedObjectExtension(ObjGenSymbolLink* link, SInt32 offset) {
    BE_SymGroup* group;
    for (group = link->groups; group != NULL; group = group->next) {
        if (group->base - 0x8000 <= offset && offset < group->base + 0x8000) {
            if (group->full) {
                return group;
            }
            return NULL;
        }
    }
    return NULL;
}

SInt32 GetPooledObjectOffset(Object* obj) {
    BE_SymNode* node;
    SInt32 offset;
    BE_SymGroup* group;

    node = MakeSymbolTableEntry(obj);
    offset = node->elf.st_value;
    if (offset == (short)offset) {
        return offset;
    }
    group = FindUsedObjectExtension(node->section->symbolLink, offset);
    if (group == NULL) {
        CError_FATAL(489);
    }
    return offset - group->base;
}

int GetPooledObjectRegister(Object* obj) {
    BE_SymNode* node;
    SInt32 offset;
    BE_SymGroup* group;

    node = MakeSymbolTableEntry(obj);
    offset = node->elf.st_value;
    if (offset == (short)offset) {
        return Registers_GetVarInfo(obj->toc)->reg;
    }
    group = FindUsedObjectExtension(node->section->symbolLink, offset);
    if (group == NULL) {
        return 0;
    }
    return group->reg;
}

Object* GetDataPoolRoot(Object* obj) {
    BE_SymNode* node;
    ObjGenSymbolLink* link;
    ObjGenSymbolLink* pool;

    node = MakeSymbolTableEntry(obj);
    if ((obj->qual & Q_TENTATIVE) && node->elf.st_size == 0) {
        ObjGen_DeclareData(obj, NULL, NULL, obj->type->size);
    }
    pool = link = node->section->symbolLink;

    if (copts.pool_data &&
        link != NULL &&
        link->symbol != NULL &&
        !(obj->qual & Q_IMPLICIT_WEAK) &&
        !(obj->qual & Q_WEAK) &&
        !(obj->datatype == DFUNC || obj->datatype == DVFUNC || obj->datatype == DINLINEFUNC) &&
        !is_16bitdata(obj) &&
        (UInt32)node->elf.st_size <
            FindOrAddObjectExtension(pool, node->elf.st_value)->base + 0x8000U) {
        return pool->object;
    }
    return NULL;
}

Boolean NeedsXREF(Object* obj) {
    BE_SymNode* node;
    ObjGenSection* section;

    node = MakeSymbolTableEntry(obj);
    section = node->section;
    CError_ASSERT(547, section != NULL);
    if (is_pooled_object(obj)) {
        section->symbolLink->used = TRUE;
        return FALSE;
    }
    return TRUE;
}
