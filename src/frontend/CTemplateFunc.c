#include "compiler/CTemplateFunc.h"
#include "compiler/CABI.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInline.h"
#include "compiler/CInt64.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CTemplateTools.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/templates.h"
#include "compiler/types.h"
#include <string.h>

static Boolean ctempl_conversion_deduction;
static UInt8 ctempl_explicitargs_nindex;
static int ctempl_explicitargs_num;
static Boolean ctempl_explicitargs_all;

static Boolean CTempl_DeduceTypeTemplDep(
    TypeTemplDep* type1,
    UInt32 qual1,
    Type* type2,
    UInt32 qual2,
    TemplArg* argArray,
    Boolean exactQuals
);
static Boolean CTempl_DeduceType1(
    Type* type1, UInt32 qual1, Type* type2, UInt32 qual2, TemplArg* argArray, Boolean exactQuals
);

static void CTemplFunc_SetupTypeDeduce(
    TypeDeduce* deduce, TemplateFunction* templ, TemplArg* args
) {
    memclrw(deduce, sizeof(TypeDeduce));
    if (templ->tfunc->nspace->theclass &&
        (templ->tfunc->nspace->theclass->flags & CLASS_IS_TEMPL)) {
        deduce->tmclass = TEMPL_CLASS(templ->tfunc->nspace->theclass);
    }
    deduce->params = templ->params;
    deduce->args = args;
}

Boolean CTempl_CanDeduceFunc(Object* object, TypeFunc* tfunc, TemplArg* args) {
    DeduceInfo info;
    int i;

    if (CTemplTool_InitDeduceInfo(&info, CTemplTool_GetFuncTempl(object)->params, args, FALSE)) {
        if (CTempl_DeduceType(object->type, 0, TYPE(tfunc), 0, info.args, FALSE, FALSE)) {
            for (i = 0; i < info.maxCount; i++) {
                if (!info.args[i].is_deduced) {
                    return FALSE;
                }
            }
            return TRUE;
        }
    }

    return FALSE;
}

static TemplFuncInstance* CTempl_GetCreateFuncInstance(
    Object* funcobj, TemplArg* args, Object* object2
) {
    TemplFuncInstance* inst;
    TemplateFunction* templ;
    Object* instobj;
    TemplParam* param;
    TemplArg* arg;
    TemplArg* last;
    short paramCount;
    short i;
    TypeDeduce deduce;

    templ = CTemplTool_GetFuncTempl(funcobj);

    paramCount = 0;
    param = templ->params;
    while (param) {
        paramCount++;
        param = param->next;
    }

    for (i = 1, arg = args; i < paramCount; i++, arg++) {
        arg->next = arg + 1;
    }
    arg->next = NULL;

    for (inst = templ->instances; inst; inst = inst->next) {
        if (CTemplTool_EqualArgs(inst->args, args)) {
            if (object2) {
                inst->object = object2;
            }
            return inst;
        }
    }

    inst = galloc(sizeof(TemplFuncInstance));
    memclrw(inst, sizeof(TemplFuncInstance));

    for (i = 0, arg = NULL; i < paramCount; i++) {
        if (arg) {
            last->next = galloc(sizeof(TemplArg));
            last = last->next;
        } else {
            last = galloc(sizeof(TemplArg));
            arg = last;
        }

        *last = *args;
        args++;
        last->next = NULL;

        if (last->pid.type == TPT_NONTYPE) {
            CError_ASSERT(114, last->data.paramdecl.expr);
            last->data.paramdecl.expr =
                CInline_CopyExpression(last->data.paramdecl.expr, CopyModeGlobal);
        }
    }

    inst->args = arg;
    inst->next = templ->instances;
    templ->instances = inst;

    if (!object2) {
        instobj = CParser_NewFunctionObject(NULL);
        instobj->access = funcobj->access;
        instobj->nspace = funcobj->nspace;
        instobj->sclass = funcobj->sclass;
        instobj->qual = funcobj->qual | Q_MANGLE_NAME;
        instobj->name = templ->name;
        instobj->u.func.inst = inst;

        CTemplFunc_SetupTypeDeduce(&deduce, templ, inst->args);
        if (funcobj->nspace->theclass && (funcobj->nspace->theclass->flags & CLASS_IS_TEMPL_INST)) {
            deduce.tmclass = TEMPL_CLASS_INST(funcobj->nspace->theclass)->templ;
            deduce.inst = TEMPL_CLASS_INST(funcobj->nspace->theclass);
        }

        instobj->type = CTemplTool_DeduceTypeCopy(&deduce, funcobj->type, &instobj->qual);
        inst->object = instobj;

        if (IS_TYPE_FUNC(instobj->type)) {
            TYPE_FUNC(instobj->type)->flags &= ~FUNC_DEFINED;
            if (TYPE_FUNC(instobj->type)->flags & FUNC_IS_CTOR) {
                FuncArg* funcarg;
                CError_ASSERT(152, TYPE_FUNC(instobj->type)->flags & FUNC_METHOD);
                CError_ASSERT(153, funcarg = TYPE_FUNC(instobj->type)->args);

                if (TYPE_METHOD(instobj->type)->theclass->flags & CLASS_HAS_VBASES) {
                    CError_ASSERT(156, funcarg = funcarg->next);
                }

                if (funcarg->next) {
                    Boolean ok;
                    ok = CDecl_CheckCtorIntegrity(
                        funcarg->next, TYPE_METHOD(instobj->type)->theclass
                    );
                    CError_ASSERT(162, ok);
                }
            }
        }

        if ((instobj->qual & Q_INLINE) && templ->stream.tokens) {
            CInline_AddTemplateFunctionAction(instobj, templ, inst);
        }

    } else {
        inst->object = object2;
    }

    return inst;
}

TemplFuncInstance* CTempl_CheckFuncInstance(
    Object* object1, TypeFunc* tfunc, TemplArg* args, Object* object2
) {
    TemplateFunction* templ;
    DeduceInfo info;
    int i;

    templ = CTemplTool_GetFuncTempl(object1);
    if (!CTemplTool_InitDeduceInfo(&info, templ->params, args, TRUE)) {
        return NULL;
    }

    if (!CTempl_DeduceType(object1->type, 0, TYPE(tfunc), 0, info.args, FALSE, FALSE)) {
        return NULL;
    }

    i = 0;
    while (i < info.maxCount) {
        if (!info.args[i++].is_deduced) {
            return NULL;
        }
    }

    return CTempl_GetCreateFuncInstance(object1, info.args, object2);
}

// NOTE: invented names: object1, tfunc, args, object2, unused
TemplFuncInstance* CTempl_DeduceFunc(
    Object* object1, TypeFunc* tfunc, TemplArg* args, Object* object2, Boolean unused
) {
    return CTempl_CheckFuncInstance(object1, tfunc, args, object2);
}

