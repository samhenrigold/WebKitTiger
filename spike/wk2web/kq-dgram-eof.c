#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/socket.h>
/* Parent closes its end but STAYS ALIVE. Does EVFILT_READ or EVFILT_WRITE on an
   AF_UNIX SOCK_DGRAM socketpair fire (EV_EOF) in the child? */
int main(int argc,char**argv){
 if(argc>1&&!strcmp(argv[1],"child")){
   int fd=atoi(argv[2]);
   int kq=kqueue();
   struct kevent ev[2];
   EV_SET(&ev[0],fd,EVFILT_READ,EV_ADD,0,0,0);
   EV_SET(&ev[1],fd,EVFILT_WRITE,EV_ADD,0,0,0);
   if(kevent(kq,ev,2,0,0,0)<0){fprintf(stderr,"add: %s\n",strerror(errno));return 1;}
   fprintf(stderr,"child armed\n");
   for(int i=0;i<6;i++){
     struct kevent out; struct timespec ts={1,0};
     int n=kevent(kq,0,0,&out,1,&ts);
     if(n>0) fprintf(stderr,"i=%d filter=%d flags=%x(EOF=%d) fflags=%x data=%ld\n",
        i,(int)out.filter,out.flags,(out.flags&EV_EOF)?1:0,out.fflags,(long)out.data);
     else fprintf(stderr,"i=%d timeout n=%d\n",i,n);
     if(n>0&&(out.flags&EV_EOF))return 0;
   }
   return 0;
 }
 int fd[2];char b[16];
 if(socketpair(AF_UNIX,SOCK_DGRAM,0,fd))return 1;
 snprintf(b,sizeof b,"%d",fd[1]);
 pid_t p=fork();
 if(!p){close(fd[0]);execl(argv[0],argv[0],"child",b,(char*)0);_exit(127);}
 close(fd[1]);
 sleep(2);
 fprintf(stderr,"parent closing but staying alive\n");
 close(fd[0]);
 sleep(6);
 return 0;}
