# WebKitLegacy/mac on Tiger — file-level task list (M3–M5)

Turns §5 of `logs/webcore-plan.md` into per-file work now that the compat layer is
measured. Written 2026-09-20 against WebKit `d2f52605`, read-only: nothing under
`WebKit/` was modified.

Every line number below was read back from the file. Where it disagrees with the
earlier survey in `webcore-plan.md`, the correction is marked **[corrected]**.

Remedy vocabulary:

| Tag | Meaning |
|---|---|
| **SHIMMED** | already covered; the compat header is named |
| **GATE** | `#if !PLATFORM(TIGER)` / `#if PLATFORM(TIGER)`; the Tiger branch is stated |
| **RESTORE** | take the older code path; the reference is cited, usually a sibling line in the same file |
| **EXCLUDE** | drop via `WebKitLegacy_UNIFIED_SOURCE_EXCLUDES`; the regex is given |
| **NEW** | needs code written; LOC estimated |

`Source/cmake/WebKitMacros.cmake` applies `<framework>_UNIFIED_SOURCE_EXCLUDES` as
a regex filter over `Sources*.txt` **before** unified bundles are generated, and it
runs after `PlatformCocoa.cmake`. Excluding a file therefore needs no edit to
`SourcesCocoa.txt` or `SourcesCMakeCocoa.txt`.

---

## 0. Headline findings

**The exclusion set is five files.** Everything else in 2100+ files of
WebKitLegacy is a feature-macro flip, a small gate, or already shimmed.

```cmake
list(APPEND WebKitLegacy_UNIFIED_SOURCE_EXCLUDES
    "^mac/WebInspector/WebInspectorFrontend\\.mm"
    "^mac/WebInspector/WebNodeHighlight\\.mm"
    "^mac/WebInspector/WebNodeHighlightView\\.mm"
    "^mac/WebInspector/WebNodeHighlighter\\.mm"
    "^mac/WebCoreSupport/TextIndicatorWindow\\.mm")
```

Plus three files excluded by regex that are not reachable by a feature flag:
`mac/WebView/WebImmediateActionController\.mm`, `mac/WebView/WebWindowAnimation\.mm`,
and — only if `ENABLE_FULLSCREEN_API=OFF` does not empty it —
`mac/WebView/WebFullScreenController\.mm`.

**Four defects nobody had logged**, all of which produce a compile error or a
silently wrong answer rather than a missing feature:

1. `mac/Panels/WebPanelAuthenticationHandler.m:39-43` — three ivars declared in the
   `@implementation` brace block. Verified failing under
   `-fobjc-runtime=macosx-fragile-10.4` with this toolchain:
   `error: inconsistent number of instance variables specified`. A class extension
   will **not** work; extension ivars need the non-fragile runtime. Move them to an
   ivar block on the `@interface` in `WebPanelAuthenticationHandler.h:34`.
   **NEW, ~6 LOC, 2 files.**
2. `mac/WebCoreSupport/WebJavaScriptTextInputPanel.m:38` — same defect. Move the
   block into `@interface WebJavaScriptTextInputPanel : NSPanel { … }`.
   **NEW, ~6 LOC.**
3. `mac/WebView/WebFeature.m` — `WebFeature.h:73-80` declares eight
   `@property (readonly)`, and the `.m` has **no getters and no `@synthesize`**. It
   assigns `_key`/`_preferenceKey`/`_name`/`_details` at `:35-42` and reads
   `self.name`/`self.key`/`self.defaultValue` at `:57`, relying entirely on
   auto-synthesised ivars — impossible on the fragile ABI. Declare the eight backing
   ivars in the `@interface` brace at `WebFeature.h:71` and add matching
   `@synthesize`. **NEW, ~18 LOC.** Note this file is added directly to
   `WebKitLegacy_SOURCES` in `PlatformCocoa.cmake` (~:71), not to a unified list, so
   an exclude regex would not reach it.
4. `mac/WebView/WebDelegateImplementationCaching.mm:81-83` — casts `objc_msgSend`
   to `typedef float (*ObjCMsgSendFPRet)(id, SEL, ...)`. On i386 a float return comes
   back on the x87 stack, so this must be `objc_msgSend_fpret`. Called at `:334`
   (`CallDelegateReturningFloat`), reached from `:1063-1068`. **Silent garbage, not a
   compile error.** GATE `#if PLATFORM(TIGER)` (or `#if defined(__i386__)`) and cast
   `objc_msgSend_fpret`. **~4 LOC.** No struct-returning delegate exists, so no
   `_stret` variant is needed.

**Three things the earlier plan got wrong and should not be done:**

- **Do not exclude `mac/Plugins/`.** There is no NPAPI in it — no
  `WebNetscapePlugin*`, no `npapi.h`. `WebPluginPackage` is the ObjC `WebPlugin`
  protocol only, and `WebFrameLoaderClient.mm:1708-1711 shouldBlockPlugin()` already
  returns `true` unconditionally, so no plugin view is ever created. The directory
  compiles clean and does nothing. Excluding its 5 sources means editing ~40 sites
  across `WebView.mm`, `WebViewData.h`, `WebViewInternal.h`, `WebHTMLView.mm`,
  `WebHTMLViewInternal.h`, `WebFrameLoaderClient.mm`, `WebChromeClient.mm`,
  `WebHTMLRepresentation.mm`, `WebPlatformStrategies.mm`, `WebHistoryItem.mm` and
  `WebPluginInfoProvider.mm` to delete ~1800 lines of already-dead code.
- **Do not exclude `LegacyWebPageInspectorController.cpp` or
  `WebInspectorClient.mm`.** Both break the link. Details in §6.
- **`WebPlatformStrategies.mm` needs zero work**, not "adapt". All 29 pasteboard
  methods at `:114-267` are one-line forwards to `WebCore::PlatformPasteboard`.

**Global facts that change many rows:**

| Fact | Evidence |
|---|---|
| Everything here compiles **MRR, not ARC** | every entry in `SourcesCocoa.txt` / `SourcesCMakeCocoa.txt` carries `@nonARC`; `WebKitMacros.cmake:187-214` routes on that marker |
| `-fobjc-weak` is already off for Tiger | `OptionsCocoa.cmake:395-396` |
| The macOS branch of `PlatformCocoa.cmake` has **no** Info.plist, `.exp` or framework-header logic | lines 238–1254 are entirely inside `if (WEBKIT_SDK_IS_IOS_FAMILY)` |
| Tiger AppKit coalesces several `setNeedsDisplayInRect:` into one `drawRect:` with the **full view bounds** | `logs/appkit-probe.md` |

---

## 1. WebView / WebFrameView / WebHTMLView

### 1.1 WebHTMLView.mm — the drawRect path with compositing off

The path itself is **clean** and needs no change:

| Step | Site |
|---|---|
| `-drawRect:` | `:3891` — `getRectsBeingDrawn:count:` `:3900`, union-vs-individual heuristic `:3918-3937` |
| `-drawSingleRect:` | `:3845` — `setAdditionalClip:` `:3853`, `resetAdditionalClip` `:3879` |
| → `-[WebFrame _drawRect:contentsOnly:]` | `WebFrame.mm:666` |

Because Tiger coalesces dirty rects to the full bounds, the multi-rect branch at
`:3918-3937` always takes `useUnionedRect`. That degrades correctly; no edit.

`WebClipView.mm` supplies `setAdditionalClip:` `:148` / `resetAdditionalClip` `:142`
/ `hasAdditionalClip` `:155` / `additionalClip` `:160`, all Tiger-era. Its
`_immediateScrollToPoint:` `:105`, `_disableDelayedWindowDisplay` `:110`,
`releaseGState` `:80` and `_focusRingVisibleRect` `:166` are Tiger-era NSClipView
SPI and stay.

| file | symbol + line | remedy |
|---|---|---|
| `WebView/WebFrame.mm` | `[[NSGraphicsContext currentContext] CGContext]` **:671** — `-CGContext` is 10.10 | **RESTORE** `(CGContextRef)[[NSGraphicsContext currentContext] graphicsPort]`. The overlay already declares `graphicsPort` at `compat/sdk-overlay/AppKit.framework/Headers/NSGraphicsContext.h:86`. **1 LOC.** Same fix at `Misc/WebKitNSStringExtras.mm:79` and `WebView/WebPDFView.mm:335,:348` |
| `WebView/WebClipView.mm` | `-_canCopyOnScrollForDeltaX:deltaY:` **:131-135**, decl **:54**, `[super _canCopyOnScrollForDeltaX:deltaY:]` **:133** — NSClipView SPI, ~10.8; `doesNotRecognizeSelector:` at runtime | **GATE.** Tiger drops the override; `_currentScrollIsBlit = NO` is already set in `_immediateScrollToPoint:` at **:108**. ~5 LOC |
| `WebView/WebFrameView.mm` | `-wantsUpdateLayer` **:517**, `-updateLayer` **:522** | **KEEP AS-IS.** Neither is declared on Tiger's NSView, so they compile as ordinary new methods and are simply never called; AppKit falls back to `drawRect:` at `:490`. Zero edits. **[corrected — the earlier plan said "delete both"]** |

### 1.2 The CA surface in WebHTMLView with `USE_CA` off

`WebChromeClient::allowedCompositingTriggers()` returning 0 (§5) means
`attachRootGraphicsLayer` is never called with a non-null layer, so these bodies
never run. They still have to not compile.

| symbol + line | remedy |
|---|---|
| `#import <QuartzCore/QuartzCore.h>` **:74** | GATE `#if !PLATFORM(TIGER)` |
| `@interface WebRootLayer : CALayer` **:702**, `@implementation` **:705-716** (`renderInContext:` :707) | GATE the whole block; its only consumer is `attachRootLayer:` |
| `-setLayer:(CALayer *)layer` **:3776-3784** | GATE — narrow the `#if PLATFORM(MAC)` at **:3774** to `#if PLATFORM(MAC) && !PLATFORM(TIGER)` |
| `[CATransaction flush]` **:3966**, `disableScreenUpdatesUntilFlush` **:3961** | GATE the `if ([NSGraphicsContext currentContext] == …)` block **:3957-3968** |
| `-attachRootLayer:` **:6132**, `WebLayerHostingFlippedView` :6134, `[WebRootLayer layer]` :6143, `setWantsLayer:YES` :6156, `_CFExecutableLinkedOnOrAfter` :6164, `detachRootLayer` :6167-6175, `setWantsLayer:NO` :6172, `-drawLayer:inContext:` :6178, `[layer drawsAsynchronously]` :6183 | GATE the whole run **:6130-6190**. No stub bodies needed |
| `WebHTMLViewInternal.h` — `@class CALayer;` **:34**; `attachRootLayer:` / `detachRootLayer` / `_web_isDrawingIntoLayer` / `_web_isDrawingIntoAcceleratedLayer` **:84-88**; `@interface WebHTMLView () <NSDraggingSource>` **:46** | GATE both blocks `#if PLATFORM(MAC) && !PLATFORM(TIGER)`. ~4 LOC |

### 1.3 Tracking rects

| symbol + line | remedy |
|---|---|
| `-addTrackingRect:owner:userData:assumeInside:` **:1761**, `_addTrackingRect:…useTrackingNum:` **:1769**, `_addTrackingRects:…count:` **:1778**, `removeTrackingRect:` **:1788**, `_removeTrackingRects:count:` **:1809** | **RESTORE** — the full Tiger tracking-rect implementation is intact in this file. This *is* the Tiger path |
| `for (NSTrackingArea *trackingArea in self.trackingAreas)` **:1828** in `_toolTipOwnerForSendingMouseEvents` | GATE `#if PLATFORM(TIGER)` → keep only `return _private->trackingRectOwner.getAutoreleased();` (the **:1825** line); drop the NSToolTipManager loop |
| `NSTrackingAreaOptions` **:4660**, `_NSRecommendedScrollerStyle() == NSScrollerStyleLegacy` **:4661**, `[self addTrackingArea:[[NSTrackingArea alloc] initWithRect:…]]` **:4666** | GATE the whole `if (!_private->installedTrackingArea)` block **:4659-4669**; Tiger does nothing there |
| `#import <pal/spi/mac/NSScrollerImpSPI.h>` **:151** (pulls `_NSRecommendedScrollerStyle`, `NSScrollerStyle`) | GATE out |

### 1.4 Text input — NSTextInput, not NSTextInputClient

**[corrected]** `WebHTMLView` never migrated. `:934` already reads
`@interface WebHTMLView (WebNSTextInputSupport) <NSTextInput>`, and
`NSTextInputClient` has **zero occurrences** in `WebKitLegacy/mac`. All twelve
implemented methods (block starts `:6259`) match
`sdk/MacOSX10.4u.sdk/.../NSInputManager.h:14-49` verbatim; `conversationIdentifier`
differs only as `long` vs `NSInteger`, identical on i386.

The real breaks are the 10.5 `NSTextInputContext` and three attribute constants —
five sites, not the "six deletions at :6263-6275" the earlier plan claimed:

| symbol + line | remedy |
|---|---|
| `#import <pal/spi/mac/NSTextInputContextSPI.h>` **:174** | GATE out |
| `@class NSTextInputContext;` **:203**, `-(NSTextInputContext *)inputContext;` on `NSResponder ()` **:220-222** | GATE out |
| `if ([[self inputContext] wantsToHandleMouseEvents] && …)` **:4164** | GATE. **RESTORE**: `mouseDragged:` at **:4265-4269** already carries the Tiger form, `[[NSInputManager currentInputManager] wantsToHandleMouseEvents]` / `handleMouseEvent:` — copy it |
| `-(NSTextInputContext *)inputContext` **:6278-6281** | GATE out. `NSTextInput` has no `inputContext`; Tiger TSM asks `textStorage` (**:6283**) |
| `validAttributesForMarkedText` **:6261-6277** — `NSMarkedClauseSegmentAttributeName` **:6269** (10.5), `NSTextAlternativesAttributeName` **:6271** (10.8), `NSTextInsertionUndoableAttributeName` **:6272** (10.8) | GATE the three entries. Tiger keeps `NSUnderlineStyleAttributeName`, `NSUnderlineColorAttributeName`, `NSTextInputReplacementRangeAttributeName` **[corrected: :6269/:6271/:6272, not the whole :6263-6275 span]** |
| `_updateSecureInputState` **:6747** — `TISCreateASCIICapableInputSourceList()` **:6768**, `TSMSetDocumentProperty(… kTSMDocumentEnabledInputSourcesPropertyTag)` **:6770**, `TSMRemoveDocumentProperty` **:6774`. Text Input Sources is **10.5+** | GATE **:6766-6775**. Tiger keeps only `EnableSecureEventInput()` / `DisableSecureEventInput()` (**:6761** / **:6772**, Carbon, 10.0). ~6 LOC |
| `WebEditorClient.mm:397` `[[NSTextInputContext currentInputContext] discardMarkedText]` | RESTORE `[[NSInputManager currentInputManager] markedTextAbandoned:]`; the pattern is already at `WebHTMLView.mm:6806` |

### 1.5 Event handling

The 10.12 renames are all **SHIMMED** by
`compat/sdk-overlay/AppKit.framework/Headers/NSEvent.h` — every `NSEventType*`,
`NSEventMask*`, `NSEventModifierFlag*` and `NSWindowStyleMask*`. Confirmed live
users: `Misc/WebNSEventExtras.m:38,:67`, `Misc/WebNSViewExtras.m:85,:95,:103,:117`.

| file | symbol + line | remedy |
|---|---|---|
| `WebView/WebDynamicScrollBarsView.mm` | `NSEventPhase momentumPhase = [event momentumPhase];` **:549**, `momentumPhase & NSEventPhaseBegan \|\| … NSEventPhaseStationary` **:550** | SHIMMED — `TigerCompat/AppKitCompat.h` (`momentumPhase`) + overlay `NSEvent.h` (`NSEventPhase*`). The shim returns `NSEventPhaseNone`, so `isLatchingEvent` is always NO, which is the correct Tiger behaviour: there is no momentum scrolling |
| `WebView/WebHTMLView.mm` | `[[self window] convertPointFromScreen:…]` **:1524**, **:2099**, **:5020** — 10.12, and **not currently in AppKitCompat.h** | **NEW** — add `-[NSWindow convertPointFromScreen:]` and `convertPointToScreen:` beside the existing `convertRectFromScreen:`. ~6 LOC. Also unblocks `WebTextCompletionController.mm:128` |
| `WebCoreSupport/WebChromeClient.mm` | `ENABLE(POINTER_LOCK)` **:127**, `requestPointerLock` **:708-725**, `requestPointerUnlock` **:727+** — `CGDisplayHideCursor(CGMainDisplayID())` :714, `CGAssociateMouseAndMouseCursorPosition` :715/:730 | **clean** — both CG calls are 10.0/10.1. **[corrected]** Pointer lock lives only here, not in `WebView.mm`. Gating it is optional; `AppKitCompat.h`'s `-[NSEvent CGEvent]` returns NULL so unaccelerated movement reads as zero rather than garbage. Recommend adding `ENABLE_POINTER_LOCK` to the Tiger off-list anyway |

### 1.6 WebView.mm — the rest

| symbol + line | remedy |
|---|---|
| Touch Bar: `WebTextTouchBarItemController` **:988-1259**, `showCandidates:…` **:4867-4904**, `<NSCandidateListTouchBarItemDelegate, NSTouchBarDelegate, NSTouchBarProvider>` **:5017**, `makeTouchBar` **:6802**, `textCheckingResultFromNSTextCheckingResult` **:6833**, candidate delegates **:6842**/**:6861**, `_touchBarUpdate` **:9225-9567**, `#import <pal/spi/cocoa/NSTouchBarSPI.h>` **:267** | **All already inside `#if HAVE(TOUCH_BAR)`.** Keep `HAVE_TOUCH_BAR=OFF`; **no source edit**. Consequence: `NSTextCheckingResult` appears only inside these blocks (**:4869, :4891, :6833, :6855, :6858**), so **no NSTextCheckingResult shim is needed anywhere** |
| `window.occlusionState & NSWindowOcclusionStateVisible` **:4257** | SHIMMED — `AppKitCompat.h`. Verify `NSWindowOcclusionStateVisible` is exported by the shim (it is, at `AppKitCompat.h` enum) |
| `[window backingScaleFactor]` **:6892**, `[hostWindow backingScaleFactor]` **:6894**, `[[NSScreen mainScreen] backingScaleFactor]` **:6895**, `-_backingScaleFactor` **:4672** | SHIMMED — `AppKitCompat.h` covers NSView, NSWindow **and** NSScreen |
| `[[self effectiveAppearance] bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]]` **:4812-4813** (NSAppearance 10.9 / DarkAqua 10.14) | GATE `_effectiveAppearanceIsDark` **:4810-4814** → `return false;`. ~4 LOC |
| `[CATransaction flush]` **:4390**, `[CATransaction synchronize]` **:4391** | GATE `#if !PLATFORM(TIGER)` |
| drag promise block **:6379-6419** — `enumerateDraggingItemsWithOptions:forView:classes:searchOptions:usingBlock:` **:6398** (10.7), `[NSFilePromiseReceiver class]` **:6398**, `receivePromisedFilesAtDestination:options:operationQueue:reader:` **:6404** (10.12) | GATE `#if !PLATFORM(TIGER)` the whole block; Tiger falls straight through to the plain `performDragOperation` at **:6421-6427**. ~3 LOC of `#if`. **[corrected: :6379-6419, not :6398-6420]** |
| `+_setCacheModel:` **:8392**, `_CFURLCacheCopyCacheDirectory([[NSURLCache sharedURLCache] _CFURLCache])` **:8397**, `attributesOfFileSystemForPath:error:` **:8403**; body ends **:8613** | GATE **:8397-8399** → use the existing `NSHomeDirectory()` fallback already at **:8398-8399**. `attributesOfFileSystemForPath:error:` is SHIMMED (`NSCompat.h`). ~4 LOC. **[corrected: :8613, not :8611]** |
| `[NSURLConnection canHandleRequest:]` **:3236**, `[[NSURLCache sharedURLCache] cachedResponseForRequest:]` **:3434** | **clean** — both are 10.2 |
| dragging destination `draggingEntered:` **:6320**, `draggingUpdated:` **:6333**, `draggingExited:` **:6350**, `prepareForDragOperation:` **:6366**, `performDragOperation:` **:6371**; `-_registerDraggedTypes` **:1355** | **clean** — `<NSDraggingInfo>` is 10.0 |
| `WebViewData.h:149` `RetainPtr<WebImmediateActionController> immediateActionController` — **not** gated | GATE `#if !PLATFORM(TIGER)` to pair with excluding the controller. `:152-159` (NSTouchBar, NSCandidateListTouchBarItem) is already `HAVE(TOUCH_BAR)`-gated |
| `WebViewInternal.h` — `-(WebImmediateActionController *)_immediateActionController;` **:352**, `-(NSCandidateListTouchBarItem *)candidateList;` **:372** | GATE to match. Forward `@class` decls at `:105`/`:109` compile fine. ~4 LOC |
| `WebViewPrivate.h` — `@class CALayer;` **:64**, `_setMediaLayer:forPluginView:` **:639/:641**, `showCandidates:…(NSTextCheckingResult *)` **:858/:860** | **clean as a header** — forward-declared types in method signatures compile without the real class; the implementations are gated |

### 1.7 The rest of WebView/ — one row per file

| file | verdict |
|---|---|
| `WebViewRenderingUpdateScheduler.mm` | **KEEP — GATE.** `registerCACommitHandlers()` spans **:105-120**; the `[CATransaction addCommitHandler:forPhase:]` calls are **:111-113** (`kCATransactionPhasePreLayout`) and **:115-117** (`kCATransactionPhasePostCommit`). **[corrected: :111-117 inside :105-120, not ":105-118"]**. **No NSTimer and no new run-loop observer are needed**: `WebCore::RunLoopObserver` at **:53-63/:65-73** is already the tick and is not CA-backed. The Tiger branch calls `_willStartRenderingUpdateDisplay` then `_didCompleteRenderingUpdateDisplay` straight through → `didCompleteRenderingUpdateDisplay()` :94-98 → `schedulePostRenderingUpdate()` :100-103. Also gate `[window _enableScreenUpdatesIfNeeded]` **:193** down to a bare `flushWindowIfNeeded`. **~10 LOC.** **[corrected — the earlier plan called this "the one hard CA dependency needing a new driver"; it is a 10-line gate]** |
| `WebFullScreenController.mm` | **EXCLUDE** via `ENABLE_FULLSCREEN_API=OFF` (defaults ON, `WebKitFeatures.cmake:224`). All call sites (`WebView.mm:303`/`:8974`, `WebViewData.h:297`) are already flag-guarded. Hostile: `CGSMainConnectionID`/`CGSNewRegionWithRect`/`CGSSetWindowClipShape` **:261-266**, `[_webViewPlaceholder setLayer:[CALayer layer]]` **:228**, `CATransaction` **:453-460**, `convertPointFromScreen:` **:503,:572**. Fallback regex `mac/WebView/WebFullScreenController\.mm` |
| `WebWindowAnimation.mm` | **EXCLUDE** — `CGSMainConnectionID()` **:117**, `CGSSetWindowWarp` **:125/:160**, `CGSSetWindowAlpha` **:263**, all 10.5 CGS spellings (Tiger is `_CGSDefaultConnection()`). Only consumer is `WebFullScreenController.mm:33`. Regex `mac/WebView/WebWindowAnimation\.mm` |
| `WebImmediateActionController.mm` | **EXCLUDE** — `<QLPreviewMenuItemDelegate>` :72, `<NSImmediateActionAnimationController>` :75/:280/:310/:334, `NSImmediateActionGestureRecognizer` :83/:132/:190/:218/:233/:247/:266, `+[NSMenuItem standardQuickLookMenuItem]` :306, `QLPreviewStylePopover` :307. Gated only by `#if PLATFORM(MAC)` :28, so it always compiles. Regex `mac/WebView/WebImmediateActionController\.mm`. **Paired gates required**: `WebView.mm:1422, :9050, :9638`, `WebHTMLView.mm:5900-5920` (`_dictionaryPopupInfoForRange:` :5913, `quickLookWithEvent:` :5916), `WebViewData.h:149`, `WebViewInternal.h:109/:352` |
| `WebVideoFullscreenController.mm` | **KEEP** — whole file `#if ENABLE(VIDEO) && PLATFORM(MAC)`; `ENABLE_VIDEO=OFF` empties it. No source edit |
| `WebPDFView.mm` | **KEEP — 2 gates.** Excluding it costs edits in `WebView.mm:102/:2171-2175/:9802/:9814`, `WebFrameView.mm:49/:255-259`, `WebDataSource.mm:47/:278-282`. Two real gaps: (a) `PDFViewSPI.h:36-38` `- (PDFKitPlatformScrollView *)documentScrollView;` — that typedef is 10.7-era and absent, hard compile error, used at **:1066** → GATE to `- (NSScrollView *)documentScrollView;`, ~4 LOC; (b) `-[PDFSelection extendSelectionAtStart:]`/`extendSelectionAtEnd:` **:1230-1234** (PDFKit 10.5) → GATE **:1221-1236** so Tiger falls to `findString:fromSelection:withOptions:` at **:1237**, ~6 LOC. Plus `graphicsPort` at **:335,:348**. Everything else is PDFKit 1.0 |
| `WebPDFRepresentation.mm`, `WebPDFDocumentExtras.mm` | **KEEP — clean.** PDFKit reached via `[[WebPDFView PDFKitBundle] classNamed:@"PDFDocument"]` :56; `CGPDFDictionaryGetNameString` at `WebPDFDocumentExtras.mm:110` is a WebKit inline (`PAL/pal/spi/cg/CoreGraphicsSPI.h:582`), not a CG export |
| `WebTextCompletionController.{h,mm}` | **KEEP — RESTORE + GATE + SHIM.** `.mm:86-87` `+[NSScrollView contentSizeForFrameSize:horizontalScrollerClass:verticalScrollerClass:borderType:controlSize:scrollerStyle:]` + `NSControlSizeRegular` + `+[NSScroller preferredScrollerStyle]` → RESTORE the sibling Tiger form already in this file at **:144**, `+contentSizeForFrameSize:hasHorizontalScroller:hasVerticalScroller:borderType:`. `.mm:128` `convertPointToScreen:` → the §1.5 NSWindow shim. `.h:31` `<NSTableViewDelegate, NSTableViewDataSource>` (both 10.6 formal protocols) → GATE the protocol list off; the two data-source methods (`.mm:309`, `.mm:314`) are found via `respondsToSelector:`. ~4 LOC |
| `WebFeature.m` | **NEW** — see §0 defect 3 |
| `WebDelegateImplementationCaching.mm` | **GATE** — see §0 defect 4 |
| `WebIndicateLayer.mm` | **KEEP — clean.** Whole file `#if PLATFORM(IOS_FAMILY) && ENABLE(REMOTE_INSPECTOR)` :26-:73. **[corrected — earlier plan listed it for deletion]** |
| `WebPreferences.mm` | **KEEP — clean.** **[corrected]** Every `dispatch_queue_create`/`dispatch_sync`/`dispatch_barrier_sync`/`dispatch_once` in this file is inside `#if PLATFORM(IOS_FAMILY)` (:122-124, :128-130, :262-269, :275-281, :292-299, :377-384, :404-410, :1989-1995, :435/458/481/504/526/549/572). All 255 properties are hand-implemented. See §8 for the defaults to change |
| `WebPreferencesDefaultValues.mm` | **KEEP — clean** here. `_CFAppVersionCheckLessThan` :118-127/:141/:147 is CFPriv and present on Tiger; `linkedOnOrAfterSDKWithBehavior` :159-218 routes to `dyld_get_program_sdk_version` (10.7+) but that lives in **WTF** — flag `RuntimeApplicationChecksCocoa` to the WTF track |
| `WebDataSource.mm`, `WebResource.mm`, `WebArchive.mm`, `WebHTMLRepresentation.mm`, `WebScriptDebugDelegate.mm`, `WebScriptDebugger.mm`, `WebDocumentLoaderMac.mm`, `WebPolicyDelegate.mm`, `WebTextIterator.mm`, `WebScriptWorld.mm`, `WebNavigationData.mm`, `WebFormDelegate.m` | **KEEP — clean.** All properties hand-implemented (checked `WebDataSource.mm` 26, `WebResource.mm` :200/209/218/227/236, `WebArchive.mm` :252/269/294/315) |
| `WebNotification.mm`, `WebGeolocationPosition.mm`, `WebDeviceOrientation*.mm`, `WebMediaPlaybackTargetPicker.mm` | **KEEP — clean.** Each is fully inside its own already-OFF `ENABLE()` guard |

