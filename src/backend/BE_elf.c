#include "compiler/BE_elf.h"
#include "compiler/BE_symbol.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/DWARF.h"
#include "compiler/ELF_Endian.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/tokens.h"
#include "driver/CodeGenOptPPC.h"
#include "driver/COSToolsCLT.h"
#include "driver/oslib.h"
#include <string.h>

// Built -inline auto,deferred: the binary holds its functions in reverse order.

ElfFile ELFfile;
GList elfmodule;
Elf32_Ehdr ElfHeader;
ObjGenRelocation* allrelocations;
UInt16 SpecificNum;

// write_codewarrior_version_record's two-byte processor field, copied byte by byte.
typedef union U16Bytes { // NOTE: invented name
    UInt16 w;
    UInt8 b[2];
} U16Bytes;

// NOTE: invented names: elfBigEndian, symbol_order_count, padding_buffer, ordered_section_index,
// padding_data, max_padding_size (Wii's BE_elf has these statics unnamed)
// (the original's statics; global while a lifted function of this unit still uses them)
// SectionAlign copies its zeros from padding_data: padding_buffer until a pad needs more.
UInt8 elfBigEndian;
SInt32 symbol_order_count;
UInt8 padding_buffer[128];
ObjGenSection** ordered_section_index;
void* padding_data = padding_buffer;
SInt32 max_padding_size = 128;

// TODO: COMMONs of the GC compilers, defined here tentatively: ObjGen's ELF_Setup resets both,
// so they are probably its. Neither name is in COMMON hash order at its address (previous_ps_ID
// is Wii's name; interrupt_generation_records is invented)
SInt32 previous_ps_ID;
InterruptGenerationRecord* interrupt_generation_records;

void SectionAlign(GList* list, SInt32 alignment) {
    int size;

    size = list->size;
    size = (size + alignment - 1) & ~(alignment - 1);
    size -= list->size;
    if (size > max_padding_size) {
        padding_data = galloc(size);
        memset(padding_data, 0, size);
        max_padding_size = size;
    }
    AppendGListData(list, padding_data, size);
}

// Chains the symbols in ELF order behind a new null symbol (TheSymbolTable.inorder) and numbers
// them.
static void assign_symbol_order(void) { // NOTE: invented name (a static Wii's BE_elf has unnamed)
    BE_SymNode* tail;
    BE_SymNode* sym;
    SInt32 count = 0;

    tail = galloc(sizeof(BE_SymNode));
    memset(tail, 0, sizeof(BE_SymNode));
    tail->elf.st_shndx = SHN_UNDEF;
    tail->section = ELFfile.null_section;
    TheSymbolTable.inorder = tail;
    for (sym = First(); sym != NULL; sym = Next()) {
        // the locals first, then the globals and weaks
        if (ELF32_ST_BIND(sym->elf.st_info) == STB_LOCAL && (sym->elf.st_other & 0x40) == 0) {
            tail->orderNext = sym;
            tail = sym;
            count++;
            sym->order = count;
        }
    }
    symbol_order_count = count;
    for (sym = First(); sym != NULL; sym = Next()) {
        if (ELF32_ST_BIND(sym->elf.st_info) > STB_LOCAL) {
            tail->orderNext = sym;
            tail = sym;
            count++;
            sym->order = count;
        }
    }
}

// The attributes record already made for this combination, or NULL.
// NOTE: invented name (a static Wii's BE_elf has unnamed)
static SectionSymbolAttributes* find_section_symbol_attributes(
    ObjGenSection* section,
    UInt8 dataMode,
    UInt8 codeMode,
    ObjGenSection* linked,
    ObjGenSection* auxiliary
) {
    SectionSymbolAttributes* entry;
    for (entry = ELFfile.section_symbol_attributes; entry; entry = entry->next) {
        if (entry->target != section) {
            continue;
        }
        if (entry->dataMode != dataMode) {
            continue;
        }
        if (entry->codeMode != codeMode) {
            continue;
        }
        if (entry->linkedSymbol != linked) {
            continue;
        }
        if (entry->auxiliary != auxiliary) {
            continue;
        }
        break;
    }
    return entry;
}

