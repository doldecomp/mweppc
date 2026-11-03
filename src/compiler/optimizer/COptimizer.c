#include "compiler/COptimizer.h"
#include "compiler/IrOptimizer.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/InlineAsm.h"
#include "compiler/objects.h"
#include "compiler/Switch.h"
#include "compiler/types.h"

typedef struct COptBlock COptBlock;
typedef struct COptCSE COptCSE;
typedef struct COptCSEList COptCSEList;

Boolean copt_isleaffunction;
static Boolean stmtchanged;
static short setbytes;
static COptBlock* currentblock;
static ENode* mexpr;
static COptCSE* csenodes[MAXEXPR];
static COptCSEList* cselist;
static short extravars;
static Boolean cse_found;
static Boolean cse_invals;
static Boolean static_for_inlines;
static short replaces;

// clang-format off
static short bitmasks[] = {
    1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80,
    0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000
};
// clang-format on

static CLabel* finallabel(CLabel* label, Statement* stmt) {
    Statement* scan;

    for (scan = label->stmt; scan; scan = scan->next) {
        if (scan->type > ST_LABEL) {
            if (scan == stmt) {
                return label;
            }
            if (scan->type == ST_GOTO) {
                if (scan->label != label) {
                    stmtchanged = TRUE;
                }
                return scan->label;
            }
            return label;
        }
    }

    return label;
}

static void optimizegoto(Statement* stmt) {
    Statement* scan;

    for (scan = stmt->next; scan; scan = scan->next) {
        if (stmt->label->stmt == scan) {
            stmt->type = ST_NOP;
            stmtchanged = TRUE;
            return;
        }
        if (scan->type > ST_LABEL) {
            break;
        }
    }

    stmt->label = finallabel(stmt->label, stmt);
}

static void removeif(Statement* stmt, Boolean condValue) {
    if (((stmt->type == ST_IFGOTO) && condValue) || ((stmt->type == ST_IFNGOTO) && !condValue)) {
        Statement* copy = lalloc(sizeof(Statement));
        *copy = *stmt;
        copy->type = ST_GOTO;
        stmt->next = copy;
    }
    stmt->type = ST_EXPRESSION;
    stmtchanged = TRUE;
}

static void optimizeif(Statement* stmt) {
    Statement* scan;
    Statement* jump;
    Boolean sawLabel;

    if (iszero(stmt->expr)) {
        removeif(stmt, FALSE);
        return;
    }
    if (isnotzero(stmt->expr)) {
        removeif(stmt, TRUE);
        return;
    }

    for (scan = stmt->next, sawLabel = FALSE; scan; scan = scan->next) {
        if (scan->type > ST_LABEL) {
            if (scan->type == ST_GOTO) {
                if (scan->label == stmt->label) {
                    stmt->type = ST_EXPRESSION;
                    stmtchanged = TRUE;
                    return;
                }

                if (!sawLabel) {
                    jump = scan;
                    for (scan = scan->next; scan; scan = scan->next) {
                        if (scan->type > ST_LABEL) {
                            break;
                        }
                        if (stmt->label->stmt == scan) {
                            stmt->label = jump->label;
                            jump->type = ST_NOP;
                            if (stmt->type == ST_IFGOTO) {
                                stmt->type = ST_IFNGOTO;
                            } else {
                                stmt->type = ST_IFGOTO;
                            }
                            stmtchanged = TRUE;
                            stmt->label = finallabel(stmt->label, stmt);
                            return;
                        }
                    }
                }
            } else if (scan->type == ST_RETURN && !scan->expr && !static_for_inlines && !sawLabel) {
                jump = scan;
                for (scan = scan->next; scan; scan = scan->next) {
                    if (scan->type > ST_LABEL) {
                        break;
                    }
                    if (stmt->label->stmt == scan) {
                        stmt->label = cleanreturnlabel;
                        needs_cleanup = TRUE;
                        jump->type = ST_NOP;
                        if (stmt->type == ST_IFGOTO) {
                            stmt->type = ST_IFNGOTO;
                        } else {
                            stmt->type = ST_IFGOTO;
                        }
                        stmtchanged = TRUE;
                        return;
                    }
                }
            }
            break;
        }

        if (scan->type == ST_LABEL) {
            sawLabel = TRUE;
        }

        if (stmt->label->stmt == scan) {
            stmt->type = ST_EXPRESSION;
            stmtchanged = TRUE;
            return;
        }
    }

    stmt->label = finallabel(stmt->label, stmt);
}

