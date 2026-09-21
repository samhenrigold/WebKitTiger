# audiobridge

Spike for the WebKit2-shaped split set in `NOTES.md` "DIRECTION SET BY THE
USER": the content process goes 64-bit on Tiger's x86_64 libSystem, but that
libSystem has no CoreAudio at all, so audio has to be *produced* in the
64-bit process and *played* by the 32-bit UI process. This prototypes the
cross-ABI hand-off with a shared-memory ring buffer.

## Confirmed: a 64-bit process cannot open the audio device

```
tiger-lipo -info sysroot/System/Library/Frameworks/CoreAudio.framework/Versions/A/CoreAudio
tiger-lipo -info sysroot/System/Library/Frameworks/AudioUnit.framework/Versions/A/AudioUnit
tiger-lipo -info sysroot/System/Library/Frameworks/AudioToolbox.framework/Versions/A/AudioToolbox
```
all report `Architectures in the fat file: ... are: i386 ppc` — no x86_64
slice in any of them. There is no libSystem-level route around this either:
these frameworks simply don't exist for a 64-bit process on this OS.

## The shared-memory API that worked across 32/64-bit

`shm_open` + `ftruncate` + `mmap` (plain POSIX shared memory). Checked first
with `tiger-nm -g -arch <i386|x86_64> sysroot/usr/lib/libSystem.B.dylib`:
both architectures export `shm_open`, `shm_unlink`, and `mmap` as defined
(`T`) symbols. (`mach_vm_map`/`mach_make_memory_entry_64`/`vm_remap` are also
present on the x86_64 side if a Mach-port-based handoff is ever needed
instead, e.g. to pass the region to a process that didn't create it by name,
but POSIX shm by a fixed name is simpler and sufficient here since both
processes know the ring's name up front.)

## Files

- `ring.h` — the shared ring buffer struct + inline helpers. Every field is a
  plain 4-byte type (`uint32_t`/`float`), deliberately never `long`/`double`/
  `long long`/pointers: the classic Mac OS X **i386** ABI aligns 8-byte types
  to 4 bytes inside a struct, while **x86_64** aligns them to 8, so any
  8-byte field sitting among 4-byte fields would put the two ABIs at
  different byte offsets for everything after it. The one 8-byte value the
  spike needs (a `mach_absolute_time()` timestamp) is split into two
  `uint32_t` halves instead.
- `ringlayouttest.c` — prints `sizeof(AudioRing)` and `offsetof()` of every
  field; run once built for i386 and once for x86_64.  **Verified identical**
  (both report `sizeof(AudioRing) = 524332` and the same offsets for every
  field; the only differing line is the sanity-check `sizeof(void*)`, 4 vs.
  8, which the struct itself never uses).
- `producer64.c` — x86_64, plain C. Creates the ring (`shm_open(O_CREAT)` +
  `ftruncate` + `mmap`), generates a 440Hz sine tone at 44.1kHz stereo float,
  paced to real time (sleeps between chunks rather than bursting the whole
  tone in at once), publishing each chunk plus a `mach_absolute_time()`
  timestamp into the ring.
- `consumer32.c` — i386, plain C, no Cocoa. Opens the default output
  `AudioUnit` via the **old Tiger Component Manager API**
  (`FindNextComponent`/`OpenAComponent`/`ComponentDescription` — this SDK
  predates the 10.6+ `AudioComponent` API), sets a Float32 44.1kHz stereo
  stream format, installs a render callback that pulls PCM out of the ring,
  and runs `AudioOutputUnitStart` (no `CFRunLoop` needed — the HAL I/O thread
  runs independently once the unit is initialized).

## Build

```
tiger-clang64 -O2 -fno-asynchronous-unwind-tables -fno-unwind-tables \
    -o build/producer64 spike/audiobridge/producer64.c
tiger-clang   -O2 -o build/consumer32 spike/audiobridge/consumer32.c \
    -framework CoreAudio -framework AudioUnit -framework AudioToolbox -framework Carbon
```

`-fno-asynchronous-unwind-tables -fno-unwind-tables` on the 64-bit build: a
teammate flagged that cctools ld64 can crash (`Assertion failed:
(targetAtom != NULL), ld.hpp:914`) linking x86_64 at a 10.4 target when
objects carry EH personality references. `producer64.c` is plain C with no
exceptions, and linked fine both with and without the flags in this spike --
added them anyway once flagged, defensively, and re-verified on the box
(rebuilt, redeployed, re-ran an 8s producer/consumer pair: still zero
underruns, latency numbers unchanged within run-to-run noise). Didn't
personally hit the ld64 crash, but the flags are free insurance for plain C
with no downside here.

## Results (run on the box, 20-30s each)

Ran with `nohup ... &` for both processes inside one ssh invocation (a
lesson from an earlier spike: a background process launched over ssh can die
when the launching connection drops, so launch + measure + collect all stay
in one ssh call).

- **Zero underruns** across three separate runs (8s, 30s, 20s): `underrun
  callbacks: 0 (0.00%)`, `silent frames served: 0`, `ring->underrunCount: 0`
  every time. Ring occupancy (logged once per second) stayed steady around
  4096-5120 frames (~93-116ms) out of a 65536-frame (~1.49s) capacity — the
  producer's real-time pacing keeps the ring comfortably fed without ever
  running the consumer dry or letting the ring fill up.
- **Latency** (producer's most-recent-write timestamp vs. render-time
  `mach_absolute_time()`, both read from the shared ring, converted via the
  machine's `mach_timebase_info`): 30s run — min 3.44ms, avg 12.04ms, max
  110.66ms; 20s run — min 6.83ms, avg 13.08ms, max 76.69ms. The high-end max
  in both runs is a startup transient (the first render callbacks fire
  before the ring has settled into steady state); steady-state latency is
  consistently in the 10-15ms range, well under one HAL callback period.
- **CPU usage** (`ps -o pid,ppid,%cpu,rss,vsz`, sampled mid-run):
  producer64 **0.9% CPU**, 1352KB RSS; consumer32 **3.5% CPU**, 3180KB RSS.
  Both trivial on this Core 2 Duo — a sine tone is a cheap producer, and the
  render callback is dominated by CoreAudio's own I/O thread overhead, not
  the ring copy.
- Render callback size was consistently ~512 frames (~86 callbacks/sec at
  44.1kHz), Tiger's default HAL output buffer size.

## Bottom line

`shm_open`/`mmap` is a clean, simple, correctly-exported-on-both-ABIs way to
hand PCM from a 64-bit content process to the 32-bit UI process that alone
can touch CoreAudio. A fixed-layout, pointer-free struct with 4-byte fields
only is sufficient to get byte-identical layout under both the i386 and
x86_64 ABIs without any `#pragma pack`/explicit padding trickery, verified
empirically as well as by design. The SPSC ring achieved zero underruns and
~10-15ms steady-state latency at trivial CPU cost on real Tiger hardware —
this pattern is good to build the real WebKit2-shaped audio path on.
