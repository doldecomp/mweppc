#include "compiler/PCodeAssembly.h"
#include "compiler/BE_symbol.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CMangler.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/DWARF.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeListing.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/StackFrame.h"
#include <string.h>

// NOTE: invented names: type, object, addend
// The relocation assemblepcode asks for: its kind in the Mac object writer's MWReloc numbering,
// which ObjGen's emit_relocation maps to an ELF relocation (-1 for none), its object and addend.
// 2-byte packing: the object at offset 2, the addend at 6.
#pragma pack(push, 2)
struct WeirdOperand {
    SInt16 type;
    struct Object* object;
    UInt32 addend;
};
#pragma pack(pop)

static UInt32 codebase;

static SInt32 pcode_update_mem_labeldiff_imm(PCode* instr, const PCodeArg* op, WeirdOperand* wop) {
    SInt32 offset;
    Object* object;
    RegClass arg;

    if (op->kind == PCOp_MEMORY) {
        object = op->data.mem.obj;
        offset = op->data.mem.offset;
        // (a memory operand's RefType is in `arg`, a RegClass field: hence the casts)
        switch (object->datatype) {
            case DLOCAL:
                switch ((UInt8)op->arg) {
                    case RefType_Local16:
                        offset += local_offset_16(object);
                        break;
                    case RefType_LocalLo:
                        offset = local_offset_lo(object, offset);
                        break;
                    case RefType_LocalHa:
                        offset = local_offset_ha(object, offset);
                        break;
                    default:
                        CError_FATAL(82);
                }
                break;
            case DDATA:
                if (instr->flags & (fIsRead | fIsWrite)) {
                    if ((UInt8)op->arg == RefType_LoAlt || NeedsXREF(object)) {
                        switch ((UInt8)op->arg) {
                            case RefType_Addr16:
                            case RefType_Lo16:
                            case RefType_Lo:
                            case RefType_LoAlt:
                                break;
                            default:
                                CError_FATAL(100);
                        }
                    } else {
                        if ((UInt8)op->arg == RefType_Pooled) {
                            offset += GetPooledObjectOffset(object);
                            if (offset != (SInt16)offset) {
                                CError_FATAL(106);
                            }
                        }
                        break;
                    }
                }
                if ((UInt8)op->arg == RefType_Pooled) {
                    offset += GetPooledObjectOffset(object);
                    if (offset != (SInt16)offset) {
                        CError_FATAL(114);
                    }
                    break;
                }
            case DFUNC:
            case DVFUNC:
                switch ((UInt8)op->arg) {
                    case RefType_Lo:
                        wop->type = 5;
                        break;
                    case RefType_Addr16:
                        wop->type = 3;
                        break;
                    case RefType_Lo16:
                        wop->type = 4;
                        break;
                    case RefType_Ha:
                        wop->type = 7;
                        break;
                    case RefType_Hi:
                        wop->type = 6;
                        break;
                    case RefType_LoAlt:
                        wop->type = 14;
                        break;
                    case RefType_HaAlt:
                        wop->type = 15;
                        break;
                    default:
                        CError_FATAL(145);
                }
                wop->object = object;
                wop->addend = offset;
                offset = 0;
                break;
            default:
                CError_FATAL(174);
        }
    } else if (op->kind == PCOp_LABELDIFF) {
        SInt32 diff;

        arg = op->arg;

        diff = op->data.labeldiff.labelA->block->codeOffset -
            op->data.labeldiff.labelB->block->codeOffset;
        diff += op->data.labeldiff.offset;
        if (arg == 1) {
            diff = -diff;
        }

        if (diff > 0x7FFF) {
            PPCError_Error(PPCERR_LABEL_TOO_FAR);
        } else if (diff < -0x8000) {
            PPCError_Error(PPCERR_LABEL_TOO_FAR);
        }
        offset = diff;
    } else if (op->kind == PCOp_IMMEDIATE) {
        offset = op->data.imm.value;
    } else {
        CError_FATAL(203);
    }

    return offset;
}

