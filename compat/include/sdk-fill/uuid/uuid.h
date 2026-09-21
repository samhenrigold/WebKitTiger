/* TIGER: uuid_string_t is 10.5+. */
#ifndef __TIGER_UUID_UUID_H__
#define __TIGER_UUID_UUID_H__
#include_next <uuid/uuid.h>
#ifndef _UUID_STRING_T
#define _UUID_STRING_T
typedef char uuid_string_t[37];
#endif
#endif
