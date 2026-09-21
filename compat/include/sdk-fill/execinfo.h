/* TIGER: <execinfo.h> (backtrace, backtrace_symbols) arrived in 10.5.
   Implemented in compat/cfcompat.c by walking the i386 frame pointer chain. */
#ifndef __TIGER_EXECINFO_H__
#define __TIGER_EXECINFO_H__

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int backtrace(void **array, int size);
char **backtrace_symbols(void *const *array, int size);
void backtrace_symbols_fd(void *const *array, int size, int fd);

#ifdef __cplusplus
}
#endif

#endif /* __TIGER_EXECINFO_H__ */
