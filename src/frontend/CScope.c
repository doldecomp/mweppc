#include "compiler/CScope.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CError.h"
#include "compiler/CMangler.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CPrepTokenizer.h"
#include "compiler/CTemplateClass.h"
#include "compiler/CTemplateNew.h"
#include "compiler/CTemplateTools.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/templates.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#include <string.h>

typedef enum ELookupType {
    Lookup_Object,
    Lookup_Tag,
    Lookup_NameSpace
} ELookupType;

struct NameSpaceLookupList {
    NameSpaceLookupList* next;
    NameSpace* nspace;
    NameSpaceList* namespaces;
};

struct CScopeNSIterator {
    NameSpace* nspace;
    NameSpaceLookupList* lookup;
    NameResult* result;
};

static TypeClass* cscope_mostderived;
static HashNameNode* cscope_name;
static TypeClass* cscope_foundclass;
static TemplClass* cscope_foundtemplate;
static SInt32 cscope_foundclassoffset;
static ELookupType cscope_lookuptype;
static Boolean cscope_isambig;
Boolean cscope_is_member_func;
Object* cscope_currentfunc;
TypeClass* cscope_currentclass;
NameSpace* cscope_current;
NameSpace* cscope_root;

void CScope_Setup(void) {
    cscope_current = cscope_root = CScope_NewHashNameSpace(NULL);
    cscope_currentclass = NULL;
    cscope_currentfunc = NULL;
    cscope_is_member_func = FALSE;
}

void CScope_Cleanup(void) {}

void CScope_GetScope(CScopeSave* save) {
    save->current = cscope_current;
    save->currentclass = cscope_currentclass;
    save->currentfunc = cscope_currentfunc;
    save->is_member_func = cscope_is_member_func;
}

void CScope_SetNameSpaceScope(NameSpace* nspace, CScopeSave* save) {
    save->current = cscope_current;
    save->currentclass = cscope_currentclass;
    save->currentfunc = cscope_currentfunc;
    save->is_member_func = cscope_is_member_func;

    cscope_current = nspace;
    cscope_currentclass = nspace->theclass;
    cscope_currentfunc = NULL;
    cscope_is_member_func = FALSE;
}

void CScope_SetClassScope(TypeClass* cls, CScopeSave* save) {
    save->current = cscope_current;
    save->currentclass = cscope_currentclass;
    save->currentfunc = cscope_currentfunc;
    save->is_member_func = cscope_is_member_func;

    cscope_current = cls->nspace;
    cscope_currentclass = cls;
    cscope_currentfunc = NULL;
    cscope_is_member_func = FALSE;
}

void CScope_SetClassDefScope(TypeClass* cls, CScopeSave* save) {
    save->current = cscope_current;
    save->currentclass = cscope_currentclass;
    save->currentfunc = cscope_currentfunc;
    save->is_member_func = cscope_is_member_func;

    cscope_current = cls->nspace;
    cscope_currentclass = cls;
}

void CScope_SetFunctionScope(Object* function, CScopeSave* save) {
    save->current = cscope_current;
    save->currentclass = cscope_currentclass;
    save->currentfunc = cscope_currentfunc;
    save->is_member_func = cscope_is_member_func;

    cscope_currentfunc = function;
    cscope_currentclass = NULL;
    cscope_is_member_func = FALSE;

    if (TYPE_FUNC(function->type)->flags & FUNC_METHOD) {
        cscope_currentclass = TYPE_METHOD(function->type)->theclass;
        cscope_current = cscope_currentclass->nspace;
        cscope_is_member_func = !TYPE_METHOD(function->type)->is_static;
    } else {
        cscope_current = function->nspace;
    }
}

void CScope_SetMethodScope(Object* function, TypeClass* cls, Boolean is_static, CScopeSave* save) {
    save->current = cscope_current;
    save->currentclass = cscope_currentclass;
    save->currentfunc = cscope_currentfunc;
    save->is_member_func = cscope_is_member_func;

    cscope_currentfunc = function;
    cscope_currentclass = cls;
    cscope_current = cls->nspace;
    cscope_is_member_func = !is_static;
}

void CScope_RestoreScope(CScopeSave* saved) {
    cscope_current = saved->current;
    cscope_currentclass = saved->currentclass;
    cscope_currentfunc = saved->currentfunc;
    cscope_is_member_func = saved->is_member_func;
}

static Boolean CScope_ObjectIsFunction(Object* obj) {
    return obj->otype == OT_OBJECT && IS_TYPE_FUNC(obj->type);
}

Boolean CScope_IsEmptySymTable(void) {
    NameSpaceObjectList* scan;
    NameSpaceName* nsname;
    SInt32 i;

    CError_ASSERT(232, cscope_root->is_hash);

    for (i = 0; i < 1024; i++) {
        for (nsname = cscope_root->data.hash[i]; nsname; nsname = nsname->next) {
            for (scan = &nsname->first; scan; scan = scan->next) {
                if (scan->object->otype != OT_OBJECT ||
                    !CParser_IsPublicRuntimeObject(OBJECT(scan->object))) {
                    return FALSE;
                }
            }
        }
    }

    return TRUE;
}

Boolean CScope_IsInLocalNameSpace(NameSpace* nspace) {
    while (nspace) {
        if (!nspace->is_global && !nspace->is_templ) {
            return TRUE;
        }
        nspace = nspace->parent;
    }
    return FALSE;
}

static void CScope_AppendNameSpaceName(NameSpaceName* nsn) {
    if (nsn->next) {
        CScope_AppendNameSpaceName(nsn->next);
    }
    if (nsn->name) {
        AppendGListName(&name_mangle_list, nsn->name->name);
        AppendGListName(&name_mangle_list, "::");
    }
}

