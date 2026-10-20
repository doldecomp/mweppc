#include "driver/Help.h"
#include "compiler/win32.h"
#include "driver/Arguments.h"
#include "driver/CWParserPluginsPrivate.h"
#include "driver/CWPlugins.h"
#include "driver/Generic.h"
#include "driver/IO.h"
#include "driver/Memory.h"
#include "driver/Option.h"
#include "driver/option_system.h"
#include "driver/Parameter.h"
#include "driver/ParserFace.h"
#include "driver/StringUtils.h"
#include "driver/Targets.h"
#include "driver/Utils.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// One column of the help screen: text queued in a 1 KB ring and laid out a line at a time
typedef struct Side {
    short offset;
    short width;
    char buffer[1024];
    short bptr;
    short blen;
    short indent;
    short vrow;
    short vcol;
    Boolean atEOL;
    Boolean impInd;
} Side;

// (COMMONs nothing in the compiler uses, here for their names: Wii's, whose hash order fits two
// otherwise unnamed slots of the original's COMMON)
short helpExtras;
Boolean showedHelp;
Side left;
Side right;
Side all;
Handle helptext;
static char outLine[256];

static void Help_Output(Side* left, Side* right);
static void Help_OutputSingle(Side* all);
static void Help_Flush(void);

static void Side_Init(Side* s, short offset, short width) {
    memset(s, 0, sizeof(Side));
    s->offset = offset;
    s->width = width;
}

static void Side_Print(Side* s, const char* format, ...) {
    char c;
    char* text;
    char buffer[1024];
    va_list args;

    va_start(args, format);
    vsprintf(buffer, format, args);
    va_end(args);

    text = buffer;
    while (*text) {
        if (s->blen < 1024) {
            c = *(text++);
            if (c == '~' && *text == '~') {
                text++;
                c = *MAINOPTCHAR;
            }

            s->buffer[(s->bptr + s->blen) & 1023] = c;
            s->blen++;
        } else {
            if (s == &left) {
                Help_Output(&left, &right);
            } else {
                Help_OutputSingle(&all);
            }
        }
    }
}

static void Side_NewLine(Side* s) {
    Side_Print(s, "\n");
}

static void Side_Indent(Side* s, short how) {
    if ((s->width - s->indent - how) > (parseopts.ioCols / 8)) {
        s->indent += how;
    } else if ((s->width - s->indent - 1) > (parseopts.ioCols / 10)) {
        s->indent++;
    }
}

static void Side_Outdent(Side* s, short how) {
    if ((s->width - s->indent) < (parseopts.ioCols / 8)) {
        s->indent++;
    } else if (s->indent >= how) {
        s->indent -= how;
    }
}

static unsigned char isBreaker(char c) {
    return (c == '\n') || (c == '\r') || (c == ' ') || (c == '\t') || (c == '\b') || (c == '|');
}

static void Side_DumpLine(Side* s) {
    short col;
    short len;
    short afterspace;
    short eol;
    short ind;
    char c;

    eol = 0;
    ind = 0;
    col = s->offset + s->indent;
    s->vcol = s->indent;
    len = 0;
    afterspace = s->width - s->indent;

    while (s->blen > 0 && s->vcol < s->width && !eol && !ind) {
        c = s->buffer[s->bptr];
        outLine[col + len] = c;
        len++;
        s->vcol++;

        if (isBreaker(c)) {
            afterspace = len;
            eol = (c == '\n') || (c == '\r');
            eol += (c == '\r');
            ind = (c == '\b') || (c == '\t');
            ind += (c == '\b');
        }

        s->bptr = (s->bptr + 1) & 1023;
        s->blen--;
    }

    if (s->blen || eol || ind) {
        s->blen += len - afterspace;
        s->bptr = (s->bptr - (len - afterspace)) & 1023;
        if (eol || ind) {
            len++;
            afterspace--;
        }

        while (afterspace < len) {
            outLine[col + --len] = ' ';
        }
    }

    s->vcol = 0;
    s->vrow++;
    s->atEOL = (eol == 1) || ind || s->blen == 0;
    if ((s->atEOL || ind) && s->impInd) {
        Side_Outdent(s, parseopts.ioCols / 40);
        s->impInd = FALSE;
    }
    if (ind) {
        if (ind == 1) {
            Side_Indent(s, parseopts.ioCols / 25);
        } else {
            Side_Outdent(s, parseopts.ioCols / 25);
        }
    } else if (!s->atEOL && s != &all && !s->impInd) {
        Side_Indent(s, parseopts.ioCols / 40);
        s->impInd = TRUE;
    }
}