---

## 2. Scrollbars

`ScrollbarThemeMac` is a **WebCore** file, so the work lives in
`logs/webcore-plan.md` §4.3a, not here. Summary for cross-reference: delete
`ScrollbarThemeMac.mm:57-106,:169,:196-241,:296-312,:540-620,:655-696`; keep
`nativeTheme` :176-180, the button constants :183-189, `buttonRepaintRect`
:350-366, `backButtonRect` :368-400, `forwardButtonRect` :402-440, `trackRect`
:442-476, `handleMousePressEvent` :488-515, `scrollbarPartToHIPressedState`
:522-538 and the `AppleAquaScrollBarVariantChanged` observer already at :167;
rewrite `scrollbarThickness` onto `+[NSScroller scrollerWidthForControlSize:]`
(`sdk/MacOSX10.4u.sdk/.../NSScroller.h:70-71`); add ~150 LOC of
`HIThemeDrawTrack` + `HIThemeDrawTrackTickMarks` paint with
`HIThemeTrackDrawInfo.kind = kThemeScrollBarMedium/Small`. Drop
`ScrollbarMac.{h,mm}`, `NSScrollerImpDetails.{h,mm}`,
`ScrollbarsControllerMac.{h,mm}`, `NSScrollerImpSPI.h`,
`page/scrolling/mac/Scroller{Pair,}Mac.*`. Two gate edits: `Scrollbar.cpp:56-60`
takes the `#else`, `ScrollbarsController.cpp:34` widens so the generic no-op
controller is used.

**The WebKitLegacy side is one row**, already listed in §1.5:
`WebDynamicScrollBarsView.mm:549-550` (`momentumPhase`), SHIMMED. The file has no
`preferredScrollerStyle`, no `NSScrollerImp`, no `flashScrollers` and no
elasticity API. The other consumer is
`WebHTMLView.mm:4661` `_NSRecommendedScrollerStyle()`, gated in §1.3.

---

## 3. Pasteboard and drag

### 3.1 Pasteboard

`WebCore/platform/mac/LegacyNSPasteboardTypes.h` (99 lines) already wraps every
Tiger constant in `legacy*PasteboardTypeSingleton()` inlines at `:35-93`, and is
already `#import`ed by all ten consumers. The WebKitLegacy-side work is three
`#define`s:

| site | symbol | remedy |
|---|---|---|
| `WebHTMLView.mm:5058`, `:5265` | `NSPasteboardNameFont` (10.13) | SHIMMED — `#define NSPasteboardNameFont NSFontPboard` |
| `Misc/WebNSPasteboardExtras.mm:179` | `NSPasteboardNameFind` | `#define … NSFindPboard` |
| `Misc/WebNSPasteboardExtras.mm:269` | `NSPasteboardNameDrag` | `#define … NSDragPboard` |

All three belong in a new `compat/sdk-overlay/AppKit.framework/Headers/NSPasteboard.h`,
owned by nscompat. **~10 LOC.** The WebCore-side single-item collapse is
`webcore-plan.md` §4.3b.

**Testing caveat carried forward from `logs/appkit-probe.md`:** `NSPasteboard` is
nil for any process launched over ssh on the box (`pbs` is not in the session's
bootstrap namespace). Pasteboard verification needs a console-session launch.

### 3.2 Drag

| file | symbol + line | remedy |
|---|---|---|
| `WebCoreSupport/WebDragClient.mm` | `bool WebDragClient::useLegacyDragClient() { return false; }` **:110-113** | **GATE → `return true;` on Tiger. 1 LOC, and that is the whole fix for this file.** It routes every drag to the Tiger path automatically |
| " | `startDrag()`: `[topHTMLView dragImage:at:offset:event:pasteboard:source:slideBack:]` **:184**, delegate variant `webView:dragImage:at:offset:event:pasteboard:source:slideBack:forView:` **:178** | **RESTORE** — reached automatically once :110 returns true |
| " | `beginDrag()` **:190-209**: `[[NSDraggingItem alloc] initWithPasteboardWriter:]` **:197**, `setDraggingFrame:contents:` **:205**, `beginDraggingSessionWithItems:event:source:` **:209** | GATE `#if !PLATFORM(TIGER)` — unreachable once :110 flips, but must not compile |
| `WebView/WebHTMLView.mm` | `dragImage:at:offset:event:pasteboard:source:slideBack:` **:4244** (`ALLOW_DEPRECATED` at :4243), `draggingSourceOperationMaskForLocal:` **:4287**, `draggedImage:endedAt:operation:` **:4299** **[corrected: :4299, not :4300]**, `namesOfPromisedFilesDroppedAtDestination:` **:4339** | **RESTORE** — all four Tiger-era methods present and complete |
| " | `draggingSession:sourceOperationMaskForDraggingContext:` **:4394**, `draggingSession:endedAtPoint:operation:` **:4405** (uses `convertRectFromScreen:` :4412) | GATE **:4390-4420** out, together with `<NSDraggingSource>` at `WebHTMLViewInternal.h:46` |
| " | `[wrapper writeToURL:… options:NSFileWrapperWritingWithNameUpdating originalContentsURL:nil error:nullptr]` **:4383** (10.6) | RESTORE `[wrapper writeToFile:path atomically:YES updateFilenames:YES]` (10.0). 1 LOC |
| `WebView/WebView.mm` | the promise block **:6379-6419** | GATE — see §1.6 |

`WebCore/platform/cocoa/DragImageCocoa.mm` (337 LOC) still needs an audit for
`-drawInRect:fromRect:operation:fraction:respectFlipped:hints:` (10.6) versus
`-lockFocus` / `-compositeToPoint:operation:`. That is WebCore's file.

---

## 4. WebCoreSupport clients

### 4.1 WebFrameLoaderClient.{h,mm} — 2170 LOC

The `nsURLRequest()` / `nsURLResponse()` boundary is **13 Mac-live lines, 15
calls**: `:297`, `:306` (both), `:323` (both), `:340`, `:355`, `:371`, `:473`,
`:866`, `:906`, `:926`, `:1056`, `:1057`, `:1144`. (Four more at `:316`, `:336`,
`:367`, `:467` are inside `#if PLATFORM(IOS_FAMILY)`.)

**Keep all 13. Do not gate them.** They feed the public `WebResourceLoadDelegate`
and `WebPolicyDelegate` signatures, which are ABI, and `NSURLRequest` /
`NSMutableURLRequest` / `NSHTTPURLResponse` all exist on 10.4. The cost lands in
WebCore, not here — `platform/network/cocoa/ResourceRequestCocoa.mm` and
`ResourceResponseCocoa.mm` must keep providing these with their CFNetwork SPI
stripped. **Hand this to the WebCore track**, with the exact sites:

| file | line | symbol |
|---|---|---|
| `ResourceRequestCocoa.mm` | 51, 245, 383 | `NSURLRequest.attribution` / `NSURLRequestAttributionDeveloper` (10.15) |
| `ResourceRequestCocoa.mm` | 136, 186, 275, 278, 282, 286, 379, 394 | `-[NSURLRequest _CFURLRequest]`, `CFURLRequestGetRequestPriority`, `_CFURLRequestSetProtocolProperty`, `_CFURLRequestSetStorageSession`, `CFURLCacheCopyResponseForRequest` |
| `ResourceResponseCocoa.mm` | 84, 88, 169 | `-[NSURLResponse _CFURLResponse]`, `_CFURLResponseGetSSLCertificateContext`, `CFURLResponseGetHTTPResponse` |

The pure-Foundation subset is already Tiger-clean:
`initWithURL:statusCode:HTTPVersion:headerFields:` (`ResourceResponseCocoa.mm:71`,
10.2) and `setValue:forHTTPHeaderField:` (`ResourceRequestCocoa.mm:303/306/358`,
10.0).

| symbol + line | remedy |
|---|---|
| `handle->connection()` **:306** — `ResourceHandle::connection()` returns `NSURLConnection *` (`WebCore/platform/network/ResourceHandle.h:114`). **Confirmed absent from the curl restore**: `grep connection() refs/webkit-history/curl-resourcehandle/*` returns zero hits | **GATE** `#if !PLATFORM(TIGER)`. Tiger falls through to the same code as the no-loader path at **:297**, `[[WebDownload alloc] initWithRequest:… delegate:…]`. Loses restart-from-in-flight-load; downloads still work. ~8 LOC. `Misc/WebDownload.mm:251 -_initWithLoadingConnection:` is the only other user |
| `HAVE(APP_LINKS)` **:156-159** (`LaunchServicesSPI.h`), **:179-181**, **:187-189**, **:874**, **:1467**, **:2083**, **:2142-2147** — `_LSOpenConfiguration`, `+[LSAppLink openWithURL:configuration:completionHandler:]` (10.11) | **GATE** — `#define HAVE_APP_LINKS 0` for Tiger at `PlatformHave.h:292-294`. Removes the ivars, the alternate `-initWithFrame:…appLinkURL:referrerURL:` and the open call. **2 LOC, cleanest single change in the file** |
| `ENABLE(DATA_DETECTION)` **:512-515** — `dispatchDidFinishDataDetection(NSArray *)` body is **already empty** | GATE via `ENABLE_DATA_DETECTION 0` at `PlatformEnableCocoa.h:306`. Zero code loss. **[corrected: not a real hazard]** |
| `createPlugin` **:1713-1812** | **KEEP UNCHANGED.** `shouldBlockPlugin()` **:1708-1711** already returns `true` unconditionally, so any found package sets `errorCode = WebKitErrorBlockedPlugInVersion` (**:1778**) and the function returns `nullptr` (**:1805**). Null is safe: `WebCore/loader/SubframeLoader.cpp:434-438` does `setPluginUnavailabilityReason(PluginMissing); return false;` — no deref, no assert |
| `redirectDataToPlugin` **:1814-1830** | **KEEP** — unreachable; only called after a non-null widget, which `createPlugin` can never produce |
| `[NSURLProtocol setProperty:forKey:inRequest:]` **:360** | **KEEP** — `NSURLProtocol` is 10.2, and it is inside `WTF::MacApplication::isAppleMail()` (**:358**) so it never runs |
| `[[NSURLFileTypeMappings sharedMappings] MIMETypeForExtension:]` **:1606**, import **:130** | **KEEP** — Foundation private class, present since 10.0 |
| `USE(QUICK_LOOK)` **:1906**, `_web_removeFileOnlyAtPath:` **:1920** | **no action** — `USE_QUICK_LOOK` is `PLATFORM(IOS) \|\| PLATFORM(VISION)` only (`PlatformUse.h:158`). **[corrected]** |

