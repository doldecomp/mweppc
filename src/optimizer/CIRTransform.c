#include "compiler/CIRTransform.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CMachine.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CScope.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include <string.h>

#pragma pack(push, 2)
typedef struct CIRTransTemp {
    struct CIRTransTemp* next;
    Object* object;
    Boolean inUse; // NOTE: invented name
} CIRTransTemp;

typedef struct MultiAccessOperand {
    Object* object;
    Object* tempobj;
    ENode* ass;
    Type* type;
    Type* bitfieldType;
} MultiAccessOperand;

// a runtime helper the dead CIRTrans_TransExpr's helpers call, its object made on first use (NOTE:
// invented name)
typedef struct RuntimeFunction {
    Object* object;
    short unused; // nothing reads or writes it. NOTE: invented name
    char name[1];
} RuntimeFunction;
#pragma pack(pop)

// clang-format off
static TypeFunc cirtrans_rtfunc2 = {
    TYPEFUNC, 0, NULL, NULL, TYPE(&stsignedshort), 0, 0
};
static TypeFunc cirtrans_rtfunc4 = {
    TYPEFUNC, 0, NULL, NULL, TYPE(&stunsignedlong), 0, 0
};
static TypeFunc cirtrans_rtfunc8 = {
    TYPEFUNC, 0, NULL, NULL, TYPE(&void_ptr), 0, 0
};
// clang-format on

static CIRTransTemp* cirtrans_temps;
// set only by the dead CIRTrans_TransExpr; the compiler defines no such variable
extern Boolean alloca_called;

static ENode* CIRTrans_TransExpr(ENode* expr, Boolean isUsed);

void CIRTrans_Setup(void) {}

void CIRTrans_Cleanup(void) {}

static Object* CIRTrans_GetRuntimeFunction(RuntimeFunction* rtfunc, Type* type) {
    Object* object;

    object = rtfunc->object;
    if (!object) {
        object = CParser_NewFunctionObject(NULL);
        rtfunc->object = object;

        object->nspace = cscope_root;
        object->name = GetHashNameNodeExport(rtfunc->name);
        object->flags = OBJECT_INTERNAL;

        if (type) {
            switch (type->size) {
                case 2:
                    object->type = TYPE(&cirtrans_rtfunc2);
                    break;
                case 4:
                    object->type = TYPE(&cirtrans_rtfunc4);
                    break;
                case 8:
                    object->type = TYPE(&cirtrans_rtfunc8);
                    break;
                default:
                    CError_FATAL(427);
            }
        } else {
            object->type = TYPE(&cirtrans_rtfunc8);
        }
    }

    return object;
}

static Object* CIRTrans_GetTemporary(Type* type) {
    CIRTransTemp* temp;

    for (temp = cirtrans_temps; temp; temp = temp->next) {
        if (temp->object->type->size == type->size && !temp->inUse) {
            temp->inUse = TRUE;
            return temp->object;
        }
    }

    temp = oalloc(sizeof(CIRTransTemp));
    temp->next = cirtrans_temps;
    cirtrans_temps = temp;

    temp->object = create_temp_object(type);
    temp->inUse = TRUE;
    return temp->object;
}

static ENode* CIRTrans_CheckRuntimeAssign(ENode* expr) {
    ENode* inner;

    if (ENODE_IS(expr->data.diadic.right, EINDIRECT)) {
        inner = expr->data.diadic.right->data.monadic;
        if (ENODE_IS(inner, EFUNCCALL) &&
            (inner->flags & ENODE_FLAG_RESULT_VIA_TEMP) &&
            inner->data.funccall.args &&
            ENODE_IS(inner->data.funccall.args->node, EOBJREF)) {
            CError_ASSERT(502, ENODE_IS(expr->data.diadic.left, EINDIRECT));
            inner->data.funccall.args->node = expr->data.diadic.left->data.monadic;
            inner->flags &= ~ENODE_FLAG_RESULT_VIA_TEMP;
            return expr->data.diadic.right;
        }
    }

    return expr;
}

