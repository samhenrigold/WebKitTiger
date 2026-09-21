# Text input and IME for the split-process (WebKit2 C API) Tiger UI process

Read-only research, independent of the JIT/QTKit spikes. No file under
`WebKit/` was touched. Builds directly on `logs/split-process-plan.md` and
`logs/webkit2-split-survey.md` (the two-process architecture this document
assumes — 32-bit Cocoa UI process on Tiger, 64-bit headless content process
on the same box per `split-process-plan.md` §0.1-0.2) and
`logs/webkitlegacy-plan.md` §1.4 (the WebKit1 `NSTextInput` analysis,
already exact and line-numbered, reused throughout). `split-process-plan.md:608`
already states the conclusion this document works out in full: *"Text
input / IME: UI process view. `NSTextInput` transfers directly...
That whole analysis carries over to the `PageClient` view."*

## 0. The headline finding: the C API needs almost nothing new

The team lead's framing (point 2) expected a new "Tiger platform variant of
`NativeWebKeyboardEvent` carrying the interpreted commands, like Cocoa's
does" would be needed. Reading `PageClient.h` and `NativeWebKeyboardEvent.h`
says otherwise: **Tiger's UI process *is* Cocoa/AppKit** (`USE(APPKIT)` is
ON per `webcore-plan.md` §1.4 — Tiger just has an old vintage of it), and
`NativeWebKeyboardEvent`'s existing `#if USE(APPKIT)` constructor —

```cpp
static Ref<NativeWebKeyboardEvent> create(NSEvent *, bool handledByInputMethod,
    bool replacesSoftSpace, const Vector<WebCore::KeypressCommand>&);
```

(`NativeWebKeyboardEvent.h:73`) — **already has exactly the shape a Tiger
view needs, unmodified.** `WebKeyboardEvent.h:64-66` shows the field it
carries is itself platform-gated (`#if PLATFORM(COCOA) Vector<KeypressCommand>
commands #else Vector<String> commands #endif`) specifically because Cocoa's
richer, ordered, interleaved `insertText:`/`setMarkedText:`/edit-command list
needs the extra text/range payload that GTK's bare command-name list doesn't.
Tiger needs exactly Cocoa's richer shape, not a new one — **the entire
`NativeWebKeyboardEvent`/`WebKeyboardEvent`/`PageClient` layer requires zero
new API.** What's missing is entirely on one side of that boundary: **the
Tiger UI-process view code that *produces* a `Vector<KeypressCommand>`**,
mirroring `WebHTMLView.mm`'s existing synchronous `NSInputManager`
`interpretKeyEvents:` flow (`webkitlegacy-plan.md` §1.4) instead of
`WebViewImpl.mm`'s asynchronous `NSTextInputContext`
`handleEventByInputMethod:` flow. §2 has the detail; §5's LOC estimate is
correspondingly small.

## 1. Message flow

### 1a. Key event: UI process interpretation → commands/insertText/marked text → content process

Traced from `WebViewImpl::interpretKeyEvent` (`WebViewImpl.mm:5957-6098`)
and `WebViewImpl::keyDown` (`:6814`):