### 4.2 WebChromeClient — the highest-leverage edit in the port

| file | symbol + line | remedy |
|---|---|---|
| `WebChromeClient.h` | `allowedCompositingTriggers()` **:178-189** — returns `ThreeDTransformTrigger \| VideoTrigger \| PluginTrigger \| CanvasTrigger \| AnimationTrigger`. **[corrected: body ends :189, not :188]** | **GATE** `#if PLATFORM(TIGER) return static_cast<CompositingTriggerFlags>(0);`. With `USE_CA` off this is what disables compositing everywhere and cannot be re-enabled by a runtime preference. **~4 LOC** |
| `WebChromeClient.h` | `__weak WebView *m_webView;` **:255** | **GATE** — `-fobjc-weak` is off for Tiger (`OptionsCocoa.cmake:395`) and `__weak` on the fragile runtime is a hard error. `#if PLATFORM(TIGER)` → plain `WebView *m_webView;`, the pre-ARC WebKit spelling. **~4 LOC** |
| `WebChromeClient.mm` | `attachRootGraphicsLayer` **:883-904**, `[webHTMLView attachRootLayer:graphicsLayer->platformLayer()]` **:898**, `detachRootLayer` **:900** | **GATE** the `#else` body **:886-903**; Tiger branch `ASSERT(!graphicsLayer); return;`. Pairs with the `WebHTMLView.mm:6130-6190` gate. ~4 LOC |
| `WebChromeClient.mm` | `shouldPaintEntireContents()` → `return [documentView layer];` **:878-879** | GATE → `return false;`. ~3 LOC |
| `WebChromeClient.mm` | `[NSApp _cursorRectCursor]` **:777**, decl **:163-165** | **clean** — AppKit private, present in 10.4 |

### 4.3 WebEditorClient — spellcheck off

`EditorClient` and `TextCheckerClient` are fully abstract, so every method must
exist. **Most of the list disappears with the feature macros**, because
`EditorClient.h` guards the same blocks (`USE(APPKIT)` :174-183,
`USE(AUTOMATIC_TEXT_REPLACEMENT)` :185-201) and `TextCheckerClient.h` guards
`checkTextOfParagraph` with `USE(UNIFIED_TEXT_CHECKING)` (:46-48). Flip the macro
in `PlatformUse.h` and both sides vanish together.

**Group 1 — vanishes with `USE_AUTOMATIC_TEXT_REPLACEMENT 0` (`PlatformUse.h:190-192`).**
16 virtuals, header `WebEditorClient.h:105-121`, impl `.mm:529-609`:
`showSubstitutionsPanel` (.h:106/.mm:531, `[NSSpellChecker substitutionsPanel]` :533, 10.6),
`substitutionsPanelIsShowing` (107/540), `toggleSmartInsertDelete` (108/545),
`isAutomaticQuoteSubstitutionEnabled`/`toggle…` (109/110, 550/555),
`isAutomaticLinkDetectionEnabled`/`toggle…` (111/112, 560/565),
`isAutomaticDashSubstitutionEnabled`/`toggle…` (113/114, 570/575),
`isAutomaticTextReplacementEnabled`/`toggle…` (115/116, 580/585),
`isAutomaticSpellingCorrectionEnabled`/`toggle…` (117/118, 590/595),
`isSmartListsEnabled`/`toggleSmartLists` (119/120, 600/605).

**Group 2 — vanishes with `USE_UNIFIED_TEXT_CHECKING 0` (`PlatformUse.h:186-188`).**
`checkTextOfParagraph(StringView, OptionSet<TextCheckingType>, const VisibleSelection&)`
(.h:176 / .mm:**1074-1078**) — `-[NSSpellChecker checkString:range:types:options:inSpellDocumentWithTag:orthography:wordCount:]`
**:1077** (10.6). Its helper `core(NSArray *, …)` **:997-1070** iterates
`NSTextCheckingResult *` and reads seven `NSTextCheckingType*` constants at
:1002, :1007, :1029, :1035, :1041, :1047, :1053.

**Group 3 — still pure-virtual after the macros; hand-stub these eight**
(`TextCheckerClient.h:40-55`, all unconditional):

| virtual | .h | .mm | hostile API | Tiger no-op |
|---|---|---|---|---|
| `shouldEraseMarkersAfterChangeSelection(TextCheckingType) const` | 171 | 936 | none | `return true;` |
| `ignoreWordInSpellDocument(const String&)` | 172 | 942 | `ignoreWord:inSpellDocumentWithTag:` :944 — **10.3, actually fine** | keep or `{ }` |
| `learnWord(const String&)` | 173 | 947 | `-[NSSpellChecker learnWord:]` :949 (**10.5**) | `{ }` |
| `checkSpellingOfString(StringView, int*, int*)` | 174 | 952 | `checkSpellingOfString:startingAt:language:wrap:inSpellDocumentWithTag:wordCount:` :954 — **10.0, fine** | keep, or `*loc = -1; *len = 0;` |
| `checkGrammarOfString(StringView, Vector<GrammarDetail>&, int*, int*)` | 175 | 968 | `checkGrammarOfString:…details:` :971 (**10.5**) | `*loc = -1; *len = 0;` |
| `getGuessesForWord(const String&, const String&, const VisibleSelection&, Vector<String>&)` | 181 | 1109 | `checkString:range:types:options:…orthography:wordCount:` :1118 (10.6) | `guesses.clear();` |
| `requestCheckingOfString(TextCheckingRequest&, const VisibleSelection&)` | 184 | 1272 | `requestCheckingOfString:range:types:options:inSpellDocumentWithTag:completionHandler:` :1286 (10.6) + block | `request.didCancel();` |
| `requestExtendedCheckingOfString(…)` | 185 | 1295 | forwards to the above | `{ }` |

Two of those eight are Tiger-native, so real spellcheck stays available later:
`checkSpellingOfString:startingAt:` (10.0) and `ignoreWord:inSpellDocumentWithTag:` (10.3).

**Group 4 — already trivial, no change**: `isContinuousSpellCheckingEnabled`
(.h:61/.mm:206), `toggleContinuousSpellChecking` (62/211),
`isGrammarCheckingEnabled` (59/218), `toggleGrammarChecking` (60/223),
`spellCheckerDocumentTag` (63/228). All read/write `WebView` BOOL properties.

**Group 5 — the candidates block**, `.h:187-191` / `.mm:1132-1225`.
`requestCandidatesForSelection` (.h:188/.mm:1134) uses
`requestCandidatesForSelectedRange:inString:types:options:inSpellDocumentWithTag:completionHandler:`
**:1159** (10.12) with an `NSArray<NSTextCheckingResult *> *` block parameter;
`handleRequestedCandidates` (.h:189/.mm:1168) and
`handleAcceptedCandidateWithSoftSpaces` (.h:190/.mm:1201) follow; the call site is
`respondToChangedSelection` **:371**. GATE `#if PLATFORM(MAC) && !PLATFORM(TIGER)`
on `.h:187`, `.mm:1132` and `.mm:369-372`. **Caveat:**
`handleAcceptedCandidateWithSoftSpaces` is `final` on an `EditorClient` virtual
guarded only by `PLATFORM(COCOA)` (`EditorClient.h:170-172`), so it **still needs a
`{ }` body on Tiger.**

Unrelated in the same file: `<pal/spi/cocoa/NSAttributedStringSPI.h>` **:88** and
`@interface NSAttributedString (WebNSAttributedStringDetails)` **:110**, used by
`_WebCreateFragment` :433/:466 behind `#if !PLATFORM(WATCHOS) && !PLATFORM(APPLETV)`
:428 — the **:438** `#else` branch is the ready-made Tiger fallback.
`<pal/spi/mac/NSSpellCheckerSPI.h>` **:89** drops with the gates.

Also flip `USE_AUTOCORRECTION_PANEL 0` (`PlatformUse.h:194-197`): that alone empties
`WebAlternativeTextClient.mm` (:41, :46) and the whole of `CorrectionPanel.{h,mm}`
(.h:29-57, .mm:32), which otherwise needs
`showCorrectionIndicatorOfType:…completionHandler:` :62,
`dismissCorrectionIndicatorForView:` :80,
`recordResponse:toCorrection:forWord:language:inSpellDocumentWithTag:` :86 and
`NSCorrectionIndicatorType*` :97/:107/:111 — all 10.7.

### 4.4 The rest of WebCoreSupport

`ls` shows 31 `.mm`/`.m` + 33 headers; two `.mm` are **0 bytes**.

| file | LOC | issue + line | remedy |
|---|---|---|---|
| `WebPlatformStrategies.mm` | 271 | none. 29 one-line forwards :114-267; `ENABLE(WEB_AUDIO)` :71 already off; `#import "WebPluginPackage.h"` :29 unused but harmless | **KEEP, zero work** |
| `WebFrameNetworkingContext.mm` | 106 | `<pal/spi/cf/CFNetworkSPI.h>` :41; `ensurePrivateBrowsingSession` :50-55 → `NetworkStorageSessionMap::ensureSession` | **GATE** :50-60 to no-ops. `localFileContentSniffingEnabled` :62, `scheduledRunLoopPairs` :67, `sourceApplicationAuditData` :74, `blockedError` :91, `storageSession` :96 are clean |
| `NetworkStorageSessionMap.cpp` | 109 | `WebCore::createPrivateStorageSession` **:67**, **:88**; `_CFURLStorageSessionCopyCookieStorage` **:72**, **:96**; `CookieStorageSession::createCFStorageSessionForIdentifier` **:90** | **GATE** — change `#if PLATFORM(COCOA)` at **:64** and **:80** to `… && !PLATFORM(TIGER)`. `defaultStorageSession()` :57-62 already uses the sessionID-only ctor, which `refs/webkit-history/curl-resourcehandle/NetworkStorageSessionCurl.cpp:87` provides. **2 LOC** |
| `WebJavaScriptTextInputPanel.m` | 80 | ivars in `@implementation` **:38** | **NEW** — §0 defect 2 |
| `TextIndicatorWindow.mm` | 195 | `<WebCore/WebActionDisablingCALayerDelegate.h>` **:34**, `<pal/spi/cocoa/QuartzCoreSPI.h>` **:37**, `<pal/spi/mac/NSColorSPI.h>` :38 | **EXCLUDE** `^mac/WebCoreSupport/TextIndicatorWindow\.mm`. Needs a matching gate at the `WebViewInternal`/`WebHTMLView` text-indicator call sites. A stub instead would be ~30 LOC of empty methods |
| `WebContextMenuClient.mm` | 291 | `[menu popUpMenuPositioningItem:nil atLocation:… inView:…]` **:285** (10.6) | **GATE** → `[NSMenu popUpContextMenu:menu withEvent:event forView:view]` (10.0). ~5 LOC. `<pal/spi/mac/NSSharingServicePickerSPI.h>` :60 and `NSSharingServicePickerStyleRollover` :255 are already `ENABLE(SERVICE_CONTROLS)`-gated and that is OFF. `@interface NSApplication ()` :66 is a method-only class extension, fragile-safe |
| `PopupMenuMac.mm` | 256 | `CTFontCreateUIFontForLanguage(kCTFontUIFontEmphasizedSystem/System, …)` **:90**, **:171**; `CTFontGetDescent` **:172**; `NSControlSize*` **:205/:208/:211/:214** | **SHIMMED, no work.** CT side: declared at `compat/sdk-overlay/CoreText.framework/Headers/CTFont.h:169` / `:122`, implemented via `compat/include/TigerCompat/CTCompat.h:395` / `:179`; `kCTFontUIFont*` enum at `CTCompat.h:82-101`. NSControlSize side: overlay `AppKit/NSCell.h:327-336`. The remaining risk is inside `PAL::popUpMenu` **:219** — hand to the PAL track |
| `WebAlternativeTextClient.mm` | 96 | whole body `USE(AUTOCORRECTION_PANEL)` :41,:46 | GATE via the macro |
| `CorrectionPanel.{h,mm}` | 124 | whole file `USE(AUTOCORRECTION_PANEL)` | GATE via the macro |
| `WebSelectionServiceController.mm` | 99 | `ENABLE(SERVICE_CONTROLS)` — already OFF | **no action** |
| `WebPaymentCoordinatorClient.mm` / `WebMediaKeySystemClient.mm` / `WebKitFullScreenListener.mm` / `WebGeolocationClient.mm` / `WebNotificationClient.mm` | 112/53/75/242/190 | each fully inside an already-OFF `ENABLE()` | **no action** |
| `WebApplicationCache.mm`, `WebApplicationCacheQuotaManager.mm` | **0** | empty files, not in any Sources list | **no action** (headers are still exported at `PlatformCocoa.cmake:593/810/971`) |
| `WebResourceLoadScheduler.{h,cpp,mm}` | 420/110 | `.mm` :41/:46/:57 build `NSError` via WebKit's own `_initWithPluginErrorCode:` / `_webKitErrorWithDomain:code:URL:` categories | **KEEP** all three |
| `WebVisitedLinkStore.mm`, `WebProgressTrackerClient.mm`, `WebCryptoClient.{h,mm}`, `WebValidationMessageClient.mm`, `SearchPopupMenuMac.mm`, `LegacyHistoryItemClient.mm`, `WebOpenPanelResultListener.mm`, `WebSecurityOrigin.mm`, `LegacySocketProvider.cpp`, `WebBroadcastChannelRegistry.cpp`, `WebViewGroup.mm` | 164/86/88/100/63/44/90/184/54/86/103 | none | **KEEP** |
| `WebPluginInfoProvider.mm` | 75 | **confirmed**: `pluginInfo()` :52-69 builds an empty `Vector` and returns it at :62/:64/:68 — the `BEGIN_BLOCK_OBJC_EXCEPTIONS` body never touches a plugin. Only `refreshPlugins()` **:47-50** touches `[[WebPluginDatabase sharedDatabaseIfExists] refresh]` at **:49** | **KEEP, zero work** — Plugins/ stays compiled (§7) |
| `SocketStreamHandleImplCFNet.cpp`, `WebSocketChannel.cpp`, `SocketStreamHandle*.cpp` | 736/855/79/188 | CFNetwork `CFReadStream`/`CFWriteStream` SPI | **no action** — these are in `Sources.txt`, **not** in `SourcesCMakeCocoa.txt`, so the CMake Mac build never compiles them. Verify once at configure time |

