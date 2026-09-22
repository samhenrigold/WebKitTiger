/*
 * Copyright (C) 2026 WebKitTiger contributors. BSD-2-Clause, as the rest of WebKit.
 */

#include "config.h"
#import "TigerWK2View.h"

#include "NativeWebKeyboardEvent.h"
#include "NativeWebMouseEvent.h"
#include "NativeWebWheelEvent.h"
#include "WebPageProxy.h"
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

// No NSTextInput here: keyDown goes straight to the page and WebPage::
// handleEditingKeyboardEvent inserts the text (the pagedriver path). Dead keys
// and input methods are the chrome's job when this moves into TigerBrowser.
- (void)keyEvent:(NSEvent*)event
{
    if (!_webView || !_webView->page())
        return;
    _webView->page()->handleKeyboardEvent(NativeWebKeyboardEvent::create(event, false, false));
}
- (void)keyDown:(NSEvent*)event { [self keyEvent:event]; }
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
