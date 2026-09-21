#!/bin/bash
# Are Tiger's middle interpolation qualities distinct? See spike/interptest.c.
set -e
WKT=/Users/shg/Developer/WebKitTiger
$WKT/toolchain/bin/tiger-clang -O1 -o $WKT/build/interptest $WKT/spike/interptest.c \
  -F $WKT/compat/sdk-overlay \
  -F $WKT/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
  -ltigercompat -framework ApplicationServices
scp -qO $WKT/build/interptest tiger:/tmp/interptest
ssh tiger "/tmp/interptest"
