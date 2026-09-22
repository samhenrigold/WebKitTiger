// tigervideoring.h - decoded video frames shared between the x86_64 web process
// (producer: WebCore's TigerVideoSink, fed by MediaPlayerPrivateFFmpeg) and the i386
// UI process (consumer: TigerWK2View, which draws the current slot straight into the
// video rect). Same rules as tigeraudioring.h: 4-byte fields only so the i386 and
// x86_64 compilations agree on every offset, and the backing store is a LINKED file
// under /tmp opened by path -- never a descriptor in flight (NOTES.md 2026-09-22,
// unlinked vnodes in flight wedge 10.4).
#ifndef TIGER_VIDEO_RING_H
#define TIGER_VIDEO_RING_H

#include <stdint.h>

#define TVR_MAGIC 0x54565231u /* 'TVR1' */
#define TVR_SLOTS 4u
#define TVR_HEADER_BYTES 64u

// Pixels are BGRA (premultiplied-irrelevant: opaque), row 0 = top, stride = width * 4.
typedef struct TigerVideoRing {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t slots;              // TVR_SLOTS
    uint32_t slotBytes;          // stride * height
    volatile uint32_t currentSlot;   // producer: the slot holding the frame to show
    volatile uint32_t sequence;      // producer: incremented after currentSlot is set
    uint32_t reserved[8];
} TigerVideoRing;

// The producer writes slot (sequence + 1) % TVR_SLOTS, so the slot being shown is not
// touched again for TVR_SLOTS - 1 further frames (~100 ms at 30 fps). The consumer
// draws within one run-loop turn, so it never reads a slot that is being overwritten.
// ponytail: no seqlock; if a consumer ever stalls past 3 frames it sees a torn frame,
// not a crash. Upgrade path: double-check `sequence` after the draw and redraw.
static inline uint8_t* tvrSlot(TigerVideoRing* r, uint32_t slot)
{
    return (uint8_t*)r + TVR_HEADER_BYTES + (size_t)slot * r->slotBytes;
}

static inline uint32_t tvrMapBytes(uint32_t stride, uint32_t height)
{
    return TVR_HEADER_BYTES + stride * height * TVR_SLOTS;
}

#endif