---

## 5. Compositing off — the switch, and what it implies

One edit does it: `WebChromeClient::allowedCompositingTriggers()`
(`WebChromeClient.h:178-189`) returns `0`. Traced through
`WebCore/rendering/RenderLayerCompositor.cpp`: `cacheAcceleratedCompositingFlags()`
**:610** ANDs the setting with that return into `m_hasAcceleratedCompositing`
**:644**; `canBeComposited()` **:3327** then returns false, so
`RenderLayer::ensureBacking()` **:2431** is never reached and no `GraphicsLayer` is
constructed; `enableCompositingMode()` **:593** never runs, so `m_rootContentsLayer`
is never created and `attachRootLayer()` **:5316** returns early at
`if (!m_rootContentsLayer)`. **`WebChromeClient::attachRootGraphicsLayer` is
therefore never called**, and neither is `-[WebHTMLView attachRootLayer:]`.

The gates in §1.2 and §4.2 exist only so the code does not have to compile against
a CALayer that is not there. Turning compositing back on later is
`logs/ca-hosting-design.md`, which targets a `PlatformCALayerTiger` sibling of
`PlatformCALayerCocoa` driven by a `CARenderer` in an `NSOpenGLView` — the shape
`spike/CAHost` proved on the box at 0.5–0.9 ms/frame.

---

## 6. WebInspector — off, but three files must stay

### 6.1 `LegacyWebPageInspectorController.cpp` cannot be excluded

**It will break the link, and it has nothing to do with `Page::inspectorController()`.**
Two unrelated classes:

- `WebCore::Page::inspectorController()` (`WebCore/page/Page.h:524`) returns
  `PageInspectorController&`, backed by
  `const UniqueRef<PageInspectorController> m_inspectorController` (`Page.h:1529`).
  Always constructed, lives in WebCore, unaffected by anything here.
- `LegacyWebPageInspectorController` is a WebKitLegacy-local
  `RefCountedAndCanMakeWeakPtr` (`LegacyWebPageInspectorController.h:46`) wrapping
  JSC's `InspectorTargetAgent`. Constructed at `WebView.mm:1555` and `:1826`
  (`LegacyWebPageInspectorController::create(*_private->page)`), stored as `RefPtr`
  at `WebViewData.h:332`, weak-held at `WebFrameInternal.h:102`, accessor
  `-[WebView inspectorController]` at `WebView.mm:9656`, imported by `WebFrame.mm:39`
  and `WebInspectorClient.h:29`.

**KEEP** it and `LegacyWebPageDebuggable.cpp`. Both are plain C++ over the JSC
inspector (326 / 127 LOC, zero platform API), and JSC works on Tiger.

### 6.2 `WebInspectorClient.mm` — split it, do not exclude it

`WebView.mm:1529` and `:1789` do
`pageConfiguration.inspectorBackendClient = makeUnique<WebInspectorClient>(self);`.
Null is **not** safe: `PageInspectorController.cpp:104` is
`ASSERT_ARG(inspectorBackendClient, m_inspectorBackendClient)` and `:210`
unconditionally calls `m_inspectorBackendClient->inspectedPageDestroyed()`.

Keep `WebInspectorClient` (the backend, **:96-180**, nine short methods).
**GATE `#if !PLATFORM(TIGER)` around :183-902** — `WebInspectorFrontendClient` and
`@implementation WebInspectorWindowController`, where every break lives:

| line | symbol | vintage |
|---|---|---|
| 227 | `-[NSWindow performWindowDragWithEvent:]` | 10.11 |
| 289 / 293 | `[NSAppearance appearanceNamed:NSAppearanceNameAqua / NSAppearanceNameDarkAqua]` | 10.9 / 10.14 |
| 232, 532, 541 | `[NSBundle bundleWithIdentifier:@"com.apple.WebInspectorUI"]` | returns **nil** — `Source/WebInspectorUI/` does not exist in this checkout, so `localizedStringsURL` :230 and `inspectorPagePath` :530 already return nil/empty |
| 442 | `panel.nameFieldStringValue` | 10.6 |
| 447 | `panel.directoryURL` / `URLByDeletingLastPathComponent` | 10.6 |
| 449, 455 | `NSModalResponseCancel` / `NSModalResponseOK` | 10.9 — **not currently in the overlay; add them** |
| 458 | `beginSheetModalForWindow:completionHandler:` | 10.6 |

Tiger branch: `openLocalFrontend` (**:111-128**) returns `nullptr`,
`bringFrontendToFront` (:130) and `releaseFrontend` (:177) become no-ops.
**~25 LOC.**

`cf/WebCoreSupport/WebInspectorClientCF.cpp` (116 LOC) **stays** — all
`CFPreferences*` (10.0), and it provides `sendMessageToFrontend` :66,
`inspectorAttachDisabled` :72, `inspectorStartsAttached` :87,
`createFrontendSettings` :102, all linked from the backend half.

### 6.3 mac/WebInspector/ — 5 sources, 1027 LOC

| file | in build | verdict |
|---|---|---|
| `WebInspector.mm` (188) | `SourcesCocoa.txt:173` | **KEEP** — imports only WebKit/WebCore (:29-38); `WebView.mm:84` imports `WebInspector.h`, so excluding it breaks the link |
| `WebInspectorFrontend.mm` (98) | `SourcesCMakeCocoa.txt:121` | **EXCLUDE** — thin wrapper over `WebInspectorFrontendClient`, constructed at `WebInspectorClient.mm:121` |
| `WebNodeHighlight.{h,mm}` (295) | :122 | **EXCLUDE** — `#import <QuartzCore/CALayer.h>` (.h:30), `@interface WebHighlightLayer : CALayer` (.h:46) |
| `WebNodeHighlightView.{h,mm}` (342) | :124 | **EXCLUDE** — `#import <QuartzCore/CAShapeLayer.h>` (.h:30), `layoutSublayers:(CALayer *)` (.h:49/.mm:317), `_attach:(CALayer *)` (.mm:114) |
| `WebNodeHighlighter.mm` (104) | :123 | **EXCLUDE** — imports the two above. Held as `RetainPtr<WebNodeHighlighter> m_highlighter` at `WebInspectorClient.h:107`, built at `WebInspectorClient.mm:101`, used by `highlight()` :153 / `hideHighlight()` :158 / `didSetSearchingForNode` :163 → stub those three to `{ }` and drop the member. ~8 LOC |

---

## 7. Plugins — off by behaviour, on in the build

**Do not exclude `mac/Plugins/`.** There is no NPAPI in it (no
`WebNetscapePlugin*`, no `npapi.h`); `WebPluginPackage` is the ObjC `WebPlugin`
protocol only, and `WebFrameLoaderClient.mm:1708-1711` already blocks everything.
The five files compile clean:

| file | LOC | note | verdict |
|---|---|---|---|
| `WebBasePluginPackage.mm` | 394 | `(__bridge id)CFDictionaryGetValue` :117 (valid in MRR); fast enumeration :144,:148 (SHIMMED, `NSCompat.h`) | KEEP |
| `WebPluginPackage.mm` | 139 | — | KEEP |
| `WebPluginDatabase.mm` | 468 | `@autoreleasepool` :248 (clang codegen, fragile-safe); fast enumeration :347; `contentsOfDirectoryAtPath:error:` :447 (SHIMMED) | KEEP |
| `WebPluginController.mm` | 641 | `CFSetApplyFunction((__bridge CFSetRef)…)` :362 | KEEP |
| `WebPluginContainerCheck.mm` | 192 | — | KEEP |

`WebPlugin.h:101`, `WebPluginContainer.h:68`/`:77` declare
`@property (nonatomic, readonly, strong)` — **declaration-only**, implemented by
explicit methods, so nothing asks the fragile runtime for an ivar. `strong` is
accepted under MRR.

If plugins must be excluded anyway the regex is `^mac/Plugins/` and it costs ~40
edit sites: `WebView.mm` (:105, :1680, :1709, :2181, :2221, :2382, :3663, :5149,
:5170 `-_pluginForMIMEType:`, :5175 `-_pluginForExtension:`, :5184),
`WebViewData.h` (:66, :263), `WebViewInternal.h` (:107, :265-266),
`WebHTMLView.mm` (:65, :1000, :2585, :3277, :3290, :6241),
`WebHTMLViewInternal.h` (:36, :102), `WebFrameLoaderClient.mm` (:36, :63-64,
:1609, :1619, :1649, :1656, :1708, :1749, :1754, :1765, :1781-1782),
`WebChromeClient.mm:37`, `WebHTMLRepresentation.mm:35`,
`WebPlatformStrategies.mm:29`, `WebHistoryItem.mm:44`,
`WebPluginInfoProvider.mm:28-29,:49`.

---

## 8. Printing, accessibility, Storage/History/DefaultDelegates, DOM

### 8.1 Printing — Tiger-compatible as written

The whole path is the 10.2-era `NSView` printing protocol:
`WebHTMLView.mm` `-_web_setPrintingModeRecursive:adjustViewSize:` **:1441**,
`-_web_setPrintingModeRecursive` **:1449**, `-_web_clearPrintingModeRecursive`
**:1454**, `-_web_setPrintingModeRecursiveAndAdjustViewSize` **:1459**,
`-_setPrinting:minimumPageLogicalWidth:…` **:4689** (decl **:930**),
`-adjustPageHeightNew:top:bottom:limit:` **:4727**,
`-_scaleFactorForPrintOperation:` **:4748**,
`-_provideTotalScaleFactorForPrintOperation:` **:4773**,
`-_delayedEndPrintMode:` **:4793**, plus `-knowsPageRange:` and `-rectForPage:` in
the **:4780-4900** block. `WebFrameView.mm` `-canPrintHeadersAndFooters` **:1080**,
`-printOperationWithPrintInfo:` **:1089**,
`+[NSPrintOperation printOperationWithView:printInfo:]` **:1098**,
`-documentViewShouldHandlePrint` **:1102**, `-printDocumentView` **:1111**.
`Misc/WebNSPrintOperationExtras.m` uses `NSPrintScalingFactor` **:37**, `paperSize`
and the margin accessors — **all 10.0, and `NSPrintScalingFactor` is a real Tiger
symbol** (an earlier survey flagged it in error).

One behavioural gap: `-_provideTotalScaleFactorForPrintOperation:` **:4773** is an
AppKit private callback only newer AppKit invokes. On Tiger it never fires, so
shrink-to-fit scaling is lost. Compensate by applying
`_scaleFactorForPrintOperation:` inside `beginDocument`. `CGFloat` is `float` on
i386, which the `adjustPageHeightNew:` signature already assumes.

### 8.2 Accessibility — gate, do not stub

