#!/bin/bash
# What Tiger's CGContextClipToMask actually does. See spike/clipmasktest.c.
set -e
WKT=/Users/shg/Developer/WebKitTiger
$WKT/toolchain/bin/tiger-clang -O1 -o $WKT/build/clipmasktest $WKT/spike/clipmasktest.c \
  -F $WKT/compat/sdk-overlay \
  -F $WKT/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
  -ltigercompat -framework ApplicationServices
scp -qO $WKT/build/clipmasktest tiger:/tmp/clipmasktest
ssh tiger "/tmp/clipmasktest"
