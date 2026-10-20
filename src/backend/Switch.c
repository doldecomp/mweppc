#include "compiler/Switch.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/InstrSelection.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/Operands.h"
#include "compiler/PCode.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/RegisterInfo.h"
#include "compiler/TOC.h"
#include "common.h"
#include "PCodeInfo.h"
#include "tokens.h"
#include "types.h"
#include <stdlib.h>

typedef struct CaseRange {
    CInt64 min;
    CInt64 range;
    PCodeLabel* label;
} CaseRange;

ObjectList* switchtables;
static SwitchCase** caselabels;
static CaseRange* caseranges;
static SInt32 ncases;
static SInt32 nranges_minus1;
static CInt64 min;
static CInt64 max;
static CInt64 first;
static short selector_gpr;
static short selector_gprHi;
static Type* selector_type;
static PCodeLabel* defaultlabel;
static CInt64 range;

static int compare_cases(const void* a, const void* b) {
    const SwitchCase** casea = (const SwitchCase**)a;
    const SwitchCase** caseb = (const SwitchCase**)b;

    if (CInt64_Less((*casea)->min, (*caseb)->min)) {
        return -1;
    }
    if (CInt64_Greater((*casea)->min, (*caseb)->min)) {
        return 1;
    }
    return 0;
}

static void build_case_ranges(Type* type, SwitchCase* cases, CLabel* label) {
    SwitchCase** caseptr;
    SInt32 i;
    SwitchCase* curcase;
    CaseRange* currange;

    if (type->size == 8) {
        min.lo = 0;
        min.hi = 0x80000000;
        max.lo = 0xFFFFFFFF;
        max.hi = 0x7FFFFFFF;
    } else if (type->size == 4) {
        CInt64_SetLong(&min, 0x80000000);
        CInt64_SetLong(&max, 0x7FFFFFFF);
    } else if (is_unsigned(type)) {
        min.hi = 0;
        min.lo = 0;
        max.hi = 0;
        max.lo = 0xFFFF;
    } else {
        CInt64_SetLong(&min, -0x8000);
        CInt64_SetLong(&max, 0x7FFF);
    }

    caselabels = lalloc(sizeof(SwitchCase*) * ncases);
    caseptr = caselabels;
    while (cases) {
        *caseptr++ = cases;
        cases = cases->next;
    }

    caseranges = lalloc(((ncases * 2) + 2) * sizeof(CaseRange));
    if (type->size < 8) {
        for (i = 0; i < ncases; i++) {
            CInt64_SetLong(&caselabels[i]->min, caselabels[i]->min.lo);
        }
    }

    qsort(caselabels, ncases, sizeof(SwitchCase*), &compare_cases);

    currange = caseranges;
    currange->min = min;
    currange->range = CInt64_Sub(max, min);
    currange->label = label->pclabel;

    for (i = 0; i < ncases; i++) {
        curcase = caselabels[i];
        if (CInt64_GreaterEqual(curcase->min, min) && CInt64_LessEqual(curcase->min, max)) {
            if (CInt64_Equal(currange->min, min)) {
                first = curcase->min;
            }
            range = CInt64_Sub(curcase->min, first);

            if (CInt64_Greater(curcase->min, currange->min)) {
                currange->range = CInt64_Sub(CInt64_Sub(curcase->min, currange->min), cint64_one);
                (++currange)->min = curcase->min;
            } else if (CInt64_Greater(currange->min, min) &&
                       curcase->label->pclabel == currange[-1].label) {
                currange[-1].range = CInt64_Add(currange[-1].range, cint64_one);
                if (CInt64_Equal(currange->range, cint64_zero)) {
                    currange--;
                } else {
                    currange->min = CInt64_Add(currange->min, cint64_one);
                    currange->range = CInt64_Sub(currange->range, cint64_one);
                }
                continue;
            }

            currange->range = cint64_zero;
            currange->label = curcase->label->pclabel;

            if (CInt64_Less(curcase->min, max)) {
                currange++;
                currange->min = CInt64_Add(curcase->min, cint64_one);
                currange->range = CInt64_Sub(max, currange->min);
                currange->label = label->pclabel;
            }
        }
    }

    nranges_minus1 = currange - caseranges;
}

