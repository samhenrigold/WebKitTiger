#!/bin/bash
# Build spike/decodebench for Tiger x86_64. Output: build/decodebench64
set -euo pipefail
WKT=/Users/shg/Developer/WebKitTiger
S=$WKT/toolchain/sysroot-x86_64/usr
mkdir -p "$WKT/build"
"$WKT/toolchain/bin/tiger-clang64" -O2 -o "$WKT/build/decodebench64" "$WKT/spike/decodebench.c" \
  -lavformat -lavcodec -lswscale -lswresample -lavutil -ldav1d -ltigercompat \
  -lpthread -lm
echo "built $WKT/build/decodebench64"
