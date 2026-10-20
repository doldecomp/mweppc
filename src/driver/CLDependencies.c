#include "driver/CLDependencies.h"
#include "compiler/CPrep.h"
#include "driver/CLAccessPaths.h"
#include "driver/CLErrors.h"
#include "driver/CLFiles.h"
#include "driver/CLMain.h"
#include "driver/cmdline.h"
#include "driver/Generic.h"
#include "driver/MacSpecs.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include "driver/pref_structs.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static OSSpec* currentsrcfss;
static Path* specialAccessPath;

static Boolean FindFileInPaths(Paths* paths, const char* filename, Path** thepath, OSSpec* spec);
Path* Deps_GetSpecialAccessPath(void);
static void SetSpecialAccessPathFromIncludeStackTOS(void);

Boolean Incls_Initialize(Incls* incls, Target* targ) {
    specialAccessPath = NULL;
    incls->targ = targ;
    incls->numincls = incls->maxincls = 0;
    incls->buflen = incls->bufpos = 0;
    incls->buffer = NULL;
    incls->files = NULL;
    incls->allPaths = xmalloc(NULL, sizeof(Paths));
    Paths_Initialize(incls->allPaths);
    return TRUE;
}

void Incls_Terminate(Incls* incls) {
    if (incls->buffer) {
        xfree(incls->buffer);
    }
    if (incls->files) {
        xfree(incls->files);
    }
    Paths_Terminate(incls->allPaths);
    xfree(incls->allPaths);
}

static Boolean IsSysIncl(Incls* incls, SInt32 idx) {
    InclFile* i = &incls->files[idx];
    return i->syspath;
}

static void MakeInclFileSpec(Incls* incls, SInt32 idx, OSSpec* spec) {
    InclFile* f = &incls->files[idx];
    OS_MakeSpecWithPath(f->globalpath->spec, &incls->buffer[f->filenameoffs], FALSE, spec);
}

static Boolean QuickFindFileInIncls(
    Incls* incls,
    Boolean fullsearch,
    const char* filename,
    OSSpec* spec,
    SInt32* index,
    InclFile** f
) {
    int idx;
    for (idx = 0; idx < incls->numincls; idx++) {
        *f = &incls->files[idx];
        if (OS_EqualPath(&incls->buffer[(*f)->filenameoffs], filename)) {
            if (fullsearch || (*f)->syspath) {
                MakeInclFileSpec(incls, idx, spec);
                *index = idx;
                return TRUE;
            }
        }
    }
    return FALSE;
}

static Boolean SameIncl(Incls* incls, SInt32 a, SInt32 b) {
    InclFile* ia;
    InclFile* ib;
    if (a == b) {
        return TRUE;
    }
    ia = &incls->files[a];
    ib = &incls->files[b];
    return ia->globalpath == ib->globalpath &&
        OS_EqualPath(&incls->buffer[ia->filenameoffs], &incls->buffer[ib->filenameoffs]);
}

static Path* FindOrAddGlobalInclPath(Paths* paths, OSPathSpec* spec) {
    Path* path = Paths_FindPathSpec(paths, spec);
    if (!path) {
        path = Path_New(spec);
        Paths_AddPath(paths, path);
    }
    return path;
}

static Boolean _FindFileInPath(Path* path, const char* filename, Path** thepath, OSSpec* spec) {
    if (!OS_MakeSpecWithPath(path->spec, filename, FALSE, spec) && OS_IsFile(spec)) {
        *thepath = path;
        return TRUE;
    }

    if (path->recursive && FindFileInPaths(path->recursive, filename, thepath, spec)) {
        return TRUE;
    }

    return FALSE;
}

static Boolean FindFileInPaths(Paths* paths, const char* filename, Path** thepath, OSSpec* spec) {
    UInt16 idx;
    Path* path;

    for (idx = 0; idx < Paths_Count(paths); idx++) {
        path = Paths_GetPath(paths, idx);
        OS_ASSERT(175, path);
        if (_FindFileInPath(path, filename, thepath, spec)) {
            return TRUE;
        }
    }

    return FALSE;
}

static void AddFileToIncls(
    Incls* incls,
    char* infilename,
    Boolean syspath,
    Path* accesspath,
    Path* globalpath,
    Path* specialpath,
    SInt32* index
) {
    InclFile* f;
    int fnlen;
    char* filename;
    OSSpec spec;

    if (!accesspath && !globalpath) {
        filename = OS_GetFileNamePtr(infilename);
    } else {
        filename = infilename;
    }
    fnlen = strlen(filename) + 1;

    if (incls->numincls >= incls->maxincls) {
        incls->maxincls += 16;
        incls->files = xrealloc("include list", incls->files, sizeof(InclFile) * incls->maxincls);
    }

    f = &incls->files[incls->numincls];
    f->filenameoffs = incls->bufpos;
    f->syspath = syspath;
    f->accesspath = accesspath;
    if (globalpath) {
        f->globalpath = globalpath;
    } else if (accesspath) {
        f->globalpath = FindOrAddGlobalInclPath(incls->allPaths, accesspath->spec);
    } else {
        OS_MakeFileSpec(infilename, &spec);
        f->globalpath = FindOrAddGlobalInclPath(incls->allPaths, &spec.path);
    }
    f->specialpath = Deps_GetSpecialAccessPath();

    if (incls->bufpos + fnlen > incls->buflen) {
        incls->buflen += 1024;
        incls->buffer = xrealloc("include list", incls->buffer, incls->buflen);
    }
    strcpy(&incls->buffer[incls->bufpos], filename);
    incls->bufpos += fnlen;
    *index = incls->numincls++;
}

