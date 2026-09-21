#!/bin/bash
# posix_memalign / aligned_alloc on Tiger, including alignments above the page size.
set -e
WKT=/Users/shg/Developer/WebKitTiger
$WKT/toolchain/bin/tiger-clang -g -O1 \
  -isystem $WKT/compat/include -isystem $WKT/compat/include/sdk-fill \
  -ltigercompat $WKT/spike/memaligntest.c -o $WKT/spike/memaligntest
scp -qO $WKT/spike/memaligntest tiger:/tmp/memaligntest
ssh tiger "/tmp/memaligntest; echo EXIT=\$?"
