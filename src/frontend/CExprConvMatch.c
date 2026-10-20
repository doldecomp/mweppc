#include "compiler/CABI.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CMangler.h"
#include "compiler/CObjC.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CScope.h"
#include "compiler/CTemplateFunc.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/templates.h"
#include "compiler/tokens.h"
#include "compiler/types.h"

#pragma pack(push, 2)
typedef struct StandardConv {
    Type* type1;
    Type* type2;
    UInt32 qual1;
    UInt32 qual2;
    Boolean lvalue_transform;
    Boolean qual_adjust;
    Boolean promotion;
    Boolean conversion;
    Boolean ref_binding;
    Boolean nonconst_ref_result;
} StandardConv;

typedef enum EImplicitConvType {
    ICT_NONE,
    ICT_ELLIPSIS,
    ICT_USER,
    ICT_STANDARD
} EImplicitConvType;

typedef struct ImplicitConv {
    EImplicitConvType type;
    union {
        struct {
            Object* func;
            StandardConv standardConv;
        } user;
        struct {
            StandardConv standardConv;
        } standard;
    } u;
} ImplicitConv;

typedef struct ConversionTypeList {
    struct ConversionTypeList* next;
    Object* func;
    Type* type;
    UInt32 qual;
} ConversionTypeList;

typedef struct Match {
    struct Match* next;
    Object* object;
    Object* specialfunc;
    Type* type;
    UInt32 qual;
    Type* type2;
    UInt32 qual2;
    ImplicitConv conv[3];
} Match;
#pragma pack(pop)

static inline ENode* CExpr_DerivedToBase(
    ENode* expr,
    Type* type2,
    UInt32 qual2,
    Boolean checkAccess,
    Boolean nullcheckflag,
    Boolean pathcheckflag
);

static Type* CExpr_GetImplictObjectParamType(Object* object, UInt32* qual) {
    Type* type;

    CError_ASSERT(98, IS_TYPE_FUNC(object->type));
    CError_ASSERT(99, TYPE_FUNC(object->type)->flags & FUNC_METHOD);
    CError_ASSERT(100, !TYPE_METHOD(object->type)->is_static);
    CError_ASSERT(101, TYPE_METHOD(object->type)->args);

    type = CDecl_NewRefPointerType(TYPE(TYPE_METHOD(object->type)->theclass));
    *qual = TYPE_METHOD(object->type)->args->qual & Q_CV;
    return type;
}

static Type* CExpr_GetParamType(Object* object, int index, UInt32* qual) {
    FuncArg* arg;

    CError_ASSERT(120, IS_TYPE_FUNC(object->type));
    CError_ASSERT(121, arg = TYPE_FUNC(object->type)->args);
    if (IS_TYPEFUNC_NONSTATIC_METHOD(TYPE_FUNC(object->type))) {
        CError_ASSERT(125, arg = arg->next);
    }

    while (index > 0) {
        CError_ASSERT(129, arg = arg->next);
        index--;
    }

    *qual = arg->qual & Q_CV;
    return arg->type;
}

static Boolean CExpr_HasNParams(Object* object, int count) {
    FuncArg* arg;
    int i;

    CError_ASSERT(146, IS_TYPE_FUNC(object->type));
    CError_ASSERT(147, arg = TYPE_FUNC(object->type)->args);
    if (IS_TYPEFUNC_NONSTATIC_METHOD(TYPE_FUNC(object->type))) {
        arg = arg->next;
    }

    i = 0;
    while (arg) {
        arg = arg->next;
        i++;
    }

    return i == count;
}

typedef enum TypeCompareMode {
    TCM_IGNORE_CV,
    TCM_POINTER_CV,
    TCM_SAME_CV
} TypeCompareMode;

static Boolean CExpr_TypeCompare(
    Type* typeA, UInt32 qualA, Type* typeB, UInt32 qualB, TypeCompareMode mode
) {
    if (typeA->type != typeB->type) {
        return FALSE;
    }

    switch (mode) {
        case TCM_IGNORE_CV:
            while (1) {
                switch (typeA->type) {
                    case TYPEPOINTER:
                        typeA = TPTR_TARGET(typeA);
                        typeB = TPTR_TARGET(typeB);
                        if (typeA->type != typeB->type) {
                            return FALSE;
                        }
                        continue;

                    case TYPEMEMBERPOINTER:
                        if (!is_typesame(
                                TYPE_MEMBER_POINTER(typeA)->ty2, TYPE_MEMBER_POINTER(typeB)->ty2
                            )) {
                            return FALSE;
                        }
                        typeA = TYPE_MEMBER_POINTER(typeA)->ty1;
                        typeB = TYPE_MEMBER_POINTER(typeB)->ty1;
                        if (typeA->type != typeB->type) {
                            return FALSE;
                        }
                        continue;
                    default:
                        break;
                }
                break;
            }
            break;

        case TCM_POINTER_CV:
            switch (typeA->type) {
                case TYPEPOINTER:
                    if ((qualA & Q_CV) != (qualB & Q_CV)) {
                        return FALSE;
                    }

                    typeA = TPTR_TARGET(typeA);
                    typeB = TPTR_TARGET(typeB);
                    break;

                case TYPEMEMBERPOINTER:
                    if ((qualA & Q_CV) != (qualB & Q_CV)) {
                        return FALSE;
                    }

                    if (!is_typesame(
                            TYPE_MEMBER_POINTER(typeA)->ty2, TYPE_MEMBER_POINTER(typeB)->ty2
                        )) {
                        return FALSE;
                    }
                    typeA = TYPE_MEMBER_POINTER(typeA)->ty1;
                    typeB = TYPE_MEMBER_POINTER(typeB)->ty1;
                    break;
                default:
                    break;
            }
            break;

        case TCM_SAME_CV:
            if ((qualA & Q_CV) != (qualB & Q_CV)) {
                return FALSE;
            }
            break;
    }

    return is_typesame(typeA, typeB);
}

static int CExpr_IsReferenceCompatible(Type* typeA, UInt32 qualA, Type* typeB, UInt32 qualB) {
    if (CParser_IsSameOrMoreCVQualified(
            CParser_GetCVTypeQualifiers(typeA, qualA), CParser_GetCVTypeQualifiers(typeB, qualB)
        )) {
        if (CExpr_TypeCompare(typeA, qualA, typeB, qualB, TCM_POINTER_CV)) {
            return 1;
        }

        if (IS_TYPE_CLASS(typeB) && IS_TYPE_CLASS(typeA)) {
            short depth;
            Boolean isambigbase;
            if (CClass_GetBasePath(TYPE_CLASS(typeB), TYPE_CLASS(typeA), &depth, &isambigbase)) {
                return 2;
            }
        }
    }

    return 0;
}

static Boolean CExpr_IsBaseOf(TypeClass* baseclass, TypeClass* superclass) {
    ClassList* base;

    for (base = superclass->bases; base; base = base->next) {
        if (base->base == baseclass || CExpr_IsBaseOf(baseclass, base->base)) {
            return TRUE;
        }
    }

    return FALSE;
}

static Boolean CExpr_IsBetterClassConversion(
    TypeClass* a, TypeClass* b, TypeClass* c, TypeClass* d
) {
    if (a == c) {
        return CExpr_IsBaseOf(d, b);
    }
    if (b == d) {
        return CExpr_IsBaseOf(a, c);
    }
    return FALSE;
}

static inline Boolean CExpr_IsPointerToBoolConv(Type* a, Type* b) {
    return (a == TYPE(&stbool)) && (IS_TYPE_POINTER_ONLY(b) || IS_TYPE_MEMBERPOINTER(b));
}

static Boolean CExpr_IsBetterStandardConv(StandardConv* a, StandardConv* b) {
    Boolean a_subset;
    Boolean b_has_more;

    a_subset = TRUE;
    b_has_more = FALSE;

    if (b->qual_adjust) {
        if (!a->qual_adjust) {
            b_has_more = TRUE;
        }
    } else {
        if (a->qual_adjust) {
            a_subset = FALSE;
        }
    }

    if (b->promotion) {
        if (!a->promotion) {
            b_has_more = TRUE;
        }
    } else {
        if (a->promotion) {
            a_subset = FALSE;
        }
    }

    if (b->conversion) {
        if (a->conversion) {
            if (CExpr_IsPointerToBoolConv(b->type2, b->type1)) {
                if (!CExpr_IsPointerToBoolConv(a->type2, a->type1)) {
                    return TRUE;
                }
            } else {
                if (CExpr_IsPointerToBoolConv(a->type2, a->type1)) {
                    return FALSE;
                }
            }
        } else {
            b_has_more = TRUE;
        }
    } else {
        if (a->conversion) {
            a_subset = FALSE;
        }
    }

    if (a_subset && b_has_more) {
        return TRUE;
    }

    if (!a->conversion) {
        if (b->conversion) {
            return TRUE;
        }

        if (a->promotion) {
            if (!b->promotion) {
                return FALSE;
            }
        } else {
            if (b->promotion) {
                return TRUE;
            }
        }
    } else {
        if (!b->conversion) {
            return FALSE;
        }
    }

    if (IS_TYPE_POINTER_ONLY(a->type1) &&
        IS_TYPE_CLASS(TPTR_TARGET(a->type1)) &&
        IS_TYPE_POINTER_ONLY(a->type2) &&
        IS_TYPE_POINTER_ONLY(b->type1) &&
        IS_TYPE_CLASS(TPTR_TARGET(b->type1)) &&
        IS_TYPE_POINTER_ONLY(b->type2)) {
        if (TPTR_TARGET(b->type2) == &stvoid) {
            if (TPTR_TARGET(a->type2) == &stvoid) {
                if (CExpr_IsBaseOf(
                        TYPE_CLASS(TPTR_TARGET(a->type1)), TYPE_CLASS(TPTR_TARGET(b->type1))
                    )) {
                    return TRUE;
                }
            } else {
                if (TPTR_TARGET(a->type1) == TPTR_TARGET(b->type1) &&
                    IS_TYPE_CLASS(TPTR_TARGET(a->type2))) {
                    return TRUE;
                }
            }
        } else if (IS_TYPE_CLASS(TPTR_TARGET(a->type2)) && IS_TYPE_CLASS(TPTR_TARGET(b->type2))) {
            if (CExpr_IsBetterClassConversion(
                    TYPE_CLASS(TPTR_TARGET(a->type1)),
                    TYPE_CLASS(TPTR_TARGET(a->type2)),
                    TYPE_CLASS(TPTR_TARGET(b->type1)),
                    TYPE_CLASS(TPTR_TARGET(b->type2))
                )) {
                return TRUE;
            }
        }
    }

    if (IS_TYPE_CLASS(a->type1) &&
        IS_TYPE_CLASS(a->type2) &&
        IS_TYPE_CLASS(b->type1) &&
        IS_TYPE_CLASS(b->type2) &&
        CExpr_IsBetterClassConversion(
            TYPE_CLASS(a->type1), TYPE_CLASS(a->type2), TYPE_CLASS(b->type1), TYPE_CLASS(b->type2)
        )) {
        return TRUE;
    }

    if (IS_TYPE_MEMBERPOINTER(a->type1) &&
        IS_TYPE_MEMBERPOINTER(a->type2) &&
        IS_TYPE_MEMBERPOINTER(b->type1) &&
        IS_TYPE_MEMBERPOINTER(b->type2) &&
        IS_TYPE_CLASS(TYPE_MEMBER_POINTER(a->type1)->ty2) &&
        IS_TYPE_CLASS(TYPE_MEMBER_POINTER(a->type2)->ty2) &&
        IS_TYPE_CLASS(TYPE_MEMBER_POINTER(b->type1)->ty2) &&
        IS_TYPE_CLASS(TYPE_MEMBER_POINTER(b->type2)->ty2) &&
        CExpr_IsBetterClassConversion(
            TYPE_CLASS(TYPE_MEMBER_POINTER(b->type1)->ty2),
            TYPE_CLASS(TYPE_MEMBER_POINTER(b->type2)->ty2),
            TYPE_CLASS(TYPE_MEMBER_POINTER(a->type1)->ty2),
            TYPE_CLASS(TYPE_MEMBER_POINTER(a->type2)->ty2)
        )) {
        return TRUE;
    }

    if (a->ref_binding &&
        b->ref_binding &&
        CExpr_TypeCompare(a->type2, a->qual2, b->type2, b->qual2, TCM_POINTER_CV) &&
        CParser_IsMoreCVQualified(
            CParser_GetTypeQualifiers(b->type2, b->qual2),
            CParser_GetTypeQualifiers(a->type2, a->qual2)
        )) {
        return TRUE;
    }

    return FALSE;
}

