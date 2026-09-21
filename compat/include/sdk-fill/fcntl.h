/* TIGER: O_CLOEXEC is 10.5+. Tiger's kernel ignores the bit, so a file opened
   with it stays inherited across exec. WebKit only uses it defensively; the
   value matches 10.5+ so that a Tiger binary run on a later system behaves. */
#ifndef __TIGER_FCNTL_H__
#define __TIGER_FCNTL_H__
#include_next <fcntl.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0x1000000
#endif
#endif
