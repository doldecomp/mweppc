#include "driver/Parameter.h"
#include "compiler/win32.h"
#include "driver/Arguments.h"
#include "driver/CWPlugins.h"
#include "driver/MacFileTypes.h"
#include "driver/MsDos.h"
#include "driver/Option.h"
#include "driver/option_system.h"
#include "driver/oslib.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"
#include "driver/StringExtras.h"
#include "driver/StringUtils.h"
#include "driver/Targets.h"
#include "driver/TextUtils.h"
#include "driver/Utils.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

char curparam[4096];
char* descparam = &curparam[0];
char* helpparam = &curparam[1024];
char* defaultparam = &curparam[2048];

#define copy_8(dst, src) \
    do { \
        ((unsigned char*)(dst))[0] = ((unsigned char*)(src))[0]; \
    } while (0)

#define copy_16(dst, src) \
    do { \
        ((unsigned char*)(dst))[0] = ((unsigned char*)(src))[0]; \
        ((unsigned char*)(dst))[1] = ((unsigned char*)(src))[1]; \
    } while (0)

#define copy_32(dst, src) \
    do { \
        ((unsigned char*)(dst))[0] = ((unsigned char*)(src))[0]; \
        ((unsigned char*)(dst))[1] = ((unsigned char*)(src))[1]; \
        ((unsigned char*)(dst))[2] = ((unsigned char*)(src))[2]; \
        ((unsigned char*)(dst))[3] = ((unsigned char*)(src))[3]; \
    } while (0)

#define inline_write_32(dst, val) \
    do { \
        union { \
            unsigned char bytes[4]; \
            unsigned long lg; \
        } _val, _arg; \
        copy_32(_val.bytes, dst); \
        copy_32(_arg.bytes, val); \
        _val.lg = _arg.lg; \
        copy_32(dst, _val.bytes); \
    } while (0)

#define inline_write_16(dst, val) \
    do { \
        union { \
            unsigned char bytes[2]; \
            unsigned short sh; \
        } _val, _arg; \
        copy_16(_val.bytes, dst); \
        copy_16(_arg.bytes, val); \
        _val.sh = _arg.sh; \
        copy_16(dst, _val.bytes); \
    } while (0)

#define inline_write_8(ptr, val) copy_8(ptr, val)

#define inline_andor_32(dst, and, or) \
    do { \
        union { \
            unsigned char bytes[4]; \
            unsigned long lg; \
        } _val, _arg, _arg2; \
        copy_32(_val.bytes, dst); \
        copy_32(_arg.bytes, and); \
        copy_32(_arg2.bytes, or); \
        _val.lg = (_val.lg & ~_arg.lg) | _arg2.lg; \
        copy_32(dst, _val.bytes); \
    } while (0)

#define inline_andor_16(dst, and, or) \
    do { \
        union { \
            unsigned char bytes[2]; \
            unsigned short sh; \
        } _val, _arg, _arg2; \
        copy_16(_val.bytes, dst); \
        copy_16(_arg.bytes, and); \
        copy_16(_arg2.bytes, or); \
        _val.sh = (_val.sh & ~_arg.sh) | _arg2.sh; \
        copy_16(dst, _val.bytes); \
    } while (0)

#define inline_andor_8(dst, and, or) \
    do { \
        ((unsigned char*)(dst))[0] = (((unsigned char*)(dst))[0] & ~((unsigned char*)(and))[0]) | \
            ((unsigned char*)(or))[0]; \
    } while (0)

#define inline_xor_32(dst, arg) \
    do { \
        union { \
            unsigned char bytes[4]; \
            unsigned long lg; \
        } _val, _arg; \
        copy_32(_val.bytes, dst); \
        copy_32(_arg.bytes, arg); \
        _val.lg ^= _arg.lg; \
        copy_32(dst, _val.bytes); \
    } while (0)

#define inline_xor_16(dst, arg) \
    do { \
        union { \
            unsigned char bytes[2]; \
            unsigned short sh; \
        } _val, _arg; \
        copy_16(_val.bytes, dst); \
        copy_16(_arg.bytes, arg); \
        _val.sh ^= _arg.sh; \
        copy_16(dst, _val.bytes); \
    } while (0)

#define inline_xor_8(dst, arg) \
    do { \
        ((unsigned char*)(dst))[0] ^= ((unsigned char*)(arg))[0]; \
    } while (0)

static int Param_GetArgument(PARAM_T* param, const char** cparam, int exec);
static int Param_Parse(PARAM_T* param, const char* cparam, int flags);
void Param_Warning(VarArgShort id, ...);

static int Param_None(PARAM_T* p, const char* pstr, int flags) {
    return TRUE;
}

static void DescHelpParam_None(
    PARAM_T* p, const char** desc, const char** help, const char** defaul
) {
    *desc = 0;
    *help = 0;
    *defaul = 0;
}

static int CompareParam_None(PARAM_T* p) {
    return FALSE;
}