1. **UI process, `-[TigerWebView keyDown:]`** (the Tiger equivalent of
   `WebViewImpl::keyDown`/`WebHTMLView`'s `keyDown:`) calls
   `[self interpretKeyEvents:@[event]]` — the **Tiger `NSInputManager`
   entry point**, not `-[NSTextInputContext handleEventByInputMethod:completionHandler:]`
   (10.6+, doesn't exist). This one call synchronously drives every
   `doCommandBySelector:`/`insertText:`/`setMarkedText:selectedRange:`
   callback the active input method needs to make, in order, before
   returning — **no async block, no holding tank, no keydown/keyup
   dispatch-ordering machinery** (all of `WebViewImpl.mm:5967-5996`'s
   `inputMethodUsesCorrectKeyEventOrder()`/`m_interpretKeyEventHoldingTank`
   logic — added for a Cocoa `NSTextInputContext` XPC-dispatch-ordering bug,
   `rdar://177042301` per the comment at `:5978` — **does not apply to
   Tiger's synchronous NSInputManager model and should not be ported.**)
2. Each callback (`doCommandBySelector:`, `insertText:`,
   `setMarkedText:selectedRange:`) **appends one `WebCore::KeypressCommand`**
   to a per-keydown `Vector<KeypressCommand>`, exactly mirroring what
   `WebViewImpl::doCommandBySelector`/`insertText`/`setMarkedText`
   (`:6100,6116,6542`) do today — those three methods' *bodies* (constructing
   a `KeypressCommand` and appending it, not their surrounding async
   plumbing) are the direct template, and per `webkitlegacy-plan.md` §1.4,
   `WebHTMLView.mm`'s existing implementations of these same twelve
   `NSTextInput` methods already do the equivalent thing for WebKit1 — reuse
   that code's shape, not `WebViewImpl`'s.
3. After `interpretKeyEvents:` returns, `NativeWebKeyboardEvent::create(NSEvent
   *, handledByInputMethod, replacesSoftSpace, commands)` builds the event
   with the collected commands — **unmodified**, per §0.
4. The event goes to `WebPageProxy` via the ordinary key-event IPC path
   (not traced further in this pass — architecture-neutral, not Tiger-specific).
5. **Content process, `WebPage::handleEditingKeyboardEvent`**
   (`WebPageMac.mm:352-395`) and `WebPage::executeKeypressCommandsInternal`
   (`:299-350`) consume the `Vector<KeypressCommand>` **completely
   unmodified** — this code has no Cocoa-version dependency at all, it's
   pure `WebCore::Editor`/`Editor::Command` dispatch (`editor->insertText`,
   `editor->confirmComposition`, `editor->command(...).execute(event)`), plus
   one `ExecuteSavedCommandBySelector` sync-IPC fallback to the UI process
   for commands `Editor` doesn't recognize (used for things like key-binding
   overrides the UI process's `NSResponder` chain — not `WebCore` — is
   supposed to handle; unchanged for Tiger).

### 1b. `EditorState` round trip for query methods

The four query-style `NSTextInput` methods
(`firstRectForCharacterRange:`, `attributedSubstringFromRange:`,
`markedRange`/`selectedRange`, `hasMarkedText`) are **not** part of the
per-keystroke flow above; they're independent async round trips
`WebViewImpl`'s corresponding methods (`selectedRangeWithCompletionHandler`
`:6187`, `markedRangeWithCompletionHandler` `:6255`,
`hasMarkedTextWithCompletionHandler` `:6271`,
`attributedSubstringForProposedRange` `:6289`,
`firstRectForCharacterRange` `:6344`, `characterIndexForPoint` `:6378`) make
directly against `WebPageProxy`, which forwards to the exact IPC messages
confirmed at `WebPageProxy.cpp:16858-16947`:

| `NSTextInput` method | `WebPageProxy` call | IPC message |
|---|---|---|
| `insertText:` | `insertTextAsync` (`:16858`) | `Messages::WebPage::InsertTextAsync` |
| `hasMarkedText` | `hasMarkedText` (`:16866`) | `Messages::WebPage::HasMarkedText` |
| — | `isMarkedTextRequiredForComposition` (`:16874`) | `Messages::WebPage::IsMarkedTextRequiredForComposition` |
| `markedRange` | `getMarkedRangeAsync` (`:16884`) | `Messages::WebPage::GetMarkedRangeAsync` |
| `selectedRange` | `getSelectedRangeAsync` (`:16891`) | `Messages::WebPage::GetSelectedRangeAsync` |
| `characterIndexForPoint:` | `characterIndexForPointAsync` (`:16904`) | `Messages::WebPage::CharacterIndexForPointAsync` |
| `firstRectForCharacterRange:` | `firstRectForCharacterRangeAsync` (`:16910`) | `Messages::WebPage::FirstRectForCharacterRangeAsync` |
| `setMarkedText:selectedRange:` | `setCompositionAsync` (`:16918`) | `Messages::WebPage::SetCompositionAsync` |
| `unmarkText` | `confirmCompositionAsync` (`:16940`) | `Messages::WebPage::ConfirmCompositionAsync` |

**None of these calls are Cocoa-version-gated in `WebPageProxy.cpp`** — the
`EditorState`/`EditingRange` types and the `sendWithAsyncReplyToFocusedOrMainFrameProcess`
transport are architecture-neutral. `WebPage::insertTextAsync`/`setCompositionAsync`/
`confirmCompositionAsync`'s *implementations* live in
`WebKit/Source/WebKit/WebProcess/WebPage/Cocoa/WebPageCocoa.mm` (confirmed by
`grep -rl`, not `mac/WebPageMac.mm` specifically) — the `Cocoa/` directory is
shared between mac and iOS, so it's already written to the lowest common
Cocoa denominator; not read in full in this pass, but its directory
placement alone is strong evidence it needs no Tiger-specific changes (same
pattern `split-process-plan.md` found repeatedly for Cocoa/ vs mac/-only
files). **`attributedSubstringForProposedRange`'s `NSAttributedString`
construction (`WebViewImpl.mm:6289-6343`) is the one piece worth a second
look** — it may reach for 10.6+ attribute constants the way
`validAttributesForMarkedTextSingleton` (`:5904-5919`, not fully read in this
pass) might; `webkitlegacy-plan.md` §1.4's table already identifies the
exact three Tiger-unsafe attribute constants
(`NSMarkedClauseSegmentAttributeName`/`NSTextAlternativesAttributeName`/
`NSTextInsertionUndoableAttributeName`) to gate for `WebHTMLView.mm`'s
version of the same list — apply the identical three-entry gate here.

