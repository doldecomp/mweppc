#include "driver/CCompiler.h"
#include "compiler/IrOptimizer.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "driver/CodeGenOptPPC.h"
#include "driver/CWPlugins.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/CWSecretPluginCallbacks.h"
#include "driver/DropInCompilerLinker.h"
#include "driver/oslib.h"
#include "driver/pref_structs.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

Boolean systemHandles = TRUE;

static char target_name[128];
static PFrontEndC pFrontEnd;
static Boolean using_license_manager;
Boolean crippled;
SInt32 license_cookie;
CompilerLinkerParamBlk cparams;

#if VERSION >= VERSION_GC_2_5
__declspec(dllimport) int __stdcall GetSystemDefaultLangID(void);
#endif

CWPLUGIN_ENTRY(CWPlugin_GetHelpInfo)(const CWHelpInfo** helpInfo) {
    static const CWHelpInfo sHelpInfo = {kCurrentCWHelpInfoVersion, "CCompiler.hlp"};
    *helpInfo = &sHelpInfo;
    return cwNoErr;
}

static void get_extension(ConstStringPtr src, char* dst) {
    int ep;

    dst[0] = 0;
    for (ep = src[0]; src[ep] != '.'; ep--) {}

    if (ep >= 2) {
        int x;
        for (x = 0; (x + ep) <= src[0]; x++) {
            dst[x] = tolower(src[x + ep]);
        }
        dst[x] = 0;
    }
}

static Boolean setup_param_block(CWPluginContext context) {
    CWTargetInfo tinfo;

    memset(&cparams, 0, sizeof(CompilerLinkerParamBlk));
    cparams.context = context;

    if (CWGetPluginRequest(context, &cparams.request) != cwNoErr) {
        return FALSE;
    }
    if (CWGetAPIVersion(context, &cparams.version) != cwNoErr) {
        return FALSE;
    }
    if (CWGetProjectFile(context, &cparams.targetfile) != cwNoErr) {
        return FALSE;
    }
    if (CWGetProjectFileCount(context, &cparams.numfiles) != cwNoErr) {
        return FALSE;
    }
    if (CWGetMainFileNumber(context, &cparams.whichfile) != cwNoErr) {
        return FALSE;
    }
    if (CWGetMainFileSpec(context, &cparams.sourcefile) != cwNoErr) {
        return FALSE;
    }
    if (CWGetMainFileText(context, &cparams.sourcetext, &cparams.sourcetextsize) != cwNoErr) {
        return FALSE;
    }
    if (CWIsPrecompiling(context, &cparams.precompile) != cwNoErr) {
        return FALSE;
    }
    if (CWIsAutoPrecompiling(context, &cparams.autoprecompile) != cwNoErr) {
        return FALSE;
    }
    if (CWIsPreprocessing(context, &cparams.preprocess) != cwNoErr) {
        return FALSE;
    }
    if (CWIsGeneratingDebugInfo(context, &cparams.SYMinfo) != cwNoErr) {
        return FALSE;
    }
    if (CWIsCachingPrecompiledHeaders(context, &cparams.caching_includes) != cwNoErr) {
        return FALSE;
    }
    if (CWGetBrowseOptions(context, &cparams.browseoptions) != cwNoErr) {
        return FALSE;
    }
    cparams.recordbrowseinfo = !cparams.preprocess;
    if (CWGetMainFileID(context, &cparams.browserfileID) != cwNoErr) {
        return FALSE;
    }
    if (CWGetTargetInfo(context, &tinfo) != cwNoErr) {
        return FALSE;
    }

    cparams.targetOS = tinfo.targetOS;
    cparams.targetCPU = tinfo.targetCPU;
    cparams.idetargetname = target_name;
    if (CWGetTargetName(context, cparams.idetargetname, sizeof(target_name)) != cwNoErr) {
        return FALSE;
    }
    return TRUE;
}