static void DescHelpParam_Number(
    NUM_T* p, const char** desc, const char** help, const char** defaul
) {
    unsigned long value;
    unsigned char value8;
    unsigned short value16;

    value = 0;
    *desc = 0;
    if (p->myname) {
        *desc = p->myname;
    }

    if (p->size == 1) {
        if (!*desc) {
            *desc = "byte";
        }
        if (p->num) {
            copy_8(&value8, p->num);
        }
        value = value8;
    } else if (p->size == 2) {
        if (!*desc) {
            *desc = "short";
        }
        if (p->num) {
            copy_16(&value16, p->num);
        }
        value = value16;
    } else if (p->size == 4) {
        if (!*desc) {
            *desc = "long";
        }
        if (p->num) {
            copy_32(&value, p->num);
        }
    }

    if (p->size == 4) {
        if (p->lo != p->hi) {
            sprintf(helpparam, "range 0x%x - 0x%x", (unsigned int)p->lo, (unsigned int)p->hi);
        }
        *help = helpparam;
        if (value <= 0x10000) {
            sprintf(defaultparam, "%u", (unsigned int)value);
        } else {
            sprintf(defaultparam, "0x%x", (unsigned int)value);
        }
        *defaul = defaultparam;
    } else {
        if (p->lo != p->hi) {
            sprintf(helpparam, "range %u - %u", (unsigned int)p->lo, (unsigned int)p->hi);
        }
        *help = helpparam;
        sprintf(defaultparam, "%u", (unsigned int)value);
        *defaul = defaultparam;
    }

    if (!p->num) {
        *defaul = 0;
    }
    if (p->lo == p->hi) {
        *help = 0;
    }
}

static int CompareParam_Number(NUM_T* p) {
    return FALSE;
}

static int Param_Number(NUM_T* p, const char* pstr, int flags) {
    unsigned long val;
    unsigned long lo;
    unsigned long hi;
    const char* opstr;

    val = 0;
    lo = 0;
    hi = 0;
    opstr = pstr;
    if (!pstr[0]) {
        Param_Error(CLPStr5, "", pstr);
        return FALSE;
    }

    if (((*pstr == '0') && my_tolower(pstr[1]) == 'x') || (*pstr == '$')) {
        if (pstr[0] == '$') {
            pstr += 1;
        } else {
            pstr += 2;
        }

        while (*pstr) {
            if (!my_isxdigit(*pstr)) {
                Param_Error(CLPStr5, "hexadecimal ", opstr);
                return FALSE;
            }

            if (val > 0xFFFFFFF) {
                Param_Error(CLPStr4, opstr);
                return FALSE;
            }

            val = (val << 4) | (my_isdigit(*pstr) ? (*pstr - '0') : (my_tolower(*pstr) - 'a' + 10));
            ++pstr;
        }
    } else if (pstr[0] == '0') {
        pstr += 1;

        while (*pstr) {
            if (*pstr < '0' || *pstr >= '8') {
                Param_Error(CLPStr5, "octal ", opstr);
                return FALSE;
            }

            if (val > 0x1FFFFFFF) {
                Param_Error(CLPStr4, opstr);
                return FALSE;
            }

            val = (val << 3) | (*pstr - '0');
            ++pstr;
        }
    } else {
        while (*pstr) {
            if (!my_isdigit(*pstr)) {
                Param_Error(CLPStr5, "decimal ", opstr);
                return FALSE;
            }

            if (val > 0x19999999) {
                Param_Error(CLPStr4, opstr);
                return FALSE;
            }

            val = (val * 10) + (*pstr - '0');
            ++pstr;
        }
    }

    if (p->lo == p->hi) {
        if (p->size == 1) {
            lo = 0;
            hi = 0xFF;
        } else if (p->size == 2) {
            lo = 0;
            hi = 0xFFFF;
        } else {
            lo = 0;
            hi = 0xFFFFFFFF;
        }
    } else {
        lo = p->lo;
        hi = p->hi;
    }

    if (!p->fit && (val < lo || val > hi)) {
        Param_Error(CLPStr6, val, lo, hi);
        return FALSE;
    }

    if (val < lo) {
        Param_Warning(CLPStr7, val, lo, hi, lo);
        val = lo;
    } else if (val > hi) {
        Param_Warning(CLPStr7, val, lo, hi, hi);
        val = hi;
    }

    if (p->size == 1) {
        unsigned char val8 = val;
        inline_write_8(p->num, &val8);
    }

    if (p->size == 2) {
        unsigned short val16 = val;
        inline_write_16(p->num, &val16);
    } else if (p->size == 4) {
        unsigned long val32 = val;
        inline_write_32(p->num, &val32);
    }

    return TRUE;
}

static void DescHelpParam_FTypeCreator(
    FTYPE_T* p, const char** desc, const char** help, const char** defaul
) {
    if (p->myname) {
        *desc = p->myname;
    } else if (p->iscreator) {
        *desc = "creator";
    } else {
        *desc = "type";
    }

    if (!p->fc) {
        *defaul = 0;
    } else {
        unsigned long code;
        unsigned char* ptr;
        int cnt;

        ptr = (unsigned char*)defaultparam;
        code = *p->fc;
        cnt = 0;
        *(ptr++) = '\'';
        for (; cnt < 4; cnt++) {
            *(ptr++) = code >> 24;
            code <<= 8;
        }
        *(ptr++) = '\'';
        *ptr = 0;
        *defaul = defaultparam;
    }
}

