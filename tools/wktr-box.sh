#!/bin/sh
# WebKitTestRunner "binary" for run-webkit-tests --platform tiger (webkitpy/port/tiger.py):
# runs the real one on the box over ssh, stdin/stdout carrying WKTR's line protocol. The
# Mac's HTTP servers are reverse-tunnelled so http tests see 127.0.0.1:8000/8443/8080.
# Staging and the box lock are tools/run-layout-tests-box.sh's; this only connects.
# On the box, wktr-guard.pl runs WKTR in its own process group and kills that group when
# this ssh goes away (webkitpy kills the driver on timeouts).
args=
for a in "$@"; do args="$args '$a'"; done
exec ssh -T -o ExitOnForwardFailure=no -o ServerAliveInterval=15 \
    -R 8000:127.0.0.1:8000 -R 8443:127.0.0.1:8443 -R 8080:127.0.0.1:8080 \
    tiger-eth "cd /Users/shg/wktr/bin && exec env \
TIGER_FONT_MANIFEST=/Users/shg/wk2/share/tiger-fonts.json TIGER_CA_BUNDLE=/Users/shg/wk2/share/cacert.pem \
WEBKIT_TIGER_HELPER_DIR=/Users/shg/wktr/bin/helpers ${WKTR_BOX_ENV:-} \
nice -n 10 perl /Users/shg/wktr/bin/wktr-guard.pl ./WebKitTestRunner $args"
