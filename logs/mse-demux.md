# Incremental MSE demuxing with libavformat (spike, 2026-09-20)

Question: can libavformat demux fragmented MP4 / WebM that arrives the way a DASH
player feeds `SourceBuffer.appendBuffer()`, or does a purpose-written fMP4 box
parser feeding raw NAL units to libavcodec have to be written instead?

**Answer: libavformat can do it, with one structural change and roughly 120 lines of
our own box scanning. No fMP4 sample-table parser is needed.** Plan B is off.

Code: `spike/msebench.c`, driven by `spike/run-msebench.sh`. Segments built by
`spike/make-mse-segments.sh` (30 s of Big Buck Bunny, 2 s closed GOP, cut by
FFmpeg's dash muxer into `init-stream*.m4s` + `chunk-stream*-NNNNN.m4s`, and the
same as WebM/VP9+Opus). Raw run log: `logs/msebench-tiger.txt`. All numbers are
from the Tiger box, x86_64, `-march=core2`.

Each run does three phases: **PLAY** (append segments in order), **SEEK** (skip a
run of segments and append from a later time position), **REMOVE** (drop buffered
bytes, keep going).

## What does not work: one AVFormatContext over a growing buffer

The obvious design — one `AVFormatContext` for the SourceBuffer's lifetime, a
custom `AVIOContext` over an append-only buffer, read callback returning
`AVERROR(EAGAIN)` when starved — decodes the first prebuffered segment and then
nothing, for both mov and matroska.

| Run | Packets | Verdict |
|---|---|---|
| fMP4 H.264, stream, open on init + 1 segment | 60 (of 600) | stalls after segment 1 |
| fMP4 H.264, stream, open on init segment alone | 0 | opens, never yields a packet |
| WebM VP9, stream, open on init + 1 segment | 60 (of 600) | stalls after segment 1 |
| WebM VP9, stream, open on init segment alone | — | `avformat_open_input` fails, EAGAIN |

Two separate mov-demuxer facts cause this, both in `libavformat/mov.c`:

- `mov_read_default` arms the continuation pointer `mov->next_root_atom` only once
  it has seen **both** `moov` and `mdat` (mov.c:9549). An init segment is `ftyp` +
  `moov` with no `mdat`, so opening on it alone leaves the pointer at zero and
  `mov_read_packet` returns a permanent `AVERROR_EOF` (mov.c:11119).
- Worse, `mov_switch_root` zeroes `next_root_atom` and resets `found_mdat`
  *before* it tries to parse the next root atom (mov.c:10889), and returns
  `AVERROR_EOF` if the read starved. Pump the demuxer once at a fragment boundary
  before the next `moof` has been appended — which is exactly when appends arrive —
  and the continuation pointer is destroyed for good. There is no resume path.

Clearing `pb->error` and `pb->eof_reached` after every starved read is necessary
(the avio layer latches both) but not sufficient; the damage is in the demuxer's
own state, not in avio. Nothing in the public API can restore it.

Matroska behaves the same way at the top level and additionally will not open at
all on an init segment alone: it needs at least one Cluster before
`avformat_open_input` succeeds.

## What works: a fresh AVFormatContext per append, one decoder for the stream

Re-open a throwaway `AVFormatContext` over `init segment || pending media bytes`
as a **finite** stream (read callback returns real EOF, never EAGAIN), pump it dry,
close it. Keep a single `AVCodecContext` alive across all appends, so no keyframes
are lost and no decoder state is rebuilt.

| Run | Packets/Frames | DTS discontinuities | Verdict |
|---|---|---|---|
| fMP4 H.264 720p video | 600 / 600 | 1 (the deliberate seek, 10.03 s) | all phases decoded |
| fMP4 AAC audio | 845 / 835 | 1 (12.03 s) | all phases decoded |
| WebM VP9 720p video | 600 / 600 | 1 (10.03 s) | all phases decoded |
| WebM Opus audio | 900 / 900 | 1 (12.02 s) | all phases decoded |

PTS ranges come out exactly where they should: play phase 0.000 to 9.933, seek
phase 20.000 to 23.933, remove phase 24.000 to 29.933. Timestamps carry straight
from each fragment's own `baseMediaDecodeTime`, so a discontinuous append needs no
timestamp fixup at all — the demuxer reports absolute presentation times whichever
segment it is handed. Zero starvation events, zero reads of evicted bytes.

`SourceBuffer.remove()` is free in this model: each append owns its bytes and the
parse context dies with it, so there is no demuxer state pinning old data.

## The part we still have to write: completeness scanning

MSE appends arbitrary byte ranges, not whole segments. Handing libavformat a
truncated fragment is **not** safe, and this is the sharpest finding of the spike:

> With whole segments split into 4 appends and no scanning, the run produced
> **1 packet and 0 frames**. The mov demuxer parsed a complete `moof`, built the
> sample table from `trun`, then read samples out of a short `mdat` and emitted a
> truncated packet as if it were real.

So "try to parse and see whether it worked" is not a usable completeness test. The
caller must know where the last complete fragment ends. That is two small scanners
(`complete_prefix_mp4` and `complete_prefix_webm` in `spike/msebench.c`, ~120 lines
including the EBML varint reader):

- **ISO-BMFF**: walk top-level boxes (32-bit size + 4cc, `size == 1` means a 64-bit
  largesize follows). The committed prefix ends at the end of the last complete
  **`mdat`**, not the last complete box — a `moof` without its `mdat` gives mov a
  sample table pointing at bytes that are not there, it emits nothing, and the
  `moof` is consumed. Cutting at box boundaries instead of fragment boundaries
  produced 0 packets at every split.