static short store_compile_results(void) {
    CWResult result;

    if (cparams.objectdata) {
        CWSecretAttachHandle(cparams.context, cparams.objectdata, &cparams.object.objectdata);
    }
    if (cparams.browsedata) {
        CWSecretAttachHandle(cparams.context, cparams.browsedata, &cparams.object.browsedata);
    }

    if (cparams.preprocess) {
        if (cparams.object.objectdata) {
            CWNewTextDocumentInfo docinfo;
            memset(&docinfo, 0, sizeof(CWNewTextDocumentInfo));
            docinfo.text = cparams.object.objectdata;
            cparams.object.objectdata = NULL;
            result = CWCreateNewTextDocument(cparams.context, &docinfo);
        }
    } else {
        result = CWStoreObjectData(cparams.context, cparams.whichfile, &cparams.object);
    }

    return result;
}

static void initialize_compiler_options(CompilerLinkerParamBlk* params) {
    Handle prefsdata;
    char extension[256];
    PWarningC pWarningC;
    PGlobalOptimizer pGlobalOptimizer;

    memclrw(&copts, sizeof(CompilerLinkerOptions));

    CWSecretGetNamedPreferences(cparams.context, "C/C++ Compiler", &prefsdata);
    pFrontEnd = *((PFrontEndC*)*prefsdata);

    copts.littleendian = FALSE;
    copts.cplusplus = TRUE;
    copts.objective_c = pFrontEnd.objective_c;
    get_extension(params->sourcefile.name, extension);
    if (!strcmp(extension, ".c") || !strcmp(extension, ".h") || !strcmp(extension, ".pch")) {
        copts.cplusplus = pFrontEnd.cplusplus;
    } else if (!strcmp(extension, ".m")) {
        copts.cplusplus = pFrontEnd.cplusplus;
        copts.objective_c = TRUE;
    } else if (!strcmp(extension, ".mm") || !strcmp(extension, ".M")) {
        copts.cplusplus = TRUE;
        copts.objective_c = TRUE;
    }

    copts.checkprotos = pFrontEnd.checkprotos;
    copts.ARMconform = pFrontEnd.arm;
    copts.ARMscoping = pFrontEnd.arm;
    copts.trigraphs = pFrontEnd.trigraphs;
    copts.onlystdkeywords = pFrontEnd.onlystdkeywords;
    copts.enumsalwaysint = pFrontEnd.enumsalwaysint;
    copts.mpwc_relax = pFrontEnd.mpwpointerstyle;
    copts.ANSIstrict = pFrontEnd.ansistrict;
    copts.mpwc_newline = pFrontEnd.mpwcnewline;
    copts.exceptions = pFrontEnd.enableexceptions;
    copts.dont_reuse_strings = pFrontEnd.dontreusestrings;
    copts.poolstrings = pFrontEnd.poolstrings;
    copts.dontinline = pFrontEnd.dontinline;
    copts.RTTI = pFrontEnd.useRTTI;
    copts.oldprefixname = pFrontEnd.oldprefixname;
    copts.multibyteaware = pFrontEnd.multibyteaware;
    copts.unsigned_char = pFrontEnd.unsignedchars;
    copts.auto_inline = pFrontEnd.autoinline;
    copts.direct_to_som = pFrontEnd.direct_to_som;
    copts.som_env_check = pFrontEnd.som_env_check;
    copts.booltruefalse = pFrontEnd.booltruefalse;
    copts.alwaysinline = pFrontEnd.alwaysinline;
    copts.inlinelevel = pFrontEnd.inlinelevel;
    copts.wchar_type = pFrontEnd.wchar_type;
    copts.ecplusplus = pFrontEnd.ecplusplus;
    copts.defer_codegen = pFrontEnd.defer_codegen;
    copts.inlinemaxsize = 512;
    copts.inlinemaxtotalsize = 7000;

    CWSecretGetNamedPreferences(cparams.context, "C/C++ Warnings", &prefsdata);
    pWarningC = *((PWarningC*)*prefsdata);

    copts.warn_illpragma = pWarningC.warn_illpragma;
    copts.warn_emptydecl = pWarningC.warn_emptydecl;
    copts.warn_possunwant = pWarningC.warn_possunwant;
    copts.warn_unusedvar = pWarningC.warn_unusedvar;
    copts.warn_unusedarg = pWarningC.warn_unusedarg;
    copts.warn_extracomma = pWarningC.warn_extracomma;
    copts.pedantic = pWarningC.pedantic;
    copts.warningerrors = pWarningC.warningerrors;
    copts.warn_hidevirtual = pWarningC.warn_hidevirtual;
    copts.warn_implicitconv = pWarningC.warn_implicitconv;
    copts.warn_notinlined = pWarningC.warn_notinlined;
    copts.warn_structclass = pWarningC.warn_structclass;

    CWSecretGetNamedPreferences(cparams.context, "EPPC Global Optimizer", &prefsdata);
    pGlobalOptimizer = *((PGlobalOptimizer*)*prefsdata);

    copts.optimizationlevel = pGlobalOptimizer.optimizationlevel;
    copts.optimizesize = (Boolean)(pGlobalOptimizer.optfor == 1);

    copts.crippled = crippled;
    copts.unrollfactor = 8;
    copts.unrollinstrfactor = 100;
    copts.filesyminfo = params->SYMinfo;
    copts.appltype = 'CWIE';
    copts.headtype = 'MMCH';
    copts.texttype = 'TEXT';
}