static void optimizeswitch(Statement* stmt) {
    SwitchInfo* info;
    SwitchCase* swcase;

    info = (SwitchInfo*)stmt->label;
    CError_ASSERT(1746, info && info->cases && info->defaultlabel);

    info->defaultlabel = finallabel(info->defaultlabel, stmt);
    for (swcase = info->cases; swcase; swcase = swcase->next) {
        swcase->label = finallabel(swcase->label, stmt);
    }

    if (ENODE_IS(stmt->expr, EINTCONST)) {
        for (swcase = info->cases; swcase; swcase = swcase->next) {
            if (CInt64_Equal(swcase->min, stmt->expr->data.intval)) {
                break;
            }
        }

        stmt->type = ST_GOTO;
        stmt->label = swcase ? swcase->label : info->defaultlabel;
    }
}

static void removeunusedlabels(Statement* stmt) {
    Statement* scan;
    CLabel* label;
    SwitchCase* swcase;
    ExceptionAction* action;

    for (scan = stmt; scan; scan = scan->next) {
        scan->marked = FALSE;
    }

    for (scan = stmt; scan; scan = scan->next) {
        switch (scan->type) {
            case ST_GOTO:
            case ST_IFGOTO:
            case ST_IFNGOTO:
            case ST_OVF:
                if (scan->label->stmt) {
                    scan->label->stmt->marked = TRUE;
                }
                break;
            case ST_SWITCH:
                ((SwitchInfo*)scan->label)->defaultlabel->stmt->marked = TRUE;
                for (swcase = ((SwitchInfo*)scan->label)->cases; swcase; swcase = swcase->next) {
                    swcase->label->stmt->marked = TRUE;
                }
                break;
            case ST_ASM:
                if ((label = InlineAsm_GetReferencedLabel(scan))) {
                    label->stmt->marked = TRUE;
                }
                if ((label = InlineAsm_GetReferencedLabel2(scan))) {
                    label->stmt->marked = TRUE;
                }
                break;
            case ST_BEGINLOOP:
                ((LoopInfo*)scan->expr)->stmt->marked = TRUE;
                break;
            case ST_ENDLOOP:
                if (scan->next->type == ST_GOTO && scan->next->next->type == ST_LABEL) {
                    scan->next->next->marked = TRUE;
                }
                break;
            default:
                for (action = scan->dobjstack; action; action = action->prev) {
                    if (action->type == EAT_CATCHBLOCK) {
                        action->data.catch_block.catch_label->stmt->marked = TRUE;
                        action->data.catch_block.catch_label->stmt->flags |= StmtFlag_KeepLabel;
                    } else if (action->type == EAT_SPECIFICATION) {
                        action->data.specification.unexp_label->stmt->marked = TRUE;
                        action->data.specification.unexp_label->stmt->flags |= StmtFlag_KeepLabel;
                    }
                }
        }
    }

    for (scan = stmt; scan; scan = scan->next) {
        if (scan->type == ST_LABEL && !scan->marked && !(scan->flags & StmtFlag_KeepLabel)) {
            scan->type = ST_NOP;
            stmtchanged = TRUE;
        }
    }
}

