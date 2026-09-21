// TigerBrowser - minimal Cocoa shell around the classic WebKit1 WebView API.
//
// This is the browser shell we will later relink against our own WebKitLegacy
// build. WebView/WebFrame/WebFrameLoadDelegate/WebPolicyDelegate/WebUIDelegate
// are identical between Tiger's system WebKit and modern WebKitLegacy, so for
// now this links against the 10.4u SDK's WebKit.framework and runs against
// Tiger's own WebKit on the box.
//
// Build: see Makefile. MRR, fragile ObjC runtime, no nib (all UI built by hand).

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

// ---------------------------------------------------------------- window controller

@interface TigerBrowserController : NSObject
{
    NSWindow *_window;
    WebView *_webView;
    NSTextField *_addressField;
    NSTextField *_statusField;
    NSButton *_backButton;
    NSButton *_forwardButton;
}
- (id)initWithURLString:(NSString *)urlString;
- (void)loadURLString:(NSString *)urlString;
- (void)goBack:(id)sender;
- (void)goForward:(id)sender;
- (void)reload:(id)sender;
- (void)loadFromAddressField:(id)sender;
- (void)focusAddressField:(id)sender;
@end

@implementation TigerBrowserController

- (id)initWithURLString:(NSString *)urlString
{
    self = [super init];
    if (!self)
        return nil;

    NSRect frame = NSMakeRect(80, 120, 900, 650);
    _window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSTitledWindowMask | NSClosableWindowMask |
                             NSMiniaturizableWindowMask | NSResizableWindowMask)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [_window setTitle:@"TigerBrowser"];
    [_window setReleasedWhenClosed:NO];

    NSView *content = [_window contentView];
    NSRect bounds = [content bounds];
    const float barHeight = 32.0;
    const float statusHeight = 18.0;
    const float pad = 6.0;

    NSRect barRect = NSMakeRect(0, bounds.size.height - barHeight,
                                 bounds.size.width, barHeight);
    NSView *bar = [[[NSView alloc] initWithFrame:barRect] autorelease];
    [bar setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];

    _backButton = [[[NSButton alloc] initWithFrame:NSMakeRect(pad, 4, 34, 24)] autorelease];
    [_backButton setBezelStyle:NSRoundedBezelStyle];
    [_backButton setTitle:@"<"];
    [_backButton setTarget:self];
    [_backButton setAction:@selector(goBack:)];
    [_backButton setAutoresizingMask:NSViewMaxXMargin];
    [bar addSubview:_backButton];

    _forwardButton = [[[NSButton alloc] initWithFrame:NSMakeRect(pad + 38, 4, 34, 24)] autorelease];
    [_forwardButton setBezelStyle:NSRoundedBezelStyle];
    [_forwardButton setTitle:@">"];
    [_forwardButton setTarget:self];
    [_forwardButton setAction:@selector(goForward:)];
    [_forwardButton setAutoresizingMask:NSViewMaxXMargin];
    [bar addSubview:_forwardButton];

    NSButton *reloadButton = [[[NSButton alloc] initWithFrame:NSMakeRect(pad + 76, 4, 34, 24)] autorelease];
    [reloadButton setBezelStyle:NSRoundedBezelStyle];
    [reloadButton setTitle:@"R"];
    [reloadButton setTarget:self];
    [reloadButton setAction:@selector(reload:)];
    [reloadButton setAutoresizingMask:NSViewMaxXMargin];
    [bar addSubview:reloadButton];

    float addrX = pad + 76 + 34 + pad;
    NSRect addrRect = NSMakeRect(addrX, 4, bounds.size.width - addrX - pad, 24);
    _addressField = [[[NSTextField alloc] initWithFrame:addrRect] autorelease];
    [_addressField setAutoresizingMask:NSViewWidthSizable];
    [_addressField setTarget:self];
    [_addressField setAction:@selector(loadFromAddressField:)];
    [bar addSubview:_addressField];

    [content addSubview:bar];

    NSRect statusRect = NSMakeRect(0, 0, bounds.size.width, statusHeight);
    _statusField = [[[NSTextField alloc] initWithFrame:statusRect] autorelease];
    [_statusField setAutoresizingMask:(NSViewWidthSizable | NSViewMaxYMargin)];
    [_statusField setEditable:NO];
    [_statusField setSelectable:NO];
    [_statusField setBezeled:NO];
    [_statusField setDrawsBackground:NO];
    [_statusField setFont:[NSFont systemFontOfSize:10]];
    [_statusField setStringValue:@""];
    [content addSubview:_statusField];

    NSRect webRect = NSMakeRect(0, statusHeight, bounds.size.width,
                                 bounds.size.height - barHeight - statusHeight);
    _webView = [[WebView alloc] initWithFrame:webRect frameName:@"" groupName:@""];
    [_webView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [_webView setFrameLoadDelegate:self];
    [_webView setPolicyDelegate:self];
    [_webView setUIDelegate:self];
    [_webView setResourceLoadDelegate:self];
    [content addSubview:_webView];

    [_window makeFirstResponder:_addressField];
    [_window setInitialFirstResponder:_addressField];

    if (urlString)
        [self loadURLString:urlString];

    return self;
}

