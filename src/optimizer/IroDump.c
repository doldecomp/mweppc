#include "compiler/IroDump.h"
#include "compiler/IroCSE.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroPropagate.h"
#include "compiler/IrOptimizer.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/BitVector.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/types.h"
#include "driver/CCompiler.h"
#include "driver/COSToolsCLT.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static FILE* DumpFile;
static char* nodenames[MAXEXPR];

char* IRO_NodeName(ENodeType nodetype) {
    return nodenames[nodetype];
}

void IRO_InitializeNodeNamesArray(void) {
    int i;
    for (i = 0; i < MAXEXPR; i++) {
        nodenames[i] = "";
    }

    nodenames[EPOSTINC] = "EPOSTINC";
    nodenames[EPOSTDEC] = "EPOSTDEC";
    nodenames[EPREINC] = "EPREINC";
    nodenames[EPREDEC] = "EPREDEC";
    nodenames[EINDIRECT] = "EINDIRECT";
    nodenames[EMONMIN] = "EMONMIN";
    nodenames[EBINNOT] = "EBINNOT";
    nodenames[ELOGNOT] = "ELOGNOT";
    nodenames[EFORCELOAD] = "EFORCELOAD";
    nodenames[EMUL] = "EMUL";
    nodenames[EMULV] = "EMULV";
    nodenames[EDIV] = "EDIV";
    nodenames[EMODULO] = "EMODULO";
    nodenames[EADDV] = "EADDV";
    nodenames[ESUBV] = "ESUBV";
    nodenames[EADD] = "EADD";
    nodenames[ESUB] = "ESUB";
    nodenames[ESHL] = "ESHL";
    nodenames[ESHR] = "ESHR";
    nodenames[ELESS] = "ELESS";
    nodenames[EGREATER] = "EGREATER";
    nodenames[ELESSEQU] = "ELESSEQU";
    nodenames[EGREATEREQU] = "EGREATEREQU";
    nodenames[EEQU] = "EEQU";
    nodenames[ENOTEQU] = "ENOTEQU";
    nodenames[EAND] = "EAND";
    nodenames[EXOR] = "EXOR";
    nodenames[EOR] = "EOR";
    nodenames[ELAND] = "ELAND";
    nodenames[ELOR] = "ELOR";
    nodenames[EASS] = "EASS";
    nodenames[EMULASS] = "EMULASS";
    nodenames[EDIVASS] = "EDIVASS";
    nodenames[EMODASS] = "EMODASS";
    nodenames[EADDASS] = "EADDASS";
    nodenames[ESUBASS] = "ESUBASS";
    nodenames[ESHLASS] = "ESHLASS";
    nodenames[ESHRASS] = "ESHRASS";
    nodenames[EANDASS] = "EANDASS";
    nodenames[EXORASS] = "EXORASS";
    nodenames[EORASS] = "EORASS";
    nodenames[ECOMMA] = "ECOMMA";
    nodenames[EPMODULO] = "EPMODULO";
    nodenames[EROTL] = "EROTL";
    nodenames[EROTR] = "EROTR";
    nodenames[EBCLR] = "EBCLR";
    nodenames[EBTST] = "EBTST";
    nodenames[EBSET] = "EBSET";
    nodenames[ETYPCON] = "ETYPCON";
    nodenames[EBITFIELD] = "EBITFIELD";
    nodenames[EINTCONST] = "EINTCONST";
    nodenames[EFLOATCONST] = "EFLOATCONST";
    nodenames[ESTRINGCONST] = "ESTRINGCONST";
    nodenames[ECOND] = "ECOND";
    nodenames[EFUNCCALL] = "EFUNCCALL";
    nodenames[EFUNCCALLP] = "EFUNCCALLP";
    nodenames[EOBJREF] = "EOBJREF";
    nodenames[EMFPOINTER] = "EMFPOINTER";
    nodenames[ENULLCHECK] = "ENULLCHECK";
    nodenames[EPRECOMP] = "EPRECOMP";
    nodenames[ETEMP] = "ETEMP";
    nodenames[EARGOBJ] = "EARGOBJ";
    nodenames[ELOCOBJ] = "ELOCOBJ";
    nodenames[ELABEL] = "ELABEL";
    nodenames[ESETCONST] = "ESETCONST";
    nodenames[ENEWEXCEPTION] = "ENEWEXCEPTION";
    nodenames[ENEWEXCEPTIONARRAY] = "ENEWEXCEPTIONARRAY";
    nodenames[EOBJLIST] = "EOBJLIST";
    nodenames[EMEMBER] = "EMEMBER";
    nodenames[ETEMPLDEP] = "ETEMPLDEP";
    nodenames[EINSTRUCTION] = "EINSTRUCTION";
    nodenames[EDEFINE] = "EDEFINE";
    nodenames[EREUSE] = "EREUSE";
    nodenames[EASSBLK] = "EASSBLK";
    nodenames[EVECTOR128CONST] = "EVECTOR128CONST";
    nodenames[ECONDASS] = "ECONDASS";
}

