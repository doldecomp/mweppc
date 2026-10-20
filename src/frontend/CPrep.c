#include "compiler/CPrep.h"
#include "compiler/IroPointerAnalysis.h"
#include "compiler/IrOptimizer.h"
#include "compiler/CBrowse.h"
#include "compiler/CError.h"
#include "compiler/CException.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrec.h"
#include "compiler/CPreprocess.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/CSOM.h"
#include "compiler/FuncLevelAsmPPC.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/scopes.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "compiler/win32.h"
#include "driver/CCompiler.h"
#include "driver/CLDependencies.h"
#include "driver/COSToolsCLT.h"
#include "driver/CWPlugins.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/DropInCompilerLinker.h"
#include "driver/oslib.h"
#include <ctype.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 2-byte packing for these records and, left in force to the end of the file, for the unit's .bss
// (MWCC lays out a file's statics with the packing in force at its end)
#pragma pack(2)
typedef struct COptsPush {
    struct COptsPush* next;
    CompilerLinkerOptions opts;
} COptsPush;
typedef struct COptPush {
    struct COptPush* next;
    SInt32 id;
    // the entry holds a pushed value (reset frees it for reuse).
    Boolean pushed; // NOTE: invented name
    UInt8 value;
} COptPush;
typedef struct IfStack {
    struct CPrepFileInfo* file;
    SInt32 pos;
    SInt16 state;
} IfStack;
typedef struct PrepValue {
    CInt64 val;
    char is_unsigned;
} PrepValue;
typedef struct PackStack {
    struct HashNameNode* identifier;
    SInt16 align_mode;
} PackStack;
typedef struct CompilerOption {
    char* name;
    short bits;
} CompilerOption;
typedef struct CPrepFileInfoList {
    struct CPrepFileInfoList* next;
    struct CPrepFileInfo* fileinfo;
} CPrepFileInfoList;

// NOTE: invented names: IfState_Active, IfState_Searching, IfState_ElseActive, IfState_Done,
// IfState_ElseDone
// an #if's state.
enum {
    IfState_Active = 0, // reading the branch an #if, #elif or #else took
    IfState_Searching = 1, // skipping: no branch taken yet (an #elif or #else may take one)
    IfState_ElseActive = 2, // reading the #else branch (no #elif or #else may follow)
    IfState_Done = 3, // skipping: a branch was taken, or the enclosing #if is skipping
    IfState_ElseDone = 4 // skipping the #else after a taken branch (no #elif or #else may follow)
};

// The pragmas that set a copts flag, each with its offset in copts.
// clang-format off
CompilerOption compileroptions[] = {
    "little_endian", OPT_FLAG_NOPRAGMA | OPT_OFFSET(littleendian),
    "longlong", OPT_OFFSET(longlong),
    "disable_registers", OPT_OFFSET(disable_registers),
    "fp_contract", OPT_OFFSET(fp_contract),
    "pool_data", OPT_OFFSET(pool_data),
    "incompatible_return_small_structs", OPT_OFFSET(incompatible_return_small_structs),
    "create_file_object", OPT_OFFSET(create_file_object),
    "incompatible_sfpe_double_params", OPT_OFFSET(incompatible_sfpe_double_params),
    "ibm_stackframe", OPT_OFFSET(ibm_stackframe),
    "debug_listing", OPT_OFFSET(debuglisting),
    "profile", OPT_OFFSET(profile),
    "optimizewithasm", OPT_OFFSET(optimizewithasm),
    "use_lmw_stmw", OPT_OFFSET(use_lmw_stmw),
    "no_register_save_helpers", OPT_OFFSET(no_register_save_helpers),
    "cats", OPT_OFFSET(cats),
    "force_cats", OPT_OFFSET(force_cats),
    "gecko_float_typecons", OPT_OFFSET(gecko_float_typecons),
    "disable_vector_frame", OPT_OFFSET(disable_vector_frame),
    "ppc_opt_bclr_bcctr", OPT_OFFSET(opt_bcc_lr_ctr),
    "misaligned_mem_access", OPT_OFFSET(misaligned_mem_access),
    "switch_tables", OPT_OFFSET(switch_tables),
    "prepare_compress", OPT_OFFSET(prepare_compress),
    "unsafe_global_reg_vars", OPT_OFFSET(unsafe_global_reg_vars),
    "ignore_global_reg_vars", OPT_OFFSET(ignore_global_reg_vars),
    "gprfloatcopy", OPT_OFFSET(gprfloatcopy),
#if VERSION >= VERSION_GC_2_5
    "old_style_bitfields", OPT_OFFSET(old_style_bitfields),
#endif
    "asmsemicolcomment", OPT_OFFSET(asmsemicolcomment),
    "asmpoundcomment", OPT_OFFSET(asmpoundcomment),
    "cplusplus", OPT_OFFSET(cplusplus),
    "ecplusplus", OPT_OFFSET(ecplusplus),
    "objective_c", OPT_OFFSET(objective_c),
    "objc_strict", OPT_OFFSET(objc_strict),
    "ARM_conform", OPT_OFFSET(ARMconform),
    "ARM_scoping", OPT_OFFSET(ARMscoping),
    "require_prototypes", OPT_OFFSET(checkprotos),
    "trigraphs", OPT_OFFSET(trigraphs),
    "only_std_keywords", OPT_OFFSET(onlystdkeywords),
    "enumsalwaysint", OPT_OFFSET(enumsalwaysint),
    "ANSI_strict", OPT_OFFSET(ANSIstrict),
    "mpwc_relax", OPT_OFFSET(mpwc_relax),
    "mpwc_newline", OPT_OFFSET(mpwc_newline),
    "ignore_oldstyle", OPT_OFFSET(ignore_oldstyle),
    "cpp_extensions", OPT_OFFSET(cpp_extensions),
    "pointercast_lvalue", OPT_OFFSET(pointercast_lvalue),
    "RTTI", OPT_OFFSET(RTTI),
    "delete_exception", OPT_OFFSET(delete_exception),
    "oldalignment", OPT_OFFSET(oldalignment),
    "multibyteaware", OPT_OFFSET(multibyteaware),
    "unsigned_char", OPT_OFFSET(unsigned_char),
    "auto_inline", OPT_OFFSET(auto_inline),
    "inline_bottom_up", OPT_OFFSET(inline_bottom_up),
    "defer_codegen", OPT_OFFSET(defer_codegen),
    "direct_to_som", OPT_OFFSET(direct_to_som),
    "SOMCheckEnvironment", OPT_OFFSET(som_env_check),
    "SOMCallOptimization", OPT_OFFSET(som_call_optimize),
    "bool", OPT_OFFSET(booltruefalse),
    "old_enum_mangler", OPT_OFFSET(old_enum_mangler),
    "longlong_enums", OPT_OFFSET(longlong_enums),
    "no_tfuncinline", OPT_OFFSET(no_tfuncinline),
    "flat_include", OPT_OFFSET(flat_include),
    "syspath_once", OPT_OFFSET(syspath_once),
    "always_import", OPT_OFFSET(always_import),
    "simple_class_byval", OPT_OFFSET(simple_class_byval),
    "wchar_type", OPT_OFFSET(wchar_type),
    "vbase_ctor_offset", OPT_OFFSET(vbase_ctor_offset),
    "vbase_abi_v2", OPT_OFFSET(vbase_abi_v2),
    "def_inherited", OPT_OFFSET(def_inherited),
    "template_patch", OPT_OFFSET(template_patch),
    "template_friends", OPT_OFFSET(template_friends),
    "faster_pch_gen", OPT_OFFSET(faster_pch_gen),
    "array_new_delete", OPT_OFFSET(array_new_delete),
    "dollar_identifiers", OPT_OFFSET(dollar_identifiers),
    "def_inline_tfuncs", OPT_OFFSET(def_inline_tfuncs),
    "arg_dep_lookup", OPT_OFFSET(arg_dep_lookup),
    "simple_prepdump", OPT_OFFSET(simple_prepdump),
    "line_prepdump", OPT_OFFSET(line_prepdump),
    "fullpath_prepdump", OPT_OFFSET(fullpath_prepdump),
    "old_mtemplparser", OPT_OFFSET(old_mtemplparser),
    "suppress_init_code", OPT_OFFSET(suppress_init_code),
    "reverse_bitfields", OPT_OFFSET(reverse_bitfields),
    "c9x", OPT_OFFSET(c9x),
    "float_constants", OPT_OFFSET(float_constants),
    "no_static_dtors", OPT_OFFSET(no_static_dtors),
    "longlong_prepeval", OPT_OFFSET(longlong_prepeval),
    "const_strings", OPT_OFFSET(const_strings),
    "dumpir", OPT_OFFSET(dumpir),
    "experimental", OPT_OFFSET(experimental),
    "gcc_extensions", OPT_OFFSET(gcc_extensions),
    "stdc_fp_contract", OPT_OFFSET(stdc_fp_contract),
    "stdc_fenv_access", OPT_OFFSET(stdc_fenv_access),
    "stdc_cx_limitedr", OPT_OFFSET(stdc_cx_limitedr),
    "old_argmatch", OPT_OFFSET(old_argmatch),
    "optEH", OPT_OFFSET(optEH),
    "optEH2", OPT_OFFSET(optEH2),
    "new_mangler", OPT_OFFSET(new_mangler),
    "fullpath_file", OPT_OFFSET(fullpath_file),
    "microsoft_exceptions", OPT_OFFSET(microsoft_EH),
    "microsoft_RTTI", OPT_OFFSET(microsoft_EH),
    "stdc_libfuncname_redefined", (short)(OPT_FLAG_RECONFIGURE | OPT_OFFSET(no_ansi_lib_builtin)),
    "no-ansi-lib-builtin", OPT_OFFSET(no_ansi_lib_builtin),
    "warning_errors", OPT_OFFSET(warningerrors),
    "extended_errorcheck", OPT_OFFSET(pedantic),
    "check_header_flags", OPT_OFFSET(checkprecompflags),
    "supress_warnings", OPT_OFFSET(supress_warnings),
    "warn_illpragma", OPT_OFFSET(warn_illpragma),
    "warn_emptydecl", OPT_OFFSET(warn_emptydecl),
    "warn_possunwant", OPT_OFFSET(warn_possunwant),
    "warn_unusedvar", OPT_OFFSET(warn_unusedvar),
    "warn_unusedarg", OPT_OFFSET(warn_unusedarg),
    "warn_extracomma", OPT_OFFSET(warn_extracomma),
    "warn_hidevirtual", OPT_OFFSET(warn_hidevirtual),
    "warn_largeargs", OPT_OFFSET(warn_largeargs),
    "warn_implicitconv", OPT_OFFSET(warn_implicitconv),
    "warn_notinlined", OPT_OFFSET(warn_notinlined),
    "warn_structclass", OPT_OFFSET(warn_structclass),
    "warn_padding", OPT_OFFSET(warn_padding),
    "warn_no_side_effect", OPT_OFFSET(warn_no_side_effect),
    "warn_resultnotused", OPT_OFFSET(warn_resultnotused),
    "warn_ptr_int_conv", OPT_OFFSET(warn_ptr_int_conv),
    "warn_uninitializedvar", OPT_OFFSET(warn_uninitializedvar),
    "align_array_members", OPT_OFFSET(alignarraymembers),
    "dont_reuse_strings", OPT_OFFSET(dont_reuse_strings),
    "pool_strings", OPT_OFFSET(poolstrings),
    "explicit_zero_data", OPT_OFFSET(explicit_zero_data),
    "readonly_strings", OPT_OFFSET(readonly_strings),
    "opt_common_subs", OPT_OFFSET(commonsubs),
    "opt_loop_invariants", OPT_OFFSET(loopinvariants),
    "opt_propagation", OPT_OFFSET(propagation),
    "opt_unroll_loops", OPT_OFFSET(unrolling),
    "opt_lifetimes", OPT_OFFSET(lifetimes),
    "opt_strength_reduction", OPT_OFFSET(strengthreduction),
    "opt_strength_reduction_strict", OPT_OFFSET(strengthreductionstrict),
    "opt_dead_code", OPT_OFFSET(deadcode),
    "opt_dead_assignments", OPT_OFFSET(deadstore),
    "opt_vectorize_loops", OPT_OFFSET(vectorizeloops),
    "opt_pointer_analysis", OPT_OFFSET(opt_pointer_analysis),
    "exceptions", OPT_OFFSET(exceptions),
    "dont_inline", OPT_OFFSET(dontinline),
    "always_inline", OPT_OFFSET(alwaysinline),
    "optimize_for_size", OPT_OFFSET(optimizesize),
    "peephole", OPT_OFFSET(peephole),
    "global_optimizer", OPT_OFFSET(globaloptimizer),
    "side_effects", OPT_OFFSET(sideeffects),
    "internal", OPT_FLAG_LIST | OPT_OFFSET(cfm_internal),
    "import", OPT_FLAG_LIST | OPT_OFFSET(cfm_import),
    "export", OPT_FLAG_LIST | OPT_OFFSET(cfm_export),
    "lib_export", OPT_FLAG_LIST | OPT_OFFSET(cfm_lib_export),
    "nosyminline", OPT_OFFSET(nosyminline),
    "force_active", OPT_OFFSET(force_active),
    "block_assign", OPT_OFFSET(block_assign),
    "has8bytebitfields", OPT_OFFSET(has8bytebitfields),
    "sym", OPT_OFFSET(filesyminfo),
    NULL, 0
};
// clang-format on

SInt16 tk;
CInt64 tkintconst;
Float tkfloatconst;
char* tkstring;
HashNameNode* tkidentifier;
SInt32 tksize;
short ispascalstring;
SInt16 nlflag;
SInt32 lines;
UInt8 cprep_nomem_exit;
UInt8 cprep_nostring;
CompilerLinkerParamBlk* cparamblkptr;
UInt8 cprep_eoltokens;
static IfStack ifstack[64];
static SInt16 iflevel;
TokenStack tokenstack[128];
SInt16 tokenstacklevel;
CPrepFileInfo* cprep_cursymfile;
char* pos;
char* macropos;
char* nextcharpos;
unsigned char CPrep_SkipNewCommentChar;
UInt8 preprocessing_only;
Handle stringmem;
SInt32 maxstringsize;
UInt8 cprep_idarray[256];
UInt8 was_escchar;
UInt8 macrocheck;
UInt8 widestring;
unsigned char at_linestart;
char* prep_file_start;
char* prep_file_end;
char* macrostart;
UInt8 cprep_strconcat;
CPrepFileInfo* prep_file;
short filesp;
SInt32 linenumber;
static CPrepFileInfo* filestack[32];
static CPrepFileInfoList* cprep_files;
// a file the preprocessor keeps open besides the include stack, released with the next pop; nothing
// sets it.
static CPrepFileInfo* cprep_extrafile; // NOTE: invented name
static UInt32 linetick;
static UInt8 waslockedmacro;
static UInt8 include_once;
static SInt32 now_time;
static SInt32 lineoffset;
static UInt8 was_prep_error;
static UInt8 cprep_hasprepline;
static UInt8 cprep_incondexpr;
static PackStack cprep_packstack[128];
static SInt16 cprep_packstackp;
static Macro lineM;
static Macro fileM;
static Macro dateM;
static Macro timeM;
static Macro stdcM;
static Macro stcvM;
static Macro stchM;
static Macro casmM;
static Macro cpplM;
static Macro MWRSM;
static Macro dtsomM;
static Macro ecppM;
static Macro optiM;
static Macro trgtM;
static Macro envM; // NOTE: invented name (after its macro, __env_var)
GList pplist;
static COptsPush* coptpushs;
static COptPush* coptpush;
static CompilerLinkerOptions* coptssave;
static UInt8 dofreeaheap;
static GList mlist;
static Handle ts_buffer;
static TStreamElement* ts_first;
TStreamElement* ts_last;
TStreamElement* ts_current;
static SInt32 ts_elements;
SInt32 ts_preread_elements;
static SInt32 gDirectiveStart;

// the include stack and its depth, for the command-line tool's dependency scan
struct CPrepFileInfo** CLT_filestack;
SInt16* CLT_filesp;

static PrepValue CPrep_ParseCond(void);
static PrepValue CPrep_ParseBinary(PrepValue* rhs, short precLimit);
static char* XpandMacro(Macro* macro);
static void prepmacro(Macro* macro);
static void prepifskip(void);
static void gotonexttoken(void);

static void cannotopenerror(StringPtr filename, Boolean err) {
    static char fname[64];

    short len = filename[0];
    if (len > 63) {
        len = 63;
    }
    memcpy(fname, filename + 1, len);
    fname[len] = 0;

    CError_ResetErrorSkip();

    if (prep_file) {
        was_prep_error = TRUE;
        CError_Error(ERR_FILE_CANNOT_OPENED, fname);
        if (err) {
            longjmp(errorreturn, 1);
        }
    } else {
        CError_CannotOpen();
    }
}

static void insertmacro(Macro* macro) {
    macro->next = macrohashtable[macro->name->hashval];
    macrohashtable[macro->name->hashval] = macro;
    macro->isExpanding = FALSE;
}

void CPrep_InsertSpecialMacro(Macro* macro, const char* name) {
    macro->name = GetHashNameNodeExport(name);
    macro->is_special = TRUE;
    insertmacro(macro);
}

void CPrep_InsertSpecialMacros(void) {
    CPrep_InsertSpecialMacro(&lineM, "__LINE__");
    CPrep_InsertSpecialMacro(&fileM, "__FILE__");
    CPrep_InsertSpecialMacro(&dateM, "__DATE__");
    CPrep_InsertSpecialMacro(&timeM, "__TIME__");
    CPrep_InsertSpecialMacro(&stdcM, "__STDC__");
    CPrep_InsertSpecialMacro(&stcvM, "__STDC_VERSION__");
    CPrep_InsertSpecialMacro(&stchM, "__STDC_HOSTED__");
    CPrep_InsertSpecialMacro(&casmM, "__CASM__");
    CPrep_InsertSpecialMacro(&cpplM, "__cplusplus");
    CPrep_InsertSpecialMacro(&MWRSM, "__MWERKS__");
    CPrep_InsertSpecialMacro(&dtsomM, "__SOM_ENABLED__");
    CPrep_InsertSpecialMacro(&ecppM, "__embedded_cplusplus");
    CPrep_InsertSpecialMacro(&optiM, "__option");
    CPrep_InsertSpecialMacro(&trgtM, "__ide_target");
    CPrep_InsertSpecialMacro(&envM, "__env_var");
    CodeGen_InsertSpecialMacros();
}