UInt32 assemblepcode(PCode* instr, UInt32 offset, WeirdOperand* wop) {
    UInt32 bits;
    int isAbsolute; // NOTE: invented name

    bits = opcodeinfo[instr->op].insn;
    wop->type = -1;
    wop->addend = 0;

    switch (instr->op) {
        case PC_BL: {
            isAbsolute = PCODE_FLAG_SET_T(instr) & fAbsolute;
            if (instr->args[0].kind == PCOp_MEMORY) {
                bits |= instr->args[0].data.mem.offset & 0x3FFFFFC;
                wop->type = 2;
                wop->object = instr->args[0].data.mem.obj;
                if (isAbsolute == 0) {
                    wop->type = 2;
                } else {
                    wop->type = 10;
                    bits |= 2;
                }
            } else if (instr->args[0].kind == PCOp_IMMEDIATE) {
                bits |= instr->args[0].data.imm.value & 0x3FFFFFC;
                if (isAbsolute) {
                    bits |= 2;
                }
            } else {
                bits |= (instr->args[0].data.label.label->block->codeOffset - offset) & 0x3FFFFFC;
                if (isAbsolute) {
                    CError_FATAL(271);
                }
            }
            break;
        }

        case PC_B: {
            isAbsolute = PCODE_FLAG_SET_T(instr) & fAbsolute;
            if (instr->args[0].kind == PCOp_MEMORY) {
                bits |= instr->args[0].data.mem.offset & 0x3FFFFFC;
                wop->object = instr->args[0].data.mem.obj;
                if (isAbsolute == 0) {
                    wop->type = 2;
                } else {
                    wop->type = 10;
                    bits |= 2;
                }
            } else if (instr->args[0].kind == PCOp_IMMEDIATE) {
                bits |= instr->args[0].data.imm.value & 0x3FFFFFC;
                if (isAbsolute) {
                    bits |= 2;
                }
            } else {
                bits |= (instr->args[0].data.label.label->block->codeOffset - offset) & 0x3FFFFFC;
                if (isAbsolute) {
                    CError_FATAL(312);
                }
            }
            if (PCODE_FLAG_SET_T(instr) & fLink) {
                bits |= 1;
            }
            break;
        }

        case PC_BDNZ:
        case PC_BDZ: {
            isAbsolute = PCODE_FLAG_SET_T(instr) & fAbsolute;
            if (instr->args[0].kind == PCOp_MEMORY) {
                bits |= instr->args[0].data.mem.offset & 0xFFFC;
                wop->object = instr->args[0].data.mem.obj;
                if (isAbsolute == 0) {
                    wop->type = 8;
                } else {
                    wop->type = 10;
                    bits |= 2;
                }
            } else {
                SInt32 value;
                if (instr->args[0].kind == PCOp_IMMEDIATE) {
                    value = instr->args[0].data.imm.value;
                } else {
                    value = instr->args[0].data.label.label->block->codeOffset - offset;
                }

                bits |= (UInt16)value;
                if (value < 0) {
                    if (PCODE_FLAG_SET_T(instr) & fBranchNotTaken) {
                        bits |= 0x200000u;
                    }
                } else {
                    if (PCODE_FLAG_SET_T(instr) & fBranchTaken) {
                        bits |= 0x200000u;
                    }
                }
            }
            if (PCODE_FLAG_SET_T(instr) & fLink) {
                bits |= 1;
            }
            break;
        }

        case PC_BC: {
            isAbsolute = PCODE_FLAG_SET_T(instr) & fAbsolute;
            bits |= (instr->args[0].data.imm.value & 31) << 21;
            bits |= ((instr->args[1].data.reg.reg * 4 + instr->args[2].data.imm.value) & 31) << 16;

            if (instr->args[3].kind == PCOp_MEMORY) {
                bits |= instr->args[3].data.mem.offset & 0xFFFC;
                wop->object = instr->args[3].data.mem.obj;
                if (isAbsolute == 0) {
                    wop->type = 8;
                } else {
                    wop->type = 10;
                    bits |= 2;
                }
            } else {
                SInt32 value;
                if (instr->args[3].kind == PCOp_IMMEDIATE) {
                    value = instr->args[3].data.imm.value;
                } else {
                    value = instr->args[3].data.label.label->block->codeOffset - offset;
                }

                bits |= (UInt16)value;
                if (value < 0) {
                    if (PCODE_FLAG_SET_T(instr) & fBranchNotTaken) {
                        bits |= 0x200000u;
                    }
                } else {
                    if (PCODE_FLAG_SET_T(instr) & fBranchTaken) {
                        bits |= 0x200000u;
                    }
                }
            }
            if (PCODE_FLAG_SET_T(instr) & fLink) {
                bits |= 1;
            }
            break;
        }

        case PC_BT:
        case PC_BF:
        case PC_BDNZT:
        case PC_BDNZF:
        case PC_BDZT:
        case PC_BDZF: {
            isAbsolute = PCODE_FLAG_SET_T(instr) & fAbsolute;
            bits |= ((instr->args[0].data.reg.reg * 4 + instr->args[1].data.imm.value) & 31) << 16;

            if (instr->args[2].kind == PCOp_MEMORY) {
                bits |= instr->args[2].data.mem.offset & 0xFFFC;
                wop->object = instr->args[2].data.mem.obj;
                if (isAbsolute == 0) {
                    wop->type = 8;
                } else {
                    wop->type = 10;
                    bits |= 2;
                }
            } else {
                SInt32 value;
                if (instr->args[2].kind == PCOp_IMMEDIATE) {
                    value = instr->args[2].data.imm.value;
                    if (isAbsolute) {
                        bits |= 2;
                    }
                } else {
                    value = instr->args[2].data.label.label->block->codeOffset - offset;
                    CError_ASSERT(468, !isAbsolute);
                }

                bits |= (UInt16)value;

                if (value < 0) {
                    if (PCODE_FLAG_SET_T(instr) & fBranchNotTaken) {
                        bits |= 0x200000u;
                    }
                } else {
                    if (PCODE_FLAG_SET_T(instr) & fBranchTaken) {
                        bits |= 0x200000u;
                    }
                }
            }

            if (PCODE_FLAG_SET_T(instr) & fLink) {
                bits |= 1;
            }
            break;
        }

        case PC_BTLR:
        case PC_BTCTR:
        case PC_BFLR:
        case PC_BFCTR:
            bits |= ((instr->args[0].data.reg.reg * 4 + instr->args[1].data.imm.value) & 31) << 16;
            if (PCODE_FLAG_SET_T(instr) & fLink) {
                bits |= 1;
            }
            if (PCODE_FLAG_SET_T(instr) & fBranchTaken) {
                bits |= 0x200000u;
            }
            break;

        case PC_BCLR:
        case PC_BCCTR:
            bits |= instr->args[0].data.imm.value << 21;
            bits |= ((instr->args[1].data.reg.reg * 4 + instr->args[2].data.imm.value) & 31) << 16;
        case PC_BLR:
        case PC_BCTR:
        case PC_BCTRL:
        case PC_BLRL:
            if (PCODE_FLAG_SET_T(instr) & fLink) {
                bits |= 1;
            }
            if (PCODE_FLAG_SET_T(instr) & fBranchTaken) {
                bits |= 0x200000u;
            }
            break;

        case PC_CRAND:
        case PC_CRANDC:
        case PC_CREQV:
        case PC_CRNAND:
        case PC_CRNOR:
        case PC_CROR:
        case PC_CRORC:
        case PC_CRXOR:
            bits |= ((instr->args[0].data.reg.reg * 4 + instr->args[1].data.imm.value) & 31) << 21;
            bits |= ((instr->args[2].data.reg.reg * 4 + instr->args[3].data.imm.value) & 31) << 16;
            bits |= ((instr->args[4].data.reg.reg * 4 + instr->args[5].data.imm.value) & 31) << 11;
            break;

        case PC_MCRF:
            bits |= instr->args[0].data.reg.reg << 23;
            bits |= instr->args[1].data.reg.reg << 18;
            break;

        case PC_LBZ:
        case PC_LBZU:
        case PC_LHZ:
        case PC_LHZU:
        case PC_LHA:
        case PC_LHAU:
        case PC_LWZ:
        case PC_LWZU:
        case PC_LMW:
        case PC_STB:
        case PC_STBU:
        case PC_STH:
        case PC_STHU:
        case PC_STW:
        case PC_STWU:
        case PC_STMW:
        case PC_LFS:
        case PC_LFSU:
        case PC_LFD:
        case PC_LFDU:
        case PC_STFS:
        case PC_STFSU:
        case PC_STFD:
        case PC_STFDU:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= (UInt16)pcode_update_mem_labeldiff_imm(instr, &instr->args[2], wop);
            break;

        case PC_LBZX:
        case PC_LBZUX:
        case PC_LHZX:
        case PC_LHZUX:
        case PC_LHAX:
        case PC_LHAUX:
        case PC_LHBRX:
        case PC_LWZX:
        case PC_LWZUX:
        case PC_LWBRX:
        case PC_STBX:
        case PC_STBUX:
        case PC_STHX:
        case PC_STHUX:
        case PC_STHBRX:
        case PC_STWX:
        case PC_STWUX:
        case PC_STWBRX:
        case PC_LFSX:
        case PC_LFSUX:
        case PC_LFDX:
        case PC_LFDUX:
        case PC_STFSX:
        case PC_STFSUX:
        case PC_STFDX:
        case PC_STFDUX:
        case PC_LWARX:
        case PC_LSWX:
        case PC_STFIWX:
        case PC_STSWX:
        case PC_STWCX:
        case PC_ECIWX:
        case PC_ECOWX:
        case PC_DCREAD:
        case PC_TLBSX:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_DCBF:
        case PC_DCBST:
        case PC_DCBT:
        case PC_DCBTST:
        case PC_DCBZ:
        case PC_DCBI:
        case PC_ICBI:
        case PC_DCCCI:
        case PC_ICBT:
        case PC_ICCCI:
        case PC_ICREAD:
        case PC_DCBA:
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[1].data.reg.reg << 11;
            break;

        case PC_ADD:
        case PC_ADDC:
        case PC_ADDE:
        case PC_DIVW:
        case PC_DIVWU:
        case PC_MULHW:
        case PC_MULHWU:
        case PC_MULLW:
        case PC_SUBF:
        case PC_SUBFC:
        case PC_SUBFE:
            bits |= instr->args[2].data.reg.reg << 11;
        case PC_ADDME:
        case PC_ADDZE:
        case PC_NEG:
        case PC_SUBFME:
        case PC_SUBFZE:
        case PC_MFROM:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            if (PCODE_FLAG_SET_F(instr) & fOverflow) {
                bits |= 0x400;
            }
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_ADDI:
        case PC_ADDIC:
        case PC_ADDICR:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= (UInt16)pcode_update_mem_labeldiff_imm(instr, &instr->args[2], wop);
            break;

        case PC_ADDIS:
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= (UInt16)pcode_update_mem_labeldiff_imm(instr, &instr->args[2], wop);
            break;

        case PC_MULLI:
        case PC_SUBFIC:
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= (UInt16)instr->args[2].data.imm.value;
            break;

        case PC_LI:
        case PC_LIS:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= (UInt16)pcode_update_mem_labeldiff_imm(instr, &instr->args[1], wop);
            break;

        case PC_ANDI:
        case PC_ANDIS:
        case PC_ORI:
        case PC_ORIS:
        case PC_XORI:
        case PC_XORIS:
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[1].data.reg.reg << 21;
            bits |= (UInt16)pcode_update_mem_labeldiff_imm(instr, &instr->args[2], wop);
            break;

        case PC_AND:
        case PC_OR:
        case PC_XOR:
        case PC_NAND:
        case PC_NOR:
        case PC_EQV:
        case PC_ANDC:
        case PC_ORC:
        case PC_SLW:
        case PC_SRW:
        case PC_SRAW:
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[1].data.reg.reg << 21;
            bits |= instr->args[2].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_ISELGT:
        case PC_ISELEQ:
        case PC_ISELLT:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            bits |= instr->args[3].data.reg.reg << 8;
            break;

        case PC_EXTSH:
        case PC_EXTSB:
        case PC_CNTLZW:
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[1].data.reg.reg << 21;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_MR:
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[1].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_NOT:
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[1].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_SRAWI:
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[1].data.reg.reg << 21;
            bits |= (instr->args[2].data.imm.value & 31) << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_RLWINM:
        case PC_RLWIMI:
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[1].data.reg.reg << 21;
            bits |= (instr->args[2].data.imm.value & 31) << 11;
            bits |= (instr->args[3].data.imm.value & 31) << 6;
            bits |= (instr->args[4].data.imm.value & 31) << 1;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_RLWNM:
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[1].data.reg.reg << 21;
            bits |= instr->args[2].data.reg.reg << 11;
            bits |= (instr->args[3].data.imm.value & 31) << 6;
            bits |= (instr->args[4].data.imm.value & 31) << 1;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_CMP:
        case PC_CMPL:
            bits |= instr->args[0].data.reg.reg << 23;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            break;

        case PC_CMPI:
        case PC_CMPLI:
            bits |= instr->args[0].data.reg.reg << 23;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= (UInt16)instr->args[2].data.imm.value;
            break;

        case PC_MTXER:
        case PC_MTCTR:
        case PC_MTLR:
        case PC_MTMSR:
        case PC_MFMSR:
        case PC_MFXER:
        case PC_MFCTR:
        case PC_MFLR:
        case PC_MFCR:
            bits |= instr->args[0].data.reg.reg << 21;
            break;

        case PC_MFFS:
            bits |= instr->args[0].data.reg.reg << 21;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_MTCRF:
            bits |= instr->args[0].data.imm.value << 12;
            bits |= instr->args[1].data.reg.reg << 21;
            break;

        case PC_MTFSF:
            bits |= (instr->args[0].data.imm.value & 0xFF) << 17;
            bits |= instr->args[1].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_FMR:
        case PC_FABS:
        case PC_FNEG:
        case PC_FNABS:
        case PC_FRES:
        case PC_FRSQRTE:
        case PC_FRSP:
        case PC_FCTIW:
        case PC_FCTIWZ:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_FADD:
        case PC_FADDS:
        case PC_FSUB:
        case PC_FSUBS:
        case PC_FDIV:
        case PC_FDIVS:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_FMADD:
        case PC_FMADDS:
        case PC_FMSUB:
        case PC_FMSUBS:
        case PC_FNMADD:
        case PC_FNMADDS:
        case PC_FNMSUB:
        case PC_FNMSUBS:
        case PC_FSEL:
            bits |= instr->args[3].data.reg.reg << 11;
        case PC_FMUL:
        case PC_FMULS:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 6;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_FCMPU:
        case PC_FCMPO:
            bits |= instr->args[0].data.reg.reg << 23;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            break;

        case PC_MTSPR:
            if (instr->args[0].kind == PCOp_REGISTER) {
                CError_ASSERT(1037, instr->args[0].arg == RegClass_SPR);
                CError_ASSERT(1038, instr->args[0].data.reg.reg < 4);
                bits |= ((spr_to_sysreg[instr->args[0].data.reg.reg] & 0x1F) << 16) +
                    ((spr_to_sysreg[instr->args[0].data.reg.reg] & 0x3E0) << 6);
            } else if (instr->args[0].kind == PCOp_SYSREG && instr->args[0].arg == 0) {
                bits |= ((instr->args[0].data.reg.reg & 0x1F) << 16) +
                    ((instr->args[0].data.reg.reg & 0x3E0) << 6);
            } else {
                CError_FATAL(1043);
            }

            bits |= instr->args[1].data.reg.reg << 21;
            break;

        case PC_MTDCR:
            if (instr->args[0].kind == PCOp_IMMEDIATE) {
                bits |= ((instr->args[0].data.imm.value & 0x1F) << 16) +
                    ((instr->args[0].data.imm.value & 0x3E0) << 6);
            } else {
                CError_FATAL(1052);
            }

            bits |= instr->args[1].data.reg.reg << 21;
            break;

        case PC_MFSPR:
            bits |= instr->args[0].data.reg.reg << 21;

            if (instr->args[1].kind == PCOp_REGISTER && instr->args[1].arg == RegClass_SPR) {
                CError_ASSERT(1065, instr->args[1].data.reg.reg < 4);
                bits |= ((spr_to_sysreg[instr->args[1].data.reg.reg] & 0x1F) << 16) +
                    ((spr_to_sysreg[instr->args[1].data.reg.reg] & 0x3E0) << 6);
            } else if (instr->args[1].kind == PCOp_SYSREG && instr->args[1].arg == 0) {
                bits |= ((instr->args[1].data.reg.reg & 0x1F) << 16) +
                    ((instr->args[1].data.reg.reg & 0x3E0) << 6);
            } else {
                CError_FATAL(1070);
            }
            break;

        case PC_MFDCR:
            bits |= instr->args[0].data.reg.reg << 21;

            if (instr->args[1].kind == PCOp_IMMEDIATE) {
                bits |= ((instr->args[1].data.imm.value & 0x1F) << 16) +
                    ((instr->args[1].data.imm.value & 0x3E0) << 6);
            } else {
                CError_FATAL(1079);
            }
            break;

        case PC_LSWI:
        case PC_STSWI:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= (instr->args[2].data.imm.value & 31) << 11;
            break;

        case PC_MCRFS:
            bits |= (instr->args[1].data.imm.value & 7) << 18;
        case PC_MCRXR:
            bits |= instr->args[0].data.reg.reg << 23;
            break;

        case PC_MFTB:
            bits |= instr->args[0].data.reg.reg << 21;
            if (instr->args[1].kind == PCOp_SYSREG && instr->args[1].arg == 0) {
                if (instr->args[1].data.reg.reg == 284) {
                    bits |= 0xC4000u;
                } else if (instr->args[1].data.reg.reg == 285) {
                    bits |= 0xD4000u;
                } else {
                    CError_FATAL(1110);
                }
            } else {
                CError_FATAL(1113);
            }
            break;

        case PC_MTSR:
            bits |= instr->args[1].data.reg.reg << 21;
            bits |= (instr->args[0].data.imm.value & 15) << 16;
            break;

        case PC_MFSR:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= (instr->args[1].data.imm.value & 15) << 16;
            break;

        case PC_MFSRIN:
        case PC_MTSRIN:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 11;
            break;

        case PC_MTFSB0:
        case PC_MTFSB1:
            bits |= (instr->args[0].data.imm.value & 31) << 21;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_MTFSFI:
            bits |= instr->args[0].data.reg.reg << 23;
            bits |= (instr->args[1].data.imm.value & 15) << 12;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_FSQRT:
        case PC_FSQRTS:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_TLBIE:
        case PC_TLBLD:
        case PC_TLBLI:
            bits |= instr->args[0].data.reg.reg << 11;
            break;

        case PC_TW:
            bits |= (instr->args[0].data.imm.value & 31) << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            break;

        case PC_TWI:
            bits |= (instr->args[0].data.imm.value & 31) << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= (UInt16)instr->args[2].data.imm.value;
            break;

        case PC_OPWORD:
            CError_ASSERT(1186, instr->args[0].kind != PCOp_MEMORY);
            bits = pcode_update_mem_labeldiff_imm(instr, &instr->args[0], wop);
            break;

        case PC_MASKG:
        case PC_MASKIR:
            bits |= instr->args[1].data.reg.reg << 21;
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_LSCBX:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_DIV:
        case PC_DIVS:
        case PC_DOZ:
        case PC_MUL:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fOverflow) {
                bits |= 0x400;
            }
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_NABS:
        case PC_ABS:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            if (PCODE_FLAG_SET_F(instr) & fOverflow) {
                bits |= 0x400;
            }
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_CLCS:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_DOZI:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= (UInt16)instr->args[2].data.imm.value;
            break;

        case PC_RLMI:
            bits |= instr->args[1].data.reg.reg << 21;
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            bits |= (instr->args[3].data.imm.value & 31) << 6;
            bits |= (instr->args[4].data.imm.value & 31) << 1;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_SLE:
        case PC_SLEQ:
        case PC_SLLQ:
        case PC_SLQ:
        case PC_SRAQ:
        case PC_SRE:
        case PC_SREA:
        case PC_SREQ:
        case PC_SRLQ:
        case PC_SRQ:
        case PC_RRIB:
            bits |= instr->args[1].data.reg.reg << 21;
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_SLIQ:
        case PC_SLLIQ:
        case PC_SRAIQ:
        case PC_SRIQ:
        case PC_SRLIQ:
            bits |= instr->args[1].data.reg.reg << 21;
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= (instr->args[2].data.imm.value & 31) << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_TLBRE:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= (instr->args[2].data.imm.value & 1) << 11;
            break;

        case PC_TLBWE:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= (instr->args[2].data.imm.value & 1) << 11;
            break;

        case PC_WRTEE:
            bits |= instr->args[0].data.reg.reg << 21;
            break;

        case PC_WRTEEI:
            bits |= instr->args[0].data.imm.value << 15;
            break;

        case PC_DSTT:
        case PC_DSTSTT:
            bits |= 0x2000000u;
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[1].data.reg.reg << 11;
            bits |= (instr->args[2].data.imm.value & 3) << 21;
            break;

        case PC_DST:
        case PC_DSTST:
            bits |= (instr->args[3].data.imm.value & 1) << 25;
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[1].data.reg.reg << 11;
            bits |= (instr->args[2].data.imm.value & 3) << 21;
            break;

        case PC_DSSALL:
            bits |= 0x2000000u;
            break;

        case PC_DSS:
            bits |= (instr->args[1].data.imm.value & 1) << 25;
            bits |= (instr->args[0].data.imm.value & 3) << 21;
            break;

        case PC_LVEBX:
        case PC_LVEHX:
        case PC_LVEWX:
        case PC_LVSL:
        case PC_LVSR:
        case PC_LVX:
        case PC_LVXL:
        case PC_STVEBX:
        case PC_STVEHX:
        case PC_STVEWX:
        case PC_STVX:
        case PC_STVXL:
        case PC_VADDCUW:
        case PC_VADDFP:
        case PC_VADDSBS:
        case PC_VADDSHS:
        case PC_VADDSWS:
        case PC_VADDUBM:
        case PC_VADDUBS:
        case PC_VADDUHM:
        case PC_VADDUHS:
        case PC_VADDUWM:
        case PC_VADDUWS:
        case PC_VAND:
        case PC_VANDC:
        case PC_VAVGSB:
        case PC_VAVGSH:
        case PC_VAVGSW:
        case PC_VAVGUB:
        case PC_VAVGUH:
        case PC_VAVGUW:
        case PC_VMAXFP:
        case PC_VMAXSB:
        case PC_VMAXSH:
        case PC_VMAXSW:
        case PC_VMAXUB:
        case PC_VMAXUH:
        case PC_VMAXUW:
        case PC_VMINFP:
        case PC_VMINSB:
        case PC_VMINSH:
        case PC_VMINSW:
        case PC_VMINUB:
        case PC_VMINUH:
        case PC_VMINUW:
        case PC_VMRGHB:
        case PC_VMRGHH:
        case PC_VMRGHW:
        case PC_VMRGLB:
        case PC_VMRGLH:
        case PC_VMRGLW:
        case PC_VMULESB:
        case PC_VMULESH:
        case PC_VMULEUB:
        case PC_VMULEUH:
        case PC_VMULOSB:
        case PC_VMULOSH:
        case PC_VMULOUB:
        case PC_VMULOUH:
        case PC_VNOR:
        case PC_VOR:
        case PC_VPKPX:
        case PC_VPKSHSS:
        case PC_VPKSHUS:
        case PC_VPKSWSS:
        case PC_VPKSWUS:
        case PC_VPKUHUM:
        case PC_VPKUHUS:
        case PC_VPKUWUM:
        case PC_VPKUWUS:
        case PC_VRLB:
        case PC_VRLH:
        case PC_VRLW:
        case PC_VSL:
        case PC_VSLB:
        case PC_VSLH:
        case PC_VSLO:
        case PC_VSLW:
        case PC_VSR:
        case PC_VSRAB:
        case PC_VSRAH:
        case PC_VSRAW:
        case PC_VSRB:
        case PC_VSRH:
        case PC_VSRO:
        case PC_VSRW:
        case PC_VSUBCUW:
        case PC_VSUBFP:
        case PC_VSUBSBS:
        case PC_VSUBSHS:
        case PC_VSUBSWS:
        case PC_VSUBUBM:
        case PC_VSUBUBS:
        case PC_VSUBUHM:
        case PC_VSUBUHS:
        case PC_VSUBUWM:
        case PC_VSUBUWS:
        case PC_VSUMSWS:
        case PC_VSUM2SWS:
        case PC_VSUM4SBS:
        case PC_VSUM4SHS:
        case PC_VSUM4UBS:
        case PC_VXOR:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            break;

        case PC_VCFSX:
        case PC_VCFUX:
        case PC_VCTSXS:
        case PC_VCTUXS:
        case PC_VSPLTB:
        case PC_VSPLTH:
        case PC_VSPLTW:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 11;
            bits |= (instr->args[2].data.imm.value & 31) << 16;
            break;

        case PC_VEXPTEFP:
        case PC_VLOGEFP:
        case PC_VREFP:
        case PC_VRFIM:
        case PC_VRFIN:
        case PC_VRFIP:
        case PC_VRFIZ:
        case PC_VRSQRTEFP:
        case PC_VUPKHPX:
        case PC_VUPKHSB:
        case PC_VUPKHSH:
        case PC_VUPKLPX:
        case PC_VUPKLSB:
        case PC_VUPKLSH:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 11;
            break;

        case PC_VCMPBFP:
        case PC_VCMPEQFP:
        case PC_VCMPEQUB:
        case PC_VCMPEQUH:
        case PC_VCMPEQUW:
        case PC_VCMPGEFP:
        case PC_VCMPGTFP:
        case PC_VCMPGTSB:
        case PC_VCMPGTSH:
        case PC_VCMPGTSW:
        case PC_VCMPGTUB:
        case PC_VCMPGTUH:
        case PC_VCMPGTUW:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 0x400;
            }
            break;

        case PC_VSPLTISB:
        case PC_VSPLTISH:
        case PC_VSPLTISW:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= (instr->args[1].data.imm.value & 31) << 16;
            break;

        case PC_VMHADDSHS:
        case PC_VMHRADDSHS:
        case PC_VMLADDUHM:
        case PC_VMSUMMBM:
        case PC_VMSUMSHM:
        case PC_VMSUMSHS:
        case PC_VMSUMUBM:
        case PC_VMSUMUHM:
        case PC_VMSUMUHS:
        case PC_VPERM:
        case PC_VSEL:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            bits |= instr->args[3].data.reg.reg << 6;
            break;

        case PC_VMADDFP:
        case PC_VNMSUBFP:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 6;
            bits |= instr->args[3].data.reg.reg << 11;
            break;

        case PC_VSLDOI:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            bits |= (instr->args[3].data.imm.value & 15) << 6;
            break;

        case PC_VMR:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[1].data.reg.reg << 11;
            break;

        case PC_VMRP:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[1].data.reg.reg << 11;
            break;

        case PC_MFVSCR:
            bits |= instr->args[0].data.reg.reg << 21;
            break;

        case PC_MTVSCR:
            bits |= instr->args[0].data.reg.reg << 11;
            break;

        case PC_PSQ_L:
        case PC_PSQ_LU:
        case PC_PSQ_ST:
        case PC_PSQ_STU:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[3].data.imm.value << 15;
            bits |= (instr->args[4].data.reg.reg - SPR_GQR0) << 12;
            bits |= pcode_update_mem_labeldiff_imm(instr, &instr->args[2], wop) & 0xFFF;
            break;

        case PC_PSQ_LX:
        case PC_PSQ_LUX:
        case PC_PSQ_STX:
        case PC_PSQ_STUX:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            bits |= instr->args[3].data.imm.value << 10;
            bits |= (instr->args[4].data.reg.reg - SPR_GQR0) << 7;
            break;

        case PC_DCBZ_L:
            bits |= instr->args[0].data.reg.reg << 16;
            bits |= instr->args[1].data.reg.reg << 11;
            break;

        case PC_PS_ADD:
        case PC_PS_SUB:
        case PC_PS_DIV:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_PS_MADD:
        case PC_PS_MSUB:
        case PC_PS_NMADD:
        case PC_PS_NMSUB:
        case PC_PS_SEL:
        case PC_PS_SUM0:
        case PC_PS_SUM1:
        case PC_PS_MADDS0:
        case PC_PS_MADDS1:
            bits |= instr->args[3].data.reg.reg << 11;
        case PC_PS_MUL:
        case PC_PS_MULS0:
        case PC_PS_MULS1:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 6;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_PS_RES:
        case PC_PS_ABS:
        case PC_PS_NABS:
        case PC_PS_NEG:
        case PC_PS_MR:
        case PC_PS_RSQRTE:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_PS_CMPU0:
        case PC_PS_CMPO0:
        case PC_PS_CMPU1:
        case PC_PS_CMPO1:
            bits |= instr->args[0].data.reg.reg << 23;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            break;

        case PC_PS_MERGE00:
        case PC_PS_MERGE01:
        case PC_PS_MERGE10:
        case PC_PS_MERGE11:
            bits |= instr->args[0].data.reg.reg << 21;
            bits |= instr->args[1].data.reg.reg << 16;
            bits |= instr->args[2].data.reg.reg << 11;
            if (PCODE_FLAG_SET_F(instr) & fRecordBit) {
                bits |= 1;
            }
            break;

        case PC_EIEIO:
        case PC_ISYNC:
        case PC_SYNC:
        case PC_RFI:
        case PC_NOP:
        case PC_SC:
        case PC_TLBIA:
        case PC_TLBSYNC:
        case PC_TRAP:
        case PC_DSA:
        case PC_ESA:
        case PC_RFCI:
            break;

        default:
            CError_FATAL(2213);
    }

    return CTool_EndianConvertWord32(bits);
}

