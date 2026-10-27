#include "compiler/DWARF.h"
#include "compiler/BE_elf.h"
#include "compiler/BE_symbol.h"
#include "compiler/CABI.h"
#include "compiler/CError.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CScope.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/RegisterInfo.h"
#include "compiler/scopes.h"
#include "compiler/StackFrame.h"
#include "compiler/templates.h"
#include "compiler/types.h"
#include "driver/COSToolsCLT.h"
#include <stdio.h>
#include <string.h>

#define TAG_array_type 0x01
#define TAG_class_type 0x02
#define TAG_enumeration_type 0x04
#define TAG_formal_parameter 0x05
#define TAG_global_subroutine 0x06
#define TAG_global_variable 0x07
#define TAG_local_variable 0x0c
#define TAG_member 0x0d
#define TAG_compile_unit 0x11
#define TAG_structure_type 0x13
#define TAG_subroutine 0x14
#define TAG_subroutine_type 0x15
#define TAG_typedef 0x16
#define TAG_union_type 0x17
#define TAG_inheritance 0x1c
#define TAG_ptr_to_member_type 0x1f

#define AT_sibling 0x0012
#define AT_location 0x0023
#define AT_name 0x0038
#define AT_fund_type 0x0055
#define AT_mod_fund_type 0x0063
#define AT_user_def_type 0x0072
#define AT_mod_u_d_type 0x0083
#define AT_subscr_data 0x00a3
#define AT_byte_size 0x00b6
#define AT_bit_offset 0x00c5
#define AT_bit_size 0x00d6
#define AT_element_list 0x00f4
#define AT_stmt_list 0x0106
#define AT_low_pc 0x0111
#define AT_high_pc 0x0121
#define AT_language 0x0136
#define AT_member 0x0142
#define AT_containing_type 0x01d2
#define AT_private 0x0248
#define AT_producer 0x0258
#define AT_protected 0x0268
#define AT_public 0x0288
#define AT_return_addr 0x02a3
#define AT_virtual 0x0308
#define AT_MW_linkage_name 0x2008
#define AT_MW_global_ref 0x2022
#define AT_MW_global_ref_name 0x2038
#define AT_MW_register_pair 0x2343

#define FT_char 0x0001
#define FT_signed_char 0x0002
#define FT_unsigned_char 0x0003
#define FT_signed_short 0x0005
#define FT_unsigned_short 0x0006
#define FT_integer 0x0007
#define FT_unsigned_integer 0x0009
#define FT_long 0x000a
#define FT_unsigned_long 0x000c
#define FT_pointer 0x000d
#define FT_float 0x000e
#define FT_dbl_prec_float 0x000f
#define FT_void 0x0014
#define FT_long_long 0x8008
#define FT_unsigned_long_long 0x8208
#define FT_MW_vec2x32float 0xac00

#define OP_REG 0x01
#define OP_BASEREG 0x02
#define OP_ADDR 0x03
#define OP_CONST 0x04
#define OP_DEREF 0x06
#define OP_ADD 0x07

#define FMT_FT_C_C 0x0
#define FMT_ET 0x8

#define LANG_C89 0x0001
#define LANG_C_PLUS_PLUS 0x0004

typedef struct DwarfVar DwarfVar;
typedef struct DwarfBlock DwarfBlock;
typedef struct DwarfSym DwarfSym;

#pragma pack(2)
struct DwarfFixup {
    struct DwarfFixup* next;
    struct ObjGenRelocation* reference;
    SInt32 offset;
};
struct DwarfFunctionState {
    ObjGenSection* section;
    GList info;
    GList lines;
    SInt32 offset;
    SInt32 lineSectionOffset;
    UInt8 lineBaseRelocated;
    struct ObjGenRelocation* pendingReference;
    SInt32 pendingPosition;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* pendingSymbol;
#endif
    UInt8 pending;
    SInt32 offsets[50];
    struct ObjGenRelocation* entries[50];
    SInt32 depth;
};
struct DwarfLocationOperand {
    UInt8 kind;
    union {
        UInt32 value;
        struct Type* type;
    } metadata;
    union {
        UInt32 value;
        struct {
            UInt8 first;
            UInt8 second;
            UInt32 offset;
        } registers;
        struct {
            UInt32 offset;
            UInt8 reg;
            struct Object* object;
        } address;
        struct {
            struct Object* object;
            UInt32 offset;
        } objectOffset;
    } operand;
};
struct DwarfVar {
#if VERSION >= VERSION_GC_2_0
    struct DwarfNode* func;
#endif
    UInt16 reg;
    UInt16 reg2;
    UInt16 qual;
    UInt16 base;
    SInt32 offset;
    struct HashNameNode* name;
};
struct DwarfBlock {
    struct Object* object;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
    UInt32 qual;
#endif
    SInt32 codeSize;
    SInt32 codeOffset;
    struct DwarfLocationOperand* returnOperand;
    struct PendingObject* pendingObjects;
};
struct DwarfSym {
    struct Object* object;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
#endif
    struct DwarfFixup* fixups;
    SInt32 offset;
};
struct DwarfNode {
    struct DwarfNode* next;
    struct DwarfNode* prev;
    struct DWInfo* type;
    struct DwarfFunctionState* scope;
    UInt16 kind;
    union DwarfNodePayload {
        struct DwarfVar var;
        struct DwarfBlock block;
        struct DwarfSym sym;
        struct {
            UInt8 language;
#if VERSION >= VERSION_GC_2_0
            BE_SymNode* symbol;
#endif
        } unit;
        struct {
            struct DwarfFixup* fixups;
            UInt16 qual;
        } type;
        struct DwarfNode* func;
    } u;
};
struct DwarfRef {
    UInt16 tag;
    SInt32 num;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
#endif
    struct QualNode* qualifiers;
    UInt8 valid;
    struct DWInfo* typeInfo;
};
struct DWInfo {
    Type* type;
    DwarfRef rec;
    struct DWInfo* next;
    UInt8 marked;
    struct DwarfNode* typeNode;
};
struct QualNode {
    struct QualNode* next;
    UInt8 type;
};
struct DwarfStateList {
    struct DwarfStateList* next;
    struct DwarfFunctionState* state;
};
struct PendingObject {
    struct Object* object;
    struct PendingObject* next;
};
typedef struct DwarfLineSymbol {
    struct DwarfLineSymbol* next;
    DwarfFunctionState* state;
    BE_SymNode* symbol;
} DwarfLineSymbol;

static struct DWInfo* find_or_create_dwinfo(struct Type* type);
#if VERSION >= VERSION_GC_2_0
static DwarfRef get_type_dwarf_ref(
    DWInfo* info, UInt16 qual, Boolean referenceOnly, BE_SymNode* symbol
);
#define GET_TYPE_DWARF_REF(info, qual, referenceOnly, symbol) \
    get_type_dwarf_ref(info, qual, referenceOnly, symbol)
#define EMIT_TYPE(emit, type, ref) emit(type, ref)
#else
static DwarfRef get_type_dwarf_ref(DWInfo* info, UInt16 qual, Boolean referenceOnly);
#define GET_TYPE_DWARF_REF(info, qual, referenceOnly, symbol) \
    get_type_dwarf_ref(info, qual, referenceOnly)
#define EMIT_TYPE(emit, type, ref) emit(type)
#endif
static int emit_location_attribute(
    DwarfLocationOperand* location, UInt16 attribute, unsigned char shouldDereference
);
#if VERSION >= VERSION_GC_2_0
static SInt32 emit_entry_header(SInt16 tag, BE_SymNode* symbol);
#define EMIT_ENTRY_HEADER(tag, symbol) emit_entry_header(tag, symbol)
#define PATCH_SIBLING(symbol) PatchSibling(symbol)
#define DWARF_BACKPATCH(reloc, value, symbol) BackPatchRelocationAddend(reloc, (symbol) ? 0 : (value), symbol)
#define DWARF_BACKPATCH_PLAIN(reloc, value, symbol) BackPatchRelocationAddend(reloc, value, symbol)
#define DWARF_RESOLVE_FIXUPS(fixups, offset, symbol) ResolveFixups(fixups, offset, symbol)
#define DWARF_RELOC_SYMBOL(object) MakeSymbolTableEntry(object)
#else
static SInt32 emit_entry_header(SInt16 tag);
#define EMIT_ENTRY_HEADER(tag, symbol) emit_entry_header(tag)
#define PATCH_SIBLING(symbol) PatchSibling()
#define DWARF_BACKPATCH(reloc, value, symbol) BackPatchRelocationAddend(reloc, value)
#define DWARF_BACKPATCH_PLAIN(reloc, value, symbol) BackPatchRelocationAddend(reloc, value)
#define DWARF_RESOLVE_FIXUPS(fixups, offset, symbol) ResolveFixups(fixups, offset)
#define DWARF_RELOC_SYMBOL(object) (object)
#endif
static SInt32 emit_dwarf_ref(DWInfo* info);
static void emit_local_entry(DWInfo* info, DwarfVar* var);
static void emit_parameter_entry(DWInfo* info, DwarfVar* var);
static void emit_variable_entry(DwarfSym* sym, Object* object, DWInfo* info);
static void create_type_node(DWInfo* info);
static void insert_type_nodes_recursive(DWInfo* parent, DWInfo* info);
static void setup_return_operand(Object* func);

DWInfo* dwinfo_buckets[512];
PendingObject* pending_objects;
DwarfFunctionState* DefaultContext;
GList* text;
DwarfFunctionState* CurContext;
ObjGenSection* LineSect;
GList* dwarf;
ObjGenSection* DebugSect;
DwarfStateList* debug_records;
DwarfNode* func_link;
GList* dwarfline;
DwarfStateList* last_debug_record;
DwarfLocationOperand ReturnAddress;

static UInt8 dwarf_unreferenced[4];
static DwarfLineSymbol* dwarf_line_symbols;
static SInt32 dwarf_file_entry_offsets[50];
static ObjGenRelocation* dwarf_file_entry_relocs[50];
static UInt8 dwarf_file_pending;
static DwarfNode* dwarf_node_head;
static DwarfNode* currentDwarfNode;
static BE_SymNode* dwarf_last_entry_symbol;
static BE_SymNode* dwarf_function_line_symbol;
static UInt32 dwarf_function_qual;
static DwarfNode* currentDwarfScope;
#if VERSION >= VERSION_GC_2_0
static DwarfNode* currentDwarfFunction;
#endif
static UInt8 dwarf_lines_header_pending;

static SInt32 dwarf_file_depth = -1;
static SInt32* dwarf_depth_ptr = &dwarf_file_depth;
static SInt32* dwarf_entry_offsets = dwarf_file_entry_offsets;
static ObjGenRelocation** dwarf_entry_relocs = dwarf_file_entry_relocs;
static UInt8* dwarf_pending_ptr = &dwarf_file_pending;
static SInt32 dwarf_block_depth = -1;

#if VERSION >= VERSION_GC_2_7
static BE_SymNode* DWARF_MakeEntrySymbol(HashNameNode* name, int sclass, int tag, SInt32 offset);
static BE_SymNode* DWARF_MakeLineSymbol(
    HashNameNode* name, int sclass, SInt32 offset, DwarfFunctionState* state
);
static SInt32 DWARF_SymbolOffset(BE_SymNode* symbol, SInt32 offset);
static HashNameNode* DWARF_QualifiedName(NameSpace* nspace, HashNameNode* name);
static void DWARF_MangleNameSpaceName(NameSpace* nspace);

#define DWARF_ENTRY_SYMBOL(name, sclass, tag, offset) \
    DWARF_MakeEntrySymbol(name, sclass, tag, offset)
#define DWARF_LINE_SYMBOL(name, sclass, offset, state) \
    DWARF_MakeLineSymbol(name, sclass, offset, state)
#define DWARF_SYMBOL_OFFSET(symbol, offset) DWARF_SymbolOffset(symbol, offset)
#else
#define DWARF_ENTRY_SYMBOL(name, sclass, tag, offset) NULL
#define DWARF_LINE_SYMBOL(name, sclass, offset, state) NULL
#define DWARF_SYMBOL_OFFSET(symbol, offset) (offset)
#endif

static DwarfNode* NewDwarfNode(void) {
    currentDwarfNode->next = galloc(sizeof(DwarfNode));
    memclrw(currentDwarfNode->next, sizeof(DwarfNode));
    currentDwarfNode->next->prev = currentDwarfNode;
    currentDwarfNode = currentDwarfNode->next;
    currentDwarfNode->scope = CurContext;
    return currentDwarfNode;
}

static DwarfNode* InsertDwarfNode(void) {
    DwarfNode* next = currentDwarfScope->next;
    currentDwarfScope->next = galloc(sizeof(DwarfNode));
    memclrw(currentDwarfScope->next, sizeof(DwarfNode));
    currentDwarfScope->next->prev = currentDwarfScope;
    currentDwarfScope = currentDwarfScope->next;
    currentDwarfScope->scope = CurContext;
    currentDwarfScope->next = next;
    if (next == NULL) {
        currentDwarfNode = currentDwarfScope;
    }
    return currentDwarfScope;
}

static SInt32 DW_String(char* s) {
    SInt32 i;
    for (i = 0; s[i] != 0; i++) {
        AppendGListByte(dwarf, s[i]);
    }
    AppendGListByte(dwarf, 0);
    return i + 1;
}

static void SetScope(DwarfFunctionState* state) {
    CurContext = state;
    if (copts.full_dwarf) {
        dwarf = &state->info;
        dwarf_depth_ptr = &state->depth;
        dwarf_entry_offsets = state->offsets;
        dwarf_entry_relocs = state->entries;
        dwarf_pending_ptr = &state->pending;
    }
    dwarfline = &state->lines;
    text = ObjGen_GetSectionGList(state->section);
}

#if VERSION >= VERSION_GC_2_0
static void PatchSibling(BE_SymNode* symbol) {
    if (copts.dwarf_relocations) {
        BackPatchRelocationAddend(
            dwarf_entry_relocs[*dwarf_depth_ptr],
            DWARF_SYMBOL_OFFSET(symbol, dwarf->size + CurContext->offset),
            symbol
        );
    } else {
        *(SInt32*)(*dwarf->data + dwarf_entry_offsets[*dwarf_depth_ptr]) =
            dwarf->size + CurContext->offset;
    }
}

static DwarfFixup* ResolveFixups(DwarfFixup* fixup, unsigned int offset, BE_SymNode* symbol) {
    for (; fixup; fixup = fixup->next) {
        if (copts.dwarf_relocations) {
            DWARF_BACKPATCH(fixup->reference, offset, symbol);
        } else {
            *(SInt32*)(*dwarf->data + fixup->offset) = offset;
        }
    }
    return fixup;
}
#else
static void PatchSibling(void) {
    if (copts.dwarf_relocations) {
        BackPatchRelocationAddend(
            dwarf_entry_relocs[*dwarf_depth_ptr], dwarf->size + CurContext->offset
        );
    } else {
        *(SInt32*)(*dwarf->data + dwarf_entry_offsets[*dwarf_depth_ptr]) =
            dwarf->size + CurContext->offset;
    }
}