void CPrep_RemoveSpecialMacros(void) {
    Macro** scan;
    int x;

    for (x = 0; x < 2048; x++) {
        scan = &macrohashtable[x];
        while (*scan) {
            if ((*scan)->is_special) {
                *scan = (*scan)->next;
            } else {
                scan = &(*scan)->next;
            }
        }
    }
}

static void CPrep_Reconfig(void) {
    cprep_idarray['$'] = copts.dollar_identifiers != 0;
}

int setupprep(void) {
    int x;

    now_time = time(NULL);
    lineoffset = 0;
    include_once = FALSE;
    cprep_eoltokens = FALSE;
    cprep_nostring = FALSE;
    cprep_incondexpr = FALSE;
    filesp = -1;
    lines = linetick = 0;
    macrocheck = dofreeaheap = TRUE;
    maxstringsize = 256;
    tokenstacklevel = iflevel = 0;
    cprep_cursymfile = NULL;
    cprep_files = NULL;

    CLT_filesp = &filesp;
    CLT_filestack = filestack;

    was_prep_error = fatalerrors = anyerrors = FALSE;

    cprep_strconcat = FALSE;

    GListErrorProc = CError_NoMem;
    mlist.data = NULL;
    pplist.data = NULL;
    if (InitGList(&mlist, 10000)) {
        CError_NoMem();
    }

    if (!(stringmem = COS_NewHandle(256))) {
        CError_NoMem();
    }
    if (!(ts_buffer = COS_NewHandle(1024 * sizeof(TStreamElement)))) {
        CError_NoMem();
    }
    COS_LockHandleHi(ts_buffer);
    ts_first = (TStreamElement*)*ts_buffer;
    ts_last = ts_first + 1023;
    ts_current = ts_first;
    ts_elements = 1024;
    ts_preread_elements = 0;

    macrohashtable = galloc(sizeof(Macro*) * 2048);
    memclrw(macrohashtable, sizeof(Macro*) * 2048);
    CPrep_InsertSpecialMacros();

    for (x = 0; x < 256; x++) {
        cprep_idarray[x] = 0;
    }

    for (x = 'a';; x++) {
        cprep_idarray[x] = 1;
        if (x == 'z') {
            break;
        }
    }

    for (x = 'A';; x++) {
        cprep_idarray[x] = 1;
        if (x == 'Z') {
            break;
        }
    }

    for (x = '0';; x++) {
        cprep_idarray[x] = 2;
        if (x == '9') {
            break;
        }
    }

    cprep_idarray['_'] = 1;

    CPrep_Reconfig();
    ELF_Setup();
    return 0;
}

// written, never read
static unsigned int high_mem_mark;

void cleanupprep(void) {
    while (filesp >= 0) {
        popfile();
    }

    high_mem_mark = CTool_TotalHeapSize();
    releaseheaps();
    GListErrorProc = NULL;
    FreeGList(&mlist);
    FreeGList(&pplist);

    if (stringmem) {
        COS_FreeHandle(stringmem);
        stringmem = NULL;
    }

    if (ts_buffer) {
        COS_FreeHandle(ts_buffer);
        ts_buffer = NULL;
    }
    ts_first = ts_last = ts_current = NULL;
}

// NOTE: invented names: filename, fullsearch, isImport
static Boolean setupfile(StringPtr filename, Boolean fullsearch, Boolean isImport) {
    CPrepFileInfo prepinfo;
    OSType file_type;
    SInt32 file_size;
    SInt16 refnum;
    Str255 file_filename;
    SInt16 file_vrefnum;
    SInt32 file_dirid;
    CWFileInfo fileinfo;
    char myfilename[256];
    void* cache;
    CWMemHandle cache_hnd;

    if (filesp >= 31) {
        was_prep_error = TRUE;
        CError_ErrorTerm(ERR_INCLUDE_NESTING_OVERFLOW);
        return FALSE;
    }

    memclrw(&prepinfo, sizeof(prepinfo));
    prepinfo.isSystemInclude = !fullsearch;
    if (filename) {
        memclrw(&fileinfo, sizeof(fileinfo));
        fileinfo.fullsearch = fullsearch;
        fileinfo.dependencyType = cwNormalDependency;
        fileinfo.isdependentoffile = -1;
        memcpy(myfilename, &filename[1], filename[0]);
        myfilename[filename[0]] = 0;

        if (CWFindAndLoadFile(cparamblkptr->context, myfilename, &fileinfo) != cwNoErr) {
            cannotopenerror(filename, FALSE);
            return FALSE;
        }

        if ((isImport || include_once) && fileinfo.alreadyincluded) {
            return TRUE;
        }

        prepinfo.textfile = fileinfo.filespec;
        prepinfo.nameNode = CTool_GetPathName(&fileinfo.filespec, &prepinfo.fileModDate);
        if (fileinfo.filedata) {
            if (fileinfo.filedatatype == cwFileTypeText) {
                prepinfo.textbuffer = (char*)fileinfo.filedata;
                prepinfo.textlength = fileinfo.filedatalength;
                prepinfo.fileID = fileinfo.fileID;
                prepinfo.recordbrowseinfo = fileinfo.recordbrowseinfo;
            } else if (fileinfo.filedatatype == cwFileTypePrecompiledHeader) {
                PrecompilerRead(0, (void*)fileinfo.filedata);
                return TRUE;
            } else {
                cannotopenerror(filename, FALSE);
                return FALSE;
            }
        } else {
            COS_FileGetFSSpecInfo(&prepinfo.textfile, &file_vrefnum, &file_dirid, file_filename);
            if (COS_FileOpen(&prepinfo.textfile, &refnum)) {
                cannotopenerror(filename, FALSE);
                return FALSE;
            }
            if (COS_FileGetType(&prepinfo.textfile, &file_type) ||
                COS_FileGetSize(refnum, &file_size)) {
                COS_FileClose(refnum);
                cannotopenerror(filename, FALSE);
                return FALSE;
            }
            if (file_type == copts.headtype) {
                if (cparamblkptr->caching_includes) {
                    if (CWAllocMemHandle(cparamblkptr->context, file_size, TRUE, &cache_hnd) !=
                        cwNoErr) {
                        if (CWAllocMemHandle(cparamblkptr->context, file_size, FALSE, &cache_hnd) !=
                            cwNoErr) {
                            COS_FileClose(refnum);
                            CError_NoMem();
                        }
                    }

                    CWLockMemHandle(cparamblkptr->context, cache_hnd, FALSE, &cache);
                    if (COS_FileRead(refnum, cache, file_size)) {
                        COS_FileClose(refnum);
                        CWFreeMemHandle(cparamblkptr->context, cache_hnd);
                        cannotopenerror(filename, FALSE);
                        return FALSE;
                    }
                    COS_FileClose(refnum);
                    CWCachePrecompiledHeader(cparamblkptr->context, &prepinfo.textfile, cache_hnd);
                    PrecompilerRead(0, cache);
                    CWUnlockMemHandle(cparamblkptr->context, cache_hnd);
                    return TRUE;
                } else {
                    PrecompilerRead(refnum, NULL);
                    COS_FileClose(refnum);
                    return TRUE;
                }
            } else {
                COS_FileClose(refnum);
                cannotopenerror(filename, FALSE);
                return FALSE;
            }
        }
    } else {
        if (!cparamblkptr->sourcetext) {
            COS_FileGetFSSpecInfo(
                &cparamblkptr->sourcefile, &file_vrefnum, &file_dirid, file_filename
            );
            cannotopenerror(file_filename, TRUE);
            return FALSE;
        }

        prepinfo.textfile = cparamblkptr->sourcefile;
        prepinfo.textbuffer = (char*)cparamblkptr->sourcetext;
        prepinfo.textlength = cparamblkptr->sourcetextsize;
        prepinfo.fileID = cparamblkptr->browserfileID;
        prepinfo.recordbrowseinfo = cparamblkptr->recordbrowseinfo;
    }

    if (filesp >= 0) {
        filestack[filesp]->linenumber = linenumber;
        filestack[filesp]->hasprepline = cprep_hasprepline;
        filestack[filesp]->pos = pos - filestack[filesp]->textbuffer;
    }

    pos = prepinfo.textbuffer;
    linenumber = 1;
    at_linestart = TRUE;
    filestack[++filesp] = galloc(sizeof(CPrepFileInfo));
    *filestack[filesp] = prepinfo;
    prep_file = filestack[filesp];
    prep_file_start = prep_file->textbuffer;
    prep_file_end = prep_file->textbuffer + prep_file->textlength;
    if (preprocessing_only && !copts.simple_prepdump) {
        CPrep_PreprocessDumpFileInfo(TRUE);
    }

    cprep_extrafile = NULL;
    return TRUE;
}

void CPrep_TSBufferGrow(SInt32 amount) {
    int current_offset = ts_current - ts_first;

    COS_UnlockHandle(ts_buffer);
    if (!COS_ResizeHandle(ts_buffer, sizeof(TStreamElement) * (ts_elements + amount))) {
        CError_NoMem();
    }
    COS_LockHandleHi(ts_buffer);
    ts_elements += amount;
    ts_first = (TStreamElement*)*ts_buffer;
    ts_last = ts_first + (ts_elements - 1);
    ts_current = ts_first + current_offset;
}

void CPrep_TokenStreamGetState(SInt32* state) {
    *state = ts_current - ts_first;
}

void CPrep_TokenStreamSetState(SInt32* state) {
    ts_preread_elements += ts_current - ts_first - *state;
    ts_current = ts_first + *state;
}

void CPrep_UnLex(void) {
    ++ts_preread_elements;
    --ts_current;
    CError_ASSERT(909, ts_current >= ts_first);
}

void CPrep_TokenStreamSetCurState(SInt32* state) {
    ts_preread_elements += ts_current - ts_first - (*state - 1);
    ts_current = ts_first + (*state - 1);
    tk = lex();
}

static SInt32 CPrep_StreamSkipToBrace(CPrepStreamFuncPtr func, SInt32 val) {
    TStreamElement ts;
    val++;

    do {
        switch (lex()) {
            case '{':
                return val;
            case 0:
            case ';':
                return 0;
            case TK_IDENTIFIER:
                if (func) {
                    ts = ts_current[-1];
                    func(&ts);
                    ts_current[-1] = ts;
                    tk = ts.tokentype;
                }
                break;
        }
        val++;
    } while (1);
}

static SInt32 CPrep_StreamSkipBlock(CPrepStreamFuncPtr func, SInt32 val) {
    TStreamElement ts;
    SInt32 brace_level;
    char save_asmpoundcomment;
    char save_cplusplus;
    char starttoken;
    char endtoken;
    SInt32 level;
    SInt16 t;

    save_asmpoundcomment = copts.asmpoundcomment;
    save_cplusplus = copts.cplusplus;
    level = 0;
    brace_level = 1;
    val++;

loop:
    val++;
    switch ((t = lex())) {
        case 0:
            return 0;
        case TK_ASM:
            cprep_nostring = TRUE;
            val++;
            t = lex();
            if ((t == TK_VOLATILE) ||
                ((t == TK_IDENTIFIER) && !strcmp(tkidentifier->name, "__volatile__"))) {
                val++;
                t = lex();
            }
            if (!t) {
                return 0;
            }
            if (t == TK_EOL) {
                t = lex();
                val++;
            }
            if (t == '(') {
                starttoken = '(';
                endtoken = ')';
            } else if (t == '{') {
                starttoken = '{';
                endtoken = '}';
                brace_level++;
            }
            level = 1;
            in_assembler = TRUE;
            val++;
            t = lex();
            if (t == '"') {
                copts.cplusplus = FALSE;
                copts.asmpoundcomment = TRUE;
                goto loop;
            }
            if (t) {
                goto loop;
            }
            return 0;

        case '(':
            if (starttoken == t) {
                level++;
            }
            goto loop;
        case ')':
            if (level && endtoken == t && !--level) {
                cprep_nostring = FALSE;
                in_assembler = FALSE;
                copts.cplusplus = save_cplusplus;
                copts.asmpoundcomment = save_asmpoundcomment;
            }
            goto loop;

        case TK_IDENTIFIER:
            if (func) {
                ts = ts_current[-1];
                func(&ts);
                ts_current[-1] = ts;
                tk = ts.tokentype;
            }
            goto loop;

        case '{':
            brace_level++;
            if (starttoken == t) {
                level++;
            }
            goto loop;
        case '}':
            if (level && endtoken == t && !--level) {
                cprep_nostring = FALSE;
                in_assembler = FALSE;
                copts.cplusplus = save_cplusplus;
                copts.asmpoundcomment = save_asmpoundcomment;
            }
            if (--brace_level > 0) {
                goto loop;
            }
            break;
        default:
            goto loop;
    }

    return val;
}

// NOTE: invented names: stream, func, unused
void CPrep_StreamGetBlock(TokenStream* stream, CPrepStreamFuncPtr func, int unused) {
    Boolean save_eoltokens;
    Boolean tryflag;
    SInt32 start_offset;
    SInt32 count;

    start_offset = ts_current - ts_first - 1;
    tryflag = FALSE;
    stream->tokens = 0;
    stream->firsttoken = NULL;
    count = 0;
    save_eoltokens = cprep_eoltokens;
    cprep_eoltokens = TRUE;

    switch (tk) {
        case TK_TRY:
            tryflag = TRUE;
        case ':':
            if (!(count = CPrep_StreamSkipToBrace(func, 0))) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                cprep_eoltokens = save_eoltokens;
                return;
            }
        case '{':
            break;
        default:
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            cprep_eoltokens = save_eoltokens;
            return;
    }

    if (!(count = CPrep_StreamSkipBlock(func, count))) {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        cprep_eoltokens = save_eoltokens;
        return;
    }

    if (tryflag) {
tryloop:
        switch (lex()) {
            case TK_EOL:
                count++;
                goto tryloop;
            case TK_CATCH:
                if ((count = CPrep_StreamSkipToBrace(func, count)) == 0 ||
                    (count = CPrep_StreamSkipBlock(func, count)) == 0) {
                    CError_Error(ERR_CATCH_EXPECTED);
                    cprep_eoltokens = save_eoltokens;
                    return;
                }
                if (lookahead_noeol() == TK_CATCH) {
                    goto tryloop;
                }
                break;
            default:
                CError_Error(ERR_CATCH_EXPECTED);
                cprep_eoltokens = save_eoltokens;
                return;
        }
    }

    cprep_eoltokens = save_eoltokens;
    stream->tokens = count;
    stream->firsttoken = galloc(count * sizeof(TStreamElement));
    memcpy(stream->firsttoken, ts_first + start_offset, count * sizeof(TStreamElement));
}

void CPrep_StreamGetSemicolon(TokenStream* stream, CPrepStreamFuncPtr func) {
    SInt32 count;
    SInt32 start_offset;
    int save_eoltokens;

    start_offset = ts_current - ts_first - 1;
    save_eoltokens = cprep_eoltokens;
    cprep_eoltokens = TRUE;
    stream->tokens = 0;
    stream->firsttoken = NULL;
    count = 1;

    while (tk && tk != ';') {
        tk = lex();
        if ((tk == TK_IDENTIFIER) && func) {
            func(&ts_current[-1]);
            tk = ts_current[-1].tokentype;
        }
        count++;
    }

    stream->tokens = count;
    stream->firsttoken = galloc(count * sizeof(TStreamElement));
    memcpy(stream->firsttoken, ts_first + start_offset, count * sizeof(TStreamElement));
    cprep_eoltokens = save_eoltokens;
}

// dead: nothing calls it (the asserts after it keep the original's line numbers)
void CPrep_StreamGetTemplate(TokenStream* stream, CPrepStreamFuncPtr func) {
    SInt32 count;
    SInt32 start_offset;
    Boolean save_eoltokens;
    SInt16 level;

    start_offset = ts_current - ts_first - 1;
    save_eoltokens = cprep_eoltokens;
    cprep_eoltokens = TRUE;
    stream->tokens = 0;
    stream->firsttoken = NULL;
    count = 0;

loop:
    switch (tk) {
        case 0:
            CError_ErrorTerm(ERR_UNEXPECTED_END_FILE);
        case '}':
            CError_Error(ERR_ILLEGAL_TEMPLATE_DECLARATION);
            return;
        case ';':
            count++;
            break;
        case TK_IDENTIFIER:
            if (func) {
                func(&ts_current[-1]);
                tk = ts_current[-1].tokentype;
            }
        default:
            count++;
            tk = lex();
            goto loop;
        case '{':
            level = 0;
            do {
                count++;
                switch (tk) {
                    case TK_IDENTIFIER:
                        if (func) {
                            func(&ts_current[-1]);
                            tk = ts_current[-1].tokentype;
                        }
                        break;
                    case '{':
                        level++;
                        break;
                    case '}':
                        level--;
                        break;
                }
                if (level <= 0) {
                    break;
                }
                if (!(tk = lex())) {
                    CError_ErrorTerm(ERR_UNEXPECTED_END_FILE);
                }
            } while (1);
            if (lookahead() == ';') {
                tk = lex();
                count++;
            }
    }

    stream->tokens = count;
    stream->firsttoken = galloc(count * sizeof(TStreamElement));
    memcpy(stream->firsttoken, ts_first + start_offset, count * sizeof(TStreamElement));
    cprep_eoltokens = save_eoltokens;
}

void CPrep_StreamInsert(TokenStream* stream, SInt32* state) {
    if (ts_preread_elements + (ts_current - ts_first) + stream->tokens >= ts_elements) {
        CPrep_TSBufferGrow(stream->tokens);
    }
    if (ts_preread_elements) {
        memmove(
            ts_current + stream->tokens, ts_current, sizeof(TStreamElement) * ts_preread_elements
        );
    }
    memcpy(ts_current, stream->firsttoken, sizeof(TStreamElement) * stream->tokens);
    ts_preread_elements += stream->tokens;
    CPrep_TokenStreamGetState(state);
}

