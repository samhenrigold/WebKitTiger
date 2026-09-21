// ring.h - fixed-layout SPSC audio ring buffer in POSIX shared memory, shared
// between a 64-bit producer and a 32-bit consumer on Tiger's x86_64/i386
// libSystem. See NOTES.md "DIRECTION SET BY THE USER": the 64-bit content
// process has no CoreAudio (i386/ppc only, confirmed by tiger-lipo -info), so
// audio has to be produced 64-bit and played by the 32-bit UI process.
//
// Layout rule: every field is a plain 4-byte type (uint32_t/int32_t/float).
// No pointers, no `long`, no `double`, no `long long` -- the classic Mac OS X
// i386 ABI aligns 8-byte types to 4 bytes inside a struct, while x86_64
// aligns them to 8, so any 8-byte field placed among 4-byte fields would put
// the i386 and x86_64 compilations at different byte offsets for every field
// after it. Splitting the one 8-byte value we need (a mach_absolute_time()
// timestamp) into two uint32_t halves sidesteps that entirely and keeps the
// struct's total size (and every offset) identical under both tiger-clang
// (i386) and tiger-clang64 (x86_64). Verified empirically too: see
// ringlayouttest.c / the "layout check" in README.md.
//
// SPSC = single producer, single consumer: exactly one thread ever writes
// writeIndex, exactly one thread (the CoreAudio render callback) ever writes
// readIndex. On x86's strongly-ordered memory model that's enough for
// correctness without extra fences: a store is never reordered after an
// earlier store from the same CPU, so by the time the reader observes a new
// writeIndex value, the sample data it points past is already visible. `volatile`
// just stops the compiler from caching indices in a register across the loop.

#ifndef AUDIOBRIDGE_RING_H
#define AUDIOBRIDGE_RING_H

#include <stdint.h>
#include <string.h>

#define RING_SHM_NAME "/wkt_audiobridge_ring"
#define RING_MAGIC 0x41425247u /* 'ABRG' */
#define RING_CHANNELS 2u
#define RING_SAMPLE_RATE 44100u
#define RING_CAPACITY_FRAMES (1u << 16) /* 65536 frames, ~1.49s at 44.1kHz */

typedef struct {
    uint32_t magic;
    uint32_t sampleRate;
    uint32_t channels;
    uint32_t capacityFrames;

    // Monotonically increasing frame counts (not wrapped to capacity); the
    // actual ring offset is index % capacityFrames. Unsigned wraparound in
    // (writeIndex - readIndex) is well-defined C and gives the right
    // occupancy even after a uint32_t wrap, which at 44.1kHz stereo would
    // take ~27 hours -- far beyond this spike's runtime.
    volatile uint32_t writeIndex;   // producer-owned
    volatile uint32_t readIndex;    // consumer-owned

    uint32_t underrunCount;         // consumer-owned; producer never touches

    // mach_absolute_time() of the producer's most recent ring write, split
    // into two halves (see file header comment on 8-byte alignment).
    // mach_absolute_time() ticks are a machine-wide monotonic counter, not
    // per-process, so the consumer can diff against its own
    // mach_absolute_time() reading directly.
    volatile uint32_t producerTimestampLo;
    volatile uint32_t producerTimestampHi;

    // mach_timebase_info, so either side can convert absolute-time ticks to
    // nanoseconds without assuming numer/denom (in practice always {1,1} on
    // Intel Macs, but this way nothing is assumed).
    uint32_t timebaseNumer;
    uint32_t timebaseDenom;

    float samples[RING_CAPACITY_FRAMES * RING_CHANNELS]; // interleaved
} AudioRing;

static inline uint64_t ringGetProducerTimestamp(AudioRing *r)
{
    // Read hi before lo, then lo, then hi again; if hi changed the low word
    // may have wrapped mid-read, retry. (Won't happen in practice at 44.1kHz
    // over a 30s run, but it's three extra lines for a spike that's supposed
    // to report real latency numbers, not lucky ones.)
    uint32_t hi1, lo, hi2;
    do {
        hi1 = r->producerTimestampHi;
        lo = r->producerTimestampLo;
        hi2 = r->producerTimestampHi;
    } while (hi1 != hi2);
    return ((uint64_t)hi1 << 32) | lo;
}

static inline void ringSetProducerTimestamp(AudioRing *r, uint64_t t)
{
    r->producerTimestampHi = (uint32_t)(t >> 32);
    r->producerTimestampLo = (uint32_t)(t & 0xffffffffu);
}

static inline uint32_t ringAvailableToRead(AudioRing *r)
{
    return r->writeIndex - r->readIndex; // unsigned wraparound-safe
}

static inline uint32_t ringAvailableToWrite(AudioRing *r)
{
    return r->capacityFrames - ringAvailableToRead(r);
}

// Copies `frames` stereo frames starting at ring index `index` (mod capacity)
// into/from a plain interleaved float buffer, handling wraparound.
static inline void ringCopyOut(AudioRing *r, uint32_t index, float *dst, uint32_t frames)
{
    uint32_t cap = r->capacityFrames;
    uint32_t start = index % cap;
    uint32_t firstRun = frames;
    if (start + firstRun > cap)
        firstRun = cap - start;
    memcpy(dst, &r->samples[start * RING_CHANNELS], (size_t)firstRun * RING_CHANNELS * sizeof(float));
    if (firstRun < frames) {
        uint32_t rest = frames - firstRun;
        memcpy(dst + firstRun * RING_CHANNELS, &r->samples[0], (size_t)rest * RING_CHANNELS * sizeof(float));
    }
}

static inline void ringCopyIn(AudioRing *r, uint32_t index, const float *src, uint32_t frames)
{
    uint32_t cap = r->capacityFrames;
    uint32_t start = index % cap;
    uint32_t firstRun = frames;
    if (start + firstRun > cap)
        firstRun = cap - start;
    memcpy(&r->samples[start * RING_CHANNELS], src, (size_t)firstRun * RING_CHANNELS * sizeof(float));
    if (firstRun < frames) {
        uint32_t rest = frames - firstRun;
        memcpy(&r->samples[0], src + firstRun * RING_CHANNELS, (size_t)rest * RING_CHANNELS * sizeof(float));
    }
}

#define RING_SHM_SIZE ((size_t)sizeof(AudioRing))

#endif
