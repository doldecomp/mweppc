#include "driver/ELF_Lib_Import.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/ELF_Endian.h"
#include "driver/COSToolsCLT.h"
#include "driver/CWPlugins.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/CWSecretPluginCallbacks.h"
#include "driver/DateTimeUtils.h"
#include "driver/DropInCompilerLinker.h"
#include "driver/Files.h"
#include "driver/LinkErrorsPPC_EABI.h"
#include "driver/Memory.h"
#include "driver/oslib.h"
#include "driver/pref_structs.h"
#include "driver/StringUtils.h"
#include <ctype.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

jmp_buf linkerexit;
SInt32 objudatasize;
SInt32 Imported_Obj_Codesize;
SInt32 linkererrors;
LinkInfo linkinfo;
SInt32 objidatasize;
SInt32 linkerwarnings;
char Target64;

struct LibImportCU {
    struct CWPluginPrivateContext* context;
    CWObjectData object;
    SInt32 objectdata;
    SInt32 browsedata;
    SInt32 request;
    SInt32 version;
    CWFileSpec targetfile;
    SInt32 numfiles;
    SInt32 whichfile;
    CWFileSpec sourcefile;
    const char* sourcetext;
    SInt32 sourcetextsize;
    Boolean precompile;
    Boolean autoprecompile;
    Boolean preprocess;
    Boolean SYMinfo;
    Boolean caching_includes;
    CWBrowseOptions browseoptions;
    Boolean recordbrowseinfo;
    SInt16 browserfileID;
    SInt32 targetOS;
    SInt32 targetCPU;
    char* idetargetname;
};

typedef enum Import_Types {
    Unknown_File,
    Archive_Member,
    Archive_Object_Member,
    Archive_String_Table,
    Object_File
} Import_Types;

typedef struct ArchiveInfo {
    struct ArchiveInfo* next;
    UInt8 type;
    char** dataHandle;
    char* header;
} ArchiveInfo;

static struct LibImportCU libimp_cu;
static UInt8 nonNativeByteOrder;

ArchiveInfo* Link_GetFileText(CWFileSpec* name, SInt16* err);
void DeleteArchiveInfo(ArchiveInfo* args);
short Import_File(ArchiveInfo* record, char* buffer);

void out_of_memory() {
    linkfailed(13, memFullErr);
}

void ReportLinkerError(char* message, char* line2) {
    CWReportMessage(libimp_cu.context, NULL, message, line2, 2, 0);
}

void ReportLinkerWarning(char* message, char* line2) {
    CWReportMessage(libimp_cu.context, NULL, message, line2, 1, 0);
}

UInt8 IsELF(char* data) {
    char magic[4] = {0x7F, 'E', 'L', 'F'};

    if (strncmp(data, magic, sizeof(magic)) == 0) {
        return TRUE;
    }
    return FALSE;
}

char** ImportObjectLibrary(
    CWFileSpec* name, SInt32* codesize, SInt32* udatasize, SInt32* idatasize
) {
    short err;
    int length;
    ArchiveInfo* rec;
    char** contents;
    char buf[256];

    nonNativeByteOrder = (!copts.littleendian) != 0;
    linkererrors = linkerwarnings = 0;

    err = 0;
    rec = Link_GetFileText(name, &err);
    if (err != 0 || rec == NULL) {
        linkerror(0x1d, name->name);
        if (rec != NULL) {
            DeleteArchiveInfo(rec);
        }
        return NULL;
    }
    if (rec->type == Unknown_File) {
        linkerror(0x1e, name->name);
        DeleteArchiveInfo(rec);
        return NULL;
    }
    Imported_Obj_Codesize = 0;
    objidatasize = 0;
    objudatasize = 0;
    p2cstrcpy(buf, name->name);
    err = Import_File(rec, buf);
    if (err != 0) {
        DeleteArchiveInfo(rec);
        return NULL;
    }
    contents = COS_NewOSHandle(length = 8);
    if (contents == NULL) {
        contents = COS_NewHandle(length);
        if (contents == NULL) {
            linkerror(0x1d, name->name);
            DeleteArchiveInfo(rec);
            return NULL;
        }
    }
    COS_LockHandle(contents);
    if (rec->type == Archive_Member) {
        strncpy(*contents, "!<arch>\n", length);
    } else {
        COS_LockHandle(rec->dataHandle);
        memcpy(*contents, *rec->dataHandle, length);
        COS_UnlockHandle(rec->dataHandle);
    }
    COS_UnlockHandle(contents);
    DeleteArchiveInfo(rec);
    *codesize = Imported_Obj_Codesize;
    *idatasize = objidatasize;
    *udatasize = objudatasize;
    return contents;
}

