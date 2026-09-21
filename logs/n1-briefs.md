# N1 dispatch briefs — "the web process links"

Milestone N1 of `logs/split-process-plan.md` §6.1, decomposed into independently
dispatchable units. Written against `4a903f0`, read-only.

**Gate for the milestone:** a 64-bit web-process binary that links, launches on
the box, records a display list for a trivial page, and writes it to a file that
a 32-bit replay can read back. No pixels yet — that is N2.

---

## 0. Read this first: two things that change the shape

### 0.1 N1 does not depend on N0

**Every brief below survives unchanged if `jsc64` reports that the JIT is
unusable.** N1 is about making WebCore compile, link and record for x86_64, and
about the wire format. None of it touches JSC's backend.

If N0 fails, exactly one thing changes, and it is a CMake variable:

```
ENABLE_JIT=OFF  ENABLE_DFG_JIT=OFF  ENABLE_FTL_JIT=OFF  ENABLE_C_LOOP=ON
```

in the WEB configuration only. `ENABLE_WEBASSEMBLY` is already off in every
configuration and must stay that way regardless, because it appears in
serialization conditions (see §0.2). So N1 proceeds at full speed either way, and
the decision an N0 failure forces is an architecture-level one — is a 64-bit
C-loop worth the split at all — not a task-level one.

**Dispatch the first wave without waiting for N0.** The only cost of being wrong
about N0 is that a browser we could have built anyway took the long route to get
there.

### 0.2 Feature flags are not per-process, and 55 of them are not CMake's

From `logs/wkcmake-journal.md`: `OptionsTigerProcesses.cmake` already sets
**every feature flag identically for all four configurations**, because 219
distinct `ENABLE_`/`USE_` names appear in the serialization inputs and any
disagreement diverges the wire. The per-process sections set only JIT switches
and target selection.

Three consequences that constrain several briefs below:

- **Video cannot be a per-process choice.** Turning `ENABLE_VIDEO` on for the
  ffmpeg backend turns it on for all four. Same for `ENABLE_GPU_PROCESS`, which
  is the most-used condition of all and flips for everyone when the render
  process lands.
- **55 of the 219 names are compile-time macros in `PlatformEnable*.h`**, not
  CMake options. They cannot be set from the build system, so making them agree
  is the port header's job. That is brief **N1-B**.
- **`USE_CG` and `USE_CORE_TEXT` are among the 55.** The two flags that most need
  to diverge between the render and web processes are not CMake variables under
  the Cocoa port, which is why `OptionsTiger64.cmake` is required rather than
  optional.

### 0.3 An open question for the lead

`OptionsTigerProcesses.cmake` defines four `TIGER_PROCESS` values: UI, RENDER,
WEB, NETWORK. The decided architecture (`NOTES.md`, "ARCHITECTURE DECIDED") is
**three** processes, with UI and RENDER merged, because compositing needs the
window.

Either RENDER is a staging artifact — a separate build config to bring replay up
in isolation before merging it into UI, which is a good idea — or the two
decisions have drifted apart. **Worth confirming with wkcmake before N2**, since
it determines whether anyone writes a standalone render-process `main()`. It does
not block any N1 brief.

---

## 1. The briefs

Waves are dependency levels; everything within a wave runs in parallel.

### Wave 0 — the single blocker

---

#### **N1-A · `OptionsTiger64.cmake`** — wkcmake

The one thing standing between here and everything else. All four configurations
already configure, but the x86_64 pair does so under `PORT=Cocoa` and **will not
compile**, because Tiger has no 64-bit Foundation. They need a non-Cocoa options
file.

| | |
|---|---|
| **Files** | new `Source/cmake/OptionsTiger64.cmake`; edits to `Source/cmake/OptionsTigerProcesses.cmake`, `Source/WebCore/PlatformTiger64.cmake` (new) |
| **Precedent** | `Source/cmake/OptionsPlayStation.cmake` — the closest analogue in the tree: no glib, no X11, pkg-config disabled outright at `:266-271`, `USE_CURL ON` at `:271`, `USE_UNIX_DOMAIN_SOCKETS` forced at `:268`. For the WebCore half, `Source/WebCore/PlatformPlayStation.cmake:1-30` shows the include set |
| **Do** | `USE_CG OFF`, `USE_CORE_TEXT OFF`, `USE_CF OFF`, `USE_APPKIT OFF`, `USE_FOUNDATION OFF`, `USE_CURL ON`, `USE_UNIX_DOMAIN_SOCKETS ON`, `USE_HARFBUZZ ON`, `USE_FREETYPE ON`. Do **not** set `USE_CAIRO` — the web process records, it does not rasterize (§N1-G) |
| **Do not** | touch the shared feature-flag list. Every `ENABLE_*` stays exactly as `OptionsTigerProcesses.cmake` sets it for all four |
| **Verify** | `cmake` configures `build/tiger-web` with `PORT=Tiger64` and `tiger-check-ipc` still reports all four configurations in agreement |
| **Lines** | 250–350 |
| **Blocks** | N1-C, N1-F, N1-G, and transitively everything else |

