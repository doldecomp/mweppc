#include "compiler/IROUseDef.h"
#include "compiler/IroDump.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroMalloc.h"
#include "compiler/IroPointerAnalysis.h"
#include "compiler/IroTransform.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/BitVector.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/InlineAsm.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/objects.h"
#include "compiler/types.h"
#include <string.h>

ENodeType IRO_NonAssignmentOp[MAXEXPR];
typedef CInt64 (*AssignmentFoldingFunc)(CInt64, CInt64);
static AssignmentFoldingFunc AssignmentFoldingFunction[MAXEXPR];
static SInt32 NumDefs;
static IRODef* FirstDef;
static IRODef* LastDef;
static SInt32 NumUses;
static BitVector* Reaches;
static BitVector* MightReachAnyUse;
static BitVector* alldefs;
static BitVector* alluses;
static BitVector* defset;
static BitVector* useset;
IROUse* IRO_FirstVarUse;
IROUse* IRO_LastVarUse;

static void AddDefToRange(IRODef* def);

// NOTE: invented names: var, linear, node, definite
static void MakeDef(VarRecord* var, IROLinear* linear, IRONode* node, Boolean definite) {
    IRODef* def = oalloc(sizeof(IRODef));

    def->index = NumDefs++;
    def->node = node;
    def->linear = linear;
    def->var = var;
    def->globalnext = NULL;
    def->useCount = 0;
    def->global = Inline_IsObjectData(var->object);
    def->noregister = var->noregister;
    def->definite = definite;
    def->removable = FALSE;

    if (FirstDef) {
        LastDef->globalnext = def;
    } else {
        FirstDef = def;
    }
    LastDef = def;

    def->varnext = var->defs;
    var->defs = def;
}

static void MakeUse(VarRecord* var, IROLinear* linear, IRONode* node) {
    IROUse* use = oalloc(sizeof(IROUse));

    use->index = NumUses++;
    use->node = node;
    use->linear = linear;
    use->var = var;
    use->globalnext = NULL;
    use->reachingDefCount = 0;

    if (IRO_FirstVarUse) {
        IRO_LastVarUse->globalnext = use;
    } else {
        IRO_FirstVarUse = use;
    }
    IRO_LastVarUse = use;

    use->varnext = var->uses;
    var->uses = use;
}

static void FindDefsAndUses(void) {
    VarRecord* var;
    IROLinear* linear;
    IRONode* node;

    NumDefs = 0;
    NumUses = 0;
    IRO_FirstVarUse = NULL;
    FirstDef = NULL;

    for (var = IRO_FirstVar; var; var = var->next) {
        var->defs = NULL;
        var->uses = NULL;
        var->usedAtCall = FALSE;
    }

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        for (linear = node->first; linear; linear = linear->next) {
            if (IS_LINEAR_ENODE(linear, EOBJREF)) {
                Object* obj = linear->u.node->data.objref;
                if ((linear->flags & IROLF_Ind) &&
                    (!(linear->flags & IROLF_Assigned) || (linear->flags & IROLF_Used))) {
                    if ((var = IRO_FindVar(obj, FALSE, TRUE))) {
                        MakeUse(var, linear, node);
                    }
                }
            }

            if (IRO_IsAssignment(linear) && (var = IRO_FindAssigned(linear))) {
                MakeDef(
                    var,
                    linear,
                    node,
                    (linear->rtype->size == var->object->type->size) && !IRO_IsBitField
                );
            }

            if (linear->type == IROLinearAsm) {
                IAEffects effects;
                int i;
                CodeGen_GetAsmEffects(linear->u.asm_stmt, &effects);

                for (i = 0; i < effects.numoperands; i++) {
                    var = IRO_FindVar(effects.operands[i].object, FALSE, TRUE);
                    switch (effects.operands[i].type) {
                        case IAEffect_Read:
                            MakeUse(var, linear, node);
                            break;
                        case IAEffect_Write:
                            MakeDef(
                                var,
                                linear,
                                node,
                                (effects.operands[i].offset == 0) &&
                                    (effects.operands[i].size == var->object->type->size)
                            );
                            break;
                        case IAEffect_Clobber:
                            MakeDef(var, linear, node, FALSE);
                            break;
                        case IAEffect_ReadWrite:
                            MakeUse(var, linear, node);
                            MakeDef(
                                var,
                                linear,
                                node,
                                (effects.operands[i].offset == 0) &&
                                    (effects.operands[i].size == var->object->type->size)
                            );
                            break;
                        default:
                            break;
                    }
                }
            }

            if (linear == node->last) {
                break;
            }
        }
    }

    for (var = IRO_FirstVar; var; var = var->next) {
        MakeDef(var, NULL, NULL, TRUE);
    }
}

static void MarkUses(Object* obj) {
    IRODef* def;
    VarRecord* var;

    if ((var = IRO_FindVar(obj, FALSE, TRUE))) {
        for (def = var->defs; def; def = def->varnext) {
            if (Bv_IsBitSet(def->index, Reaches)) {
                Bv_SetBit(def->index, MightReachAnyUse);
                def->useCount++;
            }
        }

        var->usedAtCall = TRUE;
    }
}

static Boolean IsIncOrDec(IROLinear* linear) {
    // (switched as an `int`: no other node type is an increment or decrement)
    switch ((int)linear->type) {
        case IROLinearOp1Arg:
            if (linear->nodetype == EPOSTINC ||
                linear->nodetype == EPOSTDEC ||
                linear->nodetype == EPREINC ||
                linear->nodetype == EPREDEC) {
                if (!(linear->u.monadic->flags & IROLF_BitfieldIndirect)) {
                    return TRUE;
                }
            }
            break;
        case IROLinearOp2Arg:
            if (linear->nodetype == EADDASS || linear->nodetype == ESUBASS) {
                if (IRO_IsIntConstant(linear->u.diadic.right) &&
                    !(linear->u.diadic.left->flags & IROLF_BitfieldIndirect)) {
                    return TRUE;
                }
            }
            break;
    }

    return FALSE;
}