static void Help_PrintLine(void) {
    HPrintF(helptext, "%.*s\n", parseopts.ioCols - 1, outLine);
}

static void Help_Output(Side* left, Side* right) {
    while (left->blen || right->blen) {
        memset(outLine, ' ', parseopts.ioCols);
        outLine[left->offset + left->width + 1] = '#';
        if (left->atEOL && right->atEOL) {
            left->atEOL = right->atEOL = FALSE;
        }
        if (!left->atEOL) {
            Side_DumpLine(left);
        }
        if (!right->atEOL) {
            Side_DumpLine(right);
        }
        Help_PrintLine();
    }
}

static void Help_OutputSingle(Side* all) {
    while (all->blen) {
        memset(outLine, ' ', parseopts.ioCols);
        if (all->atEOL) {
            all->atEOL = FALSE;
        }
        if (!all->atEOL) {
            Side_DumpLine(all);
        }
        Help_PrintLine();
    }
}

static void Help_Flush(void) {
    Help_Output(&left, &right);
}

static void Help_NewLine(void) {
    Side_NewLine(&left);
    Side_NewLine(&right);
}

int Help_Option(OptionList* lst, Option* opt, int subprint, const char* keyword) {
    char pfbuf[512];
    char slflags;
    int listFlags;
    Boolean allNoArgs;
    PARAM_T* lastparam;
    Boolean print;
    Boolean printMe;
    // (each parameter described is also stored here and never read: the original's).
    PARAM_T* currentArg; // NOTE: invented name

    if (!opt->names[0] && !(lst->flags & LISTFLAGS_UNNAMED)) {
        return FALSE;
    }

    if (keyword &&
        keyword[0] &&
        !strstr(opt->names, keyword) &&
        (!opt->help || !strstr(opt->help, keyword))) {
        return FALSE;
    }

    if ((opt->avail & OTF_SECRET) && !(parseopts.helpFlags & HELPFLAGS_SECRET)) {
        return FALSE;
    }

    if ((opt->avail & OTF_OBSOLETE) && !(parseopts.helpFlags & HELPFLAGS_OBSOLETE)) {
        return FALSE;
    }

    if ((opt->avail & OTF_DEPRECATED) && !(parseopts.helpFlags & HELPFLAGS_DEPRECATED)) {
        return FALSE;
    }

    if ((opt->avail & OTF_IGNORED) && !(parseopts.helpFlags & HELPFLAGS_IGNORED)) {
        return FALSE;
    }

    if ((opt->avail & OTF_MEANINGLESS) && !(parseopts.helpFlags & HELPFLAGS_MEANINGLESS)) {
        return FALSE;
    }

    if (!(parseopts.helpFlags & HELPFLAGS_NORMAL) && !(opt->avail & OTF_ALL_HIDDEN_BY_DEFAULT)) {
        return FALSE;
    }

    if (opt->help || (opt->avail & OTF_HAS_SUB_OPTIONS)) {
        allNoArgs = TRUE;
        lastparam = 0;
        if (parseopts.helpFlags & HELPFLAGS_SPACES) {
            Help_NewLine();
        }
        if ((opt->avail & OTF_GLOBAL) && !subprint) {
            Side_Print(&right, "global; ");
        }
        if (compat != COMPAT_DOS && (opt->avail & OTF_CASED)) {
            Side_Print(&right, "cased; ");
        }

        slflags = !subprint ? (char)SLFLAGS_OPTCHAR : (char)SLFLAGS_SUBOPTION;
        switch (opt->avail & OTF_PREFIX_MASK) {
            case OTF_NO_PREFIX:
                slflags |= SLFLAGS_NO_PREFIX;
                break;
            case OTF_MINUS_PREFIX:
                slflags |= SLFLAGS_MINUS_PREFIX;
                break;
            case OTF_NO_DASH_PREFIX:
                slflags |= SLFLAGS_NO_DASH_PREFIX;
                break;
        }
        if (opt->avail & OTF_STICKY) {
            slflags |= SLFLAGS_STICKY;
        }

        Utils_SpellList(opt->names[0] ? opt->names : "...", pfbuf, slflags);
        Side_Print(&left, pfbuf);

        if (opt->avail & OTF_OBSOLETE) {
            Side_Print(&right, "obsolete;\r");
        }
        if (opt->avail & OTF_COMPATIBILITY) {
            Side_Print(&right, "compatibility;\r");
        }
        if (opt->avail & OTF_IGNORED) {
            Side_Print(&right, "ignored;\r");
        }

        listFlags = ((lst->flags & LISTFLAGS_COMPILER) ? OTF_TOOL_COMPILER : 0) |
            ((lst->flags & LISTFLAGS_LINKER) ? OTF_TOOL_LINKER : 0) |
            ((lst->flags & LISTFLAGS_DISASSEMBLER) ? OTF_TOOL_DISASSEMBLER : 0);
        if (!Option_ForThisTool(opt) ||
            Option_AlsoPassedFromThisTool(opt) ||
            listFlags != Option_ThisTool()) {
            print = FALSE;
            printMe = TRUE;
            if ((opt->avail & OTF_TOOL_MASK) != (unsigned int)listFlags) {
                print = TRUE;
            }
            if (Option_ForThisTool(opt) && Option_AlsoPassedFromThisTool(opt)) {
                printMe = FALSE;
            }

            if (print) {
                char opttool[64] = "";
                if ((opt->avail & OTF_TOOL_MASK) == (unsigned int)OTF_TOOL_MASK) {
                    strcat(opttool, "all tools");
                } else {
                    if (Option_ForTool(opt, OTF_TOOL_COMPILER) &&
                        ((Option_ThisTool() != (unsigned int)OTF_TOOL_COMPILER) || printMe)) {
                        strcat(opttool, "this tool");
                    }
                    if (Option_ForTool(opt, OTF_TOOL_LINKER) &&
                        ((Option_ThisTool() != (unsigned int)OTF_TOOL_LINKER) || printMe)) {
                        if (opttool[0]) {
                            strcat(opttool, ", ");
                        }
                        strcat(opttool, "linker");
                    }
                    if (Option_ForTool(opt, OTF_TOOL_DISASSEMBLER) &&
                        ((Option_ThisTool() != (unsigned int)OTF_TOOL_DISASSEMBLER) || printMe)) {
                        if (opttool[0]) {
                            strcat(opttool, ", ");
                        }
                        strcat(opttool, "disassembler");
                    }
                    if (!Option_ForTool(opt, OTF_TOOL_MASK)) {
                        strcat(opttool, "another tool");
                    }
                }

                if (printMe || !Option_ForThisTool(opt)) {
                    Side_Print(&right, "for %s;\r", opttool);
                } else if (parseopts.passingArgs) {
                    Side_Print(&right, "passed to %s;\r", opttool);
                }
            }
        }

        if (opt->avail & OTF_WARNING) {
            Side_Print(&right, "warning:\r");
        }

        if (opt->avail & OTF_DEPRECATED) {
            Side_Print(&right, "deprecated;\rinstead use ");
        } else if (opt->avail & OTF_SUBSTITUTED) {
            Side_Print(&right, "substituted with ");
        }

        if (opt->help) {
            Side_Print(&right, "%s", opt->help);
        }

        if (opt->param && !(opt->avail & OTF_IGNORED)) {
            PARAM_T* scan = opt->param;
            PARAM_T* firstparam = 0;
            const char* desc;
            const char* help;
            const char* defaul;
            while (scan) {
                if ((scan->flags & PARAMFLAGS_ARG_MASK) != PARAMFLAGS_NO_ARG) {
                    currentArg = scan;
                    if (!firstparam) {
                        firstparam = scan;
                    }

                    allNoArgs = FALSE;
                    Param_DescHelp(scan, &desc, &help, &defaul);
                    if (desc) {
                        if (((scan->flags & PARAMFLAGS_ARG_MASK) == PARAMFLAGS_OPTIONAL_ARG) &&
                            scan->which != PARAMWHICH_Setting &&
                            scan->which != PARAMWHICH_IfArg) {
                            if (SEPOPTSTR[0] == ' ') {
                                Side_Print(
                                    &left,
                                    (scan != firstparam) ? "[,"
                                        : subprint       ? "[="
                                                         : " ["
                                );
                            } else {
                                Side_Print(
                                    &left,
                                    "[%s",
                                    (scan != firstparam) ? ","
                                        : subprint       ? "="
                                                         : SEPOPTSTR
                                );
                            }
                        } else {
                            Side_Print(
                                &left,
                                (scan != firstparam) ? ","
                                    : subprint       ? "="
                                    : ((opt->avail & OTF_STICKY) && strchr(opt->names, '|') == NULL)
                                    ? ""
                                    : SEPOPTSTR
                            );
                        }
                        Side_Print(&left, "%s", desc);
                        if (((scan->flags & PARAMFLAGS_ARG_MASK) == PARAMFLAGS_OPTIONAL_ARG) &&
                            scan->which != PARAMWHICH_Setting &&
                            scan->which != PARAMWHICH_IfArg) {
                            Side_Print(&left, "]");
                        }
                        if (help) {
                            if ((scan->flags & PARAMFLAGS_ARG_MASK) != PARAMFLAGS_OPTIONAL_ARG) {
                                Side_Print(&right, "; for '%s', %s", desc, help);
                            } else {
                                Side_Print(&right, "; if parameter specified, %s", help);
                            }
                        }
                        if (defaul && !(opt->avail & OTF_HIDE_DEFAULT)) {
                            if (firstparam == scan) {
                                Side_Print(&right, "; default is %s", defaul);
                            } else {
                                Side_Print(&right, ",%s", defaul);
                            }
                        }
                    }
                }
                lastparam = scan;
                scan = scan->next;
            }

            if (allNoArgs && !(opt->avail & OTF_HIDE_DEFAULT)) {
                PARAM_T* scan = opt->param;
                Boolean isdefault = scan != NULL;
                while (scan && isdefault) {
                    isdefault &= Param_Compare(scan);
                    scan = scan->next;
                }

                if (isdefault) {
                    Side_Print(&right, "; default");
                }
            }
        }

        if (opt->avail & OTF_MEANINGLESS) {
            Side_Print(&right, "; meaningless for this target");
        }

        if ((opt->avail & OTF_HAS_SUB_OPTIONS) && opt->sub) {
            if (!allNoArgs) {
                Side_Print(
                    &left,
                    "%s",
                    (opt->avail & OTF_SUB_OPTIONS_OPTIONAL)
                        ? ((lastparam->flags & PARAMFLAGS_EQUALS_AFTER) ? "[=" : "[,")
                        : ((lastparam->flags & PARAMFLAGS_EQUALS_AFTER) ? "=" : ",")
                );
            } else if (!(opt->avail & OTF_STICKY)) {
                if (opt->avail & OTF_SUB_OPTIONS_OPTIONAL) {
                    if (SEPOPTSTR[0] == ' ') {
                        Side_Print(&left, subprint ? "[=" : " [");
                    } else {
                        Side_Print(&left, "[%s", subprint ? "=" : SEPOPTSTR);
                    }
                } else {
                    Side_Print(&left, "%c", subprint ? '=' : SEPOPTSTR[0]);
                }
            } else {
                if (opt->avail & OTF_SUB_OPTIONS_OPTIONAL) {
                    Side_Print(&left, subprint ? "[" : (SEPOPTSTR[0] == ' ') ? " [" : "[");
                }
            }

            Side_Print(
                &left,
                "%s%s%s",
                opt->sub->help ? opt->sub->help : "keyword",
                (opt->sub->flags & PARAMFLAGS_NO_ARG) ? "" : "[,...]",
                (opt->avail & OTF_SUB_OPTIONS_OPTIONAL) ? "]" : ""
            );

            Side_Print(&left, "\t");
            Side_Print(&right, "\t");
            Help_Options(opt->sub, 1, "");
            Side_Print(&right, "\b");
            Side_Print(&left, "\b");
        } else {
            Side_Print(&left, "\n");
            Side_Print(&right, "\n");
        }
    }

    Help_Flush();
    return TRUE;
}

