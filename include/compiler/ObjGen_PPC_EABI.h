#ifndef COMPILER_OBJGEN_PPC_EABI_H
#define COMPILER_OBJGEN_PPC_EABI_H

#include "compiler/common.h"
#include "compiler/CompilerTools.h"
#include "compiler/ELF_Endian.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AddressingMode {
    AM_NONE = 0,
    AM_FAR_ABS = 1,
    AM_NEAR_ABS = 2,
    AM_UNUSED_3 = 3,
    AM_UNUSED_4 = 4,
    AM_SDA_REL = 5,
    AM_SDATA = 6,
    AM_SDATA2 = 7,
    AM_SDATA0 = 8,
    AM_LOCAL = 9,
    AM_CODE_PC_REL = 10,
    AM_CODE_NEAR_ABS = 11,
    AM_CODE_FAR_ABS = 12
} AddressingMode;
struct CGList {
    struct SectionAttributeNode* head;
    struct SectionAttributeNode* tail;
    struct ObjGenSection* sourceSection;
};
#pragma pack(push, 2)
struct InterruptGenerationRecord {
    struct InterruptGenerationRecord* next;
    UInt8 kind;
    UInt16 sectionIndex;
    UInt16 id;
    UInt16 textSection;
    UInt16 dataSection;
    UInt16 constSection;
    UInt16 sconstSection;
    UInt16 sdataSection;
    UInt8 flags;
};
enum {
    InterruptRecord_Interrupt = 1,
    InterruptRecord_DeclSpec = 2,
    InterruptRecord_SectionSet = 3
};
enum {
    Interrupt_On = 1,
    Interrupt_Enable = 2,
    Interrupt_SRR = 4,
    Interrupt_DAR = 8,
    Interrupt_DSISR = 0x10,
    Interrupt_FPRs = 0x20,
    Interrupt_VRs = 0x40,
    Interrupt_NoWarn = 0x80
};
#pragma pack(pop)
enum {
    R_PPC_ADDR32 = 1,
    R_PPC_ADDR24 = 2,
    R_PPC_ADDR16 = 3,
    R_PPC_ADDR16_LO = 4,
    R_PPC_ADDR16_HI = 5,
    R_PPC_ADDR16_HA = 6,
    R_PPC_ADDR14 = 7,
    R_PPC_REL24 = 10,
    R_PPC_REL14 = 11,
    R_PPC_EMB_SDA21 = 109
};

struct ObjGenRelocation {
    struct ObjGenRelocation* next;
    SInt32 kind;
    struct ObjGenSection* section;
    struct ObjGenSection* source;
    SInt32 index;
    struct BE_SymNode* symbol;
};
enum {
    SectionKind_Null = 0,
    SectionKind_Content = 1,
    SectionKind_Ctors = 2,
    SectionKind_Dtors = 3,
    SectionKind_Extab = 4,
    SectionKind_ExtabIndex = 5,
    SectionKind_BSS = 6,
    SectionKind_Debug = 7,
    SectionKind_Line = 8,
    SectionKind_MWCATS = 9,
    SectionKind_Rel = 10,
    SectionKind_Rela = 11,
    SectionKind_Symtab = 12,
    SectionKind_Strtab = 13,
    SectionKind_Shstrtab = 14,
    SectionKind_Comment = 15,
    SectionKind_Common = 16,
    SectionKind_Abs = 17
};

#pragma pack(push, 2)
struct ObjGenSection {
    struct ObjGenSection* next;
    struct ObjGenSection* nextInGroup;
    UInt8 kind;
    UInt8 flags;
    UInt32 permissions;
    SInt16 index;
    SInt32 relocationCount;
    struct ObjGenSymbolLink* symbolLink;
    struct SectionSymbolAttributes* header;
    char* name;
    SInt32 initialSize;
    GList buffer;
    Elf32_Shdr shdr;
    struct ObjGenSection* relocations;
    struct DwarfFunctionState* debugState;
    SInt32 context;
    struct BE_SymNode* sym;
    struct ObjGenSection* output;
    union {
        struct CGList* attributes;
    } sectionData;
};
#pragma pack(pop)
#pragma pack(push, 2)
typedef struct BE_SymGroup {
    struct BE_SymGroup* next;
    SInt32 base;
    SInt32 reg;
    ObjectList* objects;
    SInt32 limit;
    SInt32 count;
    Boolean full;
} BE_SymGroup;
struct ObjGenSymbolLink {
    struct Object* object;
    struct Object* firstObject;
    struct BE_SymNode* symbol;
    BE_SymGroup* groups;
    BE_SymGroup** tail;
    UInt8 used;
};
#pragma pack(pop)

