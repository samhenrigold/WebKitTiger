#!/bin/bash
# Build the C++ spike (dylib + exe) for Tiger and run it there.
set -e
WKT=/Users/shg/Developer/WebKitTiger
CXX=$WKT/toolchain/bin/tiger-clang++
INC="-nostdinc++ -isystem $WKT/toolchain/sysroot-i386/usr/include/c++/v1"
# -target ...10.7 only for compiling: clang refuses thread_local below 10.7 even with -femulated-tls.
# Linking stays at the wrapper's 10.4 so ld emits classic load commands.
CFLAGS="-g -O1 -std=c++2b $INC -target i386-apple-macosx10.7 -femulated-tls -fexceptions -fvisibility=hidden"
RT="-stdlib=libc++ -lc++ -lc++abi -lunwind -ltigercompat $WKT/build/builtins-i386/libclang_rt.builtins-i386.a"
cd $WKT/spike
$CXX $CFLAGS -c throwlib.cpp -o throwlib.o
$CXX $CFLAGS -c cxxtest.cpp -o cxxtest.o
$CXX -dynamiclib throwlib.o -o libthrow.dylib -install_name @executable_path/libthrow.dylib $RT
$CXX cxxtest.o libthrow.dylib -o cxxtest $RT
scp -qO libthrow.dylib cxxtest tiger:/tmp/
ssh tiger '/tmp/cxxtest; echo EXIT=$?'
