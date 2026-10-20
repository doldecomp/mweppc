#include "compiler/CBrowse.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CMangler.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/objects.h"
#include "compiler/templates.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "compiler/Unmangle.h"
#include "driver/COSToolsCLT.h"
#include "driver/CWPlugins.h"
#include "driver/CWSecretPluginCallbacks.h"
#include <string.h>

Boolean gForceSourceLoc;
Boolean gUseNameTable;
static GList gBrowseData;
static GList gClassData;
static GList gMemberFuncList;
static int gNextMemberFuncID;

enum ELanguage {
    langUnknown,
    langC,
    langCPlus,
    langPascal,
    langObjectPascal,
    langJava,
    langAssembler,
    langFortran,
    langRez
};

enum EBrowserItem {
    browseFunction,
    browseGlobal,
    browseClass,
    browseMacro,
    browseEnum,
    browseTypedef,
    browseConstant,
    browseTemplate,
    browsePackage,
    browseCompSymbolStart = 0x70,
    browseEnd = 0xFF
};

enum {
    kAbstract = 1,
    kStatic = 2,
    kFinal = 4,
    kMember = 8,

    kInterface = 0x80,
    kPublic = 0x100,

    kInline = 0x80,
    kPascal = 0x100,
    kAsm = 0x200,
    kVirtual = 0x400,
    kCtor = 0x800,
    kDtor = 0x1000,
    kNative = 0x2000,
    kSynch = 0x4000,
    kIntrinsic = 0x8000,
    kConst = 0x10000,

    kTransient = 0x80,
    kVolatile = 0x100
};

enum EAccess {
    accessNone = 0,
    accessPrivate = 1,
    accessProtected = 2,
    accessPublic = 4
};

enum EMember {
    memberFunction,
    memberData,
    memberEnd = 0xFF
};

enum ETemplateType {
    templateClass,
    templateFunction
};

static enum EAccess gFromAccessType[] = {accessPublic, accessPrivate, accessProtected, accessNone};

typedef struct BrowseHeader {
    SInt32 browse_header;
    SInt32 browse_version;
    SInt16 browse_language;
    SInt16 uses_name_table;
    SInt32 earliest_compatible_version;
    SInt32 reserved[15];
} BrowseHeader;

static void RecordUndefinedMemberFunctions(void);

void CBrowse_Setup(CompilerLinkerParamBlk* params) {
    BrowseHeader hdr;

    CError_ASSERT(123, params != NULL);

    params->object.browsedata = NULL;

    InitGList(&gBrowseData, 0x10000);
    InitGList(&gMemberFuncList, 1024);

    gNextMemberFuncID = 1;
    gForceSourceLoc = FALSE;
    gUseNameTable = FALSE;

    memclrw(&hdr, sizeof(hdr));
    hdr.browse_header = 0xBEABBAEB;
    hdr.browse_version = 2;
    hdr.earliest_compatible_version = 2;
    hdr.browse_language = copts.cplusplus ? langCPlus : langC;
    hdr.uses_name_table = gUseNameTable;

    AppendGListData(&gBrowseData, &hdr, sizeof(hdr));
}

void CBrowse_Finish(CompilerLinkerParamBlk* params) {
    CWMemHandle hnd;

    CError_ASSERT(151, params != NULL);

    if (gBrowseData.size >= sizeof(BrowseHeader)) {
        RecordUndefinedMemberFunctions();
        AppendGListByte(&gBrowseData, -1);

        COS_ResizeHandle(gBrowseData.data, gBrowseData.size);

        if (CWSecretAttachHandle(params->context, gBrowseData.data, &hnd) == cwNoErr) {
            params->object.browsedata = hnd;
            gBrowseData.data = NULL;
        }
    }
}

void CBrowse_Cleanup(CompilerLinkerParamBlk* params) {
    FreeGList(&gBrowseData);
    FreeGList(&gClassData);
    FreeGList(&gMemberFuncList);
}

static void AppendGList(GList* dst, GList* src) {
    SInt32 offset = dst->size;

    AppendGListNoData(dst, src->size);
    memcpy(*dst->data + offset, *src->data, src->size);
}