- **WebM**: walk EBML elements (leading-bit-coded ID then size). A Cluster is the
  unit. An all-ones size means "unknown length" (live) and has to be treated as
  incomplete.

With the scanner in place, append granularity stops mattering:

| Appends per 2 s segment | fMP4 packets/frames | Overhead per append |
|---|---|---|
| 1 | 600 / 600 | 5.2 ms |
| 2 | 600 / 600 | 5.3 ms |
| 4 | 600 / 600 | 6.0 ms |
| 16 | 600 / 600 | 6.3 ms |
| 64 | 600 / 600 | 6.9 ms |

WebM with element scanning is the same: 600/600 at 1 and at 4 appends per segment.

## Cost

Per-append overhead, measured as open + demux + buffer copy, for 2 s segments:

| Stream | `avformat_open_input` | Total per append |
|---|---|---|
| fMP4 H.264 720p (550-650 KB) | 0.32 ms | 5.2-6.9 ms |
| fMP4 AAC (32 KB) | 0.25 ms | 0.66 ms |
| WebM VP9 720p (670 KB) | 0.17 ms | 3.2 ms |
| WebM Opus (17 KB) | 0.10 ms | 0.79 ms |

Re-opening a parse context per append costs a third of a millisecond, and the init
segment is 834 bytes so re-parsing it every time is noise. Video plus audio is
about 7 ms of parsing per 2 s of playback, roughly **0.35% of one core** — against
8.9 ms/frame to actually decode 720p H.264. Parsing is not where the budget goes.

## Consequences for the design in logs/media64-plan.md

The plan's §2.4 described one long-lived `AVFormatContext` per SourceBuffer fed
through a custom AVIO. That is wrong and has to be replaced by:

1. `SourceBufferPrivateFFmpeg` accumulates appended bytes in a pending buffer.
2. On each `appendInternal`, run the completeness scanner; if no complete fragment
   is buffered yet, return and wait for the next append.
3. For the complete prefix, open a throwaway `AVFormatContext` over
   `init segment || prefix` as a finite stream, emit every `AVPacket` as a
   `MediaSampleFFmpeg` via `didReceiveSample`, close it, and drop the prefix from
   the pending buffer.
4. The first append after an init segment also reports the `AVStream`s upward as a
   `SourceBufferPrivateClient::InitializationSegment`.
5. Decoding stays on one long-lived `AVCodecContext` per track, fed from the
   SampleMap in presentation order. Call `avcodec_flush_buffers()` at a
   discontinuity (the spike did not and lost nothing, but a flush is correct).

This is *simpler* than the original sketch: no EAGAIN plumbing, no latched avio
state to clear, no demuxer lifetime tied to the buffer, and `remove()` needs no
cooperation from libavformat. The LOC estimate for `SourceBufferPrivateFFmpeg`
moves from 900 to about 700 for the parse side, plus ~120 for the two scanners.

### Still open

- Codec config changes mid-stream (`SourceBuffer.changeType`, a new init segment)
  are untested. In this model they are cheap: swap the retained init segment and
  reopen the decoder.
- Only one track per SourceBuffer was tested, which is what YouTube's DASH does
  (separate video and audio adaptation sets). Muxed multi-track appends should work
  unchanged but were not measured.
- Decode timings in this log were taken while other agents were loading the box and
  are not throughput numbers; `logs/decodebench-tiger.txt` has the clean ones.

## Addendum: the scanner is written and tested (`spike/msescan.c`)

`mse_scan_mp4()` and `mse_scan_webm()` are now a standalone unit (`spike/msescan.h`
+ `spike/msescan.c`, 95 lines of code) with no dependency on libavformat: pure
functions returning the byte length of the leading prefix that is safe to parse.
Tests live in `spike/msescantest.c`, run by `spike/run-msescantest.sh`; results in
`logs/msescantest-tiger.txt`. All run on the box.

25 synthetic unit tests pass, including the adversarial cases:

- a `moof` whose `mdat` never arrives returns 0, and a fragment followed by a bare
  `moof` returns the end of the first fragment;
- an `mdat` declared with size 0 ("extends to end of file") is never complete,
  since a growing buffer has no end, so the walk stops in front of it;
- 64-bit `largesize` boxes, a truncated 64-bit header, a size smaller than the box
  header, and an absurd trailing size are all handled;
- for WebM, a Cluster short by one byte, a trailing `Cues` that must not extend the
  prefix, an unknown-size (all-ones) Cluster, and an invalid `0x00` leading byte.

The pipeline sweep feeds all 15 segments of each stream, splitting **every**
segment in two at the same absolute byte offset, over 193 offsets for fMP4 and 172
for WebM. The offsets cover every byte in the first 128, every offset within +/-3 of
each top-level boundary of segment 1, and a 40-step stride across the segment.

| Stream | Cut positions | Packets at every cut | Deep cuts decoded |
|---|---|---|---|
| fMP4 H.264 720p | 193 | 900 / 900 | whole-segment, mid-`mdat`, mid-`moof` |
| WebM VP9 720p | 172 | 900 / 900 | whole-segment, mid-Cluster |

900 rather than the 600 in the tables above simply because this test feeds all 15
segments (30 s at 30 fps) instead of skipping five for a seek phase.

One further finding, from making the test work: the decoder must be built from
**init segment + first complete fragment**, not from the init segment alone.
Matroska will not `avformat_open_input` without a Cluster at all. That is the same
constraint the streaming model hit, and it means `SourceBufferPrivateFFmpeg` reports
its `InitializationSegment` upward after the first complete fragment parses, not the
moment the init segment is appended.
