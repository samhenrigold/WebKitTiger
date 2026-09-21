#!/bin/bash
# Build spike/nsmaptabletest.m against compat/sdk-overlay and run it on Tiger.
# -Wl,-ObjC is mandatory; see compat/sdk-overlay/README.md.
set -e
WKT=/Users/shg/Developer/WebKitTiger
OV=$WKT/compat/sdk-overlay
AS=$WKT/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks
"$WKT/toolchain/bin/tiger-clang" -g -O1 -Wall -fobjc-runtime=macosx-fragile-10.4 -fblocks \
    -fobjc-exceptions -isystem "$OV/usr/include" -F "$OV" -F "$AS" \
    -isystem "$WKT/compat/include" -isystem "$WKT/compat/include/sdk-fill" \
    "$WKT/spike/nsmaptabletest.m" -Wl,-ObjC -ltigercompat -ltigerdispatch \
    -framework Foundation -framework AppKit -framework ApplicationServices \
    -o "$WKT/spike/nsmaptabletest" 2>&1 | grep -v 'unused during compilation' || true
scp -qO "$WKT/spike/nsmaptabletest" tiger:/tmp/
ssh tiger /tmp/nsmaptabletest
