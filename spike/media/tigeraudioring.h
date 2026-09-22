// tigeraudioring.h - PCM ring shared between the x86_64 web process (producer,
// MediaPlayerPrivateFFmpeg) and the i386 helper tigeraudio32 (consumer, the only
// side that can open CoreAudio on 10.4). Layout rules from spike/audiobridge/ring.h:
// 4-byte fields only, so the i386 and x86_64 compilations agree on every offset.
// The backing store is a LINKED file under /tmp opened by path (never a descriptor in
// flight: NOTES.md 2026-09-22, unlinked vnodes in flight wedge 10.4).
#ifndef TIGER_AUDIO_RING_H
#define TIGER_AUDIO_RING_H

#include <stdint.h>
#include <string.h>

#define TAR_MAGIC 0x54415231u /* 'TAR1' */
#define TAR_CHANNELS 2u
#define TAR_CAPACITY_FRAMES (1u << 16) /* 65536 frames, ~1.4 s at 48 kHz */

typedef struct {
    uint32_t magic;             // TAR_MAGIC while the producer lives; 0 tells the consumer to exit
    uint32_t sampleRate;
    uint32_t channels;          // always TAR_CHANNELS
    uint32_t capacityFrames;    // always TAR_CAPACITY_FRAMES
    volatile uint32_t writeIndex;   // producer-owned, monotonically increasing frame count
    volatile uint32_t readIndex;    // consumer-owned
    volatile uint32_t paused;       // producer sets; consumer outputs silence and does not advance
    uint32_t underrunCount;         // consumer-owned
    float samples[TAR_CAPACITY_FRAMES * TAR_CHANNELS]; // interleaved Float32
} TigerAudioRing;

static inline uint32_t tarAvailableToRead(const TigerAudioRing* r) { return r->writeIndex - r->readIndex; }
static inline uint32_t tarAvailableToWrite(const TigerAudioRing* r) { return r->capacityFrames - tarAvailableToRead(r); }

static inline void tarCopyOut(TigerAudioRing* r, uint32_t index, float* dst, uint32_t frames)
{
    uint32_t start = index % r->capacityFrames, first = frames;
    if (start + first > r->capacityFrames)
        first = r->capacityFrames - start;
    memcpy(dst, &r->samples[start * TAR_CHANNELS], (size_t)first * TAR_CHANNELS * sizeof(float));
    if (first < frames)
        memcpy(dst + first * TAR_CHANNELS, &r->samples[0], (size_t)(frames - first) * TAR_CHANNELS * sizeof(float));
}

static inline void tarCopyIn(TigerAudioRing* r, uint32_t index, const float* src, uint32_t frames)
{
    uint32_t start = index % r->capacityFrames, first = frames;
    if (start + first > r->capacityFrames)
        first = r->capacityFrames - start;
    memcpy(&r->samples[start * TAR_CHANNELS], src, (size_t)first * TAR_CHANNELS * sizeof(float));
    if (first < frames)
        memcpy(&r->samples[0], src + first * TAR_CHANNELS, (size_t)(frames - first) * TAR_CHANNELS * sizeof(float));
}

#endif
