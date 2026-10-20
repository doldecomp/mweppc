#ifndef DRIVER_CLFILES_H
#define DRIVER_CLFILES_H

#include "compiler/common.h"
#include "driver/cmdline.h"
#include "driver/CLDependencies.h"
#include "driver/Memory.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

struct VFile {
    char displayName[32];
    Handle data;
    struct VFile* next;
};
struct File {
    struct File* next;
    SInt32 filenum;
    UInt16 segnum;
    OSTime srcmoddate;
    OSTime outmoddate;
    char srcfilename[256];
    char outfilename[256];
    SInt16 outfileowner;
    OSSpec srcfss;
    OSSpec outfss;
    SInt16 writeToDisk;
    SInt16 wroteToDisk;
    SInt16 tempOnDisk;
    struct Plugin* compiler;
    SInt32 dropinflags;
    SInt32 objectflags;
    SInt32 mappingflags;
    SInt16 sourceUsage;
    SInt16 objectUsage;
    Handle textdata;
    Handle objectdata;
    Handle browsedata;
    SInt32 codesize;
    SInt32 udatasize;
    SInt32 idatasize;
    SInt32 compiledlines;
    Boolean recompileDependents;
    Boolean gendebug;
    Boolean hasobjectcode;
    Boolean hasresources;
    Boolean isresourcefile;
    Boolean weakimport;
    Boolean initbefore;
    Boolean mergeintooutput;
    Deps deps;
    Boolean recordbrowseinfo;
    SInt16 browseFileID;
    char browseoptions[32];
    OSType filetype;
    OSType filecreator;
};
struct Files {
    File* fileList;
    SInt32 fileCount;
};
extern File* File_New(void);
extern Boolean Files_Initialize(Files* files);
extern Boolean Files_Terminate(Files* files);
extern Boolean Files_AddFile(Files* files, File* file);
extern Boolean Files_InsertFile(Files* files, File* file, SInt32 position);
extern File* Files_GetFile(Files* files, SInt32 filenum);
extern File* Files_FindFile(Files* files, OSSpec* spec);
extern int Files_Count(Files* files);
extern Boolean VFiles_Initialize(VFile** list);
extern void VFiles_Terminate(VFile** list);
extern VFile* VFile_New(const char* name, Handle data);
extern Boolean VFiles_Add(VFile** list, VFile* entry);
extern VFile* VFiles_Find(VFile* list, const char* name);

#ifdef __cplusplus
}
#endif

#endif
