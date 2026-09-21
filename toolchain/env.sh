# source this: cross-compile env for i386 / Mac OS X 10.4
export WKT=/Users/shg/Developer/WebKitTiger
export TIGER_SDK=$WKT/sdk/MacOSX10.4u.sdk
export TIGER_BIN=$WKT/toolchain/cctools/bin
export TIGER_LD=$TIGER_BIN/i386-apple-darwin8-ld
export TIGER_CFLAGS="-target i386-apple-macosx10.4 -isysroot $TIGER_SDK -mmacosx-version-min=10.4 -fno-stack-protector"
export TIGER_OBJCFLAGS="-fobjc-runtime=macosx-fragile-10.4"
tcc()  { clang $TIGER_CFLAGS --ld-path=$TIGER_LD "$@"; }
tcxx() { clang++ $TIGER_CFLAGS --ld-path=$TIGER_LD "$@"; }
tobjc() { clang $TIGER_CFLAGS $TIGER_OBJCFLAGS --ld-path=$TIGER_LD "$@"; }
tar_() { $TIGER_BIN/i386-apple-darwin8-ar "$@"; }
tnm()  { $TIGER_BIN/i386-apple-darwin8-nm "$@"; }
