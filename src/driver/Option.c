#include "driver/Option.h"
#include "compiler/win32.h"
#include "driver/Arguments.h"
#include "driver/CWPlugins.h"
#include "driver/Help.h"
#include "driver/IO.h"
#include "driver/MemUtils.h"
#include "driver/option_system.h"
#include "driver/oslib.h"
#include "driver/Parameter.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"
#include "driver/Targets.h"
#include "driver/Utils.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXSTACK 8

// NOTE: invented names: OptionStackPair, OptionStackOption, OptionStackEntry
// An entry of the option stack (Args_Push): an option list, an option and its spelling, or a
// parameter.
typedef struct OptionStackPair {
    void* first;
    void* second;
} OptionStackPair;
typedef struct OptionStackOption {
    Option* opt;
    char* curopt;
} OptionStackOption;
struct OptionStackEntry {
    union {
        OptionStackPair v;
        OptionList* lst;
        OptionStackOption o;
        char* param;
    } e;
    SInt16 flags;
};

// Args_SpellStack's flag: spell for the tool's arguments (a newline after the option, and the
// entries marked spelled).
enum {
    SPELLSTACK_TOOLARGS = 0x20 // NOTE: invented name
};

void Option_Error(VarArgShort id, ...);

static OptionList* optionlists[32];
int numoptionlists;
static OptionList internalset;
static OptionList legalset;
int numinternalset;
int numlegalset;
int maxlegalset;
char curopt[1024];
OptionStackEntry oStack[MAXSTACK];
int oStackPtr;

static void Option_PushList(OptionList* lst) {
    Args_Push(OSTACK_LIST, lst, 0);
}

static void Option_PushOpt(Option* opt, const char* optname) {
    char* cpy;
    short flags = OSTACK_OPTION;
    if (opt && (opt->avail & OTF_STICKY)) {
        if (Utils_CompareOptionString(opt->names, optname, opt->avail & OTF_CASED, OTF_STICKY)) {
            flags |= OSTACK_STICKY;
        }
    }
    cpy = xstrdup(optname);
    Args_Push(flags, opt, cpy);
}

static void Option_PopOpt(char* optname) {
    OptionStackEntry* os = Args_Pop(OSTACK_OPTION);
    if (optname) {
        strcpy(optname, os->e.o.curopt);
    }
    free(os->e.o.curopt);
}

static void Option_PopList(void) {
    Args_Pop(OSTACK_LIST);
}

void Args_InitStack(void) {
    oStackPtr = 0;
}
int Args_StackSize(void) {
    return oStackPtr;
}

void Args_Push(short flags, void* first, void* second) {
    OPTION_ASSERT(104, oStackPtr<MAXSTACK);
    if (oStackPtr > 0) {
        short prev = (flags & OSTACK_LIST) ? OSTACK_OPTION
            : (flags & OSTACK_OPTION)      ? OSTACK_LIST
            : (flags & OSTACK_PARAM)       ? OSTACK_OPTION
                                           : -1;
        OPTION_ASSERT(111, (oStack[oStackPtr-1].flags & prev));
    }

    oStack[oStackPtr].e.v.first = first;
    oStack[oStackPtr].e.v.second = second;
    oStack[oStackPtr].flags = flags;
    oStackPtr++;
}

OptionStackEntry* Args_Pop(short flags) {
    OPTION_ASSERT(122, oStackPtr>0);
    --oStackPtr;
    OPTION_ASSERT(124, (oStack[oStackPtr].flags & flags));
    return &oStack[oStackPtr];
}

void Args_SpellStack(char* buffer, short flags) {
    char* bptr;
    OptionStackEntry* os;
    int sp;
    int level;

    bptr = buffer;
    sp = 0;
    level = 0;
    os = &oStack[sp];

    while (sp < oStackPtr) {
        if (!flags || !(os->flags & OSTACK_SPELLED)) {
            if (os->flags & OSTACK_PARAM) {
                OptionStackEntry* po = os - 1;
                if (!(os[-1].flags & OSTACK_FOLLOWED)) {
                    if ((level == 1 || level == 2) && !(po->flags & OSTACK_STICKY)) {
                        *(bptr++) = (flags & SPELLSTACK_TOOLARGS) ? '\n' : ' ';
                    }
                    os[-1].flags |= OSTACK_FOLLOWED;
                } else if (level == 2) {
                    *(bptr++) = ',';
                } else if (level == 3) {
                    *(bptr++) = '=';
                }
                strcpy(bptr, os->e.param);
                bptr += strlen(bptr);
            } else if (os->flags & OSTACK_OPTION) {
                if (level == 1) {
                    *(bptr++) = MAINOPTCHAR[0];
                } else if (level == 2) {
                    OptionStackEntry* po = os - 2;
                    if (!(os[-1].flags & OSTACK_FOLLOWED) && !(po->flags & OSTACK_STICKY)) {
                        *(bptr++) = (flags & SPELLSTACK_TOOLARGS) ? '\n' : ' ';
                    }
                }

                if (os[-1].flags & OSTACK_FOLLOWED) {
                    if (level == 2) {
                        if (flags & SPELLSTACK_TOOLARGS) {
                            *(bptr++) = ',';
                        } else {
                            *(bptr++) = ' ';
                        }
                    }
                } else {
                    if (level == 3) {
                        *(bptr++) = '=';
                    }
                }

                os[-1].flags |= OSTACK_FOLLOWED;
                strcpy(bptr, os->e.o.curopt);
                bptr += strlen(bptr);
            }

            if (flags & SPELLSTACK_TOOLARGS) {
                os->flags |= OSTACK_SPELLED;
            }
        }

        if (os->flags & OSTACK_LIST) {
            level++;
        }

        ++sp;
        ++os;
    }
}

