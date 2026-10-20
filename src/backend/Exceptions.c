#include "compiler/Exceptions.h"
#include "compiler/CError.h"
#include "compiler/CException.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/StackFrame.h"

typedef struct EANode {
    struct EANode* dagListNext;
    struct EANode* prev;
    ExceptionAction* action;
    UInt16 count;
    UInt16 actionOffset;
} EANode;

typedef struct PCAction {
    struct PCAction* next;
    struct PCAction* prev;
    PCode* firstInstr;
    PCode* lastInstr;
    ExceptionAction* actions;
    EANode* node;
} PCAction;

static PCAction* pc_actions;
static PCAction* last_pc_action;
EANode* DAG[EAT_NACTIONS];
static GList exceptmodule;
static OLinkList* except_refs;
static OLinkList* last_except_ref;

static EANode* makeEAnode(ExceptionAction* ea) {
    EANode* prev;
    EANode* node;

    for (node = DAG[ea->type]; node; node = node->dagListNext) {
        if (node->action == ea) {
            return node;
        }
    }

    if (ea->prev) {
        prev = makeEAnode(ea->prev);
    } else {
        prev = NULL;
    }

    for (node = DAG[ea->type]; node; node = node->dagListNext) {
        if (node->prev == prev && CExcept_ActionCompare(node->action, ea)) {
            return node;
        }
    }

    node = lalloc(sizeof(EANode));
    node->prev = prev;
    node->action = ea;
    node->count = 0;
    node->actionOffset = 0;

    node->dagListNext = DAG[ea->type];
    DAG[ea->type] = node;

    if (prev) {
        prev->count++;
    }
    return node;
}

static void addrelocation(Object* obj, SInt32 offset) {
    OLinkList* ref;

    ref = lalloc(sizeof(OLinkList));
    ref->next = NULL;
    ref->obj = obj;
    ref->offset = offset;
    ref->addend = 0;
    if (except_refs) {
        last_except_ref->next = ref;
    } else {
        except_refs = ref;
    }
    last_except_ref = ref;
}

typedef struct AABC {
    UInt8 a;
    UInt8 b;
    UInt16 c;
    UInt32 d;
} AABC;

typedef struct AACC {
    UInt8 a;
    UInt8 b;
    UInt32 c;
    UInt32 d;
} AACC;

typedef struct AABBC {
    UInt8 a;
    UInt8 b;
    UInt16 c;
    UInt16 d;
    UInt32 e;
} AABBC;

typedef struct AABCC {
    UInt8 a;
    UInt8 b;
    UInt16 c;
    UInt32 d;
    UInt32 e;
} AABCC;

typedef struct AACCC {
    UInt8 a;
    UInt8 b;
    UInt32 c;
    UInt32 d;
    UInt32 e;
} AACCC;

typedef struct AABBBC {
    UInt8 a;
    UInt8 b;
    UInt16 c;
    UInt16 d;
    UInt16 e;
    UInt32 f;
} AABBBC;

typedef struct AABBCC {
    UInt8 a;
    UInt8 b;
    UInt16 c;
    UInt16 d;
    UInt32 e;
    UInt32 f;
} AABBCC;

typedef struct AACCCC {
    UInt8 a;
    UInt8 b;
    UInt32 c;
    UInt32 d;
    UInt32 e;
    UInt32 f;
} AACCCC;

typedef struct AABCCCC {
    UInt8 a;
    UInt8 b;
    UInt16 c;
    UInt32 d;
    UInt32 e;
    UInt32 f;
    UInt32 g;
} AABCCCC;

typedef struct AACCCCC {
    UInt8 a;
    UInt8 b;
    UInt32 c;
    UInt32 d;
    UInt32 e;
    UInt32 f;
    UInt32 g;
} AACCCCC;

typedef struct AAB {
    UInt8 a;
    UInt8 b;
    UInt16 c;
} AAB;

typedef struct AAC {
    UInt8 a;
    UInt8 b;
    UInt32 c;
} AAC;

typedef struct AA {
    UInt8 a;
    UInt8 b;
} AA;

