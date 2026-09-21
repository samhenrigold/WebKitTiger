# Media for the 64-bit content process (spike report, 2026-09-20)

Scope: how `<video>`/`<audio>`/MSE works in the WebKit2-shaped split the user set on
2026-09-20 22:35 — thin 32-bit Cocoa UI process, 64-bit content process on Tiger's
x86_64 libSystem. QuickTime/QTKit are 32-bit-only, so the QTKit backend
(`logs/qtkit-plan.md`) cannot be used from the content process at all.

## 0. The constraint, measured

Only three x86_64 dylibs exist on the box. Every framework is 32-bit:

```
libSystem.B   ppc ppc64 i386 x86_64      CoreAudio           i386 ppc
libz.1        ppc ppc64 i386 x86_64      AudioToolbox        i386 ppc
libstdc++.6   ppc i386 ppc64 x86_64      CoreFoundation      i386 ppc
                                         QuickTime / QTKit   ppc i386
```

So in the content process there is **no CoreAudio, no CoreMedia, no AVFoundation, no
VideoToolbox, no QuickTime, and not even CoreFoundation**. Decode must be pure
software C, and audio output must leave the process.

## 1. Part 1 result: FFmpeg decodes fast enough

FFmpeg 8.0 builds clean for `x86_64-apple-macosx10.4` (`deps/build-ffmpeg64.sh`,
installed into `toolchain/sysroot-x86_64/usr`): static, decoders only, `-O3
-march=core2`, x86-64 SSSE3 asm through nasm, pthread frame+slice threading, libdav1d
for AV1. Two Tiger-specific fixes were needed, both in the script:

- `static_assert` is not in Tiger's `<assert.h>`, so `-Dstatic_assert=_Static_assert`.
- Tiger's `<mach/i386/thread_status.h>` already defines `struct xmm_reg`; FFmpeg's own
  `xmm_reg` in `libavutil/x86/asm.h` + `libavcodec/x86/constants.{c,h}` is renamed to
  `ff_xmm_reg`.
- The wrapper puts `-I$PREFIX/include` ahead of FFmpeg's own `-I`, so a previously
  installed copy of the headers shadows the source tree. The script deletes the
  installed `libav*`/`libsw*` headers before each build.

`libtigercompat` covers the libc gaps, but nothing in this decoder-only
configuration actually needed it: configure probes `clock_gettime`,
`posix_memalign` and `pthread_setname_np` against the 10.4u SDK, does not find
them, and takes its own fallbacks.

### Decode throughput, Core 2 Duo T7500 @ 2.2 GHz

Best of 3 runs, decode only, 300 frames (10 s @ 30 fps) of Big Buck Bunny.
`spike/decodebench.c`, driven by `spike/run-decodebench.sh`, raw log in
`logs/decodebench-tiger{,-raw}.txt`. The box is shared with other agents, so
best-of-3 is what the table reports; external load can only make a run slower.

| Clip | 1 thread fps | 2 threads fps | 2-thread ms/frame | x realtime (30 fps) |
|---|---|---|---|---|
| H.264 High 480p30, 1.1 Mbps | 177.0 | 255.3 | 3.92 | 8.5 |
| H.264 High 720p30, 2.5 Mbps | 73.1 | 112.0 | 8.93 | 3.7 |
| H.264 High 1080p30, 4.5 Mbps | 29.3 | 50.1 | 19.97 | 1.7 |
| VP9 480p30, 0.9 Mbps | 111.9 | 153.9 | 6.50 | 5.1 |
| VP9 720p30, 2.0 Mbps | 46.7 | 75.5 | 13.24 | 2.5 |
| AV1 480p30 (libdav1d), CRF 40 | 119.5 | 193.6 | 5.17 | 6.5 |

Audio, single thread, 60 s stereo 48 kHz:

| Clip | x realtime |
|---|---|
| AAC-LC 128 kbps | 261 |
| Opus 96 kbps | 122 |
| MP3 128 kbps | 127 |

### yuv420p -> BGRA conversion (libswscale, SSSE3)

Frames must become RGBA before compositing. Measured in the same program with `-s`:

| Resolution | ms/frame | ms per second of 30 fps video |
|---|---|---|
| 854x480 | 1.37 | 41 |
| 1280x720 | 3.08 | 92 |
| 1920x1080 | 6.99 | 210 |

### Reading of the numbers

