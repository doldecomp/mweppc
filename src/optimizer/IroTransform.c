#include "compiler/IroTransform.h"
#include "compiler/IroDump.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroMalloc.h"
#include "compiler/IroPointerAnalysis.h"
#include "compiler/IrOptimizer.h"
#include "compiler/IROUseDef.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CInt64.h"
#include "compiler/CIRTransform.h"
#include "compiler/CMachine.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/enode.h"
#include "compiler/types.h"
#include <string.h>

ENodeType ExprType;
ENode* IndirectRef;
Boolean FirstTime;
CInt64 OperandConst;
Object* OperandObject;
ENodeList* FirstAddend;
ENodeList* LastAddend;
// the original's .bss has AssignmentOp lowest and ComplementaryOp highest (statics are allocated in
// reverse declaration order)
static ENodeType ComplementaryOp[MAXEXPR];
static ENodeType ComplementaryOpLogical[MAXEXPR];
static ENodeType AssignmentOp[MAXEXPR];

static void DoDiadic(IROLinear* nd);

void IRO_InitializeAssignmentOpArray(void) {
    int i;

    for (i = 0; i < MAXEXPR; i++) {
        AssignmentOp[i] = MAXEXPR;
    }

    AssignmentOp[EPOSTINC] = MAXEXPR;
    AssignmentOp[EPOSTDEC] = MAXEXPR;
    AssignmentOp[EPREINC] = MAXEXPR;
    AssignmentOp[EPREDEC] = MAXEXPR;
    AssignmentOp[EINDIRECT] = MAXEXPR;
    AssignmentOp[EMONMIN] = MAXEXPR;
    AssignmentOp[EBINNOT] = MAXEXPR;
    AssignmentOp[ELOGNOT] = MAXEXPR;
    AssignmentOp[EFORCELOAD] = MAXEXPR;
    AssignmentOp[EMUL] = EMULASS;
    AssignmentOp[EMULV] = MAXEXPR;
    AssignmentOp[EDIV] = EDIVASS;
    AssignmentOp[EMODULO] = EMODASS;
    AssignmentOp[EADDV] = MAXEXPR;
    AssignmentOp[ESUBV] = MAXEXPR;
    AssignmentOp[EADD] = EADDASS;
    AssignmentOp[ESUB] = ESUBASS;
    AssignmentOp[ESHL] = ESHLASS;
    AssignmentOp[ESHR] = ESHRASS;
    AssignmentOp[ELESS] = MAXEXPR;
    AssignmentOp[EGREATER] = MAXEXPR;
    AssignmentOp[ELESSEQU] = MAXEXPR;
    AssignmentOp[EGREATEREQU] = MAXEXPR;
    AssignmentOp[EEQU] = MAXEXPR;
    AssignmentOp[ENOTEQU] = MAXEXPR;
    AssignmentOp[EAND] = EANDASS;
    AssignmentOp[EXOR] = EXORASS;
    AssignmentOp[EOR] = EORASS;
    AssignmentOp[ELAND] = MAXEXPR;
    AssignmentOp[ELOR] = MAXEXPR;
    AssignmentOp[EASS] = MAXEXPR;
    AssignmentOp[EMULASS] = MAXEXPR;
    AssignmentOp[EDIVASS] = MAXEXPR;
    AssignmentOp[EMODASS] = MAXEXPR;
    AssignmentOp[EADDASS] = MAXEXPR;
    AssignmentOp[ESUBASS] = MAXEXPR;
    AssignmentOp[ESHLASS] = MAXEXPR;
    AssignmentOp[ESHRASS] = MAXEXPR;
    AssignmentOp[EANDASS] = MAXEXPR;
    AssignmentOp[EXORASS] = MAXEXPR;
    AssignmentOp[EORASS] = MAXEXPR;
    AssignmentOp[ECOMMA] = MAXEXPR;
    AssignmentOp[EPMODULO] = MAXEXPR;
    AssignmentOp[EROTL] = MAXEXPR;
    AssignmentOp[EROTR] = MAXEXPR;
    AssignmentOp[EBCLR] = MAXEXPR;
    AssignmentOp[EBTST] = MAXEXPR;
    AssignmentOp[EBSET] = MAXEXPR;
    AssignmentOp[ETYPCON] = MAXEXPR;
    AssignmentOp[EBITFIELD] = MAXEXPR;
    AssignmentOp[EINTCONST] = MAXEXPR;
    AssignmentOp[EFLOATCONST] = MAXEXPR;
    AssignmentOp[ESTRINGCONST] = MAXEXPR;
    AssignmentOp[ECOND] = MAXEXPR;
    AssignmentOp[EFUNCCALL] = MAXEXPR;
    AssignmentOp[EFUNCCALLP] = MAXEXPR;
    AssignmentOp[EOBJREF] = MAXEXPR;
    AssignmentOp[EMFPOINTER] = MAXEXPR;
    AssignmentOp[ENULLCHECK] = MAXEXPR;
    AssignmentOp[EPRECOMP] = MAXEXPR;
    AssignmentOp[ETEMP] = MAXEXPR;
    AssignmentOp[EARGOBJ] = MAXEXPR;
    AssignmentOp[ELOCOBJ] = MAXEXPR;
    AssignmentOp[ELABEL] = MAXEXPR;
    AssignmentOp[ESETCONST] = MAXEXPR;
    AssignmentOp[ENEWEXCEPTION] = MAXEXPR;
    AssignmentOp[ENEWEXCEPTIONARRAY] = MAXEXPR;
    AssignmentOp[EOBJLIST] = MAXEXPR;
    AssignmentOp[EMEMBER] = MAXEXPR;
    AssignmentOp[ETEMPLDEP] = MAXEXPR;
    AssignmentOp[EINSTRUCTION] = MAXEXPR;
    AssignmentOp[EDEFINE] = MAXEXPR;
    AssignmentOp[EREUSE] = MAXEXPR;
    AssignmentOp[EASSBLK] = MAXEXPR;
    AssignmentOp[EVECTOR128CONST] = MAXEXPR;
    AssignmentOp[ECONDASS] = MAXEXPR;
}

void IRO_InitializeComplementaryOpArray(void) {
    int i;

    for (i = 0; i < MAXEXPR; i++) {
        ComplementaryOp[i] = MAXEXPR;
    }

    ComplementaryOp[EPOSTINC] = EPOSTDEC;
    ComplementaryOp[EPOSTDEC] = EPOSTINC;
    ComplementaryOp[EPREINC] = EPREDEC;
    ComplementaryOp[EPREDEC] = EPREINC;
    ComplementaryOp[EINDIRECT] = MAXEXPR;
    ComplementaryOp[EMONMIN] = EMONMIN;
    ComplementaryOp[EBINNOT] = EBINNOT;
    ComplementaryOp[ELOGNOT] = ELOGNOT;
    ComplementaryOp[EFORCELOAD] = MAXEXPR;
    ComplementaryOp[EMUL] = EDIV;
    ComplementaryOp[EMULV] = MAXEXPR;
    ComplementaryOp[EDIV] = EMUL;
    ComplementaryOp[EMODULO] = MAXEXPR;
    ComplementaryOp[EADDV] = ESUBV;
    ComplementaryOp[ESUBV] = EADDV;
    ComplementaryOp[EADD] = ESUB;
    ComplementaryOp[ESUB] = EADD;
    ComplementaryOp[ESHL] = ESHR;
    ComplementaryOp[ESHR] = ESHL;
    ComplementaryOp[ELESS] = EGREATER;
    ComplementaryOp[EGREATER] = ELESS;
    ComplementaryOp[ELESSEQU] = EGREATEREQU;
    ComplementaryOp[EGREATEREQU] = ELESSEQU;
    ComplementaryOp[EEQU] = ENOTEQU;
    ComplementaryOp[ENOTEQU] = EEQU;
    ComplementaryOp[EAND] = EOR;
    ComplementaryOp[EXOR] = MAXEXPR;
    ComplementaryOp[EOR] = EAND;
    ComplementaryOp[ELAND] = ELOR;
    ComplementaryOp[ELOR] = ELAND;
    ComplementaryOp[EASS] = MAXEXPR;
    ComplementaryOp[EMULASS] = EDIVASS;
    ComplementaryOp[EDIVASS] = EMULASS;
    ComplementaryOp[EMODASS] = MAXEXPR;
    ComplementaryOp[EADDASS] = ESUBASS;
    ComplementaryOp[ESUBASS] = EADDASS;
    ComplementaryOp[ESHLASS] = ESHRASS;
    ComplementaryOp[ESHRASS] = ESHLASS;
    ComplementaryOp[EANDASS] = EORASS;
    ComplementaryOp[EXORASS] = MAXEXPR;
    ComplementaryOp[EORASS] = EANDASS;
    ComplementaryOp[ECOMMA] = MAXEXPR;
    ComplementaryOp[EPMODULO] = MAXEXPR;
    ComplementaryOp[EROTL] = EROTR;
    ComplementaryOp[EROTR] = EROTL;
    ComplementaryOp[EBCLR] = MAXEXPR;
    ComplementaryOp[EBTST] = MAXEXPR;
    ComplementaryOp[EBSET] = MAXEXPR;
    ComplementaryOp[ETYPCON] = MAXEXPR;
    ComplementaryOp[EBITFIELD] = MAXEXPR;
    ComplementaryOp[EINTCONST] = MAXEXPR;
    ComplementaryOp[EFLOATCONST] = MAXEXPR;
    ComplementaryOp[ESTRINGCONST] = MAXEXPR;
    ComplementaryOp[ECOND] = MAXEXPR;
    ComplementaryOp[EFUNCCALL] = MAXEXPR;
    ComplementaryOp[EFUNCCALLP] = MAXEXPR;
    ComplementaryOp[EOBJREF] = MAXEXPR;
    ComplementaryOp[EMFPOINTER] = MAXEXPR;
    ComplementaryOp[ENULLCHECK] = MAXEXPR;
    ComplementaryOp[EPRECOMP] = MAXEXPR;
    ComplementaryOp[ETEMP] = MAXEXPR;
    ComplementaryOp[EARGOBJ] = MAXEXPR;
    ComplementaryOp[ELOCOBJ] = MAXEXPR;
    ComplementaryOp[ELABEL] = MAXEXPR;
    ComplementaryOp[ESETCONST] = MAXEXPR;
    ComplementaryOp[ENEWEXCEPTION] = MAXEXPR;
    ComplementaryOp[ENEWEXCEPTIONARRAY] = MAXEXPR;
    ComplementaryOp[EOBJLIST] = MAXEXPR;
    ComplementaryOp[EMEMBER] = MAXEXPR;
    ComplementaryOp[ETEMPLDEP] = MAXEXPR;
    ComplementaryOp[EINSTRUCTION] = MAXEXPR;
    ComplementaryOp[EDEFINE] = MAXEXPR;
    ComplementaryOp[EREUSE] = MAXEXPR;
    ComplementaryOp[EASSBLK] = MAXEXPR;
    ComplementaryOp[EVECTOR128CONST] = MAXEXPR;
    ComplementaryOp[ECONDASS] = MAXEXPR;
}