static void DumpENode(ENode* enode) {
    char buf[64];

    if (IRO_Log) {
        switch (enode->type) {
            case EOBJREF:
                fprintf(DumpFile, "%s", enode->data.objref->name->name);
                break;
            case EINTCONST:
                CInt64_PrintDec(buf, enode->data.intval);
                fprintf(DumpFile, "%s", buf);
                break;
            case EFLOATCONST:
                fprintf(DumpFile, "%g", enode->data.floatval.data.value);
                break;
            case EVECTOR128CONST:
                fprintf(
                    DumpFile,
                    "%.8lX%.8lX%.8lX%.8lX",
                    enode->data.vector128val.ul[0],
                    enode->data.vector128val.ul[1],
                    enode->data.vector128val.ul[2],
                    enode->data.vector128val.ul[3]
                );
                break;
            default:
                break;
        }
    }
}

static void DumpLinearNode(IROLinear* linear) {
    int i;

    if (!IRO_Log) {
        return;
    }

    fprintf(DumpFile, "%4d: ", linear->index);
    switch (linear->type) {
        case IROLinearNop:
            fprintf(DumpFile, "Nop");
            break;
        case IROLinearOperand:
            fprintf(DumpFile, "Operand ");
            DumpENode(linear->u.node);
            break;
        case IROLinearOp1Arg:
            fprintf(DumpFile, "%s %d", nodenames[linear->nodetype], linear->u.monadic->index);
            break;
        case IROLinearOp2Arg:
            fprintf(
                DumpFile,
                "%s %d %d",
                nodenames[linear->nodetype],
                linear->u.diadic.left->index,
                linear->u.diadic.right->index
            );
            break;
        case IROLinearGoto:
            fprintf(DumpFile, "Goto %s", linear->u.label.label->name->name);
            break;
        case IROLinearIf:
            fprintf(
                DumpFile, "If %d %s", linear->u.label.cond->index, linear->u.label.label->name->name
            );
            break;
        case IROLinearIfNot:
            fprintf(
                DumpFile,
                "IfNot %d %s",
                linear->u.label.cond->index,
                linear->u.label.label->name->name
            );
            break;
        case IROLinearReturn:
            fprintf(DumpFile, "Return ");
            if (linear->u.monadic) {
                fprintf(DumpFile, "%d", linear->u.monadic->index);
            }
            break;
        case IROLinearLabel:
            fprintf(DumpFile, "Label %s", linear->u.label.label->name->name);
            break;
        case IROLinearSwitch:
            fprintf(DumpFile, "Switch %d", linear->u.swtch.cond->index);
            break;
        case IROLinearOp3Arg:
            fprintf(
                DumpFile,
                "%s %d %d %d",
                nodenames[linear->nodetype],
                linear->u.args3.a->index,
                linear->u.args3.b->index,
                linear->u.args3.c->index
            );
            break;
        case IROLinearFunccall:
            fprintf(DumpFile, "Funccall %d(", linear->u.funccall.callee->index);
            for (i = 0; i < linear->u.funccall.argCount; i++) {
                fprintf(DumpFile, "%d", linear->u.funccall.args[i]->index);
                if (i < (linear->u.funccall.argCount - 1)) {
                    fprintf(DumpFile, ",");
                }
            }
            fprintf(DumpFile, ")");
            break;
        case IROLinearBeginCatch:
            fprintf(DumpFile, "BeginCatch %d", linear->u.ctch.linear->index);
            break;
        case IROLinearEndCatch:
            fprintf(DumpFile, "EndCatch %d", linear->u.monadic->index);
            break;
        case IROLinearEndCatchDtor:
            fprintf(DumpFile, "EndCatchDtor %d", linear->u.monadic->index);
            break;
        case IROLinearEnd:
            fprintf(DumpFile, "End");
            break;
        default:
            break;
    }

    if (linear->flags & IROLF_Assigned) {
        fprintf(DumpFile, " <assigned>");
    }
    if (linear->flags & IROLF_Used) {
        fprintf(DumpFile, " <used>");
    }
    if (linear->flags & IROLF_Ind) {
        fprintf(DumpFile, " <ind>");
    }
    if (linear->flags & IROLF_Subs) {
        fprintf(DumpFile, " <subs>");
    }
    if (linear->flags & IROLF_LoopInvariant) {
        fprintf(DumpFile, " <loop invariant>");
    }
    if (linear->flags & IROLF_BeginLoop) {
        fprintf(DumpFile, " <begin loop>");
    }
    if (linear->flags & IROLF_EndLoop) {
        fprintf(DumpFile, " <end loop>");
    }
    if (linear->flags & IROLF_Ris) {
        fprintf(DumpFile, " <ris>");
    }
    if (linear->flags & IROLF_Immind) {
        fprintf(DumpFile, " <immind>");
    }
    if (linear->flags & IROLF_Reffed) {
        fprintf(DumpFile, " <reffed>");
    }
    if (linear->flags & IROLF_VecOp) {
        fprintf(DumpFile, " <vec op>");
    }
    if (linear->flags & IROLF_VecOpBase) {
        fprintf(DumpFile, " <vec op_base>");
    }
    if (linear->flags & IROLF_CounterLoop) {
        fprintf(DumpFile, " <counter loop>");
    }
    if (linear->flags & IROLF_BitfieldIndirect) {
        fprintf(DumpFile, " <bitfield_indirect>");
    }
    if (linear->flags & IROLF_CouldError) {
        fprintf(DumpFile, " <could_error>");
    }

    if (linear->rtype && CParser_IsVolatile(linear->rtype, linear->nodeflags & ENODE_FLAG_QUALS)) {
        fprintf(DumpFile, " <volatile>");
    }

    if (IS_LINEAR_ENODE(linear, EOBJREF)) {
        VarRecord* var = IRO_FindVar(linear->u.node->data.objref, FALSE, TRUE);
        if (var && is_volatile_object(var->object)) {
            fprintf(DumpFile, " <volatile obj>");
        }
    }

    fprintf(DumpFile, "\n");
}

