# WebKit1 accelerated-compositing layer hosting, 2009 era (source reference)

Plan reference: `logs/webcore-plan.md` §6 ("Later phase: compositing through
CARenderer"), specifically §6.1-6.3. This is a **later-phase (M5+) reference
fetch**, not something the phase-1 build needs. The plan's primary reference
for this work is already local and binary: `atv/extracted/3.0.2/WebKit.framework`
(WebKit 528.18.0 for Darwin 8 i386, from Apple TV Software 3.0.2) — see
`atv/REPORT.md`. These files are the **source-level companion** to that
binary: same generation of WebKit (528.x), same simple `NSOpenGLView`-free,
"just host a `CALayer` in an `NSView`" design later WebKit replaced with the
much more complex tiled/async-scrolling machinery.

## Why this era

The plan names "the Apple TV WebKit 528 build" as the target era. WebKit
528.18 shipped roughly with Safari 4 / iPhone OS 3.x, mid-2009. Querying each
file's commit history with `until=2009-08-01` (last commit before Aug 2009)
landed all five files within a 3-week window in July 2009 — consistent with a
single release branch point, and before the WebCore/WebKit → Source/WebCore /
Source/WebKitLegacy directory rename (2011) and before the Lion-era
NSScrollerImp / tiled-layer / async-scrolling rewrites.

## Files fetched

| File | Commit | Date | LOC |
|---|---|---|---|
| `WebLayer.mm` / `.h` | `cb4d272f89d89ff13d3f53246ade43393c5bd312` | 2009-07-09 | 230 / 64 |
| `WebTiledLayer.mm` / `.h` | same | 2009-07-09 | 118 / 45 |
| `GraphicsLayerCA.mm` / `.h` | `69092e68e594ad9302e216842c9fd4c3726a87d9` | 2009-07-31 | 1770 / 270 |
| `WebHTMLView.mm` | `ba4abdc6699b04cb5f7d81e81fa1ea4e9dfae37d` | 2009-07-22 (commit msg dated 2009-07-20) | 6084 |
| `WebHTMLViewInternal.h` | same | 2009-07-22 | 68 |
| `WebView.mm` | `69092e68e594ad9302e216842c9fd4c3726a87d9` | 2009-07-31 | 5508 |

Source paths at this era (pre-2011 rename, so **not** under `Source/`):
`WebCore/platform/graphics/mac/{WebLayer,WebTiledLayer,GraphicsLayerCA}.{h,mm}`
and `WebKit/mac/WebView/{WebHTMLView,WebHTMLViewInternal.h,WebView}.mm`.

## What's actually in them, confirmed by grep

`WebHTMLView.mm:5386` — `-attachRootLayer:` under `#if
USE(ACCELERATED_COMPOSITING)`:

```objc
- (void)attachRootLayer:(CALayer*)layer
{
    if (!_private->layerHostingView) {
        NSView* hostingView = [[NSView alloc] initWithFrame:[self bounds]];
        [hostingView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
        [self addSubview:hostingView];
        ...
        _private->layerHostingView = hostingView;
        [[self _webView] _startedAcceleratedCompositingForFrame:[self _frame]];
    }
    CALayer* viewLayer = [CALayer layer];
    [_private->layerHostingView setLayer:viewLayer];
    [_private->layerHostingView setWantsLayer:YES];
    [viewLayer addSublayer:layer];
}
```

and `-detachRootLayer` right after it (`:5407`). This is the same shape the
current tree still has at `Source/WebKitLegacy/mac/WebView/WebHTMLView.mm:6132`
per plan §6.2 — the API surface hasn't changed in 15+ years, only the
surrounding tiling/scheduling machinery has. Useful as a **much simpler,
readable** version of the same idea, since the 2026 tree's version is wrapped
in decades of accumulated tiled-layer and async-scrolling code the Tiger port
doesn't need.

`WebLayer.mm`/`WebTiledLayer.mm` at this era are small (230 + 118 LOC) — this
is from before `PlatformCALayerCocoa.mm` absorbed `WebLayer`/`WebSimpleLayer`
(per plan §6.2's note that the modern location is
`WebCore/platform/graphics/ca/PlatformCALayerCocoa.mm`) and before
`WebTiledLayer` was replaced by `TileController`-driven
`WebTiledBackingLayer.mm`. Good reference for the *original*, unsophisticated
`CATiledLayer` subclass the plan's §6.3 point 4 ("re-enable the CA files...
with the tiling set") is ultimately descended from.

`GraphicsLayerCA.mm`/`.h` at 1770/270 LOC (vs. the modern file which is much
larger) is the pre-tiling, pre-async-scrolling version — closer in spirit to
what a from-scratch CARenderer host (plan §6.3) would actually need, since
Tiger has no tile controller and no async scrolling.

`WebView.mm` at this commit did **not** contain `attachRootLayer` /
`WebRootLayer` / `setLayer:` matches (grepped, zero hits) — the layer-hosting
logic in 2009 lived entirely in `WebHTMLView.mm`; `WebView.mm` is included
here per the request but turned out not to carry compositing-hosting code at
this era. Fetched anyway since it was cheap and the plan's §6.2 table does
reference `WebChromeClient.mm` (not `WebView.mm`) as the other call site —
kept for general reference on WebView-level state from the same commit.

## Not fetched

- `PlatformCALayerCocoa.mm` at this era doesn't exist yet under that name (see
  above — `WebLayer.mm` is its ancestor, which was fetched instead).
- `PlatformCAAnimationCocoa.mm`, `PlatformCAFiltersCocoa.mm` — not requested,
  not fetched; plan §6.3 point 4 lists these as needed only when the M5+ phase
  actually starts re-enabling the CA files, which is well past what this
  fetch was scoped for.

## Method

Same as the curl topic: `gh api "repos/WebKit/WebKit/commits?path=<path>&until=<date>&per_page=1"`
to land the last commit before a cutoff date, then `curl -sL
https://raw.githubusercontent.com/WebKit/WebKit/<sha>/<path>`.
