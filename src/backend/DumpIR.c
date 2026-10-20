#include "compiler/DumpIR.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/objects.h"
#include "compiler/Switch.h"
#include "compiler/types.h"
#include <stdio.h>
#include <string.h>

static FILE* outfile;
static char unknown_statics[8];

static void spell(Type* type, char* buf);
void DumpType(Type* type);

static void WritePString(FILE* file, char* str, int len) {
    while (len--) {
        switch (*str) {
            case 0:
                fputs("\\x00", file);
                break;
            case 7:
                fputs("\\a", file);
                break;
            case 8:
                fputs("\\b", file);
                break;
            case 12:
                fputs("\\f", file);
                break;
            case 10:
                fputs("\\n", file);
                break;
            case 13:
                fputs("\\r", file);
                break;
            case 9:
                fputs("\\t", file);
                break;
            case 11:
                fputs("\\v", file);
                break;
            case '"':
            case '\'':
            case '?':
            case '\\':
                fputc('\\', file);
            default:
                fputc(*str, file);
                break;
        }
        str++;
    }
}

static void WriteCString(FILE* file, char* str) {
    WritePString(file, str, strlen(str));
}

static void StaticSetupDumpIR(void) {}

void SetupDumpIR(void) {
    StaticSetupDumpIR();
}

void CleanupDumpIR(void) {
    return;
}

void DumpIR(Statement* statements, Object* func) {}

