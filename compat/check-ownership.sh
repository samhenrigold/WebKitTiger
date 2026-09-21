#!/bin/sh
# check-ownership.sh -- exactly one archive per arch must define each shared symbol.
#
# libtigercompat.a and libtigerdispatch.a have twice ended up both carrying os.c,
# and once with neither carrying it. Neither state announces itself: linking two
# static archives that define the same symbol succeeds with rc=0 and no duplicate
# symbol error, because the second archive's member is simply never pulled. The
# copies then drift, with link order deciding which one runs. And a test binary
# passes when both archives own a symbol, passes when one does, and fails the same
# way as a missing library when neither does, so running a program distinguishes
# none of the three states.
#
# nm on both archives does distinguish them, in one line per symbol. Both install
# targets call this at the end, so a wrong state fails the install.
#
# Usage: compat/check-ownership.sh [arch ...]      (default: i386 x86_64)

set -e
WKT=$(cd "$(dirname "$0")/.." && pwd)
NM="$WKT/toolchain/bin/tiger-nm"
ARCHES=${*:-"i386 x86_64"}
status=0

# Symbols shared between the two archives, and who must own them.
# i386 additionally has dispatch_async; there is no 64-bit dispatch, because
# Tiger ships no x86_64 CoreFoundation and the main queue needs CFRunLoop.
symbols_for() {
    echo "os_log_create os_unfair_lock_lock os_release"
    [ "$1" = "i386" ] && echo "dispatch_async"
}

defines() {   # defines <archive> <arch> <symbol> -> prints 1 or 0
    if [ ! -f "$1" ]; then echo 0; return; fi
    n=$("$NM" -arch "$2" -g "$1" 2>/dev/null | grep -cE "^[0-9a-f]+ T _$3\$" || true)
    [ "$n" -gt 0 ] && echo 1 || echo 0
}

for arch in $ARCHES; do
    lib="$WKT/toolchain/sysroot-$arch/usr/lib"
    compat="$lib/libtigercompat.a"
    dispatch="$lib/libtigerdispatch.a"

    # An arch nobody has built yet is not a failure, it is just absent.
    if [ ! -f "$compat" ] && [ ! -f "$dispatch" ]; then
        echo "  $arch: neither archive built, skipping"
        continue
    fi

    for sym in $(symbols_for "$arch"); do
        c=$(defines "$compat" "$arch" "$sym")
        d=$(defines "$dispatch" "$arch" "$sym")
        total=$((c + d))

        if [ "$total" -eq 1 ]; then
            [ "$c" -eq 1 ] && owner="libtigercompat.a" || owner="libtigerdispatch.a"
            printf "  %-7s %-22s -> %s\n" "$arch" "$sym" "$owner"
        elif [ "$total" -gt 1 ]; then
            printf "  %-7s %-22s -> BOTH archives define it\n" "$arch" "$sym"
            echo "      Two copies that can drift, with link order picking the winner."
            echo "      Drop it from one archive; a home change is ONE commit touching both makefiles."
            status=1
        elif [ ! -f "$compat" ] || [ ! -f "$dispatch" ]; then
            # Only half the arch is installed, so "nobody defines it" is expected.
            missing=$([ -f "$compat" ] && echo libtigerdispatch.a || echo libtigercompat.a)
            printf "  %-7s %-22s -> no owner yet (%s not built)\n" "$arch" "$sym" "$missing"
        else
            printf "  %-7s %-22s -> NO archive defines it\n" "$arch" "$sym"
            echo "      Both archives exist and neither owns it: 64-bit os_* was in this"
            echo "      state once, and every test binary failed identically to a missing -l flag."
            status=1
        fi
    done
done

[ "$status" -eq 0 ] && echo "  archive ownership OK" || echo "  ARCHIVE OWNERSHIP BROKEN"
exit $status
