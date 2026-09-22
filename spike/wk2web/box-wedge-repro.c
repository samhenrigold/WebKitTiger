// Richer repro: a socket in flight whose own receive queue already holds in-flight FIFO
// and unlinked-file descriptors (the web process talking into the GPU<->web socket before
// the GPU has received it), the GPU-like peer then fcntl()s and is SIGKILLed.
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#ifndef SOCKTYPE
#define SOCKTYPE SOCK_DGRAM
#endif
static void sendfds(int sock, int* fds, int n) {
    char b = 'x'; struct iovec iov = { &b, 1 };
    char cbuf[CMSG_SPACE(sizeof(int) * 8)]; memset(cbuf, 0, sizeof cbuf);
    struct msghdr m; memset(&m, 0, sizeof m); m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = cbuf; m.msg_controllen = CMSG_SPACE(sizeof(int) * n);
    struct cmsghdr* c = CMSG_FIRSTHDR(&m); c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(sizeof(int) * n);
    memcpy(CMSG_DATA(c), fds, sizeof(int) * n);
    if (sendmsg(sock, &m, 0) < 0 && errno != EAGAIN) perror("sendmsg");
}
static int makefifo(void) {
    static unsigned counter; char path[64]; snprintf(path, sizeof path, "/tmp/fdgc-sem-%d-%u", getpid(), counter++);
    mkfifo(path, 0600); int fd = open(path, O_RDWR | O_NONBLOCK); unlink(path); return fd;
}
static int makeshm(void) {
    char path[] = "/tmp/fdgc-shm.XXXXXX"; int fd = mkstemp(path); unlink(path); ftruncate(fd, 1 << 20);
    void* p = mmap(0, 1 << 20, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); if (p != MAP_FAILED) memset(p, 1, 4096);
    return fd;
}
int main(int argc, char** argv) {
    int rounds = argc > 1 ? atoi(argv[1]) : 20;
    for (int r = 0; r < rounds; r++) {
        int ui_gpu[2], ui_web[2], gpu_web[2];
        socketpair(AF_UNIX, SOCKTYPE, 0, ui_gpu); socketpair(AF_UNIX, SOCKTYPE, 0, ui_web); socketpair(AF_UNIX, SOCKTYPE, 0, gpu_web);
        pid_t gpu = fork();
        if (!gpu) {
            close(ui_gpu[0]); close(ui_web[0]); close(ui_web[1]); close(gpu_web[0]); close(gpu_web[1]);
            usleep(400000);
            fcntl(ui_gpu[1], F_SETFL, fcntl(ui_gpu[1], F_GETFL) | O_NONBLOCK);
            pause(); _exit(0);
        }
        pid_t web = fork();
        if (!web) {
            close(ui_gpu[0]); close(ui_gpu[1]); close(ui_web[0]); close(gpu_web[0]);
            // receive my end of gpu<->web from the UI, then immediately talk into it with fds
            char buf[64]; struct iovec iov = { buf, sizeof buf }; char cbuf[256];
            struct msghdr m; memset(&m, 0, sizeof m); m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = cbuf; m.msg_controllen = sizeof cbuf;
            recvmsg(ui_web[1], &m, 0);
            int mine = -1; struct cmsghdr* c = CMSG_FIRSTHDR(&m); if (c) memcpy(&mine, CMSG_DATA(c), sizeof mine);
            for (int i = 0; i < 4 && mine >= 0; i++) {
#if defined(NO_FIFO)
                int fds[3] = { makeshm(), makeshm(), makeshm() };
#elif defined(NO_SHM)
                int fds[3] = { makefifo(), makefifo(), makefifo() };
#else
                int fds[3] = { makeshm(), makefifo(), makefifo() };
#endif
                sendfds(mine, fds, 3); close(fds[0]); close(fds[1]); close(fds[2]); }
            usleep(300000); _exit(0);
        }
        close(ui_gpu[1]); close(ui_web[1]);
        int a[1] = { gpu_web[0] }; sendfds(ui_gpu[0], a, 1);   // GPU's end, in flight in the UI->GPU socket
        int b[1] = { gpu_web[1] }; sendfds(ui_web[0], b, 1);   // web's end
        close(gpu_web[0]); close(gpu_web[1]);
        usleep(500000);
        kill(gpu, SIGKILL);
        close(ui_gpu[0]);
        int st; waitpid(gpu, &st, 0); waitpid(web, &st, 0);
        close(ui_web[0]);
        printf("round %d ok\n", r); fflush(stdout);
    }
    return 0;
}