WebKitLegacy's a11y layer is ~30 lines of forwarding, all Tiger-native informal
protocol: `WebHTMLView.mm` `-accessibilityAttributeValue:` **:4991**
(`NSAccessibilityChildrenAttribute` :4994, `super` :5000),
`-accessibilityFocusedUIElement` **:5006**, `-accessibilityHitTest:` **:5013**,
`-_accessibilityParentForSubview:` **:5027**; `WebFrame.mm`
`-accessibilityRoot` **:2197** with
`[[NSApp accessibilityAttributeValue:NSAccessibilityEnhancedUserInterfaceAttribute] boolValue]`
**:2203** (the constant is defined locally at **:209**); `WebView.mm:1032-1037`
`NSAccessibilityUnignoredDescendant` + `accessibilitySetOverrideValue:forAttribute:`.
**`WebFrameView` overrides nothing.**

The only break is `convertPointFromScreen:` at `WebHTMLView.mm:5020`, covered by
the §1.5 NSWindow shim. `WebHTMLView.mm:2523` `accessibilityRootElement` is inside
`#if PLATFORM(IOS_FAMILY)` **:2521-2528** — **[corrected: false positive]**.

The 30 missing `NSAccessibility*` string constants live on the WebCore side
(`accessibility/cocoa/CocoaAccessibilityConstants.h`); see `webcore-plan.md` §4.3c.

### 8.3 Storage, History, DefaultDelegates — one real break

**`Source/WebKitLegacy/Storage/`** (9 sources, 2578 LOC): `InProcessIDBServer.cpp`
510, `StorageAreaImpl.cpp` 307, `StorageAreaSync.cpp` 539,
`StorageNamespaceImpl.cpp` 179, `StorageSyncManager.cpp` 86, `StorageThread.cpp`
118, `StorageTracker.cpp` 634, `WebDatabaseProvider.cpp` 52,
`WebStorageNamespaceProvider.cpp` 153. **All KEEP, all clean** — WTF and WebCore
only.

**`mac/Storage/`** (6 sources, 778 LOC): `WebDatabaseManager.mm` 229
(`removeItemAtPath:error:` :126, `attributesOfItemAtPath:error:` :176,
`contentsOfDirectoryAtPath:error:` :200 — all SHIMMED by `NSCompat.h`; the
`dispatch_async` at :211 is inside `#if PLATFORM(IOS_FAMILY)`, `#endif` at :218
**[corrected: not a Mac break]**), `WebDatabaseManagerClient.mm` 221,
`WebDatabaseProvider.mm` 42, `WebDatabaseQuotaManager.mm` 73,
`WebStorageManager.mm` 137 (`removeItemAtPath:error:` :72 SHIMMED,
`NSSearchPathForDirectoriesInDomains` :102 is 10.0), `WebStorageTrackerClient.mm`
76. **All KEEP.**

**`mac/History/`** (7 sources, 3139 LOC):

| file | LOC | verdict |
|---|---|---|
| `BinaryPropertyList.cpp` | 844 | **KEEP — confirmed plain C++.** Includes `<algorithm>` :28, `<limits>` :29, **`<span>` :30 (C++20)**, `<wtf/HashMap.h>` :31, `<wtf/Hasher.h>` :32, `<wtf/StdLibExtras.h>` :33, `<wtf/text/StringHash.h>` :34. **Zero ObjC, zero CF calls.** The header pulls `<CoreFoundation/CoreFoundation.h>` (`BinaryPropertyList.h:29`) for `CFIndex`/`CFDataRef` typedefs only. The one requirement is a C++20 `<span>`, which is a libc++ question, not a Tiger one |
| `WebHistory.mm` | 886 | **GATE :542** — `[NSPropertyListSerialization propertyListWithData:options:format:error:]` is **10.6** → `+propertyListFromData:mutabilityOption:format:errorDescription:` (10.0, deprecated). ~4 LOC. `@interface WebHistory ()` :79 is a method-only class extension, fragile-safe; `-[NSData writeToURL:options:error:]` :635 is 10.4 |
| `WebHistoryItem.mm` | 565 | KEEP (`#import "WebPluginController.h"` :44 — Plugins/ stays) |
| `WebBackForwardList.mm` | 339 | KEEP |
| `BackForwardList.mm` | 261 | KEEP |
| `HistoryPropertyList.mm` | 133 | KEEP |
| `WebURLsWithTitles.m` | 111 | KEEP (compiled via `PlatformCocoa.cmake:108`) |

**`mac/DefaultDelegates/`** (4 sources, 717 LOC): `WebDefaultContextMenuDelegate.mm`
188, `WebDefaultUIDelegate.mm` 272 (`[NSApp _cycleWindowsReversed:]` :103 is AppKit
private and present on 10.4; `runModalForWindow:` :200 is 10.0),
`WebDefaultPolicyDelegate.mm` 118, `WebDefaultEditingDelegate.m` 139 (via
`PlatformCocoa.cmake:66`). **All KEEP, all clean.**

**Total real breakage across these three directories: one line.**

### 8.4 DOM/ — under 10 lines of work

131 `.mm` (19,317 LOC) + 213 `.h`. **The cleanest large directory in the port.**

`Source/WebCore/bindings/scripts/CodeGeneratorObjC.pm` is **absent from this
checkout** — `bindings/scripts/` has only `CodeGenerator.pm`, `CodeGeneratorJS.pm`
and the IDL machinery. The 344 files are **static, checked-in sources**, not
build-time output. Nothing regenerates them.

Grepped across all 344 files:

| feature | count | verdict |
|---|---|---|
| Lightweight generics (`NSArray<…> *`, `__covariant`) | **0** | clean |
| `NS_ASSUME_NONNULL` / `nullable` / `_Nullable` | **0** | clean |
| Fast enumeration | **0** | clean |
| ObjC blocks | **0** | clean |
| `@autoreleasepool` | **0** | clean |
| ARC qualifiers (`__strong`/`__weak`/`__autoreleasing`) | **0** | clean |
| `__bridge` casts | 3 | valid no-ops under MRR |
| `instancetype` | 2 — `DOMObject.h:42` (`- (instancetype)init NS_UNAVAILABLE;`), `DOMObject.mm:44` | clang keyword. Only `NS_UNAVAILABLE` needs a macro |
| ObjC literals `@[` / `@{` | 9 — `DOM.mm:570`, `DOMHTMLInputElement.mm:695`, `ExceptionHandlers.mm:51`, 6 in `DOMUIKitExtensions.mm` | lowered to `arrayWithObjects:count:` / `dictionaryWithObjects:forKeys:count:`, **both present on Tiger** per `compat/NSCOMPAT-SURVEY.md` §4 |
| `@property` declarations | **700**, all in headers | **declaration-only.** Verified on `DOMAttr.h:34-36` → `DOMAttr.mm:47/53/59/65` emits explicit `-name`, `-specified`, `-value`, `-setValue:`. Auto-synthesis is never triggered |
| `@synthesize` | **0** | confirms the above |
| Ivars in `@implementation` | 1 — `DOM.mm:211 @implementation WKQuadObject {` | inside `#if PLATFORM(IOS_FAMILY)` (**:192-240**). Not a Mac break |

**ARC vs MRR: MRR, unambiguously.** All 133 `mac/DOM/` entries in
`SourcesCocoa.txt:26-159` carry `@nonARC`; `WebKitMacros.cmake:187-214` routes them
into the non-ARC object library.

`DOMUIKitExtensions.mm` (`SourcesCocoa.txt:149`) is entirely inside one
`#if PLATFORM(IOS_FAMILY)` and compiles to nothing on Mac.

**Work: add `#define NS_UNAVAILABLE __attribute__((unavailable))` to
`compat/include/TigerCompat/FoundationCompat.h` if absent. That is all.**

### 8.5 Misc/ and Panels/ — the residue

| file | symbol + line | remedy |
|---|---|---|
| `Misc/WebKitNSStringExtras.mm` | `[nsContext CGContext]` **:79** | RESTORE `graphicsPort` (overlay `NSGraphicsContext.h:86`). `[font ascender]` **:95** / `[font descender]` **:97** are **clean** — both agree exactly with CoreText on Tiger per `logs/appkit-probe.md` |
| `Misc/WebNSPasteboardExtras.mm` | `NSPasteboardNameFind` :179, `NSPasteboardNameDrag` :269 | SHIMMED — §3.1 |
| `Misc/WebKitErrors.m` | `NSURLErrorFailingURLErrorKey` **:76**, `NSURLErrorFailingURLStringErrorKey` **:149** (both 10.6). Tiger has only `NSErrorFailingURLStringKey` | **NEW** — 2 `#define`s in `FoundationCompat.h`, ~6 LOC. The file already writes the legacy string key literally at :77. `dispatch_once` :90-91 is **SHIMMED** by `compat/dispatch/include/dispatch/dispatch.h:210-212` — **[corrected: `dispatch_*` is not missing on this port]** |
| `Misc/WebDownload.{h,mm}` | `@protocol NSURLDownloadDelegate` fwd-decl `.h:42`, `<NSURLDownloadDelegate>` `.h:78`, `WebDownloadInternal : NSObject <NSURLDownloadDelegate>` `.mm:76`, `id<NSURLDownloadDelegate>` `.mm:243`. Tiger's `NSURLDownload.h:127` declares only an **informal** `@interface NSObject (NSURLDownloadDelegate)` | **NEW** — `@protocol NSURLDownloadDelegate <NSObject> @end` in `FoundationCompat.h`, ~4 LOC |
| " | `_initWithLoadingConnection:request:response:delegate:proxy:` decl **:251**, super call **:259** | **clean** — `NSURLConnectionDelegateProxy` exists on Tiger; the SPI decl comes from `pal/spi/cocoa/NSURLDownloadSPI.h:92`. Only the §4.1 caller gate matters |
| `Misc/WebSharingServicePickerController.mm` | whole file `ENABLE(SERVICE_CONTROLS)` :28-:256 | **EXCLUDE** belt-and-braces `Misc/WebSharingServicePickerController\.mm$`; the primary fix is the flag |
| `Misc/WebNSEventExtras.m`, `WebNSViewExtras.m` | NSEventType/Mask/ModifierFlag renames | SHIMMED — overlay `NSEvent.h` |
| `Misc/WebCoreStatistics.h` | `NS_OPTIONS(NSUInteger, …)` **:82** | SHIMMED — `FoundationCompat.h:91-98` + `NSCompat.h` |
| `Misc/WebElementDictionary.mm` | `NSUInteger` :132,:157; `__bridge` :61/:67/:128/:152 | SHIMMED |
| `Misc/WebCache.mm`, `WebCoreStatistics.mm`, `WebIconDatabase.mm`, `WebKit.h`, `WebKitLogging.m`, `WebKitLogInitialization.mm`, `WebKitStatistics.m`, `WebKitVersionChecks.mm`, `WebLocalizableStrings*.mm`, `WebNSControlExtras.m`, `WebNSDataExtras.mm`, `WebNSDictionaryExtras.m`, `WebNSFileManagerExtras.mm`, `WebNSImageExtras.m`, `WebNSObjectExtras.mm`, `WebNSPrintOperationExtras.m`, `WebNSURLExtras.mm`, `WebNSURLRequestExtras.m`, `WebNSUserDefaultsExtras.mm`, `WebNSWindowExtras.m`, `WebStringTruncator.mm`, `WebUserContentURLPattern.mm` | — | **KEEP — clean.** Eight of these the earlier survey flagged in error. `WebIconDatabase.mm` is all stubs |
| `Misc/WebKitSystemBits.{h,m}` | — | **DOES NOT EXIST.** Removed upstream. **[corrected]** |
| `Panels/WebPanelAuthenticationHandler.m` | ivars in `@implementation` **:39-43** | **NEW** — §0 defect 1. `NSMapTable` alloc / `initWithKeyOptions:valueOptions:capacity:` :58-60 and `NSPointerFunctionsStrongMemory` are SHIMMED (`NSCompat.h` + `nscompat-maptable.m`) |
| `Panels/WebAuthenticationPanel.m` | `panel.sheetParent` + `endSheet:returnCode:` + `NSModalResponseCancel` **:81**, same + `NSModalResponseOK` **:97** (10.9) | **RESTORE** `[NSApp endSheet:panel returnCode:1/0]` — the Tiger-era non-sheet branch is right beside each at **:83-84** and **:99-100** |
| " | `[window beginSheet:panel completionHandler:^(NSModalResponse…)]` **:250-253** (10.9) | **RESTORE** `[NSApp beginSheet:panel modalForWindow:window modalDelegate:self didEndSelector:@selector(sheetDidEnd:returnCode:contextInfo:) contextInfo:NULL]`. The matching callback already exists at **:256** with exactly that signature — a one-line swap |
| " | `NSControlStateValueOn` **:233**, **:265** (10.13) | SHIMMED — add `#define NSControlStateValueOn NSOnState` (+Off/Mixed) to overlay `AppKit/NSCell.h`; **not currently there**. ~3 LOC |

Also referenced from `WebCoreSupport/WebSystemInterface.{h,mm}` and
`WebCoreSupport/WebViewFactory.{h,mm}` in the earlier plan — **neither exists.**
**[corrected]**

---

## 9. The JSC Objective-C API