static void allocateactioninfo(EANode* node) {
    ExceptionAction* ea;
    SInt32 offset;
    UInt32 lastFlag;
    int reg;
    int reg2;

    while (node && (node->actionOffset == 0 || node->prev == NULL)) {
        offset = exceptmodule.size;
        if (node->actionOffset == 0) {
            node->actionOffset = offset;
        }

        lastFlag = node->prev ? 0 : 0x80;

        ea = node->action;

        switch (ea->type) {
            case EAT_NOP:
                CError_FATAL(146);
                break;
            case EAT_DESTROYLOCAL: {
                if (local_is_16bit_offset(ea->data.destroy_local.local)) {
                    AABC e;
                    e.a = lastFlag | 2;
                    e.b = 0;
                    e.c = CTool_EndianConvertWord16(local_offset_16(ea->data.destroy_local.local));
                    e.d = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_local.dtor, offset + 4);
                } else {
                    AACC e;
                    e.a = lastFlag | 0x11;
                    e.b = 0;
                    e.c = CTool_EndianConvertWord32(local_offset_32(ea->data.destroy_local.local));
                    e.d = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_local.dtor, offset + 8);
                }
                break;
            }
            case EAT_DESTROYLOCALCOND: {
                reg = OBJECT_REG(ea->data.destroy_local_cond.cond);
                if ((reg || local_is_16bit_offset(ea->data.destroy_local_cond.cond)) &&
                    local_is_16bit_offset(ea->data.destroy_local_cond.local)) {
                    AABBC e;
                    e.a = lastFlag | 3;
                    e.b = (reg ? 1 : 0) << 7;
                    e.c = CTool_EndianConvertWord16(
                        reg ? reg : local_offset_16(ea->data.destroy_local_cond.cond)
                    );
                    e.d = CTool_EndianConvertWord16(
                        local_offset_16(ea->data.destroy_local_cond.local)
                    );
                    e.e = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_local_cond.dtor, offset + 8);
                } else {
                    AACCC e;
                    e.a = lastFlag | 0x12;
                    e.b = (reg ? 1 : 0) << 7;
                    e.c = CTool_EndianConvertWord32(
                        reg ? reg : local_offset_32(ea->data.destroy_local_cond.cond)
                    );
                    e.d = CTool_EndianConvertWord32(
                        local_offset_32(ea->data.destroy_local_cond.local)
                    );
                    e.e = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_local_cond.dtor, offset + 12);
                }
                break;
            }
            case EAT_DESTROYLOCALOFFSET: {
                if (local_is_16bit_offset(ea->data.destroy_local_offset.local)) {
                    AABC e;
                    e.a = lastFlag | 2;
                    e.b = 0;
                    e.c = CTool_EndianConvertWord16(
                        ea->data.destroy_local_offset.offset +
                        local_offset_16(ea->data.destroy_local_offset.local)
                    );
                    e.d = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_local_offset.dtor, offset + 4);
                } else {
                    AACC e;
                    e.a = lastFlag | 0x11;
                    e.b = 0;
                    e.c = CTool_EndianConvertWord32(
                        ea->data.destroy_local_offset.offset +
                        local_offset_32(ea->data.destroy_local_offset.local)
                    );
                    e.d = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_local_offset.dtor, offset + 8);
                }
                break;
            }
            case EAT_DESTROYLOCALPOINTER: {
                reg = OBJECT_REG(ea->data.destroy_local_pointer.pointer);
                if (reg || local_is_16bit_offset(ea->data.destroy_local_pointer.pointer)) {
                    AABC e;
                    e.a = lastFlag | 4;
                    e.b = (reg ? 1 : 0) << 7;
                    e.c = CTool_EndianConvertWord16(
                        reg ? reg : local_offset_16(ea->data.destroy_local_pointer.pointer)
                    );
                    e.d = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_local_pointer.dtor, offset + 4);
                } else {
                    AACC e;
                    e.a = lastFlag | 0x13;
                    e.b = (reg ? 1 : 0) << 7;
                    e.c = CTool_EndianConvertWord32(
                        reg ? reg : local_offset_32(ea->data.destroy_local_pointer.pointer)
                    );
                    e.d = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_local_pointer.dtor, offset + 8);
                }
                break;
            }
            case EAT_DESTROYLOCALARRAY: {
                if (local_is_16bit_offset(ea->data.destroy_local_array.localarray)) {
                    AABBBC e;
                    e.a = lastFlag | 5;
                    e.b = 0;
                    e.c = CTool_EndianConvertWord16(
                        local_offset_16(ea->data.destroy_local_array.localarray)
                    );
                    e.d = CTool_EndianConvertWord16(ea->data.destroy_local_array.elements);
                    e.e = CTool_EndianConvertWord16(ea->data.destroy_local_array.element_size);
                    e.f = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_local_array.dtor, offset + 8);
                } else {
                    AACCCC e;
                    e.a = lastFlag | 0x14;
                    e.b = 0;
                    e.c = CTool_EndianConvertWord32(
                        local_offset_32(ea->data.destroy_local_array.localarray)
                    );
                    e.d = CTool_EndianConvertWord32(ea->data.destroy_local_array.elements);
                    e.e = CTool_EndianConvertWord32(ea->data.destroy_local_array.element_size);
                    e.f = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_local_array.dtor, offset + 16);
                }
                break;
            }
            case EAT_DESTROYMEMBER: {
                reg = OBJECT_REG(ea->data.destroy_member.objectptr);
                if (reg || local_is_16bit_offset(ea->data.destroy_member.objectptr)) {
                    AABCC e;
                    e.a = lastFlag | 7;
                    e.b = (reg ? 1 : 0) << 7;
                    e.c = CTool_EndianConvertWord16(
                        reg ? reg : local_offset_16(ea->data.destroy_member.objectptr)
                    );
                    e.d = CTool_EndianConvertWord32(ea->data.destroy_member.offset);
                    e.e = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_member.dtor, offset + 8);
                } else {
                    AACCC e;
                    e.a = lastFlag | 0x16;
                    e.b = (reg ? 1 : 0) << 7;
                    e.c = CTool_EndianConvertWord32(
                        reg ? reg : local_offset_32(ea->data.destroy_member.objectptr)
                    );
                    e.d = CTool_EndianConvertWord32(ea->data.destroy_member.offset);
                    e.e = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_member.dtor, offset + 12);
                }
                break;
            }
            case EAT_DESTROYBASE: {
                reg = OBJECT_REG(ea->data.destroy_base.objectptr);
                if (reg || local_is_16bit_offset(ea->data.destroy_base.objectptr)) {
                    AABCC e;
                    e.a = lastFlag | 6;
                    e.b = (reg ? 1 : 0) << 7;
                    e.c = CTool_EndianConvertWord16(
                        reg ? reg : local_offset_16(ea->data.destroy_base.objectptr)
                    );
                    e.d = CTool_EndianConvertWord32(ea->data.destroy_base.offset);
                    e.e = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_base.dtor, offset + 8);
                } else {
                    AACCC e;
                    e.a = lastFlag | 0x15;
                    e.b = (reg ? 1 : 0) << 7;
                    e.c = CTool_EndianConvertWord32(
                        reg ? reg : local_offset_32(ea->data.destroy_base.objectptr)
                    );
                    e.d = CTool_EndianConvertWord32(ea->data.destroy_base.offset);
                    e.e = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_base.dtor, offset + 12);
                }
                break;
            }
            case EAT_DESTROYMEMBERCOND: {
                reg = OBJECT_REG(ea->data.destroy_member_cond.cond);
                reg2 = OBJECT_REG(ea->data.destroy_member_cond.objectptr);
                if ((reg || local_is_16bit_offset(ea->data.destroy_member_cond.cond)) &&
                    (reg2 || local_is_16bit_offset(ea->data.destroy_member_cond.objectptr))) {
                    AABBCC e;
                    e.a = lastFlag | 8;
                    e.b = ((reg ? 1 : 0) << 7) | (((reg2 ? 1 : 0) & 1) << 6);
                    e.c = CTool_EndianConvertWord16(
                        reg ? reg : local_offset_16(ea->data.destroy_member_cond.cond)
                    );
                    e.d = CTool_EndianConvertWord16(
                        reg2 ? reg2 : local_offset_16(ea->data.destroy_member_cond.objectptr)
                    );
                    e.e = CTool_EndianConvertWord32(ea->data.destroy_member_cond.offset);
                    e.f = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_member_cond.dtor, offset + 12);
                } else {
                    AACCCC e;
                    e.a = lastFlag | 0x17;
                    e.b = ((reg ? 1 : 0) << 7) | (((reg2 ? 1 : 0) & 1) << 6);
                    e.c = CTool_EndianConvertWord32(
                        reg ? reg : local_offset_32(ea->data.destroy_member_cond.cond)
                    );
                    e.d = CTool_EndianConvertWord32(
                        reg2 ? reg2 : local_offset_32(ea->data.destroy_member_cond.objectptr)
                    );
                    e.e = CTool_EndianConvertWord32(ea->data.destroy_member_cond.offset);
                    e.f = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_member_cond.dtor, offset + 16);
                }
                break;
            }
            case EAT_DESTROYMEMBERARRAY: {
                reg = OBJECT_REG(ea->data.destroy_member_array.objectptr);
                if (reg || local_is_16bit_offset(ea->data.destroy_member_array.objectptr)) {
                    AABCCCC e;
                    e.a = lastFlag | 9;
                    e.b = (reg ? 1 : 0) << 7;
                    e.c = CTool_EndianConvertWord16(
                        reg ? reg : local_offset_16(ea->data.destroy_member_array.objectptr)
                    );
                    e.d = CTool_EndianConvertWord32(ea->data.destroy_member_array.offset);
                    e.e = CTool_EndianConvertWord32(ea->data.destroy_member_array.elements);
                    e.f = CTool_EndianConvertWord32(ea->data.destroy_member_array.element_size);
                    e.g = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_member_array.dtor, offset + 16);
                } else {
                    AACCCCC e;
                    e.a = lastFlag | 0x18;
                    e.b = (reg ? 1 : 0) << 7;
                    e.c = CTool_EndianConvertWord32(
                        reg ? reg : local_offset_32(ea->data.destroy_member_array.objectptr)
                    );
                    e.d = CTool_EndianConvertWord32(ea->data.destroy_member_array.offset);
                    e.e = CTool_EndianConvertWord32(ea->data.destroy_member_array.elements);
                    e.f = CTool_EndianConvertWord32(ea->data.destroy_member_array.element_size);
                    e.g = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.destroy_member_array.dtor, offset + 20);
                }
                break;
            }
            case EAT_DELETEPOINTER:
            case EAT_DELETELOCALPOINTER: {
                reg = OBJECT_REG(ea->data.delete_pointer.pointerobject);
                if (reg || local_is_16bit_offset(ea->data.delete_pointer.pointerobject)) {
                    AABC e;
                    e.a = lastFlag | 0xA;
                    e.b = (reg ? 1 : 0) << 7;
                    e.c = CTool_EndianConvertWord16(
                        reg ? reg : local_offset_16(ea->data.delete_pointer.pointerobject)
                    );
                    e.d = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.delete_pointer.deletefunc, offset + 4);
                } else {
                    AACC e;
                    e.a = lastFlag | 0x19;
                    e.b = (reg ? 1 : 0) << 7;
                    e.c = CTool_EndianConvertWord32(
                        reg ? reg : local_offset_32(ea->data.delete_pointer.pointerobject)
                    );
                    e.d = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.delete_pointer.deletefunc, offset + 8);
                }
                break;
            }
            case EAT_DELETEPOINTERCOND: {
                reg = OBJECT_REG(ea->data.delete_pointer_cond.cond);
                reg2 = OBJECT_REG(ea->data.delete_pointer_cond.pointerobject);
                if ((reg || local_is_16bit_offset(ea->data.delete_pointer_cond.cond)) &&
                    (reg2 || local_is_16bit_offset(ea->data.delete_pointer_cond.pointerobject))) {
                    AABBC e;
                    e.a = lastFlag | 0xB;
                    e.b = ((reg ? 1 : 0) << 7) | (((reg2 ? 1 : 0) & 1) << 6);
                    e.c = CTool_EndianConvertWord16(
                        reg ? reg : local_offset_16(ea->data.delete_pointer_cond.cond)
                    );
                    e.d = CTool_EndianConvertWord16(
                        reg2 ? reg2 : local_offset_16(ea->data.delete_pointer_cond.pointerobject)
                    );
                    e.e = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.delete_pointer_cond.deletefunc, offset + 8);
                } else {
                    AACCC e;
                    e.a = lastFlag | 0x1A;
                    e.b = ((reg ? 1 : 0) << 7) | (((reg2 ? 1 : 0) & 1) << 6);
                    e.c = CTool_EndianConvertWord32(
                        reg ? reg : local_offset_32(ea->data.delete_pointer_cond.cond)
                    );
                    e.d = CTool_EndianConvertWord32(
                        reg2 ? reg2 : local_offset_32(ea->data.delete_pointer_cond.pointerobject)
                    );
                    e.e = 0;
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                    addrelocation(ea->data.delete_pointer_cond.deletefunc, offset + 12);
                }
                break;
            }
            case EAT_CATCHBLOCK: {
                AACCC e;
                e.a = lastFlag | 0x10;
                e.b = 0;
                e.c = 0;
                if (ea->data.catch_block.catch_label->pclabel) {
                    e.d = CTool_EndianConvertWord32(
                        ea->data.catch_block.catch_label->pclabel->block->codeOffset
                    );
                } else {
                    e.d = 0;
                }
                e.e = CTool_EndianConvertWord32(
                    local_offset_16(ea->data.catch_block.catch_info_object)
                );
                AppendGListData(&exceptmodule, &e, sizeof(e));
                if (ea->data.catch_block.catch_typeid) {
                    addrelocation(ea->data.catch_block.catch_typeid, offset + 4);
                }
                break;
            }
            case EAT_ACTIVECATCHBLOCK: {
                if (local_is_16bit_offset(ea->data.active_catch_block.catch_info_object)) {
                    AAB e;
                    e.a = lastFlag | 0xD;
                    e.b = 0;
                    e.c = CTool_EndianConvertWord16(
                        local_offset_16(ea->data.active_catch_block.catch_info_object)
                    );
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                } else {
                    AAC e;
                    e.a = lastFlag | 0x1B;
                    e.b = 0;
                    e.c = CTool_EndianConvertWord32(
                        local_offset_32(ea->data.active_catch_block.catch_info_object)
                    );
                    AppendGListData(&exceptmodule, &e, sizeof(e));
                }
                break;
            }
            case EAT_SPECIFICATION: {
                AABCC e;
                int i;

                e.a = lastFlag | 0xF;
                e.b = 0;
                e.c = CTool_EndianConvertWord16(ea->data.specification.unexp_ids);
                if (ea->data.specification.unexp_label->pclabel) {
                    e.d = CTool_EndianConvertWord32(
                        ea->data.specification.unexp_label->pclabel->block->codeOffset
                    );
                }
                e.e = CTool_EndianConvertWord32(
                    local_offset_16(ea->data.specification.unexp_info_object)
                );
                AppendGListData(&exceptmodule, &e, sizeof(e));

                for (i = 0; i < ea->data.specification.unexp_ids; i++) {
                    addrelocation(ea->data.specification.unexp_id[i], 12 + i * 4 + offset);
                    AppendGListLong(&exceptmodule, 0);
                }
                break;
            }
            case EAT_TERMINATE: {
                AA e;
                e.a = lastFlag | 0xE;
                e.b = 0;
                AppendGListData(&exceptmodule, &e, sizeof(e));
                break;
            }
            default:
                CError_FATAL(717);
        }

        node = node->prev;
    }

    if (node) {
        AAB e;
        e.a = 1;
        e.b = 0;
        e.c = CTool_EndianConvertWord16(node->actionOffset);
        AppendGListData(&exceptmodule, &e, sizeof(e));
    }
}