static Boolean CExpr_IsBetterImplicitConv(ImplicitConv* a, ImplicitConv* b) {
    if (a->type > b->type) {
        return TRUE;
    }
    if (a->type != b->type) {
        return FALSE;
    }

    if (a->type == ICT_STANDARD) {
        return CExpr_IsBetterStandardConv(&a->u.standard.standardConv, &b->u.standard.standardConv);
    }

    if (a->type == ICT_USER &&
        a->u.user.func == b->u.user.func &&
        CExpr_IsBetterStandardConv(&a->u.user.standardConv, &b->u.user.standardConv)) {
        return TRUE;
    }

    return FALSE;
}

typedef enum SSCRMode {
    SSCR_EXACT,
    SSCR_PROMOTION,
    SSCR_CONVERSION
} SSCRMode;

static Boolean CExpr_SetupStandardConversionResult(
    ENode* expr,
    Type* type2,
    UInt32 qual2,
    SSCRMode mode,
    Boolean is_ref_binding,
    Boolean refFlag,
    StandardConv* result
) {
    UInt32 cv1;
    UInt32 cv2;

    if (is_ref_binding) {
        if (!CParser_IsConst(type2, qual2)) {
            if (!refFlag && !CExpr_IsLValue(expr)) {
                return FALSE;
            }
            if (mode != SSCR_EXACT && !IS_TYPE_CLASS(type2)) {
                return FALSE;
            }
        }

        cv2 = CParser_GetTypeQualifiers(type2, qual2) & Q_CV;
        cv1 = CParser_GetTypeQualifiers(expr->rtype, ENODE_QUALS(expr)) & Q_CV;
        if (cv2 != cv1 && !CParser_IsMoreCVQualified(cv2, cv1)) {
            return FALSE;
        }
    }

    memclrw(result, sizeof(StandardConv));
    result->type2 = type2;
    result->qual2 = qual2;
    result->type1 = expr->rtype;
    result->qual1 = ENODE_QUALS(expr);
    result->ref_binding = is_ref_binding;

    switch (mode) {
        case SSCR_EXACT:
            break;
        case SSCR_PROMOTION:
            result->promotion = TRUE;
            break;
        case SSCR_CONVERSION:
            result->conversion = TRUE;
            break;
        default:
            CError_FATAL(581);
    }

    return TRUE;
}

typedef enum QualDiff {
    QD_SAME,
    QD_MORE,
    QD_LESS = 255
} QualDiff;

static inline QualDiff CExpr_CompareQualifiers(UInt32 qual1, UInt32 qual2) {
    if ((qual1 & Q_CV) == (qual2 & Q_CV)) {
        return QD_SAME;
    }

    if (((qual2 & Q_CONST) && !(qual1 & Q_CONST)) ||
        ((qual2 & Q_VOLATILE) && !(qual1 & Q_VOLATILE))) {
        return QD_LESS;
    }

    return QD_MORE;
}

static Boolean CExpr_SetQualConversionResult(
    Type* type1, UInt32 qual1, Type* type2, UInt32 qual2, StandardConv* result
) {
    Boolean allConst = TRUE;
    UInt32 cv1;
    UInt32 cv2;

    while (1) {
        cv1 = CParser_GetCVTypeQualifiers(type1, qual1);
        cv2 = CParser_GetCVTypeQualifiers(type2, qual2);

        switch (CExpr_CompareQualifiers(cv1, cv2)) {
            case QD_SAME:
                break;

            case QD_MORE:
                result->qual_adjust = TRUE;
                if (!allConst) {
                    return FALSE;
                }
                break;

            default:
                return FALSE;
        }

        if (!(cv1 & Q_CONST)) {
            allConst = FALSE;
        }

        if (IS_TYPE_POINTER_ONLY(type1)) {
            CError_ASSERT(635, IS_TYPE_POINTER_ONLY(type2));
            type1 = TPTR_TARGET(type1);
            type2 = TPTR_TARGET(type2);
        } else {
            if (!IS_TYPE_MEMBERPOINTER(type1)) {
                return TRUE;
            }

            CError_ASSERT(642, IS_TYPE_MEMBERPOINTER(type2));
            type1 = TYPE_MEMBER_POINTER(type1)->ty1;
            type2 = TYPE_MEMBER_POINTER(type2)->ty1;
        }
    }
}

static Boolean CExpr_OverloadFuncMatch(
    NameSpaceObjectList* list, TemplArg* templargs, Type* type, ENode** outExpr
) {
    Object* object;
    ENode* expr;
    FuncArg* arg;
    int i;
    ObjectList* objlist;
    Object* found;
    TemplFuncInstance* inst;
    ObjectList* ambiguous;
    ObjectList* templates;
    Boolean found_nontemplate;
    TypeFunc* functype;

    if (!IS_TYPE_POINTER_ONLY(type) || !IS_TYPE_FUNC(functype = TYPE_FUNC(TPTR_TARGET(type)))) {
        return FALSE;
    }

    found = NULL;
    ambiguous = NULL;
    templates = NULL;
    type = TYPE(functype);
    found_nontemplate = FALSE;

    while (list) {
        object = OBJECT(list->object);
        if (object->otype == OT_OBJECT) {
            if (IS_TEMPL_FUNC(object->type)) {
                if (!found_nontemplate &&
                    CTempl_CanDeduceFunc(object, TYPE_FUNC(type), templargs)) {
                    CError_ASSERT(
                        685,
                        inst = CTempl_DeduceFunc(object, TYPE_FUNC(type), templargs, NULL, FALSE)
                    );
                    if (is_typesame(inst->object->type, type)) {
                        objlist = lalloc(sizeof(ObjectList));
                        objlist->next = templates;
                        objlist->object = object;
                        templates = objlist;

                        if (found && found != inst->object) {
                            objlist = lalloc(sizeof(ObjectList));
                            objlist->next = ambiguous;
                            objlist->object = inst->object;
                            ambiguous = objlist;
                        } else {
                            found = inst->object;
                        }
                    }
                }
            } else if (is_typesame(object->type, type)) {
                if (found && found_nontemplate) {
                    Object *checkA, *checkB;
                    checkA = object;
                    if (checkA->datatype == DALIAS) {
                        checkA = checkA->u.alias.object;
                    }
                    checkB = found;
                    if (checkB->datatype == DALIAS) {
                        checkB = checkB->u.alias.object;
                    }
                    if (checkA != checkB) {
                        objlist = lalloc(sizeof(ObjectList));
                        objlist->next = ambiguous;
                        objlist->object = object;
                        ambiguous = objlist;
                    }
                } else {
                    ambiguous = NULL;
                    found = object;
                }
                found_nontemplate = TRUE;
            }
        }
        list = list->next;
    }

    if (found) {
        if (outExpr) {
            if (ambiguous) {
                i = 0;
                for (arg = TYPE_FUNC(object->type)->args; arg; arg = arg->next) {
                    i++;
                }

                if (!found_nontemplate &&
                    (object = CTempl_PartialOrdering(templates->object, templates->next, i))) {
                    CError_ASSERT(
                        741, inst = CTempl_DeduceFunc(object, functype, templargs, NULL, FALSE)
                    );
                    found = inst->object;
                } else {
                    CError_OverloadedFunctionError(found, ambiguous);
                }
            }

            expr = CExpr_MakeObjRefNode(found, TRUE);
            *outExpr = expr;
            expr->rtype = CDecl_NewPointerType(found->type);
            expr->flags = object->qual & ENODE_FLAG_QUALS;
            found->flags |= OBJECT_USED;
            if (found->datatype == DINLINEFUNC) {
                CError_Error(ERR_ILLEGAL_USE_INLINE_FUNCTION);
            }
        }

        return TRUE;
    }

    return FALSE;
}

