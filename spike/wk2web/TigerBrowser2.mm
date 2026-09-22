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
#include "NativeWebWheelEvent.h"
#include "WebWheelEvent.h"
#include <WebCore/ScrollTypes.h>
#include <WebCore/Scrollbar.h>
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
- (void)runScriptStep:(NSTimer*)timer;
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

    // Key, not just front: -interpretKeyEvents: goes through NSInputManager, which
    // wants a key window with a first responder before it will call back.
    [_window makeKeyAndOrderFront:nil];
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

// ------------------------------------------------------ scripted interaction --
// TIGER_SCRIPT="wait 5; click 300,400; click shift 400,400; drag 20,20 300,20;
//              type hello; key return; keymod cmd a;
//              keymod opt left; keymod shift opt right; scroll 0,-300; shot /path.png; load URL;
//              move 200,150; wheel 400,300 0,-3"
// Coordinates are view points, y down, as the page sees them. Events are real NSEvents
// posted to the view's handlers, so they take the same path as the user's. This is how
// login flows and scrolling get exercised from a harness with nobody at the keyboard.
static NSMutableArray* scriptSteps;
static unsigned scriptIndex;

static unsigned modifierMaskFromWords(NSArray* words, unsigned upTo)
{
    unsigned flags = 0;
    for (unsigned i = 0; i < upTo; ++i) {
        NSString* word = [[words objectAtIndex:i] lowercaseString];
        if ([word isEqualToString:@"cmd"] || [word isEqualToString:@"command"])
            flags |= NSCommandKeyMask;
        else if ([word isEqualToString:@"opt"] || [word isEqualToString:@"alt"] || [word isEqualToString:@"option"])
            flags |= NSAlternateKeyMask;
        else if ([word isEqualToString:@"shift"])
            flags |= NSShiftKeyMask;
        else if ([word isEqualToString:@"ctrl"] || [word isEqualToString:@"control"])
            flags |= NSControlKeyMask;
    }
    return flags;
}

static NSPoint pointFromString(NSString* spec)
{
    NSArray* xy = [spec componentsSeparatedByString:@","];
    if ([xy count] < 2)
        return NSZeroPoint;
    return NSMakePoint([[xy objectAtIndex:0] floatValue], [[xy objectAtIndex:1] floatValue]);
}

- (NSEvent*)mouseEventOfType:(NSEventType)type at:(NSPoint)viewPoint flags:(unsigned)flags
{
    NSPoint windowPoint = [_view convertPoint:viewPoint toView:nil];
    return [NSEvent mouseEventWithType:type location:windowPoint modifierFlags:flags timestamp:[NSDate timeIntervalSinceReferenceDate]
        windowNumber:[_window windowNumber] context:[_window graphicsContext] eventNumber:0 clickCount:1 pressure:0];
}

- (NSEvent*)mouseEventOfType:(NSEventType)type at:(NSPoint)viewPoint
{
    return [self mouseEventOfType:type at:viewPoint flags:0];
}

// "click 100,40", "click shift 300,40": modifiers then one point.
- (void)clickAt:(NSString*)spec
{
    NSArray* words = [spec componentsSeparatedByString:@" "];
    if (![words count])
        return;
    unsigned flags = modifierMaskFromWords(words, [words count] - 1);
    NSPoint p = pointFromString([words objectAtIndex:[words count] - 1]);
    [_window makeFirstResponder:_view];
    [_view mouseMoved:[self mouseEventOfType:NSMouseMoved at:p flags:flags]];
    [_view mouseDown:[self mouseEventOfType:NSLeftMouseDown at:p flags:flags]];
    [_view mouseUp:[self mouseEventOfType:NSLeftMouseUp at:p flags:flags]];
}

// "wheel 400,300 0,-3": a wheel tick at a point, deltas in lines. 10.4 has no public
// constructor for a scroll-wheel NSEvent, so the WebWheelEvent is built here and fed to
// the same WebPageProxy entry point -[TigerWK2View scrollWheel:] uses, coalescer included.
- (void)wheelAt:(NSString*)spec
{
    NSArray* words = [spec componentsSeparatedByString:@" "];
    if ([words count] < 2)
        return;
    NSPoint p = pointFromString([words objectAtIndex:0]);
    NSPoint ticks = pointFromString([words objectAtIndex:1]);
    RefPtr page = _webView ? _webView->page() : nullptr;
    if (!page)
        return;
    float perLine = static_cast<float>(WebCore::Scrollbar::pixelsPerLineStep());
    if (getenv("TIGER_INPUTLOG"))
        fprintf(stderr, "TIGER-INPUT: wheel dy=%.1f at %.0f,%.0f\n", (double)ticks.y, p.x, p.y);
    auto wheelEvent = WebWheelEvent::create(
        { WebEventType::Wheel, { }, MonotonicTime::now() },
        {
            .position = WebCore::IntPoint(p.x, p.y),
            .globalPosition = WebCore::IntPoint(p.x, p.y),
            .delta = WebCore::FloatSize(ticks.x * perLine, ticks.y * perLine),
            .wheelTicks = WebCore::FloatSize(ticks.x, ticks.y),
            .granularity = WebWheelEvent::Granularity::ScrollByPixelWheelEvent,
        });
    page->handleNativeWheelEvent(NativeWebWheelEvent::create(wheelEvent.get()));
}