static PCode* targetinstruction(PCodeLabel* label) {
    PCodeBlock* block = label->block;
    while (block->pcodeCount == 0) {
        block = block->nextBlock;
    }
    return block->firstPCode;
}

static void invertybit(PCode* instr, SInt32 value) {
    if (instr->op == PC_BC) {
        if (instr->args[0].data.imm.value & 1) {
            if (value < 0) {
                instr->flags |= fBranchNotTaken;
            } else {
                instr->flags |= fBranchTaken;
            }
        }
    } else if (instr->op == PC_BCCTR || instr->op == PC_BCLR) {
        if (instr->args[0].data.imm.value & 1) {
            instr->flags |= fBranchTaken;
        }
    }

    if (PCODE_FLAG_SET_T(instr) & fBranchTaken) {
        instr->flags = (instr->flags & ~fBranchTaken) | fBranchNotTaken;
    } else if (PCODE_FLAG_SET_T(instr) & fBranchNotTaken) {
        instr->flags = (instr->flags & ~fBranchNotTaken) | fBranchTaken;
    } else if (value < 0) {
        instr->flags = (instr->flags & ~fBranchTaken) | fBranchNotTaken;
    } else {
        instr->flags = (instr->flags & ~fBranchNotTaken) | fBranchTaken;
    }
}