static Boolean CTempl_FuncIsAtLeastAsSpecialized(Object* func1, Object* func2) {
    int i;
    int count;
    FuncArg* arg1;
    FuncArg* arg2;
    Type* type1;
    Type* type2;
    DeduceInfo info;
    UInt32 qual1;
    UInt32 qual2;

    arg1 = TYPE_FUNC(func2->type)->args;
    if (IS_TYPEFUNC_NONSTATIC_METHOD(TYPE_FUNC(func2->type))) {
        arg1 = arg1->next;
    }

    count = 0;
    while (1) {
        if (!arg1) {
            break;
        }
        CError_ASSERT(233, arg1->type != &stvoid);
        if (arg1 == &elipsis) {
            break;
        }
        if (arg1 == &oldstyle) {
            break;
        }
        arg1 = arg1->next;
        count++;
    }

    if (!CTemplTool_InitDeduceInfo(&info, CTemplTool_GetFuncTempl(func2)->params, NULL, FALSE)) {
        return FALSE;
    }

    arg1 = TYPE_FUNC(func1->type)->args;
    if (IS_TYPEFUNC_NONSTATIC_METHOD(TYPE_FUNC(func1->type))) {
        arg1 = arg1->next;
    }

    arg2 = TYPE_FUNC(func2->type)->args;
    if (IS_TYPEFUNC_NONSTATIC_METHOD(TYPE_FUNC(func2->type))) {
        arg2 = arg2->next;
    }

    for (i = count; i > 0; i--) {
        if (!arg1) {
            break;
        }
        if (arg1 == &elipsis) {
            break;
        }
        if (arg1 == &oldstyle) {
            break;
        }

        CError_ASSERT(257, arg2);

        type1 = arg1->type;
        qual1 = arg1->qual & (Q_CONST | Q_VOLATILE);
        qual2 = arg2->qual & (Q_CONST | Q_VOLATILE);
        type2 = arg2->type;

        if (!CTemplTool_IsTemplateArgumentDependentType(type2)) {
            if (IS_TYPE_REFERENCE(type1)) {
                type1 = TPTR_TARGET(type1);
            }
            type1 = CParser_RemoveTopMostQualifiers(type1, &qual1);

            if (IS_TYPE_REFERENCE(type2)) {
                type2 = TPTR_TARGET(type2);
            }
            type2 = CParser_RemoveTopMostQualifiers(type2, &qual2);

            if (!is_typesame(type1, type2) || qual1 != qual2) {
                return FALSE;
            }
        } else {
            if (!CTempl_DeduceType(type2, qual2, type1, qual1, info.args, FALSE, FALSE)) {
                return FALSE;
            }
        }

        arg1 = arg1->next;
        arg2 = arg2->next;
    }

    return TRUE;
}

Boolean CTempl_FuncIsMoreSpecialized(Object* object1, Object* object2) {
    return CTempl_FuncIsAtLeastAsSpecialized(object1, object2) &&
        !CTempl_FuncIsAtLeastAsSpecialized(object2, object1);
}

// NOTE: invented name: argcount (nothing reads the parameter; the count of candidates is a local)
Object* CTempl_PartialOrdering(Object* object, ObjectList* list, int argcount) {
    int j;
    Object** array;
    ObjectList* scan;
    int i;
    Object* arrayBuffer[16];
    int count;

    for (count = 1, scan = list; scan; scan = scan->next) {
        if (IS_TEMPL_FUNC(scan->object->type)) {
            count++;
        }
    }

    if (count > 16) {
        array = lalloc(sizeof(Object*) * count);
    } else {
        array = arrayBuffer;
    }

    array[0] = object;
    for (i = 1, scan = list; scan; scan = scan->next) {
        if (IS_TEMPL_FUNC(scan->object->type)) {
            array[i++] = scan->object;
        }
    }

    for (i = 0; i < count; i++) {
        if (array[i]) {
            for (j = 0; j < count; j++) {
                if (array[j] && i != j && CTempl_FuncIsMoreSpecialized(array[i], array[j])) {
                    array[j] = NULL;
                }
            }
        }
    }

    for (i = 0, object = NULL; i < count; i++) {
        if (array[i]) {
            if (object) {
                return NULL;
            }
            object = array[i];
        }
    }

    return object;
}

int CTempl_GetTemplateArgumentExpressionIndex(TemplArg* arg) {
    CError_ASSERT(531, arg->data.paramdecl.expr);

    if (ENODE_IS(arg->data.paramdecl.expr, ETEMPLDEP) &&
        arg->data.paramdecl.expr->data.templdep.subtype == TDE_PARAM) {
        return arg->data.paramdecl.expr->data.templdep.u.pid.index;
    }

    return -1;
}

static Type* CTempl_GetSpecifiedType(
    TemplateFunction* templ, Type* type, UInt32 qual, TemplArg* args, UInt32* resultQual
) {
    TypeDeduce deduce;

    CTemplFunc_SetupTypeDeduce(&deduce, templ, args);
    *resultQual = qual;
    return CTemplTool_DeduceTypeCopy(&deduce, type, resultQual);
}

static Boolean CTempl_DeduceTemplateArgs(TemplArg* args1, TemplArg* args2, TemplArg* argArray) {
    int index;

    while (1) {
        if (!args1) {
            return args2 == NULL;
        }
        if (!args2) {
            return FALSE;
        }

        if (args1->pid.type != args2->pid.type) {
            return FALSE;
        }

        switch (args1->pid.type) {
            case TPT_TYPE:
                if (!CTempl_DeduceType1(
                        args1->data.typeparam.type,
                        args1->data.typeparam.qual,
                        args2->data.typeparam.type,
                        args2->data.typeparam.qual,
                        argArray,
                        TRUE
                    )) {
                    return FALSE;
                }
                break;

            case TPT_NONTYPE:
                if (CTemplTool_IsTemplateArgumentDependentExpression(args1->data.paramdecl.expr)) {
                    if ((index = CTempl_GetTemplateArgumentExpressionIndex(args1)) < 0) {
                        return TRUE;
                    }

                    if (argArray[index].is_deduced) {
                        if (!CTemplTool_EqualExprTypes(
                                args2->data.paramdecl.expr, argArray[index].data.paramdecl.expr
                            )) {
                            return FALSE;
                        }
                    } else {
                        argArray[index].data.paramdecl.expr = args2->data.paramdecl.expr;
                        argArray[index].pid.type = TPT_NONTYPE;
                        argArray[index].is_deduced = TRUE;
                    }
                } else {
                    if (!CTemplTool_EqualExprTypes(
                            args2->data.paramdecl.expr, args1->data.paramdecl.expr
                        )) {
                        return FALSE;
                    }
                }
                break;

            case TPT_TEMPLATE:
                if (!IS_TEMPL_CLASS(args2->data.ttargtype)) {
                    return FALSE;
                }

                index = args1->pid.index;
                if (argArray[index].is_deduced) {
                    if (args2->data.ttargtype != argArray[index].data.ttargtype) {
                        return FALSE;
                    }
                } else {
                    argArray[index].data.ttargtype = args2->data.ttargtype;
                    argArray[index].pid.type = TPT_TEMPLATE;
                    argArray[index].is_deduced = TRUE;
                }
                break;

            default:
                CError_FATAL(642);
        }

        args1 = args1->next;
        args2 = args2->next;
    }
}

