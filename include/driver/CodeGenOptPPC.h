#ifndef DRIVER_CODEGENOPTPPC_H
#define DRIVER_CODEGENOPTPPC_H

#ifdef __cplusplus
extern "C" {
#endif

extern void CodeGen_InitCompiler(void);
extern void CodeGen_TermCompiler(void);
extern void CodeGen_InitBackEndOptions(void);
extern void Test_Version_Numbers(void);

#ifdef __cplusplus
}
#endif

#endif
