#!/bin/sh
# Benchmark the shipped build on the box, one page per run, and write a report.
#
#   tools/bench.sh                 # every page (~25 runs of <= 100 s, plus the two JS suites)
#   tools/bench.sh wiki x fwiki    # only the named pages
#   tools/bench.sh --list          # the page names
#   OUT=logs/bench/<ts> tools/bench.sh nyt   # add or redo pages in an existing run dir
#   tools/bench-report.py logs/bench/<ts>    # rebuild report.md from the logs alone
#
# Runs build/tiger-ui-port, build/tiger-web-port, build/tiger-gpu and build/tigeraudio32
# as they are, through spike/wk2web/stage-app.sh (box lock, waits for a clear box, kills
# only what it staged, HOME=/Users/shg/wk2/home). Nothing is built. spike/bench/benchstamp.pl
# goes last in APP_ENV, so stage-app's `env ... ./TigerBrowser2` runs the app under it: every
# log line gets its seconds since launch and the process table is logged at 30 s and 88 s.
#
# The standard 92 s script (fast mode unless the page says TIGER_FAITHFUL=1):
#   0-30 s  a mouse move every 0.5 s between two points (time-to-interactive probe)
#   30-60 s 40 wheel ticks of 3 lines at 480,350, one per 0.75 s
#   60-90 s 15 moves over five points, one per 2 s (hover-to-cursor)
# with TIGER_INPUTLOG, TIGER_PAINT_PROBE, TIGER_JS_PROBE, TIGER_SAMPLE_MAIN (250 ms, all
# threads) and the Layout log channel on. The JS suites (spike/bench/fetch.sh fetches them)
# run with no probes; the score comes back through the page title (TIGER title: lines).
set -u
WKT=/Users/shg/Developer/WebKitTiger
STAGE=$WKT/spike/wk2web/stage-app.sh
MAC=192.168.1.253
MEDIA=$MAC:8765     # spike/media, as tools/regress.sh serves it
BENCH=$MAC:8766     # spike/bench

# name|url|seconds|script|extra env
STD=std
pages() { cat <<EOF
wiki|https://en.wikipedia.org/wiki/Mac_OS_X_Tiger|92|$STD|
react|https://react.dev/|92|$STD|
github|https://github.com/WebKit/WebKit|92|$STD|
appledoc|https://developer.apple.com/documentation|92|$STD|
nyt|https://www.nytimes.com/|92|$STD|
verge|https://www.theverge.com/|92|$STD|
x|https://x.com/|92|$STD|
youtube|https://www.youtube.com/watch?v=f7NwyBnIRTE|92|$STD|
v480|http://$MEDIA/video480loop.html|92|$STD|
v720|http://$BENCH/video720loop.html|92|$STD|
octane|http://$BENCH/octane.html|420|wait 1|js
sp3|http://$BENCH/speedometer3.1/index.html?startAutomatically&iterationCount=1|600|wait 1|js
fwiki|https://en.wikipedia.org/wiki/Mac_OS_X_Tiger|92|$STD|TIGER_FAITHFUL=1
fx|https://x.com/|92|$STD|TIGER_FAITHFUL=1
fv480|http://$MEDIA/video480loop.html|92|$STD|TIGER_FAITHFUL=1
fv720|http://$BENCH/video720loop.html|92|$STD|TIGER_FAITHFUL=1
fnyt|https://www.nytimes.com/|92|$STD|TIGER_FAITHFUL=1
fverge|https://www.theverge.com/|92|$STD|TIGER_FAITHFUL=1
n720|http://$BENCH/video720native.html|45|wait 1; move 8,8; wait 39|TIGER_TEST_WINDOW_SIZE=1280x772 TIGER_VIDEO_PROBE=1
fn720|http://$BENCH/video720native.html|45|wait 1; move 8,8; wait 39|TIGER_TEST_WINDOW_SIZE=1280x772 TIGER_VIDEO_PROBE=1 TIGER_FAITHFUL=1
EOF
}
case "${1:-}" in --list) pages | cut -d'|' -f1 | tr '\n' ' '; echo; exit 0;; esac
WANTED=${*:-$(pages | cut -d'|' -f1)}

std_script() {
    s="wait 0.5"
    i=0; while [ $i -lt 58 ]; do
        [ $((i % 2)) = 0 ] && p=300,200 || p=620,330
        s="$s; move $p; wait 0.2"; i=$((i + 1)); done
    i=0; while [ $i -lt 40 ]; do s="$s; wheel 480,350 0,-3; wait 0.45"; i=$((i + 1)); done
    for p in 150,120 480,200 700,300 300,420 620,520 150,120 480,200 700,300 300,420 620,520 150,120 480,200 700,300 300,420 620,520; do
        s="$s; move $p; wait 1.7"; done
    echo "$s"
}

OUT=${OUT:-$WKT/logs/bench/$(date +%Y%m%d-%H%M%S)-$$}
mkdir -p "$OUT" && OUT=$(cd "$OUT" && pwd)
for name in $WANTED; do
    pages | cut -d'|' -f1 | grep -qx "$name" || { echo "bench: unknown page $name" >&2; exit 2; }
