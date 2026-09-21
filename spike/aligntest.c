#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc/malloc.h>
int main(void){ size_t al[]={16,64,4096,16384,65536,1<<20}; int bad=0;
 for(int i=0;i<6;i++){ for(size_t sz=1; sz<=(4u<<20); sz*=16){ void*p=0; int r=posix_memalign(&p,al[i],sz);
   if(r||!p||((size_t)p&(al[i]-1))){printf("FAIL align=%zu size=%zu r=%d p=%p\n",al[i],sz,r,p);bad++;continue;}
   memset(p,0xAB,sz); size_t ms=malloc_size(p); if(ms<sz){printf("FAIL malloc_size %zu<%zu align=%zu\n",ms,sz,al[i]);bad++;} free(p);} }
 void*q=malloc(100); free(q); printf(bad?"ALIGN-FAIL %d\n":"ALIGN-PASS\n",bad); return bad; }
