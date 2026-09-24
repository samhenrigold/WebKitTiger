/* Probe-only owned image storage. Uses Tiger's public malloc/pthread APIs.
 * A checked-out buffer is unavailable until its provider release callback runs.
 * Closing drops the owner; outstanding providers keep both bytes and pool alive. */
#ifndef TIGER_PROBE_PIXEL_BUFFER_POOL_H
#define TIGER_PROBE_PIXEL_BUFFER_POOL_H
#include <pthread.h>
#include <stdlib.h>

#define PIXEL_POOL_SLOTS 3
struct PixelBufferPool;
struct PixelBufferSlot {
    struct PixelBufferPool* pool;
    void* bytes;
    size_t capacity;
    int busy;
};
struct PixelBufferPoolStats {
    unsigned allocations, reuses, fallbacks, inUse, peakInUse;
};
struct PixelBufferPool {
    pthread_mutex_t mutex;
    struct PixelBufferSlot slots[PIXEL_POOL_SLOTS];
    struct PixelBufferPoolStats stats;
    unsigned references;
    int closed;
};

static void pixelPoolRequire(int condition)
{
    // Tiger's SDK assert macro calls GCC's __eprintf helper. Keep the standalone
    // probe dependent only on libSystem and retain the check in release builds.
    if (!condition)
        abort();
}

static struct PixelBufferPool* pixelPoolCreate(void)
{
    struct PixelBufferPool* pool = calloc(1, sizeof(*pool));
    if (!pool)
        return NULL;
    if (pthread_mutex_init(&pool->mutex, NULL)) {
        free(pool);
        return NULL;
    }
    pool->references = 1;
    for (unsigned i = 0; i < PIXEL_POOL_SLOTS; ++i)
        pool->slots[i].pool = pool;
    return pool;
}

static void pixelPoolRelease(void* info, const void* bytes, size_t length)
{
    if (!info) {
        free((void*)bytes);
        return;
    }
    struct PixelBufferSlot* slot = info;
    struct PixelBufferPool* pool = slot->pool;
    pthread_mutex_lock(&pool->mutex);
    pixelPoolRequire(slot->busy && slot->bytes == bytes && slot->capacity == length);
    slot->busy = 0;
    --pool->stats.inUse;
    --pool->references;
    int closed = pool->closed;
    int destroy = !pool->references;
    if (closed)
        slot->bytes = NULL;
    pthread_mutex_unlock(&pool->mutex);
    if (closed)
        free((void*)bytes);
    if (destroy) {
        pthread_mutex_destroy(&pool->mutex);
        free(pool);
    }
}

static void* pixelPoolAcquire(struct PixelBufferPool* pool, size_t length, void** info)
{
    *info = NULL;
    pthread_mutex_lock(&pool->mutex);
    pixelPoolRequire(!pool->closed);
    struct PixelBufferSlot* slot = NULL;
    for (unsigned i = 0; i < PIXEL_POOL_SLOTS; ++i) {
        if (!pool->slots[i].busy) {
            slot = &pool->slots[i];
            break;
        }
    }
    if (!slot) {
        ++pool->stats.fallbacks;
        pthread_mutex_unlock(&pool->mutex);
        return malloc(length); // Never wait for or overwrite a retained image.
    }
    slot->busy = 1;
    ++pool->references;
    ++pool->stats.inUse;
    if (pool->stats.inUse > pool->stats.peakInUse)
        pool->stats.peakInUse = pool->stats.inUse;
    int reuse = slot->bytes && slot->capacity == length;
    if (reuse)
        ++pool->stats.reuses;
    else
        ++pool->stats.allocations;
    pthread_mutex_unlock(&pool->mutex);
    // Only this reservation can touch the slot until a provider is created.
    if (!reuse) {
        free(slot->bytes);
        slot->bytes = malloc(length);
        slot->capacity = length;
    }
    if (!slot->bytes) {
        pixelPoolRelease(slot, NULL, length);
        return NULL;
    }
    *info = slot;
    return slot->bytes;
}

static struct PixelBufferPoolStats pixelPoolStats(struct PixelBufferPool* pool)
{
    pthread_mutex_lock(&pool->mutex);
    struct PixelBufferPoolStats stats = pool->stats;
    pthread_mutex_unlock(&pool->mutex);
    return stats;
}

static void pixelPoolClose(struct PixelBufferPool* pool)
{
    void* unused[PIXEL_POOL_SLOTS] = { 0 };
    pthread_mutex_lock(&pool->mutex);
    pixelPoolRequire(!pool->closed);
    pool->closed = 1;
    for (unsigned i = 0; i < PIXEL_POOL_SLOTS; ++i) {
        if (!pool->slots[i].busy) {
            unused[i] = pool->slots[i].bytes;
            pool->slots[i].bytes = NULL;
        }
    }
    int destroy = !--pool->references;
    pthread_mutex_unlock(&pool->mutex);
    for (unsigned i = 0; i < PIXEL_POOL_SLOTS; ++i)
        free(unused[i]);
    if (destroy) {
        pthread_mutex_destroy(&pool->mutex);
        free(pool);
    }
}
#endif
