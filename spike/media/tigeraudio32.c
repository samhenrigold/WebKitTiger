// tigeraudio32.c - i386 audio output helper for the x86_64 web process.
// Usage: tigeraudio32 <ring-file>. Maps the TigerAudioRing at that path, opens the
// default output AudioUnit (Tiger Component Manager API, as spike/audiobridge/
// consumer32.c proved) and plays the ring. Exits when the producer clears the magic,
// when the parent dies, or when the file cannot be mapped.
// Build: make -C spike/media
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <Carbon/Carbon.h>
#include <AudioUnit/AudioUnit.h>
#include <AudioUnit/AudioOutputUnit.h>

#include "tigeraudioring.h"

static OSStatus renderCallback(void* refCon, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts,
    UInt32 bus, UInt32 nframes, AudioBufferList* io)
{
    TigerAudioRing* ring = (TigerAudioRing*)refCon;
    float* out = (float*)io->mBuffers[0].mData;
    (void)flags; (void)ts; (void)bus;
    if (ring->paused || ring->magic != TAR_MAGIC) {
        memset(out, 0, (size_t)nframes * TAR_CHANNELS * sizeof(float));
        return noErr;
    }
    uint32_t avail = tarAvailableToRead(ring);
    uint32_t n = avail < nframes ? avail : nframes;
    if (n)
        tarCopyOut(ring, ring->readIndex, out, n);
    if (n < nframes) {
        memset(out + n * TAR_CHANNELS, 0, (size_t)(nframes - n) * TAR_CHANNELS * sizeof(float));
        ring->underrunCount++;
    }
    // Advance only by what was real: the producer measures the clock from readIndex,
    // so underrun silence must not count as played audio.
    ring->readIndex += n;
    return noErr;
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: tigeraudio32 <ring-file>\n"); return 2; }
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror("tigeraudio32: open"); return 1; }
    TigerAudioRing* ring = (TigerAudioRing*)mmap(NULL, sizeof(TigerAudioRing), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (ring == MAP_FAILED) { perror("tigeraudio32: mmap"); return 1; }
    int tries = 0;
    while (ring->magic != TAR_MAGIC && tries++ < 200)
        usleep(10000);
    if (ring->magic != TAR_MAGIC) { fprintf(stderr, "tigeraudio32: no producer\n"); return 1; }

    ComponentDescription desc = { kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput, kAudioUnitManufacturer_Apple, 0, 0 };
    Component comp = FindNextComponent(NULL, &desc);
    if (!comp) { fprintf(stderr, "tigeraudio32: no DefaultOutput component\n"); return 1; }
    AudioUnit unit;
    if (OpenAComponent(comp, &unit) != noErr) { fprintf(stderr, "tigeraudio32: OpenAComponent failed\n"); return 1; }

    AudioStreamBasicDescription asbd;
    memset(&asbd, 0, sizeof(asbd));
    asbd.mSampleRate = ring->sampleRate;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    asbd.mBitsPerChannel = 32;
    asbd.mChannelsPerFrame = TAR_CHANNELS;
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = 4 * TAR_CHANNELS;
    asbd.mBytesPerPacket = asbd.mBytesPerFrame;
    OSStatus err = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, sizeof(asbd));
    if (err != noErr) { fprintf(stderr, "tigeraudio32: StreamFormat -> %d\n", (int)err); return 1; }
    AURenderCallbackStruct cb = { renderCallback, ring };
    err = AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));
    if (err != noErr) { fprintf(stderr, "tigeraudio32: SetRenderCallback -> %d\n", (int)err); return 1; }
    if ((err = AudioUnitInitialize(unit)) != noErr) { fprintf(stderr, "tigeraudio32: Initialize -> %d\n", (int)err); return 1; }
    if ((err = AudioOutputUnitStart(unit)) != noErr) { fprintf(stderr, "tigeraudio32: Start -> %d\n", (int)err); return 1; }
    fprintf(stderr, "tigeraudio32: playing %u Hz from %s\n", ring->sampleRate, argv[1]);

    pid_t parent = getppid();
    while (ring->magic == TAR_MAGIC && getppid() == parent)
        usleep(200000);

    AudioOutputUnitStop(unit);
    AudioUnitUninitialize(unit);
    CloseComponent(unit);
    fprintf(stderr, "tigeraudio32: exit, underruns %u\n", ring->underrunCount);
    return 0;
}
