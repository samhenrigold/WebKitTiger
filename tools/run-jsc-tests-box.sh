#!/bin/sh
# Run JavaScriptCore's own test suites (JSTests) against the TIGER64 jsc shell on the box.
#
#   tools/run-jsc-tests-box.sh [--suite stress|mozilla|chakra] [--modes default,no-llint,...]
#                              [--filter REGEX] [--timeout SECS] [--jobs N] [--jsc PATH]
#                              [--memory-hogs]
#
# Upstream's run-jsc-stress-tests does the planning (the //@ directives, the tier variants,
# the output checks) and writes a bundle of one shell script per test and mode; this script
# ships the chosen scripts to the box and runs them there with tools/jsc-box-driver.pl:
# 2 at a time, nice 10, each in its own process group with a hard timeout, pausing while
# the load average is over 4. No GUI, no box.lock. Results land in logs/jsc-tests/<run>/:
# pass.txt, fail.txt, timeout.txt, signatures.txt (failures grouped), out/<test>.out.
#
# Modes are upstream's names: default, no-llint, no-cjit-validate-phases, dfg-eager,
# ftl-eager, no-ftl, ftl-no-cjit-small-pool, mini-mode, bytecode-cache, lockdown, ...
set -e
WKT=/Users/shg/Developer/WebKitTiger
SRC=${SRC:-$WKT/WebKit-tests}
JSC=$WKT/build/tiger-web-tests/bin/jsc
SUITE=stress
MODES=default
FILTER=
TIMEOUT=120
JOBS=2
MEMHOGS=0   # --memory-hogs: also run //@ memoryHog tests. Off by default: two of them at once
            # exhausted the box's swap ("no space in available paging segments"), and fresh
            # processes then died of SIGBUS.
BOX=tiger-eth
BOXDIR=/Users/shg/wk2tests
while [ $# -gt 0 ]; do
    case "$1" in
        --suite) SUITE=$2; shift ;;
        --modes) MODES=$2; shift ;;
        --filter) FILTER=$2; shift ;;
        --timeout) TIMEOUT=$2; shift ;;
        --jobs) JOBS=$2; shift ;;
        --jsc) JSC=$2; shift ;;
        --memory-hogs) MEMHOGS=1 ;;
        *) sed -n '2,20p' "$0"; exit 1 ;;
    esac
    shift
done
case "$SUITE" in
    stress) COLL=$SRC/JSTests/stress ;;
    mozilla) COLL=$SRC/JSTests/mozilla/mozilla-tests.yaml ;;
    chakra) COLL=$SRC/JSTests/ChakraCore.yaml ;;
    *) echo "unknown suite $SUITE"; exit 1 ;;
esac

RUN=$WKT/logs/jsc-tests/$(date +%Y%m%d-%H%M%S)-$SUITE-$(echo "$MODES" | tr , +)
BUNDLE=$WKT/build/tiger-web-tests/jsc-bundle-$SUITE
mkdir -p "$RUN/out"

# 1. Plan: upstream writes the bundle (tests, helpers, jsc, .runner/test_script_*).
rm -rf "$BUNDLE"
ruby "$SRC/Tools/Scripts/run-jsc-stress-tests" --jsc "$JSC" --arch x86_64 --os darwin \
    --tarball "$SUITE.tgz" -o "$BUNDLE" ${FILTER:+--filter "$FILTER"} "$COLL" > "$RUN/plan.log" 2>&1
rm -f "$(dirname "$BUNDLE")/$SUITE.tgz"
ln "$BUNDLE/.vm/JavaScriptCore.framework/Helpers/jsc" "$RUN/jsc"   # for symbolizing later

# 2. Pick the scripts whose mode is in $MODES (the name is "<suite>/<test>.<mode>").
( cd "$BUNDLE/.runner" && find . -name 'test_script_*' -print0 | xargs -0 grep -H -m1 '^echo Running' ) |
    python3 -c '
import os, sys
modes, jstests, hogs = sys.argv[1].split(","), sys.argv[2], sys.argv[3] == "1"
skipped = open(sys.argv[4], "w")
for line in sys.stdin:
    path, _, name = line.strip().partition(":echo Running ")
    if not any(name.endswith("." + m) for m in modes):
        continue
    source = os.path.join(jstests, name.rsplit(".", 1)[0])
    if not hogs and os.path.exists(source) and "//@ memoryHog" in open(source, errors="replace").read(4096):
        skipped.write(name + "\n")
        continue
    print(path[2:])
' "$MODES" "$SRC/JSTests" "$MEMHOGS" "$RUN/skipped-memoryhog.txt" | sort -t_ -k3n > "$RUN/list.txt"
echo "run-jsc-tests-box: $(wc -l < "$RUN/list.txt" | tr -d ' ') tests ($SUITE, modes $MODES; $(wc -l < "$RUN/skipped-memoryhog.txt" | tr -d ' ') memoryHog skipped) -> $RUN"

# 3. Ship: the bundle by checksum (a regenerated bundle has fresh mtimes), minus the
#    scripts, then only the chosen scripts.
ssh $BOX "mkdir -p $BOXDIR/bin $BOXDIR/$SUITE && rm -rf $BOXDIR/$SUITE/.runner && mkdir $BOXDIR/$SUITE/.runner"
rsync -rlc -z --delete --exclude=/.runner "$BUNDLE/" $BOX:$BOXDIR/$SUITE/
rsync -c -z --files-from="$RUN/list.txt" "$BUNDLE/.runner/" $BOX:$BOXDIR/$SUITE/.runner/
rsync -c "$RUN/list.txt" $BOX:$BOXDIR/$SUITE/.runner/
rsync -c "$WKT/tools/jsc-box-driver.pl" $BOX:$BOXDIR/bin/

# 4. Run. The driver kills its children and exits if this ssh goes away. jsc's soft
#    JSCTEST_timeout never fires (upstream jsc.cpp's timeout thread looks for a VM that is
#    NOT the main one), but its hard timeout exits cleanly 5 s later ("HARD TIMEOUT");
#    the driver's SIGKILL at +30 s is the backstop.
#    TZ=US/Pacific as upstream runs them (the Intl/Date tests assume it).
ssh $BOX "cd $BOXDIR/$SUITE/.runner && TZ=US/Pacific JSCTEST_timeout=$TIMEOUT JSCTEST_hardTimeout=5 perl $BOXDIR/bin/jsc-box-driver.pl . list.txt results.txt $JOBS $((TIMEOUT + 30)) < /dev/null" || echo "run-jsc-tests-box: driver exited $?"

# 5. Collect: verdicts, the log of every failure, then group by signature.
scp -qO $BOX:$BOXDIR/$SUITE/.runner/results.txt "$RUN/results.txt"
awk '$1 != "P" {print $2 ".log"}' "$RUN/results.txt" > "$RUN/faillogs.txt"
[ -s "$RUN/faillogs.txt" ] && { rsync -z --files-from="$RUN/faillogs.txt" $BOX:$BOXDIR/$SUITE/.runner/ "$RUN/out/" || true; }
python3 "$WKT/tools/jsc-results.py" "$RUN"