**The reply direction (`EditorState` back to the UI process) does not need
a new push mechanism** — these are request/reply async IPC calls
(`CompletionHandler`-based), not a subscription; the UI process asks, the
content process answers once, per call. This is simpler than the modern
Cocoa flow's additional `EditorState`-diff-driven proactive updates
(`selectionDidChange` at `WebViewImpl.mm:3228`, not traced further — likely
safe to keep as-is since it's just a notification hook triggering the UI
process to re-query, not itself Cocoa-version-gated) — a Tiger
implementation can start with pure request/reply and skip the proactive
push-update optimization for a first cut.

## 2. What the C API lacks — revised finding

Restating §0's conclusion in the terms the task asked for:

- **`NativeWebKeyboardEvent`**: nothing missing. The `USE(APPKIT)` overload
  already takes the exact `Vector<KeypressCommand>` shape needed.
- **`PageClient`**: nothing missing for text input specifically.
  `PageClient::interpretKeyEvent` (`PageClient.h:616`) is the **only**
  text-input-adjacent virtual on the abstract interface — everything else
  (`setMarkedText`, `hasMarkedText`, `firstRectForCharacterRange`, etc.) is
  **not** part of `PageClient` at all; it's implemented directly on the
  platform view class (`WebViewImpl` for Cocoa) as ordinary methods that call
  `WebPageProxy`'s async-IPC wrappers directly (§1b's table). This means a
  Tiger `PageClient` implementation doesn't need to plumb text input through
  the generic interface either — **it implements `interpretKeyEvent`** (one
  virtual, wrapping §1a's `interpretKeyEvents:`-based flow) **and calls
  `WebPageProxy`'s existing methods directly** for everything else, exactly
  as `WebViewImpl` does. No `PageClient` interface change.
- **`KeyEventInterpretationContext`** (the second parameter to
  `PageClient::interpretKeyEvent`) — not read in this pass; flagged, since
  its shape determines exactly what a Tiger `interpretKeyEvent` override
  needs to accept/return. Given the method returns `bool` and the
  parameter is named for "interpretation context" rather than "result," a
  reasonable guess (not verified) is it carries per-call context (e.g.
  whether this is a plain key vs. a repeat) rather than being an
  output — worth 10 minutes of direct reading before implementation starts.
- **What genuinely is new, but isn't API — it's application code**: the
  Tiger `PageClient`/`NSView` subclass's `NSInputManager`-based
  `interpretKeyEvents:`/`doCommandBySelector:`/`insertText:`/
  `setMarkedText:selectedRange:`/`unmarkText`/`hasMarkedText`/`markedRange`/
  `selectedRange`/`firstRectForCharacterRange:`/`characterIndexForPoint:`/
  `attributedSubstringFromRange:`/`validAttributesForMarkedText`
  implementation — twelve methods, same list `webkitlegacy-plan.md` §1.4
  already enumerated for `WebHTMLView.mm`, this time calling `WebPageProxy`'s
  async methods instead of talking to a same-process `WebCore::Editor`
  directly. §3 sketches this.

## 3. Tiger `NSTextInput` implementation sketch for the custom `NSView`

Given §0-§2, this is much closer to porting `WebHTMLView.mm`'s **existing**
implementation (already Tiger-shaped, per `webkitlegacy-plan.md` §1.4) than
to porting `WebViewImpl.mm`'s (built for a completely different, much more
recent IM-dispatch model with no Tiger analog). Concretely:

