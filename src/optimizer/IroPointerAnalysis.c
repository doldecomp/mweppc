#include "compiler/CInt64.h"
#include "compiler/common.h"
#include "compiler/objects.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include "driver/oslib.h"
#define IRO_POINTER_ANALYSIS_TU
#include "compiler/IroEval.h"
#include "compiler/IroFlowgraph.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroMalloc.h"
#include "compiler/IroUtil.h"
#include "compiler/IroVars.h"
#include "compiler/BitVector.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CMachine.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/enode.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/scopes.h"
#include <setjmp.h>
#include <stdio.h>
#include <time.h>
// the analysis' data types and their inline helpers, after every header they use
#include "IroPointerAnalysisADTs.c"

#undef IRO_ASSERT
#undef IRO_ASSERT_T
#undef IRO_ASSERT_MSG
#define IRO_ASSERT(line, expr) \
    ((expr) ? (void)0 \
            : __assertion_failed(#expr, "IroPointerAnalysis.c", (line) + CError_LINE_SHIFT))
// an assertion whose text in the original differs from our spelling of the expression
#define IRO_ASSERT_MSG(line, expr, text) \
    ((expr) ? (void)0 \
            : __assertion_failed(text, "IroPointerAnalysis.c", (line) + CError_LINE_SHIFT))

// NOTE: invented names: the flags, FindAliasingParams' members, EvalCallActionParams' targetCount,
// GetPTFActionParams' contextPTF and match, EvalAssignAction2Params' value and failed (the members
// with no known name)
// The actions' parameter blocks: `result` gathers what the calls report changed, `assignChanged`
// what Assign reports; EvalCallActionParams' `stop` is only ever set FALSE, and GetPTFActionParams'
// `contextPTF` is never read
#pragma pack(push, 2)
typedef struct EvalExprAction3Params {
    LocationSetSet* set;
    Stack** stackPtr;
    Object* proc;
    ParamMappingFunction* map;
    PartialTransferFunction* ptf;
    PointsToFunction* pointsToFunc;
    Type* indRtype;
    Boolean result;
} EvalExprAction3Params;

typedef struct MatchPTFActionParams {
    Object* proc;
    PartialTransferFunction* ptfCopy;
    ParamMappingFunction* mapCopy;
    Stack** stackPtr;
} MatchPTFActionParams;

typedef struct ExpandLocationSetSetActionParams {
    LocationSetSet* toBeRemoved;
    LocationSetSet* toBeAdded;
    Stack** stackPtr;
    ParamMappingFunction* map;
    Boolean result;
} ExpandLocationSetSetActionParams;

typedef struct EPParams {
    ParamMappingFunction* map;
    ExtendedParam* ep;
    Object* proc;
    Object* var;
    Boolean foundVar;
    Boolean result;
} EPParams;

typedef struct FindAliasingParams {
    LocationSetSet* set;
    LocationSet* loc;
    Boolean aliased;
} FindAliasingParams;

typedef struct CreateEPActionParams {
    ParamMapping* last;
    ParamMapping* lowest;
} CreateEPActionParams;

typedef struct KillLocationParams {
    Object* proc;
    IRONode* fnode;
    IROLinear* nd;
    PartialTransferFunction* ptf;
    PointsToFunction* toBeKilled;
    LocationSet* dst;
    Boolean result;
} KillLocationParams;

typedef struct EvalCallActionParams {
    Object* proc;
    IRONode* fnode;
    IROLinear* nd;
    PartialTransferFunction* ptf;
    ParamMappingFunction* map;
    int targetCount;
    Boolean stop;
    Boolean result;
    Boolean visitTarget;
} EvalCallActionParams;

typedef struct EvalAssignActionParams {
    Object* proc;
    PartialTransferFunction* ptf;
    IROLinear* nd;
    IRONode* fnode;
    LocationSetSet* srcs;
    Boolean singleDst;
    Boolean result;
    Boolean assignChanged;
} EvalAssignActionParams;

typedef struct ApplySummaryActionParams {
    ParamMappingFunction* tgtMap;
    Object* proc;
    IRONode* fnode;
    IROLinear* nd;
    PartialTransferFunction* ptf;
    ParamMappingFunction* map;
    LocationSet* loc;
    LocationSetSet* locs;
    Boolean singleTarget;
    Boolean result;
    Boolean assignChanged;
} ApplySummaryActionParams;

typedef struct GetPTFActionParams {
    ParamMappingFunction* map;
    Object* proc;
    IROLinear* nd;
    PartialTransferFunction* ptf;
    Boolean* needVisit;
    PartialTransferFunction* contextPTF;
    PartialTransferFunction* match;
} GetPTFActionParams;
#pragma pack(pop)

#pragma pack(push, 2)
typedef struct LookupLinearExprActionParams {
    Object* proc;
    Type* indirectType;
    IROListNode** list;
} LookupLinearExprActionParams;

typedef struct LookupENodeExprActionParams {
    Object* proc;
    Type* indirectType;
    ENodeList** list;
} LookupENodeExprActionParams;

typedef struct GetFunctionDepsOrKillsActionParams {
    Object* proc;
    PartialTransferFunction* ptf;
    IROLinear* funccall;
    ParamMappingFunction* map;
    ObjectList** list;
} GetFunctionDepsOrKillsActionParams;
#pragma pack(pop)

typedef enum CreateNewParamObjects {
    CREATE_NEW_PARAM_OBJECTS_FALSE,
    CREATE_NEW_PARAM_OBJECTS_TRUE
} CreateNewParamObjects;
#pragma pack(push, 2)

typedef struct EvalMeetActionParams {
    Object* proc;
    IRONode* fnode;
    IRONode* pred;
    IROLinear* nd;
    PartialTransferFunction* ptf;
    Boolean result;
    Boolean assignChanged;
} EvalMeetActionParams;

typedef struct EvalAssignAction2Params {
    CInt64 value;
    IROLinear* nd;
    Boolean failed;
} EvalAssignAction2Params;

typedef struct EvalProcActionParams {
    Object* proc;
    IRONode* fnode;
    PartialTransferFunction* ptf;
} EvalProcActionParams;

typedef struct AssignEachInPointsToFunctionActionParams {
    Object* proc;
    IROLinear* nd;
    IRONode* fnode;
    PartialTransferFunction* ptf;
    Boolean result;
} AssignEachInPointsToFunctionActionParams;
#pragma pack(pop)

// NOTE: invented name: recordInputs
// `recordInputs`: a lookup that reaches a parameter adds it to the parameter mappings and the
// procedure's initial points-to function
static void UpdatePTFDomain(void* home);
static Object* GetLocalObject(PALocalVar* local, Object* proc, Boolean searchArgs);
static Boolean GetActualLocsOfExtendedParam(
    LocationSetSet* set,
    LocationSet* ls,
    Object* var,
    Stack** stackPtr,
    ParamMappingFunction* map,
    Boolean recordInputs
);
static Boolean ExpandLocationSetSetToActuals(
    Stack** stackPtr, ParamMappingFunction* map, LocationSetSet* thingsPointedTo
);
static Boolean Lookup(
    LocationSetSet* set,
    Stack** stackPtr,
    Object* proc,
    ParamMappingFunction* map,
    PartialTransferFunction* ptf,
    LocationSet* dst,
    PointsToFunction* pointsToFunc,
    Boolean recordInputs,
    Type* indRtype
);
static ExtendedParam* CreateExtendedParam(
    Stack** stackPtr, ParamMappingFunction* map, Object* var, Boolean* result
);
static Boolean Assign(
    PartialTransferFunction* ptf,
    LocationSet* dst,
    LocationSetSet* srcs,
    Object* proc,
    IROLinear* nd,
    IRONode* fnode
);
static ObjectList* FunctionArguments(Object* proc);
static IRONode** FunctionNodeTable(Object* proc);
static void EvalCallAction(Object* proc, void* refcon);
static Boolean ApplySummary(
    PartialTransferFunction* tgtPTF,
    ParamMappingFunction* tgtMap,
    Object* proc,
    IRONode* fnode,
    IROLinear* nd,
    PartialTransferFunction* ptf,
    ParamMappingFunction* map,
    Boolean singleTarget
);
static PartialTransferFunction* GetPTF(
    ParamMappingFunction* map,
    Object* proc,
    IROLinear* nd,
    PartialTransferFunction* ptf,
    Boolean* needVisit
);
static Boolean ObjectIsAFunctionArgument(Object* proc, Object* obj);
static Boolean ObjectIsARealFunctionArgument(Object* proc, Object* obj);
static void EvalProc(Object* proc, ParamMappingFunction* map, PartialTransferFunction* ptf);
static void FindGlobalObjectAction(Object* object, void* refcon);

static Boolean ObjectIsRestrictQualified(Object* obj) {
    return (CParser_GetTypeQualifiers(obj->type, obj->qual) & Q_RESTRICT) != 0;
}

static Boolean LocationIsVolatile(LocationSet* loc, Object* proc) {
    Boolean result;
    Type* rtype;

    IRO_ASSERT(932, loc != NULL);
    IRO_ASSERT(933, proc != NULL);

    result = FALSE;

    rtype = LocationSet_rtype(loc);
    if (rtype) {
        UInt32 qual;
        switch (rtype->type) {
            case TYPEARRAY:
                qual = TYPE_POINTER(rtype)->qual;
                break;
            case TYPEPOINTER:
                qual = TYPE_POINTER(rtype)->qual;
                break;
            case TYPEMEMBERPOINTER:
                qual = TYPE_MEMBER_POINTER(rtype)->qual;
                break;
            default:
                qual = 0;
        }
        if (qual & Q_VOLATILE) {
            result = TRUE;
        }
    }

    if (!result && !LocationSet_IsUnknown(loc)) {
        Object* obj = NULL;
        PAMemoryBlock* block = LocationSet_block(loc);

        switch (PAMemoryBlock_kind(block)) {
            case PAMEMORYBLOCKKIND_LOCALVAR: {
                PALocalVar* local = PAMemoryBlock_thing(block);
                if (local) {
                    obj = GetLocalObject(local, proc, FALSE);
                }
                break;
            }
            case PAMEMORYBLOCKKIND_EXTENDEDPARAM: {
                ExtendedParam* ep = PAMemoryBlock_thing(block);
                if (ep) {
                    ObjectSet* objSet;
                    if ((objSet = ExtendedParam_objectSet(ep))) {
                        if (ObjectSet_Count(objSet) == 1) {
                            obj = ObjectSet_FindFirst(objSet);
                        } else {
                            ObjectSet_ForEach(objSet, FindGlobalObjectAction, &obj);
                        }
                    }
                }
                break;
            }
            default:
                break;
        }

        if (obj && is_volatile_object(obj)) {
            result = TRUE;
        }
    }

    return result;
}

// NOTE: invented names: local, proc, searchArgs
static Object* GetLocalObject(PALocalVar* local, Object* proc, Boolean searchArgs) {
    ObjectList* list;
    Object* arg;
    Object* obj;
    char* name;

    IRO_ASSERT(999, local != NULL);
    IRO_ASSERT(1000, proc != NULL);

    obj = PALocalVar_obj(local);
    name = PALocalVar_name(local);

    if (proc != &stUnknown && !obj && name && name[0] && searchArgs) {
        for (list = FunctionArguments(proc); list && !obj; list = list->next) {
            arg = list->object;
            // (the original tests the address of the `name->name` array: always true)
            if (arg && arg != &stUnknown && arg->name && &arg->name->name[0]) {
                if (!strcmp(arg->name->name, name)) {
                    obj = arg;
                }
            }
        }
    }

    if (obj) {
        PALocalVar_SetObject(local, obj);
    }

    return obj;
}

static Boolean ObjectIsAnExtendedParamCandidate(Object* obj) {
    IRO_ASSERT(1042, obj != NULL);

    return Inline_IsObjectData(obj) ||
#if VERSION < VERSION_GC_2_5
        // (2.0 also tests DABSOLUTE, which Inline_IsObjectData accepts)
        obj->datatype == DABSOLUTE ||
#endif
        obj->datatype == DFUNC ||
        obj->datatype == DVFUNC ||
        obj->datatype == DINLINEFUNC;
}

#if VERSION < VERSION_GC_2_5
// 2.0's file has 1 line more above here
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT 1
#endif
static Boolean ObjectIsAFunction(Object* obj) {
    IRO_ASSERT(1053, obj != NULL);

    return obj->datatype == DFUNC || obj->datatype == DVFUNC || obj->datatype == DINLINEFUNC;
}

static Boolean LocationSetRepresentsSingleLocation(
    LocationSet* ls, Object* proc, PointsToFunction* pointsToFunc
) {
    Boolean result;
    PAMemoryBlock* mb;
    PAMemoryBlockKind kind;
    ExtendedParam* ep;
    ObjectSet* objSet;

    IRO_ASSERT(1072, ls != NULL);
    IRO_ASSERT(1073, proc == NULL || proc != NULL);
    IRO_ASSERT(1074, pointsToFunc == NULL || pointsToFunc != NULL);

    result = TRUE;
    if (LocationSet_IsUnknown(ls) || LocationSet_stride(ls)) {
        result = FALSE;
    } else {
        mb = LocationSet_block(ls);
        IRO_ASSERT(1083, mb != NULL);

        kind = PAMemoryBlock_kind(mb);
        if (kind == PAMEMORYBLOCKKIND_HEAPBLOCK) {
            result = FALSE;
        } else if (kind == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
            ep = PAMemoryBlock_thing(mb);
            if (!ep) {
                result = FALSE;
            } else {
                if ((objSet = ExtendedParam_objectSet(ep)) && ObjectSet_Count(objSet) > 1) {
                    Object* obj = NULL;
                    ObjectSet_ForEach(objSet, FindGlobalObjectAction, &obj);

                    if (!obj) {
                        LocationSetSet* lss;
                        LocationSet* first; // NOTE: invented name
                        ExtendedParam* ep;
                        ObjectSet* objSet;

                        lss = LocationSetSet_New();
                        LocationSetSet_Init(lss);
                        GetActualLocsOfExtendedParam(
                            lss, ls, NULL, &stCallingContextStack, NULL, FALSE
                        );
                        if (LocationSetSet_Count(lss) != 1 ||
                            !(first = LocationSetSet_FindFirst(lss)) ||
                            LocationSet_IsUnknown(first) ||
                            (!((mb = LocationSet_block(first)) &&
                               (PAMemoryBlock_kind(mb) == PAMEMORYBLOCKKIND_EXTENDEDPARAM) &&
                               (ep = PAMemoryBlock_thing(mb)) &&
                               (objSet = ExtendedParam_objectSet(ep)) &&
                               (ObjectSet_Count(objSet) == 1) &&
                               ObjectIsAnExtendedParamCandidate(ObjectSet_FindFirst(objSet))) &&
                             !LocationSetRepresentsSingleLocation(first, proc, pointsToFunc))) {
                            result = FALSE;
                        }

                        LocationSetSet_Term(lss);
                        LocationSetSet_Delete(lss);
                    }
                }
            }
        }
    }

    return result;
}

static void EvalExprAction(LocationSet* ls, void* refcon) {
    CInt64 value;
    UInt32 stride;
    PAMemoryBlock* mb;

    IRO_ASSERT(1150, ls != NULL);
    IRO_ASSERT(1151, !LocationSet_IsUnknown(ls));
    IRO_ASSERT(1152, refcon != NULL);

    value = *((CInt64*)refcon);
    stride = LocationSet_stride(ls);

    if ((mb = LocationSet_block(ls)) && PAMemoryBlock_kind(mb) == PAMEMORYBLOCKKIND_INT) {
        Type* rtype = LocationSet_rtype(ls);
        LocationSet_Term(ls);
        value = CInt64_Add(value, *((CInt64*)PAMemoryBlock_thing(mb)));
        mb = PAMemoryBlock_New();
        PAMemoryBlock_Init(mb, PAMEMORYBLOCKKIND_INT, &value);
        LocationSet_InitKnown(ls, mb, cint64_zero, stride, rtype);
    } else {
        value = CInt64_Add(value, LocationSet_field(ls));
        if (stride) {
            CInt64 strideval;
            CInt64_SetLong(&strideval, stride);
            value = CInt64_Mod(value, strideval);
        }
        LocationSet_SetField(ls, value);
    }
}

static void EvalExprAction2(LocationSet* ls, void* refcon) {
    UInt32 value;

    IRO_ASSERT(1187, ls != NULL);
    IRO_ASSERT(1188, !LocationSet_IsUnknown(ls));
    IRO_ASSERT(1189, refcon != NULL);

    value = CInt64_GetULong((CInt64*)refcon);
    if (value) {
        LocationSet_SetField(ls, CInt64_Mod(LocationSet_field(ls), *((CInt64*)refcon)));
    }
    LocationSet_SetStride(ls, value);
}

static void EvalExprAction3(LocationSet* ls, void* refcon) {
    EvalExprAction3Params* params;

    IRO_ASSERT(1218, ls != NULL);
    IRO_ASSERT(1219, refcon != NULL);

    params = refcon;

    params->result |= Lookup(
        params->set,
        params->stackPtr,
        params->proc,
        params->map,
        params->ptf,
        ls,
        params->pointsToFunc,
        TRUE,
        params->indRtype
    );
}

static void EvalExprAction4(LocationSet* ls, void* refcon) {
    Type* type;
    PAMemoryBlock* mb;

    IRO_ASSERT(1234, ls != NULL);
    IRO_ASSERT(1235, refcon != NULL);

    type = refcon;

    if (LocationSet_IsUnknown(ls)) {
        LocationSet_SetRtype(ls, type);
    } else if ((mb = LocationSet_block(ls))) {
        if (PAMemoryBlock_kind(mb) == PAMEMORYBLOCKKIND_INT) {
            CInt64 value;
            CInt64 field;
            UInt32 stride;

            value = *((CInt64*)PAMemoryBlock_thing(mb));
            IRO_TruncateValueToType(&value, type);
            field = LocationSet_field(ls);
            IRO_TruncateValueToType(&field, type);
            stride = LocationSet_stride(ls);
            LocationSet_Term(ls);

            mb = PAMemoryBlock_New();
            PAMemoryBlock_Init(mb, PAMEMORYBLOCKKIND_INT, &value);

            LocationSet_InitKnown(ls, mb, field, stride, type);
        }
        LocationSet_SetRtype(ls, type);
    }
}

// NOTE: invented names: foundPointerBase, foundIntegerBase, first
static Boolean EvalExpr(
    LocationSetSet* set,
    Object* proc,
    IROLinear* Int,
    Stack** stackPtr,
    ParamMappingFunction* map,
    PartialTransferFunction* ptf
) {
    LocationSetSet* lss;
    LocationSetSet* lss2;
    IROLinear* indirect;
    EvalExprAction3Params params;
    Boolean result;
    IROLinear* originalInt;

    IRO_ASSERT(1283, set == NULL || set != NULL);
    IRO_ASSERT(1284, proc != NULL);
    IRO_ASSERT(1285, Int != NULL);
    IRO_ASSERT(1286, map == NULL || map != NULL);
    IRO_ASSERT(1287, ptf == NULL || ptf != NULL);
    IRO_ASSERT(1288, stackPtr == NULL || *stackPtr == NULL || *stackPtr != NULL);

    result = FALSE;
    lss = LocationSetSet_New();
    LocationSetSet_Init(lss);

    originalInt = Int;
    while (Int && Int->type == IROLinearOp1Arg && Int->nodetype == ETYPCON) {
        Int = Int->u.monadic;
    }

    IRO_ASSERT(1301, Int != NULL);

    if (IRO_IsAssignment(Int)) {
        if (Int->type == IROLinearOp1Arg) {
            indirect = Int->u.monadic;
        } else {
            indirect = Int->u.diadic.left;
        }
        IRO_ASSERT_MSG(
            1309,
            indirect->type == IROLinearOp1Arg && indirect->nodetype == EINDIRECT,
            "indirect->lt == Lt_Monadic && indirect->oper == EINDIRECT"
        );

        lss2 = LocationSetSet_New();
        LocationSetSet_Init(lss2);
        result |= EvalExpr(lss2, proc, indirect->u.monadic, stackPtr, map, ptf);

        memset(&params, 0, sizeof(params));
        params.set = lss;
        params.stackPtr = stackPtr;
        params.proc = proc;
        params.map = map;
        params.ptf = ptf;
        if (Int->nodetype == EPOSTINC || Int->nodetype == EPOSTDEC) {
            params.pointsToFunc = indirect->pointsToFunction;
        } else {
            params.pointsToFunc = Int->pointsToFunction;
        }
        params.indRtype = indirect->rtype;
        params.result = FALSE;
        LocationSetSet_ForEach(lss2, EvalExprAction3, &params);
        result |= params.result;
        LocationSetSet_Term(lss2);
        LocationSetSet_Delete(lss2);
    } else if (Int->type == IROLinearFunccall) {
        IRO_ASSERT_MSG(
            1337, Int->u.funccall.returnedLocs != NULL, "Int->u.Funccall.returnedLocs != NULL"
        );
        LocationSetSet_AddSet(lss, Int->u.funccall.returnedLocs);
    } else if (Int->type == IROLinearOp1Arg && Int->nodetype == EINDIRECT) {
        lss2 = LocationSetSet_New();
        LocationSetSet_Init(lss2);
        result |= EvalExpr(lss2, proc, Int->u.monadic, stackPtr, map, ptf);

        memset(&params, 0, sizeof(params));
        params.set = lss;
        params.stackPtr = stackPtr;
        params.proc = proc;
        params.map = map;
        params.ptf = ptf;
        params.pointsToFunc = Int->pointsToFunction;
        params.indRtype = Int->rtype;
        params.result = FALSE;
        LocationSetSet_ForEach(lss2, EvalExprAction3, &params);
        result |= params.result;
        LocationSetSet_Term(lss2);
        LocationSetSet_Delete(lss2);
    } else if (Int->type == IROLinearOp2Arg && Int->nodetype == EADD) {
        IROAddrRecord* addr = IRO_InitAddrRecordPointer(Int);
        IRO_DecomposeAddressExpression(Int, addr);
        if (addr->numObjRefs > 1) {
            LocationSetSet_AddUnknown(lss, NULL, NULL, NULL);
        } else {
            CInt64 value;
            CInt64 max;
            CInt64 work;
            IROElmList* list;
            IROLinear* nd;
            // a pointer base taken, a non-pointer term taken as the base
            Boolean foundPointerBase;
            Boolean foundIntegerBase;

            CInt64_SetLong(&value, 0);
            foundPointerBase = foundIntegerBase = FALSE;
            if (addr->numObjRefs == 1) {
                Object* obj;
                IRO_ASSERT_MSG(
                    1382,
                    addr->objRefs->element->type == IROLinearOperand,
                    "addr->BaseList->Int->lt == Lt_Operand"
                );
                IRO_ASSERT_MSG(
                    1383,
                    addr->objRefs->element->u.node->type == EOBJREF,
                    "addr->BaseList->Int->u.Operand.expr->type == EOBJREF"
                );
                obj = ((IROLinear*)addr->objRefs->element)->u.node->data.objref;
                IRO_ASSERT(1386, obj != NULL);
                result |= EvalExpr(lss, proc, addr->objRefs->element, stackPtr, map, ptf);
                foundPointerBase = TRUE;
            }

            if (addr->numMisc != 0) {
                max = cint64_max;

                for (list = addr->misc; list; list = list->next) {
                    nd = list->element;
                    while (nd && nd->type == IROLinearOp1Arg && nd->nodetype == ETYPCON) {
                        nd = nd->u.monadic;
                    }

                    if (nd) {
                        if (nd->type == IROLinearOp2Arg && nd->nodetype == EMUL) {
                            if (IRO_IsIntConstant(nd->u.diadic.left) &&
                                IRO_IsIntConstant(nd->u.diadic.right)) {
                                if (is_unsigned(nd->rtype)) {
                                    work = CInt64_MulU(
                                        nd->u.diadic.left->u.node->data.intval,
                                        nd->u.diadic.right->u.node->data.intval
                                    );
                                } else {
                                    work = CInt64_Mul(
                                        nd->u.diadic.left->u.node->data.intval,
                                        nd->u.diadic.right->u.node->data.intval
                                    );
                                }
                                IRO_TruncateValueToType(&work, ((IROLinear*)list->element)->rtype);
                                value = CInt64_Add(value, work);
                            } else if (IRO_IsIntConstant(nd->u.diadic.left)) {
                                work = nd->u.diadic.left->u.node->data.intval;
                                if (!CInt64_IsZero(&work) && CInt64_LessU(work, max)) {
                                    max = work;
                                }
                            } else if (IRO_IsIntConstant(nd->u.diadic.right)) {
                                work = nd->u.diadic.right->u.node->data.intval;
                                if (!CInt64_IsZero(&work) && CInt64_LessU(work, max)) {
                                    max = work;
                                }
                            } else {
                                max = cint64_one;
                            }
                        } else if (nd->type == IROLinearOp2Arg && nd->nodetype == ESHL) {
                            if (IRO_IsIntConstant(nd->u.diadic.left) &&
                                IRO_IsIntConstant(nd->u.diadic.right)) {
                                work = CInt64_Shl(
                                    nd->u.diadic.left->u.node->data.intval,
                                    nd->u.diadic.right->u.node->data.intval
                                );
                                IRO_TruncateValueToType(&work, ((IROLinear*)list->element)->rtype);
                                value = CInt64_Add(value, work);
                            } else if (IRO_IsIntConstant(nd->u.diadic.right)) {
                                work =
                                    CInt64_Shl(cint64_one, nd->u.diadic.right->u.node->data.intval);
                                IRO_TruncateValueToType(&work, ((IROLinear*)list->element)->rtype);
                                if (!CInt64_IsZero(&work) && CInt64_LessU(work, max)) {
                                    max = work;
                                }
                            } else {
                                max = cint64_one;
                            }
                        } else {
                            LocationSet* first;
                            PAMemoryBlock* mb;

                            lss2 = LocationSetSet_New();
                            LocationSetSet_Init(lss2);
                            result |= EvalExpr(lss2, proc, nd, stackPtr, map, ptf);

                            if (LocationSetSet_FindUnknown(lss2)) {
                                max = cint64_one;
                            } else if (LocationSetSet_Count(lss2) == 1 &&
                                       (first = LocationSetSet_FindFirst(lss2)) &&
                                       (LocationSet_stride(first) == 0) &&
                                       (mb = LocationSet_block(first)) &&
                                       (PAMemoryBlock_kind(mb) == PAMEMORYBLOCKKIND_INT)) {
                                value = CInt64_Add(value, *((CInt64*)PAMemoryBlock_thing(mb)));
                            } else if (!foundPointerBase &&
                                       (IS_TYPE_POINTER(nd->rtype) ||
                                        IS_TYPE_MEMBERPOINTER(nd->rtype))) {
                                if (foundIntegerBase) {
                                    LocationSetSet_RemoveAll(lss);
                                    max = cint64_one;
                                }
                                LocationSetSet_AddSet(lss, lss2);
                                foundPointerBase = TRUE;
                            } else if (IS_TYPE_POINTER(nd->rtype) ||
                                       IS_TYPE_MEMBERPOINTER(nd->rtype)) {
                                LocationSetSet_AddUnknown(lss, NULL, NULL, NULL);
                            } else if (!foundIntegerBase) {
                                LocationSetSet_AddSet(lss, lss2);
                                foundIntegerBase = TRUE;
                            } else {
                                LocationSetSet_AddUnknown(lss, NULL, NULL, NULL);
                            }
                            LocationSetSet_Term(lss2);
                            LocationSetSet_Delete(lss2);
                        }
                    }
                }

                if (IS_TYPE_POINTER(Int->rtype)) {
                    CInt64_SetLong(&work, TPTR_TARGET(Int->rtype)->size);
                } else if (IS_TYPE_MEMBERPOINTER(Int->rtype)) {
                    CInt64_SetLong(&work, TYPE_MEMBER_POINTER(Int->rtype)->ty1->size);
                } else {
                    work = cint64_zero;
                }

                if (CInt64_GreaterU(work, max)) {
                    max = work;
                }
            }

            if (addr->numInts != 0) {
                for (list = addr->ints; list; list = list->next) {
                    nd = list->element;

                    if (nd) {
                        IRO_ASSERT_MSG(1535, IRO_IsIntConstant(nd), "IRO_IsIntConstant(addend)");

                        value = CInt64_Add(value, nd->u.node->data.intval);
                    }
                }
            }

            IRO_TruncateValueToType(&value, Int->rtype);

            if (LocationSetSet_Count(lss) == 0) {
                if (CInt64_Equal(max, cint64_max)) {
                    PAMemoryBlock* mb;
                    LocationSet* ls;

                    mb = PAMemoryBlock_New();
                    PAMemoryBlock_Init(mb, PAMEMORYBLOCKKIND_INT, &value);

                    ls = LocationSet_New();
                    LocationSet_InitKnown(ls, mb, cint64_zero, 0, Int->rtype);
                    LocationSetSet_Add(lss, ls);
                    LocationSet_Term(ls);
                    LocationSet_Delete(ls);
                } else {
                    LocationSetSet_AddUnknown(lss, NULL, NULL, NULL);
                }
            } else {
                if (!LocationSetSet_FindUnknown(lss) && !CInt64_IsZero(&value)) {
                    LocationSetSet_ForEach(lss, EvalExprAction, &value);
                }
            }

            if (!LocationSetSet_FindUnknown(lss) &&
                addr->numMisc &&
                !CInt64_Equal(max, cint64_max)) {
                LocationSetSet_ForEach(lss, EvalExprAction2, &max);
            }
        }
    } else if (Int->type == IROLinearOperand) {
        Object* obj;
        void* thing;
        PAMemoryBlockKind kind;

        thing = NULL;
        if (IRO_IsIntConstant(Int)) {
            kind = PAMEMORYBLOCKKIND_INT;
            thing = &Int->u.node->data.intval;
        } else if (Int->u.node->type == ESTRINGCONST) {
            kind = PAMEMORYBLOCKKIND_OTHER;
            thing = Int->u.node;
        } else if (Int->u.node->type == EOBJREF) {
            obj = Int->u.node->data.objref;
            IRO_ASSERT(1596, obj != NULL);
            if (ObjectIsAnExtendedParamCandidate(obj)) {
                kind = PAMEMORYBLOCKKIND_EXTENDEDPARAM;
                thing = CreateExtendedParam(stackPtr, map, obj, &result);
            } else {
                kind = PAMEMORYBLOCKKIND_LOCALVAR;
                thing = PALocalVar_New();
                // (the original tests the address of the `name->name` array: always true)
                if (obj->name && &obj->name->name[0] && ObjectIsARealFunctionArgument(proc, obj)) {
                    PALocalVar_InitByName(thing, obj->name->name);
                } else {
                    PALocalVar_InitByObject(thing, obj);
                }
            }
        }

        if (thing) {
            PAMemoryBlock* mb;
            LocationSet* ls;

            mb = PAMemoryBlock_New();
            PAMemoryBlock_Init(mb, kind, thing);

            ls = LocationSet_New();
            LocationSet_InitKnown(ls, mb, cint64_zero, 0, Int->rtype);
            LocationSetSet_Add(lss, ls);
            LocationSet_Term(ls);
            LocationSet_Delete(ls);
        }
    } else if (Int->type == IROLinearOp1Arg && Int->nodetype == EBITFIELD) {
        LocationSet* ls;

        lss2 = LocationSetSet_New();
        LocationSetSet_Init(lss2);
        result |= EvalExpr(lss2, proc, Int->u.monadic, stackPtr, map, ptf);

        if (LocationSetSet_Count(lss2) == 1 && (ls = LocationSetSet_FindFirst(lss2))) {
            LocationSetSet_AddUnknown(lss, NULL, NULL, ls);
        } else {
            CError_FATAL(1642);
        }

        LocationSetSet_Term(lss2);
        LocationSetSet_Delete(lss2);
    } else {
        LocationSetSet_AddUnknown(lss, NULL, NULL, NULL);
    }

    LocationSetSet_ForEach(lss, EvalExprAction4, originalInt->rtype);

    if (set && lss) {
        LocationSetSet_AddSet(set, lss);
    }

    LocationSetSet_Term(lss);
    LocationSetSet_Delete(lss);

    return result;
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
static IROAddrRecord* IRO_InitENodeAddrRecordPointer(ENode* enode) {
    IROAddrRecord* addr = IRO_malloc(sizeof(IROAddrRecord));

    addr->numObjRefs = 0;
    addr->objRefs = NULL;
    addr->numMisc = 0;
    addr->misc = NULL;
    addr->numInts = 0;
    addr->ints = NULL;
    addr->unused = 0;
    addr->linear = (IROLinear*)enode;

    return addr;
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
static void IRO_AddENodeElmToList(ENode* linear, IROElmList** list) {
    IROElmList* elmlist = IRO_malloc(sizeof(IROElmList));
    elmlist->element = (IROLinear*)linear;
    elmlist->next = NULL;
    if (!*list) {
        *list = elmlist;
    } else {
        elmlist->next = *list;
        *list = elmlist;
    }
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
static void IRO_DecomposeENodeAddressExpression(ENode* node, IROAddrRecord* addr) {
    if (node->data.diadic.left->type == EADD) {
        IRO_DecomposeENodeAddressExpression(node->data.diadic.left, addr);
    } else if (node->data.diadic.left->type == EINTCONST) {
        addr->numInts++;
        IRO_AddENodeElmToList(node->data.diadic.left, &addr->ints);
    } else if (node->data.diadic.left->type == EOBJREF) {
        addr->numObjRefs++;
        IRO_AddENodeElmToList(node->data.diadic.left, &addr->objRefs);
    } else {
        addr->numMisc++;
        IRO_AddENodeElmToList(node->data.diadic.left, &addr->misc);
    }

    if (node->data.diadic.right->type == EADD) {
        IRO_DecomposeENodeAddressExpression(node->data.diadic.right, addr);
    } else if (node->data.diadic.right->type == EINTCONST) {
        addr->numInts++;
        IRO_AddENodeElmToList(node->data.diadic.right, &addr->ints);
    } else if (node->data.diadic.right->type == EOBJREF) {
        addr->numObjRefs++;
        IRO_AddENodeElmToList(node->data.diadic.right, &addr->objRefs);
    } else {
        addr->numMisc++;
        IRO_AddENodeElmToList(node->data.diadic.right, &addr->misc);
    }
}

// NOTE: invented names: foundIntegerBase, foundPointerBase, first
// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
static Boolean EvalENodeExpr(
    LocationSetSet* set,
    Object* proc,
    ENode* enode,
    Stack** stackPtr,
    ParamMappingFunction* map,
    PartialTransferFunction* ptf
) {
    Boolean result;
    LocationSetSet* lss;
    LocationSetSet* lss2;
    ENode* indirect;
    EvalExprAction3Params params;
    ENode* originalInt;

    IRO_ASSERT(0, set == NULL || set != NULL);
    IRO_ASSERT(0, proc != NULL);
    IRO_ASSERT(0, enode != NULL);
    IRO_ASSERT(0, map == NULL || map != NULL);
    IRO_ASSERT(0, stackPtr == NULL || *stackPtr == NULL || *stackPtr != NULL);

    result = FALSE;
    lss = LocationSetSet_New();
    LocationSetSet_Init(lss);

    originalInt = enode;
    while (enode && enode->type == ETYPCON) {
        enode = enode->data.monadic;
    }

    IRO_ASSERT(0, enode != NULL);

    if (IRO_IsAssignOp[enode->type]) {
        if (enode->type == EPOSTINC ||
            enode->type == EPOSTDEC ||
            enode->type == EPREINC ||
            enode->type == EPREDEC) {
            indirect = enode->data.monadic;
        } else {
            indirect = enode->data.diadic.left;
        }
        IRO_ASSERT(0, indirect->type == EINDIRECT);

        lss2 = LocationSetSet_New();
        LocationSetSet_Init(lss2);
        result |= EvalENodeExpr(lss2, proc, indirect->data.monadic, stackPtr, map, ptf);

        memset(&params, 0, sizeof(params));
        params.set = lss;
        params.stackPtr = stackPtr;
        params.proc = proc;
        params.map = map;
        params.ptf = ptf;
        if (enode->type == EPOSTINC || enode->type == EPOSTDEC) {
            params.pointsToFunc = indirect->pointsTo;
        } else {
            params.pointsToFunc = enode->pointsTo;
        }
        params.indRtype = indirect->rtype;
        params.result = FALSE;
        LocationSetSet_ForEach(lss2, EvalExprAction3, &params);
        result |= params.result;
        LocationSetSet_Term(lss2);
        LocationSetSet_Delete(lss2);
    } else if (enode->type == EINDIRECT) {
        lss2 = LocationSetSet_New();
        LocationSetSet_Init(lss2);
        result |= EvalENodeExpr(lss2, proc, enode->data.monadic, stackPtr, map, ptf);

        memset(&params, 0, sizeof(params));
        params.set = lss;
        params.stackPtr = stackPtr;
        params.proc = proc;
        params.map = map;
        params.ptf = ptf;
        params.pointsToFunc = enode->pointsTo;
        params.indRtype = enode->rtype;
        params.result = FALSE;
        LocationSetSet_ForEach(lss2, EvalExprAction3, &params);
        result |= params.result;
        LocationSetSet_Term(lss2);
        LocationSetSet_Delete(lss2);
    } else if (enode->type == EADD) {
        IROAddrRecord* addr = IRO_InitENodeAddrRecordPointer(enode);
        IRO_DecomposeENodeAddressExpression(enode, addr);
        if (addr->numObjRefs > 1) {
            LocationSetSet_AddUnknown(lss, NULL, NULL, NULL);
        } else {
            CInt64 value;
            CInt64 max;
            CInt64 work;
            // a non-pointer term taken as the base, a pointer base taken
            Boolean foundIntegerBase;
            Boolean foundPointerBase;

            CInt64_SetLong(&value, 0);
            foundIntegerBase = FALSE;
            foundPointerBase = FALSE;
            if (addr->numObjRefs == 1) {
                Object* obj;
                IRO_ASSERT_MSG(
                    0,
                    ((ENode*)addr->objRefs->element)->type == EOBJREF,
                    "addr->BaseList->enode->type == EOBJREF"
                );
                obj = ((ENode*)addr->objRefs->element)->data.objref;
                IRO_ASSERT(0, obj != NULL);
                result |=
                    EvalENodeExpr(lss, proc, (ENode*)addr->objRefs->element, stackPtr, map, ptf);
                foundPointerBase = TRUE;
            }

            if (addr->numMisc != 0) {
                IROElmList* list;
                ENode* nd;
                max = cint64_max;

                for (list = addr->misc; list; list = list->next) {
                    nd = (ENode*)list->element;
                    while (nd && nd->type == ETYPCON) {
                        nd = nd->data.monadic;
                    }

                    if (nd) {
                        if (nd->type == EMUL) {
                            if (nd->data.diadic.left->type == EINTCONST &&
                                nd->data.diadic.right->type == EINTCONST) {
                                if (is_unsigned(nd->rtype)) {
                                    work = CInt64_MulU(
                                        nd->data.diadic.left->data.intval,
                                        nd->data.diadic.right->data.intval
                                    );
                                } else {
                                    work = CInt64_Mul(
                                        nd->data.diadic.left->data.intval,
                                        nd->data.diadic.right->data.intval
                                    );
                                }
                                IRO_TruncateValueToType(&work, ((ENode*)list->element)->rtype);
                                value = CInt64_Add(value, work);
                            } else if (nd->data.diadic.left->type == EINTCONST) {
                                work = nd->data.diadic.left->data.intval;
                                if (!CInt64_IsZero(&work) && CInt64_LessU(work, max)) {
                                    max = work;
                                }
                            } else if (nd->data.diadic.right->type == EINTCONST) {
                                work = nd->data.diadic.right->data.intval;
                                if (!CInt64_IsZero(&work) && CInt64_LessU(work, max)) {
                                    max = work;
                                }
                            } else {
                                max = cint64_one;
                            }
                        } else if (nd->type == ESHL) {
                            if (nd->data.diadic.left->type == EINTCONST &&
                                nd->data.diadic.right->type == EINTCONST) {
                                work = CInt64_Shl(
                                    nd->data.diadic.left->data.intval,
                                    nd->data.diadic.right->data.intval
                                );
                                IRO_TruncateValueToType(&work, ((ENode*)list->element)->rtype);
                                value = CInt64_Add(value, work);
                            } else if (nd->data.diadic.right->type == EINTCONST) {
                                work = CInt64_Shl(cint64_one, nd->data.diadic.right->data.intval);
                                IRO_TruncateValueToType(&work, ((ENode*)list->element)->rtype);
                                if (!CInt64_IsZero(&work) && CInt64_LessU(work, max)) {
                                    max = work;
                                }
                            } else {
                                max = cint64_one;
                            }
                        } else {
                            LocationSet* first;
                            PAMemoryBlock* mb;

                            lss2 = LocationSetSet_New();
                            LocationSetSet_Init(lss2);
                            result |= EvalENodeExpr(lss2, proc, nd, stackPtr, map, ptf);

                            if (LocationSetSet_FindUnknown(lss2)) {
                                max = cint64_one;
                            } else if (LocationSetSet_Count(lss2) == 1 &&
                                       (first = LocationSetSet_FindFirst(lss2)) &&
                                       (LocationSet_stride(first) == 0) &&
                                       (mb = LocationSet_block(first)) &&
                                       (PAMemoryBlock_kind(mb) == PAMEMORYBLOCKKIND_INT)) {
                                value = CInt64_Add(value, *((CInt64*)PAMemoryBlock_thing(mb)));
                            } else if (!foundPointerBase &&
                                       (IS_TYPE_POINTER(nd->rtype) ||
                                        IS_TYPE_MEMBERPOINTER(nd->rtype))) {
                                if (foundIntegerBase) {
                                    LocationSetSet_RemoveAll(lss);
                                    max = cint64_one;
                                }
                                LocationSetSet_AddSet(lss, lss2);
                                foundPointerBase = TRUE;
                            } else if (IS_TYPE_POINTER(nd->rtype) ||
                                       IS_TYPE_MEMBERPOINTER(nd->rtype)) {
                                LocationSetSet_AddUnknown(lss, NULL, NULL, NULL);
                            } else if (!foundIntegerBase) {
                                LocationSetSet_AddSet(lss, lss2);
                                foundIntegerBase = TRUE;
                            } else {
                                LocationSetSet_AddUnknown(lss, NULL, NULL, NULL);
                            }
                            LocationSetSet_Term(lss2);
                            LocationSetSet_Delete(lss2);
                        }
                    }
                }

                if (IS_TYPE_POINTER(enode->rtype)) {
                    CInt64_SetLong(&work, TYPE_POINTER(enode->rtype)->target->size);
                } else if (IS_TYPE_MEMBERPOINTER(enode->rtype)) {
                    CInt64_SetLong(&work, TYPE_MEMBER_POINTER(enode->rtype)->ty1->size);
                } else {
                    work = cint64_zero;
                }

                if (CInt64_GreaterU(work, max)) {
                    max = work;
                }
            }

            if (addr->numInts != 0) {
                IROElmList* list;
                ENode* addend;

                for (list = addr->ints; list; list = list->next) {
                    addend = (ENode*)list->element;

                    if (addend) {
                        IRO_ASSERT(0, addend->type == EINTCONST);

                        value = CInt64_Add(value, addend->data.intval);
                    }
                }
            }

            IRO_TruncateValueToType(&value, enode->rtype);

            if (LocationSetSet_Count(lss) == 0) {
                if (CInt64_Equal(max, cint64_max)) {
                    PAMemoryBlock* mb;
                    LocationSet* ls;

                    mb = PAMemoryBlock_New();
                    PAMemoryBlock_Init(mb, PAMEMORYBLOCKKIND_INT, &value);

                    ls = LocationSet_New();
                    LocationSet_InitKnown(ls, mb, cint64_zero, 0, enode->rtype);
                    LocationSetSet_Add(lss, ls);
                    LocationSet_Term(ls);
                    LocationSet_Delete(ls);
                } else {
                    LocationSetSet_AddUnknown(lss, NULL, NULL, NULL);
                }
            } else {
                if (!LocationSetSet_FindUnknown(lss) && !CInt64_IsZero(&value)) {
                    LocationSetSet_ForEach(lss, EvalExprAction, &value);
                }
            }

            if (!LocationSetSet_FindUnknown(lss) &&
                addr->numMisc &&
                !CInt64_Equal(max, cint64_max)) {
                LocationSetSet_ForEach(lss, EvalExprAction2, &max);
            }
        }
    } else if (enode->type == EOBJREF || enode->type == EINTCONST || enode->type == ESTRINGCONST) {
        Object* obj;
        void* thing;
        PAMemoryBlockKind kind;

        thing = NULL;
        if (enode->type == EINTCONST) {
            kind = PAMEMORYBLOCKKIND_INT;
            thing = &enode->data.intval;
        } else if (enode->type == ESTRINGCONST) {
            kind = PAMEMORYBLOCKKIND_OTHER;
            thing = enode;
        } else if (enode->type == EOBJREF) {
            obj = enode->data.objref;
            IRO_ASSERT(0, obj != NULL);
            if (ObjectIsAnExtendedParamCandidate(obj)) {
                kind = PAMEMORYBLOCKKIND_EXTENDEDPARAM;
                thing = CreateExtendedParam(stackPtr, map, obj, &result);
            } else {
                kind = PAMEMORYBLOCKKIND_LOCALVAR;
                thing = PALocalVar_New();
                // (the original tests the address of the `name->name` array: always true)
                if (obj->name && &obj->name->name[0] && ObjectIsARealFunctionArgument(proc, obj)) {
                    PALocalVar_InitByName(thing, obj->name->name);
                } else {
                    PALocalVar_InitByObject(thing, obj);
                }
            }
        }

        if (thing) {
            PAMemoryBlock* mb;
            LocationSet* ls;

            mb = PAMemoryBlock_New();
            PAMemoryBlock_Init(mb, kind, thing);

            ls = LocationSet_New();
            LocationSet_InitKnown(ls, mb, cint64_zero, 0, enode->rtype);
            LocationSetSet_Add(lss, ls);
            LocationSet_Term(ls);
            LocationSet_Delete(ls);
        }
    } else if (enode->type == EBITFIELD) {
        LocationSet* ls;

        lss2 = LocationSetSet_New();
        LocationSetSet_Init(lss2);
        result |= EvalENodeExpr(lss2, proc, enode->data.monadic, stackPtr, map, ptf);

        if (LocationSetSet_Count(lss2) == 1 && (ls = LocationSetSet_FindFirst(lss2))) {
            LocationSetSet_AddUnknown(lss, NULL, NULL, ls);
        } else {
            CError_FATAL(2146);
        }

        LocationSetSet_Term(lss2);
        LocationSetSet_Delete(lss2);
    } else {
        LocationSetSet_AddUnknown(lss, NULL, NULL, NULL);
    }

    LocationSetSet_ForEach(lss, EvalExprAction4, originalInt->rtype);

    if (set && lss) {
        LocationSetSet_AddSet(set, lss);
    }

    LocationSetSet_Term(lss);
    LocationSetSet_Delete(lss);

    return result;
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
static Boolean EvalVarAddr(
    LocationSetSet* set, Object* proc, VarRecord* var, Stack** stackPtr, ParamMappingFunction* map
) {
    Boolean result;
    PAMemoryBlockKind kind;
    void* thing;
    Object* obj;
    PAMemoryBlock* block;
    LocationSet* loc;

    IRO_ASSERT_MSG(0, var->object != NULL, "var->obj != NULL");

    result = FALSE;
    obj = var->object;

    if (ObjectIsAnExtendedParamCandidate(obj)) {
        kind = PAMEMORYBLOCKKIND_EXTENDEDPARAM;
        thing = CreateExtendedParam(stackPtr, map, obj, &result);
    } else {
        kind = PAMEMORYBLOCKKIND_LOCALVAR;
        thing = PALocalVar_New();
        // (the original tests the address of the `name->name` array: always true)
        if (obj->name && &obj->name->name[0] && ObjectIsAFunctionArgument(proc, obj)) {
            PALocalVar_InitByName(thing, obj->name->name);
        } else {
            PALocalVar_InitByObject(thing, obj);
        }
    }

    block = PAMemoryBlock_New();
    PAMemoryBlock_Init(block, kind, thing);

    loc = LocationSet_New();
    LocationSet_InitKnown(loc, block, cint64_zero, 0, CDecl_NewPointerType(obj->type));
    LocationSetSet_Add(set, loc);
    LocationSet_Term(loc);
    LocationSet_Delete(loc);

    return result;
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
static Boolean EvalVariable(
    LocationSetSet* set,
    Object* proc,
    VarRecord* var,
    PointsToFunction* pointsToFunc,
    Stack** stackPtr,
    ParamMappingFunction* map,
    PartialTransferFunction* ptf
) {
    LocationSetSet* locs;
    Boolean result;
    EvalExprAction3Params params;

    locs = LocationSetSet_New();
    LocationSetSet_Init(locs);
    result = EvalVarAddr(locs, proc, var, stackPtr, map);

    memset(&params, 0, sizeof(params));
    params.set = set;
    params.stackPtr = stackPtr;
    params.proc = proc;
    params.map = map;
    params.ptf = ptf;
    params.pointsToFunc = pointsToFunc;
    params.indRtype = var->object->type;
    params.result = FALSE;
    LocationSetSet_ForEach(locs, EvalExprAction3, &params);

    result |= params.result;
    LocationSetSet_Term(locs);
    LocationSetSet_Delete(locs);

    return result;
}

static void StoreReturnedLocationsAction(LocationSet* loc, void* refcon) {
    IROLinear* nd;

    IRO_ASSERT(2274, loc != NULL);
    IRO_ASSERT(2275, refcon != NULL);

    nd = refcon;

    if (!LocationSet_IsUnknown(loc)) {
        if (PAMemoryBlock_kind(LocationSet_block(loc)) == PAMEMORYBLOCKKIND_HEAPBLOCK) {
            PAHeapBlock* hb;
            PAMemoryBlock* mb;
            CInt64 field;
            UInt32 stride;
            Type* rtype;

            hb = PAHeapBlock_New();
            PAHeapBlock_Init(hb, nd);

            mb = PAMemoryBlock_New();
            PAMemoryBlock_Init(mb, PAMEMORYBLOCKKIND_HEAPBLOCK, hb);

            field = LocationSet_field(loc);
            stride = LocationSet_stride(loc);
            rtype = LocationSet_rtype(loc);
            LocationSet_Term(loc);
            LocationSet_InitKnown(loc, mb, field, stride, rtype);
        }
    }
}

static void StoreReturnedLocations(
    IROLinear* nd, PartialTransferFunction* ptf, ParamMappingFunction* map
) {
    LocationSet* retLoc;
    LocationSetSet* retLocs;

    IRO_ASSERT(2306, nd != NULL);
    IRO_ASSERT_MSG(2307, nd->type == IROLinearFunccall, "nd->lt == Lt_Funccall");
    IRO_ASSERT(2308, ptf != NULL);
    IRO_ASSERT(2309, map != NULL);

    retLoc = PartialTransferFunction_returnLocation(ptf);
    if (!(retLocs = nd->u.funccall.returnedLocs)) {
        retLocs = nd->u.funccall.returnedLocs = LocationSetSet_New();
        LocationSetSet_Init(retLocs);
    } else {
        LocationSetSet_RemoveAll(retLocs);
    }

    Lookup(
        retLocs,
        NULL,
        NULL,
        NULL,
        NULL,
        retLoc,
        PartialTransferFunction_finalPointsToFn(ptf),
        FALSE,
        NULL
    );
    ExpandLocationSetSetToActuals(&stCallingContextStack, map, retLocs);
    LocationSetSet_ForEach(retLocs, StoreReturnedLocationsAction, nd);
}

static void FillInAppropriateMappingsWithExtParamAction(Object* obj, void* refcon) {
    EPParams* params;
    ParamMapping* mapping;

    IRO_ASSERT(2351, obj != NULL);
    IRO_ASSERT(2352, refcon != NULL);

    params = refcon;

    IRO_ASSERT(2356, params->map != NULL);
    IRO_ASSERT(2357, params->ep != NULL);
    IRO_ASSERT(2358, params->proc == NULL || params->proc != NULL);
    IRO_ASSERT(2359, params->proc != &stUnknown);
    IRO_ASSERT(2360, params->var == NULL || params->var != NULL);
    IRO_ASSERT(2361, params->var != &stUnknown);

    mapping = ParamMappingFunction_FindMappingByFormal(params->map, obj);
    if (!mapping) {
        if (ObjectIsAnExtendedParamCandidate(obj) ||
            !params->proc ||
            ObjectIsAFunctionArgument(params->proc, obj)) {
            mapping = ParamMapping_New();
            ParamMapping_Init(mapping, NULL, obj, params->ep);
            ParamMappingFunction_Add(params->map, mapping);
            ParamMapping_Term(mapping);
            ParamMapping_Delete(mapping);
            params->result = TRUE;
        }
    } else {
        if (ParamMapping_extended(mapping) != params->ep) {
            ParamMapping_SetExtended(mapping, params->ep);
            params->result = TRUE;
        }
    }

    if (obj == params->var) {
        params->foundVar = TRUE;
    }
}

static Boolean FillInAppropriateMappingsWithExtParam(
    ParamMappingFunction* map, Object* var, ExtendedParam* ep, Object* proc
) {
    EPParams params;
    ObjectSet* objSet;

    IRO_ASSERT(2397, map != NULL);
    IRO_ASSERT(2398, var == NULL || var != NULL);
    IRO_ASSERT(2399, var != &stUnknown);
    IRO_ASSERT(2400, ep != NULL);
    IRO_ASSERT(2401, proc == NULL || proc != NULL);
    IRO_ASSERT(2402, proc != &stUnknown);

    memset(&params, 0, sizeof(params));
    params.map = map;
    params.ep = ep;
    params.proc = proc;
    params.var = var;
    params.foundVar = FALSE;
    params.result = FALSE;

    if ((objSet = ExtendedParam_objectSet(ep))) {
        ObjectSet_ForEach(objSet, FillInAppropriateMappingsWithExtParamAction, &params);
    }

    if (var && !params.foundVar) {
        ExtendedParam_AddObject(ep, var);
        FillInAppropriateMappingsWithExtParamAction(var, &params);
    }

    return params.result;
}

static void MatchPTFHelper(
    LocationSet* loc,
    LocationSetSet* locs,
    Object* proc,
    ParamMappingFunction* map,
    PartialTransferFunction* ptf
) {
    PointsToFunction* initial;
    PAMemoryBlock* block;
    PAMemoryBlockKind kind;
    Object* obj;

    IRO_ASSERT(2447, loc != NULL);
    IRO_ASSERT(2448, !LocationSet_IsUnknown(loc));
    IRO_ASSERT(2449, locs != NULL);
    IRO_ASSERT(2450, proc != NULL);
    IRO_ASSERT(2451, map != NULL);
    IRO_ASSERT(2452, ptf != NULL);

    initial = PartialTransferFunction_initialPointsToFn(ptf);
    IRO_ASSERT(2455, initial != NULL);

    IRO_ASSERT(2457, !LocationSet_IsUnknown(loc));

    block = LocationSet_block(loc);
    IRO_ASSERT(2459, block != NULL);

    kind = PAMemoryBlock_kind(block);

    if (kind == PAMEMORYBLOCKKIND_LOCALVAR) {
        PALocalVar* local;

        local = PAMemoryBlock_thing(block);
        IRO_ASSERT(2465, local != NULL);

        obj = GetLocalObject(local, proc, TRUE);
        if (obj && ObjectIsAFunctionArgument(proc, obj)) {
            if (LocationSetSet_Count(locs) == 1) {
                LocationSet* ls;
                if ((ls = LocationSetSet_FindFirst(locs)) && !LocationSet_IsUnknown(ls)) {
                    if ((block = LocationSet_block(ls))) {
                        if (PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
                            ExtendedParam* ep;
                            if ((ep = PAMemoryBlock_thing(block))) {
                                ExtendedParam_AddObject(ep, obj);
                                if (stExtParamSet) {
                                    ExtendedParamSet_Add(stExtParamSet, ep);
                                }
                                FillInAppropriateMappingsWithExtParam(map, obj, ep, proc);
                            }
                        }
                    }
                }
            }
        }
    } else if (kind == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
        ExtendedParam* ep = PAMemoryBlock_thing(block);
        IRO_ASSERT(2488, ep != NULL);

        obj = NULL;
        ObjectSet_ForEach(ExtendedParam_objectSet(ep), FindGlobalObjectAction, &obj);
        if (obj && obj != &stUnknown) {
            FillInAppropriateMappingsWithExtParam(map, obj, ep, proc);
        }
    } else {
        CError_FATAL(2499);
    }

    if (!PointsToFunction_FindByLookupCompatibleLocationSet(initial, loc)) {
        PointsToEntry* pte = PointsToEntry_New();
        PointsToEntry_Init(pte, loc, locs);
        PointsToFunction_AddWithoutChecking(initial, pte);
        PointsToEntry_Term(pte);
        PointsToEntry_Delete(pte);
    }
}

static void MatchPTFAction1(PointsToEntry* tgtPTE, void* refcon) {
    MatchPTFActionParams* params;
    LocationSet* loc;
    LocationSetSet* locs;
    PAMemoryBlock* block;
    PAMemoryBlockKind kind;

    IRO_ASSERT(2524, tgtPTE != NULL);
    IRO_ASSERT(2525, refcon != NULL);

    params = refcon;

    IRO_ASSERT(2529, params->proc != NULL);
    IRO_ASSERT(2530, params->ptfCopy != NULL);
    IRO_ASSERT(2531, params->mapCopy != NULL);

    if ((loc = PointsToEntry_loc(tgtPTE)) && (locs = PointsToEntry_locs(tgtPTE))) {
        if ((block = LocationSet_block(loc))) {
            kind = PAMemoryBlock_kind(block);
            if (kind == PAMEMORYBLOCKKIND_LOCALVAR) {
                MatchPTFHelper(loc, locs, params->proc, params->mapCopy, params->ptfCopy);
            } else if (kind != PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
                CError_FATAL(2546);
            }
        }
    }
}

static void MatchPTFAction2(PointsToEntry* tgtPTE, void* refcon) {
    MatchPTFActionParams* params;
    LocationSet* loc;
    LocationSetSet* locs;
    PAMemoryBlock* block;
    PAMemoryBlockKind kind;

    IRO_ASSERT(2560, tgtPTE != NULL);
    IRO_ASSERT(2561, refcon != NULL);

    params = refcon;

    IRO_ASSERT(2565, params->proc != NULL);
    IRO_ASSERT(2566, params->ptfCopy != NULL);
    IRO_ASSERT(2567, params->mapCopy != NULL);

    if ((loc = PointsToEntry_loc(tgtPTE)) && (locs = PointsToEntry_locs(tgtPTE))) {
        if ((block = LocationSet_block(loc))) {
            kind = PAMemoryBlock_kind(block);
            if (kind == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
                MatchPTFHelper(loc, locs, params->proc, params->mapCopy, params->ptfCopy);
            }
        }
    }
}

static Boolean MatchPTF(
    PartialTransferFunction* tgtPTF,
    Object* proc,
    ParamMappingFunction* map,
    IROLinear* nd,
    PartialTransferFunction* ptf
) {
    Boolean result;
    PartialTransferFunction* ptfCopy;
    ParamMappingFunction* mapCopy;
    PointsToFunction* initial;
    MatchPTFActionParams params;

    IRO_ASSERT(2592, tgtPTF != NULL);
    IRO_ASSERT(2593, proc != NULL);
    IRO_ASSERT(2594, map != NULL);
    IRO_ASSERT(2595, nd != NULL);
    IRO_ASSERT(2596, ptf != NULL);

    ptfCopy = PartialTransferFunction_New();
    PartialTransferFunction_Copy(ptfCopy, ptf);

    mapCopy = ParamMappingFunction_New();
    ParamMappingFunction_Copy(mapCopy, map);

    initial = PartialTransferFunction_initialPointsToFn(tgtPTF);
    PointsToFunction_SortByExtendedParamNum(initial);

    memset(&params, 0, sizeof(params));
    params.proc = proc;
    params.ptfCopy = ptfCopy;
    params.mapCopy = mapCopy;
    params.stackPtr = &stCallingContextStack;

    PointsToFunction_ForEach(initial, MatchPTFAction1, &params);
    PointsToFunction_ForEach(initial, MatchPTFAction2, &params);

    result = PointsToFunctions_Match(initial, PartialTransferFunction_initialPointsToFn(ptfCopy));
    if (result) {
        PartialTransferFunction_Term(ptf);
        PartialTransferFunction_Copy(ptf, ptfCopy);

        ParamMappingFunction_Term(map);
        ParamMappingFunction_Copy(map, mapCopy);
    }

    PartialTransferFunction_Term(ptfCopy);
    PartialTransferFunction_Delete(ptfCopy);

    ParamMappingFunction_Term(mapCopy);
    ParamMappingFunction_Delete(mapCopy);

    return result;
}

static void FindCallTargetsAction2(Object* obj, void* refcon) {
    ObjectSet* procList;

    IRO_ASSERT(2649, obj != NULL);
    IRO_ASSERT(2650, refcon != NULL);

    procList = refcon;

    if (obj == &stUnknown || ObjectIsAFunction(obj)) {
        ObjectSet_Add(procList, obj);
    }
}

static void FindCallTargetsAction(LocationSet* ls, void* refcon) {
    ObjectSet* procList;

    IRO_ASSERT(2668, ls != NULL);
    IRO_ASSERT(2669, refcon != NULL);

    procList = refcon;

    if (!LocationSet_IsUnknown(ls)) {
        PAMemoryBlock* mb = LocationSet_block(ls);
        if (PAMemoryBlock_kind(mb) == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
            ExtendedParam* ep = PAMemoryBlock_thing(mb);
            ObjectSet* objSet = ExtendedParam_objectSet(ep);
            ObjectSet_ForEach(objSet, FindCallTargetsAction2, procList);
        }
    } else {
        FindCallTargetsAction2(&stUnknown, procList);
    }
}

static Boolean FindCallTargets(
    ObjectSet* procList,
    Object* proc,
    IROLinear* nd,
    ParamMappingFunction* map,
    PartialTransferFunction* ptf
) {
    LocationSetSet* set;
    Boolean result;

    IRO_ASSERT(2695, procList != NULL);
    IRO_ASSERT(2696, proc != NULL);
    IRO_ASSERT(2697, nd != NULL);
    IRO_ASSERT_MSG(2698, nd->type == IROLinearFunccall, "nd->lt == Lt_Funccall");
    IRO_ASSERT(2699, map != NULL);
    IRO_ASSERT(2700, ptf != NULL);

    set = LocationSetSet_New();
    LocationSetSet_Init(set);
    result = EvalExpr(set, proc, nd->u.funccall.callee, &stCallingContextStack, map, ptf);
    result |= ExpandLocationSetSetToActuals(&stCallingContextStack, map, set);
    LocationSetSet_ForEach(set, FindCallTargetsAction, procList);
    LocationSetSet_Term(set);
    LocationSetSet_Delete(set);

    return result;
}

static Boolean LookupParam(
    LocationSetSet* set,
    LocationSet* ls,
    Object* var,
    Stack** stackPtr,
    Object* proc,
    ParamMappingFunction* map,
    PartialTransferFunction* ptf,
    Boolean recordInputs
) {
    Boolean result;
    ExtendedParam* ep;

    IRO_ASSERT(2727, set == NULL || set != NULL);
    IRO_ASSERT(2728, ls != NULL);
    IRO_ASSERT(2729, var != NULL);
    IRO_ASSERT(2730, stackPtr == NULL || *stackPtr == NULL || *stackPtr != NULL);
    IRO_ASSERT(2731, proc != NULL);
    IRO_ASSERT(2732, map == NULL || map != NULL);
    IRO_ASSERT(2733, ptf == NULL || ptf != NULL);

    result = FALSE;

    ep = ExtendedParam_FindByObject(var);
    if (!ep) {
        ep = CreateExtendedParam(stackPtr, map, var, &result);
    }

    IRO_ASSERT(2740, ep != NULL);

    if (ep) {
        PAMemoryBlock* block;
        LocationSet* newLS;

        block = PAMemoryBlock_New();
        PAMemoryBlock_Init(block, PAMEMORYBLOCKKIND_EXTENDEDPARAM, ep);

        newLS = LocationSet_New();
        LocationSet_InitKnown(
            newLS, block, LocationSet_field(ls), LocationSet_stride(ls), LocationSet_rtype(ls)
        );
        if (set) {
            LocationSetSet_Add(set, newLS);
        }

        if (recordInputs) {
            if (map) {
                result |= FillInAppropriateMappingsWithExtParam(map, var, ep, proc);
            }

            if (ptf) {
                PointsToFunction* initial;

                initial = PartialTransferFunction_initialPointsToFn(ptf);
                if (initial && !PointsToFunction_FindByLookupCompatibleLocationSet(initial, ls)) {
                    LocationSet_Term(newLS);
                    LocationSet_InitKnown(
                        newLS, LocationSet_block(ls), cint64_zero, 0, LocationSet_rtype(ls)
                    );
                    if (!PointsToFunction_FindByLookupCompatibleLocationSet(initial, newLS)) {
                        LocationSet* newLS2;
                        LocationSetSet* newSet;
                        PointsToEntry* newPTE;

                        newLS2 = LocationSet_New();
                        LocationSet_InitKnown(newLS2, block, cint64_zero, 0, LocationSet_rtype(ls));

                        newSet = LocationSetSet_New();
                        LocationSetSet_Init(newSet);
                        LocationSetSet_Add(newSet, newLS2);

                        newPTE = PointsToEntry_New();
                        PointsToEntry_Init(newPTE, newLS, newSet);
                        result |= PointsToFunction_Add(initial, newPTE);
                        PointsToEntry_Term(newPTE);
                        PointsToEntry_Delete(newPTE);

                        LocationSetSet_Term(newSet);
                        LocationSetSet_Delete(newSet);

                        LocationSet_Term(newLS2);
                        LocationSet_Delete(newLS2);
                    }
                }
            }
        }

        LocationSet_Term(newLS);
        LocationSet_Delete(newLS);
    }

    return result;
}

// NOTE: invented names: set, ls, var, stackPtr, map, recordInputs, loc
static Boolean GetActualLocsOfExtendedParam(
    LocationSetSet* set,
    LocationSet* ls,
    Object* var,
    Stack** stackPtr,
    ParamMappingFunction* map,
    Boolean recordInputs
) {
    Stack* next;
    StackElement* element;
    PAMemoryBlock* block;
    IROLinear* nd;
    ExtendedParam* ep;
    ParamMapping* mapping;
    Type* savedRtype;
    CInt64 savedField;
    UInt32 savedStride;
    LocationSetSet* newSet;
    Boolean result;

    IRO_ASSERT(2820, set == NULL || set != NULL);
    IRO_ASSERT(2821, (ls != NULL && var == NULL) || (ls == NULL && var != NULL));
    IRO_ASSERT(2822, ls == NULL || !LocationSet_IsUnknown(ls));
    IRO_ASSERT(2823, var != &stUnknown);
    IRO_ASSERT(2824, stackPtr == NULL || *stackPtr == NULL || *stackPtr != NULL);
    IRO_ASSERT(2825, map == NULL || map != NULL);

    result = FALSE;
    block = NULL;
    nd = NULL;
    ep = NULL;
    mapping = NULL;

    if (ls) {
        block = LocationSet_block(ls);
        IRO_ASSERT(2837, block != NULL);
        IRO_ASSERT(2838, PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_EXTENDEDPARAM);

        ep = PAMemoryBlock_thing(block);
        IRO_ASSERT(2841, ep != NULL);

        savedField = LocationSet_field(ls);
        savedStride = LocationSet_stride(ls);
        savedRtype = LocationSet_rtype(ls);
    }

    IRO_ASSERT(2847, ep == NULL || ep != NULL);

    if (stackPtr && *stackPtr) {
        if ((element = Stack_Top(stackPtr)) && !map) {
            map = StackElement_map(element);
        }
    }

    IRO_ASSERT(2858, map == NULL || map != NULL);

    if (ep) {
        IRO_ASSERT(2862, var == NULL);
        ObjectSet_ForEach(ExtendedParam_objectSet(ep), FindGlobalObjectAction, &var);
        if (!var) {
            var = ObjectSet_FindFirst(ExtendedParam_objectSet(ep));
        }
        if (!var) {
            var = &stUnknown;
        }
    }

    IRO_ASSERT(2869, var != NULL);

    if (map && var != &stUnknown) {
        if (recordInputs) {
            result |= FillInAppropriateMappingsWithExtParam(map, var, ep, NULL);
        }
        mapping = ParamMappingFunction_FindMappingByFormal(map, var);
    }

    newSet = LocationSetSet_New();
    LocationSetSet_Init(newSet);

    IRO_ASSERT(2883, mapping == NULL || mapping != NULL);

    if (mapping) {
        nd = ParamMapping_actual(mapping);
    }

    if (!nd) {
        if (!ls) {
            IRO_ASSERT(2892, var != NULL);
            IRO_ASSERT(2893, ep == NULL);

            if (var != &stUnknown) {
                ep = CreateExtendedParam(stackPtr, NULL, var, &result);
                if (recordInputs && mapping && ParamMapping_extended(mapping) != ep) {
                    ParamMapping_SetExtended(mapping, ep);
                    result = TRUE;
                }

                block = PAMemoryBlock_New();
                PAMemoryBlock_Init(block, PAMEMORYBLOCKKIND_EXTENDEDPARAM, ep);
                savedField = cint64_zero;
                savedStride = 0;
                savedRtype = CDecl_NewPointerType(var->type);
            } else {
                block = LocationSet_block(stUnknownLs);
                savedRtype = NULL;
            }
        } else if (var == &stUnknown || !ObjectIsAnExtendedParamCandidate(var)) {
            block = LocationSet_block(stUnknownLs);
            savedRtype = NULL;
        }

        IRO_ASSERT(2924, block != NULL);

        if (block == LocationSet_block(stUnknownLs)) {
            LocationSetSet_AddUnknown(newSet, savedRtype, NULL, NULL);
        } else {
            LocationSet* loc = LocationSet_New();
            LocationSet_InitKnown(loc, block, savedField, savedStride, savedRtype);
            LocationSetSet_Add(newSet, loc);
            LocationSet_Term(loc);
            LocationSet_Delete(loc);
        }
    } else {
        if (stackPtr &&
            *stackPtr &&
            (next = Stack_Next(stackPtr)) &&
            (element = Stack_Top(&next))) {
            if (ls) {
                IROLinear* right;

                if (!savedStride && !CInt64_IsZero(&savedField)) {
                    IROLinear* d;
                    right = IRO_NewIntConst(savedField, (Type*)&stunsignedlong);
                    d = IRO_NewLinear(IROLinearOp2Arg);
                    d->index = ++IRO_NumLinear;
                    d->rtype = nd->rtype;
                    d->u.diadic.left = nd;
                    d->u.diadic.right = right;
                    nd = d;
                }
                if (savedStride) {
                    IROLinear* d;
                    right = IRO_NewLinear(IROLinearFunccall);
                    right->u.funccall.returnedLocs = LocationSetSet_New();
                    LocationSetSet_Init(right->u.funccall.returnedLocs);
                    LocationSetSet_AddUnknown(right->u.funccall.returnedLocs, NULL, NULL, NULL);
                    d = IRO_NewLinear(IROLinearOp2Arg);
                    d->index = ++IRO_NumLinear;
                    d->rtype = nd->rtype;
                    d->u.diadic.left = nd;
                    d->u.diadic.right = right;
                    nd = d;
                }
            }
            EvalExpr(
                newSet,
                StackElement_proc(element),
                nd,
                &next,
                StackElement_map(element),
                StackElement_ptf(element)
            );
        } else {
            LocationSetSet_AddUnknown(newSet, NULL, NULL, NULL);
        }
    }

    if (set) {
        LocationSetSet_AddSet(set, newSet);
    }

    LocationSetSet_Term(newSet);
    LocationSetSet_Delete(newSet);

    return result;
}

static void ExpandLocationSetSetToActualsAction(LocationSet* ls, void* refcon) {
    ExpandLocationSetSetActionParams* params;

    IRO_ASSERT(3020, ls != NULL);
    IRO_ASSERT(3021, refcon != NULL);

    params = refcon;

    IRO_ASSERT(3025, params->toBeRemoved != NULL);
    IRO_ASSERT(3026, params->toBeAdded != NULL);
    IRO_ASSERT(3027, params->stackPtr == NULL || *params->stackPtr == NULL || *params->stackPtr != NULL);
    IRO_ASSERT(3028, params->map == NULL || params->map != NULL);

    if (!LocationSet_IsUnknown(ls)) {
        PAMemoryBlock* block;
        if ((block = LocationSet_block(ls)) &&
            PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
            ExtendedParam* ep = PAMemoryBlock_thing(block);
            if (ep) {
                ObjectSet* objSet = ExtendedParam_objectSet(ep);
                Object* obj = NULL;
                ObjectSet_ForEach(objSet, FindGlobalObjectAction, &obj);
                if (!obj) {
                    LocationSetSet_Add(params->toBeRemoved, ls);
                    params->result |= GetActualLocsOfExtendedParam(
                        params->toBeAdded, ls, NULL, params->stackPtr, params->map, FALSE
                    );
                }
            }
        }
    }
}

static Boolean ExpandLocationSetSetToActuals(
    Stack** stackPtr, ParamMappingFunction* map, LocationSetSet* thingsPointedTo
) {
    LocationSetSet* toBeRemoved;
    LocationSetSet* toBeAdded;
    ExpandLocationSetSetActionParams params;
    Boolean result;

    IRO_ASSERT(3062, stackPtr == NULL || *stackPtr == NULL || *stackPtr != NULL);
    IRO_ASSERT(3063, map == NULL || map != NULL);
    IRO_ASSERT(3064, thingsPointedTo != NULL);

    toBeRemoved = LocationSetSet_New();
    LocationSetSet_Init(toBeRemoved);

    toBeAdded = LocationSetSet_New();
    LocationSetSet_Init(toBeAdded);

    memset(&params, 0, sizeof(params));
    params.toBeRemoved = toBeRemoved;
    params.toBeAdded = toBeAdded;
    params.stackPtr = stackPtr;
    params.map = map;
    params.result = FALSE;

    LocationSetSet_ForEach(thingsPointedTo, ExpandLocationSetSetToActualsAction, &params);

    result = params.result;
    LocationSetSet_RemoveSet(thingsPointedTo, toBeRemoved);
    LocationSetSet_AddSet(thingsPointedTo, toBeAdded);

    LocationSetSet_Term(toBeRemoved);
    LocationSetSet_Delete(toBeRemoved);

    LocationSetSet_Term(toBeAdded);
    LocationSetSet_Delete(toBeAdded);

    return result;
}

static void EvaluatePartialAbsolute(
    LocationSetSet* set, LocationSetSet* thingsPointedTo, LocationSet* dst, Type* indRtype
) {
    LocationSet* absLoc;
    Type* absLocRtype;

    IRO_ASSERT(3107, set != NULL);
    IRO_ASSERT(3108, thingsPointedTo != NULL);
    IRO_ASSERT(3109, dst != NULL);
    IRO_ASSERT(3110, indRtype != NULL);

    absLoc = LocationSetSet_FindFirst(thingsPointedTo);
    IRO_ASSERT(3113, absLoc != NULL);

    if (!LocationSet_IsUnknown(absLoc)) {
        absLocRtype = LocationSet_rtype(absLoc);
    }

    if (!LocationSet_IsUnknown(absLoc) && indRtype->size == absLocRtype->size) {
        LocationSetSet_AddSet(set, thingsPointedTo);
    } else {
        LocationSetSet_AddUnknown(set, indRtype, NULL, NULL);
    }
}

// NOTE: invented names: recordInputs, ptf, dst, set
static Boolean AddToInitialPointsToFunc(
    Boolean recordInputs, PartialTransferFunction* ptf, LocationSet* dst, LocationSetSet* set
) {
    Boolean result;
    LocationSet* ls;
    PointsToFunction* initial;
    PointsToEntry* pte;

    IRO_ASSERT(3191, ptf == NULL || ptf != NULL);
    IRO_ASSERT(3192, dst != NULL);
    IRO_ASSERT(3193, set != NULL);

    result = FALSE;

    if (recordInputs && ptf) {
        if (!(ls = LocationSetSet_FindUnknown(set)) || LocationSet_rtype(ls)) {
            if ((initial = PartialTransferFunction_initialPointsToFn(ptf)) &&
                !PointsToFunction_FindByLookupCompatibleLocationSet(initial, dst)) {
                IRO_ASSERT(3207, dst != NULL);

                pte = PointsToEntry_New();
                PointsToEntry_Init(pte, dst, set);
                result = PointsToFunction_AddWithoutChecking(initial, pte);
                PointsToEntry_Term(pte);
                PointsToEntry_Delete(pte);
            }
        }
    }

    return result;
}

static Boolean Lookup(
    LocationSetSet* set,
    Stack** stackPtr,
    Object* proc,
    ParamMappingFunction* map,
    PartialTransferFunction* ptf,
    LocationSet* dst,
    PointsToFunction* pointsToFunc,
    Boolean recordInputs,
    Type* indRtype
) {
    LocationSetSet* mySet;
    LocationSetSet* set2;
    Boolean result;
    PAMemoryBlock* block;
    ExtendedParam* ep;
    PALocalVar* local;
    ObjectSet* objSet;
    PointsToEntry* pte;
    LocationSetSet* lss3;
    LocationSet* unknown; // NOTE: invented name
    EvalExprAction3Params params;
    Object* obj;
    Stack* next;
    StackElement* element;

    IRO_ASSERT(3244, set == NULL || set != NULL);
    IRO_ASSERT(3245, stackPtr == NULL || *stackPtr == NULL || *stackPtr != NULL);
    IRO_ASSERT(3246, proc == NULL || proc != NULL);
    IRO_ASSERT(3247, map == NULL || map != NULL);
    IRO_ASSERT(3248, ptf == NULL || ptf != NULL);
    IRO_ASSERT(3249, dst != NULL);
    IRO_ASSERT(3250, pointsToFunc == NULL || pointsToFunc != NULL);
    IRO_ASSERT(3251, indRtype == NULL || indRtype != NULL);

    result = FALSE;

    set2 = NULL;

    mySet = LocationSetSet_New();
    LocationSetSet_Init(mySet);

    if (proc) {
        if (LocationIsVolatile(dst, proc)) {
            LocationSetSet_AddUnknown(mySet, indRtype, NULL, NULL);
        }
    }

    if (pointsToFunc && !LocationSet_IsUnknown(dst)) {
        if ((pte = PointsToFunction_FindByLookupCompatibleLocationSet(pointsToFunc, dst))) {
            set2 = PointsToEntry_locs(pte);
        } else if (indRtype &&
                   (pte =
                        PointsToFunction_FindContainingLocationSet(pointsToFunc, dst, indRtype))) {
            set2 = PointsToEntry_locs(pte);
        }
    }

    if (set2) {
        if (indRtype) {
            EvaluatePartialAbsolute(mySet, set2, dst, indRtype);
        } else {
            LocationSetSet_AddSet(mySet, set2);
        }
    } else if (!set2) {
        block = NULL;
        if (!LocationSet_IsUnknown(dst)) {
            block = LocationSet_block(dst);
        }

        if (!LocationSet_IsUnknown(dst) && LocationSet_stride(dst)) {
            LocationSetSet_AddUnknown(mySet, indRtype, NULL, NULL);
        } else if (block &&
                   PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_EXTENDEDPARAM &&
                   (ep = PAMemoryBlock_thing(block)) &&
                   (objSet = ExtendedParam_objectSet(ep))) {
            obj = NULL;
            ObjectSet_ForEach(objSet, FindGlobalObjectAction, &obj);
            if (!obj) {
                lss3 = LocationSetSet_New();
                LocationSetSet_Init(lss3);
                result |=
                    GetActualLocsOfExtendedParam(lss3, dst, NULL, stackPtr, map, recordInputs);

                memset(&params, 0, sizeof(params));
                params.set = mySet;
                params.stackPtr = stackPtr;
                params.proc = proc;
                params.map = map;
                params.ptf = ptf;
                params.pointsToFunc = pointsToFunc;
                params.indRtype = indRtype;
                params.result = FALSE;
                LocationSetSet_ForEach(lss3, EvalExprAction3, &params);
                result |= params.result;

                LocationSetSet_Term(lss3);
                LocationSetSet_Delete(lss3);

                if ((unknown = LocationSetSet_FindUnknown(mySet))) {
                    if (!LocationSet_restriction(unknown) && ObjectSet_Count(objSet) == 1) {
                        if ((obj = ObjectSet_FindFirst(objSet))) {
                            if (ObjectIsRestrictQualified(obj)) {
                                LocationSet_Term(unknown);
                                LocationSet_InitUnknown(unknown, indRtype, block, NULL);
                            }
                        }
                    }
                }

                result |= AddToInitialPointsToFunc(recordInputs, ptf, dst, mySet);
            } else {
                result |=
                    GetActualLocsOfExtendedParam(NULL, dst, NULL, stackPtr, map, recordInputs);
                if (stackPtr &&
                    *stackPtr &&
                    (next = Stack_Next(stackPtr)) &&
                    (element = Stack_Top(&next))) {
                    result |= Lookup(
                        mySet,
                        &next,
                        StackElement_proc(element),
                        StackElement_map(element),
                        StackElement_ptf(element),
                        dst,
                        StackElement_funcCall(element)->pointsToFunction,
                        recordInputs,
                        indRtype
                    );
                } else {
                    LocationSetSet_AddUnknown(mySet, indRtype, NULL, NULL);
                }

                result |= AddToInitialPointsToFunc(recordInputs, ptf, dst, mySet);
            }
        } else if (block &&
                   PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_LOCALVAR &&
                   (local = PAMemoryBlock_thing(block)) &&
                   proc &&
                   (obj = GetLocalObject(local, proc, TRUE)) &&
                   ObjectIsAFunctionArgument(proc, obj)) {
            result |= LookupParam(mySet, dst, obj, stackPtr, proc, map, ptf, recordInputs);
        } else {
            LocationSetSet_AddUnknown(mySet, indRtype, NULL, NULL);
        }
    }

    if (set) {
        LocationSetSet_AddSet(set, mySet);
    }

    LocationSetSet_Term(mySet);
    LocationSetSet_Delete(mySet);

    return result;
}

static Boolean InputsHaveNewPointerValues(
    PartialTransferFunction* tgtPTF, PartialTransferFunction* ptf
) {
    IRO_ASSERT(3392, tgtPTF != NULL);
    IRO_ASSERT(3393, ptf != NULL);

    return FALSE;
}

static void CreateExtendedParamAction(ParamMapping* mapping, void* refcon) {
    CreateEPActionParams* params;
    ExtendedParam* ep;
    UInt32 value;
    ExtendedParam* lowestEP;
    UInt32 lowestValue;
    ExtendedParam* lastEP;
    UInt32 lastValue;

    IRO_ASSERT(3416, mapping != NULL);
    IRO_ASSERT(3417, refcon != NULL);

    params = refcon;

    IRO_ASSERT(3421, params->last == NULL || params->last != NULL);
    IRO_ASSERT(3422, params->lowest == NULL || params->lowest != NULL);

    ep = ParamMapping_extended(mapping);
    if (ep) {
        value = ExtendedParam_num(ep);

        if (params->lowest) {
            lowestEP = ParamMapping_extended(params->lowest);
            lowestValue = ExtendedParam_num(lowestEP);
            if (params->last) {
                lastEP = ParamMapping_extended(params->last);
                lastValue = ExtendedParam_num(lastEP);
                if (value > lastValue && value < lowestValue) {
                    params->lowest = mapping;
                }
            } else if (value < lowestValue) {
                params->lowest = mapping;
            }
        } else if (params->last) {
            lastEP = ParamMapping_extended(params->last);
            lastValue = ExtendedParam_num(lastEP);
            if (value > lastValue) {
                params->lowest = mapping;
            }
        } else {
            params->lowest = mapping;
        }
    }
}

static ExtendedParam* FindMatchingExtendedParam(
    Stack** stackPtr,
    ParamMappingFunction* map,
    LocationSetSet* lss,
    ParamMapping* lowest,
    Boolean* result
) {
    ExtendedParam* ep;
    ExtendedParam* lowestEP;

    IRO_ASSERT(3472, stackPtr == NULL || *stackPtr == NULL || *stackPtr != NULL);
    IRO_ASSERT(3473, map == NULL || map != NULL);
    IRO_ASSERT(3474, lss != NULL);
    IRO_ASSERT(3475, lowest != NULL);

    ep = NULL;
    lowestEP = ParamMapping_extended(lowest);
    if (lowestEP) {
        PAMemoryBlock* block;
        LocationSet* lowestLS;
        LocationSetSet* lowestLSS;

        block = PAMemoryBlock_New();
        PAMemoryBlock_Init(block, PAMEMORYBLOCKKIND_EXTENDEDPARAM, lowestEP);

        lowestLS = LocationSet_New();
        LocationSet_InitKnown(lowestLS, block, cint64_zero, 0, (Type*)&void_ptr);

        lowestLSS = LocationSetSet_New();
        LocationSetSet_Init(lowestLSS);

        *result |= GetActualLocsOfExtendedParam(lowestLSS, lowestLS, NULL, stackPtr, map, FALSE);
        if (LocationSetSets_Equal(lowestLSS, lss)) {
            ep = lowestEP;
        }

        LocationSetSet_Term(lowestLSS);
        LocationSetSet_Delete(lowestLSS);

        LocationSet_Term(lowestLS);
        LocationSet_Delete(lowestLS);
    }

    return ep;
}

static ExtendedParam* CreateExtendedParam(
    Stack** stackPtr, ParamMappingFunction* map, Object* var, Boolean* result
) {
    ExtendedParam* ep;
    ParamMapping* mapping;
    LocationSetSet* lss;
    CreateEPActionParams params;

    IRO_ASSERT(3517, map == NULL || map != NULL);
    IRO_ASSERT(3518, stackPtr == NULL || *stackPtr == NULL || *stackPtr != NULL);
    IRO_ASSERT(3519, var != NULL);

    mapping = NULL;
    if (map) {
        mapping = ParamMappingFunction_FindMappingByFormal(map, var);
    }

    ep = ExtendedParam_FindByObject(var);
    if (ep) {
        if (mapping) {
            IRO_ASSERT(3534, ep == ParamMapping_extended(mapping) || ParamMapping_extended(mapping) == NULL);
        }
    } else if (map && !ObjectIsRestrictQualified(var)) {
        lss = LocationSetSet_New();
        LocationSetSet_Init(lss);
        *result |= GetActualLocsOfExtendedParam(lss, NULL, var, stackPtr, map, FALSE);

        memset(&params, 0, sizeof(params));
        params.last = NULL;
        do {
            params.lowest = NULL;
            ParamMappingFunction_ForEach(map, CreateExtendedParamAction, &params);
            if (params.lowest && params.lowest != mapping) {
                ep = FindMatchingExtendedParam(stackPtr, map, lss, params.lowest, result);
            }
            params.last = params.lowest;
        } while (params.lowest && !ep);

        LocationSetSet_Term(lss);
        LocationSetSet_Delete(lss);
    }

    if (!ep) {
        ep = ExtendedParam_FindByObject(var);
    }

    if (!ep) {
        ep = ExtendedParam_New();
        ExtendedParam_Init(ep, var);
    } else {
        ExtendedParam_AddObject(ep, var);
    }

    if (stExtParamSet) {
        ExtendedParamSet_Add(stExtParamSet, ep);
    }

    IRO_ASSERT(3582, ep != NULL);

    return ep;
}

#if VERSION < VERSION_GC_2_5
// 2.0's file has 1 line fewer above here
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT -1
#endif
static void RecordActuals(IROLinear* nd, Object* proc, ParamMappingFunction* map) {
    IRO_ASSERT(3629, nd != NULL);
    IRO_ASSERT_MSG(3630, nd->type == IROLinearFunccall, "nd->lt == Lt_Funccall");
    IRO_ASSERT(3631, proc != NULL);
    IRO_ASSERT(3632, map != NULL);

    if (proc != &stUnknown) {
        int i;
        ObjectList* args;
        Object* arg;

        args = FunctionArguments(proc);
        if (args) {
            arg = args->object;
        } else {
            arg = &stUnknown;
        }

        for (i = 0; i < nd->u.funccall.argCount; i++) {
            IRO_ASSERT(3644, arg != NULL);

            if (arg != &stUnknown) {
                ParamMapping* mapping = ParamMapping_New();
                ParamMapping_Init(mapping, nd->u.funccall.args[i], arg, NULL);
                ParamMappingFunction_Add(map, mapping);
                ParamMapping_Term(mapping);
                ParamMapping_Delete(mapping);
            }

            if (args) {
                args = args->next;
                if (args) {
                    arg = args->object;
                } else {
                    arg = &stUnknown;
                }
            }
        }
    }
}

static Boolean IsAddressableLocation(
    LocationSet* loc, Object* proc, IRONode* fnode, IROLinear* nd
) {
    IRO_ASSERT(3677, loc != NULL);
    IRO_ASSERT(3678, fnode != NULL);
    IRO_ASSERT(3679, nd != NULL);

    if (!LocationSet_IsUnknown(loc)) {
        PAMemoryBlock* block;
        PALocalVar* local;
        Object* obj;
        ExtendedParam* ep;
        ObjectSet* objSet;

        block = LocationSet_block(loc);
        if (PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_LOCALVAR &&
            (local = PAMemoryBlock_thing(block)) &&
            (obj = GetLocalObject(local, proc, FALSE)) &&
            fnode->addressed &&
            !ObjectSet_Find(fnode->addressed, obj)) {
            return FALSE;
        }

        if (PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_EXTENDEDPARAM &&
            (ep = PAMemoryBlock_thing(block)) &&
            (objSet = ExtendedParam_objectSet(ep)) &&
            ObjectSet_Count(objSet) == 1 &&
            (obj = ObjectSet_FindFirst(objSet)) &&
            ObjectIsAFunctionArgument(proc, obj) &&
            ObjectIsRestrictQualified(obj)) {
            return FALSE;
        }
    }

    return TRUE;
}

static Boolean LocationSetsAlias(LocationSet* ls1, LocationSet* ls2) {
    IRO_ASSERT(3720, ls1 != NULL);
    IRO_ASSERT(3721, ls2 != NULL);

    return (ls1 == ls2) ||
        LocationSet_IsUnknown(ls1) ||
        LocationSet_IsUnknown(ls2) ||
        ((LocationSet_stride(ls1) != 0 ||
          LocationSet_stride(ls2) != 0 ||
          CInt64_Equal(LocationSet_field(ls1), LocationSet_field(ls2))) &&
         MemoryBlocks_Equal(LocationSet_block(ls1), LocationSet_block(ls2)));
}

static void FindAliasingAction2(LocationSet* ls, void* refcon) {
    FindAliasingParams* params;

    params = refcon;

    if (!params->aliased) {
        if (LocationSetsAlias(params->loc, ls)) {
            params->aliased = TRUE;
        }
    }
}

static void FindAliasingAction(LocationSet* loc, void* refcon) {
    FindAliasingParams* params;

    IRO_ASSERT(3752, loc != NULL);
    IRO_ASSERT(3753, refcon != NULL);

    params = refcon;

    if (!params->aliased) {
        params->loc = loc;
        LocationSetSet_ForEach(params->set, FindAliasingAction2, params);
    }
}

static void KillAllAddressableLocationsAction(PointsToEntry* pte, void* refcon) {
    KillLocationParams* params;
    LocationSet* loc;

    IRO_ASSERT(3780, pte != NULL);
    IRO_ASSERT(3781, refcon != NULL);

    params = refcon;

    IRO_ASSERT(3785, params->proc != NULL);
    IRO_ASSERT(3786, params->fnode != NULL);
    IRO_ASSERT(3787, params->nd != NULL);
    IRO_ASSERT(3788, params->ptf != NULL);
    IRO_ASSERT(3789, params->toBeKilled != NULL);
    IRO_ASSERT(3790, params->dst == NULL);

    loc = PointsToEntry_loc(pte);
    IRO_ASSERT(3794, loc != NULL);
    IRO_ASSERT(3795, !LocationSet_IsUnknown(loc));

    if (IsAddressableLocation(loc, params->proc, params->fnode, params->nd)) {
        PointsToFunction* toBeKilled = params->toBeKilled;
        PointsToFunction_Add(toBeKilled, pte);
    }
}

static void KillAllAliasingExtParamLocsAction(PointsToEntry* pte, void* refcon) {
    KillLocationParams* params;
    LocationSet* loc;

    IRO_ASSERT(3814, pte != NULL);
    IRO_ASSERT(3815, refcon != NULL);

    params = refcon;

    IRO_ASSERT(3819, params->proc != NULL);
    IRO_ASSERT(3820, params->fnode == NULL || params->fnode != NULL);
    IRO_ASSERT(3821, params->nd == NULL || params->nd != NULL);
    IRO_ASSERT(3822, params->ptf != NULL);
    IRO_ASSERT(3823, params->toBeKilled != NULL);
    IRO_ASSERT(3824, params->dst != NULL);
    IRO_ASSERT(3825, LocationSet_block(params->dst) != NULL);
    IRO_ASSERT(3826, PAMemoryBlock_kind(LocationSet_block(params->dst)) == PAMEMORYBLOCKKIND_EXTENDEDPARAM);

    loc = PointsToEntry_loc(pte);
    IRO_ASSERT(3830, loc != NULL);
    IRO_ASSERT(3831, !LocationSet_IsUnknown(loc));

    if (loc != params->dst) {
        if (LocationSetsAlias(loc, params->dst)) {
            PointsToFunction_Add(params->toBeKilled, pte);
        } else {
            ExtendedParam* ep;
            ObjectSet* objSet;
            Object* obj;
            PAMemoryBlock* block;

            if ((ep = PAMemoryBlock_thing(LocationSet_block(params->dst))) &&
                (objSet = ExtendedParam_objectSet(ep)) &&
                (ObjectSet_Count(objSet) != 1 ||
                 !(obj = ObjectSet_FindFirst(objSet)) ||
                 !ObjectIsRestrictQualified(obj)) &&
                (block = LocationSet_block(loc)) &&
                PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_EXTENDEDPARAM &&
                (ep = PAMemoryBlock_thing(block)) &&
                (objSet = ExtendedParam_objectSet(ep)) &&
                (ObjectSet_Count(objSet) != 1 ||
                 !(obj = ObjectSet_FindFirst(objSet)) ||
                 !ObjectIsRestrictQualified(obj))) {
                LocationSetSet* lss1;
                LocationSetSet* lss2;
                Boolean changed;
                FindAliasingParams aparams;

                lss1 = LocationSetSet_New();
                LocationSetSet_Init(lss1);
                lss2 = LocationSetSet_New();
                LocationSetSet_Init(lss2);

                changed = GetActualLocsOfExtendedParam(
                    lss1, loc, NULL, &stCallingContextStack, NULL, FALSE
                );
                changed |= GetActualLocsOfExtendedParam(
                    lss2, params->dst, NULL, &stCallingContextStack, NULL, FALSE
                );
                IRO_ASSERT(3863, !changed);

                memset(&aparams, 0, sizeof(aparams));
                aparams.aliased = FALSE;
                aparams.set = lss2;
                LocationSetSet_ForEach(lss1, FindAliasingAction, &aparams);
                if (aparams.aliased) {
                    PointsToFunction_Add(params->toBeKilled, pte);
                }

                LocationSetSet_Term(lss1);
                LocationSetSet_Delete(lss1);
                LocationSetSet_Term(lss2);
                LocationSetSet_Delete(lss2);
            }
        }
    }
}

static void KillLocationsAction(PointsToEntry* pte, void* refcon) {
    KillLocationParams* params;
    LocationSet* loc;
    LocationSetSet* lss;

    IRO_ASSERT(3887, pte != NULL);
    IRO_ASSERT(3888, refcon != NULL);

    params = refcon;

    IRO_ASSERT(3892, params->proc != NULL);
    IRO_ASSERT(3893, params->fnode == NULL || params->fnode != NULL);
    IRO_ASSERT(3894, params->nd == NULL || params->nd != NULL);
    IRO_ASSERT(3895, params->ptf != NULL);
    IRO_ASSERT(3896, params->toBeKilled != NULL);
    IRO_ASSERT(3897, params->dst == NULL || params->dst != NULL);

    loc = PointsToEntry_loc(pte);
    IRO_ASSERT(3901, loc != NULL);
    IRO_ASSERT(3902, !LocationSet_IsUnknown(loc));

    lss = LocationSetSet_New();
    LocationSetSet_Init(lss);
    LocationSetSet_AddUnknown(lss, NULL, NULL, NULL);

    params->result |= Assign(params->ptf, loc, lss, params->proc, params->nd, params->fnode);

    LocationSetSet_Term(lss);
    LocationSetSet_Delete(lss);
}

static Boolean KillAllAddressableLocations(
    Object* proc, IRONode* fnode, IROLinear* nd, PartialTransferFunction* ptf
) {
    Boolean result;
    PointsToFunction* pointsToFunc;
    PointsToFunction* pointsToFuncCopy;
    PointsToFunction* toBeKilled;
    KillLocationParams params;

    IRO_ASSERT(3922, proc != NULL);
    IRO_ASSERT(3923, fnode == NULL || fnode != NULL);
    IRO_ASSERT(3924, nd == NULL || nd != NULL);
    IRO_ASSERT(3925, ptf != NULL);

    if (nd && nd->pointsToFunction) {
        pointsToFunc = nd->pointsToFunction;
    } else {
        pointsToFunc = PartialTransferFunction_finalPointsToFn(ptf);
    }

    pointsToFuncCopy = PointsToFunction_New();
    PointsToFunction_Copy(pointsToFuncCopy, pointsToFunc);

    toBeKilled = PointsToFunction_New();
    PointsToFunction_Init(toBeKilled);

    memset(&params, 0, sizeof(params));
    params.proc = proc;
    params.fnode = fnode;
    params.nd = nd;
    params.ptf = ptf;
    params.toBeKilled = toBeKilled;
    params.dst = NULL;
    params.result = FALSE;

    if (pointsToFunc) {
        PointsToFunction_ForEach(pointsToFunc, KillAllAddressableLocationsAction, &params);
        PointsToFunction_ForEach(params.toBeKilled, KillLocationsAction, &params);
    }

    PointsToFunction_Term(toBeKilled);
    PointsToFunction_Delete(toBeKilled);

    if (params.result) {
        result = !PointsToFunctions_Equal(pointsToFuncCopy, pointsToFunc);
    } else {
        result = FALSE;
    }

    PointsToFunction_Term(pointsToFuncCopy);
    PointsToFunction_Delete(pointsToFuncCopy);

    return result;
}

static void KillAllAliasingExtParamLocs(
    Object* proc, IRONode* fnode, IROLinear* nd, PartialTransferFunction* ptf, LocationSet* dst
) {
    PointsToFunction* pointsToFunc;
    PointsToFunction* toBeKilled;
    KillLocationParams params;

    IRO_ASSERT(3975, proc != NULL);
    IRO_ASSERT(3976, fnode == NULL || fnode != NULL);
    IRO_ASSERT(3977, nd == NULL || nd != NULL);
    IRO_ASSERT(3978, ptf != NULL);
    IRO_ASSERT(3979, dst != NULL);

    if (!LocationSet_IsUnknown(dst) && LocationSet_block(dst)) {
        if (PAMemoryBlock_kind(LocationSet_block(dst)) == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
            if (nd && nd->pointsToFunction) {
                pointsToFunc = nd->pointsToFunction;
            } else {
                pointsToFunc = PartialTransferFunction_finalPointsToFn(ptf);
            }

            toBeKilled = PointsToFunction_New();
            PointsToFunction_Init(toBeKilled);

            memset(&params, 0, sizeof(params));
            params.proc = proc;
            params.fnode = fnode;
            params.nd = nd;
            params.ptf = ptf;
            params.toBeKilled = toBeKilled;
            params.dst = dst;
            params.result = FALSE;

            if (pointsToFunc) {
                PointsToFunction_ForEach(pointsToFunc, KillAllAliasingExtParamLocsAction, &params);
                PointsToFunction_ForEach(params.toBeKilled, KillLocationsAction, &params);
            }

            PointsToFunction_Term(toBeKilled);
            PointsToFunction_Delete(toBeKilled);
        }
    }
}

static Boolean Assign(
    PartialTransferFunction* ptf,
    LocationSet* dst,
    LocationSetSet* srcs,
    Object* proc,
    IROLinear* nd,
    IRONode* fnode
) {
    PointsToFunction* pointsToFunc;
    Boolean result;
    PointsToEntry* pte;
    LocationSet* loc;
    LocationSetSet* locs;
    LocationSet* bitfieldOf;
    LocationSetSet* lss;

    IRO_ASSERT(4028, ptf != NULL);
    IRO_ASSERT(4029, dst != NULL);
    IRO_ASSERT(4030, srcs != NULL);
    IRO_ASSERT(4031, proc != NULL);
    IRO_ASSERT(4032, nd == NULL || nd != NULL);
    IRO_ASSERT(4033, fnode == NULL || fnode != NULL);

    if (nd) {
        if (!nd->pointsToFunction) {
            nd->pointsToFunction = PointsToFunction_New();
            PointsToFunction_Init(nd->pointsToFunction);
        }
        pointsToFunc = nd->pointsToFunction;
    } else {
        pointsToFunc = PartialTransferFunction_finalPointsToFn(ptf);
    }

    pte = PointsToFunction_FindByLookupCompatibleLocationSet(pointsToFunc, dst);
    if (pte) {
        loc = PointsToEntry_loc(pte);
        locs = PointsToEntry_locs(pte);
        IRO_ASSERT(4057, !LocationSet_IsUnknown(dst));
        IRO_ASSERT(4058, LocationSet_stride(dst) == 0 || LocationSet_stride(loc) != 0);

        result = !LocationSetSets_Equal(srcs, locs) ||
            LocationSet_stride(dst) != LocationSet_stride(loc);

        if (result) {
            pte = PointsToEntry_New();
            PointsToEntry_Init(pte, dst, srcs);
            PointsToFunction_RemoveByLocationSet(pointsToFunc, loc);
            PointsToFunction_AddWithoutChecking(pointsToFunc, pte);
            PointsToEntry_Term(pte);
            PointsToEntry_Delete(pte);
        }
    } else if (!LocationSet_IsUnknown(dst)) {
        KillAllAliasingExtParamLocs(proc, fnode, nd, ptf, dst);

        pte = PointsToEntry_New();
        PointsToEntry_Init(pte, dst, srcs);
        result = PointsToFunction_AddWithoutChecking(pointsToFunc, pte);
        PointsToEntry_Term(pte);
        PointsToEntry_Delete(pte);
    } else if ((bitfieldOf = LocationSet_bitfieldOf(dst))) {
        lss = LocationSetSet_New();
        LocationSetSet_Init(lss);
        LocationSetSet_AddUnknown(lss, NULL, NULL, NULL);
        result = Assign(ptf, bitfieldOf, lss, proc, nd, fnode);
        LocationSetSet_Term(lss);
        LocationSetSet_Delete(lss);
    } else if (!LocationSet_restriction(dst)) {
        result = KillAllAddressableLocations(proc, fnode, nd, ptf);
    }

    // the original's: an unknown `dst` with a restriction returns `result` unset
    return result;
}

static void EvalMeetAction(PointsToEntry* pte, void* refcon) {
    EvalMeetActionParams* params;
    LocationSet* loc;
    LocationSetSet* set;
    UInt16 i;

    IRO_ASSERT(4124, pte != NULL);
    IRO_ASSERT(4125, refcon != NULL);

    params = refcon;

    IRO_ASSERT(4129, params->proc != NULL);
    IRO_ASSERT(4130, params->fnode != NULL);
    IRO_ASSERT(4131, params->pred != NULL);
    IRO_ASSERT(4132, params->nd != NULL);
    IRO_ASSERT(4133, params->ptf != NULL);

    loc = PointsToEntry_loc(pte);

    set = LocationSetSet_New();
    LocationSetSet_Init(set);
    LocationSetSet_AddSet(set, PointsToEntry_locs(pte));

    for (i = 0; i < params->fnode->numpred; i++) {
        IRONode* pred = FunctionNodeTable(params->proc)[params->fnode->pred[i]];
        if (pred->marked && pred != params->pred) {
            params->result |= Lookup(
                set,
                &stCallingContextStack,
                params->proc,
                NULL,
                params->ptf,
                loc,
                pred->last->pointsToFunction,
                FALSE,
                NULL
            );
        }
    }

    params->assignChanged |= Assign(params->ptf, loc, set, params->proc, params->nd, params->fnode);
    LocationSetSet_Term(set);
    LocationSetSet_Delete(set);
}

static Boolean EvalMeet(Object* proc, IRONode* fnode, IROLinear* nd, PartialTransferFunction* ptf) {
    PointsToFunction* pointsToFunc;
    EvalMeetActionParams params;
    int i;

    IRO_ASSERT(4164, proc != NULL);
    IRO_ASSERT(4165, fnode != NULL);
    IRO_ASSERT(4166, nd != NULL);
    IRO_ASSERT(4167, ptf != NULL);

    pointsToFunc = PointsToFunction_New();
    if (nd->pointsToFunction) {
        PointsToFunction_Copy(pointsToFunc, nd->pointsToFunction);
    } else {
        PointsToFunction_Init(pointsToFunc);
    }

    memset(&params, 0, sizeof(params));
    params.proc = proc;
    params.fnode = fnode;
    params.nd = nd;
    params.ptf = ptf;
    params.result = FALSE;
    params.assignChanged = FALSE;

    for (i = 0; i < fnode->numpred; i++) {
        IRONode* pred = params.pred = FunctionNodeTable(proc)[fnode->pred[i]];
        if (pred->marked && pred->last->pointsToFunction) {
            PointsToFunction_ForEach(pred->last->pointsToFunction, EvalMeetAction, &params);
        }
    }

    if (!params.result && params.assignChanged) {
        if (nd->pointsToFunction) {
            params.result = !PointsToFunctions_Equal(pointsToFunc, nd->pointsToFunction);
        } else {
            params.result = PointsToFunction_FindFirst(pointsToFunc) != NULL;
        }
    }

    PointsToFunction_Term(pointsToFunc);
    PointsToFunction_Delete(pointsToFunc);

    return params.result;
}

static PartialTransferFunction* AllocatePTF(
    Object* proc, IROLinear* nd, PartialTransferFunction* ptf
) {
    PartialTransferFunction* newPTF;

    IRO_ASSERT(4211, proc != NULL);
    IRO_ASSERT(4212, proc->u.func.ptfList != NULL);
    IRO_ASSERT(4213, nd == NULL || nd != NULL);
    IRO_ASSERT(4214, ptf == NULL || ptf != NULL);

    newPTF = PartialTransferFunction_New();
    PartialTransferFunction_Init(newPTF, nd, ptf);
    PTFList_Add(proc->u.func.ptfList, newPTF);
    return newPTF;
}

static Object* FindMainEntryPoint(Object* function) {
    IRO_ASSERT(4230, function != NULL);

    return function;
}

static ObjectList* FunctionArguments(Object* proc) {
    ObjectList* prev;
    Object* search;
    ObjectList* scan;
    ObjectList* list;
    ExtendedParam* ep;
    char* name;
    VarInfo* vi;
    Object* obj;
    FuncArg* args;
    Boolean notFound;

    IRO_ASSERT(4253, proc != NULL);

    if (proc == stCurrentProc) {
        for (list = arguments; list; list = list->next) {
            if ((obj = list->object) && obj->name && (name = obj->name->name) && name[0]) {
                prev = NULL;
                for (scan = proc->u.func.argList; scan; scan = scan->next) {
                    prev = scan;
                    // (the original tests the address of the `name->name` array: always true)
                    if ((search = scan->object) && search->name && &search->name->name[0]) {
                        if (!strcmp(name, search->name->name)) {
                            break;
                        }
                    }
                }

                if (!scan) {
                    search = NULL;
                }

                notFound = search == NULL;

                if (!search) {
                    search = IRO_malloc(sizeof(Object));
                    ep = NULL;
                    search->u.var.info = IRO_malloc(sizeof(VarInfo));
                    memset(search->u.var.info, 0, sizeof(VarInfo));
                    search->u.var.info->func = proc;
                } else {
                    ep = search->extParam;
                }

                vi = search->u.var.info;
                memcpy(search, obj, sizeof(Object));
                search->extParam = ep;
                search->u.var.info = vi;
                search->u.var.realObj = obj;

                if (notFound) {
                    scan = IRO_malloc(sizeof(ObjectList));
                    scan->next = NULL;
                    scan->object = search;
                    if (!prev) {
                        proc->u.func.argList = scan;
                    } else {
                        prev->next = scan;
                    }
                }
            }
        }
    } else if (proc->type) {
        for (args = TYPE_FUNC(proc->type)->args; args; args = args->next) {
            if (args->name && (name = args->name->name) && name[0]) {
                prev = NULL;
                for (scan = proc->u.func.argList; scan; scan = scan->next) {
                    prev = scan;
                    // (the original tests the address of the `name->name` array: always true)
                    if ((search = scan->object) && search->name && &search->name->name[0]) {
                        if (!strcmp(name, search->name->name)) {
                            break;
                        }
                    }
                }

                if (!scan) {
                    search = NULL;
                }

                if (!search) {
                    search = IRO_malloc(sizeof(Object));
                    memset(search, 0, sizeof(Object));
                    search->datatype = DLOCAL;
                    search->extParam = NULL;
                    search->name = GetHashNameNodeExport(name);
                    search->type = args->type;
                    search->qual = args->qual;
                    search->u.var.info = IRO_malloc(sizeof(VarInfo));
                    memset(search->u.var.info, 0, sizeof(VarInfo));
                    search->u.var.info->func = proc;

                    scan = IRO_malloc(sizeof(ObjectList));
                    scan->next = NULL;
                    scan->object = search;
                    if (!prev) {
                        proc->u.func.argList = scan;
                    } else {
                        prev->next = scan;
                    }
                }
            }
        }
    }

    return proc->u.func.argList;
}

static IRONode** FunctionNodeTable(Object* proc) {
    IRO_ASSERT(4384, proc != NULL);

    IRO_ASSERT(4392, proc == stCurrentProc);

    return IRO_NodeTable;
}

static IRONode* FunctionFirstNode(Object* proc) {
    IRO_ASSERT(4402, proc != NULL);

    IRO_ASSERT(4410, proc == stCurrentProc);

    return IRO_FirstNode;
}

// Dead in every release (the linker drops it), but its literals are in the object.
// TODO: the original's UpdatePTFDomain asserts `home != NULL` (the literal sits between
// EvalCallAction's and AllocatePTF's); the rest of its body, the parameter's type and the
// assertion's line are not known
static void UpdatePTFDomain(void* home) {
    IRO_ASSERT(4420, home != NULL);
}

static void EvalCallAction(Object* proc, void* refcon) {
    ParamMappingFunction* pmf;
    PartialTransferFunction* tgtPTF;
    EvalCallActionParams* params;
    Boolean pushed; // NOTE: invented name (the call's context is on stCallingContextStack)
    Boolean needVisit;

    IRO_ASSERT(4459, proc != NULL);
    IRO_ASSERT(4460, refcon != NULL);

    params = refcon;

    IRO_ASSERT(4464, params->proc != NULL);
    IRO_ASSERT(4465, params->fnode != NULL);
    IRO_ASSERT(4466, params->nd != NULL);
    IRO_ASSERT(4467, params->ptf != NULL);
    IRO_ASSERT(4468, params->map == NULL || params->map != NULL);

    if (!params->stop) {
        pmf = ParamMappingFunction_New();
        ParamMappingFunction_Init(pmf);
        RecordActuals(params->nd, proc, pmf);

        pushed = FALSE;
        if (!Stack_FindByKey(&stCallingContextStack, proc)) {
            StackElement* element;

            needVisit = FALSE;
            tgtPTF = GetPTF(pmf, proc, params->nd, params->ptf, &needVisit);

            element = StackElement_New();
            StackElement_Init(element, proc, tgtPTF, pmf, params->nd);
            Stack_Push(&stCallingContextStack, element);
            StackElement_Term(element);
            StackElement_Delete(element);
            pushed = TRUE;

            IRO_ASSERT(4491, tgtPTF != NULL);

            if (stPTFList) {
                PTFList_Add(stPTFList, tgtPTF);
            }
            PartialTransferFunction_SetAnalyzed(tgtPTF, FALSE);

            if (needVisit || params->visitTarget) {
                if (params->visitTarget) {
                    params->visitTarget = FALSE;
                    EvalProc(proc, pmf, tgtPTF);
                } else {
                    tgtPTF = stUnknownPTF;
                }
            }
        } else {
            tgtPTF = stUnknownPTF;
        }

        if (params->map) {
            params->result |= ApplySummary(
                tgtPTF,
                pmf,
                params->proc,
                params->fnode,
                params->nd,
                params->ptf,
                params->map,
                params->targetCount == 1
            );
        }

        if (pushed) {
            StackElement* element = Stack_Pop(&stCallingContextStack);
            StackElement_Term(element);
            StackElement_Delete(element);
        }

        ParamMappingFunction_Term(pmf);
        ParamMappingFunction_Delete(pmf);
    }
}

static Boolean EvalCall(
    Object* proc,
    IRONode* fnode,
    IROLinear* nd,
    ParamMappingFunction* map,
    PartialTransferFunction* ptf
) {
    EvalCallActionParams params;
    ObjectSet* objSet;

    IRO_ASSERT(4549, proc != NULL);
    IRO_ASSERT(4550, fnode != NULL);
    IRO_ASSERT(4551, nd != NULL);
    IRO_ASSERT(4552, map != NULL);
    IRO_ASSERT(4553, ptf != NULL);

    memset(&params, 0, sizeof(params));
    params.proc = proc;
    params.fnode = fnode;
    params.nd = nd;
    params.ptf = ptf;
    params.map = map;
    params.stop = FALSE;
    params.result = FALSE;
    params.visitTarget = FALSE;

    objSet = ObjectSet_New();
    ObjectSet_Init(objSet);

    params.result |= FindCallTargets(objSet, proc, nd, map, ptf);
    params.targetCount = ObjectSet_Count(objSet);
    ObjectSet_ForEach(objSet, EvalCallAction, &params);

    ObjectSet_Term(objSet);
    ObjectSet_Delete(objSet);

    return params.result;
}

static void AdjustTypesForVolatilityAction(LocationSet* ls, void* refcon) {
    Type* type;
    Type* newtype;
    UInt32 qual;

    type = LocationSet_rtype(ls);
    switch (type->type) {
        case TYPEARRAY:
            qual = TYPE_POINTER(type)->qual;
            break;
        case TYPEPOINTER:
            qual = TYPE_POINTER(type)->qual;
            break;
        case TYPEMEMBERPOINTER:
            qual = TYPE_MEMBER_POINTER(type)->qual;
            break;
        default:
            CError_FATAL(4605);
    }

    if (!(qual & Q_VOLATILE)) {
        switch (type->type) {
            case TYPEARRAY:
                newtype = CDecl_NewArrayType(TYPE_POINTER(type)->target, type->size);
                TYPE_POINTER(newtype)->qual |= Q_VOLATILE;
                break;
            case TYPEPOINTER:
                newtype = CDecl_NewPointerType(TYPE_POINTER(type)->target);
                TYPE_POINTER(newtype)->qual |= Q_VOLATILE;
                break;
            case TYPEMEMBERPOINTER:
                newtype = galloc(sizeof(TypeMemberPointer));
                memcpy(newtype, type, sizeof(TypeMemberPointer));
                TYPE_MEMBER_POINTER(newtype)->qual |= Q_VOLATILE;
                break;
            default:
                break;
        }

        LocationSet_SetRtype(ls, newtype);
    }
}

static void AdjustTypesForVolatility(LocationSetSet* set, Object* proc, IROLinear* nd) {
    if (nd->rtype && CParser_IsVolatile(nd->rtype, nd->nodeflags & 3)) {
        LocationSetSet_ForEach(set, AdjustTypesForVolatilityAction, NULL);
    }
}

static void EvalAssignAction2(LocationSet* ls, void* refcon) {
    EvalAssignAction2Params* params;
    CInt64 value;
    IROLinear* nd;
    ENodeType oper;
    UInt32 stride;
    Type* rtype;
    PAMemoryBlock* block;

    IRO_ASSERT(4658, ls != NULL);
    IRO_ASSERT(4659, refcon != NULL);

    params = refcon;

    if (!params->failed && !LocationSet_IsUnknown(ls)) {
        value = params->value;
        nd = params->nd;
        IRO_ASSERT(4666, nd != NULL);
        oper = nd->nodetype;

        stride = LocationSet_stride(ls);
        rtype = LocationSet_rtype(ls);

        if ((block = LocationSet_block(ls)) && PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_INT) {
            LocationSet_Term(ls);

            switch (oper) {
                case EPOSTINC:
                case EPREINC:
                case EADDASS:
                    value = CInt64_Add(*((CInt64*)PAMemoryBlock_thing(block)), value);
                    break;
                case EPOSTDEC:
                case EPREDEC:
                case ESUBASS:
                    value = CInt64_Sub(*((CInt64*)PAMemoryBlock_thing(block)), value);
                    break;
                case EMULASS:
                    if (is_unsigned(nd->rtype)) {
                        value = CInt64_MulU(*((CInt64*)PAMemoryBlock_thing(block)), value);
                    } else {
                        value = CInt64_Mul(*((CInt64*)PAMemoryBlock_thing(block)), value);
                    }
                    break;
                case EDIVASS:
                    if (CInt64_IsZero(&value)) {
                        if (nd->stmt && nd->stmt->sourceoffset) {
                            TStreamElement* e = CPrep_CurStreamElement();
                            e->tokenoffset = nd->stmt->sourceoffset;
                            CError_SetErrorToken(e);
                        }
                        CError_Warning(ERR_DIVISION_BY_0);
                        params->failed = TRUE;
                    } else {
                        if (is_unsigned(nd->rtype)) {
                            value = CInt64_DivU(*((CInt64*)PAMemoryBlock_thing(block)), value);
                        } else {
                            value = CInt64_Div(*((CInt64*)PAMemoryBlock_thing(block)), value);
                        }
                    }
                    break;
                case EMODASS:
                    if (CInt64_IsZero(&value)) {
                        if (nd->stmt && nd->stmt->sourceoffset) {
                            TStreamElement* e = CPrep_CurStreamElement();
                            e->tokenoffset = nd->stmt->sourceoffset;
                            CError_SetErrorToken(e);
                        }
                        CError_Warning(ERR_DIVISION_BY_0);
                        params->failed = TRUE;
                    } else {
                        if (is_unsigned(nd->rtype)) {
                            value = CInt64_ModU(*((CInt64*)PAMemoryBlock_thing(block)), value);
                        } else {
                            value = CInt64_Mod(*((CInt64*)PAMemoryBlock_thing(block)), value);
                        }
                    }
                    break;
                case ESHLASS:
                    value = CInt64_Shl(*((CInt64*)PAMemoryBlock_thing(block)), value);
                    break;
                case ESHRASS:
                    if (is_unsigned(nd->rtype)) {
                        value = CInt64_ShrU(*((CInt64*)PAMemoryBlock_thing(block)), value);
                    } else {
                        value = CInt64_Shr(*((CInt64*)PAMemoryBlock_thing(block)), value);
                    }
                    break;
                case EANDASS:
                    value = CInt64_And(*((CInt64*)PAMemoryBlock_thing(block)), value);
                    break;
                case EXORASS:
                    value = CInt64_Xor(*((CInt64*)PAMemoryBlock_thing(block)), value);
                    break;
                case EORASS:
                    value = CInt64_Or(*((CInt64*)PAMemoryBlock_thing(block)), value);
                    break;
                default:
                    CError_FATAL(4747);
            }

            block = PAMemoryBlock_New();
            PAMemoryBlock_Init(block, PAMEMORYBLOCKKIND_INT, &value);
            LocationSet_InitKnown(ls, block, cint64_zero, stride, rtype);
        } else {
            if (oper == EPOSTDEC || oper == EPREDEC || oper == ESUBASS) {
                value = CInt64_Neg(value);
            }

            switch (oper) {
                case EPOSTINC:
                case EPOSTDEC:
                case EPREINC:
                case EPREDEC:
                case EADDASS:
                case ESUBASS:
                    value = CInt64_Add(LocationSet_field(ls), value);
                    if (stride) {
                        CInt64 strideValue; // NOTE: invented name
                        CInt64_SetLong(&strideValue, stride);
                        value = CInt64_Mod(value, strideValue);
                    }
                    LocationSet_SetField(ls, value);
                    break;
                default:
                    params->failed = TRUE;
                    break;
            }
        }
    }
}

static void EvalAssignAction(LocationSet* dst, void* refcon) {
    EvalAssignActionParams* params;
    LocationSetSet* srcs;

    IRO_ASSERT(4798, dst != NULL);
    IRO_ASSERT(4799, refcon != NULL);

    params = refcon;

    IRO_ASSERT(4803, params->proc != NULL);
    IRO_ASSERT(4804, params->ptf != NULL);
    IRO_ASSERT(4805, params->nd != NULL);
    IRO_ASSERT(4806, params->fnode != NULL);
    IRO_ASSERT(4807, params->srcs != NULL);

    srcs = params->srcs;
    if (!params->singleDst ||
        !LocationSetRepresentsSingleLocation(dst, params->proc, params->nd->pointsToFunction) ||
        LocationIsVolatile(dst, params->proc) ||
        LocationSet_IsLocalVarBase(dst)) {
        LocationSetSet* set = LocationSetSet_New();
        LocationSetSet_Init(set);
        params->result |= Lookup(
            set,
            &stCallingContextStack,
            params->proc,
            NULL,
            params->ptf,
            dst,
            params->nd->pointsToFunction,
            FALSE,
            params->nd->rtype
        );
        LocationSetSet_AddSet(srcs, set);
        LocationSetSet_Term(set);
        LocationSetSet_Delete(set);
    }

    params->assignChanged |=
        Assign(params->ptf, dst, srcs, params->proc, params->nd, params->fnode);
}

static Boolean EvalAssign(
    Object* proc,
    IROLinear* nd,
    IRONode* fnode,
    ParamMappingFunction* map,
    PartialTransferFunction* ptf
) {
    EvalAssignActionParams params;
    EvalAssignAction2Params params2;
    LocationSetSet* set;
    LocationSet* first; // NOTE: invented name
    PAMemoryBlock* block;
    Type* type;

    IRO_ASSERT(4841, proc != NULL);
    IRO_ASSERT(4842, nd != NULL);
    IRO_ASSERT(4843, fnode != NULL);
    IRO_ASSERT(4844, map != NULL);
    IRO_ASSERT(4845, ptf != NULL);

    memset(&params, 0, sizeof(params));
    params.proc = proc;
    params.ptf = ptf;
    params.nd = nd;
    params.fnode = fnode;
    params.result = FALSE;
    params.assignChanged = FALSE;

    set = LocationSetSet_New();
    params.srcs = LocationSetSet_New();
    LocationSetSet_Init(set);
    LocationSetSet_Init(params.srcs);

    if (nd->type == IROLinearOp2Arg) {
        IRO_ASSERT_MSG(
            4862,
            nd->u.diadic.left->type == IROLinearOp1Arg && nd->u.diadic.left->nodetype == EINDIRECT,
            "nd->u.Diadic.left->lt == Lt_Monadic && nd->u.Diadic.left->oper == EINDIRECT"
        );

        params.result |=
            EvalExpr(set, proc, nd->u.diadic.left->u.monadic, &stCallingContextStack, map, ptf);
        AdjustTypesForVolatility(set, proc, nd->u.diadic.left);
        params.result |=
            EvalExpr(params.srcs, proc, nd->u.diadic.right, &stCallingContextStack, map, ptf);

        if (nd->nodetype != EASS) {
            switch (nd->nodetype) {
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
                    if (LocationSetSet_Count(params.srcs) == 1 &&
                        (first = LocationSetSet_FindFirst(params.srcs)) &&
                        !LocationSet_IsUnknown(first) &&
                        !LocationSet_stride(first) &&
                        (block = LocationSet_block(first)) &&
                        PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_INT) {
                        LocationSetSet_Term(params.srcs);
                        LocationSetSet_Init(params.srcs);
                        params.result |= EvalExpr(
                            params.srcs, proc, nd->u.diadic.left, &stCallingContextStack, map, ptf
                        );

                        memset(&params2, 0, sizeof(params2));
                        params2.value = *((CInt64*)PAMemoryBlock_thing(block));
                        IRO_TruncateValueToType(&params2.value, nd->u.diadic.right->rtype);
                        params2.nd = nd;
                        params2.failed = FALSE;

                        if (!CInt64_IsZero(&params2.value)) {
                            LocationSetSet_ForEach(params.srcs, EvalAssignAction2, &params2);
                            if (params2.failed) {
                                LocationSetSet_Term(params.srcs);
                                LocationSetSet_Init(params.srcs);
                                params.result |= EvalExpr(
                                    params.srcs,
                                    proc,
                                    nd->u.diadic.left,
                                    &stCallingContextStack,
                                    map,
                                    ptf
                                );
                                if (!LocationSetSet_FindUnknown(params.srcs)) {
                                    LocationSetSet_ForEach(
                                        params.srcs, EvalExprAction2, (void*)&cint64_one
                                    );
                                }
                            }
                        }
                    } else {
                        LocationSetSet_Term(params.srcs);
                        LocationSetSet_Init(params.srcs);
                        params.result |= EvalExpr(
                            params.srcs, proc, nd->u.diadic.left, &stCallingContextStack, map, ptf
                        );
                        if (!LocationSetSet_FindUnknown(params.srcs)) {
                            LocationSetSet_ForEach(
                                params.srcs, EvalExprAction2, (void*)&cint64_one
                            );
                        }
                    }
                    break;
                default:
                    LocationSetSet_Term(params.srcs);
                    LocationSetSet_Init(params.srcs);
                    LocationSetSet_AddUnknown(params.srcs, nd->rtype, NULL, NULL);
                    break;
            }
        }
    } else if (nd->type == IROLinearOp1Arg) {
        IRO_ASSERT_MSG(
            4959,
            nd->u.monadic->type == IROLinearOp1Arg && nd->u.monadic->nodetype == EINDIRECT,
            "nd->u.Monadic.expr->lt == Lt_Monadic && nd->u.Monadic.expr->oper == EINDIRECT"
        );

        params.result |=
            EvalExpr(set, proc, nd->u.monadic->u.monadic, &stCallingContextStack, map, ptf);
        AdjustTypesForVolatility(set, proc, nd->u.monadic);
        params.result |=
            EvalExpr(params.srcs, proc, nd->u.monadic, &stCallingContextStack, map, ptf);

        switch (nd->nodetype) {
            case EPOSTINC:
            case EPOSTDEC:
            case EPREINC:
            case EPREDEC:
                memset(&params2, 0, sizeof(params2));
                params2.value = cint64_one;
                params2.nd = nd;
                params2.failed = FALSE;

                type = NULL;
                if (IS_TYPE_POINTER(nd->rtype)) {
                    type = TPTR_TARGET(nd->rtype);
                } else if (IS_TYPE_MEMBERPOINTER(nd->rtype)) {
                    type = TYPE_MEMBER_POINTER(nd->rtype)->ty1;
                }

                if (type) {
                    CInt64_SetLong(&params2.value, type->size);
                }

                if (!CInt64_IsZero(&params2.value)) {
                    LocationSetSet_ForEach(params.srcs, EvalAssignAction2, &params2);
                    if (params2.failed) {
                        LocationSetSet_Term(params.srcs);
                        LocationSetSet_Init(params.srcs);
                        params.result |= EvalExpr(
                            params.srcs, proc, nd->u.monadic, &stCallingContextStack, map, ptf
                        );
                        if (!LocationSetSet_FindUnknown(params.srcs)) {
                            LocationSetSet_ForEach(
                                params.srcs, EvalExprAction2, (void*)&cint64_one
                            );
                        }
                    }
                }
                break;

            default:
                LocationSetSet_Term(params.srcs);
                LocationSetSet_Init(params.srcs);
                LocationSetSet_AddUnknown(params.srcs, nd->rtype, NULL, NULL);
                break;
        }
    } else {
        CError_FATAL(5007);
    }

    if (LocationSetSet_Count(params.srcs) != 0) {
        PointsToFunction* pointsToFunc;

        pointsToFunc = PointsToFunction_New();
        if (nd->pointsToFunction) {
            PointsToFunction_Copy(pointsToFunc, nd->pointsToFunction);
        } else {
            PointsToFunction_Init(pointsToFunc);
        }

        params.singleDst = LocationSetSet_Count(set) == 1;
        LocationSetSet_ForEach(set, EvalAssignAction, &params);

        if (!params.result && params.assignChanged) {
            if (nd->pointsToFunction) {
                params.result = !PointsToFunctions_Equal(pointsToFunc, nd->pointsToFunction);
            } else {
                params.result = PointsToFunction_FindFirst(pointsToFunc) != NULL;
            }
        }

        PointsToFunction_Term(pointsToFunc);
        PointsToFunction_Delete(pointsToFunc);
    }

    LocationSetSet_Term(set);
    LocationSetSet_Term(params.srcs);
    LocationSetSet_Delete(set);
    LocationSetSet_Delete(params.srcs);

    return params.result;
}

static Boolean EvalReturn(
    Object* proc,
    IROLinear* nd,
    IRONode* fnode,
    ParamMappingFunction* map,
    PartialTransferFunction* ptf
) {
    EvalAssignActionParams params;
    LocationSet* loc;

    IRO_ASSERT(5047, proc != NULL);
    IRO_ASSERT(5048, nd != NULL);
    IRO_ASSERT_MSG(5049, nd->type == IROLinearReturn, "nd->lt == Lt_Return");
    IRO_ASSERT(5050, fnode != NULL);
    IRO_ASSERT(5051, map != NULL);
    IRO_ASSERT(5052, ptf != NULL);

    memset(&params, 0, sizeof(params));
    params.proc = proc;
    params.ptf = ptf;
    params.nd = nd;
    params.fnode = fnode;
    params.result = FALSE;
    params.assignChanged = FALSE;

    if (nd->u.monadic) {
        params.srcs = LocationSetSet_New();
        LocationSetSet_Init(params.srcs);

        loc = PartialTransferFunction_returnLocation(ptf);
        params.result |=
            EvalExpr(params.srcs, proc, nd->u.monadic, &stCallingContextStack, map, ptf);

        if (LocationSetSet_Count(params.srcs) != 0) {
            params.singleDst = TRUE;
            EvalAssignAction(loc, &params);
            params.result |= params.assignChanged;
        }

        LocationSetSet_Term(params.srcs);
        LocationSetSet_Delete(params.srcs);
    }

    return params.result;
}

static void ApplySummaryAction2(ParamMapping* mapping, void* refcon) {
    ApplySummaryActionParams* params;
    PAMemoryBlock* block;
    ExtendedParam* ep;
    IROLinear* nd;
    LocationSetSet* set;
    EvalAssignActionParams assignParams;

    IRO_ASSERT(5109, mapping != NULL);
    IRO_ASSERT(5110, refcon != NULL);

    params = refcon;

    IRO_ASSERT(5114, params->tgtMap != NULL);
    IRO_ASSERT(5115, params->proc != NULL);
    IRO_ASSERT(5116, params->fnode != NULL);
    IRO_ASSERT(5117, params->nd != NULL);
    IRO_ASSERT_MSG(5118, params->nd->type == IROLinearFunccall, "params->nd->lt == Lt_Funccall");
    IRO_ASSERT(5119, params->ptf != NULL);
    IRO_ASSERT(5120, params->map != NULL);
    IRO_ASSERT(5121, params->loc != NULL);
    IRO_ASSERT(5122, params->locs != NULL);

    block = LocationSet_block(params->loc);

    IRO_ASSERT(5125, block != NULL);
    IRO_ASSERT(5126, PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_EXTENDEDPARAM);

    ep = PAMemoryBlock_thing(block);

    IRO_ASSERT(5128, ep != NULL);

    if (ParamMapping_extended(mapping) == ep && (nd = ParamMapping_actual(mapping))) {
        set = LocationSetSet_New();
        LocationSetSet_Init(set);
        params->result |=
            EvalExpr(set, params->proc, nd, &stCallingContextStack, params->map, params->ptf);

        if (!LocationSetSet_FindUnknown(set)) {
            CInt64 stride64;
            CInt64 value;

            value = LocationSet_field(params->loc);
            LocationSetSet_ForEach(set, EvalExprAction, &value);

            CInt64_SetULong(&stride64, LocationSet_stride(params->loc));
            LocationSetSet_ForEach(set, EvalExprAction2, &stride64);
        }

        memset(&assignParams, 0, sizeof(assignParams));
        assignParams.proc = params->proc;
        assignParams.ptf = params->ptf;
        assignParams.nd = params->nd;
        assignParams.fnode = params->fnode;
        assignParams.srcs = params->locs;
        assignParams.singleDst = params->singleTarget && (LocationSetSet_Count(set) == 1);
        assignParams.result = FALSE;
        assignParams.assignChanged = FALSE;
        LocationSetSet_ForEach(set, EvalAssignAction, &assignParams);

        LocationSetSet_Term(set);
        LocationSetSet_Delete(set);

        params->result |= assignParams.result;
        params->assignChanged |= assignParams.assignChanged;
    }
}

static void ApplySummaryAction(PointsToEntry* pte, void* refcon) {
    ApplySummaryActionParams* params;
    LocationSet* loc;
    PAMemoryBlock* block;

    IRO_ASSERT(5176, pte != NULL);
    IRO_ASSERT(5177, refcon != NULL);

    params = refcon;

    IRO_ASSERT(5181, params->tgtMap != NULL);
    IRO_ASSERT(5182, params->proc != NULL);
    IRO_ASSERT(5183, params->fnode != NULL);
    IRO_ASSERT(5184, params->nd != NULL);
    IRO_ASSERT_MSG(5185, params->nd->type == IROLinearFunccall, "params->nd->lt == Lt_Funccall");
    IRO_ASSERT(5186, params->ptf != NULL);
    IRO_ASSERT(5187, params->map != NULL);

    loc = PointsToEntry_loc(pte);

    IRO_ASSERT(5190, loc != NULL);
    IRO_ASSERT(5191, !LocationSet_IsUnknown(loc));

    if ((block = LocationSet_block(loc)) &&
        PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
        params->loc = loc;
        params->locs = PointsToEntry_locs(pte);
        ParamMappingFunction_ForEach(params->tgtMap, ApplySummaryAction2, params);
    }
}

// NOTE: invented names: tgtPTF, tgtMap, proc, fnode, nd, ptf, map, singleTarget
static Boolean ApplySummary(
    PartialTransferFunction* tgtPTF,
    ParamMappingFunction* tgtMap,
    Object* proc,
    IRONode* fnode,
    IROLinear* nd,
    PartialTransferFunction* ptf,
    ParamMappingFunction* map,
    Boolean singleTarget
) {
    Boolean result;
    ApplySummaryActionParams params;
    PointsToFunction* pointsToFunc;

    IRO_ASSERT(5209, tgtPTF != NULL);
    IRO_ASSERT(5210, tgtMap != NULL);
    IRO_ASSERT(5211, proc != NULL);
    IRO_ASSERT(5212, fnode != NULL);
    IRO_ASSERT(5213, nd != NULL);
    IRO_ASSERT_MSG(5214, nd->type == IROLinearFunccall, "nd->lt == Lt_Funccall");
    IRO_ASSERT(5215, ptf != NULL);
    IRO_ASSERT(5216, map != NULL);

    StoreReturnedLocations(nd, tgtPTF, tgtMap);
    if (tgtPTF == stUnknownPTF) {
        result = KillAllAddressableLocations(proc, fnode, nd, ptf);
    } else {
        pointsToFunc = PointsToFunction_New();
        if (nd->pointsToFunction) {
            PointsToFunction_Copy(pointsToFunc, nd->pointsToFunction);
        } else {
            PointsToFunction_Init(pointsToFunc);
        }

        memset(&params, 0, sizeof(params));
        params.tgtMap = tgtMap;
        params.proc = proc;
        params.fnode = fnode;
        params.nd = nd;
        params.ptf = ptf;
        params.map = map;
        params.loc = NULL;
        params.locs = NULL;
        params.singleTarget = singleTarget;
        params.result = FALSE;
        params.assignChanged = FALSE;

        PointsToFunction_ForEach(
            PartialTransferFunction_finalPointsToFn(tgtPTF), ApplySummaryAction, &params
        );

        result = params.result;
        if (!result && params.assignChanged) {
            if (nd->pointsToFunction) {
                result = !PointsToFunctions_Equal(pointsToFunc, nd->pointsToFunction);
            } else {
                result = PointsToFunction_FindFirst(pointsToFunc) != NULL;
            }
        }
    }

    return result;
}

static void GetPTFAction2(ParamMapping* mapping, void* refcon) {
    IRO_ASSERT(5332, mapping != NULL);

    if (ParamMapping_extended(mapping)) {
        ParamMapping_SetExtended(mapping, NULL);
    }
}

static void GetPTFAction(PartialTransferFunction* tgtPTF, void* refcon) {
    GetPTFActionParams* params;

    IRO_ASSERT(5360, tgtPTF != NULL);
    IRO_ASSERT(5361, refcon != NULL);

    params = refcon;

    IRO_ASSERT(5365, params->map != NULL);
    IRO_ASSERT(5366, params->proc != NULL);
    IRO_ASSERT(5367, params->proc != &stUnknown);
    IRO_ASSERT(5368, params->nd != NULL);
    IRO_ASSERT(5369, params->ptf != NULL);
    IRO_ASSERT(5370, params->needVisit != NULL);

    if (!params->match) {
        if (MatchPTF(tgtPTF, params->proc, params->map, params->nd, params->ptf)) {
            if (InputsHaveNewPointerValues(tgtPTF, params->ptf)) {
                *params->needVisit = TRUE;
            }
            params->match = tgtPTF;
            return;
        } else {
            ParamMappingFunction_ForEach(params->map, GetPTFAction2, NULL);
            if (PartialTransferFunction_contextNd(tgtPTF) == params->nd &&
                PartialTransferFunction_contextPTF(tgtPTF) == params->ptf) {
                params->contextPTF = tgtPTF;
            }
        }
    }
}

static PartialTransferFunction* GetPTF(
    ParamMappingFunction* map,
    Object* proc,
    IROLinear* nd,
    PartialTransferFunction* ptf,
    Boolean* needVisit
) {
    PartialTransferFunction* result;
    GetPTFActionParams params;

    IRO_ASSERT(5397, map != NULL);
    IRO_ASSERT(5398, proc != NULL);
    IRO_ASSERT(5399, nd != NULL);
    IRO_ASSERT(5400, ptf != NULL);
    IRO_ASSERT(5401, needVisit != NULL);

    if (proc == &stUnknown) {
        result = stUnknownPTF;
    } else {
        memset(&params, 0, sizeof(params));
        params.map = map;
        params.proc = proc;
        params.nd = nd;
        params.ptf = PartialTransferFunction_New();
        PartialTransferFunction_Init(params.ptf, nd, ptf);
        params.needVisit = needVisit;
        params.contextPTF = NULL;
        params.match = NULL;

        if (!proc->u.func.ptfList) {
            proc->u.func.ptfList = PTFList_New();
            PTFList_Init(proc->u.func.ptfList);
        }

        PTFList_ForEach(proc->u.func.ptfList, GetPTFAction, &params);

        result = params.match;
        if (result && !*needVisit) {
            PartialTransferFunction* copy = PartialTransferFunction_New();
            PartialTransferFunction_Copy(copy, result);
            result = copy;
        } else {
            result = stUnknownPTF;
        }

        PartialTransferFunction_Term(params.ptf);
        PartialTransferFunction_Delete(params.ptf);
    }

    return result;
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
static Boolean IsMeetNode(IRONode* fnode, IROLinear* nd) {
    return (fnode->numpred > 1) && (fnode->first == nd);
}

static Boolean IsExitNode(Object* proc, IRONode* fnode) {
    IRO_ASSERT(5468, proc != NULL);
    IRO_ASSERT(5469, fnode != NULL);

    return (fnode->numsucc == 0) && Bv_IsBitSet(FunctionFirstNode(proc)->index, fnode->dom);
}

static Boolean SomePredecessorHasBeenVisited(Object* proc, IRONode* fnode) {
    UInt16 i;

    IRO_ASSERT(5480, proc != NULL);
    IRO_ASSERT(5481, fnode != NULL);

    for (i = 0; i < fnode->numpred; i++) {
        if (FunctionNodeTable(proc)[fnode->pred[i]]->marked) {
            return TRUE;
        }
    }

    return FALSE;
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
static Boolean AllPredecessorsHaveBeenVisited(Object* proc, IRONode* fnode) {
    UInt16 i;

    IRO_ASSERT(0, proc != NULL);
    IRO_ASSERT(0, fnode != NULL);

    for (i = 0; i < fnode->numpred; i++) {
        if (!FunctionNodeTable(proc)[fnode->pred[i]]->marked) {
            return FALSE;
        }
    }

    return TRUE;
}

static void EvalProcAction2(PointsToEntry* pte, void* refcon) {
    EvalProcActionParams* params;
    LocationSet* dst;
    PAMemoryBlock* block;
    LocationSetSet* set;
    IRONode* node;

    IRO_ASSERT(5526, pte != NULL);
    IRO_ASSERT(5527, refcon != NULL);

    params = refcon;

    IRO_ASSERT(5531, params->proc != NULL);
    IRO_ASSERT(5532, params->fnode != NULL);
    IRO_ASSERT(5533, params->ptf != NULL);

    dst = PointsToEntry_loc(pte);

    IRO_ASSERT(5536, dst != NULL);
    IRO_ASSERT(5537, !LocationSet_IsUnknown(dst));

    if ((block = LocationSet_block(dst)) &&
        (LocationSet_IsLocalVarBase(dst) ||
         PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_EXTENDEDPARAM)) {
        set = LocationSetSet_New();
        LocationSetSet_Init(set);
        LocationSetSet_AddSet(set, PointsToEntry_locs(pte));

        for (node = FunctionFirstNode(params->proc); node; node = node->nextnode) {
            if (node->marked && node != params->fnode && IsExitNode(params->proc, node)) {
                if (node->last->pointsToFunction) {
                    Lookup(
                        set,
                        &stCallingContextStack,
                        params->proc,
                        NULL,
                        params->ptf,
                        dst,
                        node->last->pointsToFunction,
                        FALSE,
                        NULL
                    );
                }
            }
        }

        Assign(params->ptf, dst, set, params->proc, NULL, NULL);

        LocationSetSet_Term(set);
        LocationSetSet_Delete(set);
    }
}

static void AssignEachInPointsToFunctionAction(PointsToEntry* pte, void* refcon) {
    AssignEachInPointsToFunctionActionParams* params;
    LocationSet* dst;
    LocationSetSet* srcs;

    IRO_ASSERT(5578, pte != NULL);
    IRO_ASSERT(5579, refcon != NULL);

    params = refcon;

    IRO_ASSERT(5583, params->proc != NULL);
    IRO_ASSERT(5584, params->nd != NULL);
    IRO_ASSERT(5585, params->fnode != NULL);
    IRO_ASSERT(5586, params->ptf != NULL);

    dst = PointsToEntry_loc(pte);

    srcs = LocationSetSet_New();
    LocationSetSet_Init(srcs);
    LocationSetSet_AddSet(srcs, PointsToEntry_locs(pte));
    params->result |= Assign(params->ptf, dst, srcs, params->proc, params->nd, params->fnode);
    LocationSetSet_Term(srcs);
    LocationSetSet_Delete(srcs);
}

static void AssignEachInPointsToFunction(PointsToFunction* pointsTo, void* refcon) {
    AssignEachInPointsToFunctionActionParams* params;
    PointsToFunction* pointsToFunc;

    IRO_ASSERT(5603, pointsTo != NULL);
    IRO_ASSERT(5604, refcon != NULL);

    params = refcon;

    IRO_ASSERT(5608, params->nd != NULL);

    pointsToFunc = PointsToFunction_New();
    if (params->nd->pointsToFunction) {
        PointsToFunction_Copy(pointsToFunc, params->nd->pointsToFunction);
    } else {
        PointsToFunction_Init(pointsToFunc);
    }

    if (PointsToFunction_FindFirst(pointsToFunc)) {
        PointsToFunction_ForEach(pointsTo, AssignEachInPointsToFunctionAction, refcon);
    } else {
        if (!params->nd->pointsToFunction) {
            params->nd->pointsToFunction = PointsToFunction_New();
        } else {
            PointsToFunction_Term(params->nd->pointsToFunction);
        }

        PointsToFunction_Copy(params->nd->pointsToFunction, pointsTo);
        params->result = TRUE;
    }

    if (params->result) {
        if (params->nd->pointsToFunction) {
            params->result = !PointsToFunctions_Equal(pointsToFunc, params->nd->pointsToFunction);
        } else {
            params->result = PointsToFunction_FindFirst(pointsToFunc) != NULL;
        }
    }

    PointsToFunction_Term(pointsToFunc);
    PointsToFunction_Delete(pointsToFunc);
}

static Boolean ObjectIsAFunctionArgument(Object* proc, Object* obj) {
    ObjectList* list;

    IRO_ASSERT(5644, proc != NULL);
    IRO_ASSERT(5645, proc != &stUnknown);
    IRO_ASSERT(5646, obj != NULL);

    if (obj->datatype == DLOCAL) {
        for (list = FunctionArguments(proc); list; list = list->next) {
            if (obj == list->object) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static Boolean ObjectIsARealFunctionArgument(Object* proc, Object* obj) {
    ObjectList* list;

    IRO_ASSERT(5662, proc != NULL);
    IRO_ASSERT(5663, proc != &stUnknown);
    IRO_ASSERT(5664, obj != NULL);

    if (obj->datatype == DLOCAL && proc == cscope_currentfunc) {
        for (list = arguments; list; list = list->next) {
            if (obj == list->object) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static void AddLocalVarsAddressedByExceptionUses(Object* var) {
    IRO_ASSERT(5700, var != NULL);
    IRO_ASSERT(5701, stExceptionFNode != NULL);
    IRO_ASSERT(5702, stExceptionFNode->addressed != NULL);

    if (var->datatype == DLOCAL) {
        ObjectSet_Add(stExceptionFNode->addressed, var);
    }
}

static Boolean LinearNodeIsInFlowgraphNode(IROLinear* nd, IRONode* fnode) {
    IROLinear* first;
    IROLinear* last;
    IROLinear* scan;

    if (fnode && (first = fnode->first) && (last = fnode->last)) {
        for (scan = first; scan && scan != last->next; scan = scan->next) {
            if (scan == nd) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

// NOTE: invented names: Int, isFirst
static void EvalProcAction(IROLinear* Int, Boolean isFirst) {
#if VERSION < VERSION_GC_2_5
    // 2.0 declares the same locals in another order
    ParamMappingFunction* map;
    Boolean result;
    Object* proc;
    PartialTransferFunction* ptf;
    IRONode* fnode;
    PointsToFunction* pointsToFunc;
    Boolean* changed;
    Boolean needMeet;
    Boolean nodeChanged;
#else
    Boolean* changed;
    Boolean result;
    Object* proc;
    PartialTransferFunction* ptf;
    Boolean needMeet;
    Boolean nodeChanged;
    IRONode* fnode;
    PointsToFunction* pointsToFunc;
    ParamMappingFunction* map;
#endif
    Object* obj;
    AssignEachInPointsToFunctionActionParams params;

    static int userBreakCounter;

    if (!isFirst && !Int->evaluated) {
        IRO_ASSERT(5749, Int != NULL);
        IRO_ASSERT(5750, stEvalProcActionParams.changed != NULL);

        proc = stEvalProcActionParams.proc;
        fnode = stEvalProcActionParams.fnode;
        map = stEvalProcActionParams.map;
        ptf = stEvalProcActionParams.ptf;
        pointsToFunc = stEvalProcActionParams.pointsToFunc;
        changed = stEvalProcActionParams.changed;
        needMeet = stEvalProcActionParams.needMeet;
        nodeChanged = stEvalProcActionParams.nodeChanged;

        IRO_ASSERT(5761, proc != NULL);
        IRO_ASSERT(5762, fnode != NULL);
        IRO_ASSERT(5763, map != NULL);
        IRO_ASSERT(5764, ptf != NULL);

        if (++userBreakCounter > 40) {
            IRO_CheckForUserBreak();
            userBreakCounter = 0;
        }

        result = FALSE;

        if (nodeChanged && Int->pointsToFunction) {
            PointsToFunction_Term(Int->pointsToFunction);
            PointsToFunction_Delete(Int->pointsToFunction);
            Int->pointsToFunction = NULL;
        }

        memset(&params, 0, sizeof(params));
        params.proc = proc;
        params.nd = Int;
        params.fnode = fnode;
        params.ptf = ptf;
        params.result = FALSE;

        if (needMeet) {
            PointsToFunction* initial = PartialTransferFunction_initialPointsToFn(ptf);
            if (fnode == FunctionFirstNode(proc) && initial) {
                AssignEachInPointsToFunction(initial, &params);
                result |= params.result;
            }
            result |= EvalMeet(proc, fnode, Int, ptf);
            pointsToFunc = Int->pointsToFunction;
            needMeet = FALSE;
        } else if (!Int->pointsToFunction) {
            if (pointsToFunc) {
                AssignEachInPointsToFunction(pointsToFunc, &params);
            }
            result |= params.result;
            pointsToFunc = Int->pointsToFunction;
        }

        if (IRO_IsAssignment(Int)) {
            if (Int->flags & IROLF_InCallArg) {
                longjmp(stAbortPointerAnalysis, 1);
            }
            result |= EvalAssign(proc, Int, fnode, map, ptf);
            pointsToFunc = Int->pointsToFunction;
        } else if (Int->type == IROLinearReturn) {
            result |= EvalReturn(proc, Int, fnode, map, ptf);
            pointsToFunc = Int->pointsToFunction;
        } else if (Int->type == IROLinearFunccall) {
            if (Int->flags & IROLF_InCallArg) {
                longjmp(stAbortPointerAnalysis, 1);
            }
            if (Int->stmt && IRO_FunctionCallMightThrowException(Int)) {
                stExceptionFNode = fnode;
                IRO_WalkExcActions(Int->stmt->dobjstack, AddLocalVarsAddressedByExceptionUses);
            }
            result |= EvalCall(proc, fnode, Int, map, ptf);
            pointsToFunc = Int->pointsToFunction;
        } else if (Int->type == IROLinearOp1Arg &&
                   Int->nodetype == EINDIRECT &&
                   (!(Int->flags & IROLF_Assigned) || (Int->flags & IROLF_Used))) {
            result |= EvalExpr(NULL, proc, Int, &stCallingContextStack, map, ptf);
        } else if (Int->type == IROLinearOperand &&
                   !(Int->flags & IROLF_Ind) &&
                   Int->u.node->type == EOBJREF &&
                   (obj = Int->u.node->data.objref) &&
                   obj->datatype == DLOCAL) {
            ObjectSet_Add(fnode->addressed, obj);
        } else if (Int->type == IROLinearAsm) {
#if VERSION < VERSION_GC_2_5
            // 2.0 analyses an asm statement: the locals whose address it takes are addressed
            IAEffects effects;
            int i;
            CodeGen_GetAsmEffects(Int->u.asm_stmt, &effects);
            for (i = 0; i < effects.numoperands; i++) {
                obj = effects.operands[i].object;
                if (obj &&
                    obj->datatype == DLOCAL &&
                    effects.operands[i].type == IAEffect_AddressOf) {
                    ObjectSet_Add(fnode->addressed, obj);
                }
            }
#else
            longjmp(stAbortPointerAnalysis, 1);
#endif
        }

        if (result && Int != fnode->last && fnode->last->pointsToFunction) {
            PointsToFunction_Term(fnode->last->pointsToFunction);
            PointsToFunction_Delete(fnode->last->pointsToFunction);
            fnode->last->pointsToFunction = NULL;
        }

        *changed |= result;
        nodeChanged |= result;

        stEvalProcActionParams.pointsToFunc = pointsToFunc;
        stEvalProcActionParams.needMeet = needMeet;
        stEvalProcActionParams.nodeChanged = nodeChanged;

        Int->evaluated = TRUE;

        if (Int->type != IROLinearReturn) {
            IROLinear* father = IRO_LocateFather(Int);
            if (father && father->type == IROLinearReturn) {
                if (LinearNodeIsInFlowgraphNode(father, fnode)) {
                    EvalProcAction(father, FALSE);
                } else {
                    longjmp(stAbortPointerAnalysis, 1);
                }
            }
        }
    }
}

#if VERSION < VERSION_GC_2_5
// 2.0's file has 9 lines more above here
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT 9
#endif
static void EvalProc(Object* proc, ParamMappingFunction* map, PartialTransferFunction* ptf) {
    IRONode* fnode;
    IRONode* pred;
    IROLinear* nd;
    UInt32 passCount;
    UInt16 i;
    AssignEachInPointsToFunctionActionParams assignParams;
    EvalProcActionParams params;
    Boolean changed;

    IRO_ASSERT(5955, proc != NULL);
    IRO_ASSERT(5956, map != NULL);
    IRO_ASSERT(5957, ptf != NULL);

    for (fnode = FunctionFirstNode(proc); fnode; fnode = fnode->nextnode) {
        fnode->marked = FALSE;
    }

    passCount = 0;
    do {
        clock();
        changed = FALSE;
        for (fnode = FunctionFirstNode(proc); fnode; fnode = fnode->nextnode) {
            if (fnode->last &&
                ((fnode->numpred == 0) || SomePredecessorHasBeenVisited(proc, fnode))) {
                clock();
                if (!fnode->addressed) {
                    fnode->addressed = ObjectSet_New();
                    ObjectSet_Init(fnode->addressed);
                }

                for (i = 0; i < fnode->numpred; i++) {
                    pred = FunctionNodeTable(proc)[fnode->pred[i]];
                    if (pred->addressed) {
                        ObjectSet_AddSet(fnode->addressed, pred->addressed);
                    }
                }

                memset(&stEvalProcActionParams, 0, sizeof(stEvalProcActionParams));
                stEvalProcActionParams.proc = proc;
                stEvalProcActionParams.fnode = fnode;
                stEvalProcActionParams.map = map;
                stEvalProcActionParams.ptf = ptf;
                stEvalProcActionParams.pointsToFunc = NULL;
                stEvalProcActionParams.changed = &changed;
                stEvalProcActionParams.needMeet = TRUE;
                stEvalProcActionParams.nodeChanged = FALSE;

                for (nd = fnode->first; nd && nd != fnode->last->next; nd = nd->next) {
                    nd->evaluated = FALSE;
                }
                IRO_WalkInts(fnode->first, fnode->last, (IROWalkTreeFunc)EvalProcAction);

                if (stEvalProcActionParams.needMeet || !fnode->last->pointsToFunction) {
                    memset(&assignParams, 0, sizeof(assignParams));
                    assignParams.proc = proc;
                    assignParams.nd = fnode->last;
                    assignParams.fnode = fnode;
                    assignParams.ptf = ptf;
                    assignParams.result = FALSE;
                    if (stEvalProcActionParams.needMeet) {
                        PointsToFunction* initial = PartialTransferFunction_initialPointsToFn(ptf);
                        if (fnode == FunctionFirstNode(proc) && initial) {
                            AssignEachInPointsToFunction(initial, &assignParams);
                            changed |= assignParams.result;
                        }
                        changed |= EvalMeet(proc, fnode, fnode->last, ptf);
                        stEvalProcActionParams.needMeet = FALSE;
                    } else {
                        if (stEvalProcActionParams.pointsToFunc) {
                            AssignEachInPointsToFunction(
                                stEvalProcActionParams.pointsToFunc, &assignParams
                            );
                        }
                        changed |= assignParams.result;
                    }
                }

                fnode->marked = TRUE;
                clock();
            }
        }

        clock();
        if (++passCount > 32) {
            CError_FATAL(6063);
        }
    } while (changed);

    if (passCount > stMaxPassCount) {
        stMaxPassCount = passCount;
    }

    PartialTransferFunction_SetAnalyzed(ptf, TRUE);

    memset(&params, 0, sizeof(params));
    params.proc = proc;
    params.ptf = ptf;
    for (fnode = FunctionFirstNode(proc); fnode; fnode = fnode->nextnode) {
        if (fnode->marked && IsExitNode(proc, fnode) && fnode->last->pointsToFunction) {
            params.fnode = fnode;
            PointsToFunction_ForEach(fnode->last->pointsToFunction, EvalProcAction2, &params);
        }
    }
}

static void PointerAnalysis_Init(void) {
    stCallingContextStack = Stack_New();
    Stack_Init(stCallingContextStack);

    stUnknownPTF = PartialTransferFunction_New();
    PartialTransferFunction_Init(stUnknownPTF, NULL, NULL);

    stExtParamSet = ExtendedParamSet_New();
    ExtendedParamSet_Init(stExtParamSet);

    stPTFList = PTFList_New();
    PTFList_Init(stPTFList);
}

static void CleanseLocationSet(LocationSet* loc, void* refcon) {
    PAMemoryBlock* block;
    PALocalVar* local;

    IRO_ASSERT(6152, loc != NULL);
    IRO_ASSERT(6153, refcon == NULL);

    if (!LocationSet_IsUnknown(loc) &&
        (block = LocationSet_block(loc)) &&
        PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_LOCALVAR &&
        (local = PAMemoryBlock_thing(block))) {
        PALocalVar_SetObject(local, NULL);
    }
}

static void CleansePointsToEntry(PointsToEntry* pte, void* refcon) {
    IRO_ASSERT(6168, pte != NULL);
    IRO_ASSERT(6169, refcon == NULL);

    CleanseLocationSet(PointsToEntry_loc(pte), NULL);
    LocationSetSet_ForEach(PointsToEntry_locs(pte), CleanseLocationSet, NULL);
}

static void PointerAnalysis_TermAction4(PartialTransferFunction* ptf, void* refcon) {
    IRO_ASSERT(6178, ptf != NULL);
    IRO_ASSERT(6179, refcon == NULL);

    if (ptf != stUnknownPTF) {
        PointsToFunction_ForEach(
            PartialTransferFunction_initialPointsToFn(ptf), CleansePointsToEntry, NULL
        );
        PointsToFunction_ForEach(
            PartialTransferFunction_finalPointsToFn(ptf), CleansePointsToEntry, NULL
        );
    }
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
static void PointerAnalysis_TermAction3(ExtendedParam* ep) {
    ExtendedParam_Term(ep);
    ExtendedParam_Delete(ep);
}

static void PointerAnalysis_TermAction2(Object* obj, void* refcon) {
    ObjectSet* objSet;
    Object* proc;
    ObjectList* list;

    objSet = refcon;

    if (!ObjectIsAnExtendedParamCandidate(obj)) {
        if (obj->datatype == DLOCAL &&
            obj->u.var.info &&
            (proc = obj->u.var.info->func) &&
            ObjectIsAFunction(proc) &&
            proc->u.func.argList) {
            for (list = proc->u.func.argList; list; list = list->next) {
                if (obj == list->object) {
                    break;
                }
            }

            if (!list) {
                ObjectSet_Add(objSet, obj);
            }
        } else {
            ObjectSet_Add(objSet, obj);
        }
    }
}

static void PointerAnalysis_TermAction1(ExtendedParam* ep, void* refcon) {
    ObjectSet* objSet;
    ObjectSet* epObjSet;
    Object* obj;

    objSet = ObjectSet_New();
    ObjectSet_Init(objSet);

    epObjSet = ExtendedParam_objectSet(ep);
    obj = NULL;
    ObjectSet_ForEach(epObjSet, FindGlobalObjectAction, &obj);
    if (obj) {
        ObjectSet_ForEach(epObjSet, PointerAnalysis_TermAction2, objSet);
        if (ObjectSet_FindFirst(objSet)) {
            ExtendedParam_RemoveObjects(ep, objSet);
        }
    }

    ObjectSet_Term(objSet);
    ObjectSet_Delete(objSet);
}

static void PointerAnalysis_Term(void) {
    if (stExtParamSet) {
        ExtendedParamSet_ForEach(stExtParamSet, PointerAnalysis_TermAction1, NULL);
        ExtendedParamSet_Term(stExtParamSet);
        ExtendedParamSet_Delete(stExtParamSet);
        stExtParamSet = NULL;
    }

    if (stPTFList) {
        PTFList_ForEach(stPTFList, PointerAnalysis_TermAction4, NULL);
        PTFList_Term(stPTFList);
        PTFList_Delete(stPTFList);
        stPTFList = NULL;
    }

    PartialTransferFunction_Term(stUnknownPTF);
    PartialTransferFunction_Delete(stUnknownPTF);
    stUnknownPTF = NULL;

    Stack_Term(&stCallingContextStack);
    Stack_Delete(stCallingContextStack);
    stCallingContextStack = NULL;
}

static void InvalidatePointsToFunctions(Object* proc) {
    IRONode* fnode;
    IROLinear* nd;

    IRO_ASSERT(6293, proc != NULL);

    for (fnode = FunctionFirstNode(proc); fnode; fnode = fnode->nextnode) {
        if (fnode->last) {
            if (fnode->addressed) {
                ObjectSet_Term(fnode->addressed);
                ObjectSet_Delete(fnode->addressed);
                fnode->addressed = NULL;
            }

            for (nd = fnode->first; nd && nd != fnode->last->next; nd = nd->next) {
                if (nd->pointsToFunction) {
                    PointsToFunction_Term(nd->pointsToFunction);
                    PointsToFunction_Delete(nd->pointsToFunction);
                    nd->pointsToFunction = NULL;
                }
            }
        }
    }
}

static void InitialSetup(void) {}

static void PointerAnalysis_HeapErrorProc(void) {
    longjmp(stAbortPointerAnalysis, 2);
}

void PointerAnalysis_Setup(void) {
    IRO_InitializeAllocator();
    stExtendedParamNum = 0;
    stParamObjs = NULL;
    stMaxPassCount = 0;
}

void PointerAnalysis_Cleanup(void) {
    ObjectList* list;
    ObjectList* next;

    for (list = stParamObjs; list; list = next) {
        IRO_free(list->object);
        next = list->next;
        IRO_free(list);
    }
    stParamObjs = NULL;
    IRO_TerminateAllocator();
}

void IRO_AnalyzePointers(Object* function) {
    EvalCallActionParams params;
    IROLinear nd;
    IRONode fnode;
    int code;
    void (*volatile saveheaperror)();

    IRO_ASSERT(6384, function != NULL);

    PointerAnalysis_Init();

    memset(&params, 0, sizeof(params));
    memset(&nd, 0, sizeof(nd));
    nd.type = IROLinearFunccall;
    memset(&fnode, 0, sizeof(fnode));
    params.proc = &stUnknown;
    params.fnode = &fnode;
    params.nd = &nd;
    params.ptf = stUnknownPTF;
    params.map = NULL;
    params.stop = FALSE;
    params.result = FALSE;
    params.visitTarget = TRUE;

    stCurrentProc = FindMainEntryPoint(function);

    if ((code = setjmp(stAbortPointerAnalysis)) == 0) {
        saveheaperror = getheaperror();
        setheaperror(PointerAnalysis_HeapErrorProc);
        InitialSetup();
        EvalCallAction(stCurrentProc, &params);
        PointerAnalysis_Term();
        stCurrentProc = NULL;
        setheaperror(saveheaperror);
    } else {
        setheaperror(saveheaperror);
        InvalidatePointsToFunctions(stCurrentProc);
        PointerAnalysis_Term();
        stCurrentProc = NULL;
        if (code == 2 && saveheaperror) {
            saveheaperror();
        }
    }
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
static void RemoveRestrictedExtendedParamsAction(LocationSet* ls, void* refcon) {
    LocationSetSet* locs;
    PAMemoryBlock* block;
    ExtendedParam* ep;
    ObjectSet* objSet;
    Object* obj;

    locs = refcon;

    if (!LocationSet_IsUnknown(ls) &&
        (block = LocationSet_block(ls)) &&
        PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_EXTENDEDPARAM &&
        (ep = PAMemoryBlock_thing(block))) {
        objSet = ExtendedParam_objectSet(ep);
        if (ObjectSet_Count(objSet) == 1 &&
            (obj = ObjectSet_FindFirst(objSet)) &&
            ObjectIsRestrictQualified(obj)) {
            LocationSetSet_Add(locs, ls);
        }
    }
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
static void RemoveRestrictedExtendedParams(LocationSetSet* locs) {
    LocationSetSet* set;

    set = LocationSetSet_New();
    LocationSetSet_Init(set);
    LocationSetSet_ForEach(locs, RemoveRestrictedExtendedParamsAction, set);
    LocationSetSet_RemoveSet(locs, set);
    LocationSetSet_Term(set);
    LocationSetSet_Delete(set);
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
Boolean PointerAnalysis_TwoLinearNodePointerExprsMightAlias(
    Object* proc, IROLinear* nd1, IROLinear* nd2
) {
    LocationSetSet* lss1;
    LocationSetSet* lss2;
    PointsToFunction* pointsTo1;
    PointsToFunction* pointsTo2;
    PointsToFunction* savePointsTo1;
    PointsToFunction* savePointsTo2;
    FindAliasingParams params;

    pointsTo1 = nd1->pointsToFunction;
    pointsTo2 = nd2->pointsToFunction;
    if (!pointsTo1) {
        pointsTo1 = pointsTo2;
    }
    if (!pointsTo2) {
        pointsTo2 = pointsTo1;
    }

    if (copts.opt_pointer_analysis_mode == 2 && !is_typeequal(nd1->rtype, nd2->rtype)) {
        return FALSE;
    }
    if (!pointsTo1 || !pointsTo2) {
        return TRUE;
    }

    lss1 = LocationSetSet_New();
    LocationSetSet_Init(lss1);
    lss2 = LocationSetSet_New();
    LocationSetSet_Init(lss2);

    savePointsTo1 = nd1->pointsToFunction;
    nd1->pointsToFunction = pointsTo1;
    EvalExpr(lss1, proc, nd1, NULL, NULL, NULL);
    nd1->pointsToFunction = savePointsTo1;

    savePointsTo2 = nd2->pointsToFunction;
    nd2->pointsToFunction = pointsTo2;
    EvalExpr(lss2, proc, nd2, NULL, NULL, NULL);
    nd2->pointsToFunction = savePointsTo2;

    memset(&params, 0, sizeof(params));
    params.aliased = FALSE;
    params.set = lss2;
    LocationSetSet_ForEach(lss1, FindAliasingAction, &params);

    if (!params.aliased) {
        params.set = lss1;
        LocationSetSet_ForEach(lss2, FindAliasingAction, &params);

        if (!params.aliased) {
            RemoveRestrictedExtendedParams(lss1);
            RemoveRestrictedExtendedParams(lss2);
            ExpandLocationSetSetToActuals(&stCallingContextStack, NULL, lss1);
            ExpandLocationSetSetToActuals(&stCallingContextStack, NULL, lss2);

            params.set = lss2;
            LocationSetSet_ForEach(lss1, FindAliasingAction, &params);

            if (!params.aliased) {
                params.set = lss1;
                LocationSetSet_ForEach(lss2, FindAliasingAction, &params);
            }
        }
    }

    LocationSetSet_Term(lss1);
    LocationSetSet_Delete(lss1);
    LocationSetSet_Term(lss2);
    LocationSetSet_Delete(lss2);

    return params.aliased;
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
Boolean PointerAnalysis_TwoENodePointerExprsMightAlias(Object* proc, ENode* nd1, ENode* nd2) {
    LocationSetSet* lss1;
    LocationSetSet* lss2;
    PointsToFunction* pointsTo1;
    PointsToFunction* pointsTo2;
    PointsToFunction* savePointsTo1;
    PointsToFunction* savePointsTo2;
    FindAliasingParams params;

    pointsTo1 = nd1->pointsTo;
    pointsTo2 = nd2->pointsTo;
    if (!pointsTo1) {
        pointsTo1 = pointsTo2;
    }
    if (!pointsTo2) {
        pointsTo2 = pointsTo1;
    }

    if (copts.opt_pointer_analysis_mode == 2 && !is_typeequal(nd1->rtype, nd2->rtype)) {
        return FALSE;
    }
    if (!pointsTo1 || !pointsTo2) {
        return TRUE;
    }

    lss1 = LocationSetSet_New();
    LocationSetSet_Init(lss1);
    lss2 = LocationSetSet_New();
    LocationSetSet_Init(lss2);

    savePointsTo1 = nd1->pointsTo;
    nd1->pointsTo = pointsTo1;
    EvalENodeExpr(lss1, proc, nd1, NULL, NULL, NULL);
    nd1->pointsTo = savePointsTo1;

    savePointsTo2 = nd2->pointsTo;
    nd2->pointsTo = pointsTo2;
    EvalENodeExpr(lss2, proc, nd2, NULL, NULL, NULL);
    nd2->pointsTo = savePointsTo2;

    memset(&params, 0, sizeof(params));
    params.aliased = FALSE;
    params.set = lss2;
    LocationSetSet_ForEach(lss1, FindAliasingAction, &params);

    if (!params.aliased) {
        params.set = lss1;
        LocationSetSet_ForEach(lss2, FindAliasingAction, &params);

        if (!params.aliased) {
            RemoveRestrictedExtendedParams(lss1);
            RemoveRestrictedExtendedParams(lss2);
            ExpandLocationSetSetToActuals(&stCallingContextStack, NULL, lss1);
            ExpandLocationSetSetToActuals(&stCallingContextStack, NULL, lss2);

            params.set = lss2;
            LocationSetSet_ForEach(lss1, FindAliasingAction, &params);

            if (!params.aliased) {
                params.set = lss1;
                LocationSetSet_ForEach(lss2, FindAliasingAction, &params);
            }
        }
    }

    LocationSetSet_Term(lss1);
    LocationSetSet_Delete(lss1);
    LocationSetSet_Term(lss2);
    LocationSetSet_Delete(lss2);

    return params.aliased;
}

Boolean PointerAnalysis_IsLinearNodePointerExprDefinite(Object* proc, IROLinear* nd) {
    LocationSetSet* lss;
    LocationSet* loc;
    Boolean result;

    if (!nd->pointsToFunction) {
        return FALSE;
    }

    lss = LocationSetSet_New();
    LocationSetSet_Init(lss);
    EvalExpr(lss, proc, nd, NULL, NULL, NULL);

    result = (LocationSetSet_Count(lss) == 1) &&
        (loc = LocationSetSet_FindFirst(lss)) &&
        LocationSetRepresentsSingleLocation(loc, NULL, NULL);

    LocationSetSet_Term(lss);
    LocationSetSet_Delete(lss);

    return result;
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
Boolean PointerAnalysis_IsENodePointerExprDefinite(Object* proc, ENode* nd) {
    LocationSetSet* lss;
    LocationSet* loc;
    Boolean result;

    if (!nd->pointsTo) {
        return FALSE;
    }

    lss = LocationSetSet_New();
    LocationSetSet_Init(lss);
    EvalENodeExpr(lss, proc, nd, NULL, NULL, NULL);

    result = (LocationSetSet_Count(lss) == 1) &&
        (loc = LocationSetSet_FindFirst(lss)) &&
        LocationSetRepresentsSingleLocation(loc, NULL, NULL);

    LocationSetSet_Term(lss);
    LocationSetSet_Delete(lss);

    return result;
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
Boolean PointerAnalysis_IsVariableValueDefinite(
    Object* proc, VarRecord* var, PointsToFunction* pointsTo
) {
    LocationSetSet* lss;
    LocationSet* loc;
    Boolean result;

    if (!pointsTo) {
        return FALSE;
    }

    lss = LocationSetSet_New();
    LocationSetSet_Init(lss);
    EvalVariable(lss, proc, var, pointsTo, NULL, NULL, NULL);

    result = (LocationSetSet_Count(lss) == 1) &&
        (loc = LocationSetSet_FindFirst(lss)) &&
        LocationSetRepresentsSingleLocation(loc, NULL, NULL);

    LocationSetSet_Term(lss);
    LocationSetSet_Delete(lss);

    return result;
}

static void FindGlobalObjectAction(Object* object, void* refcon) {
    if (ObjectIsAnExtendedParamCandidate(object)) {
        Object** ptr = refcon;
        *ptr = object;
    }
}

static void GetDefiniteObjectOfExtendedParamLoc(
    LocationSet* loc, Object** resultObj, CInt64* resultField
) {
    Object* obj;
    CInt64 field;
    PAMemoryBlock* block;
    ExtendedParam* ep;
    ObjectSet* objSet;
    LocationSetSet* locs;
    LocationSet* first; // NOTE: invented name
    PALocalVar* local;

    IRO_ASSERT(6754, loc != NULL);
    IRO_ASSERT(6755, resultObj != NULL);
    IRO_ASSERT(6756, resultField != NULL);

    obj = NULL;
    field = LocationSet_field(loc);
    block = LocationSet_block(loc);

    if (block && PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
        if ((ep = PAMemoryBlock_thing(block)) && (objSet = ExtendedParam_objectSet(ep))) {
            IRO_ASSERT(6768, obj == NULL);
            ObjectSet_ForEach(objSet, FindGlobalObjectAction, &obj);
        }
    }

    if (!obj) {
        locs = LocationSetSet_New();
        LocationSetSet_Init(locs);
        GetActualLocsOfExtendedParam(locs, loc, NULL, &stCallingContextStack, NULL, FALSE);

        if (LocationSetSet_Count(locs) == 1 &&
            (first = LocationSetSet_FindFirst(locs)) &&
            !LocationSet_IsUnknown(first)) {
            field = CInt64_Add(field, LocationSet_field(first));
            if ((block = LocationSet_block(first)) &&
                PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_EXTENDEDPARAM &&
                (ep = PAMemoryBlock_thing(block)) &&
                (objSet = ExtendedParam_objectSet(ep))) {
                IRO_ASSERT(6792, obj == NULL);
                ObjectSet_ForEach(objSet, FindGlobalObjectAction, &obj);
            } else if ((block = LocationSet_block(first)) &&
                       PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_LOCALVAR &&
                       (local = PAMemoryBlock_thing(block))) {
                obj = GetLocalObject(local, &stUnknown, FALSE);
            }
        }

        LocationSetSet_Term(locs);
        LocationSetSet_Delete(locs);
    }

    *resultObj = obj;
    *resultField = field;
}

// NOTE: invented names: offsetNd, sumNd
static void CreateExpressionForLocationSet(
    LocationSet* loc, IROList* list, Type* rtype, Object* proc
) {
    CInt64 field;
    PAMemoryBlock* block;
    PAMemoryBlockKind kind;
    void* thing;
    IROLinear* nd;
    Object* obj;

    IRO_ASSERT(6824, loc != NULL);
    IRO_ASSERT(6825, !LocationSet_IsUnknown(loc));
    IRO_ASSERT(6826, LocationSet_stride(loc) == 0);
    IRO_ASSERT(6827, list != NULL);
    IRO_ASSERT(6828, rtype != NULL);
    IRO_ASSERT(6829, proc != NULL);

    field = LocationSet_field(loc);
    block = LocationSet_block(loc);
    kind = PAMemoryBlock_kind(block);
    thing = PAMemoryBlock_thing(block);
    nd = NULL;

    switch (kind) {
        case PAMEMORYBLOCKKIND_EXTENDEDPARAM:
            GetDefiniteObjectOfExtendedParamLoc(loc, &obj, &field);
            if (obj) {
                nd = IRO_NewLinear(IROLinearOperand);
                nd->u.node = create_objectrefnode(obj);
                nd->rtype = rtype;
                nd->index = ++IRO_NumLinear;
                IRO_AddToList(nd, list);
            }
            break;
        case PAMEMORYBLOCKKIND_LOCALVAR:
            obj = GetLocalObject(thing, proc, FALSE);
            if (obj) {
                if (ObjectIsAFunctionArgument(proc, obj) && obj->u.var.realObj) {
                    obj = obj->u.var.realObj;
                }
                nd = IRO_NewLinear(IROLinearOperand);
                nd->u.node = create_objectrefnode(obj);
                nd->rtype = rtype;
                nd->index = ++IRO_NumLinear;
                IRO_AddToList(nd, list);
            }
            break;
        case PAMEMORYBLOCKKIND_INT:
            if (IS_TYPE_INT(rtype)) {
                nd = IRO_NewIntConst(*((CInt64*)thing), rtype);
                IRO_AddToList(nd, list);
            }
            break;
        case PAMEMORYBLOCKKIND_OTHER:
            break;
        default:
            CError_FATAL(6885);
    }

    if (nd && !CInt64_IsZero(&field)) {
        IROLinear* offsetNd;
        IROLinear* sumNd;

        offsetNd = IRO_NewIntConst(field, (Type*)&stunsignedlong);
        IRO_AddToList(offsetNd, list);

        sumNd = IRO_NewLinear(IROLinearOp2Arg);
        sumNd->nodetype = EADD;
        sumNd->index = ++IRO_NumLinear;
        sumNd->rtype = rtype;
        sumNd->u.diadic.left = nd;
        sumNd->u.diadic.right = offsetNd;
        IRO_AddToList(sumNd, list);
    }
}

static void LookupLinearExprAction(LocationSet* loc, void* refcon) {
    LookupLinearExprActionParams* params;
    IROListNode* list;

    IRO_ASSERT(6917, loc != NULL);
    IRO_ASSERT(6918, refcon != NULL);

    params = refcon;

    IRO_ASSERT(6922, params->proc != NULL);
    IRO_ASSERT(6923, params->indirectType != NULL);
    IRO_ASSERT(6924, params->list != NULL);

    list = *params->list = IRO_malloc(sizeof(IROListNode));
    IRO_InitList(&list->list);
    list->nextList = NULL;

    if (!LocationSet_IsUnknown(loc) && LocationSetRepresentsSingleLocation(loc, NULL, NULL)) {
        CreateExpressionForLocationSet(loc, &list->list, params->indirectType, params->proc);
    }

    params->list = &list->nextList;
}

void PointerAnalysis_LookupLinearNodePointerExpr(
    Object* proc, IROLinear* indirect, IROListNode** list
) {
    LocationSetSet* set;
    LookupLinearExprActionParams params;

    IRO_ASSERT(6948, indirect != NULL);

    if (!indirect->pointsToFunction) {
        return;
    }

    set = LocationSetSet_New();
    LocationSetSet_Init(set);
    EvalExpr(set, proc, indirect, NULL, NULL, NULL);

    memset(&params, 0, sizeof(params));
    params.proc = proc;
    params.indirectType = indirect->rtype;
    params.list = list;

    LocationSetSet_ForEach(set, LookupLinearExprAction, &params);
    LocationSetSet_Term(set);
    LocationSetSet_Delete(set);
}

// NOTE: invented names: offsetExpr, sumExpr
// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
static void CreateENodeForLocationSet(
    LocationSet* loc, ENode** enodePtr, Type* rtype, Object* proc
) {
    CInt64 field;
    PAMemoryBlock* block;
    PAMemoryBlockKind kind;
    void* thing;
    ENode* nd;
    Object* obj;

    IRO_ASSERT(0, loc != NULL);
    IRO_ASSERT(0, !LocationSet_IsUnknown(loc));
    IRO_ASSERT(0, LocationSet_stride(loc) == 0);
    IRO_ASSERT(0, enodePtr != NULL);
    IRO_ASSERT(0, rtype != NULL);
    IRO_ASSERT(0, proc != NULL);

    field = LocationSet_field(loc);
    block = LocationSet_block(loc);
    kind = PAMemoryBlock_kind(block);
    thing = PAMemoryBlock_thing(block);
    nd = NULL;

    switch (kind) {
        case PAMEMORYBLOCKKIND_EXTENDEDPARAM:
            GetDefiniteObjectOfExtendedParamLoc(loc, &obj, &field);
            if (obj) {
                nd = create_objectrefnode(obj);
                nd->rtype = rtype;
            }
            break;
        case PAMEMORYBLOCKKIND_LOCALVAR:
            obj = GetLocalObject(thing, proc, FALSE);
            if (obj) {
                if (ObjectIsAFunctionArgument(proc, obj) && obj->u.var.realObj) {
                    obj = obj->u.var.realObj;
                }
                nd = create_objectrefnode(obj);
                nd->rtype = rtype;
            }
            break;
        case PAMEMORYBLOCKKIND_INT:
            if (IS_TYPE_INT(rtype)) {
                nd = IRO_NewENode(EINTCONST);
                nd->data.intval = *((CInt64*)thing);
                nd->rtype = rtype;
            }
            break;
        case PAMEMORYBLOCKKIND_OTHER:
            break;
        default:
            CError_FATAL(7040);
    }

    if (nd && !CInt64_IsZero(&field)) {
        ENode* offsetExpr;
        ENode* sumExpr;

        offsetExpr = IRO_NewENode(EINTCONST);
        offsetExpr->data.intval = field;
        offsetExpr->rtype = TYPE(&stunsignedlong);

        sumExpr = IRO_NewENode(EADD);
        sumExpr->data.diadic.left = nd;
        sumExpr->data.diadic.right = offsetExpr;
        sumExpr->rtype = rtype;

        *enodePtr = sumExpr;
    } else {
        *enodePtr = nd;
    }
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
static void LookupENodeExprAction(LocationSet* loc, void* refcon) {
    LookupENodeExprActionParams* params;
    ENodeList* list;

    IRO_ASSERT(0, loc != NULL);
    IRO_ASSERT(0, refcon != NULL);

    params = refcon;

    IRO_ASSERT(0, params->proc != NULL);
    IRO_ASSERT(0, params->indirectType != NULL);
    IRO_ASSERT(0, params->list != NULL);

    list = *params->list = IRO_malloc(sizeof(ENodeList));
    list->node = NULL;
    list->next = NULL;

    if (!LocationSet_IsUnknown(loc) && LocationSetRepresentsSingleLocation(loc, NULL, NULL)) {
        CreateENodeForLocationSet(loc, &list->node, params->indirectType, params->proc);
    }

    params->list = &list->next;
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
void PointerAnalysis_LookupENodePointerExpr(Object* proc, ENode* indirect, ENodeList** list) {
    LocationSetSet* set;
    LookupENodeExprActionParams params;

    IRO_ASSERT(0, indirect != NULL);
    IRO_ASSERT_MSG(
        0, (indirect->flags & IROLF_Assigned) == 0, "(indirect->flags & LF_ASSIGNED) == 0"
    );

    if (indirect->pointsTo) {
        set = LocationSetSet_New();
        LocationSetSet_Init(set);
        EvalENodeExpr(set, proc, indirect, NULL, NULL, NULL);

        memset(&params, 0, sizeof(params));
        params.proc = proc;
        params.indirectType = indirect->rtype;
        params.list = list;

        LocationSetSet_ForEach(set, LookupENodeExprAction, &params);
        LocationSetSet_Term(set);
        LocationSetSet_Delete(set);
    }
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
void PointerAnalysis_LookupVariableIntoLinearNodeExprs(
    Object* proc, VarRecord* var, PointsToFunction* pointsTo, IROListNode** list
) {
    LocationSetSet* set;
    LookupLinearExprActionParams params;

    IRO_ASSERT(0, var != NULL);

    if (pointsTo) {
        set = LocationSetSet_New();
        LocationSetSet_Init(set);
        EvalVariable(set, proc, var, pointsTo, NULL, NULL, NULL);

        memset(&params, 0, sizeof(params));
        params.proc = proc;
        params.indirectType = var->object->type;
        params.list = list;

        LocationSetSet_ForEach(set, LookupLinearExprAction, &params);
        LocationSetSet_Term(set);
        LocationSetSet_Delete(set);
    }
}

// Dead in every release (the linker drops it), but its literals and the inline functions it uses
// are in the object.
void PointerAnalysis_LookupVariableIntoENodeExprs(
    Object* proc, VarRecord* var, PointsToFunction* pointsTo, ENodeList** list
) {
    LocationSetSet* set;
    LookupENodeExprActionParams params;

    IRO_ASSERT(0, var != NULL);

    if (pointsTo) {
        set = LocationSetSet_New();
        LocationSetSet_Init(set);
        EvalVariable(set, proc, var, pointsTo, NULL, NULL, NULL);

        memset(&params, 0, sizeof(params));
        params.proc = proc;
        params.indirectType = var->object->type;
        params.list = list;

        LocationSetSet_ForEach(set, LookupENodeExprAction, &params);
        LocationSetSet_Term(set);
        LocationSetSet_Delete(set);
    }
}

static void GetFunctionDepsOrKillsAction(LocationSet* ls, void* refcon) {
    GetFunctionDepsOrKillsActionParams* params;
    ObjectList* list;
    PAMemoryBlock* block;
    ExtendedParam* ep;
    ObjectSet* objSet;
    PALocalVar* local;
    Object* obj;

    IRO_ASSERT(7195, ls != NULL);
    IRO_ASSERT(7196, refcon != NULL);

    params = refcon;

    IRO_ASSERT(7200, params->proc != NULL);
    IRO_ASSERT(7201, params->ptf != NULL);
    IRO_ASSERT(7202, params->funccall != NULL);
    IRO_ASSERT(7203, params->map == NULL || params->map != NULL);
    IRO_ASSERT(7204, params->list != NULL);

    list = *params->list = IRO_malloc(sizeof(ObjectList));
    list->object = NULL;
    list->next = NULL;

    if (!LocationSet_IsUnknown(ls) && (block = LocationSet_block(ls))) {
        obj = NULL;
        switch (PAMemoryBlock_kind(block)) {
            case PAMEMORYBLOCKKIND_EXTENDEDPARAM:
                if ((ep = PAMemoryBlock_thing(block)) && (objSet = ExtendedParam_objectSet(ep))) {
                    IRO_ASSERT(7223, obj == NULL);
                    ObjectSet_ForEach(objSet, FindGlobalObjectAction, &obj);
                }
                break;
            case PAMEMORYBLOCKKIND_LOCALVAR:
                if ((local = PAMemoryBlock_thing(block))) {
                    obj = GetLocalObject(local, &stUnknown, FALSE);
                    if (ObjectIsAFunctionArgument(FunctionName, obj) && obj->u.var.realObj) {
                        obj = obj->u.var.realObj;
                    }
                }
                break;
            default:
                break;
        }

        list->object = obj;
    }

    params->list = &list->next;
}

static void GetFunctionDepsOrKillsAction2(PointsToEntry* pte, void* refcon) {
    GetFunctionDepsOrKillsActionParams* params;
    LocationSet* loc;
    PAMemoryBlock* block;
    LocationSetSet* set;

    IRO_ASSERT(7255, pte != NULL);
    IRO_ASSERT(7256, refcon != NULL);

    params = refcon;

    IRO_ASSERT(7260, params->proc != NULL);
    IRO_ASSERT(7261, params->ptf != NULL);
    IRO_ASSERT(7262, params->funccall != NULL);
    IRO_ASSERT(7263, params->map != NULL);
    IRO_ASSERT(7264, params->list != NULL);

    loc = PointsToEntry_loc(pte);

    IRO_ASSERT(7268, !LocationSet_IsUnknown(loc));

    block = LocationSet_block(loc);
    if (block && PAMemoryBlock_kind(block) == PAMEMORYBLOCKKIND_EXTENDEDPARAM) {
        set = LocationSetSet_New();
        LocationSetSet_Init(set);
        GetActualLocsOfExtendedParam(set, loc, NULL, &stCallingContextStack, params->map, FALSE);
        LocationSetSet_ForEach(set, GetFunctionDepsOrKillsAction, params);
        LocationSetSet_Term(set);
        LocationSetSet_Delete(set);
    }
}

static void CreateFalseContext(
    Object* proc,
    IROLinear* funccall,
    PartialTransferFunction** resultPTF,
    ParamMappingFunction** resultMap
) {
    IROLinear myFunccall;
    Boolean needVisit;
    ParamMappingFunction* map;
    PartialTransferFunction* ptf;
    StackElement* stackElement;
    ParamMappingFunction* map2;
    PartialTransferFunction* ptf2;
    StackElement* stackElement2;

    PointerAnalysis_Init();
    memset(&myFunccall, 0, sizeof(myFunccall));
    myFunccall.type = IROLinearFunccall;

    map = ParamMappingFunction_New();
    ParamMappingFunction_Init(map);
    RecordActuals(&myFunccall, FunctionName, map);

    needVisit = FALSE;
    ptf = GetPTF(map, FunctionName, &myFunccall, stUnknownPTF, &needVisit);

    stackElement = StackElement_New();
    StackElement_Init(stackElement, FunctionName, ptf, map, &myFunccall);
    Stack_Push(&stCallingContextStack, stackElement);
    StackElement_Term(stackElement);
    StackElement_Delete(stackElement);

    map2 = ParamMappingFunction_New();
    ParamMappingFunction_Init(map2);
    RecordActuals(funccall, proc, map2);

    if (FunctionName != proc) {
        needVisit = FALSE;
        ptf2 = GetPTF(map2, proc, funccall, ptf, &needVisit);

        stackElement2 = StackElement_New();
        StackElement_Init(stackElement2, proc, ptf2, map2, funccall);
        Stack_Push(&stCallingContextStack, stackElement2);
        StackElement_Term(stackElement2);
        StackElement_Delete(stackElement2);
    } else {
        ptf2 = stUnknownPTF;
    }

    *resultPTF = ptf2;
    *resultMap = map2;
}

static void DestroyFalseContext(
    Object* proc, PartialTransferFunction* ptf, ParamMappingFunction* map
) {
    StackElement* element;

    if (FunctionName != proc) {
        element = Stack_Pop(&stCallingContextStack);
        StackElement_Term(element);
        StackElement_Delete(element);
    }

    ParamMappingFunction_Term(map);
    ParamMappingFunction_Delete(map);

    element = Stack_Pop(&stCallingContextStack);
    map = StackElement_map(element);
    ParamMappingFunction_Term(map);
    ParamMappingFunction_Delete(map);
    StackElement_Term(element);
    StackElement_Delete(element);

    PointerAnalysis_Term();
}

// whether proc has no partial transfer function (the first one in *ptf otherwise)
static inline int NoPTF(Object* proc, PartialTransferFunction** ptf) { // NOTE: invented name
    int fail = 1;
    if (proc->u.func.ptfList && (*ptf = PTFList_FindFirst(proc->u.func.ptfList))) {
        fail = 0;
    }
    return fail;
}

void PointerAnalysis_GetFunctionKills(Object* proc, IROLinear* funccall, ObjectList** list) {
    Boolean fail;
    PartialTransferFunction* ptf;
    LocationSetSet* set;
    GetFunctionDepsOrKillsActionParams params;

    IRO_ASSERT(7389, proc != NULL);
    IRO_ASSERT(7390, funccall != NULL);
    IRO_ASSERT_MSG(7391, funccall->type == IROLinearFunccall, "funccall->lt == Lt_Funccall");

    if (!ObjectIsAFunction(proc)) {
        return;
    }

    fail = NoPTF(proc, &ptf);
    if (!fail) {
        set = PartialTransferFunction_funcModifies(ptf);
        fail = set == NULL;
        if (!fail) {
            memset(&params, 0, sizeof(params));
            params.proc = proc;
            params.ptf = ptf;
            params.funccall = funccall;
            params.map = NULL;
            params.list = list;
            LocationSetSet_ForEach(set, GetFunctionDepsOrKillsAction, &params);
        }
    }

    if (fail) {
        *list = IRO_malloc(sizeof(ObjectList));
        (*list)->object = NULL;
        (*list)->next = NULL;
    }
}

// whether proc has no partial transfer function
static inline int NoPTFList(Object* proc) { // NOTE: invented name
    int fail = 1;
    if (proc->u.func.ptfList && PTFList_FindFirst(proc->u.func.ptfList)) {
        fail = 0;
    }
    return fail;
}

void PointerAnalysis_GetFunctionDependencies(Object* proc, IROLinear* funccall, ObjectList** list) {
    Boolean fail;
    PointsToFunction* finalPointsTo;
    GetFunctionDepsOrKillsActionParams params;
    PartialTransferFunction* ptf;
    ParamMappingFunction* map;

    IRO_ASSERT(7437, proc != NULL);
    IRO_ASSERT(7438, funccall != NULL);
    IRO_ASSERT_MSG(7439, funccall->type == IROLinearFunccall, "funccall->lt == Lt_Funccall");

    if (!ObjectIsAFunction(proc)) {
        return;
    }

    fail = NoPTFList(proc);
    if (!fail) {
        CreateFalseContext(proc, funccall, &ptf, &map);
        {
            int noFinal = 1;
            if ((ptf == NULL || map == NULL) == 0 &&
                (finalPointsTo = PartialTransferFunction_finalPointsToFn(ptf))) {
                noFinal = 0;
            }
            fail = noFinal;
        }
        if (!fail) {
            memset(&params, 0, sizeof(params));
            params.proc = proc;
            params.ptf = ptf;
            params.funccall = funccall;
            params.map = map;
            params.list = list;
            PointsToFunction_ForEach(finalPointsTo, GetFunctionDepsOrKillsAction2, &params);
        }
        DestroyFalseContext(proc, ptf, map);
    }

    if (fail) {
        *list = IRO_malloc(sizeof(ObjectList));
        (*list)->object = NULL;
        (*list)->next = NULL;
    }
}

void PointerAnalysis_PragmaMode(void) {
    if (cparamblkptr->preprocess) {
        skipendofline();
        return;
    }

    if (notendofline()) {
        if (plex() == TK_IDENTIFIER) {
            if (!strcmp(tkidentifier->name, "addr")) {
                copts.opt_pointer_analysis_mode = 0;
                return;
            }
            if (!strcmp(tkidentifier->name, "ansi")) {
                copts.opt_pointer_analysis_mode = 1;
                return;
            }
            if (!strcmp(tkidentifier->name, "type")) {
                copts.opt_pointer_analysis_mode = 2;
                return;
            }
        }

        CPrep_Warning(105);
        skipendofline();
        return;
    }

    if (copts.warn_illpragma) {
        CPrep_Warning(186);
    }
    skipendofline();
}

// TODO: not exact (register allocation). Symbols off, as for LocationSets_Overlap, comes closest,
// so the original may be compiled that way.
// NOTE: invented names: allowReturnValue, nested (only the recursive call passes 1; nothing reads
// it), resultIsReturnValue, takesSelector, isReturnValue, strideValue
#pragma sym off
static void ParseLocationSet(
    LocationSet* loc,
    Type* rtype,
    Object* proc,
    CreateNewParamObjects createNewParamObjects,
    Boolean allowReturnValue,
    int nested,
    Boolean* resultIsReturnValue,
    Boolean* resultFailed
) {
    CInt64 field;
    CInt64 strideValue;
    PAMemoryBlock* block;
    UInt32 stride;
    PAMemoryBlock* restriction;
    Boolean failed;
    Boolean takesSelector;
    Boolean epFlag;
    Boolean isReturnValue;
    Boolean isUnknown;
    PAMemoryBlockKind kind;
    void* thing;

    IRO_ASSERT(7543, loc != NULL);
    IRO_ASSERT(7544, rtype == NULL || rtype != NULL);
    IRO_ASSERT(7545, proc != NULL);
    IRO_ASSERT(7546, createNewParamObjects == CREATE_NEW_PARAM_OBJECTS_FALSE ||
                         createNewParamObjects == CREATE_NEW_PARAM_OBJECTS_TRUE);
    IRO_ASSERT(7547, resultFailed != NULL);
    IRO_ASSERT_MSG(7548, *resultFailed == FALSE, "*resultFailed == false");
    IRO_ASSERT(7549, proc != NULL);
    IRO_ASSERT(7550, proc == NULL || ObjectIsAFunction(proc));

    failed = FALSE;
    isUnknown = FALSE;
    isReturnValue = FALSE;
    field = cint64_zero;
    stride = 0;

    tk = lex();
    if (tk == TK_IDENTIFIER) {
        if (!strcmp(tkidentifier->name, "__unknown")) {
            takesSelector = FALSE;
            isUnknown = TRUE;
            tk = lookahead();
            if (tk == '(') {
                lex();
                tk = lex();
                if (tk != TK_IDENTIFIER) {
                    CError_Error(ERR_IDENTIFIER_EXPECTED);
                    failed = TRUE;
                }
                if (!failed) {
                    PALocalVar* local;
                    local = PALocalVar_New();
                    PALocalVar_InitByName(local, tkidentifier->name);

                    block = PAMemoryBlock_New();
                    PAMemoryBlock_Init(block, PAMEMORYBLOCKKIND_LOCALVAR, local);

                    tk = lex();
                    if (tk != ')') {
                        CError_Error(ERR_RPAREN_EXPECTED);
                        failed = TRUE;
                    }
                    if (!failed) {
                        rtype = NULL;
                        restriction = block;
                    }
                }
            } else {
                rtype = NULL;
                restriction = NULL;
            }
        } else if (allowReturnValue && !strcmp(tkidentifier->name, "__return_value")) {
            takesSelector = FALSE;
            isReturnValue = TRUE;
        } else if (!strcmp(tkidentifier->name, "__unique_heap_allocation")) {
            PAHeapBlock* hb;

            takesSelector = FALSE;
            hb = PAHeapBlock_New();
            PAHeapBlock_Init(hb, NULL);

            block = PAMemoryBlock_New();
            PAMemoryBlock_Init(block, PAMEMORYBLOCKKIND_HEAPBLOCK, hb);
        } else if (!strcmp(tkidentifier->name, "__parameter_representative")) {
            takesSelector = TRUE;
            tk = lex();

            if (tk == '(') {
                tk = lex();
                if (tk != TK_INTCONST) {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    failed = TRUE;
                }
                if (!failed) {
                    Object* obj;
                    ObjectList* list;
                    ExtendedParam* ep;
                    obj = NULL;
                    for (list = stParamObjs; list; list = list->next) {
                        if ((obj = list->object) &&
                            ((UInt32)obj->u.var.uid == CInt64_GetULong(&tkintconst))) {
                            break;
                        }
                    }
                    if (!list) {
                        obj = NULL;
                    }

                    if (!obj) {
                        if (createNewParamObjects) {
                            obj = IRO_malloc(sizeof(Object));
                            memset(obj, 0, sizeof(Object));
                            obj->datatype = DLOCAL;
                            obj->extParam = NULL;
                            obj->name = CParser_GetUniqueName();
                            obj->type = (Type*)&stunsignedlong;
                            obj->qual = 0;
                            obj->u.var.info = NULL;
                            obj->u.var.uid = CInt64_GetULong(&tkintconst);

                            list = IRO_malloc(sizeof(ObjectList));
                            list->next = stParamObjs;
                            list->object = obj;
                            stParamObjs = list;

                            ep = CreateExtendedParam(NULL, NULL, obj, &epFlag);
                        } else {
                            char buf[64];
                            sprintf(buf, "__parameter_representative(%ld)", obj->u.var.uid);
                            CError_Error(ERR_UNDEFINED_IDENTIFIER, buf);
                            failed = TRUE;
                        }
                    } else {
                        ep = obj->extParam;
                        IRO_ASSERT(7678, ep != NULL);
                    }

                    if (!failed) {
                        tk = lex();
                        if (tk != ')') {
                            CError_Error(ERR_RPAREN_EXPECTED);
                            failed = TRUE;
                        }
                    }
                    if (!failed) {
                        block = PAMemoryBlock_New();
                        PAMemoryBlock_Init(block, PAMEMORYBLOCKKIND_EXTENDEDPARAM, ep);
                    }
                }
            }
        } else {
            Object* obj;
            struct NameSpace* nspace;

            obj = NULL;
            for (nspace = cscope_current; nspace; nspace = nspace->parent) {
                NameSpaceObjectList* chk;
                if ((chk = CScope_GetLocalObject(nspace, tkidentifier)) &&
                    chk->object->otype == OT_OBJECT) {
                    if (notendofline()) {
                        obj = (Object*)chk->object;
                        break;
                    }
                }
            }

            if (!obj) {
                ObjectList* chk;
                for (chk = FunctionArguments(proc); chk; chk = chk->next) {
                    // (the original tests the address of the `name->name` array: always true)
                    if (chk->object &&
                        chk->object->name &&
                        &chk->object->name->name[0] &&
                        !strcmp(tkidentifier->name, chk->object->name->name)) {
                        obj = chk->object;
                    }
                }
            }

            // the original's: this case leaves `takesSelector` unset
            if (obj) {
                if (ObjectIsAnExtendedParamCandidate(obj)) {
                    kind = PAMEMORYBLOCKKIND_EXTENDEDPARAM;
                    thing = CreateExtendedParam(NULL, NULL, obj, &epFlag);
                } else {
                    kind = PAMEMORYBLOCKKIND_LOCALVAR;
                    thing = PALocalVar_New();
                    // (the original tests the address of the `name->name` array: always true)
                    if (obj->name && &obj->name->name[0] && ObjectIsAFunctionArgument(proc, obj)) {
                        PALocalVar_InitByName(thing, obj->name->name);
                    } else {
                        PALocalVar_InitByObject(thing, obj);
                    }
                }

                block = PAMemoryBlock_New();
                PAMemoryBlock_Init(block, kind, thing);
                rtype = CDecl_NewPointerType(obj->type);
            } else {
                CError_Error(ERR_UNDEFINED_IDENTIFIER, tkidentifier->name);
                failed = TRUE;
            }
        }
    } else if (tk == TK_INTCONST) {
        takesSelector = FALSE;
        thing = &tkintconst;
        block = PAMemoryBlock_New();
        PAMemoryBlock_Init(block, PAMEMORYBLOCKKIND_INT, thing);
    } else if (tk == '(') {
        ParseLocationSet(
            loc, rtype, proc, createNewParamObjects, allowReturnValue, 1, &isReturnValue, &failed
        );
        if (!failed) {
            tk = plex();
            if (tk != ')') {
                CError_Error(ERR_RPAREN_EXPECTED);
            }
            failed = TRUE;
        }
    } else {
        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        failed = TRUE;
    }

    if (!failed && takesSelector) {
        tk = lookahead();
        if (tk == '[') {
            lex();
            if (rtype && IS_TYPE_POINTER(rtype) && IS_TYPE_POINTER(TPTR_TARGET(rtype))) {
                tk = lex();
                if (tk == ']') {
                    if ((stride = rtype->size)) {
                        CInt64_SetLong(&strideValue, stride);
                        field = CInt64_Mod(field, strideValue);
                    }
                    rtype = TPTR_TARGET(rtype);
                } else {
                    CError_Error(ERR_RBRACKET_EXPECTED);
                }
            } else {
                CError_Error(ERR_POINTER_ARRAY_REQUIRED);
            }
        } else if (tk == '.') {
            lex();
            if (rtype && IS_TYPE_POINTER(rtype) && IS_TYPE_STRUCT(TPTR_TARGET(rtype))) {
                if (TPTR_TARGET(rtype)->size) {
                    tk = lex();
                    if (tk == TK_IDENTIFIER) {
                        StructMember* member;
                        if ((member = ismember(TYPE_STRUCT(TPTR_TARGET(rtype)), tkidentifier))) {
                            CInt64_SetLong(&field, member->offset);
                            rtype = CDecl_NewPointerType(member->type);
                        } else {
                            CError_Error(ERR_NOT_STRUCT_UNION_CLASS_MEMBER, tkidentifier);
                        }
                    } else {
                        CError_Error(ERR_IDENTIFIER_EXPECTED);
                    }
                } else {
                    CError_Error(
                        ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, TPTR_TARGET(rtype), 0
                    );
                }
            } else {
                CError_Error(ERR_NOT_STRUCT_UNION_CLASS);
            }
        }
    }

    if (!failed && !isReturnValue) {
        LocationSet_Term(loc);
        if (!isUnknown) {
            LocationSet_InitKnown(loc, block, field, stride, rtype);
        } else {
            LocationSet_InitUnknown(loc, rtype, restriction, NULL);
        }
    }

    *resultIsReturnValue = isReturnValue;
    *resultFailed = failed;
}
#pragma sym reset

static void ParseLocationSetSet(
    LocationSetSet* locs,
    Type* rtype,
    Object* proc,
    CreateNewParamObjects createNewParamObjects,
    Boolean* resultFailed
) {
    Boolean failed;

    IRO_ASSERT(7883, locs != NULL);
    IRO_ASSERT(7884, rtype == NULL || rtype != NULL);
    IRO_ASSERT(7885, proc != NULL);
    IRO_ASSERT(7886, createNewParamObjects == CREATE_NEW_PARAM_OBJECTS_FALSE || createNewParamObjects == CREATE_NEW_PARAM_OBJECTS_TRUE);
    IRO_ASSERT(7887, resultFailed != NULL);
    IRO_ASSERT_MSG(7888, *resultFailed == FALSE, "*resultFailed == false");
    IRO_ASSERT(7889, proc != NULL);
    IRO_ASSERT(7890, proc == NULL || ObjectIsAFunction(proc));

    failed = FALSE;

    tk = lex();
    if (tk != '(') {
        CError_Error(ERR_LPAREN_EXPECTED);
        failed = TRUE;
    }

    if (!failed) {
        Boolean isReturnValue;

        LocationSet* ls;
        ls = LocationSet_New();
        LocationSet_InitUnknown(ls, NULL, NULL, NULL);

        tk = lookahead();
        while (!failed && tk != ')') {
            ParseLocationSet(
                ls, rtype, proc, createNewParamObjects, FALSE, 0, &isReturnValue, &failed
            );
            if (!failed) {
                LocationSetSet_Add(locs, ls);
            }

            if (!failed) {
                tk = lookahead();
                if (tk == ',') {
                    lex();
                    tk = lookahead();
                } else if (tk != ')') {
                    lex();
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    failed = TRUE;
                }
            }
        }

        if (!failed) {
            lex();
        }

        LocationSet_Term(ls);
        LocationSet_Delete(ls);
    }

    *resultFailed = failed;
}

static Object* GetFunctionObjectFromDeclInfo(DeclInfo* di) {
    Object* proc;

    IRO_ASSERT(7944, di != NULL);

    if (di->storageclass != TK_TYPEDEF && IS_TYPE_FUNC(di->thetype)) {
        Boolean isFirstDeclaration;
        proc = CDecl_GetFunctionObject(di, NULL, &isFirstDeclaration, TRUE);
        if (isFirstDeclaration) {
            di->firstDeclaration = TRUE;
        }
    } else {
        proc = NULL;
    }

    return proc;
}

void PointerAnalysis_ParseEntryPointsToSpecifier(DeclInfo* di) {
    Object* proc;
    Boolean failed;
    Boolean isReturnValue;

    IRO_ASSERT(7973, di != NULL);

    proc = GetFunctionObjectFromDeclInfo(di);
    if (proc) {
        IRO_ASSERT(7978, proc == NULL || ObjectIsAFunction(proc));

        tk = lex();
        IRO_ASSERT_MSG(
            7987,
            tk == TK_IDENTIFIER && strcmp(tkidentifier->name, "entry_points_to") == 0,
            "tk == TIDENTIFIER && strcmp(tkidentifier->name,\"entry_points_to\") == 0"
        );

        failed = FALSE;

        tk = lex();
        if (tk != '(') {
            CError_Error(ERR_LPAREN_EXPECTED);
            failed = TRUE;
        }

        if (!failed) {
            LocationSet* ls;
            ls = LocationSet_New();
            LocationSet_InitUnknown(ls, NULL, NULL, NULL);
            ParseLocationSet(
                ls, NULL, proc, CREATE_NEW_PARAM_OBJECTS_FALSE, FALSE, 0, &isReturnValue, &failed
            );
            if (!failed) {
                if ((tk = lex()) != ',') {
                    CError_Error(ERR_COMMA_EXPECTED);
                    failed = TRUE;
                }
            }
            if (!failed) {
                Type* type;
                Type* innerType;
                LocationSetSet* lss;

                type = LocationSet_rtype(ls);
                if (type && IS_TYPE_POINTER(type)) {
                    innerType = TPTR_TARGET(type);
                } else {
                    innerType = NULL;
                }

                lss = LocationSetSet_New();
                LocationSetSet_Init(lss);
                ParseLocationSetSet(lss, innerType, proc, CREATE_NEW_PARAM_OBJECTS_TRUE, &failed);
                if (!failed) {
                    if ((tk = lex()) != ')') {
                        CError_Error(ERR_RPAREN_EXPECTED);
                        failed = TRUE;
                    }
                }
                if (!failed) {
                    PartialTransferFunction* ptf;
                    PointsToFunction* pointsToFunc;
                    PointsToEntry* pte;

                    if (!proc->u.func.ptfList) {
                        proc->u.func.ptfList = PTFList_New();
                        PTFList_Init(proc->u.func.ptfList);
                    }
                    ptf = PTFList_FindFirst(proc->u.func.ptfList);
                    if (!ptf) {
                        ptf = AllocatePTF(proc, NULL, NULL);
                    }

                    pointsToFunc = PartialTransferFunction_initialPointsToFn(ptf);
                    pte = PointsToEntry_New();
                    PointsToEntry_Init(pte, ls, lss);
                    PointsToFunction_Add(pointsToFunc, pte);
                    PointsToEntry_Term(pte);
                    PointsToEntry_Delete(pte);
                }
                LocationSetSet_Term(lss);
                LocationSetSet_Delete(lss);
            }
            LocationSet_Term(ls);
            LocationSet_Delete(ls);
        }
    }
}

void PointerAnalysis_ParseExitPointsToSpecifier(DeclInfo* di) {
    Object* proc;
    Boolean failed;
    Boolean isReturnValue;

    IRO_ASSERT(8088, di != NULL);

    proc = GetFunctionObjectFromDeclInfo(di);
    if (proc) {
        IRO_ASSERT(8093, proc == NULL || ObjectIsAFunction(proc));

        tk = lex();
        IRO_ASSERT_MSG(
            8102,
            tk == TK_IDENTIFIER && strcmp(tkidentifier->name, "exit_points_to") == 0,
            "tk == TIDENTIFIER && strcmp(tkidentifier->name,\"exit_points_to\") == 0"
        );

        failed = FALSE;

        tk = lex();
        if (tk != '(') {
            CError_Error(ERR_LPAREN_EXPECTED);
            failed = TRUE;
        }

        if (!failed) {
            LocationSet* ls;
            ls = LocationSet_New();
            LocationSet_InitUnknown(ls, NULL, NULL, NULL);
            ParseLocationSet(
                ls, NULL, proc, CREATE_NEW_PARAM_OBJECTS_FALSE, TRUE, 0, &isReturnValue, &failed
            );
            if (!failed) {
                if ((tk = lex()) != ',') {
                    CError_Error(ERR_COMMA_EXPECTED);
                    failed = TRUE;
                }
            }
            if (!failed) {
                Type* type;
                Type* innerType;
                LocationSetSet* lss;

                type = LocationSet_rtype(ls);
                if (type && IS_TYPE_POINTER(type)) {
                    innerType = TPTR_TARGET(type);
                } else {
                    innerType = NULL;
                }

                lss = LocationSetSet_New();
                LocationSetSet_Init(lss);
                ParseLocationSetSet(lss, innerType, proc, CREATE_NEW_PARAM_OBJECTS_FALSE, &failed);
                if (!failed) {
                    if ((tk = lex()) != ')') {
                        CError_Error(ERR_RPAREN_EXPECTED);
                        failed = TRUE;
                    }
                }
                if (!failed) {
                    PartialTransferFunction* ptf;
                    PointsToFunction* pointsToFunc;
                    PointsToEntry* pte;

                    if (!proc->u.func.ptfList) {
                        proc->u.func.ptfList = PTFList_New();
                        PTFList_Init(proc->u.func.ptfList);
                    }
                    ptf = PTFList_FindFirst(proc->u.func.ptfList);
                    if (!ptf) {
                        ptf = AllocatePTF(proc, NULL, NULL);
                    }

                    pointsToFunc = PartialTransferFunction_finalPointsToFn(ptf);
                    pte = PointsToEntry_New();
                    if (isReturnValue) {
                        PointsToEntry_Init(pte, PartialTransferFunction_returnLocation(ptf), lss);
                    } else {
                        PointsToEntry_Init(pte, ls, lss);
                    }
                    PointsToFunction_Add(pointsToFunc, pte);
                    PointsToEntry_Term(pte);
                    PointsToEntry_Delete(pte);
                }
                LocationSetSet_Term(lss);
                LocationSetSet_Delete(lss);
            }
            LocationSet_Term(ls);
            LocationSet_Delete(ls);
        }
    }
}

void PointerAnalysis_ParseFunctionModifiesSpecifier(DeclInfo* di) {
    Object* proc;
    Boolean failed;
    LocationSetSet* lss;
    PartialTransferFunction* ptf;
    LocationSetSet* ptfLSS;

    IRO_ASSERT(8202, di != NULL);

    proc = GetFunctionObjectFromDeclInfo(di);
    if (proc) {
        IRO_ASSERT(8207, proc == NULL || ObjectIsAFunction(proc));

        tk = lex();
        IRO_ASSERT_MSG(
            8216,
            tk == TK_IDENTIFIER && strcmp(tkidentifier->name, "function_modifies") == 0,
            "tk == TIDENTIFIER && strcmp(tkidentifier->name,\"function_modifies\") == 0"
        );

        failed = FALSE;
        lss = LocationSetSet_New();
        LocationSetSet_Init(lss);
        ParseLocationSetSet(lss, NULL, proc, CREATE_NEW_PARAM_OBJECTS_FALSE, &failed);
        if (!failed) {
            if (!proc->u.func.ptfList) {
                proc->u.func.ptfList = PTFList_New();
                PTFList_Init(proc->u.func.ptfList);
            }
            ptf = PTFList_FindFirst(proc->u.func.ptfList);
            if (!ptf) {
                ptf = AllocatePTF(proc, NULL, NULL);
            }
            ptfLSS = PartialTransferFunction_funcModifies(ptf);
            LocationSetSet_RemoveAll(ptfLSS);
            LocationSetSet_AddSet(ptfLSS, lss);
        }
        LocationSetSet_Term(lss);
        LocationSetSet_Delete(lss);
    }
}

// The unit's statics are 2-byte packed, as in the original (MWCC lays out .bss with the packing in
// force at the end of the file)
#pragma pack(2)
