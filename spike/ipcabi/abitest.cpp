/* Two processes, one 32-bit and one 64-bit, encoding and decoding the same message through a shared
 * buffer. Built twice: once against the patched wire alignment and once against the unpatched one. */
#include "abiwire.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

static uint8_t *shmem;

static void diffTables(const AbiFieldTable *enc, const AbiFieldTable *dec, AbiResult *r)
{
    r->checked = enc->count < dec->count ? enc->count : dec->count;
    r->mismatched = 0;
    size_t used = 0;
    r->detail[0] = '\0';
    for (uint32_t i = 0; i < r->checked; ++i) {
        if (enc->fields[i].offset == dec->fields[i].offset) continue;
        ++r->mismatched;
        if (used < sizeof(r->detail) - 80) {
            int n = snprintf(r->detail + used, sizeof(r->detail) - used,
                             "%s enc@%u dec@%u; ", enc->fields[i].name,
                             enc->fields[i].offset, dec->fields[i].offset);
            if (n > 0) used += (size_t)n;
        }
    }
}

/* Decode what the peer left in the buffer, check it, and write the verdict back. */
static void decodeAndReport(uint32_t encodedSize)
{
    AbiMessage expected, got;
    abiFillMessage(&expected);
    static AbiFieldTable decTable;
    uint32_t end = abiDecodeMessage(shmem + ABI_MSG_OFF, ABI_MSG_MAX, &got, &decTable);

    AbiResult *r = (AbiResult *)(shmem + ABI_RESULT_OFF);
    memset(r, 0, sizeof(*r));
    r->decoded = abiMessagesEqual(&expected, &got) ? 1u : 0u;
    r->totalEncoded = encodedSize;
    r->totalDecoded = end;
    diffTables((const AbiFieldTable *)(shmem + ABI_TABLE_OFF), &decTable, r);
}

static uint32_t encodeInto(void)
{
    AbiMessage m;
    abiFillMessage(&m);
    AbiFieldTable *t = (AbiFieldTable *)(shmem + ABI_TABLE_OFF);
    return abiEncodeMessage(shmem + ABI_MSG_OFF, &m, t);
}

static void printVerdict(const char *direction)
{
    const AbiResult *r = (const AbiResult *)(shmem + ABI_RESULT_OFF);
    printf("%-22s %-9s  fields=%u mismatched=%u  encoded=%u decoded=%u  values=%s\n",
           direction, ABI_BUILD_NAME, r->checked, r->mismatched,
           r->totalEncoded, r->totalDecoded, r->decoded ? "OK" : "CORRUPT");
    if (r->mismatched)
        printf("    offsets that differ: %s\n", r->detail);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    int bits = (int)(sizeof(void *) * 8);

    if (argc >= 4 && !strcmp(argv[1], "child")) {
        const char *name = argv[2];
        int fd = atoi(argv[3]);
        int shmfd = shm_open(name, O_RDWR, 0600);
        if (shmfd < 0) { fprintf(stderr, "child shm_open: %s\n", strerror(errno)); return 1; }
        shmem = (uint8_t *)mmap(NULL, ABI_SHM_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
        close(shmfd);
        if (shmem == MAP_FAILED) { fprintf(stderr, "child mmap failed\n"); return 1; }

        uint32_t n = 0;
        if (read(fd, &n, sizeof(n)) != (ssize_t)sizeof(n)) return 1;
        decodeAndReport(n);                      /* round 1: peer encoded, we decode */
        uint32_t ack = 1;
        write(fd, &ack, sizeof(ack));

        if (read(fd, &ack, sizeof(ack)) != (ssize_t)sizeof(ack)) return 1;
        n = encodeInto();                        /* round 2: we encode, peer decodes */
        write(fd, &n, sizeof(n));
        read(fd, &ack, sizeof(ack));
        return 0;
    }

    /* parent */
    const char *childPath = (argc > 1) ? argv[1] : "./abitest64";
    char name[64];
    snprintf(name, sizeof(name), "/wktabi%d", (int)getpid());
    shm_unlink(name);
    int shmfd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (shmfd < 0) { fprintf(stderr, "shm_open: %s\n", strerror(errno)); return 1; }
    if (ftruncate(shmfd, ABI_SHM_BYTES) != 0) { fprintf(stderr, "ftruncate: %s\n", strerror(errno)); return 1; }
    shmem = (uint8_t *)mmap(NULL, ABI_SHM_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
    close(shmfd);
    if (shmem == MAP_FAILED) { fprintf(stderr, "mmap failed\n"); return 1; }

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { perror("socketpair"); return 1; }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (!pid) {
        close(sv[0]);
        char fdstr[16];
        snprintf(fdstr, sizeof(fdstr), "%d", sv[1]);
        execl(childPath, childPath, "child", name, fdstr, (char *)NULL);
        perror("execl");
        _exit(127);
    }
    close(sv[1]);

    printf("parent is %d-bit, child is the other; build is %s\n", bits, ABI_BUILD_NAME);

    uint32_t n = encodeInto();                   /* round 1 */
    write(sv[0], &n, sizeof(n));
    uint32_t ack = 0;
    read(sv[0], &ack, sizeof(ack));
    printVerdict(bits == 32 ? "i386 -> x86_64" : "x86_64 -> i386");

    ack = 1;                                     /* round 2 */
    write(sv[0], &ack, sizeof(ack));
    if (read(sv[0], &n, sizeof(n)) == (ssize_t)sizeof(n)) {
        decodeAndReport(n);
        printVerdict(bits == 32 ? "x86_64 -> i386" : "i386 -> x86_64");
    }
    write(sv[0], &ack, sizeof(ack));

    int st = 0;
    waitpid(pid, &st, 0);
    shm_unlink(name);
    return 0;
}
