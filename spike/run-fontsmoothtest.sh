#!/bin/bash
# Do Tiger's text antialiasing knobs do anything? See spike/fontsmoothtest.c.
set -e
WKT=/Users/shg/Developer/WebKitTiger
$WKT/toolchain/bin/tiger-clang -O1 -o $WKT/build/fontsmoothtest $WKT/spike/fontsmoothtest.c \
  -F $WKT/compat/sdk-overlay \
  -F $WKT/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
  -ltigercompat -framework ApplicationServices
scp -qO $WKT/build/fontsmoothtest tiger:/tmp/fontsmoothtest
ssh tiger "/tmp/fontsmoothtest"
