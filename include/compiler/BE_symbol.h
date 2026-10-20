#ifndef COMPILER_BE_SYMBOL_H
#define COMPILER_BE_SYMBOL_H

#include "compiler/common.h"
#include "compiler/ELF_Endian.h"

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: invented names: BE_SymNode, name, section, elf, next, hashNext, orderNext, order,
// alignment, linkageKind, linkageFlags, reserved, sclass, interruptInfo
// A symbol of the object file being written, in TheSymbolTable.
#pragma pack(push, 2)
struct BE_SymNode {
    HashNameNode* name;
    struct ObjGenSection* section;
    // the symbol table entry WriteSsymtab writes; until then st_other holds the back end's flags
    // (0x40: not written, as for a pooled section's anchor; WriteSsymtab clears the upper bits)
    Elf32_Sym elf;
    struct BE_SymNode* next;
#if VERSION >= VERSION_GC_2_7
    // the next symbol in its name's bucket of TheSymbolTable.hash
    struct BE_SymNode* hashNext;
#endif
    struct BE_SymNode* orderNext;
    SInt32 order;
    // alignment and the three fields after it are the 8-byte record WriteSsymtab appends to the
    // .comment section for the symbol: the alignment, a linkage byte (13 implicitly weak, 14 weak),
    // flags (8 force-active or exported, 0x10 a pool's anchor) and 2 bytes of zero padding
    SInt32 alignment;
    char linkageKind;
    char linkageFlags;
    char reserved[2];
    SInt16 sclass; // TK_STATIC (internal linkage) or TK_EXTERN
    struct InterruptGenerationRecord* interruptInfo;
};
#pragma pack(pop)
// NOTE: invented names: BE_SymbolTable, hash, first, current, inorder
// The symbol table: every symbol in the order added, the cursor First/Next walk with (also the last
// added), and the head of the ELF order assign_symbol_order builds (Wii's COMMON `TheSymbolTable`).
typedef struct BE_SymbolTable {
#if VERSION >= VERSION_GC_2_7
    // 2.7 finds symbols by name through a hash table (BE_SymbolHash)
    BE_SymNode* hash[0x1000];
#endif
    BE_SymNode* first;
    BE_SymNode* current;
    BE_SymNode* inorder;
} BE_SymbolTable;

extern BE_SymbolTable TheSymbolTable;

extern BE_SymNode* First(void);
extern BE_SymNode* Next(void);
extern BE_SymNode* AddSymbol(
    BE_SymNode* node,
    SInt32 size,
    SInt32 qual,
    SInt16 sclass,
    SInt32 alignment,
    UInt8 flags,
    ObjGenSection* section
);
extern BE_SymNode* AddObject(Object* obj, SInt32 size, ObjGenSection* section);
extern BE_SymNode* AddFunction(Object* obj, SInt32 size, ObjGenSection* section);
extern BE_SymNode* AddEntry(Object* obj, ObjGenSection* section);
extern BE_SymNode* AddSymbolTableEntry(HashNameNode* name);
extern void InitELFSymbols(void);
extern BE_SymNode* InorderFirst(void);
extern BE_SymNode* MakeSymbolTableEntry(Object* obj);
#if VERSION >= VERSION_GC_2_7
extern BE_SymNode* MakeNamedSymbolTableEntry(HashNameNode* name, SInt16 sclass);
#endif
extern BE_SymNode* elf_declare_symbol(ObjGenSection* section);
extern BE_SymNode* CreateDataPoolAnchorSymbol(ObjGenSection* section, Object* anchor);
extern struct BE_SymGroup* GetObjectExtension(Object* obj);
extern SInt32 GetPooledObjectOffset(Object* obj);
extern int GetPooledObjectRegister(Object* obj);
extern Object* GetDataPoolRoot(Object* obj);
extern Boolean NeedsXREF(Object* obj);

#ifdef __cplusplus
}
#endif

#endif
