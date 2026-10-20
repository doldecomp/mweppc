#include "driver/CLToolExec.h"
#include "driver/CLErrors.h"
#include "driver/CLFileOps.h"
#include "driver/CLFiles.h"
#include "driver/CLIO.h"
#include "driver/CLMain.h"
#include "driver/CLPlugins.h"
#include "driver/cmdline.h"
#include "driver/CWPlugins.h"
#include "driver/Generic.h"
#include "driver/MacSpecs.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include "driver/pref_structs.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

void AppendArgumentList(int* argc, char*** argv, const char* str) {
    *argv = xrealloc("command-line arguments", *argv, sizeof(char*) * (*argc + 2));
    (*argv)[(*argc)++] = xstrdup(str);
}

static int CopyArgumentList(int argc, char** argv, int* Argc, char*** Argv) {
    int x;
    int y;

    y = Argc ? *Argc : 0;
    *Argv = xrealloc("command-line arguments", *Argv, sizeof(char*) * (y + argc + 1));

    if (argv) {
        for (x = 0; x < argc; x++) {
            (*Argv)[y++] = xstrdup(argv[x]);
        }
    }
    (*Argv)[y] = NULL;
    if (Argc) {
        *Argc = y;
    }

    return TRUE;
}

static int FreeArgumentList(char** argv) {
    int x;
    for (x = 0; argv[x]; x++) {
        xfree(argv[x]);
    }
    xfree(argv);
    return TRUE;
}

static int SetupLinkerCommandLine(SInt32 dropinflags, File* file, CWCommandLineArgs* args) {
    CWCommandLineArgs* baseargs;
    int x;
    SInt32 idx;
    OSSpec spec;

    for (x = 0; x < numPlugins; x++) {
        if (pluginInfo[x].plugintype == CWDROPINLINKERTYPE &&
            ((dropinflags & (isPostLinker | isPreLinker)) ==
             (pluginInfo[x].dropinflags & (isPostLinker | isPreLinker)))) {
            break;
        }
    }

    OS_ASSERT(89, x < numPlugins);

    args->argc = 1;
    args->argv = xmalloc("command-line arguments", 2 * sizeof(char*));
    Main_PassSpecialArgs(&args->argc, &args->argv);

    baseargs = &plugin_args[x];
    CopyArgumentList(baseargs->argc - 1, baseargs->argv + 1, &args->argc, &args->argv);

    x = 0;
    if (baseargs->envp) {
        while (baseargs->envp[x]) {
            x++;
        }
    }
    args->envp = NULL;
    CopyArgumentList(x, baseargs->envp, NULL, &args->envp);

    if (!file && !(dropinflags & isPostLinker)) {
        for (idx = 0; idx < Files_Count(&gTarg->files); idx++) {
            file = Files_GetFile(&gTarg->files, idx);
            if (file->sourceUsage & CmdLineStageMask_Cg) {
                AppendArgumentList(&args->argc, &args->argv, file->srcfilename);
            }
            if (file->objectUsage & CmdLineStageMask_Cg) {
                if (GetOutputFile(file, CmdLineStageMask_Cg) != 0 && StoreObjectFile(file)) {
                    AppendArgumentList(
                        &args->argc,
                        &args->argv,
                        OS_SpecToString(&file->outfss, STSbuf, sizeof(STSbuf))
                    );
                } else {
                    return FALSE;
                }
            }
        }
    } else if (file) {
        args->argv =
            xrealloc("command-line arguments", args->argv, sizeof(char*) * (args->argc + 1));
        if (file->sourceUsage & CmdLineStageMask_Cg) {
            AppendArgumentList(&args->argc, &args->argv, file->srcfilename);
        }
        if (file->objectUsage & CmdLineStageMask_Cg) {
            if (GetOutputFile(file, CmdLineStageMask_Cg) != 0 && StoreObjectFile(file)) {
                AppendArgumentList(
                    &args->argc, &args->argv, OS_SpecToString(&file->outfss, STSbuf, sizeof(STSbuf))
                );
            }
        }
        if ((pCmdLine.toDisk & CmdLineStageMask_Ds) &&
            (file->objectUsage & CmdLineStageMask_Pp) &&
            file->outfilename[0] &&
            GetOutputFile(file, CmdLineStageMask_Ds)) {
            AppendArgumentList(&args->argc, &args->argv, "-o");
            AppendArgumentList(
                &args->argc, &args->argv, OS_SpecToString(&file->outfss, STSbuf, sizeof(STSbuf))
            );
        }
    } else if (dropinflags & isPostLinker) {
        OS_FSSpec_To_OSSpec(&gTarg->targetinfo->outfile, &spec);
        AppendArgumentList(
            &args->argc, &args->argv, OS_SpecToString(&spec, STSbuf, sizeof(STSbuf))
        );
    }

    args->argv[args->argc] = NULL;
    return TRUE;
}