static void optimizebranches(Statement* stmt) {
    Statement* scan;

    removeunusedlabels(stmt);
    for (scan = stmt; scan; scan = scan->next) {
        switch (scan->type) {
            case ST_GOTO:
                optimizegoto(scan);
                break;
            case ST_IFGOTO:
            case ST_IFNGOTO:
                optimizeif(scan);
                break;
            case ST_SWITCH:
                optimizeswitch(scan);
                break;
            default:
                break;
        }
    }
}

void SetVarUsage(Object* obj, Boolean noregister) {
    VarInfo* vi;

    CError_ASSERT(1875, obj->datatype != DALIAS);

    if (obj->datatype != DLOCAL) {
        return;
    }

    vi = obj->u.var.info;
    vi->used = TRUE;
    if (!copts.globaloptimizer) {
        if (copts.optimizesize) {
            vi->usage++;
        } else {
            vi->usage += curstmtvalue;
        }
    }

    if (noregister) {
        vi->noregister = TRUE;
    }
}

static void checkexpression(ENode* expr) {
    ENodeList* list;

    while (1) {
        switch (expr->type) {
            case EOBJREF:
                SetVarUsage(expr->data.objref, TRUE);
                return;
            case EINDIRECT:
                if (ENODE_IS(expr->data.monadic, EOBJREF)) {
                    SetVarUsage(expr->data.monadic->data.objref, FALSE);
                    return;
                }
                expr = expr->data.monadic;
                break;
            case EFUNCCALL:
            case EFUNCCALLP:
                copt_isleaffunction = FALSE;
                checkexpression(expr->data.funccall.funcref);
                for (list = expr->data.funccall.args; list; list = list->next) {
                    checkexpression(list->node);
                }
                return;
            case ECOND:
            case ECONDASS:
                checkexpression(expr->data.cond.cond);
                checkexpression(expr->data.cond.expr1);
                checkexpression(expr->data.cond.expr2);
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
            case EBCLR:
            case EBTST:
            case EBSET:
                checkexpression(expr->data.diadic.left);
                expr = expr->data.diadic.right;
                break;
            case EPOSTINC:
            case EPOSTDEC:
            case EPREINC:
            case EPREDEC:
            case EMONMIN:
            case EBINNOT:
            case ELOGNOT:
            case EFORCELOAD:
            case ETYPCON:
            case EBITFIELD:
                expr = expr->data.monadic;
                break;
            case ENULLCHECK:
                checkexpression(expr->data.nullcheck.nullcheckexpr);
                expr = expr->data.nullcheck.condexpr;
                break;
            case EINTCONST:
            case EFLOATCONST:
            case EPRECOMP:
            case ELABEL:
            case EVECTOR128CONST:
                return;
            case ESTRINGCONST:
                return;
            default:
                CError_FATAL(1998);
        }
    }
}

static void checklocalusage(Statement* stmt) {
    Statement* scan;

    for (scan = stmt; scan; scan = scan->next) {
        if (scan->type >= ST_EXPRESSION && scan->type <= ST_GOTOEXPR && scan->expr) {
            curstmtvalue = scan->value;
            checkexpression(scan->expr);
        } else if (scan->type == ST_ASM) {
            curstmtvalue = scan->value;
            InlineAsm_CheckLocalUsage(scan);
        }
    }
}

static void colorcode(Statement* stmt) {
    CLabel* label;
    SwitchCase* swcase;

    while (stmt && !stmt->marked) {
        stmt->marked = TRUE;
        switch (stmt->type) {
            case ST_GOTOEXPR:
                return;
            case ST_IFGOTO:
            case ST_IFNGOTO:
            case ST_OVF:
                colorcode(stmt->label->stmt);
                break;
            case ST_GOTO:
            case ST_EXIT:
                stmt = stmt->label->stmt;
                continue;
            case ST_RETURN:
                return;
            case ST_SWITCH:
                for (swcase = ((SwitchInfo*)stmt->label)->cases; swcase; swcase = swcase->next) {
                    colorcode(swcase->label->stmt);
                }
                stmt = ((SwitchInfo*)stmt->label)->defaultlabel->stmt;
                continue;
            case ST_ASM:
                if ((label = InlineAsm_GetReferencedLabel(stmt))) {
                    colorcode(label->stmt);
                }
                if ((label = InlineAsm_GetReferencedLabel2(stmt))) {
                    colorcode(label->stmt);
                }
                break;
            case ST_ENDLOOP:
                if (stmt->next && stmt->next->type == ST_GOTO) {
                    stmt = stmt->next;
                    stmt->marked = TRUE;
                }
                break;
            case ST_NOP:
            case ST_LABEL:
            case ST_EXPRESSION:
            case ST_ENTRY:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
            case ST_BEGINLOOP:
                break;
            default:
                CError_FATAL(2096);
        }
        stmt = stmt->next;
    }
}

