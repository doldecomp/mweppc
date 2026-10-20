// CExpr2.c: the expression trees' builders and walkers, conversions and the old argument matcher
// (assign_check, user_assign_check), function calls, and new and delete. Built -inline
// auto,deferred: the binary holds the functions in the reverse of the source order.
#include "compiler/CABI.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CInline.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CObjC.h"
#include "compiler/CObjCModern.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/CSOM.h"
#include "compiler/CTemplateFunc.h"
#include "compiler/CTemplateTools.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/templates.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include <stdarg.h>
#include <string.h>

ENode* assign_node;
Boolean temp_reference_init;
static SInt32 assign_value;
static ENode* firstarrayexpr;
static ComparisonValues user_std_match;
static Boolean cexpr_hascall;
static Boolean cexpr_esearch_bool[MAXEXPR];
static Boolean cexpr_rsearch_bool[MAXEXPR];
static CExprSearchCB cexpr_esearch_callback;
static CExprReplaceCB cexpr_rsearch_callback;
static Type* cexpr_left_conversion_type;
static Type* cexpr_right_conversion_type;

// (mon_arg is unused: the original's .data keeps it)
static FuncArg mon_arg = {NULL, NULL, NULL, NULL, 0, 0, FALSE};
static FuncArg diadic_arg2 = {NULL, NULL, NULL, NULL, 0, 0, FALSE};
static FuncArg diadic_arg1 = {&diadic_arg2, NULL, NULL, NULL, 0, 0, FALSE};

static void CExpr_RecSearchExprTree(ENode* expr) {
    ENodeList* list;

restart:
    if (cexpr_esearch_bool[expr->type]) {
        cexpr_esearch_callback(expr);
    }

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
            expr = expr->data.monadic;
            goto restart;
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
            CExpr_RecSearchExprTree(expr->data.diadic.left);
            expr = expr->data.diadic.right;
            goto restart;
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
        case EMEMBER:
        case EINSTRUCTION:
        case EVECTOR128CONST:
            return;
        case EFUNCCALL:
        case EFUNCCALLP:
            for (list = expr->data.funccall.args; list; list = list->next) {
                CExpr_RecSearchExprTree(list->node);
            }
            expr = expr->data.funccall.funcref;
            goto restart;
        case ENULLCHECK:
            CExpr_RecSearchExprTree(expr->data.nullcheck.nullcheckexpr);
            expr = expr->data.nullcheck.condexpr;
            goto restart;
        case EMFPOINTER:
            CExpr_RecSearchExprTree(expr->data.mfpointer.accessnode);
            expr = expr->data.mfpointer.mfpointer;
            goto restart;
        case ECOND:
            CExpr_RecSearchExprTree(expr->data.cond.cond);
            CExpr_RecSearchExprTree(expr->data.cond.expr1);
            expr = expr->data.cond.expr2;
            goto restart;
        case ENEWEXCEPTION:
        case ENEWEXCEPTIONARRAY:
            CExpr_RecSearchExprTree(expr->data.newexception.initexpr);
            expr = expr->data.newexception.tryexpr;
            goto restart;
        case EINITTRYCATCH:
            if (expr->data.itc.initexpr) {
                CExpr_RecSearchExprTree(expr->data.itc.initexpr);
            }
            if (expr->data.itc.tryexpr) {
                CExpr_RecSearchExprTree(expr->data.itc.tryexpr);
            }
            if (expr->data.itc.catchexpr) {
                CExpr_RecSearchExprTree(expr->data.itc.catchexpr);
            }
            if (expr->data.itc.result) {
                CExpr_RecSearchExprTree(expr->data.itc.result);
            }
            return;
        default:
            CError_FATAL(128);
    }
}

void CExpr_SearchExprTree(ENode* expr, CExprSearchCB callback, SInt32 count, ...) {
    va_list ap;
    short i;

    cexpr_esearch_callback = callback;

    va_start(ap, count);
    for (i = 0; i < MAXEXPR; i++) {
        cexpr_esearch_bool[i] = FALSE;
    }
    i = 0;
    while ((short)i < count) {
        cexpr_esearch_bool[va_arg(ap, int)] = TRUE;
        ++i;
    }
    va_end(ap);

    CExpr_RecSearchExprTree(expr);
}

static ENode* CExpr_RecSearchExprTreeReplace(ENode* expr) {
    ENodeList* list;
    ENode* replaced;

    if (cexpr_rsearch_bool[expr->type]) {
        replaced = cexpr_rsearch_callback(expr);
        if (!replaced) {
            return expr;
        }
        expr = replaced;
    }

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
            expr->data.monadic = CExpr_RecSearchExprTreeReplace(expr->data.monadic);
            return expr;
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
            expr->data.diadic.left = CExpr_RecSearchExprTreeReplace(expr->data.diadic.left);
            expr->data.diadic.right = CExpr_RecSearchExprTreeReplace(expr->data.diadic.right);
            return expr;
        case EINTCONST:
        case EFLOATCONST:
        case ESTRINGCONST:
        case EOBJREF:
        case EPRECOMP:
        case ETEMP:
        case EARGOBJ:
        case ELOCOBJ:
        case ELABEL:
        case EMEMBER:
        case EINSTRUCTION:
        case EVECTOR128CONST:
            return expr;
        case EFUNCCALL:
        case EFUNCCALLP:
            for (list = expr->data.funccall.args; list; list = list->next) {
                list->node = CExpr_RecSearchExprTreeReplace(list->node);
            }
            expr->data.funccall.funcref =
                CExpr_RecSearchExprTreeReplace(expr->data.funccall.funcref);
            return expr;
        case ENULLCHECK:
            expr->data.nullcheck.nullcheckexpr =
                CExpr_RecSearchExprTreeReplace(expr->data.nullcheck.nullcheckexpr);
            expr->data.nullcheck.condexpr =
                CExpr_RecSearchExprTreeReplace(expr->data.nullcheck.condexpr);
            return expr;
        case EMFPOINTER:
            expr->data.diadic.left = CExpr_RecSearchExprTreeReplace(expr->data.diadic.left);
            expr->data.diadic.right = CExpr_RecSearchExprTreeReplace(expr->data.diadic.right);
            return expr;
        case ECOND:
            expr->data.cond.cond = CExpr_RecSearchExprTreeReplace(expr->data.cond.cond);
            expr->data.cond.expr1 = CExpr_RecSearchExprTreeReplace(expr->data.cond.expr1);
            expr->data.cond.expr2 = CExpr_RecSearchExprTreeReplace(expr->data.cond.expr2);
            return expr;
        default:
            CError_FATAL(220);
            return NULL;
    }
}

// unused (nothing calls it)
ENode* CExpr_SearchExprTreeReplace(ENode* expr, CExprReplaceCB callback, int count, ...) {
    va_list ap;
    short i;

    cexpr_rsearch_callback = callback;

    va_start(ap, count);
    for (i = 0; i < MAXEXPR; i++) {
        cexpr_rsearch_bool[i] = FALSE;
    }
    i = 0;
    while ((short)i < count) {
        cexpr_rsearch_bool[va_arg(ap, int)] = TRUE;
        ++i;
    }
    va_end(ap);

    return CExpr_RecSearchExprTreeReplace(expr);
}

static void CExpr_HasFuncCallCallBack(ENode* expr) {
    cexpr_hascall = TRUE;
}

// unused (nothing calls it)
Boolean CExpr_HasFuncCall(ENode* expr) {
    cexpr_hascall = FALSE;
    CExpr_SearchExprTree(expr, CExpr_HasFuncCallCallBack, 2, EFUNCCALL, EFUNCCALLP);
    return cexpr_hascall;
}

void CExpr_AliasTransform(ENode* expr) {
    ENode* n;

    Object* obj = expr->data.objref;
    if (obj->u.alias.offset != 0) {
        n = makediadicnode(
            create_objectrefnode(obj->u.alias.object),
            intconstnode(TYPE(&stunsignedlong), obj->u.alias.offset),
            EADD
        );
        *expr = *n;
    } else {
        expr->data.objref = obj->u.alias.object;
    }
}

ENode* CExpr_UnaryFloatExpression(ENode* expr) {
    return expr;
}

ENode* CExpr_BinaryFloatExpression(ENode* expr) {
    return expr;
}

ENode* CExpr_NewENode(ENodeType ty) {
    ENode* expr = lalloc(sizeof(ENode));
    memclrw(expr, sizeof(ENode));
    expr->type = ty;
    return expr;
}

ENode* CExpr_NewTemplDepENode(TemplDepSubType t) {
    ENode* expr = CExpr_NewENode(ETEMPLDEP);
    expr->rtype = &sttemplexpr;
    expr->data.templdep.subtype = t;
    return expr;
}

ENode* nullnode(void) {
    ENode* expr = CExpr_NewENode(EINTCONST);
    expr->rtype = TYPE(&stsignedlong);
    return expr;
}

ENode* intconstnode(Type* type, SInt32 value) {
    ENode* expr = CExpr_NewENode(EINTCONST);
    expr->rtype = type;
    CInt64_SetLong(&expr->data.intval, value);
    return expr;
}

// unused (nothing calls it)
ENode* stringconstnode(char* str) {
    ENode* expr;
    SInt32 size;

    size = strlen(str) + 1;
    expr = CExpr_NewENode(ESTRINGCONST);
    expr->rtype = CDecl_NewArrayType(TYPE(&stchar), size);
    expr->data.string.size = size;
    expr->data.string.data = str;
    expr->data.string.ispascal = 0;
    if (copts.const_strings) {
        expr->flags = Q_CONST;
    }

    expr = makemonadicnode(expr, EINDIRECT);
    expr->data.monadic->rtype = CDecl_NewPointerType(expr->rtype);
    return expr;
}

ENode* forceintegral(ENode* expr) {
    if (!IS_TYPE_ENUM(expr->rtype)) {
        CError_Error(ERR_ILLEGAL_OPERAND);
        return nullnode();
    } else {
        expr->rtype = TYPE_ENUM(expr->rtype)->enumtype;
        return expr;
    }
}

ENode* makemonadicnode(ENode* inner, ENodeType ty) {
    ENode* expr = lalloc(sizeof(ENode));
    expr->type = ty;
    if ((expr->cost = inner->cost) == 0) {
        expr->cost = 1;
    }
    expr->flags = inner->flags & ENODE_FLAG_QUALS;
    expr->rtype = inner->rtype;
    expr->data.monadic = inner;
    return expr;
}

ENode* makediadicnode(ENode* left, ENode* right, UInt8 ty) {
    ENode* expr = lalloc(sizeof(ENode));
    expr->type = ty;
    expr->rtype = left->rtype;
    expr->data.diadic.left = left;
    expr->data.diadic.right = right;

    if (left->cost != right->cost) {
        expr->cost = right->cost;
        if (left->cost > expr->cost) {
            expr->cost = left->cost;
        }
    } else {
        expr->cost = right->cost + 1;
        if (expr->cost > 200) {
            expr->cost = 200;
        }
    }

    expr->flags = (left->flags | right->flags) & ENODE_FLAG_QUALS;
    return expr;
}

ENode* makecommaexpression(ENode* left, ENode* right) {
    ENode* expr;

    if (ENODE_IS(right, EINDIRECT) && !ENODE_IS(right->data.monadic, EBITFIELD)) {
        Boolean savecpp = copts.cplusplus;
        copts.cplusplus = 1;
        expr = makediadicnode(left, getnodeaddress(right, FALSE), ECOMMA);
        copts.cplusplus = savecpp;
        expr->rtype = expr->data.diadic.right->rtype;
        expr = makemonadicnode(expr, EINDIRECT);
    } else {
        expr = makediadicnode(left, right, ECOMMA);
    }

    expr->rtype = right->rtype;
    expr->flags = right->flags;
    return expr;
}

short iszero(ENode* expr) {
    switch (expr->type) {
        case EINTCONST:
            return CInt64_IsZero(&expr->data.intval);
        case EFLOATCONST:
            return CMach_FloatIsZero(expr->data.floatval);
        default:
            return FALSE;
    }
}

short isnotzero(ENode* expr) {
    Object* obj;

    switch (expr->type) {
        case EINTCONST:
            return !CInt64_IsZero(&expr->data.intval);
        case EFLOATCONST:
            return !CMach_FloatIsZero(expr->data.floatval);
        case ESTRINGCONST:
        case ETEMP:
            return TRUE;
        case EOBJREF:
            obj = expr->data.objref;
            break;
        case EADD:
        case ESUB:
            if (ENODE_IS(expr->data.diadic.left, EOBJREF) &&
                ENODE_IS(expr->data.diadic.right, EINTCONST)) {
                obj = expr->data.diadic.left->data.objref;
                break;
            }
            if (ENODE_IS(expr->data.diadic.left, EINTCONST) &&
                ENODE_IS(expr->data.diadic.right, EOBJREF)) {
                obj = expr->data.diadic.right->data.objref;
                break;
            }
            return FALSE;
        default:
            return FALSE;
    }

    switch (obj->datatype) {
        case DLOCAL:
            return TRUE;
        default:
            return FALSE;
    }
}

Boolean CExpr_IsOne(ENode* expr) {
    if (ENODE_IS(expr, EINTCONST)) {
        return CInt64_Equal(expr->data.intval, cint64_one);
    } else if (ENODE_IS(expr, EFLOATCONST)) {
        return CMach_FloatIsOne(expr->data.floatval);
    } else {
        return FALSE;
    }
}

Boolean CExpr_AllBitsSet(ENode* expr) {
    SInt32 v;

    if (ENODE_IS(expr, EINTCONST)) {
        switch (expr->rtype->size) {
            case 1:
                v = CInt64_GetULong(&expr->data.intval) & 0xFF;
                return v == 0xFF;
            case 2:
                v = CInt64_GetULong(&expr->data.intval) & 0xFFFF;
                return v == 0xFFFF;
            case 3:
                v = CInt64_GetULong(&expr->data.intval) & 0xFFFFFF;
                return v == 0xFFFFFF;
            case 4:
                v = CInt64_GetULong(&expr->data.intval) & 0xFFFFFFFF;
                return v == (SInt32)0xFFFFFFFF;
            default:
                return CInt64_Equal(expr->data.intval, cint64_negone);
        }
    }

    return FALSE;
}

ENode* CExpr_NewETEMPNode(Type* type, Boolean assign_id) {
    ENode* expr = lalloc(sizeof(ENode));
    expr->type = ETEMP;
    expr->cost = 0;
    expr->flags = 0;
    expr->rtype = CDecl_NewPointerType(type);
    expr->data.temp.type = type;
    expr->data.temp.uniqueid = assign_id ? CParser_GetUniqueID() : 0;
    expr->data.temp.needs_dtor = FALSE;
    return expr;
}

static ENode* CExpr_DerefETEMPCopy(ENode* expr) {
    ENode* copy;

    CError_ASSERT(636, IS_TYPE_POINTER_ONLY(expr->rtype));

    copy = lalloc(sizeof(ENode));
    *copy = *expr;
    copy = makemonadicnode(copy, EINDIRECT);
    copy->rtype = TYPE_POINTER(copy->rtype)->target;
    return copy;
}

ENode* CExpr_GetETEMPCopy(ENode* expr) {
    ENode* newnode;
    ENode* assnode;
    ENode* finalnode;

    newnode = CExpr_NewETEMPNode(expr->rtype, TRUE);
    newnode->flags = expr->flags;

    assnode = makediadicnode(CExpr_DerefETEMPCopy(newnode), expr, EASS);
    assnode->data.diadic.right = lalloc(sizeof(ENode));
    *assnode->data.diadic.right = *expr;
    *expr = *assnode;

    finalnode = makemonadicnode(newnode, EINDIRECT);
    finalnode->rtype = expr->rtype;
    return finalnode;
}

ENode* integralpromote(ENode* expr) {
    if (!IS_TYPE_INT(expr->rtype)) {
        expr = forceintegral(expr);
    }

    if (TYPE_INTEGRAL(expr->rtype)->integral >= IT_INT) {
        return expr;
    }

    if (!ENODE_IS(expr, EINTCONST)) {
        expr = makemonadicnode(expr, ETYPCON);
    }
    expr->rtype = TYPE(&stsignedint);

    return expr;
}

// NOTE: invented names: to, from, val
CInt64 CExpr_IntConstConvert(Type* to, Type* from, CInt64 val) {
    if (to == TYPE(&stbool)) {
        return CMach_CalcIntDiadic(from, val, TK_LOGICAL_NE, cint64_zero);
    }
    // the custom integral types (CMach_GetCustomType's, IT_17 and up) convert through CMach
    if ((TYPE_INTEGRAL(to)->type == TYPEINT && TYPE_INTEGRAL(to)->integral >= IT_17) ||
        (TYPE_INTEGRAL(from)->type == TYPEINT && TYPE_INTEGRAL(from)->integral >= IT_17)) {
        return CMach_CustomIntConvert(to, from, val);
    }
    return CMach_CalcIntDiadic(to, val, '+', cint64_zero);
}

ENode* promote(ENode* expr, Type* type) {
    if (ENODE_IS(expr, EINTCONST)) {
        if (IS_TYPE_FLOAT(type)) {
            expr->type = EFLOATCONST;
            expr->data.floatval = CMach_CalcFloatConvertFromInt(expr->rtype, expr->data.intval);
        } else {
            expr->data.intval = CExpr_IntConstConvert(type, expr->rtype, expr->data.intval);
        }
        expr->rtype = type;
        return expr;
    }
    if (ENODE_IS(expr, EFLOATCONST)) {
        if (IS_TYPE_FLOAT(type)) {
            expr->data.floatval = CMach_CalcFloatConvert(type, expr->data.floatval);
            expr->rtype = type;
            return expr;
        } else if (IS_TYPE_INT(type)) {
            expr->data.intval = CMach_CalcIntConvertFromFloat(type, expr->data.floatval);
            expr->type = EINTCONST;
            expr->rtype = type;
            return expr;
        }
    }

    expr = makemonadicnode(expr, ETYPCON);
    expr->rtype = type;
    return expr;
}

void CExpr_ArithmeticConversion(ENode** left, ENode** right) {
    ENode** tmp;

    switch ((*left)->rtype->type) {
        case TYPEINT:
        case TYPEFLOAT:
            break;
        case TYPEENUM:
            (*left)->rtype = TYPE_ENUM((*left)->rtype)->enumtype;
            break;
        default:
            CError_Error(ERR_ILLEGAL_OPERAND);
            (*left) = nullnode();
    }

    switch ((*right)->rtype->type) {
        case TYPEINT:
        case TYPEFLOAT:
            break;
        case TYPEENUM:
            (*right)->rtype = TYPE_ENUM((*right)->rtype)->enumtype;
            break;
        default:
            CError_Error(ERR_ILLEGAL_OPERAND);
            (*right) = nullnode();
    }

    if (IS_TYPE_FLOAT((*left)->rtype) || IS_TYPE_FLOAT((*right)->rtype)) {
        if ((*left)->rtype == (*right)->rtype) {
            return;
        }
        if (TYPE_INTEGRAL((*left)->rtype)->integral > TYPE_INTEGRAL((*right)->rtype)->integral) {
            *right = promote(*right, (*left)->rtype);
        } else {
            *left = promote(*left, (*right)->rtype);
        }
    } else {
        *left = integralpromote(*left);
        *right = integralpromote(*right);
        if ((*left)->rtype == (*right)->rtype) {
            return;
        }
        if (TYPE_INTEGRAL((*left)->rtype)->integral < TYPE_INTEGRAL((*right)->rtype)->integral) {
            tmp = left;
            left = right;
            right = tmp;
        }

        if ((*left)->rtype->size == (*right)->rtype->size &&
            !is_unsigned((*left)->rtype) &&
            is_unsigned((*right)->rtype)) {
            if ((*left)->rtype == TYPE(&stsignedlong)) {
                *left = promote(*left, TYPE(&stunsignedlong));
            } else {
                CError_ASSERT(838, (*left)->rtype == TYPE(&stsignedlonglong));
                *left = promote(*left, TYPE(&stunsignedlonglong));
            }
        }

        *right = promote(*right, (*left)->rtype);
    }
}

