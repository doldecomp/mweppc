#include "compiler/CPreprocess.h"
#include "compiler/CError.h"
#include "compiler/CMachine.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/tokens.h"
#include "driver/COSToolsCLT.h"
#include <stdio.h>
#include <string.h>

static SInt32 pp_text_depth;
static char* pp_text_start;

void CPrep_PreprocessDumpNewLine(void) {
    if (copts.line_prepdump && pplist.data && filesp >= 0) {
        AppendGListData(&pplist, "\r\n", 2);
    }
}

void CPrep_PreprocessDumpFileInfo(Boolean newLine) {
    char linebuf[512];
    int size;
    SInt32 tmp32;
    unsigned char filename[256];
    SInt16 tmp16;

    if (pplist.data && filesp >= 0) {
        if (pplist.size > 0 &&
            ((nlflag && newLine) ||
             (*pplist.data)[pplist.size - 1] != "\r\n"[strlen("\r\n") - 1])) {
            AppendGListName(&pplist, "\r\n");
        }

        if (copts.line_prepdump) {
            size = sprintf(linebuf, "#line %ld\t\"", (long)linenumber);
        } else {
            size = sprintf(linebuf, "/* #line %ld\t\"", (long)linenumber);
        }
        AppendGListData(&pplist, linebuf, size);

        if (copts.fullpath_prepdump) {
            if (prep_file->nameNode) {
                AppendGListData(
                    &pplist, prep_file->nameNode->name, strlen(prep_file->nameNode->name)
                );
            } else {
                COS_FileGetPathName(linebuf, &prep_file->textfile, &tmp32);
                AppendGListData(&pplist, linebuf, strlen(linebuf));
            }
        } else {
            if (prep_file->nameNode) {
                char* work = prep_file->nameNode->name + strlen(prep_file->nameNode->name);
                while (work > prep_file->nameNode->name && !strchr("/\\:", work[-1])) {
                    work--;
                }

                AppendGListData(&pplist, work, strlen(work));
            } else {
                COS_FileGetFSSpecInfo(&prep_file->textfile, &tmp16, &tmp32, filename);
                AppendGListData(&pplist, &filename[1], filename[0]);
            }
        }

        size = sprintf(linebuf, "\"\t/* stack depth %ld */", (long)filesp);
        AppendGListData(&pplist, linebuf, size);

        if (copts.line_prepdump && newLine) {
            CPrep_PreprocessDumpNewLine();
        }

        nlflag = TRUE;
    }
}

static void CPrep_DumpWString(UInt16* str, short len) {
    int divisor;
    int i;

    while (len--) {
        if (*str < 32) {
            AppendGListByte(&pplist, '\\');
            switch (*str) {
                case 7:
                    AppendGListByte(&pplist, 'a');
                    break;
                case 8:
                    AppendGListByte(&pplist, 'b');
                    break;
                case 27:
                    AppendGListByte(&pplist, 'e');
                    break;
                case 12:
                    AppendGListByte(&pplist, 'f');
                    break;
                case 10:
                    AppendGListByte(&pplist, 'n');
                    break;
                case 13:
                    AppendGListByte(&pplist, 'r');
                    break;
                case 9:
                    AppendGListByte(&pplist, 't');
                    break;
                case 11:
                    AppendGListByte(&pplist, 'v');
                    break;
                default:
                    if (*str >= 8) {
                        AppendGListByte(&pplist, '0' + (*str / 8));
                    }
                    AppendGListByte(&pplist, '0' + (*str % 8));
            }
        } else if (*str > 255) {
            AppendGListByte(&pplist, '\\');
            AppendGListByte(&pplist, 'x');

            divisor = 0x1000;
            for (i = 0; i < 4; i++) {
                AppendGListByte(&pplist, "0123456789ABCDEF"[(*str / divisor) % 16]);
                divisor /= 16;
            }
        } else {
            switch (*str) {
                case '"':
                case '\\':
                    AppendGListByte(&pplist, '\\');
                default:
                    AppendGListByte(&pplist, *str);
            }
        }
        str++;
    }
}