static UInt32 findPC(PCode* instr) {
    UInt32 pc = instr->block->codeOffset;
    instr = instr->prevPCode;
    while (instr) {
        instr = instr->prevPCode;
        pc += 4;
    }
    CError_ASSERT(704, FITS_IN_USHORT(pc));
    return pc;
}

static UInt32 findPC_long(PCode* instr) {
    UInt32 pc = instr->block->codeOffset;
    instr = instr->prevPCode;
    while (instr) {
        pc += 4;
        instr = instr->prevPCode;
    }
    return pc;
}

void initializeexceptiontables(void) {
    int i;

    for (i = 0; i < EAT_NACTIONS; i++) {
        DAG[i] = NULL;
    }

    pc_actions = last_pc_action = NULL;
    except_refs = last_except_ref = NULL;
}

int countexceptionactionregisters(ExceptionAction* actions) {
    int count = 0;

    while (actions) {
        switch (actions->type) {
            case EAT_DESTROYLOCALCOND:
                if (OBJECT_REG(actions->data.destroy_local_cond.cond)) {
                    count++;
                }
                break;
            case EAT_DESTROYLOCALPOINTER:
                if (OBJECT_REG(actions->data.destroy_local_pointer.pointer)) {
                    count++;
                }
                break;
            case EAT_DESTROYMEMBER:
                if (OBJECT_REG(actions->data.destroy_member.objectptr)) {
                    count++;
                }
                break;
            case EAT_DESTROYBASE:
                if (OBJECT_REG(actions->data.destroy_base.objectptr)) {
                    count++;
                }
                break;
            case EAT_DESTROYMEMBERCOND:
                if (OBJECT_REG(actions->data.destroy_member_cond.cond)) {
                    count++;
                }
                if (OBJECT_REG(actions->data.destroy_member_cond.objectptr)) {
                    count++;
                }
                break;
            case EAT_DESTROYMEMBERARRAY:
                if (OBJECT_REG(actions->data.destroy_member_array.objectptr)) {
                    count++;
                }
                break;
            case EAT_DELETEPOINTER:
            case EAT_DELETELOCALPOINTER:
                if (OBJECT_REG(actions->data.delete_pointer.pointerobject)) {
                    count++;
                }
                break;
            case EAT_DELETEPOINTERCOND:
                if (OBJECT_REG(actions->data.delete_pointer_cond.cond)) {
                    count++;
                }
                if (OBJECT_REG(actions->data.delete_pointer_cond.pointerobject)) {
                    count++;
                }
                break;
            default:
                break;
        }
        actions = actions->prev;
    }

    return count;
}

