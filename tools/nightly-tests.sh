#!/bin/sh
# Nightly JSC tests on the box: stress default + the next upstream tier(s) in rotation over
# the whole stress suite, mozilla, then the memoryHog set alone (1 job, 900 s). Box rules as
# tools/run-jsc-tests-box.sh (2 jobs, nice 10, SIGSTOP above load 4; no GUI, no box.lock).
#   tools/nightly-tests.sh [tier ...]     default: the next TIERS_PER_NIGHT tiers in rotation
# Results: logs/jsc-tests/nightly-<YYYYMMDD>/<run>/ plus summary.txt and delta.txt (tests that
# passed in the previous night's run of the same name and do not now: "NEW FAIL").
# Launch at 02:00: tools/nightly-tests.plist (see its header), or cron:
#   0 2 * * * /Users/shg/Developer/WebKitTiger/tools/nightly-tests.sh >> /Users/shg/Developer/WebKitTiger/logs/jsc-tests/nightly.log 2>&1
WKT=/Users/shg/Developer/WebKitTiger
export PATH=/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
TIERS_PER_NIGHT=${TIERS_PER_NIGHT:-1}
# Upstream's stress modes (run-jsc-stress-tests defaultRun) other than default.
ALL_TIERS="no-llint no-cjit-validate-phases dfg-eager ftl-eager no-ftl ftl-eager-no-cjit
ftl-no-cjit-small-pool no-cjit-collect-continuously mini-mode bytecode-cache lockdown
eager-jettison-no-cjit dfg-eager-no-cjit-validate ftl-no-cjit-validate-sampling-profiler
ftl-no-cjit-no-put-stack-validate ftl-no-cjit-no-inline-validate"
ROOT=$WKT/logs/jsc-tests
NIGHT=$ROOT/nightly-$(date +%Y%m%d)
STATE=$ROOT/nightly-rotation   # index of the next tier
mkdir -p "$NIGHT"
exec 8>"$ROOT/nightly.lock"
python3 -c 'import fcntl,sys; fcntl.flock(8, fcntl.LOCK_EX|fcntl.LOCK_NB)' 2>/dev/null || { echo "nightly-tests: already running"; exit 1; }
ssh -o ConnectTimeout=20 tiger-eth true || { echo "nightly-tests: box unreachable"; exit 1; }

if [ $# -gt 0 ]; then
    TIERS="$*"
else
    set -- $ALL_TIERS
    n=$#
    i=$(cat "$STATE" 2>/dev/null || echo 0)
    TIERS=
    k=0
    while [ $k -lt "$TIERS_PER_NIGHT" ]; do
        j=$(( (i + k) % n + 1 ))
        eval "TIERS=\"\$TIERS \${$j}\""
        k=$((k + 1))
    done
    echo $(( (i + TIERS_PER_NIGHT) % n )) > "$STATE"
fi

ninja -C "$WKT/build/tiger-web-tests" jsc > "$NIGHT/build.log" 2>&1 || { echo "nightly-tests: jsc build failed"; exit 1; }
run() { # <name> <args...>
    name=$1; shift
    echo "$(date '+%H:%M') $name"
    RUNROOT=$NIGHT RUNNAME=$name "$WKT/tools/run-jsc-tests-box.sh" "$@" > "$NIGHT/$name.log" 2>&1
    tail -2 "$NIGHT/$name.log" | head -1
}
run stress-default
for t in $TIERS; do run "stress-$t" --modes "$t"; done
run mozilla --suite mozilla --modes mozilla
run stress-memoryhog --memory-hogs-only --jobs 1 --timeout 900

# Delta against the most recent earlier night that has the same run.
python3 - "$ROOT" "$NIGHT" <<'EOF'
import os, sys
root, night = sys.argv[1:]
nights = sorted(d for d in os.listdir(root) if d.startswith("nightly-2") and os.path.join(root, d) != night)
def read(path):
    try: return set(l.strip() for l in open(path) if l.strip())
    except OSError: return None
summary, delta = [], []
for run in sorted(os.listdir(night)):
    rd = os.path.join(night, run)
    passed = read(os.path.join(rd, "pass.txt"))
    if passed is None:
        continue
    failed = (read(os.path.join(rd, "fail.txt")) or set()) | (read(os.path.join(rd, "timeout.txt")) or set())
    summary.append("%-40s pass %5d  fail+timeout %4d" % (run, len(passed), len(failed)))
    prev = next((read(os.path.join(root, n, run, "pass.txt")) for n in reversed(nights)
                 if read(os.path.join(root, n, run, "pass.txt")) is not None), None)
    if prev is None:
        delta.append("%s: no earlier night to compare" % run)
        continue
    new = sorted(failed & prev)
    fixed = sorted(passed - prev)
    delta.append("%s: %d NEW FAIL, %d newly passing" % (run, len(new), len(fixed)))
    delta += ["  NEW FAIL  " + t for t in new] + ["  fixed     " + t for t in fixed[:50]]
open(os.path.join(night, "summary.txt"), "w").write("\n".join(summary) + "\n")
open(os.path.join(night, "delta.txt"), "w").write("\n".join(delta) + "\n")
print("\n".join(summary)); print("\n".join(l for l in delta if not l.startswith("  fixed")))
EOF
echo "nightly-tests: $NIGHT"