// "drag 20,20 300,20": press, a few intermediate drags, release.
- (void)dragFromTo:(NSString*)spec
{
    NSArray* words = [spec componentsSeparatedByString:@" "];
    if ([words count] < 2)
        return;
    NSPoint from = pointFromString([words objectAtIndex:0]);
    NSPoint to = pointFromString([words objectAtIndex:1]);
    [_window makeFirstResponder:_view];
    [_view mouseDown:[self mouseEventOfType:NSLeftMouseDown at:from]];
    for (unsigned step = 1; step <= 8; ++step) {
        NSPoint p = NSMakePoint(from.x + (to.x - from.x) * step / 8, from.y + (to.y - from.y) * step / 8);
        [_view mouseDragged:[self mouseEventOfType:NSLeftMouseDragged at:p]];
    }
    [_view mouseUp:[self mouseEventOfType:NSLeftMouseUp at:to]];
}

// One place where a scripted key becomes an NSEvent. Command combinations go to
// the main menu first, exactly as NSApplication does for a real key press, so the
// Edit menu's key equivalents (and the responder chain behind them) are what the
// script exercises.
static BOOL keyForName(NSString* name, unichar* character, unsigned short* code, unsigned* extraFlags)
{
    struct { const char* name; unichar character; unsigned short code; unsigned flags; } keys[] = {
        { "return", '\r', 36, 0 },
        { "enter", '\r', 36, 0 },
        { "tab", '\t', 48, 0 },
        { "space", ' ', 49, 0 },
        { "backspace", 0x7f, 51, 0 },
        { "escape", 0x1b, 53, 0 },
        { "delete", NSDeleteFunctionKey, 117, NSFunctionKeyMask },
        { "left", NSLeftArrowFunctionKey, 123, NSFunctionKeyMask | NSNumericPadKeyMask },
        { "right", NSRightArrowFunctionKey, 124, NSFunctionKeyMask | NSNumericPadKeyMask },
        { "down", NSDownArrowFunctionKey, 125, NSFunctionKeyMask | NSNumericPadKeyMask },
        { "up", NSUpArrowFunctionKey, 126, NSFunctionKeyMask | NSNumericPadKeyMask },
        { "home", NSHomeFunctionKey, 115, NSFunctionKeyMask },
        { "end", NSEndFunctionKey, 119, NSFunctionKeyMask },
        { "pageup", NSPageUpFunctionKey, 116, NSFunctionKeyMask },
        { "pagedown", NSPageDownFunctionKey, 121, NSFunctionKeyMask },
    };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        if ([name isEqualToString:[NSString stringWithUTF8String:keys[i].name]]) {
            *character = keys[i].character;
            *code = keys[i].code;
            *extraFlags = keys[i].flags;
            return YES;
        }
    }
    if (![name length])
        return NO;
    *character = [name characterAtIndex:0];
    *code = 0; // the binding manager and the page both read characters, not codes
    *extraFlags = 0;
    return YES;
}

- (void)sendKey:(NSString*)characters flags:(unsigned)flags code:(unsigned short)code
{
    NSEvent* down = [NSEvent keyEventWithType:NSKeyDown location:NSZeroPoint modifierFlags:flags
        timestamp:[NSDate timeIntervalSinceReferenceDate] windowNumber:[_window windowNumber] context:nil
        characters:characters charactersIgnoringModifiers:characters isARepeat:NO keyCode:code];
    if ((flags & NSCommandKeyMask) && [[NSApp mainMenu] performKeyEquivalent:down])
        return;
    [_view keyDown:down];
    [_view keyUp:[NSEvent keyEventWithType:NSKeyUp location:NSZeroPoint modifierFlags:flags
        timestamp:[NSDate timeIntervalSinceReferenceDate] windowNumber:[_window windowNumber] context:nil
        characters:characters charactersIgnoringModifiers:characters isARepeat:NO keyCode:code]];
}

// "cmd a", "opt left", "shift opt right", "cmd shift z": modifiers then one key name.
- (void)sendKeyCombination:(NSString*)spec
{
    NSArray* words = [spec componentsSeparatedByString:@" "];
    if (![words count])
        return;
    unsigned flags = modifierMaskFromWords(words, [words count] - 1);
    unichar character = 0;
    unsigned short code = 0;
    unsigned extraFlags = 0;
    if (!keyForName([words objectAtIndex:[words count] - 1], &character, &code, &extraFlags))
        return;
    // -characters and -charactersIgnoringModifiers are both the bare key: the key
    // bindings and the menu match on the bare one, and no verb here wants the
    // glyph Option would actually type.
    [self sendKey:[NSString stringWithCharacters:&character length:1] flags:(flags | extraFlags) code:code];
}

