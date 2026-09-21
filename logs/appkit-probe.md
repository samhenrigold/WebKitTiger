# AppKit on Tiger versus modern, measured

Probe `spike/akbehaviour.m`, runner `spike/run-akbehaviour.sh`. Same dual-build differential method
as the CoreGraphics and Foundation probes: one source built for 10.4.11 and for this Mac, running
under a live `NSApplication` with a real on-screen window on both, printing identical `KEY=value`
lines, diffed mechanically. AppKit works fully over ssh on the Tiger box, window server and all,
which was verified before any of this was written.

**177 values compared, 48 differing, 7 unmeasurable.** Scope was what WebCore/mac and
WebKitLegacy/mac call by name.

## A caveat that shapes how to read this

Unlike CoreGraphics and Foundation, most AppKit differences here are *supposed* to exist. A 2005
Aqua control metric and a 2025 one differ because twenty years of design happened. This Mac is also
Retina, so its backing scale is 2 where Tiger's is 1, and **Tiger being 1.0 is the correct answer,
not a divergence**. The report separates structure from appearance rather than counting
differences.

## Two real findings

### `-[NSScreen backingScaleFactor]` is not shimmed, and WebKit calls it

nscompat adds `backingScaleFactor` to NSView and NSWindow but not to NSScreen, and
`AppKitCompat.h` has no NSScreen section at all. The probe died on
`-[NSScreen backingScaleFactor]: selector not recognized` before it was guarded.

Two live Mac call sites:

- `WebKitLegacy/mac/WebView/WebView.mm:6895` — `return [[NSScreen mainScreen] backingScaleFactor];`
- `WebCore/platform/mac/PlatformScreenMac.mm:201` — `screenData.scaleFactor = screen.backingScaleFactor;`

Both would raise at runtime. The fix is a three-line category returning 1.0, matching the NSView
and NSWindow shims already there. **Sent to nscompat.**

### NSFont and CoreText disagree about xHeight and capHeight on Tiger

On modern they agree exactly. On Tiger they do not:

| metric | NSFont | CoreText | modern (both) |
|---|---|---|---|
| ascender / ascent | 12.320 | 12.320 | 12.320 |
| descender / descent | -3.680 | 3.680 | matches |
| xHeight | 8.500 | 8.488 | 8.367 |
| capHeight | 11.500 | 11.633 | 11.477 |

Helvetica at 16pt. Ascent and descent agree exactly on both platforms, so this is specific to the
two metrics `ctcompat` synthesises rather than reads, and NOTES records those as computed from the
midpoint of flat and round glyph heights.

Two things follow. First, WebCore mixes the paths, taking some metrics from the platform font and
some from CoreText, so a 0.133pt capHeight disagreement can shift where a baseline or an underline
lands depending on which path produced it. Second, and more useful: **for capHeight, Tiger's own
NSFont is a much better estimator than the glyph-height heuristic.** Against modern's 11.477,
NSFont's 11.500 is off by 0.023 and ctcompat's 11.633 is off by 0.156, nearly seven times worse.
For xHeight the two are close, 0.133 against 0.121 in ctcompat's favour.

That also puts a number on the NOTES claim that the adapters reproduce modern CoreText to 0.03%.
For Helvetica 16pt capHeight the error is 1.4%. The claim may hold across the font set it was
measured on; this is one counterexample worth checking against that set. **Sent to ctcompat.**

## One shim behaviour difference, low impact

`+[NSColor colorWithSRGBRed:green:blue:alpha:]` passes its components through unchanged, so
0.25/0.5/0.75 comes back as 0.25/0.5/0.75 in calibrated RGB. Modern converts sRGB to the calibrated
space and yields 0.198/0.417/0.696. The shim treats sRGB values as if they were already calibrated.

Tiger has no sRGB colour space to convert through, so this is close to the only thing the shim can
do, and the existing `appkittest.m` check only asserts that sRGB black matches calibrated black,
which is true for any gamma. Worth a comment saying the conversion is skipped rather than
implying the spaces are the same. Colour fidelity in WebCore goes through CoreGraphics, not
NSColor, so the blast radius is small.

## Expected absences, none reached by the Mac port