static void DumpAct(IROLinear* linear, Boolean isFirst) {
    if (!isFirst) {
        DumpLinearNode(linear);
    }
}

void IRO_DumpIntTree(IROLinear* linear) {
    IRO_WalkTree(linear, DumpAct);
}

void IRO_DumpLinearList(IROLinear* linear) {
    if (!IRO_Log) {
        return;
    }

    while (linear) {
        DumpLinearNode(linear);
        linear = linear->next;
    }
    fprintf(DumpFile, "\n");
}

static void DumpList(int num, UInt16* list) {
    int i;

    if (IRO_Log) {
        for (i = 0; i < num; i++) {
            fprintf(DumpFile, "%d ", list[i]);
        }
        fprintf(DumpFile, "\n");
    }
}

void IRO_DumpBits(char* name, BitVector* bv) {
    SInt32 i;
    SInt32 rangeStart;
    Boolean inRange = FALSE;
    Boolean isFirst = TRUE;

    if (!IRO_Log) {
        return;
    }

    // The original's: the label is the format. clang's side prints it as a string (no label has a
    // conversion).
#ifdef __MWERKS__
    fprintf(DumpFile, name);
#else
    fprintf(DumpFile, "%s", name);
#endif
    if (!bv) {
        fprintf(DumpFile, "NULL");
    } else {
        for (i = 0; (UInt32)i < (bv->size * 32); i++) {
            if (Bv_IsBitSet(i, bv)) {
                if (!inRange) {
                    if (!isFirst) {
                        fputc(',', DumpFile);
                    }
                    isFirst = FALSE;
                    fprintf(DumpFile, "%d", (int)i);
                    inRange = TRUE;
                    rangeStart = i;
                }
            } else {
                if (inRange) {
                    inRange = FALSE;
                    if (i != (rangeStart + 1)) {
                        fprintf(DumpFile, "-%d", (int)(i - 1));
                    }
                }
            }
        }

        if (inRange && i != (rangeStart + 1)) {
            fprintf(DumpFile, "-%d", (int)(i - 1));
        }
    }

    fprintf(DumpFile, "\n");
}

