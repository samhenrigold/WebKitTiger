#!/bin/bash
# Build spike/fragileivars.mm with the patched clang, MRR and ARC variants.
set -e
WKT=/Users/shg/Developer/WebKitTiger
CLANGXX=${CLANGXX:-$WKT/build/llvm-host/bin/clang++}
COMMON=(-target i386-apple-macosx10.4 -isysroot "$WKT/sdk/MacOSX10.4u.sdk"
        -mmacosx-version-min=10.4 -fno-stack-protector
        --ld-path="$WKT/toolchain/cctools/bin/i386-apple-darwin8-ld"
        -include "$WKT/compat/include/tigerprelude.h"
        -I"$WKT/toolchain/sysroot-i386/usr/include"
        -L"$WKT/toolchain/sysroot-i386/usr/lib"
        -fobjc-fragile-extension-ivars -framework Foundation)

echo "== MRR =="
"$CLANGXX" "${COMMON[@]}" -fobjc-runtime=macosx-fragile-10.4 \
    "$WKT/spike/fragileivars.mm" -ltigercompat -o "$WKT/spike/fragileivars_mrr"

echo "== ARC =="
"$CLANGXX" "${COMMON[@]}" -fobjc-arc -fobjc-runtime=macosx-fragile-10.7 \
    "$WKT/spike/fragileivars.mm" -ltigercompat -lc++abi -lunwind -o "$WKT/spike/fragileivars_arc"

echo "== hello.mm regression =="
"$CLANGXX" -target i386-apple-macosx10.4 -isysroot "$WKT/sdk/MacOSX10.4u.sdk" \
    -mmacosx-version-min=10.4 -fno-stack-protector \
    --ld-path="$WKT/toolchain/cctools/bin/i386-apple-darwin8-ld" \
    -include "$WKT/compat/include/tigerprelude.h" \
    -I"$WKT/toolchain/sysroot-i386/usr/include" -L"$WKT/toolchain/sysroot-i386/usr/lib" \
    -fobjc-runtime=macosx-fragile-10.4 -framework Foundation -framework AppKit \
    -nostdinc++ -isystem "$WKT/toolchain/sysroot-i386/usr/include/c++/v1" \
    -nostdlib++ -lc++ -lc++abi -lunwind -ltigercompat \
    "$WKT/spike/hello.mm" -o "$WKT/spike/hello_mm_new"
echo OK
