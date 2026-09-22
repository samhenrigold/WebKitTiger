/*
 * Copyright (C) 2026 WebKitTiger contributors. BSD-2-Clause, as the rest of WebKit.
 */

// The NSView that hosts a WebKit::TigerWebView: paints its BackingStore and routes
// NSEvents to the page. Shared by TigerWK2App (the harness) and TigerBrowser2 (the chrome).

#pragma once

#import <AppKit/AppKit.h>
#include "TigerWebView.h"
#include <wtf/RefPtr.h>

@interface TigerWK2View : NSView {
    RefPtr<WebKit::TigerWebView> _webView;
}
- (void)attachWebView:(WebKit::TigerWebView*)view;
- (WebKit::TigerWebView*)webView;
@end
