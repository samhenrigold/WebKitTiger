# Native video contents research — 2026-09-24

Read-only research; no implementation, build, or Tiger workload was run for this note. This is independent of the current controls installation. Two native paths deserve a small later probe: public Tiger CoreVideo textures feeding a bundled `CAOpenGLLayer`, then the bundled private `CAImageQueue` as a video layer's contents. Both could preserve the existing CSS layer tree. Neither is proven to work inside this port's offscreen `CARenderer`, and neither is yet evidence of zero-copy presentation or elimination of GPU upload.

## Verified local availability

The relevant binary is `spike/CAHost/Frameworks/QuartzCore.framework/Versions/A/QuartzCore`, i386, current version 1.6.0, SHA256 `ae24133c5536c1ba386a812eda25b2391cdbdd3d9312016e7fae14e559c85cb0`. The same hash is recorded in `build/candidates/89c598d83/ui/build-manifest.json:9086`. Historical README files disagree about its original framework provenance; this note identifies the actual rebundled binary rather than resolving that history.

Use `toolchain/bin/tiger-nm -arch i386 -gU` and `toolchain/bin/tiger-otool -arch i386 -tvV` for this old Mach-O; current host `nm` rejects its obsolete load command. Defined symbols include:

| Address | Bundled QuartzCore export |
| --- | --- |
| `601d276e` | `CAImageQueueCreate` |
| `601d3e3a` | `CAImageQueueRegisterPixelBuffer` |
| `601d3f80` | `CAImageQueueRegisterBuffer` |
| `601d3828` | `CAImageQueueInsertImage` |
| `601d374e` | `CAImageQueueCollect` |
| `601d3800` | `CAImageQueueFlush` |
| `601d3da0` | `CAImageQueueDeleteBuffer` |
| `601d3a14` | `CAImageQueueInvalidate` |
| `601d2d4c` | `CAImageQueueGetReleasedImageInfo` |
| `601d331e` | `CAImageQueueGetUnconsumedImageCount` |
| `601d3660` | `CAImageQueueSetFlags` |
| `601d36c0` | `CAImageQueueSetLatestCanonicalTime` |
| `601d39d6` | `CAImageQueueSetSize` |
| Objective-C class symbol | `.objc_class_name_CAOpenGLLayer` |

The stock Tiger `sysroot/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore` has neither these image-queue exports nor `CAOpenGLLayer`. Export presence in the rebundled framework is an availability observation, not a successful rendering test.

Stock Tiger CoreVideo does export `CVOpenGLTextureCacheCreate`, `CVOpenGLTextureCacheCreateTextureFromImage`, `CVOpenGLTextureCacheFlush`, texture target/name/clean-coordinate accessors, `CVPixelBufferCreateWithBytes`, and pixel-buffer pool creation. Public declarations are in `sdk/MacOSX10.4u.sdk/System/Library/Frameworks/CoreVideo.framework/Versions/A/Headers/`.

## Option 1: public CoreVideo texture cache plus CAOpenGLLayer

Keep FFmpeg decoding in WEB64 and keep the existing IPC ownership boundary. In GPU32, create a `CVPixelBuffer` over an owned, immutable frame allocation, obtain a cached `CVOpenGLTexture`, and draw it in a `CAOpenGLLayer` occupying the existing video-child position. This could remove per-frame `CGImage` construction and give native texture recycling a chance to reduce overhead. CPU-decoded bytes still have to become a GPU texture; no upload saving is established.

Relevant local contracts:

- `CVOpenGLTextureCache.h:59–65`: a cache is created for a specific CGL context and pixel format; `:77–81` creates a texture from an image buffer.
- `CVOpenGLTextureCache.h:83–92`: cache flush performs housekeeping/recycling. It is not documented as a GPU-completion fence.
- `CVPixelBuffer.h:102–125`: wrapping caller-owned bytes takes a release callback, invoked when the pixel buffer is destroyed. Keep the pixels alive and immutable for the complete consumer lifetime; unlocking a pixel buffer alone is not evidence that rendering has finished.
- Bundled `CAOpenGLLayer.h:16–21`: synchronous layers update on `setNeedsDisplay`; asynchronous layers request periodic draws. `:28–39` provides the draw eligibility/draw callbacks, with destination already attached and other GL state undefined.
- `CAOpenGLLayer.h:48–66`: pixel format/context are acquired and released through callbacks; the default context has **no share context**. A texture-cache context cannot casually be substituted for an unrelated layer context. Establish and verify the same context or a compatible sharing arrangement and synchronization.

Older upstream template: `Source/WebCore/platform/graphics/cocoa/WebGLLayer.h` and `.mm` before WebKit r222961. The [primary WebKit timeline for r222961](https://trac.webkit.org/timeline?from=2017-10-05T01%3A28%3A37-07%3A00&precision=second) records replacing that CAOpenGLLayer implementation with CALayer/IOSurface and removing `copyCGLPixelFormatForDisplayMask`, `copyCGLContextForPixelFormat`, and `drawInCGLContext`. That historical adapter is relevant; the later IOSurface implementation is not available on stock Tiger. This research inspected the revision description, not a retrieved full pre-r222961 implementation.

Open question: does this bundled `CAOpenGLLayer` render its content correctly when nested under this port's `CARenderer`, including opacity, clipping, transforms and overlap? Class/header availability does not answer that question. Keep the current `CGImage` path as the functional reference until a target pixel/lifetime probe passes.

## Option 2: private CAImageQueue as CALayer.contents

The old WebKit Windows video path supplies a particularly close model: it sets an image queue as the layer's contents once, then registers and enqueues frames rather than constructing a new image for each layer update. The primary source survives in [Chromium/Blink's removal diff](https://chromium.googlesource.com/chromium/blink/+/6bad365865e37f5c5526ab47eab8aa84bd3af942%5E!/) (parent `c8527d40e03e5607a01ede9ab418aca118a6ad51`):

- `Source/WebCore/platform/graphics/win/WKCAImageQueue.h`
- `Source/WebCore/platform/graphics/win/WKCAImageQueue.cpp`
- `Source/WebCore/platform/graphics/win/MediaPlayerPrivateQuickTimeVisualContext.cpp`, `retrieveCurrentImage()`
- `Source/WebCore/platform/graphics/win/QTPixelBuffer.cpp`, `imageQueueReleaseCallback()`

The inspected sequence is: create queue with capacity 30, set its Fill flag, assign it to layer contents, lock the current pixel buffer, collect previously consumed images, register the bytes/length/stride/width/height/format/attachments, insert with host time and `Buffer` type plus `Opaque | Flush` flags, retain the pixel buffer on successful insertion, unlock, and mark the layer for commit. The release callback releases that retained pixel buffer. Those old video-specific flags are not a blanket recommendation for arbitrary-alpha content.

The bundled native disassembly corroborates a compatible-looking path, but does not establish the complete ABI:

- `CAImageQueueRegisterPixelBuffer` calls `CA::Render::Shmem::new_shmem` at `601d3e78`, then constructs a `CA::Render::PixelBuffer` at `601d3ebf` and registers it. It returns an image identifier through EDX:EAX. This is **not** proof that registration avoids copying: `new_shmem`'s byte-copy/map behavior was not established here.
- `CAImageQueueInsertImage` starts at `601d3828`. Its observed i386 stack layout includes queue, double timestamp, image type, 64-bit identifier, flags, release callback and callback context. It stores the callback/context and has failure paths.
- `CAImageQueueCollect` scans consumption state and releases images. `GetReleasedImageInfo` accesses thread-local state; its export alone must not be interpreted as a general completion-polling contract.

Before using this API, verify the native function/callback ABI, enum values, timestamp convention and ownership behavior against this exact binary. Windows wrappers are a primary implementation reference, not an assurance of ABI identity. Use host-monotonic time on the queue's time base, not unrelated movie PTS. Keep native queue objects and pointers entirely within GPU32; retain the existing cross-ABI ring protocol. [WebKit bug 45567](https://bugs.webkit.org/show_bug.cgi?id=45567) documents historical Windows image-queue cross-process limitations; it does not prove the same limits on this Mac implementation.

Ownership is the decisive integration requirement. A successful enqueue must retain the frame allocation until the actual release callback; a newer frame, a layer assignment, `CATransaction` flush, or an IPC acknowledgement is not automatically completion. Failed registration/insertion must unwind registration and ownership. Resizing, pause, detach/reinsert, queue invalidation and destruction must release every retained frame exactly once. Do not return a mutable shared ring slot to its producer while any queue/render consumer still reads it. An owned GPU-side frame pool may remain necessary.

This option can preserve CSS composition by replacing only the existing video child's `contents`, **if** native CARenderer supports image-queue contents in the tested configuration. Nothing in symbol inspection proves that last step.

## Existing spike is useful evidence, but a different composition path

`spike/CAHost/CAVideo.m:315–340` already draws a public CoreVideo texture. However, `:366–377` draws the video quad before rendering the CA scene; the video is an underlay, not a normal layer-tree child. Reusing that placement would not preserve arbitrary CSS overlap/clipping by itself. Its `glFinish()` was used to measure GPU time and should not be copied into a proposed fast path without measurement.

`NOTES.md:551` records that QuickTime visual-context decode reached only 7–8 of 15 fps at 720p. The later FFmpeg measurements are much faster. This research proposes reusing the native presentation APIs, not replacing the existing decoder. The older `refs/webkit-history/qtkit-mediaplayer/MediaPlayerPrivateQTKit.mm:420–450` shows a QTMovieLayer integration pattern, but local notes (`NOTES.md:313,344,3550`) report QTMovieLayer absent on Tiger; it is not an available substitute.

## Bounded next step, after the current target queue is clear

Run one isolated native probe comparing the existing CGImage reference with the two candidate layer-content paths on the same changing 1280×720 buffer. Include a clipped/transformed video child, opacity, overlapping controls/text, and a neighboring solid anchor. Require actual captured pixels, frame-sequence purity, and balanced retain/release counts across replacement, pause, resize, detach and destruction. Separately measure preparation and CARenderer time; drawing callbacks, queue counters or export lookup alone cannot establish visible success or 30 fps.

First use the public CoreVideo/CAOpenGLLayer route; if it cannot render correctly or is slower, probe the private image queue. The queue is a close fit for the current CPU-frame architecture, so an isolated comparison is warranted even if the public route works. Keep unsupported behavior and fallback selection explicit. This research does not require a new main build, source change, or delay to installation.
