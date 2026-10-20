#ifndef COMPILER_USEDEFCHAINS_H
#define COMPILER_USEDEFCHAINS_H

// UseDefChains.c: the use-def chains of registers and memory over the PCode. From 2.5 on it counts
// each object's direct and indirect uses and definitions, and keeps a block's local sets as a range
// (gen) and a compressed vector (kill).

#include "compiler/common.h"
#include "compiler/BitVectors.h"
#include "compiler/PCodeInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
// 2-byte packing: a UseOrDef is 10 bytes
typedef struct TinyValue {
    PCOpKind kind;
    RegClass arg; // a register's class; for memory 0 (the object itself) or 1 (may alias it)
    union {
        short reg;
        Object* object;
    } u;
} TinyValue;

struct UseOrDef {
    PCode* pcode;
    TinyValue v;
};
#pragma pack(pop)

struct RegUseOrDef {
    RegUseOrDef* next;
    int id;
};

struct ObjectUseDef {
    ObjectUseDef* next;
    ObjectUseDef* leftchild;
    ObjectUseDef* rightchild;
    Object* object;
    RegUseOrDef* uses;
    RegUseOrDef* defs;
#if VERSION >= VERSION_GC_2_5
    // the uses and definitions of the object itself (direct) and of what may alias it (indirect)
    short directdefs;
    short directuses;
    short indirectdefs;
    short indirectuses;
#endif
};

// NOTE: invented names: defgen, defkill, defin, defout, usegen, usekill, usein, useout
typedef struct UseDefInfo {
#if VERSION >= VERSION_GC_2_5
    // the block's uses are [usestart, usestart + usecount), its definitions likewise
    SInt32 usestart;
    SInt32 usecount;
    SInt32 defstart;
    SInt32 defcount;
#endif
    // the reaching definitions and the live uses of the block
#if VERSION >= VERSION_GC_2_5
    BitVectorRange* defgen; // a range over the block's definitions
    CompressedBitVector* defkill;
#else
    UInt32* defgen;
    UInt32* defkill;
#endif
    UInt32* defin;
    UInt32* defout;
#if VERSION >= VERSION_GC_2_5
    BitVectorRange* usegen; // a range over the block's uses
    CompressedBitVector* usekill;
#else
    UInt32* usegen;
    UInt32* usekill;
#endif
    UInt32* usein;
    UInt32* useout;
} UseDefInfo;

extern int number_of_Defs;
extern UseOrDef* Defs;
extern RegUseOrDef** reg_Defs[RegClassMax];
extern int number_of_Uses;
extern UseOrDef* Uses;
extern RegUseOrDef** reg_Uses[RegClassMax];
extern UseDefInfo* usedefinfo;

extern ObjectUseDef* findobjectusedef(Object* object);
extern void computeusedefchains(int includeMemory);
#if VERSION >= VERSION_GC_2_5
// (2.0 has it in CopyPropagation.c)
extern int precedes(PCode* a, PCode* b);
extern int canidate_reaches_use(
    PCode* candidate, int defID, int reg, PCode* use, UInt32* defvec, RegClass rclass
);
#endif

#ifdef __cplusplus
}
#endif

#endif
