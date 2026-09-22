/*
 * Copyright (C) 2026 WebKitTiger contributors. BSD-2-Clause, as the rest of WebKit.
 *
 * TigerBrowser2: the WebKit2 browser chrome for 10.4. A window with back/forward/
 * reload, an address field and a status line around a TigerWK2View; title, URL,
 * progress and button state come from WebPageProxy's PageLoadState. This is the
 * harness (TigerWK2App) plus chrome, on the same four processes.
 *
 *   TigerBrowser2 [url] [seconds-before-exit]
 */

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#include "config.h"

#include "APINavigation.h"
#include "APIPageConfiguration.h"
#include "APIProcessPoolConfiguration.h"
#include "PageLoadState.h"
#include "TigerCrashCatcher.h"
#include "TigerWebView.h"
#include "WebBackForwardList.h"
#include "WebPageProxy.h"
#include "WebPreferences.h"
#include "WebProcessPool.h"
#include "WebsiteDataStore.h"
#import "TigerWK2View.h"
#include <WebCore/IntRect.h>
#include <WebCore/Region.h>
#include <wtf/MainThread.h>
#include <wtf/RefCounted.h>
#include <wtf/RunLoop.h>
#include <wtf/text/WTFString.h>

using namespace WebKit;

@class TigerBrowserWindow;

// PageLoadState -> chrome. Every pure virtual is spelled out; only the didChange*
// ones we show do anything.
class ChromeLoadObserver final : public PageLoadState::Observer, public RefCounted<ChromeLoadObserver> {
public:
    static Ref<ChromeLoadObserver> create(TigerBrowserWindow* window) { return adoptRef(*new ChromeLoadObserver(window)); }
    void ref() const final { RefCounted<ChromeLoadObserver>::ref(); }
    void deref() const final { RefCounted<ChromeLoadObserver>::deref(); }
    void willChangeIsLoading() final { }
    void didChangeIsLoading() final { update(); }
    void willChangeTitle() final { }
    void didChangeTitle() final { update(); }
    void willChangeActiveURL() final { }
    void didChangeActiveURL() final { update(); }
    void willChangeHasOnlySecureContent() final { }
    void didChangeHasOnlySecureContent() final { }
    void willChangeEstimatedProgress() final { }
    void didChangeEstimatedProgress() final { update(); }
    void willChangeCanGoBack() final { }
    void didChangeCanGoBack() final { update(); }
    void willChangeCanGoForward() final { }
    void didChangeCanGoForward() final { update(); }
    void willChangeNetworkRequestsInProgress() final { }
    void didChangeNetworkRequestsInProgress() final { }
    void willChangeCertificateInfo() final { }
    void didChangeCertificateInfo() final { }
    void willChangeWebProcessIsResponsive() final { }
    void didChangeWebProcessIsResponsive() final { update(); }
    void didSwapWebProcesses() final { update(); }
private:
    explicit ChromeLoadObserver(TigerBrowserWindow* window) : m_window(window) { }
    void update();
    TigerBrowserWindow* m_window; // the window outlives the page
};

@interface TigerBrowserWindow : NSObject {
    NSWindow* _window;
    TigerWK2View* _view;
    NSTextField* _address;
    NSTextField* _status;
    NSButton* _back;
    NSButton* _forward;
    NSButton* _reload;
    RefPtr<WebProcessPool> _pool;
    RefPtr<TigerWebView> _webView;
    RefPtr<ChromeLoadObserver> _observer;
}
- (id)initWithURL:(NSString*)url;
- (void)updateChrome;
@end

void ChromeLoadObserver::update()
{
    [m_window updateChrome];
}

@implementation TigerBrowserWindow