```objc
@interface TigerWebView (WebNSTextInputSupport) <NSTextInput>
@end

@implementation TigerWebView (WebNSTextInputSupport)

- (void)keyDown:(NSEvent *)event
{
    _collectedCommands.clear();
    BOOL wasInterpreted = NO;
    if ([self hasMarkedTextOrIsEditable]) {  // gate identical in spirit to
                                              // WebViewImpl's `if (!inputContext())`
                                              // early-out, WebHTMLView already has one
        [self interpretKeyEvents:@[event]];
        wasInterpreted = YES;
    }
    Ref event = NativeWebKeyboardEvent::create(event, wasInterpreted, /*replacesSoftSpace*/ NO, _collectedCommands);
    _page->handleKeyboardEvent(WTF::move(event));  // ordinary key IPC, unchanged
}

- (void)doCommandBySelector:(SEL)selector
{
    _collectedCommands.append(WebCore::KeypressCommand { NSStringFromSelector(selector) });
}

- (void)insertText:(id)string
{
    // WebHTMLView.mm's twelve-method block (webkitlegacy-plan.md §1.4) is the
    // reference for the NSAttributedString-vs-NSString and replacementRange handling.
    _collectedCommands.append(WebCore::KeypressCommand { "insertText:"_s, stringFrom(string) });
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange
{
    _collectedCommands.append(WebCore::KeypressCommand { "setMarkedText:"_s, stringFrom(string),
        /* underlines from -[NSAttributedString] runs, Tiger-safe subset only */ { },
        EditingRange { selectedRange } });
}

- (void)unmarkText
{
    _page->confirmCompositionAsync();  // direct WebPageProxy call, not a collected command —
                                        // matches WebViewImpl::unmarkText's shape (:6415)
}

- (BOOL)hasMarkedText
{
    // Synchronous NSTextInput contract requires an immediate answer; the IPC round trip
    // in §1b is async. WebHTMLView.mm/Tiger TSM historically answer from locally-cached
    // state (the last EditorState received), not a fresh round trip per keystroke — this
    // needs the same local-cache pattern, not a blocking IPC call. Not designed further
    // in this pass; flagged as the one place the async IPC model creates real friction
    // against NSTextInput's synchronous method contract (NSInputManager, unlike
    // NSTextInputContext, has no async variant of these query methods at all).
    return _cachedEditorState.hasComposition;
}

- (NSRange)markedRange   { return _cachedEditorState.markedRange; }   // same caching note
- (NSRange)selectedRange { return _cachedEditorState.selectedRange; } // same caching note

- (NSRect)firstRectForCharacterRange:(NSRange)range
{
    // Also synchronous-contract; same caching concern as hasMarkedText/markedRange/
    // selectedRange above, worse here because the answer is geometry Tiger's IM positions
    // its candidate window against — a stale cache is visibly wrong, not just logically
    // stale. Not resolved in this pass.
    return [self convertRect:_cachedEditorState.firstRectForCharacterRange toView:nil];
}

// attributedSubstringFromRange:, characterIndexForPoint:, conversationIdentifier,
// validAttributesForMarkedText: same shape/caching pattern, ported from
// WebHTMLView.mm per webkitlegacy-plan.md §1.4's exact line-numbered remedies for
// the three 10.5+ attribute constants and the `long` vs `NSInteger` conversationIdentifier.

@end
```

