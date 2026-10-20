// OpcodeInfo.c: the PCode opcode table (with the Gekko paired-single opcodes). A data-only object,
// linked between CDecl.c and InlineAsmMnemonicsPPC.c.
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"

#pragma pool_strings on

// clang-format off
OpcodeInfo opcodeinfo[] = {
    "B",            "l;p",             2,  0, fIsBranch | fCanLink | fCanBeAbsolute,                                       0x48000000, // PC_B
    "BL",           "#,m;p",           2,  0, fIsCall | fLink | fCanLink | fCanBeAbsolute,                                 0x48000001, // PC_BL
    "BC",           "B,Q,l;p",         5,  0, fIsBranch | fCanLink | fCanBeAbsolute,                                       0x40000000, // PC_BC
    "BCLR",         "B,Q;L",           4,  0, fIsBranch | fCanLink,                                                        0x4C000020, // PC_BCLR
    "BCCTR",        "B,Q;Cp",          5,  0, fIsBranch | fCanLink,                                                        0x4C000420, // PC_BCCTR
    "BT",           "Q,l;p",           4,  0, fIsBranch | fCanLink | fCanBeAbsolute,                                       0x41800000, // PC_BT
    "BTLR",         "Q;L",             3,  0, fIsBranch | fCanLink,                                                        0x4D800020, // PC_BTLR
    "BTCTR",        "Q;Cp",            4,  0, fIsBranch | fCanLink,                                                        0x4D800420, // PC_BTCTR
    "BF",           "Q,l;p",           4,  0, fIsBranch | fCanLink | fCanBeAbsolute,                                       0x40800000, // PC_BF
    "BFLR",         "Q;L",             3,  0, fIsBranch | fCanLink,                                                        0x4C800020, // PC_BFLR
    "BFCTR",        "Q;Cp",            4,  0, fIsBranch | fCanLink,                                                        0x4C800420, // PC_BFCTR
    "BDNZ",         "l;Cp",            3,  0, fIsBranch | fCanLink | fCanBeAbsolute,                                       0x42000000, // PC_BDNZ
    "BDNZT",        "Q,l;Cp",          4,  0, fIsBranch | fCanLink | fCanBeAbsolute,                                       0x41000000, // PC_BDNZT
    "BDNZF",        "Q,l;Cp",          4,  0, fIsBranch | fCanLink | fCanBeAbsolute,                                       0x40000000, // PC_BDNZF
    "BDZ",          "l;Cp",            2,  0, fIsBranch | fCanLink | fCanBeAbsolute,                                       0x42400000, // PC_BDZ
    "BDZT",         "Q,l;Cp",          4,  0, fIsBranch | fCanLink | fCanBeAbsolute,                                       0x41400000, // PC_BDZT
    "BDZF",         "Q,l;Cp",          4,  0, fIsBranch | fCanLink | fCanBeAbsolute,                                       0x40400000, // PC_BDZF
    "BLR",          ";L",              1,  0, fIsBranch | fCanLink,                                                        0x4E800020, // PC_BLR
    "BCTR",         ";Cmp",            3,  0, fIsBranch | fCanLink,                                                        0x4E800420, // PC_BCTR
    "BCTRL",        "#;C=L",           2,  0, fIsCall | fLink | fCanLink,                                                  0x4E800421, // PC_BCTRL
    "BLRL",         "#;+L",            1,  0, fIsCall | fLink | fCanLink,                                                  0x4E800021, // PC_BLRL
    "LBZ",          "=r,d(b)",         3,  3, fIsRead,                                                                     0x88000000, // PC_LBZ
    "LBZU",         "=r,d(+b)",        3,  3, fIsRead | fUpdatesPtr,                                                       0x8C000000, // PC_LBZU
    "LBZX",         "=r,(b,r)",        3,  3, fIsRead | fIsPtrOp,                                                          0x7C0000AE, // PC_LBZX
    "LBZUX",        "=r,(+b,r)",       3,  3, fIsRead | fIsPtrOp | fUpdatesPtr,                                            0x7C0000EE, // PC_LBZUX
    "LHZ",          "=r,d(b)",         3,  3, fIsRead,                                                                     0xA0000000, // PC_LHZ
    "LHZU",         "=r,d(+b)",        3,  3, fIsRead | fUpdatesPtr,                                                       0xA4000000, // PC_LHZU
    "LHZX",         "=r,(b,r)",        3,  3, fIsRead | fIsPtrOp,                                                          0x7C00022E, // PC_LHZX
    "LHZUX",        "=r,(+b,r)",       3,  3, fIsRead | fIsPtrOp | fUpdatesPtr,                                            0x7C00026E, // PC_LHZUX
    "LHA",          "=r,d(b)",         3,  3, fIsRead,                                                                     0xA8000000, // PC_LHA
    "LHAU",         "=r,d(+b)",        3,  3, fIsRead | fUpdatesPtr,                                                       0xAC000000, // PC_LHAU
    "LHAX",         "=r,(b,r)",        3,  3, fIsRead | fIsPtrOp,                                                          0x7C0002AE, // PC_LHAX
    "LHAUX",        "=r,(+b,r)",       3,  3, fIsRead | fIsPtrOp | fUpdatesPtr,                                            0x7C0002EE, // PC_LHAUX
    "LHBRX",        "=r,(b,r)",        3,  3, fIsRead | fIsPtrOp,                                                          0x7C00062C, // PC_LHBRX
    "LWZ",          "=r,d(b)",         3,  3, fIsRead,                                                                     0x80000000, // PC_LWZ
    "LWZU",         "=r,d(+b)",        3,  3, fIsRead | fUpdatesPtr,                                                       0x84000000, // PC_LWZU
    "LWZX",         "=r,(b,r)",        3,  3, fIsRead | fIsPtrOp,                                                          0x7C00002E, // PC_LWZX
    "LWZUX",        "=r,(+b,r)",       3,  3, fIsRead | fIsPtrOp | fUpdatesPtr,                                            0x7C00006E, // PC_LWZUX
    "LWBRX",        "=r,(b,r)",        3,  3, fIsRead | fIsPtrOp,                                                          0x7C00042C, // PC_LWBRX
    "LMW",          "#,=r,d(b);=V",    3,  3, fIsRead,                                                                     0xB8000000, // PC_LMW
    "STB",          "r,d(b)",          3,  1, fIsWrite,                                                                    0x98000000, // PC_STB
    "STBU",         "r,d(+b)",         3,  2, fIsWrite | fUpdatesPtr,                                                      0x9C000000, // PC_STBU
    "STBX",         "r,(b,r)",         3,  1, fIsWrite | fIsPtrOp,                                                         0x7C0001AE, // PC_STBX
    "STBUX",        "r,(+b,r)",        3,  2, fIsWrite | fIsPtrOp | fUpdatesPtr,                                           0x7C0001EE, // PC_STBUX
    "STH",          "r,d(b)",          3,  1, fIsWrite,                                                                    0xB0000000, // PC_STH
    "STHU",         "r,d(+b)",         3,  2, fIsWrite | fUpdatesPtr,                                                      0xB4000000, // PC_STHU
    "STHX",         "r,(b,r)",         3,  1, fIsWrite | fIsPtrOp,                                                         0x7C00032E, // PC_STHX
    "STHUX",        "r,(+b,r)",        3,  2, fIsWrite | fIsPtrOp | fUpdatesPtr,                                           0x7C00036E, // PC_STHUX
    "STHBRX",       "r,(b,r)",         3,  1, fIsWrite | fIsPtrOp,                                                         0x7C00072C, // PC_STHBRX
    "STW",          "r,d(b)",          3,  1, fIsWrite,                                                                    0x90000000, // PC_STW
    "STWU",         "r,d(+b)",         3,  2, fIsWrite | fUpdatesPtr,                                                      0x94000000, // PC_STWU
    "STWX",         "r,(b,r)",         3,  1, fIsWrite | fIsPtrOp,                                                         0x7C00012E, // PC_STWX
    "STWUX",        "r,(+b,r)",        3,  2, fIsWrite | fIsPtrOp | fUpdatesPtr,                                           0x7C00016E, // PC_STWUX
    "STWBRX",       "r,(b,r)",         3,  1, fIsWrite | fIsPtrOp,                                                         0x7C00052C, // PC_STWBRX
    "STMW",         "#,r,d(b);V",      3,  1, fIsWrite,                                                                    0xBC000000, // PC_STMW
    "DCBF",         "b,r",             2,  1, fSideEffects | fOpTypeGPR,                                                   0x7C0000AC, // PC_DCBF
    "DCBST",        "b,r",             2,  1, fSideEffects | fOpTypeGPR,                                                   0x7C00006C, // PC_DCBST
    "DCBT",         "b,r",             2,  1, fSideEffects | fOpTypeGPR,                                                   0x7C00022C, // PC_DCBT
    "DCBTST",       "b,r",             2,  1, fSideEffects | fOpTypeGPR,                                                   0x7C0001EC, // PC_DCBTST
    "DCBZ",         "b,r",             2,  1, fSideEffects | fOpTypeGPR,                                                   0x7C0007EC, // PC_DCBZ
    "ADD",          "=r,r,r",          3,  2, fCommutative | fIsCSE | fCanSetCarry | fCanSetRecordBit | fOpTypeGPR,        0x7C000214, // PC_ADD
    "ADDC",         "=r,r,r;+X",       4,  2, fCanSetCarry | fCanSetRecordBit | fSetsCarry | fOpTypeGPR,                   0x7C000014, // PC_ADDC
    "ADDE",         "=r,r,r;+X",       4,  2, fCanSetCarry | fCanSetRecordBit | fSetsCarry | fOpTypeGPR,                   0x7C000114, // PC_ADDE
    "ADDI",         "=r,b,m;p",        4,  2, fIsCSE | fHasBaseReg | fCanSetRecordBit | fOpTypeGPR,                        0x38000000, // PC_ADDI
    "ADDIC",        "=r,r,i;+X",       4,  2, fCanSetRecordBit | fSetsCarry | fOpTypeGPR,                                  0x30000000, // PC_ADDIC
    "ADDICR",       "=r,r,n;+X=Z",     5,  2, fCanSetRecordBit | fSetsCarry | fRecordBit | fOpTypeGPR,                     0x34000000, // PC_ADDICR
    "ADDIS",        "=r,b,M",          3,  2, fIsCSE | fHasBaseReg | fOpTypeGPR,                                           0x3C000000, // PC_ADDIS
    "ADDME",        "=r,r;+X",         3,  2, fCanSetCarry | fCanSetRecordBit | fSetsCarry | fOpTypeGPR,                   0x7C0001D4, // PC_ADDME
    "ADDZE",        "=r,r;+X",         3,  2, fCanSetCarry | fCanSetRecordBit | fSetsCarry | fOpTypeGPR,                   0x7C000194, // PC_ADDZE
    "DIVW",         "=r,r,r",          3,  2, fIsCSE | fCanSetCarry | fCanSetRecordBit | fOpTypeGPR,                       0x7C0003D6, // PC_DIVW
    "DIVWU",        "=r,r,r",          3,  2, fIsCSE | fCanSetCarry | fCanSetRecordBit | fOpTypeGPR,                       0x7C000396, // PC_DIVWU
    "MULHW",        "=r,r,r",          3,  2, fCommutative | fIsCSE | fCanSetRecordBit | fOpTypeGPR,                       0x7C000096, // PC_MULHW
    "MULHWU",       "=r,r,r",          3,  2, fCommutative | fIsCSE | fCanSetRecordBit | fOpTypeGPR,                       0x7C000016, // PC_MULHWU
    "MULLI",        "=r,r,i",          3,  2, fIsCSE | fOpTypeGPR,                                                         0x1C000000, // PC_MULLI
    "MULLW",        "=r,r,r",          3,  2, fCommutative | fIsCSE | fCanSetCarry | fCanSetRecordBit | fOpTypeGPR,        0x7C0001D6, // PC_MULLW
    "NEG",          "=r,r",            2,  2, fIsCSE | fCanSetCarry | fCanSetRecordBit | fOpTypeGPR,                       0x7C0000D0, // PC_NEG
    "SUBF",         "=r,r,r",          3,  2, fIsCSE | fCanSetCarry | fCanSetRecordBit | fOpTypeGPR,                       0x7C000050, // PC_SUBF
    "SUBFC",        "=r,r,r;+X",       4,  2, fCanSetCarry | fCanSetRecordBit | fSetsCarry | fOpTypeGPR,                   0x7C000010, // PC_SUBFC
    "SUBFE",        "=r,r,r;+X",       4,  2, fCanSetCarry | fCanSetRecordBit | fSetsCarry | fOpTypeGPR,                   0x7C000110, // PC_SUBFE
    "SUBFIC",       "=r,r,i;+X",       4,  2, fSetsCarry | fOpTypeGPR,                                                     0x20000000, // PC_SUBFIC
    "SUBFME",       "=r,r;+X",         3,  2, fCanSetCarry | fCanSetRecordBit | fSetsCarry | fOpTypeGPR,                   0x7C0001D0, // PC_SUBFME
    "SUBFZE",       "=r,r;+X",         3,  2, fCanSetCarry | fCanSetRecordBit | fSetsCarry | fOpTypeGPR,                   0x7C000190, // PC_SUBFZE
    "CMPI",         "=?c,Or,i",        3,  1, fIsCSE | fOpTypeGPR,                                                         0x2C000000, // PC_CMPI
    "CMP",          "=?c,Or,r",        3,  1, fIsCSE | fOpTypeGPR,                                                         0x7C000000, // PC_CMP
    "CMPLI",        "=?c,Or,u",        3,  1, fIsCSE | fOpTypeGPR,                                                         0x28000000, // PC_CMPLI
    "CMPL",         "=?c,Or,r",        3,  1, fIsCSE | fOpTypeGPR,                                                         0x7C000040, // PC_CMPL
    "ANDI",         "=r,r,x;=Z",       4,  2, fCanSetRecordBit | fRecordBit | fOpTypeGPR,                                  0x70000000, // PC_ANDI
    "ANDIS",        "=r,r,x;=Z",       4,  2, fCanSetRecordBit | fRecordBit | fOpTypeGPR,                                  0x74000000, // PC_ANDIS
    "ORI",          "=r,r,x",          3,  2, fIsCSE | fOpTypeGPR,                                                         0x60000000, // PC_ORI
    "ORIS",         "=r,r,x",          3,  2, fIsCSE | fOpTypeGPR,                                                         0x64000000, // PC_ORIS
    "XORI",         "=r,r,x",          3,  2, fIsCSE | fOpTypeGPR,                                                         0x68000000, // PC_XORI
    "XORIS",        "=r,r,x",          3,  2, fIsCSE | fOpTypeGPR,                                                         0x6C000000, // PC_XORIS
    "AND",          "=r,r,r",          3,  2, fCommutative | fIsCSE | fCanSetRecordBit | fOpTypeGPR,                       0x7C000038, // PC_AND
    "OR",           "=r,r,r;pp",       5,  2, fCommutative | fIsCSE | fCanSetRecordBit | fOpTypeGPR,                       0x7C000378, // PC_OR
    "XOR",          "=r,r,r",          3,  2, fCommutative | fIsCSE | fCanSetRecordBit | fOpTypeGPR,                       0x7C000278, // PC_XOR
    "NAND",         "=r,r,r",          3,  2, fCommutative | fIsCSE | fCanSetRecordBit | fOpTypeGPR,                       0x7C0003B8, // PC_NAND
    "NOR",          "=r,r,r",          3,  2, fCommutative | fIsCSE | fCanSetRecordBit | fOpTypeGPR,                       0x7C0000F8, // PC_NOR
    "EQV",          "=r,r,r",          3,  2, fCommutative | fIsCSE | fCanSetRecordBit | fOpTypeGPR,                       0x7C000238, // PC_EQV
    "ANDC",         "=r,r,r",          3,  2, fIsCSE | fCanSetRecordBit | fOpTypeGPR,                                      0x7C000078, // PC_ANDC
    "ORC",          "=r,r,r",          3,  2, fIsCSE | fCanSetRecordBit | fOpTypeGPR,                                      0x7C000338, // PC_ORC
    "EXTSB",        "=r,r",            2,  2, fIsCSE | fCanSetRecordBit | fOpTypeGPR,                                      0x7C000774, // PC_EXTSB
    "EXTSH",        "=r,r",            2,  2, fIsCSE | fCanSetRecordBit | fOpTypeGPR,                                      0x7C000734, // PC_EXTSH
    "CNTLZW",       "=r,r",            2,  2, fIsCSE | fCanSetRecordBit | fOpTypeGPR,                                      0x7C000034, // PC_CNTLZW
    "RLWINM",       "=r,r,u5,u5,u5",   5,  2, fIsCSE | fCanSetRecordBit | fOpTypeGPR,                                      0x54000000, // PC_RLWINM
    "RLWNM",        "=r,r,r,u5,u5",    5,  2, fIsCSE | fCanSetRecordBit | fOpTypeGPR,                                      0x5C000000, // PC_RLWNM
    "RLWIMI",       "+r,r,u5,u5,u5",   5,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x50000000, // PC_RLWIMI
    "SLW",          "=r,r,r",          3,  2, fIsCSE | fCanSetRecordBit | fOpTypeGPR,                                      0x7C000030, // PC_SLW
    "SRW",          "=r,r,r",          3,  2, fIsCSE | fCanSetRecordBit | fOpTypeGPR,                                      0x7C000430, // PC_SRW
    "SRAWI",        "=r,r,u5;+X",      4,  2, fCanSetRecordBit | fSetsCarry | fOpTypeGPR,                                  0x7C000670, // PC_SRAWI
    "SRAW",         "=r,r,r;+X",       4,  2, fCanSetRecordBit | fSetsCarry | fOpTypeGPR,                                  0x7C000630, // PC_SRAW
    "CRAND",        "=Q,Q,Q",          6,  2, fCommutative | fIsCSE | fOpTypeGPR,                                          0x4C000202, // PC_CRAND
    "CRANDC",       "=Q,Q,Q",          6,  2, fIsCSE | fOpTypeGPR,                                                         0x4C000102, // PC_CRANDC
    "CREQV",        "=Q,Q,Q",          6,  2, fCommutative | fIsCSE | fOpTypeGPR,                                          0x4C000242, // PC_CREQV
    "CRNAND",       "=Q,Q,Q",          6,  2, fCommutative | fIsCSE | fOpTypeGPR,                                          0x4C0001C2, // PC_CRNAND
    "CRNOR",        "=Q,Q,Q",          6,  2, fCommutative | fIsCSE | fOpTypeGPR,                                          0x4C000042, // PC_CRNOR
    "CROR",         "=Q,Q,Q",          6,  2, fCommutative | fIsCSE | fOpTypeGPR,                                          0x4C000382, // PC_CROR
    "CRORC",        "=Q,Q,Q",          6,  2, fIsCSE | fOpTypeGPR,                                                         0x4C000342, // PC_CRORC
    "CRXOR",        "=Q,Q,Q",          6,  2, fCommutative | fIsCSE | fOpTypeGPR,                                          0x4C000182, // PC_CRXOR
    "MCRF",         "=c,c",            2,  1, fIsMove | fOpTypeGPR,                                                        0x4C000000, // PC_MCRF
    "MTXER",        "r;=X",            2,  1, fSetsCarry | fOpTypeGPR,                                                     0x7C0103A6, // PC_MTXER
    "MTCTR",        "r;=C",            2,  1, fOpTypeGPR,                                                                  0x7C0903A6, // PC_MTCTR
    "MTLR",         "r;=L",            2,  1, fOpTypeGPR,                                                                  0x7C0803A6, // PC_MTLR
    "MTCRF",        "x8,r;=Y",        10,  1, fOpTypeGPR,                                                                  0x7C000120, // PC_MTCRF
    "MTMSR",        "r",               1,  4, fSideEffects | fOpTypeGPR,                                                   0x7C000124, // PC_MTMSR
    "MTSPR",        "=s,r",            2,  4, fOpTypeGPR,                                                                  0x7C0003A6, // PC_MTSPR
    "MFMSR",        "=r",              1,  4, fOpTypeGPR,                                                                  0x7C0000A6, // PC_MFMSR
    "MFSPR",        "=r,s",            2,  4, fOpTypeGPR,                                                                  0x7C0002A6, // PC_MFSPR
    "MFXER",        "=r;X",            2,  4, fOpTypeGPR,                                                                  0x7C0102A6, // PC_MFXER
    "MFCTR",        "=r;C",            2,  4, fOpTypeGPR,                                                                  0x7C0902A6, // PC_MFCTR
    "MFLR",         "=r;L",            2,  4, fOpTypeGPR,                                                                  0x7C0802A6, // PC_MFLR
    "MFCR",         "=r;Y",            9,  4, fOpTypeGPR,                                                                  0x7C000026, // PC_MFCR
    "MFFS",         "=f",              1,  4, fCanSetRecordBit | fOpTypeFPR,                                               0xFC00048E, // PC_MFFS
    "MTFSF",        "x8,f",            2,  1, fSideEffects | fCanSetRecordBit | fOpTypeFPR,                                0xFC00058E, // PC_MTFSF
    "EIEIO",        "",                0,  0, fSideEffects | fOpTypeGPR,                                                   0x7C0006AC, // PC_EIEIO
    "ISYNC",        "",                0,  0, fSideEffects | fOpTypeGPR,                                                   0x4C00012C, // PC_ISYNC
    "SYNC",         "",                0,  0, fSideEffects | fOpTypeGPR,                                                   0x7C0004AC, // PC_SYNC
    "RFI",          "",                0,  0, fSideEffects,                                                                0x4C000064, // PC_RFI
    "LI",           "=r,n",            2,  4, fIsCSE | fOpTypeGPR,                                                         0x38000000, // PC_LI
    "LIS",          "=r,M",            2,  4, fIsCSE | fOpTypeGPR,                                                         0x3C000000, // PC_LIS
    "MR",           "=r,r;p",          3,  0, fIsMove | fCanSetRecordBit | fOpTypeGPR,                                     0x7C000378, // PC_MR
    "NOP",          "",                0,  0, fSideEffects | fOpTypeGPR,                                                   0x60000000, // PC_NOP
    "NOT",          "=r,r",            2,  2, fIsCSE | fCanSetRecordBit | fOpTypeGPR,                                      0x7C0000F8, // PC_NOT
    "LFS",          "=`f,d(b)",        3,  3, fIsRead,                                                                     0xC0000000, // PC_LFS
    "LFSU",         "=`f,d(+b)",       3,  3, fIsRead | fUpdatesPtr,                                                       0xC4000000, // PC_LFSU
    "LFSX",         "=`f,(b,r)",       3,  3, fIsRead | fIsPtrOp,                                                          0x7C00042E, // PC_LFSX
    "LFSUX",        "=`f,(+b,r)",      3,  3, fIsRead | fIsPtrOp | fUpdatesPtr,                                            0x7C00046E, // PC_LFSUX
    "LFD",          "=f,d(b)",         3,  3, fIsRead,                                                                     0xC8000000, // PC_LFD
    "LFDU",         "=f,d(+b)",        3,  3, fIsRead | fUpdatesPtr,                                                       0xCC000000, // PC_LFDU
    "LFDX",         "=f,(b,r)",        3,  3, fIsRead | fIsPtrOp,                                                          0x7C0004AE, // PC_LFDX
    "LFDUX",        "=f,(+b,r)",       3,  3, fIsRead | fIsPtrOp | fUpdatesPtr,                                            0x7C0004EE, // PC_LFDUX
    "STFS",         "f,d(b)",          3,  1, fIsWrite,                                                                    0xD0000000, // PC_STFS
    "STFSU",        "f,d(+b)",         3,  2, fIsWrite | fUpdatesPtr,                                                      0xD4000000, // PC_STFSU
    "STFSX",        "f,(b,r)",         3,  1, fIsWrite | fIsPtrOp,                                                         0x7C00052E, // PC_STFSX
    "STFSUX",       "f,(+b,r)",        3,  2, fIsWrite | fIsPtrOp | fUpdatesPtr,                                           0x7C00056E, // PC_STFSUX
    "STFD",         "f,d(b)",          3,  1, fIsWrite,                                                                    0xD8000000, // PC_STFD
    "STFDU",        "f,d(+b)",         3,  2, fIsWrite | fUpdatesPtr,                                                      0xDC000000, // PC_STFDU
    "STFDX",        "f,(b,r)",         3,  1, fIsWrite | fIsPtrOp,                                                         0x7C0005AE, // PC_STFDX
    "STFDUX",       "f,(+b,r)",        3,  2, fIsWrite | fIsPtrOp | fUpdatesPtr,                                           0x7C0005EE, // PC_STFDUX
    "FMR",          "=f,f",            2,  0, fIsMove | fCanSetRecordBit | fOpTypeFPR,                                     0xFC000090, // PC_FMR
    "FABS",         "=`f,f",           2,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xFC000210, // PC_FABS
    "FNEG",         "=f,f",            2,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xFC000050, // PC_FNEG
    "FNABS",        "=`f,f",           2,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xFC000110, // PC_FNABS
    "FADD",         "=f,f,f",          3,  2, fCommutative | fIsCSE | fCanSetRecordBit | fOpTypeFPR,                       0xFC00002A, // PC_FADD
    "FADDS",        "=`f,f,f",         3,  2, fCommutative | fIsCSE | fCanSetRecordBit | fOpTypeFPR,                       0xEC00002A, // PC_FADDS
    "FSUB",         "=f,f,f",          3,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xFC000028, // PC_FSUB
    "FSUBS",        "=`f,f,f",         3,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xEC000028, // PC_FSUBS
    "FMUL",         "=f,f,f",          3,  2, fCommutative | fIsCSE | fCanSetRecordBit | fOpTypeFPR,                       0xFC000032, // PC_FMUL
    "FMULS",        "=`f,f,f",         3,  2, fCommutative | fIsCSE | fCanSetRecordBit | fOpTypeFPR,                       0xEC000032, // PC_FMULS
    "FDIV",         "=f,f,f",          3,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xFC000024, // PC_FDIV
    "FDIVS",        "=`f,f,f",         3,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xEC000024, // PC_FDIVS
    "FMADD",        "=f,f,f,f",        4,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xFC00003A, // PC_FMADD
    "FMADDS",       "=`f,f,f,f",       4,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xEC00003A, // PC_FMADDS
    "FMSUB",        "=f,f,f,f",        4,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xFC000038, // PC_FMSUB
    "FMSUBS",       "=`f,f,f,f",       4,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xEC000038, // PC_FMSUBS
    "FNMADD",       "=f,f,f,f",        4,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xFC00003E, // PC_FNMADD
    "FNMADDS",      "=`f,f,f,f",       4,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xEC00003E, // PC_FNMADDS
    "FNMSUB",       "=f,f,f,f",        4,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xFC00003C, // PC_FNMSUB
    "FNMSUBS",      "=`f,f,f,f",       4,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xEC00003C, // PC_FNMSUBS
    "FRES",         "=`f,f",           2,  2, fCanSetRecordBit | fOpTypeFPR,                                               0xEC000030, // PC_FRES
    "FRSQRTE",      "=f,f",            2,  2, fCanSetRecordBit | fOpTypeFPR,                                               0xFC000034, // PC_FRSQRTE
    "FSEL",         "=f,f,f,f",        4,  2, fCanSetRecordBit | fOpTypeFPR,                                               0xFC00002E, // PC_FSEL
    "FRSP",         "=`f,f",           2,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xFC000018, // PC_FRSP
    "FCTIW",        "=f,f",            2,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xFC00001C, // PC_FCTIW
    "FCTIWZ",       "=f,f",            2,  2, fIsCSE | fCanSetRecordBit | fOpTypeFPR,                                      0xFC00001E, // PC_FCTIWZ
    "FCMPU",        "=?c,f,f",         3,  1, fIsCSE | fOpTypeFPR,                                                         0xFC000000, // PC_FCMPU
    "FCMPO",        "=?c,f,f",         3,  1, fIsCSE | fOpTypeFPR,                                                         0xFC000040, // PC_FCMPO
    "LWARX",        "=r,(b,r)",        3,  3, fIsRead | fIsPtrOp,                                                          0x7C000028, // PC_LWARX
    "LSWI",         "=r,b,N",          3,  3, fIsRead | fIsPtrOp | fSideEffects,                                           0x7C0004AA, // PC_LSWI
    "LSWX",         "=r,(b,r)",        3,  3, fIsRead | fIsPtrOp | fSideEffects,                                           0x7C00042A, // PC_LSWX
    "STFIWX",       "f,(b,r)",         3,  3, fIsWrite | fIsPtrOp,                                                         0x7C0007AE, // PC_STFIWX
    "STSWI",        "r,b,N",           3,  3, fIsWrite | fIsPtrOp,                                                         0x7C0005AA, // PC_STSWI
    "STSWX",        "r,(b,r)",         3,  3, fIsWrite | fIsPtrOp,                                                         0x7C00052A, // PC_STSWX
    "STWCX",        "r,(b,r);=Z",      4,  3, fIsWrite | fIsPtrOp | fCanSetRecordBit | fRecordBit,                         0x7C00012D, // PC_STWCX
    "ECIWX",        "=r,(b,r)",        3,  3, fIsRead | fIsPtrOp,                                                          0x7C00026C, // PC_ECIWX
    "ECOWX",        "r,(b,r)",         3,  1, fIsWrite | fIsPtrOp | fSideEffects,                                          0x7C00036C, // PC_ECOWX
    "DCBI",         "b,r",             2,  1, fSideEffects | fOpTypeGPR,                                                   0x7C0003AC, // PC_DCBI
    "ICBI",         "b,r",             2,  1, fSideEffects | fOpTypeGPR,                                                   0x7C0007AC, // PC_ICBI
    "MCRFS",        "=c,x3",           2,  1, fSideEffects | fOpTypeGPR,                                                   0xFC000080, // PC_MCRFS
    "MCRXR",        "=c;+X",           2,  1, fOpTypeGPR,                                                                  0x7C000400, // PC_MCRXR
    "MFTB",         "=r?T",            2,  4, fOpTypeGPR,                                                                  0x7C0002E6, // PC_MFTB
    "MFSR",         "=r,u4",           2,  4, fOpTypeGPR,                                                                  0x7C0004A6, // PC_MFSR
    "MTSR",         "u4,r",            2,  4, fSideEffects | fOpTypeGPR,                                                   0x7C0001A4, // PC_MTSR
    "MFSRIN",       "=r,r",            2,  4, fOpTypeGPR,                                                                  0x7C000526, // PC_MFSRIN
    "MTSRIN",       "r,r",             2,  4, fSideEffects | fOpTypeGPR,                                                   0x7C0001E4, // PC_MTSRIN
    "MTFSB0",       "u5",              1,  4, fSideEffects | fCanSetRecordBit | fOpTypeGPR,                                0xFC00008C, // PC_MTFSB0
    "MTFSB1",       "u5",              1,  4, fSideEffects | fCanSetRecordBit | fOpTypeGPR,                                0xFC00004C, // PC_MTFSB1
    "MTFSFI",       "c,x4",            2,  1, fSideEffects | fCanSetRecordBit | fOpTypeGPR,                                0xFC00010C, // PC_MTFSFI
    "SC",           "",                0,  0, fIsCall | fSideEffects,                                                      0x44000002, // PC_SC
    "FSQRT",        "=f,f",            2,  2, fCanSetRecordBit | fOpTypeFPR,                                               0xFC00002C, // PC_FSQRT
    "FSQRTS",       "=`f,f",           2,  2, fCanSetRecordBit | fOpTypeFPR,                                               0xEC00002C, // PC_FSQRTS
    "TLBIA",        "",                0,  1, fSideEffects,                                                                0x7C0002E4, // PC_TLBIA
    "TLBIE",        "r",               1,  1, fSideEffects,                                                                0x7C000264, // PC_TLBIE
    "TLBLD",        "r",               1,  1, fSideEffects,                                                                0x7C0007A4, // PC_TLBLD
    "TLBLI",        "r",               1,  1, fSideEffects,                                                                0x7C0007E4, // PC_TLBLI
    "TLBSYNC",      "",                0,  1, fSideEffects,                                                                0x7C00046C, // PC_TLBSYNC
    "TW",           "t,r,r",           3,  1, fIsCall | fSideEffects,                                                      0x7C000008, // PC_TW
    "TRAP",         "",                0,  1, fIsCall | fSideEffects,                                                      0x7FE00008, // PC_TRAP
    "TWI",          "t,r,x",           3,  1, fIsCall | fSideEffects,                                                      0x0C000000, // PC_TWI
    "OPWORD",       "w",               1,  0, fSideEffects,                                                                0x00000000, // PC_OPWORD
    "MFROM",        "=r,r",            2,  4, fIsRead,                                                                     0x7C000212, // PC_MFROM
    "DSA",          "",                0,  0, fSideEffects | fOpTypeGPR,                                                   0x7C0004E8, // PC_DSA
    "ESA",          "",                0,  0, fSideEffects | fOpTypeGPR,                                                   0x7C0004A8, // PC_ESA
    "DCCCI",        "b,r",             2,  0, fSideEffects | fOpTypeGPR,                                                   0x7C00038C, // PC_DCCCI
    "DCREAD",       "=r,(b,r)",        3,  0, fSideEffects | fHasBaseReg | fOpTypeGPR,                                     0x7C0003CC, // PC_DCREAD
    "ICBT",         "b,r",             2,  0, fSideEffects | fOpTypeGPR,                                                   0x7C00020C, // PC_ICBT
    "ICCCI",        "b,r",             2,  0, fSideEffects | fOpTypeGPR,                                                   0x7C00078C, // PC_ICCCI
    "ICREAD",       "b,r",             2,  0, fSideEffects | fOpTypeGPR,                                                   0x7C0007CC, // PC_ICREAD
    "RFCI",         "",                0,  0, fSideEffects | fOpTypeGPR,                                                   0x4C000066, // PC_RFCI
    "TLBRE",        "=r,r,u1",         3,  0, fSideEffects | fOpTypeGPR,                                                   0x7C000764, // PC_TLBRE
    "TLBSX",        "=r,(b,r)",        3,  0, fSideEffects | fHasBaseReg | fCanSetRecordBit | fOpTypeGPR,                  0x7C000724, // PC_TLBSX
    "TLBWE",        "=r,r,i",          3,  0, fSideEffects | fOpTypeGPR,                                                   0x7C0007A4, // PC_TLBWE
    "WRTEE",        "r",               1,  0, fSideEffects | fOpTypeGPR,                                                   0x7C000106, // PC_WRTEE
    "WRTEEI",       "u1",              1,  0, fSideEffects | fOpTypeGPR,                                                   0x7C000146, // PC_WRTEEI
    "MFDCR",        "=r,D",            2,  4, fSideEffects | fOpTypeGPR,                                                   0x7C000286, // PC_MFDCR
    "MTDCR",        "D,r",             2,  4, fSideEffects | fOpTypeGPR,                                                   0x7C000386, // PC_MTDCR
    "DCBA",         "b,r",             2,  1, fSideEffects | fOpTypeGPR,                                                   0x7C0005EC, // PC_DCBA
    "ISELGT",       "=r,r,r,c",        4,  2, fOpTypeGPR,                                                                  0x7C00001E, // PC_ISELGT
    "ISELEQ",       "=r,r,r,c",        4,  2, fOpTypeGPR,                                                                  0x7C00005E, // PC_ISELEQ
    "ISELLT",       "=r,r,r,c",        4,  2, fOpTypeGPR,                                                                  0x7C00009E, // PC_ISELLT
    "DSS",          "u2?u2",           2,  1, fSideEffects | fOpTypeGPR,                                                   0x7C00066C, // PC_DSS
    "DSSALL",       "",                0,  1, fSideEffects | fOpTypeGPR,                                                   0x7E00066C, // PC_DSSALL
    "DST",          "b,r,u2?u2",       4,  1, fSideEffects | fOpTypeGPR,                                                   0x7C0002AC, // PC_DST
    "DSTT",         "b,r,u2",          3,  1, fSideEffects | fOpTypeGPR,                                                   0x7E0002AC, // PC_DSTT
    "DSTST",        "b,r,u2?u2",       4,  1, fSideEffects | fOpTypeGPR,                                                   0x7C0002EC, // PC_DSTST
    "DSTSTT",       "b,r,u2",          3,  1, fSideEffects | fOpTypeGPR,                                                   0x7E0002EC, // PC_DSTSTT
    "LVEBX",        "=v,(b,r)",        3,  3, fIsRead | fIsPtrOp | fOpTypeVR,                                              0x7C00000E, // PC_LVEBX
    "LVEHX",        "=v,(b,r)",        3,  3, fIsRead | fIsPtrOp | fOpTypeVR,                                              0x7C00004E, // PC_LVEHX
    "LVEWX",        "=v,(b,r)",        3,  3, fIsRead | fIsPtrOp | fOpTypeVR,                                              0x7C00008E, // PC_LVEWX
    "LVSL",         "=v,(b,r)",        3,  3, fHasBaseReg | fOpTypeVR,                                                     0x7C00000C, // PC_LVSL
    "LVSR",         "=v,(b,r)",        3,  3, fHasBaseReg | fOpTypeVR,                                                     0x7C00004C, // PC_LVSR
    "LVX",          "=v,(b,r)",        3,  3, fIsRead | fIsPtrOp | fOpTypeVR,                                              0x7C0000CE, // PC_LVX
    "LVXL",         "=v,(b,r)",        3,  3, fIsRead | fIsPtrOp | fOpTypeVR,                                              0x7C0002CE, // PC_LVXL
    "STVEBX",       "v,(b,r)",         3,  1, fIsWrite | fIsPtrOp | fOpTypeVR,                                             0x7C00010E, // PC_STVEBX
    "STVEHX",       "v,(b,r)",         3,  1, fIsWrite | fIsPtrOp | fOpTypeVR,                                             0x7C00014E, // PC_STVEHX
    "STVEWX",       "v,(b,r)",         3,  1, fIsWrite | fIsPtrOp | fOpTypeVR,                                             0x7C00018E, // PC_STVEWX
    "STVX",         "v,(b,r)",         3,  1, fIsWrite | fIsPtrOp | fOpTypeVR,                                             0x7C0001CE, // PC_STVX
    "STVXL",        "v,(b,r)",         3,  1, fIsWrite | fIsPtrOp | fOpTypeVR,                                             0x7C0003CE, // PC_STVXL
    "MFVSCR",       "=v",              1,  3, fSideEffects | fOpTypeVR,                                                    0x10000604, // PC_MFVSCR
    "MTVSCR",       "v",               1,  3, fSideEffects | fOpTypeVR,                                                    0x10000644, // PC_MTVSCR
    "VADDCUW",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000180, // PC_VADDCUW
    "VADDFP",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x1000000A, // PC_VADDFP
    "VADDSBS",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000300, // PC_VADDSBS
    "VADDSHS",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000340, // PC_VADDSHS
    "VADDSWS",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000380, // PC_VADDSWS
    "VADDUBM",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000000, // PC_VADDUBM
    "VADDUBS",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000200, // PC_VADDUBS
    "VADDUHM",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000040, // PC_VADDUHM
    "VADDUHS",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000240, // PC_VADDUHS
    "VADDUWM",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000080, // PC_VADDUWM
    "VADDUWS",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000280, // PC_VADDUWS
    "VAND",         "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000404, // PC_VAND
    "VANDC",        "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000444, // PC_VANDC
    "VAVGSB",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000502, // PC_VAVGSB
    "VAVGSH",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000542, // PC_VAVGSH
    "VAVGSW",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000582, // PC_VAVGSW
    "VAVGUB",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000402, // PC_VAVGUB
    "VAVGUH",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000442, // PC_VAVGUH
    "VAVGUW",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000482, // PC_VAVGUW
    "VCFSX",        "=v,v,u5",         3,  2, fIsCSE | fOpTypeVR,                                                          0x1000034A, // PC_VCFSX
    "VCFUX",        "=v,v,u5",         3,  2, fIsCSE | fOpTypeVR,                                                          0x1000030A, // PC_VCFUX
    "VCMPBFP",      "=v,v,v",          3,  2, fCanSetRecordBit | fOpTypeVR,                                                0x100003C6, // PC_VCMPBFP
    "VCMPEQFP",     "=v,v,v",          3,  2, fCanSetRecordBit | fOpTypeVR,                                                0x100000C6, // PC_VCMPEQFP
    "VCMPEQUB",     "=v,v,v",          3,  2, fCanSetRecordBit | fOpTypeVR,                                                0x10000006, // PC_VCMPEQUB
    "VCMPEQUH",     "=v,v,v",          3,  2, fCanSetRecordBit | fOpTypeVR,                                                0x10000046, // PC_VCMPEQUH
    "VCMPEQUW",     "=v,v,v",          3,  2, fCanSetRecordBit | fOpTypeVR,                                                0x10000086, // PC_VCMPEQUW
    "VCMPGEFP",     "=v,v,v",          3,  2, fCanSetRecordBit | fOpTypeVR,                                                0x100001C6, // PC_VCMPGEFP
    "VCMPGTFP",     "=v,v,v",          3,  2, fCanSetRecordBit | fOpTypeVR,                                                0x100002C6, // PC_VCMPGTFP
    "VCMPGTSB",     "=v,v,v",          3,  2, fCanSetRecordBit | fOpTypeVR,                                                0x10000306, // PC_VCMPGTSB
    "VCMPGTSH",     "=v,v,v",          3,  2, fCanSetRecordBit | fOpTypeVR,                                                0x10000346, // PC_VCMPGTSH
    "VCMPGTSW",     "=v,v,v",          3,  2, fCanSetRecordBit | fOpTypeVR,                                                0x10000386, // PC_VCMPGTSW
    "VCMPGTUB",     "=v,v,v",          3,  2, fCanSetRecordBit | fOpTypeVR,                                                0x10000206, // PC_VCMPGTUB
    "VCMPGTUH",     "=v,v,v",          3,  2, fCanSetRecordBit | fOpTypeVR,                                                0x10000246, // PC_VCMPGTUH
    "VCMPGTUW",     "=v,v,v",          3,  2, fCanSetRecordBit | fOpTypeVR,                                                0x10000286, // PC_VCMPGTUW
    "VCTSXS",       "=v,v,u5",         3,  2, fIsCSE | fOpTypeVR,                                                          0x100003CA, // PC_VCTSXS
    "VCTUXS",       "=v,v,u5",         3,  2, fIsCSE | fOpTypeVR,                                                          0x1000038A, // PC_VCTUXS
    "VEXPTEFP",     "=v,v",            2,  2, fIsCSE | fOpTypeVR,                                                          0x1000018A, // PC_VEXPTEFP
    "VLOGEFP",      "=v,v",            2,  2, fIsCSE | fOpTypeVR,                                                          0x100001CA, // PC_VLOGEFP
    "VMAXFP",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x1000040A, // PC_VMAXFP
    "VMAXSB",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000102, // PC_VMAXSB
    "VMAXSH",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000142, // PC_VMAXSH
    "VMAXSW",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000182, // PC_VMAXSW
    "VMAXUB",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000002, // PC_VMAXUB
    "VMAXUH",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000042, // PC_VMAXUH
    "VMAXUW",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000082, // PC_VMAXUW
    "VMINFP",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x1000044A, // PC_VMINFP
    "VMINSB",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000302, // PC_VMINSB
    "VMINSH",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000342, // PC_VMINSH
    "VMINSW",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000382, // PC_VMINSW
    "VMINUB",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000202, // PC_VMINUB
    "VMINUH",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000242, // PC_VMINUH
    "VMINUW",       "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000282, // PC_VMINUW
    "VMRGHB",       "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000000C, // PC_VMRGHB
    "VMRGHH",       "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000004C, // PC_VMRGHH
    "VMRGHW",       "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000008C, // PC_VMRGHW
    "VMRGLB",       "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000010C, // PC_VMRGLB
    "VMRGLH",       "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000014C, // PC_VMRGLH
    "VMRGLW",       "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000018C, // PC_VMRGLW
    "VMULESB",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000308, // PC_VMULESB
    "VMULESH",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000348, // PC_VMULESH
    "VMULEUB",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000208, // PC_VMULEUB
    "VMULEUH",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000248, // PC_VMULEUH
    "VMULOSB",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000108, // PC_VMULOSB
    "VMULOSH",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000148, // PC_VMULOSH
    "VMULOUB",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000008, // PC_VMULOUB
    "VMULOUH",      "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000048, // PC_VMULOUH
    "VNOR",         "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000504, // PC_VNOR
    "VOR",          "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x10000484, // PC_VOR
    "VPKPX",        "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000030E, // PC_VPKPX
    "VPKSHSS",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000018E, // PC_VPKSHSS
    "VPKSHUS",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000010E, // PC_VPKSHUS
    "VPKSWSS",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x100001CE, // PC_VPKSWSS
    "VPKSWUS",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000014E, // PC_VPKSWUS
    "VPKUHUM",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000000E, // PC_VPKUHUM
    "VPKUHUS",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000008E, // PC_VPKUHUS
    "VPKUWUM",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000004E, // PC_VPKUWUM
    "VPKUWUS",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x100000CE, // PC_VPKUWUS
    "VREFP",        "=v,v",            2,  2, fIsCSE | fOpTypeVR,                                                          0x1000010A, // PC_VREFP
    "VRFIM",        "=v,v",            2,  2, fIsCSE | fOpTypeVR,                                                          0x100002CA, // PC_VRFIM
    "VRFIN",        "=v,v",            2,  2, fIsCSE | fOpTypeVR,                                                          0x1000020A, // PC_VRFIN
    "VRFIP",        "=v,v",            2,  2, fIsCSE | fOpTypeVR,                                                          0x1000028A, // PC_VRFIP
    "VRFIZ",        "=v,v",            2,  2, fIsCSE | fOpTypeVR,                                                          0x1000024A, // PC_VRFIZ
    "VRLB",         "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000004, // PC_VRLB
    "VRLH",         "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000044, // PC_VRLH
    "VRLW",         "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000084, // PC_VRLW
    "VRSQRTEFP",    "=v,v",            2,  2, fIsCSE | fOpTypeVR,                                                          0x1000014A, // PC_VRSQRTEFP
    "VSL",          "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x100001C4, // PC_VSL
    "VSLB",         "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000104, // PC_VSLB
    "VSLH",         "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000144, // PC_VSLH
    "VSLO",         "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000040C, // PC_VSLO
    "VSLW",         "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000184, // PC_VSLW
    "VSPLTB",       "=v,v,u5",         3,  2, fIsCSE | fOpTypeVR,                                                          0x1000020C, // PC_VSPLTB
    "VSPLTH",       "=v,v,u5",         3,  2, fIsCSE | fOpTypeVR,                                                          0x1000024C, // PC_VSPLTH
    "VSPLTW",       "=v,v,u5",         3,  2, fIsCSE | fOpTypeVR,                                                          0x1000028C, // PC_VSPLTW
    "VSPLTISB",     "=v,i5",           2,  2, fIsCSE | fOpTypeVR,                                                          0x1000030C, // PC_VSPLTISB
    "VSPLTISH",     "=v,i5",           2,  2, fIsCSE | fOpTypeVR,                                                          0x1000034C, // PC_VSPLTISH
    "VSPLTISW",     "=v,i5",           2,  2, fIsCSE | fOpTypeVR,                                                          0x1000038C, // PC_VSPLTISW
    "VSR",          "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x100002C4, // PC_VSR
    "VSRAB",        "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000304, // PC_VSRAB
    "VSRAH",        "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000344, // PC_VSRAH
    "VSRAW",        "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000384, // PC_VSRAW
    "VSRB",         "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000204, // PC_VSRB
    "VSRH",         "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000244, // PC_VSRH
    "VSRO",         "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000044C, // PC_VSRO
    "VSRW",         "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000284, // PC_VSRW
    "VSUBCUW",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000580, // PC_VSUBCUW
    "VSUBFP",       "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x1000004A, // PC_VSUBFP
    "VSUBSBS",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000700, // PC_VSUBSBS
    "VSUBSHS",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000740, // PC_VSUBSHS
    "VSUBSWS",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000780, // PC_VSUBSWS
    "VSUBUBM",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000400, // PC_VSUBUBM
    "VSUBUBS",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000600, // PC_VSUBUBS
    "VSUBUHM",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000440, // PC_VSUBUHM
    "VSUBUHS",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000640, // PC_VSUBUHS
    "VSUBUWM",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000480, // PC_VSUBUWM
    "VSUBUWS",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000680, // PC_VSUBUWS
    "VSUMSWS",      "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000788, // PC_VSUMSWS
    "VSUM2SWS",     "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000688, // PC_VSUM2SWS
    "VSUM4SBS",     "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000708, // PC_VSUM4SBS
    "VSUM4SHS",     "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000648, // PC_VSUM4SHS
    "VSUM4UBS",     "=v,v,v",          3,  2, fIsCSE | fOpTypeVR,                                                          0x10000608, // PC_VSUM4UBS
    "VUPKHPX",      "=v,v",            2,  2, fIsCSE | fOpTypeVR,                                                          0x1000034E, // PC_VUPKHPX
    "VUPKHSB",      "=v,v",            2,  2, fIsCSE | fOpTypeVR,                                                          0x1000020E, // PC_VUPKHSB
    "VUPKHSH",      "=v,v",            2,  2, fIsCSE | fOpTypeVR,                                                          0x1000024E, // PC_VUPKHSH
    "VUPKLPX",      "=v,v",            2,  2, fIsCSE | fOpTypeVR,                                                          0x100003CE, // PC_VUPKLPX
    "VUPKLSB",      "=v,v",            2,  2, fIsCSE | fOpTypeVR,                                                          0x1000028E, // PC_VUPKLSB
    "VUPKLSH",      "=v,v",            2,  2, fIsCSE | fOpTypeVR,                                                          0x100002CE, // PC_VUPKLSH
    "VXOR",         "=v,v,v",          3,  2, fCommutative | fIsCSE | fOpTypeVR,                                           0x100004C4, // PC_VXOR
    "VMADDFP",      "=v,v,v,v",        4,  2, fIsCSE | fOpTypeVR,                                                          0x1000002E, // PC_VMADDFP
    "VMHADDSHS",    "=v,v,v,v",        4,  2, fIsCSE | fOpTypeVR,                                                          0x10000020, // PC_VMHADDSHS
    "VMHRADDSHS",   "=v,v,v,v",        4,  2, fIsCSE | fOpTypeVR,                                                          0x10000021, // PC_VMHRADDSHS
    "VMLADDUHM",    "=v,v,v,v",        4,  2, fIsCSE | fOpTypeVR,                                                          0x10000022, // PC_VMLADDUHM
    "VMSUMMBM",     "=v,v,v,v",        4,  2, fIsCSE | fOpTypeVR,                                                          0x10000025, // PC_VMSUMMBM
    "VMSUMSHM",     "=v,v,v,v",        4,  2, fIsCSE | fOpTypeVR,                                                          0x10000028, // PC_VMSUMSHM
    "VMSUMSHS",     "=v,v,v,v",        4,  2, fIsCSE | fOpTypeVR,                                                          0x10000029, // PC_VMSUMSHS
    "VMSUMUBM",     "=v,v,v,v",        4,  2, fIsCSE | fOpTypeVR,                                                          0x10000024, // PC_VMSUMUBM
    "VMSUMUHM",     "=v,v,v,v",        4,  2, fIsCSE | fOpTypeVR,                                                          0x10000026, // PC_VMSUMUHM
    "VMSUMUHS",     "=v,v,v,v",        4,  2, fIsCSE | fOpTypeVR,                                                          0x10000027, // PC_VMSUMUHS
    "VNMSUBFP",     "=v,v,v,v",        4,  2, fIsCSE | fOpTypeVR,                                                          0x1000002F, // PC_VNMSUBFP
    "VPERM",        "=v,v,v,v",        4,  2, fIsCSE | fOpTypeVR,                                                          0x1000002B, // PC_VPERM
    "VSEL",         "=v,v,v,v",        4,  2, fIsCSE | fOpTypeVR,                                                          0x1000002A, // PC_VSEL
    "VSLDOI",       "=v,v,v,u4",       4,  2, fIsCSE | fOpTypeVR,                                                          0x1000002C, // PC_VSLDOI
    "VMR",          "=v,v",            2,  2, fIsMove | fOpTypeVR,                                                         0x10000484, // PC_VMR
    "VMRP",         "=v,v",            2,  2, fIsMove | fOpTypeVR,                                                         0x1000002C, // PC_VMRP
    "PSQ_L",        "=:f,d(b),u1,P3|912", 5,  3, fIsRead,                                                                     0xE0000000, // PC_PSQ_L
    "PSQ_LU",       "=:f,d(+b),u1,P3|912", 5,  3, fIsRead | fUpdatesPtr,                                                       0xE4000000, // PC_PSQ_LU
    "PSQ_LX",       "=:f,(b,r),u1,P3|912", 5,  3, fIsRead | fIsPtrOp,                                                          0x1000000C, // PC_PSQ_LX
    "PSQ_LUX",      "=:f,(+b,r),u1,P3|912", 5,  3, fIsRead | fIsPtrOp | fUpdatesPtr,                                            0x1000004C, // PC_PSQ_LUX
    "PSQ_ST",       ":f,d(b),u1,P3|912", 5,  1, fIsWrite,                                                                    0xF0000000, // PC_PSQ_ST
    "PSQ_STU",      ":f,d(+b),u1,P3|912", 5,  2, fIsWrite | fUpdatesPtr,                                                      0xF4000000, // PC_PSQ_STU
    "PSQ_STX",      ":f,(b,r),u1,P3|912", 5,  1, fIsWrite | fIsPtrOp,                                                         0x1000000E, // PC_PSQ_STX
    "PSQ_STUX",     ":f,(+b,r),u1,P3|912", 5,  2, fIsWrite | fIsPtrOp | fUpdatesPtr,                                           0x1000004E, // PC_PSQ_STUX
    "DCBZ_L",       "b,r",             2,  1, fSideEffects | fOpTypeGPR,                                                   0x100007EC, // PC_DCBZ_L
    "PS_ADD",       "=:f,:f,:f",       3,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x1000002A, // PC_PS_ADD
    "PS_SUB",       "=:f,:f,:f",       3,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x10000028, // PC_PS_SUB
    "PS_MUL",       "=:f,:f,:f",       3,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x10000032, // PC_PS_MUL
    "PS_DIV",       "=:f,:f,:f",       3,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x10000024, // PC_PS_DIV
    "PS_MADD",      "=:f,:f,:f,:f",    4,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x1000003A, // PC_PS_MADD
    "PS_MSUB",      "=:f,:f,:f,:f",    4,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x10000038, // PC_PS_MSUB
    "PS_NMADD",     "=:f,:f,:f,:f",    4,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x1000003E, // PC_PS_NMADD
    "PS_NMSUB",     "=:f,:f,:f,:f",    4,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x1000003C, // PC_PS_NMSUB
    "PS_RES",       "=:f,:f",          2,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x10000030, // PC_PS_RES
    "PS_SEL",       "=:f,:f,:f,:f",    4,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x1000002E, // PC_PS_SEL
    "PS_ABS",       "=:f,:f",          2,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x10000210, // PC_PS_ABS
    "PS_NABS",      "=:f,:f",          2,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x10000110, // PC_PS_NABS
    "PS_NEG",       "=:f,:f",          2,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x10000050, // PC_PS_NEG
    "PS_MR",        "=:f,:f",          2,  0, fIsMove | fCanSetRecordBit | fOpTypeFPR,                                     0x10000090, // PC_PS_MR
    "PS_RSQRTE",    "=:f,:f",          2,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x10000034, // PC_PS_RSQRTE
    "PS_CMPU0",     "=c,^f,^f",        3,  1, fOpTypeFPR,                                                                  0x10000000, // PC_PS_CMPU0
    "PS_CMPO0",     "=c,^f,^f",        3,  1, fOpTypeFPR,                                                                  0x10000040, // PC_PS_CMPO0
    "PS_CMPU1",     "=c,.f,.f",        3,  1, fOpTypeFPR,                                                                  0x10000080, // PC_PS_CMPU1
    "PS_CMPO1",     "=c,.f,.f",        3,  1, fOpTypeFPR,                                                                  0x100000C0, // PC_PS_CMPO1
    "PS_MERGE00",   "=:f,^f,^f",       3,  0, fCanSetRecordBit | fOpTypeFPR,                                               0x10000420, // PC_PS_MERGE00
    "PS_MERGE01",   "=:f,^f,.f",       3,  0, fCanSetRecordBit | fOpTypeFPR,                                               0x10000460, // PC_PS_MERGE01
    "PS_MERGE10",   "=:f,.f,^f",       3,  0, fCanSetRecordBit | fOpTypeFPR,                                               0x100004A0, // PC_PS_MERGE10
    "PS_MERGE11",   "=:f,.f,.f",       3,  0, fCanSetRecordBit | fOpTypeFPR,                                               0x100004E0, // PC_PS_MERGE11
    "PS_SUM0",      "=:f,^f,.f,.f",    4,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x10000014, // PC_PS_SUM0
    "PS_SUM1",      "=:f,^f,^f,.f",    4,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x10000016, // PC_PS_SUM1
    "PS_MULS0",     "=:f,:f,^f",       3,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x10000018, // PC_PS_MULS0
    "PS_MULS1",     "=:f,:f,.f",       3,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x1000001A, // PC_PS_MULS1
    "PS_MADDS0",    "=:f,:f,^f,:f",    4,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x1000001C, // PC_PS_MADDS0
    "PS_MADDS1",    "=:f,:f,.f,:f",    4,  2, fCanSetRecordBit | fOpTypeFPR,                                               0x1000001E, // PC_PS_MADDS1
    "SLE",          "=r,r,r",          3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C000132, // PC_SLE
    "SLEQ",         "=r,r,r",          3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C0001B2, // PC_SLEQ
    "SLIQ",         "=r,r,u5",         3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C000170, // PC_SLIQ
    "SLLIQ",        "=r,r,u5",         3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C0001F0, // PC_SLLIQ
    "SLLQ",         "=r,r,r",          3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C0001B0, // PC_SLLQ
    "SLQ",          "=r,r,r",          3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C000130, // PC_SLQ
    "SRAIQ",        "=r,r,u5",         3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C000770, // PC_SRAIQ
    "SRAQ",         "=r,r,r",          3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C000730, // PC_SRAQ
    "SRE",          "=r,r,r",          3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C000532, // PC_SRE
    "SREA",         "=r,r,r",          3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C000732, // PC_SREA
    "SREQ",         "=r,r,r",          3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C0005B2, // PC_SREQ
    "SRIQ",         "=r,r,u5",         3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C000570, // PC_SRIQ
    "SRLIQ",        "=r,r,u5",         3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C0005F0, // PC_SRLIQ
    "SRLQ",         "=r,r,r",          3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C0005B0, // PC_SRLQ
    "SRQ",          "=r,r,r",          3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C000530, // PC_SRQ
    "MASKG",        "=r,r,r",          3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C00003A, // PC_MASKG
    "MASKIR",       "=r,r,r",          3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C00043A, // PC_MASKIR
    "LSCBX",        "=r,(b,r)",        3,  3, fIsRead | fIsPtrOp | fSideEffects | fCanSetRecordBit,                        0x7C00022A, // PC_LSCBX
    "DIV",          "=r,r,r",          3,  2, fCanSetCarry | fCanSetRecordBit | fOpTypeGPR,                                0x7C000296, // PC_DIV
    "DIVS",         "=r,r,r",          3,  2, fCanSetCarry | fCanSetRecordBit | fOpTypeGPR,                                0x7C0002D6, // PC_DIVS
    "DOZ",          "=r,r,r",          3,  2, fCanSetCarry | fCanSetRecordBit | fOpTypeGPR,                                0x7C000210, // PC_DOZ
    "MUL",          "=r,r,r",          3,  2, fCanSetCarry | fCanSetRecordBit | fOpTypeGPR,                                0x7C0000D6, // PC_MUL
    "NABS",         "=r,r",            2,  2, fCanSetCarry | fCanSetRecordBit | fOpTypeGPR,                                0x7C0003D0, // PC_NABS
    "ABS",          "=r,r",            2,  2, fCanSetCarry | fCanSetRecordBit | fOpTypeGPR,                                0x7C0002D0, // PC_ABS
    "CLCS",         "=r,r",            2,  2, fOpTypeGPR,                                                                  0x7C000426, // PC_CLCS
    "DOZI",         "=r,r,u5",         3,  2, fOpTypeGPR,                                                                  0x24000000, // PC_DOZI
    "RLMI",         "+r,r,r,u5,u5",    5,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x58000000, // PC_RLMI
    "RRIB",         "=r,r,r",          3,  2, fCanSetRecordBit | fOpTypeGPR,                                               0x7C000432, // PC_RRIB
    "PENTRY",       "",                0,  0, 0,                                                                           0x00000000, // PC_PENTRY
    "PEXIT",        "",                0,  0, 0,                                                                           0x00000000 // PC_PEXIT
};
// clang-format on
