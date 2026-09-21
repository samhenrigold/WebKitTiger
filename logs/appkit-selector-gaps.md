# AppKit/Foundation selectors WebKit's Mac code sends that Tiger does not implement

A *starting list*, not a work queue. Produced after nscompat's observation that "a probe finds the
first gap and a grep of the call sites finds the rest, and neither substitutes for the other" — my
AppKit probe found `-[NSScreen backingScaleFactor]` by asking for it, and missed
`-[NSScreen safeAreaInsets]` because it never asked. This is the grep half.

## Method

1. Every selector Tiger's own AppKit and Foundation implement, from the ObjC method lists in both
   binaries: `tiger-otool -arch i386 -oV`, 15,869 selectors.
2. Every zero-argument and keyword message WebKit's Mac sources send to a receiver that looks like
   an AppKit or Foundation object (an `NS`-prefixed class literal, a bracketed `NS...` expression,
   or a local named `ns*`). 406 files across `WebCore/platform/{mac,cocoa,graphics/mac}`,
   `WebCore/{page,editing}/{mac,cocoa}` and `WebKitLegacy/mac`, excluding anything iOS-shaped.
3. Subtract selectors WebKit itself defines, and selectors `compat/` already shims.

353 selectors sent to NS receivers; 48 left after all three filters.

## Limitations, which matter before anyone works the list

- **Heuristic receivers.** A send through a variable not named `ns*` is missed entirely, and some
  hits are not AppKit at all: `removeFromSuperlayer`, `setAnchorPoint:` and `valueWithCATransform3D:`
  are CoreAnimation, which the atv track covers separately.
- **No gate awareness.** It does not know which sites sit behind a `HAVE()` or `ENABLE()` that is
  off for this port. That is the first filter to apply, and the HDR case shows why: two NSScreen
  accessors looked like shim candidates and turned out to want a gate instead.
- **No build awareness.** Some files may not be compiled for this port at all.
- Only zero-argument and keyword sends of at most four parts are matched.

So: triage it, do not implement it. The value is that the next gap gets found by reading rather
than by a compile or a runtime failure.

## The list

