#include "compiler/CInit.h"
#include "compiler/CABI.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CException.h"
#include "compiler/CExpr.h"
#include "compiler/CFunc.h"
#include "compiler/CInline.h"
#include "compiler/CInt64.h"
#include "compiler/CMachine.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrec.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CScope.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include <string.h>

typedef void (*ExprCB)(Type* type, ENode* expr, Boolean flag);

typedef struct InitInfo {
    Object* obj;
    OLinkList* list;
    void* unused;
    SInt32 expr_offset;
    ExprCB expr_cb;
    Boolean expr_cb_called;
    Boolean has_runtime_init;
    Boolean use_register_cb;
    InitExprRegisterCB init_expr_register_cb;
    Object* auto_obj;
    InsertExprCB insert_expr_cb;
    RegisterObjectCB register_object_cb;
    char* buffer;
    SInt32 size;
    SInt32 bufferSize;
    struct InitInfo* next;
} InitInfo;

TempNodeCB cinit_tempnodefunc;
InitInfo* cinit_initinfo;
static PooledString* cinit_stringlist;
static PooledString* cinit_pooledstringlist;
static PooledString* cinit_pooledwstringlist;
static ObjectList* cinit_tentative;
static TypeClass* cinit_loop_class;
static ENodeList* cinit_fdtnode;
static Boolean cinit_fdtambig;

#pragma pack(push, 2)
typedef struct CInit_NonConst {
    struct CInit_NonConst* next;
    Type* type;
    ENode* expr;
    SInt32 offset;
} CInit_NonConst;

typedef struct CInit_DataBuffer {
    struct CInit_DataBuffer* next;
    struct CInit_DataBuffer* root;
    char* buffer;
    SInt32 offset;
    SInt32 size;
    SInt32 bufferSize;
    SInt32 flexsize;
    CInit_NonConst* nonconst;
    OLinkList* list;
    Boolean allow_nonconst;
} CInit_DataBuffer;

typedef enum {
    StageStart,
    StageOpen,
    StageExpr,
    StageClose,
    StageEnd
} Stage;

typedef struct CInit_ParseState {
    ENode* expr;
    ENode myexpr;
    Stage stage;
    Boolean is_vector;
    SInt32 parens;
    Type* type;
} CInit_ParseState;
#pragma pack(pop)

static void CInit_InitType(
    CInit_DataBuffer* db, CInit_ParseState* ps, Type* type, UInt32 qual, Boolean errorflag
);
static void CInit_Type(Type* type, UInt32 qual, Boolean isTopLevel);

void CInit_Init(void) {
    cinit_tempnodefunc = NULL;
    cinit_initinfo = NULL;
    cinit_stringlist = NULL;
    cinit_pooledstringlist = NULL;
    cinit_pooledwstringlist = NULL;
    cinit_tentative = NULL;
}

static void CInit_SetupInitInfo(InitInfo* info, Object* obj) {
    memclrw(info, sizeof(InitInfo));
    info->obj = obj;
    info->next = cinit_initinfo;
    cinit_initinfo = info;
}

static void CInit_CleanupInitInfo(InitInfo* info) {
    cinit_initinfo = info->next;
}

static void CInit_SetupInitInfoBuffer(Type* type) {
    SInt32 size = type->size;
    cinit_initinfo->size = size;

    if (!size) {
        size = 512;
    } else if (size & 1) {
        size++;
    }

    cinit_initinfo->buffer = lalloc(size);
    cinit_initinfo->bufferSize = size;
    memclrw(cinit_initinfo->buffer, size);
}

static void CInit_SetData(void* data, SInt32 offset, SInt32 size) {
    SInt32 end;
    char* buffer;

    end = offset + size;
    if (end > cinit_initinfo->size) {
        cinit_initinfo->size = end;
    }

    if (end > cinit_initinfo->bufferSize) {
        if (cinit_initinfo->obj->type->size == 0) {
            if (end < 8000) {
                end += 0x400;
            } else {
                end += 0x4000;
            }
        }
        if (end & 1) {
            end++;
        }

        buffer = lalloc(end);
        memclrw(buffer, end);
        memcpy(buffer, cinit_initinfo->buffer, cinit_initinfo->bufferSize);
        cinit_initinfo->buffer = buffer;
        cinit_initinfo->bufferSize = end;
    }

    if (data) {
        memcpy(cinit_initinfo->buffer + offset, data, size);
    }
}

typedef struct CInit_Initializer {
    struct CInit_Initializer* next;
    struct CInit_Initializer* sublist;
    ENode* expr;
    TStreamElement element;
} CInit_Initializer;

static CInit_Initializer* CInit_ParseInitializerList(void) {
    CInit_Initializer* head;
    CInit_Initializer* tail;
    CInit_Initializer* item;

    if ((tk = lex()) == '}') {
        return NULL;
    }

    head = NULL;
    do {
        if (head) {
            item = lalloc(sizeof(CInit_Initializer));
            tail->next = item;
            tail = item;
        }
        if (!head) {
            head = tail = lalloc(sizeof(CInit_Initializer));
        }
        tail->next = NULL;

        if (tk == '{') {
            tail->element = *CPrep_CurStreamElement();
            tail->sublist = CInit_ParseInitializerList();
            tail->expr = NULL;
            tk = lex();
        } else {
            tail->sublist = NULL;
            tail->expr = conv_assignment_expression();
            tail->element = *CPrep_CurStreamElement();
        }

        if (tk == '}') {
            return head;
        }

        if (tk != ',') {
            CError_Error(ERR_COMMA_EXPECTED);
            return head;
        }
    } while ((tk = lex()) != '}');

    return head;
}

static CInit_Initializer* CInit_ParseInitializerClause(void) {
    CInit_Initializer* init;

    init = lalloc(sizeof(CInit_Initializer));
    init->next = NULL;
    if (tk != '{') {
        init->sublist = NULL;
        init->expr = conv_assignment_expression();
        init->element = *CPrep_CurStreamElement();
    } else {
        init->element = *CPrep_CurStreamElement();
        init->expr = NULL;
        init->sublist = CInit_ParseInitializerList();
        tk = lex();
    }

    return init;
}

static ENode* CInit_ParseInitializer(ENode* expr) {
    CInt64 save_int;
    Float save_float;
    SInt32 save_size;
    short t;

    switch (tk) {
        case TK_INTCONST:
        case TK_FLOATCONST:
            save_int = tkintconst;
            save_float = tkfloatconst;
            save_size = tksize;
            t = lookahead();
            tkintconst = save_int;
            tkfloatconst = save_float;
            tksize = save_size;

            switch (t) {
                case ',':
                case ';':
                case '}':
                    memclrw(expr, sizeof(ENode));
                    switch (tk) {
                        case TK_INTCONST:
                            expr->type = EINTCONST;
                            expr->rtype = atomtype();
                            expr->data.intval = tkintconst;
                            break;
                        case TK_FLOATCONST:
                            expr->type = EFLOATCONST;
                            expr->rtype = atomtype();
                            expr->data.floatval = tkfloatconst;
                            break;
                    }
                    tk = lex();
                    CPrep_TokenStreamFlush();
                    return expr;
            }
    }

    expr = assignment_expression();
    CPrep_TokenStreamFlush();
    return expr;
}

static Stage CInit_ParseNextInit(CInit_ParseState* ps) {
    DeclInfo di;
    short t;

    ps->expr = NULL;
    if (tk == ';') {
        ps->stage = StageEnd;
        return ps->stage;
    }
    switch (ps->stage) {
        case StageStart:
            if (ps->is_vector) {
                if (tk == '(') {
                    tk = lex();
                    CParser_GetDeclSpecs(&di, TRUE);
                    ps->type = di.thetype;
                    if (tk == ')') {
                        tk = lex();
                    } else {
                        CError_Error(ERR_RPAREN_EXPECTED);
                    }

                    if (tk == '(') {
                        tk = lex();
                    } else {
                        CError_Error(ERR_LPAREN_EXPECTED);
                    }
                    ps->parens++;
                    t = lookahead();
                    if (t == TK_UU_VECTOR ||
                        (t == TK_IDENTIFIER && !strcmp("vector", tkidentifier->name))) {
                        CInit_ParseNextInit(ps);
                    }
                    ps->stage = StageOpen;
                    return ps->stage;
                }
            } else {
                if (tk == '{') {
                    tk = lex();
                    ps->parens = 0;
                    ps->stage = StageOpen;
                    return ps->stage;
                }
            }
            ps->expr = CInit_ParseInitializer(&ps->myexpr);
            ps->stage = StageExpr;
            return ps->stage;
        case StageOpen:
            break;
        case StageExpr:
        case StageClose:
            if (tk == ',') {
                tk = lex();
                break;
            }
            if (ps->parens) {
                if (tk != ')') {
                    CError_Error(ERR_ILLEGAL_INITIALIZATION);
                }
                if (ps->parens > 1) {
                    ps->parens--;
                    tk = lex();
                    CInit_ParseNextInit(ps);
                }
            } else {
                if (tk != '}') {
                    CError_Error(ERR_ILLEGAL_INITIALIZATION);
                }
            }
            ps->stage = StageClose;
            return ps->stage;
        default:
            CError_FATAL(389);
    }

    switch (tk) {
        case '{':
            tk = lex();
            ps->stage = StageOpen;
            return ps->stage;
        case '}':
            ps->stage = StageClose;
            return ps->stage;
        case '(':
            if (ps->is_vector) {
                tk = lex();
                ps->stage = StageOpen;
                return ps->stage;
            }
        case ')':
            if (ps->is_vector && ps->parens) {
                if (ps->parens > 1) {
                    ps->parens--;
                    tk = lex();
                    CInit_ParseNextInit(ps);
                }
                ps->stage = StageClose;
                return ps->stage;
            }
        default:
            ps->expr = CInit_ParseInitializer(&ps->myexpr);
            ps->stage = StageExpr;
            return ps->stage;
    }
}

static void CInit_CloseInitList(void) {
    if (tk == ',' && copts.cplusplus) {
        tk = lex();
    }

    if (tk != '}') {
        CError_ErrorSkip(ERR_RBRACE_EXPECTED);
    } else {
        tk = lex();
    }
}

static Boolean CInit_IsAllZero(char* buf, SInt32 size) {
    SInt32 i;

    if (copts.explicit_zero_data) {
        return FALSE;
    }

    for (i = 0; i < size; i++) {
        if (buf[i]) {
            return FALSE;
        }
    }

    return TRUE;
}

static Boolean CInit_ClassNeedsConstruction(TypeClass* tclass) {
    return CClass_Constructor(tclass) || CClass_Destructor(tclass);
}

static Boolean CInit_IsSimpleStructArrayInit(Type* type) {
    switch (type->type) {
        case TYPESTRUCT:
            return TRUE;
        case TYPEARRAY:
            while (IS_TYPE_ARRAY(type)) {
                type = TYPE_POINTER(type)->target;
            }
            if (!IS_TYPE_CLASS(type)) {
                return TRUE;
            }
        case TYPECLASS:
            return !CInit_ClassNeedsConstruction(TYPE_CLASS(type));
        default:
            return FALSE;
    }
}

static Boolean CInit_IsSimpleInit(Type* type) {
    switch (type->type) {
        case TYPEPOINTER:
            return (TYPE_POINTER(type)->qual & Q_REFERENCE) == 0;
        case TYPEARRAY:
            while (IS_TYPE_ARRAY(type)) {
                type = TYPE_POINTER(type)->target;
            }
            if (!IS_TYPE_CLASS(type)) {
                return TRUE;
            }
        case TYPECLASS:
            return !CInit_ClassNeedsConstruction(TYPE_CLASS(type));
        default:
            return TRUE;
    }
}

static Object* CInit_GetInitObject(Object* obj) {
    if (obj->datatype == DALIAS) {
        CError_ASSERT(521, !obj->u.alias.offset);
        obj = obj->u.alias.object;
    }
    return obj;
}

static Object* CInit_CreateStaticDataObject(Type* type, UInt32 qual, HashNameNode* name) {
    Object* obj;
    DeclInfo di;

    memclrw(&di, sizeof(DeclInfo));
    di.thetype = type;
    di.name = name ? name : CParser_GetUniqueName();
    di.qual = qual;
    di.storageclass = TK_STATIC;
    di.is_extern_c = TRUE;

    obj = CParser_NewGlobalDataObject(&di);
    obj->nspace = cscope_root;
    return obj;
}

static Type* CInit_GetRegMemType(void) {
    return CDecl_NewStructType(void_ptr.size * 3, CMach_GetTypeAlign(TYPE(&void_ptr)));
}

static Object* CInit_CreateStaticData(Type* type) {
    Object* obj = CInit_CreateStaticDataObject(type, 0, NULL);
    CodeGen_UpdateObject(obj);
    CInit_DeclareData(obj, NULL, NULL, obj->type->size);
    return obj;
}

static void CInit_InitNonConst(CInit_DataBuffer* db, Type* type, ENode* expr) {
    CInit_NonConst* entry;
    CInit_NonConst* scan;
    MWVector128* vec;

    if (db->root->allow_nonconst || IS_TYPE_VECTOR(type)) {
        if (IS_TYPE_VECTOR(type) && ENODE_IS(expr, EVECTOR128CONST)) {
            vec = (MWVector128*)(db->buffer + db->size);
            *vec = expr->data.vector128val;
            CMach_InitVectorMem(type, *vec, vec, TRUE);
        }

        entry = lalloc(sizeof(CInit_NonConst));
        memclrw(entry, sizeof(CInit_NonConst));
        entry->next = NULL;
        entry->type = type;
        entry->expr = expr;
        entry->offset = db->offset + db->size;
        if ((scan = db->root->nonconst)) {
            while (scan->next) {
                scan = scan->next;
            }
            scan->next = entry;
        } else {
            db->root->nonconst = entry;
        }
    } else {
        CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
    }
}