---

### Wave 1 — parallel, unblocked by N1-A

---

#### **N1-B · `TIGER_WIRE_*` in the port header** — wkcmake, with objcrt

The 55 IPC-relevant names that are compile-time macros rather than CMake options,
including `USE_CG` and `USE_CORE_TEXT`. Per `logs/serializer-asymmetry.md` §1.3,
the fix is to separate "is this field on the wire" from "do I have this
framework".

| | |
|---|---|
| **Files** | `Source/WTF/wtf/PlatformEnable.h` or a new `PlatformEnableTiger.h`; the 69 `.serialization.in` files that carry a split-sensitive conditional, largest first: `Shared/WebCoreArgumentCoders.serialization.in` (37), `Shared/WebProcessCreationParameters.serialization.in` (15), `Shared/Cocoa/WebCoreArgumentCodersCocoa.serialization.in` (14), `Shared/WebPageCreationParameters.serialization.in` (12), `Shared/WebEvent.serialization.in` (10) |
| **Precedent** | none upstream — this is genuinely ours, which is why it goes in `toolchain/patches/` beside the cross-ABI patch rather than into the tree as a silent divergence |
| **Do** | define `TIGER_WIRE_COCOA`, `_CG`, `_CF`, `_APPKIT`, `_CORE_TEXT` as 1 on **both** sides; remap `#if PLATFORM(COCOA)` → `#if TIGER_WIRE_COCOA` etc. **in generator inputs only** |
| **Cheapest first** | turn off Apple Pay, WebXR, service controls and the model process. That removes `CoreIPCPassKit.serialization.in` (7), `PlatformXR.serialization.in` (4) and others from the check entirely, CMake-only, before a line of remapping |
| **Verify** | `tiger-check-ipc` passes with an empty known-divergent list, where today it reports `USE_CG`, `USE_CORE_TEXT` and three types as expected divergences |
| **Lines** | ~60 of header, plus a mechanical remap across 69 files |
| **Blocked by** | nothing — can start now |

---

#### **N1-C · WTF for x86_64: source list and link** — wkcmake

| | |
|---|---|
| **Files** | new `Source/WTF/wtf/PlatformTiger64.cmake` |
| **Precedent** | `Source/WTF/wtf/PlatformPlayStation.cmake` — **copy it almost verbatim**. It is 15 files: the four `generic/` (`MainThreadGeneric`, `MemoryFootprintGeneric`, `RunLoopGeneric`, `WorkQueueGeneric`), four `playstation/` (FileSystem, Language, OSAllocator, UniStdExtras — each needs a Tiger twin), five `posix/`, plus `text/unix/TextBreakIteratorInternalICUUnix.cpp`, `unix/LoggingUnix.cpp`, `unix/MemoryPressureHandlerUnix.cpp` |
| **Key fact** | `RunLoopGeneric.cpp` is pure WTF — `Condition`, `MonotonicTime`, `RedBlackTree`, no dispatch, no CF. **The web process needs no x86_64 libdispatch** (plan §4) |
| **But** | link `-ltigerdispatch` anyway for `os_log` / `os_unfair_lock` / `os_signpost`; a 64-bit `libtigerdispatch.a` exists (`make -C compat/dispatch ARCH=x86_64 install`). `dispatch_*` itself does not exist at 64 bits and cannot, since the main queue needs CFRunLoop |
| **Verify** | `libWTF.a` archives for x86_64; a one-file test links against it and runs on the box |
| **Lines** | 150–250 |
| **Blocked by** | N1-A |

---

#### **N1-D · Cross-ABI IPC patch: rebase + the fifth offender** — objcrt · *in flight*