static void insertlongbranches(SInt32 max, SInt32 min) {
    PCodeBlock* block;
    PCodeLabel* label;
    SInt32 i;
    SInt32 offset; // (the branch's own)

    i = 0;
    for (block = pcbasicblocks; block; block = block->nextBlock) {
        block->codeOffset = i;
        if (block->pcodeCount) {
            i += block->pcodeCount * 4;
            if (block->pcodeCount && (block->lastPCode->flags & fIsBranch)) {
                i += 4;
            }
        }
    }

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        if (block->pcodeCount && (block->lastPCode->flags & fIsBranch)) {
            switch (block->lastPCode->op) {
                case PC_BT:
                case PC_BF:
                    offset = block->codeOffset + ((block->pcodeCount - 1) * 4);
                    if (block->lastPCode->args[2].kind == PCOp_LABEL) {
                        label = block->lastPCode->args[2].data.label.label;
                        i = label->block->codeOffset - offset;
                        if (i > max || i < min) {
                            block->lastPCode->op = (block->lastPCode->op == PC_BT) ? PC_BF : PC_BT;
                            block->lastPCode->args[2].data.label.label = block->nextBlock->labels;
                            invertybit(block->lastPCode, i);
                            appendpcode(block, makepcode(PC_B, label));
                        }
                    }
                    break;

                case PC_BC:
                    offset = block->codeOffset + ((block->pcodeCount - 1) * 4);
                    if (block->lastPCode->args[3].kind == PCOp_LABEL) {
                        label = block->lastPCode->args[3].data.label.label;
                        i = label->block->codeOffset - offset;
                        invertybit(block->lastPCode, i);
                        if (i > max || i < min) {
                            switch (block->lastPCode->args[0].data.imm.value & 30) {
                                case 0:
                                case 2:
                                case 8:
                                case 10:
                                    block->lastPCode->args[0].data.imm.value ^= 11;
                                    block->lastPCode->args[3].data.label.label =
                                        block->nextBlock->labels;
                                    break;
                                case 16:
                                case 18:
                                    block->lastPCode->args[0].data.imm.value ^= 3;
                                    block->lastPCode->args[3].data.label.label =
                                        block->nextBlock->labels;
                                    break;
                                case 4:
                                case 12:
                                    block->lastPCode->args[0].data.imm.value ^= 9;
                                    block->lastPCode->args[3].data.label.label =
                                        block->nextBlock->labels;
                                    break;
                                case 20:
                                    deletepcode(block->lastPCode);
                                    break;
                                default:
                                    CError_FATAL(2380);
                            }

                            appendpcode(block, makepcode(PC_B, label));
                        }
                    }
                    break;

                case PC_BDNZ:
                case PC_BDZ:
                    offset = block->codeOffset + ((block->pcodeCount - 1) * 4);
                    if (block->lastPCode->args[0].kind == PCOp_LABEL) {
                        label = block->lastPCode->args[0].data.label.label;
                        i = label->block->codeOffset - offset;
                        if (i > max || i < min) {
                            switch (block->lastPCode->op) {
                                case PC_BDZ:
                                    block->lastPCode->op = PC_BDNZ;
                                    break;
                                case PC_BDNZ:
                                    block->lastPCode->op = PC_BDZ;
                                    break;
                                default:
                                    CError_FATAL(2401);
                            }

                            block->lastPCode->args[0].data.label.label = block->nextBlock->labels;
                            invertybit(block->lastPCode, i);
                            appendpcode(block, makepcode(PC_B, label));
                        }
                    }
                    break;

                case PC_BDNZT:
                case PC_BDNZF:
                case PC_BDZT:
                case PC_BDZF:
                    offset = block->codeOffset + ((block->pcodeCount - 1) * 4);
                    if (block->lastPCode->args[2].kind == PCOp_LABEL) {
                        label = block->lastPCode->args[2].data.label.label;
                        i = label->block->codeOffset - offset;
                        if (i > max || i < min) {
                            switch (block->lastPCode->op) {
                                case PC_BDNZT:
                                    block->lastPCode->op = PC_BDZF;
                                    break;
                                case PC_BDNZF:
                                    block->lastPCode->op = PC_BDZT;
                                    break;
                                case PC_BDZT:
                                    block->lastPCode->op = PC_BDNZF;
                                    break;
                                case PC_BDZF:
                                    block->lastPCode->op = PC_BDNZT;
                                    break;
                                default:
                                    CError_FATAL(2432);
                            }

                            block->lastPCode->args[2].data.label.label = block->nextBlock->labels;
                            invertybit(block->lastPCode, i);
                            appendpcode(block, makepcode(PC_B, label));
                        }
                    }
                    break;
            }
        }
    }
}

