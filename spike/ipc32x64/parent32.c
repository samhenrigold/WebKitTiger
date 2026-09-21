/* 32-bit UI-process stand-in: launches the 64-bit child, hands it a port through the bootstrap
 * namespace, then measures the IPC and shared-memory paths the drawing area would use. */
#include "common.h"
#include <mach/mach_vm.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

static mach_port_t myPort;
static mach_port_t childPort;
static mach_port_t childTask;
static mach_port_t excPort;
static pid_t childPid;
static uint8_t *shmem;

#ifdef NO_GL
static void glUploadBenchmark(const uint8_t *p) { (void)p; printf("RESULT gl_upload skipped (NO_GL build)\n"); }
#else
void glUploadBenchmark(const uint8_t *pixels); /* glupload.c */
#endif

static void fail(const char *what, kern_return_t kr)
{
    fprintf(stderr, "parent32: %s: %s (0x%x)\n", what, ipcErr(kr), kr);
    exit(1);
}

static void rpcPing(uint32_t seq, uint64_t value, uint64_t *out)
{
    union { IPCSimpleMsg send; IPCSimpleRcv rcv; } m;
    memset(&m, 0, sizeof(m));
    m.send.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    m.send.header.msgh_size = sizeof(IPCSimpleMsg);
    m.send.header.msgh_remote_port = childPort;
    m.send.header.msgh_local_port = MACH_PORT_NULL;
    m.send.op = IPC_MSG_PING;
    m.send.header.msgh_id = IPC_MSG_PING;
    m.send.seq = seq;
    m.send.value = value;
    kern_return_t kr = mach_msg(&m.send.header, MACH_SEND_MSG | MACH_RCV_MSG,
                                sizeof(IPCSimpleMsg), sizeof(IPCSimpleRcv), myPort,
                                MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) fail("rpc ping", kr);
    if (out) *out = m.rcv.value;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    const char *childPath = (argc > 1) ? argv[1] : "./child64";

    printf("parent32: %d-bit, pid %d\n", (int)(sizeof(void *) * 8), (int)getpid());
    printf("parent32: sizeof header=%u simple=%u portmsg=%u ool_desc=%u\n",
           (unsigned)sizeof(mach_msg_header_t), (unsigned)sizeof(IPCSimpleMsg),
           (unsigned)sizeof(IPCPortMsg), (unsigned)sizeof(mach_msg_ool_descriptor_t));

    kern_return_t kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &myPort);
    if (kr != KERN_SUCCESS) fail("mach_port_allocate", kr);
    kr = mach_port_insert_right(mach_task_self(), myPort, myPort, MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) fail("mach_port_insert_right", kr);

    char service[128];
    snprintf(service, sizeof(service), "org.webkittiger.ipcspike.%d", (int)getpid());
    kr = bootstrap_register(bootstrap_port, service, myPort);
    if (kr != KERN_SUCCESS) fail("bootstrap_register", kr);
    printf("parent32: registered %s\n", service);

    childPid = fork();  /* Tiger has no posix_spawn */
    if (childPid < 0) { perror("fork"); return 1; }
    if (childPid == 0) {
        execl(childPath, childPath, service, (char *)NULL);
        perror("execl");
        _exit(127);
    }

    /* Setup messages from the child: its receive port, its task port, and the memory entry. */
    mach_port_t memEntry = MACH_PORT_NULL;
    for (int got = 0; got < 3; ++got) {
        IPCPortRcv in;
        memset(&in, 0, sizeof(in));
        kr = mach_msg(&in.header, MACH_RCV_MSG, 0, sizeof(in), myPort,
                      MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        if (kr != KERN_SUCCESS) fail("recv setup", kr);
        if (in.header.msgh_id == IPC_MSG_PORT) childPort = in.port.name;
        else if (in.header.msgh_id == IPC_MSG_TASKPORT) childTask = in.port.name;
        else if (in.header.msgh_id == IPC_MSG_SHMEM) memEntry = in.port.name;
        else { fprintf(stderr, "parent32: unexpected setup id %d\n", in.header.msgh_id); return 1; }
    }
    printf("parent32: child port 0x%x, task port 0x%x, memory entry 0x%x\n",
           childPort, childTask, memEntry);

    /* Map the 64-bit process's region into our 32-bit address space. */
    vm_address_t mapped = 0;
    kr = vm_map(mach_task_self(), &mapped, IPC_SHM_BYTES, 0, VM_FLAGS_ANYWHERE,
                memEntry, 0, FALSE, VM_PROT_READ | VM_PROT_WRITE,
                VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS) fail("vm_map of 64-bit memory entry", kr);
    shmem = (uint8_t *)mapped;
    printf("parent32: mapped %u bytes at %p\n", (unsigned)IPC_SHM_BYTES, shmem);

    /* --- round-trip latency --- */
    uint64_t v = 0;
    for (int i = 0; i < 200; ++i) rpcPing(i, i, &v); /* warm up */
    const int iterations = 10000;
    double t0 = ipcNowSeconds();
    for (int i = 0; i < iterations; ++i) rpcPing(i, (uint64_t)i, &v);
    double t1 = ipcNowSeconds();
    printf("RESULT rtt_us %.2f   (%d iterations, one mach_msg send+recv per trip)\n",
           (t1 - t0) * 1e6 / iterations, iterations);

    /* --- 64 KB inline throughput, one way plus a small ack --- */
    static IPCBulkMsg bulk;
    memset(&bulk, 0, sizeof(bulk));
    bulk.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    bulk.header.msgh_size = sizeof(IPCBulkMsg);
    bulk.header.msgh_remote_port = childPort;
    bulk.op = IPC_MSG_BULK;
    bulk.header.msgh_id = IPC_MSG_BULK;
    for (int i = 0; i < IPC_BULK_BYTES; ++i) bulk.payload[i] = (uint8_t)i;

    const int bulkIterations = 2000;
    t0 = ipcNowSeconds();
    for (int i = 0; i < bulkIterations; ++i) {
        union { IPCBulkMsg send; IPCSimpleRcv rcv; } m;
        memcpy(&m.send, &bulk, sizeof(bulk));
        m.send.seq = i;
        kr = mach_msg(&m.send.header, MACH_SEND_MSG | MACH_RCV_MSG,
                      sizeof(IPCBulkMsg), sizeof(IPCSimpleRcv), myPort,
                      MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        if (kr != KERN_SUCCESS) fail("bulk rpc", kr);
    }
    t1 = ipcNowSeconds();
    double secs = t1 - t0;
    printf("RESULT bulk64k_mb_s %.1f   rtt_us %.2f   (%d x 64 KB inline)\n",
           (double)bulkIterations * IPC_BULK_BYTES / (1024.0 * 1024.0) / secs,
           secs * 1e6 / bulkIterations, bulkIterations);

    /* --- out-of-line descriptor across the ABI split --- */
    {
        static uint8_t oolData[256 * 1024];
        for (size_t i = 0; i < sizeof(oolData); ++i) oolData[i] = (uint8_t)(i * 7);
        union { IPCOolMsg send; IPCSimpleRcv rcv; } m;
        memset(&m, 0, sizeof(m));
        m.send.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0) | MACH_MSGH_BITS_COMPLEX;
        m.send.header.msgh_size = sizeof(IPCOolMsg);
        m.send.header.msgh_remote_port = childPort;
        m.send.body.msgh_descriptor_count = 1;
        m.send.ool.address = oolData;
        m.send.ool.size = sizeof(oolData);
        m.send.ool.deallocate = FALSE;
        m.send.ool.copy = MACH_MSG_VIRTUAL_COPY;
        m.send.ool.type = MACH_MSG_OOL_DESCRIPTOR;
        m.send.op = IPC_MSG_OOL;
        m.send.header.msgh_id = IPC_MSG_OOL;
        kr = mach_msg(&m.send.header, MACH_SEND_MSG | MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                      sizeof(IPCOolMsg), sizeof(IPCSimpleRcv), myPort,
                      5000, MACH_PORT_NULL);
        if (kr != KERN_SUCCESS)
            printf("RESULT ool_32to64 FAILED %s (0x%x)\n", ipcErr(kr), kr);
        else
            printf("RESULT ool_32to64 ok, child saw size=%u first=%u (expect 262144 and 0)\n",
                   (unsigned)(m.rcv.value >> 16), (unsigned)(m.rcv.value & 0xff));
    }

    /* --- shared-memory frame pipeline --- */
    {
        const uint32_t frames = 120;
        static uint8_t *sink;
        if (!sink) sink = (uint8_t *)malloc(IPC_FRAME_BYTES);

        IPCSimpleMsg start;
        memset(&start, 0, sizeof(start));
        start.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
        start.header.msgh_size = sizeof(start);
        start.header.msgh_remote_port = childPort;
        start.op = IPC_MSG_START;
        start.header.msgh_id = IPC_MSG_START;
        start.value = frames;
        kr = mach_msg(&start.header, MACH_SEND_MSG, sizeof(start), 0, MACH_PORT_NULL,
                      MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        if (kr != KERN_SUCCESS) fail("send start", kr);

        double copyTotal = 0, firstFrame = 0, lastFrame = 0;
        uint32_t received = 0;
        while (received < frames) {
            IPCSimpleRcv in;
            kr = mach_msg(&in.header, MACH_RCV_MSG, 0, sizeof(in), myPort,
                          MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
            if (kr != KERN_SUCCESS) fail("recv frame", kr);
            if (in.header.msgh_id != IPC_MSG_FRAME) continue;
            if (!received) firstFrame = ipcNowSeconds();
            double c0 = ipcNowSeconds();
            memcpy(sink, shmem + in.value * IPC_FRAME_BYTES, IPC_FRAME_BYTES);
            copyTotal += ipcNowSeconds() - c0;
            lastFrame = ipcNowSeconds();
            ++received;
            IPCSimpleMsg ack;
            memset(&ack, 0, sizeof(ack));
            ack.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
            ack.header.msgh_size = sizeof(ack);
            ack.header.msgh_remote_port = childPort;
            ack.op = IPC_MSG_ACK;
            ack.header.msgh_id = IPC_MSG_ACK;
            ack.seq = in.seq;
            mach_msg(&ack.header, MACH_SEND_MSG, sizeof(ack), 0, MACH_PORT_NULL,
                     MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        }
        double wall = lastFrame - firstFrame;
        double mb = (double)IPC_FRAME_BYTES / (1024.0 * 1024.0);
        printf("RESULT frame_copy_ms %.2f   copy_mb_s %.1f   (32-bit side, %ux%u BGRA = %.2f MB)\n",
               copyTotal * 1000.0 / received, mb * received / copyTotal,
               IPC_FRAME_W, IPC_FRAME_H, mb);
        printf("RESULT pipeline_fps %.1f   pipeline_mb_s %.1f   (%u frames, double buffered)\n",
               (received - 1) / wall, mb * (received - 1) / wall, received);
        /* Drain the child's completion message. */
        IPCSimpleRcv done;
        mach_msg(&done.header, MACH_RCV_MSG, 0, sizeof(done), myPort, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    }

    /* --- OpenGL upload straight from the shared mapping --- */
    glUploadBenchmark(shmem);

    /* --- crash isolation: can we get the child's fault on an exception port? --- */
    {
        kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &excPort);
        if (kr == KERN_SUCCESS)
            kr = mach_port_insert_right(mach_task_self(), excPort, excPort, MACH_MSG_TYPE_MAKE_SEND);
        if (kr == KERN_SUCCESS)
            kr = task_set_exception_ports(childTask, EXC_MASK_BAD_ACCESS, excPort,
                                          EXCEPTION_DEFAULT, THREAD_STATE_NONE);
        if (kr != KERN_SUCCESS) {
            printf("RESULT exception_ports FAILED %s (0x%x)\n", ipcErr(kr), kr);
        } else {
            IPCSimpleMsg crash;
            memset(&crash, 0, sizeof(crash));
            crash.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
            crash.header.msgh_size = sizeof(crash);
            crash.header.msgh_remote_port = childPort;
            crash.op = IPC_MSG_CRASH;
            crash.header.msgh_id = IPC_MSG_CRASH;
            mach_msg(&crash.header, MACH_SEND_MSG, sizeof(crash), 0, MACH_PORT_NULL,
                     MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

            char buf[1024];
            mach_msg_header_t *hdr = (mach_msg_header_t *)buf;
            kr = mach_msg(hdr, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(buf), excPort,
                          5000, MACH_PORT_NULL);
            if (kr == KERN_SUCCESS)
                printf("RESULT exception_ports ok, got msgh_id %d from the 64-bit child (2401 = exception_raise)\n",
                       hdr->msgh_id);
            else
                printf("RESULT exception_ports no message: %s (0x%x)\n", ipcErr(kr), kr);
        }
        kill(childPid, SIGKILL);
        int st = 0; waitpid(childPid, &st, 0);
        printf("parent32: child reaped\n");
        return 0;
    }

    IPCSimpleMsg quit;
    memset(&quit, 0, sizeof(quit));
    quit.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    quit.header.msgh_size = sizeof(quit);
    quit.header.msgh_remote_port = childPort;
    quit.op = IPC_MSG_QUIT;
    quit.header.msgh_id = IPC_MSG_QUIT;
    mach_msg(&quit.header, MACH_SEND_MSG, sizeof(quit), 0, MACH_PORT_NULL,
             MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

    int status = 0;
    waitpid(childPid, &status, 0);
    printf("parent32: child exited status %d\n", WEXITSTATUS(status));
    return 0;
}
