# A 64-bit content process on Mac OS X 10.4.11

Design for option (3) of the JS-performance decision in `NOTES.md`: a
WebKit2-shaped split, with a 32-bit Cocoa UI process and an x86_64 content
process carrying WTF, JSC (for the maintained x86_64 JIT, including FTL) and
WebCore.

Written 2026-09-20 against the fork at `df6cc9ff`. Read-only: nothing under
`WebKit/` was modified. `Source/WebKit` (65 MB, the full WebKit2 tree) was
fetched into the sparse checkout to write this; that is the only change to the
working tree.

The two spikes this was supposed to wait on (`logs/jsc64-spike.md`,
`logs/leopard-x86_64-spike.md`) have not landed yet, so §3 branches on their
outcomes. Where I could settle a question myself with the material already in
the repo, I did, and those answers are marked **measured**.

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

### 0.3 The alignment trap — two teammates' rules that look contradictory

`NOTES.md` records the audio-bridge rule: *"i386 ABI 4-byte-aligns 8-byte
fields; x86_64 8-byte-aligns them; use 4-byte fields only in shared structs."*
The IPC analysis for this document found the opposite-looking result: that
`alignof(long long)` is 8 on both, so IPC's wire alignment agrees.

**Both are correct.** Measured with our own clang, by static assertion:

| | i386-apple-macosx10.4 | x86_64-apple-macosx10.5 |
|---|---|---|
| `_Alignof(long long)` | **8** | **8** |
| `offsetof(struct { unsigned; unsigned long long; }, second)` | **4** | **8** |
| `sizeof(long)` | 4 | 8 |

`_Alignof` reports the *preferred* alignment; struct **field layout** uses the
i386 ABI's 4-byte rule. They are different numbers from the same compiler.

The practical split:

- **Raw structs blitted through shared memory** follow field layout, so the
  audio-bridge rule applies: 4-byte fields only, split 64-bit values into two
  `uint32_t`, verify `offsetof` from both compilers.
- **`IPC::Encoder`/`Decoder`** use `grow(alignof(T), sizeof(T))`
  (`Platform/IPC/Encoder.h:132-147`) and
  `roundUpToMultipleOf<alignof(T)>` (`Decoder.h:241`), i.e. `_Alignof`, which
  agrees. The serialized wire format is therefore ABI-compatible.

Anyone writing a shared struct must be told which of the two rules applies, or
they will apply the wrong one. This belongs in `NOTES.md` next to the existing
audio-bridge rule.

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
`posix_spawn` with the client fd left un-`CLOEXEC`. **Estimate 90–130 LOC.**

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

## 2. The content process rendering backend

### 2.1 Branch (a): Leopard's x86_64 frameworks loaded privately

**Status: not dead, but it chains further than the brief assumed.** The
`leopard` agent is measuring the real binaries; what follows is what I could
establish from the 10.5 SDK stubs already in the repo, and it is a useful
early signal rather than a verdict.

Leopard's x86_64 CoreFoundation has 419 undefined symbols. Against Tiger's
x86_64 libSystem exports, 200 are unresolved, of which **12 are genuine
libSystem version gaps** (present in Leopard's x86_64 libSystem, absent from
Tiger's):

```
_bootstrap_look_up2   _bootstrap_register2  _bootstrap_strerror
_dlopen_preflight     _flsl                 _fstat64
_gethostuuid          _stat64               _vproc_swap_integer
_OSAtomicCompareAndSwapPtr  _OSAtomicCompareAndSwapPtrBarrier
_select$DARWIN_EXTSN
```

That is a **shimmable** list, unlike the CFNetwork 250 lead in `atv/REPORT.md`
which needed private CoreFoundation internals. `flsl` is three lines;
`OSAtomicCompareAndSwapPtr` is a CAS on a pointer, which Tiger has at 64 bits;
`select$DARWIN_EXTSN` is an alias; `stat64`/`fstat64` are struct translations;
the four bootstrap and `vproc` entries are launchd machinery that can fail
gracefully.

**But the remaining 188 undefined symbols are the chain**, and they are the
problem:

- `___objc_personality_v0`, `__objc_empty_cache`, `__objc_empty_vtable`,
  `_class_addMethod`, … — Leopard's x86_64 **ObjC 2.0 runtime**. You would be
  bringing `libobjc` too.
- `_auto_zone_*`, `_auto_assign_weak_reference`, … — **libauto**, Leopard's
  garbage collector, which CF on 64-bit was built against.

So branch (a) is not "load CoreGraphics" but "bring up a private Leopard
userland — libobjc, libauto, CoreFoundation, then ApplicationServices —
inside a Tiger process." Each layer adds its own version gap against Tiger's
libSystem. The 12-symbol result is encouraging for the *first* layer only.

**Recommendation: do not gate the project on this.** Treat it as a possible
later upgrade for text rendering quality (§2.4 explains why text is where it
would pay), not as the primary backend. The spike is still worth finishing,
because a positive result on the real binaries would be worth a lot, but it
should not hold up branch (b).

If it does work, the content process would still need curl for networking
(Leopard's CFNetwork drags the same launchd chain) and would gain CoreGraphics,
CoreText and ImageIO. That is the branch where the existing CG and CT
*behavioural* knowledge partially transfers — the probe results in
`compat/CG-SURVEY.md` and `logs/ct-probe.md` describe Tiger's 32-bit
frameworks, and Leopard's are a different generation, so even then the shims
themselves do not carry over, only the method.

### 2.2 Branch (b): the cross-platform backend — **recommended**

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

The remedy is a `ControlFactoryTiger` written against the structure of
`platform/graphics/adwaita/` (22 files, 1,933 LOC) to redraw eight control types
in Aqua — roughly **2,000 LOC**, and entirely optional. It is worth stating
plainly that this is the price of the branch, because it is the one cost that a
user notices immediately and that no amount of correctness work hides.

Text rendering is the second tell and the subtler one. FreeType with its own
hinting and HarfBuzz shaping produces Linux-looking text, not Mac-looking text:
different stem darkening, different hinting, no Quartz-style gamma-corrected
blending. It will be *sharper* than Tiger's CoreText at small sizes and less
Mac-like. This is the one area where branch (a) would genuinely pay, which is
why it is worth finishing that spike even though it should not gate the project.

---

## 3. Process-model details on Tiger

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

## 4. What transfers from the work already done

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
| curl + LibreSSL networking backend | n/a | **yes** — and it becomes mandatory, not optional |
| `spike/audiobridge` shm ring | **yes** — it is the 32-bit consumer | **yes** — it is the 64-bit producer |
| cctools ld64 x86_64 stub patch | n/a | **yes, load-bearing** |
| `toolchain/sysroot-x86_64` deps | n/a | **yes** — most of them already built |
| ABI screen (`tools/abi-screen.py`, `logs/abi-screen-cf.md`) | yes | no — nothing to screen against |
| CG/CT/ImageIO behavioural probes | yes | no under branch (b); method only under (a) |
| `logs/webkitlegacy-plan.md` | partially — the AppKit knowledge, not the WebKit1 structure | no |
| 32-bit WebCore compile fixes (49 remaining TUs) | only the generic ones | the generic ones |

The short version: **the UI-side work all transfers, the graphics-shim work does
not, and the toolchain and dependency work transfers to whichever side it was
built for.** The CoreText and CoreGraphics shim tracks, which are the two
largest investments after WTF, become UI-process-only assets under branch (b) —
and the UI process barely draws anything. That is the real cost of this
direction, and it should be said out loud to whoever is running those tracks
before they invest another day.

---

## 5. Milestones and effort

### 5.1 Branch (b), the recommended path

| # | Milestone | Gate | Effort |
|---|---|---|---|
| **N0** | `jsc` x86_64 runs on the box with the JIT on | `jsc -e 'print(1+1)'`, then the 2M-iteration loop. **This is the whole premise** — if the x86_64 JIT does not work on Tiger, stop | in flight (`jsc64` agent) |
| **N1** | x86_64 dependency set complete | build pixman, cairo, harfbuzz, curl for x86_64; everything else is already there | 2–3 d |
| **N2** | `PORT=TigerContent` WebCore configures and compiles | clone PlayStation: `OptionsTigerContent.cmake` (250–350 LOC), `PlatformTigerContent.cmake` (150–250), the seven `platform/playstation/`-shaped files (~480), `RenderThemeTigerContent` (~80) | 10–15 d |
| **N3** | WebCore links, and a headless render-to-PNG works | proves Cairo, FreeType, HarfBuzz, the decoders and curl end to end without any IPC | 5–8 d |
| **N4** | WebKit2 content process builds and runs headless | `USE_UNIX_DOMAIN_SOCKETS`, the three ABI fixes, `MessageInfo` widened, the `requires` guard; `Shared/unix/AuxiliaryProcessMain.cpp` | 5–8 d |
| **N5** | Two processes talk | `ProcessLauncherTiger.cpp` (~120), `PageClientImpl` + view stack cloned from PlayStation (~856); a page loads and paints into a `ShareableBitmap` | 8–12 d |
| **N6** | Pixels on screen | UI process wraps the bitmap as a `CGImage` and sets it as a `CALayer`'s contents in the CARenderer host; `logs/ca-hosting-design.md` is the reference | 5–8 d |
| **N7** | Interactive | events, `WebPopupMenuProxy` as a real `NSMenu`, pasteboard, IME, scrolling | 10–15 d |
| **N8** | The three target sites | HTTP/2 via nghttp2, MSE and H.264 via the ffmpeg libs already built, audio via the proven bridge | 15–25 d |

**N0 through N7: roughly 45–70 engineer-days**, comparable to the WebCore
milestone estimate in `logs/webcore-plan.md`, and on top of it rather than
instead of it. N8 is the part that makes YouTube work and is the least
predictable.

New code, by area:

| Area | LOC |
|---|---|
| WebCore port files (`OptionsTigerContent`, `PlatformTigerContent`, theme, screen, MIME, user agent, font database, SSL handle) | 1,600–2,500 |
| WebKit2 glue (launcher, PageClient, view, popup proxy) | 1,200–1,800 |
| IPC ABI fixes | ~30 |
| Optional: fontconfig-free `FontCacheFreeType` fork | +600 |
| Optional: `ControlFactoryTiger` for an Aqua look | +2,000 |
| **Baseline** | **≈ 2,800–4,300** |
| **With both optionals** | **≈ 6,900** |

### 5.2 Branch (a), if the Leopard spike succeeds

Add to N1: bring up a private Leopard x86_64 userland (libobjc, libauto,
CoreFoundation, ApplicationServices) with ~12 libSystem shims for CF plus
whatever the other three layers need, all under
`@executable_path`-relative install names as `spike/CAHost/rebundle.sh` already
does for QuartzCore. Subtract the cairo, pixman, freetype and fontconfig
builds; subtract `RenderThemeAdwaita` in favour of something CG-based; keep
curl regardless.

**Net effect: probably a wash on effort, a clear win on text rendering, and a
large increase in risk** — four layers of a pre-release-adjacent OS loaded into
a process on a different OS. It also reintroduces an ObjC runtime into the
content process, which changes §3's "C-only 64-bit compat library" conclusion.

Do not sequence N2 behind it. If the spike lands positive, fold it in at N3 as
an alternative graphics backend behind a build option, not as a prerequisite.

### 5.3 Comparison with the alternatives

`NOTES.md` records that **the 2021 pin is off the table by explicit user
decision (22:50)**, not merely deprioritised: *"The port runs the 2026 WebKit
tree, period. JS performance comes from the 64-bit content process with today's
x86_64 JIT; if the 64-bit path failed, the answer would be the 2026 tree on the
interpreter, never an older browser."* So this is a two-way comparison, not a
three-way one.

| | (1) 2026 tree, interpreter, single 32-bit process | **(3) 64-bit content process** |
|---|---|---|
| JS on the 2M-iteration loop | 2.24 s | x86_64 JIT + FTL; Safari 4.1.3's i386 JIT does it in 59 ms, so expect that order |
| Architecture | one process, WebKit1 | two processes, WebKit2 |
| Extra engineering | none beyond the existing plans | 2,800–4,300 LOC, 45–70 days |
| Rendering | Tiger CoreGraphics + CoreText, native Aqua | Cairo + FreeType, Adwaita controls, foreign look |
| Crash isolation | none | yes |
| CT/CG shim investment | fully used | stranded (UI process only) |
| Address space | ~2.7 GB | full 64-bit |
| Risk | low, and largely retired | concentrated in N0 |

The honest framing: option (3) buys roughly a 30–40x JavaScript improvement and
crash isolation, and pays for it with a foreign-looking page, a second
architecture, and the stranding of two of the project's largest completed
tracks. Given the user's stated targets — React apps, YouTube, The Verge —
none of which are usable at interpreter speed, that trade is the right one. But
the stranding cost is real and is not visible from the JS benchmark alone.

**Everything hinges on N0.** If `jsc` x86_64 with the JIT does not run on
Tiger's libSystem, the entire branch collapses to option (1), and the work in
§4's "does not transfer" column was spent for nothing. N0 should stay ahead of
every other milestone here, and nothing in §5.1 should start before it reports.

---

## 6. Open questions for the spikes

1. **`jsc64`** — does the x86_64 JIT run at all, and does FTL? Does
   `ExecutableAllocator` need a Tiger gate beyond the `MAP_JIT` one already
   applied? What does the 2M-iteration loop measure?
2. **`leopard`** — do the *real* Leopard x86_64 binaries (not the SDK stubs
   measured here) resolve against Tiger's x86_64 libSystem, and how far does the
   libobjc and libauto chain go?

The third question I had listed here — whether the content process needs an
x86_64 libdispatch — is **answered in §3: it does not.** `RunLoopGeneric` and
`WorkQueueGeneric` are pure WTF and are selected by source list.