void DumpExpression(ENode* expr, int indent) {
    static int bt;
    static int i;
    static char* nodenames[] = {
        "EPOSTINC",
        "EPOSTDEC",
        "EPREINC",
        "EPREDEC",
        "EINDIRECT",
        "EMONMIN",
        "EBINNOT",
        "ELOGNOT",
        "EFORCELOAD",
        "EMUL",
        "EMULV",
        "EDIV",
        "EMODULO",
        "EADDV",
        "ESUBV",
        "EADD",
        "ESUB",
        "ESHL",
        "ESHR",
        "ELESS",
        "EGREATER",
        "ELESSEQU",
        "EGREATEREQU",
        "EEQU",
        "ENOTEQU",
        "EAND",
        "EXOR",
        "EOR",
        "ELAND",
        "ELOR",
        "EASS",
        "EMULASS",
        "EDIVASS",
        "EMODASS",
        "EADDASS",
        "ESUBASS",
        "ESHLASS",
        "ESHRASS",
        "EANDASS",
        "EXORASS",
        "EORASS",
        "ECOMMA",
        "EPMODULO",
        "EROTL",
        "EROTR",
        "EBCLR",
        "EBTST",
        "EBSET",
        "ETYPCON",
        "EBITFIELD",
        "EINTCONST",
        "EFLOATCONST",
        "ESTRINGCONST",
        "ECOND",
        "EFUNCCALL",
        "EFUNCCALLP",
        "EOBJREF",
        "EMFPOINTER",
        "ENULLCHECK",
        "EPRECOMP",
        "ETEMP",
        "EARGOBJ",
        "ELOCOBJ",
        "ELABEL",
        "ESETCONST",
        "ENEWEXCEPTION",
        "ENEWEXCEPTIONARRAY",
        "EINITTRYCATCH",
        "EOBJLIST",
        "EMEMBER",
        "ETEMPLDEP",
        "EINSTRUCTION",
        "EDEFINE",
        "EREUSE",
        "EASSBLK",
        "EVECTOR128CONST",
        "ECONDASS"
    };
    char buf[64];
    ENodeList* list;

    while (1) {
        for (i = 0; i < indent; i++) {
            fputc('\t', outfile);
        }

        if (expr->flags) {
            fprintf(outfile, "%s {%02X}", nodenames[expr->type], expr->flags);
        } else {
            fprintf(outfile, "%s", nodenames[expr->type]);
        }

        switch (expr->type) {
            case EINTCONST:
                if (expr->rtype->size > 4) {
                    fprintf(outfile, "[0x%.8lX%.8lX]", expr->data.intval.hi, expr->data.intval.lo);
                } else {
                    fprintf(outfile, "[%ld]", expr->data.intval.lo);
                }
                DumpType(expr->rtype);
                fprintf(outfile, "\r\n");
                return;

            case EFLOATCONST:
                CMach_PrintFloat(buf, expr->data.floatval);
                fprintf(outfile, "[%s]", buf);
                DumpType(expr->rtype);
                fprintf(outfile, "\r\n");
                return;

            case ESTRINGCONST:
                if (expr->data.string.ispascal) {
                    fputs("[\"", outfile);
                    WritePString(outfile, expr->data.string.data, expr->data.string.size);
                    fputs("\"]", outfile);
                } else {
                    fputs("[\"", outfile);
                    WriteCString(outfile, expr->data.string.data);
                    fputs("\"]", outfile);
                }
                DumpType(expr->rtype);
                fprintf(outfile, "\r\n");
                return;

            case EVECTOR128CONST:
                fprintf(
                    outfile,
                    "[0x%.8lX%.8lX%.8lX%.8lX]",
                    expr->data.vector128val.ul[0],
                    expr->data.vector128val.ul[1],
                    expr->data.vector128val.ul[2],
                    expr->data.vector128val.ul[3]
                );
                DumpType(expr->rtype);
                fprintf(outfile, "\r\n");
                return;

            case ECOND:
            case ECONDASS:
                DumpType(expr->rtype);
                fprintf(outfile, "\r\n");
                DumpExpression(expr->data.cond.cond, indent + 1);
                DumpExpression(expr->data.cond.expr1, indent + 1);
                expr = expr->data.cond.expr2;
                indent++;
                break;

            case EFUNCCALL:
            case EFUNCCALLP:
                DumpType(expr->rtype);
                fprintf(outfile, "\r\n");
                DumpExpression(expr->data.funccall.funcref, indent + 1);
                for (list = expr->data.funccall.args; list; list = list->next) {
                    DumpExpression(list->node, indent + 1);
                }
                return;

            case EOBJREF:
                switch (expr->data.objref->datatype) {
                    case DFUNC:
                        fprintf(outfile, "[%s{PR}]", CMangler_GetLinkName(expr->data.objref)->name);
                        break;
                    case DVFUNC:
                        fprintf(outfile, "[%s{TD}]", CMangler_GetLinkName(expr->data.objref)->name);
                        break;
                    case DDATA:
                        fprintf(outfile, "[%s{RW}]", CMangler_GetLinkName(expr->data.objref)->name);
                        break;
                    default:
                        fprintf(outfile, "[%s]", expr->data.objref->name->name);
                        break;
                }
                DumpType(expr->rtype);
                fprintf(outfile, "\r\n");
                return;

            case EMUL:
            case EMULV:
            case EDIV:
            case EMODULO:
            case EADDV:
            case ESUBV:
            case EADD:
            case ESUB:
            case ESHL:
            case ESHR:
            case ELESS:
            case EGREATER:
            case ELESSEQU:
            case EGREATEREQU:
            case EEQU:
            case ENOTEQU:
            case EAND:
            case EXOR:
            case EOR:
            case ELAND:
            case ELOR:
            case EASS:
            case EMULASS:
            case EDIVASS:
            case EMODASS:
            case EADDASS:
            case ESUBASS:
            case ESHLASS:
            case ESHRASS:
            case EANDASS:
            case EXORASS:
            case EORASS:
            case ECOMMA:
            case EPMODULO:
            case EROTL:
            case EROTR:
            case EBTST:
                DumpType(expr->rtype);
                fprintf(outfile, "\r\n");
                DumpExpression(expr->data.diadic.left, indent + 1);
                expr = expr->data.diadic.right;
                indent++;
                break;

ENODE_CASE_MONADIC:
                DumpType(expr->rtype);
                fprintf(outfile, "\r\n");
                expr = expr->data.monadic;
                indent++;
                break;

            case EMFPOINTER:
                DumpType(expr->rtype);
                fprintf(outfile, "\r\n");
                DumpExpression(expr->data.mfpointer.accessnode, indent + 1);
                expr = expr->data.mfpointer.mfpointer;
                indent++;
                break;

            case ENULLCHECK:
                fprintf(outfile, " unique [%ld]", expr->data.nullcheck.precompid);
                DumpType(expr->rtype);
                fprintf(outfile, "\r\n");
                DumpExpression(expr->data.nullcheck.nullcheckexpr, indent + 1);
                expr = expr->data.nullcheck.condexpr;
                indent++;
                break;

            case EPRECOMP:
                fprintf(outfile, " unique [%ld]", expr->data.precompid);
                DumpType(expr->rtype);
                fprintf(outfile, "\r\n");
                return;

            case ELABEL:
                fprintf(outfile, "[%s]", expr->data.label->uniquename->name);
                DumpType(expr->rtype);
                fprintf(outfile, "\r\n");
                return;

            case ETEMP:
                DumpType(expr->data.temp.type);
                fprintf(outfile, "\r\n");
                return;

            case EINITTRYCATCH:
                DumpType(expr->rtype);
                fprintf(outfile, "\r\n");
                if (expr->data.itc.initexpr) {
                    DumpExpression(expr->data.itc.initexpr, indent + 1);
                }
                if (expr->data.itc.tryexpr) {
                    DumpExpression(expr->data.itc.tryexpr, indent + 1);
                }
                if (expr->data.itc.catchexpr) {
                    DumpExpression(expr->data.itc.catchexpr, indent + 1);
                }
                if (expr->data.itc.result) {
                    DumpExpression(expr->data.itc.result, indent + 1);
                }
                return;

            case EDEFINE:
                fprintf(outfile, "[%.8lX]", (unsigned long)expr);
                DumpType(expr->rtype);
                fputs("\r\n", outfile);
                expr = expr->data.monadic;
                indent++;
                break;

            case EREUSE:
                fprintf(outfile, "[%.8lX]", (unsigned long)expr->data.monadic);
                DumpType(expr->rtype);
                fputs("\r\n", outfile);
                return;

            default:
                return;
        }
    }
}