SInt32 optimizefinalbranches(SInt32 codesize) {
    PCodeBlock* block;
    PCode* instr;
    SInt32 offset;
    int changed;
    int deleted;
    PCodeLabel* label;
    PCode* target;

    do {
        changed = deleted = FALSE;

        for (block = pcbasicblocks; block; block = block->nextBlock) {
            if (block->pcodeCount == 0) {
                continue;
            }

            if (!((instr = block->lastPCode)->flags & fIsBranch)) {
                continue;
            }

            offset = block->codeOffset + (block->pcodeCount - 1) * 4;

            if (instr->op == PC_B && instr->args[0].kind == PCOp_LABEL) {
                target = targetinstruction(label = instr->args[0].data.label.label);

                if (label->block->codeOffset == (offset + 4)) {
                    deletepcode(instr);
                    changed = deleted = TRUE;
                } else if (target->op == PC_B) {
                    if (target->args[0].kind == PCOp_LABEL &&
                        target->args[0].data.label.label != instr->args[0].data.label.label) {
                        instr->args[0].data.label.label = target->args[0].data.label.label;
                        changed = TRUE;
                    }
                } else if (target->op == PC_BLR) {
                    instr->op = PC_BLR;
                    changed = TRUE;
                }
                continue;
            }

            if ((instr->op == PC_BT || instr->op == PC_BF) && instr->args[2].kind == PCOp_LABEL) {
                PCodeBlock* branchBlock = instr->block; // NOTE: invented name
                target = targetinstruction(label = instr->args[2].data.label.label);

                if (label->block->codeOffset == (offset + 4)) {
                    deletepcode(instr);
                    changed = deleted = TRUE;
                } else if (target->op == PC_B) {
                    if (target->args[0].kind == PCOp_LABEL &&
                        target->args[0].data.label.label != instr->args[2].data.label.label) {
                        instr->args[2].data.label.label = target->args[0].data.label.label;
                        changed = TRUE;
                    }
                } else if (copts.opt_bcc_lr_ctr) {
                    if (target->op == PC_BLR) {
                        if (instr->op == PC_BT) {
                            instr->op = PC_BTLR;
                        } else {
                            instr->op = PC_BFLR;
                        }
                        instr->argCount = 2;
                        changed = TRUE;
                    } else if (target->op == PC_BCTR) {
                        if (instr->op == PC_BT) {
                            instr->op = PC_BTCTR;
                        } else {
                            instr->op = PC_BFCTR;
                        }
                        instr->argCount = 2;
                        changed = TRUE;
                    } else if (branchBlock->nextBlock &&
                               branchBlock->nextBlock->firstPCode &&
                               branchBlock->nextBlock->firstPCode->op == PC_BLR &&
                               label->block->codeOffset == (offset + 8)) {
                        if (branchBlock->nextBlock->predecessors &&
                            branchBlock->nextBlock->predecessors->block == branchBlock &&
                            !branchBlock->nextBlock->predecessors->nextLink) {
                            if (instr->op == PC_BT) {
                                instr->op = PC_BFLR;
                            } else {
                                instr->op = PC_BTLR;
                            }
                            change_num_operands(instr, 2);
                            deletepcode(branchBlock->nextBlock->firstPCode);
                            changed = deleted = TRUE;
                        }
                    } else if (branchBlock->nextBlock &&
                               branchBlock->nextBlock->firstPCode &&
                               branchBlock->nextBlock->firstPCode->op == PC_BCTR &&
                               label->block->codeOffset == (offset + 8)) {
                        if (branchBlock->nextBlock->predecessors &&
                            branchBlock->nextBlock->predecessors->block == branchBlock &&
                            !branchBlock->nextBlock->predecessors->nextLink) {
                            if (instr->op == PC_BT) {
                                instr->op = PC_BFCTR;
                            } else {
                                instr->op = PC_BTCTR;
                            }
                            change_num_operands(instr, 2);
                            deletepcode(branchBlock->nextBlock->firstPCode);
                            changed = deleted = TRUE;
                        }
                    }
                }
                continue;
            }

            if (instr->op == PC_BC &&
                instr->args[3].kind == PCOp_LABEL &&
                !(PCODE_FLAG_SET_T(instr) & (fSideEffects | fLink))) {
                PCodeBlock* branchBlock = instr->block;
                target = targetinstruction(label = instr->args[3].data.label.label);

                if (label->block->codeOffset == (offset + 4)) {
                    deletepcode(instr);
                    changed = deleted = TRUE;
                } else if (target->op == PC_B) {
                    if (target->args[0].kind == PCOp_LABEL &&
                        target->args[0].data.label.label != instr->args[3].data.label.label) {
                        instr->args[3].data.label.label = target->args[0].data.label.label;
                        changed = TRUE;
                    }
                } else if (copts.opt_bcc_lr_ctr) {
                    if (target->op == PC_BLR) {
                        instr->op = PC_BCLR;
                        instr->argCount = 3;
                        changed = TRUE;
                    } else if (target->op == PC_BCTR) {
                        instr->op = PC_BCCTR;
                        instr->argCount = 3;
                        changed = TRUE;
                    } else if (branchBlock->nextBlock &&
                               branchBlock->nextBlock->firstPCode &&
                               branchBlock->nextBlock->firstPCode->op == PC_BLR &&
                               label->block->codeOffset == (offset + 8)) {
                        SInt32 val = instr->args[0].data.imm.value & 30;
                        PCLink* pred = branchBlock->nextBlock->predecessors;
                        if (pred && pred->block == branchBlock && !pred->nextLink) {
                            if ((val & 30) == 4) {
                                instr->args[0].data.imm.value = val | 12;
                            } else if ((val & 30) == 12) {
                                instr->args[0].data.imm.value = val & 23;
                            }
                            instr->op = PC_BCLR;
                            instr->argCount = 3;
                            deletepcode(branchBlock->nextBlock->firstPCode);
                            changed = deleted = TRUE;
                        }
                    } else if (branchBlock->nextBlock &&
                               branchBlock->nextBlock->firstPCode &&
                               branchBlock->nextBlock->firstPCode->op == PC_BCTR &&
                               label->block->codeOffset == (offset + 8)) {
                        SInt32 val = instr->args[0].data.imm.value & 30;
                        if (branchBlock->nextBlock->predecessors &&
                            branchBlock->nextBlock->predecessors->block == branchBlock &&
                            !branchBlock->nextBlock->predecessors->nextLink) {
                            if ((val & 30) == 4) {
                                instr->args[0].data.imm.value = val | 12;
                            } else if ((val & 30) == 12) {
                                instr->args[0].data.imm.value = val & 23;
                            }
                            instr->op = PC_BCCTR;
                            instr->argCount = 3;
                            deletepcode(branchBlock->nextBlock->firstPCode);
                            changed = deleted = TRUE;
                        }
                    }
                }
            }
        }

        if (deleted) {
            codesize = pccomputeoffsets();
        }
    } while (changed);

    return codesize;
}

