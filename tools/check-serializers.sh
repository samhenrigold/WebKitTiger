#!/bin/sh
# Preprocess GeneratedSerializersShared.cpp with each tree's real flags and diff the decoded
# member lists. tiger-check-ipc compares the generated TEXT; a field whose #if evaluates
# differently on the two sides is invisible to that and shifts every byte after it.
#   tools/check-serializers.sh build/tiger-ui-port build/tiger-web-port
set -e
WKT=$(cd "$(dirname "$0")/.." && pwd)
A=$1; B=$2
for T in "$A" "$B"; do
    N=$(basename "$T")
    OBJ=$(ninja -C "$WKT/$T" -t targets all 2>/dev/null | grep -o 'Source/WebKit/CMakeFiles/WebKitShared.dir/[^:]*GeneratedSerializersShared.cpp.o' | head -1)
    CMD=$(cd "$WKT/$T" && ninja -t commands "$OBJ" | tail -1 | sed 's/^: && //; s/ && :$//')
    SRC=$(echo "$CMD" | grep -oE ' -c [^ ]+$' | awk '{print $2}')
    PRE=$(echo "$CMD" | sed -E 's/ -o [^ ]+ -c [^ ]+$//; s/ -MD -MT [^ ]+ -MF [^ ]+//; s/-Xclang -include-pch -Xclang [^ ]+//; s/-Xclang -include -Xclang [^ ]+//')
    (cd "$WKT/$T" && eval "$PRE -E $SRC") 2>/dev/null \
        | grep -E 'ArgumentCoder<.*>::decode\(|auto [A-Za-z0-9_]+ = decoder\.decode<' \
        | sed -E 's/^.*std::optional<(.*)> ArgumentCoder<.*>::decode\(.*$/== \1/; s/^\s*auto ([A-Za-z0-9_]+) = decoder\.decode<(.*)>\(\);.*$/  \1 : \2/' > "/tmp/ser-$N.txt"
    echo "$T: $(wc -l < "/tmp/ser-$N.txt") decoded members"
done
if diff "/tmp/ser-$(basename "$A").txt" "/tmp/ser-$(basename "$B").txt" > /tmp/ser-diff.txt; then
    echo "SERIALIZERS: the two trees decode the same members"
else
    echo "SERIALIZERS DIFFER (< $A, > $B):"; grep '^[<>]' /tmp/ser-diff.txt | cut -c1-150; exit 1
fi
