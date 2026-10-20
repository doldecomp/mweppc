#include "compiler/InlineAsmPPC.h"
#include "compiler/Alias.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/FuncLevelAsmPPC.h"
#include "compiler/InlineAsm.h"
#include "compiler/InlineAsmMnemonicsPPC.h"
#include "compiler/InlineAsmRegisters.h"
#include "compiler/InlineAsmRegistersPPC.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/PCode.h"
#include "compiler/PCodeInfo.h"
#include "compiler/PCodeUtilities.h"
#include "compiler/PPCError.h"
#include "compiler/RegisterInfo.h"
#include "compiler/Registers.h"
#include "compiler/StackFrame.h"
#include "compiler/TOC.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "driver/CodeGenOptPPC.h"
#include <ctype.h>
#include <stddef.h>
#include <string.h>

char asm_alloc_flags[10];
UInt16 sm_section;
UInt32 cpu;
SInt32 fralloc_parameter_area_size;
Boolean user_responsible_for_frame;
Boolean supports_hardware_fpu;
UInt32 assembledinstructions;
AssemblerType assembler_type;
char volatileasm;
Boolean InlineAsm_gccmode;
Boolean InlineAsm_labelref;
// NOTE: invented names: warn_def_reserved_reg (the directive's), asm_volatile
// `warn_def_reserved_reg on|off`
static Boolean warn_def_reserved_reg;
// `volatile`, `nonvolatile` and `scheduling off|on`; starts as volatileasm
static Boolean asm_volatile;
// Nothing uses it: the EABI has no PIC base
static CLabel* pic_base_label;

#define OPCODE_PART_1(opcode) ((opcode) >> 26)
#define OPCODE_PART_2(opcode) (((opcode) >> 21) & 0x1F)
#define OPCODE_PART_3(opcode) (((opcode) >> 1) & 0x3FF)

static SInt32 InlineAsm_ConstantExpressionPPC(SInt32 value);
static Object* isvariableoperand(void);
static Object* isregisterstructpointeroperand(void);
static void registeroperand(IAOperand* op, char rclass, int effect);
static void DiadicOperatorPPC(IAExpr* left, short token, IAExpr* right);
static void InlineAsm_ExpressionPPC(IAExpr* expr, SInt32 value);

inline SInt32 ExtractValue(CInt64 value) {
    return (SInt32)CInt64_GetULong(&value);
}

static void IllegalObjectOperator(HashNameNode* name1, HashNameNode* name2, short token) {
    char* opstr;
    switch (token) {
        case '*':
            opstr = "*";
            break;
        case '/':
            opstr = "/";
            break;
        case '%':
            opstr = "%";
            break;
        case '+':
            opstr = "+";
            break;
        case '-':
            opstr = "-";
            break;
        case TK_SHL:
            opstr = "<<";
            break;
        case TK_SHR:
            opstr = ">>";
            break;
        case '<':
            opstr = "<";
            break;
        case '>':
            opstr = ">";
            break;
        // the original's bug: these two strings are swapped
        case TK_LESS_EQUAL:
            opstr = ">=";
            break;
        case TK_GREATER_EQUAL:
            opstr = "<=";
            break;
        case TK_LOGICAL_EQ:
            opstr = "==";
            break;
        case TK_LOGICAL_NE:
            opstr = "!=";
            break;
        case '&':
            opstr = "&";
            break;
        case '^':
            opstr = "^";
            break;
        case '|':
            opstr = "|";
            break;
        case TK_LOGICAL_AND:
            opstr = "&&";
            break;
        case TK_LOGICAL_OR:
            opstr = "||";
            break;
        default:
            opstr = "?\?\?";
            break;
    }

    if (name2 == NULL) {
        PPCError_Error(PPCERR_ASM_ONLY_OBJECT_PLUS_CONSTANT, opstr, name1->name);
    } else if (name1 == NULL) {
        PPCError_Error(PPCERR_ASM_ONLY_CONSTANT_PLUS_OBJECT, opstr, name2->name);
    } else {
        PPCError_Error(PPCERR_ASM_ONLY_OBJECT_DIFFERENCE, name1->name, opstr, name2->name);
    }
}

static void IllegalObjectInConst(IAExpr* expr) {
    if (expr->var) {
        PPCError_Error(PPCERR_ASM_OBJECT_IN_CONSTANT, expr->var->name->name);
    } else if (expr->object) {
        PPCError_Error(PPCERR_ASM_OBJECT_IN_CONSTANT, expr->object->name->name);
    } else if (expr->label) {
        PPCError_Error(PPCERR_ASM_UNDEFINED_SYMBOL, expr->label->name->name);
    }
}

static void NotInRegisterError(char* name, char rclass) {
    PPCError_Error(PPCERR_ASM_NOT_IN_REGISTER, name, register_class_name[rclass]);
}

static int isregisteroperand(char rclass) {
    IARegister* reg;

    return (
        tk == TK_IDENTIFIER &&
        (reg = InlineAsm_LookupRegisterPPCName(tkidentifier)) &&
        reg->rclass == rclass
    );
}

static SInt32 getcroperand(char rclass) {
    SInt32 value;
    IARegister* reg;

    value = 0;
    if (tk == TK_IDENTIFIER &&
        (reg = InlineAsm_LookupRegisterPPCName(tkidentifier)) &&
        reg->rclass == rclass) {
        value = reg->num;
    } else {
        PPCError_Error(PPCERR_ASM_NOT_IN_REGISTER, tkidentifier->name);
    }

    tk = lex();
    return value;
}

static SInt32 getimmediateoperand(SInt32 minimum, SInt32 maximum) {
    SInt32 value = InlineAsm_ConstantExpressionPPC(0);
    if (value < minimum || value > maximum) {
        CError_Error(ERR_NUMBER_OUT_RANGE);
    }
    return value;
}

static void PrimaryExpressionPPC(IAExpr* expr, SInt32 value) {
    Object* obj;
    IALookupResult result;
    IAExpr subexpr;
    IAExprType type = IAExpr_None;
    SInt32 v;

    switch (tk) {
        case TK_IDENTIFIER:
            if ((obj = isregisterstructpointeroperand())) {
                tk = lex();
                InlineAsm_InitExpr5(
                    expr, InlineAsm_StructPointerMemberOffset(TPTR_TARGET(obj->type))
                );
                expr->object = obj;
                return;
            }

            if ((obj = isvariableoperand())) {
                short reg;
                InlineAsm_InitExpr5(expr, 0);
                expr->var = obj;

                if (obj->datatype == DLOCAL) {
                    reg = 1;
                } else {
                    reg = 0;
                }

                tk = lex();
                if (tk == '.' || tk == '[') {
                    InlineAsm_InitExpr5(&subexpr, InlineAsm_StructArrayMemberOffset(obj->type));
                    DiadicOperatorPPC(expr, '+', &subexpr);
                }

                if (tk == '+') {
                    tk = lex();
                    InlineAsm_ExpressionPPC(&subexpr, value);
                    DiadicOperatorPPC(expr, '+', &subexpr);
                } else if (tk == '-') {
                    tk = lex();
                    InlineAsm_ExpressionPPC(&subexpr, value);
                    DiadicOperatorPPC(expr, '-', &subexpr);
                } else if (!strcmp(tkidentifier->name, "@loword")) {
                    tk = lex();
                    if (low_offset) {
                        InlineAsm_InitExpr5(&subexpr, low_offset);
                        DiadicOperatorPPC(expr, '+', &subexpr);
                    }
                } else if (!strcmp(tkidentifier->name, "@hiword")) {
                    tk = lex();
                    if (high_offset) {
                        InlineAsm_InitExpr5(&subexpr, high_offset);
                        DiadicOperatorPPC(expr, '+', &subexpr);
                    }
                }
                expr->reg = reg;
                return;
            }

            if (InlineAsm_LookupSymbolOrTag(tkidentifier, &result, TRUE)) {
                if (result.has_value) {
                    tk = lex();
                    InlineAsm_InitExpr5(expr, result.value);
                    return;
                }

                if (result.type && (IS_TYPE_STRUCT(result.type) || IS_TYPE_CLASS(result.type))) {
                    tk = lex();
                    if (tk != '.') {
                        CError_Error(ERR_UNEXPECTED_TOKEN);
                    }

                    if (allow_array_expressions) {
                        InlineAsm_InitExpr5(expr, InlineAsm_StructArrayMemberOffset(result.type));
                        return;
                    }

                    InlineAsm_InitExpr5(expr, InlineAsm_StructMemberOffset(result.type));
                    return;
                }

                if (result.object && result.object->datatype == DABSOLUTE) {
                    tk = lex();
                    InlineAsm_InitExpr5(expr, result.object->u.data.u.intconst.hi);
                    return;
                }
            } else if (value) {
                if (isregisteroperand(RegClass_CRFIELD)) {
                    InlineAsm_InitExpr5(expr, getcroperand(RegClass_CRFIELD));
                    return;
                }
                if (isregisteroperand(RegClass_CRBIT)) {
                    InlineAsm_InitExpr5(expr, getcroperand(RegClass_CRBIT));
                    return;
                }
                if (strlen(tkidentifier->name) == 6 &&
                    !strncmp(tkidentifier->name, "cr", 2) &&
                    tkidentifier->name[3] == '_') {
                    IARegister* reg;
                    char regname[4];
                    regname[0] = tkidentifier->name[0];
                    regname[1] = tkidentifier->name[1];
                    regname[2] = tkidentifier->name[2];
                    regname[3] = 0;
                    if ((reg = InlineAsm_LookupRegisterPPC(regname)) &&
                        reg->rclass == RegClass_CRFIELD) {
                        v = reg->num * 4;
                        regname[0] = tkidentifier->name[4];
                        regname[1] = tkidentifier->name[5];
                        regname[2] = 0;
                        if ((reg = InlineAsm_LookupRegisterPPC(regname)) &&
                            reg->rclass == RegClass_CRBIT) {
                            tk = lex();
                            InlineAsm_InitExpr5(expr, v + reg->num);
                            return;
                        }
                    }
                }
            }

            if (!strcmp("ha16", tkidentifier->name)) {
                type = IAExpr_Ha;
            } else if (!strcmp("hi16", tkidentifier->name)) {
                type = IAExpr_Hi;
            } else if (!strcmp("lo16", tkidentifier->name)) {
                type = IAExpr_Lo;
            }

            if (type != IAExpr_None) {
                tk = lex();
                if (tk == '(') {
                    tk = lex();
                    PrimaryExpressionPPC(expr, value);
                    expr->type = type;
                    if (tk != ')') {
                        CError_Error(ERR_RPAREN_EXPECTED);
                    }

                    tk = lex();
                    if (InlineAsm_CheckExpr(expr)) {
                        expr->value = InlineAsm_GetExprValue(expr);
                        expr->type = IAExpr_Value;
                    }
                    return;
                }

                CError_Error(ERR_LPAREN_EXPECTED);
            } else {
                if (!result.label) {
                    result.label = InlineAsm_DeclareLabel(tkidentifier);
                }

                InlineAsm_InitExpr5(expr, 0);
                expr->flags |= IAFlag_Directive;
                expr->label = result.label;
                tk = lex();
                return;
            }
            break;

        case TK_INTCONST:
            v = CInt64_GetULong(&tkintconst);
            tk = lex();
            InlineAsm_InitExpr5(expr, v);
            return;

        case TK_SIZEOF:
            InlineAsm_InitExpr5(expr, scansizeof());
            return;

        case '+':
            tk = lex();
            PrimaryExpressionPPC(expr, value);
            return;

        case '-':
            tk = lex();
            PrimaryExpressionPPC(expr, value);
            if (InlineAsm_CheckExpr(expr)) {
                expr->value = -expr->value;
            } else {
                CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
            }
            return;

        case '!':
            tk = lex();
            PrimaryExpressionPPC(expr, value);
            if (InlineAsm_CheckExpr(expr)) {
                expr->value = !expr->value;
            } else {
                CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
            }
            return;

        case '~':
            tk = lex();
            PrimaryExpressionPPC(expr, value);
            if (InlineAsm_CheckExpr(expr)) {
                expr->value = ~expr->value;
            } else {
                CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
            }
            return;

        case '(':
            tk = lex();
            InlineAsm_ExpressionPPC(expr, value);
            if (tk != ')') {
                CError_Error(ERR_RPAREN_EXPECTED);
            }
            tk = lex();
            return;

        default:
            CError_Error(ERR_UNEXPECTED_TOKEN);
    }

    InlineAsm_InitExpr5(expr, 0);
}