static Boolean CExpr_StandardConversionMatch(
    ENode* expr, Type* type2, UInt32 qual2, Boolean implicit_object, StandardConv* result
) {
    Type* type1;
    UInt32 qual1;
    Boolean refFlag;
    Type* inner2;
    Type* inner1;
    SSCRMode mode;
    NameSpaceObjectList list;

    if (IS_TYPE_REFERENCE(type2)) {
        type2 = TPTR_TARGET(type2);
        if (IS_TYPE_POINTER_ONLY(type2)) {
            expr = pointer_generation(expr);
        }
        refFlag = TRUE;
    } else {
        if ((IS_TYPE_ARRAY(expr->rtype) && !IS_TYPE_ARRAY(type2)) ||
            (IS_TYPE_FUNC(expr->rtype) && !IS_TYPE_FUNC(type2))) {
            expr = pointer_generation(expr);
        }
        refFlag = FALSE;
    }

    type1 = expr->rtype;
    qual1 = ENODE_QUALS(expr);

    if (IS_TYPE_POINTER_ONLY(type2)) {
        if (ENODE_IS(expr, EINTCONST) && CInt64_IsZero(&expr->data.intval)) {
            if (IS_TYPE_INT(type1) || (!copts.cplusplus && IS_TYPE_ENUM(type1))) {
                return CExpr_SetupStandardConversionResult(
                    expr, type2, qual2, SSCR_CONVERSION, implicit_object, refFlag, result
                );
            }
        }

        if (IS_TYPE_INT(expr->rtype) &&
            ENODE_IS_INDIRECT_TO(expr, EOBJREF) &&
            (expr->data.monadic->data.objref->qual & Q_INLINE_DATA) &&
            CInt64_IsZero(&expr->data.monadic->data.objref->u.data.u.intconst)) {
            return CExpr_SetupStandardConversionResult(
                expr, type2, qual2, SSCR_CONVERSION, implicit_object, refFlag, result
            );
        }

        if (ENODE_IS(expr, EOBJLIST)) {
            return CExpr_OverloadFuncMatch(
                       expr->data.objlist.list, expr->data.objlist.templargs, type2, NULL
                   ) &&
                CExpr_SetupStandardConversionResult(
                       expr, type2, qual2, SSCR_EXACT, refFlag, implicit_object, result
                );
        }

        if (ENODE_IS(expr, EOBJREF) && IS_TEMPL_FUNC(expr->data.objref->type)) {
            list.next = NULL;
            list.object = OBJ_BASE(expr->data.objref);
            return CExpr_OverloadFuncMatch(&list, NULL, type2, NULL) &&
                CExpr_SetupStandardConversionResult(
                       expr, type2, qual2, SSCR_EXACT, refFlag, implicit_object, result
                );
        }

        if (IS_TYPE_POINTER_ONLY(type1)) {
            if (ENODE_IS(expr, ESTRINGCONST) &&
                TPTR_TARGET(type2) == TPTR_TARGET(type1) &&
                !(qual2 & Q_CONST) &&
                (TPTR_TARGET(type2) == TYPE(&stchar) ||
                 TPTR_TARGET(type2) == TYPE(&stunsignedchar) ||
                 TPTR_TARGET(type2) == CParser_GetWCharType())) {
                if (CExpr_SetupStandardConversionResult(
                        expr, type2, qual2, SSCR_EXACT, refFlag, implicit_object, result
                    ) &&
                    CExpr_SetQualConversionResult(
                        TPTR_TARGET(type2), qual2, TPTR_TARGET(type1), qual1 & ~Q_CONST, result
                    )) {
                    result->qual_adjust = TRUE;
                    return TRUE;
                } else {
                    return FALSE;
                }
            }

            if (copts.objective_c && CObjC_IsCompatibleType(expr->rtype, type2)) {
                return CExpr_SetupStandardConversionResult(
                    expr, type2, qual2, SSCR_CONVERSION, implicit_object, refFlag, result
                );
            }

            if (IS_TYPE_VOID(TPTR_TARGET(type2)) ||
                (!copts.cplusplus && IS_TYPE_VOID(TPTR_TARGET(type1)))) {
                if (CExpr_SetupStandardConversionResult(
                        expr,
                        type2,
                        qual2,
                        IS_TYPE_VOID(TPTR_TARGET(type1)) ? SSCR_EXACT : SSCR_CONVERSION,
                        refFlag,
                        implicit_object,
                        result
                    )) {
                    switch (CExpr_CompareQualifiers(
                        qual2, CParser_GetCVTypeQualifiers(TPTR_TARGET(type1), qual1)
                    )) {
                        case QD_MORE:
                            result->qual_adjust = TRUE;
                        case QD_SAME:
                            return TRUE;
                        default:
                            return FALSE;
                    }
                } else {
                    return FALSE;
                }
            }

            inner2 = TPTR_TARGET(type2);
            inner1 = TPTR_TARGET(type1);
            while (1) {
                if (inner2->type != inner1->type) {
                    break;
                }

                switch (inner2->type) {
                    case TYPEPOINTER:
                        inner2 = TPTR_TARGET(inner2);
                        inner1 = TPTR_TARGET(inner1);
                        continue;
                    case TYPEMEMBERPOINTER:
                        if (!is_typesame(
                                TYPE_MEMBER_POINTER(inner2)->ty2, TYPE_MEMBER_POINTER(inner1)->ty2
                            )) {
                            break;
                        }

                        inner2 = TYPE_MEMBER_POINTER(inner2)->ty1;
                        inner1 = TYPE_MEMBER_POINTER(inner1)->ty1;
                        if (!IS_TYPE_POINTER_ONLY(inner2) && !IS_TYPE_MEMBERPOINTER(inner2)) {
                            if (!is_memberpointerequal(inner2, inner1)) {
                                break;
                            }

                            if (CExpr_SetupStandardConversionResult(
                                    expr, type2, qual2, SSCR_EXACT, refFlag, implicit_object, result
                                )) {
                                return CExpr_SetQualConversionResult(
                                    TPTR_TARGET(type2), qual2, TPTR_TARGET(type1), qual1, result
                                );
                            } else {
                                return FALSE;
                            }
                        }
                        continue;
                    default:
                        if (!is_typesame(inner2, inner1)) {
                            break;
                        }

                        if (CExpr_SetupStandardConversionResult(
                                expr, type2, qual2, SSCR_EXACT, refFlag, implicit_object, result
                            )) {
                            return CExpr_SetQualConversionResult(
                                TPTR_TARGET(type2), qual2, TPTR_TARGET(type1), qual1, result
                            );
                        } else {
                            return FALSE;
                        }
                }

                break;
            }

            if (IS_TYPE_CLASS(TPTR_TARGET(type2)) && IS_TYPE_CLASS(TPTR_TARGET(type1))) {
                short depth;
                Boolean isambigbase;
                if (CClass_GetBasePath(
                        TYPE_CLASS(TPTR_TARGET(type1)),
                        TYPE_CLASS(TPTR_TARGET(type2)),
                        &depth,
                        &isambigbase
                    )) {
                    if (CExpr_SetupStandardConversionResult(
                            expr, type2, qual2, SSCR_CONVERSION, refFlag, implicit_object, result
                        )) {
                        return CExpr_SetQualConversionResult(
                            TPTR_TARGET(type2), qual2, TPTR_TARGET(type1), qual1, result
                        );
                    } else {
                        return FALSE;
                    }
                }
            }
        }

        if (copts.mpwc_relax && !copts.cplusplus && IS_TYPE_POINTER_ONLY(type1)) {
            if (CExpr_SetupStandardConversionResult(
                    expr, type2, qual2, SSCR_CONVERSION, refFlag, implicit_object, result
                )) {
                return TRUE;
            }
        }

        return FALSE;
    }

    if (is_typesame(type2, type1)) {
        return CExpr_SetupStandardConversionResult(
            expr, type2, qual2, SSCR_EXACT, refFlag, implicit_object, result
        );
    }

    if (type2 == TYPE(&stbool)) {
        switch (type1->type) {
            case TYPEINT:
            case TYPEFLOAT:
            case TYPEENUM:
            case TYPEMEMBERPOINTER:
            case TYPEPOINTER:
                return CExpr_SetupStandardConversionResult(
                    expr, type2, qual2, SSCR_CONVERSION, refFlag, implicit_object, result
                );
            default:
                return FALSE;
        }
    }

    if (IS_TYPE_MEMBERPOINTER(type2)) {
        if (ENODE_IS(expr, EINTCONST) && CInt64_IsZero(&expr->data.intval)) {
            if (IS_TYPE_INT(type1) || (!copts.cplusplus && IS_TYPE_ENUM(type1))) {
                return CExpr_SetupStandardConversionResult(
                    expr, type2, qual2, SSCR_CONVERSION, refFlag, implicit_object, result
                );
            }
        }

        if (ENODE_IS(expr, EMEMBER)) {
            expr = getpointertomemberfunc(expr, type2, FALSE);
            type1 = expr->rtype;
        }

        if (IS_TYPE_MEMBERPOINTER(type1)) {
            short depth;
            Boolean isambigbase;

            CError_ASSERT(
                996,
                IS_TYPE_CLASS(TYPE_MEMBER_POINTER(type2)->ty2) &&
                    IS_TYPE_CLASS(TYPE_MEMBER_POINTER(type1)->ty2)
            );
            if (!is_memberpointerequal(
                    TYPE_MEMBER_POINTER(type2)->ty1, TYPE_MEMBER_POINTER(type1)->ty1
                )) {
                return FALSE;
            }

            if (TYPE_MEMBER_POINTER(type2)->ty2 == TYPE_MEMBER_POINTER(type1)->ty2 ||
                CClass_GetBasePath(
                    TYPE_CLASS(TYPE_MEMBER_POINTER(type2)->ty2),
                    TYPE_CLASS(TYPE_MEMBER_POINTER(type1)->ty2),
                    &depth,
                    &isambigbase
                )) {
                if (CExpr_SetupStandardConversionResult(
                        expr, type2, qual2, SSCR_CONVERSION, refFlag, implicit_object, result
                    )) {
                    return CExpr_SetQualConversionResult(
                        TYPE_MEMBER_POINTER(type2)->ty1,
                        qual2,
                        TYPE_MEMBER_POINTER(type1)->ty1,
                        qual1,
                        result
                    );
                } else {
                    return FALSE;
                }
            }
        }

        return FALSE;
    }

    mode = SSCR_CONVERSION;
    switch (type1->type) {
        case TYPEINT:
            switch (type2->type) {
                case TYPEINT:
                    if (TYPE_INTEGRAL(type1)->integral < IT_INT) {
                        if (type2 == TYPE(&stsignedint)) {
                            if (type1->size < type2->size || !is_unsigned(type1)) {
                                mode = SSCR_PROMOTION;
                            }
                        } else if (type2 == TYPE(&stunsignedint)) {
                            if (type2->size == type1->size && is_unsigned(type1)) {
                                mode = SSCR_PROMOTION;
                            }
                        }
                    }
                    break;
                case TYPEFLOAT:
                    break;
                case TYPEENUM:
                    if (copts.cplusplus) {
                        return FALSE;
                    }
                    break;
                default:
                    return FALSE;
            }
            break;

        case TYPEFLOAT:
            switch (type2->type) {
                case TYPEINT:
                    break;
                case TYPEFLOAT:
                    if (type2 == TYPE(&stdouble)) {
                        if (type1 == TYPE(&stfloat) || type1 == TYPE(&stshortdouble)) {
                            mode = SSCR_PROMOTION;
                        }
                    }
                    break;
                case TYPEENUM:
                    if (copts.cplusplus) {
                        return FALSE;
                    }
                    break;
                default:
                    return FALSE;
            }
            break;

        case TYPEENUM:
            switch (type2->type) {
                case TYPEINT:
                    if (TYPE_INTEGRAL(TYPE_ENUM(expr->rtype)->enumtype)->integral < IT_INT) {
                        if (type1->size == type2->size && is_unsigned(TYPE_ENUM(type1)->enumtype)) {
                            if (type2 == TYPE(&stunsignedint)) {
                                mode = SSCR_PROMOTION;
                            }
                        } else {
                            if (type2 == TYPE(&stsignedint)) {
                                mode = SSCR_PROMOTION;
                            }
                        }
                    } else {
                        if (TYPE_ENUM(type1)->enumtype == type2) {
                            mode = SSCR_PROMOTION;
                        }
                    }
                    break;
                case TYPEFLOAT:
                    break;
                case TYPEENUM:
                    if (copts.cplusplus) {
                        return FALSE;
                    }
                    break;
                default:
                    return FALSE;
            }
            break;

        case TYPECLASS: {
            short depth;
            Boolean isambigbase;

            if (!IS_TYPE_CLASS(type1) ||
                !CClass_GetBasePath(TYPE_CLASS(type1), TYPE_CLASS(type2), &depth, &isambigbase)) {
                return FALSE;
            }
            break;
        }

        default:
            return FALSE;
    }

    return CExpr_SetupStandardConversionResult(
        expr, type2, qual2, mode, refFlag, implicit_object, result
    );
}