void CPrep_StreamRemove(TokenStream* stream, SInt32* state) {
    TStreamElement* end;
    SInt32 amount;

    end = ts_first + *state;
    amount = ts_preread_elements + ((ts_current - ts_first) - *state - stream->tokens);
    if (amount >= 0) {
        if (amount) {
            memmove(end, end + stream->tokens, sizeof(TStreamElement) * amount);
        }
        ts_current = end;
        ts_preread_elements = amount;
    }
}

// dead: nothing calls it
void CPrep_RemoveTokens(SInt32 amount) {
    CError_ASSERT(1305, ts_preread_elements >= amount);

    ts_preread_elements -= amount;
    memmove(ts_current, ts_current + amount, sizeof(TStreamElement) * ts_preread_elements);
}

void CPrep_TokenStreamFlush(void) {
    if (!ts_preread_elements) {
        ts_current = ts_first;
    }
}

static short CPrep_TokenSize(char* str) {
    int len;
    short c;

    if ((str[0] >= 'a' && str[0] <= 'z') || (str[0] >= 'A' && str[0] <= 'Z') || (str[0] == '_')) {
        len = 1;
        str++;
        do {
            c = *(str++);
            if ((c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                (c == '_')) {
                len++;
            } else {
                break;
            }
        } while (1);
        return len;
    }

    switch (str[0]) {
        case '*':
            if (str[1] == '=') {
                return 2;
            }
            break;
        case '/':
            if (str[1] == '=' || str[1] == '*' || str[1] == '/') {
                return 2;
            }
            break;
        case '%':
            if (str[1] == '=') {
                return 2;
            }
            break;
        case '+':
            if (str[1] == '=' || str[1] == '+') {
                return 2;
            }
            break;
        case '-':
            if (str[1] == '>') {
                if (str[2] == '*') {
                    return 3;
                } else {
                    return 2;
                }
            }
            if (str[1] == '=' || str[1] == '-') {
                return 2;
            }
            break;
        case '<':
            if (str[1] == '=') {
                return 2;
            }
            if (str[1] == '<') {
                if (str[2] == '=') {
                    return 3;
                } else {
                    return 2;
                }
            }
            break;
        case '>':
            if (str[1] == '=') {
                return 2;
            }
            if (str[1] == '>') {
                if (str[2] == '=') {
                    return 3;
                } else {
                    return 2;
                }
            }
            break;
        case '&':
            if (str[1] == '=' || str[1] == '&') {
                return 2;
            }
            break;
        case '^':
            if (str[1] == '=') {
                return 2;
            }
            break;
        case '|':
            if (str[1] == '=' || str[1] == '|') {
                return 2;
            }
            break;
        case '=':
            if (str[1] == '=') {
                return 2;
            }
            break;
        case '!':
            if (str[1] == '=') {
                return 2;
            }
            break;
        case '.':
            if (str[1] == '.' && str[2] == '.') {
                return 3;
            }
            if (str[1] == '*') {
                return 2;
            }
            break;
        case ':':
            if (str[1] == ':') {
                return 2;
            }
            break;
    }

    return 1;
}

TStreamElement* CPrep_CurStreamElement(void) {
    static TStreamElement dummyelement;

    if (ts_first < ts_current) {
        return ts_current - 1;
    } else {
        return &dummyelement;
    }
}

void CPrep_GetTokenContext(
    TStreamElement* token,
    CPrepFileInfo** file,
    SInt32* position,
    short* column,
    SInt32* line,
    char* text,
    short* textpos,
    short* textcol,
    char* context,
    short* contextpos
) {
    SInt32 j;
    SInt32 n;
    TStreamElement* tok;
    char *base, *cursor, *start;
    SInt32 offset;
    SInt32 i;
    short c;
    Boolean release;
    short auxshort;
    SInt32 auxlong;
    char* loaded;
    unsigned char name[256];
    CPrepFileInfo* pf;
    char* end;

    if (token && !token->tokenfile) {
        token = NULL;
    }
    tok = token;
    if (!tok && ts_first < ts_current) {
        tok = ts_current - 1;
    }
    if (tok && !tok->tokenfile) {
        CError_FATAL(1463);
    }
    if (was_prep_error || !tok) {
        if (!prep_file) {
            CError_CannotOpen();
        }
        *file = prep_file;
        if (!tokenstacklevel) {
            offset = pos - prep_file_start;
            if (was_prep_error && offset > 0) {
                --offset;
            }
        } else {
            offset = tokenstack[0].pos - prep_file_start;
        }
        *position = offset;
    } else {
        *file = tok->tokenfile;
        *position = offset = tok->tokenoffset;
    }
    // TODO: getfiledata, written in place: the original inlines it, but as a static function of its
    // own it does not compile the same
    if (!(base = (pf = *file)->textbuffer)) {
        if (CWGetFileText(
                cparamblkptr->context, &pf->textfile, (const char**)&loaded, &auxlong, &auxshort
            )) {
            COS_FileGetFSSpecInfo(&pf->textfile, NULL, NULL, name);
            cannotopenerror(name, TRUE);
            base = NULL;
        } else {
            base = loaded;
        }
        release = TRUE;
    } else {
        release = FALSE;
    }
    for (j = 0, n = 1; j < offset; j++) {
        if (base[j] == 13) {
            n++;
        }
    }
    *line = n;
    c = CPrep_TokenSize(end = base + offset);
    *column = c;
    if (!token && tokenstacklevel == 1) {
        start = macrostart;
        cursor = macropos;
    } else {
        start = base;
        cursor = end;
    }
    *textpos = 0;
    for (i = 1; i < 80 && cursor - i >= start && cursor[-i] != 13; i++) {}
    --i;
    while ((c = cursor[-i]) && (c == 32 || c == 9 || c == TK_MACRO_NOEXPAND)) {
        --i;
    }
    j = 0;
    while ((c = cursor[-i]) != 13 && c && j < 126) {
        if (!i) {
            *textpos = j;
        }
        if (c != TK_MACRO_NOEXPAND) {
            text[j] = c == 9 ? 32 : c;
            j++;
        }
        --i;
    }
    if (!i) {
        text[j] = 32;
        *textpos = j;
        text[j + 1] = 0;
        *textcol = 1;
    } else {
        text[j] = 0;
        *textcol = CPrep_TokenSize(text + *textpos);
    }
    if (offset > 16) {
        cursor = end - 16;
        *contextpos = 16;
    } else {
        cursor = base;
        *contextpos = offset;
    }
    for (n = 0; n < 31 && *cursor; n++) {
        context[n] = *cursor++;
    }
    context[n] = 0;
    if (release) {
        CWReleaseFileText(cparamblkptr->context, base);
    }
    was_prep_error = FALSE;
}

void CPrep_Error(short code) {
    Boolean save = in_assembler;
    in_assembler = FALSE;
    if (code == ERR_UNEXPECTED_END_FILE && (tokenstacklevel > 0 || pos < prep_file_end)) {
        code = ERR_ILLEGAL_TOKEN;
    }
    was_prep_error = TRUE;
    CError_Error(code);
    in_assembler = save;
}

// The illegal-token error is reported without the name.
// TODO: the original compares the code as an unsigned short here (CPrep_Error compares it signed)
void CPrep_ErrorName(short code, const char* name) {
    Boolean save = in_assembler;
    in_assembler = FALSE;
    was_prep_error = TRUE;
    if ((unsigned short)code == ERR_UNEXPECTED_END_FILE &&
        (tokenstacklevel > 0 || pos < prep_file_end)) {
        CError_Error(ERR_ILLEGAL_TOKEN);
    } else {
        CError_Error(code, name);
    }
    in_assembler = save;
}

void CPrep_Warning(short code) {
    Boolean save = in_assembler;
    in_assembler = FALSE;
    was_prep_error = TRUE;
    CError_Warning(code);
    in_assembler = save;
}

void CPrep_WarningName(short code, const char* name) {
    Boolean save = in_assembler;
    in_assembler = FALSE;
    was_prep_error = TRUE;
    CError_Warning(code, name);
    in_assembler = save;
}

// NOTE: invented names: errTable, str, isTerminal, isWarning
// dead: nothing calls it
void CPrep_ErrorMessage(int errTable, char* str, Boolean isTerminal, Boolean isWarning) {
    Boolean save = in_assembler;
    in_assembler = FALSE;
    was_prep_error = TRUE;
    CError_ErrorMessage(errTable, str, isTerminal, isWarning);
    in_assembler = save;
}

// NOTE: invented names: code, format, list, isTerminal, isWarning
// dead: nothing calls it
void CPrep_ErrorMessageVA(
    int code, const char* format, va_list list, Boolean isTerminal, Boolean isWarning
) {
    Boolean save = in_assembler;
    in_assembler = FALSE;
    was_prep_error = TRUE;
    CError_ErrorMessageVA(code, format, list, isTerminal, isWarning);
    in_assembler = save;
}

void popfile(void) {
    char* text;
    CPrepFileInfo* file;

    if (filesp < 0) {
        return;
    }

    CWReleaseFileText(cparamblkptr->context, prep_file->textbuffer);
    prep_file->textbuffer = NULL;
    if (cprep_extrafile && (text = cprep_extrafile->textbuffer)) {
        CWReleaseFileText(cparams.context, text);
        cprep_extrafile->textbuffer = NULL;
        cprep_extrafile = NULL;
    }
    if (--filesp >= 0) {
        prep_file_start = (prep_file = file = filestack[filesp])->textbuffer;
        prep_file_end = prep_file->textbuffer + prep_file->textlength;
        pos = prep_file_start + file->pos;
        linenumber = prep_file->linenumber;
        cprep_hasprepline = prep_file->hasprepline;
        at_linestart = TRUE;
    }
    if (preprocessing_only && !copts.simple_prepdump) {
        CPrep_PreprocessDumpFileInfo(TRUE);
    }
}

// dead: nothing calls it
static SInt32 prepoffset(void) {
    if (filesp > 0) {
        return filestack[0]->pos;
    } else if (tokenstacklevel) {
        return tokenstack[0].pos - filestack[filesp]->textbuffer;
    } else {
        return pos - filestack[0]->textbuffer;
    }
}

// dead: nothing calls it
static SInt32 prepoffset2(void) {
    return lineoffset;
}

void CPrep_SetSourceFile(FileOffsetInfo* foi) {
    CPrepFileInfoList* list;

    if (!foi->file) {
        return;
    }

    if (foi->file == filestack[0]) {
        if (!cprep_cursymfile) {
            return;
        }
        if (cparamblkptr->precompile != 1) {
            ObjGen_SrcBreakName(NULL, 0, FALSE);
        }
        cprep_cursymfile = NULL;
        return;
    }

    if (cprep_cursymfile == foi->file) {
        return;
    }

    for (list = cprep_files; list; list = list->next) {
        if (foi->file == list->fileinfo) {
            if (cparamblkptr->precompile != 1) {
                ObjGen_SrcBreakName(list->fileinfo->nameNode, list->fileinfo->fileModDate, FALSE);
            }
            cprep_cursymfile = foi->file;
            return;
        }
    }

    if (foi->file == cprep_extrafile) {
        if (cparamblkptr->precompile != 1) {
            ObjGen_SrcBreakName(cprep_extrafile->nameNode, cprep_extrafile->fileModDate, FALSE);
        }
        cprep_cursymfile = foi->file;
        return;
    }

    list = galloc(sizeof(CPrepFileInfoList));
    list->next = cprep_files;
    cprep_files = list;
    list->fileinfo = foi->file;
    if (cparamblkptr->precompile != 1) {
        ObjGen_SrcBreakName(list->fileinfo->nameNode, list->fileinfo->fileModDate, TRUE);
    }
    cprep_cursymfile = foi->file;
}

HashNameNode* CPrep_GetSourceFilePath(CPrepFileInfo* fileinfo) {
    HashNameNode* node;

    if ((node = fileinfo->nameNode) == NULL) {
        node = CTool_GetPathName(&fileinfo->textfile, NULL);
    }
    return node;
}

void CPrep_NewFileOffsetInfo(FileOffsetInfo* foi, TStreamElement* ts) {
    if (!ts || !ts->tokenfile) {
        if (ts_first < ts_current) {
            foi->file = ts_current[-1].tokenfile;
            foi->tokenline = ts_current[-1].tokenline;
            foi->tokenoffset = ts_current[-1].tokenoffset;
        } else {
            foi->file = filestack[filesp];
            foi->tokenline = linenumber;
            if (tokenstacklevel) {
                foi->tokenoffset = tokenstack[0].pos - filestack[filesp]->textbuffer;
            } else {
                foi->tokenoffset = pos - filestack[filesp]->textbuffer;
            }
        }
    } else {
        foi->file = ts->tokenfile;
        foi->tokenline = ts->tokenline;
        foi->tokenoffset = ts->tokenoffset;
    }
    foi->is_inline = FALSE;
}

SInt32 CPrep_GetFileOffsetInfo(FileOffsetInfo* foi) {
    SInt32 p;
    SInt32 i;
    SInt32 off;

    if (ts_first < ts_current && ts_current[-1].tokenfile == foi->file) {
        p = ts_current[-1].tokenline;
        if (p > foi->tokenline) {
            foi->tokenline = p;
        }
        off = ts_current[-1].tokenoffset;
        if (off > foi->tokenoffset) {
            foi->tokenoffset = off;
        }
    } else if (foi->file == filestack[filesp]) {
        if (linenumber > foi->tokenline) {
            foi->tokenline = linenumber;
        }
        if (tokenstacklevel) {
            p = tokenstack[0].pos - filestack[filesp]->textbuffer;
        } else {
            p = pos - filestack[filesp]->textbuffer;
        }
        if (p > foi->tokenoffset) {
            foi->tokenoffset = p;
        }
    } else {
        for (i = filesp - 1; i >= 0; i--) {
            if (foi->file == filestack[i]) {
                if (filestack[i]->linenumber > foi->tokenline) {
                    foi->tokenline = filestack[i]->linenumber;
                }
                if (filestack[i]->pos > foi->tokenoffset) {
                    foi->tokenoffset = filestack[i]->pos;
                }
                break;
            }
        }
    }

    return foi->tokenline;
}

void CPrep_GetFileOffsetInfo2(FileOffsetInfo* foi, SInt32* pLine, HashNameNode** pName) {
    SInt32 p;
    SInt32 i;
    SInt32 off;

    if (ts_first < ts_current && ts_current[-1].tokenfile == foi->file) {
        p = ts_current[-1].tokenline;
        if (p > foi->tokenline) {
            foi->tokenline = p;
        }
        off = ts_current[-1].tokenoffset;
        if (off > foi->tokenoffset) {
            foi->tokenoffset = off;
        }
    } else if (foi->file == filestack[filesp]) {
        if (linenumber > foi->tokenline) {
            foi->tokenline = linenumber;
        }
        if (tokenstacklevel) {
            p = tokenstack[0].pos - filestack[filesp]->textbuffer;
        } else {
            p = pos - filestack[filesp]->textbuffer;
        }
        if (p > foi->tokenoffset) {
            foi->tokenoffset = p;
        }
    } else {
        for (i = filesp - 1; i >= 0; i--) {
            if (foi->file == filestack[i]) {
                if (filestack[i]->linenumber > foi->tokenline) {
                    foi->tokenline = filestack[i]->linenumber;
                }
                if (filestack[i]->pos > foi->tokenoffset) {
                    foi->tokenoffset = filestack[i]->pos;
                }
                break;
            }
        }
    }

    if (foi->file && foi->file->linedelta) {
        *pLine = linenumber;
    } else {
        *pLine = foi->tokenline;
    }
    CError_ASSERT(2019, *pLine >= 0);
    *pName = foi->file ? foi->file->nameNode : NULL;
}

// Gives a file its own path again after a #line named another file.
void CPrep_ResetFileInfo(CPrepFileInfo* file) {
    HashNameNode* name;
    HashNameNode* mainname;

    if (file && file->hasprepline) {
        name = CTool_GetPathName(&file->textfile, NULL);
        if (file->nameNode != name) {
            mainname = CTool_GetPathName(&cparamblkptr->sourcefile, NULL);
            file->linedelta = 0;
            if (name == mainname) {
                name = NULL;
            }
            file->nameNode = name;
        }
    }
}

void CPrep_GetPrepPos(CPrepFileInfo** file, SInt32* ppos) {
    *file = prep_file;
    if (tokenstacklevel > 0) {
        *ppos = tokenstack->pos - prep_file_start;
    } else {
        *ppos = pos - prep_file_start;
    }
}

UInt8 C_Compiler(CompilerLinkerParamBlk* param) {
    TStreamElement ts;
    UInt8 code;

    CInt64_Init();
    cprep_packstackp = 128;
    widestring = FALSE;
    cprep_nomem_exit = FALSE;
    cparamblkptr = param;
    prep_file = NULL;
    linenumber = lines = 0;
    cprep_hasprepline = FALSE;

    if (CWDisplayLines(param->context, 0) != cwNoErr) {
        return 0;
    }

    linetick = COS_GetTicks() + 5;
    code = 0;
    copts.delete_exception = TRUE;
    copts.som_call_optimize = TRUE;
    copts.template_patch = TRUE;
    copts.template_friends = TRUE;
    copts.simple_class_byval = TRUE;
    copts.array_new_delete = TRUE;
    copts.syspath_once = TRUE;
    copts.arg_dep_lookup = TRUE;
    copts.warn_uninitializedvar = TRUE;
    copts.longlong_prepeval = copts.longlong = TRUE;
    copts.vbase_ctor_offset = TRUE;
    copts.vbase_abi_v2 = TRUE;
    copts.optEH = TRUE;
    copts.optEH2 = FALSE;
    if (copts.cplusplus) {
        copts.const_strings = TRUE;
    }
    copts.inline_bottom_up = TRUE;

    if (initheaps(&CError_NoMem)) {
        releaseheaps();
        cprep_nomem_exit = TRUE;
        code = 255;
    } else {
        if (setjmp(errorreturn) == 0) {
            InitNameHash();
            setupprep();
            CParser_Setup();
            SetupPrecompiler(cparamblkptr->precompile);
            SetupAssembler();
            ObjGen_Setup();
            PointerAnalysis_Setup();
            CBrowse_Setup(param);

            setupfile(NULL, TRUE, FALSE);
            if (copts.oldprefixname[0]) {
                setupfile(copts.oldprefixname, TRUE, FALSE);
            }

            coptssave = galloc(sizeof(CompilerLinkerOptions));
            *coptssave = copts;
            coptpushs = NULL;
            coptpush = NULL;
            preprocessing_only = param->preprocess;
            if (param->preprocess) {
                CPrep_Preprocess();
            } else {
                cparser();
            }

            if (CWDisplayLines(param->context, lines) != cwNoErr) {
                anyerrors = TRUE;
            }

            if (tokenstacklevel) {
                was_prep_error = TRUE;
                CError_ErrorTerm(ERR_UNTERMINATED_IF_MACRO);
            } else if (iflevel) {
                was_prep_error = FALSE;
                ts.tokenfile = ifstack[iflevel - 1].file;
                ts.tokenoffset = ifstack[iflevel - 1].pos;
                if (iflevel) {
                    CError_SetErrorToken(&ts);
                }
                CError_ErrorTerm(ERR_UNTERMINATED_IF_MACRO);
            }

            if (!anyerrors) {
                if (param->precompile == 1) {
                    CBrowse_Finish(param);
                    PrecompilerWrite();
                } else if (!param->preprocess) {
                    ObjGen_Finish();
                    CBrowse_Finish(param);
                }
                code = 1;
            }

            if (param->preprocess) {
                cparamblkptr->objectdata = pplist.data;
                pplist.data = NULL;
                cparamblkptr->browsedata = NULL;
            }
        } else {
            CWDisplayLines(param->context, lines);
        }

        CParser_Cleanup();
        CleanupPrecompiler();
        CleanupAssembler();
        ObjGen_Cleanup();
        ObjGen_CodeCleanup();
        PointerAnalysis_Cleanup();
        cleanupprep();
        CBrowse_Cleanup(param);
    }

    param->object.compiledlines = lines;
    if (cprep_nomem_exit) {
        CompilerGetCString(7, string);
        CWReportMessage(cparamblkptr->context, NULL, string, NULL, messagetypeError, 0);
    }

    return code;
}

static void pushtokenseq(Macro* macro) {
    if (tokenstacklevel >= 128) {
        was_prep_error = TRUE;
        CError_ErrorTerm(ERR_MACROS_TOO_COMPLEX);
    } else {
        tokenstack[tokenstacklevel].pos = pos;
        tokenstack[tokenstacklevel].macrostart = macrostart;
        tokenstack[tokenstacklevel].macro = macro;
        if (macro) {
            macro->isExpanding = TRUE;
        }
        tokenstack[tokenstacklevel].macrocheck = macrocheck;
        tokenstacklevel++;
    }
}

void poptokenseq(void) {
    if (!--tokenstacklevel && dofreeaheap) {
        freeaheap();
    }

    pos = tokenstack[tokenstacklevel].pos;
    macrostart = tokenstack[tokenstacklevel].macrostart;
    if (tokenstack[tokenstacklevel].macro) {
        tokenstack[tokenstacklevel].macro->isExpanding = FALSE;
    }
    macrocheck = tokenstack[tokenstacklevel].macrocheck;
    spaceskip = TRUE;
}

static Boolean is_nextchar(short t) {
    char* p = pos;
    short level = tokenstacklevel;
    short c;

    do {
        switch ((c = *p)) {
            case 0:
                if (level <= 0) {
                    return FALSE;
                }
                p = tokenstack[--level].pos;
                continue;
            case 13:
                p++;
                continue;
        }
        if (c == t) {
            return TRUE;
        }
        if (c != ' ' && (c < 9 || c > 12)) {
            break;
        }
        p++;
    } while (1);

    return FALSE;
}

static Macro* ismacroname(void) {
    Macro* scan;
    int save_macrocheck;
    HashNameNode* name = tkidentifier;

    for (scan = macrohashtable[name->hashval]; scan; scan = scan->next) {
        if (scan->name == name) {
            if (scan->is_special) {
                if (scan == &vecM && !copts.altivec_model) {
                    return NULL;
                }
                if (scan == &stdcM && copts.cplusplus) {
                    return NULL;
                }
                if (scan == &cpplM && !copts.cplusplus) {
                    return NULL;
                }
                if (scan == &ecppM && (!copts.cplusplus || !copts.ecplusplus)) {
                    return NULL;
                }
            }

            if (scan->isExpanding) {
                waslockedmacro = TRUE;
                return NULL;
            }

            if (scan->nargs) {
                save_macrocheck = macrocheck;
                macrocheck = FALSE;
                gotonexttoken();
                macrocheck = save_macrocheck;
                if (*pos != '(') {
                    return NULL;
                }
            }

            return scan;
        }
    }

    return NULL;
}

static Macro* ismacroname2(char* str) {
    Macro* scan;
    HashNameNode* name = tkidentifier;

    for (scan = macrohashtable[name->hashval]; scan; scan = scan->next) {
        if (scan->name == name) {
            if (scan->isExpanding) {
                waslockedmacro = TRUE;
                return NULL;
            }

            if (scan->nargs) {
                while (*str == ' ' || (*str >= 9 && *str <= 12)) {
                    str++;
                }
                if (*str != '(' && !is_nextchar('(')) {
                    return NULL;
                }
            }

            return scan;
        }
    }

    return NULL;
}

static Macro* ismacroname5(char* str) {
    Macro* scan;
    HashNameNode* name = tkidentifier;

    for (scan = macrohashtable[name->hashval]; scan; scan = scan->next) {
        if (scan->name == name) {
            if (scan->isExpanding) {
                waslockedmacro = TRUE;
                return NULL;
            }

            if (scan->nargs) {
                while (*str == ' ' || (*str >= 9 && *str <= 12)) {
                    str++;
                }
                if (*str != '(') {
                    return NULL;
                }
            }

            return scan;
        }
    }

    return NULL;
}

static Macro* ismacroname3(void) {
    Macro* scan;
    HashNameNode* name = tkidentifier;

    for (scan = macrohashtable[name->hashval]; scan; scan = scan->next) {
        if (scan->name == name) {
            if (scan->is_special) {
                if (scan == &vecM && !copts.altivec_model) {
                    return NULL;
                }
                if (scan == &stdcM && copts.cplusplus) {
                    return NULL;
                }
                if (scan == &cpplM && !copts.cplusplus) {
                    return NULL;
                }
            }
            break;
        }
    }

    return scan;
}

static Macro* ismacroname4(HashNameNode* name) {
    Macro* scan;

    for (scan = macrohashtable[name->hashval]; scan; scan = scan->next) {
        if (scan->name == name) {
            if (scan->is_special) {
                if (scan == &vecM && !copts.altivec_model) {
                    return NULL;
                }
                if (scan == &stdcM && copts.cplusplus) {
                    return NULL;
                }
                if (scan == &cpplM && !copts.cplusplus) {
                    return NULL;
                }
            }
            break;
        }
    }

    return scan;
}

void foundnl(void) {
    if (!tokenstacklevel) {
        if (preprocessing_only) {
            CPrep_PreprocessDumpNewLine();
        }
        linenumber++;
        lines++;
        if (filesp <= 0) {
            lineoffset = pos - filestack[0]->textbuffer;
        }
    }
}

void newline(void) {
    foundnl();
    nlflag = TRUE;
    at_linestart = TRUE;
    spaceskip = TRUE;
    if ((UInt32)COS_GetTicks() > linetick) {
        if (CWDisplayLines(cparamblkptr->context, lines) != cwNoErr) {
            CError_UserBreak();
        }
        linetick = COS_GetTicks() + 5;
    }
}

static void gotonexttoken(void) {
    char* save_pos;
    Macro* macro;
    char c;

loop:
    switch ((c = prepskipnextchar())) {
        case 0:
            if (tokenstacklevel) {
                poptokenseq();
                goto loop;
            }
            if (tokenstacklevel > 0 || pos >= prep_file_end) {
                if (filesp > 0) {
                    popfile();
                    goto loop;
                }
                return;
            }
            pos = nextcharpos;
            goto loop;
        case 13:
            newline();
            pos = nextcharpos;
            goto loop;
    }

    if (macrocheck && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_'))) {
        pos = ReadIdentifier(save_pos = pos);
        if ((macro = ismacroname())) {
            prepmacro(macro);
            goto loop;
        } else {
            pos = save_pos;
        }
    }
}

short notendofline(void) {
    char* save_pos;
    Macro* macro;
    char c;

loop:
    switch ((c = prepskipnextchar())) {
        case 0:
            if (tokenstacklevel) {
                poptokenseq();
                goto loop;
            }
        case 13:
            return FALSE;
    }

    if (macrocheck && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_'))) {
        pos = ReadIdentifier(save_pos = pos);
        if ((macro = ismacroname())) {
            prepmacro(macro);
            goto loop;
        } else {
            pos = save_pos;
        }
    }

    return TRUE;
}

// NOTE: invented names: name, reported
static void CPrep_MacroRedefError(char* name, Boolean* reported) {
    if (!*reported) {
        if (copts.gcc_extensions) {
            CPrep_WarningName(ERR_MACRO_REDEFINED, name);
        } else if (copts.cpp_extensions) {
            if (copts.pedantic) {
                CPrep_WarningName(ERR_MACRO_REDEFINED, name);
            }
        } else {
            CPrep_ErrorName(ERR_MACRO_REDEFINED, name);
        }
        *reported = TRUE;
    }
}

static void goendofline(void) {
    if (notendofline()) {
        CPrep_Error(ERR_END_LINE_EXPECTED);
        skipendofline();
    }
}

static void CPrep_Define(void) {
    Macro* existing_macro;
    Macro* macro;
    HashNameNode* name;
    HashNameNode** names;
    short t;
    short i;
    short argCount;
    char* token_start;
    char* save_pos;
    Boolean hasVarArgs;
    Boolean isPrecedingTokenAnArg;
    Boolean tokenPasteFlag;
    Boolean error_flag;
    HashNameNode* myNameList[128];

    nlflag = macrocheck = FALSE;
    t = lexidentifier();
    if (nlflag) {
        CPrep_Error(ERR_UNEXPECTED_END_LINE);
        skipendofline();
    }
    if (t != TK_IDENTIFIER) {
        CPrep_Error(ERR_IDENTIFIER_EXPECTED);
        skipendofline();
        return;
    }

    existing_macro = ismacroname3();
    name = tkidentifier;
    error_flag = FALSE;
    hasVarArgs = FALSE;
    if (prepcurchar() == '(') {
        argCount = 1;
        pos = nextcharpos;
        if (existing_macro) {
            names = existing_macro->names;
        } else {
            names = NULL;
        }

        do {
            t = lexidentifier();
            if (t != TK_IDENTIFIER) {
                break;
            }

            if (nlflag) {
                CPrep_Error(ERR_UNEXPECTED_END_LINE);
                return;
            }

            if (!strcmp(tkidentifier->name, "__VA_ARGS__")) {
                CPrep_ErrorName(ERR_ILLEGAL_MACRO_ARGUMENT_NAME, tkidentifier->name);
                return;
            }

            for (i = 1; i < argCount; i++) {
                if (!strcmp(tkidentifier->name, myNameList[i - 1]->name)) {
                    CPrep_ErrorName(ERR_ILLEGAL_MACRO_ARGUMENT_NAME, tkidentifier->name);
                    return;
                }
            }

            myNameList[argCount - 1] = tkidentifier;

            if (existing_macro) {
                if ((existing_macro->nargs & 0x7FFF) < argCount) {
                    CPrep_MacroRedefError(name->name, &error_flag);
                }
                if (names[argCount - 1] != tkidentifier && !copts.cpp_extensions) {
                    CPrep_MacroRedefError(name->name, &error_flag);
                }
            }

            if (argCount >= 128) {
                CError_Error(ERR_TOO_MANY_MACRO_ARGUMENTS);
            } else {
                argCount++;
            }

            t = plex();
            if (nlflag) {
                CPrep_Error(ERR_UNEXPECTED_END_LINE);
                return;
            }
        } while (t == ',');

        if (nlflag) {
            CPrep_Error(ERR_UNEXPECTED_END_LINE);
            return;
        }

        if (t == TK_ELLIPSIS) {
            hasVarArgs = TRUE;
            myNameList[argCount - 1] = GetHashNameNode("__VA_ARGS__");
            if (argCount >= 128) {
                CError_Error(ERR_TOO_MANY_MACRO_ARGUMENTS);
            } else {
                argCount++;
            }

            t = plex();
            if (nlflag) {
                CPrep_Error(ERR_UNEXPECTED_END_LINE);
                return;
            }
        }

        if (t != ')') {
            CPrep_Error(ERR_ILLEGAL_ARGUMENT_LIST);
            skipendofline();
            return;
        }

        macro = galloc(sizeof(Macro) + sizeof(HashNameNode*) * (argCount - 2));
    } else {
        macro = galloc(sizeof(Macro) - sizeof(HashNameNode*));
        argCount = 0;
    }

    if (existing_macro && (existing_macro->nargs & 0x7FFF) != argCount) {
        CPrep_MacroRedefError(name->name, &error_flag);
    }

    macro->name = name;
    macro->nargs = hasVarArgs ? (argCount | 0x8000) : argCount;
    macro->is_special = FALSE;
    macro->isExpanding = FALSE;

    for (i = 1; i < argCount; i++) {
        macro->names[i - 1] = myNameList[i - 1];
    }

    mlist.size = 0;
    if (notendofline()) {
        while (1) {
            tokenPasteFlag = FALSE;
innerLoop:
            isPrecedingTokenAnArg = FALSE;
            token_start = pos;
            spaceskip = FALSE;
            t = prepnextchar();

            switch (t) {
                case 0:
                    CPrep_Error(ERR_UNEXPECTED_END_FILE);
                    break;

                case '"':
                case '\'':
                    CPrep_MatchChar(t, TRUE);
                    AppendGListData(&mlist, token_start, pos - token_start);
                    break;

                case '#':
                    save_pos = pos;
                    if (prepnextchar() == '#') {
                        CPrep_Error(ERR_PREPROCESSOR_SYNTAX_ERROR);
                    }
                    pos = save_pos;

                    if (notendofline()) {
                        t = prepnextchar();
                        if ((t >= 'a' && t <= 'z') || (t >= 'A' && t <= 'Z') || t == '_') {
                            pos = ReadIdentifier(pos - 1);
                            for (i = 1; i < argCount; i++) {
                                if (tkidentifier == macro->names[i - 1]) {
                                    break;
                                }
                            }

                            if (i < argCount) {
                                AppendGListByte(&mlist, TK_MACRO_ARG_HASH);
                                AppendGListByte(&mlist, i);
                                break;
                            }
                        }
                    }

                    if (copts.ANSIstrict) {
                        CPrep_Error(ERR_PREPROCESSOR_SYNTAX_ERROR);
                    }
                    AppendGListByte(&mlist, '#');
                    pos = save_pos;
                    break;

                default:
                    if ((t >= 'a' && t <= 'z') || (t >= 'A' && t <= 'Z') || t == '_') {
                        pos = ReadIdentifier(pos - 1);
                        for (i = 1; i < argCount; i++) {
                            if (tkidentifier == macro->names[i - 1]) {
                                break;
                            }
                        }

                        if (i < argCount) {
                            AppendGListByte(
                                &mlist, tokenPasteFlag ? TK_MACRO_ARG_PASTED : TK_MACRO_ARG_EXPANDED
                            );
                            AppendGListByte(&mlist, i);
                            isPrecedingTokenAnArg = TRUE;
                        } else {
                            AppendGListName(&mlist, tkidentifier->name);
                        }
                    } else {
                        AppendGListByte(&mlist, t);
                    }
            }

            if (!notendofline()) {
                break;
            }

            save_pos = pos;
            if (prepnextchar() == '#' && prepnextchar() == '#') {
                if (!notendofline()) {
                    CPrep_Error(ERR_PREPROCESSOR_SYNTAX_ERROR);
                }
                if (isPrecedingTokenAnArg) {
                    (*mlist.data)[mlist.size - 2] = TK_MACRO_ARG_PASTED;
                }
                tokenPasteFlag = TRUE;
                goto innerLoop;
            }
            pos = save_pos;

            if (spaceskip) {
                AppendGListByte(&mlist, ' ');
            }
        }
    }

    macrocheck = TRUE;

    if (mlist.size > 0) {
        SInt32 length;

        AppendGListByte(&mlist, 0);
        length = mlist.size;
        if (length > 0x20000) {
            CPrep_Error(ERR_MACROS_TOO_COMPLEX);
            return;
        }

        if (existing_macro &&
            (!existing_macro->text || memcmp(*mlist.data, existing_macro->text, length))) {
            CPrep_MacroRedefError(name->name, &error_flag);
        }

        macro->text = galloc(mlist.size);
        memcpy(macro->text, *mlist.data, mlist.size);
    } else {
        macro->text = NULL;
        if (existing_macro && existing_macro->text) {
            CPrep_MacroRedefError(name->name, &error_flag);
        }
    }

    if (!existing_macro || error_flag) {
        macro->next = macrohashtable[macro->name->hashval];
        macrohashtable[macro->name->hashval] = macro;

        if (cparamblkptr->browseoptions.recordMacros && prep_file->recordbrowseinfo) {
            CBrowse_NewMacro(macro, prep_file, gDirectiveStart, pos - prep_file_start + 1);
        }

        if (error_flag) {
            macro = macrohashtable[macro->name->hashval];
            while (1) {
                if (macro->next == existing_macro) {
                    macro->next = macro->next->next;
                    break;
                }
                CError_ASSERT(2858, macro = macro->next);
            }
        }
    }
}

static void prepundefine(void) {
    short t;
    Macro* macro;
    Macro** scan;
    HashNameNode* name;

    nlflag = FALSE;
    t = lexidentifier();
    if (nlflag) {
        CPrep_Error(ERR_UNEXPECTED_END_LINE);
        skipendofline();
        return;
    }
    if (t != TK_IDENTIFIER) {
        CPrep_Error(ERR_IDENTIFIER_EXPECTED);
        skipendofline();
        return;
    }

    name = tkidentifier;
    for (scan = &macrohashtable[name->hashval], macro = *scan; macro;
         scan = &macro->next, macro = *scan) {
        if (macro->name == name) {
            *scan = macro->next;
            macrocheck = TRUE;
            return;
        }
    }
    goendofline();
}

static Boolean CPrep_CheckTarget(void) {
    Boolean result;

    if (!notendofline()) {
        CError_Error(ERR_UNEXPECTED_END_LINE);
        return FALSE;
    }

    if (plex() != '(') {
        CError_Error(ERR_LPAREN_EXPECTED);
        return FALSE;
    }

    if (!notendofline()) {
        CError_Error(ERR_UNEXPECTED_END_LINE);
        return FALSE;
    }

    if (plex() != TK_STRING) {
        CError_Error(ERR_UNEXPECTED_TOKEN);
        return FALSE;
    }

    result = strcmp(cparams.idetargetname, tkstring) == 0;

    if (!notendofline()) {
        CError_Error(ERR_UNEXPECTED_END_LINE);
        return FALSE;
    }

    if (plex() != ')') {
        CError_Error(ERR_RPAREN_EXPECTED);
        return FALSE;
    }

    return result;
}

// `__env_var(name)` gives the environment variable's value, quoted.
static char* CPrep_GetQuotedEnvVar(char* buffer) { // NOTE: invented name
    int len = 0;
    char* value;
    char c;

    if (!notendofline()) {
        CError_Error(ERR_UNEXPECTED_END_LINE);
        return NULL;
    }
    if (plex() != '(') {
        CError_Error(ERR_LPAREN_EXPECTED);
        return NULL;
    }
    if (!notendofline()) {
        CError_Error(ERR_UNEXPECTED_END_LINE);
        return NULL;
    }
    if (lexidentifier() != TK_IDENTIFIER) {
        CError_Error(ERR_IDENTIFIER_EXPECTED);
        return NULL;
    }

    value = getenv(tkidentifier->name);
    *buffer++ = '"';
    if (value) {
        while (len < 512 && (c = *value)) {
            len++;
            *buffer++ = *value++;
            if (c == '\\') {
                *buffer++ = '\\';
                len++;
            }
        }
    }
    *buffer = '"';
    buffer[1] = 0;

    if (!notendofline()) {
        CError_Error(ERR_UNEXPECTED_END_LINE);
        return NULL;
    }
    if (plex() != ')') {
        CError_Error(ERR_RPAREN_EXPECTED);
        return NULL;
    }
    return buffer + 1;
}

static Boolean CPrep_CheckOption(void) {
    CompilerOption* option;
    SInt32 offset;
    Boolean result;

    if (!notendofline()) {
        CError_Error(ERR_UNEXPECTED_END_LINE);
        return FALSE;
    }

    if (plex() != '(') {
        CError_Error(ERR_LPAREN_EXPECTED);
        return FALSE;
    }

    if (!notendofline()) {
        CError_Error(ERR_UNEXPECTED_END_LINE);
        return FALSE;
    }

    if (lexidentifier() != TK_IDENTIFIER) {
        CError_Error(ERR_IDENTIFIER_EXPECTED);
        return FALSE;
    }

    if (!strcmp(tkidentifier->name, "scheduling")) {
        result = copts.schedule_factor != 0;
    } else if (!strcmp(tkidentifier->name, "floatingpoint")) {
        result = copts.use_fp;
    } else if (!strcmp(tkidentifier->name, "sfp_emulation")) {
        result = copts.soft_float;
    } else if (!strcmp(tkidentifier->name, "precompile")) {
        result = cparamblkptr->precompile == 1;
    } else if (!strcmp(tkidentifier->name, "preprocess")) {
        result = cparamblkptr->preprocess;
    } else {
        for (option = compileroptions; option->name; option++) {
            if (!strcmp(tkidentifier->name, option->name)) {
                offset = option->bits & OPT_OFFSET_MASK;
                result = ((UInt8*)&copts)[offset];
                if (offset == OPT_OFFSET(booltruefalse) && !copts.cplusplus) {
                    result = FALSE;
                }
                goto gotResult;
            }
        }
        result = FALSE;
    }

gotResult:
    if (!notendofline()) {
        CError_Error(ERR_UNEXPECTED_END_LINE);
        return FALSE;
    }

    if (plex() != ')') {
        CError_Error(ERR_RPAREN_EXPECTED);
        return FALSE;
    }

    return result;
}

static char* CPrep_XpandDefinedCheck(char* str) {
    while (*str == ' ' || (*str >= 9 && *str <= 12)) {
        str++;
    }

    if (*str == '(') {
        str++;
        while (*str == ' ' || (*str >= 9 && *str <= 12)) {
            str++;
        }
    }

    if ((*str >= 'a' && *str <= 'z') || (*str >= 'A' && *str <= 'Z') || *str == '_') {
        return ReadIdentifier(str);
    }

    return NULL;
}

// TODO: a copy of poptokenseq for XpandString, which the original compiles as if the macro pointer
// were a local (poptokenseq's other callers compile it without one); one definition should serve
// both once its shape is the original's.
static void poptokenseqInline(void) { // TODO: invented name
    Macro* macro;

    if (!--tokenstacklevel && dofreeaheap) {
        freeaheap();
    }

    pos = tokenstack[tokenstacklevel].pos;
    macrostart = tokenstack[tokenstacklevel].macrostart;
    if ((macro = tokenstack[tokenstacklevel].macro)) {
        macro->isExpanding = FALSE;
    }
    macrocheck = tokenstack[tokenstacklevel].macrocheck;
    spaceskip = TRUE;
}

static char* XpandString(Macro* macro, char* text) {
    short character;
    short initialDepth;
    char* token;
    char* quotedText;
    char* remainingText;
    Macro* expansion;
    SInt32 length;
    char* next;
    char* savedText;
    int split;
    SInt32 size;

    pushtokenseq(macro);
    macrostart = macropos = text;
    initialDepth = tokenstacklevel;
    pos = text;
    mlist.size = 0;

    goto checkDepth;
    for (;;) {
        switch ((char)*pos) {
            case 0:
                break;

            case TK_MACRO_NOEXPAND:
                AppendGListByte(&mlist, TK_MACRO_NOEXPAND);
                pos++;
                pos = ReadIdentifier((token = pos));
                if (cprep_incondexpr != 0) {
                    if (!strcmp(tkidentifier->name, "defined")) {
                        next = CPrep_XpandDefinedCheck(pos);
                        if (next != NULL) {
                            pos = next;
                        }
                    }
                }
                AppendGListData(&mlist, token, pos - token);
                continue;

            case '"':
            case '\'':
                quotedText = pos++;
                CPrep_MatchChar(*quotedText, FALSE);
                AppendGListData(&mlist, quotedText, pos - quotedText);
                continue;

            case 'A':
            case 'B':
            case 'C':
            case 'D':
            case 'E':
            case 'F':
            case 'G':
            case 'H':
            case 'I':
            case 'J':
            case 'K':
            case 'L':
            case 'M':
            case 'N':
            case 'O':
            case 'P':
            case 'Q':
            case 'R':
            case 'S':
            case 'T':
            case 'U':
            case 'V':
            case 'W':
            case 'X':
            case 'Y':
            case 'Z':
            case '_':
            case 'a':
            case 'b':
            case 'c':
            case 'd':
            case 'e':
            case 'f':
            case 'g':
            case 'h':
            case 'i':
            case 'j':
            case 'k':
            case 'l':
            case 'm':
            case 'n':
            case 'o':
            case 'p':
            case 'q':
            case 'r':
            case 's':
            case 't':
            case 'u':
            case 'v':
            case 'w':
            case 'x':
            case 'y':
            case 'z':
                pos = ReadIdentifier((token = pos));
                waslockedmacro = FALSE;
                if (cprep_incondexpr != 0) {
                    if (!strcmp(tkidentifier->name, "defined")) {
                        next = CPrep_XpandDefinedCheck(pos);
                        if (next != NULL) {
                            pos = next;
                            AppendGListData(&mlist, token, next - token);
                            continue;
                        }
                    }
                }
                if (macro != NULL) {
                    expansion = ismacroname2(pos);
                } else {
                    expansion = ismacroname5(pos);
                }
                if (expansion != NULL) {
                    AppendGListByte(&mlist, TK_MACRO_BOUNDARY);
                    length = mlist.size;
                    if (length > 0x20000) {
                        macropos = pos;
                        CPrep_Error(ERR_MACROS_TOO_COMPLEX);
                        return "";
                    }
                    savedText = aalloc(length);
                    memcpy(savedText, *mlist.data, length);
                    token = XpandMacro(expansion);
                    if (tokenstacklevel == initialDepth) {
                        split = strlen(token) - 1;
                        for (; split > 0; split--) {
                            switch ((char)token[split]) {
                                case ' ':
                                case '"':
                                case '\'':
                                case ';':
                                    break;
                                default:
                                    continue;
                            }
                            break;
                        }
                        if (split > 0) {
                            mlist.size = 0;
                            AppendGListName(&mlist, &token[split + 1]);
                            AppendGListID(&mlist, pos);
                            pos = aalloc(mlist.size);
                            memcpy(pos, *mlist.data, mlist.size);
                            mlist.size = 0;
                            AppendGListData(&mlist, savedText, length);
                            AppendGListData(&mlist, token, split + 1);
                        } else {
                            mlist.size = 0;
                            AppendGListName(&mlist, token);
                            AppendGListID(&mlist, pos);
                            remainingText = aalloc(mlist.size);
                            memcpy(remainingText, *mlist.data, mlist.size);
                            mlist.size = 0;
                            pos = remainingText;
                            AppendGListData(&mlist, savedText, length);
                        }
                        goto checkDepth;
                    } else {
                        mlist.size = 0;
                        AppendGListData(&mlist, savedText, length);
                        AppendGListID(&mlist, token);
                        if (tokenstacklevel < initialDepth) {
                            goto allocateResult;
                        }
                        mlist.size--;
                        continue;
                    }
                } else {
                    if (waslockedmacro != 0) {
                        AppendGListByte(&mlist, TK_MACRO_NOEXPAND);
                    }
                    AppendGListData(&mlist, token, pos - token);
                }
                continue;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                AppendGListByte(&mlist, *pos++);
                for (;;) {
                    character = (char)*pos;
                    if ((character >= 'a' && character <= 'z') ||
                        (character >= 'A' && character <= 'Z') ||
                        character == '_' ||
                        (character >= '0' && character <= '9')) {
                        AppendGListByte(&mlist, character);
                        pos++;
                    } else {
                        break;
                    }
                }
                continue;

            default:
                AppendGListByte(&mlist, *pos++);
                continue;
        }
        if (tokenstacklevel >= initialDepth) {
            poptokenseqInline();
        }
checkDepth:
        if (tokenstacklevel >= initialDepth) {
            continue;
        }
        break;
    }

    AppendGListByte(&mlist, 0);
allocateResult:
    size = mlist.size;
    if (size > 0x20000) {
        CPrep_Error(ERR_MACROS_TOO_COMPLEX);
        return "";
    }
    savedText = aalloc(size);
    memcpy(savedText, *mlist.data, size);
    return savedText;
}

// NOTE: invented names: buffer, mainFile, quoted
char* CPrep_GetFileName(char* buffer, Boolean mainFile, Boolean quoted) {
    Str255 filename;
    int size;
    char* dst;
    int i;

    COS_FileGetFSSpecInfo(&filestack[mainFile ? 0 : filesp]->textfile, NULL, NULL, filename);

    size = filename[0];
    if (!buffer) {
        buffer = galloc(size + 3);
    }

    dst = buffer;

    if (quoted) {
        *(dst++) = '"';
    }

    i = 0;
    while (i < size) {
        i++;
        *(dst++) = filename[i];
    }

    if (quoted) {
        *(dst++) = '"';
    }

    *dst = 0;

    return buffer;
}

// Converts a Pascal string to a C string in place (a CompilerTools function in later releases;
// this one has no out-of-line copy)
static inline char* CTool_PtoCstr(StringPtr str) {
    StringPtr start = str;
    int n;
    for (n = str[0]; n > 0; n--) {
        *str = str[1];
        str++;
    }
    *str = 0;
    return (char*)start;
}

// __FILE__ gives the file's name (with fullpath_file, its path), quoted, with '\\' and '"' escaped.
static void CPrep_GetQuotedFileName(char* buffer) { // NOTE: invented name
    char filename[256];
    char* src;
    int len;

    if (copts.fullpath_file) {
        COS_FileGetPathName(filename, &filestack[filesp]->textfile, NULL);
    } else {
        COS_FileGetFSSpecInfo(&filestack[filesp]->textfile, NULL, NULL, (StringPtr)filename);
        CTool_PtoCstr((StringPtr)filename);
    }

    src = filename;
    *buffer++ = '"';
    for (len = 1; *src && len < 250; len++) {
        if (*src == '\\' || *src == '"') {
            *buffer++ = '\\';
            len++;
        }
        *buffer++ = *src++;
    }
    *buffer = '"';
    buffer[1] = 0;
}

static char* XpandSpecialMacro(Macro* macro) {
    char buf[512];
    char smallbuf[32];
    char* strptr;
    struct tm* tm;

    if (macro == &optiM) {
        return CPrep_CheckOption() ? "1" : "0";
    } else if (macro == &trgtM) {
        return CPrep_CheckTarget() ? "1" : "0";
    } else if (macro == &lineM) {
        sprintf(buf, "%ld", linenumber);
do_string:
        strptr = aalloc(strlen(buf) + 1);
        strcpy(strptr, buf);
        return strptr;
    } else if (macro == &fileM) {
        CPrep_GetQuotedFileName(buf);
        goto do_string;
    } else if (macro == &dateM) {
        tm = localtime(&now_time);
        strftime(buf, 64, "\"%b ", tm);
        strftime(smallbuf, 32, "%d", tm);
        if (smallbuf[0] == '0') {
            smallbuf[0] = ' ';
        }
        strcat(buf, smallbuf);
        strftime(smallbuf, 32, " %Y\"", tm);
        strcat(buf, smallbuf);
        goto do_string;
    } else if (macro == &timeM) {
        strftime(buf, 64, "\"%H:%M:%S\"", localtime(&now_time));
        goto do_string;
    } else if (macro == &stdcM) {
        return "1";
    } else if (macro == &casmM || macro == &MWRSM) {
        return "0x2407";
    } else if (macro == &cpplM) {
        return "199711L";
    } else if (macro == &dtsomM) {
        return copts.direct_to_som ? "1" : "0";
    } else if (macro == &ecppM) {
        return copts.ecplusplus ? "1" : "0";
    } else if (macro == &stcvM) {
        return copts.c9x ? "199901L" : "199409L";
    } else if (macro == &stchM) {
        return "0";
    } else if (macro == &envM) {
        CPrep_GetQuotedEnvVar(buf);
        goto do_string;
    } else {
        return CodeGen_ExpandSpecialMacro(macro);
    }
}

static char* XpandMacro(Macro* macro) {
    char* text;
    Boolean variadicArgument;
    SInt16 argument;
    char** arguments;
    char** expandedArguments;
    SInt32 argumentCount;
    SInt32 parameterCount;
    Boolean variadic;
    UInt16 nargs;
    SInt16 argumentIndex;
    char* result;
    char* argumentText;
    SInt16 character;
    SInt32 length;
    char* tokenStart;
    SInt16 token;
    SInt16 depth;
    SInt16 firstToken;

    if (macro->is_special) {
        return XpandSpecialMacro(macro);
    }

    nargs = macro->nargs & 0x7fff;
    variadic = (macro->nargs & 0x8000) != 0;
    if (nargs != 0) {
        gotonexttoken();
        CError_ASSERT(3360, prepnextchar() == '(');

        if (nargs > 1) {
            variadicArgument = variadic && nargs == 2;
            parameterCount = (argumentCount = nargs) - 1;
            length = parameterCount * sizeof(char*);
            arguments = aalloc(length);
            expandedArguments = aalloc(length);
            argument = 1;
            for (; argument < argumentCount;) {
                firstToken = TRUE;
                depth = 0;
                mlist.size = 0;
                for (;;) {
                    spaceskip = FALSE;
                    for (;;) {
                        token = prepnextchar();
                        switch (token) {
                            case CR:
                                foundnl();
                                spaceskip = TRUE;
                                continue;
                            case '\t':
                            case LF:
                            case '\v':
                            case '\f':
                            case ' ':
                                spaceskip = TRUE;
                                continue;
                            case 0:
                                if (tokenstacklevel != 0) {
                                    poptokenseq();
                                    continue;
                                }
                                CPrep_Error(ERR_UNEXPECTED_END_FILE);
                                goto argumentFinished;
                            case '"':
                            case '\'':
                                tokenStart = pos - 1;
                                CPrep_MatchChar(token, FALSE);
                                AppendGListData(&mlist, tokenStart, pos - tokenStart);
                                goto tokenFinished;
                            case ')':
                                depth--;
                                if (depth < 0) {
                                    argument++;
                                    if (argument < argumentCount) {
                                        macropos = pos;
                                        CPrep_Error(ERR_COMMA_EXPECTED);
                                        return "";
                                    }
                                    goto argumentFinished;
                                }
                                break;
                            case ',':
                                if (depth <= 0 && !variadicArgument) {
                                    argument++;
                                    if (variadic && argument == parameterCount) {
                                        variadicArgument = TRUE;
                                    } else if (argument >= argumentCount) {
                                        macropos = pos;
                                        CPrep_Error(ERR_RPAREN_EXPECTED);
                                        return "";
                                    }
                                    goto argumentFinished;
                                }
                                break;
                            case '(':
                                depth++;
                            default:
                                break;
                        }
                        if (spaceskip != 0 && !firstToken) {
                            AppendGListByte(&mlist, ' ');
                        }
                        AppendGListByte(&mlist, token);
                        firstToken = FALSE;
tokenFinished:
                        break;
                    }
                }
argumentFinished:
                expandedArguments[argument - 2] = arguments[argument - 2] = NULL;
                if (mlist.size > 0) {
                    AppendGListByte(&mlist, 0);
                    length = mlist.size;
                    if (length <= 0x20000) {
                        arguments[argument - 2] = aalloc(length);
                        memcpy(arguments[argument - 2], *mlist.data, length);
                    } else {
                        CPrep_Error(ERR_MACROS_TOO_COMPLEX);
                    }
                }
            }
        } else {
            gotonexttoken();
            if (prepnextchar() != ')') {
                macropos = pos;
                CPrep_Error(ERR_RPAREN_EXPECTED);
            }
        }
    }

    if ((text = macro->text) == NULL) {
        return "";
    }
    if (nargs <= 1 && !variadic) {
        return XpandString(macro, text);
    }

    mlist.size = 0;
    while ((token = *text++) != 0) {
        switch (token) {
            case '"':
            case '\'': {
                result = text - 1;
                text = CPrep_MatchChar2(text, token, FALSE);
                AppendGListData(&mlist, result, text - result);
                continue;
            }
            case TK_MACRO_ARG_PASTED:
                if ((argumentText = arguments[*text++ - 1]) != NULL) {
                    for (;;) {
                        switch (*argumentText) {
                            case 0:
                                break;
                            case TK_MACRO_NOEXPAND:
                            case TK_MACRO_BOUNDARY:
                                argumentText++;
                                continue;
                            default:
                                AppendGListByte(&mlist, *argumentText++);
                                continue;
                        }
                        break;
                    }
                }
                continue;
            case TK_MACRO_ARG_HASH:
                AppendGListByte(&mlist, '"');
                if ((argumentText = arguments[*text++ - 1]) != NULL) {
                    while ((character = *argumentText++) != 0) {
                        switch (character) {
                            case '"':
                                AppendGListByte(&mlist, '\\');
                            case '\'':
                                AppendGListByte(&mlist, character);
                                while (*argumentText != 0 && *argumentText != character) {
                                    if (*argumentText == '"' || *argumentText == '\\') {
                                        AppendGListByte(&mlist, '\\');
                                    }
                                    AppendGListByte(&mlist, *argumentText++);
                                }
                                if (*argumentText == '"') {
                                    AppendGListByte(&mlist, '\\');
                                }
                                if (*argumentText != 0) {
                                    AppendGListByte(&mlist, *argumentText++);
                                }
                                break;
                            case TK_MACRO_NOEXPAND:
                            case TK_MACRO_BOUNDARY:
                                break;
                            default:
                                AppendGListByte(&mlist, character);
                                break;
                        }
                    }
                }
                AppendGListByte(&mlist, '"');
                continue;
            case TK_MACRO_ARG_EXPANDED: {
                char* savedText;
                SInt32 savedLength;
                if ((argumentText = expandedArguments[argumentIndex = *text++ - 1]) == NULL) {
                    if (arguments[argumentIndex] == NULL) {
                        continue;
                    }
                    savedLength = mlist.size;
                    savedText = aalloc(savedLength);
                    memcpy(savedText, *mlist.data, savedLength);
                    expandedArguments[argumentIndex] = XpandString(NULL, arguments[argumentIndex]);
                    argumentText = expandedArguments[argumentIndex];
                    mlist.size = 0;
                    AppendGListData(&mlist, savedText, savedLength);
                }
                while (*argumentText != 0) {
                    AppendGListByte(&mlist, *argumentText++);
                }
                if (argumentText[-1] == '>' && *text == '>') {
                    AppendGListByte(&mlist, ' ');
                }
                continue;
            }
            default:
                AppendGListByte(&mlist, token);
        }
    }
    AppendGListByte(&mlist, 0);
    length = mlist.size;
    if (length > 0x20000) {
        CPrep_Error(ERR_MACROS_TOO_COMPLEX);
        return "";
    }
    result = aalloc(length);
    memcpy(result, *mlist.data, mlist.size);
    return XpandString(macro, result);
}

static void prepmacro(Macro* macro) {
    Boolean save_spaceskip;
    char* result;

    dofreeaheap = macrocheck = FALSE;
    save_spaceskip = spaceskip;
    result = XpandMacro(macro);
    macrocheck = TRUE;
    pushtokenseq(NULL);
    macrostart = pos = macropos = result;
    dofreeaheap = TRUE;
    macrocheck = FALSE;
    spaceskip = save_spaceskip;
}

Boolean macrotest(void) {
    Macro* macro;

    if ((macro = ismacroname())) {
        prepmacro(macro);
        return TRUE;
    } else {
        return FALSE;
    }
}

short CPrep_PragmaLex(Boolean no_err) {
    if (notendofline()) {
        return plex();
    }
    if (!no_err) {
        CPrep_Error(ERR_END_LINE_EXPECTED);
    }
    return 0;
}

void CPrep_PushOption(SInt32 id, char value) {
    struct COptPush* opt;
    struct COptPush* newopt;

    opt = coptpush;
    do {
        if (!opt || (opt->pushed && opt->id == id)) {
            newopt = galloc(sizeof(struct COptPush));
            newopt->next = coptpush;
            coptpush = opt = newopt;
            break;
        }
        if (!opt->pushed) {
            break;
        }
        opt = opt->next;
    } while (1);

    opt->value = ((UInt8*)&copts)[id];
    opt->id = id;
    opt->pushed = TRUE;
    ((UInt8*)&copts)[id] = value;
}

void CPrep_PopOption(SInt32 id) {
    struct COptPush* opt;

    for (opt = coptpush; opt; opt = opt->next) {
        if (opt->pushed && opt->id == id) {
            ((UInt8*)&copts)[id] = opt->value;
            opt->pushed = FALSE;
            return;
        }
    }

    ((UInt8*)&copts)[id] = ((UInt8*)coptssave)[id];
}

static void CPrep_PragmaImExport(int flags) {
    NameSpaceObjectList* list;

    do {
        if (!notendofline() ||
            plex() != TK_IDENTIFIER ||
            !(list = CScope_GetLocalObject(cscope_root, tkidentifier))) {
            break;
        }

        while (list) {
            if (list->object->otype == OT_OBJECT) {
                switch (OBJECT(list->object)->datatype) {
                    case DDATA:
                    case DFUNC:
                        OBJECT(list->object)->flags |= flags;
                        break;
                    default:
                        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                        return;
                }
            }
            list = list->next;
        }

        if (!notendofline()) {
            return;
        }
    } while (plex() == ',' && notendofline());

    if (copts.warn_illpragma) {
        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
    }
}

// (`bits` is an int: preppragma tests the bits it passes here as the same int)
static void pragma_on_off_reset(int bits) {
    int imex_flags;
    Boolean allowList = (bits & OPT_FLAG_LIST) != 0; // NOTE: invented name

    if (notendofline() && plex() == TK_IDENTIFIER) {
        if (!strcmp(tkidentifier->name, "on")) {
            CPrep_PushOption(bits & OPT_OFFSET_MASK, TRUE);
            return;
        }
        if (!strcmp(tkidentifier->name, "off")) {
            CPrep_PushOption(bits & OPT_OFFSET_MASK, FALSE);
            return;
        }
        if (!strcmp(tkidentifier->name, "reset")) {
            CPrep_PopOption(bits & OPT_OFFSET_MASK);
            return;
        }
        if (allowList && !strcmp(tkidentifier->name, "list")) {
            if (cparamblkptr->preprocess) {
                skipendofline();
                return;
            }

            macrocheck = TRUE;
            if ((bits & OPT_OFFSET_MASK) == OPT_OFFSET(cfm_internal)) {
                imex_flags = OBJECT_INTERNAL;
            } else if ((bits & OPT_OFFSET_MASK) == OPT_OFFSET(cfm_import)) {
                imex_flags = OBJECT_IMPORT;
            } else if ((bits & OPT_OFFSET_MASK) == OPT_OFFSET(cfm_export)) {
                imex_flags = OBJECT_EXPORT;
            } else if ((bits & OPT_OFFSET_MASK) == OPT_OFFSET(cfm_lib_export)) {
                imex_flags = OBJECT_LIB_EXPORT;
            } else {
                CError_FATAL(3677);
            }
            CPrep_PragmaImExport(imex_flags);
            return;
        }
    }

    if (copts.warn_illpragma) {
        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
    }
}

// NOTE: invented names: name, isSystemInclude
static inline HashNameNode* CPrep_PragmaOnceName(StringPtr name, Boolean isSystemInclude) {
    char buf[256];
    char* myname;
    char* src;
    char* dst;
    short len;

    len = name[0];
    if (len > 250) {
        len = 250;
    }

    myname = buf;
    myname++;
    dst = myname;
    src = (char*)&name[1];
    while (len-- > 0) {
        *(dst++) = tolower(*(src++));
    }
    *dst = 0;

    *(--myname) = (copts.syspath_once && isSystemInclude) ? '$' : -'\\';
    return GetHashNameNodeExport(myname);
}

static void pragma_precompile_target(void) {
    if (!notendofline()) {
        CPrep_Error(ERR_UNEXPECTED_END_LINE);
        return;
    }

    if (plex() != TK_STRING) {
        CPrep_Error(ERR_PREPROCESSOR_SYNTAX_ERROR);
        skipendofline();
        return;
    }

    if (ispascalstring) {
        CPrep_Error(ERR_ILLEGAL_STRING_CONSTANT);
        skipendofline();
        return;
    }

    precomp_target_str = tkstring;
    goendofline();
}

// NOTE: invented names: name, isSystemInclude
static inline void CPrep_DefinePragmaOnceMacro(StringPtr name, Boolean isSystemInclude) {
    HashNameNode* namenode;
    Macro* macro;

    namenode = CPrep_PragmaOnceName(name, isSystemInclude);
    if (!ismacroname4(namenode)) {
        macro = galloc(sizeof(Macro));
        memclrw(macro, sizeof(Macro));
        macro->name = namenode;
        macro->next = macrohashtable[namenode->hashval];
        macrohashtable[namenode->hashval] = macro;
    }
}

static void CPrep_PragmaOnce(void) {
    if (notendofline()) {
        if (plex() == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "on")) {
                include_once = TRUE;
                return;
            } else if (!strcmp(tkidentifier->name, "off")) {
                include_once = FALSE;
                return;
            }
        }
        if (copts.warn_illpragma) {
            CPrep_Warning(ERR_ILLEGAL_PRAGMA);
        }
        return;
    } else {
        unsigned char filename[256];
        COS_FileGetFSSpecInfo(&prep_file->textfile, NULL, NULL, filename);
        CPrep_DefinePragmaOnceMacro(filename, prep_file->isSystemInclude);
    }
}

