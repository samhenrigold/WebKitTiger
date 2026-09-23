/* Exercise the actual shared header, without WebKit or CoreGraphics. */
#include TVR_TEST_HEADER
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static void fill(uint8_t* bytes, size_t size, uint32_t frame)
{
    size_t i;
    uint32_t* words = (uint32_t*)bytes;
    for (i = 0; i < size / sizeof(uint32_t); ++i)
        words[i] = frame ^ ((uint32_t)i * 2654435761u);
}

static void verify(const uint8_t* bytes, size_t size)
{
    size_t i;
    const uint32_t* words = (const uint32_t*)bytes;
    uint32_t frame = words[0];
    for (i = 0; i < size / sizeof(uint32_t); ++i)
        assert(words[i] == (frame ^ ((uint32_t)i * 2654435761u)));
}

static TigerVideoRing* newRing(TigerVideoRingLayout* layout, uint32_t width, uint32_t height)
{
    TigerVideoRing* ring;
    assert(tvrCheckedLayout(width, height, width * 4, layout));
    ring = calloc(1, layout->mapBytes);
    assert(ring);
    tvrInitializeRing(ring, layout);
    return ring;
}

static void bounds(void)
{
    TigerVideoRingLayout layout, actual;
    TigerVideoRing* ring = newRing(&layout, 16, 16);
    uint8_t* pixels = malloc(layout.slotBytes);
    uint8_t* output = malloc(layout.slotBytes + 16);
    long pageSize = sysconf(_SC_PAGESIZE);
    void* inaccessible = mmap(NULL, (size_t)pageSize, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(inaccessible != MAP_FAILED);
    assert(!tvrValidateRing(inaccessible, TVR_HEADER_BYTES - 1, &actual));
    assert(!tvrCheckedLayout(0, 1, 4, &actual));
    assert(!tvrCheckedLayout(1, 0, 4, &actual));
    assert(!tvrCheckedLayout(16, 16, 63, &actual));
    assert(!tvrCheckedLayout(INT32_MAX, 1, UINT32_MAX, &actual));
    assert(!tvrCheckedLayout(1, UINT32_MAX, 4, &actual));
    assert(!tvrCheckedLayout(1, INT32_MAX, UINT32_MAX, &actual));
    assert(tvrValidateRing(ring, layout.mapBytes, &actual));
    assert(!tvrValidateRing(ring, layout.mapBytes - 1, &actual));
    assert(!tvrValidateRing(ring, layout.mapBytes + 1, &actual));
    ring->magic = 0x54565231u;
    assert(!tvrValidateRing(ring, layout.mapBytes, &actual));
    ring->magic = TVR_MAGIC;
    ring->slots = TVR_SLOTS + 1;
    assert(!tvrValidateRing(ring, layout.mapBytes, &actual));
    ring->slots = TVR_SLOTS;
    ++ring->slotBytes;
    assert(!tvrValidateRing(ring, layout.mapBytes, &actual));
    --ring->slotBytes;
    fill(pixels, layout.slotBytes, 91);
    memset(output, 0xa5, layout.slotBytes + 16);
    assert(!tvrCopySnapshot(ring, &layout, output, layout.slotBytes, NULL));
    assert(!tvrPublishFrame(ring, &layout, pixels, layout.slotBytes - 1));
    assert(tvrPublishFrame(ring, &layout, pixels, layout.slotBytes));
    assert(!tvrCopySnapshot(ring, &layout, output, layout.slotBytes - 1, NULL));
    assert(output[0] == 0xa5);
    assert(tvrCopySnapshot(ring, &layout, output, layout.slotBytes, NULL));
    verify(output, layout.slotBytes);
    assert(output[layout.slotBytes] == 0xa5);
    ring->currentSlot = UINT32_MAX;
    assert(!tvrCopySnapshot(ring, &layout, output, layout.slotBytes, NULL));
    assert(!tvrPublishFrame(ring, &layout, pixels, layout.slotBytes));
    munmap(inaccessible, (size_t)pageSize);
    free(output);
    free(pixels);
    free(ring);
}

static void ownership(void)
{
    TigerVideoRingLayout layout;
    TigerVideoRing* ring = newRing(&layout, 32, 32);
    uint8_t* pixels = malloc(layout.slotBytes);
    uint8_t* snapshot = malloc(layout.slotBytes);
    uint32_t heldSlot, sequence, currentSlot, i;
    fill(pixels, layout.slotBytes, 7);
    assert(tvrPublishFrame(ring, &layout, pixels, layout.slotBytes));
    heldSlot = ring->currentSlot;
    __atomic_store_n(&ring->slotState[heldSlot], 1, __ATOMIC_RELEASE);
    for (i = 0; i < 1000; ++i) {
        fill(pixels, layout.slotBytes, i + 8);
        assert(tvrPublishFrame(ring, &layout, pixels, layout.slotBytes));
        assert(*(uint32_t*)tvrSlot(ring, &layout, heldSlot) == 7);
    }
    __atomic_fetch_sub(&ring->slotState[heldSlot], 1, __ATOMIC_RELEASE);
    assert(tvrCopySnapshot(ring, &layout, snapshot, layout.slotBytes, &sequence));
    verify(snapshot, layout.slotBytes);

    // A crashed reader can strand every spare slot; the producer must return
    // promptly without touching any pinned bytes or its current publication.
    currentSlot = ring->currentSlot;
    for (i = 0; i < TVR_SLOTS; ++i)
        __atomic_store_n(&ring->slotState[i], i == currentSlot ? 0 : 1, __ATOMIC_RELEASE);
    for (i = 0; i < TVR_RECREATE_AFTER_DROPS; ++i)
        assert(!tvrPublishFrame(ring, &layout, pixels, layout.slotBytes));
    assert(ring->sequence == sequence);
    assert(tvrCopySnapshot(ring, &layout, snapshot, layout.slotBytes, NULL));

    __atomic_store_n(&ring->slotState[currentSlot], TVR_WRITING, __ATOMIC_RELEASE);
    assert(!tvrCopySnapshot(ring, &layout, snapshot, layout.slotBytes, NULL));
    __atomic_store_n(&ring->slotState[currentSlot], 0, __ATOMIC_RELEASE);
    ++ring->slotGeneration[currentSlot];
    assert(!tvrCopySnapshot(ring, &layout, snapshot, layout.slotBytes, NULL));

    // Ring replacement recovers abandoned pins; old owned snapshots remain valid.
    free(ring);
    verify(snapshot, layout.slotBytes);
    ring = newRing(&layout, 32, 32);
    ring->sequence = UINT32_MAX;
    fill(pixels, layout.slotBytes, 55);
    assert(tvrPublishFrame(ring, &layout, pixels, layout.slotBytes));
    assert(ring->sequence == 1);
    assert(tvrCopySnapshot(ring, &layout, snapshot, layout.slotBytes, &sequence));
    assert(sequence == 1);
    verify(snapshot, layout.slotBytes);
    free(ring);
    free(snapshot);
    free(pixels);
}

struct Stress {
    TigerVideoRing* ring;
    TigerVideoRingLayout layout;
    uint32_t start, done, copied;
};

static void* reader(void* argument)
{
    struct Stress* stress = argument;
    uint8_t* snapshot = malloc(stress->layout.slotBytes);
    unsigned i = 0;
    assert(snapshot);
    while (!__atomic_load_n(&stress->start, __ATOMIC_ACQUIRE))
        sched_yield();
    do {
        if (tvrCopySnapshot(stress->ring, &stress->layout, snapshot, stress->layout.slotBytes, NULL)) {
            verify(snapshot, stress->layout.slotBytes);
            __atomic_fetch_add(&stress->copied, 1, __ATOMIC_RELAXED);
        }
        if (!(++i % 17))
            sched_yield();
    } while (!__atomic_load_n(&stress->done, __ATOMIC_ACQUIRE));
    free(snapshot);
    return NULL;
}

static void concurrent(void)
{
    struct Stress stress = { 0 };
    pthread_t readers[4];
    uint8_t* pixels;
    uint32_t frame, i;
    stress.ring = newRing(&stress.layout, 128, 64);
    pixels = malloc(stress.layout.slotBytes);
    assert(pixels);
    for (i = 0; i < 4; ++i)
        assert(!pthread_create(&readers[i], NULL, reader, &stress));
    __atomic_store_n(&stress.start, 1, __ATOMIC_RELEASE);
    for (frame = 1; frame <= 12000; ++frame) {
        fill(pixels, stress.layout.slotBytes, frame);
        tvrPublishFrame(stress.ring, &stress.layout, pixels, stress.layout.slotBytes);
        if (!(frame % 19))
            sched_yield();
    }
    __atomic_store_n(&stress.done, 1, __ATOMIC_RELEASE);
    for (i = 0; i < 4; ++i)
        assert(!pthread_join(readers[i], NULL));
    assert(stress.copied > 100);
    printf("validated %u concurrent snapshots\n", stress.copied);
    free(pixels);
    free(stress.ring);
}

static void resize(void)
{
    uint32_t iteration;
    for (iteration = 1; iteration <= 50; ++iteration) {
        TigerVideoRingLayout layout, consumerLayout;
        TigerVideoRing *producer, *consumer;
        uint8_t *pixels, *snapshot;
        char path[] = "/tmp/tvr-test-XXXXXX";
        int fd = mkstemp(path);
        assert(fd >= 0);
        assert(tvrCheckedLayout(16 + iteration * 4, 8 + iteration, (16 + iteration * 4) * 4, &layout));
        assert(!ftruncate(fd, layout.mapBytes));
        producer = mmap(NULL, layout.mapBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        consumer = mmap(NULL, layout.mapBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        assert(producer != MAP_FAILED && consumer != MAP_FAILED);
        close(fd);
        tvrInitializeRing(producer, &layout);
        assert(tvrValidateRing(consumer, layout.mapBytes, &consumerLayout));
        pixels = malloc(layout.slotBytes);
        snapshot = malloc(layout.slotBytes);
        fill(pixels, layout.slotBytes, iteration);
        assert(tvrPublishFrame(producer, &layout, pixels, layout.slotBytes));
        assert(!unlink(path));
        assert(open(path, O_RDWR) < 0); // a queued stale replacement path
        assert(!munmap(producer, layout.mapBytes));
        // An old consumer mapping still works after producer resize/unlink.
        assert(tvrCopySnapshot(consumer, &consumerLayout, snapshot, layout.slotBytes, NULL));
        assert(!munmap(consumer, layout.mapBytes));
        verify(snapshot, layout.slotBytes); // provider-owned bytes outlive mappings
        free(snapshot);
        free(pixels);
    }
}

static void crashedReaders(void)
{
    TigerVideoRingLayout layout;
    TigerVideoRing* ring;
    uint8_t* pixels;
    uint32_t i;
    assert(tvrCheckedLayout(32, 32, 128, &layout));
    ring = mmap(NULL, layout.mapBytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    assert(ring != MAP_FAILED);
    tvrInitializeRing(ring, &layout);
    pixels = malloc(layout.slotBytes);
    fill(pixels, layout.slotBytes, 99);
    assert(tvrPublishFrame(ring, &layout, pixels, layout.slotBytes));
    for (i = 0; i < TVR_SLOTS - 1; ++i) {
        int status;
        pid_t child = fork();
        assert(child >= 0);
        if (!child) {
            uint32_t slot = __atomic_load_n(&ring->currentSlot, __ATOMIC_RELAXED);
            __atomic_fetch_add(&ring->slotState[slot], 1, __ATOMIC_ACQUIRE);
            _exit(0); // simulate a process dying without releasing its pin
        }
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
        assert(tvrPublishFrame(ring, &layout, pixels, layout.slotBytes));
    }
    for (i = 0; i < TVR_RECREATE_AFTER_DROPS; ++i)
        assert(!tvrPublishFrame(ring, &layout, pixels, layout.slotBytes));
    assert(!munmap(ring, layout.mapBytes));
    ring = newRing(&layout, 32, 32);
    assert(tvrPublishFrame(ring, &layout, pixels, layout.slotBytes));
    free(ring);
    free(pixels);
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "bounds"))
        bounds();
    else if (!strcmp(argv[1], "ownership"))
        ownership();
    else if (!strcmp(argv[1], "concurrent"))
        concurrent();
    else if (!strcmp(argv[1], "resize"))
        resize();
    else if (!strcmp(argv[1], "crashed-readers"))
        crashedReaders();
    else
        return 2;
    return 0;
}