static Boolean CTempl_DeduceTemplDepTemplate(
    TemplClass* templ1, TemplArg* args1, TemplClass* templ2, TemplArg* args2, TemplArg* argArray
) {
    if (templ1 != templ2) {
        return FALSE;
    }

    return CTempl_DeduceTemplateArgs(args2, args1, argArray);
}

static Boolean CTempl_DeduceTemplateType(
    TemplClass* templ, TemplArg* args, TemplClassInst* inst, TemplArg* argArray
) {
    TemplClassInst* scan;

    for (scan = templ->instances; scan; scan = scan->next) {
        if (scan == inst) {
            return CTempl_DeduceTemplateArgs(
                args, scan->oargs ? scan->oargs : scan->inst_args, argArray
            );
        }
    }

    return FALSE;
}

// NOTE: invented names: templ, args, inst, argArray, searchBases
static Boolean CTempl_DeduceTemplateTypeBase(
    TemplClass* templ, TemplArg* args, TemplClassInst* inst, TemplArg* argArray, Boolean searchBases
) {
    ClassList* base;

    if (CTempl_DeduceTemplateType(templ, args, inst, argArray)) {
        return TRUE;
    }

    if (searchBases) {
        for (base = inst->theclass.bases; base; base = base->next) {
            if (CTempl_DeduceTemplateType(templ, args, TEMPL_CLASS_INST(base->base), argArray)) {
                ctempl_conversion_deduction = TRUE;
                return TRUE;
            }
        }

        for (base = inst->theclass.bases; base; base = base->next) {
            if (CTempl_DeduceTemplateTypeBase(
                    templ, args, TEMPL_CLASS_INST(base->base), argArray, TRUE
                )) {
                ctempl_conversion_deduction = TRUE;
                return TRUE;
            }
        }
    }

    return FALSE;
}

static Boolean CTempl_DeduceQualTemplate(
    TypeTemplDep* type1, TemplArg* args1, Type* type2, TemplArg* argArray
) {
    int index;

    if (type1->dtype == TEMPLDEP_ARGUMENT && type1->u.pid.type == TPT_TEMPLATE) {
        index = type1->u.pid.index;
        if (IS_TEMPL_CLASS_INST(type2)) {
            if (argArray[index].is_deduced) {
                if (argArray[index].data.ttargtype != TYPE(TEMPL_CLASS_INST(type2)->templ)) {
                    return FALSE;
                }
            } else {
                argArray[index].data.ttargtype = TYPE(TEMPL_CLASS_INST(type2)->templ);
                argArray[index].pid.type = TPT_TEMPLATE;
                argArray[index].is_deduced = TRUE;
            }

            return CTempl_DeduceTemplateArgs(args1, TEMPL_CLASS_INST(type2)->inst_args, argArray);
        }

        if (IS_TYPE_TEMPLATE(type2)) {
            if (TYPE_TEMPLATE(type2)->dtype == TEMPLDEP_TEMPLATE) {
                if (argArray[index].is_deduced) {
                    if (argArray[index].data.ttargtype !=
                        TYPE(TYPE_TEMPLATE(type2)->u.templ.templ)) {
                        return FALSE;
                    }
                } else {
                    argArray[index].data.ttargtype = TYPE(TYPE_TEMPLATE(type2)->u.templ.templ);
                    argArray[index].pid.type = TPT_TEMPLATE;
                    argArray[index].is_deduced = TRUE;
                }

                return CTempl_DeduceTemplateArgs(
                    args1, TYPE_TEMPLATE(type2)->u.templ.args, argArray
                );
            }

            if (TYPE_TEMPLATE(type2)->dtype == TEMPLDEP_QUALTEMPL) {
                return CTempl_DeduceTypeTemplDep(
                           type1, 0, TYPE(TYPE_TEMPLATE(type2)->u.qualtempl.type), 0, argArray, TRUE
                       ) &&
                    CTempl_DeduceTemplateArgs(
                           args1, TYPE_TEMPLATE(type2)->u.qualtempl.args, argArray
                    );
            }
        }
    }

    return FALSE;
}