static void GetLicense(void) {
    if (!license_cookie) {
        crippled = FALSE;
#if VERSION < VERSION_GC_2_5
        CWCheckoutLicense(
            cparams.context, "Win32_Plugins_PPC_Nintendo", "1", 0, NULL, &license_cookie
        );
#else
        if ((Boolean)(GetSystemDefaultLangID() == 0x411)) {
            CWCheckoutLicense(
                cparams.context, "Win32_Plugins_PPC_Nintendo", "2", 0, NULL, &license_cookie
            );
        } else {
            CWCheckoutLicense(
                cparams.context, "Win32_Plugins_PPC_Nintendo", "1", 0, NULL, &license_cookie
            );
        }
#endif
        using_license_manager = TRUE;
    }
}

static void ReleaseLicense(void) {
    if (license_cookie && using_license_manager) {
        CWCheckinLicense(cparams.context, license_cookie);
    }
    using_license_manager = FALSE;
    license_cookie = 0;
}

CWPLUGIN_ENTRY(MWC_main)(CWPluginContext context) {
    CWResult result;
    SInt32 request;

    result = cwNoErr;
    CWGetPluginRequest(context, &request);

    switch (request) {
        case reqInitialize:
            cparams.context = context;
            license_cookie = 0;
            CodeGen_InitCompiler();
            break;
        case reqTerminate:
            cparams.context = context;
            CodeGen_TermCompiler();
            ReleaseLicense();
            break;
        case reqCompile:
            setup_param_block(context);
            GetLicense();
            if (license_cookie) {
                initialize_compiler_options(&cparams);
                CodeGen_UpdateOptimizerOptions();
                CodeGen_InitBackEndOptions();
                CodeGen_UpdateOptimizerOptions();
                CodeGen_UpdateBackEndOptions();
                result = C_Compiler(&cparams);
                if (result != FALSE) {
                    result = store_compile_results();
                } else {
                    result = cwErrRequestFailed;
                }
            } else {
                result = cwErrSilent;
            }
            break;
    }

    return CWDonePluginRequest(context, result);
}

void PrintProgressFunction(const char* str) {
    char buf[96];
    sprintf(buf, "Compiling function:\t%.64s", str);
    CWShowStatus(cparams.context, buf, "");
}
