# WebKit2 split survey — build system and source tree

Round 3.5, read-only. Agent wkcmake, 2026-09-20. No files under `WebKit/` were
modified for this survey. WebKit main @ d2f52605, `Source/WebKit` fully present.

Scope: the build-system and source-tree angle on the split the user has set —
a thin 32-bit Cocoa UI process on Tiger AppKit plus a 64-bit content process on
Tiger's x86_64 libSystem, with no Apple frameworks on the 64-bit side. The
rendering backend is still the spikes' call and is excluded from every estimate
below.

---

## 1. Template port for the 64-bit content process

**PlayStation, by a wide margin.** It is the only in-tree port that is
simultaneously socket-based in its IPC, GLib-free, CoreFoundation-free,
sandbox-free, crash-reporter-free, on the generic RunLoop and WorkQueue, and
already structured as `PORT` + a `-D` + per-project `Platform<Port>.cmake` with
no edits to `Platform.h` at all.

### Requirements matrix

| | PlayStation | WPE / GTK | Cocoa |
|---|---|---|---|
| IPC transport | `Platform/IPC/unix/ConnectionUnix.cpp` (576), socketpair + SCM_RIGHTS | `Platform/IPC/glib/ConnectionGLib.cpp` (631), GSocket over the same pair | `Platform/IPC/cocoa/ConnectionCocoa.mm` (773), mach_msg |
| Readiness | dedicated `Thread::create("SocketMonitor")` around blocking `select()`, `ConnectionUnix.cpp:333-365` | GSocketMonitor in the GLib loop | libdispatch mach sources |
| SharedMemory | `WebCore/platform/unix/SharedMemoryUnix.cpp` (198) | same | `SharedMemoryCocoa.mm` (264) |
| Launcher | `UIProcess/Launcher/playstation/ProcessLauncherPlayStation.cpp` (122) | `glib/ProcessLauncherGLib.cpp` (354), ~200 of it sandbox | `cocoa/ProcessLauncherCocoa.mm` (596), XPC |
| RunLoop | `WTF/wtf/generic/RunLoopGeneric.cpp` (363) | GLib | CFRunLoop |
| WorkQueue | `WTF/wtf/generic/WorkQueueGeneric.cpp` (85) | GLib | libdispatch |
| Crash / sandbox | none; `ENABLE(SANDBOX_EXTENSIONS)` off gives inline stubs at `Shared/SandboxExtension.h:159` | optional Breakpad, optional bubblewrap | required |
| Port cmake | `PlatformPlayStation.cmake` 182 | 840 / 675 | 2880 |

### Three findings that make this cheaper than it looks

**The unix IPC path is already Darwin-aware.** `ConnectionUnix.cpp:50` defines
`MSG_NOSIGNAL` as 0 for `OS(DARWIN)`, and lines 53-59 deliberately fall back
from `SOCK_SEQPACKET` to `SOCK_DGRAM` on Darwin with a comment explaining why.
Someone upstream already walked this path.

**`SharedMemoryUnix.cpp` needs no edits.** Its three-way selection at lines
78-118 prefers Linux memfd, then FreeBSD anonymous shm, then plain `shm_open`
with an immediate `shm_unlink`. Both of the first two are cmake feature
detections (`Source/cmake/OptionsCommon.cmake:358` and `:377`) that come out
OFF on Tiger, so the portable path is selected automatically. Two things to
test rather than code around: the generated name fits Darwin's 31-character
limit with room to spare, and Tiger's POSIX shm segment limits are low for a
process that allocates aggressively. That second one is a sysctl, not a patch.

**The generic event loop is selected automatically.** `WTF/wtf/PlatformUse.h:228-241`
falls through to `USE_GENERIC_EVENT_LOOP` when GLib is off, Windows is off and
`PLATFORM(COCOA)` is off. `RunLoopGeneric.cpp` needs pthreads and a monotonic
clock and nothing else. Note the consequence: it has no file-descriptor event
source, which is exactly why the PlayStation socket monitor thread exists. A
kqueue-backed RunLoop is a later optimisation, not a prerequisite.

### The one genuine gap

