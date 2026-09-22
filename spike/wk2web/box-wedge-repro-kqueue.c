// Plain repro + kqueue EVFILT_READ monitors on every socket end (what ConnectionUnix's
// tigerMonitorSocket does), to test the 10.4 kqueue/socket lock inversion theory.
#include <sys/socket.h>
#include <sys/event.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <pthread.h>
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
static void* monitor(void* arg) { // like tigerMonitorSocket: kqueue, EVFILT_READ, EVFILT_PROC on parent, 2 s timeout, zero-byte probe
    int fd = (int)(long)arg; int q = kqueue(); struct kevent ch, ev;
    EV_SET(&ch, fd, EVFILT_READ, EV_ADD, 0, 0, 0); kevent(q, &ch, 1, 0, 0, 0);
    pid_t pp = getppid(); if (pp > 1) { EV_SET(&ch, pp, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, 0); kevent(q, &ch, 1, 0, 0, 0); }
    for (;;) {
        struct timespec ts = { 2, 0 }; int n = kevent(q, 0, 0, &ev, 1, &ts);
        if (n > 0 && ev.filter == EVFILT_READ) { char buf[256]; struct iovec iov = { buf, sizeof buf }; char cbuf[256]; struct msghdr m; memset(&m, 0, sizeof m); m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = cbuf; m.msg_controllen = sizeof cbuf; while (recvmsg(fd, &m, 0) > 0) {} }
        else if (n == 0) { if (send(fd, "", 0, 0) < 0 && errno == ECONNRESET) return 0; }
    }
}
static void start_monitor(int fd) { pthread_t t; fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); pthread_create(&t, 0, monitor, (void*)(long)fd); }
int main(int argc, char** argv) {
    int rounds = argc > 1 ? atoi(argv[1]) : 20;
    for (int r = 0; r < rounds; r++) {
        int ui_gpu[2], ui_web[2], gpu_web[2];
        socketpair(AF_UNIX, SOCK_DGRAM, 0, ui_gpu); socketpair(AF_UNIX, SOCK_DGRAM, 0, ui_web); socketpair(AF_UNIX, SOCK_DGRAM, 0, gpu_web);
        pid_t gpu = fork();
        if (!gpu) {
            close(ui_gpu[0]); close(ui_web[0]); close(ui_web[1]); close(gpu_web[0]); close(gpu_web[1]);
            usleep(400000);
            fcntl(ui_gpu[1], F_SETFL, fcntl(ui_gpu[1], F_GETFL) | O_NONBLOCK);
            start_monitor(ui_gpu[1]);
            pause(); _exit(0);
        }
        pid_t web = fork();
        if (!web) {
            close(ui_gpu[0]); close(ui_gpu[1]); close(ui_web[0]); close(gpu_web[0]);
            char buf[64]; struct iovec iov = { buf, sizeof buf }; char cbuf[256];
            struct msghdr m; memset(&m, 0, sizeof m); m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = cbuf; m.msg_controllen = sizeof cbuf;
            recvmsg(ui_web[1], &m, 0);
            int mine = -1; struct cmsghdr* c = CMSG_FIRSTHDR(&m); if (c) memcpy(&mine, CMSG_DATA(c), sizeof mine);
            start_monitor(ui_web[1]);
            if (mine >= 0) { start_monitor(mine); for (int i = 0; i < 6; i++) { int p[2]; socketpair(AF_UNIX, SOCK_DGRAM, 0, p); sendfd(mine, p[0]); close(p[0]); close(p[1]); } }
            usleep(600000); _exit(0);
        }
        close(ui_gpu[1]); close(ui_web[1]);
        start_monitor(ui_gpu[0]); start_monitor(ui_web[0]);
        for (int i = 0; i < 3; i++) send(ui_gpu[0], "hello", 5, 0);
        sendfd(ui_gpu[0], gpu_web[0]); sendfd(ui_web[0], gpu_web[1]);
        close(gpu_web[0]); close(gpu_web[1]);
        usleep(700000);
        kill(gpu, SIGKILL);
        int st; waitpid(gpu, &st, 0); waitpid(web, &st, 0);
        close(ui_gpu[0]); close(ui_web[0]);
        printf("round %d ok\n", r); fflush(stdout);
    }
    return 0;
}
