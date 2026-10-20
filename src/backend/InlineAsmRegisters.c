#include "compiler/InlineAsmRegisters.h"
#include "compiler/CompilerTools.h"
#include <string.h>

#pragma pack(push, 2)
typedef struct HashedRegister {
    struct HashedRegister* next;
    IARegister reg;
} HashedRegister;
#pragma pack(pop)

static HashedRegister* hashedregisters[64];

void InlineAsm_InitializeRegisters(void) {
    SInt32 i;

    for (i = 0; i < 64; i++) {
        hashedregisters[i] = NULL;
    }
}

void InlineAsm_InsertRegister(char* name, RegClass rclass, short num, Object* object) {
    HashedRegister** ptr;
    HashedRegister* hr;

    ptr = hashedregisters + (CHash(name) & 63);
    hr = lalloc(sizeof(HashedRegister));
    hr->reg.name = name;
    hr->reg.rclass = rclass;
    hr->reg.num = num;
    hr->reg.object = object;

    hr->next = *ptr;
    *ptr = hr;
}

IARegister* InlineAsm_LookupRegister(char* name) {
    HashedRegister* scan;
    IARegister* reg;

    for (scan = hashedregisters[CHash(name) & 63]; scan; scan = scan->next) {
        reg = &scan->reg;
        if (!strcmp(reg->name, name)) {
            return reg;
        }
    }

    return NULL;
}
