# Display-list remoting to a 32-bit CoreGraphics render process

Round 3.5 addendum, read-only. Agent wkcmake, 2026-09-20. Nothing under
`WebKit/` was modified. WebKit main @ d2f52605.

The proposal: a 64-bit WebProcess doing JavaScript, DOM, layout, image
decoding, text shaping and display-list **recording**, and a 32-bit process
**replaying** those display lists against Tiger's real CoreGraphics and
CoreText through our verified compatibility layer. That is WebKit's GPU-process
shape with the roles inverted, the process that has the platform graphics being
the small one.

**Verdict up front: the architecture works, there is a shipping in-tree
precedent for exactly this, and the hard part is not the remoting. It is
fonts.** Recommended topology differs from the brief in one respect: do not
build a third process. Put the replay in the 32-bit UI process.

---

## 1. Is the 2D remoting usable with the platform flags split across processes?

### The remoting layer itself is portable, and this is not speculation

`WinCairo remotes DOM rendering to its GPU process by default.` The settling
quote is `Shared/WebPreferencesDefaultValues.cpp:324-335`, where
`defaultUseGPUProcessForDOMRenderingEnabled()` returns true whenever the
Windows coordinated layer flag is on. The GPU process is enabled by default for
Windows (`OptionsWin.cmake:105`), GTK, WPE and Cocoa, and off for PlayStation
(`OptionsPlayStation.cmake:218`).

So there is an in-tree, shipping, non-Cocoa port that remotes 2D image-buffer
drawing to another process, using the same generic backend Cocoa uses with the
Cocoa flag simply off. The files build unconditionally for every CMake port from
`Sources.txt:32-47`; no port gates them.

Supporting evidence for portability:

- `RemoteGraphicsContext.messages.in` has roughly 110 messages and exactly two
  are platform-gated, the two pattern-apply calls under the CoreGraphics flag,
  plus a video block that should be off anyway.
- `RemoteResourceCache.cpp` has no platform includes at all.
- The stream transport, a shared-memory ring buffer with two semaphores, has no
  Cocoa blocks in the buffer or the server side and exactly one in the client.
- Gradients, patterns, filters, shadows, paths and source images are all plain
  value types carried inline in the stream. A source image is just a resource
  identifier.

### The Windows coordinated-layer directories are a separate thing

Worth stating plainly, because the brief asked. Those directories are
**compositing and WebGL only**, about 3,460 lines, whose entire cross-process
interface is one message carrying a layer-delta structure. The 2D remoting
Windows uses is the generic backend, not that code. Both are relevant to us, for
different reasons; see section 3.

### There is no Objective-C to strip

The single most reassuring measurement in this survey. Across the CoreGraphics
platform directory, 50 files, and the CoreText one, 10 files, there are four
Objective-C++ files and **exactly one line of actual Objective-C** between them,
a uniform-type-identifier lookup. The extensions are Apple convention, not
content. The 64-bit process's problem is CoreFoundation and CoreGraphics, not the
runtime, and the 32-bit replay side has both.

Equally worth recording: the old Apple Windows port's CoreGraphics-without-Cocoa
configuration is **completely gone**. Searches for the obvious guard forms return
nothing and the Windows graphics directory is entirely Cairo, Skia, GDI and
Uniscribe. We get no free ride from it.

### The real obstacle: WebKit's serialization is symmetric by construction

The argument coders are compiled into both processes from the same conditionals,
on the assumption that both are the same port. This design deliberately breaks
that. Three of the hottest types on the 2D wire change encoding:

| Type | With CoreGraphics on | With it off |
|---|---|---|
| `ColorSpace` | a retained `CGColorSpaceRef`, encoded through a Cocoa-only Objective-C++ coder as a four-way variant | a single byte |
| `ShareableBitmapConfiguration` | additionally carries bitmap info and a gain map | neither |
| `FontPlatformDataAttributes` | a whitelisted CoreText descriptor attribute dictionary | only size and synthetic flags, with no font identity at all |

