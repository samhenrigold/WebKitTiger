#!/bin/bash
# build an x86_64 spike program for Tiger, copy it over, run it. Sibling of run.sh (i386).
# The C++ flags here are the canonical 64-bit link line; see NOTES.md.
set -e
WKT=/Users/shg/Developer/WebKitTiger
SRC=$1; shift
OUT=$WKT/spike/$(basename ${SRC%.*})64
SYS=$WKT/toolchain/sysroot-x86_64/usr
# -ltigercompat supplies _dyld_find_unwind_sections and posix_memalign, which libunwind and
# libc++abi need and Tiger's libSystem does not have. Without it the link fails outright, so a
# stale copy, not a missing one, is what silently breaks unwinding.
CXX_RT="-nostdinc++ -isystem $SYS/include/c++/v1 -stdlib=libc++ -lc++ -lc++abi -lunwind -ltigercompat"
case "$SRC" in
  *.cpp) $WKT/toolchain/bin/tiger-clang64++ -g -O2 -std=c++2b -fexceptions $CXX_RT "$@" $SRC -o $OUT ;;
  *.c)   $WKT/toolchain/bin/tiger-clang64 -g -O2 -ltigercompat "$@" $SRC -o $OUT ;;
esac
scp -qO $OUT tiger:/tmp/$(basename $OUT)
ssh tiger "/tmp/$(basename $OUT)"