static void CPrep_DumpString(UInt8* str, short len) {
    while (len--) {
        if (*str < 32) {
            AppendGListByte(&pplist, '\\');
            switch (*str) {
                case 7:
                    AppendGListByte(&pplist, 'a');
                    break;
                case 8:
                    AppendGListByte(&pplist, 'b');
                    break;
                case 12:
                    AppendGListByte(&pplist, 'f');
                    break;
                case 10:
                    AppendGListByte(&pplist, 'n');
                    break;
                case 13:
                    AppendGListByte(&pplist, 'r');
                    break;
                case 9:
                    AppendGListByte(&pplist, 't');
                    break;
                case 11:
                    AppendGListByte(&pplist, 'v');
                    break;
                default:
                    if (*str >= 8) {
                        AppendGListByte(&pplist, '0' + (*str / 8));
                    }
                    AppendGListByte(&pplist, '0' + (*str % 8));
            }
        } else {
            switch (*str) {
                case '"':
                case '\\':
                    AppendGListByte(&pplist, '\\');
                default:
                    AppendGListByte(&pplist, *str);
            }
        }
        str++;
    }
}

void CPrep_PreprocessDumpDirective(char* directive, char* name) {
    if (!copts.line_prepdump && nlflag && pplist.size > 0) {
        AppendGListName(&pplist, "\r\n");
    }
    AppendGListData(&pplist, directive, strlen(directive));
    if (name) {
        AppendGListData(&pplist, name, strlen(name));
    }
    pp_text_start = pos;
    pp_text_depth = tokenstacklevel;
}

void CPrep_PreprocessDumpDirectiveEnd(void) {
    CError_ASSERT(239, pp_text_start);
    if (pp_text_depth == tokenstacklevel) {
        AppendGListData(&pplist, pp_text_start, pos - pp_text_start);
    }
}

