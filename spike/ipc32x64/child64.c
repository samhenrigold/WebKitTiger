/* 64-bit content-process stand-in. Finds the parent's port through the inherited bootstrap
 * namespace, then serves whatever the parent asks for: RPC, bulk, OOL, and shared-memory frames. */
#include "common.h"
#include <mach/mach_vm.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

static mach_port_t parentPort;
static mach_port_t myPort;
static uint8_t *shmemMach;
static uint8_t *shmemPosix;
static const char *shmName;

static void fail(const char *what, kern_return_t kr)
{
    fprintf(stderr, "child64: %s: %s (0x%x)\n", what, ipcErr(kr), kr);
    exit(1);
}

static void sendSimple(uint32_t op, uint32_t seq, uint64_t value)
{
    IPCSimpleMsg m;
    memset(&m, 0, sizeof(m));
    m.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    m.header.msgh_size = sizeof(m);
    m.header.msgh_remote_port = parentPort;
    m.header.msgh_id = (int)op;   /* fixed offset in every message shape; op fields are not */
    m.op = op; m.seq = seq; m.value = value;
    kern_return_t kr = mach_msg(&m.header, MACH_SEND_MSG, sizeof(m), 0, MACH_PORT_NULL,
                                MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) fail("sendSimple", kr);
}

/* One frame of synthetic BGRA written straight into the shared region, so the cost measured is a
 * real write of 5.2 MB rather than a memset the compiler could sink. */
static void paintFrame(uint8_t *dst, uint32_t seq)
{
    uint32_t *px = (uint32_t *)dst;
    uint32_t base = 0xff000000u | (seq * 2654435761u);
    for (uint32_t i = 0; i < IPC_FRAME_W * IPC_FRAME_H; ++i) px[i] = base ^ i;
}

/* Allocate the double buffer here (the producer side owns it) and hand the parent a memory entry. */
static void setUpSharedMemory(void)
{
    mach_vm_address_t addr = 0;
    kern_return_t kr = mach_vm_allocate(mach_task_self(), &addr, IPC_SHM_BYTES, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) fail("mach_vm_allocate", kr);
    shmemMach = (uint8_t *)(uintptr_t)addr;

    memory_object_size_t size = IPC_SHM_BYTES;
    mach_port_t entry = MACH_PORT_NULL;
    kr = mach_make_memory_entry_64(mach_task_self(), &size, addr,
                                   VM_PROT_READ | VM_PROT_WRITE, &entry, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) fail("mach_make_memory_entry_64", kr);
    printf("child64: memory entry 0x%x for %u bytes at %#llx\n",
           entry, (unsigned)IPC_SHM_BYTES, (unsigned long long)addr);

    IPCPortMsg m;
    memset(&m, 0, sizeof(m));
    m.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0) | MACH_MSGH_BITS_COMPLEX;
    m.header.msgh_size = sizeof(m);
    m.header.msgh_remote_port = parentPort;
    m.body.msgh_descriptor_count = 1;
    m.port.name = entry;
    m.port.disposition = MACH_MSG_TYPE_COPY_SEND;
    m.port.type = MACH_MSG_PORT_DESCRIPTOR;
    m.op = IPC_MSG_SHMEM;
    m.header.msgh_id = IPC_MSG_SHMEM;
    kr = mach_msg(&m.header, MACH_SEND_MSG, sizeof(m), 0, MACH_PORT_NULL,
                  MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) fail("send memory entry", kr);
}

/* Double-buffered production: paint buffer i%2, announce it, and only wait for the ack of frame
 * i-1 before painting i+1, so the parent's copy of frame i overlaps our paint of frame i+1. */
/* POSIX shared memory, the baseline the audio bridge established. Same size, same access pattern,
 * so the only difference from the mach path is how the pages were obtained. */
static void setUpPosixSharedMemory(void)
{
    shm_unlink(shmName); /* a previous run may have left it behind */
    int fd = shm_open(shmName, O_CREAT | O_RDWR, 0600);
    if (fd < 0) { printf("child64: shm_open failed (%s); POSIX path unavailable\n", strerror(errno)); return; }
    if (ftruncate(fd, IPC_SHM_BYTES) != 0) {
        printf("child64: ftruncate to %u failed (%s); POSIX path unavailable\n",
               (unsigned)IPC_SHM_BYTES, strerror(errno));
        close(fd); shm_unlink(shmName); return;
    }
    void *p = mmap(NULL, IPC_SHM_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) { printf("child64: mmap failed (%s)\n", strerror(errno)); shm_unlink(shmName); return; }
    shmemPosix = (uint8_t *)p;
    printf("child64: POSIX shm %s mapped at %p\n", shmName, p);
}