static void removeunusedcode(Statement* stmt) {
    Statement* scan;

    for (scan = stmt; scan; scan = scan->next) {
        scan->marked = FALSE;
    }

    colorcode(stmt);
    for (scan = stmt; scan; scan = scan->next) {
        if (!scan->marked && (scan->flags & StmtFlag_KeepLabel)) {
            colorcode(scan);
        }
    }

    for (scan = stmt; scan; scan = scan->next) {
        if (!scan->marked && scan->type != ST_NOP) {
            scan->type = ST_NOP;
            stmtchanged = TRUE;
        }
    }
}

static void COpt_ReturnCheck(Object* obj, Statement* stmt) {
    Statement* scan;

    if ((copts.pedantic || copts.cplusplus) && obj && TYPE_FUNC(obj->type)->functype != &stvoid) {
        scan = stmt;
        while (scan) {
            if (!scan->next && scan->type != ST_GOTO && scan->type != ST_RETURN) {
                CError_Warning(ERR_RETURN_VALUE_EXPECTED);
                break;
            }

            if (scan->type == ST_RETURN && !scan->expr && !(scan->flags & StmtFlag_AfterNoReturn)) {
                CError_Warning(ERR_RETURN_VALUE_EXPECTED);
                break;
            }

            scan = scan->next;
        }
    }
}

static void COpt_Optimize(Object* obj, Statement* stmt) {
    Statement* scan;
    Statement** ptr;

    do {
        stmtchanged = FALSE;
        optimizebranches(stmt->next);
        removeunusedcode(stmt->next);
    } while (stmtchanged);

    ptr = &stmt->next;
    while ((scan = *ptr)) {
        if (scan->type == ST_NOP) {
            *ptr = scan->next;
        } else {
            ptr = &scan->next;
        }
    }
}

static void COpt_ELABELCallBack(ENode* expr) {
    CError_ASSERT(2195, expr->data.label->stmt && expr->data.label->stmt->type == ST_LABEL);
    expr->data.label->stmt->flags |= StmtFlag_KeepLabel;
}

void COpt_SimpleOptimizer(Object* obj, Statement* stmt) {
    Statement* scan;

    for (scan = stmt; scan; scan = scan->next) {
        if ((scan->type >= ST_EXPRESSION && scan->type <= ST_GOTOEXPR) && scan->expr) {
            CExpr_SearchExprTree(scan->expr, COpt_ELABELCallBack, 1, ELABEL);
        }
    }

    static_for_inlines = TRUE;
    cleanreturnlabel = NULL;
    COpt_Optimize(obj, stmt);
    COpt_ReturnCheck(obj, stmt);
}

Statement* COpt_Optimizer(Object* obj, Statement* stmt) {
    copt_isleaffunction = TRUE;
    if (copts.globaloptimizer) {
        stmt = IRO_Optimizer(obj, stmt);
    }

    static_for_inlines = FALSE;
    COpt_Optimize(obj, stmt);

    if (obj && !(obj->qual & Q_INLINE)) {
        COpt_ReturnCheck(obj, stmt);
    }

    checklocalusage(stmt->next);
    return stmt;
}
