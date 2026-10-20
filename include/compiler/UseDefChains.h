#ifndef COMPILER_USEDEFCHAINS_H
#define COMPILER_USEDEFCHAINS_H

#include "compiler/common.h"
#include "compiler/BitVectors.h"
#include "compiler/PCodeInfo.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
typedef struct TinyValue {
    PCOpKind kind;
    RegClass arg;
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
    short directdefs;
    short directuses;
    short indirectdefs;
    short indirectuses;
#endif
};

typedef struct UseDefInfo {
#if VERSION >= VERSION_GC_2_5
    SInt32 usestart;
    SInt32 usecount;
    SInt32 defstart;
    SInt32 defcount;
#endif
#if VERSION >= VERSION_GC_2_5
    BitVectorRange* defgen;
    CompressedBitVector* defkill;
#else
    UInt32* defgen;
    UInt32* defkill;
#endif
    UInt32* defin;
    UInt32* defout;
#if VERSION >= VERSION_GC_2_5
    BitVectorRange* usegen;
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
extern int precedes(PCode* a, PCode* b);
extern int canidate_reaches_use(
    PCode* candidate, int defID, int reg, PCode* use, UInt32* defvec, RegClass rclass
);
#endif

#ifdef __cplusplus
}
#endif

#endif