void CPrep_Preprocess(void) {
    int innertoken;
    short token;
    char startToken;
    char endToken;
    int depth;
    Boolean save_asmpoundcomment;
    Boolean save_cplusplus;
    char* p;

    startToken = 0;
    depth = 0;

    if (InitGList(&pplist, 10000)) {
        CError_NoMem();
    }

    nlflag = FALSE;
    spaceskip = FALSE;

    if ((token = lex())) {
        do {
            if (nlflag) {
                if (!copts.line_prepdump) {
                    AppendGListData(&pplist, "\r\n", 2);
                }
            } else {
                if (spaceskip) {
                    AppendGListByte(&pplist, ' ');
                }
            }

            while (1) {
                switch ((innertoken = token)) {
                    case '(':
                    case ')':
                    case '{':
                    case '}':
                        AppendGListByte(&pplist, token);
                        if (cprep_nostring) {
                            if (innertoken == startToken) {
                                depth++;
                            } else if (innertoken == endToken) {
                                if (--depth == 0) {
                                    cprep_nostring = FALSE;
                                    in_assembler = FALSE;
                                    copts.cplusplus = save_cplusplus;
                                    copts.asmpoundcomment = save_asmpoundcomment;
                                }
                            }
                        }
                        break;

                    case TK_INTCONST:
                    case TK_FLOATCONST:
                        if (tokenstacklevel > 0) {
                            p = macropos;
                        } else {
                            p = prep_file_start + ts_current[-1].tokenoffset;
                        }
                        AppendGListData(&pplist, p, pos - p);
                        break;

                    case TK_IDENTIFIER:
                        AppendGListData(&pplist, tkidentifier->name, strlen(tkidentifier->name));
                        break;

                    case TK_AUTO:
                        AppendGListData(&pplist, "auto", 4);
                        break;
                    case TK_REGISTER:
                        AppendGListData(&pplist, "register", 8);
                        break;
                    case TK_STATIC:
                        AppendGListData(&pplist, "static", 6);
                        break;
                    case TK_EXTERN:
                        AppendGListData(&pplist, "extern", 6);
                        break;
                    case TK_TYPEDEF:
                        AppendGListData(&pplist, "typedef", 7);
                        break;
                    case TK_INLINE:
                        AppendGListData(&pplist, "inline", 6);
                        break;
                    case TK_VOID:
                        AppendGListData(&pplist, "void", 4);
                        break;
                    case TK_CHAR:
                        AppendGListData(&pplist, "char", 4);
                        break;
                    case TK_SHORT:
                        AppendGListData(&pplist, "short", 5);
                        break;
                    case TK_INT:
                        AppendGListData(&pplist, "int", 3);
                        break;
                    case TK_LONG:
                        AppendGListData(&pplist, "long", 4);
                        break;
                    case TK_FLOAT:
                        AppendGListData(&pplist, "float", 5);
                        break;
                    case TK_DOUBLE:
                        AppendGListData(&pplist, "double", 6);
                        break;
                    case TK_SIGNED:
                        AppendGListData(&pplist, "signed", 6);
                        break;
                    case TK_UNSIGNED:
                        AppendGListData(&pplist, "unsigned", 8);
                        break;
                    case TK_STRUCT:
                        AppendGListData(&pplist, "struct", 6);
                        break;
                    case TK_UNION:
                        AppendGListData(&pplist, "union", 5);
                        break;
                    case TK_ENUM:
                        AppendGListData(&pplist, "enum", 4);
                        break;
                    case TK_CLASS:
                        AppendGListData(&pplist, "class", 5);
                        break;
                    case TK_CONST:
                        AppendGListData(&pplist, "const", 5);
                        break;
                    case TK_VOLATILE:
                        AppendGListData(&pplist, "volatile", 8);
                        break;
                    case TK_PASCAL:
                        AppendGListData(&pplist, "pascal", 6);
                        break;
                    case TK_UU_FAR:
                        AppendGListData(&pplist, "__far", 5);
                        break;
                    case TK_ONEWAY:
                        AppendGListData(&pplist, "oneway", 6);
                        break;
                    case TK_IN:
                        AppendGListData(&pplist, "in", 2);
                        break;
                    case TK_OUT:
                        AppendGListData(&pplist, "out", 3);
                        break;
                    case TK_INOUT:
                        AppendGListData(&pplist, "inout", 5);
                        break;
                    case TK_BYCOPY:
                        AppendGListData(&pplist, "bycopy", 6);
                        break;
                    case TK_BYREF:
                        AppendGListData(&pplist, "byref", 5);
                        break;
                    case TK_ASM:
                        AppendGListData(&pplist, "asm", 3);
                        startToken = endToken = 0;
                        AppendGListByte(&pplist, ' ');
                        token = lex();
                        if (token == TK_VOLATILE ||
                            (token == TK_IDENTIFIER &&
                             !strcmp(tkidentifier->name, "__volatile__"))) {
                            AppendGListData(&pplist, "volatile", 8);
                            token = lex();
                        }
                        if (token) {
                            if (token < ' ' || token > 255) {
                                continue;
                            }
                            AppendGListByte(&pplist, token);

                            if (token == '(') {
                                startToken = '(';
                                endToken = ')';
                            } else if (token == '{') {
                                startToken = '{';
                                endToken = '}';
                            } else {
                                continue;
                            }

                            cprep_nostring = TRUE;
                            in_assembler = TRUE;
                            depth = 1;
                            save_asmpoundcomment = copts.asmpoundcomment;
                            save_cplusplus = copts.cplusplus;

                            token = lex();
                            if (token == '"') {
                                AppendGListByte(&pplist, token);
                                copts.cplusplus = FALSE;
                                copts.asmpoundcomment = TRUE;
                                break;
                            }
                            if (token) {
                                continue;
                            }
                        }
                        break;

                    case TK_CASE:
                        AppendGListData(&pplist, "case", 4);
                        break;
                    case TK_DEFAULT:
                        AppendGListData(&pplist, "default", 7);
                        break;
                    case TK_IF:
                        AppendGListData(&pplist, "if", 2);
                        break;
                    case TK_ELSE:
                        AppendGListData(&pplist, "else", 4);
                        break;
                    case TK_SWITCH:
                        AppendGListData(&pplist, "switch", 6);
                        break;
                    case TK_WHILE:
                        AppendGListData(&pplist, "while", 5);
                        break;
                    case TK_DO:
                        AppendGListData(&pplist, "do", 2);
                        break;
                    case TK_FOR:
                        AppendGListData(&pplist, "for", 3);
                        break;
                    case TK_GOTO:
                        AppendGListData(&pplist, "goto", 4);
                        break;
                    case TK_CONTINUE:
                        AppendGListData(&pplist, "continue", 8);
                        break;
                    case TK_BREAK:
                        AppendGListData(&pplist, "break", 5);
                        break;
                    case TK_RETURN:
                        AppendGListData(&pplist, "return", 6);
                        break;
                    case TK_SIZEOF:
                        AppendGListData(&pplist, "sizeof", 6);
                        break;
                    case TK_CATCH:
                        AppendGListData(&pplist, "catch", 5);
                        break;
                    case TK_DELETE:
                        AppendGListData(&pplist, "delete", 6);
                        break;
                    case TK_FRIEND:
                        AppendGListData(&pplist, "friend", 6);
                        break;
                    case TK_NEW:
                        AppendGListData(&pplist, "new", 3);
                        break;
                    case TK_OPERATOR:
                        AppendGListData(&pplist, "operator", 8);
                        break;
                    case TK_PRIVATE:
                        AppendGListData(&pplist, "private", 7);
                        break;
                    case TK_PROTECTED:
                        AppendGListData(&pplist, "protected", 9);
                        break;
                    case TK_PUBLIC:
                        AppendGListData(&pplist, "public", 6);
                        break;
                    case TK_TEMPLATE:
                        AppendGListData(&pplist, "template", 8);
                        break;
                    case TK_THIS:
                        AppendGListData(&pplist, "this", 4);
                        break;
                    case TK_THROW:
                        AppendGListData(&pplist, "throw", 5);
                        break;
                    case TK_TRY:
                        AppendGListData(&pplist, "try", 3);
                        break;
                    case TK_VIRTUAL:
                        AppendGListData(&pplist, "virtual", 7);
                        break;
                    case TK_INHERITED:
                        AppendGListData(&pplist, "inherited", 9);
                        break;
                    case TK_CONST_CAST:
                        AppendGListData(&pplist, "const_cast", 10);
                        break;
                    case TK_DYNAMIC_CAST:
                        AppendGListData(&pplist, "dynamic_cast", 12);
                        break;
                    case TK_EXPLICIT:
                        AppendGListData(&pplist, "explicit", 8);
                        break;
                    case TK_MUTABLE:
                        AppendGListData(&pplist, "mutable", 7);
                        break;
                    case TK_NAMESPACE:
                        AppendGListData(&pplist, "namespace", 9);
                        break;
                    case TK_REINTERPRET_CAST:
                        AppendGListData(&pplist, "reinterpret_cast", 16);
                        break;
                    case TK_STATIC_CAST:
                        AppendGListData(&pplist, "static_cast", 11);
                        break;
                    case TK_USING:
                        AppendGListData(&pplist, "using", 5);
                        break;
                    case TK_WCHAR_T:
                        AppendGListData(&pplist, "wchar_t", 7);
                        break;
                    case TK_TYPENAME:
                        AppendGListData(&pplist, "typename", 8);
                        break;
                    case TK_TRUE:
                        AppendGListData(&pplist, "true", 4);
                        break;
                    case TK_FALSE:
                        AppendGListData(&pplist, "false", 5);
                        break;
                    case TK_TYPEID:
                        AppendGListData(&pplist, "typeid", 6);
                        break;
                    case TK_EXPORT:
                        AppendGListData(&pplist, "export", 6);
                        break;
                    case TK_UU_STDCALL:
                        AppendGListData(&pplist, "__stdcall", 9);
                        break;
                    case TK_UU_CDECL:
                        AppendGListData(&pplist, "__cdecl", 7);
                        break;
                    case TK_UU_FASTCALL:
                        AppendGListData(&pplist, "__fastcall", 10);
                        break;
                    case TK_UU_DECLSPEC:
                        AppendGListData(&pplist, "__declspec", 10);
                        break;
                    case TK_MULT_ASSIGN:
                        AppendGListData(&pplist, "*=", 2);
                        break;
                    case TK_DIV_ASSIGN:
                        AppendGListData(&pplist, "/=", 2);
                        break;
                    case TK_MOD_ASSIGN:
                        AppendGListData(&pplist, "%=", 2);
                        break;
                    case TK_ADD_ASSIGN:
                        AppendGListData(&pplist, "+=", 2);
                        break;
                    case TK_SUB_ASSIGN:
                        AppendGListData(&pplist, "-=", 2);
                        break;
                    case TK_SHL_ASSIGN:
                        AppendGListData(&pplist, "<<=", 3);
                        break;
                    case TK_SHR_ASSIGN:
                        AppendGListData(&pplist, ">>=", 3);
                        break;
                    case TK_AND_ASSIGN:
                        AppendGListData(&pplist, "&=", 2);
                        break;
                    case TK_XOR_ASSIGN:
                        AppendGListData(&pplist, "^=", 2);
                        break;
                    case TK_OR_ASSIGN:
                        AppendGListData(&pplist, "|=", 2);
                        break;
                    case TK_LOGICAL_OR:
                        AppendGListData(&pplist, "||", 2);
                        break;
                    case TK_LOGICAL_AND:
                        AppendGListData(&pplist, "&&", 2);
                        break;
                    case TK_LOGICAL_EQ:
                        AppendGListData(&pplist, "==", 2);
                        break;
                    case TK_LOGICAL_NE:
                        AppendGListData(&pplist, "!=", 2);
                        break;
                    case TK_LESS_EQUAL:
                        AppendGListData(&pplist, "<=", 2);
                        break;
                    case TK_GREATER_EQUAL:
                        AppendGListData(&pplist, ">=", 2);
                        break;
                    case TK_SHL:
                        AppendGListData(&pplist, "<<", 2);
                        break;
                    case TK_SHR:
                        AppendGListData(&pplist, ">>", 2);
                        break;
                    case TK_INCREMENT:
                        AppendGListData(&pplist, "++", 2);
                        break;
                    case TK_DECREMENT:
                        AppendGListData(&pplist, "--", 2);
                        break;
                    case TK_ARROW:
                        AppendGListData(&pplist, "->", 2);
                        break;
                    case TK_ELLIPSIS:
                        AppendGListData(&pplist, "...", 3);
                        break;
                    case TK_DOT_STAR:
                        AppendGListData(&pplist, ".*", 2);
                        break;
                    case TK_ARROW_STAR:
                        AppendGListData(&pplist, "->*", 3);
                        break;
                    case TK_COLON_COLON:
                        AppendGListData(&pplist, "::", 2);
                        break;
                    case TK_AT_INTERFACE:
                        AppendGListData(&pplist, "@interface", 10);
                        break;
                    case TK_AT_IMPLEMENTATION:
                        AppendGListData(&pplist, "@implementation", 15);
                        break;
                    case TK_AT_PROTOCOL:
                        AppendGListData(&pplist, "@protocol", 9);
                        break;
                    case TK_AT_END:
                        AppendGListData(&pplist, "@end", 4);
                        break;
                    case TK_AT_PRIVATE:
                        AppendGListData(&pplist, "@private", 8);
                        break;
                    case TK_AT_PROTECTED:
                        AppendGListData(&pplist, "@protected", 10);
                        break;
                    case TK_AT_PUBLIC:
                        AppendGListData(&pplist, "@public", 7);
                        break;
                    case TK_AT_CLASS:
                        AppendGListData(&pplist, "@class", 6);
                        break;
                    case TK_AT_SELECTOR:
                        AppendGListData(&pplist, "@selector", 9);
                        break;
                    case TK_AT_ENCODE:
                        AppendGListData(&pplist, "@encode", 7);
                        break;
                    case TK_AT_DEFS:
                        AppendGListData(&pplist, "@defs", 5);
                        break;
                    case TK_SELF:
                        AppendGListData(&pplist, "self", 4);
                        break;
                    case TK_SUPER:
                        AppendGListData(&pplist, "super", 5);
                        break;
                    case TK_BOOL:
                        if (!copts.cplusplus && copts.c9x) {
                            AppendGListData(&pplist, "_Bool", 5);
                        } else {
                            AppendGListData(&pplist, "bool", 4);
                        }
                        break;
                    case TK_RESTRICT:
                        if (copts.c9x) {
                            AppendGListData(&pplist, "restrict", 8);
                        } else {
                            AppendGListData(&pplist, "__restrict", 10);
                        }
                        break;
                    case TK_UU_VEC2X32FLOAT_UU:
                        AppendGListData(&pplist, "__vec2x32float__", 16);
                        break;
                    case TK_UU_VECTOR:
                        AppendGListData(&pplist, "__vector", 8);
                        break;
                    case TK_UU_TYPEOF_UU:
                        AppendGListData(&pplist, "__typeof__", 10);
                        break;
                    case TK_UU_ATTRIBUTE_UU:
                        AppendGListData(&pplist, "__attribute__", 13);
                        break;
                    case TK_UU_ALIGNOF_UU:
                        AppendGListData(&pplist, "__alignof__", 11);
                        break;
                    case TK_UU_UUIDOF:
                        AppendGListData(&pplist, "__uuidof", 8);
                        break;
                    case TK_U_COMPLEX:
                        AppendGListData(&pplist, "_Complex", 8);
                        break;
                    case TK_U_IMAGINARY:
                        AppendGListData(&pplist, "_Imaginary", 10);
                        break;

                    case TK_STRING:
                        if (ispascalstring) {
                            AppendGListData(&pplist, "\"\\p", 3);
                            CPrep_DumpString((UInt8*)tkstring + 1, tksize - 1);
                        } else {
                            AppendGListByte(&pplist, '"');
                            CPrep_DumpString((UInt8*)tkstring, tksize - 1);
                        }
                        AppendGListByte(&pplist, '"');
                        break;

                    case TK_STRING_WIDE:
                        AppendGListData(&pplist, "L\"", 2);
                        CPrep_DumpWString((UInt16*)tkstring, (tksize / stwchar.size) - 1);
                        AppendGListByte(&pplist, '"');
                        break;

                    default:
                        if (token >= 32 && token <= 255) {
                            AppendGListByte(&pplist, token);
                        } else {
                            CError_FATAL(606);
                        }
                }
                break;
            }

            CPrep_TokenStreamFlush();
            nlflag = FALSE;
            spaceskip = FALSE;
        } while ((token = lex()));
    }

    AppendGListByte(&pplist, 0);
    COS_ResizeHandle(pplist.data, pplist.size);
}