static void DiadicOperatorPPC(IAExpr* left, short token, IAExpr* right) {
    CInt64 leftval;
    CInt64 rightval;

    CInt64_SetLong(&leftval, left->value);
    CInt64_SetLong(&rightval, right->value);
    rightval = CMach_CalcIntDiadic(TYPE(&stsignedint), leftval, token, rightval);

    if (left->var) {
        if (right->label) {
            PPCError_Error(
                PPCERR_ASM_LABEL_AND_OBJECT, left->var->name->name, right->label->name->name
            );
        }

        if (right->var) {
            if (left->var2) {
                PPCError_Error(
                    PPCERR_ASM_TOO_MANY_OBJECTS,
                    left->var->name->name,
                    left->var2->name->name,
                    right->var->name->name
                );
            } else if (right->var2) {
                PPCError_Error(
                    PPCERR_ASM_TOO_MANY_OBJECTS,
                    left->var->name->name,
                    right->var->name->name,
                    right->var2->name->name
                );
            } else if (token == '-') {
                left->value = CInt64_GetULong(&rightval);
                left->var2 = right->var;
            } else {
                IllegalObjectOperator(left->var->name, right->var->name, token);
            }
        } else if (token == '-' || token == '+') {
            left->value = CInt64_GetULong(&rightval);
        } else {
            IllegalObjectOperator(left->var->name, NULL, token);
        }
    } else if (right->var) {
        if (right->label) {
            PPCError_Error(
                PPCERR_ASM_LABEL_AND_OBJECT, right->var->name->name, right->label->name->name
            );
        }

        if (token == '+') {
            left->var = right->var;
            left->var2 = right->var2;
            left->value = CInt64_GetULong(&rightval);
        } else {
            IllegalObjectOperator(NULL, right->var->name, token);
        }
    } else if (left->label) {
        if (left->var) {
            PPCError_Error(
                PPCERR_ASM_LABEL_AND_OBJECT, left->label->name->name, left->var->name->name
            );
        }

        if (right->label) {
            if (left->label2) {
                PPCError_Error(
                    PPCERR_ASM_TOO_MANY_OBJECTS,
                    left->label->name->name,
                    left->label2->name->name,
                    right->label->name->name
                );
            } else if (right->label2) {
                PPCError_Error(
                    PPCERR_ASM_TOO_MANY_OBJECTS,
                    left->label->name->name,
                    right->label->name->name,
                    right->label2->name->name
                );
            } else if (token == '-') {
                left->value = CInt64_GetULong(&rightval);
                left->label2 = right->label;
            } else {
                IllegalObjectOperator(left->label->name, right->label->name, token);
            }
        } else if (token == '+' || token == '-') {
            left->value = CInt64_GetULong(&rightval);
        } else {
            IllegalObjectOperator(NULL, left->label->name, token);
        }
    } else if (right->label) {
        if (token == '+') {
            left->label = right->label;
            left->label2 = right->label2;
            left->value = CInt64_GetULong(&rightval);
        } else {
            IllegalObjectOperator(NULL, right->label->name, token);
        }
    } else {
        left->value = CInt64_GetULong(&rightval);
    }

    left->flags |= right->flags;
    if (left->type == IAExpr_Value) {
        if (right->type != IAExpr_Value) {
            left->type = right->type;
        }
    } else {
        if (right->type != IAExpr_Value && right->type != left->type) {
            PPCError_Error(PPCERR_ASM_MULTIPLE_RELOCATIONS);
        }
    }
}

static void ExpressionTailPPC(IAExpr* left, SInt32 value) {
    IAExpr right;
    short left_tk;
    short right_prec;

    while (1) {
        left_tk = tk;
        tk = lex();

        PrimaryExpressionPPC(&right, value);
        right_prec = GetPrec(tk);
        if (right_prec == 0) {
            DiadicOperatorPPC(left, left_tk, &right);
            return;
        }

        if (GetPrec(left_tk) >= right_prec) {
            DiadicOperatorPPC(left, left_tk, &right);
        } else {
            ExpressionTailPPC(&right, value);
            DiadicOperatorPPC(left, left_tk, &right);
            if (GetPrec(tk) == 0) {
                return;
            }
        }
    }
}

static void immediateoperand(IAOperand* op, SInt32 minval, SInt32 maxval) {
    // After the error for a `%` without a number, `value` is stored unset (the original's bug)
    SInt32 value;
    if (InlineAsm_gccmode && tk == '%') {
        tk = lex();
        if (tk != TK_INTCONST) {
            CError_Error(ERR_ILLEGAL_OPERAND);
        } else {
            CInt64 c = tkintconst;
            value = CInt64_GetULong(&c);
            if (value < 0) {
                CError_Error(ERR_ILLEGAL_OPERAND);
            }
        }
        op->type = IAOpnd_GCCImm;
        op->u.gccimm.value = value;
        op->u.gccimm.kind = TRUE;
        op->u.gccimm.effect = 0;
        op->u.gccimm.rclass = 0;
        tk = lex();
        return;
    } else {
        value = InlineAsm_ConstantExpressionPPC(0);
        if (value < minval || value > maxval) {
            CError_Error(ERR_NUMBER_OUT_RANGE);
        }
        op->type = IAOpnd_Imm;
        op->u.imm.value = value;
    }
}

static SInt32 immediatevalue(IAOperand* op, SInt32 minval, SInt32 maxval) {
    SInt32 value;
    if (InlineAsm_gccmode && tk == '%') {
        CError_Error(ERR_ILLEGAL_OPERAND);
        return 0;
    } else {
        value = InlineAsm_ConstantExpressionPPC(0);
        if (value < minval || value > maxval) {
            CError_Error(ERR_NUMBER_OUT_RANGE);
        }
        op->type = IAOpnd_Imm;
        op->u.imm.value = value;
        return value;
    }
}

static void InlineAsm_ExpressionPPC(IAExpr* expr, SInt32 value) {
    PrimaryExpressionPPC(expr, value);
    if (GetPrec(tk)) {
        ExpressionTailPPC(expr, value);
    }
    if (GetPrec(tk)) {
        ExpressionTailPPC(expr, value);
    }

    if (expr->type == IAExpr_Value && tk == TK_IDENTIFIER) {
        if (!strcmp(tkidentifier->name, "@l")) {
            expr->type = IAExpr_Lo;
            tk = lex();
        } else if (!strcmp(tkidentifier->name, "@ha")) {
            expr->type = IAExpr_Ha;
            tk = lex();
        } else if (!strcmp(tkidentifier->name, "@h")) {
            expr->type = IAExpr_Hi;
            tk = lex();
        }
    }
}

static SInt32 InlineAsm_ConstantExpressionPPC(SInt32 value) {
    IAExpr expr;

    InlineAsm_ExpressionPPC(&expr, value);
    if (!InlineAsm_CheckExpr(&expr)) {
        IllegalObjectInConst(&expr);
        return 0;
    } else {
        return InlineAsm_GetExprValue(&expr);
    }
}

static SInt32 crbitoperand(void) {
    SInt32 value = InlineAsm_ConstantExpressionPPC(1);
    if (value < 0 || value > 31) {
        CError_Error(ERR_NUMBER_OUT_RANGE);
    }
    return value;
}

static void floatoperand(IAOperand* op, InlineAsm* ia, Type* type) {
    Object* obj;

    obj = createfloatconstant(type, &tkfloatconst);
    op[0].type = IAOpnd_Reg;
    op[0].u.reg.rclass = RegClass_GPR;
    op[0].u.reg.object = NULL;
    op[0].u.reg.effect = EffectRead;
    op[0].u.reg.num = 0;
    op[0].u.reg.effect = 0;

    ia->argcount++;
    op[1].type = IAOpnd_GlobalObj;
    op[1].u.obj.obj = obj;
    op[1].u.obj.reftype = IAExpr_Addr16;
    op[1].u.obj.offset = 0;
    tk = lex();
}

static inline Object* isvariableoperand(void) {
    IALookupResult result;
    Object* obj;

    if (tk == TK_IDENTIFIER) {
        InlineAsm_LookupSymbol(tkidentifier, &result);
        if ((obj = result.object)) {
            PPC_EABI_SetSection(obj, obj->type->size, FALSE, FALSE);
            if (obj->datatype == DLOCAL) {
                if (OBJECT_REG(obj)) {
                    obj = NULL;
                } else {
                    return obj;
                }
            } else if (!(obj->datatype == DFUNC || obj->datatype == DVFUNC)) {
                if (obj->datatype != DDATA && !is_16bitdata(obj)) {
                    return NULL;
                }
                createIndirect(obj, FALSE, FALSE);
            }
            return obj;
        }
    }

    return NULL;
}

static Object* isregisterstructpointeroperand(void) {
    IALookupResult result;
    Object* obj;

    if (tk != TK_IDENTIFIER) {
        return NULL;
    }

    InlineAsm_LookupSymbol(tkidentifier, &result);
    if ((obj = result.object)) {
        if (obj->datatype != DLOCAL) {
            return NULL;
        }
        if (!OBJECT_REG(obj) && !is_register_object(obj)) {
            return NULL;
        }
        if (!IS_TYPE_POINTER(obj->type)) {
            return NULL;
        }
        if (!IS_TYPE_STRUCT(TPTR_TARGET(obj->type)) && !IS_TYPE_CLASS(TPTR_TARGET(obj->type))) {
            return NULL;
        }
        return obj;
    }

    return NULL;
}

// NOTE: invented names: op, ia, allowBaseReg, effect
static void memoryoperand(IAOperand* op, InlineAsm* ia, Boolean allowBaseReg, int effect) {
    IAExpr expr;

    InlineAsm_ExpressionPPC(&expr, 0);
    if (expr.object) {
        if (!allowBaseReg) {
            CError_Error(ERR_ILLEGAL_ADDRESSING_MODE);
        }
        if (expr.var) {
            // the original's bug: the Object, not its name's string, for the message's %u
            PPCError_Error(PPCERR_ASM_OBJECT_IN_CONSTANT, expr.var);
        }

        op[0].type = IAOpnd_Reg;
        op[0].u.reg.rclass = RegClass_GPR;
        op[0].u.reg.object = expr.object;
        op[0].u.reg.num = 0;
        op[0].u.reg.effect = effect;
        if (expr.object || expr.reg) {
            op[0].u.reg.effect = effect;
        } else {
            op[0].u.reg.effect = 0;
        }

        ia->argcount++;
        op[1].type = IAOpnd_Imm;
        switch (expr.type) {
            case IAExpr_Value:
                expr.type = IAExpr_Addr16;
            case IAExpr_Addr16:
            case IAExpr_Lo:
            case IAExpr_Hi:
            case IAExpr_Ha:
                op[1].u.imm.value = InlineAsm_GetExprValue(&expr);
                break;
            default:
                CError_Error(ERR_ILLEGAL_ADDRESSING_MODE);
        }

        expr.object->flags |= OBJECT_USED;
        return;
    }

    if (expr.var) {
        if (expr.var2) {
            PPCError_Error(
                PPCERR_ASM_ILLEGAL_OBJECT_DIFFERENCE, expr.var->name->name, expr.var2->name->name
            );
        }

        if (allowBaseReg) {
            if (tk == '(') {
                if (allowBaseReg) {
                    tk = lex();
                    registeroperand(op, RegClass_GPR, effect);
                    if (tk != ')') {
                        CError_Error(ERR_RPAREN_EXPECTED);
                    }
                    tk = lex();
                }
            } else {
                op[0].type = IAOpnd_Reg;
                op[0].u.reg.rclass = RegClass_GPR;
                op[0].u.reg.object = expr.object;
                op[0].u.reg.num = expr.reg;
            }

            if (expr.object || expr.reg) {
                op[0].u.reg.effect = effect;
            } else {
                op[0].u.reg.effect = 0;
            }

            op++;
            ia->argcount++;
        }

        if (expr.var->datatype == DLOCAL) {
            op[0].type = IAOpnd_LocalObj;
            op[0].u.obj.reftype = IAExpr_Local16;
        } else {
            op[0].type = IAOpnd_GlobalObj;
            if (expr.type == IAExpr_Value) {
                expr.type = IAExpr_Addr16;
            }
            op[0].u.obj.reftype = expr.type;
        }

        op[0].u.obj.obj = expr.var;
        op[0].u.obj.offset = expr.value;
        expr.var->flags |= OBJECT_USED;
        return;
    }

    if (allowBaseReg) {
        if (tk == '(') {
            tk = lex();
            registeroperand(op, RegClass_GPR, effect);
            op++;
            ia->argcount++;
            if (tk != ')') {
                CError_Error(ERR_RPAREN_EXPECTED);
            }
            tk = lex();
        } else {
            op->type = IAOpnd_Reg;
            op->u.reg.rclass = RegClass_GPR;
            op->u.reg.object = expr.object;
            op->u.reg.num = expr.reg;
            op++;
            ia->argcount++;
        }
    }

    if (expr.label) {
        if (expr.label2) {
            op->type = IAOpnd_LabDiff;
            op->negated = FALSE;
            op->u.labdiff.label1 = expr.label;
            op->u.labdiff.label2 = expr.label2;
            op->u.labdiff.offset = expr.value;
        } else {
            PPCError_Error(PPCERR_ASM_ILLEGAL_LABEL, expr.label->name->name);
        }
    } else {
        op->type = IAOpnd_Imm;
        op->u.imm.value = InlineAsm_GetExprValue(&expr);
    }
}