void Help_Options(OptionList* lst, int subprint, const char* keyword) {
    Option** opts;
    int toolflags;
    Boolean show;

    opts = lst->list;
    toolflags = 0;
    if (Option_ThisTool() == OTF_TOOL_COMPILER) {
        toolflags |= LISTFLAGS_COMPILER;
    } else {
        toolflags |= LISTFLAGS_LINKER;
    }

    if (!subprint && (parseopts.helpFlags & HELPFLAGS_TOOL)) {
        if ((parseopts.helpFlags & HELPFLAGS_TOOL_BOTH) == HELPFLAGS_TOOL_THIS &&
            (lst->flags & LISTFLAGS_TOOL_MASK) &&
            !(lst->flags & toolflags)) {
            return;
        }
        if ((parseopts.helpFlags & HELPFLAGS_TOOL_BOTH) == HELPFLAGS_TOOL_OTHER &&
            (((lst->flags & LISTFLAGS_TOOL_MASK) == (unsigned int)toolflags) ||
             ((lst->flags & LISTFLAGS_TOOL_MASK) == (unsigned int)LISTFLAGS_NONE))) {
            return;
        }
    }

    if (lst->help && !subprint && opts[0]) {
        Help_Line('-');
        Side_Print(&all, "%s", lst->help);
        Help_OutputSingle(&all);
        Help_Line('-');
    }

    while (*opts) {
        show = FALSE;
        if (!(parseopts.helpFlags & HELPFLAGS_TOOL)) {
            if (((parseopts.helpFlags & HELPFLAGS_TOOL_BOTH) == HELPFLAGS_TOOL_BOTH) &&
                (parseopts.passingArgs ? (Option_ForTool(*opts, OTF_TOOL_LINKER) ||
                                          Option_ForTool(*opts, OTF_TOOL_DISASSEMBLER))
                                       : TRUE) &&
                Option_ForThisTool(*opts)) {
                show = TRUE;
            }
        } else if ((parseopts.helpFlags & HELPFLAGS_TOOL_BOTH) == HELPFLAGS_TOOL_BOTH) {
            show = TRUE;
        } else if ((parseopts.helpFlags & HELPFLAGS_TOOL_THIS) && Option_ForThisTool(*opts)) {
            show = TRUE;
        } else if ((parseopts.helpFlags & HELPFLAGS_TOOL_OTHER) && !Option_ForThisTool(*opts)) {
            show = TRUE;
        } else if ((parseopts.helpFlags & HELPFLAGS_TOOL_OTHER) &&
                   Option_ForTool(*opts, ~Option_ThisTool() & OTF_TOOL_MASK)) {
            show = TRUE;
        }

        if (show) {
            Help_Option(lst, *opts, subprint, keyword);
        }

        ++opts;
    }

    if (subprint && (parseopts.helpFlags & HELPFLAGS_SPACES)) {
        Help_NewLine();
    }
    Help_Flush();
    if (!subprint) {
        HPrintF(helptext, "\n");
    }
}