// NOTE: invented names: str, dumpGraph
void IRO_DumpAfterPhase(char* str, Boolean dumpGraph) {
    if (dumpGraph) {
        IRO_Dump(
            "Dumping function %s after %s \n",
            FunctionName ? FunctionName->name->name : "Init-code",
            str
        );
        IRO_Dump(
            "--------------------------------------------------------------------------------\n"
        );
        IRO_DumpFlowgraph();
    }
}

void IRO_LogForFunction(char* name) {
    if (FunctionName) {
        if (!strcmp(FunctionName->name->name, name)) {
            IRO_Log = TRUE;
        } else {
            IRO_Log = FALSE;
        }
    }
}

void IRO_DumpFlowgraph(void) {
    IRONode* node;
    IROLinear* linear;

    if (!IRO_Log) {
        return;
    }
    if (!DumpFile) {
        return;
    }

    fprintf(DumpFile, "\nFlowgraph\n");
    for (node = IRO_FirstNode; node; node = node->nextnode) {
        fprintf(
            DumpFile,
            "Flowgraph node %d  First=%d, Last=%d\n",
            node->index,
            node->first->index,
            node->last->index
        );

        fprintf(DumpFile, "Succ = ");
        DumpList(node->numsucc, node->succ);
        fprintf(DumpFile, "Pred = ");
        DumpList(node->numpred, node->pred);

        fprintf(DumpFile, "MustReach = %d, MustReach1=%d\n", node->mustreach, node->mustreach1);
        fprintf(DumpFile, "LoopDepth = %d\n", node->loopdepth);

        IRO_DumpBits("Dom: ", node->dom);
        if ((linear = node->first)) {
            while (1) {
                DumpLinearNode(linear);
                if (linear == node->last) {
                    break;
                }
                linear = linear->next;
            }
        }

        fprintf(DumpFile, "\n\n");
    }
    fprintf(DumpFile, "\n");
    fflush(DumpFile);
}

void IRO_DumpNode(IRONode* node) {
    IROLinear* linear;

    if (IRO_Log) {
        if (!DumpFile) {
            return;
        }

        while (node) {
            fprintf(
                DumpFile,
                "Flowgraph node %d  First=%d, Last=%d\n",
                node->index,
                node->first->index,
                node->last->index
            );

            fprintf(DumpFile, "Succ = ");
            DumpList(node->numsucc, node->succ);
            fprintf(DumpFile, "Pred = ");
            DumpList(node->numpred, node->pred);

            fprintf(
                DumpFile, "MustReach = %d MustReach1 = %d\n", node->mustreach, node->mustreach1
            );
            fprintf(DumpFile, "LoopDepth = %d\n", node->loopdepth);

            IRO_DumpBits("Dom: ", node->dom);
            if ((linear = node->first)) {
                while (1) {
                    DumpLinearNode(linear);
                    if (linear == node->last) {
                        break;
                    }
                    linear = linear->next;
                }
            }

            fprintf(DumpFile, "\n\n");
            node = node->nextnode;
        }

        fprintf(DumpFile, "\n");
        fflush(DumpFile);
    }
}

void IRO_DumpAssignments(void) {
    IROAssign* assign;

    if (IRO_Log) {
        fprintf(DumpFile, "\nAssignments\n\n");
        for (assign = IRO_FirstAssign; assign; assign = assign->next) {
            fprintf(DumpFile, "%5d ", assign->index);
            DumpLinearNode(assign->linear);
            fprintf(DumpFile, "\n");
        }
        fprintf(DumpFile, "\n");
    }
}

void IRO_DumpVars(void) {
    VarRecord* var;

    if (IRO_Log) {
        fprintf(DumpFile, "\nVariables\n");
        for (var = IRO_FirstVar; var; var = var->next) {
            fprintf(
                DumpFile,
                "%5d %s %s\n",
                var->index,
                var->object->name->name,
                var->noregister ? "<addressed>" : ""
            );
        }
        fprintf(DumpFile, "\n");
    }
}

void IRO_DumpDf(void) {
    IRONode* node;

    if (IRO_Log) {
        for (node = IRO_FirstNode; node; node = node->nextnode) {
            fprintf(DumpFile, "Node %d\n", node->index);
            if (node->in) {
                IRO_DumpBits("In:   ", node->in);
            }
            if (node->gen) {
                IRO_DumpBits("Gen:  ", node->gen);
            }
            if (node->kill) {
                IRO_DumpBits("Kill: ", node->kill);
            }
            if (node->out) {
                IRO_DumpBits("Out:  ", node->out);
            }
            if (node->copyOut) {
                IRO_DumpBits("AA:   ", node->copyOut);
            }
            fprintf(DumpFile, "\n");
        }
        fprintf(DumpFile, "\n");
    }
}

