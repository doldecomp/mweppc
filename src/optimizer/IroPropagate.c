#include "compiler/IroPropagate.h"
#include "compiler/IroCSE.h"
#include "compiler/IroDump.h"
#include "compiler/IroEval.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IROUseDef.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/BitVector.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/enode.h"
#include "compiler/InlineAsm.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/objects.h"
#include "compiler/types.h"

IROAssign* IRO_FirstAssign;
IROAssign* IRO_LastAssign;
SInt32 IRO_NumAssign;

static Boolean VarIsUsedInOtherFNodes(VarRecord* var, IRONode* node) {
    if (var->defs && var->uses) {
        IROUse* use;
        IRODef* def = NULL;
        for (use = var->uses; use; use = use->varnext) {
            if (!def &&
                use->node == node &&
                use->linear &&
                !(use->linear->flags & IROLF_Assigned)) {
                for (def = var->defs; def; def = def->varnext) {
                    if (Bv_IsBitSet(def->index, use->reachingDefs)) {
                        break;
                    }
                }
            }
        }

        if (def) {
            for (use = var->uses; use; use = use->varnext) {
                if (use->node != node && use->linear && !(use->linear->flags & IROLF_Assigned)) {
                    if (Bv_IsBitSet(def->index, use->reachingDefs)) {
                        return TRUE;
                    }
                }
            }
        }
    } else {
        IROLinear* linear;
        IRONode* scan;
        for (scan = IRO_FirstNode; scan; scan = scan->nextnode) {
            if (scan != node) {
                linear = scan->first;
                while (1) {
                    if (!linear) {
                        break;
                    }

                    if (IRO_IsVariable(linear) && !(linear->flags & IROLF_Assigned)) {
                        if (IRO_FindVar(linear->u.diadic.left->u.node->data.objref, FALSE, TRUE) ==
                            var) {
                            return TRUE;
                        }
                    }

                    if (linear == scan->last) {
                        break;
                    }
                    linear = linear->next;
                }
            }
        }
        IRO_CheckForUserBreak();
    }

    return FALSE;
}

static void GetExprUses(IROLinear* linear, Boolean isFirst) {
    if (isFirst && IS_LINEAR_ENODE(linear, EOBJREF)) {
        Object* obj = linear->u.node->data.objref;
        if ((linear->flags & IROLF_Ind) &&
            (!(linear->flags & IROLF_Assigned) || (linear->flags & IROLF_Used))) {
            VarRecord* var = IRO_FindVar(obj, FALSE, TRUE);
            if (var) {
                Bv_SetBit(var->index, IRO_VarKills);
            }
        }
    }
}

static void GetExprKills(IROLinear* linear, Boolean isFirst) {
    if (isFirst) {
        IRO_GetKills(linear);
    }
}

static void CheckUnorderedRegionForDefs(
    IROLinear* linear, BitVector* kills, BitVector* uses, Boolean* found
) {
    Bv_Clear(kills);
    Bv_Clear(IRO_VarKills);
    IRO_WalkTree(linear, GetExprKills);
    Bv_Or(IRO_VarKills, kills);
    if (Bv_BitsInCommon(kills, uses)) {
        *found = TRUE;
    }
}

static void CheckUnorderedRegionsForDefs(
    IROLinear* nd, IROLinear* excluded, BitVector* kills, BitVector* uses, Boolean* found
) {
    int i;

    switch (nd->type) {
        case IROLinearOp2Arg:
            if (nd->nodetype == ELAND) {
                break;
            }
            if (nd->nodetype == ELOR) {
                break;
            }
            if (nd->nodetype == ECOMMA) {
                break;
            }
            if (nd->u.diadic.left != excluded) {
                CheckUnorderedRegionForDefs(nd->u.diadic.left, kills, uses, found);
            }
            if (nd->u.diadic.right != excluded) {
                CheckUnorderedRegionForDefs(nd->u.diadic.right, kills, uses, found);
            }
            break;
        case IROLinearFunccall:
            if (nd->u.funccall.callee != excluded) {
                CheckUnorderedRegionForDefs(nd->u.funccall.callee, kills, uses, found);
            }
            for (i = 0; !*found && i < nd->u.funccall.argCount; i++) {
                if (nd->u.funccall.args[i] != excluded) {
                    CheckUnorderedRegionForDefs(nd->u.funccall.args[i], kills, uses, found);
                }
            }
            break;
        default:
            break;
    }
}

