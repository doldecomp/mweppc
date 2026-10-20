#include "driver/TextUtils.h"
#include "driver/Memory.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include "driver/Resources.h"
#include "driver/ResourceStrings.h"
#include "driver/StringUtils.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

StringPtr c2pstr(char* s) {
    unsigned int len;

    len = strlen(s);
    if (len > 255) {
        len = 255;
    }
    memmove(s + 1, s, len);
    s[0] = len;
    return (StringPtr)s;
}

char* __stdcall p2cstr(StringPtr s) {
    unsigned int len = s[0];
    memmove(s, s + 1, len);
    s[len] = 0;
    return (char*)s;
}

SInt16 CharacterByteType(char* textBuf, SInt16 textOffset, ScriptCode script) {
    return 0;
}

SInt16 CharByte(void) {
    return 0;
}

void GetDateTime(UInt32* secs) {
    OSTime time;
    OS_GetTime(&time);
    OS_TimeToMac(time, secs);
}

Boolean EqualString(ConstStringPtr strA, ConstStringPtr strB, Boolean caseSens, Boolean diacSens) {
    int i, length;
    Boolean equal;

    equal = FALSE;
    length = strA[0];
    if (strA && strB && length == strB[0]) {
        if (caseSens) {
            equal = !memcmp(strA + 1, strB + 1, length);
        } else {
            equal = TRUE;
            i = 0;
            while (equal && i < length) {
                equal = toupper(strA[i]) == toupper(strB[i]);
                ++i;
            }
        }
    }

    return equal;
}

void __stdcall GetIndString(StringPtr theString, SInt16 strListID, SInt16 index) {
    StringPtr ptr;
    StringPtr strings;
    SInt16 idc;
    const char* ret;
    Handle strs;
    SInt16 last;
    StringPtr dptr;
    SInt32 len;
    unsigned char buffer[256];

    ret = Res_GetResource(strListID, index);
    if (ret) {
        strcpy((char*)buffer, ret);
        c2pstr((char*)buffer);
    } else {
        sprintf((char*)buffer, "[Resource string id=%d index=%d not found]", strListID, index);
        c2pstr((char*)buffer);
        strs = GetResource('STR#', strListID);
        if (strs) {
            last = (((unsigned char)(*strs)[0]) << 8) + ((unsigned char)(*strs)[1]);
            if (index > 0 && index <= last) {
                len = GetHandleSize(strs);
                HLock(strs);
                strings = (StringPtr)*strs;
                ptr = strings + 2;
                idc = index;
                while (ptr < strings + len && --idc) {
                    ptr += *ptr + 1;
                }
                if (ptr < strings + len) {
                    _pstrcpy(buffer, ptr);
                }
                HUnlock(strs);
            }
        }
    }
    ptr = buffer + 1;
    dptr = theString + 1;
    while (ptr <= buffer + buffer[0]) {
        if (*ptr == 0xD4) {
            *dptr = '`';
        } else if (*ptr == 0xD5) {
            *dptr = '\'';
        } else if (*ptr == 0xD2 || *ptr == 0xD3) {
            *dptr = '"';
        } else if (*ptr == 0xC9 && dptr - theString < 253) {
            *dptr = '.';
            dptr[1] = '.';
            dptr += 2;
            *dptr = '.';
        } else {
            *dptr = *ptr;
        }
        ++ptr;
        ++dptr;
    }
    *theString = dptr - theString - 1;
}

char* __stdcall getindstring(char* theString, SInt16 strListID, SInt16 index) {
    GetIndString((StringPtr)theString, strListID, index);
    return p2cstr((StringPtr)theString);
}

void NumToString(SInt32 theNum, StringPtr theString) {
    sprintf((char*)theString, "%d", (int)theNum);
    c2pstr((char*)theString);
}

void StringToNum(ConstStringPtr theString, SInt32* theNum) {
    p2cstr((StringPtr)theString);
    sscanf((char*)theString, "%d", (int*)theNum);
    c2pstr((char*)theString);
}