`ColorSpace` is embedded in image-buffer creation, colour-space transforms and
every shareable-bitmap configuration, so this must be settled before anything
renders. The fix is contained: force the neutral enumerated encoding on both
sides and have the render process map the name to a real colour space locally,
which its existing table already does.

### The other structural gap: no image-buffer backend for "no 2D library"

`ImageBufferShareableBitmapBackend` is the portable backend, but portable there
means CoreGraphics **or** Cairo **or** Skia, with no fallback arm. A process with
none of the three does not compile it. The same three-way gap appears in the
lockdown-font-parser predicate.

This is less bad than it sounds, because upstream already proves a process can
hold image buffers it cannot draw into. The Cocoa remote-surface backend and the
remote display-list and PDF backends each hold a `NullGraphicsContext` and return
it from `context()`. `NullGraphicsContext` is a real, used class, not a stub:
the frame view runs its entire paint tree walk against one to invalidate control
tints. `NullImageBufferBackend` exists as the out-of-memory fallback and the GPU
process already instantiates it.

So the missing piece is one new pixel-less shareable-bitmap backend modelled on
the existing remote-surface one, which is 140 lines. Not a redesign.

---

## 2. Can the two processes be different architectures?

Yes, under the same conditions the earlier survey established, and with one
addition.

From `logs/webkit2-split-survey.md`: the wire is mostly fixed-width by design;
the leak is the generic arithmetic coder putting native size and alignment on the
wire; the pointer-width types disagree and the fixed-width ones agree. **Both
sides must be built with the same clang**, because the agreement rests on clang
reporting eight-byte alignment for a 64-bit integer on i386 Darwin where Tiger's
own compiler reports four. Three known offenders: two message fields and the unix
transport's framing header.

The addition, and it is good news. The obvious hazard in the display list is the
glyph advance type, which is a CoreGraphics size on CoreGraphics ports, two
doubles on 64-bit and two floats on 32-bit, and a float size everywhere else.
**Upstream already normalises this at the send site**, converting to a float size
in the proxy before the message goes out, and the message declares the float type.
The glyph type itself is an unsigned short on both sides. No hazard.

Display-list items are otherwise floats, enumerations, rectangles, transforms and
colours. The pointer-width types do not appear in them.

---

## 3. Topology: do not build a third process

**Recommendation: put the CoreGraphics and CoreText replay in the 32-bit UI
process, and let the UI process own the CoreAnimation layer tree.** Two
processes per web view, not three.

Four reasons, the first close to decisive.

**Compositing needs a window, and Tiger cannot lend one across a process
boundary.** The Windows coordinated-layer design escapes this because its
layer-tree host accepts a raw native window handle and Win32 and X11 let one
process create a GL context on another process's window. Tiger has no such
facility: no layer host, no layer context, and no IOSurface, which arrived in
10.6. A separate compositor process would have to copy every composited frame
back to the UI process for blitting. That path exists upstream for the offscreen
case and it costs a full-window copy per frame, which is the whole frame budget
on this hardware.

**The no-copy tile path only exists if the replay is in the UI process.** Merged,
a painted tile goes from the 64-bit process into shared memory, the UI process
maps it, wraps it in a data provider and a bitmap context, and assigns it to the
layer. One mapping, zero copies, bytes landing directly in the layer. Split, you
add a hop that buys nothing, because the second hop cannot be a surface handle
here. Apple's own design only escapes this because surface send rights are cheap
to pass to a third process.

**Process cost.** Each extra process is a full load of CoreGraphics, CoreText and
AppKit, tens of megabytes of non-shared dirty pages, on a machine with one to two
gigabytes. Upstream gets a discount we do not, by reusing one work queue inside an
already-existing GPU process.

**Crash isolation is the one argument the other way, and it is weaker than it
looks.** What actually crashes is JavaScript, layout and decoding, and all three
stay in the 64-bit process, which is isolated. Replaying a display list our own
code produced is comparatively boring. And isolating it buys nothing if the
compositor dies with it, since we cannot re-host a dead compositor's layers
without the machinery we do not have. Upstream itself treats repeated GPU-process
crashes as near-fatal and kills the content processes.