- **720p30 H.264 is comfortable**: 8.9 ms decode + 3.1 ms convert = 12.0 ms of a
  33.3 ms frame budget, about 36% of the machine, leaving the other core for JS,
  layout and the compositor.
- **1080p30 is the ceiling and not a good one**: 20.0 + 7.0 = 27.0 ms of 33.3 ms.
  It decodes in real time with nothing else running. Cap `<video>` at 720p.
- **VP9 720p30 works** (13.2 + 3.2 = 16.4 ms), so YouTube's VP9 ladder is usable up
  to 720p, but H.264 is 1.5x cheaper at the same resolution and should be preferred.
- **AV1 480p via dav1d is cheaper than VP9 480p.** 720p AV1 was not measured; on this
  ratio it would land near VP9 720p. Advertising AV1 support is optional, not harmful.
- For scale, QTKit 7.6.4's software path managed **~2 fps at 720p**
  (`spike/qtrenderertest.m`). FFmpeg is roughly 50x faster on the same box and the
  same content. This is the single strongest argument for the 64-bit split.
- Two decoder threads is right. The 1-to-2 thread speedup is 1.4-1.7x; a third
  thread on two cores buys nothing and costs latency.
- Conversion is 13-30% of the total. If it ever matters, the fix is not a faster
  swscale but skipping the conversion: hand yuv420p to the UI process and let the
  compositor convert, or upload the three planes as GL textures in the CA host.

Not built: the `ffmpeg` command-line tool. With encoders, muxers and filters all
disabled it would have nothing useful to do; re-run the script with
`--enable-programs --enable-encoder=... --enable-muxer=...` if a decode-only
transcoder is ever wanted on the box.

## 2. Part 2: which backend

### 2.1 What WebCore offers today

| Backend | Selected by | LOC |
|---|---|---|
| `MediaPlayerPrivateAVFoundationObjC` | `USE(AVFOUNDATION)` | 5,254 |
| `MediaPlayerPrivateMediaSourceAVFObjC` | `USE(AVFOUNDATION)` + `ENABLE(MEDIA_SOURCE)` | 2,024 |
| `MediaPlayerPrivateWebM` | `ENABLE(COCOA_WEBM_PLAYER)` | 2,410 |
| `MediaPlayerPrivateGStreamer` | `USE(GSTREAMER)` | 5,731 |
| `MediaPlayerPrivateGStreamerMSE` | + `ENABLE(MEDIA_SOURCE)` | 850 |
| `MediaPlayerPrivateMediaFoundation` | `USE(MEDIA_FOUNDATION)` | 3,493 |
| `MediaPlayerPrivateHolePunch` | `USE(EXTERNAL_HOLEPUNCH)` | 293 |
| `NullMediaPlayerPrivate` | always (fallback) | ~62, inline in `MediaPlayer.cpp` |
| `MockMediaPlayerMediaSource` | test-only | 470 (whole mock MSE dir: 1,449) |