void Args_AddToToolArgs(PtrList* ta) {
    char buffer[4096];
    char* nptr;

    Args_SpellStack(buffer, SPELLSTACK_TOOLARGS);
    nptr = strchr(buffer, '\n');
    if (nptr) {
        *(nptr++) = 0;
        Arg_AddToToolArgs(ta, ATK_ARG, buffer);
        Arg_AddToToolArgs(ta, ATK_ARG_END, NULL);
    } else {
        nptr = buffer;
    }
    Arg_AddToToolArgs(ta, ATK_ARG, nptr);
}

void Options_Init(void) {
    numoptionlists = 0;
    maxlegalset = 0;
    numlegalset = 0;
    numinternalset = 0;

    if (legalset.list) {
        free(legalset.list);
    }
    if (internalset.list) {
        free(internalset.list);
    }

    legalset.list = 0;
    legalset.flags = ((Option_ThisTool() & OTF_TOOL_COMPILER) ? LISTFLAGS_COMPILER : 0) |
        ((Option_ThisTool() & OTF_TOOL_LINKER) ? LISTFLAGS_LINKER : 0) |
        ((Option_ThisTool() & OTF_TOOL_DISASSEMBLER) ? LISTFLAGS_DISASSEMBLER : 0);
    internalset.list = 0;
    internalset.flags = ((Option_ThisTool() & OTF_TOOL_COMPILER) ? LISTFLAGS_COMPILER : 0) |
        ((Option_ThisTool() & OTF_TOOL_LINKER) ? LISTFLAGS_LINKER : 0) |
        ((Option_ThisTool() & OTF_TOOL_DISASSEMBLER) ? LISTFLAGS_DISASSEMBLER : 0);

    Args_InitStack();
}

OptionList* Options_GetOptions(void) {
    return &legalset;
}

void Options_SortOptions(void) {
    int r;

    if (numinternalset > 0) {
        legalset.list =
            (Option**)xrealloc("options", legalset.list, sizeof(Option*) * (numinternalset + 1));
        for (r = 0; r < numinternalset; r++) {
            if (internalset.list[r]->avail & (OTF_STICKY | OTF_CASED)) {
                legalset.list[numlegalset++] = internalset.list[r];
            }
        }
        for (r = 0; r < numinternalset; r++) {
            if (!(internalset.list[r]->avail & (OTF_STICKY | OTF_CASED))) {
                legalset.list[numlegalset++] = internalset.list[r];
            }
        }
        legalset.list[numlegalset] = 0;

        if (internalset.list) {
            free(internalset.list);
        }
        internalset.list = 0;
        numinternalset = 0;
    }
}

static void Options_AddOption(Option* opt) {
    if (numinternalset >= maxlegalset) {
        maxlegalset += 32;
        internalset.list =
            (Option**)xrealloc("options", internalset.list, sizeof(Option*) * (maxlegalset + 1));
    }

    internalset.list[numinternalset++] = opt;
    internalset.list[numinternalset] = 0;
}

int Options_AddList(OptionList* optlst) {
    Option** ptr;

    if (numoptionlists >= 32) {
        CLPFatalError("Too many option lists defined!");
    }

    optionlists[numoptionlists++] = optlst;
    for (ptr = optlst->list; *ptr; ptr++) {
        Options_AddOption(*ptr);
    }

    return TRUE;
}

// (no caller: the linker dropped it)
int Options_AddLists(OptionList** optlst) {
    int ret = TRUE;
    OptionList** ptr;

    for (ptr = optlst; *ptr; ptr++) {
        ret = Options_AddList(*ptr);
        if (!ret) {
            return ret;
        }
    }

    return ret;
}

static void Options_Reset(OptionList* optlst) {
    Option** os;

    os = optlst->list;
    if (os) {
        for (; *os; os++) {
            (*os)->avail &= ~(OTF_INTERNAL_ALREADY_SEEN | OTF_INTERNAL_SEEN_CONFLICTED);
        }
    }
}