`-[WebFrame javaScriptContext]` (`WebFrame.mm:2626-2631`) and
`-_javaScriptContextForScriptWorld:` (`WebFrame.mm:2137-2142`) are **available**,
via `#import <JavaScriptCore/JSContextInternal.h>` at `WebFrame.mm:60`.
`WebScriptWorld.mm:104-109` uses `-[JSContext JSGlobalContextRef]`. Nothing to do
in WebKitLegacy.

Two standing constraints from the JSC ObjC API work, both of which are **link
rules**, not source changes:

- **Never strip local symbols** (no `strip -x`, no `-Wl,-x`). `JSExport` recovers
  protocol ext records (`__OBJC_PROTOCOLEXT_*`) by name from the symbol table,
  because Tiger's runtime discards the pointer. Stripping turns every lookup into
  an empty result with no crash and no diagnostic.
- `NSMapTable` under `JSVirtualMachine` / `JSWrapperMap` / `JSManagedValue` asks
  for zeroing-weak keys, which the fragile runtime cannot provide. The shim backs
  them with non-retained keys, so a wrapper-cache entry outlives its key rather
  than zeroing. Documented in `compat/nscompat-maptable.m`.

---

## 10. Info.plist, exports, framework headers

| artifact | what is there | static-lib build needs | framework build would need |
|---|---|---|---|
| `WebKitLegacy-iOS.exp` | applied at `PlatformCocoa.cmake:259-261`, **inside `if (WEBKIT_SDK_IS_IOS_FAMILY)`** (238-1254) | **nothing** | There is **no Mac `.exp` in `Source/WebKitLegacy/`**. The Mac lists are one level down, `mac/WebKit.exp` and `mac/WebKit.mac.exp`, and `PlatformCocoa.cmake` references **neither**. A framework build would need a new `-exported_symbols_list` clause built from those two |
| `WebKitLegacy-iOS-unexported.exp` | present, **explicitly not applied** — see the comment at `PlatformCocoa.cmake:264-272` (`-exported_symbols_list` is a whitelist; ld errors if both are passed) | nothing | nothing |
| `mac/Info.plist` | `configure_file` → `WebKitLegacy-Info.plist` at **`PlatformCocoa.cmake:247`**, then `plutil -insert MinimumOSVersion` :248 and `UIDeviceFamily` :249, copied by `WebKitLegacy_POST_BUILD_COMMAND` :251-254. **All iOS-only** | **nothing** | a macOS `configure_file` + post-build copy, minus the two `plutil` calls |
| `WebKitLegacyPrefix.h` | applied **unconditionally** at `PlatformCocoa.cmake:4` (`WEBKIT_ADD_PREFIX_HEADER(… PREFIX_LANGUAGES CXX OBJC OBJCXX)`) | **required** — not platform-gated | same |
| framework headers (`WebKitLegacy_PUBLIC_FRAMEWORK_HEADERS` :707, unifdef migration :1084-1178, `PrivateHeaders` symlink :1180-1183, VFS overlay :1244) | all inside the iOS block | **nothing** | a macOS twin of the whole 238-1254 machinery, or `mac/migrate-headers.sh` |
| `WebKitLegacy_FORWARDED_PUBLIC_HEADERS` :1266+, `_FORWARDED_PRIVATE_HEADERS`, `_PRIVATE_FRAMEWORK_HEADERS` :1522, and the three `WEBKIT_COPY_FILES` targets :1531-1553 | **outside** the iOS block — runs on macOS | **required** if anything does `#import <WebKitLegacy/…>`, which TigerBrowser will | same |
| `mac/Resources/`, `en.lproj/` | not referenced by the macOS branch at all | **nothing** — `WebLocalizableStrings.mm` will then `NSLocalizedString`-miss and return the key, which is acceptable | a `Resources/` copy step |
| `Modules/` | `WebKitLegacy.modulemap` is **1 byte (empty)**; `.private.modulemap` 278 B; `WorkAround173516139.h` copied at :1203, iOS-only. `mac/Modules/` does not exist | **nothing** — module maps only matter for `@import`, and `-fmodules` is off | nothing |

**A static-library build needs exactly two things from this list**:
`WebKitLegacyPrefix.h` (already unconditional) and the three `WEBKIT_COPY_FILES`
header-forwarding targets (already macOS-live). Port none of the rest.

---

## 11. WebPreferences defaults for Tiger

`WebPreferences.mm` `+initialize` is `:305-359`, but almost everything now comes
from `INITIALIZE_DEFAULT_PREFERENCES_DICTIONARY_FROM_GENERATED_PREFERENCES`
(**:313**), generated by `GeneratePreferences.rb` from
`Source/WTF/Scripts/Preferences/UnifiedWebPreferences.yaml`. **Change defaults in
that YAML's `WebKitLegacy:` override branch, not in `WebPreferences.mm`.** Keys are
`#define`s in `WebView/WebPreferenceKeysPrivate.h`.

| Concern | Key (`WebPreferenceKeysPrivate.h`) | Tiger value | Why |
|---|---|---|---|
| Accelerated compositing | `WebKitAcceleratedCompositingEnabledPreferenceKey` `:98` | **`false`** | YAML default is `true`; §5 makes it moot but set it anyway so the pref agrees with reality |
| Cache model | `WebKitCacheModelPreferenceKey` `:182` | `WebCacheModelDocumentViewer` | 512 MB–2 GB boxes; the default comes from `_cacheModelForBundleIdentifier:` (:322) |
| Back/forward cache | `WebKitUsesPageCachePreferenceKey` `:82` | **`false` initially** | multiplies resident memory and hides teardown bugs; turn on once M5 is stable |
| WebGL | `WebKitWebGLEnabledPreferenceKey` `:107` | `false` | no ANGLE, no GL path |
| Accelerated 2D canvas | `WebKitAccelerated2dCanvasEnabledPreferenceKey` `:109` | `false` | |
| Web Audio | `WebKitWebAudioEnabledPreferenceKey` `:106` | `false` | |
| Offline app cache | `WebKitOfflineWebApplicationCacheEnabledPreferenceKey` `:92` | `false` | `WebKitApplicationCacheTotalQuota` is already `@0` at :324, and both `WebApplicationCache*.mm` are 0-byte files |
| Full screen | `WebKitFullScreenEnabledPreferenceKey` `:112` | `false` | pairs with `ENABLE_FULLSCREEN_API=OFF` |
| Service controls | `WebKitServiceControlsEnabledPreferenceKey` `:150` | `false` | |
| Gamepads | `WebKitGamepadsEnabledPreferenceKey` `:149` | `false` | |
| SubtleCrypto | `WebKitSubtleCryptoEnabledPreferenceKey` `:159` | `false` | **Tiger CommonCrypto is CommonDigest only** — MD2/4/5, SHA1/256/384/512, no SHA224, no `CommonCryptor`/`CCHmac`/PBKDF/`CCRandom`. Digests stay; symmetric algorithms cannot |
| Spelling / grammar | `WebContinuousSpellCheckingEnabled` `:73`, `WebGrammarCheckingEnabled` `:74` | `NO` | grammar needs 10.5 `NSSpellChecker` |
| Autocorrect and substitutions | `WebAutomaticSpellingCorrectionEnabled` `:80`, `WebAutomaticQuoteSubstitutionEnabled` `:76`, `WebAutomaticLinkDetectionEnabled` `:77`, `WebAutomaticDashSubstitutionEnabled` `:78`, `WebAutomaticTextReplacementEnabled` `:79`, `WebKitAsynchronousSpellCheckingEnabledPreferenceKey` `:113` | all `NO` | every one is 10.6 `NSSpellChecker`; §4.3 removes the code |
| Local storage / WebSQL | `WebKitLocalStorageEnabledPreferenceKey` `:61`, `WebKitDatabasesEnabledPreferenceKey` `:60`, `WebKitWebSQLEnabledPreferenceKey` `:168` | keep **on** | SQLite-backed and cheap; `mac/Storage/` is clean |
| Smart insert/delete | `WebSmartInsertDeleteEnabled` `:71` | keep `true` | |
| Private browsing | `WebKitPrivateBrowsingEnabledPreferenceKey` `:70` | `NO` (already `:321`) | and `WebFrameNetworkingContext.mm:50-60` is gated to a no-op |
| Developer extras | `WebKitDeveloperExtrasEnabledPreferenceKey` `:84` | `false` | inert once §6 lands |

There is **no font-smoothing preference any more** (`WebKitFontSmoothingLevel` was
removed upstream), and **no plugins or Java preference**. Font smoothing is moot
regardless: Tiger's `CGContextSetShouldSmoothFonts` and `SetAllowsFontSmoothing`
are no-ops in bitmap contexts, and `SetShouldAntialias` is the only knob.

---

## 12. Static versus dynamic, and the final link

`OptionsCocoa.cmake:568-596` sets every library type to `STATIC` under `TIGER`,
overriding the stock `SHARED`, and turns off `CMAKE_BUILD_WITH_INSTALL_NAME_DIR`:

```cmake
if (TIGER)
    set(bmalloc_LIBRARY_TYPE STATIC)
    set(WTF_LIBRARY_TYPE STATIC)
    set(JavaScriptCore_LIBRARY_TYPE STATIC)
    set(PAL_LIBRARY_TYPE STATIC)
    set(WebCore_LIBRARY_TYPE STATIC)
    set(WebKitLegacy_LIBRARY_TYPE STATIC)
    set(CMAKE_BUILD_WITH_INSTALL_NAME_DIR OFF)
endif ()
```

The reasoning holds: Tiger's dyld has no `@rpath`, the stock Cocoa port hardcodes
`INSTALL_NAME_DIR "/System/Library/Frameworks"` (`OptionsCocoa.cmake:619-622`),
which we must never write to on the box, and a single self-contained binary is far
easier to `scp -O`. `WEBKIT_MAX_BUNDLE_SIZE` is 16 under TIGER
(`OptionsCocoa.cmake:610-613`) because the 10.4 SDK's non-modular headers exhaust
clang's source-location space at 128.

**So there is no `WebKit.framework` dylib.** The deliverable is six archives —
`libWebKitLegacy.a`, `libWebCore.a`, `libPAL.a`, `libJavaScriptCore.a`,
`libWTF.a`, `libbmalloc.a` — linked straight into the app binary. Every
third-party dependency in `toolchain/sysroot-i386/usr/lib` is also static
(`libcurl.a`, `libssl.a`, `libcrypto.a`, `libicu{uc,i18n,data}.a`, `libsqlite3.a`,
`libxml2.a`, `libxslt.a`, `libharfbuzz.a`, `libpng16.a`, `libjpeg.a`, `libwebp*.a`,
`libz.a`), so **nothing has to ship in `Contents/Frameworks` until compositing
lands.**

### 12.1 The link line

Three rules, all of which fail **silently** if broken:

1. **`-ObjC` is mandatory.** A category in a static archive is pulled in only when
   something references a symbol in the same object file. Without it, every
   nscompat category and every `Misc/WebNS*Extras` category is absent at runtime
   with no link error and no warning. The symptom is a selector-not-found for a
   method that *is* implemented. This is why the WebKitLegacy archive in
   particular needs it: DOM classes and the `WebNS*Extras` categories are reached
   by name, never by a direct symbol reference.
2. **`-ltigercompat` must come before the compiler-rt builtins archive.** Both
   define `___eprintf`; the first wins and the second is never pulled.
3. **Never strip local symbols** (§9).

`-ObjC` force-loads whole archives, so the line then also needs `-ltigerdispatch`
and the AppKit / ApplicationServices frameworks, because `nscompat-appkit.m.o`
wants `NSColor`/`CGColorCreate` and `nscompat-operation.m.o` wants
`dispatch_get_{global,main}_queue`. `spike/run-fndbehaviour.sh` is the working
reference.

```
tiger-clang++ -o TigerBrowser.app/Contents/MacOS/TigerBrowser \
  TigerBrowser.o \
  -ObjC \
  -Wl,-force_load,$B/libWebKitLegacy.a \
  $B/libWebCore.a $B/libPAL.a $B/libJavaScriptCore.a $B/libWTF.a $B/libbmalloc.a \
  -L$S/lib -lxslt -lxml2 -licui18n -licuuc -licudata -lsqlite3 \
            -lcurl -lssl -lcrypto -lharfbuzz -ljpeg -lpng16 -lwebp -lwebpdemux -lz \
  -ltigercompat -ltigerdispatch \
  $ROOT/build/builtins-i386/libclang_rt.builtins-i386.a \
  -lc++ -lc++abi -lunwind \
  -framework Cocoa -framework ApplicationServices -framework Carbon \
  -framework Security -framework OpenGL
```

`$B` = `build/tiger-webkit`, `$S` = `toolchain/sysroot-i386/usr`. Carbon is needed
for `HIThemeDrawTrack` (§2), `EnableSecureEventInput` (§1.4) and the UTI C API that
`PasteboardMac.mm` uses. `-force_load` on `libWebKitLegacy.a` is belt-and-braces
next to `-ObjC`; measure whether `-ObjC` alone suffices before keeping it, because
it costs binary size.

