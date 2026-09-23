#!/bin/sh
# Assemble matched artifacts and install without terminating the user's app.
# INSTALL=0 assembles only. UIDIR/WEBDIR/GPUDIR select explicit build directories.
# BUNDLE overrides build/TigerBrowser.app; previous local/remote bundles are retained.
set -eu
WKT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec python3 "$WKT/tools/tiger-run.py" bundle "$@"
