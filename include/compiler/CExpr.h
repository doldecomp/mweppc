#ifndef COMPILER_CEXPR_H
#define COMPILER_CEXPR_H

#include "compiler/common.h"
#include "compiler/CScope.h"
#include "compiler/enode.h"
#include "compiler/types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ConIteratorList {
    struct ConIteratorList* next;
    struct ConIterator* iter;
};
struct ConIterator {
    struct ConIterator* parent;
    struct ConIteratorList* children;
    struct TypeClass* tclass;
};
struct ConversionIterator {
    CScopeObjectIterator objiter;
    struct ConIterator myconiter;
    struct ConIterator* coniter;
};
enum {
    CheckResult_NoMatch,
    CheckResult_Exact,
    CheckResult_Promotion,
    CheckResult_Conversion,
    CheckResult_UserConversion
};

#pragma pack(push, 2)
struct ComparisonValues {
    SInt16 exactMatches;
    SInt16 promotions;
    SInt16 conversions;
    SInt16 conversionWeight;
    SInt16 qualifierMatches;
};
struct ArgMatch {
    struct Object* object;
    ComparisonValues standard;
    SInt16 userconversions;
    ComparisonValues userStandard;
    struct ObjectList* list;
};
#pragma pack(pop)
struct FuncMatchArgs {
    struct ENodeList* exprs;
    struct FuncArg* args;
};
struct Conversion {
    ENode* expression;
    ENode* left;
    ENode* right;
};

typedef Boolean (*NameObjCheckCB)(HashNameNode*, struct Object*);
extern NameObjCheckCB name_obj_check;
extern Boolean disallowgreaterthan;

extern ENode* CExpr_RewriteConst(ENode* expr);
extern void optimizecomm(ENode* expr);
extern short canadd(ENode* expr, SInt32 value);
extern ENode* checkreference(ENode* expr);
extern ENode* pointer_generation(ENode* expr);
extern ENode* CExpr_PointerGeneration(ENode* expr);
extern ENode* oldassignmentpromotion(ENode* expr, Type* type, short qual, Boolean checkAccess);
extern ENode* argumentpromotion(ENode* expr, Type* type, short qual, Boolean checkAccess);
extern ENode* classargument(ENode* expr);
extern ENodeList* CExpr_ScanExpressionList(Boolean is_parens);
extern ENode* CExpr_DoExplicitConversion(Type* type, UInt32 qual, ENodeList* list);
extern ENode* CExpr_MakeNameLookupResultExpr(NameResult* pr);
extern SInt32 scansizeof(void);
extern ENode* getnodeaddress(ENode* expr, Boolean isAddressOf);
extern ENode* getpointertomemberfunc(ENode* expr, Type* type, Boolean buildData);
extern ENode* CExpr_New_ELOGNOT_Node(ENode* input);
extern ENode* CExpr_New_EMONMIN_Node(ENode* input);
extern ENode* CExpr_New_EBINNOT_Node(ENode* input);
extern ENode* unary_expression(void);
extern ENode* do_castnullcheck(ENode* condexpr, ENode* nullcheckexpr);
extern ENode* CExpr_SafeClassPointerCast(
    ENode* expr, TypeClass* a, TypeClass* b, Boolean typconflag, Boolean pathcheckflag
);
extern ENode* PointerToMemberCast(
    ENode* expr, TypeMemberPointer* tm1, TypeMemberPointer* tm2, Boolean checkAccess
);
extern ENode* CExpr_MemberPointerConversion(
    ENode* expr, TypeMemberPointer* type, Boolean buildData
);
extern ENode* do_typecast(ENode* expr, Type* type, UInt32 qual);
extern ENode* cast_expression(void);
extern ENode* CExpr_New_ELESSEQU_Node(ENode* left, ENode* right);
extern ENode* memberpointercompare(ENodeType nt, ENode* left, ENode* right);
extern ENode* CExpr_New_EEQU_Node(ENode* left, ENode* right);
extern ENode* CExpr_NewDyadicNode(ENode* left, ENodeType nt, ENode* right);
extern ENode* CExpr_New_ECOND_Node(ENode* cond, ENode* expr1, ENode* expr2);
extern ENode* assignment_expression(void);
extern ENode* conv_assignment_expression(void);
extern void CExpr_CheckUnusedExpression(ENode* expr);
extern ENode* s_expression(void);
extern ENode* expression(void);
extern CInt64 CExpr_IntegralConstExprType(Type** tint);
extern ENode* CExpr_IntegralConstOrDepExpr(void);
extern CInt64 CExpr_IntegralConstExpr(void);
extern void CExpr_CheckUnwantedAssignment(ENode* expr);

extern ENode* assign_node;
extern Boolean temp_reference_init;

typedef void (*CExprSearchCB)(ENode* expr);
typedef ENode* (*CExprReplaceCB)(ENode* expr);

