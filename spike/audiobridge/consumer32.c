// consumer32.c - i386 audio consumer for the cross-ABI audio bridge spike.
// Opens the default output AudioUnit and, from its render callback, pulls
// interleaved Float32 stereo PCM out of a shared-memory SPSC ring (ring.h)
// written in real time by a separate 64-bit process (producer64.c). This is
// the shape of the real split per NOTES.md "DIRECTION SET BY THE USER": the
// 64-bit content process has no CoreAudio, so it can only ever produce PCM
// into shared memory; only a 32-bit process (like this one, standing in for
// the 32-bit Cocoa UI process) can actually open the audio device.
//
// Old-style Tiger Component Manager API (FindNextComponent/OpenAComponent),
// not the 10.6+ AudioComponent API -- this SDK/OS predates it. Plain C, no
// Cocoa/ObjC, no CFRunLoop needed: AudioOutputUnitStart runs the HAL I/O
// thread independently once the unit is initialized.
//
// Build: tiger-clang -O2 -o build/consumer32 spike/audiobridge/consumer32.c \
//          -framework CoreAudio -framework AudioUnit -framework AudioToolbox \
//          -framework Carbon

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <mach/mach_time.h>

#include <Carbon/Carbon.h>
#include <AudioUnit/AudioUnit.h>
#include <AudioUnit/AudioOutputUnit.h>

#include "ring.h"

typedef struct {
    AudioRing *ring;
    uint32_t callbackCount;
    uint32_t underrunCallbacks;   // callbacks where we had to pad with silence
    uint32_t framesRequested;
    uint32_t framesUnderrun;      // total silent frames served
    double latencyMinMs, latencyMaxMs, latencyTotalMs;
    uint32_t latencySamples;
    mach_timebase_info_data_t timebase;
} RenderContext;

static OSStatus renderCallback(void *inRefCon,
                                AudioUnitRenderActionFlags *ioActionFlags,
                                const AudioTimeStamp *inTimeStamp,
                                UInt32 inBusNumber,
                                UInt32 inNumberFrames,
                                AudioBufferList *ioData)
{
    RenderContext *ctx = (RenderContext *)inRefCon;
    AudioRing *ring = ctx->ring;
    ctx->callbackCount++;
    ctx->framesRequested += inNumberFrames;

    float *out = (float *)ioData->mBuffers[0].mData;
    uint32_t available = ringAvailableToRead(ring);
    uint32_t toCopy = available < inNumberFrames ? available : inNumberFrames;

    if (toCopy > 0)
        ringCopyOut(ring, ring->readIndex, out, toCopy);
    if (toCopy < inNumberFrames) {
        memset(out + toCopy * RING_CHANNELS, 0, (size_t)(inNumberFrames - toCopy) * RING_CHANNELS * sizeof(float));
        ctx->underrunCallbacks++;
        ctx->framesUnderrun += (inNumberFrames - toCopy);
        ring->underrunCount++;
    }
    // Advance by the full request even under underrun: the hardware clock
    // doesn't wait for us, so treat the missing frames as consumed too
    // (matches how a real ring-buffered audio consumer has to behave).
    ring->readIndex += inNumberFrames;

    // Latency sample: time since the producer's most recent ring write,
    // converted from mach ticks to ms via the (machine-wide, so either
    // side's copy is equally valid) timebase.
    uint64_t producerTicks = ringGetProducerTimestamp(ring);
    uint64_t nowTicks = mach_absolute_time();
    if (producerTicks != 0 && nowTicks >= producerTicks) {
        double ns = (double)(nowTicks - producerTicks) * ctx->timebase.numer / ctx->timebase.denom;
        double ms = ns / 1e6;
        if (ctx->latencySamples == 0 || ms < ctx->latencyMinMs) ctx->latencyMinMs = ms;
        if (ms > ctx->latencyMaxMs) ctx->latencyMaxMs = ms;
        ctx->latencyTotalMs += ms;
        ctx->latencySamples++;
    }

    return noErr;
}