void IRO_InitializeComplementaryOpLogicalArray(void) {
    int i;

    for (i = 0; i < MAXEXPR; i++) {
        ComplementaryOpLogical[i] = MAXEXPR;
    }

    ComplementaryOpLogical[EPOSTINC] = MAXEXPR;
    ComplementaryOpLogical[EPOSTDEC] = MAXEXPR;
    ComplementaryOpLogical[EPREINC] = MAXEXPR;
    ComplementaryOpLogical[EPREDEC] = MAXEXPR;
    ComplementaryOpLogical[EINDIRECT] = MAXEXPR;
    ComplementaryOpLogical[EMONMIN] = MAXEXPR;
    ComplementaryOpLogical[EBINNOT] = MAXEXPR;
    ComplementaryOpLogical[ELOGNOT] = ELOGNOT;
    ComplementaryOpLogical[EFORCELOAD] = MAXEXPR;
    ComplementaryOpLogical[EMUL] = MAXEXPR;
    ComplementaryOpLogical[EMULV] = MAXEXPR;
    ComplementaryOpLogical[EDIV] = MAXEXPR;
    ComplementaryOpLogical[EMODULO] = MAXEXPR;
    ComplementaryOpLogical[EADDV] = MAXEXPR;
    ComplementaryOpLogical[ESUBV] = MAXEXPR;
    ComplementaryOpLogical[EADD] = MAXEXPR;
    ComplementaryOpLogical[ESUB] = MAXEXPR;
    ComplementaryOpLogical[ESHL] = MAXEXPR;
    ComplementaryOpLogical[ESHR] = MAXEXPR;
    ComplementaryOpLogical[ELESS] = EGREATEREQU;
    ComplementaryOpLogical[EGREATER] = ELESSEQU;
    ComplementaryOpLogical[ELESSEQU] = EGREATER;
    ComplementaryOpLogical[EGREATEREQU] = ELESS;
    ComplementaryOpLogical[EEQU] = ENOTEQU;
    ComplementaryOpLogical[ENOTEQU] = EEQU;
    ComplementaryOpLogical[EAND] = MAXEXPR;
    ComplementaryOpLogical[EXOR] = MAXEXPR;
    ComplementaryOpLogical[EOR] = MAXEXPR;
    ComplementaryOpLogical[ELAND] = ELOR;
    ComplementaryOpLogical[ELOR] = ELAND;
    ComplementaryOpLogical[EASS] = MAXEXPR;
    ComplementaryOpLogical[EMULASS] = MAXEXPR;
    ComplementaryOpLogical[EDIVASS] = MAXEXPR;
    ComplementaryOpLogical[EMODASS] = MAXEXPR;
    ComplementaryOpLogical[EADDASS] = MAXEXPR;
    ComplementaryOpLogical[ESUBASS] = MAXEXPR;
    ComplementaryOpLogical[ESHLASS] = MAXEXPR;
    ComplementaryOpLogical[ESHRASS] = MAXEXPR;
    ComplementaryOpLogical[EANDASS] = MAXEXPR;
    ComplementaryOpLogical[EXORASS] = MAXEXPR;
    ComplementaryOpLogical[EORASS] = MAXEXPR;
    ComplementaryOpLogical[ECOMMA] = MAXEXPR;
    ComplementaryOpLogical[EPMODULO] = MAXEXPR;
    ComplementaryOpLogical[EROTL] = MAXEXPR;
    ComplementaryOpLogical[EROTR] = MAXEXPR;
    ComplementaryOpLogical[EBCLR] = MAXEXPR;
    ComplementaryOpLogical[EBTST] = MAXEXPR;
    ComplementaryOpLogical[EBSET] = MAXEXPR;
    ComplementaryOpLogical[ETYPCON] = MAXEXPR;
    ComplementaryOpLogical[EBITFIELD] = MAXEXPR;
    ComplementaryOpLogical[EINTCONST] = MAXEXPR;
    ComplementaryOpLogical[EFLOATCONST] = MAXEXPR;
    ComplementaryOpLogical[ESTRINGCONST] = MAXEXPR;
    ComplementaryOpLogical[ECOND] = MAXEXPR;
    ComplementaryOpLogical[EFUNCCALL] = MAXEXPR;
    ComplementaryOpLogical[EFUNCCALLP] = MAXEXPR;
    ComplementaryOpLogical[EOBJREF] = MAXEXPR;
    ComplementaryOpLogical[EMFPOINTER] = MAXEXPR;
    ComplementaryOpLogical[ENULLCHECK] = MAXEXPR;
    ComplementaryOpLogical[EPRECOMP] = MAXEXPR;
    ComplementaryOpLogical[ETEMP] = MAXEXPR;
    ComplementaryOpLogical[EARGOBJ] = MAXEXPR;
    ComplementaryOpLogical[ELOCOBJ] = MAXEXPR;
    ComplementaryOpLogical[ELABEL] = MAXEXPR;
    ComplementaryOpLogical[ESETCONST] = MAXEXPR;
    ComplementaryOpLogical[ENEWEXCEPTION] = MAXEXPR;
    ComplementaryOpLogical[ENEWEXCEPTIONARRAY] = MAXEXPR;
    ComplementaryOpLogical[EOBJLIST] = MAXEXPR;
    ComplementaryOpLogical[EMEMBER] = MAXEXPR;
    ComplementaryOpLogical[ETEMPLDEP] = MAXEXPR;
    ComplementaryOpLogical[EINSTRUCTION] = MAXEXPR;
    ComplementaryOpLogical[EDEFINE] = MAXEXPR;
    ComplementaryOpLogical[EREUSE] = MAXEXPR;
    ComplementaryOpLogical[EASSBLK] = MAXEXPR;
    ComplementaryOpLogical[EVECTOR128CONST] = MAXEXPR;
    ComplementaryOpLogical[ECONDASS] = MAXEXPR;
}

static void DoTransformations(IROLinear* nd) {
    IROLinear* copy;
    IROLinear* nd2;
    IROLinear* nd3;
    IROLinear* nd5;
    int value;
    IROLinear* nd4;
    IROLinear* right;

    if (nd->type == IROLinearOp2Arg) {
        switch (nd->nodetype) {
            case EASS:
                right = nd->u.diadic.right;
                if (right->type == IROLinearOp2Arg &&
                    AssignmentOp[right->nodetype] < MAXEXPR &&
                    IRO_TypesEqual(nd->rtype, right->rtype) &&
                    IRO_IsVariable(nd3 = nd->u.diadic.left) &&
                    !(nd3->flags & IROLF_BitfieldIndirect) &&
                    IRO_ExprsSame(nd3, right->u.diadic.left)) {
                    nd->nodetype = AssignmentOp[right->nodetype];
                    nd->u.diadic.right = right->u.diadic.right;
                    IRO_NopOut(right->u.diadic.left);
                    right->type = IROLinearNop;

                    nd3->flags |= IROLF_Used;
                    nd3->u.diadic.left->flags |= IROLF_Used;
                }
                break;

            case EMUL:
                if (IS_TYPE_INT(nd->rtype) && IRO_IsPow2(nd->u.diadic.right, &value)) {
                    nd->nodetype = ESHL;
                    CInt64_SetLong(&nd->u.diadic.right->u.node->data.intval, value);
                }
                break;

            case EDIV:
                if (IS_TYPE_INT(nd->rtype) && IRO_IsPow2(nd->u.diadic.right, &value)) {
                    if (is_unsigned(nd->rtype)) {
                        nd->nodetype = ESHR;
                        CInt64_SetLong(&nd->u.diadic.right->u.node->data.intval, value);
                    } else if (!is_unsigned(nd->rtype) &&
                               TYPE_INTEGRAL(nd->rtype)->integral != IT_BOOL &&
                               nd->u.diadic.left->nodetype == ETYPCON &&
                               IS_TYPE_INT(nd->u.diadic.left->u.monadic->rtype) &&
                               is_unsigned(nd->u.diadic.left->u.monadic->rtype) &&
                               nd->u.diadic.left->u.monadic->rtype->size <
                                   nd->u.diadic.left->rtype->size) {
                        nd->nodetype = ESHR;
                        CInt64_SetLong(&nd->u.diadic.right->u.node->data.intval, value);
                        if (nd->flags & IROLF_Reffed) {
                            copy = IRO_NewLinear(IROLinearOp1Arg);
                            memcpy(copy, nd, sizeof(IROLinear));
                            copy->type = IROLinearOp1Arg;
                            copy->nodetype = ETYPCON;
                            copy->index = IRO_NumLinear++;
                            copy->rtype = nd->rtype;
                            IRO_PasteAfter(copy, copy, nd);
                            IRO_LocateFather_Cut_And_Paste_Without_Nopping(nd, copy);
                            copy->u.monadic = nd;
                            nd->flags |= IROLF_Reffed;
                        }
                        nd->rtype = IRO_UnsignedType(nd->rtype);
                        nd->u.diadic.right->rtype = nd->u.diadic.right->u.node->rtype =
                            IRO_UnsignedType(nd->u.diadic.right->rtype);
                        nd->u.diadic.left->rtype = IRO_UnsignedType(nd->u.diadic.left->rtype);
                    }
                }
                break;

            case EMODULO:
                if (IS_TYPE_INT(nd->rtype) &&
                    is_unsigned(nd->rtype) &&
                    IRO_IsPow2(nd->u.diadic.right, &value)) {
                    nd->nodetype = EAND;
                    nd->u.diadic.right->u.node->data.intval =
                        CInt64_Sub(nd->u.diadic.right->u.node->data.intval, cint64_one);
                }
                break;

            case EEQU:
                if ((nd2 = IRO_LocateFather(nd)) &&
                    nd2->nodetype == ETYPCON &&
                    IS_TYPE_INT(nd2->rtype) &&
                    (nd3 = IRO_LocateFather(nd2)) &&
                    nd3->nodetype == ELOGNOT &&
                    (nd4 = IRO_LocateFather(nd3)) &&
                    nd4->nodetype == ETYPCON &&
                    IS_TYPE_INT(nd4->rtype) &&
                    (nd5 = IRO_LocateFather(nd4)) &&
                    (nd5->type == IROLinearIf || nd5->type == IROLinearIfNot)) {
                    IRO_LocateFather_Cut_And_Paste_Without_Nopping(nd4, nd);
                    nd->nodetype = ENOTEQU;
                    nd2->type = IROLinearNop;
                    nd3->type = IROLinearNop;
                    nd4->type = IROLinearNop;
                }
                break;
            default:
                break;
        }
    }
}

static void IRO_SwitchChildren(IROLinear* node) {
    IROLinear* swap = node->u.diadic.left; // NOTE: invented name
    node->u.diadic.left = node->u.diadic.right;
    node->u.diadic.right = swap;
}

static void ReplaceExprWithConst(IROLinear* node, CInt64 value) {
    IRO_NopOut(node);
    node->type = IROLinearOperand;
    node->nodetype = EINTCONST;
    node->u.node = IRO_NewENode(EINTCONST);
    node->u.node->data.intval = value;
    node->u.node->flags = node->nodeflags;
    node->u.node->rtype = node->rtype;
    if (node->rtype->type == TYPEFLOAT) {
        node->nodetype = node->u.node->type = EFLOATCONST;
        node->u.node->data.floatval = CMach_CalcFloatConvertFromInt((Type*)&stsignedlong, value);
    }
}

static void ReplaceExprWithLeftChild(IROLinear* expr) {
    IROLinear* left = expr->u.diadic.left;
    IROLinear* right = expr->u.diadic.right;

    if (left->rtype == expr->rtype) {
        IRO_LocateFather_Cut_And_Paste_Without_Nopping(expr, left);
        left->flags = expr->flags;
        left->nodeflags = expr->nodeflags;
        expr->type = IROLinearNop;
        IRO_NopOut(right);
    } else {
        expr->type = IROLinearOp1Arg;
        expr->nodetype = ETYPCON;
        expr->u.monadic = left;
        IRO_NopOut(right);
    }
}

static void ReplaceExprWithRightChild(IROLinear* expr) {
    IROLinear* left = expr->u.diadic.left;
    IROLinear* right = expr->u.diadic.right;

    if (right->rtype == expr->rtype) {
        IRO_LocateFather_Cut_And_Paste_Without_Nopping(expr, right);
        right->flags = expr->flags;
        right->nodeflags = expr->nodeflags;
        expr->type = IROLinearNop;
        IRO_NopOut(left);
    } else {
        expr->type = IROLinearOp1Arg;
        expr->nodetype = ETYPCON;
        expr->u.monadic = right;
        IRO_NopOut(left);
    }
}

static void ReplaceExprWithMonaminRight(IROLinear* nd) {
    IROLinear* left = nd->u.diadic.left;
    IROLinear* right = nd->u.diadic.right;

    if (right->rtype == nd->rtype) {
        nd->type = IROLinearOp1Arg;
        nd->nodetype = EMONMIN;
        nd->u.monadic = right;
        IRO_NopOut(left);
    } else {
        IRO_NopOut(left);
        left->type = IROLinearOp1Arg;
        left->nodetype = ETYPCON;
        left->expr = right->expr;
        left->rtype = nd->rtype;
        left->u.monadic = right;
        nd->type = IROLinearOp1Arg;
        nd->nodetype = EMONMIN;
        nd->u.monadic = left;
    }
}

static void ReplaceExprWithMonaminLeft(IROLinear* nd) {
    IROLinear* left = nd->u.diadic.left;
    IROLinear* right = nd->u.diadic.right;

    if (left->rtype == nd->rtype) {
        nd->type = IROLinearOp1Arg;
        nd->nodetype = EMONMIN;
        nd->u.monadic = left;
        IRO_NopOut(right);
    } else {
        IRO_NopOut(right);
        right->type = IROLinearOp1Arg;
        right->nodetype = ETYPCON;
        right->expr = left->expr;
        right->rtype = nd->rtype;
        right->u.monadic = left;
        nd->type = IROLinearOp1Arg;
        nd->nodetype = EMONMIN;
        nd->u.monadic = right;
    }
}