- (id)initWithURL:(NSString*)url
{
    self = [super init];
    if (!self)
        return nil;

    const float barHeight = 34, statusHeight = 18, pad = 6;
    NSRect frame = NSMakeRect(80, 80, 960, 700);
    _window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:(NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask | NSResizableWindowMask)
        backing:NSBackingStoreBuffered defer:NO];
    [_window setTitle:@"TigerBrowser"];
    [_window setReleasedWhenClosed:NO];
    NSView* content = [_window contentView];
    NSRect bounds = [content bounds];

    // Toolbar row, pinned to the top.
    NSView* bar = [[[NSView alloc] initWithFrame:NSMakeRect(0, bounds.size.height - barHeight, bounds.size.width, barHeight)] autorelease];
    [bar setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];
    struct { NSButton** slot; NSString* title; SEL action; float x; } buttons[] = {
        { &_back, @"<", @selector(goBack:), pad },
        { &_forward, @">", @selector(goForward:), pad + 38 },
        { &_reload, @"R", @selector(reload:), pad + 76 },
    };
    for (unsigned i = 0; i < 3; ++i) {
        NSButton* button = [[[NSButton alloc] initWithFrame:NSMakeRect(buttons[i].x, 5, 34, 24)] autorelease];
        [button setBezelStyle:NSRoundedBezelStyle];
        [button setTitle:buttons[i].title];
        [button setTarget:self];
        [button setAction:buttons[i].action];
        [button setAutoresizingMask:NSViewMaxXMargin];
        [bar addSubview:button];
        *buttons[i].slot = button;
    }
    float addressX = pad + 76 + 34 + pad;
    _address = [[[NSTextField alloc] initWithFrame:NSMakeRect(addressX, 5, bounds.size.width - addressX - pad, 24)] autorelease];
    [_address setAutoresizingMask:NSViewWidthSizable];
    [_address setTarget:self];
    [_address setAction:@selector(loadFromAddress:)];
    [_address setStringValue:url];
    [bar addSubview:_address];
    [content addSubview:bar];

    // Status line, pinned to the bottom.
    _status = [[[NSTextField alloc] initWithFrame:NSMakeRect(pad, 1, bounds.size.width - 2 * pad, statusHeight - 2)] autorelease];
    [_status setBezeled:NO];
    [_status setDrawsBackground:NO];
    [_status setEditable:NO];
    [_status setSelectable:NO];
    [_status setFont:[NSFont systemFontOfSize:11]];
    [_status setAutoresizingMask:(NSViewWidthSizable | NSViewMaxYMargin)];
    [content addSubview:_status];

    // The page, everything in between.
    _view = [[TigerWK2View alloc] initWithFrame:NSMakeRect(0, statusHeight, bounds.size.width, bounds.size.height - barHeight - statusHeight)];
    [_view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [content addSubview:_view];

    [_window orderFrontRegardless];
    [NSApp activateIgnoringOtherApps:YES];

    // WebKit2: one process pool, one page, the same preferences as the harness.
    auto poolConfiguration = API::ProcessPoolConfiguration::create();
    _pool = WebProcessPool::create(poolConfiguration);
    auto pageConfiguration = API::PageConfiguration::create();
    pageConfiguration->setProcessPool(_pool.get());
    pageConfiguration->setWebsiteDataStore(&WebsiteDataStore::defaultDataStore());
    // Same switches as the harness: TIGER_FAITHFUL=1 composites in the GPU process's CA
    // scene (read back into the BackingStore); TIGER_GPU_DOM=1 additionally paints the DOM
    // there. Default is fast mode: software WC in the web process.
    bool faithful = getenv("TIGER_FAITHFUL") && !strcmp(getenv("TIGER_FAITHFUL"), "1");
    bool gpuDOM = faithful && getenv("TIGER_GPU_DOM") && !strcmp(getenv("TIGER_GPU_DOM"), "1");
    pageConfiguration->preferences().setUseGPUProcessForDOMRenderingEnabled(gpuDOM);
    pageConfiguration->preferences().setAcceleratedCompositingEnabled(faithful);
    _webView = TigerWebView::create(pageConfiguration.get());
    if (!_webView) {
        fprintf(stderr, "TigerBrowser2: could not create the web view\n");
        return self;
    }
    [_view attachWebView:_webView.get()];
    NSRect viewBounds = [_view bounds];
    _webView->setViewSize(WebCore::IntSize(viewBounds.size.width, viewBounds.size.height));
    if (RefPtr page = _webView->page()) {
        _observer = ChromeLoadObserver::create(self);
        page->pageLoadState().addObserver(*_observer);
        page->setViewNeedsDisplay(WebCore::Region(WebCore::IntRect(0, 0, viewBounds.size.width, viewBounds.size.height)));
        page->loadRequest(URL { String::fromUTF8([url UTF8String]) });
    }
    [_window makeFirstResponder:_view];
    return self;
}