// NOTE: invented names: type1, qual1, type2, qual2, argArray, exactQuals, deducedType, deducedQual,
// modified
static Boolean CTempl_DeduceTypeTemplDep(
    TypeTemplDep* type1,
    UInt32 qual1,
    Type* type2,
    UInt32 qual2,
    TemplArg* argArray,
    Boolean exactQuals
) {
    Type* deducedType;
    UInt32 deducedQual;
    short index;
    Boolean modified;
    UInt32 modqual;

    switch (type1->dtype) {
        case TEMPLDEP_ARGUMENT:
            index = type1->u.pid.index;
            deducedType = type2;
            if (type1->u.pid.type == TPT_TEMPLATE) {
                if (argArray[index].is_deduced) {
                    if (!is_typesame(deducedType, argArray[index].data.ttargtype)) {
                        return FALSE;
                    }
                } else {
                    argArray[index].data.ttargtype = type2;
                    argArray[index].pid.type = TPT_TEMPLATE;
                    argArray[index].is_deduced = TRUE;
                }
                return TRUE;
            }

            switch (type2->type) {
                case TYPEPOINTER:
                    deducedQual = qual2;
                    modified = FALSE;
                    modqual = qual2 = TPTR_QUAL(type2);

                    if ((modqual & Q_CONST) && (qual1 & Q_CONST)) {
                        modqual &= ~Q_CONST;
                        modified = TRUE;
                    }
                    if ((modqual & Q_VOLATILE) && (qual1 & Q_VOLATILE)) {
                        modqual &= ~Q_VOLATILE;
                        modified = TRUE;
                    }

                    if (modified) {
                        deducedType = galloc(sizeof(TypePointer));
                        *TYPE_POINTER(deducedType) = *TYPE_POINTER(type2);
                        TPTR_QUAL(deducedType) = modqual;
                    }

                    if (exactQuals && (qual2 | modqual) != (qual1 | modqual)) {
                        return FALSE;
                    }

                    break;

                case TYPEMEMBERPOINTER:
                    deducedQual = qual2;
                    modified = FALSE;
                    qual2 = TYPE_MEMBER_POINTER(type2)->qual;
                    modqual = qual2;

                    if ((modqual & Q_CONST) && (qual1 & Q_CONST)) {
                        modqual &= ~Q_CONST;
                        modified = TRUE;
                    }
                    if ((modqual & Q_VOLATILE) && (qual1 & Q_VOLATILE)) {
                        modqual &= ~Q_VOLATILE;
                        modified = TRUE;
                    }

                    if (modified) {
                        deducedType = galloc(sizeof(TypeMemberPointer));
                        *TYPE_MEMBER_POINTER(deducedType) = *TYPE_MEMBER_POINTER(type2);
                        TYPE_MEMBER_POINTER(deducedType)->qual = modqual;
                    }

                    if (exactQuals && (qual2 | modqual) != (qual1 | modqual)) {
                        return FALSE;
                    }

                    break;

                default:
                    deducedQual = 0;
                    if ((qual2 & Q_CONST) && !(qual1 & Q_CONST)) {
                        deducedQual |= Q_CONST;
                    }
                    if ((qual2 & Q_VOLATILE) && !(qual1 & Q_VOLATILE)) {
                        deducedQual |= Q_VOLATILE;
                    }

                    if (exactQuals && (qual2 | deducedQual) != (qual1 | deducedQual)) {
                        return FALSE;
                    }
            }

            if (argArray[index].is_deduced) {
                if (!is_typesame(deducedType, argArray[index].data.typeparam.type) ||
                    deducedQual != argArray[index].data.typeparam.qual) {
                    return FALSE;
                }
            } else {
                argArray[index].data.typeparam.type = deducedType;
                argArray[index].data.typeparam.qual = deducedQual;
                argArray[index].pid.type = TPT_TYPE;
                argArray[index].is_deduced = TRUE;
            }
            return TRUE;

        case TEMPLDEP_QUALNAME:
            return TRUE;

        case TEMPLDEP_TEMPLATE:
            if (IS_TYPE_CLASS(type2)) {
                return CTempl_DeduceTemplateTypeBase(
                    type1->u.templ.templ,
                    type1->u.templ.args,
                    TEMPL_CLASS_INST(type2),
                    argArray,
                    FALSE
                );
            }

            if (IS_TYPE_TEMPLATE(type2) && TYPE_TEMPLATE(type2)->dtype == TEMPLDEP_TEMPLATE) {
                return CTempl_DeduceTemplDepTemplate(
                    TYPE_TEMPLATE(type2)->u.templ.templ,
                    TYPE_TEMPLATE(type2)->u.templ.args,
                    type1->u.templ.templ,
                    type1->u.templ.args,
                    argArray
                );
            }

            return FALSE;

        case TEMPLDEP_ARRAY:
            if (IS_TYPE_ARRAY(type2)) {
                SInt32 elements;

                if (!CTempl_DeduceType1(
                        type1->u.array.type, qual1, TPTR_TARGET(type2), qual2, argArray, exactQuals
                    )) {
                    return FALSE;
                }

                if (ENODE_IS(type1->u.array.index, EINTCONST)) {
                    CError_ASSERT(963, TPTR_TARGET(type2)->size);
                    elements = type2->size / TPTR_TARGET(type2)->size;
                    return elements == (SInt32)CInt64_GetULong(&type1->u.array.index->data.intval);
                }

                if (!ENODE_IS(type1->u.array.index, ETEMPLDEP) ||
                    type1->u.array.index->data.templdep.subtype != TDE_PARAM) {
                    return TRUE;
                }

                CError_ASSERT(972, TPTR_TARGET(type2)->size);

                elements = type2->size / TPTR_TARGET(type2)->size;
                index = type1->u.array.index->data.templdep.u.pid.index;
                if (argArray[index].is_deduced) {
                    CError_ASSERT(978, ENODE_IS(argArray[index].data.paramdecl.expr, EINTCONST));
                    return elements ==
                        (SInt32)CInt64_GetULong(&argArray[index].data.paramdecl.expr->data.intval);
                } else {
                    argArray[index].data.paramdecl.expr =
                        intconstnode(CABI_GetPtrDiffTType(), elements);
                    argArray[index].pid.type = TPT_NONTYPE;
                    argArray[index].is_deduced = TRUE;
                    return TRUE;
                }
            } else if (IS_TYPE_TEMPLATE(type2) && TYPE_TEMPLATE(type2)->dtype == TEMPLDEP_ARRAY) {
                if (!CTempl_DeduceType1(
                        type1->u.array.type,
                        qual1,
                        TYPE_TEMPLATE(type2)->u.array.type,
                        qual2,
                        argArray,
                        exactQuals
                    )) {
                    return FALSE;
                }

                if (ENODE_IS(type1->u.array.index, EINTCONST)) {
                    return ENODE_IS(TYPE_TEMPLATE(type2)->u.array.index, EINTCONST) &&
                        CInt64_Equal(
                               type1->u.array.index->data.intval,
                               TYPE_TEMPLATE(type2)->u.array.index->data.intval
                        );
                }

                if (!ENODE_IS(type1->u.array.index, ETEMPLDEP) ||
                    type1->u.array.index->data.templdep.subtype != TDE_PARAM) {
                    return TRUE;
                }

                index = type1->u.array.index->data.templdep.u.pid.index;
                if (argArray[index].is_deduced) {
                    return CTemplTool_EqualExprTypes(
                        TYPE_TEMPLATE(type2)->u.array.index, argArray[index].data.paramdecl.expr
                    );
                } else {
                    argArray[index].data.paramdecl.expr = TYPE_TEMPLATE(type2)->u.array.index;
                    argArray[index].pid.type = TPT_NONTYPE;
                    argArray[index].is_deduced = TRUE;
                    return TRUE;
                }
            }

            return FALSE;

        case TEMPLDEP_QUALTEMPL:
            return CTempl_DeduceQualTemplate(
                type1->u.qualtempl.type, type1->u.qualtempl.args, type2, argArray
            );

        case TEMPLDEP_BITFIELD:
        default:
            CError_FATAL(1024);
            return TRUE;
    }
}

static Boolean CTempl_DeduceFuncType(TypeFunc* tfunc1, TypeFunc* tfunc2, TemplArg* argArray) {
    FuncArg* arg1;
    FuncArg* arg2;
    Type* type2;
    Type* type1;
    UInt32 qual2;
    UInt32 qual1;

    arg1 = tfunc1->args;
    arg2 = tfunc2->args;

    if (IS_TYPEFUNC_NONSTATIC_METHOD(tfunc1) && !IS_TYPEFUNC_METHOD(tfunc2)) {
        CError_ASSERT(1047, arg1);
        arg1 = arg1->next;
    }

    while (1) {
        if (arg1 == NULL) {
            return arg2 == NULL;
        }
        if (arg1 == &oldstyle) {
            return arg2 == &oldstyle;
        }
        if (arg1 == &elipsis) {
            return arg2 == &elipsis;
        }

        if (arg2 == NULL || arg2 == &oldstyle || arg2 == &elipsis) {
            return FALSE;
        }

        qual2 = arg2->qual;
        type2 = CParser_RemoveTopMostQualifiers(arg2->type, &qual2);

        qual1 = arg1->qual;
        type1 = CParser_RemoveTopMostQualifiers(arg1->type, &qual1);

        if (!CTempl_DeduceType1(type1, qual1, type2, qual2, argArray, TRUE)) {
            return FALSE;
        }

        arg1 = arg1->next;
        arg2 = arg2->next;
    }
}