static int CompareParam_FTypeCreator(FTYPE_T* p) {
    return FALSE;
}

static int Param_FTypeCreator(FTYPE_T* p, const char* pstr, int flags) {
    char code[4] = {' ', ' ', ' ', ' '};
    int cnt;
    const char* opstr;

    cnt = 0;
    opstr = pstr;
    while (*pstr && cnt < 4) {
        code[4 - ++cnt] = *(pstr++);
    }

    if (*pstr) {
        Param_Error(CLPStr8, opstr);
        return FALSE;
    }

    inline_write_32(p->fc, &code);
    return TRUE;
}

static void DescHelpParam_String(
    STRING_T* p, const char** desc, const char** help, const char** defaul
) {
    if (p->myname) {
        *desc = p->myname;
    } else {
        *desc = "string";
    }

    if (!p->str) {
        *help = 0;
        *defaul = 0;
    } else {
        sprintf(helpparam, "maximum length %d chars", p->maxlen - 1);
        *help = helpparam;

        if (*p->str) {
            sprintf(defaultparam, "'%s'", p->str);
            *defaul = defaultparam;
        } else {
            *defaul = "none";
        }
    }
}

static int CompareParam_String(STRING_T* p) {
    return FALSE;
}

static int Param_String(STRING_T* p, const char* pstr, int flags) {
    int len;

    len = strlen(pstr);
    strncpy(p->str, pstr, p->maxlen - 1);
    if (p->pstring) {
        c2pstr(p->str);
    }

    if (len > p->maxlen) {
        Param_Error(CLPStr9, pstr, pstr + len - 5, p->maxlen - 1);
        return FALSE;
    }

    return TRUE;
}

static void DescHelpParam_ID_or_SYM(
    STRING_T* p, const char** desc, const char** help, const char** defaul
) {
    if (p->myname) {
        *desc = p->myname;
    } else if (p->which == PARAMWHICH_Id) {
        *desc = "identifier";
    } else {
        *desc = "symbol";
    }

    if (!p->str) {
        *defaul = 0;
        *help = 0;
    } else {
        sprintf(helpparam, "maximum length %d chars", p->maxlen - 1);
        *help = helpparam;

        if (*p->str) {
            sprintf(defaultparam, "'%s'", p->str);
            *defaul = defaultparam;
        } else {
            *defaul = "none";
        }
    }
}

static int CompareParam_ID_or_SYM(STRING_T* p) {
    return FALSE;
}

static int Param_ID_or_SYM(STRING_T* p, const char* pstr, int flags) {
    const char* ptr;
    const char* allowed;

    if (Param_String(p, pstr, flags)) {
        if (p->which == PARAMWHICH_Id) {
            allowed = "$_";
        } else if (p->which == PARAMWHICH_Sym) {
            allowed = "_.$@?#";
        }

        for (ptr = pstr; *ptr; ++ptr) {
            if (ptr == pstr && my_isdigit(*ptr)) {
                Param_Warning(CLPStr10, pstr);
            }

            if (!my_isalnum(*ptr) && !strchr(allowed, *ptr)) {
                Param_Warning(CLPStr11, pstr, *ptr);
            }
        }

        return TRUE;
    } else {
        return FALSE;
    }
}

static void DescHelpParam_OnOff(
    ONOFF_T* p, const char** desc, const char** help, const char** defaul
) {
    unsigned char def;

    if (p->myname) {
        *desc = p->myname;
    } else {
        *desc = "on|off";
    }

    *help = 0;
    if (!p->var) {
        *defaul = 0;
    } else {
        if (p->var) {
            *((unsigned char*)&def) = *((unsigned char*)p->var);
        }
        if (def) {
            *defaul = "on";
        } else {
            *defaul = "off";
        }
    }
}

static int CompareParam_OnOff(ONOFF_T* p) {
    return FALSE;
}

static int Param_OnOff(ONOFF_T* p, const char* pstr, int flags) {
    Boolean mytrue = (flags & PARAMPARSEFLAGS_NEGATED) == 0;

    if (!ustrcmp(pstr, "on")) {
        *(p->var) = mytrue;
    } else if (!ustrcmp(pstr, "off")) {
        *(p->var) = !mytrue;
    } else {
        Param_Error(CLPStr12, pstr);
        return FALSE;
    }

    return TRUE;
}

static void DescHelpParam_OffOn(
    ONOFF_T* p, const char** desc, const char** help, const char** defaul
) {
    unsigned char def;

    if (p->myname) {
        *desc = p->myname;
    } else {
        *desc = "off|on";
    }

    *help = 0;
    if (!p->var) {
        *defaul = 0;
    } else {
        if (p->var) {
            *((unsigned char*)&def) = *((unsigned char*)p->var);
        }
        if (!def) {
            *defaul = "on";
        } else {
            *defaul = "off";
        }
    }
}

