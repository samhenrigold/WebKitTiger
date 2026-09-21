# TigerBrowser

The 32-bit UI-process app shell for the WebKit2-shaped split (see
`NOTES.md` "DIRECTION SET BY THE USER" and "WEBKIT2 SPLIT SURVEY"). It used
to host a classic WebKit1 `WebView`; **2026-09-20 it was merged with
`spike/CAHost`'s Core Animation compositor host** and now hosts a
`TigerPageView` (CARenderer over an `NSOpenGLView`, same phase-2 tiled
CALayer page as CAHost) instead. No real web content and no 64-bit content
process are wired up yet — the page view paints a stub placeholder (a
checkerboard tile grid + a banner showing the current URL) so the window's
scrolling, resizing, hit-testing, menus and keyboard access can all be
exercised and built on before the content process exists.

## Structure

One file, `TigerBrowser.m`, no nib, MRR, fragile ObjC runtime. Classes, top
to bottom in the file:

- **`checkFrameworksLoaded()`** — a plain C function, not a class. Walks
  `_dyld_image_count()`/`_dyld_get_image_name()` at launch and aborts loudly
  (`NSRunAlertPanel` + `exit(1)`) if the bundled, decollided
  `Contents/Frameworks/QuartzCore.framework` isn't among the loaded images.
  Called first thing in `main()`, before any window is built.
- **`TigerPageView : NSOpenGLView <NSTextInput>`** — the compositor host.
  Owns the `CARenderer`, the `viewport`/`page` `CALayer` pair (`viewport` is
  `masksToBounds` + `geometryFlipped`, matching CAHost), a manual grid of
  256px tile `CALayer`s (`buildTileGrid`/`updateTiles`, `TILE_SIZE`/
  `PAGE_WIDTH`/`PAGE_HEIGHT` `#define`s at the top of the file), the
  `-drawLayer:inContext:` delegate that calls `paintStubPage()`, scrolling
  (`-setScrollY:`, `-scrollerAction:`, `-scrollWheel:`), hit-testing
  (`-mouseDown:` logs view point → page point → hit layer name), and the
  Edit-menu responder-chain actions (`-cut:`/`-copy:`/`-paste:`/
  `-selectAll:`/`-undo:`/`-redo:`, logging via `-logEditCommand:` — no real
  text model exists yet). Also implements the full `NSTextInput` protocol
  (`-keyDown:` → `-interpretKeyEvents:`, `-doCommandBySelector:`,
  `-insertText:`, `-setMarkedText:selectedRange:`, `-unmarkText`,
  `-hasMarkedText`, `-markedRange`, `-selectedRange`,
  `-firstRectForCharacterRange:`, `-characterIndexForPoint:`,
  `-conversationIdentifier`, `-validAttributesForMarkedText`,
  `-attributedSubstringFromRange:`) — see "Text input spike" below for the
  design and findings. `-startTextInputSelfTest`/`-fireNextSelfTestEvent:`
  drive a scripted sequence of synthetic key events for exercising it
  without interactive input.
- **`TigerFindBar : NSView`** — a small view (label + `NSTextField` + Done
  button) that `TigerBrowserController` shows/hides for Cmd-F. Its field's
  action (`-performFind:` on the controller) just logs the search text.
- **`TigerBrowserController`** — owns the window and all the chrome: the
  toolbar row (back/forward/reload buttons, address field), the find bar,
  the status field, the `TigerPageView` + its `NSScroller` sibling (an
  `NSOpenGLView`'s surface composites over whatever's under it, so per CAHost
  the scroller can't overlap it — it's a separate view docked to the right
  edge), and a simple in-memory `_history`/`_historyIndex` array so
  back/forward/reload have real (if stub) behavior to drive. Also owns the
  View-menu stub actions (`-zoomActualSize:`/`-zoomIn:`/`-zoomOut:`, logged
  only). Sets up the full-keyboard-access tab chain explicitly
  (`-setNextKeyView:` across back → forward → reload → address field → page
  view → scroller → back), since Tiger's `NSWindow` doesn't infer one from
  view geometry.
- **`TigerBrowserAppDelegate`** — thin: builds the controller, forwards
  `openLocation:`/`performFindPanelAction:`/zoom actions from the menu bar
  to it.
- **`main()`** — `NSApplication` setup, the frameworks check, then builds
  the menu bar by hand: File (Open Location, Cmd-L), Edit (Undo/Redo/Cut/
  Copy/Paste/Select All, all target-`nil` so they dispatch through the
  responder chain to `TigerPageView`, plus Find..., Cmd-F), View (Actual
  Size/Zoom In/Zoom Out).