static DwarfFixup* ResolveFixups(DwarfFixup* fixup, unsigned int offset) {
    for (; fixup; fixup = fixup->next) {
        if (copts.dwarf_relocations) {
            BackPatchRelocationAddend(fixup->reference, offset);
        } else {
            *(SInt32*)(*dwarf->data + fixup->offset) = offset;
        }
    }
    return fixup;
}
#endif

static void DWARF_MangleNameSpaceName(NameSpace* nspace) {
    while (nspace && !nspace->name) {
        nspace = nspace->parent;
    }
    if (nspace) {
        DWARF_MangleNameSpaceName(nspace->parent);
        AppendGListName(&name_mangle_list, nspace->name->name);
        AppendGListName(&name_mangle_list, "::");
    }
}

#if VERSION >= VERSION_GC_2_7
static HashNameNode* DWARF_QualifiedName(NameSpace* nspace, HashNameNode* name) {
    if (IsTempName(name)) {
        return NULL;
    }
    while (nspace && !nspace->name) {
        nspace = nspace->parent;
    }
    if (nspace) {
        name_mangle_list.size = 0;
        DWARF_MangleNameSpaceName(nspace);
        AppendGListID(&name_mangle_list, name->name);
        COS_LockHandle(name_mangle_list.data);
        name = GetHashNameNodeExport(*name_mangle_list.data);
        COS_UnlockHandle(name_mangle_list.data);
    }
    return name;
}
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -72
#endif
#if VERSION >= VERSION_GC_2_0
static void emit_enum_type(Type* type, DwarfRef* ref) {
#else
static void emit_enum_type(Type* type) {
#endif
    UInt32 offset;
    DWInfo* info;
    SInt32 size;
    SInt32 listOffset;
    SInt32 listSize;
    ObjEnumConst* constant;
    SInt32 length;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
#endif
    DwarfFixup* fixup;

    offset = dwarf->size;
    info = find_or_create_dwinfo(type);
    CError_ASSERT(331, info->typeNode);
#if VERSION >= VERSION_GC_2_0
    if (TYPE_ENUM(type)->enumname) {
        symbol = DWARF_ENTRY_SYMBOL(
            DWARF_QualifiedName(TYPE_ENUM(type)->nspace, TYPE_ENUM(type)->enumname),
            TK_EXTERN,
            TAG_enumeration_type,
            offset + CurContext->offset
        );
    } else {
        symbol =
            DWARF_ENTRY_SYMBOL(NULL, TK_STATIC, TAG_enumeration_type, offset + CurContext->offset);
    }
    ref->symbol = symbol;
#endif
    for (fixup = info->typeNode->u.type.fixups; fixup; fixup = fixup->next) {
        if (copts.dwarf_relocations) {
            DWARF_BACKPATCH(fixup->reference, offset, symbol);
        } else {
            *(SInt32*)(*dwarf->data + fixup->offset) = offset;
        }
    }
    info->typeNode->u.type.fixups = fixup;
    size = EMIT_ENTRY_HEADER(TAG_enumeration_type, symbol);
    if (TYPE_ENUM(type)->enumname) {
        AppendGListWord(dwarf, AT_name);
        size += 2;
        size += DW_String(TYPE_ENUM(type)->enumname->name);
    }
    AppendGListWord(dwarf, AT_byte_size);
    AppendGListLong(dwarf, type->size);
    AppendGListWord(dwarf, AT_element_list);
    listOffset = dwarf->size;
    listSize = 0;
    AppendGListLong(dwarf, 0);
    size += 12;
    for (constant = TYPE_ENUM(type)->enumlist; constant; constant = constant->next) {
        AppendGListLong(dwarf, constant->val.lo);
        size += 4;
        listSize += 4;
        length = DW_String(constant->name->name);
        size += length;
        listSize += length;
    }
    *(SInt32*)(*dwarf->data + listOffset) = listSize;
    *(SInt32*)(*dwarf->data + offset) = size;
#if VERSION >= VERSION_GC_2_7
    AddSymbol(
        symbol,
        dwarf->size + CurContext->offset - symbol->elf.st_value,
        Q_IMPLICIT_WEAK,
        symbol->sclass,
        1,
        2,
        DebugSect
    );
#endif
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION >= VERSION_GC_2_0
static void emit_struct_type(TypeStruct* tstruct, DwarfRef* ref) {
#else
static void emit_struct_type(TypeStruct* tstruct) {
#endif
    UInt32 offset;
    DWInfo* info;
    UInt16 tag;
    SInt32 size;
    StructMember* member;
    TypeBitfield* bitfield;
    DWInfo* memberInfo;
    SInt32 memberOffset;
    SInt32 memberSize;
    Boolean hasMembers;
    DwarfLocationOperand location;
    TypeBitfield reversed;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
#endif
    DwarfFixup* fixup;

    hasMembers = FALSE;
    offset = dwarf->size;
    info = find_or_create_dwinfo(TYPE(tstruct));
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -82
#endif
    CError_ASSERT(384, info->typeNode);
#if VERSION < VERSION_GC_2_0
    for (fixup = info->typeNode->u.type.fixups; fixup; fixup = fixup->next) {
        if (copts.dwarf_relocations) {
            DWARF_BACKPATCH(fixup->reference, offset, symbol);
        } else {
            *(SInt32*)(*dwarf->data + fixup->offset) = offset;
        }
    }
    info->typeNode->u.type.fixups = fixup;
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -79
#endif
#endif
    switch (tstruct->stype) {
        case STRUCT_TYPE_STRUCT:
        case STRUCT_VECTOR_UCHAR:
        case STRUCT_VECTOR_SCHAR:
        case STRUCT_VECTOR_BCHAR:
        case STRUCT_VECTOR_USHORT:
        case STRUCT_VECTOR_SSHORT:
        case STRUCT_VECTOR_BSHORT:
        case STRUCT_VECTOR_UINT:
        case STRUCT_VECTOR_SINT:
        case STRUCT_VECTOR_BINT:
        case STRUCT_VECTOR_FLOAT:
        case STRUCT_VECTOR_PIXEL:
            tag = TAG_structure_type;
            break;
        case STRUCT_TYPE_UNION:
            tag = TAG_union_type;
            break;
        default:
            CError_FATAL(406);
    }
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -88
#endif
#if VERSION >= VERSION_GC_2_0
    if (tstruct->name) {
        symbol = DWARF_ENTRY_SYMBOL(
            DWARF_QualifiedName(NULL, tstruct->name), TK_EXTERN, tag, offset + CurContext->offset
        );
    } else {
        symbol = DWARF_ENTRY_SYMBOL(NULL, TK_STATIC, tag, offset + CurContext->offset);
    }
    ref->symbol = symbol;
#endif
#if VERSION >= VERSION_GC_2_0
    for (fixup = info->typeNode->u.type.fixups; fixup; fixup = fixup->next) {
        if (copts.dwarf_relocations) {
            DWARF_BACKPATCH(fixup->reference, offset, symbol);
        } else {
            *(SInt32*)(*dwarf->data + fixup->offset) = offset;
        }
    }
    info->typeNode->u.type.fixups = fixup;
#endif
    size = EMIT_ENTRY_HEADER(tag, symbol);
    if (tstruct->name) {
        AppendGListWord(dwarf, AT_name);
        size += 2;
        size += DW_String(tstruct->name->name);
    }
    AppendGListWord(dwarf, AT_byte_size);
    AppendGListLong(dwarf, tstruct->size);
    size += 6;
    *(SInt32*)(*dwarf->data + offset) = size;
    CError_ASSERT(426, ++*dwarf_depth_ptr < 50);
    dwarf_entry_offsets[*dwarf_depth_ptr] = 0;

    for (member = tstruct->members; member; member = member->next) {
        hasMembers = TRUE;
        bitfield = TYPE_BITFIELD(member->type);
        if (bitfield->type == TYPEBITFIELD) {
            memberInfo = find_or_create_dwinfo(bitfield->bitfieldtype);
            if (copts.littleendian) {
                reversed = *bitfield;
                bitfield = &reversed;
                CABI_ReverseBitField(&reversed);
            }
        } else {
            memberInfo = find_or_create_dwinfo(TYPE(bitfield));
            bitfield = NULL;
        }
        GET_TYPE_DWARF_REF(memberInfo, member->qual, TRUE, NULL);
        memberOffset = dwarf->size;
        memberSize = EMIT_ENTRY_HEADER(TAG_member, symbol);
        AppendGListWord(dwarf, AT_name);
        memberSize += 2;
        memberSize += DW_String(member->name->name);
        memberSize += emit_dwarf_ref(memberInfo);
        if (bitfield) {
            AppendGListWord(dwarf, AT_bit_offset);
            AppendGListWord(dwarf, bitfield->offset);
            AppendGListWord(dwarf, AT_bit_size);
            AppendGListLong(dwarf, bitfield->bitlength);
            AppendGListWord(dwarf, AT_byte_size);
            AppendGListLong(dwarf, bitfield->bitfieldtype->size);
            memberSize += 16;
        }
        switch (tstruct->stype) {
            case STRUCT_VECTOR_UCHAR:
            case STRUCT_VECTOR_SCHAR:
            case STRUCT_VECTOR_BCHAR:
                location.kind = 9;
                if (copts.littleendian) {
                    location.operand.value = 15 - member->offset;
                } else {
                    location.operand.value = member->offset;
                }
                break;
            case STRUCT_VECTOR_USHORT:
            case STRUCT_VECTOR_SSHORT:
            case STRUCT_VECTOR_BSHORT:
            case STRUCT_VECTOR_PIXEL:
                location.kind = 9;
                if (copts.littleendian) {
                    location.operand.value = 14 - member->offset;
                } else {
                    location.operand.value = member->offset;
                }
                break;
            case STRUCT_VECTOR_UINT:
            case STRUCT_VECTOR_SINT:
            case STRUCT_VECTOR_BINT:
            case STRUCT_VECTOR_FLOAT:
                location.kind = 9;
                if (copts.littleendian) {
                    location.operand.value = 12 - member->offset;
                } else {
                    location.operand.value = member->offset;
                }
                break;
            case STRUCT_TYPE_STRUCT:
            case STRUCT_TYPE_UNION:
                location.kind = 9;
                location.operand.value = member->offset;
                break;
            default:
                CError_FATAL(516);
        }
        memberSize += emit_location_attribute(&location, AT_location, FALSE);
        *(SInt32*)(*dwarf->data + memberOffset) = memberSize;
    }

    if (copts.dwarf_siblings && hasMembers) {
        PATCH_SIBLING(symbol);
        AppendGListLong(dwarf, 4);
    }
    --*dwarf_depth_ptr;
#if VERSION >= VERSION_GC_2_7
    AddSymbol(
        symbol,
        dwarf->size + CurContext->offset - symbol->elf.st_value,
        Q_IMPLICIT_WEAK,
        symbol->sclass,
        1,
        2,
        DebugSect
    );
#endif
    if (!hasMembers) {
        PATCH_SIBLING(symbol);
    }
}

#if VERSION >= VERSION_GC_2_0
static void emit_class_type(TypeClass* tclass, DwarfRef* ref) {
#else
static void emit_class_type(TypeClass* tclass) {
#endif
    UInt32 offset;
    UInt16 tag;
    SInt32 size;
    ClassList* base;
    ObjMemberVar* ivar;
    DWInfo* info;
    TypeBitfield* bitfield;
    DWInfo* memberInfo;
    SInt32 memberOffset;
    SInt32 memberSize;
    Boolean hasChildren;
    DwarfLocationOperand location;
    CScopeObjectIterator iter;
    Object* object;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
#endif
    DwarfFixup* fixup;

    hasChildren = FALSE;
    offset = dwarf->size;
    info = find_or_create_dwinfo(TYPE(tclass));
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -89
#endif
    CError_ASSERT(560, info->typeNode);
#if VERSION < VERSION_GC_2_0
    for (fixup = info->typeNode->u.type.fixups; fixup; fixup = fixup->next) {
        if (copts.dwarf_relocations) {
            DWARF_BACKPATCH(fixup->reference, offset, symbol);
        } else {
            *(SInt32*)(*dwarf->data + fixup->offset) = offset;
        }
    }
    info->typeNode->u.type.fixups = fixup;
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -87
#endif
#endif
    if (tclass->mode == CLASS_MODE_CLASS) {
        tag = TAG_class_type;
    } else if (tclass->mode == CLASS_MODE_STRUCT) {
        tag = TAG_structure_type;
    } else if (tclass->mode == CLASS_MODE_UNION) {
        tag = TAG_union_type;
    } else {
        CError_FATAL(568);
    }
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -94
#endif
#if VERSION >= VERSION_GC_2_0
    if (tclass->classname) {
        symbol = DWARF_ENTRY_SYMBOL(
            DWARF_QualifiedName(tclass->nspace, tclass->classname), TK_EXTERN, tag, offset
        );
    } else {
        symbol = DWARF_ENTRY_SYMBOL(NULL, TK_STATIC, tag, offset + CurContext->offset);
    }
    ref->symbol = symbol;
#endif
#if VERSION >= VERSION_GC_2_0
    for (fixup = info->typeNode->u.type.fixups; fixup; fixup = fixup->next) {
        if (copts.dwarf_relocations) {
            DWARF_BACKPATCH(fixup->reference, offset, symbol);
        } else {
            *(SInt32*)(*dwarf->data + fixup->offset) = offset;
        }
    }
    info->typeNode->u.type.fixups = fixup;
#endif
    size = EMIT_ENTRY_HEADER(tag, symbol);
    if (tclass->classname) {
        AppendGListWord(dwarf, AT_name);
        size += 2;
        size += DW_String(tclass->classname->name);
    }
    AppendGListWord(dwarf, AT_byte_size);
    AppendGListLong(dwarf, tclass->size);
    size += 6;
    *(SInt32*)(*dwarf->data + offset) = size;
    CError_ASSERT(587, ++*dwarf_depth_ptr < 50);
    dwarf_entry_offsets[*dwarf_depth_ptr] = 0;

    if (tclass->bases) {
        for (base = tclass->bases; base; base = base->next) {
            hasChildren = TRUE;
            memberOffset = dwarf->size;
            memberSize = EMIT_ENTRY_HEADER(TAG_inheritance, symbol);
            AppendGListWord(dwarf, AT_name);
            memberSize += 2;
            memberSize += DW_String(base->base->classname->name);
            memberInfo = find_or_create_dwinfo(TYPE(base->base));
            GET_TYPE_DWARF_REF(memberInfo, 0, TRUE, NULL);
            memberSize += emit_dwarf_ref(memberInfo);
            switch (base->access) {
                case ACCESSPUBLIC:
                    AppendGListWord(dwarf, AT_public);
                    memberSize += 2;
                    memberSize += DW_String("");
                    break;
                case ACCESSPRIVATE:
                    AppendGListWord(dwarf, AT_private);
                    memberSize += 2;
                    memberSize += DW_String("");
                    break;
                case ACCESSPROTECTED:
                    AppendGListWord(dwarf, AT_protected);
                    memberSize += 2;
                    memberSize += DW_String("");
                    break;
                default:
                    break;
            }
            if (base->is_virtual) {
                AppendGListWord(dwarf, AT_virtual);
                memberSize += 2;
                memberSize += DW_String("");
            }
            location.kind = 9;
            location.operand.value = base->offset;
            memberSize += emit_location_attribute(&location, AT_location, FALSE);
            *(SInt32*)(*dwarf->data + memberOffset) = memberSize;
        }
    }

    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
        hasChildren = TRUE;
        if (ivar->type->type == TYPEBITFIELD) {
            bitfield = TYPE_BITFIELD(ivar->type);
            memberInfo = find_or_create_dwinfo(bitfield->bitfieldtype);
        } else {
            memberInfo = find_or_create_dwinfo(ivar->type);
            bitfield = NULL;
        }
        GET_TYPE_DWARF_REF(memberInfo, ivar->qual, TRUE, NULL);
        memberOffset = dwarf->size;
        memberSize = EMIT_ENTRY_HEADER(TAG_member, symbol);
        AppendGListWord(dwarf, AT_name);
        memberSize += 2;
        memberSize += DW_String(ivar->name->name);
        memberSize += emit_dwarf_ref(memberInfo);
        switch (ivar->access) {
            case ACCESSPUBLIC:
                AppendGListWord(dwarf, AT_public);
                memberSize += 2;
                memberSize += DW_String("");
                break;
            case ACCESSPRIVATE:
                AppendGListWord(dwarf, AT_private);
                memberSize += 2;
                memberSize += DW_String("");
                break;
            case ACCESSPROTECTED:
                AppendGListWord(dwarf, AT_protected);
                memberSize += 2;
                memberSize += DW_String("");
                break;
            default:
                break;
        }
        if (bitfield) {
            AppendGListWord(dwarf, AT_bit_offset);
            AppendGListWord(dwarf, bitfield->offset);
            AppendGListWord(dwarf, AT_bit_size);
            AppendGListLong(dwarf, bitfield->bitlength);
            memberSize += 10;
        }
        location.kind = 9;
        location.operand.value = ivar->offset;
        memberSize += emit_location_attribute(&location, AT_location, FALSE);
        *(SInt32*)(*dwarf->data + memberOffset) = memberSize;
    }

    CScope_InitObjectIterator(&iter, tclass->nspace);
    for (;;) {
        if (!(object = OBJECT(CScope_NextObjectIteratorObject(&iter)))) {
            break;
        }
        if (object->datatype == DFUNC || object->datatype == DVFUNC) {
            continue;
        }
        if (object->datatype == DDATA) {
            hasChildren = TRUE;
            memberOffset = dwarf->size;
            memberSize = EMIT_ENTRY_HEADER(TAG_typedef, symbol);
            AppendGListWord(dwarf, AT_name);
            memberSize += 2;
            memberSize += DW_String(object->name->name);
            memberInfo = find_or_create_dwinfo(object->type);
            GET_TYPE_DWARF_REF(memberInfo, object->qual, TRUE, NULL);
            memberSize += emit_dwarf_ref(memberInfo);
            *(SInt32*)(*dwarf->data + memberOffset) = memberSize;
        }
    }

    if (copts.dwarf_siblings && hasChildren) {
        PATCH_SIBLING(symbol);
        AppendGListLong(dwarf, 4);
    }
    --*dwarf_depth_ptr;
#if VERSION >= VERSION_GC_2_7
    AddSymbol(
        symbol,
        dwarf->size + CurContext->offset - symbol->elf.st_value,
        Q_IMPLICIT_WEAK,
        symbol->sclass,
        1,
        2,
        DebugSect
    );
#endif
    if (!hasChildren) {
        PATCH_SIBLING(symbol);
    }
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -96
#endif
#if VERSION >= VERSION_GC_2_0
static void emit_array_type(Type* type, DwarfRef* ref) {
#else
static void emit_array_type(Type* type) {
#endif
    unsigned int offset;
    DWInfo* elementInfo;
    DWInfo* info;
    SInt32 size;
    SInt32 blockOffset;
    SInt16 blockSize;
    SInt32 upperBound;
#if VERSION >= VERSION_GC_2_7
    BE_SymNode* symbol;
#endif

    offset = dwarf->size;
    elementInfo = find_or_create_dwinfo(TPTR_TARGET(type));
    info = find_or_create_dwinfo(type);
    CError_ASSERT(789, info->typeNode);
#if VERSION >= VERSION_GC_2_7
    ref->symbol = symbol =
        DWARF_MakeEntrySymbol(NULL, TK_STATIC, TAG_array_type, (SInt32)offset + CurContext->offset);
    info->typeNode->u.type.fixups = DWARF_RESOLVE_FIXUPS(info->typeNode->u.type.fixups, offset, symbol);
    GET_TYPE_DWARF_REF(elementInfo, 0, TRUE, NULL);
    size = EMIT_ENTRY_HEADER(TAG_array_type, symbol);
#else
#if VERSION >= VERSION_GC_2_0
    ref->symbol = NULL;
#endif
    info->typeNode->u.type.fixups = DWARF_RESOLVE_FIXUPS(info->typeNode->u.type.fixups, offset, NULL);
    GET_TYPE_DWARF_REF(elementInfo, 0, TRUE, NULL);
    size = EMIT_ENTRY_HEADER(TAG_array_type, NULL);
#endif
    AppendGListWord(dwarf, AT_subscr_data);
    size += 2;
    blockOffset = dwarf->size;
    AppendGListWord(dwarf, 0);
    blockSize = 2;
    AppendGListByte(dwarf, FMT_FT_C_C);
    AppendGListWord(dwarf, FT_long);
    AppendGListLong(dwarf, 0);
    if (TPTR_TARGET(type)->size == 0) {
        upperBound = 0;
    } else if (type->size == TPTR_TARGET(type)->size) {
        upperBound = 0;
    } else {
        upperBound = type->size / TPTR_TARGET(type)->size - 1;
    }
    AppendGListLong(dwarf, upperBound);
    AppendGListByte(dwarf, FMT_ET);
    blockSize += 12;
    blockSize += emit_dwarf_ref(elementInfo);
    *(UInt16*)(*dwarf->data + blockOffset) = blockSize - 2;
    size += blockSize;
    *(SInt32*)(*dwarf->data + offset) = size;
#if VERSION >= VERSION_GC_2_7
    AddSymbol(
        symbol,
        dwarf->size + CurContext->offset - symbol->elf.st_value,
        0,
        symbol->sclass,
        1,
        2,
        DebugSect
    );
#endif
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static UInt16 get_integral_type_code(Type* type) {
    switch (((TypeIntegral*)type)->integral) {
        case IT_BOOL:
            return FT_unsigned_char;
        case IT_CHAR:
            return FT_char;
        case IT_SCHAR:
            return FT_signed_char;
        case IT_UCHAR:
            return FT_unsigned_char;
        case IT_SHORT:
            return FT_signed_short;
        case IT_USHORT:
            return FT_unsigned_short;
        case IT_INT:
            return FT_integer;
        case IT_UINT:
            return FT_unsigned_integer;
        case IT_LONG:
            return FT_long;
        case IT_ULONG:
            return FT_unsigned_long;
        case IT_LONGLONG:
            return FT_long_long;
        case IT_ULONGLONG:
            return FT_unsigned_long_long;
        case IT_FLOAT:
            return FT_float;
        case IT_SHORTDOUBLE:
            return FT_dbl_prec_float;
        case IT_DOUBLE:
            return FT_dbl_prec_float;
        case IT_LONGDOUBLE:
            return FT_dbl_prec_float;
        case IT_VEC2X32FLOAT:
            return FT_MW_vec2x32float;
        case IT_WCHAR_T:
        default:
            return 0;
    }
}

enum {
    MOD_none = 0,
    MOD_pointer_to = 1,
    MOD_reference_to = 2,
    MOD_const = 3,
    MOD_volatile = 4
};

#define NEXT_QUAL(qual) \
    (((qual) & Q_REFERENCE)         ? ((qual) -= Q_REFERENCE, MOD_reference_to) \
         : ((qual) & Q_CONST)       ? ((qual) -= Q_CONST, MOD_const) \
         : ((qual) & Q_INLINE_DATA) ? MOD_const \
         : ((qual) & Q_VOLATILE)    ? ((qual) -= Q_VOLATILE, MOD_volatile) \
                                    : MOD_none)

static DwarfRef create_type_dwarf_ref(Type* type, UInt16 qual, Boolean referenceOnly) {
    Type* t;
    DwarfRef ref;
    DwarfRef typeRef;
    QualNode* tail;
    QualNode* head;
    UInt16 ptrQual;
    UInt8 ptrKind;
    UInt8 kind;
    DWInfo* info;

    t = type;
#if VERSION >= VERSION_GC_2_0
    memset(&ref, 0, sizeof(ref));
#endif
    head = galloc(sizeof(QualNode));
    ref.qualifiers = tail = head;
    head->next = NULL;
    ref.valid = FALSE;

    while (t->type == TYPEPOINTER) {
        ptrQual = TYPE_POINTER(t)->qual;
        if (ptrQual & Q_REFERENCE) {
            ptrQual -= Q_REFERENCE;
            ptrKind = MOD_reference_to;
        } else {
            ptrKind = MOD_pointer_to;
        }
        while ((kind = NEXT_QUAL(ptrQual))) {
            tail->type = kind;
            tail->next = galloc(sizeof(QualNode));
            tail = tail->next;
        }
        tail->type = ptrKind;
        t = TPTR_TARGET(t);
        if (t->type == TYPEPOINTER) {
            tail->next = galloc(sizeof(QualNode));
            tail = tail->next;
        }
    }
    tail->next = NULL;

    if (!IS_TYPE_ENUM(t) &&
        (IS_TYPE_INT_OR_ENUM(t) || IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(t) || IS_TYPE_VEC2X32FLOAT(t))) {
        ref.tag = AT_mod_fund_type;
        while ((kind = NEXT_QUAL(qual))) {
            tail->next = galloc(sizeof(QualNode));
            tail = tail->next;
            tail->type = kind;
            tail->next = NULL;
        }
        ref.num = get_integral_type_code(t);
        return ref;
    }
    if (IS_TYPE_VOID(t)) {
        ref.tag = AT_fund_type;
        ref.num = FT_pointer;
        return ref;
    }
    ref.tag = AT_mod_u_d_type;
    while ((kind = NEXT_QUAL(qual))) {
        tail->next = galloc(sizeof(QualNode));
        tail = tail->next;
        tail->type = kind;
        tail->next = NULL;
    }
    info = find_or_create_dwinfo(t);
#if VERSION >= VERSION_GC_2_0
    memset(&typeRef, 0, sizeof(typeRef));
#endif
    typeRef = GET_TYPE_DWARF_REF(info, 0, referenceOnly, NULL);
    ref.num = typeRef.num;
#if VERSION >= VERSION_GC_2_0
    ref.symbol = typeRef.symbol;
#endif
    ref.typeInfo = info;
    return ref;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -81
#endif
static void set_type_dwarf_ref(Type* type, UInt16 qual, Boolean referenceOnly, DwarfRef* ref) {
    switch (type->type) {
        case TYPEENUM:
            ref->tag = AT_user_def_type;
            if (!referenceOnly) {
                ref->num = dwarf->size + CurContext->offset;
                ref->valid = TRUE;
                EMIT_TYPE(emit_enum_type, type, ref);
            }
            return;
        case TYPESTRUCT:
            ref->tag = AT_user_def_type;
            if (!referenceOnly) {
                ref->num = dwarf->size + CurContext->offset;
                ref->valid = TRUE;
                EMIT_TYPE(emit_struct_type, TYPE_STRUCT(type), ref);
            }
            return;
        case TYPEPOINTER:
            *ref = create_type_dwarf_ref(type, qual, referenceOnly);
            return;
        case TYPEARRAY:
            ref->tag = AT_user_def_type;
            if (!referenceOnly) {
                ref->num = dwarf->size + CurContext->offset;
                ref->valid = TRUE;
                EMIT_TYPE(emit_array_type, type, ref);
            }
            return;
        case TYPECLASS:
            ref->tag = AT_user_def_type;
            if (!referenceOnly) {
                ref->num = dwarf->size + CurContext->offset;
                ref->valid = TRUE;
                EMIT_TYPE(emit_class_type, TYPE_CLASS(type), ref);
            }
            return;
#if VERSION < VERSION_GC_2_0
        case TYPEFUNC:
            ref->tag = AT_user_def_type;
            if (!referenceOnly) {
                ref->num = dwarf->size + CurContext->offset;
                ref->valid = TRUE;
            }
            return;
#endif
        case TYPEBITFIELD:
            ref->tag = AT_fund_type;
            ref->num = FT_void;
            ref->valid = TRUE;
            return;
#if VERSION < VERSION_GC_2_0
        case TYPEMEMBERPOINTER:
            ref->tag = AT_user_def_type;
            if (!referenceOnly) {
                ref->num = dwarf->size + CurContext->offset;
                ref->valid = TRUE;
            }
            return;
#endif
        default:
            CError_FATAL(993);
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static struct DWInfo* find_or_create_dwinfo(struct Type* type) {
    unsigned short low, high, middleLow, middleHigh;
    int hash;
    int bucketIndex;
    DWInfo* record;
    DWInfo* newRecord;

    low = (UInt32)type & 511;
    high = ((UInt32)type >> 23) & 511;
    middleLow = ((UInt32)type >> 9) & 127;
    middleHigh = ((UInt32)type >> 16) & 127;
    bucketIndex = hash = low ^ high ^ (middleLow ^ middleHigh);
    for (record = dwinfo_buckets[bucketIndex]; record; record = record->next) {
        if (record->type == type) {
            return record;
        }
    }
    newRecord = galloc(sizeof(DWInfo));
    memclrw(newRecord, sizeof(DWInfo));
    newRecord->next = dwinfo_buckets[hash];
    dwinfo_buckets[hash] = newRecord;
    newRecord->type = type;
    return newRecord;
}

#if VERSION >= VERSION_GC_2_0
static DwarfRef get_type_dwarf_ref(
    DWInfo* info, UInt16 qual, Boolean referenceOnly, BE_SymNode* symbol
) {
#else
static DwarfRef get_type_dwarf_ref(DWInfo* info, UInt16 qual, Boolean referenceOnly) {
#endif
    Type* type;

    if (info->rec.valid) {
        return info->rec;
    }
    type = info->type;
    info->rec.qualifiers = NULL;
    info->rec.num = 0;
    info->rec.typeInfo = NULL;
    if (!IS_TYPE_ENUM(type) &&
        (IS_TYPE_INT_OR_ENUM(type) ||
         IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) ||
         IS_TYPE_VEC2X32FLOAT(type))) {
        info->rec.tag = AT_fund_type;
        info->rec.num = get_integral_type_code(type);
        info->rec.valid = TRUE;
    } else if (IS_TYPE_VOID(type)) {
        info->rec.tag = AT_fund_type;
        info->rec.num = FT_void;
        info->rec.valid = TRUE;
#if VERSION >= VERSION_GC_2_0
    } else if (IS_TYPE_FUNC(type) || IS_TYPE_MEMBERPOINTER(type)) {
        info->rec.tag = AT_user_def_type;
        if (!referenceOnly) {
            info->rec.num = dwarf->size + CurContext->offset;
#if VERSION >= VERSION_GC_2_0
            info->rec.symbol = symbol;
#endif
            info->rec.valid = TRUE;
        }
#endif
    } else {
        set_type_dwarf_ref(type, qual, referenceOnly, &info->rec);
    }
    return info->rec;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -82
#endif
static int emit_location_attribute(
    DwarfLocationOperand* location, UInt16 attribute, unsigned char shouldDereference
) {
    int attributeSize;
    int expressionSize;
    SInt32 expressionSizeOffset;

    if (location->kind == 0) {
        return 0;
    }

    AppendGListWord(dwarf, attribute);
    attributeSize = 2;
    expressionSizeOffset = dwarf->size;
    AppendGListNoData(dwarf, attributeSize);
    attributeSize += 2;
    expressionSize = 0;

    switch (location->kind) {
        case 1:
            AppendGListByte(dwarf, OP_REG);
            AppendGListLong(dwarf, location->operand.registers.first);
            expressionSize += 5;
            if (location->operand.registers.offset != 0) {
                AppendGListByte(dwarf, OP_CONST);
                AppendGListLong(dwarf, location->operand.registers.offset);
                AppendGListByte(dwarf, OP_ADD);
                expressionSize += 6;
            }
            break;
        case 8:
            AppendGListByte(dwarf, location->operand.registers.second + 0x50);
            AppendGListByte(dwarf, -0x6d);
            AppendGListByte(dwarf, 4);
            AppendGListByte(dwarf, location->operand.registers.first + 0x50);
            AppendGListByte(dwarf, -0x6d);
            AppendGListByte(dwarf, 4);
            expressionSize += 6;
            break;
        case 2:
            AppendGListByte(dwarf, OP_REG);
            AppendGListLong(dwarf, location->operand.registers.first + 0x20);
            expressionSize += 5;
            break;
        case 3:
            AppendGListByte(dwarf, -0x7f);
            AppendGListLong(dwarf, location->operand.registers.first);
            expressionSize += 5;
            break;
        case 4:
            AppendGListByte(dwarf, OP_REG);
            AppendGListLong(dwarf, location->operand.registers.first + 0x464);
            expressionSize += 5;
            break;
        case 12:
            AppendGListByte(dwarf, OP_REG);
            AppendGListLong(dwarf, location->operand.registers.first + 0x485);
            expressionSize += 5;
            break;
        case 7:
            AppendGListByte(dwarf, OP_ADDR);
            WriteDebugRelocation(
                DebugSect,
                dwarf->size,
                DWARF_RELOC_SYMBOL(location->operand.address.object),
                NULL,
                location->operand.address.offset,
                CurContext->offset
            );
            AppendGListLong(dwarf, copts.dwarf_relocations ? 0 : location->operand.address.offset);
            expressionSize += 5;
            break;
        case 5:
            AppendGListByte(dwarf, OP_BASEREG);
            AppendGListLong(dwarf, location->operand.address.reg);
            AppendGListByte(dwarf, OP_CONST);
            AppendGListLong(dwarf, location->operand.address.offset);
            AppendGListByte(dwarf, OP_ADD);
            expressionSize += 11;
            if (shouldDereference) {
                AppendGListByte(dwarf, OP_DEREF);
                expressionSize += 1;
            }
            break;
        case 11:
            AppendGListByte(dwarf, OP_CONST);
            AppendGListLong(dwarf, location->metadata.value);
            AppendGListByte(dwarf, OP_ADD);
            AppendGListByte(dwarf, OP_DEREF);
            AppendGListByte(dwarf, OP_CONST);
            AppendGListLong(dwarf, location->operand.value);
            AppendGListByte(dwarf, OP_ADD);
            AppendGListByte(dwarf, OP_DEREF);
            expressionSize += 14;
            break;
        case 6:
            AppendGListByte(dwarf, OP_ADDR);
            WriteDebugRelocation(
                DebugSect,
                dwarf->size,
                DWARF_RELOC_SYMBOL(location->operand.objectOffset.object),
                NULL,
                location->operand.objectOffset.offset,
                CurContext->offset
            );
            AppendGListLong(
                dwarf, copts.dwarf_relocations ? 0 : location->operand.objectOffset.offset
            );
            expressionSize += 5;
            break;
        case 9:
            AppendGListByte(dwarf, OP_CONST);
            AppendGListLong(dwarf, location->operand.value);
            AppendGListByte(dwarf, OP_ADD);
            expressionSize += 6;
            break;
        case 10:
            AppendGListByte(dwarf, OP_BASEREG);
            AppendGListLong(dwarf, location->operand.registers.first);
            expressionSize += 5;
            break;
        default:
            CError_FATAL(1182);
            break;
    }

    *(UInt16*)(*dwarf->data + expressionSizeOffset) = expressionSize;
    attributeSize += expressionSize;
    return attributeSize;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION >= VERSION_GC_2_0
static SInt32 emit_entry_header(SInt16 tag, BE_SymNode* symbol) {
#else
static SInt32 emit_entry_header(SInt16 tag) {
#endif
    SInt32 depth;
    SInt32 patchOffset;

    *dwarf_pending_ptr = TRUE;
    depth = *dwarf_depth_ptr;
    if (depth >= 0 && (patchOffset = dwarf_entry_offsets[depth])) {
        if (copts.dwarf_relocations) {
#if VERSION >= VERSION_GC_2_7
            BackPatchRelocationAddend(
                dwarf_entry_relocs[*dwarf_depth_ptr],
                DWARF_SymbolOffset(dwarf_last_entry_symbol, dwarf->size + CurContext->offset),
                dwarf_last_entry_symbol
            );
#elif VERSION >= VERSION_GC_2_0
            BackPatchRelocationAddend(
                dwarf_entry_relocs[depth], dwarf->size + CurContext->offset, dwarf_last_entry_symbol
            );
#else
            BackPatchRelocationAddend(dwarf_entry_relocs[depth], dwarf->size + CurContext->offset);
#endif
        } else {
            *(SInt32*)(*dwarf->data + patchOffset) = dwarf->size + CurContext->offset;
        }
    }
    AppendGListLong(dwarf, 0);
    AppendGListWord(dwarf, tag);
    AppendGListWord(dwarf, AT_sibling);
#if VERSION >= VERSION_GC_2_0
    dwarf_last_entry_symbol = symbol;
    if ((depth = *dwarf_depth_ptr) >= 0) {
        dwarf_entry_offsets[depth] = dwarf->size;
        dwarf_entry_relocs[*dwarf_depth_ptr] =
            WriteDebugRelocation(DebugSect, dwarf->size, symbol, DebugSect, 0, CurContext->offset);
    } else {
        CurContext->pendingSymbol = symbol;
        CurContext->pendingReference =
            WriteDebugRelocation(DebugSect, dwarf->size, symbol, DebugSect, 0, CurContext->offset);
        CurContext->pendingPosition = dwarf->size;
    }
#else
    if ((depth = *dwarf_depth_ptr) >= 0) {
        dwarf_entry_offsets[depth] = dwarf->size;
        dwarf_entry_relocs[*dwarf_depth_ptr] =
            WriteDebugRelocation(DebugSect, dwarf->size, NULL, DebugSect, 0, CurContext->offset);
    } else {
        CurContext->pendingReference =
            WriteDebugRelocation(DebugSect, dwarf->size, NULL, DebugSect, 0, CurContext->offset);
        CurContext->pendingPosition = dwarf->size;
    }
#endif
    AppendGListLong(dwarf, 0);
    return 12;
}

void newDWARF_file(void* osection) {
    ObjGenSection* section = osection;
    DwarfNode* node;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
#endif
    DwarfNode* entry;
    UInt8 language;
    SInt32* position;

    if (!section->debugState) {
        section->debugState = DefaultContext;
        section->debugState->section = osection;
        if (InitGList(&section->debugState->lines, 1000)) {
            CError_NoMem();
        }
        if (InitGList(&section->debugState->info, 5000)) {
            CError_NoMem();
        }
    }
    SetScope(section->debugState);
    node = galloc(sizeof(DwarfNode));
    memclrw(node, sizeof(DwarfNode));
    dwarf_node_head->prev = node;
    node->next = dwarf_node_head;
    dwarf_node_head = node;
    node->kind = 0xffff;
    dwarf_node_head->next->scope = CurContext;
    entry = dwarf_node_head->next;
    entry->kind = 0x11;
    if (copts.cplusplus) {
        language = LANG_C_PLUS_PLUS;
    } else {
        language = LANG_C89;
    }
    entry->u.unit.language = language;
#if VERSION >= VERSION_GC_2_0
    entry->u.unit.symbol = symbol = DWARF_LINE_SYMBOL(NULL, TK_STATIC, dwarfline->size, CurContext);
#endif
    AppendGListLong(dwarfline, 4);
    AppendGListLong(dwarfline, 0);
#if VERSION >= VERSION_GC_2_7
    AddSymbol(symbol, dwarfline->size - symbol->elf.st_value, 0, symbol->sclass, 1, 2, LineSect);
#endif
    position = (SInt32*)*dwarfline->data;
    *position = *position + 4;
}

static char* dwarf_file_name(char* path, Boolean fullPath) {
    SInt32 i;

    if (!fullPath) {
        for (i = strlen(path); i >= 0; i--) {
            if (path[i] == '\\' || path[i] == '/') {
                return &path[i + 1];
            }
        }
    }
    return path;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -95
#endif
#if VERSION >= VERSION_GC_2_0
static void emit_compile_unit(ObjGenSection* section, UInt8 language, BE_SymNode* symbol) {
#else
static void emit_compile_unit(ObjGenSection* section, UInt8 language) {
#endif
    SInt32 offset;
    UInt32 lang;
    SInt32 size;
    char* name;
    char path[1024];
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* unitSymbol;
#endif

    lang = language;
    offset = dwarf->size;
#if VERSION >= VERSION_GC_2_0
    unitSymbol = DWARF_ENTRY_SYMBOL(NULL, TK_STATIC, TAG_compile_unit, offset + CurContext->offset);
#endif
    size = EMIT_ENTRY_HEADER(TAG_compile_unit, unitSymbol);
    AppendGListWord(dwarf, AT_producer);
    size += 2;
    size += DW_String("MW EABI PPC C-Compiler");
    AppendGListWord(dwarf, AT_name);
    size += 2;
    name = GetSourceFileName(section);
    strncpy(path, name, sizeof(path));
    name = dwarf_file_name(path, copts.absolutepath);
    size += DW_String(name);
    AppendGListWord(dwarf, AT_language);
    AppendGListData(dwarf, &lang, sizeof(lang));
    AppendGListWord(dwarf, AT_low_pc);
    WriteDebugRelocation(DebugSect, dwarf->size, NULL, section, 0, CurContext->offset);
    AppendGListLong(dwarf, 0);
    AppendGListWord(dwarf, AT_high_pc);
    WriteDebugRelocation(DebugSect, dwarf->size, NULL, section, text->size, CurContext->offset);
    AppendGListLong(dwarf, copts.dwarf_relocations ? 0 : text->size);
    AppendGListWord(dwarf, AT_stmt_list);
#if VERSION >= VERSION_GC_2_0
    WriteDebugRelocation(
        DebugSect,
        dwarf->size,
        symbol,
        LineSect,
        symbol ? 0 : CurContext->lineSectionOffset,
        CurContext->offset
    );
#else
    WriteDebugRelocation(
        DebugSect, dwarf->size, NULL, LineSect, CurContext->lineSectionOffset, CurContext->offset
    );
#endif
    AppendGListLong(dwarf, copts.dwarf_relocations ? 0 : CurContext->lineSectionOffset);
    size += 24;
    *(SInt32*)(*dwarf->data + offset) = size;
    WriteDebugRelocation(LineSect, 4, NULL, section, 0, CurContext->lineSectionOffset);
    if (!copts.full_dwarf) {
        CurContext->lineBaseRelocated = TRUE;
    }
#if VERSION >= VERSION_GC_2_7
    AddSymbol(
        unitSymbol,
        dwarf->size + CurContext->offset - unitSymbol->elf.st_value,
        0,
        unitSymbol->sclass,
        4,
        2,
        DebugSect
    );
#endif
    CError_ASSERT(1360, ++*dwarf_depth_ptr < 50);
    dwarf_entry_offsets[*dwarf_depth_ptr] = 0;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

void newDWARF_line(unsigned int line, unsigned int offset) {
    int outputPosition;

    outputPosition = *(int*)*dwarfline->data;
    AppendGListLong(dwarfline, line);
    AppendGListWord(dwarfline, -1);
    AppendGListLong(dwarfline, offset);
    *(int*)*dwarfline->data = outputPosition + 10;
}

void correctDWARF_line(unsigned int line, unsigned int offset) {
    int savedCount;

    savedCount = *(int*)*dwarfline->data;
    dwarfline->size -= 10;
    *(int*)*dwarfline->data = savedCount - 10;
    savedCount = *(int*)*dwarfline->data;
    AppendGListLong(dwarfline, line);
    AppendGListWord(dwarfline, -1);
    AppendGListLong(dwarfline, offset);
    *(int*)*dwarfline->data = savedCount + 10;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -98
#endif
static void emit_function_type(DwarfFixup** fixups, DWInfo* info) {
    unsigned int offset;
    SInt32 size;
    TypeFunc* tfunc;
    DWInfo* typeInfo;
    FuncArg* arg;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
#endif

    tfunc = TYPE_FUNC(info->type);
    offset = dwarf->size;
#if VERSION >= VERSION_GC_2_0
    symbol = DWARF_ENTRY_SYMBOL(
        NULL, TK_STATIC, TAG_subroutine_type, (SInt32)offset + CurContext->offset
    );
#endif
    *fixups = DWARF_RESOLVE_FIXUPS(*fixups, offset, symbol);
    GET_TYPE_DWARF_REF(info, 0, FALSE, symbol);
    size = EMIT_ENTRY_HEADER(TAG_subroutine_type, symbol);
    if (tfunc->functype) {
        typeInfo = find_or_create_dwinfo(tfunc->functype);
        GET_TYPE_DWARF_REF(typeInfo, 0, TRUE, NULL);
        size += emit_dwarf_ref(typeInfo);
    }
    CError_ASSERT(1414, info->typeNode);
#if VERSION >= VERSION_GC_2_0
    currentDwarfFunction = currentDwarfScope = info->typeNode;
#else
    currentDwarfScope = info->typeNode;
#endif
    *dwarf_pending_ptr = FALSE;
    arg = tfunc->args;
    if (arg && copts.cplusplus && (tfunc->flags & FUNC_METHOD)) {
        arg = arg->next;
    }
    *(SInt32*)(*dwarf->data + offset) = size;
    CError_ASSERT(1423, ++*dwarf_depth_ptr < 50);
    dwarf_entry_offsets[*dwarf_depth_ptr] = 0;
    for (; arg; arg = arg->next) {
        if (arg->type) {
            currentDwarfNode = currentDwarfScope;
            InsertDwarfNode();
            offset = dwarf->size;
            size = EMIT_ENTRY_HEADER(TAG_formal_parameter, symbol);
            typeInfo = find_or_create_dwinfo(arg->type);
            GET_TYPE_DWARF_REF(typeInfo, 0, TRUE, NULL);
            size += emit_dwarf_ref(typeInfo);
            *(SInt32*)(*dwarf->data + offset) = size;
        }
    }
#if VERSION >= VERSION_GC_2_0
    currentDwarfFunction = currentDwarfScope = NULL;
#else
    currentDwarfScope = NULL;
#endif
    if (copts.dwarf_siblings && *dwarf_pending_ptr) {
        PATCH_SIBLING(symbol);
        AppendGListLong(dwarf, 4);
    }
    --*dwarf_depth_ptr;
#if VERSION >= VERSION_GC_2_7
    AddSymbol(
        symbol,
        dwarf->size + CurContext->offset - symbol->elf.st_value,
        0,
        symbol->sclass,
        1,
        2,
        DebugSect
    );
#endif
    *dwarf_pending_ptr = FALSE;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static void emit_member_pointer_type(DwarfFixup** fixups, DWInfo* info) {
    unsigned int offset;
    TypeMemberPointer* tmemp;
    SInt32 size;
    DWInfo* typeInfo;
    DwarfRef saved;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
#endif

    tmemp = TYPE_MEMBER_POINTER(info->type);
    offset = dwarf->size;
#if VERSION >= VERSION_GC_2_0
    symbol = DWARF_ENTRY_SYMBOL(
        NULL, TK_STATIC, TAG_ptr_to_member_type, (SInt32)offset + CurContext->offset
    );
#endif
    *fixups = DWARF_RESOLVE_FIXUPS(*fixups, offset, symbol);
    GET_TYPE_DWARF_REF(info, 0, FALSE, symbol);
    size = EMIT_ENTRY_HEADER(TAG_ptr_to_member_type, symbol);
    if (tmemp->ty1) {
        typeInfo = find_or_create_dwinfo(tmemp->ty1);
        GET_TYPE_DWARF_REF(typeInfo, 0, TRUE, NULL);
        size += emit_dwarf_ref(typeInfo);
    }
    if (tmemp->ty2) {
        typeInfo = find_or_create_dwinfo(tmemp->ty2);
        GET_TYPE_DWARF_REF(typeInfo, 0, TRUE, NULL);
        saved = typeInfo->rec;
        typeInfo->rec.tag = AT_containing_type;
        size += emit_dwarf_ref(typeInfo);
        typeInfo->rec = saved;
    }
    *(SInt32*)(*dwarf->data + offset) = size;
#if VERSION >= VERSION_GC_2_7
    AddSymbol(
        symbol,
        dwarf->size + CurContext->offset - symbol->elf.st_value,
        0,
        symbol->sclass,
        1,
        2,
        DebugSect
    );
#endif
}

#if VERSION >= VERSION_GC_2_0
static SInt32 emit_function_entry(DwarfBlock* block) {
#else
static SInt32 emit_function_entry(
    Object* func,
    SInt32 codeSize,
    SInt32 codeOffset,
    DwarfLocationOperand* returnOperand,
    PendingObject* list
) {
#endif
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -117
#endif
#if VERSION >= VERSION_GC_2_0
    Object* func;
#endif
    SInt32 offset;
#if VERSION >= VERSION_GC_2_0
    DwarfLocationOperand* returnOperand;
#endif
    DwarfNode* node;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* funcSymbol;
#endif
#if VERSION >= VERSION_GC_2_0
    SInt32 codeSize;
#endif
#if VERSION >= VERSION_GC_2_0
    SInt32 codeOffset;
#endif
    DwarfRef classRef;
    SInt32 size;
    DWInfo* info;
    DwarfFixup* fixup;
    ObjGenRelocation* reloc;
#if VERSION >= VERSION_GC_2_0
    PendingObject* list;
#endif
#if VERSION >= VERSION_GC_2_0
    UInt16 tag;
#endif
    PendingObject* pending;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* entrySymbol;
    BE_SymNode* classSymbol;
#endif
#if VERSION >= VERSION_GC_2_7
    BE_SymNode* symbol;
#endif

#if VERSION >= VERSION_GC_2_0
    func = block->object;
    codeSize = block->codeSize;
    codeOffset = block->codeOffset;
    returnOperand = block->returnOperand;
    list = block->pendingObjects;
#endif
#if VERSION >= VERSION_GC_2_0
    funcSymbol = MakeSymbolTableEntry(func);
#endif
    offset = dwarf->size;
#if VERSION >= VERSION_GC_2_0
    memset(&classRef, 0, sizeof(classRef));
#endif
#if VERSION >= VERSION_GC_2_0
    if (CParser_HasInternalLinkage(func)) {
        tag = TAG_subroutine;
    } else {
        tag = TAG_global_subroutine;
    }
#endif
#if VERSION >= VERSION_GC_2_7
    block->symbol = symbol = DWARF_MakeEntrySymbol(
        CMangler_GetLinkName(func), func->sclass, tag, offset + CurContext->offset
    );
    block->qual = func->qual;
    size = EMIT_ENTRY_HEADER(tag, symbol);
#elif VERSION >= VERSION_GC_2_0
    block->symbol = NULL;
    block->qual = func->qual;
    size = EMIT_ENTRY_HEADER(tag, NULL);
#else
    if (CParser_HasInternalLinkage(func)) {
        size = emit_entry_header(TAG_subroutine);
    } else {
        size = emit_entry_header(TAG_global_subroutine);
    }
#endif
    AppendGListWord(dwarf, AT_name);
    size += 2;
    size += DW_String(func->name->name);
    if (copts.cplusplus && func->name->name != CMangler_GetLinkName(func)->name) {
        AppendGListWord(dwarf, AT_MW_linkage_name);
        size += 2;
        size += DW_String(CMangler_GetLinkName(func)->name);
    }
    if (copts.cplusplus && (TYPE_FUNC(func->type)->flags & FUNC_METHOD)) {
        AppendGListWord(dwarf, AT_member);
        info = find_or_create_dwinfo(TYPE(TYPE_METHOD(func->type)->theclass));
        classRef = GET_TYPE_DWARF_REF(info, 0, TRUE, NULL);
#if VERSION >= VERSION_GC_2_0
        classSymbol = classRef.symbol;
        reloc = WriteDebugRelocation(
            DebugSect,
            dwarf->size,
            classSymbol,
            DebugSect,
            classSymbol ? 0 : classRef.num,
            CurContext->offset
        );
#else
        reloc = WriteDebugRelocation(
            DebugSect, dwarf->size, NULL, DebugSect, classRef.num, CurContext->offset
        );
#endif
        if (!classRef.valid) {
            CError_ASSERT(1540, info->typeNode);
            fixup = galloc(sizeof(DwarfFixup));
            fixup->reference = reloc;
            fixup->offset = dwarf->size;
            fixup->next = info->typeNode->u.type.fixups;
            info->typeNode->u.type.fixups = fixup;
        }
        AppendGListLong(dwarf, copts.dwarf_relocations ? 0 : classRef.num);
        size += 6;
    }
    if (returnOperand->kind) {
        info = find_or_create_dwinfo(returnOperand->metadata.type);
        GET_TYPE_DWARF_REF(info, 0, TRUE, NULL);
        if (returnOperand->kind == 5 || returnOperand->kind == 10) {
            size += emit_location_attribute(returnOperand, AT_return_addr, TRUE);
        }
        size += emit_dwarf_ref(info);
    }
    AppendGListWord(dwarf, AT_low_pc);
#if VERSION >= VERSION_GC_2_0
    WriteDebugRelocation(DebugSect, dwarf->size, funcSymbol, NULL, 0, CurContext->offset);
#else
    WriteDebugRelocation(DebugSect, dwarf->size, func, NULL, 0, CurContext->offset);
#endif
    AppendGListLong(dwarf, copts.dwarf_relocations ? 0 : codeOffset);
    AppendGListWord(dwarf, AT_high_pc);
#if VERSION >= VERSION_GC_2_7
    WriteDebugRelocation(
        DebugSect, dwarf->size, funcSymbol, CurContext->section, codeSize, CurContext->offset
    );
    AppendGListLong(dwarf, copts.dwarf_relocations ? 0 : (codeOffset += codeSize));
#else
    WriteDebugRelocation(
        DebugSect,
        dwarf->size,
        NULL,
        CurContext->section,
#if VERSION >= VERSION_GC_2_0
        codeOffset += codeSize,
#else
        codeOffset + codeSize,
#endif
        CurContext->offset
    );
#if VERSION >= VERSION_GC_2_0
    AppendGListLong(dwarf, copts.dwarf_relocations ? 0 : codeOffset);
#else
    AppendGListLong(dwarf, copts.dwarf_relocations ? 0 : codeOffset + codeSize);
#endif
#endif
    size += 12;
    if (!copts.full_dwarf) {
        if (CurContext != DefaultContext) {
            AppendGListWord(dwarf, AT_stmt_list);
            WriteDebugRelocation(
                DebugSect,
                dwarf->size,
                NULL,
                LineSect,
                CurContext->lineSectionOffset,
                CurContext->offset
            );
            AppendGListLong(dwarf, copts.dwarf_relocations ? 0 : CurContext->lineSectionOffset);
            size += 6;
        }
        if (!CurContext->lineBaseRelocated) {
            CurContext->lineBaseRelocated = TRUE;
            WriteDebugRelocation(
                LineSect, 4, NULL, CurContext->section, 0, CurContext->lineSectionOffset
            );
        }
    }
    for (pending = list; pending; pending = pending->next) {
        if (pending->object->debugInfo.entry) {
            AppendGListWord(dwarf, AT_MW_global_ref);
            node = pending->object->debugInfo.entry;
#if VERSION >= VERSION_GC_2_0
            entrySymbol = node->u.sym.symbol;
            reloc = WriteDebugRelocation(
                DebugSect,
                dwarf->size,
                entrySymbol,
                DebugSect,
                entrySymbol ? 0 : node->u.sym.offset,
                CurContext->offset
            );
#else
            reloc = WriteDebugRelocation(
                DebugSect, dwarf->size, NULL, DebugSect, node->u.sym.offset, CurContext->offset
            );
#endif
            if (!node->u.sym.offset) {
                fixup = galloc(sizeof(DwarfFixup));
                fixup->reference = reloc;
                fixup->offset = dwarf->size;
                fixup->next = node->u.sym.fixups;
                node->u.sym.fixups = fixup;
            }
            AppendGListLong(dwarf, copts.dwarf_relocations ? 0 : node->u.sym.offset);
            size += 6;
        } else {
            CError_FATAL(1604);
            AppendGListWord(dwarf, AT_MW_global_ref_name);
            size += 2;
            size += DW_String(CMangler_GetLinkName(pending->object)->name);
        }
    }
    *(SInt32*)(*dwarf->data + offset) = size;
    CError_ASSERT(1611, ++*dwarf_depth_ptr < 50);
    dwarf_entry_offsets[*dwarf_depth_ptr] = 0;
    *dwarf_pending_ptr = FALSE;
    return offset;
}

void DWARF_EstablishContext(ObjGenSection* functionSection) {
    if (functionSection->debugState == NULL) {
        CurContext = galloc(sizeof(DwarfFunctionState));
        memset(CurContext, 0, sizeof(DwarfFunctionState));
        CurContext->depth = -1;
        CurContext->section = functionSection;
        if (InitGList(&CurContext->lines, 1000)) {
            CError_NoMem();
        }
        if (InitGList(&CurContext->info, 5000)) {
            CError_NoMem();
        }
        functionSection->debugState = CurContext;
        last_debug_record->next = galloc(sizeof(DwarfStateList));
        last_debug_record = last_debug_record->next;
        memset(last_debug_record, 0, sizeof(DwarfStateList));
        last_debug_record->state = CurContext;
        if (copts.full_dwarf) {
            newDWARF_file(functionSection);
        } else {
            SetScope(functionSection->debugState);
            dwarf_lines_header_pending = TRUE;
        }
    } else {
        SetScope(functionSection->debugState);
    }
}

int newDWARF_function(Object* object, SInt32 codeSize, SInt32 codeOffset, ObjGenSection* section) {
    DwarfNode* node;
    Type* rtype;
    DWInfo* info;
    DWInfo* typeInfo;
    SInt32* position;

#if VERSION >= VERSION_GC_2_0
    if (!copts.filesyminfo) {
        if (ELFfile.debug_sections_inited && section->debugState) {
            DWARF_EstablishContext(section);
            dwarf_function_line_symbol = DWARF_LINE_SYMBOL(
                CMangler_GetLinkName(object), object->sclass, dwarfline->size, CurContext
            );
            dwarf_function_qual = object->qual;
#if VERSION >= VERSION_GC_2_7
            AddSymbol(
                dwarf_function_line_symbol,
                dwarfline->size - dwarf_function_line_symbol->elf.st_value,
                dwarf_function_qual,
                dwarf_function_line_symbol->sclass,
                1,
                2,
                LineSect
            );
#endif
            SetScope(DefaultContext);
        }
        return FALSE;
    }
#endif
    DWARF_EstablishContext(section);
    setup_return_operand(object);
    node = NewDwarfNode();
    if (++dwarf_block_depth == 0) {
        func_link = node;
    }
    node->kind = 0x14;
    node->u.block.object = object;
    node->u.block.codeSize = codeSize;
    node->u.block.codeOffset = codeOffset;
    node->u.block.pendingObjects = pending_objects;
    pending_objects = NULL;
    node->u.block.returnOperand = galloc(sizeof(DwarfLocationOperand));
    *node->u.block.returnOperand = ReturnAddress;
    if (copts.cplusplus && (TYPE_FUNC(object->type)->flags & FUNC_METHOD)) {
        info = find_or_create_dwinfo(TYPE(TYPE_METHOD(object->type)->theclass));
        if (!info->typeNode) {
            create_type_node(info);
        }
    }
    if ((rtype = TYPE_FUNC(object->type)->functype)) {
        typeInfo = find_or_create_dwinfo(rtype);
        if (!typeInfo->typeNode) {
            create_type_node(typeInfo);
        }
    }
    if (!copts.full_dwarf && dwarf_lines_header_pending) {
        dwarf_lines_header_pending = FALSE;
        AppendGListLong(dwarfline, 4);
        AppendGListLong(dwarfline, 0);
        position = (SInt32*)*dwarfline->data;
        *position = *position + 4;
    }
#if VERSION >= VERSION_GC_2_0
    dwarf_function_line_symbol = DWARF_LINE_SYMBOL(
        CMangler_GetLinkName(object), object->sclass, dwarfline->size, CurContext
    );
    dwarf_function_qual = object->qual;
    currentDwarfFunction = currentDwarfScope = node;
#else
    currentDwarfScope = node;
#endif
    return TRUE;
}

static void end_function_entries(DwarfNode** func) {
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
#endif
#if VERSION >= VERSION_GC_2_7
    UInt32 qual;

    qual = 0;
#endif
#if VERSION >= VERSION_GC_2_0
    if (*func) {
#if VERSION >= VERSION_GC_2_7
        CError_ASSERT(1718, (*func)->u.block.symbol);
#endif
#if VERSION >= VERSION_GC_2_0
        symbol = (*func)->u.block.symbol;
#endif
#if VERSION >= VERSION_GC_2_7
        qual = (*func)->u.block.qual;
#endif
    } else {
        CError_FATAL(1724);
    }
#endif
    if (copts.dwarf_siblings && *dwarf_pending_ptr) {
#if VERSION >= VERSION_GC_2_7
        if (copts.dwarf_relocations) {
            BackPatchRelocationAddend(
                dwarf_entry_relocs[*dwarf_depth_ptr],
                DWARF_SymbolOffset(symbol, dwarf->size + CurContext->offset),
                symbol
            );
        } else {
            *(SInt32*)(*dwarf->data + dwarf_entry_offsets[*dwarf_depth_ptr]) =
                dwarf->size + CurContext->offset;
        }
#else
        PATCH_SIBLING(symbol);
#endif
        AppendGListLong(dwarf, 4);
    }
    --*dwarf_depth_ptr;
#if VERSION >= VERSION_GC_2_7
    AddSymbol(
        symbol,
        dwarf->size + CurContext->offset - symbol->elf.st_value,
        qual,
        symbol->sclass,
        1,
        2,
        DebugSect
    );
#else
    if (!copts.full_dwarf) {
        SetScope(DefaultContext);
    }
#endif
}

void endDWARF_function(void) {
    DwarfNode* node;

    node = NewDwarfNode();
    node->kind = 0x4080;
#if VERSION >= VERSION_GC_2_0
    CError_ASSERT(1749, currentDwarfFunction);
    node->u.func = currentDwarfFunction;
    currentDwarfFunction = currentDwarfScope = NULL;
#else
    currentDwarfScope = NULL;
#endif
#if VERSION >= VERSION_GC_2_7
    AddSymbol(
        dwarf_function_line_symbol,
        dwarfline->size - dwarf_function_line_symbol->elf.st_value,
        dwarf_function_qual,
        dwarf_function_line_symbol->sclass,
        1,
        2,
        LineSect
    );
#endif
    SetScope(DefaultContext);
    if (--dwarf_block_depth == -1) {
        func_link = NULL;
    }
}

static void WritePadding(int amount) {
    AppendGListLong(dwarf, amount);
    amount -= 4;
    if (amount >= 2) {
        AppendGListWord(dwarf, 0);
        amount -= 2;
    }
    if (amount == 1) {
        AppendGListByte(dwarf, 0);
    }
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -165
#endif
void tweakDWARF_file(void) {
    DwarfNode* node;
    DWInfo* info;
    DwarfStateList* list;
    SInt32 position;
    SInt32 depth;
    SInt32 offset;
    int padding;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
#endif
#if VERSION >= VERSION_GC_2_7
    BE_SymNode* lineTableSymbol;
    DwarfLineSymbol* lineSymbol;
    ObjGenRelocation* reloc;
    DwarfFunctionState* state;

    symbol = NULL;
#endif

    for (currentDwarfNode = dwarf_node_head; (node = currentDwarfNode->next);
         currentDwarfNode = currentDwarfNode->next) {
        if (node->kind == 0x13 || node->kind == 0x4081) {
            info = node->type;
            if (!info->marked) {
                insert_type_nodes_recursive(NULL, info);
            }
        }
    }

    offset = 0;
    for (list = debug_records; list;) {
        SetScope(list->state);
        CurContext->offset = 0;
        CurContext->lineSectionOffset = offset;
#if VERSION >= VERSION_GC_2_7
        lineTableSymbol = DWARF_MakeLineSymbol(NULL, TK_STATIC, dwarfline->size, CurContext);
#endif
        AppendGListLong(dwarfline, 0);
        position = *(SInt32*)*dwarfline->data;
        AppendGListWord(dwarfline, -1);
        AppendGListLong(dwarfline, text->size);
        *(SInt32*)*dwarfline->data = position + 10;
#if VERSION >= VERSION_GC_2_7
        AddSymbol(
            lineTableSymbol,
            dwarfline->size - lineTableSymbol->elf.st_value,
            0,
            lineTableSymbol->sclass,
            1,
            2,
            LineSect
        );
        state = CurContext;
        for (lineSymbol = dwarf_line_symbols; lineSymbol; lineSymbol = lineSymbol->next) {
            if (lineSymbol->state == state) {
                lineSymbol->symbol->elf.st_value += state->lineSectionOffset;
            }
        }
#endif
        list = list->next;
        offset += dwarfline->size;
    }

    offset = 0;
    for (list = debug_records; list;) {
        SetScope(list->state);
        if (copts.full_dwarf) {
            CurContext->offset = offset;
        }
        if (copts.full_dwarf || list->state == DefaultContext) {
            for (currentDwarfNode = dwarf_node_head; (node = currentDwarfNode->next);) {
                if (copts.full_dwarf && node->scope != CurContext) {
                    currentDwarfNode = node;
                    continue;
                }
                info = node->type;
                switch (node->kind) {
                    case 0x11:
#if VERSION >= VERSION_GC_2_0
                        emit_compile_unit(
                            CurContext->section, node->u.unit.language, node->u.unit.symbol
                        );
#else
                        emit_compile_unit(CurContext->section, node->u.unit.language);
#endif
                        break;
                    case 0x4080:
                        end_function_entries(&node->u.func);
#if VERSION >= VERSION_GC_2_7
                        if (!copts.full_dwarf) {
                            SetScope(DefaultContext);
                        }
#endif
                        break;
                    case 0x15:
                        emit_function_type(&node->u.type.fixups, info);
                        break;
                    case 0x1f:
                        emit_member_pointer_type(&node->u.type.fixups, info);
                        break;
                    case 7:
                        emit_variable_entry(&node->u.sym, node->u.sym.object, node->type);
                        break;
                    case 5:
                        emit_parameter_entry(info, &node->u.var);
                        break;
                    case 0xc:
                        emit_local_entry(info, &node->u.var);
                        break;
                    case 6:
                    case 0x14:
                        if (!copts.full_dwarf) {
                            SetScope(node->scope);
                        }
#if VERSION >= VERSION_GC_2_0
                        emit_function_entry(&node->u.block);
#else
                        emit_function_entry(
                            node->u.block.object,
                            node->u.block.codeSize,
                            node->u.block.codeOffset,
                            node->u.block.returnOperand,
                            node->u.block.pendingObjects
                        );
#endif
                        break;
                    case 0x13:
                        if (info->marked) {
                            GET_TYPE_DWARF_REF(info, node->u.type.qual, FALSE, NULL);
                        }
                        break;
                    case 0x4081:
                    case 0xffff:
                        break;
                    default:
                        CError_FATAL(1896);
                }
                currentDwarfNode = currentDwarfNode->next;
            }
            if (copts.dwarf_siblings) {
#if VERSION >= VERSION_GC_2_7
                if ((depth = *dwarf_depth_ptr) >= 0 && dwarf_entry_relocs[depth]) {
                    symbol =
                        DWARF_MakeEntrySymbol(NULL, TK_STATIC, 0, dwarf->size + CurContext->offset);
                    if (copts.dwarf_relocations) {
                        BackPatchRelocationAddend(
                            dwarf_entry_relocs[*dwarf_depth_ptr],
                            DWARF_SymbolOffset(
                                dwarf_last_entry_symbol, dwarf->size + CurContext->offset
                            ),
                            dwarf_last_entry_symbol
                        );
                    } else {
                        *(SInt32*)(*dwarf->data + dwarf_entry_offsets[*dwarf_depth_ptr]) =
                            dwarf->size + CurContext->offset;
                    }
                    AppendGListLong(dwarf, 4);
                    AddSymbol(
                        symbol,
                        dwarf->size + CurContext->offset - symbol->elf.st_value,
                        0,
                        symbol->sclass,
                        1,
                        2,
                        DebugSect
                    );
                }
                symbol =
                    DWARF_MakeEntrySymbol(NULL, TK_STATIC, 0, dwarf->size + CurContext->offset);
                AppendGListLong(dwarf, 4);
                AddSymbol(
                    symbol,
                    dwarf->size + CurContext->offset - symbol->elf.st_value,
                    0,
                    symbol->sclass,
                    1,
                    2,
                    DebugSect
                );
#else
                if ((depth = *dwarf_depth_ptr) >= 0 && dwarf_entry_relocs[depth]) {
                    if (copts.dwarf_relocations) {
                        DWARF_BACKPATCH_PLAIN(
                            dwarf_entry_relocs[depth],
                            dwarf->size + CurContext->offset,
                            dwarf_last_entry_symbol
                        );
                    } else {
                        *(SInt32*)(*dwarf->data + dwarf_entry_offsets[depth]) =
                            dwarf->size + CurContext->offset;
                    }
                    AppendGListLong(dwarf, 4);
                }
                AppendGListLong(dwarf, 4);
#endif
            }
#if VERSION >= VERSION_GC_2_0
            else {
                CError_FATAL(1919);
            }
#endif
            --*dwarf_depth_ptr;
            padding = 4 - (dwarf->size % 4);
            if (padding < 4) {
                WritePadding(padding + 4);
            }
#if VERSION >= VERSION_GC_2_7
            symbol->elf.st_size = dwarf->size + CurContext->offset - symbol->elf.st_value;
#endif
#if VERSION >= VERSION_GC_2_0
            symbol = CurContext->pendingSymbol;
#endif
            if (copts.dwarf_relocations) {
                DWARF_BACKPATCH_PLAIN(
                    CurContext->pendingReference,
                    DWARF_SYMBOL_OFFSET(symbol, dwarf->size + CurContext->offset),
                    symbol
                );
            } else {
                *(SInt32*)(*dwarf->data + CurContext->pendingPosition) =
                    dwarf->size + CurContext->offset;
            }
        }
        list = list->next;
        offset += dwarf->size;
    }

#if VERSION >= VERSION_GC_2_7
    if (allrelocations) {
        for (reloc = allrelocations; reloc; reloc = reloc->next) {
            if (reloc->source == DebugSect && reloc->symbol == NULL) {
                CError_FATAL(1941);
            }
        }
    }
#endif

    for (list = debug_records; list; list = list->next) {
        if (list->state) {
            if (list->state->info.size) {
                TransferSectionData(&ELFfile.dwarf_info_section->buffer, &list->state->info);
                FreeGList(&list->state->info);
            }
            if (list->state->lines.size) {
                TransferSectionData(&ELFfile.dwarf_line_section->buffer, &list->state->lines);
                FreeGList(&list->state->lines);
            }
        }
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

void endDWARF_file(void) {
    return;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -190
#endif
static SInt32 emit_dwarf_ref(DWInfo* info) {
    DwarfRef* ref;
    QualNode* qual;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
#endif
    SInt32 size;
    SInt32 lengthOffset;
    SInt16 count;
#if VERSION >= VERSION_GC_2_0
    SInt32 value;
#endif
    ObjGenRelocation* reloc;
    DWInfo* typeInfo;
    DwarfFixup* fixup;

    ref = &info->rec;
    qual = ref->qualifiers;
#if VERSION >= VERSION_GC_2_0
    symbol = NULL;
#endif
    switch (*(UInt16*)ref) {
        case AT_fund_type:
            AppendGListWord(dwarf, ref->tag);
            size = 2;
            AppendGListWord(dwarf, ref->num);
            size += 2;
            break;
        case AT_mod_u_d_type:
            AppendGListWord(dwarf, ref->tag);
            size = 2;
            lengthOffset = dwarf->size;
            count = 0;
            AppendGListWord(dwarf, 0);
            size += 2;
            while (qual) {
                AppendGListByte(dwarf, qual->type);
                count++;
                size++;
                qual = qual->next;
            }
#if VERSION >= VERSION_GC_2_0
            value = ref->num;
            if (value) {
                symbol = ref->symbol;
            }
            if (symbol) {
                value = 0;
            }
            reloc = WriteDebugRelocation(
                DebugSect, dwarf->size, symbol, DebugSect, value, CurContext->offset
            );
#else
            reloc = WriteDebugRelocation(
                DebugSect, dwarf->size, NULL, DebugSect, ref->num, CurContext->offset
            );
#endif
            typeInfo = ref->typeInfo;
            if (!typeInfo->rec.valid) {
                CError_ASSERT(2004, typeInfo->typeNode);
                fixup = galloc(sizeof(DwarfFixup));
                fixup->reference = reloc;
                fixup->offset = dwarf->size;
                fixup->next = typeInfo->typeNode->u.type.fixups;
                typeInfo->typeNode->u.type.fixups = fixup;
            }
            AppendGListLong(dwarf, copts.dwarf_relocations ? 0 : ref->num);
            size += 4;
            *(UInt16*)(*dwarf->data + lengthOffset) = count + 4;
            break;
        case AT_mod_fund_type:
            AppendGListWord(dwarf, ref->tag);
            size = 2;
            lengthOffset = dwarf->size;
            count = 0;
            AppendGListWord(dwarf, 0);
            size += 2;
            while (qual) {
                AppendGListByte(dwarf, qual->type);
                count++;
                size++;
                qual = qual->next;
            }
            AppendGListWord(dwarf, ref->num);
            size += 2;
            *(UInt16*)(*dwarf->data + lengthOffset) = count + 2;
            break;
        case AT_user_def_type:
        case AT_containing_type:
            AppendGListWord(dwarf, ref->tag);
            size = 2;
#if VERSION >= VERSION_GC_2_0
            value = ref->num;
            if (value) {
                symbol = ref->symbol;
            }
            if (symbol) {
                value = 0;
            }
            reloc = WriteDebugRelocation(
                DebugSect, dwarf->size, symbol, DebugSect, value, CurContext->offset
            );
#else
            reloc = WriteDebugRelocation(
                DebugSect, dwarf->size, NULL, DebugSect, ref->num, CurContext->offset
            );
#endif
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -192
#endif
            if (!ref->valid) {
                CError_ASSERT(2036, info->typeNode);
                fixup = galloc(sizeof(DwarfFixup));
                fixup->reference = reloc;
                fixup->offset = dwarf->size;
                fixup->next = info->typeNode->u.type.fixups;
                info->typeNode->u.type.fixups = fixup;
            }
            AppendGListLong(dwarf, copts.dwarf_relocations ? 0 : ref->num);
            size += 4;
            break;
        default:
            CError_FATAL(2046);
    }
    return size;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif
static void emit_local_entry(DWInfo* info, DwarfVar* var) {
    Type* type;
    SInt32 stackOffset;
    UInt16 reg;
    UInt16 reg2;
    HashNameNode* name;
    SInt32 offset;
    SInt32 size;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
#endif
    DwarfLocationOperand location;
    DwarfLocationOperand pair;

    type = info->type;
    stackOffset = var->offset;
    reg = var->reg;
    reg2 = var->reg2;
    name = var->name;
    offset = dwarf->size;
    GET_TYPE_DWARF_REF(info, var->qual, TRUE, NULL);
#if VERSION >= VERSION_GC_2_0
    if (var->func) {
#if VERSION >= VERSION_GC_2_7
        CError_ASSERT(2068, var->func->u.block.symbol);
#endif
        symbol = var->func->u.block.symbol;
    } else {
        CError_FATAL(2073);
    }
#endif
    size = EMIT_ENTRY_HEADER(TAG_local_variable, symbol);
    AppendGListWord(dwarf, AT_name);
    size += 2;
    size += DW_String(name->name);
    pair.kind = 0;
    if (reg || stackOffset == -1) {
        if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
            if (TYPE_IS_SOFT_FLOAT(type)) {
                if (TYPE_IS_SOFT_FLOAT(type) && type->size != 4) {
                    location.kind = 1;
                    location.operand.registers.first = reg2;
                    location.operand.registers.offset = 0;
                    pair.kind = 8;
                    pair.operand.registers.first = reg;
                    pair.operand.registers.second = reg2;
                } else {
                    location.kind = 1;
                    location.operand.registers.first = reg;
                    location.operand.registers.offset = 0;
                }
            } else {
                location.kind = 2;
                location.operand.registers.first = reg;
            }
        } else if (IS_TYPE_VECTOR(type)) {
            location.kind = 4;
            location.operand.registers.first = reg;
        } else if (IS_TYPE_VEC2X32FLOAT(type)) {
            location.kind = 12;
            location.operand.registers.first = reg;
        } else if (TYPE_IS_8BYTES(type)) {
            location.kind = 1;
            location.operand.registers.first = reg2;
            location.operand.registers.offset = 0;
            pair.kind = 8;
            pair.operand.registers.first = reg;
            pair.operand.registers.second = reg2;
        } else {
            location.kind = 1;
            location.operand.registers.first = reg;
            location.operand.registers.offset = 0;
        }
    } else {
        location.kind = 5;
        location.operand.address.offset = stackOffset;
        location.operand.address.reg = var->base;
    }
    size += emit_dwarf_ref(info);
    if (pair.kind == 8) {
        size += emit_location_attribute(&pair, AT_MW_register_pair, FALSE);
    }
    size += emit_location_attribute(&location, AT_location, FALSE);
    *(SInt32*)(*dwarf->data + offset) = size;
}

void newDWARF_local(Object* object, SInt32 unused) {
    VarInfo* vi;
    DWInfo* info;
    DwarfNode* node;

    vi = object->u.var.info;
    if (object->datatype != DLOCAL) {
        return;
    }
    info = find_or_create_dwinfo(object->type);
    if (!info->typeNode) {
        create_type_node(info);
    }
    node = NewDwarfNode();
    node->kind = 0xc;
    node->type = info;
    node->u.var.qual = object->qual;
    node->u.var.reg = vi->reg;
#if VERSION >= VERSION_GC_2_0
    node->u.var.func = currentDwarfFunction;
#endif
    if (vi->flags & VarInfoFlag_InReg) {
        node->u.var.offset = -1;
        if ((TYPE_IS_SOFT_FLOAT(object->type) && object->type->size != 4) ||
            TYPE_IS_8BYTES(object->type)) {
            node->u.var.reg2 = vi->regHi;
        }
    } else {
        node->u.var.offset = local_offset_32(object);
        node->u.var.base = local_base_register(object);
    }
    node->u.var.name = object->name;
}

static void emit_parameter_entry(DWInfo* info, DwarfVar* var) {
    Type* type;
    SInt32 stackOffset;
    UInt16 reg;
    UInt16 reg2;
    HashNameNode* name;
    SInt32 offset;
    SInt32 size;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
#endif
    DwarfLocationOperand location;
    DwarfLocationOperand pair;

    type = info->type;
    stackOffset = var->offset;
    reg = var->reg;
    reg2 = var->reg2;
    name = var->name;
#if VERSION >= VERSION_GC_2_0
    symbol = NULL;
#endif
    offset = dwarf->size;
    GET_TYPE_DWARF_REF(info, var->qual, TRUE, NULL);
#if VERSION >= VERSION_GC_2_0
    if (var->func) {
#if VERSION >= VERSION_GC_2_7
        CError_ASSERT(2194, var->func->u.block.symbol);
#endif
        symbol = var->func->u.block.symbol;
    } else {
        CError_FATAL(2199);
    }
#endif
    size = EMIT_ENTRY_HEADER(TAG_formal_parameter, symbol);
    AppendGListWord(dwarf, AT_name);
    size += 2;
    size += DW_String(name->name);
    pair.kind = 0;
    if (reg || stackOffset == -1) {
        if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) {
            if (TYPE_IS_SOFT_FLOAT(type)) {
                if (TYPE_IS_SOFT_FLOAT(type) && type->size != 4) {
                    location.kind = 1;
                    location.operand.registers.first = reg2;
                    location.operand.registers.offset = 0;
                    pair.kind = 8;
                    pair.operand.registers.first = reg;
                    pair.operand.registers.second = reg2;
                } else {
                    location.kind = 1;
                    location.operand.registers.first = reg;
                    location.operand.registers.offset = 0;
                }
            } else {
                location.kind = 2;
                location.operand.registers.first = reg;
            }
        } else if (IS_TYPE_VECTOR(type)) {
            location.kind = 4;
            location.operand.registers.first = reg;
        } else if (IS_TYPE_VEC2X32FLOAT(type)) {
            location.kind = 12;
            location.operand.registers.first = reg;
        } else if (TYPE_IS_8BYTES(type)) {
            location.kind = 1;
            location.operand.registers.first = reg2;
            location.operand.registers.offset = 0;
            pair.kind = 8;
            pair.operand.registers.first = reg;
            pair.operand.registers.second = reg2;
        } else {
            location.kind = 1;
            location.operand.registers.first = reg;
            location.operand.registers.offset = 0;
        }
    } else {
        location.kind = 5;
        location.operand.address.offset = stackOffset;
        location.operand.address.reg = var->base;
    }
    size += emit_dwarf_ref(info);
    if (pair.kind == 8) {
        size += emit_location_attribute(&pair, AT_MW_register_pair, FALSE);
    }
    size += emit_location_attribute(&location, AT_location, FALSE);
    *(SInt32*)(*dwarf->data + offset) = size;
}

void newDWARF_param(Object* object, SInt32 unused) {
    DwarfNode* node;
    VarInfo* vi;
    DWInfo* info;

    vi = object->u.var.info;
    info = find_or_create_dwinfo(object->type);
    if (!info->typeNode) {
        create_type_node(info);
    }
    if (currentDwarfScope) {
        node = InsertDwarfNode();
    } else {
        node = NewDwarfNode();
    }
    node->kind = 5;
    node->type = info;
    node->u.var.qual = object->qual;
#if VERSION >= VERSION_GC_2_0
    node->u.var.func = currentDwarfFunction;
#endif
    node->u.var.reg = vi->reg;
    if (vi->flags & VarInfoFlag_InReg) {
        node->u.var.offset = -1;
        if ((TYPE_IS_SOFT_FLOAT(object->type) && object->type->size != 4) ||
            TYPE_IS_8BYTES(object->type)) {
            node->u.var.reg2 = vi->regHi;
        }
    } else {
        node->u.var.offset = local_offset_32(object);
        node->u.var.base = local_base_register(object);
    }
    node->u.var.name = object->name;
}

#if VERSION >= VERSION_GC_2_7
static BE_SymNode* DWARF_MakeLineSymbol(
    HashNameNode* name, int sclass, SInt32 offset, DwarfFunctionState* state
) {
    BE_SymNode* symbol;
    DwarfLineSymbol* entry;
    HashNameNode* symbolName;
    char uniqueName[16];

    name_mangle_list.size = 0;
    AppendGListName(&name_mangle_list, ".line.");
    if (name) {
        AppendGListID(&name_mangle_list, name->name);
    } else {
        uniqueName[0] = '.';
        CParser_PrintUniqueID(uniqueName + 1);
        AppendGListID(&name_mangle_list, uniqueName);
        sclass = TK_STATIC;
    }
    COS_LockHandle(name_mangle_list.data);
    symbolName = GetHashNameNodeExport(*name_mangle_list.data);
    COS_UnlockHandle(name_mangle_list.data);
    symbol = MakeNamedSymbolTableEntry(symbolName, sclass);
    symbol->elf.st_value = offset;
    entry = galloc(sizeof(DwarfLineSymbol));
    memclrw(entry, sizeof(DwarfLineSymbol));
    entry->state = state;
    entry->symbol = symbol;
    entry->next = dwarf_line_symbols;
    dwarf_line_symbols = entry;
    return symbol;
}

static BE_SymNode* DWARF_MakeEntrySymbol(HashNameNode* name, int sclass, int tag, SInt32 offset) {
    BE_SymNode* symbol;
    char tagName[64];
    char uniqueName[16];

    name_mangle_list.size = 0;
    sprintf(tagName, "%04x.", tag);
    AppendGListName(&name_mangle_list, ".dwarf.");
    AppendGListName(&name_mangle_list, tagName);
    if (name) {
        AppendGListID(&name_mangle_list, name->name);
    } else {
        uniqueName[0] = '.';
        CParser_PrintUniqueID(uniqueName + 1);
        AppendGListID(&name_mangle_list, uniqueName);
        sclass = TK_STATIC;
    }
    COS_LockHandle(name_mangle_list.data);
    name = GetHashNameNodeExport(*name_mangle_list.data);
    COS_UnlockHandle(name_mangle_list.data);
    symbol = MakeNamedSymbolTableEntry(name, sclass);
    symbol->elf.st_value = offset;
    return symbol;
}

static SInt32 DWARF_SymbolOffset(BE_SymNode* symbol, SInt32 offset) {
    CError_ASSERT(2423, symbol);
    return offset - symbol->elf.st_value;
}
#endif

static char* DWARF_LocalName(char* name, char* buffer) {
    char* p = buffer;
    SInt32 i;

    for (i = 0; *name && i < 240 && *name != '$'; i++) {
        *p++ = *name++;
    }
    *p = 0;
    return buffer;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -336
#endif
static void emit_variable_entry(DwarfSym* sym, Object* object, DWInfo* info) {
    UInt32 offset;
    ObjRegInfo* reginfo;
    UInt16 tag;
    DwarfFixup* fixup;
    SInt32 size;
#if VERSION >= VERSION_GC_2_0
    BE_SymNode* symbol;
#endif
    char* linkname;
    DwarfLocationOperand location;
    char buffer[256];
#if VERSION >= VERSION_GC_2_7
    UInt32 qual;
#endif

    reginfo = Registers_GetRegInfo(object);
    offset = dwarf->size;
#if VERSION < VERSION_GC_2_0
    for (fixup = sym->fixups; fixup; fixup = fixup->next) {
        if (copts.dwarf_relocations) {
            DWARF_BACKPATCH(fixup->reference, offset, symbol);
        } else {
            *(SInt32*)(*dwarf->data + fixup->offset) = offset;
        }
    }
    sym->fixups = fixup;
#endif
    sym->offset = offset;
    GET_TYPE_DWARF_REF(info, object->qual, TRUE, NULL);
#if VERSION >= VERSION_GC_2_7
    if (CParser_HasInternalLinkage(object)) {
        tag = TAG_local_variable;
        qual = 0;
    } else {
        tag = TAG_global_variable;
        qual = Q_IMPLICIT_WEAK;
    }
    sym->symbol = symbol = DWARF_MakeEntrySymbol(
        object->u.data.linkname, object->sclass, tag, (SInt32)offset + CurContext->offset
    );
#elif VERSION >= VERSION_GC_2_0
    if (CParser_HasInternalLinkage(object)) {
        tag = TAG_local_variable;
        symbol = NULL;
    } else {
        tag = TAG_global_variable;
        symbol = NULL;
    }
    sym->symbol = symbol;
#else
    if (CParser_HasInternalLinkage(object)) {
        tag = TAG_local_variable;
    } else {
        tag = TAG_global_variable;
    }
#endif
#if VERSION >= VERSION_GC_2_0
    for (fixup = sym->fixups; fixup; fixup = fixup->next) {
        if (copts.dwarf_relocations) {
            DWARF_BACKPATCH(fixup->reference, offset, symbol);
        } else {
            *(SInt32*)(*dwarf->data + fixup->offset) = offset;
        }
    }
    sym->fixups = fixup;
#endif
    size = EMIT_ENTRY_HEADER(tag, symbol);
    AppendGListWord(dwarf, AT_name);
    size += 2;
    linkname = CMangler_GetLinkName(object)->name;
    if (CParser_HasInternalLinkage(object)) {
        size += DW_String(DWARF_LocalName(object->name->name, buffer));
        if (copts.cplusplus && object->name->name != linkname) {
            AppendGListWord(dwarf, AT_MW_linkage_name);
            size += 2;
            size += DW_String(DWARF_LocalName(linkname, buffer));
        }
    } else {
        size += DW_String(object->name->name);
        if (copts.cplusplus && object->name->name != linkname) {
            AppendGListWord(dwarf, AT_MW_linkage_name);
            size += 2;
            size += DW_String(linkname);
        }
    }
    if (reginfo && reginfo->reg && !copts.ignore_global_reg_vars) {
        switch (reginfo->rclass) {
            case RegClass_VR:
                location.kind = 4;
                location.operand.registers.first = reginfo->reg;
                break;
            case RegClass_GPR:
                location.kind = 1;
                location.operand.registers.offset = 0;
                location.operand.registers.first = reginfo->reg;
                break;
            case RegClass_FPR:
                location.kind = 2;
                location.operand.registers.first = reginfo->reg;
                break;
            default:
                CError_FATAL(2503);
        }
    } else {
        location.kind = 7;
        location.operand.address.offset = 0;
        location.operand.address.object = object;
    }
    size += emit_dwarf_ref(info);
    size += emit_location_attribute(&location, AT_location, FALSE);
    *(SInt32*)(*dwarf->data + offset) = size;
#if VERSION >= VERSION_GC_2_7
    AddSymbol(
        symbol,
        dwarf->size + CurContext->offset - symbol->elf.st_value,
        qual,
        symbol->sclass,
        1,
        2,
        DebugSect
    );
#endif
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -338
#endif
void DWARF_DeclareData(Object* object) {
    DWInfo* info;
    DwarfNode* node;

    if (!copts.filesyminfo) {
        return;
    }
    if (object->name->name[0] == '@') {
        return;
    }
    if (object->debugInfo.entry) {
        return;
    }
    info = find_or_create_dwinfo(object->type);
    if (!info->typeNode) {
        create_type_node(info);
    }
    node = NewDwarfNode();
    object->debugInfo.entry = node;
    if (object->toc) {
        CError_FATAL(2545);
        object->toc->debugInfo.entry = node;
    }
    node->kind = 7;
    node->type = info;
    node->u.sym.object = object;
    node->u.sym.fixups = NULL;
    node->u.sym.offset = 0;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -338
#endif
static DwarfNode* NewDwarfNodeBefore(DwarfNode* next) {
    DwarfNode* node;

    CError_ASSERT(2614, next);
    node = galloc(sizeof(DwarfNode));
    memclrw(node, sizeof(DwarfNode));
    node->next = next;
    node->prev = next->prev;
    CError_ASSERT(2618, node->prev);
    next->prev = node;
    node->prev->next = node;
    node->scope = DefaultContext;
    return node;
}

static void insert_type_node_before(DWInfo* before, DWInfo* info) {
    DwarfNode* node;
    UInt16 kind;
    Type* type;

    type = info->type;
    CError_ASSERT(2632, before);
    switch (type->type) {
        case TYPEENUM:
        case TYPESTRUCT:
        case TYPECLASS:
        case TYPEMEMBERPOINTER:
        case TYPEARRAY:
            kind = 0x13;
            break;
        case TYPEFUNC:
            kind = 0xffff;
            break;
        case TYPEPOINTER:
            kind = 0x4081;
            break;
        case TYPETEMPLATE:
            CError_FATAL(2649);
            break;
        default:
            return;
    }
    node = NewDwarfNodeBefore(before->typeNode);
    node->kind = kind;
    node->type = info;
    node->type->typeNode = node;
}

static void create_type_node(DWInfo* info) {
    DwarfNode* node;
    UInt16 kind;

    kind = 0;
    switch (info->type->type) {
        case TYPEENUM:
        case TYPESTRUCT:
        case TYPECLASS:
        case TYPEMEMBERPOINTER:
        case TYPEARRAY:
            kind = 0x13;
            break;
        case TYPEFUNC:
            kind = 0xffff;
            break;
        case TYPEPOINTER:
            kind = 0x4081;
            break;
        case TYPETEMPLATE:
            CError_FATAL(2683);
            break;
        default:
            return;
    }
    if (func_link == NULL) {
        node = NewDwarfNode();
        node->scope = DefaultContext;
    } else {
        node = NewDwarfNodeBefore(func_link);
    }
    node->kind = kind;
    node->type = info;
    node->type->typeNode = node;
}

static void insert_type_nodes_recursive(DWInfo* parent, DWInfo* info) {
    Type* t = info->type;

    info->marked = TRUE;
    switch (t->type) {
        case TYPETEMPLATE:
            CError_FATAL(2719);
            break;

        case TYPEBITFIELD: {
            DWInfo* e;
            if (info->typeNode == NULL) {
                insert_type_node_before(parent, info);
            }
            e = find_or_create_dwinfo(TYPE_BITFIELD(t)->bitfieldtype);
            if (!e->marked) {
                insert_type_nodes_recursive(parent, e);
            }
            break;
        }

        case TYPEENUM:
            if (info->typeNode == NULL) {
                insert_type_node_before(parent, info);
            }
            break;

        case TYPESTRUCT: {
            StructMember* m;
            if (info->typeNode == NULL) {
                insert_type_node_before(parent, info);
            }
            for (m = TYPE_STRUCT(t)->members; m != NULL; m = m->next) {
                DWInfo* e = find_or_create_dwinfo(m->type);
                if (!e->marked) {
                    insert_type_nodes_recursive(info, e);
                }
            }
            break;
        }

        case TYPEPOINTER: {
            DWInfo* e;
            if (info->typeNode == NULL) {
                insert_type_node_before(parent, info);
            }
            e = find_or_create_dwinfo(TPTR_TARGET(t));
            if (!e->marked) {
                insert_type_nodes_recursive(info, e);
            }
            break;
        }

        case TYPEARRAY: {
            DWInfo* e;
            if (info->typeNode == NULL) {
                insert_type_node_before(parent, info);
            }
            e = find_or_create_dwinfo(TPTR_TARGET(t));
            if (!e->marked) {
                insert_type_nodes_recursive(info, e);
            }
            break;
        }

        case TYPECLASS: {
            CScopeObjectIterator save;
            ObjMemberVar* iv;
            ClassList* cb;
            VClassList* vb;

            if (info->typeNode == NULL) {
                insert_type_node_before(parent, info);
            }

            if (TYPE_CLASS(t)->vbases != NULL) {
                for (vb = TYPE_CLASS(t)->vbases; vb != NULL; vb = vb->next) {
                    DWInfo* e = find_or_create_dwinfo(TYPE(vb->base));
                    if (!e->marked) {
                        insert_type_nodes_recursive(info, e);
                    }
                }
            }
            if (TYPE_CLASS(t)->bases != NULL) {
                for (cb = TYPE_CLASS(t)->bases; cb != NULL; cb = cb->next) {
                    DWInfo* e = find_or_create_dwinfo(TYPE(cb->base));
                    if (!e->marked) {
                        insert_type_nodes_recursive(info, e);
                    }
                }
            }
            for (iv = TYPE_CLASS(t)->ivars; iv != NULL; iv = iv->next) {
                DWInfo* e = find_or_create_dwinfo(iv->type);
                if (!e->marked) {
                    insert_type_nodes_recursive(info, e);
                }
            }

            CScope_InitObjectIterator(&save, TYPE_CLASS(t)->nspace);
            for (;;) {
                Object* obj = OBJECT(CScope_NextObjectIteratorObject(&save));
                DWInfo* e;

                if (obj == NULL) {
                    break;
                }
                if (obj->type == NULL) {
                    continue;
                }
                if (obj->datatype == DALIAS) {
                    continue;
                }
                if (IS_TYPE_FUNC(obj->type) && (TYPE_FUNC(obj->type)->flags & FUNC_IS_TEMPL)) {
                    if (obj->u.func.u.templ->instances == NULL) {
                        continue;
                    }
                    e = find_or_create_dwinfo(obj->u.func.u.templ->instances->object->type);
                } else {
                    e = find_or_create_dwinfo(obj->type);
                }
                if (!e->marked) {
                    insert_type_nodes_recursive(info, e);
                }
            }
            break;
        }

        case TYPEMEMBERPOINTER:
            if (info->typeNode == NULL) {
                insert_type_node_before(parent, info);
            }
            info->typeNode->kind = 0x1f;
            if (TYPE_MEMBER_POINTER(t)->ty1 != NULL) {
                DWInfo* e = find_or_create_dwinfo(TYPE_MEMBER_POINTER(t)->ty1);
                if (!e->marked) {
                    insert_type_nodes_recursive(info, e);
                }
            }
            if (TYPE_MEMBER_POINTER(t)->ty2 != NULL) {
                DWInfo* e = find_or_create_dwinfo(TYPE_MEMBER_POINTER(t)->ty2);
                if (!e->marked) {
                    insert_type_nodes_recursive(info, e);
                }
            }
            break;

        case TYPEFUNC: {
            FuncArg* arg;
            if (info->typeNode == NULL) {
                insert_type_node_before(parent, info);
            }
            info->typeNode->kind = 0x15;
            if (TYPE_FUNC(t)->functype != NULL) {
                DWInfo* e = find_or_create_dwinfo(TYPE_FUNC(t)->functype);
                if (!e->marked) {
                    insert_type_nodes_recursive(info, e);
                }
            }
            for (arg = TYPE_FUNC(t)->args; arg != NULL; arg = arg->next) {
                if (arg->type != NULL) {
                    DWInfo* e = find_or_create_dwinfo(arg->type);
                    if (!e->marked) {
                        insert_type_nodes_recursive(info, e);
                    }
                }
            }
            break;
        }

        case TYPELABEL:
        default:
            break;
    }
}

static void init_dwarf_state(void) {
    last_debug_record = debug_records = galloc(sizeof(DwarfStateList));
    memset(debug_records, 0, sizeof(DwarfStateList));
    CurContext = DefaultContext = galloc(sizeof(DwarfFunctionState));
    memset(CurContext, 0, sizeof(DwarfFunctionState));
    debug_records->state = CurContext;
    if (copts.full_dwarf) {
        dwarf_depth_ptr = &CurContext->depth;
        dwarf_entry_offsets = CurContext->offsets;
        dwarf_entry_relocs = CurContext->entries;
        dwarf_pending_ptr = &CurContext->pending;
    } else {
        dwarf_depth_ptr = &dwarf_file_depth;
        dwarf_entry_offsets = dwarf_file_entry_offsets;
        dwarf_entry_relocs = dwarf_file_entry_relocs;
        dwarf_pending_ptr = &dwarf_file_pending;
    }
    *dwarf_depth_ptr = -1;
    *dwarf_pending_ptr = FALSE;
    memset(dwinfo_buckets, 0, sizeof(dwinfo_buckets));
    pending_objects = NULL;
#if VERSION >= VERSION_GC_2_0
    dwarf_node_head = currentDwarfNode = currentDwarfScope = currentDwarfFunction = NULL;
    dwarf_block_depth = -1;
    func_link = NULL;
#if VERSION >= VERSION_GC_2_7
    dwarf_line_symbols = NULL;
#endif
    dwarf_last_entry_symbol = dwarf_function_line_symbol = NULL;
    dwarf = dwarfline = text = NULL;
    dwarf_function_qual = 0;
#else
    dwarf_node_head = currentDwarfNode = currentDwarfScope = NULL;
    dwarf_block_depth = -1;
    func_link = NULL;
#endif
}

void InitDwarf(void) {
    copts.full_dwarf = TRUE;
    dwarf_lines_header_pending = FALSE;
    init_dwarf_state();
    dwarf_node_head = galloc(sizeof(DwarfNode));
    memclrw(dwarf_node_head, sizeof(DwarfNode));
    currentDwarfNode = dwarf_node_head;
    dwarf_node_head->kind = 0xffff;
}

void InitDebugSections(ObjGenSection* infoSection, ObjGenSection* lineSection) {
    DebugSect = infoSection;
    if (!copts.full_dwarf) {
        dwarf = ObjGen_GetSectionGList(infoSection);
    }
    LineSect = lineSection;
}

static void setup_return_operand(Object* func) {
    Type* rtype = TYPE_FUNC(func->type)->functype;

    ReturnAddress.kind = 0;
    if (IS_TYPE_VOID(rtype)) {
        ReturnAddress.kind = 0;
    } else if (TYPE_FITS_IN_REGISTER(rtype)) {
        if (TYPE_IS_8BYTES(rtype)) {
            ReturnAddress.kind = 8;
            ReturnAddress.metadata.type = rtype;
            ReturnAddress.operand.registers.first = low_reg;
            ReturnAddress.operand.registers.second = high_reg;
        } else {
            ReturnAddress.kind = 1;
            ReturnAddress.operand.registers.offset = 0;
            ReturnAddress.operand.registers.first = 3;
            ReturnAddress.metadata.type = rtype;
        }
    } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(rtype)) {
        if (TYPE_IS_SOFT_FLOAT(rtype) && rtype->size == 4) {
            ReturnAddress.kind = 1;
            ReturnAddress.operand.registers.offset = 0;
            ReturnAddress.operand.registers.first = 3;
            ReturnAddress.metadata.type = rtype;
        } else if (TYPE_IS_SOFT_FLOAT(rtype) && rtype->size != 4) {
            ReturnAddress.kind = 8;
            ReturnAddress.metadata.type = rtype;
            ReturnAddress.operand.registers.first = low_reg;
            ReturnAddress.operand.registers.second = high_reg;
        } else {
            ReturnAddress.kind = 2;
            ReturnAddress.operand.registers.first = 1;
            ReturnAddress.metadata.type = rtype;
        }
    } else if (IS_TYPE_VEC2X32FLOAT(rtype)) {
        ReturnAddress.kind = 12;
        ReturnAddress.operand.registers.first = 1;
        ReturnAddress.metadata.type = rtype;
    } else if ((IS_TYPE_STRUCT(rtype) || IS_TYPE_CLASS(rtype)) &&
               !Type_RequiresMemoryReturn(rtype)) {
        ReturnAddress.kind = 8;
        ReturnAddress.metadata.type = rtype;
        ReturnAddress.operand.registers.first = low_reg;
        ReturnAddress.operand.registers.second = high_reg;
    } else {
        ReturnAddress.kind = 10;
        ReturnAddress.metadata.type = rtype;
        ReturnAddress.operand.registers.first = 3;
    }
}

void DWARF1_AddToFunctionRefList(Object* object) {
    PendingObject* previous;
    PendingObject* entry;

    if (!copts.filesyminfo) {
        return;
    }
    if (object->name->name[0] == '@') {
        return;
    }
    if (object->datatype != DDATA) {
        return;
    }

    entry = pending_objects;
    previous = entry;
    if (entry) {
        do {
            if (entry->object == object) {
                if (previous != entry) {
                    previous->next = entry->next;
                    entry->next = pending_objects;
                    pending_objects = entry;
                }
                return;
            }
            previous = entry;
            entry = entry->next;
        } while (entry);
    }
    previous = galloc(sizeof(PendingObject));
    previous->object = object;
    previous->next = pending_objects;
    pending_objects = previous;
    if (!previous->object->debugInfo.entry) {
        DWARF_DeclareData(previous->object);
    }
}