struct SectionSymbolAttributes {
    struct SectionSymbolAttributes* next;
    struct ObjGenSection* target;
    unsigned short index;
    AddressingMode dataMode;
    AddressingMode codeMode;
    struct ObjGenSection* linkedSymbol;
    struct ObjGenSection* auxiliary;
};

extern UInt8 ok_to_compress;

extern void ELF_Setup(void);
extern void ObjGen_Setup(void);
extern void ObjGen_Finish(void);
extern void ObjGen_Cleanup(void);
extern void ObjGen_CodeCleanup(void);
extern void ObjGen_SetupSym(void);
extern void ObjGen_SymFunc(Object* func);
extern void ObjGen_SegmentName(HashNameNode* name);
extern void ObjGen_SrcBreakName(HashNameNode* name, SInt32 fileModDate, Boolean unused);
extern void ObjGen_DeclareData(
    Object* object, const void* data, OLinkList* list, unsigned int size
);
extern void ObjGen_DeclareReadOnlyData(
    Object* object, const void* data, OLinkList* list, unsigned int size
);
extern void ObjGen_DeclareSwitchTable(Object* tableobj, Object* funcobj);
extern ObjGenSection* ObjGen_DeclareCode(Object* object, SInt32 size);
extern void ObjGen_DeclareEntry(Object* object, SInt32 offset);
extern void ObjGen_DeclareSymInfo(void);
extern void ObjGen_DeclareExceptionTables(
    Object* obj, SInt32 codesize, void* data, SInt32 size, OLinkList* list
);
extern void ObjGen_RelocateObj(
    ObjGenSection* section, SInt32 offset, Object* object, UInt8 type, SInt32 addend
);
extern void ObjGen_SourceRef(UInt32 line, UInt32 offset);
extern GList* ObjGen_GetSectionGList(ObjGenSection* section);
extern void ObjGen_DeclareFloatConst(Object* object);
extern void ObjGen_DeclareCodeLabel(Object* labelobj, SInt32 offset, Object* funcobj);

extern int ObjGen_GetAddressing(Object* obj);
extern int base_register(Object* obj);
extern Boolean is_16bitdata(Object* obj);
extern void ObjGen_BestSection(Object* func);
#if VERSION < VERSION_GC_2_0
extern void PPC_EABI_SetSection(Object* object, SInt32 size, Boolean isConst);
#else
extern void PPC_EABI_SetSection(Object* object, SInt32 size, Boolean isConst, Boolean allowSmall);
#endif
extern short GetCurrentSection(short kind);
extern short pseudo_section_index(short key);
extern SInt16 update_section(Object* func);
extern InterruptGenerationRecord* GetInterruptRoutineFlags(Object* obj);
extern InterruptGenerationRecord* get_current_pseudo_section_info(void);
extern void Finish_Pragma_Section(SectionRec* section);
extern Boolean is_illegal_abs_section_name(char* name);
extern unsigned char is_illegal_section_name(char* name);
extern void CodeGen_PragmaPush(void);
extern void CodeGen_PragmaPop(void);
extern void InitElfSections(void);
extern void declareinit(Object* object);
extern void init_xref_anchors(void);
extern char* GetSourceFileName(ObjGenSection* section);
extern void MWCATS_AddProc(Object* object, UInt8 flags);
extern void MWCATS_AddExit(ObjGenSection* section, unsigned int offset);
extern void MWCATS_WriteFunction(Object* func, int codesize);

#ifdef __cplusplus
}
#endif

#endif