Migration runs one way only. Merged to split requires re-solving window hosting,
which is unsolvable here. Split to merged is deleting a hop. **Start merged.**

### Who owns the layer tree

The UI process. Follow the Cocoa model rather than the Windows one: the 64-bit
process builds a shadow tree that owns no platform object, serializes a delta,
and the UI process instantiates real layers.

We get most of this for free. The Windows coordinated graphics layer class, 756
lines, is already a graphics layer subclass owning no platform layer and emitting
a change-flag delta, twenty-nine flags. Its update structure, backing store, tile
grid, layer factory and drawing area are about 1,600 lines of process-side code we
would not write. **Retarget the update structure at the UI process and rewrite its
scene applier, 305 lines, to build real layers instead of texture-mapper ones.**
The layer-tree host collapses into an ordinary message receiver once there is no
process boundary to the window.

One useful detail: that design supports both paint-here-composite-there and
paint-there-composite-there, and the choice is a one-line policy rather than an
architectural commitment.

---

## 4. What must stay local in the 64-bit process

The master switch is `WebProcess::shouldUseRemoteRenderingFor(RenderingPurpose)`.
With DOM, canvas and layer-backing routed remote, the 64-bit process creates
essentially no local raster buffers; every remote buffer becomes a pure handle
whose context is null. The design comment upstream is explicit: whoever needs to
touch the pixels allocates them.

Two residual local paths must be audited, because both are hardcoded to allocate
locally: the shareable-local-snapshot purpose and the unspecified purpose.

**Text measurement and hit testing need no graphics context.** The measurement
entry points take none; only the drawing ones do. Upstream states the split
explicitly on the GPU side, where the shared font type is documented as display
only, carrying no glyph tables and unusable for shaping or measuring. That is
exactly the division this design wants.

**Pixel readback is the one unavoidable synchronous stall.** Canvas readback is a
synchronous message, but the payload travels through a cached shared-memory region
sized on demand and reused, torn down on a five-second idle timer. The round trip
is a rendezvous, not a data transfer. Writes back are asynchronous and batched
below a 3,600-pixel area.

**Filters follow the buffer.** There is no separate filter remoting; effects run
wherever the pixels are, so with DOM rendering remote they run on the 32-bit side.
Good news either way: the software applier family is plain scalar C++ with no
platform graphics dependency, so they could come back to the 64-bit process later.
Only the Core Image variants are CoreGraphics-bound, and those are
accelerated-only.

**Decode locally in the 64-bit process.** The in-tree scalable decoder already
sits ahead of the CoreGraphics one in the fallback chain, so we get it by simply
not compiling the latter. Decoding is the most address-space-hungry thing the
engine does, which is the whole reason we want 64 bits, and it produces a plain
premultiplied buffer with no CoreGraphics dependency that we can hand over as
shared memory. Only the AVFoundation-backed formats are remoted upstream, and
that is a sandboxing artefact.

**Path geometry is an open item.** The CoreGraphics path implementation is backed
by a platform path object. The 64-bit process needs the scalar implementation, or
hit testing and bounds break.

---

## 5. Fonts: the hard part

### The text item is already right

**Confirmed: the draw-glyphs item carries glyph identifiers and per-glyph
advances plus one anchor point.** Shaping has already happened in the recording
process; the replayer only rasterizes at accumulated pen positions. The
decomposed-glyph indirection has been removed from the tree entirely. The
deconstructing draw mode is CoreText-only and needs a real context in the
recorder, so we use the normal mode, which is what every non-CoreGraphics port
already does.

### The font handle already exists, and we would be removing CoreFoundation from it