SectionSymbolAttributes* Initialize_Specific(
    ObjGenSection* section,
    UInt8 dataMode,
    UInt8 codeMode,
    ObjGenSection* linked,
    ObjGenSection* auxiliary
) {
    SectionSymbolAttributes* record;
    SectionSymbolAttributes* tail;

    record = find_section_symbol_attributes(section, dataMode, codeMode, linked, auxiliary);
    if (record != NULL) {
        return record;
    }

    record = galloc(sizeof(SectionSymbolAttributes));
    memset(record, 0, sizeof(SectionSymbolAttributes));

    record->target = section;
    record->dataMode = dataMode;
    record->codeMode = codeMode;
    record->linkedSymbol = linked;
    record->auxiliary = auxiliary;

    if (SpecificNum == 0x7FFF) {
        PPCError_Error(PPCERR_TOO_MANY_SECTION_PRAGMAS, 0x7fff);
    }

    record->index = SpecificNum;
    SpecificNum++;
    ELFfile.section_symbol_attribute_count++;

    tail = ELFfile.section_symbol_attributes;
    if (tail == NULL) {
        ELFfile.section_symbol_attributes = record;
    } else {
        SectionSymbolAttributes* last = tail;
        while (last->next != NULL) {
            last = last->next;
        }
        last->next = record;
    }
    return record;
}

ObjGenSection* InitElfSection(
    char* name,
    UInt8 kind,
    SInt8 align,
    SInt32 initialSize,
    SectionRec* rec,
    UInt32 permissions,
    struct ObjGenSymbolLink* symbolLink
) {
    ObjGenSection** list;
    ObjGenSection* section;

    list = NULL;
    section = galloc(sizeof(ObjGenSection));
    memset(section, 0, sizeof(ObjGenSection));
    ELFfile.sectionHeaderCount++;
    section->name = name;
    section->kind = kind;
    section->initialSize = initialSize;
    if (rec != NULL && kind != SectionKind_BSS) {
        if (rec->typebits & 1) {
            copts.textSection = section;
        }
        if (rec->typebits & 2) {
            copts.dataSection = section;
        }
        if (rec->typebits & 4) {
            copts.constSection = section;
        }
        if (rec->typebits & 8) {
            copts.sdataSection = section;
        }
        if (rec->typebits & 16) {
            copts.sconstSection = section;
        }
    }
    section->symbolLink = symbolLink;
    section->shdr.sh_type = (kind == SectionKind_BSS) ? SHT_NOBITS : SHT_PROGBITS;
    section->permissions = permissions;
    section->shdr.sh_flags = permissions;
    section->shdr.sh_addralign = align;
    if (section->initialSize && InitGList(&section->buffer, section->initialSize)) {
        CError_NoMem();
    }

    switch (kind) {
        case SectionKind_Null:
            CError_ASSERT(273, ELFfile.null_section == NULL);
            section->shdr.sh_type = SHT_NULL;
            section->shdr.sh_link = 0;
            list = &ELFfile.null_section;
            break;
        case SectionKind_Content:
            break;
        case SectionKind_Ctors:
            CError_ASSERT(282, ELFfile.ctors_section == NULL);
            list = &ELFfile.ctors_section;
            break;
        case SectionKind_Dtors:
            CError_ASSERT(286, ELFfile.dtors_section == NULL);
            list = &ELFfile.dtors_section;
            break;
        case SectionKind_Extab:
            CError_ASSERT(290, ELFfile.extab_section == NULL);
            list = &ELFfile.extab_section;
            break;
        case SectionKind_ExtabIndex:
            CError_ASSERT(294, ELFfile.extabindex_section == NULL);
            list = &ELFfile.extabindex_section;
            break;
        case SectionKind_BSS:
            break;
        case SectionKind_Debug:
            CError_ASSERT(298, ELFfile.dwarf_info_section == NULL);
            list = &ELFfile.dwarf_info_section;
            section->shdr.sh_entsize = 1;
            break;
        case SectionKind_Line:
            CError_ASSERT(303, ELFfile.dwarf_line_section == NULL);
            list = &ELFfile.dwarf_line_section;
            section->shdr.sh_entsize = 1;
            break;
        case SectionKind_Rel:
            section->shdr.sh_type = SHT_REL;
            section->shdr.sh_entsize = 8;
            list = &ELFfile.rela_sections;
            break;
        case SectionKind_Rela:
            section->shdr.sh_type = SHT_RELA;
            section->shdr.sh_entsize = 0xc;
            list = &ELFfile.rela_sections;
            break;
        case SectionKind_Symtab:
            CError_ASSERT(318, ELFfile.symtab_section == NULL);
            section->shdr.sh_type = SHT_SYMTAB;
            section->shdr.sh_entsize = 0x10;
            list = &ELFfile.symtab_section;
            break;
        case SectionKind_Strtab:
            CError_ASSERT(324, ELFfile.symbol_string_table_section == NULL);
            section->shdr.sh_type = SHT_STRTAB;
            section->shdr.sh_entsize = 1;
            list = &ELFfile.symbol_string_table_section;
            break;
        case SectionKind_Shstrtab:
            CError_ASSERT(330, ELFfile.shstrtab_section == NULL);
            section->shdr.sh_type = SHT_STRTAB;
            section->shdr.sh_entsize = 1;
            list = &ELFfile.shstrtab_section;
            break;
        case SectionKind_Comment:
            CError_ASSERT(336, ELFfile.comment_section == NULL);
            section->shdr.sh_entsize = 1;
            list = &ELFfile.comment_section;
            break;
        case SectionKind_Common:
            CError_ASSERT(341, ELFfile.common_section == NULL);
            list = &ELFfile.common_section;
            break;
        case SectionKind_Abs:
            CError_ASSERT(345, ELFfile.abs_section == NULL);
            list = &ELFfile.abs_section;
            break;
        case SectionKind_MWCATS:
            // .mwcats' section type (CodeWarrior's own)
            section->shdr.sh_type = 0xca2a82c2;
            section->shdr.sh_entsize = 1;
            break;
        default:
            CError_FATAL(356);
    }

    if (ELFfile.section_list == NULL) {
        ELFfile.section_list = section;
    } else {
        ObjGenSection* tail;
        for (tail = ELFfile.section_list; tail->next; tail = tail->next) {}
        tail->next = section;
    }
    if (list != NULL) {
        if (*list == NULL) {
            *list = section;
        } else {
            ObjGenSection* tail;
            for (tail = *list; tail->nextInGroup; tail = tail->nextInGroup) {}
            tail->nextInGroup = section;
        }
    }
    return section;
}