static void CPrep_PragmaUnused(void) {
    NameSpace* nspace;
    NameSpaceObjectList* list;
    short t;

    if (cparamblkptr->preprocess) {
        skipendofline();
        return;
    }

    if (cscope_currentfunc && notendofline() && plex() == '(' && notendofline()) {
restart:
        if (plex() == TK_IDENTIFIER) {
            for (nspace = cscope_current; nspace; nspace = nspace->parent) {
                if (nspace->is_global) {
                    break;
                }

                list = CScope_GetLocalObject(nspace, tkidentifier);
                if (list &&
                    list->object->otype == OT_OBJECT &&
                    OBJECT(list->object)->datatype == DLOCAL) {
                    OBJECT(list->object)->flags |= OBJECT_USED;
                    if (notendofline()) {
                        t = plex();
                        if (t == ')') {
                            return;
                        }
                        if (t == ',' && notendofline()) {
                            goto restart;
                        }
                    }
                    break;
                }
            }
        }
    }

    if (copts.warn_illpragma) {
        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
    }
}

static void CPrep_PragmaInlineDepth(void) {
    int lv;

    if (notendofline() && plex() == '(' && notendofline()) {
        switch (plex()) {
            case TK_IDENTIFIER:
                if (strcmp(tkidentifier->name, "smart")) {
                    CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                } else {
                    copts.inlinelevel = 0;
                }
                break;
            case TK_INTCONST:
                lv = CInt64_GetULong(&tkintconst);
                if (lv >= 0 && lv <= 1024) {
                    if (!lv) {
                        lv = -1;
                    }
                    copts.inlinelevel = lv;
                } else {
                    CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                }
                break;
            default:
                CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                skipendofline();
                return;
        }

        if (!notendofline() || plex() != ')') {
            CPrep_Warning(ERR_RPAREN_EXPECTED);
        }
        if (notendofline()) {
            CPrep_Warning(ERR_END_LINE_EXPECTED);
            skipendofline();
        }
        return;
    }

    if (copts.warn_illpragma) {
        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
    }
}

