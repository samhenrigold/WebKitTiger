// Decoded BGRA frames shared by Tiger's x86_64 producer and i386 UI/GPU readers.
// Files stay linked while advertised; no descriptors travel over IPC. A resize
// creates a new file, never truncates a mapping another process may still use.
#ifndef TIGER_VIDEO_RING_H
#define TIGER_VIDEO_RING_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TVR_MAGIC 0x54565232u /* 'TVR2': all processes must use this protocol. */
#define TVR_SLOTS 4u
#define TVR_HEADER_BYTES 64u
#define TVR_SNAPSHOT_ATTEMPTS 3u
#define TVR_WRITING 0x80000000u
#define TVR_RECREATE_AFTER_DROPS 8u

// All shared fields are aligned 32-bit values, identically laid out in both ABIs.
// Geometry is immutable after magic is published. Only one producer writes a ring.
typedef struct TigerVideoRing {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t slots;
    uint32_t slotBytes;
    uint32_t currentSlot;
    uint32_t sequence; // zero until the first publication; wrap skips zero
    uint32_t slotGeneration[TVR_SLOTS];
    uint32_t slotState[TVR_SLOTS]; // TVR_WRITING, or a count of copying readers
} TigerVideoRing;

typedef char TigerVideoRingHeaderSize[(sizeof(TigerVideoRing) == TVR_HEADER_BYTES) ? 1 : -1];
typedef char TigerVideoRingLockFree[__atomic_always_lock_free(sizeof(uint32_t), 0) ? 1 : -1];

// Cache a validated, private layout. Never derive a pointer or allocation from a
// shared geometry field after validation (the mapping is writable by its peer).
typedef struct TigerVideoRingLayout {
    uint32_t width, height, stride, slotBytes, mapBytes;
} TigerVideoRingLayout;

static inline int tvrCheckedLayout(uint32_t width, uint32_t height, uint32_t stride, TigerVideoRingLayout* result)
{
    uint64_t rowBytes = (uint64_t)width * 4;
    uint64_t slotBytes = (uint64_t)stride * height;
    if (!width || !height || width > INT32_MAX || height > INT32_MAX || rowBytes > stride
        || slotBytes > (UINT32_MAX - TVR_HEADER_BYTES) / TVR_SLOTS)
        return 0;
    result->width = width;
    result->height = height;
    result->stride = stride;
    result->slotBytes = (uint32_t)slotBytes;
    result->mapBytes = TVR_HEADER_BYTES + (uint32_t)slotBytes * TVR_SLOTS;
    return 1;
}

static inline int tvrValidateRing(const TigerVideoRing* ring, size_t mappedBytes, TigerVideoRingLayout* result)
{
    TigerVideoRingLayout layout;
    if (mappedBytes < TVR_HEADER_BYTES || mappedBytes > UINT32_MAX
        || __atomic_load_n(&ring->magic, __ATOMIC_ACQUIRE) != TVR_MAGIC
        || !tvrCheckedLayout(ring->width, ring->height, ring->stride, &layout)
        || ring->slots != TVR_SLOTS || ring->slotBytes != layout.slotBytes
        || mappedBytes != layout.mapBytes)
        return 0;
    *result = layout;
    return 1;
}

static inline void tvrInitializeRing(TigerVideoRing* ring, const TigerVideoRingLayout* layout)
{
    memset(ring, 0, TVR_HEADER_BYTES);
    ring->width = layout->width;
    ring->height = layout->height;
    ring->stride = layout->stride;
    ring->slots = TVR_SLOTS;
    ring->slotBytes = layout->slotBytes;
    __atomic_store_n(&ring->magic, TVR_MAGIC, __ATOMIC_RELEASE);
}

static inline uint8_t* tvrSlot(TigerVideoRing* ring, const TigerVideoRingLayout* layout, uint32_t slot)
{
    return (uint8_t*)ring + TVR_HEADER_BYTES + (size_t)slot * layout->slotBytes;
}

// The writer never waits for a reader, including one that crashed while pinned.
// If all spare slots are pinned, drop the frame; the owner replaces this file
// after TVR_RECREATE_AFTER_DROPS consecutive drops to recover abandoned pins.
static inline int tvrPublishFrame(TigerVideoRing* ring, const TigerVideoRingLayout* layout, const void* pixels, size_t bytes)
{
    uint32_t sequence = __atomic_load_n(&ring->sequence, __ATOMIC_RELAXED);
    uint32_t currentSlot = __atomic_load_n(&ring->currentSlot, __ATOMIC_RELAXED);
    uint32_t nextSequence = sequence + 1;
    uint32_t attempt;
    if (!pixels || bytes < layout->slotBytes || currentSlot >= TVR_SLOTS)
        return 0;
    if (!nextSequence)
        nextSequence = 1;
    for (attempt = 1; attempt <= TVR_SLOTS; ++attempt) {
        uint32_t slot = (currentSlot + attempt) % TVR_SLOTS;
        uint32_t expected = 0;
        if (sequence && slot == currentSlot)
            continue;
        if (!__atomic_compare_exchange_n(&ring->slotState[slot], &expected, TVR_WRITING, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            continue;
        memcpy(tvrSlot(ring, layout, slot), pixels, layout->slotBytes);
        __atomic_store_n(&ring->slotGeneration[slot], nextSequence, __ATOMIC_RELAXED);
        __atomic_store_n(&ring->slotState[slot], 0, __ATOMIC_RELEASE);
        __atomic_store_n(&ring->currentSlot, slot, __ATOMIC_RELAXED);
        __atomic_store_n(&ring->sequence, nextSequence, __ATOMIC_RELEASE);
        return 1;
    }
    return 0;
}

// Take an owned snapshot while briefly pinning one slot. Generation equality
// prevents accepting a slot selected across a publication; the pin prevents the
// writer racing memcpy itself. No lock is held while CoreGraphics/CA draws.
// The caller keeps its last good image if these bounded attempts fail.
static inline int tvrCopySnapshot(TigerVideoRing* ring, const TigerVideoRingLayout* layout, void* pixels, size_t capacity, uint32_t* copiedSequence)
{
    uint32_t attempt;
    if (!pixels || capacity < layout->slotBytes)
        return 0;
    for (attempt = 0; attempt < TVR_SNAPSHOT_ATTEMPTS; ++attempt) {
        uint32_t sequence = __atomic_load_n(&ring->sequence, __ATOMIC_ACQUIRE);
        uint32_t slot = __atomic_load_n(&ring->currentSlot, __ATOMIC_RELAXED);
        uint32_t state;
        int matches;
        if (!sequence || slot >= TVR_SLOTS)
            return 0;
        state = __atomic_load_n(&ring->slotState[slot], __ATOMIC_RELAXED);
        if (state >= TVR_WRITING - 1
            || !__atomic_compare_exchange_n(&ring->slotState[slot], &state, state + 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            continue;
        matches = __atomic_load_n(&ring->slotGeneration[slot], __ATOMIC_RELAXED) == sequence;
        if (matches)
            memcpy(pixels, tvrSlot(ring, layout, slot), layout->slotBytes);
        __atomic_fetch_sub(&ring->slotState[slot], 1, __ATOMIC_RELEASE);
        if (matches) {
            if (copiedSequence)
                *copiedSequence = sequence;
            return 1;
        }
    }
    return 0;
}

#endif
