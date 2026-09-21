#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main(int argc,char**argv){ char buf[4096]; memset(buf,0,sizeof buf); printf("hello from i386 darwin8, argc=%d %s\n", argc, buf); return 0; }
