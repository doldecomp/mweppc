#include "compiler/PCodeInfo.h"
#include "compiler/Alias.h"
#include "compiler/CError.h"
#include "compiler/CMangler.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/PCode.h"
#include "compiler/PCodeListing.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/StackFrame.h"
#include "compiler/TOC.h"
#include "compiler/types.h"
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma pool_strings on

void pcode_get_hi_lo(int bits, char typechar, SInt32* hi, SInt32* lo) {
    if (bits == 0) {
        *hi = 0;
        *lo = 0;
    } else if (bits == 32) {
        *hi = 0x7FFFFFFF;
        *lo = -0x80000000;
    } else if (bits < 32 && bits > 0) {
        switch (typechar) {
            case 'u':
                *hi = (1 << bits) - 1;
                *lo = 0;
                break;
            case 'i':
            case 'x':
                *hi = (1 << bits) - 1;
                *lo = -(1 << (bits - 1));
                break;
            default:
                *hi = (1 << (bits - 1)) - 1;
                *lo = -(1 << (bits - 1));
        }
    } else {
        CError_FATAL(68);
    }
}

int pcode_check_imm_bits(SInt32 value, int bits, char typechar) {
    char buf[4];
    int forcedBits;
    SInt32 hi;
    SInt32 lo;

    forcedBits = 0;
    if (bits >= 0 && bits < 32) {
        if (bits == 0) {
            hi = 0;
            lo = 0;
        } else {
            switch (typechar) {
                case 'u':
                    hi = (1 << bits) - 1;
                    lo = 0;
                    break;
                case 'i':
                case 'x':
                    hi = (1 << bits) - 1;
                    lo = -(1 << (bits - 1));
                    break;
                case '1':
                case '2':
                case '3':
                case '4':
                    buf[0] = typechar;
                    buf[1] = 0;
                    forcedBits = atoi(buf);
                default:
                    hi = (1 << (bits - 1)) - 1;
                    lo = -(1 << (bits - 1));
            }
        }

        if ((value < lo) || (value > hi)) {
            return TRUE;
        }
        if (forcedBits > 0 && value != ((value >> forcedBits) << forcedBits)) {
            return TRUE;
        }
    } else if (bits > 32) {
        CError_FATAL(113);
    }

    return FALSE;
}

int pcode_const_from_format(const char* format, SInt32* pResult) {
    char buf[32];
    int len = 0;

    for (len = 0; len < 30 && isdigit(format[len]); len++) {
        buf[len] = format[len];
    }
    buf[len] = 0;

    *pResult = atoi(buf);
    return len;
}

