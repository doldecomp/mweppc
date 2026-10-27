#include "compiler/CExpr.h"
#include "compiler/CABI.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CException.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CInline.h"
#include "compiler/CInt64.h"
#include "compiler/CIRTransform.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CObjC.h"
#include "compiler/CObjCModern.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CRTTI.h"
#include "compiler/CScope.h"
#include "compiler/CSOM.h"
#include "compiler/CTemplateNew.h"
#include "compiler/CTemplateTools.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/PPCError.h"
#include "compiler/scopes.h"
#include "compiler/templates.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include <string.h>

NameObjCheckCB name_obj_check;
Boolean disallowgreaterthan;

static ENode* makeaddnode(ENode* left, ENode* right);
static ENode* makesubnode(ENode* left, ENode* right);

ENode* CExpr_RewriteConst(ENode* expr) {
    Object* obj;

restart:
    if (ENODE_IS(expr, EINDIRECT) && ENODE_IS(expr->data.monadic, EOBJREF)) {
        obj = expr->data.monadic->data.objref;
        if (obj->datatype == DALIAS) {
            CExpr_AliasTransform(expr->data.monadic);
            goto restart;
        }

        if ((obj->qual & Q_INLINE_DATA) && expr->rtype == obj->type) {
            switch (expr->rtype->type) {
                case TYPEINT:
                case TYPEENUM:
                    expr->type = EINTCONST;
                    expr->data.intval = obj->u.data.u.intconst;
                    break;
                case TYPEPOINTER:
                    expr->type = EINTCONST;
                    expr->data.intval = obj->u.data.u.intconst;
                    expr->rtype = TYPE(&stunsignedlong);
                    expr = makemonadicnode(expr, ETYPCON);
                    expr->rtype = obj->type;
                    break;
                case TYPEFLOAT:
                    expr->type = EFLOATCONST;
                    if (obj->u.data.u.floatconst) {
                        expr->data.floatval = *obj->u.data.u.floatconst;
                    } else {
                        expr->data.floatval =
                            CMach_CalcFloatConvertFromInt(TYPE(&stsignedlong), cint64_zero);
                    }
                    break;
                default:
                    CError_FATAL(105);
            }
        }
    }

    return expr;
}

void optimizecomm(ENode* expr) {
    ENode* right;
    ENode* left;

    right = expr->data.diadic.right;
    if (ENODE_IS(right, EINTCONST)) {
        return;
    }
    left = expr->data.diadic.left;
    if (ENODE_IS(left, EINTCONST)) {
swap:
        expr->data.diadic.right = left;
        expr->data.diadic.left = right;
        return;
    }

    if (ENODE_IS(left, EFLOATCONST)) {
        return;
    }
    if (ENODE_IS(right, EFLOATCONST)) {
        goto swap;
    }

    if (expr->rtype->type > TYPEFLOAT) {
        return;
    }

    if (left->cost > right->cost) {
        goto swap;
    }
}

static void checkadditive(ENode* expr) {
    switch (expr->rtype->type) {
        case TYPEINT:
            if (expr->rtype == TYPE(&stbool)) {
                break;
            }
        case TYPEFLOAT:
            return;
        case TYPEENUM:
            if (copts.cplusplus) {
                break;
            }
            return;
        case TYPEPOINTER:
            if (TPTR_TARGET(expr->rtype)->size == 0) {
                CDecl_CompleteType(TPTR_TARGET(expr->rtype));
            }
            if (TPTR_TARGET(expr->rtype)->size == 0) {
                break;
            }
            return;
        case TYPEARRAY:
            if (ENODE_IS(expr, EOBJREF)) {
                return;
            }
        default:
            break;
    }

    CError_Error(ERR_ILLEGAL_OPERAND_TYPE, expr->rtype, ENODE_QUALS(expr));
}

static void CExpr_CompareConvert(ENode** leftp, char* opname, ENode** rightp, Boolean unused) {
    ENode* left;
    ENode* right;
    CInt64 val;

    left = *leftp;
    right = *rightp;

    switch (left->rtype->type) {
        case TYPEINT:
            break;
        case TYPEFLOAT:
            if (left->rtype == right->rtype) {
                return;
            }
            CExpr_ArithmeticConversion(leftp, rightp);
            return;
        case TYPEENUM:
            left->rtype = TYPE_ENUM(left->rtype)->enumtype;
            break;
        default:
            CError_Error(
                ERR_ILLEGAL_OPERANDS_TYPES,
                left->rtype,
                ENODE_QUALS(left),
                opname,
                right->rtype,
                ENODE_QUALS(right)
            );
            left = nullnode();
    }

    switch (right->rtype->type) {
        case TYPEINT:
            break;
        case TYPEFLOAT:
            CExpr_ArithmeticConversion(leftp, rightp);
            return;
        case TYPEENUM:
            right->rtype = TYPE_ENUM(right->rtype)->enumtype;
            break;
        default:
            CError_Error(
                ERR_ILLEGAL_OPERANDS_TYPES,
                left->rtype,
                ENODE_QUALS(left),
                opname,
                right->rtype,
                ENODE_QUALS(right)
            );
            right = nullnode();
    }

    if (left->rtype == right->rtype) {
        *leftp = left;
        *rightp = right;
        return;
    }

    if (left->rtype->size == right->rtype->size) {
        if (is_unsigned(left->rtype) == is_unsigned(right->rtype)) {
            left->rtype = right->rtype;
            *leftp = left;
            *rightp = right;
            return;
        }
    } else {
        if (ENODE_IS(right, EINTCONST) &&
            left->rtype->size <= right->rtype->size &&
            (is_unsigned(left->rtype) == is_unsigned(right->rtype) || is_unsigned(left->rtype))) {
            val = CMach_CalcIntDiadic(left->rtype, right->data.intval, '+', cint64_zero);
            val = CMach_CalcIntDiadic(right->rtype, val, '+', cint64_zero);
            if (CInt64_Equal(val, right->data.intval)) {
                right->rtype = left->rtype;
                *leftp = left;
                *rightp = right;
                return;
            }
        }

        if (ENODE_IS(left, EINTCONST) &&
            left->rtype->size >= right->rtype->size &&
            (is_unsigned(left->rtype) == is_unsigned(right->rtype) || is_unsigned(right->rtype))) {
            val = CMach_CalcIntDiadic(right->rtype, left->data.intval, '+', cint64_zero);
            val = CMach_CalcIntDiadic(left->rtype, val, '+', cint64_zero);
            if (CInt64_Equal(val, left->data.intval)) {
                left->rtype = right->rtype;
                *leftp = left;
                *rightp = right;
                return;
            }
        }
    }

    *leftp = left;
    *rightp = right;
    CExpr_ArithmeticConversion(leftp, rightp);
}

static ENode* CExpr_ConstResult(ENode* expr, SInt32 value) {
    ENode* constnode;

    if (IS_TYPE_FLOAT(expr->rtype)) {
        constnode = intconstnode(TYPE(&stsignedint), value);
        constnode->type = EFLOATCONST;
        constnode->data.floatval =
            CMach_CalcFloatConvertFromInt(TYPE(&stsignedint), constnode->data.intval);
        constnode->rtype = expr->rtype;
    } else {
        constnode = intconstnode(expr->rtype, value);
    }

    if (CInline_ExpressionHasSideEffect(expr)) {
        return makediadicnode(expr, constnode, ECOMMA);
    } else {
        return constnode;
    }
}

static ENode* makemultnode(ENode* left, ENode* right) {
    CExpr_ArithmeticConversion(&left, &right);
    if (iszero(left)) {
        return CExpr_ConstResult(right, 0);
    }
    if (iszero(right)) {
        return CExpr_ConstResult(left, 0);
    }

    if (CExpr_IsOne(right)) {
        return left;
    }
    if (CExpr_IsOne(left)) {
        return right;
    }

    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, '*', right->data.intval);
        return left;
    }
    if (ENODE_IS(left, EFLOATCONST) && ENODE_IS(right, EFLOATCONST)) {
        left->data.floatval =
            CMach_CalcFloatDiadic(left->rtype, left->data.floatval, '*', right->data.floatval);
        return left;
    }

    left = makediadicnode(left, right, EMUL);
    optimizecomm(left);
    if (IS_TYPE_INT(left->rtype) && left->rtype->size > 2) {
        left->cost++;
        if (left->cost > 200) {
            left->cost = 200;
        }
    }

    if (IS_TYPE_FLOAT(left->rtype)) {
        left = CExpr_BinaryFloatExpression(left);
    }

    left = CodeGen_CheckExpr(left);
    return left;
}

static ENode* makedivnode(ENode* left, ENode* right, Boolean no_warning) {
    CExpr_ArithmeticConversion(&left, &right);
    if (iszero(right) && IS_TYPE_INT(right->rtype)) {
        if (!no_warning) {
            CError_Warning(ERR_DIVISION_BY_0);
        }
        return right;
    }

    if (CExpr_IsOne(right)) {
        return left;
    }

    if (iszero(left) && IS_TYPE_INT(left->rtype)) {
        return CExpr_ConstResult(right, 0);
    }

    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, '/', right->data.intval);
        return left;
    }
    if (ENODE_IS(left, EFLOATCONST) && ENODE_IS(right, EFLOATCONST)) {
        left->data.floatval =
            CMach_CalcFloatDiadic(left->rtype, left->data.floatval, '/', right->data.floatval);
        return left;
    }

    left = makediadicnode(left, right, EDIV);
    if (IS_TYPE_FLOAT(left->rtype)) {
        left = CExpr_BinaryFloatExpression(left);
    }

    left = CodeGen_CheckExpr(left);
    return left;
}

static short canadd2(ENode* expr, CInt64 value) {
    Float inc;

    if (CInt64_IsZero(&value)) {
        return TRUE;
    }

    switch (expr->type) {
        case EINTCONST:
            expr->data.intval = CMach_CalcIntDiadic(expr->rtype, expr->data.intval, '+', value);
            return TRUE;
        case EFLOATCONST:
            inc = CMach_CalcFloatConvertFromInt(TYPE(&stsignedlong), value);
            expr->data.floatval = CMach_CalcFloatDiadic(expr->rtype, expr->data.floatval, '+', inc);
            return TRUE;
        case EADD:
            if (canadd2(expr->data.diadic.left, value)) {
                return TRUE;
            }
            if (canadd2(expr->data.diadic.right, value)) {
                return TRUE;
            }
            return FALSE;
        case ESUB:
            if (canadd2(expr->data.diadic.left, value)) {
                return TRUE;
            }
            if (canadd2(expr->data.diadic.right, CInt64_Neg(value))) {
                return TRUE;
            }
            return FALSE;
        case ETYPCON:
            if (IS_TYPE_POINTER_ONLY(expr->rtype) && ENODE_IS(expr->data.monadic, EINTCONST)) {
                expr->data.monadic->data.intval = CMach_CalcIntDiadic(
                    TYPE(&stunsignedlong), expr->data.monadic->data.intval, '+', value
                );
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }
}

short canadd(ENode* expr, SInt32 value) {
    CInt64 value64;
    CInt64_SetLong(&value64, value);
    return canadd2(expr, value64);
}

static ENode* addconst(ENode* expr, SInt32 value) {
    ENode* right;

    if (canadd(expr, value)) {
        return expr;
    }

    if (stsignedint.size < 4 && (value > 0x7FFF || value < -0x8000)) {
        right = intconstnode(TYPE(&stsignedlong), value);
    } else {
        right = intconstnode(TYPE(&stsignedint), value);
    }

    CExpr_ArithmeticConversion(&expr, &right);
    expr = makediadicnode(expr, right, EADD);
    return expr;
}

static ENode* integralpointerpromote(ENode* expr) {
    Boolean uns;
    Type* type;

    if (!IS_TYPE_INT(expr->rtype)) {
        expr = forceintegral(expr);
    }

    if (expr->rtype->size != 4) {
        type = TYPE(&stunsignedlong);
        if (is_unsigned(type) != (uns = is_unsigned(expr->rtype))) {
            if (uns) {
                if (stunsignedlong.size == 4) {
                    type = TYPE(&stunsignedlong);
                } else if (stunsignedint.size == 4) {
                    type = TYPE(&stunsignedint);
                } else {
                    CError_FATAL(480);
                }
            } else {
                if (stsignedlong.size == 4) {
                    type = TYPE(&stsignedlong);
                } else if (stsignedint.size == 4) {
                    type = TYPE(&stsignedint);
                } else {
                    CError_FATAL(486);
                }
            }
        }

        if (ENODE_IS(expr, EINTCONST)) {
            expr->data.intval = CExpr_IntConstConvert(type, expr->rtype, expr->data.intval);
        } else {
            expr = makemonadicnode(expr, ETYPCON);
        }

        expr->rtype = type;
    }

    return expr;
}

static ENode* padd(ENode* left, ENode* right) {
    Type* innertype;
    SInt32 innersize;
    ENode* expr;

    right = integralpointerpromote(right);
    innertype = TPTR_TARGET(left->rtype);
    innersize = innertype->size;
    if (innersize == 0) {
        CDecl_CompleteType(innertype);
        innersize = innertype->size;
        if (innersize == 0) {
            CError_Error(ERR_ILLEGAL_TYPE);
            return left;
        }
    }

    expr = makemultnode(
        right,
        intconstnode((innersize > 0x7FFF) ? TYPE(&stsignedlong) : TYPE(&stsignedint), innersize)
    );

    if (ENODE_IS(expr, EINTCONST) && canadd2(left, expr->data.intval)) {
        return left;
    }

    if (ENODE_IS(left, EADD) && ENODE_IS(left->data.diadic.right, EINTCONST)) {
        left->data.diadic.left = makediadicnode(left->data.diadic.left, expr, EADD);
        return left;
    }

    expr = makediadicnode(left, expr, EADD);
    expr->rtype = left->rtype;
    expr->flags = left->flags;
    return expr;
}

static ENode* psub(ENode* left, ENode* right) {
    Type* innertype;
    SInt32 innersize;
    ENode* expr;

    if (IS_TYPE_POINTER(right->rtype)) {
        innersize = TPTR_TARGET(left->rtype)->size;
        if (innersize == 0) {
            CDecl_CompleteType(TPTR_TARGET(left->rtype));
            innersize = TPTR_TARGET(left->rtype)->size;
            if (innersize == 0) {
                CError_Error(ERR_ILLEGAL_TYPE);
                return left;
            }
        }

        if (!is_typeequal(left->rtype, right->rtype)) {
            CError_Error(
                ERR_TYPE_MISMATCH, left->rtype, ENODE_QUALS(left), right->rtype, ENODE_QUALS(right)
            );
            return left;
        }

        if (ENODE_IS(left, ETYPCON) &&
            ENODE_IS(left->data.monadic, EINTCONST) &&
            ENODE_IS(right, ETYPCON) &&
            ENODE_IS(right->data.monadic, EINTCONST)) {
            left->data.monadic->rtype = right->data.monadic->rtype = CABI_GetPtrDiffTType();
            expr = makesubnode(left->data.monadic, right->data.monadic);
            if (innersize > 1) {
                expr = makedivnode(expr, intconstnode(CABI_GetPtrDiffTType(), innersize), 11);
            }
            return expr;
        }

        expr = makediadicnode(left, right, ESUB);
        expr->rtype = CABI_GetPtrDiffTType();
        if (innersize > 1) {
            expr = makediadicnode(expr, intconstnode(CABI_GetPtrDiffTType(), innersize), EDIV);
        }
        return expr;
    }

    right = integralpointerpromote(right);
    innertype = TPTR_TARGET(left->rtype);
    innersize = innertype->size;
    if (innersize == 0) {
        CDecl_CompleteType(innertype);
        innersize = innertype->size;
        if (innersize == 0) {
            CError_Error(ERR_ILLEGAL_TYPE);
            return left;
        }
    }

    expr = makemultnode(right, intconstnode(CABI_GetPtrDiffTType(), innersize));
    if (ENODE_IS(expr, EINTCONST) && canadd2(left, CInt64_Neg(expr->data.intval))) {
        return left;
    }

    expr = makediadicnode(left, expr, ESUB);
    expr->rtype = left->rtype;
    expr->flags = left->flags;
    return expr;
}

static ENode* makeaddnode(ENode* left, ENode* right) {
    if (IS_TYPE_POINTER(left->rtype)) {
        return padd(left, right);
    }
    if (IS_TYPE_POINTER(right->rtype)) {
        return padd(right, left);
    }

    CExpr_ArithmeticConversion(&left, &right);
    if (iszero(right)) {
        return left;
    }
    if (iszero(left)) {
        return right;
    }

    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, '+', right->data.intval);
        return left;
    }
    if (ENODE_IS(left, EFLOATCONST) && ENODE_IS(right, EFLOATCONST)) {
        left->data.floatval =
            CMach_CalcFloatDiadic(left->rtype, left->data.floatval, '+', right->data.floatval);
        return left;
    }

    if (ENODE_IS(left, EINTCONST) && canadd2(right, left->data.intval)) {
        return right;
    }
    if (ENODE_IS(right, EINTCONST) && canadd2(left, right->data.intval)) {
        return left;
    }

    left = makediadicnode(left, right, EADD);
    optimizecomm(left);
    if (IS_TYPE_FLOAT(left->rtype)) {
        left = CExpr_BinaryFloatExpression(left);
    }
    left = CodeGen_CheckExpr(left);
    return left;
}

static ENode* makesubnode(ENode* left, ENode* right) {
    if (ENODE_IS(right, EINTCONST) && !is_unsigned(right->rtype) && !IS_TYPE_FLOAT(left->rtype)) {
        right->data.intval = CInt64_Neg(right->data.intval);
        return makeaddnode(left, right);
    }

    if (IS_TYPE_POINTER(left->rtype)) {
        return psub(left, right);
    }

    CExpr_ArithmeticConversion(&left, &right);
    if (iszero(right)) {
        return left;
    }
    if (iszero(left)) {
        if (ENODE_IS(right, EINTCONST)) {
            right->data.intval = CInt64_Neg(right->data.intval);
            return right;
        }
        if (ENODE_IS(right, EFLOATCONST)) {
            right->data.floatval = CMach_CalcFloatMonadic(right->rtype, '-', right->data.floatval);
            return right;
        }
        return CExpr_UnaryFloatExpression(makemonadicnode(right, EMONMIN));
    }

    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, '-', right->data.intval);
        return left;
    }
    if (ENODE_IS(left, EFLOATCONST) && ENODE_IS(right, EFLOATCONST)) {
        left->data.floatval =
            CMach_CalcFloatDiadic(left->rtype, left->data.floatval, '-', right->data.floatval);
        return left;
    }

    if (ENODE_IS(right, EINTCONST) && canadd2(left, CInt64_Neg(right->data.intval))) {
        return left;
    }

    left = makediadicnode(left, right, ESUB);
    if (IS_TYPE_FLOAT(left->rtype)) {
        left = CExpr_BinaryFloatExpression(left);
    }
    left = CodeGen_CheckExpr(left);
    return left;
}

ENode* checkreference(ENode* expr) {
    if (!IS_TYPE_REFERENCE(expr->rtype)) {
        return expr;
    }

    expr = makemonadicnode(expr, EINDIRECT);
    expr->rtype = TPTR_TARGET(expr->rtype);
    return expr;
}