**The one substantive design problem this sketch surfaces that §0-§2 didn't
already answer**: `NSTextInput` (unlike `NSTextInputClient`, which has no
async variants either, but was designed alongside a same-process `WebCore`
so the sync contract was never actually a problem for Cocoa's own
implementation) has **synchronous** query methods
(`hasMarkedText`/`markedRange`/`selectedRange`/`firstRectForCharacterRange:`),
but the split-process architecture's `EditorState` data lives in the content
process and only arrives via async IPC. **`WebViewImpl.mm` doesn't hit this
problem because `NSTextInputContext`'s equivalent methods
(`selectedRangeWithCompletionHandler` etc., §1b's table) are *already*
async on modern Cocoa** — the API changed to match WebKit2's process split
specifically. `NSTextInput`/`NSInputManager` predates any of that and offers
no async escape hatch. **The fix is a local cache**: track the UI process's
best-known `EditorState` (updated on every `EditorState` push/reply the
content process sends, which the existing selection/composition-changed
notification machinery already provides per §1b) and answer these four
synchronous methods from the cache, accepting one-IPC-round-trip staleness
— exactly the kind of tradeoff `WebViewImpl.mm`'s own extensive commentary
around `selectedRangeWithCompletionHandler` (`:6193-6250`, about queued
`insertText:`/`setMarkedText:` commands not having reached the web process
yet) already wrestles with for a *different* reason (its own internal
command-queue staleness, not a sync/async API mismatch) — the Tiger version
has a strictly bigger version of the same class of problem. **Flagged as the
single highest-risk design point in this whole document, not resolved
here.**

### 3a. Survey update (2026-09-20): how `WebViewImpl`/`WebPageProxy` actually
answer the six synchronous queries today, and the recommended Tiger mapping

Direct read of `Source/WebKit/UIProcess/mac/WebViewImpl.mm` and
`Source/WebKit/UIProcess/WebPageProxy.cpp` (both files, current tree,
`d2f52605` per `NOTES.md`), specifically the six methods §1b's table names:
`selectedRange`, `markedRange`, `hasMarkedText`,
`attributedSubstringForProposedRange`, `firstRectForCharacterRange`,
`characterIndexForPoint`. **Two things in this section's original framing
turn out to be wrong, corrected below**: (1) these are not answered from a
cached `EditorState`/`postLayoutData` at all — there is no cache in the
loop; (2) the actual synchronous `NSTextInputClient` protocol methods
(no `WithCompletionHandler`/no `Async` suffix) are **dead code**, not the
live implementation.

**Finding 1 — the truly synchronous `NSTextInputClient` methods are stubs.**
`WebViewImpl.mm:6725-6763`:

```cpp
// Synchronous NSTextInputClient is still implemented to catch spurious sync calls. Remove when that is no longer needed.

NSRange WebViewImpl::selectedRange() { return NSMakeRange(NSNotFound, 0); }
bool WebViewImpl::hasMarkedText() { ASSERT_NOT_REACHED(); return NO; }
NSRange WebViewImpl::markedRange() { ASSERT_NOT_REACHED(); return NSMakeRange(NSNotFound, 0); }
NSAttributedString *WebViewImpl::attributedSubstringForProposedRange(...) { ASSERT_NOT_REACHED(); return nil; }
NSUInteger WebViewImpl::characterIndexForPoint(NSPoint) { ASSERT_NOT_REACHED(); return 0; }
NSRect WebViewImpl::firstRectForCharacterRange(...) { ASSERT_NOT_REACHED(); return NSZeroRect; }
```

The comment says it all: these exist only to catch AppKit spuriously calling
the *old*, pre-async `NSTextInputClient` entry points, which the code
expects never to happen on a current OS. **Modern AppKit (10.6+) added an
async completion-handler-based extension to `NSTextInputContext`
(`selectedRangeWithCompletionHandler:` etc.) that supersedes the classic
synchronous protocol entirely** when the input context/client opts in — this
is the actual live implementation, at `WebViewImpl.mm:6187-6395`. **This is
the load-bearing fact for Tiger**: upstream didn't solve "how do you answer
a synchronous query with data that lives in another process" — it made the
query itself asynchronous, which requires an AppKit version Tiger doesn't
have. There is no synchronous-answer machinery to port; Tiger has to invent
one, because Tiger's `NSTextInput`/`NSInputManager` (`NSInputManager.h`,
confirmed unchanged since 2005 in the 10.4u SDK) has no async variant of
any of these six methods at all — none of the `WithCompletionHandler`
methods' *names* even exist as options on Tiger's protocol.

**Finding 2 — the six real implementations are fresh, uncached async IPC,
every single call, with no `EditorState`/`postLayoutData` involved.**
`WebViewImpl.mm:6187-6395` (`selectedRangeWithCompletionHandler`,
`markedRangeWithCompletionHandler`, `hasMarkedTextWithCompletionHandler`,
`attributedSubstringForProposedRange`, `firstRectForCharacterRange`,
`characterIndexForPoint`) each call straight into `WebPageProxy`
(`getSelectedRangeAsync`/`getMarkedRangeAsync`/`hasMarkedText`/
`attributedSubstringForCharacterRangeAsync`/`firstRectForCharacterRangeAsync`/
`characterIndexForPointAsync`, `WebPageProxy.cpp:16866-16916`), and every one
of those does exactly one thing: `sendWithAsyncReplyToFocusedOrMainFrameProcess(...)`
— a genuine, uncached async IPC round trip to the content process, per call,
every time. **`grep`-checked directly: neither `waitForAndDispatchImmediately`
nor `sendSync` appears anywhere in this code path** — both do exist
elsewhere in `WebPageProxy.cpp` (`:10408` for subframe creation, `:19569`
for site-isolation message forwarding), so blocking/synchronous IPC is a
tool this codebase has and uses in general, but **deliberately not here**.
On the content-process side (`WebPageCocoa.mm:2207-2288`), the handlers
answer straight from live `WebCore::Editor`/`FrameSelection` state at the
moment the IPC message is handled (`focusedOrMainFrame->editor().hasComposition()`,
`frame->selection().selection().toNormalizedRange()`,
`frame->editor()->firstRectForRange(*range)`, etc.) — **not** from
`EditorState`/`postLayoutData` either. `EditorState`'s
`hasPostLayoutData()`/`postLayoutData` split (`EditorState.h:158-159`) is a
real mechanism, but it belongs to a completely different data path: the
proactive `EditorState` *push* from content process to UI process on every
selection/composition change (feeds Touch Bar formatting flags, spell-check
candidate requests, `canCut`/`canCopy`/`canPaste`, `isContentEditable`
gating — all read via `m_page->editorState()` at call sites like
`WebViewImpl.mm:3825,7471-7477`). **The team lead's hypothesis that the
pre-layout/post-layout split is what avoids stale answers for the six sync
queries does not hold up under direct reading — corrected here.** That
split's job is to avoid a partial/pre-layout `EditorState` snapshot being
used for the *push*-driven consumers; the six query methods never consult
`postLayoutData` at all, because they never consult `EditorState` at all —
they get a fresh, authoritative, live answer via IPC every time, which is
strictly better than any cache and is only possible because the query
itself is async.

