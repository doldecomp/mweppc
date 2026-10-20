#ifndef COMPILER_COMPILERTOOLS_H
#define COMPILER_COMPILERTOOLS_H

#include <setjmp.h>
#include "compiler/common.h"
#include "compiler/types.h"
#include "compiler/win32.h"

struct FSSpec;

#ifdef __cplusplus
extern "C" {
#endif

extern void CompilerGetCString(short index, char* string);

typedef struct HeapInfo {
    SInt32 blocks;
    SInt32 allocsize;
    SInt32 total_size;
    SInt32 total_free;
    SInt32 average_block_size;
    SInt32 average_block_free;
    SInt32 largest_free_block;
} HeapInfo;

#pragma pack(push, 2)
struct GList {
    char** data;
    SInt32 size;
    SInt32 hndlsize;
    SInt32 growsize;
};
#pragma pack(pop)

extern HashNameNode** name_hash_nodes;
extern void (*GListErrorProc)(void);

extern short InitGList(GList* gl, SInt32 size);
extern void FreeGList(GList* gl);
extern void LockGList(GList* gl);
extern void ShrinkGList(GList* gl);
extern void AppendGListData(GList* gl, const void* data, SInt32 size);
extern void AppendGListNoData(GList* gl, SInt32 size);
extern void AppendGListByte(GList* gl, SInt8 thebyte);
extern void AppendGListWord(GList* gl, SInt16 theword);
extern void AppendGListTargetEndianWord(GList* gl, UInt16 theword);
extern void AppendGListLong(GList* gl, SInt32 theword);
extern void AppendGListTargetEndianLong(GList* gl, UInt32 theword);
extern void AppendGListID(GList* gl, const char* name);
extern void AppendGListName(GList* gl, const char* name);

extern SInt16 CHash(const char* string);
extern HashNameNode* GetHashNameNode(const char* name);
extern HashNameNode* GetHashNameNodeExport(const char* name);
extern void InitNameHash(void);

typedef void (*heaperror_t)(void);

extern SInt32 CTool_TotalHeapSize(void);
extern void CTool_GetHeapInfo(HeapInfo* result, unsigned char heapID);
extern short initheaps(heaperror_t heaperrorproc);
extern short initgheap(heaperror_t heaperrorproc);
extern heaperror_t getheaperror(void);
extern void setheaperror(heaperror_t heaperrorproc);
extern void releaseheaps(void);
extern void releasegheap(void);
extern void releaseoheap(void);
extern void* galloc(SInt32 size);
extern void* lalloc(SInt32 size);
extern void* aalloc(SInt32 size);
extern void* oalloc(SInt32 size);

extern void locklheap(void);
extern void unlocklheap(void);
extern void freelheap(void);
extern void freeaheap(void);
extern void freeoheap(void);

extern char* ScanDec(char* string, UInt32* result, Boolean* overflow);

extern short hash(char* str);
extern void memclrw(void* ptr, SInt32 size);
extern void CToLowercase(char* src, char* dst);
extern short getbit(SInt32 mask);
extern UInt16 CTool_EndianConvertWord16(UInt16 theword);
extern UInt32 CTool_EndianConvertWord32(UInt32 theword);
extern void CTool_EndianConvertMem(void* data, short len);
static inline UInt32 CTool_EndianReadWord32(void* ptr) {
    return *(UInt32*)ptr;
}
extern void CTool_EndianConvertWord64(CInt64 ci, char* result);
extern UInt16 CTool_EndianConvertInPlaceWord16Ptr(UInt16* x);
extern UInt32 CTool_EndianConvertInPlaceWord32Ptr(UInt32* x);
extern HashNameNode* CTool_GetPathName(const struct FSSpec* fss, SInt32* moddateptr);

#ifdef __cplusplus
}
#endif

#endif
