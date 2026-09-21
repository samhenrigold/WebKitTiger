#!/bin/bash
# Build spike/cgbehaviour.c for Tiger AND for this Mac, run both, diff them.
# Modern CoreGraphics is the reference, so the output is a comparison rather
# than a judgement about what Tiger "should" do.
set -e
WKT=/Users/shg/Developer/WebKitTiger
clang -O1 -w -o $WKT/build/cgbehaviour-host $WKT/spike/cgbehaviour.c \
  -framework CoreGraphics -framework ApplicationServices
$WKT/build/cgbehaviour-host > /tmp/cgb-host.txt
$WKT/toolchain/bin/tiger-clang -O1 -w -o $WKT/build/cgbehaviour $WKT/spike/cgbehaviour.c \
  -F $WKT/compat/sdk-overlay \
  -F $WKT/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
  -ltigercompat -framework ApplicationServices
scp -qO $WKT/build/cgbehaviour tiger:/tmp/cgbehaviour
ssh tiger "/tmp/cgbehaviour" > /tmp/cgb-tiger.txt
paste <(sed 's/=.*//' /tmp/cgb-tiger.txt) \
      <(sed 's/.*=//' /tmp/cgb-tiger.txt) \
      <(sed 's/.*=//' /tmp/cgb-host.txt) |
  awk -F'\t' 'NR>1{s=($2==$3)?"":"  <-- DIFFERS";printf "%-30s tiger=%-10s host=%-10s%s\n",$1,$2,$3,s}'