static int CompareParam_OffOn(OFFON_T* p) {
    return FALSE;
}

static int Param_OffOn(OFFON_T* p, const char* pstr, int flags) {
    Boolean mytrue = (flags & PARAMPARSEFLAGS_NEGATED) == 0;

    if (!ustrcmp(pstr, "off")) {
        *(p->var) = mytrue;
    } else if (!ustrcmp(pstr, "on")) {
        *(p->var) = !mytrue;
    } else {
        Param_Error(CLPStr12, pstr);
        return FALSE;
    }

    return TRUE;
}

static void DescHelpParam_FilePath(
    FILEPATH_T* p, const char** desc, const char** help, const char** defaul
) {
    char* hptr = helpparam;

    if (p->myname) {
        *desc = p->myname;
    } else {
        *desc = "filepath";
    }

    *help = 0;
    *defaul = p->defaultstr;
}

static int CompareParam_FilePath(FILEPATH_T* p) {
    return FALSE;
}

static int Param_FilePath(FILEPATH_T* p, const char* pstr, int flags) {
    int err;
    OSSpec spec;
    char canon[260];
    int clen;

    if (!pstr[0]) {
        ((unsigned char*)p->filename)[0] = 0;
        return TRUE;
    }

    err = OS_MakeFileSpec(pstr, &spec);
    if (err) {
        Param_Error(CLPStr18, pstr, OS_GetErrText(err));
        return FALSE;
    }

    OS_SpecToString(&spec, canon, sizeof(canon));
    if (p->fflags & 1) {
        c2pstrcpy(p->filename, canon);
    } else {
        clen = strlen(canon);
        if (clen > p->maxlen) {
            Param_Error(CLPStr13, canon + clen - 32, canon);
            return FALSE;
        }
        strcpy(p->filename, canon);
    }

    return TRUE;
}

static void DescHelpParam_Mask(
    MASK_T* p, const char** desc, const char** help, const char** defaul
) {
    *help = 0;
    *desc = 0;
    *defaul = 0;
}

static int CompareParam_Mask(MASK_T* p) {
    if (p->size == 1) {
        unsigned char or8 = p->ormask, and8 = p->andmask, val8;
        if (p->num) {
            copy_8(&val8, p->num);
        }
        return ((val8 & ~and8) | or8) == val8;
    } else if (p->size == 2) {
        unsigned short or16 = p->ormask, and16 = p->andmask, val16;
        if (p->num) {
            copy_16(&val16, p->num);
        }
        return ((val16 & ~and16) | or16) == val16;
    } else {
        unsigned long or32 = p->ormask, and32 = p->andmask, val32;
        if (p->num) {
            copy_32(&val32, p->num);
        }
        return ((val32 & ~and32) | or32) == val32;
    }
}

static int Param_Mask(MASK_T* p, const char* pstr, int flags) {
    if (p->size == 1) {
        unsigned char or8 = p->ormask, and8 = p->andmask, tmp8;
        if (flags & PARAMPARSEFLAGS_NEGATED) {
            tmp8 = and8;
            and8 |= or8;
            or8 = tmp8;
        }
        inline_andor_8(p->num, &and8, &or8);
    } else if (p->size == 2) {
        unsigned short or16 = p->ormask, and16 = p->andmask, tmp16;
        if (flags & PARAMPARSEFLAGS_NEGATED) {
            tmp16 = and16;
            and16 |= or16;
            or16 = tmp16;
        }
        inline_andor_16(p->num, &and16, &or16);
    } else {
        unsigned long or32 = p->ormask, and32 = p->andmask, tmp32;
        if (flags & PARAMPARSEFLAGS_NEGATED) {
            tmp32 = and32;
            and32 |= or32;
            or32 = tmp32;
        }
        inline_andor_32(p->num, &and32, &or32);
    }
    return TRUE;
}

static void DescHelpParam_Toggle(
    TOGGLE_T* p, const char** desc, const char** help, const char** defaul
) {
    *help = 0;
    *desc = 0;
    *defaul = 0;
}

static int CompareParam_Toggle(TOGGLE_T* p) {
    if (p->size == 1) {
        unsigned char mask8, val8;
        mask8 = (unsigned char)p->mask;
        if (p->num) {
            copy_8(&val8, p->num);
        }
        return (val8 ^ mask8) == val8;
    } else if (p->size == 2) {
        unsigned short mask16, val16;
        mask16 = (unsigned short)p->mask;
        if (p->num) {
            copy_16(&val16, p->num);
        }
        return (val16 ^ mask16) == val16;
    } else {
        unsigned long mask32, val32;
        mask32 = p->mask;
        if (p->num) {
            copy_16(&val32, p->num);
        }
        return (val32 ^ mask32) == val32;
    }
}