// NOTE: invented names: below, pivot (here and in I8_treecompare)
// a binary search of the ranges start..end: `pivot` is the first range above the split, `below`
// the last one under it
static void treecompare(SInt32 start, SInt32 end) {
    int reg;
    SInt32 below;
    SInt32 pivot;
    int count;
    SInt32 value;
    CaseRange* currange;

    count = end - start;
    CError_ASSERT(175, selector_type->size <= 4);

    pivot = start + (count >> 1) + 1;
    currange = caseranges + pivot;

    if (CInt64_Equal(currange[-1].range, cint64_zero) &&
        (!(count & 1) || (CInt64_NotEqual(currange->range, cint64_zero) && count > 1))) {
        currange--;
        pivot--;
    }

    below = pivot - 1;

    if (selector_type->size < 4 && is_unsigned(selector_type)) {
        emitpcode(PC_CMPLI, 0, selector_gpr, CInt64_GetULong(&currange->min));
    } else {
        value = CInt64_GetULong(&currange->min);
        if (FITS_IN_SHORT(value)) {
            emitpcode(PC_CMPI, 0, selector_gpr, value);
        } else {
            load_immediate(reg = ALLOC_GPR(), value);
            emitpcode(PC_CMP, 0, selector_gpr, reg);
        }
    }

    if (CInt64_Equal(currange->range, cint64_zero) && pivot < end) {
        branch_conditional(0, EEQU, 1, currange->label);
        pivot++;
    }

    if (pivot == end) {
        if (start == below) {
            if (caseranges[start].label == caseranges[end].label) {
                branch_always(caseranges[start].label);
            } else {
                branch_conditional(0, EGREATEREQU, 1, caseranges[end].label);
                branch_always(caseranges[start].label);
            }
        } else {
            branch_conditional(0, EGREATEREQU, 1, caseranges[end].label);
            treecompare(start, below);
        }
    } else {
        if (start == below) {
            branch_conditional(0, ELESS, 1, caseranges[start].label);
            treecompare(pivot, end);
        } else {
            PCodeLabel* label = makepclabel();
            branch_conditional(0, EGREATEREQU, 1, label);
            treecompare(start, below);
            branch_label(label);
            treecompare(pivot, end);
        }
    }
}

