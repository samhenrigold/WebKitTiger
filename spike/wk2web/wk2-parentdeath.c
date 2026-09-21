/*
 * TIGER: how long does an auxiliary process take to notice that the process that
 * launched it has died?
 *
 * The web process watches its parent with kqueue EVFILT_PROC/NOTE_EXIT, which is
 * the case that matters: the UI process crashed and a 22 MB content process must
 * not be left behind. This measures that path, which wk2-driver.c does not --
 * wk2-driver stays alive and only closes the socket, which on 10.4 no event
 * reports and the two-second backstop catches.
 *
 * Shape: main forks a stand-in launcher, the launcher makes the socketpair and
 * execs the process under test and then _exit()s, main times how long the
 * grandchild survives.
 *
 *   cc -std=c99 -o wk2-parentdeath wk2-parentdeath.c
 *   ./wk2-parentdeath /path/to/TigerWebProcess
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

static int alive(pid_t pid)
{
    return !kill(pid, 0);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <auxiliary-process>\n", argv[0]);
        return 2;
    }
    const char* path = argv[1];
    const char* pidFile = "/tmp/wk2-parentdeath.pid";
    unlink(pidFile);

    pid_t launcher = fork();
    if (launcher < 0) {
        perror("fork");
        return 1;
    }
    if (!launcher) {
        int fds[2];
        if (socketpair(AF_UNIX, SOCK_DGRAM, 0, fds))
            _exit(1);
        char fdString[16];
        snprintf(fdString, sizeof fdString, "%d", fds[1]);
        pid_t child = fork();
        if (!child) {
            close(fds[0]);
            execl(path, path, "1", fdString, (char*)NULL);
            _exit(127);
        }
        close(fds[1]);
        FILE* f = fopen(pidFile, "w");
        fprintf(f, "%d\n", (int)child);
        fclose(f);
        /* Let it settle into its run loop, then die holding the socket open. */
        sleep(4);
        _exit(0);
    }

    pid_t child = 0;
    for (int i = 0; i < 100 && !child; i++) {
        FILE* f = fopen(pidFile, "r");
        if (f) {
            if (fscanf(f, "%d", &child) != 1)
                child = 0;
            fclose(f);
        }
        if (!child)
            usleep(50000);
    }
    if (!child) {
        fprintf(stderr, "launcher never reported a pid\n");
        return 1;
    }
    printf("process under test: %d\n", (int)child);

    int status;
    waitpid(launcher, &status, 0);
    double died = now();
    printf("launcher gone\n");

    for (int i = 0; i < 600; i++) {
        if (!alive(child)) {
            printf("noticed and exited %.3f s after the parent died\n", now() - died);
            return 0;
        }
        usleep(10000);
    }
    printf("STILL ALIVE 6 s after the parent died\n");
    kill(child, SIGKILL);
    return 1;
}
