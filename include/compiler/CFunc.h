#ifndef COMPILER_CFUNC_H
#define COMPILER_CFUNC_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum StatementType {
    ST_NOP = 1,
    ST_LABEL,
    ST_GOTO,
    ST_EXPRESSION,
    ST_SWITCH,
    ST_IFGOTO,
    ST_IFNGOTO,
    ST_RETURN,
    ST_OVF,
    ST_EXIT,
    ST_ENTRY,
    ST_BEGINCATCH,
    ST_ENDCATCH,
    ST_ENDCATCHDTOR,
    ST_GOTOEXPR,
    ST_ASM,
    ST_BEGINLOOP,
    ST_ENDLOOP,
    ST_ILLEGAL
} StatementType;
enum {
    StmtFlag_KeepLabel = 1,
    StmtFlag_EndsConstruction = 2,
    StmtFlag_LoopBranch = 4,
    StmtFlag_AfterNoReturn = 8,
    StmtFlag_Unrolled = 0x10
};
#pragma pack(push, 2)
typedef struct DeclBlock {
    struct DeclBlock* next;
    struct ExceptionAction* dobjstack;
    NameSpace* parent_nspace;
    UInt16 index;
} DeclBlock;
typedef struct LoopInfo {
    int unused0;
    int unused4;
    short unused8;
    Statement* stmt;
} LoopInfo;
struct Statement {
    Statement* next;
    StatementType type;
    UInt8 marked;
    UInt8 flags;
    UInt16 value;
    ENode* expr;
    CLabel* label;
    struct ExceptionAction* dobjstack;
    SInt32 sourceoffset;
    HashNameNode* sourcefilepath;
};
#pragma pack(pop)
struct InitExpr {
    struct InitExpr* next;
    ENode* expr;
    Object* object;
};
struct CLabel {
    struct CLabel* next;
    struct Statement* stmt;
    struct HashNameNode* uniquename;
    struct HashNameNode* name;
    struct PCodeLabel* pclabel;
};
typedef enum CtorChainType {
    CtorChain_Base = 0,
    CtorChain_VBase = 1,
    CtorChain_MemberVar = 2
} CtorChainType;
#pragma pack(push, 2)
struct CtorChain {
    struct CtorChain* next;
    CtorChainType what;
    ENode* objexpr;
    union {
        ClassList* base;
        VClassList* vbase;
        ObjMemberVar* membervar;
    } u;
};
#pragma pack(pop)
struct DeclThing {
    Type* thetype;
    UInt32 qual;
    SwitchInfo* switchinfo;
    CLabel* loopContinue;
    CLabel* loopBreak;
};
struct DefArgCtorInfo {
    Object* default_func;
    ENode* default_arg;
};

extern FuncArg elipsis;
extern FuncArg oldstyle;
extern ObjectList* arguments;
extern ObjectList* locals;
extern short localcount;
extern SInt32 curstmtvalue;
extern SInt32 sourceoffset;
extern HashNameNode* sourcefilepath;
extern SInt32 functionbodyoffset;
extern HashNameNode* functionbodypath;
extern InitExpr* init_expressions;
extern CLabel* Labels;
extern CtorChain* ctor_chain;
extern Statement* curstmt;
extern SInt32 symdecloffset;
extern SInt32 symdeclend;

extern DeclBlock* CFunc_NewDeclBlock(void);
extern void CFunc_RestoreBlock(DeclBlock* block);
extern void CFunc_SetupLocalVarInfo(Object* obj);
extern Boolean CFunc_ParseFakeArgList(Boolean stopAtComma);
extern FuncArg* parameter_type_list(DeclInfo* declinfo);
extern CLabel* findlabel(void);
extern CLabel* newlabel(void);
extern Statement* CFunc_AppendStatement(StatementType sttype);
extern Statement* CFunc_InsertStatement(StatementType sttype, Statement* after);
extern Statement* CFunc_InsertBeforeStatement(StatementType sttype, Statement* before);
extern void CheckCLabels(void);
extern Object* create_temp_object(Type* type);
extern ENode* create_temp_node(Type* type);
extern ENode* create_temp_node2(Type* type);
extern void CFunc_WarnUnused(void);
extern void CFunc_CodeCleanup(Statement* stmt);
extern Statement* CFunc_GenerateLoop(
    Statement* stmt,
    Type* type,
    ENode* lowerBound,
    ENode* upperBound,
    ENode* increment1,
    ENode* increment2,
    ENode* (*callback)(ENode*, ENode*)
);
extern void CFunc_CompoundStatement(DeclThing* thing);
extern void CFunc_SetupNewFuncArgs(Object* func, FuncArg* args);
extern NameSpace* CFunc_FuncGenSetup(Statement* stmt, Object* func);
extern void CFunc_Gen(Statement* stmt, Object* func, UInt8 oldStyleParameters);
extern void CFunc_CheckClassCtors(TypeClass* tclass);
extern void CFunc_ParseFuncDef(
    Object* func,
    DeclInfo* di,
    TypeClass* tclass,
    Boolean is_method,
    Boolean is_static,
    NameSpace* nspace
);
extern void InitExpr_Register(ENode* expr, Object* object);
extern void CFunc_GenerateDummyFunction(Object* func);
extern void CFunc_GenerateSingleExprFunc(Object* func, ENode* expr);
extern void CFunc_GenerateDummyCtorFunc(Object* func, Object* real_ctor);

#ifdef __cplusplus
}
#endif

#endif