static Boolean PropagationHasDefsInUnorderedRegions(IROLinear* use, IROLinear* value) {
    IROLinear* father;
    BitVector* kills;
    BitVector* uses;
    Boolean found;
    VarRecord* var;
    IROLinear* linear;

    found = FALSE;
    linear = use;
    if ((father = IRO_LocateFather(use))) {
        Bv_AllocVector(&kills, IRO_NumVars + 1);
        Bv_AllocVector(&uses, IRO_NumVars + 1);
        Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);
        IRO_WalkTree(value, GetExprUses);
        Bv_Or(IRO_VarKills, uses);
        var = IRO_FindVar(use->u.diadic.left->u.node->data.objref, FALSE, TRUE);
        Bv_SetBit(var->index, uses);
        while (father && !found) {
            CheckUnorderedRegionsForDefs(father, linear, kills, uses, &found);
            linear = father;
            father = IRO_LocateFather(father);
        }
    }

    return found;
}

int IRO_IsRegable(Object* obj) {
    if (obj->datatype == DLOCAL && obj->u.var.info) {
        return !obj->u.var.info->noregister;
    }
    return FALSE;
}

static Boolean IsPropagatable(IROLinear* linear) {
    Object* obj;
    Object* srcObj;

    if (IS_LINEAR_DIADIC(linear, EASS) &&
        (obj = IRO_IsVariable(linear->u.diadic.left)) &&
        obj->type == linear->rtype &&
        !is_volatile_object(obj)) {
        if (linear->u.diadic.left->flags & IROLF_BitfieldIndirect) {
            if (!IRO_IsConstant(linear->u.diadic.right)) {
                return FALSE;
            } else {
                return TRUE;
            }
        }

        if (IRO_IsConstant(linear->u.diadic.right)) {
            return TRUE;
        }

        if ((srcObj = IRO_IsVariable(linear->u.diadic.right)) &&
            !is_volatile_object(srcObj) &&
            IRO_TypesEqual(obj->type, linear->rtype)) {
            if (!Inline_IsObjectData(linear->u.diadic.right->u.monadic->u.node->data.objref)) {
#if VERSION == VERSION_GC_2_0P2
                if (obj->datatype == DLOCAL && obj->u.var.info && obj->u.var.info->noregister) {
                    return FALSE;
                }
#endif
                if (IRO_IsRegable(obj) && !IRO_IsRegable(srcObj)) {
                    return FALSE;
                }
                return TRUE;
            }
        }
    }

    return FALSE;
}

static Boolean IsIntGenKillCandidate(IROLinear* linear) {
    if (linear->type == IROLinearFunccall) {
        return TRUE;
    }

    if (IRO_IsAssignOp[linear->nodetype] &&
        (linear->type == IROLinearOp1Arg || linear->type == IROLinearOp2Arg)) {
        return TRUE;
    }

    if (linear->type == IROLinearAsm) {
        return TRUE;
    }

    return FALSE;
}

