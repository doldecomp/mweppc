#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/IrOptimizer.h"
#include "compiler/BE_elf.h"
#include "compiler/BE_symbol.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/DumpIR.h"
#include "compiler/DWARF.h"
#include "compiler/ELF_Endian.h"
#include "compiler/objects.h"
#include "compiler/Operands.h"
#include "compiler/PCodeListing.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/StackFrame.h"
#include "compiler/Switch.h"
#include "compiler/TOC.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "driver/COSToolsCLT.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct ExtabIndexEntry {
    UInt32 function;
    UInt32 size;
    UInt32 extab;
} ExtabIndexEntry;

#pragma pack(push, 2)
struct BufferUpdate {
    struct BufferUpdate* next;
    struct BE_SymNode* target;
    Boolean active;
    Boolean suppressed;
};
#pragma pack(pop)

struct CNameNode {
    struct CNameNode* next;
    HashNameNode* key;
    SInt32 uid;
    SInt32 lastLine;
};

typedef struct MWCATSExit {
    struct MWCATSExit* next;
    UInt32 offset;
} MWCATSExit;

#pragma pack(push, 2)
struct SectionAttributeNode {
    struct SectionAttributeNode* next;
    struct Object* object;
    MWCATSExit* exits;
    MWCATSExit* lastExit;
    SInt32 exitCount;
    UInt8 flags;
};
#pragma pack(pop)

typedef struct MWCATSShortHeader {
    UInt8 kind;
    UInt8 exitCount;
    UInt16 codesize;
    UInt32 address;
} MWCATSShortHeader;

typedef struct MWCATSWideHeader {
    UInt8 kind;
    UInt8 flags;
    UInt16 exitCount;
    UInt32 codesize;
    UInt32 address;
} MWCATSWideHeader;

static void create_main_file_object(void);
static ObjGenSection* select_object_section(
    Object* obj, SInt32 size, Boolean initialized, Boolean noCommon, Boolean isConst
);
static InterruptGenerationRecord* resolve_object_pseudo_section(Object* obj, BE_SymNode* sym);
static void emit_object_data_and_relocations(
    Object* object, const char* data, OLinkList* list, SInt32 size, Boolean isConst
);
static void allocate_object_storage(Object* object, SInt32 size, Boolean isConst);
static void report_section_permission_conflict(
    Object* object, ObjGenSection* section, char* kind, UInt32 permissions
);

static Boolean unused_flag;
static struct ObjGenSection* default_sconst_section;
static struct ObjGenSection* default_sdata_section;
static struct ObjGenSection* default_const_section;
static struct ObjGenSection* default_data_section;
static struct ObjGenSection* default_code_section;
static UInt8 section_table_dirty;
static SInt32 uid_counter;
static SInt32 object_storage_size;
static SInt32 object_data_size;
static SInt32 unused_size;
static SInt32 last_line_offset;
static int last_base_offset;
static struct ObjGenSection* last_function_section;
static SectionSymbolAttributes** saved_section_table;
static struct BufferUpdate* pending_buffer_updates;
static struct BufferUpdate* pending_data_tail;

struct CNameNode* AllFiles;
SInt32 objcodesize;
struct CNameNode* CurFile;
UInt8 ok_to_compress;

static inline void SetDataSectionOptions(
    SectionRec* section, ObjGenSection* symbol, Boolean useDefaults
) {
    if (section->typebits & 2) {
        copts.dataSection = useDefaults ? default_data_section : symbol;
    }
    if (section->typebits & 4) {
        copts.constSection = useDefaults ? default_const_section : symbol;
    }
    if (section->typebits & 8) {
        copts.sdataSection = useDefaults ? default_sdata_section : symbol;
    }
    if (section->typebits & 0x10) {
        copts.sconstSection = useDefaults ? default_sconst_section : symbol;
    }
}

static inline void SetCodeSectionOption(ObjGenSection* symbol, Boolean useDefaults) {
    copts.textSection = useDefaults ? default_code_section : symbol;
}

static void SymContainedVars(void) {
    ObjectList* list;
    Object* object;
    Type* type;
    ObjectList* scan;
    ObjectList* reversed;
    ObjectList* copy;
    short gpr;

    gpr = 3;
    for (list = arguments; list; list = list->next) {
        object = list->object;
        if ((object->u.var.info->used || (object->u.var.info->flags & VarInfoFlag_InReg)) &&
            object->name->name[0] != '@') {
            type = object->type;
            if ((TYPE_IS_8BYTES(type) ||
                 (TYPE_IS_SOFT_FLOAT(type) &&
                  type->size != 4 &&
                  !copts.incompatible_sfpe_double_params)) &&
                (gpr % 2) == 0) {
                gpr++;
            }
            if (!IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) || TYPE_IS_SOFT_FLOAT(type)) {
                if (TYPE_IS_GPR_PAIR(type)) {
                    gpr += 2;
                } else if (TYPE_FITS_IN_REGISTER(type) ||
                           (TYPE_IS_SOFT_FLOAT(type) && type->size == 4)) {
                    gpr++;
                } else {
                    gpr += type->size >> 2;
                    if (type->size & 3) {
                        gpr++;
                    }
                }
            }
            newDWARF_param(object, 0);
        }
    }

    reversed = NULL;
    for (scan = locals; scan; scan = scan->next) {
        copy = galloc(sizeof(ObjectList));
        copy->object = scan->object;
        copy->next = reversed;
        reversed = copy;
    }
    for (list = reversed; list; list = list->next) {
        Object* local = list->object;
        if ((local->u.var.info->used || (local->u.var.info->flags & VarInfoFlag_InReg)) &&
            local->name->name[0] != '@') {
            newDWARF_local(local, 0);
        }
    }
}

void ELF_Setup(void) {
    previous_ps_ID = 0;
    interrupt_generation_records = NULL;
    InitDwarf();
    InitELFSymbols();
    InitElfFile();
}

void ObjGen_Setup(void) {
    setup_global_registers();
    elfmodule.data = NULL;
    pending_buffer_updates = NULL;
    CurFile = AllFiles = NULL;
    uid_counter = 0;
    ok_to_compress = FALSE;
    if (InitGList(&elfmodule, 40000)) {
        CError_NoMem();
    }
    {
        UInt32* w;

        w = (UInt32*)&one_point_zero;
        w[0] = 0;
        w[1] = 0x3ff00000;
        w = (UInt32*)&int_to_float_cc;
        w[0] = 0x80000000;
        w[1] = 0x43300000;
        w = (UInt32*)&uns_to_float_cc;
        w[0] = 0;
        w[1] = 0x43300000;
    }
    CurFile = AllFiles = galloc(sizeof(CNameNode));
    memset(CurFile, 0, sizeof(CNameNode));
    CurFile->key = CTool_GetPathName(&cparamblkptr->sourcefile, NULL);
    CurFile->lastLine = -1;
    unused_flag = FALSE;
    objcodesize = object_storage_size = object_data_size = unused_size = 0;
    IRO_Setup();
}

void ObjGen_Finish(void) {
    BufferUpdate* update;

    CInit_DeclarePooledStrings();
    DeclarePooledConstants();
    if (copts.create_file_object) {
        create_main_file_object();
    }
    for (update = pending_buffer_updates; update; update = update->next) {
        if (update->active &&
            update->target->section != ELFfile.common_section &&
            !update->suppressed) {
            SectionAlign(&update->target->section->buffer, update->target->alignment);
            update->target->elf.st_value = update->target->section->buffer.size;
            AppendGListNoData(&update->target->section->buffer, update->target->elf.st_size);
        }
    }
    pending_buffer_updates = NULL;
    BackEnd_WriteElf();
    ShrinkGList(&elfmodule);
    cparamblkptr->objectdata = (Handle)elfmodule.data;
    elfmodule.data = 0;
    cparamblkptr->object.codesize = objcodesize;
    cparamblkptr->object.udatasize = object_storage_size;
    cparamblkptr->object.idatasize = object_data_size;
    finish_global_registers();
}

void ObjGen_Cleanup(void) {
    if (ELFfile.debug_sections_inited) {
        endDWARF_file();
    }
    CleanupElfSections();
    FreeGList(&elfmodule);
    CleanupDumpIR();
    pccleanuplisting();
}