char ELF_Lib_Import_setup_param_block(CWPluginPrivateContext* handle) {
    libimp_cu.context = handle;
    memset(&libimp_cu.object, 0, sizeof(CWObjectData));
    if (CWGetMainFileNumber(handle, &libimp_cu.whichfile) != 0) {
        return FALSE;
    }
    if (CWGetMainFileSpec(handle, &libimp_cu.sourcefile) != 0) {
        return FALSE;
    }
    if (CWIsGeneratingDebugInfo(handle, &libimp_cu.SYMinfo) != 0) {
        return FALSE;
    }
    return TRUE;
}

unsigned int store_compile_results(void) {
    unsigned int result = libimp_cu.objectdata;

    if (result) {
        result =
            CWSecretAttachHandle(libimp_cu.context, (Handle)result, &libimp_cu.object.objectdata);
        if (result) {
            return result;
        }
    }
    result = CWStoreObjectData(libimp_cu.context, libimp_cu.whichfile, &libimp_cu.object);
    if (result) {
        return result;
    }
    return result;
}

#pragma optimization_level 2
CWPLUGIN_ENTRY(ELFImport_main)(CWPluginPrivateContext* context) {
    int result;
    char** status;
    long mode;
    PProject project;
    PLinker linker;
    Handle projectHandle;
    Handle linkerHandle;

    result = cwNoErr;
    CWGetPluginRequest(context, &mode);
    switch (mode) {
        case reqInitialize:
            result = cwNoErr;
            break;
        case reqTerminate:
            result = cwNoErr;
            break;
        case reqCompile:
            memset(&copts, 0, 264);
            memset(&linkinfo, 0, sizeof(linkinfo));
            projectHandle = NULL;
            CWSecretGetNamedPreferences(context, "PPC EABI Project", &projectHandle);
            if (projectHandle != NULL) {
                project = *((PProject*)*projectHandle);
            }
            copts.littleendian = !project.endian;
            linkerHandle = NULL;
            CWSecretGetNamedPreferences(context, "PPC EABI Linker", &linkerHandle);
            if (linkerHandle != NULL) {
                linker = *((PLinker*)*linkerHandle);
            }
            linkinfo.nolinkwarnings = linker.suppresswarn;
            Target64 = 0;
            ELF_Lib_Import_setup_param_block(context);
            libimp_cu.browsedata = 0;
            libimp_cu.object.compiledlines = 0;
            libimp_cu.objectdata = 0;
            if (initgheap(out_of_memory) != 0) {
                out_of_memory();
                result = cwErrRequestFailed;
                break;
            }
            status = ImportObjectLibrary(
                &libimp_cu.sourcefile,
                &libimp_cu.object.codesize,
                &libimp_cu.object.udatasize,
                &libimp_cu.object.idatasize
            );
            libimp_cu.objectdata = (unsigned int)status;
            if (status != NULL) {
                result = store_compile_results();
            } else {
                result = cwErrRequestFailed;
            }
            releasegheap();
            break;
    }
    result = CWDonePluginRequest(context, (short)result);
    return result;
}

void MyAlert(char* message) {
    CWAlert(libimp_cu.context, message, NULL, NULL, 0);
}

void OSAlert(char* message, short error) {
    CWReportMessage(libimp_cu.context, NULL, message, NULL, 2, error);
}

static UInt32 next_break_check;

void Linker_UserBreakCheck(void) {
    if (TickCount() > next_break_check) {
        if (CWUserBreak(libimp_cu.context) == 1) {
            longjmp(linkerexit, 1);
        }
        next_break_check = TickCount() + 15;
    }
}

static char mwmagic[] = "mw";

unsigned char IsMWLibraryFile(CWFileSpec* input) {
    Boolean ok;
    short ref;
    SInt32 count;
    SInt32 header[2];

    count = 8;
    ok = FALSE;
    if (FSpOpenDF(input, 1, &ref) == 0) {
        if (FSRead(ref, &count, header) == 0 && count == 8) {
            ok = TRUE;
        }
        FSClose(ref);
    }
    if (ok) {
        if (IsELF((char*)header)) {
            return Object_File;
        }
        if (strncmp((char*)header, "!<arch>\n", 8) == 0) {
            return Archive_Member;
        }
        if (header[0] == mwmagic[0] && header[1] == mwmagic[1]) {
            return Unknown_File;
        }
    }
    return Unknown_File;
}

