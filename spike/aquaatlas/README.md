# aquaatlas

Harness for `compat/aquacontrols.m`, which is the actual deliverable.

WebCore already describes a native control as a serialisable `ControlPart` plus
a `ControlStyle` and hands the pair to whichever process can draw it. On this
port the 64-bit content process has no AppKit, so the 32-bit render process
draws them through `TigerDrawControl` in
`<TigerCompat/AquaControls.h>`, a C API implemented by porting
`WebCore/platform/graphics/mac/controls/*.mm` to what 10.4 has.

This directory renders every kind, state and size class into PNGs so the
artwork can be looked at, and dumps the system colours and font metrics the
content process needs.

```
spike/aquaatlas/build.sh          # writes spike/aquaatlas/out/
```

540 PNGs plus `metrics.json`. 32-bit MRR Cocoa, ARGB8888 premultiplied, 1x.

## What came from WebKit rather than being invented

The cell types and their configuration follow `ControlFactoryMac`: the button
cell's `NSButtonTypeMomentaryPushIn` with a nil title, the toggle cells'
exterior focus ring and `allowsMixedState`, the popup's `usesItemFromMenu:NO`,
the search field's rounded bezel, the slider cell pinned to the small control
size. The size-class-from-font rule is `ControlMac::controlSizeForFont`, asking
`+[NSFont systemFontSizeForControlSize:]` rather than hardcoding thresholds. The
`cellSize` and `cellOutsets` tables are the ones from `ButtonMac`,
`ToggleButtonMac` and `MenuListMac`, and `inflatedRect` centres and grows the
same way `ControlMac` does. `ButtonMac::bezelStyle`'s rule that a button taller
than the rounded bezel gets the square one is kept.

## Where Tiger forced a difference

**The button family draws through HITheme, not the cell.** `-[NSCell
drawWithFrame:inView:]` produces byte-identical pixels whether or not its
window is key, so the window-inactive appearance is unreachable through the
cell. `HIThemeDrawButton` takes the state as an argument, has distinct active,
inactive, pressed and disabled artwork, and is what AppKit draws through
underneath. The cells are still configured because the geometry comes from
them; only the final blit is HITheme. Buttons, check boxes, radios and popups
therefore have a real inactive appearance; text fields, search fields and
progress bars do not, because nothing on 10.4 exposes one for them.

**No mini artwork for the toggles.** Tiger's `ThemeButtonKind` has small
variants for the check box, radio button and bevel button and no mini ones, so
mini falls back to small.

**Indeterminate progress has one frame.** `ProgressBarMac` advances the barber
pole by an animation phase; Tiger's `NSProgressIndicator` owns its own timer and
exposes no phase.

**Search field uses the rounded text field bezel.** `NSSearchFieldCell` exists
on 10.4 but draws its bezel through the search field's own subviews. The frame
artwork is the same, and the magnifier and cancel glyphs are separate parts that
WebCore draws as `SearchFieldResults` and `SearchFieldCancelButton` anyway.

**`NSRectFromCGRect` is 10.5**, and on 10.4 `NSRect` and `CGRect` are distinct
structs, so the conversion is written out rather than cast.

## Five things that were measured rather than assumed

Each of these looked fine and was wrong, so they are worth keeping written down.

**`CGBitmapContextGetData` returns NULL on Tiger** for a context created with a
NULL data pointer. The 9-slice measurement reads pixels back, so the buffer is
allocated here and handed to CoreGraphics rather than letting it allocate. The
first version crashed in the pixel diff; a NULL check would have "fixed" the
crash and silently produced zero insets for everything.

**A control at its natural size is all end-cap.** Measuring a 48-pixel capsule
button finds the left 24 columns and the right 24 columns invariant, meeting in
the middle with no tileable centre. The stored image is therefore padded by 16
pixels along each stretchable axis so a real middle exists, and the inset search
gives back one column at the centre if the two caps still meet.

**`-[NSCell drawWithFrame:inView:]` does not vary with window-key state.** Its
artwork is byte-identical whether or not the cell's window is key, so the
window-inactive axis is unreachable that way. `HIThemeDrawButton` takes the
state as an argument and does have distinct active, inactive, pressed and
disabled artwork, and is what AppKit draws through underneath. The button family
(push, check box, radio, disclosure, both popup kinds) goes through HITheme for
that reason. Text fields and slider tracks are still NSCell and still do not vary
by window state; that is a real gap, not a bug in the generator.

**A bare executable cannot become the active application**, so no window can
become key, so every control draws its inactive artwork with nothing to say so.
The tool has to run from inside a `.app` bundle. Worse, the *first* launch of a
freshly created bundle still cannot be foregrounded, because LaunchServices has
not registered it yet. `build.sh` burns a warm-up launch, and the tool refuses
to write an atlas when its active pass is not actually active. Two regenerations
were silently wrong before that guard existed.

**`HIThemeDrawTrack` draws a whole scrollbar**, not a part. Asking it separately
for a track, a thumb and the arrows produced three identical complete
scrollbars. There is now one image per orientation with the parts reported as
rects inside it, measured with `HIThemeGetTrackPartBounds`, which is the shape
`ScrollbarThemeMac` wants anyway.

## Known limits

- `kThemeStatePressed` wins over window-inactive, so `pressed-inactive` equals
  `pressed`. A control being clicked is in an active window by definition.
- Tiger's `ThemeButtonKind` has no mini variants and small ones only for the
  check box, radio button and bevel button. Mini falls back to small, which is
  the closest artwork this system has; for the push button and popup the size
  comes from the bounds, because there is no `kThemeSmallPushButton` to ask for.
- `+controlColor` and `+windowBackgroundColor` are pattern colours with no RGB
  components. They are sampled by filling one pixel and reading it back, which
  gives 0.9255 grey, rather than being reported as null.
- The default-button pulse is not rendered; the atlas has the unpulsed artwork.
- Text field and slider track have no distinct window-inactive artwork, per the
  NSCell finding above. Routing them through HITheme would fix it.