PCode* vformatpcode(short opcode, va_list argList) {
    int argcount;
    OpcodeInfo* info;
    int effect;
    const char* format;
    PCode* pcode;
    PCodeArg* arg;
    int nregs;
    int extra;
    PCodeArg* lastArg;
    int pcode_size;
    int tmp;
    int foundSysreg;
    int i;
    SInt32 pbits;
    SInt32 spr;
    SInt32 bits;
    SInt32 suffix;
    PCodeLabel* label;
    Object* obj;
    char c;

    info = &opcodeinfo[opcode];
    format = info->format;
    argcount = info->operand_count;
    nregs = 0;
    extra = 0;

    if (format[0] == '#') {
        nregs = va_arg(argList, int);
        argcount += nregs;
        format++;
    }

    if (info->flags & fCanSetRecordBit) {
        extra++;
    }

    if ((argcount + extra) < 5) {
        extra += 5 - (argcount + extra);
    }

    pcode_size = (argcount + extra) * sizeof(PCodeArg) + offsetof(PCode, args);
    pcode = lalloc(pcode_size);
    memclrw(pcode, pcode_size);

    pcode->op = opcode;
    pcode->argCount = argcount;
    pcode->flags = info->flags;
    lastArg = pcode->args + pcode->argCount;
    arg = pcode->args;
    while (*format) {
        if (arg >= lastArg) {
            CError_FATAL(192);
        }

        if (*format == ',' || *format == ';') {
            format++;
        }
        if (*format == '[' || *format == '(') {
            format++;
        }

        effect = EffectRead;
        if (*format == '=') {
            effect = EffectWrite;
            format++;
        } else if (*format == '+') {
            effect = EffectRead | EffectWrite;
            format++;
        }

        if (*format == '^') {
            effect |= EffectPS1;
            format++;
        } else if (*format == ':') {
            effect |= EffectPS0 | EffectPS1;
            format++;
        } else if (*format == '.') {
            effect |= EffectPS0;
            format++;
        } else if (*format == '`') {
            effect |= EffectSingle;
            format++;
        }

        if (*format == '-') {
            format++;
        }
        if (*format == '?') {
            format++;
        }
        if (*format == '!') {
            format++;
        }

        switch (*format) {
            case 'b':
                tmp = va_arg(argList, int);
                if (!tmp) {
                    effect = 0;
                }
                arg->kind = PCOp_REGISTER;
                arg->arg = RegClass_GPR;
                arg->data.reg.reg = tmp;
                arg->data.reg.effect = effect;
                break;
            case 'r':
                arg->kind = PCOp_REGISTER;
                arg->arg = RegClass_GPR;
                arg->data.reg.reg = va_arg(argList, int);
                arg->data.reg.effect = effect;
                break;
            case 'f':
                arg->kind = PCOp_REGISTER;
                arg->arg = RegClass_FPR;
                arg->data.reg.reg = va_arg(argList, int);
                arg->data.reg.effect = effect;
                break;
            case 'v':
                arg->kind = PCOp_REGISTER;
                arg->arg = RegClass_VR;
                arg->data.reg.reg = va_arg(argList, int);
                arg->data.reg.effect = effect;
                break;
            case 'c':
                arg->kind = PCOp_REGISTER;
                arg->arg = RegClass_CRFIELD;
                arg->data.reg.reg = va_arg(argList, int);
                arg->data.reg.effect = effect;
                break;
            case 'C':
                arg->kind = PCOp_REGISTER;
                arg->arg = RegClass_SPR;
                arg->data.reg.reg = 2;
                arg->data.reg.effect = effect;
                break;
            case 'L':
                arg->kind = PCOp_REGISTER;
                arg->arg = RegClass_SPR;
                arg->data.reg.reg = 1;
                arg->data.reg.effect = effect;
                break;
            case 'V':
                tmp = nregs;
                while (tmp--) {
                    arg->kind = PCOp_REGISTER;
                    arg->arg = RegClass_GPR;
                    arg->data.reg.reg = 31 - tmp;
                    arg->data.reg.effect = effect;
                    arg++;
                }
                arg--;
                break;
            case 'X':
                arg->kind = PCOp_REGISTER;
                arg->arg = RegClass_SPR;
                arg->data.reg.reg = 0;
                arg->data.reg.effect = effect;
                break;
            case 'Y': {
                int mask;
                if (pcode->op == PC_MTCRF) {
                    mask = pcode->args[0].data.imm.value;
                    for (i = 0; i < 8; i++) {
                        if (mask & (0x80 >> i)) {
                            arg->kind = PCOp_REGISTER;
                            arg->arg = RegClass_CRFIELD;
                            arg->data.reg.reg = i;
                            arg->data.reg.effect = EffectWrite;
                            arg++;
                        }
                    }
                } else {
                    i = 0;
                    while (i < 8) {
                        arg->kind = PCOp_REGISTER;
                        arg->arg = RegClass_CRFIELD;
                        arg->data.reg.reg = i;
                        arg->data.reg.effect = EffectRead;
                        i++;
                        arg++;
                    }
                }
                arg--;
                break;
            }

            case 'Z':
                arg->kind = PCOp_REGISTER;
                arg->arg = RegClass_CRFIELD;
                arg->data.reg.reg = 0;
                arg->data.reg.effect = effect;
                break;
            case 'P':
                if (isdigit(format[1])) {
                    format += pcode_const_from_format(format + 1, &pbits);
                } else {
#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT (-4 + CError_LINE_DELTA)
#endif
                    CError_FATAL(331);
                }
            case 's': {
                int sysreg;
                tmp = va_arg(argList, int);
                foundSysreg = -1;
                for (sysreg = 0; sysreg < 4; sysreg++) {
                    if (tmp == spr_to_sysreg[sysreg]) {
                        foundSysreg = sysreg;
                        arg->kind = PCOp_REGISTER;
                        arg->arg = RegClass_SPR;
                        arg->data.reg.reg = sysreg;
                        arg->data.reg.effect = effect;
                        if ((effect & EffectWrite) && (tmp == 0x100)) {
                            pcsetsideeffects(pcode);
                        }
                        break;
                    }
                }

                if (foundSysreg < 0) {
                    pcsetsideeffects(pcode);
                    arg->kind = PCOp_SYSREG;
                    arg->arg = RegClass_SPR;
                    arg->data.reg.reg = tmp;
                    arg->data.reg.effect = effect;
                }

                break;
            }

            case 'T': {
                int tbr;
                tbr = va_arg(argList, int);
                if (tbr == 0x10C) {
                    arg->kind = PCOp_REGISTER;
                    arg->arg = RegClass_SPR;
                    arg->data.reg.reg = 0x11C;
                    arg->data.reg.effect = effect;
                } else if (tbr == 0x10D) {
                    arg->kind = PCOp_REGISTER;
                    arg->arg = RegClass_SPR;
                    arg->data.reg.reg = 0x11D;
                    arg->data.reg.effect = effect;
                } else {
                    CError_FATAL(365);
                }
                pcsetsideeffects(pcode);
                arg->kind = PCOp_SYSREG;
                arg->arg = RegClass_SPR;
                arg->data.reg.reg = va_arg(argList, int);
                arg->data.reg.effect = effect;
                break;
            }

            case 'S': {
                int sysreg;
                foundSysreg = -1;
                if (isdigit(format[1])) {
                    format += pcode_const_from_format(format + 1, &spr);
                } else {
                    CError_FATAL(383);
                }

                for (sysreg = 0; sysreg < 4; sysreg++) {
                    if (spr == spr_to_sysreg[sysreg]) {
                        foundSysreg = sysreg;
                        arg->kind = PCOp_REGISTER;
                        arg->arg = RegClass_SPR;
                        arg->data.reg.reg = sysreg;
                        arg->data.reg.effect = effect;
                        if ((effect & EffectWrite) && (spr == 0x100)) {
                            pcsetsideeffects(pcode);
                        }
                        break;
                    }
                }

                if (foundSysreg < 0) {
                    pcsetsideeffects(pcode);
                    arg->kind = PCOp_SYSREG;
                    arg->arg = RegClass_SPR;
                    arg->data.reg.reg = va_arg(argList, int);
                    arg->data.reg.effect = effect;
                }
                break;
            }

            case 'a':
            case 'i':
            case 'n':
            case 'u':
            case 'w':
            case 'x':
                c = *format;
                bits = 16;
                if (*format == 'a') {
                    if (isdigit(format[1])) {
                        c = format[1];
                        format++;
                    } else {
                        CError_FATAL(420);
                    }
                }
                if (isdigit(format[1])) {
                    format += pcode_const_from_format(format + 1, &bits);
                }
                arg->kind = PCOp_IMMEDIATE;
                arg->data.imm.value = va_arg(argList, int);
                arg->data.imm.obj = NULL;
                if (pcode_check_imm_bits(arg->data.imm.value, bits, c)) {
                    CError_FATAL(431);
                }
                break;

            case 'N':
                arg->kind = PCOp_IMMEDIATE;
                arg->data.imm.value = va_arg(argList, int);
                arg->data.imm.obj = NULL;
                if (pcode_check_imm_bits(arg->data.imm.value, 6, 'u')) {
                    CError_FATAL(441);
                }
                break;

            case 'D':
                arg->kind = PCOp_IMMEDIATE;
                arg->data.imm.value = va_arg(argList, int);
                arg->data.imm.obj = NULL;
                if (pcode_check_imm_bits(arg->data.imm.value, 10, 'u')) {
                    CError_FATAL(450);
                }
                break;

            case 'B':
            case 't':
                arg->kind = PCOp_IMMEDIATE;
                arg->data.imm.value = va_arg(argList, int);
                arg->data.imm.obj = NULL;
                if (pcode_check_imm_bits(arg->data.imm.value, 5, 'u')) {
                    CError_FATAL(460);
                }
                break;

            case 'Q':
                arg->kind = PCOp_REGISTER;
                arg->arg = RegClass_CRFIELD;
                arg->data.reg.reg = va_arg(argList, int);
                arg->data.reg.effect = effect;
                arg++;
            case 'q':
                arg->kind = PCOp_IMMEDIATE;
                arg->data.imm.value = va_arg(argList, int);
                arg->data.imm.obj = NULL;
                if (pcode_check_imm_bits(arg->data.imm.value, 4, 'u')) {
                    CError_FATAL(475);
                }
                break;

            case 'l':
                if ((label = va_arg(argList, PCodeLabel*))) {
                    arg->kind = PCOp_LABEL;
                    arg->data.label.label = label;
                } else {
                    arg->kind = PCOp_MEMORY;
                    obj = va_arg(argList, Object*);
                    CError_ASSERT(488, obj->otype == OT_OBJECT);
                    arg->data.mem.obj = obj;
                    arg->data.mem.offset = 0;
                    arg->arg = (RegClass)RefType_Branch;
                }
                break;

            case 'd': {
                int baseEffect;
                CError_ASSERT(502, format[1] == '(');
                format += 2;
                baseEffect = EffectRead;
                if (*format == '=') {
                    baseEffect = EffectWrite;
                    format++;
                } else if (*format == '+') {
                    baseEffect = EffectRead | EffectWrite;
                    format++;
                }

                CError_ASSERT(514, format[0] == 'b');

                tmp = va_arg(argList, int);
                if (tmp == 0) {
                    baseEffect = 0;
                }
                arg->kind = PCOp_REGISTER;
                arg->arg = RegClass_GPR;
                arg->data.reg.reg = tmp;
                arg->data.reg.effect = baseEffect;
                arg++;
            }

            case 'm':
                obj = va_arg(argList, Object*);
                if (obj) {
                    CError_ASSERT(527, obj->otype == OT_OBJECT);

                    if (obj->datatype == DABSOLUTE) {
                        arg->kind = PCOp_IMMEDIATE;
                        arg->data.imm.obj = obj;
                        arg->data.imm.value = va_arg(argList, SInt32);
                    } else {
                        arg->kind = PCOp_MEMORY;
                        arg->data.mem.obj = obj;
                        arg->data.mem.offset = va_arg(argList, SInt32);

                        if (pcode->flags & (fIsRead | fIsWrite | fOpFromMemory | fOpToMemory)) {
                            pcode->alias = make_alias(
                                obj, arg->data.mem.offset, nbytes_loaded_or_stored_by(pcode)
                            );
                            if (is_volatile_object(obj)) {
                                pcode->flags |= fIsVolatile;
                            }
                            if (OBJ_GET_TARGET_CONST(obj)) {
                                pcode->flags |= fIsConst;
                            }
                        } else {
                            if (pcode->op == PC_ADDI) {
                                pcode->alias = make_alias(obj, arg->data.mem.offset, 1);
                            }
                        }
                        if (pcode->flags & (fIsRead | fIsWrite)) {
                            if (OBJ_GET_TARGET_VOLATILE(obj)) {
                                pcode->flags |= fIsVolatile;
                            }
                            if (OBJ_GET_TARGET_CONST(obj)) {
                                pcode->flags |= fIsConst;
                            }
                        }

                        if (pcode->flags & (fIsBranch | fIsCall)) {
                            arg->arg = (RegClass)RefType_Branch;
                        } else if (obj->datatype == DLOCAL) {
                            if (!local_is_16bit_offset(obj)) {
                                arg->arg = (RegClass)RefType_LocalLo;
                            } else {
                                arg->arg = (RegClass)RefType_Local16;
                            }
                        } else if (is_16bitdata(obj)) {
                            arg->arg = (RegClass)RefType_Addr16;
                        } else if (is_pooled_object(obj)) {
                            arg->arg = (RegClass)RefType_Pooled;
                        } else {
                            arg->arg = (RegClass)RefType_Lo;
                        }
                    }
                } else {
                    arg->kind = PCOp_IMMEDIATE;
                    arg->data.imm.value = va_arg(argList, SInt32);
                    arg->data.imm.obj = NULL;
                    if (pcode->flags & (fIsRead | fIsWrite)) {
                        pcode->flags |= fIsPtrOp;
                    }
                }
                break;

            case 'M':
                obj = va_arg(argList, Object*);
                if (obj) {
                    CError_ASSERT(592, obj->otype == OT_OBJECT);

                    if (obj->datatype == DABSOLUTE) {
                        arg->kind = PCOp_IMMEDIATE;
                        arg->data.imm.obj = obj;
                        arg->data.imm.value = va_arg(argList, SInt32);
                    } else {
                        arg->kind = PCOp_MEMORY;
                        arg->data.mem.obj = obj;
                        arg->data.mem.offset = va_arg(argList, SInt32);

                        if (pcode->flags & (fIsRead | fIsWrite)) {
                            if (OBJ_GET_TARGET_VOLATILE(obj)) {
                                pcode->flags |= fIsVolatile;
                            }
                            if (OBJ_GET_TARGET_CONST(obj)) {
                                pcode->flags |= fIsConst;
                            }
                        }

                        if (obj->datatype == DLOCAL) {
                            arg->arg = (RegClass)RefType_LocalHa;
                        } else {
                            arg->arg = (RegClass)RefType_Ha;
                        }
                    }
                } else {
                    arg->kind = PCOp_IMMEDIATE;
                    arg->data.imm.value = va_arg(argList, SInt32);
                    arg->data.imm.obj = NULL;
                    if (pcode->flags & (fIsRead | fIsWrite)) {
                        pcode->flags |= fIsPtrOp;
                    }
                }
                break;

            case 'p':
                arg->kind = PCOp_PLACEHOLDEROPERAND;
                break;

            case 'O':
                arg--;
                break;

            default:
                CError_FATAL(643);
        }

        while (format[1] && strchr("@/<>|*", format[1])) {
            switch (*(++format)) {
                case '/':
                    if (format[1] == '2') {
                        format++;
                    }
                    break;
                case '<':
                case '*':
                case '|':
                case '>':
                case '@':
                    if (format[1] == 'p') {
                        format++;
                    } else if (isdigit(format[1])) {
                        format += pcode_const_from_format(format + 1, &suffix);
                    } else {
                        CError_FATAL(674);
                    }
                    break;
            }
        }

        if ((c = *(++format)) == ']' || c == ')') {
            format++;
        }
        arg++;
    }

    while (arg < lastArg) {
        arg->kind = PCOp_PLACEHOLDEROPERAND;
        arg++;
    }
    while (extra) {
        arg->kind = PCOp_PLACEHOLDEROPERAND;
        arg++;
        extra--;
    }
    return pcode;
}

