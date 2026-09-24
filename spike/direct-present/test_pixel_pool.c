/* Host-only ownership/lifetime check of the exact probe pool. */
#include "PixelBufferPool.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct Lease { void* info; void* bytes; size_t length; };
static void* releaseWorker(void* value)
{
    struct Lease* lease = value;
    pixelPoolRelease(lease->info, lease->bytes, lease->length);
    return NULL;
}
int main(void)
{
    struct PixelBufferPool* pool = pixelPoolCreate();
    assert(pool);
    struct Lease held[4];
    for (unsigned i = 0; i < 4; ++i) {
        held[i].length = 4096;
        held[i].bytes = pixelPoolAcquire(pool, held[i].length, &held[i].info);
        assert(held[i].bytes);
        memset(held[i].bytes, i + 1, held[i].length);
        for (unsigned j = 0; j < i; ++j) {
            assert(held[j].bytes != held[i].bytes);
            assert(((unsigned char*)held[j].bytes)[0] == j + 1);
        }
    }
    assert(!held[3].info); // Exhaustion allocates private fallback, never steals.
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, releaseWorker, &held[1]));
    assert(!pthread_join(worker, NULL));
    void* info;
    void* reused = pixelPoolAcquire(pool, 4096, &info);
    assert(reused == held[1].bytes && info == held[1].info);
    memset(reused, 99, 4096);
    assert(((unsigned char*)held[0].bytes)[0] == 1);
    assert(((unsigned char*)held[2].bytes)[0] == 3);
    struct PixelBufferPoolStats stats = pixelPoolStats(pool);
    assert(stats.allocations == 3 && stats.reuses == 1 && stats.fallbacks == 1 && stats.peakInUse == 3);
    pixelPoolRelease(info, reused, 4096);
    void* resized = pixelPoolAcquire(pool, 8192, &info);
    assert(resized);
    memset(resized, 55, 8192);
    // Close while providers still retain slots. Their callbacks must be safe
    // after the caller has destroyed its owner, even from a different thread.
    pixelPoolClose(pool);
    assert(((unsigned char*)held[0].bytes)[0] == 1);
    assert(((unsigned char*)held[2].bytes)[0] == 3);
    pixelPoolRelease(info, resized, 8192);
    assert(!pthread_create(&worker, NULL, releaseWorker, &held[0]));
    pixelPoolRelease(held[2].info, held[2].bytes, held[2].length);
    assert(!pthread_join(worker, NULL));
    pixelPoolRelease(held[3].info, held[3].bytes, held[3].length);
    puts("PASS pinned ownership, bounded fallback, reuse, resize, asynchronous close");
    return 0;
}