extern ENode* intconstnode(Type* type, SInt32 value);
extern ENode* makemonadicnode(ENode* inner, ENodeType ty);
extern ENode* create_objectnode(Object* obj);
extern ENode* CExpr_ConstructObject(
    TypeClass* tclass,
    ENode* addr_expr,
    ENodeList* args,
    Boolean asObject,
    Boolean mostDerived,
    Boolean forceCtor,
    Boolean checkAccess,
    Boolean allowExplicit
);
extern Boolean CExpr_CheckOperator(short token, ENode* left, ENode* right, Conversion* conv);
extern Boolean CExpr_CheckOperatorConversion(
    short token, ENode* left, ENode* right, ENodeList* list, Conversion* conv
);
extern ENode* CExpr_MakeFunctionCall(ENode* funcexpr, ENodeList* arg_exprs);
extern Boolean CExpr_GetFuncMatchArgs(
    Object* obj, ENodeList* argexprs, ENode* expr, FuncMatchArgs* result
);
extern Boolean CExpr_MatchCompare(Object* obj, ArgMatch* a, ArgMatch* b);
extern short assign_check(
    ENode* expr,
    Type* type,
    UInt32 qual,
    Boolean doConvert,
    Boolean allowExplicit,
    Boolean checkAccess
);
extern ENode* get_address_of_temp_copy(ENode* expr, Boolean copyConstant);
extern void CExpr_CheckArithmConversion(ENode* expr, Type* type);
extern Object* CExpr_ConversionIteratorNext(ConversionIterator* iter);
extern void CExpr_ConversionIteratorInit(ConversionIterator* iter, TypeClass* tclass);
extern ENode* CExpr_ConvertToBool(ENode* expr, Boolean isExplicit);
extern ENode* CExpr_ConvertToCondition(ENode* expr);
extern ENode* CExpr_ConvertToIntegral(ENode* expr);
extern ENode* CExpr_GetDefaultArgument(ENode* funcexpr, FuncArg* arg);
extern ENode* CExpr_GenericPtmfCall(Object* obj, TypeFunc* tfunc, ENodeList* arg_exprs);
extern ENode* integralpromote(ENode* expr);
extern ENode* CExpr_GetClassAccessNode(
    BClassList* a, BClassList* b, ENode* expr, Object* obj, AccessType access, Boolean checkAccess
);
extern ENode* CExpr_ClassPointerCast(BClassList* cls, ENode* origexpr, Boolean nullcheckflag);
extern Boolean CExpr_MatchAssign(Type* type, UInt32 qual, ENode* expr, ArgMatch* match);
extern void CExpr_MatchCV(Type* t1, UInt32 q1, Type* t2, UInt32 q2, ArgMatch* match);
extern ENode* CExpr_FuncCallSix(
    Object* func, ENode* arg1, ENode* arg2, ENode* arg3, ENode* arg4, ENode* arg5, ENode* arg6
);
extern ENode* funccallexpr(Object* func, ENode* arg1, ENode* arg2, ENode* arg3, ENode* arg4);
extern ENode* CExpr_AdjustFunctionCall(ENode* expr);
extern ENode* CExpr_IsTempConstruction(ENode* expr, Type* type, ENode** resultexpr);
extern ENode* create_objectnode2(Object* obj);
extern ENode* create_objectrefnode(Object* obj);
extern ENode* CExpr_MakeObjRefNode(Object* obj, Boolean markUsed);
extern Boolean CExpr_IsLValue(ENode* expr);
extern ENode* CExpr_TempModifyExpr(ENode* expr);
extern ENode* promote(ENode* expr, Type* type);
extern void CExpr_ArithmeticConversion(ENode** left, ENode** right);
extern CInt64 CExpr_IntConstConvert(Type* to, Type* from, CInt64 val);
extern ENode* CExpr_GetETEMPCopy(ENode* expr);
extern void CExpr_AliasTransform(ENode* expr);
extern Boolean CExpr_IsOne(ENode* expr);
extern short isnotzero(ENode* expr);
extern short iszero(ENode* expr);
extern ENode* makecommaexpression(ENode* left, ENode* right);
extern ENode* makediadicnode(ENode* left, ENode* right, UInt8 ty);
extern ENode* CExpr_BinaryFloatExpression(ENode* expr);
extern ENode* CExpr_UnaryFloatExpression(ENode* expr);
extern ENode* CExpr_NewENode(ENodeType ty);
extern ENode* nullnode(void);
extern ENode* CExpr_NewETEMPNode(Type* type, Boolean assign_id);
extern ENode* CExpr_NewTemplDepENode(TemplDepSubType t);
extern ENode* forceintegral(ENode* expr);
extern Boolean CExpr_AllBitsSet(ENode* expr);
extern void CExpr_SearchExprTree(ENode* expr, CExprSearchCB callback, SInt32 count, ...);
extern ENode* CExpr_VarArgPromotion(ENode* expr, Boolean toEllipsis);
extern ENode* CExpr_GenericFuncCall(
    BClassList* path,
    ENode* funcexpr,
    Boolean isQualified,
    Object* obj,
    NameSpaceObjectList* nsol,
    TemplArg* templargs,
    ENodeList* nodes,
    Boolean excludeExplicit,
    Boolean isAmbiguous,
    Boolean checkAccess
);
extern short user_assign_check(
    ENode* expr,
    Type* type,
    UInt32 qual,
    Boolean diagnose,
    Boolean allowExplicit,
    Boolean checkAccess
);
extern ENode* CExpr_LValue(ENode* expr, Boolean isModified, Boolean requireLValue);
extern ENode* scannew(Boolean isGlobal);
extern ENode* scandelete(Boolean isGlobal);

extern ENode* CExpr_AssignmentPromotion(
    ENode* expr, Type* type2, UInt32 qual2, Boolean checkAccess
);
extern Boolean CExpr_CanImplicitlyConvert(ENode* expr, Type* type2, UInt32 qual2);
extern ENode* CExpr_Convert(
    ENode* expr, Type* type, UInt32 qual, Boolean isExplicit, Boolean checkAccess
);
extern void CExpr_FuncArgMatch(
    NameSpaceObjectList* list,
    TemplArg* templargs,
    ENodeList* argexprs,
    ArgMatch* bestmatch,
    ENode* expr,
    Boolean excludeExplicit
);
extern Boolean CExpr_CondOperatorMatch(ENode* left, ENode* right, Conversion* conv);
extern Boolean CExpr_OperatorMatch(short token, ENode* left, ENode* right, Conversion* conv);

#ifdef __cplusplus
}
#endif

#endif