Every Cocoa backend is built on CoreMedia/AVFoundation and is out. There is **no
ffmpeg or libav code anywhere in the tree** (confirmed: the only `grep` hits are
dav1d's credits file and a SIMDe attribution). The only vendored codec is
`Source/WebCore/PAL/ThirdParty/dav1d`, which is the same dav1d we already have
built for x86_64 in the sysroot.

### 2.2 GStreamer: rejected

`USE(GSTREAMER)` is nominally the port-neutral option, and gst-libav wraps the same
libavcodec we just benchmarked. It still does not fit:

- **Dependency mass.** GLib (plus gettext, libffi, PCRE2), GObject, GIO, GStreamer
  core, gst-plugins-base, -good, -bad and gst-libav. That is on the order of a
  million lines of C to cross-build for `x86_64-apple-macosx10.4`, and GIO in
  particular assumes a POSIX surface Tiger only partly has. Every one of those
  packages would need its own pass of the same work we did for FFmpeg — and GLib's
  Darwin paths reach for CoreFoundation, which does not exist in 64-bit here.
- **The WebKit glue is not port-neutral.** 28,786 LOC under
  `platform/graphics/gstreamer` plus 3,483 under `platform/audio/gstreamer`, and the
  load-bearing files (`MediaPlayerPrivateGStreamer.cpp`, `GStreamerCommon`,
  `VideoFrameGStreamer`, `GLVideoSinkGStreamer`, `mse/SourceBufferPrivateGStreamer`)
  are conditioned on `PLATFORM(GTK)`/`PLATFORM(WPE)`, `USE(TEXTURE_MAPPER)`,
  `USE(GBM)` and Coordinated Graphics. None of that exists on this port.
- It buys nothing we do not already have. The decode is libavcodec either way.

### 2.3 A purpose-written FFmpeg backend: recommended

The interface a new backend must satisfy is small.
`Source/WebCore/platform/graphics/MediaPlayerPrivate.h` is 417 lines with 179
virtuals of which only **16 are pure** with MSE and MediaStream off
(`mediaPlayerType`, `cancelLoad`, `play`, `pause`, `naturalSize`, `hasVideo`,
`hasAudio`, `setPageIsVisible`, `seekToTarget`, `paused`, `networkState`,
`readyState`, `buffered`, `didLoadingProgress`, `paint`, `colorSpace`), +1 for the
MSE `load()` overload, plus a `load()` override and `ref`/`deref`.
`NullMediaPlayerPrivate` (`MediaPlayer.cpp:130`) is the working 62-line template.

Registration is four methods on a `MediaPlayerFactory` subclass
(`MediaPlayer.h:930`), hooked into `buildMediaEnginesVector()` at
`MediaPlayer.cpp:326` behind a new `USE(FFMPEG)` gate. Note
`MediaPlayerFactorySupport::callRegisterMediaEngine()` (`MediaPlayer.cpp:1900`)
already exists so a layer outside WebCore can register an engine — that is how the
GPU-process player is injected, and it is available if we later want the player to
live on the UI side.

MSE is the part that usually scares people, and most of it is already written and
port-independent:

| Piece | Path | LOC | Who writes it |
|---|---|---|---|
| MSE DOM + buffering algorithms | `Modules/mediasource/` | 6,227 | upstream |
| Coded-frame processing, SampleMap, eviction, buffered ranges | `platform/graphics/SourceBufferPrivate.cpp` | 1,971 | upstream |
| `SourceBufferPrivate` interface | `.../SourceBufferPrivate.h` | 338, 28 virtual / **5 pure** | us |
| `MediaSourcePrivate` interface | `.../MediaSourcePrivate.h` | 261, 15 virtual / **5 pure** | us |
| `MediaSample` | `platform/MediaSample.h` | 183, ~12 pure | us |
| `MediaDescription` | `platform/MediaDescription.h` | 53 | us |

Of `SourceBufferPrivate`'s five pure virtuals, the one with real content is
`appendInternal(Ref<SharedBuffer>&&)`. Everything above it — the coded frame
processing algorithm, the sample map, eviction, `buffered` bookkeeping — is generic
C++ we inherit.

**There is no port-independent fragmented-MP4 demuxer to reuse.**
`SourceBufferParserAVFObjC` uses the private `AVStreamDataParser`;
`SourceBufferParserWebM` needs external libwebm *and* emits `CMSampleBuffer`s;
`platform/graphics/iso/` (1,769 LOC) is a clean dependency-free box parser but
implements only encryption and WebVTT boxes, no `moov`/`moof`/`traf`/`trun`. That is
exactly the hole libavformat fills.

### 2.4 How MSE append works over libavformat

Per `SourceBuffer`, keep one `AVFormatContext` opened with `AVFMT_FLAG_CUSTOM_IO`
over an `avio_alloc_context` whose read/seek callbacks run against a growing
`SharedBuffer` the appends accumulate into:

1. First append carries the init segment (`ftyp`+`moov`, or the WebM EBML header +
   Segment Info + Tracks). `avformat_open_input` + `avformat_find_stream_info` on it
   yields the `AVStream`s; translate those into a
   `SourceBufferPrivateClient::InitializationSegment` (track IDs, a
   `MediaDescriptionFFmpeg` per track carrying the codec string, sample rate /
   natural size) and call `didReceiveInitializationSegment`.
2. Each media segment append (`moof`+`mdat`, or a WebM Cluster) extends the buffer;
   pump `av_read_frame` until `AVERROR(EAGAIN)`; wrap each `AVPacket` in a
   `MediaSampleFFmpeg` and call `didReceiveSample`. Timestamps come straight from
   `pkt->pts/dts/duration` rescaled from the stream time base into `MediaTime`;
   `AV_PKT_FLAG_KEY` maps to `MediaSample::IsSync`.
3. `resetParserStateInternal` closes and reopens the context on the retained init
   segment. `abort()` and `removeCodedFrames` are handled upstream.

Two small upstream-facing edits: a new `MediaSample::Type::FFmpegPacket` enum value
and a matching alternative in the `PlatformSample` variant (`MediaSample.h:40-75`).

The decode itself does **not** happen at append time. Samples are decoded on a
decoder thread pulling from the SampleMap in presentation order, which is what keeps
`appendBuffer` off the main thread's critical path.

### 2.5 Audio has to leave the process — this is the real dependency

There is no CoreAudio for x86_64 on Tiger (verified above), and no other 64-bit audio
device API. PCM must go to the 32-bit UI process.

WebCore already has the exact shape for this.
`AudioDestination` (`platform/audio/AudioDestination.h`, 150 LOC) is pulled, not
pushed: `AudioIOCallback::render(AudioBus&, framesToProcess, AudioIOPosition)`.
`AudioDestinationResampler` (148+91 LOC) does hardware-vs-context rate conversion
generically, and **`RemoteAudioDestinationProxy`**
(`Source/WebKit/WebProcess/GPU/media/`, 279+119 LOC) is the precedent: it subclasses
the resampler, runs its own rendering thread, writes into a shared ring buffer and
signals another process. Clone that shape:

- content process: `AudioDestinationTiger : AudioDestinationResampler` pulling on a
  timer thread into a shared-memory ring buffer (~600 LOC with the IPC);
- UI process: a 32-bit sink draining the ring into a CoreAudio AudioUnit or an
  AudioQueue (~400 LOC).

For `<video>` element audio specifically, the same ring buffer is fed from the
decoder thread after `swresample` conversion to the device format.

Latency budget: audio decode is 120-260x realtime, so the ring can be deep (100-200
ms) without anyone noticing, which also absorbs scheduling jitter on a 2005 kernel.
A/V sync should be driven by the audio clock reported back from the UI process.

### 2.6 Video frames to the compositor

`paint(GraphicsContext&, const FloatRect&)` is the one mandatory hook;
`nativeImageForCurrentTime()` gives canvas `drawImage`, WebGL upload and
`bitmapImageForCurrentTime` for free (`MediaPlayerPrivate.cpp:57-86` implements the
latter generically). `platformLayer()` returning nullptr makes `RenderVideo` fall
back to the software paint path. `VideoFrame` is **not** needed — it is only for
WebCodecs/MediaStream/`requestVideoFrameCallback`, and its `createRGBA`/`createBGRA`
factories are stubs returning nullptr outside the CV and GStreamer ports
(`platform/VideoFrame.cpp:82-124`).

Because the content process also has no CoreGraphics, the practical path is to skip
the content-process image backend for video entirely: swscale writes BGRA straight
into a shared-memory surface, and the 32-bit UI process sets it as a `CALayer`'s
contents in the existing CA host (`spike/CAHost`, `logs/ca-hosting-design.md`). Only
canvas `drawImage(video)` and WebGL need `nativeImageForCurrentTime()`, and those can
go through whatever the rendering spike settles on.

## 3. What YouTube actually needs

- **MSE is mandatory.** The desktop HTML5 player refuses to start without
  `window.MediaSource`; the progressive itags (18 = 360p avc1+mp4a, 22 = 720p) still
  exist on many videos but are no longer what the player requests, and relying on
  them means depending on a fallback Google keeps narrowing.
- **The codec probe is `MediaSource.isTypeSupported`**, not `canPlayType`. Answer yes
  for `video/mp4; codecs="avc1.42E01E"` / `"avc1.4D401F"` / `"avc1.64001F"` and
  `audio/mp4; codecs="mp4a.40.2"`, which gets the avc1 ladder (itag 136/137 video +
  140 audio). Answering **no** to `video/webm; codecs="vp9"` and to `av01.*` is the
  right call at first: it makes YouTube serve H.264, which we decode 1.5x cheaper.
  VP9 can be turned on later per the numbers above; the decoder is already built.
- **Cap the served resolution at 720p.** YouTube picks by player size and measured
  bandwidth, so sizing the player element and reporting a modest
  `didLoadingProgress`/buffered rate is usually enough; an explicit cap in
  `isTypeSupported`/`supportsTypeAndCodecs` is not possible since resolution is not
  in the MIME type. The blunt version is to reject the higher itags by declining to
  buffer them, or to set the element's size.
- **No EME.** Standard videos are unencrypted; DRM only appears for Movies/Premium
  content, which is out of scope. **No WebRTC.**
- Networking is range-requested `fetch`/XHR over HTTPS, which the curl backend
  already handles.
- The remaining risk is not media at all, it is **the player's JavaScript**. The
  64-bit content process is the whole reason this is even arguable: it gets the
  maintained x86_64 JIT. With the i386 interpreter (2.24 s on the TigerBrowser test
  loop vs Safari 4.1.3's 59 ms) the player would be unusable no matter how fast the
  decoder is.

## 4. Recommendation and estimates

**Write a purpose-built FFmpeg backend. Do not port GStreamer.**

| Piece | New LOC (estimate) |
|---|---|
| `FFmpegDecoder` wrapper: open codec from `MediaDescription`, decode thread, swscale to BGRA, swresample to device format | 500 |
| `MediaPlayerPrivateFFmpeg`: progressive playback, demux thread, clock, seek, `buffered`, `paint` | 1,800 |
| `MediaSampleFFmpeg` (wraps `AVPacket`) + `MediaDescriptionFFmpeg` + codec-string mapping / `supportsTypeAndCodecs` | 550 |
| `SourceBufferPrivateFFmpeg`: custom-AVIO append parsing, init segment, sample emission | 900 |
| `MediaSourcePrivateFFmpeg` | 350 |
| Audio out: `AudioDestinationTiger` + ring buffer + IPC (content side) | 600 |
| Video frame handoff to the shared surface (content side) | 400 |
| Registration, `USE(FFMPEG)` gates, CMake, `MediaSample::Type` addition | 200 |
| **Content process total** | **~5,300** |
| UI process: 32-bit CoreAudio/AudioQueue sink + layer contents plumbing | ~700 |

Compare: 32,269 LOC of GStreamer glue that does not compile on this port, on top of
cross-building GLib and six GStreamer modules against a libc with no CoreFoundation.

Suggested order, each step independently testable on the box:

1. `MediaPlayerPrivateFFmpeg` for progressive `<video src=...>`, video only, no
   audio, painting into a shared BGRA surface. Proves the frame pipeline end to end.
   (~2,700 LOC)
2. Audio out over the ring buffer + A/V sync on the audio clock. Proves the split.
   (~1,300 LOC)
3. MSE: `MediaSourcePrivateFFmpeg` + `SourceBufferPrivateFFmpeg`. Test against the
   mock MSE tests first, then YouTube. (~1,800 LOC)
4. Tune: yuv420p straight to the compositor if the 13-30% conversion cost shows up in
   a profile; VP9 on or off by measurement.

### Open items

- The content process has no CoreGraphics, so `nativeImageForCurrentTime()` depends
  on whatever the rendering spike (Leopard x86_64 CG loaded privately vs cairo)
  concludes. Video playback itself does not need it; canvas and WebGL do.
- 720p AV1 decode was not measured. Measure before advertising `av01.*`.
- The `-s` runs show the conversion serializing behind the decoder when both happen
  on one thread (720p 2-thread throughput drops from 112 to 92 fps). In the real
  backend the conversion must run on the decoder thread pool or on its own thread,
  not on whatever thread drains frames.

---

## Addendum, 2026-09-20: §2.4 superseded

The incremental-append spike (`logs/mse-demux.md`, `spike/msebench.c`) tested §2.4's
"one AVFormatContext per SourceBuffer over a growing buffer with EAGAIN semantics"
on the box. **It does not work**, for mov or for matroska: `mov_switch_root` zeroes
its continuation pointer and resets `found_mdat` before parsing the next root atom
(mov.c:10889), so the first pump at a fragment boundary with the next `moof` not yet
appended destroys the demuxer's ability to continue. It decodes the first segment
and nothing after.

The working model is a throwaway `AVFormatContext` per append over
`init segment || complete buffered fragments` as a finite stream, with one
long-lived `AVCodecContext` per track. It decodes every phase (play, seek across a
timestamp discontinuity, remove) on fMP4 H.264+AAC and WebM VP9+Opus, costs 0.32 ms
per open and 5-7 ms per 2 s append, and makes `remove()` free. It does require ~120
lines of our own top-level box / EBML scanning, because a truncated `mdat` parses
into garbage packets rather than failing. See `logs/mse-demux.md` for the detail and
the revised step list.
