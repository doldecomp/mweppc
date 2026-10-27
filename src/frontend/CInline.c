#include "compiler/CInline.h"
#include "compiler/CABI.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CException.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/COptimizer.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/CTemplateNew.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/InlineAsm.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/scopes.h"
#include "compiler/Switch.h"
#include "compiler/templates.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "driver/CWPlugins.h"
#include "driver/DropInCompilerLinker.h"

enum {
    CI_SFLAGS_NoClass = 0,
    CI_SFLAGS_Register = 1,
    CI_SFLAGS_Auto = 2,
    CI_SFLAGS_Referenced = 0x80
};

typedef enum CI_CanInline {
    CI_CanInlineNone = 0,
    CI_CanInlineStatement = 3,
    CI_CanInlineExpression = 6
} CI_CanInline;

#pragma pack(push, 2)
typedef struct IDTrans {
    struct IDTrans* next;
    SInt32 from;
    SInt32 to;
} IDTrans;

typedef struct LabelTrans {
    struct LabelTrans* next;
    CLabel** labelptr;
    short id;
} LabelTrans;

typedef struct UIDTemp {
    struct UIDTemp* next;
    Object* object;
    SInt32 uid;
} UIDTemp;

typedef struct CI_Export {
    struct CI_Export* next;
    Object* object;
    CI_FuncData* funcdata;
    Boolean oldStyleParameters;
} CI_Export;

typedef struct AObject {
    Object* object;
    ENode* useExpr;
    ENode* evalExpr;
} AObject;

typedef struct CI_StmtLink {
    struct CI_StmtLink* next;
    Statement* stmt;
    CI_Statement* ciStmt;
} CI_StmtLink;
#pragma pack(pop)

static CInlineCopyMode enode_copymode;
static Boolean enode_globalcopy;
static IDTrans* enode_idtrans;
static Object** local_dobjects;
static AObject* local_aobjects;
static CI_Var* loc_args;
static CI_Var* loc_vars;
static Boolean inline_expanded;
static Boolean any_inline_expanded;
static short cinline_level;
static LabelTrans* cinline_label_trans;
static Statement* cinline_first_stmt;
static ENode* cinline_stmtlevelexpr[16];
static short cinline_stmtlevelexprs;
static Boolean cinline_unconditionalpart;
static Boolean cinline_serialize_stmt;
static CI_Export* cinline_exportlist;
static CI_Action* cinline_actionlist;
CI_Action* cinline_tactionlist;
static ObjectList* cinline_freflist;
static Boolean cinline_gendeps;
static Statement* cinline_serial_stmt;
static Statement* cinline_cur_serial_stmt;
static UIDTemp* cinline_uid_temps;
static Boolean cinline_has_sideeffect;
static SInt32 inline_max_size;
static Boolean recursive_inline;
static Object* expanding_function;
static Boolean cinline_funccallfound;

static ENode* CInline_FoldConst(ENode* expr);
static ENode* CInline_CopyNodes(ENode* node);
static SInt32 CInline_EstimateSizeOfFunc(CI_FuncData* funcdata, SInt32 size, SInt32 level);
static ENode* CInline_SerializeExpr(ENode* expr);
static void CInline_AddFRefList_InlineFunc(CI_FuncData* data);

void CInline_Init(void) {
    cinline_exportlist = NULL;
    cinline_actionlist = NULL;
    cinline_tactionlist = NULL;
    cinline_gendeps = FALSE;
}

static ENode* CInline_MakeNotNot(ENode* expr) {
    expr = CInline_FoldConst(expr);

    if (!ENODE_IS(expr, EINTCONST)) {
        expr = makemonadicnode(expr, ELOGNOT);
        expr->rtype = CParser_GetBoolType();
        expr = makemonadicnode(expr, ELOGNOT);
    } else {
        expr->data.intval = CInt64_Not(CInt64_Not(expr->data.intval));
    }

    return expr;
}