static void CIRTrans_SetupMultiAccessOperand(MultiAccessOperand* mop, ENode* expr) {
    memclrw(mop, sizeof(MultiAccessOperand));

    mop->type = expr->rtype;

    CError_ASSERT(527, ENODE_IS(expr, EINDIRECT));
    expr = expr->data.monadic;

    if (ENODE_IS(expr, EOBJREF)) {
        mop->object = expr->data.objref;
    } else {
        if (ENODE_IS(expr, EBITFIELD)) {
            mop->bitfieldType = expr->rtype;
            expr = expr->data.monadic;
        }
        expr->rtype = CDecl_NewPointerType(mop->type);
        mop->tempobj = create_temp_object(expr->rtype);
        mop->ass = makediadicnode(create_objectnode(mop->tempobj), expr, EASS);
    }
}

static ENode* CIRTrans_GetMultiAccessOperand(MultiAccessOperand* mop) {
    ENode* expr;

    if (mop->object == NULL) {
        expr = create_objectnode(mop->tempobj);
        if (mop->bitfieldType) {
            expr = makemonadicnode(expr, EBITFIELD);
            expr->rtype = mop->bitfieldType;
        }
        expr = makemonadicnode(expr, EINDIRECT);
    } else {
        expr = create_objectnode(mop->object);
    }

    expr->rtype = mop->type;
    return expr;
}

static ENode* CIRTrans_InitMultiAccessExpression(MultiAccessOperand* mop, ENode* expr) {
    if (mop->ass) {
        expr = makediadicnode(mop->ass, expr, ECOMMA);
        expr->rtype = expr->data.diadic.right->rtype;
    }
    return expr;
}

ENode* CIRTrans_TransformOpAss(ENode* expr) {
    ENodeType nt;
    ENode* result; // NOTE: invented name
    MultiAccessOperand mop;

    if (!ENODE_IS(expr->data.diadic.left, EINDIRECT)) {
        CError_Error(ERR_NOT_LVALUE);
        return nullnode();
    }

    CIRTrans_SetupMultiAccessOperand(&mop, expr->data.diadic.left);

    switch (expr->type) {
        case EMULASS:
            nt = EMUL;
            break;
        case EDIVASS:
            nt = EDIV;
            break;
        case EMODASS:
            nt = EMODULO;
            break;
        case EADDASS:
            nt = EADD;
            break;
        case ESUBASS:
            nt = ESUB;
            break;
        case ESHLASS:
            nt = ESHL;
            break;
        case ESHRASS:
            nt = ESHR;
            break;
        case EANDASS:
            nt = EAND;
            break;
        case EXORASS:
            nt = EXOR;
            break;
        case EORASS:
            nt = EOR;
            break;
        default:
            CError_FATAL(627);
    }

    result = CIRTrans_GetMultiAccessOperand(&mop);

    if (!IS_TYPE_POINTER_ONLY(result->rtype)) {
        result = CExpr_NewDyadicNode(result, nt, expr->data.diadic.right);
        if (result->rtype != expr->data.diadic.left->rtype) {
            result = makemonadicnode(result, ETYPCON);
            result->rtype = expr->data.diadic.left->rtype;
        }
    } else {
        result = makediadicnode(result, expr->data.diadic.right, nt);
    }

    if (IS_TYPE_FLOAT(result->rtype)) {
        result = CExpr_BinaryFloatExpression(result);
    }

    result = makediadicnode(CIRTrans_GetMultiAccessOperand(&mop), result, EASS);
    return CIRTrans_InitMultiAccessExpression(&mop, result);
}

static void CIRTrans_TransIncDec(void) {
    // empty, never called
}

static ENode* CIRTrans_TransIntConst(ENode* expr) {
    Object* obj;
    UInt8 data[16];

    CMach_InitIntMem(expr->rtype, expr->data.intval, data);

    obj = CParser_NewGlobalDataObject(NULL);
    obj->name = CParser_GetUniqueName();
    obj->type = expr->rtype;
    obj->sclass = TK_STATIC;
    obj->datatype = DDATA;
    CScope_AddGlobalObject(obj);
    CInit_DeclareData(obj, data, NULL, obj->type->size);
    return create_objectnode(obj);
}