static int Param_Toggle(TOGGLE_T* p, const char* pstr, int flags) {
    if (p->size == 1) {
        unsigned char val8;
        val8 = (unsigned char)p->mask;
        inline_xor_8(p->num, &val8);
    } else if (p->size == 2) {
        unsigned short val16;
        val16 = (unsigned short)p->mask;
        inline_xor_16(p->num, &val16);
    } else {
        inline_xor_32(p->num, &p->mask);
    }
    return TRUE;
}

static void DescHelpParam_Set(SET_T* p, const char** desc, const char** help, const char** defaul) {
    *help = 0;
    *desc = 0;
    *defaul = 0;
}

static int CompareParam_Set(SET_T* p) {
    if (p->size == 1) {
        unsigned char set8, val8;
        set8 = (unsigned char)p->value;
        if (p->num) {
            copy_8(&val8, p->num);
        }
        return set8 == val8;
    } else if (p->size == 2) {
        unsigned short set16, val16;
        set16 = (unsigned short)p->value;
        if (p->num) {
            copy_16(&val16, p->num);
        }
        return set16 == val16;
    } else {
        unsigned long set32, val32;
        set32 = p->value;
        if (p->num) {
            copy_32(&val32, p->num);
        }
        return set32 == val32;
    }
}

static int Param_Set(SET_T* p, const char* pstr, int flags) {
    if (p->size == 1) {
        unsigned char val8;
        val8 = (unsigned char)p->value;
        if ((flags & PARAMPARSEFLAGS_NEGATED) && val8 <= 1) {
            val8 = !val8;
        }
        inline_write_8(p->num, &val8);
    } else if (p->size == 2) {
        unsigned short val16;
        val16 = (unsigned short)p->value;
        if ((flags & PARAMPARSEFLAGS_NEGATED) && val16 <= 1) {
            val16 = !val16;
        }
        inline_write_16(p->num, &val16);
    } else {
        unsigned long val32;
        val32 = p->value;
        if ((flags & PARAMPARSEFLAGS_NEGATED) && val32 <= 1) {
            val32 = !val32;
        }
        inline_write_32(p->num, &val32);
    }
    return TRUE;
}

static void DescHelpParam_SetString(
    SETSTRING_T* p, const char** desc, const char** help, const char** defaul
) {
    *help = 0;
    *desc = 0;
    *defaul = 0;
}

static int CompareParam_SetString(SETSTRING_T* p, int flags) {
    if (p->pstring) {
        return !pstrcmp((const unsigned char*)p->var, (const unsigned char*)p->value);
    } else {
        return !strcmp(p->var, p->value);
    }
}

static int Param_SetString(SETSTRING_T* p, const char* pstr, int flags) {
    if (p->pstring) {
        memcpy(p->var, p->value, p->value[0]);
    } else {
        strcpy(p->var, p->value);
    }
    return TRUE;
}

static void DescHelpParam_Generic(
    GENERIC_T* p, const char** desc, const char** help, const char** defaul
) {
    if (p->myname) {
        *desc = p->myname;
    } else if ((p->flags & PARAMFLAGS_ARG_MASK) != 1) {
        *desc = "xxx";
    } else {
        *desc = 0;
    }
    *help = p->help;
    *defaul = 0;
}

static int CompareParam_Generic(GENERIC_T* p) {
    return FALSE;
}

static int Param_Generic(GENERIC_T* p, const char* pstr, int flags) {
    return p->parse(curopt, p->var, pstr, flags);
}

static void DescHelpParam_Setting(
    SETTING_T* p, const char** desc, const char** help, const char** defaul
) {
    char* dptr = descparam;

    dptr += sprintf(dptr, "%s", p->myname ? p->myname : "var");
    if ((p->flags & PARAMFLAGS_ARG_MASK) != 1) {
        dptr += sprintf(
            dptr,
            "%s=%s%s",
            (p->flags & PARAMFLAGS_OPTIONAL_ARG) ? "[" : "",
            p->valuename ? p->valuename : "...",
            (p->flags & PARAMFLAGS_OPTIONAL_ARG) ? "]" : ""
        );
    }
    *desc = descparam;
    *help = 0;
    *defaul = 0;
}

static int CompareParam_Setting(SETTING_T* p) {
    return FALSE;
}

static int Param_Setting(SETTING_T* p, const char* pstr, int flags) {
    char savevar[256];
    const char* vstr;
    ArgToken* tok;
    Boolean equals = FALSE;

    if (!pstr) {
        Param_Error(CLPStr40);
        return FALSE;
    }

    strncpy(savevar, pstr, sizeof(savevar));
    tok = Arg_PeekToken();
    if (tok && tok->val == ATK_EQUALS) {
        Arg_UsedToken();
        equals = TRUE;
        if (!Param_GetArgument((PARAM_T*)p, &vstr, flags)) {
            return FALSE;
        }
    } else {
        vstr = 0;
    }

    if (!vstr && equals) {
        vstr = "";
    }

    return p->parse(savevar, vstr);
}