static void produceFrames(uint32_t count, uint32_t usePosix)
{
    uint8_t *region = usePosix ? shmemPosix : shmemMach;
    if (!region) { sendSimple(IPC_MSG_PONG, count, 0); return; }
    double paintTotal = 0;
    uint32_t acked = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (i >= IPC_BUFFERS) {
            IPCSimpleRcv ack;
            kern_return_t kr = mach_msg(&ack.header, MACH_RCV_MSG, 0, sizeof(ack), myPort,
                                        MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
            if (kr != KERN_SUCCESS) fail("recv ack", kr);
            acked = ack.seq;
        }
        double t0 = ipcNowSeconds();
        paintFrame(region + (i % IPC_BUFFERS) * IPC_FRAME_BYTES, i);
        paintTotal += ipcNowSeconds() - t0;
        sendSimple(IPC_MSG_FRAME, i, i % IPC_BUFFERS);
    }
    while (acked < count - 1) {
        IPCSimpleRcv ack;
        if (mach_msg(&ack.header, MACH_RCV_MSG, 0, sizeof(ack), myPort,
                     MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL) != KERN_SUCCESS) break;
        acked = ack.seq;
    }
    printf("child64: paint %.2f ms/frame (%.1f MB/s write into %s)\n",
           paintTotal * 1000.0 / count,
           (double)count * IPC_FRAME_BYTES / (1024.0 * 1024.0) / paintTotal,
           usePosix ? "POSIX shm" : "mach memory entry");
    sendSimple(IPC_MSG_PONG, count, 0);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    if (argc < 3) { fprintf(stderr, "child64: need service name and shm name\n"); return 1; }
    shmName = argv[2];

    printf("child64: %d-bit, pid %d\n", (int)(sizeof(void *) * 8), (int)getpid());
    printf("child64: sizeof header=%u simple=%u portmsg=%u ool_desc=%u\n",
           (unsigned)sizeof(mach_msg_header_t), (unsigned)sizeof(IPCSimpleMsg),
           (unsigned)sizeof(IPCPortMsg), (unsigned)sizeof(mach_msg_ool_descriptor_t));

    kern_return_t kr = bootstrap_look_up(bootstrap_port, argv[1], &parentPort);
    if (kr != KERN_SUCCESS) fail("bootstrap_look_up", kr);

    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &myPort);
    if (kr != KERN_SUCCESS) fail("mach_port_allocate", kr);
    kr = mach_port_insert_right(mach_task_self(), myPort, myPort, MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) fail("mach_port_insert_right", kr);

    /* Hand the parent a send right to us, and separately our task port so it can install
     * exception ports for crash isolation without needing task_for_pid. */
    IPCPortMsg hello;
    memset(&hello, 0, sizeof(hello));
    hello.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0) | MACH_MSGH_BITS_COMPLEX;
    hello.header.msgh_size = sizeof(hello);
    hello.header.msgh_remote_port = parentPort;
    hello.body.msgh_descriptor_count = 1;
    hello.port.name = myPort;
    hello.port.disposition = MACH_MSG_TYPE_COPY_SEND;
    hello.port.type = MACH_MSG_PORT_DESCRIPTOR;
    hello.op = IPC_MSG_PORT;
    hello.header.msgh_id = IPC_MSG_PORT;
    kr = mach_msg(&hello.header, MACH_SEND_MSG, sizeof(hello), 0, MACH_PORT_NULL,
                  MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) fail("send hello", kr);

    hello.port.name = mach_task_self();
    hello.op = IPC_MSG_TASKPORT;
    hello.header.msgh_id = IPC_MSG_TASKPORT;
    kr = mach_msg(&hello.header, MACH_SEND_MSG, sizeof(hello), 0, MACH_PORT_NULL,
                  MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) fail("send task port", kr);

    setUpSharedMemory();
    setUpPosixSharedMemory();

    for (;;) {
        union {
            mach_msg_header_t header;
            IPCSimpleRcv simple;
            IPCBulkRcv bulk;
            IPCOolRcv ool;
        } in;
        kr = mach_msg(&in.header, MACH_RCV_MSG, 0, sizeof(in), myPort,
                      MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        if (kr != KERN_SUCCESS) fail("recv", kr);

        switch (in.header.msgh_id) {
        case IPC_MSG_PING:
            sendSimple(IPC_MSG_PONG, in.simple.seq, in.simple.value + 1);
            break;
        case IPC_MSG_BULK:
            sendSimple(IPC_MSG_PONG, in.bulk.seq,
                       (uint64_t)in.bulk.payload[0] + in.bulk.payload[IPC_BULK_BYTES - 1]);
            break;
        case IPC_MSG_OOL: {
            /* If the kernel did not translate the descriptor across the ABI split, address or
             * size would be garbage here. Report what actually arrived. */
            uint32_t got = in.ool.body.msgh_descriptor_count;
            uint64_t addr = (uint64_t)(uintptr_t)in.ool.ool.address;
            uint32_t size = in.ool.ool.size;
            uint8_t first = size ? ((uint8_t *)in.ool.ool.address)[0] : 0xff;
            uint8_t last = size ? ((uint8_t *)in.ool.ool.address)[size - 1] : 0xff;
            printf("child64: OOL descriptors=%u size=%u addr=%#llx first=%u last=%u\n",
                   got, size, (unsigned long long)addr, first, last);
            if (addr) vm_deallocate(mach_task_self(), (vm_address_t)addr, size);
            sendSimple(IPC_MSG_PONG, in.ool.seq, (uint64_t)size << 16 | first);
            break;
        }
        case IPC_MSG_START:
            produceFrames((uint32_t)in.simple.value, in.simple.seq);
            break;
        case IPC_MSG_CRASH:
            printf("child64: crashing on purpose\n");
            *(volatile int *)0 = 1;
            break;
        case IPC_MSG_QUIT:
            printf("child64: quitting\n");
            if (shmemPosix) shm_unlink(shmName);
            return 0;
        default:
            break;
        }
    }
}