char** Link_GetFileSpecTextHandle(CWFileSpec* file, short* error, SInt32 extraBytes) {
    char** buffer;
    short fileRef;
    SInt32 fileSize;
    SInt32 bytesRead;
    *error = FSpOpenDF(file, 1, &fileRef);
    if (*error == 0) {
        *error = GetEOF(fileRef, &fileSize);
        if (*error != 0 || fileSize == 0) {
            FSClose(fileRef);
            return NULL;
        }
        buffer = COS_NewOSHandle(fileSize + extraBytes);
        if (buffer == NULL) {
            buffer = COS_NewHandle(fileSize + extraBytes);
            if (buffer == NULL) {
                FSClose(fileRef);
                *error = memFullErr;
                return NULL;
            }
        }
        COS_LockHandle(buffer);
        bytesRead = fileSize;
        *error = FSRead(fileRef, &bytesRead, *buffer);
        if (*error != 0 || bytesRead != fileSize) {
            COS_FreeHandle(buffer);
            FSClose(fileRef);
            return NULL;
        }
        if (extraBytes != 0) {
            (*buffer)[fileSize] = 0;
        }
        COS_UnlockHandle(buffer);
        FSClose(fileRef);
        return buffer;
    }
    return NULL;
}

ArchiveInfo* Link_GetFileText(CWFileSpec* name, SInt16* err) {
    ArchiveInfo* member;
    unsigned char type;
    ArchiveInfo* head;
    ArchiveInfo* last;
    char** data;
    char* header;
    SInt32 size;
    SInt16 handle;
    SInt32 count;
    SInt32 length;
    char buffer[8];
    SInt32 position;

    type = IsMWLibraryFile(name);
    if (type == Object_File) {
        member = (ArchiveInfo*)galloc(sizeof(ArchiveInfo));
        memset(member, 0, sizeof(ArchiveInfo));
        member->dataHandle = Link_GetFileSpecTextHandle(name, err, 0);
        member->type = type;
        return member;
    }
    if (type == Archive_Member) {
        *err = FSpOpenDF(name, 1, &handle);
        if (*err != 0) {
            return NULL;
        }
        *err = GetEOF(handle, &size);
        if (*err != 0 || size == 0) {
            FSClose(handle);
            return NULL;
        }
        head = last = NULL;
        count = sizeof(buffer);
        *err = FSRead(handle, &count, buffer);
        if (*err != 0 || count != sizeof(buffer)) {
            FSClose(handle);
            return NULL;
        }
        position = sizeof(buffer);
        while (position < size) {
            member = (ArchiveInfo*)galloc(sizeof(ArchiveInfo));
            if (member == NULL) {
                FSClose(handle);
                *err = memFullErr;
                for (member = head; member; member = member->next) {
                    if (member->dataHandle) {
                        COS_FreeHandle(member->dataHandle);
                    }
                }
                return NULL;
            }
            if (head == NULL) {
                head = member;
            }
            memset(member, 0, sizeof(ArchiveInfo));
            member->type = Archive_Member;
            count = 60;
            member->header = galloc(60);
            if (member->header == NULL) {
                FSClose(handle);
                for (member = head; member; member = member->next) {
                    if (member->dataHandle) {
                        COS_FreeHandle(member->dataHandle);
                    }
                }
                return NULL;
            }
            *err = FSRead(handle, &count, member->header);
            if (*err != 0 || count != 60) {
                FSClose(handle);
                for (member = head; member; member = member->next) {
                    if (member->dataHandle) {
                        COS_FreeHandle(member->dataHandle);
                    }
                }
                return NULL;
            }
            header = member->header;
            position += 60;
            sscanf(header + 48, "%ld", &length);
            data = COS_NewOSHandle(length);
            if (data == NULL) {
                data = COS_NewHandle(length);
                if (data == NULL) {
                    FSClose(handle);
                    *err = memFullErr;
                    for (member = head; member; member = member->next) {
                        if (member->dataHandle) {
                            COS_FreeHandle(member->dataHandle);
                        }
                    }
                    return NULL;
                }
            }
            member->dataHandle = data;
            COS_LockHandle(member->dataHandle);
            count = length;
            *err = FSRead(handle, &count, *member->dataHandle);
            if (*err != 0 || count != length) {
                FSClose(handle);
                for (member = head; member; member = member->next) {
                    if (member->dataHandle) {
                        COS_FreeHandle(member->dataHandle);
                    }
                }
                return NULL;
            }
            position += length;
            if ((position & 1) != 0 && position < size) {
                count = 1;
                *err = FSRead(handle, &count, buffer);
                if (*err != 0 || count != 1) {
                    FSClose(handle);
                    for (member = head; member; member = member->next) {
                        if (member->dataHandle) {
                            COS_FreeHandle(member->dataHandle);
                        }
                    }
                    return NULL;
                }
                position++;
            }
            COS_UnlockHandle(member->dataHandle);
            if (last != NULL) {
                last->next = member;
            }
            last = member;
        }
        FSClose(handle);
        return head;
    }
    return NULL;
}

void DeleteArchiveInfo(ArchiveInfo* args) {
    while (args != NULL) {
        if (args->dataHandle != NULL) {
            COS_FreeHandle(args->dataHandle);
        }
        args->dataHandle = NULL;
        args = args->next;
    }
}