int main(int argc, char **argv)
{
    double runSeconds = argc > 1 ? atof(argv[1]) : 30.0;

    // Wait for the producer to create + publish the ring (shm_open without
    // O_CREAT so we never race it into creating a zero-length object).
    int fd = -1;
    int tries;
    for (tries = 0; tries < 100; tries++) {
        fd = shm_open(RING_SHM_NAME, O_RDWR, 0666);
        if (fd >= 0)
            break;
        usleep(100000);
    }
    if (fd < 0) {
        fprintf(stderr, "consumer32: shm_open failed after waiting 10s (is producer64 running?)\n");
        return 1;
    }
    AudioRing *ring = (AudioRing *)mmap(NULL, RING_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ring == MAP_FAILED) {
        perror("consumer32: mmap");
        return 1;
    }
    while (ring->magic != RING_MAGIC)
        usleep(10000);
    fprintf(stderr, "consumer32: attached ring, sizeof(AudioRing)=%lu, sampleRate=%u, capacityFrames=%u\n",
            (unsigned long)sizeof(AudioRing), ring->sampleRate, ring->capacityFrames);

    ComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    Component comp = FindNextComponent(NULL, &desc);
    if (!comp) {
        fprintf(stderr, "consumer32: FindNextComponent(DefaultOutput) -> NULL\n");
        return 1;
    }
    AudioUnit unit;
    OSErr err = OpenAComponent(comp, &unit);
    if (err != noErr) {
        fprintf(stderr, "consumer32: OpenAComponent -> %d\n", (int)err);
        return 1;
    }

    AudioStreamBasicDescription asbd;
    memset(&asbd, 0, sizeof(asbd));
    asbd.mSampleRate = RING_SAMPLE_RATE;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    asbd.mBitsPerChannel = 32;
    asbd.mChannelsPerFrame = RING_CHANNELS;
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = 4 * RING_CHANNELS;
    asbd.mBytesPerPacket = asbd.mBytesPerFrame * asbd.mFramesPerPacket;

    err = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, sizeof(asbd));
    if (err != noErr) {
        fprintf(stderr, "consumer32: AudioUnitSetProperty(StreamFormat) -> %d\n", (int)err);
        return 1;
    }

    RenderContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ring = ring;
    mach_timebase_info(&ctx.timebase);

    AURenderCallbackStruct cb;
    cb.inputProc = renderCallback;
    cb.inputProcRefCon = &ctx;
    err = AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));
    if (err != noErr) {
        fprintf(stderr, "consumer32: AudioUnitSetProperty(SetRenderCallback) -> %d\n", (int)err);
        return 1;
    }

    err = AudioUnitInitialize(unit);
    if (err != noErr) {
        fprintf(stderr, "consumer32: AudioUnitInitialize -> %d\n", (int)err);
        return 1;
    }
    err = AudioOutputUnitStart(unit);
    if (err != noErr) {
        fprintf(stderr, "consumer32: AudioOutputUnitStart -> %d\n", (int)err);
        return 1;
    }

    fprintf(stderr, "consumer32: playing, running %.1fs, logging occupancy every 1s...\n", runSeconds);
    int seconds;
    for (seconds = 0; seconds < (int)runSeconds; seconds++) {
        sleep(1);
        uint32_t occFrames = ringAvailableToRead(ring);
        double occMs = 1000.0 * occFrames / ring->sampleRate;
        fprintf(stderr, "  t=%ds occupancy=%u frames (%.1fms), callbacks=%u, underrunCallbacks=%u, framesUnderrun=%u\n",
                seconds + 1, occFrames, occMs, ctx.callbackCount, ctx.underrunCallbacks, ctx.framesUnderrun);
    }

    AudioOutputUnitStop(unit);
    AudioUnitUninitialize(unit);
    CloseComponent(unit);

    fprintf(stderr, "\nconsumer32: summary over %.1fs\n", runSeconds);
    fprintf(stderr, "  render callbacks: %u, total frames requested: %u\n", ctx.callbackCount, ctx.framesRequested);
    fprintf(stderr, "  underrun callbacks: %u (%.2f%% of callbacks), silent frames served: %u (%.3f%% of frames)\n",
            ctx.underrunCallbacks,
            ctx.callbackCount ? 100.0 * ctx.underrunCallbacks / ctx.callbackCount : 0.0,
            ctx.framesUnderrun,
            ctx.framesRequested ? 100.0 * ctx.framesUnderrun / ctx.framesRequested : 0.0);
    fprintf(stderr, "  ring->underrunCount (shared, consumer-owned): %u\n", ring->underrunCount);
    if (ctx.latencySamples > 0) {
        fprintf(stderr, "  producer-write-to-render latency: min=%.2fms avg=%.2fms max=%.2fms (n=%u)\n",
                ctx.latencyMinMs, ctx.latencyTotalMs / ctx.latencySamples, ctx.latencyMaxMs, ctx.latencySamples);
    }

    munmap(ring, RING_SHM_SIZE);
    return 0;
}
