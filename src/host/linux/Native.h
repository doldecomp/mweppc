#ifndef HOST_LINUX_NATIVE_H
#define HOST_LINUX_NATIVE_H

extern int Native_argc;
extern char** Native_argv;
extern char** Native_envp;

void Native_InitializeWin32(void);
int Native_FindResource(const char* type, const char* name, const void** data, unsigned int* size);
unsigned int Native_ResourceSize(const void* data);

#endif