unsigned char Inspect_ELF_Header(Elf32_Ehdr* record, char* location, char* name) {
    if (record->e_ident[5] == 1) {
        if (!copts.littleendian) {
            linkwarning(48, location, name);
            return FALSE;
        }
    } else if (record->e_ident[5] == 2) {
        if (copts.littleendian) {
            linkwarning(48, location, name);
            return FALSE;
        }
    } else {
        linkwarning(46, "EI_DATA", location, name);
        return FALSE;
    }

    if (record->e_ident[4] == 1) {
        if (Target64) {
            linkwarning(47, location, name);
            return FALSE;
        }
    } else if (record->e_ident[4] == 2) {
        if (!Target64) {
            linkwarning(47, location, name);
            return FALSE;
        }
    } else {
        linkwarning(46, "EI_CLASS", location, name);
        return FALSE;
    }

    if (record->e_machine != 20 && record->e_machine != 17) {
        linkwarning(
            54,
            *name ? name : location,
            *name ? " of archive '" : "",
            *name ? location : "",
            *name ? "'" : "",
            "PowerPC"
        );
        return FALSE;
    }
    return TRUE;
}

void Import_Object_File(ArchiveInfo* input, char* location, char* name) {
    Elf32_Ehdr* header;
    Elf32_Shdr* section;
    int section_index;
    char* image;
    COS_LockHandleHi(input->dataHandle);
    image = (char*)*input->dataHandle;
    Linker_UserBreakCheck();
    header = (Elf32_Ehdr*)image;
    if (nonNativeByteOrder != 0) {
        Convert_Elf32_Ehdr(header);
    }
    if (Inspect_ELF_Header(header, location, name)) {
        for (section_index = 0; section_index < header->e_shnum; ++section_index) {
            section = (Elf32_Shdr*)(image + header->e_shoff + section_index * sizeof(Elf32_Shdr));
            if (nonNativeByteOrder != 0) {
                Convert_Elf32_Shdr(section);
            }
            if ((section->sh_flags & SHF_EXECINSTR) != 0) {
                Imported_Obj_Codesize += section->sh_size;
            } else if ((section->sh_flags & SHF_ALLOC) != 0) {
                if (section->sh_type == SHT_PROGBITS) {
                    objidatasize += section->sh_size;
                } else if (section->sh_type == SHT_NOBITS) {
                    objudatasize += section->sh_size;
                }
            }
        }
    }
    COS_UnlockHandle(input->dataHandle);
}

char* Get_AR_Name(char** baseOffset, char* text) {
    char* base;
    SInt32 index;
    index = 0;
    base = NULL;

    if (baseOffset != NULL) {
        base = *baseOffset;
    }

    if (base == NULL && text[0] == '/') {
        ++index;
        if (text[index] == '/') {
            ++index;
        }
        if (text[index] == ' ' || text[index] == 0) {
            text[index] = 0;
            return text;
        }
    }

    if (text[0] == '/' && base != NULL && isdigit(text[1])) {
        sscanf(text + 1, "%ld", &index);
        return base + index;
    }

    for (index = 15; index != 0; --index) {
        if (text[index] == '/' || text[index] == 0) {
            text[index] = 0;
            return text;
        }
    }
    text[15] = 0;
    return text;
}

void term_ar_string_table(char** buffer, int length) {
    char* text = *buffer;
    int index;
    for (index = length - 1; index > 1; --index) {
        if (text[index] == '\n' && text[index - 1] == '/') {
            text[index - 1] = 0;
        }
    }
}

void Import_Archive(ArchiveInfo* list, char* location) {
    char** longNames;
    char* name;
    char** data;
    ArchiveInfo* node;
    char* header;

    longNames = NULL;
    for (node = list; node; node = node->next) {
        header = node->header;
        data = node->dataHandle;
        name = Get_AR_Name(longNames, header);

        if (!GetHandleSize(data)) {
            linkwarning(0x7e, location, name);
            continue;
        }
        if (!memcmp(name, "//", 3)) {
            longNames = data;
            COS_LockHandleHi(data);
            term_ar_string_table(data, GetHandleSize(data));
            node->type = Archive_String_Table;
        }
        if (!IsELF(*data)) {
            continue;
        }
        node->type = Archive_Object_Member;
        Import_Object_File(node, location, name);
    }
    if (longNames) {
        COS_UnlockHandle(longNames);
    }
}

short Import_File(ArchiveInfo* record, char* buffer) {
    short result = setjmp(linkerexit);

    if (result == 0) {
        if (record->type == Archive_Member) {
            Import_Archive(record, buffer);
        } else if (record->type == Object_File) {
            Import_Object_File(record, buffer, "");
        } else {
            linkerror(0x39, buffer);
            linkfailed(0xd, 2);
        }
    } else {
        linkalert(result);
    }
    return result;
}