**Finding 3 — the one place upstream does patch a query answer locally is
narrower than a general cache, and doesn't actually apply to Tiger's
architecture.** `selectedRangeWithCompletionHandler` and
`attributedSubstringForProposedRange` locally "stage" the cumulative effect
of the *current keydown's not-yet-IPC'd* `KeypressCommand`s
(`m_collectedKeypressCommands`/`m_stagedMarkedRange`,
`WebViewImpl.mm:6203-6222,6299-6320`) on top of the fresh IPC reply — this
exists because modern AppKit's `NSTextInputContext handleEventByInputMethod:`
is itself asynchronous (an XPC round trip to the input method), so a
modeless IME (Vietnamese Telex, Korean Hangul) can call `insertText:` and
then immediately poll `selectedRange` *before* the UI process has even sent
that `insertText:`'s `KeypressCommand` over IPC to the content process —
see the extensive comment at `:6193-6201`. **This exact race is structurally
impossible on Tiger**: `-interpretKeyEvents:` (`NSInputManager`) is fully
synchronous and single-threaded — every `doCommandBySelector:`/`insertText:`/
`setMarkedText:selectedRange:` callback for one keydown happens in order, on
the same call stack, before `-interpretKeyEvents:` returns, and *nothing* is
sent over IPC to the content process until after it returns (per §1a). So a
Tiger IME polling `markedRange`/`selectedRange` **from within its own
callback during that same `-interpretKeyEvents:` call** (the direct Tiger
analog of upstream's race) can be answered with **zero staleness** — the
answer is sitting in the current call's local, not-yet-dispatched state,
same process, same stack frame, no IPC involved yet. `spike/TigerBrowser`'s
existing `_pending*`/`_applied*` split already has exactly the right shape
for this, just needs the distinction made explicit (see recommendation
below): a query during the live `-interpretKeyEvents:` call should read
`_pending*`, not `_applied*`.

**Recommended Tiger mapping**, combining all three findings:

1. **The six query methods must answer synchronously — there is no way
   around this, and no upstream precedent solves it, because upstream's
   equivalent surface is async and Tiger's isn't.** Blocking the UI
   process's main thread on a real IPC round trip per query (`sendSync`
   with a timeout) was considered and rejected: it's a tool the WebKit2
   codebase has and uses elsewhere, but conspicuously *never* for text
   input, and for good reason — an IME can poll these multiple times per
   keystroke (candidate window repositioning, live preview), and a content
   process that's busy (running a layout, a long JS task, blocked on a
   dispatch_sync from a `WebCore::Editor` command Tiger's UI process itself
   just sent) would freeze IME interaction, which is far more visible and
   annoying than a stale answer. A bounded synchronous wait only trades
   "always somewhat stale" for "usually correct, occasionally the whole UI
   process hangs for the timeout" — worse, not better.