ObjGenSection* GetSectionByName(const char* name, SInt32 context, UInt8 flags) {
    ObjGenSection* section;

    if (!name || !*name) {
        return NULL;
    }
    for (section = ELFfile.section_list; section; section = section->next) {
        if (section->context == context &&
            strcmp(section->name, name) == 0 &&
            flags == section->flags) {
            return section;
        }
    }
    return NULL;
}

ObjGenSection* DoElfContentSection(
    SectionRec* rec,
    short kind,
    SInt8 align,
    SInt32 size,
    int relocationSize,
    UInt32 permissions,
    char allocateData,
    char createEntry
) {
    ObjGenSection* section;
    ObjGenSection* relocations;
    char* relocationName;
    ObjGenSection* linked = NULL;
    ObjGenSection* auxiliary = NULL;
    struct ObjGenSymbolLink* symbolLink = NULL;

    relocationName = galloc(strlen(rec->sectionName) + strlen(".rela") + 1);
    strcpy(relocationName, ".rela");
    strcat(relocationName, rec->sectionName);
    if (allocateData) {
        symbolLink = galloc(sizeof(*symbolLink));
        memset(symbolLink, 0, sizeof(*symbolLink));
    }
    section = InitElfSection(rec->sectionName, kind, align, size, rec, permissions, symbolLink);
    relocations =
        InitElfSection(relocationName, SectionKind_Rela, 4, relocationSize, NULL, 0, NULL);
    section->relocations = relocations;
    relocations->relocations = section;
    if (rec->linkedSectionName) {
        linked = GetSectionByName(rec->linkedSectionName, 0, 0);
        if (!linked) {
            if (allocateData) {
                symbolLink = galloc(sizeof(*symbolLink));
                memset(symbolLink, 0, sizeof(*symbolLink));
            }
            linked = InitElfSection(
                rec->linkedSectionName,
                SectionKind_BSS,
                align,
                size,
                rec,
                permissions & ~0x40000004, // not executable or compressed
                symbolLink
            );
        }
        if (linked == ELFfile.common_section) {
            linked = ELFfile.data_section_linked_symbol;
            auxiliary = ELFfile.common_section;
        }
    }
    if (createEntry) {
        section->header =
            Initialize_Specific(section, rec->dataMode, rec->codeMode, linked, auxiliary);
    }
    return section;
}