static ENode* CInline_FoldConst(ENode* expr) {
    ENode* inner;
    ENode* left;
    ENodeList* list;

    switch (expr->type) {
        case EINTCONST:
        case EFLOATCONST:
        case ESTRINGCONST:
        case EOBJREF:
        case EPRECOMP:
        case ETEMP:
        case ELABEL:
        case EOBJLIST:
        case EINSTRUCTION:
        case EVECTOR128CONST:
            return expr;

        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
            expr->data.monadic = CInline_FoldConst(expr->data.monadic);
            inner = expr->data.monadic;
            switch (inner->type) {
                case EINTCONST:
                    if (!ENODE_IS(expr, ELOGNOT)) {
                        inner->data.intval = CMach_CalcIntMonadic(
                            expr->rtype, CParser_GetOperator(expr->type), inner->data.intval
                        );
                    } else {
                        inner->data.intval = CInt64_Not(inner->data.intval);
                    }
                    inner->rtype = expr->rtype;
                    return inner;

                case EFLOATCONST:
                    if (ENODE_IS(expr, ELOGNOT)) {
                        inner->type = EINTCONST;
                        CInt64_SetLong(
                            &inner->data.intval, CMach_FloatIsZero(inner->data.floatval)
                        );
                    } else {
                        inner->data.floatval = CMach_CalcFloatMonadic(
                            expr->rtype, CParser_GetOperator(expr->type), inner->data.floatval
                        );
                    }
                    inner->rtype = expr->rtype;
                    return inner;
                default:
                    break;
            }

            return expr;

        case ETYPCON:
            expr->data.monadic = CInline_FoldConst(expr->data.monadic);
            switch (expr->data.monadic->type) {
                case EINTCONST:
                    switch (expr->rtype->type) {
                        case TYPEFLOAT:
                            expr->type = EFLOATCONST;
                            expr->data.floatval = CMach_CalcFloatConvertFromInt(
                                expr->data.monadic->rtype, expr->data.monadic->data.intval
                            );
                            return expr;

                        case TYPEINT:
                            expr->type = EINTCONST;
                            expr->data.intval = CExpr_IntConstConvert(
                                expr->rtype,
                                expr->data.monadic->rtype,
                                expr->data.monadic->data.intval
                            );
                            break;
                        default:
                            break;
                    }
                    break;

                case EFLOATCONST:
                    switch (expr->rtype->type) {
                        case TYPEFLOAT:
                            expr->type = EFLOATCONST;
                            expr->data.floatval = CMach_CalcFloatConvert(
                                expr->rtype, expr->data.monadic->data.floatval
                            );
                            return expr;

                        case TYPEINT:
                            expr->type = EINTCONST;
                            expr->data.intval = CMach_CalcIntConvertFromFloat(
                                expr->rtype, expr->data.monadic->data.floatval
                            );
                            return expr;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }

            return expr;

        case EPOSTINC:
        case EPOSTDEC:
            expr->data.monadic = CInline_FoldConst(expr->data.monadic);
            switch (expr->data.monadic->type) {
                case EINTCONST:
                case EFLOATCONST:
                    expr->data.monadic->rtype = expr->rtype;
                    return expr->data.monadic;
                default:
                    break;
            }

            return expr;

        case EPREINC:
        case EPREDEC:
        case EINDIRECT:
        case EFORCELOAD:
        case EBITFIELD:
            expr->data.monadic = CInline_FoldConst(expr->data.monadic);
            return expr;

        case EMUL:
        case EDIV:
        case EMODULO:
        case EADD:
        case ESUB:
        case ESHL:
        case ESHR:
        case EAND:
        case EXOR:
        case EOR:
            expr->data.diadic.left = CInline_FoldConst(expr->data.diadic.left);
            expr->data.diadic.right = CInline_FoldConst(expr->data.diadic.right);
            if (expr->data.diadic.left->type == expr->data.diadic.right->type) {
                left = expr->data.diadic.left;
                switch (expr->data.diadic.left->type) {
                    case EINTCONST:
                        left->data.intval = CMach_CalcIntDiadic(
                            expr->rtype,
                            expr->data.diadic.left->data.intval,
                            CParser_GetOperator(expr->type),
                            expr->data.diadic.right->data.intval
                        );
                        left->rtype = expr->rtype;
                        return left;

                    case EFLOATCONST:
                        left->data.floatval = CMach_CalcFloatDiadic(
                            expr->rtype,
                            expr->data.diadic.left->data.floatval,
                            CParser_GetOperator(expr->type),
                            expr->data.diadic.right->data.floatval
                        );
                        left->rtype = expr->rtype;
                        return left;
                    default:
                        break;
                }
            }

            return expr;

        case ELESS:
        case EGREATER:
        case ELESSEQU:
        case EGREATEREQU:
        case EEQU:
        case ENOTEQU:
            expr->data.diadic.left = CInline_FoldConst(expr->data.diadic.left);
            expr->data.diadic.right = CInline_FoldConst(expr->data.diadic.right);
            if (expr->data.diadic.left->type == expr->data.diadic.right->type) {
                left = expr->data.diadic.left;
                switch (expr->data.diadic.left->type) {
                    case EINTCONST:
                        left->data.intval = CMach_CalcIntDiadic(
                            left->rtype,
                            expr->data.diadic.left->data.intval,
                            CParser_GetOperator(expr->type),
                            expr->data.diadic.right->data.intval
                        );
                        left->rtype = expr->rtype;
                        return left;

                    case EFLOATCONST:
                        CInt64_SetLong(
                            &left->data.intval,
                            CMach_CalcFloatDiadicBool(
                                left->rtype,
                                expr->data.diadic.left->data.floatval,
                                CParser_GetOperator(expr->type),
                                expr->data.diadic.right->data.floatval
                            )
                        );
                        left->type = EINTCONST;
                        left->rtype = expr->rtype;
                        return left;
                    default:
                        break;
                }
            }

            return expr;

        case ELAND:
            expr->data.diadic.left = CInline_FoldConst(expr->data.diadic.left);
            if (iszero(expr->data.diadic.left)) {
                return expr->data.diadic.left;
            }
            if (isnotzero(expr->data.diadic.left)) {
                return CInline_MakeNotNot(expr->data.diadic.right);
            }

            expr->data.diadic.right = CInline_FoldConst(expr->data.diadic.right);
            if (isnotzero(expr->data.diadic.right)) {
                return CInline_MakeNotNot(expr->data.diadic.left);
            }

            return expr;

        case ELOR:
            expr->data.diadic.left = CInline_FoldConst(expr->data.diadic.left);
            if (iszero(expr->data.diadic.left)) {
                return CInline_MakeNotNot(expr->data.diadic.right);
            }
            if (isnotzero(expr->data.diadic.left)) {
                return CInline_MakeNotNot(expr->data.diadic.left);
            }

            expr->data.diadic.right = CInline_FoldConst(expr->data.diadic.right);
            if (iszero(expr->data.diadic.right)) {
                return CInline_MakeNotNot(expr->data.diadic.left);
            }

            return expr;

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
        case EROTL:
        case EROTR:
            expr->data.diadic.left = CInline_FoldConst(expr->data.diadic.left);
            expr->data.diadic.right = CInline_FoldConst(expr->data.diadic.right);
            return expr;

        case ECOND:
            expr->data.cond.cond = CInline_FoldConst(expr->data.cond.cond);
            if (isnotzero(expr->data.cond.cond)) {
                return CInline_FoldConst(expr->data.cond.expr1);
            }
            if (iszero(expr->data.cond.cond)) {
                return CInline_FoldConst(expr->data.cond.expr2);
            }

            expr->data.cond.expr1 = CInline_FoldConst(expr->data.cond.expr1);
            expr->data.cond.expr2 = CInline_FoldConst(expr->data.cond.expr2);
            return expr;

        case EMFPOINTER:
            expr->data.mfpointer.accessnode = CInline_FoldConst(expr->data.mfpointer.accessnode);
            expr->data.mfpointer.mfpointer = CInline_FoldConst(expr->data.mfpointer.mfpointer);
            return expr;

        case EFUNCCALL:
        case EFUNCCALLP:
            expr->data.funccall.funcref = CInline_FoldConst(expr->data.funccall.funcref);
            for (list = expr->data.funccall.args; list; list = list->next) {
                list->node = CInline_FoldConst(list->node);
            }
            return expr;

        case ENULLCHECK:
            expr->data.nullcheck.nullcheckexpr =
                CInline_FoldConst(expr->data.nullcheck.nullcheckexpr);
            expr->data.nullcheck.condexpr = CInline_FoldConst(expr->data.nullcheck.condexpr);
            return expr;

        case EMEMBER:
            if (expr->data.emember->expr) {
                expr->data.emember->expr = CInline_FoldConst(expr->data.emember->expr);
            }
            return expr;

        default:
            CError_FATAL(421);
            return expr;
    }
}

inline SInt32 CInline_GetLocalID2(Object* object) {
    ObjectList* list;
    SInt32 counter;

    for (list = locals, counter = 0; list; list = list->next) {
        if (list->object->datatype == DLOCAL) {
            if (list->object == object) {
                return counter;
            }
            counter++;
        }
    }

    return -1;
}

SInt32 CInline_GetLocalID(Object* object) {
    ObjectList* list;
    int counter;

    if (object) {
        for (list = arguments, counter = 0; list; list = list->next, counter++) {
            if (list->object == object) {
                loc_args[counter].used = TRUE;
                loc_args[counter].unmodified = FALSE;
                return counter - 0x7FFFFFFF;
            }
        }

        counter = CInline_GetLocalID2(object);
        CError_ASSERT(465, counter >= 0);
        loc_vars[counter].used = TRUE;
        return counter + 1;
    }

    return 0;
}

static Boolean CInline_IsTrivialExpression(ENode* expr) {
    while (1) {
        switch (expr->type) {
            case EINTCONST:
            case EFLOATCONST:
            case EOBJREF:
            case EARGOBJ:
            case ELOCOBJ:
            case EOBJLIST:
            case EVECTOR128CONST:
                return FALSE;

            case ESTRINGCONST:
                return copts.dont_reuse_strings;

            case EMEMBER:
                if (expr->data.emember->expr) {
                    return CInline_IsTrivialExpression(expr->data.emember->expr);
                }
                return FALSE;

            case EINDIRECT:
                if (ENODE_IS(expr->data.monadic, EOBJREF)) {
                    Object* object = expr->data.monadic->data.objref;
                    Boolean result;

                    if (object->datatype == DLOCAL && !(object->flags & OBJECT_REFERENCED)) {
                        result = FALSE;
                    } else if (is_const_object(object)) {
                        result = FALSE;
                    } else {
                        result = TRUE;
                    }
                    return result;
                }

                return TRUE;

            case EPOSTINC:
            case EPOSTDEC:
            case EPREINC:
            case EPREDEC:
            case EFORCELOAD:
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
            case EFUNCCALL:
            case EFUNCCALLP:
            case EMFPOINTER:
            case ENULLCHECK:
            case EPRECOMP:
            case ETEMP:
            case ELABEL:
            case EINSTRUCTION:
                return TRUE;

            case EMONMIN:
            case EBINNOT:
            case ELOGNOT:
            case ETYPCON:
            case EBITFIELD:
                expr = expr->data.monadic;
                continue;

            case EMUL:
            case EDIV:
            case EMODULO:
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
            case ECOMMA:
            case EROTL:
            case EROTR:
                if (CInline_IsTrivialExpression(expr->data.diadic.left)) {
                    return TRUE;
                }
                expr = expr->data.diadic.right;
                continue;

            case ECOND:
                if (CInline_IsTrivialExpression(expr->data.cond.cond)) {
                    return TRUE;
                }
                if (CInline_IsTrivialExpression(expr->data.cond.expr1)) {
                    return TRUE;
                }
                expr = expr->data.cond.expr2;
                continue;

            default:
                CError_FATAL(582);
        }
    }
}

Boolean CInline_ExpressionHasSideEffect(ENode* expr) {
    while (1) {
        switch (expr->type) {
            case EINTCONST:
            case EFLOATCONST:
            case ESTRINGCONST:
            case EOBJREF:
            case EPRECOMP:
            case ETEMP:
            case EARGOBJ:
            case ELOCOBJ:
            case ELABEL:
            case EOBJLIST:
            case EVECTOR128CONST:
                return FALSE;

            case EPOSTINC:
            case EPOSTDEC:
            case EPREINC:
            case EPREDEC:
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
            case EFUNCCALL:
            case EFUNCCALLP:
            case EINSTRUCTION:
                return TRUE;

            case EINDIRECT:
            case EMONMIN:
            case EBINNOT:
            case ELOGNOT:
            case EFORCELOAD:
            case ETYPCON:
            case EBITFIELD:
                expr = expr->data.monadic;
                continue;

            case EMUL:
            case EDIV:
            case EMODULO:
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
            case ECOMMA:
            case EROTL:
            case EROTR:
                if (CInline_ExpressionHasSideEffect(expr->data.diadic.left)) {
                    return TRUE;
                }
                expr = expr->data.diadic.right;
                continue;

            case EMEMBER:
                if (expr->data.emember->expr) {
                    return CInline_ExpressionHasSideEffect(expr->data.emember->expr);
                }
                return FALSE;

            case EMFPOINTER:
                if (CInline_ExpressionHasSideEffect(expr->data.mfpointer.accessnode)) {
                    return TRUE;
                }
                expr = expr->data.mfpointer.mfpointer;
                continue;

            case ENULLCHECK:
                if (CInline_ExpressionHasSideEffect(expr->data.nullcheck.nullcheckexpr)) {
                    return TRUE;
                }
                expr = expr->data.nullcheck.condexpr;
                continue;

            case ECOND:
                if (CInline_ExpressionHasSideEffect(expr->data.cond.cond)) {
                    return TRUE;
                }
                if (CInline_ExpressionHasSideEffect(expr->data.cond.expr1)) {
                    return TRUE;
                }
                expr = expr->data.cond.expr2;
                continue;

            default:
                CError_FATAL(689);
        }
    }
}

static ENode* CInline_CopyExpressionSave(ENode* expr) {
    CInlineCopyMode save_copymode;
    Boolean save_globalcopy;
    IDTrans* save_idtrans;

    save_globalcopy = enode_globalcopy;
    enode_globalcopy = TRUE;

    save_copymode = enode_copymode;
    enode_copymode = CopyModeExpand;

    save_idtrans = enode_idtrans;
    enode_idtrans = NULL;

    expr = CInline_CopyNodes(expr);

    enode_globalcopy = save_globalcopy;
    enode_copymode = save_copymode;
    enode_idtrans = save_idtrans;

    return expr;
}

static SInt32 CInline_TranslateID(SInt32 id) {
    IDTrans* trans;

    for (trans = enode_idtrans; trans; trans = trans->next) {
        if (trans->from == id) {
            return trans->to;
        }
    }

    trans = lalloc(sizeof(IDTrans));
    trans->next = enode_idtrans;
    enode_idtrans = trans;

    trans->from = id;
    trans->to = CParser_GetUniqueID();

    return trans->to;
}

static short CInline_GetLabelStatementNumber(HashNameNode* name) {
    Statement* stmt;
    short i;

    for (stmt = cinline_first_stmt, i = 0; stmt; stmt = stmt->next, i++) {
        if (stmt->type == ST_LABEL && stmt->label->uniquename == name) {
            return i;
        }
    }

    CError_FATAL(742);
    return 0;
}

static ENodeList* CInline_CopyNodeList(ENodeList* list) {
    ENodeList* first;
    ENodeList* last;
    ENodeList* copy;

    first = NULL;
    while (list) {
        if (enode_globalcopy) {
            copy = galloc(sizeof(ENodeList));
        } else {
            copy = lalloc(sizeof(ENodeList));
        }

        copy->node = CInline_CopyNodes(list->node);
        copy->next = NULL;

        if (first) {
            last->next = copy;
            last = copy;
        } else {
            first = last = copy;
        }

        list = list->next;
    }

    return first;
}

static EMemberInfo* CInline_CopyEMemberInfo(EMemberInfo* mi) {
    EMemberInfo* copy;

    if (enode_globalcopy) {
        copy = galloc(sizeof(EMemberInfo));
    } else {
        copy = lalloc(sizeof(EMemberInfo));
    }

    *copy = *mi;
    if (copy->path) {
        copy->path = CClass_GetPathCopy(copy->path, enode_globalcopy);
    }
    if (copy->expr) {
        copy->expr = CInline_CopyNodes(copy->expr);
    }

    return copy;
}

static ENode* CInline_CopyNodes(ENode* node) {
    ENode* copy;
    ObjectList* list;
    int i;

    if (enode_globalcopy) {
        copy = galloc(sizeof(ENode));
    } else {
        copy = lalloc(sizeof(ENode));
    }

    while (1) {
        *copy = *node;
        switch (copy->type) {
            case ETEMPLDEP:
                switch (copy->data.templdep.subtype) {
                    case TDE_PARAM:
                    case TDE_SIZEOF:
                    case TDE_ALIGNOF:
                    case TDE_QUALNAME:
                    case TDE_OBJ:
                        break;
                    case TDE_CAST:
                        copy->data.templdep.u.cast.args =
                            CInline_CopyNodeList(copy->data.templdep.u.cast.args);
                        break;
                    case TDE_SOURCEREF:
                        copy->data.templdep.u.sourceref.expr =
                            CInline_CopyNodes(copy->data.templdep.u.sourceref.expr);
                        break;
                    case TDE_ADDRESS_OF:
                        copy->data.templdep.u.monadic =
                            CInline_CopyNodes(copy->data.templdep.u.monadic);
                        break;
                    default:
                        CError_FATAL(840);
                }
                break;

            case ETEMP:
                if (enode_copymode == CopyModeUnpack && copy->data.temp.uniqueid != 0) {
                    copy->data.temp.uniqueid = CInline_TranslateID(copy->data.temp.uniqueid);
                }
                break;

            case ELABEL:
                switch (enode_copymode) {
                    case CopyModePack:
                        copy->data.precompid =
                            CInline_GetLabelStatementNumber(copy->data.label->uniquename);
                        return copy;
                    case CopyModeUnpack:
                    case CopyModeExpand: {
                        LabelTrans* trans = lalloc(sizeof(LabelTrans));
                        trans->next = cinline_label_trans;
                        cinline_label_trans = trans;
                        trans->id = copy->data.precompid;
                        trans->labelptr = &copy->data.label;
                        return copy;
                    }
                    default:
                        break;
                }
                break;

            case EINTCONST:
            case EFLOATCONST:
            case ESTRINGCONST:
            case EOBJLIST:
            case EINSTRUCTION:
            case EVECTOR128CONST:
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
                copy->data.monadic = CInline_CopyNodes(copy->data.monadic);
                break;

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
                copy->data.diadic.left = CInline_CopyNodes(copy->data.diadic.left);
                copy->data.diadic.right = CInline_CopyNodes(copy->data.diadic.right);
                break;

            case ECOND:
                copy->data.cond.cond = CInline_CopyNodes(copy->data.cond.cond);
                copy->data.cond.expr1 = CInline_CopyNodes(copy->data.cond.expr1);
                copy->data.cond.expr2 = CInline_CopyNodes(copy->data.cond.expr2);
                break;

            case EMFPOINTER:
                copy->data.mfpointer.accessnode =
                    CInline_CopyNodes(copy->data.mfpointer.accessnode);
                copy->data.mfpointer.mfpointer = CInline_CopyNodes(copy->data.mfpointer.mfpointer);
                break;

            case EFUNCCALL:
            case EFUNCCALLP:
                copy->data.funccall.funcref = CInline_CopyNodes(copy->data.funccall.funcref);
                copy->data.funccall.args = CInline_CopyNodeList(copy->data.funccall.args);
                break;

            case ENULLCHECK:
                copy->data.nullcheck.precompid =
                    CInline_TranslateID(copy->data.nullcheck.precompid);
                copy->data.nullcheck.nullcheckexpr =
                    CInline_CopyNodes(copy->data.nullcheck.nullcheckexpr);
                copy->data.nullcheck.condexpr = CInline_CopyNodes(copy->data.nullcheck.condexpr);
                break;

            case EPRECOMP:
                copy->data.precompid = CInline_TranslateID(copy->data.precompid);
                break;

            case EINDIRECT:
                if (enode_copymode == CopyModeExpand &&
                    ENODE_IS(copy->data.monadic, EARGOBJ) &&
                    local_aobjects[copy->data.monadic->data.longval].object == NULL) {
                    CError_ASSERT(910, local_aobjects[copy->data.monadic->data.longval].useExpr);
                    copy = CInline_CopyExpressionSave(
                        local_aobjects[copy->data.monadic->data.longval].useExpr
                    );
                    if (copy->rtype != node->rtype) {
                        if (IS_TYPE_INT(copy->rtype) && IS_TYPE_INT(node->rtype)) {
                            copy = makemonadicnode(copy, ETYPCON);
                        }
                        copy->rtype = node->rtype;
                    }
                    return copy;
                }

                copy->data.monadic = CInline_CopyNodes(copy->data.monadic);
                break;

            case EOBJREF:
                if (enode_copymode == CopyModePack) {
                    if (node->data.objref->datatype == DALIAS) {
                        CExpr_AliasTransform(node);
                        continue;
                    }

                    if (node->data.objref->datatype == DDATA) {
                        return copy;
                    }

                    for (list = arguments, i = 0; list; list = list->next, i++) {
                        if (list->object == copy->data.objref) {
                            copy->type = EARGOBJ;
                            copy->data.longval = i;
                            return copy;
                        }
                    }

                    i = CInline_GetLocalID2(copy->data.objref);
                    if (i >= 0) {
                        copy->type = ELOCOBJ;
                        copy->data.longval = i;
                        return copy;
                    }

                    if (node->data.objref->datatype == DLOCAL) {
                        CError_FATAL(949);
                    }
                }
                break;

            case EARGOBJ:
                switch (enode_copymode) {
                    case CopyModeExpand:
                        CError_ASSERT(957, local_aobjects[copy->data.longval].object);
                        copy->type = EOBJREF;
                        copy->data.objref = local_aobjects[copy->data.longval].object;
                        return copy;

                    case CopyModeUnpack:
                        for (list = arguments, i = 0; list; list = list->next, i++) {
                            if (i == copy->data.longval) {
                                copy->type = EOBJREF;
                                copy->data.objref = list->object;
                                CError_ASSERT(966, copy->data.objref);
                                return copy;
                            }
                        }

                    default:
                        CError_FATAL(971);
                }

            case ELOCOBJ:
                switch (enode_copymode) {
                    case CopyModeExpand:
                        copy->type = EOBJREF;
                        copy->data.objref = local_dobjects[copy->data.longval];
                        return copy;

                    case CopyModeUnpack:
                        for (list = locals, i = 0; list; list = list->next, i++) {
                            if (i == copy->data.longval) {
                                copy->type = EOBJREF;
                                copy->data.objref = list->object;
                                CError_ASSERT(986, copy->data.objref);
                                return copy;
                            }
                        }

                    default:
                        CError_FATAL(991);
                }
                break;

            case ENEWEXCEPTION:
            case ENEWEXCEPTIONARRAY:
                copy->data.newexception.initexpr =
                    CInline_CopyNodes(copy->data.newexception.initexpr);
                copy->data.newexception.tryexpr =
                    CInline_CopyNodes(copy->data.newexception.tryexpr);
                break;

            case EINITTRYCATCH:
                copy->data.itc.initexpr = CInline_CopyNodes(copy->data.itc.initexpr);
                copy->data.itc.tryexpr = CInline_CopyNodes(copy->data.itc.tryexpr);
                copy->data.itc.catchexpr = CInline_CopyNodes(copy->data.itc.catchexpr);
                copy->data.itc.result = CInline_CopyNodes(copy->data.itc.result);
                break;

            case EMEMBER:
                copy->data.emember = CInline_CopyEMemberInfo(copy->data.emember);
                break;

            default:
                CError_FATAL(1015);
        }

        return copy;
    }
}

static void CInline_CheckUsage(ENode* expr, Boolean isModified) {
    ENodeList* list;

    while (1) {
        switch (expr->type) {
            case EARGOBJ:
                loc_args[expr->data.longval].used = TRUE;
                loc_args[expr->data.longval].unmodified = FALSE;
                return;

            case ELOCOBJ:
                loc_vars[expr->data.longval].used = TRUE;
                return;

            case EINDIRECT:
                expr = expr->data.monadic;
                if (ENODE_IS(expr, EARGOBJ)) {
                    loc_args[expr->data.longval].used = TRUE;
                    if (isModified) {
                        loc_args[expr->data.longval].unmodified = FALSE;
                    }
                    return;
                }
                isModified = FALSE;
                continue;

            case EMONMIN:
            case EBINNOT:
            case ELOGNOT:
            case EFORCELOAD:
            case ETYPCON:
            case EBITFIELD:
                expr = expr->data.monadic;
                isModified = FALSE;
                continue;

            case EPOSTINC:
            case EPOSTDEC:
            case EPREINC:
            case EPREDEC:
                expr = expr->data.monadic;
                isModified = TRUE;
                continue;

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
                CInline_CheckUsage(expr->data.diadic.left, TRUE);
                expr = expr->data.diadic.right;
                isModified = FALSE;
                continue;

            case EMUL:
            case EDIV:
            case EMODULO:
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
            case ECOMMA:
            case EROTL:
            case EROTR:
                CInline_CheckUsage(expr->data.diadic.left, FALSE);
                expr = expr->data.diadic.right;
                isModified = FALSE;
                continue;

            case EINTCONST:
            case EFLOATCONST:
            case ESTRINGCONST:
            case EOBJREF:
            case EPRECOMP:
            case ETEMP:
            case ELABEL:
            case EOBJLIST:
            case EVECTOR128CONST:
                return;

            case EMEMBER:
                if (expr->data.emember->expr) {
                    CInline_CheckUsage(expr->data.emember->expr, FALSE);
                }
                return;

            case EFUNCCALL:
            case EFUNCCALLP:
                CInline_CheckUsage(expr->data.funccall.funcref, FALSE);
                for (list = expr->data.funccall.args; list; list = list->next) {
                    CInline_CheckUsage(list->node, FALSE);
                }
                return;

            case ENULLCHECK:
                CInline_CheckUsage(expr->data.nullcheck.nullcheckexpr, FALSE);
                expr = expr->data.nullcheck.condexpr;
                isModified = FALSE;
                continue;

            case EMFPOINTER:
                CInline_CheckUsage(expr->data.mfpointer.accessnode, FALSE);
                expr = expr->data.mfpointer.mfpointer;
                isModified = FALSE;
                continue;

            case ECOND:
                CInline_CheckUsage(expr->data.cond.cond, FALSE);
                CInline_CheckUsage(expr->data.cond.expr1, FALSE);
                expr = expr->data.cond.expr2;
                isModified = FALSE;
                continue;

            case EINSTRUCTION:
                return;

            default:
                CError_FATAL(1146);
        }
    }
}

ENode* CInline_CopyExpression(ENode* expr, CInlineCopyMode mode) {
    enode_copymode = mode;

    switch (mode) {
        case CopyModeLocal:
        case CopyModeExpand:
            enode_idtrans = NULL;
            enode_globalcopy = FALSE;
            expr = CInline_CopyNodes(expr);
            break;
        case CopyModeUnpack:
            enode_globalcopy = FALSE;
            expr = CInline_CopyNodes(expr);
            break;
        case CopyModeGlobal:
            enode_idtrans = NULL;
            enode_globalcopy = TRUE;
            expr = CInline_CopyNodes(expr);
            break;
        case CopyModePack:
            enode_idtrans = NULL;
            enode_globalcopy = TRUE;
            expr = CInline_CopyNodes(expr);
            CInline_CheckUsage(expr, FALSE);
            break;
    }

    return expr;
}

static UInt8 CInline_GetObjectSFlags(Object* object) {
    UInt8 flags;

    switch (object->sclass) {
        case 0:
            flags = CI_SFLAGS_NoClass;
            break;
        case TK_REGISTER:
            flags = CI_SFLAGS_Register;
            break;
        case TK_AUTO:
            flags = CI_SFLAGS_Auto;
            break;
        default:
            CError_FATAL(1204);
    }

    if (object->flags & OBJECT_REFERENCED) {
        flags |= CI_SFLAGS_Referenced;
    }

    return flags;
}

static void CInline_SetObjectSFlags(Object* object, UInt8 sflags) {
    if (sflags & CI_SFLAGS_Referenced) {
        object->flags |= OBJECT_REFERENCED;
        sflags &= ~CI_SFLAGS_Referenced;
    }

    switch (sflags) {
        case CI_SFLAGS_NoClass:
            object->sclass = 0;
            break;
        case CI_SFLAGS_Register:
            object->sclass = TK_REGISTER;
            break;
        case CI_SFLAGS_Auto:
            object->sclass = TK_AUTO;
            break;
        default:
            CError_FATAL(1229);
    }
}

static Object* CInline_NewLocalObject(Type* type, short qual, UInt8 sflags, int unused) {
    Object* object = CParser_NewLocalDataObject(NULL, TRUE);
    object->name = CParser_GetUniqueName();
    object->type = type;
    object->qual = qual;
    CInline_SetObjectSFlags(object, sflags);
    CFunc_SetupLocalVarInfo(object);
    return object;
}

static ENode* CInline_FuncArgConvert(ENode* expr) {
    ENode* copy;

    switch (expr->type) {
        case EOBJREF:
            copy = lalloc(sizeof(ENode));
            *copy = *expr;
            return copy;
        case ETEMP:
            CError_FATAL(1272);
        default:
            break;
    }

    return NULL;
}

static ENode* CInline_RefArgTransform(ENode* expr, Boolean convertArg) {
    ENodeList* arg;

    while (ENODE_IS(expr, ECOMMA)) {
        expr = expr->data.diadic.right;
    }

    switch (expr->type) {
        case EOBJREF:
        case ETEMP:
            if (convertArg) {
                return CInline_FuncArgConvert(expr);
            }
            break;

        case EFUNCCALL:
            if (IS_TYPE_POINTER_ONLY(expr->rtype) && IS_TYPE_CLASS(TPTR_TARGET(expr->rtype))) {
                if (ENODE_IS(expr->data.funccall.funcref, EOBJREF) &&
                    CClass_IsConstructor(expr->data.funccall.funcref->data.objref) &&
                    (arg = expr->data.funccall.args)) {
                    return CInline_FuncArgConvert(arg->node);
                }

                if (TPTR_TARGET(expr->rtype) == expr->data.funccall.functype->functype &&
                    CMach_GetFunctionResultClass(expr->data.funccall.functype) == 1 &&
                    (arg = expr->data.funccall.args)) {
                    switch (CABI_GetStructResultArgumentIndex(expr->data.funccall.functype)) {
                        case 0:
                            break;
                        case 1:
                            if ((arg = arg->next)) {
                                break;
                            }
                            CError_FATAL(1313);
                        default:
                            CError_FATAL(1314);
                    }

                    return CInline_FuncArgConvert(arg->node);
                }
            }
            break;
        default:
            break;
    }

    return NULL;
}

static ENode* CInline_SetupArgsExpression(Object* object, CI_FuncData* data, ENodeList* list) {
    ENode* commaNodes;
    CI_Var* var;
    ENodeList* scan;
    ENode* expr;
    Object* obj;
    SInt32 i;
    Boolean is_oldstyle;

    is_oldstyle = FALSE;
    if (TYPE_FUNC(object->type)->args == &oldstyle) {
        is_oldstyle = TRUE;
    }

    local_dobjects = lalloc(sizeof(Object*) * data->numlocals);
    local_aobjects = lalloc(sizeof(AObject) * data->numarguments);

    for (i = 0, var = data->locals; i < data->numlocals; i++, var++) {
        if (var->used) {
            local_dobjects[i] = obj = CInline_NewLocalObject(var->type, var->qual, var->sflags, 0);
            if (!var->unmodified) {
                obj->flags |= OBJECT_REFERENCED;
            }
        } else {
            local_dobjects[i] = NULL;
        }
    }

    for (i = 0, var = data->arguments, scan = list; i < data->numarguments; i++, var++) {
        local_aobjects[i].evalExpr = NULL;

        if (!var->used) {
            local_aobjects[i].object = NULL;
            local_aobjects[i].useExpr = NULL;
        } else if (scan &&
                   var->unmodified &&
                   !CInline_IsTrivialExpression(scan->node) &&
                   (!is_oldstyle || scan->node->rtype->size == var->type->size)) {
            local_aobjects[i].object = NULL;
            local_aobjects[i].useExpr = scan->node;
        } else if (scan &&
                   var->unmodified &&
                   IS_TYPE_REFERENCE(var->type) &&
                   (expr = CInline_RefArgTransform(scan->node, TRUE))) {
            local_aobjects[i].object = NULL;
            local_aobjects[i].useExpr = expr;
            local_aobjects[i].evalExpr = scan->node;
        } else {
            local_aobjects[i].object = CInline_NewLocalObject(var->type, var->qual, var->sflags, 0);
            local_aobjects[i].useExpr = NULL;
        }

        if (scan) {
            scan = scan->next;
        }
    }

    commaNodes = NULL;

    for (i = 0, scan = list; scan; scan = scan->next, i++) {
        if (i >= data->numarguments) {
            if (!commaNodes) {
                commaNodes = scan->node;
            } else {
                commaNodes = makecommaexpression(scan->node, commaNodes);
            }
        } else if (!local_aobjects[i].object || local_aobjects[i].evalExpr) {
            if (local_aobjects[i].evalExpr) {
                if (!commaNodes) {
                    commaNodes = local_aobjects[i].evalExpr;
                } else {
                    commaNodes = makecommaexpression(local_aobjects[i].evalExpr, commaNodes);
                }
            } else if (!local_aobjects[i].useExpr && CInline_IsTrivialExpression(scan->node)) {
                if (!commaNodes) {
                    commaNodes = scan->node;
                } else {
                    commaNodes = makecommaexpression(scan->node, commaNodes);
                }
                CError_ASSERT(1470, !ENODE_IS(scan->node, EPRECOMP));
            }
        } else {
            if (is_oldstyle && scan->node->rtype->size != local_aobjects[i].object->type->size) {
                scan->node = makemonadicnode(scan->node, ETYPCON);
                scan->node->rtype = local_aobjects[i].object->type;
            }

            expr = makediadicnode(create_objectnode2(local_aobjects[i].object), scan->node, EASS);
            if (!commaNodes) {
                commaNodes = expr;
            } else {
                commaNodes = makecommaexpression(expr, commaNodes);
            }
        }
    }

    return commaNodes;
}

static void CInline_ReturnCheckCB(ENode* expr) {
    cinline_has_sideeffect = TRUE;
}

static ENode* CInline_ReturnCheck(ENode* expr) {
    ENode* copy;

    if (ENODE_IS(expr, EFORCELOAD)) {
        return expr;
    }

    cinline_has_sideeffect = FALSE;
    CExpr_SearchExprTree(expr, CInline_ReturnCheckCB, 3, EINDIRECT, EFUNCCALL, EFUNCCALLP);

    if (!cinline_has_sideeffect) {
        return expr;
    }

    copy = lalloc(sizeof(ENode));
    *copy = *expr;
    copy->type = EFORCELOAD;

    copy->data.monadic = expr;
    return copy;
}

static ENode* CInline_ReturnMemResult(Object* object) {
    int index = CABI_GetStructResultArgumentIndex(TYPE_FUNC(object->type));
    if (local_aobjects[index].object == NULL) {
        return CInline_CopyExpressionSave(local_aobjects[index].useExpr);
    } else {
        return create_objectnode(local_aobjects[index].object);
    }
}

static ENode* CInline_InlineFunctionExpression(ENode* expr) {
    Object* object;
    CI_FuncData* funcdata;
    short i;
    Boolean resultInMemory;
    ENode* argsExpr;

    object = expr->data.funccall.funcref->data.objref;
    if (object->datatype == DALIAS) {
        object = object->u.alias.object;
    }

    funcdata = object->u.func.u.ifuncdata;
    if (!funcdata) {
        return expr;
    }

    if (funcdata->can_inline < CI_CanInlineExpression) {
        if (funcdata->can_inline == CI_CanInlineStatement) {
            if (cinline_unconditionalpart && cinline_stmtlevelexprs < 16) {
                cinline_stmtlevelexpr[cinline_stmtlevelexprs++] = expr;
            }
            cinline_serialize_stmt = TRUE;
        }
        return expr;
    }

    resultInMemory = CMach_GetFunctionResultClass(TYPE_FUNC(object->type)) == 1;
    argsExpr = CInline_SetupArgsExpression(object, funcdata, expr->data.funccall.args);

    for (i = 0; i < funcdata->numstatements; i++) {
        switch (funcdata->statements[i].type) {
            case ST_RETURN:
                if (funcdata->statements[i].u.expr) {
                    ENode* copy =
                        CInline_CopyExpression(funcdata->statements[i].u.expr, CopyModeExpand);
                    if (resultInMemory) {
                        if (argsExpr) {
                            argsExpr = makecommaexpression(argsExpr, copy);
                        } else {
                            argsExpr = copy;
                        }

                        argsExpr = makecommaexpression(argsExpr, CInline_ReturnMemResult(object));
                    } else {
                        if (argsExpr) {
                            argsExpr = makecommaexpression(argsExpr, CInline_ReturnCheck(copy));
                        } else {
                            argsExpr = CInline_ReturnCheck(copy);
                        }
                    }
                }
                break;
            case ST_EXPRESSION:
                if (argsExpr) {
                    argsExpr = makecommaexpression(
                        argsExpr,
                        CInline_CopyExpression(funcdata->statements[i].u.expr, CopyModeExpand)
                    );
                } else {
                    argsExpr =
                        CInline_CopyExpression(funcdata->statements[i].u.expr, CopyModeExpand);
                }
                break;

            default:
                CError_FATAL(1632);
        }
    }

    if (!argsExpr) {
        argsExpr = nullnode();
    }
    if (!IS_TYPE_VOID(expr->rtype)) {
        argsExpr->rtype = expr->rtype;
    }

    inline_expanded = TRUE;
    return CInline_FoldConst(argsExpr);
}

static Boolean CInline_CanExpand(ENode* expr) {
    TypeFunc* tfunc;
    Object* object;

    object = expr->data.objref;
    tfunc = TYPE_FUNC(object->type);

    if (IS_TYPE_FUNC(tfunc) &&
        ((object->qual & Q_INLINE) || (tfunc->flags & FUNC_AUTO_INLINE)) &&
        (object->datatype == DFUNC ||
         (object->datatype == DVFUNC && (expr->flags & ENODE_FLAG_NONVIRTUAL)))) {
        return TRUE;
    }

    return FALSE;
}

static SInt32 CInline_EstimateSizeOfExpr(ENode* expr, SInt32 size, SInt32 level) {
    ENodeList* list;

    switch (expr->type) {
        case EPOSTINC:
        case EPOSTDEC:
        case EPREINC:
        case EPREDEC:
        case EINDIRECT:
        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
        case EFORCELOAD:
        case ETYPCON:
        case EBITFIELD:
            size = CInline_EstimateSizeOfExpr(expr->data.monadic, size, level) + 1;
            break;

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
            size = CInline_EstimateSizeOfExpr(expr->data.diadic.left, size, level);
            if (size <= inline_max_size) {
                size = CInline_EstimateSizeOfExpr(expr->data.diadic.right, size, level) + 1;
            }
            break;

        case EFUNCCALL:
        case EFUNCCALLP:
            if (ENODE_IS(expr->data.funccall.funcref, EOBJREF) &&
                expr->data.funccall.funcref->data.objref->u.func.u.ifuncdata &&
                CInline_CanExpand(expr->data.funccall.funcref)) {
                recursive_inline |= expr->data.funccall.funcref->data.objref == expanding_function;
                if (level == 0) {
                    if (!recursive_inline) {
                        size = inline_max_size + 1;
                    }
                } else {
                    size = CInline_EstimateSizeOfFunc(
                        expr->data.funccall.funcref->data.objref->u.func.u.ifuncdata,
                        size,
                        level - 1
                    );
                }
            } else {
                size++;
            }

            for (list = expr->data.funccall.args; list; list = list->next) {
                if (size > inline_max_size) {
                    break;
                }

                size = CInline_EstimateSizeOfExpr(list->node, size, level);
            }
            break;

        case ECOND:
            size = CInline_EstimateSizeOfExpr(expr->data.cond.cond, size, level);
            if (size <= inline_max_size) {
                size = CInline_EstimateSizeOfExpr(expr->data.cond.expr1, size, level) + 1;
            }
            if (size <= inline_max_size) {
                size = CInline_EstimateSizeOfExpr(expr->data.cond.expr2, size, level) + 1;
            }
            break;

        case ENULLCHECK:
            size = CInline_EstimateSizeOfExpr(expr->data.nullcheck.nullcheckexpr, size, level);
            if (size <= inline_max_size) {
                size = CInline_EstimateSizeOfExpr(expr->data.nullcheck.condexpr, size, level) + 1;
            }
            break;

        case EMFPOINTER:
            size = CInline_EstimateSizeOfExpr(expr->data.mfpointer.accessnode, size, level);
            if (size <= inline_max_size) {
                size = CInline_EstimateSizeOfExpr(expr->data.mfpointer.mfpointer, size, level) + 1;
            }
            break;

        case EMEMBER:
            if (expr->data.emember->expr) {
                size = CInline_EstimateSizeOfExpr(expr->data.emember->expr, size, level) + 1;
            }
            break;

        default:
            size++;
    }

    return size;
}

static SInt32 CInline_EstimateSizeOfFunc(CI_FuncData* funcdata, SInt32 size, SInt32 level) {
    CI_Statement* stmt;
    SInt32 i;

    size += funcdata->numstatements;
    if (size > inline_max_size) {
        return size;
    }

    for (i = 0, stmt = funcdata->statements; i < funcdata->numstatements; i++, stmt++) {
        switch (stmt->type) {
            case ST_NOP:
            case ST_LABEL:
            case ST_GOTO:
            case ST_ASM:
                break;
            case ST_EXPRESSION:
            case ST_IFGOTO:
            case ST_IFNGOTO:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
            case ST_GOTOEXPR:
                size = CInline_EstimateSizeOfExpr(stmt->u.expr, size, level);
                break;
            case ST_SWITCH:
                size = CInline_EstimateSizeOfExpr(stmt->u.switchdata->expr, size, level);
                break;
            case ST_RETURN:
                if (stmt->u.expr) {
                    size = CInline_EstimateSizeOfExpr(stmt->u.expr, size, level);
                }
                break;
            default:
                CError_FATAL(1840);
        }

        if (size > inline_max_size) {
            break;
        }
    }

    return size;
}

static SInt32 EstimateExpandedSizeOfExpr(ENode* expr, SInt32 level) {
    ENodeList* list;
    SInt32 size;

    size = 0;

    switch (expr->type) {
        case EPOSTINC:
        case EPOSTDEC:
        case EPREINC:
        case EPREDEC:
        case EINDIRECT:
        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
        case EFORCELOAD:
        case ETYPCON:
        case EBITFIELD:
            size++;
            size += EstimateExpandedSizeOfExpr(expr->data.monadic, level);
            break;

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
            size++;
            size += EstimateExpandedSizeOfExpr(expr->data.diadic.left, level);
            size += EstimateExpandedSizeOfExpr(expr->data.diadic.right, level);
            break;

        case EFUNCCALL:
        case EFUNCCALLP:
            if (ENODE_IS(expr->data.funccall.funcref, EOBJREF) &&
                expr->data.funccall.funcref->data.objref->u.func.u.ifuncdata &&
                CInline_CanExpand(expr->data.funccall.funcref)) {
                if (level) {
                    SInt32 est = CInline_EstimateSizeOfFunc(
                        expr->data.funccall.funcref->data.objref->u.func.u.ifuncdata,
                        size,
                        level - 1
                    );
                    if (est > inline_max_size) {
                        size++;
                    } else {
                        size += est;
                    }
                } else {
                    size++;
                }
            } else {
                size++;
            }

            for (list = expr->data.funccall.args; list; list = list->next) {
                size += EstimateExpandedSizeOfExpr(list->node, level);
            }
            break;

        case ECOND:
            size++;
            size += EstimateExpandedSizeOfExpr(expr->data.cond.cond, level);
            size += EstimateExpandedSizeOfExpr(expr->data.cond.expr1, level);
            size += EstimateExpandedSizeOfExpr(expr->data.cond.expr2, level);
            break;

        case ENULLCHECK:
            size++;
            size += EstimateExpandedSizeOfExpr(expr->data.nullcheck.nullcheckexpr, level);
            size += EstimateExpandedSizeOfExpr(expr->data.nullcheck.condexpr, level);
            break;

        case EMFPOINTER:
            size++;
            size += EstimateExpandedSizeOfExpr(expr->data.mfpointer.accessnode, level);
            size += EstimateExpandedSizeOfExpr(expr->data.mfpointer.mfpointer, level);
            break;

        case EMEMBER:
            if (expr->data.emember->expr) {
                size = EstimateExpandedSizeOfExpr(expr->data.emember->expr, level);
            }
            break;

        default:
            size++;
    }

    return size;
}

static SInt32 EstimateExpandedSizeOfFunction(Statement* stmt) {
    SInt32 size;
    SInt32 level;

    level = copts.inlinelevel;
    if (!level) {
        level = 8;
    }

    size = 0;

    while (stmt) {
        switch (stmt->type) {
            case ST_NOP:
                break;
            case ST_EXPRESSION:
            case ST_SWITCH:
            case ST_IFGOTO:
            case ST_IFNGOTO:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
            case ST_GOTOEXPR:
                size++;
                size += EstimateExpandedSizeOfExpr(stmt->expr, level);
                break;
            case ST_RETURN:
                size++;
                if (stmt->expr) {
                    size += EstimateExpandedSizeOfExpr(stmt->expr, level);
                }
                break;
            case ST_LABEL:
            case ST_GOTO:
            case ST_ASM:
                size++;
                break;
            default:
                CError_FATAL(2015);
        }

        stmt = stmt->next;
    }

    return size;
}

static Boolean CInline_InlineFunctionCheck(ENode* expr) {
    Object* object;
    SInt32 level;

    object = expr->data.objref;
    if (object->datatype == DALIAS) {
        object = object->u.alias.object;
    }

    if (IS_TYPE_FUNC(object->type) &&
        ((object->qual & Q_INLINE) || (TYPE_FUNC(object->type)->flags & FUNC_AUTO_INLINE)) &&
        (object->datatype == DFUNC ||
         (object->datatype == DVFUNC && (expr->flags & ENODE_FLAG_NONVIRTUAL)))) {
        if (copts.alwaysinline) {
            return TRUE;
        }

        if (copts.inline_bottom_up) {
            CI_FuncData* funcdata;
            if (!(funcdata = object->u.func.u.ifuncdata)) {
                return FALSE;
            }

            if (copts.inlinelevel == 0) {
                level = 7 - cinline_level;
            } else {
                level = copts.inlinelevel - cinline_level - 1;
            }

            if (CInline_EstimateSizeOfFunc(funcdata, 0, level) > inline_max_size) {
                return FALSE;
            }
        } else if (cinline_level > 0 && copts.inlinelevel == 0) {
            CI_FuncData* funcdata;
            funcdata = object->u.func.u.ifuncdata;
            if (!funcdata) {
                return FALSE;
            }

            if (funcdata->numstatements > 10) {
                return FALSE;
            }
            if (cinline_level > 1 && funcdata->numstatements > 7) {
                return FALSE;
            }
            if (cinline_level > 2 && funcdata->numstatements > 3) {
                return FALSE;
            }
        }

        return TRUE;
    } else {
        return FALSE;
    }

    return FALSE;
}

static ENode* CInline_ExpandExpression(ENode* expr) {
    ENodeList* list;
    Boolean save;

    switch (expr->type) {
        case EPOSTINC:
        case EPOSTDEC:
        case EPREINC:
        case EPREDEC:
        case EINDIRECT:
        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
        case ETYPCON:
        case EBITFIELD:
            expr->data.monadic = CInline_ExpandExpression(expr->data.monadic);
            break;

        case EFORCELOAD:
            expr->data.monadic = CInline_ExpandExpression(expr->data.monadic);
            if (ENODE_IS(expr->data.monadic, EFORCELOAD)) {
                expr->data.monadic = expr->data.monadic->data.monadic;
            }
            break;

        case EMUL:
        case EDIV:
        case EMODULO:
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
        case EPMODULO:
        case EROTL:
        case EROTR:
            expr->data.diadic.left = CInline_ExpandExpression(expr->data.diadic.left);
            expr->data.diadic.right = CInline_ExpandExpression(expr->data.diadic.right);
            break;

        case ELAND:
        case ELOR:
        case ECOMMA:
            expr->data.diadic.left = CInline_ExpandExpression(expr->data.diadic.left);
            save = cinline_unconditionalpart;
            cinline_unconditionalpart = FALSE;
            expr->data.diadic.right = CInline_ExpandExpression(expr->data.diadic.right);
            cinline_unconditionalpart = save;
            break;

        case EFUNCCALL:
        case EFUNCCALLP:
            expr->data.funccall.funcref = CInline_ExpandExpression(expr->data.funccall.funcref);
            for (list = expr->data.funccall.args; list; list = list->next) {
                list->node = CInline_ExpandExpression(list->node);
            }

            if (ENODE_IS(expr->data.funccall.funcref, EOBJREF) &&
                CInline_InlineFunctionCheck(expr->data.funccall.funcref)) {
                expr = CInline_InlineFunctionExpression(expr);
            }
            break;

        case ENULLCHECK:
            expr->data.nullcheck.nullcheckexpr =
                CInline_ExpandExpression(expr->data.nullcheck.nullcheckexpr);
            save = cinline_unconditionalpart;
            cinline_unconditionalpart = FALSE;
            expr->data.nullcheck.condexpr = CInline_ExpandExpression(expr->data.nullcheck.condexpr);
            cinline_unconditionalpart = save;
            break;

        case EMFPOINTER:
            expr->data.mfpointer.accessnode =
                CInline_ExpandExpression(expr->data.mfpointer.accessnode);
            expr->data.mfpointer.mfpointer =
                CInline_ExpandExpression(expr->data.mfpointer.mfpointer);
            break;

        case ECOND:
            expr->data.cond.cond = CInline_ExpandExpression(expr->data.cond.cond);
            save = cinline_unconditionalpart;
            cinline_unconditionalpart = FALSE;
            expr->data.cond.expr1 = CInline_ExpandExpression(expr->data.cond.expr1);
            expr->data.cond.expr2 = CInline_ExpandExpression(expr->data.cond.expr2);
            cinline_unconditionalpart = save;
            break;

        case EMEMBER:
            if (expr->data.emember->expr) {
                expr = CInline_ExpandExpression(expr->data.emember->expr);
            } else {
                expr = nullnode();
            }
            break;

        case EINTCONST:
        case EFLOATCONST:
        case ESTRINGCONST:
        case EOBJREF:
        case EPRECOMP:
        case ELABEL:
        case EOBJLIST:
        case EINSTRUCTION:
        case EVECTOR128CONST:
            break;

        default:
            CError_FATAL(2235);
    }

    return expr;
}

static Statement* CInline_NewStatement(StatementType sttype) {
    Statement* stmt = lalloc(sizeof(Statement));
    memclrw(stmt, sizeof(Statement));

    stmt->type = sttype;
    if (cinline_serial_stmt) {
        cinline_cur_serial_stmt->next = stmt;
    } else {
        cinline_serial_stmt = stmt;
    }

    return cinline_cur_serial_stmt = stmt;
}

static ENode* CInline_LoadToTemp(ENode* expr, Object** objectptr) {
    Object* object;

    if (!(object = *objectptr)) {
        switch (expr->rtype->type) {
            case TYPEVOID:
                return expr;
            case TYPEINT:
            case TYPEFLOAT:
            case TYPEENUM:
            case TYPESTRUCT:
            case TYPECLASS:
            case TYPEMEMBERPOINTER:
            case TYPEPOINTER:
                object = create_temp_object(expr->rtype);
                *objectptr = object;
                break;
            default:
                CError_FATAL(2288);
        }
    }

    return makediadicnode(create_objectnode(object), expr, EASS);
}

static ENode* CInline_SerializeEFORCELOAD(ENode* expr) {
    Statement* stmt;
    Object* temp = NULL;

    while (ENODE_IS(expr->data.monadic, EFORCELOAD)) {
        expr->data.monadic = expr->data.monadic->data.monadic;
    }

    expr->data.monadic = CInline_SerializeExpr(expr->data.monadic);
    stmt = CInline_NewStatement(ST_EXPRESSION);
    stmt->expr = CInline_LoadToTemp(expr->data.monadic, &temp);
    return create_objectnode(temp);
}

static ENode* CInline_SerializeECOMMA(ENode* expr) {
    Statement* stmt;

    expr->data.diadic.left = CInline_SerializeExpr(expr->data.diadic.left);
    stmt = CInline_NewStatement(ST_EXPRESSION);
    stmt->expr = expr->data.diadic.left;
    return CInline_SerializeExpr(expr->data.diadic.right);
}

static ENode* CInline_SerializeELOR(ENode* expr) {
    ENode* n;
    Statement* stmt;
    CLabel* label;
    Object* temp = NULL;

    label = newlabel();

    n = CInline_SerializeExpr(expr->data.diadic.left);
    n = makemonadicnode(n, ELOGNOT);
    n->rtype = expr->rtype;
    n = makemonadicnode(n, ELOGNOT);
    n = CInline_LoadToTemp(n, &temp);
    stmt = CInline_NewStatement(ST_IFGOTO);
    stmt->expr = n;
    stmt->label = label;

    n = CInline_SerializeExpr(expr->data.diadic.right);
    n = makemonadicnode(n, ELOGNOT);
    n->rtype = expr->rtype;
    n = makemonadicnode(n, ELOGNOT);
    n = CInline_LoadToTemp(n, &temp);
    stmt = CInline_NewStatement(ST_EXPRESSION);
    stmt->expr = n;

    stmt = CInline_NewStatement(ST_LABEL);
    stmt->label = label;
    label->stmt = stmt;

    return create_objectnode(temp);
}

static ENode* CInline_SerializeELAND(ENode* expr) {
    ENode* n;
    Statement* stmt;
    CLabel* label;
    Object* temp = NULL;

    label = newlabel();

    n = CInline_SerializeExpr(expr->data.diadic.left);
    n = makemonadicnode(n, ELOGNOT);
    n->rtype = expr->rtype;
    n = makemonadicnode(n, ELOGNOT);
    n = CInline_LoadToTemp(n, &temp);
    stmt = CInline_NewStatement(ST_IFNGOTO);
    stmt->expr = n;
    stmt->label = label;

    n = CInline_SerializeExpr(expr->data.diadic.right);
    n = makemonadicnode(n, ELOGNOT);
    n->rtype = expr->rtype;
    n = makemonadicnode(n, ELOGNOT);
    n = CInline_LoadToTemp(n, &temp);
    stmt = CInline_NewStatement(ST_EXPRESSION);
    stmt->expr = n;

    stmt = CInline_NewStatement(ST_LABEL);
    stmt->label = label;
    label->stmt = stmt;

    return create_objectnode(temp);
}

static ENode* CInline_SerializeEPRECOMP(ENode* expr) {
    UIDTemp* uidtemp;

    uidtemp = cinline_uid_temps;
    while (1) {
        if (!uidtemp) {
            CError_FATAL(2449);
        }
        if (uidtemp->uid == expr->data.precompid) {
            return create_objectnode(uidtemp->object);
        }
        uidtemp = uidtemp->next;
    }
}

static ENode* CInline_SerializeENULLCHECK(ENode* expr) {
    Statement* stmt;
    CLabel* label;
    ENode* n;
    Object* temp = NULL;
    UIDTemp uidtemp;

    label = newlabel();

    n = CInline_SerializeExpr(expr->data.nullcheck.nullcheckexpr);
    stmt = CInline_NewStatement(ST_IFNGOTO);
    stmt->expr = CInline_LoadToTemp(n, &temp);
    stmt->label = label;

    uidtemp.next = cinline_uid_temps;
    uidtemp.object = temp;
    uidtemp.uid = expr->data.nullcheck.precompid;
    cinline_uid_temps = &uidtemp;

    n = CInline_SerializeExpr(expr->data.nullcheck.condexpr);
    stmt = CInline_NewStatement(ST_EXPRESSION);
    stmt->expr = CInline_LoadToTemp(n, &temp);

    cinline_uid_temps = uidtemp.next;

    stmt = CInline_NewStatement(ST_LABEL);
    stmt->label = label;
    label->stmt = stmt;

    return create_objectnode(temp);
}

static ENode* CInline_SerializeECOND(ENode* expr) {
    ENode* n;
    Statement* stmt;
    CLabel* label1;
    CLabel* label2;
    Object* temp = NULL;

    label1 = newlabel();
    label2 = newlabel();

    n = CInline_SerializeExpr(expr->data.cond.cond);
    stmt = CInline_NewStatement(ST_IFNGOTO);
    stmt->expr = n;
    stmt->label = label1;

    n = CInline_SerializeExpr(expr->data.cond.expr1);
    n = CInline_LoadToTemp(n, &temp);
    stmt = CInline_NewStatement(ST_EXPRESSION);
    stmt->expr = n;

    stmt = CInline_NewStatement(ST_GOTO);
    stmt->label = label2;

    stmt = CInline_NewStatement(ST_LABEL);
    stmt->label = label1;
    label1->stmt = stmt;

    n = CInline_SerializeExpr(expr->data.cond.expr2);
    n = CInline_LoadToTemp(n, &temp);
    stmt = CInline_NewStatement(ST_EXPRESSION);
    stmt->expr = n;

    stmt = CInline_NewStatement(ST_LABEL);
    stmt->label = label2;
    label2->stmt = stmt;

    if (!temp) {
        n = nullnode();
        n->rtype = &stvoid;
        return n;
    }

    return create_objectnode(temp);
}

static ENode* CInline_SerializeExpr(ENode* expr) {
    ENodeList* list;

    switch (expr->type) {
        case EFORCELOAD:
            return CInline_SerializeEFORCELOAD(expr);
        case ECOMMA:
            return CInline_SerializeECOMMA(expr);
        case ELAND:
            return CInline_SerializeELAND(expr);
        case ELOR:
            return CInline_SerializeELOR(expr);
        case EPRECOMP:
            return CInline_SerializeEPRECOMP(expr);
        case ENULLCHECK:
            return CInline_SerializeENULLCHECK(expr);
        case ECOND:
            return CInline_SerializeECOND(expr);

        case EINITTRYCATCH:
            expr->data.itc.initexpr = CInline_SerializeExpr(expr->data.itc.initexpr);
            expr->data.itc.tryexpr = CInline_SerializeExpr(expr->data.itc.tryexpr);
            expr->data.itc.catchexpr = CInline_SerializeExpr(expr->data.itc.catchexpr);
            expr->data.itc.result = CInline_SerializeExpr(expr->data.itc.result);
            return expr;

        case EPOSTINC:
        case EPOSTDEC:
        case EPREINC:
        case EPREDEC:
        case EINDIRECT:
        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
        case ETYPCON:
        case EBITFIELD:
            expr->data.monadic = CInline_SerializeExpr(expr->data.monadic);
            return expr;

        case EMUL:
        case EDIV:
        case EMODULO:
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
        case EPMODULO:
        case EROTL:
        case EROTR:
            expr->data.diadic.left = CInline_SerializeExpr(expr->data.diadic.left);
            expr->data.diadic.right = CInline_SerializeExpr(expr->data.diadic.right);
            return expr;

        case EINTCONST:
        case EFLOATCONST:
        case ESTRINGCONST:
        case EOBJREF:
        case ELABEL:
        case EOBJLIST:
        case EINSTRUCTION:
        case EVECTOR128CONST:
            return expr;

        case EFUNCCALL:
        case EFUNCCALLP:
            expr->data.funccall.funcref = CInline_SerializeExpr(expr->data.funccall.funcref);
            for (list = expr->data.funccall.args; list; list = list->next) {
                list->node = CInline_SerializeExpr(list->node);
            }
            return expr;

        case EMFPOINTER:
            expr->data.mfpointer.accessnode =
                CInline_SerializeExpr(expr->data.mfpointer.accessnode);
            expr->data.mfpointer.accessnode = CInline_SerializeExpr(expr->data.mfpointer.mfpointer);
            return expr;

        case EMEMBER:
            if (expr->data.emember->expr) {
                return CInline_SerializeExpr(expr->data.emember->expr);
            }
            return expr;

        default:
            CError_FATAL(2684);
            return expr;
    }
}

void CInline_SerializeStatement(Statement* stmt) {
    Statement* scan;
    Statement* copy;

    cinline_serial_stmt = NULL;
    cinline_uid_temps = NULL;
    stmt->expr = CInline_SerializeExpr(stmt->expr);

    if (cinline_serial_stmt) {
        for (scan = cinline_serial_stmt; scan; scan = scan->next) {
            scan->value = stmt->value;
            scan->dobjstack = stmt->dobjstack;
            scan->sourceoffset = stmt->sourceoffset;
            scan->sourcefilepath = stmt->sourcefilepath;
        }

        copy = CInline_NewStatement(ST_EXPRESSION);
        *copy = *stmt;

        *stmt = *cinline_serial_stmt;
    }
}

static void CInline_UnpackSwitch(Statement* stmt, CI_Statement* packstmt, CLabel** labels) {
    SwitchInfo* info;
    SwitchCase* swcase;
    short i;

    info = lalloc(sizeof(SwitchInfo));
    stmt->label = (CLabel*)info;
    CError_ASSERT(2730, info->defaultlabel = labels[packstmt->u.switchdata->defaultlabelID]);
    info->exprtype = packstmt->u.switchdata->exprtype;

    for (i = 0; i < packstmt->u.switchdata->numcases; i++) {
        if (i == 0) {
            swcase = lalloc(sizeof(SwitchCase));
            info->cases = swcase;
        } else {
            swcase->next = lalloc(sizeof(SwitchCase));
            swcase = swcase->next;
        }

        swcase->next = NULL;
        swcase->min = packstmt->u.switchdata->cases[i].min;
#if VERSION < VERSION_GC_2_0
        swcase->max = packstmt->u.switchdata->cases[i].max;
#endif
        CError_ASSERT(2740, swcase->label = labels[packstmt->u.switchdata->cases[i].labelID]);
    }
}

Object* CInline_GetLocalObj(SInt32 id, Boolean inlineCopy) {
    ObjectList* list;

    if (id) {
        if (id & 0x80000000) {
            id = (id & ~0x80000000) - 1;
            if (inlineCopy) {
                CError_ASSERT(2761, local_aobjects[id].object);
                return local_aobjects[id].object;
            }

            for (list = arguments; list; list = list->next, id--) {
                if (id == 0) {
                    return list->object;
                }
            }

            CError_FATAL(2765);
        } else {
            id--;
            if (inlineCopy) {
                CError_ASSERT(2772, local_dobjects[id]);
                return local_dobjects[id];
            }

            for (list = locals; list; list = list->next, id--) {
                if (id == 0) {
                    return list->object;
                }
            }

            CError_FATAL(2776);
        }
    }

    return NULL;
}

static ExceptionAction* CInline_UnpackActions(CI_Statement* packstmt, Boolean inlineCopy) {
    ExceptionAction* packexc;
    ExceptionAction* last;
    ExceptionAction* exc;

    packexc = packstmt->dobjstack;
    last = NULL;

    while (packexc) {
        exc = galloc(sizeof(ExceptionAction));
        exc->prev = last;
        last = exc;

        exc->type = packexc->type;

        switch (packexc->type) {
            case EAT_DESTROYLOCAL:
                exc->data.destroy_local.local =
                    CInline_GetLocalObj((SInt32)packexc->data.destroy_local.local, inlineCopy);
                exc->data.destroy_local.dtor = packexc->data.destroy_local.dtor;
                break;
            case EAT_DESTROYLOCALCOND:
                exc->data.destroy_local_cond.local =
                    CInline_GetLocalObj((SInt32)packexc->data.destroy_local_cond.local, inlineCopy);
                exc->data.destroy_local_cond.dtor = packexc->data.destroy_local_cond.dtor;
                exc->data.destroy_local_cond.cond =
                    CInline_GetLocalObj((SInt32)packexc->data.destroy_local_cond.cond, inlineCopy);
                break;
            case EAT_DESTROYLOCALOFFSET:
                exc->data.destroy_local_offset.local = CInline_GetLocalObj(
                    (SInt32)packexc->data.destroy_local_offset.local, inlineCopy
                );
                exc->data.destroy_local_offset.dtor = packexc->data.destroy_local_offset.dtor;
                exc->data.destroy_local_offset.offset = packexc->data.destroy_local_offset.offset;
                break;
            case EAT_DESTROYLOCALPOINTER:
                exc->data.destroy_local_pointer.pointer = CInline_GetLocalObj(
                    (SInt32)packexc->data.destroy_local_pointer.pointer, inlineCopy
                );
                exc->data.destroy_local_pointer.dtor = packexc->data.destroy_local_pointer.dtor;
                break;
            case EAT_DESTROYLOCALARRAY:
                exc->data.destroy_local_array.localarray = CInline_GetLocalObj(
                    (SInt32)packexc->data.destroy_local_array.localarray, inlineCopy
                );
                exc->data.destroy_local_array.dtor = packexc->data.destroy_local_array.dtor;
                exc->data.destroy_local_array.elements = packexc->data.destroy_local_array.elements;
                exc->data.destroy_local_array.element_size =
                    packexc->data.destroy_local_array.element_size;
                break;
            case EAT_DESTROYPARTIALARRAY:
                exc->data.destroy_partial_array.arraypointer = CInline_GetLocalObj(
                    (SInt32)packexc->data.destroy_partial_array.arraypointer, inlineCopy
                );
                exc->data.destroy_partial_array.arraycounter = CInline_GetLocalObj(
                    (SInt32)packexc->data.destroy_partial_array.arraycounter, inlineCopy
                );
                exc->data.destroy_partial_array.dtor = CInline_GetLocalObj(
                    (SInt32)packexc->data.destroy_partial_array.dtor, inlineCopy
                );
                exc->data.destroy_partial_array.element_size = CInline_GetLocalObj(
                    (SInt32)packexc->data.destroy_partial_array.element_size, inlineCopy
                );
                break;
            case EAT_DESTROYMEMBER:
            case EAT_DESTROYBASE:
                exc->data.destroy_member.objectptr =
                    CInline_GetLocalObj((SInt32)packexc->data.destroy_member.objectptr, inlineCopy);
                exc->data.destroy_member.dtor = packexc->data.destroy_member.dtor;
                exc->data.destroy_member.offset = packexc->data.destroy_member.offset;
                break;
            case EAT_DESTROYMEMBERCOND:
                exc->data.destroy_member_cond.objectptr = CInline_GetLocalObj(
                    (SInt32)packexc->data.destroy_member_cond.objectptr, inlineCopy
                );
                exc->data.destroy_member_cond.cond =
                    CInline_GetLocalObj((SInt32)packexc->data.destroy_member_cond.cond, inlineCopy);
                exc->data.destroy_member_cond.dtor = packexc->data.destroy_member_cond.dtor;
                exc->data.destroy_member_cond.offset = packexc->data.destroy_member_cond.offset;
                break;
            case EAT_DESTROYMEMBERARRAY:
                exc->data.destroy_member_array.objectptr = CInline_GetLocalObj(
                    (SInt32)packexc->data.destroy_member_array.objectptr, inlineCopy
                );
                exc->data.destroy_member_array.dtor = packexc->data.destroy_member_array.dtor;
                exc->data.destroy_member_array.offset = packexc->data.destroy_member_array.offset;
                exc->data.destroy_member_array.elements =
                    packexc->data.destroy_member_array.elements;
                exc->data.destroy_member_array.element_size =
                    packexc->data.destroy_member_array.element_size;
                break;
            case EAT_DELETEPOINTER:
            case EAT_DELETELOCALPOINTER:
                exc->data.delete_pointer.pointerobject = CInline_GetLocalObj(
                    (SInt32)packexc->data.delete_pointer.pointerobject, inlineCopy
                );
                exc->data.delete_pointer.deletefunc = packexc->data.delete_pointer.deletefunc;
                break;
            case EAT_DELETEPOINTERCOND:
                exc->data.delete_pointer_cond.pointerobject = CInline_GetLocalObj(
                    (SInt32)packexc->data.delete_pointer_cond.pointerobject, inlineCopy
                );
                exc->data.delete_pointer_cond.deletefunc =
                    packexc->data.delete_pointer_cond.deletefunc;
                exc->data.delete_pointer_cond.cond =
                    CInline_GetLocalObj((SInt32)packexc->data.delete_pointer_cond.cond, inlineCopy);
                break;
            case EAT_CATCHBLOCK: {
                LabelTrans* trans;
                exc->data.catch_block.catch_object =
                    CInline_GetLocalObj((SInt32)packexc->data.catch_block.catch_object, inlineCopy);
                exc->data.catch_block.catch_info_object = CInline_GetLocalObj(
                    (SInt32)packexc->data.catch_block.catch_info_object, inlineCopy
                );

                trans = lalloc(sizeof(LabelTrans));
                trans->next = cinline_label_trans;
                cinline_label_trans = trans;

                trans->id = (SInt32)packexc->data.catch_block.catch_label;
                trans->labelptr = &exc->data.catch_block.catch_label;

                exc->data.catch_block.catch_typeid = packexc->data.catch_block.catch_typeid;
                exc->data.catch_block.catch_type = packexc->data.catch_block.catch_type;
                exc->data.catch_block.catch_qual = packexc->data.catch_block.catch_qual;
                break;
            }
            case EAT_ACTIVECATCHBLOCK:
                exc->data.active_catch_block.catch_info_object = CInline_GetLocalObj(
                    (SInt32)packexc->data.active_catch_block.catch_info_object, inlineCopy
                );
                break;
            case EAT_SPECIFICATION: {
                LabelTrans* trans;
                exc->data.specification.unexp_ids = packexc->data.specification.unexp_ids;
                exc->data.specification.unexp_id = packexc->data.specification.unexp_id;

                trans = lalloc(sizeof(LabelTrans));
                trans->next = cinline_label_trans;
                cinline_label_trans = trans;

                trans->id = (SInt32)packexc->data.specification.unexp_label;
                trans->labelptr = &exc->data.specification.unexp_label;

                exc->data.specification.unexp_info_object = CInline_GetLocalObj(
                    (SInt32)packexc->data.specification.unexp_info_object, inlineCopy
                );
                break;
            }
            case EAT_TERMINATE:
                break;
            default:
                CError_FATAL(2904);
        }

        packexc = packexc->prev;
    }

    return last;
}

static Statement* CInline_ExpandStatements(
    Object* funcobj,
    Statement* stmt,
    CI_FuncData* funcdata,
    ENode* funccall,
    CLabel* label,
    Object* resultobj,
    Boolean appendOriginal
) {
    CLabel** labels;
    CI_Statement* packstmt;
    short i;
    CI_StmtLink* stmtLinks;
    CI_StmtLink* link;
    ENode* setupArgs;
    Boolean resultInMemory;
    Statement origStmt;

    origStmt = *stmt;
    resultInMemory = CMach_GetFunctionResultClass(TYPE_FUNC(funcobj->type)) == 1;

    if ((setupArgs =
             CInline_SetupArgsExpression(funcobj, funcdata, funccall->data.funccall.args))) {
        stmt->type = ST_EXPRESSION;
        stmt->expr = CInline_FoldConst(setupArgs);
    } else {
        stmt->type = ST_NOP;
    }

    stmtLinks = NULL;
    cinline_label_trans = NULL;

    labels = lalloc(sizeof(CLabel*) * funcdata->numstatements);
    memclrw(labels, sizeof(CLabel*) * funcdata->numstatements);

    for (i = 0, packstmt = funcdata->statements; i < funcdata->numstatements; i++, packstmt++) {
        stmt->next = lalloc(sizeof(Statement));
        stmt = stmt->next;
        *stmt = origStmt;

        stmt->type = packstmt->type;
        stmt->flags = packstmt->flags;
        stmt->value += packstmt->value;

        if (packstmt->dobjstack) {
            ExceptionAction* unpacked = CInline_UnpackActions(packstmt, TRUE);
            if (stmt->dobjstack) {
                ExceptionAction* scan = unpacked;
                while (scan->prev) {
                    scan = scan->prev;
                }
                scan->prev = stmt->dobjstack;
            }
            stmt->dobjstack = unpacked;
        }

        switch (stmt->type) {
            case ST_NOP:
                break;

            case ST_EXPRESSION:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
            case ST_GOTOEXPR:
                stmt->expr =
                    CInline_FoldConst(CInline_CopyExpression(packstmt->u.expr, CopyModeExpand));
                break;

            case ST_RETURN:
                if (packstmt->u.expr) {
                    stmt->expr =
                        CInline_FoldConst(CInline_CopyExpression(packstmt->u.expr, CopyModeExpand));
                    if (resultInMemory) {
                        stmt->expr =
                            makecommaexpression(stmt->expr, CInline_ReturnMemResult(funcobj));
                    }

                    if (resultobj) {
                        stmt->type = ST_EXPRESSION;
                        stmt->expr =
                            makediadicnode(create_objectnode2(resultobj), stmt->expr, EASS);
                    } else {
                        stmt->type = origStmt.type;
                        if (stmt->type == ST_EXPRESSION &&
                            !CInline_ExpressionHasSideEffect(stmt->expr)) {
                            stmt->type = ST_NOP;
                        }
                    }

                    if (label) {
                        stmt->next = lalloc(sizeof(Statement));
                        stmt = stmt->next;
                        *stmt = origStmt;
                        stmt->type = ST_GOTO;
                        stmt->label = label;
                    }
                } else {
                    if (label) {
                        stmt->type = ST_GOTO;
                        stmt->label = label;
                    } else {
                        stmt->type = ST_NOP;
                    }
                }
                break;

            case ST_LABEL:
                labels[i] = stmt->label = newlabel();
                stmt->label->stmt = stmt;
                break;

            case ST_IFGOTO:
            case ST_IFNGOTO:
                stmt->expr = CInline_FoldConst(
                    CInline_CopyExpression(packstmt->u.ifgoto.expr, CopyModeExpand)
                );
            case ST_GOTO:
                link = lalloc(sizeof(CI_StmtLink));
                link->next = stmtLinks;
                stmtLinks = link;

                link->stmt = stmt;
                link->ciStmt = packstmt;
                break;

            case ST_SWITCH:
                stmt->expr = CInline_FoldConst(
                    CInline_CopyExpression(packstmt->u.switchdata->expr, CopyModeExpand)
                );
            case ST_ASM:
                link = lalloc(sizeof(CI_StmtLink));
                link->next = stmtLinks;
                stmtLinks = link;

                link->stmt = stmt;
                link->ciStmt = packstmt;
                break;

            default:
                CError_FATAL(3040);
        }
    }

    if (label) {
        stmt->next = lalloc(sizeof(Statement));
        stmt = stmt->next;
        *stmt = origStmt;

        stmt->type = ST_LABEL;
        stmt->label = label;
        label->stmt = stmt;

        if (appendOriginal) {
            stmt->next = lalloc(sizeof(Statement));
            stmt = stmt->next;
            *stmt = origStmt;
        }
    }

    while (stmtLinks) {
        Statement* linkstmt = stmtLinks->stmt;
        packstmt = stmtLinks->ciStmt;

        switch (linkstmt->type) {
            case ST_GOTO:
                CError_ASSERT(3060, linkstmt->label = labels[packstmt->u.statementnum]);
                break;
            case ST_IFGOTO:
            case ST_IFNGOTO:
                CError_ASSERT(3065, linkstmt->label = labels[packstmt->u.ifgoto.statementnum]);
                break;
            case ST_SWITCH:
                CInline_UnpackSwitch(linkstmt, packstmt, labels);
                break;
            case ST_ASM:
                InlineAsm_UnpackAsmStatement(
                    linkstmt, labels, TRUE, packstmt->u.asmdata.data, packstmt->u.asmdata.size
                );
                break;
            default:
                CError_FATAL(3076);
        }

        stmtLinks = stmtLinks->next;
    }

    while (cinline_label_trans) {
        CError_ASSERT(3083, *cinline_label_trans->labelptr = labels[cinline_label_trans->id]);
        cinline_label_trans = cinline_label_trans->next;
    }

    return stmt;
}

static Statement* CInline_InlineFunctionStatement(Statement* stmt, Boolean* changed) {
    Object* object;
    CI_FuncData* funcdata;
    CLabel* label;

    *changed = FALSE;

    object = stmt->expr->data.funccall.funcref->data.objref;
    if (object->datatype == DALIAS) {
        object = object->u.alias.object;
    }

    funcdata = object->u.func.u.ifuncdata;
    if (!funcdata || funcdata->can_inline < CI_CanInlineStatement) {
        return stmt;
    }

    if (stmt->type != ST_EXPRESSION) {
        short i;
        for (i = 0; i < (funcdata->numstatements - 1); i++) {
            if (funcdata->statements[i].type == ST_RETURN) {
                return stmt;
            }
        }

        if (funcdata->statements[funcdata->numstatements - 1].type != ST_RETURN) {
            return stmt;
        }

        label = NULL;
    } else {
        label = newlabel();
    }

    *changed = TRUE;
    return CInline_ExpandStatements(object, stmt, funcdata, stmt->expr, label, NULL, FALSE);
}

static Statement* CInline_ExtractInlineFunction(Statement* stmt) {
    TypeFunc* tfunc;
    ENode* expr;
    CI_FuncData* funcdata;
    short i;
    Object* funcObject;
    Object* resultObject;

    for (i = 0; i < cinline_stmtlevelexprs; i++) {
        expr = cinline_stmtlevelexpr[i];

        funcObject = expr->data.funccall.funcref->data.objref;
        if (funcObject->datatype == DALIAS) {
            funcObject = funcObject->u.alias.object;
        }

        if ((funcdata = funcObject->u.func.u.ifuncdata)) {
            tfunc = TYPE_FUNC(funcObject->type);
            CError_ASSERT(3141, IS_TYPE_FUNC(tfunc));

            if (!IS_TYPE_VOID(tfunc->functype)) {
                if (CMach_GetFunctionResultClass(TYPE_FUNC(funcObject->type)) == 1) {
                    resultObject =
                        CInline_NewLocalObject(CDecl_NewPointerType(tfunc->functype), 0, 0, 0);
                } else {
                    resultObject = CInline_NewLocalObject(tfunc->functype, 0, 0, 0);
                }
            } else {
                resultObject = NULL;
            }

            stmt = CInline_ExpandStatements(
                funcObject, stmt, funcdata, expr, newlabel(), resultObject, TRUE
            );

            if (resultObject) {
                *expr = *create_objectnode2(resultObject);
            } else {
                *expr = *nullnode();
            }
        }
    }

    return stmt;
}

static Statement* CInline_ExpandStatement(Statement* stmt) {
    ENode* expr;
    Boolean changed;

    do {
        changed = FALSE;

        if (stmt->type == ST_EXPRESSION &&
            ENODE_IS(stmt->expr, EINDIRECT) &&
            !CParser_IsVolatile(stmt->expr->rtype, ENODE_QUALS(stmt->expr))) {
            stmt->expr = stmt->expr->data.monadic;
            changed = TRUE;
            if (ENODE_IS2(stmt->expr, EOBJREF, EBITFIELD)) {
                stmt->expr = nullnode();
            }
        }

        if (ENODE_IS(stmt->expr, ECOMMA)) {
            Statement* newStmt = lalloc(sizeof(Statement));
            *newStmt = *stmt;

            stmt->next = newStmt;
            stmt->type = ST_EXPRESSION;
            stmt->expr = stmt->expr->data.diadic.left;
            newStmt->expr = newStmt->expr->data.diadic.right;

            changed = TRUE;
        }
    } while (changed);

    if (((expr = stmt->expr)->type == EFUNCCALL || expr->type == EFUNCCALLP) &&
        ENODE_IS(expr->data.funccall.funcref, EOBJREF) &&
        CInline_InlineFunctionCheck(expr->data.funccall.funcref)) {
        stmt = CInline_InlineFunctionStatement(stmt, &changed);
        if (changed) {
            any_inline_expanded = TRUE;
            return stmt;
        }
    }

    inline_expanded = FALSE;
    cinline_unconditionalpart = TRUE;
    cinline_serialize_stmt = FALSE;
    cinline_stmtlevelexprs = 0;
    stmt->expr = CInline_ExpandExpression(stmt->expr);

    if (cinline_serialize_stmt) {
        cinline_unconditionalpart = TRUE;
        cinline_serialize_stmt = FALSE;
        cinline_stmtlevelexprs = 0;
        CInline_SerializeStatement(stmt);
        stmt->expr = CInline_ExpandExpression(stmt->expr);
    }

    if (inline_expanded) {
        stmt->expr = CInline_FoldConst(stmt->expr);
        any_inline_expanded = TRUE;
    }

    if (cinline_stmtlevelexprs) {
        stmt = CInline_ExtractInlineFunction(stmt);
        any_inline_expanded = TRUE;
    }

    return stmt;
}

static void CInline_ForceReverseSearch(ENode* expr) {
    cinline_funccallfound = TRUE;
}

static ENode* CInline_ForceReverseEvaluation(ENode* expr) {
    ENode* commanodes;
    ENodeList* list;
    int counter;
    ENode* ass;
    ENode* inner;
    ENode* copy;

    list = expr->data.funccall.args;
    counter = 0;
    commanodes = NULL;

    while (list) {
        cinline_funccallfound = FALSE;
        inner = list->node;
        CExpr_SearchExprTree(inner, CInline_ForceReverseSearch, 2, EFUNCCALL, EFUNCCALLP);

        if (cinline_funccallfound && ++counter > 0) {
            inner = create_objectrefnode(create_temp_object(inner->rtype));
            copy = lalloc(sizeof(ENode));
            *copy = *inner;

            copy = makemonadicnode(copy, EINDIRECT);
            copy->rtype = TPTR_TARGET(copy->rtype);

            inner = makemonadicnode(inner, EINDIRECT);
            inner->rtype = TPTR_TARGET(inner->rtype);

            ass = makediadicnode(inner, copy, EASS);
            list->node = copy;

            if (commanodes) {
                commanodes = makediadicnode(ass, commanodes, ECOMMA);
            } else {
                commanodes = ass;
            }
        }

        list = list->next;
    }

    if (commanodes) {
        commanodes = makediadicnode(commanodes, expr, ECOMMA);
        commanodes->rtype = expr->rtype;
        return commanodes;
    }

    return expr;
}

static void CInline_ExportCheck(ENode* expr) {
    while (1) {
        switch (expr->type) {
            case EOBJREF:
                CInline_ObjectAddrRef(expr->data.objref);
                if (expr->data.objref->datatype == DALIAS) {
                    CExpr_AliasTransform(expr);
                    continue;
                }
                return;

            case EPOSTINC:
            case EPOSTDEC:
            case EPREINC:
            case EPREDEC:
            case EINDIRECT:
            case EMONMIN:
            case EBINNOT:
            case ELOGNOT:
            case EFORCELOAD:
            case ETYPCON:
            case EBITFIELD:
                expr = expr->data.monadic;
                continue;

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
                CInline_ExportCheck(expr->data.diadic.left);
                expr = expr->data.diadic.right;
                continue;

            case EINTCONST:
            case EFLOATCONST:
            case ESTRINGCONST:
            case EPRECOMP:
            case EINSTRUCTION:
            case EVECTOR128CONST:
                return;

            case ELABEL:
                if (expr->data.label->stmt) {
                    expr->data.label->stmt->flags |= StmtFlag_KeepLabel;
                }
                return;

            case EFUNCCALL:
            case EFUNCCALLP: {
                ENodeList* list;
                TypeClass* tclass;
                SInt32 index;

                for (list = expr->data.funccall.args; list; list = list->next) {
                    CInline_ExportCheck(list->node);
                }

                expr = expr->data.funccall.funcref;
                if (copts.warn_notinlined &&
                    !copts.dontinline &&
                    ENODE_IS(expr, EOBJREF) &&
                    (expr->data.objref->qual & Q_INLINE) &&
                    expr->data.objref->datatype != DINLINEFUNC &&
                    !CParser_IsVirtualFunction(expr->data.objref, &tclass, &index)) {
                    CError_Warning(ERR_INLINE_FUNCTION_CALL_NOT_INLINED, expr->data.objref);
                }

                continue;
            }

            case ENULLCHECK:
                CInline_ExportCheck(expr->data.nullcheck.nullcheckexpr);
                expr = expr->data.nullcheck.condexpr;
                continue;

            case EMFPOINTER:
                *expr = *nullnode();
                continue;

            case ECOND:
                CInline_ExportCheck(expr->data.cond.cond);
                CInline_ExportCheck(expr->data.cond.expr1);
                expr = expr->data.cond.expr2;
                continue;

            case EMEMBER:
                if (expr->data.emember->expr) {
                    *expr = *expr->data.emember->expr;
                    continue;
                }
            case EOBJLIST:
                *expr = *nullnode();
                continue;

            default:
                CError_FATAL(3372);
        }
    }
}

static void CInline_Expand(Statement* stmt) {
    Statement* scan;

    if (!copts.dontinline && copts.inlinelevel >= 0) {
        if (copts.inline_bottom_up) {
            inline_max_size = copts.inlinemaxsize;
            while (inline_max_size > 1 &&
                   EstimateExpandedSizeOfFunction(stmt) > copts.inlinemaxtotalsize) {
                inline_max_size >>= 1;
            }
        }

        cinline_level = 0;
        while (1) {
            any_inline_expanded = FALSE;
            for (scan = stmt; scan; scan = scan->next) {
                switch (scan->type) {
                    case ST_NOP:
                    case ST_LABEL:
                    case ST_GOTO:
                    case ST_BEGINCATCH:
                    case ST_ENDCATCH:
                    case ST_ENDCATCHDTOR:
                    case ST_ASM:
                        break;
                    case ST_RETURN:
                        if (!scan->expr) {
                            break;
                        }
                    case ST_EXPRESSION:
                    case ST_SWITCH:
                    case ST_IFGOTO:
                    case ST_IFNGOTO:
                    case ST_GOTOEXPR:
                        scan = CInline_ExpandStatement(scan);
                        break;
                    default:
                        CError_FATAL(3438);
                }
            }

            if (!copts.inline_bottom_up && !any_inline_expanded) {
                break;
            }

            if (!copts.alwaysinline || copts.inline_bottom_up) {
                if (copts.inlinelevel == 0) {
                    if (copts.inline_bottom_up) {
                        if ((cinline_level + 1) >= 8) {
                            break;
                        }
                    } else {
                        if (cinline_level >= 3) {
                            break;
                        }
                    }
                } else {
                    if ((cinline_level + 1) >= copts.inlinelevel) {
                        break;
                    }
                }
            }

            if (CWDisplayLines(cparamblkptr->context, lines) != cwNoErr) {
                CError_UserBreak();
            }

            cinline_level++;
        }
    }

    for (scan = stmt; scan; scan = scan->next) {
        if (scan->dobjstack) {
            CExcept_CheckStackRefs(scan->dobjstack);
        }

        switch (scan->type) {
            case ST_NOP:
            case ST_LABEL:
            case ST_GOTO:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
            case ST_ASM:
                break;
            case ST_RETURN:
                if (!scan->expr) {
                    break;
                }
            case ST_EXPRESSION:
            case ST_SWITCH:
            case ST_IFGOTO:
            case ST_IFNGOTO:
            case ST_GOTOEXPR:
                CInline_ExportCheck(scan->expr);
                break;
            default:
                CError_FATAL(3501);
        }
    }
}

SInt16 CInline_GetStatementNumber(Statement* first, Statement* stmt) {
    SInt16 number = 0;

    while (first) {
        if (first == stmt) {
            return number;
        }

        first = first->next;
        number++;
    }

    CError_FATAL(3517);
    return 0;
}

static CI_Switch* CInline_PackSwitch(Statement* start, Statement* stmt) {
    SwitchInfo* info;
    SwitchCase* swcase;
    short numcases;
    CI_Switch* packed;

    info = (SwitchInfo*)stmt->label;
    swcase = info->cases;
    numcases = 0;
    while (swcase) {
        numcases++;
        swcase = swcase->next;
    }

    packed = galloc(sizeof(CI_Switch) - sizeof(CI_SwitchCase) + numcases * sizeof(CI_SwitchCase));
    packed->expr = CInline_CopyExpression(stmt->expr, CopyModePack);
    packed->defaultlabelID = CInline_GetStatementNumber(start, info->defaultlabel->stmt);
    packed->exprtype = info->exprtype;
    packed->numcases = numcases;

    for (swcase = info->cases, numcases = 0; swcase; swcase = swcase->next, numcases++) {
        packed->cases[numcases].labelID = CInline_GetStatementNumber(start, swcase->label->stmt);
        packed->cases[numcases].min = swcase->min;
#if VERSION < VERSION_GC_2_0
        packed->cases[numcases].max = swcase->max;
#endif
    }

    return packed;
}

static UInt8 CInline_CanInline(Object* object, Statement* stmt) {
    UInt8 resultClass;
    FuncArg* arg;
    UInt8 result;

    resultClass = CMach_GetFunctionResultClass(TYPE_FUNC(object->type));
    if (resultClass &&
        (resultClass != 1 ||
         (IS_TYPE_CLASS(TYPE_FUNC(object->type)->functype) &&
          CClass_Destructor(TYPE_CLASS(TYPE_FUNC(object->type)->functype))))) {
        return CI_CanInlineNone;
    }

    for (arg = TYPE_FUNC(object->type)->args; arg; arg = arg->next) {
        if (arg == &elipsis) {
            return CI_CanInlineNone;
        }
        if (arg == &oldstyle) {
            break;
        }

        if (IS_TYPE_CLASS(arg->type) && CClass_Destructor(TYPE_CLASS(arg->type))) {
            return CI_CanInlineNone;
        }
    }

    result = CI_CanInlineExpression;

    while (stmt) {
        if (stmt->dobjstack) {
            return CI_CanInlineStatement;
        }

        switch (stmt->type) {
            case ST_EXPRESSION:
                break;
            case ST_RETURN:
                if (stmt->next ||
                    (stmt->expr == NULL && TYPE_FUNC(object->type)->functype != &stvoid)) {
                    result = CI_CanInlineStatement;
                }
                break;
            default:
                result = CI_CanInlineStatement;
        }

        stmt = stmt->next;
    }

    return result;
}

static ExceptionAction* CInline_PackActions(Statement* start, Statement* stmt) {
    ExceptionAction* exc;
    ExceptionAction* last;
    ExceptionAction* packexc;

    exc = stmt->dobjstack;
    last = NULL;

    while (exc) {
        packexc = galloc(sizeof(ExceptionAction));
        packexc->prev = last;
        last = packexc;

        packexc->type = exc->type;

        switch (exc->type) {
            case EAT_DESTROYLOCAL:
                packexc->data.destroy_local.local =
                    (void*)CInline_GetLocalID(exc->data.destroy_local.local);
                packexc->data.destroy_local.dtor = exc->data.destroy_local.dtor;
                break;
            case EAT_DESTROYLOCALCOND:
                packexc->data.destroy_local_cond.local =
                    (void*)CInline_GetLocalID(exc->data.destroy_local_cond.local);
                packexc->data.destroy_local_cond.dtor = exc->data.destroy_local_cond.dtor;
                packexc->data.destroy_local_cond.cond =
                    (void*)CInline_GetLocalID(exc->data.destroy_local_cond.cond);
                break;
            case EAT_DESTROYLOCALOFFSET:
                packexc->data.destroy_local_offset.local =
                    (void*)CInline_GetLocalID(exc->data.destroy_local_offset.local);
                packexc->data.destroy_local_offset.dtor = exc->data.destroy_local_offset.dtor;
                packexc->data.destroy_local_offset.offset = exc->data.destroy_local_offset.offset;
                break;
            case EAT_DESTROYLOCALPOINTER:
                packexc->data.destroy_local_pointer.pointer =
                    (void*)CInline_GetLocalID(exc->data.destroy_local_pointer.pointer);
                packexc->data.destroy_local_pointer.dtor = exc->data.destroy_local_pointer.dtor;
                break;
            case EAT_DESTROYLOCALARRAY:
                packexc->data.destroy_local_array.localarray =
                    (void*)CInline_GetLocalID(exc->data.destroy_local_array.localarray);
                packexc->data.destroy_local_array.dtor = exc->data.destroy_local_array.dtor;
                packexc->data.destroy_local_array.elements = exc->data.destroy_local_array.elements;
                packexc->data.destroy_local_array.element_size =
                    exc->data.destroy_local_array.element_size;
                break;
            case EAT_DESTROYPARTIALARRAY:
                packexc->data.destroy_partial_array.arraypointer =
                    (void*)CInline_GetLocalID(exc->data.destroy_partial_array.arraypointer);
                packexc->data.destroy_partial_array.arraycounter =
                    (void*)CInline_GetLocalID(exc->data.destroy_partial_array.arraycounter);
                packexc->data.destroy_partial_array.dtor =
                    (void*)CInline_GetLocalID(exc->data.destroy_partial_array.dtor);
                packexc->data.destroy_partial_array.element_size =
                    (void*)CInline_GetLocalID(exc->data.destroy_partial_array.element_size);
                break;
            case EAT_DESTROYMEMBER:
            case EAT_DESTROYBASE:
                packexc->data.destroy_member.objectptr =
                    (void*)CInline_GetLocalID(exc->data.destroy_member.objectptr);
                packexc->data.destroy_member.dtor = exc->data.destroy_member.dtor;
                packexc->data.destroy_member.offset = exc->data.destroy_member.offset;
                break;
            case EAT_DESTROYMEMBERCOND:
                packexc->data.destroy_member_cond.objectptr =
                    (void*)CInline_GetLocalID(exc->data.destroy_member_cond.objectptr);
                packexc->data.destroy_member_cond.cond =
                    (void*)CInline_GetLocalID(exc->data.destroy_member_cond.cond);
                packexc->data.destroy_member_cond.dtor = exc->data.destroy_member_cond.dtor;
                packexc->data.destroy_member_cond.offset = exc->data.destroy_member_cond.offset;
                break;
            case EAT_DESTROYMEMBERARRAY:
                packexc->data.destroy_member_array.objectptr =
                    (void*)CInline_GetLocalID(exc->data.destroy_member_array.objectptr);
                packexc->data.destroy_member_array.dtor = exc->data.destroy_member_array.dtor;
                packexc->data.destroy_member_array.offset = exc->data.destroy_member_array.offset;
                packexc->data.destroy_member_array.elements =
                    exc->data.destroy_member_array.elements;
                packexc->data.destroy_member_array.element_size =
                    exc->data.destroy_member_array.element_size;
                break;
            case EAT_DELETEPOINTER:
            case EAT_DELETELOCALPOINTER:
                packexc->data.delete_pointer.pointerobject =
                    (void*)CInline_GetLocalID(exc->data.delete_pointer.pointerobject);
                packexc->data.delete_pointer.deletefunc = exc->data.delete_pointer.deletefunc;
                break;
            case EAT_DELETEPOINTERCOND:
                packexc->data.delete_pointer_cond.pointerobject =
                    (void*)CInline_GetLocalID(exc->data.delete_pointer_cond.pointerobject);
                packexc->data.delete_pointer_cond.deletefunc =
                    exc->data.delete_pointer_cond.deletefunc;
                packexc->data.delete_pointer_cond.cond =
                    (void*)CInline_GetLocalID(exc->data.delete_pointer_cond.cond);
                break;
            case EAT_CATCHBLOCK:
                packexc->data.catch_block.catch_object =
                    (void*)CInline_GetLocalID(exc->data.catch_block.catch_object);
                packexc->data.catch_block.catch_info_object =
                    (void*)CInline_GetLocalID(exc->data.catch_block.catch_info_object);
                packexc->data.catch_block.catch_label = (void*)(SInt32)CInline_GetStatementNumber(
                    start->next, exc->data.catch_block.catch_label->stmt
                );
                packexc->data.catch_block.catch_typeid = exc->data.catch_block.catch_typeid;
                packexc->data.catch_block.catch_type = exc->data.catch_block.catch_type;
                packexc->data.catch_block.catch_qual = exc->data.catch_block.catch_qual;
                break;
            case EAT_ACTIVECATCHBLOCK:
                packexc->data.active_catch_block.catch_info_object =
                    (void*)CInline_GetLocalID(exc->data.active_catch_block.catch_info_object);
                packexc->data.active_catch_block.call_dtor = exc->data.active_catch_block.call_dtor;
                break;
            case EAT_SPECIFICATION:
                packexc->data.specification.unexp_ids = exc->data.specification.unexp_ids;
                packexc->data.specification.unexp_id = exc->data.specification.unexp_id;
                packexc->data.specification.unexp_label = (void*)(SInt32)CInline_GetStatementNumber(
                    start->next, exc->data.specification.unexp_label->stmt
                );
                packexc->data.specification.unexp_info_object =
                    (void*)CInline_GetLocalID(exc->data.specification.unexp_info_object);
                break;
            case EAT_TERMINATE:
                break;
            default:
                CError_FATAL(3720);
        }

        exc = exc->prev;
    }

    return last;
}

void CInline_PackIFunctionData(CI_FuncData* funcdata, Statement* stmt, Object* object) {
    ObjectList* list;
    CI_Var* var;
    Statement* scan;
    CI_Statement* packstmt;
    int i;

    cinline_first_stmt = stmt->next;
    memclrw(funcdata, sizeof(CI_FuncData));
    CMach_GetBackEndOptions(&funcdata->options);

    funcdata->can_inline = CInline_CanInline(object, stmt->next);

    if (copts.filesyminfo) {
        funcdata->fileoffset = cparser_fileoffset;
        funcdata->fileoffset.is_inline = TRUE;
        funcdata->symdecloffset = symdecloffset;
        funcdata->functionbodyoffset = functionbodyoffset;
        funcdata->functionbodypath = functionbodypath;
        funcdata->symdeclend = symdeclend;
    }

    list = arguments;
    i = 0;
    while (list) {
        i++;
        list = list->next;
    }

    if ((funcdata->numarguments = i) > 0) {
        loc_args = funcdata->arguments = galloc(sizeof(CI_Var) * i);
        memclrw(funcdata->arguments, sizeof(CI_Var) * i);

        for (list = arguments, var = funcdata->arguments; list; list = list->next, var++) {
            var->name = list->object->name;
            var->type = list->object->type;
            var->qual = list->object->qual;
            var->sflags = CInline_GetObjectSFlags(list->object);
            var->used = FALSE;
            var->unmodified = TRUE;
        }
    }

    list = locals;
    i = 0;
    while (list) {
        if (list->object->datatype == DLOCAL) {
            i++;
        }
        list = list->next;
    }

    if ((funcdata->numlocals = i) > 0) {
        loc_vars = funcdata->locals = galloc(sizeof(CI_Var) * i);
        memclrw(funcdata->locals, sizeof(CI_Var) * i);

        for (list = locals, var = funcdata->locals; list; list = list->next) {
            if (list->object->datatype == DLOCAL) {
                var->name = list->object->name;
                var->type = list->object->type;
                var->qual = list->object->qual;
                var->sflags = CInline_GetObjectSFlags(list->object);
                var->used = FALSE;
                var->unmodified = FALSE;
                var++;
            }
        }
    }

    scan = stmt->next;
    i = 0;
    while (scan) {
        i++;
        scan = scan->next;
    }

    funcdata->numstatements = i;
    funcdata->statements = galloc(sizeof(CI_Statement) * i);

    for (scan = stmt->next, packstmt = funcdata->statements; scan; scan = scan->next, packstmt++) {
        packstmt->type = scan->type;
        packstmt->flags = scan->flags;
        packstmt->value = scan->value;
        packstmt->dobjstack = CInline_PackActions(stmt, scan);
        packstmt->sourceoffset = scan->sourceoffset;
        packstmt->sourcefilepath = scan->sourcefilepath;

        switch (scan->type) {
            case ST_NOP:
            case ST_LABEL:
                break;
            case ST_EXPRESSION:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
            case ST_GOTOEXPR:
                packstmt->u.expr = CInline_CopyExpression(scan->expr, CopyModePack);
                break;
            case ST_RETURN:
                if (scan->expr) {
                    packstmt->u.expr = CInline_CopyExpression(scan->expr, CopyModePack);
                } else {
                    packstmt->u.expr = NULL;
                }
                break;
            case ST_GOTO:
                packstmt->u.statementnum =
                    CInline_GetStatementNumber(stmt->next, scan->label->stmt);
                break;
            case ST_IFGOTO:
            case ST_IFNGOTO:
                packstmt->u.ifgoto.expr = CInline_CopyExpression(scan->expr, CopyModePack);
                packstmt->u.ifgoto.statementnum =
                    CInline_GetStatementNumber(stmt->next, scan->label->stmt);
                break;
            case ST_SWITCH:
                packstmt->u.switchdata = CInline_PackSwitch(stmt->next, scan);
                break;
            case ST_ASM:
                InlineAsm_PackAsmStatement(
                    scan, stmt->next, &packstmt->u.asmdata.data, &packstmt->u.asmdata.size
                );
                break;
            default:
                CError_FATAL(3862);
        }
    }
}

void CInline_UnpackIFunctionData(Object* object, CI_FuncData* funcdata, Statement* firstStmt) {
    Object* obj;
    CLabel** labels;
    ObjectList* last;
    Statement* stmt;
    CI_Statement* packstmt;
    CI_Var* var;
    int i;

    cparser_fileoffset = funcdata->fileoffset;
    symdecloffset = funcdata->symdecloffset;
    functionbodyoffset = funcdata->functionbodyoffset;
    functionbodypath = funcdata->functionbodypath;
    symdeclend = funcdata->symdeclend;

    for (i = 0, var = funcdata->arguments; i < funcdata->numarguments; i++, var++) {
        if (i == 0) {
            last = lalloc(sizeof(ObjectList));
            arguments = last;
        } else {
            last->next = lalloc(sizeof(ObjectList));
            last = last->next;
        }

        obj = galloc(sizeof(Object));
        memclrw(obj, sizeof(Object));
        last->object = obj;
        last->next = NULL;

        obj->otype = OT_OBJECT;
        obj->access = ACCESSPUBLIC;
        obj->datatype = DLOCAL;
        obj->name = var->name;
        obj->type = var->type;
        obj->qual = var->qual;
        CInline_SetObjectSFlags(obj, var->sflags);
        CFunc_SetupLocalVarInfo(obj);

        if (funcdata->fileoffset.file) {
            obj->u.var.info->deftoken.tokenfile = funcdata->fileoffset.file;
            obj->u.var.info->deftoken.tokenoffset = funcdata->functionbodyoffset;
        }
    }

    for (i = 0, var = funcdata->locals; i < funcdata->numlocals; i++, var++) {
        if (i == 0) {
            last = lalloc(sizeof(ObjectList));
            locals = last;
        } else {
            last->next = lalloc(sizeof(ObjectList));
            last = last->next;
        }

        obj = galloc(sizeof(Object));
        memclrw(obj, sizeof(Object));
        last->object = obj;
        last->next = NULL;

        obj->otype = OT_OBJECT;
        obj->access = ACCESSPUBLIC;
        obj->datatype = DLOCAL;
        obj->name = var->name;
        obj->type = var->type;
        obj->qual = var->qual;
        CInline_SetObjectSFlags(obj, var->sflags);
        CFunc_SetupLocalVarInfo(obj);

        if (funcdata->fileoffset.file) {
            obj->u.var.info->deftoken.tokenfile = funcdata->fileoffset.file;
            obj->u.var.info->deftoken.tokenoffset = funcdata->functionbodyoffset;
        }
    }

    enode_idtrans = NULL;
    cinline_label_trans = NULL;
    labels = lalloc(sizeof(CLabel*) * funcdata->numstatements);
    memclrw(labels, sizeof(CLabel*) * funcdata->numstatements);

    for (i = 0, stmt = firstStmt, packstmt = funcdata->statements; i < funcdata->numstatements;
         i++, packstmt++) {
        stmt->next = lalloc(sizeof(Statement));
        stmt = stmt->next;

        stmt->type = packstmt->type;
        stmt->value = packstmt->value;
        stmt->flags = packstmt->flags;
        stmt->sourceoffset = packstmt->sourceoffset;
        stmt->sourcefilepath = packstmt->sourcefilepath;
        stmt->dobjstack = CInline_UnpackActions(packstmt, FALSE);
        stmt->next = NULL;

        switch (stmt->type) {
            case ST_NOP:
            case ST_GOTO:
            case ST_ASM:
                break;
            case ST_EXPRESSION:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
            case ST_GOTOEXPR:
                stmt->expr = CInline_CopyExpression(packstmt->u.expr, CopyModeUnpack);
                break;
            case ST_RETURN:
                if (packstmt->u.expr) {
                    stmt->expr = CInline_CopyExpression(packstmt->u.expr, CopyModeUnpack);
                } else {
                    stmt->expr = NULL;
                }
                break;
            case ST_LABEL:
                labels[i] = stmt->label = newlabel();
                stmt->label->stmt = stmt;
                break;
            case ST_IFGOTO:
            case ST_IFNGOTO:
                stmt->expr = CInline_CopyExpression(packstmt->u.ifgoto.expr, CopyModeUnpack);
                break;
            case ST_SWITCH:
                stmt->expr = CInline_CopyExpression(packstmt->u.switchdata->expr, CopyModeUnpack);
                break;
            default:
                CError_FATAL(4017);
        }
    }

    for (stmt = firstStmt->next, packstmt = funcdata->statements; stmt;
         stmt = stmt->next, packstmt++) {
        switch (stmt->type) {
            case ST_GOTO:
                CError_ASSERT(4024, stmt->label = labels[packstmt->u.statementnum]);
                break;
            case ST_IFGOTO:
            case ST_IFNGOTO:
                CError_ASSERT(4029, stmt->label = labels[packstmt->u.ifgoto.statementnum]);
                break;
            case ST_SWITCH:
                CInline_UnpackSwitch(stmt, packstmt, labels);
                break;
            case ST_ASM:
                InlineAsm_UnpackAsmStatement(
                    stmt, labels, FALSE, packstmt->u.asmdata.data, packstmt->u.asmdata.size
                );
                break;
            default:
                break;
        }
    }

    cinline_first_stmt = firstStmt->next;

    while (cinline_label_trans) {
        CError_ASSERT(4045, *cinline_label_trans->labelptr = labels[cinline_label_trans->id]);
        cinline_label_trans = cinline_label_trans->next;
    }
}

static void CInline_GenIFunctionCode(Object* object, CI_FuncData* func, UInt8 oldStyleParameters) {
    Boolean saveDebugInfo;
    Statement firstStmt;
    CScopeSave saveScope;

    if (cparamblkptr->precompile == 1) {
        return;
    }
    if (!func) {
        return;
    }

    ObjGen_SetupSym();
    CScope_SetFunctionScope(object, &saveScope);
    CFunc_FuncGenSetup(&firstStmt, object);
    CInline_UnpackIFunctionData(object, func, &firstStmt);

    saveDebugInfo = copts.filesyminfo;
    if (copts.nosyminline || (!symdecloffset && !symdeclend)) {
        copts.filesyminfo = FALSE;
    }

    expanding_function = object;
    recursive_inline = FALSE;
    CInline_Expand(&firstStmt);

    if (!anyerrors) {
        if (copts.filesyminfo) {
            CPrep_SetSourceFile(&cparser_fileoffset);
        }
        CodeGen_Generator(&firstStmt, object, oldStyleParameters, FALSE);
    }

    CScope_RestoreScope(&saveScope);
    copts.filesyminfo = saveDebugInfo;
}

void CInline_AddDefaultFunctionAction(Object* object) {
    CI_Action* action;

    for (action = cinline_actionlist; action; action = action->next) {
        if (action->obj == object) {
            return;
        }
    }

    action = galloc(sizeof(CI_Action));
    memclrw(action, sizeof(CI_Action));

    action->actiontype = CI_ActionDefaultFunc;
    action->obj = object;

    action->next = cinline_actionlist;
    cinline_actionlist = action;
}

void CInline_AddInlineFunctionAction(
    Object* object,
    TypeClass* tclass,
    FileOffsetInfo* fileoffset,
    TokenStream* stream,
    Boolean isTemplate
) {
    CI_Action* action;

    for (action = isTemplate ? cinline_tactionlist : cinline_actionlist; action;
         action = action->next) {
        if (action->obj == object) {
            return;
        }
    }

    CError_ASSERT(4132, IS_TYPE_FUNC(object->type));

    TYPE_FUNC(object->type)->flags |= FUNC_IS_TEMPL_INSTANCE;

    action = galloc(sizeof(CI_Action));
    memclrw(action, sizeof(CI_Action));

    action->actiontype = CI_ActionInlineFunc;
    action->obj = object;
    action->u.inlinefunc.tclass = tclass;
    action->u.inlinefunc.fileoffset = *fileoffset;
    action->u.inlinefunc.stream = *stream;

    if (isTemplate) {
        action->next = cinline_tactionlist;
        cinline_tactionlist = action;
        TYPE_FUNC(object->type)->flags |= FUNC_TEMPLATE_ACTION;
    } else {
        action->next = cinline_actionlist;
        cinline_actionlist = action;
    }
}

void CInline_AddMemberFunctionAction(
    Object* object, TemplClass* templ, TemplClassInst* inst, TemplateMember* tmemb
) {
    CI_Action* action;

    for (action = cinline_tactionlist; action; action = action->next) {
        if (action->obj == object) {
            return;
        }
    }

    action = galloc(sizeof(CI_Action));
    memclrw(action, sizeof(CI_Action));

    action->actiontype = CI_ActionMemberFunc;
    action->obj = object;
    action->u.memberfunc.templ = templ;
    action->u.memberfunc.inst = inst;
    action->u.memberfunc.tmemb = tmemb;

    action->next = cinline_tactionlist;
    cinline_tactionlist = action;

    TYPE_FUNC(object->type)->flags |= FUNC_TEMPLATE_ACTION;
}

void CInline_AddTemplateFunctionAction(Object* object, void* func, void* inst) {
    CI_Action* action;

    for (action = cinline_tactionlist; action; action = action->next) {
        if (action->obj == object) {
            return;
        }
    }

    action = galloc(sizeof(CI_Action));
    memclrw(action, sizeof(CI_Action));

    action->actiontype = CI_ActionTemplateFunc;
    action->obj = object;
    action->u.templatefunc.func = func;
    action->u.templatefunc.inst = inst;

    action->next = cinline_tactionlist;
    cinline_tactionlist = action;

    TYPE_FUNC(object->type)->flags |= FUNC_TEMPLATE_ACTION;
}

static void CInline_AddFRefList_Object(Object* object) {
    ObjectList* list;

    if (!(object->datatype == DFUNC || object->datatype == DVFUNC) ||
        (object->flags & OBJECT_DEFINED) ||
        IS_TEMPL_FUNC(object->type)) {
        return;
    }

    for (list = cinline_freflist; list; list = list->next) {
        if (list->object == object) {
            return;
        }
    }

    list = lalloc(sizeof(ObjectList));
    list->object = object;
    list->next = cinline_freflist;
    cinline_freflist = list;

    if ((object->qual & Q_INLINE) && object->u.func.u.ifuncdata) {
        CInline_AddFRefList_InlineFunc(object->u.func.u.ifuncdata);
    }
}

static void CInline_AddFRefList_ExAction(ExceptionAction* exc) {
    while (exc) {
        switch (exc->type) {
            case EAT_DESTROYLOCAL:
                CInline_AddFRefList_Object(exc->data.destroy_local.dtor);
                break;
            case EAT_DESTROYLOCALCOND:
                CInline_AddFRefList_Object(exc->data.destroy_local_cond.dtor);
                break;
            case EAT_DESTROYLOCALOFFSET:
                CInline_AddFRefList_Object(exc->data.destroy_local_offset.dtor);
                break;
            case EAT_DESTROYLOCALPOINTER:
                CInline_AddFRefList_Object(exc->data.destroy_local_pointer.dtor);
                break;
            case EAT_DESTROYLOCALARRAY:
                CInline_AddFRefList_Object(exc->data.destroy_local_array.dtor);
                break;
            case EAT_DESTROYPARTIALARRAY:
                CInline_AddFRefList_Object(exc->data.destroy_partial_array.dtor);
                break;
            case EAT_DESTROYMEMBER:
            case EAT_DESTROYBASE:
                CInline_AddFRefList_Object(exc->data.destroy_member.dtor);
                break;
            case EAT_DESTROYMEMBERCOND:
                CInline_AddFRefList_Object(exc->data.destroy_member_cond.dtor);
                break;
            case EAT_DESTROYMEMBERARRAY:
                CInline_AddFRefList_Object(exc->data.destroy_member_array.dtor);
                break;
            case EAT_DELETEPOINTER:
            case EAT_DELETELOCALPOINTER:
                CInline_AddFRefList_Object(exc->data.delete_pointer.deletefunc);
                break;
            case EAT_DELETEPOINTERCOND:
                CInline_AddFRefList_Object(exc->data.delete_pointer_cond.deletefunc);
                break;
            case EAT_CATCHBLOCK:
            case EAT_ACTIVECATCHBLOCK:
            case EAT_SPECIFICATION:
            case EAT_TERMINATE:
                break;
            default:
                CError_FATAL(4307);
        }
        exc = exc->prev;
    }
}

static void CInline_AddFRefList_ExprCB(ENode* expr) {
    CInline_AddFRefList_Object(expr->data.objref);
}

static void CInline_AddFRefList_Expr(ENode* expr) {
    CExpr_SearchExprTree(expr, CInline_AddFRefList_ExprCB, 1, EOBJREF);
}

static void CInline_AddFRefList_Statement(Statement* stmt) {
    while (stmt) {
        if (stmt->dobjstack) {
            CInline_AddFRefList_ExAction(stmt->dobjstack);
        }

        switch (stmt->type) {
            case ST_NOP:
            case ST_LABEL:
            case ST_GOTO:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
            case ST_ASM:
                break;
            case ST_RETURN:
                if (!stmt->expr) {
                    break;
                }
            case ST_EXPRESSION:
            case ST_SWITCH:
            case ST_IFGOTO:
            case ST_IFNGOTO:
            case ST_GOTOEXPR:
                CInline_AddFRefList_Expr(stmt->expr);
                break;
            default:
                CError_FATAL(4368);
        }

        stmt = stmt->next;
    }
}

static void CInline_AddFRefList_InlineFunc(CI_FuncData* data) {
    short i;
    ExceptionAction* exc;

    for (i = 0; i < data->numstatements; i++) {
        switch (data->statements[i].type) {
            case ST_NOP:
            case ST_LABEL:
            case ST_GOTO:
            case ST_ASM:
                break;
            case ST_EXPRESSION:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
            case ST_GOTOEXPR:
                CInline_AddFRefList_Expr(data->statements[i].u.expr);
                break;
            case ST_RETURN:
                if (data->statements[i].u.expr) {
                    CInline_AddFRefList_Expr(data->statements[i].u.expr);
                }
                break;
            case ST_IFGOTO:
            case ST_IFNGOTO:
                CInline_AddFRefList_Expr(data->statements[i].u.ifgoto.expr);
                break;
            case ST_SWITCH:
                CInline_AddFRefList_Expr(data->statements[i].u.switchdata->expr);
                break;
            default:
                CError_FATAL(4420);
        }

        for (exc = data->statements[i].dobjstack; exc; exc = exc->prev) {
            switch (exc->type) {
                case EAT_DESTROYLOCAL:
                    CInline_AddFRefList_Object(exc->data.destroy_local.dtor);
                    break;
                case EAT_DESTROYLOCALCOND:
                    CInline_AddFRefList_Object(exc->data.destroy_local_cond.dtor);
                    break;
                case EAT_DESTROYLOCALOFFSET:
                    CInline_AddFRefList_Object(exc->data.destroy_local_offset.dtor);
                    break;
                case EAT_DESTROYLOCALPOINTER:
                    CInline_AddFRefList_Object(exc->data.destroy_local_pointer.dtor);
                    break;
                case EAT_DESTROYLOCALARRAY:
                    CInline_AddFRefList_Object(exc->data.destroy_local_array.dtor);
                    break;
                case EAT_DESTROYPARTIALARRAY:
                    break;
                case EAT_DESTROYMEMBER:
                case EAT_DESTROYBASE:
                    CInline_AddFRefList_Object(exc->data.destroy_member.dtor);
                    break;
                case EAT_DESTROYMEMBERCOND:
                    CInline_AddFRefList_Object(exc->data.destroy_member_cond.dtor);
                    break;
                case EAT_DESTROYMEMBERARRAY:
                    CInline_AddFRefList_Object(exc->data.destroy_member_array.dtor);
                    break;
                case EAT_DELETEPOINTER:
                case EAT_DELETELOCALPOINTER:
                    CInline_AddFRefList_Object(exc->data.delete_pointer.deletefunc);
                    break;
                case EAT_DELETEPOINTERCOND:
                    CInline_AddFRefList_Object(exc->data.delete_pointer_cond.deletefunc);
                    break;
                case EAT_CATCHBLOCK:
                case EAT_ACTIVECATCHBLOCK:
                case EAT_SPECIFICATION:
                case EAT_TERMINATE:
                    break;
                default:
                    CError_FATAL(4470);
            }
        }
    }
}

static void CInline_GenerateTemplateInline(Object* object) {
    CI_Action** ptr;
    CI_Action* action;

    ptr = &cinline_tactionlist;
    while ((action = *ptr)) {
        if (object == action->obj) {
            *ptr = action->next;
            action->next = cinline_actionlist;
            cinline_actionlist = action;

            TYPE_FUNC(object->type)->flags &= ~FUNC_TEMPLATE_ACTION;
            return;
        }

        ptr = &action->next;
    }

    CError_FATAL(4499);
}

void CInline_ObjectAddrRef(Object* object) {
    CI_FuncData* funcdata;

    object->flags |= OBJECT_REFERENCED;

    switch (object->datatype) {
        case DFUNC:
        case DVFUNC:
            if ((object->qual & Q_INLINE) &&
                (funcdata = object->u.func.u.ifuncdata) &&
                !(object->flags & OBJECT_DEFINED) &&
                !(TYPE_FUNC(object->type)->flags & FUNC_IS_TEMPL)) {
                CI_Export* export = galloc(sizeof(CI_Export));

                export->object = object;
                export->funcdata = funcdata;
                export->oldStyleParameters = FALSE;

                export->next = cinline_exportlist;
                cinline_exportlist = export;

                object->flags |= OBJECT_DEFINED;
                return;
            } else if ((TYPE_FUNC(object->type)->flags & FUNC_AUTO_GENERATED) &&
                       !(TYPE_FUNC(object->type)->flags & FUNC_DEFINED)) {
                CInline_AddDefaultFunctionAction(object);
                return;
            } else if (TYPE_FUNC(object->type)->flags & FUNC_TEMPLATE_ACTION) {
                CInline_GenerateTemplateInline(object);
                return;
            }
            return;

        case DALIAS:
            CInline_ObjectAddrRef(object->u.alias.object);
            return;

        case DDATA:
            if (object->qual & Q_INLINE_DATA) {
                CInit_ExportConst(object);
            }

            if (object->flags & OBJECT_LAZY) {
                object->flags &= ~OBJECT_LAZY;
                CParser_CallBackAction(object);
            }
            return;
    }
}

static Boolean CInline_CheckDependencies(ObjectList* list) {
    Object* object;
    Boolean result;

    result = FALSE;

    while (list) {
        object = list->object;

        if ((TYPE_FUNC(object->type)->flags & FUNC_AUTO_GENERATED) &&
            !(TYPE_FUNC(object->type)->flags & FUNC_DEFINED)) {
            CInline_AddDefaultFunctionAction(object);
            result = TRUE;
        } else if ((object->qual & Q_IS_TEMPLATED) && CTempl_InlineFunctionCheck(object)) {
            result = TRUE;
        } else {
            CI_Action* action;
            for (action = cinline_actionlist; action; action = action->next) {
                if (object == action->obj) {
                    result = TRUE;
                    break;
                }
            }

            if (TYPE_FUNC(object->type)->flags & FUNC_TEMPLATE_ACTION) {
                CInline_GenerateTemplateInline(object);
                result = TRUE;
            }
        }

        list = list->next;
    }

    return result;
}

static Boolean CInline_IsSmallFunction(Object* object, Statement* stmt) {
    ObjectList* list;
    SInt32 statementCount;
    SInt32 localSize;

    statementCount = 0;
    while (stmt) {
        if (stmt->type != ST_NOP && stmt->type != ST_LABEL) {
            statementCount++;
        }
        if (statementCount > 30) {
            return FALSE;
        }
        stmt = stmt->next;
    }

    for (list = locals, localSize = 0; list; list = list->next) {
        localSize += list->object->type->size;
    }

    if (localSize > 1024) {
        return FALSE;
    }

    return TRUE;
}

static Boolean CInline_NoFPLocals(void) {
    ObjectList* list;

    for (list = locals; list; list = list->next) {
        if (IS_TYPE_FLOAT(list->object->type)) {
            return FALSE;
        }
    }

    return TRUE;
}

void CInline_GenFunc(Statement* stmt, Object* object, UInt8 oldStyleParameters) {
    CI_FuncData* funcdata;
    CI_Export* export;
    Boolean isAutoInline;
    Boolean isInline;

    TYPE_FUNC(object->type)->flags |= FUNC_DEFINED;

    isInline = isAutoInline = FALSE;
    if (!(object->qual & Q_INLINE)) {
        if (copts.auto_inline &&
            !copts.dontinline &&
            CInline_CanInline(object, stmt->next) &&
            CInline_IsSmallFunction(object, stmt->next)) {
            isInline = isAutoInline = TRUE;
            TYPE_FUNC(object->type)->flags |= FUNC_AUTO_INLINE;
        }
    } else {
        isInline = TRUE;
    }

    if (isInline) {
        COpt_SimpleOptimizer(object, stmt);

        funcdata = galloc(sizeof(CI_FuncData));
        CInline_PackIFunctionData(funcdata, stmt, object);

        object->u.func.u.ifuncdata = funcdata;

        if (!isAutoInline && !(object->flags & OBJECT_REFERENCED)) {
            if (cinline_gendeps) {
                cinline_freflist = NULL;
                CInline_AddFRefList_Statement(stmt);
                CInline_CheckDependencies(cinline_freflist);
            }
            return;
        }
    }

    object->flags |= OBJECT_DEFINED;

    cinline_freflist = NULL;
    CInline_AddFRefList_Statement(stmt);

    if (CInline_CheckDependencies(cinline_freflist) || copts.defer_codegen) {
        if (!isInline) {
            funcdata = galloc(sizeof(CI_FuncData));
            CInline_PackIFunctionData(funcdata, stmt, object);
        } else {
            funcdata = object->u.func.u.ifuncdata;
        }

        export = galloc(sizeof(CI_Export));
        export->object = object;
        export->funcdata = funcdata;
        export->oldStyleParameters = oldStyleParameters;

        export->next = cinline_exportlist;
        cinline_exportlist = export;
    } else {
        expanding_function = object;
        recursive_inline = FALSE;
        CInline_Expand(stmt);

        if (copts.filesyminfo) {
            CPrep_SetSourceFile(&cparser_fileoffset);
        }

        if (!anyerrors) {
            CodeGen_Generator(stmt, object, oldStyleParameters, FALSE);
        }
    }
}

static void CInline_GenerateDefaultFunc(Object* object) {
    TypeClass* tclass;

    CError_ASSERT(4771, TYPE_FUNC(object->type)->flags & FUNC_AUTO_GENERATED);
    CError_ASSERT(4772, TYPE_FUNC(object->type)->flags & FUNC_METHOD);

    tclass = TYPE_METHOD(object->type)->theclass;

    if (object == CClass_DefaultConstructor(tclass)) {
        if (object->u.func.defargdata) {
            CABI_MakeDefaultArgConstructor(tclass, object);
        } else {
            CABI_MakeDefaultConstructor(tclass, object);
        }
        return;
    }
    if (object == CClass_CopyConstructor(tclass)) {
        CABI_MakeDefaultCopyConstructor(tclass, object);
        return;
    }
    if (object == CClass_AssignmentOperator(tclass)) {
        CABI_MakeDefaultAssignmentOperator(tclass, object);
        return;
    }
    if (object == CClass_Destructor(tclass)) {
        CABI_MakeDefaultDestructor(tclass, object);
        return;
    }
    CError_FATAL(4806);
}

static TemplClassInst* CInline_FindNestedTemplInst(TypeClass* tclass) {
    NameSpace* nspace;

    while (tclass) {
        if ((tclass->flags & CLASS_IS_TEMPL_INST)) {
            return TEMPL_CLASS_INST(tclass);
        }

        if (!copts.template_patch) {
            break;
        }

        nspace = tclass->nspace->parent;
        tclass = NULL;
        while (nspace) {
            if (nspace->theclass) {
                tclass = nspace->theclass;
                break;
            }
            nspace = nspace->parent;
        }
    }

    return NULL;
}

static void CInline_GenerateInlineFunc(CI_Action* action) {
    Object* object;
    TemplClassInst* inst;
    DeclInfo di;
    SInt32 streamState;

    object = action->obj;

    CPrep_StreamInsert(&action->u.inlinefunc.stream, &streamState);
    cparser_fileoffset = action->u.inlinefunc.fileoffset;
    symdecloffset = cparser_fileoffset.tokenline;

    switch ((tk = lex())) {
        case ':':
        case '{':
        case TK_TRY:
            break;
        default:
            CError_FATAL(4861);
    }

    symdecltoken = *CPrep_CurStreamElement();

    TYPE_FUNC(object->type)->flags &= ~FUNC_DEFINED;
    if (IS_TYPE_METHOD(object->type) &&
        (inst = CInline_FindNestedTemplInst(TYPE_METHOD(object->type)->theclass))) {
        CTempl_ParseInstanceScopeFunction(object, inst, NULL);
    } else {
        memclrw(&di, sizeof(di));
        if (action->u.inlinefunc.tclass) {
            if ((inst = CInline_FindNestedTemplInst(action->u.inlinefunc.tclass))) {
                CTempl_ParseInstanceScopeFunction(object, inst, action->u.inlinefunc.tclass);
            } else {
                CFunc_ParseFuncDef(object, &di, action->u.inlinefunc.tclass, FALSE, FALSE, NULL);
            }
        } else {
            CFunc_ParseFuncDef(object, &di, NULL, FALSE, FALSE, NULL);
        }
    }

    CPrep_StreamRemove(&action->u.inlinefunc.stream, &streamState);
}

Boolean CInline_CanFreeLHeap(void) {
    CI_Action* action;

    if (!anyerrors) {
        for (action = cinline_actionlist; action; action = action->next) {
            if (action->actiontype == CI_ActionInlineFunc) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

Boolean CInline_GenerateDeferredFuncs(void) {
    CI_Action* action;
    CI_Export* export;
    struct CInlineBackendOptions saveOptions;

    if (!anyerrors) {
        if ((action = cinline_actionlist)) {
            cinline_actionlist = cinline_actionlist->next;
            cinline_gendeps = TRUE;

            switch (action->actiontype) {
                case CI_ActionDefaultFunc:
                    CInline_GenerateDefaultFunc(action->obj);
                    break;
                case CI_ActionInlineFunc:
                    if (!(action->obj->flags & OBJECT_DEFINED)) {
                        CInline_GenerateInlineFunc(action);
                    }
                    break;
                case CI_ActionMemberFunc:
                    if (!(TYPE_FUNC(action->obj->type)->flags & FUNC_DEFINED)) {
                        CTempl_InstantiateMember(
                            action->u.memberfunc.templ,
                            action->u.memberfunc.inst,
                            action->u.memberfunc.tmemb,
                            action->obj,
                            FALSE
                        );
                    }
                    break;
                case CI_ActionTemplateFunc:
                    if (!(TYPE_FUNC(action->obj->type)->flags & FUNC_DEFINED) &&
                        !action->u.templatefunc.inst->is_specialized) {
                        CTempl_GenFuncInstance(
                            action->u.templatefunc.func, action->u.templatefunc.inst, FALSE
                        );
                    }
                    break;
                default:
                    CError_FATAL(5002);
            }

            cinline_gendeps = FALSE;
            return TRUE;
        } else {
            if (cinline_exportlist && !copts.defer_codegen) {
                export = cinline_exportlist;
                cinline_exportlist = export->next;
                CMach_GetBackEndOptions(&saveOptions);
                CMach_SetBackEndOptions(&export->funcdata->options);
                CInline_GenIFunctionCode(
                    export->object, export->funcdata, export->oldStyleParameters
                );
                CMach_SetBackEndOptions(&saveOptions);
                copts.defer_codegen = FALSE;
                return TRUE;
            }
        }
    }

    return FALSE;
}

static InitExpr* CInline_InitTemplateData(InitExpr* init) {
    Statement* stmt;
    CLabel* label;
    Object* object;
    Object* data;

    object = init->object;

    data = CParser_NewCompilerDefDataObject();
    data->type = TYPE(&stsignedchar);
    data->name = CParser_NameConcat("__init__", CMangler_GetLinkName(object)->name);
    data->qual = Q_WEAK;
    CInit_DeclareData(data, NULL, NULL, data->type->size);

    stmt = CFunc_AppendStatement(ST_IFGOTO);
    stmt->expr = create_objectnode(data);
    label = newlabel();
    stmt->label = label;

    do {
        stmt = CFunc_AppendStatement(ST_EXPRESSION);
        stmt->expr = CInline_CopyExpression(init->expr, CopyModeLocal);
        init = init->next;
    } while (init && init->object == object);

    stmt = CFunc_AppendStatement(ST_EXPRESSION);
    stmt->expr =
        makediadicnode(create_objectnode(data), intconstnode(TYPE(&stsignedchar), 1), EASS);

    stmt = CFunc_AppendStatement(ST_LABEL);
    stmt->label = label;
    label->stmt = stmt;

    return init;
}

void CInline_Finish(void) {
    NameSpace* nspace;
    Boolean saveDebugInfo;
    Statement firstStmt;
    Statement* stmt;
    InitExpr* init;
    Boolean doMore;

    if (!init_expressions || anyerrors) {
        return;
    }

    cinline_freflist = NULL;

    for (init = init_expressions; init; init = init->next) {
        CInline_AddFRefList_Expr(init->expr);
    }

    CInline_CheckDependencies(cinline_freflist);

    do {
        doMore = CInline_GenerateDeferredFuncs();
    } while (doMore);

    nspace = CFunc_FuncGenSetup(&firstStmt, NULL);
    saveDebugInfo = copts.filesyminfo;
    copts.filesyminfo = FALSE;

    init = init_expressions;
    while (init) {
        if (init->object->nspace->theclass &&
            (init->object->nspace->theclass->flags & CLASS_IS_TEMPL_INST)) {
            init = CInline_InitTemplateData(init);
        } else {
            stmt = CFunc_AppendStatement(ST_EXPRESSION);
            stmt->expr = CInline_CopyExpression(init->expr, CopyModeLocal);
            init = init->next;
        }
    }

    CFunc_CodeCleanup(&firstStmt);

    expanding_function = NULL;
    recursive_inline = FALSE;
    CInline_Expand(&firstStmt);

    if (!anyerrors) {
        if (copts.filesyminfo) {
            CPrep_SetSourceFile(&cparser_fileoffset);
        }
        CodeGen_Generator(&firstStmt, NULL, 0, TRUE);
    }

    cscope_current = nspace->parent;
    copts.filesyminfo = saveDebugInfo;
}

#pragma pack(2)