static ENode* pointer_generation2(ENode* expr) {
    switch (expr->type) {
        case EINDIRECT:
            switch (expr->rtype->type) {
                case TYPEARRAY:
                    switch (expr->data.monadic->type) {
                        case EPOSTINC:
                        case EPOSTDEC:
                        case EPREINC:
                        case EPREDEC:
                            expr->type = ETYPCON;
                            expr->rtype = CDecl_NewPointerType(TPTR_TARGET(expr->rtype));
                            return expr;
                        default:
                            expr->data.monadic->rtype =
                                CDecl_NewPointerType(TPTR_TARGET(expr->rtype));
                            expr->data.monadic->flags = expr->flags;
                            return expr->data.monadic;
                    }
                case TYPEFUNC:
                    expr = expr->data.monadic;
                    if (ENODE_IS(expr, EOBJREF) && expr->data.objref->datatype == DINLINEFUNC) {
                        CError_Error(ERR_ILLEGAL_USE_INLINE_FUNCTION);
                    }
                    return expr;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    return expr;
}

ENode* pointer_generation(ENode* expr) {
    return CExpr_RewriteConst(pointer_generation2(expr));
}

ENode* CExpr_PointerGeneration(ENode* expr) {
    switch (expr->type) {
        case EINDIRECT:
            switch (expr->rtype->type) {
                case TYPEARRAY:
                    expr->data.monadic->rtype = CDecl_NewPointerType(TPTR_TARGET(expr->rtype));
                    return expr->data.monadic;
                case TYPEFUNC:
                    return expr->data.monadic;
                default:
                    break;
            }
        default:
            break;
    }
    return expr;
}

static void CExpr_ConstPointerCheck(ENode* expr, Type* type, short qual) {
    Type* exprtype;
    Type* a;
    Type* b;
    short exprqual;

    exprtype = expr->rtype;
    if (IS_TYPE_POINTER_ONLY(type) && IS_TYPE_POINTER_ONLY(exprtype)) {
        exprqual = expr->flags;

        if (TPTR_TARGET(type) == &stvoid) {
            exprqual = CParser_GetCVTypeQualifiers(TPTR_TARGET(exprtype), exprqual);
        } else {
            a = TPTR_TARGET(type);
            b = TPTR_TARGET(exprtype);
            while (IS_TYPE_POINTER_ONLY(a) && IS_TYPE_POINTER_ONLY(b)) {
                if (CParser_IsMoreCVQualified(TPTR_QUAL(b), TPTR_QUAL(a))) {
                    CError_Warning(
                        ERR_ILLEGAL_IMPLICIT_CONST_VOLATILE_POINTER_CONVERSION,
                        expr->rtype,
                        ENODE_QUALS(expr),
                        type,
                        qual
                    );
                    return;
                }
                a = TPTR_TARGET(a);
                b = TPTR_TARGET(b);
            }
        }

        if (CParser_IsMoreCVQualified(exprqual, qual)) {
            CError_Warning(
                ERR_ILLEGAL_IMPLICIT_CONST_VOLATILE_POINTER_CONVERSION,
                expr->rtype,
                ENODE_QUALS(expr),
                type,
                (UInt32)qual
            );
        }
    }
}

ENode* oldassignmentpromotion(ENode* expr, Type* type, short qual, Boolean checkAccess) {
    Boolean is_ref;
    UInt32 ref_qual;
    SInt32 orig_qual;

    is_ref = FALSE;
    if (!IS_TYPE_MEMBERPOINTER(type)) {
        if (IS_TYPE_REFERENCE(type)) {
            if (ENODE_IS(expr, ECOND)) {
                expr = CExpr_LValue(expr, FALSE, FALSE);
            }
            expr = pointer_generation2(expr);
            ref_qual = CParser_GetCVTypeQualifiers(expr->rtype, expr->flags);
            is_ref = TRUE;
        } else {
            expr = pointer_generation(expr);
        }
    }

    if (ENODE_IS(expr, EMEMBER)) {
        expr = getpointertomemberfunc(expr, type, TRUE);
    }

    if (!is_ref) {
        CExpr_ConstPointerCheck(expr, type, qual);
    } else {
        CExpr_ConstPointerCheck(expr, TPTR_TARGET(type), qual);
    }

    if (!(assign_check(expr, type, orig_qual = qual, TRUE, FALSE, checkAccess))) {
        return expr;
    }

    if (is_ref) {
        if (temp_reference_init) {
            switch (TPTR_TARGET(type)->type) {
                case TYPEPOINTER:
                    qual = TPTR_QUAL(TPTR_TARGET(type));
                default:
                    break;
            }
            if (!(qual & Q_CONST)) {
                CError_Warning(ERR_NON_CONST_AMPERSAND_REFERENCE_INITIALIZED_TEMPORARY);
            }
        } else {
            if (ref_qual && ref_qual > CParser_GetCVTypeQualifiers(TPTR_TARGET(type), orig_qual)) {
                CError_Warning(ERR_ILLEGAL_CONST_VOLATILE_AMPERSAND_REFERENCE_INITIALIZATION);
            }
        }
    }

    return assign_node;
}

ENode* argumentpromotion(ENode* expr, Type* type, short qual, Boolean checkAccess) {
    ENode* constructed;
    ENodeList* list;

    if (IS_TYPE_CLASS(type) && CClass_ReferenceArgument(TYPE_CLASS(type))) {
        if ((constructed = CExpr_IsTempConstruction(expr, type, NULL))) {
            return constructed;
        }

        list = lalloc(sizeof(ENodeList));
        list->next = NULL;
        list->node = expr;
        return getnodeaddress(
            CExpr_ConstructObject(
                TYPE_CLASS(type), create_temp_node(type), list, TRUE, TRUE, TRUE, TRUE, FALSE
            ),
            FALSE
        );
    }

    if (CMach_PassResultInHiddenArg(type)) {
        ENode* temp;
        ENode* tempcopy;
        ENode* indirect;
        ENode* result;

        expr = CExpr_AssignmentPromotion(expr, type, qual, FALSE);
        temp = CExpr_NewETEMPNode(type, TRUE);
        tempcopy = lalloc(sizeof(ENode));
        *tempcopy = *temp;
        indirect = makemonadicnode(temp, EINDIRECT);
        indirect->rtype = type;
        result = makediadicnode(indirect, expr, EASS);
        result = makediadicnode(result, tempcopy, ECOMMA);
        result->rtype = tempcopy->rtype;
        return result;
    }

    return CExpr_AssignmentPromotion(expr, type, qual, checkAccess);
}

ENode* classargument(ENode* expr) {
    ENodeList* list;

    if (CClass_CopyConstructor(TYPE_CLASS(expr->rtype))) {
        list = lalloc(sizeof(ENodeList));
        list->next = NULL;
        list->node = expr;
        return CExpr_ConstructObject(
            TYPE_CLASS(expr->rtype),
            create_temp_node(expr->rtype),
            list,
            TRUE,
            TRUE,
            TRUE,
            TRUE,
            FALSE
        );
    }

    return expr;
}

ENodeList* CExpr_ScanExpressionList(Boolean is_parens) {
    ENodeList* list;
    ENodeList* current;

    if (is_parens && tk == ')') {
        return NULL;
    }

    list = current = lalloc(sizeof(ENodeList));

    while (1) {
        current->next = NULL;
        current->node = assignment_expression();
        if (copts.old_argmatch && !ENODE_IS(current->node, EMEMBER)) {
            current->node = pointer_generation(current->node);
        }

        if (is_parens) {
            if (tk == ')') {
                break;
            }
        } else {
            if (tk == ']') {
                break;
            }
        }

        if (tk != ',') {
            CError_ErrorSkip(ERR_COMMA_EXPECTED);
            break;
        }

        tk = lex();
        current->next = lalloc(sizeof(ENodeList));
        current = current->next;
    }

    return list;
}

static ENode* skipcommaexpr(ENode* expr) {
    while (ENODE_IS(expr, ECOMMA)) {
        expr = expr->data.diadic.right;
    }
    return expr;
}

ENode* CExpr_DoExplicitConversion(Type* type, UInt32 qual, ENodeList* list) {
    Object* obj;
    ENode* tempnode;

    if (!IS_TYPE_CLASS(type)) {
        if (!list) {
            return do_typecast(nullnode(), type, qual);
        }
        if (list->next) {
            CError_Error(ERR_MORE_THAN_ONE_EXPRESSION_NON_CLASS);
        }
        return do_typecast(pointer_generation(list->node), type, qual);
    }

    CDecl_CompleteType(type);
    if (!(TYPE_CLASS(type)->flags & CLASS_COMPLETED)) {
        CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, type, 0);
    }

    CanCreateObject(type);
    if (!list && CClass_IsPODClass(TYPE_CLASS(type))) {
        obj = CParser_NewGlobalDataObject(NULL);
        obj->name = CParser_GetUniqueName();
        obj->nspace = cscope_root;
        obj->type = type;
        obj->qual = qual;
        obj->sclass = TK_STATIC;
        CInit_DeclareData(obj, NULL, NULL, type->size);

        tempnode = makemonadicnode(create_temp_node(type), EINDIRECT);
        tempnode->rtype = type;
        tempnode->flags = qual & ENODE_FLAG_QUALS;
        return makediadicnode(tempnode, create_objectnode(obj), EASS);
    }

    return CExpr_ConstructObject(
        TYPE_CLASS(type), create_temp_node(type), list, TRUE, TRUE, TRUE, TRUE, TRUE
    );
}

static ENode* CExpr_TemplArgDepCast(Type* type, UInt32 qual, ENodeList* args) {
    ENode* expr = CExpr_NewTemplDepENode(TDE_CAST);
    expr->data.templdep.u.cast.args = args;
    expr->data.templdep.u.cast.type = type;
    expr->data.templdep.u.cast.qual = qual;
    return expr;
}

static ENode* CExpr_ParseExplicitConversion(Type* type, UInt32 qual) {
    ENodeList* args;
    ENodeList* scan;

    if (IS_TEMPL_CLASS(type) && !CParser_CheckTemplateClassUsage(TEMPL_CLASS(type), TRUE)) {
        type = TYPE(&stsignedint);
    }

    if (tk == '(') {
        tk = lex();
    } else {
        CError_Error(ERR_LPAREN_EXPECTED);
    }

    args = CExpr_ScanExpressionList(TRUE);
    if (tk != ')') {
        CError_Error(ERR_RPAREN_EXPECTED);
        return nullnode();
    }

    tk = lex();
    if (CTemplTool_IsTemplateArgumentDependentType(type)) {
        return CExpr_TemplArgDepCast(type, qual, args);
    }

    for (scan = args; scan; scan = scan->next) {
        if (CTemplTool_IsTemplateArgumentDependentExpression(scan->node)) {
            return CExpr_TemplArgDepCast(type, qual, args);
        }
    }

    return CExpr_DoExplicitConversion(type, qual, args);
}

static ENode* CExpr_MemberVarAccess(BClassList* path, ObjMemberVar* var, ENode* expr) {
    ENode* accessnode;
    BClassList* varpath;

    CError_ASSERT(1152, path);

    if (TYPE_CLASS(path->type)->sominfo) {
        return CSOM_MemberVarAccess(path, var, expr);
    }

    varpath = NULL;
    if (var->has_path) {
        varpath = OBJ_MEMBER_VAR_PATH(var)->path;
    }
    accessnode = CExpr_GetClassAccessNode(path, varpath, expr, NULL, var->access, TRUE);
    if (!accessnode) {
        return nullnode();
    }

    return CClass_AccessMember(accessnode, var->type, var->qual, var->offset);
}

static Boolean CExpr_IsTemplateFunc(Object* obj) {
    return IS_TEMPL_FUNC(obj->type);
}

static ENode* CExpr_ExplicitTemplateArgCheck(NameResult* pr) {
    NameSpaceObjectList* list;
    NameSpaceObjectList* newhead;
    NameSpaceObjectList* newlist;
    ENode* expr;

    if (pr->object) {
        if (pr->object->otype != OT_OBJECT || !CExpr_IsTemplateFunc(OBJECT(pr->object))) {
            return NULL;
        }

        list = lalloc(sizeof(NameSpaceObjectList));
        memclrw(list, sizeof(NameSpaceObjectList));
        list->object = pr->object;
    } else if (pr->objects) {
        for (list = pr->objects; list; list = list->next) {
            if (list->object->otype == OT_OBJECT && CExpr_IsTemplateFunc(OBJECT(list->object))) {
                newhead = newlist = galloc(sizeof(NameSpaceObjectList));
                *newlist = *list;
                newlist->next = NULL;
                while ((list = list->next)) {
                    if (list->object->otype == OT_OBJECT &&
                        CExpr_IsTemplateFunc(OBJECT(list->object))) {
                        newlist->next = galloc(sizeof(NameSpaceObjectList));
                        newlist = newlist->next;
                        *newlist = *list;
                        newlist->next = NULL;
                    }
                }
                list = newhead;
                break;
            }
        }
        if (!list) {
            return NULL;
        }
    } else {
        return NULL;
    }

    expr = CExpr_NewENode(EOBJLIST);
    expr->rtype = OBJECT(list->object)->type;
    expr->data.objlist.list = list;
    expr->data.objlist.templargs = CTempl_ParseUncheckTemplArgs(NULL, FALSE);

    tk = lex();
    return expr;
}

ENode* CExpr_MakeNameLookupResultExpr(NameResult* pr) {
    ENode* expr;

    if (pr->object) {
        switch (pr->object->otype) {
            case OT_OBJECT:
                CClass_CheckObjectAccess(pr->basePath, OBJECT(pr->object));
                return create_objectnode(OBJECT(pr->object));
            case OT_ENUMCONST:
                CClass_CheckEnumAccess(pr->basePath, OBJ_ENUM_CONST(pr->object));
                expr = lalloc(sizeof(ENode));
                expr->type = EINTCONST;
                expr->cost = 0;
                expr->flags = 0;
                expr->rtype = OBJ_ENUM_CONST(pr->object)->type;
                expr->data.intval = OBJ_ENUM_CONST(pr->object)->val;
                return expr;
            case OT_MEMBERVAR:
                CError_Error(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
                return nullnode();
            default:
                CError_FATAL(1268);
        }
    }

    if (pr->objects) {
        expr = CExpr_NewENode(EOBJLIST);
        expr->rtype = OBJECT(pr->objects->object)->type;
        expr->data.objlist.list = pr->objects;
        return expr;
    }

    CError_FATAL(1278);
    return NULL;
}

static Type* CExpr_NewPTMType(EMemberInfo* member, Object* obj) {
    TypeMemberPointer* ptm;
    TypeMemberFunc* tmethod;
    BClassList* path;

    ptm = galloc(sizeof(TypeMemberPointer));
    memclrw(ptm, sizeof(TypeMemberPointer));
    ptm->type = TYPEMEMBERPOINTER;

    if (member->list->object->otype == OT_MEMBERVAR) {
        path = member->path;
        while (path->next) {
            path = path->next;
        }
        ptm->size = 4;
        ptm->ty2 = path->type;
        ptm->ty1 = OBJ_MEMBER_VAR(member->list->object)->type;
    } else {
        if (!obj) {
            CError_ASSERT(1306, member->list->object->otype == OT_OBJECT);
            obj = OBJECT(member->list->object);
            CError_ASSERT(1308, IS_TYPE_FUNC(obj->type));
        }

        tmethod = galloc(sizeof(TypeMemberFunc));
        memclrw(tmethod, sizeof(TypeMemberFunc));
        *tmethod = *TYPE_METHOD(obj->type);

        CError_ASSERT(1312, tmethod->args);

        tmethod->args = tmethod->args->next;
        CDecl_MakePTMFuncType(TYPE_FUNC(tmethod));
        tmethod->flags &= ~FUNC_DEFINED;

        ptm->size = 12;
        ptm->ty2 = TYPE(tmethod->theclass);
        ptm->ty1 = TYPE(tmethod);
    }

    return TYPE(ptm);
}

static ENode* CExpr_ParseNameResultExpr(
    NameResult* pr, ENode* expr, Boolean isAddressOf, Boolean allowObjList
) {
    ENode* result;
    ENode* ta_expr;
    TemplateAction* act;
    EMemberInfo* member;
    NameSpaceObjectList* list;
    Object* obj;
    TypeFunc* tfunc;
    SInt32 val;
    TypeEnum* tenum;

    if (pr->type) {
        if (copts.cplusplus) {
            if (IS_TYPE_TEMPLATE(pr->type)) {
                if (TYPE_TEMPLATE(pr->type)->dtype == TEMPLDEP_ARGUMENT &&
                    TYPE_TEMPLATE(pr->type)->u.pid.type == TPT_NONTYPE) {
                    ta_expr = CExpr_NewTemplDepENode(TDE_PARAM);
                    ta_expr->data.templdep.u.pid = TYPE_TEMPLATE(pr->type)->u.pid;
                    tk = lex();
                    return ta_expr;
                }
                if (TYPE_TEMPLATE(pr->type)->dtype == TEMPLDEP_QUALNAME && !pr->is_type) {
                    ta_expr = CExpr_NewTemplDepENode(TDE_QUALNAME);
                    ta_expr->data.templdep.u.qual.type = TYPE_TEMPLATE(pr->type)->u.qual.type;
                    ta_expr->data.templdep.u.qual.name = TYPE_TEMPLATE(pr->type)->u.qual.name;
                    tk = lex();
                    return ta_expr;
                }
            }
            tk = lex();
            return CExpr_ParseExplicitConversion(pr->type, pr->qual);
        }
        CError_ErrorSkip(ERR_EXPRESSION_SYNTAX_ERROR);
        tk = lex();
        return nullnode();
    }

    if (pr->object) {
        switch (pr->object->otype) {
            case OT_OBJECT:
                if (OBJECT(pr->object)->nspace &&
                    OBJECT(pr->object)->nspace->theclass &&
                    (OBJECT(pr->object)->nspace->theclass->flags & CLASS_IS_TEMPL)) {
                    result = CExpr_NewTemplDepENode(TDE_OBJ);
                    result->data.templdep.u.obj = OBJECT(pr->object);
                    tk = lex();
                    return result;
                }
                if (!expr || !IS_TEMPL_FUNC(OBJECT(pr->object)->type)) {
                    if (!IS_TYPE_NONSTATIC_METHOD(OBJECT(pr->object)->type)) {
                        tk = lex();
                        if (tk == '<' && (ta_expr = CExpr_ExplicitTemplateArgCheck(pr))) {
                            return ta_expr;
                        }
                        if (OBJECT(pr->object)->datatype == DLOCAL &&
                            OBJECT(pr->object)->u.var.info->func != cscope_currentfunc) {
                            CError_Error(ERR_ILLEGAL_ACCESS_LOCAL_VARIABLE_FROM_OTHER);
                            return nullnode();
                        }
                        if (OBJECT(pr->object)->datatype == DEXPR) {
                            result =
                                CInline_CopyExpression(OBJECT(pr->object)->u.expr, CopyModeLocal);
                            if (IS_TYPE_POINTER_ONLY((Type*)(expr = (ENode*)result->rtype)) &&
                                ENODE_IS(result, EINTCONST)) {
                                result = makemonadicnode(result, ETYPCON);
                                result->data.monadic->rtype = TYPE(&stunsignedlong);
                            }
                            return result;
                        }
                        CClass_CheckObjectAccess(pr->basePath, OBJECT(pr->object));
                        if (tk == '(' &&
                            allowObjList &&
                            OBJECT(pr->object)->datatype == DFUNC &&
                            copts.cplusplus &&
                            !pr->is_qualified &&
                            !IS_TYPEFUNC_METHOD(TYPE_FUNC(OBJECT(pr->object)->type))) {
                            expr = CExpr_NewENode(EOBJLIST);
                            expr->rtype = OBJECT(pr->object)->type;
                            expr->data.objlist.list = galloc(sizeof(NameSpaceObjectList));
                            expr->data.objlist.list->next = pr->objects;
                            expr->data.objlist.list->object = pr->object;
                            expr->data.objlist.name = OBJECT(pr->object)->name;
                            return expr;
                        }

                        result = create_objectnode(OBJECT(pr->object));
                        if (expr) {
                            while (ENODE_IS(expr, EINDIRECT)) {
                                expr = expr->data.monadic;
                            }
                            switch (expr->type) {
                                case EINTCONST:
                                case EOBJREF:
                                case EOBJLIST:
                                    break;
                                default:
                                    result = makecommaexpression(expr, result);
                            }
                        }
                        return result;
                    }
                    if (CClass_IsDestructor(OBJECT(pr->object))) {
                        if ((tk = lex()) != '(') {
                            CError_Error(ERR_LPAREN_EXPECTED);
                            return nullnode();
                        }
                        if ((tk = lex()) != ')') {
                            CError_Error(ERR_RPAREN_EXPECTED);
                            return nullnode();
                        }
                        if (!expr &&
                            (!cscope_currentfunc ||
                             !cscope_currentclass ||
                             !cscope_is_member_func)) {
                            CError_Error(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
                            return nullnode();
                        }
                        if (pr->isambig) {
                            CError_Error(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
                        }

                        if ((expr = CExpr_GetClassAccessNode(
                                 pr->basePath,
                                 NULL,
                                 expr,
                                 OBJECT(pr->object),
                                 pr->object->access,
                                 TRUE
                             ))) {
                            tk = lex();
                            return CABI_DestroyObject(
                                OBJECT(pr->object),
                                expr->data.monadic,
                                CABIDestroyComplete,
                                pr->is_qualified,
                                FALSE
                            );
                        }
                    }
                }
                break;
            case OT_ENUMCONST:
                CClass_CheckEnumAccess(pr->basePath, OBJ_ENUM_CONST(pr->object));
                if (CInt64_IsZero(&OBJ_ENUM_CONST(pr->object)->val) &&
                    IS_TYPE_ENUM(OBJ_ENUM_CONST(pr->object)->type) &&
                    (tenum = TYPE_ENUM(OBJ_ENUM_CONST(pr->object)->type))->nspace &&
                    tenum->nspace->theclass &&
                    (tenum->nspace->theclass->flags & CLASS_IS_TEMPL)) {
                    val = 0;
                    expr = NULL;
                    for (act = TEMPL_CLASS(tenum->nspace->theclass)->actions; act;
                         act = act->next) {
                        if (act->type == TAT_ENUMERATOR) {
                            if (act->u.enumerator.initexpr) {
                                expr = act->u.enumerator.initexpr;
                                val = 0;
                            } else {
                                val++;
                            }
                            if (act->u.enumerator.objenumconst == OBJ_ENUM_CONST(pr->object)) {
                                CError_ASSERT(1521, expr);
                                expr = CInline_CopyExpression(expr, CopyModeLocal);
                                if (val) {
                                    expr = makediadicnode(
                                        expr, intconstnode(TYPE(&stsignedlong), val), EADD
                                    );
                                }
                                tk = lex();
                                return expr;
                            }
                        }
                    }
                }
                result = lalloc(sizeof(ENode));
                result->type = EINTCONST;
                result->cost = 0;
                result->flags = 0;
                result->rtype = OBJ_ENUM_CONST(pr->object)->type;
                result->data.intval = OBJ_ENUM_CONST(pr->object)->val;
                tk = lex();
                return result;
            case OT_MEMBERVAR:
                if (!isAddressOf || expr || !pr->is_qualified) {
                    if (pr->isambig) {
                        CError_Error(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
                    }
                    result = checkreference(
                        CExpr_MemberVarAccess(pr->basePath, OBJ_MEMBER_VAR(pr->object), expr)
                    );
                    tk = lex();
                    return result;
                }
                break;
            default:
                CError_FATAL(1552);
        }

        member = lalloc(sizeof(EMemberInfo));
        memclrw(member, sizeof(EMemberInfo));
        member->path = pr->basePath;
        member->expr = expr;
        member->is_qualified = pr->is_qualified;
        member->isambig = pr->isambig;
        if ((tk = lex()) == '<' && (ta_expr = CExpr_ExplicitTemplateArgCheck(pr))) {
            CError_ASSERT(1564, ENODE_IS(ta_expr, EOBJLIST));
            member->list = ta_expr->data.objlist.list;
            member->templargs = ta_expr->data.objlist.templargs;
        } else {
            member->list = galloc(sizeof(NameSpaceObjectList));
            member->list->next = NULL;
            member->list->object = pr->object;
        }

        result = CExpr_NewENode(EMEMBER);
        result->data.emember = member;
        result->rtype = &stvoid;
        return result;
    }

    if (pr->objects) {
        if ((tk = lex()) == '<' && (ta_expr = CExpr_ExplicitTemplateArgCheck(pr))) {
            CError_ASSERT(1591, ENODE_IS(ta_expr, EOBJLIST));
            for (list = ta_expr->data.objlist.list; list; list = list->next) {
                if (list->object->otype == OT_OBJECT &&
                    IS_TYPE_NONSTATIC_METHOD(OBJECT(list->object)->type)) {
                    member = lalloc(sizeof(EMemberInfo));
                    memclrw(member, sizeof(EMemberInfo));

                    member->path = pr->basePath;
                    member->expr = expr;
                    member->list = ta_expr->data.objlist.list;
                    member->templargs = ta_expr->data.objlist.templargs;
                    member->is_qualified = pr->is_qualified;
                    member->isambig = pr->isambig;

                    result = CExpr_NewENode(EMEMBER);
                    result->data.emember = member;
                    result->rtype = &stvoid;
                    return result;
                }
            }
            return ta_expr;
        }

        for (list = pr->objects; list; list = list->next) {
            if (list->object->otype == OT_OBJECT &&
                IS_TYPE_NONSTATIC_METHOD(OBJECT(list->object)->type)) {
                member = lalloc(sizeof(EMemberInfo));
                memclrw(member, sizeof(EMemberInfo));

                member->path = pr->basePath;
                member->expr = expr;
                member->list = pr->objects;
                member->is_qualified = pr->is_qualified;
                member->isambig = pr->isambig;

                result = CExpr_NewENode(EMEMBER);
                result->data.emember = member;
                result->rtype = &stvoid;
                return result;
            }
        }

        if (tk == '<' && (ta_expr = CExpr_ExplicitTemplateArgCheck(pr))) {
            return ta_expr;
        }

        result = CExpr_NewENode(EOBJLIST);
        result->rtype = OBJECT(pr->objects->object)->type;
        result->data.objlist.list = pr->objects;
        if (tk == '(' &&
            copts.cplusplus &&
            allowObjList &&
            !pr->is_qualified &&
            pr->objects->object->otype == OT_OBJECT) {
            result->data.objlist.name = OBJECT(pr->objects->object)->name;
        }
        return result;
    }

    if (pr->name) {
        if (copts.cplusplus && allowObjList) {
            if (lookahead() == '(') {
                result = CExpr_NewENode(EOBJLIST);
                result->rtype = &stvoid;
                result->data.objlist.name = pr->name;
                tk = lex();
                return result;
            }
            CError_Error(ERR_UNDEFINED_IDENTIFIER, pr->name->name);
            tk = lex();
            return nullnode();
        }

        if (lookahead() != '(') {
            CError_Error(ERR_UNDEFINED_IDENTIFIER, pr->name->name);
            tk = lex();
            return nullnode();
        }

        if (copts.checkprotos) {
            CError_Error(ERR_FUNCTION_NO_PROTOTYPE);
        }

        tfunc = galloc(sizeof(TypeFunc));
        memclrw(tfunc, sizeof(TypeFunc));
        tfunc->type = TYPEFUNC;
        tfunc->functype = TYPE(&stsignedint);
        tfunc->args = &oldstyle;
        CDecl_SetFuncFlags(tfunc, 0);

        obj = CParser_NewFunctionObject(NULL);
        obj->name = pr->name;
        obj->sclass = TK_EXTERN;
        obj->nspace = cscope_root;
        obj->type = TYPE(tfunc);

        CScope_AddGlobalObject(obj);
        tk = lex();
        return create_objectrefnode(obj);
    }

    CError_FATAL(1711);
    return NULL;
}

static ENode* CExpr_ParseRotate(Boolean is_right) {
    ENode* expr1;
    ENode* expr2;

    if (lex() != '(') {
        CError_Error(ERR_LPAREN_EXPECTED);
        return nullnode();
    }

    tk = lex();
    expr1 = assignment_expression();

    if (tk != ',') {
        CError_Error(ERR_COMMA_EXPECTED);
        return nullnode();
    }

    tk = lex();
    expr2 = assignment_expression();

    if (tk != ')') {
        CError_Error(ERR_RPAREN_EXPECTED);
        return nullnode();
    }

    if (!IS_TYPE_INT(expr1->rtype)) {
        expr1 = forceintegral(expr1);
    }
    expr2 = integralpromote(expr2);

    tk = lex();
    if (iszero(expr1) || iszero(expr2)) {
        return expr1;
    }

    return makediadicnode(expr1, expr2, is_right ? EROTR : EROTL);
}

static ENode* CExpr_ParseNextArg(void) {
    NameSpaceObjectList* list;
    NameResult pr;
    ENode* expr;
    SInt32 rounded_size;

    if ((tk = lex()) != '(') {
        CError_Error(ERR_LPAREN_EXPECTED);
        return nullnode();
    }
    if ((tk = lex()) != TK_IDENTIFIER) {
        CError_Error(ERR_IDENTIFIER_EXPECTED);
        return nullnode();
    }

    list = CScope_FindObjectList(&pr, tkidentifier);
    if (!list) {
        CError_Error(ERR_UNDEFINED_IDENTIFIER, tkidentifier->name);
        return nullnode();
    }

    if (list->object->otype != OT_OBJECT || OBJECT(list->object)->datatype != DLOCAL) {
        CError_Error(ERR_UNDEFINED_IDENTIFIER, tkidentifier->name);
        return nullnode();
    }

    rounded_size = CMach_RoundedSizeOf(OBJECT(list->object));
    expr = CExpr_MakeObjRefNode(OBJECT(list->object), TRUE);
    expr = makediadicnode(expr, intconstnode(CABI_GetPtrDiffTType(), rounded_size), EADD);
    expr->rtype = TYPE(&void_ptr);

    if ((tk = lex()) != ')') {
        CError_Error(ERR_RPAREN_EXPECTED);
        return expr;
    } else {
        tk = lex();
        return expr;
    }
}

static ENode* CExpr_ParseVecStep(void) {
    ENode* expr;
    Type* type;
    SInt32 value;
    DeclInfo di;

    expr = intconstnode(TYPE(&stsignedint), 0);
    if ((tk = lex()) == '(') {
        if (tk == '(' && islookaheaddeclaration()) {
            tk = lex();
            memclrw(&di, sizeof(DeclInfo));
            CParser_GetDeclSpecs(&di, FALSE);
            scandeclarator(&di);
            if (di.name) {
                CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            }

            if (tk != ')') {
                CError_ErrorSkip(ERR_RPAREN_EXPECTED);
            } else {
                tk = lex();
            }

            type = di.thetype;
            if (IS_TYPE_REFERENCE(type)) {
                type = TPTR_TARGET(type);
            }
        } else {
            expr = unary_expression();
            if (ENODE_IS(expr, EINDIRECT) && ENODE_IS(expr->data.monadic, EBITFIELD)) {
                CError_Error(ERR_ILLEGAL_OPERAND);
            }
            type = expr->rtype;
        }

        CDecl_CompleteType(type);
        if (IS_TYPE_VECTOR(type)) {
            switch (TYPE_STRUCT(type)->stype) {
                case STRUCT_VECTOR_UCHAR:
                case STRUCT_VECTOR_SCHAR:
                case STRUCT_VECTOR_BCHAR:
                    value = 16;
                    break;
                case STRUCT_VECTOR_USHORT:
                case STRUCT_VECTOR_SSHORT:
                case STRUCT_VECTOR_BSHORT:
                case STRUCT_VECTOR_PIXEL:
                    value = 8;
                    break;
                default:
                    value = 4;
            }
            expr = intconstnode(TYPE(&stsignedint), value);
        } else {
            PPCError_Error(PPCERR_ALTIVEC_INVALID_ARGUMENT, "vec_step", "vec_step", type, 0);
        }
    } else {
        CError_Error(ERR_LPAREN_EXPECTED);
    }

    return expr;
}

static SInt32 CExpr_BuiltInComputeAlign(Type* type) {
    return CMach_GetTypeAlign(type);
}

static SInt32 CExpr_AtomTypeID(UInt8 integral) {
    switch (integral) {
        case IT_BOOL:
            return 1;
        case IT_CHAR:
            return 2;
        case IT_SCHAR:
            return 3;
        case IT_UCHAR:
            return 4;
        case IT_WCHAR_T:
            return 5;
        case IT_SHORT:
            return 6;
        case IT_USHORT:
            return 7;
        case IT_INT:
            return 8;
        case IT_UINT:
            return 9;
        case IT_LONG:
            return 10;
        case IT_ULONG:
            return 10;
        case IT_LONGLONG:
            return 12;
        case IT_ULONGLONG:
            return 13;
        case IT_FLOAT:
            return 14;
        case IT_SHORTDOUBLE:
            return 15;
        case IT_DOUBLE:
            return 16;
        case IT_LONGDOUBLE:
            return 17;
        case IT_17:
            return 32;
        case IT_18:
            return 33;
        case IT_VEC2X32FLOAT:
            return 34;
        case IT_20:
            return 35;
        case IT_21:
            return 36;
        case IT_22:
            return 37;
        case IT_23:
            return 38;
        case IT_24:
            return 39;
        default:
            CError_FATAL(1976);
            return 0;
    }
}

static SInt32 CExpr_BuiltInComputeType(Type* type) {
    switch (type->type) {
        case TYPEINT:
            return 0x100 | CExpr_AtomTypeID(TYPE_INTEGRAL(type)->integral);
        case TYPEFLOAT:
            return 0x200 | CExpr_AtomTypeID(TYPE_INTEGRAL(type)->integral);
        case TYPEENUM:
            return 0x400 | (CExpr_BuiltInComputeType(TYPE_ENUM(type)->enumtype) & 0xFF);
        case TYPEPOINTER:
            return 0x800;
        case TYPEARRAY:
            return 0x1000;
        case TYPESTRUCT:
            return 0x2000;
        case TYPECLASS:
            return 0x2000;
        case TYPEMEMBERPOINTER:
            return 0x4000;
        case TYPEFUNC:
            return 0x8000;
        default:
            CError_Error(ERR_ILLEGAL_TYPE);
        case TYPEVOID:
            return 0;
    }
}

static SInt32 CExpr_BuiltInClassifyType(Type* type) {
    switch (type->type) {
        case TYPEVOID:
            return 0;
        case TYPEFUNC:
            return 10;
        case TYPEENUM:
            return 3;
        case TYPEINT:
            return 1;
        case TYPEFLOAT:
            return 8;
        case TYPEPOINTER:
        case TYPEMEMBERPOINTER:
            return 5;
        case TYPEARRAY:
            return 14;
        case TYPESTRUCT:
            return 12;
        case TYPECLASS:
            return 12;
        case TYPEBITFIELD:
            return -1;
        case TYPETEMPLATE:
            return -1;
        default:
            return -1;
    }
}

static SInt32 CExpr_BuiltInComputeVArgType(Type* type) {
    SInt32 result;

    switch (type->type) {
        case TYPESTRUCT:
            if (IS_TYPE_VECTOR(type)) {
                result = 4;
                break;
            }
        case TYPECLASS:
            result = 0;
            break;
        case TYPEFLOAT:
            if (copts.soft_float) {
                if (TYPE_INTEGRAL(type)->integral == IT_FLOAT) {
                    result = 1;
                } else if (copts.incompatible_sfpe_double_params) {
                    result = 4;
                } else {
                    result = 2;
                }
            } else {
                result = 3;
            }
            break;
        case TYPEINT:
        case TYPEENUM:
            if (TYPE_INTEGRAL(type)->integral == IT_LONGLONG ||
                TYPE_INTEGRAL(type)->integral == IT_ULONGLONG) {
                result = 2;
            } else {
                result = 1;
            }
            break;
        default:
            result = 1;
            break;
    }
    return result;
}

static Type* CExpr_ParseTypeExpression(Boolean* isZero) {
    ENode* expr;
    Type* type;
    DeclInfo di;

#if VERSION >= VERSION_GC_2_0
    if (isZero) {
        *isZero = FALSE;
    }
#endif
    tk = lex();
    if (tk == '(' && islookaheaddeclaration()) {
        tk = lex();
        memclrw(&di, sizeof(DeclInfo));
        CParser_GetDeclSpecs(&di, FALSE);
        scandeclarator(&di);
        if (di.name) {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        }

        if (tk != ')') {
            CError_ErrorSkip(ERR_RPAREN_EXPECTED);
        } else {
            tk = lex();
        }

        type = di.thetype;
        if (IS_TYPE_REFERENCE(type)) {
            type = TPTR_TARGET(type);
        }
    } else {
        expr = unary_expression();
        if (ENODE_IS(expr, EINDIRECT) && ENODE_IS(expr->data.monadic, EBITFIELD)) {
            CError_Error(ERR_ILLEGAL_OPERAND);
        }

        if (isZero) {
            *isZero = ENODE_IS(expr, EINTCONST) && CInt64_IsZero(&expr->data.intval);
        }

        type = expr->rtype;
    }

    CDecl_CompleteType(type);
    return type;
}

static ENode* CExpr_ParseBuiltin(SInt32 (*parser)(Type*)) {
    ENode* expr;
    expr = intconstnode(TYPE(&stsignedint), 0);
    CInt64_SetLong(&expr->data.intval, parser(CExpr_ParseTypeExpression(NULL)));
    return expr;
}

static ENode* CExpr_ParseBuiltin_isintconst(void) {
    ENode* expr;
    expr = intconstnode(TYPE(&stsignedint), 0);

    tk = lex();
    if (tk != '(') {
        CError_ErrorSkip(ERR_DECLARATION_SYNTAX_ERROR);
    } else {
        tk = lex();
    }

    if (ENODE_IS(expression(), EINTCONST)) {
        CInt64_SetLong(&expr->data.intval, 1);
    }

    if (tk != ')') {
        CError_ErrorSkip(ERR_RPAREN_EXPECTED);
    } else {
        tk = lex();
    }

    return expr;
}

static ENode* primary_expression(Boolean isAddressOf) {
    NameResult pr;
    ENode* expr;

    switch (tk) {
        case TK_TRUE:
            expr = lalloc(sizeof(ENode));
            expr->type = EINTCONST;
            expr->cost = 0;
            expr->flags = 0;
            expr->rtype = TYPE(&stbool);
            CInt64_SetULong(&expr->data.intval, 1);
            tk = lex();
            return expr;
        case TK_FALSE:
            expr = lalloc(sizeof(ENode));
            expr->type = EINTCONST;
            expr->cost = 0;
            expr->flags = 0;
            expr->rtype = TYPE(&stbool);
            CInt64_SetULong(&expr->data.intval, 0);
            tk = lex();
            return expr;
        case TK_INTCONST:
            expr = lalloc(sizeof(ENode));
            expr->type = EINTCONST;
            expr->cost = 0;
            expr->flags = 0;
            expr->rtype = TYPE(atomtype());
            expr->data.intval = tkintconst;
            tk = lex();
            return expr;
        case TK_FLOATCONST:
            expr = lalloc(sizeof(ENode));
            expr->type = EFLOATCONST;
            expr->cost = 0;
            expr->flags = 0;
            expr->rtype = TYPE(atomtype());
            expr->data.floatval = tkfloatconst;
            tk = lex();
            return expr;
        case TK_STRING:
            expr = CExpr_NewENode(ESTRINGCONST);
            expr->rtype =
                CDecl_NewArrayType(ispascalstring ? TYPE(&stunsignedchar) : TYPE(&stchar), tksize);
            expr->data.string.size = tksize;
            expr->data.string.data = tkstring;
            expr->data.string.ispascal = ispascalstring;
            if (copts.const_strings) {
                expr->flags = ENODE_FLAG_CONST;
            }
            expr = makemonadicnode(expr, EINDIRECT);
            expr->data.monadic->rtype = CDecl_NewPointerType(expr->rtype);
            tk = lex();
            return expr;
        case TK_STRING_WIDE:
            expr = CExpr_NewENode(ESTRINGCONST);
            expr->rtype = CDecl_NewArrayType(CParser_GetWCharType(), tksize);
            expr->data.string.size = tksize;
            expr->data.string.data = tkstring;
            expr->data.string.ispascal = ispascalstring;
            if (copts.const_strings) {
                expr->flags = ENODE_FLAG_CONST;
            }
            expr = makemonadicnode(expr, EINDIRECT);
            expr->data.monadic->rtype = CDecl_NewPointerType(expr->rtype);
            tk = lex();
            return expr;
        case TK_THIS:
        case TK_SELF:
            expr = CClass_CreateThisSelfExpr();
            if (!expr) {
                expr = nullnode();
            }
            tk = lex();
            return expr;
        case TK_AT_SELECTOR:
            return CObjC_ParseSelectorExpression();
        case TK_AT_ENCODE:
            return CObjC_ParseEncodeExpression();
        case TK_AT_PROTOCOL:
            return CObjC_ParseProtocolExpression();
        case '@':
            if (copts.objective_c) {
                return CObjC_ParseAtExpression();
            }
            break;
        case '(':
            tk = lex();
            expr = s_expression();
            if (tk != ')') {
                CError_ErrorSkip(ERR_RPAREN_EXPECTED);
            } else {
                tk = lex();
            }
            if (ENODE_IS(expr, EASS)) {
                expr->flags |= ENODE_FLAG_PARENS;
            }
            return expr;
        case '[':
            if (copts.objective_c) {
                return CObjC_ParseMessageExpression();
            }
            break;
        case TK_IDENTIFIER:
            if (tkidentifier->name[0] == '_' && tkidentifier->name[1] == '_') {
                if (!strcmp(tkidentifier->name, "__builtin_align")) {
                    return CExpr_ParseBuiltin(CExpr_BuiltInComputeAlign);
                }
                if (!strcmp(tkidentifier->name, "__builtin_ntype")) {
                    return CExpr_ParseBuiltin(CExpr_BuiltInComputeType);
                }
                if (!strcmp(tkidentifier->name, "__builtin_type") ||
                    !strcmp(tkidentifier->name, "__builtin_vargtype")) {
                    return CExpr_ParseBuiltin(CExpr_BuiltInComputeVArgType);
                }
                if (!strcmp(tkidentifier->name, "__builtin_classify_type")) {
                    return CExpr_ParseBuiltin(CExpr_BuiltInClassifyType);
                }
                if (!strcmp(tkidentifier->name, "__builtin_next_arg")) {
                    return CExpr_ParseNextArg();
                }
            }
            if (copts.altivec_model && !strcmp("vec_step", tkidentifier->name)) {
                return CExpr_ParseVecStep();
            }
            if (!strcmp("_INFO", tkidentifier->name) ||
                !strcmp("_var_arg_typeof", tkidentifier->name)) {
                return CExpr_ParseBuiltin(CExpr_BuiltInComputeVArgType);
            }
        case '~':
        case TK_OPERATOR:
        case TK_INHERITED:
        case TK_COLON_COLON:
            if (CScope_ParseExprName(&pr)) {
                return CExpr_ParseNameResultExpr(&pr, NULL, isAddressOf, TRUE);
            }
            tk = lex();
            return nullnode();
    }

    CError_ErrorSkip(ERR_EXPRESSION_SYNTAX_ERROR);
    return nullnode();
}

static ENode* CExpr_SimpleExplicitConversion(void) {
    DeclInfo di;

    memclrw(&di, sizeof(DeclInfo));

    if (!copts.cpp_extensions &&
        (int)tk != TK_UU_TYPEOF_UU &&
        (int)tk != TK_TYPENAME &&
        lookahead() != '(') {
        CError_Error(ERR_LPAREN_EXPECTED);
    }

    CParser_GetDeclSpecs(&di, FALSE);
    return CExpr_ParseExplicitConversion(di.thetype, di.qual);
}

static ENode* CExpr_NewPTMFCall(void) {
    tk = lex();
    CExpr_ScanExpressionList(TRUE);

    if (tk != ')') {
        CError_Error(ERR_RPAREN_EXPECTED);
        return nullnode();
    } else {
        CError_FATAL(2465);
        return nullnode();
    }
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -2
#endif
static ENode* call_ptmf(ENode* expr) {
    ENodeList* list;
    Type* rettype;
    ENodeList* args;
    Object* callobj;

    rettype = TYPE_FUNC(TPTR_TARGET(expr->data.mfpointer.mfpointer->rtype))->functype;
    tk = lex();
    args = CExpr_ScanExpressionList(TRUE);

    if (tk != ')') {
        CError_Error(ERR_RPAREN_EXPECTED);
        return nullnode();
    }

    if (Type_RequiresMemoryReturn(rettype)) {
        callobj = rt_ptmf_scall4;
    } else {
        callobj = rt_ptmf_scall;
    }

    list = lalloc(sizeof(ENodeList));
    list->next = args;
    args = list;
    list->node = expr->data.mfpointer.accessnode->data.monadic;

    list = lalloc(sizeof(ENodeList));
    list->next = args;
    list->node = expr->data.mfpointer.mfpointer->data.monadic;

    if (!copts.old_argmatch) {
        CError_ASSERT(2570, IS_TYPE_POINTER_ONLY(list->node->rtype));
        list->node->rtype = TYPE(&void_ptr);
    }

    expr = CExpr_GenericPtmfCall(
        callobj, TYPE_FUNC(TPTR_TARGET(expr->data.mfpointer.mfpointer->rtype)), list
    );
    tk = lex();
    return expr;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static ENode* CExpr_DummyDestr(ENode* expr) {
    DeclInfo di;
    SInt32 state;
    NameResult pr;
    NameSpace* nspace;

    CPrep_TokenStreamGetState(&state);
    nspace = cscope_current;
    if ((tk = lex()) == TK_COLON_COLON) {
        nspace = cscope_root;
        tk = lex();
    } else if (tk != '~' && tk != TK_IDENTIFIER && !((int)tk >= TK_AUTO && (int)tk <= TK_BYREF)) {
        CPrep_TokenStreamSetCurState(&state);
        return NULL;
    }

loop:
    if (tk == '~') {
        goto is_tilde;
    }
    if (tk == TK_IDENTIFIER) {
        if (CScope_FindTypeName(nspace, tkidentifier, &pr)) {
            tk = lex();
            if (pr.nspace) {
                if (tk == TK_COLON_COLON) {
                    tk = lex();
                    nspace = pr.nspace;
                    goto loop;
                }
            } else if (IS_TYPE_CLASS(pr.type) && tk == TK_COLON_COLON) {
                tk = lex();
                nspace = TYPE_CLASS(pr.type)->nspace;
                goto loop;
            } else {
                if (!is_typesame(pr.type, expr->rtype)) {
                    CError_Error(ERR_ILLEGAL_TYPE);
                }
                if (tk == TK_COLON_COLON &&
                    ((tk = lex()) == '~') &&
                    ((tk = lex()) == TK_IDENTIFIER)) {
parse_dtor:
                    if (CScope_FindTypeName(nspace, tkidentifier, &pr) && !pr.nspace) {
                        if (!is_typesame(pr.type, expr->rtype)) {
                            CError_Error(ERR_ILLEGAL_TYPE);
                        }
                        tk = lex();
                        goto parsed;
                    }
                }
            }
        }
    } else if ((int)tk >= TK_AUTO && (int)tk <= TK_BYREF) {
        memclrw(&di, sizeof(DeclInfo));
        CParser_GetDeclSpecs(&di, FALSE);
        if (di.storageclass || di.qual || !is_typesame(di.thetype, expr->rtype)) {
            CError_Error(ERR_ILLEGAL_TYPE);
        }
        if (tk == TK_COLON_COLON && ((tk = lex()) == '~')) {
is_tilde:
            if ((tk = lex()) == TK_IDENTIFIER) {
                goto parse_dtor;
            }
            memclrw(&di, sizeof(DeclInfo));
            CParser_GetDeclSpecs(&di, FALSE);
            if (di.storageclass || !is_typesame(di.thetype, expr->rtype)) {
                CError_Error(ERR_ILLEGAL_TYPE);
            }
            goto parsed;
        }
    }

    CError_Error(ERR_EXPRESSION_SYNTAX_ERROR);
    return NULL;

parsed:
    if (tk == '(') {
        if ((tk = lex()) != ')') {
            CError_Error(ERR_RPAREN_EXPECTED);
        } else {
            tk = lex();
        }
    } else {
        CError_Error(ERR_LPAREN_EXPECTED);
    }

    expr = makemonadicnode(expr, ETYPCON);
    expr->rtype = &stvoid;
    return expr;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -2
#endif
static ENode* postfix_expression(Boolean isAddressOf) {
    Conversion conv;
    NameResult pr;
    ENode* expr;
    ENode* subexpr;
    ENode* temp;
    ENodeList* args;
    ENode* copy;
    ENode* tmp;
    StructMember* member;

    if (copts.cplusplus) {
        switch (tk) {
            case TK_VOID:
            case TK_CHAR:
            case TK_SHORT:
            case TK_INT:
            case TK_LONG:
            case TK_FLOAT:
            case TK_DOUBLE:
            case TK_SIGNED:
            case TK_UNSIGNED:
            case TK_CUSTOM_TYPE_113:
            case TK_CUSTOM_TYPE_114:
            case TK_UU_VEC2X32FLOAT_UU:
            case TK_CUSTOM_TYPE_116:
            case TK_CUSTOM_TYPE_117:
            case TK_CUSTOM_TYPE_118:
            case TK_CUSTOM_TYPE_119:
            case TK_CUSTOM_TYPE_11A:
            case TK_UU_TYPEOF_UU:
            case TK_BOOL:
            case TK_WCHAR_T:
            case TK_TYPENAME:
                expr = CExpr_SimpleExplicitConversion();
                break;
            default:
                expr = primary_expression(isAddressOf);
                break;
            case TK_CONST_CAST:
                expr = CRTTI_Parse_const_cast();
                break;
            case TK_DYNAMIC_CAST:
                expr = CRTTI_Parse_dynamic_cast();
                break;
            case TK_REINTERPRET_CAST:
                expr = CRTTI_Parse_reinterpret_cast();
                break;
            case TK_STATIC_CAST:
                expr = CRTTI_Parse_static_cast();
                break;
            case TK_TYPEID:
                expr = CRTTI_ParseTypeID();
                break;
        }
    } else {
        expr = primary_expression(isAddressOf);
    }

loop:
    switch (tk) {
        case '[':
            expr = pointer_generation(expr);
            tk = lex();
            subexpr = expression();
            if (copts.cplusplus && CExpr_CheckOperator('[', expr, subexpr, &conv)) {
                if ((expr = conv.expression)) {
                    if (tk != ']') {
                        CError_ErrorSkip(ERR_RBRACKET_EXPECTED);
                    } else {
                        tk = lex();
                    }
                    goto loop;
                }

                CError_ASSERT(2755, expr = conv.left);
                CError_ASSERT(2756, subexpr = conv.right);
            }

            if (IS_TYPE_POINTER(expr->rtype)) {
                expr = padd(expr, subexpr);
            } else if (IS_TYPE_POINTER(subexpr->rtype)) {
                expr = padd(subexpr, expr);
            } else {
                CError_Error(ERR_POINTER_ARRAY_REQUIRED);
                goto dont_do_indirect;
            }
            expr = makemonadicnode(expr, EINDIRECT);
            expr->rtype = TPTR_TARGET(expr->rtype);
dont_do_indirect:
            if (tk != ']') {
                CError_ErrorSkip(ERR_RBRACKET_EXPECTED);
            } else {
                tk = lex();
            }
            goto loop;

        case '(':
            expr = CExpr_PointerGeneration(expr);
            if (copts.cplusplus) {
                if (CExpr_CheckOperator('(', expr, NULL, &conv)) {
                    CError_ASSERT(2777, expr = conv.expression);
                    goto loop;
                }
                if (ENODE_IS(expr, EMFPOINTER)) {
                    expr = checkreference(call_ptmf(expr));
                    goto loop;
                }
            }

            tk = lex();
            args = CExpr_ScanExpressionList(TRUE);
            if (tk != ')') {
                CError_Error(ERR_RPAREN_EXPECTED);
            }

            if (ENODE_IS(expr, ETEMPLDEP)) {
                tmp = CExpr_NewENode(EFUNCCALL);
                tmp->rtype = &sttemplexpr;
                tmp->data.funccall.funcref = expr;
                tmp->data.funccall.args = args;
                tmp->data.funccall.functype = &rt_func;
                tk = lex();
                expr = tmp;
            } else {
                expr = checkreference(CExpr_MakeFunctionCall(expr, args));
                tk = lex();
            }
            goto loop;

        case TK_ARROW:
            expr = pointer_generation(expr);
            if (copts.cplusplus) {
                while (IS_TYPE_CLASS(expr->rtype) &&
                       CExpr_CheckOperator(TK_ARROW, expr, NULL, &conv)) {
                    CError_ASSERT(2812, subexpr = conv.expression);
                    expr = pointer_generation(subexpr);
                }
            }

            if (!IS_TYPE_POINTER(expr->rtype)) {
                CError_ErrorSkip(ERR_POINTER_ARRAY_REQUIRED);
                return expr;
            }

            if (copts.cplusplus && copts.objective_c && CObjC_IsType_id(expr->rtype)) {
                expr = makemonadicnode(expr, EINDIRECT);
                expr->rtype = TPTR_TARGET(expr->rtype);
                if ((subexpr = CObjC_CheckModernSendMessage(NULL, expr))) {
                    return subexpr;
                }
            } else {
                expr = makemonadicnode(expr, EINDIRECT);
                expr->rtype = TPTR_TARGET(expr->rtype);
            }
        case '.':
            expr = pointer_generation(expr);
            if (ENODE_IS(expr, EFUNCCALL) &&
                (IS_TYPE_CLASS(expr->rtype) || IS_TYPE_STRUCT(expr->rtype)) &&
                !Type_RequiresMemoryReturn(expr->rtype)) {
                temp = CExpr_NewETEMPNode(expr->rtype, TRUE);
                copy = lalloc(sizeof(ENode));
                *copy = *temp;

                tmp = makemonadicnode(temp, EINDIRECT);
                tmp->rtype = expr->rtype;

                tmp = makediadicnode(tmp, expr, EASS);
                tmp = makediadicnode(tmp, copy, ECOMMA);
                tmp->rtype = copy->rtype;

                tmp = makemonadicnode(tmp, EINDIRECT);
                tmp->rtype = expr->rtype;
                expr = tmp;
            }
            if (IS_TYPE_CLASS(expr->rtype)) {
                CDecl_CompleteType(expr->rtype);
                if (TYPE_CLASS(expr->rtype)->objcinfo &&
                    copts.cplusplus &&
                    (subexpr = CObjC_CheckModernSendMessage(TYPE_CLASS(expr->rtype), expr))) {
                    return subexpr;
                }

                if (!(TYPE_CLASS(expr->rtype)->flags & CLASS_COMPLETED)) {
                    CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, expr->rtype, 0);
                }

                if ((tk = lex()) == TK_TEMPLATE && (tk = lex()) != TK_IDENTIFIER) {
                    CError_Error(ERR_IDENTIFIER_EXPECTED);
                }

                if (CScope_ParseMemberName(TYPE_CLASS(expr->rtype), &pr, FALSE)) {
                    if (pr.is_destructor) {
                        if ((tk = lex()) == '(') {
                            if ((tk = lex()) != ')') {
                                CError_Error(ERR_RPAREN_EXPECTED);
                            } else {
                                tk = lex();
                            }
                        } else {
                            CError_Error(ERR_LPAREN_EXPECTED);
                        }

                        expr = makemonadicnode(expr, ETYPCON);
                        expr->rtype = &stvoid;
                    } else {
                        expr = checkreference(CExpr_ParseNameResultExpr(&pr, expr, FALSE, FALSE));
                    }
                }
                goto loop;
            }
            if (!IS_TYPE_STRUCT(expr->rtype) || TYPE_STRUCT(expr->rtype)->stype > STRUCT_TYPE_MAX) {
                if (copts.cplusplus && (subexpr = CExpr_DummyDestr(expr))) {
                    return subexpr;
                }
                CError_ErrorSkip(ERR_NOT_STRUCT_UNION_CLASS);
                return expr;
            }
            if (!ENODE_IS(expr, EINDIRECT)) {
                subexpr = CExpr_NewETEMPNode(expr->rtype, TRUE);
                copy = lalloc(sizeof(ENode));
                *copy = *subexpr;

                tmp = makemonadicnode(subexpr, EINDIRECT);
                tmp->rtype = expr->rtype;

                tmp = makediadicnode(tmp, expr, EASS);
                tmp = makediadicnode(tmp, copy, ECOMMA);
                tmp->rtype = copy->rtype;

                tmp = makemonadicnode(tmp, EINDIRECT);
                tmp->rtype = expr->rtype;
                expr = tmp;
            }
            if ((tk = lex()) != TK_IDENTIFIER) {
                CError_Error(ERR_IDENTIFIER_EXPECTED);
                return expr;
            }

            member = ismember(TYPE_STRUCT(expr->rtype), tkidentifier);
            if (!member) {
                if (!expr->rtype->size) {
                    CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, expr->rtype, 0);
                } else {
                    CError_Error(ERR_NOT_STRUCT_UNION_CLASS_MEMBER, tkidentifier->name);
                }
                return expr;
            }

            if (!IS_TYPE_POINTER(expr->data.monadic->rtype)) {
                CError_ErrorSkip(ERR_NOT_STRUCT_UNION_CLASS);
                return expr;
            }

            expr = checkreference(
                CClass_AccessMember(expr, member->type, member->qual, member->offset)
            );
            tk = lex();
            goto loop;

        case TK_INCREMENT:
            tmp = pointer_generation(expr);
            if (copts.cplusplus && CExpr_CheckOperator(TK_INCREMENT, tmp, nullnode(), &conv)) {
                if ((expr = conv.expression)) {
                    tk = lex();
                    goto loop;
                }
                CError_ASSERT(2954, tmp = conv.left);
            }
            tmp = CExpr_LValue(tmp, TRUE, TRUE);
            if (tmp->rtype == TYPE(&stbool)) {
                expr = CExpr_TempModifyExpr(tmp);
                tk = lex();
            } else {
                checkadditive(tmp);
                expr = CodeGen_CheckExpr(makemonadicnode(tmp, EPOSTINC));
                tk = lex();
            }
            goto loop;

        case TK_DECREMENT:
            tmp = pointer_generation(expr);
            if (copts.cplusplus && CExpr_CheckOperator(TK_DECREMENT, tmp, nullnode(), &conv)) {
                if ((expr = conv.expression)) {
                    tk = lex();
                    goto loop;
                }
                CError_ASSERT(2978, tmp = conv.left);
            }
            tmp = CExpr_LValue(tmp, TRUE, TRUE);
            checkadditive(tmp);
            expr = CodeGen_CheckExpr(makemonadicnode(tmp, EPOSTDEC));
            tk = lex();
            goto loop;
    }

    return expr;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static ENode* CExpr_ParseSizeof(void) {
    Type* type;
    ENode* expr;

    type = CExpr_ParseTypeExpression(NULL);
    if (IS_TYPE_CLASS(type) && TYPE_CLASS(type)->sominfo) {
        CError_Error(ERR_SIZEOF_NOT_SUPPORTED_SOM_CLASSES);
    }

    if (CTemplTool_IsTemplateArgumentDependentType(type)) {
        expr = CExpr_NewTemplDepENode(TDE_SIZEOF);
        expr->data.templdep.u.typeexpr.type = type;
        return expr;
    }

    if (type->size == 0) {
        if (copts.gcc_extensions && (IS_TYPE_FUNC(type) || IS_TYPE_VOID(type))) {
            return intconstnode(CABI_GetSizeTType(), 1);
        }

        if (IS_TYPE_CLASS(type) || IS_TYPE_STRUCT(type)) {
            CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, type, 0);
        } else {
            CError_Error(ERR_ILLEGAL_TYPE);
        }
    }

    return intconstnode(CABI_GetSizeTType(), type->size);
}

SInt32 scansizeof(void) {
    ENode* expr;

    expr = CExpr_ParseSizeof();
    if (!ENODE_IS(expr, EINTCONST)) {
        CError_Error(ERR_UNIMPLEMENTED_C_FEATURE);
        return 0;
    }

    return CInt64_GetULong(&expr->data.intval);
}

static ENode* CExpr_ParseAlignof(void) {
    Type* type;
    ENode* expr;
    SInt16 align;

    type = CExpr_ParseTypeExpression(NULL);
    if (IS_TYPE_CLASS(type) && TYPE_CLASS(type)->sominfo) {
        CError_Error(ERR_ALIGNOF_NOT_SUPPORTED_SOM_CLASSES);
    }

    if (CTemplTool_IsTemplateArgumentDependentType(type)) {
        expr = CExpr_NewTemplDepENode(TDE_ALIGNOF);
        expr->data.templdep.u.typeexpr.type = type;
        return expr;
    }

    if (type->size == 0) {
        if (IS_TYPE_CLASS(type) || IS_TYPE_STRUCT(type)) {
            CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, type, 0);
        } else {
            CError_Error(ERR_ILLEGAL_TYPE);
        }
    }

    align = CMach_GetTypeAlign(type);
    if (align == 0) {
        align = 1;
    }

    return intconstnode(CABI_GetSizeTType(), align);
}

SInt32 scanalignof(void) {
    ENode* expr;

    expr = CExpr_ParseAlignof();
    if (!ENODE_IS(expr, EINTCONST)) {
        CError_Error(ERR_UNIMPLEMENTED_C_FEATURE);
        return 0;
    }

    return CInt64_GetULong(&expr->data.intval);
}

static ENode* logicalexpression(ENode* expr) {
    if (copts.cplusplus && copts.booltruefalse) {
        expr->rtype = TYPE(&stbool);
    } else {
        expr->rtype = TYPE(&stsignedint);
    }
    return expr;
}

ENode* getnodeaddress(ENode* expr, Boolean isAddressOf) {
    ENode* result;
    Object* obj;

    if (!ENODE_IS(expr, EINDIRECT)) {
        expr = CExpr_LValue(expr, isAddressOf, isAddressOf);
        if (!ENODE_IS(expr, EINDIRECT)) {
            if (!isAddressOf) {
                CError_Error(ERR_NOT_LVALUE);
            }
            return nullnode();
        }
    } else {
        if (isAddressOf &&
            ENODE_IS(expr->data.monadic, EOBJREF) &&
            expr->data.monadic->data.objref->name == this_name_node &&
            cscope_currentfunc &&
            cscope_currentclass &&
            expr->data.monadic->data.objref == CClass_ThisSelfObject()) {
            CError_Error(ERR_ILLEGAL_USE_THIS);
        }
    }

    result = lalloc(sizeof(ENode));
    *result = *expr;
restart:
    switch (result->data.monadic->type) {
        case EPOSTINC:
        case EPOSTDEC:
        case EPREINC:
        case EPREDEC:
            result->type = ETYPCON;
            if (IS_TYPE_POINTER_ONLY(result->rtype)) {
                result->flags = TPTR_QUAL(result->rtype) & ENODE_FLAG_QUALS;
            }
            result->rtype = CDecl_NewPointerType(result->rtype);
            return result;
        case EOBJREF:
            obj = result->data.monadic->data.objref;
            if (obj->datatype == DALIAS) {
                CExpr_AliasTransform(result->data.monadic);
                goto restart;
            }
            if (obj->datatype == DINLINEFUNC) {
                CError_Error(ERR_ILLEGAL_USE_INLINE_FUNCTION);
            }
            obj->flags |= OBJECT_REFERENCED;
            if (isAddressOf && !copts.cplusplus && obj->sclass == TK_REGISTER) {
                CError_Error(ERR_ILLEGAL_USE_REGISTER_VARIABLE);
            }
            break;
        case EFUNCCALL:
            if (isAddressOf &&
                !IS_TYPE_POINTER_ONLY(result->data.monadic->data.funccall.functype->functype)) {
                CError_Warning(ERR_NOT_LVALUE);
            }
            break;
        case EBITFIELD:
            CError_Error(ERR_ILLEGAL_OPERAND);
            return nullnode();
        default:
            break;
    }

    switch (result->rtype->type) {
        case TYPEPOINTER:
            result->data.monadic->rtype = CDecl_NewPointerType(result->rtype);
            result->data.monadic->flags = result->flags;
            break;
        default:
            result->data.monadic->rtype = CDecl_NewPointerType(result->rtype);
            result->data.monadic->flags = result->flags;
    }

    return result->data.monadic;
}

static ENode* CExpr_MakeStaticMemberList(NameSpaceObjectList* list) {
    NameSpaceObjectList* newlist;
    NameSpaceObjectList* n;
    ENode* result;

    newlist = NULL;
    while (list) {
        if (list->object->otype == OT_OBJECT && IS_TYPE_STATIC_METHOD(OBJECT(list->object)->type)) {
            n = galloc(sizeof(NameSpaceObjectList));
            *n = *list;
            n->next = newlist;
            newlist = n;
        }
        list = list->next;
    }

    if (!newlist) {
        CError_Warning(ERR_ILLEGAL_IMPLICIT_MEMBER_POINTER_CONVERSION);
        return nullnode();
    }

    result = CExpr_NewENode(EOBJLIST);
    result->rtype = OBJECT(newlist->object)->type;
    result->data.objlist.list = newlist;
    return result;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -2
#endif
static ENode* CExpr_MakePTDM(ENode* expr) {
    ENode* result;

    CError_ASSERT(
        3416, ENODE_IS(expr, EMEMBER) && expr->data.emember->list->object->otype == OT_MEMBERVAR
    );

    result = nullnode();
    result->rtype = CExpr_NewPTMType(expr->data.emember, NULL);
    CInt64_SetLong(
        &result->data.intval, OBJ_MEMBER_VAR(expr->data.emember->list->object)->offset + 1
    );
    return result;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -2
#endif
ENode* getpointertomemberfunc(ENode* expr, Type* type, Boolean buildData) {
    NameSpaceObjectList* list;
    Object* obj;
    Type* ptmtype;
    TypeMemberFunc* tmethod;
    OLinkList* olist;
    Object* dataobj;
    SInt32 data[3];

    CError_ASSERT(3444, ENODE_IS(expr, EMEMBER));

    if (expr->data.emember->expr && !copts.cpp_extensions) {
        CError_Error(ERR_EXPRESSION_SYNTAX_ERROR);
    }

    if (!copts.cpp_extensions) {
        if (!(expr->data.emember->addressTaken && expr->data.emember->is_qualified)) {
            if (type && IS_TYPE_MEMBERPOINTER(type)) {
                CError_Warning(ERR_ILLEGAL_IMPLICIT_MEMBER_POINTER_CONVERSION);
            }
        }
    }

    if (expr->data.emember->list->next) {
        if (type) {
            if (IS_TYPE_MEMBERPOINTER(type)) {
                type = TYPE_MEMBER_POINTER(type)->ty1;
                for (list = expr->data.emember->list; list; list = list->next) {
                    if (list->object->otype == OT_OBJECT &&
                        is_memberpointerequal(OBJECT(list->object)->type, type)) {
                        obj = OBJECT(list->object);
                        break;
                    }
                }
                if (!list) {
                    CError_Error(ERR_ILLEGAL_TYPE);
                    return nullnode();
                }
            } else {
                return CExpr_MakeStaticMemberList(expr->data.emember->list);
            }
        } else {
            obj = OBJECT(expr->data.emember->list->object);
        }
    } else {
        obj = OBJECT(expr->data.emember->list->object);
    }

    while (obj->datatype == DALIAS) {
        obj = obj->u.alias.object;
    }

    CError_ASSERT(3505, obj->otype == OT_OBJECT && IS_TYPE_NONSTATIC_METHOD(obj->type));

    if (TYPE_FUNC(obj->type)->flags & FUNC_IS_TEMPL) {
        CError_Error(ERR_UNIMPLEMENTED_C_FEATURE);
    }

    ptmtype = CExpr_NewPTMType(expr->data.emember, obj);
    tmethod = TYPE_METHOD(obj->type);
    dataobj = CParser_NewGlobalDataObject(NULL);
    dataobj->name = CParser_GetUniqueName();
    dataobj->nspace = cscope_root;
    dataobj->type = ptmtype;
    dataobj->sclass = TK_STATIC;

    if (buildData) {
        data[0] = CTool_EndianConvertWord32(0);
        if (obj->datatype == DVFUNC) {
            olist = NULL;
            data[1] = CTool_EndianConvertWord32(tmethod->vtbl_index);
            data[2] = CTool_EndianConvertWord32(tmethod->theclass->vtable->offset);
        } else {
            data[1] = CTool_EndianConvertWord32(-1);
            data[2] = CTool_EndianConvertWord32(0);
            olist = galloc(sizeof(OLinkList));
            olist->next = NULL;
            olist->obj = obj;
            olist->addend = 0;
            olist->offset = 8;
        }
        CInit_DeclareData(dataobj, data, olist, dataobj->type->size);
    }

    return create_objectnode(dataobj);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -2
#endif
static ENode* getpointertomember(ENode* expr) {
    CError_ASSERT(3556, ENODE_IS(expr, EMEMBER));

    if (expr->data.emember->expr) {
        CError_Error(ERR_EXPRESSION_SYNTAX_ERROR);
    }

    expr->data.emember->addressTaken = TRUE;
    if (!expr->data.emember->list->next) {
        if (expr->data.emember->list->object->otype == OT_MEMBERVAR) {
            return CExpr_MakePTDM(expr);
        } else {
            return getpointertomemberfunc(expr, NULL, TRUE);
        }
    }

    return expr;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -2
#endif
ENode* CExpr_New_ELOGNOT_Node(ENode* input) {
    ENode* expr;
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(input->rtype)) {
        return CTempl_MakeTemplDepExpr(NULL, ELOGNOT, input);
    }

    expr = pointer_generation(input);
    if (copts.cplusplus && CExpr_CheckOperator('!', expr, NULL, &conv)) {
        if ((input = conv.expression)) {
            return input;
        }
        CError_ASSERT(3595, expr = conv.left);
    }

    switch (expr->rtype->type) {
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEPOINTER:
        case TYPEARRAY:
            break;
        case TYPEENUM:
            expr = forceintegral(expr);
            break;
        case TYPEMEMBERPOINTER:
            expr = CExpr_ConvertToCondition(expr);
            break;
        default:
            CError_Error(ERR_ILLEGAL_OPERAND);
            return expr;
    }

    switch (expr->type) {
        case EINTCONST:
            expr->data.intval = CInt64_Not(expr->data.intval);
            break;
        case EFLOATCONST:
            expr->type = EINTCONST;
            CInt64_SetLong(&expr->data.intval, CMach_FloatIsZero(expr->data.floatval));
            break;
        default:
            expr = makemonadicnode(expr, ELOGNOT);
    }

    return logicalexpression(CodeGen_CheckExpr(expr));
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -2
#endif
ENode* CExpr_New_EMONMIN_Node(ENode* input) {
    ENode* expr;
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(input->rtype)) {
        return CTempl_MakeTemplDepExpr(NULL, EMONMIN, input);
    }

    expr = pointer_generation(input);
    if (copts.cplusplus && CExpr_CheckOperator('-', expr, NULL, &conv)) {
        if ((input = conv.expression)) {
            return input;
        }
        CError_ASSERT(3654, expr = conv.left);
    }

    switch (expr->rtype->type) {
        case TYPEINT:
        case TYPEENUM:
            expr = integralpromote(expr);
            if (ENODE_IS(expr, EINTCONST)) {
                expr->data.intval = CMach_CalcIntMonadic(expr->rtype, '-', expr->data.intval);
                return expr;
            }
            return CodeGen_CheckExpr(makemonadicnode(expr, EMONMIN));
        case TYPEFLOAT:
            if (ENODE_IS(expr, EFLOATCONST)) {
                expr->data.floatval = CMach_CalcFloatMonadic(expr->rtype, '-', expr->data.floatval);
                return expr;
            }
            return CodeGen_CheckExpr(CExpr_UnaryFloatExpression(makemonadicnode(expr, EMONMIN)));
        default:
            CError_Error(ERR_ILLEGAL_OPERAND);
            return expr;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -2
#endif
ENode* CExpr_New_EBINNOT_Node(ENode* input) {
    ENode* expr;
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(input->rtype)) {
        return CTempl_MakeTemplDepExpr(NULL, EBINNOT, input);
    }

    expr = pointer_generation(input);
    if (copts.cplusplus && CExpr_CheckOperator('~', expr, NULL, &conv)) {
        if ((input = conv.expression)) {
            return input;
        }
        CError_ASSERT(3704, expr = conv.left);
    }

    expr = integralpromote(expr);

    if (ENODE_IS(expr, EINTCONST)) {
        expr->data.intval = CMach_CalcIntMonadic(expr->rtype, '~', expr->data.intval);
        return expr;
    }
    expr = makemonadicnode(expr, EBINNOT);
    expr = CodeGen_CheckExpr(expr);
    return expr;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -2
#endif
ENode* unary_expression(void) {
    ENode* expr;
    ENode* node;
    Conversion conv;

    switch (tk) {
        case TK_COLON_COLON:
            switch (lookahead()) {
                case TK_NEW:
                    tk = lex();
                    return scannew(TRUE);
                case TK_DELETE:
                    tk = lex();
                    return scandelete(TRUE);
            }
            return postfix_expression(FALSE);

        case TK_NEW:
            return scannew(FALSE);
        case TK_DELETE:
            return scandelete(FALSE);
        case TK_INCREMENT:
            tk = lex();
            if (copts.cplusplus) {
                expr = pointer_generation(cast_expression());
                if (CExpr_CheckOperator(TK_INCREMENT, expr, NULL, &conv)) {
                    if ((node = conv.expression)) {
                        return node;
                    }
                    CError_ASSERT(3750, expr = conv.left);
                }
            } else {
                expr = pointer_generation(unary_expression());
            }
            expr = CExpr_LValue(expr, TRUE, TRUE);
            if (expr->rtype == TYPE(&stbool)) {
                node = nullnode();
                node->rtype = TYPE(&stbool);
                CInt64_SetLong(&node->data.intval, 1);
                return makediadicnode(expr, node, EASS);
            } else {
                checkadditive(expr);
                return CodeGen_CheckExpr(makemonadicnode(expr, EPREINC));
            }
        case TK_DECREMENT:
            tk = lex();
            if (copts.cplusplus) {
                expr = pointer_generation(cast_expression());
                if (CExpr_CheckOperator(TK_DECREMENT, expr, NULL, &conv)) {
                    if ((node = conv.expression)) {
                        return node;
                    }
                    CError_ASSERT(3778, expr = conv.left);
                }
            } else {
                expr = pointer_generation(unary_expression());
            }
            expr = CExpr_LValue(expr, TRUE, TRUE);
            checkadditive(expr);
            return CodeGen_CheckExpr(makemonadicnode(expr, EPREDEC));

        case '&':
            if (copts.cplusplus) {
                switch ((tk = lex())) {
                    case TK_IDENTIFIER:
                    case TK_INHERITED:
                    case TK_COLON_COLON:
                        expr = postfix_expression(TRUE);
                        if (ENODE_IS(expr, EMEMBER)) {
                            return getpointertomember(expr);
                        }
                        break;
                    default:
                        expr = cast_expression();
                }

                if (CExpr_CheckOperator('&', expr, NULL, &conv)) {
                    CError_ASSERT(3811, conv.expression);
                    return conv.expression;
                }
            } else {
                tk = lex();
                expr = cast_expression();
            }

            if (copts.mpwc_relax &&
                !copts.cplusplus &&
                IS_TYPE_ARRAY(expr->rtype) &&
                ENODE_IS(expr, EINDIRECT)) {
                return pointer_generation(expr);
            }
            if (ENODE_IS(expr, EOBJLIST)) {
                return expr;
            }

            if (IS_TYPE_TEMPLDEPEXPR(expr->rtype)) {
                node = CExpr_NewTemplDepENode(TDE_ADDRESS_OF);
                node->data.templdep.u.monadic = expr;
                return node;
            }

            return getnodeaddress(expr, TRUE);

        case '*':
            tk = lex();
            expr = pointer_generation(cast_expression());
            if (copts.cplusplus && CExpr_CheckOperator('*', expr, NULL, &conv)) {
                if ((node = conv.expression)) {
                    return node;
                }
                CError_ASSERT(3842, expr = conv.left);
            }

            if (!IS_TYPE_POINTER(expr->rtype)) {
                CError_Error(ERR_POINTER_ARRAY_REQUIRED);
                return expr;
            }

            expr = makemonadicnode(expr, EINDIRECT);
            CDecl_CompleteType(expr->rtype = TPTR_TARGET(expr->rtype));
            return expr;

        case '+':
            tk = lex();
            expr = pointer_generation(cast_expression());
            if (copts.cplusplus && CExpr_CheckOperator('+', expr, NULL, &conv)) {
                if ((node = conv.expression)) {
                    return node;
                }
                CError_ASSERT(3854, expr = conv.left);
            }

            switch (expr->rtype->type) {
                case TYPEINT:
                case TYPEENUM:
                    return integralpromote(expr);
                case TYPEFLOAT:
                case TYPEPOINTER:
                case TYPEARRAY:
                    return expr;
                default:
                    CError_Error(ERR_ILLEGAL_OPERAND);
                    return expr;
            }

        case '-':
            tk = lex();
            return CExpr_New_EMONMIN_Node(cast_expression());
        case '~':
            tk = lex();
            return CExpr_New_EBINNOT_Node(cast_expression());
        case '!':
            tk = lex();
            return CExpr_New_ELOGNOT_Node(cast_expression());

        case TK_SIZEOF:
            return CExpr_ParseSizeof();
        case TK_UU_ALIGNOF_UU:
            return CExpr_ParseAlignof();

        case TK_LOGICAL_AND:
            if (copts.ANSIstrict) {
                break;
            }

            if ((tk = lex()) != TK_IDENTIFIER) {
                CError_Error(ERR_IDENTIFIER_EXPECTED);
                return nullnode();
            }

            expr = lalloc(sizeof(ENode));
            expr->type = ELABEL;
            expr->cost = 0;
            expr->flags = 0;
            expr->rtype = TYPE(&void_ptr);
            expr->data.label = findlabel();
            if (!expr->data.label) {
                expr->data.label = newlabel();
                expr->data.label->name = tkidentifier;
                expr->data.label->next = Labels;
                Labels = expr->data.label;
            }
            tk = lex();
            return expr;
    }

    return postfix_expression(FALSE);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

ENode* do_castnullcheck(ENode* condexpr, ENode* nullcheckexpr) {
    ENode* result;

    if (isnotzero(nullcheckexpr)) {
        return condexpr;
    }

    result = lalloc(sizeof(ENode));
    *result = *condexpr;
    result->type = ENULLCHECK;
    result->data.nullcheck.nullcheckexpr = lalloc(sizeof(ENode));
    *result->data.nullcheck.nullcheckexpr = *nullcheckexpr;
    result->data.nullcheck.condexpr = condexpr;
    result->data.nullcheck.precompid = CParser_GetUniqueID();

    nullcheckexpr->type = EPRECOMP;
    nullcheckexpr->data.precompid = result->data.nullcheck.precompid;

    return result;
}

ENode* CExpr_SafeClassPointerCast(
    ENode* expr, TypeClass* a, TypeClass* b, Boolean typconflag, Boolean pathcheckflag
) {
    ENode* result;

    result = CClass_ClassPointerCast(expr, a, b, typconflag, TRUE, pathcheckflag);
    if (result != expr) {
        if (!(ENODE_IS(result, ETYPCON) && result->data.monadic == expr)) {
            result = do_castnullcheck(result, expr);
        }
    }
    return result;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -2
#endif
ENode* PointerToMemberCast(
    ENode* expr, TypeMemberPointer* tm1, TypeMemberPointer* tm2, Boolean checkAccess
) {
    BClassList* path;
    Boolean negate;
    short depth;
    Boolean isambig;
    SInt32 pathoffset;
    CInt64 pathoffset64;
    ENode* result;

    CError_ASSERT(3986, IS_TYPE_CLASS(tm1->ty2));
    CError_ASSERT(3987, IS_TYPE_CLASS(tm2->ty2));

    if (tm1->ty2 == tm2->ty2) {
        expr->rtype = TYPE(tm2);
        return expr;
    }

    negate = FALSE;
    path = CClass_GetBasePath(TYPE_CLASS(tm2->ty2), TYPE_CLASS(tm1->ty2), &depth, &isambig);
    if (!path) {
        path = CClass_GetBasePath(TYPE_CLASS(tm1->ty2), TYPE_CLASS(tm2->ty2), &depth, &isambig);
        if (!path) {
            goto failed;
        }
        negate = TRUE;
    }

    if (isambig) {
        CError_Error(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
    }

    if ((pathoffset = CClass_GetPathOffset(path)) < 0) {
        goto failed;
    }
    if (negate) {
        pathoffset = -pathoffset;
    }

    if (checkAccess) {
        CClass_CheckPathAccess(path, NULL, ACCESSPUBLIC);
    }

    if (tm1->size != tm2->size) {
failed:
        CError_Error(ERR_ILLEGAL_EXPLICIT_CONVERSION_FROM, tm1, 0, tm2, 0);
        return nullnode();
    }

    if (!pathoffset) {
        expr->rtype = TYPE(tm2);
        return expr;
    }

    if (tm1->size == 4u) {
        if (ENODE_IS(expr, EINTCONST)) {
            if (!CInt64_IsZero(&expr->data.intval)) {
                CInt64_SetLong(&pathoffset64, pathoffset);
                expr->data.intval = CInt64_Add(expr->data.intval, pathoffset64);
            }
            expr->rtype = TYPE(tm2);
            return expr;
        } else {
            expr->rtype = TYPE(&stunsignedlong);
            result = makemonadicnode(
                makediadicnode(expr, intconstnode(TYPE(&stunsignedlong), pathoffset), EADD), ETYPCON
            );
            result->rtype = TYPE(tm2);
            return do_castnullcheck(result, expr);
        }
    } else {
        expr = funccallexpr(
            rt_ptmf_cast,
            intconstnode(TYPE(&stsignedlong), pathoffset),
            getnodeaddress(expr, FALSE),
            create_temp_node(TYPE(&ptmstruct)),
            NULL
        );
        result = makemonadicnode(expr, EINDIRECT);
        result->rtype = TYPE(tm2);
        return result;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

ENode* CExpr_MemberPointerConversion(ENode* expr, TypeMemberPointer* type, Boolean buildData) {
    if (ENODE_IS(expr, EINTCONST) && CInt64_IsZero(&expr->data.intval)) {
        if (IS_TYPE_FUNC(type->ty1)) {
            expr = create_objectnode(rt_ptmf_null);
        }
        expr->rtype = TYPE(type);
        return expr;
    }
    if (ENODE_IS(expr, EMEMBER)) {
        return getpointertomemberfunc(expr, TYPE(type), buildData);
    }

    return expr;
}

static ENode* CExpr_MemberPointerCast(ENode* expr, TypeMemberPointer* type, UInt32 qual) {
    if (!IS_TYPE_MEMBERPOINTER(expr->rtype)) {
        expr = CExpr_MemberPointerConversion(expr, type, TRUE);
    }

    if (!IS_TYPE_MEMBERPOINTER(expr->rtype)) {
        CError_Error(ERR_ILLEGAL_TYPE_CAST);
        return nullnode();
    }

    expr = PointerToMemberCast(expr, TYPE_MEMBER_POINTER(expr->rtype), type, FALSE);
    expr->flags = qual & ENODE_FLAG_QUALS;
    return expr;
}

ENode* do_typecast(ENode* expr, Type* type, UInt32 qual) {
    TypePointer* tptr;
    ENode* tmp;
    short flags;

    if (!copts.old_argmatch) {
        expr = CExpr_Convert(expr, type, qual, TRUE, FALSE);
        return expr;
    }

    if (copts.cpp_extensions && is_typesame(expr->rtype, type) && !ENODE_IS(expr, EOBJLIST)) {
        expr->rtype = type;
        expr->flags &= ~ENODE_FLAG_QUALS;
        expr->flags |= qual & ENODE_FLAG_QUALS;
        return expr;
    }

    switch (type->type) {
        case TYPEARRAY:
            CError_Error(
                ERR_ILLEGAL_EXPLICIT_CONVERSION_FROM, expr->rtype, ENODE_QUALS(expr), type, qual
            );
            return expr;
        case TYPEMEMBERPOINTER:
            if (!IS_TYPE_CLASS(expr->rtype)) {
                return CExpr_MemberPointerCast(expr, TYPE_MEMBER_POINTER(type), qual);
            }
        default:
            break;
    }

    flags = qual & ENODE_FLAG_QUALS;

    if (ENODE_IS(expr, EOBJLIST)) {
        return CExpr_AssignmentPromotion(expr, type, qual & ENODE_FLAG_QUALS, TRUE);
    }

    if (ENODE_IS(expr, EOBJREF) && IS_TYPE_NONSTATIC_METHOD(expr->data.objref->type)) {
        CError_Error(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
        return nullnode();
    }

    if (type == &stvoid) {
        expr = makemonadicnode(expr, ETYPCON);
        expr->rtype = type;
        expr->flags = flags;
        return expr;
    }

    if (IS_TYPE_REFERENCE(type)) {
        tmp = getnodeaddress(expr, FALSE);
        tptr = galloc(sizeof(TypePointer));
        *tptr = *TYPE_POINTER(type);
        tptr->qual &= ~Q_REFERENCE;

        tmp = do_typecast(tmp, TYPE(tptr), qual);
        tmp = makemonadicnode(tmp, EINDIRECT);
        tmp->rtype = TPTR_TARGET(type);
        tmp->flags = flags;
        return tmp;
    }

    if (IS_TYPE_CLASS(expr->rtype) || IS_TYPE_CLASS(type)) {
        if (expr->rtype->size == 0) {
            CDecl_CompleteType(expr->rtype);
        }

        if (expr->rtype == type && !CClass_CopyConstructor(TYPE_CLASS(type))) {
            return expr;
        }

        if (user_assign_check(expr, type, qual, TRUE, TRUE, TRUE)) {
            assign_node->flags = flags;
            return assign_node;
        }

        CError_Error(
            ERR_ILLEGAL_EXPLICIT_CONVERSION_FROM, expr->rtype, ENODE_QUALS(expr), type, qual
        );
        return nullnode();
    }

    if (IS_TYPE_STRUCT(type) && copts.cplusplus && is_typesame(expr->rtype, type)) {
        expr->rtype = type;
        expr->flags = flags;
        return expr;
    }

    if (type == TYPE(&stbool)) {
        expr = CExpr_ConvertToBool(expr, TRUE);
        expr->flags = flags;
        return expr;
    }

    if (IS_TYPE_ENUM(type)) {
        tmp = do_typecast(expr, TYPE_ENUM(type)->enumtype, qual);
        if (!ENODE_IS(tmp, EINTCONST)) {
            tmp = makemonadicnode(tmp, ETYPCON);
        }
        tmp->rtype = type;
        tmp->flags = flags;
        return tmp;
    }

    if (IS_TYPE_INT_OR_FLOAT(type)) {
        if (ENODE_IS(expr, ETYPCON) &&
            expr->rtype->type == type->type &&
            expr->rtype->size == type->size) {
            if (is_unsigned(expr->rtype) == is_unsigned(type) && ENODE_QUALS(expr) == qual) {
                expr->rtype = type;
                expr->flags |= ENODE_FLAG_SAME_TYPE_CAST;
                return expr;
            }
        }

        if (IS_TYPE_ENUM(expr->rtype)) {
            expr = forceintegral(expr);
        }

        if (IS_TYPE_INT_OR_FLOAT(expr->rtype)) {
            expr = promote(expr, type);
            expr->flags = flags;
            return expr;
        }

        if (!(IS_TYPE_POINTER_ONLY(expr->rtype) && !IS_TYPE_FLOAT(type))) {
            CError_Error(
                ERR_ILLEGAL_EXPLICIT_CONVERSION_FROM, expr->rtype, ENODE_QUALS(expr), type, qual
            );
        }

        if (ENODE_IS(expr, ETYPCON) && ENODE_IS(tmp = expr->data.monadic, EINTCONST)) {
            tmp->rtype = type;
            tmp->flags = flags;
            tmp->data.intval = CExpr_IntConstConvert(type, TYPE(&stunsignedlong), tmp->data.intval);
            return tmp;
        }

        if (type->size != 4) {
            expr = makemonadicnode(expr, ETYPCON);
            expr->rtype = TYPE(&stunsignedlong);
        }

        expr = makemonadicnode(expr, ETYPCON);
        expr->rtype = type;
        expr->flags = flags;
        return expr;
    }

    if (IS_TYPE_POINTER(type)) {
        if (IS_TYPE_POINTER(expr->rtype)) {
            tptr = TYPE_POINTER(TPTR_TARGET(expr->rtype));
            if (IS_TYPE_CLASS(tptr) && IS_TYPE_CLASS(TPTR_TARGET(type))) {
                expr = CExpr_SafeClassPointerCast(
                    expr, TYPE_CLASS(tptr), TYPE_CLASS(TPTR_TARGET(type)), TRUE, FALSE
                );
            }

            if (!ENODE_IS(expr, ETYPCON)) {
                expr = makemonadicnode(expr, ETYPCON);
            }

            expr->rtype = type;
            expr->flags = flags;
            return expr;
        }

        if (!IS_TYPE_INT(expr->rtype)) {
            if (!IS_TYPE_ENUM(expr->rtype)) {
                CError_Error(
                    ERR_ILLEGAL_EXPLICIT_CONVERSION_FROM, expr->rtype, ENODE_QUALS(expr), type, qual
                );
                return expr;
            }
            expr = forceintegral(expr);
        }

        if (expr->rtype->size != 4) {
            if (!ENODE_IS(expr, EINTCONST)) {
                expr = makemonadicnode(expr, ETYPCON);
            }
            expr->rtype = TYPE(&stunsignedlong);
        }

        expr = makemonadicnode(expr, ETYPCON);
        expr->rtype = type;
        expr->flags = flags;
        return expr;
    }

    if ((tmp = CodeGen_HandleTypeCast(expr, type, qual))) {
        return tmp;
    }

    CError_Error(ERR_ILLEGAL_EXPLICIT_CONVERSION_FROM, expr->rtype, ENODE_QUALS(expr), type, qual);
    return nullnode();
}

static Boolean isvectorconst(ENode* node) {
    if (ENODE_IS3(node, ECOMMA, EINTCONST, EFLOATCONST)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

ENode* cast_expression(void) {
    ENode* expr;
    ENode* result;
    ENodeList* args;
    DeclInfo di;
    MWVector128 vec;

    if (!(tk == '(' && islookaheaddeclaration())) {
        return unary_expression();
    }
    tk = lex();

    memclrw(&di, sizeof(DeclInfo));
    CParser_GetDeclSpecs(&di, FALSE);
    scandeclarator(&di);

    if (tk != ')') {
        CError_ErrorSkip(ERR_RPAREN_EXPECTED);
    } else {
        tk = lex();
    }

    if (di.name) {
        CError_Error(ERR_ILLEGAL_TYPE_CAST);
    }

    if (copts.altivec_model && tk == '(' && IS_TYPE_VECTOR(di.thetype)) {
#if VERSION >= VERSION_GC_2_0
        Boolean save_warn = copts.warn_no_side_effect;
        copts.warn_no_side_effect = FALSE;
#endif
        tk = lex();
        expr = s_expression();
#if VERSION >= VERSION_GC_2_0
        copts.warn_no_side_effect = save_warn;
#endif
        if (tk != ')') {
            CError_ErrorSkip(ERR_RPAREN_EXPECTED);
        } else {
            tk = lex();
        }

        if (CodeGen_CollapseVectorExpression(expr, &vec, di.thetype)) {
            result = lalloc(sizeof(ENode));
            result->type = EVECTOR128CONST;
            if ((result->cost = expr->cost) == 0) {
                result->cost = 1;
            }
            result->flags = ENODE_QUALS(expr);
            result->rtype = di.thetype;
            result->data.vector128val = vec;
        } else {
            result = makemonadicnode(expr, ETYPCON);
        }
        result->rtype = di.thetype;
        result->flags = expr->flags;
        return result;
    }

    if (tk == '{' && (!copts.ANSIstrict || copts.c9x) && !IS_TYPE_VECTOR(di.thetype)) {
        return CInit_AutoObject(NULL, di.thetype, di.qual);
    }

    expr = cast_expression();
    if (copts.cplusplus &&
        (CTemplTool_IsTemplateArgumentDependentType(di.thetype) ||
         CTemplTool_IsTemplateArgumentDependentExpression(expr))) {
        args = lalloc(sizeof(ENodeList));
        args->next = NULL;
        args->node = expr;
        return CExpr_TemplArgDepCast(di.thetype, di.qual, args);
    }

    if (!IS_TYPE_REFERENCE(di.thetype)) {
        expr = pointer_generation(expr);
    }
    return do_typecast(expr, di.thetype, di.qual);
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static ENode* pm_expression(void) {
    ENode* left;
    ENode* right;
    ENode* result;
    Type* type;
    Conversion conv;
    UInt32 qual;
    short flags;

    left = cast_expression();
restart:
    switch (tk) {
        case TK_ARROW_STAR:
            left = pointer_generation(left);
            tk = lex();
            right = pointer_generation(cast_expression());

            if (CExpr_CheckOperator(TK_ARROW_STAR, left, right, &conv)) {
                CError_ASSERT(4464, left = conv.expression);
                goto restart;
            }

            if (!IS_TYPE_POINTER(left->rtype)) {
                CError_Error(ERR_POINTER_ARRAY_REQUIRED);
                return left;
            }

            left = makemonadicnode(left, EINDIRECT);
            left->rtype = TPTR_TARGET(left->rtype);
            goto common_part;
        case TK_DOT_STAR:
            left = pointer_generation(left);
            tk = lex();
            right = pointer_generation(cast_expression());
            if (!ENODE_IS(left, EINDIRECT)) {
                CError_Error(ERR_NOT_LVALUE);
                return left;
            }
common_part:
            if (!IS_TYPE_CLASS(left->rtype)) {
                CError_Error(ERR_NOT_STRUCT_UNION_CLASS);
                return left;
            }
            if (!IS_TYPE_MEMBERPOINTER(right->rtype)) {
                CError_Error(ERR_ILLEGAL_OPERAND);
                return left;
            }
            if (left->rtype != TYPE_MEMBER_POINTER(right->rtype)->ty2) {
                if (CClass_IsBaseClass(
                        TYPE_CLASS(left->rtype),
                        TYPE_CLASS(TYPE_MEMBER_POINTER(right->rtype)->ty2),
                        NULL,
                        TRUE,
                        TRUE
                    )) {
                    left->data.monadic = CClass_ClassPointerCast(
                        left->data.monadic,
                        TYPE_CLASS(left->rtype),
                        TYPE_CLASS(TYPE_MEMBER_POINTER(right->rtype)->ty2),
                        FALSE,
                        TRUE,
                        TRUE
                    );
                    left->rtype = TYPE_MEMBER_POINTER(right->rtype)->ty2;
                } else {
                    CError_Error(ERR_ILLEGAL_TYPE);
                    return left;
                }
            }
            type = CClass_CombineClassAccessQualifiers(
                TYPE_MEMBER_POINTER(right->rtype)->ty1, ENODE_QUALS(right), left->flags, &qual
            );
            flags = qual;
            if (!IS_TYPE_FUNC(type)) {
                if (!ENODE_IS(right, EINTCONST)) {
                    if (!canadd(left->data.monadic, -1)) {
                        left->data.monadic = makediadicnode(left->data.monadic, nullnode(), EADD);
                        CInt64_SetLong(&left->data.monadic->data.diadic.right->data.intval, -1);
                        optimizecomm(left->data.monadic);
                    }
                    right->rtype = TYPE(&stunsignedlong);
                    left->data.monadic = makediadicnode(left->data.monadic, right, EADD);
                    optimizecomm(left->data.monadic);
                } else {
                    right->data.intval = CInt64_Sub(right->data.intval, cint64_one);
                    if (!canadd2(left->data.monadic, right->data.intval)) {
                        right->rtype = TYPE(&stunsignedlong);
                        left->data.monadic = makediadicnode(left->data.monadic, right, EADD);
                        optimizecomm(left->data.monadic);
                    }
                }

                if (IS_TYPE_BITFIELD(type)) {
                    left->data.monadic = makemonadicnode(left->data.monadic, EBITFIELD);
                    left->data.monadic->rtype = type;
                    left->rtype = TYPE_BITFIELD(type)->bitfieldtype;
                } else {
                    left->rtype = type;
                }
                left->flags = flags;
                left = checkreference(left);
                goto restart;
            } else {
                CError_ASSERT(4542, ENODE_IS(right, EINDIRECT));
                result = lalloc(sizeof(ENode));
                result->type = EMFPOINTER;
                result->cost = 4;
                result->flags = 0;
                result->rtype = &stvoid;
                result->data.mfpointer.accessnode = left;
                result->data.mfpointer.mfpointer = right;
                return result;
            }
        default:
            return left;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_EMUL_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, EMUL, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator('*', left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(4573, left = conv.left);
        CError_ASSERT(4574, right = conv.right);
    }

    return makemultnode(left, right);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_EDIV_Node(ENode* left, ENode* right, Boolean no_warning) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, EDIV, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator('/', left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(4599, left = conv.left);
        CError_ASSERT(4600, right = conv.right);
    }

    return makedivnode(left, right, no_warning);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_EMODULO_Node(ENode* left, ENode* right, Boolean no_warning) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, EMODULO, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator('%', left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(4625, left = conv.left);
        CError_ASSERT(4626, right = conv.right);
    }

    left = integralpromote(left);
    right = integralpromote(right);
    CExpr_ArithmeticConversion(&left, &right);

    if (iszero(right)) {
        if (!no_warning) {
            CError_Warning(ERR_DIVISION_BY_0);
        }
        return left;
    }

    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, '%', right->data.intval);
        return left;
    }

    if (iszero(left)) {
        return makediadicnode(right, left, ECOMMA);
    }

    if (CExpr_IsOne(right)) {
        right = nullnode();
        right->rtype = left->rtype;
        return makediadicnode(left, right, ECOMMA);
    }

    return makediadicnode(left, right, EMODULO);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_EADD_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, EADD, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator('+', left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(4672, left = conv.left);
        CError_ASSERT(4673, right = conv.right);
    }

    return makeaddnode(left, right);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_ESUB_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, ESUB, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator('-', left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(4697, left = conv.left);
        CError_ASSERT(4698, right = conv.right);
    }

    return makesubnode(left, right);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_ESHL_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, ESHL, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator(TK_SHL, left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(4722, left = conv.left);
        CError_ASSERT(4723, right = conv.right);
    }

    left = integralpromote(left);
    right = integralpromote(right);

    if (iszero(left) || iszero(right)) {
        return left;
    }

    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, TK_SHL, right->data.intval);
        return left;
    }

    return CodeGen_CheckExpr(makediadicnode(left, right, ESHL));
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_ESHR_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, ESHR, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator(TK_SHR, left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(4759, left = conv.left);
        CError_ASSERT(4760, right = conv.right);
    }

    left = integralpromote(left);
    right = integralpromote(right);

    if (iszero(left) || iszero(right)) {
        return left;
    }

    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, TK_SHR, right->data.intval);
        return left;
    }

    return CodeGen_CheckExpr(makediadicnode(left, right, ESHR));
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static ENode* pointercompare(ENodeType nt, ENode* left, ENode* right) {
    Type* ltype;
    Type* rtype;
    ENode* expr;

    ltype = left->rtype;
    rtype = right->rtype;
    if (IS_TYPE_POINTER_ONLY(ltype) && IS_TYPE_POINTER_ONLY(rtype)) {
        ltype = TPTR_TARGET(ltype);
        rtype = TPTR_TARGET(rtype);
        if (IS_TYPE_CLASS(ltype) && IS_TYPE_CLASS(rtype)) {
            if (ltype != rtype) {
                if (ltype == TPTR_TARGET(left->rtype)) {
                    if (CClass_IsBaseClass(
                            TYPE_CLASS(ltype), TYPE_CLASS(rtype), NULL, FALSE, TRUE
                        )) {
                        left = CExpr_SafeClassPointerCast(
                            left, TYPE_CLASS(ltype), TYPE_CLASS(rtype), FALSE, TRUE
                        );
                    } else if (CClass_IsBaseClass(
                                   TYPE_CLASS(rtype), TYPE_CLASS(ltype), NULL, FALSE, TRUE
                               )) {
                        expr = CExpr_SafeClassPointerCast(
                            right, TYPE_CLASS(rtype), TYPE_CLASS(ltype), FALSE, TRUE
                        );
                        right = expr;
                    } else {
                        CError_Error(
                            ERR_TYPE_MISMATCH,
                            left->rtype,
                            ENODE_QUALS(left),
                            right->rtype,
                            ENODE_QUALS(right)
                        );
                    }
                } else {
                    CError_Error(
                        ERR_TYPE_MISMATCH,
                        left->rtype,
                        ENODE_QUALS(left),
                        right->rtype,
                        ENODE_QUALS(right)
                    );
                }
            }
        } else if (!is_typeequal(left->rtype, right->rtype)) {
            if (!copts.objective_c || !CObjC_IsCompatibleType(left->rtype, right->rtype)) {
                CError_Error(
                    ERR_TYPE_MISMATCH,
                    left->rtype,
                    ENODE_QUALS(left),
                    right->rtype,
                    ENODE_QUALS(right)
                );
            }
        }
    } else if (nt == EEQU || nt == ENOTEQU) {
        if (IS_TYPE_INT(ltype)) {
            if (!(ENODE_IS(left, EINTCONST) && CInt64_IsZero(&left->data.intval))) {
                CError_Error(ERR_ILLEGAL_OPERAND);
            }
            CError_ASSERT(4854, IS_TYPE_POINTER_ONLY(rtype));
            left->rtype = TYPE(&stunsignedlong);
        } else if (IS_TYPE_INT(rtype)) {
            if (!(ENODE_IS(right, EINTCONST) && CInt64_IsZero(&right->data.intval))) {
                CError_Error(ERR_ILLEGAL_OPERAND);
            }
            CError_ASSERT(4862, IS_TYPE_POINTER_ONLY(ltype));
            right->rtype = TYPE(&stunsignedlong);
        } else if (!is_typeequal(ltype, rtype)) {
            CError_Error(
                ERR_TYPE_MISMATCH, left->rtype, ENODE_QUALS(left), right->rtype, ENODE_QUALS(right)
            );
        }
    } else {
        if (!is_typeequal(ltype, rtype)) {
            CError_Error(
                ERR_TYPE_MISMATCH, left->rtype, ENODE_QUALS(left), right->rtype, ENODE_QUALS(right)
            );
        }
    }

    return logicalexpression(makediadicnode(left, right, nt));
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static ENode* unsigncheck(ENode* expr, Boolean isLess, Boolean orEqual) {
    if (is_unsigned(expr->data.diadic.left->rtype)) {
        if (ENODE_IS(expr->data.diadic.left, EINTCONST) &&
            CInt64_IsZero(&expr->data.diadic.left->data.intval)) {
            isLess = !isLess;
        } else if (!(ENODE_IS(expr->data.diadic.right, EINTCONST) &&
                     CInt64_IsZero(&expr->data.diadic.right->data.intval))) {
            return logicalexpression(expr);
        }

        if (isLess && orEqual) {
            expr->type = EEQU;
            return logicalexpression(expr);
        }
        if (!isLess && !orEqual) {
            expr->type = ENOTEQU;
            return logicalexpression(expr);
        }
        return CExpr_ConstResult(logicalexpression(expr), !isLess);
    }
    return logicalexpression(expr);
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_ELESS_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, ELESS, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator('<', left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(4936, left = conv.left);
        CError_ASSERT(4937, right = conv.right);
    }

    if (IS_TYPE_POINTER(left->rtype) || IS_TYPE_POINTER(right->rtype)) {
        return pointercompare(ELESS, left, right);
    }

    CExpr_CompareConvert(&left, "<", &right, FALSE);
    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, '<', right->data.intval);
        left->rtype = CParser_GetBoolType();
    } else if (ENODE_IS(left, EFLOATCONST) && ENODE_IS(right, EFLOATCONST)) {
        CInt64_SetLong(
            &left->data.intval,
            CMach_CalcFloatDiadicBool(left->rtype, left->data.floatval, '<', right->data.floatval)
        );
        left->type = EINTCONST;
        left->rtype = CParser_GetBoolType();
    } else {
        left = unsigncheck(makediadicnode(left, right, ELESS), TRUE, FALSE);
    }

    left = CodeGen_CheckExpr(left);
    return left;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_ELESSEQU_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, ELESSEQU, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator(TK_LESS_EQUAL, left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(4983, left = conv.left);
        CError_ASSERT(4984, right = conv.right);
    }

    if (IS_TYPE_POINTER(left->rtype) || IS_TYPE_POINTER(right->rtype)) {
        return pointercompare(ELESSEQU, left, right);
    }

    CExpr_CompareConvert(&left, "<=", &right, FALSE);
    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, TK_LESS_EQUAL, right->data.intval);
        left->rtype = CParser_GetBoolType();
    } else if (ENODE_IS(left, EFLOATCONST) && ENODE_IS(right, EFLOATCONST)) {
        CInt64_SetLong(
            &left->data.intval,
            CMach_CalcFloatDiadicBool(
                left->rtype, left->data.floatval, TK_LESS_EQUAL, right->data.floatval
            )
        );
        left->type = EINTCONST;
        left->rtype = CParser_GetBoolType();
    } else {
        left = unsigncheck(makediadicnode(left, right, ELESSEQU), TRUE, TRUE);
    }

    left = CodeGen_CheckExpr(left);
    return left;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_EGREATER_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, EGREATER, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator('>', left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(5030, left = conv.left);
        CError_ASSERT(5031, right = conv.right);
    }

    if (IS_TYPE_POINTER(left->rtype) || IS_TYPE_POINTER(right->rtype)) {
        return pointercompare(EGREATER, left, right);
    }

    CExpr_CompareConvert(&left, ">", &right, FALSE);
    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, '>', right->data.intval);
        left->rtype = CParser_GetBoolType();
    } else if (ENODE_IS(left, EFLOATCONST) && ENODE_IS(right, EFLOATCONST)) {
        CInt64_SetLong(
            &left->data.intval,
            CMach_CalcFloatDiadicBool(left->rtype, left->data.floatval, '>', right->data.floatval)
        );
        left->type = EINTCONST;
        left->rtype = CParser_GetBoolType();
    } else {
        left = unsigncheck(makediadicnode(left, right, EGREATER), FALSE, FALSE);
    }

    left = CodeGen_CheckExpr(left);
    return left;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_EGREATEREQU_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, EGREATEREQU, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator(TK_GREATER_EQUAL, left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(5077, left = conv.left);
        CError_ASSERT(5078, right = conv.right);
    }

    if (IS_TYPE_POINTER(left->rtype) || IS_TYPE_POINTER(right->rtype)) {
        return pointercompare(EGREATEREQU, left, right);
    }

    CExpr_CompareConvert(&left, ">=", &right, FALSE);
    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval = CMach_CalcIntDiadic(
            left->rtype, left->data.intval, TK_GREATER_EQUAL, right->data.intval
        );
        left->rtype = CParser_GetBoolType();
    } else if (ENODE_IS(left, EFLOATCONST) && ENODE_IS(right, EFLOATCONST)) {
        CInt64_SetLong(
            &left->data.intval,
            CMach_CalcFloatDiadicBool(
                left->rtype, left->data.floatval, TK_GREATER_EQUAL, right->data.floatval
            )
        );
        left->type = EINTCONST;
        left->rtype = CParser_GetBoolType();
    } else {
        left = unsigncheck(makediadicnode(left, right, EGREATEREQU), FALSE, TRUE);
    }

    left = CodeGen_CheckExpr(left);
    return left;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

ENode* memberpointercompare(ENodeType nt, ENode* left, ENode* right) {
    Object* func;
    ENodeList* arg;

    if (!IS_TYPE_MEMBERPOINTER(left->rtype)) {
        if (!(IS_TYPE_INT(left->rtype) &&
              ENODE_IS(left, EINTCONST) &&
              CInt64_IsZero(&left->data.intval))) {
            CError_Error(ERR_ILLEGAL_OPERAND);
            return nullnode();
        }
    } else if (!IS_TYPE_MEMBERPOINTER(right->rtype)) {
        if (!(IS_TYPE_INT(right->rtype) &&
              ENODE_IS(right, EINTCONST) &&
              CInt64_IsZero(&right->data.intval))) {
            CError_Error(ERR_ILLEGAL_OPERAND);
            return nullnode();
        }
    } else if (!is_typeequal(left->rtype, right->rtype)) {
        left = PointerToMemberCast(
            left, TYPE_MEMBER_POINTER(left->rtype), TYPE_MEMBER_POINTER(right->rtype), TRUE
        );
    }

    if ((ENODE_IS(left, EINTCONST) || !IS_TYPE_FUNC(TYPE_MEMBER_POINTER(left->rtype)->ty1)) &&
        (ENODE_IS(right, EINTCONST) || !IS_TYPE_FUNC(TYPE_MEMBER_POINTER(right->rtype)->ty1))) {
        left->rtype = TYPE(&stunsignedlong);
        right->rtype = TYPE(&stunsignedlong);
        return logicalexpression(makediadicnode(left, right, nt));
    }

    arg = lalloc(sizeof(ENodeList));
    if (ENODE_IS(left, EINTCONST) || ENODE_IS(right, EINTCONST)) {
        func = rt_ptmf_test;
        if (ENODE_IS(left, EINTCONST)) {
            arg->node = getnodeaddress(right, FALSE);
        } else {
            arg->node = getnodeaddress(left, FALSE);
        }
        arg->next = NULL;
    } else {
        func = rt_ptmf_cmpr;
        arg->next = lalloc(sizeof(ENodeList));
        arg->node = getnodeaddress(left, FALSE);
        arg->next->node = getnodeaddress(right, FALSE);
        arg->next->next = NULL;
    }

    left = lalloc(sizeof(ENode));
    left->type = EFUNCCALL;
    left->rtype = TYPE(&stsignedlong);
    left->cost = 4;
    left->data.funccall.funcref = create_objectrefnode(func);
    left->data.funccall.args = arg;
    left->data.funccall.functype = TYPE_FUNC(func->type);
    left->flags = TYPE_FUNC(func->type)->qual & ENODE_FLAG_QUALS;

    if (nt == EEQU) {
        left = makemonadicnode(left, ELOGNOT);
    }

    return left;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_EEQU_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, EEQU, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator(TK_LOGICAL_EQ, left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(5208, left = conv.left);
        CError_ASSERT(5209, right = conv.right);
    }

    if (IS_TYPE_POINTER(left->rtype) || IS_TYPE_POINTER(right->rtype)) {
        return pointercompare(EEQU, left, right);
    }
    if (IS_TYPE_MEMBERPOINTER(left->rtype) || IS_TYPE_MEMBERPOINTER(right->rtype)) {
        return memberpointercompare(EEQU, left, right);
    }

    CExpr_CompareConvert(&left, "==", &right, TRUE);
    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, TK_LOGICAL_EQ, right->data.intval);
        left->rtype = CParser_GetBoolType();
    } else if (ENODE_IS(left, EFLOATCONST) && ENODE_IS(right, EFLOATCONST)) {
        left->type = EINTCONST;
        CInt64_SetLong(
            &left->data.intval,
            CMach_CalcFloatDiadicBool(
                left->rtype, left->data.floatval, TK_LOGICAL_EQ, right->data.floatval
            )
        );
        left->type = EINTCONST;
        left->rtype = CParser_GetBoolType();
    } else {
        left = makediadicnode(left, right, EEQU);
        optimizecomm(left);
    }

    left = CodeGen_CheckExpr(left);
    return logicalexpression(left);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_ENOTEQU_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, ENOTEQU, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator(TK_LOGICAL_NE, left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(5268, left = conv.left);
        CError_ASSERT(5269, right = conv.right);
    }

    if (IS_TYPE_POINTER(left->rtype) || IS_TYPE_POINTER(right->rtype)) {
        return pointercompare(ENOTEQU, left, right);
    }
    if (IS_TYPE_MEMBERPOINTER(left->rtype) || IS_TYPE_MEMBERPOINTER(right->rtype)) {
        return memberpointercompare(ENOTEQU, left, right);
    }

    CExpr_CompareConvert(&left, "!=", &right, TRUE);
    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, TK_LOGICAL_NE, right->data.intval);
        left->rtype = CParser_GetBoolType();
    } else if (ENODE_IS(left, EFLOATCONST) && ENODE_IS(right, EFLOATCONST)) {
        CInt64_SetLong(
            &left->data.intval,
            CMach_CalcFloatDiadicBool(
                left->rtype, left->data.floatval, TK_LOGICAL_NE, right->data.floatval
            )
        );
        left->type = EINTCONST;
        left->rtype = CParser_GetBoolType();
    } else {
        left = makediadicnode(left, right, ENOTEQU);
        optimizecomm(left);
    }

    left = CodeGen_CheckExpr(left);
    return logicalexpression(left);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_EAND_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, EAND, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator('&', left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(5328, left = conv.left);
        CError_ASSERT(5329, right = conv.right);
    }

    left = integralpromote(left);
    right = integralpromote(right);
    CExpr_ArithmeticConversion(&left, &right);

    if (iszero(left) || CExpr_AllBitsSet(right)) {
        return left;
    }
    if (iszero(right) || CExpr_AllBitsSet(left)) {
        return right;
    }

    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, '&', right->data.intval);
        return left;
    }

    left = makediadicnode(left, right, EAND);
    left = CodeGen_CheckExpr(left);
    optimizecomm(left);
    return left;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_EXOR_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, EXOR, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator('^', left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(5367, left = conv.left);
        CError_ASSERT(5368, right = conv.right);
    }

    left = integralpromote(left);
    right = integralpromote(right);
    CExpr_ArithmeticConversion(&left, &right);

    if (iszero(right)) {
        return left;
    }
    if (iszero(left)) {
        return right;
    }

    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, '^', right->data.intval);
        return left;
    }

    left = makediadicnode(left, right, EXOR);
    left = CodeGen_CheckExpr(left);
    optimizecomm(left);
    return left;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_EOR_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, EOR, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator('|', left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(5406, left = conv.left);
        CError_ASSERT(5407, right = conv.right);
    }

    left = integralpromote(left);
    right = integralpromote(right);
    CExpr_ArithmeticConversion(&left, &right);

    if (iszero(right) || CExpr_AllBitsSet(left)) {
        return left;
    }
    if (iszero(left) || CExpr_AllBitsSet(right)) {
        return right;
    }

    if (ENODE_IS(left, EINTCONST) && ENODE_IS(right, EINTCONST)) {
        left->data.intval =
            CMach_CalcIntDiadic(left->rtype, left->data.intval, '|', right->data.intval);
        return left;
    }

    left = makediadicnode(left, right, EOR);
    left = CodeGen_CheckExpr(left);
    optimizecomm(left);
    return left;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_ELAND_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, ELAND, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator(TK_LOGICAL_AND, left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(5445, left = conv.left);
        CError_ASSERT(5446, right = conv.right);
    }

    switch (left->rtype->type) {
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEPOINTER:
        case TYPEARRAY:
            break;
        case TYPEENUM:
        case TYPEMEMBERPOINTER:
            left = CExpr_ConvertToCondition(left);
            break;
        default:
            CError_Error(ERR_ILLEGAL_OPERAND);
            left = nullnode();
    }
    switch (right->rtype->type) {
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEPOINTER:
        case TYPEARRAY:
            break;
        case TYPEENUM:
        case TYPEMEMBERPOINTER:
            right = CExpr_ConvertToCondition(right);
            break;
        default:
            CError_Error(ERR_ILLEGAL_OPERAND);
            right = nullnode();
    }

    if (iszero(left)) {
        left->type = EINTCONST;
        left->rtype = CParser_GetBoolType();
        CInt64_SetLong(&left->data.intval, 0);
        return left;
    }

    if (isnotzero(left)) {
        if (iszero(right)) {
            left->type = EINTCONST;
            left->rtype = CParser_GetBoolType();
            CInt64_SetLong(&left->data.intval, 0);
            return left;
        } else if (isnotzero(right)) {
            left->type = EINTCONST;
            left->rtype = CParser_GetBoolType();
            CInt64_SetLong(&left->data.intval, 1);
            return left;
        } else {
            left = makemonadicnode(right, ELOGNOT);
            left->rtype = CParser_GetBoolType();
            return makemonadicnode(left, ELOGNOT);
        }
    } else {
        if (isnotzero(right)) {
            left = makemonadicnode(left, ELOGNOT);
            left->rtype = CParser_GetBoolType();
            return makemonadicnode(left, ELOGNOT);
        } else if (iszero(right)) {
            right->type = EINTCONST;
            right->rtype = CParser_GetBoolType();
            CInt64_SetLong(&right->data.intval, 0);
            return makecommaexpression(left, right);
        } else {
            left = makediadicnode(left, right, ELAND);
            left->rtype = CParser_GetBoolType();
            return CodeGen_CheckExpr(left);
        }
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_ELOR_Node(ENode* left, ENode* right) {
    Conversion conv;

    if (IS_TYPE_TEMPLDEPEXPR(left->rtype) || IS_TYPE_TEMPLDEPEXPR(right->rtype)) {
        return CTempl_MakeTemplDepExpr(left, ELOR, right);
    }

    left = pointer_generation(left);
    right = pointer_generation(right);
    if (copts.cplusplus && CExpr_CheckOperator(TK_LOGICAL_OR, left, right, &conv)) {
        if (conv.expression) {
            return conv.expression;
        }
        CError_ASSERT(5550, left = conv.left);
        CError_ASSERT(5551, right = conv.right);
    }

    switch (left->rtype->type) {
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEPOINTER:
        case TYPEARRAY:
            break;
        case TYPEENUM:
        case TYPEMEMBERPOINTER:
            left = CExpr_ConvertToCondition(left);
            break;
        default:
            CError_Error(ERR_ILLEGAL_OPERAND);
            left = nullnode();
    }
    switch (right->rtype->type) {
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEPOINTER:
        case TYPEARRAY:
            break;
        case TYPEENUM:
        case TYPEMEMBERPOINTER:
            right = CExpr_ConvertToCondition(right);
            break;
        default:
            CError_Error(ERR_ILLEGAL_OPERAND);
            right = nullnode();
    }

    if (isnotzero(left)) {
        left->type = EINTCONST;
        left->rtype = CParser_GetBoolType();
        CInt64_SetLong(&left->data.intval, 1);
        return left;
    }

    if (iszero(left)) {
        if (iszero(right)) {
            left->type = EINTCONST;
            left->rtype = CParser_GetBoolType();
            CInt64_SetLong(&left->data.intval, 0);
            return left;
        } else if (isnotzero(right)) {
            left->type = EINTCONST;
            left->rtype = CParser_GetBoolType();
            CInt64_SetLong(&left->data.intval, 1);
            return left;
        } else {
            left = makemonadicnode(right, ELOGNOT);
            left->rtype = CParser_GetBoolType();
            return makemonadicnode(left, ELOGNOT);
        }
    } else {
        if (isnotzero(right)) {
            right->type = EINTCONST;
            right->rtype = CParser_GetBoolType();
            CInt64_SetLong(&right->data.intval, 1);
        } else if (iszero(right)) {
            left = makemonadicnode(left, ELOGNOT);
            left->rtype = CParser_GetBoolType();
            return makemonadicnode(left, ELOGNOT);
        }
        left = makediadicnode(left, right, ELOR);
        left->rtype = CParser_GetBoolType();
        return CodeGen_CheckExpr(left);
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_NewDyadicNode(ENode* left, ENodeType nt, ENode* right) {
    switch (nt) {
        default:
            CError_FATAL(5649);
        case EADD:
            return CExpr_New_EADD_Node(left, right);
        case ESUB:
            return CExpr_New_ESUB_Node(left, right);
        case EMUL:
            return CExpr_New_EMUL_Node(left, right);
        case EDIV:
            return CExpr_New_EDIV_Node(left, right, TRUE);
        case EMODULO:
            return CExpr_New_EMODULO_Node(left, right, TRUE);
        case EAND:
            return CExpr_New_EAND_Node(left, right);
        case EXOR:
            return CExpr_New_EXOR_Node(left, right);
        case EOR:
            return CExpr_New_EOR_Node(left, right);
        case ESHL:
            return CExpr_New_ESHL_Node(left, right);
        case ESHR:
            return CExpr_New_ESHR_Node(left, right);
        case ELESS:
            return CExpr_New_ELESS_Node(left, right);
        case EGREATER:
            return CExpr_New_EGREATER_Node(left, right);
        case ELESSEQU:
            return CExpr_New_ELESSEQU_Node(left, right);
        case EGREATEREQU:
            return CExpr_New_EGREATEREQU_Node(left, right);
        case EEQU:
            return CExpr_New_EEQU_Node(left, right);
        case ENOTEQU:
            return CExpr_New_ENOTEQU_Node(left, right);
        case ELAND:
            return CExpr_New_ELAND_Node(left, right);
        case ELOR:
            return CExpr_New_ELOR_Node(left, right);
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

typedef struct DyadicInfo {
    ENodeType t;
    UInt8 prec;
} DyadicInfo;

static Boolean CExpr_GetDyadicInfo(short token, DyadicInfo* info) {
    switch (token) {
        case '*':
            info->t = EMUL;
            info->prec = 20;
            return TRUE;
        case '/':
            info->t = EDIV;
            info->prec = 20;
            return TRUE;
        case '%':
            info->t = EMODULO;
            info->prec = 20;
            return TRUE;
        case '+':
            info->t = EADD;
            info->prec = 19;
            return TRUE;
        case '-':
            info->t = ESUB;
            info->prec = 19;
            return TRUE;
        case TK_SHL:
            info->t = ESHL;
            info->prec = 18;
            return TRUE;
        case TK_SHR:
            info->t = ESHR;
            info->prec = 18;
            return TRUE;
        case '<':
            info->t = ELESS;
            info->prec = 17;
            return TRUE;
        case TK_LESS_EQUAL:
            info->t = ELESSEQU;
            info->prec = 17;
            return TRUE;
        case '>':
            if (disallowgreaterthan) {
                return FALSE;
            }
            info->t = EGREATER;
            info->prec = 17;
            return TRUE;
        case TK_GREATER_EQUAL:
            info->t = EGREATEREQU;
            info->prec = 17;
            return TRUE;
        case TK_LOGICAL_EQ:
            info->t = EEQU;
            info->prec = 16;
            return TRUE;
        case TK_LOGICAL_NE:
            info->t = ENOTEQU;
            info->prec = 16;
            return TRUE;
        case '&':
            info->t = EAND;
            info->prec = 15;
            return TRUE;
        case '^':
            info->t = EXOR;
            info->prec = 14;
            return TRUE;
        case '|':
            info->t = EOR;
            info->prec = 13;
            return TRUE;
        case TK_LOGICAL_AND:
            info->t = ELAND;
            info->prec = 12;
            return TRUE;
        case TK_LOGICAL_OR:
            info->t = ELOR;
            info->prec = 11;
            return TRUE;
        default:
            return FALSE;
    }
}

static ENode* CExpr_ParseDyadicExpression(ENode* left, UInt8 prec, Boolean no_warning) {
    ENode* right;
    Boolean is_eland_or_elor;
    Boolean save_dgt;
    Boolean cont;
    DyadicInfo left_info;
    DyadicInfo right_info;

    save_dgt = disallowgreaterthan;
    if (!left) {
        disallowgreaterthan = FALSE;
        left = pm_expression();
        disallowgreaterthan = save_dgt;
    }

    do {
        if (!CExpr_GetDyadicInfo(tk, &left_info)) {
            return left;
        }

        switch (left_info.t) {
            case ELAND:
                CExpr_CheckUnwantedAssignment(left);
                if (iszero(left)) {
                    no_warning = TRUE;
                }
                is_eland_or_elor = TRUE;
                break;
            case ELOR:
                CExpr_CheckUnwantedAssignment(left);
                if (isnotzero(left)) {
                    no_warning = TRUE;
                }
                is_eland_or_elor = TRUE;
                break;
            default:
                is_eland_or_elor = FALSE;
        }

        tk = lex();
        disallowgreaterthan = FALSE;
        right = pm_expression();
        disallowgreaterthan = save_dgt;
inner_loop:
        if (CExpr_GetDyadicInfo(tk, &right_info)) {
            if (left_info.prec >= right_info.prec) {
                cont = (right_info.prec <= prec);
            } else {
                right = CExpr_ParseDyadicExpression(right, left_info.prec, no_warning);
                goto inner_loop;
            }
        } else {
            cont = TRUE;
        }

        if (is_eland_or_elor) {
            CExpr_CheckUnwantedAssignment(right);
        }

        switch (left_info.t) {
            case EDIV:
                left = CExpr_New_EDIV_Node(left, right, no_warning);
                break;
            case EMODULO:
                left = CExpr_New_EMODULO_Node(left, right, no_warning);
                break;
            default:
                left = CExpr_NewDyadicNode(left, left_info.t, right);
        }
    } while (!cont);

    return left;
}

static Boolean CExpr_IsBlockMoveType(Type* type) {
    switch (type->type) {
        case TYPESTRUCT:
        case TYPECLASS:
            return TRUE;
        case TYPEMEMBERPOINTER:
            if (type->size != 4) {
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_New_ECOND_Node(ENode* cond, ENode* expr1, ENode* expr2) {
    ENode* result;
    ENodeList* args;
    short cost;
    Conversion conv;

    cond = CExpr_ConvertToCondition(pointer_generation(cond));
    expr1 = pointer_generation(expr1);
    expr2 = pointer_generation(expr2);

    if (!IS_TYPE_INT_OR_FLOAT(cond->rtype) && !IS_TYPE_POINTER_ONLY(cond->rtype)) {
        CError_Error(ERR_ILLEGAL_OPERAND);
        return nullnode();
    }

    cost = cond->cost + 1;
    if (expr1->cost > expr2->cost) {
        cost += expr1->cost;
    } else {
        cost += expr2->cost;
    }
    if (expr2->cost > cost) {
        cost = expr2->cost;
    }
    if (cost > 200) {
        cost = 200;
    }

    result = CExpr_NewENode(ECOND);
    result->cost = cost;
    result->rtype = expr1->rtype;
    result->flags = expr1->flags | expr2->flags;
    result->data.cond.cond = cond;
    if (ENODE_IS(expr1, EFUNCCALL) &&
        expr1->rtype == &stvoid &&
        (expr1->flags & ENODE_FLAG_VOLATILE) != 0) {
        result->rtype = expr2->rtype;
        result->flags = expr2->flags;
        result->data.cond.expr1 = expr1;
        result->data.cond.expr2 = expr2;
        return result;
    }
    if (ENODE_IS(expr2, EFUNCCALL) &&
        expr2->rtype == &stvoid &&
        (expr2->flags & ENODE_FLAG_VOLATILE) != 0) {
        result->rtype = expr1->rtype;
        result->flags = expr1->flags;
        result->data.cond.expr1 = expr1;
        result->data.cond.expr2 = expr2;
        return result;
    }

    if (ENODE_IS(expr1, EINDIRECT) &&
        ENODE_IS(expr2, EINDIRECT) &&
        is_typesame(expr1->rtype, expr2->rtype) &&
        ENODE_QUALS(expr1) == ENODE_QUALS(expr2) &&
        CExpr_IsBlockMoveType(expr1->rtype) &&
        !ENODE_IS(expr1->data.monadic, EBITFIELD) &&
        !ENODE_IS(expr2->data.monadic, EBITFIELD)) {
        if (isnotzero(cond)) {
            return expr1;
        }
        if (iszero(cond)) {
            return expr2;
        }
        result->data.cond.expr1 = getnodeaddress(expr1, FALSE);
        result->data.cond.expr2 = getnodeaddress(expr2, FALSE);
        result->rtype = result->data.cond.expr1->rtype;
        result = makemonadicnode(result, EINDIRECT);
        result->rtype = TPTR_TARGET(result->rtype);
        return result;
    }

    if ((IS_TYPE_CLASS(expr1->rtype) || IS_TYPE_CLASS(expr2->rtype)) &&
        !is_typesame(expr1->rtype, expr2->rtype)) {
        if (!copts.old_argmatch) {
            if (CExpr_CondOperatorMatch(expr1, expr2, &conv)) {
                CError_ASSERT(6253, !conv.expression);
                expr1 = conv.left;
                expr2 = conv.right;
            } else if (CExpr_CanImplicitlyConvert(expr1, expr2->rtype, ENODE_QUALS(expr2))) {
                if (CExpr_CanImplicitlyConvert(expr2, expr1->rtype, ENODE_QUALS(expr1))) {
                    CError_Error(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
                }
                expr1 = CExpr_Convert(expr1, expr2->rtype, ENODE_QUALS(expr2), FALSE, TRUE);
            } else if (CExpr_CanImplicitlyConvert(expr2, expr1->rtype, ENODE_QUALS(expr1))) {
                expr2 = CExpr_Convert(expr2, expr1->rtype, ENODE_QUALS(expr1), FALSE, TRUE);
            } else {
                goto failed;
            }

            result->rtype = expr1->rtype;
        } else {
            args = lalloc(sizeof(ENodeList));
            args->node = expr1;
            args->next = lalloc(sizeof(ENodeList));
            args->next->node = expr2;
            args->next->next = NULL;

            if (CExpr_CheckOperatorConversion(':', expr1, expr2, args, &conv)) {
                CError_ASSERT(6281, !conv.expression);
                expr1 = conv.left;
                expr2 = conv.right;
            }

            result->rtype = expr1->rtype;
        }
    }

    switch (expr1->rtype->type) {
        case TYPEENUM:
            if (expr1->rtype == expr2->rtype) {
                break;
            }
            expr1 = forceintegral(expr1);
        case TYPEINT:
            if (IS_TYPE_POINTER_ONLY(expr2->rtype) || IS_TYPE_MEMBERPOINTER(expr2->rtype)) {
                expr1 = CExpr_Convert(expr1, expr2->rtype, ENODE_QUALS(expr2), FALSE, TRUE);
                result->rtype = expr2->rtype;
                break;
            }
        case TYPEFLOAT:
            if (expr1->rtype != expr2->rtype) {
                CExpr_ArithmeticConversion(&expr1, &expr2);
                result->rtype = expr1->rtype;
            }
            break;
        case TYPEPOINTER:
            if (ENODE_IS(expr2, EINTCONST) && CInt64_IsZero(&expr2->data.intval)) {
                expr2->rtype = TYPE(&stunsignedlong);
                break;
            }
            if (IS_TYPE_POINTER_ONLY(expr2->rtype)) {
                if (IS_TYPE_CLASS(TPTR_TARGET(expr1->rtype)) &&
                    IS_TYPE_CLASS(TPTR_TARGET(expr2->rtype))) {
                    if (TPTR_TARGET(expr1->rtype) != TPTR_TARGET(expr2->rtype)) {
                        if (CClass_IsBaseClass(
                                TYPE_CLASS(TPTR_TARGET(expr1->rtype)),
                                TYPE_CLASS(TPTR_TARGET(expr2->rtype)),
                                NULL,
                                FALSE,
                                TRUE
                            )) {
                            expr1 = CExpr_SafeClassPointerCast(
                                expr1,
                                TYPE_CLASS(TPTR_TARGET(expr1->rtype)),
                                TYPE_CLASS(TPTR_TARGET(expr2->rtype)),
                                FALSE,
                                TRUE
                            );
                            expr1->rtype = expr2->rtype;
                        } else if (CClass_IsBaseClass(
                                       TYPE_CLASS(TPTR_TARGET(expr2->rtype)),
                                       TYPE_CLASS(TPTR_TARGET(expr1->rtype)),
                                       NULL,
                                       FALSE,
                                       TRUE
                                   )) {
                            expr2 = CExpr_SafeClassPointerCast(
                                expr2,
                                TYPE_CLASS(TPTR_TARGET(expr2->rtype)),
                                TYPE_CLASS(TPTR_TARGET(expr1->rtype)),
                                FALSE,
                                TRUE
                            );
                            expr2->rtype = expr1->rtype;
                        } else {
                            goto failed;
                        }
                    }
                    result->rtype = expr1->rtype;
                    break;
                }
                if (TPTR_TARGET(expr2->rtype) == &stvoid) {
                    result->rtype = expr2->rtype;
                }
            }
            if (!is_typeequal(expr1->rtype, expr2->rtype)) {
                if (!copts.objective_c) {
                    goto failed;
                }
                if (!CObjC_IsCompatibleType(expr1->rtype, expr2->rtype)) {
                    goto failed;
                }
                expr1->rtype = expr2->rtype = CObjC_GetObjCType_id(TRUE);
            }
            break;
        case TYPEVOID:
            if (!is_typeequal(expr1->rtype, expr2->rtype)) {
                goto failed;
            }
            break;
        case TYPESTRUCT:
        case TYPECLASS:
            if (!is_typeequal(expr1->rtype, expr2->rtype)) {
                goto failed;
            }
            result->rtype = expr1->rtype;
            break;
        case TYPEMEMBERPOINTER:
            if (IS_TYPE_MEMBERPOINTER(expr2->rtype) &&
                TYPE_MEMBER_POINTER(expr1->rtype)->ty2 == TYPE_MEMBER_POINTER(expr2->rtype)->ty2) {
                expr2 = CExpr_Convert(expr2, expr1->rtype, ENODE_QUALS(expr1), FALSE, TRUE);
            } else if (CExpr_CanImplicitlyConvert(expr1, expr2->rtype, ENODE_QUALS(expr2))) {
                if (CExpr_CanImplicitlyConvert(expr2, expr1->rtype, ENODE_QUALS(expr1))) {
                    CError_Error(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
                }
                expr1 = CExpr_Convert(expr1, expr2->rtype, ENODE_QUALS(expr2), FALSE, TRUE);
            } else if (CExpr_CanImplicitlyConvert(expr2, expr1->rtype, ENODE_QUALS(expr1))) {
                expr2 = CExpr_Convert(expr2, expr1->rtype, ENODE_QUALS(expr1), FALSE, TRUE);
            } else {
                goto failed;
            }
            result->rtype = expr1->rtype;
            break;
        default:
failed:
            CError_Error(
                ERR_TYPE_MISMATCH,
                expr1->rtype,
                ENODE_QUALS(expr1),
                expr2->rtype,
                ENODE_QUALS(expr2)
            );
            return nullnode();
    }

    result->data.cond.expr1 = expr1;
    result->data.cond.expr2 = expr2;
    if (isnotzero(cond)) {
        result = expr1;
    } else if (iszero(cond)) {
        result = expr2;
    }
    return CodeGen_CheckExpr(result);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static ENode* conditional_expression(void) {
    ENode* cond;
    ENode* expr1;
    ENode* expr2;
    ENode* result;
    Boolean is_templdep_cond;

    is_templdep_cond = FALSE;
    cond = CExpr_ParseDyadicExpression(NULL, 0, FALSE);
    if (tk != '?') {
        return cond;
    }

    cond = pointer_generation(cond);
    if (!IS_TYPE_TEMPLDEPEXPR(cond->rtype)) {
        cond = CExpr_ConvertToCondition(cond);
        if (!IS_TYPE_INT_OR_FLOAT(cond->rtype) && !IS_TYPE_POINTER_ONLY(cond->rtype)) {
            CError_Error(ERR_ILLEGAL_OPERAND);
            return nullnode();
        }
    } else {
        is_templdep_cond = TRUE;
    }

    tk = lex();
    expr1 = expression();
    if (tk != ':') {
        CError_ErrorSkip(ERR_EXPRESSION_SYNTAX_ERROR);
    } else {
        tk = lex();
    }

    expr2 =
        (copts.cplusplus && !copts.ARMconform) ? assignment_expression() : conditional_expression();

    if (is_templdep_cond ||
        IS_TYPE_TEMPLDEPEXPR(expr1->rtype) ||
        IS_TYPE_TEMPLDEPEXPR(expr2->rtype)) {
        result = CExpr_NewENode(ECOND);
        result->rtype = &sttemplexpr;
        result->data.cond.cond = cond;
        result->data.cond.expr1 = expr1;
        result->data.cond.expr2 = expr2;
        return result;
    }

    return CExpr_New_ECOND_Node(cond, expr1, expr2);
}

static ENode* CExpr_MakeOpAssNode(ENode* left, ENode* right, ENodeType nt) {
    if (left->rtype != right->rtype) {
        switch (right->rtype->type) {
            case TYPEINT:
            case TYPEFLOAT:
                break;
            case TYPEENUM:
                right->rtype = TYPE_ENUM(right->rtype)->enumtype;
                break;
            default:
                right = CExpr_AssignmentPromotion(right, left->rtype, 0, TRUE);
        }
        if (IS_TYPE_FLOAT(left->rtype)) {
            if (IS_TYPE_INT(right->rtype) ||
                (IS_TYPE_FLOAT(right->rtype) && left->rtype->size >= right->rtype->size)) {
                right = CExpr_AssignmentPromotion(right, left->rtype, 0, TRUE);
            }
        } else if (IS_TYPE_INT(left->rtype)) {
            if (IS_TYPE_INT(right->rtype) &&
                (left->rtype->size > right->rtype->size ||
                 (left->rtype->size == right->rtype->size &&
                  is_unsigned(left->rtype) == is_unsigned(right->rtype)))) {
                right = CExpr_AssignmentPromotion(right, left->rtype, 0, TRUE);
            }
        }
    }

    left = makediadicnode(left, right, nt);
    return CodeGen_CheckExpr(left);
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static ENode* makeassignmentnode(ENode* left, ENodeType nt, short token) {
    ENode* right;
    ENode* result;
    ENodeList* args;
    ENode* funcexpr;
    Conversion conv;
    ENode* expr;

    tk = lex();
    right = assignment_expression();
    if (copts.cplusplus) {
        if (copts.old_argmatch && !ENODE_IS(right, EMEMBER)) {
            right = pointer_generation(right);
        }
        if (CExpr_CheckOperator(token, left, right, &conv)) {
            if (!conv.expression) {
                if (nt == EASS) {
                    goto continue_anyway;
                }
                CError_FATAL(6538);
            }
            return conv.expression;
        }
        if (IS_TYPE_CLASS(left->rtype) && CClass_AssignmentOperator(TYPE_CLASS(left->rtype))) {
            CError_Error(ERR_ILLEGAL_OPERAND);
        }
    }
continue_anyway:
    if (IS_TYPE_ARRAY(left->rtype)) {
        if (copts.gcc_extensions && nt == EASS && is_typesame(left->rtype, right->rtype)) {
            result = makediadicnode(left, right, nt);
            result->flags = left->flags;
            return result;
        }
        CError_Error(ERR_ILLEGAL_OPERAND);
        return nullnode();
    }

    left = CExpr_LValue(pointer_generation(left), TRUE, TRUE);
    if (nt != EASS) {
        if (!IS_TYPE_INT(right->rtype)) {
            if (!IS_TYPE_ENUM(right->rtype)) {
                CError_Error(ERR_ILLEGAL_OPERAND);
                return left;
            }
            right = forceintegral(right);
        }
        if (!IS_TYPE_INT(left->rtype)) {
            if (copts.cplusplus) {
                CError_Error(ERR_ILLEGAL_OPERAND);
                return left;
            }
            left = forceintegral(left);
            if (!IS_TYPE_INT(left->rtype)) {
                CError_Error(ERR_ILLEGAL_OPERAND);
                return left;
            }
        }
        return CExpr_MakeOpAssNode(left, right, nt);
    }

    if (IS_TYPE_CLASS(left->rtype) && TYPE_CLASS(left->rtype)->sominfo) {
        CError_Error(ERR_ASSIGNMENT_NOT_SUPPORTED_SOM_CLASSES);
        return left;
    }

    if (copts.warn_implicitconv &&
        ENODE_IS(left, EINDIRECT) &&
        ENODE_IS(left->data.monadic, EBITFIELD) &&
        !ENODE_IS(right, EINTCONST)) {
        copts.warn_implicitconv = FALSE;
        right = CExpr_AssignmentPromotion(right, left->rtype, left->flags, TRUE);
        copts.warn_implicitconv = TRUE;
    } else {
        right = CExpr_AssignmentPromotion(right, left->rtype, left->flags, TRUE);
    }

    expr = right;
    if (IS_TYPE_FLOAT(right->rtype) &&
        ENODE_IS(right, ETYPCON) &&
        right->rtype->size == right->data.monadic->rtype->size) {
        expr = right->data.monadic;
    }

    if (ENODE_IS(left, EINDIRECT) &&
        ENODE_IS(left->data.monadic, EOBJREF) &&
        ENODE_IS(expr, EINDIRECT)) {
        funcexpr = right->data.monadic;
        if ((ENODE_IS(funcexpr, EFUNCCALL) || ENODE_IS(funcexpr, EFUNCCALLP)) &&
            left->rtype == funcexpr->data.funccall.functype->functype &&
            CMach_GetFunctionResultClass(funcexpr->data.funccall.functype) == 1 &&
            (args = funcexpr->data.funccall.args)) {
            switch (CABI_GetStructResultArgumentIndex(funcexpr->data.funccall.functype)) {
                case 0:
                    break;
                case 1:
                    if ((args = args->next)) {
                        break;
                    }
                    CError_FATAL(6632);
                default:
                    CError_FATAL(6633);
            }
            if (ENODE_IS(args->node, ETEMP)) {
                if (!(IS_TYPE_CLASS(left->rtype) && CClass_Destructor(TYPE_CLASS(left->rtype)))) {
                    args->node = getnodeaddress(left, FALSE);
                    return right;
                }
            }
        }
    }

    right = makediadicnode(left, right, nt);
    right->flags = left->flags;
    return CodeGen_CheckExpr(right);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static ENode* makepassignmentnode(ENode* left, ENodeType nt, short token) {
    ENode* right;
    Boolean is_array;
    Conversion conv;

    is_array = IS_TYPE_ARRAY(left->rtype);
    left = pointer_generation(left);
    if (copts.cplusplus) {
        tk = lex();
        right = pointer_generation(assignment_expression());
        if (CExpr_CheckOperator(token, left, right, &conv)) {
            CError_ASSERT(6676, conv.expression);
            return conv.expression;
        }
        left = CExpr_LValue(left, TRUE, TRUE);
    } else {
        left = CExpr_LValue(left, TRUE, TRUE);
        tk = lex();
        right = pointer_generation(assignment_expression());
    }

    if (is_array) {
        CError_Error(ERR_ILLEGAL_OPERAND);
    }

    switch (left->rtype->type) {
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEPOINTER:
            break;
        case TYPEENUM:
            if (copts.cplusplus) {
                CError_Error(ERR_ILLEGAL_OPERAND);
                return left;
            }
            left = forceintegral(left);
            break;
        default:
            CError_Error(ERR_ILLEGAL_OPERAND);
            return left;
    }

    if (IS_TYPE_ENUM(right->rtype)) {
        right = forceintegral(right);
    }

    if (iszero(right)) {
        return left;
    }

    if (IS_TYPE_POINTER_ONLY(left->rtype)) {
        if (IS_TYPE_INT(right->rtype)) {
            if (nt == ESUBASS) {
                left = psub(left, right);
                if (ENODE_IS(left, ESUB)) {
                    left->type = ESUBASS;
                }
            } else {
                left = padd(left, right);
                if (ENODE_IS(left, EADD)) {
                    left->type = EADDASS;
                }
            }
            return left;
        }
        CError_Error(ERR_ILLEGAL_OPERAND);
        return left;
    } else {
        return CExpr_MakeOpAssNode(left, right, nt);
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static ENode* makemulassignmentnode(ENode* left, ENodeType nt, short token) {
    ENode* right;
    Boolean is_array;
    Conversion conv;

    is_array = IS_TYPE_ARRAY(left->rtype);
    left = pointer_generation(left);
    if (copts.cplusplus) {
        tk = lex();
        right = pointer_generation(assignment_expression());
        if (CExpr_CheckOperator(token, left, right, &conv)) {
            CError_ASSERT(6760, conv.expression);
            return conv.expression;
        }
        if (!IS_TYPE_INT(left->rtype) && !(IS_TYPE_FLOAT(left->rtype) && nt != EMODASS)) {
            CError_Error(ERR_ILLEGAL_OPERAND);
            return nullnode();
        }
        left = CExpr_LValue(left, TRUE, TRUE);
    } else {
        if (IS_TYPE_ENUM(left->rtype)) {
            left = forceintegral(left);
        }
        if (!IS_TYPE_INT(left->rtype) && !(IS_TYPE_FLOAT(left->rtype) && nt != EMODASS)) {
            CError_Error(ERR_ILLEGAL_OPERAND);
            return nullnode();
        }

        left = CExpr_LValue(left, TRUE, TRUE);
        tk = lex();
        right = pointer_generation(assignment_expression());
    }

    if (is_array) {
        CError_Error(ERR_ILLEGAL_OPERAND);
    }

    if (IS_TYPE_ENUM(right->rtype)) {
        right = forceintegral(right);
    }

    if (IS_TYPE_INT(left->rtype) && IS_TYPE_FLOAT(right->rtype) && nt == EMODASS) {
        CError_Error(ERR_ILLEGAL_OPERAND);
        return nullnode();
    }

    return CExpr_MakeOpAssNode(left, right, nt);
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static ENode* CExpr_TransformOpAssign(ENode* expr) {
    switch (expr->type) {
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
            if (expr->rtype == TYPE(&stbool)) {
                expr = CIRTrans_TransformOpAss(expr);
                if (ENODE_IS(expr, EASS)) {
                    expr->data.diadic.right = makemonadicnode(expr->data.diadic.right, ELOGNOT);
                    expr->data.diadic.right->rtype = TYPE(&stbool);
                    expr->data.diadic.right = makemonadicnode(expr->data.diadic.right, ELOGNOT);
                }
            }
        default:
            break;
    }

    return expr;
}

ENode* assignment_expression(void) {
    ENode* expr;

    if (tk == TK_THROW) {
        return CExcept_ScanThrowExpression();
    }

    expr = conditional_expression();
    switch (tk) {
        case '=':
            return makeassignmentnode(expr, EASS, tk);
        case TK_ADD_ASSIGN:
            return CExpr_TransformOpAssign(makepassignmentnode(expr, EADDASS, tk));
        case TK_SUB_ASSIGN:
            return CExpr_TransformOpAssign(makepassignmentnode(expr, ESUBASS, tk));
        case TK_MULT_ASSIGN:
            expr = makemulassignmentnode(expr, EMULASS, tk);
            if (ENODE_IS(expr, EMULASS) && CExpr_IsOne(expr->data.diadic.right)) {
                return expr->data.diadic.left;
            }
            return CExpr_TransformOpAssign(expr);
        case TK_DIV_ASSIGN:
            expr = makemulassignmentnode(expr, EDIVASS, tk);
            if (ENODE_IS(expr, EDIVASS)) {
                if (iszero(expr->data.diadic.right) && !IS_TYPE_FLOAT(expr->rtype)) {
                    CError_Warning(ERR_DIVISION_BY_0);
                    return expr->data.diadic.left;
                }
                if (CExpr_IsOne(expr->data.diadic.right)) {
                    return expr->data.diadic.left;
                }
            }
            return CExpr_TransformOpAssign(expr);
        case TK_MOD_ASSIGN:
            expr = makemulassignmentnode(expr, EMODASS, tk);
            if (ENODE_IS(expr, EMODASS)) {
                if (iszero(expr->data.diadic.right)) {
                    CError_Warning(ERR_DIVISION_BY_0);
                    return expr->data.diadic.left;
                }
            }
            return CExpr_TransformOpAssign(expr);
        case TK_SHL_ASSIGN:
            expr = makeassignmentnode(expr, ESHLASS, tk);
            if (ENODE_IS(expr, ESHLASS) && iszero(expr->data.diadic.right)) {
                return expr->data.diadic.left;
            }
            return CExpr_TransformOpAssign(expr);
        case TK_SHR_ASSIGN:
            expr = makeassignmentnode(expr, ESHRASS, tk);
            if (ENODE_IS(expr, ESHRASS) && iszero(expr->data.diadic.right)) {
                return expr->data.diadic.left;
            }
            return CExpr_TransformOpAssign(expr);
        case TK_AND_ASSIGN:
            expr = makeassignmentnode(expr, EANDASS, tk);
            if (ENODE_IS(expr, EANDASS) && CExpr_AllBitsSet(expr->data.diadic.right)) {
                return expr->data.diadic.left;
            }
            return expr;
        case TK_XOR_ASSIGN:
            expr = makeassignmentnode(expr, EXORASS, tk);
            if (ENODE_IS(expr, EXORASS) && iszero(expr->data.diadic.right)) {
                return expr->data.diadic.left;
            }
            return CExpr_TransformOpAssign(expr);
        case TK_OR_ASSIGN:
            expr = makeassignmentnode(expr, EORASS, tk);
            if (ENODE_IS(expr, EORASS) && iszero(expr->data.diadic.right)) {
                return expr->data.diadic.left;
            }
            return CExpr_TransformOpAssign(expr);
        default:
            return expr;
    }
}

ENode* conv_assignment_expression(void) {
    return pointer_generation(assignment_expression());
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static Boolean CExpr_HasSideEffect(ENode* expr) {
    switch (expr->type) {
        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
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
        case EROTL:
        case EROTR:
        case EBITFIELD:
        case EINTCONST:
        case EFLOATCONST:
        case ESTRINGCONST:
        case EOBJREF:
        case ETEMP:
        case EARGOBJ:
        case ELOCOBJ:
        case EOBJLIST:
        case EMEMBER:
        case EVECTOR128CONST:
            return FALSE;
        case ETYPCON:
            return IS_TYPE_VOID(expr->rtype);
        case EINDIRECT:
            switch (expr->data.monadic->type) {
                case EFUNCCALL:
                case EFUNCCALLP:
                    return TRUE;
                default:
                    return FALSE;
            }
        case ECOMMA:
            return CInline_ExpressionHasSideEffect(expr->data.diadic.right);
        case ECOND:
            return CInline_ExpressionHasSideEffect(expr->data.cond.expr1) ||
                CInline_ExpressionHasSideEffect(expr->data.cond.expr2);
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
        case ELABEL:
        case ENEWEXCEPTION:
        case ENEWEXCEPTIONARRAY:
        case EINITTRYCATCH:
        case EINSTRUCTION:
            return TRUE;
        case EMULV:
        case EADDV:
        case ESUBV:
        case EPMODULO:
        case EBCLR:
        case EBTST:
        case EBSET:
        case ESETCONST:
        case ETEMPLDEP:
        case EDEFINE:
        case EREUSE:
        case EASSBLK:
        default:
            CError_FATAL(7063);
            return FALSE;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
void CExpr_CheckUnusedExpression(ENode* expr) {
    ENode* scan;
    ENodeList* arg;

    if (copts.warn_possunwant) {
        scan = expr;
        while (ENODE_IS(scan, ETYPCON)) {
            scan = scan->data.monadic;
        }
        if (ENODE_IS(scan, EEQU)) {
            CError_Warning(ERR_POSSIBLE_UNWANTED_COMPARE);
            return;
        }
    }

    if (copts.warn_no_side_effect) {
        if (!CExpr_HasSideEffect(expr)) {
            CError_Warning(ERR_EXPRESSION_NO_SIDE_EFFECT);
            return;
        }
    }

    if (copts.warn_resultnotused) {
        scan = expr;
        if (!IS_TYPE_VOID(expr->rtype)) {
            if (ENODE_IS(expr, EINDIRECT)) {
                scan = expr->data.monadic;
            }

            switch (scan->type) {
                case EFUNCCALL:
                case EFUNCCALLP:
                    if (ENODE_IS(scan->data.funccall.funcref, EOBJREF) &&
                        scan->data.funccall.funcref->data.objref->name == asop_name_node) {
                        break;
                    }
                    if (CMach_GetFunctionResultClass(scan->data.funccall.functype) == 1 &&
                        (arg = scan->data.funccall.args)) {
                        switch (CABI_GetStructResultArgumentIndex(scan->data.funccall.functype)) {
                            case 0:
                                break;
                            case 1:
                                if ((arg = arg->next)) {
                                    break;
                                }
                                CError_FATAL(7117);
                            default:
                                CError_FATAL(7118);
                        }

                        if (!ENODE_IS(arg->node, ETEMP)) {
                            break;
                        }
                    }
                    CError_Warning(ERR_RESULT_FUNCTION_CALL_NOT_USED);
                default:
                    break;
            }
        }
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* s_expression(void) {
    ENode* left;
    ENode* right;
    Conversion conv;

    left = assignment_expression();
    while (tk == ',') {
        left = pointer_generation(left);
        tk = lex();
        right = pointer_generation(assignment_expression());

        if (copts.cplusplus && CExpr_CheckOperator(',', left, right, &conv)) {
            CError_ASSERT(7150, left = conv.expression);
        } else {
            CExpr_CheckUnusedExpression(left);
            left = makecommaexpression(left, right);
            left->rtype = right->rtype;
        }
    }

    return left;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

ENode* expression(void) {
    return pointer_generation(s_expression());
}

CInt64 CExpr_IntegralConstExprType(Type** tint) {
    ENode* expr;

    expr = pointer_generation(conditional_expression());
    if (ENODE_IS(expr, EINTCONST)) {
        switch (expr->rtype->type) {
            case TYPEINT:
                *tint = expr->rtype;
                return expr->data.intval;
            case TYPEENUM:
                *tint = TYPE_ENUM(expr->rtype)->enumtype;
                return expr->data.intval;
            default:
                break;
        }
    }

    CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
    *tint = TYPE(&stchar);
    return cint64_zero;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
ENode* CExpr_IntegralConstOrDepExpr(void) {
    ENode* expr;

    expr = pointer_generation(conditional_expression());
    if (ENODE_IS(expr, EINTCONST)) {
        switch (expr->rtype->type) {
            case TYPEINT:
                return expr;
            case TYPEENUM:
                expr->rtype = TYPE_ENUM(expr->rtype)->enumtype;
                return expr;
            default:
                CError_FATAL(7216);
        }
    }

    if (CTemplTool_IsTemplateArgumentDependentExpression(expr)) {
        return expr;
    }

    CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
    expr = nullnode();
    expr->rtype = TYPE(&stchar);
    return expr;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

CInt64 CExpr_IntegralConstExpr(void) {
    Type* throwaway;
    return CExpr_IntegralConstExprType(&throwaway);
}

void CExpr_CheckUnwantedAssignment(ENode* expr) {
    if (copts.warn_possunwant) {
        if (ENODE_IS(expr, EASS) && !(expr->flags & ENODE_FLAG_PARENS)) {
            CError_Warning(ERR_POSSIBLE_UNWANTED_ASSIGNMENT);
        }
    }
}

Boolean CExpr_ParseAsmExpr(Object** objptr, CInt64* valptr) {
    ENode* expr;

    if (objptr) {
        *objptr = NULL;
    }
    *valptr = cint64_zero;

    expr = pointer_generation(assignment_expression());
    if (ENODE_IS(expr, EINTCONST)) {
        *valptr = expr->data.intval;
        return TRUE;
    }

    if (objptr) {
        switch (expr->type) {
            case EINDIRECT:
                if (CInit_RelocInitCheck(expr->data.monadic, objptr, valptr, TRUE)) {
                    return TRUE;
                }
                break;
            case EOBJREF:
                *objptr = expr->data.objref;
                while ((*objptr)->datatype == DALIAS) {
                    *objptr = (*objptr)->u.alias.object;
                }
                return TRUE;
            case EMEMBER:
                if (expr->data.emember->list->object->otype == OT_OBJECT) {
                    if (expr->data.emember->list->next &&
                        expr->data.emember->list->next->object->otype == OT_OBJECT) {
                        CError_Error(ERR_AMBIGUOUS_ACCESS_OVERLOADED_FUNCTION);
                    }
                    *objptr = OBJECT(expr->data.emember->list->object);
                    while ((*objptr)->datatype == DALIAS) {
                        *objptr = (*objptr)->u.alias.object;
                    }
                    return TRUE;
                }
                break;
            case EOBJLIST:
                CError_Error(ERR_AMBIGUOUS_ACCESS_OVERLOADED_FUNCTION);
                return FALSE;
            default:
                break;
        }
    }

    CError_Error(ERR_ILLEGAL_ADDRESSING_MODE);
    return FALSE;
}