static CInit_DataBuffer* CInit_GrowBuffer(CInit_DataBuffer* db, SInt32 size) {
    CInit_DataBuffer* newbuf;

    newbuf = lalloc(sizeof(CInit_DataBuffer));
    memclrw(newbuf, sizeof(CInit_DataBuffer));
    newbuf->root = db->root;
    newbuf->buffer = lalloc(size);
    newbuf->offset = db->offset + db->size;
    newbuf->bufferSize = size;
    db->next = newbuf;
    memset(newbuf->buffer, 0, newbuf->bufferSize);
    return newbuf;
}

Boolean CInit_RelocInitCheck(ENode* expr, Object** objptr, CInt64* valptr, Boolean allowLocal) {
    Object* objcheck1;
    Object* objcheck2;
    CInt64 valcheck1;
    CInt64 valcheck2;

    *objptr = NULL;
    valptr->lo = 0;
    valptr->hi = 0;

    while (1) {
        switch (expr->type) {
            case EINTCONST:
                *valptr = expr->data.intval;
                return TRUE;
            case EOBJREF:
                objcheck1 = CInit_GetInitObject(expr->data.objref);
                if (objcheck1->datatype == DLOCAL && !allowLocal) {
                    return FALSE;
                }
                *objptr = objcheck1;
                return TRUE;
            case ESTRINGCONST:
                CInit_RewriteString(expr, FALSE);
                continue;
            case ETYPCON:
                do {
                    if (expr->rtype->size != expr->data.monadic->rtype->size) {
                        return FALSE;
                    }
                    expr = expr->data.monadic;
                    if (!IS_TYPE_POINTER_ONLY(expr->rtype) && !IS_TYPE_INT(expr->rtype)) {
                        return FALSE;
                    }
                } while (ENODE_IS(expr, ETYPCON));
                continue;
            case EADD:
                if (!CInit_RelocInitCheck(
                        expr->data.diadic.left, &objcheck1, &valcheck1, allowLocal
                    )) {
                    return FALSE;
                }
                if (!CInit_RelocInitCheck(
                        expr->data.diadic.right, &objcheck2, &valcheck2, allowLocal
                    )) {
                    return FALSE;
                }

                if (objcheck1) {
                    if (objcheck2) {
                        return FALSE;
                    }
                    *objptr = objcheck1;
                } else {
                    *objptr = objcheck1;
                }

                *valptr = CMach_CalcIntDiadic(TYPE(&stunsignedlong), valcheck1, '+', valcheck2);
                return TRUE;
            case ESUB:
                if (!CInit_RelocInitCheck(
                        expr->data.diadic.left, &objcheck1, &valcheck1, allowLocal
                    )) {
                    return FALSE;
                }
                if (!CInit_RelocInitCheck(
                        expr->data.diadic.right, &objcheck2, &valcheck2, allowLocal
                    )) {
                    return FALSE;
                }

                if (objcheck2) {
                    return FALSE;
                }

                *objptr = objcheck1;
                *valptr = CMach_CalcIntDiadic(TYPE(&stunsignedlong), valcheck1, '-', valcheck2);
                return TRUE;
            default:
                return FALSE;
        }
    }
}

static void CInit_InitTypePointer(
    CInit_DataBuffer* db, ENode* expr, TypePointer* tptr, UInt32 qual
) {
    Object* obj;
    CInt64 val;
    OLinkList* list;

    expr = CExpr_AssignmentPromotion(expr, TYPE(tptr), qual & (Q_CONST | Q_VOLATILE), TRUE);
    if (IS_TYPE_POINTER_ONLY(expr->rtype) || ENODE_IS(expr, EINTCONST)) {
        if (CInit_RelocInitCheck(expr, &obj, &val, FALSE)) {
            if (obj) {
                list = lalloc(sizeof(OLinkList));
                list->next = db->root->list;
                list->obj = obj;
                list->addend = CInt64_GetULong(&val);
                list->offset = db->offset + db->size;
                db->root->list = list;
            } else {
                CMach_InitIntMem(TYPE(&stunsignedlong), val, db->buffer + db->size);
            }
        } else {
            CInit_InitNonConst(db, TYPE(tptr), expr);
        }
    } else {
        CError_Error(ERR_ILLEGAL_INITIALIZATION);
    }
}

static void CInit_InitTypeInt(CInit_DataBuffer* db, ENode* expr, TypeIntegral* tint, UInt32 qual) {
    expr = CExpr_AssignmentPromotion(expr, TYPE(tint), qual & (Q_CONST | Q_VOLATILE), TRUE);
    if (IS_TYPE_INT(expr->rtype)) {
        if (ENODE_IS(expr, EINTCONST)) {
            CMach_InitIntMem(TYPE(tint), expr->data.intval, db->buffer + db->size);
        } else if (ENODE_IS(expr, ETYPCON) &&
                   IS_TYPE_POINTER_ONLY(expr->data.monadic->rtype) &&
                   expr->rtype->size == 4 &&
                   (copts.cplusplus || !copts.ANSIstrict)) {
            CInit_InitTypePointer(
                db, expr->data.monadic, TYPE_POINTER(expr->data.monadic->rtype), qual
            );
        } else {
            CInit_InitNonConst(db, TYPE(tint), expr);
        }
    } else {
        CError_Error(ERR_ILLEGAL_INITIALIZATION);
    }
}

static void CInit_InitTypeFloat(CInit_DataBuffer* s, ENode* expr, TypeIntegral* tint, UInt32 qual) {
    expr = CExpr_AssignmentPromotion(expr, TYPE(tint), qual & (Q_CONST | Q_VOLATILE), TRUE);
    if (IS_TYPE_FLOAT(expr->rtype)) {
        if (ENODE_IS(expr, EFLOATCONST)) {
            CMach_InitFloatMem(TYPE(tint), expr->data.floatval, s->buffer + s->size);
        } else {
            CInit_InitNonConst(s, TYPE(tint), expr);
        }
    } else {
        CError_Error(ERR_ILLEGAL_INITIALIZATION);
    }
}

static void CInit_InitTypeEnum(CInit_DataBuffer* db, ENode* expr, TypeEnum* tenum, UInt32 qual) {
    expr = CExpr_AssignmentPromotion(expr, TYPE(tenum), qual & (Q_CONST | Q_VOLATILE), TRUE);
    if (IS_TYPE_ENUM(expr->rtype)) {
        if (ENODE_IS(expr, EINTCONST)) {
            CMach_InitIntMem(tenum->enumtype, expr->data.intval, db->buffer + db->size);
        } else {
            CInit_InitNonConst(db, TYPE(tenum), expr);
        }
    } else {
        CError_Error(ERR_ILLEGAL_INITIALIZATION);
    }
}

static void CInit_InitTypeMemberPointer(
    CInit_DataBuffer* db, ENode* expr, TypeMemberPointer* tmptr, UInt32 qual
) {
    expr = CExpr_AssignmentPromotion(expr, TYPE(tmptr), qual & (Q_CONST | Q_VOLATILE), TRUE);
    if (ENODE_IS(expr, EINTCONST)) {
        CMach_InitIntMem(TYPE(&stsignedlong), expr->data.intval, db->buffer + db->size);
    } else {
        CInit_InitNonConst(db, TYPE(tmptr), expr);
    }
}

static void CInit_SetBitfield(TypeBitfield* tbitfield, UInt8* buffer, CInt64 val) {
    int i;
    int pos;
    int step;

    if (copts.littleendian) {
        pos = tbitfield->offset;
        step = 1;
    } else {
        pos = tbitfield->bitlength + tbitfield->offset - 1;
        step = -1;
    }
    for (i = 0; i < tbitfield->bitlength; i++, pos += step) {
        if (CInt64_GetULong(&val) & 1) {
            if (copts.littleendian) {
                buffer[pos >> 3] |= 1 << (pos & 7);
            } else {
                buffer[pos >> 3] |= 0x80 >> (pos & 7);
            }
        }
        val = CInt64_ShrU(val, cint64_one);
    }
}

static void CInit_InitTypeBitfield(
    CInit_DataBuffer* db, ENode* expr, TypeBitfield* tbitfield, UInt32 qual
) {
    Type* inner;

    inner = tbitfield->bitfieldtype;
    if (IS_TYPE_ENUM(inner)) {
        inner = TYPE_ENUM(inner)->enumtype;
    }
    expr = CExpr_AssignmentPromotion(expr, inner, qual & (Q_CONST | Q_VOLATILE), TRUE);

    if (IS_TYPE_INT(expr->rtype)) {
        if (ENODE_IS(expr, EINTCONST)) {
            CInit_SetBitfield(tbitfield, (UInt8*)db->buffer + db->size, expr->data.intval);
        } else {
            CInit_InitNonConst(db, TYPE(tbitfield), expr);
        }
    } else {
        CError_Error(ERR_ILLEGAL_INITIALIZATION);
    }
}

static void CInit_InitTypeArray(
    CInit_DataBuffer* db, CInit_ParseState* ps, TypePointer* tptr, UInt32 qual, Boolean errorflag
) {
    SInt32 targetsize;
    SInt32 start;
    SInt32 i;
    Boolean braced;
    Boolean is_zero_size;
    SInt32 size;
    Boolean is_char_ptr;
    Boolean is_wchar_ptr;

    is_zero_size = tptr->size == 0;
    if (!(targetsize = tptr->target->size)) {
        CError_Error(ERR_DATA_TYPE_INCOMPLETE);
        return;
    }

    is_char_ptr = IS_TYPE_INT(tptr->target) && (tptr->target->size == 1);
    is_wchar_ptr = IS_TYPE_INT(tptr->target) && (tptr->target->size == stwchar.size);
    switch (ps->stage) {
        case StageOpen:
            braced = TRUE;
            if (CInit_ParseNextInit(ps) == StageClose) {
                if (is_zero_size) {
                    CError_Error(ERR_ILLEGAL_INITIALIZATION);
                }
                tk = lex();
                return;
            }
            break;
        case StageExpr:
            braced = FALSE;
            break;
        default:
            break;
    }
    switch (ps->stage) {
        case StageOpen:
        case StageExpr:
            break;
        default:
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
            return;
    }

    if (ps->stage == StageExpr) {
        ps->expr = pointer_generation(ps->expr);
    }

    if (ps->stage == StageExpr &&
        ENODE_IS(ps->expr, ESTRINGCONST) &&
        (is_char_ptr || is_wchar_ptr)) {
        if (IS_TYPE_POINTER_ONLY(ps->expr->rtype) &&
            tptr->target->size != TYPE_POINTER(ps->expr->rtype)->target->size) {
            CError_Warning(ERR_ILLEGAL_INITIALIZATION);
        }
        size = ps->expr->data.string.size;
        if (is_zero_size) {
            tptr->size = ps->expr->data.string.size;
            if (db->bufferSize < size) {
                db = CInit_GrowBuffer(db, size);
            }
            memcpy(db->buffer, ps->expr->data.string.data, size);
            db->size = size;
        } else {
            if (ps->expr->data.string.size > tptr->size) {
                if (copts.cplusplus || (ps->expr->data.string.size - 1) > tptr->size) {
                    CError_Error(ERR_TOO_MANY_INITIALIZERS);
                }
                ps->expr->data.string.size = tptr->size;
                size = tptr->size;
            }
            memcpy(db->buffer + db->size, ps->expr->data.string.data, size);
        }
    } else {
        if (!braced && errorflag) {
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
            return;
        }

        start = db->size;
        i = 0;
        while (1) {
            if (is_zero_size) {
                size = (i + 1) * targetsize;
                db->size = start + size - targetsize - db->offset;
                if (db->size + targetsize > db->bufferSize) {
                    db = CInit_GrowBuffer(db, targetsize * 16);
                }
                CInit_InitType(db, ps, tptr->target, qual, FALSE);
                tptr->size = size;
                db->size = start + size - db->offset;
            } else {
                if (tptr->size <= i * targetsize) {
                    i--;
                    CError_Error(ERR_TOO_MANY_INITIALIZERS);
                }
                db->size = start + i * targetsize;
                CInit_InitType(db, ps, tptr->target, qual, FALSE);
                if (!braced && tptr->size <= (i + 1) * targetsize) {
                    break;
                }
            }

            switch (CInit_ParseNextInit(ps)) {
                case StageOpen:
                case StageExpr:
                    break;
                case StageClose:
                    if (braced) {
                        tk = lex();
                    }
                    return;
                default:
                    CError_Error(ERR_RBRACE_EXPECTED);
                    return;
            }

            i++;
        }
    }

    if (braced) {
        switch (CInit_ParseNextInit(ps)) {
            case StageClose:
                tk = lex();
                return;
            case StageExpr:
                CError_Error(ERR_TOO_MANY_INITIALIZERS);
                return;
            default:
                CError_Error(ERR_RBRACE_EXPECTED);
                return;
        }
    }
}

