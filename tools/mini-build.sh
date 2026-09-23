#!/bin/sh
# Cross-build on the Mini, verify the results, then publish them locally.
# Usage: tools/mini-build.sh WebKit-perf tiger-web-perf TigerWebProcess TigerNetworkProcess
# MINI, WKT, TIGER_PROCESS, J and EXTRA_CMAKE retain their previous meanings.
set -eu
exec python3 "$(dirname "$0")/mini-build.py" "$@"