static SInt32 insert_align_nops(Object* func, SInt32 codesize) {
    PCodeBlock* block;
    int changed;
    PCodeBlock* prev;

    do {
        changed = FALSE;

        for (block = pcbasicblocks; block; block = block->nextBlock) {
            if ((block->flags & (fInnerLoop | fUnrolled)) == fInnerLoop &&
                (block->codeOffset & 7) &&
                block->pcodeCount < 8 &&
                (prev = block->prevBlock) &&
                !(prev->flags & fInnerLoop)) {
                if (prev->lastPCode &&
                    prev->lastPCode->op == PC_NOP &&
                    !(prev->lastPCode->flags & fSideEffects)) {
                    deletepcode(prev->lastPCode);
                } else {
                    PCode* nop = makepcode(PC_NOP);
                    nop->flags = nop->flags & ~fSideEffects;
                    appendpcode(prev, nop);
                }

                codesize = pccomputeoffsets();
                changed = TRUE;
            }
        }

        if (changed) {
            pclistblocks(CMangler_GetLinkName(func)->name, "AFTER INSERT ALIGN NOPs");
            if ((UInt32)codesize >= (copts.bc_range >> 1)) {
                insertlongbranches((copts.bc_range >> 1) - 1, -(copts.bc_range >> 1));
                codesize = pccomputeoffsets();
            }
        }

    } while (changed);

    return codesize;
}