done
candidate_path() { case "$1" in /*) printf '%s\n' "$1";; *) printf '%s/%s\n' "$WKT" "$1";; esac; }
python3 "$WKT/tools/tiger-artifacts.py" verify \
    --ui "$(candidate_path "${UIDIR:-build/tiger-ui-port}")" \
    --web "$(candidate_path "${WEBDIR:-build/tiger-web-port}")" \
    --gpu "$(candidate_path "${GPUDIR:-build/tiger-gpu}")" > "$OUT/candidate.json" || exit 1
candidate_path "${WEBDIR:-build/tiger-web-port}" > "$OUT/web-build-dir.txt"
case " $WANTED " in *" n720 "*|*" fn720 "*)
    # Extract the parser from the verified candidate's Git object, never from a
    # mutable worktree. The snapshot records its source commit/blob and SHA-256.
    python3 "$WKT/tools/tiger-video-bench.py" prepare --candidate "$OUT/candidate.json" \
        --repo "$WKT/WebKit" --snapshot "$OUT/video-paint-tool" > "$OUT/video-paint-parser.json" || exit 1;;
esac
case " $WANTED " in *" sp3 "*|*" octane "*)
    [ -d "$WKT/spike/bench/speedometer3.1" ] || sh "$WKT/spike/bench/fetch.sh" || exit 1;;
esac

# The two page servers on this Mac. Only the ones started here are stopped at the end.
STARTED=""
serve() { # serve <port> <dir>
    curl -sf -m 3 -o /dev/null "http://$MAC:$1/" && return
    python3 -m http.server "$1" -d "$WKT/$2" > "$OUT/server-$1.log" 2>&1 &
    server_pid=$!; STARTED="$STARTED $server_pid"
    sleep 2
    kill -0 "$server_pid" 2>/dev/null || { cat "$OUT/server-$1.log" >&2; return 1; }
}
cleanup() { for p in $STARTED; do kill "$p" 2>/dev/null || true; done; }
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
serve 8765 spike/media || exit 1
serve 8766 spike/bench || exit 1

run() { # run <name> <url> <secs> <script> <extra env>
    name=$1; url=$2; secs=$3; script=$4; extra=$5
    [ "$script" = "$STD" ] && script=$(std_script)
    env="TIGER_INPUTLOG=1 TIGER_PAINT_PROBE=1 TIGER_JS_PROBE=1 TIGER_SAMPLE_MAIN=$((secs * 4)) WEBKIT_DEBUG=Process,Loading,Layout $extra"
    # The JS suites: no probes at all, only the title log (and the console) for the score.
    [ "$extra" = js ] && env="TIGER_CONSOLE=1"
    tries=0
    while :; do
        # Staging waits for an idle box and copies benchstamp while holding its lease.
        echo "== $name ($secs s) $(date +%T)"
        APP=TigerBrowser2 SHOT="$OUT/$name.png" LOG="$OUT/$name.log" \
        APP_ENV="$env TIGER_SCRIPT='$script' BENCH_PS_AT='30 $((secs - 4))' /Users/shg/wk2/share/benchstamp.pl" \
            sh "$STAGE" "$url" "$secs" > "$OUT/$name.stage.log" 2>&1
        rc=$?
        # stage-app gives up after 10 min on the lock or 5 min on a busy box: try again.
        if [ $rc != 0 ] && grep -q 'box busy\|box still busy\|occupied' "$OUT/$name.stage.log"; then
            tries=$((tries + 1)); echo "   $(date +%T) box busy, retry $tries"; sleep 30; continue
        fi
        case "$name" in n720|fn720)
            if [ "$rc" = 0 ] && ! grep -q 'NATIVE720 READY' "$OUT/$name.log"; then
                echo '   invalid native-720 workload: source or viewport dimensions did not match'
                rc=1
            fi
            mode=fast; [ "$name" = fn720 ] && mode=faithful
            # UI window paints only: 8 s warmup lets the controls finish fading.
            # The helper persists the report/rejection reasons and propagates all
            # parser, duration, source-size, presentation-rate and gap failures.
            python3 "$WKT/tools/tiger-video-bench.py" check --snapshot "$OUT/video-paint-tool" \
                --log "$OUT/$name.log" --output "$OUT/$name.video-paints.json" --mode "$mode" || rc=1;;
        esac
        [ $rc = 0 ] || echo "   stage-app.sh exited $rc (see $name.stage.log)"
        echo "$url" > "$OUT/$name.url"
        echo "$rc" > "$OUT/$name.exit-status"
        return $rc
    done
}

FAILS=0
for name in $WANTED; do
    line=$(pages | grep "^$name|") || { echo "bench: no page $name"; continue; }
    IFS='|' read -r n url secs script extra <<EOF
$line
EOF
    run "$n" "$url" "$secs" "$script" "$extra" || FAILS=$((FAILS + 1))
done

python3 "$WKT/tools/bench-report.py" "$OUT" > /dev/null || exit 1
echo "tables: $OUT/tables.md"
[ "$FAILS" -eq 0 ]