static void Option_SpellList(char* buffer, OptionList* conflicts, int flags) {
    Option** scan;
    Boolean first;
    int total;
    char spelling[256]; // NOTE: invented name
    Option** next;
    int slflags;

    scan = conflicts->list;
    first = TRUE;
    total = 0;
    while (*scan) {
        next = scan + 1;
        if (!((*scan)->avail &
              (((parseopts.helpFlags & HELPFLAGS_SECRET) ? 0 : OTF_SECRET) |
               ((parseopts.helpFlags & HELPFLAGS_DEPRECATED) ? 0 : OTF_DEPRECATED) |
               OTF_SUBSTITUTED)) &&
            (*scan)->names[0]) {
            if (!first) {
                buffer += sprintf(buffer, ", ");
            } else {
                first = FALSE;
            }

            if (total > 1) {
                while (*next &&
                       (((*next)->avail &
                         (((parseopts.helpFlags & HELPFLAGS_SECRET) ? 0 : OTF_SECRET) |
                          ((parseopts.helpFlags & HELPFLAGS_DEPRECATED) ? 0 : OTF_DEPRECATED) |
                          OTF_SUBSTITUTED)) ||
                        !(*next)->names[0])) {
                    ++next;
                }

                if (!*next) {
                    buffer += sprintf(buffer, "or ");
                }
            }

            slflags = (flags & OFLAGS_SUBOPTION) ? SLFLAGS_SUBOPTION : SLFLAGS_OPTCHAR;
            switch ((*scan)->avail & OTF_PREFIX_MASK) {
                case OTF_NO_PREFIX:
                    slflags |= SLFLAGS_NO_PREFIX;
                    break;
                case OTF_NO_DASH_PREFIX:
                    slflags |= SLFLAGS_NO_DASH_PREFIX;
                    break;
                case OTF_MINUS_PREFIX:
                    slflags |= SLFLAGS_MINUS_PREFIX;
                    break;
            }

            Utils_SpellList((*scan)->names, spelling, slflags);
            total++;
            buffer += sprintf(buffer, "%s", spelling);

            if ((*scan)->avail & OTF_HAS_SUB_OPTIONS) {
                buffer += sprintf(buffer, " ...");
            }
        }
        scan++;
    }
}

int Option_ForTool(Option* opt, int which) {
    return which == 0 || (opt->avail & which);
}

int Option_ThisTool(void) {
    return (pTool->TYPE == CWDROPINCOMPILERTYPE) ? OTF_TOOL_COMPILER : OTF_TOOL_LINKER;
}

int Option_ForThisTool(Option* opt) {
    return !Option_ForTool(opt, OTF_TOOL_MASK) || Option_ForTool(opt, Option_ThisTool());
}

// (no caller: the linker dropped it)
int Option_AlsoPassedToTool(Option* opt, int which) {
    return Option_ForThisTool(opt) && Option_ForTool(opt, which);
}

int Option_AlsoPassedFromThisTool(Option* opt) {
    return Option_ForThisTool(opt) && Option_ForTool(opt, ~Option_ThisTool() & OTF_TOOL_MASK);
}

static Boolean Option_ContinuesThisLevel(int level, ArgToken* tok) {
    ArgToken* next; // NOTE: invented name
    int ret;

    if (level == 1) {
        return (tok->val == ATK_ARG_END) || (tok->val == ATK_OPTION) || (tok->val == ATK_ARG);
    } else if (level == 2) {
        next = Arg_UsedToken();
        ret = (tok->val == ATK_COMMA && next->val != ATK_OPTION) ||
            (tok->val == ATK_ARG && next->val != ATK_ARG_END);
        Arg_UndoToken();
        return ret;
    } else if (level == 3) {
        return (tok->val == ATK_EQUALS) || (tok->val == ATK_ARG);
    } else {
        return FALSE;
    }
}

// clang-format off
static short endingStack[5][3] = {
    {ATK_END, ATK_END, ATK_END},
    {ATK_ARG_END, ATK_COMMA, ATK_OPTION},
    {ATK_COMMA, ATK_END, ATK_END},
    {ATK_EQUALS, ATK_END, ATK_END},
    {ATK_END, ATK_END, ATK_END}
};
// clang-format on

static Boolean Option_IsEndingThisLevel(int level, ArgToken* tok) {
    ArgToken* next; // NOTE: invented name

    if (!tok) {
        return TRUE;
    }
    if (tok->val == endingStack[level][0]) {
        return TRUE;
    }

    if (endingStack[level][1] && tok->val == endingStack[level][1]) {
        next = Arg_UsedToken();
        if (next && next->val == endingStack[level][2]) {
            tok = Arg_UndoToken();
            return TRUE;
        }
        tok = Arg_UndoToken();
    }

    return FALSE;
}

static Boolean Option_IsEndingLevel(int level, ArgToken* tok) {
    if (!tok) {
        return TRUE;
    }

    while (level > 0) {
        if (Option_IsEndingThisLevel(level - 1, tok)) {
            return TRUE;
        }
        level--;
    }

    return FALSE;
}

