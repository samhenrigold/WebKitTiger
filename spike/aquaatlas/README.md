# aquaatlas

Renders Tiger's real Aqua controls into an atlas the 64-bit content process can
paint from. The 64-bit side has no AppKit, so every control
`RenderThemeMac` / `ThemeMac` / `ScrollbarThemeMac` would draw is rendered here
by the system's own code and shipped across as pixels plus a manifest.

Build, run on the box, and fetch the result:

```
spike/aquaatlas/build.sh          # writes spike/aquaatlas/out/
```

366 PNGs and `atlas.json`. 32-bit MRR Cocoa, ARGB8888 premultiplied, 1x.

## What is in the manifest

Per image: control id, state, size, pixel rect, 9-slice insets, which axes
stretch, the minimum size those insets imply, and the natural size the control
reports for itself. Scrollbars additionally carry part rects for the thumb, the
two track halves and both arrows. Then a `colors` block with the system colours
`RenderTheme` asks `NSColor` for.

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