// a Gekko quantized load or store's d(b): a 12-bit displacement, then the base register
// NOTE: invented name
static IAOperand* parse_displacement_operand(IAOperand* op, InlineAsm* ia, int effect) {
    IAExpr expr;
    SInt32 value;

    InlineAsm_ExpressionPPC(&expr, 0);
    if (!InlineAsm_CheckExpr(&expr)) {
        IllegalObjectInConst(&expr);
        value = 0;
    } else {
        value = InlineAsm_GetExprValue(&expr);
    }
    if (value < -0x800 || value > 0x7FF) {
        CError_Error(ERR_NUMBER_OUT_RANGE);
    }

    if (tk == '(') {
        tk = lex();
        registeroperand(op, RegClass_GPR, effect);
        if (op->u.reg.object || op->u.reg.num) {
            op->u.reg.effect = effect;
        } else {
            op->u.reg.effect = 0;
        }
        op++;
        ia->argcount++;
        if (tk != ')') {
            CError_Error(ERR_RPAREN_EXPECTED);
        }
        tk = lex();
    } else {
        CError_Error(ERR_LPAREN_EXPECTED);
    }

    op->type = IAOpnd_Imm;
    op->u.imm.value = value;
    return op;
}

static void registeroperand(IAOperand* op, char rclass, int effect) {
    IARegister* reg;
    Object* obj;

    if (tk == TK_IDENTIFIER &&
        (reg = InlineAsm_LookupRegisterPPCName(tkidentifier)) &&
        reg->rclass == rclass) {
        SInt32 num;
        obj = reg->object;
        num = reg->num;
        op->type = IAOpnd_Reg;
        op->u.reg.rclass = rclass;
        op->u.reg.object = obj;
        op->u.reg.num = num;
        op->u.reg.effect = effect;
        if (obj) {
            reg->object->flags |= OBJECT_USED;
            Registers_GetVarInfo(obj)->flags |= VarInfoFlag_AsmNamed;
        }
    } else if (rclass == RegClass_CRFIELD) {
        op->type = IAOpnd_Reg;
        op->u.reg.rclass = RegClass_CRFIELD;
        op->u.reg.object = NULL;
        op->u.reg.num = getimmediateoperand(0, 7);
        op->u.reg.effect = effect;
        return;
    } else if (rclass == RegClass_SPR) {
        op->type = IAOpnd_Reg;
        op->u.reg.rclass = RegClass_SPR;
        op->u.reg.object = NULL;
        op->u.reg.num = getimmediateoperand(0, 1024);
        op->u.reg.effect = effect;
        return;
    } else if (tk == '%' && InlineAsm_gccmode) {
        // After the error for a `%` without a number, `num` is stored unset (the original's bug)
        SInt32 num;
        tk = lex();
        if (tk != TK_INTCONST) {
            CError_Error(ERR_ILLEGAL_OPERAND);
        } else {
            num = ExtractValue(tkintconst);
            if (num < 0) {
                CError_Error(ERR_ILLEGAL_OPERAND);
            }
        }

        op->type = IAOpnd_GCCReg;
        op->u.gccreg.num = num;
        op->u.gccreg.kind = 2;
        op->u.gccreg.effect = effect;
        op->u.gccreg.rclass = rclass;
        tk = lex();
        return;
    } else if (tk == TK_IDENTIFIER) {
        NotInRegisterError(tkidentifier->name, rclass);
    } else {
        PPCError_Error(PPCERR_ASM_EXPECTED_REGISTER);
    }

    // After the errors above `reg` is NULL, another class's register or (no identifier) unset, and
    // the tests below read through it (the original's)
    tk = lex();
    if (!strcmp(tkidentifier->name, "@loword")) {
        tk = lex();
        return;
    }
    if (!strcmp(tkidentifier->name, "@hiword")) {
        if (reg->object) {
            op->u.reg.num = -3;
        } else {
            PPCError_Error(PPCERR_ASM_ILLEGAL_HIWORD);
        }
        tk = lex();
        return;
    }
    // An 8-byte variable in a GPR must be one held in a GPR pair: IT_17 and IT_18 (CMachine's
    // custom types of tokens 0x113 and 0x114) or a paired single
    if (rclass == RegClass_GPR) {
        if (reg->object && reg->object->type->size == 8) {
            Type* type = reg->object->type;
            if (!(IS_TYPE_INT(type) && TYPE_INTEGRAL(type)->integral == IT_17) &&
                !(IS_TYPE_INT(type) && TYPE_INTEGRAL(type)->integral == IT_18) &&
                !IS_TYPE_VEC2X32FLOAT(type)) {
                HashNameNode* name = reg->object->name;
                PPCError_Error(PPCERR_ASM_REGISTER_PAIR, name->name, name->name, name->name);
            }
        }
    }
}

static void cr0_operand(IAOperand* op, short effect) {
    IARegister* reg;

    if ((reg = InlineAsm_LookupRegisterPPC("cr0"))) {
        op->type = IAOpnd_Reg;
        op->u.reg.rclass = RegClass_CRFIELD;
        op->u.reg.object = reg->object;
        op->u.reg.num = reg->num;
        op->u.reg.effect = effect;
    } else {
        NotInRegisterError("cr0", RegClass_CRFIELD);
    }
}

static void r0_operand(IAOperand* op) {
    IARegister* reg;

    if ((reg = InlineAsm_LookupRegisterPPC("r0"))) {
        op->type = IAOpnd_Reg;
        op->u.reg.rclass = RegClass_GPR;
        op->u.reg.object = reg->object;
        op->u.reg.num = reg->num;
        op->u.reg.effect = 0;
        tk = lex();
    } else {
        NotInRegisterError("r0", RegClass_GPR);
    }
}

// NOTE: invented names: ia, op, isUncondBranch, isAbsolute, isLink
static void labeloperand(
    InlineAsm* ia, IAOperand* op, Boolean isUncondBranch, Boolean isAbsolute, Boolean isLink
) {
    IALookupResult result;

    if (tk == TK_IDENTIFIER) {
        if (!InlineAsm_LookupSymbol(tkidentifier, &result)) {
            result.label = InlineAsm_DeclareLabel(tkidentifier);
        }

        if (result.label) {
            op->type = IAOpnd_Lab;
            op->u.lab.label = result.label;
        } else if (result.object && result.object->datatype == DFUNC) {
            if (isLink) {
                ia->flags |= IAFlag_Call;
            }
            op->type = IAOpnd_GlobalObj;
            op->u.obj.obj = result.object;
            op->u.obj.offset = 0;

            if (isUncondBranch) {
                op->u.obj.reftype = IAExpr_Branch;
                if (isAbsolute) {
                    op->u.obj.reftype = IAExpr_AbsBranch;
                }
            } else {
                op->u.obj.reftype = IAExpr_Addr16;
                if (isAbsolute) {
                    op->u.obj.reftype = IAExpr_LoAlt;
                }
            }
        } else {
            CError_Error(ERR_ILLEGAL_OPERAND);
        }

        tk = lex();
    } else if (tk == '*') {
        if (!isAbsolute) {
            tk = lex();
            if (tk == '+') {
                tk = lex();
                if (isUncondBranch) {
                    immediateoperand(op, -0x2000000, 0x1FFFFFF);
                } else {
                    immediateoperand(op, -0x8000, 0xFFFF);
                }
            } else if (tk == '-') {
                tk = lex();
                op->u.imm.value = isUncondBranch ? -immediatevalue(op, -0x1FFFFFF, 0x2000000)
                                                 : -immediatevalue(op, -0x7FFF, 0x10000);
            } else {
                CError_Error(ERR_ILLEGAL_OPERAND);
            }
        } else {
            CError_Error(ERR_ILLEGAL_OPERAND);
        }
    } else {
        if (isAbsolute) {
            if (isUncondBranch) {
                immediateoperand(op, -0x2000000, 0x1FFFFFF);
            } else {
                immediateoperand(op, -0x8000, 0xFFFF);
            }
        } else {
            CError_Error(ERR_ILLEGAL_OPERAND);
        }
    }
}

static void imm_or_labeldiff_operand(
    InlineAsm* ia, IAOperand* op, SInt32 minimum, SInt32 maximum, Boolean negate
) {
    IAExpr expr;
    Object* obj;
    SInt32 value;

    InlineAsm_ExpressionPPC(&expr, 0);

    if ((obj = expr.var) &&
        !expr.var2 &&
        expr.type == IAExpr_Value &&
        obj->datatype == DDATA &&
        IS_TYPE_INT_OR_ENUM(obj->type) &&
        (obj->qual & Q_INLINE_DATA)) {
        switch (ia->opcode) {
            case PC_ADDI:
            case PC_ADDIC:
            case PC_ADDICR:
            case PC_ADDIS:
            case PC_ORI:
            case PC_ORIS:
                if (ia->args[1].u.imm.value &&
                    ia->args[1].u.imm.value == (short)base_register(obj)) {
                    break;
                }
            case PC_LI:
            case PC_LIS:
            case PC_TWI:
            case PC_OPWORD:
                InlineAsm_InitExpr5(&expr, expr.value + CInt64_GetULong(&obj->u.data.u.intconst));
                break;
            default:
                break;
        }
    }

    if (expr.label) {
        if (expr.label2) {
            op->type = IAOpnd_LabDiff;
            if (negate) {
                op->negated = TRUE;
            } else {
                op->negated = FALSE;
            }
            op->u.labdiff.label1 = expr.label;
            op->u.labdiff.label2 = expr.label2;
            op->u.labdiff.offset = expr.value;
        } else {
            PPCError_Error(PPCERR_ASM_ILLEGAL_LABEL, expr.label->name->name);
        }
    } else if (expr.var) {
        if (!expr.var2) {
            if (ia->opcode != PC_OPWORD && expr.type == IAExpr_Value) {
                IllegalObjectInConst(&expr);
                return;
            } else {
                if (expr.var->datatype == DLOCAL) {
                    op->type = IAOpnd_LocalObj;
                    op->u.obj.reftype = IAExpr_Local16;
                } else {
                    op->type = IAOpnd_GlobalObj;
                    if (expr.type == IAExpr_Value) {
                        expr.type = IAExpr_Addr16;
                    }
                    op->u.obj.reftype = expr.type;
                }
                op->u.obj.obj = expr.var;
                op->u.obj.offset = expr.value;
            }
        } else {
            PPCError_Error(
                PPCERR_ASM_ILLEGAL_OBJECT_DIFFERENCE, expr.var->name->name, expr.var2->name->name
            );
        }
    } else {
        value = InlineAsm_GetExprValue(&expr);
        if (value < minimum || value > maximum) {
            CError_Error(ERR_NUMBER_OUT_RANGE);
        }

        op->type = IAOpnd_Imm;
        if (negate) {
            op->u.imm.value = -value;
        } else {
            op->u.imm.value = value;
        }
    }
}

static void eatcommatoken(void) {
    if (tk == ',') {
        tk = lex();
    } else {
        CError_Error(ERR_COMMA_EXPECTED);
    }
}