static ENode* CExpr_UserConversion(
    ENode* expr,
    Type* type2,
    UInt32 qual2,
    ImplicitConv* result,
    Boolean doConvert,
    Boolean isExplicit,
    Boolean bindReference
) {
    StandardConv conv_std;
    StandardConv ctor_std;
    StandardConv cand_std;
    ConversionIterator convIter;
    Object* best_conv;
    Object* best_ctor;
    Object* cand;
    ObjectList* ambig_convs;
    ObjectList* ambig_ctors;
    ObjectList* objlist;
    TypeFunc* tfunc;
    Type* argtype;
    ENodeList* arglist;
    NameSpaceObjectList* ctors;
    Boolean cv_mismatch_ok;
    Boolean cv_differs;
    FuncArg* arg;
    Boolean nonconst_ref;
    ENode* newExpr;
    ENode* funcref;
    ENode* converted;
    UInt32 q1;
    UInt32 q2;
    ENodeList myarglist;
    ObjectList myobjlist;
    BClassList path;

    best_conv = NULL;
    best_ctor = NULL;
    ambig_convs = NULL;
    ambig_ctors = NULL;

    if (type2->size == 0) {
        CDecl_CompleteType(type2);
    }
    if (expr->rtype->size == 0) {
        CDecl_CompleteType(expr->rtype);
    }

    if (IS_TYPE_CLASS(expr->rtype)) {
        CExpr_ConversionIteratorInit(&convIter, TYPE_CLASS(expr->rtype));
        cv_mismatch_ok = TRUE;
        while ((cand = CExpr_ConversionIteratorNext(&convIter))) {
            tfunc = TYPE_FUNC(cand->type);
            if (tfunc->flags & FUNC_IS_TEMPL) {
                cand = CTempl_DeduceFromConversion(cand, type2, qual2);
                if (!cand) {
                    continue;
                }
                tfunc = TYPE_FUNC(cand->type);
            }

            if (bindReference) {
                if (!IS_TYPE_REFERENCE(tfunc->functype) ||
                    !CExpr_IsReferenceCompatible(
                        type2, qual2, TPTR_TARGET(tfunc->functype), tfunc->qual
                    )) {
                    continue;
                }
            }

            CError_ASSERT(1230, tfunc->args && IS_TYPE_POINTER_ONLY(tfunc->args->type));

            q1 = expr->flags;
            q2 = tfunc->args->qual;
            if ((q1 & Q_CV) != (q2 & Q_CV)) {
                if (!cv_mismatch_ok) {
                    continue;
                }
                if ((q1 & Q_CONST) && !(q2 & Q_CONST)) {
                    continue;
                }
                if ((q1 & Q_VOLATILE) && !(q2 & Q_VOLATILE)) {
                    continue;
                }
                cv_differs = TRUE;
            } else {
                cv_differs = FALSE;
            }

            newExpr = CExpr_NewENode(ETEMP);
            newExpr->rtype = tfunc->functype;
            newExpr->flags = tfunc->qual & ENODE_FLAG_QUALS;
            nonconst_ref = FALSE;

            if (IS_TYPE_REFERENCE(newExpr->rtype)) {
                newExpr->rtype = TPTR_TARGET(newExpr->rtype);
                if (!CParser_IsConst(newExpr->rtype, tfunc->qual)) {
                    newExpr = makemonadicnode(newExpr, EINDIRECT);
                    newExpr->data.monadic->rtype = TYPE(&void_ptr);
                    newExpr = makemonadicnode(newExpr, EINDIRECT);
                    newExpr->data.monadic->rtype = TYPE(&void_ptr);
                    nonconst_ref = TRUE;
                }
            }

            if (CExpr_StandardConversionMatch(newExpr, type2, qual2, FALSE, &cand_std)) {
                if (cv_mismatch_ok && !cv_differs) {
                    best_conv = NULL;
                    ambig_convs = NULL;
                    cv_mismatch_ok = FALSE;
                }

                if (best_conv && best_conv != cand) {
                    if (CExpr_IsBetterStandardConv(&conv_std, &cand_std)) {
                        continue;
                    }

                    if (!CExpr_IsBetterStandardConv(&cand_std, &conv_std)) {
                        objlist = lalloc(sizeof(ObjectList));
                        objlist->next = ambig_convs;
                        objlist->object = best_conv;
                        ambig_convs = objlist;
                    } else {
                        ambig_convs = NULL;
                    }
                }

                best_conv = cand;
                conv_std = cand_std;
                conv_std.nonconst_ref_result = nonconst_ref;
            }
        }
    }

    if (!bindReference && IS_TYPE_CLASS(type2) && (ctors = CClass_Constructor(TYPE_CLASS(type2)))) {
        for (; ctors; ctors = ctors->next) {
            cand = OBJECT(ctors->object);
            if (cand->otype == OT_OBJECT &&
                IS_TYPE_FUNC(cand->type) &&
                (isExplicit || !(cand->qual & Q_EXPLICIT))) {
                tfunc = TYPE_FUNC(cand->type);
                if (tfunc->flags & FUNC_IS_TEMPL) {
                    myarglist.next = NULL;
                    myarglist.node = expr;
                    cand = CTempl_DeduceFromFunctionCall(cand, NULL, &myarglist);
                    if (!cand) {
                        continue;
                    }
                    tfunc = TYPE_FUNC(cand->type);
                }

                if (!(arg = tfunc->args)) {
                    continue;
                }
                if (!(arg = arg->next)) {
                    continue;
                }
                if ((TYPE_CLASS(type2)->flags & CLASS_HAS_VBASES) && !(arg = arg->next)) {
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
                    argtype = TPTR_TARGET(argtype);
                    if (!CParser_IsConst(argtype, arg->qual) && !CExpr_IsLValue(expr)) {
                        continue;
                    }
                }

                if (CExpr_StandardConversionMatch(expr, argtype, arg->qual, FALSE, &cand_std)) {
                    if (best_ctor) {
                        if (cand->u.func.inst && !best_ctor->u.func.inst) {
                            continue;
                        }
                        if (CExpr_IsBetterStandardConv(&ctor_std, &cand_std)) {
                            continue;
                        }

                        if (!CExpr_IsBetterStandardConv(&cand_std, &ctor_std)) {
                            if (!cand->u.func.inst && best_ctor->u.func.inst) {
                                ambig_ctors = NULL;
                            } else {
                                objlist = lalloc(sizeof(ObjectList));
                                objlist->next = ambig_ctors;
                                objlist->object = best_ctor;
                                ambig_ctors = objlist;
                            }
                        } else {
                            ambig_ctors = NULL;
                        }
                    }

                    best_ctor = cand;
                    ctor_std = cand_std;
                }
            }
        }
    }

    if (best_conv && best_ctor) {
        if (!CExpr_IsBetterStandardConv(&ctor_std, &conv_std)) {
            if (!CExpr_IsBetterStandardConv(&conv_std, &ctor_std)) {
                if (result) {
                    result->type = ICT_USER;
                    result->u.user.func = best_conv;
                    result->u.user.standardConv = conv_std;
                }
                if (doConvert) {
                    myobjlist.next = NULL;
                    myobjlist.object = best_ctor;
                    CError_OverloadedFunctionError(best_conv, &myobjlist);
                }
            } else {
                best_ctor = NULL;
            }
        } else {
            best_conv = NULL;
        }
    }

    if (best_conv) {
        if (result) {
            result->type = ICT_USER;
            result->u.user.func = best_conv;
            result->u.user.standardConv = conv_std;
        }
        if (!doConvert) {
            return expr;
        }
        if (ambig_convs) {
            CError_OverloadedFunctionError(best_conv, ambig_convs);
        }
        tfunc = TYPE_FUNC(best_conv->type);
        CError_ASSERT(1416, IS_TYPEFUNC_METHOD(tfunc));

        funcref = create_objectrefnode(best_conv);
        best_conv->flags |= OBJECT_USED;

        arglist = lalloc(sizeof(ENodeList));
        arglist->next = NULL;

        expr = getnodeaddress(expr, FALSE);
        arglist->node = CExpr_AssignmentPromotion(
            expr, CDecl_NewPointerType(TYPE(TYPE_METHOD(tfunc)->theclass)), expr->flags, FALSE
        );

        newExpr = lalloc(sizeof(ENode));
        newExpr->type = EFUNCCALL;
        newExpr->cost = 4;
        newExpr->rtype = tfunc->functype;
        newExpr->flags = tfunc->qual & ENODE_FLAG_QUALS;
        newExpr->data.funccall.funcref = funcref;
        newExpr->data.funccall.args = arglist;
        newExpr->data.funccall.functype = TYPE_FUNC(best_conv->type);
        newExpr = CExpr_AdjustFunctionCall(newExpr);
        newExpr = checkreference(newExpr);

        if (newExpr->rtype != type2) {
            if (bindReference &&
                (converted = CExpr_DerivedToBase(newExpr, type2, qual2, TRUE, FALSE, TRUE))) {
                return converted;
            }
            newExpr = CExpr_Convert(newExpr, type2, qual2, FALSE, TRUE);
        }
        return newExpr;
    }

    if (best_ctor) {
        if (result) {
            result->type = ICT_USER;
            result->u.user.func = best_ctor;
            result->u.user.standardConv = ctor_std;
        }
        if (!doConvert) {
            return expr;
        }
        if (ambig_ctors) {
            CError_OverloadedFunctionError(best_ctor, ambig_ctors);
        }

        arglist = lalloc(sizeof(ENodeList));
        arglist->next = NULL;
        arglist->node = expr;

        if (TYPE_CLASS(type2)->flags & CLASS_HAS_VBASES) {
            arglist->next = lalloc(sizeof(ENodeList));
            arglist->next->node = expr;
            arglist->next->next = NULL;
            arglist->node = intconstnode(TYPE(&stsignedshort), 1);
        }

        path.next = NULL;
        path.type = type2;

        newExpr = makemonadicnode(create_temp_node(type2), EINDIRECT);
        newExpr->rtype = type2;

        converted = CExpr_GenericFuncCall(
            &path, newExpr, FALSE, best_ctor, NULL, NULL, arglist, FALSE, FALSE, TRUE
        );

        if (ENODE_IS2(converted, EFUNCCALL, EFUNCCALLP)) {
            converted->rtype = CDecl_NewPointerType(type2);
            converted = makemonadicnode(converted, EINDIRECT);
            converted->rtype = type2;
        }

        return converted;
    }

    return NULL;
}

static Boolean CExpr_UserConversionMatch(
    ENode* expr, Type* type2, UInt32 qual2, ImplicitConv* result
) {
    Boolean bindReference;

    if (IS_TYPE_REFERENCE(type2)) {
        type2 = TPTR_TARGET(type2);
        bindReference = !CParser_IsConst(type2, qual2);
    } else {
        expr = pointer_generation(expr);
        bindReference = FALSE;
    }

    if (CExpr_UserConversion(expr, type2, qual2, result, FALSE, FALSE, bindReference)) {
        return TRUE;
    }

    return FALSE;
}

static Boolean CExpr_ImplicitConversionMatch(
    ENode* expr, Type* type2, UInt32 qual2, ImplicitConv* result
) {
    if (CExpr_StandardConversionMatch(
            expr, type2, qual2, FALSE, &result->u.standard.standardConv
        )) {
        result->type = ICT_STANDARD;
        return TRUE;
    }

    if (IS_TYPE_CLASS(expr->rtype) ||
        IS_TYPE_CLASS(type2) ||
        (IS_TYPE_REFERENCE(type2) && IS_TYPE_CLASS(TPTR_TARGET(type2)))) {
        if (CExpr_UserConversionMatch(expr, type2, qual2, result)) {
            result->type = ICT_USER;
            return TRUE;
        }
    }

    return FALSE;
}

Boolean CExpr_CanImplicitlyConvert(ENode* expr, Type* type2, UInt32 qual2) {
    ImplicitConv result;
    return CExpr_ImplicitConversionMatch(expr, type2, qual2, &result);
}