static void DescHelpParam_IfArg(
    IFARG_T* p, const char** desc, const char** help, const char** defaul
) {
    const char* argdesc = 0;
    const char* nonedesc = 0;
    const char* arghelp = 0;
    const char* nonehelp = 0;
    const char* argdefault = 0;
    const char* nonedefault = 0;
    char desclocal[1024];
    char* dptr = desclocal;
    char helplocal[1024];
    char* hptr = helplocal;
    const char* ind;

    *dptr = 0;
    *hptr = 0;

    if (p->parg) {
        Param_DescHelp(p->parg, &argdesc, &arghelp, &argdefault);
    }
    if (p->pnone) {
        Param_DescHelp(p->pnone, &nonedesc, &nonehelp, &nonedefault);
    }

    if (p->helpa && p->helpn) {
        if (argdefault) {
            hptr += sprintf(hptr, "%s (default is %s), else %s", p->helpa, argdefault, p->helpn);
        } else {
            hptr += sprintf(hptr, "%s, else %s", p->helpa, p->helpn);
        }
    } else if (p->helpa) {
        hptr += sprintf(hptr, "%s", p->helpa);
        if (argdefault) {
            hptr += sprintf(hptr, "; default is %s", argdefault);
        }
    } else if (p->helpn) {
        hptr += sprintf(hptr, "nothing, else %s", p->helpn);
    }

    if (argdesc) {
        if (p->myname) {
            dptr += sprintf(dptr, "[%s]", p->myname ? p->myname : "param");
        } else if (arghelp) {
            ind = strchr(argdesc, '\n');
            if (!ind) {
                ind = argdesc + strlen(argdesc);
            }
            dptr += sprintf(dptr, "[%.*s]", ind - argdesc, argdesc);
        } else {
            dptr += sprintf(dptr, "[%s]", argdesc);
            argdesc = 0;
        }
    }

    if (arghelp || nonehelp) {
        dptr += sprintf(dptr, "\n\t");
        hptr += sprintf(hptr, "\n\t");
        if (arghelp) {
            if (argdesc) {
                dptr += sprintf(dptr, "%s", argdesc);
            }
            if (arghelp) {
                hptr += sprintf(hptr, "%s", arghelp);
            }
            if (nonehelp) {
                dptr += sprintf(dptr, "\b\t");
                hptr += sprintf(hptr, "\b\t");
            }
        }
        if (nonehelp) {
            if (nonedesc) {
                dptr += sprintf(dptr, "%s", nonedesc);
            } else {
#ifdef __MWERKS__
                dptr += sprintf(dptr, "(if blank)", nonedesc);
#else
                dptr += sprintf(dptr, "(if blank)");
#endif
            }
            if (nonehelp) {
                hptr += sprintf(hptr, "%s", nonehelp);
            }
            dptr += sprintf(dptr, "\n");
            hptr += sprintf(hptr, "\n");
        }
        dptr += sprintf(dptr, "\b");
        hptr += sprintf(hptr, "\b");
    }

    if (dptr != desclocal) {
        strcpy(descparam, desclocal);
        *desc = descparam;
    } else {
        *desc = 0;
    }

    if (hptr != helplocal) {
        strcpy(helpparam, helplocal);
        *help = helpparam;
    } else {
        *help = 0;
    }

    *defaul = 0;
}

static int CompareParam_IfArg(IFARG_T* p) {
    return FALSE;
}

static int Param_IfArg(IFARG_T* p, const char* pstr, int flags) {
    if (pstr) {
        return Param_Parse(p->parg, pstr, flags);
    } else if (p->pnone) {
        return Param_Parse(p->pnone, pstr, flags);
    } else {
        return TRUE;
    }
}

typedef int (*PARSE_FUNC)(PARAM_T*, const char*, int);
PARSE_FUNC ParamParsers[16] = {
    (PARSE_FUNC)&Param_None,
    (PARSE_FUNC)&Param_FTypeCreator,
    (PARSE_FUNC)&Param_FilePath,
    (PARSE_FUNC)&Param_Number,
    (PARSE_FUNC)&Param_String,
    (PARSE_FUNC)&Param_ID_or_SYM,
    (PARSE_FUNC)&Param_ID_or_SYM,
    (PARSE_FUNC)&Param_OnOff,
    (PARSE_FUNC)&Param_OffOn,
    (PARSE_FUNC)&Param_Mask,
    (PARSE_FUNC)&Param_Toggle,
    (PARSE_FUNC)&Param_Set,
    (PARSE_FUNC)&Param_SetString,
    (PARSE_FUNC)&Param_Generic,
    (PARSE_FUNC)&Param_IfArg,
    (PARSE_FUNC)&Param_Setting
};

static int Param_Parse(PARAM_T* param, const char* cparam, int flags) {
    int ret = FALSE;

    if (!param) {
        CLPFatalError("PARAM_T is NULL");
    }

    if (param->which >= PARAMWHICH_None && param->which < PARAMWHICH_MAX) {
        return ParamParsers[(int)param->which](param, cparam, flags);
    } else {
        CLPFatalError("Unhandled PARAM_T (%d)", param->which);
        ret = FALSE;
    }
    return ret;
}

typedef void (*DESC_HELP_FUNC)(PARAM_T*, const char**, const char**, const char**);
DESC_HELP_FUNC DescHelpParams[16] = {
    (DESC_HELP_FUNC)&DescHelpParam_None,
    (DESC_HELP_FUNC)&DescHelpParam_FTypeCreator,
    (DESC_HELP_FUNC)&DescHelpParam_FilePath,
    (DESC_HELP_FUNC)&DescHelpParam_Number,
    (DESC_HELP_FUNC)&DescHelpParam_String,
    (DESC_HELP_FUNC)&DescHelpParam_ID_or_SYM,
    (DESC_HELP_FUNC)&DescHelpParam_ID_or_SYM,
    (DESC_HELP_FUNC)&DescHelpParam_OnOff,
    (DESC_HELP_FUNC)&DescHelpParam_OffOn,
    (DESC_HELP_FUNC)&DescHelpParam_Mask,
    (DESC_HELP_FUNC)&DescHelpParam_Toggle,
    (DESC_HELP_FUNC)&DescHelpParam_Set,
    (DESC_HELP_FUNC)&DescHelpParam_SetString,
    (DESC_HELP_FUNC)&DescHelpParam_Generic,
    (DESC_HELP_FUNC)&DescHelpParam_IfArg,
    (DESC_HELP_FUNC)&DescHelpParam_Setting
};
void Param_DescHelp(PARAM_T* param, const char** desc, const char** help, const char** defaul) {
    *desc = 0;
    *help = 0;
    *defaul = 0;

    if (!param) {
        CLPFatalError("PARAM_T is NULL");
        return;
    }

    if (param->which >= PARAMWHICH_None && param->which < PARAMWHICH_MAX) {
        DescHelpParams[(int)param->which](param, desc, help, defaul);
    } else {
        CLPFatalError("Unhandled PARAM_T (%d)", param->which);
    }
}

typedef int (*COMPARE_FUNC)(PARAM_T*);
COMPARE_FUNC CompareParams[16] = {
    (COMPARE_FUNC)&CompareParam_None,
    (COMPARE_FUNC)&CompareParam_FTypeCreator,
    (COMPARE_FUNC)&CompareParam_FilePath,
    (COMPARE_FUNC)&CompareParam_Number,
    (COMPARE_FUNC)&CompareParam_String,
    (COMPARE_FUNC)&CompareParam_ID_or_SYM,
    (COMPARE_FUNC)&CompareParam_ID_or_SYM,
    (COMPARE_FUNC)&CompareParam_OnOff,
    (COMPARE_FUNC)&CompareParam_OffOn,
    (COMPARE_FUNC)&CompareParam_Mask,
    (COMPARE_FUNC)&CompareParam_Toggle,
    (COMPARE_FUNC)&CompareParam_Set,
    (COMPARE_FUNC)&CompareParam_SetString,
    (COMPARE_FUNC)&CompareParam_Generic,
    (COMPARE_FUNC)&CompareParam_IfArg,
    (COMPARE_FUNC)&CompareParam_Setting
};
int Param_Compare(PARAM_T* param) {
    if (!param) {
        CLPFatalError("PARAM_T is NULL");
    }

    if (param->which >= PARAMWHICH_None && param->which < PARAMWHICH_MAX) {
        return CompareParams[(int)param->which](param);
    } else {
        CLPFatalError("Unhandled PARAM_T (%d)", param->which);
        return FALSE;
    }
}

static void Param_PushParam(const char* param) {
    Args_Push(OSTACK_PARAM, (void*)param, 0);
}

static void Param_PopParam(void) {
    Args_Pop(OSTACK_PARAM);
}

static Boolean Param_IsNonTextFile(const char* fname, Boolean warn) {
    OSSpec spec;
    CWDataType mactype;
    Boolean isfile;

    if (!OS_MakeSpec(fname, &spec, &isfile)) {
        if (isfile) {
            if (!OS_Status(&spec)) {
                if (!OS_GetMacFileType(&spec, &mactype)) {
                    if (mactype != 'TEXT') {
                        if (warn) {
                            Param_Warning(CLPStr77, fname);
                        }
                        return TRUE;
                    }
                }
            }
        }
    }

    return FALSE;
}