// NOTE: invented names: opType, swap, otherValue
static InlineAsm* InlineAsm_ScanAssemblyOperands(IAMnemonic* mnemonic) {
    OpcodeInfo* info;
    InlineAsm* ia;
    SInt32 buffersize;
    IAOperand* op;
    int argcount;
    char* format;
    IARegister* reg;

    char code;
    int effect;
    Boolean has_excl;
    Boolean has_question;
    Boolean negate;
    UInt32 opType;

    info = &opcodeinfo[mnemonic->opcode];
    argcount = info->operand_count;
    if (FLAG_SET_F(info->flags) & fCanSetRecordBit) {
        argcount++;
    }
    if (!(FLAG_SET_F(info->flags) & fSetsCarry) && (FLAG_SET_F(info->flags) & fCanSetCarry)) {
        argcount++;
    }
    if (FLAG_SET_T(info->flags) & fCanLink) {
        argcount++;
    }

    buffersize = sizeof(InlineAsm) + sizeof(IAOperand) * argcount;
    ia = galloc(buffersize);
    memset(ia, 0, buffersize);

    ia->opcode = mnemonic->opcode;
    ia->flags = 0;
    ia->argcount = 0;

    op = ia->args;
    for (format = mnemonic->format; *format; format++) {
        CError_ASSERT(1673, ia->argcount < argcount);

        if (*format == ',') {
            eatcommatoken();
            format++;
        } else if (*format == ';') {
            format++;
        }

        if (*format == '[' || *format == '(') {
            format++;
        }

        effect = EffectRead;
        has_excl = FALSE;
        has_question = FALSE;
        negate = FALSE;
        if (*format == '=') {
            effect = EffectWrite;
            format++;
        } else if (*format == '+') {
            effect = EffectRead | EffectWrite;
            format++;
        }

        // the paired-single operand halves
        if (*format == '^') {
            effect |= EffectPS1;
            format++;
        } else if (*format == ':') {
            effect |= EffectPS0 | EffectPS1;
            format++;
        } else if (*format == '.') {
            effect |= EffectPS0;
            format++;
        } else if (*format == '`') {
            effect |= EffectSingle;
            format++;
        }

        if (*format == '-') {
            negate = TRUE;
            format++;
        }

        if (*format == '?') {
            has_question = TRUE;
            format++;
        }

        if (*format == '!') {
            has_excl = TRUE;
            format++;
        }

        switch (*format) {
            case 'p':
                continue;
            case '&':
                if (format[1] == '2') {
                    format++;
                    op[0] = op[-2];
                    if (op->type == IAOpnd_Reg) {
                        op->u.reg.effect = effect;
                    }
                    ia->argcount++;
                    op++;
                    op[0] = op[-2];
                    if (op->type == IAOpnd_Reg) {
                        op->u.reg.effect = effect;
                    }
                } else {
                    op[0] = op[-1];
                    if (op->type == IAOpnd_Reg) {
                        op->u.reg.effect = effect;
                    }
                }
                break;

            case '%': {
                SInt32 value;
                format += pcode_const_from_format(format + 1, &value);
                op->type = IAOpnd_Imm;
                op->u.imm.value = value;
                break;
            }

            case 'b':
                if (!isregisteroperand(RegClass_GPR)) {
                    if (tk == TK_INTCONST && tkintconst.lo == 0) {
                        r0_operand(op);
                    }
                } else {
                    registeroperand(op, RegClass_GPR, effect);
                    if (op->u.reg.object == NULL && op->u.reg.num == 0) {
                        op->u.reg.effect = 0;
                    }
                }
                break;

            case 'r':
                registeroperand(op, RegClass_GPR, effect);
                break;

            case 'B':
            case 'a':
            case 'i':
            case 't':
            case 'u':
            case 'x': {
                SInt32 value;
                SInt32 hi, lo;
                code = *format;
                value = 16;
                if (code == 'a') {
                    if (isdigit(format[1])) {
                        code = format[1];
                        format++;
                    } else {
#if VERSION < VERSION_GC_2_6
// 2.0 and 2.5's file has 3 lines fewer above here
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -3
#endif
                        CError_FATAL(1822);
                    }
                }

                if (isdigit(format[1])) {
                    format += pcode_const_from_format(format + 1, &value);
                } else if (code == 't' || code == 'B') {
                    code = 'u';
                    value = 5;
                }

                pcode_get_hi_lo(value, code, &hi, &lo);
                if (!has_question || tk == ',') {
                    if (has_question) {
                        tk = lex();
                    }
                    if (has_excl) {
                        getimmediateoperand(lo, hi);
                    } else {
                        immediateoperand(op, lo, hi);
                    }
                } else {
                    op->type = IAOpnd_Imm;
                    op->u.imm.value = 0;
                }

                if (code == 'i' && value == 16) {
                    op->u.imm.value = (short)op->u.imm.value;
                }
                if (negate) {
                    op->u.imm.value = -op->u.imm.value;
                }

                if (pcode_check_imm_bits(op->u.imm.value, value, code)) {
                    CError_FATAL(1856);
                }

                break;
            }

            case 'O':
                if (tk == TK_INTCONST && tkintconst.lo == 0) {
                    getimmediateoperand(0, 0);
                    eatcommatoken();
                }
                continue;

            case 'f':
                registeroperand(op, RegClass_FPR, effect);
                break;

            case 'v':
                registeroperand(op, RegClass_VR, effect);
                break;

            case 'T':
                if (!has_question || tk == ',') {
                    if (has_question) {
                        tk = lex();
                    }

                    immediateoperand(op, 268, 269);
                    if (op->u.reg.num == 268) {
                        op->u.reg.num = 284;
                    } else {
                        op->u.reg.num = 285;
                    }
                } else {
                    op->u.reg.num = 284;
                }

                op->type = IAOpnd_Reg;
                op->u.reg.rclass = RegClass_SPR;
                op->u.reg.effect = effect;
                break;

            case 'S': {
                SInt32 value;
                format += pcode_const_from_format(format + 1, &value);
                op->type = IAOpnd_Reg;
                op->u.reg.rclass = RegClass_SPR;
                op->u.reg.num = value;
                op->u.reg.effect = effect;
                break;
            }

            case 'c':
                if (has_question) {
                    if (isregisteroperand(RegClass_CRFIELD) || tk == TK_INTCONST) {
                        registeroperand(op, RegClass_CRFIELD, effect);
                    } else {
                        cr0_operand(op, effect);
                        if (format[1] == ',') {
                            format++;
                        }
                    }
                } else {
                    registeroperand(op, RegClass_CRFIELD, effect);
                }
                break;

            case 'l':
                switch (ia->opcode) {
                    case PC_B:
                    case PC_BL:
                        labeloperand(
                            ia, op, TRUE, mnemonic->instruction & 2, mnemonic->instruction & 1
                        );
                        break;
                    default:
                        labeloperand(
                            ia, op, FALSE, mnemonic->instruction & 2, mnemonic->instruction & 1
                        );
                        break;
                }
                break;

            case 'w':
                imm_or_labeldiff_operand(ia, op, -0x80000000, 0x7FFFFFFF, negate);
                break;

            case 'M':
            case 'm':
            case 'n':
                imm_or_labeldiff_operand(ia, op, -0x8000, 0xFFFF, negate);
                break;

            case 'Q': {
                SInt32 cr = crbitoperand();
                op->type = IAOpnd_Reg;
                op->u.reg.num = cr >> 2;
                op->u.reg.rclass = RegClass_CRFIELD;
                op->u.reg.effect = effect;

                ia->argcount++;
                op++;
                op->type = IAOpnd_Imm;
                op->u.imm.value = cr % 4;
                break;
            }

            case 'd': {
                int effect2;
                CError_ASSERT(1989, format[1] == '(');
                format++;
                effect2 = EffectRead;
                if (format[1] == '=') {
                    effect2 = EffectWrite;
                    format++;
                } else if (format[1] == '+') {
                    effect2 = EffectRead | EffectWrite;
                    format++;
                }

                CError_ASSERT(2001, format[1] == 'b');
                CError_ASSERT(2003, format[2] == ')');
                format += 2;

                switch (ia->opcode) {
                    case PC_LFS:
                        if (tk == TK_FLOATCONST) {
                            floatoperand(op, ia, TYPE(&stfloat));
                        } else {
                            memoryoperand(op, ia, TRUE, effect2);
                        }
                        break;
                    case PC_LFD:
                        if (tk == TK_FLOATCONST) {
                            floatoperand(op, ia, TYPE(&stdouble));
                        } else {
                            memoryoperand(op, ia, TRUE, effect2);
                        }
                        break;
                    case PC_PSQ_L:
                    case PC_PSQ_LU:
                    case PC_PSQ_LX:
                    case PC_PSQ_LUX:
                    case PC_PSQ_ST:
                    case PC_PSQ_STU:
                    case PC_PSQ_STX:
                    case PC_PSQ_STUX:
                        op = parse_displacement_operand(op, ia, effect2);
                        break;
                    default:
                        memoryoperand(op, ia, TRUE, effect2);
                }

                break;
            }

            case 'N':
                immediateoperand(op, 1, 32);
                break;

            case 's':
                registeroperand(op, RegClass_SPR, effect);
                break;

            case 'D':
                if (tk == TK_IDENTIFIER &&
                    (reg = InlineAsm_LookupDCRRegister(tkidentifier->name))) {
                    op->type = IAOpnd_Imm;
                    op->u.imm.value = reg->num;
                    tk = lex();
                } else {
                    immediateoperand(op, 0, 1023);
                }
                break;

            case 'Y': {
                SInt32 mask = 255;
                int i;
                if (ia->opcode == PC_MTCRF) {
                    if (ia->args[0].type != IAOpnd_Imm) {
                        CError_Error(ERR_ILLEGAL_OPERAND);
                    }
                    mask = ia->args[0].u.imm.value;
                }
                for (i = 0; i < 8; i++) {
                    if ((0x80 >> i) & mask) {
                        op->type = IAOpnd_Reg;
                        op->u.reg.num = i;
                        op->u.reg.rclass = RegClass_CRFIELD;
                        op->u.reg.effect = effect;
                        ia->argcount++;
                        op++;
                    }
                }
                op--;
                ia->argcount--;
                break;
            }

            case 'P':
                op->type = IAOpnd_Reg;
                op->u.reg.rclass = RegClass_SPR;
                op->u.reg.object = NULL;
                if (format[1] == '3') {
                    format++;
                    op->u.reg.num = getimmediateoperand(0, 7);
                } else {
                    op->u.reg.num = getimmediateoperand(0, 3);
                }
                op->u.reg.effect = effect;
                break;

            case 'C':
                op->type = IAOpnd_Reg;
                op->u.reg.rclass = RegClass_SPR;
                op->u.reg.object = NULL;
                op->u.reg.num = 9;
                op->u.reg.effect = effect;
                break;

            case 'L':
                op->type = IAOpnd_Reg;
                op->u.reg.rclass = RegClass_SPR;
                op->u.reg.object = NULL;
                op->u.reg.num = 8;
                op->u.reg.effect = effect;
                break;

            case 'X':
                op->type = IAOpnd_Reg;
                op->u.reg.rclass = RegClass_SPR;
                op->u.reg.object = NULL;
                op->u.reg.num = 1;
                op->u.reg.effect = effect;
                break;

            case 'Z':
                op->type = IAOpnd_Reg;
                op->u.reg.rclass = RegClass_CRFIELD;
                op->u.reg.object = NULL;
                opType = info->flags & fOpTypeMask;
                if (opType == fOpTypeFPR) {
                    op->u.reg.num = 1;
                } else if (opType == fOpTypeVR) {
                    op->u.reg.num = 6;
                } else {
                    op->u.reg.num = 0;
                }
                op->u.reg.effect = effect;
                break;

            default:
                CError_FATAL(2285);
        }

        while (format[1] && strchr("/<>|*@", format[1])) {
            int index;
            SInt32 value;
            IAOperand swap;
            SInt32 otherValue;
            switch ((code = *(++format))) {
                case '/':
                    index = -1;
                    if (format[1] == '2') {
                        format++;
                        index = -2;
                    }

                    swap = op[index];
                    op[index] = op[0];
                    op[0] = swap;
                    break;
                case '*':
                case '<':
                case '>':
                case '@':
                case '|':
                    if (op->type == IAOpnd_Imm) {
                        value = op->u.imm.value;
                    } else if (op->type == IAOpnd_Reg) {
                        value = op->u.reg.num;
                    } else {
                        CError_FATAL(2331);
                    }

                    if (format[1] == 'p') {
                        format++;
                        if (op[-1].type == IAOpnd_Imm) {
                            otherValue = op[-1].u.imm.value;
                        } else if (op[-1].type == IAOpnd_Reg) {
                            otherValue = op[-1].u.reg.num;
                        } else {
                            CError_FATAL(2341);
                        }
                    } else if (isdigit(format[1])) {
                        format += pcode_const_from_format(format + 1, &otherValue);
                    } else {
                        CError_FATAL(2346);
                    }

                    switch (code) {
                        case '<':
                            value = otherValue - value;
                            break;
                        case '>':
                            value = value - otherValue;
                            break;
                        case '|':
                            value = value + otherValue;
                            break;
                        case '*':
                            value = value * otherValue;
                            break;
                        case '@':
                            value = value & (0xFFFFFFFF >> (32 - otherValue));
                            break;
                        default:
                            CError_FATAL(2371);
                    }

                    if (op->type == IAOpnd_Imm) {
                        op->u.imm.value = value;
                    } else if (op->type == IAOpnd_Reg) {
                        op->u.reg.num = value;
                    } else {
                        CError_FATAL(2378);
                    }
                    break;
            }
        }

        ia->argcount++;
        op++;
        if (format[1] == ']' || format[1] == ')') {
            format++;
        }
    }

    return ia;
}