static inline void switchFatherLeftMonadic(IROLinear* nd) {
    IROLinear* inner = nd->u.monadic;
    IRO_LocateFather_Cut_And_Paste_Without_Nopping(nd, inner);
    nd->u.monadic = inner->u.monadic;
    inner->u.monadic = nd;
    inner->rtype = nd->rtype;
    inner->flags = nd->flags;
    inner->nodeflags = nd->nodeflags;
    IRO_CutAndPasteAfter(inner, inner, nd);
}

// NOTE: invented names: child, grandchild
static void switchFatherLeft(IROLinear* nd, int isRight) {
    IROLinear* child;
    IROLinear* grandchild;

    child = nd->u.diadic.left;
    IRO_LocateFather_Cut_And_Paste_Without_Nopping(nd, child);

    if (isRight) {
        nd->u.diadic.left = child->u.diadic.left;
        child->u.diadic.left = nd;
        grandchild = child->u.diadic.right;
    } else {
        nd->u.diadic.left = child->u.diadic.right;
        child->u.diadic.right = nd;
        grandchild = child->u.diadic.left;
    }

    child->rtype = nd->rtype;
    child->flags = nd->flags;
    child->nodeflags = nd->nodeflags;
    IRO_CutAndPasteAfter(child, child, nd);
    IRO_CutAndPasteAfter(IRO_FindFirst(grandchild), grandchild, nd);
}

static void PickCommonsubAtLeftLeft(IROLinear* expr) {
    switchFatherLeft(expr, 0);
    ReplaceExprWithRightChild(expr->u.diadic.right);
}

static void PickCommonsubAtRightLeft(IROLinear* expr) {
    switchFatherLeft(expr, 1);
    ReplaceExprWithRightChild(expr->u.diadic.right);
}

static void PickCommonsubAtLeftRight(IROLinear* expr) {
    switchFatherLeft(expr, 0);
    ReplaceExprWithLeftChild(expr->u.diadic.right);
}

static void PickCommonsubAtRightRight(IROLinear* expr) {
    switchFatherLeft(expr, 1);
    ReplaceExprWithLeftChild(expr->u.diadic.right);
}

// NOTE: invented names: resultBits, operandBits
static void DoTransformations11(IROLinear* nd) {
    IROLinear* left;
    IROLinear* right;
    int changed;
    int compare;
    Type* type;
    SInt32 resultBits;
    SInt32 operandBits;
    CInt64 val;

    if (nd->type == IROLinearOp2Arg) {
        left = nd->u.diadic.left;
        right = nd->u.diadic.right;
        if (IS_TYPE_FLOAT(nd->rtype) || IS_TYPE_FLOAT(left->rtype) || IS_TYPE_FLOAT(right->rtype)) {
            return;
        }

        changed = FALSE;
        if (!IRO_HasSideEffect(left) && !IRO_HasSideEffect(right)) {
            if (IRO_IsIntConstant(right) || IRO_IsFloatConstant(right)) {
                if (IRO_IsConstantZero(right)) {
                    switch (nd->nodetype) {
                        case EADDV:
                        case ESUBV:
                        case EADD:
                        case ESUB:
                        case ESHL:
                        case ESHR:
                        case EXOR:
                        case EOR:
                        case ELOR:
                        case EADDASS:
                        case ESUBASS:
                        case ESHLASS:
                        case ESHRASS:
                        case EXORASS:
                        case EORASS:
                            ReplaceExprWithLeftChild(nd);
                            changed = TRUE;
                            break;
                        case EMUL:
                        case EAND:
                        case ELAND:
                            ReplaceExprWithConst(nd, cint64_zero);
                            changed = TRUE;
                            break;
                        case EMULASS:
                        case EANDASS:
                            nd->nodetype = EASS;
                            nd->u.diadic.right->rtype = nd->rtype;
                            nd->u.diadic.right->u.node->rtype = nd->rtype;
                            changed = TRUE;
                            break;
                        case EDIV:
                        case EMODULO:
                        case EDIVASS:
                        case EMODASS:
                            if (nd->stmt && nd->stmt->sourceoffset) {
                                TStreamElement* token = CPrep_CurStreamElement();
                                token->tokenoffset = nd->stmt->sourceoffset;
                                CError_SetErrorToken(token);
                            }
                            CError_Warning(ERR_DIVISION_BY_0);
                            break;
                        default:
                            break;
                    }
                } else if (nd->nodetype == ELAND) {
                    ReplaceExprWithLeftChild(nd);
                    changed = TRUE;
                } else if (nd->nodetype == ELOR) {
                    ReplaceExprWithConst(nd, cint64_one);
                    changed = TRUE;
                } else if (nd->nodetype == ESHL ||
                           nd->nodetype == ESHR ||
                           nd->nodetype == ESHLASS ||
                           nd->nodetype == ESHRASS) {
                    type = nd->rtype;
                    resultBits = type->size * 8;
                    if (left->type == IROLinearOp1Arg && left->nodetype == ETYPCON) {
                        type = left->u.monadic->rtype;
                        if (left->u.monadic->type == IROLinearOp1Arg &&
                            left->u.monadic->nodetype == EINDIRECT &&
                            left->u.monadic->u.monadic->type == IROLinearOp1Arg &&
                            left->u.monadic->u.monadic->nodetype == EBITFIELD &&
                            IS_TYPE_BITFIELD(left->u.monadic->u.monadic->rtype)) {
                            operandBits =
                                TYPE_BITFIELD(left->u.monadic->u.monadic->rtype)->bitlength;
                        } else {
                            operandBits = type->size * 8;
                        }
                    } else if (left->type == IROLinearOp1Arg &&
                               left->nodetype == EINDIRECT &&
                               left->u.monadic->type == IROLinearOp1Arg &&
                               left->u.monadic->nodetype == EBITFIELD &&
                               IS_TYPE_BITFIELD(left->u.monadic->rtype)) {
                        operandBits = TYPE_BITFIELD(left->u.monadic->rtype)->bitlength;
                    } else {
                        operandBits = resultBits;
                    }

                    switch (nd->nodetype) {
                        case ESHL:
                        case ESHLASS:
                            CInt64_SetLong(&val, resultBits);
                            if (is_unsigned(type)) {
                                compare = CInt64_GreaterEqualU(right->u.node->data.intval, val);
                            } else {
                                compare = CInt64_GreaterEqual(right->u.node->data.intval, val);
                            }
                            break;
                        case ESHR:
                        case ESHRASS:
                            CInt64_SetLong(&val, operandBits);
                            compare = is_unsigned(type) &&
                                CInt64_GreaterEqualU(right->u.node->data.intval, val);
                            break;
                        default:
                            break;
                    }

                    if (compare) {
                        switch (nd->nodetype) {
                            case ESHL:
                            case ESHR:
                                ReplaceExprWithConst(nd, cint64_zero);
                                break;
                            case ESHLASS:
                            case ESHRASS:
                                nd->nodetype = EASS;
                                ReplaceExprWithConst(right, cint64_zero);
                                break;
                            default:
                                break;
                        }
                        changed = TRUE;
                    }
                } else if (IRO_IsConstantOne(right)) {
                    switch (nd->nodetype) {
                        case EMUL:
                        case EMULV:
                        case EDIV:
                        case EMULASS:
                        case EDIVASS:
                            ReplaceExprWithLeftChild(nd);
                            changed = TRUE;
                            break;
                        case EMODULO:
                            ReplaceExprWithConst(nd, cint64_zero);
                            changed = TRUE;
                            break;
                        case EMODASS:
                            nd->nodetype = EASS;
                            ReplaceExprWithConst(right, cint64_zero);
                            nd->u.diadic.right->rtype = nd->rtype;
                            nd->u.diadic.right->u.node->rtype = nd->rtype;
                            changed = TRUE;
                            break;
                        default:
                            break;
                    }
                } else if (IRO_IsConstantNegativeOne(right)) {
                    switch (nd->nodetype) {
                        case EMUL:
                        case EMULV:
                        case EDIV:
                            ReplaceExprWithMonaminLeft(nd);
                            changed = TRUE;
                            break;
                        case EMODULO:
                            ReplaceExprWithConst(nd, cint64_zero);
                            changed = TRUE;
                            break;
                        case EMODASS:
                            nd->nodetype = EASS;
                            ReplaceExprWithConst(right, cint64_zero);
                            nd->u.diadic.right->rtype = nd->rtype;
                            nd->u.diadic.right->u.node->rtype = nd->rtype;
                            changed = TRUE;
                            break;
                        default:
                            break;
                    }
                }
            }

            if (!changed && (IRO_IsIntConstant(left) || IRO_IsFloatConstant(left))) {
                if (IRO_IsConstantZero(left)) {
                    switch (nd->nodetype) {
                        case EADDV:
                        case EADD:
                        case EXOR:
                        case EOR:
                        case ELOR:
                            ReplaceExprWithRightChild(nd);
                            break;
                        case EMUL:
                        case ESHL:
                        case ESHR:
                        case EAND:
                        case ELAND:
                            ReplaceExprWithConst(nd, cint64_zero);
                            break;
                        case ESUBV:
                        case ESUB:
                            ReplaceExprWithMonaminRight(nd);
                            break;
                        default:
                            break;
                    }
                } else if (nd->nodetype == ELAND) {
                    ReplaceExprWithRightChild(nd);
                } else if (nd->nodetype == ELOR) {
                    ReplaceExprWithConst(nd, cint64_one);
                } else if (IRO_IsConstantOne(left)) {
                    switch (nd->nodetype) {
                        case EMUL:
                        case EMULV:
                            ReplaceExprWithRightChild(nd);
                            break;
                        default:
                            break;
                    }
                } else if (IRO_IsConstantNegativeOne(left)) {
                    switch (nd->nodetype) {
                        case EMUL:
                        case EMULV:
                            ReplaceExprWithMonaminRight(nd);
                            break;
                        default:
                            break;
                    }
                }
            }
        }
    }
}

static void DoTransformations12(IROLinear* nd) {
    IROLinear* left;
    IROLinear* right;

    if (nd->type == IROLinearOp2Arg) {
        left = nd->u.diadic.left;
        right = nd->u.diadic.right;
        if (IS_TYPE_FLOAT(nd->rtype) || IS_TYPE_FLOAT(left->rtype) || IS_TYPE_FLOAT(right->rtype)) {
            return;
        }

        if (IRO_ExprsSameSemantically(left, right) && !IRO_HasSideEffect(left)) {
            switch (nd->nodetype) {
                case ESUBV:
                case ESUB:
                case ELESS:
                case EGREATER:
                case ENOTEQU:
                case EXOR:
                    ReplaceExprWithConst(nd, cint64_zero);
                    break;
                case ELESSEQU:
                case EGREATEREQU:
                case EEQU:
                    ReplaceExprWithConst(nd, cint64_one);
                    break;
                case EAND:
                case EOR:
                case ELAND:
                case ELOR:
                case EASS:
                case EANDASS:
                case EORASS:
                    ReplaceExprWithLeftChild(nd);
                    break;
                case ESUBASS:
                case EXORASS:
                    nd->nodetype = EASS;
                    ReplaceExprWithConst(right, cint64_zero);
                    break;
                default:
                    break;
            }
        }
    }
}