static ENode* CIRTrans_TransFloatConst(ENode* expr) {
    Object* obj;
    UInt8 data[16];

    CMach_InitFloatMem(expr->rtype, expr->data.floatval, data);

    obj = CParser_NewGlobalDataObject(NULL);
    obj->name = CParser_GetUniqueName();
    obj->type = expr->rtype;
    obj->sclass = TK_STATIC;
    obj->datatype = DDATA;
    CScope_AddGlobalObject(obj);
    CInit_DeclareData(obj, data, NULL, obj->type->size);
    return create_objectnode(obj);
}

static ENode* CIRTrans_TransUnary(ENode* expr, Type* type, RuntimeFunction* rtfunc) {
    if (type->size > 4) {
        expr = funccallexpr(
            CIRTrans_GetRuntimeFunction(rtfunc, type),
            create_objectrefnode(CIRTrans_GetTemporary(type)),
            expr,
            NULL,
            NULL
        );
        expr->flags |= ENODE_FLAG_RESULT_VIA_TEMP;
        expr = makemonadicnode(expr, EINDIRECT);
        expr->rtype = type;
        return expr;
    } else {
        expr = funccallexpr(CIRTrans_GetRuntimeFunction(rtfunc, type), expr, NULL, NULL, NULL);
        expr->rtype = type;
        return expr;
    }
}

static ENode* CIRTrans_TransBinary(ENode* expr, RuntimeFunction* rtfunc) {
    ENode* call; // NOTE: invented name

    if (expr->rtype->size > 4) {
        call = funccallexpr(
            CIRTrans_GetRuntimeFunction(rtfunc, expr->rtype),
            create_objectrefnode(CIRTrans_GetTemporary(expr->rtype)),
            expr->data.diadic.left,
            expr->data.diadic.right,
            NULL
        );
        call->flags |= ENODE_FLAG_RESULT_VIA_TEMP;
        call = makemonadicnode(call, EINDIRECT);
        call->rtype = expr->rtype;
        return call;
    } else {
        call = funccallexpr(
            CIRTrans_GetRuntimeFunction(rtfunc, expr->rtype),
            expr->data.diadic.left,
            expr->data.diadic.right,
            NULL,
            NULL
        );
        call->rtype = expr->rtype;
        return call;
    }
}

static ENodeList* CIRTrans_TransExprList(ENodeList* list) {
    ENodeList* scan;

    for (scan = list; scan; scan = scan->next) {
        scan->node = CIRTrans_TransExpr(scan->node, TRUE);
    }

    return list;
}