void noteexceptionactionregisters(ExceptionAction* actions, PCodeArg* ops) {
    Object* obj;
    int reg;

    while (actions) {
        switch (actions->type) {
            case EAT_DESTROYLOCALCOND:
                reg = OBJECT_REG(obj = actions->data.destroy_local_cond.cond);
                if (reg) {
                    ops->kind = PCOp_REGISTER;
                    ops->arg = obj->u.var.info->rclass;
                    ops->data.reg.reg = reg;
                    ops->data.reg.effect = EffectRead | EffectPinned;
                    ops++;
                }
                break;
            case EAT_DESTROYLOCALPOINTER:
                reg = OBJECT_REG(obj = actions->data.destroy_local_pointer.pointer);
                if (reg) {
                    ops->kind = PCOp_REGISTER;
                    ops->arg = obj->u.var.info->rclass;
                    ops->data.reg.reg = reg;
                    ops->data.reg.effect = EffectRead | EffectPinned;
                    ops++;
                }
                break;
            case EAT_DESTROYMEMBER:
                reg = OBJECT_REG(obj = actions->data.destroy_member.objectptr);
                if (reg) {
                    ops->kind = PCOp_REGISTER;
                    ops->arg = obj->u.var.info->rclass;
                    ops->data.reg.reg = reg;
                    ops->data.reg.effect = EffectRead | EffectPinned;
                    ops++;
                }
                break;
            case EAT_DESTROYBASE:
                reg = OBJECT_REG(obj = actions->data.destroy_base.objectptr);
                if (reg) {
                    ops->kind = PCOp_REGISTER;
                    ops->arg = obj->u.var.info->rclass;
                    ops->data.reg.reg = reg;
                    ops->data.reg.effect = EffectRead | EffectPinned;
                    ops++;
                }
                break;
            case EAT_DESTROYMEMBERCOND:
                reg = OBJECT_REG(obj = actions->data.destroy_member_cond.cond);
                if (reg) {
                    ops->kind = PCOp_REGISTER;
                    ops->arg = obj->u.var.info->rclass;
                    ops->data.reg.reg = reg;
                    ops->data.reg.effect = EffectRead | EffectPinned;
                    ops++;
                }
                reg = OBJECT_REG(obj = actions->data.destroy_member_cond.objectptr);
                if (reg) {
                    ops->kind = PCOp_REGISTER;
                    ops->arg = obj->u.var.info->rclass;
                    ops->data.reg.reg = reg;
                    ops->data.reg.effect = EffectRead | EffectPinned;
                    ops++;
                }
                break;
            case EAT_DESTROYMEMBERARRAY:
                reg = OBJECT_REG(obj = actions->data.destroy_member_array.objectptr);
                if (reg) {
                    ops->kind = PCOp_REGISTER;
                    ops->arg = obj->u.var.info->rclass;
                    ops->data.reg.reg = reg;
                    ops->data.reg.effect = EffectRead | EffectPinned;
                    ops++;
                }
                break;
            case EAT_DELETEPOINTER:
            case EAT_DELETELOCALPOINTER:
                reg = OBJECT_REG(obj = actions->data.delete_pointer.pointerobject);
                if (reg) {
                    ops->kind = PCOp_REGISTER;
                    ops->arg = obj->u.var.info->rclass;
                    ops->data.reg.reg = reg;
                    ops->data.reg.effect = EffectRead | EffectPinned;
                    ops++;
                }
                break;
            case EAT_DELETEPOINTERCOND:
                reg = OBJECT_REG(obj = actions->data.delete_pointer_cond.cond);
                if (reg) {
                    ops->kind = PCOp_REGISTER;
                    ops->arg = obj->u.var.info->rclass;
                    ops->data.reg.reg = reg;
                    ops->data.reg.effect = EffectRead | EffectPinned;
                    ops++;
                }
                reg = OBJECT_REG(obj = actions->data.delete_pointer_cond.pointerobject);
                if (reg) {
                    ops->kind = PCOp_REGISTER;
                    ops->arg = obj->u.var.info->rclass;
                    ops->data.reg.reg = reg;
                    ops->data.reg.effect = EffectRead | EffectPinned;
                    ops++;
                }
                break;
            default:
                break;
        }
        actions = actions->prev;
    }
}

