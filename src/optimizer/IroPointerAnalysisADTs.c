#ifdef IRO_POINTER_ANALYSIS_TU
#include "compiler/common.h"
#include "driver/CLIO.h"
#include "compiler/CInt64.h"
#include "compiler/CError.h"
#include "compiler/objects.h"
#include "compiler/types.h"
#include "compiler/CParser.h"
#include "compiler/IroLinearForm.h"
#include "compiler/IroUtil.h"
#include "compiler/IroMalloc.h"
#include <setjmp.h>
#include <string.h>

#define IRO_ASSERT(line, expr) \
    ((expr) ? (void)0 \
            : __assertion_failed(#expr, "IroPointerAnalysisADTs.c", (line) + CError_LINE_SHIFT))

#define IRO_ASSERT_T(line, expr) \
    ((expr) \
         ? 0 \
         : (__assertion_failed(#expr, "IroPointerAnalysisADTs.c", (line) + CError_LINE_SHIFT), 0))
#define IRO_ASSERT_MSG(line, expr, text) \
    ((expr) ? (void)0 \
            : __assertion_failed(text, "IroPointerAnalysisADTs.c", (line) + CError_LINE_SHIFT))
#define IRO_DEBUG_CLEAR(obj, size) memset((obj), 0xFF, (size))

HEADER_INLINE UInt32 gcd(UInt32 a, UInt32 b) {
    UInt32 chk;

    if (!a) {
        return b;
    }
    if (!b) {
        return a;
    }

    while (1) {
        chk = a % b;
        if (!chk) {
            return b;
        }
        a = b;
        b = chk;
    }
}

typedef UInt32 uint32;
typedef struct ExtendedParam ExtendedParam;
typedef struct ExtendedParamSet ExtendedParamSet;
typedef struct LocationSet LocationSet;
typedef struct LocationSetSet LocationSetSet;
typedef struct ObjectSet ObjectSet;
typedef struct PAHeapBlock PAHeapBlock;
typedef struct PALocalVar PALocalVar;
typedef struct PAMemoryBlock PAMemoryBlock;
typedef struct ParamMapping ParamMapping;
typedef struct ParamMappingFunction ParamMappingFunction;
typedef struct PartialTransferFunction PartialTransferFunction;
typedef struct PointsToEntry PointsToEntry;
typedef struct PointsToFunction PointsToFunction;
typedef struct PTFList PTFList;
typedef struct Stack Stack;
typedef struct StackElement StackElement;

#pragma pack(push, 2)
struct StackElement {
    Object* proc;
    PartialTransferFunction* ptf;
    ParamMappingFunction* map;
    IROLinear* funcCall;
};

struct Stack {
    StackElement* top;
    Stack* next;
};

struct ObjectSet {
    Object* proc;
    ObjectSet* otherProcs;
};

struct ExtendedParam {
    ObjectSet* objectSet;
    UInt32 num;
};

struct ExtendedParamSet {
    ExtendedParam* ep;
    ExtendedParamSet* otherEps;
};

struct PAHeapBlock {
    IROLinear* nd;
};

struct PALocalVar {
    Object* obj;
    char* name;
};

typedef enum PAMemoryBlockKind {
    PAMEMORYBLOCKKIND_INVALID,
    PAMEMORYBLOCKKIND_UNKNOWN,
    PAMEMORYBLOCKKIND_EXTENDEDPARAM,
    PAMEMORYBLOCKKIND_LOCALVAR,
    PAMEMORYBLOCKKIND_HEAPBLOCK,
    PAMEMORYBLOCKKIND_INT,
    PAMEMORYBLOCKKIND_OTHER
} PAMemoryBlockKind;

struct PAMemoryBlock {
    PAMemoryBlockKind kind;
    union {
        ExtendedParam* ep;
        PALocalVar* localvar;
        PAHeapBlock* heapblock;
        CInt64 intval;
        void* other;
    } u;
};

struct LocationSet {
    PAMemoryBlock* block;
    Type* rtype;
    union {
        struct {
            CInt64 field;
            UInt32 stride;
        } known;
        struct {
            PAMemoryBlock* restriction;
            LocationSet* bitfieldOf;
        } unknown;
    } u;
};

struct LocationSetSet {
    LocationSet* loc;
    LocationSetSet* otherLocs;
    UInt8 count;
};

struct ParamMapping {
    IROLinear* actual;
    Object* formal;
    ExtendedParam* extended;
};

struct ParamMappingFunction {
    ParamMapping* mapping;
    ParamMappingFunction* otherMappings;
};

struct PointsToEntry {
    LocationSet* loc;
    LocationSetSet* locs;
};

struct PointsToFunction {
    PointsToEntry* pte;
    PointsToFunction* otherPtes;
};

struct PartialTransferFunction {
    PointsToFunction* initialPointsToFn;
    PointsToFunction* finalPointsToFn;
    LocationSetSet* funcModifies;
    LocationSet* returnLocation;
    Boolean analyzed;
    struct {
        IROLinear* nd;
        PartialTransferFunction* ptf;
    } context;
};

struct PTFList {
    PartialTransferFunction* ptf;
    PTFList* otherPTFs;
};

typedef struct EvalProcActionStatic {
    Object* proc;
    IRONode* fnode;
    ParamMappingFunction* map;
    PartialTransferFunction* ptf;
    PointsToFunction* pointsToFunc;
    Boolean* changed;
    Boolean needMeet;
    Boolean nodeChanged;
} EvalProcActionStatic;
#pragma pack(pop)

static UInt32 stExtendedParamNum;
static PartialTransferFunction* stUnknownPTF;
static UInt32 stIndentationLevel;
static UInt8 stTabs[0x2A];
static Stack* stCallingContextStack;
static ObjectList* stParamObjs;
static jmp_buf stAbortPointerAnalysis;
static Object stUnknown;
static Object* stCurrentProc;
static ExtendedParamSet* stExtParamSet;
static PTFList* stPTFList;
static UInt32 stMaxPassCount;
static EvalProcActionStatic stEvalProcActionParams;
static IRONode* stExceptionFNode;

static PAMemoryBlock stDummyMemoryBlock = {PAMEMORYBLOCKKIND_UNKNOWN};
static PAMemoryBlock* stUnknownMb = &stDummyMemoryBlock;

static LocationSet stDummyLocationSet = {&stDummyMemoryBlock};
static LocationSet* stUnknownLs = &stDummyLocationSet;

HEADER_INLINE StackElement* Stack_Pop(Stack** stackPtr);
HEADER_INLINE void ObjectSet_RemoveAll(ObjectSet* procList);
HEADER_INLINE void ExtendedParamSet_RemoveAll(ExtendedParamSet* epList);
HEADER_INLINE void LocationSet_Copy(LocationSet* dest, LocationSet* src);
HEADER_INLINE Boolean LocationSet_IsUnknown(LocationSet* ls);
HEADER_INLINE void LocationSetSet_RemoveAll(LocationSetSet* lss);
HEADER_INLINE void LocationSetSet_AddSet(LocationSetSet* dest, LocationSetSet* src);
HEADER_INLINE void ParamMappingFunction_RemoveAll(ParamMappingFunction* pmf);
HEADER_INLINE void ParamMappingFunction_AddAll(
    ParamMappingFunction* dest, ParamMappingFunction* src
);
HEADER_INLINE void PointsToFunction_RemoveAll(PointsToFunction* pointsToFunc);
HEADER_INLINE void PointsToFunction_AddAll(PointsToFunction* dest, PointsToFunction* src);
HEADER_INLINE void PTFList_RemoveAll(PTFList* ptfList);

HEADER_INLINE StackElement* StackElement_New(void) {
    StackElement* stackElement = IRO_malloc(sizeof(StackElement));
    IRO_ASSERT(103, stackElement != NULL);
    stackElement->proc = NULL;
    stackElement->ptf = NULL;
    stackElement->map = NULL;
    stackElement->funcCall = NULL;
    return stackElement;
}

HEADER_INLINE void StackElement_Delete(StackElement* stackElement) {
    IRO_ASSERT(117, stackElement != NULL);
    IRO_ASSERT(118, stackElement->proc == NULL);
    IRO_ASSERT(119, stackElement->ptf == NULL);
    IRO_ASSERT(120, stackElement->map == NULL);
    IRO_ASSERT(121, stackElement->funcCall == NULL);
    IRO_DEBUG_CLEAR(stackElement, sizeof(StackElement));
    IRO_free(stackElement);
}

HEADER_INLINE void StackElement_Init(
    StackElement* stackElement,
    Object* proc,
    PartialTransferFunction* ptf,
    ParamMappingFunction* map,
    IROLinear* funcCall
) {
    IRO_ASSERT(131, stackElement != NULL);
    IRO_ASSERT(132, proc != NULL);
    IRO_ASSERT(133, ptf != NULL);
    IRO_ASSERT(134, map != NULL);
    IRO_ASSERT(135, funcCall != NULL);
    stackElement->proc = proc;
    stackElement->ptf = ptf;
    stackElement->map = map;
    stackElement->funcCall = funcCall;
}

HEADER_INLINE void StackElement_Copy(StackElement* dest, StackElement* src) {
    IRO_ASSERT(145, dest != NULL);
    IRO_ASSERT(146, src != NULL);
    StackElement_Init(dest, src->proc, src->ptf, src->map, src->funcCall);
}

HEADER_INLINE void StackElement_Term(StackElement* stackElement) {
    IRO_ASSERT(156, stackElement != NULL);
    stackElement->proc = NULL;
    stackElement->ptf = NULL;
    stackElement->map = NULL;
    stackElement->funcCall = NULL;
}

HEADER_INLINE void* StackElement_key(StackElement* stackElement) {
    IRO_ASSERT(213, stackElement != NULL);
    return stackElement->proc;
}

HEADER_INLINE Boolean StackElement_KeysEqual(void* key1, void* key2) {
    IRO_ASSERT(220, key1 != NULL);
    IRO_ASSERT(221, key2 != NULL);
    return key1 == key2;
}

HEADER_INLINE Object* StackElement_proc(StackElement* stackElement) {
    IRO_ASSERT(228, stackElement != NULL);
    return stackElement->proc;
}

HEADER_INLINE PartialTransferFunction* StackElement_ptf(StackElement* stackElement) {
    IRO_ASSERT(235, stackElement != NULL);
    return stackElement->ptf;
}

HEADER_INLINE ParamMappingFunction* StackElement_map(StackElement* stackElement) {
    IRO_ASSERT(242, stackElement != NULL);
    return stackElement->map;
}

HEADER_INLINE IROLinear* StackElement_funcCall(StackElement* stackElement) {
    IRO_ASSERT(249, stackElement != NULL);
    return stackElement->funcCall;
}

HEADER_INLINE Stack* Stack_New(void) {
    Stack* stack = IRO_malloc(sizeof(Stack));
    IRO_ASSERT(265, stack != NULL);
    stack->top = NULL;
    stack->next = NULL;
    return stack;
}

HEADER_INLINE void Stack_Delete(Stack* stack) {
    IRO_ASSERT(277, stack != NULL);
    IRO_ASSERT(278, stack->top == NULL);
    IRO_ASSERT(279, stack->next == NULL);
    IRO_DEBUG_CLEAR(stack, sizeof(Stack));
    IRO_free(stack);
}

HEADER_INLINE void Stack_Init(Stack* stack) {
    IRO_ASSERT(289, stack != NULL);
    stack->top = NULL;
    stack->next = NULL;
}

HEADER_INLINE void Stack_Term(Stack** stackPtr) {
    StackElement* stackElement;

    IRO_ASSERT(299, stackPtr != NULL);
    IRO_ASSERT(300, *stackPtr != NULL);

    while ((*stackPtr)->top) {
        stackElement = Stack_Pop(stackPtr);
        StackElement_Term(stackElement);
        StackElement_Delete(stackElement);
    }
}

HEADER_INLINE void Stack_Push(Stack** stackPtr, StackElement* stackElement) {
    StackElement* newElement;
    Stack* newStack;

    IRO_ASSERT(315, stackPtr != NULL);
    IRO_ASSERT(316, *stackPtr != NULL);

    newElement = StackElement_New();
    StackElement_Copy(newElement, stackElement);

    newStack = Stack_New();
    newStack->top = newElement;
    newStack->next = *stackPtr;
    *stackPtr = newStack;
}

HEADER_INLINE StackElement* Stack_Top(Stack** stackPtr) {
    StackElement* top;

    IRO_ASSERT(331, stackPtr != NULL);
    IRO_ASSERT(332, *stackPtr != NULL);

    top = (*stackPtr)->top;
    return top;
}

HEADER_INLINE Stack* Stack_Next(Stack** stackPtr) {
    IRO_ASSERT(343, stackPtr != NULL);
    IRO_ASSERT(344, *stackPtr != NULL);

    return (*stackPtr)->next;
}

HEADER_INLINE StackElement* Stack_Pop(Stack** stackPtr) {
    StackElement* stackElement;

    IRO_ASSERT(357, stackPtr != NULL);
    IRO_ASSERT(358, *stackPtr != NULL);

    stackElement = (*stackPtr)->top;
    if (stackElement) {
        Stack* next = (*stackPtr)->next;
        (*stackPtr)->top = NULL;
        (*stackPtr)->next = NULL;
        Stack_Delete(*stackPtr);
        *stackPtr = next;
    }

    return stackElement;
}

HEADER_INLINE StackElement* Stack_FindByKey(Stack** stackPtr, void* key) {
    Stack* stack;

    IRO_ASSERT(379, stackPtr != NULL);
    IRO_ASSERT(380, key != NULL);

    for (stack = *stackPtr; stack; stack = stack->next) {
        StackElement* element = stack->top;
        if (element) {
            if (StackElement_KeysEqual(StackElement_key(element), key)) {
                return element;
            }
        }
    }

    return NULL;
}

HEADER_INLINE ObjectSet* ObjectSet_New(void) {
    ObjectSet* procList;

    procList = IRO_malloc(sizeof(ObjectSet));
    IRO_ASSERT(439, procList != NULL);
    procList->proc = NULL;
    procList->otherProcs = NULL;
    return procList;
}

HEADER_INLINE void ObjectSet_Delete(ObjectSet* procList) {
    IRO_ASSERT(451, procList != NULL);
    IRO_ASSERT(452, procList->proc == NULL);
    IRO_ASSERT(453, procList->otherProcs == NULL);
    IRO_DEBUG_CLEAR(procList, sizeof(ObjectSet));
    IRO_free(procList);
}

HEADER_INLINE void ObjectSet_Init(ObjectSet* procList) {
    IRO_ASSERT(463, procList != NULL);
    procList->proc = NULL;
    procList->otherProcs = NULL;
}

HEADER_INLINE void ObjectSet_Term(ObjectSet* procList) {
    IRO_ASSERT(481, procList != NULL);
    ObjectSet_RemoveAll(procList);
    procList->proc = NULL;
    procList->otherProcs = NULL;
}

HEADER_INLINE void ObjectSet_ForEach(
    ObjectSet* procList, void (*action)(Object*, void*), void* refcon
) {
    IRO_ASSERT(528, procList != NULL);
    IRO_ASSERT(529, action != NULL);
    IRO_ASSERT(530, refcon == NULL || refcon != NULL);

    while (procList && procList->proc) {
        action(procList->proc, refcon);
        procList = procList->otherProcs;
    }
}

HEADER_INLINE Object* ObjectSet_Find(ObjectSet* procList, Object* proc) {
    IRO_ASSERT(540, procList != NULL);
    IRO_ASSERT(541, proc != NULL);
    while (procList && procList->proc) {
        if (procList->proc == proc) {
            return procList->proc;
        }
        procList = procList->otherProcs;
    }
    return NULL;
}

HEADER_INLINE Object* ObjectSet_FindFirst(ObjectSet* procList) {
    IRO_ASSERT(552, procList != NULL);
    return procList->proc;
}

HEADER_INLINE int ObjectSet_Count(ObjectSet* procList) {
    int count;

    IRO_ASSERT(561, procList != NULL);

    count = 0;
    while (procList && procList->proc) {
        count++;
        procList = procList->otherProcs;
    }

    return count;
}

HEADER_INLINE void ObjectSet_SimpleAdd(ObjectSet* procList, Object* proc) {
    ObjectSet* newProcList;

    IRO_ASSERT(574, procList != NULL);
    IRO_ASSERT(575, proc != NULL);

    if (procList->proc) {
        newProcList = ObjectSet_New();
        ObjectSet_Init(newProcList);
        newProcList->proc = procList->proc;
        newProcList->otherProcs = procList->otherProcs;
        procList->otherProcs = newProcList;
    }

    procList->proc = proc;
}

HEADER_INLINE void ObjectSet_Add(ObjectSet* procList, Object* proc) {
    IRO_ASSERT(592, procList != NULL);
    IRO_ASSERT(593, proc != NULL);

    if (!ObjectSet_Find(procList, proc)) {
        ObjectSet_SimpleAdd(procList, proc);
    }
}

HEADER_INLINE void ObjectSet_Remove(ObjectSet* procList, Object* proc) {
    ObjectSet* prev;
    ObjectSet* second;

    IRO_ASSERT(605, procList != NULL);
    IRO_ASSERT(606, proc != NULL);

    prev = NULL;
    while (procList && procList->proc) {
        if (procList->proc == proc) {
            if (!prev) {
                if (procList->otherProcs == NULL) {
                    procList->proc = NULL;
                } else {
                    second = procList->otherProcs;
                    procList->proc = procList->otherProcs->proc;
                    procList->otherProcs = procList->otherProcs->otherProcs;
                    second->proc = NULL;
                    second->otherProcs = NULL;
                    ObjectSet_Term(second);
                    ObjectSet_Delete(second);
                }
            } else {
                prev->otherProcs = procList->otherProcs;
                procList->proc = NULL;
                procList->otherProcs = NULL;
                ObjectSet_Term(procList);
                ObjectSet_Delete(procList);
            }
            return;
        }
        prev = procList;
        procList = procList->otherProcs;
    }
}

HEADER_INLINE void ObjectSet_RemoveAll(ObjectSet* procList) {
    IRO_ASSERT(645, procList != NULL);

    while (procList && procList->proc) {
        ObjectSet_Remove(procList, procList->proc);
    }
}

HEADER_INLINE void ObjectSet_AddSetAction(Object* proc, void* refcon) {
    IRO_ASSERT(655, proc != NULL);
    IRO_ASSERT(656, refcon != NULL);

    ObjectSet_Add(refcon, proc);
}

HEADER_INLINE void ObjectSet_SimpleAddSetAction(Object* proc, void* refcon) {
    IRO_ASSERT(663, proc != NULL);
    IRO_ASSERT(664, refcon != NULL);

    ObjectSet_SimpleAdd(refcon, proc);
}

HEADER_INLINE void ObjectSet_AddSet(ObjectSet* dest, ObjectSet* src) {
    IRO_ASSERT(671, dest != NULL);
    IRO_ASSERT(672, src != NULL);

    if (dest->proc) {
        ObjectSet_ForEach(src, ObjectSet_AddSetAction, dest);
    } else {
        ObjectSet_ForEach(src, ObjectSet_SimpleAddSetAction, dest);
    }
}

HEADER_INLINE void ObjectSet_RemoveSetAction(Object* proc, void* refcon) {
    IRO_ASSERT(682, proc != NULL);
    IRO_ASSERT(683, refcon != NULL);

    ObjectSet_Remove(refcon, proc);
}

HEADER_INLINE void ObjectSet_RemoveSet(ObjectSet* dest, ObjectSet* src) {
    IRO_ASSERT(690, dest != NULL);
    IRO_ASSERT(691, src != NULL);

    ObjectSet_ForEach(src, ObjectSet_RemoveSetAction, dest);
}

HEADER_INLINE Boolean ObjectSets_Equal(ObjectSet* os1, ObjectSet* os2) {
    ObjectSet* scan;

    IRO_ASSERT(700, os1 != NULL);
    IRO_ASSERT(701, os2 != NULL);

    if (os1 == os2) {
        return TRUE;
    }

    for (scan = os1; scan && scan->proc; scan = scan->otherProcs) {
        if (!ObjectSet_Find(os2, scan->proc)) {
            return FALSE;
        }
    }

    for (scan = os2; scan && scan->proc; scan = scan->otherProcs) {
        if (!ObjectSet_Find(os1, scan->proc)) {
            return FALSE;
        }
    }

    return TRUE;
}

HEADER_INLINE ExtendedParam* ExtendedParam_New(void) {
    ExtendedParam* ep = IRO_malloc(sizeof(ExtendedParam));

    IRO_ASSERT(755, ep != NULL);
    ep->objectSet = NULL;
    return ep;
}

HEADER_INLINE void ExtendedParam_Delete(ExtendedParam* ep) {
    IRO_ASSERT(762, ep != NULL);
    IRO_ASSERT(763, ep->objectSet == NULL);
    IRO_DEBUG_CLEAR(ep, sizeof(ExtendedParam));
    IRO_free(ep);
}

HEADER_INLINE void ExtendedParam_Init(ExtendedParam* ep, Object* obj) {
    IRO_ASSERT(777, ep != NULL);
    IRO_ASSERT(778, obj != NULL);
    IRO_ASSERT(779, obj->extParam == NULL);
    IRO_ASSERT(780, stExtendedParamNum < ((uint32) -1) / 2 - 1);

    ep->objectSet = ObjectSet_New();
    ObjectSet_Init(ep->objectSet);
    ObjectSet_Add(ep->objectSet, obj);
    obj->extParam = ep;

    ep->num = stExtendedParamNum++;
}

HEADER_INLINE void ExtendedParam_TermAction(Object* obj, void* refcon) {
    obj->extParam = NULL;
}

HEADER_INLINE void ExtendedParam_Term(ExtendedParam* ep) {
    IRO_ASSERT(800, ep != NULL);

    ObjectSet_ForEach(ep->objectSet, ExtendedParam_TermAction, NULL);
    ObjectSet_Term(ep->objectSet);
    ObjectSet_Delete(ep->objectSet);
}

HEADER_INLINE Boolean ExtendedParams_Equal(ExtendedParam* ep1, ExtendedParam* ep2) {
    IRO_ASSERT(841, ep1 != NULL);
    IRO_ASSERT(842, ep2 != NULL);
    IRO_ASSERT(843, ep1->objectSet != NULL);
    IRO_ASSERT(844, ep2->objectSet != NULL);

    if (ep1 == ep2) {
        return TRUE;
    }

    return ep1->num == ep2->num && ObjectSets_Equal(ep1->objectSet, ep2->objectSet);
}

HEADER_INLINE ExtendedParam* ExtendedParam_FindByObject(Object* obj) {
    IRO_ASSERT(856, obj != NULL);

    return obj->extParam;
}

HEADER_INLINE void ExtendedParam_AddObject(ExtendedParam* ep, Object* obj) {
    IRO_ASSERT(863, ep != NULL);
    IRO_ASSERT(864, ep->objectSet != NULL);
    IRO_ASSERT(865, obj != NULL);

    ObjectSet_Add(ep->objectSet, obj);
    obj->extParam = ep;
}

HEADER_INLINE void ExtendedParam_RemoveObjects(ExtendedParam* ep, ObjectSet* objSet) {
    IRO_ASSERT(888, ep != NULL);
    IRO_ASSERT(889, ep->objectSet != NULL);
    IRO_ASSERT(890, objSet != NULL);

    ObjectSet_RemoveSet(ep->objectSet, objSet);
    ObjectSet_ForEach(objSet, ExtendedParam_TermAction, NULL);
}

HEADER_INLINE ObjectSet* ExtendedParam_objectSet(ExtendedParam* ep) {
    IRO_ASSERT(898, ep != NULL);

    return ep->objectSet;
}

HEADER_INLINE UInt32 ExtendedParam_num(ExtendedParam* ep) {
    IRO_ASSERT(905, ep != NULL);

    return ep->num;
}

HEADER_INLINE ExtendedParamSet* ExtendedParamSet_New(void) {
    ExtendedParamSet* epList = IRO_malloc(sizeof(ExtendedParamSet));

    IRO_ASSERT(924, epList != NULL);
    epList->ep = NULL;
    epList->otherEps = NULL;
    return epList;
}

HEADER_INLINE void ExtendedParamSet_Delete(ExtendedParamSet* epList) {
    IRO_ASSERT(936, epList != NULL);
    IRO_ASSERT(937, epList->ep == NULL);
    IRO_ASSERT(938, epList->otherEps == NULL);
    IRO_DEBUG_CLEAR(epList, sizeof(ExtendedParamSet));
    IRO_free(epList);
}

HEADER_INLINE void ExtendedParamSet_Init(ExtendedParamSet* epList) {
    IRO_ASSERT(948, epList != NULL);
    epList->ep = NULL;
    epList->otherEps = NULL;
}

HEADER_INLINE void ExtendedParamSet_Term(ExtendedParamSet* epList) {
    IRO_ASSERT(966, epList != NULL);
    ExtendedParamSet_RemoveAll(epList);
    epList->ep = NULL;
    epList->otherEps = NULL;
}

HEADER_INLINE void ExtendedParamSet_ForEach(
    ExtendedParamSet* epList, void (*action)(ExtendedParam*, void*), void* refcon
) {
    IRO_ASSERT(1010, epList != NULL);
    IRO_ASSERT(1011, action != NULL);

    while (epList && epList->ep) {
        action(epList->ep, refcon);
        epList = epList->otherEps;
    }
}

HEADER_INLINE ExtendedParam* ExtendedParamSet_Find(ExtendedParamSet* epList, ExtendedParam* ep) {
    IRO_ASSERT(1022, epList != NULL);
    IRO_ASSERT(1023, ep != NULL);

    while (epList && epList->ep) {
        if (epList->ep == ep) {
            return epList->ep;
        }
        epList = epList->otherEps;
    }

    return NULL;
}

HEADER_INLINE void ExtendedParamSet_SimpleAdd(ExtendedParamSet* epList, ExtendedParam* ep) {
    IRO_ASSERT(1056, epList != NULL);
    IRO_ASSERT(1057, ep != NULL);

    if (epList->ep) {
        ExtendedParamSet* newSet = ExtendedParamSet_New();
        ExtendedParamSet_Init(newSet);
        newSet->ep = epList->ep;
        newSet->otherEps = epList->otherEps;
        epList->otherEps = newSet;
    }

    epList->ep = ep;
}

HEADER_INLINE void ExtendedParamSet_Add(ExtendedParamSet* epList, ExtendedParam* ep) {
    IRO_ASSERT(1076, epList != NULL);
    IRO_ASSERT(1077, ep != NULL);

    if (!ExtendedParamSet_Find(epList, ep)) {
        ExtendedParamSet_SimpleAdd(epList, ep);
    }
}

HEADER_INLINE void ExtendedParamSet_Remove(ExtendedParamSet* epList, ExtendedParam* ep) {
    ExtendedParamSet* prev;
    ExtendedParamSet* second;

    IRO_ASSERT(1089, epList != NULL);
    IRO_ASSERT(1090, ep != NULL);

    prev = NULL;
    while (epList && epList->ep) {
        if (epList->ep == ep) {
            if (!prev) {
                if (epList->otherEps == NULL) {
                    epList->ep = NULL;
                } else {
                    second = epList->otherEps;
                    epList->ep = epList->otherEps->ep;
                    epList->otherEps = epList->otherEps->otherEps;
                    second->ep = NULL;
                    second->otherEps = NULL;
                    ExtendedParamSet_Term(second);
                    ExtendedParamSet_Delete(second);
                }
            } else {
                prev->otherEps = epList->otherEps;
                epList->ep = NULL;
                epList->otherEps = NULL;
                ExtendedParamSet_Term(epList);
                ExtendedParamSet_Delete(epList);
            }
            return;
        }
        prev = epList;
        epList = epList->otherEps;
    }
}

HEADER_INLINE void ExtendedParamSet_RemoveAll(ExtendedParamSet* epList) {
    IRO_ASSERT(1129, epList != NULL);

    while (epList && epList->ep) {
        ExtendedParamSet_Remove(epList, epList->ep);
    }
}

HEADER_INLINE PAHeapBlock* PAHeapBlock_New(void) {
    PAHeapBlock* hb = IRO_malloc(sizeof(PAHeapBlock));

    IRO_ASSERT(1225, hb != NULL);
    hb->nd = NULL;
    return hb;
}

HEADER_INLINE void PAHeapBlock_Init(PAHeapBlock* hb, IROLinear* nd) {
    IRO_ASSERT(1247, hb == NULL || hb != NULL);

    hb->nd = nd;
}

HEADER_INLINE Boolean PAHeapBlocks_Equal(PAHeapBlock* hb1, PAHeapBlock* hb2) {
    IRO_ASSERT(1296, hb1 != NULL);
    IRO_ASSERT(1297, hb2 != NULL);

    return (hb1 == hb2) || (hb1->nd == hb2->nd);
}

HEADER_INLINE PALocalVar* PALocalVar_New(void) {
    PALocalVar* local = IRO_malloc(sizeof(PALocalVar));

    IRO_ASSERT(1333, local != NULL);
    local->obj = NULL;
    local->name = NULL;
    return local;
}

HEADER_INLINE void PALocalVar_InitByObject(PALocalVar* local, Object* obj) {
    IRO_ASSERT(1357, local != NULL);
    IRO_ASSERT(1358, obj != NULL);

    local->obj = obj;
    if (obj->name && &obj->name->name[0]) {
        local->name = IRO_malloc(strlen(obj->name->name) + 1);
        strcpy(local->name, obj->name->name);
    } else {
        local->name = NULL;
    }
}

HEADER_INLINE void PALocalVar_InitByName(PALocalVar* local, char* name) {
    IRO_ASSERT(1372, local != NULL);
    IRO_ASSERT(1373, name != NULL);

    local->obj = NULL;
    local->name = IRO_malloc(strlen(name) + 1);
    strcpy(local->name, name);
}

HEADER_INLINE Boolean PALocalVars_Equal(PALocalVar* local1, PALocalVar* local2) {
    IRO_ASSERT(1419, local1 == NULL || local1 != NULL);
    IRO_ASSERT(1420, local2 == NULL || local2 != NULL);

    if (local1 == local2) {
        return TRUE;
    }
    if (!local1 || !local2) {
        return FALSE;
    }

    if (!local1->obj || !local2->obj) {
        if (local1->name) {
            return local2->name && strcmp(local1->name, local2->name) == 0;
        }
    }

    return local1->obj == local2->obj;
}

HEADER_INLINE void PALocalVar_SetObject(PALocalVar* local, Object* obj) {
    IRO_ASSERT(1436, local != NULL);
    IRO_ASSERT(1437, obj == NULL || obj != NULL);

    local->obj = obj;
}

HEADER_INLINE Object* PALocalVar_obj(PALocalVar* local) {
    IRO_ASSERT(1444, local != NULL);
    return local->obj;
}

HEADER_INLINE char* PALocalVar_name(PALocalVar* local) {
    IRO_ASSERT(1451, local != NULL);
    return local->name;
}

HEADER_INLINE PAMemoryBlock* PAMemoryBlock_New(void) {
    PAMemoryBlock* mb = IRO_malloc(sizeof(PAMemoryBlock));

    IRO_ASSERT(1491, mb != NULL);
    mb->kind = PAMEMORYBLOCKKIND_INVALID;
    return mb;
}

HEADER_INLINE void PAMemoryBlock_Delete(PAMemoryBlock* mb) {
    IRO_ASSERT(1502, mb != NULL);
    IRO_ASSERT(1503, mb->kind == PAMEMORYBLOCKKIND_INVALID);
    IRO_DEBUG_CLEAR(mb, sizeof(PAMemoryBlock));
    IRO_free(mb);
}

HEADER_INLINE void PAMemoryBlock_Init(PAMemoryBlock* mb, PAMemoryBlockKind kind, void* thing) {
    IRO_ASSERT(1513, mb != NULL);
    IRO_ASSERT(1514, thing == NULL || thing != NULL);

    mb->kind = kind;
    switch (mb->kind) {
        case PAMEMORYBLOCKKIND_EXTENDEDPARAM:
            mb->u.ep = (ExtendedParam*)thing;
            break;
        case PAMEMORYBLOCKKIND_LOCALVAR:
            mb->u.localvar = (PALocalVar*)thing;
            break;
        case PAMEMORYBLOCKKIND_HEAPBLOCK:
            mb->u.heapblock = (PAHeapBlock*)thing;
            break;
        case PAMEMORYBLOCKKIND_INT:
            mb->u.intval = *((CInt64*)thing);
            break;
        case PAMEMORYBLOCKKIND_OTHER:
            mb->u.other = thing;
            break;
        default:
            CError_FATAL(1535);
    }
}

HEADER_INLINE void PAMemoryBlock_Term(PAMemoryBlock* mb) {
    IRO_ASSERT(1552, mb != NULL);

    mb->kind = PAMEMORYBLOCKKIND_INVALID;
}

HEADER_INLINE Boolean MemoryBlocks_Equal(PAMemoryBlock* mb1, PAMemoryBlock* mb2) {
    IRO_ASSERT(1657, mb1 == NULL || mb1 != NULL);
    IRO_ASSERT(1658, mb2 == NULL || mb2 != NULL);

    if (mb1 == mb2) {
        return TRUE;
    }

    if (!mb1 || !mb2 || mb1->kind != mb2->kind) {
        return FALSE;
    }

    switch (mb1->kind) {
        case PAMEMORYBLOCKKIND_EXTENDEDPARAM:
            return ExtendedParams_Equal(mb1->u.ep, mb2->u.ep);
        case PAMEMORYBLOCKKIND_LOCALVAR:
            return PALocalVars_Equal(mb1->u.localvar, mb2->u.localvar);
        case PAMEMORYBLOCKKIND_HEAPBLOCK:
            return PAHeapBlocks_Equal(mb1->u.heapblock, mb2->u.heapblock);
        case PAMEMORYBLOCKKIND_INT:
            return CInt64_Equal(mb1->u.intval, mb2->u.intval);
        case PAMEMORYBLOCKKIND_OTHER:
            return mb1->u.other == mb2->u.other;
        default:
            CError_FATAL(1684);
            return FALSE;
    }
}

HEADER_INLINE PAMemoryBlockKind PAMemoryBlock_kind(PAMemoryBlock* mb) {
    IRO_ASSERT(1692, mb != NULL);

    return mb->kind;
}

HEADER_INLINE void* PAMemoryBlock_thing(PAMemoryBlock* mb) {
    IRO_ASSERT(1699, mb != NULL);

    switch (mb->kind) {
        case PAMEMORYBLOCKKIND_EXTENDEDPARAM:
            return mb->u.ep;
        case PAMEMORYBLOCKKIND_LOCALVAR:
            return mb->u.localvar;
        case PAMEMORYBLOCKKIND_HEAPBLOCK:
            return mb->u.heapblock;
        case PAMEMORYBLOCKKIND_INT:
            return &mb->u.intval;
        case PAMEMORYBLOCKKIND_OTHER:
            return mb->u.other;
        default:
            CError_FATAL(1719);
            return NULL;
    }
}

HEADER_INLINE LocationSet* LocationSet_New(void) {
    LocationSet* ls = IRO_malloc(sizeof(LocationSet));

    IRO_ASSERT(1767, ls != NULL);
    ls->block = NULL;
    ls->rtype = NULL;
    ls->u.known.field = cint64_zero;
    ls->u.known.stride = 0;
    return ls;
}

HEADER_INLINE void LocationSet_Delete(LocationSet* ls) {
    IRO_ASSERT(1781, ls != NULL);
    IRO_ASSERT(1782, ls != stUnknownLs);
    IRO_ASSERT(1783, ls->block == NULL);
    IRO_ASSERT(1784, CInt64_IsZero(&ls->u.known.field));
    IRO_ASSERT(1785, ls->u.known.stride == 0);
    IRO_ASSERT(1786, ls->rtype == NULL);
    IRO_DEBUG_CLEAR(ls, sizeof(LocationSet));
    IRO_free(ls);
}

HEADER_INLINE void LocationSet_InitKnown(
    LocationSet* ls, PAMemoryBlock* block, CInt64 field, UInt32 stride, Type* rtype
) {
    IRO_ASSERT(1796, ls != NULL);
    IRO_ASSERT(1797, ls != stUnknownLs);
    IRO_ASSERT(1798, block != NULL);
    IRO_ASSERT(1799, rtype == NULL || rtype != NULL);
    ls->block = block;
    ls->rtype = rtype;
    ls->u.known.field = field;
    ls->u.known.stride = stride;
}

HEADER_INLINE void LocationSet_InitUnknown(
    LocationSet* ls, Type* rtype, PAMemoryBlock* restriction, LocationSet* bitfieldOf
) {
    IRO_ASSERT(1809, ls != NULL);
    IRO_ASSERT(1810, ls != stUnknownLs);
    IRO_ASSERT(1811, rtype == NULL || rtype != NULL);
    IRO_ASSERT(1812, restriction == NULL || restriction != NULL);
    IRO_ASSERT(1813, bitfieldOf == NULL || bitfieldOf != NULL);

    LocationSet_Copy(ls, stUnknownLs);
    ls->rtype = rtype;
    ls->u.unknown.restriction = restriction;
    if (bitfieldOf) {
        ls->u.unknown.bitfieldOf = LocationSet_New();
        LocationSet_Copy(ls->u.unknown.bitfieldOf, bitfieldOf);
    } else {
        ls->u.unknown.bitfieldOf = NULL;
    }
}

HEADER_INLINE void LocationSet_Copy(LocationSet* dest, LocationSet* src) {
    IRO_ASSERT(1829, src != NULL);
    IRO_ASSERT(1830, dest != NULL);

    dest->block = src->block;
    dest->rtype = src->rtype;

    if (!LocationSet_IsUnknown(src)) {
        dest->u.known.field = src->u.known.field;
        dest->u.known.stride = src->u.known.stride;
    } else {
        dest->u.unknown.restriction = src->u.unknown.restriction;
        if (src->u.unknown.bitfieldOf != NULL) {
            dest->u.unknown.bitfieldOf = LocationSet_New();
            LocationSet_Copy(dest->u.unknown.bitfieldOf, src->u.unknown.bitfieldOf);
        } else {
            dest->u.unknown.bitfieldOf = NULL;
        }
    }
}

HEADER_INLINE void LocationSet_Term(LocationSet* ls) {
    IRO_ASSERT(1857, ls != NULL);
    IRO_ASSERT(1858, ls != stUnknownLs);

    if (LocationSet_IsUnknown(ls) && ls->u.unknown.bitfieldOf) {
        LocationSet_Term(ls->u.unknown.bitfieldOf);
        LocationSet_Delete(ls->u.unknown.bitfieldOf);
    }
    ls->block = NULL;
    ls->rtype = NULL;
    ls->u.known.field = cint64_zero;
    ls->u.known.stride = 0;
}

#pragma sym off
HEADER_INLINE Boolean
LocationSets_Overlap(LocationSet* ls1, Type* rtype1, LocationSet* ls2, Type* rtype2) {
    Boolean isUnknown1, isUnknown2;
    PAMemoryBlock *restriction1, *restriction2;

    IRO_ASSERT(1974, ls1 != NULL);
    IRO_ASSERT(1975, rtype1 == NULL || rtype1 != NULL);
    IRO_ASSERT(1976, ls2 != NULL);
    IRO_ASSERT(1977, rtype2 == NULL || rtype2 != NULL);

    if (ls1 == ls2) {
        return TRUE;
    }

    isUnknown1 = LocationSet_IsUnknown(ls1);
    restriction1 = isUnknown1 ? ls1->u.unknown.restriction : NULL;

    isUnknown2 = LocationSet_IsUnknown(ls2);
    restriction2 = isUnknown2 ? ls2->u.unknown.restriction : NULL;

    if ((isUnknown1 && !restriction1) ||
        (isUnknown2 && !restriction2) ||
        (isUnknown1 && isUnknown2 && MemoryBlocks_Equal(restriction1, restriction2))) {
        return TRUE;
    }

    if (isUnknown1 || isUnknown2) {
        return FALSE;
    }

    if (MemoryBlocks_Equal(ls1->block, ls2->block)) {
        UInt32 size1;
        UInt32 size2;
        UInt32 i;
        CInt64 work;
        CInt64 longgcd;

        if (rtype1) {
            size1 = rtype1->size;
        } else {
            size1 = (UInt32)-1;
        }

        if (rtype2) {
            size2 = rtype2->size;
        } else {
            size2 = (UInt32)-1;
        }

        if (ls1->u.known.stride == ls2->u.known.stride) {
            CInt64 longsize1;
            CInt64 longsize2;
            CInt64_SetULong(&longsize1, size1);
            CInt64_SetULong(&longsize2, size2);

            return CInt64_Equal(ls1->u.known.field, ls2->u.known.field) ||
                (CInt64_Less(ls1->u.known.field, ls2->u.known.field) &&
                 CInt64_Greater(CInt64_Add(ls1->u.known.field, longsize1), ls2->u.known.field)) ||
                (CInt64_Less(ls2->u.known.field, ls1->u.known.field) &&
                 CInt64_Greater(CInt64_Add(ls2->u.known.field, longsize2), ls1->u.known.field));
        } else {
            work = CInt64_Sub(ls1->u.known.field, ls2->u.known.field);
            if (CInt64_IsNegative(&work)) {
                work = CInt64_Neg(work);
            }

            CInt64_SetULong(&longgcd, gcd(ls1->u.known.stride, ls2->u.known.stride));
            if (CInt64_Equal(CInt64_ModU(work, longgcd), cint64_zero)) {
                return TRUE;
            }

            if (size1 == (UInt32)-1) {
                return TRUE;
            }

            for (i = 1; i < size1; i++) {
                CInt64_SetLong(&work, i);
                work = CInt64_Add(work, ls1->u.known.field);
                work = CInt64_Sub(work, ls2->u.known.field);
                if (CInt64_IsNegative(&work)) {
                    work = CInt64_Neg(work);
                }
                if (CInt64_Equal(CInt64_ModU(work, longgcd), cint64_zero)) {
                    return TRUE;
                }
            }

            if (size2 == (UInt32)-1) {
                return TRUE;
            }

            for (i = 1; i < size2; i++) {
                CInt64_SetLong(&work, i);
                work = CInt64_Add(work, ls2->u.known.field);
                work = CInt64_Sub(work, ls1->u.known.field);
                if (CInt64_IsNegative(&work)) {
                    work = CInt64_Neg(work);
                }
                if (CInt64_Equal(CInt64_ModU(work, longgcd), cint64_zero)) {
                    return TRUE;
                }
            }

            return FALSE;
        }
    }

    return FALSE;
}

#pragma sym reset
HEADER_INLINE Boolean LocationSets_Equal(LocationSet* ls1, LocationSet* ls2) {
    IRO_ASSERT(2080, ls1 != NULL);
    IRO_ASSERT(2081, ls2 != NULL);

    return (ls1 == ls2) ||
        ((LocationSet_IsUnknown(ls1) && LocationSet_IsUnknown(ls2)) &&
         (MemoryBlocks_Equal(ls1->u.unknown.restriction, ls2->u.unknown.restriction)) &&
         ((ls1->u.unknown.bitfieldOf == ls2->u.unknown.bitfieldOf) ||
          (ls1->u.unknown.bitfieldOf != NULL &&
           ls2->u.unknown.bitfieldOf != NULL &&
           LocationSets_Equal(ls1->u.unknown.bitfieldOf, ls2->u.unknown.bitfieldOf))) &&
         ((ls1->rtype == ls2->rtype) ||
          (ls1->rtype != NULL && ls2->rtype != NULL && ls1->rtype->size == ls2->rtype->size))) ||
        ((!LocationSet_IsUnknown(ls1) && !LocationSet_IsUnknown(ls2)) &&
         (ls1->u.known.stride == ls2->u.known.stride) &&
         ((ls1->rtype == ls2->rtype) ||
          (ls1->rtype != NULL && ls2->rtype != NULL && ls1->rtype->size == ls2->rtype->size)) &&
         CInt64_Equal(ls1->u.known.field, ls2->u.known.field) &&
         MemoryBlocks_Equal(ls1->block, ls2->block));
}

HEADER_INLINE Boolean LocationSets_LookupCompatible(LocationSet* ls1, LocationSet* ls2) {
    IRO_ASSERT(2119, ls1 != NULL);
    IRO_ASSERT(2120, ls2 != NULL);

    if ((ls1 == ls2) ||
        (LocationSet_IsUnknown(ls1) &&
         LocationSet_IsUnknown(ls2) &&
         MemoryBlocks_Equal(ls1->u.unknown.restriction, ls2->u.unknown.restriction) &&
         (ls1->rtype == ls2->rtype ||
          (ls1->rtype && ls2->rtype && ls1->rtype->size == ls2->rtype->size)))) {
        return TRUE;
    }

    if ((!LocationSet_IsUnknown(ls1) && !LocationSet_IsUnknown(ls2)) &&
        (ls1->rtype == ls2->rtype ||
         (ls1->rtype && ls2->rtype && ls1->rtype->size == ls2->rtype->size)) &&
        MemoryBlocks_Equal(ls1->block, ls2->block)) {
        CInt64 work;
        CInt64 longgcd;

        if (ls1->u.known.stride == ls2->u.known.stride) {
            return CInt64_Equal(ls1->u.known.field, ls2->u.known.field);
        }

        work = CInt64_Sub(ls1->u.known.field, ls2->u.known.field);
        if (CInt64_IsNegative(&work)) {
            work = CInt64_Neg(work);
        }

        CInt64_SetULong(&longgcd, gcd(ls1->u.known.stride, ls2->u.known.stride));
        return CInt64_Equal(CInt64_ModU(work, longgcd), cint64_zero);
    }

    return FALSE;
}

HEADER_INLINE Boolean
LocationSet_Contains(LocationSet* ls1, Type* rtype1, LocationSet* ls2, Type* rtype2) {
    Boolean unknown1;
    Boolean unknown2;
    PAMemoryBlock* restriction1;
    PAMemoryBlock* restriction2;
    CInt64 longsize1;
    CInt64 longsize2;

    IRO_ASSERT(2168, ls1 != NULL);
    IRO_ASSERT(2169, ls2 != NULL);
    IRO_ASSERT(2170, rtype1 != NULL);
    IRO_ASSERT(2171, rtype2 != NULL);

    if (ls1 == ls2) {
        return TRUE;
    }

    unknown1 = LocationSet_IsUnknown(ls1);
    if (unknown1) {
        restriction1 = ls1->u.unknown.restriction;
    } else {
        restriction1 = NULL;
    }

    unknown2 = LocationSet_IsUnknown(ls2);
    if (unknown2) {
        restriction2 = ls2->u.unknown.restriction;
    } else {
        restriction2 = NULL;
    }

    if (unknown1) {
        return restriction1 == NULL || (unknown2 && MemoryBlocks_Equal(restriction2, restriction1));
    }

    CInt64_SetULong(&longsize1, rtype1->size);
    CInt64_SetULong(&longsize2, rtype2->size);

    return !LocationSet_IsUnknown(ls2) &&
        (ls1->u.known.stride == 0) &&
        (ls2->u.known.stride == 0) &&
        rtype1->size >= rtype2->size &&
        CInt64_LessEqual(ls1->u.known.field, ls2->u.known.field) &&
        CInt64_GreaterEqual(
            CInt64_Add(ls1->u.known.field, longsize1), CInt64_Add(ls2->u.known.field, longsize2)
        ) &&
        MemoryBlocks_Equal(ls1->block, ls2->block);
}

HEADER_INLINE Boolean LocationSet_IsUnknown(LocationSet* ls) {
    IRO_ASSERT(2233, ls != NULL);

    return (ls == stUnknownLs) || (ls->block == stUnknownMb);
}

HEADER_INLINE Boolean LocationSet_IsLocalVarBase(LocationSet* ls) {
    IRO_ASSERT(2251, ls != NULL);

    return !LocationSet_IsUnknown(ls) &&
        (ls->u.known.stride == 0) &&
        CInt64_IsZero(&ls->u.known.field) &&
        PAMemoryBlock_kind(ls->block) == PAMEMORYBLOCKKIND_LOCALVAR &&
        PAMemoryBlock_thing(ls->block) == NULL;
}

HEADER_INLINE void LocationSet_SetRtype(LocationSet* ls, Type* rtype) {
    IRO_ASSERT(2263, ls != NULL);
    IRO_ASSERT(2264, ls != stUnknownLs);
    IRO_ASSERT(2265, rtype != NULL);

    ls->rtype = rtype;
}

HEADER_INLINE void LocationSet_SetField(LocationSet* ls, CInt64 field) {
    IRO_ASSERT(2272, ls != NULL);
    IRO_ASSERT(2273, !LocationSet_IsUnknown(ls));

    ls->u.known.field = field;
}

HEADER_INLINE void LocationSet_SetStride(LocationSet* ls, SInt32 stride) {
    IRO_ASSERT(2280, ls != NULL);
    IRO_ASSERT(2281, !LocationSet_IsUnknown(ls));

    ls->u.known.stride = stride;
}

HEADER_INLINE PAMemoryBlock* LocationSet_block(LocationSet* ls) {
    IRO_ASSERT(2298, ls != NULL);

    return ls->block;
}

HEADER_INLINE Type* LocationSet_rtype(LocationSet* ls) {
    IRO_ASSERT(2306, ls != NULL);
    IRO_ASSERT(2307, ls != stUnknownLs);

    return ls->rtype;
}

HEADER_INLINE CInt64 LocationSet_field(LocationSet* ls) {
    IRO_ASSERT(2314, ls != NULL);
    IRO_ASSERT(2315, !LocationSet_IsUnknown(ls));

    return ls->u.known.field;
}

HEADER_INLINE UInt32 LocationSet_stride(LocationSet* ls) {
    IRO_ASSERT(2322, ls != NULL);
    IRO_ASSERT(2323, !LocationSet_IsUnknown(ls));

    return ls->u.known.stride;
}

HEADER_INLINE PAMemoryBlock* LocationSet_restriction(LocationSet* ls) {
    IRO_ASSERT(2330, ls != NULL);
    IRO_ASSERT(2331, LocationSet_IsUnknown(ls));

    return ls->u.unknown.restriction;
}

HEADER_INLINE LocationSet* LocationSet_bitfieldOf(LocationSet* ls) {
    IRO_ASSERT(2338, ls != NULL);
    IRO_ASSERT(2339, LocationSet_IsUnknown(ls));

    return ls->u.unknown.bitfieldOf;
}

HEADER_INLINE LocationSetSet* LocationSetSet_New(void) {
    LocationSetSet* lss = IRO_malloc(sizeof(LocationSetSet));

    IRO_ASSERT(2356, lss != NULL);
    lss->loc = NULL;
    lss->otherLocs = NULL;
    lss->count = 0;
    return lss;
}

HEADER_INLINE void LocationSetSet_Delete(LocationSetSet* lss) {
    IRO_ASSERT(2369, lss != NULL);
    IRO_ASSERT(2370, lss->loc == NULL);
    IRO_ASSERT(2371, lss->otherLocs == NULL);
    IRO_ASSERT(2372, lss->count == 0);
    IRO_DEBUG_CLEAR(lss, sizeof(LocationSetSet));
    IRO_free(lss);
}

HEADER_INLINE void LocationSetSet_Init(LocationSetSet* lss) {
    IRO_ASSERT(2382, lss != NULL);

    lss->loc = NULL;
    lss->otherLocs = NULL;
    lss->count = 0;
}

HEADER_INLINE void LocationSetSet_Copy(LocationSetSet* dest, LocationSetSet* src) {
    IRO_ASSERT(2391, dest != NULL);
    IRO_ASSERT(2392, src != NULL);

    dest->loc = NULL;
    dest->otherLocs = NULL;
    dest->count = 0;
    LocationSetSet_AddSet(dest, src);
}

HEADER_INLINE void LocationSetSet_Term(LocationSetSet* lss) {
    IRO_ASSERT(2402, lss != NULL);

    LocationSetSet_RemoveAll(lss);
    lss->loc = NULL;
    lss->otherLocs = NULL;
    lss->count = 0;
}

HEADER_INLINE void LocationSetSet_ForEach(
    LocationSetSet* lss, void (*action)(LocationSet*, void*), void* refcon
) {
    IRO_ASSERT(2446, lss != NULL);
    IRO_ASSERT(2447, action != NULL);
    IRO_ASSERT(2448, refcon == NULL || refcon != NULL);

    while (lss && lss->loc) {
        action(lss->loc, refcon);
        lss = lss->otherLocs;
    }
}

HEADER_INLINE LocationSet* LocationSetSet_Find(LocationSetSet* lss, LocationSet* ls) {
    IRO_ASSERT(2458, lss != NULL);
    IRO_ASSERT(2459, ls != NULL);

    while (lss && lss->loc) {
        if (LocationSets_Equal(lss->loc, ls)) {
            return lss->loc;
        }
        lss = lss->otherLocs;
    }

    return NULL;
}

HEADER_INLINE LocationSet* LocationSetSet_FindUnknown(LocationSetSet* lss) {
    IRO_ASSERT(2470, lss != NULL);

    if (!lss->loc) {
        return stUnknownLs;
    }

    while (lss && lss->loc) {
        if (LocationSet_IsUnknown(lss->loc)) {
            return lss->loc;
        }
        lss = lss->otherLocs;
    }

    return NULL;
}

HEADER_INLINE LocationSet* LocationSetSet_FindFirst(LocationSetSet* lss) {
    IRO_ASSERT(2498, lss != NULL);

    return lss->loc;
}

HEADER_INLINE int LocationSetSet_Count(LocationSetSet* lss) {
    IRO_ASSERT(2505, lss != NULL);

    return lss->count;
}

HEADER_INLINE void LocationSetSet_RemoveAllWithMemoryBlock(
    LocationSetSet* lss, PAMemoryBlock* block
) {
    LocationSetSet* first;
    LocationSetSet* prev;
    LocationSetSet* next;
    LocationSetSet* second;

    IRO_ASSERT(2514, lss != NULL);
    IRO_ASSERT(2515, block != NULL);

    first = lss;
    prev = NULL;
    while (lss && lss->loc) {
        next = lss->otherLocs;
        if (MemoryBlocks_Equal(block, lss->loc->block)) {
            if (lss->loc != stUnknownLs) {
                LocationSet_Term(lss->loc);
                LocationSet_Delete(lss->loc);
            }
            if (!prev) {
                if (lss->otherLocs == NULL) {
                    lss->loc = NULL;
                    prev = lss;
                } else {
                    second = lss->otherLocs;
                    lss->loc = lss->otherLocs->loc;
                    lss->otherLocs = lss->otherLocs->otherLocs;
                    second->loc = NULL;
                    second->otherLocs = NULL;
                    LocationSetSet_Term(second);
                    LocationSetSet_Delete(second);
                    prev = NULL;
                    next = lss;
                }
            } else {
                prev->otherLocs = lss->otherLocs;
                lss->loc = NULL;
                lss->otherLocs = NULL;
                LocationSetSet_Term(lss);
                LocationSetSet_Delete(lss);
                prev = lss;
            }
            first->count--;
        }
        lss = next;
    }
}

HEADER_INLINE void LocationSetSet_SimpleAdd(LocationSetSet* lss, LocationSet* ls) {
    LocationSet* ls2;

    IRO_ASSERT(2572, lss != NULL);
    IRO_ASSERT(2573, ls != NULL);

    if (!LocationSet_IsUnknown(ls) && lss->count < 4) {
        if (ls == stUnknownLs) {
            ls2 = stUnknownLs;
        } else {
            ls2 = LocationSet_New();
            LocationSet_Copy(ls2, ls);
        }

        if (lss->loc) {
            LocationSetSet* lss2 = LocationSetSet_New();
            LocationSetSet_Init(lss2);
            lss2->loc = lss->loc;
            lss2->otherLocs = lss->otherLocs;
            lss->otherLocs = lss2;
        }

        lss->loc = ls2;
        lss->count++;
    } else {
        LocationSetSet_RemoveAll(lss);
        ls2 = LocationSet_New();
        if (LocationSet_IsUnknown(ls)) {
            LocationSet_Copy(ls2, ls);
        } else {
            LocationSet_Copy(ls2, stUnknownLs);
            if (ls->rtype) {
                LocationSet_SetRtype(ls2, ls->rtype);
            }
        }

        lss->loc = ls2;
        lss->count = 1;
    }
}

HEADER_INLINE void LocationSetSet_Add(LocationSetSet* lss, LocationSet* ls) {
    IRO_ASSERT(2622, lss != NULL);
    IRO_ASSERT(2623, ls != NULL);

    if (!lss->loc || (!LocationSet_IsUnknown(lss->loc) && !LocationSetSet_Find(lss, ls))) {
        if (!LocationSet_IsUnknown(ls) && ls->u.known.stride) {
            LocationSetSet_RemoveAllWithMemoryBlock(lss, ls->block);
        }
        LocationSetSet_SimpleAdd(lss, ls);
    }
}

HEADER_INLINE void LocationSetSet_AddUnknown(
    LocationSetSet* lss, Type* rtype, PAMemoryBlock* restriction, LocationSet* bitfieldOf
) {
    LocationSet* ls;

    IRO_ASSERT(2643, lss != NULL);
    IRO_ASSERT(2644, rtype == NULL || rtype != NULL);
    IRO_ASSERT(2645, restriction == NULL || restriction != NULL);
    IRO_ASSERT(2646, bitfieldOf == NULL || bitfieldOf != NULL);

    ls = LocationSet_New();
    LocationSet_InitUnknown(ls, rtype, restriction, bitfieldOf);
    LocationSetSet_Add(lss, ls);
    LocationSet_Term(ls);
    LocationSet_Delete(ls);
}

HEADER_INLINE void LocationSetSet_Remove(LocationSetSet* lss, LocationSet* ls) {
    LocationSetSet* first;
    LocationSetSet* prev;
    LocationSetSet* second;

    IRO_ASSERT(2659, lss != NULL);
    IRO_ASSERT(2660, ls != NULL);

    first = lss;
    prev = NULL;
    while (lss && lss->loc) {
        if (LocationSets_Equal(lss->loc, ls)) {
            if (lss->loc != stUnknownLs) {
                LocationSet_Term(lss->loc);
                LocationSet_Delete(lss->loc);
            }
            if (!prev) {
                if (lss->otherLocs == NULL) {
                    lss->loc = NULL;
                } else {
                    second = lss->otherLocs;
                    lss->loc = lss->otherLocs->loc;
                    lss->otherLocs = lss->otherLocs->otherLocs;
                    second->loc = NULL;
                    second->otherLocs = NULL;
                    LocationSetSet_Term(second);
                    LocationSetSet_Delete(second);
                }
            } else {
                prev->otherLocs = lss->otherLocs;
                lss->loc = NULL;
                lss->otherLocs = NULL;
                LocationSetSet_Term(lss);
                LocationSetSet_Delete(lss);
            }
            first->count--;
            return;
        }

        prev = lss;
        lss = lss->otherLocs;
    }
}

HEADER_INLINE void LocationSetSet_RemoveAll(LocationSetSet* lss) {
    IRO_ASSERT(2707, lss != NULL);

    while (lss && lss->loc) {
        LocationSetSet_Remove(lss, lss->loc);
    }
}

HEADER_INLINE void LocationSetSet_AddSetAction(LocationSet* ls, void* refcon) {
    IRO_ASSERT(2717, ls != NULL);
    IRO_ASSERT(2718, refcon != NULL);

    LocationSetSet_Add((LocationSetSet*)refcon, ls);
}

HEADER_INLINE void LocationSetSet_SimpleAddSetAction(LocationSet* ls, void* refcon) {
    IRO_ASSERT(2725, ls != NULL);
    IRO_ASSERT(2726, refcon != NULL);

    LocationSetSet_SimpleAdd((LocationSetSet*)refcon, ls);
}

HEADER_INLINE void LocationSetSet_AddSet(LocationSetSet* dest, LocationSetSet* src) {
    IRO_ASSERT(2733, dest != NULL);
    IRO_ASSERT(2734, src != NULL);

    if (dest->count) {
        LocationSetSet_ForEach(src, LocationSetSet_AddSetAction, dest);
    } else {
        LocationSetSet_ForEach(src, LocationSetSet_SimpleAddSetAction, dest);
    }
}

HEADER_INLINE void LocationSetSet_RemoveSetAction(LocationSet* ls, void* refcon) {
    IRO_ASSERT(2744, ls != NULL);
    IRO_ASSERT(2745, refcon != NULL);

    LocationSetSet_Remove((LocationSetSet*)refcon, ls);
}

HEADER_INLINE void LocationSetSet_RemoveSet(LocationSetSet* dest, LocationSetSet* src) {
    IRO_ASSERT(2752, dest != NULL);
    IRO_ASSERT(2753, src != NULL);

    LocationSetSet_ForEach(src, LocationSetSet_RemoveSetAction, dest);
}

HEADER_INLINE Boolean LocationSetSets_Equal(LocationSetSet* lss1, LocationSetSet* lss2) {
    IRO_ASSERT(2826, lss1 != NULL);
    IRO_ASSERT(2827, lss2 != NULL);

    if (lss1 == lss2) {
        return TRUE;
    }
    if (LocationSetSet_Count(lss1) != LocationSetSet_Count(lss2)) {
        return FALSE;
    }

    while (lss1 && lss1->loc) {
        if (!LocationSetSet_Find(lss2, lss1->loc)) {
            return FALSE;
        }
        lss1 = lss1->otherLocs;
    }

    return TRUE;
}

HEADER_INLINE ParamMapping* ParamMapping_New(void) {
    ParamMapping* pm = IRO_malloc(sizeof(ParamMapping));

    IRO_ASSERT(2885, pm != NULL);
    pm->actual = NULL;
    pm->formal = NULL;
    pm->extended = NULL;
    return pm;
}

HEADER_INLINE void ParamMapping_Delete(ParamMapping* pm) {
    IRO_ASSERT(2898, pm != NULL);
    IRO_ASSERT(2899, pm->actual == NULL);
    IRO_ASSERT(2900, pm->formal == NULL);
    IRO_ASSERT(2901, pm->extended == NULL);
    IRO_DEBUG_CLEAR(pm, sizeof(ParamMapping));
    IRO_free(pm);
}

HEADER_INLINE void ParamMapping_Init(
    ParamMapping* pm, IROLinear* actual, Object* formal, ExtendedParam* extended
) {
    IRO_ASSERT(2911, pm != NULL);

    pm->actual = actual;
    pm->formal = formal;
    pm->extended = extended;
}

HEADER_INLINE void ParamMapping_Copy(ParamMapping* dest, ParamMapping* src) {
    IRO_ASSERT(2920, src != NULL);
    IRO_ASSERT(2921, dest != NULL);

    dest->actual = src->actual;
    dest->formal = src->formal;
    dest->extended = src->extended;
}

HEADER_INLINE void ParamMapping_Term(ParamMapping* pm) {
    IRO_ASSERT(2933, pm != NULL);

    pm->actual = NULL;
    pm->formal = NULL;
    pm->extended = NULL;
}

HEADER_INLINE void ParamMapping_SetExtended(ParamMapping* pm, ExtendedParam* ep) {
    IRO_ASSERT(2992, pm != NULL);

    pm->extended = ep;
}

HEADER_INLINE IROLinear* ParamMapping_actual(ParamMapping* pm) {
    IRO_ASSERT(2999, pm != NULL);

    return pm->actual;
}

HEADER_INLINE ExtendedParam* ParamMapping_extended(ParamMapping* pm) {
    IRO_ASSERT(3011, pm != NULL);

    return pm->extended;
}

HEADER_INLINE ParamMappingFunction* ParamMappingFunction_New(void) {
    ParamMappingFunction* pmf = IRO_malloc(sizeof(ParamMappingFunction));

    IRO_ASSERT(3026, pmf != NULL);
    pmf->mapping = NULL;
    pmf->otherMappings = NULL;
    return pmf;
}

HEADER_INLINE void ParamMappingFunction_Delete(ParamMappingFunction* pmf) {
    IRO_ASSERT(3038, pmf != NULL);
    IRO_ASSERT(3039, pmf->mapping == NULL);
    IRO_ASSERT(3040, pmf->otherMappings == NULL);
    IRO_DEBUG_CLEAR(pmf, sizeof(ParamMappingFunction));
    IRO_free(pmf);
}

HEADER_INLINE void ParamMappingFunction_Init(ParamMappingFunction* pmf) {
    IRO_ASSERT(3050, pmf != NULL);

    pmf->mapping = NULL;
    pmf->otherMappings = NULL;
}

HEADER_INLINE void ParamMappingFunction_Copy(
    ParamMappingFunction* dest, ParamMappingFunction* src
) {
    IRO_ASSERT(3058, src != NULL);
    IRO_ASSERT(3059, dest != NULL);

    dest->mapping = NULL;
    dest->otherMappings = NULL;
    ParamMappingFunction_AddAll(dest, src);
}

HEADER_INLINE void ParamMappingFunction_Term(ParamMappingFunction* pmf) {
    IRO_ASSERT(3068, pmf != NULL);

    ParamMappingFunction_RemoveAll(pmf);
    pmf->mapping = NULL;
    pmf->otherMappings = NULL;
}

HEADER_INLINE void ParamMappingFunction_ForEach(
    ParamMappingFunction* pmf, void (*action)(ParamMapping*, void*), void* refcon
) {
    IRO_ASSERT(3111, pmf != NULL);
    IRO_ASSERT(3112, action != NULL);
    IRO_ASSERT(3113, refcon == NULL || refcon != NULL);

    while (pmf && pmf->mapping) {
        action(pmf->mapping, refcon);
        pmf = pmf->otherMappings;
    }
}

HEADER_INLINE ParamMapping* ParamMappingFunction_FindMappingByFormal(
    ParamMappingFunction* pmf, Object* formal
) {
    IRO_ASSERT(3123, pmf != NULL);
    IRO_ASSERT(3124, formal != NULL);

    while (pmf && pmf->mapping) {
        if (pmf->mapping->formal == formal) {
            return pmf->mapping;
        }
        pmf = pmf->otherMappings;
    }

    return NULL;
}

HEADER_INLINE void ParamMappingFunction_Add(ParamMappingFunction* pmf, ParamMapping* mapping) {
    ParamMapping* existing;

    IRO_ASSERT(3138, pmf != NULL);
    IRO_ASSERT(3139, mapping != NULL);

    existing = ParamMappingFunction_FindMappingByFormal(pmf, mapping->formal);
    if (!existing) {
        existing = ParamMapping_New();
        ParamMapping_Copy(existing, mapping);
        if (pmf->mapping) {
            ParamMappingFunction* newPMF = ParamMappingFunction_New();
            ParamMappingFunction_Init(newPMF);
            newPMF->mapping = pmf->mapping;
            newPMF->otherMappings = pmf->otherMappings;
            pmf->otherMappings = newPMF;
        }
        pmf->mapping = existing;
    } else {
        existing->actual = mapping->actual;
        existing->extended = mapping->extended;
    }
}

HEADER_INLINE void ParamMappingFunction_Remove(ParamMappingFunction* pmf, ParamMapping* mapping) {
    ParamMappingFunction* prev;
    ParamMappingFunction* second;

    IRO_ASSERT(3170, pmf != NULL);
    IRO_ASSERT(3171, mapping != NULL);

    prev = NULL;
    while (pmf && pmf->mapping) {
        if (pmf->mapping->formal == mapping->formal) {
            ParamMapping_Term(pmf->mapping);
            ParamMapping_Delete(pmf->mapping);
            if (!prev) {
                if (pmf->otherMappings == NULL) {
                    pmf->mapping = NULL;
                } else {
                    second = pmf->otherMappings;
                    pmf->mapping = pmf->otherMappings->mapping;
                    pmf->otherMappings = pmf->otherMappings->otherMappings;
                    second->mapping = NULL;
                    second->otherMappings = NULL;
                    ParamMappingFunction_Term(second);
                    ParamMappingFunction_Delete(second);
                }
            } else {
                prev->otherMappings = pmf->otherMappings;
                pmf->mapping = NULL;
                pmf->otherMappings = NULL;
                ParamMappingFunction_Term(pmf);
                ParamMappingFunction_Delete(pmf);
            }
            return;
        }

        prev = pmf;
        pmf = pmf->otherMappings;
    }
}

HEADER_INLINE void ParamMappingFunction_RemoveAll(ParamMappingFunction* pmf) {
    IRO_ASSERT(3213, pmf != NULL);

    while (pmf && pmf->mapping) {
        ParamMappingFunction_Remove(pmf, pmf->mapping);
    }
}

HEADER_INLINE void ParamMappingFunction_AddFunctionAction(ParamMapping* mapping, void* refcon) {
    IRO_ASSERT(3223, mapping != NULL);
    IRO_ASSERT(3224, refcon != NULL);

    ParamMappingFunction_Add((ParamMappingFunction*)refcon, mapping);
}

HEADER_INLINE void ParamMappingFunction_AddAll(
    ParamMappingFunction* dest, ParamMappingFunction* src
) {
    IRO_ASSERT(3231, dest != NULL);
    IRO_ASSERT(3232, src != NULL);

    ParamMappingFunction_ForEach(src, ParamMappingFunction_AddFunctionAction, dest);
}

HEADER_INLINE PointsToEntry* PointsToEntry_New(void) {
    PointsToEntry* pte = IRO_malloc(sizeof(PointsToEntry));

    IRO_ASSERT(3288, pte != NULL);
    pte->loc = NULL;
    pte->locs = NULL;
    return pte;
}

HEADER_INLINE void PointsToEntry_Delete(PointsToEntry* pte) {
    IRO_ASSERT(3300, pte != NULL);
    IRO_ASSERT(3301, pte->loc == NULL);
    IRO_ASSERT(3302, pte->locs == NULL);
    IRO_DEBUG_CLEAR(pte, sizeof(PointsToEntry));
    IRO_free(pte);
}

HEADER_INLINE void PointsToEntry_Init(PointsToEntry* pte, LocationSet* loc, LocationSetSet* locs) {
    IRO_ASSERT(3312, pte != NULL);
    IRO_ASSERT(3313, loc != NULL);
    IRO_ASSERT(3314, !LocationSet_IsUnknown(loc));
    IRO_ASSERT(3315, locs != NULL);

    pte->loc = LocationSet_New();
    LocationSet_Copy(pte->loc, loc);

    pte->locs = LocationSetSet_New();
    LocationSetSet_Copy(pte->locs, locs);
}

HEADER_INLINE void PointsToEntry_Copy(PointsToEntry* dest, PointsToEntry* src) {
    IRO_ASSERT(3325, src != NULL);
    IRO_ASSERT(3326, dest != NULL);

    PointsToEntry_Init(dest, src->loc, src->locs);
}

HEADER_INLINE void PointsToEntry_Term(PointsToEntry* pte) {
    IRO_ASSERT(3333, pte != NULL);

    LocationSet_Term(pte->loc);
    LocationSet_Delete(pte->loc);
    LocationSetSet_Term(pte->locs);
    LocationSetSet_Delete(pte->locs);
    pte->loc = NULL;
    pte->locs = NULL;
}

HEADER_INLINE Boolean PointsToEntries_Equal(PointsToEntry* pte1, PointsToEntry* pte2) {
    IRO_ASSERT(3381, pte1 != NULL);
    IRO_ASSERT(3382, pte2 != NULL);

    if (pte1 == pte2) {
        return TRUE;
    }

    return LocationSets_Equal(pte1->loc, pte2->loc) &&
        LocationSetSets_Equal(pte1->locs, pte2->locs);
}

HEADER_INLINE LocationSet* PointsToEntry_loc(PointsToEntry* pte) {
    IRO_ASSERT(3407, pte != NULL);

    return pte->loc;
}

HEADER_INLINE LocationSetSet* PointsToEntry_locs(PointsToEntry* pte) {
    IRO_ASSERT(3414, pte != NULL);

    return pte->locs;
}

HEADER_INLINE PointsToFunction* PointsToFunction_New(void) {
    PointsToFunction* pointsToFunc = IRO_malloc(sizeof(PointsToFunction));

    IRO_ASSERT(3430, pointsToFunc != NULL);
    pointsToFunc->pte = NULL;
    pointsToFunc->otherPtes = NULL;
    return pointsToFunc;
}

HEADER_INLINE void PointsToFunction_Delete(PointsToFunction* pointsToFunc) {
    IRO_ASSERT(3442, pointsToFunc != NULL);
    IRO_ASSERT(3443, pointsToFunc->pte == NULL);
    IRO_ASSERT(3444, pointsToFunc->otherPtes == NULL);
    IRO_DEBUG_CLEAR(pointsToFunc, sizeof(PointsToFunction));
    IRO_free(pointsToFunc);
}

HEADER_INLINE void PointsToFunction_Init(PointsToFunction* pointsToFunc) {
    IRO_ASSERT(3454, pointsToFunc != NULL);

    pointsToFunc->pte = NULL;
    pointsToFunc->otherPtes = NULL;
}

HEADER_INLINE void PointsToFunction_Copy(PointsToFunction* dest, PointsToFunction* src) {
    IRO_ASSERT(3462, src != NULL);
    IRO_ASSERT(3463, dest != NULL);

    dest->pte = NULL;
    dest->otherPtes = NULL;
    PointsToFunction_AddAll(dest, src);
}

HEADER_INLINE void PointsToFunction_Term(PointsToFunction* pointsToFunc) {
    IRO_ASSERT(3472, pointsToFunc != NULL);

    PointsToFunction_RemoveAll(pointsToFunc);
    pointsToFunc->pte = NULL;
    pointsToFunc->otherPtes = NULL;
}

HEADER_INLINE void PointsToFunction_ForEach(
    PointsToFunction* pointsToFunc, void (*action)(PointsToEntry*, void*), void* refcon
) {
    IRO_ASSERT(3515, pointsToFunc != NULL);
    IRO_ASSERT(3516, action != NULL);
    IRO_ASSERT(3517, refcon == NULL || refcon != NULL);

    while (pointsToFunc && pointsToFunc->pte) {
        action(pointsToFunc->pte, refcon);
        pointsToFunc = pointsToFunc->otherPtes;
    }
}

HEADER_INLINE PointsToEntry* PointsToFunction_FindByLocationSet(
    PointsToFunction* pointsToFunc, LocationSet* ls
) {
    IRO_ASSERT(3527, pointsToFunc != NULL);
    IRO_ASSERT(3528, ls != NULL);

    while (pointsToFunc && pointsToFunc->pte) {
        if (LocationSets_Equal(pointsToFunc->pte->loc, ls)) {
            return pointsToFunc->pte;
        }
        pointsToFunc = pointsToFunc->otherPtes;
    }

    return NULL;
}

HEADER_INLINE PointsToEntry* PointsToFunction_FindFirst(PointsToFunction* pointsToFunc) {
    IRO_ASSERT(3539, pointsToFunc != NULL);

    return pointsToFunc->pte;
}

HEADER_INLINE PointsToEntry* PointsToFunction_FindByLookupCompatibleLocationSet(
    PointsToFunction* pointsToFunc, LocationSet* ls
) {
    IRO_ASSERT(3546, pointsToFunc != NULL);
    IRO_ASSERT(3547, ls != NULL);

    while (pointsToFunc && pointsToFunc->pte) {
        if (ls->u.known.stride) {
            if (LocationSets_Equal(pointsToFunc->pte->loc, ls)) {
                return pointsToFunc->pte;
            }
        } else if (LocationSets_LookupCompatible(pointsToFunc->pte->loc, ls)) {
            return pointsToFunc->pte;
        }
        pointsToFunc = pointsToFunc->otherPtes;
    }

    return NULL;
}

HEADER_INLINE PointsToEntry* PointsToFunction_FindContainingLocationSet(
    PointsToFunction* pointsToFunc, LocationSet* ls, Type* rtype
) {
    IRO_ASSERT(3565, pointsToFunc != NULL);
    IRO_ASSERT(3566, ls != NULL);
    IRO_ASSERT(3567, rtype != NULL);

    while (pointsToFunc && pointsToFunc->pte) {
        if (pointsToFunc->pte->locs->loc && !LocationSet_IsUnknown(pointsToFunc->pte->locs->loc)) {
            if (!pointsToFunc->pte->locs->otherLocs &&
                LocationSet_Contains(
                    pointsToFunc->pte->loc, pointsToFunc->pte->locs->loc->rtype, ls, rtype
                )) {
                return pointsToFunc->pte;
            }
        }
        pointsToFunc = pointsToFunc->otherPtes;
    }

    return NULL;
}

HEADER_INLINE void PointsToFunction_RemoveOverlappingLocations(
    PointsToFunction* pointsToFunc, PointsToEntry* pte
) {
    Type* rtype2;
    LocationSet* ls;
    PointsToFunction* prev;
    PointsToFunction* next;
    Type* rtype1;
    PointsToFunction* second;

    IRO_ASSERT(3601, pointsToFunc != NULL);
    IRO_ASSERT(3602, pte != NULL);
    IRO_ASSERT(3603, pte->locs != NULL);

    if (pte->locs->loc && pte->locs->loc != stUnknownLs) {
        rtype1 = pte->locs->loc->rtype;
    } else {
        rtype1 = NULL;
    }
    ls = pte->loc;
    IRO_ASSERT(3614, ls != NULL);

    prev = NULL;
    while (pointsToFunc && pointsToFunc->pte) {
        next = pointsToFunc->otherPtes;
        if (pointsToFunc->pte->locs->loc && pointsToFunc->pte->locs->loc != stUnknownLs) {
            rtype2 = pointsToFunc->pte->locs->loc->rtype;
        } else {
            rtype2 = NULL;
        }

        if (LocationSets_Overlap(ls, rtype1, pointsToFunc->pte->loc, rtype2)) {
            PointsToEntry_Term(pointsToFunc->pte);
            PointsToEntry_Delete(pointsToFunc->pte);
            if (!prev) {
                if (pointsToFunc->otherPtes == NULL) {
                    pointsToFunc->pte = NULL;
                    prev = pointsToFunc;
                } else {
                    second = pointsToFunc->otherPtes;
                    pointsToFunc->pte = pointsToFunc->otherPtes->pte;
                    pointsToFunc->otherPtes = pointsToFunc->otherPtes->otherPtes;
                    second->pte = NULL;
                    second->otherPtes = NULL;
                    PointsToFunction_Term(second);
                    PointsToFunction_Delete(second);
                    prev = NULL;
                    next = pointsToFunc;
                }
            } else {
                prev->otherPtes = pointsToFunc->otherPtes;
                pointsToFunc->pte = NULL;
                pointsToFunc->otherPtes = NULL;
                PointsToFunction_Term(pointsToFunc);
                PointsToFunction_Delete(pointsToFunc);
                prev = pointsToFunc;
            }
        }

        pointsToFunc = next;
    }
}

HEADER_INLINE Boolean
ShouldAddNewPointsToEntryToFunction(PointsToFunction* pointsToFunc, PointsToEntry* pte) {
    Boolean anyOverlap;
    Boolean isKnown;
    SInt32 stride;
    PointsToFunction* next;
    LocationSet* loc;
    LocationSet* loc2;
    Type* rtype1;
    Type* rtype2;
    LocationSetSet* locs2;
    LocationSet* unknown;
    Boolean otherNotPlainUnknown;
    Boolean overlaps;

    IRO_ASSERT(3675, pointsToFunc != NULL);
    IRO_ASSERT(3676, pte != NULL);
    IRO_ASSERT(3677, pte->locs != NULL);
    IRO_ASSERT(3678, PointsToFunction_FindByLookupCompatibleLocationSet(pointsToFunc, pte->loc) == NULL);
    IRO_ASSERT_MSG(
        3679,
        (unknown = LocationSetSet_FindFirst(locs2 = pte->locs)) != NULL,
        "(unknown = LocationSetSet_FindFirst(pte->locs)) != NULL"
    );
    IRO_ASSERT(3680, LocationSet_IsUnknown(unknown));
    IRO_ASSERT(3681, LocationSet_bitfieldOf(unknown) == NULL);
    IRO_ASSERT(3682, LocationSet_restriction(unknown) == NULL);

    if (pte->locs->loc && pte->locs->loc != stUnknownLs) {
        rtype1 = pte->locs->loc->rtype;
    } else {
        rtype1 = NULL;
    }

    loc = pte->loc;
    IRO_ASSERT(3693, loc != NULL);

    isKnown = !LocationSet_IsUnknown(loc);
    if (isKnown) {
        stride = LocationSet_stride(loc);
    }

    anyOverlap = FALSE;
    while (pointsToFunc && pointsToFunc->pte) {
        next = pointsToFunc->otherPtes;

        locs2 = pointsToFunc->pte->locs;

        if (locs2->loc && locs2->loc != stUnknownLs) {
            rtype2 = locs2->loc->rtype;
        } else {
            rtype2 = NULL;
        }

        loc2 = pointsToFunc->pte->loc;

        otherNotPlainUnknown = (unknown = LocationSetSet_FindFirst(locs2)) == NULL ||
            !LocationSet_IsUnknown(unknown) ||
            LocationSet_bitfieldOf(unknown) ||
            LocationSet_restriction(unknown);

        overlaps = LocationSets_Overlap(loc, rtype1, loc2, rtype2);

        if (!anyOverlap && overlaps) {
            anyOverlap = TRUE;
        }

        if (overlaps &&
            (otherNotPlainUnknown || (isKnown && stride && LocationSet_stride(loc2) == 0))) {
            return TRUE;
        }

        pointsToFunc = next;
    }

    return !anyOverlap;
}

HEADER_INLINE Boolean
PointsToFunction_SimpleAdd(PointsToFunction* pointsToFunc, PointsToEntry* pte) {
    PointsToEntry* newPTE;

    IRO_ASSERT(3741, pointsToFunc != NULL);
    IRO_ASSERT(3742, pte != NULL);

    newPTE = PointsToEntry_New();
    PointsToEntry_Copy(newPTE, pte);
    if (pointsToFunc->pte) {
        PointsToFunction* newPointsToFunc = PointsToFunction_New();
        PointsToFunction_Init(newPointsToFunc);
        newPointsToFunc->pte = pointsToFunc->pte;
        newPointsToFunc->otherPtes = pointsToFunc->otherPtes;
        pointsToFunc->otherPtes = newPointsToFunc;
    }
    pointsToFunc->pte = newPTE;
    return TRUE;
}

HEADER_INLINE Boolean PointsToFunction_Add(PointsToFunction* pointsToFunc, PointsToEntry* pte) {
    IRO_ASSERT(3766, pointsToFunc != NULL);
    IRO_ASSERT(3767, pte != NULL);

    if (!PointsToFunction_FindByLookupCompatibleLocationSet(pointsToFunc, pte->loc)) {
        LocationSet* ls;
        if (!(ls = LocationSetSet_FindFirst(pte->locs)) ||
            !LocationSet_IsUnknown(ls) ||
            LocationSet_bitfieldOf(ls) ||
            LocationSet_restriction(ls) ||
            ShouldAddNewPointsToEntryToFunction(pointsToFunc, pte)) {
            PointsToFunction_RemoveOverlappingLocations(pointsToFunc, pte);
            if (!LocationSet_IsUnknown(pte->loc) || pte->loc->rtype) {
                PointsToFunction_SimpleAdd(pointsToFunc, pte);
            }
            return TRUE;
        }
    }

    return FALSE;
}

HEADER_INLINE Boolean
PointsToFunction_AddWithoutChecking(PointsToFunction* pointsToFunc, PointsToEntry* pte) {
    LocationSet* ls;

    IRO_ASSERT(3793, pointsToFunc != NULL);
    IRO_ASSERT(3794, pte != NULL);

    if (!(ls = LocationSetSet_FindFirst(pte->locs)) ||
        !LocationSet_IsUnknown(ls) ||
        LocationSet_bitfieldOf(ls) ||
        LocationSet_restriction(ls) ||
        ShouldAddNewPointsToEntryToFunction(pointsToFunc, pte)) {
        PointsToFunction_RemoveOverlappingLocations(pointsToFunc, pte);
        if (!LocationSet_IsUnknown(pte->loc) || pte->loc->rtype) {
            PointsToFunction_SimpleAdd(pointsToFunc, pte);
        }
        return TRUE;
    }

    return FALSE;
}

HEADER_INLINE void PointsToFunction_RemoveByLocationSet(
    PointsToFunction* pointsToFunc, LocationSet* ls
) {
    PointsToFunction* prev;
    PointsToFunction* second;

    IRO_ASSERT(3818, pointsToFunc != NULL);
    IRO_ASSERT(3819, ls != NULL);
    IRO_ASSERT_T(3820, !LocationSet_IsUnknown(ls));

    prev = NULL;
    while (pointsToFunc && pointsToFunc->pte) {
        if (LocationSets_Equal(pointsToFunc->pte->loc, ls)) {
            PointsToEntry_Term(pointsToFunc->pte);
            PointsToEntry_Delete(pointsToFunc->pte);
            if (!prev) {
                if (pointsToFunc->otherPtes == NULL) {
                    pointsToFunc->pte = NULL;
                } else {
                    second = pointsToFunc->otherPtes;
                    pointsToFunc->pte = pointsToFunc->otherPtes->pte;
                    pointsToFunc->otherPtes = pointsToFunc->otherPtes->otherPtes;
                    second->pte = NULL;
                    second->otherPtes = NULL;
                    PointsToFunction_Term(second);
                    PointsToFunction_Delete(second);
                }
            } else {
                prev->otherPtes = pointsToFunc->otherPtes;
                pointsToFunc->pte = NULL;
                pointsToFunc->otherPtes = NULL;
                PointsToFunction_Term(pointsToFunc);
                PointsToFunction_Delete(pointsToFunc);
            }
            return;
        }

        prev = pointsToFunc;
        pointsToFunc = pointsToFunc->otherPtes;
    }
}

HEADER_INLINE void PointsToFunction_RemoveAll(PointsToFunction* pointsToFunc) {
    IRO_ASSERT(3862, pointsToFunc != NULL);

    while (pointsToFunc && pointsToFunc->pte) {
        PointsToFunction_RemoveByLocationSet(pointsToFunc, pointsToFunc->pte->loc);
    }
}

HEADER_INLINE void PointsToFunction_AddFunctionAction(PointsToEntry* pte, void* refcon) {
    IRO_ASSERT(3872, pte != NULL);
    IRO_ASSERT(3873, refcon != NULL);

    PointsToFunction_Add((PointsToFunction*)refcon, pte);
}

HEADER_INLINE void PointsToFunction_SimpleAddFunctionAction(PointsToEntry* pte, void* refcon) {
    IRO_ASSERT(3880, pte != NULL);
    IRO_ASSERT(3881, refcon != NULL);

    PointsToFunction_SimpleAdd((PointsToFunction*)refcon, pte);
}

HEADER_INLINE void PointsToFunction_AddAll(PointsToFunction* dest, PointsToFunction* src) {
    IRO_ASSERT(3888, dest != NULL);
    IRO_ASSERT(3889, src != NULL);

    if (dest->pte) {
        PointsToFunction_ForEach(src, PointsToFunction_AddFunctionAction, dest);
    } else {
        PointsToFunction_ForEach(src, PointsToFunction_SimpleAddFunctionAction, dest);
    }
}

HEADER_INLINE void PointsToFunction_SortByExtendedParamNum(PointsToFunction* pointsToFunc) {
    UInt32 value1;
    UInt32 value2;
    PointsToFunction* scan;

    IRO_ASSERT(3904, pointsToFunc != NULL);

    while (pointsToFunc && pointsToFunc->pte) {
        value1 = 0;
        if (pointsToFunc->pte->loc && pointsToFunc->pte->loc->block) {
            PAMemoryBlock* block = pointsToFunc->pte->loc->block;
            if (block->kind == PAMEMORYBLOCKKIND_EXTENDEDPARAM && block->u.ep) {
                value1 = 2 * (block->u.ep->num) + 1;
            } else if (block->kind == PAMEMORYBLOCKKIND_LOCALVAR && block->u.localvar) {
                if (block->u.localvar->obj && block->u.localvar->obj->extParam) {
                    value1 = 2 * block->u.localvar->obj->extParam->num;
                }
            }
        }

        for (scan = pointsToFunc->otherPtes; scan && scan->pte; scan = scan->otherPtes) {
            value2 = 0;
            if (scan->pte->loc && scan->pte->loc->block) {
                PAMemoryBlock* block = scan->pte->loc->block;
                if (block->kind == PAMEMORYBLOCKKIND_EXTENDEDPARAM && block->u.ep) {
                    value2 = 2 * (block->u.ep->num) + 1;
                } else if (block->kind == PAMEMORYBLOCKKIND_LOCALVAR && block->u.localvar) {
                    if (block->u.localvar->obj && block->u.localvar->obj->extParam) {
                        value2 = 2 * block->u.localvar->obj->extParam->num;
                    }
                }
            }

            if (value2 < value1) {
                LocationSet* saveloc;
                LocationSetSet* savelocs;
                saveloc = pointsToFunc->pte->loc;
                savelocs = pointsToFunc->pte->locs;
                pointsToFunc->pte->loc = scan->pte->loc;
                pointsToFunc->pte->locs = scan->pte->locs;
                scan->pte->loc = saveloc;
                scan->pte->locs = savelocs;
            }
        }

        pointsToFunc = pointsToFunc->otherPtes;
    }
}

HEADER_INLINE Boolean
PointsToFunctions_Equal(PointsToFunction* pointsToFunc1, PointsToFunction* pointsToFunc2) {
    PointsToFunction* scan;
    PointsToEntry* pte;

    IRO_ASSERT(3968, pointsToFunc1 != NULL);
    IRO_ASSERT(3969, pointsToFunc2 != NULL);

    if (pointsToFunc1 == pointsToFunc2) {
        return TRUE;
    }

    for (scan = pointsToFunc1; scan && scan->pte; scan = scan->otherPtes) {
        pte = PointsToFunction_FindByLocationSet(pointsToFunc2, scan->pte->loc);
        if (!pte || !PointsToEntries_Equal(pte, scan->pte)) {
            return FALSE;
        }
    }

    for (scan = pointsToFunc2; scan && scan->pte; scan = scan->otherPtes) {
        pte = PointsToFunction_FindByLocationSet(pointsToFunc1, scan->pte->loc);
        if (!pte || !PointsToEntries_Equal(pte, scan->pte)) {
            return FALSE;
        }
    }

    return TRUE;
}

HEADER_INLINE Boolean
PointsToFunctions_Match(PointsToFunction* pointsToFunc1, PointsToFunction* pointsToFunc2) {
    return TRUE;
}

HEADER_INLINE PartialTransferFunction* PartialTransferFunction_New(void) {
    PartialTransferFunction* ptf = IRO_malloc(sizeof(PartialTransferFunction));

    IRO_ASSERT(4110, ptf != NULL);
    ptf->initialPointsToFn = NULL;
    ptf->finalPointsToFn = NULL;
    ptf->funcModifies = NULL;
    ptf->context.nd = NULL;
    ptf->context.ptf = NULL;
    ptf->returnLocation = NULL;
    return ptf;
}

HEADER_INLINE void PartialTransferFunction_Delete(PartialTransferFunction* ptf) {
    IRO_ASSERT(4126, ptf != NULL);
    IRO_ASSERT(4127, ptf->initialPointsToFn == NULL);
    IRO_ASSERT(4128, ptf->finalPointsToFn == NULL);
    IRO_ASSERT(4129, ptf->funcModifies == NULL);
    IRO_ASSERT(4130, ptf->context.nd == NULL);
    IRO_ASSERT(4131, ptf->context.ptf == NULL);
    IRO_ASSERT(4132, ptf->returnLocation == NULL);
    IRO_DEBUG_CLEAR(ptf, sizeof(PartialTransferFunction));
    IRO_free(ptf);
}

HEADER_INLINE void PartialTransferFunction_Init(
    PartialTransferFunction* ptf, IROLinear* contextNd, PartialTransferFunction* contextPTF
) {
    IRO_ASSERT(4142, ptf != NULL);
    IRO_ASSERT(4143, contextNd == NULL || contextNd != NULL);
    IRO_ASSERT(4144, contextPTF == NULL || contextPTF != NULL);

    ptf->initialPointsToFn = PointsToFunction_New();
    PointsToFunction_Init(ptf->initialPointsToFn);
    ptf->finalPointsToFn = PointsToFunction_New();
    PointsToFunction_Init(ptf->finalPointsToFn);

    ptf->funcModifies = LocationSetSet_New();
    LocationSetSet_Init(ptf->funcModifies);
    LocationSetSet_AddUnknown(ptf->funcModifies, NULL, NULL, NULL);

    ptf->returnLocation = NULL;
    ptf->analyzed = FALSE;

    ptf->context.nd = contextNd;
    ptf->context.ptf = contextPTF;
}

HEADER_INLINE void PartialTransferFunction_Copy(
    PartialTransferFunction* dest, PartialTransferFunction* src
) {
    IRO_ASSERT(4164, src != NULL);
    IRO_ASSERT(4165, dest != NULL);

    dest->initialPointsToFn = PointsToFunction_New();
    PointsToFunction_Copy(dest->initialPointsToFn, src->initialPointsToFn);
    dest->finalPointsToFn = PointsToFunction_New();
    PointsToFunction_Copy(dest->finalPointsToFn, src->finalPointsToFn);

    dest->funcModifies = LocationSetSet_New();
    LocationSetSet_Copy(dest->funcModifies, src->funcModifies);

    if (src->returnLocation) {
        dest->returnLocation = LocationSet_New();
        LocationSet_Copy(dest->returnLocation, src->returnLocation);
    } else {
        dest->returnLocation = NULL;
    }

    dest->analyzed = src->analyzed;
    dest->context = src->context;
}

HEADER_INLINE void PartialTransferFunction_Term(PartialTransferFunction* ptf) {
    IRO_ASSERT(4190, ptf != NULL);

    PointsToFunction_Term(ptf->initialPointsToFn);
    PointsToFunction_Delete(ptf->initialPointsToFn);
    PointsToFunction_Term(ptf->finalPointsToFn);
    PointsToFunction_Delete(ptf->finalPointsToFn);
    LocationSetSet_Term(ptf->funcModifies);
    LocationSetSet_Delete(ptf->funcModifies);

    if (ptf->returnLocation) {
        PAMemoryBlock_Term(ptf->returnLocation->block);
        PAMemoryBlock_Delete(ptf->returnLocation->block);
        LocationSet_Term(ptf->returnLocation);
        LocationSet_Delete(ptf->returnLocation);
        ptf->returnLocation = NULL;
    }

    ptf->initialPointsToFn = NULL;
    ptf->finalPointsToFn = NULL;
    ptf->funcModifies = NULL;
    ptf->context.nd = NULL;
    ptf->context.ptf = NULL;
}

HEADER_INLINE PointsToFunction* PartialTransferFunction_initialPointsToFn(
    PartialTransferFunction* ptf
) {
    IRO_ASSERT(4221, ptf != NULL);

    return ptf->initialPointsToFn;
}

HEADER_INLINE PointsToFunction* PartialTransferFunction_finalPointsToFn(
    PartialTransferFunction* ptf
) {
    IRO_ASSERT(4227, ptf != NULL);

    return ptf->finalPointsToFn;
}

HEADER_INLINE LocationSetSet* PartialTransferFunction_funcModifies(PartialTransferFunction* ptf) {
    IRO_ASSERT(4233, ptf != NULL);

    return ptf->funcModifies;
}

HEADER_INLINE LocationSet* PartialTransferFunction_returnLocation(PartialTransferFunction* ptf) {
    IRO_ASSERT(4249, ptf != NULL);

    if (!ptf->returnLocation) {
        PAMemoryBlock* block;
        LocationSet* ls;

        block = PAMemoryBlock_New();
        PAMemoryBlock_Init(block, PAMEMORYBLOCKKIND_LOCALVAR, NULL);
        ls = LocationSet_New();
        LocationSet_InitKnown(ls, block, cint64_zero, 0, (Type*)&void_ptr);
        ptf->returnLocation = ls;
    }

    return ptf->returnLocation;
}

HEADER_INLINE IROLinear* PartialTransferFunction_contextNd(PartialTransferFunction* ptf) {
    IRO_ASSERT(4265, ptf != NULL);

    return ptf->context.nd;
}

HEADER_INLINE PartialTransferFunction* PartialTransferFunction_contextPTF(
    PartialTransferFunction* ptf
) {
    IRO_ASSERT(4271, ptf != NULL);

    return ptf->context.ptf;
}

HEADER_INLINE void PartialTransferFunction_SetAnalyzed(
    PartialTransferFunction* ptf, Boolean value
) {
    IRO_ASSERT(4298, ptf != NULL);

    ptf->analyzed = (value != 0) ? TRUE : FALSE;
}

HEADER_INLINE PTFList* PTFList_New(void) {
    PTFList* ptfList = IRO_malloc(sizeof(PTFList));

    IRO_ASSERT(4393, ptfList != NULL);
    ptfList->ptf = NULL;
    ptfList->otherPTFs = NULL;
    return ptfList;
}

HEADER_INLINE void PTFList_Delete(PTFList* ptfList) {
    IRO_ASSERT(4405, ptfList != NULL);
    IRO_ASSERT(4406, ptfList->ptf == NULL);
    IRO_ASSERT(4407, ptfList->otherPTFs == NULL);
    IRO_DEBUG_CLEAR(ptfList, sizeof(PTFList));
    IRO_free(ptfList);
}

HEADER_INLINE void PTFList_Init(PTFList* ptfList) {
    IRO_ASSERT(4417, ptfList != NULL);

    ptfList->ptf = NULL;
    ptfList->otherPTFs = NULL;
}

HEADER_INLINE void PTFList_Term(PTFList* ptfList) {
    IRO_ASSERT(4435, ptfList != NULL);

    PTFList_RemoveAll(ptfList);
    ptfList->ptf = NULL;
    ptfList->otherPTFs = NULL;
}

HEADER_INLINE void PTFList_ForEach(
    PTFList* ptfList, void (*action)(PartialTransferFunction*, void*), void* refcon
) {
    IRO_ASSERT(4478, ptfList != NULL);
    IRO_ASSERT(4479, action != NULL);
    IRO_ASSERT(4480, refcon == NULL || refcon != NULL);

    while (ptfList && ptfList->ptf) {
        action(ptfList->ptf, refcon);
        ptfList = ptfList->otherPTFs;
    }
}

HEADER_INLINE PartialTransferFunction* PTFList_Find(
    PTFList* ptfList, PartialTransferFunction* ptf
) {
    IRO_ASSERT(4490, ptfList != NULL);
    IRO_ASSERT(4491, ptf != NULL);

    while (ptfList && ptfList->ptf) {
        if (ptfList->ptf == ptf) {
            return ptfList->ptf;
        }

        ptfList = ptfList->otherPTFs;
    }

    return NULL;
}

HEADER_INLINE PartialTransferFunction* PTFList_FindFirst(PTFList* ptfList) {
    IRO_ASSERT(4502, ptfList != NULL);

    return ptfList->ptf;
}

HEADER_INLINE void PTFList_SimpleAdd(PTFList* ptfList, PartialTransferFunction* ptf) {
    IRO_ASSERT(4511, ptfList != NULL);
    IRO_ASSERT(4512, ptf != NULL);

    if (ptfList->ptf) {
        PTFList* newList = PTFList_New();
        PTFList_Init(newList);
        newList->ptf = ptfList->ptf;
        newList->otherPTFs = ptfList->otherPTFs;
        ptfList->otherPTFs = newList;
    }

    ptfList->ptf = ptf;
}

HEADER_INLINE void PTFList_Add(PTFList* ptfList, PartialTransferFunction* ptf) {
    IRO_ASSERT(4529, ptfList != NULL);
    IRO_ASSERT(4530, ptf != NULL);

    if (!PTFList_Find(ptfList, ptf)) {
        PTFList_SimpleAdd(ptfList, ptf);
    }
}

HEADER_INLINE void PTFList_Remove(PTFList* ptfList, PartialTransferFunction* ptf) {
    PTFList* prev;
    PTFList* second;

    IRO_ASSERT(4542, ptfList != NULL);
    IRO_ASSERT(4543, ptf != NULL);

    prev = NULL;
    while (ptfList && ptfList->ptf) {
        if (ptfList->ptf == ptf) {
            if (!prev) {
                if (ptfList->otherPTFs == NULL) {
                    ptfList->ptf = NULL;
                } else {
                    second = ptfList->otherPTFs;
                    ptfList->ptf = ptfList->otherPTFs->ptf;
                    ptfList->otherPTFs = ptfList->otherPTFs->otherPTFs;
                    second->ptf = NULL;
                    second->otherPTFs = NULL;
                    PTFList_Term(second);
                    PTFList_Delete(second);
                }
            } else {
                prev->otherPTFs = ptfList->otherPTFs;
                ptfList->ptf = NULL;
                ptfList->otherPTFs = NULL;
                PTFList_Term(ptfList);
                PTFList_Delete(ptfList);
            }
            return;
        }

        prev = ptfList;
        ptfList = ptfList->otherPTFs;
    }
}

HEADER_INLINE void PTFList_RemoveAll(PTFList* ptfList) {
    IRO_ASSERT(4582, ptfList != NULL);

    while (ptfList && ptfList->ptf) {
        PTFList_Remove(ptfList, ptfList->ptf);
    }
}
#endif