static void RecordName(GList* gl, const char* str, SInt32 id) {
    HashNameNode* name;

    CError_ASSERT(190, gl && str && *str);

    if (id < 0 && gUseNameTable) {
        for (name = name_hash_nodes[CHash(str)]; name; name = name->next) {
            if (!strcmp(str, name->name)) {
                id = name->id;
                break;
            }
        }
    }

    if (id >= 0 && gUseNameTable) {
        AppendGListWord(gl, -1);
        AppendGListLong(gl, id);
    } else {
        int len = strlen(str);
        AppendGListWord(gl, len);
        if (len) {
            AppendGListData(gl, str, len + 1);
        }
    }
}

void CBrowse_BeginClass(DeclInfo* di, GList* gl) {
    ClassList* base;
    SInt32 i;
    TypeClass* tclass;
    char* baseName; // NOTE: invented name
    char* buf;

    CError_ASSERT(227, di && di->thetype && gl);

    *gl = gClassData;

    if (!di->file ||
        !di->file->fileID ||
        !di->file->recordbrowseinfo ||
        !di->file2 ||
        !di->file2->fileID ||
        di->sourceoffset <= 0) {
        memclrw(&gClassData, sizeof(gClassData));
        return;
    }

    if (IsTempName(TYPE_CLASS(di->thetype)->classname)) {
        memclrw(&gClassData, sizeof(gClassData));
        return;
    }

    InitGList(&gClassData, 0x4000);
    AppendGListByte(&gClassData, browseClass);
    AppendGListWord(&gClassData, di->file->fileID);
    AppendGListWord(&gClassData, di->file2->fileID);
    AppendGListLong(&gClassData, di->sourceoffset - 1);
    CError_ASSERT(270, gClassData.size == 9);
    AppendGListLong(&gClassData, di->sourceoffset - 1);
    AppendGListLong(&gClassData, 0);
    RecordName(
        &gClassData,
        TYPE_CLASS(di->thetype)->classname->name,
        TYPE_CLASS(di->thetype)->classname->id
    );

    CMangler_MangleType(di->thetype, 0);
    AppendGListByte(&name_mangle_list, 0);

    buf = lalloc(name_mangle_list.size + 1);
    strcpy(buf, *name_mangle_list.data);

    while (*buf && *buf >= '0' && *buf <= '9') {
        buf++;
    }

    if (strcmp(TYPE_CLASS(di->thetype)->classname->name, buf)) {
        RecordName(&gClassData, buf, -1);
    } else {
        AppendGListWord(&gClassData, 0);
    }

    AppendGListLong(&gClassData, 0);

    i = 0;
    for (base = TYPE_CLASS(di->thetype)->bases; base; base = base->next) {
        i++;
    }

    AppendGListByte(&gClassData, i);

    for (base = TYPE_CLASS(di->thetype)->bases; base; base = base->next) {
        AppendGListByte(&gClassData, gFromAccessType[base->access]);
        AppendGListByte(&gClassData, base->is_virtual);

        tclass = base->base;
        if ((tclass->flags & CLASS_IS_TEMPL_INST) && !TEMPL_CLASS_INST(tclass)->is_specialized) {
            tclass = TYPE_CLASS(TEMPL_CLASS_INST(tclass)->templ);
        }

        CMangler_MangleType(TYPE(tclass), 0);
        AppendGListByte(&name_mangle_list, 0);

        baseName = lalloc(name_mangle_list.size + 1);
        strcpy(baseName, *name_mangle_list.data);

        while (*baseName && *baseName >= '0' && *baseName <= '9') {
            baseName++;
        }

        i = base->base->classname->id;
        while (*baseName && *baseName >= '0' && *baseName <= '9') {
            baseName++;
            i = -1;
        }

        RecordName(&gClassData, baseName, i);
    }
}