- (void)updateChrome
{
    RefPtr page = _webView ? _webView->page() : nullptr;
    if (!page)
        return;
    auto& state = page->pageLoadState();
    String title = state.title();
    [_window setTitle:title.isEmpty() ? @"TigerBrowser" : (NSString*)title.createNSString().get()];
    if (![[_window firstResponder] isKindOfClass:[NSTextView class]] || [_address currentEditor] == nil)
        [_address setStringValue:(NSString*)state.activeURL().string().createNSString().get()];
    [_back setEnabled:state.canGoBack()];
    [_forward setEnabled:state.canGoForward()];
    if (state.isLoading())
        [_status setStringValue:[NSString stringWithFormat:@"Loading… %d%%", (int)(state.estimatedProgress() * 100)]];
    else
        [_status setStringValue:@""];
}

- (void)loadFromAddress:(id)sender
{
    RefPtr page = _webView ? _webView->page() : nullptr;
    if (!page)
        return;
    NSString* text = [[_address stringValue] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (![text length])
        return;
    if ([text rangeOfString:@"://"].location == NSNotFound)
        text = [@"http://" stringByAppendingString:text];
    page->loadRequest(URL { String::fromUTF8([text UTF8String]) });
    [_window makeFirstResponder:_view];
}

- (void)goBack:(id)sender { if (RefPtr page = _webView ? _webView->page() : nullptr) page->goBack(); }
- (void)goForward:(id)sender { if (RefPtr page = _webView ? _webView->page() : nullptr) page->goForward(); }
- (void)reload:(id)sender { if (RefPtr page = _webView ? _webView->page() : nullptr) page->reload({ }); }
- (void)focusAddress:(id)sender { [_window makeFirstResponder:_address]; [_address selectText:self]; }

@end

static void buildMenus(TigerBrowserWindow* browser)
{
    NSMenu* menubar = [[NSMenu alloc] initWithTitle:@""];
    NSMenuItem* appItem = [[NSMenuItem alloc] initWithTitle:@"" action:NULL keyEquivalent:@""];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"TigerBrowser"];
    [appMenu addItemWithTitle:@"Quit TigerBrowser" action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [menubar addItem:appItem];
    NSMenuItem* fileItem = [[NSMenuItem alloc] initWithTitle:@"File" action:NULL keyEquivalent:@""];
    NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem* openLocation = [[NSMenuItem alloc] initWithTitle:@"Open Location…" action:@selector(focusAddress:) keyEquivalent:@"l"];
    [openLocation setTarget:browser];
    [fileMenu addItem:openLocation];
    [fileItem setSubmenu:fileMenu];
    [menubar addItem:fileItem];
    NSMenuItem* historyItem = [[NSMenuItem alloc] initWithTitle:@"History" action:NULL keyEquivalent:@""];
    NSMenu* historyMenu = [[NSMenu alloc] initWithTitle:@"History"];
    NSMenuItem* back = [[NSMenuItem alloc] initWithTitle:@"Back" action:@selector(goBack:) keyEquivalent:@"["];
    [back setTarget:browser];
    NSMenuItem* forward = [[NSMenuItem alloc] initWithTitle:@"Forward" action:@selector(goForward:) keyEquivalent:@"]"];
    [forward setTarget:browser];
    NSMenuItem* reload = [[NSMenuItem alloc] initWithTitle:@"Reload Page" action:@selector(reload:) keyEquivalent:@"r"];
    [reload setTarget:browser];
    [historyMenu addItem:back];
    [historyMenu addItem:forward];
    [historyMenu addItem:reload];
    [historyItem setSubmenu:historyMenu];
    [menubar addItem:historyItem];
    [NSApp setMainMenu:menubar];
}

int main(int argc, const char* argv[])
{
    WebKit::installTigerCrashCatcher();
    NSString* url = argc > 1 ? [NSString stringWithUTF8String:argv[1]] : @"http://example.com/";
    double secondsBeforeExit = argc > 2 ? atof(argv[2]) : 0;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    WTF::initializeMainThread();
    [NSApplication sharedApplication];
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    TransformProcessType(&psn, kProcessTransformToForegroundApplication);
    TigerBrowserWindow* browser = [[TigerBrowserWindow alloc] initWithURL:url];
    buildMenus(browser);
    if (secondsBeforeExit > 0) {
        [NSTimer scheduledTimerWithTimeInterval:secondsBeforeExit target:NSApp
            selector:@selector(terminate:) userInfo:nil repeats:NO];
    }
    [NSApp run];
    [pool release];
    return 0;
}
