#!/bin/sh
# Crash baseline: five real pages, 90 s each, sampler off, one web process per page.
# Counts the lines TigerCrashCatcher prints (TIGER-CRASH, TIGER-CRASH-SIGNAL, TIGER-ABORT,
# TIGER-EXIT, TIGER-JSC-FAULT) so a release can be compared with the one before it.
#   WEBBIN=/tmp/tigerfrozen UIBIN=/tmp/tigerfrozen TAG=r2 spike/wk2web/crash-baseline.sh
set -e
WKT=/Users/shg/Developer/WebKitTiger
TAG=${TAG:-base}
SECS=${SECS:-90}
export WEBBIN UIBIN

scrolls() {
    s="wait 20"
    i=0
    while [ $i -lt 20 ]; do s="$s; scroll 0,-600; wait 3"; i=$((i+1)); done
    echo "$s"
}

run() {
    name=$1; url=$2; script=$3
    out="$WKT/logs/perf/$TAG-$name.log"
    APP=TigerBrowser2 SAMPLE=0 SCRIPT="$script" OUT="$out" \
        "$WKT/spike/wk2web/stage-jsperf.sh" "$url" "$SECS" > "$out.stage" 2>&1 || echo "$name: stage failed"
    n() { c=$(grep -c "$1" "$out" 2>/dev/null) || true; echo "${c:-?}"; }   # grep -c exits 1 on 0
    printf '%-10s crash=%s signal=%s abort=%s exit=%s jscfault=%s\n' "$name" \
        "$(n 'TIGER-CRASH pid')" "$(n TIGER-CRASH-SIGNAL)" "$(n TIGER-ABORT)" "$(n TIGER-EXIT)" "$(n TIGER-JSC-FAULT)"
}

run verge   https://www.theverge.com/            "$(scrolls)"
run nytimes https://www.nytimes.com/             "$(scrolls)"
run apple   https://www.apple.com/iphone-duo/    "$(scrolls)"
run youtube "https://www.youtube.com/watch?v=f7NwyBnIRTE" "wait 30; scroll 0,-400; wait 10; scroll 0,-400; wait 30"
run x       https://x.com/                       "wait 20; click 300,236; wait 10; scroll 0,-400; wait 5; scroll 0,-400; wait 20"
