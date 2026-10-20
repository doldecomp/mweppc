#ifndef DRIVER_OPTION_SYSTEM_H
#define DRIVER_OPTION_SYSTEM_H

#include "compiler/common.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

// The option system's types: the parameter kinds, the options and their lists, packed to 1 byte
// (2.6's tables have the 10-byte PARAM_T header and unaligned fields).
#pragma pack(push, 1)
enum {
    OptParseError = 0,
    OptParseSuccess = 1
};

typedef struct PARAM_T {
    char which;
    char flags;
    char* myname;
    struct PARAM_T* next;
} PARAM_T;
typedef struct MASK_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    char size;
    UInt32 ormask;
    UInt32 andmask;
    void* num;
} MASK_T;
typedef struct STRING_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    SInt16 maxlen;
    Boolean pstring;
    char* str;
} STRING_T;
typedef struct SET_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    char size;
    UInt32 value;
    char* num;
} SET_T;
typedef struct SETSTRING_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    char* value;
    char pstring;
    void* var;
} SETSTRING_T;
typedef struct GENERIC_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    int (*parse)(const char* opt, void* var, const char* pstr, int flags);
    void* var;
    char* help;
} GENERIC_T;
typedef struct SETTING_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    // the setting's name, then its value (NULL without `=`)
    int (*parse)(const char* name, const char* value);
    char* valuename;
} SETTING_T;
typedef struct TOGGLE_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    char size;
    UInt32 mask;
    void* num;
} TOGGLE_T;
typedef struct NUM_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    char size;
    char fit;
    UInt32 lo;
    UInt32 hi;
    void* num;
} NUM_T;
typedef struct FILEPATH_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    char fflags;
    char* defaultstr;
    void* filename;
    int maxlen;
} FILEPATH_T;
typedef struct IFARG_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    PARAM_T* parg;
    char* helpa;
    PARAM_T* pnone;
    char* helpn;
} IFARG_T;
typedef struct ONOFF_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    unsigned char* var;
} ONOFF_T;
typedef struct OFFON_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    unsigned char* var;
} OFFON_T;
typedef struct FTYPE_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    OSType* fc;
    Boolean iscreator;
} FTYPE_T;
typedef struct OptionList {
    char* help;
    int flags;
    struct Option** list;
} OptionList;
typedef struct Option {
    char* names;
    int avail;
    PARAM_T* param;
    OptionList* sub;
    OptionList* conflicts;
    char* help;
} Option;

