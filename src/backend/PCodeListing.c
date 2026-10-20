// PCodeListing.c: the pcode listing. The compiler is built with the listing compiled out (its
// public functions are empty), but the helpers stay in the object, dead: the linker drops them, and
// their literals are the unit's .data. Where no source of a helper's body is known, the body is
// written to use those literals in their order; each such body is marked TODO (reconstructed from
// the literals only).
#include "compiler/PCodeListing.h"
#include "compiler/Alias.h"
#include "compiler/BitVectors.h"
#include "compiler/CError.h"
#include "compiler/CMangler.h"
#include "compiler/CompilerTools.h"
#include "compiler/InterferenceGraph.h"
#include "compiler/LiveVariables.h"
#include "compiler/objects.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include <stdio.h>
#include <string.h>

// The listing's file. The object has no .bss (none of the listing's statics are there), so this is
// a declaration that only the dead helpers use and the linker never resolves.
extern FILE* pcfile;
int pclist_bad_operand;

int dumpalias(Alias* alias, int len, Boolean newline, Boolean expand);
int GetLineOffset(char* str, int lineNum, int len);

// 2.0 lists every bit of the set; 2.5 on lists runs of bits as ranges and counts them
#if VERSION < VERSION_GC_2_5
static void formatdataflowset(char* name, UInt32* vec, UInt32 size, char* format) {
    UInt32 i;
    UInt32 counter;
    char* separator;

    separator = "";
    fprintf(pcfile, "%s = {", name);

    for (i = 0, counter = 0; i < size; i++) {
        if (bitvectorgetbit(i, vec)) {
            if (i) {
#ifdef __MWERKS__
                fprintf(pcfile, separator);
#else
                fprintf(pcfile, "%s", separator); // (no separator has a conversion)
#endif
            }
            if (counter++ == 10) {
                fprintf(pcfile, "\n\t\t");
                counter = 0;
            }
            fprintf(pcfile, format, i);
            separator = ",";
        }
    }

    fprintf(pcfile, "}\n");
}
#else
// TODO: reconstructed from the literals
static void formatdataflowset(char* name, UInt32* vec, UInt32 size, char* format) {
    UInt32 i;
    UInt32 start;
    UInt32 counter;
    int runs;
    char* separator;

    separator = "";
    fprintf(pcfile, "%s = {", name);

    runs = 0;
    for (i = 0, counter = 0; i < size; i++) {
        if (bitvectorgetbit(i, vec)) {
            start = i;
            while (i + 1 < size && bitvectorgetbit(i + 1, vec)) {
                i++;
            }
#ifdef __MWERKS__
            fprintf(pcfile, separator);
#else
            fprintf(pcfile, "%s", separator); // (no separator has a conversion)
#endif
            fprintf(pcfile, format, start);
            if (i != start) {
                fprintf(pcfile, "-");
                fprintf(pcfile, format, i);
            }
            runs++;
            if (++counter == 10) {
                separator = ",\n\t\t";
                counter = 0;
            } else {
                separator = ",";
            }
        }
    }

    if (runs) {
        fprintf(pcfile, "} words=%d runs = %d\n", (int)((size + 31) >> 5), runs);
    } else {
        fprintf(pcfile, "}\n");
    }
    if (counter) {
        fprintf(pcfile, "\n\t\t");
    }
}
#endif

