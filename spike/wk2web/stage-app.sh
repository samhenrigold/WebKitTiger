#!/bin/sh
# Matched artifacts, local + Tiger-side lease, unique binaries/logs, isolated HOME.
# UIDIR/WEBDIR/GPUDIR select build dirs; APP_ENV contains quoted assignments and
# optionally a wrapper command (parsed as argv, never evaluated by a shell).
# SHOT/LOG override build/runs/<id>/ outputs. RUN_HOME_NEW=1 uses a fresh profile;
# default /Users/shg/wk2/home preserves cookies across staged launches.
set -eu
WKT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec python3 "$WKT/tools/tiger-run.py" stage "$@"