extern char* orig_buf;
extern int do_show_basic_blocks;

int expectandformatoperand(
    PCodeArg* operand, PCOpKind expectedKind, char kind, int bitCount, char* buf
) {
    int errorlen;
    char* name;
    int len;
    int regclass;
    int refis1;
    int b_null;

    errorlen = 0;

    if (operand->kind != expectedKind) {
        char* kindstr;
        switch (expectedKind) {
            case PCOp_REGISTER:
                kindstr = "REGISTER";
                break;
            case PCOp_SYSREG:
                kindstr = "SYSREG";
                break;
            case PCOp_IMMEDIATE:
                kindstr = "IMMEDIATE";
                break;
            case PCOp_MEMORY:
                kindstr = "MEMORY";
                break;
            case PCOp_LABEL:
                kindstr = "LABEL";
                break;
            case PCOp_LABELDIFF:
                kindstr = "LABELDIFF";
                break;
            case PCOp_PLACEHOLDEROPERAND:
                kindstr = "PLACEHOLDEROPERAND";
                break;
            default:
                kindstr = "unknown kind";
        }
        len = sprintf(buf, "{EXPECTED %s}", kindstr);
        errorlen += len;
        buf += len;
        pclist_bad_operand = 1;
    }

    switch (operand->kind) {
        case PCOp_REGISTER:
            if (operand->arg != (regclass = kind)) {
                len = sprintf(buf, "{EXPECTED %s}", register_class_name[regclass]);
                errorlen += len;
                buf += len;
            }
            if (operand->data.reg.reg < n_real_registers[regclass] &&
                (name = special_register_names[operand->arg][operand->data.reg.reg])) {
                len = sprintf(buf, "%s", name);
            } else {
                len = sprintf(buf, register_class_format[operand->arg], operand->data.reg.reg);
            }
            errorlen += len;
            break;
        case PCOp_SYSREG:
            if (operand->arg != RegClass_SPR) {
                len = sprintf(buf, "{EXPECTED %s}", register_class_name[RegClass_SPR]);
                errorlen += len;
                buf += len;
            }
            len = sprintf(buf, register_class_format[RegClass_SPR], operand->data.reg.reg);
            errorlen += len;
            break;
        case PCOp_IMMEDIATE:
            switch (kind) {
                case 'x':
                    len = sprintf(buf, "0x%x", (unsigned int)operand->data.imm.value);
                    break;
                case 'u':
                    len = sprintf(buf, "%u", (unsigned int)operand->data.imm.value);
                    break;
                default:
                    len = sprintf(buf, "%ld", operand->data.imm.value);
                    break;
            }
            errorlen += len;
            buf += len;
            if (operand->data.imm.obj) {
                name = CMangler_GetLinkName(operand->data.imm.obj)->name;
                if (strlen(name) > 50) {
                    len = sprintf(buf, "{%45.45s...}", name);
                } else {
                    len = sprintf(buf, "{%s}", name);
                }
                errorlen += len;
                buf += len;
            }
            if (pcode_check_imm_bits(operand->data.imm.value, bitCount, (char)kind)) {
                errorlen += sprintf(buf, "{IMM too large %i bits}", bitCount);
                pclist_bad_operand = 1;
            }
            break;
        case PCOp_MEMORY:
            switch ((unsigned char)operand->arg) {
                case RefType_None:
                case RefType_Local16:
                case RefType_Addr16:
                case RefType_Lo16:
                case RefType_Branch:
                case RefType_Value:
                case RefType_AbsBranch:
                    break;
                case RefType_Ha:
                case RefType_HaAlt:
                case RefType_LocalHa:
                    len = sprintf(buf, "HA(");
                    errorlen += len;
                    buf += len;
                    break;
                case RefType_Hi:
                    len = sprintf(buf, "HI(");
                    errorlen += len;
                    buf += len;
                    break;
                case RefType_Lo:
                case RefType_LoAlt:
                case RefType_LocalLo:
                    len = sprintf(buf, "LO(");
                    errorlen += len;
                    buf += len;
                    break;
                default:
                    len = sprintf(buf, "{UNEXPECTED reftype = %d}", (unsigned char)operand->arg);
                    errorlen += len;
                    buf += len;
            }
            name = CMangler_GetLinkName(operand->data.mem.obj)->name;
            if (strlen(name) == 0 || strlen(name) > 3200 || name[0] < 0) {
                CError_FATAL(849);
            }
            if (strlen(name) > 50) {
                len = sprintf(buf, "%45.45s...", name);
            } else {
                len = sprintf(buf, "%s", name);
            }
            errorlen += len;
            buf += len;
            if (operand->data.mem.offset > 0) {
                len = sprintf(buf, "+%d", (int)operand->data.mem.offset);
            } else if (operand->data.mem.offset != 0) {
                len = sprintf(buf, "-%d", (int)-operand->data.mem.offset);
            } else {
                len = 0;
            }
            errorlen += len;
            buf += len;
            switch ((unsigned char)operand->arg) {
                case RefType_Lo:
                case RefType_Hi:
                case RefType_Ha:
                case RefType_LoAlt:
                case RefType_HaAlt:
                case RefType_LocalHa:
                case RefType_LocalLo:
                    errorlen += sprintf(buf, ")");
            }
            break;
        case PCOp_LABEL:
            if (do_show_basic_blocks) {
                if (!operand->data.label.label->block) {
                    len = sprintf(buf, "B<unknown>");
                } else {
                    len = sprintf(buf, "B%ld", (long)operand->data.label.label->block->blockIndex);
                }
            } else {
                len = sprintf(
                    buf, "%.8lX", (unsigned long)operand->data.label.label->block->codeOffset
                );
            }
            errorlen += len;
            break;
        case PCOp_LABELDIFF:
            refis1 = ((unsigned char)operand->arg == RefType_Local16);
            b_null = !operand->data.labeldiff.labelB->block;
            if (operand->data.labeldiff.labelA->block == NULL) {
                if (b_null) {
                    len = sprintf(
                        buf,
                        "%sB<unknown>-B<unknown>+%ld",
                        refis1 ? "-" : "",
                        (long)operand->data.labeldiff.offset
                    );
                } else {
                    len = sprintf(
                        buf,
                        "%sB<unknown>-B%ld+%ld",
                        refis1 ? "-" : "",
                        (long)operand->data.labeldiff.labelB->block->blockIndex,
                        (long)operand->data.labeldiff.offset
                    );
                }
            } else {
                if (b_null) {
                    len = sprintf(
                        buf,
                        "%sB%ld-B<unknown>+%ld",
                        refis1 ? "-" : "",
                        (long)operand->data.labeldiff.labelA->block->blockIndex,
                        (long)operand->data.labeldiff.offset
                    );
                } else {
                    len = sprintf(
                        buf,
                        "%sB%ld-B%ld+%ld",
                        refis1 ? "-" : "",
                        (long)operand->data.labeldiff.labelA->block->blockIndex,
                        (long)operand->data.labeldiff.labelB->block->blockIndex,
                        (long)operand->data.labeldiff.offset
                    );
                }
            }
            errorlen += len;
            break;
        case PCOp_PLACEHOLDEROPERAND:
            errorlen += sprintf(buf, "{placeholder}");
            break;
        default:
            errorlen += sprintf(buf, "{UNEXPECTED kind = %d}", operand->kind);
    }

    return errorlen;
}