static ENode* CExpr_GetEA(ENode* expr) {
    ENode* copy;

    for (;;) {
        switch (expr->type) {
            case EINDIRECT:
                expr = expr->data.monadic;
                for (;;) {
                    switch (expr->type) {
                        case EOBJREF:
                            copy = lalloc(sizeof(ENode));
                            *copy = *expr;
                            return copy;
                        case ECOMMA:
                            expr = expr->data.diadic.right;
                            continue;
                        default:
                            return CExpr_GetETEMPCopy(expr);
                    }
                }
                break;
            case EPOSTINC:
            case EPOSTDEC:
            case EPREINC:
            case EPREDEC:
                expr = expr->data.monadic;
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
                expr = expr->data.monadic;
                continue;
            case ECOMMA:
                expr = expr->data.diadic.right;
                continue;
            default:
                return NULL;
        }
    }
}

ENode* CExpr_TempModifyExpr(ENode* expr) {
    Type* type;
    ENode* left;
    ENode* right;
    ENode* eanode;
    ENode* tempnode;
    ENode* indnode;
    ENode* truenode;

    type = expr->rtype;
    tempnode = CExpr_NewETEMPNode(type, TRUE);
    eanode = CExpr_GetEA(expr);
    if (!eanode) {
        CError_Error(ERR_NOT_LVALUE);
        return expr;
    }

    // *tempnode = expr
    left = makemonadicnode(tempnode, EINDIRECT);
    left->rtype = type;
    left = makediadicnode(left, expr, EASS);

    // *eanode = true
    indnode = makemonadicnode(eanode, EINDIRECT);
    indnode->rtype = type;
    truenode = nullnode();
    truenode->rtype = TYPE(&stbool);
    CInt64_SetLong(&truenode->data.intval, 1);
    right = makediadicnode(indnode, truenode, EASS);

    expr = makediadicnode(left, right, ECOMMA);

    indnode = makemonadicnode(tempnode, EINDIRECT);
    indnode->rtype = type;
    return makediadicnode(expr, indnode, ECOMMA);
}

Boolean CExpr_IsLValue(ENode* expr) {
    while (!ENODE_IS(expr, EINDIRECT)) {
        switch (expr->type) {
            case EPREINC:
            case EPREDEC:
                return copts.cplusplus;
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
                if (!copts.cplusplus) {
                    return FALSE;
                }
                expr = expr->data.diadic.left;
                continue;
            case ECOMMA:
                if (!copts.cplusplus) {
                    return FALSE;
                }
                expr = expr->data.diadic.right;
                continue;
            case ECOND:
                if (copts.cplusplus &&
                    is_typesame(expr->data.cond.expr1->rtype, expr->data.cond.expr2->rtype) &&
                    (expr->data.cond.expr1->rtype->type != TYPEPOINTER ||
                     (UInt32)(expr->data.cond.expr1->flags & ENODE_FLAG_QUALS) ==
                         (expr->data.cond.expr2->flags & ENODE_FLAG_QUALS))) {
                    return CExpr_IsLValue(expr->data.cond.expr1) &&
                        CExpr_IsLValue(expr->data.cond.expr2);
                }
                return FALSE;
            default:
                return FALSE;
        }
    }

    expr = expr->data.monadic;
    switch (expr->type) {
        case ETEMP:
            return FALSE;
        case EFUNCCALL:
            if (expr->data.funccall.functype->functype->type != TYPEPOINTER ||
                (expr->data.funccall.functype->flags & FUNC_IS_CTOR)) {
                return FALSE;
            }
        default:
            return TRUE;
    }
}

// NOTE: invented names: expr, isModified, requireLValue, result
ENode* CExpr_LValue(ENode* expr, Boolean isModified, Boolean requireLValue) {
    ENode* eanode;
    ENode* result;

loop:
    switch (expr->type) {
        case ETYPCON:
            if (copts.pointercast_lvalue || !copts.ANSIstrict) {
                if (expr->rtype->type == TYPEPOINTER &&
                    expr->data.monadic->rtype->type == TYPEPOINTER) {
                    switch (expr->data.monadic->type) {
                        case EINDIRECT:
                        case ETYPCON:
                            expr->data.monadic->rtype = expr->rtype;
                            expr->data.monadic->flags = expr->flags;
                            expr = expr->data.monadic;
                            goto loop;
                        default:
                            break;
                    }
                }
            }
            break;
        case EINDIRECT:
            if (requireLValue) {
                if (!CExpr_IsLValue(expr)) {
                    CError_Warning(ERR_NOT_LVALUE);
                }

                if (ENODE_IS(expr->data.monadic, EOBJREF) &&
                    expr->data.monadic->data.objref->name == this_name_node &&
                    cscope_currentfunc &&
                    cscope_currentclass &&
                    expr->data.monadic->data.objref == CClass_ThisSelfObject()) {
                    CError_Error(ERR_ILLEGAL_USE_THIS);
                }
            }
            if (isModified) {
                if (CParser_IsConst(expr->rtype, expr->flags & ENODE_FLAG_QUALS)) {
                    CError_Error(ERR_ILLEGAL_ASSIGNMENT_CONSTANT);
                }
            }
            return expr;
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
        case ECOMMA:
            if (copts.cplusplus) {
                if ((eanode = CExpr_GetEA(expr))) {
                    result = makediadicnode(expr, eanode, ECOMMA);
                    result->rtype = result->data.diadic.right->rtype;
                    result = makemonadicnode(result, EINDIRECT);
                    result->rtype = expr->rtype;
                    return result;
                }
                CError_Error(ERR_UNIMPLEMENTED_C_FEATURE);
                return expr;
            }
            break;
        case ECOND:
            if (copts.cplusplus &&
                is_typesame(expr->data.cond.expr1->rtype, expr->data.cond.expr2->rtype) &&
                (expr->data.cond.expr1->rtype->type != TYPEPOINTER ||
                 (UInt32)(expr->data.cond.expr1->flags & ENODE_FLAG_QUALS) ==
                     (expr->data.cond.expr2->flags & ENODE_FLAG_QUALS))) {
                expr->data.cond.expr1 =
                    CExpr_LValue(expr->data.cond.expr1, isModified, requireLValue);
                expr->data.cond.expr2 =
                    CExpr_LValue(expr->data.cond.expr2, isModified, requireLValue);
                if (ENODE_IS(expr->data.cond.expr1, EINDIRECT) &&
                    ENODE_IS(expr->data.cond.expr2, EINDIRECT)) {
                    if (!ENODE_IS(expr->data.cond.expr1->data.monadic, EBITFIELD) &&
                        !ENODE_IS(expr->data.cond.expr2->data.monadic, EBITFIELD)) {
                        expr->data.cond.expr1 = getnodeaddress(expr->data.cond.expr1, FALSE);
                        expr->data.cond.expr2 = getnodeaddress(expr->data.cond.expr2, FALSE);
                        expr->rtype = expr->data.cond.expr1->rtype;
                        result = makemonadicnode(expr, EINDIRECT);
                        result->rtype = TYPE_POINTER(result->rtype)->target;
                        return result;
                    }
                }
            }
            break;
        case EFUNCCALL:
            // a small struct returned in registers is stored to a temporary to make an lvalue
            // of it
            if ((IS_TYPE_CLASS(expr->rtype) || IS_TYPE_STRUCT(expr->rtype)) &&
                !Type_RequiresMemoryReturn(expr->rtype)) {
                ENode* temp;
                ENode* tempcopy;

                temp = CExpr_NewETEMPNode(expr->rtype, TRUE);
                tempcopy = lalloc(sizeof(ENode));
                *tempcopy = *temp;
                result = makemonadicnode(temp, EINDIRECT);
                result->rtype = expr->rtype;
                result = makediadicnode(makediadicnode(result, expr, EASS), tempcopy, ECOMMA);
                result->rtype = tempcopy->rtype;
                result = makemonadicnode(result, EINDIRECT);
                result->rtype = expr->rtype;
                return result;
            }
            break;
        default:
            break;
    }

    if (requireLValue) {
        CError_Error(ERR_NOT_LVALUE);
    }
    return expr;
}

// NOTE: invented names: obj, markUsed
ENode* CExpr_MakeObjRefNode(Object* obj, Boolean markUsed) {
    ENode* expr;

    if (obj->sclass == TK_TYPEDEF) {
        CError_Error(ERR_EXPRESSION_SYNTAX_ERROR);
        return intconstnode(TYPE(&void_ptr), 0);
    }

    expr = lalloc(sizeof(ENode));
    memclrw(expr, sizeof(ENode));
    expr->type = EOBJREF;
    expr->data.objref = obj;
    expr->rtype = CDecl_NewPointerType(obj->type);

    if (!IS_TYPE_FUNC(obj->type)) {
        expr->flags = obj->qual & ENODE_FLAG_QUALS;
    }
    if (markUsed) {
        obj->flags |= OBJECT_USED;
    }

    return expr;
}

ENode* create_objectrefnode(Object* obj) {
    if (name_obj_check && !name_obj_check(NULL, obj)) {
        return intconstnode(TYPE(&void_ptr), 0);
    }
    return CExpr_MakeObjRefNode(obj, TRUE);
}

ENode* create_objectnode2(Object* obj) {
    ENode* expr;

    if (name_obj_check && !name_obj_check(NULL, obj)) {
        return nullnode();
    }

    expr = makemonadicnode(CExpr_MakeObjRefNode(obj, TRUE), EINDIRECT);
    expr->rtype = TYPE_POINTER(expr->rtype)->target;
    return expr;
}

ENode* create_objectnode(Object* obj) {
    return checkreference(create_objectnode2(obj));
}

// unused (nothing calls it)
static ENode* CExpr_ExpandArg(ENode* expr, Type* type) {
    if (ENODE_IS(expr, ETYPCON) && IS_TYPE_FLOAT(type)) {
        expr->rtype = type;
        return expr;
    } else {
        return promote(expr, type);
    }
}

ENode* CExpr_IsTempConstruction(ENode* expr, Type* type, ENode** resultexpr) {
    ENodeList* args;
    ENode* funccall;
    ENode* funcref;

    if (!ENODE_IS(expr, EINDIRECT) ||
        expr->rtype != type ||
        !ENODE_IS((funccall = expr->data.monadic), EFUNCCALL) ||
        !(args = funccall->data.funccall.args)) {
        return NULL;
    }

    if (!ENODE_IS((funcref = funccall->data.funccall.funcref), EOBJREF) ||
        !CClass_IsConstructor(funcref->data.objref)) {
        if (expr->data.monadic->data.funccall.functype->functype != type) {
            return NULL;
        }
        if (CABI_GetStructResultArgumentIndex(expr->data.monadic->data.funccall.functype) == 1) {
            args = args->next;
            CError_ASSERT(1277, args);
        }
    }

    if (resultexpr) {
        *resultexpr = args->node;
    }
    return expr->data.monadic;
}

ENode* CExpr_AdjustFunctionCall(ENode* expr) {
    ENodeList* list;

    switch (expr->data.funccall.functype->functype->type) {
        case TYPECLASS:
            CDecl_CompleteType(expr->data.funccall.functype->functype);
        case TYPESTRUCT:
            if (!expr->data.funccall.functype->functype->size) {
                CError_Error(
                    ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS,
                    expr->data.funccall.functype->functype,
                    0
                );
            }
        default:
            break;
    }

    if (CMach_GetFunctionResultClass(expr->data.funccall.functype)) {
        list = lalloc(sizeof(ENodeList));
        if (IS_TYPE_CLASS(expr->data.funccall.functype->functype)) {
            CDecl_CompleteType(expr->data.funccall.functype->functype);
            if (CClass_Destructor(TYPE_CLASS(expr->data.funccall.functype->functype))) {
                list->node = create_temp_node2(expr->rtype);
            } else {
                list->node = create_temp_node(expr->rtype);
            }
        } else {
            list->node = create_temp_node(expr->rtype);
        }
        list->next = expr->data.funccall.args;
        expr->data.funccall.args = list;

        if (expr->data.funccall.funcref->flags & ENODE_FLAG_SOM_ENV_CHECK) {
            expr = CSOM_EnvCheck(expr, list);
        }

        expr = makemonadicnode(expr, EINDIRECT);
        expr->data.monadic->rtype = CDecl_NewPointerType(expr->rtype);
        return expr;
    }

    if (expr->data.funccall.funcref->flags & ENODE_FLAG_SOM_ENV_CHECK) {
        expr = CSOM_EnvCheck(expr, NULL);
    }
    return expr;
}

ENode* funccallexpr(Object* func, ENode* arg1, ENode* arg2, ENode* arg3, ENode* arg4) {
    ENode* expr;
    TypeFunc* tfunc;
    ENodeList* list;

    tfunc = TYPE_FUNC(func->type);
    CError_ASSERT(1411, IS_TYPE_FUNC(tfunc));

    expr = lalloc(sizeof(ENode));
    expr->type = EFUNCCALL;
    expr->cost = 4;
    expr->rtype = tfunc->functype;
    expr->flags = tfunc->qual & ENODE_FLAG_QUALS;
    expr->data.funccall.funcref = create_objectrefnode(func);
    expr->data.funccall.functype = tfunc;

    if (arg1) {
        list = lalloc(sizeof(ENodeList));
        expr->data.funccall.args = list;
        list->node = arg1;
        if (arg2) {
            list->next = lalloc(sizeof(ENodeList));
            list = list->next;
            list->node = arg2;
            if (arg3) {
                list->next = lalloc(sizeof(ENodeList));
                list = list->next;
                list->node = arg3;
                if (arg4) {
                    list->next = lalloc(sizeof(ENodeList));
                    list = list->next;
                    list->node = arg4;
                }
            }
        }
        list->next = NULL;
    } else {
        expr->data.funccall.args = NULL;
    }

    return CExpr_AdjustFunctionCall(expr);
}

ENode* CExpr_FuncCallSix(
    Object* func, ENode* arg1, ENode* arg2, ENode* arg3, ENode* arg4, ENode* arg5, ENode* arg6
) {
    ENode* expr;
    TypeFunc* tfunc;
    ENodeList* list;

    tfunc = TYPE_FUNC(func->type);
    CError_ASSERT(1460, IS_TYPE_FUNC(tfunc));

    expr = lalloc(sizeof(ENode));
    expr->type = EFUNCCALL;
    expr->cost = 4;
    expr->rtype = tfunc->functype;
    expr->flags = tfunc->qual & ENODE_FLAG_QUALS;
    expr->data.funccall.funcref = create_objectrefnode(func);
    expr->data.funccall.functype = tfunc;

    list = lalloc(sizeof(ENodeList));
    expr->data.funccall.args = list;
    list->node = arg1;
    list->next = lalloc(sizeof(ENodeList));
    list = list->next;
    list->node = arg2;
    list->next = lalloc(sizeof(ENodeList));
    list = list->next;
    list->node = arg3;
    list->next = lalloc(sizeof(ENodeList));
    list = list->next;
    list->node = arg4;
    list->next = lalloc(sizeof(ENodeList));
    list = list->next;
    list->node = arg5;
    if (arg6) {
        list->next = lalloc(sizeof(ENodeList));
        list = list->next;
        list->node = arg6;
    }
    list->next = NULL;

    return CExpr_AdjustFunctionCall(expr);
}

// NOTE: invented names: checkresult, match, t1, q1, t2, q2, isConversionFunc
static void CExpr_CalcStdAssign(
    short checkresult,
    ComparisonValues* match,
    Type* t1,
    UInt32 q1,
    Type* t2,
    UInt32 q2,
    Boolean isConversionFunc
) {
    memclrw(match, sizeof(ComparisonValues));
    switch (checkresult) {
        case CheckResult_Exact:
            match->exactMatches++;
            break;
        case CheckResult_Promotion:
            match->promotions++;
            break;
        case CheckResult_Conversion:
            match->conversions++;
            match->conversionWeight += assign_value;
            break;
        default:
            CError_FATAL(1504);
    }

    if (isConversionFunc ||
        (IS_TYPE_POINTER_ONLY(t2) &&
         (IS_TYPE_POINTER_ONLY(t1) || IS_TYPEPOINTER_REFERENCE(TYPE_POINTER(t2))) != 0)) {
        if ((q2 & Q_CONST) == (q1 & Q_CONST)) {
            match->qualifierMatches++;
        }
        if ((q2 & Q_VOLATILE) == (q1 & Q_VOLATILE)) {
            match->qualifierMatches++;
        }
    }
}

void CExpr_MatchCV(Type* t1, UInt32 q1, Type* t2, UInt32 q2, ArgMatch* match) {
    Boolean check_cv;

    if (IS_TYPEPOINTER_REFERENCE(TYPE_POINTER(t2))) {
        t2 = TYPE_POINTER(t2)->target;
        check_cv = TRUE;
    } else {
        check_cv = FALSE;
    }

    while (IS_TYPE_POINTER_ONLY(t2) && IS_TYPE_POINTER_ONLY(t1)) {
        if (check_cv) {
            if ((TYPE_POINTER(t1)->qual & Q_CONST) != (TYPE_POINTER(t2)->qual & Q_CONST)) {
                match->standard.qualifierMatches--;
            }
            if ((TYPE_POINTER(t1)->qual & Q_VOLATILE) != (TYPE_POINTER(t2)->qual & Q_VOLATILE)) {
                match->standard.qualifierMatches--;
            }
        }
        t2 = TYPE_POINTER(t2)->target;
        t1 = TYPE_POINTER(t1)->target;
        check_cv = TRUE;
    }

    if ((q1 & Q_CONST) != (q2 & Q_CONST)) {
        match->standard.qualifierMatches--;
    }
    if ((q1 & Q_VOLATILE) != (q2 & Q_VOLATILE)) {
        match->standard.qualifierMatches--;
    }
}

Boolean CExpr_MatchAssign(Type* type, UInt32 qual, ENode* expr, ArgMatch* match) {
    switch (assign_check(expr, type, qual, FALSE, FALSE, TRUE)) {
        case CheckResult_NoMatch:
            return FALSE;
        case CheckResult_Exact:
            match->standard.exactMatches++;
            break;
        case CheckResult_Promotion:
            match->standard.promotions++;
            break;
        case CheckResult_Conversion:
            match->standard.conversions++;
            match->standard.conversionWeight += assign_value;
            break;
        case CheckResult_UserConversion:
            match->userconversions++;
            match->userStandard.exactMatches += user_std_match.exactMatches;
            match->userStandard.promotions += user_std_match.promotions;
            match->userStandard.conversions += user_std_match.conversions;
            match->userStandard.conversionWeight += user_std_match.conversionWeight;
            match->userStandard.qualifierMatches += user_std_match.qualifierMatches;
            break;
        default:
            CError_FATAL(1585);
    }

    if (IS_TYPE_POINTER_ONLY(type)) {
        CExpr_MatchCV(expr->rtype, expr->flags & ENODE_FLAG_QUALS, type, qual, match);
    }

    return TRUE;
}