static int mnemonic_has_overflow(char* buf) {
    int result = FALSE;

    if (buf[strlen(buf) - 1] == 'o') {
        if (!strcmp(buf, "bso")) {
            return FALSE;
        }
        if (!strcmp(buf, "fcmpo")) {
            return FALSE;
        }
        if (!strcmp(buf, "eieio")) {
            return FALSE;
        }
        if (!strcmp(buf, "vslo")) {
            return FALSE;
        }
        if (!strcmp(buf, "vsro")) {
            return FALSE;
        }
        result = TRUE;
    }

    return result;
}

static int mnemonic_has_absolute(const char* buf) {
    int result = FALSE;
    char last = buf[strlen(buf) - 1];

    if (buf[0] == 'b' && last == 'a') {
        result = TRUE;
    }

    return result;
}

static int mnemonic_has_linkregister(const char* buf) {
    int result = FALSE;
    char last = buf[strlen(buf) - 1];

    if (buf[0] == 'b' && last == 'l') {
        result = TRUE;
    }

    return result;
}

void InlineAsm_InitializePPC(void) {
    Test_Version_Numbers();
    asm_volatile = volatileasm;
    warn_def_reserved_reg = TRUE;
    allow_array_expressions = TRUE;
    supports_hardware_fpu = TRUE;
    assembledinstructions = 0;

    switch (copts.processor) {
        case CPU_PPC401:
            cpu = CPUMask_401;
            break;
        case CPU_PPC403:
            cpu = CPUMask_403;
            break;
        case CPU_PPC505:
            cpu = CPUMask_50x;
            break;
        case CPU_PPC509:
            cpu = CPUMask_50x;
            break;
        case CPU_PPC555:
            cpu = CPUMask_55x_56x;
            break;
        case CPU_PPC556:
            cpu = CPUMask_55x_56x;
            break;
        case CPU_PPC565:
            cpu = CPUMask_55x_56x;
            break;
        case CPU_PPC601:
            cpu = CPUMask_601;
            break;
        case CPU_PPC602:
            cpu = CPUMask_602;
            break;
        case CPU_PPC8240:
            cpu = CPUMask_8240;
            break;
        case CPU_PPC8260:
            cpu = CPUMask_8260;
            break;
        case CPU_PPC603:
            cpu = CPUMask_603;
            break;
        case CPU_PPC603e:
            cpu = CPUMask_603;
            break;
        case CPU_PPC604:
            cpu = CPUMask_604;
            break;
        case CPU_PPC604e:
            cpu = CPUMask_604;
            break;
        case CPU_PPC740:
            cpu = CPUMask_740_750;
            break;
        case CPU_PPC750:
            cpu = CPUMask_740_750;
            break;
        case CPU_PPC801:
            cpu = CPUMask_801_821_860;
            break;
        case CPU_PPC821:
            cpu = CPUMask_801_821_860;
            break;
        case CPU_PPC823:
            cpu = CPUMask_823_850;
            break;
        case CPU_PPC850:
            cpu = CPUMask_823_850;
            break;
        case CPU_PPC860:
            cpu = CPUMask_801_821_860;
            break;
        case CPU_PPC7400:
        case CPU_PPC7450:
            cpu = CPUMask_74xx;
            break;
        case CPU_PPCGEKKO:
            cpu = CPUMask_Gekko;
            break;
        case CPU_Generic:
            cpu = CPUMask_Generic;
            break;
        default:
            CError_FATAL(2643);
    }

    supports_hardware_fpu = copts.use_fp && !copts.soft_float;

    if (copts.altivec_model) {
        cpu |= CPUFLAG_ALTIVEC;
    }
}

void InlineAsm_Initialize(AssemblerType assemblertype) {
    assembler_type = assemblertype;
    if (copts.cats && !copts.force_cats) {
        MWCATS_AddProc(cscope_currentfunc, 1);
    }

    if (assembler_type == AssemblerType_Statements) {
        InlineAsm_InitializePPC();
    }

    InlineAsm_InitializeMnemonicsPPC();
    InlineAsm_InitializeRegisters();
    InlineAsm_InitializeRegistersPPC();

    if (assembler_type != AssemblerType_Statements) {
        process_arguments(setup_assembly_argument, FALSE);
        assign_local_addresses();
    }
}

// NOTE: invented names: name, isStatic
static InlineAsm* InlineAsm_CreateEntryPoint(HashNameNode* name, Boolean isStatic) {
    Object* obj;
    IAEntryPoint* ep;
    InlineAsm* ia;
    IALookupResult result;

    if (InlineAsm_LookupSymbol(name, &result) && (obj = result.object)) {
        if (!(obj->datatype == DFUNC && !(obj->flags & OBJECT_DEFINED))) {
            CError_Error(ERR_IDENTIFIER_REDECLARED, name->name);
        }

        obj->flags |= OBJECT_DEFINED;
        obj->sclass = isStatic ? TK_STATIC : TK_EXTERN;

        ep = lalloc(sizeof(IAEntryPoint));
        memclrw(ep, sizeof(IAEntryPoint));
        ia = (InlineAsm*)ep;
        ia->opcode = IADirective_Entry;
        ia->flags = IAFlag_Directive;
        ep->object = obj;
        ep->size = assembledinstructions * 4;
    } else {
        // the original's bug: after this error `ia` is returned unset
        CError_Error(ERR_UNDEFINED_IDENTIFIER, name->name);
    }

    return ia;
}

SInt32 InlineAsm_IsDirective(AssemblerType assemblertype) {
    char* name;
    SInt32 directive = IADirective_Null;

    if (tk == '.') {
        tk = lex();
    }

    if (tk == TK_IDENTIFIER || tk == TK_VOLATILE) {
        name = tkidentifier->name;
        if (!strcmp(name, "machine")) {
            directive = IADirective_Machine;
        } else if (!strcmp(name, "warn_def_reserved_reg")) {
            directive = IADirective_WarnDefReservedReg;
        } else if (!strcmp(name, "volatile")) {
            directive = IADirective_Volatile;
        } else if (!strcmp(name, "nonvolatile")) {
            directive = IADirective_NonVolatile;
        } else if (!strcmp(name, "scheduling")) {
            directive = IADirective_Scheduling;
        } else if (assemblertype == AssemblerType_Function) {
            if (!strcmp(name, "entry")) {
                directive = IADirective_Entry;
            } else if (!strcmp(name, "fralloc")) {
                directive = IADirective_FrAlloc;
            } else if (!strcmp(name, "nofralloc")) {
                directive = IADirective_NoFrAlloc;
            } else if (!strcmp(name, "frfree")) {
                directive = IADirective_FrFree;
            } else if (!strcmp(name, "smclass")) {
                directive = IADirective_SmClass;
            } else {
                directive = IADirective_Null;
            }
        }
    }

    return directive;
}

void InlineAsm_ProcessDirective(SInt32 directive) {
    Boolean isStatic; // NOTE: invented name
    Statement* stmt;
    int token;

    switch (directive) {
        case IADirective_Entry:
            isStatic = FALSE;
            tk = lex();
            if ((token = tk) == TK_STATIC) {
                isStatic = TRUE;
                tk = lex();
            } else if (token == TK_EXTERN) {
                tk = lex();
            }

            if (tk != TK_IDENTIFIER) {
                CError_Error(ERR_IDENTIFIER_EXPECTED);
            }

            stmt = CFunc_AppendStatement(ST_ASM);
            stmt->expr = NULL;
            stmt->expr = (ENode*)InlineAsm_CreateEntryPoint(tkidentifier, isStatic);
            stmt->sourceoffset = -1;
            tk = lex();
            break;

        case IADirective_FrAlloc:
            if (assembledinstructions) {
                CError_Error(ERR_FUNCTION_NO_INITIALIZED_STACKFRAME);
            }
            if (asm_alloc_flags[0]) {
                CError_Error(ERR_FUNCTION_ALREADY_STACKFRAME);
            }
            if (asm_alloc_flags[1] || asm_alloc_flags[2]) {
                CError_Error(ERR_FUNCTION_NO_INITIALIZED_STACKFRAME);
            }

            tk = lex();
            if (tk != TK_EOL && tk != ';') {
                IAExpr expr;
                int absolute, noNodes;
                Object* base;
                Object* offset;
                SInt32 value;
                InlineAsm_ExpressionPPC(&expr, 0);
                absolute = FALSE;
                noNodes = FALSE;
                if ((base = expr.var) == NULL && expr.object == NULL) {
                    noNodes = TRUE;
                }
                if (noNodes && expr.label == NULL) {
                    absolute = TRUE;
                }
                if (!absolute) {
                    if (base != NULL) {
                        PPCError_Error(PPCERR_ASM_OBJECT_IN_CONSTANT, base->name->name);
                    } else if ((offset = expr.object) != NULL) {
                        PPCError_Error(PPCERR_ASM_OBJECT_IN_CONSTANT, offset->name->name);
                    } else if (expr.label) {
                        PPCError_Error(PPCERR_ASM_UNDEFINED_SYMBOL, expr.label->name->name);
                    }
                    value = 0;
                } else {
                    value = InlineAsm_GetExprValue(&expr);
                }
                if (value < 0x20 || value > 0x7FFE) {
                    CError_Error(ERR_NUMBER_OUT_RANGE);
                }
                fralloc_parameter_area_size = value;
            }

            requires_frame = TRUE;
            asm_alloc_flags[0] = TRUE;
            break;

        case IADirective_NoFrAlloc:
            if (asm_alloc_flags[0] || asm_alloc_flags[1]) {
                CError_Error(ERR_FUNCTION_ALREADY_STACKFRAME);
            }
            if (asm_alloc_flags[2]) {
                CError_Error(ERR_FUNCTION_NO_INITIALIZED_STACKFRAME);
            }
            if (assembledinstructions) {
                CError_Error(ERR_FUNCTION_NO_INITIALIZED_STACKFRAME);
            }
            tk = lex();

            asm_alloc_flags[1] = TRUE;
            user_responsible_for_frame = TRUE;
            break;

        case IADirective_FrFree: {
            Statement* node;
            InlineAsm* ia;
            if (asm_alloc_flags[1] || asm_alloc_flags[2]) {
                CError_Error(ERR_FUNCTION_NO_INITIALIZED_STACKFRAME);
            }

            asm_alloc_flags[2] = TRUE;
            node = CFunc_AppendStatement(ST_ASM);
            node->expr = NULL;
            ia = lalloc(sizeof(InlineAsm));
            memclrw(ia, sizeof(InlineAsm));
            ia->opcode = IADirective_FrFree;
            ia->flags = IAFlag_Directive;
            node->expr = (ENode*)ia;
            if (copts.filesyminfo) {
                node->sourceoffset = CPrep_GetFileOffsetInfo(&cparser_fileoffset);
            } else {
                node->sourceoffset = -1;
            }
            tk = lex();
            break;
        }

        case IADirective_Machine:
            tk = lex();
            if (tk == TK_INTCONST) {
                switch (tkintconst.lo) {
                    case 401:
                        cpu = CPUMask_401;
                        break;
                    case 403:
                        cpu = CPUMask_403;
                        break;
                    case 505:
                        cpu = CPUMask_50x;
                        break;
                    case 509:
                        cpu = CPUMask_50x;
                        break;
                    case 555:
                        cpu = CPUMask_55x_56x;
                        break;
                    case 556:
                        cpu = CPUMask_55x_56x;
                        break;
                    case 565:
                        cpu = CPUMask_55x_56x;
                        break;
                    case 601:
                        cpu = CPUMask_601;
                        break;
                    case 602:
                        cpu = CPUMask_602;
                        break;
                    case 8240:
                        cpu = CPUMask_8240;
                        break;
                    case 8260:
                        cpu = CPUMask_8260;
                        break;
                    case 603:
                        cpu = CPUMask_603;
                        break;
                    case 604:
                        cpu = CPUMask_604;
                        break;
                    case 740:
                        cpu = CPUMask_740_750;
                        break;
                    case 750:
                        cpu = CPUMask_740_750;
                        break;
                    case 801:
                        cpu = CPUMask_801_821_860;
                        break;
                    case 821:
                        cpu = CPUMask_801_821_860;
                        break;
                    case 823:
                        cpu = CPUMask_823_850;
                        break;
                    case 850:
                        cpu = CPUMask_823_850;
                        break;
                    case 860:
                        cpu = CPUMask_801_821_860;
                        break;
                    case 7400:
                        cpu = CPUMask_74xx;
                        break;
                    default:
                        CError_Error(ERR_ILLEGAL_OPERAND);
                }
            } else if (tk == TK_IDENTIFIER) {
                if (!strcmp(tkidentifier->name, "all")) {
                    cpu = CPUMask_All;
                } else if (!strcmp(tkidentifier->name, "generic")) {
                    cpu = CPUMask_Generic;
                } else if (!strcmp(tkidentifier->name, "603e")) {
                    cpu = CPUMask_603;
                } else if (!strcmp(tkidentifier->name, "604e")) {
                    cpu = CPUMask_604;
                } else if (!strcmp(tkidentifier->name, "PPC603e")) {
                    cpu = CPUMask_603;
                } else if (!strcmp(tkidentifier->name, "PPC604e")) {
                    cpu = CPUMask_604;
                } else if (!strcmp(tkidentifier->name, "PPC403GA")) {
                    cpu = CPUMask_403 | 0x100;
                } else if (!strcmp(tkidentifier->name, "PPC403GB")) {
                    cpu = CPUMask_403;
                } else if (!strcmp(tkidentifier->name, "PPC403GC")) {
                    cpu = CPUMask_403 | CPUFLAG_TLB | 0x200;
                } else if (!strcmp(tkidentifier->name, "PPC403GCX")) {
                    cpu = CPUMask_403 | CPUFLAG_TLB | 0x400;
                } else if (!strcmp(tkidentifier->name, "altivec")) {
                    cpu = CPUMask_74xx;
                } else if (!strcmp(tkidentifier->name, "gekko") ||
                           !strcmp(tkidentifier->name, "gecko")) {
                    cpu = CPUMask_Gekko;
                } else {
                    CError_Error(ERR_ILLEGAL_OPERAND);
                }
            } else {
                CError_Error(ERR_ILLEGAL_OPERAND);
            }
            tk = lex();
            break;

        case IADirective_WarnDefReservedReg:
            tk = lex();
            if (tk == TK_IDENTIFIER) {
                if (!strcmp(tkidentifier->name, "on")) {
                    warn_def_reserved_reg = TRUE;
                } else if (!strcmp(tkidentifier->name, "off")) {
                    warn_def_reserved_reg = FALSE;
                } else {
                    CError_Error(ERR_ILLEGAL_OPERAND);
                }
            } else {
                CError_Error(ERR_ILLEGAL_OPERAND);
            }
            tk = lex();
            break;

        case IADirective_Volatile:
            asm_volatile = TRUE;
            tk = lex();
            break;

        case IADirective_NonVolatile:
            asm_volatile = FALSE;
            tk = lex();
            break;

        case IADirective_Scheduling:
            tk = lex();
            if (tk == TK_IDENTIFIER) {
                if (!strcmp(tkidentifier->name, "on")) {
                    asm_volatile = FALSE;
                } else if (!strcmp(tkidentifier->name, "off")) {
                    asm_volatile = TRUE;
                } else {
                    CError_Error(ERR_ILLEGAL_OPERAND);
                }
            } else {
                CError_Error(ERR_ILLEGAL_OPERAND);
            }
            tk = lex();
            break;

        case IADirective_SmClass:
            tk = lex();
            if (tk == TK_IDENTIFIER) {
                if (!strcmp(tkidentifier->name, "PR")) {
                    sm_section = GetCurrentSection(SECT_TEXT);
                } else if (!strcmp(tkidentifier->name, "GL")) {
                    sm_section = GetCurrentSection(29);
                } else {
                    CError_Error(ERR_ILLEGAL_OPERAND);
                }
            } else {
                CError_Error(ERR_ILLEGAL_OPERAND);
            }
            tk = lex();
            break;

        default:
            CError_Error(ERR_UNKNOWN_ASSEMBLER_INSTRUCTION_MNEMONIC);
            break;
    }
}