// NOTE: invented names: type1, qual1, type2, qual2, argArray, exactQuals
static Boolean CTempl_DeduceType1(
    Type* type1, UInt32 qual1, Type* type2, UInt32 qual2, TemplArg* argArray, Boolean exactQuals
) {
    while (1) {
        switch (type1->type) {
            case TYPETEMPLATE:
                return CTempl_DeduceTypeTemplDep(
                    TYPE_TEMPLATE(type1), qual1, type2, qual2, argArray, exactQuals
                );

            case TYPEVOID:
            case TYPEINT:
            case TYPEFLOAT:
            case TYPEENUM:
            case TYPESTRUCT:
            case TYPECLASS:
                return type1 == type2;

            case TYPEMEMBERPOINTER:
                if (type1->type != type2->type ||
                    TYPE_MEMBER_POINTER(type1)->qual != TYPE_MEMBER_POINTER(type2)->qual) {
                    return FALSE;
                }
                if (!CTempl_DeduceType1(
                        TYPE_MEMBER_POINTER(type1)->ty1,
                        qual1,
                        TYPE_MEMBER_POINTER(type2)->ty1,
                        qual2,
                        argArray,
                        exactQuals
                    )) {
                    return FALSE;
                }
                type1 = TYPE_MEMBER_POINTER(type1)->ty2;
                type2 = TYPE_MEMBER_POINTER(type2)->ty2;
                continue;

            case TYPEARRAY:
                if (type2->type != TYPEARRAY) {
                    return FALSE;
                }
                type1 = TPTR_TARGET(type1);
                type2 = TPTR_TARGET(type2);
                continue;

            case TYPEPOINTER:
                if (type2->type != TYPEPOINTER || TPTR_QUAL(type1) != TPTR_QUAL(type2)) {
                    return FALSE;
                }
                type1 = TPTR_TARGET(type1);
                type2 = TPTR_TARGET(type2);
                continue;

            case TYPEFUNC:
                if (type1->type != type2->type) {
                    return FALSE;
                }

                if (!CTempl_DeduceType1(
                        TYPE_FUNC(type1)->functype,
                        TYPE_FUNC(type1)->qual,
                        TYPE_FUNC(type2)->functype,
                        TYPE_FUNC(type2)->qual,
                        argArray,
                        exactQuals
                    )) {
                    return FALSE;
                }
                return CTempl_DeduceFuncType(TYPE_FUNC(type1), TYPE_FUNC(type2), argArray);

            default:
                CError_FATAL(1126);
        }
    }
}

static Boolean CTempl_DeduceType2(
    Type* type1, UInt32 qual1, Type* type2, UInt32 qual2, TemplArg* argArray
) {
    while (1) {
        switch (type1->type) {
            case TYPEPOINTER:
                if (type2->type != TYPEPOINTER) {
                    return FALSE;
                }
                if (!CParser_IsSameOrMoreCVQualified(TPTR_QUAL(type2), TPTR_QUAL(type1))) {
                    return FALSE;
                }
                type1 = TPTR_TARGET(type1);
                type2 = TPTR_TARGET(type2);
                continue;

            case TYPEMEMBERPOINTER:
                if (type2->type != TYPEMEMBERPOINTER) {
                    return FALSE;
                }
                if (!CParser_IsSameOrMoreCVQualified(
                        TYPE_MEMBER_POINTER(type2)->qual, TYPE_MEMBER_POINTER(type1)->qual
                    )) {
                    return FALSE;
                }
                if (!CTempl_DeduceType2(
                        TYPE_MEMBER_POINTER(type1)->ty1,
                        qual1,
                        TYPE_MEMBER_POINTER(type2)->ty1,
                        qual2,
                        argArray
                    )) {
                    return FALSE;
                }
                type1 = TYPE_MEMBER_POINTER(type1)->ty2;
                type2 = TYPE_MEMBER_POINTER(type2)->ty2;
                continue;

            default:
                if (!CParser_IsSameOrMoreCVQualified(qual2, qual1)) {
                    return FALSE;
                }
                return CTempl_DeduceType1(type1, qual2, type2, qual2, argArray, FALSE);
        }
    }
}

static Boolean CTempl_DeduceType3(
    Type* type1, UInt32 qual1, Type* type2, UInt32 qual2, TemplArg* argArray
) {
    Boolean throughPointer = FALSE; // NOTE: invented name

    while (1) {
        switch (type1->type) {
            case TYPEPOINTER:
                throughPointer = TRUE;
                if (type2->type != TYPEPOINTER) {
                    return FALSE;
                }
                if (!CParser_IsSameOrMoreCVQualified(TPTR_QUAL(type2), TPTR_QUAL(type1))) {
                    return FALSE;
                }
                type1 = TPTR_TARGET(type1);
                type2 = TPTR_TARGET(type2);
                continue;

            case TYPEMEMBERPOINTER:
                throughPointer = TRUE;
                if (type2->type != TYPEMEMBERPOINTER) {
                    return FALSE;
                }
                if (!CParser_IsSameOrMoreCVQualified(
                        TYPE_MEMBER_POINTER(type2)->qual, TYPE_MEMBER_POINTER(type1)->qual
                    )) {
                    return FALSE;
                }
                if (!CTempl_DeduceType2(
                        TYPE_MEMBER_POINTER(type1)->ty1,
                        qual1,
                        TYPE_MEMBER_POINTER(type2)->ty1,
                        qual2,
                        argArray
                    )) {
                    return FALSE;
                }
                type1 = TYPE_MEMBER_POINTER(type1)->ty2;
                type2 = TYPE_MEMBER_POINTER(type2)->ty2;
                continue;

            default:
                if (!throughPointer) {
                    return FALSE;
                }
                if (!CParser_IsSameOrMoreCVQualified(qual2, qual1)) {
                    return FALSE;
                }
                return CTempl_DeduceType1(type1, qual2, type2, qual2, argArray, FALSE);
        }
    }
}

static Boolean CTempl_DeduceType4(
    Type* type1, UInt32 qual1, Type* type2, UInt32 qual2, TemplArg* argArray
) {
    if (IS_TYPE_POINTER_ONLY(type1)) {
        if (!IS_TYPE_POINTER_ONLY(type2)) {
            return FALSE;
        }

        if (!CParser_IsSameOrMoreCVQualified(TPTR_QUAL(type1), TPTR_QUAL(type2))) {
            return FALSE;
        }

        type1 = TPTR_TARGET(type1);
        type2 = TPTR_TARGET(type2);
    }

    if (IS_TYPE_TEMPLATE(type1) &&
        TYPE_TEMPLATE(type1)->dtype == TEMPLDEP_TEMPLATE &&
        IS_TYPE_CLASS(type2)) {
        if (CParser_IsSameOrMoreCVQualified(qual1, qual2)) {
            return CTempl_DeduceTemplateTypeBase(
                TYPE_TEMPLATE(type1)->u.templ.templ,
                TYPE_TEMPLATE(type1)->u.templ.args,
                TEMPL_CLASS_INST(type2),
                argArray,
                TRUE
            );
        } else {
            return FALSE;
        }
    }

    return FALSE;
}

