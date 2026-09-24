/*
 * Copyright (C) 2026 WebKitTiger contributors. BSD-2-Clause, as the rest of WebKit.
 */

#include "config.h"
#import "TigerWK2View.h"

#include "NativeWebKeyboardEvent.h"
#include "NativeWebMouseEvent.h"
#include "NativeWebWheelEvent.h"
#include "WebPageProxy.h"
#if TIGER_HAS_IME
#include "EditingRange.h"
#include "TextChecker.h"
#include "TextCheckerState.h"
#include "WebProcessPool.h"
#include "WebProcessProxy.h"
#include <WebCore/CompositionUnderline.h>
#endif
#include <wtf/text/WTFString.h>
#include <WebCore/IntRect.h>

using namespace WebKit;

@implementation TigerWK2View

- (WebKit::TigerWebView*)webView { return _webView.get(); }

- (void)attachWebView:(TigerWebView*)view
{
    if (_webView && _webView.get() != view)
        _webView->setNSView(nullptr);
    _webView = view;
    if (_webView)
        _webView->setNSView(self);
}

// The web process paints in view coordinates, y down. An NSView that is not
// flipped would hand -drawRect: a y-up context and the page would come out
// upside down.
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque { return !_webView || !_webView->isTigerDirectPresentationActive(); }

- (void)dealloc
{
    if (_webView)
        _webView->setNSView(nullptr);
    _webView = nullptr;
    [super dealloc];
}

- (void)viewWillMoveToWindow:(NSWindow*)window
{
    if (_webView)
        _webView->invalidateTigerDirectPresentation();
    [super viewWillMoveToWindow:window];
}

// Tiger calls didAddSubview: from _setSuperview: before _setWindow:.
// Invalidation draws the retained bitmap synchronously, so do it before the
// hierarchy mutation rather than drawing a child whose window is still nil.
- (void)addSubview:(NSView*)view
{
    if (_webView && view)
        _webView->invalidateTigerDirectPresentation();
    [super addSubview:view];
}

- (void)addSubview:(NSView*)view positioned:(NSWindowOrderingMode)place relativeTo:(NSView*)otherView
{
    if (_webView && view)
        _webView->invalidateTigerDirectPresentation();
    [super addSubview:view positioned:place relativeTo:otherView];
}

- (void)setHidden:(BOOL)hidden
{
    if (_webView && hidden != [self isHidden])
        _webView->invalidateTigerDirectPresentation();
    [super setHidden:hidden];
}

- (void)setFrameOrigin:(NSPoint)origin
{
    if (_webView && !NSEqualPoints(origin, [self frame].origin))
        _webView->invalidateTigerDirectPresentation();
    [super setFrameOrigin:origin];
}

- (void)setBoundsOrigin:(NSPoint)origin
{
    if (_webView && !NSEqualPoints(origin, [self bounds].origin))
        _webView->invalidateTigerDirectPresentation();
    [super setBoundsOrigin:origin];
}

- (void)setBoundsSize:(NSSize)size
{
    if (_webView && !NSEqualSizes(size, [self bounds].size))
        _webView->invalidateTigerDirectPresentation();
    [super setBoundsSize:size];
}

- (void)setFrameRotation:(CGFloat)rotation
{
    if (_webView)
        _webView->invalidateTigerDirectPresentation();
    [super setFrameRotation:rotation];
}

- (void)setBoundsRotation:(CGFloat)rotation
{
    if (_webView)
        _webView->invalidateTigerDirectPresentation();
    [super setBoundsRotation:rotation];
}

- (void)drawRect:(NSRect)dirtyRect
{
    CGContextRef context = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];
    if (!_webView) {
        CGContextSetRGBFillColor(context, 1, 1, 1, 1);
        CGContextFillRect(context, NSRectToCGRect(dirtyRect));
        return;
    }
    _webView->paint(context, WebCore::IntRect(dirtyRect.origin.x, dirtyRect.origin.y,
        dirtyRect.size.width, dirtyRect.size.height));
}

- (void)setFrameSize:(NSSize)size
{
    if (_webView && !NSEqualSizes(size, [self frame].size))
        _webView->invalidateTigerDirectPresentation();
    [super setFrameSize:size];
    if (_webView)
        _webView->setViewSizeFromNSView(WebCore::IntSize(size.width, size.height));
}