static void CPrep_PragmaInlineMaxSize(void) {
    if (notendofline() && plex() == '(' && notendofline()) {
        switch (plex()) {
            case TK_INTCONST:
                copts.inlinemaxsize = CInt64_GetULong(&tkintconst);
                break;
            default:
                CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                skipendofline();
                return;
        }

        if (!notendofline() || plex() != ')') {
            CPrep_Warning(ERR_RPAREN_EXPECTED);
        }
        if (notendofline()) {
            CPrep_Warning(ERR_END_LINE_EXPECTED);
            skipendofline();
        }
        return;
    }

    if (copts.warn_illpragma) {
        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
    }
}

static void CPrep_PragmaInlineMaxTotalSize(void) {
    if (notendofline() && plex() == '(' && notendofline()) {
        switch (plex()) {
            case TK_INTCONST:
                copts.inlinemaxtotalsize = CInt64_GetULong(&tkintconst);
                break;
            default:
                CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                skipendofline();
                return;
        }

        if (!notendofline() || plex() != ')') {
            CPrep_Warning(ERR_RPAREN_EXPECTED);
        }
        if (notendofline()) {
            CPrep_Warning(ERR_END_LINE_EXPECTED);
            skipendofline();
        }
        return;
    }

    if (copts.warn_illpragma) {
        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
    }
}

