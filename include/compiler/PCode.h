#ifndef COMPILER_PCODE_H
#define COMPILER_PCODE_H

#include "compiler/common.h"
#include "compiler/PCodeInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

// (2-byte packing keeps a PCodeArg 12 bytes: labeldiff's SInt16 offset is followed by pointers)
#pragma pack(push, 2)
#define PC_OP_IS_REGISTER(_op, _rclass, _reg) \
    ((_op)->kind == PCOp_REGISTER && (_op)->arg == (_rclass) && (_op)->data.reg.reg == (_reg))
#define PC_OP_IS_READ_REGISTER(_op, _rclass, _reg) \
    ((_op)->kind == PCOp_REGISTER && \
     (_op)->arg == (_rclass) && \
     (_op)->data.reg.reg == (_reg) && \
     ((_op)->data.reg.effect & EffectRead))
#define PC_OP_IS_WRITE_REGISTER(_op, _rclass, _reg) \
    ((_op)->kind == PCOp_REGISTER && \
     (_op)->arg == (_rclass) && \
     (_op)->data.reg.reg == (_reg) && \
     ((_op)->data.reg.effect & EffectWrite))
#define PC_OP_IS_R_OR_W_REGISTER(_op, _rclass, _reg) \
    ((_op)->kind == PCOp_REGISTER && \
     (_op)->arg == (_rclass) && \
     (_op)->data.reg.reg == (_reg) && \
     ((_op)->data.reg.effect & (EffectRead | EffectWrite)))
#define PC_OP_IS_ANY_REGISTER(_op, _rclass) \
    ((_op)->kind == PCOp_REGISTER && (_op)->arg == (_rclass))
#define PC_OP_IS_READ_ANY_REGISTER(_op, _rclass) \
    ((_op)->kind == PCOp_REGISTER && \
     (_op)->arg == (_rclass) && \
     ((_op)->data.reg.effect & EffectRead))
#define PC_OP_IS_WRITE_ANY_REGISTER(_op, _rclass) \
    ((_op)->kind == PCOp_REGISTER && \
     (_op)->arg == (_rclass) && \
     ((_op)->data.reg.effect & EffectWrite))

// NOTE: invented names: EffectLastUse, EffectPinned, EffectSpilled
// A register operand's effect (PCodeArg.data.reg.effect)
enum {
    EffectRead = 1,
    EffectWrite = 2,
    EffectLastUse = 4, // a read after which the register is dead (buildinterferencegraph)
    EffectPinned = 8, // an exception action's read, which copy propagation leaves alone
    EffectSpilled = 0x40 // rewritten to a spill temporary (insertspillcode)
};

struct PCodeArg {
    PCOpKind kind;
    RegClass arg; // (a register's class; a memory operand's relocation type)
    union {
        struct {
            unsigned short effect;
            short reg;
        } reg;
        struct {
            SInt32 value;
            struct Object* obj;
        } imm;
        struct {
            SInt32 offset;
            struct Object* obj;
        } mem;
        struct {
            struct PCodeLabel* label;
        } label;
        struct {
            SInt16 offset;
            struct PCodeLabel* labelA;
            struct PCodeLabel* labelB;
        } labeldiff;
        unsigned char placeholder[10]; // (keeps the operand 12 bytes)
    } data;
};
// NOTE: invented names: EffectSingle, EffectPS0, EffectPS1
// A floating-point register operand's paired-single halves (Gekko), from the operand's format
// character: `.` the first, `^` the second, `:` both, `` ` `` a single-precision value (lfs, stfs;
// which half is which is read from psq_l's W bit: a W=1 load clears EffectPS1)
enum {
    EffectSingle = 0x2000,
    EffectPS0 = 0x4000,
    EffectPS1 = 0x8000
};

// NOTE: invented names: fOpFromMemory, fOpToMemory, fMWCATSExit, fHasBaseReg
// PCode flags
enum {
    fIsBranch = 1,
    fIsRead = 2, // (reads memory)
    fIsWrite = 4, // (writes memory)
    fIsCall = 8,
    fIsMove = 0x10, // (moves a register: mcrf, mr, fmr, vmr, vmrp)
    // (any instruction)
    fIsConst = 0x40,
    fIsVolatile = 0x80,
    fSideEffects = 0x100, // (does other things to the processor's state)
    fCommutative = 0x2000,
    fIsCSE = 0x4000,
    fIsArgInit = 0x8000, // (stores a varargs function's GPR arguments to the stack)
    // an operation with a memory operand that reads it or writes it, other than a plain load or
    // store (ValueNumbering's operatefrommemory/operatetomemory); no opcode sets either
    fOpFromMemory = 0x20000,
    fOpToMemory = 0x40000,
    // an exit MWCATS records (MWCATS_AddExit; nothing in the compiler sets it)
    fMWCATSExit = 0x80000,
    // (branches only)
    fLink = 0x1000000,
    fCanLink = 0x2000000,
    fBranchNotTaken = 0x4000000,
    fBranchTaken = 0x8000000,
    fAbsolute = 0x10000000,
    fCanBeAbsolute = 0x20000000,
    // (other instructions only)
    fIsPtrOp = 0x20,
    // its second operand is a base register (rA|0: r0 reads as zero), as a load's or store's is:
    // addi, addis, dcread, tlbsx, lvsl, lvsr
    fHasBaseReg = 0x400000,
    fOverflow = 0x800000,
    fUpdatesPtr = 0x2000000, // (lbzu, lbzux, lwzu, stbu, stbux, ...)
    fCanSetCarry = 0x4000000,
    fCanSetRecordBit = 0x8000000,
    fSetsCarry = 0x10000000,
    fRecordBit = 0x20000000,
    fOpTypeFPR = 0x40000000,
    fOpTypeGPR = 0x80000000,
    fOpTypeVR = 0xC0000000,
    fOpTypeMask = 0xC0000000
};

