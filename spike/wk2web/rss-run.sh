#!/bin/sh
# Stage a run with stage-jsperf.sh and sample the staged web process's RSS every 2 s from
# this side (one short ssh per sample, nothing left running on the box).
#   OUT=logs/perf/x.log APP_ENV=... spike/wk2web/rss-run.sh <url> <secs>
# Prints "t=<s> rss=<MB>" lines to $OUT.rss and the peak / last values at the end.
WKT=/Users/shg/Developer/WebKitTiger
URL=$1; SECS=${2:-60}
OUT=${OUT:-$WKT/logs/perf/rss-last.log}
: > "$OUT.rss"
APP=${APP:-TigerBrowser2} SAMPLE=0 OUT="$OUT" "$WKT/spike/wk2web/stage-jsperf.sh" "$URL" "$SECS" > "$OUT.stage" 2>&1 &
stage=$!
# Wait for the launch (the stage script rsyncs first).
while kill -0 $stage 2>/dev/null && ! ssh tiger-eth "ps -axo command | grep -q '[/]wk2jsperf/bin/./TigerWebProcess'"; do sleep 2; done
t0=$(date +%s)
while kill -0 $stage 2>/dev/null; do
    rss=$(ssh tiger-eth "ps -axo rss,command | awk '/[\/]wk2jsperf\/bin\/.\/TigerWebProcess/ {s += \$1} END {print int(s / 1024)}'")
    echo "t=$(( $(date +%s) - t0 )) rss=$rss" >> "$OUT.rss"
    sleep 2
done
wait $stage
awk -F'rss=' '{ if ($2 + 0 > peak) peak = $2 + 0; last = $2 } END { print "peak " peak " MB, last " last " MB" }' "$OUT.rss"