void InlineAsm_ScanAssemblyDirective(void) {
    SInt32 directive;

    if ((directive = InlineAsm_IsDirective(assembler_type)) != IADirective_Null) {
        InlineAsm_ProcessDirective(directive);
    }
}

// NOTE: invented names: overflow_flag, absolute_flag, link_flag, plus_flag, minus_flag (the
// mnemonic's suffixes and branch hint)
void InlineAsm_ScanAssemblyInstruction(void) {
    Boolean record_flag;
    Statement* stmt;
    // TODO: 2.6 stores these three as words and tests only their low byte, which only these
    // `(Boolean)` casts reproduce; what the original declared is not known
    UInt32 overflow_flag, absolute_flag, link_flag;
    Boolean plus_flag = FALSE;
    Boolean minus_flag = FALSE;
    char buf[20];
    char* name;
    IAMnemonic* mnemonic;
    OpcodeInfo* info;
    InlineAsm* ia;
    InlineAsm* parsed;
    SInt32 directive;

    directive = InlineAsm_IsDirective(assembler_type);
    if (directive != IADirective_Null) {
        InlineAsm_ProcessDirective(directive);
        return;
    }

    stmt = CFunc_AppendStatement(ST_ASM);
    stmt->expr = NULL;
    if (copts.filesyminfo) {
        stmt->sourceoffset = CPrep_GetFileOffsetInfo(&cparser_fileoffset);
    } else {
        stmt->sourceoffset = -1;
    }

    strncpy(buf, tkidentifier->name, sizeof(buf));
    name = buf;
    record_flag = FALSE;
    if (lookahead() == '.') {
        tk = lex();
        record_flag = TRUE;
        strcat(name, ".");
    }

    mnemonic = InlineAsm_LookupMnemonicPPC(name);
    if (mnemonic == NULL) {
        CError_Error(ERR_UNKNOWN_ASSEMBLER_INSTRUCTION_MNEMONIC);
    }

    info = &opcodeinfo[mnemonic->opcode];
    overflow_flag = FALSE;
    if ((FLAG_SET_F(info->flags) & fCanSetCarry) && (mnemonic->instruction & 0x400)) {
        overflow_flag = TRUE;
    }
    absolute_flag = FALSE;
    if ((FLAG_SET_T(info->flags) & fCanBeAbsolute) && (mnemonic->instruction & 2)) {
        absolute_flag = TRUE;
    }
    link_flag = FALSE;
    if ((FLAG_SET_T(info->flags) & fCanLink) && (mnemonic->instruction & 1)) {
        link_flag = TRUE;
    }

    if (cpu == CPUMask_Generic &&
        (cpu & CPUFLAG_LOW_MASK) != (cpu & mnemonic->cpu & CPUFLAG_LOW_MASK)) {
        CError_Error(ERR_ILLEGAL_INSTRUCTION_PROCESSOR);
    } else if ((cpu & mnemonic->cpu & CPUFLAG_LOW_MASK) == 0) {
        CError_Error(ERR_ILLEGAL_INSTRUCTION_PROCESSOR);
    } else if ((mnemonic->cpu & CPUFLAG_TIME_BASE) && !(cpu & CPUFLAG_TIME_BASE)) {
        CError_Error(ERR_ILLEGAL_INSTRUCTION_PROCESSOR);
    } else if ((mnemonic->cpu & CPUFLAG_CACHE) && !(cpu & CPUFLAG_CACHE)) {
        CError_Error(ERR_ILLEGAL_INSTRUCTION_PROCESSOR);
    } else if ((mnemonic->cpu & CPUFLAG_EXTERNAL_CONTROL) && !(cpu & CPUFLAG_EXTERNAL_CONTROL)) {
        CError_Error(ERR_ILLEGAL_INSTRUCTION_PROCESSOR);
    } else if ((mnemonic->cpu & CPUFLAG_TLB) && !(cpu & CPUFLAG_TLB)) {
        CError_Error(ERR_ILLEGAL_INSTRUCTION_PROCESSOR);
    } else if ((mnemonic->cpu & CPUFLAG_SEGMENTS) && !(cpu & CPUFLAG_SEGMENTS)) {
        CError_Error(ERR_ILLEGAL_INSTRUCTION_PROCESSOR);
    } else if ((mnemonic->cpu & CPUFLAG_ALTIVEC) && !(cpu & CPUFLAG_ALTIVEC)) {
        CError_Error(ERR_ILLEGAL_INSTRUCTION_PROCESSOR);
    } else if ((mnemonic->cpu & CPUFLAG_PAIRED_SINGLE) && !(cpu & CPUFLAG_PAIRED_SINGLE)) {
        CError_Error(ERR_ILLEGAL_INSTRUCTION_PROCESSOR);
    } else if ((mnemonic->cpu & CPUFLAG_GRAPHICS) && !(cpu & CPUFLAG_GRAPHICS)) {
        CError_Error(ERR_ILLEGAL_INSTRUCTION_PROCESSOR);
    } else if (mnemonic->cpu & CPUFLAG_FPU) {
        if (!supports_hardware_fpu) {
            PPCError_Error(PPCERR_NO_HARDWARE_FLOAT);
        }
    }

    tk = lex();
    if (tk == '+' || tk == '-') {
        if ((((mnemonic->instruction >> 26) & 0x3F) == 16 &&
             OPCODE_PART_2(mnemonic->instruction) != 20) ||
            (((mnemonic->instruction >> 26) & 0x3F) == 19 &&
             OPCODE_PART_3(mnemonic->instruction) == 528 &&
             OPCODE_PART_2(mnemonic->instruction) != 20) ||
            (((mnemonic->instruction >> 26) & 0x3F) == 19 &&
             OPCODE_PART_3(mnemonic->instruction) == 16 &&
             OPCODE_PART_2(mnemonic->instruction) != 20)) {
            if (tk == '+') {
                plus_flag = TRUE;
            } else if (tk == '-') {
                minus_flag = TRUE;
            }
            tk = lex();
        } else {
            CError_Error(ERR_UNEXPECTED_TOKEN);
        }
    }

    parsed = InlineAsm_ScanAssemblyOperands(mnemonic);
    stmt->expr = (ENode*)parsed;
    ia = (InlineAsm*)stmt->expr;
    if (record_flag) {
        ia->flags2 |= IAFlagsB_Record;
    }
    if ((Boolean)overflow_flag) {
        ia->flags2 |= IAFlagsB_Overflow;
    }
    if ((Boolean)absolute_flag) {
        ia->flags2 |= IAFlagsB_Absolute;
    }
    if ((Boolean)link_flag) {
        ia->flags2 |= IAFlagsB_Link;
    }
    if (plus_flag) {
        ia->flags2 |= IAFlagsB_HintPlus;
    }
    if (minus_flag) {
        ia->flags2 |= IAFlagsB_HintMinus;
    }
    if (asm_volatile) {
        ia->flags2 |= IAFlagsB_Volatile;
    }
    if (volatileasm) {
        ia->flags2 |= IAFlagsB_Volatile;
    }

    assembledinstructions++;
}