static void I8_treecompare(SInt32 start, SInt32 end) {
    SInt32 below;
    SInt32 pivot;
    CaseRange* currange;
    int count;
    short a, b, c, d;

    count = end - start;

    pivot = start + (count >> 1) + 1;
    currange = caseranges + pivot;

    if (CInt64_Equal(currange[-1].range, cint64_zero) &&
        (!(count & 1) || (CInt64_NotEqual(currange->range, cint64_zero) && count > 1))) {
        currange--;
        pivot--;
    }

    below = pivot - 1;

    if (CInt64_Equal(currange->range, cint64_zero) && pivot < end) {
        a = ALLOC_GPR();
        b = ALLOC_GPR();
        load_immediate(a, currange->min.lo);
        load_immediate(b, currange->min.hi);
        emitpcode(PC_XOR, a, selector_gpr, a);
        emitpcode(PC_XOR, b, selector_gprHi, b);
        emitpcode(PC_OR, b, a, b);
        emitpcode(PC_CMPI, 0, b, 0);
        branch_conditional(0, EEQU, 1, currange->label);
        pivot++;
    }

    if (pivot == end) {
        if (start == below) {
            if (caseranges[start].label == caseranges[end].label) {
                branch_always(caseranges[start].label);
            } else {
                a = ALLOC_GPR();
                b = ALLOC_GPR();
                c = ALLOC_GPR();
                d = ALLOC_GPR();
                load_immediate(a, currange->min.lo);
                load_immediate(b, currange->min.hi);
                if (TYPE_INTEGRAL(selector_type)->integral != IT_ULONGLONG &&
                    TYPE_INTEGRAL(selector_type)->integral != IT_ULONGLONG) {
                    emitpcode(PC_XORIS, c, selector_gprHi, 0x8000);
                    emitpcode(PC_XORIS, d, b, 0x8000);
                } else {
                    c = selector_gprHi;
                    d = b;
                }
                emitpcode(PC_SUBFC, a, a, selector_gpr);
                emitpcode(PC_SUBFE, b, d, c);
                emitpcode(PC_SUBFE, b, a, a);
                emitpcode(PC_NEG, b, b);
                emitpcode(PC_CMPI, 0, b, 0);
                branch_conditional(0, EEQU, 1, caseranges[end].label);
                branch_always(caseranges[start].label);
            }
        } else {
            a = ALLOC_GPR();
            b = ALLOC_GPR();
            c = ALLOC_GPR();
            d = ALLOC_GPR();
            load_immediate(a, currange->min.lo);
            load_immediate(b, currange->min.hi);
            if (TYPE_INTEGRAL(selector_type)->integral != IT_ULONGLONG &&
                TYPE_INTEGRAL(selector_type)->integral != IT_ULONGLONG) {
                emitpcode(PC_XORIS, c, selector_gprHi, 0x8000);
                emitpcode(PC_XORIS, d, b, 0x8000);
            } else {
                c = selector_gprHi;
                d = b;
            }
            emitpcode(PC_SUBFC, a, a, selector_gpr);
            emitpcode(PC_SUBFE, b, d, c);
            emitpcode(PC_SUBFE, b, a, a);
            emitpcode(PC_NEG, b, b);
            emitpcode(PC_CMPI, 0, b, 0);
            branch_conditional(0, EEQU, 1, caseranges[end].label);
            I8_treecompare(start, below);
        }
    } else {
        if (start == below) {
            a = ALLOC_GPR();
            b = ALLOC_GPR();
            c = ALLOC_GPR();
            d = ALLOC_GPR();
            load_immediate(a, currange->min.lo);
            load_immediate(b, currange->min.hi);
            if (TYPE_INTEGRAL(selector_type)->integral != IT_ULONGLONG &&
                TYPE_INTEGRAL(selector_type)->integral != IT_ULONGLONG) {
                emitpcode(PC_XORIS, c, selector_gprHi, 0x8000);
                emitpcode(PC_XORIS, d, b, 0x8000);
            } else {
                c = selector_gprHi;
                d = b;
            }
            emitpcode(PC_SUBFC, a, selector_gpr, a);
            emitpcode(PC_SUBFE, b, c, d);
            emitpcode(PC_SUBFE, b, a, a);
            emitpcode(PC_NEG, b, b);
            emitpcode(PC_CMPI, 0, b, 0);
            // to the range below the pivot
            branch_conditional(0, ENOTEQU, 1, caseranges[start].label);
            I8_treecompare(pivot, end);
        } else {
            PCodeLabel* label;
            a = ALLOC_GPR();
            b = ALLOC_GPR();
            c = ALLOC_GPR();
            d = ALLOC_GPR();
            load_immediate(a, currange->min.lo);
            load_immediate(b, currange->min.hi);
            if (TYPE_INTEGRAL(selector_type)->integral != IT_ULONGLONG &&
                TYPE_INTEGRAL(selector_type)->integral != IT_ULONGLONG) {
                emitpcode(PC_XORIS, c, selector_gprHi, 0x8000);
                emitpcode(PC_XORIS, d, b, 0x8000);
            } else {
                c = selector_gprHi;
                d = b;
            }
            emitpcode(PC_SUBFC, a, a, selector_gpr);
            emitpcode(PC_SUBFE, b, d, c);
            emitpcode(PC_SUBFE, b, a, a);
            emitpcode(PC_NEG, b, b);
            emitpcode(PC_CMPI, 0, b, 0);
            label = makepclabel();
            branch_conditional(0, EEQU, 1, label);
            I8_treecompare(start, below);
            branch_label(label);
            I8_treecompare(pivot, end);
        }
    }
}