- `currentDrawingAppearance` — 12 site(s), e.g. `WebCore/platform/graphics/mac/AppKitControlSystemImage.mm`
- `systemUptime` — 3 site(s), e.g. `WebCore/platform/cocoa/PlaybackSessionModelMediaElement.mm`
- `accessibilityDisplayShouldDifferentiateWithoutColor` — 2 site(s), e.g. `WebCore/platform/graphics/mac/controls/ControlMac.mm`
- `accessibilityDisplayShouldIncreaseContrast` — 2 site(s), e.g. `WebCore/platform/graphics/mac/controls/ControlMac.mm`
- `disableSuddenTermination` — 2 site(s), e.g. `WebCore/platform/mac/SuddenTermination.mm`
- `dismissCorrectionIndicatorForView:` — 2 site(s), e.g. `WebCore/editing/cocoa/AlternativeTextUIController.mm`
- `enableSuddenTermination` — 2 site(s), e.g. `WebCore/platform/mac/SuddenTermination.mm`
- `graphicsContextWithCGContext:flipped:` — 2 site(s), e.g. `WebCore/platform/mac/LocalCurrentGraphicsContextMac.mm`
- `mainRunLoop` — 2 site(s), e.g. `WebCore/platform/mac/ScrollbarsControllerMac.mm`
- `menuTypeForEvent:` — 2 site(s), e.g. `WebCore/platform/mac/PlatformEventFactoryMac.mm`
- `preferredScrollerStyle` — 2 site(s), e.g. `WebCore/platform/mac/NSScrollerImpDetails.mm`
- `pressedMouseButtons` — 2 site(s), e.g. `WebCore/platform/mac/PlatformEventFactoryMac.mm`
- `propertyListWithData:options:format:error:` — 2 site(s), e.g. `WebKitLegacy/mac/History/WebHistory.mm`
- `substitutionsPanel` — 2 site(s), e.g. `WebKitLegacy/mac/WebCoreSupport/WebEditorClient.mm`
- `accessibilityDisplayShouldInvertColors` — 1 site(s), e.g. `WebCore/platform/mac/PlatformScreenMac.mm`
- `accessibilityDisplayShouldReduceMotion` — 1 site(s), e.g. `WebCore/platform/mac/ThemeMac.mm`
- `appearanceNamed:` — 1 site(s), e.g. `WebKitLegacy/mac/WebCoreSupport/WebInspectorClient.mm`
- `beginActivityWithOptions:reason:` — 1 site(s), e.g. `WebCore/platform/mac/UserActivityMac.mm`
- `beginGrouping` — 1 site(s), e.g. `WebKitLegacy/mac/WebView/WebVideoFullscreenController.mm`
- `blockQuoteIntentWithIdentity:nestedInsideIntent:` — 1 site(s), e.g. `WebCore/editing/cocoa/NodeHTMLConverter.mm`
- `calendarWithIdentifier:` — 1 site(s), e.g. `WebKitLegacy/mac/History/WebHistory.mm`
- `contextualMenuCursor` — 1 site(s), e.g. `WebCore/platform/mac/CursorMac.mm`
- `deletesAutospaceBeforeString:language:` — 1 site(s), e.g. `WebKitLegacy/mac/WebView/WebHTMLView.mm`
- `discardMarkedText` — 1 site(s), e.g. `WebKitLegacy/mac/WebCoreSupport/WebEditorClient.mm`
- `dragCopyCursor` — 1 site(s), e.g. `WebCore/platform/mac/CursorMac.mm`
- `drawFocusRingMaskWithFrame` — 1 site(s), e.g. `WebCore/platform/graphics/mac/controls/ControlMac.mm`
- `endActivity:` — 1 site(s), e.g. `WebCore/platform/mac/UserActivityMac.mm`
- `endGrouping` — 1 site(s), e.g. `WebKitLegacy/mac/WebView/WebVideoFullscreenController.mm`
- `grammarCheckingEnabled` — 1 site(s), e.g. `WebKitLegacy/mac/WebView/WebView.mm`
- `initWithCGImage:size:` — 1 site(s), e.g. `WebKitLegacy/mac/WebView/WebView.mm`
- `initWithInteger:` — 1 site(s), e.g. `WebKitLegacy/mac/WebCoreSupport/PopupMenuMac.mm`
- `initWithPasteboardPropertyList:ofType:` — 1 site(s), e.g. `WebCore/platform/mac/PlatformPasteboardMac.mm`
- `isAutomaticTextCompletionEnabled` — 1 site(s), e.g. `WebKitLegacy/mac/WebView/WebView.mm`
- `propertyListFromData:` — 1 site(s), e.g. `WebKitLegacy/mac/WebView/WebArchive.mm`
- `removeFromSuperlayer` — 1 site(s), e.g. `WebCore/platform/cocoa/VideoFullscreenCaptions.mm`
- `removeMonitor:` — 1 site(s), e.g. `WebKitLegacy/mac/WebView/WebHTMLView.mm`
- `requestBubbleClosureUnanchorOnFailure:` — 1 site(s), e.g. `WebKitLegacy/mac/WebView/WebImmediateActionController.mm`
- `serviceRolloverButtonCellForStyle:` — 1 site(s), e.g. `WebCore/platform/graphics/mac/controls/ControlFactoryMac.mm`
- `setAnchorPoint:` — 1 site(s), e.g. `WebCore/platform/cocoa/VideoFullscreenCaptions.mm`
- `setCurrentAppearance:` — 1 site(s), e.g. `WebCore/platform/mac/LocalDefaultSystemAppearance.mm`
- `setDocumentView` — 1 site(s), e.g. `WebKitLegacy/mac/WebView/WebPDFView.mm`
- `standardQuickLookMenuItem` — 1 site(s), e.g. `WebKitLegacy/mac/WebView/WebImmediateActionController.mm`
- `standardShareMenuItemForItems:` — 1 site(s), e.g. `WebKitLegacy/mac/WebView/WebHTMLView.mm`
- `updatePanels` — 1 site(s), e.g. `WebKitLegacy/mac/WebView/WebView.mm`
- `updateSpellingPanelWithGrammarString:detail:` — 1 site(s), e.g. `WebKitLegacy/mac/WebCoreSupport/WebEditorClient.mm`
- `valueWithCATransform3D:` — 1 site(s), e.g. `WebCore/page/cocoa/WebTextIndicatorLayer.mm`
- `valueWithCGRect:` — 1 site(s), e.g. `WebCore/editing/cocoa/DictionaryLookup.mm`
- `writeToURL:options:originalContentsURL:error:` — 1 site(s), e.g. `WebKitLegacy/mac/WebView/WebHTMLView.mm`
