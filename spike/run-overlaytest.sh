#!/bin/bash
# Build spike/overlaytest.mm against compat/sdk-overlay in both MRR and ARC,
# copy it to the Tiger box and run it. Flag order matters; see the overlay
# README.
#
# -Wl,-ObjC is load-bearing, and goes to the linker rather than the compiler: a
# bare -ObjC would retarget the compiler to Objective-C and break the C++ in
# this file. Most of libtigercompat's Foundation surface is Objective-C
# categories, and a static archive member is only pulled in when it defines a
# symbol somebody referenced. A category defines no symbol, so without -ObjC the
# member never joins the link and every category method is missing at runtime,
# with nothing said at build time.
#
# -ObjC also pulls in NSOperationQueue, which is built on libtigerdispatch, so
# -ltigerdispatch comes along whether or not this program uses a queue.
set -e
WKT=/Users/shg/Developer/WebKitTiger
OV=$WKT/compat/sdk-overlay
AS=$WKT/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks
BUILTINS=$WKT/build/builtins-i386/libclang_rt.builtins-i386.a

OVERLAY=(-isystem "$OV/usr/include" -F "$OV" -F "$AS")
SDKFILL=(-isystem "$WKT/compat/include" -isystem "$WKT/compat/include/sdk-fill")
CXXRT=(-nostdinc++ -isystem "$WKT/toolchain/sysroot-i386/usr/include/c++/v1" -stdlib=libc++
       -lc++ -lc++abi -lunwind)

build() # $1 = mrr|arc, $2 = output
{
    local objc=(-fobjc-runtime=macosx-fragile-10.4 -fblocks)
    [ "$1" = arc ] && objc=(-fobjc-arc -fobjc-runtime=macosx-fragile-10.7)
    "$WKT/toolchain/bin/tiger-clang++" -g -O1 -Wall "${objc[@]}" \
        -femulated-tls -fobjc-exceptions -fexceptions \
        "${OVERLAY[@]}" "${SDKFILL[@]}" "${CXXRT[@]}" \
        "$WKT/spike/overlaytest.mm" -Wl,-ObjC -ltigercompat -ltigerdispatch "$BUILTINS" \
        -framework Foundation -framework AppKit -framework ApplicationServices \
        -o "$2"
}

for mode in mrr arc; do
    out=$WKT/spike/overlaytest_$mode
    echo "== build $mode"
    build $mode "$out" 2>&1 | grep -v 'unused during compilation' || true
    scp -qO "$out" tiger:/tmp/
    echo "== run $mode"
    ssh tiger "/tmp/overlaytest_$mode"
done