static void pragma_segment(void) {
    short i;
    short t;
    char name[256];

    if (notendofline()) {
        for (i = 0; i < 255; i++) {
            spaceskip = FALSE;
            t = prepskipnextchar();
            if (spaceskip) {
                break;
            }
            if (t <= ' ') {
                break;
            }
            name[i] = t;
            pos = nextcharpos;
        }
        name[i] = 0;

        if (!i || i >= 255) {
            CPrep_Warning(ERR_ILLEGAL_PRAGMA);
        }

        copts.cursegmentname = GetHashNameNodeExport(name);
        ObjGen_SegmentName(copts.cursegmentname);
        return;
    }

    if (copts.warn_illpragma) {
        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
    }
}

static void pragma_options(void) {
    if (notendofline() &&
        plex() == TK_IDENTIFIER &&
        !strcmp(tkidentifier->name, "align") &&
        notendofline() &&
        plex() == '=' &&
        notendofline() &&
        plex() == TK_IDENTIFIER) {
        if (!strcmp(tkidentifier->name, "reset")) {
            CPrep_PopOption(OPT_OFFSET(structalignment));
            goto done;
        } else if (!strcmp(tkidentifier->name, "native")) {
            CPrep_PushOption(OPT_OFFSET(structalignment), AlignMode2_PPC);
            goto done;
        } else if (!strcmp(tkidentifier->name, "mac68k")) {
            CPrep_PushOption(OPT_OFFSET(structalignment), AlignMode0_Mac68k);
            goto done;
        } else if (!strcmp(tkidentifier->name, "mac68k4byte")) {
            CPrep_PushOption(OPT_OFFSET(structalignment), AlignMode1_Mac68k4byte);
            goto done;
        } else if (!strcmp(tkidentifier->name, "power")) {
            CPrep_PushOption(OPT_OFFSET(structalignment), AlignMode2_PPC);
            goto done;
        } else if (!strcmp(tkidentifier->name, "packed")) {
            CPrep_PushOption(OPT_OFFSET(structalignment), AlignMode8_Packed);
            goto done;
        }
    }

    if (copts.warn_illpragma) {
        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
    }
    skipendofline();
    return;

done:
    if (notendofline()) {
        CPrep_Error(ERR_END_LINE_EXPECTED);
        skipendofline();
    }
}

static void pragma_push(void) {
    COptsPush* push;

    CodeGen_PragmaPush();
    push = galloc(sizeof(COptsPush));
    push->next = coptpushs;
    coptpushs = push;
    push->opts = copts;
}

static void pragma_pop(void) {
    if (coptpushs) {
        copts = coptpushs->opts;
        coptpushs = coptpushs->next;
        CodeGen_PragmaPop();
        CMach_Configure();
    } else {
        CPrep_Error(ERR_PRECEDING_PRAGMA_PUSH_MISSING);
    }
}

static void pragma_overload(void) {
    Object* obj;

    if (cparamblkptr->preprocess) {
        skipendofline();
        return;
    }

    nlflag = FALSE;
    tk = lex();
    if ((obj = CParser_ParseObject())) {
        if (obj->sclass && obj->sclass != TK_EXTERN) {
            CPrep_Error(ERR_ILLEGAL_STORAGE_CLASS);
        }
        obj->qual |= Q_WEAK;
    } else {
        CPrep_Error(ERR_ILLEGAL_PRAGMA);
    }

    if (nlflag) {
        CPrep_Error(ERR_UNEXPECTED_END_LINE);
    }
    if (tk != ';') {
        CPrep_Error(ERR_SEMICOLON_EXPECTED);
    }
}

