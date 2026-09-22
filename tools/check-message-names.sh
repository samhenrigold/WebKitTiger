#!/bin/sh
# The IPC MessageName enum is generated with #if conditions inside it, so its numbering
# is per-compile: a message conditioned on a flag that differs between two trees shifts
# every later name, and the peer decodes the wrong message. tiger-check-ipc compares the
# generated text; this compares the PREPROCESSED enumerator list, with each tree's flags.
#   tools/check-message-names.sh build/tiger-ui-port build/tiger-web-port
set -e
WKT=$(cd "$(dirname "$0")/.." && pwd)
A=$1; B=$2
for T in "$A" "$B"; do
    OBJ=$(ninja -C "$WKT/$T" -t targets all 2>/dev/null | grep -o 'Source/WebKit/CMakeFiles/WebKitShared.dir/[^:]*GeneratedSerializersCommon.cpp.o' | head -1)
    CMD=$(cd "$WKT/$T" && ninja -t commands "$OBJ" | tail -1 | sed 's/^: && //; s/ && :$//')
    PRE=$(echo "$CMD" | sed -E 's/ -o [^ ]+ -c [^ ]+$//; s/ -MD -MT [^ ]+ -MF [^ ]+//; s/-Xclang -include-pch -Xclang [^ ]+//; s/-Xclang -include -Xclang [^ ]+//')
    printf '#include "config.h"\n#include "MessageNames.h"\n' > "/tmp/mn-$(basename "$T").cpp"
    (cd "$WKT/$T" && eval "$PRE -E /tmp/mn-$(basename "$T").cpp") 2>/dev/null \
        | awk '/enum class MessageName : uint16_t/{f=1} f{print} /^};/{if(f){exit}}' \
        | grep -E '^\s*[A-Za-z_][A-Za-z0-9_]*,?\s*$' | sed 's/[ ,]//g' > "/tmp/mn-$(basename "$T").txt"
    echo "$T: $(wc -l < "/tmp/mn-$(basename "$T").txt") message names"
done
if diff "/tmp/mn-$(basename "$A").txt" "/tmp/mn-$(basename "$B").txt" > /tmp/mn-diff.txt; then
    echo "MESSAGE NAMES: the two trees agree"
else
    echo "MESSAGE NAMES DIFFER (every name after the first difference is renumbered):"
    grep '^[<>]' /tmp/mn-diff.txt | sed -e "s|^<|  only in $A:|" -e "s|^>|  only in $B:|"
    exit 1
fi
