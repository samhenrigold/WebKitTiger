#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <errno.h>
#include <string.h>
static void probe(int type,const char*name){
  int fd[2]; if(socketpair(AF_UNIX,type,0,fd)){printf("%s: socketpair %s\n",name,strerror(errno));return;}
  close(fd[1]);
  fd_set s; FD_ZERO(&s); FD_SET(fd[0],&s);
  struct timeval tv={1,0};
  int r=select(fd[0]+1,&s,0,0,&tv);
  char b[8]; ssize_t n=-2; int e=0;
  if(r>0){n=recv(fd[0],b,sizeof b,0); e=errno;}
  printf("%s: select=%d readable=%d recv=%ld errno=%s\n",name,r,r>0?FD_ISSET(fd[0],&s):0,(long)n,n<0?strerror(e):"-");
  close(fd[0]);
}
int main(){probe(SOCK_DGRAM,"DGRAM");probe(SOCK_STREAM,"STREAM");
#ifdef SOCK_SEQPACKET
probe(SOCK_SEQPACKET,"SEQPACKET");
#endif
return 0;}