int Option_Parse(Option* opt, int oflags) {
    int ret;
    int pushed;
    int samelevel;
    int subparse;
    int flags;
    char errstr[1024];
    Option** cscan;
    Option** scan;
    int goingtosubparse;
    ArgToken* tok;

    ret = TRUE;
    pushed = FALSE;
    samelevel = (opt->avail & OTF_GLOBAL) == (oflags & OFLAGS_GLOBAL);
    subparse = (oflags & OFLAGS_SUBOPTION) != 0;
    flags = 0;
    if (subparse) {
        flags |= PFLAGS_SUBOPTION;
    }
    if (oflags & OFLAGS_NEGATED) {
        flags |= PFLAGS_NEGATED;
    }
    if (oflags & OFLAGS_NO_EXECUTE) {
        flags |= PFLAGS_NO_EXECUTE;
    }

    if (curopt[0]) {
        pushed = TRUE;
        Option_PushOpt(opt, curopt);
    }

    if (samelevel) {
        if (!(flags & PFLAGS_NO_EXECUTE)) {
            if ((opt->avail & OTF_INTERNAL_ALREADY_SEEN) && (opt->avail & OTF_ONLY_ONCE)) {
                Option_Warning(CLPStr30_OptionShouldNotBeSpecifiedMultipleTimes);
            } else if (opt->avail & OTF_HAS_CONFLICTS) {
                cscan = opt->conflicts->list;
                while (*cscan &&
                       (*cscan == opt || !((*cscan)->avail & OTF_INTERNAL_ALREADY_SEEN))) {
                    ++cscan;
                }

                if (*cscan && *cscan != opt) {
                    (*cscan)->avail &= ~OTF_INTERNAL_ALREADY_SEEN;
                    Option_SpellList(errstr, opt->conflicts, oflags);
                    if (opt->conflicts->help) {
                        Option_Warning(
                            CLPStr32_OptionOverridesEffectWithHelp,
                            (*cscan)->names,
                            errstr,
                            opt->conflicts->help
                        );
                    } else {
                        Option_Warning(CLPStr31_OptionOverridesEffect, (*cscan)->names, errstr);
                    }
                }
            }
        }

        if (Option_ThisTool() == OTF_TOOL_COMPILER &&
            Option_ForTool(opt, OTF_TOOL_LINKER | OTF_TOOL_DISASSEMBLER)) {
            flags |= PFLAGS_TO_LINKER;
            if (!Option_ForThisTool(opt)) {
                flags |= PFLAGS_NO_EXECUTE;
            }
            if (!subparse) {
                Arg_AddToToolArgs(&linkargs, ATK_ARG_END, NULL);
            }
            Args_AddToToolArgs(&linkargs);
        }

        if (!(flags & PFLAGS_NO_EXECUTE)) {
            if (opt->avail & OTF_OBSOLETE) {
                if (opt->help) {
                    Option_Error(CLPStr22_OptionObsoleteWithHelp, opt->help);
                } else {
                    Option_Error(CLPStr21_OptionObsolete);
                }
                flags |= PFLAGS_NO_EXECUTE;
            }

            if (opt->avail & OTF_IGNORED) {
                if (!(opt->avail & (OTF_WARNING | OTF_MEANINGLESS))) {
                    if (opt->help) {
                        Option_Warning(CLPStr27_OptionIgnoredWithText, opt->help);
                    } else {
                        Option_Warning(CLPStr26_OptionIgnored);
                    }
                }
                flags |= PFLAGS_NO_EXECUTE;
            } else if (opt->avail & OTF_SUBSTITUTED) {
                Option_Warning(CLPStr23_OptionXSubstitutedWithX, curopt, opt->help);
            } else if (opt->avail & OTF_DEPRECATED) {
                if (opt->help) {
                    Option_Warning(CLPStr25_OptionDeprecatedWithHelp, opt->help);
                } else {
                    Option_Warning(CLPStr24_OptionDeprecated);
                }
            }

            if (opt->avail & OTF_WARNING) {
                Option_Warning(CLPStr28_WarningText, opt->help);
            }
            if (opt->avail & OTF_MEANINGLESS) {
                Option_Warning(CLPStr29_OptionHasNoEffect);
            }
        }

        opt->avail |= OTF_INTERNAL_ALREADY_SEEN;
        if (opt->avail & OTF_HAS_CONFLICTS) {
            scan = opt->conflicts->list;
            opt->avail |= OTF_INTERNAL_SEEN_CONFLICTED;
            while (*scan) {
                (*scan)->avail |= OTF_INTERNAL_SEEN_CONFLICTED;
                scan++;
            }
        }
    } else {
        flags |= PFLAGS_NO_EXECUTE;
    }

    // (ret is always TRUE here: the original's test)
    if (ret) {
        goingtosubparse = opt->avail & OTF_HAS_SUB_OPTIONS;
        if (opt->param) {
            ret = Params_Parse(opt->param, flags | (goingtosubparse ? PFLAGS_HAS_SUBARGS : 0));
        } else {
            tok = Arg_PeekToken();
            if (tok && tok->val == ATK_EQUALS) {
                tok = Arg_UsedToken();
            }
        }

        if (ret && goingtosubparse) {
            ret = ret &&
                Options_Parse(
                      opt->sub,
                      (oflags & ~OFLAGS_OPTIONAL) | OFLAGS_SUBOPTION |
                          ((flags & PFLAGS_NO_EXECUTE) ? OFLAGS_NO_EXECUTE : 0) |
                          (subparse ? OFLAGS_NESTED : 0) |
                          ((opt->avail & OTF_SUB_OPTIONS_OPTIONAL) ? OFLAGS_OPTIONAL : 0)
                );
        }
    }

    if (pushed) {
        Option_PopOpt(curopt);
    }

    if (Option_ThisTool() == OTF_TOOL_COMPILER && !subparse) {
        Arg_AddToToolArgs(&linkargs, ATK_ARG_END, NULL);
    }

    return ret;
}

