#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
int main() {
  mkdir("/tmp/wk2tests-op", 0755); close(open("/tmp/wk2tests-op/f", O_CREAT|O_WRONLY, 0644));
  symlink("/tmp/wk2tests-op/nonexist", "/tmp/wk2tests-op/broken"); symlink("/tmp/wk2tests-op/f", "/tmp/wk2tests-op/lf");
  const char* p[] = {"/tmp/wk2tests-op/f", "/tmp/wk2tests-op/broken", "/tmp/wk2tests-op/lf", "/tmp/wk2tests-op"};
  for (int i = 0; i < 4; i++) {
    int fd = open(p[i], O_RDONLY|O_DIRECTORY|O_NOFOLLOW); printf("%s DIR|NOFOLLOW -> %d %s\n", p[i], fd, fd<0?strerror(errno):""); if (fd>=0) close(fd);
    fd = open(p[i], O_RDONLY|O_NOFOLLOW); printf("%s NOFOLLOW -> %d %s\n", p[i], fd, fd<0?strerror(errno):""); if (fd>=0) close(fd);
  }
  unlink("/tmp/wk2tests-op/f"); unlink("/tmp/wk2tests-op/broken"); unlink("/tmp/wk2tests-op/lf"); rmdir("/tmp/wk2tests-op");
}