static void CInit_InitTypeStruct(
    CInit_DataBuffer* db,
    CInit_ParseState* ps,
    const TypeStruct* tstruct,
    UInt32 qual,
    Boolean errorflag
) {
    StructMember* member;
    SInt32 start;
    Boolean braced;
    SInt32 count;
    TypePointer arraytype;
    int i;

    count = 0;
    if (ps->type) {
        tstruct = TYPE_STRUCT(ps->type);
    }

    if (!(member = tstruct->members)) {
        CError_Error(ERR_DATA_TYPE_INCOMPLETE);
        return;
    }

    switch (ps->stage) {
        case StageOpen:
            braced = TRUE;
            if (CInit_ParseNextInit(ps) == StageClose) {
                tk = lex();
                return;
            }
            break;
        case StageExpr:
            braced = FALSE;
            break;
        default:
            break;
    }

    switch (ps->stage) {
        case StageOpen:
        case StageExpr:
            break;
        default:
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
            return;
    }

    if (!braced && ps->stage == StageExpr && (errorflag || ps->expr->rtype == TYPE(tstruct))) {
        ps->expr = CExpr_AssignmentPromotion(ps->expr, TYPE(tstruct), qual, TRUE);
        if (IS_TYPE_STRUCT(ps->expr->rtype)) {
            CInit_InitNonConst(db, TYPE(tstruct), ps->expr);
        }
        return;
    }

    start = db->size;
    while (1) {
        db->size = start + member->offset;
        if (!member->type->size) {
            if (!errorflag || !IS_TYPE_ARRAY(member->type)) {
                CError_Error(ERR_TOO_MANY_INITIALIZERS);
                if (!IS_TYPE_ARRAY(member->type)) {
                    return;
                }
            }

            arraytype = *TYPE_POINTER(member->type);
            CInit_InitTypeArray(db, ps, &arraytype, member->qual, TRUE);
            db->flexsize = arraytype.size;
        } else {
            CInit_InitType(db, ps, member->type, member->qual, FALSE);
        }

        count++;
        if (IS_TYPESTRUCT_VECTOR(tstruct) && ps->expr) {
            CError_ASSERT(1218, !ENODE_IS(ps->expr, EVECTOR128CONST));
        }

        do {
            member = member->next;
        } while (member && (member->qual & Q_WEAK));

        if (!member || tstruct->stype == STRUCT_TYPE_UNION) {
            if (braced) {
                switch (CInit_ParseNextInit(ps)) {
                    case StageClose:
                        if (IS_TYPESTRUCT_VECTOR(tstruct)) {
                            CMach_InitVectorMem(
                                TYPE(tstruct),
                                *(MWVector128*)(db->buffer + start),
                                (MWVector128*)(db->buffer + start),
                                FALSE
                            );
                        }
                        tk = lex();
                        return;
                    case StageExpr:
                        CError_Error(ERR_TOO_MANY_INITIALIZERS);
                        return;
                    default:
                        CError_Error(ERR_RBRACE_EXPECTED);
                        return;
                }
            }
            return;
        } else {
            switch (CInit_ParseNextInit(ps)) {
                case StageOpen:
                case StageExpr:
                    continue;
                case StageClose:
                    if (braced) {
                        tk = lex();
                    }
                    if (IS_TYPESTRUCT_VECTOR(tstruct)) {
                        switch (TYPE_STRUCT(tstruct)->stype) {
                            case STRUCT_VECTOR_UCHAR:
                            case STRUCT_VECTOR_SCHAR:
                            case STRUCT_VECTOR_BCHAR:
                                if (count != 16) {
                                    if (count == 1) {
                                        UInt8 *p, val;
                                        p = (UInt8*)db->buffer;
                                        val = p[0];
                                        for (i = 1; i < 16; i++) {
                                            p[i] = val;
                                        }
                                    } else {
                                        CError_Error(ERR_ILLEGAL_INITIALIZATION);
                                    }
                                }
                                break;
                            case STRUCT_VECTOR_USHORT:
                            case STRUCT_VECTOR_SSHORT:
                            case STRUCT_VECTOR_BSHORT:
                            case STRUCT_VECTOR_PIXEL:
                                if (count != 8) {
                                    if (count == 1) {
                                        SInt16 *p, val;
                                        p = (SInt16*)db->buffer;
                                        val = p[0];
                                        for (i = 1; i < 8; i++) {
                                            p[i] = val;
                                        }
                                    } else {
                                        CError_Error(ERR_ILLEGAL_INITIALIZATION);
                                    }
                                }
                                break;
                            case STRUCT_VECTOR_UINT:
                            case STRUCT_VECTOR_SINT:
                            case STRUCT_VECTOR_BINT:
                            case STRUCT_VECTOR_FLOAT:
                                if (count != 4) {
                                    if (count == 1) {
                                        UInt32 *p, val;
                                        p = (UInt32*)db->buffer;
                                        val = p[0];
                                        for (i = 1; i < 4; i++) {
                                            p[i] = val;
                                        }
                                    } else {
                                        CError_Error(ERR_ILLEGAL_INITIALIZATION);
                                    }
                                }
                                break;
                        }
                    }
                    return;
                default:
                    CError_Error(ERR_ILLEGAL_INITIALIZATION);
                    return;
            }
        }
    }
}

static ObjMemberVar* CInit_FindNextMember(ObjMemberVar* ivar) {
    ObjMemberVar* scan = ivar;
    while (1) {
        scan = scan->next;
        if (!scan) {
            return NULL;
        }
        if (!scan->anonunion) {
            return scan;
        }
        if (scan->offset > ivar->offset) {
            return scan;
        }
        if (IS_TYPE_BITFIELD(scan->type) &&
            IS_TYPE_BITFIELD(ivar->type) &&
            TYPE_BITFIELD(scan->type)->offset != TYPE_BITFIELD(ivar->type)->offset) {
            return scan;
        }
    }
}

static void CInit_InitTypeClass(
    CInit_DataBuffer* db, CInit_ParseState* ps, TypeClass* tclass, UInt32 qual, Boolean errorflag
) {
    ObjMemberVar* ivar;
    SInt32 start;
    Boolean braced;
    UInt32 last_offset;
    TypePointer arraytype;

    if (tclass->bases || tclass->vtable) {
        CError_Error(ERR_ILLEGAL_INITIALIZATION);
        return;
    }

    switch (ps->stage) {
        case StageOpen:
            braced = TRUE;
            if (CInit_ParseNextInit(ps) == StageClose) {
                tk = lex();
                return;
            }
            break;
        case StageExpr:
            braced = FALSE;
            break;
        default:
            break;
    }

    switch (ps->stage) {
        case StageOpen:
        case StageExpr:
            break;
        default:
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
            return;
    }

    if (!braced &&
        ps->stage == StageExpr &&
        (errorflag ||
         ps->expr->rtype == TYPE(tclass) ||
         CExpr_CanImplicitlyConvert(ps->expr, TYPE(tclass), 0))) {
        ps->expr = CExpr_AssignmentPromotion(ps->expr, TYPE(tclass), qual, TRUE);
        if (IS_TYPE_CLASS(ps->expr->rtype)) {
            CInit_InitNonConst(db, TYPE(tclass), ps->expr);
        }
        return;
    }

    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
        if (ivar->access != ACCESSPUBLIC) {
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
            break;
        }
    }

    if (!(ivar = tclass->ivars)) {
        CError_Error(ERR_TOO_MANY_INITIALIZERS);
        return;
    }
    start = db->size;
    while (1) {
        db->size = start + ivar->offset;
        if (!ivar->type->size) {
            if (!errorflag || !IS_TYPE_ARRAY(ivar->type)) {
                CError_Error(ERR_TOO_MANY_INITIALIZERS);
                if (!IS_TYPE_ARRAY(ivar->type)) {
                    return;
                }
            }

            arraytype = *TYPE_POINTER(ivar->type);
            CInit_InitTypeArray(db, ps, &arraytype, ivar->qual, TRUE);
            db->flexsize = arraytype.size;
        } else {
            CInit_InitType(db, ps, ivar->type, ivar->qual, FALSE);
        }

        last_offset = ivar->offset;
        if (!(ivar = CInit_FindNextMember(ivar)) ||
            (tclass->mode == CLASS_MODE_UNION && ivar->offset == last_offset)) {
            if (braced) {
                switch (CInit_ParseNextInit(ps)) {
                    case StageClose:
                        tk = lex();
                        return;
                    case StageExpr:
                        CError_Error(ERR_TOO_MANY_INITIALIZERS);
                        return;
                    default:
                        CError_Error(ERR_RBRACE_EXPECTED);
                        return;
                }
            }
            return;
        } else {
            switch (CInit_ParseNextInit(ps)) {
                case StageOpen:
                case StageExpr:
                    continue;
                case StageClose:
                    if (braced) {
                        tk = lex();
                    }
                    return;
                default:
                    CError_Error(ERR_ILLEGAL_INITIALIZATION);
                    return;
            }
        }
    }
}

static void CInit_InitType(
    CInit_DataBuffer* db, CInit_ParseState* ps, Type* type, UInt32 qual, Boolean errorflag
) {
    Boolean braced;

    switch (type->type) {
        case TYPEVOID:
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
            return;
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEENUM:
        case TYPEBITFIELD:
        case TYPEMEMBERPOINTER:
        case TYPEPOINTER:
            switch (ps->stage) {
                case StageOpen:
                    braced = TRUE;
                    CInit_ParseNextInit(ps);
                    break;
                case StageExpr:
                    braced = FALSE;
                    break;
                default:
                    break;
            }
            if (ps->stage != StageExpr) {
                CError_Error(ERR_ILLEGAL_INITIALIZATION);
                return;
            }

            switch (type->type) {
                case TYPEINT:
                    CInit_InitTypeInt(db, ps->expr, TYPE_INTEGRAL(type), qual);
                    break;
                case TYPEFLOAT:
                    CInit_InitTypeFloat(db, ps->expr, TYPE_INTEGRAL(type), qual);
                    break;
                case TYPEENUM:
                    CInit_InitTypeEnum(db, ps->expr, TYPE_ENUM(type), qual);
                    break;
                case TYPEPOINTER:
                    CInit_InitTypePointer(db, ps->expr, TYPE_POINTER(type), qual);
                    break;
                case TYPEMEMBERPOINTER:
                    CInit_InitTypeMemberPointer(db, ps->expr, TYPE_MEMBER_POINTER(type), qual);
                    break;
                case TYPEBITFIELD:
                    CInit_InitTypeBitfield(db, ps->expr, TYPE_BITFIELD(type), qual);
                    break;
                default:
                    CError_FATAL(1542);
            }

            if (braced) {
                switch (CInit_ParseNextInit(ps)) {
                    case StageClose:
                        tk = lex();
                        break;
                    case StageExpr:
                        CError_Error(ERR_TOO_MANY_INITIALIZERS);
                        break;
                    default:
                        CError_Error(ERR_RBRACE_EXPECTED);
                }
            }
            break;
        case TYPESTRUCT:
            CInit_InitTypeStruct(db, ps, TYPE_STRUCT(type), qual, errorflag);
            break;
        case TYPEARRAY:
            CInit_InitTypeArray(db, ps, TYPE_POINTER(type), qual, errorflag);
            break;
        case TYPECLASS:
            CInit_InitTypeClass(db, ps, TYPE_CLASS(type), qual, errorflag);
            break;
        default:
            CError_FATAL(1573);
    }
}

static void CInit_InitData(CInit_DataBuffer* db, Type* type, UInt32 qual, Boolean allowNonConst) {
    CInit_ParseState ps;
    SInt32 size;
    CInit_DataBuffer* chunk;
    char* joined;

    locklheap();
    memclrw(db, sizeof(CInit_DataBuffer));
    db->root = db;
    if (type->size == 0) {
        if (IS_TYPE_ARRAY(type)) {
            db->bufferSize = 16 * TYPE_POINTER(type)->target->size;
        } else {
            CError_Error(ERR_DATA_TYPE_INCOMPLETE);
        }
    } else {
        db->bufferSize = type->size;
    }

    db->buffer = lalloc(db->bufferSize);
    memset(db->buffer, 0, db->bufferSize);
    db->allow_nonconst = allowNonConst;

    ps.stage = StageStart;
    ps.is_vector = FALSE;
    if (IS_TYPE_VECTOR(type)) {
        ps.is_vector = TRUE;
        db->allow_nonconst = TRUE;
    }
    if (IS_TYPE_ARRAY(type) && IS_TYPE_VECTOR(TYPE_POINTER(type)->target)) {
        db->allow_nonconst = TRUE;
    }

    ps.type = NULL;
    ps.parens = 0;
    CInit_ParseNextInit(&ps);
    CInit_InitType(db, &ps, type, qual, TRUE);

    if ((size = type->size + db->flexsize)) {
        if (db->next) {
            joined = lalloc(size);
            for (chunk = db; chunk; chunk = chunk->next) {
                CError_ASSERT(1647, (chunk->offset + chunk->size) <= size);
                memcpy(joined + chunk->offset, chunk->buffer, chunk->size);
            }
            db->buffer = joined;
        }
    } else {
        CError_Error(ERR_ILLEGAL_INITIALIZATION);
    }

    db->size = size;
    db->next = NULL;
    unlocklheap();
}

static ENode* CInit_InitConcat(ENode* prev, ENode* base, SInt32 offset, Type* type, ENode* value) {
    ENode* assign;
    ENode* addr;
    ENode* lvalue;

    addr = lalloc(sizeof(ENode));
    *addr = *base;
    if (offset) {
        addr = makediadicnode(addr, intconstnode(TYPE(&stunsignedlong), offset), EADD);
    }

    if (IS_TYPE_BITFIELD(type)) {
        lvalue = makemonadicnode(addr, EBITFIELD);
        lvalue->rtype = type;
        lvalue = makemonadicnode(lvalue, EINDIRECT);
        lvalue->rtype = TYPE_BITFIELD(type)->bitfieldtype;
    } else {
        lvalue = makemonadicnode(addr, EINDIRECT);
        lvalue->rtype = type;
    }

    assign = makediadicnode(lvalue, value, EASS);
    if (!prev) {
        return assign;
    } else {
        prev = makediadicnode(prev, assign, ECOMMA);
        prev->rtype = assign->rtype;
        return prev;
    }
}