static void pclistblock(PCodeBlock* block, char* format, UInt32 vecSize) {
    PCLink* link;
    PCodeLabel* label;
    int chr;
    PCode* instr;
    char buf[500];

    fprintf(
        pcfile,
        ":{%4.4x}::::::::::::::::::::::::::::::::::::::::LOOPWEIGHT=%ld\n",
        block->flags,
        (long)block->loopWeight
    );
    fprintf(pcfile, "B%ld: ", (long)block->blockIndex);

    fprintf(pcfile, "Successors = { ");
    for (link = block->successors; link; link = link->nextLink) {
        if (link->block) {
            fprintf(pcfile, "B%ld ", (long)link->block->blockIndex);
        }
    }
    fprintf(pcfile, "}  ");

    fprintf(pcfile, "Predecessors = { ");
    for (link = block->predecessors; link; link = link->nextLink) {
        if (link->block) {
            fprintf(pcfile, "B%ld ", (long)link->block->blockIndex);
        }
    }

    if (block->labels) {
        fprintf(pcfile, "}  Labels = { ");
        for (label = block->labels; label; label = label->nextLabel) {
            fprintf(pcfile, "L%ld ", (long)label->index);
        }
    }

    fprintf(pcfile, "}\n\n");

    for (instr = block->firstPCode; instr; instr = instr->nextPCode) {
        formatoperands(instr, buf, 1);
        chr = (PCODE_FLAG_SET_F(instr) & fRecordBit) ? '.' : ' ';
        fprintf(pcfile, "        %-7s%c %s\n", opcodeinfo[instr->op].name, chr, buf);

        if (instr->alias) {
            dumpalias(instr->alias, 0, TRUE, FALSE);
        }
    }

    if (vecSize) {
        fprintf(pcfile, "............................................................\n");
        formatdataflowset("use", liveinfo[block->blockIndex].use, vecSize, format);
        formatdataflowset("def", liveinfo[block->blockIndex].def, vecSize, format);
        formatdataflowset("in ", liveinfo[block->blockIndex].in, vecSize, format);
        formatdataflowset("out", liveinfo[block->blockIndex].out, vecSize, format);
    }

    // TODO: the original's literal; where it printed it is not known
    if (pclist_bad_operand) {
        fprintf(pcfile, " %-7s %s", "", "");
    }

    fflush(pcfile);

    if (pclist_bad_operand) {
        CError_FATAL(252);
    }
}

static void pclistonoff(int on) { // NOTE: invented name: on
    if (on) {
        fprintf(pcfile, "On\n");
    } else {
        fprintf(pcfile, "Off\n");
    }
}

void pcinitlisting(void) {}

void pccleanuplisting(void) {}

void pclistblocks(char* name1, char* name2) {}

void pclistdataflow(void) {}

void pclistinterferences(char* class_format, int regcount) {}

void pclistspill(void) {}

void pclistcopypropitem(void) {}

void pclistcoalesce(void) {}

void pclistusedefs(void) {}

void pclistpropinfo(void) {}

static void listloop(void) {}

static void listloops(void) {}

void pclistloops(void) {}

static void listswitchtables(void) {}

void pclistswitchtables(void) {}

void pclistdominators(void) {}

void pclistbackedge(void) {}

static char* GetInterferenceFlags(IGNode* node) {
    char* buf;
    Boolean first;

    first = TRUE;
    buf = oalloc(512);
    buf[0] = 0;

    if (node->flags & fSpilled) {
        strcat(buf, "fSpilled");
        first = FALSE;
    }

    if (node->flags & fPushed) {
        if (!first) {
            strcat(buf, "|");
        }
        strcat(buf, "fPushed");
        first = FALSE;
    }

    if (node->flags & fCoalesced) {
        if (!first) {
            strcat(buf, "|");
        }
        strcat(buf, "fCoalesced");
        first = FALSE;
    }

    if (node->flags & fCoalescedInto) {
        if (!first) {
            strcat(buf, "|");
        }
        strcat(buf, "fCoalescedInto");
        first = FALSE;
    }

    if (node->flags & fPairHigh) {
        if (!first) {
            strcat(buf, "|");
        }
        strcat(buf, "fPairHigh");
        first = FALSE;
    }

    if (node->flags & fPairLow) {
        if (!first) {
            strcat(buf, "|");
        }
        strcat(buf, "fPairLow");
        first = FALSE;
    }

    if (!*buf) {
        strcat(buf, "no_flags");
    }

    return buf;
}

void pclistinterferencegraphnode(void) {}

void pclistinterferencegraph(void) {}

void pclistblock_scheduler(void) {}