// NOTE: invented names: HELPFLAGS_ONE_OPTION, HELPFLAGS_SEARCH_HELP
// parseopts.helpFlags: what -help shows (ONE_OPTION: help for the one option named by helpKey;
// SEARCH_HELP: looks for helpKey in the lists' help texts)
enum {
    HELPFLAGS_ONE_OPTION = 1,
    HELPFLAGS_IGNORED = 2,
    HELPFLAGS_OBSOLETE = 4,
    HELPFLAGS_DEPRECATED = 8,
    HELPFLAGS_SECRET = 0x10,
    HELPFLAGS_MEANINGLESS = 0x20,
    HELPFLAGS_COMPATIBLE = 0x40,
    HELPFLAGS_NORMAL = 0x80,
    HELPFLAGS_SPACES = 0x100,
    HELPFLAGS_TOOL = 0x200,
    HELPFLAGS_TOOL_THIS = 0x400,
    HELPFLAGS_TOOL_OTHER = 0x800,
    HELPFLAGS_TOOL_BOTH = 0xC00,
    HELPFLAGS_USAGE = 0x4000,
    HELPFLAGS_SEARCH_HELP = 0x8000
};
// NOTE: invented names: OTF_NO_PREFIX, OTF_MINUS_PREFIX, OTF_NO_DASH_PREFIX
// Option.avail
enum {
    OTF_GLOBAL = 1,
    OTF_STICKY = 2,
    OTF_CASED = 4,
    OTF_OBSOLETE = 8,
    OTF_SUBSTITUTED = 0x10,
    OTF_DEPRECATED = 0x20,
    OTF_TOOL_LINKER = 0x40,
    OTF_TOOL_DISASSEMBLER = 0x80,
    OTF_TOOL_COMPILER = 0x100,
    OTF_TOOL_MASK = OTF_TOOL_LINKER | OTF_TOOL_DISASSEMBLER | OTF_TOOL_COMPILER,
    OTF_IGNORED = 0x800,
    OTF_SECRET = 0x1000,
    OTF_HIDE_DEFAULT = 0x2000,
    OTF_COMPATIBILITY = 0x4000,
    OTF_HAS_SUB_OPTIONS = 0x8000,
    OTF_SUB_OPTIONS_OPTIONAL = 0x10000,
    OTF_ONLY_ONCE = 0x20000,
    OTF_HAS_CONFLICTS = 0x40000,
    OTF_WARNING = 0x80000,
    // the negations the option takes: `no`, `-`, `no-`
    OTF_NO_PREFIX = 0x100000,
    OTF_MINUS_PREFIX = 0x200000,
    OTF_NO_DASH_PREFIX = 0x400000,
    OTF_PREFIX_MASK = OTF_NO_PREFIX | OTF_MINUS_PREFIX | OTF_NO_DASH_PREFIX,
    OTF_MEANINGLESS = 0x800000,
    OTF_ALL_HIDDEN_BY_DEFAULT = OTF_OBSOLETE | OTF_DEPRECATED | OTF_IGNORED | OTF_SECRET |
        OTF_MEANINGLESS,
    OTF_INTERNAL_SEEN_CONFLICTED = 0x40000000,
    OTF_INTERNAL_ALREADY_SEEN = 0x80000000
};
enum {
    PARAMWHICH_None = 0,
    PARAMWHICH_FTypeCreator = 1,
    PARAMWHICH_FilePath = 2,
    PARAMWHICH_Number = 3,
    PARAMWHICH_String = 4,
    PARAMWHICH_Id = 5,
    PARAMWHICH_Sym = 6,
    PARAMWHICH_OnOff = 7,
    PARAMWHICH_OffOn = 8,
    PARAMWHICH_Mask = 9,
    PARAMWHICH_Toggle = 0xA,
    PARAMWHICH_Set = 0xB,
    PARAMWHICH_SetString = 0xC,
    PARAMWHICH_Generic = 0xD,
    PARAMWHICH_IfArg = 0xE,
    PARAMWHICH_Setting = 0xF,
    PARAMWHICH_MAX = 0x10
};
// NOTE: invented names: PARAMFLAGS_NO_ARG, PARAMFLAGS_OPTIONAL_ARG, PARAMFLAGS_ARG_MASK,
// PARAMFLAGS_SUBARGS, PARAMFLAGS_EQUALS_AFTER, PARAMFLAGS_NOT_FILE, PARAMFLAGS_OPTIONAL_NOT_FILE
// PARAM_T.flags: whether the parameter takes an argument (none, optional; 0 required),
// sub-arguments after it, an `=` after its argument, and an optional argument that is not taken
// when it names a non-text file. No table sets NOT_FILE; Param_GetArgument tests OPTIONAL_NOT_FILE
// where the optional bit is already set, so it checks every optional argument for a file (the
// original's)
enum {
    PARAMFLAGS_NO_ARG = 1,
    PARAMFLAGS_OPTIONAL_ARG = 2,
    PARAMFLAGS_ARG_MASK = 3,
    PARAMFLAGS_SUBARGS = 4,
    PARAMFLAGS_EQUALS_AFTER = 8,
    PARAMFLAGS_NOT_FILE = 0x10,
    PARAMFLAGS_OPTIONAL_NOT_FILE = 0x12
};
// NOTE: invented names: PARAMPARSEFLAGS_NO_EXECUTE, PARAMPARSEFLAGS_TO_LINKER,
// PARAMPARSEFLAGS_SUBOPTION, PARAMPARSEFLAGS_NEGATED, PARAMPARSEFLAGS_FIRST,
// PARAMPARSEFLAGS_HAS_SUBARGS, PARAMPARSEFLAGS_EQUALS_OK
// Params_Parse's flags, the PFLAGS Option_Parse passes: parse without acting (an obsolete or
// ignored option, or another tool's), also add the argument to the linker's arguments, a
// sub-option, the negated form, the first parameter, sub-arguments follow, `=` allowed
enum {
    PARAMPARSEFLAGS_NO_EXECUTE = 1,
    PARAMPARSEFLAGS_TO_LINKER = 2,
    PARAMPARSEFLAGS_SUBOPTION = 4,
    PARAMPARSEFLAGS_NEGATED = 8,
    PARAMPARSEFLAGS_FIRST = 0x10,
    PARAMPARSEFLAGS_HAS_SUBARGS = 0x20,
    PARAMPARSEFLAGS_EQUALS_OK = 0x40
};
// NOTE: invented names: SLFLAGS_OPTCHAR, SLFLAGS_SUBOPTION, SLFLAGS_NO_PREFIX,
// SLFLAGS_MINUS_PREFIX, SLFLAGS_NO_DASH_PREFIX, SLFLAGS_STICKY
// Utils_SpellList's options: the main option character first, a sub-option, the negations, and no
// shared prefixes for a sticky option (nothing tests SUBOPTION)
enum {
    SLFLAGS_OPTCHAR = 1,
    SLFLAGS_SUBOPTION = 2,
    SLFLAGS_NO_PREFIX = 8, // displays [no]: e.g. [no]err[or] | [no]iserr[or], [no]implicit[conv]
    SLFLAGS_MINUS_PREFIX = 0x10, // displays [-]
    SLFLAGS_NO_DASH_PREFIX = 0x20, // displays [no-]
    SLFLAGS_STICKY = 0x40 // displays a + after the first name
};
// OptionList.flags (UNNAMED: a list whose unnamed option matches any argument)
enum {
    LISTFLAGS_NONE = 0,
    LISTFLAGS_EXCLUSIVE = 1,
    LISTFLAGS_ALLOW_UNKNOWNS = 2,
    LISTFLAGS_UNNAMED = 4, // NOTE: invented name
    LISTFLAGS_COMPILER = 0x100,
    LISTFLAGS_LINKER = 0x200,
    LISTFLAGS_DISASSEMBLER = 0x400,
    LISTFLAGS_TOOL_MASK = LISTFLAGS_COMPILER | LISTFLAGS_LINKER | LISTFLAGS_DISASSEMBLER
};
// NOTE: invented names: OFLAGS_GLOBAL, OFLAGS_SUBOPTION, OFLAGS_OPTIONAL, OFLAGS_NEGATED,
// OFLAGS_NESTED, OFLAGS_NO_EXECUTE
// Options_Parse's flags: parsing global options, sub-options (OPTIONAL: may be absent; NESTED: of a
// sub-option), the negated form, parse without acting
enum {
    OFLAGS_GLOBAL = 1,
    OFLAGS_SUBOPTION = 2,
    OFLAGS_OPTIONAL = 4,
    OFLAGS_NEGATED = 8,
    OFLAGS_NESTED = 0x10,
    OFLAGS_NO_EXECUTE = 0x40
};
// NOTE: invented names: PFLAGS_NO_EXECUTE, PFLAGS_TO_LINKER, PFLAGS_SUBOPTION, PFLAGS_NEGATED,
// PFLAGS_HAS_SUBARGS
// the flags Option_Parse passes to Params_Parse: the same bits as PARAMPARSEFLAGS
enum {
    PFLAGS_NO_EXECUTE = 1,
    PFLAGS_TO_LINKER = 2,
    PFLAGS_SUBOPTION = 4,
    PFLAGS_NEGATED = 8,
    PFLAGS_HAS_SUBARGS = 0x20
};
#pragma pack(pop)

/* Arguments.c */
// A token of the command line (Arg_GetToken): its kind (ATK_*) and text
typedef struct ArgToken {
    SInt16 val;
    char* text;
} ArgToken;
enum {
    ATK_END,
    ATK_ARG_END,
    ATK_ARG,
    ATK_OPTION,
    ATK_EQUALS,
    ATK_COMMA
};

#ifdef __cplusplus
}
#endif

#endif