`FontPlatformDataAttributes` is precisely the abstraction the brief asked about,
and it is already per-port with a neutral common core. The neutral half,
`FontMetadata`, carries point size, orientation, width variant, text rendering
mode, synthetic bold, synthetic oblique and metrics overrides. The naming half on
Cocoa is the font file's reference URL, the PostScript name, descriptor options,
and an attribute set that already includes variation axes as tag-and-value pairs.

What goes on the wire today on Cocoa is **not** a font blob and not an opaque
handle. It is a CoreText descriptor attribute dictionary, whitelisted key by key
into a typed struct of about twenty fields. Web fonts take a different arm and
ship the actual bytes, which is portable as is.

So the irreducible handle is: file path, PostScript name as the face selector,
point size, synthetic bold and oblique, orientation, width variant, text
rendering mode, variation axes, feature settings and metrics overrides, plus a
custom-font arm carrying bytes. **That is one-for-one with what upstream ships.**
The work is replacing the CoreFoundation-typed members with plain types on the
recording side and converting to CoreFoundation only in the replay process. That
is mechanical, not architectural.

Name the face by PostScript name, never by index. Tiger's system fonts are
suitcases and collections, and the index ordering a font library assigns need not
match what a name lookup resolves to.

### Glyph identifiers do agree, conditionally

A glyph identifier indexes the font file's outline table. It is a property of the
file, not of the rasterizer. If both sides open the same file and face, the
numbers match, and the advance table is read by index in both. The GTK, WPE and
Skia ports all shape with one library and rasterize with another against one file
and rely on exactly this.

Five things break it, in rough order of how likely they are to bite:

1. **AAT shaping divergence, and this is the top risk.** Tiger's system faces are
   AAT-first, carrying the older metamorphosis tables and no OpenType substitution
   table. HarfBuzz implements those tables but not bit-identically to CoreText,
   and Tiger-era CoreText applies them unconditionally. There is no upstream
   precedent for HarfBuzz-shaping AAT faces for CoreText rasterization. Ligatures
   and contextual forms in Lucida Grande and Geneva are where this shows first.
   Mitigation: prefer or bundle OpenType faces.
2. **Fallback must move entirely to the recording process.** The CoreText complex
   text controller exists partly to interrogate which font CoreText chose per run.
   This design forbids that; every draw must name a concrete resolved face. That
   means a real font cache and cascade implementation over file-scanned metadata,
   and it is the source of missing-glyph bugs.
3. **Derivative faces.** Vertical, half-width, small-caps and synthetic-oblique
   variants can have different glyph repertoires from the base file. The handle
   must name the derivative, and the shaper must be told to apply the matching
   features, or identifiers silently mean different things on the two sides.
4. **System-font tracking**, which CoreText applies as size-dependent letter
   spacing and the shaper knows nothing about. Disable it or replicate the table.
5. Hinting, which affects advances at small sizes. Unhinted advances agree.

### Metrics: do not compute them in the 64-bit process

The CoreText metrics initialiser is a pile of CoreText-specific heuristics: an
ascent override from the typographic table under conditions, a fifteen percent
ascent bump for specific faces, a rounding ladder, and an x-height derived from
the measured bounding box of the actual glyph rather than from a table.
Reimplementing that elsewhere is a bug farm, and any one-pixel divergence shifts
baselines so that layout and paint disagree.

**Have the 32-bit process compute metrics and per-glyph advances with real
CoreText and return them as plain floats, cached per font handle.** Then layout
and rendering agree by construction. The metrics-overrides field already in the
handle is the existing hook. Synthetic bold offset is pure arithmetic on point
size and the synthetic flag, both of which are in the handle, so it agrees for
free.

This is the highest-risk unknown in the plan and the one with no analogue to copy.
The shared font type upstream solves the opposite problem, display without
metrics. Decide early between the metrics service and parsing tables directly, and
cache aggressively per font and per glyph; the existing glyph-geometry cache is
the natural point.

---

## 6. Effort and comparison

### This branch

