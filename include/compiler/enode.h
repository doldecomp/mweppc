#ifndef COMPILER_ENODE_H
#define COMPILER_ENODE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ENODE_IS(_enode, _etype) ((_enode)->type == (_etype))
#define ENODE_IS2(_enode, _etype1, _etype2) \
    (ENODE_IS(_enode, (_etype1)) || ENODE_IS(_enode, (_etype2)))
#define ENODE_IS3(_enode, _etype1, _etype2, _etype3) \
    (ENODE_IS2(_enode, _etype1, _etype2) || ENODE_IS(_enode, (_etype3)))
#define ENODE_IS4(_enode, _etype1, _etype2, _etype3, _etype4) \
    (ENODE_IS3(_enode, _etype1, _etype2, _etype3) || ENODE_IS(_enode, (_etype4)))
#define ENODE_IS_RANGE(_enode, _lo, _hi) (((_enode)->type >= (_lo)) && ((_enode)->type <= (_hi)))
#define ENODE_QUALS(_enode) ((UInt32)((_enode)->flags & ENODE_FLAG_QUALS))
#define ENODE_IS_INDIRECT_TO(_enode, _etype) \
    (ENODE_IS((_enode), EINDIRECT) && ENODE_IS((_enode)->data.monadic, (_etype)))

#define ENODE_CASE_MONADIC \
    case EPOSTINC: \
    case EPOSTDEC: \
    case EPREINC: \
    case EPREDEC: \
    case EINDIRECT: \
    case EMONMIN: \
    case EBINNOT: \
    case ELOGNOT: \
    case EFORCELOAD: \
    case ETYPCON: \
    case EBITFIELD

#define ENODE_CASE_DIADIC_ALL \
    case EMUL: \
    case EMULV: \
    case EDIV: \
    case EMODULO: \
    case EADDV: \
    case ESUBV: \
    case EADD: \
    case ESUB: \
    case ESHL: \
    case ESHR: \
    case ELESS: \
    case EGREATER: \
    case ELESSEQU: \
    case EGREATEREQU: \
    case EEQU: \
    case ENOTEQU: \
    case EAND: \
    case EXOR: \
    case EOR: \
    case ELAND: \
    case ELOR: \
    case EASS: \
    case EMULASS: \
    case EDIVASS: \
    case EMODASS: \
    case EADDASS: \
    case ESUBASS: \
    case ESHLASS: \
    case ESHRASS: \
    case EANDASS: \
    case EXORASS: \
    case EORASS: \
    case ECOMMA: \
    case EPMODULO: \
    case EROTL: \
    case EROTR: \
    case EBCLR: \
    case EBTST: \
    case EBSET