void recordexceptionactions(PCode* instr, ExceptionAction* actions) {
    PCAction* pca;

    if (!actions && (!last_pc_action || !last_pc_action->actions)) {
        return;
    }

    pca = lalloc(sizeof(PCAction));
    pca->next = NULL;
    pca->firstInstr = pca->lastInstr = instr;
    pca->actions = actions;
    pca->prev = last_pc_action;
    if (last_pc_action) {
        last_pc_action->next = pca;
    } else {
        pc_actions = pca;
    }
    last_pc_action = pca;

    branch_label(makepclabel());
    while (actions) {
        if (actions->type == EAT_CATCHBLOCK && actions->data.catch_block.catch_label->pclabel) {
            pcbranch(instr->block, actions->data.catch_block.catch_label->pclabel);
        } else if (actions->type == EAT_SPECIFICATION &&
                   actions->data.catch_block.catch_label->pclabel) {
            pcbranch(instr->block, actions->data.catch_block.catch_label->pclabel);
        }
        actions = actions->prev;
    }
}

static void deleteexceptionaction(PCAction* pca) {
    if (pca->prev) {
        pca->prev->next = pca->next;
    } else {
        pc_actions = pca->next;
    }

    if (pca->next) {
        pca->next->prev = pca->prev;
    }
}