int formatoperand(PCodeArg* operand, char* buf) {
    return expectandformatoperand(
        operand, operand->kind, (operand->kind == PCOp_REGISTER) ? operand->arg : 'x', -1, buf
    );
}

void formatoperands(PCode* pcode, char* buf, int showBasicBlocks) {
    const char* format;
    PCodeArg* pa;
    int arg_index;
    char* name;
    int i;
    int code;
    int hasConstant;
    SInt32 pbits;
    SInt32 spr;
    SInt32 suffix;
    char c;
    int flagSetT;
    int flagSetF;

    static char* cc[] = {"lt", "gt", "eq", "un"};
    static char* to[] = {"", "lgt", "llt", "", "eq", "lge", "lle", "", "gt", "",   "",
                         "", "ge",  "",    "", "",   "lt",  "",    "", "",   "le", "",
                         "", "",    "ne",  "", "",   "",    "",    "", ""};

    format = opcodeinfo[pcode->op].format;
    orig_buf = buf;
    do_show_basic_blocks = showBasicBlocks;

    if (format[0] == '#') {
        format++;
        if (format[0] == ',') {
            format++;
        }
    }

    arg_index = 0;
    pa = pcode->args;
    while (*format) {
        if (*format == ';') {
            break;
        }
        if (arg_index == pcode->argCount) {
            pclist_bad_operand = 1;
            buf += sprintf(buf, "{EXCEDED noperands, remaning format = %s}", format);
            break;
        }

        if (*format == ',') {
            *(buf++) = ',';
            format++;
        }

        if (*format == '[' || *format == '(') {
            *(buf++) = *format;
            format++;
        }

        if (*format == '=' || *format == '+') {
            format++;
        }
        if (*format == '-') {
            format++;
        }

        if (*format == '?') {
            format++;
            if (*format != 'c') {
                *(buf++) = ',';
            }
        }

        if (*format == '!') {
            format++;
        }

        switch (*format) {
            case 'b':
                if (pa->kind == PCOp_REGISTER && pa->arg == RegClass_GPR && pa->data.reg.reg == 0) {
                    if (pa->data.reg.effect & EffectWrite) {
                        pclist_bad_operand = 1;
                        buf += sprintf(buf, "!!!r");
                    }
                    buf += sprintf(buf, "0");
                    break;
                }
            case 'r':
                buf += expectandformatoperand(pa, PCOp_REGISTER, RegClass_GPR, -1, buf);
                break;
            case 'f':
                buf += expectandformatoperand(pa, PCOp_REGISTER, RegClass_FPR, -1, buf);
                break;
            case 'v':
                buf += expectandformatoperand(pa, PCOp_REGISTER, RegClass_VR, -1, buf);
                break;
            case 'c':
                buf += expectandformatoperand(pa, PCOp_REGISTER, RegClass_CRFIELD, -1, buf);
                break;
            case 'X':
                CError_ASSERT(1124, pa->data.reg.reg == 0);
                buf += expectandformatoperand(pa, PCOp_REGISTER, RegClass_SPR, -1, buf);
                break;
            case 'C':
                CError_ASSERT(1129, pa->data.reg.reg == 2);
                buf += expectandformatoperand(pa, PCOp_REGISTER, RegClass_SPR, -1, buf);
                break;
            case 'L':
                CError_ASSERT(1134, pa->data.reg.reg == 1);
                buf += expectandformatoperand(pa, PCOp_REGISTER, RegClass_SPR, -1, buf);
                break;
            case 'Z':
                CError_ASSERT(1139, pa->data.reg.reg == 0);
                buf += expectandformatoperand(pa, PCOp_REGISTER, RegClass_SPR, -1, buf);
                break;
            case 'P':
                if (isdigit(format[1])) {
                    format += pcode_const_from_format(format + 1, &pbits);
                } else {
                    CError_FATAL(1149);
                }
            case 'S':
            case 'T':
            case 's':
                if (pa->kind == PCOp_REGISTER && pa->arg == RegClass_SPR) {
                    buf += expectandformatoperand(pa, PCOp_REGISTER, RegClass_SPR, -1, buf);
                } else {
                    for (i = 0; i < 4; i++) {
                        if (pa->data.reg.reg == spr_to_sysreg[i]) {
                            CError_FATAL(1161);
                            break;
                        }
                    }

                    buf += expectandformatoperand(pa, PCOp_SYSREG, RegClass_SPR, -1, buf);
                }
                break;

            case 'V':
                do {
                    buf += expectandformatoperand(pa, PCOp_REGISTER, RegClass_GPR, -1, buf);
                    *(buf++) = ',';
                    pa++;
                    arg_index++;
                } while (arg_index < pcode->argCount &&
                         pa->kind == PCOp_REGISTER &&
                         pa->arg == RegClass_GPR);
                buf--;
                pa--;
                arg_index--;
                break;

            case 'Y':
                do {
                    buf += expectandformatoperand(pa, PCOp_REGISTER, RegClass_CRFIELD, -1, buf);
                    *(buf++) = ',';
                    pa++;
                    arg_index++;
                } while (arg_index < pcode->argCount &&
                         pa->kind == PCOp_REGISTER &&
                         pa->arg == RegClass_CRFIELD);
                buf--;
                pa--;
                arg_index--;
                break;

            case 'Q':
                buf += expectandformatoperand(pa, PCOp_REGISTER, RegClass_CRFIELD, -1, buf);
                *(buf++) = ',';
                pa++;
                arg_index++;
            case 'q':
                if (pa->kind == PCOp_IMMEDIATE &&
                    pa->data.imm.value >= 0 &&
                    pa->data.imm.value < 4 &&
                    (name = cc[pa->data.imm.value])[0]) {
                    buf += sprintf(buf, "%s", name);
                } else {
                    buf += sprintf(buf, "{OUT OF RANGE}");
                    buf += expectandformatoperand(pa, PCOp_IMMEDIATE, 0, -1, buf);
                }
                break;
            case 'a':
            case 'i':
            case 'u':
            case 'x':
                code = *format;
                if (code == 'a') {
                    if (isdigit(format[1])) {
                        format++;
                        code = *format;
                    } else {
                        CError_FATAL(1227);
                    }
                }
                if ((hasConstant = isdigit(format[1]))) {
                    format += pcode_const_from_format(format + 1, &spr);
                }
                if (!hasConstant) {
                    spr = -1;
                }
                buf += expectandformatoperand(pa, PCOp_IMMEDIATE, code, spr, buf);
                break;

            case 'N':
                buf += expectandformatoperand(pa, PCOp_IMMEDIATE, 'u', 6, buf);
                break;

            case 'D':
                buf += expectandformatoperand(pa, PCOp_IMMEDIATE, 'u', 10, buf);
                break;

            case 't':
                if (pa->kind == PCOp_IMMEDIATE &&
                    pa->data.imm.value > 0 &&
                    pa->data.imm.value < 31 &&
                    (name = to[pa->data.imm.value])[0]) {
                    buf += sprintf(buf, "%s", name);
                    break;
                }

            case 'B':
                buf += expectandformatoperand(pa, PCOp_IMMEDIATE, 'x', 5, buf);
                break;

            case 'l':
                if (pa->kind == PCOp_IMMEDIATE) {
                    buf += sprintf(
                        buf, "*%s%ld", (pa->data.imm.value >= 0) ? "+" : "", pa->data.imm.value
                    );
                } else if (pa->kind == PCOp_LABELDIFF) {
                    buf += expectandformatoperand(pa, PCOp_LABELDIFF, 0, -1, buf);
                } else if (pa->kind == PCOp_LABEL) {
                    buf += expectandformatoperand(pa, PCOp_LABEL, 0, -1, buf);
                } else {
                    buf += expectandformatoperand(pa, PCOp_MEMORY, 0, -1, buf);
                }
                break;

            case 'd':
                if (pa[1].kind == PCOp_MEMORY) {
                    buf += expectandformatoperand(pa + 1, PCOp_MEMORY, 0, -1, buf);
                } else {
                    buf += expectandformatoperand(pa + 1, PCOp_IMMEDIATE, 0, -1, buf);
                }
                CError_ASSERT(1283, format[1] == '(');
                format++;
                *(buf++) = *(format++);
                if (*format == '+') {
                    format++;
                }
                CError_ASSERT(1291, format[0] == 'b');
                if (pa->kind == PCOp_REGISTER && pa->arg == RegClass_GPR && pa->data.reg.reg == 0) {
                    if (pa->data.reg.effect & (EffectRead | EffectWrite)) {
                        pclist_bad_operand = 1;
                        buf += sprintf(buf, "!!!r");
                    }
                    buf += sprintf(buf, "0");
                } else {
                    buf += expectandformatoperand(pa, PCOp_REGISTER, RegClass_GPR, -1, buf);
                }
                pa++;
                i++;
                break;

            case 'w':
                if (pa->kind == PCOp_LABELDIFF) {
                    buf += expectandformatoperand(pa, PCOp_LABELDIFF, 0, -1, buf);
                } else {
                    buf += expectandformatoperand(pa, PCOp_IMMEDIATE, 0, -1, buf);
                }
                break;

            case 'm':
            case 'n':
                if (pa->kind == PCOp_MEMORY) {
                    buf += expectandformatoperand(pa, PCOp_MEMORY, 0, -1, buf);
                } else if (pa->kind == PCOp_LABELDIFF) {
                    buf += expectandformatoperand(pa, PCOp_LABELDIFF, 0, -1, buf);
                } else if ((pcode->flags & (fIsBranch | fIsCall)) && (pa->kind == PCOp_LABEL)) {
                    buf += expectandformatoperand(pa, PCOp_LABEL, 0, -1, buf);
                } else {
                    buf += expectandformatoperand(pa, PCOp_IMMEDIATE, 0, -1, buf);
                }
                break;

            case 'M':
                if (pa->kind == PCOp_MEMORY) {
                    CError_ASSERT(
                        1335,
                        (int)pa->arg == RefType_Ha ||
                            (int)pa->arg == RefType_HaAlt ||
                            (int)pa->arg == RefType_LocalHa
                    );
                    buf += expectandformatoperand(pa, PCOp_MEMORY, 0, -1, buf);
                } else if (pa->kind == PCOp_LABELDIFF) {
                    buf += expectandformatoperand(pa, PCOp_LABELDIFF, 0, -1, buf);
                } else {
                    buf += expectandformatoperand(pa, PCOp_IMMEDIATE, 0, -1, buf);
                }
                break;

            case 'O':
                pa--;
                arg_index--;
                break;

            case 'p':
                buf += expectandformatoperand(pa, PCOp_PLACEHOLDEROPERAND, 0, -1, buf);
                break;

            default:
                CError_FATAL(1465);
        }

        while (format[1] && strchr("@/<>|*", format[1])) {
            switch (*(++format)) {
                case '/':
                    if (format[1] == '2') {
                        format++;
                    }
                    break;
                case '<':
                case '*':
                case '|':
                case '>':
                case '@':
                    if (format[1] == 'p') {
                        format++;
                    } else if (isdigit(format[1])) {
                        format += pcode_const_from_format(format + 1, &suffix);
                    } else {
                        CError_FATAL(1495);
                    }
                    break;
            }
        }

        if ((c = *(++format)) == ']' || c == ')') {
            *(buf++) = c;
            format++;
        }

        pa++;
        arg_index++;
    }

    if (buf[-1] == ',') {
        buf--;
    }

    flagSetT = PCODE_FLAG_SET_T(pcode);
    flagSetF = PCODE_FLAG_SET_F(pcode);
    if (pcode->flags & fIsConst) {
        buf += sprintf(buf, "; fIsConst");
    }
    if (pcode->flags & fIsVolatile) {
        buf += sprintf(buf, "; fIsVolatile");
    }
    if (pcode->flags & fSideEffects) {
        buf += sprintf(buf, "; fSideEffects");
    }
    if (pcode->flags & fIsCSE) {
        buf += sprintf(buf, "; fIsCSE");
    }
    if (pcode->flags & fCommutative) {
        buf += sprintf(buf, "; fCommutative");
    }

    if (flagSetF & fIsPtrOp) {
        buf += sprintf(buf, "; fIsPtrOp");
    }

    if (flagSetT) {
        if (flagSetT & fLink) {
            buf += sprintf(buf, "; fLink");
        }
        if (flagSetT & fAbsolute) {
            buf += sprintf(buf, "; fAbsolute");
        }
        if (flagSetT & fBranchTaken) {
            buf += sprintf(buf, "; fBranchTaken");
        }
        if (flagSetT & fBranchNotTaken) {
            buf += sprintf(buf, "; fBranchNotTaken");
        }
    } else if (flagSetF) {
        if (flagSetF & fSetsCarry) {
            buf += sprintf(buf, "; fSetsCarry");
        }
        if (flagSetF & fOverflow) {
            buf += sprintf(buf, "; fOverflow");
        }
    }

    *buf = 0;
}