// folds an equality of two pointers that pointer analysis resolves to one place each
static void DoTransformations13(IROLinear* nd) {
    IROLinear* left;
    IROLinear* right;
    IROLinear* left2;
    IROLinear* right2;
    IROListNode* next; // NOTE: invented name
    IROListNode* leftlist;
    IROListNode* rightlist;

    if (nd->type == IROLinearOp2Arg) {
        left = nd->u.diadic.left;
        right = nd->u.diadic.right;

        if (!IRO_HasSideEffect(left) &&
            !IRO_HasSideEffect(right) &&
            (nd->nodetype == EEQU || nd->nodetype == ENOTEQU) &&
            PointerAnalysis_IsLinearNodePointerExprDefinite(FunctionName, left) &&
            PointerAnalysis_IsLinearNodePointerExprDefinite(FunctionName, right)) {
            leftlist = NULL;
            PointerAnalysis_LookupLinearNodePointerExpr(FunctionName, left, &leftlist);
            if (leftlist) {
                if (leftlist->list.head && leftlist->list.tail && !leftlist->nextList) {
                    rightlist = NULL;
                    PointerAnalysis_LookupLinearNodePointerExpr(FunctionName, right, &rightlist);
                    if (rightlist) {
                        if (rightlist->list.head && rightlist->list.tail && !rightlist->nextList) {
                            left2 = leftlist->list.tail;
                            right2 = rightlist->list.tail;
                            if (IRO_ExprsSameSemantically(left2, right2)) {
                                ReplaceExprWithConst(
                                    nd, (nd->nodetype == EEQU) ? cint64_one : cint64_zero
                                );
                            } else if (left2->type == right2->type &&
                                       IRO_TypesEqual(left2->rtype, right2->rtype)) {
                                ReplaceExprWithConst(
                                    nd, (nd->nodetype == EEQU) ? cint64_zero : cint64_one
                                );
                            }
                        }

                        while (rightlist) {
                            next = rightlist->nextList;
                            IRO_free(rightlist);
                            rightlist = next;
                        }
                    }
                }

                while (leftlist) {
                    next = leftlist->nextList;
                    IRO_free(leftlist);
                    leftlist = next;
                }
            }
        }
    }
}

