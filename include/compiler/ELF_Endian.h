#ifndef COMPILER_ELF_ENDIAN_H
#define COMPILER_ELF_ENDIAN_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

// The ELF specification's constants, those the compiler uses
#define SHN_UNDEF 0

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHT_REL 9

#define SHF_ALLOC 2
#define SHF_EXECINSTR 4

#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2

#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STT_FILE 4

#define ELF32_ST_BIND(info) ((info) >> 4)
#define ELF32_ST_TYPE(info) ((info) & 0xf)
#define ELF32_ST_INFO(bind, type) (((bind) << 4) + ((type) & 0xf))

// The ELF structures, with the ELF specification's names (the functions converting them are named
// after them).
typedef struct Elf32_Ehdr {
    UInt8 e_ident[16];
    UInt16 e_type;
    UInt16 e_machine;
    UInt32 e_version;
    UInt32 e_entry;
    UInt32 e_phoff;
    UInt32 e_shoff;
    UInt32 e_flags;
    UInt16 e_ehsize;
    UInt16 e_phentsize;
    UInt16 e_phnum;
    UInt16 e_shentsize;
    UInt16 e_shnum;
    UInt16 e_shstrndx;
} Elf32_Ehdr;

typedef struct Elf32_Shdr {
    UInt32 sh_name;
    UInt32 sh_type;
    UInt32 sh_flags;
    UInt32 sh_addr;
    UInt32 sh_offset;
    UInt32 sh_size;
    UInt32 sh_link;
    UInt32 sh_info;
    UInt32 sh_addralign;
    UInt32 sh_entsize;
} Elf32_Shdr;

// st_value and st_size are signed, unlike the specification's (ObjGen_DeclareExceptionTables'
// chained `st_value = base = buffer.size` compiles otherwise with unsigned ones)
typedef struct Elf32_Sym {
    UInt32 st_name;
    SInt32 st_value;
    SInt32 st_size;
    UInt8 st_info;
    UInt8 st_other;
    UInt16 st_shndx;
} Elf32_Sym;

typedef struct Elf32_Rela {
    UInt32 r_offset;
    UInt32 r_info;
    SInt32 r_addend;
} Elf32_Rela;

extern void Convert_DWARF_Info(
    UInt8* data, UInt32 size, void (*errorHandler)(char*, int), char readBeforeSwap
);
extern void Convert_DWARF_line_info(
    UInt8* data, int size, void (*errorHandler)(char*, int), char readBeforeSwap
);
extern void Convert_Elf32_Rela(Elf32_Rela* rela);
extern void Convert_Elf32_Sym(Elf32_Sym* sym);
extern void Convert_Elf32_Shdr(Elf32_Shdr* shdr);
extern void Convert_Elf32_Ehdr(Elf32_Ehdr* ehdr);

#ifdef __cplusplus
}
#endif

#endif
