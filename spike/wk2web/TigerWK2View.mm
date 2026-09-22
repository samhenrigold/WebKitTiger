/*
 * Copyright (C) 2026 WebKitTiger contributors. BSD-2-Clause, as the rest of WebKit.
 */

#include "config.h"
#import "TigerWK2View.h"

#include "NativeWebKeyboardEvent.h"
#include "NativeWebMouseEvent.h"
#include "NativeWebWheelEvent.h"
#include "WebPageProxy.h"
#include <wtf/text/WTFString.h>
#include <WebCore/IntRect.h>

using namespace WebKit;

@implementation TigerWK2View

- (WebKit::TigerWebView*)webView { return _webView.get(); }

- (void)attachWebView:(TigerWebView*)view
{
    _webView = view;
    if (_webView)
        _webView->setNSView(self);
}

// The web process paints in view coordinates, y down. An NSView that is not
// flipped would hand -drawRect: a y-up context and the page would come out
// upside down.
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque { return YES; }

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
}

- (void)mouseEvent:(NSEvent*)event
{
    if (!_webView || !_webView->page())
        return;
    _webView->page()->handleMouseEvent(NativeWebMouseEvent::create(event, nil, self, WebEventInputSource::UserDriven));
}
- (void)mouseDown:(NSEvent*)event { [self mouseEvent:event]; }
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
- (void)copy:(id)sender
{
    RefPtr page = [self page];
    if (!page || !page->hasSelectedRange())
        return;
    page->getSelectionOrContentsAsString([](const String& selection) {
        if (selection.isEmpty())
            return;
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard declareTypes:[NSArray arrayWithObject:NSStringPboardType] owner:nil];
        [pasteboard setString:selection.createNSString().get() forType:NSStringPboardType];
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
    NSString* text = [[NSPasteboard generalPasteboard] stringForType:NSStringPboardType];
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

    _interpreting = YES;
    [self interpretKeyEvents:[NSArray arrayWithObject:event]];
    _interpreting = NO;

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