SectionRec* Initialize_Input(
    char* name, char* linkedName, UInt8 dataMode, UInt8 codeMode, UInt8 typebits
) {
    SectionRec* rec;
    rec = galloc(sizeof(SectionRec));
    memset(rec, 0, sizeof(SectionRec));
    rec->sectionName = name;
    rec->linkedSectionName = linkedName;
    rec->dataMode = dataMode;
    rec->codeMode = codeMode;
    rec->typebits = typebits;
    return rec;
}

ObjGenSection* CloneSection(ObjGenSection* input, SInt32 context, UInt8 flags) {
    ObjGenSection* relocations = NULL;
    struct ObjGenSymbolLink* symbolLink = NULL;
    UInt32 permissions = input->permissions;
    SectionRec* rec;
    ObjGenSection* section;

    rec = Initialize_Input(input->name, NULL, input->header->dataMode, input->header->codeMode, 0);
    if (input->symbolLink != NULL) {
        symbolLink = galloc(sizeof(*symbolLink));
        memset(symbolLink, 0, sizeof(*symbolLink));
    }
    if (flags) {
        // compressed, allocated, executable
        permissions = 0x40000006;
    }
    (section = InitElfSection(rec->sectionName, input->kind, 4, 200, rec, permissions, symbolLink))
        ->context = context;
    section->flags = flags;
    if (input->relocations != NULL) {
        relocations =
            InitElfSection(input->relocations->name, SectionKind_Rela, 4, 100, NULL, 0, NULL);
        relocations->relocations = section;
    }
    section->relocations = relocations;
    section->header = Initialize_Specific(
        section,
        input->header->dataMode,
        input->header->codeMode,
        input->header->linkedSymbol,
        input->header->auxiliary
    );
    return section;
}

void DoElfSystemSection(char* name, UInt8 kind, SInt8 align, SInt32 initialSize) {
    InitElfSection(name, kind, align, initialSize, NULL, 0, NULL);
}

void DoParseDeclSpec(char* name, DeclInfo* declinfo) {
    InterruptGenerationRecord* record = NULL;
    ObjGenSection* section;
    UInt16 targetIndex;

    if (declinfo->section != 0) {
        CError_FATAL(495);
    } else {
        record = galloc(sizeof(InterruptGenerationRecord));
        memclrw(record, sizeof(InterruptGenerationRecord));
        record->next = interrupt_generation_records;
        interrupt_generation_records = record;
        record->id = declinfo->section = --previous_ps_ID;
        record->kind = InterruptRecord_DeclSpec;
    }

    for (section = ELFfile.section_list->next; section != NULL; section = section->next) {
        if (section->kind == SectionKind_BSS ||
            section->kind == SectionKind_Common ||
            section->kind == SectionKind_Content ||
            section->kind == SectionKind_Ctors ||
            section->kind == SectionKind_Dtors) {
            if (strcmp(name, section->name) == 0) {
                if (section->kind == SectionKind_BSS || section->kind == SectionKind_Common) {
                    PPCError_Error(PPCERR_SECTION_NAME_IS_BSS, name);
                    return;
                }
                targetIndex = pseudo_section_index(declinfo->section);
                if (targetIndex != 0 && targetIndex != section->header->index) {
                    PPCError_Error(
                        PPCERR_DEFINED_IN_OTHER_SECTION, declinfo->name->name, section->name
                    );
                    return;
                }
                record->sectionIndex = section->header->index;
                return;
            }
        }
    }
    PPCError_Error(PPCERR_UNKNOWN_SECTION_NAME, name);
}