- (void)dealloc
{
    [_webView setFrameLoadDelegate:nil];
    [_webView setPolicyDelegate:nil];
    [_webView setUIDelegate:nil];
    [_webView setResourceLoadDelegate:nil];
    [_webView release];
    [_window release];
    [super dealloc];
}

- (void)showWindow
{
    [_window makeKeyAndOrderFront:nil];
}

- (void)loadURLString:(NSString *)urlString
{
    NSURL *url = [NSURL URLWithString:urlString];
    if (!url) {
        fprintf(stderr, "TigerBrowser: could not parse URL '%s'\n", [urlString UTF8String]);
        return;
    }
    [_addressField setStringValue:urlString];
    NSURLRequest *request = [NSURLRequest requestWithURL:url];
    [[_webView mainFrame] loadRequest:request];
}

- (void)loadFromAddressField:(id)sender
{
    NSString *text = [_addressField stringValue];
    if ([text length] == 0)
        return;
    // Bare "Open Location" convenience: no scheme means http://.
    NSRange schemeRange = [text rangeOfString:@"://"];
    if (schemeRange.location == NSNotFound)
        text = [NSString stringWithFormat:@"http://%@", text];
    [self loadURLString:text];
}

- (void)goBack:(id)sender
{
    [_webView goBack];
}

- (void)goForward:(id)sender
{
    [_webView goForward];
}

- (void)reload:(id)sender
{
    [[_webView mainFrame] reload];
}

- (void)focusAddressField:(id)sender
{
    [_window makeFirstResponder:_addressField];
    [_addressField selectText:nil];
}

// ---------------------------------------------------------- WebFrameLoadDelegate

- (void)webView:(WebView *)sender didStartProvisionalLoadForFrame:(WebFrame *)frame
{
    if (frame != [sender mainFrame])
        return;
    fprintf(stderr, "TigerBrowser: load started\n");
    [_statusField setStringValue:@"Loading..."];
}

- (void)webView:(WebView *)sender didCommitLoadForFrame:(WebFrame *)frame
{
    if (frame != [sender mainFrame])
        return;
    fprintf(stderr, "TigerBrowser: load committed\n");
}

- (void)webView:(WebView *)sender didReceiveTitle:(NSString *)title forFrame:(WebFrame *)frame
{
    if (frame != [sender mainFrame])
        return;
    [_window setTitle:(title && [title length]) ? title : @"TigerBrowser"];
}

- (void)webView:(WebView *)sender didFinishLoadForFrame:(WebFrame *)frame
{
    if (frame != [sender mainFrame])
        return;
    fprintf(stderr, "TigerBrowser: load finished\n");
    [_statusField setStringValue:@""];
}

- (void)webView:(WebView *)sender didFailProvisionalLoadWithError:(NSError *)error forFrame:(WebFrame *)frame
{
    if (frame != [sender mainFrame])
        return;
    fprintf(stderr, "TigerBrowser: provisional load failed: %s\n",
            [[error localizedDescription] UTF8String]);
    [_statusField setStringValue:[NSString stringWithFormat:@"Failed: %@", [error localizedDescription]]];
}

