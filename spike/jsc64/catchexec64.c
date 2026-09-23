/* catchexec64: run a command with a task exception port installed, so that neither it nor
 * anything it forks can reach 10.4's crashdump (NOTES 2026-09-22: crashdump wedges the box).
 * Task exception ports are inherited across fork and exec; a crashed task is reported as
 *   TIGER-CHILD-CRASH pid <n> exception <e> code <c>
 * on stderr and SIGKILLed, so its parent sees an ordinary signal death.
 *   catchexec64 <command> [args...]      -> the command's exit status (128+signal if killed)
 * For test binaries that do not link TigerCrashCatcher (libpas's test_pas).
 * Build: toolchain/bin/tiger-clang64 -O1 catchexec64.c -o catchexec64 */
#include <mach/mach.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

struct ExceptionMessage { /* exception_raise, EXCEPTION_DEFAULT (no mach/exc.h in the 10.4u SDK) */
    mach_msg_header_t header;
    mach_msg_body_t body;
    mach_msg_port_descriptor_t thread;
    mach_msg_port_descriptor_t task;
    NDR_record_t ndr;
    exception_type_t exception;
    mach_msg_type_number_t codeCount;
    integer_t code[2];
    char trailer[128];
};

static mach_port_t port;

static void* handler(void* unused)
{
    (void)unused;
    for (;;) {
        struct ExceptionMessage m;
        memset(&m, 0, sizeof m);
        if (mach_msg(&m.header, MACH_RCV_MSG, 0, sizeof m, port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL) != KERN_SUCCESS)
            continue;
        int pid = -1;
        pid_for_task(m.task.name, &pid);
        fprintf(stderr, "\nTIGER-CHILD-CRASH pid %d exception %d code %#x %#x\n", pid, m.exception, m.code[0], m.code[1]);
        if (pid > 0 && pid != getpid())
            kill(pid, SIGKILL);
        mach_port_deallocate(mach_task_self(), m.thread.name);
        mach_port_deallocate(mach_task_self(), m.task.name);
    }
    return NULL;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: catchexec64 command [args...]\n");
        return 2;
    }
    mach_port_t task = mach_task_self();
    if (mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &port) || mach_port_insert_right(task, port, port, MACH_MSG_TYPE_MAKE_SEND)
        || task_set_exception_ports(task, EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION | EXC_MASK_ARITHMETIC | EXC_MASK_BREAKPOINT,
            port, EXCEPTION_DEFAULT, THREAD_STATE_NONE)) {
        fprintf(stderr, "catchexec64: cannot install the exception port\n");
        return 2;
    }
    pthread_t thread;
    pthread_create(&thread, NULL, handler, NULL);
    pid_t child = fork();
    if (!child) {
        execvp(argv[1], argv + 1);
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) { }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
