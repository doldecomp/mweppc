#ifndef COMPILER_DWARF_H
#define COMPILER_DWARF_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void DWARF1_AddToFunctionRefList(Object* object);
extern void InitDebugSections(ObjGenSection* infoSection, ObjGenSection* lineSection);
extern void InitDwarf(void);
extern void DWARF_DeclareData(Object* object);
extern void newDWARF_param(Object* object, SInt32 unused);
extern void newDWARF_local(Object* object, SInt32 unused);
extern void endDWARF_file(void);
extern void tweakDWARF_file(void);
extern void endDWARF_function(void);
extern int newDWARF_function(
    Object* object, SInt32 codeSize, SInt32 codeOffset, ObjGenSection* section
);
extern void correctDWARF_line(unsigned int line, unsigned int offset);
extern void newDWARF_line(unsigned int line, unsigned int offset);
extern void newDWARF_file(void* osection);

#ifdef __cplusplus
}
#endif

#endif