void Help_Usage(void) {
    Side_Print(
        &all,
        "\tGuide to help:\b"
        "\tWhen an option is specified as '~~xxx | yy[y] | zzz', then either '~~xxx', '~~yy', '~~yyy', or '~~zzz' matches the option.\b"
        "\tAn option given as '~~[no]xxx' may be given as '~~xxx' or '~~noxxx'; '~~noxxx' reverses the meaning of the option.\b"
    );
    Help_OutputSingle(&all);

    Side_Print(
        &all,
        "\tFor most options, the option and the parameters are separated by a %sspace.  When the option's name is '~~xxx+', however, the parameter must directly follow the option, without the '+' (as in '~~xxx45').\b",
        (compat != COMPAT_DOS) ? "" : "colon or "
    );
    Side_Print(
        &all,
        "\tA parameter included in brackets '[]' is optional. An ellipsis '...' indicates that the previous type of parameter may be repeated as a list.\b"
    );
    Help_OutputSingle(&all);

    Side_Print(
        &all,
        "\t%s-- \"compatability\" indicates that the option is borrowed from another vendor's tool and may only approximate its counterpart.\r"
        "-- \"global\" indicates that the option has an effect over the entire command line and is parsed before any other options.  When several global options are specified, they are interpreted in order.\r"
        "-- \"deprecated\" indicates that the option will be eliminated in the future and should not be used any longer.  An alternative form is supplied.\r",
        (compat != COMPAT_DOS)
            ? "-- \"cased\" indicates that the option is case-sensitive.  By default, no options are case-sensitive.\r"
            : ""
    );
    Help_OutputSingle(&all);

    Side_Print(
        &all,
        "-- \"ignored\" means the option will be accepted but has no effect on the tool.\r"
        "-- \"meaningless\" means the option is accepted but probably has no meaning for the target OS.\r"
        "-- \"obsolete\" means the option was once deprecated and is now gone.\r"
        "-- \"substituted\" means the option has the same effect as another. This points out a preferred form and prevents confusion when similar options appear in the help.\r"
        "-- \"default\" in the help text indicates that the given value or variation of an option will be used unless otherwise overridden. \b"
    );
    Help_OutputSingle(&all);

    Side_Print(
        &all,
        "\tThe symbols ',' %s separate options and parameters unconditionally; to include one of these symbols in a parameter or filename, escape it (e.g., as '\\,' in mwcc file.c\\,v).\b\n",
        (compat != COMPAT_DOS) ? "and '='" : ", ':', and '='"
    );
    Help_OutputSingle(&all);

    if (parseopts.passingArgs && pTool->TYPE == CWDROPINCOMPILERTYPE) {
        Side_Print(
            &all,
            "\tThis tool calls the linker (unless a compiler option such as ~~c prevents it) and understands linker options -- use '~~help tool=other' to see them.  Options marked \"passed to linker\" are used by the compiler and the linker; options marked \"for linker\" are used only by the linker. When using the compiler and linker separately, you must pass the common options to both.\b\n"
        );
    }
    Help_OutputSingle(&all);
}