/* (the flags that mean one thing on a branch or call and another elsewhere) */
#define FLAG_SET_T(flags) (((flags) & (fIsBranch | fIsCall)) ? (flags) : 0)
#define FLAG_SET_F(flags) (((flags) & (fIsBranch | fIsCall)) ? 0 : (flags))
#define PCODE_FLAG_SET_T(pcode) \
    ((((PCode*)(pcode))->flags & (fIsBranch | fIsCall)) ? ((PCode*)(pcode))->flags : 0)
#define PCODE_FLAG_SET_F(pcode) \
    ((((PCode*)(pcode))->flags & (fIsBranch | fIsCall)) ? 0 : ((PCode*)(pcode))->flags)

struct PCode {
    struct PCode* nextPCode;
    struct PCode* prevPCode;
    struct PCodeBlock* block;
    int useID;
    int defID;
    UInt32 flags;
    struct Alias* alias;
    SInt32 sourceoffset;
    short op;
    short argCount;
    PCodeArg args[1]; // (argCount of them)
};
struct PCLink {
    struct PCLink* nextLink;
    struct PCodeBlock* block;
};
struct PCodeBlock {
    struct PCodeBlock* nextBlock;
    struct PCodeBlock* prevBlock;
    struct PCodeLabel* labels;
    PCLink* predecessors;
    PCLink* successors;
    PCode* firstPCode;
    PCode* lastPCode;
    int blockIndex;
    int codeOffset; // (in bytes)
    int loopWeight;
    short pcodeCount;
    unsigned short flags;
};
struct PCodeLabel {
    struct PCodeLabel* nextLabel;
    // (until pclabel resolves it, the PCLinks that branch to it, chained through block)
    struct PCodeBlock* block;
    short resolved;
    UInt16 index;
};
#pragma pack(pop)
// NOTE: invented names: fInnerLoop, fUnrolled, fEntryPoint
// PCodeBlock flags
enum {
    fIsProlog = 1,
    fIsEpilogue = 2,
    fVisited = 4,
    fScheduled = 8,
    fDeleted = 0x20,
    fInnerLoop = 0x2000, // in an innermost loop (LoopDetection; insert_align_nops aligns its head)
    fUnrolled = 0x4000, // code the IR optimizer unrolled (a statement's StmtFlag_Unrolled)
    // an asm function's `entry` (Peephole does not merge it into its predecessor)
    fEntryPoint = 0x8000
};

// The paired-single quantization register GQR0 (SPR 912), which Gekko's psq_* operands name and the
// compiler uses for unscaled floats; a number, as the other SPR operands are
#define SPR_GQR0 912 // NOTE: invented name
// GQR1 (SPR 913), which InstrSelection sets to a quantization type (u8, u16, s8, s16) for the
// psq_l/psq_st of a paired-single conversion
#define SPR_GQR1 913 // NOTE: invented name

extern PCodeBlock* pcbasicblocks;
extern PCodeBlock* pclastblock;
extern PCodeBlock* prologue;
extern PCodeBlock* epilogue;
extern PCodeBlock** depthfirstordering;
extern int pcblockcount;
extern unsigned int pcloopweight;

extern void initpcode(void);
extern PCode* makepcode(VarArgShort op, ...);
extern void emitpcode(VarArgShort op, ...);
extern PCode* copypcode(PCode* pcode);
extern PCodeLabel* makepclabel(void);
extern PCodeBlock* makepcblock(void);
extern void pclabel(PCodeBlock* block, PCodeLabel* label);
extern void pcbranch(PCodeBlock* block, PCodeLabel* label);
extern void pccomputepredecessors(void);
extern void deleteunreachableblocks(void);
extern void appendpcode(PCodeBlock* block, PCode* pcode);
extern void deletepcode(PCode* pcode);
extern void insertpcodebefore(PCode* anchor, PCode* newpcode);
extern void insertpcodeafter(PCode* anchor, PCode* newpcode);
extern void setpcodeflags(unsigned int flags); // (unsigned: Coerce_to_register's code needs it)
extern int pccomputeoffsets(void);
extern void computedepthfirstordering(void);

#ifdef __cplusplus
}
#endif

#endif