2. **Two-tier local answer, not one cache:**
   - **During the current `-interpretKeyEvents:` call** (a real, cheaply
     detectable state — set a `BOOL _inInterpretKeyEvents` flag around the
     call in `-keyDown:`): answer `hasMarkedText`/`markedRange`/
     `selectedRange`/`attributedSubstringFromRange:` from the **pending**
     (not-yet-IPC'd) local state, per Finding 3 — this is the direct,
     zero-staleness Tiger analog of upstream's staging trick, and covers
     exactly the same class of IME (a composing input method verifying its
     own just-issued edit before returning control).
   - **Outside that window** (a query triggered by anything else — a
     candidate-window repaint timer, a second unrelated event, `NSTextInput`
     being polled from outside a keydown at all): answer from the last
     **applied** state, i.e. the most recent `EditorState` actually
     confirmed by the content process over async IPC. This is where real
     staleness is unavoidable — bounded by one keydown's round-trip
     latency, exactly as `spike/TigerBrowser`'s existing spike models it.
   - `firstRectForCharacterRange:`/`characterIndexForPoint:` are geometry
     queries an IME uses to position its candidate window; per the plan's
     original note these are the highest-visible-cost case of staleness
     (a wrong rect, not just a logically-stale range) — same two-tier
     answer applies, but flag this pair for extra scrutiny once real
     layout geometry (not the spike's fixed stub rect) is wired up, since
     a resize/scroll between "applied" and "now" moves the rect independent
     of any edit at all.
3. **The UI process must push a fresh `EditorState`-equivalent to the "applied"
   cache on every content-process reply that changes selection/composition** —
   not just after `setCompositionAsync`/`confirmCompositionAsync`/
   `insertTextAsync`, but after *any* IPC that could move the selection
   (a `KeypressCommand` batch execution, a mouse-driven selection change
   forwarded from the UI process itself, a JS-driven `document.execCommand`,
   etc.) — mirroring the general proactive `EditorState` push upstream
   already has (`WebViewImpl.mm:3228`'s `selectionDidChange`, not read in
   full in this pass, flagged again) rather than only updating the cache
   from the narrow keydown-round-trip path the spike currently models.
   Missing this would let the "applied" cache go stale for reasons that
   have nothing to do with typing at all (e.g. the content process
   processing an in-page `<a>` focus jump), which the spike's current
   design doesn't yet account for.
4. **`conversationIdentifier`** needs no IPC or caching at all on either
   architecture — trivially answerable locally (`spike/TigerBrowser` already
   returns `(long)self`; upstream doesn't implement it as a real
   `WebViewImpl` method at all, confirmed by its absence from both the
   `WithCompletionHandler` list and the stub list above — Tiger's version is
   fine as-is).
5. **Dead-key/IME composition testing via `CGEventPost`/
   `CGEventCreateKeyboardEvent`** (to genuinely exercise `setMarkedText:`
   through the real HID/TSM pipeline, per the prior spike's finding that a
   hand-built `NSEvent` cannot) **stays deferred until the box is quiet** —
   noted, not attempted, in this pass; it needs frontmost-window focus that
   concurrent teammate GUI tests on the shared box have been unreliable for.

## 4. Spelling / dictation / autocorrect scope

`webkitlegacy-plan.md` §4.3 ("WebEditorClient — spellcheck off") and
`webcore-plan.md` §5.6 already establish the WebKit1 answer: every
`NSSpellChecker`-touching `WebEditorClient` method becomes a no-op for Tiger
because continuous spell-checking, grammar-checking, automatic
substitution/correction, and `NSTextCheckingResult`-based candidates are all
10.5-10.12-era APIs. **The same scoping applies to WebKit2's UI-process-side
`TextCheckerMac.mm` (599 lines)**, confirmed by direct read: its Tiger-unsafe
surface is exactly the same family —
`isAutomaticTextReplacementEnabled`/`isAutomaticQuoteSubstitutionEnabled`/
`isAutomaticDashSubstitutionEnabled`/`grammarCheckingEnabled` (`:59-94`, all
10.5/10.6 `+[NSSpellChecker ...]` class methods), plus a dozen
`[[NSSpellChecker sharedSpellChecker] updatePanels]` calls (`:205` onward,
present in every Tiger `NSSpellChecker` version — 10.0+ — these are fine) and
`uniqueSpellDocumentTag`/`closeSpellDocumentWithTag:` (`:363,368`, also
10.0+, fine). **Verdict: same as WebKit1 — the basic spell-check-tag and
panel-update calls are Tiger-safe and can stay; the automatic
substitution/correction/grammar family (10.5+) gets the identical no-op
treatment `webkitlegacy-plan.md` §4.3 already specifies, applied to
`TextCheckerMac.mm` instead of `WebEditorClient.mm`.** No new design work —
this is a direct transplant of an already-solved problem onto the
architecturally-equivalent WebKit2 file. Dictation isn't mentioned anywhere
in either file (grepped, zero hits for `NSDictation`) — out of scope by
absence, not by exclusion.

## 5. LOC estimate

| Piece | LOC | Basis |
|---|---|---|
| `TigerWebView`/`PageClient` `interpretKeyEvent` override + the 12-method `NSTextInput` category (§3) | 300-450 | Direct structural port of `WebHTMLView.mm`'s existing ~550-line `NSTextInput` implementation (per `webkitlegacy-plan.md` §1.4's line ranges, roughly `:6259-6810`), minus the parts that talk to a same-process `Editor` (replaced by `WebPageProxy` async calls, §1b's table — a mechanical swap, not new logic) |
| `EditorState` local cache for the four synchronous query methods (§3's flagged design problem) | 100-200 | New — no existing analog in either `WebHTMLView.mm` (same-process, no caching needed) or `WebViewImpl.mm` (async API, no caching needed) has this exact shape; smallest plausible design is a single cached-`EditorState` struct updated from the existing selection/composition-changed IPC push |
| `PageClient::interpretKeyEvent` override wiring (the one real virtual, §2) | 20-40 | One method, mirrors `WebViewImpl::interpretKeyEvent`'s *outcome* (call the view's `interpretKeyEvents:`, build commands, hand back) without any of its async holding-tank machinery (§1a) |
| Command-name-to-selector and `commandNameForSelectorName` mapping | 0 | **Already architecture-neutral** — lives in `WebPageMac.mm`/shared code per §1a, unmodified |
| `TextCheckerMac.mm` Tiger gating (§4) | 20-40 | Direct transplant of `webkitlegacy-plan.md` §4.3's already-designed no-op pattern; same handful of `#if !PLATFORM(TIGER)` gates, different file |
| `NativeWebKeyboardEvent`/`WebKeyboardEvent`/`PageClient.h` | **0** | Confirmed no changes needed, §0/§2 |
| `WebPage::executeKeypressCommandsInternal`/`handleEditingKeyboardEvent` (content process) | **0** | Confirmed architecture-neutral, §1a |
| `WebPageProxy.cpp`'s nine IPC-wrapper methods (§1b's table) | **0** | Confirmed not Cocoa-version-gated, §1b |
| `WebPageCocoa.mm`'s `insertTextAsync`/`setCompositionAsync`/`confirmCompositionAsync` implementations | 0-100 | Not fully read in this pass (flagged) — directory placement (`Cocoa/`, shared mac+iOS) is strong but not conclusive evidence of zero Tiger-specific work needed |
| `attributedSubstringForProposedRange`'s three attribute-constant gates (§1b) | ~10 | Direct transplant of `webkitlegacy-plan.md` §1.4's already-identified three-entry gate |
| **Total** | **~450-840**, mostly in one file | Small relative to the rest of the split-process effort (`webkit2-split-survey.md` §6 estimates 3,000-4,200 LOC for the whole UI process side) — text input is a well-isolated, already-mostly-solved corner of that total, not a new blocker |

**This is meaningfully smaller than the team lead's framing implied**,
because the framing's biggest assumed cost (a new `NativeWebKeyboardEvent`
platform variant) turned out to already exist as the Cocoa overload, and the
second-biggest assumed cost (reconciling WebKit2's C API with a Tiger
custom view) turned out to be almost entirely a `WebHTMLView.mm`-to-
`PageClient`-method port of logic `webkitlegacy-plan.md` §1.4 had already
fully worked out for WebKit1. The one genuinely new problem this document
surfaces — §3's synchronous-`NSTextInput`-vs-async-`EditorState` mismatch —
wasn't anticipated in the task's framing at all, and is the one item here
that both existing WebKit1 and modern WebKit2 code independently avoid
(one because it's same-process, the other because its API is already
async), leaving no directly-reusable prior art to port from.

