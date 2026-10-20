#ifndef COMPILER_OBJGEN_PPC_EABI_H
#define COMPILER_OBJGEN_PPC_EABI_H

#include "compiler/common.h"
#include "compiler/CompilerTools.h"
#include "compiler/ELF_Endian.h"

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: invented names: AddressingMode, AM_NONE, AM_FAR_ABS, AM_NEAR_ABS, AM_UNUSED_3, AM_UNUSED_4,
// AM_SDA_REL, AM_SDATA, AM_SDATA2, AM_SDATA0, AM_LOCAL, AM_CODE_PC_REL, AM_CODE_NEAR_ABS,
// AM_CODE_FAR_ABS
// How an object is addressed (ObjGen_GetAddressing): a section's `#pragma section` data_mode and
// code_mode (SectionRec.dataMode, .codeMode; SectionSymbolAttributes.dataMode, .codeMode) by the
// pragma's words. AM_SDA_REL becomes the small-data area of its section (Finish_Pragma_Section);
// base_register gives each mode's base register.
typedef enum AddressingMode {
    AM_NONE = 0,
    AM_FAR_ABS = 1,
    AM_NEAR_ABS = 2,
    AM_UNUSED_3 = 3, // nothing sets 3 or 4; base_register still gives them r2
    AM_UNUSED_4 = 4,
    AM_SDA_REL = 5,
    AM_SDATA = 6, // .sdata/.sbss, from r13
    AM_SDATA2 = 7, // .sdata2/.sbss2, from r2
    AM_SDATA0 = 8, // .PPC.EMB.sdata0/.sbss0, from r0
    AM_LOCAL = 9, // a local, from the frame's base register
    AM_CODE_PC_REL = 10,
    AM_CODE_NEAR_ABS = 11,
    AM_CODE_FAR_ABS = 12
} AddressingMode;
// A .mwcats section's procedure list (MWCATS_AddProc)
struct CGList {
    struct SectionAttributeNode* head;
    struct SectionAttributeNode* tail;
    struct ObjGenSection* sourceSection; // (the code section it describes)
};
#pragma pack(push, 2)
// NOTE: invented names: textSection, dataSection, constSection, sconstSection, sdataSection
struct InterruptGenerationRecord {
    struct InterruptGenerationRecord* next;
    UInt8 kind; // InterruptRecord_*
    UInt16 sectionIndex; // (kinds 1 and 2)
    UInt16 id;
    // (kind 3) the section header indexes PPC_EABI_SetSection picks from, one per `#pragma section`
    // object type: code_type, data_type, const_type, sconst_type, sdata_type
    UInt16 textSection;
    UInt16 dataSection;
    UInt16 constSection;
    UInt16 sconstSection;
    UInt16 sdataSection;
    UInt8 flags; // #pragma interrupt's options (Interrupt_*)
};
// NOTE: invented names: InterruptRecord_Interrupt, InterruptRecord_DeclSpec,
// InterruptRecord_SectionSet
// InterruptGenerationRecord.kind
enum {
    // an interrupt function (#pragma interrupt, __declspec(interrupt))
    InterruptRecord_Interrupt = 1,
    // __declspec(section) (DoParseDeclSpec)
    InterruptRecord_DeclSpec = 2,
    // the `#pragma section` settings (get_current_pseudo_section_info)
    InterruptRecord_SectionSet = 3
};
// NOTE: invented names: Interrupt_On, Interrupt_Enable, Interrupt_SRR, Interrupt_DAR,
// Interrupt_DSISR, Interrupt_FPRs, Interrupt_VRs, Interrupt_NoWarn (after the pragma's words)
// InterruptGenerationRecord.flags: what `#pragma interrupt` asked for
enum {
    Interrupt_On = 1,
    Interrupt_Enable = 2, // enable: run with external interrupts enabled
    Interrupt_SRR = 4, // SRR: save SRR0 and SRR1
    Interrupt_DAR = 8,
    Interrupt_DSISR = 0x10,
    Interrupt_FPRs = 0x20, // fprs: save the scratch FPRs
    Interrupt_VRs = 0x40, // vrs: save the scratch vector registers
    Interrupt_NoWarn = 0x80
};
#pragma pack(pop)
// The PowerPC ELF relocation types ObjGen writes (ObjGenRelocation.kind, Elf32_Rela.r_info's low
// byte)
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
    SInt32 kind; // R_PPC_*
    struct ObjGenSection* section;
    // the section whose symbol the relocation names, until BE_elf declares it
    struct ObjGenSection* source; // NOTE: invented name
    SInt32 index;
    struct BE_SymNode* symbol;
};
// NOTE: invented names: SectionKind_Null, SectionKind_Content, SectionKind_Ctors,
// SectionKind_Dtors, SectionKind_Extab, SectionKind_ExtabIndex, SectionKind_BSS, SectionKind_Debug,
// SectionKind_Line, SectionKind_MWCATS, SectionKind_Rel, SectionKind_Rela, SectionKind_Symtab,
// SectionKind_Strtab, SectionKind_Shstrtab, SectionKind_Comment, SectionKind_Common,
// SectionKind_Abs
// ObjGenSection.kind: what InitElfSection files a section as
enum {
    SectionKind_Null = 0, // the null section header (ELFfile.null_section)
    SectionKind_Content = 1, // code or initialized data
    SectionKind_Ctors = 2,
    SectionKind_Dtors = 3,
    SectionKind_Extab = 4,
    SectionKind_ExtabIndex = 5,
    SectionKind_BSS = 6, // uninitialized data (SHT_NOBITS)
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
    UInt32 permissions; // (ELF's SHF_*: 1 W, 2 A (shown as R), 4 X; 0x40000000 compressed)
    SInt16 index; // (the ELF section header index: build_ordered_section_index)
    SInt32 relocationCount;
    struct ObjGenSymbolLink* symbolLink;
    struct SectionSymbolAttributes* header;
    char* name;
    SInt32 initialSize;
    GList buffer;
    // the section's header, which BackEnd_WriteElf writes (sh_flags: `permissions` keeps the access
    // the section was declared with, which report_section_permission_conflict compares;
    // sh_addralign: the largest alignment of what the section holds)
    Elf32_Shdr shdr;
    struct ObjGenSection* relocations; // (the section's .rela section)
    struct DwarfFunctionState* debugState;
    // the uid of the source file (ObjGen's CurFile) a clone's code comes from: CloneSection,
    // GetSectionByName, GetSourceFileName; 0 for the main file
    SInt32 context;
    struct BE_SymNode* sym;
    struct ObjGenSection* output;
    union {
        struct CGList* attributes; // (a .mwcats section, SectionKind_MWCATS: MWCATS_AddProc)
    } sectionData;
};
#pragma pack(pop)
#pragma pack(push, 2)
// A 64K window of a pooled section that objects are addressed through from one base register
typedef struct BE_SymGroup { // NOTE: invented name
    struct BE_SymGroup* next;
    SInt32 base; // the window covers base - 0x8000 .. base + 0x7fff
    SInt32 reg;
    ObjectList* objects;
    SInt32 limit;
    SInt32 count;
    Boolean full;
} BE_SymGroup;
// What a pooled section's anchor keeps
struct ObjGenSymbolLink { // NOTE: invented name
    // the section's anchor object; GetDataPoolRoot returns it
    struct Object* object;
    // the first object select_object_section placed in the section (CreateDataPoolAnchorSymbol's
    // argument)
    struct Object* firstObject;
    // the anchor's symbol, which pooled relocations use (CreateDataPoolAnchorSymbol returns it)
    struct BE_SymNode* symbol;
    BE_SymGroup* groups;
    // where the next group is linked (the list's last `next`)
    BE_SymGroup** tail;
    // init_xref_anchors clears it
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

/* The EABI object writer's own */
extern int ObjGen_GetAddressing(Object* obj);
extern int base_register(Object* obj);
extern Boolean is_16bitdata(Object* obj);
extern void ObjGen_BestSection(Object* func);
extern void PPC_EABI_SetSection(Object* object, SInt32 size, Boolean isConst, Boolean allowSmall);
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