// NOTE: invented names: fromCall, fromConversion, wasReference, exactQuals
// fromCall, fromConversion: deducing from a call's argument ([temp.deduct.call]) or from a
// conversion function's type ([temp.deduct.conv])
Boolean CTempl_DeduceType(
    Type* type1,
    UInt32 qual1,
    Type* type2,
    UInt32 qual2,
    TemplArg* argArray,
    Boolean fromCall,
    Boolean fromConversion
) {
    // type1 was a reference; the qualifiers must match exactly
    Boolean wasReference;
    Boolean exactQuals;

    wasReference = FALSE;
    exactQuals = TRUE;

    if (fromCall || fromConversion) {
        if (IS_TYPE_REFERENCE(type1)) {
            type1 = TPTR_TARGET(type1);
            wasReference = TRUE;
        } else {
            switch (type2->type) {
                case TYPEFUNC:
                    type2 = CDecl_NewPointerType(type2);
                    break;
                case TYPEARRAY:
                    type2 = CDecl_NewPointerType(TPTR_TARGET(type2));
                    break;
                default:
                    break;
            }

            type1 = CParser_RemoveTopMostQualifiers(type1, &qual1);
            type2 = CParser_RemoveTopMostQualifiers(type2, &qual2);
        }
        exactQuals = FALSE;
    }

    if (CTempl_DeduceType1(type1, qual1, type2, qual2, argArray, exactQuals)) {
        return TRUE;
    }

    if (fromCall || fromConversion) {
        if (wasReference && CTempl_DeduceType2(type1, qual1, type2, qual2, argArray)) {
            return TRUE;
        }
        if (CTempl_DeduceType3(type1, qual1, type2, qual2, argArray)) {
            return TRUE;
        }
        if (fromCall && CTempl_DeduceType4(type1, qual1, type2, qual2, argArray)) {
            return TRUE;
        }
    }

    return FALSE;
}

static Boolean CTempl_TypeNeedsDeduction(Type* type) {
    FuncArg* arg;
    Boolean result;

    while (1) {
        switch (type->type) {
            case TYPETEMPLATE:
                switch (TYPE_TEMPLATE(type)->dtype) {
                    case TEMPLDEP_ARGUMENT:
                        if (TYPE_TEMPLATE(type)->u.pid.index >= ctempl_explicitargs_num ||
                            TYPE_TEMPLATE(type)->u.pid.nindex != ctempl_explicitargs_nindex) {
                            ctempl_explicitargs_all = FALSE;
                        }
                        return TRUE;

                    case TEMPLDEP_QUALNAME:
                        CTempl_TypeNeedsDeduction(TYPE(TYPE_TEMPLATE(type)->u.qual.type));
                        return TRUE;

                    case TEMPLDEP_TEMPLATE:
                        ctempl_explicitargs_all = FALSE;
                        return TRUE;

                    case TEMPLDEP_ARRAY:
                        CTempl_TypeNeedsDeduction(TYPE_TEMPLATE(type)->u.array.type);
                        ctempl_explicitargs_all = FALSE;
                        return TRUE;

                    case TEMPLDEP_QUALTEMPL:
                        CTempl_TypeNeedsDeduction(TYPE(TYPE_TEMPLATE(type)->u.qualtempl.type));
                        ctempl_explicitargs_all = FALSE;
                        return TRUE;

                    case TEMPLDEP_BITFIELD:
                    default:
                        CError_FATAL(1359);
                }

            case TYPEVOID:
            case TYPEINT:
            case TYPEFLOAT:
            case TYPEENUM:
            case TYPESTRUCT:
            case TYPECLASS:
                return FALSE;

            case TYPEMEMBERPOINTER:
                result = FALSE;
                if (CTempl_TypeNeedsDeduction(TYPE_MEMBER_POINTER(type)->ty1)) {
                    result = TRUE;
                }
                if (CTempl_TypeNeedsDeduction(TYPE_MEMBER_POINTER(type)->ty2)) {
                    result = TRUE;
                }
                return result;

            case TYPEPOINTER:
            case TYPEARRAY:
                type = TPTR_TARGET(type);
                continue;

            case TYPEFUNC:
                result = FALSE;
                for (arg = TYPE_FUNC(type)->args; arg && arg != &elipsis; arg = arg->next) {
                    if (CTempl_TypeNeedsDeduction(arg->type)) {
                        result = TRUE;
                    }
                }

                if (CTempl_TypeNeedsDeduction(TYPE_FUNC(type)->functype)) {
                    result = TRUE;
                }
                return result;

            default:
                CError_FATAL(1391);
        }
    }
}