## Not done in this pass

- Did not read `KeyEventInterpretationContext`'s definition (§2) — needed to
  finalize the exact `PageClient::interpretKeyEvent` override signature.
- Did not read `WebPageCocoa.mm`'s `insertTextAsync`/`setCompositionAsync`/
  `confirmCompositionAsync` bodies in full (§1b, §5) — only confirmed their
  location by `grep -rl`, not their content, so the "0-100 LOC" estimate for
  that row is a placeholder pending a direct read.
- Did not read `validAttributesForMarkedTextSingleton`
  (`WebViewImpl.mm:5904-5919`) in full — assumed it needs the same
  three-attribute-constant gate `webkitlegacy-plan.md` §1.4 already found
  for `WebHTMLView.mm`'s version of the same list, not independently
  verified against this file's exact contents.
- Did not investigate the C API surface for non-Cocoa custom views (WPE's
  `wpe_view_backend`, the `WKPage.h` C API, PlayStation's view) as the task
  requested for comparison — §0-§2's finding that `NativeWebKeyboardEvent`
  needs no Tiger-specific variant made that comparison less load-bearing
  than expected (the GTK/WPE variants were read only as evidence for what
  the *type itself* already supports, per §0, not as a design template,
  since Tiger's `USE(APPKIT)` overload already covers the need). If a
  genuinely new non-Cocoa-shaped need turns up during implementation, this
  survey would be the next thing to do properly.
- `WebPageProxy::selectionDidChange`/`WebViewImpl::selectionDidChange`
  (`WebViewImpl.mm:3228`) — the push-notification side of the `EditorState`
  cache §3 proposes — was not read, only referenced as "likely exists and is
  probably fine." Needs a direct read before the cache design in §3/§5 is
  final.
