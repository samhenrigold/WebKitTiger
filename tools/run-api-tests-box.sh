#!/bin/sh
# Run TestWebKitAPI's x86_64 binaries (TestWTF, TestJavaScriptCore) on the box, one
# process per test, through tools/jsc-box-driver.pl (2 jobs, nice 10, per-test
# hard timeout, load pause, dies with its ssh).
#   tools/run-api-tests-box.sh [TestWTF|TestJavaScriptCore] [gtest filter] [timeout secs]
# Build: ninja -C build/tiger-web-tests TestWTF TestJavaScriptCore (-DENABLE_API_TESTS=ON).
# Results: logs/api-tests/<run>/{results.txt,failed.txt,out/*.log}.
set -e
WKT=/Users/shg/Developer/WebKitTiger
BIN=${1:-TestWTF}
FILTER=${2:-*}
TIMEOUT=${3:-120}
BOX=tiger-eth
D=/Users/shg/wk2tests/api-$BIN
RUN=$WKT/logs/api-tests/$(date +%Y%m%d-%H%M%S)-$BIN
mkdir -p "$RUN/out"
ssh $BOX "mkdir -p /Users/shg/wk2tests/bin && rm -rf $D && mkdir -p $D"
rsync -c -z "$WKT/build/tiger-web-tests/bin/TestWebKitAPI/$BIN" "$WKT/tools/jsc-box-driver.pl" $BOX:/Users/shg/wk2tests/bin/
ssh $BOX "cd $D && perl -e 'alarm 60; exec @ARGV' -- ../bin/$BIN --gtest_list_tests '--gtest_filter=$FILTER'" > "$RUN/list-tests.txt"
# One process per test (a crash takes out only its own test), in the shape
# (run by a relative path: stage scripts on the box kill every process whose argv[0]
# contains /wk2<letters>/, which includes /Users/shg/wk2tests/),
# jsc-box-driver.pl expects ("echo Running", "FAIL: "). DISABLED_ tests are counted, not run.
awk '/^[^ ]/ {suite = $1} /^  / && $1 !~ /^DISABLED_/ {print suite $1}' "$RUN/list-tests.txt" > "$RUN/tests.txt"
grep -c '^  DISABLED_' "$RUN/list-tests.txt" > "$RUN/disabled-count.txt" || true
n=0
mkdir -p "$RUN/scripts"
while read -r test; do
    n=$((n + 1))
    cat > "$RUN/scripts/test_script_$n" <<EOF
echo Running $BIN/$test
mkdir -p $D/tmp-$n && cd $D/tmp-$n && TMPDIR=$D/tmp-$n ../../bin/$BIN '--gtest_filter=$test' > $D/out-$n.txt 2>&1
rc=\$?
cat $D/out-$n.txt; cd $D && rm -rf $D/tmp-$n
if [ \$rc -ne 0 ] || grep -q '^\*\*FAIL\*\*' $D/out-$n.txt; then echo "FAIL: $BIN/$test rc=\$rc"; fi
rm -f $D/out-$n.txt
EOF
    echo "test_script_$n" >> "$RUN/scripts/list.txt"
done < "$RUN/tests.txt"
echo "run-api-tests-box: $n tests -> $RUN"
rsync -z -r "$RUN/scripts/" $BOX:$D/
ssh $BOX "cd $D && perl /Users/shg/wk2tests/bin/jsc-box-driver.pl . list.txt results.txt 2 $TIMEOUT < /dev/null" || echo "driver exited $?"
scp -qO $BOX:$D/results.txt "$RUN/results.txt"
rsync -z "$BOX:$D/test_script_*.log" "$RUN/out/" 2>/dev/null || true
awk '$1 != "P" {print $1, $4}' "$RUN/results.txt" | sed 's|TestWTF/||; s|TestJavaScriptCore/||' | sort -k2 > "$RUN/failed.txt"
echo "tests: $(awk '$1=="P"' "$RUN/results.txt" | wc -l | tr -d ' ') pass, $(awk '$1=="F"' "$RUN/results.txt" | wc -l | tr -d ' ') fail, $(awk '$1=="T"' "$RUN/results.txt" | wc -l | tr -d ' ') timeout, $(cat "$RUN/disabled-count.txt") disabled"
cat "$RUN/failed.txt"
