#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/socket.h>
/* Does EVFILT_PROC/NOTE_EXIT work on Darwin 8, and can it be mixed with an
   EVFILT_READ on a socket in one kqueue? */
int main(int argc,char**argv){
 if(argc>1&&!strcmp(argv[1],"child")){
   int fd=atoi(argv[2]);
   pid_t pp=getppid();
   int kq=kqueue();
   if(kq<0){fprintf(stderr,"child kqueue: %s\n",strerror(errno));return 1;}
   struct kevent ev[2];
   EV_SET(&ev[0],fd,EVFILT_READ,EV_ADD,0,0,0);
   EV_SET(&ev[1],pp,EVFILT_PROC,EV_ADD|EV_ONESHOT,NOTE_EXIT,0,0);
   if(kevent(kq,ev,2,0,0,0)<0){fprintf(stderr,"child kevent add: %s\n",strerror(errno));return 1;}
   fprintf(stderr,"child armed on fd=%d ppid=%d\n",fd,(int)pp);
   struct timeval t0,t1; gettimeofday(&t0,0);
   struct kevent out;
   int n=kevent(kq,0,0,&out,1,0);
   gettimeofday(&t1,0);
   double ms=(t1.tv_sec-t0.tv_sec)*1000.0+(t1.tv_usec-t0.tv_usec)/1000.0;
   fprintf(stderr,"child woke n=%d filter=%d ident=%lu flags=%x fflags=%x after %.1f ms\n",
     n,n>0?(int)out.filter:0,n>0?(unsigned long)out.ident:0,n>0?out.flags:0,n>0?out.fflags:0,ms);
   return 0;
 }
 int fd[2];char b[16];
 if(socketpair(AF_UNIX,SOCK_DGRAM,0,fd))return 1;
 snprintf(b,sizeof b,"%d",fd[1]);
 pid_t p=fork();
 if(!p){close(fd[0]);execl(argv[0],argv[0],"child",b,(char*)0);_exit(127);}
 close(fd[1]);
 sleep(2);
 fprintf(stderr,"parent exiting\n");
 return 0;}
