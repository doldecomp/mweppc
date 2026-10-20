#ifndef COMPILER_CPREPTOKENIZER_H
#define COMPILER_CPREPTOKENIZER_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CR = 13,
    LF = 10
};

extern short prepskipnextchar(void);
extern short prepnextchar(void);
extern short prepnextstringchar(unsigned char* str, Boolean countNewlines);
extern void CPrep_MatchChar(char ch, Boolean countNewlines);
extern char* CPrep_MatchChar2(char* start, char ch, Boolean countNewlines);
extern short prepcurchar(void);
extern char* ReadIdentifier(const char* p);
extern Boolean skipendoflinematch(unsigned char* str, short token);
extern void skipendofline(void);
extern short lookahead(void);
extern short lookahead_noeol(void);
extern short lex(void);
extern short plex(void);
extern short lexidentifier(void);

#ifdef __cplusplus
}
#endif

#endif