static int Param_GetArgument(PARAM_T* param, const char** cparam, int exec) {
    int subparse;
    short nextOpt;
    short nextParam;
    short lastOpt;
    ArgToken* tok;

    subparse = (exec & PARAMPARSEFLAGS_SUBOPTION) != 0;
    nextOpt = !subparse ? ATK_ARG_END : ATK_COMMA;
    nextParam = !subparse ? ATK_COMMA : ATK_EQUALS;
    lastOpt = !subparse ? ATK_END : ATK_ARG_END;

    tok = Arg_PeekToken();
    OPTION_ASSERT(1467, tok);

    if ((param->flags & PARAMFLAGS_ARG_MASK) == 0) {
        if (tok->val == nextParam || tok->val == ATK_EQUALS) {
            tok = Arg_UsedToken();
        }
        if (tok->val == nextOpt || tok->val == nextParam || tok->val == lastOpt) {
            Param_Error(CLPStr34_ArgumentsExpected);
            return FALSE;
        }
        if (tok->val != ATK_ARG) {
            Param_Error(CLPStr57, "parameter", Arg_GetTokenName(tok));
            return FALSE;
        }
        Arg_GetTokenText(tok, curparam, sizeof(curparam), TRUE);
        Arg_UsedToken();
        *cparam = curparam;

        if (param->flags & PARAMFLAGS_EQUALS_AFTER) {
            tok = Arg_PeekToken();
            if (tok->val == ATK_EQUALS) {
                tok = Arg_UsedToken();
            }
        }
    } else if (param->flags & PARAMFLAGS_NO_ARG) {
        *cparam = 0;
        if (tok->val == ATK_EQUALS && !subparse) {
            if (!(exec & PARAMPARSEFLAGS_EQUALS_OK)) {
                Param_Error(CLPStr37);
                return FALSE;
            }
            tok = Arg_UsedToken();
        }
    } else if (param->flags & PARAMFLAGS_OPTIONAL_ARG) {
        if (tok->val == nextOpt || tok->val == lastOpt) {
            *cparam = 0;
        } else if (tok->val == nextParam) {
            tok = Arg_UsedToken();
            if (tok->val == ATK_ARG) {
                Arg_GetTokenText(tok, curparam, sizeof(curparam), TRUE);
                if ((param->flags & PARAMFLAGS_OPTIONAL_NOT_FILE) &&
                    Param_IsNonTextFile(curparam, !(exec & PARAMPARSEFLAGS_NO_EXECUTE))) {
                    *cparam = 0;
                } else {
                    Arg_UsedToken();
                    *cparam = curparam;
                }
            } else {
                *cparam = 0;
            }
        } else if (tok->val == ATK_ARG && !tok->text[0]) {
            tok = Arg_UsedToken();
            if (tok->val == nextParam) {
                tok = Arg_UsedToken();
            }
            *cparam = 0;
        } else if ((exec & PARAMPARSEFLAGS_SUBOPTION) && tok->val != ATK_EQUALS) {
            *cparam = 0;
        } else {
            if (tok->val == ATK_EQUALS) {
                tok = Arg_UsedToken();
            }
            if (tok->val == ATK_ARG) {
                Arg_GetTokenText(tok, curparam, sizeof(curparam), TRUE);
                if ((param->flags & PARAMFLAGS_OPTIONAL_NOT_FILE) &&
                    Param_IsNonTextFile(curparam, !(exec & PARAMPARSEFLAGS_NO_EXECUTE))) {
                    *cparam = 0;
                } else {
                    Arg_UsedToken();
                    *cparam = curparam;
                }
            } else {
                *cparam = 0;
            }
        }
    } else {
        CLPFatalError("Unknown parameter type");
    }

    if (pTool->TYPE == CWDROPINCOMPILERTYPE && (exec & PARAMPARSEFLAGS_TO_LINKER) && *cparam) {
        if (!(exec & PARAMPARSEFLAGS_FIRST)) {
            if (subparse) {
                Arg_AddToToolArgs(&linkargs, ATK_EQUALS, 0);
            } else {
                Arg_AddToToolArgs(&linkargs, ATK_COMMA, 0);
            }
        }
        Param_PushParam(*cparam);
        Args_AddToToolArgs(&linkargs);
        Param_PopParam();
    }

    return TRUE;
}

int Params_Parse(PARAM_T* param, int flags) {
    const char* cparam;
    int subparse;

    subparse = (flags & PARAMPARSEFLAGS_SUBOPTION) != 0;
    flags |= PARAMPARSEFLAGS_FIRST;

    if (param) {
        while (param) {
            if (flags & PARAMPARSEFLAGS_HAS_SUBARGS) {
                flags |= PARAMPARSEFLAGS_EQUALS_OK;
            }

            if (!Param_GetArgument(param, &cparam, flags)) {
                return FALSE;
            }

            if (!(flags & PARAMPARSEFLAGS_NO_EXECUTE)) {
                if (!Param_Parse(
                        param,
                        cparam,
                        flags |
                            (short)((param->flags & PARAMFLAGS_SUBARGS)
                                        ? PARAMPARSEFLAGS_HAS_SUBARGS
                                        : 0)
                    )) {
                    return FALSE;
                }
            } else if (param->flags & PARAMFLAGS_SUBARGS) {
                if (!Param_GetArgument(param, &cparam, flags | PARAMPARSEFLAGS_SUBOPTION)) {
                    return FALSE;
                }
            }

            if (cparam) {
                flags &= ~PARAMPARSEFLAGS_FIRST;
            }
            flags &= ~PARAMPARSEFLAGS_EQUALS_OK;
            param = param->next;
        }
    }

    return TRUE;
}

void Param_Error(VarArgShort id, ...) {
    va_list va;
    va_start(va, id);
    Option_ParamError(id, va);
    va_end(va);
}

void Param_Warning(VarArgShort id, ...) {
    va_list va;
    va_start(va, id);
    Option_ParamWarning(id, va);
    va_end(va);
}
