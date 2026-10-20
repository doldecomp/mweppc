#include "compiler/GCCInlineAsm.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/enode.h"
#include "compiler/InlineAsm.h"
#include "compiler/objects.h"
#include "compiler/tokens.h"

Statement* first_ST_ASM;
IALookupResult gcc_name_list[20];
int gcc_name_list_index;

void InlineAsm_SkipComment(void) {
    while (1) {
        if (tk != '/') {
            break;
        }
        if (lookahead() != '*') {
            break;
        }

        tk = lex();
        while (!((tk = lex()) == '*' && (tk = lex()) == '/')) {}
        tk = lex();
    }
}

static char gcc_parse_attribute(void) {
    char ch;

    while (tk == TK_EOL) {
        tk = lex();
    }

    if (tk != '"') {
        CError_Error(ERR_ILLEGAL_TOKEN);
    }

    while ((tk = lex()) != TK_IDENTIFIER) {}

    ch = tkidentifier->name[0];

    if ((tk = lex()) != '"') {
        CError_Error(ERR_ILLEGAL_TOKEN);
    }
    tk = lex();

    return ch;
}

static void gcc_parse_name(Boolean isOutput, char attribute) {
    ENode* expr;
    IALookupResult* nameentry;
    Object* tempobj;
    ENode* tempexpr;
    Statement* stmt;

    while (tk == TK_EOL) {
        tk = lex();
    }

    if (tk != '(') {
        CError_Error(ERR_LPAREN_EXPECTED);
    }

    tk = lex();

    if (isOutput) {
        if (tk != TK_IDENTIFIER) {
            CError_Error(ERR_ILLEGAL_TOKEN);
        }

        InlineAsm_LookupSymbol(tkidentifier, &gcc_name_list[++gcc_name_list_index]);
        if (gcc_name_list[gcc_name_list_index].object &&
            gcc_name_list[gcc_name_list_index].object->u.var.info) {
            gcc_name_list[gcc_name_list_index].object->u.var.info->used = TRUE;
        }

        tk = lex();
    } else {
        in_assembler = FALSE;
        cprep_nostring = FALSE;
        nameentry = &gcc_name_list[++gcc_name_list_index];

        expr = expression();
        if (attribute == 'i' || attribute == 'I') {
            if (!ENODE_IS(expr, EINTCONST)) {
                CError_Error(ERR_ILLEGAL_OPERAND);
            }
            nameentry->value = CInt64_GetULong(&expr->data.intval);
            nameentry->has_value = TRUE;
        } else {
            tempobj = create_temp_object(expr->rtype);
            tempexpr = create_objectnode(tempobj);
            if (tempobj->u.var.info) {
                tempobj->u.var.info->used = TRUE;
            }
            expr = makediadicnode(tempexpr, expr, EASS);

            stmt = CFunc_InsertBeforeStatement(ST_EXPRESSION, first_ST_ASM);
            first_ST_ASM = stmt->next;
            if (!first_ST_ASM->next) {
                curstmt = first_ST_ASM;
            }
            stmt->expr = expr;

            nameentry->name = tempobj->name;
            nameentry->object = tempobj;
            nameentry->label = NULL;
            nameentry->type = NULL;
            nameentry->has_value = FALSE;
        }
    }

    cprep_nostring = TRUE;
    in_assembler = TRUE;

    if (tk != ')') {
        CError_Error(ERR_RPAREN_EXPECTED);
    }
    tk = lex();
}

static void gcc_parse_expression(Boolean isOutput) {
    while (1) {
        gcc_parse_name(isOutput, gcc_parse_attribute());
        if (tk != ',') {
            break;
        }
        tk = lex();
    }
}

static void gcc_parse_input(void) {
    if (tk == ':') {
        if ((tk = lex()) == ':' || tk == ')' || tk == '}') {
            return;
        }
        gcc_parse_expression(FALSE);
    }
}

static void gcc_parse_output(void) {
    if (tk == ':') {
        if ((tk = lex()) == ':' || tk == ')' || tk == '}') {
            return;
        }
        gcc_parse_expression(TRUE);
    }
}

static inline void gcc_parse_killed(void) {
    if (tk == ':') {
        while (1) {
            if ((tk = lex()) != '"') {
                return;
            }

            tk = lex();
            while (1) {
                if (tk == '"') {
                    if (lookahead() == ',') {
                        tk = lex();
                        break;
                    }
                    tk = lex();
                    return;
                }
                tk = lex();
            }
        }
    }
}

static void gcc_replace_arg_st_asm(Statement* stmt) {
    InlineAsm* ia;
    int i;
    IAOperand* op;
    short effect;
    short rclass;
    SInt32 num;

    if ((ia = (InlineAsm*)stmt->expr)) {
        for (i = 0, op = ia->args; i < ia->argcount; i++, op++) {
            switch (op->type) {
                case IAOpnd_Imm:
                case IAOpnd_Reg:
                case IAOpnd_GlobalObj:
                case IAOpnd_LocalObj:
                case IAOpnd_Lab:
                    break;

                case IAOpnd_GCCReg:
                    if (op->u.gccreg.kind == 2) {
                        effect = op->u.gccreg.effect;
                        rclass = op->u.gccreg.rclass;
                        num = op->u.gccreg.num;
                        op->type = IAOpnd_Reg;
                        op->u.reg.effect = effect;
                        op->u.reg.rclass = rclass;
                        op->u.reg.object = NULL;
                        if (num <= gcc_name_list_index) {
                            op->u.reg.object = gcc_name_list[num].object;
                        } else {
                            CError_Error(ERR_ILLEGAL_OPERAND);
                        }
                        op->u.reg.num = 0;
                    } else {
                        CError_FATAL(365);
                    }
                    break;

                case IAOpnd_GCCImm:
                    op->type = IAOpnd_Imm;
                    op->u.imm.value = gcc_name_list[op->u.gccimm.value].value;
                    break;
                default:
                    break;
            }
        }
    }
}

static void gcc_replace_arg(void) {
    Statement* stmt;

    for (stmt = first_ST_ASM; stmt; stmt = stmt->next) {
        if (stmt->type == ST_ASM) {
            gcc_replace_arg_st_asm(stmt);
        }
    }
}

void InlineAsm_gcc_parse(void) {
    gcc_name_list_index = -1;
    cprep_eoltokens = FALSE;

    if (tk == TK_EOL) {
        tk = lex();
    }

    gcc_parse_output();
    gcc_parse_input();
    gcc_parse_killed();
    gcc_replace_arg();
}