void DumpSwitch(SwitchInfo* info) {
    char buf[32];
    SwitchCase* cs;

    for (cs = info->cases; cs; cs = cs->next) {
        CInt64_PrintDec(buf, cs->min);
        CInt64_PrintDec(buf, cs->min);
        fprintf(outfile, "\t\t%11s: %s\r\n", buf, cs->label->uniquename->name);
    }

    fprintf(outfile, "\t\t    default: %s\r\n", info->defaultlabel->uniquename->name);
}

void DumpType(Type* type) {
    char buf[256];

    spell(type, buf);
    fprintf(outfile, " (%s)", buf);
}

static void spell(Type* type, char* buf) {
    char mybuf[256];
    char mybuf2[256];

    switch (type->type) {
        case TYPEVOID:
            strcpy(buf, "void");
            break;
        case TYPEINT:
            switch (TYPE_INTEGRAL(type)->integral) {
                case IT_BOOL:
                    strcpy(buf, "bool");
                    break;
                case IT_CHAR:
                    strcpy(buf, "char");
                    break;
                case IT_WCHAR_T:
                    strcpy(buf, "wchar_t");
                    break;
                case IT_SCHAR:
                    strcpy(buf, "signed char");
                    break;
                case IT_UCHAR:
                    strcpy(buf, "unsigned char");
                    break;
                case IT_SHORT:
                    strcpy(buf, "short");
                    break;
                case IT_USHORT:
                    strcpy(buf, "unsigned short");
                    break;
                case IT_INT:
                    strcpy(buf, "int");
                    break;
                case IT_UINT:
                    strcpy(buf, "unsigned int");
                    break;
                case IT_LONG:
                    strcpy(buf, "long");
                    break;
                case IT_ULONG:
                    strcpy(buf, "unsigned long");
                    break;
                case IT_LONGLONG:
                    strcpy(buf, "long long");
                    break;
                case IT_ULONGLONG:
                    strcpy(buf, "unsigned long long");
                    break;
            }
            break;
        case TYPEFLOAT:
            switch (TYPE_INTEGRAL(type)->integral) {
                case IT_VEC2X32FLOAT:
                    strcpy(buf, "__vec2x32float__");
                    break;
                case IT_FLOAT:
                    strcpy(buf, "float");
                    break;
                case IT_SHORTDOUBLE:
                    strcpy(buf, "short double");
                    break;
                case IT_DOUBLE:
                    strcpy(buf, "double");
                    break;
                case IT_LONGDOUBLE:
                    strcpy(buf, "long double");
                    break;
            }
            break;
        case TYPEENUM:
            strcpy(buf, "enum ");
            if (TYPE_ENUM(type)->enumname) {
                strcat(buf, TYPE_ENUM(type)->enumname->name);
            }
            break;
        case TYPESTRUCT:
            if (IS_TYPESTRUCT_VECTOR(TYPE_STRUCT(type))) {
                switch (TYPE_STRUCT(type)->stype) {
                    case STRUCT_VECTOR_UCHAR:
                        strcpy(buf, "vector unsigned char ");
                        break;
                    case STRUCT_VECTOR_SCHAR:
                        strcpy(buf, "vector signed char ");
                        break;
                    case STRUCT_VECTOR_BCHAR:
                        strcpy(buf, "vector bool char ");
                        break;
                    case STRUCT_VECTOR_USHORT:
                        strcpy(buf, "vector unsigned short ");
                        break;
                    case STRUCT_VECTOR_SSHORT:
                        strcpy(buf, "vector signed short ");
                        break;
                    case STRUCT_VECTOR_BSHORT:
                        strcpy(buf, "vector bool short ");
                        break;
                    case STRUCT_VECTOR_UINT:
                        strcpy(buf, "vector unsigned int ");
                        break;
                    case STRUCT_VECTOR_SINT:
                        strcpy(buf, "vector signed int ");
                        break;
                    case STRUCT_VECTOR_BINT:
                        strcpy(buf, "vector bool int ");
                        break;
                    case STRUCT_VECTOR_FLOAT:
                        strcpy(buf, "vector float ");
                        break;
                    case STRUCT_VECTOR_PIXEL:
                        strcpy(buf, "vector pixel ");
                        break;
                }
            } else {
                strcpy(buf, "struct ");
                if (TYPE_STRUCT(type)->name) {
                    strcat(buf, TYPE_STRUCT(type)->name->name);
                }
            }
            break;
        case TYPECLASS:
            strcpy(buf, "class ");
            if (TYPE_CLASS(type)->classname) {
                strcat(buf, TYPE_CLASS(type)->classname->name);
            }
            break;
        case TYPEFUNC:
            spell(TYPE_FUNC(type)->functype, mybuf);
            strcpy(buf, "freturns(");
            strcat(buf, mybuf);
            strcat(buf, ")");
            break;
        case TYPEBITFIELD:
            spell(TYPE_BITFIELD(type)->bitfieldtype, mybuf);
            sprintf(
                buf,
                "bitfield(%s){%d:%d}",
                mybuf,
                TYPE_BITFIELD(type)->offset,
                TYPE_BITFIELD(type)->bitlength
            );
            break;
        case TYPELABEL:
            strcpy(buf, "label");
            break;
        case TYPEPOINTER:
            spell(TPTR_TARGET(type), mybuf);
            strcpy(buf, "pointer(");
            strcat(buf, mybuf);
            strcat(buf, ")");
            break;
        case TYPEARRAY:
            spell(TPTR_TARGET(type), mybuf);
            strcpy(buf, "array(");
            strcat(buf, mybuf);
            strcat(buf, ")");
            break;
        case TYPEMEMBERPOINTER:
            spell(TYPE_MEMBER_POINTER(type)->ty1, mybuf);
            spell(TYPE_MEMBER_POINTER(type)->ty2, mybuf2);
            strcpy(buf, "memberpointer(");
            strcat(buf, mybuf);
            strcat(buf, ",");
            strcat(buf, mybuf2);
            strcat(buf, ")");
            break;
        default:
            break;
    }
}