- (void)webView:(WebView *)sender didFailLoadWithError:(NSError *)error forFrame:(WebFrame *)frame
{
    if (frame != [sender mainFrame])
        return;
    fprintf(stderr, "TigerBrowser: load failed: %s\n",
            [[error localizedDescription] UTF8String]);
    [_statusField setStringValue:[NSString stringWithFormat:@"Failed: %@", [error localizedDescription]]];
}

// -------------------------------------------------------------- WebPolicyDelegate

- (void)webView:(WebView *)webView decidePolicyForNavigationAction:(NSDictionary *)actionInformation
                                                           request:(NSURLRequest *)request
                                                             frame:(WebFrame *)frame
                                                  decisionListener:(id<WebPolicyDecisionListener>)listener
{
    [listener use];
}

// ------------------------------------------------------------------ WebUIDelegate

- (void)webView:(WebView *)sender setStatusText:(NSString *)text
{
    [_statusField setStringValue:(text ? text : @"")];
}

- (void)webView:(WebView *)sender runJavaScriptAlertPanelWithMessage:(NSString *)message
{
    NSRunAlertPanel(@"JavaScript", @"%@", @"OK", nil, nil, message);
}

- (BOOL)webView:(WebView *)sender runJavaScriptConfirmPanelWithMessage:(NSString *)message
{
    int result = NSRunAlertPanel(@"JavaScript", @"%@", @"OK", @"Cancel", nil, message);
    return result == NSAlertDefaultReturn;
}

@end

// ---------------------------------------------------------------- app delegate

@interface TigerBrowserAppDelegate : NSObject
{
    TigerBrowserController *_controller;
}
- (id)initWithURLString:(NSString *)urlString;
@end

@implementation TigerBrowserAppDelegate

- (id)initWithURLString:(NSString *)urlString
{
    self = [super init];
    if (!self)
        return nil;
    _controller = [[TigerBrowserController alloc] initWithURLString:urlString];
    return self;
}

- (void)dealloc
{
    [_controller release];
    [super dealloc];
}

- (void)applicationDidFinishLaunching:(NSNotification *)note
{
    [_controller showWindow];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app
{
    return YES;
}

- (void)openLocation:(id)sender
{
    [_controller focusAddressField:sender];
}

@end

// ---------------------------------------------------------------------- main

int main(int argc, const char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    NSApplication *app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];

    NSString *urlString = @"about:blank";
    if (argc > 1)
        urlString = [NSString stringWithUTF8String:argv[1]];

    TigerBrowserAppDelegate *delegate =
        [[TigerBrowserAppDelegate alloc] initWithURLString:urlString];
    [app setDelegate:delegate];

    // Minimal menu bar: an Edit menu (so Cmd-C/V work in the address field) and
    // a File menu with Cmd-L for "Open Location".
    NSMenu *menubar = [[NSMenu alloc] init];
    NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
    [menubar addItem:appMenuItem];
    [app setMainMenu:menubar];

    NSMenu *appMenu = [[NSMenu alloc] init];
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit TigerBrowser"
                                                        action:@selector(terminate:)
                                                 keyEquivalent:@"q"];
    [appMenu addItem:quitItem];
    [quitItem release];
    [appMenuItem setSubmenu:appMenu];
    [appMenu release];
    [appMenuItem release];

    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem *fileMenuItem = [[NSMenuItem alloc] initWithTitle:@"File" action:NULL keyEquivalent:@""];
    [fileMenuItem setSubmenu:fileMenu];
    [menubar addItem:fileMenuItem];
    [fileMenuItem release];

    NSMenuItem *openLocationItem = [[NSMenuItem alloc] initWithTitle:@"Open Location..."
                                                                action:@selector(openLocation:)
                                                         keyEquivalent:@"l"];
    [openLocationItem setTarget:delegate];
    [fileMenu addItem:openLocationItem];
    [openLocationItem release];
    [fileMenu release];

    [menubar release];

    [app activateIgnoringOtherApps:YES];
    [app run];

    [pool release];
    return 0;
}
