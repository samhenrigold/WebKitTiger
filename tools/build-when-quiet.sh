#!/bin/sh
# ninja -j2 in <build dir> for <targets>, started only when this Mac's 1-minute load is
# under 12 (shared machine: several agents build here).   tools/build-when-quiet.sh dir targets...
dir=$1; shift
while :; do
    load=$(sysctl -n vm.loadavg | awk '{print int($2)}')
    [ "$load" -lt 12 ] && break
    sleep 60
done
echo "build-when-quiet: $(date '+%H:%M') load $load, ninja -j2 $*"
exec ninja -C "$dir" -j2 "$@"