static int mergeexceptionactions(void) {
    int count;
    PCAction* pca;
    PCAction* prev;

    if (!pc_actions) {
        return 0;
    }

    for (pca = pc_actions; pca; pca = pca->next) {
        if (pca->firstInstr->block->flags & fDeleted) {
            deleteexceptionaction(pca);
        }
    }

    if (!pc_actions) {
        return 0;
    }

    for (pca = pc_actions; pca; pca = pca->next) {
        pca->node = pca->actions ? makeEAnode(pca->actions) : NULL;
    }

    prev = pc_actions;
    for (pca = pc_actions->next; pca; pca = pca->next) {
        if (pca->node == prev->node) {
            prev->lastInstr = pca->lastInstr;
            deleteexceptionaction(pca);
        } else {
            prev = pca;
        }
    }

    count = 0;
    for (pca = pc_actions; pca; pca = pca->next) {
        if (!pca->actions) {
            deleteexceptionaction(pca);
        } else {
            count++;
        }
    }

    return count;
}

typedef struct ExceptionRange {
    UInt32 pcOffset;
    UInt16 pcCount;
    UInt16 actionOffset;
} ExceptionRange;

void dumpexceptiontables(Object* function, SInt32 codesize) {
    PCAction* pca;
    UInt32 insn_start;
    UInt32 insn_count;
    UInt16* sh;
    ExceptionRange* range;
    int count;
    char* data;
    SInt32 zero = 0;

    count = mergeexceptionactions();
    InitGList(&exceptmodule, 256);
    AppendGListNoData(&exceptmodule, 8 * count + 4);
    AppendGListLong(&exceptmodule, 0);

    for (pca = pc_actions; pca; pca = pca->next) {
        if (pca->node->count == 0 && pca->node->actionOffset == 0) {
            if (exceptmodule.size & 3) {
                AppendGListData(
                    &exceptmodule, &zero, ((exceptmodule.size + 3) & ~3) - exceptmodule.size
                );
            }
            allocateactioninfo(pca->node);
        }
    }

    data = *exceptmodule.data;
    sh = (UInt16*)data;
    if (copts.altivec_model && used_nonvolatile_registers[RegClass_VR]) {
        sh[0] = (used_nonvolatile_registers[RegClass_GPR] << 11) |
            ((used_nonvolatile_registers[RegClass_FPR] & 0x1F) << 6) |
            (((used_nonvolatile_registers[RegClass_CRFIELD] != 0) & 1) << 5) |
            ((dynamic_stack & 1) << 4) | 8;
        sh[0] = sh[0] | 4;
        sh[0] = CTool_EndianConvertWord16(sh[0]);
        if (copts.altivec_vrsave) {
            sh[1] =
                CTool_EndianConvertWord16((used_nonvolatile_registers[RegClass_VR] << 11) | 0x400);
        } else {
            sh[1] = CTool_EndianConvertWord16(used_nonvolatile_registers[RegClass_VR] << 11);
        }
    } else {
        sh[0] = (used_nonvolatile_registers[RegClass_GPR] << 11) |
            ((used_nonvolatile_registers[RegClass_FPR] & 0x1F) << 6) |
            (((used_nonvolatile_registers[RegClass_CRFIELD] != 0) & 1) << 5) |
            ((dynamic_stack & 1) << 4) | 8;
        if ((copts.processor == CPU_PPCELF || copts.processor == CPU_PPCGEKKO) &&
            StackFrame_has_vectors) {
            sh[0] = sh[0] | 2;
        }
        sh[0] = CTool_EndianConvertWord16(sh[0]);
        sh[1] = 0;
    }

    range = (ExceptionRange*)(data + 4);
    pca = pc_actions;
    while (pca) {
        insn_start = findPC_long(pca->firstInstr);
        insn_count = (findPC_long(pca->lastInstr) - insn_start) / 4;
        CError_ASSERT(1264, (insn_count & 0xFFFF0000) == 0);
        range->pcOffset = CTool_EndianConvertWord32(insn_start + 4);
        range->pcCount = CTool_EndianConvertWord16(insn_count);
        range->actionOffset = CTool_EndianConvertWord16(pca->node->actionOffset);
        pca = pca->next;
        range++;
    }

    LockGList(&exceptmodule);
    ObjGen_DeclareExceptionTables(
        function, codesize, *exceptmodule.data, exceptmodule.size, except_refs
    );
    FreeGList(&exceptmodule);
}
