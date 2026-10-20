#include "compiler/IroSubable.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroPropagate.h"
#include "compiler/IroUtil.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/types.h"

static Boolean IsSubableOp[MAXEXPR];

void IRO_InitializeIsSubableOpArray(void) {
    int i;

    for (i = 0; i < MAXEXPR; i++) {
        IsSubableOp[i] = FALSE;
    }

    IsSubableOp[EPOSTINC] = FALSE;
    IsSubableOp[EPOSTDEC] = FALSE;
    IsSubableOp[EPREINC] = FALSE;
    IsSubableOp[EPREDEC] = FALSE;
    IsSubableOp[EINDIRECT] = FALSE;
    IsSubableOp[EMONMIN] = TRUE;
    IsSubableOp[EBINNOT] = TRUE;
    IsSubableOp[ELOGNOT] = TRUE;
    IsSubableOp[EFORCELOAD] = FALSE;
    IsSubableOp[EMUL] = TRUE;
    IsSubableOp[EMULV] = TRUE;
    IsSubableOp[EDIV] = TRUE;
    IsSubableOp[EMODULO] = TRUE;
    IsSubableOp[EADDV] = TRUE;
    IsSubableOp[ESUBV] = TRUE;
    IsSubableOp[EADD] = TRUE;
    IsSubableOp[ESUB] = TRUE;
    IsSubableOp[ESHL] = TRUE;
    IsSubableOp[ESHR] = TRUE;
    IsSubableOp[ELESS] = FALSE;
    IsSubableOp[EGREATER] = FALSE;
    IsSubableOp[ELESSEQU] = FALSE;
    IsSubableOp[EGREATEREQU] = FALSE;
    IsSubableOp[EEQU] = FALSE;
    IsSubableOp[ENOTEQU] = FALSE;
    IsSubableOp[EAND] = TRUE;
    IsSubableOp[EXOR] = TRUE;
    IsSubableOp[EOR] = TRUE;
    IsSubableOp[ELAND] = FALSE;
    IsSubableOp[ELOR] = FALSE;
    IsSubableOp[EASS] = FALSE;
    IsSubableOp[EMULASS] = FALSE;
    IsSubableOp[EDIVASS] = FALSE;
    IsSubableOp[EMODASS] = FALSE;
    IsSubableOp[EADDASS] = FALSE;
    IsSubableOp[ESUBASS] = FALSE;
    IsSubableOp[ESHLASS] = FALSE;
    IsSubableOp[ESHRASS] = FALSE;
    IsSubableOp[EANDASS] = FALSE;
    IsSubableOp[EXORASS] = FALSE;
    IsSubableOp[EORASS] = FALSE;
    IsSubableOp[ECOMMA] = FALSE;
    IsSubableOp[EPMODULO] = FALSE;
    IsSubableOp[EROTL] = FALSE;
    IsSubableOp[EROTR] = FALSE;
    IsSubableOp[EBCLR] = FALSE;
    IsSubableOp[EBTST] = FALSE;
    IsSubableOp[EBSET] = FALSE;
    IsSubableOp[ETYPCON] = FALSE;
    IsSubableOp[EBITFIELD] = FALSE;
    IsSubableOp[EINTCONST] = FALSE;
    IsSubableOp[EFLOATCONST] = FALSE;
    IsSubableOp[ESTRINGCONST] = FALSE;
    IsSubableOp[ECOND] = FALSE;
    IsSubableOp[EFUNCCALL] = FALSE;
    IsSubableOp[EFUNCCALLP] = FALSE;
    IsSubableOp[EOBJREF] = FALSE;
    IsSubableOp[EMFPOINTER] = FALSE;
    IsSubableOp[ENULLCHECK] = FALSE;
    IsSubableOp[EPRECOMP] = FALSE;
    IsSubableOp[ETEMP] = FALSE;
    IsSubableOp[EARGOBJ] = FALSE;
    IsSubableOp[ELOCOBJ] = FALSE;
    IsSubableOp[ELABEL] = FALSE;
    IsSubableOp[ESETCONST] = FALSE;
    IsSubableOp[ENEWEXCEPTION] = FALSE;
    IsSubableOp[ENEWEXCEPTIONARRAY] = FALSE;
    IsSubableOp[EOBJLIST] = FALSE;
    IsSubableOp[EMEMBER] = FALSE;
    IsSubableOp[ETEMPLDEP] = FALSE;
    IsSubableOp[EINSTRUCTION] = FALSE;
    IsSubableOp[EDEFINE] = FALSE;
    IsSubableOp[EREUSE] = FALSE;
    IsSubableOp[EASSBLK] = FALSE;
    IsSubableOp[EVECTOR128CONST] = FALSE;
    IsSubableOp[ECONDASS] = FALSE;
}

static int IsSubscript(IROLinear* nd) {
    return FALSE;
}

Boolean IRO_IsSubableExpression(IROLinear* nd) {
    Object* varobj;
    Boolean result;

    switch (nd->type) {
        case IROLinearOp2Arg:
            if (nd->nodetype == EADD || nd->nodetype == ESUB) {
                if (IRO_IsConstant(nd->u.diadic.right) &&
                    (varobj = IRO_IsVariable(nd->u.diadic.left)) &&
                    varobj->datatype == DLOCAL &&
                    varobj->u.var.info &&
                    !varobj->u.var.info->noregister) {
                    return FALSE;
                }
            }
            result = IsSubableOp[nd->nodetype] && !IsSubscript(nd);
            return result;
        case IROLinearOp1Arg:
            if (IsSubableOp[nd->nodetype] && !IsSubscript(nd)) {
                return TRUE;
            }

            if (nd->nodetype == EINDIRECT && !(nd->flags & IROLF_Assigned)) {
                if (nd->flags & IROLF_Ind) {
                    nd = nd->u.monadic;
                    if (nd->type == IROLinearOperand &&
                        nd->u.node->type == EOBJREF &&
                        nd->u.node->data.objref->datatype == DLOCAL &&
                        nd->u.node->data.objref->u.var.info &&
                        !nd->u.node->data.objref->u.var.info->noregister) {
                        return FALSE;
                    }
                    return TRUE;
                }

                if (IRO_IsVariable(nd) && IRO_IsRegable(nd->u.monadic->u.node->data.objref)) {
                    return FALSE;
                }
                return TRUE;
            } else if (nd->nodetype == ETYPCON &&
                       IS_TYPE_INT(nd->rtype) &&
                       nd->rtype->size >= nd->u.monadic->rtype->size) {
                return TRUE;
            } else {
                return FALSE;
            }
        case IROLinearOperand:
        default:
            return FALSE;
    }
}

// Dead in every release (nothing calls it; the linker drops it, and it has no data of its own).
Boolean IRO_IsVectorTempCandidate(IROLinear* nd) {
    return (nd->type == IROLinearOp1Arg ||
            nd->type == IROLinearOp2Arg ||
            nd->type == IROLinearOp3Arg ||
            nd->type == IROLinearOperand ||
            nd->type == IROLinearFunccall) &&
        ((nd->flags & IROLF_LoopInvariant) && !(nd->flags & IROLF_Ind));
}