#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT (-8 + CError_LINE_DELTA)
#endif
PCode* makecopyinstruction(PCodeArg* src, PCodeArg* dst) {
    if (dst->kind == PCOp_REGISTER) {
        switch (dst->arg) {
            case RegClass_GPR:
                return makepcode(PC_MR, dst->data.reg.reg, src->data.reg.reg);
            case RegClass_FPR:
                if ((src->data.reg.effect & EffectPS1) ||
                    (src->data.reg.effect & EffectPS0) ||
                    (src->data.reg.effect & (EffectPS0 | EffectPS1))) {
                    return makepcode(PC_PS_MR, dst->data.reg.reg, src->data.reg.reg);
                }
                return makepcode(PC_FMR, dst->data.reg.reg, src->data.reg.reg);
            case RegClass_VR:
                return makepcode(PC_VMR, dst->data.reg.reg, src->data.reg.reg);
            case RegClass_CRFIELD:
                return makepcode(PC_MCRF, dst->data.reg.reg, src->data.reg.reg);
            default:
                break;
        }
    }

    CError_FATAL(1647);
    return NULL;
}

int is_location_independent(PCode* pcode) {
    int reg;

    switch (pcode->op) {
        case PC_LI:
        case PC_LIS:
        case PC_VSPLTISB:
        case PC_VSPLTISH:
        case PC_VSPLTISW:
            return TRUE;
        case PC_ADDI:
        case PC_ADDIS:
        case PC_ORI:
        case PC_ORIS:
            reg = pcode->args[1].data.reg.reg;
            if (reg == 13 || reg == 2) {
                return TRUE;
            }
            return reg == _FP_;
        case PC_LWZ:
            if (pcode->args[1].data.reg.reg == 1 &&
                pcode->args[2].kind == PCOp_IMMEDIATE &&
                pcode->args[2].data.imm.value == 0) {
                return TRUE;
            }
    }

    return FALSE;
}