void DumpStack(ExceptionAction* act) {
    while (act) {
        fprintf(outfile, "\t\t:");
        switch (act->type) {
            case EAT_DESTROYLOCAL:
                fprintf(
                    outfile,
                    "EAT_DESTROYLOCAL %s(&%s)%s",
                    CMangler_GetLinkName(act->data.destroy_local.dtor)->name,
                    act->data.destroy_local.local->name->name,
                    "\r\n"
                );
                break;
            case EAT_DESTROYLOCALCOND:
                fprintf(outfile, "EAT_DESTROYLOCALCOND%s", "\r\n");
                break;
            case EAT_DESTROYLOCALOFFSET:
                fprintf(
                    outfile,
                    "EAT_DESTROYLOCALOFFSET %s(&%s+%ld)%s",
                    CMangler_GetLinkName(act->data.destroy_local_offset.dtor)->name,
                    act->data.destroy_local_offset.local->name->name,
                    act->data.destroy_local_offset.offset,
                    "\r\n"
                );
                break;
            case EAT_DESTROYLOCALPOINTER:
                fprintf(outfile, "EAT_DESTROYLOCALPOINTER%s", "\r\n");
                break;
            case EAT_DESTROYLOCALARRAY:
                fprintf(outfile, "EAT_DESTROYLOCALARRAY%s", "\r\n");
                break;
            case EAT_DESTROYBASE:
                fprintf(
                    outfile,
                    "EAT_DESTROYBASE %s(this+%ld)%s",
                    CMangler_GetLinkName(act->data.destroy_base.dtor)->name,
                    act->data.destroy_base.offset,
                    "\r\n"
                );
                break;
            case EAT_DESTROYMEMBER:
                fprintf(
                    outfile,
                    "EAT_DESTROYMEMBER %s(%s+%ld)%s",
                    CMangler_GetLinkName(act->data.destroy_member.dtor)->name,
                    act->data.destroy_member.objectptr->name->name,
                    act->data.destroy_member.offset,
                    "\r\n"
                );
                break;
            case EAT_DESTROYMEMBERCOND:
                fprintf(
                    outfile,
                    "EAT_DESTROYMEMBERCOND if(%s) %s(this+%ld)%s",
                    act->data.destroy_member_cond.cond->name->name,
                    CMangler_GetLinkName(act->data.destroy_member_cond.dtor)->name,
                    act->data.destroy_member_cond.offset,
                    "\r\n"
                );
                break;
            case EAT_DESTROYMEMBERARRAY:
                fprintf(
                    outfile,
                    "EAT_DESTROYMEMBERARRAY %s(this+%ld)[%ld] size: %ld%s",
                    CMangler_GetLinkName(act->data.destroy_member_array.dtor)->name,
                    act->data.destroy_member_array.offset,
                    act->data.destroy_member_array.elements,
                    act->data.destroy_member_array.element_size,
                    "\r\n"
                );
                break;
            case EAT_DELETEPOINTER:
                fprintf(
                    outfile,
                    "EAT_DELETEPOINTER(%s)%s",
                    act->data.delete_pointer.pointerobject->name->name,
                    "\r\n"
                );
                break;
            case EAT_DELETELOCALPOINTER:
                fprintf(
                    outfile,
                    "EAT_DELETELOCALPOINTER(%s)%s",
                    act->data.delete_pointer.pointerobject->name->name,
                    "\r\n"
                );
                break;
            case EAT_DELETEPOINTERCOND:
                fprintf(
                    outfile,
                    "EAT_DELETEPOINTERCOND if (%s)(%s)%s",
                    act->data.delete_pointer_cond.cond->name->name,
                    act->data.delete_pointer_cond.pointerobject->name->name,
                    "\r\n"
                );
                break;
            case EAT_CATCHBLOCK:
                fprintf(outfile, "EAT_CATCHBLOCK ");
                if (act->data.catch_block.catch_type) {
                    if (act->data.catch_block.catch_object) {
                        fprintf(outfile, "[%s]", act->data.catch_block.catch_object->name->name);
                    } else {
                        fprintf(outfile, "[]");
                    }
                    DumpType(act->data.catch_block.catch_type);
                } else {
                    fprintf(outfile, "[...] ");
                }
                fprintf(
                    outfile,
                    " Label: %s%s",
                    act->data.catch_block.catch_label->uniquename->name,
                    "\r\n"
                );
                break;
            case EAT_SPECIFICATION:
                fprintf(outfile, "EAT_SPECIFICATION%s", "\r\n");
                break;
            case EAT_ACTIVECATCHBLOCK:
                fprintf(outfile, "EAT_ACTIVECATCHBLOCK%s", "\r\n");
                break;
            case EAT_TERMINATE:
                fprintf(outfile, "EAT_TERMINATE%s", "\r\n");
                break;
            default:
                break;
        }
        act = act->prev;
    }
}