// NOTE: invented names: expr, isUsed
static ENode* CIRTrans_TransExpr(ENode* expr, Boolean isUsed) {
    switch (expr->type) {
        case EINDIRECT:
        case EFORCELOAD:
        case EBITFIELD:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, isUsed);
            break;
        case EPOSTINC:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, TRUE);
            break;
        case EPOSTDEC:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, TRUE);
            break;
        case EPREINC:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, TRUE);
            break;
        case EPREDEC:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, TRUE);
            break;
        case ETYPCON:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, isUsed);
            if (!isUsed) {
                return expr->data.monadic;
            }
            break;
        case EBINNOT:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, isUsed);
            break;
        case ELOGNOT:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, isUsed);
            break;
        case EMONMIN:
            expr->data.monadic = CIRTrans_TransExpr(expr->data.monadic, isUsed);
            break;
        case EADD:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, isUsed);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            break;
        case ESUB:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, isUsed);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            break;
        case EMUL:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, isUsed);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            // (falls through, so a multiplication's operands are transformed twice)
        case EDIV:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, isUsed);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            break;
        case EMODULO:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, isUsed);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            break;
        case ESHL:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, isUsed);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            break;
        case ESHR:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, isUsed);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            break;
        case EROTL:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, isUsed);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            break;
        case EROTR:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, isUsed);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            break;
        case EAND:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, isUsed);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            break;
        case EXOR:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, isUsed);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            break;
        case EOR:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, isUsed);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            break;
        case ELESS:
        case EGREATER:
        case ELESSEQU:
        case EGREATEREQU:
        case EEQU:
        case ENOTEQU:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, isUsed);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            if (!isUsed) {
                expr->type = ECOMMA;
                expr->rtype = expr->data.diadic.right->rtype;
                return expr;
            }
            break;
        case ELAND:
        case ELOR:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, TRUE);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            break;
        case EMULV:
        case EADDV:
        case ESUBV:
        case EPMODULO:
        case EBCLR:
        case EBTST:
        case EBSET:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, isUsed);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            break;
        case EASS:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, TRUE);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, TRUE);
            break;
        case EMULASS:
        case EDIVASS:
        case EADDASS:
        case ESUBASS:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, TRUE);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, TRUE);
            break;
        case EMODASS:
        case ESHLASS:
        case ESHRASS:
        case EANDASS:
        case EXORASS:
        case EORASS:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, TRUE);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, TRUE);
            break;
        case ECOMMA:
            expr->data.diadic.left = CIRTrans_TransExpr(expr->data.diadic.left, FALSE);
            expr->data.diadic.right = CIRTrans_TransExpr(expr->data.diadic.right, isUsed);
            break;
        case ECOND:
            expr->data.cond.cond = CIRTrans_TransExpr(expr->data.cond.cond, TRUE);
            expr->data.cond.expr1 = CIRTrans_TransExpr(expr->data.cond.expr1, TRUE);
            expr->data.cond.expr2 = CIRTrans_TransExpr(expr->data.cond.expr2, TRUE);
            break;
        case EMFPOINTER:
            expr->data.mfpointer.accessnode =
                CIRTrans_TransExpr(expr->data.mfpointer.accessnode, isUsed);
            expr->data.mfpointer.mfpointer =
                CIRTrans_TransExpr(expr->data.mfpointer.mfpointer, isUsed);
            break;
        case EFUNCCALL:
        case EFUNCCALLP:
            if (ENODE_IS(expr->data.funccall.funcref, EOBJREF) &&
                !strcmp(expr->data.funccall.funcref->data.objref->name->name, "__alloca")) {
                alloca_called = TRUE;
            }
            expr->data.funccall.funcref = CIRTrans_TransExpr(expr->data.funccall.funcref, TRUE);
            expr->data.funccall.args = CIRTrans_TransExprList(expr->data.funccall.args);
            break;
        case ENULLCHECK:
            expr->data.nullcheck.nullcheckexpr =
                CIRTrans_TransExpr(expr->data.nullcheck.nullcheckexpr, TRUE);
            expr->data.nullcheck.condexpr = CIRTrans_TransExpr(expr->data.nullcheck.condexpr, TRUE);
            break;
        case ENEWEXCEPTION:
        case ENEWEXCEPTIONARRAY:
            expr->data.newexception.initexpr =
                CIRTrans_TransExpr(expr->data.newexception.initexpr, TRUE);
            expr->data.newexception.tryexpr =
                CIRTrans_TransExpr(expr->data.newexception.tryexpr, TRUE);
            break;
        case EINITTRYCATCH:
            expr->data.itc.initexpr = CIRTrans_TransExpr(expr->data.itc.initexpr, TRUE);
            expr->data.itc.tryexpr = CIRTrans_TransExpr(expr->data.itc.tryexpr, TRUE);
            expr->data.itc.catchexpr = CIRTrans_TransExpr(expr->data.itc.catchexpr, TRUE);
            expr->data.itc.result = CIRTrans_TransExpr(expr->data.itc.result, TRUE);
            break;
        case EINTCONST:
        case EFLOATCONST:
        case ESTRINGCONST:
        case EOBJREF:
        case EPRECOMP:
        case ETEMP:
        case ELABEL:
        case EMEMBER:
        case EINSTRUCTION:
        case EVECTOR128CONST:
            break;
        default:
            CError_FATAL(1947);
    }

    return expr;
}

void CIRTrans_Transform(void) {
    cirtrans_temps = NULL;
}