void CBrowse_AddClassMemberVar(ObjMemberVar* ivar, SInt32 startOffset, SInt32 endOffset) {
    short len;

    CError_ASSERT(360, ivar);

    if (gClassData.data && startOffset > 0 && endOffset >= startOffset) {
        if (tk == ';') {
            endOffset++;
        }

        AppendGListByte(&gClassData, memberData);
        AppendGListByte(&gClassData, gFromAccessType[ivar->access]);
        AppendGListLong(&gClassData, 0);
        AppendGListLong(&gClassData, startOffset - 1);
        AppendGListLong(&gClassData, endOffset - 1);

        len = strlen(ivar->name->name);
        AppendGListWord(&gClassData, len);
        AppendGListData(&gClassData, ivar->name->name, len + 1);
    }
}

void CBrowse_AddClassMemberFunction(Object* object, SInt32 startOffset, SInt32 endOffset) {
    SInt32 flags;
    SInt32 id;
    TypeMemberFunc* tfunc;

    CError_ASSERT(380, object);

    if (IsTempName(object->name)) {
        return;
    }

    if (gClassData.data && startOffset > 0 && endOffset >= startOffset) {
        flags = 0;
        CError_ASSERT(391, object->type && IS_TYPE_FUNC(object->type));
        tfunc = TYPE_METHOD(object->type);

        if (!(tfunc->flags & FUNC_AUTO_GENERATED)) {
            if (object->datatype == DVFUNC) {
                flags |= kVirtual;
            }
            if (tfunc->flags & FUNC_PURE) {
                flags |= kAbstract;
            }
            if (tfunc->is_static) {
                flags |= kStatic;
            }
            if (tfunc->flags & FUNC_IS_CTOR) {
                flags |= kCtor;
            }
            if (tfunc->flags & FUNC_IS_DTOR) {
                flags |= kDtor;
            }

            AppendGListByte(&gClassData, memberFunction);
            AppendGListByte(&gClassData, gFromAccessType[object->access]);
            AppendGListLong(&gClassData, flags);

            id = tfunc->funcid;
            if (id <= 0) {
                // TODO: keeps the Object pointer in a 32-bit long (not 64-bit safe)
                if (!(tfunc->flags & FUNC_DEFINED) || id == -1) {
                    AppendGListLong(&gMemberFuncList, (SInt32)object);
                }
                id = gNextMemberFuncID++;
                tfunc->funcid = id;
            }

            AppendGListLong(&gClassData, id);
            AppendGListLong(&gClassData, startOffset - 1);
            AppendGListLong(&gClassData, endOffset);
        }
    }
}

void CBrowse_AddClassMemberData(Object* object, SInt32 startOffset, SInt32 endOffset) {
    short len;

    CError_ASSERT(435, object);

    if (gClassData.data &&
        startOffset > 0 &&
        endOffset >= startOffset &&
        object->datatype == DDATA) {
        if (tk == ';') {
            endOffset++;
        }

        AppendGListByte(&gClassData, memberData);
        AppendGListByte(&gClassData, gFromAccessType[object->access]);
        AppendGListLong(&gClassData, kStatic);
        AppendGListLong(&gClassData, startOffset - 1);
        AppendGListLong(&gClassData, endOffset - 1);

        len = strlen(object->name->name);
        AppendGListWord(&gClassData, len);
        AppendGListData(&gClassData, object->name->name, len + 1);
    }
}

void CBrowse_EndClass(SInt32 offset, GList* gl) {
    CError_ASSERT(453, gl);

    if (gClassData.data) {
        if (gClassData.size > 0) {
            if (tk == ';') {
                offset++;
            }
            memcpy(*gClassData.data + 9, &offset, 4);
            AppendGList(&gBrowseData, &gClassData);
            AppendGListByte(&gBrowseData, (SInt8)memberEnd);
        }
        FreeGList(&gClassData);
    }

    gClassData = *gl;
}

