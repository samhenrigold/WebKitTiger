#!/bin/bash
# Build the std::filesystem spike for Tiger and run it there.
set -e
WKT=/Users/shg/Developer/WebKitTiger
CXX=$WKT/toolchain/bin/tiger-clang++
INC="-nostdinc++ -isystem $WKT/toolchain/sysroot-i386/usr/include/c++/v1"
CFLAGS="-g -O1 -std=c++23 $INC -fexceptions -fvisibility=hidden"
RT="-stdlib=libc++ -lc++ -lc++abi -lunwind -ltigercompat $WKT/build/builtins-i386/libclang_rt.builtins-i386.a"
cd $WKT/spike
$CXX $CFLAGS fstest.cpp -o fstest $RT
scp -qO fstest tiger:/tmp/
ssh tiger '/tmp/fstest; echo EXIT=$?'
