#!/bin/bash
# AppKit on Tiger versus modern. Runs under a live NSApplication with a real
# window on both machines; AppKit works over ssh on the box as long as someone
# is logged in at the console.
#
# Known gap: the pasteboard server is not in an ssh session's bootstrap
# namespace, so every NSPasteboard is nil on the box. Run this from a console
# session there if you need the pasteboard numbers.
set -e
WKT=/Users/shg/Developer/WebKitTiger
clang -w -fobjc-exceptions -o $WKT/build/akbehaviour-host $WKT/spike/akbehaviour.m \
  -framework AppKit -framework Foundation -framework ApplicationServices -framework CoreText
$WKT/build/akbehaviour-host > /tmp/ak-host.txt 2>&1
$WKT/toolchain/bin/tiger-clang -w -O1 -DTIGER=1 \
  -fobjc-runtime=macosx-fragile-10.4 -fobjc-exceptions -fblocks -ObjC \
  -F $WKT/compat/sdk-overlay \
  -isystem $WKT/compat/include -isystem $WKT/compat/include/sdk-fill \
  -ltigercompat -ltigerdispatch \
  -framework AppKit -framework Foundation -framework ApplicationServices \
  -o $WKT/build/akbehaviour $WKT/spike/akbehaviour.m
scp -qO $WKT/build/akbehaviour tiger:/tmp/akbehaviour
ssh tiger "/tmp/akbehaviour" > /tmp/ak-tiger.txt 2>&1
python3 - <<'PY'
t={};h={};ho=[]
for f,d,o in (('/tmp/ak-tiger.txt',t,None),('/tmp/ak-host.txt',h,ho)):
    for l in open(f):
        l=l.rstrip('\n')
        if '=' in l and not l.startswith(('202','*** ','\t')):
            k,v=l.split('=',1); d[k]=v
            if o is not None: o.append(k)
diff=[k for k in ho if k in t and t[k]!=h[k] and k!='platform']
miss=[k for k in ho if k not in t]
print(f"compared={len([k for k in ho if k in t])} differing={len(diff)} missing-on-tiger={len(miss)}")
for k in diff:  print(f"  DIFF    {k:34s} tiger={t[k]:30s} host={h[k]}")
for k in miss:  print(f"  MISSING {k} (host={h[k]})")
PY