void IRO_DumpExprs(void) {
    IROExpr* expr;

    if (!IRO_Log) {
        return;
    }

    fprintf(DumpFile, "Expressions\n\n");
    for (expr = IRO_FirstExpr; expr; expr = expr->next) {
        fprintf(
            DumpFile,
            "%4d: %d FN:%d CE:%d NS:%d ",
            expr->index,
            expr->linear->index,
            expr->node->index,
            expr->couldError,
            expr->notSubable
        );
        IRO_DumpBits("Depends: ", expr->depends);
        fprintf(DumpFile, "\n");
    }
    fprintf(DumpFile, "\n");
}

// The log is named after the source file, its extension replaced by ".log".
void IRO_SetupDump(void) {
    char name[256];
    int i;

    if (IRO_Log) {
        COS_FileGetPathName(name, &cparams.sourcefile, NULL);
        for (i = strlen(name); i >= 0; i--) {
            if (name[i] == '.') {
                name[i] = 0;
                break;
            }
        }
        strcat(name, ".log");
        if ((DumpFile = fopen(name, "wt")) == NULL) {
            IRO_Log = FALSE;
        }
    }
}

void IRO_CleanupDump(void) {
    if (DumpFile) {
        fclose(DumpFile);
    }
}

void IRO_Dump(char* format, ...) {
    va_list va;
    if (!IRO_Log) {
        return;
    }
    va_start(va, format);
    vfprintf(DumpFile, format, va);
    va_end(va);
}

void IRO_DumpAddr(IROAddrRecord* rec) {
    IROElmList* list;

    if (IRO_Log && DumpFile) {
        fprintf(DumpFile, "\n");
        fprintf(DumpFile, "Address  :\n");
        IRO_DumpIntTree(rec->linear);
        fprintf(DumpFile, "\n");
        fprintf(DumpFile, "BaseTerms:\n");
        for (list = rec->objRefs; list; list = list->next) {
            IRO_DumpIntTree(list->element);
            fprintf(DumpFile, "\n");
        }
        fprintf(DumpFile, "VarTerms:\n");
        for (list = rec->misc; list; list = list->next) {
            IRO_DumpIntTree(list->element);
            fprintf(DumpFile, "\n");
        }
        fprintf(DumpFile, "ConstTerms:\n");
        for (list = rec->ints; list; list = list->next) {
            IRO_DumpIntTree(list->element);
            fprintf(DumpFile, "\n");
        }
    }
}

static void IRO_DumpType(Type* type) {
    char buf[256];
    IRO_SpellType(type, buf);
    fprintf(DumpFile, " (%s)", buf);
}

void IRO_SpellType(Type* type, char* buf) {
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
                        strcpy(buf, "vector unsigned long ");
                        break;
                    case STRUCT_VECTOR_SINT:
                        strcpy(buf, "vector signed long ");
                        break;
                    case STRUCT_VECTOR_BINT:
                        strcpy(buf, "vector bool long ");
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
            }
            if (TYPE_STRUCT(type)->name) {
                strcat(buf, TYPE_STRUCT(type)->name->name);
            }
            break;
        case TYPECLASS:
            strcpy(buf, "class ");
            if (TYPE_CLASS(type)->classname) {
                strcat(buf, TYPE_CLASS(type)->classname->name);
            }
            break;
        case TYPEFUNC:
            IRO_SpellType(TYPE_FUNC(type)->functype, mybuf);
            strcpy(buf, "freturns(");
            strcat(buf, mybuf);
            strcat(buf, ")");
            break;
        case TYPEBITFIELD:
            IRO_SpellType(TYPE_BITFIELD(type)->bitfieldtype, mybuf);
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
            IRO_SpellType(TPTR_TARGET(type), mybuf);
            strcpy(buf, "pointer(");
            strcat(buf, mybuf);
            strcat(buf, ")");
            break;
        case TYPEARRAY:
            IRO_SpellType(TPTR_TARGET(type), mybuf);
            strcpy(buf, "array(");
            strcat(buf, mybuf);
            strcat(buf, ")");
            break;
        case TYPEMEMBERPOINTER:
            IRO_SpellType(TYPE_MEMBER_POINTER(type)->ty2, mybuf);
            IRO_SpellType(TYPE_MEMBER_POINTER(type)->ty1, mybuf2);
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