int SetupTemporaries(void) {
    SInt32 idx;
    File* file;

    for (idx = 0; idx < Files_Count(&gTarg->files); idx++) {
        file = Files_GetFile(&gTarg->files, idx);
        if ((file->objectUsage & CmdLineStageMask_Cg) &&
            !(optsCmdLine.toDisk & CmdLineStageMask_Cg) &&
            !(file->writeToDisk & CmdLineStageMask_Cg)) {
            if (!optsCompiler.keepObjects) {
                file->tempOnDisk |= CmdLineStageMask_Cg;
            } else {
                file->writeToDisk |= CmdLineStageMask_Cg;
            }
        }
    }

    return TRUE;
}

int DeleteTemporaries(void) {
    SInt32 idx;
    File* file;

    for (idx = 0; idx < Files_Count(&gTarg->files); idx++) {
        file = Files_GetFile(&gTarg->files, idx);
        if ((file->objectUsage & CmdLineStageMask_Cg) &&
            (file->tempOnDisk & CmdLineStageMask_Cg) &&
            (file->wroteToDisk & CmdLineStageMask_Cg)) {
            GetOutputFile(file, CmdLineStageMask_Cg);
            if (optsCmdLine.verbose > 1) {
                CLReport(CLStr19, OS_SpecToString(&file->outfss, STSbuf, sizeof(STSbuf)));
            }
            OS_Delete(&file->outfss);
            file->tempOnDisk &= ~CmdLineStageMask_Cg;
            file->wroteToDisk &= ~CmdLineStageMask_Cg;
        }
    }

    return TRUE;
}

int ExecuteLinker(
    Plugin* plugin, SInt32 dropinflags, File* file, char* stdoutfile, char* stderrfile
) {
    char cname[256];
    const char* linkername;
    OSSpec linkerspec;
    CWCommandLineArgs args;
    int exitcode;
    int execerr;
    const char* linkertype;
    int ret;
    char* ptr;
    int x;

    OS_ASSERT(269, dropinflags & isExecutableTool);

    args.argc = 0;
    args.argv = args.envp = NULL;

    if (dropinflags & isPreLinker) {
        linkertype = "pre-linker";
    } else if (dropinflags & isPostLinker) {
        linkertype = "post-linker";
    } else {
        linkertype = "linker";
    }

    linkername = Plugin_GetDropInName(plugin);
    if (!(dropinflags & (isPreLinker | isPostLinker)) && optsCompiler.linkerName[0]) {
        linkername = optsCompiler.linkerName;
    }

    if (OS_FindProgram(linkername, &linkerspec)) {
        strcpy(cname, clState.programName);

        for (ptr = cname; *ptr; ptr++) {
            *ptr = tolower(*ptr);
        }

        if ((ptr = strstr(cname, "cc")) ||
            (ptr = strstr(cname, "pas")) ||
            (ptr = strstr(cname, "asm"))) {
            if (ptr[0] != 'c') {
                // (offsets `ptr` as an integer, here and below: Metrowerks' Mac idiom
                // `(Ptr)((long)p + n)`)
                memmove((char*)((long)ptr + 2), (char*)((long)ptr + 3), strlen(ptr) - 3);
                ptr[strlen(ptr) - 1] = '\0';
            }
            ptr[0] = 'l';
            ptr[1] = 'd';
        } else {
            if ((ptr = strstr(cname, "mwc"))) {
                memmove((char*)((long)ptr + 2) + 3, (char*)((long)ptr + 2), strlen(ptr + 2));
                memcpy(ptr + 2, "Link", 4);
            }
        }

        if (!OS_FindProgram(cname, &linkerspec)) {
            ptr = cname;
            if (optsCompiler.linkerName[0]) {
                CLReportWarning(CLStr66, linkertype, optsCompiler.linkerName);
                CLReport(CLStr65, ptr, clState.programName);
            } else if (optsCmdLine.verbose) {
                CLReport(CLStr65, ptr, clState.programName);
            }
        } else {
            CLReportError(CLStr66, linkertype, linkername);
            return FALSE;
        }
    }

    ret = TRUE;
    SetupLinkerCommandLine(dropinflags, file, &args);
    args.argv[0] = xstrdup(OS_SpecToString(&linkerspec, STSbuf, sizeof(STSbuf)));
    args.argv[args.argc] = NULL;

    if (optsCmdLine.verbose || optsCmdLine.dryRun) {
        CLPrint("\nCommand line:\n");
        for (x = 0; x < args.argc && args.argv[x]; x++) {
            if (strchr(args.argv[x], ' ')) {
                CLPrint("\"%s\" ", args.argv[x]);
            } else {
                CLPrint("%s ", args.argv[x]);
            }
        }
        CLPrint("\n\n");
    }

    fflush(stdout);
    fflush(stderr);

    if (optsCmdLine.verbose) {
        CLReport(CLStr67, linkertype, OS_SpecToString(&linkerspec, STSbuf, sizeof(STSbuf)));
    }

    if (!optsCmdLine.dryRun) {
        execerr = OS_Execute(&linkerspec, args.argv, args.envp, stdoutfile, stderrfile, &exitcode);
        if (execerr) {
            CLReportError(CLStr68, linkertype, args.argv[0], OS_GetErrText(execerr));
            ret = FALSE;
        } else if (exitcode) {
            CLReportError(CLStr69, linkertype, args.argv[0], exitcode);
            ret = FALSE;
        }
    }

    FreeArgumentList(args.argv);
    FreeArgumentList(args.envp);

    return ret;
}
