#!/bin/sh
# Regression harness for the Tiger port: runs the main build dirs on the box and
# says pass or fail per page, with a diff report.
#
#   tools/regress.sh                 # checks for the integrated baseline
#   tools/regress.sh --all           # also pending-topic checks (requires those features)
#   tools/regress.sh example scroll  # only the named checks
#   tools/regress.sh --list          # the check names
#   BLESS=1 tools/regress.sh boxtest # run it, then make its shot the new golden
#
# What it runs: build/tiger-ui-port (TigerBrowser2), build/tiger-web-port
# (TigerWebProcess, TigerNetworkProcess), build/tiger-gpu (TigerGPUProcess) and
# build/tigeraudio32, exactly as they are -- the harness never builds anything.
# Staging goes through spike/wk2web/stage-app.sh, which takes the box lock on fd 9,
# waits for a clear box, aborts if the user's TigerBrowser.app is up, and kills only
# what it staged. The test pages are rsynced to /Users/shg/wk2/share by that script.
#
# Every run also checks the app log: no TIGER-CRASH, no TIGER-ABORT, no "unresponsive",
# no "cannot connect to :", no "Autoplay blocked", and at least one
# "TIGER ui: incorporate" (TIGER_PAINT_PROBE=1 is set for every run, so a run that
# paints nothing at all is a failure even when the screenshot happens to look right).
#
# Screenshot checks compare the window content rect against tests/regress/golden/<name>.png
# with tools/regress-compare.py: per-pixel tolerance, a cap on the fraction of differing
# pixels, and the fraction is always printed. Live sites (x.com, youtube.com) cannot have
# a golden, so they are checked for crashes and for ink where ink is expected.
#
# Results, one directory per run: logs/regress/<timestamp>/ with <name>.png, <name>.log
# (the box's app.log), <name>.stage.log and summary.md.
set -u
WKT=/Users/shg/Developer/WebKitTiger
STAGE=${STAGE:-$WKT/spike/wk2web/stage-app.sh}   # a track's copy (stage-app-features.sh) stages into its own box dir
COMPARE=$WKT/tools/regress-compare.py
GOLDEN=$WKT/tests/regress/golden
SHARE=${SHARE:-/Users/shg/wk2/share}             # ...and then SHARE is that dir's share
MEDIA_HOST=${MEDIA_HOST:-192.168.1.253:8765}
BLESS=${BLESS:-}

ALL="example scroll controls boxtest textarea xcom video youtube fexample fscroll fcontrols cookies ghost fghost relaunch frelaunch features scrollbars fscrollbars hostedclip fhostedclip videolifetime fvideolifetime"
DEFAULT="example scroll controls boxtest textarea xcom video youtube fexample fscroll fcontrols cookies ghost fghost"
case "${1:-}" in --list) echo "$ALL"; exit 0;; --all) shift; set -- $ALL "$@";; esac
WANTED=${*:-$DEFAULT}
for requested in $WANTED; do
    case " $ALL " in *" $requested "*) ;; *) echo "regress: unknown check: $requested" >&2; exit 2;; esac
done

OUT=${OUT:-$WKT/logs/regress/$(date +%Y%m%d-%H%M%S)-$$}
mkdir -p "$OUT" "$GOLDEN"
SUMMARY=$OUT/summary.md
PASSES=0; FAILS=0
SERVER_PIDS=""
STAGE_FAILURES=""
cleanup_servers() { for server_pid in $SERVER_PIDS; do kill "$server_pid" 2>/dev/null || true; done; }
trap cleanup_servers EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

