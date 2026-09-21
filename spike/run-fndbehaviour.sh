#!/bin/bash
# Foundation on Tiger versus modern: build spike/fndbehaviour.m for both, run
# both, diff. See the file header for what it covers.
#
# The -ObjC is load-bearing: without it the linker does not pull nscompat's
# categories out of libtigercompat.a and the shimmed methods are silently
# missing at runtime. -ObjC then drags the whole archive, which is why AppKit,
# ApplicationServices and libtigerdispatch are on the line.
set -e
WKT=/Users/shg/Developer/WebKitTiger
clang -w -o $WKT/build/fndbehaviour-host $WKT/spike/fndbehaviour.m -framework Foundation
$WKT/build/fndbehaviour-host > /tmp/fnd-host.txt
$WKT/toolchain/bin/tiger-clang -w -O1 -DTIGER=1 \
  -fobjc-runtime=macosx-fragile-10.4 -fobjc-exceptions -fblocks -ObjC \
  -F $WKT/compat/sdk-overlay \
  -isystem $WKT/compat/include -isystem $WKT/compat/include/sdk-fill \
  -ltigercompat -ltigerdispatch \
  -framework Foundation -framework AppKit -framework ApplicationServices \
  -o $WKT/build/fndbehaviour $WKT/spike/fndbehaviour.m
scp -qO $WKT/build/fndbehaviour tiger:/tmp/fndbehaviour
ssh tiger "/tmp/fndbehaviour" > /tmp/fnd-tiger.txt
python3 - <<'PY'
t={};h={};ho=[]
for f,d,o in (('/tmp/fnd-tiger.txt',t,None),('/tmp/fnd-host.txt',h,ho)):
    for l in open(f):
        l=l.rstrip('\n')
        if '=' in l:
            k,v=l.split('=',1); d[k]=v
            if o is not None: o.append(k)
diff=[k for k in ho if k in t and t[k]!=h[k] and k!='platform']
missing=[k for k in ho if k not in t]
print(f"compared={len([k for k in ho if k in t])} differing={len(diff)} missing-on-tiger={len(missing)}")
for k in diff:  print(f"  DIFF    {k}\n            tiger={t[k]}\n            host ={h[k]}")
for k in missing: print(f"  MISSING {k} (host={h[k]})")
PY