static PCode* InlineAsm_TranslateIRtoPCodePPC(
    InlineAsm* ia, int argcount, AssemblerType assemblertype
) {
    PCode* pc;
    int index;
    int extra_args;
    int newargcount;
    SInt32 buffersize;
    IAOperand* src;
    PCodeArg* dest;
    OpcodeInfo* info;
    int reg;

    info = &opcodeinfo[ia->opcode];
    index = 0;
    extra_args = 0;
    reg = 0;

    if ((FLAG_SET_F(info->flags) & fCanSetRecordBit) && !(FLAG_SET_F(info->flags) & fRecordBit)) {
        extra_args++;
    }

    if (!(FLAG_SET_F(info->flags) & fSetsCarry) && (FLAG_SET_F(info->flags) & fCanSetCarry)) {
        extra_args++;
    }

    if (argcount < ia->argcount) {
        if ((argcount + extra_args) >= ia->argcount) {
            extra_args -= (ia->argcount - argcount);
            argcount = ia->argcount;
        } else {
            CError_FATAL(3432);
        }
    }

    if (ia->opcode == PC_B && ia->args[0].type == IAOpnd_Imm) {
        ia->flags2 |= IAFlagsB_Volatile;
    }

    if (ia->opcode == PC_TWI ||
        ia->opcode == PC_TW ||
        ia->opcode == PC_TRAP ||
        ia->opcode == PC_SC) {
        ia->flags |= IAFlag_Call;
    }

    if (ia->flags & IAFlag_Call) {
        newargcount = argcount + branch_count_volatiles();
        if (copts.exceptions && current_statement && assembler_type == AssemblerType_Statements) {
            newargcount += countexceptionactionregisters(current_statement->dobjstack);
        }

        buffersize = offsetof(PCode, args) + sizeof(PCodeArg) * (newargcount + extra_args);
        pc = lalloc(buffersize);
        memset(pc, 0, buffersize);
        pc->argCount = newargcount;
    } else if (ia->opcode == PC_STMW || ia->opcode == PC_LMW) {
        src = ia->args;
        reg = src->u.reg.object ? OBJECT_REG(src->u.reg.object) : src->u.reg.num;
        newargcount = argcount + (32 - reg);

        buffersize = offsetof(PCode, args) + sizeof(PCodeArg) * newargcount;
        pc = lalloc(buffersize);
        memset(pc, 0, buffersize);
        pc->argCount = newargcount;
    } else {
        buffersize = offsetof(PCode, args) + sizeof(PCodeArg) * (argcount + extra_args);
        pc = lalloc(buffersize);
        memset(pc, 0, buffersize);
        pc->argCount = (ia->argcount > argcount) ? ia->argcount : argcount;
    }

    pc->op = ia->opcode;
    pc->flags = info->flags;
    pc->sourceoffset = current_statement ? current_statement->sourceoffset : -1;

    dest = pc->args;
    src = ia->args;
    while (index < argcount + extra_args) {
        if (index >= ia->argcount) {
            dest->kind = PCOp_PLACEHOLDEROPERAND;
        } else {
            switch (src->type) {
                case IAOpnd_Placeholder:
                    dest->kind = PCOp_PLACEHOLDEROPERAND;
                    break;

                case IAOpnd_Imm:
                    dest->kind = PCOp_IMMEDIATE;
                    dest->data.imm.value = src->u.imm.value;
                    if (pc->flags & (fIsRead | fIsWrite)) {
                        pc->flags |= fIsPtrOp;
                    }
                    dest->data.imm.obj = NULL;
                    break;

                case IAOpnd_Reg: {
                    int regnum;
                    regnum = 0;
                    if (src->u.reg.object) {
                        if (Registers_GetVarInfo(src->u.reg.object)->flags & VarInfoFlag_InReg) {
                            if (src->u.reg.num == -3) {
                                regnum = OBJECT_REG_HI(src->u.reg.object);
                            } else {
                                regnum = OBJECT_REG(src->u.reg.object);
                            }
                        } else {
                            if (Registers_GetVarInfo(src->u.reg.object)->flags &
                                VarInfoFlag_AsmNamed) {
                                PPCError_Error(
                                    PPCERR_OUT_OF_REGISTERS_FOR_LOCAL, src->u.reg.object->name->name
                                );
                            } else {
                                PPCError_Error(
                                    PPCERR_ASM_NOT_IN_REGISTER, src->u.reg.object->name->name
                                );
                            }
                        }
                    } else {
                        regnum = src->u.reg.num;
                    }

                    dest->kind = PCOp_REGISTER;
                    dest->arg = src->u.reg.rclass;
                    dest->data.reg.reg = regnum;
                    dest->data.reg.effect = src->u.reg.effect;
                    if (pc->op == PC_RLWIMI &&
                        (dest->data.reg.effect & EffectWrite) &&
                        dest->arg == RegClass_GPR &&
                        !(dest->data.reg.effect & EffectRead)) {
                        CError_FATAL(3558);
                    }

                    if (dest->arg == RegClass_SPR) {
                        short i;
                        dest->kind = PCOp_SYSREG;
                        for (i = 0; i < 4; i++) {
                            if (dest->data.reg.reg == spr_to_sysreg[i]) {
                                dest->kind = PCOp_REGISTER;
                                dest->arg = RegClass_SPR;
                                dest->data.reg.reg = i;
                                break;
                            }
                        }
                        switch (pc->op) {
                            case PC_PSQ_L:
                            case PC_PSQ_LU:
                            case PC_PSQ_LX:
                            case PC_PSQ_LUX:
                            case PC_PSQ_ST:
                            case PC_PSQ_STX:
                                if (dest->data.reg.reg != SPR_GQR0) {
                                    pcsetsideeffects(pc);
                                }
                                break;
                            default:
                                pcsetsideeffects(pc);
                                break;
                        }
                    } else if (dest->arg == RegClass_CRBIT || dest->arg == RegClass_DCR) {
                        SInt32 save = dest->data.reg.reg;
                        dest->kind = PCOp_IMMEDIATE;
                        dest->data.imm.value = save;
                        dest->data.imm.obj = NULL;
                        break;
                    }

                    if ((src->u.reg.effect & EffectWrite) &&
                        dest->data.reg.reg < n_real_registers[dest->arg]) {
                        if (warn_def_reserved_reg &&
                            dest->data.reg.reg < n_real_registers[dest->arg] &&
                            reg_state[dest->arg][dest->data.reg.reg] == RegState_Reserved &&
                            assemblertype == AssemblerType_Statements) {
                            PPCError_Warning(
                                PPCERR_ASM_DEFINES_RESERVED_REGISTER, info->name, dest->data.reg.reg
                            );
                            pcsetsideeffects(pc);
                        }
                        if (src->u.reg.object) {
                            if (Registers_GetVarInfo(src->u.reg.object)->flags &
                                VarInfoFlag_RegPair) {
                                int reg, regHi;
                                CError_ASSERT(3615, dest->arg == RegClass_GPR);

                                regHi = OBJECT_REG_HI(src->u.reg.object);
                                reg = OBJECT_REG(src->u.reg.object);
                                retain_GPR_pair(src->u.reg.object, reg, regHi);
                            } else {
                                retain_register(src->u.reg.object, dest->arg, dest->data.reg.reg);
                            }
                        } else {
                            retain_register(NULL, dest->arg, dest->data.reg.reg);
                        }
                    }

                    break;
                }

                case IAOpnd_Lab:
                    if (!src->u.lab.label->pclabel) {
                        src->u.lab.label->pclabel = makepclabel();
                    }
                    dest->kind = PCOp_LABEL;
                    dest->data.label.label = src->u.lab.label->pclabel;
                    break;

                case IAOpnd_GlobalObj:
                case IAOpnd_LocalObj:
                    dest->kind = PCOp_MEMORY;
                    dest->arg = src->u.obj.reftype;
                    dest->data.mem.obj = src->u.obj.obj;
                    dest->data.mem.offset = src->u.obj.offset;
                    if (pc->flags & (fIsRead | fIsWrite | fOpFromMemory | fOpToMemory)) {
                        pc->alias = make_alias(
                            dest->data.mem.obj,
                            dest->data.mem.offset,
                            nbytes_loaded_or_stored_by(pc)
                        );
                        if (is_volatile_object(dest->data.mem.obj)) {
                            pc->flags |= fIsVolatile;
                        }
                        if (OBJ_GET_TARGET_CONST(dest->data.mem.obj)) {
                            pc->flags |= fIsConst;
                        }
                    }
                    break;

                case IAOpnd_LabDiff:
                    if (src->u.labdiff.label1->pclabel == NULL) {
                        src->u.labdiff.label1->pclabel = makepclabel();
                    }
                    if (src->u.labdiff.label2->pclabel == NULL) {
                        src->u.labdiff.label2->pclabel = makepclabel();
                    }
                    if (pc->flags & (fIsRead | fIsWrite)) {
                        pc->flags |= fIsPtrOp;
                    }
                    dest->kind = PCOp_LABELDIFF;
                    dest->data.labeldiff.labelA = src->u.labdiff.label1->pclabel;
                    dest->data.labeldiff.labelB = src->u.labdiff.label2->pclabel;
                    dest->arg = src->negated;
                    dest->data.labeldiff.offset = src->u.labdiff.offset;
                    break;

                default:
                    CError_FATAL(3669);
            }
        }

        index++;
        dest++;
        src++;
    }

    if (ia->opcode == PC_STMW || ia->opcode == PC_LMW) {
        int i;
        for (i = reg; i < 32; i++, dest++) {
            dest->kind = PCOp_REGISTER;
            dest->arg = RegClass_GPR;
            dest->data.reg.reg = i;
            dest->data.reg.effect = (ia->opcode == PC_LMW) ? 2 : 1;
        }
    }

    if (ia->flags & IAFlag_Call) {
        UInt32 masks[5] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
        branch_record_volatiles(dest, masks);
        if (copts.exceptions && current_statement && assembler_type == AssemblerType_Statements) {
            noteexceptionactionregisters(current_statement->dobjstack, dest);
        }
    }

    return pc;
}

void InlineAsm_TranslateIRtoPCode(Statement* stmt) {
    InlineAsm* ia;
    PCode* pc;

    ia = (InlineAsm*)stmt->expr;
    pc = InlineAsm_TranslateIRtoPCodePPC(ia, opcodeinfo[ia->opcode].operand_count, assembler_type);
    appendpcode(pclastblock, pc);

    if ((ia->flags2 & IAFlagsB_Volatile) || !copts.optimizewithasm) {
        setpcodeflags(fSideEffects);
    }

    if (ia->flags2 & IAFlagsB_Record) {
        if (PCODE_FLAG_SET_F(pc) & fCanSetRecordBit) {
            pcsetrecordbit(pclastblock->lastPCode);
        } else {
            CError_Error(ERR_UNKNOWN_ASSEMBLER_INSTRUCTION_MNEMONIC);
        }
    }

    if (ia->flags2 & IAFlagsB_Overflow) {
        if (PCODE_FLAG_SET_F(pc) & fCanSetCarry) {
            setpcodeflags(fOverflow);
        } else {
            CError_Error(ERR_UNKNOWN_ASSEMBLER_INSTRUCTION_MNEMONIC);
        }
    }

    if (ia->flags2 & IAFlagsB_Absolute) {
        if (PCODE_FLAG_SET_T(pc) & fCanBeAbsolute) {
            int i;
            for (i = 0; i < pc->argCount; i++) {
                if (pc->args[i].kind == PCOp_LABEL) {
                    PPCError_Error(PPCERR_ASM_ABSOLUTE_BRANCH_TARGET);
                    break;
                }
            }
            setpcodeflags(fAbsolute);
        } else {
            CError_Error(ERR_UNKNOWN_ASSEMBLER_INSTRUCTION_MNEMONIC);
        }
    }

    if (ia->flags2 & IAFlagsB_Link) {
        if (PCODE_FLAG_SET_T(pc) & fCanLink) {
            pcsetlinkbit(pclastblock->lastPCode);
            if (!(ia->flags & IAFlag_Call)) {
                pclastblock->lastPCode->flags &= ~fIsCall;
                pclastblock->lastPCode->flags |= fIsBranch;
            }
            makes_call = TRUE;
        } else {
            CError_Error(ERR_UNKNOWN_ASSEMBLER_INSTRUCTION_MNEMONIC);
        }
    }

    if (ia->flags2 & IAFlagsB_HintPlus) {
        setpcodeflags(fBranchTaken);
    }
    if (ia->flags2 & IAFlagsB_HintMinus) {
        setpcodeflags(fBranchNotTaken);
    }

    if (OPCODE_PART_1(opcodeinfo[pc->op].insn) == 16) {
        PCodeLabel* dest = NULL;
        PCodeLabel* src = makepclabel();
        switch (pc->op) {
            case PC_BC:
                if (pc->args[3].kind == PCOp_LABEL) {
                    dest = pc->args[3].data.label.label;
                }
                break;
            case PC_BT:
            case PC_BF:
            case PC_BDNZT:
            case PC_BDNZF:
            case PC_BDZT:
            case PC_BDZF:
                if (pc->args[2].kind == PCOp_LABEL) {
                    dest = pc->args[2].data.label.label;
                }
                break;
            case PC_BDNZ:
            case PC_BDZ:
                if (pc->args[0].kind == PCOp_LABEL) {
                    dest = pc->args[0].data.label.label;
                }
                break;
            default:
                CError_FATAL(3856);
        }

        if (dest) {
            pcbranch(pclastblock, dest);
            pcbranch(pclastblock, src);
            makepcblock();
            pclabel(pclastblock, src);
        }
    } else if (OPCODE_PART_1(opcodeinfo[pc->op].insn) == 18) {
        PCodeLabel* label;
        if (ia->flags2 & IAFlagsB_Link) {
            label = makepclabel();
            pcbranch(pclastblock, label);
        }
        if (pc->args[0].kind == PCOp_LABEL) {
            pcbranch(pclastblock, pc->args[0].data.label.label);
        }
        makepcblock();
        if (ia->flags2 & IAFlagsB_Link) {
            pclabel(pclastblock, label);
        }
    } else if (OPCODE_PART_1(opcodeinfo[pc->op].insn) == 19) {
        switch (pc->op) {
            case PC_BLR:
            case PC_RFI:
                if (asm_alloc_flags[3]) {
                    asm_alloc_flags[9] = TRUE;
                } else {
                    asm_alloc_flags[8] = TRUE;
                }

                if (asm_alloc_flags[4]) {
                    asm_alloc_flags[5] = TRUE;
                }

                if (pc->op == PC_BLR) {
                    asm_alloc_flags[6] = TRUE;
                } else {
                    asm_alloc_flags[7] = TRUE;
                }
                break;
        }
    }
}