void InitElfFile(void) {
    char filename[257];
    SInt32 length;
    BE_SymNode* sym;

    SpecificNum = 1;
    memset(&ELFfile, 0, sizeof(ELFfile));
    InitElfSections();
    COS_FileGetFSSpecInfo(&cparamblkptr->sourcefile, NULL, NULL, (StringPtr)filename);
    // The original's bug: the length byte is a signed char, so the check below never fires and a
    // name of 128 characters or more indexes before the buffer.
    length = filename[0];
    if (length > 255) {
        CError_FATAL(543);
    }
    filename[length + 1] = 0;

    sym = AddSymbolTableEntry(GetHashNameNode(&filename[1]));
    sym->elf.st_info = ELF32_ST_INFO(STB_LOCAL, STT_FILE);
    sym->section = ELFfile.abs_section;
    sym->alignment = 1;
    sym->sclass = TK_STATIC;
    ELFfile.file_symbol = sym;

    allrelocations = NULL;
    padding_data = padding_buffer;
    max_padding_size = 128;
}

void CleanupElfSections(void) {
    ObjGenSection* section;

    for (section = ELFfile.section_list; section != NULL; section = section->next) {
        if (section->initialSize != 0) {
            FreeGList(&section->buffer);
        }
    }
}

// Numbers the sections in the order their headers are written (contents, relocations, then the
// symbol, string and comment tables) and declares each content section's symbol.
// NOTE: invented name (a static Wii's BE_elf has unnamed)
static void build_ordered_section_index(void) {
    int count;
    ObjGenSection* section;
    BE_SymNode* sym;

    count = 1;
    for (section = ELFfile.section_list; section != NULL; section = section->next) {
        section->index = 0;
        if (section->kind < SectionKind_Symtab && section->buffer.size != 0) {
            ++count;
        }
        if (section->kind >= SectionKind_Symtab && section->kind < SectionKind_Common) {
            ++count;
        }
    }
    ordered_section_index = galloc((count + 2) * sizeof(ObjGenSection*));
    ordered_section_index[0] = ELFfile.null_section;
    count = 1;
    if (ELFfile.debug_sections_inited) {
        section = GetSectionByName(".text", 0, 0);
        sym = elf_declare_symbol(section);
        if (section->buffer.size == 0) {
            ordered_section_index[1] = section;
            section->index = 1;
            ++count;
            sym->elf.st_info = ELF32_ST_INFO(STB_LOCAL, STT_SECTION);
            sym->section = section;
            sym->alignment = section->shdr.sh_addralign;
            sym->sclass = TK_STATIC;
        }
    }
    for (section = ELFfile.section_list->next; section != NULL; section = section->next) {
        if (section->kind < SectionKind_Rel && section->buffer.size != 0) {
            ordered_section_index[count] = section;
            section->index = count;
            ++count;
            sym = elf_declare_symbol(section);
            sym->elf.st_info = ELF32_ST_INFO(STB_LOCAL, STT_SECTION);
            sym->section = section;
            sym->alignment = section->shdr.sh_addralign;
            sym->sclass = TK_STATIC;
        }
    }
    for (section = ELFfile.section_list->next; section != NULL; section = section->next) {
        if (section->kind >= SectionKind_Rel &&
            section->kind < SectionKind_Symtab &&
            section->buffer.size != 0) {
            ordered_section_index[count] = section;
            section->index = count;
            ++count;
        }
    }
    for (section = ELFfile.section_list->next; section != NULL; section = section->next) {
        if (section->kind >= SectionKind_Symtab && section->kind < SectionKind_Common) {
            ordered_section_index[count] = section;
            section->index = count;
            ++count;
        }
    }
    ordered_section_index[count] = NULL;
    ELFfile.common_section->index = -14;
    ELFfile.abs_section->index = -15;
    ELFfile.ctors_section->permissions = 2;
    ELFfile.ctors_section->shdr.sh_flags = 2;
    ELFfile.dtors_section->permissions = 2;
    ELFfile.dtors_section->shdr.sh_flags = 2;
}

void WriteSsymtab(void) {
    BE_SymNode* sym;

    SectionAlign(&elfmodule, ELFfile.symtab_section->shdr.sh_addralign);
    sym = InorderFirst();
    while (sym != NULL) {
        if ((sym->elf.st_other & 64) == 0) {
            sym->elf.st_shndx = sym->section->index;
            if (copts.no_comment_records != 0) {
                sym->elf.st_other = 0;
            } else {
                sym->elf.st_other &= 63;
                sym->alignment = CTool_EndianConvertWord32(sym->alignment);
                AppendGListData(&ELFfile.comment_section->buffer, &sym->alignment, 8);
            }
            Convert_Elf32_Sym(&sym->elf);
            AppendGListData(&ELFfile.symtab_section->buffer, &sym->elf, sizeof(Elf32_Sym));
        }
        sym = sym->orderNext;
    }
}