static ENode* CInit_RegisterDtorObject(Type* type, Object* dtor, ENode* objexpr) {
    ENode* expr;

    if (copts.no_static_dtors) {
        return objexpr;
    }

    expr = lalloc(sizeof(ENode));
    expr->type = EFUNCCALL;
    expr->cost = 4;
    expr->flags = 0;
    expr->rtype = CDecl_NewPointerType(type);
    expr->data.funccall.funcref = create_objectrefnode(Xgreg_func);
    expr->data.funccall.functype = TYPE_FUNC(Xgreg_func->type);
    expr->data.funccall.args = lalloc(sizeof(ENodeList));
    expr->data.funccall.args->node = objexpr;
    expr->data.funccall.args->next = lalloc(sizeof(ENodeList));
    expr->data.funccall.args->next->node =
        create_objectrefnode(CABI_GetDestructorObject(dtor, CABIDestroyComplete));
    expr->data.funccall.args->next->next = lalloc(sizeof(ENodeList));
    expr->data.funccall.args->next->next->node =
        create_objectrefnode(CInit_CreateStaticData(CInit_GetRegMemType()));
    expr->data.funccall.args->next->next->next = NULL;

    return expr;
}

static Boolean CInit_ConstructGlobalObject(
    Object* obj, TypeClass* tclass, ENode* valueexpr, SInt32 offset, Boolean parseInit
) {
    NameSpaceObjectList* ctor;
    Object* dtor;
    ENodeList* list;
    ENode* expr;
    Boolean ctorflag;

    ctor = CClass_Constructor(tclass);
    dtor = CClass_Destructor(tclass);
    if (!ctor && !dtor) {
        return FALSE;
    }

    if (parseInit && !ctor && tk == '=' && lookahead() == '{') {
        return FALSE;
    }

    if (parseInit && tk == '(') {
        tk = lex();
        list = CExpr_ScanExpressionList(TRUE);
        if (tk == ')') {
            tk = lex();
        } else {
            CError_Error(ERR_RPAREN_EXPECTED);
        }
    } else if (valueexpr) {
        list = lalloc(sizeof(ENodeList));
        list->node = valueexpr;
        list->next = NULL;
    } else {
        list = NULL;
    }

    expr = create_objectrefnode(obj);
    if (offset) {
        expr = makediadicnode(expr, intconstnode(TYPE(&stunsignedlong), offset), EADD);
    }

    if (ctor) {
        ctorflag = TRUE;
        if (tk == '=') {
            ctorflag = FALSE;
            if (list) {
                CError_Error(ERR_ILLEGAL_INITIALIZATION);
            }

            list = lalloc(sizeof(ENodeList));
            list->next = NULL;
            tk = lex();
            list->node = conv_assignment_expression();
        }

        expr = CExpr_ConstructObject(tclass, expr, list, FALSE, TRUE, FALSE, TRUE, ctorflag);
        if (expr->rtype->type != TYPEPOINTER) {
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
            return TRUE;
        }
    } else {
        if (list) {
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
        }
    }

    if (dtor) {
        expr = CInit_RegisterDtorObject(TYPE(tclass), dtor, expr);
    }

    if (cinit_initinfo->use_register_cb) {
        cinit_initinfo->init_expr_register_cb(expr);
    } else {
        InitExpr_Register(expr, obj);
    }

    return TRUE;
}

static Boolean CInit_ConstructAutoObject(
    TypeClass* tclass, ENode* expr, SInt32 offset, Boolean parseInit
) {
    ENodeList* list;
    ENode* objexpr;
    NameSpaceObjectList* ctor;
    Object* dtor;
    Boolean ctorflag;

    ctor = CClass_Constructor(tclass);
    dtor = CClass_Destructor(tclass);
    if (!ctor && !dtor) {
        return FALSE;
    }

    if (dtor) {
        CClass_CheckStaticAccess(NULL, tclass, dtor->access);
    }

    if (parseInit && !ctor && tk == '=' && lookahead() == '{') {
        return FALSE;
    }

    if (parseInit && tk == '(') {
        tk = lex();
        list = CExpr_ScanExpressionList(TRUE);
        if (tk == ')') {
            tk = lex();
        } else {
            CError_Error(ERR_RPAREN_EXPECTED);
        }
    } else if (expr) {
        list = lalloc(sizeof(ENodeList));
        list->node = expr;
        list->next = NULL;
    } else {
        list = NULL;
    }

    if (ctor) {
        ctorflag = TRUE;
        if (tk == '=') {
            if (list) {
                CError_Error(ERR_ILLEGAL_INITIALIZATION);
            }
            list = lalloc(sizeof(ENodeList));
            list->next = NULL;
            tk = lex();
            list->node = conv_assignment_expression();
            ctorflag = FALSE;
        }

        if (!dtor) {
            objexpr = create_objectrefnode(cinit_initinfo->auto_obj);
            if (offset) {
                objexpr =
                    makediadicnode(objexpr, intconstnode(TYPE(&stunsignedlong), offset), EADD);
            }
        } else {
            objexpr = cinit_initinfo->register_object_cb(
                TYPE(tclass), cinit_initinfo->auto_obj, offset, FALSE
            );
        }

        objexpr = CExpr_ConstructObject(tclass, objexpr, list, FALSE, TRUE, FALSE, TRUE, ctorflag);
        if (!IS_TYPE_POINTER_ONLY(objexpr->rtype)) {
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
            return TRUE;
        }
        objexpr = makemonadicnode(objexpr, EINDIRECT);
        objexpr->rtype = TYPE_POINTER(objexpr->rtype)->target;
        cinit_initinfo->insert_expr_cb(objexpr);
    } else {
        if (list) {
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
        }
        if (dtor) {
            objexpr = cinit_initinfo->register_object_cb(
                TYPE(tclass), cinit_initinfo->auto_obj, offset, FALSE
            );
        }
        cinit_initinfo->insert_expr_cb(objexpr);
    }

    return TRUE;
}

static void CInit_ExprPointer(TypePointer* tptr, ENode* expr) {
    Object* obj;
    CInt64 val;
    OLinkList* list;

    if (CInit_RelocInitCheck(expr, &obj, &val, FALSE)) {
        if (obj) {
            list = lalloc(sizeof(OLinkList));
            list->next = cinit_initinfo->list;
            list->obj = obj;
            list->addend = CInt64_GetULong(&val);
            list->offset = cinit_initinfo->expr_offset;
            cinit_initinfo->list = list;
        } else {
            CMach_InitIntMem(
                TYPE(&stunsignedlong), val, cinit_initinfo->buffer + cinit_initinfo->expr_offset
            );
        }
    } else if (cinit_initinfo->expr_cb) {
        cinit_initinfo->expr_cb(TYPE(tptr), expr, FALSE);
        cinit_initinfo->expr_cb_called = TRUE;
    } else {
        CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
    }

    cinit_initinfo->expr_offset += 4;
}

static void CInit_ExprInt(TypeIntegral* tint, ENode* expr) {
    if (ENODE_IS(expr, EINTCONST)) {
        CMach_InitIntMem(
            TYPE(tint), expr->data.intval, cinit_initinfo->buffer + cinit_initinfo->expr_offset
        );
    } else if (ENODE_IS(expr, ETYPCON) &&
               IS_TYPE_POINTER_ONLY(expr->data.monadic->rtype) &&
               expr->rtype->size == 4 &&
               (copts.cplusplus || !copts.ANSIstrict)) {
        CInit_ExprPointer(TYPE_POINTER(expr->data.monadic->rtype), expr->data.monadic);
    } else if (cinit_initinfo->expr_cb) {
        cinit_initinfo->expr_cb(TYPE(tint), expr, FALSE);
        cinit_initinfo->expr_cb_called = TRUE;
    } else {
        CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
    }

    cinit_initinfo->expr_offset += tint->size;
}

static void CInit_ExprFloat(TypeIntegral* tint, ENode* expr) {
    if (ENODE_IS(expr, EFLOATCONST)) {
        CMach_InitFloatMem(
            TYPE(tint), expr->data.floatval, cinit_initinfo->buffer + cinit_initinfo->expr_offset
        );
    } else if (cinit_initinfo->expr_cb) {
        cinit_initinfo->expr_cb(TYPE(tint), expr, FALSE);
        cinit_initinfo->expr_cb_called = TRUE;
    } else {
        CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
    }

    cinit_initinfo->expr_offset += tint->size;
}

static void CInit_ExprEnum(TypeEnum* tenum, ENode* expr) {
    if (ENODE_IS(expr, EINTCONST)) {
        CMach_InitIntMem(
            tenum->enumtype, expr->data.intval, cinit_initinfo->buffer + cinit_initinfo->expr_offset
        );
    } else if (cinit_initinfo->expr_cb) {
        cinit_initinfo->expr_cb(TYPE(tenum), expr, FALSE);
        cinit_initinfo->expr_cb_called = TRUE;
    } else {
        CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
    }

    cinit_initinfo->expr_offset += tenum->size;
}

static void CInit_ExprMemberPointer(TypeMemberPointer* tmptr, ENode* expr) {
    if (ENODE_IS(expr, EINTCONST)) {
        CMach_InitIntMem(
            TYPE(&stsignedlong),
            expr->data.intval,
            cinit_initinfo->buffer + cinit_initinfo->expr_offset
        );
    } else if (cinit_initinfo->expr_cb) {
        cinit_initinfo->expr_cb(TYPE(tmptr), expr, FALSE);
        cinit_initinfo->expr_cb_called = TRUE;
    } else {
        CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
    }

    cinit_initinfo->expr_offset += tmptr->size;
}

static void CInit_TypeExpr(Type* type, ENode* expr) {
    switch (type->type) {
        case TYPEINT:
            CInit_ExprInt(TYPE_INTEGRAL(type), expr);
            return;
        case TYPEFLOAT:
            CInit_ExprFloat(TYPE_INTEGRAL(type), expr);
            return;
        case TYPEENUM:
            CInit_ExprEnum(TYPE_ENUM(type), expr);
            return;
        case TYPEPOINTER:
            CInit_ExprPointer(TYPE_POINTER(type), expr);
            return;
        case TYPEMEMBERPOINTER:
            CInit_ExprMemberPointer(TYPE_MEMBER_POINTER(type), expr);
            return;
        case TYPESTRUCT:
        case TYPECLASS:
            if (cinit_initinfo->expr_cb) {
                cinit_initinfo->expr_cb(type, expr, FALSE);
                cinit_initinfo->expr_cb_called = TRUE;
            } else {
                CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
            }
            return;
        case TYPEARRAY:
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
            return;
        default:
            CError_FATAL(2082);
    }
}

static void CInit_Bitfield(TypeBitfield* tbitfield) {
    Boolean in_block;
    ENode* expr;
    ENode myexpr;

    in_block = tk == '{';
    if (in_block) {
        tk = lex();
    }

    expr = CInit_ParseInitializer(&myexpr);
    expr = CExpr_AssignmentPromotion(
        expr,
        IS_TYPE_ENUM(tbitfield->bitfieldtype) ? TYPE_ENUM(tbitfield->bitfieldtype)->enumtype
                                              : tbitfield->bitfieldtype,
        0,
        TRUE
    );
    if (ENODE_IS(expr, EINTCONST)) {
        CInit_SetBitfield(
            tbitfield,
            (UInt8*)cinit_initinfo->buffer + cinit_initinfo->expr_offset,
            expr->data.intval
        );
    } else {
        CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
    }

    if (in_block) {
        CInit_CloseInitList();
    }
}

static void CInit_Array(TypePointer* tptr, UInt32 qual, Boolean isTopLevel) {
    SInt32 start;
    SInt32 i;
    SInt32 targetsize1;
    SInt32 targetsize2;
    Boolean in_block;
    Boolean is_char_ptr;
    Boolean needs_construction;
    Boolean is_wchar_ptr;

    targetsize1 = tptr->target->size;
    targetsize2 = tptr->target->size;
    if (!tptr->target->size) {
        if (!IS_TYPE_ARRAY(tptr->target)) {
            CError_Error(ERR_DATA_TYPE_INCOMPLETE);
            return;
        }
        targetsize1 = targetsize2 = TYPE_POINTER(tptr->target)->size;
        if (!targetsize1) {
            CError_Error(ERR_DATA_TYPE_INCOMPLETE);
            return;
        }
    }

    is_char_ptr = IS_TYPE_INT(tptr->target) && (tptr->target->size == 1);
    is_wchar_ptr = IS_TYPE_INT(tptr->target) && (tptr->target->size == stwchar.size);

    in_block = TRUE;
    if (isTopLevel &&
        !(tk == TK_STRING && is_char_ptr) &&
        !(tk == TK_STRING_WIDE && is_wchar_ptr)) {
        if (tk != '{') {
            CError_ErrorSkip(ERR_LBRACE_EXPECTED);
            return;
        }
        tk = lex();
    } else {
        if (tk == '{') {
            tk = lex();
        } else {
            in_block = FALSE;
        }
    }

    if ((tk == TK_STRING && is_char_ptr) || (tk == TK_STRING_WIDE && is_wchar_ptr)) {
        if (tptr->size) {
            if (tksize > tptr->size) {
                if (copts.cplusplus || (tksize - (is_wchar_ptr ? stwchar.size : 1)) > tptr->size) {
                    CError_Error(ERR_TOO_MANY_INITIALIZERS);
                }
                tksize = tptr->size;
            }
            memcpy(cinit_initinfo->buffer + cinit_initinfo->expr_offset, tkstring, tksize);
        } else {
            tptr->size = tksize;
            CInit_SetData(tkstring, cinit_initinfo->expr_offset, tptr->size);
        }
        cinit_initinfo->expr_offset += tptr->size;
        tk = lex();
        if (in_block) {
            CInit_CloseInitList();
        }
        return;
    }

    if (IS_TYPE_CLASS(tptr->target) && CInit_ClassNeedsConstruction(TYPE_CLASS(tptr->target))) {
        needs_construction = TRUE;
    } else {
        needs_construction = FALSE;
    }

    start = cinit_initinfo->expr_offset;
    i = 0;
    while (1) {
        if (tk == '}') {
innerloop:
            if (tptr->size) {
                if (needs_construction) {
                    while (tptr->size > (i * targetsize1)) {
                        cinit_initinfo->expr_offset = start + i * targetsize2;
                        if (cinit_initinfo->expr_cb) {
                            cinit_initinfo->expr_cb(tptr->target, NULL, TRUE);
                            cinit_initinfo->expr_cb_called = TRUE;
                        } else {
                            CError_Error(ERR_ILLEGAL_INITIALIZATION);
                        }
                        i++;
                    }
                }
            } else {
                tptr->size = i * targetsize1;
            }
            cinit_initinfo->expr_offset = start + tptr->size;
            if (in_block) {
                tk = lex();
            }
            return;
        }

        if (tptr->size == 0) {
            cinit_initinfo->expr_offset = start + i * targetsize2;
            CInit_SetData(NULL, cinit_initinfo->expr_offset, targetsize2);
            if (needs_construction) {
                if (cinit_initinfo->expr_cb) {
                    cinit_initinfo->expr_cb(tptr->target, conv_assignment_expression(), TRUE);
                    cinit_initinfo->expr_cb_called = TRUE;
                } else {
                    CError_Error(ERR_ILLEGAL_INITIALIZATION);
                }
            } else {
                CInit_Type(tptr->target, qual, FALSE);
            }
        } else {
            if (tptr->size <= i * targetsize1) {
                i--;
                CError_Error(ERR_TOO_MANY_INITIALIZERS);
            }

            cinit_initinfo->expr_offset = start + i * targetsize2;
            if (needs_construction) {
                if (cinit_initinfo->expr_cb) {
                    cinit_initinfo->expr_cb(tptr->target, conv_assignment_expression(), TRUE);
                    cinit_initinfo->expr_cb_called = TRUE;
                } else {
                    CError_Error(ERR_ILLEGAL_INITIALIZATION);
                }
            } else {
                CInit_Type(tptr->target, qual, FALSE);
            }

            if (!in_block) {
                if (tptr->size <= (i + 1) * targetsize1) {
                    return;
                }
            }
        }

        if (tk != '}') {
            if (tk != ',') {
                CError_ErrorSkip(ERR_DECLARATION_SYNTAX_ERROR);
                in_block = FALSE;
                i++;
                goto innerloop;
            }
            tk = lex();
        }
        i++;
    }
}

