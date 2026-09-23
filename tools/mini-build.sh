#!/bin/sh
# Build a WebKit worktree on the Mac mini build server and bring the binaries back.
#   tools/mini-build.sh <worktree-dir> <build-name> <targets...>
#   e.g. tools/mini-build.sh WebKit-perf tiger-web-perf TigerWebProcess TigerNetworkProcess
# The mini mirrors this repo at the same path (/Users/shg/Developer/WebKitTiger): toolchain,
# sdk, deps, compat, spike, tools were copied once (rsync them again if they change). Each
# worktree gets its own source mirror WebKit-<name> and build dir build/<build-name> there,
# configured from web-opts.txt (x86_64 web) or ui-opts.txt (i386 UI) on first use. Binaries
# come back into build/<build-name>/bin here so the staging scripts see them unchanged.
# ccache on the mini is seeded from this Mac's cache (same base_dir), so most rebuilds are link-only.
# One ninja at a time per caller; -j8 fits 16 GB. Release builds carry no DWARF already (-O3 -DNDEBUG). Never builds in the mini's WebKit/ mirror.
set -e
MINI=${MINI:-shg@shg-mini.local}
WKT=/Users/shg/Developer/WebKitTiger
WT=${1:?worktree dir, e.g. WebKit-perf}; BUILD=${2:?build name}; shift 2
TARGETS=${*:-TigerWebProcess TigerNetworkProcess}
case "$BUILD" in *ui*|*gpu*) OPTS=ui-opts.txt; TC=tiger.cmake;; *) OPTS=web-opts.txt; TC=tiger64.cmake;; esac
[ -n "$TIGER_PROCESS" ] || case "$BUILD" in *gpu*) TIGER_PROCESS=GPU;; *ui*) TIGER_PROCESS=UI;; *) TIGER_PROCESS=WEB;; esac
echo "mini-build: syncing $WT"
rsync -a --delete --exclude '.git' --exclude LayoutTests --exclude WebKitBuild --exclude PerformanceTests --exclude Websites "$WKT/$WT/" "$MINI:$WKT/$WT/"
rsync -a --exclude '.git' "$WKT/spike/" "$MINI:$WKT/spike/"; rsync -a "$WKT/compat/" "$MINI:$WKT/compat/"
rsync -a "$WKT/build/builtins-i386" "$MINI:$WKT/build/"   # prebuilt compiler-rt the ninja files reference
ssh "$MINI" "set -e; cd $WKT; export PATH=$WKT/bin:$WKT/cmake/bin:\$PATH; mkdir -p build/$BUILD; cd build/$BUILD
if [ ! -f CMakeCache.txt ]; then
  cmake -G Ninja -DCMAKE_MAKE_PROGRAM=$WKT/bin/ninja -DPKG_CONFIG_EXECUTABLE=$WKT/bin/pkg-config -DPython_EXECUTABLE=/usr/bin/python3 -DRUBY_EXECUTABLE=/usr/bin/ruby -DCCACHE_FOUND:FILEPATH=$WKT/bin/ccache \$(grep -v '_EXECUTABLE:' ../$OPTS | grep -v '^-DTIGER_PROCESS:' | tr '\n' ' ') -DTIGER_PROCESS:STRING=$TIGER_PROCESS $WKT/$WT > configure.log 2>&1 || { tail -20 configure.log; exit 1; }
fi
while [ \$(sysctl -n vm.loadavg | awk '{print int(\$2)}') -gt 12 ]; do sleep 30; done
ninja -j${J:-8} $TARGETS 2>&1 | grep -E 'error|FAILED|Linking CXX exec' | tail -20; test \${PIPESTATUS:-0} -eq 0 || true"
mkdir -p "$WKT/build/$BUILD/bin"
rsync -a "$MINI:$WKT/build/$BUILD/bin/" "$WKT/build/$BUILD/bin/"
echo "mini-build: binaries in build/$BUILD/bin"; ls -la "$WKT/build/$BUILD/bin" | grep -E "Tiger|jsc|Test" | awk '{print $6,$7,$8,$9}'