| | |
|---|---|
| **Files** | `toolchain/patches/webkit-ipc-cross-abi.patch`, adding to `Source/WebKit/Platform/IPC/ArgumentCoders.h` |
| **Do** | the `std::span` static assertion from `logs/serializer-asymmetry.md` §4.3. `ArgumentCoder<std::span<T, Extent>>::encode` at `ArgumentCoders.h:60-70` calls `encodeSpan` for **any** `T`, and `wireAlignmentOf` fixes alignment but not size |
| **Current state** | audited: only two spans in the generator inputs have a composite element type — `WebCore::FloatSegment` (two floats) and `WebCore::PathDataLineColorThickness` (all 4-byte fields). **Both are safe today.** The assertion keeps them that way |
| **Verify** | both builds compile; a throwaway `std::span<struct{uint32_t;uint64_t;}>` fails to compile with the new message |
| **Lines** | ~15 |
| **Blocked by** | nothing |

---

#### **N1-E · Preprocessor probe replaces the hash check** — wkcmake · *accepted, in flight*

| | |
|---|---|
| **Files** | `Source/cmake/TigerCheckIPC.cmake`; new `tools/extract-wire-flags.py`, `tools/check-wire-flags.sh` |
| **Why** | `generate-serializers.py` emits conditionals verbatim into the generated C++ (`:629`, `:651`, `:753`, `:759`, `:776`, `:784`, `:838`, `:866`, `:878`, `:891`), so both sides produce a byte-identical `.cpp` and a source hash is blind to flag drift. Full reasoning in `logs/serializer-asymmetry.md` §0 |
| **Do** | keep the hash as the second check — because the generated sources *should* be identical, an inequality is itself a signal — and add the preprocessor probe as the primary |
| **Needs from wkcmake** | `SHARED_SERIALIZATION_INPUTS` as the intersection of the two source lists, so the probe has an exact scope |
| **Verify** | a throwaway tree with one flag flipped fails the check and names the flag |
| **Lines** | 150–250 |
| **Blocked by** | nothing |

---

### Wave 2 — needs N1-A and N1-C

---

#### **N1-F · WebCore compiles for x86_64 with no raster backend** — wkcmake

The bulk of the milestone, and the one with the long tail.

| | |
|---|---|
| **Files** | new `Source/WebCore/PlatformTiger64.cmake`; new `Source/WebCore/platform/tiger64/` for the port stubs |
| **Precedent** | `Source/WebCore/PlatformPlayStation.cmake` for the include set; `Source/WebCore/platform/playstation/` for the stub shape — it is **7 files, 480 LOC total**: `MIMETypeRegistryPlayStation.cpp`, `PlatformScreenPlayStation.cpp`, `ScrollbarThemePlayStation.{cpp,h}`, `ThemePlayStation.{cpp,h}`, `UserAgentPlayStation.cpp` |
| **Do** | `include(platform/Curl.cmake)`, `include(platform/ImageDecoders.cmake)`, `include(platform/OpenSSL.cmake)`, `include(platform/FreeType.cmake)`. **Do not** include `Cairo.cmake`, `TextureMapper.cmake` or `CoordinatedGraphics.cmake` |
| **Expect** | a long error tail. The 32-bit pass got to 49 failing units in 8 clusters (`logs/wc-build.log`); the 64-bit one will differ — no Foundation at all, rather than an old one |
| **Verify** | every WebCore translation unit compiles for x86_64. Host-only; nothing runs yet |
| **Lines** | 800–1,200 |
| **Blocked by** | N1-A |

---

#### **N1-G · `NullImageBufferBackend` as the web-process default** — wkcmake

**Mostly already done, and the plan's estimate was wrong.** §6.2 budgets ~150 LOC
for a "pixel-less shareable-bitmap backend". It exists:

| | |
|---|---|
| **Files** | `Source/WebCore/platform/graphics/NullImageBufferBackend.{h,cpp}` — **63 + 94 = 157 LOC, already in `Sources.txt` unconditionally** |
| **Already used by** | `Source/WebKit/GPUProcess/graphics/RemoteRenderingBackend.cpp` and `RemoteImageBufferSet.cpp` — i.e. by the remoting path we are adopting |
| **Do** | confirm it satisfies `ImageBufferBackend`'s contract for a recording-only process, and wire it as the default `ImageBuffer` backend when `USE_CG` and `USE_CAIRO` and `USE_SKIA` are all off. Also check `ImageBufferDisplayListBackend.{h,cpp}`, which may be the better fit |
| **Verify** | `ImageBuffer::create` returns a usable buffer in the web process and recording into it produces a display list |
| **Lines** | ~30, not 150 |
| **Blocked by** | N1-A |

---

#### **N1-H · `FontPlatformDataAttributes` de-CoreFoundation-ed** — ctcompat