- `-[NSBitmapImageRep initWithCGImage:]` is 10.5 and absent. The probe measures the Tiger route
  instead, drawing the CGImage into a rep's own graphics context, and that works. WebKit's
  `initWithCGImage:` call sites are all `UIImage`, so the Mac port does not reach this.
- `-[NSEvent CGEvent]` returns nil where modern returns a real event. Tiger has no CGEvent backing
  an NSEvent; the shim returning NULL is the honest answer.

## The shim is more permissive than modern, deliberately

The 10.7-10.10 scroll accessors nscompat adds — `phase`, `momentumPhase`, `scrollingDeltaX/Y`,
`isDirectionInvertedFromDevice`, `stage` — return zero values on Tiger for any event. **Modern
raises** `NSInternalInconsistencyException` for several of them when the event type does not carry
them: `-phase` on a mouse event throws. The probe catches per accessor and records `raises`.

This is the safe direction and WebCore only calls them on scroll events, so nothing needs changing.
Worth knowing when reading the probe output, since six of the 48 differences are this.

## Cosmetic and metric differences, no action

Control metrics and artwork moved between 2005 and 2025: scroller width 15 against 17, which also
explains the scroll view's clip bounds being 185x85 against 200x100 for the same frame; I-beam
cursor hotspot; the appearance-derived colours (selected text, secondary label, find highlight),
which the probe prints for the record without asserting equality.

`+[NSBezierPath bezierPathWithRect:]` yields 6 elements on Tiger and 5 on modern. Only matters to
code that walks elements; WebCore does not.

`redColor` in calibrated RGB is 1,0,0 on Tiger and 0.986,0,0.027 on modern, because modern's
`redColor` is sRGB-based and converting it to calibrated is lossy. Same for the `CGColor` component
that follows from it. This is a colour-space semantics change in AppKit, not a Tiger defect.

## Could not be measured, and why

**NSPasteboard: no pasteboard server in an ssh session.** Every pasteboard comes back nil on the
box, including `+generalPasteboard`, so `declareTypes:`, `setString:forType:`, `stringForType:` and
the change-count behaviour are all unmeasured. This is the session, not Tiger: the window server
*is* reachable from ssh, windows are created and `screencapture` produces a real 984KB screenshot,
but `pbs` is not in the session's bootstrap namespace. Confirmed with a standalone program and
again through `osascript`, so it is not an artifact of the probe. **Running the probe from a
console session on the box would measure it**, and that is the only way to close this gap.

**Drawing and occlusion: the host side does not display.** `-displayIfNeeded` draws on Tiger and
not on the host, because a bare non-bundled binary on modern macOS never gets its window shown
even after `orderFront:`. So the limited side here is the *host*, not Tiger, and the three drawing
keys plus `occlusionState` have no usable reference. What Tiger does on its own is worth recording:
two separate `setNeedsDisplayInRect:` calls coalesce into a single `drawRect:` whose dirty rect is
the **full view bounds**, not the union of the two rects. That is more repainting than modern would
do, a performance characteristic rather than a correctness one.

**`[NSCursor currentCursor]` after push/pop** did not return to the original on Tiger. Low
confidence: `currentCursor` may not be meaningful in a session without console focus, and I did not
find a way to establish that without a console login.

## What matched, including the parts that matter most

The coordinate conversions WebHTMLView lives on are exact: a flipped subview and an unflipped
subview in the same content view both convert points and rects to and from the content view
identically on Tiger and modern, including the flipped case landing at y=70 where the unflipped one
lands at y=20. Bounds scaling behaves the same, with `setBoundsSize:` leaving the frame alone and
changing the bounds, and conversions afterwards agreeing.

Also matching: content view not flipped, custom view flipped; backing conversions being the
identity at scale 1, which is what Tiger correctly reports; tracking rect tags non-zero and
distinct per rect, and removable; menu construction, titles, key equivalents, separator detection
and lookup by title; NSFont name, family, point size, ascender, descender, leading and glyph count;
attributed string substrings, attribute dictionaries, effective ranges including stopping at a
mixed-attribute boundary, and an RTF round trip preserving both the string and the font attribute;
NSImage size, TIFF round trip and representation count; NSBezierPath bounds, element count for a
constructed path, emptiness, `containsPoint:` inside and outside, oval element count, default line
width and winding rule; scroll view document geometry and scrolled clip origin; cursor factory
methods; and the structural colours, black, white and clear.