void WriteSstrtab(void) {
    BE_SymNode* sym;
    unsigned int offset;
    unsigned int length;
    const char* name;
    GList* list;

    offset = 0;
    AppendGListByte(&ELFfile.symbol_string_table_section->buffer, offset);
    offset++;
    sym = InorderFirst()->orderNext;
    for (; sym != NULL; sym = sym->orderNext) {
        if (ELF32_ST_TYPE(sym->elf.st_info) == STT_SECTION) { // a section symbol has no name
            sym->elf.st_name = 0;
        } else {
            sym->elf.st_name = offset;
            name = sym->name->name;
            list = &ELFfile.symbol_string_table_section->buffer;
            length = strlen(name) + 1;
            AppendGListData(list, name, length);
            offset += length;
        }
    }
}

void TransferSectionData(GList* dst, GList* src) {
    if (dst->size + src->size > dst->hndlsize) {
        dst->hndlsize += src->size + dst->growsize;
        if (!COS_ResizeHandle(dst->data, dst->hndlsize) && GListErrorProc) {
            GListErrorProc();
        }
    }
    memcpy(*dst->data + dst->size, *src->data, src->size);
    dst->size += src->size;
}

// Writes the .comment section's header record.
// NOTE: invented names: write_codewarrior_version_record (a static Wii's BE_elf has unnamed), name,
// majorVersion, minorVersion, patchVersion, reservedVersion, versionFormat, abiOption,
// returnConvention, processor, recordSize, options, reservedBytes, reservedWords (the record's
// fields)
static void write_codewarrior_version_record(void) {
    struct {
        char name[11];
        UInt8 majorVersion;
        UInt8 minorVersion;
        UInt8 patchVersion;
        UInt8 reservedVersion;
        UInt8 versionFormat;
        UInt8 abiOption;
        UInt8 returnConvention;
        UInt8 processor[2];
        UInt8 recordSize;
        UInt8 options;
        UInt8 reservedBytes[2];
        UInt32 reservedWords[5];
    } record;
    U16Bytes version;
    UInt8 returnConvention;
    UInt8 flags = 0;

    Test_Version_Numbers();

    record.name[0] = 'C';
    record.name[1] = 'o';
    record.name[2] = 'd';
    record.name[3] = 'e';
    record.name[4] = 'W';
    record.name[5] = 'a';
    record.name[6] = 'r';
    record.name[7] = 'r';
    record.name[8] = 'i';
    record.name[9] = 'o';
    record.name[10] = 'r';
#if VERSION >= VERSION_GC_2_7
    record.majorVersion = 11;
#else
    record.majorVersion = 10;
#endif
    record.minorVersion = 2;
    record.patchVersion = 4;
    record.reservedVersion = 7;
    record.versionFormat = 1;

    record.abiOption = copts.pool_data ? 1 : 0;
    if (copts.use_fp) {
        if (copts.soft_float) {
            returnConvention = 1;
        } else {
            returnConvention = 2;
        }
    } else {
        returnConvention = 0;
    }
    record.returnConvention = returnConvention;

    version.w = CTool_EndianConvertWord16(copts.processor);
    record.processor[0] = version.b[0];
    record.processor[1] = version.b[1];

    record.recordSize = sizeof(record);

    if (copts.incompatible_return_small_structs != 0) {
        flags++;
    }
    if (copts.incompatible_sfpe_double_params != 0) {
        flags += 2;
    }
    if (copts.unsafe_global_reg_vars != 0 && reg_info_list != NULL) {
        flags += 4;
    }
    record.options = flags;

    record.reservedBytes[0] = 0;
    record.reservedBytes[1] = 0;
    record.reservedWords[0] = 0;
    record.reservedWords[1] = 0;
    record.reservedWords[2] = 0;
    record.reservedWords[3] = 0;
    record.reservedWords[4] = 0;

    AppendGListData(&ELFfile.comment_section->buffer, &record, sizeof(record));
}