static void DoTransformations21(IROLinear* nd) {
    IROLinear* left;
    IROLinear* right;
    Boolean changed;

    if (nd->type == IROLinearOp2Arg) {
        left = nd->u.diadic.left;
        right = nd->u.diadic.right;

        if (left->type == IROLinearOp1Arg &&
            right->type == IROLinearOp1Arg &&
            left->nodetype == right->nodetype) {
            switch (left->nodetype) {
                case EMONMIN:
                case EBINNOT:
                case ELOGNOT:
                    changed = FALSE;
                    switch (nd->nodetype) {
                        case EXOR:
                            if (left->nodetype == EBINNOT) {
                                goto collapse;
                            }
                            break;
                        case ELESS:
                        case EGREATER:
                        case ELESSEQU:
                        case EGREATEREQU:
                            if (left->nodetype == EMONMIN) {
                                nd->nodetype = ComplementaryOp[nd->nodetype];
                                goto collapse;
                            }
                            break;
                        case EMUL:
                        case EDIV:
                            if (left->nodetype == EMONMIN) {
                                goto collapse;
                            }
                            break;
                        case EEQU:
                        case ENOTEQU:
                            if (left->nodetype != ELOGNOT) {
collapse:
                                nd->u.diadic.left = left->u.monadic;
                                nd->u.diadic.right = right->u.monadic;
                                left->type = right->type = IROLinearNop;
                                changed = TRUE;
                            }
                            break;
                        case ELAND:
                        case ELOR:
                            if (left->nodetype == ELOGNOT) {
                                nd->nodetype = ComplementaryOp[nd->nodetype];
                                goto switchfather;
                            }
                            break;
                        case EAND:
                        case EOR:
                            if (!IS_TYPE_FLOAT(nd->rtype) &&
                                !IS_TYPE_FLOAT(left->rtype) &&
                                !IS_TYPE_FLOAT(right->rtype) &&
                                left->nodetype != EMONMIN) {
                                nd->nodetype = ComplementaryOp[nd->nodetype];
                                goto switchfather;
                            }
                            break;
                        case EADD:
                        case ESUB:
                            if (!IS_TYPE_FLOAT(nd->rtype) &&
                                !IS_TYPE_FLOAT(left->rtype) &&
                                !IS_TYPE_FLOAT(right->rtype) &&
                                left->nodetype == EMONMIN) {
switchfather:
                                switchFatherLeftMonadic(nd);
                                nd->u.diadic.right = right->u.monadic;
                                right->type = IROLinearNop;
                                changed = TRUE;
                            }
                            break;
                        default:
                            break;
                    }

                    if (changed) {
                        DoTransformations(nd);
                        DoTransformations11(nd);
                        DoTransformations12(nd);
                        DoTransformations13(nd);
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

static void DoTransformations22(IROLinear* nd) {
    IROLinear* left;
    IROLinear* right;
    IROLinear* ndtmp;

    if (nd->type == IROLinearOp2Arg) {
        left = nd->u.diadic.left;
        right = nd->u.diadic.right;
        if (IS_TYPE_FLOAT(nd->rtype) || IS_TYPE_FLOAT(left->rtype) || IS_TYPE_FLOAT(right->rtype)) {
            return;
        }

        if (!IRO_HasSideEffect(left) && !IRO_HasSideEffect(right)) {
            if (left->type == IROLinearOp1Arg && left->nodetype == EMONMIN) {
                switch (nd->nodetype) {
                    case ESUB:
                    case ESUBV:
                        nd->nodetype = ComplementaryOp[nd->nodetype];
                        switchFatherLeftMonadic(nd);
                        break;

                    case EADD:
                    case EADDV:
                        nd->nodetype = ComplementaryOp[nd->nodetype];
                        nd->u.diadic.left = right;
                        nd->u.diadic.right = left->u.monadic;
                        left->type = IROLinearNop;
                        DoTransformations(nd);
                        DoTransformations12(nd);
                        DoTransformations21(nd);
                        break;

                    case EDIV:
                        if (IRO_ExprsSameSemantically(left->u.monadic, right)) {
                            ReplaceExprWithConst(nd, cint64_negone);
                        }
                        break;
                    default:
                        break;
                }
            } else {
                ndtmp = NULL;
                if (left->type == IROLinearOp1Arg &&
                    IRO_ExprsSameSemantically(left->u.monadic, right)) {
                    ndtmp = left;
                } else if (right->type == IROLinearOp1Arg &&
                           IRO_ExprsSameSemantically(left, right->u.monadic)) {
                    ndtmp = right;
                }

                if (ndtmp) {
                    switch (nd->nodetype) {
                        case EAND:
                            if (ndtmp->nodetype == EBINNOT || ndtmp->nodetype == ELOGNOT) {
                                ReplaceExprWithConst(nd, cint64_zero);
                            }
                            break;

                        case EANDASS:
                            if (ndtmp->nodetype == EBINNOT || ndtmp->nodetype == ELOGNOT) {
                                nd->nodetype = EASS;
                                ReplaceExprWithConst(right, cint64_zero);
                                nd->u.diadic.right->rtype = nd->rtype;
                                nd->u.diadic.right->u.node->rtype = nd->rtype;
                            }
                            break;

                        case EXOR:
                        case EOR:
                            if (ndtmp->nodetype == EBINNOT) {
                                ReplaceExprWithConst(nd, cint64_zero);
                                nd->u.node->data.intval = CInt64_Inv(nd->u.node->data.intval);
                            }
                            break;

                        case EXORASS:
                        case EORASS:
                            if (ndtmp->nodetype == EBINNOT) {
                                nd->nodetype = EASS;
                                ReplaceExprWithConst(right, cint64_zero);
                                right->u.node->data.intval = CInt64_Inv(right->u.node->data.intval);
                                nd->u.diadic.right->rtype = nd->rtype;
                                nd->u.diadic.right->u.node->rtype = nd->rtype;
                            }
                            break;

                        case ELOR:
                            if (ndtmp->nodetype == EBINNOT || ndtmp->nodetype == ELOGNOT) {
                                ReplaceExprWithConst(nd, cint64_one);
                            }
                            break;

                        case ELAND:
                            if (ndtmp->nodetype == ELOGNOT) {
                                ReplaceExprWithConst(nd, cint64_zero);
                            }
                            break;

                        case EDIV:
                            if (ndtmp->nodetype == EMONMIN) {
                                ReplaceExprWithConst(nd, cint64_negone);
                            }
                            break;

                        case EDIVASS:
                            if (ndtmp->nodetype == EMONMIN) {
                                nd->nodetype = EASS;
                                ReplaceExprWithConst(right, cint64_negone);
                                nd->u.diadic.right->rtype = nd->rtype;
                                nd->u.diadic.right->u.node->rtype = nd->rtype;
                            }
                            break;
                        default:
                            break;
                    }
                }
            }
        }
    }
}

// Converts `node` to `type` by a conversion node pasted after it (a bool by two logical nots), like
// IROUseDef's IRO_AddTypeConversion without the list.
static IROLinear* InsertTypeConversion(IROLinear* node, Type* type) { // NOTE: invented name
    IROLinear* first;
    IROLinear* last;

    if (type->type == TYPEINT && TYPE_INTEGRAL(type)->integral == IT_BOOL) {
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
        IRO_PasteAfter(first, last, node);
    } else {
        last = IRO_NewLinear(IROLinearOp1Arg);
        memcpy(last, node, sizeof(IROLinear));
        last->type = IROLinearOp1Arg;
        last->nodetype = ETYPCON;
        last->index = IRO_NumLinear++;
        last->rtype = type;
        last->u.monadic = node;
        IRO_PasteAfter(last, last, node);
    }
    return last;
}

static void DoTransformations23(IROLinear* nd) {
    Boolean changed;
    Boolean isCompare;
    UInt8 which;
    IROLinear* left;
    IROLinear* right;
    CInt64 size;
    CInt64 val;

    if (nd->type == IROLinearOp2Arg && !IRO_HasSideEffect(nd)) {
        left = nd->u.diadic.left;
        right = nd->u.diadic.right;
        if (IS_TYPE_FLOAT(nd->rtype) || IS_TYPE_FLOAT(left->rtype) || IS_TYPE_FLOAT(right->rtype)) {
            return;
        }

        changed = isCompare = FALSE;
        which = 0;

        switch (nd->nodetype) {
            case ELESS:
            case EGREATER:
            case ELESSEQU:
            case EGREATEREQU:
                isCompare = TRUE;
            case EADDV:
            case EADD:
            case EEQU:
            case ENOTEQU:
            case EAND:
            case EOR:
                if (left->type == IROLinearOp2Arg) {
                    if (IRO_ExprsSameSemantically(right, left->u.diadic.left)) {
                        which = 1;
                    } else if (IRO_ExprsSameSemantically(right, left->u.diadic.right)) {
                        which = 2;
                    }

                    if (which) {
                        if (isCompare) {
                            nd->nodetype = ComplementaryOp[nd->nodetype];
                        }
                        IRO_SwitchChildren(nd);
                        left = nd->u.diadic.left;
                        right = nd->u.diadic.right;
                        break;
                    }
                }
            case ESUBV:
            case ESUB:
            case EADDASS:
            case ESUBASS:
            case EANDASS:
            case EORASS:
                if (right->type == IROLinearOp2Arg) {
                    if (IRO_ExprsSameSemantically(left, right->u.diadic.left)) {
                        which = 1;
                    } else if (IRO_ExprsSameSemantically(left, right->u.diadic.right)) {
                        which = 2;
                    }
                }
                break;

            default:
                goto done;
        }

        if (which) {
            switch (right->nodetype) {
                case EAND:
                case EOR:
                    if (which == 2) {
                        IRO_SwitchChildren(right);
                    }

                    if (nd->nodetype == right->nodetype ||
                        (nd->nodetype == EANDASS && right->nodetype == EAND) ||
                        (nd->nodetype == EORASS && right->nodetype == EOR)) {
                        ReplaceExprWithRightChild(right);
                        changed = TRUE;
                    } else if (nd->nodetype == ComplementaryOp[right->nodetype] ||
                               (nd->nodetype == EANDASS && right->nodetype == EOR) ||
                               (nd->nodetype == EORASS && right->nodetype == EAND)) {
                        ReplaceExprWithLeftChild(nd);
                    }
                    break;

                case EADD:
                    if (which == 2) {
                        IRO_SwitchChildren(right);
                    }

                    switch (nd->nodetype) {
                        case EEQU:
                        case ENOTEQU:
                            ReplaceExprWithConst(left, cint64_zero);
                            ReplaceExprWithRightChild(right);
                            IRO_SwitchChildren(nd);
                            if (isCompare) {
                                nd->nodetype = ComplementaryOp[nd->nodetype];
                            }
                            changed = TRUE;
                            break;

                        case ESUB:
                        case ESUBV:
                            ReplaceExprWithRightChild(right);
                            ReplaceExprWithMonaminRight(nd);
                            changed = TRUE;
                            break;

                        case ESUBASS:
                            ReplaceExprWithRightChild(right);
                            changed = TRUE;
                            break;
                        default:
                            break;
                    }
                    break;

                case ESUB:
                    switch (nd->nodetype) {
                        case EEQU:
                        case ENOTEQU:
                            if (which == 1) {
                                ReplaceExprWithConst(left, cint64_zero);
                                ReplaceExprWithRightChild(right);
                                IRO_SwitchChildren(nd);
                            }
                            break;
                        case EADD:
                        case EADDV:
                            if (which == 2) {
                                ReplaceExprWithLeftChild(right);
                                ReplaceExprWithRightChild(nd);
                            }
                            break;
                        case ESUB:
                        case ESUBV:
                            if (which == 1) {
                                ReplaceExprWithRightChild(right);
                                ReplaceExprWithRightChild(nd);
                            }
                            break;
                        case EADDASS:
                            if (which == 2) {
                                nd->nodetype = EASS;
                                ReplaceExprWithLeftChild(right);
                                changed = TRUE;
                            }
                            break;
                        case ESUBASS:
                            if (which == 1) {
                                nd->nodetype = EASS;
                                ReplaceExprWithRightChild(right);
                                changed = TRUE;
                            }
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
        }

done:
        if (!changed) {
            switch (nd->nodetype) {
                case ESUB:
                case ESUBV:
                    which = 0;
                    if (left->type == IROLinearOp2Arg) {
                        if (IRO_ExprsSameSemantically(right, left->u.diadic.left)) {
                            which = 1;
                        } else if (IRO_ExprsSameSemantically(right, left->u.diadic.right)) {
                            which = 2;
                        }
                    }

                    if (which == 1) {
                        if (left->nodetype == ESUB) {
                            ReplaceExprWithMonaminRight(left);
                            ReplaceExprWithLeftChild(nd);
                        } else if (left->nodetype == EADD) {
                            ReplaceExprWithRightChild(left);
                            ReplaceExprWithLeftChild(nd);
                        }
                    } else if (which == 2) {
                        if (left->nodetype == EADD) {
                            ReplaceExprWithLeftChild(left);
                            ReplaceExprWithLeftChild(nd);
                        }
                    }
                    break;
                default:
                    break;
            }

            switch (nd->nodetype) {
                case ESHL:
                case ESHR:
                case ESHLASS:
                case ESHRASS:
                    which = 0;
                    if (left->type == IROLinearOp2Arg) {
                        if (left->nodetype == ComplementaryOp[nd->nodetype] ||
                            left->nodetype == AssignmentOp[ComplementaryOp[nd->nodetype]]) {
                            if (IRO_IsIntConstant(right) &&
                                IRO_ExprsSameSemantically(right, left->u.diadic.right)) {
                                which = 2;
                            }
                        }
                    }

                    if (which == 2) {
                        val = right->u.node->data.intval;
                        if (left->nodetype == ESHR || left->nodetype == ESHRASS) {
                            ReplaceExprWithLeftChild(left);
                            nd->nodetype = (nd->nodetype == ESHL) ? EAND : EANDASS;
                            right->u.node->data.intval = CInt64_Shl(cint64_negone, val);
                            if (nd->rtype != right->rtype) {
                                nd->u.diadic.right = InsertTypeConversion(right, nd->rtype);
                            }
                            changed = TRUE;
                        } else if (is_unsigned(nd->rtype)) {
                            ReplaceExprWithLeftChild(left);
                            nd->nodetype = (nd->nodetype == ESHR) ? EAND : EANDASS;
                            if (nd->rtype->size < 8) {
                                CInt64_SetLong(&size, 64 - 8 * nd->rtype->size);
                                val = CInt64_Add(val, size);
                            }
                            right->u.node->data.intval = CInt64_ShrU(cint64_negone, val);
                            if (nd->rtype != right->rtype) {
                                nd->u.diadic.right = InsertTypeConversion(right, nd->rtype);
                            }
                            changed = TRUE;
                        }
                    }
                    break;
                default:
                    break;
            }
        }

        if (changed) {
            DoTransformations(nd);
            DoTransformations11(nd);
            DoTransformations12(nd);
            DoTransformations13(nd);
            DoTransformations21(nd);
            DoTransformations22(nd);
            DoTransformations23(nd);
        }
    }
}

// TODO: the local copy `nd` of the parameter is what gives the original's code (it reloads the
// parameter after each store); the original's reason for the copy and its name are not known
static void DoTransformations24(IROLinear* expr) {
    IROLinear* nd = expr;
    IROLinear* left;
    IROLinear* right;
    UInt8 changed;

    if (nd->type == IROLinearOp2Arg) {
        left = nd->u.diadic.left;
        right = nd->u.diadic.right;
        if (IS_TYPE_FLOAT(nd->rtype) || IS_TYPE_FLOAT(left->rtype) || IS_TYPE_FLOAT(right->rtype)) {
            return;
        }

        if (left->type == IROLinearOp2Arg &&
            right->type == IROLinearOp2Arg &&
            !IRO_HasSideEffect(nd)) {
            changed = 0;

            if (IRO_ExprsSameSemantically(left->u.diadic.left, right->u.diadic.left)) {
                if (left->nodetype == right->nodetype) {
                    switch (left->nodetype) {
                        case EADD:
                            if (nd->nodetype == ComplementaryOp[left->nodetype]) {
                                ReplaceExprWithRightChild(left);
                                ReplaceExprWithRightChild(right);
                                changed = 1;
                            }
                            break;
                        case ESUB:
                            if (nd->nodetype == ESUB || nd->nodetype == ESUBV) {
                                ReplaceExprWithRightChild(left);
                                ReplaceExprWithRightChild(right);
                                IRO_SwitchChildren(nd);
                                changed = 1;
                            }
                            break;
                        case EMUL:
                            switch (nd->nodetype) {
                                case EADD:
                                case ESUB:
                                    PickCommonsubAtLeftLeft(nd);
                                    changed = 3;
                                    break;
                                default:
                                    break;
                            }
                            break;
                        case EAND:
                            if (nd->nodetype == EXOR) {
                                PickCommonsubAtLeftLeft(nd);
                                changed = 3;
                                break;
                            }
                        case EOR:
                            if (nd->nodetype == left->nodetype) {
                                ReplaceExprWithRightChild(left);
                                changed = 1;
                            } else if (nd->nodetype == ComplementaryOp[left->nodetype]) {
                                PickCommonsubAtLeftLeft(nd);
                                changed = 3;
                            }
                            break;
                        default:
                            break;
                    }
                } else if (left->nodetype == ComplementaryOp[right->nodetype]) {
                    switch (nd->nodetype) {
                        case ESUB:
                        case ESUBV:
                            switch (left->nodetype) {
                                case EADD:
                                    nd->nodetype = ComplementaryOp[nd->nodetype];
                                    ReplaceExprWithRightChild(left);
                                    ReplaceExprWithRightChild(right);
                                    changed = 1;
                                    break;
                                case ESUB:
                                    ReplaceExprWithMonaminRight(left);
                                    ReplaceExprWithRightChild(right);
                                    changed = 1;
                                    break;
                                default:
                                    break;
                            }
                            break;

                        case EAND:
                        case EOR:
                            if (left->nodetype == nd->nodetype) {
                                ReplaceExprWithLeftChild(nd);
                            } else if (right->nodetype == nd->nodetype) {
                                ReplaceExprWithRightChild(nd);
                            }
                            break;
                        default:
                            break;
                    }
                }
            } else if (IRO_ExprsSameSemantically(left->u.diadic.right, right->u.diadic.left)) {
                if (left->nodetype == right->nodetype) {
                    switch (left->nodetype) {
                        case EADD:
                            if (nd->nodetype == ComplementaryOp[left->nodetype]) {
                                ReplaceExprWithLeftChild(left);
                                ReplaceExprWithRightChild(right);
                                changed = 1;
                            }
                            break;
                        case EMUL:
                            switch (nd->nodetype) {
                                case EADD:
                                case ESUB:
                                    PickCommonsubAtRightLeft(nd);
                                    changed = 3;
                                    break;
                                default:
                                    break;
                            }
                            break;
                        case EAND:
                            if (nd->nodetype == EXOR) {
                                PickCommonsubAtRightLeft(nd);
                                changed = 3;
                                break;
                            }
                        case EOR:
                            if (nd->nodetype == left->nodetype) {
                                ReplaceExprWithLeftChild(left);
                                changed = 1;
                            } else if (nd->nodetype == ComplementaryOp[left->nodetype]) {
                                PickCommonsubAtRightLeft(nd);
                                changed = 3;
                            }
                            break;
                        default:
                            break;
                    }
                } else if (left->nodetype == ComplementaryOp[right->nodetype]) {
                    switch (nd->nodetype) {
                        case ESUB:
                        case ESUBV:
                            if (left->nodetype == EADD) {
                                nd->nodetype = ComplementaryOp[nd->nodetype];
                                ReplaceExprWithLeftChild(left);
                                ReplaceExprWithRightChild(right);
                                changed = 1;
                            }
                            break;

                        case EADD:
                        case EADDV:
                            if (left->nodetype == ESUB) {
                                ReplaceExprWithLeftChild(left);
                                ReplaceExprWithRightChild(right);
                                changed = 1;
                            }
                            break;

                        case EAND:
                        case EOR:
                            if (left->nodetype == nd->nodetype) {
                                ReplaceExprWithLeftChild(nd);
                            } else if (right->nodetype == nd->nodetype) {
                                ReplaceExprWithRightChild(nd);
                            }
                            break;
                        default:
                            break;
                    }
                }
            } else if (IRO_ExprsSameSemantically(left->u.diadic.left, right->u.diadic.right)) {
                if (left->nodetype == right->nodetype) {
                    switch (left->nodetype) {
                        case EADD:
                            if (nd->nodetype == ComplementaryOp[left->nodetype]) {
                                ReplaceExprWithRightChild(left);
                                ReplaceExprWithLeftChild(right);
                                changed = 1;
                            }
                            break;
                        case ESUB:
                            switch (nd->nodetype) {
                                case EADDV:
                                case EADD:
                                    nd->nodetype = ComplementaryOp[nd->nodetype];
                                    ReplaceExprWithRightChild(left);
                                    ReplaceExprWithLeftChild(right);
                                    IRO_SwitchChildren(nd);
                                    changed = 1;
                                    break;
                                default:
                                    break;
                            }
                            break;
                        case EMUL:
                            switch (nd->nodetype) {
                                case EADD:
                                case ESUB:
                                    PickCommonsubAtLeftRight(nd);
                                    changed = 2;
                                    break;
                                default:
                                    break;
                            }
                            break;
                        case EAND:
                            if (nd->nodetype == EXOR) {
                                PickCommonsubAtLeftRight(nd);
                                changed = 2;
                                break;
                            }
                        case EOR:
                            if (nd->nodetype == left->nodetype) {
                                ReplaceExprWithRightChild(left);
                                changed = 1;
                            } else if (nd->nodetype == ComplementaryOp[left->nodetype]) {
                                PickCommonsubAtLeftRight(nd);
                                changed = 2;
                            }
                            break;
                        default:
                            break;
                    }
                } else if (left->nodetype == ComplementaryOp[right->nodetype]) {
                    switch (nd->nodetype) {
                        case EADD:
                        case EADDV:
                            if (left->nodetype == EADD) {
                                ReplaceExprWithRightChild(left);
                                ReplaceExprWithLeftChild(right);
                                changed = 1;
                            }
                            break;

                        case ESUB:
                        case ESUBV:
                            if (left->nodetype == ESUB) {
                                ReplaceExprWithMonaminRight(left);
                                ReplaceExprWithRightChild(right);
                                changed = 1;
                            }
                            break;

                        case EAND:
                        case EOR:
                            if (left->nodetype == nd->nodetype) {
                                ReplaceExprWithLeftChild(nd);
                            } else if (right->nodetype == nd->nodetype) {
                                ReplaceExprWithRightChild(nd);
                            }
                            break;
                        default:
                            break;
                    }
                }
            } else if (IRO_ExprsSameSemantically(left->u.diadic.right, right->u.diadic.right)) {
                if (left->nodetype == right->nodetype) {
                    switch (nd->nodetype) {
                        case ESUB:
                            switch (left->nodetype) {
                                case EADD:
                                case ESUB:
                                    ReplaceExprWithLeftChild(left);
                                    ReplaceExprWithLeftChild(right);
                                    changed = 1;
                                default:
                                    break;
                            }
                        case EADD:
                            switch (left->nodetype) {
                                case EMUL:
                                case ESHL:
                                    PickCommonsubAtRightRight(nd);
                                    changed = 2;
                                    break;
                                default:
                                    break;
                            }
                            break;
                        case EXOR:
                            switch (left->nodetype) {
                                case ESHL:
                                case ESHR:
                                case EAND:
                                    PickCommonsubAtRightRight(nd);
                                    changed = 2;
                                    break;
                                default:
                                    break;
                            }
                            break;
                        case EAND:
                        case EOR:
                            if (left->nodetype == nd->nodetype) {
                                ReplaceExprWithLeftChild(right);
                                changed = 1;
                            } else if (left->nodetype == ComplementaryOp[nd->nodetype] ||
                                       left->nodetype == ESHR ||
                                       left->nodetype == ESHL) {
                                PickCommonsubAtRightRight(nd);
                                changed = 2;
                            }
                            break;
                        default:
                            break;
                    }
                } else if (left->nodetype == ComplementaryOp[right->nodetype]) {
                    switch (nd->nodetype) {
                        case EADD:
                        case EADDV:
                            switch (left->nodetype) {
                                case EADD:
                                case ESUB:
                                    ReplaceExprWithLeftChild(left);
                                    ReplaceExprWithLeftChild(right);
                                    changed = 1;
                                    break;
                                default:
                                    break;
                            }
                            break;

                        case EAND:
                        case EOR:
                            if (left->nodetype == nd->nodetype) {
                                ReplaceExprWithLeftChild(nd);
                            } else if (right->nodetype == nd->nodetype) {
                                ReplaceExprWithRightChild(nd);
                            }
                            break;
                        default:
                            break;
                    }
                }
            }

            if (changed) {
                DoDiadic(nd);
                if (changed == 2) {
                    DoDiadic(nd->u.diadic.left);
                } else if (changed == 3) {
                    DoDiadic(nd->u.diadic.right);
                }
                IRO_Dump("remove common op at: %d\n", nd->index);
            }
        }
    }
}

static void DoTransformations25(IROLinear* nd) {
    int changed = FALSE;
    IROLinear* left;
    IROLinear* right;

    if (nd->type == IROLinearOp2Arg) {
        left = nd->u.diadic.left;
        right = nd->u.diadic.right;

        if ((left->type == IROLinearOp2Arg &&
             (IS_TYPE_FLOAT(left->u.diadic.left->rtype) ||
              IS_TYPE_FLOAT(left->u.diadic.right->rtype))) ||
            (right->type == IROLinearOp2Arg &&
             (IS_TYPE_FLOAT(right->u.diadic.left->rtype) ||
              IS_TYPE_FLOAT(right->u.diadic.right->rtype)))) {
            return;
        }

        switch (left->nodetype) {
            case ELESS:
            case EGREATER:
            case ELESSEQU:
            case EGREATEREQU:
            case EEQU:
            case ENOTEQU:
                switch (nd->nodetype) {
                    case EEQU:
                        if (IRO_IsConstantOne(right)) {
                            ReplaceExprWithLeftChild(nd);
                            changed = TRUE;
                        } else if (IRO_IsConstantZero(right)) {
                            left->nodetype = ComplementaryOpLogical[left->nodetype];
                            ReplaceExprWithLeftChild(nd);
                            changed = TRUE;
                        }
                        break;
                    case ENOTEQU:
                        if (IRO_IsConstantOne(right)) {
                            left->nodetype = ComplementaryOpLogical[left->nodetype];
                            ReplaceExprWithLeftChild(nd);
                            changed = TRUE;
                        } else if (IRO_IsConstantZero(right)) {
                            ReplaceExprWithLeftChild(nd);
                            changed = TRUE;
                        }
                        break;
                    default:
                        break;
                }
                break;
            case ELAND:
            case ELOR:
                switch (nd->nodetype) {
                    case EEQU:
                        if (IRO_IsConstantOne(right)) {
                            ReplaceExprWithLeftChild(nd);
                            changed = TRUE;
                        }
                        break;
                    case ENOTEQU:
                        if (IRO_IsConstantZero(right)) {
                            ReplaceExprWithLeftChild(nd);
                            changed = TRUE;
                        }
                        break;
                    default:
                        break;
                }
                break;
            default:
                break;
        }

        if (!changed) {
            switch (right->nodetype) {
                case ELESS:
                case EGREATER:
                case ELESSEQU:
                case EGREATEREQU:
                case EEQU:
                case ENOTEQU:
                    switch (nd->nodetype) {
                        case EEQU:
                            if (IRO_IsConstantOne(left)) {
                                ReplaceExprWithRightChild(nd);
                            } else if (IRO_IsConstantZero(left)) {
                                right->nodetype = ComplementaryOpLogical[right->nodetype];
                                ReplaceExprWithRightChild(nd);
                            }
                            break;
                        case ENOTEQU:
                            if (IRO_IsConstantOne(left)) {
                                right->nodetype = ComplementaryOpLogical[right->nodetype];
                                ReplaceExprWithRightChild(nd);
                            } else if (IRO_IsConstantZero(left)) {
                                ReplaceExprWithRightChild(nd);
                            }
                            break;
                        default:
                            break;
                    }
                    break;
                case ELAND:
                case ELOR:
                    switch (nd->nodetype) {
                        case EEQU:
                            if (IRO_IsConstantOne(left)) {
                                ReplaceExprWithRightChild(nd);
                            }
                            break;
                        case ENOTEQU:
                            if (IRO_IsConstantZero(left)) {
                                ReplaceExprWithRightChild(nd);
                            }
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

static inline Boolean isOrderingOperator(ENodeType op) {
    switch (op) {
        case ELAND:
        case ELOR:
        case ECOMMA:
        case ECOND:
        case ECONDASS:
            return TRUE;
        default:
            return FALSE;
    }
}

static inline void RemoveUnreffed(IROLinear* nd) {
    if (!(nd->flags & IROLF_Reffed)) {
        switch (nd->type) {
            case IROLinearOperand:
                IRO_NopNonSideEffects(nd, 0);
                break;
            case IROLinearOp1Arg:
            case IROLinearOp2Arg:
            case IROLinearOp3Arg:
            case IROLinearFunccall:
                if (!isOrderingOperator(nd->nodetype)) {
                    IRO_NopNonSideEffects(nd, 0);
                }
                break;
            default:
                break;
        }
    }
}

// NOTE: invented names: father, grandfather
static void RemoveRedundantMonadicOp(IROLinear* nd) {
    IROLinear* father;
    IROLinear* grandfather;

    if (nd->type == IROLinearOp1Arg &&
        (father = IRO_LocateFather(nd)) &&
        father->nodetype == nd->nodetype) {
        switch (nd->nodetype) {
            case ELOGNOT:
                if ((grandfather = IRO_LocateFather(father))) {
                    if (grandfather->rtype &&
                        TYPE_INTEGRAL_UNCHECKED(grandfather->rtype) == IT_BOOL &&
                        nd->u.monadic->rtype &&
                        TYPE_INTEGRAL_UNCHECKED(nd->u.monadic->rtype) == IT_BOOL) {
                        goto remove;
                    }

                    if (grandfather->type == IROLinearIf) {
                        goto remove;
                    }
                    if (grandfather->type == IROLinearIfNot) {
                        goto remove;
                    }
                    if (grandfather->type == IROLinearOp3Arg && nd == grandfather->u.args3.a) {
                        goto remove;
                    }

                    switch (grandfather->nodetype) {
                        case ELOGNOT:
                        case ELAND:
                        case ELOR:
                            goto remove;
                        default:
                            break;
                    }
                }

                if (nd->u.monadic->type == IROLinearOp1Arg ||
                    nd->u.monadic->type == IROLinearOp2Arg) {
                    switch (nd->u.monadic->nodetype) {
                        case ELOGNOT:
                        case ELESS:
                        case EGREATER:
                        case ELESSEQU:
                        case EGREATEREQU:
                        case EEQU:
                        case ENOTEQU:
                        case ELAND:
                        case ELOR:
                            goto remove;
                        default:
                            break;
                    }
                }
                break;

            case EMONMIN:
            case EBINNOT:
remove:
                IRO_LocateFather_Cut_And_Paste_Without_Nopping(father, nd->u.monadic);
                nd->type = father->type = IROLinearNop;
                break;

            case ETYPCON:
                if (TYPE_INTEGRAL_UNCHECKED(nd->rtype) == IT_FLOAT) {
                    switch (TYPE_INTEGRAL_UNCHECKED(father->rtype)) {
                        case IT_DOUBLE:
                        case IT_LONGDOUBLE:
                            switch (TYPE_INTEGRAL_UNCHECKED(nd->u.monadic->rtype)) {
                                case IT_BOOL:
                                case IT_CHAR:
                                case IT_SCHAR:
                                case IT_UCHAR:
                                case IT_WCHAR_T:
                                case IT_SHORT:
                                case IT_USHORT:
                                    nd->type = IROLinearNop;
                                    father->u.monadic = nd->u.monadic;
                                    break;
                            }
                            break;
                    }
                }
                break;
            default:
                break;
        }
    }
}

static void ReverseOpForMonmin(IROLinear* nd) {
    IROLinear* father;

    if (nd->type == IROLinearOp1Arg &&
        nd->nodetype == EMONMIN &&
        (father = IRO_LocateFather(nd)) &&
        father->type == IROLinearOp2Arg &&
        father->u.diadic.right == nd) {
        switch (father->nodetype) {
            case EADDV:
            case ESUBV:
            case EADD:
            case ESUB:
            case EADDASS:
            case ESUBASS:
                father->nodetype = ComplementaryOp[father->nodetype];
                nd->type = IROLinearNop;
                father->u.diadic.right = nd->u.monadic;
                break;
            default:
                break;
        }
    }
}

static void DoDiadic(IROLinear* nd) {
    RemoveUnreffed(nd);
    DoTransformations(nd);
    DoTransformations11(nd);
    DoTransformations12(nd);
    DoTransformations13(nd);
    DoTransformations21(nd);
    DoTransformations22(nd);
    DoTransformations23(nd);
    DoTransformations24(nd);
    DoTransformations25(nd);
}

void IRO_DoTransformations(void) {
    IROLinear* nd;

    for (nd = IRO_FirstLinear; nd; nd = nd->next) {
        switch (nd->type) {
            case IROLinearOp2Arg:
                DoDiadic(nd);
                break;
            case IROLinearOp1Arg:
                RemoveUnreffed(nd);
                RemoveRedundantMonadicOp(nd);
                ReverseOpForMonmin(nd);
                break;
            case IROLinearOperand:
                RemoveUnreffed(nd);
                break;
            default:
                break;
        }
    }

    IRO_CheckForUserBreak();
}

static Boolean ReconcileAssignments(IROLinear* nd1, IROLinear* nd2, IROList* list) {
    IROLinear* copy;
    Boolean result = FALSE;
    int argCount;
    int i;
    IROLinear* savedTail; // NOTE: invented name

    if ((nd2->type == IROLinearOp1Arg && nd2->nodetype == ETYPCON) &&
        !(nd1->type == IROLinearOp1Arg && nd1->nodetype == ETYPCON) &&
        ReconcileAssignments(nd1, nd2->u.monadic, list)) {
        result = TRUE;
    }

    if ((nd1->type == IROLinearOp1Arg && nd1->nodetype == ETYPCON) &&
        !(nd2->type == IROLinearOp1Arg && nd2->nodetype == ETYPCON) &&
        ReconcileAssignments(nd1->u.monadic, nd2, list)) {
        copy = IRO_NewLinear(IROLinearOp1Arg);
        *copy = *nd2;
        copy->index = IRO_NumLinear++;
        copy->type = IROLinearOp1Arg;
        copy->nodetype = ETYPCON;
        copy->rtype = nd1->rtype;
        copy->next = NULL;
        copy->u.monadic = list->tail;
        IRO_AddToList(copy, list);
        result = TRUE;
    }

    if (nd1->type == nd2->type && nd1->nodetype == nd2->nodetype) {
        copy = IRO_NewLinear(IROLinearNop);
        *copy = *nd2;
        copy->index = IRO_NumLinear++;
        copy->rtype = nd1->rtype;
        copy->next = NULL;
        switch (nd1->type) {
            case IROLinearOperand:
                if (nd1->u.node->type == nd2->u.node->type) {
                    if (!(nd1->u.node->type == EOBJREF &&
                          nd1->u.node->data.objref != nd2->u.node->data.objref)) {
                        result = TRUE;
                    }
                }
                break;

            case IROLinearOp1Arg:
                if (ReconcileAssignments(nd1->u.monadic, nd2->u.monadic, list)) {
                    copy->u.monadic = list->tail;
                    result = TRUE;
                }
                break;

            case IROLinearOp2Arg:
                savedTail = list->tail;
                if (ReconcileAssignments(nd1->u.diadic.left, nd2->u.diadic.left, list)) {
                    copy->u.diadic.left = list->tail;
                    if (ReconcileAssignments(nd1->u.diadic.right, nd2->u.diadic.right, list)) {
                        copy->u.diadic.right = list->tail;
                        result = TRUE;
                    }
                }

                if (!result && !IRO_HasSideEffect(nd1) && !IRO_HasSideEffect(nd2)) {
                    if (nd1->nodetype == EMUL ||
                        nd1->nodetype == EADD ||
                        nd1->nodetype == EAND ||
                        nd1->nodetype == EXOR ||
                        nd1->nodetype == EOR) {
                        list->tail = savedTail;
                        if (ReconcileAssignments(nd1->u.diadic.left, nd2->u.diadic.right, list)) {
                            copy->u.diadic.right = list->tail;
                            if (ReconcileAssignments(
                                    nd1->u.diadic.right, nd2->u.diadic.left, list
                                )) {
                                copy->u.diadic.left = list->tail;
                                result = TRUE;
                            }
                        }
                    }
                }
                break;

            case IROLinearOp3Arg:
                if (ReconcileAssignments(nd1->u.args3.c, nd2->u.args3.c, list)) {
                    copy->u.args3.c = list->tail;
                    if (ReconcileAssignments(nd1->u.args3.b, nd2->u.args3.b, list)) {
                        copy->u.args3.b = list->tail;
                        if (ReconcileAssignments(nd1->u.args3.a, nd2->u.args3.a, list)) {
                            copy->u.args3.a = list->tail;
                            result = TRUE;
                        }
                    }
                }
                break;

            case IROLinearFunccall:
                argCount = nd1->u.funccall.argCount;
                if (argCount == nd2->u.funccall.argCount) {
                    result = TRUE;
                    copy->u.funccall.args = oalloc(sizeof(IROLinear*) * argCount);
                    for (i = argCount - 1; i >= 0; i--) {
                        if (!ReconcileAssignments(
                                nd1->u.funccall.args[i], nd2->u.funccall.args[i], list
                            )) {
                            result = FALSE;
                            break;
                        }
                        copy->u.funccall.args[i] = list->tail;
                    }

                    if (result) {
                        if (!ReconcileAssignments(
                                nd1->u.funccall.callee, nd2->u.funccall.callee, list
                            )) {
                            result = FALSE;
                            break;
                        }
                        copy->u.funccall.callee = list->tail;
                    }
                }
                break;
            default:
                break;
        }

        if (result) {
            IRO_AddToList(copy, list);
        }
    }

    return result;
}

static IROLinear* FrontendTransformSelfAssignmentToAssignment(IROLinear* expr) {
    Statement* stmt;
    IROLinear* nd;
    IROLinearIRSave save;
    IROList list;

    IRO_SaveLinearIR(&save);

    IRO_InitList(&list);
    IRO_DuplicateExpr(expr, &list);

    stmt = IRO_Delinearize(NULL, list.head);
    CError_ASSERT(3645, stmt);
    CError_ASSERT(3647, stmt->expr);
    stmt->expr = CIRTrans_TransformOpAss(stmt->expr);
    CError_ASSERT(3652, stmt->expr);

    if (DoLinearize) {
        IRO_PreLinearize(stmt);
    }
    IRO_Linearize(stmt);

    IRO_InitList(&list);
    list.head = IRO_FirstLinear;
    list.tail = IRO_LastLinear;

    IRO_RestoreLinearIR(&save);

    for (nd = list.head; nd; nd = nd->next) {
        if (!(nd->flags & IROLF_Reffed) && IRO_IsAssignment(nd)) {
            break;
        }
    }

    return nd;
}

static Type* PromotedIntegralType(Type* type) {
    CError_ASSERT(3681, IS_TYPE_ENUM(type) || IS_TYPE_INT(type));

    if (IS_TYPE_ENUM(type)) {
        type = TYPE_ENUM(type)->enumtype;
    }

    if (TYPE_INTEGRAL(type)->integral < IT_INT) {
        if (is_unsigned(type)) {
            return TYPE(&stunsignedint);
        } else {
            return TYPE(&stsignedint);
        }
    } else {
        return type;
    }
}

static Boolean TransformMonadicSelfAssignmentToDiadicSelfAssignment(IROLinear* nd) {
    ENodeType t;
    ENodeType newtype;
    IROLinear* incExpr;
    IROLinear* varExpr;

    t = nd->nodetype;

    if (IRO_IsAssignment(nd) && IRO_IsModifyOp[t]) {
        varExpr = incExpr = NULL;
        newtype = MAXEXPR;

        if (nd->type == IROLinearOp1Arg &&
            (t == EPOSTINC || t == EPOSTDEC || t == EPREINC || t == EPREDEC) &&
            (!(nd->flags & IROLF_Reffed) || t == EPREINC || t == EPREDEC)) {
            Type* type = nd->rtype;
            TypeType typetype = type->type;
            varExpr = nd->u.monadic;
            if (typetype == TYPEINT || typetype == TYPEENUM) {
                incExpr = IRO_NewIntConst(cint64_one, PromotedIntegralType(type));
            } else if (typetype == TYPEPOINTER ||
                       typetype == TYPEARRAY ||
                       typetype == TYPEMEMBERPOINTER) {
                Type* inner = NULL;
                CInt64 val = cint64_zero;

                if (typetype == TYPEPOINTER || typetype == TYPEARRAY) {
                    inner = TPTR_TARGET(type);
                } else if (typetype == TYPEMEMBERPOINTER) {
                    inner = TYPE_MEMBER_POINTER(type)->ty1;
                }

                if (inner) {
                    CInt64_SetLong(&val, inner->size);
                }

                if (!CInt64_IsZero(&val)) {
                    incExpr = IRO_NewIntConst(val, TYPE(&stsignedlong));
                } else {
                    return FALSE;
                }
            } else if (typetype == TYPEFLOAT) {
                Float fval;
                fval = CMach_CalcFloatConvertFromInt(type, cint64_one);
                incExpr = IRO_NewFloatConst(fval, nd->rtype);
            } else {
                return FALSE;
            }

            if (t == EPOSTINC || t == EPREINC) {
                newtype = EADDASS;
            } else {
                newtype = ESUBASS;
            }
        }

        if (varExpr &&
            incExpr &&
            newtype != MAXEXPR &&
            varExpr->u.diadic.left &&
            varExpr->u.diadic.left->type == IROLinearOperand &&
            varExpr->u.diadic.left->u.node &&
            varExpr->u.diadic.left->u.node->type == EOBJREF &&
            !IRO_HasSideEffect(varExpr)) {
            incExpr->flags |= IROLF_Reffed;
            nd->nodetype = newtype;
            nd->u.diadic.right = incExpr;
            nd->type = IROLinearOp2Arg;
            IRO_Paste(incExpr, incExpr, nd);
            return TRUE;
        }
    }

    return FALSE;
}

Boolean IRO_TransformSelfAssignmentToAssignment(IROLinear* nd) {
    ENodeType nonAssOp;
    IROLinear* left;
    IROLinear* right;
    ENodeType nodetype;
    IROLinear* nonAss;
    IROLinear* dupLeft;
    IROLinear* last;
    IROList list1;
    IROList list2;

    nodetype = nd->nodetype;
    if (IRO_IsAssignment(nd) &&
        IRO_IsModifyOp[nodetype] &&
        nd->type == IROLinearOp1Arg &&
        TransformMonadicSelfAssignmentToDiadicSelfAssignment(nd)) {
        nodetype = nd->nodetype;
    }

    if (IRO_IsAssignment(nd) &&
        IRO_IsModifyOp[nodetype] &&
        nd->type == IROLinearOp2Arg &&
        IRO_NonAssignmentOp[nodetype] != MAXEXPR) {
        left = nd->u.diadic.left;
        right = nd->u.diadic.right;
        nonAssOp = IRO_NonAssignmentOp[nodetype];
        if (left &&
            right &&
            nonAssOp != MAXEXPR &&
            left->u.monadic &&
            left->u.monadic->type == IROLinearOperand &&
            left->u.monadic->u.node &&
            left->u.monadic->u.node->type == EOBJREF &&
            !IRO_HasSideEffect(left)) {
            IRO_InitList(&list1);
            dupLeft = IRO_DuplicateExpr(left, &list1);

            if (left->rtype != right->rtype) {
                IROLinear* cast = IRO_NewLinear(IROLinearOp1Arg); // NOTE: invented name
                cast->nodetype = ETYPCON;
                cast->flags |= IROLF_Reffed;
                cast->rtype = right->rtype;
                cast->index = ++IRO_NumLinear;
                cast->u.monadic = dupLeft;
                IRO_AddToList(cast, &list1);
                dupLeft = cast;
            }

            nonAss = IRO_NewLinear(IROLinearOp2Arg);
            nonAss->nodetype = nonAssOp;
            nonAss->flags |= IROLF_Reffed;
            nonAss->rtype = dupLeft->rtype;
            nonAss->index = ++IRO_NumLinear;
            nonAss->u.diadic.left = dupLeft;
            nonAss->u.diadic.right = right;
            IRO_AddToList(nonAss, &list1);

            if (left->rtype != right->rtype) {
                IROLinear* cast = IRO_NewLinear(IROLinearOp1Arg);
                cast->nodetype = ETYPCON;
                cast->flags |= IROLF_Reffed;
                cast->rtype = left->rtype;
                cast->index = ++IRO_NumLinear;
                cast->u.monadic = nonAss;
                IRO_AddToList(cast, &list1);
                nonAss = cast;
            }

            IRO_InitList(&list2);
            last = FrontendTransformSelfAssignmentToAssignment(nd);
            if (last &&
                last->type == IROLinearOp2Arg &&
                ReconcileAssignments(last->u.diadic.right, nonAss, &list2)) {
                IRO_NopOut(nd->u.diadic.right);
                nd->nodetype = EASS;
                nd->u.diadic.right = list2.tail;
                nd->type = IROLinearOp2Arg;
                IRO_Paste(list2.head, list2.tail, nd);
                return TRUE;
            }
        }
    }

    return FALSE;
}

static void AddAddend(ENode* expr) {
    if (expr->type == EADD) {
        AddAddend(expr->data.diadic.left);
        AddAddend(expr->data.diadic.right);
    } else {
        ENodeList* list = oalloc(sizeof(ENodeList));
        list->node = expr;
        list->next = NULL;

        if (FirstAddend) {
            LastAddend->next = list;
        } else {
            FirstAddend = list;
        }
        LastAddend = list;
    }
}

static ENode* CombineConstants(ENode* expr) {
    ENode* addend;
    ENodeList* el;
    ENode* result;
    ENode* var;
    Type* type;
    ENode* tmp;
    ENodeList* prev;
    CInt64 val;

    FirstAddend = LastAddend = NULL;
    AddAddend(expr->data.diadic.left);
    AddAddend(expr->data.diadic.right);

    el = FirstAddend;
    prev = NULL;
    var = NULL;
    while (el) {
        addend = el->node;
        if (addend->type == EOBJREF) {
            var = addend;
            if (prev) {
                prev->next = el->next;
            } else {
                FirstAddend = el->next;
            }
            break;
        }
        prev = el;
        el = el->next;
    }

    if (!var) {
        el = FirstAddend;
        prev = NULL;
        while (el) {
            addend = el->node;
            if (addend->type == EINDIRECT) {
                var = addend;
                if (prev) {
                    prev->next = el->next;
                } else {
                    FirstAddend = el->next;
                }
                break;
            }
            prev = el;
            el = el->next;
        }
    }

    prev = NULL;
    CInt64_SetLong(&val, 0);
    type = NULL;

    for (el = FirstAddend; el; el = el->next) {
        addend = el->node;
        if (addend->type == EINTCONST && addend->rtype) {
            if (!type || type->size < addend->rtype->size) {
                type = addend->rtype;
            }
            val = CInt64_Add(val, addend->data.intval);
            if (prev) {
                prev->next = el->next;
            } else {
                FirstAddend = el->next;
            }
        } else if (addend->type == EMUL &&
                   addend->data.diadic.right->type == EINTCONST &&
                   addend->rtype) {
            if (!type || type->size < addend->rtype->size) {
                type = addend->rtype;
            }

            tmp = addend->data.diadic.left;
            if (tmp->type == EADD && tmp->data.diadic.right->type == EINTCONST) {
                val = CInt64_Add(
                    val,
                    CInt64_MulU(
                        tmp->data.diadic.right->data.intval, addend->data.diadic.right->data.intval
                    )
                );
                addend->data.diadic.left = tmp->data.diadic.left;
            }
            prev = el;
        } else {
            prev = el;
        }
    }

    result = NULL;
    if (var) {
        result = var;
        if (!CInt64_IsZero(&val)) {
            result = IRO_NewENode(EADD);
            result->data.diadic.left = var;
            result->data.diadic.right = IRO_NewENode(EINTCONST);
            result->data.diadic.right->data.intval = val;
            result->data.diadic.right->rtype = type;
            result->rtype = var->rtype;
            result->cost = 1;
            CInt64_SetLong(&val, 0);
        }
    }

    for (el = FirstAddend; el; el = el->next) {
        addend = el->node;
        if (result) {
            tmp = IRO_NewENode(EADD);
            tmp->data.diadic.left = result;
            tmp->data.diadic.right = addend;
            tmp->cost = result->cost + 1;
            tmp->rtype = result->rtype;
            result = tmp;
        } else {
            result = addend;
        }
    }

    if (!CInt64_IsZero(&val)) {
        tmp = IRO_NewENode(EADD);
        tmp->data.diadic.left = result;
        tmp->data.diadic.right = IRO_NewENode(EINTCONST);
        tmp->data.diadic.right->data.intval = val;
        tmp->data.diadic.right->rtype = type;
        tmp->cost = result->cost + 1;
        tmp->rtype = result->rtype;
        result = tmp;
    }

    return result;
}

static ENode* TransformExprNode(ENode* expr) {
    ENode* left;

    switch (expr->type) {
        case EINDIRECT:
            if (ENODE_IS(expr->data.monadic, EADD)) {
                expr->data.monadic = CombineConstants(expr->data.monadic);
            }
            break;

        case EMUL:
        case EADD:
        case EAND:
        case EXOR:
        case EOR:
            if (IS_TYPE_INT(expr->rtype) &&
                !ENODE_IS(expr->data.diadic.right, EINTCONST) &&
                ENODE_IS(expr->data.diadic.left, EINTCONST)) {
                left = expr->data.diadic.left;
                expr->data.diadic.left = expr->data.diadic.right;
                expr->data.diadic.right = left;
            }
            break;

        case EEQU:
        case ENOTEQU:
            if (IS_TYPE_INT(expr->rtype) &&
                !ENODE_IS(expr->data.diadic.right, EINTCONST) &&
                ENODE_IS(expr->data.diadic.left, EINTCONST)) {
                left = expr->data.diadic.left;
                expr->data.diadic.left = expr->data.diadic.right;
                expr->data.diadic.right = left;
            }

            if (ENODE_IS(expr->data.diadic.right, EINTCONST) &&
                ENODE_IS(expr->data.diadic.left, EBINNOT)) {
                left = expr->data.diadic.left;
                expr->data.diadic.left = left->data.monadic;
                left->data.monadic = expr->data.diadic.right;
                expr->data.diadic.right = left;
            }

            break;
        default:
            break;
    }

    return expr;
}

static ENode* TransformExprTree(ENode* expr) {
    ENodeList* list;

    switch (expr->type) {
ENODE_CASE_MONADIC:
        expr->data.monadic = TransformExprTree(expr->data.monadic);
        break;

ENODE_CASE_DIADIC_ALL:
        expr->data.diadic.left = TransformExprTree(expr->data.diadic.left);
        expr->data.diadic.right = TransformExprTree(expr->data.diadic.right);
        break;

        case EFUNCCALL:
        case EFUNCCALLP:
            TransformExprTree(expr->data.funccall.funcref);
            for (list = expr->data.funccall.args; list; list = list->next) {
                TransformExprTree(list->node);
            }
            break;

        case ECOND:
            TransformExprTree(expr->data.cond.cond);
            TransformExprTree(expr->data.cond.expr1);
            TransformExprTree(expr->data.cond.expr2);
            break;

        case ENULLCHECK:
            TransformExprTree(expr->data.nullcheck.nullcheckexpr);
            TransformExprTree(expr->data.nullcheck.condexpr);
            break;
        default:
            break;
    }

    return TransformExprNode(expr);
}

static void FoldConstantsinAssociativeExprs(ENode* expr) {
    short nodetype1;
    short nodetype2;
    short nodetype3;
    Boolean changed;
    short op;
    CInt64 val1;
    CInt64 val2;
    CInt64 typeBits; // NOTE: invented name
    CInt64 sum;

    if ((expr->type == EADD ||
         expr->type == EMUL ||
         expr->type == EAND ||
         expr->type == EXOR ||
         expr->type == EOR ||
         expr->type == ESHL ||
         expr->type == ESHR) &&
        IS_TYPE_INT(expr->rtype) &&
        expr->data.diadic.right->type == EINTCONST) {
        do {
            changed = FALSE;

            if (expr->data.diadic.left->type == expr->type &&
                expr->data.diadic.left->data.diadic.right->type == EINTCONST) {
                val1 = expr->data.diadic.right->data.intval;
                val2 = expr->data.diadic.left->data.diadic.right->data.intval;
                switch (expr->type) {
                    case EADD:
                    case ESHL:
                        op = '+';
                        break;
                    case ESHR:
                        op = '+';
                        if (!is_unsigned(expr->rtype)) {
                            CInt64_SetLong(&typeBits, expr->rtype->size * 8);
                            if (CInt64_GreaterEqualU(val1, typeBits) ||
                                CInt64_GreaterEqualU(val2, typeBits)) {
                                return;
                            }

                            sum = CMach_CalcIntDiadic(expr->rtype, val1, '+', val2);
                            if (CInt64_GreaterEqualU(sum, typeBits)) {
                                val1 = CInt64_Sub(typeBits, cint64_one);
                                val2 = cint64_zero;
                            }
                        }
                        break;
                    case EMUL:
                        op = '*';
                        break;
                    case EAND:
                        op = '&';
                        break;
                    case EOR:
                        op = '|';
                        break;
                    case EXOR:
                        op = '^';
                        break;
                    default:
                        return;
                }

                expr->data.diadic.right->data.intval =
                    CMach_CalcIntDiadic(expr->rtype, val1, op, val2);
                expr->data.diadic.left = expr->data.diadic.left->data.diadic.left;
                changed = TRUE;
            } else if (((nodetype1 = expr->type) == EAND || nodetype1 == EOR) &&
                       ((nodetype2 = expr->data.diadic.left->type) == EAND || nodetype2 == EOR) &&
                       ((nodetype3 = expr->data.diadic.left->data.diadic.left->type) == EAND ||
                        nodetype3 == EOR) &&
                       expr->data.diadic.left->data.diadic.left->data.diadic.right->type ==
                           EINTCONST) {
                val1 = expr->data.diadic.right->data.intval;
                if (CInt64_Equal(
                        val1,
                        expr->data.diadic.left->data.diadic.left->data.diadic.right->data.intval
                    )) {
                    if (nodetype1 == nodetype3) {
                        expr->data.diadic.left->data.diadic.left =
                            expr->data.diadic.left->data.diadic.left->data.diadic.left;
                        changed = TRUE;
                    } else if (nodetype2 == nodetype3) {
                        *expr = *expr->data.diadic.right;
                    } else {
                        expr->data.diadic.left = expr->data.diadic.left->data.diadic.right;
                        changed = TRUE;
                    }
                }
            }
        } while (changed);
    }
}

static void TransformExprTree1(ENode* expr) {
    ENodeList* list;

    switch (expr->type) {
ENODE_CASE_MONADIC:
        TransformExprTree1(expr->data.monadic);
        break;

ENODE_CASE_DIADIC_ALL:
        TransformExprTree1(expr->data.diadic.left);
        TransformExprTree1(expr->data.diadic.right);
        break;

        case EFUNCCALL:
        case EFUNCCALLP:
            TransformExprTree1(expr->data.funccall.funcref);
            for (list = expr->data.funccall.args; list; list = list->next) {
                TransformExprTree1(list->node);
            }
            break;

        case ECOND:
            TransformExprTree1(expr->data.cond.cond);
            TransformExprTree1(expr->data.cond.expr1);
            TransformExprTree1(expr->data.cond.expr2);
            break;

        case ENULLCHECK:
            TransformExprTree1(expr->data.nullcheck.nullcheckexpr);
            TransformExprTree1(expr->data.nullcheck.condexpr);
            break;
        default:
            break;
    }

    FoldConstantsinAssociativeExprs(expr);
}

static int RemoveRedundantBitOperations(ENode* expr) {
    Boolean a;
    Boolean b;

    if (expr->type == ExprType) {
        a = RemoveRedundantBitOperations(expr->data.diadic.left);
        b = RemoveRedundantBitOperations(expr->data.diadic.right);
        return a & b;
    }

    if (expr->type == EINDIRECT) {
        if (expr->data.monadic->type == EOBJREF) {
            if (!OperandObject) {
                OperandObject = expr->data.monadic->data.objref;
                IndirectRef = expr;
                return TRUE;
            }
            if (expr->data.monadic->data.objref != OperandObject) {
                return FALSE;
            }
            return TRUE;
        } else {
            return FALSE;
        }
    }

    if (expr->type == EINTCONST) {
        if (FirstTime) {
            OperandConst = expr->data.intval;
            FirstTime = FALSE;
        } else if (ExprType == EAND) {
            OperandConst = CInt64_And(expr->data.intval, OperandConst);
        } else if (ExprType == EOR) {
            OperandConst = CInt64_Or(expr->data.intval, OperandConst);
        } else if (ExprType == EXOR) {
            OperandConst = CInt64_Xor(expr->data.intval, OperandConst);
        }
        return TRUE;
    }

    return FALSE;
}

static void TransformExprTree2(ENode* expr) {
    ENodeList* list;

    switch (expr->type) {
ENODE_CASE_MONADIC:
        TransformExprTree2(expr->data.monadic);
        break;

ENODE_CASE_DIADIC_ALL:
        TransformExprTree2(expr->data.diadic.left);
        TransformExprTree2(expr->data.diadic.right);
        break;

        case EFUNCCALL:
        case EFUNCCALLP:
            TransformExprTree2(expr->data.funccall.funcref);
            for (list = expr->data.funccall.args; list; list = list->next) {
                TransformExprTree2(list->node);
            }
            break;

        case ECOND:
            TransformExprTree2(expr->data.cond.cond);
            TransformExprTree2(expr->data.cond.expr1);
            TransformExprTree2(expr->data.cond.expr2);
            break;

        case ENULLCHECK:
            TransformExprTree2(expr->data.nullcheck.nullcheckexpr);
            TransformExprTree2(expr->data.nullcheck.condexpr);
            break;
        default:
            break;
    }

    if (ENODE_IS3(expr, EAND, EOR, EXOR) &&
        (expr->type == expr->data.diadic.left->type ||
         expr->type == expr->data.diadic.right->type)) {
        OperandObject = NULL;
        ExprType = expr->type;
        FirstTime = TRUE;
        IndirectRef = NULL;

        if (RemoveRedundantBitOperations(expr)) {
            expr->data.diadic.left = IndirectRef;
            expr->data.diadic.right->type = EINTCONST;
            expr->data.diadic.right->data.intval = OperandConst;
        }
    }
}

void IRO_TransformTree(Statement* statements) {
    Statement* stmt;
    Statement* next;

    for (stmt = statements; stmt; stmt = next) {
        next = stmt->next;
        switch (stmt->type) {
            case ST_EXPRESSION:
            case ST_SWITCH:
            case ST_IFGOTO:
            case ST_IFNGOTO:
            case ST_RETURN:
                if (stmt->expr) {
                    stmt->expr = TransformExprTree(stmt->expr);
                    TransformExprTree2(stmt->expr);
                    TransformExprTree1(stmt->expr);
                }
                break;
            default:
                break;
        }
    }

    IRO_CheckForUserBreak();
}
