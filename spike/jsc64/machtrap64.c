/* TIGER64: does a task-level Mach exception port steal the traps JSC handles with
 * POSIX signals?
 *
 * TigerCrashCatcher registers EXC_MASK_BAD_ACCESS|EXC_MASK_BAD_INSTRUCTION|... on the
 * task and _exit()s on anything it receives. JSC recovers from two of those on purpose:
 * VMTraps patches `hlt` over DFG invalidation points and fixes it up in a SIGILL/SIGSEGV
 * handler, and wasm catches out-of-bounds loads in a SIGBUS/SIGSEGV handler. Mach
 * exception ports are consulted before the BSD signal is ever synthesized, so whoever
 * holds the port wins.
 *
 * This prints, for `hlt` and for a null load out of an RWX mapping:
 *   - the exception type/code the port receives, and
 *   - whether the POSIX handler runs at all (with the port installed, and without it).
 *
 * Build: toolchain/bin/tiger-clang64 -O1 -o machtrap64 machtrap64.c
 */
#include <mach/mach.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct ExceptionMessage {
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

static mach_port_t exceptionPort = MACH_PORT_NULL;
static volatile int portSawException;
static volatile int signalSawException;
static sigjmp_buf jumpBuffer;

static void* handlerThread(void*)
{
    for (;;) {
        struct ExceptionMessage message;
        memset(&message, 0, sizeof(message));
        if (mach_msg(&message.header, MACH_RCV_MSG, 0, sizeof(message), exceptionPort, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL) != KERN_SUCCESS)
            continue;
        printf("  port: exception 0x%x code 0x%x 0x%x\n", message.exception, message.code[0], message.code[1]);
        fflush(stdout);
        portSawException = 1;
        /* Say "not mine": the kernel should then try the next handler and, finding
         * none, turn the exception into a BSD signal. */
        mig_reply_error_t reply;
        reply.Head = message.header;
        reply.Head.msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(message.header.msgh_bits), 0);
        reply.Head.msgh_size = sizeof(reply);
        reply.Head.msgh_remote_port = message.header.msgh_remote_port;
        reply.Head.msgh_local_port = MACH_PORT_NULL;
        reply.Head.msgh_id = message.header.msgh_id + 100;
        reply.NDR = NDR_record;
        reply.RetCode = KERN_FAILURE;
        mach_msg(&reply.Head, MACH_SEND_MSG, sizeof(reply), 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    }
    return 0;
}

static void installPort(void)
{
    mach_port_t task = mach_task_self();
    mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &exceptionPort);
    mach_port_insert_right(task, exceptionPort, exceptionPort, MACH_MSG_TYPE_MAKE_SEND);
    task_set_exception_ports(task, EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION, exceptionPort, EXCEPTION_DEFAULT, THREAD_STATE_NONE);
    pthread_t thread;
    pthread_create(&thread, 0, handlerThread, 0);
}

static void removePort(void)
{
    task_set_exception_ports(mach_task_self(), EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION, MACH_PORT_NULL, EXCEPTION_DEFAULT, THREAD_STATE_NONE);
}

static void onSignal(int signo)
{
    signalSawException = signo;
    siglongjmp(jumpBuffer, 1);
}

/* Run `code` (a byte sequence ending in ret) out of an RWX mapping. */
static void runInJITMemory(const char* what, const unsigned char* code, size_t size)
{
    void* memory = mmap(0, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    memcpy(memory, code, size);
    portSawException = 0;
    signalSawException = 0;
    printf("%s\n", what);
    fflush(stdout);
    if (!sigsetjmp(jumpBuffer, 1))
        ((void (*)(void))memory)();
    printf("  port saw it: %d   posix signal: %d\n", portSawException, signalSawException);
    fflush(stdout);
    munmap(memory, 4096);
}

int main(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = onSignal;
    sigaction(SIGILL, &action, 0);
    sigaction(SIGSEGV, &action, 0);
    sigaction(SIGBUS, &action, 0);
    sigaction(SIGTRAP, &action, 0);

    static const unsigned char halt[] = { 0xf4, 0xc3 };                         /* hlt; ret */
    static const unsigned char nullLoad[] = { 0x48, 0x31, 0xc0, 0x48, 0x8b, 0x00, 0xc3 }; /* xor rax,rax; mov (rax),rax; ret */
    static const unsigned char ud2[] = { 0x0f, 0x0b, 0xc3 };

    printf("== no exception port (POSIX only)\n");
    runInJITMemory("hlt", halt, sizeof(halt));
    runInJITMemory("ud2", ud2, sizeof(ud2));
    runInJITMemory("null load", nullLoad, sizeof(nullLoad));

    printf("== with a task exception port (as TigerCrashCatcher installs it)\n");
    installPort();
    runInJITMemory("hlt", halt, sizeof(halt));
    runInJITMemory("ud2", ud2, sizeof(ud2));
    runInJITMemory("null load", nullLoad, sizeof(nullLoad));
    removePort();
    printf("done\n");
    return 0;
}
