#include "compiler/CObjCModern.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CObjC.h"
#include "compiler/CompilerTools.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/enode.h"
#include "compiler/tokens.h"
#include <string.h>

static ObjCNamedArg* CObjC_GetKeywordArgList(HashNameNode* name, Boolean* isSingleArg) {
    ObjCNamedArg* first;
    ObjCNamedArg* arg;
    char* start;
    char* p;

    start = name->name;
    first = NULL;

    while (1) {
        for (p = start; *p != '_'; p++) {
            if (*p == 0) {
                if (!first) {
                    first = lalloc(sizeof(ObjCNamedArg));
                    memclrw(first, sizeof(ObjCNamedArg));

                    first->name = name;

                    *isSingleArg = TRUE;
                    return first;
                }

                arg->next = lalloc(sizeof(ObjCNamedArg));
                arg = arg->next;

                arg->next = NULL;
                arg->name = GetHashNameNode(start);
                arg->expr = NULL;

                *isSingleArg = FALSE;
                return first;
            }
        }

        if (first) {
            arg->next = lalloc(sizeof(ObjCNamedArg));
            arg = arg->next;
        } else {
            arg = lalloc(sizeof(ObjCNamedArg));
            first = arg;
        }

        *p = 0;
        arg->next = NULL;
        arg->name = GetHashNameNode(start);
        arg->expr = NULL;
        *p = '_';

        p++;
        if (*p == 0) {
            *isSingleArg = FALSE;
            return first;
        }

        start = p;
    }
}

static ENode* CObjC_ModernSendMessage(
    TypeClass* tclass, ENode* expr, HashNameNode* name, Boolean isSuper
) {
    ENode* callexpr;
    ObjCNamedArg* args;
    ObjCNamedArg* arg;
    ENodeList* unnamedArgs;
    Boolean isSingleArg;

    CError_ASSERT(92, ENODE_IS(expr, EINDIRECT));
    expr = expr->data.monadic;

    args = CObjC_GetKeywordArgList(name, &isSingleArg);

    if ((tk = lex()) == ')') {
        if (!isSingleArg) {
            CError_Error(ERR_FUNCTION_CALL_DOES_NOT_MATCH_PROTOTYPE);
            return nullnode();
        }

        callexpr = CObjC_MakeSendMsgExpr(expr, tclass, args, NULL, 1, isSuper);
        tk = lex();
        return callexpr;
    }

    if (isSingleArg) {
        CError_Error(ERR_FUNCTION_CALL_DOES_NOT_MATCH_PROTOTYPE);
        return nullnode();
    }

    unnamedArgs = NULL;
    arg = args;
    while (1) {
        arg->expr = assignment_expression();

        if (tk == ')') {
            if (arg->next) {
                CError_Error(ERR_FUNCTION_CALL_DOES_NOT_MATCH_PROTOTYPE);
                return nullnode();
            }
            break;
        }

        if (tk != ',') {
            CError_Error(ERR_EXPRESSION_SYNTAX_ERROR);
            return nullnode();
        }

        tk = lex();
        arg = arg->next;

        if (!arg) {
            unnamedArgs = CExpr_ScanExpressionList(FALSE);
            break;
        }
    }

    callexpr = CObjC_MakeSendMsgExpr(expr, tclass, args, unnamedArgs, 1, isSuper);
    tk = lex();
    return callexpr;
}

ENode* CObjC_CheckModernSendMessage(TypeClass* tclass, ENode* expr) {
    HashNameNode* name;
    SInt32 state;

    CPrep_TokenStreamGetState(&state);

    tk = lex();
    CObjC_TranslateSelectorToken();
    if (tk == TK_IDENTIFIER) {
        if (!strcmp(tkidentifier->name, "super")) {
            if (lex() == TK_COLON_COLON) {
                tk = lex();
                CObjC_TranslateSelectorToken();

                name = tkidentifier;
                if (tk == TK_IDENTIFIER && lex() == '(') {
                    return CObjC_ModernSendMessage(tclass, expr, name, TRUE);
                }
            }
        } else {
            name = tkidentifier;
            if (lex() == '(') {
                return CObjC_ModernSendMessage(tclass, expr, name, FALSE);
            }
        }
    }

    CPrep_TokenStreamSetState(&state);
    return NULL;
}

ENode* CObjC_New(TypeClass* tclass) {
    ObjCNamedArg* arg;
    ENode* objexpr;

    arg = lalloc(sizeof(ObjCNamedArg));
    memclrw(arg, sizeof(ObjCNamedArg));

    arg->name = GetHashNameNode("alloc");

    objexpr = create_objectrefnode(tclass->objcinfo->classobject);

    return CObjC_MakeSendMsgExpr(objexpr, tclass, arg, NULL, 1, FALSE);
}

ENode* CObjC_Delete(TypeClass* tclass, ENode* objexpr) {
    ObjCNamedArg* arg;

    arg = lalloc(sizeof(ObjCNamedArg));
    memclrw(arg, sizeof(ObjCNamedArg));

    arg->name = GetHashNameNode("dealloc");

    return CObjC_MakeSendMsgExpr(objexpr, tclass, arg, NULL, 0, FALSE);
}