**Size caveat:** `libicudata.a` alone is 32 MB. Expect a large binary and check
whether `-dead_strip` is safe here — it is **not** safe in combination with the
symbol-table requirement in §9, so measure before enabling it.

---

# TigerBrowser bundling recipe

`spike/TigerBrowser` already exists, runs on the box, and its screenshots are in
that directory. Today its `Makefile` links `-framework Cocoa -framework WebKit`
against the 10.4u SDK, so it drives **Tiger's own system WebKit**. That is the
baseline to beat.

**The published baseline is stale and must be re-measured.** The 5.3 s figure for
the 2,000,000-iteration JS loop in `spike/TigerBrowser/README.md` was taken against
the box's original WebKit 523.12 (2007). The 2026-09-20 22:00 update installed
**Safari 4.1.3, i.e. WebKit 4533.19.4**, whose JavaScriptCore is four years newer
than the one that produced that number. Re-run `testpages/script.html` before
quoting any comparison against our C-loop jsc (2.24 s on the same box).

The same update replaced CoreGraphics, CoreText, AppKit and ATS on disk, so
`sysroot/` is now stale for new disassembly — re-mirror before any further ABI
screening. Every behavioural probe was re-run and **no measured behaviour changed**
(`logs/appkit-probe.md`), so nothing in this document needs revising on that
account. One consequence worth checking rather than assuming: the README's
"HTTPS to apple.com fails through Tiger's system WebKit" observation predates the
update too, and a newer CFNetwork may behave differently. It does not change our
plan — we replace that stack with curl regardless — but do not cite the old result
as current.

**The WebKit1 API is identical between the two**, and `spike/TigerBrowser/README.md`
records that no `WebFrameLoadDelegate`, `WebPolicyDelegate` or `WebUIDelegate`
method used by the app is missing on Tiger. So relinking is a Makefile change, not
a source change.

## A. Relinking against our libraries

Static, per §12 — no framework, no `install_name_tool`, nothing in
`Contents/Frameworks`. The `Makefile` changes from

```make
CFLAGS  := -fobjc-runtime=macosx-fragile-10.4 -O0 -g -Wall
LDFLAGS := -framework Cocoa -framework WebKit
```

to a `-I` at the forwarded WebKitLegacy headers (the three `WEBKIT_COPY_FILES`
targets from §10, which land in `<build>/WebKitLegacy/Headers` and
`PrivateHeaders`) plus the §12.1 link line. Keep `-fobjc-runtime=macosx-fragile-10.4`
and MRR; the app itself needs nothing from ARC.

**One source change is likely**: `#import <WebKit/WebKit.h>` becomes
`#import <WebKitLegacy/WebKitLegacy.h>`. Check whether the forwarded-header target
also vends a `WebKit/` alias directory; if it does, even that is unnecessary.

## B. When compositing lands (M8), the dylib case

Only then does anything ship in `Contents/Frameworks`, and only one thing: the
Apple TV QuartzCore. `spike/CAHost/rebundle.sh` is the working recipe and should be
copied verbatim rather than reinvented:

```bash
cp -R atv/extracted/3.0.2/QuartzCore.framework spike/TigerBrowser/Frameworks/
find … -name .DS_Store -delete
cp -R sdk/MacOSX10.5.sdk/.../QuartzCore.framework/Versions/A/Headers \
      .../Versions/A/Headers          # the framework ships none; Leopard CA 1.x matches
ln -sf Versions/Current/Headers .../Headers
toolchain/bin/tiger-install_name_tool -id \
  '@executable_path/../Frameworks/QuartzCore.framework/Versions/A/QuartzCore' \
  .../Versions/A/QuartzCore
toolchain/bin/tiger-otool -D .../Versions/A/QuartzCore   # verify
```

Why the rename is mandatory: the stock `LC_ID_DYLIB` is exactly Tiger's own
`/System/Library/Frameworks/QuartzCore.framework/...`, and 207 of its class names
(`CIFilter`, `CIImage`, `CIContext`, …) duplicate Tiger's. Two copies in one
process is duplicate-class chaos. **Never install it into the box's `/System`.**
Tiger's dyld has no `@rpath`, which is why this is `@executable_path` and not
`@rpath`.

The CAHost `Makefile` shows the build side: `-F$(ROOT)/spike/CAHost/Frameworks`
on both the compile and the link, `-framework QuartzCore`, and
`cp -R $(FW) $(APP)/Contents/Frameworks/` before the link, with a
`tiger-otool -L | grep -i quartz` afterwards to prove the install name took.

Two CA-specific compile requirements from `NOTES.md`, both easy to forget:
`#undef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER` before importing the CA headers
at a 10.4 deployment target, and `[CATransaction flush]` plus `addUpdateRect:`
every frame.

## C. What ships in the bundle

```
TigerBrowser.app/
  Contents/
    Info.plist
    PkgInfo                         'APPL????'
    MacOS/TigerBrowser              one static binary, everything linked in
    Resources/
      cacert.pem                    see D
    Frameworks/                     EMPTY until M8, then QuartzCore.framework only
```

**ICU data needs no file.** `libicudata.a` is 32 MB of static archive built
`--disable-renaming`, linked into the binary. Note the matching global compile
definition `U_DISABLE_RENAMING=1` under TIGER — without it the bundled ICU 74
headers append `_74` to every call and nothing in the ICU 76 library answers.

**Nothing else ships.** Every dependency is static (§12).

## D. The CA bundle — a real runtime gap

curl was configured `--with-ca-bundle=$P/etc/ssl/cacert.pem`, which expands to
`toolchain/sysroot-i386/usr/etc/ssl/cacert.pem` on **this** machine. **That path
does not exist on the Tiger box**, so a default-configured curl will fail every
TLS verification there. The file is real and present here (188,900 bytes, from
`deps/src/cacert.pem`).

Two things to do:

1. Ship it: `cp toolchain/sysroot-i386/usr/etc/ssl/cacert.pem
   TigerBrowser.app/Contents/Resources/`.
2. Point WebCore at it at runtime rather than relying on the compiled-in path. The
   hook is `CurlSSLHandle::platformInitialize()`, whose PlayStation implementation
   (`Source/WebCore/platform/network/playstation/CurlSSLHandlePlayStation.cpp:33-48`)
   is the template. A `platform/network/tiger/CurlSSLHandleTiger.cpp` calls
   `setCACertPath(String&&)` (`CurlSSLHandle.h:66`) with
   `[[NSBundle mainBundle] pathForResource:@"cacert" ofType:@"pem"]`. **~30 LOC**,
   and it is the difference between HTTPS working and not.

This closes the one failure `spike/TigerBrowser/README.md` records against Tiger's
own WebKit: `https://www.apple.com/` fails provisional load with "secure connection
failed", because Tiger's CFNetwork predates SNI and TLS 1.2. Our curl 8.14 +
LibreSSL 4.1 stack already does an HTTPS GET to apple.com from the box.

## E. Info.plist

The existing `spike/TigerBrowser/Info.plist` is correct except for one key:

| key | value |
|---|---|
| `CFBundleExecutable` | `TigerBrowser` |
| `CFBundleIdentifier` | `org.webkittiger.TigerBrowser` |
| `CFBundlePackageType` | `APPL` |
| `CFBundleSignature` | `????` |
| `CFBundleInfoDictionaryVersion` | `6.0` |
| `NSPrincipalClass` | `NSApplication` |
| **`LSMinimumSystemVersion`** | **`10.4.11`** — currently `10.4`. 10.4.11 is the tested floor, and the earlier releases differ in libSystem (88.3.9 vs earlier) and ATS |

Do **not** add `NSHighResolutionCapable` (10.7), `NSSupportsAutomaticGraphicsSwitching`
(10.7), or any `LSApplicationCategoryType`. `NSApplicationActivationPolicyRegular`
is 10.6 and absent from the 10.4u SDK; the app defaults to a regular foreground app
without it.

## F. Running it on the box

Two environment facts that will otherwise waste an afternoon, both from `NOTES.md`
and `spike/TigerBrowser/README.md`:

- **A GUI process launched over ssh dies when that ssh session closes**, even under
  `nohup` — Tiger's shell has no `setsid`. Do launch, `sleep`, and
  `screencapture -x` inside **one** ssh invocation, or launch via `open` and let the
  app outlive the session.
- **`NSPasteboard` is nil over ssh** (`pbs` is not in the session's bootstrap
  namespace). Copy and paste cannot be tested that way; use a console login.

The window server, window creation and `screencapture -x` all work over ssh, and
Xcode 2.5 is installed on the box, so
`ssh tiger gdb --batch -ex run -ex bt --args /tmp/foo` is the crash-triage loop.
Our binaries carry DWARF when built `-g`.

```bash
scp -O -r build/TigerBrowser.app tiger:/Users/shg/
ssh tiger '/Users/shg/TigerBrowser.app/Contents/MacOS/TigerBrowser \
    "file:///Users/shg/TigerBrowser-testpages/index.html" > /tmp/tb.log 2>&1 & \
    sleep 5; screencapture -x /tmp/tb.png'
scp -O tiger:/tmp/tb.png .
```

`spike/TigerBrowser/testpages/` already holds five pages exercising CSS layout,
PNG and JPEG decode, native form controls, a JS timing loop and an HTTPS link.
They are the M5 and M6 acceptance set.

---

## Work summary, ranked

| # | item | est. |
|---|---|---|
| 1 | `WebEditorClient` spellcheck-off: 3 macro flips + 8 hand-stubs + the candidates gate | ~120 LOC |
| 2 | `WebInspectorClient.mm` gate at :183-902 + 4 backend stubs + the 4 highlight exclusions | ~35 LOC |
| 3 | `WebHTMLView.mm` CA gates (:74, :702-716, :3776-3784, :3957-3968, :6130-6190) + `WebHTMLViewInternal.h` | ~25 LOC |
| 4 | `WebHTMLView.mm` text input: 5 gates + `_updateSecureInputState` | ~20 LOC |
| 5 | `WebFeature.m` ivars + `@synthesize` | ~18 LOC |
| 6 | `WebPDFView.mm` 2 gates | ~10 LOC |
| 7 | `WebViewRenderingUpdateScheduler.mm` gate | ~10 LOC |
| 8 | `WebDragClient.mm` `useLegacyDragClient()` + `beginDrag` gate | ~16 LOC |
| 9 | `CurlSSLHandleTiger.cpp` (bundle-relative CA path) | ~30 LOC |
| 10 | `AppKitCompat.h`: `-[NSWindow convertPoint{To,From}Screen:]` | ~6 LOC |
| 11 | overlay `AppKit/NSPasteboard.h` (3 names) + `NSCell.h` `NSControlStateValue*` + `NSModalResponse*` | ~15 LOC |
| 12 | `FoundationCompat.h`: `NSURLErrorFailingURL*ErrorKey`, `@protocol NSURLDownloadDelegate`, `NS_UNAVAILABLE` | ~12 LOC |
| 13 | `WebPanelAuthenticationHandler` + `WebJavaScriptTextInputPanel` ivar moves | ~12 LOC |
| 14 | `WebAuthenticationPanel.m` sheet API restores | ~8 LOC |
| 15 | `WebFrameLoaderClient.mm:306` `handle->connection()` gate | ~8 LOC |
| 16 | `WebChromeClient` triggers + `__weak` + `attachRootGraphicsLayer` + `shouldPaintEntireContents` | ~15 LOC |
| 17 | `WebContextMenuClient.mm:285`, `WebClipView.mm:131-135`, `WebTextCompletionController` | ~14 LOC |
| 18 | `WebDelegateImplementationCaching.mm:81-83` `objc_msgSend_fpret` | ~4 LOC |
| 19 | `NetworkStorageSessionMap.cpp` 2 `#if` edits, `WebHistory.mm:542`, `WebFrame.mm:671` graphicsPort | ~8 LOC |
| 20 | CMake: 8 exclude regexes + the feature flips (`HAVE_APP_LINKS 0`, `ENABLE_DATA_DETECTION 0`, `ENABLE_POINTER_LOCK OFF`, `ENABLE_FULLSCREEN_API OFF`, `HAVE_TRANSLATION_UI_SERVICES 0`, `USE_AUTOCORRECTION_PANEL 0`, `USE_AUTOMATIC_TEXT_REPLACEMENT 0`, `USE_UNIFIED_TEXT_CHECKING 0`) | ~20 lines |
| 21 | TigerBrowser Makefile relink + `cacert.pem` + `LSMinimumSystemVersion` | ~20 lines |

**Total: roughly 430 LOC of edits plus 40 lines of build configuration**, against
the ~72k LOC of WebKitLegacy/mac that is being kept. The WebCore-side prerequisite
that gates all of it is Tiger-gating `ResourceRequestCocoa.mm` and
`ResourceResponseCocoa.mm` down to their Foundation subset (§4.1) — that is the
real blocker, and it belongs to the WebCore track.
