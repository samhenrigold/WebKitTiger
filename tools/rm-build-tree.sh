#!/bin/bash
# Remove a build tree under $WKT/build, safely. Usage: rm_build_tree <name> [--dry-run]
# The guard is on the NAME, not on the assembled path: a single path component with no
# slash, no dot-dot and no expansion can never escape build/, which kills the whole class
# rather than the one case that bit us. The realpath check is belt-and-braces for symlinks.
WKT=${WKT:-/Users/shg/Developer/WebKitTiger}

rm_build_tree() {
    local name=$1 dry=$2 target real root
    # Canonicalise the root too: on macOS /var, /tmp and mktemp -d are symlinks, so comparing a
    # `pwd -P` result against an uncanonicalised root rejects every legitimate path.
    root=$(cd "$WKT" 2>/dev/null && pwd -P) || { echo "refusing: cannot resolve WKT '$WKT'" >&2; return 1; }
    # 1. non-empty, and a bare component: no /, no .., not . or ..
    case $name in
        "" | */* | *..* | . ) echo "refusing: bad build tree name '$name'" >&2; return 1 ;;
    esac
    target=$root/build/$name
    [ -e "$target" ] || { echo "skip: $target does not exist"; return 0; }
    # 2. after symlink resolution it must still sit strictly under $WKT/build
    real=$(cd "$target" 2>/dev/null && pwd -P) || { echo "refusing: cannot resolve $target" >&2; return 1; }
    case $real in
        "$root/build/"?*) : ;;
        *) echo "refusing: $target resolves outside build/ (-> $real)" >&2; return 1 ;;
    esac
    # 3. explicit blacklist, so the intent is greppable even though 1 and 2 already cover it
    case $real in
        / | "$HOME" | "$root" | "$root/build") echo "refusing: $real" >&2; return 1 ;;
    esac
    if [ "$dry" = "--dry-run" ]; then echo "would remove: $real"; else echo "removing: $real"; rm -rf "$real"; fi
}

# ---- self-check: `bash rmbuild.sh --self-test` ----
if [ "$1" = "--self-test" ]; then
    WKT=$(mktemp -d); mkdir -p "$WKT/build/good" "$WKT/keepme"; ln -s "$WKT/keepme" "$WKT/build/escape"
    fail=0
    t() { # t <expect-rc> <desc> <args...>
        local want=$1 desc=$2; shift 2
        rm_build_tree "$@" >/dev/null 2>&1; local got=$?
        [ "$got" = "$want" ] && echo "PASS  $desc" || { echo "FAIL  $desc (rc=$got want=$want)"; fail=1; }
    }
    t 1 "empty name"                    ""
    t 1 "name with a slash"             "../.."
    t 1 "dot-dot inside the name"       "a..b"
    t 1 "bare dot"                      "."
    t 1 "symlink escaping build/"       "escape"
    t 0 "absent tree is a no-op"        "nosuch"
    t 0 "real tree removes"             "good"
    [ -d "$WKT/build/good" ] && { echo "FAIL  tree still present"; fail=1; } || echo "PASS  tree actually gone"
    [ -d "$WKT/keepme" ] && echo "PASS  symlink target untouched" || { echo "FAIL  symlink target destroyed"; fail=1; }
    rm_build_tree good --dry-run >/dev/null 2>&1
    rm -rf "$WKT"
    echo; [ $fail = 0 ] && echo "ALL PASS" || echo "FAILURES"; exit $fail
fi