static inline ENode* CExpr_DerivedToBase(
    ENode* expr,
    Type* type2,
    UInt32 qual2,
    Boolean checkAccess,
    Boolean nullcheckflag,
    Boolean pathcheckflag
) {
    BClassList* path;
    short depth;
    Boolean isambigbase;

    if (IS_TYPE_CLASS(type2) &&
        IS_TYPE_CLASS(expr->rtype) &&
        (path = CClass_GetBasePath(
             TYPE_CLASS(expr->rtype), TYPE_CLASS(type2), &depth, &isambigbase
         ))) {
        if (isambigbase) {
            CError_Error(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
        }
        if (checkAccess) {
            CClass_CheckPathAccess(path, NULL, ACCESSPUBLIC);
        }

        if (pathcheckflag) {
            expr = getnodeaddress(expr, FALSE);
            expr = CExpr_ClassPointerCast(path, expr, nullcheckflag);
            expr = makemonadicnode(expr, EINDIRECT);
            expr->rtype = type2;
            expr->flags = qual2 & ENODE_FLAG_QUALS;
        }

        return expr;
    } else {
        return NULL;
    }
}

static ENode* CExpr_ClassReferenceConversion(
    ENode* expr, Type* type2, UInt32 qual2, Boolean pathcheckflag
) {
    int refcompat;

    if (CExpr_IsLValue(expr) && IS_TYPE_CLASS(type2) && IS_TYPE_CLASS(expr->rtype)) {
        if ((refcompat = CExpr_IsReferenceCompatible(type2, 0, expr->rtype, 0)) > 0) {
            if (refcompat == 2) {
                CError_ASSERT(1668, IS_TYPE_CLASS(type2) && IS_TYPE_CLASS(expr->rtype));
                expr = CExpr_DerivedToBase(expr, type2, qual2, pathcheckflag, FALSE, TRUE);
            }
            expr->flags = qual2 & ENODE_FLAG_QUALS;
            return expr;
        }

        refcompat = CExpr_IsReferenceCompatible(expr->rtype, 0, type2, 0);
        if (refcompat > 0) {
            expr = CClass_ClassPointerCast(
                getnodeaddress(expr, FALSE),
                TYPE_CLASS(expr->rtype),
                TYPE_CLASS(type2),
                TRUE,
                TRUE,
                pathcheckflag
            );
            CError_ASSERT(1680, IS_TYPE_POINTER_ONLY(expr->rtype));
            expr = makemonadicnode(expr, EINDIRECT);
            expr->rtype = type2;
            expr->flags = qual2 & ENODE_FLAG_QUALS;
            return expr;
        }
    }

    if (IS_TYPE_CLASS(expr->rtype)) {
        if ((expr = CExpr_UserConversion(expr, type2, qual2, NULL, TRUE, FALSE, TRUE))) {
            return expr;
        }
    }

    return NULL;
}

static ENode* CExpr_BindToReference(ENode* expr, Type* type2, UInt32 qual2) {
    UInt32 cv;
    int refcompat;
    ENode* converted;

    cv = CParser_GetCVTypeQualifiers(type2, qual2);

    if (CExpr_IsLValue(expr)) {
        if ((refcompat =
                 CExpr_IsReferenceCompatible(type2, qual2, expr->rtype, ENODE_QUALS(expr))) > 0) {
            if (refcompat == 2) {
                CError_ASSERT(1718, IS_TYPE_CLASS(type2) && IS_TYPE_CLASS(expr->rtype));
                expr = CExpr_DerivedToBase(expr, type2, qual2, TRUE, FALSE, TRUE);
            }
            return getnodeaddress(expr, FALSE);
        }
    } else if (IS_TYPE_CLASS(type2) && IS_TYPE_CLASS(expr->rtype)) {
        if ((refcompat =
                 CExpr_IsReferenceCompatible(type2, qual2, expr->rtype, ENODE_QUALS(expr))) > 0) {
            if (refcompat == 2) {
                expr = CExpr_DerivedToBase(expr, type2, qual2, TRUE, FALSE, TRUE);
            }
            return getnodeaddress(expr, FALSE);
        }
    }

    if (IS_TYPE_CLASS(expr->rtype)) {
        if ((converted = CExpr_UserConversion(expr, type2, qual2, NULL, TRUE, FALSE, TRUE))) {
            return getnodeaddress(converted, FALSE);
        }
    }

    if (!(cv & Q_CONST)) {
        CError_Error(ERR_NON_CONST_AMPERSAND_REFERENCE_INITIALIZED_TEMPORARY);
    }
    if (cv & Q_VOLATILE) {
        CError_Error(ERR_ILLEGAL_CONST_VOLATILE_AMPERSAND_REFERENCE_INITIALIZATION);
    }

    if (expr->rtype != type2) {
        expr = CExpr_Convert(expr, type2, qual2, FALSE, TRUE);
    }

    if (!CExpr_IsLValue(expr)) {
        expr = CExpr_LValue(expr, FALSE, FALSE);
        if (!ENODE_IS(expr, EINDIRECT)) {
            expr = get_address_of_temp_copy(expr, TRUE);
        } else {
            expr = getnodeaddress(expr, FALSE);
        }
    } else {
        expr = getnodeaddress(expr, FALSE);
    }

    return expr;
}

ENode* CExpr_Convert(
    ENode* expr, Type* type, UInt32 qual, Boolean isExplicit, Boolean checkAccess
) {
    UInt32 cv;
    ENode* refExpr;
    ENode* newExpr;
    Type* typeCopy;
    NameSpaceObjectList myList;

    cv = qual & Q_CV;

    if (copts.cpp_extensions && is_typesame(expr->rtype, type) && !ENODE_IS(expr, EOBJLIST)) {
        expr = CExpr_RewriteConst(expr);
        expr->rtype = type;
        expr->flags &= ~ENODE_FLAG_QUALS;
        expr->flags |= cv;
        return expr;
    }

    if (type == TYPE(&stvoid)) {
        expr = makemonadicnode(expr, ETYPCON);
        expr->rtype = type;
        expr->flags = cv;
        return expr;
    }

    if (IS_TYPE_REFERENCE(type)) {
        if (isExplicit) {
            refExpr = CExpr_ClassReferenceConversion(expr, TPTR_TARGET(type), qual, checkAccess);
            if (refExpr) {
                return refExpr;
            }

            expr = getnodeaddress(expr, FALSE);
            typeCopy = galloc(sizeof(TypePointer));
            *TYPE_POINTER(typeCopy) = *TYPE_POINTER(type);
            TPTR_QUAL(typeCopy) &= ~Q_REFERENCE;
            expr = CExpr_Convert(expr, typeCopy, qual, FALSE, checkAccess);
            expr = makemonadicnode(expr, EINDIRECT);
            expr->rtype = TPTR_TARGET(type);
            expr->flags = cv;
        } else {
            expr = CExpr_BindToReference(expr, TPTR_TARGET(type), qual);
        }
        return expr;
    }

    if ((IS_TYPE_ARRAY(expr->rtype) && !IS_TYPE_ARRAY(type)) ||
        (IS_TYPE_FUNC(expr->rtype) && !IS_TYPE_FUNC(type))) {
        expr = pointer_generation(expr);
    } else {
        expr = CExpr_RewriteConst(expr);
    }

    if (ENODE_IS(expr, EOBJLIST)) {
        if (CExpr_OverloadFuncMatch(
                expr->data.objlist.list, expr->data.objlist.templargs, type, &refExpr
            )) {
            return refExpr;
        }
    } else if (ENODE_IS(expr, EOBJREF) && IS_TEMPL_FUNC(expr->data.objref->type)) {
        myList.next = NULL;
        myList.object = OBJ_BASE(expr->data.objref);
        if (CExpr_OverloadFuncMatch(&myList, NULL, type, &refExpr)) {
            return refExpr;
        }
    } else if (IS_TYPE_CLASS(expr->rtype) || IS_TYPE_CLASS(type)) {
        if (expr->rtype->size == 0) {
            CDecl_CompleteType(expr->rtype);
        }

        if (IS_TYPE_CLASS(type)) {
            CanAllocObject(type);
            if (!CClass_CopyConstructor(TYPE_CLASS(type)) ||
                CClass_IsTrivialCopyClass(TYPE_CLASS(type))) {
                if (expr->rtype == type) {
                    return expr;
                }

                refExpr = CExpr_DerivedToBase(expr, type, qual, checkAccess, FALSE, TRUE);
                if (refExpr) {
                    return refExpr;
                }
            }
        }

        refExpr = CExpr_UserConversion(expr, type, qual, NULL, TRUE, isExplicit, FALSE);
        if (refExpr) {
            refExpr->flags = cv;
            return refExpr;
        }
    } else if (!isExplicit && is_typesame(expr->rtype, type)) {
        if (ENODE_IS(expr, EINDIRECT) && ENODE_QUALS(expr) != (cv & Q_CV)) {
            expr = makemonadicnode(expr, ETYPCON);
        }
        expr->rtype = type;
        expr->flags = cv;
        return expr;
    } else {
        if (copts.warn_implicitconv &&
            !isExplicit &&
            IS_TYPE_INT_OR_FLOAT(type) &&
            IS_TYPE_INT_OR_FLOAT(expr->rtype)) {
            CExpr_CheckArithmConversion(expr, type);
        }

        switch (type->type) {
            case TYPEINT:
                if (type == TYPE(&stbool)) {
                    switch (expr->rtype->type) {
                        case TYPEINT:
                        case TYPEFLOAT:
                        case TYPEENUM:
                        case TYPEMEMBERPOINTER:
                        case TYPEPOINTER:
                            return CExpr_ConvertToBool(expr, isExplicit);
                        default:
                            break;
                    }
                } else {
                    switch (expr->rtype->type) {
                        case TYPEENUM:
                            expr->rtype = TYPE_ENUM(expr->rtype)->enumtype;
                        case TYPEINT:
                        case TYPEFLOAT:
do_int_float_conversion:
                            if (ENODE_IS(expr, ETYPCON) &&
                                expr->rtype->type == type->type &&
                                expr->rtype->size == type->size &&
                                is_unsigned(expr->rtype) == is_unsigned(type) &&
                                ENODE_QUALS(expr) == qual) {
                                expr->rtype = type;
                                expr->flags |= ENODE_FLAG_SAME_TYPE_CAST;
                                return expr;
                            } else {
                                refExpr = promote(expr, type);
                                refExpr->flags = cv;
                                refExpr = CodeGen_CheckExpr(refExpr);
                                return refExpr;
                            }
                            break;
                        case TYPEPOINTER:
                            if (expr->rtype->size > type->size && copts.warn_ptr_int_conv) {
                                CError_Warning(ERR_INTEGRAL_TYPE_TOO_SMALL_FOR_POINTER);
                            }
                            if (ENODE_IS(expr, ETYPCON)) {
                                ENode* inner = expr->data.monadic;
                                if (ENODE_IS(inner, EINTCONST)) {
                                    inner->rtype = type;
                                    inner->flags = cv;
                                    inner->data.intval = CExpr_IntConstConvert(
                                        type, TYPE(&stunsignedlong), inner->data.intval
                                    );
                                    return inner;
                                }
                            }
                            if (type->size != 4) {
                                expr = makemonadicnode(expr, ETYPCON);
                                expr->rtype = TYPE(&stunsignedlong);
                            }
                            expr = makemonadicnode(expr, ETYPCON);
                            expr->rtype = type;
                            expr->flags = cv;
                            return expr;
                        default:
                            break;
                    }
                }
                break;

            case TYPEFLOAT:
                switch (expr->rtype->type) {
                    case TYPEENUM:
                        expr->rtype = TYPE_ENUM(expr->rtype)->enumtype;
                    case TYPEINT:
                    case TYPEFLOAT:
                        goto do_int_float_conversion;
                    default:
                        break;
                }
                break;

            case TYPEENUM:
                expr =
                    CExpr_Convert(expr, TYPE_ENUM(type)->enumtype, qual, isExplicit, checkAccess);
                if (!ENODE_IS(expr, EINTCONST)) {
                    expr = makemonadicnode(expr, ETYPCON);
                }
                expr->rtype = type;
                expr->flags = cv;
                return CodeGen_CheckExpr(expr);

            case TYPEPOINTER:
                switch (expr->rtype->type) {
                    case TYPEENUM:
                        expr->rtype = TYPE_ENUM(expr->rtype)->enumtype;
                    case TYPEINT:
                        if (expr->rtype->size != 4) {
                            if (!ENODE_IS(expr, EINTCONST)) {
                                expr = makemonadicnode(expr, ETYPCON);
                            }
                            expr->rtype = TYPE(&stunsignedlong);
                        }
                        expr = makemonadicnode(expr, ETYPCON);
                        expr->rtype = type;
                        expr->flags = cv;
                        return CodeGen_CheckExpr(expr);

                    case TYPEPOINTER:
                        if (IS_TYPE_CLASS(TPTR_TARGET(expr->rtype)) &&
                            IS_TYPE_CLASS(TPTR_TARGET(type))) {
                            expr = CExpr_SafeClassPointerCast(
                                expr,
                                TYPE_CLASS(TPTR_TARGET(expr->rtype)),
                                TYPE_CLASS(TPTR_TARGET(type)),
                                TRUE,
                                checkAccess
                            );
                        }
                        if (!ENODE_IS(expr, ETYPCON)) {
                            expr = makemonadicnode(expr, ETYPCON);
                        }
                        expr->rtype = type;
                        expr->flags = cv;
                        return CodeGen_CheckExpr(expr);
                    default:
                        break;
                }
                break;

            case TYPEMEMBERPOINTER:
                if (!IS_TYPE_MEMBERPOINTER(expr->rtype)) {
                    expr = CExpr_MemberPointerConversion(expr, TYPE_MEMBER_POINTER(type), TRUE);
                }

                if (IS_TYPE_MEMBERPOINTER(expr->rtype)) {
                    expr = PointerToMemberCast(
                        expr,
                        TYPE_MEMBER_POINTER(expr->rtype),
                        TYPE_MEMBER_POINTER(type),
                        checkAccess
                    );
                    expr->flags = cv;
                    return expr;
                }
                break;
            default:
                break;
        }
    }

    if (isExplicit) {
        if ((newExpr = CodeGen_HandleTypeCast(expr, type, qual))) {
            return newExpr;
        }
    }

    CError_Error(
        isExplicit ? ERR_ILLEGAL_EXPLICIT_CONVERSION_FROM : ERR_ILLEGAL_IMPLICIT_CONVERSION_FROM,
        expr->rtype,
        ENODE_QUALS(expr),
        type,
        qual
    );
    return nullnode();
}

ENode* CExpr_AssignmentPromotion(ENode* expr, Type* type2, UInt32 qual2, Boolean checkAccess) {
    ImplicitConv result;

    if (copts.old_argmatch) {
        return oldassignmentpromotion(expr, type2, qual2, checkAccess);
    }

    if (ENODE_IS(expr, EMEMBER)) {
        expr = getpointertomemberfunc(expr, type2, TRUE);
    }

    if (!CExpr_ImplicitConversionMatch(expr, type2, qual2, &result)) {
        CError_Error(
            ERR_ILLEGAL_IMPLICIT_CONVERSION_FROM, expr->rtype, ENODE_QUALS(expr), type2, qual2
        );
        return nullnode();
    }

    return CExpr_Convert(expr, type2, qual2, FALSE, checkAccess);
}

static Boolean CExpr_IsBetterMatch(Match* a, Match* b, int count) {
    ImplicitConv* convA;
    ImplicitConv* convB;
    int i;
    Boolean better;

    convA = a->conv;
    convB = b->conv;
    better = FALSE;

    if (convA->type != ICT_NONE && convB->type != ICT_NONE) {
        if (CExpr_IsBetterImplicitConv(convB, convA)) {
            return FALSE;
        }
        if (CExpr_IsBetterImplicitConv(convA, convB)) {
            better = TRUE;
        }
    }

    for (i = 0; i < count; i++) {
        convA++;
        convB++;
        if (CExpr_IsBetterImplicitConv(convB, convA)) {
            return FALSE;
        }
        if (CExpr_IsBetterImplicitConv(convA, convB)) {
            better = TRUE;
        }
    }

    if (better) {
        return TRUE;
    }

    if (b->object) {
        CError_ASSERT(2165, IS_TYPE_FUNC(b->object->type));
        if (b->object->u.func.inst) {
            if (!a->object) {
                return TRUE;
            }

            CError_ASSERT(2169, IS_TYPE_FUNC(a->object->type));
            if (!a->object->u.func.inst) {
                return TRUE;
            }

            CError_ASSERT(2174, a->specialfunc && b->specialfunc);
            if (CTempl_FuncIsMoreSpecialized(a->specialfunc, b->specialfunc)) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static Boolean CExpr_MatchArgs(
    Object* func, ENodeList* argexprs, ENode* expr, ImplicitConv* convs
) {
    ENode* newExpr;
    FuncArg* args;
    Type* type;

    args = TYPE_FUNC(func->type)->args;

    if (!(TYPE_FUNC(func->type)->flags & FUNC_METHOD)) {
        convs->type = ICT_NONE;
    } else if (TYPE_METHOD(func->type)->is_static) {
        convs->type = ICT_NONE;
    } else if (TYPE_FUNC(func->type)->flags & FUNC_IS_CTOR) {
        convs->type = ICT_NONE;
        args = args->next;
    } else {
        if (!expr) {
            return FALSE;
        }

        newExpr = lalloc(sizeof(ENode));
        newExpr->type = EINTCONST;
        newExpr->cost = 0;
        newExpr->flags = expr->flags;
        newExpr->rtype = CDecl_NewPointerType(expr->rtype);
        newExpr->data.intval = cint64_zero;

        if (func->datatype == DALIAS) {
            CError_ASSERT(2231, func->u.alias.member);
            type = CDecl_NewPointerType(func->u.alias.member->type);
        } else {
            type = args->type;
        }

        if (!CExpr_ImplicitConversionMatch(newExpr, type, args->qual, convs)) {
            return FALSE;
        }
        args = args->next;
    }

    convs++;
    while (1) {
        if (!args || args->type == &stvoid) {
            if (argexprs) {
                return FALSE;
            } else {
                return TRUE;
            }
        }

        if (args == &elipsis || args == &oldstyle) {
            while (argexprs) {
                convs->type = ICT_ELLIPSIS;
                convs++;
                argexprs = argexprs->next;
            }
            return TRUE;
        }

        if (!argexprs) {
            if (args->dexpr) {
                return TRUE;
            }
            return FALSE;
        }

        if (!CExpr_ImplicitConversionMatch(argexprs->node, args->type, args->qual, convs)) {
            return FALSE;
        }

        argexprs = argexprs->next;
        args = args->next;
        convs++;
    }
}

static Object* CExpr_GetMatchObject(Match* match, HashNameNode* name) {
    Object* object;
    FuncArg* arg;
    TypeFunc* tfunc;

    if (match->object) {
        return match->object;
    }

    tfunc = lalloc(sizeof(TypeFunc));
    memclrw(tfunc, sizeof(TypeFunc));
    tfunc->type = TYPEFUNC;
    tfunc->functype = &stvoid;

    arg = lalloc(sizeof(FuncArg));
    memclrw(arg, sizeof(FuncArg));
    arg->type = match->type;
    arg->qual = match->qual;
    tfunc->args = arg;

    if (match->type2) {
        arg = lalloc(sizeof(FuncArg));
        memclrw(arg, sizeof(FuncArg));
        arg->type = match->type2;
        arg->qual = match->qual2;
        tfunc->args->next = arg;
    }

    object = lalloc(sizeof(Object));
    memclrw(object, sizeof(Object));
    object->name = name;
    object->datatype = DFUNC;
    object->nspace = cscope_root;
    object->type = TYPE(tfunc);
    return object;
}

static Match* CExpr_FindBestMatch(
    Match* matches, int count, HashNameNode* name, ObjectList** outList, ENodeList* argExprs
) {
    Match* best;
    Match* scan;
    ObjectList* listHead;
    ObjectList* list;

    best = matches;
    for (scan = matches->next; scan; scan = scan->next) {
        if (CExpr_IsBetterMatch(scan, best, count)) {
            best = scan;
        }
    }

    for (scan = matches, listHead = NULL; scan; scan = scan->next) {
        if (scan != best && !CExpr_IsBetterMatch(best, scan, count)) {
            list = lalloc(sizeof(ObjectList));
            list->next = listHead;
            list->object = CExpr_GetMatchObject(scan, name);
            listHead = list;
        }
    }

    if (!outList) {
        if (listHead) {
            CError_OverloadedFunctionError(CExpr_GetMatchObject(best, name), listHead);
        }
    } else {
        *outList = listHead;
    }

    return best;
}

void CExpr_FuncArgMatch(
    NameSpaceObjectList* list,
    TemplArg* templargs,
    ENodeList* argexprs,
    ArgMatch* bestmatch,
    ENode* expr,
    Boolean excludeExplicit
) {
    NameSpaceObjectList* i;
    NameSpaceObjectList* j;
    Match* match;
    Match* matches;
    Object* object;
    Type* type;
    Object* object2;
    Object* specialfunc;
    ENodeList* argscan;
    int argcount;

    for (argscan = argexprs, argcount = 0; argscan; argscan = argscan->next) {
        CDecl_CompleteType(argscan->node->rtype);
        argcount++;
    }

    matches = NULL;
    match = lalloc(sizeof(Match) + ((argcount - 2) * sizeof(ImplicitConv)));

    for (i = list; i; i = i->next) {
        object = OBJECT(i->object);
        if (object->otype == OT_OBJECT &&
            IS_TYPE_FUNC(type = object->type) &&
            (!excludeExplicit || !(object->qual & Q_EXPLICIT))) {
            if (object->datatype == DALIAS && (TYPE_FUNC(type)->flags & FUNC_METHOD)) {
                for (j = list; j; j = j->next) {
                    if (j == i) {
                        continue;
                    }

                    object2 = OBJECT(j->object);
                    if (object2->otype == OT_OBJECT &&
                        IS_TYPE_METHOD(object2->type) &&
                        (TYPE_FUNC(object2->type)->flags & FUNC_FLAGS_CV) ==
                            (TYPE_FUNC(object->type)->flags & FUNC_FLAGS_CV)) {
                        FuncArg* argsA;
                        FuncArg* argsB;
                        argsA = TYPE_FUNC(object->type)->args;
                        if (argsA && !TYPE_METHOD(object->type)->is_static) {
                            argsA = argsA->next;
                        }
                        argsB = TYPE_FUNC(object2->type)->args;
                        if (argsB && !TYPE_METHOD(object2->type)->is_static) {
                            argsB = argsB->next;
                        }
                        if (is_arglistsame(argsA, argsB)) {
                            break;
                        }
                    }
                }

                if (j) {
                    continue;
                }
            }

            if (TYPE_FUNC(object->type)->flags & FUNC_IS_TEMPL) {
                specialfunc = object;
                object = CTempl_DeduceFromFunctionCall(object, templargs, argexprs);
                if (!object) {
                    continue;
                }
            } else {
                if (templargs) {
                    continue;
                }
                specialfunc = NULL;
            }

            if (CExpr_MatchArgs(object, argexprs, expr, match->conv)) {
                match->object = object;
                match->specialfunc = specialfunc;
                match->next = matches;
                matches = match;
                match = lalloc(sizeof(Match) + ((argcount - 2) * sizeof(ImplicitConv)));
            }
        }
    }

    if (!matches) {
        return;
    }

    matches = CExpr_FindBestMatch(matches, argcount, NULL, &bestmatch->list, argexprs);
    bestmatch->object = matches->object;
}

static ConversionTypeList* CExpr_BuildConversionTypeList(ENode* expr) {
    ConversionTypeList* first;
    ConversionTypeList* list;
    Object* object;
    Type* type;
    ConversionIterator convIter;

    if (!IS_TYPE_CLASS(expr->rtype)) {
        first = lalloc(sizeof(ConversionTypeList));
        first->next = NULL;
        first->func = NULL;
        first->type = expr->rtype;
        first->qual = ENODE_QUALS(expr);
        if (IS_TYPE_ENUM(first->type)) {
            first->qual = 0;
        }
    } else {
        first = NULL;
        CExpr_ConversionIteratorInit(&convIter, TYPE_CLASS(expr->rtype));
        while ((object = CExpr_ConversionIteratorNext(&convIter))) {
            type = TYPE_FUNC(object->type)->functype;
            if (IS_TYPE_REFERENCE(type)) {
                type = TPTR_TARGET(type);
            }

            if (!IS_TYPE_CLASS(type)) {
                list = lalloc(sizeof(ConversionTypeList));
                list->next = first;
                list->func = object;
                list->type = type;
                list->qual = TYPE_FUNC(object->type)->qual & Q_CV;
                first = list;
            }
        }
    }

    return first;
}

static Type* CExpr_NextPromotedIntegralType(int* p) {
    switch (++(*p)) {
        case 1:
            return TYPE(&stsignedint);
        case 2:
            return TYPE(&stunsignedint);
        case 3:
            return TYPE(&stsignedlong);
        case 4:
            return TYPE(&stunsignedlong);
        case 5:
            return copts.longlong ? TYPE(&stsignedlonglong) : NULL;
        case 6:
            return copts.longlong ? TYPE(&stunsignedlonglong) : NULL;
    }

    return NULL;
}

static Type* CExpr_NextArithmeticType(int* p) {
    switch (++(*p)) {
        case 1:
            return TYPE(&stbool);
        case 2:
            return TYPE(&stchar);
        case 3:
            return TYPE(&stsignedchar);
        case 4:
            return TYPE(&stunsignedchar);
        case 5:
            return TYPE(&stwchar);
        case 6:
            return TYPE(&stsignedshort);
        case 7:
            return TYPE(&stunsignedshort);
        case 8:
            return TYPE(&stsignedint);
        case 9:
            return TYPE(&stunsignedint);
        case 10:
            return TYPE(&stsignedlong);
        case 11:
            return TYPE(&stunsignedlong);
        case 12:
            return TYPE(&stfloat);
        case 13:
            return TYPE(&stdouble);
        case 14:
            return TYPE(&stlongdouble);
        case 15:
            if (copts.longlong) {
                return TYPE(&stsignedlonglong);
            } else {
                return NULL;
            }
        case 16:
            if (copts.longlong) {
                return TYPE(&stunsignedlonglong);
            } else {
                return NULL;
            }
    }

    return NULL;
}

static Type* CExpr_NextPromotedArithmeticType(int* p) {
    switch (++(*p)) {
        case 1:
            return TYPE(&stsignedint);
        case 2:
            return TYPE(&stunsignedint);
        case 3:
            return TYPE(&stsignedlong);
        case 4:
            return TYPE(&stunsignedlong);
        case 5:
            return TYPE(&stdouble);
        case 6:
            return TYPE(&stlongdouble);
        case 7:
            return copts.longlong ? TYPE(&stsignedlonglong) : NULL;
        case 8:
            return copts.longlong ? TYPE(&stunsignedlonglong) : NULL;
    }

    return NULL;
}

static Match* CExpr_MatchBuiltin(
    Match* matches,
    ENode* left,
    Type* leftType,
    UInt32 leftQual,
    ENode* right,
    Type* rightType,
    UInt32 rightQual
) {
    Match* scan;
    Match mymatch;

    if (CExpr_ImplicitConversionMatch(left, leftType, leftQual, &mymatch.conv[0])) {
        if (!right ||
            CExpr_ImplicitConversionMatch(right, rightType, rightQual, &mymatch.conv[1])) {
            if (right) {
                for (scan = matches; scan; scan = scan->next) {
                    if (!scan->object &&
                        is_typesame(scan->type, leftType) &&
                        scan->qual == leftQual &&
                        is_typesame(scan->type2, rightType) &&
                        scan->qual2 == rightQual) {
                        return matches;
                    }
                }
            }

            mymatch.next = matches;
            mymatch.object = NULL;
            mymatch.specialfunc = NULL;
            mymatch.type = leftType;
            mymatch.qual = leftQual;
            mymatch.type2 = rightType;
            mymatch.qual2 = rightQual;

            matches = lalloc(sizeof(Match));
            *matches = mymatch;
        }
    }

    return matches;
}

static Match* CExpr_CheckIncDecBuiltin(Match* matches, short token, ENode* expr1, ENode* expr2) {
    Object* object;
    Type* type;
    TypeFunc* tfunc;
    ConversionIterator convIter;

    if (IS_TYPE_CLASS(expr1->rtype)) {
        CExpr_ConversionIteratorInit(&convIter, TYPE_CLASS(expr1->rtype));
        while ((object = CExpr_ConversionIteratorNext(&convIter))) {
            tfunc = TYPE_FUNC(object->type);
            type = tfunc->functype;
            if (IS_TYPE_REFERENCE(type)) {
                type = TPTR_TARGET(type);
                switch (type->type) {
                    case TYPEINT:
                        if (type == TYPE(&stbool) && token == TK_DECREMENT) {
                            break;
                        }
                    case TYPEFLOAT:
                    case TYPEPOINTER:
                        if (!CParser_IsConst(type, tfunc->qual)) {
                            matches = CExpr_MatchBuiltin(
                                matches,
                                expr1,
                                type,
                                tfunc->qual,
                                expr2,
                                expr2 ? TYPE(&stsignedint) : NULL,
                                0
                            );
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    }

    return matches;
}

static Match* CExpr_CheckUnaryBuiltin(Match* matches, short token, ENode* expr) {
    ConversionTypeList* typelist;
    Type* type;
    int typenum;

    switch (token) {
        case TK_INCREMENT:
        case TK_DECREMENT:
            return CExpr_CheckIncDecBuiltin(matches, token, expr, NULL);

        case '!':
            matches = CExpr_MatchBuiltin(matches, expr, TYPE(&stbool), 0, NULL, NULL, 0);
            break;

        case '~':
            typenum = 0;
            while ((type = CExpr_NextPromotedIntegralType(&typenum))) {
                matches = CExpr_MatchBuiltin(matches, expr, type, 0, NULL, NULL, 0);
            }
            break;

        case '*':
        case '+':
            for (typelist = CExpr_BuildConversionTypeList(expr); typelist;
                 typelist = typelist->next) {
                if (IS_TYPE_POINTER_ONLY(typelist->type)) {
                    matches = CExpr_MatchBuiltin(
                        matches, expr, typelist->type, typelist->qual, NULL, NULL, 0
                    );
                }
            }
            if (token != '+') {
                break;
            }

        case '-':
            typenum = 0;
            while ((type = CExpr_NextPromotedArithmeticType(&typenum))) {
                matches = CExpr_MatchBuiltin(matches, expr, type, 0, NULL, NULL, 0);
            }
            break;
    }

    return matches;
}

static Match* CExpr_CheckBinaryBuiltin(Match* matches, ENode* left, short token, ENode* right) {
    int typenum1;
    int typenum2;
    Type* type1;
    Type* type2;
    Type* ptrdiff;
    Boolean allowPtrDiffOnRight;
    Boolean allowPtrCV;
    Boolean allowPtrDiffOnLeft;
    Boolean allowMemberPtrs;
    Boolean allowEnum;
    ConversionTypeList* leftList;
    ConversionTypeList* rightList;
    ConversionTypeList* scan;

    switch (token) {
        case TK_INCREMENT:
        case TK_DECREMENT:
            return CExpr_CheckIncDecBuiltin(matches, token, left, right);

        case '*':
        case '+':
        case '-':
        case '/':
        case ':':
        case '<':
        case '>':
        case TK_LOGICAL_EQ:
        case TK_LOGICAL_NE:
        case TK_LESS_EQUAL:
        case TK_GREATER_EQUAL:
            typenum1 = 0;
            while ((type1 = CExpr_NextPromotedArithmeticType(&typenum1))) {
                typenum2 = 0;
                while ((type2 = CExpr_NextPromotedArithmeticType(&typenum2))) {
                    matches = CExpr_MatchBuiltin(matches, left, type1, 0, right, type2, 0);
                }
            }
            break;

        case '%':
        case '&':
        case '^':
        case '|':
        case TK_SHL:
        case TK_SHR:
            typenum1 = 0;
            while ((type1 = CExpr_NextPromotedIntegralType(&typenum1))) {
                typenum2 = 0;
                while ((type2 = CExpr_NextPromotedIntegralType(&typenum2))) {
                    matches = CExpr_MatchBuiltin(matches, left, type1, 0, right, type2, 0);
                }
            }
            return matches;

        case TK_LOGICAL_OR:
        case TK_LOGICAL_AND:
            return CExpr_MatchBuiltin(matches, left, TYPE(&stbool), 0, right, TYPE(&stbool), 0);
    }

    allowPtrDiffOnRight = allowPtrDiffOnLeft = allowPtrCV = allowMemberPtrs = allowEnum = FALSE;

    switch (token) {
        case '+':
        case '[':
            allowPtrDiffOnRight = allowPtrDiffOnLeft = TRUE;
            break;
        case '-':
            allowPtrDiffOnRight = allowPtrCV = TRUE;
            break;
        case ':':
        case TK_LOGICAL_EQ:
        case TK_LOGICAL_NE:
            allowMemberPtrs = TRUE;
        case '<':
        case '>':
        case TK_LESS_EQUAL:
        case TK_GREATER_EQUAL:
            allowEnum = allowPtrCV = TRUE;
            break;
        default:
            return matches;
    }

    leftList = CExpr_BuildConversionTypeList(left);
    rightList = CExpr_BuildConversionTypeList(right);
    ptrdiff = CABI_GetPtrDiffTType();

    for (scan = leftList;; scan = scan->next) {
        if (!scan) {
            if (!(scan = rightList)) {
                break;
            }
            rightList = NULL;
        }

        type1 = scan->type;
        if (IS_TYPE_REFERENCE(type1)) {
            type1 = TPTR_TARGET(type1);
        }

        switch (type1->type) {
            case TYPEENUM:
                if (allowEnum) {
                    matches = CExpr_MatchBuiltin(
                        matches, left, type1, scan->qual, right, type1, scan->qual
                    );
                }
                break;

            case TYPEPOINTER:
                if (allowPtrDiffOnRight) {
                    matches =
                        CExpr_MatchBuiltin(matches, left, type1, scan->qual, right, ptrdiff, 0);
                }
                if (allowPtrDiffOnLeft) {
                    matches =
                        CExpr_MatchBuiltin(matches, left, ptrdiff, 0, right, type1, scan->qual);
                }
                if (allowPtrCV) {
                    if (IS_TYPE_POINTER_ONLY(TPTR_TARGET(type1))) {
                        type2 = galloc(sizeof(TypePointer));
                        *TYPE_POINTER(type2) = *TYPE_POINTER(type1);
                        TPTR_QUAL(type2) |= Q_CONST | Q_VOLATILE;
                        matches = CExpr_MatchBuiltin(
                            matches, left, type2, scan->qual, right, type2, scan->qual
                        );
                    } else {
                        matches = CExpr_MatchBuiltin(
                            matches,
                            left,
                            type1,
                            Q_CONST | Q_VOLATILE,
                            right,
                            type1,
                            Q_CONST | Q_VOLATILE
                        );
                    }
                }
                break;

            case TYPEMEMBERPOINTER:
                if (allowMemberPtrs) {
                    matches = CExpr_MatchBuiltin(
                        matches, left, type1, scan->qual, right, type1, scan->qual
                    );
                }
                break;
            default:
                break;
        }
    }

    return matches;
}

static Boolean CExpr_MatchOperands(
    ENode* left,
    Type* leftType,
    UInt32 leftQual,
    ENode* right,
    Type* rightType,
    UInt32 rightQual,
    ImplicitConv* twoResults,
    Boolean leftStandardOnly
) {
    if (leftStandardOnly) {
        if (!CExpr_StandardConversionMatch(
                left, leftType, leftQual, TRUE, &twoResults[0].u.standard.standardConv
            )) {
            return FALSE;
        }
        twoResults[0].type = ICT_STANDARD;
    } else {
        if (!CExpr_ImplicitConversionMatch(left, leftType, leftQual, &twoResults[0])) {
            return FALSE;
        }
    }

    if (right) {
        if (!CExpr_ImplicitConversionMatch(right, rightType, rightQual, &twoResults[1])) {
            return FALSE;
        }
    }

    return TRUE;
}

Boolean CExpr_CondOperatorMatch(ENode* left, ENode* right, Conversion* conv) {
    Match* match;

    if ((match = CExpr_CheckBinaryBuiltin(NULL, left, ':', right))) {
        match = CExpr_FindBestMatch(match, 1, GetHashNameNode("operator?:"), NULL, NULL);
        CError_ASSERT(2931, !match->object);

        conv->expression = NULL;
        conv->left = CExpr_Convert(left, match->type, match->qual, FALSE, TRUE);
        conv->right = CExpr_Convert(right, match->type2, match->qual2, FALSE, TRUE);
        return TRUE;
    }

    return FALSE;
}

Boolean CExpr_OperatorMatch(short token, ENode* left, ENode* right, Conversion* conv) {
    HashNameNode* name;
    ENodeList* argExprs;
    BClassList* path;
    int hasArg;
    Match* matches;
    Object* object;
    Object* specialfunc;
    NameSpaceObjectList* list;
    Type* leftType;
    UInt32 leftQual;
    Type* rightType;
    UInt32 rightQual;
    NameResult pr;
    Match myMatch;
    NameSpaceObjectList myList;

    if (!IS_TYPE_CLASS(left->rtype)) {
        if (!IS_TYPE_ENUM(left->rtype)) {
            if (!right || !(IS_TYPE_CLASS(right->rtype) || IS_TYPE_ENUM(right->rtype))) {
                return FALSE;
            }
        }
    } else {
        CDecl_CompleteType(left->rtype);
    }

    name = CMangler_OperatorName(token);
    path = NULL;

    argExprs = lalloc(sizeof(ENodeList));
    argExprs->node = left;
    if (right) {
        argExprs->next = lalloc(sizeof(ENodeList));
        argExprs->next->node = right;
        argExprs->next->next = NULL;
        hasArg = 1;
    } else {
        argExprs->next = NULL;
        hasArg = 0;
    }

    matches = NULL;

    if (IS_TYPE_CLASS(left->rtype) &&
        CScope_FindClassMemberObject(TYPE_CLASS(left->rtype), &pr, name)) {
        if (token != '=' || (pr.basePath->type == left->rtype && !pr.basePath->next)) {
            if (pr.object) {
                myList.next = NULL;
                myList.object = pr.object;
                pr.objects = &myList;
            } else {
                CError_ASSERT(3009, pr.objects);
            }

            for (list = pr.objects; list; list = list->next) {
                object = OBJECT(list->object);
                if (object->otype == OT_OBJECT && IS_TYPE_FUNC(object->type)) {
                    if (TYPE_FUNC(object->type)->flags & FUNC_IS_TEMPL) {
                        specialfunc = object;
                        object = CTempl_DeduceFromFunctionCall(object, NULL, argExprs->next);
                        if (!object) {
                            continue;
                        }
                    } else {
                        specialfunc = NULL;
                    }

                    leftType = CExpr_GetImplictObjectParamType(object, &leftQual);
                    if (right) {
                        if (!CExpr_HasNParams(object, 1)) {
                            continue;
                        }
                        rightType = CExpr_GetParamType(object, 0, &rightQual);
                    } else {
                        if (!CExpr_HasNParams(object, 0)) {
                            continue;
                        }
                    }

                    if (CExpr_MatchOperands(
                            left,
                            leftType,
                            leftQual,
                            right,
                            rightType,
                            rightQual,
                            myMatch.conv,
                            TRUE
                        )) {
                        myMatch.object = object;
                        myMatch.specialfunc = specialfunc;
                        myMatch.next = matches;
                        matches = lalloc(sizeof(Match));
                        *matches = myMatch;
                        path = pr.basePath;
                    }
                }
            }
        }
    }

    if (CScope_FindNonClassObject(cscope_current, &pr, name)) {
        if (pr.object) {
            myList.next = NULL;
            myList.object = pr.object;
            pr.objects = &myList;
        }
    } else {
        pr.objects = NULL;
    }

    if (copts.arg_dep_lookup) {
        pr.objects = CScope_ArgumentDependentNameLookup(pr.objects, name, argExprs, TRUE);
    }

    for (list = pr.objects; list; list = list->next) {
        object = OBJECT(list->object);
        if (object->otype == OT_OBJECT && IS_TYPE_NONMETHOD(object->type)) {
            if (TYPE_FUNC(object->type)->flags & FUNC_IS_TEMPL) {
                specialfunc = object;
                object = CTempl_DeduceFromFunctionCall(object, NULL, argExprs);
                if (!object) {
                    continue;
                }
            } else {
                specialfunc = NULL;
            }

            leftType = CExpr_GetParamType(object, 0, &leftQual);
            if (right) {
                if (!CExpr_HasNParams(object, 2)) {
                    continue;
                }
                rightType = CExpr_GetParamType(object, 1, &rightQual);
            } else {
                if (!CExpr_HasNParams(object, 1)) {
                    continue;
                }
            }

            if (CExpr_MatchOperands(
                    left, leftType, leftQual, right, rightType, rightQual, myMatch.conv, FALSE
                )) {
                myMatch.object = object;
                myMatch.specialfunc = specialfunc;
                myMatch.next = matches;
                matches = lalloc(sizeof(Match));
                *matches = myMatch;
            }
        }
    }

    if (right) {
        matches = CExpr_CheckBinaryBuiltin(matches, left, token, right);
    } else {
        matches = CExpr_CheckUnaryBuiltin(matches, token, left);
    }

    if (matches) {
        conv->expression = NULL;
        conv->left = NULL;
        conv->right = NULL;

        matches = CExpr_FindBestMatch(matches, hasArg, name, NULL, argExprs);
        object = matches->object;
        if (!object) {
            if (IS_TYPE_CLASS(left->rtype)) {
                conv->left = CExpr_Convert(left, matches->type, matches->qual, FALSE, TRUE);
            } else {
                conv->left = left;
            }

            if (right) {
                if (IS_TYPE_CLASS(right->rtype)) {
                    conv->right = CExpr_Convert(right, matches->type2, matches->qual2, FALSE, TRUE);
                } else {
                    conv->right = right;
                }
            }

            return TRUE;
        }

        if (IS_TYPEFUNC_NONSTATIC_METHOD(TYPE_FUNC(object->type))) {
            CError_ASSERT(3125, path);
            left = CExpr_GenericFuncCall(
                path, argExprs->node, FALSE, object, NULL, NULL, argExprs->next, FALSE, FALSE, TRUE
            );
        } else {
            left = CExpr_GenericFuncCall(
                NULL, NULL, FALSE, object, NULL, NULL, argExprs, FALSE, FALSE, TRUE
            );
        }

        conv->expression = checkreference(left);
        return TRUE;
    }

    return FALSE;
}

static ENode* CExpr_ClassCopyInit(TypeClass* tclass, ENode* expr1, ENode* expr2) {
    Object* best;
    NameSpaceObjectList* list;
    ObjectList* objlist;
    ObjectList* objlistEntry;
    ENodeList* argExprs;
    FuncArg* arg;
    Type* type;
    Object* object;
    ImplicitConv bestConv;
    ImplicitConv conv;
    BClassList path;
    ConversionIterator convIter;

    best = NULL;
    objlist = NULL;

    for (list = CClass_Constructor(tclass); list; list = list->next) {
        object = OBJECT(list->object);
        if (object->otype == OT_OBJECT &&
            IS_TYPE_FUNC(object->type) &&
            !(object->qual & Q_EXPLICIT)) {
            arg = TYPE_FUNC(object->type)->args;

            CError_ASSERT(3199, arg);
            arg = arg->next;

            if (tclass->flags & CLASS_HAS_VBASES) {
                CError_ASSERT(3203, arg);
                arg = arg->next;
            }

            if (arg &&
                arg != &elipsis &&
                (!arg->next || arg->next->dexpr) &&
                CExpr_ImplicitConversionMatch(expr2, arg->type, arg->qual, &conv)) {
                if (!best || CExpr_IsBetterImplicitConv(&conv, &bestConv)) {
                    best = object;
                    bestConv = conv;
                    objlist = NULL;
                } else if (!CExpr_IsBetterImplicitConv(&bestConv, &conv)) {
                    objlistEntry = lalloc(sizeof(ObjectList));
                    objlistEntry->next = objlist;
                    objlistEntry->object = object;
                    objlist = objlistEntry;
                }
            }
        }
    }

    if (IS_TYPE_CLASS(expr2->rtype)) {
        CExpr_ConversionIteratorInit(&convIter, TYPE_CLASS(expr2->rtype));
        while ((object = CExpr_ConversionIteratorNext(&convIter))) {
            type = TYPE_FUNC(object->type)->functype;
            if (IS_TYPE_REFERENCE(type)) {
                type = TPTR_TARGET(type);
            }

            if (IS_TYPE_CLASS(type) &&
                (tclass == TYPE_CLASS(type) ||
                 CClass_IsBaseClass(tclass, TYPE_CLASS(type), NULL, FALSE, FALSE))) {
                CError_ASSERT(
                    3248,
                    TYPE_FUNC(object->type)->args &&
                        IS_TYPE_POINTER_ONLY(TYPE_FUNC(object->type)->args->type)
                );

                type = galloc(sizeof(TypePointer));
                *TYPE_POINTER(type) = *TYPE_POINTER(TYPE_FUNC(object->type)->args->type);
                TPTR_QUAL(type) = Q_REFERENCE;
                if (CExpr_ImplicitConversionMatch(
                        expr2, type, TYPE_FUNC(object->type)->args->qual, &conv
                    )) {
                    if (!best || CExpr_IsBetterImplicitConv(&conv, &bestConv)) {
                        best = object;
                        bestConv = conv;
                        objlist = NULL;
                    } else if (!CExpr_IsBetterImplicitConv(&bestConv, &conv)) {
                        objlistEntry = lalloc(sizeof(ObjectList));
                        objlistEntry->next = objlist;
                        objlistEntry->object = object;
                        objlist = objlistEntry;
                    }
                }
            }
        }
    }

    if (objlist) {
        CError_OverloadedFunctionError(best, objlist);
    }

    if (!best) {
        CError_Error(
            ERR_ILLEGAL_IMPLICIT_CONVERSION_FROM, expr2->rtype, ENODE_QUALS(expr2), tclass, 0
        );
        return expr1;
    }

    CError_ASSERT(3284, IS_TYPE_POINTER_ONLY(expr1->rtype));

    expr1 = makemonadicnode(expr1, EINDIRECT);
    expr1->rtype = TYPE(tclass);

    argExprs = lalloc(sizeof(ENodeList));
    argExprs->node = expr2;
    if (tclass->flags & CLASS_HAS_VBASES) {
        argExprs->next = lalloc(sizeof(ENodeList));
        argExprs->next->next = NULL;
        argExprs->next->node = intconstnode(TYPE(&stsignedshort), 1);
    } else {
        argExprs->next = NULL;
    }

    path.next = NULL;
    path.type = TYPE(tclass);
    expr1 =
        CExpr_GenericFuncCall(&path, expr1, FALSE, best, NULL, NULL, argExprs, FALSE, FALSE, TRUE);

    if (ENODE_IS2(expr1, EFUNCCALL, EFUNCCALLP)) {
        expr1->rtype = CDecl_NewPointerType(TYPE(tclass));
    }

    expr1 = makemonadicnode(expr1, EINDIRECT);
    expr1->rtype = TYPE(tclass);
    return expr1;
}
