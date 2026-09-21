#!/bin/bash
# Build msebench, deploy it and the DASH segments, and run the append-model matrix.
# Output: logs/msebench-tiger.txt
set -euo pipefail
WKT=/Users/shg/Developer/WebKitTiger
cd "$WKT"

"$WKT/toolchain/bin/tiger-clang64" -O2 -o "$WKT/build/msebench64" "$WKT/spike/msebench.c" \
  -lavformat -lavcodec -lswscale -lswresample -lavutil -ldav1d -ltigercompat -lpthread -lm

ssh tiger 'rm -rf /tmp/media64/fmp4 /tmp/media64/fwebm; mkdir -p /tmp/media64/fmp4 /tmp/media64/fwebm'
scp -O spike/media64/fmp4/*  tiger:/tmp/media64/fmp4/  >/dev/null
scp -O spike/media64/fwebm/* tiger:/tmp/media64/fwebm/ >/dev/null
scp -O build/msebench64 tiger:/tmp/ >/dev/null

ssh tiger 'chmod +x /tmp/msebench64
run() { d="$1"; shift; echo; echo "##### $d"; "$@" 2>&1 | grep -v "^\[" || true; }

cd /tmp/media64/fmp4
run "fMP4 H.264 video, reopen per append"   /tmp/msebench64 -m reopen -f mp4 -q init-stream0.m4s chunk-stream0-*.m4s
run "fMP4 AAC audio, reopen per append"     /tmp/msebench64 -m reopen -f mp4 -q init-stream1.m4s chunk-stream1-*.m4s
run "fMP4 H.264 video, one streaming context, open on init+1" /tmp/msebench64 -m stream -f mp4 -d 1 -q init-stream0.m4s chunk-stream0-*.m4s
run "fMP4 H.264 video, one streaming context, open on init only" /tmp/msebench64 -m stream -f mp4 -d 0 -q init-stream0.m4s chunk-stream0-*.m4s

cd /tmp/media64/fmp4
run "fMP4 H.264, reopen + box scan, whole-segment appends" /tmp/msebench64 -m reopen -f mp4 -b -q -p 1 init-stream0.m4s chunk-stream0-*.m4s
run "fMP4 H.264, reopen + box scan, 4 partial appends per segment" /tmp/msebench64 -m reopen -f mp4 -b -q -p 4 init-stream0.m4s chunk-stream0-*.m4s
run "fMP4 H.264, reopen + box scan, 64 partial appends per segment" /tmp/msebench64 -m reopen -f mp4 -b -q -p 64 init-stream0.m4s chunk-stream0-*.m4s
run "fMP4 H.264, reopen WITHOUT box scan, 4 partial appends per segment" /tmp/msebench64 -m reopen -f mp4 -q -p 4 init-stream0.m4s chunk-stream0-*.m4s

cd /tmp/media64/fwebm
run "WebM VP9 video, reopen per append"     /tmp/msebench64 -m reopen -f matroska -q init-stream0.webm chunk-stream0-*.webm
run "WebM Opus audio, reopen per append"    /tmp/msebench64 -m reopen -f matroska -q init-stream1.webm chunk-stream1-*.webm
run "WebM VP9 video, one streaming context, open on init+1" /tmp/msebench64 -m stream -f matroska -d 1 -q init-stream0.webm chunk-stream0-*.webm
run "WebM VP9, reopen + element scan, 4 partial appends per segment" /tmp/msebench64 -m reopen -f matroska -b -q -p 4 init-stream0.webm chunk-stream0-*.webm
run "WebM VP9 video, one streaming context, open on init only" /tmp/msebench64 -m stream -f matroska -d 0 -q init-stream0.webm chunk-stream0-*.webm
' | tee "$WKT/logs/msebench-tiger.txt"