// NOTE: invented names: a, b, compareQualifiers
static short CExpr_StdMatchCompare(
    ComparisonValues* a, ComparisonValues* b, Boolean compareQualifiers
) {
    if (a->exactMatches > b->exactMatches) {
        return 1;
    }
    if (a->exactMatches == b->exactMatches) {
        if (a->promotions > b->promotions) {
            return 1;
        }
        if (a->promotions == b->promotions) {
            if (a->conversions > b->conversions) {
                return 1;
            }
            if (a->conversions == b->conversions) {
                if (a->conversionWeight > b->conversionWeight) {
                    return 1;
                }
                if (a->conversionWeight == b->conversionWeight) {
                    if (!compareQualifiers) {
                        return 0;
                    }
                    if (a->qualifierMatches > b->qualifierMatches) {
                        return 1;
                    }
                    if (a->qualifierMatches == b->qualifierMatches) {
                        return 0;
                    }
                }
            }
        }
    }
    return -1;
}

// NOTE: invented names: type, expr, doConvert
static short CExpr2_MemberPointerConversion(Type* type, ENode* expr, Boolean doConvert) {
    ENode* newnode;
    short depth;

    newnode = lalloc(sizeof(ENode));
    *newnode = *expr;
    if (!IS_TYPE_MEMBERPOINTER(newnode->rtype)) {
        newnode = CExpr_MemberPointerConversion(newnode, TYPE_MEMBER_POINTER(type), doConvert);
        if (iscpp_typeequal(newnode->rtype, type)) {
            if (doConvert) {
                assign_node = newnode;
            }
            return CheckResult_Conversion;
        }
    }

    if (IS_TYPE_MEMBERPOINTER(newnode->rtype)) {
        CError_ASSERT(1656, IS_TYPE_CLASS(TYPE_MEMBER_POINTER(type)->ty2));
        CError_ASSERT(1657, IS_TYPE_CLASS(TYPE_MEMBER_POINTER(newnode->rtype)->ty2));

        if (CClass_IsBaseClass(
                TYPE_CLASS(TYPE_MEMBER_POINTER(type)->ty2),
                TYPE_CLASS(TYPE_MEMBER_POINTER(newnode->rtype)->ty2),
                &depth,
                FALSE,
                FALSE
            )) {
            assign_value = 1000 - depth;
            if (doConvert) {
                assign_node = PointerToMemberCast(
                    newnode, TYPE_MEMBER_POINTER(newnode->rtype), TYPE_MEMBER_POINTER(type), TRUE
                );
            }
            return CheckResult_Conversion;
        }
    }

    return CheckResult_NoMatch;
}

