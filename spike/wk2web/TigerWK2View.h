/*
 * Copyright (C) 2026 WebKitTiger contributors. BSD-2-Clause, as the rest of WebKit.
 */

// The NSView that hosts a WebKit::TigerWebView: paints its BackingStore and routes
// NSEvents to the page. Shared by TigerWK2App (the harness) and TigerBrowser2 (the chrome).

#pragma once

#import <AppKit/AppKit.h>
#include "TigerWebView.h"
#include <wtf/RefPtr.h>

@interface TigerWK2View : NSView <NSTextInput> {
    RefPtr<WebKit::TigerWebView> _webView;
    // Filled in by -insertText:/-doCommandBySelector: while -interpretKeyEvents: runs.
    NSString* _interpretedText;
    NSMutableArray* _interpretedCommands;
    BOOL _interpreting;
    // An input method's composition (a dead key's accent, Kotoeri's kana) is in the page.
    BOOL _hasMarkedText;
    // Set by -setMarkedText: or a commit while -interpretKeyEvents: runs: the input method
    // took this key, so the raw event must not insert its characters as well.
    BOOL _inputMethodHandledKey;
}
- (void)attachWebView:(WebKit::TigerWebView*)view;
- (WebKit::TigerWebView*)webView;
@end