// Dead: only InlineAsm.c's dead InlineAsm_DumpStatement calls it
const char* InlineAsm_GetMnemonic(InlineAsm* ia) {
    return opcodeinfo[ia->opcode].name;
}

static SInt32 InlineAsm_OpcodeSize(InlineAsm* ia) {
    OpcodeInfo* info = &opcodeinfo[ia->opcode];

    if (info->flags & (fIsRead | fIsWrite)) {
        switch (ia->opcode) {
            case PC_LBZ:
            case PC_LBZU:
            case PC_LBZX:
            case PC_LBZUX:
            case PC_STB:
            case PC_STBU:
            case PC_STBX:
            case PC_STBUX:
                return 1;
            case PC_LHZ:
            case PC_LHZU:
            case PC_LHZX:
            case PC_LHZUX:
            case PC_LHA:
            case PC_LHAU:
            case PC_LHAX:
            case PC_LHAUX:
            case PC_LHBRX:
            case PC_STH:
            case PC_STHU:
            case PC_STHX:
            case PC_STHUX:
            case PC_STHBRX:
                return 2;
            case PC_LWZ:
            case PC_LWZU:
            case PC_LWZX:
            case PC_LWZUX:
            case PC_LWBRX:
            case PC_STW:
            case PC_STWU:
            case PC_STWX:
            case PC_STWUX:
            case PC_STWBRX:
            case PC_LFS:
            case PC_LFSU:
            case PC_LFSX:
            case PC_LFSUX:
            case PC_STFS:
            case PC_STFSU:
            case PC_STFSX:
            case PC_STFSUX:
            case PC_LWARX:
            case PC_STFIWX:
            case PC_STWCX:
            case PC_ECIWX:
            case PC_ECOWX:
            case PC_MFROM:
            case PC_LSCBX:
                return 4;
            case PC_LMW:
            case PC_STMW:
                if (ia->args[0].type == IAOpnd_Reg && ia->args[0].u.reg.object == NULL) {
                    return (32 - ia->args[0].u.reg.num) * 4;
                } else {
                    return 128;
                }
            case PC_LFD:
            case PC_LFDU:
            case PC_LFDX:
            case PC_LFDUX:
            case PC_STFD:
            case PC_STFDU:
            case PC_STFDX:
            case PC_STFDUX:
                return 8;
            case PC_LSWI:
            case PC_STSWI:
                return ia->args[2].u.imm.value;
            case PC_LSWX:
            case PC_STSWX:
                return 128;
            case PC_LVEBX:
            case PC_STVEBX:
                return 1;
            case PC_LVEHX:
            case PC_STVEHX:
                return 2;
            case PC_LVEWX:
            case PC_STVEWX:
                return 4;
            case PC_LVSL:
            case PC_LVSR:
            case PC_LVX:
            case PC_LVXL:
            case PC_STVX:
            case PC_STVXL:
                return 16;
            default:
                CError_FATAL(4065);
        }
    } else {
        if (info->flags & fOpTypeGPR) {
            return 4;
        }
        if (info->flags & fOpTypeFPR) {
            return 8;
        }
        if (info->flags & fOpTypeVR) {
            return 16;
        }

        if (info->flags & fSideEffects) {
            switch (ia->opcode) {
                case PC_TLBIE:
                case PC_TLBLD:
                case PC_TLBLI:
                    return 4;
                default:
                    CError_FATAL(4082);
            }
        }
    }

    CError_FATAL(4085);
    return 0;
}

void CodeGen_GetAsmEffects(Statement* stmt, IAEffects* effects) {
    InlineAsm* ia;
    OpcodeInfo* info;
    int i;
    IAOperand* op;
    VarInfo* vi;

    ia = (InlineAsm*)stmt->expr;
    info = &opcodeinfo[ia->opcode];

    effects->numoperands = 0;
    effects->numlabels = 0;
    effects->writesMemory = FALSE;
    effects->readsMemory = FALSE;
    effects->unmodeledControlFlow = FALSE;
    effects->branchWithLink = FALSE;
    effects->optimizationBarrier = FALSE;
    effects->noFallthrough = FALSE;

    if (!effects->numoperands && (info->flags & fIsRead)) {
        effects->readsMemory = TRUE;
    }
    if (!effects->numoperands && (info->flags & fIsWrite)) {
        effects->writesMemory = TRUE;
    }

    if (FLAG_SET_T(info->flags) & fCanLink) {
        if (ia->flags2 & IAFlagsB_Link) {
            effects->branchWithLink = TRUE;
        } else if ((info->flags & fIsCall) || (info->flags & fIsWrite)) {
            effects->unmodeledControlFlow = TRUE;
        }

        if (ia->opcode == PC_B) {
            if (ia->args[0].type == IAOpnd_Imm) {
                effects->optimizationBarrier = TRUE;
            }
            effects->noFallthrough = TRUE;
        }
    }

    if (info->flags & fSideEffects) {
        effects->optimizationBarrier = TRUE;
    }

    if (ia->opcode == PC_BC && (ia->flags2 & IAFlagsB_Link)) {
        effects->branchWithLink = TRUE;
    }

    for (i = 0, op = ia->args; i < ia->argcount; i++, op++) {
        switch (op->type) {
            case IAOpnd_Placeholder:
            case IAOpnd_Imm:
                break;
            case IAOpnd_Reg:
                if (op->u.reg.object) {
                    if ((vi = Registers_GetVarInfo(op->u.reg.object))) {
                        vi->flags |= VarInfoFlag_AsmNamed;
                    }
                    if (op->u.reg.effect & EffectRead) {
                        Type* type = op->u.reg.object->type;
                        if (TYPE_FITS_IN_REGISTER(type) ||
                            IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) ||
                            IS_TYPE_VECTOR(type)) {
                            effects->operands[effects->numoperands].type = IAEffect_Read;
                            effects->operands[effects->numoperands].object = op->u.reg.object;
                            effects->operands[effects->numoperands].offset = 0;
                            effects->operands[effects->numoperands].size =
                                op->u.reg.object->type->size;
                            effects->numoperands++;
                        } else if ((IS_TYPE_INT(type) && TYPE_INTEGRAL(type)->integral == IT_17) ||
                                   (IS_TYPE_INT(type) && TYPE_INTEGRAL(type)->integral == IT_18) ||
                                   IS_TYPE_VEC2X32FLOAT(type)) {
                            effects->operands[effects->numoperands].type = IAEffect_Read;
                            effects->operands[effects->numoperands].object = op->u.reg.object;
                            effects->operands[effects->numoperands].offset = 0;
                            effects->operands[effects->numoperands].size =
                                op->u.reg.object->type->size;
                            effects->numoperands++;
                        } else {
                            CError_FATAL(4196);
                        }
                    }
                }
                break;
            case IAOpnd_GlobalObj:
            case IAOpnd_LocalObj:
                if (op->u.obj.obj) {
                    if (info->flags & fIsRead) {
                        effects->operands[effects->numoperands].type = IAEffect_Read;
                        effects->operands[effects->numoperands].object = op->u.obj.obj;
                        effects->operands[effects->numoperands].offset = op->u.obj.offset;
                        effects->operands[effects->numoperands].size = InlineAsm_OpcodeSize(ia);
                        effects->numoperands++;
                    } else if (!(info->flags & (fIsBranch | fIsCall))) {
                        effects->operands[effects->numoperands].type = IAEffect_AddressOf;
                        effects->operands[effects->numoperands].object = op->u.obj.obj;
                        effects->operands[effects->numoperands].offset = op->u.obj.offset;
                        effects->operands[effects->numoperands].size = InlineAsm_OpcodeSize(ia);
                        effects->numoperands++;
                    }
                }
                break;
            case IAOpnd_Lab:
                effects->labels[effects->numlabels] = op->u.lab.label;
                effects->numlabels++;
                break;
            case IAOpnd_LabDiff:
                effects->labels[effects->numlabels] = op->u.labdiff.label1;
                effects->numlabels++;
                effects->labels[effects->numlabels] = op->u.labdiff.label2;
                effects->numlabels++;
                effects->unmodeledControlFlow = TRUE;
                break;
            default:
                CError_FATAL(4232);
        }

        CError_ASSERT(4235, (UInt32)effects->numoperands <= IAMaxOperands);
        CError_ASSERT(4238, (UInt32)effects->numlabels <= IAMaxLabels);
    }

    for (i = 0, op = ia->args; i < ia->argcount; i++, op++) {
        switch (op->type) {
            case IAOpnd_Reg:
                if (op->u.reg.object) {
                    if ((vi = Registers_GetVarInfo(op->u.reg.object))) {
                        vi->flags |= VarInfoFlag_AsmNamed;
                    }
                    if (op->u.reg.effect & EffectWrite) {
                        Type* type = op->u.reg.object->type;
                        if (TYPE_FITS_IN_REGISTER(type) ||
                            IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type) ||
                            IS_TYPE_VECTOR(type)) {
                            effects->operands[effects->numoperands].type = IAEffect_Write;
                            effects->operands[effects->numoperands].object = op->u.reg.object;
                            effects->operands[effects->numoperands].offset = 0;
                            effects->operands[effects->numoperands].size =
                                op->u.reg.object->type->size;
                            effects->numoperands++;
                        } else if ((IS_TYPE_INT(type) && TYPE_INTEGRAL(type)->integral == IT_17) ||
                                   (IS_TYPE_INT(type) && TYPE_INTEGRAL(type)->integral == IT_18) ||
                                   IS_TYPE_VEC2X32FLOAT(type)) {
                            effects->operands[effects->numoperands].type = IAEffect_Write;
                            effects->operands[effects->numoperands].object = op->u.reg.object;
                            effects->operands[effects->numoperands].offset = 0;
                            effects->operands[effects->numoperands].size =
                                op->u.reg.object->type->size;
                            effects->numoperands++;
                        } else {
                            CError_FATAL(4277);
                        }
                    }
                }
                break;
            case IAOpnd_GlobalObj:
            case IAOpnd_LocalObj:
                if (op->u.obj.obj) {
                    if (info->flags & fIsWrite) {
                        effects->operands[effects->numoperands].type = IAEffect_Write;
                        effects->operands[effects->numoperands].object = op->u.obj.obj;
                        effects->operands[effects->numoperands].offset = op->u.obj.offset;
                        effects->operands[effects->numoperands].size = InlineAsm_OpcodeSize(ia);
                        effects->numoperands++;
                    }
                }
                break;
            default:
                break;
        }

        CError_ASSERT(4296, (UInt32)effects->numoperands <= IAMaxOperands);
    }

    if ((info->flags & (fIsBranch | fIsCall)) && effects->numlabels == 0) {
        effects->unmodeledControlFlow = TRUE;
    }
}

void CodeGen_PropagateIntoAsm(Statement* stmt, Object* obj, ENode* expr) {
    InlineAsm* ia;
    Object* newobj;

    ia = (InlineAsm*)stmt->expr;
    if (ENODE_IS(expr, EOBJREF)) {
        newobj = expr->data.objref;
        if (obj->otype == newobj->otype && obj->datatype == newobj->datatype) {
            int i;
            for (i = 0; i < ia->argcount; i++) {
                switch (ia->args[i].type) {
                    case IAOpnd_Reg:
                        if (ia->args[i].u.reg.object == obj &&
                            (ia->args[i].u.reg.effect & (EffectRead | EffectWrite)) == EffectRead) {
                            if (TYPE_FITS_IN_REGISTER(newobj->type) &&
                                ia->args[i].u.reg.rclass == RegClass_GPR) {
                                ia->args[i].u.reg.object = newobj;
                            } else if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(newobj->type) &&
                                       ia->args[i].u.reg.rclass == RegClass_FPR) {
                                ia->args[i].u.reg.object = newobj;
                            } else if (IS_TYPE_VECTOR(newobj->type) &&
                                       ia->args[i].u.reg.rclass == RegClass_VR) {
                                ia->args[i].u.reg.object = newobj;
                            }
                        }
                        break;
                    case IAOpnd_GlobalObj:
                    case IAOpnd_LocalObj:
                        if (!(opcodeinfo[ia->opcode].flags & (fIsWrite | fOpToMemory)) &&
                            ia->args[i].u.obj.obj == obj) {
                            ia->args[i].u.obj.obj = newobj;
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }
}

Statement* CodeGen_CopyAsmStat(Statement* stmt) {
    Statement* copy;
    InlineAsm* ia;
    SInt32 size;
    InlineAsm* iacopy;

    copy = galloc(sizeof(Statement));
    *copy = *stmt;

    ia = (InlineAsm*)stmt->expr;
    size = sizeof(InlineAsm) + sizeof(IAOperand) * ia->argcount;
    iacopy = galloc(size);
    memcpy(iacopy, ia, size);
    copy->expr = (ENode*)iacopy;

    return copy;
}