static int Option_MatchString(char* list, char* str, int flags, int* result) {
    int str_len;
    char cpy[64];

    if (result) {
        *result = 0;
    }

    if (!list[0] && !str[0]) {
        return TRUE;
    }

    while (*list) {
        if (Utils_CompareOptionString(list, str, flags & OTF_CASED, 0)) {
            return TRUE;
        }

        switch (flags & OTF_PREFIX_MASK) {
            case OTF_NO_PREFIX:
                if (my_tolower(str[0]) == 'n' &&
                    my_tolower(str[1]) == 'o' &&
                    Utils_CompareOptionString(list, str + 2, flags & OTF_CASED, 0)) {
                    if (result) {
                        *result |= OTF_NO_PREFIX;
                    }
                    return TRUE;
                }
                break;
            case OTF_NO_DASH_PREFIX:
                if (my_tolower(str[0]) == 'n' &&
                    my_tolower(str[1]) == 'o' &&
                    str[2] == '-' &&
                    Utils_CompareOptionString(list, str + 3, flags & OTF_CASED, 0)) {
                    if (result) {
                        *result |= OTF_NO_DASH_PREFIX;
                    }
                    return TRUE;
                }
                break;
            case OTF_MINUS_PREFIX:
                str_len = strlen(str);
                if (str[str_len - 1] == '-') {
                    strcpy(cpy, str);
                    cpy[str_len - 1] = 0;
                    if (Utils_CompareOptionString(list, cpy, flags & OTF_CASED, 0)) {
                        if (result) {
                            *result |= OTF_MINUS_PREFIX;
                        }
                        return TRUE;
                    }
                }
                break;
        }

        while (*list && *list != '|') {
            ++list;
        }

        if (*list) {
            ++list;
        }
        if (*list == '|') {
            ++list;
        }
    }

    return FALSE;
}

// NOTE: invented names: mask, unused
// (mask, the option flags to match on, is not used: the original's).
static Option* Option_Lookup(OptionList* search, int mask, int* flags) {
    Option** os;
    Option* stickyopt;
    int stickyflags;
    Boolean matched;
    char* names;

    os = search->list;
    stickyopt = 0;
    stickyflags = *flags;

    if (os) {
        while (*os) {
            // (a second Boolean cleared each pass and never read: the original's).
            Boolean unused = FALSE;
            matched = FALSE;
            names = (*os)->names;
            if (((*os)->avail & OTF_STICKY) == OTF_STICKY) {
                while (*names && *names != '|') {
                    ++names;
                }
                if (*names) {
                    ++names;
                }
                if (*names == '|') {
                    ++names;
                }
            }

            matched = Option_MatchString(
                          names, curopt, (*os)->avail & (OTF_PREFIX_MASK | OTF_CASED), flags
                      ) &&
                (*names || names == (*os)->names);
            if (matched) {
                if (!stickyopt || (*os)->names[0] == curopt[0] || strlen(curopt) > 1) {
                    return *os;
                }
            }

            if ((*os)->avail & OTF_STICKY) {
                matched =
                    Utils_CompareOptionString((*os)->names, curopt, (*os)->avail & OTF_CASED, 2);
                if (matched) {
                    stickyflags |= OTF_STICKY;
                }
            } else {
                matched = FALSE;
            }

            if (matched) {
                stickyopt = *os;
            }

            ++os;
        }
    }

    *flags = stickyflags;
    return stickyopt;
}