// ------------------------------------------------------------------ input --
// Every handler goes NSEvent -> Shared/tiger/NativeWeb*EventTiger.mm -> WebPageProxy.
// The view is flipped, so pointForEvent's conversion lands in web coordinates.

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
    [[self window] makeFirstResponder:self];
    [self applyContinuousSpellChecking];
}

// TIGER_INPUTLOG=1 prints one line as each input event leaves the UI process. The
// matching TIGER-INPUT: cursor line comes from TigerWebView::setCursor, so hover
// latency is the gap between the two in the timestamped app.log.
static bool inputLoggingEnabled()
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = getenv("TIGER_INPUTLOG") ? 1 : 0;
    return enabled;
}

- (void)mouseEvent:(NSEvent*)event
{
    if (!_webView || !_webView->page())
        return;
    if (inputLoggingEnabled()) {
        NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
        fprintf(stderr, "TIGER-INPUT: mouse type=%d at %.0f,%.0f\n", (int)[event type], p.x, p.y);
    }
    _webView->page()->handleMouseEvent(NativeWebMouseEvent::create(event, nil, self, WebEventInputSource::UserDriven));
}
- (void)mouseDown:(NSEvent*)event { [self abandonMarkedText]; [self mouseEvent:event]; }
- (void)mouseUp:(NSEvent*)event { [self mouseEvent:event]; }
- (void)mouseMoved:(NSEvent*)event { [self mouseEvent:event]; }
- (void)mouseDragged:(NSEvent*)event { [self mouseEvent:event]; }
- (void)rightMouseDown:(NSEvent*)event { [self mouseEvent:event]; }
- (void)rightMouseUp:(NSEvent*)event { [self mouseEvent:event]; }
- (void)rightMouseDragged:(NSEvent*)event { [self mouseEvent:event]; }
- (void)otherMouseDown:(NSEvent*)event { [self mouseEvent:event]; }
- (void)otherMouseUp:(NSEvent*)event { [self mouseEvent:event]; }
- (void)otherMouseDragged:(NSEvent*)event { [self mouseEvent:event]; }

- (void)scrollWheel:(NSEvent*)event
{
    if (!_webView || !_webView->page())
        return;
    if (inputLoggingEnabled())
        fprintf(stderr, "TIGER-INPUT: wheel dy=%.1f\n", (double)[event deltaY]);
    _webView->page()->handleNativeWheelEvent(NativeWebWheelEvent::create(event, self));
}

// Keyboard. Cocoa's own key bindings are the source of truth: -keyDown: runs
// -interpretKeyEvents:, the system's StandardKeyBinding.dict turns the event into
// NSResponder selectors, and each selector becomes a WebKit editor command
// (upstream's WebViewImpl::commandNameForSelector: strip the trailing colon, the
// names are not case sensitive, with a handful of exceptions). The raw event is
// still forwarded so that the page sees keydown/keypress and WebCore keeps doing
// plain text insertion and its own default handling (Tab moves focus).
//
// ponytail: the commands are a second IPC message after the key event rather than
// riding inside WebKeyboardEvent::commands as they do under USE(APPKIT). If the web
// process is busy enough for a key event to sit in WebPageProxy's queue a command
// can overtake it. Upgrade path: add the commands to the event, which means
// WebEvent.serialization.in and the wire checks.

static NSString* commandNameForSelector(SEL selector)
{
    static NSDictionary* exceptions = nil;
    if (!exceptions) {
        exceptions = [[NSDictionary alloc] initWithObjectsAndKeys:
            @"InsertNewline", @"insertNewlineIgnoringFieldEditor:",
            @"InsertNewline", @"insertParagraphSeparator:",
            @"InsertTab", @"insertTabIgnoringFieldEditor:",
            @"MovePageDown", @"pageDown:",
            @"MovePageDownAndModifySelection", @"pageDownAndModifySelection:",
            @"MovePageUp", @"pageUp:",
            @"MovePageUpAndModifySelection", @"pageUpAndModifySelection:",
            @"ScrollPageForward", @"scrollPageDown:",
            @"ScrollPageBackward", @"scrollPageUp:",
            nil];
    }
    NSString* name = NSStringFromSelector(selector);
    NSString* exception = [exceptions objectForKey:name];
    if (exception)
        return exception;
    if ([name length] < 2 || ![name hasSuffix:@":"])
        return nil;
    return [name substringToIndex:[name length] - 1];
}