static Boolean IsPropagatableExpr(IROLinear* linear, IROList* list) {
    Object* obj;
    if (IS_LINEAR_DIADIC(linear, EASS) && !(linear->flags & IROLF_Reffed)) {
        if ((obj = IRO_IsVariable(linear->u.diadic.left)) &&
            !is_volatile_object(obj) &&
            !(linear->u.diadic.left->flags & IROLF_BitfieldIndirect)) {
            if (!IRO_IsRegable(obj)) {
                return FALSE;
            }
            if (IS_LINEAR_ENODE(linear->u.diadic.right, ESTRINGCONST)) {
                return FALSE;
            }

            IRO_FindDepends(linear->u.diadic.right);
            if (!IRO_NotSubable) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static void ClearAvailibilityOnNode(IRONode* node, UInt32 bit) {
    IRONode* scan;

    for (scan = node->nextnode; scan; scan = scan->nextnode) {
        Bv_ClearBit(bit, scan->in);
    }
}

Boolean IRO_CopyAndConstantPropagation(void) {
    IRONode* node;
    IROAssign* ass2;
    IROAssign* ass;
    IROLinear* linear;
    BitVector* bv;
    Boolean changed;
    Boolean result;
    IROAssign* newass;
    VarRecord* var;

    IRO_FirstAssign = NULL;
    IRO_NumAssign = 0;
    result = FALSE;

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        linear = node->first;
        while (1) {
            if (!linear) {
                break;
            }

            if (IsPropagatable(linear) && (var = IRO_FindAssigned(linear))) {
                IRO_Dump("Found propagatable assignment at: %d\n", linear->index);
                newass = oalloc(sizeof(IROAssign));
                newass->linear = linear;
                newass->next = NULL;
                newass->index = ++IRO_NumAssign;
                newass->varIndex = var->index;
                newass->linear2 = linear->u.diadic.right;
                newass->var = NULL;
                newass->varObj = IRO_IsVariable(linear->u.diadic.right);
                newass->node = node;
                if (newass->varObj) {
                    newass->var = IRO_FindVar(newass->varObj, FALSE, TRUE);
                }
                if (!IRO_FirstAssign) {
                    IRO_FirstAssign = newass;
                } else {
                    IRO_LastAssign->next = newass;
                }
                IRO_LastAssign = newass;
            }

            if (linear == node->last) {
                break;
            }
            linear = linear->next;
        }
    }

    IRO_CheckForUserBreak();

    node = IRO_FirstNode;
    ass = IRO_FirstAssign;
    Bv_AllocVector(&IRO_VarKills, IRO_NumVars + 1);
    while (node) {
        Bv_AllocVector(&node->in, IRO_NumAssign);
        if (node != IRO_FirstNode) {
            Bv_Set(node->in);
        }
        Bv_AllocVector(&node->kill, IRO_NumAssign);
        Bv_AllocVector(&node->gen, IRO_NumAssign);
        Bv_AllocVector(&node->copyOut, IRO_NumAssign);
        node->out = NULL;

        linear = node->first;
        while (1) {
            if (!linear) {
                break;
            }

            if (IsIntGenKillCandidate(linear)) {
                Bv_Clear(IRO_VarKills);
                IRO_GetKills(linear);

                for (ass2 = IRO_FirstAssign; ass2; ass2 = ass2->next) {
                    if (Bv_IsBitSet(ass2->varIndex, IRO_VarKills)) {
                        Bv_SetBit(ass2->index, node->kill);
                        Bv_ClearBit(ass2->index, node->gen);
                    }
                    if (ass2->var && Bv_IsBitSet(ass2->var->index, IRO_VarKills)) {
                        Bv_SetBit(ass2->index, node->kill);
                        Bv_ClearBit(ass2->index, node->gen);
                    }
                }
            }

            while (ass && ass->linear == linear) {
                Bv_SetBit(ass->index, node->gen);
                ass = ass->next;
            }

            if (linear == node->last) {
                break;
            }
            linear = linear->next;
        }

        Bv_Copy(node->in, node->copyOut);
        Bv_Minus(node->kill, node->copyOut);
        Bv_Or(node->gen, node->copyOut);
        node = node->nextnode;
    }

    IRO_CheckForUserBreak();

    Bv_AllocVector(&bv, IRO_NumAssign);
    do {
        changed = FALSE;
        for (node = IRO_FirstNode; node; node = node->nextnode) {
            if (node == IRO_FirstNode) {
                Bv_Clear(bv);
            } else {
                UInt16 i;
                Bv_Set(bv);
                for (i = 0; i < node->numpred; i++) {
                    Bv_And(IRO_NodeTable[node->pred[i]]->copyOut, bv);
                }
            }

            if (!Bv_Compare(bv, node->in)) {
                changed = TRUE;
                Bv_Copy(bv, node->in);
            }

            Bv_Copy(node->in, node->copyOut);
            Bv_Minus(node->kill, node->copyOut);
            Bv_Or(node->gen, node->copyOut);
        }
    } while (changed);

    IRO_CheckForUserBreak();

    node = IRO_FirstNode;
    ass = IRO_FirstAssign;
    while (node) {
        IRO_Avail = node->in;
        linear = node->first;
        while (1) {
            if (!linear) {
                break;
            }

            if (IRO_IsVariable(linear) && !(linear->flags & IROLF_Assigned)) {
                if ((var = IRO_FindVar(linear->u.diadic.left->u.node->data.objref, FALSE, TRUE))) {
                    for (ass2 = IRO_FirstAssign; ass2; ass2 = ass2->next) {
                        if (ass2->varIndex == var->index &&
                            Bv_IsBitSet(ass2->index, IRO_Avail) &&
                            (IRO_IsConstant(ass2->linear2) ||
                             node->loopdepth <= ass2->node->loopdepth) &&
                            !PropagationHasDefsInUnorderedRegions(linear, ass2->linear2)) {
                            if (ass2->linear2->type == IROLinearOperand &&
                                IRO_is_CPtypeequal(
                                    linear->rtype, ass2->linear->u.diadic.left->rtype
                                )) {
                                ENode* enode = IRO_NewENode(ass2->linear2->u.node->type);
                                *enode = *ass2->linear2->u.node;
                                if (enode->type == EINTCONST) {
                                    if (linear->flags & IROLF_BitfieldIndirect) {
                                        IRO_TruncateBitfieldValueToType(
                                            &enode->data.intval, linear->rtype, var->bitfieldType
                                        );
                                        enode->rtype = linear->rtype;
                                        result = TRUE;
                                    } else {
                                        IRO_TruncateValueToType(&enode->data.intval, linear->rtype);
                                        enode->rtype = linear->rtype;
                                    }
                                }
                                linear->u.monadic->type = IROLinearNop;
                                linear->type = IROLinearOperand;
                                linear->u.node = enode;
                            } else if (ass2->varObj &&
                                       IRO_is_CPtypeequal(linear->rtype, ass2->linear->rtype)) {
                                linear->u.diadic.left->u.node = create_objectrefnode(ass2->varObj);
                                if (ass2->linear2->flags & IROLF_BitfieldIndirect) {
                                    linear->flags |= IROLF_BitfieldIndirect;
                                }
                            }
                            IRO_Dump(
                                "Found propagation at %d from %d\n",
                                linear->index,
                                ass2->linear->index
                            );
                            break;
                        }
                    }
                }
            } else if (linear->type == IROLinearAsm) {
                IAEffects effects;
                int i;
                CodeGen_GetAsmEffects(linear->u.asm_stmt, &effects);

                for (i = 0; i < effects.numoperands; i++) {
                    if (effects.operands[i].type == IAEffect_Read &&
                        effects.operands[i].offset == 0 &&
                        effects.operands[i].object->type->size == effects.operands[i].size) {
                        if ((var = IRO_FindVar(effects.operands[i].object, FALSE, TRUE))) {
                            for (ass2 = IRO_FirstAssign; ass2; ass2 = ass2->next) {
                                if (ass2->varIndex == var->index &&
                                    Bv_IsBitSet(ass2->index, IRO_Avail) &&
                                    ass2->linear->rtype->size == effects.operands[i].size) {
                                    ENode* enode;
                                    if (ass2->varObj) {
                                        enode = create_objectrefnode(ass2->varObj);
                                    } else if (ass2->linear2->type == IROLinearOperand) {
                                        enode = ass2->linear2->u.node;
                                    } else {
                                        CError_FATAL(768);
                                    }
                                    CodeGen_PropagateIntoAsm(
                                        linear->u.asm_stmt, effects.operands[i].object, enode
                                    );
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            if (IsIntGenKillCandidate(linear)) {
                Bv_Clear(IRO_VarKills);
                IRO_GetKills(linear);

                for (ass2 = IRO_FirstAssign; ass2; ass2 = ass2->next) {
                    if (Bv_IsBitSet(ass2->varIndex, IRO_VarKills)) {
                        Bv_ClearBit(ass2->index, IRO_Avail);
                    }
                    if (ass2->var && Bv_IsBitSet(ass2->var->index, IRO_VarKills)) {
                        Bv_ClearBit(ass2->index, IRO_Avail);
                    }
                }

                while (ass && ass->linear == linear) {
                    Bv_SetBit(ass->index, IRO_Avail);
                    ass = ass->next;
                }
            }

            if (linear == node->last) {
                break;
            }
            linear = linear->next;
        }
        node = node->nextnode;
    }

    IRO_CheckForUserBreak();

    return result;
}

void IRO_ExpressionPropagation(void) {
    IRONode* node;
    IROLinear* linear;
    Object* obj;
    IROAssign* ass;
    VarRecord* var;

    for (node = IRO_FirstNode; node; node = node->nextnode) {
        IRO_FirstAssign = NULL;
        IRO_LastAssign = NULL;
        IRO_NumAssign = 0;

        linear = node->first;
        while (1) {
            IROList list;
            if (!linear) {
                break;
            }

            IRO_InitList(&list);
            if (IsPropagatableExpr(linear, &list)) {
                if ((var = IRO_FindAssigned(linear))) {
                    IRO_Dump("Found propagatable expression assignment at: %d\n", linear->index);
                    ass = oalloc(sizeof(IROAssign));
                    ass->linear = linear;
                    ass->next = NULL;
                    ass->index = ++IRO_NumAssign;
                    ass->varIndex = var->index;
                    ass->linear2 = linear->u.diadic.right;
                    ass->useCount = 0;
                    ass->node = node;
                    IRO_FindDepends(linear->u.diadic.right);
                    ass->depends = IRO_Depends;
                    if (!IRO_FirstAssign) {
                        IRO_FirstAssign = ass;
                        ass->prev = NULL;
                    } else {
                        IRO_LastAssign->next = ass;
                        ass->prev = IRO_LastAssign;
                    }
                    IRO_LastAssign = ass;
                }
            }

            if (IRO_IsVariable(linear) && !(linear->flags & IROLF_Assigned)) {
                if ((var = IRO_FindVar(linear->u.monadic->u.node->data.objref, FALSE, TRUE))) {
                    for (ass = IRO_LastAssign; ass; ass = ass->prev) {
                        if (ass->varIndex == var->index) {
                            ass->useCount++;
                        }
                    }
                }
            }

            if (linear == node->last) {
                break;
            }
            linear = linear->next;
        }

        Bv_AllocVector(&IRO_Avail, IRO_NumAssign);

        linear = node->first;
        while (1) {
            if (!linear) {
                break;
            }

            if (IRO_IsVariable(linear) && !(linear->flags & IROLF_Assigned)) {
                if ((var = IRO_FindVar(linear->u.monadic->u.node->data.objref, FALSE, TRUE))) {
                    for (ass = IRO_FirstAssign; ass; ass = ass->next) {
                        if (ass->varIndex == var->index &&
                            Bv_IsBitSet(ass->index, IRO_Avail) &&
                            !PropagationHasDefsInUnorderedRegions(linear, ass->linear2)) {
                            if (ass->linear2->type == IROLinearOperand &&
                                IRO_is_CPtypeequal(
                                    linear->rtype, ass->linear->u.diadic.left->rtype
                                )) {
                                ENode* enode = IRO_NewENode(ass->linear2->u.node->type);
                                *enode = *ass->linear2->u.node;
                                if (enode->type == EINTCONST) {
                                    IRO_TruncateValueToType(&enode->data.intval, linear->rtype);
                                    enode->rtype = linear->rtype;
                                } else if (enode->type == EOBJREF) {
                                    IROLinear* father = IRO_LocateFather(linear);
                                    if (father && (father->flags & IROLF_Assigned)) {
                                        linear->flags |= IROLF_Assigned;
                                    }
                                }
                                linear->u.monadic->type = IROLinearNop;
                                linear->type = IROLinearOperand;
                                linear->u.node = enode;
                            } else if (IRO_IsVariable(ass->linear2) &&
                                       IRO_is_CPtypeequal(linear->rtype, ass->linear->rtype)) {
                                linear->u.monadic->u.node = create_objectrefnode(
                                    ass->linear2->u.monadic->u.node->data.objref
                                );
                                if (ass->linear2->flags & IROLF_BitfieldIndirect) {
                                    linear->flags |= IROLF_BitfieldIndirect;
                                }
                            } else if (IRO_is_CPtypeequal(linear->rtype, ass->linear->rtype) &&
                                       ass->useCount == 1 &&
                                       !VarIsUsedInOtherFNodes(var, node)) {
                                IROList list;
                                IRO_InitList(&list);
                                IRO_DuplicateExpr(ass->linear->u.diadic.right, &list);
                                if (IS_TYPE_FLOAT(ass->linear->rtype)) {
                                    IROLinear* cast = IRO_NewLinear(IROLinearOp1Arg);
                                    cast->index = ++IRO_NumLinear;
                                    cast->rtype = ass->linear->rtype;
                                    cast->nodetype = ETYPCON;
                                    cast->nodeflags = ENODE_FLAG_SAME_TYPE_CAST;
                                    cast->u.monadic = list.tail;
                                    IRO_AddToList(cast, &list);
                                }
                                IRO_PasteAfter(list.head, list.tail, linear);
                                IRO_LocateFather_Cut_And_Paste(linear, list.tail);
                                linear = list.tail;
                            }
                            IRO_Dump(
                                "Found expression propagation at %d from %d\n",
                                linear->index,
                                ass->linear->index
                            );
                            break;
                        }
                    }
                }
            }

            if (linear->type != IROLinearNop && IsIntGenKillCandidate(linear)) {
                Bv_Clear(IRO_VarKills);
                IRO_GetKills(linear);
                for (ass = IRO_FirstAssign; ass; ass = ass->next) {
                    if (Bv_IsBitSet(ass->varIndex, IRO_VarKills)) {
                        Bv_ClearBit(ass->index, IRO_Avail);
                    }
                    if ((obj = IRO_IsVariable(ass->linear->u.diadic.right))) {
                        if ((var = IRO_FindVar(obj, FALSE, TRUE))) {
                            if (Bv_IsBitSet(var->index, IRO_VarKills)) {
                                Bv_ClearBit(ass->index, IRO_Avail);
                            }
                        }
                    } else {
                        IROList list;
                        IRO_InitList(&list);
                        IRO_Depends = ass->depends;
                        IRO_FindDepends_NoAlloc(ass->linear->u.diadic.right);
                        if (Bv_BitsInCommon(IRO_VarKills, ass->depends)) {
                            Bv_ClearBit(ass->index, IRO_Avail);
                        }
                    }
                }

                for (ass = IRO_FirstAssign; ass; ass = ass->next) {
                    IRO_Depends = ass->depends;
                    IRO_FindDepends_NoAlloc(ass->linear->u.diadic.right);
                    if (ass->linear == linear && !Bv_IsBitSet(ass->varIndex, ass->depends)) {
                        Bv_SetBit(ass->index, IRO_Avail);
                    }
                }
            }

            if (linear == node->last) {
                break;
            }
            linear = linear->next;
        }
    }

    IRO_CheckForUserBreak();
}