// NOTE: invented names: ending, nested, level
static int Options_DoParse(OptionList* search, int flags) {
    char* optname;
    Option* opt;
    Option* dopt;
    // (this level's row of endingStack, taken and never used: the original's).
    short* ending;
    Boolean isOpt;
    char* lptr;
    ArgToken* prev;
    int haderrors;
    int parsedany;
    int failed;
    int matchflags;
    int subparse;
    // (nested: a sub-option list inside a sub-option list; level, which of the three the list is).
    int nested;
    ArgToken* starttok;
    ArgToken* tok;
    ArgToken* opttok;
    int level;
    char saveopt[1024];
    char savecuropt[64];
    char sticky[64];

    haderrors = 0;
    parsedany = 0;
    failed = 0;
    matchflags = 0;
    subparse = (flags & OFLAGS_SUBOPTION) != 0;
    nested = subparse && (flags & OFLAGS_NESTED);
    opttok = 0;
    level = (subparse == 0) ? 1 : (nested == 0) ? 2 : 3;
    ending = endingStack[level];

    starttok = Arg_PeekToken();
    while ((tok = Arg_PeekToken())) {
        opt = 0;
        isOpt = FALSE;
        parsedany = 0;
        isOpt = ((level == 1) && (tok->val == ATK_OPTION)) ||
            ((level == 2) &&
             ((tok->val == ATK_ARG) && (tok->text[0] || search->flags & LISTFLAGS_UNNAMED))) ||
            ((level == 3) && (tok->val == ATK_EQUALS));
        if ((level == 3) && isOpt) {
            tok = Arg_UsedToken();
            isOpt = tok && (tok->val == ATK_ARG);
            flags &= ~OFLAGS_OPTIONAL;
        }

        if (isOpt) {
            opt = 0;
            Arg_GetTokenText(tok, curopt, sizeof(curopt), FALSE);
            if (curopt[0]) {
                opt = Option_Lookup(search, OTF_PREFIX_MASK, &matchflags);
                if (opt) {
                    opttok = tok;
                    if (!(matchflags & OTF_STICKY)) {
                        if (tok->val == ATK_OPTION) {
                            tok = Arg_UsedToken();
                        }
                        tok = Arg_UsedToken();
                    } else {
                        lptr = opt->names;
                        optname = tok->text;
                        while (*lptr && *lptr != '|') {
                            ++lptr;
                        }
                        tok = Arg_UsedToken();

                        if (!subparse) {
                            strcpy(tok->text, optname + (lptr - opt->names));
                        }

                        curopt[(lptr - opt->names)] = 0;

                        if (!tok->text || !tok->text[0]) {
                            Option_PushOpt(0, curopt);
                            Option_ParamError(CLPStr34_ArgumentsExpected, curopt);
                            Option_PopOpt(0);
                            return FALSE;
                        }
                    }
                }
            }

            if (!opt) {
                if (search->flags & LISTFLAGS_UNNAMED) {
                    strcpy(saveopt, curopt);
                    curopt[0] = 0;
                    opt = Option_Lookup(search, 0, &matchflags);
                    strcpy(curopt, tok->text);

                    if (opt) {
                        if (opt->names[0]) {
                            tok = Arg_UsedToken();
                        }
                        if (tok->val == ATK_EQUALS) {
                            tok = Arg_UsedToken();
                        }
                        haderrors = Option_Parse(opt, flags) == 0;
                        parsedany++;
                        if (haderrors) {
                            failed = 1;
                        }
                        opt = 0;
                    } else {
                        CLPFatalError("Missing default for variable list");
                        return FALSE;
                    }
                    strcpy(curopt, saveopt);
                } else if (!opt) {
                    curopt[0] = 0;
                    if (level > 1) {
                        if ((flags & OFLAGS_OPTIONAL) && tok == starttok) {
                            return haderrors == 0;
                        }
                        if (search->flags & LISTFLAGS_ALLOW_UNKNOWNS) {
                            if (!(flags & OFLAGS_GLOBAL)) {
                                Option_SpellList(curparam, search, flags);
                                Option_Warning(
                                    CLPStr20_UnknownOptionX_ExpectedOneOfX, tok->text, curparam
                                );
                            }
                            tok = Arg_UsedToken();
                            parsedany++;
                        } else {
                            Option_SpellList(curparam, search, flags);
                            Option_Error(
                                CLPStr20_UnknownOptionX_ExpectedOneOfX, tok->text, curparam
                            );
                            haderrors++;
                            failed = 1;
                        }
                    } else {
                        if ((search->flags & LISTFLAGS_ALLOW_UNKNOWNS) || parseopts.ignoreUnknown) {
                            if (!(flags & OFLAGS_GLOBAL)) {
                                Option_Warning(CLPStr19_UnknownOptionX, tok->text);
                            }
                            tok = Arg_UsedToken();
                            tok = Arg_GetToken();
                            parsedany++;
                        } else {
                            Option_Error(CLPStr19_UnknownOptionX, tok->text);
                            haderrors++;
                            failed = 1;
                        }
                    }
                }
            }

            if (!haderrors && !failed && opt) {
                flags &= ~OFLAGS_NEGATED;
                if (matchflags & OTF_PREFIX_MASK) {
                    flags |= OFLAGS_NEGATED;
                }
                haderrors = Option_Parse(opt, flags) == 0;
                if (haderrors) {
                    failed = 1;
                }
                parsedany++;
            }
        } else if ((level == 1) && (tok->val == ATK_ARG)) {
            opttok = 0;
            curopt[0] = 0;
            opt = Option_Lookup(search, 0, &matchflags);
            strcpy(curopt, tok->text);
            if (!opt) {
                Option_Warning(CLPStr33_NoDefaultHandlerSetUpForX_Ignoring, curopt);
                failed = 1;
            } else {
                if (!(flags & OFLAGS_GLOBAL)) {
                    haderrors = Option_Parse(opt, flags) == 0;
                } else {
                    parseopts.possibleFiles++;
                    haderrors = 0;
                }
                parsedany++;
                if (haderrors) {
                    failed = 1;
                } else {
                    tok = Arg_UsedToken();
                }
            }
        } else if ((level > 1) && Option_IsEndingLevel(level, tok)) {
            strcpy(savecuropt, curopt);
            if (!(search->flags & LISTFLAGS_UNNAMED)) {
                curopt[0] = 0;
                dopt = Option_Lookup(search, 0, &matchflags);
            } else {
                dopt = 0;
            }

            if (!dopt) {
                if (!(flags & OFLAGS_OPTIONAL)) {
                    Option_Error(CLPStr34_ArgumentsExpected, savecuropt);
                    haderrors++;
                    failed = 1;
                } else {
                    strcpy(curopt, savecuropt);
                    break;
                }
            } else {
                haderrors = Option_Parse(dopt, flags) == 0;
                parsedany++;
                if (haderrors) {
                    failed = 1;
                }
            }
            strcpy(curopt, savecuropt);
        }

        tok = Arg_PeekToken();
        if (!failed) {
            if (tok && (tok->val == ATK_ARG) && (matchflags & OTF_STICKY)) {
                prev = Arg_UndoToken();
                tok = Arg_UsedToken();
                if (tok->text[0] && prev == opttok) {
                    if (opttok) {
                        strcpy(sticky, opttok->text);
                        sticky[strlen(tok->text)] = 0;
                        Option_PushOpt(0, sticky);
                        Param_Error(CLPStr36_UnexpectedAdditionalArgumentX, Arg_GetTokenName(tok));
                        Option_PopOpt(0);
                    } else {
                        CLPReportError(
                            CLPStr36_UnexpectedAdditionalArgumentX, Arg_GetTokenName(tok)
                        );
                    }
                    haderrors++;
                } else {
                    if (!tok->text[0]) {
                        tok = Arg_UsedToken();
                    }
                    if (flags & OFLAGS_GLOBAL) {
                        parseopts.possibleFiles++;
                    }
                }
            } else {
                if (Option_IsEndingThisLevel(level - 1, tok)) {
                    break;
                }
                if (Option_IsEndingThisLevel(level + 1, tok)) {
                    prev = tok;
                    tok = Arg_UsedToken();
                    if ((level != 1) || (tok->val != ATK_OPTION)) {
                        if (opttok) {
                            Option_PushOpt(0, opttok->text);
                            if (tok->val == ATK_ARG) {
                                Param_Error(
                                    CLPStr36_UnexpectedAdditionalArgumentX, Arg_GetTokenName(tok)
                                );
                            } else {
                                Param_Error(CLPStr35_TokenXNotExpected, Arg_GetTokenName(prev));
                            }
                            Option_PopOpt(opttok->text);
                        } else if (tok->val == ATK_ARG) {
                            CLPReportError(
                                CLPStr36_UnexpectedAdditionalArgumentX, Arg_GetTokenName(tok)
                            );
                        } else {
                            CLPReportError(CLPStr35_TokenXNotExpected, Arg_GetTokenName(prev));
                        }
                        haderrors++;
                        failed++;
                    }
                } else if ((level < 2) && Option_IsEndingThisLevel(level + 2, tok)) {
                    if (opttok) {
                        Option_PushOpt(0, opttok->text);
                        if (tok->val == ATK_ARG) {
                            Param_Error(
                                CLPStr36_UnexpectedAdditionalArgumentX, Arg_GetTokenName(tok)
                            );
                        } else {
                            Param_Error(CLPStr35_TokenXNotExpected, Arg_GetTokenName(tok));
                        }
                        Option_PopOpt(opttok->text);
                    } else if (tok->val == ATK_ARG) {
                        CLPReportError(
                            CLPStr36_UnexpectedAdditionalArgumentX, Arg_GetTokenName(tok)
                        );
                    } else {
                        CLPReportError(CLPStr35_TokenXNotExpected, Arg_GetTokenName(tok));
                    }
                    haderrors++;
                    failed++;
                }
            }
        }

        if (haderrors || failed) {
            while (!Option_IsEndingLevel(level, tok)) {
                tok = Arg_GetToken();
            }
            if (!tok) {
                tok = Arg_UndoToken();
            }
            OPTION_ASSERT(1335, tok);
        }

        if (!parsedany || haderrors) {
            break;
        }

        if (Option_IsEndingThisLevel(level, tok)) {
            tok = Arg_UsedToken();
        } else if (!Option_ContinuesThisLevel(level, tok)) {
            break;
        }
    }

    return haderrors == 0;
}