Shared helpers near the top of the file (`makeColor`, `makeCheckerImage`,
`paintStubPage`) are adapted directly from `spike/CAHost/CAHost.m`'s phase-2
code — same technique, trimmed of CAHost's video/animation/auto-scroll
benchmark extras that don't belong in the shell.

## Files

- `TigerBrowser.m` — the whole app.
- `Makefile` — builds `build/TigerBrowser.app`. Depends on
  `spike/CAHost/Frameworks/QuartzCore.framework` (runs CAHost's
  `rebundle.sh` if it isn't already built) and copies it into
  `Contents/Frameworks/`, matching CAHost's install-name convention
  (`@executable_path/../Frameworks/QuartzCore.framework/...`). Links
  `-framework Cocoa -framework OpenGL -framework ApplicationServices
  -framework QuartzCore` (from the bundled framework's `-F` search path) —
  no more `-framework WebKit`.
- `Info.plist` — bundle metadata, `LSMinimumSystemVersion` 10.4, unchanged.
- `testpages/` — left over from the WebView era (see "Archived" below); not
  used by the current shell, kept for whenever real content loading returns.
- `textinput-selftest.log` — full transcript from a run of the text-input
  self-test on the box (`TIGERBROWSER_TEXTINPUT_TEST=1`); see "Text input
  spike" below.

## Build

```
cd spike/TigerBrowser
make
```

`tiger-otool -L` on the built binary confirms the private install name:

```
@executable_path/../Frameworks/QuartzCore.framework/Versions/A/QuartzCore (compatibility version 1.2.0, current version 1.6.0)
```

## Running on the box

```
scp -O -r build/TigerBrowser.app tiger:/Users/shg/
ssh tiger '/Users/shg/TigerBrowser.app/Contents/MacOS/TigerBrowser \
    "https://example.com/test-page" > /tmp/tb.log 2>&1 & sleep 3; screencapture -x /tmp/tb.png'
```

## Screenshot

`screenshot-shell.png` (2026-09-20, on the 10.4.11 box): toolbar with back/
forward/reload buttons and the loaded URL in the address field, window title
following the URL, the `TigerPageView` showing the checkerboard tile grid
with the blue URL banner at the top of the page, the `NSScroller` sibling on
the right, and the status field at the bottom reading
`Loaded (stub): https://example.com/test-page`. (The crash-report dialog
visible in the screenshot is an unrelated teammate's `CAVideo` test running
concurrently on the same shared box — not TigerBrowser.)

## What works

- Frameworks-loaded check passes: the bundled, decollided QuartzCore is
  correctly bound (confirmed via the `_dyld_get_image_name` dump this check
  logs). Tiger's *system* QuartzCore is also loaded in the same process —
  apparently a transitive AppKit dependency, not something this app links
  directly — but since `decollide.py` already renamed the private copy's
  `CI*` Core Image class names to `ZI*`, the two coexisting doesn't collide
  (this app never touches Core Image/QuickTime anyway).
- CARenderer hosts the tiled page exactly as in CAHost phase 2: 48 tiles
  (4x12 at 256px) painted on demand via `-drawLayer:inContext:`, correct
  `geometryFlipped` top-left page coordinates, checkerboard + URL banner
  render correctly on screen.
- Toolbar, address bar (loads on Enter), status bar, window title tracking,
  back/forward/reload against the in-memory history stack, Cmd-L to focus
  the address field, Cmd-F to open/close the find bar, Edit/View menus all
  built and wired.
- No crashes, no missing selectors, no fragile-ABI issues merging CAHost's
  CA-hosting code into the browser-chrome shell.

## Known rough edges

- Interactive verification of scrolling/hit-testing via UI-scripted clicks
  (`osascript`/System Events) was unreliable on the shared Tiger box during
  this session — a teammate's concurrently-running `CAWidgets`/`CAVideo`
  test windows kept stealing frontmost/key-window status between my
  `activate` and `click` calls, and stderr redirected to a file on the box
  is fully buffered (not line-buffered), so `-mouseDown:`'s hit-test log
  didn't appear until process exit. The scrolling/hit-testing code itself is
  the same method bodies as CAHost's own already-validated implementation
  (`-setScrollY:`, `-scrollerAction:`, `-mouseDown:`), not new code, so this
  is a test-environment contention issue on a shared box, not a sign the
  functionality doesn't work — but it wasn't independently re-screenshotted
  mid-scroll in this session. Worth a clean re-verification pass once the
  box isn't shared with concurrent GUI tests.
- Back/forward/reload only manipulate the address bar, window title and page
  banner text (via the stub history array) — there's no real navigation or
  content yet, by design (that's the 64-bit content process's job later).

## Text input spike (2026-09-20, logs/textinput-plan.md)

`TigerPageView` now adopts Tiger's `NSTextInput` informal protocol (declared
in `NSInputManager.h`, not `NSTextInputClient` — that's 10.6+) and logs
every call instead of building the `Vector<KeypressCommand>`/sending IPC the
real UI process eventually will. Logging goes to a real file opened with
`setvbuf(..., _IOLBF, 0)` (`tiLogOpen()`/`tiLog()` near the top of the file,
default path `/tmp/tigerbrowser-textinput.log`, override with
`TIGERBROWSER_TEXTINPUT_LOG`) — stderr piped over ssh on this box is fully
block-buffered, which silently dropped log lines in an earlier spike (the
scrolling section above); a dedicated line-buffered file sidesteps that
entirely.

**EditorState cache, modeling the plan's §3 flagged design problem.**
`TigerPageView` keeps two copies of a tiny fake document (`selectedRange`,
`markedRange`, a document string): `_pending*` (what this keydown's
`insertText:`/`setMarkedText:`/`unmarkText` calls have produced so far) and
`_applied*` (what the four synchronous query methods —
`hasMarkedText`/`markedRange`/`selectedRange`/`firstRectForCharacterRange:`
— actually answer from). `_pending*` only overwrites `_applied*` after a
`performSelector:withObject:afterDelay:0.05` — a deliberately simulated
content-process IPC round trip. `doCommandBySelector:` deliberately does
**not** mutate either copy (per the task: "record... do not execute" — the
real content process's `WebCore::Editor` would own that). This reproduces,
in miniature, the exact staleness the plan's §3 identifies as its single
highest-risk finding: a query made before the simulated round trip lands
sees the pre-keystroke state, on purpose.

**No way to type over ssh**, so `TigerPageView startTextInputSelfTest`
(gated behind `TIGERBROWSER_TEXTINPUT_TEST=1`, fires 1.5s after the window
appears) drives a scripted sequence of synthetic `NSEvent`s built with
`+[NSEvent keyEventWithType:...]` straight into `-keyDown:`, in-process — no
`osascript`/System Events UI-scripting and no window-focus dependency,
avoiding the shared-box contention noted above entirely. Full transcript:
`textinput-selftest.log` (checked in). Screenshot: `screenshot-textinput.png`.

### Command vocabulary observed vs. the plan's expectations

Matches the plan closely, once the synthetic events were built correctly
(see "Tiger quirks" below):

| Input | `NSTextInput` call(s) |
|---|---|
| Plain character ("H", "i") | `insertText:` with a plain `NSString` |
| Return | `doCommandBySelector: insertNewline:` |
| Tab | `doCommandBySelector: insertTab:` |
| Escape | `doCommandBySelector: cancelOperation:` |
| Shift-Left/Right | `moveLeftAndModifySelection:` / `moveRightAndModifySelection:` |
| Option-Left/Right | `moveWordLeft:` / `moveWordRight:` |
| Cmd-Left/Right | `moveToBeginningOfLine:` / `moveToEndOfLine:` |
| Delete (backspace) | `deleteBackward:` |
| Forward Delete | `deleteForward:` |
| Ctrl-A / Ctrl-E (Emacs) | `moveToBeginningOfParagraph:` / `moveToEndOfParagraph:` |
| Ctrl-K (Emacs) | `deleteToEndOfParagraph:` |
| Ctrl-D (Emacs) | `deleteForward:` — same selector the physical Forward Delete key produces |

Every one of these arrived as `doCommandBySelector:` with the exact
`NSStringFromSelector` names `WebHTMLView.mm`'s existing implementation
already expects per `webkitlegacy-plan.md` §1.4 — nothing unrecognized, no
Tiger-specific renamed selector. `hasMarkedText`/`markedRange`/
`selectedRange`/`firstRectForCharacterRange:` were never called during any
of this — they're IME-candidate-window-driven, not typing-driven, so a
plain US-layout sequence with no live composition session never exercises
them; consistent with the plan's §1b framing that they're an independent
concern from the per-keystroke flow.

### Tiger quirks and gotchas found

- **Synthetic `NSEvent`s for arrow/function keys need the real
  Unicode private-use glyph in `characters`, not an empty string.** The
  first pass of this self-test passed `characters:@""` for the arrow keys
  and Forward Delete, and Tiger's `interpretKeyEvents:` responded by calling
  `insertText:""` for *all* of them — Shift/Option/Cmd-arrows included —
  instead of any movement/selection selector, and Cmd-arrows produced no
  callback at all. Fixed by supplying `NSLeftArrowFunctionKey`/
  `NSRightArrowFunctionKey`/`NSDeleteFunctionKey` (the `0xF700`-range
  OpenStep private-use constants from `NSEvent.h`) as the event's
  `characters`/`charactersIgnoringModifiers`, after which every arrow/delete
  combination produced the correct selector (table above). Conclusion:
  `NSInputManager`'s `interpretKeyEvents:` dispatches from the **already-
  resolved** `characters` string (the same string a live keyboard-layout
  translation would have produced before AppKit ever builds the `NSEvent`)
  — it does not itself re-derive a command from `keyCode` + modifier flags.
  A hand-built `NSEvent` has to supply that resolved string explicitly.
- **The dead-key test (Option-E then E, expected to compose é via
  `setMarkedText:`) did not produce marked text.** Both key presses came
  through as plain `insertText:` calls (an empty string for the Option-E
  dead-key event, then `"e"`) — no `setMarkedText:selectedRange:`, no
  composition session. Given the quirk above, this makes sense: a real dead
  key's marked-text behavior comes from the system's keyboard-layout/TSM
  machinery resolving `keyCode 14 + NSAlternateKeyMask` against the current
  input source's dead-key table — something that happens **before** an
  `NSEvent` reaches `-keyDown:` on real hardware, not something
  `interpretKeyEvents:` re-derives from a synthetic event's `keyCode` alone.
  Reproducing genuine dead-key/IME composition would need events built the
  way the team lead's alternate suggestion described — `CGEventPost`/
  `CGEventCreateKeyboardEvent` through the actual HID event tap — which
  goes through the real TSM pipeline but reintroduces the frontmost-window/
  shared-box focus contention this in-process approach was chosen to avoid.
  Flagged, not resolved, in this spike.
- **`NSTextInput`'s `aString` parameter is genuinely polymorphic** — the
  protocol comment says `insertText:`/`setMarkedText:` can receive either an
  `NSString` or `NSAttributedString`; this self-test only ever observed
  plain `NSString` (no live IME session ever ran, matching the point
  above), so the `NSAttributedString` branch in `-tiDescribeString:` is
  exercised by inspection of the protocol contract, not confirmed live in
  this session.
- **`NSMarkedClauseSegmentAttributeName`/`NSTextAlternativesAttributeName`/
  `NSTextInsertionUndoableAttributeName`** (the three attribute constants
  `webkitlegacy-plan.md` §1.4 already flagged as 10.5+-only) are confirmed
  absent from the 10.4u SDK's `NSAttributedString.h` by direct grep, not
  just assumed — `validAttributesForMarkedText` returns only
  `NSUnderlineStyleAttributeName` (10.0+, confirmed present) as a result.
- **No async escape hatch exists anywhere in this protocol** — every one of
  the four query methods is a synchronous return, confirmed directly against
  `NSInputManager.h`'s declarations (`conversationIdentifier` even returns
  bare `long`, not `NSInteger`, since `NSInteger` postdates Tiger). This
  matches the plan's §3 finding exactly and is why the cache design above
  exists at all.

## Archived: the WebView1 era (superseded 2026-09-20)

Before this merge, `TigerBrowser.m` hosted a classic WebKit1 `WebView`
against Tiger's system `WebKit.framework` and could actually load pages.
That work is superseded but the findings remain useful background:

- `screenshot.png`, `screenshot-layout.png`, `screenshot-images.png`,
  `screenshot-form.png` — WebView1 rendering local test pages (CSS layout,
  PNG/JPEG, native Aqua form controls) correctly on Tiger's original 2007
  system WebKit.
- `screenshot-js.png`, `screenshot-js-run{1,2,3}.png` — a 2,000,000-iteration
  JS loop took **5298 ms** on Tiger's original non-JIT WebKit, and **~59 ms**
  (59/62/56 ms across three runs) after the user installed Safari 4.1.3,
  which upgraded `/System/Library/Frameworks/WebKit.framework` to 533.19.4
  (first JIT-capable JavaScriptCore on this box) — roughly a 90x speedup,
  and the number that flips the comparison against the project's
  interpreter-only C-loop jsc build (2.24s on the same loop).
- `screenshot-https.png` — `https://www.apple.com/` fails ("secure
  connection failed") through Tiger's system WebKit both before and after
  the Safari 4.1.3 update; the update evidently doesn't touch whatever TLS
  bits CFNetwork/`NSURLConnection` use underneath. Not relevant to the
  current CA-hosted shell (no networking at all right now), kept as a
  finding for whenever the 64-bit content process's curl-based networking
  lands.
- No missing/renamed `WebFrameLoadDelegate`/`WebPolicyDelegate`/
  `WebUIDelegate` methods were ever hit on Tiger's WebKit.
- `testpages/` is this era's test fixture set; unused by the current shell.
