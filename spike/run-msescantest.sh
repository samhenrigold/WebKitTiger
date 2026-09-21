#!/bin/bash
# Build spike/msescan.c + its tests for Tiger x86_64, deploy, and run on the box.
# Needs the DASH segments from spike/make-mse-segments.sh already on the box
# (spike/run-msebench.sh copies them to /tmp/media64/{fmp4,fwebm}).
set -euo pipefail
WKT=/Users/shg/Developer/WebKitTiger
cd "$WKT"

"$WKT/toolchain/bin/tiger-clang64" -O2 -o "$WKT/build/msescantest64" \
  "$WKT/spike/msescantest.c" "$WKT/spike/msescan.c" \
  -lavformat -lavcodec -lswscale -lswresample -lavutil -ldav1d -ltigercompat -lpthread -lm

ssh tiger 'test -d /tmp/media64/fmp4' || {
  ssh tiger 'mkdir -p /tmp/media64/fmp4 /tmp/media64/fwebm'
  scp -O spike/media64/fmp4/*  tiger:/tmp/media64/fmp4/  >/dev/null
  scp -O spike/media64/fwebm/* tiger:/tmp/media64/fwebm/ >/dev/null
}
scp -O build/msescantest64 tiger:/tmp/ >/dev/null
ssh tiger 'chmod +x /tmp/msescantest64; /tmp/msescantest64 /tmp/media64/fmp4 /tmp/media64/fwebm' \
  | tee "$WKT/logs/msescantest-tiger.txt"