int Options_Parse(OptionList* options, int flags) {
    int ret;
    char savecuropt[64];

    Options_Reset(options);
    Option_PushList(options);

    if (!(flags & OFLAGS_SUBOPTION)) {
        ret = Options_DoParse(options, flags);
        if (Option_ThisTool() == OTF_TOOL_COMPILER) {
            Arg_AddToToolArgs(&linkargs, ATK_ARG_END, NULL);
        }
    } else {
        strcpy(savecuropt, curopt);
        ret = Options_DoParse(options, flags);
        strcpy(curopt, savecuropt);
    }

    Option_PopList();
    return ret;
}

// (no caller: the linker dropped it, but its literals stay in the unit's .data)
int Option_ParseDefaultOption(OptionList* options) {
    int ret;
    Option* dopt;
    int matchflags;

    Options_Reset(options);
    Option_PushList(options);

    strcpy(curopt, "defaultoptions");
    dopt = Option_Lookup(options, 0, &matchflags);
    if (!dopt) {
        CLPFatalError("Default options not defined");
        ret = TRUE;
    } else {
        ret = Option_Parse(dopt, 0);
    }

    Option_PopList();
    return ret;
}

void Option_ParamError(short id, va_list ap) {
    char buf[4096];

    CLPGetErrorString(id, buf);
    sprintf(&buf[strlen(buf)], "\nwhile parsing option '");
    Args_SpellStack(&buf[strlen(buf)], 0);
    sprintf(&buf[strlen(buf)], "'");
    CLPReportError_V(buf, ap);
}