void CBrowse_BeginStruct(DeclInfo* di, TypeStruct* tstruct, GList* gl) {
    HashNameNode* name;

    CError_ASSERT(480, di && gl);

    *gl = gClassData;

    if (!di->file ||
        !di->file->fileID ||
        !di->file->recordbrowseinfo ||
        !di->file2 ||
        !di->file2->fileID ||
        di->sourceoffset <= 0) {
        memclrw(&gClassData, sizeof(gClassData));
        return;
    }

    name = tstruct->name;
    if (!name || IsTempName(name)) {
        memclrw(&gClassData, sizeof(gClassData));
        return;
    }

    InitGList(&gClassData, 0x4000);
    AppendGListByte(&gClassData, browseClass);
    AppendGListWord(&gClassData, di->file->fileID);
    AppendGListWord(&gClassData, di->file2->fileID);
    AppendGListLong(&gClassData, di->sourceoffset - 1);
    CError_ASSERT(521, gClassData.size == 9);
    AppendGListLong(&gClassData, di->sourceoffset - 1);
    AppendGListLong(&gClassData, 0);
    RecordName(&gClassData, name->name, name->id);
    AppendGListWord(&gClassData, 0);
    AppendGListLong(&gClassData, 0);
    AppendGListByte(&gClassData, 0);
}

void CBrowse_AddStructMember(StructMember* member, SInt32 startOffset, SInt32 endOffset) {
    short len;

    if (tk == ';') {
        endOffset++;
    }

    if (gClassData.data && member && startOffset > 0 && endOffset >= startOffset) {
        AppendGListByte(&gClassData, memberData);
        AppendGListByte(&gClassData, accessPublic);
        AppendGListLong(&gClassData, 0);
        AppendGListLong(&gClassData, startOffset - 1);
        AppendGListLong(&gClassData, endOffset - 1);

        len = strlen(member->name->name);
        AppendGListWord(&gClassData, len);
        AppendGListData(&gClassData, member->name->name, len + 1);
    }
}

void CBrowse_EndStruct(SInt32 offset, GList* gl) {
    CError_ASSERT(558, gl);

    if (gClassData.data) {
        if (offset > 0 && gClassData.size > 0) {
            memcpy(*gClassData.data + 9, &offset, 4);
            AppendGList(&gBrowseData, &gClassData);
            AppendGListByte(&gBrowseData, (SInt8)memberEnd);
        }
        FreeGList(&gClassData);
    }

    gClassData = *gl;
}

static void EmitStandardData(
    enum EBrowserItem item,
    int fileID1,
    int fileID2,
    SInt32 startOffset,
    SInt32 endOffset,
    const char* str,
    SInt32 id,
    const char* str2,
    SInt32 id2
) {
    CError_ASSERT(584, str);

    AppendGListByte(&gBrowseData, item);
    AppendGListWord(&gBrowseData, fileID1);
    AppendGListWord(&gBrowseData, fileID2);
    AppendGListLong(&gBrowseData, startOffset - 1);
    AppendGListLong(&gBrowseData, endOffset - 1);
    AppendGListLong(&gBrowseData, 0);

    RecordName(&gBrowseData, str, id);
    if (str2 && str2 != str) {
        RecordName(&gBrowseData, str2, id2);
    } else {
        AppendGListWord(&gBrowseData, 0);
    }
}

void CBrowse_NewTypedef(
    NameSpace* nspace,
    HashNameNode* name,
    CPrepFileInfo* file1,
    CPrepFileInfo* file2,
    SInt32 startOffset,
    SInt32 endOffset
) {
    CError_ASSERT(618, file1 && file1->recordbrowseinfo);

    if (file2 && file2->fileID && startOffset > 0 && endOffset >= startOffset) {
        EmitStandardData(
            browseTypedef,
            file1->fileID,
            file2->fileID,
            startOffset,
            endOffset,
            name->name,
            name->id,
            CError_GetQualifiedName(nspace, name),
            -1
        );
    }
}

void CBrowse_NewEnum(
    NameSpace* nspace,
    HashNameNode* name,
    CPrepFileInfo* file1,
    CPrepFileInfo* file2,
    SInt32 startOffset,
    SInt32 endOffset
) {
    CError_ASSERT(632, file1 && file1->recordbrowseinfo);

    if (file2 && file2->fileID && startOffset > 0 && endOffset >= startOffset) {
        EmitStandardData(
            browseEnum,
            file1->fileID,
            file2->fileID,
            startOffset,
            endOffset,
            name->name,
            name->id,
            CError_GetQualifiedName(nspace, name),
            -1
        );
    }
}