| Piece | Est. |
|---|---|
| Render-side assembly: PlayStation-shaped port plus CoreGraphics and CoreText WebCore plus our compat layer | 800-1,200 |
| Neutral colour-space and bitmap-configuration encoding on both sides | 150-250 |
| Pixel-less shareable-bitmap backend for the 64-bit side | 150 |
| Font handle de-CoreFoundation-ed, both directions | 300-450 |
| Metrics service and cache | 300-500 |
| Font cache and cascade over scanned metadata in the 64-bit process | 800-1,500 |
| Shaping: HarfBuzz wiring, a new path for a CoreGraphics-shaped tree | 400-700 |
| Layer delta retargeted at the UI process, scene applier rewritten for real layers | 400-600 |
| Backing store blit and tile plumbing | 200-300 |
| Port glue for the 64-bit process, from the earlier survey | 1,400-1,600 |
| **Total, excluding the UI process itself** | **about 4,900 to 7,250** |

The UI process from the earlier survey, 3,000 to 4,200, is additive and largely
independent.

### Against the alternatives

| | (a) Leopard CoreGraphics in 64-bit | (b) Cairo in 64-bit | (c) This branch |
|---|---|---|---|
| Rendering fidelity | native, if the libraries load and behave | a lookalike; no Aqua controls, different text | **native, by construction** |
| Legal and sourcing | requires private loading of another release's libraries | clean | clean |
| Risk concentration | does the whole stack load and run on Tiger's 64-bit libSystem | shaping and theme fidelity | fonts, and only fonts |
| Fonts | CoreText in-process, no handle problem | FreeType and HarfBuzz throughout, self-consistent | **the hard part** |
| Upstream precedent | none | GTK, WPE, PlayStation | **Windows, by default** |
| Compat layer reuse | partial; a different release's binaries | none | **complete; our verified CoreGraphics and CoreText work is on the right side of the line** |
| New code | unknown until the spike reports | large, a whole graphics backend | 4,900-7,250 |
| Fails how | all at once, at load time | gradually, in fidelity | in text, visibly and early |

The distinguishing argument for this branch is that it is the only one that puts
our existing verified compatibility work, 104 CoreText checks with a live oracle
and 54 CoreGraphics checks with behavioural probes, on the side of the line where
it is used, and the only one with a shipping upstream port doing the same thing.

### Risk list

1. **AAT shaping divergence.** Highest. No upstream precedent, visible immediately
   on Tiger's own system faces.
2. **Metrics disagreement between the two sides**, causing layout that does not
   match the paint. Mitigated by making the render process authoritative.
3. **Font fallback rewritten from scratch** in the recording process.
4. **Serializer asymmetry.** Contained and well understood, but it must be fixed
   before a single pixel appears, and a mistake here is a silent wire corruption
   rather than a compile error.
5. **The same-clang alignment rule.** A build-environment rule, not code. Easy to
   violate accidentally, catastrophic and confusing when violated.
6. **Shared-memory limits** on Tiger under a process that allocates tiles
   aggressively. A measurement.
7. **Path geometry** in the 64-bit process needs the scalar implementation.

---

## 7. Recommendation

Adopt the split, with these four decisions:

1. **Two processes, not three.** The replay lives in the 32-bit UI process
   alongside AppKit and the compositor. Start merged; splitting later is cheap
   and merging later is impossible.
2. **The UI process owns the layer tree.** Retarget the Windows coordinated-layer
   delta at it and rewrite the scene applier for real layers. That reuses about
   1,600 lines of recording-side code unchanged.
3. **Force the neutral colour-space encoding on both sides** before anything
   renders, and add the pixel-less image-buffer backend.
4. **Make the 32-bit side authoritative for font metrics.** Do not reimplement
   the CoreText metrics ladder anywhere else. Decide the metrics service versus
   direct table parsing before shaping work starts, because everything downstream
   depends on it.

Then prove the risky thing first: shape a paragraph of Lucida Grande in the
recording process, rasterize it in the replay process, and compare against the
box rendering the same text with CoreText end to end. If that matches, the branch
is sound. If it does not, we learn it in a week rather than a quarter.