// NOTE: invented name (a static Wii's BE_elf has unnamed; inlined into BackEnd_WriteElf)
static inline void WriteSectionData(void) {
    SInt32 i;
    ObjGenSection* section;

    for (i = 1, section = ordered_section_index[1]; section != NULL;
         section = ordered_section_index[++i]) {
        SectionAlign(&elfmodule, section->shdr.sh_addralign);
        section->shdr.sh_offset = elfmodule.size;
        section->shdr.sh_size = section->buffer.size;
        if (section->shdr.sh_type != SHT_NOBITS) {
            TransferSectionData(&elfmodule, &section->buffer);
        }
    }
}

// Wii's ObjGen_PPC_EABI defines it out of line; 2.6 has no copy of it anywhere, so 2.6's was an
// inline function or a macro (where 2.6 defined it is not known)
static inline int is_big_endian(void) {
    return !copts.littleendian;
}

// TODO: 2.6 picks this function's scratch registers the way a -sym off build does, so symbols are
// off for this function alone (-sym off for the whole unit loses other functions); what in 2.6's
// source makes that so is not found
#pragma sym off

// Writes the ELF object file: the header, the relocations' symbol indices, the section links, the
// section-name table, each section's contents at its alignment and the section header table, in the
// target's byte order.
void BackEnd_WriteElf(void) {
    ObjGenSection* section;
    Elf32_Rela* rela;
    ObjGenRelocation* reloc;
    int index;
    SInt32 offset;
    SInt32 length;

    elfBigEndian = is_big_endian() != 0;
    if (ELFfile.debug_sections_inited) {
        tweakDWARF_file();
        if (elfBigEndian) {
            COS_LockHandle(ELFfile.dwarf_line_section->buffer.data);
            Convert_DWARF_line_info(
                (UInt8*)*ELFfile.dwarf_line_section->buffer.data,
                ELFfile.dwarf_line_section->buffer.size,
                CError_Internal,
                TRUE
            );
            COS_UnlockHandle(ELFfile.dwarf_line_section->buffer.data);
            COS_LockHandle(ELFfile.dwarf_info_section->buffer.data);
            Convert_DWARF_Info(
                (UInt8*)*ELFfile.dwarf_info_section->buffer.data,
                ELFfile.dwarf_info_section->buffer.size,
                CError_Internal,
                TRUE
            );
            COS_UnlockHandle(ELFfile.dwarf_info_section->buffer.data);
        }
    }
    build_ordered_section_index();
    if (!copts.no_comment_records) {
        write_codewarrior_version_record();
    }
    ElfHeader.e_ident[0] = 0x7f;
    ElfHeader.e_ident[1] = 'E';
    ElfHeader.e_ident[2] = 'L';
    ElfHeader.e_ident[3] = 'F';
    ElfHeader.e_ident[4] = 1;
    ElfHeader.e_ident[5] = is_big_endian() ? 2 : 1;
    ElfHeader.e_ident[6] = 1;
    ElfHeader.e_type = CTool_EndianConvertWord16(1); // ET_REL
    ElfHeader.e_machine = CTool_EndianConvertWord16(20); // EM_PPC
    ElfHeader.e_version = CTool_EndianConvertWord32(1);
    ElfHeader.e_entry = 0;
    ElfHeader.e_phoff = 0;
    ElfHeader.e_shoff = 0;
    ElfHeader.e_flags = CTool_EndianConvertWord32(0x80000000); // EF_PPC_EMB
    ElfHeader.e_ehsize = CTool_EndianConvertWord16(sizeof(Elf32_Ehdr));
    ElfHeader.e_phentsize = 0;
    ElfHeader.e_phnum = 0;
    ElfHeader.e_shentsize = CTool_EndianConvertWord16(sizeof(Elf32_Shdr));
    ElfHeader.e_shnum = 0;
    ElfHeader.e_shstrndx = CTool_EndianConvertWord16(ELFfile.shstrtab_section->index);
    AppendGListData(&elfmodule, &ElfHeader, sizeof(Elf32_Ehdr));
    assign_symbol_order();
    if (allrelocations) {
        for (reloc = allrelocations; reloc; reloc = reloc->next) {
            rela = (Elf32_Rela*)*reloc->section->buffer.data;
            rela += reloc->index;
            if (!reloc->symbol) {
                reloc->symbol = elf_declare_symbol(reloc->source);
            }
            rela->r_info = (reloc->symbol->order << 8) + (reloc->kind & 255);
            Convert_Elf32_Rela(rela);
        }
    }
    section = ELFfile.symtab_section;
    section->shdr.sh_link = ELFfile.symbol_string_table_section->index;
    section->shdr.sh_info = symbol_order_count + 1;
    for (section = ELFfile.section_list->next; section; section = section->next) {
        if (section->kind < SectionKind_Rel && section->relocations) {
            section->relocations->shdr.sh_link = ELFfile.symtab_section->index;
            section->relocations->shdr.sh_info = section->index;
        }
        if (section->kind == SectionKind_MWCATS) {
            section->shdr.sh_link = section->sectionData.attributes->sourceSection->index;
        }
    }
    WriteSstrtab();
    offset = 0;
    AppendGListByte(&ELFfile.shstrtab_section->buffer, offset);
    offset++;
    {
        ObjGenSection* s;
        for (index = 1, s = ordered_section_index[1]; s != NULL;
             s = ordered_section_index[++index]) {
            char* name;
            GList* list;

            s->shdr.sh_name = offset;
            name = s->name;
            list = &ELFfile.shstrtab_section->buffer;
            AppendGListData(list, name, length = strlen(name) + 1);
            offset += length;
        }
    }
    WriteSsymtab();
    WriteSectionData();
    SectionAlign(&elfmodule, 8);
    ElfHeader.e_shoff = elfmodule.size;
    Convert_Elf32_Shdr(&ELFfile.section_list->shdr);
    AppendGListData(&elfmodule, &ELFfile.section_list->shdr, sizeof(Elf32_Shdr));
    ElfHeader.e_shnum++;
    for (index = 1, section = ordered_section_index[1]; section != NULL;
         section = ordered_section_index[++index]) {
        Convert_Elf32_Shdr(&section->shdr);
        AppendGListData(&elfmodule, &section->shdr, sizeof(Elf32_Shdr));
        ElfHeader.e_shnum++;
    }
    ElfHeader.e_shoff = offset = CTool_EndianConvertWord32(ElfHeader.e_shoff);
    ((Elf32_Ehdr*)*elfmodule.data)->e_shoff = offset;
    ElfHeader.e_shnum = CTool_EndianConvertWord16(ElfHeader.e_shnum);
    ((Elf32_Ehdr*)*elfmodule.data)->e_shnum = ElfHeader.e_shnum;
}