static void CInit_Struct(TypeStruct* tstruct, Boolean isTopLevel) {
    StructMember* member;
    SInt32 start;
    Boolean in_block;

    if (!(member = tstruct->members)) {
        CError_Error(ERR_DATA_TYPE_INCOMPLETE);
        return;
    }

    if (tstruct->stype == STRUCT_TYPE_UNION) {
        if (tk == '{') {
            tk = lex();
            CInit_Type(member->type, member->qual, FALSE);
            if (tk == '}') {
                tk = lex();
            }
        } else {
            CInit_Type(member->type, member->qual, FALSE);
        }
        return;
    }

    if (IS_TYPE_VECTOR(tstruct) && tk != '{') {
        CInit_TypeExpr(
            TYPE(tstruct),
            CExpr_AssignmentPromotion(conv_assignment_expression(), TYPE(tstruct), 0, TRUE)
        );
        return;
    }

    if (tk != '{') {
        if (isTopLevel) {
            CError_ErrorSkip(ERR_LBRACE_EXPECTED);
        }
        in_block = FALSE;
    } else {
        in_block = TRUE;
        tk = lex();
    }

    start = cinit_initinfo->expr_offset;
    while (1) {
        if (tk == '}') {
            break;
        }

        cinit_initinfo->expr_offset = start + member->offset;
        if (!member->type->size && IS_TYPE_ARRAY(member->type)) {
            CError_Error(ERR_TOO_MANY_INITIALIZERS);
            break;
        }

        CInit_Type(member->type, member->qual, FALSE);
        if (tk == '}') {
            break;
        }

        if (tk != ',') {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
            break;
        }

        do {
            member = member->next;
        } while (member && (member->qual & Q_WEAK));

        if (!member) {
            if (!in_block) {
                break;
            }
            if ((tk = lex()) != '}') {
                CError_Error(ERR_TOO_MANY_INITIALIZERS);
                break;
            }
        } else {
            tk = lex();
        }
    }

    cinit_initinfo->expr_offset = start + tstruct->size;
    if (tk == '}' && in_block) {
        tk = lex();
    }
}

static void CInit_Class(TypeClass* tclass, Boolean isTopLevel) {
    ObjMemberVar* ivar;
    SInt32 start;
    Boolean in_block;

    if (tk == '{') {
        in_block = TRUE;
        tk = lex();
    } else {
        in_block = FALSE;
    }

    if (tclass->bases || tclass->vtable) {
        CError_Error(ERR_ILLEGAL_INITIALIZATION);
        return;
    }

    for (ivar = tclass->ivars; ivar; ivar = ivar->next) {
        if (ivar->access != ACCESSPUBLIC) {
            break;
        }
    }

    if (!ivar && !CClass_Constructor(tclass) && (!CClass_Destructor(tclass) || in_block)) {
        if ((ivar = tclass->ivars)) {
            start = cinit_initinfo->expr_offset;
            while (1) {
                if (tk == '}') {
                    break;
                }

                if (!ivar->type->size && IS_TYPE_ARRAY(ivar->type)) {
                    CError_Error(ERR_TOO_MANY_INITIALIZERS);
                    break;
                }

                cinit_initinfo->expr_offset = start + ivar->offset;
                CInit_Type(ivar->type, ivar->qual, FALSE);

                if (tk == '}') {
                    break;
                }

                if (tk != ',') {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    break;
                }

                do {
                    ivar = ivar->next;
                } while (ivar && ivar->anonunion);

                if (!ivar) {
                    if (!in_block) {
                        break;
                    }
                    if ((tk = lex()) != '}') {
                        CError_Error(ERR_TOO_MANY_INITIALIZERS);
                        break;
                    }
                } else {
                    tk = lex();
                }
            }
        } else {
            if (in_block && tk != '}') {
                CError_Error(ERR_TOO_MANY_INITIALIZERS);
            }
        }
    } else {
        if (in_block) {
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
        }
        CInit_TypeExpr(
            TYPE(tclass),
            CExpr_AssignmentPromotion(conv_assignment_expression(), TYPE(tclass), 0, TRUE)
        );
    }

    cinit_initinfo->expr_offset = start + tclass->size;
    if (tk == '}' && in_block) {
        tk = lex();
    }
}

static void CInit_Type(Type* type, UInt32 qual, Boolean isTopLevel) {
    ENode* expr;
    ENode myexpr;

    switch (type->type) {
        case TYPEVOID:
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
            return;
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEENUM:
        case TYPEPOINTER:
        case TYPEMEMBERPOINTER:
            if (tk == '{') {
                tk = lex();
                expr = CInit_ParseInitializer(&myexpr);
                expr = CExpr_AssignmentPromotion(expr, type, qual & (Q_CONST | Q_VOLATILE), TRUE);
                CInit_CloseInitList();
            } else {
                expr = CInit_ParseInitializer(&myexpr);
                expr = CExpr_AssignmentPromotion(expr, type, qual & (Q_CONST | Q_VOLATILE), TRUE);
            }
            CInit_TypeExpr(type, expr);
            return;
        case TYPEBITFIELD:
            CInit_Bitfield(TYPE_BITFIELD(type));
            return;
        case TYPEARRAY:
            CInit_Array(TYPE_POINTER(type), qual, isTopLevel);
            return;
        case TYPESTRUCT:
            CInit_Struct(TYPE_STRUCT(type), isTopLevel);
            return;
        case TYPECLASS:
            CInit_Class(TYPE_CLASS(type), isTopLevel);
            return;
        default:
            CError_FATAL(2482);
    }
}

static void CInit_GlobalStaticInit(Type* type, ENode* valueexpr, Boolean isClass) {
    ENode* expr;
    ENode* lvalue;

    cinit_initinfo->has_runtime_init = TRUE;
    if (isClass) {
        CInit_ConstructGlobalObject(
            cinit_initinfo->obj, TYPE_CLASS(type), valueexpr, cinit_initinfo->expr_offset, FALSE
        );
    } else {
        expr = create_objectrefnode(cinit_initinfo->obj);
        if (!IS_TYPE_POINTER_ONLY(expr->rtype)) {
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
            return;
        }
        TYPE_POINTER(expr->rtype)->target = type;

        if (cinit_initinfo->expr_offset) {
            expr = makediadicnode(
                expr, intconstnode(TYPE(&stunsignedlong), cinit_initinfo->expr_offset), EADD
            );
        }

        lvalue = makemonadicnode(expr, EINDIRECT);
        lvalue->rtype = type;
        expr = makediadicnode(lvalue, valueexpr, EASS);
        if (cinit_initinfo->use_register_cb) {
            cinit_initinfo->init_expr_register_cb(expr);
        } else {
            InitExpr_Register(expr, cinit_initinfo->obj);
        }
    }
}

static void CInit_AutoInit(Type* type, ENode* valueexpr, Boolean isClass) {
    ENode* expr;
    ENode* lvalue;
    TypePointer* copy;

    if (isClass) {
        CInit_ConstructAutoObject(TYPE_CLASS(type), valueexpr, cinit_initinfo->expr_offset, FALSE);
    } else {
        if (IS_TYPE_ARRAY(type) && (type->size & 1)) {
            copy = galloc(sizeof(TypePointer));
            *copy = *TYPE_POINTER(type);
            type = TYPE(copy);
            type->size++;
        }
        expr = create_objectrefnode(cinit_initinfo->auto_obj);
        if (!IS_TYPE_POINTER_ONLY(expr->rtype)) {
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
            return;
        }
        TYPE_POINTER(expr->rtype)->target = type;

        if (cinit_initinfo->expr_offset) {
            expr = makediadicnode(
                expr, intconstnode(TYPE(&stunsignedlong), cinit_initinfo->expr_offset), EADD
            );
        }

        lvalue = makemonadicnode(expr, EINDIRECT);
        lvalue->rtype = type;
        expr = makediadicnode(lvalue, valueexpr, EASS);
        if (!copts.cplusplus) {
            CError_Error(ERR_ILLEGAL_CONSTANT_EXPRESSION);
        }
        cinit_initinfo->insert_expr_cb(expr);
    }
}

static SInt32 CInit_AdjustObjectDataSize(Object* obj) {
    if (obj->type->size <= 1) {
        return obj->type->size;
    }
    if (obj->type->size & 1) {
        return obj->type->size + 1;
    } else {
        return obj->type->size;
    }
}