The font handle that crosses the wire. Per `logs/render-process-survey.md` §5,
this is already the right abstraction and already per-port with a neutral common
core; the work is replacing CF-typed members with plain types on the recording
side and converting only in the replay process.

| | |
|---|---|
| **Files** | `Source/WebKit/Shared/WebCoreFont.serialization.in` (5 split-sensitive conditionals), `Source/WebCore/platform/graphics/FontPlatformData.h` and its Cocoa arm |
| **Precedent** | the existing per-port split — the neutral half, `FontMetadata`, already carries point size, orientation, width variant, text rendering mode, synthetic bold, synthetic oblique and metrics overrides |
| **The irreducible handle** | file path, PostScript name, point size, synthetic bold and oblique, orientation, width variant, text rendering mode, variation axes, feature settings, metrics overrides, plus a custom-font arm carrying bytes. One-for-one with what upstream ships |
| **Rule** | **name faces by PostScript name, never by index.** Tiger's system fonts are suitcases and collections, and index ordering is not stable across a name lookup |
| **Verify** | a handle encoded by the 64-bit side decodes on the 32-bit side to a `CTFontRef` for the same face; compare `CTFontCopyPostScriptName` |
| **Lines** | 300–450 |
| **Blocked by** | N1-B (it is one of the files being remapped) |

---

### Wave 3 — needs N1-F and N1-H

---

#### **N1-I · Font manifest generator, 32-bit side** — ctcompat · *in flight (`build/fontmanifest` exists)*

| | |
|---|---|
| **Files** | `compat/fontmanifest.m` or similar; output consumed by N1-J |
| **Do** | enumerate installed faces with real CoreText and ATS; emit per face the PostScript name, family, traits, covered character set and file path |
| **Verify** | the manifest names every face in `/System/Library/Fonts` and `/Library/Fonts` on the box, and the covered-character sets match `CTFontCopyCharacterSet` |
| **Lines** | 300–500 |
| **Blocked by** | nothing — 32-bit only, can run ahead |

---

#### **N1-J · Font cache and cascade over the manifest, 64-bit side** — ctcompat

**The largest single unit in N1, and the second-highest residual risk in the
project** after serializer asymmetry.

| | |
|---|---|
| **Files** | new `Source/WebCore/platform/graphics/tiger64/FontCacheTiger64.cpp`, `FontCustomPlatformDataTiger64.cpp` |
| **Why it is hard** | WebKit's CoreText complex-text controller exists partly to ask CoreText *which font it chose* per run. This architecture forbids that: every draw must name a concrete resolved face, because the replay side rasterizes exactly what it is told. So fallback moves entirely here, over file-scanned metadata |
| **Precedent** | `Source/WebCore/platform/graphics/freetype/FontCacheFreeType.cpp` for the shape, **but** it is written against `FcPattern`/`FcFontSet` throughout. We have fontconfig built for x86_64, so using it is an option; resolving from the manifest directly is the other |
| **Verify** | a page mixing Latin, CJK and Arabic resolves every run to a concrete face, and the 32-bit side rasterizes every glyph without a `.notdef` |
| **Watch for** | this is where missing-glyph bugs will come from |
| **Lines** | 800–1,500 |
| **Blocked by** | N1-F, N1-H, N1-I |

---

#### **N1-K · HarfBuzz shaping for a CoreGraphics-shaped tree** — ctcompat

| | |
|---|---|
| **Files** | `Source/WebCore/platform/graphics/harfbuzz/` wiring; a `ComplexTextController` path selected for the web process |
| **Risk status** | **the top-ranked font risk is retired.** `logs/hb-vs-ct.md` measured HarfBuzz layout against Tiger CoreText rasterization at **0.03 pt on run width, 0.008 pt per glyph**, for every font tested at 12, 16 and 24 pt, including the AAT and AAT-Arabic cases, with both sides handed identical bytes |
| **Do** | disable system-font tracking, which CoreText applies as size-dependent letter spacing and the shaper knows nothing about |
| **Verify** | re-run `spike/hbvsct.c` against the shaping path as wired, not just the harness, and confirm the same tolerance |
| **Lines** | 400–700 |
| **Blocked by** | N1-F, N1-J |

---

#### **N1-L · Display-list recording smoke test** — wkcmake · *the N1 exit gate*