void Option_ParamWarning(short id, va_list ap) {
    char buf[1024];

    CLPGetErrorString(id, buf);
    sprintf(&buf[strlen(buf)], "\nwhile parsing option '");
    Args_SpellStack(&buf[strlen(buf)], 0);
    sprintf(&buf[strlen(buf)], "'");
    CLPReportWarning_V(buf, ap);
}

void Option_OptionError(short id, va_list ap) {
    char buf[1024];

    CLPGetErrorString(id, buf);
    if (Args_StackSize() >= 2) {
        sprintf(&buf[strlen(buf)], "\nwhile parsing option '");
        Args_SpellStack(&buf[strlen(buf)], 0);
        sprintf(&buf[strlen(buf)], "'");
    }
    CLPReportError_V(buf, ap);
}

void Option_OptionWarning(short id, va_list ap) {
    char buf[1024];

    CLPGetErrorString(id, buf);
    if (Args_StackSize() >= 2) {
        sprintf(&buf[strlen(buf)], "\nwhile parsing option '");
        Args_SpellStack(&buf[strlen(buf)], 0);
        sprintf(&buf[strlen(buf)], "'");
    }
    CLPReportWarning_V(buf, ap);
}

void Option_Error(VarArgShort id, ...) {
    va_list va;
    va_start(va, id);
    Option_OptionError(id, va);
    va_end(va);
}

void Option_Warning(VarArgShort id, ...) {
    va_list va;
    va_start(va, id);
    Option_OptionWarning(id, va);
    va_end(va);
}

int Options_Help(const char* keyword) {
    int scan;
    OptionList* lst;

    Help_Init();

    if (parseopts.helpFlags & HELPFLAGS_USAGE) {
        ShowVersion(TRUE);
        Help_Line('=');
        Help_Usage();
    } else {
        ShowVersion(TRUE);
        for (scan = 0; scan < numoptionlists; scan++) {
            if ((lst = optionlists[scan])) {
                if (parseopts.helpFlags & HELPFLAGS_SEARCH_HELP) {
                    if (keyword && keyword[0] && lst->help && strstr(lst->help, keyword)) {
                        Help_Options(lst, 0, "");
                    }
                } else {
                    Help_Options(lst, 0, keyword);
                }
            }
        }
    }

    Help_Term();
    return TRUE;
}

int Option_Help(const char* opt) {
    Option* find;
    int matchflags;
    int ret;

    find = NULL;
    ret = FALSE;
    Option_PushList(Options_GetOptions());
    Option_PushOpt(0, "help");
    if (opt[0] == MAINOPTCHAR[0]) {
        strcpy(curopt, opt + 1);
    } else {
        strcpy(curopt, opt);
    }

    if (!curopt[1]) {
        find = Option_Lookup(Options_GetOptions(), OTF_PREFIX_MASK | OTF_STICKY, &matchflags);
    }
    if (!find) {
        find = Option_Lookup(Options_GetOptions(), OTF_PREFIX_MASK, &matchflags);
    }

    if (find) {
        Help_Init();
        if (!Help_Option(Options_GetOptions(), find, 0, "")) {
            CLPReportWarning(CLPStr38_NoHelpAvailableForOptionX, opt);
        }
        Help_Term();
        ret = TRUE;
    } else {
        Option_Error(CLPStr19_UnknownOptionX, opt);
        ret = FALSE;
    }

    Option_PopOpt(curopt);
    Option_PopList();
    return ret;
}

int Options_DisplayHelp(void) {
    if (parseopts.helpFlags & HELPFLAGS_ONE_OPTION) {
        return Option_Help(parseopts.helpKey);
    } else {
        return Options_Help(parseopts.helpKey);
    }
}
