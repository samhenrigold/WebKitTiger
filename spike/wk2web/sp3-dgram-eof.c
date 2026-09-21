#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
int main(int argc,char**argv){
 if(argc>1&&!strcmp(argv[1],"child")){
   int fd=atoi(argv[2]);
   for(int i=0;i<8;i++){
     fd_set s;FD_ZERO(&s);FD_SET(fd,&s);
     struct timeval tv={1,0};
     int r=select(fd+1,&s,0,0,&tv);
     ssize_t w=send(fd,"",0,0); int e=errno;
     ssize_t w2=write(fd,"",0); int e2=errno;
     fprintf(stderr,"child i=%d select=%d send0=%ld(%s) write0=%ld(%s)\n",i,r,(long)w,w<0?strerror(e):"ok",(long)w2,w2<0?strerror(e2):"ok");
     if(w<0||w2<0)return 0;
   }
   return 0;
 }
 int fd[2];char b[16];
 if(socketpair(AF_UNIX,SOCK_DGRAM,0,fd))return 1;
 snprintf(b,sizeof b,"%d",fd[1]);
 pid_t p=fork();
 if(!p){close(fd[0]);execl(argv[0],argv[0],"child",b,(char*)0);_exit(127);}
 close(fd[1]);
 sleep(3);
 fprintf(stderr,"parent closing\n");
 close(fd[0]);
 sleep(4);
 return 0;}