`Platform/IPC/unix/IPCSemaphoreUnix.cpp` (136) is built on Linux `eventfd`.
Tiger has none. Two ways out: widen the `PLATFORM(COCOA)` guard at
`Platform/IPC/IPCSemaphore.h:34` to reuse `Platform/IPC/darwin/IPCSemaphoreDarwin.cpp`,
which is pure Mach with no CF and no ObjC but stores a `MachSendRight` that
cannot travel over SCM_RIGHTS; or write a pipe-backed counting semaphore in
about 60 lines. It only matters for the stream connection and the GPU process,
both of which stay off, so stub it first.

---

## 2. Can PLATFORM(COCOA) be off on one side and on on the other?

**No, not for the WebKit2 layer, and this is the most important finding in the
survey.**

`Source/WebKit/Scripts/generate-serializers.py` emits `#if <condition>` directly
into the generated serializers, from the conditions written in the
`.serialization.in` files. Two of those conditions are `USE(CF)` (line 807) and
`USE(GLIB)` (line 815), and `Shared/Cocoa/` holds **115 `CoreIPC*` files** that
serialize CF and Objective-C objects behind `PLATFORM(COCOA)`. If one side is
Cocoa and the other is not, the two sides compile different serializer sets
from the same generated source and the wire format diverges.

The way out is the PlayStation shape taken seriously: build WebKit2 itself with
`PLATFORM(COCOA)` off on **both** sides, and confine AppKit to the app shell and
the view class, which talk to WebKit2 only through the C API. `OS(DARWIN)` stays
on for both, which is what we want, since the Darwin branches in the unix IPC
code are the ones that make it work.

### 32/64 ABI hazards in the message set

The wire format is mostly fixed-width by design. Container lengths are
explicitly widened to `uint64_t` (`WTF/wtf/ArgumentCoders.h:463` and `:65`),
`ObjectIdentifier` is a `uint64_t` (`WTF/wtf/ObjectIdentifier.h:73`), and
`SharedMemoryHandle::m_size` is deliberately `uint64_t` rather than `size_t`
(`WebCore/platform/SharedMemory.h:109`).

The leak is the generic arithmetic coder at `WTF/wtf/ArgumentCoder.h:58-71`,
which puts native `sizeof` and native `alignof` on the wire for any arithmetic
type. Measured with clang for both targets: `uint64_t`, `double`, `long double`,
`int`, `float`, `bool` and the char types all agree in both size and alignment.
`long`, `size_t`, `ptrdiff_t`, `uintptr_t` and pointers do not.

That alignment agreement is load-bearing, and it holds **only because clang
reports `alignof(long long)` as 8 on i386 Darwin**. Tiger's own GCC 4.0 reports
4, and every message with a 64-bit field would desynchronise. Both sides must be
built with the same clang. This belongs in NOTES.md as a build rule.

Complete list of what would not agree today:

| Offender | Location |
|---|---|
| `Vector<size_t, 1> snapAreaIndices` | `Shared/WebCoreArgumentCoders.serialization.in:2932` |
| `size_t numImages` | `Shared/XR/PlatformXR.serialization.in:351` |
| `MessageInfo`, three `size_t` fields blitted raw as the socket framing header | `Platform/IPC/unix/UnixMessage.h:89-92` |

Two message fields and one transport header. There is no memcpy or bitwise fast
path in the generator, and enums go through `std::to_underlying`, so
`enum class X : uint8_t` is stable. The durable fix is a `requires` clause on
the arithmetic coder banning `long` and `unsigned long`, which turns an
open-ended audit into two compile errors today.

`Attachment` is a `MachSendRight` or a `UnixFileDescriptor`, both 32-bit on both
architectures.

---

## 3. The UI process

`UIProcess/Cocoa/` 32,490 LOC, `UIProcess/mac/` 26,456, `UIProcess/API/Cocoa/`
66,784, `UIProcess/API/mac/` 4,078, `UIProcess/RemoteLayerTree/` 14,232. About
144,000 lines of Cocoa UI surface, and almost none of it is required by a Cocoa
UI process as such. It exists to serve WKWebView.

**RemoteLayerTree is excluded by the user's own constraint**, since it serializes
CALayer trees and the content process has no Objective-C runtime.

**WKView does not save us.** `API/mac/WKView.mm` is 1,441 lines of shim over
`mac/WebViewImpl.mm` (8,579) plus its header (1,302), and WebViewImpl is where
every 10.7-through-10.15 AppKit dependency lives: layer-backed views,
`NSTextInputClient`, scroll view content insets, `NSDraggingSession`,
`NSTouchBar`, the accessibility protocols. `UIProcess/mac/AppKitGestures/` is
entirely `NSGestureRecognizer`, which is 10.10. Two files are Swift.

**The C API is the answer, and mac already builds it.** The 54 shared
`UIProcess/API/C/*.cpp` entries in `Sources.txt` are pure C++ wrappers over
`WebPageProxy` and `WebProcessPool`, Cocoa-free, and every CMake port builds
them. What mac lacks is a C `WKView`: `API/C/mac/` is 13 files and 718 lines of
context and frame privates with no view. PlayStation, Windows, GTK and WPE each
ship their own, and `API/C/playstation/WKView.{h,cpp}` plus `WKViewClient.h`
(335 lines together) is the model, notable because `WKViewCreate` there takes no
native window handle at all.

**PageClient is smaller than it looks.** `UIProcess/PageClient.h` is 912 lines
with 210 pure virtuals, but only **57 sit outside any `#if`**; the rest vanish
with the features off. Implementation sizes: mac 1,663, GTK 642, WPE 625,
PlayStation 552, Windows 459. PlayStation is the template, paired with
`UIProcess/PlayStation/PlayStationWebView.{h,cpp}` (279), which is exactly the
shape of a hand-written NSView-backed view object.

**The backing-store path is alive in main.** This matters, because it is the
only drawing area that asks nothing of the content process that Tiger's 64-bit
userland cannot give.

- `UIProcess/CoordinatedGraphics/DrawingAreaProxyCoordinatedGraphics.{h,cpp}` (527), used by Windows and PlayStation as well as GTK and WPE
- `Shared/UpdateInfo.h` (69), gated `#if !PLATFORM(WPE) && !PLATFORM(GTK) && (USE(COORDINATED_GRAPHICS) || USE(TEXTURE_MAPPER))` at line 27, carrying a `ShareableBitmap::Handle`, dirty rects and a scroll delta, all fixed-width
- `UIProcess/BackingStore.h` (88), same gate
- `WebCore/platform/graphics/ShareableBitmap.*` intact, and `WebCore/platform/graphics/cg/ShareableBitmapCG.mm` exists

The content-process half, `WebProcess/WebPage/CoordinatedGraphics/DrawingAreaCoordinatedGraphics.cpp`
(712), asserts no layer tree host on the non-accelerated path, creates a
`ShareableBitmap`, paints the dirty rects into it and sends the handle. No GL,
no CF, no ObjC.

**One file is missing.** `BackingStore` has a Cairo backend (133) and a Skia
backend (123) and **no CG backend**. Nothing was removed; Apple went straight to
RemoteLayerTree and never needed one. That is roughly 130 to 200 lines mirroring
the Cairo file, blitting the shareable bitmap's CGImage into a CGContext or
into our CARenderer host.

### Do not build (replaceable by the cross-platform or C-API equivalents)

All of `UIProcess/API/Cocoa/` (66,784), all of `UIProcess/API/mac/` (4,078), all
of `UIProcess/RemoteLayerTree/` (14,232), `mac/TiledCoreAnimationDrawingAreaProxy`,
`mac/WebViewImpl.{h,mm}` (9,881), and the delegate bridges
`Cocoa/NavigationState.mm` (1,810) and `Cocoa/UIDelegate.mm` (2,360), which
exist solely to translate C-API client structs into Objective-C delegates.

### Would need heavy gating if kept, ranked by pain

`mac/WebViewImpl.mm` (8,579), `API/Cocoa/WKWebView.mm` (7,425),
`mac/AppKitGestures/WKAppKitGestureController.mm` (2,164, delete outright),
`API/mac/WKWebViewMac.mm` (2,305), `mac/WKFullScreenWindowController.mm` (1,125),
`mac/WebContextMenuProxyMac.mm` (1,194), `mac/ViewGestureControllerMac.mm` (763),
`mac/WKImmediateActionController.mm` (518), `mac/WKTextFinderClient.mm` (348),
`Cocoa/_WKWarningView.mm` (651), the two Swift files, and the four files that
import frameworks 10.4 does not have.

### Worth keeping from the Cocoa tree

`UIProcess/Cocoa/WebPasteboardProxyCocoa.mm` (1,155),
`UIProcess/mac/TextCheckerMac.mm`, `API/C/mac/WKProtectionSpaceNS.mm`. The
launcher, `Launcher/cocoa/ProcessLauncherCocoa.mm` (596), is XPC from top to
bottom and gets replaced rather than kept.

---

## 4. CMake shape

**One configure cannot do it.** `PORT` is a single cache variable that drives
`WEBKIT_OPTION_DEFAULT_PORT_VALUE`, the `Platform<Port>.cmake` includes and the
generated `cmakeconfig.h`; `CMAKE_OSX_ARCHITECTURES` with two values would build
both slices with one flag set, which is precisely what we cannot have. Two build
directories, two toolchain files:

- `toolchain/tiger.cmake`, i386, for the UI process and its shell
- a new `toolchain/tiger64.cmake`, x86_64, for the content process and the network process

Both should use `PORT=Tiger64`-shaped options with `PLATFORM(COCOA)` off in
WebKit2, for the serializer reason in section 2.

**Keeping the generated IPC sources consistent.** The generator is deterministic
and architecture-independent: it reads the `.messages.in` and `.serialization.in`
files and emits C++ whose only variability is the `#if` conditions it copies
through. So two build directories running it independently produce byte-identical
output, and the real requirement is that **every `ENABLE_` and `USE_` that appears
in a serialization condition has the same value in both build directories**. The
lazy way to enforce that is a single shared `.cmake` fragment holding the feature
set, included by both option files, plus a build step that diffs the two
`DerivedSources/WebKit/` trees and fails on any difference. That check is cheap
and it catches the whole class of problem.

**Bundle layout.** No XPC on Tiger, so nothing can be an XPCService. The
64-bit executables go in `Contents/Resources/` or a plain `Contents/Helpers/`
directory as ordinary Mach-O executables, launched with `posix_spawn` by our own
`ProcessLauncherTiger.cpp`. The contract the child expects is already generic and
documented by `Shared/unix/AuxiliaryProcessMain.cpp:60-83`:

```
argv[1] = decimal process identifier
argv[2] = decimal inherited file descriptor number
```

The client end of the socketpair is left without close-on-exec so it survives the
spawn. Windows does the same thing with an inherited handle number in its command
line, so this is the sanctioned non-XPC pattern rather than an invention of ours.

---

## 5. NetworkProcess: separate, not folded in

**Recommendation: a separate 64-bit NetworkProcess.**

There is no in-process network mode left in main. The old `ENABLE(NETWORK_PROCESS)`
toggle is gone, `WebsiteDataStore::networkProcess()` returns a
`NetworkProcessProxy` unconditionally, and folding it into the content process
would mean inventing a code path upstream does not have and maintaining it
against every future change to the network session plumbing. That is a fork, and
we would be paying for it forever to save one process.

Against that, the costs of keeping it separate are small and already paid for:

- `NetworkProcess/curl/` exists, seven files, and `Platform/Curl.cmake` is how
  PlayStation wires it up. We have already built curl with LibreSSL, working TLS
  1.2 and SNI, and verified it on the box.
- `NetworkProcess/EntryPoint/playstation/NetworkProcessMain.cpp` is 81 lines.
- The launcher and the IPC connection are the same code as for the content
  process, so the second process is nearly free once the first one works.

The one thing to watch is memory on a 2 GB Core 2 Duo with three processes. That
is a measurement to take, not a reason to fork.

---

## 6. LOC estimates

Excluding the rendering backend, which the spikes decide.

### Content process side, about 1,400 to 1,600 lines

| Item | Model | Est. |
|---|---|---|
| `Source/cmake/OptionsTiger64.cmake` | OptionsPlayStation minus EGL, libwpe and the module copying | 200 |
| `Platform<Tiger64>.cmake` in WTF, JSC, WebCore, WebKit | 42 / 15 / 173 / 185 | 350 |
| `WTF/wtf/PlatformEnableTiger64.h` | `PlatformEnablePlayStation.h` (48) | 50 |
| Socket monitor | widen two `#if PLATFORM(PLAYSTATION)` in `ConnectionUnix.cpp` | 4 |
| IPC semaphore | Mach reuse or a pipe semaphore; stub first | 0-80 |
| `ProcessLauncherTiger64.cpp` | `ProcessLauncherPlayStation.cpp` (122), posix_spawn in place of the proprietary call | 120 |
| Entry points, WebProcess and NetworkProcess | `EntryPoint/unix/` (32), `playstation/` (81) | 110 |
| `WebProcessTiger64.cpp` and friends | `WebProcess/playstation/*` (54 + 77) | 110 |
| Proxy and stub classes: WebProcessPool, WebPageProxy, WebsiteDataStore, InjectedBundle, WebPreferences | the PlayStation equivalents | 300 |
| WTF file system, language, allocator | mostly existing `posix/` and `unix/` files | 120 |
| SharedMemory, RunLoop, WorkQueue, AuxiliaryProcessMain | no change | 0 |

About 1,100 of that is mechanical copy-and-strip from the PlayStation files, plus
roughly 550 lines of CMake.

### UI process side, about 3,000 to 4,200 lines

| Item | Model | Est. |
|---|---|---|
| `PageClientImplTiger`, the 57 unconditional pure virtuals | `UIProcess/PlayStation/PageClientImpl.*` (552) | 600-750 |
| A view model object: event routing, focus, cursor | `PlayStationWebView.*` (279) or `win/WebView.cpp` (972) | 400-600 |
| A C-API `WKView` for mac | `API/C/playstation/WKView.*` (335) | 300 |
| `WKTigerView : NSView`, drawRect blit and event conversion | new | 500-700 |
| `BackingStoreCG.mm`, the missing file | `UIProcess/cairo/BackingStoreCairo.cpp` (133) | 130-200 |
| DrawingAreaProxy | reuse `DrawingAreaProxyCoordinatedGraphics` (527) as is | 0-150 |
| Event conversion from Tiger `NSEvent` | the Windows equivalents | 400-600 |
| Glue: text checker stub, popup menu on `NSMenu`, cursor mapping | — | 400-600 |

For comparison, gating WKWebView into shape means carrying about 27,100 lines
with more than 200 call sites on APIs that postdate 10.4, and it drags in the
14,232 lines of RemoteLayerTree that we cannot use at all. The ratio is roughly
seven to one in favour of writing the UI process by hand on the C API.

---

## 7. Summary of recommendations

1. Clone PlayStation as `PORT=Tiger64` for the 64-bit side. It is the only
   GLib-free, CF-free, sandbox-free, generic-RunLoop port in the tree.
2. Build WebKit2 with `PLATFORM(COCOA)` off on **both** sides. Confine AppKit to
   the app shell and the view class, which use the C API only. Otherwise the
   serializer sets diverge and the wire format breaks.
3. Take the unix socket IPC transport, not Mach. It needs no libdispatch, no XPC,
   and it is already Darwin-aware upstream.
4. Take `DrawingAreaCoordinatedGraphics` on its non-accelerated path, and write
   the one missing file, `BackingStoreCG.mm`.
5. Write the UI process by hand on the C API, modelled on PlayStation. Do not
   gate WKWebView.
6. Keep the NetworkProcess separate. Upstream has no in-process mode and the
   curl backend and entry point already exist.
7. Two build directories, one shared feature-flag fragment, and a build step that
   diffs the two generated IPC source trees.
8. Build both architectures with the same clang. The 32/64 wire compatibility
   depends on clang reporting `alignof(long long)` as 8 on i386; Tiger's GCC 4.0
   reports 4 and would break every message with a 64-bit field.
9. Fix the three known ABI offenders and add a `requires` clause to the arithmetic
   argument coder banning `long` and `unsigned long`, so the compiler finds any
   future one.

Open items for whoever picks this up: Tiger's POSIX shared-memory segment limits
under an allocation-heavy content process, and memory headroom for three
processes on a 2 GB machine. Both are measurements, not design questions.