// (no caller: the linker dropped it, but its literal stays in the unit's .data)
void Help_Null(void) {
    Side_Print(
        &all,
        "%s [options, filenames...]\n\nExecute '%s %shelp' for more information.",
        OS_GetFileNamePtr(parseopts.args->argv[0]),
        OS_GetFileNamePtr(parseopts.args->argv[0]),
        MAINOPTCHAR
    );
    Help_OutputSingle(&all);
}

void Help_Init(void) {
    short lb;
    short le;
    short rb;
    short re;

    if (!(helptext = NewHandle(0))) {
        fprintf(stderr, "\n*** Out of memory\n");
        exit(-23);
    }

    lb = parseopts.ioCols / 40;
    le = (parseopts.ioCols / 3) + lb;
    rb = le + 3 + ((parseopts.ioCols / 60) & ~1);
    re = parseopts.ioCols - 1;
    Side_Init(&left, lb, le - lb);
    Side_Init(&right, rb, re - rb);
    Side_Init(&all, 0, re);
}

void Help_Line(char ch) {
    char line[256];
    memset(line, ch, 255);
    line[255] = 0;
    HPrintF(helptext, "%.*s\n", parseopts.ioCols - 1, line);
}

void Help_Term(void) {
    ShowTextHandle(0, helptext);
    DisposeHandle(helptext);
}
