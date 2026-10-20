#ifndef COMPILER_PCODE_H
#define COMPILER_PCODE_H

#include "compiler/common.h"
#include "compiler/PCodeInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

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

enum {
    EffectRead = 1,
    EffectWrite = 2,
    EffectLastUse = 4,
    EffectPinned = 8,
    EffectSpilled = 0x40
};

struct PCodeArg {
    PCOpKind kind;
    RegClass arg;
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
        unsigned char placeholder[10];
    } data;
};
enum {
    EffectSingle = 0x2000,
    EffectPS0 = 0x4000,
    EffectPS1 = 0x8000
};

enum {
    fIsBranch = 1,
    fIsRead = 2,
    fIsWrite = 4,
    fIsCall = 8,
    fIsMove = 0x10,
    fIsConst = 0x40,
    fIsVolatile = 0x80,
    fSideEffects = 0x100,
    fCommutative = 0x2000,
    fIsCSE = 0x4000,
    fIsArgInit = 0x8000,
    fOpFromMemory = 0x20000,
    fOpToMemory = 0x40000,
    fMWCATSExit = 0x80000,
    fLink = 0x1000000,
    fCanLink = 0x2000000,
    fBranchNotTaken = 0x4000000,
    fBranchTaken = 0x8000000,
    fAbsolute = 0x10000000,
    fCanBeAbsolute = 0x20000000,
    fIsPtrOp = 0x20,
    fHasBaseReg = 0x400000,
    fOverflow = 0x800000,
    fUpdatesPtr = 0x2000000,
    fCanSetCarry = 0x4000000,
    fCanSetRecordBit = 0x8000000,
    fSetsCarry = 0x10000000,
    fRecordBit = 0x20000000,
    fOpTypeFPR = 0x40000000,
    fOpTypeGPR = 0x80000000,
    fOpTypeVR = 0xC0000000,
    fOpTypeMask = 0xC0000000
};

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
    PCodeArg args[1];
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
    int codeOffset;
    int loopWeight;
    short pcodeCount;
    unsigned short flags;
};
struct PCodeLabel {
    struct PCodeLabel* nextLabel;
    struct PCodeBlock* block;
    short resolved;
    UInt16 index;
};
#pragma pack(pop)
enum {
    fIsProlog = 1,
    fIsEpilogue = 2,
    fVisited = 4,
    fScheduled = 8,
    fDeleted = 0x20,
    fInnerLoop = 0x2000,
    fUnrolled = 0x4000,
    fEntryPoint = 0x8000
};

#define SPR_GQR0 912
#define SPR_GQR1 913

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
extern void setpcodeflags(unsigned int flags);
extern int pccomputeoffsets(void);
extern void computedepthfirstordering(void);

#ifdef __cplusplus
}
#endif

#endif