static Boolean IsOtherSelfAssignment(IROLinear* linear) {
    switch (linear->type) {
        case IROLinearOp2Arg:
            switch (linear->nodetype) {
                case EMULASS:
                case EDIVASS:
                case ESHLASS:
                case ESHRASS:
                case EANDASS:
                case EXORASS:
                case EORASS:
                    if (IRO_IsIntConstant(linear->u.diadic.right)) {
                        return TRUE;
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }

    return FALSE;
}

CInt64 IRO_GetSelfAssignmentVal(IROLinear* linear) {
    CInt64 result;

    if (linear->type == IROLinearOp1Arg) {
        switch (linear->nodetype) {
            case EPOSTINC:
            case EPREINC:
                CInt64_SetLong(&result, 1);
                break;
            case EPOSTDEC:
            case EPREDEC:
                CInt64_SetLong(&result, -1);
                break;
            default:
                CError_FATAL(445);
        }

        if (IS_TYPE_POINTER_ONLY(linear->rtype)) {
            CInt64 mul;
            CInt64_SetLong(&mul, TPTR_TARGET(linear->rtype)->size);
            result = CInt64_Mul(result, mul);
        }
    } else if (linear->type == IROLinearOp2Arg) {
        switch (linear->nodetype) {
            case EMULASS:
            case EDIVASS:
            case EMODASS:
            case EADDASS:
            case ESHLASS:
            case ESHRASS:
            case EANDASS:
            case EXORASS:
            case EORASS:
                result = linear->u.diadic.right->u.node->data.intval;
                break;
            case ESUBASS:
                result = linear->u.diadic.right->u.node->data.intval;
                result = CInt64_Neg(result);
                break;
            default:
                CError_FATAL(491);
        }
    } else {
        CError_FATAL(496);
    }

    return result;
}

#if VERSION >= VERSION_GC_2_5
// 2.5 and later: converts NODE to TYPE (a bool through two ELOGNOTs), for the integral promotion in
// UpdateUse and UpdateUseForOtherSelfAssignment
static IROLinear* IRO_AddTypeConversion(IROLinear* node, Type* type, IROList* list) {
    IROLinear* first;
    IROLinear* last;
    int isBool;

    isBool = type->type == TYPEINT && TYPE_INTEGRAL(type)->integral == IT_BOOL;

    if (isBool) {
        first = IRO_NewLinear(IROLinearOp1Arg);
        memcpy(first, node, sizeof(IROLinear));
        first->type = IROLinearOp1Arg;
        first->nodetype = ELOGNOT;
        first->index = IRO_NumLinear++;
        first->rtype = type;
        first->u.monadic = node;
        first->next = last = IRO_NewLinear(IROLinearOp1Arg);
        memcpy(last, node, sizeof(IROLinear));
        last->type = IROLinearOp1Arg;
        last->nodetype = ELOGNOT;
        last->index = IRO_NumLinear++;
        last->rtype = type;
        last->u.monadic = first;
        last->next = NULL;
        if (!list) {
            IRO_PasteAfter(first, last, node);
        } else {
            IRO_AddToList(first, list);
        }
    } else {
        first = IRO_NewLinear(IROLinearOp1Arg);
        memcpy(first, node, sizeof(IROLinear));
        first->type = IROLinearOp1Arg;
        first->nodetype = ETYPCON;
        first->index = IRO_NumLinear++;
        first->rtype = type;
        first->u.monadic = node;
        first->next = NULL;
        if (!list) {
            IRO_PasteAfter(first, first, node);
        } else {
            IRO_AddToList(first, list);
        }
        last = first;
    }
    return last;
}
#endif

static void UpdateUse(IROLinear* linear, CInt64 val, Type* type) {
#if VERSION < VERSION_GC_2_5
    IROLinear* father;
    IROLinear* repl;
    IROLinear* newdiadic;
#else
    Type* promoted;
    Boolean isInt;
    IROLinear* father;
    IROLinear* newdiadic;
    IROLinear* operand;
    IROList list;
    Type* constType;
    IROLinear* repl;
#endif

    if ((father = IRO_LocateFather(linear))) {
        switch (father->type) {
            case IROLinearOp1Arg:
                switch (father->nodetype) {
                    case EPOSTINC:
                    case EPOSTDEC:
                    case EPREINC:
                    case EPREDEC:
                        val = CInt64_Add(val, IRO_GetSelfAssignmentVal(father));
                        father->nodetype = EADDASS;
                        father->type = IROLinearOp2Arg;
                        repl = IRO_NewIntConst(val, type);
                        father->u.diadic.right = repl;
                        IRO_PasteAfter(repl, repl, linear);
                        return;
                    default:
                        break;
                }
                break;
            case IROLinearOp2Arg:
                if (IRO_IsIntConstant(father->u.diadic.right)) {
                    switch (father->nodetype) {
                        case EADD:
                        case EADDASS:
                            father->u.diadic.right->u.node->data.intval =
                                CInt64_Add(father->u.diadic.right->u.node->data.intval, val);
                            return;
                        case ESUB:
                        case ESUBASS:
                            father->u.diadic.right->u.node->data.intval =
                                CInt64_Sub(father->u.diadic.right->u.node->data.intval, val);
                            return;
                        default:
                            break;
                    }
                }
                break;
            default:
                break;
        }
    }

#if VERSION < VERSION_GC_2_5
    // 2.0: the step is added in the use's own type, without promotion
    repl = IRO_NewIntConst(val, type);
    newdiadic = IRO_NewLinear(IROLinearOp2Arg);
    newdiadic->index = ++IRO_NumLinear;
    newdiadic->nodetype = EADD;
    newdiadic->u.diadic.left = linear;
    newdiadic->u.diadic.right = repl;
    newdiadic->rtype = linear->rtype;
    repl->next = newdiadic;
    IRO_LocateFather_Cut_And_Paste_Without_Nopping(linear, newdiadic);
    IRO_PasteAfter(repl, newdiadic, linear);
#else
    IRO_InitList(&list);
    promoted = type;
    isInt = type->type == TYPEINT && TYPE_INTEGRAL(type)->integral < IT_17;
    if (isInt || type->type == TYPEENUM) {
        promoted = type;
        if (type->type == TYPEENUM) {
            promoted = TYPE_ENUM(type)->enumtype;
        }
        if (TYPE_INTEGRAL(promoted)->integral < IT_INT) {
            if (is_unsigned(promoted)) {
                promoted = TYPE(&stunsignedint);
            } else {
                promoted = TYPE(&stsignedint);
            }
        }
    }
    constType = type;
    if (type->type == TYPEPOINTER) {
        constType = TYPE(&stunsignedlong);
    }
    operand = linear;
    if (promoted != type) {
        operand = IRO_AddTypeConversion(linear, promoted, &list);
    }
    if (constType == type) {
        constType = promoted;
    }
    repl = IRO_NewIntConst(val, constType);
    IRO_AddToList(repl, &list);
    newdiadic = IRO_NewLinear(IROLinearOp2Arg);
    newdiadic->index = ++IRO_NumLinear;
    newdiadic->nodetype = EADD;
    newdiadic->u.diadic.left = operand;
    newdiadic->u.diadic.right = repl;
    newdiadic->rtype = promoted;
    IRO_AddToList(newdiadic, &list);
    if (promoted != type) {
        newdiadic = IRO_AddTypeConversion(newdiadic, type, &list);
    }
    IRO_LocateFather_Cut_And_Paste_Without_Nopping(linear, newdiadic);
    IRO_PasteAfter(list.head, list.tail, linear);
#endif
}

void IRO_InitializeNonAssignmentOpArray(void) {
    int i;

    for (i = 0; i < MAXEXPR; i++) {
        IRO_NonAssignmentOp[i] = MAXEXPR;
    }

    IRO_NonAssignmentOp[EPOSTINC] = MAXEXPR;
    IRO_NonAssignmentOp[EPOSTDEC] = MAXEXPR;
    IRO_NonAssignmentOp[EPREINC] = MAXEXPR;
    IRO_NonAssignmentOp[EPREDEC] = MAXEXPR;
    IRO_NonAssignmentOp[EINDIRECT] = MAXEXPR;
    IRO_NonAssignmentOp[EMONMIN] = MAXEXPR;
    IRO_NonAssignmentOp[EBINNOT] = MAXEXPR;
    IRO_NonAssignmentOp[ELOGNOT] = MAXEXPR;
    IRO_NonAssignmentOp[EFORCELOAD] = MAXEXPR;
    IRO_NonAssignmentOp[EMUL] = MAXEXPR;
    IRO_NonAssignmentOp[EMULV] = MAXEXPR;
    IRO_NonAssignmentOp[EDIV] = MAXEXPR;
    IRO_NonAssignmentOp[EMODULO] = MAXEXPR;
    IRO_NonAssignmentOp[EADDV] = MAXEXPR;
    IRO_NonAssignmentOp[ESUBV] = MAXEXPR;
    IRO_NonAssignmentOp[EADD] = MAXEXPR;
    IRO_NonAssignmentOp[ESUB] = MAXEXPR;
    IRO_NonAssignmentOp[ESHL] = MAXEXPR;
    IRO_NonAssignmentOp[ESHR] = MAXEXPR;
    IRO_NonAssignmentOp[ELESS] = MAXEXPR;
    IRO_NonAssignmentOp[EGREATER] = MAXEXPR;
    IRO_NonAssignmentOp[ELESSEQU] = MAXEXPR;
    IRO_NonAssignmentOp[EGREATEREQU] = MAXEXPR;
    IRO_NonAssignmentOp[EEQU] = MAXEXPR;
    IRO_NonAssignmentOp[ENOTEQU] = MAXEXPR;
    IRO_NonAssignmentOp[EAND] = MAXEXPR;
    IRO_NonAssignmentOp[EXOR] = MAXEXPR;
    IRO_NonAssignmentOp[EOR] = MAXEXPR;
    IRO_NonAssignmentOp[ELAND] = MAXEXPR;
    IRO_NonAssignmentOp[ELOR] = MAXEXPR;
    IRO_NonAssignmentOp[EASS] = MAXEXPR;
    IRO_NonAssignmentOp[EMULASS] = EMUL;
    IRO_NonAssignmentOp[EDIVASS] = EDIV;
    IRO_NonAssignmentOp[EMODASS] = EMODULO;
    IRO_NonAssignmentOp[EADDASS] = EADD;
    IRO_NonAssignmentOp[ESUBASS] = ESUB;
    IRO_NonAssignmentOp[ESHLASS] = ESHL;
    IRO_NonAssignmentOp[ESHRASS] = ESHR;
    IRO_NonAssignmentOp[EANDASS] = EAND;
    IRO_NonAssignmentOp[EXORASS] = EXOR;
    IRO_NonAssignmentOp[EORASS] = EOR;
    IRO_NonAssignmentOp[ECOMMA] = MAXEXPR;
    IRO_NonAssignmentOp[EPMODULO] = MAXEXPR;
    IRO_NonAssignmentOp[EROTL] = MAXEXPR;
    IRO_NonAssignmentOp[EROTR] = MAXEXPR;
    IRO_NonAssignmentOp[EBCLR] = MAXEXPR;
    IRO_NonAssignmentOp[EBTST] = MAXEXPR;
    IRO_NonAssignmentOp[EBSET] = MAXEXPR;
    IRO_NonAssignmentOp[ETYPCON] = MAXEXPR;
    IRO_NonAssignmentOp[EBITFIELD] = MAXEXPR;
    IRO_NonAssignmentOp[EINTCONST] = MAXEXPR;
    IRO_NonAssignmentOp[EFLOATCONST] = MAXEXPR;
    IRO_NonAssignmentOp[ESTRINGCONST] = MAXEXPR;
    IRO_NonAssignmentOp[ECOND] = MAXEXPR;
    IRO_NonAssignmentOp[EFUNCCALL] = MAXEXPR;
    IRO_NonAssignmentOp[EFUNCCALLP] = MAXEXPR;
    IRO_NonAssignmentOp[EOBJREF] = MAXEXPR;
    IRO_NonAssignmentOp[EMFPOINTER] = MAXEXPR;
    IRO_NonAssignmentOp[ENULLCHECK] = MAXEXPR;
    IRO_NonAssignmentOp[EPRECOMP] = MAXEXPR;
    IRO_NonAssignmentOp[ETEMP] = MAXEXPR;
    IRO_NonAssignmentOp[EARGOBJ] = MAXEXPR;
    IRO_NonAssignmentOp[ELOCOBJ] = MAXEXPR;
    IRO_NonAssignmentOp[ELABEL] = MAXEXPR;
    IRO_NonAssignmentOp[ESETCONST] = MAXEXPR;
    IRO_NonAssignmentOp[ENEWEXCEPTION] = MAXEXPR;
    IRO_NonAssignmentOp[ENEWEXCEPTIONARRAY] = MAXEXPR;
    IRO_NonAssignmentOp[EOBJLIST] = MAXEXPR;
    IRO_NonAssignmentOp[EMEMBER] = MAXEXPR;
    IRO_NonAssignmentOp[ETEMPLDEP] = MAXEXPR;
    IRO_NonAssignmentOp[EINSTRUCTION] = MAXEXPR;
    IRO_NonAssignmentOp[EDEFINE] = MAXEXPR;
    IRO_NonAssignmentOp[EREUSE] = MAXEXPR;
    IRO_NonAssignmentOp[EASSBLK] = MAXEXPR;
    IRO_NonAssignmentOp[EVECTOR128CONST] = MAXEXPR;
    IRO_NonAssignmentOp[ECONDASS] = MAXEXPR;
}

void IRO_InitializeAssignmentFoldingFunctionArray(void) {
    int i;

    for (i = 0; i < MAXEXPR; i++) {
        AssignmentFoldingFunction[i] = NULL;
    }

    AssignmentFoldingFunction[EPOSTINC] = NULL;
    AssignmentFoldingFunction[EPOSTDEC] = NULL;
    AssignmentFoldingFunction[EPREINC] = NULL;
    AssignmentFoldingFunction[EPREDEC] = NULL;
    AssignmentFoldingFunction[EINDIRECT] = NULL;
    AssignmentFoldingFunction[EMONMIN] = NULL;
    AssignmentFoldingFunction[EBINNOT] = NULL;
    AssignmentFoldingFunction[ELOGNOT] = NULL;
    AssignmentFoldingFunction[EFORCELOAD] = NULL;
    AssignmentFoldingFunction[EMUL] = NULL;
    AssignmentFoldingFunction[EMULV] = NULL;
    AssignmentFoldingFunction[EDIV] = NULL;
    AssignmentFoldingFunction[EMODULO] = NULL;
    AssignmentFoldingFunction[EADDV] = NULL;
    AssignmentFoldingFunction[ESUBV] = NULL;
    AssignmentFoldingFunction[EADD] = NULL;
    AssignmentFoldingFunction[ESUB] = NULL;
    AssignmentFoldingFunction[ESHL] = NULL;
    AssignmentFoldingFunction[ESHR] = NULL;
    AssignmentFoldingFunction[ELESS] = NULL;
    AssignmentFoldingFunction[EGREATER] = NULL;
    AssignmentFoldingFunction[ELESSEQU] = NULL;
    AssignmentFoldingFunction[EGREATEREQU] = NULL;
    AssignmentFoldingFunction[EEQU] = NULL;
    AssignmentFoldingFunction[ENOTEQU] = NULL;
    AssignmentFoldingFunction[EAND] = NULL;
    AssignmentFoldingFunction[EXOR] = NULL;
    AssignmentFoldingFunction[EOR] = NULL;
    AssignmentFoldingFunction[ELAND] = NULL;
    AssignmentFoldingFunction[ELOR] = NULL;
    AssignmentFoldingFunction[EASS] = NULL;
    AssignmentFoldingFunction[EMULASS] = CInt64_Mul;
    AssignmentFoldingFunction[EDIVASS] = CInt64_Mul;
    AssignmentFoldingFunction[EMODASS] = NULL;
    AssignmentFoldingFunction[EADDASS] = CInt64_Add;
    AssignmentFoldingFunction[ESUBASS] = CInt64_Add;
    AssignmentFoldingFunction[ESHLASS] = CInt64_Add;
    AssignmentFoldingFunction[ESHRASS] = CInt64_Add;
    AssignmentFoldingFunction[EANDASS] = CInt64_And;
    AssignmentFoldingFunction[EXORASS] = CInt64_Xor;
    AssignmentFoldingFunction[EORASS] = CInt64_Or;
    AssignmentFoldingFunction[ECOMMA] = NULL;
    AssignmentFoldingFunction[EPMODULO] = NULL;
    AssignmentFoldingFunction[EROTL] = NULL;
    AssignmentFoldingFunction[EROTR] = NULL;
    AssignmentFoldingFunction[EBCLR] = NULL;
    AssignmentFoldingFunction[EBTST] = NULL;
    AssignmentFoldingFunction[EBSET] = NULL;
    AssignmentFoldingFunction[ETYPCON] = NULL;
    AssignmentFoldingFunction[EBITFIELD] = NULL;
    AssignmentFoldingFunction[EINTCONST] = NULL;
    AssignmentFoldingFunction[EFLOATCONST] = NULL;
    AssignmentFoldingFunction[ESTRINGCONST] = NULL;
    AssignmentFoldingFunction[ECOND] = NULL;
    AssignmentFoldingFunction[EFUNCCALL] = NULL;
    AssignmentFoldingFunction[EFUNCCALLP] = NULL;
    AssignmentFoldingFunction[EOBJREF] = NULL;
    AssignmentFoldingFunction[EMFPOINTER] = NULL;
    AssignmentFoldingFunction[ENULLCHECK] = NULL;
    AssignmentFoldingFunction[EPRECOMP] = NULL;
    AssignmentFoldingFunction[ETEMP] = NULL;
    AssignmentFoldingFunction[EARGOBJ] = NULL;
    AssignmentFoldingFunction[ELOCOBJ] = NULL;
    AssignmentFoldingFunction[ELABEL] = NULL;
    AssignmentFoldingFunction[ESETCONST] = NULL;
    AssignmentFoldingFunction[ENEWEXCEPTION] = NULL;
    AssignmentFoldingFunction[ENEWEXCEPTIONARRAY] = NULL;
    AssignmentFoldingFunction[EOBJLIST] = NULL;
    AssignmentFoldingFunction[EMEMBER] = NULL;
    AssignmentFoldingFunction[ETEMPLDEP] = NULL;
    AssignmentFoldingFunction[EINSTRUCTION] = NULL;
    AssignmentFoldingFunction[EDEFINE] = NULL;
    AssignmentFoldingFunction[EREUSE] = NULL;
    AssignmentFoldingFunction[EASSBLK] = NULL;
    AssignmentFoldingFunction[EVECTOR128CONST] = NULL;
    AssignmentFoldingFunction[ECONDASS] = NULL;
}

static void UpdateUseForOtherSelfAssignment(IROLinear* a, IROLinear* b, Type* type) {
    CInt64 val;
#if VERSION < VERSION_GC_2_5
    IROLinear* repl;
    IROLinear* father;
#else
    IROLinear* father;
    IROLinear* repl;
#endif
    IROLinear* newdiadic;
#if VERSION >= VERSION_GC_2_5
    IROLinear* operand;
    IROList list;
    Type* promoted;
    Boolean isInt;
#endif

    val = IRO_GetSelfAssignmentVal(b);
    if ((father = IRO_LocateFather(a)) &&
        father->type == IROLinearOp2Arg &&
        IRO_IsIntConstant(father->u.diadic.right)) {
        // the father's constant operand
        CInt64 fatherval = father->u.diadic.right->u.node->data.intval; // NOTE: invented name
        if (AssignmentFoldingFunction[b->nodetype] &&
            ((father->nodetype == b->nodetype) ||
             (father->nodetype == IRO_NonAssignmentOp[b->nodetype]))) {
            CInt64 v;
            CInt64 folded;
            CInt64_SetLong(&v, b->rtype->size * 8);
            folded = AssignmentFoldingFunction[b->nodetype](fatherval, val);
            if (b->nodetype == ESHRASS && !is_unsigned(b->rtype)) {
                if (CInt64_LessU(fatherval, v) && CInt64_LessU(val, v)) {
                    if (CInt64_GreaterEqualU(folded, v)) {
                        folded = CInt64_Sub(v, cint64_one);
                    }
                    father->u.diadic.right->u.node->data.intval = folded;
                    return;
                }
            } else {
                father->u.diadic.right->u.node->data.intval = folded;
                return;
            }
        } else {
            switch (b->nodetype) {
                case EMULASS:
                    if (father->nodetype == ESHL || father->nodetype == ESHLASS) {
                        CInt64 v;
                        int powvalue;
                        if (IRO_IsPow2(b->u.diadic.right, &powvalue)) {
                            if (powvalue > 0) {
                                CInt64_SetLong(&v, powvalue);
                                father->u.diadic.right->u.node->data.intval =
                                    CInt64_Add(v, fatherval);
                            }
                        } else {
                            father->nodetype = (father->nodetype == ESHL) ? EMUL : EMULASS;
                            fatherval = CInt64_Shl(cint64_one, fatherval);
                            father->u.diadic.right->u.node->data.intval =
                                CInt64_Mul(fatherval, val);
                        }
                        return;
                    }
                    break;
                case EDIVASS:
                    if ((father->nodetype == ESHR || father->nodetype == ESHRASS) &&
                        is_unsigned(father->rtype)) {
                        CInt64 v;
                        int powvalue;
                        if (IRO_IsPow2(b->u.diadic.right, &powvalue)) {
                            if (powvalue > 0) {
                                CInt64_SetLong(&v, powvalue);
                                father->u.diadic.right->u.node->data.intval =
                                    CInt64_Add(v, fatherval);
                            }
                        } else {
                            father->nodetype = (father->nodetype == ESHR) ? EDIV : EDIVASS;
                            fatherval = CInt64_Shl(cint64_one, fatherval);
                            father->u.diadic.right->u.node->data.intval =
                                CInt64_Mul(fatherval, val);
                        }
                        return;
                    }
                    break;
                case ESHLASS:
                    if (father->nodetype == EMUL || father->nodetype == EMULASS) {
                        CInt64 v;
                        int powvalue;
                        if (IRO_IsPow2(father->u.diadic.right, &powvalue)) {
                            if (powvalue > 0) {
                                father->nodetype = (father->nodetype == EMUL) ? ESHL : ESHLASS;
                                CInt64_SetLong(&v, powvalue);
                                father->u.diadic.right->u.node->data.intval = CInt64_Add(v, val);
                            }
                        } else {
                            val = CInt64_Shl(cint64_one, val);
                            father->u.diadic.right->u.node->data.intval =
                                CInt64_Mul(fatherval, val);
                        }
                        return;
                    } else if (father->nodetype == ESHR || father->nodetype == ESHRASS) {
                        if (CInt64_Equal(fatherval, val) && is_unsigned(father->rtype)) {
                            father->nodetype = (father->nodetype == ESHR) ? EAND : EANDASS;
                            if (father->rtype->size < 8) {
                                CInt64 v;
                                CInt64_SetLong(&v, 64 - (father->rtype->size * 8));
                                val = CInt64_Add(val, v);
                            }
                            father->u.diadic.right->u.node->data.intval =
                                CInt64_ShrU(cint64_negone, val);
                            return;
                        }
                    }
                    break;
                case ESHRASS:
                    if ((father->nodetype == EDIV || father->nodetype == EDIVASS) &&
                        is_unsigned(father->rtype)) {
                        CInt64 v;
                        int powvalue;
                        if (IRO_IsPow2(father->u.diadic.right, &powvalue)) {
                            if (powvalue > 0) {
                                father->nodetype = (father->nodetype == EDIV) ? ESHR : ESHRASS;
                                CInt64_SetLong(&v, powvalue);
                                father->u.diadic.right->u.node->data.intval = CInt64_Add(v, val);
                            }
                        } else {
                            val = CInt64_Shl(cint64_one, val);
                            father->u.diadic.right->u.node->data.intval =
                                CInt64_Mul(fatherval, val);
                        }
                        return;
                    } else if (father->nodetype == ESHL || father->nodetype == ESHLASS) {
                        if (CInt64_Equal(fatherval, val)) {
                            father->nodetype = (father->nodetype == ESHL) ? EAND : EANDASS;
                            father->u.diadic.right->u.node->data.intval =
                                CInt64_Shl(cint64_negone, val);
                            return;
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }

#if VERSION < VERSION_GC_2_5
    // 2.0: the operation is done in the use's own type, without promotion
    repl = IRO_NewIntConst(val, type);
    newdiadic = IRO_NewLinear(IROLinearOp2Arg);
    newdiadic->index = ++IRO_NumLinear;
    newdiadic->nodetype = IRO_NonAssignmentOp[b->nodetype];
    newdiadic->u.diadic.left = a;
    newdiadic->u.diadic.right = repl;
    newdiadic->rtype = a->rtype;
    repl->next = newdiadic;
    IRO_LocateFather_Cut_And_Paste_Without_Nopping(a, newdiadic);
    IRO_PasteAfter(repl, newdiadic, a);
#else
    IRO_InitList(&list);
    promoted = type;
    isInt = type->type == TYPEINT && TYPE_INTEGRAL(type)->integral < IT_17;
    if (isInt || type->type == TYPEENUM) {
        promoted = type;
        if (type->type == TYPEENUM) {
            promoted = TYPE_ENUM(type)->enumtype;
        }
        if (TYPE_INTEGRAL(promoted)->integral < IT_INT) {
            if (is_unsigned(promoted)) {
                promoted = TYPE(&stunsignedint);
            } else {
                promoted = TYPE(&stsignedint);
            }
        }
    }
    operand = a;
    if (promoted != type) {
        operand = IRO_AddTypeConversion(a, promoted, &list);
    }
    repl = IRO_NewIntConst(val, promoted);
    IRO_AddToList(repl, &list);
    newdiadic = IRO_NewLinear(IROLinearOp2Arg);
    newdiadic->index = ++IRO_NumLinear;
    newdiadic->nodetype = IRO_NonAssignmentOp[b->nodetype];
    newdiadic->u.diadic.left = operand;
    newdiadic->u.diadic.right = repl;
    newdiadic->rtype = promoted;
    IRO_AddToList(newdiadic, &list);
    if (promoted != type) {
        newdiadic = IRO_AddTypeConversion(newdiadic, type, &list);
    }
    IRO_LocateFather_Cut_And_Paste_Without_Nopping(a, newdiadic);
    IRO_PasteAfter(list.head, list.tail, a);
#endif
}

static Boolean PropagateIncsAndDecs(void) {
    IRODef* def;
    Boolean result;

    result = FALSE;
    def = FirstDef;
    while (def) {
        if (def->linear &&
            def->definite &&
            IsIncOrDec(def->linear) &&
            !(def->linear->flags & IROLF_Reffed) &&
            (IS_TYPE_INT_OR_ENUM(def->linear->rtype) || IS_TYPE_POINTER_ONLY(def->linear->rtype)) &&
            IRO_TypesEqual(def->linear->rtype, def->var->object->type) &&
            !is_volatile_object(def->var->object)) {
            IROUse* use;
            IROUse* use2;
            IROLinear* father;
            CInt64 val;
            SInt32 i;
#if VERSION < VERSION_GC_2_5
            Type* type;
#endif

            for (use = def->var->uses, i = 0; use; use = use->varnext) {
                if (Bv_IsBitSet(def->index, use->reachingDefs)) {
                    if (use->reachingDefCount == 1 &&
                        use->linear->type == IROLinearOperand &&
                        (father = IRO_LocateFather(use->linear)) &&
                        IRO_IsVariable(father) &&
                        IRO_TypesEqual(def->linear->rtype, father->rtype)) {
                        if (use->linear->flags & IROLF_Assigned) {
                            if (!((father = IRO_LocateFather(father)) &&
                                  IsIncOrDec(father) &&
                                  !(father->flags & IROLF_Reffed))) {
                                goto nextDef;
                            }
                        }
                        i++;
                    } else {
                        goto nextDef;
                    }
                }
            }

            if (i == def->useCount) {
                for (use = def->var->uses; use; use = use->varnext) {
                    if (Bv_IsBitSet(def->index, use->reachingDefs)) {
                        IRO_Dump(
                            "Propagating inc/dec from %d to %d\n",
                            def->linear->index,
                            use->linear->index
                        );
                        father = IRO_LocateFather(use->linear);
                        val = IRO_GetSelfAssignmentVal(def->linear);
#if VERSION < VERSION_GC_2_5
                        // 2.0: the step's type is the variable's (an address steps as unsigned
                        // long)
                        type = def->linear->rtype;
                        if (IS_TYPE_POINTER_ONLY(def->linear->rtype)) {
                            type = TYPE(&stunsignedlong);
                        }
                        UpdateUse(father, val, type);
#else
                        UpdateUse(father, val, father->rtype);
#endif

                        result = TRUE;
                        for (use2 = def->var->uses; use2; use2 = use2->varnext) {
                            if (use2->linear == def->linear->u.monadic->u.diadic.left) {
                                use->reachingDefCount = use2->reachingDefCount;
                                Bv_Copy(use2->reachingDefs, use->reachingDefs);
                                break;
                            }
                        }
                    }
                }
                def->useCount = 0;
                IRO_NopNonSideEffects(def->linear, -1);
                def->linear->type = IROLinearNop;
                IRO_Dump("Removing deadddd assignment %d\n", def->linear->index);
            }
        }
nextDef:
        def = def->globalnext;
    }

    return result;
}

static Boolean PropagateOtherSelfAssignments(void) {
    IRODef* def;
    Boolean result;

    result = FALSE;

    def = FirstDef;
    while (def) {
        if (def->linear &&
            def->definite &&
            IsOtherSelfAssignment(def->linear) &&
            !(def->linear->flags & IROLF_Reffed) &&
            IS_TYPE_INT_OR_ENUM(def->linear->rtype) &&
            IRO_TypesEqual(def->linear->rtype, def->var->object->type) &&
            !is_volatile_object(def->var->object)) {
            IROUse* use;
            IROUse* use2;
            IROLinear* father;
            SInt32 i;

            for (use = def->var->uses, i = 0; use; use = use->varnext) {
                if (Bv_IsBitSet(def->index, use->reachingDefs)) {
                    if (use->reachingDefCount == 1 &&
                        use->linear->type == IROLinearOperand &&
                        (father = IRO_LocateFather(use->linear)) &&
                        IRO_IsVariable(father) &&
                        IRO_TypesEqual(def->linear->rtype, father->rtype)) {
                        if (use->linear->flags & IROLF_Assigned) {
                            if (!((father = IRO_LocateFather(father)) &&
                                  IsOtherSelfAssignment(father) &&
                                  (father->nodetype == def->linear->nodetype) &&
                                  !(father->flags & IROLF_Reffed))) {
                                goto nextDef;
                            }
                        }
                        i++;
                    } else {
                        goto nextDef;
                    }
                }
            }

            if (i == def->useCount) {
                for (use = def->var->uses; use; use = use->varnext) {
                    if (Bv_IsBitSet(def->index, use->reachingDefs)) {
                        IRO_Dump(
                            "Propagating self assignment from %d to %d\n",
                            def->linear->index,
                            use->linear->index
                        );
                        father = IRO_LocateFather(use->linear);
#if VERSION < VERSION_GC_2_5
                        UpdateUseForOtherSelfAssignment(father, def->linear, def->linear->rtype);
#else
                        UpdateUseForOtherSelfAssignment(father, def->linear, father->rtype);
#endif

                        result = TRUE;
                        for (use2 = def->var->uses; use2; use2 = use2->varnext) {
                            if (use2->linear == def->linear->u.monadic->u.diadic.left) {
                                use->reachingDefCount = use2->reachingDefCount;
                                Bv_Copy(use2->reachingDefs, use->reachingDefs);
                                break;
                            }
                        }
                    }
                }
                def->useCount = 0;
                IRO_NopNonSideEffects(def->linear, -1);
                def->linear->type = IROLinearNop;
                IRO_Dump("Removing deadddd assignment %d\n", def->linear->index);
            }
        }
nextDef:
        def = def->globalnext;
    }

    return result;
}

#if VERSION < VERSION_GC_2_5
// 2.0's file has 159 lines fewer above here
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -159
#endif
static void MarkUsesByIndirect(IROLinear* linear, BitVector* a, BitVector* b) {
    IROListNode* scan;
    Object* obj;
    VarRecord* var;
    Boolean foundObjRef;
    IROLinear* nd;
    IRODef* def;
    Boolean found;

    found = FALSE;

    if (linear && copts.opt_pointer_analysis && linear->pointsToFunction && FunctionName) {
        IROListNode* resultList;
        resultList = NULL;
        PointerAnalysis_LookupLinearNodePointerExpr(FunctionName, linear, &resultList);
        if (resultList) {
            for (scan = resultList; scan; scan = scan->nextList) {
                if (!scan->list.head || !scan->list.tail) {
                    found = TRUE;
                    break;
                }

                foundObjRef = FALSE;
                for (nd = scan->list.head; nd != scan->list.tail->next; nd = nd->next) {
                    if (nd->type == IROLinearOperand && nd->u.node->type == EOBJREF) {
                        foundObjRef = TRUE;
                        break;
                    }
                }

                if (!foundObjRef) {
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                for (scan = resultList; scan; scan = scan->nextList) {
                    for (nd = scan->list.head; nd != scan->list.tail->next; nd = nd->next) {
                        if (nd->type == IROLinearOperand && nd->u.node->type == EOBJREF) {
                            obj = nd->u.node->data.objref;
                            CError_ASSERT(1581, obj != NULL);
                            var = IRO_FindVar(obj, TRUE, TRUE);
                            CError_ASSERT(1583, var != NULL);

                            for (def = var->defs; def; def = def->varnext) {
                                if (def->global ||
                                    (def->noregister && Bv_IsBitSet(def->index, Reaches))) {
                                    def->useCount++;
                                    Bv_SetBit(def->index, MightReachAnyUse);
                                }
                            }
                        }
                    }
                }
            }

            while (resultList) {
                IROListNode* next = resultList->nextList;
                IRO_free(resultList);
                resultList = next;
            }
        } else {
            found = TRUE;
        }
    } else {
        found = TRUE;
    }

    if (found) {
        Bv_Copy(Reaches, a);
        Bv_And(b, a);
        Bv_Or(a, MightReachAnyUse);
        for (def = FirstDef; def; def = def->globalnext) {
            if (def->global || (def->noregister && Bv_IsBitSet(def->index, Reaches))) {
                def->useCount++;
            }
        }
    }
}

static void MarkUsesByFunctionCall(IROLinear* linear, BitVector* a, BitVector* b) {
    ObjectList* olist;
    IROLinear* funcnd;
    IROLinear* nd;
    Object* obj;
    VarRecord* var;
    Boolean foundObjRef;
    IROListNode* scan;
    IRODef* def;
    Boolean found;

    found = FALSE;

    if ((funcnd = linear->u.funccall.callee) &&
        copts.opt_pointer_analysis &&
        funcnd->pointsToFunction &&
        FunctionName) {
        IROListNode* resultList;
        ObjectList* depsList;

        resultList = NULL;
        PointerAnalysis_LookupLinearNodePointerExpr(FunctionName, funcnd, &resultList);
        if (resultList) {
            for (scan = resultList; scan; scan = scan->nextList) {
                if (!scan->list.head || !scan->list.tail) {
                    found = TRUE;
                    break;
                }

                foundObjRef = FALSE;
                for (nd = scan->list.head; nd != scan->list.tail->next; nd = nd->next) {
                    if (nd->type == IROLinearOperand && nd->u.node->type == EOBJREF) {
                        foundObjRef = TRUE;
                        obj = nd->u.node->data.objref;
                        CError_ASSERT(1681, obj != NULL);

                        depsList = NULL;
                        PointerAnalysis_GetFunctionDependencies(obj, linear, &depsList);
                        for (olist = depsList; olist; olist = olist->next) {
                            if (!olist->object) {
                                found = TRUE;
                                break;
                            }
                        }

                        while (depsList) {
                            olist = depsList->next;
                            IRO_free(depsList);
                            depsList = olist;
                        }

                        if (found) {
                            break;
                        }
                    }
                }

                if (!foundObjRef) {
                    found = TRUE;
                }
                if (found) {
                    break;
                }
            }

            if (!found) {
                for (scan = resultList; scan; scan = scan->nextList) {
                    for (nd = scan->list.head; nd != scan->list.tail->next; nd = nd->next) {
                        if (nd->type == IROLinearOperand && nd->u.node->type == EOBJREF) {
                            obj = nd->u.node->data.objref;
                            depsList = NULL;
                            PointerAnalysis_GetFunctionDependencies(obj, linear, &depsList);

                            for (olist = depsList; olist; olist = olist->next) {
                                var = IRO_FindVar(olist->object, TRUE, TRUE);
                                CError_ASSERT(1732, var != NULL);

                                for (def = var->defs; def; def = def->varnext) {
                                    if (def->global ||
                                        (def->noregister && Bv_IsBitSet(def->index, Reaches))) {
                                        def->useCount++;
                                        Bv_SetBit(def->index, MightReachAnyUse);
                                    }
                                }
                            }

                            while (depsList) {
                                olist = depsList->next;
                                IRO_free(depsList);
                                depsList = olist;
                            }
                        }
                    }
                }
            }

            while (resultList) {
                IROListNode* next = resultList->nextList;
                IRO_free(resultList);
                resultList = next;
            }
        } else {
            found = TRUE;
        }
    } else {
        found = TRUE;
    }

    if (found) {
        Bv_Copy(Reaches, a);
        Bv_And(b, a);
        Bv_Or(a, MightReachAnyUse);
        for (def = FirstDef; def; def = def->globalnext) {
            if (def->global || (def->noregister && Bv_IsBitSet(def->index, Reaches))) {
                def->useCount++;
            }
        }
    }

    if (linear->stmt && IRO_FunctionCallMightThrowException(linear)) {
        IRO_WalkExcActions(linear->stmt->dobjstack, MarkUses);
    }
}

static Boolean UseOfVarIsInsideASimpleDefOfVar(IROUse* use) {
    VarRecord* var;
    VarRecord* varAss;
    IROLinear* nd;
    IROLinear* rhs; // NOTE: invented name

    if ((var = use->var)) {
        if ((nd = use->linear) && (nd->flags & IROLF_InCallArg)) {
            return FALSE;
        }

        while (nd && (nd->flags & IROLF_Reffed)) {
            nd = nd->next;
        }

        if (nd &&
            IRO_IsAssignment(nd) &&
            (varAss = IRO_FindAssigned(nd)) &&
            varAss == var &&
            !IRO_HasSideEffect((nd->type == IROLinearOp1Arg) ? nd->u.monadic : nd->u.diadic.left) &&
            (!(rhs = (nd->type == IROLinearOp1Arg) ? NULL : nd->u.diadic.right) ||
             !IRO_HasSideEffect(rhs))) {
            return TRUE;
        }
    }

    return FALSE;
}

static Boolean AllLoopDefUsesAreInSimpleLoopDefs(IRODef* def) {
    VarRecord* var;
    IRODef* scan;
    IROUse* use;

    if ((var = def->var)) {
        for (scan = var->defs; scan; scan = scan->varnext) {
            if (scan->node && scan->node->loopdepth) {
                for (use = var->uses; use; use = use->varnext) {
                    if (Bv_IsBitSet(scan->index, use->reachingDefs)) {
                        if (!use->node ||
                            !use->node->loopdepth ||
                            !UseOfVarIsInsideASimpleDefOfVar(use)) {
                            return FALSE;
                        }
                    }
                }
            }
        }
        return TRUE;
    } else {
        return FALSE;
    }
}

static void MarkRemovableLoopDefs(void) {
    VarRecord* var;
    IRODef* def;
    IRODef* scan;

    for (def = FirstDef; def; def = def->globalnext) {
        if (!def->global &&
            !def->noregister &&
            def->node &&
            def->node->loopdepth &&
            !def->removable &&
            AllLoopDefUsesAreInSimpleLoopDefs(def) &&
            (var = def->var)) {
            for (scan = var->defs; scan; scan = scan->varnext) {
                if (scan->node && scan->node->loopdepth) {
                    scan->removable = TRUE;
                }
            }
        }
    }
}

static Boolean EliminateReffedDeadStore(IRODef* def) {
    Boolean isPostIncOrDec;
    IROLinear* nd;
    Boolean result;

    nd = def->linear;
    isPostIncOrDec =
        (nd->type == IROLinearOp1Arg) && (nd->nodetype == EPOSTINC || nd->nodetype == EPOSTDEC);

    result = IRO_LocateFather(nd) != NULL;
    if (result && IRO_IsAssignment(nd) && IRO_IsModifyOp[nd->nodetype]) {
        result = IRO_TransformSelfAssignmentToAssignment(nd);
    }
    result = result && (nd->type == IROLinearOp2Arg) && (nd->nodetype == EASS);

    if (result) {
        def->useCount = 0;
        IRO_Dump("Removing referenced dead assignment %d\n", nd->index);

        if (isPostIncOrDec) {
            IRO_NopNonSideEffects(nd->u.diadic.right, 0);
            nd->type = IROLinearNop;
            IRO_LocateFather_Cut_And_Paste_Without_Nopping(nd, nd->u.diadic.left);
        } else {
            IRO_NopNonSideEffects(nd->u.diadic.left, 0);
            nd->type = IROLinearNop;
            IRO_LocateFather_Cut_And_Paste_Without_Nopping(nd, nd->u.diadic.right);
        }
    }

    return result;
}

Boolean IRO_UseDef(Boolean optDeadAssignments, Boolean optPropagation) {
    VarRecord* var;
    Boolean result;
    IRODef* gdef;
    IROUse* guse;
    IRODef* def;
    IROUse* use;
    IRONode* node;
    IROLinear* nd;
    Boolean changed; // NOTE: invented name
    BitVector* bv3;
    BitVector* bv2;
    BitVector* bv;
    int i;

    FindDefsAndUses();

    IRO_CheckForUserBreak();

    for (use = IRO_FirstVarUse; use; use = use->globalnext) {
        Bv_AllocVector(&use->reachingDefs, NumDefs);
    }
    Bv_AllocVector(&MightReachAnyUse, NumDefs);
    Bv_AllocVector(&bv, NumDefs);

    IRO_CheckForUserBreak();

    for (gdef = FirstDef; gdef; gdef = gdef->globalnext) {
        if (gdef->global || gdef->noregister) {
            Bv_SetBit(gdef->index, bv);
        }
    }

    Bv_AllocVector(&bv2, NumDefs);
    gdef = FirstDef;
    for (node = IRO_FirstNode; node; node = node->nextnode) {
        Bv_AllocVector(&node->in, NumDefs);
        Bv_AllocVector(&node->gen, NumDefs);
        Bv_AllocVector(&node->kill, NumDefs);
        Bv_AllocVector(&node->out, NumDefs);

        for (nd = node->first; nd; nd = nd->next) {
            while (gdef && gdef->linear == nd) {
                Bv_SetBit(gdef->index, node->gen);
                if (gdef->definite) {
                    for (def = gdef->var->defs; def; def = def->varnext) {
                        if (def != gdef) {
                            Bv_SetBit(def->index, node->kill);
                            Bv_ClearBit(def->index, node->gen);
                        }
                    }
                }
                gdef = gdef->globalnext;
            }
            if (nd == node->last) {
                break;
            }
        }

        if (node->numpred) {
            Bv_Set(node->in);
        }
        Bv_Copy(node->gen, node->out);
        IRO_CheckForUserBreak();
    }

    Bv_AllocVector(&bv3, NumDefs);

    do {
        changed = FALSE;
        for (node = IRO_FirstNode; node; node = node->nextnode) {
            Bv_Clear(node->in);
            if (node->numpred) {
                for (i = 0; i < node->numpred; i++) {
                    Bv_Or(IRO_NodeTable[node->pred[i]]->out, node->in);
                }
            } else if (node == IRO_FirstNode) {
                for (var = IRO_FirstVar; var; var = var->next) {
                    Bv_SetBit(var->defs->index, node->in);
                }
            }
            Bv_Copy(node->in, bv3);
            Bv_Minus(node->kill, bv3);
            Bv_Or(node->gen, bv3);
            if (!Bv_Compare(bv3, node->out)) {
                Bv_Copy(bv3, node->out);
                changed = TRUE;
            }
        }

        IRO_CheckForUserBreak();
    } while (changed);

    gdef = FirstDef;
    guse = IRO_FirstVarUse;
    Bv_AllocVector(&Reaches, NumDefs);

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        Bv_Copy(node->in, Reaches);
        nd = node->first;
        while (1) {
            while (guse && guse->linear == nd) {
                for (def = guse->var->defs; def; def = def->varnext) {
                    if (Bv_IsBitSet(def->index, Reaches)) {
                        Bv_SetBit(def->index, guse->reachingDefs);
                        Bv_SetBit(def->index, MightReachAnyUse);
                        def->useCount++;
                        guse->reachingDefCount++;
                    }
                }
                guse = guse->globalnext;
            }

            if (nd->type == IROLinearFunccall) {
                MarkUsesByFunctionCall(nd, bv2, bv);
            } else if (nd->type == IROLinearOp1Arg &&
                       nd->nodetype == EINDIRECT &&
                       !IRO_IsVariable(nd)) {
                MarkUsesByIndirect(nd, bv2, bv);
            } else if ((nd->type == IROLinearFunccall) ||
                       (nd->type == IROLinearOp1Arg &&
                        nd->nodetype == EINDIRECT &&
                        !IRO_IsVariable(nd))) {
                // (never reached: the branches above take both cases)
                IRODef* d;
                Bv_Copy(Reaches, bv2);
                Bv_And(bv, bv2);
                Bv_Or(bv2, MightReachAnyUse);
                for (d = FirstDef; d; d = d->globalnext) {
                    if (d->global || (d->noregister && Bv_IsBitSet(d->index, Reaches))) {
                        d->useCount++;
                    }
                }
                if (nd->type == IROLinearFunccall &&
                    nd->stmt &&
                    IRO_FunctionCallMightThrowException(nd)) {
                    IRO_WalkExcActions(nd->stmt->dobjstack, MarkUses);
                }
            }

            if (nd->type == IROLinearAsm) {
                IAEffects effects;
                CodeGen_GetAsmEffects(nd->u.asm_stmt, &effects);
                if (effects.readsMemory) {
                    Bv_Copy(Reaches, bv2);
                    Bv_And(bv, bv2);
                    Bv_Or(bv2, MightReachAnyUse);
                }
            }

            if (nd->type == IROLinearReturn || nd->type == IROLinearEnd) {
                for (def = FirstDef; def; def = def->globalnext) {
                    if (def->global && Bv_IsBitSet(def->index, Reaches)) {
                        Bv_SetBit(def->index, MightReachAnyUse);
                        def->useCount++;
                    }
                }
            }

            while (gdef && gdef->linear == nd) {
                if (gdef->definite) {
                    for (def = gdef->var->defs; def; def = def->varnext) {
                        Bv_ClearBit(def->index, Reaches);
                    }
                }
                Bv_SetBit(gdef->index, Reaches);
                gdef = gdef->globalnext;
            }

            if (nd == node->last) {
                break;
            }
            nd = nd->next;
        }
    }

    IRO_CheckForUserBreak();

    result = FALSE;

    if (optDeadAssignments) {
        MarkRemovableLoopDefs();
        for (def = FirstDef; def; def = def->globalnext) {
            if (def->linear &&
                (!Bv_IsBitSet(def->index, MightReachAnyUse) || def->removable) &&
                (copts.opt_pointer_analysis || !def->noregister) &&
                def->linear->type != IROLinearAsm &&
                (!def->linear->rtype ||
                 !CParser_IsVolatile(
                     def->linear->rtype, def->linear->nodeflags & ENODE_FLAG_QUALS
                 )) &&
                !is_volatile_object(def->var->object)) {
                if (!(def->linear->flags & IROLF_Reffed)) {
                    def->useCount = 0;
                    IRO_NopNonSideEffects(def->linear, -1);
                    def->linear->type = IROLinearNop;
                    IRO_Dump("Removing dead assignment %d\n", def->linear->index);
                    result = TRUE;
                } else {
                    result = EliminateReffedDeadStore(def);
                }
            }
        }
    }

    IRO_CheckForUserBreak();

    if (optPropagation) {
        while (1) {
            if (PropagateIncsAndDecs() || PropagateOtherSelfAssignments()) {
                result = TRUE;
            } else {
                break;
            }
        }
    }

    IRO_CheckForUserBreak();

    return result;
}

static IROLinear* GetOperand(IROLinear* nd) {
    IROLinear* inner;

    if (nd->type == IROLinearOperand) {
        return nd;
    }

    if (nd->type == IROLinearOp2Arg && nd->nodetype == EADD) {
        inner = GetOperand(nd->u.diadic.left);
        if (!inner) {
            inner = GetOperand(nd->u.diadic.right);
        }
        return inner;
    }

    return NULL;
}

static IROLinear* GetAssigned(IROLinear* nd) {
    if (!nd) {
        return NULL;
    }

    if (nd->type == IROLinearOp2Arg) {
        nd = nd->u.diadic.left;
    } else if (nd->type == IROLinearOp1Arg) {
        nd = nd->u.monadic;
    } else {
        CError_FATAL(2497);
    }

    if (nd->type != IROLinearOp1Arg || nd->nodetype != EINDIRECT) {
        CError_FATAL(2510);
    }

    nd = nd->u.monadic;

    // a bitfield's address
    if (nd->type == IROLinearOp1Arg && nd->nodetype == EBITFIELD) {
        nd = nd->u.monadic;
    }

    if (nd->type == IROLinearOp2Arg && nd->nodetype == EADD) {
        nd = GetOperand(nd);
    }

    return nd;
}

static void AddUseToRange(IROUse* use) {
    IRODef* def;

    Bv_SetBit(use->index, useset);
    Bv_ClearBit(use->index, alluses);

    for (def = use->var->defs; def; def = def->varnext) {
        if (Bv_IsBitSet(def->index, alldefs) &&
            (Bv_IsBitSet(def->index, use->reachingDefs) ||
             GetAssigned(def->linear) == use->linear)) {
            AddDefToRange(def);
        }
    }
}

static void AddDefToRange(IRODef* def) {
    IROUse* use;
    IROLinear* assigned;

    Bv_SetBit(def->index, defset);
    Bv_ClearBit(def->index, alldefs);

    for (use = def->var->uses, assigned = GetAssigned(def->linear); use; use = use->varnext) {
        if (Bv_IsBitSet(use->index, alluses) &&
            (Bv_IsBitSet(def->index, use->reachingDefs) || assigned == use->linear)) {
            AddUseToRange(use);
        }
    }
}

static void ReplaceAssigned(IROLinear* nd, Object* from, Object* to) {
    IROLinear* assigned;

    if (!(assigned = GetAssigned(nd)) ||
        assigned->type != IROLinearOperand ||
        assigned->u.node->type != EOBJREF ||
        assigned->u.node->data.objref != from) {
        CError_FATAL(2621);
    }

    assigned->u.node->data.objref = to;
}

static void ReplaceUsed(IROLinear* nd, Object* from, Object* to) {
    CError_ASSERT(2644, nd->type == IROLinearOperand && nd->u.node->type == EOBJREF);

    if (nd->u.node->data.objref == from) {
        nd->u.node->data.objref = to;
    } else if (nd->u.node->data.objref != to) {
        CError_FATAL(2656);
    }
}

static void SplitOffRange(VarRecord* var) {
    Object* newObj;
    IRODef* def;
    IROUse* use;

    IRO_Dump("Splitting range for variable: %d\n", var->index);
    IRO_DumpBits("Def set: ", defset);
    IRO_DumpBits("Use set: ", useset);
    IRO_DumpBits("All defs: ", alldefs);
    IRO_DumpBits("All uses: ", alluses);

    newObj = create_temp_object(var->object->type);
    IRO_FindVar(newObj, TRUE, TRUE);

    for (def = var->defs; def; def = def->varnext) {
        if (Bv_IsBitSet(def->index, defset) && def->linear) {
            ReplaceAssigned(def->linear, var->object, newObj);
        }
    }

    for (use = var->uses; use; use = use->varnext) {
        if (Bv_IsBitSet(use->index, useset)) {
            ReplaceUsed(use->linear, var->object, newObj);
        }
    }
}

void IRO_SplitLifetimes(void) {
    VarRecord* var;
    SInt32 numVars;
    IRODef* def;
    IROUse* use;
    Boolean firstRange; // NOTE: invented name

    Bv_AllocVector(&alldefs, NumDefs);
    Bv_AllocVector(&alluses, NumUses);
    Bv_AllocVector(&defset, NumDefs);
    Bv_AllocVector(&useset, NumUses);

    var = IRO_FirstVar;
    numVars = IRO_NumVars;

    while (var && var->index <= numVars) {
        if (var->object->datatype == DLOCAL &&
            !is_volatile_object(var->object) &&
            !var->usedAtCall &&
            !var->noregister &&
            !var->bitFieldReplacement) {
            Bv_Clear(alldefs);
            Bv_Clear(alluses);

            for (def = var->defs; def; def = def->varnext) {
                if (def->linear && def->linear->type == IROLinearAsm) {
                    goto skipThisVar;
                }
                Bv_SetBit(def->index, alldefs);
            }

            for (use = var->uses; use; use = use->varnext) {
                if (use->linear && use->linear->type == IROLinearAsm) {
                    goto skipThisVar;
                }
                Bv_SetBit(use->index, alluses);
            }

            firstRange = TRUE;
            while (1) {
                Bv_Clear(defset);
                Bv_Clear(useset);

                def = var->defs;
                while (def && !Bv_IsBitSet(def->index, alldefs)) {
                    def = def->varnext;
                }

                if (!def) {
                    break;
                }

                AddDefToRange(def);
                if (Bv_IsEmpty(alldefs)) {
                    break;
                }

                if (!firstRange) {
                    SplitOffRange(var);
                }
                firstRange = FALSE;
            }
        }

skipThisVar:
        var = var->next;
    }

    IRO_CheckForUserBreak();
}
