#!/bin/bash
# build a spike program for Tiger, copy it over, run it
set -e
WKT=/Users/shg/Developer/WebKitTiger
SRC=$1; shift
OUT=$WKT/spike/$(basename ${SRC%.*})
BUILTINS=$WKT/build/builtins-i386/libclang_rt.builtins-i386.a
# same two include roots the WebKit build uses: our objc/runtime.h first, then the SDK-gap fills
SDKFILL="-isystem $WKT/compat/include -isystem $WKT/compat/include/sdk-fill"
ARC="-Xclang -fobjc-arc -fobjc-runtime=macosx-fragile-10.7"
[ -n "$NOARC" ] && ARC="-fobjc-runtime=macosx-fragile-10.4 -fblocks"
INC="-nostdinc++ -isystem $WKT/toolchain/sysroot-i386/usr/include/c++/v1"
CXX_RT="$INC -stdlib=libc++ -lc++ -lc++abi -lunwind"
case "$SRC" in
  *.mm) $WKT/toolchain/bin/tiger-clang++ -g -O1 $ARC -femulated-tls -fobjc-exceptions -fexceptions $SDKFILL \
          $CXX_RT -ltigercompat $BUILTINS -framework Foundation "$@" $SRC -o $OUT ;;
  *.m)  $WKT/toolchain/bin/tiger-clang -g -O1 -fobjc-runtime=macosx-fragile-10.4 -fobjc-exceptions $SDKFILL \
          -ltigercompat -framework Foundation "$@" $SRC -o $OUT ;;
  *.cpp) $WKT/toolchain/bin/tiger-clang++ -g -O1 -std=c++2b -femulated-tls -fexceptions \
          $CXX_RT -ltigercompat $BUILTINS "$@" $SRC -o $OUT ;;
esac
scp -qO $OUT tiger:/tmp/$(basename $OUT)
ssh tiger "/tmp/$(basename $OUT)"