- (WebKit::WebPageProxy*)page
{
    return _webView ? _webView->page() : nullptr;
}

- (void)executeEditCommand:(NSString*)name argument:(NSString*)argument
{
    RefPtr page = [self page];
    if (!page || ![name length])
        return;
    page->executeEditCommand(String::fromUTF8([name UTF8String]),
        argument ? String::fromUTF8([argument UTF8String]) : String());
}

// TIGER_HAS_IME: the worktree whose WebPageProxy has the tiger* text-input calls
// (branch tiger-ime) defines it for the apps; every other tree keeps the stubs.
#if TIGER_HAS_IME
// ---------------------------------------------------------------- NSTextInput --
// 10.4's input manager is synchronous: it calls -markedRange or
// -firstRectForCharacterRange: and uses the answer before its own call returns, so
// those block on the web process (WebPageProxy::tiger*, 100 ms at most). The
// composition itself goes as WebCore's Editor::setComposition / confirmComposition,
// the same calls upstream's WebViewImpl makes through setCompositionAsync.

- (void)insertText:(id)string
{
    NSString* text = [string isKindOfClass:[NSAttributedString class]] ? [string string] : string;
    if (_hasMarkedText) {
        // The input method commits: a dead key's second key, Kotoeri's Return.
        _hasMarkedText = NO;
        _inputMethodHandledKey = YES;
        if (RefPtr page = [self page])
            page->tigerConfirmComposition(String(text));
        return;
    }
    if (_interpreting) {
        [_interpretedText release];
        _interpretedText = [text copy];
        return;
    }
    [self executeEditCommand:@"InsertText" argument:text];
}

- (void)doCommandBySelector:(SEL)selector
{
    NSString* name = commandNameForSelector(selector);
    if (!name)
        return;
    if (_interpreting) {
        [_interpretedCommands addObject:name];
        return;
    }
    [self executeEditCommand:name argument:nil];
}

