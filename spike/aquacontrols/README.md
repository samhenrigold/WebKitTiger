# aquacontrols — the test plan for ControlFactoryTiger

`compat/aquacontrols.m` draws Aqua controls and `spike/aquaatlas` already proves it
matches live 10.4 cells at the C API. What is new in
`Source/WebCore/platform/tiger/` is the layer between WebCore and that C API:

- `ControlFactoryTiger` — the sixteen `createPlatform*`s, and `ControlFactory::create()`
- `ControlTiger` — `StyleAppearance` → `TigerControlKind`, `ControlStyle` →
  `TigerControlStyle`, `rectForBounds`, `sizeForBounds`, the per-part payload
- `RenderThemeTiger` — the Aqua metrics the boxes are laid out to

None of it has been compiled: it was written while the build machine was reserved for
another track. This file is what to run the day it compiles, in the order that finds
the most bugs soonest.

## 0. Does it build

```
# i386 arm, which is where these files live
cmake -DPORT=Tiger -DTIGER64=OFF ...     # PlatformTigerControls.cmake is included from
ninja -C build/tiger-gpu WebCore         # PlatformTiger.cmake's NOT TIGER64 arm
```

Three edits are expected to be needed first and are documented at the top of
`RenderThemeTiger.h`: drop `final` from the thirteen `RenderThemeAdwaita` functions
overridden here, and add a `PLATFORM(TIGER)` branch to `RenderTheme.h`'s singleton
declaration. Both were left unmade on purpose — those headers are included by most of
WebCore in the x86_64 tree too.

## 1. aquaparts — pixel compare, part by part

`aquaparts.mm` is the harness. It builds a real `ControlPart`, draws it through
whatever `ControlFactory::create()` returns, and compares the result against a real
`NSCell` drawn in a real window on the box — same rect, same state, same size class,
same backdrop. Harness pattern is `spike/aquaatlas/comparecontrols.m` and
`spike/textpixel`: two bitmaps of identical geometry, channel-by-channel compare,
PNG pair written only for failures, non-zero exit if anything fails.

```
# link line, never run: WebCore i386 plus the compat archive
toolchain/bin/tiger-clang++ -g -O1 -fobjc-runtime=macosx-fragile-10.4 -fobjc-exceptions \
    -isystem compat/include -I build/tiger-gpu/WebCore/PrivateHeaders \
    spike/aquacontrols/aquaparts.mm \
    build/tiger-gpu/lib/libWebCore.a build/tiger-gpu/lib/libWTF.a compat/libtigercompat.a \
    -framework Cocoa -framework Carbon -ObjC -o spike/aquacontrols/aquaparts
```

Run it **from inside a .app bundle**, and burn the first launch. Both rules are
aquaatlas findings and both fail silently rather than loudly: a bare executable cannot
be foregrounded, so every live control draws its inactive artwork, and a freshly
created bundle's first launch only registers with LaunchServices.
`spike/aquaatlas/compare.sh` is the script to copy.

Cases, 3 size classes × 8 states each: button, checkbox, radio, textfield, textarea,
searchfield, menulist, sliderthumb, slidertrack, progressbar (skipped, see below),
switch.

**What this catches that aquaatlas cannot**, and therefore what to look at first when
it fails:

1. **The appearance map.** `PushButton` vs `DefaultButton` vs `SquareButton` all reach
   `createPlatformButton`; a wrong `TigerControlKind` shows up as the right control in
   the wrong bezel.
2. **The state transfer.** `TigerControlState*` is declared at the bit positions of
   `ControlStyle::State`, so `states.toRaw()` crosses with no table. If someone
   reorders `ControlStyle::State`, every state is subtly wrong and *only this test
   sees it* — the enum values are the contract and nothing checks them at compile
   time. **Add a `static_assert` per bit in `ControlTiger.mm` when this first runs.**
3. **`rectForBounds`.** The bezel and its shadow live outside the border box. A
   control that is right but sits a pixel high is the classic outsets bug, which is
   why every comparison covers the control's frame plus an 8px margin.
4. **The per-part payload.** `slidertrack` is drawn at thumb position 0.75 and
   compared against a live cell at 0.75. A track that ignores the part's position
   passes every C-level test and fails this one.

## 2. Metrics — the eight numbers that are not yet measured

`RenderThemeTiger.mm` marks every value it could not take straight from a cell with
`MEASURE:`. Each is one line in a tool on the box, and each has a table entry in the
file to check against:

| value | in the file | how to measure |
| --- | --- | --- |
| popup internal padding | `popUpPadding`, `{2,26,3,8}` / `{2,23,3,8}` / `{2,22,3,10}` | `-[NSPopUpButtonCell titleRectForBounds:]` minus the bounds, per control size |
| text field inset | `textFieldBorderWidth` 2, `textFieldPadding` 1 | `-[NSTextFieldCell drawingRectForBounds:]` minus the bounds |
| slider thumb | `sliderThumbSize` 15 | `-[NSSliderCell knobRectFlipped:]` on a small cell (today's RenderThemeMac says 17, which is the 10.10 knob) |
| slider ticks | `sliderTickWidth` 1, `sliderTickHeight` 7, offset 8 | `-[NSSliderCell rectOfTickMarkAtIndex:]` |
| progress bar heights | `progressBarHeights` 16/10/10 | `-[NSProgressIndicator sizeThatFits:]` per control size, and the painted height in the atlas |

The five tables that are **not** marked — push button 20/16/13, checkbox 14/12/10,
radio 16/12/10, popup 21/18/15, search field 22/19/17 — come from
`compat/aquacontrols.m`'s `cellSizeFor`, which drew the 540-image atlas that was
pixel-compared against live controls, so they are already measured.

The size-class thresholds (13 / 11 / 9) are measured: `spike/aquaatlas/out/metrics.json`.

## 3. What is stubbed, and what each stub costs

- **Search field cancel and results buttons.** `nullptr`. On 10.4 those glyphs live
  inside `NSSearchFieldCell`'s own subviews and cannot be drawn standalone. Cost: a
  search field with no glyphs. Fix: blit them out of the atlas.
- **Switch.** Drawn as a check box. Aqua 2005 has no switch — `NSSwitch` is 10.15 —
  and inventing one would put the only non-real control on the page. The `switch` case
  in aquaparts compares against a check box on purpose, so the divergence is recorded
  rather than hidden.
- **Progress bar, live side.** `NSProgressIndicator` is a view, not a cell, so the
  harness has no live comparison for it and every progress state reports SKIP. The
  drawn side works (aquaatlas has the images); writing the live capture means putting
  the indicator in the window and capturing the view rect.
- **Indeterminate progress animation.** One fixed frame. Tiger's
  `NSProgressIndicator` owns its own barber-pole timer and exposes no phase, so
  `animationPhase` is never read.
- **Apple Pay, image controls.** `nullptr`. Neither exists in 2005.
- **`deviceScaleFactor`.** Ignored. Every 10.4 display is 1x.

## 4. The end-to-end check, once the replayer runs

The pixel tests above run in one process. The thing the port actually ships is a
`ControlPart` **serialized** from the web process and replayed here. When gpu32b's
`RemoteGraphicsContext` replayer is up, the check is: load a page with one of each
control, `UseGPUProcessForDOMRenderingEnabled` on, and diff the page bitmap against
the same page with the parts drawn locally. Any difference is in the wire, not in
this code, because this code is already proven by step 1.