static Boolean CTempl_MatchTemplFunc(
    Object* object, DeduceInfo* info, FuncArg* args, ENodeList* exprs, ArgMatch* best
) {
    ArgMatch match;
    int i;

    memclrw(&match, sizeof(match));

    while (1) {
        if (!args || args->type == &stvoid) {
            if (!exprs) {
                break;
            }
            return FALSE;
        }

        if (args == &elipsis) {
            break;
        }
        if (args == &oldstyle) {
            break;
        }

        if (!exprs) {
            if (args->dexpr) {
                break;
            }
            return FALSE;
        }

        ctempl_explicitargs_nindex = info->nindex;
        ctempl_explicitargs_num = info->count;
        ctempl_explicitargs_all = TRUE;

        if (CTempl_TypeNeedsDeduction(args->type)) {
            if (!ctempl_explicitargs_all) {
                UInt32 cv2;
                UInt32 cv1;
                Type* type2;
                Type* type1;
                UInt32 qual2;
                UInt32 qual1;

                type1 = args->type;
                qual1 = args->qual & (Q_CONST | Q_VOLATILE);

                type2 = exprs->node->rtype;
                qual2 = ENODE_QUALS(exprs->node);

                if (IS_TYPE_REFERENCE(type1)) {
                    type1 = TPTR_TARGET(type1);
                    cv1 = CParser_GetCVTypeQualifiers(type1, qual1);
                    cv2 = CParser_GetCVTypeQualifiers(type2, qual2);
                    if ((!(cv1 & Q_CONST) && (cv2 & Q_CONST)) ||
                        (!(cv1 & Q_VOLATILE) && (cv2 & Q_VOLATILE))) {
                        return FALSE;
                    }

                    if (!(cv1 & Q_CONST) && !CExpr_IsLValue(exprs->node)) {
                        return FALSE;
                    }
                }

                type1 = CParser_RemoveTopMostQualifiers(type1, &qual1);
                type2 = CParser_RemoveTopMostQualifiers(type2, &qual2);

                if (ENODE_IS(exprs->node, EOBJREF) &&
                    IS_TEMPL_FUNC(exprs->node->data.objref->type)) {
                    return FALSE;
                }

                if (ENODE_IS(exprs->node, EOBJLIST)) {
                    NameSpaceObjectList* list;

                    for (list = exprs->node->data.objlist.list; list; list = list->next) {
                        if (list->object->otype == OT_OBJECT &&
                            IS_TEMPL_FUNC(OBJECT(list->object)->type)) {
                            break;
                        }
                    }

                    if (list) {
                        return FALSE;
                    }

                    if (IS_TYPE_FUNC(type2)) {
                        type2 = CDecl_NewPointerType(type2);
                    }
                }

                ctempl_conversion_deduction = FALSE;
                if (!CTempl_DeduceType(type1, qual1, type2, qual2, info->args, TRUE, FALSE)) {
                    return FALSE;
                }

                if (ctempl_conversion_deduction) {
                    match.standard.conversions++;
                } else {
                    match.standard.exactMatches++;
                }

                if (IS_TYPE_POINTER_ONLY(args->type)) {
                    CExpr_MatchCV(type2, qual2, args->type, args->qual, &match);
                }

            } else {
                Type* type;
                UInt32 qual;

                if ((type = CTempl_GetSpecifiedType(
                         CTemplTool_GetFuncTempl(object), args->type, args->qual, info->args, &qual
                     )) &&
                    !CExpr_MatchAssign(type, qual, exprs->node, &match)) {
                    return FALSE;
                }
            }
        } else {
            if (copts.old_argmatch &&
                !CExpr_MatchAssign(args->type, args->qual, exprs->node, &match)) {
                return FALSE;
            }
        }

        exprs = exprs->next;
        args = args->next;
    }

    for (i = 0; i < info->maxCount; i++) {
        if (!info->args[i].is_deduced) {
            return FALSE;
        }
        info->args[i].next = &info->args[i + 1];
    }
    info->args[info->maxCount].next = NULL;

    return CExpr_MatchCompare(object, best, &match);
}

// NOTE: invented name: best
// best: the best match found so far, which the call updates
void CTempl_FuncMatch(
    NameSpaceObjectList* list, TemplArg* args, ENodeList* argexprs, ArgMatch* best, ENode* expr
) {
    Object* obj;
    Object* success;
    Object* object1;
    FuncMatchArgs funcMatchArgs;
    DeduceInfo info;
    DeduceInfo info2;

    object1 = best->object;
    success = NULL;
    while (list) {
        obj = OBJECT(list->object);
        if (obj->otype == OT_OBJECT &&
            IS_TEMPL_FUNC(obj->type) &&
            CExpr_GetFuncMatchArgs(obj, argexprs, expr, &funcMatchArgs) &&
            CTemplTool_InitDeduceInfo(&info, CTemplTool_GetFuncTempl(obj)->params, args, FALSE) &&
            CTempl_MatchTemplFunc(obj, &info, funcMatchArgs.args, funcMatchArgs.exprs, best)) {
            info2 = info;
            if (info.args == info.argBuffer) {
                info2.args = info2.argBuffer;
            }
            success = obj;
        }
        list = list->next;
    }

    if (success) {
        if (best->list) {
            ENodeList* argexpr = argexprs;
            int argexprcount = 0;
            while (argexpr) {
                argexprcount++;
                argexpr = argexpr->next;
            }

            obj = CTempl_PartialOrdering(best->object, best->list, argexprcount);
            if (!obj) {
                CError_OverloadedFunctionError(best->object, best->list);
                best->list = NULL;
                if (object1) {
                    best->object = object1;
                } else {
                    best->object = CTempl_GetCreateFuncInstance(success, info2.args, NULL)->object;
                }
            } else {
                NameSpaceObjectList mylist;
                mylist.next = NULL;
                mylist.object = OBJ_BASE(obj);
                memclrw(best, sizeof(ArgMatch));
                CTempl_FuncMatch(&mylist, args, argexprs, best, expr);
            }
        } else {
            best->object = CTempl_GetCreateFuncInstance(success, info2.args, NULL)->object;
        }
    } else {
        best->object = object1;
    }
}

static Boolean CExpr_DeduceFromObjList(FuncArg* arg, NameSpaceObjectList* list, DeduceInfo* info) {
    TemplArg* savebuf;
    NameSpaceObjectList* scan;
    size_t size;

    for (scan = list; scan; scan = scan->next) {
        if (scan->object->otype == OT_OBJECT && IS_TEMPL_FUNC(OBJECT(scan->object)->type)) {
            return FALSE;
        }
    }

    size = sizeof(TemplArg) * info->maxCount;
    savebuf = lalloc(size);

    for (scan = list; scan; scan = scan->next) {
        if (scan->object->otype == OT_OBJECT && IS_TYPE_FUNC(OBJECT(scan->object)->type)) {
            memcpy(savebuf, info->args, size);

            if (CTempl_DeduceType(
                    arg->type,
                    arg->qual & (Q_CONST | Q_VOLATILE),
                    OBJECT(scan->object)->type,
                    OBJECT(scan->object)->qual,
                    savebuf,
                    TRUE,
                    FALSE
                )) {
                memcpy(info->args, savebuf, size);
                return TRUE;
            }
        }
    }

    return FALSE;
}

Object* CTempl_DeduceFromFunctionCall(Object* funcobj, TemplArg* templargs, ENodeList* argexprs) {
    DeduceInfo info;
    FuncArg* arg;
    int i;

    CError_ASSERT(1657, IS_TEMPL_FUNC(funcobj->type));

    if (CTemplTool_InitDeduceInfo(
            &info, CTemplTool_GetFuncTempl(funcobj)->params, templargs, FALSE
        )) {
        arg = TYPE_FUNC(funcobj->type)->args;
        if (IS_TYPEFUNC_NONSTATIC_METHOD(TYPE_FUNC(funcobj->type))) {
            CError_ASSERT(1666, arg);
            arg = arg->next;
        }

        while (1) {
            if (!arg || arg->type == &stvoid) {
                if (argexprs) {
                    return NULL;
                }
                break;
            }

            if (arg == &elipsis) {
                break;
            }
            if (arg == &oldstyle) {
                break;
            }

            if (!argexprs) {
                if (arg->dexpr) {
                    break;
                }
                return NULL;
            }

            ctempl_explicitargs_nindex = info.nindex;
            ctempl_explicitargs_num = info.count;
            ctempl_explicitargs_all = TRUE;

            if (CTempl_TypeNeedsDeduction(arg->type) && !ctempl_explicitargs_all) {
                ENode* node = argexprs->node;

                if (ENODE_IS(node, EOBJREF) && IS_TEMPL_FUNC(node->data.objref->type)) {
                    return NULL;
                }

                if (ENODE_IS(node, EOBJLIST)) {
                    if (!CExpr_DeduceFromObjList(arg, node->data.objlist.list, &info)) {
                        return NULL;
                    }
                } else {
                    if (!CTempl_DeduceType(
                            arg->type,
                            arg->qual & (Q_CONST | Q_VOLATILE),
                            node->rtype,
                            ENODE_QUALS(node),
                            info.args,
                            TRUE,
                            FALSE
                        )) {
                        return NULL;
                    }
                }
            }

            argexprs = argexprs->next;
            arg = arg->next;
        }

        for (i = 0; i < info.maxCount; i++) {
            if (!info.args[i].is_deduced) {
                return NULL;
            }
            info.args[i].next = &info.args[i + 1];
        }
        info.args[info.maxCount].next = NULL;

        return CTempl_GetCreateFuncInstance(funcobj, info.args, NULL)->object;
    } else {
        return NULL;
    }
}

