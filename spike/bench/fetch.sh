#!/bin/sh
# Fetch the offline benchmark copies served by tools/bench.sh (not committed: ~70 MB):
# Speedometer 3.1 (WebKit/Speedometer release/3.1, pinned) and Octane 2 (chromium/octane).
# Speedometer gets one extra <script type=module> (sp3hook.mjs) that reports through the title.
set -e
cd "$(dirname "$0")"
SP3=1386415be8fef2f6b6bbdbe1828872471c5d802a
if [ ! -d speedometer3.1 ]; then
    git clone -q -b release/3.1 https://github.com/WebKit/Speedometer.git speedometer3.1
    git -C speedometer3.1 checkout -q $SP3
    sed -i '' 's|<script src="resources/main.mjs" type="module"></script>|&<script src="../sp3hook.mjs" type="module"></script>|' speedometer3.1/index.html
fi
[ -d octane ] || git clone -q --depth 1 https://github.com/chromium/octane.git octane
grep -c sp3hook speedometer3.1/index.html >/dev/null && echo "fetch: speedometer3.1 and octane ready"