ENode* CExpr_ClassPointerCast(BClassList* cls, ENode* origexpr, Boolean nullcheckflag) {
    ENode* expr;
    ClassList* base;
    Boolean do_nullcheck;
    TypeClass* tclass;
    SInt32 offset;
    ENode* nullexpr; // NOTE: invented name

    expr = origexpr;
    tclass = TYPE_CLASS(cls->type);
    do_nullcheck = FALSE;
    CError_ASSERT(1691, cls);

    if (!IS_TYPE_POINTER_ONLY(origexpr->rtype)) {
        CError_Error(ERR_EXPRESSION_SYNTAX_ERROR);
        return origexpr;
    }

    cls = cls->next;
    while (cls) {
        for (base = tclass->bases; base; base = base->next) {
            if (base->base == TYPE_CLASS(cls->type)) {
                break;
            }
        }

        if (!base) {
            CError_Error(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
            while (cls->next) {
                cls = cls->next;
            }

            nullexpr = nullnode();
            nullexpr->rtype = CDecl_NewPointerType(cls->type);
            return nullexpr;
        }

        if (base->is_virtual) {
            if (!base->base->sominfo) {
                do_nullcheck = TRUE;
                if ((offset = base->offset) && !canadd(expr, offset)) {
                    expr = makediadicnode(expr, intconstnode(TYPE(&stunsignedlong), offset), EADD);
                    optimizecomm(expr);
                }
                expr->rtype = CDecl_NewPointerType(CDecl_NewPointerType(TYPE(base->base)));
                expr = makemonadicnode(expr, EINDIRECT);
            }
        } else {
            if ((offset = base->offset)) {
                do_nullcheck = TRUE;
                if (!canadd(expr, offset)) {
                    expr = makediadicnode(expr, intconstnode(TYPE(&stunsignedlong), offset), EADD);
                    optimizecomm(expr);
                }
            }
        }

        switch (expr->type) {
            case EPOSTINC:
            case EPOSTDEC:
            case EPREINC:
            case EPREDEC:
                expr = makemonadicnode(expr, ETYPCON);
            default:
                break;
        }

        expr->rtype = CDecl_NewPointerType(TYPE(base->base));
        tclass = TYPE_CLASS(cls->type);
        cls = cls->next;
    }

    if (nullcheckflag && do_nullcheck) {
        expr = do_castnullcheck(expr, origexpr);
    }
    return expr;
}

// NOTE: invented names: a, b, expr, obj, access, checkAccess
ENode* CExpr_GetClassAccessNode(
    BClassList* a, BClassList* b, ENode* expr, Object* obj, AccessType access, Boolean checkAccess
) {
    TypeClass* tclass;

    if (!expr) {
        if (!cscope_currentfunc ||
            !cscope_currentclass ||
            !cscope_is_member_func ||
            !(expr = CClass_CreateThisSelfExpr())) {
            CError_Error(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
            return NULL;
        }
        expr = makemonadicnode(expr, EINDIRECT);
        expr->rtype = TYPE(cscope_currentclass);
    }

    CError_ASSERT(1786, a);
    CError_ASSERT(1787, IS_TYPE_CLASS(expr->rtype));

    tclass = TYPE_CLASS(expr->rtype);
    a = CScope_GetClassAccessPath(a, tclass);
    if (!a || a->type != TYPE(tclass)) {
        CError_Error(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
        return NULL;
    }

    if (checkAccess) {
        CClass_CheckPathAccess(a, obj, access);
    }

    if (!TYPE_CLASS(a->type)->sominfo) {
        if (b) {
            a = CClass_AppendPath(a, b);
        }

        if (!ENODE_IS(expr, EINDIRECT)) {
            expr = CExpr_LValue(expr, FALSE, FALSE);
        }

        if (ENODE_IS(expr, EINDIRECT)) {
            expr->data.monadic->flags = expr->flags;
            expr = expr->data.monadic;
            switch (expr->type) {
                case EPOSTINC:
                case EPOSTDEC:
                case EPREINC:
                case EPREDEC:
                    expr = makemonadicnode(expr, ETYPCON);
                default:
                    break;
            }
            expr = makemonadicnode(CExpr_ClassPointerCast(a, expr, FALSE), EINDIRECT);
            expr->rtype = TYPE(tclass);
        }
    }

    return expr;
}

// NOTE: invented names: list, templargs, type, doConvert
static short std_assign_check_overload(
    NameSpaceObjectList* list, TemplArg* templargs, Type* type, Boolean doConvert
) {
    ENode* expr;
    Object* used_obj;
    Object* obj;
    TemplFuncInstance* instance;
    Boolean is_ambig;
    Boolean found_non_template_func;

    if (!IS_TYPE_POINTER_ONLY(type) || !IS_TYPE_FUNC(TYPE_POINTER(type)->target)) {
        return CheckResult_NoMatch;
    }

    used_obj = NULL;
    type = TYPE_POINTER(type)->target;
    found_non_template_func = FALSE;
    is_ambig = FALSE;
    for (; list; list = list->next) {
        obj = OBJECT(list->object);
        if (obj->otype != OT_OBJECT) {
            continue;
        }

        if (IS_TYPE_FUNC(obj->type) && (TYPE_FUNC(obj->type)->flags & FUNC_IS_TEMPL)) {
            if (!found_non_template_func && CTempl_CanDeduceFunc(obj, TYPE_FUNC(type), templargs)) {
                instance = CTempl_DeduceFunc(obj, TYPE_FUNC(type), templargs, NULL, FALSE);
                CError_ASSERT(1861, instance);
                if (is_typesame(instance->object->type, type)) {
                    if (used_obj && used_obj != instance->object) {
                        is_ambig = TRUE;
                    } else {
                        used_obj = instance->object;
                    }
                }
            }
        } else {
            if (is_typesame(obj->type, type)) {
                if (used_obj && found_non_template_func) {
                    Object* cmp1;
                    Object* cmp2;
                    cmp1 = obj;
                    if (obj->datatype == DALIAS) {
                        cmp1 = obj->u.alias.object;
                    }
                    cmp2 = used_obj;
                    if (used_obj->datatype == DALIAS) {
                        cmp2 = used_obj->u.alias.object;
                    }
                    if (cmp1 != cmp2) {
                        is_ambig = TRUE;
                    }
                } else {
                    is_ambig = FALSE;
                    used_obj = obj;
                }
                found_non_template_func = TRUE;
            }
        }
    }

    if (used_obj) {
        if (doConvert) {
            if (is_ambig) {
                CError_Error(ERR_AMBIGUOUS_ACCESS_OVERLOADED_FUNCTION);
            }
            // TODO: CExpr_MakeObjRefNode(used_obj, TRUE) written out: the call form (which MWCC
            // inlines too) compiles another way
            if (used_obj->sclass == TK_TYPEDEF) {
                CError_Error(ERR_EXPRESSION_SYNTAX_ERROR);
                expr = intconstnode(TYPE(&void_ptr), 0);
            } else {
                expr = lalloc(sizeof(ENode));
                memclrw(expr, sizeof(ENode));
                expr->type = EOBJREF;
                expr->data.objref = used_obj;
                expr->rtype = CDecl_NewPointerType(used_obj->type);
                if (!IS_TYPE_FUNC(used_obj->type)) {
                    expr->flags = used_obj->qual & ENODE_FLAG_QUALS;
                }
                used_obj->flags |= OBJECT_USED;
            }
            assign_node = expr;
            expr->rtype = CDecl_NewPointerType(used_obj->type);
            expr->flags = obj->qual & ENODE_FLAG_QUALS;
            used_obj->flags |= OBJECT_USED;
            if (used_obj->datatype == DINLINEFUNC) {
                CError_Error(ERR_ILLEGAL_USE_INLINE_FUNCTION);
            }
        }
        return CheckResult_Exact;
    } else {
        return CheckResult_NoMatch;
    }
}

ENode* CExpr_ConvertToBool(ENode* expr, Boolean isExplicit) {
    if (IS_TYPE_MEMBERPOINTER(expr->rtype)) {
        expr = CExpr_ConvertToCondition(expr);
    }

    switch (expr->rtype->type) {
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEENUM:
        case TYPEPOINTER:
            if (IS_TYPE_ENUM(expr->rtype)) {
                expr = forceintegral(expr);
            }
            switch (expr->type) {
                case EINTCONST:
                    CInt64_SetLong(&expr->data.intval, !CInt64_IsZero(&expr->data.intval));
                    break;
                case EFLOATCONST:
                    CInt64_SetLong(&expr->data.intval, !CMach_FloatIsZero(expr->data.floatval));
                    expr->type = EINTCONST;
                    break;
                default:
                    expr = makemonadicnode(expr, ELOGNOT);
                    expr->rtype = TYPE(&stbool);
                    expr = makemonadicnode(expr, ELOGNOT);
            }
            break;
        default:
            CError_Error(
                isExplicit ? ERR_ILLEGAL_EXPLICIT_CONVERSION_FROM
                           : ERR_ILLEGAL_IMPLICIT_CONVERSION_FROM,
                expr->rtype,
                expr->flags & ENODE_FLAG_QUALS,
                &stbool,
                0
            );
            expr = nullnode();
    }

    expr->rtype = TYPE(&stbool);
    return expr;
}

// NOTE: invented names: expr, type, doConvert, checkAccess
static short std_assign_check(ENode* expr, Type* type, Boolean doConvert, Boolean checkAccess) {
    short result;
    TypePointer* ptr1;
    TypePointer* ptr2;

    if (copts.cplusplus) {
        illegalimplicitconversion = FALSE;

        if ((result = iscpp_typeequal(expr->rtype, type))) {
            assign_node = expr;
            if (result == -1) {
                assign_value = 1;
                return CheckResult_Conversion;
            } else {
                return CheckResult_Exact;
            }
        }

        if (doConvert && illegalimplicitconversion) {
            CError_Error(
                ERR_ILLEGAL_IMPLICIT_CONVERSION_FROM,
                expr->rtype,
                expr->flags & ENODE_FLAG_QUALS,
                type,
                0
            );
            return CheckResult_NoMatch;
        }
    } else {
        if (is_typeequal(expr->rtype, type)) {
            assign_node = expr;
            return CheckResult_Exact;
        }
    }

    if (type == TYPE(&stbool)) {
        switch (expr->rtype->type) {
            case TYPEPOINTER:
            case TYPEMEMBERPOINTER:
                assign_value = 0;
            case TYPEINT:
            case TYPEFLOAT:
            case TYPEENUM:
                if (doConvert) {
                    assign_node = CExpr_ConvertToBool(expr, FALSE);
                }
                return CheckResult_Conversion;
            default:
                return CheckResult_NoMatch;
        }
    }

    if (IS_TYPE_ENUM(expr->rtype) && (IS_TYPE_INT(type) || IS_TYPE_FLOAT(type))) {
        result = CheckResult_Conversion;
        if (IS_TYPE_INT(type)) {
            if (TYPE_ENUM(expr->rtype)->enumtype == type) {
                result = CheckResult_Promotion;
            } else if (TYPE_INTEGRAL(TYPE_ENUM(expr->rtype)->enumtype)->integral < IT_INT) {
                switch (TYPE_INTEGRAL(type)->integral) {
                    case IT_INT:
                        if (expr->rtype->size < stsignedint.size ||
                            TYPE_ENUM(expr->rtype)->enumtype == TYPE(&stsignedshort)) {
                            result = CheckResult_Promotion;
                        }
                        break;
                    case IT_UINT:
                        if (expr->rtype->size >= stsignedint.size &&
                            TYPE_ENUM(expr->rtype)->enumtype != TYPE(&stsignedshort)) {
                            result = CheckResult_Promotion;
                        }
                        break;
                }
            }
        }
        if (doConvert) {
            expr->rtype = TYPE_ENUM(expr->rtype)->enumtype;
            assign_node = promote(expr, type);
        }
        return result;
    }

    if (IS_TYPE_INT(expr->rtype) && (IS_TYPE_INT(type) || IS_TYPE_FLOAT(type))) {
        result = CheckResult_Conversion;
        if (TYPE_INTEGRAL(expr->rtype)->integral <= IT_INT) {
            if (type == TYPE(&stsignedint) || type == TYPE(&stunsignedint)) {
                switch (TYPE_INTEGRAL(type)->integral) {
                    case IT_INT:
                        if (expr->rtype->size < stsignedint.size ||
                            type != TYPE(&stunsignedshort)) {
                            result = CheckResult_Promotion;
                        }
                        break;
                    case IT_UINT:
                        if (expr->rtype->size == stsignedint.size &&
                            type == TYPE(&stunsignedshort)) {
                            result = CheckResult_Promotion;
                        }
                        break;
                }
            }
        }

        if (doConvert && type != expr->rtype) {
            assign_node = promote(expr, type);
        } else {
            assign_node = expr;
        }
        return result;
    }

    if (IS_TYPE_FLOAT(expr->rtype) && (IS_TYPE_INT(type) || IS_TYPE_FLOAT(type))) {
        if (type == TYPE(&stdouble) &&
            (expr->rtype == TYPE(&stfloat) || expr->rtype == TYPE(&stshortdouble))) {
            result = CheckResult_Promotion;
        } else {
            result = CheckResult_Conversion;
        }

        if (doConvert && (!IS_TYPE_FLOAT(type) || type->size != expr->rtype->size)) {
            assign_node = promote(expr, type);
        } else {
            assign_node = expr;
        }
        return result;
    }

    if (IS_TYPE_POINTER_ONLY(type)) {
        if (ENODE_IS(expr, EINTCONST) &&
            CInt64_IsZero(&expr->data.intval) &&
            (IS_TYPE_INT(expr->rtype) || (!copts.cplusplus && IS_TYPE_ENUM(expr->rtype)))) {
            if (doConvert) {
                expr->rtype = TYPE(&stunsignedlong);
            }
            assign_node = expr;
            return CheckResult_Conversion;
        }
        if (ENODE_IS(expr, EOBJLIST)) {
            return std_assign_check_overload(
                expr->data.objlist.list, expr->data.objlist.templargs, type, doConvert
            );
        }
        if (IS_TYPE_POINTER_ONLY(expr->rtype)) {
            if (ENODE_IS(expr, EOBJREF) &&
                IS_TYPE_FUNC(expr->data.objref->type) &&
                (TYPE_FUNC(expr->data.objref->type)->flags & FUNC_IS_TEMPL)) {
                NameSpaceObjectList list;
                list.next = NULL;
                list.object = OBJ_BASE(expr->data.objref);
                return std_assign_check_overload(&list, NULL, type, doConvert);
            }
            if (copts.objective_c && CObjC_IsCompatibleType(expr->rtype, type)) {
                assign_value = 1;
                return CheckResult_Conversion;
            }
            ptr1 = TYPE_POINTER(expr->rtype);
            ptr2 = TYPE_POINTER(type);
            if (IS_TYPE_CLASS(ptr1->target) && IS_TYPE_CLASS(ptr2->target)) {
                short depth;
                Boolean isambig;
                BClassList* path;
                path = CClass_GetBasePath(
                    TYPE_CLASS(ptr1->target), TYPE_CLASS(ptr2->target), &depth, &isambig
                );
                if (path) {
                    assign_value = 1000 - depth;
                    if (doConvert) {
                        if (isambig) {
                            CError_Error(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
                        }
                        if (checkAccess) {
                            CClass_CheckPathAccess(path, NULL, ACCESSPUBLIC);
                        }
                        assign_node = CExpr_ClassPointerCast(path, expr, TRUE);
                    }
                    return CheckResult_Conversion;
                } else {
                    if (doConvert) {
                        if (isambig) {
                            CError_Error(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
                        } else {
                            CError_Error(
                                ERR_CANNOT_CONVERT,
                                expr->rtype,
                                expr->flags & ENODE_FLAG_QUALS,
                                type,
                                0
                            );
                        }
                    }
                    return CheckResult_NoMatch;
                }
            }
        }
    }

    if (IS_TYPE_MEMBERPOINTER(type) && !IS_TYPE_CLASS(expr->rtype)) {
        return CExpr2_MemberPointerConversion(type, expr, doConvert);
    }

    if (IS_TYPE_CLASS(expr->rtype) && IS_TYPE_CLASS(type)) {
        short depth;
        Boolean isambig;
        BClassList* path;
        path = CClass_GetBasePath(TYPE_CLASS(expr->rtype), TYPE_CLASS(type), &depth, &isambig);
        if (path) {
            assign_value = 1000 - depth;
            if (doConvert) {
                if (isambig) {
                    CError_Error(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
                }
                CClass_CheckPathAccess(path, NULL, ACCESSPUBLIC);
                assign_node = getnodeaddress(expr, FALSE);
                assign_node = CExpr_ClassPointerCast(path, assign_node, FALSE);
                assign_node = makemonadicnode(assign_node, EINDIRECT);
                assign_node->rtype = type;
            }
            return CheckResult_Conversion;
        }
    }

    if (IS_TYPE_ENUM(type)) {
        switch (expr->rtype->type) {
            case TYPEINT:
            case TYPEFLOAT:
            case TYPEENUM:
                if (!copts.cplusplus) {
                    if (doConvert) {
                        if (copts.pedantic) {
                            CError_Warning(
                                ERR_ILLEGAL_IMPLICIT_ENUM_CONVERSION_FROM,
                                expr->rtype,
                                expr->flags & ENODE_FLAG_QUALS,
                                type,
                                0
                            );
                        }
                        assign_node = do_typecast(expr, type, 0);
                        assign_node->flags = expr->flags;
                    }
                    return CheckResult_Promotion;
                } else {
                    if (doConvert) {
                        CError_Error(
                            ERR_ILLEGAL_IMPLICIT_ENUM_CONVERSION_FROM,
                            expr->rtype,
                            expr->flags & ENODE_FLAG_QUALS,
                            type,
                            0
                        );
                    }
                }
            default:
                break;
        }
    }

    return CodeGen_AssignCheck(expr, type, doConvert, checkAccess);
}

static short is_compatible_conversion(Type* a, Type* b) {
    if (IS_TYPE_REFERENCE(a)) {
        a = TYPE_POINTER(a)->target;
    }
    if (IS_TYPE_REFERENCE(b)) {
        b = TYPE_POINTER(b)->target;
    }
    return iscpp_typeequal(b, a);
}

static void CExpr_ConIteratorInit(ConIterator* iter) {
    ClassList* base;
    ConIteratorList* list;
    ConIterator* subiter;

    for (base = iter->tclass->bases; base; base = base->next) {
        if (base->base->flags & CLASS_IS_CONVERTIBLE) {
            subiter = galloc(sizeof(ConIterator));
            memclrw(subiter, sizeof(ConIterator));
            subiter->parent = iter;
            subiter->tclass = base->base;
            CExpr_ConIteratorInit(subiter);

            list = galloc(sizeof(ConIteratorList));
            memclrw(list, sizeof(ConIteratorList));
            list->iter = subiter;
            list->next = iter->children;
            iter->children = list;
        }
    }
}

void CExpr_ConversionIteratorInit(ConversionIterator* iter, TypeClass* tclass) {
    memclrw(iter, sizeof(ConversionIterator));
    if (tclass->flags & CLASS_IS_CONVERTIBLE) {
        iter->coniter = &iter->myconiter;
        iter->myconiter.tclass = tclass;
        CExpr_ConIteratorInit(&iter->myconiter);
        CScope_InitObjectIterator(&iter->objiter, tclass->nspace);
    }
}

static inline Boolean CExpr_ConversionIteratorIsHidden(ConIterator* iter, TypeFunc* tfunc) {
    CScopeObjectIterator objiter;
    ObjBase* obj;
    TypeFunc* objtfunc;

    while (iter) {
        CScope_InitObjectIterator(&objiter, iter->tclass->nspace);
        while (1) {
            if (!(obj = CScope_NextObjectIteratorObject(&objiter))) {
                break;
            }

            objtfunc = TYPE_FUNC(OBJECT(obj)->type);
            if (IS_TYPE_FUNC(objtfunc) &&
                (TYPE_FUNC(OBJECT(obj)->type)->flags & FUNC_CONVERSION) &&
                is_compatible_conversion(tfunc->functype, objtfunc->functype) &&
                (tfunc->args->qual & Q_CONST) == (objtfunc->args->qual & Q_CONST) &&
                (tfunc->qual & Q_CONST) == (objtfunc->qual & Q_CONST)) {
                return TRUE;
            }
        }
        iter = iter->parent;
    }

    return FALSE;
}

Object* CExpr_ConversionIteratorNext(ConversionIterator* iter) {
    ConIterator* ci;
    Object* obj;

    ci = iter->coniter;
    if (!ci) {
        return NULL;
    }

restart:
    if ((obj = OBJECT(CScope_NextObjectIteratorObject(&iter->objiter)))) {
        if (IS_TYPE_FUNC(obj->type) &&
            (TYPE_FUNC(obj->type)->flags & FUNC_CONVERSION) &&
            !CExpr_ConversionIteratorIsHidden(ci->parent, TYPE_FUNC(obj->type))) {
            return obj;
        }
        goto restart;
    }

    do {
        if (ci->children) {
            iter->coniter = ci->children->iter;
            ci->children = ci->children->next;
            ci = iter->coniter;
            CScope_InitObjectIterator(&iter->objiter, ci->tclass->nspace);
            goto restart;
        }
    } while ((ci = ci->parent));

    return NULL;
}

// NOTE: invented names: expr, type, qual, diagnose, allowExplicit, checkAccess
// diagnose: report the errors and build the conversion in assign_node
short user_assign_check(
    ENode* expr,
    Type* type,
    UInt32 qual,
    Boolean diagnose,
    Boolean allowExplicit,
    Boolean checkAccess
) {
    Object* best_ctor;
    Object* best_conv;
    ENode* ctor_arg;
    Boolean found_conv;
    Boolean found_ctor;
    Boolean is_ambig;
    short best_cv_matches;
    TypeFunc* ctor_tfunc;
    Type* argtype;
    Object* ctor;
    NameSpaceObjectList* ctors;
    Object* conv;
    ENode* convresult;
    short cv_matches;
    TypeFunc* conv_tfunc;
    ENodeList* conv_args;
    ENodeList* ctor_args;
    ENode* conv_ref;
    TypeMemberFunc* conv_tmethod;
    short result;
    FuncArg* arg;
    ConversionIterator iter;
    ComparisonValues stdmatch;
    ComparisonValues ctor_match;
    ComparisonValues match;
    BClassList path;
    int chk;
    UInt32 is_const;
    UInt32 is_volatile;

    CError_ASSERT(2378, copts.old_argmatch);

    memclrw(&stdmatch, sizeof(ComparisonValues));
    found_conv = FALSE;
    is_ambig = FALSE;
    found_ctor = FALSE;

    if (!type->size) {
        CDecl_CompleteType(type);
    }
    if (!expr->rtype->size) {
        CDecl_CompleteType(expr->rtype);
    }

    if (IS_TYPE_CLASS(argtype = expr->rtype)) {
        best_cv_matches = 0;
        CExpr_ConversionIteratorInit(&iter, TYPE_CLASS(argtype));
        while ((conv = CExpr_ConversionIteratorNext(&iter))) {
            conv_tfunc = TYPE_FUNC(conv->type);
            convresult = CExpr_NewENode(ETEMP);
            convresult->rtype = conv_tfunc->functype;
            if (IS_TYPE_REFERENCE(convresult->rtype)) {
                convresult->rtype = TYPE_POINTER(convresult->rtype)->target;
                if (!CParser_IsConst(convresult->rtype, conv_tfunc->qual)) {
                    convresult = makemonadicnode(convresult, EINDIRECT);
                    convresult->data.monadic->rtype = TYPE(&void_ptr);
                    convresult = makemonadicnode(convresult, EINDIRECT);
                    convresult->data.monadic->rtype = TYPE(&void_ptr);
                }
            }
            if ((result = std_assign_check(convresult, type, FALSE, checkAccess))) {
                CExpr_CalcStdAssign(
                    result, &match, conv_tfunc->functype, conv_tfunc->qual, type, qual, TRUE
                );
                CError_ASSERT(2419, (arg = conv_tfunc->args) && IS_TYPE_POINTER_ONLY(arg->type));
                chk = expr->flags;
                if (!(is_const = (conv_tfunc->args->qual & Q_CONST)) && (chk & Q_CONST) != 0) {
                    continue;
                }
                if (!(is_volatile = (conv_tfunc->args->qual & Q_VOLATILE)) &&
                    (chk & Q_VOLATILE) != 0) {
                    continue;
                }

                cv_matches = 0;
                if (is_const == (chk & Q_CONST)) {
                    cv_matches++;
                }
                if (is_volatile == (chk & Q_VOLATILE)) {
                    cv_matches++;
                }
                switch (CExpr_StdMatchCompare(&match, &stdmatch, TRUE)) {
                    case -1:
                        continue;
                    case 0:
                        if (best_conv == conv) {
                            continue;
                        }
                        if (cv_matches < best_cv_matches) {
                            continue;
                        }
                        if (cv_matches != best_cv_matches) {
                            break;
                        }
                        is_ambig = TRUE;
                        continue;
                }
                best_conv = conv;
                stdmatch = match;
                found_conv = TRUE;
                is_ambig = FALSE;
                best_cv_matches = cv_matches;
            }
        }
    }

    if (IS_TYPE_CLASS(type) && (ctors = CClass_Constructor(TYPE_CLASS(type)))) {
        memclrw(&ctor_match, sizeof(ComparisonValues));
        for (; ctors; ctors = ctors->next) {
            ctor = OBJECT(ctors->object);
            if (ctor->otype != OT_OBJECT) {
                continue;
            }
            ctor_tfunc = TYPE_FUNC(ctor->type);
            if (!IS_TYPE_FUNC(ctor_tfunc)) {
                continue;
            }
            if (!allowExplicit && (ctor->qual & Q_EXPLICIT)) {
                continue;
            }
            if (!ctor_tfunc->args) {
                continue;
            }
            if (!(arg = ctor_tfunc->args->next)) {
                continue;
            }
            if ((TYPE_CLASS(type)->flags & CLASS_HAS_VBASES) && !(arg = arg->next)) {
                continue;
            }
            if (arg == &elipsis) {
                continue;
            }
            if (arg->next && !arg->next->dexpr && arg->next != &elipsis) {
                continue;
            }

            argtype = arg->type;
            if (IS_TYPE_REFERENCE(argtype)) {
                argtype = TYPE_POINTER(argtype)->target;
                if (!CParser_IsConst(argtype, arg->qual) && !CExpr_IsLValue(expr)) {
                    continue;
                }
            }

            if ((result = std_assign_check(expr, argtype, FALSE, checkAccess))) {
                CExpr_CalcStdAssign(
                    result, &match, ctor_tfunc->functype, ctor_tfunc->qual, type, qual, FALSE
                );
                switch (CExpr_StdMatchCompare(&match, &ctor_match, TRUE)) {
                    case -1:
                    case 0:
                        continue;
                }
                ctor_arg = expr;
                ctor_match = match;
                found_ctor = TRUE;
                best_ctor = ctor;
            }
        }

        if (found_ctor) {
            if (found_conv) {
                switch (CExpr_StdMatchCompare(&stdmatch, &ctor_match, TRUE)) {
                    case -1:
                        stdmatch = ctor_match;
                        found_conv = FALSE;
                        break;
                    case 0:
                        is_ambig = TRUE;
                        break;
                }
            } else {
                stdmatch = ctor_match;
            }
        }
    }

    if (is_ambig && diagnose) {
        CError_Error(ERR_AMBIGUOUS_ACCESS_OVERLOADED_FUNCTION);
    }

    if (found_conv || found_ctor) {
        if (diagnose) {
            if (found_conv) {
                conv_tmethod = TYPE_METHOD(best_conv->type);
                CError_ASSERT(2537, conv_tmethod->flags & FUNC_METHOD);
                conv_ref = create_objectrefnode(best_conv);
                best_conv->flags |= OBJECT_USED;
                conv_args = lalloc(sizeof(ENodeList));
                conv_args->next = NULL;
                expr = getnodeaddress(expr, FALSE);
                conv_args->node = CExpr_AssignmentPromotion(
                    expr, CDecl_NewPointerType(TYPE(conv_tmethod->theclass)), expr->flags, FALSE
                );

                expr = lalloc(sizeof(ENode));
                expr->type = EFUNCCALL;
                expr->cost = 4;
                expr->rtype = conv_tmethod->functype;
                expr->flags = conv_tmethod->qual & ENODE_FLAG_QUALS;
                expr->data.funccall.funcref = conv_ref;
                expr->data.funccall.args = conv_args;
                expr->data.funccall.functype = TYPE_FUNC(best_conv->type);
                assign_node = checkreference(CExpr_AdjustFunctionCall(expr));
                if (assign_node->rtype != type) {
                    assign_node = CExpr_AssignmentPromotion(assign_node, type, qual, TRUE);
                }
                if (!IS_TYPE_REFERENCE(conv_tmethod->functype)) {
                    temp_reference_init = TRUE;
                }
            } else {
                ctor_args = lalloc(sizeof(ENodeList));
                ctor_args->next = NULL;
                ctor_args->node = ctor_arg;
                if (TYPE_CLASS(type)->flags & CLASS_HAS_VBASES) {
                    ctor_args->next = lalloc(sizeof(ENodeList));
                    ctor_args->next->node = ctor_arg;
                    ctor_args->next->next = NULL;
                    ctor_args->node = intconstnode(TYPE(&stsignedshort), 1);
                }
                path.next = NULL;
                path.type = type;
                assign_node = makemonadicnode(create_temp_node(type), EINDIRECT);
                assign_node->rtype = type;
                assign_node = CExpr_GenericFuncCall(
                    &path, assign_node, FALSE, best_ctor, NULL, NULL, ctor_args, FALSE, FALSE, TRUE
                );
                if (ENODE_IS2(assign_node, EFUNCCALL, EFUNCCALLP)) {
                    assign_node->rtype = CDecl_NewPointerType(type);
                    assign_node = makemonadicnode(assign_node, EINDIRECT);
                    assign_node->rtype = type;
                }
                temp_reference_init = TRUE;
            }
        }

        user_std_match = stdmatch;
        return CheckResult_UserConversion;
    } else {
        if (diagnose) {
            CError_Error(
                ERR_CANNOT_CONVERT, expr->rtype, expr->flags & ENODE_FLAG_QUALS, type, qual
            );
        }
        return CheckResult_NoMatch;
    }
}

ENode* CExpr_ConvertToCondition(ENode* expr) {
    switch (expr->rtype->type) {
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEPOINTER:
            return expr;
        case TYPEENUM:
            expr->rtype = TYPE_ENUM(expr->rtype)->enumtype;
            return expr;
        case TYPEMEMBERPOINTER:
            return memberpointercompare(ENOTEQU, expr, nullnode());
        case TYPECLASS:
            return CExpr_Convert(expr, TYPE(&stbool), 0, FALSE, TRUE);
        default:
            CError_Error(ERR_ILLEGAL_OPERAND_TYPE, expr->rtype, expr->flags & ENODE_FLAG_QUALS);
            return nullnode();
    }
}

ENode* CExpr_ConvertToIntegral(ENode* expr) {
    ConversionIterator iter;
    Type* found;
    Object* obj;

    switch (expr->rtype->type) {
        case TYPEINT:
        case TYPEENUM:
            return integralpromote(expr);
        case TYPECLASS:
            CDecl_CompleteType(expr->rtype);
            found = NULL;
            CExpr_ConversionIteratorInit(&iter, TYPE_CLASS(expr->rtype));
            while ((obj = CExpr_ConversionIteratorNext(&iter))) {
                if (IS_TYPE_INT_OR_ENUM(TYPE_FUNC(obj->type)->functype)) {
                    if (found) {
                        CError_Error(ERR_AMBIGUOUS_ACCESS_OVERLOADED_FUNCTION);
                        break;
                    }
                    found = TYPE_FUNC(obj->type)->functype;
                }
            }
            if (found) {
                return integralpromote(CExpr_Convert(expr, found, 0, FALSE, TRUE));
            }
            break;
        default:
            break;
    }

    CError_Error(ERR_ILLEGAL_OPERAND_TYPE, expr->rtype, expr->flags & ENODE_FLAG_QUALS);
    return nullnode();
}

void CExpr_CheckArithmConversion(ENode* expr, Type* type) {
    CInt64 val;

    if (expr->rtype == type) {
        return;
    }
    if (expr->rtype == TYPE(&stbool)) {
        return;
    }

    if (IS_TYPE_INT(expr->rtype)) {
        if (IS_TYPE_FLOAT(type)) {
            return;
        }
        CError_ASSERT(2772, IS_TYPE_INT(type));

        if (type->size > expr->rtype->size) {
            return;
        }
        if (type->size == expr->rtype->size && is_unsigned(type) == is_unsigned(expr->rtype)) {
            return;
        }

        switch (expr->type) {
            case EINTCONST:
                if (!CInt64_IsNegative(&expr->data.intval) ||
                    is_unsigned(expr->rtype) ||
                    !is_unsigned(type)) {
                    val = CExpr_IntConstConvert(type, expr->rtype, expr->data.intval);
                    val = CExpr_IntConstConvert(expr->rtype, type, val);
                    if (CInt64_Equal(val, expr->data.intval)) {
                        return;
                    }
                }
                break;
            case ELOGNOT:
            case ELESS:
            case EGREATER:
            case ELESSEQU:
            case EGREATEREQU:
            case EEQU:
            case ENOTEQU:
            case ELAND:
            case ELOR:
                return;
            default:
                break;
        }
    } else {
        if (IS_TYPE_FLOAT(type) && type->size >= expr->rtype->size) {
            return;
        }
    }

    CError_Warning(ERR_IMPLICIT_ARITHMETIC_CONVERSION_FROM, expr->rtype, 0, type, 0);
}

// NOTE: invented names: expr, copyConstant
ENode* get_address_of_temp_copy(ENode* expr, Boolean copyConstant) {
    char buf[64];
    ENode* result;
    Object* obj;
    Type* innertype;

    if (copyConstant) {
        if (ENODE_IS2(expr, EINTCONST, EFLOATCONST)) {
            obj = CParser_NewCompilerDefDataObject();
            obj->type = expr->rtype;
            obj->name = CParser_GetUniqueName();
            obj->sclass = TK_STATIC;
            if (ENODE_IS(expr, EINTCONST)) {
                innertype = expr->rtype;
                switch (innertype->type) {
                    case TYPEINT:
                        break;
                    case TYPEENUM:
                        innertype = TYPE_ENUM(innertype)->enumtype;
                        break;
                    case TYPEPOINTER:
                        innertype = TYPE(&stunsignedlong);
                        break;
                    default:
                        CError_FATAL(2857);
                }
                CMach_InitIntMem(innertype, expr->data.intval, buf);
            } else {
                CMach_InitFloatMem(expr->rtype, expr->data.floatval, buf);
            }
            CInit_DeclareData(obj, buf, NULL, obj->type->size);
            return create_objectrefnode(obj);
        }

        if (cinit_tempnodefunc == NULL) {
            result = CExpr_NewETEMPNode(expr->rtype, TRUE);
        } else {
            result = cinit_tempnodefunc(expr->rtype, FALSE);
        }
        result = makemonadicnode(result, EINDIRECT);
        result->rtype = TYPE_POINTER(result->rtype)->target;
        return makecommaexpression(makediadicnode(result, expr, EASS), result->data.monadic);
    } else {
        result = nullnode();
        CInt64_SetLong(&result->data.intval, -1);
        result->rtype = CDecl_NewPointerType(expr->rtype);
        return result;
    }
}

// NOTE: invented names: expr, type, qual, doConvert, allowExplicit, checkAccess
short assign_check(
    ENode* expr,
    Type* type,
    UInt32 qual,
    Boolean doConvert,
    Boolean allowExplicit,
    Boolean checkAccess
) {
    Type* type2;
    Boolean is_reference;
    Boolean is_array;
    short result;

    assign_value = 1000;
    is_reference = FALSE;
    is_array = FALSE;
    temp_reference_init = FALSE;

    type2 = type;
    if (IS_TYPE_REFERENCE(type) && !IS_TYPE_FUNC(TYPE_POINTER(type)->target)) {
        type2 = TYPE_POINTER(type)->target;
        is_reference = TRUE;
    }

    assign_node = expr;
    if (IS_TYPE_ARRAY(type2)) {
        is_array = TRUE;
        type2 = CDecl_NewPointerType(TYPE_POINTER(type2)->target);
    }

    if (!type2->size) {
        CDecl_CompleteType(type2);
        if (!type2->size && !is_reference) {
            if (doConvert) {
                if (IS_TYPE_CLASS(type2)) {
                    CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, type2, 0);
                } else {
                    CError_Error(
                        ERR_CANNOT_CONVERT, expr->rtype, expr->flags & ENODE_FLAG_QUALS, type2, qual
                    );
                }
            }
            return CheckResult_NoMatch;
        }
    }

    if (copts.warn_implicitconv && doConvert && !allowExplicit) {
        if (IS_TYPE_INT_OR_FLOAT(type2) && IS_TYPE_INT_OR_FLOAT(expr->rtype)) {
            CExpr_CheckArithmConversion(expr, type2);
        }
    }

    result = std_assign_check(expr, type2, doConvert, checkAccess);
    if (!result) {
        if (IS_TYPE_CLASS(expr->rtype) || IS_TYPE_CLASS(type2)) {
            result = user_assign_check(expr, type2, qual, doConvert, allowExplicit, checkAccess);
        } else if (doConvert) {
            CError_Error(
                ERR_CANNOT_CONVERT, expr->rtype, expr->flags & ENODE_FLAG_QUALS, type2, qual
            );
        }
    }

    if (is_reference && result) {
        if (doConvert) {
            if (!ENODE_IS(assign_node, EINDIRECT)) {
                if (!is_array) {
                    assign_node = CExpr_LValue(assign_node, FALSE, FALSE);
                    if (!ENODE_IS(assign_node, EINDIRECT)) {
                        assign_node = get_address_of_temp_copy(assign_node, TRUE);
                        temp_reference_init = TRUE;
                    } else {
                        assign_node = getnodeaddress(assign_node, FALSE);
                    }
                }
            } else {
                if (!CExpr_IsLValue(assign_node)) {
                    temp_reference_init = TRUE;
                }
                if (!is_array) {
                    assign_node = getnodeaddress(assign_node, FALSE);
                }
            }
        } else {
            if (!is_array &&
                !CExpr_IsLValue(assign_node) &&
                !CParser_IsConst(TYPE_POINTER(type)->target, qual)) {
                result = CheckResult_NoMatch;
            }
        }
    }

    return result;
}

Boolean CExpr_MatchCompare(Object* obj, ArgMatch* a, ArgMatch* b) {
    ObjectList* list;

    switch (CExpr_StdMatchCompare(&b->standard, &a->standard, FALSE)) {
        case -1:
            return FALSE;
        case 0:
            if (a->userconversions > b->userconversions) {
                return FALSE;
            }
            if (a->userconversions == b->userconversions) {
                switch (CExpr_StdMatchCompare(&b->userStandard, &a->userStandard, TRUE)) {
                    case -1:
                        return FALSE;
                    case 0:
                        if (a->standard.qualifierMatches > b->standard.qualifierMatches) {
                            return FALSE;
                        }
                        if (a->standard.qualifierMatches == b->standard.qualifierMatches &&
                            a->object) {
                            if (a->object->datatype == obj->datatype) {
add_it:
                                list = lalloc(sizeof(ObjectList));
                                list->next = a->list;
                                a->list = list;
                                list->object = obj;
                                return FALSE;
                            }
                            if (obj->datatype == DALIAS) {
                                return FALSE;
                            }
                            if (a->object->datatype != DALIAS) {
                                goto add_it;
                            }
                        }
                        break;
                }
            }
            break;
    }

    *a = *b;
    a->object = obj;
    return TRUE;
}

static void MatchOverloadFunc(Object* obj, FuncArg* args, ENodeList* argexprs, ArgMatch* match) {
    ArgMatch match2;

    if (IS_TYPE_FUNC(obj->type) && !(TYPE_FUNC(obj->type)->flags & FUNC_IS_TEMPL)) {
        memclrw(&match2, sizeof(ArgMatch));
        while (1) {
            if (!args || args->type == &stvoid) {
                if (!argexprs) {
                    break;
                }
                return;
            }

            if (args == &elipsis) {
                break;
            }
            if (args == &oldstyle) {
                break;
            }

            if (!argexprs) {
                if (args->dexpr) {
                    break;
                }
                return;
            }

            if (!CExpr_MatchAssign(args->type, args->qual, argexprs->node, &match2)) {
                return;
            }

            argexprs = argexprs->next;
            args = args->next;
        }

        CExpr_MatchCompare(obj, match, &match2);
    }
}

Boolean CExpr_GetFuncMatchArgs(
    Object* obj, ENodeList* argexprs, ENode* expr, FuncMatchArgs* result
) {
    ENode* intexpr;

    if (!(TYPE_FUNC(obj->type)->flags & FUNC_METHOD)) {
        result->exprs = argexprs;
        result->args = TYPE_FUNC(obj->type)->args;
        return TRUE;
    }

    if (TYPE_METHOD(obj->type)->is_static) {
        result->exprs = argexprs;
        result->args = TYPE_FUNC(obj->type)->args;
        return TRUE;
    }

    if (TYPE_FUNC(obj->type)->flags & FUNC_IS_CTOR) {
        result->exprs = argexprs;
        result->args = TYPE_FUNC(obj->type)->args->next;
        return TRUE;
    }

    if (expr) {
        intexpr = lalloc(sizeof(ENode));
        intexpr->type = EINTCONST;
        intexpr->cost = 0;
        intexpr->flags = expr->flags;
        intexpr->rtype = CDecl_NewPointerType(expr->rtype);
        intexpr->data.intval = cint64_zero;

        result->exprs = lalloc(sizeof(ENodeList));
        result->exprs->next = argexprs;
        result->exprs->node = intexpr;

        if (obj->datatype == DALIAS) {
            result->args = lalloc(sizeof(FuncArg));
            *result->args = *TYPE_FUNC(obj->type)->args;
            result->args->type = CDecl_NewPointerType(expr->rtype);
        } else {
            result->args = TYPE_FUNC(obj->type)->args;
        }

        return TRUE;
    }

    return FALSE;
}

static NameSpaceObjectList* CExpr_CopyNameSpaceObjectList(NameSpaceObjectList* list) {
    NameSpaceObjectList* first;
    NameSpaceObjectList* work;

    first = work = lalloc(sizeof(NameSpaceObjectList));
    while (1) {
        work->object = list->object;
        list = list->next;
        if (!list) {
            work->next = NULL;
            break;
        } else {
            work->next = lalloc(sizeof(NameSpaceObjectList));
            work = work->next;
        }
    }
    return first;
}

// NOTE: invented names: list, templargs, argexprs, match, expr, excludeExplicit
static void CExpr_MatchArgList(
    NameSpaceObjectList* list,
    TemplArg* templargs,
    ENodeList* argexprs,
    ArgMatch* match,
    ENode* expr,
    Boolean excludeExplicit
) {
    NameSpaceObjectList* copied_list;
    NameSpaceObjectList* scan_list;
    Object* obj;
    ENodeList* scan_expr;
    Boolean is_template;
    FuncMatchArgs fma;

    if (!copts.old_argmatch) {
        CExpr_FuncArgMatch(
            CExpr_CopyNameSpaceObjectList(list), templargs, argexprs, match, expr, excludeExplicit
        );
    } else {
        copied_list = CExpr_CopyNameSpaceObjectList(list);

        for (scan_expr = argexprs; scan_expr; scan_expr = scan_expr->next) {
            CDecl_CompleteType(scan_expr->node->rtype);
        }

        scan_list = copied_list;
        is_template = FALSE;
        for (; scan_list; scan_list = scan_list->next) {
            obj = OBJECT(scan_list->object);
            if (obj->otype != OT_OBJECT) {
                continue;
            }
            if (IS_TYPE_FUNC(obj->type) && (!excludeExplicit || !(obj->qual & Q_EXPLICIT))) {
                if (!(TYPE_FUNC(obj->type)->flags & FUNC_IS_TEMPL)) {
                    if (CExpr_GetFuncMatchArgs(obj, argexprs, expr, &fma)) {
                        MatchOverloadFunc(obj, fma.args, fma.exprs, match);
                    }
                } else {
                    is_template = TRUE;
                }
            }
        }

        if (is_template) {
            if (!match->object ||
                match->standard.promotions ||
                match->standard.conversions ||
                match->userconversions) {
                CTempl_FuncMatch(copied_list, templargs, argexprs, match, expr);
            }
        }
    }
}

ENode* CExpr_GetDefaultArgument(ENode* funcexpr, FuncArg* arg) {
    ENode* expr; // NOTE: invented name

    if (CTemplTool_IsTemplateArgumentDependentExpression(arg->dexpr)) {
        CError_ASSERT(3264, ENODE_IS(funcexpr, EOBJREF));
        expr = CTemplTool_DeduceDefaultArg(
            funcexpr->data.objref, CInline_CopyExpression(arg->dexpr, CopyModeLocal)
        );
        expr = argumentpromotion(expr, arg->type, arg->qual, TRUE);
    } else {
        expr = CInline_CopyExpression(arg->dexpr, CopyModeLocal);
    }

    return expr;
}

static ENode* CExpr_GenericCall(
    ENode* funcexpr, ENodeList* argexprs, TypeFunc* tfunc, FuncArg* args
) {
    ENodeList* list;
    ENode* callexpr;

    while (args) {
        if (args->dexpr) {
            if (argexprs) {
                list = argexprs;
                while (list->next) {
                    list = list->next;
                }
                list->next = lalloc(sizeof(ENodeList));
                list = list->next;
            } else {
                list = argexprs = lalloc(sizeof(ENodeList));
            }
            list->next = NULL;
            list->node = CExpr_GetDefaultArgument(funcexpr, args);
        }
        args = args->next;
    }

    callexpr = lalloc(sizeof(ENode));
    callexpr->type = EFUNCCALL;
    callexpr->cost = 4;
    callexpr->rtype = tfunc->functype;
    callexpr->flags = tfunc->qual & ENODE_FLAG_QUALS;
    callexpr->data.funccall.funcref = funcexpr;
    callexpr->data.funccall.funcref->rtype = CDecl_NewPointerType(TYPE(tfunc));
    callexpr->data.funccall.args = argexprs;
    callexpr->data.funccall.functype = tfunc;
    funcexpr->data.objref->flags |= OBJECT_USED;
    return CExpr_AdjustFunctionCall(callexpr);
}

static Boolean CExpr_IsObjrefPlusX(ENode* expr) {
    Type* type;

    if (ENODE_IS(expr, EOBJREF)) {
        type = expr->data.objref->type;
        while (IS_TYPE_ARRAY(type)) {
            type = TYPE_POINTER(type)->target;
        }
        return IS_TYPE_CLASS(type);
    }

    if (ENODE_IS2(expr, EADD, ESUB)) {
        if (CExpr_IsObjrefPlusX(expr->data.diadic.left)) {
            return TRUE;
        }
        if (CExpr_IsObjrefPlusX(expr->data.diadic.right)) {
            return TRUE;
        }
    }

    return FALSE;
}

// (tclass is unused; the original's caller passes it)
static Boolean CExpr_IsStaticType(ENode* expr, TypeClass* tclass) {
    return ENODE_IS(expr, EINDIRECT) && CExpr_IsObjrefPlusX(expr->data.monadic);
}

// NOTE: invented names: expr, toEllipsis
ENode* CExpr_VarArgPromotion(ENode* expr, Boolean toEllipsis) {
    if (!copts.old_argmatch) {
        expr = pointer_generation(expr);
    }

    switch (expr->rtype->type) {
        case TYPEVOID:
        case TYPEFUNC:
            CError_Error(ERR_CANNOT_PASS_VOID_FUNCTION_PARAMETER);
            expr = nullnode();
            break;
        case TYPEINT:
        case TYPEENUM:
            expr = integralpromote(expr);
            break;
        case TYPEFLOAT:
            if (TYPE_INTEGRAL(expr->rtype)->integral < IT_DOUBLE) {
                expr = promote(expr, TYPE(&stdouble));
            }
            break;
        case TYPECLASS:
            expr = classargument(expr);
            break;
        default:
            break;
    }

    // an argument passed through a hidden pointer (a struct, or a soft float)
    if (CMach_PassResultInHiddenArg(expr->rtype)) {
        expr = argumentpromotion(expr, expr->rtype, expr->flags, TRUE);
    }

    if (!toEllipsis && copts.warn_largeargs) {
        if ((IS_TYPE_INT(expr->rtype) && TYPE_INTEGRAL(expr->rtype)->integral >= IT_LONGLONG) ||
            IS_TYPE_FLOAT(expr->rtype)) {
            CError_Warning(ERR_ASSIGNING_NON_INT_NUMERIC_VALUE_UNPROTOTYPED);
        }
    }

    return expr;
}

// NOTE: invented names: path, funcexpr, isQualified, obj, nsol, templargs, nodes, excludeExplicit,
// isAmbiguous, checkAccess
ENode* CExpr_GenericFuncCall(
    BClassList* path,
    ENode* funcexpr,
    Boolean isQualified,
    Object* obj,
    NameSpaceObjectList* nsol,
    TemplArg* templargs,
    ENodeList* nodes,
    Boolean excludeExplicit,
    Boolean isAmbiguous,
    Boolean checkAccess
) {
    ENodeList* scan_expr;
    TypeFunc* tfunc;
    AccessType access;
    FuncArg* scan_arg;
    BClassList* buildpath;
    ENode* objexpr;
    BClassList* pathcopy;
    Boolean had_alias;
    ArgMatch match;
    NameSpaceObjectList my_list;

    memclrw(&match, sizeof(ArgMatch));

    if (!obj || IS_TEMPL_FUNC(obj->type)) {
        if (!funcexpr && cscope_currentfunc && cscope_currentclass && cscope_is_member_func) {
            funcexpr = CClass_CreateThisSelfExpr();
            if (funcexpr) {
                funcexpr = makemonadicnode(funcexpr, EINDIRECT);
                funcexpr->rtype = TYPE(cscope_currentclass);
            }
        }

        if (obj) {
            my_list.next = NULL;
            my_list.object = OBJ_BASE(obj);
            nsol = &my_list;
        }

        CExpr_MatchArgList(nsol, templargs, nodes, &match, funcexpr, excludeExplicit);
        if (!match.object) {
            CError_ErrorFuncCall(ERR_FUNCTION_CALL_STAR_DOES_NOT_MATCH, nsol, nodes);
            return nullnode();
        }
        if (match.list) {
            CError_OverloadedFunctionError(match.object, match.list);
        }
        obj = match.object;
    }

    objexpr = create_objectrefnode(obj);
    tfunc = TYPE_FUNC(obj->type);
    if (!IS_TYPE_FUNC(tfunc)) {
        CError_Error(ERR_CALL_NON_FUNCTION);
        return nullnode();
    }

    if (IS_TYPEFUNC_METHOD(tfunc) && !TYPE_METHOD(tfunc)->is_static) {
        had_alias = FALSE;
        buildpath = NULL;
        access = obj->access;
        while (obj->datatype == DALIAS) {
            if (buildpath) {
                buildpath =
                    CClass_AppendPath(buildpath, CClass_GetPathCopy(obj->u.alias.member, FALSE));
            } else {
                buildpath = CClass_GetPathCopy(obj->u.alias.member, FALSE);
            }
            obj = obj->u.alias.object;
            objexpr = create_objectrefnode(obj);
            had_alias = TRUE;
        }
        if (isAmbiguous) {
            CError_Error(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
        }

        if (TYPE_METHOD(tfunc)->theclass->sominfo &&
            (!(obj->qual & Q_INLINE) || (obj->datatype == DVFUNC && !isQualified))) {
            pathcopy = CClass_GetPathCopy(path, FALSE);
            funcexpr =
                CExpr_GetClassAccessNode(path, buildpath, funcexpr, obj, access, checkAccess);
            if (!funcexpr) {
                return nullnode();
            }
            objexpr = CSOM_MethodAccess(pathcopy, obj, isQualified);
        } else {
            if (obj->datatype == DVFUNC) {
                if (isQualified ||
                    (!copts.always_vdispatch &&
                     !had_alias &&
                     funcexpr &&
                     CExpr_IsStaticType(funcexpr, TYPE_METHOD(tfunc)->theclass))) {
                    objexpr->flags |= ENODE_FLAG_NONVIRTUAL;
                }
            }
            funcexpr =
                CExpr_GetClassAccessNode(path, buildpath, funcexpr, obj, access, checkAccess);
            if (!funcexpr) {
                return nullnode();
            }
        }

        if ((tfunc->flags & FUNC_PURE) &&
            cscope_currentfunc &&
            (TYPE_FUNC(cscope_currentfunc->type)->flags & (FUNC_IS_CTOR | FUNC_IS_DTOR)) &&
            cscope_currentclass == TYPE_METHOD(tfunc)->theclass &&
            ENODE_IS(funcexpr, EINDIRECT) &&
            ENODE_IS(funcexpr->data.monadic, EINDIRECT) &&
            ENODE_IS(funcexpr->data.monadic->data.monadic, EOBJREF) &&
            funcexpr->data.monadic->data.monadic->data.objref->name == this_name_node &&
            !(objexpr->flags & ENODE_FLAG_NONVIRTUAL)) {
            CError_Warning(ERR_ILLEGAL_USE_PURE_FUNCTION);
        }

        scan_expr = lalloc(sizeof(ENodeList));
        scan_expr->next = nodes;
        scan_expr->node = funcexpr->data.monadic;
        if (ENODE_IS(scan_expr->node, EOBJREF)) {
            scan_expr->node->data.objref->flags |= OBJECT_REFERENCED;
        }

        if (((funcexpr->flags & Q_CONST) && !(tfunc->args->qual & Q_CONST)) ||
            ((funcexpr->flags & Q_VOLATILE) && !(tfunc->args->qual & Q_VOLATILE))) {
            if (!(tfunc->flags & (FUNC_IS_CTOR | FUNC_IS_DTOR))) {
                CError_Error(ERR_CANNOT_PASS_CONST_VOLATILE_DATA_OBJECT);
            }
        }

        nodes = scan_expr;
        scan_expr = scan_expr->next;
        scan_arg = tfunc->args->next;
    } else {
        if (checkAccess && obj->access != ACCESSPROTECTED) {
            CClass_CheckObjectAccess(path, obj);
        }

        scan_arg = tfunc->args;
        scan_expr = nodes;
        if (tfunc->flags & FUNC_METHOD) {
            CError_ASSERT(3599, TYPE_METHOD(tfunc)->theclass->sominfo == NULL);
        }
    }

    while (scan_expr) {
        if (scan_arg && scan_arg != &elipsis && scan_arg != &oldstyle) {
            scan_expr->node =
                argumentpromotion(scan_expr->node, scan_arg->type, scan_arg->qual, TRUE);
            scan_arg = scan_arg->next;
        } else {
            if (!scan_arg) {
                my_list.next = NULL;
                my_list.object = OBJ_BASE(obj);
                CError_ErrorFuncCall(ERR_FUNCTION_CALL_STAR_DOES_NOT_MATCH, &my_list, nodes);
            }
            scan_expr->node = CExpr_VarArgPromotion(scan_expr->node, scan_arg == &elipsis);
        }
        scan_expr = scan_expr->next;
    }

    if (scan_arg) {
        if (scan_arg != &elipsis && scan_arg != &oldstyle) {
            if (!scan_arg->dexpr) {
                my_list.next = NULL;
                my_list.object = OBJ_BASE(obj);
                CError_ErrorFuncCall(ERR_FUNCTION_CALL_STAR_DOES_NOT_MATCH, &my_list, nodes);
                scan_arg = NULL;
            }
        } else {
            scan_arg = NULL;
        }
    }

    return CExpr_GenericCall(objexpr, nodes, tfunc, scan_arg);
}

ENode* CExpr_GenericPtmfCall(Object* obj, TypeFunc* tfunc, ENodeList* arg_exprs) {
    ENodeList* scan_expr;
    FuncArg* arg;

    scan_expr = arg_exprs;
    arg = tfunc->args;
    while (scan_expr) {
        if (!arg) {
            CError_Error(ERR_FUNCTION_CALL_DOES_NOT_MATCH_PROTOTYPE);
            return nullnode();
        }

        if (arg != &elipsis && arg != &oldstyle) {
            scan_expr->node = argumentpromotion(scan_expr->node, arg->type, arg->qual, TRUE);
            arg = arg->next;
        } else {
            scan_expr->node = CExpr_VarArgPromotion(scan_expr->node, arg == &elipsis);
        }
        scan_expr = scan_expr->next;
    }

    if (arg) {
        if (arg != &elipsis && arg != &oldstyle) {
            if (!arg->dexpr) {
                CError_Error(ERR_FUNCTION_CALL_DOES_NOT_MATCH_PROTOTYPE);
                arg = NULL;
            }
        } else {
            arg = NULL;
        }
    }

    return CExpr_GenericCall(create_objectrefnode(obj), arg_exprs, tfunc, arg);
}

static ENode* CExpr_ConvertEMember(ENode* expr) {
    ENode* result;

    if (expr->data.emember->list->next || expr->data.emember->templargs) {
        result = CExpr_NewENode(EOBJLIST);
        result->rtype = OBJECT(expr->data.emember->list->object)->type;
        result->data.objlist.list = expr->data.emember->list;
        result->data.objlist.templargs = expr->data.emember->templargs;
        return result;
    }

    if (expr->data.emember->list->object->otype != OT_OBJECT) {
        return NULL;
    }
    return CExpr_MakeObjRefNode(OBJECT(expr->data.emember->list->object), TRUE);
}

ENode* CExpr_MakeFunctionCall(ENode* funcexpr, ENodeList* arg_exprs) {
    ENode* expr;
    TypeFunc* tfunc;
    BClassList* save_path;
    ENode* save_expr;
    Boolean save_is_qualified;
    Boolean save_isambig;

    ENodeList* scan_expr;
    Boolean has_varargs;
    FuncArg* scan_arg;

    if (ENODE_IS(funcexpr, EOBJLIST) && funcexpr->data.objlist.name) {
        funcexpr->data.objlist.list = CScope_ArgumentDependentNameLookup(
            funcexpr->data.objlist.list, funcexpr->data.objlist.name, arg_exprs, FALSE
        );
        if (!funcexpr->data.objlist.list) {
            CError_Error(
                ERR_UNDEFINED_IDENTIFIER, CError_GetNameString(NULL, funcexpr->data.objlist.name)
            );
            return nullnode();
        }

        if (funcexpr->data.objlist.list->object->otype == OT_OBJECT &&
            (TYPE_FUNC(OBJECT(funcexpr->data.objlist.list->object)->type)->flags &
             FUNC_INTRINSIC) &&
            (expr = CodeGen_HandleIntrinsicCall(
                 OBJECT(funcexpr->data.objlist.list->object), arg_exprs
             ))) {
            return expr;
        }

        return CExpr_GenericFuncCall(
            NULL,
            NULL,
            FALSE,
            NULL,
            funcexpr->data.objlist.list,
            funcexpr->data.objlist.templargs,
            arg_exprs,
            FALSE,
            FALSE,
            TRUE
        );
    }

    if (ENODE_IS(funcexpr, EMEMBER)) {
        save_path = funcexpr->data.emember->path;
        save_expr = funcexpr->data.emember->expr;
        save_is_qualified = funcexpr->data.emember->is_qualified;
        save_isambig = funcexpr->data.emember->isambig;
        funcexpr = CExpr_ConvertEMember(funcexpr);
        if (!funcexpr) {
            CError_Error(ERR_CALL_NON_FUNCTION);
            return nullnode();
        }
    } else {
        save_path = NULL;
        save_expr = NULL;
        save_is_qualified = FALSE;
        save_isambig = FALSE;
    }

    if (ENODE_IS(funcexpr, EOBJREF) &&
        IS_TYPE_FUNC(funcexpr->data.objref->type) &&
        (TYPE_FUNC(funcexpr->data.objref->type)->flags & FUNC_INTRINSIC)) {
        if (!(expr = CodeGen_HandleIntrinsicCall(funcexpr->data.objref, arg_exprs))) {
            expr = CExpr_GenericFuncCall(
                save_path,
                save_expr,
                save_is_qualified,
                funcexpr->data.objref,
                NULL,
                NULL,
                arg_exprs,
                FALSE,
                save_isambig,
                TRUE
            );
        }
        return expr;
    }

    if (ENODE_IS(funcexpr, EOBJLIST)) {
        return CExpr_GenericFuncCall(
            save_path,
            save_expr,
            save_is_qualified,
            NULL,
            funcexpr->data.objlist.list,
            funcexpr->data.objlist.templargs,
            arg_exprs,
            FALSE,
            save_isambig,
            TRUE
        );
    }

    if (!IS_TYPE_POINTER_ONLY(funcexpr->rtype) ||
        !IS_TYPE_FUNC((tfunc = TYPE_FUNC(TYPE_POINTER(funcexpr->rtype)->target)))) {
        CError_Error(ERR_CALL_NON_FUNCTION);
        return nullnode();
    }

    if (ENODE_IS(funcexpr, EOBJREF)) {
        return CExpr_GenericFuncCall(
            save_path,
            save_expr,
            save_is_qualified,
            funcexpr->data.objref,
            NULL,
            NULL,
            arg_exprs,
            FALSE,
            save_isambig,
            TRUE
        );
    }

    scan_expr = arg_exprs;
    scan_arg = tfunc->args;
    has_varargs = FALSE;
    while (scan_expr) {
        if (!has_varargs) {
            if (!scan_arg) {
                CError_Error(ERR_FUNCTION_CALL_DOES_NOT_MATCH_PROTOTYPE);
                return nullnode();
            }
            if (scan_arg == &elipsis || scan_arg == &oldstyle) {
                scan_expr->node = CExpr_VarArgPromotion(scan_expr->node, scan_arg == &elipsis);
                has_varargs = TRUE;
            } else {
                scan_expr->node =
                    argumentpromotion(scan_expr->node, scan_arg->type, scan_arg->qual, TRUE);
                scan_arg = scan_arg->next;
            }
        } else {
            scan_expr->node = CExpr_VarArgPromotion(scan_expr->node, FALSE);
        }
        scan_expr = scan_expr->next;
    }

    if (!has_varargs && scan_arg && scan_arg != &elipsis && scan_arg != &oldstyle) {
        do {
            if (!scan_arg->dexpr) {
                CError_Error(ERR_FUNCTION_CALL_DOES_NOT_MATCH_PROTOTYPE);
                return nullnode();
            }

            if (arg_exprs) {
                scan_expr = arg_exprs;
                while (scan_expr->next) {
                    scan_expr = scan_expr->next;
                }
                scan_expr->next = lalloc(sizeof(ENodeList));
                scan_expr = scan_expr->next;
            } else {
                scan_expr = lalloc(sizeof(ENodeList));
                arg_exprs = scan_expr;
            }

            scan_expr->next = NULL;
            scan_expr->node = CExpr_GetDefaultArgument(funcexpr, scan_arg);
        } while ((scan_arg = scan_arg->next) && scan_arg != &elipsis && scan_arg != &oldstyle);
    }

    expr = CExpr_NewENode(EFUNCCALL);
    expr->cost = 4;
    expr->rtype = tfunc->functype;
    expr->flags = tfunc->qual & ENODE_FLAG_QUALS;
    expr->data.funccall.funcref = funcexpr;
    expr->data.funccall.args = arg_exprs;
    expr->data.funccall.functype = tfunc;
    return CExpr_AdjustFunctionCall(expr);
}

static Boolean accept_conversion_type(Type* type, short mode) {
    switch (mode) {
        case 0:
            return IS_TYPE_INT(type);
        case 1:
            return IS_TYPE_INT_OR_FLOAT(type);
        case 2:
            return IS_TYPE_INT_OR_FLOAT(type) || IS_TYPE_POINTER_ONLY(type);
        case 3:
            return IS_TYPE_POINTER_ONLY(type);
        default:
            CError_FATAL(3912);
            return FALSE;
    }
}

static ENode* CExpr_OperatorConversion(ENode* expr, Type* type, UInt32 qual) {
    if (IS_TYPE_CLASS(expr->rtype) && type) {
        if (user_assign_check(expr, type, qual, TRUE, FALSE, TRUE)) {
            return assign_node;
        }
        CError_Error(ERR_ILLEGAL_OPERAND);
    }
    return expr;
}

static Boolean wild_conversion_check(ENode* left, ENode* right, Conversion* conv) {
    ConversionIterator iter;
    Object* obj;

    Type* check;
    Type* left_type;
    Type* right_type;

    left_type = right_type = NULL;
    if (IS_TYPE_CLASS(left->rtype)) {
        CExpr_ConversionIteratorInit(&iter, TYPE_CLASS(left->rtype));
        while ((obj = CExpr_ConversionIteratorNext(&iter))) {
            check = TYPE_FUNC(obj->type)->functype;
            if (accept_conversion_type(check, 2)) {
                while ((obj = CExpr_ConversionIteratorNext(&iter))) {
                    if (accept_conversion_type(TYPE_FUNC(obj->type)->functype, 2)) {
                        CError_Error(ERR_AMBIGUOUS_ACCESS_OVERLOADED_FUNCTION);
                    }
                }
                left_type = check;
                goto found_left;
            }
        }
        return FALSE;
    }
found_left:
    if (IS_TYPE_CLASS(right->rtype)) {
        CExpr_ConversionIteratorInit(&iter, TYPE_CLASS(right->rtype));
        while ((obj = CExpr_ConversionIteratorNext(&iter))) {
            check = TYPE_FUNC(obj->type)->functype;
            if (accept_conversion_type(check, 2)) {
                while ((obj = CExpr_ConversionIteratorNext(&iter))) {
                    if (accept_conversion_type(TYPE_FUNC(obj->type)->functype, 2)) {
                        CError_Error(ERR_AMBIGUOUS_ACCESS_OVERLOADED_FUNCTION);
                    }
                }
                right_type = check;
                goto found_right;
            }
        }
        return FALSE;
    }
found_right:
    conv->expression = NULL;
    conv->left = CExpr_OperatorConversion(left, left_type, 0);
    conv->right = CExpr_OperatorConversion(right, right_type, 0);
    return TRUE;
}

static Boolean monadic_conversion_check(ENode* expr, short which, Conversion* conv) {
    ConversionIterator iter;
    Object* obj;
    Type* check;

    if (!IS_TYPE_CLASS(expr->rtype)) {
        return FALSE;
    }

    if ((int)which == 4) {
        which = 2;
    } else if ((int)which == 5) {
        which = 1;
    }

    CExpr_ConversionIteratorInit(&iter, TYPE_CLASS(expr->rtype));
    while ((obj = CExpr_ConversionIteratorNext(&iter))) {
        check = TYPE_FUNC(obj->type)->functype;
        if (accept_conversion_type(check, which)) {
            while ((obj = CExpr_ConversionIteratorNext(&iter))) {
                if (accept_conversion_type(TYPE_FUNC(obj->type)->functype, which)) {
                    CError_Error(ERR_AMBIGUOUS_ACCESS_OVERLOADED_FUNCTION);
                }
            }

            conv->expression = NULL;
            conv->left = CExpr_OperatorConversion(expr, check, 0);
            conv->right = NULL;
            return TRUE;
        }
    }

    return FALSE;
}

static Boolean is_legal_type_combination(Type* left, Type* right, short mode) {
    short left_type;
    short right_type;

    if (IS_TYPE_ENUM(left)) {
        left = TYPE_ENUM(left)->enumtype;
    }
    if (IS_TYPE_ENUM(right)) {
        right = TYPE_ENUM(right)->enumtype;
    }

    if (IS_TYPE_REFERENCE(left)) {
        left = TYPE_POINTER(left)->target;
    }
    if (IS_TYPE_REFERENCE(right)) {
        right = TYPE_POINTER(right)->target;
    }

    left_type = left->type;
    right_type = right->type;
    switch (mode) {
        case 3:
            if (left_type != TYPEPOINTER || right_type != TYPEPOINTER) {
                return FALSE;
            }
use_cpp_typeequal:
            diadic_arg1.type = left;
            diadic_arg2.type = right;
            return iscpp_typeequal(left, right);
        case 7:
            if ((left_type == TYPEPOINTER && right_type == TYPEINT) ||
                (left_type == TYPEINT && right_type == TYPEPOINTER)) {
                diadic_arg1.type = left;
                diadic_arg2.type = right;
                return TRUE;
            }
            return FALSE;
        case 6:
            if ((left_type == TYPEPOINTER && right_type == TYPEINT) ||
                (left_type == TYPEINT && right_type == TYPEPOINTER)) {
                diadic_arg1.type = left;
                diadic_arg2.type = right;
                return TRUE;
            }
        case 2:
            if (left_type == TYPEPOINTER || right_type == TYPEPOINTER) {
                goto use_cpp_typeequal;
            }
        case 1:
mode1:
            if (left_type == TYPEFLOAT || right_type == TYPEFLOAT) {
                if (left_type == TYPEFLOAT) {
                    if (right_type == TYPEFLOAT) {
                        if (TYPE_INTEGRAL(right)->integral > TYPE_INTEGRAL(left)->integral) {
                            left = right;
                        }
                        diadic_arg1.type = diadic_arg2.type = left;
                        return TRUE;
                    }
                    if (right_type != TYPEINT) {
                        return FALSE;
                    }
                    diadic_arg1.type = diadic_arg2.type = left;
                    return TRUE;
                }
                if (left_type != TYPEINT) {
                    return FALSE;
                }
                diadic_arg1.type = diadic_arg2.type = right;
                return TRUE;
            }
        case 0:
            if (left_type != TYPEINT || right_type != TYPEINT) {
                return FALSE;
            }
            if (TYPE_INTEGRAL(right)->integral > TYPE_INTEGRAL(left)->integral) {
                left = right;
            }
            if (TYPE_INTEGRAL(left)->integral < IT_INT) {
                left = TYPE(&stsignedint);
            }
            diadic_arg1.type = diadic_arg2.type = left;
            return TRUE;
        case 4:
            if (left_type == TYPEPOINTER) {
                if (right_type != TYPEINT) {
                    return FALSE;
                }
                diadic_arg1.type = left;
                diadic_arg2.type = right;
                return TRUE;
            }
            if (right_type == TYPEPOINTER) {
                if (left_type != TYPEINT) {
                    return FALSE;
                }
                diadic_arg1.type = left;
                diadic_arg2.type = right;
                return TRUE;
            }
            goto mode1;
        case 5:
            if (left_type == TYPEPOINTER) {
                if (right_type == TYPEPOINTER) {
                    goto use_cpp_typeequal;
                }
                if (right_type != TYPEINT) {
                    return FALSE;
                }
                diadic_arg1.type = left;
                diadic_arg2.type = right;
                return TRUE;
            }
            goto mode1;
        default:
            CError_FATAL(4132);
            return FALSE;
    }
}

static void match_class_type_conversion(
    ArgMatch* match, ENode* left, ENode* right, ENodeList* list, short which
) {
    ConversionIterator iter;
    Object* obj;

    if (which == 6) {
        if (!ENODE_IS(right, EINTCONST) ||
            !CInt64_IsZero(&right->data.intval) ||
            !IS_TYPE_INT(right->rtype)) {
            which = 2;
        }
    }

    CExpr_ConversionIteratorInit(&iter, TYPE_CLASS(left->rtype));
    while ((obj = CExpr_ConversionIteratorNext(&iter))) {
        if (is_legal_type_combination(TYPE_FUNC(obj->type)->functype, right->rtype, which)) {
            MatchOverloadFunc(obj, &diadic_arg1, list, match);
            if (match->object == obj) {
                cexpr_left_conversion_type = diadic_arg1.type;
                cexpr_right_conversion_type = diadic_arg2.type;
            }
        }
    }
}

static void match_type_class_conversion(
    ArgMatch* match, ENode* left, ENode* right, ENodeList* list, short which
) {
    ConversionIterator iter;
    Object* obj;

    if (which == 6) {
        if (!ENODE_IS(left, EINTCONST) ||
            !CInt64_IsZero(&left->data.intval) ||
            !IS_TYPE_INT(left->rtype)) {
            which = 2;
        }
    }

    CExpr_ConversionIteratorInit(&iter, TYPE_CLASS(right->rtype));
    while ((obj = CExpr_ConversionIteratorNext(&iter))) {
        if (is_legal_type_combination(left->rtype, TYPE_FUNC(obj->type)->functype, which)) {
            MatchOverloadFunc(obj, &diadic_arg1, list, match);
            if (match->object == obj) {
                cexpr_left_conversion_type = diadic_arg1.type;
                cexpr_right_conversion_type = diadic_arg2.type;
            }
        }
    }
}

static void match_class_class_conversion(
    ArgMatch* match, ENode* left, ENode* right, ENodeList* list, short which
) {
    ConversionIterator iter_left;
    ConversionIterator iter_right;
    Object* obj_left;
    Object* obj_right;

    if (which == 6) {
        which = 2;
    }

    CExpr_ConversionIteratorInit(&iter_left, TYPE_CLASS(left->rtype));
    while ((obj_left = CExpr_ConversionIteratorNext(&iter_left))) {
        CExpr_ConversionIteratorInit(&iter_right, TYPE_CLASS(right->rtype));
        while ((obj_right = CExpr_ConversionIteratorNext(&iter_right))) {
            if (is_legal_type_combination(
                    TYPE_FUNC(obj_left->type)->functype, TYPE_FUNC(obj_right->type)->functype, which
                )) {
                MatchOverloadFunc(obj_left, &diadic_arg1, list, match);
                if (match->object == obj_left) {
                    cexpr_left_conversion_type = diadic_arg1.type;
                    cexpr_right_conversion_type = diadic_arg2.type;
                }
            }
        }
    }
}

Boolean CExpr_CheckOperatorConversion(
    short token, ENode* left, ENode* right, ENodeList* list, Conversion* conv
) {
    ArgMatch match;
    short which;

    switch (token) {
        case '*':
            if (!right) {
                which = 3;
                break;
            }
        case '/':
            which = 1;
            break;
        case '&':
            if (!right) {
                return FALSE;
            }
        case '%':
        case '^':
        case '|':
        case '~':
        case TK_SHL:
        case TK_SHR:
            which = 0;
            break;
        case '[':
            which = 7;
            break;
        case '+':
            which = 4;
            break;
        case '-':
            which = 5;
            break;
        case '!':
        case ':':
        case '<':
        case '>':
        case TK_LESS_EQUAL:
        case TK_GREATER_EQUAL:
            which = 2;
            break;
        case TK_LOGICAL_EQ:
        case TK_LOGICAL_NE:
            which = 6;
            break;
        case TK_LOGICAL_OR:
        case TK_LOGICAL_AND:
            return wild_conversion_check(left, right, conv);
        default:
            return FALSE;
    }

    if (!right) {
        return monadic_conversion_check(left, which, conv);
    }

    cexpr_left_conversion_type = cexpr_right_conversion_type = NULL;
    memclrw(&match, sizeof(ArgMatch));

    if (IS_TYPE_CLASS(left->rtype)) {
        if (IS_TYPE_CLASS(right->rtype)) {
            match_class_class_conversion(&match, left, right, list, which);
        } else {
            match_class_type_conversion(&match, left, right, list, which);
        }
    } else {
        if (IS_TYPE_CLASS(right->rtype)) {
            match_type_class_conversion(&match, left, right, list, which);
        } else {
            return FALSE;
        }
    }

    if (!match.object) {
        return FALSE;
    }
    if (match.list) {
        CError_OverloadedFunctionError(match.object, match.list);
    }

    conv->expression = NULL;
    conv->left = CExpr_OperatorConversion(left, cexpr_left_conversion_type, 0);
    conv->right = CExpr_OperatorConversion(right, cexpr_right_conversion_type, 0);
    return TRUE;
}

Boolean CExpr_CheckOperator(short token, ENode* left, ENode* right, Conversion* conv) {
    HashNameNode* name;
    NameResult pr;
    NameResult pr2;
    ENode* expr;
    Object* obj;
    ENodeList* nodes;
    ArgMatch match;
    BClassList* path;
    EMemberInfo* member;
    Object* prev_obj;
    NameSpaceObjectList nonmember_list;
    NameSpaceObjectList member_list;

    if (!copts.old_argmatch) {
        if (token == '(') {
            CDecl_CompleteType(left->rtype);
            if (IS_TYPE_CLASS(left->rtype) &&
                CScope_FindClassMemberObject(
                    TYPE_CLASS(left->rtype), &pr, CMangler_OperatorName(token)
                )) {
                if (pr.objects ||
                    (pr.object &&
                     pr.object->otype == OT_OBJECT &&
                     IS_TYPE_FUNC(OBJECT(pr.object)->type))) {
                    member = lalloc(sizeof(EMemberInfo));
                    memclrw(member, sizeof(EMemberInfo));
                    member->path = pr.basePath;
                    member->expr = left;
                    member->is_qualified = pr.is_qualified;
                    if (!pr.objects) {
                        member->list = galloc(sizeof(NameSpaceObjectList));
                        member->list->next = NULL;
                        member->list->object = pr.object;
                    } else {
                        member->list = pr.objects;
                    }
                    expr = CExpr_NewENode(EMEMBER);
                    expr->rtype = &stvoid;
                    expr->data.emember = member;
                    tk = lex();
                    conv->expression = checkreference(
                        CExpr_MakeFunctionCall(expr, CExpr_ScanExpressionList(TRUE))
                    );
                    conv->left = NULL;
                    conv->right = NULL;
                    tk = lex();
                    return TRUE;
                } else {
                    CError_FATAL(4371);
                }
            }
            return FALSE;
        } else {
            return CExpr_OperatorMatch(token, left, right, conv);
        }
    }

    if (!IS_TYPE_CLASS(left->rtype)) {
        if (!IS_TYPE_ENUM(left->rtype)) {
            if (!right) {
                return FALSE;
            }
            if (!IS_TYPE_CLASS(right->rtype) && !IS_TYPE_ENUM(right->rtype)) {
                return FALSE;
            }
        }
    } else {
        CDecl_CompleteType(left->rtype);
    }

    memclrw(&match, sizeof(ArgMatch));
    name = CMangler_OperatorName(token);
    if (token == '(') {
        if (IS_TYPE_CLASS(left->rtype) &&
            CScope_FindClassMemberObject(TYPE_CLASS(left->rtype), &pr2, name)) {
            if (pr2.objects ||
                (pr2.object &&
                 pr2.object->otype == OT_OBJECT &&
                 IS_TYPE_FUNC(OBJECT(pr2.object)->type))) {
                member = lalloc(sizeof(EMemberInfo));
                memclrw(member, sizeof(EMemberInfo));
                member->path = pr2.basePath;
                member->expr = left;
                member->is_qualified = pr2.is_qualified;
                if (!pr2.objects) {
                    member->list = galloc(sizeof(NameSpaceObjectList));
                    member->list->next = NULL;
                    member->list->object = pr2.object;
                } else {
                    member->list = pr2.objects;
                }
                expr = CExpr_NewENode(EMEMBER);
                expr->rtype = &stvoid;
                expr->data.emember = member;
                tk = lex();
                conv->expression =
                    checkreference(CExpr_MakeFunctionCall(expr, CExpr_ScanExpressionList(TRUE)));
                conv->left = NULL;
                conv->right = NULL;
                tk = lex();
                return TRUE;
            } else {
                CError_FATAL(4439);
            }
        }
        return FALSE;
    }

    nodes = lalloc(sizeof(ENodeList));
    nodes->node = left;
    if (right) {
        nodes->next = lalloc(sizeof(ENodeList));
        nodes->next->node = right;
        nodes->next->next = NULL;
    } else {
        nodes->next = NULL;
    }

    obj = NULL;
    if (IS_TYPE_CLASS(left->rtype) &&
        CScope_FindClassMemberObject(TYPE_CLASS(left->rtype), &pr2, name)) {
        if (pr2.object) {
            member_list.next = NULL;
            member_list.object = pr2.object;
            pr2.objects = &member_list;
        } else {
            CError_ASSERT(4470, pr2.objects);
        }

        if (token != '=' || (pr2.basePath->type == left->rtype && pr2.basePath->next == NULL)) {
            prev_obj = match.object;
            CExpr_MatchArgList(pr2.objects, NULL, nodes->next, &match, left, FALSE);
            if (prev_obj != match.object) {
                obj = match.object;
                path = pr2.basePath;
            }
        }
    }

    if (CScope_FindNonClassObject(cscope_current, &pr2, name)) {
        if (pr2.object) {
            nonmember_list.next = NULL;
            nonmember_list.object = pr2.object;
            pr2.objects = &nonmember_list;
        }
    } else {
        pr2.objects = NULL;
    }

    if (copts.arg_dep_lookup) {
        pr2.objects = CScope_ArgumentDependentNameLookup(pr2.objects, name, nodes, TRUE);
    }
    if (pr2.objects) {
        CExpr_MatchArgList(pr2.objects, NULL, nodes, &match, NULL, FALSE);
    }

    if (!match.object) {
        if (!IS_TYPE_CLASS(left->rtype) && (!right || !IS_TYPE_CLASS(right->rtype))) {
            return FALSE;
        }
        return CExpr_CheckOperatorConversion(token, left, right, nodes, conv);
    }

    if (!(token == '&' && !right) && (token != ',')) {
        if (!IS_TYPE_CLASS(left->rtype) &&
            (!right || !IS_TYPE_CLASS(right->rtype)) &&
            match.userconversions) {
            return FALSE;
        }
        if (right &&
            match.userconversions == 2 &&
            CExpr_CheckOperatorConversion(token, left, right, nodes, conv)) {
            return TRUE;
        }
    }

    if (match.list) {
        CError_OverloadedFunctionError(match.object, match.list);
    }

    if (match.object == obj) {
        conv->expression = CExpr_GenericFuncCall(
            path, nodes->node, FALSE, match.object, NULL, NULL, nodes->next, FALSE, FALSE, TRUE
        );
    } else {
        conv->expression = CExpr_GenericFuncCall(
            NULL, NULL, FALSE, match.object, NULL, NULL, nodes, FALSE, FALSE, TRUE
        );
    }

    conv->expression = checkreference(conv->expression);
    conv->left = NULL;
    conv->right = NULL;
    return TRUE;
}

// TODO: invented helpers: the original tests a list entry for a function (template) through
// helpers like these (the IS_TEMPL_FUNC test written inline compiles another way)
static inline int IS_FUNC_LIST(NameSpaceObjectList* list) {
    return list->object->otype == OT_OBJECT && IS_TYPE_FUNC(OBJECT(list->object)->type);
}

static inline Boolean IS_TEMPL_FUNC_LIST(NameSpaceObjectList* list) {
    return IS_FUNC_LIST(list) && (TYPE_FUNC(OBJECT(list->object)->type)->flags & FUNC_IS_TEMPL);
}

// NOTE: invented names: tclass, addr_expr, args, asObject, mostDerived, forceCtor, checkAccess,
// allowExplicit
ENode* CExpr_ConstructObject(
    TypeClass* tclass,
    ENode* addr_expr,
    ENodeList* args,
    Boolean asObject,
    Boolean mostDerived,
    Boolean forceCtor,
    Boolean checkAccess,
    Boolean allowExplicit
) {
    ENode* expr;
    NameSpaceObjectList* ctorlist;
    BClassList path;
    NameSpaceObjectList* scan;
    NameSpaceObjectList* copy;

    CError_ASSERT(4602, IS_TYPE_POINTER_ONLY(addr_expr->rtype));

    addr_expr = makemonadicnode(addr_expr, EINDIRECT);
    addr_expr->rtype = TYPE(tclass);

    // TODO: `expr` is assigned here and never read; the original's code needs the assignment (the
    // result goes to addr_expr), so its source probably had it too
    if (!forceCtor &&
        args &&
        !args->next &&
        (expr = args->node)->rtype == TYPE(tclass) &&
        !CClass_CopyConstructor(tclass)) {
        CError_ASSERT(4612, IS_TYPE_CLASS(addr_expr->rtype));
        addr_expr = makediadicnode(addr_expr, args->node, EASS);
        if (!asObject) {
            addr_expr = getnodeaddress(addr_expr, FALSE);
        }
        return addr_expr;
    }

    ctorlist = CClass_Constructor(tclass);
    // a copy from the class's own type does not consider template constructors
    if (ctorlist && args && !args->next && args->node->rtype == TYPE(tclass)) {
        for (scan = ctorlist; scan; scan = scan->next) {
            if (IS_TEMPL_FUNC_LIST(scan)) {
                scan = NULL;
                for (; ctorlist; ctorlist = ctorlist->next) {
                    if (!IS_TEMPL_FUNC_LIST(ctorlist)) {
                        copy = lalloc(sizeof(NameSpaceObjectList));
                        *copy = *ctorlist;
                        copy->next = scan;
                        scan = copy;
                    }
                }
                ctorlist = scan;
                break;
            }
        }
    }

    if (ctorlist) {
        if (tclass->flags & CLASS_HAS_VBASES) {
            ENodeList* list = lalloc(sizeof(ENodeList));
            list->next = args;
            args = list;
            list->node = intconstnode(TYPE(&stsignedshort), mostDerived ? 1 : 0);
        }
        path.next = NULL;
        path.type = TYPE(tclass);
        expr = CExpr_GenericFuncCall(
            &path, addr_expr, FALSE, NULL, ctorlist, NULL, args, !allowExplicit, FALSE, checkAccess
        );
        if (ENODE_IS2(expr, EFUNCCALL, EFUNCCALLP)) {
            expr->rtype = CDecl_NewPointerType(TYPE(tclass));
        }
        if (asObject) {
            expr = makemonadicnode(expr, EINDIRECT);
            expr->rtype = TYPE(tclass);
        }
        return expr;
    } else {
        if (args) {
            if (!args->next && ENODE_IS(addr_expr, EINDIRECT)) {
                return makediadicnode(
                    addr_expr, CExpr_AssignmentPromotion(args->node, TYPE(tclass), 0, TRUE), EASS
                );
            }
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
        }
        return addr_expr;
    }
}

static ENode* CExpr_DeleteFuncCall(Object* obj, ENode* arg, Type* type, Boolean include_size) {
    ENode* expr;
    ENodeList* list;

    expr = lalloc(sizeof(ENode));
    expr->type = EFUNCCALL;
    expr->cost = 4;
    expr->flags = 0;
    expr->rtype = &stvoid;
    expr->data.funccall.funcref = create_objectrefnode(obj);
    expr->data.funccall.functype = TYPE_FUNC(obj->type);
    obj->flags |= OBJECT_USED;

    list = lalloc(sizeof(ENodeList));
    list->node = arg;
    expr->data.funccall.args = list;

    if (include_size) {
        list->next = lalloc(sizeof(ENodeList));
        list->next->node = nullnode();
        CInt64_SetLong(&list->next->node->data.intval, type->size);
        list->next->node->rtype = CABI_GetSizeTType();
        list->next->next = NULL;
    } else {
        list->next = NULL;
    }

    return expr;
}

static ENode* CExpr_CopyPlacementNewArg(ENodeList* list) {
    switch (list->node->type) {
        case EINDIRECT:
            if (!ENODE_IS(list->node->data.monadic, EOBJREF)) {
                break;
            }
            if (list->node->data.monadic->data.objref->datatype != DLOCAL &&
                !is_const_object(list->node->data.monadic->data.objref)) {
                break;
            }
        case EINTCONST:
        case EFLOATCONST:
        case ESTRINGCONST:
        case EOBJREF:
        case EARGOBJ:
        case ELOCOBJ:
        case EOBJLIST:
        case EVECTOR128CONST:
            return CInline_CopyExpression(list->node, CopyModeLocal);
        default:
            break;
    }

    switch (list->node->rtype->type) {
        default:
            CError_FATAL(4759);
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEENUM:
        case TYPESTRUCT:
        case TYPECLASS:
        case TYPEMEMBERPOINTER:
        case TYPEPOINTER:
        case TYPEOBJCID:
            return CExpr_GetETEMPCopy(list->node);
    }
}

// NOTE: invented names: type, expr, obj, isArray, isGlobal, includeSize
static ENode* CExpr_PlacementDeleteCall(
    Type* type, ENode* expr, Object* obj, Boolean isArray, Boolean isGlobal
) {
    ENodeList* list;
    Object* funcobj;
    ENode* result;
    ENodeList* inputarg;
    Boolean includeSize;

    CError_ASSERT(
        4785, ENODE_IS(expr, EFUNCCALL) && ENODE_IS(expr->data.funccall.funcref, EOBJREF)
    );

    funcobj = expr->data.funccall.funcref->data.objref;
    CError_ASSERT(
        4789,
        IS_TYPE_FUNC(funcobj->type) &&
            TYPE_FUNC(funcobj->type)->args &&
            TYPE_FUNC(funcobj->type)->args->next
    );

    funcobj = CParser_FindDeallocationObject(
        type, TYPE_FUNC(funcobj->type)->args->next, isArray, isGlobal, &includeSize
    );
    if (!funcobj) {
        return NULL;
    }

    result = CExpr_NewENode(EFUNCCALL);
    result->type = EFUNCCALL;
    result->cost = 4;
    result->rtype = &stvoid;
    result->data.funccall.funcref = create_objectrefnode(funcobj);
    result->data.funccall.functype = TYPE_FUNC(funcobj->type);
    funcobj->flags |= OBJECT_USED;

    list = lalloc(sizeof(ENodeList));
    list->node = create_objectnode(obj);
    result->data.funccall.args = list;

    CError_ASSERT(4813, (inputarg = expr->data.funccall.args) && (inputarg = inputarg->next));

    do {
        list->next = lalloc(sizeof(ENodeList));
        list = list->next;
        list->node = CExpr_CopyPlacementNewArg(inputarg);
        inputarg = inputarg->next;
    } while (inputarg);
    list->next = NULL;

    return result;
}

static Type* scan_type_name(UInt32* qual) {
    DeclInfo di;
    memclrw(&di, sizeof(DeclInfo));

    CParser_GetDeclSpecs(&di, FALSE);
    di.isNewExpression = TRUE;
    scandeclarator(&di);

    if (di.name) {
        CError_Error(ERR_ILLEGAL_TYPE);
    }

    firstarrayexpr = di.arrayBound;
    *qual = di.qual;
    return di.thetype;
}

static UInt32 cv_qualifier_list(void) {
    UInt32 qual;
    int t;

    qual = 0;
    t = tk = lex();
    while (t >= TK_CONST && t <= TK_ASM) {
        switch (t) {
            case TK_CONST:
                if (qual & Q_CONST) {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                }
                qual |= Q_CONST;
                break;
            case TK_VOLATILE:
                if (qual & Q_VOLATILE) {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                }
                qual |= Q_VOLATILE;
                break;
            default:
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        }

        t = tk = lex();
    }

    return qual;
}

// NOTE: invented names: di, isFirst
static void scan_new_declarator(DeclInfo* di, Boolean isFirst) {
    NameResult pr;
    SInt32 size;
    ENode* expr;

    switch (tk) {
        case '*':
            makethetypepointer(di, cv_qualifier_list());
            if (tk != '[') {
                scan_new_declarator(di, isFirst);
            }
            break;
        case TK_IDENTIFIER:
        case TK_COLON_COLON:
            if (CScope_ParseQualifiedNameSpace(&pr, TRUE, FALSE) &&
                pr.nspace &&
                pr.nspace->theclass &&
                tk == '*') {
                makememberpointertype(di, pr.nspace->theclass, cv_qualifier_list());
                if (tk != '[') {
                    scan_new_declarator(di, isFirst);
                }
            } else {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }
            break;
    }

    if (tk == '[') {
        tk = lex();
        expr = expression();

        if (IS_TYPE_ENUM(expr->rtype)) {
            expr->rtype = TYPE_ENUM(expr->rtype)->enumtype;
        }
        if (!IS_TYPE_INT(expr->rtype)) {
            CError_Error(ERR_ILLEGAL_TYPE);
        }

        if (tk != ']') {
            CError_Error(ERR_RBRACKET_EXPECTED);
        } else {
            tk = lex();
        }

        if (tk == '[') {
            scan_new_declarator(di, FALSE);
        }

        if (CanCreateObject(di->thetype) && IsCompleteType(di->thetype)) {
            di->thetype = CDecl_NewArrayType(di->thetype, 0);
            if (!ENODE_IS(expr, EINTCONST)) {
                size = 1;
                if (!isFirst) {
                    CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
                } else {
                    firstarrayexpr = expr;
                }
            } else {
                size = CInt64_GetULong(&expr->data.intval);
            }
            TYPE_POINTER(di->thetype)->size = size * TYPE_POINTER(di->thetype)->target->size;
        } else {
            CError_Error(ERR_ILLEGAL_ARRAY_DEFINITION);
        }
    }
}

static Type* scan_new_type_name(UInt32* qual) {
    DeclInfo di;
    memclrw(&di, sizeof(DeclInfo));

    di.isNewTypeId = TRUE;
    CParser_GetDeclSpecs(&di, FALSE);
    scan_new_declarator(&di, TRUE);

    *qual = di.qual;
    return di.thetype;
}

// NOTE: invented names: type, args, isGlobal, isArray
static ENode* CExpr_NewAlloc(Type* type, ENodeList* args, Boolean isGlobal, Boolean isArray) {
    NameSpaceObjectList* list;
    Object* obj;
    Boolean found;
    HashNameNode* name;
    NameResult pr;

    found = FALSE;
    if (!isGlobal && IS_TYPE_CLASS(type)) {
        list = NULL;
        obj = NULL;
        name = (isArray && copts.array_new_delete) ? newa_fobj->name : newp_fobj->name;
        if (CScope_FindClassMemberObject(TYPE_CLASS(type), &pr, name)) {
            list = pr.objects;
            obj = OBJECT(pr.object);
            CError_ASSERT(4968, list || obj);
            found = TRUE;
        } else if (TYPE_CLASS(type)->flags & CLASS_HANDLEOBJECT) {
            CError_ASSERT(4975, !isArray);
            obj = newh_func;
            found = TRUE;
        }
    }

    if (!found) {
        if (isArray && copts.array_new_delete) {
            list = &newa_fobj->first;
        } else {
            list = &newp_fobj->first;
        }
        obj = NULL;
    }

    return CExpr_GenericFuncCall(NULL, NULL, FALSE, obj, list, NULL, args, FALSE, FALSE, TRUE);
}

// NOTE: invented names: type, node, args, isArray, isGlobal, objptr
static ENode* CExpr_NewExceptionSafeAlloc(
    Type* type, ENode* node, ENodeList* args, Boolean isArray, Boolean isGlobal, Object** objptr
) {
    Object* obj;
    ENode* result;
    ENode* catchexpr;
    Object* deletefunc;
    Boolean include_size;

    if (!cscope_currentfunc || !copts.delete_exception || !copts.exceptions) {
        return NULL;
    }

    obj = create_temp_object(TYPE(&void_ptr));
    *objptr = obj;
    deletefunc = NULL;
    catchexpr = NULL;
    include_size = FALSE;
    if (args) {
        catchexpr = CExpr_PlacementDeleteCall(type, node, obj, isArray, isGlobal);
    } else {
        deletefunc = CParser_FindDeallocationObject(type, NULL, isArray, isGlobal, &include_size);
    }

    if (!deletefunc && !catchexpr) {
        return NULL;
    }

    result = lalloc(sizeof(ENode));
    *result = *node;
    node = makediadicnode(create_objectnode(obj), node, EASS);
    if (!catchexpr && !include_size) {
        result->type = ENEWEXCEPTION;
        result->data.newexception.initexpr = node;
        result->data.newexception.tryexpr = NULL;
        result->data.newexception.pointertemp = obj;
        result->data.newexception.deletefunc = deletefunc;
    } else {
        if (!catchexpr) {
            catchexpr =
                CExpr_DeleteFuncCall(deletefunc, create_objectnode(obj), type, include_size);
        }
        result->type = EINITTRYCATCH;
        result->data.itc.initexpr = node;
        result->data.itc.tryexpr = NULL;
        result->data.itc.result = create_objectnode(obj);
        result->data.itc.catchexpr = catchexpr;
    }

    return result;
}

static ENode* CExpr_NewExceptionSafeInit(ENode* expr, ENode* tryexpr) {
    switch (expr->type) {
        case ENEWEXCEPTION:
            expr->data.newexception.tryexpr = tryexpr;
            break;
        case EINITTRYCATCH:
            expr->data.itc.tryexpr = tryexpr;
            break;
        default:
            CError_FATAL(5089);
    }
    return expr;
}

// NOTE: invented names: type, qual, nodelist, isGlobal
static ENode* CExpr_NewArray(Type* type, UInt32 qual, ENodeList* nodelist, Boolean isGlobal) {
    Type* tptr;
    Type* sizetype;
    Type* innertype;
    ENode* result;
    Object* ctor;
    Object* dtor;
    ENodeList* newlist;
    ENode* ass;
    ENode* etemp;
    ENode* mul;
    SInt32 count;
    ENode* newalloc;
    ENode* newESalloc;
    Object* tempobj;
    ENode* callexpr;

    tptr = CDecl_NewPointerType(TYPE_POINTER(type)->target);
    sizetype = CABI_GetSizeTType();
    innertype = TYPE_POINTER(type)->target;
    while (IS_TYPE_ARRAY(innertype)) {
        innertype = TYPE_POINTER(innertype)->target;
    }

    if (!CanAllocObject(innertype) || !IsCompleteType(innertype)) {
        return nullnode();
    }

    newlist = lalloc(sizeof(ENodeList));
    newlist->next = nodelist;
    if (tk == '(') {
        tk = lex();
        if (CExpr_ScanExpressionList(TRUE)) {
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
        }
        if (tk == ')') {
            tk = lex();
        } else {
            CError_Error(ERR_RPAREN_EXPECTED);
        }
    }

    if (!IS_TYPE_CLASS(innertype) || CClass_IsPODClass(TYPE_CLASS(innertype))) {
        newlist->node = intconstnode(sizetype, type->size);
        if (firstarrayexpr) {
            newlist->node = makediadicnode(newlist->node, promote(firstarrayexpr, sizetype), EMUL);
            optimizecomm(newlist->node);
        }
        result = CExpr_NewAlloc(innertype, newlist, isGlobal, TRUE);
        result->rtype = tptr;
        result->flags |= (qual & ENODE_FLAG_QUALS);
        return result;
    }

    ctor = NULL;
    if (CClass_Constructor(TYPE_CLASS(innertype))) {
        ctor = CClass_DefaultConstructor(TYPE_CLASS(innertype));
        if (ctor) {
            ctor->flags |= OBJECT_USED;
        } else {
            ctor = CClass_DummyDefaultConstructor(TYPE_CLASS(innertype));
            if (!ctor) {
                CError_Error(ERR_CLASS_NO_DEFAULT_CONSTRUCTOR);
            }
        }
    }

    dtor = CClass_Destructor(TYPE_CLASS(innertype));
    if (dtor) {
        dtor = CABI_GetDestructorObject(dtor, CABIDestroyComplete);
    }

    ass = NULL;
    if (firstarrayexpr) {
        etemp = CExpr_NewETEMPNode(sizetype, TRUE);
        mul = promote(firstarrayexpr, sizetype);
        if ((count = innertype->size ? type->size / innertype->size : 0) > 1) {
            mul = makediadicnode(mul, intconstnode(sizetype, count), EMUL);
            optimizecomm(mul);
        }
        ass = makediadicnode(CExpr_DerefETEMPCopy(etemp), mul, EASS);
        mul = makediadicnode(
            CExpr_DerefETEMPCopy(etemp), intconstnode(sizetype, innertype->size), EMUL
        );
        optimizecomm(mul);
        mul = makediadicnode(mul, intconstnode(sizetype, 16), EADD);
        optimizecomm(mul);
        newlist->node = mul;
        etemp = CExpr_DerefETEMPCopy(etemp);
    } else {
        newlist->node = intconstnode(sizetype, type->size + 16);
        count = innertype->size ? type->size / innertype->size : 0;
        etemp = intconstnode(sizetype, count);
    }

    newalloc = CExpr_NewAlloc(innertype, newlist, isGlobal, TRUE);
    newalloc->rtype = tptr;
    newESalloc =
        CExpr_NewExceptionSafeAlloc(innertype, newalloc, nodelist, TRUE, isGlobal, &tempobj);
    if (newESalloc) {
        callexpr = CExpr_FuncCallSix(
            cnar_func,
            create_objectnode(tempobj),
            ctor ? create_objectrefnode(ctor) : nullnode(),
            dtor ? create_objectrefnode(dtor) : nullnode(),
            intconstnode(sizetype, innertype->size),
            etemp,
            NULL
        );
        result = CExpr_NewExceptionSafeInit(
            newESalloc, makediadicnode(create_objectnode(tempobj), callexpr, EASS)
        );
    } else {
        result = CExpr_FuncCallSix(
            cnar_func,
            newalloc,
            ctor ? create_objectrefnode(ctor) : nullnode(),
            dtor ? create_objectrefnode(dtor) : nullnode(),
            intconstnode(sizetype, innertype->size),
            etemp,
            NULL
        );
    }

    if (ass) {
        result = makecommaexpression(ass, result);
    }
    result->rtype = tptr;
    result->flags |= (qual & ENODE_FLAG_QUALS);
    return result;
}

static ENode* CExpr_NewSimpleClass(TypeClass* tclass, ENode* expr, ENodeList* args) {
    ENode* precomp;
    ENode* nullcheck;

    precomp = lalloc(sizeof(ENode));
    *precomp = *expr;
    precomp->type = EPRECOMP;
    precomp->data.precompid = CParser_GetUniqueID();

    nullcheck = lalloc(sizeof(ENode));
    *nullcheck = *expr;
    nullcheck->type = ENULLCHECK;
    nullcheck->cost = 4;
    nullcheck->data.nullcheck.nullcheckexpr = expr;
    nullcheck->data.nullcheck.condexpr =
        CExpr_ConstructObject(tclass, precomp, args, FALSE, TRUE, TRUE, TRUE, TRUE);
    nullcheck->data.nullcheck.precompid = precomp->data.precompid;
    return nullcheck;
}

// NOTE: invented names: tclass, expr, args1, args2, isGlobal
static ENode* CExpr_NewClass(
    TypeClass* tclass, ENode* expr, ENodeList* args1, ENodeList* args2, Boolean isGlobal
) {
    Object* objptr;
    ENode* newESalloc;

    if (!args2 && !isGlobal && CABI_ConstructorCallsNew(tclass)) {
        expr = nullnode();
        expr->rtype = CDecl_NewPointerType(TYPE(tclass));
        return CExpr_ConstructObject(tclass, expr, args1, FALSE, TRUE, TRUE, TRUE, TRUE);
    } else {
        newESalloc =
            CExpr_NewExceptionSafeAlloc(TYPE(tclass), expr, args2, FALSE, isGlobal, &objptr);
        if (newESalloc) {
            return CExpr_NewExceptionSafeInit(
                newESalloc,
                CExpr_ConstructObject(
                    tclass, create_objectnode(objptr), args1, FALSE, TRUE, TRUE, TRUE, TRUE
                )
            );
        } else {
            return CExpr_NewSimpleClass(tclass, expr, args1);
        }
    }
}

ENode* scannew(Boolean isGlobal) { // NOTE: invented name: isGlobal
    Type* type;
    UInt32 qual;
    ENodeList* args;
    ENodeList* newargs;
    ENode* expr;
    ENodeList* args2;
    Object* tempobj;
    ENode* newESalloc;
    ENode* tempobj_expr;
    ENode* etemp_expr;
    ENode* indirect_expr;
    ENode* ass;
    ENode* cond;

    type = NULL;
    args = NULL;
    firstarrayexpr = NULL;
    if ((tk = lex()) == '(') {
        tk = lex();
        if (!isdeclaration(1, 1, 1, ')')) {
            args = CExpr_ScanExpressionList(TRUE);
        } else {
            type = scan_type_name(&qual);
        }
        if (tk != ')') {
            CError_Error(ERR_RPAREN_EXPECTED);
        } else {
            tk = lex();
        }
    }

    if (!type) {
        if (tk == '(') {
            tk = lex();
            type = scan_type_name(&qual);
            if (tk != ')') {
                CError_Error(ERR_RPAREN_EXPECTED);
            } else {
                tk = lex();
            }
        } else {
            type = scan_new_type_name(&qual);
        }
    }

    if (IS_TYPE_ARRAY(type)) {
        return CExpr_NewArray(type, qual, args, isGlobal);
    }

    if (IS_TYPE_CLASS(type)) {
        if (TYPE_CLASS(type)->sominfo) {
            return CSOM_New(TYPE_CLASS(type));
        }
        if (TYPE_CLASS(type)->objcinfo) {
            return CObjC_New(TYPE_CLASS(type));
        }
    }

    if (!IsCompleteType(type) || !CanAllocObject(type)) {
        return nullnode();
    }

    newargs = lalloc(sizeof(ENodeList));
    newargs->next = args;
    newargs->node = intconstnode(CABI_GetSizeTType(), type->size);

    expr = CExpr_NewAlloc(type, newargs, isGlobal, FALSE);
    expr->rtype = CDecl_NewPointerType(type);
    expr->flags |= (qual & ENODE_FLAG_QUALS);

    if (tk == '(') {
        tk = lex();
        args2 = CExpr_ScanExpressionList(TRUE);
        if (!args2 && !IS_TYPE_CLASS(type)) {
            args2 = lalloc(sizeof(ENodeList));
            memclrw(args2, sizeof(ENodeList));
            args2->node = do_typecast(nullnode(), type, 0);
        }
        if (tk == ')') {
            tk = lex();
        } else {
            CError_Error(ERR_RPAREN_EXPECTED);
        }
    } else {
        args2 = NULL;
    }

    if (ENODE_IS(expr, EINTCONST)) {
        return expr;
    }

    if (IS_TYPE_CLASS(type) && CClass_Constructor(TYPE_CLASS(type))) {
        return CExpr_NewClass(TYPE_CLASS(type), expr, args2, args, isGlobal);
    }

    if (args2) {
        if (args2->next) {
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
            return nullnode();
        }

        newESalloc = CExpr_NewExceptionSafeAlloc(type, expr, args, FALSE, isGlobal, &tempobj);
        if (newESalloc) {
            tempobj_expr = makemonadicnode(create_objectnode(tempobj), EINDIRECT);
            tempobj_expr->rtype = type;
            tempobj_expr->flags = args2->node->flags & ENODE_FLAG_QUALS;
            return CExpr_NewExceptionSafeInit(
                newESalloc,
                makediadicnode(
                    tempobj_expr,
                    CExpr_AssignmentPromotion(args2->node, type, newESalloc->flags, TRUE),
                    EASS
                )
            );
        } else {
            etemp_expr = CExpr_GetETEMPCopy(expr);
            indirect_expr = makemonadicnode(etemp_expr, EINDIRECT);
            indirect_expr->rtype = type;
            indirect_expr->flags = args2->node->flags & ENODE_FLAG_QUALS;
            ass = makediadicnode(
                indirect_expr,
                CExpr_AssignmentPromotion(args2->node, type, indirect_expr->flags, TRUE),
                EASS
            );
            cond = CExpr_NewENode(ECOND);
            cond->cost = 4;
            cond->rtype = &stvoid;
            cond->data.cond.cond = expr;
            cond->data.cond.expr1 = ass;
            cond->data.cond.expr2 = nullnode();
            cond = makecommaexpression(cond, etemp_expr);
            cond->rtype = etemp_expr->rtype;
            cond->flags = etemp_expr->flags;
            return cond;
        }
    } else {
        return expr;
    }
}

// NOTE: invented names: expr, type, isGlobal, includeSize
static ENode* CExpr_DeleteArray(ENode* expr, Type* type, Boolean isGlobal) {
    Object* obj;
    Object* dtor;
    Boolean includeSize;
    ENode* precomp;
    SInt32 precompid;
    ENode* result;

    obj = CParser_FindDeallocationObject(type, NULL, TRUE, isGlobal, &includeSize);

    if (!IS_TYPE_CLASS(type) || CClass_IsPODClass(TYPE_CLASS(type))) {
        return CExpr_DeleteFuncCall(obj, expr, type, includeSize);
    }

    dtor = CClass_Destructor(TYPE_CLASS(type));
    if (dtor || includeSize) {
        if (obj->nspace == cscope_root && !includeSize) {
            return funccallexpr(
                dnar_func, expr, dtor ? create_objectrefnode(dtor) : nullnode(), NULL, NULL
            );
        }
        return funccallexpr(
            dnar3_func,
            expr,
            dtor ? create_objectrefnode(dtor) : nullnode(),
            create_objectrefnode(obj),
            intconstnode(TYPE(&stsignedshort), includeSize)
        );
    }

    precomp = lalloc(sizeof(ENode));
    *precomp = *expr;
    precomp->type = EPRECOMP;
    precomp->data.precompid = precompid = CParser_GetUniqueID();
    precomp = CExpr_DeleteFuncCall(
        obj, makediadicnode(precomp, intconstnode(CABI_GetSizeTType(), 16), ESUB), type, FALSE
    );

    result = CExpr_NewENode(ENULLCHECK);
    result->rtype = &stvoid;
    result->cost = 4;
    result->data.nullcheck.nullcheckexpr = expr;
    result->data.nullcheck.condexpr = precomp;
    result->data.nullcheck.precompid = precompid;

    return result;
}

// NOTE: invented names: isGlobal, includeSize
ENode* scandelete(Boolean isGlobal) {
    Boolean is_array;
    Type* conv_type;
    ENode* expr;
    UInt32 conv_qual;
    Type* innertype;
    Type* t;
    Object* obj;
    Object* dtor;
    ENode* result_expr;
    ENode* precomp_orig;
    SInt32 precompid;
    Boolean is_virtual;
    ENode* nc;
    Boolean includeSize;
    ConversionIterator iter;
    BClassList path;

    if ((tk = lex()) == '[') {
        if ((tk = lex()) != ']') {
            CError_Error(ERR_RBRACKET_EXPECTED);
        } else {
            tk = lex();
        }
        is_array = TRUE;
    } else {
        is_array = FALSE;
    }

    expr = cast_expression();
    if (!IS_TYPE_POINTER_ONLY(expr->rtype)) {
        if (IS_TYPE_CLASS(expr->rtype)) {
            conv_type = NULL;
            CExpr_ConversionIteratorInit(&iter, TYPE_CLASS(expr->rtype));
            while ((obj = CExpr_ConversionIteratorNext(&iter))) {
                TypeFunc* tfunc = TYPE_FUNC(obj->type);
                if (IS_TYPE_POINTER_ONLY(tfunc->functype)) {
                    if (conv_type) {
                        CError_Error(ERR_AMBIGUOUS_ACCESS_OVERLOADED_FUNCTION);
                        break;
                    }
                    conv_type = tfunc->functype;
                    conv_qual = tfunc->qual;
                }
            }
            if (conv_type) {
                if (!copts.old_argmatch) {
                    expr = CExpr_Convert(expr, conv_type, conv_qual, TRUE, TRUE);
                } else {
                    if (user_assign_check(expr, conv_type, conv_qual, TRUE, FALSE, TRUE)) {
                        expr = assign_node;
                    }
                }
            }
        }
        if (!IS_TYPE_POINTER_ONLY(expr->rtype)) {
            CError_Error(ERR_ILLEGAL_TYPE);
            return nullnode();
        }
    }

    innertype = TYPE_POINTER(expr->rtype)->target;
    if (innertype->size == 0) {
        CDecl_CompleteType(innertype);
    }

    if (IS_TYPE_ARRAY(innertype) && !is_array) {
        CError_Error(ERR_ILLEGAL_TYPE);
    }

    if (is_array) {
        t = innertype;
        while (IS_TYPE_ARRAY(t)) {
            t = TYPE_POINTER(t)->target;
        }
        return CExpr_DeleteArray(expr, t, isGlobal);
    }

    if (copts.objective_c && CObjC_IsType_id(expr->rtype)) {
        return CObjC_Delete(NULL, expr);
    }

    if (!IS_TYPE_CLASS(innertype)) {
        obj = CParser_FindDeallocationObject(innertype, NULL, FALSE, isGlobal, &includeSize);
        CClass_CheckObjectAccess(NULL, obj);
        return CExpr_DeleteFuncCall(obj, expr, innertype, includeSize);
    }

    if (TYPE_CLASS(innertype)->sominfo) {
        return CSOM_Delete(TYPE_CLASS(innertype), expr);
    }

    if (TYPE_CLASS(innertype)->objcinfo) {
        return CObjC_Delete(TYPE_CLASS(innertype), expr);
    }

    if (!(TYPE_CLASS(innertype)->flags & CLASS_COMPLETED) && copts.pedantic) {
        CError_Warning(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, innertype, 0);
    }

    obj = CParser_FindDeallocationObject(innertype, NULL, FALSE, isGlobal, &includeSize);
    CClass_CheckObjectAccess(NULL, obj);
    dtor = CClass_Destructor(TYPE_CLASS(innertype));
    if (!dtor) {
        return CExpr_DeleteFuncCall(obj, expr, innertype, includeSize);
    }

    path.next = NULL;
    path.type = innertype;
    CClass_CheckObjectAccess(&path, dtor);

    result_expr = lalloc(sizeof(ENode));
    result_expr->type = EFUNCCALL;
    result_expr->cost = 4;
    result_expr->flags = 0;
    result_expr->rtype = &stvoid;
    if (dtor->datatype == DVFUNC) {
        precomp_orig = expr;
        expr = lalloc(sizeof(ENode));
        *expr = *precomp_orig;
        expr->type = EPRECOMP;
        expr->data.precompid = precompid = CParser_GetUniqueID();
        is_virtual = TRUE;
    } else {
        is_virtual = FALSE;
    }

    if (!isGlobal) {
        result_expr = CABI_DestroyObject(dtor, expr, CABIDestroyDelete, FALSE, TRUE);
    } else {
        CError_ASSERT(5683, !includeSize);
        result_expr = CABI_DestroyObject(dtor, expr, CABIDestroyDelete, FALSE, FALSE);
        result_expr->rtype = TYPE(&void_ptr);
        result_expr = funccallexpr(obj, result_expr, NULL, NULL, NULL);
        obj->flags |= OBJECT_USED;
    }

    if (is_virtual) {
        nc = lalloc(sizeof(ENode));
        nc->type = ENULLCHECK;
        nc->rtype = &stvoid;
        nc->cost = 4;
        nc->flags = 0;
        nc->data.nullcheck.nullcheckexpr = precomp_orig;
        nc->data.nullcheck.condexpr = result_expr;
        nc->data.nullcheck.precompid = precompid;
        return nc;
    } else {
        return result_expr;
    }
}

// The unit's statics are 2-byte packed, as in the original (MWCC lays out .bss with the packing in
// force at the end of the file)
#pragma pack(2)
