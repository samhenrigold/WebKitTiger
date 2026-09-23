#!/bin/sh
# Run TestWebKitAPI's x86_64 binaries (TestWTF, TestJavaScriptCore) on the box, one
# process per test suite, through tools/jsc-box-driver.pl (2 jobs, nice 10, per-suite
# hard timeout, load pause, dies with its ssh).
#   tools/run-api-tests-box.sh [TestWTF|TestJavaScriptCore] [gtest filter] [timeout secs]
# Build: ninja -C build/tiger-web-tests TestWTF TestJavaScriptCore (-DENABLE_API_TESTS=ON).
# Results: logs/api-tests/<run>/{results.txt,failed.txt,out/*.log}.
set -e
WKT=/Users/shg/Developer/WebKitTiger
BIN=${1:-TestWTF}
FILTER=${2:-*}
TIMEOUT=${3:-300}
BOX=tiger-eth
D=/Users/shg/wk2tests/api-$BIN
RUN=$WKT/logs/api-tests/$(date +%Y%m%d-%H%M%S)-$BIN
mkdir -p "$RUN/out"
ssh $BOX "mkdir -p /Users/shg/wk2tests/bin && rm -rf $D && mkdir -p $D"
rsync -c -z "$WKT/build/tiger-web-tests/bin/TestWebKitAPI/$BIN" "$WKT/tools/jsc-box-driver.pl" $BOX:/Users/shg/wk2tests/bin/
# One script per suite, in the shape jsc-box-driver.pl expects ("echo Running", "FAIL: ").
ssh $BOX "cd $D && perl -e 'alarm 60; exec @ARGV' -- /Users/shg/wk2tests/bin/$BIN --gtest_list_tests '--gtest_filter=$FILTER'" > "$RUN/list-tests.txt"
grep -v '^ ' "$RUN/list-tests.txt" | grep '\.$' | sed 's/\.$//' > "$RUN/suites.txt"
n=0
mkdir -p "$RUN/scripts"
while read -r suite; do
    n=$((n + 1))
    cat > "$RUN/scripts/test_script_$n" <<EOF
echo Running $BIN/$suite
mkdir -p $D/tmp-$n && cd $D/tmp-$n && TMPDIR=$D/tmp-$n /Users/shg/wk2tests/bin/$BIN '--gtest_filter=$suite.*' 2>&1
rc=\$?
cd $D && rm -rf $D/tmp-$n
[ \$rc -ne 0 ] && echo "FAIL: $BIN/$suite rc=\$rc"
true
EOF
    echo "test_script_$n" >> "$RUN/scripts/list.txt"
done < "$RUN/suites.txt"
echo "run-api-tests-box: $n suites -> $RUN"
rsync -z -r "$RUN/scripts/" $BOX:$D/
ssh $BOX "cd $D && perl /Users/shg/wk2tests/bin/jsc-box-driver.pl . list.txt results.txt 2 $TIMEOUT < /dev/null" || echo "driver exited $?"
scp -qO $BOX:$D/results.txt "$RUN/results.txt"
rsync -z "$BOX:$D/test_script_*.log" "$RUN/out/"
cat "$RUN"/out/*.log | grep -E '^\[  (PASSED|FAILED)  \]|^\[==========\] [0-9]+ tests? from' > "$RUN/gtest-summary.txt" || true
grep -h '^\[  FAILED  \] [A-Za-z_0-9]*\.' "$RUN"/out/*.log | grep -v ' listed below' | sed 's/ (.*//' | sort -u > "$RUN/failed.txt" || true
passed=$(grep -h '^\[  PASSED  \]' "$RUN"/out/*.log | awk '{s += $4} END {print s + 0}')
echo "suites: $(awk '$1=="P"' "$RUN/results.txt" | wc -l | tr -d ' ') ok, $(awk '$1=="F"' "$RUN/results.txt" | wc -l | tr -d ' ') failing, $(awk '$1=="T"' "$RUN/results.txt" | wc -l | tr -d ' ') timed out"
echo "tests: $passed passed, $(wc -l < "$RUN/failed.txt" | tr -d ' ') failed (list: $RUN/failed.txt)"
awk '$1!="P" {print}' "$RUN/results.txt"
