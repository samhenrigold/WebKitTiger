#!/bin/sh
# Symbolize TIGER-CRASH frame addresses printed by TigerCrashCatcher.
#   tools/symbolize-tiger.sh build/tiger-web-port/bin/TigerWebProcess 0x1041ace92 ...
# Tiger has no ASLR: x86_64 executables load at 0x100000000, i386 at 0x1000.
bin=$1; shift
case "$(file -b "$bin")" in *x86_64*) base=0x100000000;; *) base=0x1000;; esac
atos -o "$bin" -l $base "$@"