int can_reuse_stored_value(PCode* store, PCode* load) {
    switch (load->op) {
        case PC_LBZ:
        case PC_LBZU:
        case PC_LBZX:
        case PC_LBZUX:
            switch (store->op) {
                case PC_STB:
                case PC_STBU:
                case PC_STBX:
                case PC_STBUX:
                    return 2;
            }
            break;
        case PC_LHZ:
        case PC_LHZU:
        case PC_LHZX:
        case PC_LHZUX:
            switch (store->op) {
                case PC_STH:
                case PC_STHU:
                case PC_STHX:
                case PC_STHUX:
                    return 3;
            }
            break;
        case PC_LHA:
        case PC_LHAU:
        case PC_LHAX:
        case PC_LHAUX:
            switch (store->op) {
                case PC_STH:
                case PC_STHU:
                case PC_STHX:
                case PC_STHUX:
                    return 4;
            }
            break;
        case PC_LWZ:
        case PC_LWZU:
        case PC_LWZX:
        case PC_LWZUX:
            switch (store->op) {
                case PC_STW:
                case PC_STWU:
                case PC_STWX:
                case PC_STWUX:
                    return 1;
            }
            break;
        case PC_LFS:
        case PC_LFSU:
        case PC_LFSX:
        case PC_LFSUX:
            switch (store->op) {
                case PC_STFS:
                case PC_STFSU:
                case PC_STFSX:
                case PC_STFSUX:
                    return 5;
            }
            break;
        case PC_LFD:
        case PC_LFDU:
        case PC_LFDX:
        case PC_LFDUX:
            switch (store->op) {
                case PC_STFD:
                case PC_STFDU:
                case PC_STFDX:
                case PC_STFDUX:
                    return 1;
            }
            break;
        case PC_LVX:
            if (store->op == PC_STVX) {
                return 1;
            }
            break;
        case PC_PSQ_L:
        case PC_PSQ_LU:
        case PC_PSQ_LX:
        case PC_PSQ_LUX:
            switch (store->op) {
                case PC_PSQ_ST:
                case PC_PSQ_STX:
#if VERSION < VERSION_GC_2_0
                    if (store->args[4].data.reg.reg == SPR_GQR0 &&
                        load->args[4].data.reg.reg == SPR_GQR0) {
#else
                    if (store->args[4].data.reg.reg == SPR_GQR0 &&
                        load->args[4].data.reg.reg == SPR_GQR0 &&
                        store->args[3].data.imm.value == load->args[3].data.imm.value) {
#endif
                        return 1;
                    }
            }
            break;
    }

    return 0;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -3
#endif
PCode* makecopyforload(int kind, PCodeArg* src, PCodeArg* dst) {
    switch (kind) {
        case 1:
            return makecopyinstruction(src, dst);
        case 2:
            return makepcode(PC_RLWINM, dst->data.reg.reg, src->data.reg.reg, 0, 24, 31);
        case 3:
            return makepcode(PC_RLWINM, dst->data.reg.reg, src->data.reg.reg, 0, 16, 31);
        case 4:
            return makepcode(PC_EXTSH, dst->data.reg.reg, src->data.reg.reg);
        case 5:
            return makepcode(PC_FRSP, dst->data.reg.reg, src->data.reg.reg);
        default:
            CError_FATAL(1933);
            return NULL;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -17
#endif
int nbytes_loaded_or_stored_by(PCode* pcode) {
    OpcodeInfo* oinfo = opcodeinfo + pcode->op;
    if (oinfo->flags & (fIsRead | fIsWrite)) {
        switch (pcode->op) {
            case PC_LBZ:
            case PC_LBZU:
            case PC_LBZX:
            case PC_LBZUX:
            case PC_STB:
            case PC_STBU:
            case PC_STBX:
            case PC_STBUX:
                return 1;
            case PC_LHZ:
            case PC_LHZU:
            case PC_LHZX:
            case PC_LHZUX:
            case PC_LHA:
            case PC_LHAU:
            case PC_LHAX:
            case PC_LHAUX:
            case PC_LHBRX:
            case PC_STH:
            case PC_STHU:
            case PC_STHX:
            case PC_STHUX:
            case PC_STHBRX:
                return 2;
            case PC_LWZ:
            case PC_LWZU:
            case PC_LWZX:
            case PC_LWZUX:
            case PC_LWBRX:
            case PC_STW:
            case PC_STWU:
            case PC_STWX:
            case PC_STWUX:
            case PC_STWBRX:
            case PC_LFS:
            case PC_LFSU:
            case PC_LFSX:
            case PC_LFSUX:
            case PC_STFS:
            case PC_STFSU:
            case PC_STFSX:
            case PC_STFSUX:
            case PC_LWARX:
            case PC_STFIWX:
            case PC_STWCX:
            case PC_ECIWX:
            case PC_ECOWX:
            case PC_MFROM:
            case PC_LSCBX:
                return 4;
            case PC_LMW:
            case PC_STMW:
                if (pcode->args[0].kind == PCOp_REGISTER && pcode->args[0].arg == RegClass_GPR) {
                    return (32 - pcode->args[0].data.reg.reg) * 4;
                } else {
                    return 128;
                }
            case PC_LFD:
            case PC_LFDU:
            case PC_LFDX:
            case PC_LFDUX:
            case PC_STFD:
            case PC_STFDU:
            case PC_STFDX:
            case PC_STFDUX:
                return 8;
            case PC_LSWI:
            case PC_STSWI:
                return pcode->args[2].data.imm.value;
            case PC_LSWX:
            case PC_STSWX:
                return 128;
            case PC_LVEBX:
            case PC_STVEBX:
                return 1;
            case PC_LVEHX:
            case PC_STVEHX:
                return 2;
            case PC_LVEWX:
            case PC_STVEWX:
                return 4;
            case PC_LVSL:
            case PC_LVSR:
            case PC_LVX:
            case PC_LVXL:
            case PC_STVX:
            case PC_STVXL:
                return 16;
            case PC_PSQ_L:
            case PC_PSQ_LU:
            case PC_PSQ_LX:
            case PC_PSQ_LUX:
            case PC_PSQ_ST:
            case PC_PSQ_STU:
            case PC_PSQ_STX:
            case PC_PSQ_STUX:
#if VERSION < VERSION_GC_2_0
                return 8;
#else
                if (pcode->args[4].data.reg.reg == SPR_GQR0 && pcode->args[3].data.imm.value == 1) {
                    return 4;
                }
                return 8;
#endif
            default:
                CError_FATAL(2135);
        }
    }

    CError_FATAL(2138);
    return 0;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -17
#endif
void change_num_operands(PCode* pcode, int newNum) {
    int i;

    CError_ASSERT(2150, ((pcode->argCount > 5) ? pcode->argCount : 5) >= newNum);

    for (i = pcode->argCount - 1; i >= newNum; i--) {
        pcode->args[i].kind = PCOp_PLACEHOLDEROPERAND;
    }

    pcode->argCount = newNum;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

void change_opcode(PCode* pcode, short opcode) {
    UInt32 flags;

    pcode->flags =
        (pcode->flags & ~(opcodeinfo[pcode->op].flags & ~fIsPtrOp)) | opcodeinfo[opcode].flags;
    flags = pcode->flags;
    if ((flags & fIsMove) && (FLAG_SET_F(flags) & fRecordBit)) {
        pcode->flags = flags & ~fIsMove;
    }
    pcode->op = opcode;
}