void pclistblocks_start_scheduler(char* str1, char* str2) {}

#if VERSION < VERSION_GC_2_5
// 2.0's file has 141 lines fewer above here
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -141
#endif
void pclistblocks_end_scheduler(void) {
    if (pclist_bad_operand) {
        CError_FATAL(1525);
    }
}

static void printheapsize(void) {}

void pctotalheap(void) {}

void pctotalmemory(void) {}

void pcmessage(char* probably_a_string, ...) {}

// TODO: reconstructed from the literals (the set case's "!*0:%d")
int formatalias(Alias* alias, char* buf, int bufSize) {
    char* name;
    char* typestr;
    int len;
    int len2;

    if (bufSize < 16) {
        return sprintf(buf, "...");
    }

    switch (alias->type) {
        case AliasType_Object:
        case AliasType_ObjectPart:
            name = CMangler_GetLinkName(alias->object)->name;
            if (!strlen(name) || name[0] < 0) {
                CError_FATAL(1458);
            }

            if (strlen(name) + 16 > (UInt32)bufSize) {
                return sprintf(buf, "...");
            }

            switch (alias->object->datatype) {
                case DDATA:
                    typestr = "{RW}";
                    break;
                case DLOCAL:
                    typestr = "{SP}";
                    break;
                default:
                    typestr = "";
            }

#ifdef __MWERKS__
            // The original gives the string the '0' flag (which MSL's printf ignores without a
            // width) and passes an argument the format does not use
            len = sprintf(buf, "%0.*s%s", bufSize - 20, name, typestr, alias->size);
#else
            len = sprintf(buf, "%.*s%s", bufSize - 20, name, typestr); // (without either)
#endif
            buf += len;
            if (alias->type == AliasType_Object) {
                return len;
            }

            if (alias->offset == 0) {
                len2 = sprintf(buf, ":%d", (int)alias->size);
            } else if (alias->offset > 0) {
                len2 = sprintf(buf, "+%d:%d", (int)alias->offset, (int)alias->size);
            } else {
                len2 = sprintf(buf, "-%d:%d", (int)-alias->offset, (int)alias->size);
            }

            return len + len2;

        case AliasType_Set:
            len = 0;

            len2 = sprintf(buf, "{");
            buf += len2;
            len += len2;

            if (alias->size) {
                len2 = sprintf(buf, "!*0:%d", (int)alias->size);
            } else {
                len2 = sprintf(buf, "*");
            }
            buf += len2;
            len += len2;

            len2 = sprintf(buf, "}");
            buf += len2;
            len += len2;
            return len;

        default:
            CError_FATAL(1543);
            return 0;
    }
}

