// Repro attempt for the Tiger wedge: AF_UNIX SOCK_DGRAM socketpairs, fd passing with
// SCM_RIGHTS across three processes, and a receiver killed with fds still in flight.
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
static void sendfd(int sock, int fd) {
    char b = 'x'; struct iovec iov = { &b, 1 };
    char cbuf[CMSG_SPACE(sizeof(int))]; memset(cbuf, 0, sizeof cbuf);
    struct msghdr m; memset(&m, 0, sizeof m); m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = cbuf; m.msg_controllen = sizeof cbuf;
    struct cmsghdr* c = CMSG_FIRSTHDR(&m); c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &fd, sizeof fd);
    if (sendmsg(sock, &m, 0) < 0 && errno != EAGAIN) perror("sendmsg");
}
int main(int argc, char** argv) {
    int rounds = argc > 1 ? atoi(argv[1]) : 20;
    for (int r = 0; r < rounds; r++) {
        int ui_gpu[2], ui_web[2], gpu_web[2];
        socketpair(AF_UNIX, SOCK_DGRAM, 0, ui_gpu); socketpair(AF_UNIX, SOCK_DGRAM, 0, ui_web); socketpair(AF_UNIX, SOCK_DGRAM, 0, gpu_web);
        pid_t gpu = fork();
        if (!gpu) { // "GPU": slow to start, then sets nonblock, reads a bit, gets killed
            close(ui_gpu[0]); close(ui_web[0]); close(ui_web[1]); close(gpu_web[1]);
            usleep(300000);
            fcntl(ui_gpu[1], F_SETFL, fcntl(ui_gpu[1], F_GETFL) | O_NONBLOCK);
            char buf[64]; struct iovec iov = { buf, sizeof buf }; char cbuf[256];
            struct msghdr m; memset(&m, 0, sizeof m); m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = cbuf; m.msg_controllen = sizeof cbuf;
            recvmsg(ui_gpu[1], &m, 0);
            pause(); _exit(0);
        }
        pid_t web = fork();
        if (!web) { // "web": receives fds, opens them, exits normally
            close(ui_gpu[0]); close(ui_gpu[1]); close(ui_web[0]); close(gpu_web[0]);
            char buf[64]; struct iovec iov = { buf, sizeof buf }; char cbuf[256];
            for (int i = 0; i < 3; i++) { struct msghdr m; memset(&m, 0, sizeof m); m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = cbuf; m.msg_controllen = sizeof cbuf; recvmsg(ui_web[1], &m, 0); }
            usleep(200000); _exit(0);
        }
        close(ui_gpu[1]); close(ui_web[1]);
        // "UI": pass the gpu<->web ends and some shared-memory-like fds, then kill the GPU with fds in flight
        int shm = open("/tmp/fdgc.shm", O_RDWR | O_CREAT, 0600); ftruncate(shm, 65536);
        sendfd(ui_gpu[0], gpu_web[0]); sendfd(ui_gpu[0], shm); sendfd(ui_gpu[0], shm);
        sendfd(ui_web[0], gpu_web[1]); sendfd(ui_web[0], shm); sendfd(ui_web[0], shm);
        close(gpu_web[0]); close(gpu_web[1]); close(shm);
        usleep(350000);
        kill(gpu, SIGKILL);
        close(ui_gpu[0]);
        int st; waitpid(gpu, &st, 0); waitpid(web, &st, 0);
        close(ui_web[0]);
        printf("round %d ok\n", r); fflush(stdout);
    }
    unlink("/tmp/fdgc.shm");
    return 0;
}
