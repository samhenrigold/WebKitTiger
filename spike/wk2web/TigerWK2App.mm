/*
 * Copyright (C) 2026 WebKitTiger.
 *
 * TIGER: the first real UI process. An AppKit app that launches the x86_64
 * TigerWebProcess and TigerNetworkProcess through ProcessLauncherTiger, creates a
 * WebPageProxy with a PageClient over an NSView, loads a URL and shows the page.
 *
 * The picture comes back over the WC path's NON-accelerated arm: the web process
 * paints into a ShareableBitmap, DrawingAreaProxyWC composites it onto a
 * BackingStore (UIProcess/cg/BackingStoreCG.mm, written for this), and -drawRect:
 * blits that. The GPU process and its CALayer scene are not in this picture --
 * when gpu32b's TigerGPUProcess links, the same app gets the accelerated arm by
 * the web process entering compositing mode, and nothing here changes.
 *
 * This is deliberately NOT spike/TigerBrowser/TigerBrowser.m. That is the chrome
 * -- toolbar, find bar, menus, NSTextInput -- hosting a CARenderer and a stub
 * page, and it is where this ends up. Bringing WebKit2 up inside it at the same
 * time would mean debugging two unknowns at once.
 *
 *   TigerWK2App <url> [seconds-before-exit]
 */

#import <AppKit/AppKit.h>

#include "config.h"

#include "APINavigation.h"
#include "APIPageConfiguration.h"
#include "APIProcessPoolConfiguration.h"
#include "NativeWebKeyboardEvent.h"
#include "NativeWebMouseEvent.h"
#include "NativeWebWheelEvent.h"
#include "TigerWebView.h"
#include "WebPageProxy.h"
#include "WebPreferences.h"
#include "WebProcessPool.h"
#include "WebsiteDataStore.h"

#include <WebCore/IntRect.h>
#include <WebCore/PlatformKeyboardEvent.h>
#include <WebCore/Region.h>
#include <wtf/MainThread.h>
#include <wtf/RunLoop.h>
#include <wtf/text/WTFString.h>

using namespace WebKit;

// ---------------------------------------------------------------- the view --

@interface TigerWK2View : NSView {
    RefPtr<TigerWebView> _webView;
}
- (void)attachWebView:(TigerWebView*)view;
@end

@implementation TigerWK2View

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

// ------------------------------------------------------------------- main --

static RefPtr<TigerWebView> createWebView(const String& url, NSView* container)
{
    auto processPoolConfiguration = API::ProcessPoolConfiguration::create();
    Ref processPool = WebProcessPool::create(processPoolConfiguration);

    auto pageConfiguration = API::PageConfiguration::create();
    pageConfiguration->setProcessPool(processPool.ptr());
    pageConfiguration->setWebsiteDataStore(&WebsiteDataStore::defaultDataStore());

    // The same preference pagedriver sets, and for the same reason: with DOM
    // rendering remote the web process produces display-list items for a GPU
    // process, and there is not one yet. Off means it rasterizes locally with
    // cairo and hands over a ShareableBitmap the BackingStore can composite.
    pageConfiguration->preferences().setUseGPUProcessForDOMRenderingEnabled(false);
    pageConfiguration->preferences().setAcceleratedCompositingEnabled(false);

    auto webView = TigerWebView::create(pageConfiguration.get());
    if (!webView)
        return nullptr;

    [(TigerWK2View*)container attachWebView:webView.get()];

    NSRect bounds = [container bounds];
    webView->setViewSize(WebCore::IntSize(bounds.size.width, bounds.size.height));
    if (RefPtr page = webView->page()) {
        page->setViewNeedsDisplay(WebCore::Region(WebCore::IntRect(0, 0, bounds.size.width, bounds.size.height)));
        page->loadRequest(URL { url });
    }
    return webView;
}

int main(int argc, const char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <url> [seconds-before-exit]\n", argv[0]);
        return 2;
    }
    String url = String::fromUTF8(argv[1]);
    double secondsBeforeExit = argc > 2 ? atof(argv[2]) : 0;

    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    WTF::initializeMainThread();

    [NSApplication sharedApplication];
    // TIGER: setActivationPolicy: is 10.6. TransformProcessType is the 10.3+ public way
    // to make a bare executable a foreground app with a menu bar and Dock tile.
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    TransformProcessType(&psn, kProcessTransformToForegroundApplication);

    NSRect frame = NSMakeRect(80, 80, 800, 600);
    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:(NSTitledWindowMask | NSClosableWindowMask | NSResizableWindowMask)
        backing:NSBackingStoreBuffered defer:NO];
    [window setTitle:@"TigerWK2"];

    TigerWK2View* view = [[TigerWK2View alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];
    [window setContentView:view];

    // spike/CAHost's recipe for getting a window in front of the console session
    // over ssh: order front regardless, then activate ignoring other apps.
    [window orderFrontRegardless];
    [NSApp activateIgnoringOtherApps:YES];

    auto webView = createWebView(url, view);
    if (!webView) {
        fprintf(stderr, "TigerWK2App: could not create the web view\n");
        return 1;
    }
    fprintf(stderr, "TigerWK2App: loading %s\n", url.utf8().data());

    if (secondsBeforeExit > 0) {
        [NSTimer scheduledTimerWithTimeInterval:secondsBeforeExit target:NSApp
            selector:@selector(terminate:) userInfo:nil repeats:NO];
    }

    [NSApp run];
    [pool release];
    return 0;
}