typedef enum ENodeType {
    EPOSTINC,
    EPOSTDEC,
    EPREINC,
    EPREDEC,
    EINDIRECT,
    EMONMIN,
    EBINNOT,
    ELOGNOT,
    EFORCELOAD,
    EMUL,
    EMULV,
    EDIV,
    EMODULO,
    EADDV,
    ESUBV,
    EADD,
    ESUB,
    ESHL,
    ESHR,
    ELESS,
    EGREATER,
    ELESSEQU,
    EGREATEREQU,
    EEQU,
    ENOTEQU,
    EAND,
    EXOR,
    EOR,
    ELAND,
    ELOR,
    EASS,
    EMULASS,
    EDIVASS,
    EMODASS,
    EADDASS,
    ESUBASS,
    ESHLASS,
    ESHRASS,
    EANDASS,
    EXORASS,
    EORASS,
    ECOMMA,
    EPMODULO,
    EROTL,
    EROTR,
    EBCLR,
    EBTST,
    EBSET,
    ETYPCON,
    EBITFIELD,
    EINTCONST,
    EFLOATCONST,
    ESTRINGCONST,
    ECOND,
    EFUNCCALL,
    EFUNCCALLP,
    EOBJREF,
    EMFPOINTER,
    ENULLCHECK,
    EPRECOMP,
    ETEMP,
    EARGOBJ,
    ELOCOBJ,
    ELABEL,
    ESETCONST,
    ENEWEXCEPTION,
    ENEWEXCEPTIONARRAY,
    EINITTRYCATCH,
    EOBJLIST,
    EMEMBER,
    ETEMPLDEP,
    EINSTRUCTION,
    EDEFINE,
    EREUSE,
    EASSBLK,
    EVECTOR128CONST,
    ECONDASS,
    MAXEXPR
} ENodeType;
enum {
    ENODE_FLAG_CONST = Q_CONST,
    ENODE_FLAG_VOLATILE = Q_VOLATILE,
    ENODE_FLAG_QUALS = Q_CONST | Q_VOLATILE,
    ENODE_FLAG_SOM_ENV_CHECK = 0x10,
    ENODE_FLAG_NONVIRTUAL = 0x80,
    ENODE_FLAG_PARENS = 0x80,
    ENODE_FLAG_SAME_TYPE_CAST = 0x80,
    ENODE_FLAG_RESULT_VIA_TEMP = 0x80
};
#pragma pack(push, 2)
struct EMemberInfo {
    BClassList* path;
    ENode* expr;
    NameSpaceObjectList* list;
    TemplArg* templargs;
    Boolean is_qualified;
    Boolean addressTaken;
    Boolean isambig;
};
struct ENodeList {
    ENodeList* next;
    ENode* node;
};
typedef enum TemplDepSubType {
    TDE_PARAM,
    TDE_SIZEOF,
    TDE_ALIGNOF,
    TDE_CAST,
    TDE_QUALNAME,
    TDE_OBJ,
    TDE_SOURCEREF,
    TDE_ADDRESS_OF
} TemplDepSubType;
union ENodeUnion {
    CInt64 intval;
    Float floatval;
    MWVector128 vector128val;
    SInt32 longval;
    ENode* monadic;
    EMemberInfo* emember;
    Object* objref;
    struct CLabel* label;
    struct {
        struct NameSpaceObjectList* list;
        TemplArg* templargs;
        struct HashNameNode* name;
    } objlist;
    struct {
        ENode* left;
        ENode* right;
    } diadic;
    struct {
        ENode* accessnode;
        ENode* mfpointer;
    } mfpointer;
    struct {
        ENode* nullcheckexpr;
        ENode* condexpr;
        SInt32 precompid;
    } nullcheck;
    SInt32 precompid;
    struct {
        ENode* cond;
        ENode* expr1;
        ENode* expr2;
    } cond;
    struct {
        ENode* funcref;
        ENodeList* args;
        TypeFunc* functype;
    } funccall;
    struct {
        Type* type;
        SInt32 uniqueid;
        Boolean needs_dtor;
    } temp;
    struct {
        SInt32 size;
        char* data;
        unsigned char ispascal;
    } string;
    struct {
        Object* objref;
        SInt32 offset;
    } addr;
    struct {
        ENode* initexpr;
        ENode* tryexpr;
        Object* pointertemp;
        Object* deletefunc;
    } newexception;
    struct {
        ENode* initexpr;
        ENode* tryexpr;
        ENode* catchexpr;
        ENode* result;
    } itc;
    struct {
        union {
            TemplParamID pid;
            struct {
                Type* type;
            } typeexpr;
            struct {
                ENodeList* args;
                Type* type;
                UInt32 qual;
            } cast;
            struct {
                struct TypeTemplDep* type;
                HashNameNode* name;
            } qual;
            struct {
                ENode* expr;
                struct TStreamElement* token;
            } sourceref;
            ENode* monadic;
            Object* obj;
        } u;
        TemplDepSubType subtype;
    } templdep;
};
struct ENode {
    ENodeType type;
    UInt8 cost;
    UInt16 flags;
    Boolean ignored;
    Boolean hascall;
    Type* rtype;
    struct PointsToFunction* pointsTo;
    ENodeUnion data;
};
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif
