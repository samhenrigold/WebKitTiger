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

---

# Addendum: text input, native widgets, accessibility

Read-only, same rules. Added for the user's requirement that the port deliver
real Aqua behaviour rather than stubs.

## A. The UI-process text input path

### What the modern design actually does

`UIProcess/API/mac/WKView.mm` is a red herring. All eighteen of its text-input
methods are empty stubs at lines 511-612. `API/Cocoa/WKWebView.mm` contains no
text input at all. The Mac text-input category is declared at
`API/mac/WKWebViewMac.mm:110` and every method there is a one-line forward. All
of the machinery is in `UIProcess/mac/WebViewImpl.mm`.

The collector is `WebViewImpl` itself, holding
`Deque<Vector<WebCore::KeypressCommand>> m_collectedKeypressCommands`
(`WebViewImpl.h:1183`). `collectKeyboardLayoutCommandsForEvent` at
`WebViewImpl.mm:5930-5954` pushes an empty queue at the front, re-enters AppKit
through `interpretKeyEvents:`, and pops what accumulated. AppKit's callbacks
into `doCommandBySelector:` (`:6100`) and `insertText:` (`:6116`) append to that
front queue instead of executing.

**The decisive architectural fact: the selector round trip is already
string-based and platform-neutral.** `WebCore/platform/KeypressCommand.h` (82
lines) holds a `String commandName` and plain WTF types with nothing Cocoa in
it. `NSStringFromSelector` at `WebViewImpl.mm:6105` and `NSSelectorFromString`
at `PageClientImplMac.mm:828` are the only two Objective-C touches in the whole
path. The commands travel inside the key event itself
(`Shared/WebKeyboardEvent.h:64`) and the content process maps them to editing
commands through a static string table in `WebPageMac.mm`. A 64-bit process with
no Objective-C runtime can execute `"moveLeft:"` and `"deleteBackward:"` without
any AppKit whatsoever.

`Shared/EditorState.h` (192 lines) is likewise entirely WebCore and WTF value
types. Every Cocoa-conditional field is an enum, a colour, a rect or a string.
The one Cocoa-named type in the path, `WebCore::AttributedString`, is a
serializable struct that becomes an `NSAttributedString` only at the endpoints.
It crosses the boundary cleanly.

### What Tiger cannot have

Modern WebKit answers every text-input **query** through the async client
protocol, the completion-handler family that arrived in 10.6. The six
synchronous protocol methods are deliberately dead, asserting not-reached at
`WebViewImpl.mm:6725-6760`. Tiger's informal protocol is entirely synchronous:
marked range, first rect for a character range and character index for a point
must return a value immediately. **The entire query half of the modern design
rests on an interface Tiger does not have.**

Mapping the twelve methods: four are exact, `doCommandBySelector:`, `unmarkText`,
`hasMarkedText` and the valid-attributes list. Five lose a parameter, since
Tiger has no replacement range on insert or set-marked-text and no actual-range
out-parameter on the two query methods. Six lose their async plumbing entirely.
One, the conversation identifier, is Tiger-only and must be added; WebKit1's
HTML view returned self.

The two-pass split between the input method and the keyboard layout, which is
what most of the complexity in `interpretKeyEvent` exists to manage, cannot be
expressed on Tiger at all. AppKit routes to the input manager internally. That
deletes roughly 120 of the 140 lines of that function, which is a simplification
rather than a loss.

Three things must be built rather than dropped:

1. **Sync variants of five query messages.** The async implementations already
   exist in `WebPage.messages.in` at lines 612-621; each needs a sync wrapper,
   about five lines of message definition and forty of handler.
2. **The staging logic**, `WebViewImpl.mm:6187-6250` and `6288-6358`, roughly
   115 lines. The UI process locally applies queued insertions to the reply so
   that modeless input methods see the cursor they expect. It exists precisely
   because even an async reply is too stale for an input method. Without it
   Korean Hangul and Vietnamese Telex fall out of modeless mode permanently,
   which is exactly the real Aqua behaviour the user asked for.
3. **A clamp at the not-found boundary.** Tiger's character-index method returns
   a 32-bit unsigned with `NSNotFound` at the 32-bit maximum, while the IPC reply
   is a `uint64_t` and WTF's not-found sentinel is the 64-bit maximum.

Spell checking is mostly fine. Tiger has the document tag, learn, ignore, the
spelling panel and guesses, all exact. What has no Tiger counterpart is the
unified `checkString:` call from 10.6, which needs roughly 80 to 100 lines
rewritten against the older per-word loop. Everything automatic, the
substitutions, the correction panel, the candidates and inline predictions, is
10.6 through 13.0 and corresponds to user interface Tiger never had. That takes
`UIProcess/mac/TextCheckerMac.mm` from 599 lines to about 200 and deletes
`UIProcess/mac/CorrectionPanel.*` outright.

### Estimate, and the recommendation

**Write a lean custom view against Tiger's informal protocol: about 755 lines,
plus 200 for the text checker and 45 for the sync message wrappers.** The
irreducible set is twenty-one methods: twelve from the informal protocol, five
responder overrides, and four internal pieces. Two of the internal four are not
optional. The event re-send, `WebViewImpl.mm:6762-6782`, is what makes command-key
equivalents and beep suppression work. The responder-chain sink,
`WebViewImpl.mm:3141-3150` plus its helper class, services the one synchronous
message the content process sends back when WebCore's editor does not recognise
a selector.

Gating the existing files is not cheaper and is much worse. In the text path
alone there are about ninety distinct call sites on post-10.4 interfaces, worth
roughly 1,050 lines of gating, of which about 520 is straight deletion. But a
path cannot be gated in isolation inside an 8,579-line translation unit. To make
that file compile on Tiger at all means gating four to five thousand lines
covering drag and drop, full screen, the Touch Bar, immediate actions, the text
finder and layer hosting, none of which exists on Tiger and none of which we
want.

So: **write it.** The layering upstream already did makes this work. The proxy's
text-input interface is plain C++ over strings, ranges and points; the command
vocabulary is selector names as strings; the editor state is value types. A fresh
view drives exactly the same IPC the modern client does, with nothing stubbed.

## B. Hooks for native widgets

### The precedent, and why it does not fit

The existing UI-side controls all share one shape, which is worth naming: an
**ephemeral modal chooser**. One async show-message on the proxy carrying a
plain-data description and a rect in window coordinates, a `PageClient` factory
that returns a proxy object owning the AppKit control, and one async message
back. The popup menu is the fullest example:
`WebProcess/WebCoreSupport/WebPopupMenu.cpp:97` flattens the client into a
`Vector<WebPopupItem>` and sends `ShowPopupMenuFromFrame` without blocking; the
UI process builds a real popup cell in `UIProcess/mac/WebPopupMenuProxyMac.mm:106`
and blocks itself in a nested AppKit run loop. The colour picker, the data-list
dropdown, the date-time picker and the context menu are the same three parts.

None of this models "here are forty widgets, keep them positioned every frame".
These are one-shot and user-triggered.

### What upstream already built, which is the answer

WebKit refactored native-control drawing out of the render theme years ago into
a serializable value type, and then pointed it over IPC at the GPU process. All
of the following already exists:

- `WebCore/platform/graphics/controls/`, about 2,163 lines with no Objective-C,
  holding `ControlPart`, `ControlStyle` and a factory.
- `ControlStyle` is a flat struct of a state option-set, font size, zoom, accent
  colour, text colour and border width. That is precisely the "state and label"
  payload we would otherwise have designed.
- `RenderTheme::createControlPart` is a pure switch on the appearance keyword,
  and `RenderTheme::paint` funnels everything through
  `GraphicsContext::drawControlPart`.
- `ControlPart::draw` returns silently when the factory yields nothing, so a
  content process with the empty factory is already a safe no-op renderer.
- **`drawControlPart` is already overridden to go over IPC**, at
  `WebProcess/GPU/graphics/RemoteGraphicsContextProxy.cpp:689-693`, against a
  message declared in `GPUProcess/graphics/RemoteGraphicsContext.messages.in:124`.
- **The serializers are already written**, 212 lines at
  `Shared/WebCoreArgumentCoders.serialization.in:2249-2460`.
- The AppKit side is `WebCore/platform/graphics/mac/controls/`, about 4,484
  lines of cell drawing, and `ControlPart.h:36` already has a factory override
  hook.

So WebKit's answer to "draw a native control in the process that has the toolkit,
driven by one that does not" is shipped, serialized and tested. It is merely
aimed at the GPU process instead of the UI process.

### Where it rides

`Shared/UpdateInfo.h` is 69 lines and its serialization file is 37, of which the
body is ten lines, one per field. Adding a vector of control geometry costs two
lines plus the struct. It arrives frame-synchronised with the bitmap it
annotates, which is the property that made a per-update channel attractive in the
first place, and the UI process already has a CoreGraphics context in hand at
`DrawingAreaProxyCoordinatedGraphics::paint`.

The remote layer tree transaction is the wrong model to copy. Its dirty-mask diff
over 45 fields, roughly 3,900 lines in total, pays for itself at thousands of
independently animating long-lived layers. Form controls number in the tens and
are cheap to re-send whole.

A new message file is cheap mechanically, about fifteen lines of boilerplate plus
one handler per message, but it buys an out-of-band channel that can arrive out
of step with the bitmap it describes. Only worth it if widgets outlive frames.

### Estimate

| Piece | Est. |
|---|---|
| `BackingStoreCG`, owed regardless | 120 |
| Geometry struct plus two lines of serialization | 25 |
| Content side: append instead of draw, mirroring the five-line GPU override | 40 |
| UI side: loop the list after the blit with the Mac factory installed | 60 |
| Build split: keep the Mac control sources out of the 64-bit link | 30 |
| Scrollbars, hand-drawn | 250 |
| **Total** | **about 525, of which 250 is scrollbars** |

### Two corrections to the brief

**Real `NSControl` view objects would be worse, not just dearer.** They need
stable identities across frames, create and destroy messages, a subview z-order
and clipping story that cannot honour an ancestor's overflow clipping or
stacking, hit-test arbitration against the content process's own event handling,
and transforms and opacity that Tiger AppKit cannot apply to a view. Realistically
1,500 to 2,500 lines and a permanent correctness tax. The control-part route gets
**actual Aqua** anyway, because WebKit has never used live controls for form
widgets; it has always drawn cells, which is the same code path Safari uses and
the same pixels.

**Scrollbars do not get to ride along.** They are not control parts, so
`createControlPart` has no case for them. `ScrollbarThemeMac.mm` is 700 lines of
scroller-imp SPI, which is a 10.7 overlay-scrollbar interface with no Tiger
ancestor. The non-Cocoa ports hand-draw on the composite theme, and Tiger's
non-overlay scrollbar is geometrically simple. Budget about 250 lines.

An all-in-WebCore theme in the Adwaita style would need no IPC at all, but
Adwaita's 1,933 lines draw flat rectangles. Aqua is pinstripes, lozenge bezels,
focus glow and a pulsing default button, so that number understates it badly, and
the result would be a lookalike that drifts and never tracks the user's blue or
graphite setting.

## C. Accessibility

### Remote accessibility is architecturally closed on Tiger

It is not a tree transfer. The whole mechanism rests on one AppKit private class
introduced in 10.7, `NSAccessibilityRemoteUIElement`. The content process marks
itself a remote-UI server, mints an opaque token for a mock element
(`WebProcess/WebPage/mac/WebPageMac.mm:201-205`) and ships it; the UI process
rehydrates it (`UIProcess/mac/WebViewImpl.mm:4225-4232`) and returns it as the
web view's single accessibility child. Every subsequent attribute request goes
straight into the content process as a synchronous Mach message, entirely behind
WebKit's back. **WebKit never serializes an accessibility node.** The token is a
capability handle.

Three further blockers, each fatal alone. The content process calls a private
`NSApplication` accessibility initialiser, and ours has no Objective-C runtime at
all; accessibility on the Mac *is* Objective-C message dispatch. The presenter
identifier call is 10.7. The trust and mode negotiation has no Tiger counterpart.

Worth recording: **no port in WebKit serializes the accessibility tree across
processes.** The GTK and WPE ports use the same handle-then-direct-RPC shape over
D-Bus, sending a plug identifier instead of a token. Neither helps.

### What the content process can still produce

The core tree is genuinely platform-neutral, about 44,658 lines under
`WebCore/accessibility/`, with the platform wrappers layered on top. The proof is
`AXCoreObject.h:85`, which already declares an empty wrapper base for ports with
no accessibility platform, next to the Cocoa and ATSPI branches. The Windows and
PlayStation wrappers exist because someone has walked this path. A non-Objective-C
content process falls into that third branch.

There is no master accessibility feature flag; it was removed upstream and the
neutral core is compiled unconditionally. The isolated-tree flag defaults off
everywhere except Cocoa, where `WTF/wtf/PlatformEnableCocoa.h:47-49` raises it.
Define it to zero in the port header. The clean exclusion is at file level: drop
the Mac, Cocoa, iOS and isolated-tree directories from the content-process
sources and stub the three registration messages.

### Recommendation

**Version one: declare the absence, do not merely have it.** The correction to
the brief is that native controls being accessible by themselves is true and
free, but the web view must actively report a group role with no children. If it
falls back to the default view behaviour it reports its subviews and an assistive
client walks into a broken hierarchy. That is one attribute override, about
fifteen lines. Note that Tiger shipped VoiceOver, so this is a real regression
for a user, though it is the same one every non-Safari browser on Tiger had.

**Version two, if anyone asks later: serialize the tree, about 2,000 to 3,200
lines** for a read-only version without text markers or actions. The reuse is
mostly a data shape rather than code. The isolated tree's node model at
`AXIsolatedTree.h:335-420` is already flat, identifier-keyed, a property bag,
and carries an incremental-change record; it was designed to cross a thread and
crossing a process is a smaller step than anything else on offer. Copy the shape,
trim the variant to what can be encoded, and do not enable the isolated tree
itself. The ATSPI object, 1,514 lines, is the best worked example of exposing
WebCore's accessibility through a non-Objective-C interface, and its role and
state mapping tables translate almost directly. Keep this on the shelf.