static void generate_tree(ENode* expr) {
    Operand op;

    memclrw(&op, sizeof(Operand));
    if (TYPE_IS_8BYTES(expr->rtype)) {
        GEN_NODE(expr, &op);
        coerce_to_register_pair(&op, expr->rtype, 0, 0);
        selector_type = expr->rtype;
        selector_gpr = op.reg;
        selector_gprHi = op.regHi;
        I8_treecompare(0, nranges_minus1);
    } else {
        GEN_NODE(expr, &op);
        if (expr->rtype->size < 4) {
            extend32(&op, expr->rtype, 0);
        }
        ENSURE_GPR(&op, expr->rtype, 0);
        selector_type = expr->rtype;
        selector_gpr = op.reg;
        treecompare(0, nranges_minus1);
    }
}

static Object* create_switch_table(void) {
    Object* obj;
    ObjectList* list;
    UInt32* outptr;
    CaseRange* currange;
    CInt64 value;

    obj = galloc(sizeof(Object));
    list = galloc(sizeof(ObjectList));
    memclrw(obj, sizeof(Object));
    memclrw(list, sizeof(ObjectList));

    obj->otype = OT_OBJECT;
    obj->access = ACCESSPUBLIC;
    obj->datatype = DDATA;
    obj->name = CParser_GetUniqueName();
    obj->toc = NULL;
    obj->sclass = TK_STATIC;
    obj->qual = Q_CONST;
    obj->flags |= OBJECT_REFERENCED | OBJECT_DEFINED;
    obj->u.data.linkname = obj->name;
    obj->type = NULL;
    // the table goes in the data section, and its type is set before createIndirect
    obj->section = GetCurrentSection(SECT_DATA);
    obj->type = TYPE(&void_ptr);
    createIndirect(obj, FALSE, FALSE);

    obj->u.data.u.switchtable.data =
        lalloc(4 * (obj->u.data.u.switchtable.size = CInt64_GetULong(&range) + 1));

    currange = caseranges;
    outptr = (UInt32*)obj->u.data.u.switchtable.data;
    value = cint64_zero;
    while (CInt64_LessEqual(value, range)) {
        while (
            CInt64_Greater(CInt64_Add(first, value), CInt64_Add(currange->min, currange->range))) {
            currange++;
        }
        *outptr = (UInt32)currange->label;
        value = CInt64_Add(value, cint64_one);
        outptr++;
    }

    list->object = obj;
    list->next = switchtables;
    switchtables = list;
    return list->object;
}