# Fail before starting servers or touching the box if the candidate is incomplete.
candidate_path() { case "$1" in /*) printf '%s\n' "$1";; *) printf '%s/%s\n' "$WKT" "$1";; esac; }
python3 "$WKT/tools/tiger-artifacts.py" verify \
    --ui "$(candidate_path "${UIDIR:-build/tiger-ui-port}")" \
    --web "$(candidate_path "${WEBDIR:-build/tiger-web-port}")" \
    --gpu "$(candidate_path "${GPUDIR:-build/tiger-gpu}")" > "$OUT/candidate.json" || exit 1

# spike/media is served from this Mac; the box fetches the video over the LAN.
if ! curl -sf -m 3 -o /dev/null "http://$MEDIA_HOST/video480loop.html"; then
    echo "regress: starting python3 -m http.server on $MEDIA_HOST"
    python3 -m http.server "${MEDIA_HOST##*:}" -d "$WKT/spike/media" > "$OUT/media-server.log" 2>&1 &
    MEDIA_PID=$!; SERVER_PIDS="$SERVER_PIDS $MEDIA_PID"
    sleep 2
    kill -0 "$MEDIA_PID" 2>/dev/null || { cat "$OUT/media-server.log" >&2; exit 1; }
fi

note() { printf '%s\n' "$*" >> "$SUMMARY"; }

# stage one page. run <name> <url> <seconds> <script> [extra env]
run() {
    name=$1; url=$2; secs=$3; script=$4; extra_env=${5:-}
    echo "== $name ($secs s)"
    # The staging helper holds the remote lease while waiting for an idle box.
    APP=TigerBrowser2 \
    SHOT="$OUT/$name.png" \
    LOG="$OUT/$name.log" \
    APP_ENV="$extra_env TIGER_PAINT_PROBE=1 TIGER_SCRIPT='$script'" \
        sh "$STAGE" "$url" "$secs" > "$OUT/$name.stage.log" 2>&1
    stage_rc=$?
    if [ $stage_rc -ne 0 ]; then
        echo "   stage-app.sh exited $stage_rc (see $name.stage.log)"
        STAGE_FAILURES="$STAGE_FAILURES $name"
    fi
    return $stage_rc
}

# Every run's log check. Returns the failure reasons on stdout, empty when clean.
log_faults() {
    log=$1
    [ -f "$log" ] || { echo "no app.log came back"; return; }
    for pattern in TIGER-CRASH TIGER-ABORT unresponsive 'cannot connect to :' 'Autoplay blocked'; do
        n=$(grep -ac "$pattern" "$log" 2>/dev/null || true)
        [ "${n:-0}" -gt 0 ] && echo "$n x \"$pattern\""
    done
    grep -aq 'TIGER ui: incorporate' "$log" || echo 'no "TIGER ui: incorporate" (nothing was painted)'
}

# record <name> <pass|fail> <detail...>
record() {
    name=$1; verdict=$2; shift 2
    if [ "$verdict" = pass ]; then PASSES=$((PASSES + 1)); mark=PASS; else FAILS=$((FAILS + 1)); mark=FAIL; fi
    printf '| %-9s | %-4s | %s |\n' "$name" "$mark" "$*" >> "$SUMMARY"
    printf '   %s: %s\n' "$mark" "$*"
}

# The whole check for one page: log faults plus whatever extra checks the caller ran.
# check <name> <extra-detail-or-empty>
check() {
    name=$1; extra=$2
    faults=$(log_faults "$OUT/$name.log" | tr '\n' ';' | sed 's/;$//')
    case " $STAGE_FAILURES " in *" $name "*) faults="stage failed${faults:+; $faults}";; esac
    if [ "$name" = cookies ]; then
        case " $STAGE_FAILURES " in *" cookies2 "*) faults="cookie relaunch failed${faults:+; $faults}";; esac
        second_faults=$(log_faults "$OUT/cookies2.log" | tr '\n' ';' | sed 's/;$//')
        [ -z "$second_faults" ] || faults="${faults:+$faults; }cookie relaunch: $second_faults"
    fi
    if [ -n "$faults" ]; then
        record "$name" fail "log: $faults${extra:+ -- $extra}"
    elif case "$extra" in *FAILED*) true;; *) false;; esac; then
        record "$name" fail "$(echo "$extra" | sed 's/FAILED //g')"
    else
        record "$name" pass "${extra:-log clean}"
    fi
}

# golden <golden-name> <shot-name> [compare options...] -> the detail string for check().
# The two names differ when one shot is compared against more than one golden crop.
golden() {
    name=$1; shot=$2; shift 2
    if [ -n "$BLESS" ]; then
        cp "$OUT/$shot.png" "$GOLDEN/$name.png"
        echo "BLESSED $GOLDEN/$name.png"
        return
    fi
    if [ ! -f "$GOLDEN/$name.png" ]; then
        echo "FAILED no golden: $GOLDEN/$name.png (run with BLESS=1 once the shot is right)"
        return
    fi
    out=$(python3 "$COMPARE" "$OUT/$shot.png" "$GOLDEN/$name.png" "$@" 2>&1)
    [ $? -eq 0 ] && echo "$out" || echo "FAILED $out"
}

probe() { # probe <compare args...> -> detail string
    out=$(python3 "$COMPARE" "$@" 2>&1)
    [ $? -eq 0 ] && echo "$out" || echo "FAILED $out"
}

wants() { case " $WANTED " in *" $1 "*) return 0;; *) return 1;; esac; }

title_seen() {
    grep -aF 'TIGER title:' "$OUT/$1.log" | grep -Fq "$2" \
        && printf '%s\n' "$2" || printf 'FAILED missing title: %s\n' "$2"
}

note "# regress $(date '+%Y-%m-%d %H:%M:%S')"
note ""
note "UI \`${UIDIR:-build/tiger-ui-port}\`, web/network \`${WEBDIR:-build/tiger-web-port}\`, GPU \`${GPUDIR:-build/tiger-gpu}\`, audio \`build/tigeraudio32\`. (UIDIR/WEBDIR/GPUDIR pick a track's own dirs.)"
note ""
note '| check | verdict | detail |'
note '| --- | --- | --- |'

# 1. example.com: the simplest end-to-end path through all four processes.
if wants example; then
    run example http://example.com/ 14 'wait 8'
    detail=$(probe "$OUT/example.png" --nonblank)
    case $detail in FAILED*) ;; *) detail="$detail; $(golden example example --max-frac 0.02)";; esac
    check example "$detail"
fi

# 2. scrolltest.html: two page scrolls, checked for vertically mirrored blits.
if wants scroll; then
    run scroll "file://$SHARE/scrolltest.html" 18 'wait 6;scroll 0,-300;wait 3;scroll 0,-300;wait 3'
    detail=$(python3 "$WKT/spike/wk2web/check-scrollshot.py" "$OUT/scroll.png" 2>&1)
    case $detail in *": OK"*) ;; *) detail="FAILED $detail";; esac
    check scroll "$(echo "$detail" | sed "s|$OUT/||")"
fi

# 3. controls-test.html: the hosted Aqua controls, golden over the control column.
if wants controls; then
    run controls "file://$SHARE/controls-test.html" 14 'wait 8'
    check controls "$(golden controls controls --crop 80,152,640,700 --max-frac 0.02)"
fi

# 4. boxtest.html: a repaint whose bounds do not start at the origin. The box changes
#    colour and caption every 1.5 s, so its interior cannot be a golden -- but the static
#    12 px black frame around it can, and a misplaced partial update spills colour over
#    that frame. Two crops, both outside the box: the top band (full width, the grey
#    margin plus the top border) and the left band (full height). A shift in any
#    direction that matters puts colour where black or grey belongs.
if wants boxtest; then
    run boxtest "file://$SHARE/boxtest.html" 12 'wait 6'
    detail="$(golden boxtest-top boxtest --crop 344,318,800,340 --max-frac 0.01)"
    detail="$detail; $(golden boxtest-left boxtest --crop 340,318,372,660 --max-frac 0.01)"
    check boxtest "$detail"
fi

# 5. A textarea: click, type, select all. The golden is the selection highlight.
if wants textarea; then
    run textarea \
      'data:text/html,<body%20style="margin:20px;font:16px%20Helvetica"><textarea%20id=t%20rows=6%20cols=44></textarea><script>t.focus()</script>' \
      16 'wait 5;click 120,60;wait 1;type the quick brown fox;wait 2;keymod cmd a;wait 2'
    check textarea "$(golden textarea textarea --crop 90,165,470,285 --max-frac 0.02)"
fi

# 6. x.com onboarding. A live site: no golden is possible, so the check is that nothing
#    crashed and that the phone field has dark text in it after the typing.
if wants xcom; then
    run xcom https://x.com/ 50 'wait 22;click 300,236;wait 12;click 560,217;wait 2;type 2125551234;wait 8;shot /Users/shg/wk2/shot.png'
    check xcom "$(probe "$OUT/xcom.png" --dark-text 420,340,800,400)"
fi

# 7. video480loop over the LAN: the numbers the media path prints, not a picture.
if wants video; then
    run video "http://$MEDIA_HOST/video480loop.html" 25 'wait 3'
    detail=$(grep -a TIGER-MEDIA "$OUT/video.log" | python3 -c '
import re, sys
windows = []
for line in sys.stdin:
    if "TIGER-MEDIA" not in line:
        continue
    fps = float(re.search(r"fps=([\d.]+)", line).group(1))
    dropped = int(re.search(r"dropped=(\d+)", line).group(1))
    pts = float(re.search(r"pts=([\d.]+)", line).group(1))
    # The page loops the clip: the window that straddles the wrap is short by the
    # gap, and its fps is meaningless. Drop it, not the real windows around it.
    if windows and pts < windows[-1][2]:
        continue
    windows.append((fps, dropped, pts))
windows = windows[-2:]
if len(windows) < 2:
    print("FAILED only %d usable TIGER-MEDIA windows" % len(windows)); raise SystemExit
bad = ["fps=%.1f dropped=%d" % (f, d) for f, d, _ in windows if f < 29 or d]
print(("FAILED " + "; ".join(bad)) if bad else
      "last two windows: " + " / ".join("fps=%.1f dropped=%d" % (f, d) for f, d, _ in windows))
')

    check video "$detail"
fi

# 8. youtube.com: the heaviest page that has to come up at all.
if wants youtube; then
    run youtube https://www.youtube.com/ 40 'wait 30'
    check youtube "$(probe "$OUT/youtube.png" --nonblank)"
fi

# 9-11. Faithful mode (TIGER_FAITHFUL=1): accelerated compositing on, layers composited
#    by CARenderer in the GPU process and read back. A page without compositing layers
#    still paints in the web process, so example.com and the controls page must match
#    the fast-mode goldens pixel for pixel -- the same golden files, on purpose.
#    scrolltest.html?composited puts a will-change layer on the fixed bar and animates a
#    second one, which forces the CA path; the check is the mirrored-blit checker plus at
#    least one "TIGER gpu: frame" line, so a run that never reached the GPU scene fails --
#    when the staged GPU process has that probe at all (see below).
if wants fexample; then
    run fexample http://example.com/ 14 'wait 8' TIGER_FAITHFUL=1
    detail=$(probe "$OUT/fexample.png" --nonblank)
    # BLESS is cleared for the shared goldens: a faithful run must never re-bless the
    # fast-mode picture it is being held to.
    case $detail in FAILED*) ;; *) detail="$detail; $(BLESS=; golden example fexample --max-frac 0.02)";; esac
    check fexample "$detail"
fi

if wants fscroll; then
    run fscroll "file://$SHARE/scrolltest.html?composited" 20 'wait 8;scroll 0,-300;wait 3;scroll 0,-300;wait 3' TIGER_FAITHFUL=1
    detail=$(python3 "$WKT/spike/wk2web/check-scrollshot.py" "$OUT/fscroll.png" 2>&1)
    case $detail in *": OK"*) ;; *) detail="FAILED $detail";; esac
    # The frame count needs a GPU process built with the "TIGER gpu:" probe. A binary
    # without it prints no "TIGER gpu:" line of any kind; then the check is skipped
    # and says so, rather than failing a build that simply predates the probe.
    if grep -aq 'TIGER gpu:' "$OUT/fscroll.log" 2>/dev/null; then
        frames=$(grep -ac 'TIGER gpu: frame' "$OUT/fscroll.log" 2>/dev/null || true)
        [ "${frames:-0}" -gt 0 ] || detail="FAILED no TIGER gpu: frame (the CA scene never rendered); $detail"
        gpu="gpu frames=${frames:-0}"
    else
        gpu="gpu frame count skipped: probe absent in this GPU binary"
    fi
    check fscroll "$(echo "$detail" | sed "s|$OUT/||"); $gpu"
fi

if wants fcontrols; then
    run fcontrols "file://$SHARE/controls-test.html" 14 'wait 8' TIGER_FAITHFUL=1
    check fcontrols "$(BLESS=; golden controls fcontrols --crop 80,152,640,700 --max-frac 0.02)"
fi

# 13. Hosted controls must not outlive their document: load the controls page, then
#    navigate to example.com in the same window. Every native control is a live NSView over
#    the page view, so one left behind shows up as Aqua artwork on top of example.com; the
#    shot must match the plain example golden. Fast and faithful.
if wants ghost; then
    run ghost "file://$SHARE/controls-test.html" 22 'wait 8; load http://example.com/; wait 8'
    check ghost "$(BLESS=; golden example ghost --max-frac 0.02)"
fi
if wants fghost; then
    run fghost "file://$SHARE/controls-test.html" 22 'wait 8; load http://example.com/; wait 8' TIGER_FAITHFUL=1
    check fghost "$(BLESS=; golden example fghost --max-frac 0.02)"
fi

# 14. Recovery: SIGKILL the children mid-run (TigerBrowser2's killproc verb) and the page
#    must come back by itself. Fast: the web process during idle (reloaded, painted by the
#    new process), then the network process, then a load that needs it. Faithful: the GPU
#    process (a frame from the rebuilt scene must follow), then the web process. The last
#    shot is the example golden either way; after_kill checks that something happened after.
after_kill() { # after_kill <log> <which> <pattern> -> "" or FAILED
    awk -v k="killproc $2" -v p="$3" 'index($0, k) { seen = 1 } seen && index($0, p) { found = 1 } END { exit !found }' "$1" \
        && echo "$2: ok" || echo "FAILED nothing matching \"$3\" after killproc $2"
}
if wants relaunch; then
    run relaunch http://example.com/ 30 'wait 6; killproc web; wait 7; killproc net; wait 2; load http://example.com/; wait 8'
    detail="$(after_kill "$OUT/relaunch.log" web 'TIGER ui: incorporate'); $(after_kill "$OUT/relaunch.log" net 'TIGER ui: incorporate')"
    grep -aq 'TIGER-RECOVER: web process exited' "$OUT/relaunch.log" || detail="$detail; FAILED no status line for the web process"
    check relaunch "$detail; $(BLESS=; golden example relaunch --max-frac 0.02)"
fi
if wants frelaunch; then
    run frelaunch http://example.com/ 30 'wait 7; killproc gpu; wait 1; scroll 0,-300; wait 6; killproc web; wait 8; scroll 0,300; wait 3' TIGER_FAITHFUL=1
    detail="$(after_kill "$OUT/frelaunch.log" gpu 'TIGER gpu: frame'); $(after_kill "$OUT/frelaunch.log" web 'TIGER ui: incorporate')"
    grep -aq 'TIGER-RECOVER: GPU process gone' "$OUT/frelaunch.log" || detail="$detail; FAILED the web process never rebuilt its scene"
    check frelaunch "$detail; $(BLESS=; golden example frelaunch --max-frac 0.02)"
fi

# 15. Feature probes: spike/wk2web/features.html, served over https from this Mac by
#    tools/features-server.py (a secure context with COOP/COEP, plus /ws and /stream). The
#    page prints "FEATURE <name> PASS|FAIL|PENDING" per probe. FT_PASS must all PASS: the
#    unsupported features answering at once (absent/null/denied), the working set actually
#    working. FT_MISSING are known gaps; one of those passing is reported, not failed. Any
#    PENDING is a failure: a promise a site would wait on forever.
if wants features; then
    FT_PASS="webgl webgpu webrtc getusermedia eme notifications geolocation gamepad devices payment share speech battery vibrate wakelock webxr webauthn permissions
             localstorage indexeddb cacheapi serviceworker websocket fetchstream wasm offscreencanvas worker sab mediasource"
    FT_MISSING="webcodecs"
    python3 "$WKT/tools/features-server.py" 8444 > "$OUT/features-server.out" 2>&1 &
    FT_PID=$!; SERVER_PIDS="$SERVER_PIDS $FT_PID"
    sleep 2
    kill -0 "$FT_PID" 2>/dev/null || { cat "$OUT/features-server.out" >&2; exit 1; }
    export STAGE_CA_BUNDLE=/tmp/tiger-cookie-cert/bundle.pem
    run features "https://192.168.1.253:8444/features.html" 26 'wait 20' "TIGER_CONSOLE=1 TIGER_CA_BUNDLE=$SHARE/cookie-bundle.pem"
    unset STAGE_CA_BUNDLE
    grep -a 'FEATURE ' "$OUT/features.log" | sed 's/^.*FEATURE /FEATURE /; s/ (https:.*$//' > "$OUT/features.txt"
    bad=""; for f in $FT_PASS; do grep -q "^FEATURE $f PASS" "$OUT/features.txt" || bad="$bad $f"; done
    news=""; for f in $FT_MISSING; do grep -q "^FEATURE $f PASS" "$OUT/features.txt" && news="$news $f"; done
    pend=$(grep ' PENDING ' "$OUT/features.txt" | cut -d' ' -f2 | tr '\n' ' ')
    detail="$(grep -c ' PASS ' "$OUT/features.txt") pass (features.txt)${news:+; now passing, move to FT_PASS:$news}"
    [ -n "$bad$pend" ] && detail="FAILED not passing:$bad${pend:+; pending: $pend}"
    check features "$detail"
fi

# 16. Aqua scrollbars: spike/wk2web/scrollbars.html. The main frame's scrollbar is a hosted
#    NSScroller; the overflow div's, the thin div's and the iframe's are HIThemeDrawTrack
#    artwork painted by the web process. The page reports its scroll positions in its title,
#    which TigerBrowser2 logs as "TIGER title:". A click on the NSScroller's down arrow is
#    one line (40 px), a click on the overflow div's drawn down arrow is one line of the div,
#    a knob drag scrolls the page and a drag back returns it to the top; then the shot (the
#    drawn scrollbars in view, the div scrolled by one line) is a golden. Fast and faithful.
SB_SCRIPT='wait 8; click 265,230; wait 1; click 952,640; wait 1; drag 952,60 952,360; wait 2; drag 952,370 952,0; wait 2'
scrollbar_checks() { # scrollbar_checks <name> -> detail string
    log=$OUT/$1.log
    d=""
    grep -aq 'TIGER title: y=0 x=0 over=40,0' "$log" || d="$d; FAILED the drawn arrow did not scroll the div one line"
    grep -aq 'TIGER title: y=40 x=0 over=40,0' "$log" || d="$d; FAILED the NSScroller's arrow did not scroll the page one line"
    most=$(grep -a 'TIGER title: y=' "$log" | sed 's/^.*TIGER title: y=\([0-9]*\).*$/\1/' | sort -n | tail -1)
    [ "${most:-0}" -gt 400 ] 2>/dev/null || d="$d; FAILED the knob drag did not scroll the page (y=${most:-none})"
    grep -a 'TIGER title: y=' "$log" | tail -1 | grep -q 'y=0 ' || d="$d; FAILED the drag back up did not return to the top"
    echo "titles ok, y=${most:-none} after the drag, 0 after dragging back$d"
}
if wants scrollbars; then
    run scrollbars "file://$SHARE/scrollbars.html" 22 "$SB_SCRIPT"
    check scrollbars "$(scrollbar_checks scrollbars); $(golden scrollbars scrollbars --max-frac 0.02)"
fi
if wants fscrollbars; then
    run fscrollbars "file://$SHARE/scrollbars.html" 22 "$SB_SCRIPT" TIGER_FAITHFUL=1
    check fscrollbars "$(scrollbar_checks fscrollbars); $(BLESS=; golden scrollbars fscrollbars --max-frac 0.02)"
fi

# Fully clipped native controls must neither cover nor intercept the page beneath
# them; the visible offset iframe's field must still accept native text input.
CLIP_SCRIPT='wait 6; click 60,232; wait 1; click 380,92; wait 1; click 380,382; wait 1; click 380,242; wait 1; type frame; wait 2'
if wants hostedclip; then
    run hostedclip "file://$SHARE/hosted-clipping.html" 22 "$CLIP_SCRIPT" TIGER_CONSOLE=1
    check hostedclip "$(title_seen hostedclip 'hits=1,1,1 values=//frame/')"
fi
if wants fhostedclip; then
    run fhostedclip "file://$SHARE/hosted-clipping.html" 22 "$CLIP_SCRIPT" 'TIGER_CONSOLE=1 TIGER_FAITHFUL=1'
    check fhostedclip "$(title_seen fhostedclip 'hits=1,1,1 values=//frame/')"
fi

# Ring lifecycle workload: API progress plus crash/paint checks. A JS pass does
# not prove frame integrity; inspect returned screenshots and presentation probes.
if wants videolifetime; then
    run videolifetime "http://$MEDIA_HOST/video-lifetime.html" 26 'wait 22' TIGER_CONSOLE=1
    check videolifetime "$(title_seen videolifetime 'VIDEO LIFETIME PASS (API only)')"
fi
if wants fvideolifetime; then
    run fvideolifetime "http://$MEDIA_HOST/video-lifetime.html" 26 'wait 22' 'TIGER_CONSOLE=1 TIGER_FAITHFUL=1'
    check fvideolifetime "$(title_seen fvideolifetime 'VIDEO LIFETIME PASS (API only)')"
fi

# 12. Cookies: tools/cookie-server.py on this Mac is the oracle (it logs every Cookie
#    header it gets). Two launches with the same storage: the first walks sets, fetches,
#    a third-party iframe, a redirect chain, http, a cross-site visit, a cross-site POST
#    and back (SameSite, Secure and third-party blocking); the second clicks on the other
#    site, then checks that persistent cookies survived the quit, session ones did not,
#    and that the clicked site's cookies now reach it inside a frame.
#    HOME is a scratch dir so the jar is this check's own, never the user's profile.
if wants cookies; then
    CK_LOG=$OUT/cookies.jsonl
    # TIGER_CURL_RESOLVE: 10.4 resolves the .local name unreliably; pin it to this Mac.
    CK_ENV="HOME=/Users/shg/wk2/cookiehome-$(date +%Y%m%d-%H%M%S)-$$ TIGER_CA_BUNDLE=$SHARE/cookie-bundle.pem TIGER_COOKIE_LOG=1 TIGER_CURL_RESOLVE=shg-mbp.local:8443:192.168.1.253,shg-mbp.local:8480:192.168.1.253"
    python3 "$WKT/tools/cookie-server.py" serve "$CK_LOG" > "$OUT/cookie-server.out" 2>&1 &
    CK_PID=$!; SERVER_PIDS="$SERVER_PIDS $CK_PID"
    sleep 2
    kill -0 "$CK_PID" 2>/dev/null || { cat "$OUT/cookie-server.out" >&2; exit 1; }
    export STAGE_CA_BUNDLE=/tmp/tiger-cookie-cert/bundle.pem
    run cookies "https://shg-mbp.local:8443/start" 40 'wait 1' "$CK_ENV"
    # The click on B is the user interaction that lifts third-party blocking for B.
    run cookies2 "https://192.168.1.253:8443/interact" 24 'wait 7;click 300,300' "$CK_ENV"
    unset STAGE_CA_BUNDLE
    python3 "$WKT/tools/cookie-server.py" report "$CK_LOG" > "$OUT/cookies.md"
    detail=$(grep -c '| PASS |' "$OUT/cookies.md")" pass"
    failed=$(grep '| FAIL |' "$OUT/cookies.md" | cut -d'|' -f2 | sed 's/^ //;s/ $//' | tr '\n' ',' | sed 's/,$//')
    [ -n "$failed" ] && detail="FAILED $failed (cookies.md)"
    check cookies "$detail"
fi

note ""
note "**$PASSES passed, $FAILS failed.**"
echo
echo "$PASSES passed, $FAILS failed -- $SUMMARY"
cat "$SUMMARY"
[ "$FAILS" -eq 0 ]