static void pragma_optimization_level(void) {
    short t;
    int lv;

    if (notendofline()) {
        t = plex();
        if (t == TK_INTCONST) {
            lv = CInt64_GetULong(&tkintconst);
            if (lv >= 0 && lv <= 4) {
                if (copts.crippled && lv > 1) {
                    CPrep_Warning(ERR_ILLEGAL_OPTIMIZATION_LEVEL_LIMITED);
                    lv = 1;
                }
                CPrep_PushOption(OPT_OFFSET(optimizationlevel), lv);
                CodeGen_UpdateOptimizerOptions();
                CodeGen_UpdateBackEndOptions();
                return;
            }
            CPrep_Error(ERR_NUMBER_OUT_RANGE);
            skipendofline();
            return;
        }
        if (t == TK_IDENTIFIER && !strcmp(tkidentifier->name, "reset")) {
            CPrep_PopOption(OPT_OFFSET(optimizationlevel));
            CodeGen_UpdateOptimizerOptions();
            CodeGen_UpdateBackEndOptions();
            return;
        }
        CPrep_Error(ERR_ILLEGAL_TOKEN);
        skipendofline();
        return;
    }

    if (copts.warn_illpragma) {
        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
    }
    skipendofline();
}

static void pragma_opt_unroll_count(void) {
    short t;
    int lv;

    if (notendofline()) {
        t = plex();
        if (t == TK_INTCONST) {
            lv = CInt64_GetULong(&tkintconst);
            if (lv >= 0 && lv <= 127) {
                CPrep_PushOption(OPT_OFFSET(unrollfactor), lv);
                CodeGen_UpdateOptimizerOptions();
                CodeGen_UpdateBackEndOptions();
                return;
            }
            CPrep_Error(ERR_NUMBER_OUT_RANGE);
            skipendofline();
            return;
        }
        if (t == TK_IDENTIFIER && !strcmp(tkidentifier->name, "reset")) {
            CPrep_PopOption(OPT_OFFSET(unrollfactor));
            CodeGen_UpdateOptimizerOptions();
            CodeGen_UpdateBackEndOptions();
            return;
        }
        CPrep_Error(ERR_ILLEGAL_TOKEN);
        skipendofline();
        return;
    }

    if (copts.warn_illpragma) {
        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
    }
    skipendofline();
}

static void pragma_opt_unroll_instr_count(void) {
    short t;
    int lv;

    if (notendofline()) {
        t = plex();
        if (t == TK_INTCONST) {
            lv = CInt64_GetULong(&tkintconst);
            if (lv >= 0 && lv <= 127) {
                CPrep_PushOption(OPT_OFFSET(unrollinstrfactor), lv);
                CodeGen_UpdateOptimizerOptions();
                CodeGen_UpdateBackEndOptions();
                return;
            }
            CPrep_Error(ERR_NUMBER_OUT_RANGE);
            skipendofline();
            return;
        }
        if (t == TK_IDENTIFIER && !strcmp(tkidentifier->name, "reset")) {
            CPrep_PopOption(OPT_OFFSET(unrollinstrfactor));
            CodeGen_UpdateOptimizerOptions();
            CodeGen_UpdateBackEndOptions();
            return;
        }
        CPrep_Error(ERR_ILLEGAL_TOKEN);
        skipendofline();
        return;
    }

    if (copts.warn_illpragma) {
        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
    }
    skipendofline();
}

static void pragma_pack(void) {
    SInt32 t;
    SInt32 i;
    Boolean did_pop;
    Boolean did_push;

    if (notendofline() && plex() == '(') {
        macrocheck = TRUE;
        // (when the line ends here, the loop below tests `t` unset: the original's)
        if (notendofline() && ((t = plex()) == ')')) {
            copts.structalignment = coptssave->structalignment;
        } else {
            did_push = did_pop = FALSE;
            do {
                if (t == TK_IDENTIFIER) {
                    if (!strcmp(tkidentifier->name, "push")) {
                        if (cprep_packstackp) {
                            cprep_packstackp--;
                            cprep_packstack[cprep_packstackp].identifier = NULL;
                            cprep_packstack[cprep_packstackp].align_mode = copts.structalignment;
                            did_push = TRUE;
                        } else {
                            CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                        }
                    } else if (!strcmp(tkidentifier->name, "pop")) {
                        if (cprep_packstackp < 128) {
                            copts.structalignment = cprep_packstack[cprep_packstackp].align_mode;
                            cprep_packstackp++;
                            did_pop = TRUE;
                        } else {
                            CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                        }
                    } else {
                        if (did_push) {
                            cprep_packstack[cprep_packstackp].identifier = tkidentifier;
                        } else if (did_pop) {
                            for (i = cprep_packstackp - 1; i < 128; i++) {
                                if (cprep_packstack[i].identifier == tkidentifier) {
                                    break;
                                }
                            }
                            if (i < 128) {
                                copts.structalignment = cprep_packstack[i].align_mode;
                                cprep_packstackp = i + 1;
                            } else {
                                CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                            }
                        } else {
                            CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                        }
                        did_push = did_pop = FALSE;
                    }
                } else {
                    if (t == TK_INTCONST) {
                        switch (CInt64_GetULong(&tkintconst)) {
                            case 0:
                                copts.structalignment = coptssave->structalignment;
                                break;
                            case 1:
                                copts.structalignment = AlignMode3_1Byte;
                                break;
                            case 2:
                                copts.structalignment = AlignMode4_2Byte;
                                break;
                            case 4:
                                copts.structalignment = AlignMode5_4Byte;
                                break;
                            case 8:
                                copts.structalignment = AlignMode6_8Byte;
                                break;
                            case 16:
                                copts.structalignment = AlignMode7_16Byte;
                                break;
                            default:
                                if (copts.warn_illpragma) {
                                    CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                                }
                        }
                    }
                    did_push = did_pop = FALSE;
                }

                if (notendofline()) {
                    t = plex();
                }
                if (t != ',') {
                    break;
                }
                t = plex();
            } while (1);
        }
        if (t != ')' && copts.warn_illpragma) {
            CPrep_Warning(ERR_ILLEGAL_PRAGMA);
        }
        macrocheck = FALSE;
    } else {
        if (copts.warn_illpragma) {
            CPrep_Warning(ERR_ILLEGAL_PRAGMA);
        }
    }
}

// dead: preppragma does not dispatch `#pragma comment`, but its literals are in the unit's .data
static void pragma_comment(void) {
    int t;
    short start_tokenstacklevel = tokenstacklevel;

    if (notendofline() && plex() == '(') {
        if (notendofline()) {
            t = plex();
            if (t == TK_IDENTIFIER) {
                if (!strcmp(tkidentifier->name, "compiler")) {
                    t = plex();
                } else if (!strcmp(tkidentifier->name, "exestr") ||
                           !strcmp(tkidentifier->name, "linker") ||
                           !strcmp(tkidentifier->name, "user")) {
                    if (plex() == ',') {
                        macrocheck = TRUE;
                        t = plex();
                        macrocheck = FALSE;
                        if (t == TK_STRING) {
                            while (t == TK_STRING) {
                                macrocheck = TRUE;
                                if (tokenstacklevel == start_tokenstacklevel && !notendofline()) {
                                    break;
                                }
                                macrocheck = TRUE;
                                t = plex();
                                macrocheck = FALSE;
                            }
                        } else {
                            if (copts.warn_illpragma) {
                                CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                            }
                            skipendofline();
                            return;
                        }
                    } else {
                        if (copts.warn_illpragma) {
                            CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                        }
                        skipendofline();
                        return;
                    }
                } else if (!strcmp(tkidentifier->name, "lib")) {
                    if (plex() == ',') {
                        macrocheck = TRUE;
                        t = plex();
                        macrocheck = FALSE;
                        if (t == TK_STRING) {
                            while (t == TK_STRING) {
                                macrocheck = TRUE;
                                if (tokenstacklevel == start_tokenstacklevel && !notendofline()) {
                                    break;
                                }
                                macrocheck = TRUE;
                                t = plex();
                                macrocheck = FALSE;
                            }
                        } else {
                            if (copts.warn_illpragma) {
                                CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                            }
                            skipendofline();
                            return;
                        }
                    } else {
                        if (copts.warn_illpragma) {
                            CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                        }
                        skipendofline();
                        return;
                    }
                } else {
                    if (copts.warn_illpragma) {
                        CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                    }
                    skipendofline();
                    return;
                }
            } else {
                if (copts.warn_illpragma) {
                    CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                }
                skipendofline();
                return;
            }
        }

        if (t != ')') {
            if (copts.warn_illpragma) {
                CPrep_Warning(ERR_ILLEGAL_PRAGMA);
            }
            skipendofline();
        }
    } else {
        if (copts.warn_illpragma) {
            CPrep_Warning(ERR_ILLEGAL_PRAGMA);
        }
        skipendofline();
    }
}

static void pragma_message(void) {
    char str[128];
    char* ptr;
    int start_tokenstacklevel;
    int t;

    if (notendofline() && plex() == '(') {
        if (notendofline()) {
            start_tokenstacklevel = tokenstacklevel;
            ptr = str;
            str[0] = 0;
            macrocheck = TRUE;
            t = plex();
            macrocheck = FALSE;
            if (t == TK_STRING) {
                while (t == TK_STRING) {
                    strncpy(ptr, tkstring, sizeof(str) - (ptr - str));
                    str[sizeof(str) - 1] = 0;
                    ptr = str + strlen(str);

                    macrocheck = TRUE;
                    if (tokenstacklevel == start_tokenstacklevel && !notendofline()) {
                        break;
                    }
                    macrocheck = TRUE;
                    t = plex();
                    macrocheck = FALSE;
                }

                CWReportMessage(cparamblkptr->context, NULL, str, NULL, messagetypeInfo, 337);
                skipendofline();
                return;
            } else {
                if (copts.warn_illpragma) {
                    CPrep_Warning(ERR_ILLEGAL_PRAGMA);
                }
                skipendofline();
                return;
            }
        } else if (plex() != ')') {
            if (copts.warn_illpragma) {
                CPrep_Warning(ERR_ILLEGAL_PRAGMA);
            }
            skipendofline();
            return;
        }
    } else {
        if (copts.warn_illpragma) {
            CPrep_Warning(ERR_ILLEGAL_PRAGMA);
        }
        skipendofline();
        return;
    }
}

static void preppragma(void) {
    CompilerOption* opt;
    Boolean save_eoltokens;

    if (!notendofline()) {
        return;
    }

    macrocheck = FALSE;
    if (lexidentifier() == TK_IDENTIFIER) {
        if (preprocessing_only) {
            CPrep_PreprocessDumpDirective("#pragma ", tkidentifier->name);
        }
        if (!strcmp(tkidentifier->name, "pack")) {
            pragma_pack();
        } else if (!strcmp(tkidentifier->name, "optimization_level")) {
            pragma_optimization_level();
        } else if (!strcmp(tkidentifier->name, "once")) {
            CPrep_PragmaOnce();
        } else if (!strcmp(tkidentifier->name, "unused")) {
            CPrep_PragmaUnused();
        } else if (!strcmp(tkidentifier->name, "inline_depth")) {
            CPrep_PragmaInlineDepth();
        } else if (!strcmp(tkidentifier->name, "inline_max_size")) {
            CPrep_PragmaInlineMaxSize();
        } else if (!strcmp(tkidentifier->name, "inline_max_total_size")) {
            CPrep_PragmaInlineMaxTotalSize();
        } else if (!strcmp(tkidentifier->name, "opt_pointer_analysis_mode")) {
            PointerAnalysis_PragmaMode();
        } else if (!strcmp(tkidentifier->name, "options")) {
            pragma_options();
            macrocheck = TRUE;
            goto exit;
        } else if (!strcmp(tkidentifier->name, "segment")) {
            pragma_segment();
        } else if (!strcmp(tkidentifier->name, "push")) {
            pragma_push();
        } else if (!strcmp(tkidentifier->name, "pop")) {
            pragma_pop();
        } else if (!strcmp(tkidentifier->name, "parameter")) {
            macrocheck = TRUE;
            CMach_PragmaParams();
        } else if (!strcmp(tkidentifier->name, "overload")) {
            macrocheck = TRUE;
            pragma_overload();
        } else if (!strcmp(tkidentifier->name, "mark")) {
            skipendofline();
            macrocheck = TRUE;
            goto exit;
        } else if (!strcmp(tkidentifier->name, "precompile_target")) {
            pragma_precompile_target();
        } else if (!strcmp(tkidentifier->name, "message")) {
            pragma_message();
        } else if (!strcmp(tkidentifier->name, "opt_unroll_count")) {
            pragma_opt_unroll_count();
        } else if (!strcmp(tkidentifier->name, "opt_unroll_instr_count")) {
            pragma_opt_unroll_instr_count();
        } else if (!strcmp(tkidentifier->name, "exception_terminate")) {
            if (!preprocessing_only) {
                CExcept_Terminate();
            }
        } else if (!strcmp(tkidentifier->name, "exception_arrayinit")) {
            if (!preprocessing_only) {
                CExcept_ArrayInit();
            }
        } else if (!strcmp(tkidentifier->name, "exception_magic")) {
            if (!preprocessing_only) {
                CExcept_Magic();
            }
        } else if (!strcmp(tkidentifier->name, "SOMReleaseOrder")) {
            macrocheck = TRUE;
            CSOM_PragmaReleaseOrder();
        } else if (!strcmp(tkidentifier->name, "SOMClassVersion")) {
            macrocheck = TRUE;
            CSOM_PragmaClassVersion();
        } else if (!strcmp(tkidentifier->name, "SOMMetaClass")) {
            macrocheck = TRUE;
            CSOM_PragmaMetaClass();
        } else if (!strcmp(tkidentifier->name, "SOMCallStyle")) {
            CSOM_PragmaCallStyle();
        } else {
            for (opt = compileroptions; opt->name; opt++) {
                if (!strcmp(tkidentifier->name, opt->name) && !(opt->bits & OPT_FLAG_NOPRAGMA)) {
                    pragma_on_off_reset(opt->bits);
                    if (opt->bits & OPT_FLAG_RECONFIGURE) {
                        CMach_Configure();
                    }
                    goto foundOption;
                }
            }

            save_eoltokens = cprep_eoltokens;
            cprep_eoltokens = TRUE;
            CodeGen_ParsePragma(tkidentifier);
            cprep_eoltokens = save_eoltokens;
            macrocheck = TRUE;
            goto exit;
        }
    } else {
        if (copts.warn_illpragma) {
            CPrep_Warning(ERR_ILLEGAL_PRAGMA);
        }
        skipendofline();
        macrocheck = TRUE;
        goto exit;
    }
foundOption:
    goendofline();
    CPrep_Reconfig();
    macrocheck = TRUE;
exit:
    if (preprocessing_only) {
        CPrep_PreprocessDumpDirectiveEnd();
    }
}

static void prepinclude(Boolean isImport) { // NOTE: invented name: isImport
    short t;
    short c;
    Boolean fullsearch;
    short len;
    char* save_pos;
    char buf[256];
    StringPtr ptr;

    if (!notendofline()) {
        CPrep_Error(ERR_UNEXPECTED_END_LINE);
        return;
    }

    t = prepnextchar();
    if (t != '"' && t != '<') {
        CPrep_Error(ERR_PREPROCESSOR_SYNTAX_ERROR);
        skipendofline();
        return;
    }

    save_pos = pos;
    fullsearch = TRUE;
    if (t == '<') {
        t = '>';
        if (!copts.flat_include) {
            fullsearch = FALSE;
        }
    }
    len = 0;
    while ((c = prepnextstringchar((StringPtr)save_pos, TRUE)) != t) {
        if (c != TK_MACRO_BOUNDARY) {
            if (len > 254) {
                CPrep_Error(ERR_STRING_TOO_LONG);
                skipendofline();
                return;
            }
            buf[++len] = c;
        }
    }
    buf[0] = len;
    ptr = (StringPtr)buf;

    goendofline();
    if (!ismacroname4(CPrep_PragmaOnceName((StringPtr)buf, !fullsearch))) {
        if (isImport || copts.always_import) {
            CPrep_DefinePragmaOnceMacro((StringPtr)buf, !fullsearch);
        }

        if (copts.flat_include) {
            for (t = len; t > 0; t--) {
                switch (buf[t]) {
                    case ':':
                    case '/':
                    case '\\':
                        ptr = (StringPtr)buf;
                        ptr += t;
                        buf[t] = len - t;
                        goto all_done;
                }
            }
        }

all_done:
        setupfile(ptr, fullsearch, isImport);
    }
}

static void prepline(void) {
    short t;
    short len;
    char buf[256];
    CWFileInfo fileinfo;
    SInt32 moddate;
    SInt32 linedelta;

    cprep_hasprepline = TRUE;
    if (!notendofline()) {
        CPrep_Error(ERR_UNEXPECTED_END_LINE);
        return;
    }

    if (plex() != TK_INTCONST) {
        CPrep_Error(ERR_PREPROCESSOR_SYNTAX_ERROR);
        skipendofline();
        return;
    }

    linenumber = CInt64_GetULong(&tkintconst) - 1;
    if (!notendofline()) {
        strncpy(buf, CPrep_GetSourceFilePath(filestack[filesp])->name, 255);
        buf[255] = 0;
    } else {
        if (prepnextchar() != '"') {
            CPrep_Error(ERR_PREPROCESSOR_SYNTAX_ERROR);
            skipendofline();
            return;
        }

        len = 0;
        while ((t = prepnextchar()) != '"') {
            if (!t) {
                break;
            }
            if (len > 252) {
                CPrep_Error(ERR_STRING_TOO_LONG);
                skipendofline();
                return;
            }
            buf[len] = t;
            len++;
        }
        buf[len] = 0;
        goendofline();
    }

    memset(&fileinfo, 0, sizeof(fileinfo));
    fileinfo.fullsearch = TRUE;
    fileinfo.suppressload = FALSE;
    fileinfo.dependencyType = cwNormalDependency;
    fileinfo.isdependentoffile = -1;
    if (!CWFindAndLoadFile(cparams.context, buf, &fileinfo)) {
        COS_FileGetPathName(buf, &fileinfo.filespec, &moddate);
        linedelta = filestack[filesp]->linenumber - linenumber;
    } else {
        if (copts.warn_possunwant) {
            CPrep_WarningName(ERR_FILE_CANNOT_OPENED, buf);
        }
        linedelta = 0;
    }

    filestack[filesp]->nameNode = GetHashNameNode(buf);
    filestack[filesp]->linedelta = linedelta;
    filestack[filesp]->linenumber = linenumber;
    if (copts.filesyminfo && !cparamblkptr->precompile) {
        ObjGen_SrcBreakName(filestack[filesp]->nameNode, moddate, TRUE);
    }
    if (preprocessing_only && !copts.simple_prepdump) {
        CPrep_PreprocessDumpFileInfo(FALSE);
    }
}

