#ifndef COMPILER_BE_ELF_H
#define COMPILER_BE_ELF_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct SectionRec {
    UInt8 flags;
    UInt8 typebits;
    UInt8 dataMode;
    UInt8 codeMode;
    UInt32 mode;
    char* sectionName;
    char* linkedSectionName;
};
#pragma pack(push, 2)
typedef struct ElfFile {
    Boolean debug_sections_inited;
    BE_SymNode* file_symbol;
    SInt16 sectionHeaderCount;
    ObjGenSection* section_list;
    ObjGenSection* null_section;
    ObjGenSection* ctors_section;
    ObjGenSection* dtors_section;
    ObjGenSection* extab_section;
    ObjGenSection* extabindex_section;
    ObjGenSection* data_section_linked_symbol;
    ObjGenSection* dwarf_info_section;
    ObjGenSection* dwarf_line_section;
    ObjGenSection* rela_sections;
    ObjGenSection* symtab_section;
    ObjGenSection* symbol_string_table_section;
    ObjGenSection* shstrtab_section;
    ObjGenSection* comment_section;
    ObjGenSection* common_section;
    ObjGenSection* abs_section;
    UInt32 unused;
    SectionSymbolAttributes* section_symbol_attributes;
    UInt16 section_symbol_attribute_count;
} ElfFile;
#pragma pack(pop)

extern ElfFile ELFfile;
extern GList elfmodule;
extern ObjGenRelocation* allrelocations;
extern SInt32 previous_ps_ID;
extern InterruptGenerationRecord* interrupt_generation_records;

#if VERSION < VERSION_GC_2_0
extern void BackPatchRelocationAddend(ObjGenRelocation* reloc, unsigned int value);
extern ObjGenRelocation* WriteDebugRelocation(
    ObjGenSection* context, int offset, Object* object, void* source, int value, int adjustment
);
#else
extern void BackPatchRelocationAddend(
    ObjGenRelocation* reloc, unsigned int value, BE_SymNode* symbol
);
extern ObjGenRelocation* WriteDebugRelocation(
    ObjGenSection* context, int offset, BE_SymNode* symbol, void* source, int value, int adjustment
);
#endif
extern void BackEnd_WriteElf(void);
extern void TransferSectionData(GList* dst, GList* src);
extern void CleanupElfSections(void);
extern void InitElfFile(void);
extern void DoParseDeclSpec(char* name, DeclInfo* declinfo);
extern void DoElfSystemSection(char* name, UInt8 kind, SInt8 align, SInt32 initialSize);
extern ObjGenSection* CloneSection(ObjGenSection* input, SInt32 context, UInt8 flags);
extern SectionRec* Initialize_Input(
    char* name, char* linkedName, UInt8 dataMode, UInt8 codeMode, UInt8 typebits
);
extern ObjGenSection* DoElfContentSection(
    SectionRec* rec,
    short kind,
    SInt8 align,
    SInt32 size,
    int relocationSize,
    UInt32 permissions,
    char allocateData,
    char createEntry
);
extern ObjGenSection* GetSectionByName(const char* name, SInt32 context, UInt8 flags);
extern ObjGenSection* InitElfSection(
    char* name,
    UInt8 kind,
    SInt8 align,
    SInt32 initialSize,
    SectionRec* rec,
    UInt32 permissions,
    struct ObjGenSymbolLink* symbolLink
);
extern SectionSymbolAttributes* Initialize_Specific(
    ObjGenSection* section,
    UInt8 dataMode,
    UInt8 codeMode,
    ObjGenSection* linked,
    ObjGenSection* auxiliary
);
extern void SectionAlign(GList* list, SInt32 alignment);

#ifdef __cplusplus
}
#endif

#endif
