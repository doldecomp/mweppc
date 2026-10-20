#ifndef COMPILER_TOKENS_H
#define COMPILER_TOKENS_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TK_EOF 0
#define TK_EOL -7
#define TK_ILLEGAL -6
#define TK_STRING_WIDE -5
#define TK_STRING -4
#define TK_IDENTIFIER -3
#define TK_FLOATCONST -2
#define TK_INTCONST -1
#define TK_NOT 33
#define TK_BITAND 38
#define TK_BITOR 124
#define TK_COMPL 126
#define TK_AUTO 256
#define TK_REGISTER 257
#define TK_STATIC 258
#define TK_EXTERN 259
#define TK_TYPEDEF 260
#define TK_INLINE 261
#define TK_VOID 262
#define TK_CHAR 263
#define TK_SHORT 264
#define TK_INT 265
#define TK_LONG 266
#define TK_FLOAT 267
#define TK_DOUBLE 268
#define TK_SIGNED 269
#define TK_UNSIGNED 270
#define TK_STRUCT 271
#define TK_UNION 272
#define TK_ENUM 273
#define TK_CLASS 274
#define TK_CUSTOM_TYPE_113 275
#define TK_CUSTOM_TYPE_114 276
#define TK_UU_VEC2X32FLOAT_UU 277
#define TK_CUSTOM_TYPE_116 278
#define TK_CUSTOM_TYPE_117 279
#define TK_CUSTOM_TYPE_118 280
#define TK_CUSTOM_TYPE_119 281
#define TK_CUSTOM_TYPE_11A 282
#define TK_UU_VECTOR 283
#define TK_UU_TYPEOF_UU 284
#define TK_BOOL 285
#define TK_WCHAR_T 286
#define TK_U_COMPLEX 287
#define TK_U_IMAGINARY 288
#define TK_TYPENAME 289
#define TK_CONST 290
#define TK_VOLATILE 291
#define TK_PASCAL 292
#define TK_UU_DECLSPEC 293
#define TK_UU_STDCALL 294
#define TK_UU_CDECL 295
#define TK_UU_FASTCALL 296
#define TK_UU_FLOATCALL 296
#define TK_UU_FAR 297
#define TK_EXPLICIT 298
#define TK_MUTABLE 299
#define TK_ONEWAY 300
#define TK_IN 301
#define TK_INOUT 302
#define TK_OUT 303
#define TK_BYCOPY 304
#define TK_BYREF 305
#define TK_ASM 310
#define TK_CASE 311
#define TK_DEFAULT 312
#define TK_IF 313
#define TK_ELSE 314
#define TK_SWITCH 315
#define TK_WHILE 316
#define TK_DO 317
#define TK_FOR 318
#define TK_GOTO 319
#define TK_CONTINUE 320
#define TK_BREAK 321
#define TK_RETURN 322
#define TK_SIZEOF 323
#define TK_CATCH 324
#define TK_DELETE 325
#define TK_FRIEND 326
#define TK_NEW 327
#define TK_OPERATOR 328
#define TK_PRIVATE 329
#define TK_PROTECTED 330
#define TK_PUBLIC 331
#define TK_TEMPLATE 332
#define TK_THIS 333
#define TK_THROW 334
#define TK_TRY 335
#define TK_VIRTUAL 336
#define TK_INHERITED 337
#define TK_CONST_CAST 338
#define TK_DYNAMIC_CAST 339
#define TK_NAMESPACE 340
#define TK_REINTERPRET_CAST 341
#define TK_STATIC_CAST 342
#define TK_USING 343
#define TK_TRUE 344
#define TK_FALSE 345
#define TK_TYPEID 346
#define TK_EXPORT 347
#define TK_MULT_ASSIGN 348
#define TK_DIV_ASSIGN 349
#define TK_MOD_ASSIGN 350
#define TK_ADD_ASSIGN 351
#define TK_SUB_ASSIGN 352
#define TK_SHL_ASSIGN 353
#define TK_SHR_ASSIGN 354
#define TK_AND_ASSIGN 355
#define TK_XOR_ASSIGN 356
#define TK_OR_ASSIGN 357
#define TK_LOGICAL_OR 358
#define TK_LOGICAL_AND 359
#define TK_LOGICAL_EQ 360
#define TK_LOGICAL_NE 361
#define TK_LESS_EQUAL 362
#define TK_GREATER_EQUAL 363
#define TK_SHL 364
#define TK_SHR 365
#define TK_INCREMENT 366
#define TK_DECREMENT 367
#define TK_ARROW 368
#define TK_ELLIPSIS 369
#define TK_DOT_STAR 370
#define TK_ARROW_STAR 371
#define TK_COLON_COLON 372
#define TK_AT_INTERFACE 373
#define TK_AT_IMPLEMENTATION 374
#define TK_AT_PROTOCOL 375
#define TK_AT_END 376
#define TK_AT_PRIVATE 377
#define TK_AT_PROTECTED 378
#define TK_AT_PUBLIC 379
#define TK_AT_CLASS 380
#define TK_AT_SELECTOR 381
#define TK_AT_ENCODE 382
#define TK_AT_DEFS 383
#define TK_SELF 384
#define TK_SUPER 385
#define TK_NEW_ARRAY 386
#define TK_DELETE_ARRAY 387
#define TK_UU_ALIGNOF_UU 388
#define TK_RESTRICT 389
#define TK_UU_ATTRIBUTE_UU 390
#define TK_UU_UUIDOF 391

#pragma pack(push, 2)
struct FileOffsetInfo {
    struct CPrepFileInfo* file;
    SInt32 tokenline;
    SInt32 tokenoffset;
    Boolean is_inline;
};

typedef union TData {
    struct HashNameNode* tkidentifier;
    CInt64 tkintconst;
    Float tkfloatconst;
    struct {
        char* data;
        SInt32 size;
    } tkstring;
} TData;
struct TStreamElement {
    SInt16 tokentype;
    SInt16 subtype;
    struct CPrepFileInfo* tokenfile;
    SInt32 tokenoffset;
    SInt32 tokenline;
    TData data;
};
struct TokenStream {
    SInt32 tokens;
    TStreamElement* firsttoken;
};
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif
