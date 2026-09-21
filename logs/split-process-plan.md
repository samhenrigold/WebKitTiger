# The Tiger browser architecture: two processes, split 32/64

**This is the decided design** (`NOTES.md`, "ARCHITECTURE DECIDED", 00:50),
not a candidate. It came out of the JS-performance decision, went through four
branches, and settled on the one below after
`logs/render-process-survey.md` (`73fbec3`) found WebKit's display-list remoting
viable with a shipping upstream precedent.

| Process | Arch | Owns |
|---|---|---|
| **UI + render** | **i386** | AppKit shell and view, the Core Animation compositor (private QuartzCore), **display-list replay against Tiger's real CoreGraphics and CoreText** through `compat/`, `NSCell`/`HITheme` control drawing, text input and IME, audio output |
| **Web** | **x86_64** | JSC with the full JIT including FTL, DOM and layout, image and video decoding (ffmpeg), HarfBuzz shaping and font fallback, display-list **recording**. No Apple frameworks — libSystem only |
| **Network** | **x86_64** | curl, LibreSSL, HTTP/2 |

The shape is WebKit's GPU-process architecture with the roles inverted: the
process holding the platform graphics is the small one. The precedent is
WinCairo, which remotes 2D image-buffer drawing to its GPU process by default
with `PLATFORM(COCOA)` off — about 110 messages, of which two are platform-gated.

Two processes, not three. Compositing needs the window, Tiger cannot lend a
window across processes, and a separate compositor would copy every frame back.
So replay lives with AppKit.

Written against the fork at `df6cc9ff`; `Source/WebKit` was fetched into the
sparse checkout to write it. Read-only otherwise.

Spikes folded in: `logs/render-process-survey.md` (the architecture),
`logs/hb-vs-ct.md` (the font risk, retired), `logs/leopard-x86_64-spike.md`
(branch (a), now dead for rendering), `spike/ipc32x64` (transport numbers),
`logs/decodebench-tiger.txt` and `logs/media64-plan.md` (media),
`spike/aquaatlas` (fallback artwork), and the IPC cross-ABI patch
(`toolchain/patches/webkit-ipc-cross-abi.patch`). **`logs/jsc64-spike.md` has
not landed, so milestone N0 remains the one unverified load-bearing
assumption.**

---

## 0. The facts that decide the architecture

### 0.1 Tiger's x86_64 userland, measured

`lipo -info` across `sysroot/` (the mirror of the box):

| Component | Architectures |
|---|---|
| `libSystem.B.dylib` | ppc ppc64 i386 **x86_64** |
| `dyld` | ppc ppc64 i386 **x86_64** |
| `libstdc++.6.dylib` | ppc i386 ppc64 **x86_64** |
| `libz.1.dylib` | ppc ppc64 i386 **x86_64** |
| `libgcc_s.1.dylib` | i386 **x86_64** ppc ppc64 |
| Accelerate's BLAS/LAPACK/ATLAS | **x86_64** present |
| **`libobjc.A.dylib`** | **i386 ppc only** |
| **CoreFoundation** | **i386 ppc only** |
| Foundation, AppKit, ApplicationServices | i386 ppc only |
| CoreGraphics, CoreText, ImageIO, ATS | i386 ppc only |
| CFNetwork, Security, OpenGL | i386 ppc only |
| libxml2, libsqlite3, libicucore, libcurl, libiconv | i386 ppc only |

So the premise holds, and the consequence is sharper than "no frameworks":

> **The x86_64 content process is a pure POSIX / libSystem / C++ environment.
> No Objective-C runtime, no CoreFoundation, no Core Graphics, no Core Text, no
> ImageIO, no Security, no OpenGL.**

Every design choice below follows from that one line. It also means the content
process cannot use *any* of the CoreText and CoreGraphics shim work, because
those shim Tiger's **32-bit** frameworks, which do not exist at 64 bits.

### 0.2 The 64-bit toolchain already works on the box

From `NOTES.md` and the last few commits, this is further along than the brief
assumed:

- `toolchain/bin/tiger-clang64{,++}` exist and are the supported wrappers.
- The cctools ld64 crash on x86_64 links at `-macosx_version_min 10.4` is
  **fixed** (`toolchain/patches/cctools-ld64-x86_64-classic-stubs.patch`,
  commit `2d12f9c`). No on-box linker is needed.
- **C++ exceptions work in 64-bit binaries on the box**, after
  `_dyld_find_unwind_sections` in `compat/libcompat.c` was made 64-bit aware
  (it was using `struct section` / `getsectbynamefromheader`, the 32-bit
  accessors, and reading garbage in a 64-bit image).
- `toolchain/sysroot-x86_64/usr/lib` already carries **freetype, fontconfig,
  expat, libpng16, libjpeg, libwebp(+demux,+mux,+sharpyuv), libxml2, libxslt,
  libsqlite3, libssl/libcrypto/libtls, ICU, brotli, nghttp2, libavif, dav1d,
  and ffmpeg (libavcodec/libavformat/libavutil/libswscale/libswresample)**,
  plus `libc++`, `libc++abi`, `libunwind`, `libtigercompat`, and
  `libclang_rt.builtins-x86_64.a`.
- `spike/audiobridge` already proves a cross-ABI data path: a 64-bit producer
  feeding a 32-bit CoreAudio consumer through an `shm_open`/`mmap` SPSC ring,
  0 underruns, ~12 ms latency, under 4% CPU.

**Missing from the x86_64 sysroot, and needed by this plan: cairo, pixman,
harfbuzz, curl.** All four are plain C autotools/meson builds with no modern-CPU
requirements. That is the whole third-party gap.

### 0.3 The alignment trap, and a correction to an earlier draft

This section previously concluded that the IPC wire format agreed between the
two architectures because `alignof(long long)` is 8 on both. **That was right
about scalars and wrong about the conclusion**, and the objcrt track found the
real offender while building
`toolchain/patches/webkit-ipc-cross-abi.patch`. Measured with our clang, by
static assertion, in C++:

| | i386-apple-macosx10.4 | x86_64-apple-macosx10.5 |
|---|---|---|
| `alignof(uint64_t)`, `alignof(double)` | 8 | 8 |
| **`alignof(struct { uint64_t; })`** | **4** | **8** |
| **`alignof(struct { uint32_t; uint64_t; })`** | **4** | **8** |
| **`sizeof(struct { uint32_t; uint64_t; })`** | **12** | **16** |
| `offsetof(struct { uint32_t; uint64_t; }, second)` | 4 | 8 |
| `sizeof(long)`, `sizeof(size_t)`, `sizeof(void*)` | 4 | 8 |
| `sizeof(ptrdiff_t)` | 4 (`int`) | 8 |

**Wrapping a 64-bit scalar in a struct changes its alignment on i386.** So
`Encoder::grow(alignof(T), sizeof(T))` agrees for a bare `uint64_t` and
disagrees for any struct containing one — in both the padding *and* the size.
A scalar-only test cannot catch this, which is exactly how the first draft
missed it.

Four classes of offender, all now fixed in that patch:

1. **`Encoder`/`Decoder` padding by `alignof(T)`** — fixed with a
   `wireAlignmentOf` that aligns 8-byte scalars to 8 on both sides.
2. **`long` / `unsigned long` / `size_t` fields** — banned by a `requires`
   clause on the arithmetic coder, so the compiler finds future offenders.
3. **`IPC::MessageInfo`** framing in `Platform/IPC/unix/UnixMessage.h` — made
   fixed-width.
4. **`ScrollSnapOffsetsInfo` and `PlatformXR`** fields — fixed individually.

Two rules that follow, and both are build-environment rules rather than code,
which makes them easy to violate by accident:

- **Both builds must run to catch every offender.** `ptrdiff_t` is `int` on
  i386, so a 32-bit-only build is silent about a whole class of mismatch.
- **Build both sides with our clang.** Tiger's GCC 4.0 reports
  `__alignof__(long long)` as 4, and mixing compilers reintroduces the bug
  underneath the fix.

Separately, for **raw structs blitted through shared memory** — the audio ring,
any hand-rolled header — the rule from `spike/audiobridge` still stands and is
stricter: **4-byte fields only**, split 64-bit values into two `uint32_t`, and
static-assert `sizeof`/`offsetof` from both compilers.

---

## 1. How much real WebKit2 machinery transfers

Short answer: **most of it, and the parts that do not are the Cocoa-specific
ones we would have replaced anyway.** The port to imitate is **PlayStation** —
no glib, no X11, no Wayland, no D-Bus, pkg-config disabled outright
(`Source/cmake/OptionsPlayStation.cmake:266-271`), the C API as its public API,
and the smallest platform CMake in the tree at 182 lines.

### 1.1 IPC transport — take the Unix socket path, not Mach

| Transport | File | LOC | Primitive | 32↔64? |
|---|---|---|---|---|
| Mach | `Platform/IPC/cocoa/ConnectionCocoa.mm` | 773 | raw `mach_msg` + `MachMessage.{h,cpp}` | yes |
| **Unix sockets** | **`Platform/IPC/unix/ConnectionUnix.cpp`** | **576** | `socketpair(AF_UNIX)` + `sendmsg`/`recvmsg` + `SCM_RIGHTS` | yes, after one fix |
| GLib | `Platform/IPC/glib/ConnectionGLib.cpp` | 631 | GSocket over the same socketpair | n/a |
| Windows | `Platform/IPC/win/ConnectionWin.cpp` | 370 | named pipes | n/a |

The Mach path is **not** blocked by XPC, contrary to the brief's assumption.
`ConnectionCocoa.mm` contains zero `bootstrap_*` calls, and XPC appears only on
the control path, never in send or receive — five droppable lines
(`:165`, `:721`, `:733`, `:743`, `:754`) for audit tokens, pid and kill.

It is blocked by **libdispatch**. `ConnectionCocoa.mm:211` and `:452` create
`DISPATCH_SOURCE_TYPE_MACH_RECV` and `DISPATCH_SOURCE_TYPE_MACH_SEND` sources,
12 `dispatch_*` calls in all. Our pthreads polyfill is i386-only, and Mach-port
dispatch sources are not cheaply polyfilled.

The Unix path needs no libdispatch at all — a plain `Thread::create("SocketMonitor")`
(`:339`) around `poll()` (`:462`). And upstream already handles Darwin's
quirk, at `ConnectionUnix.cpp:53-59`:

```c
// Although it's available on Darwin, SOCK_SEQPACKET seems to work differently
// than in traditional Unix so fallback to DGRAM on that platform.
#if defined(SOCK_SEQPACKET) && !OS(DARWIN)
#define SOCKET_TYPE SOCK_SEQPACKET
#else
#define SOCKET_TYPE SOCK_DGRAM
#endif
```

`USE(UNIX_DOMAIN_SOCKETS)` is defined at `Source/WTF/wtf/PlatformUse.h:177-180`
for GTK and WPE, and PlayStation forces it from CMake
(`OptionsPlayStation.cmake:268`), which proves a Darwin port may do the same.
Nothing gates it on being non-Darwin.

**Decision: `USE_UNIX_DOMAIN_SOCKETS ON`.** One line of build config, no
libdispatch, no XPC, and the Darwin fallback is already written.

### 1.2 The 32/64 wire format — three fixes, not a rewrite

The wire format is deliberately fixed-width: container lengths are widened
(`static_cast<uint64_t>(vector.size())`, `ArgumentCoders.h:463`),
`ObjectIdentifier` is `uint64_t` (`WTF/wtf/ObjectIdentifier.h:73`),
`SharedMemoryHandle::m_size` is `uint64_t` and not `size_t`
(`WebCore/platform/SharedMemory.h:109`). Enums go through `std::to_underlying`
(`ArgumentCoder.h:79`), so `enum class X : uint8_t` is stable. There is no
memcpy or bitwise fast path in `Source/WebKit/Scripts/generate-serializers.py`.

The leak is the generic arithmetic coder at `WTF/wtf/ArgumentCoder.h:58-71`,
which puts native `sizeof` and `alignof` on the wire. Per §0.3 the alignment
agrees; only the *sizes* of `long`, `size_t`, `uintptr_t` and pointers differ.
Every offender in the tree:

| Offender | Location |
|---|---|
| `Vector<size_t, 1> snapAreaIndices` | `Shared/WebCoreArgumentCoders.serialization.in:2932` |
| `size_t numImages` | `Shared/XR/PlatformXR.serialization.in:351` (WebXR is off anyway) |
| **`MessageInfo`** — three raw `size_t` fields used as the socket framing header | `Platform/IPC/unix/UnixMessage.h:89-92` |

`MessageInfo` is the transport's own header, not the Encoder's, and is a hard
break on the Unix path: three `uint64_t`s and it is fixed.

The durable guard is a `requires` clause on the arithmetic coder banning
pointer-width types, so the compiler finds any future offender instead of a
human auditing forever:

```cpp
// WTF/wtf/ArgumentCoder.h:58
requires (std::is_arithmetic_v<T> && !std::is_same_v<T, long> && !std::is_same_v<T, unsigned long>)
```

**One caveat that would be fatal if missed:** the alignment agreement in §0.3
holds because *clang* reports `alignof(long long) == 8` on i386-darwin. Tiger's
own GCC 4.0 reports 4. Both sides must be built with our clang. They will be,
but it is worth an assertion rather than an assumption.

### 1.3 Process launcher — clone PlayStation's, ~120 LOC

| Implementation | LOC | Mechanism |
|---|---|---|
| `Launcher/cocoa/ProcessLauncherCocoa.mm` | 596 | XPC services throughout |
| `Launcher/glib/ProcessLauncherGLib.cpp` | 354 | `g_subprocess_launcher_*`, ~200 lines of it Flatpak sandboxing |
| `Launcher/win/ProcessLauncherWin.cpp` | 139 | `CreateProcess` |
| **`Launcher/playstation/ProcessLauncherPlayStation.cpp`** | **122** | socketpair + exec |

The Cocoa launcher is unusable: `xpc_connection_create` at `:243`, `:280`,
`:298`, and the whole port handoff is one line,
`xpc_dictionary_set_mach_send(bootstrapMessage, "server-port", listeningPort)`
at `:405`. XPC is 10.7.

PlayStation's is the pattern with nothing attached: create the socket pair, set
buffer sizes, build `argv`, launch, hand the server end to
`didFinishLaunchingProcess`. Swap its one `PlayStation::launchProcess` call for
the launch primitive with the client fd left un-`CLOEXEC`. **Estimate 90–130 LOC.**

**Correction: use `fork` + `exec`, not `posix_spawn`.** `spike/ipc32x64`
established that Tiger has no `posix_spawn` (it is 10.5). The same spike
confirmed `bootstrap_register` works, so Mach naming is available if the Mach
transport is ever preferred over the Unix one.

### 1.4 SharedMemory — take the Unix implementation

| File | LOC |
|---|---|
| `WebCore/platform/SharedMemory.{h,cpp}` | 171 / 114 |
| `WebCore/platform/cocoa/SharedMemoryCocoa.mm` | 264 |
| **`WebCore/platform/unix/SharedMemoryUnix.cpp`** | **198** |

The Cocoa one would *mostly* work — Tiger has `mach_vm_allocate`,
`mach_make_memory_entry_64`, `mach_vm_map` — but its flags do not:
`MAP_MEM_USE_DATA_ADDR` and `VM_FLAGS_RETURN_DATA_ADDR` are Mavericks-era,
`MAP_MEM_VM_SHARE` and `VM_FLAGS_PURGABLE` are 10.5+. Take the Unix one
(`shm_open`/`ftruncate`/`mmap`, fd passed as the `Attachment` via `SCM_RIGHTS`),
matching the transport choice, and reuse what `spike/audiobridge` already
proved.

The handle crosses the boundary cleanly either way: `SharedMemoryHandle` is
`{ MachSendRight m_handle; uint64_t m_size; }`, and `mach_port_t` is `unsigned
int`, 32 bits on both architectures.

### 1.5 DrawingArea — the central choice

| Implementation | Content-process LOC | Needs in the content process | Crosses IPC |
|---|---|---|---|
| `RemoteLayerTreeDrawingArea` | 620 + ~5,400 in the RemoteLayerTree subsystem | ObjC++, CG, IOSurface, ImageBuffer. **No CALayer** | layer create/destroy lists + `LayerProperties` diffs + backing-store handles |
| `TiledCoreAnimationDrawingArea` | 891 | **real CALayers**, `LayerHostingContext`, CARenderServer | a mach port, no pixels |
| **`DrawingAreaCoordinatedGraphics`** (non-GLib) | **712** | nothing — the non-accelerated path needs no GL, no CA, no ObjC | `UpdateInfo` = `ShareableBitmap::Handle` + dirty rects + scroll delta |
| `DrawingAreaCoordinatedGraphicsGLib` | 401 | Skia + GLContext, always | DMA-BUF surfaces |
| `DrawingAreaWC` | 450 | `GraphicsLayerWC`, compositing in a GPU process | layer props + bitmap handles |

**The RemoteLayerTree question, answered.** `PlatformCALayerRemote` really is a
pure data object — `platformLayer()` returns `nullptr`
(`WebProcess/WebPage/RemoteLayerTree/PlatformCALayerRemote.h:77`), and its whole
state is a `LayerProperties` struct of `TransformationMatrix`,
`FloatRoundedRect`, `FilterOperations`, `Color`, `EventRegion`
(`Shared/RemoteLayerTree/LayerProperties.h:142-191`). No `RetainPtr<CALayer>`.
The only CoreAnimation call in the content-process drawing area is an empty
`[CATransaction begin]`/`[commit]` pair at `RemoteLayerTreeDrawingArea.mm:460-461`.

**So the architecture survives a no-ObjC content process. The code does not.**
Every file is `.mm`: `PlatformCALayerRemote.mm` 1252, `RemoteLayerBackingStore.mm`
757, `PlatformCAAnimationRemote.mm` 741, `GraphicsLayerCARemote.mm` 289,
`RemoteLayerTreeContext.mm` 260. The base class
`WebCore/platform/graphics/ca/PlatformCALayer.h` carries
`RetainPtr<PlatformLayer> m_layer` (`:399`), `CGContextRef` (`:43`) and
`OBJC_CLASS AVPlayerLayer` (`:41`). `RemoteLayerBackingStore::enumerateRectsBeingDrawn`
takes an Objective-C block **in its signature** (`RemoteLayerBackingStore.h:151`).
Its ImageBuffer backends are both CG. That is roughly 6,000 LOC to de-Objective-C
before a single pixel appears.

**Recommendation: `DrawingAreaCoordinatedGraphics`, non-accelerated path.**
It is the only drawing area that asks for nothing our content process lacks. It
paints into a `ShareableBitmap` in POSIX shared memory and sends a handle plus
dirty rects; `UpdateInfo` (`Shared/UpdateInfo.h:42-63`) is all fixed-width or
`IntRect`/`FloatSize`, so it crosses the ABI boundary cleanly. The UI process
does what `UIProcess/cairo/BackingStoreCairo.cpp` (133 LOC) does — a blit with a
scroll-by-copy fast path — except ours wraps the buffer as a `CGImage` and sets
it as the `contents` of a `CALayer` in the CARenderer host from
`logs/ca-hosting-design.md`.

Revisit RemoteLayerTree only if per-layer CA animation on the UI side turns out
to matter more than shipping. It is the architecturally nicer answer and the
more expensive one.

### 1.6 API surface

| Directory | LOC |
|---|---|
| `UIProcess/API/C/` (193 files) | **23,968** |
| `UIProcess/API/Cocoa/` | 66,784 |
| `UIProcess/API/mac/` | 4,078 |
| `UIProcess/API/C/playstation/` (`WKView`, `WKViewClient`, `WKRunloop`) | 853 |

The C API is in the shared `Source/WebKit/Sources.txt` and its include directory
is added unconditionally at `Source/WebKit/CMakeLists.txt:60`, so **every CMake
port builds it**. PlayStation builds directly on it with no wrapper and installs
the C headers as its public API (`PlatformPlayStation.cmake:177-181`).

A 32-bit Tiger UI process in plain Cocoa needs:

```
WKContextConfigurationCreate() → WKContextCreateWithConfiguration()
WKPageConfigurationCreate() + WKPageConfigurationSetContext()
WKViewCreate(...)          // we write this, modelled on playstation/WKView.cpp (206 LOC)
WKViewGetPage() → WKPageLoadURL()
```

plus a `PageClient` subclass. `UIProcess/PageClient.h` is 912 LOC with 210 pure
virtuals, but **only 57 at `#if` nesting depth zero** — the rest sit behind
`PLATFORM(COCOA)`, `ENABLE(DRAG_SUPPORT)`, `ENABLE(FULLSCREEN_API)`,
`ENABLE(PDF_HUD)` and vanish with the features off. There is no generic or
headless `PageClient`; the smallest to copy is
`UIProcess/playstation/PageClientImpl.{h,cpp}` (552 LOC) with
`PlayStationWebView.{h,cpp}` (279), about **856 LOC for a complete view and
client stack**, and its `WKViewCreate` takes no native window handle at all.

### 1.7 What this does to the WebKitLegacy plan

`logs/webkitlegacy-plan.md` becomes **moot as a deliverable**. WebKit1 is not
the target in this architecture. What survives from it:

- The Tiger gates in WTF (`PlatformHave.h`, `PlatformUse.h`,
  `PlatformEnableCocoa.h`) — those are cross-cutting.
- The AppKit and Foundation knowledge, which the UI process still needs: event
  handling, the pasteboard constants, `NSTextInput`, tracking rects, printing,
  drag and drop. The UI process has to do all of that; it just does it against
  `PageClient` instead of against `WebHTMLView`.
- The `ScrollbarThemeMac`/HITheme work does **not** transfer, because scrollbars
  are now drawn by WebCore in the content process (§2.4).

The 32-bit WebCore compile work (49 failing translation units, per commit
`e788401`) stops being on the critical path. It is still worth finishing only
to the extent that its fixes are generic rather than Cocoa-specific.

---

## 2. The rendering architecture

### 2.0 Display-list remoting to the 32-bit process

This is WebKit's GPU-process shape with the roles inverted: the process that
holds the platform graphics is the small one. A **64-bit WebProcess** does
JavaScript, DOM, layout, image decoding, text shaping and display-list
*recording*; a **32-bit process** *replays* those display lists against Tiger's
real CoreGraphics and CoreText through the compat layer we have already
verified, and hosts the CA compositor.

Sections 2.1 and 2.2 record the two alternatives and why they lost. **2.1 is
dead**, not deferred. **2.2 is the fallback** if the remoting turns out not to
work, which the survey and the spikes make unlikely.

#### 2.0.1 Why it wins

The decisive argument is not performance, it is that **this is the only branch
that puts the project's existing verified work on the side of the line where it
is used.** 104 CoreText checks against a live Leopard oracle, 54 CoreGraphics
checks with behavioural probes, the ABI screen over 510 functions, the
`ctcompat` metric adapters, `cgcompat`'s gradient and tiling fixes — under
branch (b) all of that is stranded on a UI process that barely draws. Here it is
the renderer.

It is also the only branch with a **shipping upstream precedent for exactly this
mechanism**. Per `logs/render-process-survey.md`, WinCairo remotes DOM rendering
to its GPU process by default
(`Shared/WebPreferencesDefaultValues.cpp:324-335`), using the same generic
backend Cocoa uses with the Cocoa flag off. The files build unconditionally for
every CMake port from `Sources.txt:32-47`. Of roughly 110 messages in
`RemoteGraphicsContext.messages.in`, exactly two are platform-gated.

And it keeps everything the split was for: the x86_64 JIT, a 64-bit address
space, 64-bit ffmpeg decode, crash isolation.

#### 2.0.2 Why two processes and not three

Replay lives in the 32-bit UI process, alongside AppKit and the compositor.
Three reasons, the first of which is decisive and is not about performance:

1. **Compositing needs the window, and Tiger cannot lend a window across
   processes.** There is no CA render-server protocol (`atv/REPORT.md`), so the
   compositor must live wherever the `NSWindow` is.
2. **A separate compositor would copy every frame back** to the process that
   owns the window — the cost the whole display-list design exists to avoid.
3. **Splitting later is cheap; merging later is impossible.**

| | merged UI + render (recommended) | separate render process |
|---|---|---|
| Processes | 2 (+ NetworkProcess) | 3 (+ NetworkProcess) |
| Hop from replay to screen | none — same process as the CA host | one more IPC hop per frame |
| Hop for control drawing | none — AppKit is right there | another hop |
| Scheduling on 2 cores | better | three runnable processes on two cores |
| Crash isolation of graphics | none | yes |
| Can composite at all | **yes** | **no** — the window is in the other process |

The last row is the one that settles it.

The **NetworkProcess stays 64-bit** (§2.6): pure C++ over curl, no frameworks,
and it keeps TLS and HTTP/2 off the WebProcess's cores.

So: **64-bit WebProcess, 64-bit NetworkProcess, 32-bit UI+render process.**

#### 2.0.3 Data flow

| What crosses | As | Direction |
|---|---|---|
| 2D drawing | display-list items over the shared-memory stream ring with two semaphores | 64 → 32 |
| Text | **glyph IDs + per-glyph advances + one anchor point** — shaping already done in the recorder | 64 → 32 |
| Native controls | `ControlPart` + `ControlStyle`, serialized upstream already (§2.7) | 64 → 32 |
| Decoded images | `ShareableBitmap` handles; a source image in the stream is a resource identifier | 64 → 32 |
| Canvas | remote `ImageBuffer`s | both |
| Font handles | `FontPlatformDataAttributes` — file path, PostScript name, size, synthetic bold/oblique, orientation, variation axes, feature settings; web fonts ship bytes | 64 → 32 |
| **Font metrics and advances** | plain floats, computed by **real CoreText in the 32-bit process**, cached per handle | 32 → 64 |
| Layer tree deltas | the Windows coordinated-graphics layer delta (**~1,600 LOC reused unchanged**), consumed by a **~300-LOC scene applier** that builds real `CALayer`s | 64 → 32 |
| Decoded video frames | shared bitmaps, or a video layer in the compositor (§3.1) | 64 → 32 |

The measured IPC characteristics on the box (`spike/ipc32x64`, commits
`6c45186`/`f88e046`) say this is affordable:

| Measurement | Result |
|---|---|
| Mach round trip | **11 µs** |
| 64 KB inline message | 241 MB/s |
| Shared-frame memcpy | ~900 MB/s |
| Double-buffered pipeline | ~176 fps |
| GL upload, 1440×900 BGRA from the mapping | ~8 ms, no copy |

Three findings from that spike that constrain the design: **Tiger has no
`posix_spawn`**, so process launch is `fork`+`exec` (which revises §1.3);
`bootstrap_register` works, so Mach naming is available; and
`mach_make_memory_entry_64` is preferred over `shm_open` for crash-safe
lifetime. Exception ports give crash isolation without `task_for_pid`. Client
storage is *slower* for changing textures — do not use it.

Note that 11 µs and 900 MB/s make the **merged** topology even more clearly
right: a third process would add a hop of that order to every frame for no
benefit we need.

#### 2.0.4 Fonts: the hard part, and the spike that mostly closed it

The survey names fonts as the only concentrated risk, and it is right that this
is where the design could fail. Two halves:

**Glyph agreement.** A glyph ID indexes the font file's outline table, so if
both sides open the same file and face, the numbers match. That is how GTK, WPE
and Skia already work. The survey's top risk was **AAT shaping divergence**:
Tiger's system faces are AAT-first with `morx` and no GSUB, HarfBuzz implements
those tables but not provably bit-identically to CoreText, and there is no
upstream precedent for HarfBuzz-shaping AAT faces for CoreText rasterization.

**`logs/hb-vs-ct.md` has since measured it, and the answer is yes.** HarfBuzz
layout against Tiger CoreText rasterization agrees **to within 0.03 pt on run
width and 0.008 pt per glyph**, for every font tested, at 12, 16 and 24 pt —
including Helvetica, Lucida Grande and Geeza Pro, i.e. the AAT and AAT-Arabic
cases. Both sides were handed **identical bytes** rather than a font name, which
is the methodological point that makes the result mean anything. One
font-specific caveat and one Tiger incapability remain, neither forcing per-font
routing.

That retires risk #1 from the survey's list. Treat the remaining font work as
engineering rather than research.

**Metrics.** Do not recompute them in the 64-bit process. WebKit's CoreText
metrics initialiser is a pile of CoreText-specific heuristics — a typographic
ascent override under conditions, a 15% ascent bump for named faces, a rounding
ladder, an x-height measured from a glyph bounding box rather than a table.
Reimplementing that is a bug farm, and a one-pixel divergence makes layout and
paint disagree.

**Have the 32-bit process compute metrics and per-glyph advances with real
CoreText and return plain floats, cached per font handle.** Then layout and
rendering agree by construction. The `metricsOverrides` field already in
`FontPlatformDataAttributes` is the hook, and synthetic-bold offset is pure
arithmetic on fields already in the handle.

The lead asked specifically whether `hmtx`-derived advances match CoreText's
unhinted advances. The survey's answer is that unhinted advances do agree and
hinting is what breaks them at small sizes; the hb-vs-ct numbers bear that out.
**But the metrics service makes the question moot**, which is the better reason
to adopt it: we never derive an advance in the 64-bit process at all.

Two rules that follow, both cheap and both easy to get wrong later:

- **Name faces by PostScript name, never by index.** Tiger's system fonts are
  suitcases and collections, and index ordering is not stable across a name
  lookup.
- **Font fallback moves entirely into the recording process.** The CoreText
  complex-text controller exists partly to ask CoreText which font it chose per
  run; this design forbids that, so every draw must name a concrete resolved
  face.

  The mechanism is a **font manifest**: the 32-bit process enumerates the
  installed faces with real CoreText and ATS and writes out, per face, the
  PostScript name, family, traits, the covered character set and the file path.
  The 64-bit process loads that manifest and runs its own cache and cascade over
  it, so it can resolve a concrete face for every run without ever asking
  CoreText anything. `ctcompat` is building the manifest generator and the
  handle resolver.

  This is where missing-glyph bugs will come from, and it is the second-largest
  remaining risk after serializer asymmetry.

#### 2.0.5 The known structural gaps

From the survey, both must be fixed before a pixel appears:

1. **WebKit's serialization is symmetric by construction.** The same generated
   coder runs on both sides, so a type whose encoding depends on a platform
   `#if` corrupts the wire silently rather than failing to compile. **Three
   types are CoreGraphics-flag-dependent and must be forced to a neutral
   encoding on both sides before the first pixel:**

   | Type | Why it differs |
   |---|---|
   | **Colour space** | encodes as a `CGColorSpaceRef`-derived form with the CG flag on, an enumerated form without |
   | **`ShareableBitmap` configuration** | pixel format and bitmap-info fields are CG-typed under the flag |
   | **Font attributes** (`FontPlatformDataAttributes`) | CoreFoundation-typed members under the flag; see §2.0.4 |

   The 64-bit side has the flag off and the 32-bit side has it on, which is the
   entire point of the architecture and also precisely what makes these three
   dangerous. Fix them first. This is the failure mode to fear, because it
   produces wrong pixels rather than a build error.
2. **There is no image-buffer backend for "no 2D library".** The 64-bit side
   records without rasterizing, so it needs a **pixel-less shareable-bitmap
   backend**, about 150 LOC.

#### 2.0.6 What this costs

| Piece | LOC |
|---|---|
| Render-side assembly: PlayStation-shaped port + CG/CoreText WebCore + our compat layer | 800–1,200 |
| Neutral colour-space and bitmap-configuration encoding, both sides | 150–250 |
| Pixel-less shareable-bitmap backend for the 64-bit side | 150 |
| Font handle de-CoreFoundation-ed, both directions | 300–450 |
| Metrics service and cache | 300–500 |
| Font cache and cascade over scanned metadata, 64-bit side | 800–1,500 |
| Shaping: HarfBuzz wiring for a CoreGraphics-shaped tree | 400–700 |
| Layer delta retargeted at the UI process, scene applier for real layers | 400–600 |
| Backing store blit and tile plumbing | 200–300 |
| Port glue for the 64-bit process | 1,400–1,600 |
| **Subtotal** | **≈ 4,900–7,250** |
| UI process itself (additive, largely independent) | 3,000–4,200 |
| Aqua controls (§2.7) | ~525 + the Tiger NSCell drawing |
| Media (§3.1) | ~6,000 |

Reused unchanged: about 1,600 lines of recording-side layer code, the whole
stream transport, and every line of `compat/` that touches CoreGraphics or
CoreText.

#### 2.0.7 Residual risks

Ordered, with the survey's ranking amended by the hb-vs-ct result:

1. **Serializer asymmetry** — now the top risk, because it fails silently as
   wire corruption rather than visibly. Fix before first pixel.
2. **Font fallback rewritten from scratch** in the recording process.
3. **Metrics disagreement** between the sides. Mitigated by making the render
   process authoritative, which is the whole point of the metrics service.
4. ~~AAT shaping divergence~~ — **measured and retired** by `logs/hb-vs-ct.md`.
5. **The same-clang rule** (§0.3). A build-environment rule, not code: easy to
   violate by accident, confusing when violated.
6. **Shared-memory limits** on Tiger under aggressive tile allocation. A
   measurement, not an unknown.
7. **Path geometry** in the 64-bit process needs the scalar implementation.

### 2.1 Rejected: Leopard's x86_64 frameworks (dead for rendering)

**Status, per `logs/leopard-x86_64-spike.md`: plausible, one fault away.**
The spike has run on the box and supersedes the estimate I made from the SDK
stubs. Worth recording that the two agreed exactly — I measured 12 unresolved
libSystem symbols for CoreFoundation from the 10.5 SDK stub, and the spike
measured 12 from the real binary. The method is sound, which matters for the
next time someone wants a cheap answer before a spike lands.

What the spike established on the box:

| Framework | undefined | unresolved against Tiger |
|---|---|---|
| **CoreText** | 391 | **0** |
| ColorSync | 219 | 1 |
| ATS | 334 | 3 |
| libobjc | 132 | 3 |
| CoreGraphics | 624 | 9 |
| CoreFoundation | 419 | 12 |
| Foundation | 1613 | 19 |

**CoreText needs nothing at all.** That is the single most interesting number
here, because text rendering is precisely where branch (b) is weakest (§2.4).

Results: all three of CoreFoundation, CoreGraphics and CoreText **load** into a
64-bit process on 10.4.11. CoreFoundation **works** — real strings, correct
`CFGetTypeID`, a live run loop. CoreGraphics **half works**: colour spaces are
real objects, but **`CGBitmapContextCreate` faults** with SIGSEGV in every
variant tried. CoreText loads but could not be exercised without a context.

Three findings from that spike are worth carrying regardless of which branch
wins:

- **`LC_REEXPORT_DYLIB` (0x8000001f) must be demoted to `LC_LOAD_DYLIB`.**
  Leopard introduced it and dyld-46 refuses outright; CoreServices carries ten.
  `spike/demote-reexports.py` rewrites the command word, and the structures are
  identical so nothing else changes. The dropped re-export costs nothing under
  `DYLD_FORCE_FLAT_NAMESPACE=1`.
- **`dyld_register_image_state_change_handler` was the unlock.** objc4 learns
  about images through it and Tiger's dyld has no such call — which is exactly
  what killed the earlier 32-bit libobjc attempt. Tiger's
  `_dyld_register_func_for_add_image` replays every loaded image at
  registration, so the shim drives the Leopard-style handler with a one-element
  batch per callback.
- **The toolchain is ruled out of the CG fault.** The shim was relinked on the
  box with Xcode 2.5's own ld64-62.1 and crashes identically, so the cctools
  x86_64 stub bug is not implicated.

The honest caveat is the **closure**: walking dependencies from CoreText and
CoreGraphics reaches **25 libraries** with 3,094 undefined and 112 unresolved,
because CoreGraphics pulls in CoreServices, which pulls in CarbonCore,
LaunchServices, Metadata, SearchKit, Security and DiskArbitration. Almost none
of that sits on a drawing path — it is CommonCrypto, file quarantine, launchd
and `fenv` — and cutting CoreServices out of the closure would remove most of
it. But it is 25 libraries of private userland to ship and keep working.

#### Verdict: dead for rendering

The `CGBitmapContextCreate` fault was isolated, and the cause is structural
rather than a bug to fix. **Leopard's x86_64 CoreGraphics and CoreText block on
Mach services that exist only in 32-bit form on Tiger** — the font server and
the window server. A 64-bit process cannot reach them, and there is no shim for
a service that is not there.

So this branch is **rejected, not deferred**. The architecture in §2.0 gets the
same thing by a better route: Tiger's own CoreGraphics and CoreText, in the
32-bit process where those Mach services are reachable, with the compat layer
we have already verified against them.

**The 64-bit process itself is fine**, and that is the part of the spike that
matters and survives. It proved every JIT prerequisite on the box — RWX `mmap`,
the W^X flip, executing generated code, `mach_vm`, 16 MB stacks, 64 GB of
address space — which is what milestone N0 rests on. The
`LC_REEXPORT_DYLIB` demotion trick and the
`dyld_register_image_state_change_handler` shim are recorded above in case
anything else ever needs to load a Leopard library.

Either way the content process still needs curl: Leopard's CFNetwork drags the
same launchd chain, and §2.6 shows curl is now the cheaper option anyway.

### 2.1.1 The third toolchain variant

Branch (a) is not just a different backend, it is a **third build
configuration**, and it should be named as such:

| Variant | Target | SDK | Links against | Runs against |
|---|---|---|---|---|
| UI process | i386 | `sdk/MacOSX10.4u.sdk` + `compat/sdk-overlay` | Tiger frameworks | Tiger frameworks |
| Content, branch (b) | x86_64 | 10.4u SDK, libSystem only | `sysroot-x86_64` static deps | libSystem only |
| **Content, branch (a)** | **x86_64** | **`sdk/MacOSX10.5.sdk` x86_64 stubs** | Leopard framework stubs | **private repointed copies** under `@executable_path` |

The 10.5 SDK already carries x86_64 slices for CoreFoundation and the rest, so
there is something to link against. The runtime copies then need the
`install_name` rewrite that `spike/CAHost/rebundle.sh` already does for
QuartzCore, plus the re-export demotion, plus `DYLD_FORCE_FLAT_NAMESPACE=1` and
the two shim libraries via `DYLD_INSERT_LIBRARIES`.

WebCore under branch (a) would build much closer to a normal `PLATFORM(COCOA)`
configuration — `USE_CG`, `USE_CORE_TEXT`, the CG and CoreText backends — but
still with **no AppKit**, which §2.5 shows is the part that actually hurts. So
branch (a) buys back text and raster quality, not native controls.

### 2.2 Held in reserve: Cairo in the 64-bit process (fallback only)

**Not the plan.** This is what we would build if display-list remoting failed,
and it is recorded at its original length because that decision would have to be
made quickly and with the analysis already done. Its costs — a lookalike theme,
FreeType text, and the whole 32-bit compat investment stranded — are exactly
what §2.0 avoids.

**Cairo is alive in this tree and is the default for a new port.**
`Source/WebCore/platform/graphics/cairo/` is 41 files / 7,362 LOC;
`Source/WebCore/platform/Cairo.cmake` and `platform/SourcesCairo.txt` are live.
Skia replaced Cairo *for GTK and WPE only*:

| Port | `USE_SKIA` | `USE_CAIRO` |
|---|---|---|
| GTK | ON (`OptionsGTK.cmake:158`) | OFF (`:307`) |
| WPE | ON (`OptionsWPE.cmake:158`) | FALSE (`:448`) |
| Win | ON (`OptionsWin.cmake:91`) | ON at `:173` when Skia off |
| **PlayStation** | default OFF | **ON** (`OptionsPlayStation.cmake:276-281`) |
| generic default | **OFF** (`WebKitFeatures.cmake:328`) | implied ON |

A port that never sets `USE_SKIA` gets Cairo for free, and there is no
deprecation notice in the tree. This matters because Skia is ~1M LOC with GN or
Bazel, C++17 and modern-CPU assumptions, against `platform/graphics/skia`'s
14,464 LOC of glue — whereas pixman and cairo are plain C89/C99 autotools.

The rest of the stack, all PlayStation's choices:

| Concern | Choice | In-tree LOC |
|---|---|---|
| Graphics | Cairo (`USE_CAIRO`) | 7,362 |
| Fonts | FreeType + HarfBuzz + **Fontconfig** | 1,975 + 507 |
| Images | WebCore's own decoders (`platform/ImageDecoders.cmake`) | 7,882 |
| Network | curl (`USE_CURL`) | 7,226 |
| Theme | `RenderThemeAdwaita` | 490 + 114 + 1,933 painters |
| i18n | `platform/text/LocaleICU.cpp` | — |
| Keyed coding | `platform/generic/KeyedEncoder/DecoderGeneric.cpp` | — |

`platform/graphics/fontconfig/` does not exist; **fontconfig is unconditional
inside the FreeType backend.** `Source/WebCore/platform/FreeType.cmake` hard-links
`Fontconfig::Fontconfig` and `FontCacheFreeType.cpp` is written against
`FcPattern`/`FcFontSet` throughout. Fontconfig and expat are **already built** in
`toolchain/sysroot-x86_64/usr/lib`, so this costs nothing here — but note that
fontconfig will need a cache and a config file pointing at
`/System/Library/Fonts` and `/Library/Fonts` on the box. Forking
`FontCacheFreeType.cpp` to resolve families from a static table plus `FT_New_Face`
is a ~600 LOC alternative if fontconfig misbehaves on Tiger; do not pre-emptively
take it.

What drops out of the build, and what replaces it:

| Removed | LOC |
|---|---|
| `platform/graphics/cocoa` | 23,194 |
| `platform/graphics/ca` | 14,671 |
| `platform/mac` | 13,513 |
| `platform/cocoa` | 13,047 |
| `platform/graphics/cg` | 10,387 |
| `platform/network/cocoa` | 6,132 |
| `WTF/wtf/cocoa` | 5,078 |
| `platform/graphics/coretext` | 3,507 |
| `platform/network/cf`, `WTF/wtf/cf`, `platform/text/cocoa`, `platform/cf`, `platform/network/mac`, `platform/text/cf` | 5,290 |
| **total removed** | **≈ 94,800** |
| replaced by cairo + freetype + harfbuzz + decoders + curl + texmap + adwaita | ≈ 45,900 |

`USE(CF)` turns out to be genuinely narrow — 30 files in WTF, 11 in
`WebCore/platform` — because it is a bridging layer, not load-bearing.
`WTF::URL` is **pure WTF**, with three `#if USE(CF)` blocks in `wtf/URL.h`
(lines 34, 231, 270), all bridging; the parser is `URLParser.cpp`. Text encoding
goes through `TextCodecICU.cpp`, and we have ICU for x86_64 already.

### 2.3 Branch (c): the hybrid — what you get for free

The brief asks whether a hybrid is possible where the UI process owns everything
AppKit-flavoured. **Partly, and it is not a choice — it is what the architecture
already does.** Delegated to the UI process by WebKit2's own design:

- the open `<select>` dropdown, via `PopupMenuClient` → `Chrome::createPopupMenu`
  → `WebChromeClient::createPopupMenu` → IPC → `WebPopupMenuProxy`;
- context menus, colour and date pickers, `<datalist>`, validation bubbles;
- the pasteboard, IME and input methods, and accessibility, all of which are
  UI-process responsibilities in WebKit2 regardless of port.

So the `<select>` popup can be a real `NSMenu` and look native, because we write
`WebPopupMenuProxy` ourselves and the UI process has AppKit. PlayStation's
`PageClientImpl::createPopupMenu` is a stub, and per-port proxies exist only for
gtk, win and mac — so this is ours to write either way.

What is **not** delegable, because WebCore draws it in-process:
checkboxes, radios, push buttons, text fields and textareas, the *closed*
`<select>` control, `<progress>`, range sliders, spin buttons, **scrollbars**,
focus rings, and every CSS system colour.

### 2.4 The honest cost of branch (b): the page looks foreign

There is no `RenderThemeGtk` any more; GTK, WPE and Win all set
`USE_THEME_ADWAITA` and share `RenderThemeAdwaita`.
`RenderThemePlayStation` is a 78-line subclass of it. That is the template, and
it means **~80 LOC gets you a working theme and a GNOME look**.

Concretely, what the user sees: Aqua window chrome and a native `NSMenu`
dropdown wrapping a page whose checkboxes, radios, buttons, text fields,
sliders, spin buttons, scrollbars and focus rings are flat Adwaita. On Tiger's
brushed-metal and pinstripe desktop that reads as obviously foreign. GNOME
overlay-style thin scrollbars next to an Aqua window frame is the most visible
single tell.

**Superseded by §2.7.** The user has since made Aqua controls a requirement,
and §2.7 designs the real answer: capture actual `NSCell` renderings in the UI
process and blit them in the content process. Adwaita remains the right choice
for **N2**, to get the browser running at a cost of 80 LOC, and is replaced
between N7 and N8.

Text rendering is the second tell and the subtler one. FreeType with its own
hinting and HarfBuzz shaping produces Linux-looking text, not Mac-looking text:
different stem darkening, different hinting, no Quartz-style gamma-corrected
blending. It will be *sharper* than Tiger's CoreText at small sizes and less
Mac-like. This is the one area where branch (a) would genuinely pay, which is
why it is worth finishing that spike even though it should not gate the project.

### 2.5 No AppKit in the content process — the theme consequence in full

This deserves its own section because it is true in **both** rendering
branches, and it is easy to assume branch (a) fixes it. It does not.

**AppKit and HIToolbox are 32-bit-only on Tiger** (§0.1: AppKit is i386/ppc;
HIToolbox likewise, as part of Carbon). So `RenderThemeMac.mm` (2,033 LOC),
`ThemeMac`, and `ScrollbarThemeMac.mm` (700 LOC) — which draw form controls with
`NSCell` and scrollbars with `HIThemeDrawTrack` — **cannot run in the content
process under any branch**, because that is where WebCore paints. Branch (a)
gives back CoreGraphics and CoreText; it does not give back `NSButtonCell`.

This also retires a piece of already-planned work: the
`ScrollbarThemeMac`-on-HITheme rewrite in `logs/webcore-plan.md` §4.3a and
`logs/webkitlegacy-plan.md` §2, roughly 150 new lines plus eight file
exclusions, **is no longer needed** and should be stopped if anyone has started
it.

#### The three options

**Note: §2.7 supersedes the conclusion of this subsection.** The three options
below were written when cross-platform controls were acceptable for v1. They
are kept because the reasoning about what is and is not delegable still holds,
and because option (2) is worth having on record as considered and rejected.
The chosen design is §2.7's capture path, which is option (1)'s successor.

**(1) A cross-platform theme in the content process — the N2 answer.**
`RenderThemeAdwaita` (490 + 114 LOC) with the 22-file, 1,933-LOC
`platform/graphics/adwaita/` painter set, subclassed the way
`RenderThemePlayStation` does it in 78 lines. Controls are drawn with plain
2D primitives, so they work identically over Cairo (branch b) or CG (branch a).
Cost: ~80 LOC. Look: GNOME, as described in §2.4.

A **Tiger-Aqua variant is genuinely feasible** and is the honest middle path.
The Adwaita painters are ordinary path-and-gradient drawing — rounded rects,
linear gradients, strokes — and Aqua's 2005 look is well within that
vocabulary: the lozenge push button, the blue-and-white gradient checkbox, the
pinstripe scrollbar track, the blue focus glow. Nothing needs a bitmap from the
system. Writing `ControlFactoryTiger` against the Adwaita structure is ~2,000
LOC and produces something that reads as Aqua at a glance. It is optional,
deferrable, and the single highest-visibility polish item in the project.

**(2) Remote control rendering — do not do this.** The content process would ask
the UI process to draw a named control at a size into a shared bitmap, and the
UI process would use real `NSCell`s. **WebKit2 has no such mechanism today**;
there is no "render me a control" message anywhere in `Source/WebKit`. It would
mean a new message pair, a control-description serialization covering every
control type and state permutation (pressed, hovered, focused, disabled,
indeterminate, sized, with a tint), a bitmap cache keyed on that description,
and a synchronous round trip on a paint path — or an async one with an
invalidation dance. Estimate **1,500–2,500 LOC of new IPC surface** plus the UI-side
`NSCell` rendering, for controls that would still be a frame behind, still need
a fallback for the first paint, and still not match Aqua's animation. Against
~2,000 LOC for option (1)'s Aqua variant, which has no IPC, no latency and no
cache, this is strictly worse. Record it as considered and rejected.

**(3) Accept cross-platform controls for v1** — rejected by the user. Adwaita
now serves only as the N2 bring-up theme, replaced by §2.7 before ship.

#### What is already native, for free

The WebKit2 design puts a great deal on the UI process side, where AppKit is
available, and all of it transfers from the WebKitLegacy work:

| Concern | Where it lives | On Tiger |
|---|---|---|
| `<select>` **open dropdown** | `WebPopupMenuProxy`, UI process | **native `NSMenu`** — we write the proxy anyway, per-port proxies exist only for gtk/win/mac |
| Context menus | UI process | native `NSMenu` |
| Colour and date pickers, `<datalist>` | UI process | native, or omitted |
| Validation bubbles | UI process | native |
| **Text input / IME** | UI process view | **`NSTextInput` transfers directly.** `WebKitLegacy/mac/WebHTMLView.mm` never migrated off the Tiger-era `NSTextInput` protocol (`logs/webkitlegacy-plan.md` §1.4), and the WebKit2 design already assumes the view owns text input. That whole analysis carries over to the `PageClient` view |
| Spellcheck | UI process `TextChecker` | can use Tiger's `NSSpellChecker` 10.0/10.3 API, or stay off |
| Pasteboard | UI process | native, and the `LegacyNSPasteboardTypes.h` constant work transfers |
| Drag and drop | UI process | native, and the Tiger-era `dragImage:at:offset:…` path transfers |
| Accessibility | UI process | Tiger's informal `NSAccessibility` protocol; the WebCore-side object graph is remote |

So the split is: **chrome, input and menus are native; the page's own controls
are not.** That is a defensible product for v1 and an honest thing to say in a
README.

#### Scrollbars, specifically

Scrollbars are the most visible of the in-page controls because they are on
every page and always at the window edge next to real Aqua chrome.
`ScrollbarThemeAdwaita` draws GNOME overlay-style thin bars. Tiger's Aqua
scrollbars are wide, with a pinstriped track and paired arrows whose placement
follows the `AppleScrollBarVariant` default. The gap is obvious.

Two mitigations, both cheap: draw Aqua-styled scrollbars as the first piece of
the `ControlFactoryTiger` work (they are the highest ratio of visibility to
effort, maybe 300 of the 2,000 LOC), or have the UI process draw the window's
outer scrollbars as real `NSScroller`s and let WebCore draw only overflow
scrollers. The second is tempting but splits the scrolling model across two
processes, so prefer the first.

### 2.6 Networking: the curl backend is upstream and current

A correction to §1.7 and to `logs/webcore-plan.md` §2, which planned to restore
`ResourceHandleCurl` from `refs/webkit-history/curl-resourcehandle/` because the
WebKit1 handle was deleted in 2023.

**In the WebKit2 architecture none of that restoration is needed.**
`Source/WebKit/NetworkProcess/curl/` exists in the tree and is live, maintained
upstream code used by WinCairo and PlayStation:

```
NetworkDataTaskCurl.{cpp,h}      NetworkSessionCurl.{cpp,h}
NetworkProcessCurl.cpp           NetworkStorageSessionCurl.cpp
NetworkProcessMainCurl.cpp       WebSocketTaskCurl.{cpp,h}
```

That is the whole networking stack, including WebSockets, against the same
`platform/network/curl/` transport we already have. PlayStation sets
`USE_CURL ON` and includes `platform/Curl.cmake`.

So the WebKit1 curl work in `refs/webkit-history/curl-resourcehandle/` — the
~600-line `ResourceHandleCurl` plus its delegate, and the ~1,000–1,250 LOC
estimate in `logs/webcore-plan.md` §2.8 — **is dead work under this direction.**
Anyone on that track should stop. The only piece that survives is the CA bundle
finding: curl's compiled-in path points into `toolchain/sysroot-x86_64`, which
does not exist on the box, so `cacert.pem` must ship and be set at runtime via
`CurlSSLHandle::setCACertPath` (the PlayStation pattern).

**The NetworkProcess is mandatory**, not optional: there is no
`ENABLE_NETWORK_PROCESS` flag, and `WebProcessPool.cpp` references
`NetworkProcessProxy` unconditionally. So the architecture is **three
processes**, not two.

---

### 2.7 Aqua controls — WebKit already remotes them

The user's requirement is that controls look **and behave** like Aqua. The
answer turns out to be much cheaper than the two designs this section previously
carried, because **WebKit already serializes native controls and remotes their
drawing to another process.**

**Correction to record.** The previous version of this section designed an
offscreen-`NSWindow` capture cache, ~3,250–4,750 LOC, on the stated basis that
"`ControlPart` appears nowhere in `Source/WebKit`; there is no existing
cross-process control-painting protocol to copy." **That was wrong**, from a
grep that missed it. The mechanism is right there:

| Evidence | Location |
|---|---|
| `DrawControlPart(Ref<ControlPart> part, FloatRoundedRect borderRect, float deviceScaleFactor, ControlStyle style) StreamBatched` | `GPUProcess/graphics/RemoteGraphicsContext.messages.in:124` |
| `ControlStyle` serialized, with its 18-flag `State` OptionSet | `Shared/WebCoreArgumentCoders.serialization.in:2249-2271` |
| `ControlPart` serialized, with its subclass list | `Shared/WebCoreArgumentCoders.serialization.in:2325-2326` |
| Proxy and replay sides | `WebProcess/GPU/graphics/RemoteGraphicsContextProxy.{h,cpp}`, `GPUProcess/graphics/RemoteGraphicsContext.{h,cpp}` |

So the capture cache, the state enumeration, the atlas consumer and the bespoke
message pair are all unnecessary. Both earlier rejections in this section stand
for their original reasons — live `NSView`s for z-order, and the vestigial
`Widget`/`RenderWidget` machinery — but the replacement is upstream's, not ours.

#### 2.7.1 The design

`ControlPart` and `ControlStyle` ride the same display-list stream as everything
else (§2.0.3) into the 32-bit process, which draws them with **real `NSCell`s
and `HITheme`**. In branch (d) that process is the merged UI+render process; in
branches (a) or (b) it is the UI process, and the routing is the same.

The drawing implementation is `compat/aquacontrols.m`, which the nscompat track
is porting from `Source/WebCore/platform/graphics/mac/controls/*Mac.mm` — that
is, from the logic Safari itself uses, adapted to Tiger's cells.

What this yields, and it is worth being precise because it is stronger than
anything the earlier designs offered:

- **Safari's exact pixels.** Not a lookalike, not a capture of a cell we
  configured by hand, but the same `*Mac.mm` control logic driving the same
  `NSCell` API.
- **WebCore's behaviours**, unchanged: press, hover, focus, radio exclusivity,
  space and return activation, tab order. §2.7.3 still applies.
- **Real `NSMenu` popups**, already native through `WebPopupMenuProxyMac`.

#### 2.7.2 Cost

| Piece | LOC |
|---|---|
| Routing `DrawControlPart` to the 32-bit process and into the Tiger drawing code | ~275 |
| Scrollbars, hand-drawn with `HITheme` | ~250 |
| `compat/aquacontrols.m` — the Tiger `NSCell`/`HITheme` drawing itself | nscompat track |
| **Our side** | **≈ 525** |

Scrollbars need their own code rather than riding `ControlPart` because
`ScrollbarThemeMac` is built on `NSScrollerImp`, the 10.7 overlay-scrollbar
class. Tiger has `HIThemeDrawTrack` and `+[NSScroller scrollerWidthForControlSize:]`
instead. The `ScrollbarTheme` surface is metrics-only — 124 LOC, ~32 virtuals,
none pure — and WebCore's layout depends on `scrollbarThickness()` (15 on
Tiger) and the four `thumb`/`track` geometry virtuals at `ScrollbarTheme.h:97-100`.
`ScrollbarThemeAdwaita` (72/224 LOC) is the shape to copy. This also covers
`overflow: scroll` elements, which a UI-process `NSScroller` never could.

Set against the alternatives this section previously proposed: **525 LOC instead
of 3,250–4,750 for the capture cache, or ~2,000 for a hand-drawn painter set
that could never be pixel-exact.**

`spike/aquaatlas` is **done** — 366 images across 18 control types, using
`HIThemeDrawButton` for the window-inactive states AppKit will not render
directly. It keeps two roles: **fallback artwork**, for any state
`compat/aquacontrols.m` cannot draw live, and **the reference** for verifying
that the remoted drawing matches, plus the 17 system colours its `atlas.json`
captures from real `NSColor`.

Three tool gotchas from building it, worth not rediscovering:
`CGBitmapContextGetData` returns NULL on Tiger unless you supply the buffer
yourself; a bare executable, or the first launch of a new bundle, cannot become
active, which is why the inactive artwork needed `HITheme`; and
`HIThemeDrawTrack` draws a whole scrollbar at once where WebCore wants the parts
separately.

**`spike/CAHost` phase 4, the offscreen-view prototype, is cancelled** and
should not be built.

#### 2.7.3 Behaviour — unchanged from the earlier analysis

Most Aqua behaviour does not require AppKit, because WebCore already implements
it and its semantics already match. This table survives the redesign intact:

| Behaviour | Who implements it | Matches Aqua? |
|---|---|---|
| Press state, including drag-out cancel and drag-back re-arm | WebCore, `Element::active()` | yes |
| Hover | WebCore, `Element::hovered()` | yes |
| Focus ring, focus on click and tab | WebCore + `ControlStyle::Focused`; note `outline: none` suppresses it at state extraction (`RenderTheme.cpp:770`), correctly | yes |
| Checkbox and radio toggling, radio group exclusivity | WebCore | yes |
| Space and Return activation | WebCore; `RenderTheme::popsMenuBySpaceOrReturn` carries the Mac convention | yes |
| Tab order | WebCore sequential focus navigation | yes |
| **`<select>` dropdown** | UI process, real `NSMenu` via `WebPopupMenuProxyMac` (77/246 LOC) | **genuinely native** |
| Context menus, colour picker, file picker | UI process, native | native |
| Cocoa key bindings in text fields | UI process `NSTextInput` → WebCore editing commands. See `logs/textinput-plan.md`: a lean Tiger `NSTextInput` view over the C API, ~1,000 LOC, with 5 query messages needing synchronous variants and `NSNotFound` clamped between 32- and 64-bit | yes |
| Slider drag, page-click, arrow keys | WebCore | yes |

Two residual differences, both cheap:

- **Default-button pulse.** Tiger's default button breathes at ~1 Hz. With
  remoted drawing this is actually easier than with captures: the 32-bit process
  owns a real cell and can animate it, driven by a repaint timer.
- **Scroller "jump to here"** (`AppleScrollerPagingBehavior`). Read the default
  in the 32-bit process; WebCore's page-vs-jump decision is already
  parameterised.

One warning carried forward: **avoid nested tracking loops in the 32-bit
process.** `WebPopupMenuProxyMac.mm:192-220` has to synthesize a fake mouse-up
because the menu's nested runloop swallowed the real one (bug 57904). Anything
that spins its own loop inherits that class of bug.

#### 2.7.4 System colours, fonts, and what still cannot be Aqua

System colours come from real `NSColor` in the 32-bit process, feeding
`RenderTheme::systemColor` and the ~20 `platform*Color` virtuals.
`spike/aquaatlas/out/atlas.json` already carries all 17 read from the box.

**Under branch (d), the text-rendering gap closes.** This was the larger
fidelity problem and the strongest remaining argument for branch (a): under
branch (b), FreeType text next to Aqua controls reads as wrong on every page.
Branch (d) rasterizes with **Tiger's own CoreText**, so text is native by
construction — and per §2.0.4 the shaping matches to 0.03 pt. That removes the
concern rather than mitigating it, and is a second independent reason to prefer
(d).

What remains out of reach: **accessibility**. Remote AX is closed on Tiger — it
needs a 10.7 private class and an ObjC runtime in the content process. V1 must
**declare absence** (the web view reports a group role with no children, ~15
LOC) rather than appear broken. Tiger shipped VoiceOver, so this is a known
regression and belongs in the README.

## 3. The three target sites

The user's stated targets are YouTube, React applications and The Verge. Each
exercises a different part of the stack, and together they define what "done"
means. The governing principle from `NOTES.md` is **as much 64-bit as
possible**: everything except the AppKit-facing UI process runs x86_64,
including the NetworkProcess.

### 3.0 Process layout

Three processes, not two, because the NetworkProcess is mandatory (§2.6):

Under branch (d), the recommended primary (§2.0.2):

| Process | Arch | Contains |
|---|---|---|
| UI + render | **i386** | AppKit, window, events, IME, pasteboard, menus; the CARenderer compositing host; **display-list replay against Tiger's real CoreGraphics and CoreText**; Aqua control drawing; CoreAudio |
| WebProcess | **x86_64** | WTF, JSC with the full JIT stack, WebCore, HarfBuzz shaping, image decode, display-list recording, the media pipeline |
| Network | **x86_64** | curl, LibreSSL, nghttp2, the cookie and cache stores |

Running the NetworkProcess 64-bit costs nothing — it is pure C++ over curl with
no framework dependencies — and keeps TLS and HTTP/2 off the WebProcess's cores.
On a 2-core machine a third process is a real scheduling cost, so if measurement
shows contention, the fallback is to run the network code inside the WebProcess
rather than to move it to 32-bit.

Under branches (a) or (b) the middle row keeps the rendering and the first row
shrinks to AppKit and compositing; everything else here is unchanged.

### 3.1 YouTube: Media Source Extensions

YouTube needs `ENABLE_MEDIA_SOURCE`: fragmented MP4 appended by JavaScript
through `SourceBuffer.appendBuffer`. QuickTime and QTKit cannot do this — they
are file- and stream-oriented, have no concept of a JS-driven append, and are
32-bit-only besides. `logs/qtkit-plan.md` remains valid only for a 32-bit
single-process media path, which this architecture abandons.

**DRM is out.** There is no Widevine CDM for this platform and never will be, so
`ENABLE_ENCRYPTED_MEDIA` and `ENABLE_LEGACY_ENCRYPTED_MEDIA` stay off. In
practice YouTube serves non-DRM fMP4 for ordinary videos, so this costs playback
of premium and rented content, not the general case. Say so in the README rather
than letting a user discover it.

#### GStreamer versus ffmpeg-direct

| | GStreamer | **ffmpeg-direct** |
|---|---|---|
| WebCore glue already in tree | 25,662 LOC (`platform/graphics/gstreamer/` 61 files/20,296 + `mse/` 18 files/5,366) | 0 |
| WebCore glue to write | ~0, plus a Tiger video sink by hand | **~4,000–6,000** |
| Third-party to port to libSystem-only x86_64 | **~1.2–1.5M LOC**: libffi, pcre2, gettext, glib, gobject, gio, gstreamer core, -base, -good, -libav | **0 — ffmpeg is already built** |
| Build system | meson, cross-bootstrapped; static-only (`USE_GSTREAMER_FULL`) because Tiger's dyld has no `@rpath` and the plugin registry `dlopen`s | our existing scripts |
| Abstract interface | 32 pure virtuals | 32 pure virtuals |
| Compositing | wants `USE_GSTREAMER_GL` (default ON) + texmap, must be pried off | none — `paint()` only |

**Recommendation: ffmpeg-direct.** 70 of the 79 GStreamer files are GObject
down to the bone; there is no GLib-free subset. Porting GLib to a libSystem-only
10.4 target would become the whole project, and at the end of it we would still
be writing a Tiger video sink by hand. Note also that
`Source/cmake/OptionsPlayStation.cmake` contains **zero** media lines — the
PlayStation template gives nothing here, so a backend is being written either
way.

#### The shape of the work

The port-independent machinery is large and we reimplement none of it:
`Source/WebCore/Modules/mediasource/` is 6,894 LOC, plus
`platform/graphics/SourceBufferPrivate.cpp` (1,971) and `MediaSourcePrivate.cpp`
(745), which own coded-frame processing, `SampleMap`, buffered-range math and
eviction.

What a port must supply is **32 pure virtuals**: `MediaPlayerPrivateInterface`
18, `MediaSourcePrivate` 5, `SourceBufferPrivate` 5, `MediaPlayerFactory` 4.
The real surface is three methods — `appendInternal` (demux one fMP4 segment),
`enqueueSample` (hand samples to a decode queue) and `paint()`. Everything else
is state plumbing.

**There is a complete reference implementation to clone**:
`Source/WebCore/platform/mock/mediasource/` is 1,477 LOC, of which 1,060 is the
backend minus its fake parser. Copy it into
`Source/WebCore/platform/graphics/tiger/` (which already exists, holding
`GraphicsLayerTiger.cpp`), swap `MockBox` for a libavformat `AVIOContext`-backed
`mov` demuxer over the appended `SharedBuffer`, and swap the mock renderer for
libavcodec plus `sws_scale`.

**Frames reach the screen through `paint()`, not through compositing.**
`MediaPlayerPrivateInterface::paint(GraphicsContext&, const FloatRect&)` is pure
virtual and the software path is fully live: `RenderVideo::paintReplaced`
(`Source/WebCore/rendering/RenderVideo.cpp:415-431`) only skips software
painting when `hasAcceleratedCompositing() && supportsAcceleratedRendering()`,
and the latter defaults to `false`. So decode with ffmpeg, `sws_scale` to BGRA,
wrap in a `NativeImage` over a Cairo image surface, and draw into the same
`ImageBuffer` as the rest of the page. Video then rides the existing
shared-memory path to the UI process for free: **no `VideoFrame`, no IOSurface,
no extra IPC surface, no compositing integration.** `videoFrameForCurrentTime()`
and `nativeImageForCurrentTime()` are both non-pure and default to `nullptr`, so
they can be ignored entirely.

#### Codec policy — where to steer YouTube

A Core 2 Duo at 2.2 GHz decodes 720p H.264 acceptably with SSSE3 assembly and
does **not** decode VP9 720p in real time. The ffmpeg build is already
`-O3 -march=core2` with SSSE3 asm via nasm (`deps/build-ffmpeg64.sh`), which is
the right configuration.

The decision point is one function: the backend's
`MediaPlayerFactory::supportsTypeAndCodecs`, forwarded to a static
`supportsType`, exactly as `MockMediaPlayerMediaSource.cpp:57-60, 84-102` does.
Advertise `video/mp4` and `audio/mp4` only, and within those return
`IsSupported` for `avc1.*` and `mp4a.40.*` while returning `IsNotSupported` for
`vp09.*`, `vp9`, `av01.*`, `opus` and `vorbis`. Implement `getSupportedTypes`
with the same two MIME types, since that is what `canPlayType` reports.

YouTube's player calls `MediaSource.isTypeSupported` for every candidate and
picks from the survivors, so this single function forces the `avc1`+`mp4a`
ladder. Note that `MediaSourceTypeSupportedCache` memoizes per content-type
string, so the answer cannot change at runtime without clearing it.

#### Measured on the box, which raises the target substantially

`spike/decodebench.c` on the 2.2 GHz Core 2 Duo, ffmpeg x86_64 `-march=core2`,
best of 3 (`logs/decodebench-tiger.txt`):

| Stream | 1 thread | **2 threads** | swscale → BGRA |
|---|---|---|---|
| H.264 High 480p30 | 177 fps | **255 fps** | 1.4 ms |
| **H.264 High 720p30** | 73 fps | **112 fps (8.9 ms)** | **3.1 ms** |
| H.264 High 1080p30 | 29 fps | **50 fps (20 ms)** | 7.0 ms |
| VP9 480p30 | 112 fps | **154 fps** | 1.4 ms |
| VP9 720p30 | 47 fps | **76 fps** | 3.2 ms |
| AV1 480p30 (dav1d) | 119 fps | **194 fps** | 1.4 ms |
| AAC / MP3 / Opus | 261× / 127× / 122× realtime | | |

Two decoder threads is the right number; a third buys nothing on two cores.

**So the target is 720p H.264, comfortably.** Decode plus convert is ~12 ms of a
33 ms frame budget, leaving two thirds of the machine for YouTube's
JavaScript-heavy player, which is the real constraint on that site. 1080p30 is
27 ms and leaves nothing — decode-only possible, not viable with a page running.

For scale: QTKit 7.6.4's software path managed **~2 fps at 720p**
(`NOTES.md`, `spike/qtrenderertest.m`). This is roughly **50× faster**, and it
is the single clearest argument for the whole split.

Codec policy can therefore relax: **prefer H.264, and allow VP9 up to 480p** if
YouTube insists on it, since 480p VP9 at 154 fps is cheaper than 720p H.264.
Refuse VP9 at 720p and above, and refuse AV1 above 480p.

Aim the first milestone at `isTypeSupported` returning true for
`video/mp4; codecs="avc1.42E01E"` and one keyframe painted.

Audio goes out through the already-proven `spike/audiobridge` ring to the 32-bit
process, which owns CoreAudio (there are no x86_64 CoreAudio, AudioUnit or
AudioToolbox slices). That path measured 0 underruns at ~12 ms latency under 4%
CPU, and the shared-struct 4-byte rule from §0.3 applies to its ring header. The
shape to clone is `RemoteAudioDestinationProxy`'s — an
`AudioDestinationResampler` plus a shared ring buffer.

**Effort** (`logs/media64-plan.md`): `MediaPlayerPrivateFFmpeg` ~**5,300 LOC**
content-side plus ~**700 LOC** on the 32-bit side. MSE is cheap because
`SourceBufferPrivate.cpp`'s coded-frame processing is port-independent; we owe
only `appendInternal`, parsed with libavformat over a **custom AVIO on a growing
buffer**.

**The frame path under branch (d)** has two options, and the second is the one
to aim for:

1. **Decoded frames as shared bitmaps**, painted by the 32-bit process as part
   of the normal display-list replay. Simplest, correct everywhere, and it costs
   a full-frame blit per frame.
2. **A video layer in the compositor.** The 32-bit process already hosts the CA
   compositor, so a decoded frame can become a layer's `contents` directly and
   skip the page-repaint path entirely. `spike/ipc32x64` measured a 1440×900
   BGRA GL upload from the mapping at ~8 ms with no copy, so at 720p this is
   well inside budget.

Start with (1) because it needs nothing new, and move to (2) once compositing is
proven — at which point video costs a texture upload rather than a page repaint.

### 3.2 React applications: the JIT tiers

This is the reason for the whole architecture, and it is the cheapest of the
three targets because the work is upstream's.

All gating lives in `Source/WTF/wtf/PlatformEnable.h`, not in CMake:

| Enable | Location | Condition on x86_64 |
|---|---|---|
| `ENABLE_JIT` | `:717-719` | `CPU(X86_64)` → **1** |
| `ENABLE_DFG_JIT` | `:776-787` | `ENABLE(JIT) && CPU(X86_64) && OS(DARWIN)` → **1** |
| `ENABLE_FTL_JIT` | `:749-755` | no negative gate at 64 bits; default from `WebKitFeatures.cmake:223` |
| `ENABLE_B3_JIT` | `:814-816` | auto-1 whenever FTL is on |
| `ENABLE_CONCURRENT_JS` | `:796-798` | auto-1 whenever JIT is on |

**FTL does not need LLVM** — B3/Air replaced that years ago and there is no
`ENABLE_LLVM` anywhere in the gating. Nothing extra to link.

**Concurrent JIT thread counts are already correct for a 2-core machine.**
`Options::computeNumberOfWorkerThreads` (`runtime/Options.cpp:436-446`) does
`min(cores, max) → max(that, 2) → minus 1`, which on two cores yields **one DFG
thread and one FTL thread**. Do not raise them. The one thing to verify is that
`kernTCSMAwareNumberOfProcessorCores()` returns 2 rather than 0 on 10.4; the
floor of 2 saves us either way, but a wrong value also feeds GC marker counts.

**WebAssembly** is available and worth turning on later.
`ENABLE_WEBASSEMBLY`, `_BBQJIT` and `_OMGJIT` exist
(`WebKitFeatures.cmake:297-299`), both JITs depend on FTL, and x86_64 support is
real — 63 `CPU(X86_64)` sites across `Source/JavaScriptCore/wasm/`. Turning it on
means `ENABLE_C_LOOP OFF`, the full B3/Air build, and verifying the executable
allocator's `mmap` flags (§4). Disable `ENABLE_ZYDIS`, which auto-enables on
`CPU(X86_64) && ENABLE(JIT)` (`PlatformEnable.h:761`) and only costs build time.
YouTube's player does not need wasm; treat it as a follow-up.

#### The blocker in front of all of this

`Source/cmake/OptionsCocoa.cmake:165-199` is **architecture-blind**. It forces
`ENABLE_JIT`, `ENABLE_DFG_JIT`, `ENABLE_FTL_JIT`, `ENABLE_WEBASSEMBLY` and both
wasm JITs OFF and `ENABLE_C_LOOP` ON for all of `TIGER`, and the same list turns
off `ENABLE_VIDEO`, `ENABLE_MEDIA_SOURCE`, `ENABLE_WEB_AUDIO`,
`ENABLE_WEB_CODECS` and `ENABLE_AV1`. It also hardcodes the i386 sysroot at
`:299` and `:469`.

Every one of those is correct for the 32-bit UI process and wrong for the 64-bit
content process. **Splitting that block on target architecture is the first
build-system task**, ahead of everything in §6, because nothing in §3 can be
built until it is done.

### 3.3 The Verge: throughput

Modern content sites are bound by network setup, image decode and compositing
rather than by script.

**HTTP/2 and compression.** curl with nghttp2 gives multiplexing, which matters
most on a high-latency link with many small assets. nghttp2 and brotli are
already built for x86_64. Both are live in the NetworkProcess curl path.

**Image formats.** WebP and AVIF are the two that a 2026 site will actually
serve. `libwebp` and `libavif`+`dav1d` are already built for x86_64, and
WebCore's own decoders (`platform/image-decoders/`, 7,882 LOC) cover png, jpeg,
gif, bmp, ico and webp, with `USE_AVIF` and `USE_JPEGXL` gating the modern two.
Turn AVIF on and JPEG-XL off. Note that AVIF decode on a Core 2 Duo is slow; a
large hero AVIF will be visibly late. Lazy loading (`loading="lazy"`) is
upstream behaviour and needs nothing from us.

**libjpeg-turbo SIMD.** The i386 build used `-DWITH_SIMD=0` because nasm was not
installed at the time (`deps/build-c-deps.sh:151`). **nasm is installed now**, so
the x86_64 build should enable SIMD — JPEG is the single most common decode on a
site like this and the SIMD path is roughly 2–4x.

**Compositing bandwidth, estimated.** A 1440x900 display at 32 bits per pixel is
5.18 MB per full frame; at 60 Hz that is **311 MB/s**. The shared-memory handoff
itself is free — the UI process maps the same pages — so the real costs are
Cairo painting into the buffer, and the UI process turning it into a `CGImage`
and setting it as a `CALayer`'s contents, which CoreAnimation then uploads to
the GeForce 8600M.

Putting numbers on that: the T7500's DDR2-667 dual-channel memory is about
10.6 GB/s theoretical and 5–6 GB/s real, so a full-screen 60 Hz cycle of one
write plus one read is roughly **620 MB/s, about 10–12% of memory bandwidth**,
before any painting. The PCIe upload to the 8600M is not the constraint. The
constraint is that full-screen 60 Hz repaint is not a realistic target on this
machine and should not be designed for. Tiled, dirty-rect-driven updates are,
and that is exactly what `DrawingAreaCoordinatedGraphics` sends: a
`ShareableBitmap` handle plus the dirty rects. A scroll of a Verge article
should touch one or two tile rows per frame, on the order of 20–40 MB/s, which
is comfortable.

`spike/CAHost` already measured the UI-side half of this: 0.5–0.9 ms per frame
for CA rendering with 48 manual 256px tiles, about 5 ms to paint a tile, and
12 live tiles at ~32 MB resident. Those numbers are the budget.

### 3.4 Performance engineering

Ordered by expected return per unit of effort.

| # | Item | Notes |
|---|---|---|
| 1 | **`-march=core2 -O3` everywhere** | The T7500 is Merom: SSE through **SSSE3, but no SSE4.1** (that is Penryn). `-march=core2` is exactly right. Already used for ffmpeg; apply to WTF, JSC, WebCore and every dependency |
| 2 | **libjpeg-turbo SIMD** | nasm is installed; rebuild x86_64 with `-DWITH_SIMD=1`. The i386 build's `WITH_SIMD=0` was a tooling accident, not a decision |
| 3 | **LTO across WTF/JSC/WebCore** | Real gains on a codebase this size, but watch link memory on the build host and confirm cctools ld64 handles the bitcode; if it does not, per-library LTO still helps |
| 4 | **JSC options** | Leave the concurrent-JIT thread counts alone (§3.2). Consider raising `Options::thresholdForOptimizeSoon`-family only after measuring; the defaults are tuned for machines with more cores but also more memory pressure |
| 5 | **Memory** | 6 GB box, 64-bit content process, no compressed memory on Tiger, so swapping is real 2007-laptop disk I/O. Keep the back/forward cache small or off initially. The content process can address far more than it should actually use; budget ~1.5–2 GB before paging hurts |
| 6 | **PGO** | Plausible and unglamorous. JSC and WebCore both benefit; the cost is a two-stage build and a representative profile run. Defer until after the three sites work, then profile on Speedometer |
| 7 | **Accelerate/vecLib** | x86_64 slices exist on Tiger. Marginal for a browser, but available for colour conversion and `sws_scale`-adjacent work if profiling points there |

**What to measure, and when.** Do not benchmark before the thing runs; do not
tune without a baseline.

| Benchmark | Measures | When |
|---|---|---|
| **JetStream 2** | raw JS, JIT tiers, wasm if on | as soon as `jsc` runs; this is the N0 gate's real scorecard |
| **Speedometer 2 and 3** | the React case end to end: DOM, layout, style, JS | once the content process paints |
| **MotionMark**, a subset | compositing and paint throughput; expect poor absolute numbers and use it for regression tracking, not bragging | after N6 |
| **A YouTube 360p playback** | the whole media path, A/V sync, sustained decode | after the media backend lands |
| **A Verge article scroll** | tile upload bandwidth, image decode, memory | after N7 |
| The existing 2M-iteration loop | continuity with the numbers already in `NOTES.md` (2.24 s interpreter, 59 ms for Safari 4.1.3's i386 JIT) | throughout |

Speedometer 2 versus 3 is worth running both: 3 is the current standard, but 2
is closer to the 2010s-era React workloads and more likely to complete on this
hardware.

---

## 4. Process-model details on Tiger

**Memory.** A 64-bit process escapes the ~2.7 GB usable address space of a
32-bit one, which matters on a 6 GB box for exactly the sites the user named:
The Verge's image working set and YouTube's MSE buffers. There is no per-process
limit on Tiger beyond `ulimit`; the practical limit is the 6 GB of RAM and the
absence of any compressed-memory subsystem, so swapping is real disk I/O on a
2007 laptop drive. Budget conservatively and keep the back/forward cache off
initially, as `logs/webkitlegacy-plan.md` §11 already recommends for other
reasons.

**JIT memory.** Plain `mmap` with `PROT_READ|PROT_WRITE|PROT_EXEC`. Tiger has no
`MAP_JIT` (10.7), no W^X enforcement, no code-signing requirement, no hardened
runtime, and no `com.apple.security.cs.allow-jit` entitlement to request. JSC's
`ExecutableAllocator` needs its Darwin path checked for `MAP_JIT` use — the
existing gate in `WTF/wtf/posix/OSAllocatorPOSIX.cpp` already drops `MAP_JIT`
for Tiger per the build journal. This is the one place where Tiger's age is an
outright advantage.

**Sandbox.** None. Tiger predates Seatbelt in any usable form. `ENABLE_SANDBOX_EXTENSIONS`
is already OFF in the Tiger block of `OptionsCocoa.cmake`. The security posture
of this browser is "do not use it for anything that matters", which should be
stated in the README rather than implied.

**Crash isolation.** The real, non-performance benefit of the split: a WebCore
or JSC crash takes down the content process, not the window. The UI process can
show a sad-tab and relaunch. Given that we are running a 2026 engine against a
2005 libSystem with ~500 shimmed functions, this is worth more here than it is
on a normal port.

**Two toolchains, two compat libraries.** `toolchain/bin/tiger-clang{,++}` for
i386 and `tiger-clang64{,++}` for x86_64, with `toolchain/sysroot-i386` and
`toolchain/sysroot-x86_64` beside each other. Both already exist. The 64-bit
`libtigercompat.a` is **C and C++ only** — no ObjC, no ARC entry points, no
Blocks runtime, no `objc2compat`, because there is no 64-bit ObjC runtime to
compat with. That is a much smaller library, and it is already built.

**The content process needs no libdispatch at all** — checked, not assumed.
`WTF/wtf/generic/RunLoopGeneric.cpp` and `WTF/wtf/generic/WorkQueueGeneric.cpp`
exist and are pure WTF: `RunLoopGeneric` is built on `Condition`,
`MonotonicTime` and `RedBlackTree` with no dispatch and no CoreFoundation, and
`WorkQueueGeneric` is built on `RunLoop`. They are selected by **source list,
not by macro** — `Source/WTF/wtf/PlatformPlayStation.cmake:1-5` simply lists
them, alongside `MainThreadGeneric.cpp` and `MemoryFootprintGeneric.cpp`.

That whole file is the template for our WTF source list, and it is 15 files:
the four `generic/`, four `playstation/` (FileSystem, Language, OSAllocator,
UniStdExtras — each needs a Tiger twin), five `posix/`, plus
`text/unix/TextBreakIteratorInternalICUUnix.cpp`, `unix/LoggingUnix.cpp` and
`unix/MemoryPressureHandlerUnix.cpp`.

So `compat/dispatch` stays i386-only and the UI process keeps it. This removes
the largest uncertainty in the process model.

---

## 5. What transfers from the work already done

| Asset | 32-bit UI process | 64-bit content process |
|---|---|---|
| `compat/` ObjC runtime shims (`objc2compat.m`, `arc.m`, `blockclasses.m`) | **yes** | no — no ObjC at 64 bits |
| `compat/nscompat*.m` + `AppKitCompat.h` + `NSCompat.h` | **yes, fully** | no |
| `compat/sdk-overlay/` (AppKit, Foundation, CoreFoundation headers) | **yes, fully** | no |
| `compat/ctcompat.c` (CoreText shims) | yes | **no** — shims Tiger's 32-bit CoreText, which has no 64-bit counterpart |
| `compat/cgcompat.c` (CoreGraphics shims) | yes | **no**, same reason |
| `compat/dispatch/` (libdispatch polyfill) | yes | needs an x86_64 build, or use `WorkQueueGeneric` |
| `compat/libcompat.c` (libc gaps) | yes | **yes** — already built for x86_64, and the unwind-sections fix landed there |
| CA hosting design + `spike/CAHost` | **yes, this is the compositor** | n/a |
| curl + LibreSSL networking backend | n/a | **yes, in the NetworkProcess** — and it is upstream's current code, not a restoration (§2.6) |
| `refs/webkit-history/curl-resourcehandle/` (the WebKit1 handle) | n/a | **no — dead work.** `NetworkProcess/curl/` supersedes it entirely |
| `ScrollbarThemeMac` on HITheme (`webcore-plan.md` §4.3a) | n/a | **no — retired.** WebCore draws scrollbars in the content process, which has no AppKit or HIToolbox (§2.5) |
| `NSTextInput` analysis (`webkitlegacy-plan.md` §1.4) | **yes** — the UI-process view owns text input in WebKit2 too | n/a |
| `logs/qtkit-plan.md` (QuickTime media) | n/a | **no** — QTKit cannot do MSE and is 32-bit-only (§3.1) |
| ffmpeg x86_64 build, `-march=core2` + SSSE3 asm | n/a | **yes — it is the media pipeline** |
| `spike/audiobridge` shm ring | **yes** — it is the 32-bit consumer | **yes** — it is the 64-bit producer |
| cctools ld64 x86_64 stub patch | n/a | **yes, load-bearing** |
| `toolchain/sysroot-x86_64` deps | n/a | **yes** — most of them already built |
| ABI screen (`tools/abi-screen.py`, `logs/abi-screen-cf.md`) | yes | no — nothing to screen against |
| CG/CT/ImageIO behavioural probes | yes | no under branch (b); method only under (a) |
| `logs/webkitlegacy-plan.md` | partially — the AppKit knowledge, not the WebKit1 structure | no |
| 32-bit WebCore compile fixes (49 remaining TUs) | only the generic ones | the generic ones |

The short version: **the UI-side work all transfers, the graphics-shim work does
not, and the toolchain and dependency work transfers to whichever side it was
built for.**

**Branch (d) changes this conclusion, and it is the strongest argument for it.**
Under branch (b) the CoreText and CoreGraphics shim tracks — the two largest
investments after WTF — become UI-process-only assets, and the UI process barely
draws. Under branch (d) that process *is* the renderer, so all of it is load-
bearing: `ctcompat`'s 104 checks against a live oracle, `cgcompat`'s 54 checks
and behavioural fixes, the metric adapters, the ABI screen. Nothing is stranded.
The earlier warning to those tracks is withdrawn.

---

## 6. Milestones and effort

### 6.1 The sequence

| # | Milestone | Gate | New LOC | Days |
|---|---|---|---|---|
| **N-1** | Split the arch-blind Tiger block | `OptionsCocoa.cmake:165-199` forces `ENABLE_C_LOOP` and turns off JIT, video and MSE for all of `TIGER`, and hardcodes the i386 sysroot at `:299`/`:469`. It must now split **three** ways: 64-bit web, 64-bit network, 32-bit UI+render. Nothing below configures until it does | ~150 | 1–2 |
| **N0** | **`jsc` x86_64 runs with the JIT** | `jsc -e 'print(1+1)'`, then JetStream 2 and the 2M-iteration loop. **The whole premise.** Every platform prerequisite is already proven on the box by the Leopard spike, so the remaining risk is JSC itself | ~100 | in flight |
| **N1** | **Web process links** | WTF, JSC and WebCore for x86_64 with **no raster backend** — the pixel-less shareable-bitmap backend (~150), display-list recording, HarfBuzz shaping, the font cache and cascade over the manifest, ffmpeg decode. Plus the three neutral wire encodings and the cross-ABI IPC patch, both of which must land here | 2,500–3,800 | 12–18 |
| **N2** | **UI + render process** | C-API view (`PageClientImpl` + view stack, ~856 from PlayStation), display-list **replay** against Tiger CG/CoreText via `compat/`, the CA compositor from `logs/ca-hosting-design.md`, the ~300-LOC scene applier over the reused ~1,600-LOC layer delta, and the font **metrics service** | 3,000–4,200 | 15–22 |
| **N3** | **`about:blank` end to end** | two processes launched with `fork`+`exec`, the stream connected, a page loaded, a white frame composited on screen. Proves the transport, the serializer symmetry fix, the font handle and the compositor together | 400–700 | 6–10 |
| **N4** | **An HTTPS page with text and images** | the 64-bit NetworkProcess over curl with `cacert.pem` shipped and set at runtime; image decode; **real CoreText rasterization of HarfBuzz-shaped runs** — the first point where §2.0.4's 0.03 pt result is tested on real pages rather than a harness | 600–1,000 | 8–12 |
| **N5** | **Controls, scrollbars, text input** | route `DrawControlPart` into `compat/aquacontrols.m` (~275), `HITheme` scrollbars (~250), the lean Tiger `NSTextInput` view over the C API (~1,000, per `logs/textinput-plan.md`, including the 5 query messages that need synchronous variants and the `NSNotFound` clamp), `WebPopupMenuProxy` as a real `NSMenu` | 1,800–2,400 | 12–16 |
| **N6** | **Video** | `MediaPlayerPrivateFFmpeg` (~5,300 content-side, ~700 on the 32-bit side), MSE via libavformat over a custom AVIO on a growing buffer, audio over the `spike/audiobridge` ring in the `RemoteAudioDestination` shape, codec policy preferring H.264 | ~6,000 | 15–25 |

**N-1 through N5: roughly 54–80 engineer-days** to an interactive browser with
native text and Aqua controls. **N6 adds 15–25.**

### 6.2 New code, by area

| Area | LOC |
|---|---|
| Render side: PlayStation-shaped port + CG/CoreText WebCore + compat layer | 800–1,200 |
| Three neutral wire encodings, both sides | 150–250 |
| Pixel-less shareable-bitmap backend | 150 |
| Font handle de-CoreFoundation-ed, both directions | 300–450 |
| Font metrics service and cache (32-bit side authoritative) | 300–500 |
| Font manifest generator + the 64-bit cache and cascade over it | 800–1,500 |
| HarfBuzz wiring for a CoreGraphics-shaped tree | 400–700 |
| Scene applier building real `CALayer`s (the ~1,600-LOC layer delta is reused) | ~300 |
| Backing store blit and tile plumbing | 200–300 |
| Port glue for the 64-bit process | 1,400–1,600 |
| UI process: view, `PageClientImpl`, launcher (`fork`+`exec`), popup proxy | 3,000–4,200 |
| Aqua controls and scrollbars (§2.7) | ~525 |
| Text input (`logs/textinput-plan.md`) | ~1,000 |
| Accessibility: **declare absence** (~15) | ~15 |
| **To an interactive browser** | **≈ 9,300–12,700** |
| Media (N6) | +6,000 |
| **Everything** | **≈ 15,300–18,700** |

Reused unchanged: ~1,600 lines of the coordinated-graphics layer delta, the
whole stream transport, `NetworkProcess/curl`, and every line of `compat/` that
touches CoreGraphics or CoreText.

### 6.3 Risks, ordered

1. **Serializer asymmetry.** Fails as silent wire corruption, not a build error.
   The three CG-flag-dependent types (§2.0.5) and the cross-ABI alignment patch
   (§0.3) must both land before the first pixel.
2. **Font fallback rewritten** in the recording process over the manifest.
   Missing-glyph bugs live here.
3. **Metrics disagreement** between the sides, making layout and paint disagree.
   Mitigated structurally by making the 32-bit side authoritative.
4. ~~AAT shaping divergence~~ — **measured and retired** (`logs/hb-vs-ct.md`).
5. ~~Does the Leopard stack load~~ — **answered: it does not, for rendering**
   (§2.1). No longer a risk because it is no longer a plan.
6. **The same-clang rule** (§0.3). A build-environment rule, easy to violate by
   accident and confusing when violated.
7. **Shared-memory limits** under aggressive tile allocation. A measurement.
8. **Accessibility is a known regression.** Remote AX is closed on Tiger; v1
   declares absence. Tiger shipped VoiceOver, so this belongs in the README
   rather than in a bug tracker.

### 6.4 Against not splitting at all

`NOTES.md` records that the 2021 pin is off the table by explicit user decision:
*"The port runs the 2026 WebKit tree, period."* So this is a two-way comparison.

| | 2026 tree, interpreter, single 32-bit process | **the split** |
|---|---|---|
| JS on the 2M-iteration loop | 2.24 s | x86_64 JIT + FTL; Safari 4.1.3's i386 JIT does it in 59 ms |
| Processes | one, WebKit1 | three |
| Extra engineering | none beyond the existing plans | ~9,300–12,700 LOC, 54–80 days |
| Rendering | Tiger CG + CoreText, native Aqua | **the same** — real CG, real CoreText, real `NSCell`s |
| **YouTube** | **no** — QTKit cannot do MSE, and managed ~2 fps at 720p | yes, non-DRM, **720p H.264 comfortably** (~50×) |
| **React apps** | unusable at interpreter speed | the reason for the architecture |
| Crash isolation | none | yes |
| CT/CG compat investment | fully used | **fully used — the 32-bit process is the renderer** |
| Address space | ~2.7 GB | full 64-bit |
| Accessibility | works | **regression: declared absent in v1** |
| Risk | low, largely retired | concentrated in N0, then in the serializer |

The trade is a 30–40× JavaScript improvement, working video, and crash
isolation, against roughly three months of engineering and losing accessibility
in v1. Given the stated targets — React apps, YouTube, The Verge, none of which
are usable at interpreter speed — that is the right trade. **Everything still
hinges on N0.**

## 7. What is still open

1. **`jsc64`** — does the x86_64 JIT run, and does FTL? What does JetStream 2
   say? **N0, and the only load-bearing unknown left.** Every platform
   prerequisite it depends on is already proven on the box.
2. **Shared-memory limits** on Tiger under a tile-allocating renderer. A
   measurement, cheap, worth doing before N2.
3. **The arch-blind CMake block** (`OptionsCocoa.cmake:165-199`) now has to
   split three ways. Not a question, just the first task.

Closed since the first draft, recorded so nobody reopens them:

| Question | Answer | Where |
|---|---|---|
| Does the 64-bit side need libdispatch? | No — `RunLoopGeneric`/`WorkQueueGeneric` are pure WTF, selected by source list | §4 |
| Do HarfBuzz and Tiger CoreText agree? | Yes, to 0.008 pt per glyph, AAT included | §2.0.4 |
| Is there a cross-process control-drawing protocol? | Yes — `DrawControlPart`, already serialized upstream | §2.7 |
| Can Leopard's x86_64 frameworks render? | No — they block on 32-bit-only Mach services | §2.1 |
| Is the curl backend dead work? | No — `NetworkProcess/curl` is live upstream. The WebKit1 restoration is the dead work | §2.6 |
| How fast does video decode? | 720p30 H.264 in 12 ms of a 33 ms budget, ~50× QTKit | §3.1 |
| Does the IPC wire format agree? | Only for bare scalars; structs containing 64-bit fields disagree in padding *and* size. Fixed in `webkit-ipc-cross-abi.patch` | §0.3 |
| Two processes or three? | Two — compositing needs the window, and Tiger cannot lend one across processes | §2.0.2 |