void CBrowse_NewEnumConstant(
    NameSpace* nspace,
    HashNameNode* name,
    CPrepFileInfo* file1,
    CPrepFileInfo* file2,
    SInt32 startOffset,
    SInt32 endOffset
) {
    CError_ASSERT(646, file1 && file1->recordbrowseinfo);

    if (tk == ',') {
        endOffset++;
    }

    if (file2 && file2->fileID && startOffset > 0 && endOffset >= startOffset) {
        EmitStandardData(
            browseConstant,
            file1->fileID,
            file2->fileID,
            startOffset,
            endOffset,
            name->name,
            name->id,
            CError_GetQualifiedName(nspace, name),
            -1
        );
    }
}

static HashNameNode* CBrowse_GetLinkName(Object* object) {
    return CMangler_GetLinkName(object);
}

// NOTE: invented names: ptr, unmangle, dtorname
static void RecordFunction(
    Object* object, int fileID1, int fileID2, SInt32 startOffset, SInt32 endOffset
) {
    SInt32 flags;
    SInt32 funcid;
    char* namestr;
    char* ptr;
    char* namestr2;
    Boolean unmangle;
    TypeMemberFunc* tfunc;
    SInt32 nameid2;
    char buf[2048];
    char dtorname[256];
    HashNameNode* linkname;
    SInt32 nameid;

    CError_ASSERT(740, object->type && IS_TYPE_FUNC(object->type));

    if (IsTempName(object->name)) {
        return;
    }

    tfunc = TYPE_METHOD(object->type);
    if ((tfunc->flags & (FUNC_AUTO_GENERATED | FUNC_INTRINSIC)) && (!fileID2 || startOffset < 0)) {
        return;
    }

    if (object->name->name[0] != '_' || object->name->name[1] != '_') {
        namestr = object->name->name;
        nameid = object->name->id;
        switch (*namestr) {
            case '.':
                namestr++;
                nameid = -1;
                break;
            case '_':
                switch (namestr[1]) {
                    case '#':
                    case '%':
                    case '@':
                        namestr += 2;
                        nameid = -1;
                        break;
                }
                break;
        }
    } else {
        unmangle = TRUE;
        if (tfunc->flags & (FUNC_IS_CTOR | FUNC_IS_DTOR)) {
            ptr = tfunc->theclass->classname->name;
            while (*ptr >= '0' && *ptr <= '9') {
                ptr++;
            }
            MWUnmangleClassName(ptr, buf, sizeof(buf));

            namestr = buf;
            if ((ptr = strrchr(buf, ':'))) {
                namestr = ptr + 1;
            }

            if (tfunc->flags & FUNC_IS_DTOR) {
                dtorname[0] = '~';
                strncpy(&dtorname[1], namestr, sizeof(dtorname) - 1);
                namestr = dtorname;
            }

            unmangle = FALSE;
        }

        if (unmangle) {
            MWUnmangle(object->name->name, buf, sizeof(buf));
            namestr = buf;
        }

        nameid = -1;
    }

    while (*namestr >= '0' && *namestr <= '9') {
        namestr++;
        nameid = -1;
    }

    namestr2 = NULL;
    nameid2 = -1;

    linkname = CBrowse_GetLinkName(object);
    if (object->name != linkname) {
        namestr2 = linkname->name;
        if (*namestr2 == '.') {
            namestr2++;
        } else {
            nameid2 = linkname->id;
        }
    }

    EmitStandardData(
        browseFunction, fileID1, fileID2, startOffset, endOffset, namestr, nameid, namestr2, nameid2
    );

    flags = 0;
    if (object->qual & Q_INLINE) {
        flags |= kInline;
    }
    if (object->qual & Q_PASCAL) {
        flags |= kPascal;
    }
    if (object->qual & Q_ASM) {
        flags |= kAsm;
    }
    if (object->sclass == TK_STATIC) {
        flags |= kStatic;
    }
    if (tfunc->flags & FUNC_METHOD) {
        flags |= kMember;
    }
    AppendGListLong(&gBrowseData, flags);

    funcid = 0;
    if (tfunc->flags & FUNC_METHOD) {
        funcid = tfunc->funcid;
        if (funcid <= 0) {
            funcid = gNextMemberFuncID++;
            tfunc->funcid = funcid;
        }
    }
    AppendGListLong(&gBrowseData, funcid);
}

