// producer64.c - x86_64 audio producer for the cross-ABI audio bridge spike.
// Generates a 440Hz sine tone in real time and writes it into a shared-memory
// SPSC ring (ring.h) that a 32-bit consumer (consumer32.c) reads from a
// CoreAudio render callback. Tiger's x86_64 libSystem has no CoreAudio
// (confirmed: CoreAudio.framework/AudioUnit.framework/AudioToolbox.framework
// are all i386/ppc-only fat binaries, tiger-lipo -info), so this process
// can't touch the audio device directly -- this file only ever writes PCM
// into shared memory. Plain C, no ObjC, no Cocoa.
//
// Build: tiger-clang64 -O2 -o build/producer64 spike/audiobridge/producer64.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <mach/mach_time.h>

#include "ring.h"

#define TONE_HZ 440.0
#define CHUNK_FRAMES 1024u

int main(int argc, char **argv)
{
    double runSeconds = argc > 1 ? atof(argv[1]) : 30.0;

    int fd = shm_open(RING_SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        perror("producer64: shm_open");
        return 1;
    }
    if (ftruncate(fd, RING_SHM_SIZE) != 0) {
        perror("producer64: ftruncate");
        return 1;
    }
    AudioRing *ring = (AudioRing *)mmap(NULL, RING_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ring == MAP_FAILED) {
        perror("producer64: mmap");
        return 1;
    }
    close(fd);

    memset(ring, 0, RING_SHM_SIZE);
    ring->sampleRate = RING_SAMPLE_RATE;
    ring->channels = RING_CHANNELS;
    ring->capacityFrames = RING_CAPACITY_FRAMES;
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    ring->timebaseNumer = tb.numer;
    ring->timebaseDenom = tb.denom;
    ring->writeIndex = 0;
    ring->readIndex = 0;
    ring->underrunCount = 0;
    ringSetProducerTimestamp(ring, mach_absolute_time());
    // Publish magic last: this is the field the consumer polls to know the
    // ring is fully initialized (sampleRate/capacityFrames/etc. all valid).
    ring->magic = RING_MAGIC;

    fprintf(stderr, "producer64: sizeof(AudioRing)=%lu (%luKB), capacityFrames=%u, running %.1fs\n",
            (unsigned long)sizeof(AudioRing), (unsigned long)(sizeof(AudioRing) / 1024), RING_CAPACITY_FRAMES, runSeconds);

    float chunk[CHUNK_FRAMES * RING_CHANNELS];
    uint64_t totalFrames = (uint64_t)(runSeconds * RING_SAMPLE_RATE);
    uint64_t framesWritten = 0;
    double phase = 0.0;
    double phaseStep = 2.0 * M_PI * TONE_HZ / RING_SAMPLE_RATE;

    uint64_t startTicks = mach_absolute_time();
    while (framesWritten < totalFrames) {
        uint32_t want = CHUNK_FRAMES;
        // Real-time pacing: don't just burst the whole tone into the ring as
        // fast as the CPU allows -- wait for room like a live encoder would.
        while (ringAvailableToWrite(ring) < want)
            usleep(1000);

        uint32_t i;
        for (i = 0; i < want; i++) {
            float s = (float)(0.2 * sin(phase));
            phase += phaseStep;
            if (phase > 2.0 * M_PI)
                phase -= 2.0 * M_PI;
            chunk[i * 2 + 0] = s;
            chunk[i * 2 + 1] = s;
        }

        ringCopyIn(ring, ring->writeIndex, chunk, want);
        ringSetProducerTimestamp(ring, mach_absolute_time());
        ring->writeIndex += want; // publish after data + timestamp are written
        framesWritten += want;

        // Pace to real time: sleep off however much wall-clock time this
        // chunk represents, minus what generating it actually cost.
        uint64_t nowTicks = mach_absolute_time();
        double elapsedSeconds = (double)(nowTicks - startTicks) * tb.numer / tb.denom / 1e9;
        double targetSeconds = (double)framesWritten / RING_SAMPLE_RATE;
        double aheadBy = targetSeconds - elapsedSeconds;
        if (aheadBy > 0)
            usleep((useconds_t)(aheadBy * 1e6));
    }

    fprintf(stderr, "producer64: done, wrote %llu frames (%.2fs of audio)\n",
            (unsigned long long)framesWritten, (double)framesWritten / RING_SAMPLE_RATE);

    munmap(ring, RING_SHM_SIZE);
    shm_unlink(RING_SHM_NAME);
    return 0;
}