| | |
|---|---|
| **Files** | a throwaway `spike/dlrecord64.cpp` and `spike/dlreplay32.cpp` |
| **Do** | in the 64-bit process, lay out a trivial page and record to a `DisplayList`; serialize it; in a 32-bit process, decode and replay it into a `CGBitmapContext` and write a PNG |
| **Why this gate** | it exercises, in one test, everything N1 built: the port files, the null backend, the font handle, the wire encodings, the cross-ABI patch and the alignment fix. Nothing else proves they work together |
| **Verify** | the PNG shows the text. Run the recorder on the box, not just on the host |
| **Lines** | 200–400 |
| **Blocked by** | all of the above |

---

### Independent throughout

---

#### **N1-M · Media continues on its own track** — media64

Not gated on N1 and not gating it. `MediaPlayerPrivateFFmpeg` is N6 work, but the
decode numbers are already measured and ffmpeg is already built.

**One coupling to respect:** `ENABLE_VIDEO` and `ENABLE_MEDIA_SOURCE` appear in
serialization conditions, so turning them on is a **four-configuration** change,
not a web-process one. Coordinate the flip with wkcmake and expect
`tiger-check-ipc` to be the thing that catches it if anyone gets it wrong.

---

#### **N1-N · Dependencies — nothing to do** — deps

`toolchain/sysroot-x86_64/usr/lib` already carries **harfbuzz, curl, cairo,
pixman**, freetype, fontconfig, expat, libpng16, libjpeg, libwebp, libxml2,
libxslt, sqlite3, LibreSSL, ICU, brotli, nghttp2, libavif, dav1d and the ffmpeg
set. The plan's "N1: build pixman, cairo, harfbuzz, curl" is **already done**.

One item worth a follow-up, cheap and real: **libjpeg-turbo was built
`-DWITH_SIMD=0`** because nasm was not installed at the time
(`deps/build-c-deps.sh:151`). nasm is installed now, and JPEG is the most common
decode on the target sites, with the SIMD path worth roughly 2–4×.

---

## 2. Dispatch order

```
                    N1-A  OptionsTiger64.cmake          (wkcmake)   ← send first, alone
                      │
      ┌───────────────┼───────────────┬──────────────┐
      ▼               ▼               ▼              ▼
   N1-C WTF        N1-F WebCore    N1-G Null      (N1-B, N1-D, N1-E, N1-I
   (wkcmake)       (wkcmake)       (wkcmake)       need nothing — send with A)
      │               │               │
      └───────┬───────┴───────────────┘
              ▼
           N1-H FontPlatformData  (ctcompat)
              │
              ▼
           N1-J FontCache/cascade (ctcompat)   ← the long pole
              │
              ▼
           N1-K HarfBuzz wiring   (ctcompat)
              │
              ▼
           N1-L recording gate    (wkcmake)
```

**The first wave to send the moment N0 reports — or now, per §0.1:**
N1-A, N1-B, N1-D, N1-E, N1-I. Five briefs, four tracks, no interdependencies.

**The long pole is N1-J**, at 800–1,500 lines and the highest uncertainty. It is
blocked by three things, so anything that shortens N1-F or N1-H shortens the
milestone. If ctcompat has spare capacity early, the manifest generator (N1-I)
can start immediately and is pure 32-bit work.

## 3. Totals

| Brief | Track | Lines | Depends on |
|---|---|---|---|
| N1-A `OptionsTiger64.cmake` | wkcmake | 250–350 | — |
| N1-B `TIGER_WIRE_*` + remap | wkcmake, objcrt | ~60 + remap | — |
| N1-C WTF x86_64 | wkcmake | 150–250 | A |
| N1-D cross-ABI span assert | objcrt | ~15 | — |
| N1-E preprocessor probe | wkcmake | 150–250 | — |
| N1-F WebCore x86_64 | wkcmake | 800–1,200 | A |
| N1-G null backend wiring | wkcmake | ~30 | A |
| N1-H font handle de-CF | ctcompat | 300–450 | B |
| N1-I font manifest generator | ctcompat | 300–500 | — |
| N1-J font cache + cascade | ctcompat | 800–1,500 | F, H, I |
| N1-K HarfBuzz wiring | ctcompat | 400–700 | F, J |
| N1-L recording gate | wkcmake | 200–400 | all |
| **Total** | | **3,455–5,705** | |

Against the plan's N1 estimate of 2,500–3,800: **higher**, because the plan
folded the font manifest and the cascade into a single line item and because
N1-B's remap was not costed at all. Two things push the other way — the null
backend is already written, and the dependencies are already built — but not
enough to close the gap. **Revise N1 to 14–20 days**, from 12–18.

No track is assigned two briefs in the same wave, except wkcmake in wave 2, which
is a real serialization point and the argument for pulling N1-G forward as a
warm-up since it is ~30 lines of verification.