static short exprtk;

static Type* CPrep_GetPrepType(PrepValue* pv) {
    if (copts.longlong_prepeval || copts.c9x) {
        if (pv->is_unsigned) {
            return TYPE(&stunsignedlonglong);
        } else {
            return TYPE(&stsignedlonglong);
        }
    } else {
        if (pv->is_unsigned) {
            return TYPE(&stunsignedlong);
        } else {
            return TYPE(&stsignedlong);
        }
    }
}

static PrepValue CPrep_ParseUnary(void) {
    PrepValue val;

    if (!notendofline()) {
        CPrep_Error(ERR_UNEXPECTED_END_LINE);
        val.val = cint64_zero;
        val.is_unsigned = FALSE;
    } else {
        switch (plex()) {
            case '-':
                val = CPrep_ParseUnary();
                val.val = CMach_CalcIntMonadic(CPrep_GetPrepType(&val), '-', val.val);
                return val;

            case '+':
                val = CPrep_ParseUnary();
                return val;

            case '!':
                val = CPrep_ParseUnary();
                CInt64_SetLong(&val.val, CInt64_IsZero(&val.val));
                return val;

            case '~':
                val = CPrep_ParseUnary();
                val.val = CMach_CalcIntMonadic(CPrep_GetPrepType(&val), '~', val.val);
                return val;

            case '(':
                val = CPrep_ParseCond();
                if (exprtk != ')') {
                    CPrep_Error(ERR_RPAREN_EXPECTED);
                    return val;
                }
                break;

            case TK_TRUE:
                val.val = cint64_one;
                val.is_unsigned = TRUE;
                break;

            default:
                CPrep_Error(ERR_EXPRESSION_SYNTAX_ERROR);
            case TK_FALSE:
                val.val = cint64_zero;
                val.is_unsigned = TRUE;
                break;

            case TK_INTCONST:
                val.val = tkintconst;
                val.is_unsigned =
                    (tksize == IT_ULONGLONG ||
                     tksize == IT_ULONG ||
                     tksize == IT_UINT ||
                     tksize == IT_UCHAR);
                break;

            case TK_IDENTIFIER:
                val.val = cint64_zero;
                val.is_unsigned = FALSE;
                if (!strcmp("defined", tkidentifier->name)) {
                    macrocheck = FALSE;
                    if (!notendofline()) {
                        CPrep_Error(ERR_UNEXPECTED_END_LINE);
                        break;
                    }

                    if ((exprtk = plex()) == '(') {
                        if (!notendofline()) {
                            CPrep_Error(ERR_UNEXPECTED_END_LINE);
                            macrocheck = TRUE;
                            break;
                        }

                        if (lexidentifier() != TK_IDENTIFIER) {
                            CPrep_Error(ERR_IDENTIFIER_EXPECTED);
                            macrocheck = TRUE;
                            break;
                        }

                        if (ismacroname3()) {
                            val.val = cint64_one;
                        }

                        if (!notendofline()) {
                            CPrep_Error(ERR_UNEXPECTED_END_LINE);
                            macrocheck = TRUE;
                            break;
                        }

                        if (plex() != ')') {
                            CPrep_Error(ERR_RPAREN_EXPECTED);
                        }
                    } else if (exprtk == TK_IDENTIFIER) {
                        if (ismacroname3()) {
                            val.val = cint64_one;
                        }
                    } else {
                        CPrep_Error(ERR_IDENTIFIER_EXPECTED);
                    }

                    macrocheck = TRUE;
                }
                break;
        }
    }

    if (notendofline()) {
        exprtk = plex();
    } else {
        exprtk = 0;
    }
    return val;
}

static PrepValue CPrep_ParseBinary(PrepValue* rhs, short precLimit) {
    PrepValue a;
    PrepValue b;
    short op;
    short prec;
    short newprec;

    if (!rhs) {
        a = CPrep_ParseUnary();
    } else {
        a = *rhs;
    }

    while (1) {
        if (!(prec = GetPrec(op = exprtk))) {
            return a;
        }

        if (!notendofline()) {
            CPrep_Error(ERR_UNEXPECTED_END_LINE);
            return a;
        }

        b = CPrep_ParseUnary();
        while (1) {
            if (exprtk == ')' || !notendofline()) {
                if (b.is_unsigned) {
                    a.is_unsigned = TRUE;
                }
                a.val = CMach_CalcIntDiadic(CPrep_GetPrepType(&a), a.val, op, b.val);
                return a;
            }

            if (!(newprec = GetPrec(exprtk))) {
                CPrep_Error(ERR_EXPRESSION_SYNTAX_ERROR);
                return a;
            }

            if (prec >= newprec) {
                if (b.is_unsigned) {
                    a.is_unsigned = TRUE;
                }
                a.val = CMach_CalcIntDiadic(CPrep_GetPrepType(&a), a.val, op, b.val);
                // (GetPrec of a precedence rather than a token is always 0: the original's)
                if (GetPrec(newprec) <= precLimit) {
                    return a;
                }
                break;
            } else {
                b = CPrep_ParseBinary(&b, prec);
            }
        }
    }
}

static PrepValue CPrep_ParseCond(void) {
    PrepValue a;
    PrepValue b;
    PrepValue c;

    a = CPrep_ParseBinary(NULL, -1);
    if (exprtk == '?') {
        b = CPrep_ParseCond();
        if (!notendofline()) {
            CPrep_Error(ERR_UNEXPECTED_END_LINE);
            return a;
        }
        if (exprtk != ':') {
            CPrep_Error(ERR_COLON_EXPECTED);
            return a;
        }
        if (!notendofline()) {
            CPrep_Error(ERR_UNEXPECTED_END_LINE);
            return a;
        }
        c = CPrep_ParseCond();
        a = !CInt64_IsZero(&a.val) ? b : c;
    }

    return a;
}

static void doevalconstexpr(void) {
    PrepValue v;

    macrocheck = TRUE;
    nlflag = FALSE;
    cprep_incondexpr = TRUE;
    v = CPrep_ParseCond();
    if (exprtk) {
        CPrep_Error(ERR_EXPRESSION_SYNTAX_ERROR);
    }
    cprep_incondexpr = FALSE;
    if (nlflag) {
        CPrep_Error(ERR_UNEXPECTED_END_LINE);
    } else {
        goendofline();
    }
    tkintconst = v.val;
}

static void pushifstate(SInt16 state) {
    if (iflevel >= 64) {
        CPrep_Error(ERR_IF_NESTING_OVERFLOW);
    } else {
        ifstack[iflevel].state = state;
        ifstack[iflevel].file = prep_file;
        ifstack[iflevel].pos = pos - prep_file_start;
        iflevel++;
    }
}

static void popifstate(void) {
    if (iflevel <= 0) {
        CPrep_Error(ERR_PREPROCESSOR_SYNTAX_ERROR);
    } else {
        switch (ifstack[iflevel - 1].state) {
            case IfState_Searching:
            case IfState_Done:
            case IfState_ElseDone:
                iflevel--;
                return;
        }

        iflevel--;
        if (iflevel > 0) {
            switch (ifstack[iflevel - 1].state) {
                case IfState_Searching:
                case IfState_Done:
                case IfState_ElseDone:
                    prepifskip();
            }
        }
    }
}

static void positiveif(void) {
    pushifstate(IfState_Active);
}

static void negativeif(Boolean isFalse) { // NOTE: invented name: isFalse
    if (isFalse) {
        pushifstate(IfState_Searching);
        prepifskip();
    } else {
        pushifstate(IfState_Done);
    }
}

static void prepif(void) {
    if (iflevel > 0) {
        switch (ifstack[iflevel - 1].state) {
            case IfState_Searching:
            case IfState_Done:
            case IfState_ElseDone:
                skipendofline();
                negativeif(FALSE);
                return;
        }
    }

    doevalconstexpr();
    if (CInt64_IsZero(&tkintconst)) {
        negativeif(TRUE);
    } else {
        positiveif();
    }
}

static void prepifdef(void) {
    if (iflevel > 0) {
        switch (ifstack[iflevel - 1].state) {
            case IfState_Searching:
            case IfState_Done:
            case IfState_ElseDone:
                skipendofline();
                negativeif(FALSE);
                return;
        }
    }

    macrocheck = FALSE;
    if (!notendofline()) {
        CPrep_Error(ERR_UNEXPECTED_END_LINE);
        negativeif(TRUE);
        return;
    }

    if (lexidentifier() != TK_IDENTIFIER) {
        CPrep_Error(ERR_IDENTIFIER_EXPECTED);
        negativeif(TRUE);
        return;
    }

    macrocheck = TRUE;
    goendofline();
    if (ismacroname3()) {
        positiveif();
    } else {
        negativeif(TRUE);
    }
}

static void prepifndef(void) {
    if (iflevel > 0) {
        switch (ifstack[iflevel - 1].state) {
            case IfState_Searching:
            case IfState_Done:
            case IfState_ElseDone:
                skipendofline();
                negativeif(FALSE);
                return;
        }
    }

    macrocheck = FALSE;
    if (!notendofline()) {
        CPrep_Error(ERR_UNEXPECTED_END_LINE);
        negativeif(TRUE);
        return;
    }

    if (lexidentifier() != TK_IDENTIFIER) {
        CPrep_Error(ERR_IDENTIFIER_EXPECTED);
        negativeif(TRUE);
        return;
    }

    macrocheck = TRUE;
    goendofline();
    if (!ismacroname3()) {
        positiveif();
    } else {
        negativeif(TRUE);
    }
}

static void prepelif(void) {
    if (iflevel <= 0 ||
        ifstack[iflevel - 1].state == IfState_ElseActive ||
        ifstack[iflevel - 1].state == IfState_ElseDone) {
        CPrep_Error(ERR_PRECEDING_IF_MISSING);
        skipendofline();
        return;
    }

    switch (ifstack[iflevel - 1].state) {
        case IfState_Searching:
            doevalconstexpr();
            if (!CInt64_IsZero(&tkintconst)) {
                ifstack[iflevel - 1].state = IfState_Active;
            }
            return;
        case IfState_Done:
            skipendofline();
            return;
        case IfState_Active:
            ifstack[iflevel - 1].state = IfState_Done;
            skipendofline();
            prepifskip();
            return;
        default:
            CError_FATAL(5787);
    }
}

static void prepelse(void) {
    if (iflevel <= 0 ||
        ifstack[iflevel - 1].state == IfState_ElseActive ||
        ifstack[iflevel - 1].state == IfState_ElseDone) {
        CPrep_Error(ERR_PRECEDING_IF_MISSING);
        skipendofline();
        return;
    }

    if (!copts.ANSIstrict) {
        skipendofline();
    } else {
        goendofline();
    }

    switch (ifstack[iflevel - 1].state) {
        case IfState_Searching:
            ifstack[iflevel - 1].state = IfState_ElseActive;
            return;
        case IfState_Active:
            ifstack[iflevel - 1].state = IfState_ElseDone;
            prepifskip();
            return;
        case IfState_Done:
            ifstack[iflevel - 1].state = IfState_ElseDone;
            return;
        default:
            CError_FATAL(5825);
    }
}

static void prependif(void) {
    if (iflevel <= 0) {
        CPrep_Error(ERR_PRECEDING_IF_MISSING);
        skipendofline();
        return;
    }

    macrocheck = FALSE;
    if (!copts.ANSIstrict) {
        skipendofline();
    } else {
        goendofline();
    }

    macrocheck = TRUE;
    popifstate();
}

static void prepifskip(void) {
    TStreamElement ts;
    short t;

    while (iflevel > 0) {
        switch (ifstack[iflevel - 1].state) {
            case IfState_Searching:
            case IfState_Done:
            case IfState_ElseDone:
restart:
                t = prepskipnextchar();
                pos = nextcharpos;
                switch (t) {
                    case 0:
                        if (tokenstacklevel > 0) {
                            poptokenseq();
                        } else if (tokenstacklevel > 0 || pos >= prep_file_end) {
                            if (filesp > 0) {
                                popfile();
                            } else {
                                was_prep_error = FALSE;
                                ts.tokenfile = ifstack[iflevel - 1].file;
                                ts.tokenoffset = ifstack[iflevel - 1].pos;
                                CError_SetErrorToken(&ts);
                                CError_ErrorTerm(ERR_UNTERMINATED_IF_MACRO);
                                iflevel = 0;
                                return;
                            }
                        } else {
                            CPrep_Error(ERR_ILLEGAL_TOKEN);
                        }
                        goto restart;
                    case 13:
                        newline();
                        goto restart;
                    case '"':
                        skipendoflinematch((StringPtr)pos, '"');
                        goto restart;
                    case '\'':
                        skipendoflinematch((StringPtr)pos, '"');
                        goto restart;
                    case '#':
                        t = prepskipnextchar();
                        pos = nextcharpos;
                        switch (t) {
                            case 13:
                                goto restart;
                            case 0:
                                CPrep_Error(ERR_UNEXPECTED_END_FILE);
                            default:
                                pos = ReadIdentifier(pos - 1);
                                if (!strcmp("if", tkidentifier->name)) {
                                    prepif();
                                } else if (!strcmp("ifdef", tkidentifier->name)) {
                                    prepifdef();
                                } else if (!strcmp("ifndef", tkidentifier->name)) {
                                    prepifndef();
                                } else if (!strcmp("elif", tkidentifier->name)) {
                                    prepelif();
                                } else if (!strcmp("else", tkidentifier->name)) {
                                    prepelse();
                                } else if (!strcmp("endif", tkidentifier->name)) {
                                    prependif();
                                } else {
                                    skipendofline();
                                    goto restart;
                                }
                        }
                        break;
                    default:
                        skipendofline();
                        goto restart;
                }
                break;
            case IfState_ElseActive:
            default:
                return;
        }
    }
}

void preprocessor(void) {
    short t;

    gDirectiveStart = pos - prep_file_start;
    t = prepskipnextchar();
    pos = nextcharpos;

    switch (t) {
        case 0:
            CPrep_Error(ERR_UNEXPECTED_END_FILE);
        case 13:
            return;
    }

    pos = ReadIdentifier(pos - 1);
    if (!strcmp("define", tkidentifier->name)) {
        CPrep_Define();
        macrocheck = TRUE;
        return;
    }
    if (!strcmp("undef", tkidentifier->name)) {
        prepundefine();
        macrocheck = TRUE;
        return;
    }
    if (!strcmp("include", tkidentifier->name)) {
        prepinclude(FALSE);
        macrocheck = TRUE;
        return;
    }
    if (!strcmp("line", tkidentifier->name)) {
        prepline();
        macrocheck = TRUE;
        return;
    }
    if (!strcmp("error", tkidentifier->name)) {
        CPrep_Error(ERR_PREPROCESSOR_ERROR_DIRECTIVE);
        CError_ResetErrorSkip();
        skipendofline();
        macrocheck = TRUE;
        return;
    }
    if (!strcmp("pragma", tkidentifier->name)) {
        preppragma();
        macrocheck = TRUE;
        return;
    }
    if (!copts.ANSIstrict) {
        if (!strcmp("warning", tkidentifier->name)) {
            CPrep_Warning(ERR_PREPROCESSOR_WARNING_DIRECTIVE);
            skipendofline();
            macrocheck = TRUE;
            return;
        }
        if (!strcmp(tkidentifier->name, "ident")) {
            skipendofline();
            macrocheck = TRUE;
            return;
        }
    }
    if (!strcmp("if", tkidentifier->name)) {
        macrocheck = TRUE;
        prepif();
        return;
    }
    if (!strcmp("ifdef", tkidentifier->name)) {
        macrocheck = TRUE;
        prepifdef();
        return;
    }
    if (!strcmp("ifndef", tkidentifier->name)) {
        macrocheck = TRUE;
        prepifndef();
        return;
    }
    if (!strcmp("elif", tkidentifier->name)) {
        macrocheck = TRUE;
        prepelif();
        return;
    }
    if (!strcmp("else", tkidentifier->name)) {
        macrocheck = TRUE;
        prepelse();
        return;
    }
    if (!strcmp("endif", tkidentifier->name)) {
        macrocheck = TRUE;
        prependif();
        return;
    }
    if (copts.objective_c || !copts.ANSIstrict) {
        if (!strcmp("import", tkidentifier->name)) {
            prepinclude(TRUE);
            macrocheck = TRUE;
            return;
        }
    }
    CPrep_Error(ERR_UNDEFINED_PREPROCESSOR_DIRECTIVE);
    skipendofline();
    macrocheck = TRUE;
}

SInt32 CPrep_BrowserTokenOffset(FileOffsetInfo* foi) {
    return foi->tokenoffset;
}

SInt32 CPrep_BrowserFileOffset(void) {
    if (ts_first < ts_current) {
        return ts_current[-1].tokenoffset + 1;
    } else if (tokenstacklevel) {
        return tokenstack[0].pos - filestack[filesp]->textbuffer;
    } else {
        return pos - filestack[filesp]->textbuffer;
    }
}

void CPrep_BrowserFilePosition(CPrepFileInfo** fileinfo, SInt32* offset) {
    CPrepFileInfo* file;

    if (ts_first < ts_current) {
        file = ts_current[-1].tokenfile;
        *offset = ts_current[-1].tokenoffset + 1;
    } else {
        file = filestack[filesp];
        if (tokenstacklevel) {
            *offset = tokenstack[0].pos - filestack[filesp]->textbuffer;
        } else {
            *offset = pos - filestack[filesp]->textbuffer;
        }
    }

    if (file && file->fileID > 0 && (file->recordbrowseinfo || gForceSourceLoc)) {
        *fileinfo = file;
    } else {
        *fileinfo = NULL;
        *offset = 0;
    }
}

CPrepFileInfo* CPrep_BrowserCurrentFile(void) {
    return prep_file;
}