static ENode* CInit_GenericData(
    Object* obj, Type* type, UInt32 qual, ExprCB expr_cb, Boolean isGlobal
) {
    ENode* expr;
    Object* target;
    ENode* stripped;
    Type* inner;
    TypePointer* copy;
    SInt32 size;
    Boolean lastflag;
    SInt16 cv;

    cinit_initinfo->expr_cb = expr_cb;
    expr = NULL;

    if (tk == '(') {
        if (IS_TYPE_ARRAY(type)) {
            CError_Error(ERR_ILLEGAL_INITIALIZATION);
        }
        tk = lex();
        expr = CExpr_AssignmentPromotion(assignment_expression(), type, qual, TRUE);
        if (tk != ')') {
            CError_ErrorSkip(ERR_RPAREN_EXPECTED);
        } else {
            tk = lex();
        }
        goto jump_ahead;
    }

    tk = lex();
    switch (type->type) {
        case TYPECLASS:
            if (tk == '{' && CClass_Constructor(TYPE_CLASS(type))) {
                CError_Error(ERR_ILLEGAL_INITIALIZATION);
            }
        case TYPESTRUCT:
            if (tk != '{') {
                goto generic_type;
            }
        case TYPEARRAY:
            if (!obj) {
                if (IS_TYPE_ARRAY(type)) {
                    inner = type;
                    while (IS_TYPE_ARRAY(inner)) {
                        inner = TYPE_POINTER(inner)->target;
                    }

                    if (IS_TYPE_CLASS(inner) && CInit_ClassNeedsConstruction(TYPE_CLASS(inner))) {
                        CInit_SetupInitInfoBuffer(type);
                        cinit_initinfo->obj = cinit_initinfo->auto_obj;
                        CInit_Type(type, cinit_initinfo->obj->qual, TRUE);
                        return NULL;
                    }
                    if (type->size & 1) {
                        copy = galloc(sizeof(TypePointer));
                        *copy = *TYPE_POINTER(type);
                        type = TYPE(copy);
                        type->size++;
                    }
                }

                cinit_initinfo->obj = obj = CInit_CreateStaticDataObject(type, qual, NULL);
                expr = create_objectnode(obj);
                cinit_initinfo->auto_obj = obj;
            }
            CInit_SetupInitInfoBuffer(type);
            CInit_Type(type, obj->qual, TRUE);
            if (obj->type->size != (size = cinit_initinfo->size)) {
                CError_FATAL(2639);
            }
            if (cinit_initinfo->list || !CInit_IsAllZero(cinit_initinfo->buffer, size)) {
                CInit_DeclareData(
                    obj, cinit_initinfo->buffer, cinit_initinfo->list, size = obj->type->size
                );
            } else {
                CInit_DeclareData(obj, NULL, NULL, size = obj->type->size);
            }
            return expr;
        case TYPEINT:
        case TYPEFLOAT:
        case TYPEENUM:
        case TYPEPOINTER:
        case TYPEMEMBERPOINTER:
generic_type:
            if (obj) {
                cv = obj->qual & Q_CV;
            } else {
                cv = cinit_initinfo->auto_obj->qual & Q_CV;
            }

            if (tk == '{') {
                tk = lex();
                expr = assignment_expression();
                CInit_CloseInitList();
            } else {
                expr = assignment_expression();
            }
            expr = CExpr_AssignmentPromotion(expr, type, cv, TRUE);
jump_ahead:
            if (obj == NULL) {
                target = cinit_initinfo->auto_obj;
            } else {
                target = obj;
            }

            if (is_const_object(target)) {
                switch (target->type->type) {
                    case TYPEINT:
                    case TYPEENUM:
                        if (ENODE_IS(expr, EINTCONST)) {
                            target->u.data.u.intconst = expr->data.intval;
                            goto inline_data;
                        }
                        break;
                    case TYPEFLOAT:
                        if (ENODE_IS(expr, EFLOATCONST)) {
                            target->u.data.u.floatconst = galloc(sizeof(Float));
                            *target->u.data.u.floatconst =
                                CMach_CalcFloatConvert(target->type, expr->data.floatval);
                            goto inline_data;
                        }
                        break;
                    case TYPEPOINTER:
                        stripped = expr;
                        while (ENODE_IS(stripped, ETYPCON)) {
                            stripped = stripped->data.monadic;
                        }
                        if (!ENODE_IS(stripped, EINTCONST)) {
                            break;
                        }
                        target->u.data.u.intconst = stripped->data.intval;
inline_data:
                        target->qual |= Q_INLINE_DATA;
                        if (!obj) {
                            target->sclass = TK_STATIC;
                            target->datatype = DDATA;
                            target->u.data.linkname = CParser_AppendUniqueName(target->name->name);
                        } else if (target->sclass != TK_STATIC ||
                                   (target->flags & OBJECT_REFERENCED)) {
                            CInit_ExportConst(target);
                        }
                        return NULL;
                    default:
                        break;
                }
            }

            if (!obj || (isGlobal && copts.cplusplus)) {
                if (obj) {
                    IsCompleteType(obj->type);
                    CError_ASSERT(2750, obj->type->size == type->size);
                    CInit_DeclareData(obj, NULL, NULL, obj->type->size);
                }
                return expr;
            }

            CInit_SetupInitInfoBuffer(type);
            CInit_TypeExpr(type, expr);
            CError_ASSERT(2759, obj->type->size == cinit_initinfo->size);

            IsCompleteType(obj->type);
            CInit_AdjustObjectDataSize(obj);
            lastflag = !cinit_initinfo->has_runtime_init && is_const_object(target);
            if (cinit_initinfo->list || !CInit_IsAllZero(cinit_initinfo->buffer, obj->type->size)) {
                if (lastflag) {
                    CInit_DeclareReadOnlyData(
                        obj, cinit_initinfo->buffer, cinit_initinfo->list, obj->type->size
                    );
                } else {
                    CInit_DeclareData(
                        obj, cinit_initinfo->buffer, cinit_initinfo->list, obj->type->size
                    );
                }
            } else {
                if (lastflag) {
                    CInit_DeclareReadOnlyData(obj, NULL, NULL, obj->type->size);
                } else {
                    CInit_DeclareData(obj, NULL, NULL, obj->type->size);
                }
            }
            break;

        default:
            CError_FATAL(2779);
    }

    return NULL;
}

void CInit_ExportConst(Object* obj) {
    char buffer[64];

    if (obj->flags & OBJECT_DEFINED) {
        return;
    }

    switch (obj->type->type) {
        case TYPEINT:
            CMach_InitIntMem(obj->type, obj->u.data.u.intconst, buffer);
            break;
        case TYPEENUM:
            CMach_InitIntMem(TYPE_ENUM(obj->type)->enumtype, obj->u.data.u.intconst, buffer);
            break;
        case TYPEPOINTER:
            CMach_InitIntMem(TYPE(&stunsignedlong), obj->u.data.u.intconst, buffer);
            break;
        case TYPEFLOAT:
            CMach_InitFloatMem(obj->type, *obj->u.data.u.floatconst, buffer);
            break;
        default:
            CError_FATAL(2810);
    }

    if (is_const_object(obj)) {
        CInit_DeclareReadOnlyData(obj, buffer, NULL, obj->type->size);
    } else {
        CInit_DeclareData(obj, buffer, NULL, obj->type->size);
    }
}

static ENode* CInit_ClassInitLoopCallBack(ENode* expr) {
    return CExpr_ConstructObject(cinit_loop_class, expr, NULL, FALSE, TRUE, FALSE, TRUE, TRUE);
}

Statement* CInit_ConstructClassArray(
    Statement* stmt, TypeClass* tclass, Object* ctor, Object* dtor, ENode* firstarg, SInt32 count
) {
    ENode* dtor_expr;

    if (stmt) {
        stmt = CFunc_InsertStatement(ST_EXPRESSION, stmt);
    } else {
        stmt = CFunc_AppendStatement(ST_EXPRESSION);
    }

    if (dtor) {
        dtor_expr = create_objectrefnode(CABI_GetDestructorObject(dtor, CABIDestroyComplete));
    } else {
        dtor_expr = nullnode();
    }

    stmt->expr = CExpr_FuncCallSix(
        carr_func,
        firstarg,
        create_objectrefnode(ctor),
        dtor_expr,
        intconstnode(TYPE(&stunsignedlong), tclass->size),
        intconstnode(TYPE(&stunsignedlong), count),
        NULL
    );

    return stmt;
}

static void CInit_InitializeClassArray(Object* obj, TypeClass* tclass, Boolean isGlobal) {
    Object* ctor;
    Object* dtor;
    SInt32 count;
    SInt32 i;
    ENode* expr;
    SInt32 offset;
    Statement* stmt;
    TypeFunc* tfunc;
    ENode* dtor_expr;
    Object* funcobj;

    dtor = CClass_Destructor(tclass);
    count = obj->type->size / tclass->size;
    if (CClass_Constructor(tclass)) {
        ctor = CClass_DefaultConstructor(tclass);
        if (!ctor) {
            ctor = CClass_DummyDefaultConstructor(tclass);
            if (!ctor) {
                CError_Error(ERR_CLASS_NO_DEFAULT_CONSTRUCTOR);
                return;
            }
        }
    } else {
        ctor = NULL;
    }

    if (count <= 1 || (!isGlobal && count <= 8)) {
        if (isGlobal) {
            for (i = 0; i < count; i++) {
                CInit_ConstructGlobalObject(obj, tclass, NULL, i * tclass->size, FALSE);
            }
        } else {
            for (i = 0; i < count; i++) {
                offset = i * tclass->size;
                expr = create_objectrefnode(obj);
                if (offset) {
                    expr = makediadicnode(expr, intconstnode(TYPE(&stunsignedlong), offset), EADD);
                }
                if (ctor) {
                    expr =
                        CExpr_ConstructObject(tclass, expr, NULL, FALSE, TRUE, FALSE, TRUE, TRUE);
                }
                cinit_initinfo->insert_expr_cb(expr);
                if (dtor) {
                    CExcept_RegisterDestructorObject(obj, offset, dtor, TRUE);
                }
            }
            if (dtor) {
                stmt = CFunc_AppendStatement(ST_EXPRESSION);
                stmt->expr = nullnode();
            }
        }
    } else {
        if (ctor) {
            if (!isGlobal && !dtor) {
                CInit_ConstructClassArray(
                    NULL, tclass, ctor, dtor, create_objectrefnode(obj), count
                );
                expr = nullnode();
            } else {
                if (dtor) {
                    dtor_expr =
                        create_objectrefnode(CABI_GetDestructorObject(dtor, CABIDestroyComplete));
                } else {
                    dtor_expr = nullnode();
                }
                expr = CExpr_FuncCallSix(
                    carr_func,
                    create_objectrefnode(obj),
                    create_objectrefnode(ctor),
                    dtor_expr,
                    intconstnode(TYPE(&stunsignedlong), tclass->size),
                    intconstnode(TYPE(&stunsignedlong), count),
                    NULL
                );
            }
        } else {
            expr = nullnode();
        }

        if (isGlobal) {
            if (dtor && !copts.no_static_dtors) {
                tfunc = galloc(sizeof(TypeFunc));
                memclrw(tfunc, sizeof(TypeFunc));
                tfunc->type = TYPEFUNC;
                tfunc->functype = &stvoid;
                CDecl_SetFuncFlags(tfunc, 1);

                funcobj = CParser_NewCompilerDefFunctionObject();
                funcobj->name = CParser_AppendUniqueName("__arraydtor");
                funcobj->type = TYPE(tfunc);
                funcobj->sclass = TK_STATIC;
                funcobj->qual = Q_INLINE;

                CParser_RegisterSingleExprFunction(
                    funcobj,
                    funccallexpr(
                        darr_func,
                        create_objectrefnode(obj),
                        create_objectrefnode(CABI_GetDestructorObject(dtor, CABIDestroyComplete)),
                        intconstnode(TYPE(&stsignedlong), tclass->size),
                        intconstnode(TYPE(&stsignedlong), count)
                    )
                );

                expr = makediadicnode(expr, nullnode(), ECOMMA);
                expr->rtype = TYPE(&void_ptr);

                expr = funccallexpr(
                    Xgreg_func,
                    expr,
                    create_objectrefnode(funcobj),
                    create_objectrefnode(CInit_CreateStaticData(CInit_GetRegMemType())),
                    NULL
                );
            }
            if (cinit_initinfo->use_register_cb) {
                cinit_initinfo->init_expr_register_cb(expr);
            } else {
                InitExpr_Register(expr, obj);
            }
        } else {
            stmt = CFunc_AppendStatement(ST_EXPRESSION);
            stmt->expr = expr;
            if (dtor) {
                CExcept_RegisterLocalArray(stmt, obj, dtor, count, tclass->size);
                stmt = CFunc_AppendStatement(ST_EXPRESSION);
                stmt->expr = nullnode();
            }
        }
    }
}

static ENode* CInit_AutoTempNode(Type* type, Boolean assignID) {
    ENode* node;
    node = CExpr_NewETEMPNode(type, FALSE);
    if (IS_TYPE_CLASS(type) && CClass_Destructor(TYPE_CLASS(type))) {
        node->data.temp.needs_dtor = TRUE;
    }
    return node;
}

static ENode* CInit_GlobalTempNode(Type* type, Boolean assignID) {
    Object* dtor;
    ENode* funcnode;
    ENode* node;

    node = create_objectrefnode(CInit_CreateStaticData(type));
    if (IS_TYPE_CLASS(type) &&
        (dtor = CClass_Destructor(TYPE_CLASS(type))) &&
        !copts.no_static_dtors) {
        if (assignID) {
            CError_Error(ERR_UNIMPLEMENTED_C_FEATURE);
        }

        funcnode = galloc(sizeof(ENode));
        funcnode->type = EFUNCCALL;
        funcnode->cost = 200;
        funcnode->flags = 0;
        funcnode->rtype = CDecl_NewPointerType(type);
        funcnode->data.funccall.funcref = create_objectrefnode(Xgreg_func);
        funcnode->data.funccall.functype = TYPE_FUNC(Xgreg_func->type);
        funcnode->data.funccall.args = lalloc(sizeof(ENodeList));
        funcnode->data.funccall.args->node = node;
        funcnode->data.funccall.args->next = lalloc(sizeof(ENodeList));
        funcnode->data.funccall.args->next->node =
            create_objectrefnode(CABI_GetDestructorObject(dtor, CABIDestroyComplete));
        funcnode->data.funccall.args->next->next = lalloc(sizeof(ENodeList));
        funcnode->data.funccall.args->next->next->node =
            create_objectrefnode(CInit_CreateStaticData(CInit_GetRegMemType()));
        funcnode->data.funccall.args->next->next->next = NULL;
        node = funcnode;
    }
    return node;
}

static void CInit_RefInit(Type* type, ENode* expr, Boolean unused) {
    ENode* objexpr;

    objexpr = create_objectrefnode(cinit_initinfo->obj);
    if (!IS_TYPE_POINTER_ONLY(objexpr->rtype)) {
        CError_Error(ERR_ILLEGAL_INITIALIZATION);
        return;
    }
    TYPE_POINTER(objexpr->rtype)->target = type;

    if (cinit_initinfo->expr_offset) {
        objexpr = makediadicnode(
            objexpr, intconstnode(TYPE(&stunsignedlong), cinit_initinfo->expr_offset), EADD
        );
    }

    objexpr = makemonadicnode(objexpr, EINDIRECT);
    objexpr->rtype = type;

    objexpr = makediadicnode(objexpr, expr, EASS);
    if (cinit_initinfo->use_register_cb) {
        cinit_initinfo->init_expr_register_cb(objexpr);
    } else {
        InitExpr_Register(objexpr, cinit_initinfo->obj);
    }
}

static Boolean CInit_IsDtorTemp(ENode* expr) {
    return ENODE_IS(expr, ETEMP) && expr->data.temp.needs_dtor;
}

static void CInit_FindDtorTemp(ENode* expr) {
    ENodeList* list;

    while (ENODE_IS(expr, ECOMMA)) {
        expr = expr->data.diadic.right;
    }

    if (IS_TYPE_POINTER_ONLY(expr->rtype) && IS_TYPE_CLASS(TYPE_POINTER(expr->rtype)->target)) {
        switch (expr->type) {
            case ETYPCON:
                CInit_FindDtorTemp(expr->data.monadic);
                return;
            case EADD:
            case ESUB:
                CInit_FindDtorTemp(expr->data.diadic.left);
                CInit_FindDtorTemp(expr->data.diadic.right);
                return;
            case EFUNCCALL:
            case EFUNCCALLP:
                if ((list = expr->data.funccall.args)) {
                    if (CInit_IsDtorTemp(list->node) ||
                        ((list = list->next) && CInit_IsDtorTemp(list->node))) {
                        if (!cinit_fdtnode) {
                            cinit_fdtnode = list;
                        } else {
                            cinit_fdtambig = TRUE;
                        }
                    }
                }
                return;
            default:
                break;
        }
    }
}