void CBrowse_NewFunction(
    Object* object, CPrepFileInfo* file1, CPrepFileInfo* file2, SInt32 startOffset, SInt32 endOffset
) {
    CError_ASSERT(890, file1 && file1->recordbrowseinfo);

    if (file2 && file2->fileID && startOffset > 0 && (endOffset + 1) >= startOffset) {
        RecordFunction(object, file1->fileID, file2->fileID, startOffset, endOffset + 1);
    }
}

void CBrowse_NewData(
    Object* object, CPrepFileInfo* file1, CPrepFileInfo* file2, SInt32 startOffset, SInt32 endOffset
) {
    char* namestr = NULL;
    SInt32 flags = 0;
    Boolean is_const = is_const_object(object);
    SInt32 linkid;
    SInt32 nameid;

    CError_ASSERT(912, file1 && file1->recordbrowseinfo);
    CError_ASSERT(913, object);

    if (tk == ';') {
        endOffset++;
    }

    if (file2 && file2->fileID && startOffset > 0 && endOffset >= startOffset) {
        HashNameNode* name = CBrowse_GetLinkName(object);
        if (object->name != name) {
            namestr = name->name;
        }

        linkid = name->id;
        nameid = object->name->id;
        EmitStandardData(
            is_const ? browseConstant : browseGlobal,
            file1->fileID,
            file2->fileID,
            startOffset,
            endOffset,
            object->name->name,
            nameid,
            namestr,
            linkid
        );

        if (!is_const) {
            if (object->sclass == TK_STATIC) {
                flags |= kStatic;
            }
            AppendGListLong(&gBrowseData, flags);
        }
    }
}

void CBrowse_NewMacro(Macro* macro, CPrepFileInfo* file, SInt32 startOffset, SInt32 endOffset) {
    CError_ASSERT(951, !file || (file->recordbrowseinfo && !macro->is_special));

    if (file && file->fileID && startOffset > 0 && endOffset >= startOffset) {
        EmitStandardData(
            browseMacro,
            file->fileID,
            file->fileID,
            startOffset,
            endOffset,
            macro->name->name,
            macro->name->id,
            NULL,
            -1
        );
    }
}

void CBrowse_NewTemplateClass(
    TemplClass* tmclass, CPrepFileInfo* file, SInt32 startOffset, SInt32 endOffset
) {
    CError_ASSERT(965, !file || file->recordbrowseinfo);

    if (file && file->fileID && startOffset > 0 && endOffset >= startOffset) {
        EmitStandardData(
            browseTemplate,
            file->fileID,
            file->fileID,
            startOffset,
            endOffset,
            tmclass->theclass.classname->name,
            tmclass->theclass.classname->id,
            NULL,
            -1
        );
        AppendGListByte(&gBrowseData, templateClass);
    }
}

void CBrowse_NewTemplateFunc(TemplateFunction* tmfunc) {
    CError_ASSERT(979, !tmfunc->srcfile || tmfunc->srcfile->recordbrowseinfo);

    if (tmfunc->srcfile &&
        tmfunc->srcfile->fileID &&
        tmfunc->startoffset > 0 &&
        tmfunc->endoffset >= tmfunc->startoffset) {
        EmitStandardData(
            browseTemplate,
            tmfunc->srcfile->fileID,
            tmfunc->srcfile->fileID,
            tmfunc->startoffset,
            tmfunc->endoffset,
            tmfunc->name->name,
            tmfunc->name->id,
            NULL,
            -1
        );
        AppendGListByte(&gBrowseData, templateFunction);
    }
}

static void RecordUndefinedMemberFunctions(void) {
    int i;
    int count;
    Object** array;

    COS_LockHandleHi(gMemberFuncList.data);

    count = gMemberFuncList.size / sizeof(Object*);
    array = (Object**)*gMemberFuncList.data;
    for (i = 0; i < count; i++, array++) {
        if (IS_TYPE_FUNC((*array)->type) && !(TYPE_FUNC((*array)->type)->flags & FUNC_DEFINED)) {
            RecordFunction(*array, 0, 0, -1, -1);
        }
    }

    COS_UnlockHandle(gMemberFuncList.data);
}