- (void)typeString:(NSString*)text
{
    for (NSUInteger i = 0; i < [text length]; ++i)
        [self sendKey:[text substringWithRange:NSMakeRange(i, 1)] flags:0 code:0];
}

- (void)runScriptStep:(NSTimer*)timer
{
    if (scriptIndex >= [scriptSteps count])
        return;
    NSString* step = [[scriptSteps objectAtIndex:scriptIndex++] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    NSArray* parts = [step componentsSeparatedByString:@" "];
    NSString* verb = [parts objectAtIndex:0];
    NSString* rest = [parts count] > 1 ? [step substringFromIndex:[verb length] + 1] : @"";
    double delay = 0.3;
    fprintf(stderr, "TIGER script: %s\n", [step UTF8String]);
    if ([verb isEqualToString:@"wait"])
        delay = [rest doubleValue];
    else if ([verb isEqualToString:@"click"])
        [self clickAt:rest];
    else if ([verb isEqualToString:@"drag"])
        [self dragFromTo:rest];
    else if ([verb isEqualToString:@"type"])
        [self typeString:rest];
    else if ([verb isEqualToString:@"key"] || [verb isEqualToString:@"keymod"])
        [self sendKeyCombination:rest];
    else if ([verb isEqualToString:@"move"])
        [_view mouseMoved:[self mouseEventOfType:NSMouseMoved at:pointFromString(rest)]];
    else if ([verb isEqualToString:@"wheel"])
        [self wheelAt:rest];
    else if ([verb isEqualToString:@"scroll"]) {
        NSArray* xy = [rest componentsSeparatedByString:@","];
        // No public constructor for scroll-wheel NSEvents on 10.4 and WebWheelEvent's is
        // protected: scroll the page through the proxy, one page per step.
        if (RefPtr page = _webView ? _webView->page() : nullptr) {
            float dy = [[xy objectAtIndex:1] floatValue];
            page->scrollBy(dy < 0 ? WebCore::ScrollDirection::ScrollDown : WebCore::ScrollDirection::ScrollUp, WebCore::ScrollGranularity::Page);
        }
    } else if ([verb isEqualToString:@"shot"]) {
        NSString* cmd = [NSString stringWithFormat:@"/usr/sbin/screencapture -x '%@'", rest];
        system([cmd UTF8String]);
    } else if ([verb isEqualToString:@"load"]) {
        if (RefPtr page = _webView ? _webView->page() : nullptr)
            page->loadRequest(URL { String::fromUTF8([rest UTF8String]) });
    }
    [NSTimer scheduledTimerWithTimeInterval:delay target:self selector:@selector(runScriptStep:) userInfo:nil repeats:NO];
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
    // Edit. Every item targets the first responder (target nil), so Cmd-Z/X/C/V/A
    // reach TigerWK2View through the responder chain, the same route a real Cocoa
    // text view takes.
    NSMenuItem* editItem = [[NSMenuItem alloc] initWithTitle:@"Edit" action:NULL keyEquivalent:@""];
    NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    struct { NSString* title; SEL action; NSString* key; unsigned mask; } editItems[] = {
        { @"Undo", @selector(undo:), @"z", NSCommandKeyMask },
        { @"Redo", @selector(redo:), @"z", NSCommandKeyMask | NSShiftKeyMask },
        { nil, NULL, nil, 0 },
        { @"Cut", @selector(cut:), @"x", NSCommandKeyMask },
        { @"Copy", @selector(copy:), @"c", NSCommandKeyMask },
        { @"Paste", @selector(paste:), @"v", NSCommandKeyMask },
        { @"Delete", @selector(delete:), @"", 0 },
        { @"Select All", @selector(selectAll:), @"a", NSCommandKeyMask },
    };
    for (unsigned i = 0; i < sizeof(editItems) / sizeof(editItems[0]); ++i) {
        if (!editItems[i].title) {
            [editMenu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSMenuItem* item = [[[NSMenuItem alloc] initWithTitle:editItems[i].title
            action:editItems[i].action keyEquivalent:editItems[i].key] autorelease];
        [item setKeyEquivalentModifierMask:editItems[i].mask];
        [editMenu addItem:item];
    }
    [editItem setSubmenu:editMenu];
    [menubar addItem:editItem];

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
    if (const char* script = getenv("TIGER_SCRIPT")) {
        scriptSteps = [[[NSString stringWithUTF8String:script] componentsSeparatedByString:@";"] mutableCopy];
        scriptIndex = 0;
        [NSTimer scheduledTimerWithTimeInterval:0.5 target:browser selector:@selector(runScriptStep:) userInfo:nil repeats:NO];
    }
    if (secondsBeforeExit > 0) {
        [NSTimer scheduledTimerWithTimeInterval:secondsBeforeExit target:NSApp
            selector:@selector(terminate:) userInfo:nil repeats:NO];
    }
    [NSApp run];
    [pool release];
    return 0;
}
