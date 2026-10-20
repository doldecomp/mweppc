#ifndef COMPILER_PCODELISTING_H
#define COMPILER_PCODELISTING_H

#ifdef __cplusplus
extern "C" {
#endif

extern int pclist_bad_operand;

extern void pccleanuplisting(void);
extern void pclistblocks(char* name1, char* name2);
extern void pclistinterferences(char* class_format, int regcount);
extern void pclistblocks_start_scheduler(char* str1, char* str2);
extern void pclistblocks_end_scheduler(void);

#ifdef __cplusplus
}
#endif

#endif