Boolean Incls_FindFileInPaths(
    Incls* incls, char* filename, Boolean fullsearch, OSSpec* spec, SInt32* inclidx
) {
    Boolean found;
    InclFile* incl;
    Boolean userpath;
    Boolean foundglobal;
    Path* globalpath;
    Path* accesspath;
    Path* specialpath;

    found = FALSE;
    if (optsCompiler.includeSearch == IncludeSearch_Include && fullsearch) {
        SetSpecialAccessPathFromIncludeStackTOS();
    }
    *inclidx = -1;

    foundglobal = QuickFindFileInIncls(incls, fullsearch, filename, spec, inclidx, &incl);
    if (foundglobal) {
        if (incl->specialpath == specialAccessPath && incl->syspath && !fullsearch) {
            found = TRUE;
        } else if (!incl->accesspath) {
            foundglobal = FALSE;
        }
    }

    if (!found) {
        globalpath = NULL;
        accesspath = NULL;
        userpath = fullsearch;
        if (OS_IsFullPath(filename)) {
            found = OS_MakeFileSpec(filename, spec) == 0 && OS_Status(spec) == 0;
            accesspath = globalpath = specialpath = NULL;
        } else {
            globalpath = specialpath = Deps_GetSpecialAccessPath();
            if (specialpath) {
                found = _FindFileInPath(globalpath, filename, &globalpath, spec);
                accesspath = NULL;
            }
            if (!found && foundglobal && incl->syspath && !fullsearch) {
                globalpath = incl->globalpath;
                accesspath = incl->accesspath;
                specialpath = NULL;
                MakeInclFileSpec(incls, *inclidx, spec);
                found = TRUE;
            }
            if (!found && fullsearch) {
                userpath = TRUE;
                globalpath = NULL;
                found = FindFileInPaths(&incls->targ->userPaths, filename, &accesspath, spec);
            }
            if (!found) {
                userpath = FALSE;
                globalpath = NULL;
                found = FindFileInPaths(&incls->targ->sysPaths, filename, &accesspath, spec);
            }
        }

        if (found && *inclidx < 0) {
            AddFileToIncls(
                incls, filename, !userpath, accesspath, globalpath, specialpath, inclidx
            );
        }
    }

    return found;
}

Boolean Deps_Initialize(Deps* deps, Incls* incls) {
    deps->numDeps = 0;
    deps->maxDeps = 0;
    deps->list = NULL;
    deps->incls = incls;
    return TRUE;
}

// (no caller: the linker dropped it; Wii's CLDependencies has it)
void Deps_Terminate(Deps* deps) {
    if (deps->list) {
        xfree(deps->list);
    }
}

int Deps_ChangeSpecialAccessPath(OSSpec* srcfss, Boolean initialize) {
    OSPathSpec pathspec;
    OSPathSpec* pathspecptr;

    if (initialize && srcfss) {
        currentsrcfss = srcfss;
    }

    switch (optsCompiler.includeSearch) {
        case IncludeSearch_Explicit:
            specialAccessPath = NULL;
            return TRUE;
        case IncludeSearch_Proj:
            if (!initialize) {
                return TRUE;
            }
            OS_GetCWD(&pathspec);
            pathspecptr = &pathspec;
            break;
        case IncludeSearch_Source:
            if (!initialize) {
                return TRUE;
            }
            // fall through
        case IncludeSearch_Include:
            if (srcfss) {
                pathspecptr = &srcfss->path;
            } else if (currentsrcfss) {
                pathspecptr = &currentsrcfss->path;
            } else {
                OS_GetCWD(&pathspec);
                pathspecptr = &pathspec;
            }
            break;
        default:
            // (the original goes on with pathspecptr unset: CLInternalError returns)
            DO_INTERNAL_ERROR(
                468, "Unhandled include file search type (%d)\n", optsCompiler.includeSearch
            );
    }

    specialAccessPath = FindOrAddGlobalInclPath(gTarg->incls.allPaths, pathspecptr);
    if (optsCmdLine.verbose > 2) {
        CLReport(CLStr105, OS_PathSpecToString(pathspecptr, STSbuf, sizeof(STSbuf)));
    }
    return TRUE;
}

Path* Deps_GetSpecialAccessPath(void) {
    return specialAccessPath;
}

static void SetSpecialAccessPathFromIncludeStackTOS(void) {
    OSSpec spec;
    SInt16 index;
    char* fnameptr;
    VFile* vf;
    struct CPrepFileInfo* file;

    if (optsCompiler.includeSearch == IncludeSearch_Include && CLT_filesp && CLT_filestack) {
        if (*CLT_filesp >= 0) {
            index = *CLT_filesp;
            while (index) {
                if (!(file = CLT_filestack[index])) {
                    break;
                }
                index--;
                OS_FSSpec_To_OSSpec(&file->textfile, &spec);
                fnameptr =
                    OS_GetFileNamePtr(OS_NameSpecToString(&spec.name, STSbuf, sizeof(STSbuf)));
                vf = VFiles_Find(gTarg->virtualFiles, fnameptr);
                if (!vf) {
                    if (!specialAccessPath ||
                        !OS_EqualPathSpec(&spec.path, specialAccessPath->spec)) {
                        Deps_ChangeSpecialAccessPath(&spec, FALSE);
                        return;
                    }
                    return;
                }
            }
        }
        Deps_ChangeSpecialAccessPath(NULL, FALSE);
    }
}

static Boolean FindDepFile(Deps* deps, SInt32 incl) {
    int idx;

    for (idx = 0; idx < deps->numDeps; idx++) {
        if (SameIncl(&gTarg->incls, deps->list[idx], incl)) {
            return TRUE;
        }
    }

    return FALSE;
}

static void AddDepFile(Deps* deps, SInt32 incl, SInt16 dependencyType) {
    if (deps->numDeps >= deps->maxDeps) {
        deps->maxDeps += 16;
        deps->list = xrealloc("dependency list", deps->list, sizeof(SInt32) * deps->maxDeps);
    }
    deps->list[deps->numDeps++] = incl;
}

// `syspath` is unused (the missing-file callback passes `!(fullsearch || noSysPath)`, the other
// FALSE).
void Deps_AddDependency( // NOTE: invented name
    Deps* deps,
    SInt32 incl,
    OSSpec* spec,
    Boolean syspath,
    SInt16 dependencyType,
    Boolean* alreadyincluded
) {
    Boolean included;
    Boolean sys;

    if (incl < 0) {
        AddFileToIncls(
            &gTarg->incls,
            OS_SpecToString(spec, STSbuf, sizeof(STSbuf)),
            FALSE,
            NULL,
            NULL,
            NULL,
            &incl
        );
    }

    included = FindDepFile(deps, incl);
    if (!included) {
        sys = optsCompiler.depsOnlyUserFiles ? IsSysIncl(deps->incls, incl) : FALSE;
        if (!sys) {
            AddDepFile(deps, incl, dependencyType);
        }
    }

    if (alreadyincluded) {
        *alreadyincluded = included;
    }
}

static char* EscapeName(Boolean spaces, char* escbuf, char* path) {
    char* ret;

    if (!spaces) {
        return path;
    }

    ret = escbuf;
    while (*path) {
        if (*path == ' ') {
            *(escbuf++) = '\\';
        }
        *(escbuf++) = *(path++);
    }
    *escbuf = 0;
    return ret;
}

void Deps_ListDependencies(Incls* incls, File* file, OSHandle* h) {
    Boolean spaces;
    int idx;
    char linebuf[520];
    char outfilename[260];
    int totdeps;
    char path[260];
    char escbuf[516];
    OSSpec spec;

// (the original's quote arms are both empty: a name with spaces is escaped instead)
#define LIST_DEP(format, name, ...) \
    do { \
        sprintf( \
            linebuf, \
            format, \
            spaces ? "" : "", \
            EscapeName(spaces, escbuf, (name)), \
            spaces ? "" : "", \
            __VA_ARGS__ \
        ); \
        if (OS_AppendHandle(h, linebuf, strlen(linebuf))) { \
            goto memFail; \
        } \
    } while (0)

    if (!OS_NewHandle(0, h)) {
        totdeps = file->deps.numDeps;
        OS_SpecToStringRelative(&file->outfss, NULL, outfilename, sizeof(outfilename));
        if (outfilename[0]) {
            spaces = strchr(outfilename, ' ') != NULL;
            LIST_DEP("%s%s%s%s ", outfilename, ":");
            spaces = strchr(file->srcfilename, ' ') != NULL;
            LIST_DEP("%s%s%s %s\n", file->srcfilename, totdeps ? "\\" : "");
        } else {
            spaces = strchr(file->srcfilename, ' ') != NULL;
            LIST_DEP("%s%s%s%s %s\n", file->srcfilename, ":", totdeps ? "\\" : "");
        }

        for (idx = 0; idx < file->deps.numDeps; idx++) {
            totdeps--;
            MakeInclFileSpec(incls, file->deps.list[idx], &spec);
            OS_SpecToString(&spec, path, sizeof(path));
            spaces = strchr(path, ' ') != NULL;
            LIST_DEP("\t%s%s%s %s\n", path, totdeps ? "\\" : "");
        }
        return;
    }

memFail:
    fprintf(stderr, "\n*** Out of memory\n");
    exit(-23);
}