static void CInit_RefTempTransform(Type* type, ENode* expr) {
    Object* obj;

    CError_ASSERT(3167, IS_TYPE_POINTER_ONLY(type));

    if (IS_TYPE_CLASS(TYPE_POINTER(type)->target)) {
        cinit_fdtnode = NULL;
        cinit_fdtambig = FALSE;
        CInit_FindDtorTemp(expr);
        if (cinit_fdtnode) {
            CError_ASSERT(3175, !cinit_fdtambig);
            obj = create_temp_object(cinit_fdtnode->node->data.temp.type);
            cinit_initinfo->register_object_cb(cinit_fdtnode->node->data.temp.type, obj, 0, FALSE);
            cinit_fdtnode->node = create_objectrefnode(obj);
        }
    }
}

static Boolean CInit_InitReference(Object* obj, Boolean isLocal) {
    ENode* expr;
    SInt32 size;

    if (tk == '=') {
        if (isLocal) {
            cinit_tempnodefunc = CInit_AutoTempNode;
        } else {
            cinit_tempnodefunc = CInit_GlobalTempNode;
        }
        tk = lex();
        expr = assignment_expression();
        expr = CExpr_AssignmentPromotion(expr, obj->type, obj->qual & (Q_CONST | Q_VOLATILE), TRUE);
        cinit_tempnodefunc = NULL;

        if (isLocal) {
            CInit_RefTempTransform(obj->type, expr);
            expr = makediadicnode(create_objectnode2(obj), expr, EASS);
            cinit_initinfo->insert_expr_cb(expr);
        } else {
            cinit_initinfo->expr_cb = CInit_RefInit;
            CInit_SetupInitInfoBuffer(obj->type);
            CInit_ExprPointer(TYPE_POINTER(obj->type), expr);
            CError_ASSERT(3216, obj->type->size == cinit_initinfo->size);

            if (cinit_initinfo->list || !CInit_IsAllZero(cinit_initinfo->buffer, obj->type->size)) {
                IsCompleteType(obj->type);
                CInit_DeclareData(
                    obj, cinit_initinfo->buffer, cinit_initinfo->list, size = obj->type->size
                );
            } else {
                IsCompleteType(obj->type);
                CInit_DeclareData(obj, NULL, NULL, size = obj->type->size);
            }
        }

        return TRUE;
    }

    return FALSE;
}

ENode* CInit_AutoObject(Object* obj, Type* type, UInt32 qual) {
    CInit_DataBuffer db;
    CInit_NonConst* entry;
    ENode* expr;
    ENode* indirect_expr;
    ENode* obj_expr;
    ENode* const_expr;
    Type* newtype;
    Object* newobj;

    CInit_InitData(
        &db, type, qual, copts.cplusplus || copts.gcc_extensions || copts.c9x || obj == NULL
    );
    if (!obj && !cscope_currentfunc) {
        obj = CParser_NewCompilerDefDataObject();
        obj->name = CParser_GetUniqueName();
        obj->type = type;
        obj->qual = qual;
        obj->sclass = TK_STATIC;
        CScope_AddGlobalObject(obj);
    }

    if (IS_TYPE_VECTOR(type) && !db.nonconst) {
        if (obj) {
            obj_expr = create_objectrefnode(obj);
        } else {
            obj_expr = CExpr_NewETEMPNode(type, TRUE);
        }

        const_expr = CExpr_NewENode(EVECTOR128CONST);
        const_expr->rtype = type;
        const_expr->data.vector128val = *((MWVector128*)db.buffer);

        indirect_expr = makemonadicnode(obj_expr, EINDIRECT);
        indirect_expr->rtype = type;

        expr = makediadicnode(indirect_expr, const_expr, EASS);
        if (!obj) {
            ENode* objval = lalloc(sizeof(ENode));
            *objval = *obj_expr;
            objval = makemonadicnode(objval, EINDIRECT);
            objval->rtype = type;
            objval->flags = qual & ENODE_FLAG_QUALS;
            expr = makecommaexpression(expr, objval);
        }
        return expr;
    }

    if (db.flexsize) {
        type = CDecl_NewStructType(type->size + db.flexsize, CMach_GetTypeAlign(type));
        if (obj) {
            obj->type = type;
        }
    }

    if (obj) {
        obj_expr = create_objectrefnode(obj);
    } else {
        obj_expr = CExpr_NewETEMPNode(type, TRUE);
    }

    if (IS_TYPE_ARRAY((newtype = type))) {
        newtype = CDecl_NewStructType(type->size, CMach_GetTypeAlign(type));
    }
    newobj = CInit_CreateStaticDataObject(newtype, 0, NULL);
    if (db.list || !CInit_IsAllZero(db.buffer, db.size)) {
        CInit_DeclareReadOnlyData(newobj, db.buffer, db.list, db.size);
    } else {
        CInit_DeclareReadOnlyData(newobj, NULL, NULL, db.size);
    }

    indirect_expr = makemonadicnode(obj_expr, EINDIRECT);
    indirect_expr->rtype = newtype;
    expr = makediadicnode(indirect_expr, create_objectnode(newobj), EASS);

    for (entry = db.nonconst; entry; entry = entry->next) {
        expr = CInit_InitConcat(expr, obj_expr, entry->offset, entry->type, entry->expr);
    }

    if (!obj) {
        ENode* objval = lalloc(sizeof(ENode));
        *objval = *obj_expr;
        objval = makemonadicnode(objval, EINDIRECT);
        objval->rtype = type;
        objval->flags = qual & ENODE_FLAG_QUALS;
        expr = makecommaexpression(expr, objval);
    } else if (IS_TYPE_ARRAY(type)) {
        expr = makecommaexpression(expr, create_objectnode(obj));
    }

    return expr;
}

static void CInit_GlobalObject(Object* obj) {
    CInit_DataBuffer db;
    CInit_NonConst* entry;
    ENode* obj_expr;
    ENode* expr;

    CInit_InitData(&db, obj->type, obj->qual, copts.cplusplus);
    obj_expr = create_objectrefnode(obj);

    IsCompleteType(obj->type);

    if (!db.nonconst && is_const_object(obj)) {
        CInit_DeclareReadOnlyData(obj, db.buffer, db.list, db.size);
    } else {
        CInit_DeclareData(obj, db.buffer, db.list, db.size);
    }

    if (db.nonconst) {
        entry = db.nonconst;
        expr = NULL;
        while (entry) {
            if (!ENODE_IS(entry->expr, EVECTOR128CONST)) {
                expr = CInit_InitConcat(expr, obj_expr, entry->offset, entry->type, entry->expr);
            }
            entry = entry->next;
        }

        if (expr) {
            InitExpr_Register(expr, obj);
        }
    }
}

void CInit_InitializeAutoData(Object* obj, InsertExprCB insert_cb, RegisterObjectCB register_cb) {
    Type* type = obj->type;
    Type* inner;
    Boolean is_simple;
    InitInfo initinfo;
    ENode* expr;

    switch (type->type) {
        case TYPESTRUCT:
            is_simple = TRUE;
            break;
        case TYPEARRAY:
            while (IS_TYPE_ARRAY(type)) {
                type = TYPE_POINTER(type)->target;
            }
            if (!IS_TYPE_CLASS(type)) {
                is_simple = TRUE;
                break;
            }
        case TYPECLASS:
            is_simple = !CInit_ClassNeedsConstruction(TYPE_CLASS(type));
            break;
        default:
            is_simple = FALSE;
    }

    if (is_simple) {
        if (tk == '=' || (tk == '(' && copts.cplusplus)) {
            if (tk == '(') {
                tk = lex();
                expr = conv_assignment_expression();
                if (tk != ')') {
                    CError_Error(ERR_RPAREN_EXPECTED);
                }
                tk = lex();
            } else if (tk == '=' && ((tk = lex()) == '{' || IS_TYPE_ARRAY(obj->type))) {
                insert_cb(CInit_AutoObject(obj, obj->type, obj->qual));
                return;
            } else {
                expr = conv_assignment_expression();
            }
            expr = CExpr_AssignmentPromotion(
                expr, obj->type, obj->qual & (Q_CONST | Q_VOLATILE), TRUE
            );
            insert_cb(makediadicnode(create_objectnode2(obj), expr, EASS));
            return;
        } else if (copts.cplusplus && is_const_object(obj)) {
            CError_Error(ERR_CONST_AMPERSAND_VARIABLE_NEEDS_INITIALIZER);
        }

        return;
    }

    CInit_SetupInitInfo(&initinfo, obj);
    initinfo.auto_obj = obj;
    initinfo.insert_expr_cb = insert_cb;
    initinfo.register_object_cb = register_cb;

    if (IS_TYPE_CLASS(obj->type) &&
        CInit_ConstructAutoObject(TYPE_CLASS(obj->type), NULL, 0, TRUE)) {
        CInit_CleanupInitInfo(&initinfo);
        return;
    }

    if (IS_TYPE_REFERENCE(obj->type) && CInit_InitReference(obj, TRUE)) {
        CInit_CleanupInitInfo(&initinfo);
        return;
    }

    if (tk != '=' && (tk != '(' || !copts.cplusplus)) {
        if (IS_TYPE_ARRAY(obj->type)) {
            inner = obj->type;
            while (IS_TYPE_ARRAY(inner)) {
                inner = TYPE_POINTER(inner)->target;
            }
            if (IS_TYPE_CLASS(inner)) {
                if (CInit_ClassNeedsConstruction(TYPE_CLASS(inner))) {
                    CInit_InitializeClassArray(obj, TYPE_CLASS(inner), FALSE);
                    CInit_CleanupInitInfo(&initinfo);
                    return;
                }
                CFunc_CheckClassCtors(TYPE_CLASS(inner));
            }
        }

        if (IS_TYPE_CLASS(obj->type)) {
            CFunc_CheckClassCtors(TYPE_CLASS(obj->type));
        }

        if ((IS_TYPE_REFERENCE(obj->type) || is_const_object(obj)) && copts.cplusplus) {
            CError_Error(ERR_CONST_AMPERSAND_VARIABLE_NEEDS_INITIALIZER);
        }
    } else {
        if (obj->type->size || IS_TYPE_ARRAY(obj->type)) {
            if ((expr = CInit_GenericData(NULL, obj->type, obj->qual, CInit_AutoInit, FALSE))) {
                insert_cb(makediadicnode(create_objectnode2(obj), expr, EASS));
            }
        } else {
            CError_Error(ERR_DATA_TYPE_INCOMPLETE);
        }
    }

    if (IS_TYPE_CLASS(obj->type) && CClass_Destructor(TYPE_CLASS(obj->type))) {
        register_cb(obj->type, obj, 0, FALSE);
    }

    CInit_CleanupInitInfo(&initinfo);
}

void CInit_InitializeStaticData(Object* obj, InitExprRegisterCB cb) {
    ENode* expr;
    Type* type;
    InitInfo initinfo;
    CInit_DataBuffer db;
    CInit_NonConst* entry;
    ENode* obj_expr;

    if (CInit_IsSimpleStructArrayInit(obj->type)) {
        if (tk == '=' || (tk == '(' && copts.cplusplus)) {
            if (tk == '=') {
                tk = lex();
            }
            CInit_InitData(&db, obj->type, obj->qual, copts.cplusplus);

            IsCompleteType(obj->type);

            if (!db.nonconst && is_const_object(obj)) {
                CInit_DeclareReadOnlyData(obj, db.buffer, db.list, db.size);
            } else {
                CInit_DeclareData(obj, db.buffer, db.list, db.size);
            }

            if (db.nonconst) {
                obj_expr = create_objectrefnode(obj);
                entry = db.nonconst;
                expr = NULL;
                while (entry) {
                    expr =
                        CInit_InitConcat(expr, obj_expr, entry->offset, entry->type, entry->expr);
                    entry = entry->next;
                }
                cb(expr);
            }
        } else {
            if (copts.cplusplus && is_const_object(obj)) {
                CError_Error(ERR_CONST_AMPERSAND_VARIABLE_NEEDS_INITIALIZER);
            }

            if (is_const_object(obj)) {
                CInit_DeclareReadOnlyData(obj, NULL, NULL, obj->type->size);
            } else {
                CInit_DeclareData(obj, NULL, NULL, obj->type->size);
            }
        }
        return;
    }

    CInit_SetupInitInfo(&initinfo, obj);
    initinfo.use_register_cb = TRUE;
    initinfo.init_expr_register_cb = cb;

    if (IS_TYPE_CLASS(obj->type) &&
        CInit_ConstructGlobalObject(obj, TYPE_CLASS(obj->type), NULL, 0, TRUE)) {
        IsCompleteType(obj->type);
        CInit_DeclareData(obj, NULL, NULL, obj->type->size);
        CInit_CleanupInitInfo(&initinfo);
        return;
    }

    if (IS_TYPE_REFERENCE(obj->type) && CInit_InitReference(obj, FALSE)) {
        CInit_CleanupInitInfo(&initinfo);
        return;
    }

    if (tk != '=' && (tk != '(' || !copts.cplusplus)) {
        if (IsCompleteType(obj->type)) {
            CInit_DeclareData(obj, NULL, NULL, obj->type->size);
        }

        if (IS_TYPE_ARRAY(obj->type)) {
            type = obj->type;
            while (IS_TYPE_ARRAY(type)) {
                type = TYPE_POINTER(type)->target;
            }
            if (IS_TYPE_CLASS(type)) {
                if (CInit_ClassNeedsConstruction(TYPE_CLASS(type))) {
                    CInit_InitializeClassArray(obj, TYPE_CLASS(type), TRUE);
                    CInit_CleanupInitInfo(&initinfo);
                    return;
                }
                CFunc_CheckClassCtors(TYPE_CLASS(type));
            }
        }

        if (IS_TYPE_CLASS(obj->type)) {
            CFunc_CheckClassCtors(TYPE_CLASS(obj->type));
        }

        if ((IS_TYPE_REFERENCE(obj->type) || is_const_object(obj)) && copts.cplusplus) {
            CError_Error(ERR_CONST_AMPERSAND_VARIABLE_NEEDS_INITIALIZER);
        }
    } else {
        if (obj->type->size || IS_TYPE_ARRAY(obj->type)) {
            if ((expr =
                     CInit_GenericData(obj, obj->type, obj->qual, CInit_GlobalStaticInit, TRUE))) {
                cb(makediadicnode(create_objectnode2(obj), expr, EASS));
            }
        } else {
            CError_Error(ERR_DATA_TYPE_INCOMPLETE);
        }
    }

    CInit_CleanupInitInfo(&initinfo);
}