#pragma sym reset
ObjGenRelocation* WriteDebugRelocation(
    ObjGenSection* context, int offset, BE_SymNode* symbol, void* source, int value, int adjustment
) {
    ObjGenSection* section;
    ObjGenRelocation* reloc;
    int kind;
    reloc = NULL;
#if VERSION >= VERSION_GC_2_7
    // 2.7 writes every DWARF relocation unaligned
    kind = 24; // R_PPC_UADDR32
#else
    kind = R_PPC_ADDR32;
#endif
    section = context->relocations;
    if (copts.dwarf_relocations != 0) {
        Elf32_Rela payload;
        section->relocationCount += 1;
        reloc = galloc(sizeof(ObjGenRelocation));
        reloc->symbol = NULL;
        reloc->source = NULL;
        if (symbol != NULL) {
            reloc->symbol = symbol;
        } else {
            reloc->source = source;
        }
        offset += adjustment;
#if VERSION < VERSION_GC_2_7
        if (((offset + 3) & -4) - offset != 0) {
            kind = 24; // R_PPC_UADDR32
        }
#endif
        reloc->kind = kind;
        reloc->section = section;
        reloc->index = section->relocationCount - 1;
        payload.r_offset = offset;
        payload.r_addend = value;
        payload.r_info = kind & 255;
        AppendGListData(&section->buffer, &payload, sizeof(payload));
        reloc->next = allrelocations;
        allrelocations = reloc;
    }
    return reloc;
}

void BackPatchRelocationAddend(ObjGenRelocation* reloc, unsigned int value, BE_SymNode* symbol) {
    Elf32_Rela* rela = (Elf32_Rela*)*reloc->section->buffer.data;
    rela += reloc->index;
    rela->r_addend = value;
    if (symbol) {
#if VERSION < VERSION_GC_2_7
        // (the original goes on after this; 2.7 drops the error)
        CError_FATAL(1025);
#endif
        reloc->symbol = symbol;
        reloc->source = NULL;
    }
}