SInt32 assemblefunction(Object* func, EntryPoint* entrypoints) {
    WeirdOperand wop;
    PCodeBlock* block;
    PCode* instr;
    SInt32 offset2;
    SInt32 codesize;
    SInt32 tbsize;
    SInt32 offset;
    ObjGenSection* section;
    GList* gl;
    EntryPoint* ep;
    void* tbdata;

    codesize = pccomputeoffsets();
    if (codesize <= 0) {
        PPCError_Error(PPCERR_FUNCTION_HAS_NO_INSTRUCTIONS, func->name->name);
    }

    if (copts.peephole || copts.optimizationlevel >= 3) {
        codesize = optimizefinalbranches(codesize);
    }

    if ((UInt32)codesize >= (copts.bc_range >> 1)) {
        insertlongbranches((copts.bc_range >> 1) - 1, -(copts.bc_range >> 1));
        codesize = pccomputeoffsets();
    }

    if (copts.function_align > 4) {
        codesize = insert_align_nops(func, codesize);
    }

    ObjGen_BestSection(func);
    if (copts.filesyminfo) {
        ObjGen_SymFunc(func);
    }

    tbsize = 0;
    if (copts.traceback) {
        tbdata = generate_traceback(codesize, CMangler_GetLinkName(func)->name, &tbsize, func);
    }

    if (func->section == SECT_DEFAULT) {
        func->section = SECT_TEXT;
    }

    offset = tbsize;
    section = ObjGen_DeclareCode(func, codesize + tbsize);
    gl = ObjGen_GetSectionGList(section);

    codebase = gl->size;
    AppendGListNoData(gl, codesize + offset);

    if (copts.filesyminfo) {
        newDWARF_function(func, codesize + tbsize, codebase, section);
        ObjGen_SourceRef(functionbodyoffset, 0);
#if VERSION >= VERSION_GC_2_7
    } else {
        // 2.7: without -sym too, for the function's .line symbol in a section with debugging
        // information
        newDWARF_function(func, codesize + tbsize, codebase, section);
#endif
    }

    init_xref_anchors();
    if (entrypoints) {
        for (ep = entrypoints; ep; ep = ep->next) {
            ObjGen_DeclareEntry(ep->object, ep->block->codeOffset);
        }
    }

    for (block = pcbasicblocks; block; block = block->nextBlock) {
        for (offset2 = block->codeOffset, instr = block->firstPCode; instr;
             instr = instr->nextPCode, offset2 += 4) {
            if (copts.filesyminfo && instr->sourceoffset != -1) {
                ObjGen_SourceRef(instr->sourceoffset, offset2);
            }

            *((UInt32*)(*gl->data + codebase + offset2)) = assemblepcode(instr, offset2, &wop);

            if (instr->flags & fMWCATSExit) {
                MWCATS_AddExit(section, offset2);
            }

            if (wop.type != -1) {
                ObjGen_RelocateObj(section, offset2, wop.object, wop.type, wop.addend);
            }
        }
    }

    if (copts.cats) {
        MWCATS_WriteFunction(func, codesize);
    }

    if (copts.traceback) {
        memcpy(*gl->data + codebase + codesize, tbdata, offset);
    }

    if (copts.filesyminfo) {
        ObjGen_DeclareSymInfo();
    }

    if (copts.traceback) {
        return codesize + tbsize;
    } else {
        return codesize;
    }
}