void CInit_InitializeData(Object* obj) {
    Object* dtor;
    ObjectList* list;
    InitInfo initinfo;
    CInt64 val;
    Boolean needs_construction;
    Type* type;

    if (tk == ':') {
        tk = lex();
        obj->datatype = DABSOLUTE;
        val = CExpr_IntegralConstExpr();
        obj->u.address = CInt64_GetULong(&val);
        return;
    }

    if (tk != '=' && (tk != '(' || !copts.cplusplus)) {
        if (obj->sclass == TK_EXTERN) {
            return;
        }
        if (!copts.cplusplus) {
            if (IsCompleteType(obj->type)) {
                for (list = cinit_tentative; list; list = list->next) {
                    if (list->object == obj) {
                        break;
                    }
                }
                if (!list) {
                    list = galloc(sizeof(ObjectList));
                    list->object = obj;
                    list->next = cinit_tentative;
                    cinit_tentative = list;
                    obj->qual |= Q_TENTATIVE;
                }
            }

            return;
        }

        if (obj->flags & OBJECT_DEFINED) {
            CError_Error(ERR_DATA_OBJECT_REDEFINED, obj);
        }
        obj->flags |= OBJECT_DEFINED;

        needs_construction = FALSE;
        if (IS_TYPE_ARRAY(obj->type)) {
            type = obj->type;
            while (IS_TYPE_ARRAY(type)) {
                type = TYPE_POINTER(type)->target;
            }
            if (IS_TYPE_CLASS(type)) {
                if (CInit_ClassNeedsConstruction(TYPE_CLASS(type))) {
                    CInit_SetupInitInfo(&initinfo, obj);
                    CInit_InitializeClassArray(obj, TYPE_CLASS(type), TRUE);
                    CInit_CleanupInitInfo(&initinfo);
                    needs_construction = TRUE;
                } else {
                    CFunc_CheckClassCtors(TYPE_CLASS(type));
                }
            }
        } else {
            if (IS_TYPE_CLASS(obj->type)) {
                if (CInit_ClassNeedsConstruction(TYPE_CLASS(obj->type))) {
                    CInit_SetupInitInfo(&initinfo, obj);
                    CInit_ConstructGlobalObject(obj, TYPE_CLASS(obj->type), NULL, 0, FALSE);
                    CInit_CleanupInitInfo(&initinfo);
                    needs_construction = TRUE;
                } else {
                    CFunc_CheckClassCtors(TYPE_CLASS(obj->type));
                }
            }
        }

        if (!needs_construction && copts.cplusplus) {
            if (IS_TYPE_REFERENCE(obj->type) || is_const_object(obj)) {
                CError_Error(ERR_CONST_AMPERSAND_VARIABLE_NEEDS_INITIALIZER);
            }
        }
        if (IsCompleteType(obj->type)) {
            CInit_DeclareData(obj, NULL, NULL, obj->type->size);
        }
        return;
    }

    if (obj->flags & OBJECT_DEFINED) {
        CError_Error(ERR_DATA_OBJECT_REDEFINED, obj);
    }

    if (CInit_IsSimpleStructArrayInit(obj->type)) {
        if (tk == '=') {
            tk = lex();
        } else {
            CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
        }
        CInit_GlobalObject(obj);
        return;
    }

    CInit_SetupInitInfo(&initinfo, obj);
    if (IS_TYPE_CLASS(obj->type) &&
        CInit_ConstructGlobalObject(obj, TYPE_CLASS(obj->type), NULL, 0, TRUE)) {
        IsCompleteType(obj->type);
        CInit_DeclareData(obj, NULL, NULL, obj->type->size);
        CInit_CleanupInitInfo(&initinfo);
        return;
    }

    if (IS_TYPE_REFERENCE(obj->type) && CInit_InitReference(obj, FALSE)) {
        CInit_CleanupInitInfo(&initinfo);
        return;
    }

    if (obj->type->size == 0 && !IS_TYPE_ARRAY(obj->type)) {
        CError_Error(ERR_DATA_TYPE_INCOMPLETE);
        CInit_CleanupInitInfo(&initinfo);
        return;
    }

    if (copts.cplusplus) {
        CInit_GenericData(obj, obj->type, obj->qual, &CInit_GlobalStaticInit, FALSE);
    } else {
        CInit_GenericData(obj, obj->type, obj->qual, NULL, FALSE);
    }

    if (IS_TYPE_CLASS(obj->type) && (dtor = CClass_Destructor(TYPE_CLASS(obj->type)))) {
        ENode* expr = CInit_RegisterDtorObject(obj->type, dtor, create_objectrefnode(obj));
        InitExpr_Register(expr, obj);
    }

    CInit_CleanupInitInfo(&initinfo);
}

Object* CInit_DeclareString(char* data, SInt32 size, Boolean ispascal, Boolean iswide) {
    PooledString* str;
    Object* obj;
    PooledString* scan;

    if (!copts.dont_reuse_strings) {
        for (scan = cinit_stringlist; scan; scan = scan->next) {
            if (scan->size == size &&
                scan->ispascal == ispascal &&
                scan->iswide == iswide &&
                !memcmp(scan->data, data, size)) {
                return scan->obj;
            }
        }
    }

    obj = CParser_NewCompilerDefDataObject();
    obj->name = CParser_GetUniqueName();
    if (iswide) {
        obj->type = CDecl_NewArrayType(CParser_GetWCharType(), size);
    } else {
        obj->type = CDecl_NewArrayType(ispascal ? TYPE(&stunsignedchar) : TYPE(&stchar), size);
    }
    obj->sclass = TK_STATIC;
    CScope_AddGlobalObject(obj);

    if (copts.readonly_strings) {
        CInit_DeclareReadOnlyData(obj, data, NULL, obj->type->size);
    } else {
        CInit_DeclareData(obj, data, NULL, obj->type->size);
    }

    str = galloc(sizeof(PooledString));
    str->next = cinit_stringlist;
    cinit_stringlist = str;
    str->obj = obj;
    str->offset = 0;
    str->size = size;
    str->ispascal = ispascal;
    str->iswide = iswide;
    str->data = galloc(size);
    memcpy(str->data, data, size);

    return obj;
}

PooledString* CInit_DeclarePooledString(char* data, SInt32 size, Boolean ispascal) {
    PooledString* str;
    Object* obj;
    SInt32 offset;

    if (!copts.dont_reuse_strings) {
        for (str = cinit_pooledstringlist; str; str = str->next) {
            if (str->size == size && str->ispascal == ispascal && !memcmp(str->data, data, size)) {
                return str;
            }
        }
    }

    if (cinit_pooledstringlist) {
        obj = cinit_pooledstringlist->obj;
        offset = cinit_pooledstringlist->offset + cinit_pooledstringlist->size;
    } else {
        obj = CInit_CreateStaticDataObject(
            CDecl_NewArrayType(ispascal ? TYPE(&stunsignedchar) : TYPE(&stchar), size),
            0,
            GetHashNameNodeExport("@stringBase0")
        );
        PPC_EABI_SetSection(obj, 0, copts.readonly_strings, TRUE);
        offset = 0;
    }

    str = galloc(sizeof(PooledString));
    str->next = cinit_pooledstringlist;
    cinit_pooledstringlist = str;
    str->obj = obj;
    str->offset = offset;
    str->size = size;
    str->ispascal = ispascal;
    str->data = galloc(size);
    memcpy(str->data, data, size);
    return str;
}

PooledString* CInit_DeclarePooledWString(char* data, SInt32 size) {
    PooledString* str;
    Object* obj;
    SInt32 offset;

    if (!copts.dont_reuse_strings) {
        for (str = cinit_pooledwstringlist; str; str = str->next) {
            if (str->size == size && !memcmp(str->data, data, size)) {
                return str;
            }
        }
    }

    if (cinit_pooledwstringlist) {
        obj = cinit_pooledwstringlist->obj;
        offset = cinit_pooledwstringlist->offset + cinit_pooledwstringlist->size;
    } else {
        obj = CInit_CreateStaticDataObject(
            CDecl_NewArrayType(CParser_GetWCharType(), size),
            0,
            GetHashNameNodeExport("@wstringBase0")
        );
        offset = 0;
    }

    str = galloc(sizeof(PooledString));
    str->next = cinit_pooledwstringlist;
    cinit_pooledwstringlist = str;
    str->obj = obj;
    str->offset = offset;
    str->size = size;
    str->ispascal = FALSE;
    str->data = galloc(size);
    memcpy(str->data, data, size);
    return str;
}

void CInit_RewriteString(ENode* expr, Boolean unused) {
    PooledString* str;
    Boolean is_wide;

    if (cparamblkptr->precompile == 1) {
        CError_Error(ERR_ILLEGAL_USE_PRECOMPILED_HEADER);
    }

    CError_ASSERT(4229, expr->rtype->type == TYPEPOINTER);

    is_wide = TYPE_POINTER(expr->rtype)->target->size != 1;
    if (copts.poolstrings) {
        if (is_wide) {
            str = CInit_DeclarePooledWString(expr->data.string.data, expr->data.string.size);
        } else {
            str = CInit_DeclarePooledString(
                expr->data.string.data, expr->data.string.size, expr->data.string.ispascal
            );
        }

        if (str->offset) {
            expr->type = EADD;
            expr->data.diadic.right = intconstnode(TYPE(&stunsignedlong), str->offset);
            expr->data.diadic.left = create_objectrefnode(str->obj);
            expr->cost = 1;
        } else {
            expr->type = EOBJREF;
            expr->data.objref = str->obj;
        }
    } else {
        expr->type = EOBJREF;
        expr->data.objref = CInit_DeclareString(
            expr->data.string.data, expr->data.string.size, expr->data.string.ispascal, is_wide
        );
    }
}

void CInit_DeclarePooledStrings(void) {
    SInt32 size;
    char* buffer;
    PooledString* str;

    size = 0;
    for (str = cinit_pooledstringlist; str; str = str->next) {
        size += str->size;
    }

    if (size) {
        cinit_pooledstringlist->obj->type = CDecl_NewArrayType(TYPE(&stchar), size);
        buffer = galloc(size);
        for (str = cinit_pooledstringlist; str; str = str->next) {
            memcpy(buffer + str->offset, str->data, str->size);
        }

        if (copts.readonly_strings) {
            CInit_DeclareReadOnlyData(cinit_pooledstringlist->obj, buffer, NULL, size);
        } else {
            CInit_DeclareData(cinit_pooledstringlist->obj, buffer, NULL, size);
        }
    }

    size = 0;
    for (str = cinit_pooledwstringlist; str; str = str->next) {
        size += str->size;
    }

    if (size) {
        cinit_pooledwstringlist->obj->type = CDecl_NewArrayType(CParser_GetWCharType(), size);
        buffer = galloc(size);
        for (str = cinit_pooledwstringlist; str; str = str->next) {
            memcpy(buffer + str->offset, str->data, str->size);
        }

        if (copts.readonly_strings) {
            CInit_DeclareReadOnlyData(cinit_pooledwstringlist->obj, buffer, NULL, size);
        } else {
            CInit_DeclareData(cinit_pooledwstringlist->obj, buffer, NULL, size);
        }
    }
}

static void declaredata(
    Object* obj, void* data, OLinkList* list, SInt32 size, Boolean is_readonly
) {
    OLinkList* scan;
    UInt32 qual;

    qual = obj->qual;

    if (cparamblkptr->precompile == 1) {
        PreComp_StaticData(obj, data, list, size);
        return;
    }

    obj->flags |= OBJECT_DEFINED;
    if (!fatalerrors) {
        for (scan = list; scan; scan = scan->next) {
            CInline_ObjectAddrRef(scan->obj);
        }
        if (copts.filesyminfo) {
            CPrep_SetSourceFile(&cparser_fileoffset);
        }
        if (is_readonly) {
            ObjGen_DeclareReadOnlyData(obj, data, list, size);
        } else {
            ObjGen_DeclareData(obj, data, list, size);
        }
        obj->qual = qual;
    }
}

void CInit_DeclareData(Object* obj, void* data, OLinkList* list, SInt32 size) {
    declaredata(obj, data, list, size, FALSE);
}

void CInit_DeclareReadOnlyData(Object* obj, void* data, OLinkList* list, SInt32 size) {
    declaredata(obj, data, list, size, TRUE);
}

void CInit_DefineTentativeData(void) {
    ObjectList* list;

    for (list = cinit_tentative; list; list = list->next) {
        if (!(list->object->flags & OBJECT_DEFINED)) {
            CInit_DeclareData(list->object, NULL, NULL, list->object->type->size);
        }
    }

    cinit_tentative = NULL;
}

#pragma pack(2)
