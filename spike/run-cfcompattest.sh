#!/bin/bash
# The compat/cfcompat.c surface, Tiger against modern. See the file header.
set -e
WKT=/Users/shg/Developer/WebKitTiger
clang -w -o $WKT/build/cfcompattest-host $WKT/spike/cfcompattest.c -framework CoreFoundation
$WKT/build/cfcompattest-host > /tmp/cfc-host.txt 2>&1
$WKT/toolchain/bin/tiger-clang -w -O1 -DTIGER=1 \
  -isystem $WKT/compat/include -isystem $WKT/compat/include/sdk-fill \
  -ltigercompat -framework CoreFoundation -o $WKT/build/cfcompattest $WKT/spike/cfcompattest.c
scp -qO $WKT/build/cfcompattest tiger:/tmp/cfcompattest
ssh tiger "/tmp/cfcompattest" > /tmp/cfc-tiger.txt 2>&1
paste <(sed 's/=.*//' /tmp/cfc-tiger.txt) \
      <(sed 's/^[^=]*=//' /tmp/cfc-tiger.txt) \
      <(sed 's/^[^=]*=//' /tmp/cfc-host.txt) |
  awk -F'\t' 'NR>1{s=($2==$3)?"":"  <-- DIFFERS";printf "%-30s tiger=%-20s host=%-20s%s\n",$1,$2,$3,s}'
