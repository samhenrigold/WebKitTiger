/* TIGER: RTLD_MAIN_ONLY is 10.5+. */
#ifndef __TIGER_DLFCN_H__
#define __TIGER_DLFCN_H__
#include_next <dlfcn.h>
#ifndef RTLD_MAIN_ONLY
#define RTLD_MAIN_ONLY ((void *) -5)
#endif
#endif