Object* CTempl_DeduceFromConversion(Object* funcobj, Type* type, UInt32 qual) {
    DeduceInfo info;
    int i;

    CError_ASSERT(1747, IS_TEMPL_FUNC(funcobj->type));

    if (!CTemplTool_InitDeduceInfo(&info, CTemplTool_GetFuncTempl(funcobj)->params, NULL, FALSE)) {
        return NULL;
    }

    ctempl_explicitargs_nindex = info.nindex;
    ctempl_explicitargs_num = info.count;
    ctempl_explicitargs_all = TRUE;

    if (!CTempl_TypeNeedsDeduction(TYPE_FUNC(funcobj->type)->functype)) {
        return NULL;
    }

    if (!CTempl_DeduceType(
            TYPE_FUNC(funcobj->type)->functype,
            TYPE_FUNC(funcobj->type)->qual,
            type,
            qual,
            info.args,
            FALSE,
            TRUE
        )) {
        return NULL;
    }

    for (i = 0; i < info.maxCount; i++) {
        if (!info.args[i].is_deduced) {
            return NULL;
        }
        info.args[i].next = &info.args[i + 1];
    }
    info.args[info.maxCount].next = NULL;

    return CTempl_GetCreateFuncInstance(funcobj, info.args, NULL)->object;
}

static int CTemplFunc_TemplateNestLevel(NameSpace* nspace) {
    int level = 0;

    while (nspace) {
        if (nspace->theclass && (nspace->theclass->flags & CLASS_IS_TEMPL_ANY)) {
            level++;
        }
        nspace = nspace->parent;
    }

    return level;
}

static Boolean CTemplFunc_SameFuncType(TypeFunc* a, TypeFunc* b) {
    FuncArg* arg1;
    FuncArg* arg2;

    CError_ASSERT(1802, IS_TYPE_FUNC(a) && IS_TYPE_FUNC(b));

    if (!is_typesame(a->functype, b->functype)) {
        return FALSE;
    }

    if (a->qual != b->qual) {
        return FALSE;
    }

    if ((a->flags & FUNC_CALL_CONV_MASK) != (b->flags & FUNC_CALL_CONV_MASK)) {
        return FALSE;
    }

    arg1 = a->args;
    if ((a->flags & FUNC_METHOD) && !TYPE_METHOD(a)->is_static) {
        CError_ASSERT(1810, arg1);
        arg1 = arg1->next;
    }

    arg2 = b->args;
    if ((b->flags & FUNC_METHOD) && !TYPE_METHOD(b)->is_static) {
        CError_ASSERT(1816, arg2);
        arg2 = arg2->next;
    }

    return is_arglistsame(arg1, arg2);
}

Object* CTempl_TemplateFunctionCheck(DeclInfo* di, NameSpaceObjectList* nsol) {
    TemplArg* arg;
    TemplFuncInstance* inst;
    Object* object;

    for (arg = di->expltargs; arg; arg = arg->next) {
        switch (arg->pid.type) {
            case TPT_TYPE:
                if (CTemplTool_IsTemplateArgumentDependentType(arg->data.typeparam.type)) {
                    break;
                }
                continue;
            case TPT_NONTYPE:
                if (CTemplTool_IsTemplateArgumentDependentExpression(arg->data.paramdecl.expr)) {
                    break;
                }
                continue;
            default:
                CError_FATAL(1846);
        }

        break;
    }

    if (arg) {
        while (nsol) {
            object = OBJECT(nsol->object);
            if (object->otype == OT_OBJECT &&
                IS_TEMPL_FUNC(object->type) &&
                CTemplTool_IsSameTemplate(CTemplTool_GetFuncTempl(object)->params, di->expltargs) &&
                CTemplFunc_SameFuncType(
                    TYPE_FUNC(OBJECT(nsol->object)->type), TYPE_FUNC(di->thetype)
                )) {
                return OBJECT(nsol->object);
            }

            nsol = nsol->next;
        }
    } else {
        while (nsol) {
            object = OBJECT(nsol->object);
            if (object->otype == OT_OBJECT &&
                IS_TEMPL_FUNC(object->type) &&
                (inst = CTempl_DeduceFunc(
                     object, TYPE_FUNC(di->thetype), di->expltargs, NULL, TRUE
                 ))) {
                if (di->requireTemplateClassMember) {
                    if (!(di->qual & Q_INLINE)) {
                        if (!inst->is_specialized) {
                            inst->object->qual &= ~Q_INLINE;
                        }
                    } else {
                        inst->object->qual |= Q_INLINE;
                    }

                    if (!inst->is_specialized && inst->is_instantiated) {
                        CError_Error(ERR_ILLEGAL_EXPLICIT_TEMPLATE_SPECIALIZATION);
                    }

                    if (di->requireTemplateClassMember !=
                        (CTemplFunc_TemplateNestLevel(inst->object->nspace) + 1)) {
                        CError_Warning(ERR_ILLEGAL_EXPLICIT_TEMPLATE_SPECIALIZATION);
                    }

                    inst->is_specialized = TRUE;
                    di->requireTemplateClassMember = 0;
                }

                CTemplTool_MergeArgNames(TYPE_FUNC(di->thetype), TYPE_FUNC(inst->object->type));
                di->templateFunction = CTemplTool_GetFuncTempl(OBJECT(nsol->object));
                return inst->object;
            }
            nsol = nsol->next;
        }
    }

    CError_ResetErrorSkip();
    CError_Error(ERR_ILLEGAL_EXPLICIT_TEMPLATE_SPECIALIZATION);
    di->requireTemplateClassMember = 0;
    return NULL;
}

// The unit's statics are 2-byte packed, as in the original (MWCC lays out .bss with the packing in
// force at the end of the file)
#pragma pack(2)
