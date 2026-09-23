#!/bin/sh
# LayoutTests on the box: run-webkit-tests --platform tiger on this Mac, WebKitTestRunner on
# the box (tools/wktr-box.sh over ssh), the Mac's Apache reverse-tunnelled for http/tests.
#   tools/run-layout-tests-box.sh http/tests/cookies js/dom [-- more run-webkit-tests args]
# A GUI run: holds spike/wk2web/box.lock for its whole length, never runs while the user's
# TigerBrowser.app (or anyone's Tiger* processes) is up, and kills only its own processes
# (argv under /Users/shg/wktr/) afterwards. Results: logs/layout-tests/<stamp>/.
WKT=/Users/shg/Developer/WebKitTiger
SRC=$WKT/WebKit-tests
BOX=tiger-eth
D=/Users/shg/wktr
exec 9>"$WKT/spike/wk2web/box.lock"
python3 -c 'import fcntl,time,sys
for _ in range(600):
    try: fcntl.flock(9, fcntl.LOCK_EX|fcntl.LOCK_NB); sys.exit(0)
    except OSError: time.sleep(1)
sys.exit(1)' || { echo "run-layout-tests-box: box busy for 10 min"; exit 1; }
busy=$(ssh $BOX "ps -axo command | grep -E '[/]Applications/TigerBrowser.app|[T]iger(WK2App|Browser2|WebProcess|NetworkProcess|GPUProcess)' | head -3")
if [ -n "$busy" ]; then echo "run-layout-tests-box: the box is in use, not starting:"; echo "$busy"; exit 1; fi

tests=
while [ $# -gt 0 ] && [ "$1" != "--" ]; do tests="$tests $1"; shift; done
[ "$1" = "--" ] && shift

# Binaries: one directory, helpers next to the runner; the rebundled QuartzCore one level up.
ssh $BOX "mkdir -p $D/bin $D/Frameworks"
rsync -t -z --partial --inplace \
    "$WKT/build/tiger-ui-tests/bin/WebKitTestRunner" \
    "$WKT/build/tiger-web-tests/bin/wktr/TigerWebProcess" \
    "$WKT/build/tiger-web-tests/bin/TigerNetworkProcess" \
    "$WKT/build/tiger-gpu/bin/TigerGPUProcess" \
    "$WKT/tools/wktr-guard.pl" $BOX:$D/bin/ || exit 1
rsync -rtl -z "$WKT/spike/CAHost/Frameworks/QuartzCore.framework" $BOX:$D/Frameworks/
# Tests at the same absolute path as here (file: URLs need no mapping), plus what they load.
( cd "$SRC/LayoutTests" && rsync -rtR -z --exclude '*-expected.*' $tests resources http/tests/resources js/resources \
    $BOX:$SRC/LayoutTests/ ) 2>&1 | grep -v 'No such file' || true
ssh $BOX "ls $SRC/LayoutTests >/dev/null" || exit 1

RUN=$WKT/logs/layout-tests/$(date +%Y%m%d-%H%M%S)
mkdir -p "$RUN"
python3 "$SRC/Tools/Scripts/run-webkit-tests" --platform tiger --no-build --no-show-results \
    --child-processes 1 --results-directory "$RUN" "$@" $tests 2>&1 | tee "$RUN/run.log"
# Our processes only (the guard should have taken them already).
ssh $BOX "ps -axo pid,command | awk '\$2 ~ /^\\/Users\\/shg\\/wktr\\// || \$2 ~ /^\\.\\/WebKitTestRunner/ {print \$1}' | xargs kill 2>/dev/null; true"
echo "results: $RUN"
