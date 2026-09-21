/*
 * TIGER: the smallest thing that proves bin/TigerWebProcess starts.
 *
 * It is the UI-process half of ProcessLauncherTiger's contract and nothing else:
 * make an AF_UNIX SOCK_DGRAM socketpair (SOCK_DGRAM is what ConnectionUnix.cpp's
 * SOCKET_TYPE is on Darwin), hand the child one end as argv[2] with its process
 * identifier as argv[1], send nothing, wait, then close our end and see whether
 * the child notices the EOF and exits by itself.
 *
 * Reports: time from fork to the child being up, peak RSS read from ps, and how
 * the child exited.
 *
 *   cc -o wk2-driver wk2-driver.c
 *   ./wk2-driver /path/to/TigerWebProcess [seconds-to-wait]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static long rssKB(pid_t pid)
{
    char cmd[128];
    snprintf(cmd, sizeof cmd, "ps -o rss= -p %d", (int)pid);
    FILE* p = popen(cmd, "r");
    if (!p)
        return -1;
    long kb = -1;
    if (fscanf(p, "%ld", &kb) != 1)
        kb = -1;
    pclose(p);
    return kb;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <TigerWebProcess> [seconds]\n", argv[0]);
        return 2;
    }
    const char* path = argv[1];
    int waitSeconds = argc > 2 ? atoi(argv[2]) : 5;

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, fds)) {
        perror("socketpair");
        return 1;
    }

    char fdString[16];
    snprintf(fdString, sizeof fdString, "%d", fds[1]);

    double t0 = now();
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (!pid) {
        close(fds[0]);
        execl(path, path, "1", fdString, (char*)NULL);
        _exit(127);
    }
    close(fds[1]);

    /* First moment the child has an address space we can measure. */
    long firstRSS = -1;
    double tUp = 0;
    for (int i = 0; i < 2000; i++) {
        long kb = rssKB(pid);
        if (kb > 4096) {          /* past dyld, into real work */
            firstRSS = kb;
            tUp = now();
            break;
        }
        usleep(5000);
    }

    long peak = firstRSS;
    for (int i = 0; i < waitSeconds * 10; i++) {
        usleep(100000);
        long kb = rssKB(pid);
        if (kb < 0)
            break;              /* gone */
        if (kb > peak)
            peak = kb;
    }

    printf("fork -> 4MB RSS: %.3f s\n", tUp ? tUp - t0 : -1.0);
    printf("RSS after %d s:  %ld KB (%.1f MB)\n", waitSeconds, peak, peak / 1024.0);

    /* Is it still there, i.e. did it settle into its run loop rather than fall over? */
    int status = 0;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) {
        printf("child exited BEFORE we closed the socket: %s %d\n",
            WIFSIGNALED(status) ? "signal" : "status",
            WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status));
        return 1;
    }
    printf("still running after %d s, waiting on IPC\n", waitSeconds);

    /* EOF. */
    double tClose = now();
    close(fds[0]);
    for (int i = 0; i < 100; i++) {
        r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            printf("exited %.3f s after EOF: %s %d\n", now() - tClose,
                WIFSIGNALED(status) ? "signal" : "status",
                WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status));
            return 0;
        }
        usleep(100000);
    }
    printf("did NOT exit within 10 s of EOF; killing\n");
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return 1;
}