void ObjGen_CodeCleanup(void) {}

void ObjGen_SetupSym(void) {}

void ObjGen_BestSection(Object* func) {
    ObjGenSection* section;
    ObjGenSection* clone;
    SectionSymbolAttributes* header;
    Boolean compress = FALSE;

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -13
#endif
    CError_ASSERT(422, !ok_to_compress);
    ok_to_compress = TRUE;
    if (copts.prepare_compress) {
#if VERSION < VERSION_GC_2_0
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -10
        switch (copts.processor) {
            case CPU_PPC556:
            case CPU_PPC565:
                CError_ASSERT(426, !switchtables);
                compress = TRUE;
                break;
            default:
                PPCError_Warning(PPCERR_PREPARE_COMPRESS_IGNORED);
                break;
        }
#else
        CError_ASSERT(426, !switchtables);
        compress = TRUE;
#endif
    }
    if (CurFile != AllFiles || compress) {
        section = select_object_section(func, 0, TRUE, TRUE, TRUE);
        clone = GetSectionByName(section->name, CurFile->uid, compress);
        if (!clone) {
            clone = CloneSection(section, CurFile->uid, compress);
        }
        header = section->header;
        clone->header = Initialize_Specific(
            clone, header->dataMode, header->codeMode, header->linkedSymbol, header->auxiliary
        );
        func->section = clone->header->index;
    }
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -5
#endif
void ObjGen_SymFunc(Object* func) {
    CError_ASSERT(451, ok_to_compress);
    if (!ELFfile.debug_sections_inited) {
        InitDebugSections(ELFfile.dwarf_info_section, ELFfile.dwarf_line_section);
        newDWARF_file(GetSectionByName(".text", 0, 0));
        ELFfile.debug_sections_inited = TRUE;
    }
    last_line_offset = -1;
    CurFile->lastLine = -1;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

void ObjGen_SegmentName(HashNameNode* name) {}

void ObjGen_SrcBreakName(HashNameNode* name, SInt32 fileModDate, Boolean unused) {
    CNameNode* file;
    CNameNode* last;

    if (!name) {
        CurFile = AllFiles;
        return;
    }
    for (file = AllFiles; file; file = file->next) {
        if (name == file->key) {
            CurFile = file;
            return;
        }
        last = file;
    }
    last->next = galloc(sizeof(CNameNode));
    CurFile = last->next;
    memset(CurFile, 0, sizeof(CNameNode));
    CurFile->lastLine = -1;
    CurFile->key = name;
    CurFile->uid = ++uid_counter;
}

void init_xref_anchors(void) {
    ObjGenSection* section;

    for (section = ELFfile.section_list->next; section; section = section->next) {
        if (section->kind < SectionKind_Rel && section->symbolLink) {
            section->symbolLink->used = FALSE;
        }
    }
}

static void declaredata(
    Object* object, const void* data, OLinkList* list, unsigned int size, Boolean isConst
) {
    if (copts.filesyminfo && object->name->name[0] != '@') {
        if (!ELFfile.debug_sections_inited) {
            InitDebugSections(ELFfile.dwarf_info_section, ELFfile.dwarf_line_section);
            newDWARF_file(GetSectionByName(".text", 0, 0));
            ELFfile.debug_sections_inited = TRUE;
        }
        DWARF_DeclareData(object);
    }
    if (data) {
        emit_object_data_and_relocations(object, data, list, size, isConst);
    } else {
        allocate_object_storage(object, size, isConst);
    }
}

void ObjGen_DeclareData(Object* object, const void* data, OLinkList* list, unsigned int size) {
    declaredata(object, data, list, size, FALSE);
}

void ObjGen_DeclareReadOnlyData(
    Object* object, const void* data, OLinkList* list, unsigned int size
) {
    declaredata(object, data, list, size, TRUE);
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -5
#endif
char* GetSourceFileName(ObjGenSection* section) {
    CNameNode* file;
    SInt32 uid;

    uid = section->context;
    for (file = AllFiles; file; file = file->next) {
        if (uid == file->uid) {
            return file->key->name;
        }
    }
    CError_FATAL(667);
    return NULL;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -5
#endif
int ObjGen_GetAddressing(Object* obj) {
    SectionSymbolAttributes* entry;
    SInt32 id;

    if (obj->datatype == DLOCAL) {
        return AM_LOCAL;
    }
    if (obj->datatype == DABSOLUTE) {
        if (obj->u.address == (UInt32)(SInt16)obj->u.address) {
            return AM_NEAR_ABS;
        }
        return AM_FAR_ABS;
    }

#if VERSION < VERSION_GC_2_0
    PPC_EABI_SetSection(obj, obj->type->size, FALSE);
#else
    PPC_EABI_SetSection(obj, obj->type->size, FALSE, FALSE);
#endif
    id = pseudo_section_index(obj->section);
    if (section_table_dirty) {
        CodeGen_PragmaPush();
    }
    for (entry = ELFfile.section_symbol_attributes; entry; entry = entry->next) {
        if ((SInt16)entry->index == id) {
            break;
        }
    }
    CError_ASSERT(695, entry);

    if (obj->datatype == DFUNC || obj->datatype == DVFUNC || obj->datatype == DINLINEFUNC) {
        return entry->codeMode;
    }
    return entry->dataMode;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

Boolean is_16bitdata(Object* obj) {
    AddressingMode value = ObjGen_GetAddressing(obj);

    if (value == AM_SDATA || value == AM_SDATA2 || value == AM_SDATA0 || value == AM_NEAR_ABS) {
        return TRUE;
    }
    return FALSE;
}

static inline Boolean is_SDAdata(Object* obj) {
    AddressingMode value = ObjGen_GetAddressing(obj);

    if (value == AM_SDATA || value == AM_SDATA2 || value == AM_SDATA0) {
        return TRUE;
    }
    return FALSE;
}

Boolean is_illegal_abs_section_name(char* name) {
    char* p;
    char ch;
    SInt32 len;

    if (strncmp(".abs.", name, 5) != 0) {
        return FALSE;
    }
    len = strlen(name + 5);
    if (len == 0 || len > 8) {
        return TRUE;
    }
    p = name + 5;
    while (*p != 0) {
        ch = *p;
        p++;
        switch (tolower(ch)) {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case 'a':
            case 'b':
            case 'c':
            case 'd':
            case 'e':
            case 'f':
                break;
            default:
                return TRUE;
        }
    }
    return FALSE;
}

unsigned char is_illegal_section_name(char* name) {
    if (*name == 0) {
        return TRUE;
    }
    if (memcmp("sinit", name, 6) == 0) {
        return TRUE;
    }
    if (memcmp("extab", name, 6) == 0) {
        return TRUE;
    }
    if (memcmp("extabindex", name, 11) == 0) {
        return TRUE;
    }
    if (strncmp(".rela", name, 5) == 0) {
        return TRUE;
    }
    if (strncmp(".rel", name, 4) == 0) {
        return TRUE;
    }
    if (memcmp(".strtab", name, 8) == 0) {
        return TRUE;
    }
    if (memcmp(".shstrtab", name, 10) == 0) {
        return TRUE;
    }
    if (memcmp(".symtab", name, 8) == 0) {
        return TRUE;
    }
    if (strncmp(".debug", name, 6) == 0) {
        return TRUE;
    }
    if (memcmp(".line", name, 6) == 0) {
        return TRUE;
    }
    if (memcmp(".ctors", name, 7) == 0) {
        return TRUE;
    }
    if (memcmp(".dtors", name, 7) == 0) {
        return TRUE;
    }
    if (strncmp(".mwcats", name, 7) == 0) {
        return TRUE;
    }
    return FALSE;
}

void CodeGen_PragmaPush(void) {
    ObjGenSection* section;
    SInt16 count;
    SInt32 i;

    if (section_table_dirty) {
        section_table_dirty = FALSE;
        copts.sectionHeaderTableSize = count = ELFfile.sectionHeaderCount;
        saved_section_table = copts.sectionHeaderTable =
            galloc(count * sizeof(SectionSymbolAttributes*));
        memset(
            copts.sectionHeaderTable,
            0,
            ELFfile.sectionHeaderCount * sizeof(SectionSymbolAttributes*)
        );
        for (i = 0, section = ELFfile.section_list; section; section = section->next, i++) {
            copts.sectionHeaderTable[i] = section->header;
        }
    }
}

void CodeGen_PragmaPop(void) {
    ObjGenSection* section;
    SInt32 i;

    if (copts.sectionHeaderTableSize < ELFfile.sectionHeaderCount) {
        memcpy(
            saved_section_table,
            copts.sectionHeaderTable,
            copts.sectionHeaderTableSize * sizeof(SectionSymbolAttributes*)
        );
        copts.sectionHeaderTable = saved_section_table;
        copts.sectionHeaderTableSize = ELFfile.sectionHeaderCount;
    }
    for (i = 0, section = ELFfile.section_list; section; section = section->next, i++) {
#if VERSION < VERSION_GC_2_0
        section->header = copts.sectionHeaderTable[i];
#else
        if (copts.sectionHeaderTable[i]) {
            section->header = copts.sectionHeaderTable[i];
        }
#endif
    }
}

void Finish_Pragma_Section(SectionRec* section) {
    ObjGenSection* symbol;
    ObjGenSection* objectSymbol;
    ObjGenSection* linkedSymbol;
    struct ObjGenSymbolLink* symbolLink;
    UInt32 sizeFlags;
    AddressingMode dataMode;
    ObjGenSection* auxiliary;
    AddressingMode codeMode;

    if (section->sectionName == NULL) {
        if (section->typebits & 1) {
            SetCodeSectionOption(NULL, TRUE);
        }
        SetDataSectionOptions(section, NULL, TRUE);
        return;
    }
    symbol = GetSectionByName(section->sectionName, 0, 0);
    objectSymbol = GetSectionByName(section->linkedSectionName, 0, 0);
    if (symbol == NULL) {
        if (section->dataMode == AM_SDA_REL) {
            PPCError_Error(PPCERR_SDA_REL_NOT_SMALL_DATA);
            return;
        }
        sizeFlags = section->mode;
        if (sizeFlags == 0) {
            sizeFlags = 2;
        }
        if (section->dataMode == AM_NONE) {
            section->dataMode = AM_FAR_ABS;
        }
        if (section->codeMode == AM_NONE) {
            section->codeMode = AM_CODE_PC_REL;
        }
        if (section->typebits != 0) {
            if (section->typebits & 0x1a) {
                if (section->linkedSectionName == NULL) {
                    PPCError_Error(PPCERR_DATA_SECTION_NEEDS_BSS);
                    return;
                }
                sizeFlags |= 3;
            }
            if (section->typebits & 1) {
                sizeFlags |= 6;
            }
        }
        DoElfContentSection(section, SectionKind_Content, 4, 1000, 100, sizeFlags, 1, 1);
        section_table_dirty = TRUE;
        return;
    }
    if (symbol->kind == SectionKind_BSS || symbol->kind == SectionKind_Common) {
        PPCError_Error(PPCERR_SECTION_NAME_IS_BSS, section->sectionName);
        return;
    }
    dataMode = symbol->header->dataMode;
    codeMode = symbol->header->codeMode;
    linkedSymbol = symbol->header->linkedSymbol;
    auxiliary = symbol->header->auxiliary;
    if (objectSymbol && objectSymbol->kind != SectionKind_Common) {
        section_table_dirty = TRUE;
        auxiliary = NULL;
    }
    if (section->linkedSectionName && linkedSymbol && linkedSymbol != objectSymbol) {
        section_table_dirty = TRUE;
        if (objectSymbol->kind == SectionKind_Common) {
            auxiliary = ELFfile.common_section;
            objectSymbol = linkedSymbol;
        }
        linkedSymbol = objectSymbol;
    }
    if (linkedSymbol == NULL && objectSymbol != NULL) {
        section_table_dirty = TRUE;
        if (objectSymbol->kind == SectionKind_Common) {
            auxiliary = ELFfile.common_section;
            objectSymbol = ELFfile.data_section_linked_symbol;
        }
        linkedSymbol = objectSymbol;
    }
    sizeFlags = section->mode;
    if (sizeFlags == 0) {
        sizeFlags = 2;
    }
    if (section->linkedSectionName && linkedSymbol == NULL) {
        section_table_dirty = TRUE;
        symbolLink = galloc(sizeof(ObjGenSymbolLink));
        memset(symbolLink, 0, sizeof(ObjGenSymbolLink));
        linkedSymbol = InitElfSection(
            section->linkedSectionName,
            SectionKind_BSS,
            4,
            1000,
            section,
            sizeFlags & ~4,
            symbolLink
        );
        if (symbol->symbolLink == NULL) {
            symbolLink = galloc(sizeof(ObjGenSymbolLink));
            memset(symbolLink, 0, sizeof(ObjGenSymbolLink));
            symbol->symbolLink = symbolLink;
        }
    }
    if (section->dataMode != AM_NONE) {
        if (section->dataMode == AM_SDA_REL) {
            if (memcmp(section->sectionName, ".sdata", 7) == 0) {
                section->dataMode = AM_SDATA;
            } else if (memcmp(section->sectionName, ".sdata2", 8) == 0) {
                section->dataMode = AM_SDATA2;
            } else if (memcmp(section->sectionName, ".PPC.EMB.sdata0", 0x10) == 0) {
                section->dataMode = AM_SDATA0;
            } else {
                PPCError_Error(PPCERR_SDA_REL_NOT_SMALL_DATA);
                return;
            }
        }
        section_table_dirty = TRUE;
        dataMode = section->dataMode;
    }
    if (section->codeMode != AM_NONE) {
        section_table_dirty = TRUE;
        codeMode = section->codeMode;
    }
    if (section->typebits != 0) {
        if (section->typebits & 0x1a) {
            if (linkedSymbol == NULL) {
                PPCError_Error(PPCERR_DATA_SECTION_NEEDS_BSS);
                return;
            }
            sizeFlags |= 3;
        }
        if (section->typebits & 1) {
            sizeFlags |= 6;
            SetCodeSectionOption(symbol, FALSE);
        }
        SetDataSectionOptions(section, symbol, FALSE);
    }
    symbol->shdr.sh_flags |= sizeFlags;
    symbol->permissions |= sizeFlags;
    if (linkedSymbol != NULL) {
        linkedSymbol->shdr.sh_flags |= (sizeFlags & ~0x40000004);
        linkedSymbol->permissions |= (sizeFlags & ~0x40000004);
    }
    symbol->header = Initialize_Specific(symbol, dataMode, codeMode, linkedSymbol, auxiliary);
}

void InitElfSections(void) {
    ObjGenSection* dataSection;
    SectionRec* section;
    ObjGenSection* associatedRecord;

    section_table_dirty = TRUE;
    DoElfSystemSection("", SectionKind_Null, 0, 0);

    section = Initialize_Input(".text", ".text", AM_FAR_ABS, AM_CODE_PC_REL, 1);
    DoElfContentSection(section, SectionKind_Content, 4, 20000, 1000, 6, 1, 1);

    default_code_section = copts.textSection;
    section = Initialize_Input(".init", ".init", AM_FAR_ABS, AM_CODE_PC_REL, 0);
    DoElfContentSection(section, SectionKind_Content, 4, 100, 100, 6, 1, 1);

    section = Initialize_Input(".fini", ".fini", AM_FAR_ABS, AM_CODE_PC_REL, 0);
    DoElfContentSection(section, SectionKind_Content, 4, 100, 100, 6, 1, 1);

    section = Initialize_Input(".ctors", NULL, AM_FAR_ABS, AM_CODE_PC_REL, 0);
    DoElfContentSection(section, SectionKind_Ctors, 4, 4, 4, 3, 0, 1);

    section = Initialize_Input(".dtors", NULL, AM_FAR_ABS, AM_CODE_PC_REL, 0);
    DoElfContentSection(section, SectionKind_Dtors, 4, 4, 4, 3, 0, 1);

    section = Initialize_Input("extab", NULL, AM_FAR_ABS, AM_CODE_PC_REL, 0);
    DoElfContentSection(section, SectionKind_Extab, 4, 0x40, 0x40, 2, 0, 1);

    section = Initialize_Input("extabindex", NULL, AM_FAR_ABS, AM_CODE_PC_REL, 0);
    DoElfContentSection(section, SectionKind_ExtabIndex, 4, 0x40, 0x40, 2, 0, 1);

    section = Initialize_Input(".rodata", ".rodata", AM_FAR_ABS, AM_CODE_PC_REL, 4);
    DoElfContentSection(section, SectionKind_Content, 8, 1000, 1000, 2, 1, 1);

    default_const_section = copts.constSection;
    section = Initialize_Input(".data", ".bss", AM_FAR_ABS, AM_CODE_PC_REL, 2);
    dataSection = DoElfContentSection(section, SectionKind_Content, 8, 4000, 1000, 3, 1, 1);

    default_data_section = copts.dataSection;
    associatedRecord = InitElfSection("COMM", SectionKind_Common, 1, 0, NULL, 3, NULL);
    if (copts.common) {
        dataSection->header->auxiliary = associatedRecord;
    }
    ELFfile.data_section_linked_symbol = dataSection->header->linkedSymbol;

    section = Initialize_Input(".line", NULL, AM_NONE, AM_NONE, 0);
    DoElfContentSection(section, SectionKind_Line, 1, 1000, 1000, 0, 0, 0);

    section = Initialize_Input(".debug", NULL, AM_NONE, AM_NONE, 0);
    DoElfContentSection(section, SectionKind_Debug, 4, 5000, 1000, 0, 0, 0);

    section = Initialize_Input(".sdata", ".sbss", AM_SDATA, AM_CODE_PC_REL, 8);
    DoElfContentSection(section, SectionKind_Content, 8, 1000, 1000, 3, 0, 1);

    default_sdata_section = copts.sdataSection;
    section = Initialize_Input(".sdata2", ".sbss2", AM_SDATA2, AM_CODE_PC_REL, 0x10);
    DoElfContentSection(section, SectionKind_Content, 8, 1000, 1000, 3, 0, 1);

    default_sconst_section = copts.sconstSection;
    section = Initialize_Input(".PPC.EMB.sdata0", ".PPC.EMB.sbss0", AM_SDATA0, AM_CODE_PC_REL, 0);
    DoElfContentSection(section, SectionKind_Content, 8, 1000, 1000, 3, 0, 1);

    DoElfSystemSection(".symtab", SectionKind_Symtab, 4, 1000);
    DoElfSystemSection(".strtab", SectionKind_Strtab, 1, 1000);
    DoElfSystemSection(".shstrtab", SectionKind_Shstrtab, 1, 300);
    DoElfSystemSection(".comment", SectionKind_Comment, 1, 0x40);
    DoElfSystemSection("", SectionKind_Abs, 1, 0);

    CodeGen_PragmaPush();
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -9
#endif
short GetCurrentSection(short kind) {
    unsigned short value;
    switch (kind) {
        case SECT_TEXT:
            value = copts.textSection->header->index;
            break;
        case SECT_DATA:
            value = copts.dataSection->header->index;
            break;
        case SECT_CONST_TYPE:
            value = copts.constSection->header->index;
            break;
        case SECT_SCONST_TYPE:
            value = copts.sconstSection->header->index;
            break;
        case SECT_SDATA_TYPE:
            value = copts.sdataSection->header->index;
            break;
        default:
            CError_FATAL(1117);
    }
    return value;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -8
#endif
#if VERSION < VERSION_GC_2_0
void PPC_EABI_SetSection(Object* object, SInt32 size, Boolean isConst) {
#else
void PPC_EABI_SetSection(Object* object, SInt32 size, Boolean isConst, Boolean allowSmall) {
#endif
    InterruptGenerationRecord* rec;
    SInt16 section;

    rec = NULL;
    section = object->section;
    if (object == rt_ptmf_null && size == 0) {
        size = 12;
    }
    if (section > 0) {
        return;
    }
    if (section < 0) {
        rec = resolve_object_pseudo_section(object, MakeSymbolTableEntry(object));
        switch (rec->kind) {
            case InterruptRecord_Interrupt:
            case InterruptRecord_DeclSpec:
                if (rec->sectionIndex != 0) {
                    return;
                }
                break;
            case 0:
            default:
                CError_FATAL(1187);
            case InterruptRecord_SectionSet:
                break;
        }
        section = 0;
    }
    if (object->datatype == DDATA) {
        if (object->type && IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(object->type) && !copts.use_fp) {
            PPCError_Error(PPCERR_SYMBOL_NEEDS_FLOAT, CMangler_GetLinkName(object)->name);
        }
        if (object->name &&
            object->name->name[1] == '_' &&
            strncmp(object->name->name, "__vt__", 6) == 0) {
            size = object->type->size;
        }
#if VERSION >= VERSION_GC_2_0
        if (!allowSmall) {
            if ((size <= copts.sdata2_threshold && size <= copts.sdata_threshold) ||
                (!isConst && !is_const_object(object)) ||
                isConst) {
                allowSmall = TRUE;
            }
        }
#endif
        if (isConst) {
            if (size == 0) {
                section = rec == NULL ? copts.constSection->header->index : rec->constSection;
            } else if (
#if VERSION >= VERSION_GC_2_0
                allowSmall &&
#endif
                copts.sdata2_threshold && size <= copts.sdata2_threshold) {
                section = rec == NULL ? copts.sconstSection->header->index : rec->sconstSection;
            } else {
                section = rec == NULL ? copts.constSection->header->index : rec->constSection;
            }
        } else {
            if (size == 0) {
                section = rec == NULL ? copts.dataSection->header->index : rec->dataSection;
            } else if (
#if VERSION >= VERSION_GC_2_0
                allowSmall &&
#endif
                copts.sdata_threshold && size <= copts.sdata_threshold) {
                section = rec == NULL ? copts.sdataSection->header->index : rec->sdataSection;
            } else {
                section = rec == NULL ? copts.dataSection->header->index : rec->dataSection;
            }
        }
    } else if (object->datatype == DFUNC ||
               object->datatype == DVFUNC ||
               object->datatype == DINLINEFUNC) {
        section = rec == NULL ? copts.textSection->header->index : rec->textSection;
    }
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -20
#endif
    if (rec != NULL) {
        switch (rec->kind) {
            case InterruptRecord_Interrupt:
            case InterruptRecord_DeclSpec:
                rec->sectionIndex = section;
                break;
            case 0:
            default:
                CError_FATAL(1240);
            case InterruptRecord_SectionSet:
                break;
        }
    }
    object->section = section;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -20
#endif
int base_register(Object* obj) {
    switch ((AddressingMode)ObjGen_GetAddressing(obj)) {
        case AM_FAR_ABS:
        case AM_NEAR_ABS:
        case AM_SDATA0:
        case AM_CODE_PC_REL:
        case AM_CODE_NEAR_ABS:
        case AM_CODE_FAR_ABS:
            return 0;
        case AM_SDATA:
            return 13;
        case AM_UNUSED_3:
        case AM_UNUSED_4:
        case AM_SDATA2:
            return 2;
        case AM_LOCAL:
            return local_base_register(obj);
        default:
            ObjGen_GetAddressing(obj);
            CError_FATAL(1287);
            return 2;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -20
#endif
static ObjGenSection* select_object_section(
    Object* obj, SInt32 size, Boolean initialized, Boolean noCommon, Boolean isConst
) {
    ObjGenSection* section;
    ObjGenSection* linked;
    SInt32 id;
    SectionSymbolAttributes* entry;
    Boolean hasAuxiliary = FALSE;

#if VERSION < VERSION_GC_2_0
    PPC_EABI_SetSection(obj, size, isConst);
#else
    PPC_EABI_SetSection(obj, size, isConst, TRUE);
#endif
    id = pseudo_section_index(obj->section);
    if (section_table_dirty) {
        CodeGen_PragmaPush();
    }
    for (entry = ELFfile.section_symbol_attributes; entry; entry = entry->next) {
        if ((SInt16)entry->index == id) {
            break;
        }
    }
    CError_ASSERT(1301, entry);
    section = entry->target;
    CError_ASSERT(1303, section);

    if (!initialized) {
        if (!(linked = entry->linkedSymbol)) {
            PPCError_Error(PPCERR_SECTION_NEEDS_BSS_FOR_OBJECT, section->name, obj->name->name);
            return section;
        }
        hasAuxiliary = entry->auxiliary != NULL;
        section = linked;
    }

    if (!is_16bitdata(obj) && !noCommon && hasAuxiliary && !CParser_HasInternalLinkage(obj)) {
        return ELFfile.common_section;
    }

    if (section->symbolLink) {
        if (!section->buffer.size &&
            !section->symbolLink->object &&
            !(obj->qual & Q_IMPLICIT_WEAK) &&
            !(obj->qual & Q_WEAK)) {
            section->symbolLink->symbol = CreateDataPoolAnchorSymbol(section, obj);
        }
        obj->toc = section->symbolLink->object;
    }
    return section;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static void report_section_permission_conflict(
    Object* object, ObjGenSection* section, char* kind, UInt32 permissions
) {
    UInt32 requested;
    char* p;
    char current[4];
    char changed[4];

    p = current;
    if (section->permissions & 2) {
        *p++ = 'R';
    }
    if (section->permissions & 1) {
        *p++ = 'W';
    }
    if (section->permissions & 4) {
        *p++ = 'X';
    }
    *p = 0;

    p = changed;
    requested = section->shdr.sh_flags | permissions;
    if (requested & 2) {
        *p++ = 'R';
    }
    if (requested & 1) {
        *p++ = 'W';
    }
    if (requested & 4) {
        *p++ = 'X';
    }
    *p = 0;

    PPCError_Warning(
        PPCERR_SECTION_PERMISSION_CHANGED,
        kind,
        CMangler_GetLinkName(object)->name,
        section->name,
        current,
        changed
    );
}

static void allocate_object_storage(Object* object, SInt32 size, Boolean isConst) {
    ObjGenSection* section;
    BE_SymNode* sym;
    BufferUpdate* update;

    if (MakeSymbolTableEntry(object)->elf.st_size) {
        return;
    }
    section = select_object_section(object, size, FALSE, FALSE, isConst);
    if (!isConst && !(section->permissions & 1)) {
        report_section_permission_conflict(object, section, "RW", 1);
    }
    sym = AddObject(object, size, section);
    if ((UInt32)sym->alignment > section->shdr.sh_addralign) {
        section->shdr.sh_addralign = sym->alignment;
    }
    if (section->kind == SectionKind_Common) {
        sym->elf.st_value = sym->alignment;
    } else if (section->kind != SectionKind_BSS) {
        SectionAlign(&section->buffer, sym->alignment);
        sym->elf.st_value = section->buffer.size;
        AppendGListNoData(&section->buffer, sym->elf.st_size);
        memset(*section->buffer.data + sym->elf.st_value, 0, sym->elf.st_size);
    } else {
        update = galloc(sizeof(BufferUpdate));
        if (!pending_buffer_updates) {
            pending_buffer_updates = update;
        } else {
            pending_data_tail->next = update;
        }
        update->next = NULL;
        update->target = sym;
        update->active = TRUE;
        update->suppressed = FALSE;
        pending_data_tail = update;
        if (copts.pool_data &&
            section->symbolLink &&
            section->symbolLink->symbol &&
            !(object->qual & Q_IMPLICIT_WEAK) &&
            !(object->qual & Q_WEAK) &&
            !(object->datatype == DFUNC ||
              object->datatype == DVFUNC ||
              object->datatype == DINLINEFUNC) &&
            !is_16bitdata(object)) {
            SectionAlign(&section->buffer, sym->alignment);
            sym->elf.st_value = section->buffer.size;
            AppendGListNoData(&section->buffer, sym->elf.st_size);
            update->suppressed = TRUE;
        }
    }
    object_storage_size += size;
}

static void emit_object_data_and_relocations(
    Object* object, const char* data, OLinkList* list, SInt32 size, Boolean isConst
) {
    ObjGenSection* section;
    Object* target;
    BE_SymNode* sym;
    ObjGenRelocation* reloc;
    ObjGenSection* relocations;
    BufferUpdate* update;
    SInt32 addend;
    Elf32_Rela rela;
    SInt32 offset;

    section = select_object_section(object, size, TRUE, FALSE, isConst);
    if (!isConst && !(section->permissions & 1)) {
        report_section_permission_conflict(object, section, "RW", 1);
    }
    sym = AddObject(object, size, section);
    if ((UInt32)sym->alignment > section->shdr.sh_addralign) {
        section->shdr.sh_addralign = sym->alignment;
    }
    SectionAlign(&section->buffer, sym->alignment);
    sym->elf.st_value = offset = section->buffer.size;
    AppendGListData(&section->buffer, data, sym->elf.st_size);
    relocations = section->relocations;
    while (list) {
        target = list->obj;
        relocations->relocationCount++;
        reloc = galloc(sizeof(ObjGenRelocation));
        reloc->symbol = MakeSymbolTableEntry(target);
        addend = 0;
        if (copts.pool_data &&
            reloc->symbol->section->symbolLink &&
            reloc->symbol->section->symbolLink->symbol &&
            !(target->qual & Q_IMPLICIT_WEAK) &&
            !(target->qual & Q_WEAK) &&
            !(target->datatype == DFUNC ||
              target->datatype == DVFUNC ||
              target->datatype == DINLINEFUNC) &&
            !is_16bitdata(target) &&
            GetPooledObjectRegister(target)) {
            reloc->symbol->elf.st_other |= 0x80;
            addend = reloc->symbol->elf.st_value;
            reloc->symbol = reloc->symbol->section->symbolLink->symbol;
            reloc->symbol->elf.st_other &= 0xbf;
        }
        reloc->kind = R_PPC_ADDR32;
        reloc->section = relocations;
        reloc->index = relocations->relocationCount - 1;
        rela.r_offset = offset + list->offset;
        rela.r_addend = addend + list->addend;
        rela.r_info = R_PPC_ADDR32;
        AppendGListData(&relocations->buffer, &rela, sizeof(rela));
        reloc->next = allrelocations;
        allrelocations = reloc;
        list = list->next;
    }
    for (update = pending_buffer_updates; update; update = update->next) {
        if (update->target == sym) {
            if (update->active && (sym->elf.st_other & 0x80)) {
                PPCError_Error(PPCERR_POOLED_DATA_REDEFINED);
            }
            update->active = FALSE;
            break;
        }
    }
    object_data_size += size;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -20
#endif
void ObjGen_DeclareFloatConst(Object* object) {
    ObjGenSection* section;
    BE_SymNode* sym;

    section = select_object_section(object, object->type->size, TRUE, FALSE, TRUE);
    sym = AddObject(object, object->type->size, section);
    if ((UInt32)sym->alignment > section->shdr.sh_addralign) {
        section->shdr.sh_addralign = sym->alignment;
    }
    SectionAlign(&section->buffer, sym->alignment);
    sym->elf.st_value = section->buffer.size;
    if (object->type->size == 4) {
        float f = object->u.data.u.floatconst->data.value;
        CTool_EndianConvertMem(&f, 4);
        AppendGListData(&section->buffer, &f, 4);
    } else if (object->type->size == 8) {
        double f = object->u.data.u.floatconst->data.value;
        CTool_EndianConvertMem(&f, 8);
        AppendGListData(&section->buffer, &f, 8);
    } else {
        CError_FATAL(1561);
    }
    object_data_size += object->type->size;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -20
#endif
void ObjGen_DeclareSwitchTable(Object* tableobj, Object* funcobj) {
    UInt32 i;
    ObjGenRelocation* reloc;
    SInt32 index;
    Elf32_Rela rela;
    SInt32 offset;
    ObjGenSection* relocations;
    ObjGenSection* section;
    BE_SymNode* sym;
    BE_SymNode* funcsym;

    CError_ASSERT(1662, ok_to_compress);
    section = select_object_section(
        tableobj, tableobj->u.data.u.switchtable.size * 4, TRUE, FALSE, FALSE
    );
    sym = AddObject(tableobj, tableobj->u.data.u.switchtable.size * 4, section);
    funcsym = MakeSymbolTableEntry(funcobj);
    SectionAlign(&section->buffer, sym->alignment);
    sym->elf.st_value = offset = section->buffer.size;
    index = 0;
    relocations = section->relocations;
    for (i = tableobj->u.data.u.switchtable.size; i; i--) {
        relocations->relocationCount++;
        reloc = galloc(sizeof(ObjGenRelocation));
        reloc->symbol = funcsym;
        if (copts.pool_data &&
            reloc->symbol->section->symbolLink &&
            reloc->symbol->section->symbolLink->symbol &&
            !(funcobj->qual & Q_IMPLICIT_WEAK) &&
            !(funcobj->qual & Q_WEAK) &&
            !(funcobj->datatype == DFUNC ||
              funcobj->datatype == DVFUNC ||
              funcobj->datatype == DINLINEFUNC) &&
            !is_16bitdata(funcobj) &&
            GetPooledObjectRegister(funcobj)) {
            reloc->symbol = reloc->symbol->section->symbolLink->symbol;
        }
        reloc->kind = R_PPC_ADDR32;
        reloc->section = relocations;
        reloc->index = relocations->relocationCount - 1;
        rela.r_offset = offset + index;
        rela.r_addend = *(SInt32*)(tableobj->u.data.u.switchtable.data + index);
        rela.r_addend = CTool_EndianConvertWord32(rela.r_addend);
        *(SInt32*)(tableobj->u.data.u.switchtable.data + index) = 0;
        rela.r_info = R_PPC_ADDR32;
        AppendGListData(&relocations->buffer, &rela, sizeof(rela));
        reloc->next = allrelocations;
        allrelocations = reloc;
        index += 4;
    }
    AppendGListData(
        &section->buffer,
        tableobj->u.data.u.switchtable.data,
        tableobj->u.data.u.switchtable.size * 4
    );
    object_data_size += sym->elf.st_size;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -20
#endif
void ObjGen_DeclareCodeLabel(Object* labelobj, SInt32 offset, Object* funcobj) {
    CError_FATAL(1718);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -20
#endif
ObjGenSection* ObjGen_DeclareCode(Object* object, SInt32 size) {
    BE_SymNode* sym;
    ObjGenSection* section;

    CError_ASSERT(1754, ok_to_compress);
    last_function_section = section = select_object_section(object, size, TRUE, TRUE, FALSE);
    if (!(section->permissions & 4)) {
        report_section_permission_conflict(object, section, "RX", 4);
    }
    if (section->flags && !(section->permissions & 0x40000000)) {
        CError_FATAL(1761);
    }
#if VERSION >= VERSION_GC_2_0
    if (copts.prepare_compress && !section->flags) {
        PPCError_Error(PPCERR_MIXED_COMPRESSED_SECTIONS);
    }
#endif
    if (interrupt_routine_flags &&
        !(interrupt_routine_flags->flags & Interrupt_NoWarn) &&
        size > 256) {
        PPCError_Warning(PPCERR_INTERRUPT_FUNCTION_TOO_BIG, object, size);
    }
    sym = AddFunction(object, size, section);
    if ((UInt32)sym->alignment > section->shdr.sh_addralign) {
        section->shdr.sh_addralign = sym->alignment;
    }
    SectionAlign(&section->buffer, sym->alignment);
    sym->elf.st_value = section->buffer.size;
    if (copts.cats) {
        MWCATS_AddProc(object, 0);
    }
    objcodesize += size;
    last_base_offset = section->buffer.size;
    return section;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -23
#endif
void ObjGen_DeclareEntry(Object* object, SInt32 offset) {
    BE_SymNode* record;

    CError_ASSERT(1800, ok_to_compress);
    record = AddEntry(object, last_function_section);
    record->elf.st_value = last_base_offset + offset;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -18
#endif
static void emit_relocation(SInt32 type, SInt32 offset, Object* obj, void* section, SInt32 value) {
    ObjGenSection* relocations;
    ObjGenRelocation* reloc;
    Elf32_Rela record;

    relocations = ((ObjGenSection*)section)->relocations;
    relocations->relocationCount++;
    reloc = galloc(sizeof(ObjGenRelocation));
    reloc->symbol = MakeSymbolTableEntry(obj);
#if VERSION < VERSION_GC_2_0
    if (is_pooled_object(obj) && type != 14 && type != 15) {
        reloc->symbol = reloc->symbol->section->symbolLink->symbol;
    }
#endif
    offset += last_base_offset;
    reloc->kind = -1;
    record.r_addend = value;
    if (type == 3) {
        if (is_SDAdata(obj)) {
            reloc->kind = R_PPC_EMB_SDA21;
            offset += copts.littleendian ? 0 : copts.rel109_offset;
        } else {
            reloc->kind = R_PPC_ADDR16;
            offset += copts.littleendian ? 0 : 2;
        }
    } else if (type == 2) {
        reloc->kind = R_PPC_REL24;
    } else if (type == 10) {
        reloc->kind = R_PPC_ADDR24;
    } else if (type == 8) {
        reloc->kind = R_PPC_REL14;
    } else if (type == 13) {
        reloc->kind = R_PPC_ADDR14;
    } else if (type == 4) {
        reloc->kind = R_PPC_ADDR16_LO;
        offset += copts.littleendian ? 0 : 2;
    } else if (type == 9) {
        reloc->kind = -3;
        CError_FATAL(1857);
    } else if (type == 11) {
        reloc->kind = -4;
        CError_FATAL(1860);
    } else if (type == 5) {
        reloc->kind = R_PPC_ADDR16_LO;
        offset += copts.littleendian ? 0 : 2;
    } else if (type == 6) {
        reloc->kind = R_PPC_ADDR16_HI;
        offset += copts.littleendian ? 0 : 2;
    } else if (type == 7) {
        reloc->kind = R_PPC_ADDR16_HA;
        offset += copts.littleendian ? 0 : 2;
    } else if (type == 15) {
        reloc->kind = R_PPC_ADDR16_HA;
        offset += copts.littleendian ? 0 : 2;
    } else if (type == 14) {
        reloc->kind = R_PPC_ADDR16_LO;
        offset += copts.littleendian ? 0 : 2;
    } else {
        CError_FATAL(1877);
    }
    reloc->section = relocations;
    reloc->index = relocations->relocationCount - 1;
    record.r_offset = offset;
    record.r_info = reloc->kind & 0xff;
    AppendGListData(&relocations->buffer, &record, sizeof(record));
    reloc->next = allrelocations;
    allrelocations = reloc;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -18
#endif
void ObjGen_SourceRef(UInt32 line, UInt32 offset) {
    SInt32 sline;
    SInt32 soffset;
    CError_ASSERT(1898, ok_to_compress);
    sline = line;
    if (sline <= CurFile->lastLine) {
        return;
    }
    soffset = offset;
    if (soffset <= last_line_offset) {
        if (soffset == last_line_offset) {
            correctDWARF_line(CurFile->lastLine = sline, soffset + last_base_offset);
        }
        return;
    }
    newDWARF_line(CurFile->lastLine = sline, soffset + last_base_offset);
    last_line_offset = soffset;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

SInt16 update_section(Object* func) {
    last_function_section = select_object_section(func, 0, TRUE, TRUE, FALSE);
#if VERSION < VERSION_GC_2_0
    PPC_EABI_SetSection(func, 0, FALSE);
#else
    PPC_EABI_SetSection(func, 0, FALSE, TRUE);
#endif
    return pseudo_section_index(func->section);
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -18
#endif
void ObjGen_DeclareSymInfo(void) {
    CError_ASSERT(1954, ok_to_compress);
    SymContainedVars();
    endDWARF_function();
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -18
#endif
void ObjGen_DeclareExceptionTables(
    Object* obj, SInt32 codesize, void* data, SInt32 size, OLinkList* list
) {
    Object* object;
    ObjGenSection* section;
    ObjGenSection* relocations;
    ObjGenRelocation* relocation;
    BE_SymNode* symbol;
    ObjGenSection* recordSection;
    Object* target;
    SInt32 base;
    BE_SymNode* dataSymbol;
    SInt32 dataAddend;
    SInt32 objectAddend;
    SInt32 recordOffset;
    ObjGenSymbolLink* link;
    Elf32_Rela record;
    ExtabIndexEntry descriptor;
    SInt32 zero = 0;

    CError_ASSERT(1983, ok_to_compress);
    if (size != 4) {
        object = galloc(sizeof(*object));
        memset(object, 0, sizeof(*object));
        object->sclass = TK_STATIC;
        object->name = CParser_GetUniqueName();
        CMangler_GetLinkName(object);
        object->datatype = DDATA;
        object->section = ELFfile.extab_section->header->index;
        section = ELFfile.extab_section;
        dataSymbol = MakeSymbolTableEntry(object);
        dataSymbol->elf.st_info = ELF32_ST_INFO(STB_LOCAL, STT_OBJECT);
        dataSymbol->elf.st_size = size;
        dataSymbol->elf.st_shndx = SHN_UNDEF;
        dataSymbol->section = section;
        dataSymbol->alignment = 4;
        dataSymbol->elf.st_other = 2;
        SectionAlign(&section->buffer, dataSymbol->alignment);
        dataSymbol->elf.st_value = base = section->buffer.size;
        AppendGListData(&section->buffer, data, size);
        if (size & 3) {
            AppendGListData(&section->buffer, &zero, ((size + 3) & ~3u) - size);
        }
        relocations = section->relocations;
        for (; list != NULL; list = list->next) {
            target = list->obj;
            relocations->relocationCount++;
            relocation = galloc(sizeof(*relocation));
            relocation->symbol = MakeSymbolTableEntry(target);
            relocation->kind = R_PPC_ADDR32;
            relocation->section = relocations;
            relocation->index = relocations->relocationCount - 1;
            record.r_offset = base + list->offset;
            record.r_addend = 0;
            record.r_info = R_PPC_ADDR32;
            AppendGListData(&relocations->buffer, &record, sizeof(record));
            relocation->next = allrelocations;
            allrelocations = relocation;
        }
        object_data_size += size;
    }

    object = galloc(sizeof(*object));
    memset(object, 0, sizeof(*object));
    object->sclass = TK_STATIC;
    object->name = CParser_GetUniqueName();
    CMangler_GetLinkName(object);
    object->datatype = DDATA;
    object->section = ELFfile.extabindex_section->header->index;
    recordSection = ELFfile.extabindex_section;
    symbol = MakeSymbolTableEntry(object);
    symbol->elf.st_info = ELF32_ST_INFO(STB_LOCAL, STT_OBJECT);
    symbol->elf.st_size = sizeof(descriptor);
    symbol->elf.st_shndx = SHN_UNDEF;
    symbol->section = recordSection;
    symbol->alignment = 4;
    symbol->elf.st_other = 2;
    SectionAlign(&recordSection->buffer, symbol->alignment);
    recordOffset = recordSection->buffer.size;
    symbol->elf.st_value = recordOffset;
    descriptor.function = 0;
    descriptor.size = CTool_EndianConvertWord32((codesize & 0x7fffffff) | ((size == 4) << 31));
    descriptor.extab = (size == 4) ? *(SInt32*)data : 0;
    AppendGListData(&recordSection->buffer, &descriptor, sizeof(descriptor));
    relocations = recordSection->relocations;
    relocations->relocationCount++;
    relocation = galloc(sizeof(*relocation));
    relocation->symbol = MakeSymbolTableEntry(obj);
    objectAddend = 0;
    if (copts.pool_data &&
        (link = relocation->symbol->section->symbolLink) != NULL &&
        link->symbol &&
        !(obj->qual & Q_IMPLICIT_WEAK) &&
        !(obj->qual & Q_WEAK) &&
        !(obj->datatype == DFUNC || obj->datatype == DVFUNC || obj->datatype == DINLINEFUNC) &&
        !is_16bitdata(obj) &&
        GetPooledObjectRegister(obj)) {
        relocation->symbol->elf.st_other |= 0x80;
        objectAddend = relocation->symbol->elf.st_value;
        relocation->symbol = relocation->symbol->section->symbolLink->symbol;
        relocation->symbol->elf.st_other &= 0xbf;
    }
    relocation->kind = R_PPC_ADDR32;
    relocation->section = relocations;
    relocation->index = relocations->relocationCount - 1;
    record.r_offset = recordOffset;
    record.r_addend = objectAddend;
    record.r_info = R_PPC_ADDR32;
    AppendGListData(&relocations->buffer, &record, sizeof(record));
    relocation->next = allrelocations;
    allrelocations = relocation;

    if (size != 4) {
        relocations->relocationCount++;
        relocation = galloc(sizeof(*relocation));
        relocation->symbol = dataSymbol;
        dataAddend = 0;
        if (copts.pool_data &&
            (link = relocation->symbol->section->symbolLink) != NULL &&
            link->symbol &&
            !(object->qual & Q_IMPLICIT_WEAK) &&
            !(object->qual & Q_WEAK) &&
            !(object->datatype == DFUNC ||
              object->datatype == DVFUNC ||
              object->datatype == DINLINEFUNC) &&
            !is_16bitdata(object) &&
            GetPooledObjectRegister(object)) {
            relocation->symbol->elf.st_other |= 0x80;
            dataAddend = relocation->symbol->elf.st_value;
            relocation->symbol = relocation->symbol->section->symbolLink->symbol;
            relocation->symbol->elf.st_other &= 0xbf;
        }
        relocation->kind = R_PPC_ADDR32;
        relocation->section = relocations;
        relocation->index = relocations->relocationCount - 1;
        recordOffset += 8;
        record.r_offset = recordOffset;
        record.r_addend = dataAddend;
        record.r_info = R_PPC_ADDR32;
        AppendGListData(&relocations->buffer, &record, sizeof(record));
        relocation->next = allrelocations;
        allrelocations = relocation;
    }
    object_data_size += sizeof(descriptor);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

GList* ObjGen_GetSectionGList(ObjGenSection* section) {
    return &section->buffer;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -18
#endif
void ObjGen_RelocateObj(
    ObjGenSection* section, SInt32 offset, Object* object, UInt8 type, SInt32 addend
) {
    CError_ASSERT(2169, ok_to_compress);
    emit_relocation(type, offset, object, section, addend);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -18
#endif
void declareinit(Object* object) {
    Elf32_Rela rela;
    UInt32 zero;
    ObjGenSection* section;
    ObjGenSection* relocations;
    ObjGenRelocation* reloc;
    UInt32 offset;

    zero = CTool_EndianConvertWord32(0);
    section = ELFfile.ctors_section;
    CError_ASSERT(2255, section);
    offset = section->buffer.size;
    AppendGListData(&section->buffer, &zero, 4);
    relocations = section->relocations;
    relocations->relocationCount++;
    reloc = galloc(sizeof(ObjGenRelocation));
    reloc->symbol = MakeSymbolTableEntry(object);
    reloc->kind = R_PPC_ADDR32;
    reloc->section = relocations;
    reloc->index = relocations->relocationCount - 1;
    rela.r_offset = offset;
    rela.r_addend = 0;
    rela.r_info = reloc->kind & 0xff;
    AppendGListData(&relocations->buffer, &rela, sizeof(rela));
    reloc->next = allrelocations;
    allrelocations = reloc;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -18
#endif
short pseudo_section_index(short key) {
    InterruptGenerationRecord* record;

    if (key < 0) {
        record = CodeGen_FindInterruptGenerationRecord(key);
        if (record != NULL) {
            switch (record->kind) {
                case InterruptRecord_Interrupt:
                case InterruptRecord_DeclSpec:
                    return record->sectionIndex;
                case 0:
                case InterruptRecord_SectionSet:
                default:
                    CError_FATAL(2301);
            }
        }
    }
    return key;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

InterruptGenerationRecord* get_current_pseudo_section_info(void) {
    InterruptGenerationRecord* rec;

    rec = CodeGen_FindInterruptGenerationRecord(previous_ps_ID);
    if (rec &&
        rec->textSection == copts.textSection->header->index &&
        rec->dataSection == copts.dataSection->header->index &&
        rec->constSection == copts.constSection->header->index &&
        rec->sconstSection == copts.sconstSection->header->index &&
        rec->sdataSection == copts.sdataSection->header->index) {
        return rec;
    }
    rec = galloc(sizeof(InterruptGenerationRecord));
    memclrw(rec, sizeof(InterruptGenerationRecord));
    rec->next = interrupt_generation_records;
    interrupt_generation_records = rec;
    previous_ps_ID--;
    rec->id = previous_ps_ID;
    rec->kind = InterruptRecord_SectionSet;
    rec->textSection = copts.textSection->header->index;
    rec->dataSection = copts.dataSection->header->index;
    rec->constSection = copts.constSection->header->index;
    rec->sconstSection = copts.sconstSection->header->index;
    rec->sdataSection = copts.sdataSection->header->index;
    return rec;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -18
#endif
static InterruptGenerationRecord* resolve_object_pseudo_section(Object* obj, BE_SymNode* sym) {
    InterruptGenerationRecord* rec;
    SInt32 id;
    SectionSymbolAttributes* entry;
    InterruptGenerationRecord* prev;

    rec = CodeGen_FindInterruptGenerationRecord(obj->section);
    if (rec) {
        switch (rec->kind) {
            case InterruptRecord_Interrupt:
            case InterruptRecord_DeclSpec:
                CError_ASSERT(2354, rec->sectionIndex);
                if (!(prev = sym->interruptInfo)) {
                    sym->interruptInfo = rec;
                    obj->section = rec->sectionIndex;
                    return rec;
                }
                if (!prev->sectionIndex) {
                    obj->section = prev->sectionIndex = rec->sectionIndex;
                }
                if (*(volatile UInt16*)&prev->sectionIndex != rec->sectionIndex) {
                    id = pseudo_section_index(prev->sectionIndex);
                    if (section_table_dirty) {
                        CodeGen_PragmaPush();
                    }
                    for (entry = ELFfile.section_symbol_attributes; entry; entry = entry->next) {
                        if ((SInt16)entry->index == id) {
                            break;
                        }
                    }
                    CError_ASSERT(2374, entry);
                    CError_ASSERT(2375, entry->target);
                    PPCError_Error(
                        PPCERR_DEFINED_IN_OTHER_SECTION, sym->name->name, entry->target->name
                    );
                }
                obj->section = prev->sectionIndex;
                return prev;
            case InterruptRecord_SectionSet:
                break;
            case 0:
            default:
                CError_FATAL(2384);
        }
    }
    return rec;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -18
#endif
InterruptGenerationRecord* GetInterruptRoutineFlags(Object* obj) {
    InterruptGenerationRecord* info;

    info = MakeSymbolTableEntry(obj)->interruptInfo;
    CError_ASSERT(2396, info);
    return info;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static void create_main_file_object(void) {
    Boolean savedFileSymInfo;
    UInt8 sourceName[256];
    char objectName[256];
    Object* obj;
    char* cursor;

    savedFileSymInfo = copts.filesyminfo;
    COS_FileGetFSSpecInfo(&cparamblkptr->sourcefile, NULL, NULL, sourceName);
    sprintf(objectName, "__%.*s", sourceName[0], sourceName + 1);

    cursor = &objectName[sourceName[0] + 1];
    while (*cursor) {
        if (*cursor == '.') {
            cursor[0] = '_';
            cursor[1] = 'o';
            cursor[2] = 0;
            break;
        }
        cursor--;
    }
    for (cursor = objectName + 1; *cursor; cursor++) {
        if (*cursor == '.') {
            *cursor = '_';
        }
    }

    obj = (Object*)galloc(sizeof(Object));
    memclrw(obj, sizeof(Object));
    obj->otype = OT_OBJECT;
    obj->type = (Type*)&stchar;
    obj->name = GetHashNameNodeExport(objectName);
    obj->sclass = TK_EXTERN;
    obj->datatype = DDATA;
#if VERSION < VERSION_GC_2_0
    PPC_EABI_SetSection(obj, obj->type->size, FALSE);
#else
    PPC_EABI_SetSection(obj, obj->type->size, FALSE, TRUE);
#endif
    MakeSymbolTableEntry(obj);
    copts.filesyminfo = FALSE;
    ObjGen_DeclareData(obj, NULL, NULL, obj->type->size);
    copts.filesyminfo = savedFileSymInfo;
}

void MWCATS_AddProc(Object* object, UInt8 flags) {
    ObjGenSection* section;
    ObjGenSection* catsSection;
    CGList* list;
    SectionRec* rec;
    char* name;

    section = select_object_section(object, 0, TRUE, TRUE, FALSE);
    if (!section->output) {
        name = galloc(strlen(section->name) + 8);
        strcpy(name, ".mwcats");
        strcat(name, section->name);
        rec = Initialize_Input(name, NULL, 0, 0, 0);
        catsSection = DoElfContentSection(rec, SectionKind_MWCATS, 4, 5000, 1000, 0, 0, 0);
        section->output = catsSection;
        list = galloc(sizeof(CGList));
        catsSection->sectionData.attributes = list;
        memset(list, 0, sizeof(CGList));
        list->sourceSection = section;
    }
    list = section->output->sectionData.attributes;
    if (!list->tail) {
        list->tail = galloc(sizeof(SectionAttributeNode));
        list->head = list->tail;
    } else {
        if (list->tail->object == object) {
            list->tail->flags |= flags;
            return;
        }
        list->tail->next = galloc(sizeof(SectionAttributeNode));
        list->tail = list->tail->next;
    }
    memset(list->tail, 0, sizeof(SectionAttributeNode));
    list->tail->object = object;
    list->tail->flags = flags;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -18
#endif
void MWCATS_AddExit(ObjGenSection* section, unsigned int offset) {
    SectionAttributeNode* proc = section->output->sectionData.attributes->tail;

    if (!proc->flags) {
        if (!proc->lastExit) {
            proc->lastExit = galloc(sizeof(MWCATSExit));
            proc->exits = proc->lastExit;
        } else {
            proc->lastExit->next = galloc(sizeof(MWCATSExit));
            proc->lastExit = proc->lastExit->next;
        }
        memset(proc->lastExit, 0, sizeof(MWCATSExit));
        proc->lastExit->offset = offset;
        if (proc->exitCount > 32767) {
            CError_FATAL(2528);
        }
        proc->exitCount++;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

void MWCATS_WriteFunction(Object* func, int codesize) {
    ObjGenSection* catsSection;
    SectionAttributeNode* proc;
    MWCATSExit* exit;
    MWCATSWideHeader* wide;
    MWCATSShortHeader* narrow;
    ObjGenRelocation* reloc;
    ObjGenSection* relocations;
    int position;
    int addressOffset;
    Elf32_Rela rela;

    catsSection = select_object_section(func, codesize, TRUE, TRUE, FALSE)->output;
    proc = catsSection->sectionData.attributes->tail;
    if (!proc->flags) {
        SectionAlign(&catsSection->buffer, 4);
        position = catsSection->buffer.size;
        if (proc->exitCount >= 256 || codesize > 32767) {
            wide = galloc(sizeof(MWCATSWideHeader));
            memset(wide, 0, sizeof(MWCATSWideHeader));
            wide->kind = 3;
            wide->flags = 0;
            wide->exitCount = CTool_EndianConvertWord16(proc->exitCount);
            wide->codesize = CTool_EndianConvertWord32(codesize);
            wide->address = 0;
            addressOffset = 8;
            AppendGListData(&catsSection->buffer, wide, sizeof(MWCATSWideHeader));
            for (exit = proc->exits; exit; exit = exit->next) {
                AppendGListTargetEndianLong(&catsSection->buffer, exit->offset);
            }
        } else {
            narrow = galloc(sizeof(MWCATSShortHeader));
            memset(narrow, 0, sizeof(MWCATSShortHeader));
            narrow->kind = 2;
            narrow->exitCount = proc->exitCount;
            narrow->codesize = CTool_EndianConvertWord16(codesize);
            narrow->address = 0;
            addressOffset = 4;
            AppendGListData(&catsSection->buffer, narrow, sizeof(MWCATSShortHeader));
            for (exit = proc->exits; exit; exit = exit->next) {
                AppendGListTargetEndianWord(&catsSection->buffer, exit->offset);
            }
        }
        relocations = catsSection->relocations;
        relocations->relocationCount++;
        reloc = galloc(sizeof(ObjGenRelocation));
        reloc->symbol = MakeSymbolTableEntry(func);
        reloc->kind = R_PPC_ADDR32;
        reloc->section = relocations;
        reloc->index = relocations->relocationCount - 1;
        rela.r_offset = position + addressOffset;
        rela.r_addend = 0;
        rela.r_info = R_PPC_ADDR32;
        AppendGListData(&relocations->buffer, &rela, sizeof(rela));
        reloc->next = allrelocations;
        allrelocations = reloc;
    }
}

#pragma pack(2)
