#!/bin/bash
# Build spike/appkittest.m in MRR and ARC, copy to the Tiger box and run it
# under a real NSApplication. See compat/sdk-overlay/README.md for flag order
# and for why -Wl,-ObjC is mandatory.
set -e
WKT=/Users/shg/Developer/WebKitTiger
OV=$WKT/compat/sdk-overlay
AS=$WKT/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks

for mode in mrr arc; do
    objc=(-fobjc-runtime=macosx-fragile-10.4 -fblocks)
    [ "$mode" = arc ] && objc=(-fobjc-arc -fobjc-runtime=macosx-fragile-10.7)
    out=$WKT/spike/appkittest_$mode
    echo "== build $mode"
    "$WKT/toolchain/bin/tiger-clang" -g -O1 -Wall "${objc[@]}" -fobjc-exceptions \
        -isystem "$OV/usr/include" -F "$OV" -F "$AS" \
        -isystem "$WKT/compat/include" -isystem "$WKT/compat/include/sdk-fill" \
        "$WKT/spike/appkittest.m" -Wl,-ObjC -ltigercompat -ltigerdispatch \
        -framework Foundation -framework AppKit -framework ApplicationServices \
        -o "$out" 2>&1 | grep -v 'unused during compilation' || true
    scp -qO "$out" tiger:/tmp/
    echo "== run $mode"
    ssh tiger "/tmp/appkittest_$mode"
done
