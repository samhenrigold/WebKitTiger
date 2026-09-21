#!/bin/bash
# objc2test links against a dylib so protocol-ext recovery is tested across two images.
set -e
WKT=/Users/shg/Developer/WebKitTiger
CC=$WKT/toolchain/bin/tiger-clang
SDKFILL="-isystem $WKT/compat/include -isystem $WKT/compat/include/sdk-fill"
FLAGS="-g -O1 -fobjc-runtime=${TIGER_OBJC_RT:-macosx-fragile-10.4} -fobjc-exceptions $SDKFILL"
cd $WKT/spike
$CC $FLAGS -dynamiclib protolib.m -o libproto.dylib -install_name @executable_path/libproto.dylib \
    -ltigercompat -framework Foundation
$CC $FLAGS objc2test.m libproto.dylib -o objc2test -ltigercompat -framework Foundation
scp -qO libproto.dylib objc2test tiger:/tmp/
ssh tiger '/tmp/objc2test'