static void CScope_AmbigNameError(NameSpace* nspace1, NameSpace* nspace2, HashNameNode* name) {
    if (name && nspace1 != nspace2) {
        CError_Error(
            ERR_AMBIGUOUS_ACCESS_NAME_FOUND,
            CError_GetNameString(nspace1, name),
            CError_GetNameString(nspace2, name)
        );
    } else {
        CError_Error(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
    }
}

NameSpaceObjectList* CScope_FindName(NameSpace* nspace, HashNameNode* name) {
    NameSpaceName* scan;

    for (scan = nspace->tparams; scan; scan = scan->next) {
        if (scan->name == name) {
            return &scan->first;
        }
    }

    if (!nspace->is_hash) {
        scan = nspace->data.list;
    } else {
        scan = nspace->data.hash[name->hashval & 1023];
    }

    for (; scan; scan = scan->next) {
        if (scan->name == name) {
            return &scan->first;
        }
    }

    return NULL;
}

static NameSpaceObjectList* CScope_FindQualName(NameSpace* nspace, HashNameNode* name) {
    NameSpaceName* scan;

    if (!nspace->is_hash) {
        scan = nspace->data.list;
    } else {
        scan = nspace->data.hash[name->hashval & 1023];
    }

    for (; scan; scan = scan->next) {
        if (scan->name == name) {
            return &scan->first;
        }
    }

    return NULL;
}

NameSpaceName* CScope_FindNameSpaceName(NameSpace* nspace, HashNameNode* name) {
    NameSpaceName* scan;

    for (scan = nspace->tparams; scan; scan = scan->next) {
        if (scan->name == name) {
            return scan;
        }
    }

    if (!nspace->is_hash) {
        scan = nspace->data.list;
    } else {
        scan = nspace->data.hash[name->hashval & 1023];
    }

    for (; scan; scan = scan->next) {
        if (scan->name == name) {
            return scan;
        }
    }

    return NULL;
}

NameSpaceObjectList* CScope_InsertName(NameSpace* nspace, HashNameNode* name) {
    NameSpaceName** nsnptr;
    NameSpaceName* nsname;
    nsname = nspace->is_global ? galloc(sizeof(NameSpaceName)) : lalloc(sizeof(NameSpaceName));

    nsname->name = name;
    nsname->first.next = NULL;
    nsname->first.object = NULL;
    if (nspace->is_hash) {
        nsnptr = &nspace->data.hash[name->hashval & 0x3FF];
        nsname->next = *nsnptr;
        *nsnptr = nsname;
    } else {
        nsname->next = nspace->data.list;
        nspace->data.list = nsname;
    }
    nspace->names++;

    return &nsname->first;
}

static NameSpaceObjectList* CScope_AppendName(NameSpace* nspace, HashNameNode* name) {
    NameSpaceName* nsname;
    NameSpaceName* scan;
    CError_ASSERT(387, !nspace->is_hash);

    if (nspace->is_global) {
        nsname = galloc(sizeof(NameSpaceName));
    } else {
        nsname = lalloc(sizeof(NameSpaceName));
    }

    nsname->next = NULL;
    nsname->name = name;
    nsname->first.next = NULL;
    nsname->first.object = NULL;
    if (nspace->data.list) {
        scan = nspace->data.list;
        while (scan->next) {
            scan = scan->next;
        }
        scan->next = nsname;
    } else {
        nspace->data.list = nsname;
    }
    nspace->names++;

    return &nsname->first;
}

static NameSpaceList* CScope_AddNameSpaceToList(NameSpaceList* list, NameSpace* nspace) {
    NameSpaceList* scan;
    NameSpaceList* newlist;
    ClassList* clslist;

    for (scan = list; scan; scan = scan->next) {
        if (scan->nspace == nspace) {
            return list;
        }
    }

    newlist = lalloc(sizeof(NameSpaceList));
    newlist->next = list;
    newlist->nspace = nspace;
    list = newlist;

    if (nspace->theclass) {
        list = CScope_AddNameSpaceToList(list, nspace->parent);
        for (clslist = nspace->theclass->bases; clslist; clslist = clslist->next) {
            list = CScope_AddNameSpaceToList(list, clslist->base->nspace);
        }
    }

    return list;
}

static NameSpaceList* CScope_BuildTypeAssociatedNameSpaceList(NameSpaceList* list, Type* type) {
    FuncArg* arg;

restart:
    switch (type->type) {
        case TYPEVOID:
        case TYPEINT:
        case TYPEFLOAT:
        case TYPESTRUCT:
        case TYPEBITFIELD:
        case TYPETEMPLATE:
        case TYPETEMPLDEPEXPR:
            break;
        case TYPEPOINTER:
        case TYPEARRAY:
            type = TYPE_POINTER(type)->target;
            goto restart;
        case TYPEENUM:
            list = CScope_AddNameSpaceToList(list, TYPE_ENUM(type)->nspace);
            break;
        case TYPEFUNC:
            for (arg = TYPE_FUNC(type)->args; arg; arg = arg->next) {
                if (arg->type) {
                    list = CScope_BuildTypeAssociatedNameSpaceList(list, arg->type);
                }
            }
            type = TYPE_FUNC(type)->functype;
            goto restart;
        case TYPEMEMBERPOINTER:
            list = CScope_BuildTypeAssociatedNameSpaceList(list, TYPE_MEMBER_POINTER(type)->ty1);
            type = TYPE_MEMBER_POINTER(type)->ty2;
            if (!IS_TYPE_CLASS(type)) {
                break;
            }
        case TYPECLASS:
            list = CScope_AddNameSpaceToList(list, TYPE_CLASS(type)->nspace);
            break;
        default:
            CError_FATAL(494);
    }

    return list;
}

static NameSpaceList* CScope_BuildAssociatedNameSpaceList(ENodeList* enodeList) {
    NameSpaceList* nslist = NULL;
    while (enodeList) {
        nslist = CScope_BuildTypeAssociatedNameSpaceList(nslist, enodeList->node->rtype);
        enodeList = enodeList->next;
    }
    return nslist;
}

static Boolean CScope_IsSameObject(Object* a, Object* b) {
    if (a == b) {
        return TRUE;
    }
    return (a->nspace == b->nspace) && (a->name == b->name) && is_typesame(a->type, b->type);
}

NameSpaceObjectList* CScope_ArgumentDependentNameLookup(
    NameSpaceObjectList* list, HashNameNode* name, ENodeList* argNodes, Boolean skipMethods
) {
    NameSpaceList* scan;
    NameSpaceObjectList* iscan;
    NameSpaceObjectList* iiscan;
    NameSpaceObjectList* newlist;

    for (scan = CScope_BuildAssociatedNameSpaceList(argNodes); scan; scan = scan->next) {
        for (iscan = CScope_FindQualName(scan->nspace, name); iscan; iscan = iscan->next) {
            if (iscan->object->otype != OT_OBJECT) {
                continue;
            }
            if (!IS_TYPE_FUNC(OBJECT(iscan->object)->type)) {
                continue;
            }
            if (skipMethods && (TYPE_FUNC(OBJECT(iscan->object)->type)->flags & FUNC_METHOD)) {
                continue;
            }

            for (iiscan = list; iiscan; iiscan = iiscan->next) {
                if (iiscan->object->otype == OT_OBJECT &&
                    CScope_IsSameObject(OBJECT(iiscan->object), OBJECT(iscan->object))) {
                    break;
                }
            }

            if (!iiscan) {
                newlist = lalloc(sizeof(NameSpaceObjectList));
                newlist->object = iscan->object;
                newlist->next = list;
                list = newlist;
            }
        }
    }

    return list;
}

NameSpace* CScope_NewHashNameSpace(HashNameNode* name) {
    NameSpace* nspace;
    void* buffer;

    buffer = galloc(0x400 * sizeof(void*));
    memclrw(buffer, 0x400 * sizeof(void*));
    nspace = galloc(sizeof(NameSpace));
    memclrw(nspace, sizeof(NameSpace));
    nspace->name = name;
    nspace->data.hash = buffer;
    nspace->is_hash = TRUE;
    nspace->is_global = TRUE;
    return nspace;
}

NameSpace* CScope_NewListNameSpace(HashNameNode* name, Boolean is_global) {
    NameSpace* nspace;
    if (is_global) {
        nspace = galloc(sizeof(NameSpace));
        memclrw(nspace, sizeof(NameSpace));
    } else {
        nspace = lalloc(sizeof(NameSpace));
        memclrw(nspace, sizeof(NameSpace));
    }
    nspace->name = name;
    nspace->is_hash = FALSE;
    nspace->is_global = is_global;
    return nspace;
}

NameSpace* CScope_FindNonClassNonFunctionNS(NameSpace* nspace) {
    while (nspace) {
        if (!nspace->theclass && !nspace->is_templ) {
            return nspace;
        }
        nspace = nspace->parent;
    }
    return cscope_root;
}

NameSpace* CScope_FindGlobalNS(NameSpace* nspace) {
    while (nspace) {
        if (nspace->name && !nspace->theclass && nspace->is_global) {
            return nspace;
        }
        nspace = nspace->parent;
    }
    return cscope_root;
}

Boolean CScope_IsStdNameSpace(NameSpace* nspace) {
    return nspace &&
        !nspace->theclass &&
        (nspace->parent == cscope_root) &&
        nspace->name &&
        !strcmp(nspace->name->name, "std");
}

Boolean CScope_IsEmptyNameSpace(NameSpace* nspace) {
    CError_ASSERT(664, !nspace->is_hash);
    return nspace->data.list == NULL;
}

void CScope_MergeNameSpace(NameSpace* dst, NameSpace* src) {
    NameSpaceName* nsname;

    CError_ASSERT(678, !dst->is_hash && !src->is_hash);

    if ((nsname = dst->data.list)) {
        while (nsname->next) {
            nsname = nsname->next;
        }
        nsname->next = src->data.list;
    } else {
        dst->data.list = src->data.list;
    }
}

void CScope_AddObject(NameSpace* nspace, HashNameNode* name, ObjBase* obj) {
    UInt8 otype;
    NameSpaceObjectList* scan;
    NameSpaceObjectList* objlist;
    NameSpaceObjectList* newlist;
    Boolean isFunction;
    Boolean isExistingFunction;

    objlist = CScope_FindQualName(nspace, name);
    if ((scan = objlist)) {
        newlist = nspace->is_global ? (NameSpaceObjectList*)galloc(sizeof(NameSpaceObjectList))
                                    : (NameSpaceObjectList*)lalloc(sizeof(NameSpaceObjectList));
        if ((otype = obj->otype) == OT_NAMESPACE || objlist->object->otype == OT_NAMESPACE) {
            CError_Error(ERR_ILLEGAL_NAME_OVERLOADING);
            return;
        }
        if (otype == OT_TYPETAG) {
            do {
                if (scan->object->otype == OT_TYPETAG) {
                    CError_Error(ERR_ILLEGAL_NAME_OVERLOADING);
                    return;
                }
                if (copts.cplusplus &&
                    scan->object->otype == OT_TYPE &&
                    !is_typesame(OBJ_TYPE_TAG(obj)->type, OBJ_TYPE(scan->object)->type)) {
                    CError_Error(ERR_TYPENAME_REDEFINED);
                    return;
                }
                if (!scan->next) {
                    scan->next = newlist;
                    newlist->next = NULL;
                    newlist->object = obj;
                    return;
                }
                scan = scan->next;
            } while (1);
        }
        if (objlist->object->otype == OT_TYPETAG) {
            if (copts.cplusplus &&
                otype == OT_TYPE &&
                !is_typesame(OBJ_TYPE(obj)->type, OBJ_TYPE_TAG(objlist->object)->type)) {
                CError_Error(ERR_TYPENAME_REDEFINED);
                return;
            }
            CError_ASSERT(739, !objlist->next);
            newlist->object = objlist->object;
            newlist->next = NULL;
            objlist->object = obj;
            objlist->next = newlist;
            goto done;
        }
        if (!copts.cplusplus ||
            !(isFunction = otype == OT_OBJECT && IS_TYPE_FUNC(OBJECT(obj)->type))) {
            CError_Error(ERR_ILLEGAL_NAME_OVERLOADING);
            return;
        }

        do {
            if (scan->object->otype == OT_TYPETAG) {
                scan->next = galloc(sizeof(NameSpaceObjectList));
                scan->next->next = NULL;
                scan->next->object = scan->object;
                scan->object = obj;
                return;
            }

            isExistingFunction = OBJECT(scan->object)->otype == OT_OBJECT &&
                IS_TYPE_FUNC(OBJECT(scan->object)->type);
            if (!isExistingFunction) {
                CError_Error(ERR_ILLEGAL_NAME_OVERLOADING);
                return;
            }

            if (!scan->next) {
                scan->next = galloc(sizeof(NameSpaceObjectList));
                scan->next->next = NULL;
                scan->next->object = obj;
                return;
            }

            scan = scan->next;
        } while (1);
    } else if (nspace->theclass && TYPE_CLASS(nspace->theclass)->flags & CLASS_IS_TEMPL_ANY) {
        CScope_AppendName(nspace, name)->object = obj;
    } else {
        CScope_InsertName(nspace, name)->object = obj;
    }
done:;
}

void CScope_AddGlobalObject(Object* obj) {
    obj->nspace = cscope_root;
    CScope_AddObject(cscope_root, obj->name, OBJ_BASE(obj));
}

NameSpaceLookupList* CScope_BuildNameSpaceLookupList(NameSpace* nspace) {
    NameSpaceLookupList* lookup;
    NameSpaceList list;
    NameSpaceList* current2;
    NameSpaceList* current3;
    NameSpaceList* newlist;
    NameSpaceLookupList* currentL;
    NameSpaceList* current;
    NameSpace* currentNS;

    lookup = lalloc(sizeof(NameSpaceLookupList));
    memclrw(lookup, sizeof(NameSpaceLookupList));
    lookup->nspace = nspace;

    if (nspace->parent) {
        lookup->next = CScope_BuildNameSpaceLookupList(nspace->parent);
    }

    if (nspace->usings) {
        list.nspace = nspace;
        list.next = NULL;
        for (current2 = &list; current2; current2 = current2->next) {
            for (current = current2->nspace->usings; current; current = current->next) {
                for (current3 = &list; current3; current3 = current3->next) {
                    if (current3->nspace == current->nspace) {
                        break;
                    }
                }
                if (!current3) {
                    newlist = lalloc(sizeof(NameSpaceList));
                    newlist->nspace = current->nspace;
                    newlist->next = current2->next;
                    current2->next = newlist;
                }
            }
        }

        for (current = list.next; current; current = current->next) {
            for (currentL = lookup; currentL; currentL = currentL->next) {
                for (currentNS = current->nspace; currentNS; currentNS = currentNS->parent) {
                    if (currentL->nspace == currentNS) {
                        goto foundIt;
                    }
                }
            }
            CError_FATAL(853);
foundIt:
            for (current2 = currentL->namespaces; current2; current2 = current2->next) {
                if (current2->nspace == current->nspace) {
                    break;
                }
            }
            if (!current2) {
                newlist = lalloc(sizeof(NameSpaceList));
                newlist->nspace = current->nspace;
                newlist->next = currentL->namespaces;
                currentL->namespaces = newlist;
            }
        }
    }

    return lookup;
}

static Boolean CScope_IsInLookup(NameSpaceLookupList* list, NameSpace* nspace) {
    NameSpaceLookupList* scan;
    NameSpaceList* iscan;

    for (scan = list; scan; scan = scan->next) {
        if (scan->nspace == nspace) {
            return TRUE;
        }
        for (iscan = scan->namespaces; iscan; iscan = iscan->next) {
            if (iscan->nspace == nspace) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static NameSpaceLookupList* CScope_BuildNameSpaceMemberLookupList(NameSpace* nspace) {
    NameSpaceLookupList list;
    NameSpaceLookupList* current;
    NameSpaceList* scan;
    NameSpaceList* oscan;
    NameSpaceList* newlist;

    list.nspace = nspace;
    list.namespaces = NULL;
    list.next = NULL;

    for (current = &list; current; current = current->next) {
        if (current->nspace) {
            for (scan = current->nspace->usings; scan; scan = scan->next) {
                if (!CScope_IsInLookup(&list, scan->nspace)) {
                    if (!current->next) {
                        current->next = lalloc(sizeof(NameSpaceLookupList));
                        current->next->nspace = NULL;
                        current->next->namespaces = NULL;
                        current->next->next = NULL;
                    }
                    newlist = lalloc(sizeof(NameSpaceList));
                    newlist->nspace = scan->nspace;
                    newlist->next = current->next->namespaces;
                    current->next->namespaces = newlist;
                }
            }
        }

        for (oscan = current->namespaces; oscan; oscan = oscan->next) {
            for (scan = oscan->nspace->usings; scan; scan = scan->next) {
                if (!CScope_IsInLookup(&list, scan->nspace)) {
                    if (!current->next) {
                        current->next = lalloc(sizeof(NameSpaceLookupList));
                        current->next->nspace = NULL;
                        current->next->namespaces = NULL;
                        current->next->next = NULL;
                    }
                    newlist = lalloc(sizeof(NameSpaceList));
                    newlist->nspace = scan->nspace;
                    newlist->next = current->next->namespaces;
                    current->next->namespaces = newlist;
                }
            }
        }
    }

    return list.next;
}

static NameSpaceObjectList* CScope_CopyNameSpaceObjectList(NameSpaceObjectList* list) {
    NameSpaceObjectList *first, *current;

    first = current = lalloc(sizeof(NameSpaceObjectList));
    do {
        current->object = list->object;
        list = list->next;
        if (!list) {
            current->next = NULL;
            break;
        } else {
            current->next = lalloc(sizeof(NameSpaceObjectList));
            current = current->next;
        }
    } while (1);

    return first;
}

static NameSpace* CScope_ExtractNameSpace(NameSpaceObjectList* list, Boolean* fail) {
    Type* type;

restart:
    if (!list) {
        return NULL;
    }

    switch (list->object->otype) {
        case OT_TYPE:
            type = OBJ_TYPE(list->object)->type;
            break;
        case OT_TYPETAG:
            type = OBJ_TYPE_TAG(list->object)->type;
            break;
        case OT_NAMESPACE:
            return OBJ_NAMESPACE(list->object)->nspace;
        default:
            CError_FATAL(1052);
            break;
        case OT_ENUMCONST:
        case OT_MEMBERVAR:
        case OT_OBJECT:
            list = list->next;
            goto restart;
    }

    if (!IS_TYPE_CLASS(type)) {
        if (IS_TYPE_TEMPLATE(type)) {
            return NULL;
        }
        CError_Error(ERR_ILLEGAL_NAMESPACE);
        if (fail) {
            *fail = TRUE;
        }
        return NULL;
    } else {
        return TYPE_CLASS(type)->nspace;
    }
}

static BClassList* CScope_RecFindClassMember(NameResult* result, TypeClass* tclass, SInt32 offset) {
    Boolean fail;
    NameSpace* nspace;
    NameSpaceObjectList* list;
    TypeClass* bestClass;
    ClassList* base;
    BClassList* bestBase;
    BClassList* n;
    BClassList* candidate;
    SInt32 thisoffset;
    BClassList* newlist;

    if ((list = CScope_FindName(tclass->nspace, cscope_name))) {
        if (cscope_foundclass) {
            if (CClass_ClassDominates(cscope_foundclass, tclass)) {
                return NULL;
            }
            if (CClass_ClassDominates(tclass, cscope_foundclass)) {
                cscope_foundclass = NULL;
            }
        }

        switch (cscope_lookuptype) {
            case Lookup_NameSpace:
                fail = FALSE;
                if ((nspace = CScope_ExtractNameSpace(list, &fail))) {
                    if (cscope_foundclass) {
                        if (cscope_foundclass != tclass) {
                            CScope_AmbigNameError(
                                cscope_foundclass->nspace, tclass->nspace, cscope_name
                            );
                            return NULL;
                        }
                        if (cscope_foundclassoffset != offset) {
                            cscope_isambig = TRUE;
                        }
                        return NULL;
                    }

                    cscope_foundclass = tclass;
                    cscope_foundclassoffset = offset;
                    result->nspace = nspace;

                    newlist = lalloc(sizeof(BClassList));
                    newlist->next = NULL;
                    newlist->type = TYPE(tclass);
                    return newlist;
                }

                if (fail) {
                    return NULL;
                }
                break;
            case Lookup_Object:
                if (cscope_foundclass) {
                    if (list->object->otype == OT_TYPETAG &&
                        result->objects->object->otype == OT_TYPETAG &&
                        IS_TYPE_CLASS(OBJ_TYPE_TAG(list->object)->type) &&
                        IS_TYPE_CLASS(OBJ_TYPE_TAG(result->objects->object)->type) &&
                        (TYPE_CLASS(OBJ_TYPE_TAG(list->object)->type)->flags &
                         CLASS_IS_TEMPL_INST) &&
                        (TYPE_CLASS(OBJ_TYPE_TAG(result->objects->object)->type)->flags &
                         CLASS_IS_TEMPL_INST) &&
                        TEMPL_CLASS_INST(OBJ_TYPE_TAG(list->object)->type)->templ ==
                            TEMPL_CLASS_INST(OBJ_TYPE_TAG(result->objects->object)->type)->templ) {
                        cscope_foundtemplate =
                            TEMPL_CLASS_INST(OBJ_TYPE_TAG(result->objects->object)->type)->templ;
                    } else {
                        if (cscope_foundclass != tclass) {
                            CScope_AmbigNameError(
                                cscope_foundclass->nspace, tclass->nspace, cscope_name
                            );
                            return NULL;
                        }
                        if (cscope_foundclassoffset != offset) {
                            cscope_isambig = TRUE;
                            return NULL;
                        }
                    }
                }

                cscope_foundclass = tclass;
                cscope_foundclassoffset = offset;
                result->objects = list;
                newlist = lalloc(sizeof(BClassList));
                newlist->next = NULL;
                newlist->type = TYPE(tclass);
                return newlist;
            case Lookup_Tag:
                for (; list; list = list->next) {
                    if (list->object->otype == OT_TYPETAG) {
                        if (cscope_foundclass) {
                            if (cscope_foundclass != tclass) {
                                CScope_AmbigNameError(
                                    cscope_foundclass->nspace, tclass->nspace, cscope_name
                                );
                                return NULL;
                            }
                            if (cscope_foundclassoffset != offset) {
                                cscope_isambig = TRUE;
                            }
                            return NULL;
                        }

                        cscope_foundclass = tclass;
                        cscope_foundclassoffset = offset;
                        result->type = OBJ_TYPE_TAG(list->object)->type;
                        newlist = lalloc(sizeof(BClassList));
                        newlist->next = NULL;
                        newlist->type = TYPE(tclass);
                        return newlist;
                    }
                }
                break;
            default:
                CError_FATAL(1202);
        }
    }

    for (base = tclass->bases, bestBase = NULL; base; base = base->next) {
        thisoffset = base->is_virtual ? CClass_VirtualBaseOffset(cscope_mostderived, base->base)
                                      : offset + base->offset;
        if ((candidate = CScope_RecFindClassMember(result, base->base, thisoffset))) {
            n = lalloc(sizeof(BClassList));
            n->next = candidate;
            n->type = TYPE(tclass);
            if (bestBase && bestClass == cscope_foundclass) {
                if (CClass_IsMoreAccessiblePath(n, bestBase)) {
                    bestBase = n;
                }
            } else {
                bestClass = cscope_foundclass;
                bestBase = n;
            }
        }
    }

    return bestBase;
}

static Boolean CScope_FindClassMember(
    NameResult* result, NameSpace* nspace, HashNameNode* name, ELookupType lookupType
) {
    BClassList* bcl;
    BClassList* scan;

    cscope_mostderived = nspace->theclass;
    cscope_foundclass = NULL;
    cscope_foundtemplate = NULL;
    cscope_name = name;
    cscope_lookuptype = lookupType;
    cscope_isambig = FALSE;

    if ((bcl = CScope_RecFindClassMember(result, cscope_mostderived, 0))) {
        if (cscope_foundtemplate) {
            return TRUE;
        }

        if ((scan = result->basePath)) {
            while (scan->next) {
                scan = scan->next;
            }
            if (TYPE_CLASS(scan->type) != cscope_mostderived) {
                scan->next = bcl;
            } else {
                scan->next = bcl->next;
            }
        } else {
            result->basePath = bcl;
        }

        if (cscope_isambig) {
            result->isambig = TRUE;
        }
        return TRUE;
    } else {
        return FALSE;
    }
}

static BClassList* CScope_GetAccessPathRec(TypeClass* src, TypeClass* dest, SInt32 offset) {
    BClassList* list;
    ClassList* base;
    BClassList* best;

    if (src == dest) {
        if (cscope_foundclass && cscope_foundclassoffset != offset) {
            CError_Error(ERR_AMBIGUOUS_ACCESS_CLASS_STRUCT_UNION_MEMBER);
        }

        list = lalloc(sizeof(BClassList));
        list->next = NULL;
        list->type = TYPE(src);
        cscope_foundclass = src;
        cscope_foundclassoffset = offset;
        return list;
    }

    for (base = src->bases, best = NULL; base; base = base->next) {
        if ((list = CScope_GetAccessPathRec(
                 base->base,
                 dest,
                 base->is_virtual ? CClass_VirtualBaseOffset(cscope_mostderived, base->base)
                                  : (offset + base->offset)
             ))) {
            best = list;
        }
    }

    if (best) {
        list = lalloc(sizeof(BClassList));
        list->next = best;
        list->type = TYPE(src);
        return list;
    }

    return NULL;
}

static BClassList* CScope_GetAccessPath(TypeClass* src, TypeClass* dest) {
    cscope_mostderived = src;
    cscope_foundclass = NULL;
    return CScope_GetAccessPathRec(src, dest, 0);
}

static NameSpace* CScope_FindLookupNameSpace(
    CScopeNSIterator* iterator, NameSpaceLookupList* lookup, HashNameNode* name
) {
    NameSpaceObjectList* list;
    NameSpaceList* scan;
    NameSpace* retnspace = NULL;
    NameSpace* candidate;
    Boolean fail = FALSE;

    if (lookup->nspace->theclass) {
        if (CScope_FindClassMember(iterator->result, lookup->nspace, name, Lookup_NameSpace)) {
            retnspace = iterator->result->nspace;
            iterator->result->nspace = NULL;
            return retnspace;
        } else {
            return NULL;
        }
    }

    if ((list = CScope_FindName(lookup->nspace, name))) {
        fail = FALSE;
        retnspace = CScope_ExtractNameSpace(list, &fail);
        if (fail) {
            return NULL;
        }
    }

    for (scan = lookup->namespaces; scan; scan = scan->next) {
        fail = FALSE;
        if ((list = CScope_FindName(scan->nspace, name)) &&
            (candidate = CScope_ExtractNameSpace(list, &fail))) {
            if (retnspace) {
                if (retnspace != candidate) {
                    CScope_AmbigNameError(retnspace, candidate, NULL);
                }
            } else {
                retnspace = candidate;
            }
        }
        if (fail) {
            return NULL;
        }
    }

    return retnspace;
}

static NameSpaceObjectList* CScope_FindLookupName(
    NameSpaceLookupList* list, HashNameNode* name, NameSpace** foundnspace
) {
    NameSpaceName* scanname;
    NameSpaceObjectList* found;
    NameSpaceObjectList* result;
    NameSpaceList* scan;
    NameSpace* resultnspace;
    Boolean copied;
    NameSpaceObjectList* check;
    NameSpaceObjectList* cand;
    NameSpaceObjectList* scanOL;
    NameSpaceObjectList* newlist;

    if (list->nspace) {
        for (scanname = list->nspace->tparams; scanname; scanname = scanname->next) {
            if (scanname->name == name) {
                return &scanname->first;
            }
        }

        if ((found = CScope_FindName(list->nspace, name))) {
            result = found;
            resultnspace = list->nspace;
        } else {
            result = NULL;
            resultnspace = NULL;
        }
    } else {
        result = NULL;
        resultnspace = NULL;
    }

    copied = FALSE;
    for (scan = list->namespaces; scan; scan = scan->next) {
        if ((found = CScope_FindName(scan->nspace, name))) {
            if (result) {
                for (cand = found; cand; cand = cand->next) {
                    for (check = result; check; check = check->next) {
                        if (check->object == cand->object) {
                            break;
                        }
                        if (check->object->otype == cand->object->otype) {
                            switch (check->object->otype) {
                                case OT_TYPE:
                                    if ((OBJ_TYPE(check->object)->type ==
                                         OBJ_TYPE(cand->object)->type) &&
                                        (OBJ_TYPE(check->object)->qual ==
                                         OBJ_TYPE(cand->object)->qual)) {
                                        goto break1;
                                    }
                                    break;
                                case OT_TYPETAG:
                                    if (OBJ_TYPE_TAG(check->object)->type ==
                                        OBJ_TYPE_TAG(cand->object)->type) {
                                        goto break1;
                                    }
                                    break;
                                case OT_ENUMCONST:
                                    if (OBJ_ENUM_CONST(check->object)->type ==
                                        OBJ_ENUM_CONST(cand->object)->type) {
                                        goto break1;
                                    }
                                    break;
                                case OT_OBJECT:
                                    if (OBJECT(check->object)->datatype == DALIAS) {
                                        if (OBJECT(cand->object)->datatype == DALIAS) {
                                            if (OBJECT(check->object)->u.alias.object ==
                                                OBJECT(cand->object)->u.alias.object) {
                                                goto break1;
                                            }
                                        } else {
                                            if (OBJECT(check->object)->u.alias.object ==
                                                OBJECT(cand->object)) {
                                                goto break1;
                                            }
                                        }
                                    } else {
                                        if ((OBJECT(cand->object)->datatype == DALIAS) &&
                                            OBJECT(check->object) ==
                                                OBJECT(cand->object)->u.alias.object) {
                                            goto break1;
                                        }
                                    }
                                    break;
                            }
                        }
                    }
break1:
                    if (!check) {
                        if (!copied) {
                            result = CScope_CopyNameSpaceObjectList(result);
                            copied = TRUE;
                        }
                        if (CScope_ObjectIsFunction(OBJECT(cand->object))) {
                            for (scanOL = result; scanOL; scanOL = scanOL->next) {
                                if (!CScope_ObjectIsFunction(OBJECT(scanOL->object))) {
                                    if (scanOL->object->otype != OT_TYPETAG) {
                                        CScope_AmbigNameError(resultnspace, scan->nspace, name);
                                        break;
                                    }
                                }
                            }
                        } else if (cand->object->otype == OT_TYPETAG) {
                            for (scanOL = result; scanOL; scanOL = scanOL->next) {
                                if (scanOL->object->otype == OT_TYPETAG) {
                                    CScope_AmbigNameError(resultnspace, scan->nspace, name);
                                    break;
                                }
                            }
                        } else {
                            for (scanOL = result; scanOL; scanOL = scanOL->next) {
                                if (scanOL->object->otype != OT_TYPETAG) {
                                    CScope_AmbigNameError(resultnspace, scan->nspace, name);
                                    break;
                                }
                            }
                        }

                        if (!scanOL) {
                            newlist = lalloc(sizeof(NameSpaceObjectList));
                            newlist->object = cand->object;
                            newlist->next = result;
                            result = newlist;
                        }
                    }
                }
            } else {
                result = found;
                resultnspace = scan->nspace;
            }
        }
    }

    if (foundnspace) {
        *foundnspace = resultnspace;
    }
    return result;
}

static NameSpaceObjectList* CScope_NSIteratorFind(CScopeNSIterator* iterator, HashNameNode* name) {
    NameSpaceObjectList* list;

    if (iterator->lookup) {
        if (iterator->lookup->namespaces) {
            return CScope_FindLookupName(iterator->lookup, name, NULL);
        }
        if (iterator->lookup->nspace->theclass) {
            if (CScope_FindClassMember(
                    iterator->result, iterator->lookup->nspace, name, Lookup_Object
                )) {
                list = iterator->result->objects;
                iterator->result->objects = NULL;
                return list;
            } else {
                return NULL;
            }
        }

        if ((list = CScope_FindName(iterator->lookup->nspace, name))) {
            return list;
        }
    } else {
        if (iterator->nspace->theclass) {
            if (CScope_FindClassMember(iterator->result, iterator->nspace, name, Lookup_Object)) {
                list = iterator->result->objects;
                iterator->result->objects = NULL;
                return list;
            } else {
                return NULL;
            }
        }

        if ((list = CScope_FindName(iterator->nspace, name))) {
            return list;
        }
    }

    return NULL;
}

static NameSpaceObjectList* CScope_NSIteratorNonClassFind(
    CScopeNSIterator* iterator, HashNameNode* name
) {
    NameSpaceObjectList* list;

    if (iterator->lookup) {
        if (iterator->lookup->namespaces) {
            return CScope_FindLookupName(iterator->lookup, name, NULL);
        }
        if (iterator->lookup->nspace->theclass) {
            return NULL;
        }
        if ((list = CScope_FindName(iterator->lookup->nspace, name))) {
            return list;
        }
    } else {
        if (iterator->nspace->theclass) {
            return NULL;
        }
        if ((list = CScope_FindName(iterator->nspace, name))) {
            return list;
        }
    }

    return NULL;
}

static NameSpace* CScope_NSIteratorFindNameSpace(CScopeNSIterator* iterator, HashNameNode* name) {
    NameSpace* nspace;
    NameSpaceObjectList* list;

    if (iterator->lookup) {
        if (iterator->lookup->namespaces) {
            return CScope_FindLookupNameSpace(iterator, iterator->lookup, name);
        }
        if (iterator->lookup->nspace->theclass) {
            if (CScope_FindClassMember(
                    iterator->result, iterator->lookup->nspace, name, Lookup_NameSpace
                )) {
                nspace = iterator->result->nspace;
                iterator->result->nspace = NULL;
                return nspace;
            } else {
                return NULL;
            }
        }

        if ((list = CScope_FindName(iterator->lookup->nspace, name))) {
            return CScope_ExtractNameSpace(list, NULL);
        }
    } else {
        if (iterator->nspace->theclass) {
            if (CScope_FindClassMember(
                    iterator->result, iterator->nspace, name, Lookup_NameSpace
                )) {
                nspace = iterator->result->nspace;
                iterator->result->nspace = NULL;
                return nspace;
            } else {
                return NULL;
            }
        }

        if ((list = CScope_FindName(iterator->nspace, name))) {
            return CScope_ExtractNameSpace(list, NULL);
        }
    }

    return NULL;
}

static Boolean CScope_SetupParseNameResult(
    NameResult* result, NameSpaceObjectList* list, HashNameNode* name
) {
    if (!list->next || list->next->object->otype == OT_TYPETAG) {
        switch (list->object->otype) {
            case OT_NAMESPACE:
                CError_Error(ERR_ILLEGAL_USE_NAMESPACE_NAME);
                return FALSE;
            case OT_TYPE:
                result->type = OBJ_TYPE(list->object)->type;
                result->qual = OBJ_TYPE(list->object)->qual;
                result->object = list->object;
                result->name = name;
                result->is_type = TRUE;
                break;
            case OT_TYPETAG:
                result->type = OBJ_TYPE_TAG(list->object)->type;
                result->qual = 0;
                result->object = list->object;
                result->name = name;
                result->is_type = TRUE;
                break;
            default:
                result->object = list->object;
        }
    } else {
        result->objects = list;
    }

    return TRUE;
}

Boolean CScope_FindQualifiedClassMember(NameResult* result, TypeClass* tclass, HashNameNode* name) {
    NameSpaceObjectList* list;

    memclrw(result, sizeof(NameResult));
    CDecl_CompleteType(TYPE(tclass));

    if (CScope_FindClassMember(result, tclass->nspace, name, Lookup_Object)) {
        list = result->objects;
        CError_ASSERT(1780, list);
        result->objects = NULL;

        if (CScope_SetupParseNameResult(result, list, name) && !result->type) {
            return TRUE;
        } else {
            CError_Error(ERR_ILLEGAL_USE_TEMPLATE_ARGUMENT_DEPENDENT_TYPE, name->name);
        }
    }

    return FALSE;
}

static NameSpace* CScope_FindQualifiedNameSpace(
    NameResult* result, NameSpace* nspace, HashNameNode* name
) {
    NameSpaceObjectList* list;
    NameSpace* cand;
    NameSpace* found;
    NameSpaceList* scan;
    NameSpaceLookupList* lookup;
    Boolean fail = FALSE;

    if (nspace->theclass) {
        if (CScope_FindClassMember(result, nspace, name, Lookup_NameSpace)) {
            nspace = result->nspace;
            result->nspace = NULL;
            return nspace;
        } else {
            return NULL;
        }
    }

    if ((list = CScope_FindQualName(nspace, name)) && CScope_ExtractNameSpace(list, &fail)) {
        return nspace;
    }

    if (!fail && nspace->usings) {
        found = NULL;
        for (lookup = CScope_BuildNameSpaceMemberLookupList(nspace); lookup;
             lookup = lookup->next) {
            for (scan = lookup->namespaces; scan; scan = scan->next) {
                if ((list = CScope_FindQualName(scan->nspace, name)) &&
                    (cand = CScope_ExtractNameSpace(list, &fail))) {
                    if (found && cand != found) {
                        CScope_AmbigNameError(found, cand, NULL);
                    }
                    found = cand;
                }

                if (fail) {
                    return NULL;
                }
            }

            if (found) {
                return found;
            }
        }
    }

    return NULL;
}

static NameSpaceObjectList* CScope_FindQualifiedName(
    NameResult* result, NameSpace* nspace, HashNameNode* name, NameSpace** foundnspace
) {
    NameSpaceObjectList* list;
    NameSpaceLookupList* lookup;

    if (nspace->theclass) {
        CDecl_CompleteType(TYPE(nspace->theclass));
        if (CScope_FindClassMember(result, nspace, name, Lookup_Object)) {
            list = result->objects;
            result->objects = NULL;
            return list;
        } else {
            return NULL;
        }
    }

    if ((list = CScope_FindQualName(nspace, name))) {
        *foundnspace = nspace;
        return list;
    }

    if (nspace->usings) {
        for (lookup = CScope_BuildNameSpaceMemberLookupList(nspace); lookup;
             lookup = lookup->next) {
            if ((list = CScope_FindLookupName(lookup, name, foundnspace))) {
                return list;
            }
        }
    }

    return NULL;
}

static Boolean CScope_FindQualifiedTag(NameResult* result, NameSpace* nspace, HashNameNode* name) {
    NameSpaceObjectList* list;
    NameSpaceLookupList* lookup;
    Type* best;
    NameSpace* bestnspace;
    NameSpaceList* scan;

    if (nspace->theclass) {
        CDecl_CompleteType(TYPE(nspace->theclass));
        if (CScope_FindClassMember(result, nspace, name, Lookup_Tag)) {
            return TRUE;
        }
        return FALSE;
    }

    if ((list = CScope_FindQualName(nspace, name))) {
        for (; list; list = list->next) {
            if (list->object->otype == OT_TYPETAG) {
                result->type = OBJ_TYPE_TAG(list->object)->type;
                return TRUE;
            }
        }
    }

    if (nspace->usings) {
        best = NULL;
        for (lookup = CScope_BuildNameSpaceMemberLookupList(nspace); lookup;
             lookup = lookup->next) {
            for (scan = lookup->namespaces; scan; scan = scan->next) {
                for (list = CScope_FindQualName(scan->nspace, name); list; list = list->next) {
                    if (list->object->otype == OT_TYPETAG) {
                        if (best && best != OBJ_TYPE_TAG(list->object)->type) {
                            CScope_AmbigNameError(scan->nspace, bestnspace, name);
                        }
                        best = OBJ_TYPE_TAG(list->object)->type;
                        bestnspace = scan->nspace;
                        break;
                    }
                }
            }

            if (best) {
                result->type = best;
                return TRUE;
            }
        }
    }

    return FALSE;
}

static void CScope_NSIteratorInit(
    CScopeNSIterator* iterator, NameSpace* nspace, NameResult* result
) {
    if (nspace->usings) {
        iterator->nspace = NULL;
        iterator->lookup = CScope_BuildNameSpaceLookupList(nspace);
    } else {
        iterator->nspace = nspace;
        iterator->lookup = NULL;
    }
    iterator->result = result;
}

static Boolean CScope_NSIteratorNext(CScopeNSIterator* iterator) {
    if (iterator->lookup) {
        return (iterator->lookup = iterator->lookup->next) != NULL;
    }
    if (iterator->result->is_qualified) {
        return FALSE;
    }

    if ((iterator->nspace = iterator->nspace->parent)) {
        if (iterator->nspace->usings && !iterator->result->is_qualified) {
            iterator->lookup = CScope_BuildNameSpaceLookupList(iterator->nspace);
            iterator->nspace = NULL;
        }
        return TRUE;
    } else {
        return FALSE;
    }
}

Type* CScope_GetType(NameSpace* nspace, HashNameNode* name, UInt32* qual) {
    CScopeNSIterator iterator;
    NameResult result;
    NameSpaceObjectList* list;

    memclrw(&result, sizeof(result));
    CScope_NSIteratorInit(&iterator, nspace, &result);

    do {
        for (list = CScope_NSIteratorFind(&iterator, name); list; list = list->next) {
            if (list->object->otype == OT_TYPETAG) {
                if (qual) {
                    *qual = 0;
                }
                return OBJ_TYPE_TAG(list->object)->type;
            }
            if (list->object->otype == OT_TYPE) {
                if (qual) {
                    *qual = OBJ_TYPE(list->object)->qual;
                }
                return OBJ_TYPE(list->object)->type;
            }
        }
    } while (CScope_NSIteratorNext(&iterator));

    return NULL;
}

Type* CScope_GetTagType(NameSpace* nspace, HashNameNode* name) {
    CScopeNSIterator iterator;
    NameResult result;
    NameSpaceObjectList* list;

    memclrw(&result, sizeof(result));
    CScope_NSIteratorInit(&iterator, nspace, &result);

    do {
        for (list = CScope_NSIteratorFind(&iterator, name); list; list = list->next) {
            if (list->object->otype == OT_TYPETAG) {
                return OBJ_TYPE_TAG(list->object)->type;
            }
        }
    } while (CScope_NSIteratorNext(&iterator));

    return NULL;
}

static Boolean CScope_DependentTemplateMember(
    NameResult* result, TypeTemplDep* ttempldep, Boolean allowOperator, Boolean noTemplateArgs
) {
    SInt32 streamstate;
    short token;
    TypeTemplDep* newtype;
    TypeTemplDep* newtype2;

    CPrep_TokenStreamGetState(&streamstate);
    token = lex();
    CError_ASSERT(2026, token == TK_COLON_COLON);

    while (1) {
        token = lex();
        if ((int)token == TK_OPERATOR && allowOperator) {
            if (!CParser_ParseOperatorName(NULL, TRUE, TRUE)) {
                return FALSE;
            }
            newtype = TYPE_TEMPLATE(CDecl_NewTemplDepType(TEMPLDEP_QUALNAME));
            newtype->u.qual.type = ttempldep;
            newtype->u.qual.name = tkidentifier;
            CPrep_TokenStreamSetState(&streamstate);
            lex();
            tk = lex();
            result->type = TYPE(newtype);
            return TRUE;
        }

        if ((int)token == TK_TEMPLATE) {
            if (lex() != TK_IDENTIFIER) {
                CError_Error(ERR_IDENTIFIER_EXPECTED);
                return FALSE;
            }

            CPrep_TokenStreamGetState(&streamstate);
            newtype = TYPE_TEMPLATE(CDecl_NewTemplDepType(TEMPLDEP_QUALNAME));
            newtype->u.qual.type = ttempldep;
            newtype->u.qual.name = tkidentifier;
            if ((token = lex()) != '<') {
                CError_Error(ERR_LESS_EXPECTED);
                return FALSE;
            }
            tk = token;
            ttempldep = newtype;
            newtype = TYPE_TEMPLATE(CDecl_NewTemplDepType(TEMPLDEP_QUALTEMPL));
            newtype->u.qualtempl.type = ttempldep;
            newtype->u.qualtempl.args = CTempl_ParseUncheckTemplArgs(NULL, TRUE);
            CPrep_TokenStreamGetState(&streamstate);
            if (lex() == TK_COLON_COLON) {
                ttempldep = newtype;
                continue;
            } else {
                CPrep_TokenStreamSetState(&streamstate);
                result->type = TYPE(newtype);
                return TRUE;
            }
        }

        if (token == TK_IDENTIFIER) {
            newtype = TYPE_TEMPLATE(CDecl_NewTemplDepType(TEMPLDEP_QUALNAME));
            newtype->u.qual.type = ttempldep;
            newtype->u.qual.name = tkidentifier;
            tk = token;

            CPrep_TokenStreamGetState(&streamstate);
            token = lex();
            tkidentifier = newtype->u.qual.name;
            if (token == TK_COLON_COLON) {
                ttempldep = newtype;
                continue;
            }

            if ((token == '<') && !noTemplateArgs) {
                tk = token;
                newtype2 = newtype;
                newtype = TYPE_TEMPLATE(CDecl_NewTemplDepType(TEMPLDEP_QUALTEMPL));
                newtype->u.qualtempl.type = newtype2;
                newtype->u.qualtempl.args = CTempl_ParseUncheckTemplArgs(NULL, TRUE);

                CPrep_TokenStreamGetState(&streamstate);
                if (lex() == TK_COLON_COLON) {
                    ttempldep = newtype;
                    continue;
                }
            }

            CPrep_TokenStreamSetState(&streamstate);
            result->type = TYPE(newtype);
            return TRUE;
        }

        CPrep_TokenStreamSetState(&streamstate);
        result->type = TYPE(ttempldep);
        return TRUE;
    }
}

static Boolean CScope_CheckDtorName(TypeClass* tclass, Boolean* noDtor) {
    if ((tk = lex()) != TK_IDENTIFIER) {
        CError_Error(ERR_IDENTIFIER_EXPECTED);
    }

    if (tclass) {
        *noDtor = FALSE;
        if ((tclass->classname == tkidentifier) ||
            (CScope_GetType(tclass->nspace, tkidentifier, NULL) == TYPE(tclass)) ||
            (CScope_GetType(cscope_current, tkidentifier, NULL) == TYPE(tclass))) {
            if (!CClass_Destructor(tclass)) {
                *noDtor = TRUE;
            }
            if ((tclass->flags & CLASS_IS_TEMPL_INST) && (lookahead() == '<')) {
                tk = lex();
                if (!CTemplTool_EqualArgs(
                        TEMPL_CLASS_INST(tclass)->inst_args,
                        CTempl_ParseUncheckTemplArgs(NULL, FALSE)
                    )) {
                    CError_Error(ERR_TEMPLATE_PARAMETER_ARGUMENT_LIST_MISMATCH);
                }
            }
            return TRUE;
        }
    }

    CError_Error(ERR_EXPRESSION_SYNTAX_ERROR);
    return FALSE;
}

static Boolean CScope_ParseQualifiedName(NameResult* result, NameSpace* nspace) {
    NameSpaceObjectList* list;
    HashNameNode* saveidentifier;
    TypeClass* tclass;
    Boolean isDtor = FALSE;

    do {
        switch (tk) {
            case TK_TEMPLATE:
                if ((tk = lex()) != TK_IDENTIFIER) {
                    CError_Error(ERR_IDENTIFIER_EXPECTED);
                    return FALSE;
                }
            case TK_IDENTIFIER:
                saveidentifier = tkidentifier;
                if (lookahead() == TK_COLON_COLON) {
                    tk = lex();
                    nspace = CScope_FindQualifiedNameSpace(result, nspace, saveidentifier);
                    if (!nspace) {
                        return FALSE;
                    }
                    result->is_qualified = TRUE;
                    tk = lex();
                    continue;
                }
                break;
            case TK_OPERATOR:
                if (!CParser_ParseOperatorName(NULL, TRUE, TRUE)) {
                    return FALSE;
                }
                CPrep_UnLex();
                saveidentifier = tkidentifier;
                break;
            case '~':
                if (!CScope_CheckDtorName(nspace->theclass, &isDtor)) {
                    return FALSE;
                }
                saveidentifier = destructor_name_node;
                isDtor = TRUE;
                break;
            default:
                CError_Error(ERR_IDENTIFIER_EXPECTED);
                return FALSE;
        }

        if (!(list = CScope_FindQualifiedName(result, nspace, saveidentifier, &result->nspace)) ||
            !CScope_SetupParseNameResult(result, list, saveidentifier)) {
            if (isDtor) {
                result->is_destructor = TRUE;
                return TRUE;
            }
            if (nspace->theclass && !(nspace->theclass->flags & CLASS_COMPLETED)) {
                CError_Error(ERR_ILLEGAL_USE_INCOMPLETE_STRUCT_UNION_CLASS, nspace->theclass, 0);
            } else {
                CError_Error(ERR_UNDEFINED_IDENTIFIER, saveidentifier->name);
            }
            return FALSE;
        }

        if (result->type && IS_TYPE_CLASS(result->type) && (lookahead() == '<')) {
            tclass = TYPE_CLASS(result->type);
            if (tclass->flags & CLASS_IS_TEMPL_INST) {
                tclass = TYPE_CLASS(TEMPL_CLASS_INST(tclass)->templ);
            } else if (!(tclass->flags & CLASS_IS_TEMPL)) {
                return TRUE;
            }

            tk = lex();
            result->type = CTempl_ClassGetType(TEMPL_CLASS(tclass));
            if (IS_TYPE_CLASS(result->type) && (lookahead() == TK_COLON_COLON)) {
                lex();
                tk = lex();
                result->is_qualified = TRUE;
                nspace = TYPE_CLASS(result->type)->nspace;
                result->type = NULL;
                result->object = NULL;
                continue;
            }
        }

        return TRUE;
    } while (1);
}

Boolean CScope_ParseExprName(NameResult* result) {
    CScopeNSIterator iterator;
    HashNameNode* name;
    NameSpaceObjectList* list;
    Boolean noDtor;

    if (!copts.cplusplus) {
        memclrw(result, sizeof(NameResult));
        if (tk != TK_IDENTIFIER) {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
            return FALSE;
        }

        name = tkidentifier;
        CScope_NSIteratorInit(&iterator, cscope_current, result);
        do {
            if ((list = CScope_NSIteratorFind(&iterator, name)) &&
                (list->object->otype != OT_TYPETAG)) {
                result->nspace = iterator.lookup ? iterator.lookup->nspace : iterator.nspace;
                return CScope_SetupParseNameResult(result, list, name);
            }
        } while (CScope_NSIteratorNext(&iterator));

        result->nspace = cscope_current;
        result->name = name;
        return TRUE;
    } else {
        if ((tk == TK_COLON_COLON || tk == TK_IDENTIFIER) &&
            CScope_ParseQualifiedNameSpace(result, TRUE, TRUE)) {
            if (result->type) {
                return TRUE;
            }
            CError_ASSERT(2313, result->nspace);
        } else {
            memclrw(result, sizeof(NameResult));
            result->nspace = cscope_current;
        }

        switch (tk) {
            case TK_TEMPLATE:
                if (!result->is_qualified) {
                    CError_Error(ERR_ILLEGAL_TEMPLATE_PREFIX);
                }

                if ((tk = lex()) != TK_IDENTIFIER) {
                    CError_Error(ERR_IDENTIFIER_EXPECTED);
                    return FALSE;
                }
            case TK_IDENTIFIER:
                name = tkidentifier;
                break;
            case TK_OPERATOR:
                if (!CParser_ParseOperatorName(NULL, TRUE, TRUE)) {
                    return FALSE;
                }
                name = tkidentifier;
                CPrep_UnLex();
                break;
            case '~':
                if (!CScope_CheckDtorName(result->nspace->theclass, &noDtor)) {
                    return FALSE;
                }
                if (noDtor) {
                    result->is_destructor = TRUE;
                    return TRUE;
                }
                name = destructor_name_node;
                break;
            default:
                CError_Error(ERR_IDENTIFIER_EXPECTED);
                return FALSE;
        }

        if (result->is_qualified) {
            if (!(list = CScope_FindQualifiedName(result, result->nspace, name, &result->nspace))) {
                CError_Error(ERR_UNDEFINED_IDENTIFIER, CError_GetNameString(result->nspace, name));
                return FALSE;
            } else {
                return CScope_SetupParseNameResult(result, list, name);
            }
        }

        CScope_NSIteratorInit(&iterator, result->nspace, result);
        do {
            if ((list = CScope_NSIteratorFind(&iterator, name))) {
                result->nspace = iterator.lookup ? iterator.lookup->nspace : iterator.nspace;
                return CScope_SetupParseNameResult(result, list, name);
            }
        } while (CScope_NSIteratorNext(&iterator));

        if (result->is_qualified) {
            CError_Error(ERR_UNDEFINED_IDENTIFIER, name->name);
            return FALSE;
        } else {
            result->nspace = cscope_current;
            result->name = name;
            return TRUE;
        }
    }
}

Boolean CScope_ParseDeclName(NameResult* result) {
    CScopeNSIterator iterator;
    HashNameNode* name;
    NameSpaceObjectList* list;
    NameSpace* nspace;
    CScopeSave save;
    short op;

    if (!copts.cplusplus) {
non_cpp_mode:
        if (tk != TK_IDENTIFIER) {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
            return FALSE;
        }
        memclrw(result, sizeof(NameResult));

        name = tkidentifier;
        CScope_NSIteratorInit(&iterator, cscope_current, result);
        do {
            if ((list = CScope_NSIteratorFind(&iterator, name)) &&
                (copts.cplusplus || (list->object->otype != OT_TYPETAG))) {
                result->nspace = iterator.lookup ? iterator.lookup->nspace : iterator.nspace;
                return CScope_SetupParseNameResult(result, list, name);
            }
        } while (CScope_NSIteratorNext(&iterator));

        result->nspace = cscope_current;
        result->name = name;
        return FALSE;
    } else {
        if ((tk != TK_COLON_COLON) && (tk != TK_IDENTIFIER)) {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
            return FALSE;
        }

        if (!CScope_ParseQualifiedNameSpace(result, FALSE, FALSE)) {
            goto non_cpp_mode;
        }

        if (result->type) {
            return TRUE;
        }

        nspace = result->nspace;
        CError_ASSERT(2435, nspace);

        switch (tk) {
            case TK_OPERATOR:
                CScope_SetNameSpaceScope(nspace, &save);
                if (!CParser_ParseOperatorName(&op, TRUE, TRUE)) {
                    CScope_RestoreScope(&save);
                    return FALSE;
                }
                CScope_RestoreScope(&save);
                if (op) {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                }
                result->is_conv_operator = TRUE;
                return TRUE;
            case TK_IDENTIFIER:
                name = tkidentifier;
                if (nspace->theclass && nspace->theclass->classname == name) {
                    name = constructor_name_node;
                }
                break;
            case '~':
                if (!nspace->theclass) {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                    return FALSE;
                }
                tk = lex();
                if (tk != TK_IDENTIFIER) {
                    CError_Error(ERR_IDENTIFIER_EXPECTED);
                    return FALSE;
                }
                if (nspace->theclass->classname != tkidentifier) {
                    CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                }
                name = destructor_name_node;
                break;
            default:
                CError_Error(ERR_IDENTIFIER_EXPECTED);
                return FALSE;
        }

        if (result->is_qualified) {
            if (!(list = CScope_FindQualifiedName(result, result->nspace, name, &result->nspace))) {
                CError_Error(ERR_UNDEFINED_IDENTIFIER, name->name);
                return FALSE;
            } else {
                return CScope_SetupParseNameResult(result, list, name);
            }
        }

        CScope_NSIteratorInit(&iterator, nspace, result);
        do {
            if ((list = CScope_NSIteratorFind(&iterator, name))) {
                result->nspace = iterator.lookup ? iterator.lookup->nspace : iterator.nspace;
                return CScope_SetupParseNameResult(result, list, name);
            }
        } while (CScope_NSIteratorNext(&iterator));

        CError_Error(ERR_UNDEFINED_IDENTIFIER, name->name);
        return FALSE;
    }
}

Boolean CScope_ParseQualifiedNameSpace(
    NameResult* result, Boolean allowOperator, Boolean noTemplateArgs
) {
    HashNameNode* name;
    NameSpace* nspace;
    short t;
    Type* type;
    Type* type2;
    NameSpaceObjectList* list;
    CScopeNSIterator iterator;

    memclrw(result, sizeof(NameResult));
    nspace = NULL;
    if (tk == TK_COLON_COLON) {
        result->nspace = nspace = cscope_root;
        result->is_qualified = TRUE;
        tk = lex();
    }

restart:
    if (tk != TK_IDENTIFIER) {
        if (tk != TK_TEMPLATE) {
            return nspace != NULL;
        }
        if (!result->is_qualified) {
            CError_Error(ERR_ILLEGAL_TEMPLATE_PREFIX);
        }
        if ((tk = lex()) != TK_IDENTIFIER) {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
            return FALSE;
        }
    }

    name = tkidentifier;
    t = lookahead();
    tkidentifier = name;

    if (t != TK_COLON_COLON && t != '<') {
        return nspace != NULL;
    }

    CScope_NSIteratorInit(&iterator, nspace ? nspace : cscope_current, result);
    do {
        for (list = CScope_NSIteratorFind(&iterator, name); list; list = list->next) {
            if (list->object->otype == OT_NAMESPACE) {
                CError_ASSERT(2565, !nspace || !nspace->theclass);

                result->nspace = nspace = OBJ_NAMESPACE(list->object)->nspace;
                if ((tk = lex()) != TK_COLON_COLON) {
                    CError_Error(ERR_ILLEGAL_USE_NAMESPACE_NAME);
                    return FALSE;
                }

                result->is_qualified = TRUE;
                tk = lex();
                goto restart;
            }
            if (list->object->otype == OT_TYPETAG) {
                type = OBJ_TYPE_TAG(list->object)->type;
                if (type->type != TYPECLASS) {
                    if (t == '<') {
                        result->type = type;
                        return TRUE;
                    } else {
                        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                        return FALSE;
                    }
                }
parse_thing:
                if (t == '<') {
                    if (TYPE_CLASS(type)->flags & CLASS_IS_TEMPL_INST) {
                        type = TYPE(TEMPL_CLASS_INST(type)->templ);
                    } else if (!(TYPE_CLASS(type)->flags & CLASS_IS_TEMPL)) {
                        result->type = type;
                        return TRUE;
                    }
                }
                if ((tk = lex()) == '<') {
                    CError_ASSERT(2609, TYPE_CLASS(type)->flags & CLASS_IS_TEMPL);
                    type2 = CTempl_ClassGetType(TEMPL_CLASS(type));
                    if (IS_TYPE_TEMPLATE(type2)) {
                        if (lookahead() != TK_COLON_COLON) {
                            result->type = type2;
                            return TRUE;
                        }
                        return CScope_DependentTemplateMember(
                            result, TYPE_TEMPLATE(type2), allowOperator, noTemplateArgs
                        );
                    } else if (type2->type != TYPECLASS) {
                        return FALSE;
                    }

                    result->nspace = nspace = TYPE_CLASS(type2)->nspace;
                    if (lookahead() != TK_COLON_COLON) {
                        result->type = type2;
                        return TRUE;
                    }
                    tk = lex();
                    CDecl_CompleteType(type2);
                } else {
                    CError_ASSERT(2632, tk == TK_COLON_COLON);
                    if (!(TYPE_CLASS(type)->flags & CLASS_IS_TEMPL) ||
                        CParser_CheckTemplateClassUsage(TEMPL_CLASS(type), TRUE)) {
                        result->nspace = nspace = TYPE_CLASS(type)->nspace;
                    }
                    CDecl_CompleteType(type);
                }

                result->is_qualified = TRUE;
                tk = lex();
                goto restart;
            }
            if (list->object->otype == OT_TYPE) {
                type2 = OBJ_TYPE(list->object)->type;
                if (!IS_TYPE_CLASS(type2)) {
                    if (IS_TYPE_TEMPLATE(type2)) {
                        if (TYPE_TEMPLATE(type2)->dtype == TEMPLDEP_ARGUMENT &&
                            TYPE_TEMPLATE(type2)->u.pid.type == TPT_TEMPLATE) {
                            type2 = CTempl_ParseTemplTemplParam(TYPE_TEMPLATE(type2));
                            if ((type2->type != TYPETEMPLATE) ||
                                ((t = lookahead()) != TK_COLON_COLON)) {
                                result->type = type2;
                                return TRUE;
                            }
                        }
                        if (t == TK_COLON_COLON) {
                            return CScope_DependentTemplateMember(
                                result, TYPE_TEMPLATE(type2), allowOperator, noTemplateArgs
                            );
                        }
                    }
                    if (t == '<') {
                        result->type = type2;
                        return TRUE;
                    } else {
                        CError_Error(ERR_DECLARATION_SYNTAX_ERROR);
                        return FALSE;
                    }
                } else {
                    if ((t == '<') && (TYPE_CLASS(type2)->flags & CLASS_IS_TEMPL)) {
                        type = type2;
                        goto parse_thing;
                    }
                    if ((tk = lex()) == '<') {
                        result->type = type2;
                        return TRUE;
                    }
                    CError_ASSERT(2686, tk == TK_COLON_COLON);
                    if (!type2->size) {
                        CDecl_CompleteType(type2);
                    }
                    result->nspace = nspace = TYPE_CLASS(type2)->nspace;
                    result->is_qualified = TRUE;
                    tk = lex();
                    goto restart;
                }
            }
            if (t == '<') {
                return nspace != NULL;
            }
        }
    } while (CScope_NSIteratorNext(&iterator));

    CError_Error(ERR_UNDEFINED_IDENTIFIER, name->name);
    return FALSE;
}

Boolean CScope_ParseElaborateName(NameResult* result) {
    CScopeNSIterator iterator;
    HashNameNode* name;
    NameSpaceObjectList* list;

    if (!copts.cplusplus) {
        memclrw(result, sizeof(NameResult));
        if (tk != TK_IDENTIFIER) {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
            return FALSE;
        }

        name = tkidentifier;
        CScope_NSIteratorInit(&iterator, cscope_current, result);
        do {
            for (list = CScope_NSIteratorFind(&iterator, name); list; list = list->next) {
                if (list->object->otype == OT_TYPETAG) {
                    result->nspace = iterator.lookup ? iterator.lookup->nspace : iterator.nspace;
                    return CScope_SetupParseNameResult(result, list, name);
                }
            }
        } while (CScope_NSIteratorNext(&iterator));

        result->name = name;
        return TRUE;
    } else {
        if ((tk != TK_COLON_COLON) && (tk != TK_IDENTIFIER)) {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
            return FALSE;
        }

        if (!CScope_ParseQualifiedNameSpace(result, FALSE, FALSE)) {
            result->nspace = cscope_current;
            if (tk != TK_IDENTIFIER) {
                CError_Error(ERR_IDENTIFIER_EXPECTED);
                return FALSE;
            }
            name = tkidentifier;
        } else {
            if (result->type) {
                return TRUE;
            }
            CError_ASSERT(2760, result->nspace);

            if (tk != TK_IDENTIFIER) {
                CError_Error(ERR_IDENTIFIER_EXPECTED);
                return FALSE;
            }
            name = tkidentifier;

            if (result->is_qualified) {
                if (result->nspace->theclass) {
                    if (CScope_FindClassMember(result, result->nspace, name, Lookup_Tag)) {
                        return TRUE;
                    }
                    return FALSE;
                } else {
                    return CScope_FindQualifiedTag(result, result->nspace, name);
                }
            }
        }

        CScope_NSIteratorInit(&iterator, result->nspace, result);
        do {
            for (list = CScope_NSIteratorFind(&iterator, name); list; list = list->next) {
                if (list->object->otype == OT_TYPETAG || list->object->otype == OT_TYPE) {
                    result->nspace = iterator.lookup ? iterator.lookup->nspace : iterator.nspace;
                    return CScope_SetupParseNameResult(result, list, name);
                }
            }
        } while (CScope_NSIteratorNext(&iterator));

        result->name = name;
        return TRUE;
    }
}

Boolean CScope_FindObject(NameSpace* nspace, NameResult* result, HashNameNode* name) {
    CScopeNSIterator iterator;
    NameSpaceObjectList* list;

    memclrw(result, sizeof(NameResult));
    CScope_NSIteratorInit(&iterator, nspace, result);
    do {
        for (list = CScope_NSIteratorFind(&iterator, name); list; list = list->next) {
            if (copts.cplusplus || list->object->otype != OT_TYPETAG) {
                result->nspace = iterator.lookup ? iterator.lookup->nspace : iterator.nspace;
                return CScope_SetupParseNameResult(result, list, name);
            }
        }
    } while (CScope_NSIteratorNext(&iterator));

    return FALSE;
}

Boolean CScope_FindNonClassObject(NameSpace* nspace, NameResult* result, HashNameNode* name) {
    CScopeNSIterator iterator;
    NameSpaceObjectList* list;

    memclrw(result, sizeof(NameResult));
    CScope_NSIteratorInit(&iterator, nspace, result);
    do {
        for (list = CScope_NSIteratorNonClassFind(&iterator, name); list; list = list->next) {
            if (copts.cplusplus || list->object->otype != OT_TYPETAG) {
                result->nspace = iterator.lookup ? iterator.lookup->nspace : iterator.nspace;
                return CScope_SetupParseNameResult(result, list, name);
            }
        }
    } while (CScope_NSIteratorNext(&iterator));

    return FALSE;
}

NameSpaceObjectList* CScope_FindObjectList(NameResult* result, HashNameNode* name) {
    CScopeNSIterator iterator;
    NameSpaceObjectList* list;

    memclrw(result, sizeof(NameResult));
    CScope_NSIteratorInit(&iterator, cscope_current, result);
    do {
        for (list = CScope_NSIteratorFind(&iterator, name); list; list = list->next) {
            if (copts.cplusplus || list->object->otype != OT_TYPETAG) {
                result->nspace = iterator.lookup ? iterator.lookup->nspace : iterator.nspace;
                return list;
            }
        }
    } while (CScope_NSIteratorNext(&iterator));

    return NULL;
}

Boolean CScope_PossibleTypeName(HashNameNode* name) {
    CScopeNSIterator iterator;
    NameResult result;
    NameSpaceObjectList* list;

    memclrw(&result, sizeof(NameResult));
    CScope_NSIteratorInit(&iterator, cscope_current, &result);
    do {
        if ((list = CScope_NSIteratorFind(&iterator, name))) {
            switch (list->object->otype) {
                case OT_TYPE:
                case OT_NAMESPACE:
                    return TRUE;
                case OT_TYPETAG:
                    if (copts.cplusplus) {
                        return TRUE;
                    }
                    break;
                default:
                    return FALSE;
            }
        }
    } while (CScope_NSIteratorNext(&iterator));

    return FALSE;
}

Boolean CScope_FindClassMemberObject(TypeClass* tclass, NameResult* result, HashNameNode* name) {
    NameSpaceObjectList* list;

    memclrw(result, sizeof(NameResult));

    if (CScope_FindClassMember(result, tclass->nspace, name, Lookup_Object)) {
        list = result->objects;
        result->objects = NULL;
        if (list && list->object->otype == OT_OBJECT) {
            return CScope_SetupParseNameResult(result, list, name);
        }
    }

    return FALSE;
}

void CScope_InitObjectIterator(CScopeObjectIterator* iterator, NameSpace* nspace) {
    memclrw(iterator, sizeof(CScopeObjectIterator));
    iterator->nspace = nspace;
    if (!iterator->nspace->is_hash) {
        iterator->nextname = nspace->data.list;
    } else {
        iterator->nextname = nspace->data.hash[0];
    }
}

ObjBase* CScope_NextObjectIteratorObject(CScopeObjectIterator* iterator) {
    ObjBase* obj;

    do {
        if (iterator->currlist) {
            do {
                obj = iterator->currlist->object;
                if (obj->otype == OT_OBJECT) {
                    iterator->currlist = iterator->currlist->next;
                    return obj;
                }
            } while ((iterator->currlist = iterator->currlist->next));
        }

        if (iterator->nextname) {
            iterator->currlist = &iterator->nextname->first;
            iterator->nextname = iterator->nextname->next;
        } else {
            if (!iterator->nspace->is_hash || ++iterator->hashindex >= 1024) {
                return NULL;
            }
            iterator->nextname = iterator->nspace->data.hash[iterator->hashindex];
        }
    } while (1);
}

NameSpaceObjectList* CScope_NextObjectIteratorObjectList(CScopeObjectIterator* iterator) {
    NameSpaceName* nsname;

    do {
        if ((nsname = iterator->nextname)) {
            iterator->nextname = nsname->next;
            return &nsname->first;
        } else {
            if (!iterator->nspace->is_hash || ++iterator->hashindex >= 1024) {
                return NULL;
            }
            iterator->nextname = iterator->nspace->data.hash[iterator->hashindex];
        }
    } while (1);
}

void CScope_DefineTypeTag(NameSpace* nspace, HashNameNode* name, Type* type) {
    ObjTypeTag* ott;

    ott = galloc(sizeof(ObjTypeTag));
    memclrw(ott, sizeof(ObjTypeTag));
    ott->otype = OT_TYPETAG;
    ott->access = nspace->theclass ? global_access : ACCESSPUBLIC;
    ott->type = type;
    CScope_AddObject(nspace, name, OBJ_BASE(ott));
}

Type* CScope_GetLocalTagType(NameSpace* nspace, HashNameNode* name) {
    NameSpaceObjectList* list;

    for (list = CScope_FindQualName(nspace, name); list; list = list->next) {
        if (list->object->otype == OT_TYPETAG) {
            return OBJ_TYPE_TAG(list->object)->type;
        }
    }

    return NULL;
}

Boolean CScope_FindTypeName(NameSpace* nspace, HashNameNode* name, NameResult* result) {
    CScopeNSIterator iterator;
    NameSpaceObjectList* list;
    NameSpaceObjectList* scan;

    memclrw(result, sizeof(NameResult));
    CScope_NSIteratorInit(&iterator, nspace, result);

    do {
        list = CScope_NSIteratorFind(&iterator, name);
        for (scan = list; scan; scan = scan->next) {
            switch (scan->object->otype) {
                case OT_NAMESPACE:
                    result->nspace = OBJ_NAMESPACE(scan->object)->nspace;
                    return TRUE;
                case OT_TYPE:
                case OT_TYPETAG:
                    return CScope_SetupParseNameResult(result, scan, name);
                default:
                    return FALSE;
            }
        }
    } while (CScope_NSIteratorNext(&iterator));

    return FALSE;
}

static NameSpaceObjectList* CScope_GetLocalObjects(NameSpaceObjectList* objs) {
    NameSpaceObjectList* list;
    NameSpaceObjectList** ptr;

    list = objs;
    while (list) {
        if (list->object->otype == OT_OBJECT && OBJECT(list->object)->datatype == DALIAS) {
            list = CScope_CopyNameSpaceObjectList(objs);
            ptr = &list;
            while (*ptr) {
                if ((*ptr)->object->otype == OT_OBJECT &&
                    OBJECT((*ptr)->object)->datatype == DALIAS) {
                    *ptr = (*ptr)->next;
                } else {
                    ptr = &(*ptr)->next;
                }
            }
            return list;
        } else {
            list = list->next;
        }
    }

    return objs;
}

NameSpaceObjectList* CScope_GetLocalObject(NameSpace* nspace, HashNameNode* name) {
    NameSpaceObjectList* list;

    if ((list = CScope_FindQualName(nspace, name))) {
        switch (list->object->otype) {
            case OT_OBJECT:
                return CScope_GetLocalObjects(list);
            default:
                CError_Error(ERR_IDENTIFIER_REDECLARED, name->name);
                return NULL;
            case OT_TYPETAG:
                break;
        }
    }

    return NULL;
}

static BClassList* CScope_GetBaseAccessPathEnd(BClassList* list) {
    BClassList* best = list;
    ClassList* scan;

    do {
        if (!list->next) {
            return best;
        }

        for (scan = TYPE_CLASS(list->type)->bases; scan; scan = scan->next) {
            if (TYPE_CLASS(list->next->type) == scan->base) {
                break;
            }
        }

        if (!scan) {
            best = list->next;
        }
        list = list->next;
    } while (1);
}

BClassList* CScope_GetClassAccessPath(BClassList* list, TypeClass* tclass) {
    BClassList* scan;
    BClassList* path;

    if (!list) {
        return NULL;
    }

    list = CScope_GetBaseAccessPathEnd(list);
    for (scan = list; scan; scan = scan->next) {
        if (scan->type == TYPE(tclass)) {
            return scan;
        }
    }

    if ((path = CScope_GetAccessPath(tclass, TYPE_CLASS(list->type)))) {
        for (scan = path; scan; scan = scan->next) {
            if (scan->type == list->type) {
                scan->next = list->next;
                return path;
            }
        }
        CError_FATAL(3217);
    }

    return NULL;
}

static Boolean CScope_FixMemberResult(TypeClass* tclass, NameResult* result) {
    if (!(result->basePath = CScope_GetClassAccessPath(result->basePath, tclass))) {
        if (result->name) {
            CError_Error(ERR_NOT_STRUCT_UNION_CLASS_MEMBER, result->name->name);
        } else {
            CError_Error(ERR_ILLEGAL_CLASS_MEMBER_ACCESS);
        }
        return FALSE;
    } else {
        return TRUE;
    }
}

Boolean CScope_ParseMemberName(TypeClass* tclass, NameResult* result, Boolean isTemplate) {
    HashNameNode* name;
    NameSpaceObjectList* list;
    short t;
    Boolean noDtor;

    if (tk == TK_COLON_COLON) {
restart:
        if (!CScope_ParseExprName(result)) {
            return FALSE;
        }

        if (result->type &&
            IS_TYPE_TEMPLATE(result->type) &&
            TYPE_TEMPLATE(result->type)->dtype == TEMPLDEP_QUALNAME) {
            if (isTemplate) {
                return TRUE;
            }
            CError_Error(
                ERR_ILLEGAL_USE_TEMPLATE_ARGUMENT_DEPENDENT_TYPE,
                TYPE_TEMPLATE(result->type)->u.qual.name->name
            );
            result->type = NULL;
            return FALSE;
        }

        if (result->is_destructor) {
            return TRUE;
        } else {
            return CScope_FixMemberResult(tclass, result);
        }
    }

    if (tk == TK_IDENTIFIER) {
        name = tkidentifier;
        t = lookahead();
        tkidentifier = name;
        switch (t) {
            case TK_COLON_COLON:
                memclrw(result, sizeof(NameResult));
                if (!CScope_FindClassMember(result, tclass->nspace, name, Lookup_NameSpace)) {
                    goto restart;
                }
                break;
            case '<':
                if (isTemplate) {
                    goto restart;
                }
                break;
        }
    } else if (tk == '~') {
        memclrw(result, sizeof(NameResult));
        if (CScope_CheckDtorName(tclass, &noDtor)) {
            if (noDtor) {
                result->is_destructor = TRUE;
                return TRUE;
            }

            if (!(list = CScope_FindQualifiedName(
                      result, tclass->nspace, destructor_name_node, &result->nspace
                  ))) {
                CError_Error(
                    ERR_UNDEFINED_IDENTIFIER,
                    CError_GetNameString(result->nspace, destructor_name_node)
                );
                return FALSE;
            } else {
                return CScope_SetupParseNameResult(result, list, destructor_name_node);
            }
        }
        return FALSE;
    }

    memclrw(result, sizeof(NameResult));
    return CScope_ParseQualifiedName(result, tclass->nspace);
}

static void CScope_AddUsingObject(
    BClassList* bcl, NameSpace* nspace, ObjBase* obj, HashNameNode* name, AccessType access
) {
    NameSpaceObjectList* list;
    TypeClass* tclass;
    ObjBase* copy;
    Object* objscan;

    if (bcl) {
        if (!nspace->theclass) {
            CError_Error(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
        } else {
            CClass_CheckPathAccess(bcl, NULL, obj->access);
        }
    }

    if (obj->otype == OT_TYPE) {
        if (!nspace->theclass) {
            if ((list = CScope_FindQualName(nspace, name)) &&
                (list->object->otype == OT_TYPE) &&
                (OBJ_TYPE(obj)->type == OBJ_TYPE(list->object)->type) &&
                (OBJ_TYPE(obj)->qual == OBJ_TYPE(list->object)->qual)) {
                return;
            }
        }
        copy = galloc(sizeof(ObjType));
        *OBJ_TYPE(copy) = *OBJ_TYPE(obj);
        copy->access = access;
        CScope_AddObject(nspace, name, copy);
        return;
    }

    if (obj->otype == OT_TYPETAG) {
        if (!nspace->theclass) {
            if ((list = CScope_FindQualName(nspace, name)) &&
                (list->object->otype == OT_TYPETAG) &&
                (OBJ_TYPE_TAG(obj)->type == OBJ_TYPE_TAG(list->object)->type)) {
                return;
            }
        }
        copy = galloc(sizeof(ObjTypeTag));
        *OBJ_TYPE_TAG(copy) = *OBJ_TYPE_TAG(obj);
        copy->access = access;
        CScope_AddObject(nspace, name, copy);
        return;
    }

    if (obj->otype == OT_ENUMCONST) {
        copy = galloc(sizeof(ObjEnumConst));
        *OBJ_ENUM_CONST(copy) = *OBJ_ENUM_CONST(obj);
        copy->access = access;
        CScope_AddObject(nspace, OBJ_ENUM_CONST(copy)->name, copy);
        return;
    }

    if (obj->otype == OT_MEMBERVAR) {
        if (nspace->theclass) {
            copy = galloc(sizeof(ObjMemberVarPath));
            *OBJ_MEMBER_VAR(copy) = *OBJ_MEMBER_VAR(obj);
            copy->access = access;
            if ((bcl =
                     CScope_GetClassAccessPath(CClass_GetPathCopy(bcl, TRUE), nspace->theclass)) &&
                bcl->type == TYPE(nspace->theclass)) {
                OBJ_MEMBER_VAR_PATH(copy)->has_path = TRUE;
                OBJ_MEMBER_VAR_PATH(copy)->path = bcl;
            } else {
                CError_Error(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
            }
            CScope_AddObject(nspace, OBJ_MEMBER_VAR(copy)->name, copy);
        } else {
            CError_Error(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
        }
        return;
    }

    if (obj->otype == OT_OBJECT) {
        if (!nspace->theclass) {
            for (list = CScope_FindQualName(nspace, OBJECT(obj)->name); list; list = list->next) {
                if (list->object->otype == OT_OBJECT) {
                    objscan = OBJECT(list->object);
                    while (objscan->datatype == DALIAS) {
                        objscan = objscan->u.alias.object;
                    }
                    if (OBJECT(obj) == objscan) {
                        return;
                    }
                }
            }
        }

        copy = galloc(sizeof(Object));
        *OBJECT(copy) = *OBJECT(obj);
        copy->access = access;
        OBJECT(copy)->datatype = DALIAS;
        OBJECT(copy)->u.alias.object = OBJECT(obj);
        OBJECT(copy)->u.alias.member = NULL;
        OBJECT(copy)->u.alias.offset = 0;

        if (IS_TYPE_NONSTATIC_METHOD(OBJECT(copy)->type)) {
            if (!(tclass = nspace->theclass) ||
                !(OBJECT(copy)->u.alias.member =
                      CScope_GetClassAccessPath(CClass_GetPathCopy(bcl, TRUE), tclass)) ||
                (OBJECT(copy)->u.alias.member->type != TYPE(nspace->theclass))) {
                CError_Error(ERR_ILLEGAL_USE_NON_STATIC_MEMBER);
                OBJECT(copy)->u.alias.member = NULL;
            }
        }

        CScope_AddObject(nspace, OBJECT(copy)->name, copy);
        return;
    }

    CError_Error(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
}

void CScope_AddClassUsingDeclaration(
    TypeClass* tclass, TypeClass* tclass2, HashNameNode* name, AccessType access
) {
    NameResult result;
    NameSpaceObjectList* scan;

    memclrw(&result, sizeof(NameResult));
    if (!CScope_FindClassMember(&result, tclass2->nspace, name, Lookup_Object) ||
        !CScope_FixMemberResult(tclass, &result)) {
        CError_Error(ERR_ILLEGAL_USE_TEMPLATE_ARGUMENT_DEPENDENT_TYPE, name->name);
        return;
    }

    if (result.objects) {
        for (scan = result.objects; scan; scan = scan->next) {
            switch (scan->object->otype) {
                case OT_ENUMCONST:
                case OT_MEMBERVAR:
                case OT_OBJECT:
                    CScope_AddUsingObject(
                        result.basePath, tclass->nspace, scan->object, result.name, access
                    );
                    break;
            }
        }
    } else {
        if (result.object) {
            CScope_AddUsingObject(
                result.basePath, tclass->nspace, result.object, result.name, access
            );
        } else {
            CError_Error(ERR_ILLEGAL_USE_TEMPLATE_ARGUMENT_DEPENDENT_TYPE, name->name);
        }
    }
}

void CScope_ParseUsingDeclaration(NameSpace* nspace, AccessType access, Boolean unused) {
    NameSpace* saveNSpace;
    Boolean isTemplate;
    Boolean isTypename;
    NameSpaceObjectList* scan;
    NameResult result;

    if (nspace->theclass) {
        isTemplate = (TYPE_CLASS(nspace->theclass)->flags & CLASS_IS_TEMPL) != 0;
        isTypename = FALSE;
        if (tk == TK_TYPENAME) {
            if (!isTemplate) {
                CError_Error(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
            }
            isTypename = TRUE;
            tk = lex();
        }

        if (!CScope_ParseMemberName(nspace->theclass, &result, isTemplate)) {
            CError_Error(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
            return;
        }

        if (result.type &&
            IS_TYPE_TEMPLATE(result.type) &&
            TYPE_TEMPLATE(result.type)->dtype == TEMPLDEP_QUALNAME) {
            CError_ASSERT(3578, isTemplate);

            if (isTypename) {
                ObjType* objtype = galloc(sizeof(ObjType));
                memclrw(objtype, sizeof(ObjType));
                objtype->otype = OT_TYPE;
                objtype->access = access;
                objtype->type = result.type;
                CScope_AddObject(
                    nspace, TYPE_TEMPLATE(result.type)->u.qual.name, OBJ_BASE(objtype)
                );
            } else {
                CTemplClass_RegisterUsingDecl(
                    TEMPL_CLASS(nspace->theclass), TYPE_TEMPLATE(result.type), access
                );
            }

            if ((tk = lex()) != ';') {
                CError_Error(ERR_SEMICOLON_EXPECTED);
            }
            return;
        }

        if (!result.is_qualified) {
            CError_Error(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
            return;
        }
    } else {
        saveNSpace = cscope_current;
        cscope_current = nspace;
        if (!CScope_ParseExprName(&result) || !result.is_qualified) {
            cscope_current = saveNSpace;
            CError_Error(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
            return;
        } else {
            cscope_current = saveNSpace;
        }
    }

    if (result.objects) {
        for (scan = result.objects; scan; scan = scan->next) {
            if (scan->object->otype == OT_OBJECT) {
                CScope_AddUsingObject(result.basePath, nspace, scan->object, result.name, access);
            }
        }
    } else {
        if (result.object) {
            CScope_AddUsingObject(result.basePath, nspace, result.object, result.name, access);
        } else {
            CError_Error(ERR_ILLEGAL_ACCESS_USING_DECLARATION);
        }
    }

    if ((tk = lex()) != ';') {
        CError_Error(ERR_SEMICOLON_EXPECTED);
    }
}

static NameSpace* CScope_ParseQualifiedNamespaceSpecifier(NameSpace* nspace) {
    NameResult result;
    CScopeNSIterator iterator;
    NameSpaceObjectList* list;

    memclrw(&result, sizeof(NameResult));
    if (tk == TK_COLON_COLON) {
        nspace = cscope_root;
        result.is_qualified = TRUE;
        tk = lex();
    }

    do {
        if (tk != TK_IDENTIFIER) {
            CError_Error(ERR_IDENTIFIER_EXPECTED);
            break;
        }

        CScope_NSIteratorInit(&iterator, nspace, &result);
        do {
            if ((list = CScope_NSIteratorFind(&iterator, tkidentifier)) &&
                list->object->otype == OT_NAMESPACE) {
                nspace = OBJ_NAMESPACE(list->object)->nspace;
                goto found;
            }
        } while (CScope_NSIteratorNext(&iterator));
        CError_Error(ERR_UNDEFINED_IDENTIFIER, tkidentifier->name);
found:
        if ((tk = lex()) != TK_COLON_COLON) {
            break;
        }
        result.is_qualified = TRUE;
        tk = lex();
    } while (1);

    return nspace;
}

void CScope_ParseNameSpaceAlias(HashNameNode* name) {
    NameSpaceObjectList* list;
    ObjNameSpace* objns;

    if (!(list = CScope_FindQualName(cscope_current, name))) {
        tk = lex();
        objns = galloc(sizeof(ObjNameSpace));
        memclrw(objns, sizeof(ObjNameSpace));
        objns->otype = OT_NAMESPACE;
        objns->access = ACCESSPUBLIC;
        objns->nspace = CScope_ParseQualifiedNamespaceSpecifier(cscope_current);
        CScope_AddObject(cscope_current, name, OBJ_BASE(objns));
    } else if (list->object->otype != OT_NAMESPACE) {
        CError_Error(ERR_ILLEGAL_NAMESPACE);
        tk = lex();
        CScope_ParseQualifiedNamespaceSpecifier(cscope_current);
    } else {
        tk = lex();
        if (CScope_ParseQualifiedNamespaceSpecifier(cscope_current) !=
            OBJ_NAMESPACE(list->object)->nspace) {
            CError_Error(ERR_IDENTIFIER_REDECLARED, name->name);
        }
    }
    if (tk != ';') {
        CError_Error(ERR_SEMICOLON_EXPECTED);
    }
}

void CScope_ParseUsingDirective(NameSpace* nspace) {
    NameSpace* target;
    NameSpaceList* list;

    target = CScope_ParseQualifiedNamespaceSpecifier(nspace);
    if (target != nspace) {
        for (list = nspace->usings; list; list = list->next) {
            if (list->nspace == target) {
                break;
            }
        }
        if (!list) {
            list = galloc(sizeof(NameSpaceList));
            list->next = nspace->usings;
            list->nspace = target;
            nspace->usings = list;
        }
    } else {
        CError_Error(ERR_ILLEGAL_USE_NAMESPACE_NAME);
    }

    if (tk != ';') {
        CError_Error(ERR_SEMICOLON_EXPECTED);
    }
}

#pragma pack(2)
