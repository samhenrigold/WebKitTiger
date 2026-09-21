// ringlayouttest.c - prints sizeof(AudioRing) and offsetof() of every field,
// compiled and run once for i386 and once for x86_64. Diffing the two
// outputs is the empirical proof that ring.h's "every field is 4 bytes"
// discipline actually produces an identical layout under both ABIs (the
// thing the two real programs, producer64 and consumer32, depend on).
#include <stddef.h>
#include <stdio.h>
#include "ring.h"

int main(void)
{
    printf("sizeof(AudioRing) = %lu\n", (unsigned long)sizeof(AudioRing));
    printf("offsetof magic              = %lu\n", (unsigned long)offsetof(AudioRing, magic));
    printf("offsetof sampleRate         = %lu\n", (unsigned long)offsetof(AudioRing, sampleRate));
    printf("offsetof channels           = %lu\n", (unsigned long)offsetof(AudioRing, channels));
    printf("offsetof capacityFrames     = %lu\n", (unsigned long)offsetof(AudioRing, capacityFrames));
    printf("offsetof writeIndex         = %lu\n", (unsigned long)offsetof(AudioRing, writeIndex));
    printf("offsetof readIndex          = %lu\n", (unsigned long)offsetof(AudioRing, readIndex));
    printf("offsetof underrunCount      = %lu\n", (unsigned long)offsetof(AudioRing, underrunCount));
    printf("offsetof producerTimestampLo= %lu\n", (unsigned long)offsetof(AudioRing, producerTimestampLo));
    printf("offsetof producerTimestampHi= %lu\n", (unsigned long)offsetof(AudioRing, producerTimestampHi));
    printf("offsetof timebaseNumer      = %lu\n", (unsigned long)offsetof(AudioRing, timebaseNumer));
    printf("offsetof timebaseDenom      = %lu\n", (unsigned long)offsetof(AudioRing, timebaseDenom));
    printf("offsetof samples            = %lu\n", (unsigned long)offsetof(AudioRing, samples));
    printf("sizeof(void*) = %lu (sanity: 4 on i386, 8 on x86_64)\n", (unsigned long)sizeof(void *));
    return 0;
}