// NOTE: invented names: targetReg, three
static void generate_table(ENode* expr, SwitchInfo* info) {
    Object* table;
    short reg;
    SwitchCase* curcase;
    short reg2;
    short targetReg;
    Operand op1;
    Operand op2;

    CInt64 three = {0, 3};
    memclrw(&op1, sizeof(Operand));
    memclrw(&op2, sizeof(Operand));

    if (CInt64_Greater(first, cint64_zero) && CInt64_Less(first, three)) {
        range = CInt64_Add(range, first);
        first = cint64_zero;
    }

    table = create_switch_table();
    CError_ASSERT(553, !TYPE_IS_8BYTES(expr->rtype));

    GEN_NODE(expr, &op1);
    if (expr->rtype->size < 4) {
        extend32(&op1, expr->rtype, 0);
    }
    ENSURE_GPR(&op1, expr->rtype, 0);

    reg = op1.reg;
    if (CInt64_NotEqual(first, cint64_zero)) {
        SInt32 value;
        reg = ALLOC_GPR();
        value = -CInt64_GetULong(&first);
        if (!FITS_IN_SHORT(value)) {
            emitpcode(PC_ADDIS, reg, op1.reg, 0, HIGH_PART(value));
            if (LOW_PART(value)) {
                emitpcode(PC_ADDI, reg, reg, 0, LOW_PART(value));
            }
        } else {
            emitpcode(PC_ADDI, reg, op1.reg, 0, value);
        }
    }

    if (!FITS_IN_USHORT(CInt64_GetULong(&range))) {
        load_immediate(reg2 = ALLOC_GPR(), CInt64_GetULong(&range));
        emitpcode(PC_CMPL, 0, reg, reg2);
    } else {
        emitpcode(PC_CMPLI, 0, reg, CInt64_GetULong(&range));
    }

    branch_conditional(0, EGREATER, 1, defaultlabel);
    if (table->toc) {
        op2.optype = OpndType_Symbol;
        op2.object = table->toc;
        indirect(&op2, NULL);
    } else {
        op2.optype = OpndType_Symbol;
        op2.object = table;
    }

    if (op2.optype != OpndType_GPR) {
        Coerce_to_register(&op2, TYPE(&void_ptr), reg2 = ALLOC_GPR());
    }

    if (op2.optype != OpndType_GPR) {
        CError_FATAL(599);
    } else {
        if (op2.reg != reg2) {
            emitpcode(PC_MR, reg2, op2.reg);
        }
    }

    if (CInt64_Equal(first, cint64_zero)) {
        reg = ALLOC_GPR();
        emitpcode(PC_RLWINM, reg, op1.reg, 2, 0, 29);
    } else {
        emitpcode(PC_RLWINM, reg, reg, 2, 0, 29);
    }

    targetReg = reg2;
    emitpcode(PC_LWZX, targetReg, targetReg, reg);
    for (curcase = info->cases; curcase; curcase = curcase->next) {
        pcbranch(pclastblock, curcase->label->pclabel);
    }
    pcbranch(pclastblock, info->defaultlabel->pclabel);
    emitpcode(PC_MTCTR, targetReg);
    branch_indirect(table);
}

void switchstatement(ENode* expr, SwitchInfo* info) {
    Boolean use_table;
    SwitchCase* swcase;

    use_table = copts.switch_tables;
    // no jump tables when preparing for code compression
    if (copts.prepare_compress) {
        use_table = FALSE;
    }

    ncases = 0;
    for (swcase = info->cases; swcase; swcase = swcase->next) {
        if (!swcase->label->pclabel) {
            swcase->label->pclabel = makepclabel();
        }
        ncases++;
    }

    CError_ASSERT(656, ncases >= 0 && ncases <= 0x3333332U);

    if (!info->defaultlabel->pclabel) {
        info->defaultlabel->pclabel = makepclabel();
    }
    defaultlabel = info->defaultlabel->pclabel;

    build_case_ranges(expr->rtype, info->cases, info->defaultlabel);

    if (TYPE_IS_8BYTES(expr->rtype)) {
        generate_tree(expr);
    } else if (!use_table || nranges_minus1 < 8 || (nranges_minus1 * 2) < ((range.lo / 2) + 4)) {
        generate_tree(expr);
    } else {
        generate_table(expr, info);
    }
}

void dumpswitchtables(Object* funcobj) {
    SInt32 size;
    UInt32* array;
    Object* table;
    ObjectList* list;

    for (list = switchtables; list; list = list->next) {
        table = list->object;
        CError_ASSERT(
            694,
            table->otype == OT_OBJECT && table->access == ACCESSPUBLIC && table->datatype == DDATA
        );

        size = table->u.data.u.switchtable.size;
        array = (UInt32*)table->u.data.u.switchtable.data;
        while (size) {
            *array = CTool_EndianConvertWord32(((PCodeLabel*)*array)->block->codeOffset);
            size--;
            array++;
        }

        ObjGen_DeclareSwitchTable(table, funcobj);
    }
}