// NOTE: invented names: alias, len, newline, expand
// TODO: reconstructed from the literals (2.5 on: two worst cases, and names cut at 60 characters)
int dumpalias(Alias* alias, int len, Boolean newline, Boolean expand) {
    char* name;
    char* typestr;
    AliasMember* member;
    Boolean notFirst;

#if VERSION < VERSION_GC_2_5
    // 2.0: one worst case
    if (!expand && alias == worst_case_ptr) {
        fprintf(pcfile, "    ALIAS = {worst_case}");
        if (newline) {
            fprintf(pcfile, "\n");
        }
        return 0;
    }

    if (newline) {
        if (alias == worst_case_ptr) {
            fprintf(pcfile, "ALIAS worst_case = ");
        } else {
            fprintf(pcfile, "    ALIAS = ");
        }
    }
#else
    if (!expand && alias == worst_case_ptr) {
        fprintf(pcfile, "    ALIAS = {worst_case_ptr}");
        if (newline) {
            fprintf(pcfile, "\n");
        }
        return 0;
    }

    if (!expand && alias == worst_case_func) {
        fprintf(pcfile, "    ALIAS = {worst_case_func}");
        if (newline) {
            fprintf(pcfile, "\n");
        }
        return 0;
    }

    if (newline) {
        if (alias == worst_case_ptr || alias == worst_case_func) {
            fprintf(pcfile, "ALIAS ");
        } else {
            fprintf(pcfile, "    ALIAS = ");
        }
    }

    if (alias == worst_case_ptr) {
        len += fprintf(pcfile, "{worst_case_ptr}");
    } else if (alias == worst_case_func) {
        len += fprintf(pcfile, "{worst_case_func}");
    }
#endif

    switch (alias->type) {
        case AliasType_Object:
        case AliasType_ObjectPart:
            name = CMangler_GetLinkName(alias->object)->name;
            if (!strlen(name) || name[0] < 0) {
                CError_FATAL(1581);
            }

            switch (alias->object->datatype) {
                case DDATA:
                    typestr = "{RW}";
                    break;
                case DLOCAL:
                    typestr = "{SP}";
                    break;
                default:
                    typestr = "";
            }

#if VERSION < VERSION_GC_2_5
#ifdef __MWERKS__
            len += fprintf(pcfile, "%0.80s%s", name, typestr); // (the '0' as in formatalias)
#else
            len += fprintf(pcfile, "%.80s%s", name, typestr);
#endif
#else
            if (strlen(name) > 60) {
#ifdef __MWERKS__
                len += fprintf(pcfile, "%0.60s...%s", name, typestr); // (as in formatalias)
#else
                len += fprintf(pcfile, "%.60s...%s", name, typestr);
#endif
            } else {
                len += fprintf(pcfile, "%s%s", name, typestr);
            }
#endif

            if (alias->type == AliasType_Object) {
                if (newline) {
                    fprintf(pcfile, "\n");
                }
                return len;
            }

            if (alias->offset == 0) {
                len += fprintf(pcfile, ":%d", (int)alias->size);
            } else if (alias->offset > 0) {
                len += fprintf(pcfile, "+%d:%d", (int)alias->offset, (int)alias->size);
            } else {
                len += fprintf(pcfile, "-%d:%d", (int)-alias->offset, (int)alias->size);
            }

            if (newline) {
                fprintf(pcfile, "\n");
            }

            return len;

        case AliasType_Set:
            len += fprintf(pcfile, "{");
            notFirst = FALSE;
            for (member = alias->parents; member; member = member->nextParent) {
                if (member->child->type == AliasType_Object) {
                    if (notFirst) {
                        len += fprintf(pcfile, ",");
                    }
                    if (len > 60) {
                        fprintf(pcfile, "\n                  ");
                        len = 0;
                    }
                    len = dumpalias(member->child, len, FALSE, FALSE);
                    notFirst = TRUE;
                }
            }
            for (member = alias->parents; member; member = member->nextParent) {
                if (member->child->type != AliasType_Object) {
                    if (notFirst) {
                        len += fprintf(pcfile, ",");
                    }
                    if (len > 60) {
                        fprintf(pcfile, "\n                  ");
                        len = 0;
                    }
                    len = dumpalias(member->child, len, FALSE, FALSE);
                    notFirst = TRUE;
                }
            }

            len += fprintf(pcfile, "}");
            if (newline) {
                fprintf(pcfile, "\n");
            }
            return len;

        default:
            CError_FATAL(1661);
            return 0;
    }
}

void pcformatset(void) {}

int GetLineEndOffset(char* str, int lineNum, int len) {
    int offset;
    char* work;

    offset = GetLineOffset(str, lineNum, len);
    if (offset < 0) {
        return offset;
    }

    work = str + offset;
    while (*work) {
        if (*work == '\n') {
            return work - str - 1;
        }
        work++;
    }
    return -1;
}

int GetLineOffset(char* str, int lineNum, int len) {
    char* work = str;
    char* end;

    if (lineNum < 0) {
        return -1;
    }

    end = str + len;
    while (work < end) {
        if (*work == '\n' && --lineNum <= 0) {
            return work - str;
        }
        work++;
    }

    return 0;
}

void DumpSourceCode(void) {}

int DumpIR_SrcBreak(void) {
    return 0;
}