// WebViewImpl's compositionUnderlines, the pre-inline-predictions arm: one underline
// per run the input method underlined, thick where it asked for more than a single
// line (Kotoeri's clause being converted). Text with no underline at all still gets
// one thin line, as -[WebHTMLView setMarkedText:] did, so a dead key's accent shows.
static Vector<WebCore::CompositionUnderline> compositionUnderlines(id string, unsigned length)
{
    Vector<WebCore::CompositionUnderline> underlines;
    if ([string isKindOfClass:[NSAttributedString class]]) {
        for (unsigned i = 0; i < length;) {
            NSRange range;
            NSDictionary* attributes = [string attributesAtIndex:i longestEffectiveRange:&range inRange:NSMakeRange(i, length - i)];
            if (NSNumber* style = [attributes objectForKey:NSUnderlineStyleAttributeName]) {
                if ([style intValue])
                    underlines.append(WebCore::CompositionUnderline(range.location, NSMaxRange(range), WebCore::CompositionUnderlineColor::TextColor, WebCore::Color::black, [style intValue] > 1));
            }
            i = NSMaxRange(range);
        }
    }
    if (underlines.isEmpty() && length)
        underlines.append(WebCore::CompositionUnderline(0, length, WebCore::CompositionUnderlineColor::TextColor, WebCore::Color::black, false));
    return underlines;
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange
{
    NSString* text = [string isKindOfClass:[NSAttributedString class]] ? [string string] : string;
    RefPtr page = [self page];
    if (!page)
        return;
    if (_interpreting)
        _inputMethodHandledKey = YES;
    _hasMarkedText = [text length] > 0;
    // Empty marked text is the input method cancelling: Editor::setComposition
    // with an empty string removes the composition.
    page->tigerSetComposition(String(text), compositionUnderlines(string, [text length]), EditingRange(selectedRange));
}

- (void)unmarkText
{
    if (!_hasMarkedText)
        return;
    _hasMarkedText = NO;
    if (RefPtr page = [self page])
        page->tigerConfirmComposition(String());
}

// A click ends a composition where it stands, as in any Cocoa text view.
- (void)abandonMarkedText
{
    if (!_hasMarkedText)
        return;
    [[NSInputManager currentInputManager] markedTextAbandoned:self];
    [self unmarkText];
}

- (BOOL)hasMarkedText { return _hasMarkedText; }

- (NSRange)markedRange
{
    RefPtr page = [self page];
    if (!page || !_hasMarkedText)
        return NSMakeRange(NSNotFound, 0);
    return page->tigerSelectedAndMarkedRanges().second;
}

- (NSRange)selectedRange
{
    RefPtr page = [self page];
    if (!page)
        return NSMakeRange(NSNotFound, 0);
    return page->tigerSelectedAndMarkedRanges().first;
}

- (long)conversationIdentifier { return (long)self; }

- (NSAttributedString*)attributedSubstringFromRange:(NSRange)range
{
    RefPtr page = [self page];
    if (!page || range.location == NSNotFound)
        return nil;
    String string = page->tigerStringForCharacterRange(EditingRange(range));
    if (string.isEmpty())
        return nil;
    return [[[NSAttributedString alloc] initWithString:string.createNSString().get()] autorelease];
}

// Page rects are root-view coordinates, which in this flipped view are view coordinates.
- (NSRect)firstRectForCharacterRange:(NSRange)range
{
    RefPtr page = [self page];
    if (!page || range.location == NSNotFound || ![self window])
        return NSZeroRect;
    auto rect = page->tigerFirstRectForCharacterRange(EditingRange(range)).first;
    NSRect inWindow = [self convertRect:NSMakeRect(rect.x(), rect.y(), rect.width(), rect.height()) toView:nil];
    inWindow.origin = [[self window] convertBaseToScreen:inWindow.origin];
    return inWindow;
}

- (unsigned int)characterIndexForPoint:(NSPoint)screenPoint
{
    RefPtr page = [self page];
    if (!page || ![self window])
        return NSNotFound;
    NSPoint point = [self convertPoint:[[self window] convertScreenToBase:screenPoint] fromView:nil];
    uint64_t location = page->tigerCharacterIndexForPoint(WebCore::IntPoint(point.x, point.y));
    return location == notFound ? NSNotFound : (unsigned)location;
}

- (NSArray*)validAttributesForMarkedText
{
    return [NSArray arrayWithObjects:NSUnderlineStyleAttributeName, NSUnderlineColorAttributeName, nil];
}

// ------------------------------------------------------------------ spelling --
// "Check Spelling While Typing". The page's markers come from TextChecker
// (UIProcess/tiger/TextCheckerTiger.mm) once every web process knows the new
// state; hosted fields are AppKit's own NSTextViews and get the flag directly.

static BOOL continuousSpellCheckingEnabled()
{
    return TextChecker::state().contains(TextCheckerState::ContinuousSpellCheckingEnabled);
}

static void setContinuousSpellCheckingOnHostedEditors(NSView* view, BOOL enabled)
{
    if ([view isKindOfClass:[NSTextView class]])
        [(NSTextView*)view setContinuousSpellCheckingEnabled:enabled];
    NSArray* subviews = [view subviews];
    for (unsigned i = 0; i < [subviews count]; ++i)
        setContinuousSpellCheckingOnHostedEditors([subviews objectAtIndex:i], enabled);
}

- (void)applyContinuousSpellChecking
{
    BOOL enabled = continuousSpellCheckingEnabled();
    // The window's field editor is what edits every hosted NSTextField.
    NSText* fieldEditor = [[self window] fieldEditor:YES forObject:nil];
    if ([fieldEditor isKindOfClass:[NSTextView class]])
        [(NSTextView*)fieldEditor setContinuousSpellCheckingEnabled:enabled];
    setContinuousSpellCheckingOnHostedEditors(self, enabled);
}

// A hosted text area arrives as an NSScrollView around its NSTextView.
- (void)didAddSubview:(NSView*)subview
{
    [super didAddSubview:subview];
    setContinuousSpellCheckingOnHostedEditors(subview, continuousSpellCheckingEnabled());
}

- (void)toggleContinuousSpellChecking:(id)sender
{
    RefPtr page = [self page];
    if (!page)
        return;
    TextChecker::setContinuousSpellCheckingEnabled(!continuousSpellCheckingEnabled());
    page->legacyMainFrameProcess().processPool().textCheckerStateChanged();
    [self applyContinuousSpellChecking];
}

- (BOOL)validateMenuItem:(NSMenuItem*)item
{
    if ([item action] == @selector(toggleContinuousSpellChecking:))
        [item setState:continuousSpellCheckingEnabled() ? NSOnState : NSOffState];
    return YES;
}

#else
// ---------------------------------------------------------------- NSTextInput --

- (void)insertText:(id)string
{
    NSString* text = [string isKindOfClass:[NSAttributedString class]] ? [string string] : string;
    if (_interpreting) {
        [_interpretedText release];
        _interpretedText = [text copy];
        return;
    }
    [self executeEditCommand:@"InsertText" argument:text];
}

- (void)doCommandBySelector:(SEL)selector
{
    NSString* name = commandNameForSelector(selector);
    if (!name)
        return;
    if (_interpreting) {
        [_interpretedCommands addObject:name];
        return;
    }
    [self executeEditCommand:name argument:nil];
}

// No marked text: there is no input-method UI in the page yet. A dead key still
// works because the input manager ends the composition with -insertText:.
// ponytail: real marked text needs the EditorState round-trip that
// PLATFORM(COCOA) && !PLATFORM(TIGER) gets from setCompositionAsync.
- (void)setMarkedText:(id)string selectedRange:(NSRange)range { }
- (void)unmarkText { }
- (BOOL)hasMarkedText { return NO; }
- (NSRange)markedRange { return NSMakeRange(NSNotFound, 0); }
- (NSRange)selectedRange { return NSMakeRange(NSNotFound, 0); }
- (long)conversationIdentifier { return (long)self; }
- (NSAttributedString*)attributedSubstringFromRange:(NSRange)range { return nil; }
- (NSRect)firstRectForCharacterRange:(NSRange)range { return NSZeroRect; }
- (unsigned int)characterIndexForPoint:(NSPoint)point { return NSNotFound; }
- (NSArray*)validAttributesForMarkedText { return [NSArray array]; }

// Nothing is ever marked in this arm.
- (void)abandonMarkedText { }
- (void)applyContinuousSpellChecking { }
#endif // TIGER_HAS_IME

// ------------------------------------------------------- responder-chain edits --
// Cmd-A/C/V/X/Z arrive here from the Edit menu's key equivalents.

- (void)selectAll:(id)sender { [self executeEditCommand:@"SelectAll" argument:nil]; }
- (void)undo:(id)sender { [self executeEditCommand:@"Undo" argument:nil]; }
- (void)redo:(id)sender { [self executeEditCommand:@"Redo" argument:nil]; }
- (void)delete:(id)sender { [self executeEditCommand:@"DeleteBackward" argument:nil]; }

// Copy and paste are plain text through the UI process's NSPasteboard. The web
// process has no pasteboard of its own (platform/tiger64/PasteboardTiger64.cpp is
// inert) and giving it one means a WebPasteboardProxy and three serialization
// inputs.
// ponytail: plain text only, and the page's own Copy/Paste editor commands and the
// JavaScript clipboard API still see an empty pasteboard. Upgrade path: the
// PasteboardStrategy -> WebPasteboardProxy wiring the GTK and WPE ports use.
// 10.4 hands a process launched outside the console session no pasteboard server
// at all ([NSPasteboard generalPasteboard] is nil, and pbcopy/pbpaste are just as
// dead there), so the clipboard falls back to one held in this process. A window
// opened from the Finder gets the real system pasteboard.
// ponytail: the fallback is per-process, so copy between two of these windows
// only works through the real pasteboard. It is the same ceiling every app on the
// box has when pbs is unreachable.
static NSString* sFallbackPasteboardText;

static void writePlainTextToPasteboard(NSString* text)
{
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    if (pasteboard) {
        [pasteboard declareTypes:[NSArray arrayWithObject:NSStringPboardType] owner:nil];
        [pasteboard setString:text forType:NSStringPboardType];
        return;
    }
    [sFallbackPasteboardText release];
    sFallbackPasteboardText = [text copy];
}

static NSString* plainTextFromPasteboard()
{
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    return pasteboard ? [pasteboard stringForType:NSStringPboardType] : sFallbackPasteboardText;
}

- (void)copy:(id)sender
{
    RefPtr page = [self page];
    if (!page || !page->hasSelectedRange())
        return;
    page->getSelectionOrContentsAsString([](const String& selection) {
        if (selection.isEmpty())
            return;
        writePlainTextToPasteboard(selection.createNSString().get());
    });
}

- (void)cut:(id)sender
{
    RefPtr page = [self page];
    if (!page || !page->hasSelectedRange())
        return;
    [self copy:sender];
    // Ordered on the same connection: the selection is read before it is deleted.
    [self executeEditCommand:@"DeleteBackward" argument:nil];
}

- (void)paste:(id)sender
{
    NSString* text = plainTextFromPasteboard();
    if ([text length])
        [self executeEditCommand:@"InsertText" argument:text];
}

- (void)pasteAsPlainText:(id)sender { [self paste:sender]; }


- (void)keyEvent:(NSEvent*)event
{
    if (RefPtr page = [self page])
        page->handleKeyboardEvent(NativeWebKeyboardEvent::create(event, false, false));
}

- (void)keyDown:(NSEvent*)event
{
    if (![self page])
        return;

    if (!_interpretedCommands)
        _interpretedCommands = [[NSMutableArray alloc] init];
    [_interpretedCommands removeAllObjects];
    [_interpretedText release];
    _interpretedText = nil;

    // An input method that is composing owns every key until it commits or cancels.
    BOOL wasComposing = _hasMarkedText;
    _inputMethodHandledKey = NO;
    _interpreting = YES;
    [self interpretKeyEvents:[NSArray arrayWithObject:event]];
    _interpreting = NO;

    if (getenv("TIGER_KEYLOG")) {
        fprintf(stderr, "TIGER-KEY: chars=%s flags=0x%x key=%s -> text=%s commands=%s marked=%d im=%d\n",
            [[event characters] UTF8String], (unsigned)[event modifierFlags],
            [[NSApp keyWindow] isEqual:[self window]] ? "yes" : "no",
            _interpretedText ? [_interpretedText UTF8String] : "(none)",
            [[_interpretedCommands componentsJoinedByString:@","] UTF8String], _hasMarkedText, _inputMethodHandledKey);
    }

    // The input method took the key (marked text set, changed or committed), so the
    // raw event must not reach the page as well: it would insert the romaji, or the
    // key under a dead key's accent, next to the composition.
    // ponytail: upstream sends such a keydown as keyCode 229 (handledByInputMethod),
    // which this port's WebKeyboardEvent does not carry; the page sees no keydown for
    // composition keys at all. Upgrade path: the flag in WebEvent.serialization.in.
    if (wasComposing || _inputMethodHandledKey) {
        unsigned count = [_interpretedCommands count];
        for (unsigned i = 0; i < count; ++i)
            [self executeEditCommand:[_interpretedCommands objectAtIndex:i] argument:nil];
        if (_interpretedText)
            [self executeEditCommand:@"InsertText" argument:_interpretedText];
        return;
    }

    // A dead-key composition ends with text the event itself does not carry, so
    // the raw event would insert nothing (or the wrong thing) in the page.
    BOOL textIsNew = _interpretedText && ![_interpretedText isEqualToString:[event characters]];
    if (!textIsNew)
        [self keyEvent:event];

    unsigned count = [_interpretedCommands count];
    for (unsigned i = 0; i < count; ++i)
        [self executeEditCommand:[_interpretedCommands objectAtIndex:i] argument:nil];
    if (textIsNew)
        [self executeEditCommand:@"InsertText" argument:_interpretedText];
}

- (void)keyUp:(NSEvent*)event { [self keyEvent:event]; }
- (void)flagsChanged:(NSEvent*)event { [self keyEvent:event]; }


- (void)setFocused:(BOOL)focused
{
    if (!_webView)
        return;
    auto state = _webView->viewState();
    state.set(WebCore::ActivityState::IsFocused, focused);
    _webView->setViewState(state);
}
- (BOOL)becomeFirstResponder { [self setFocused:YES]; return YES; }
- (BOOL)resignFirstResponder { [self setFocused:NO]; return YES; }

@end
