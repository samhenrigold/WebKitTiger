#!/bin/bash
# Build compiler-rt's builtins archive for i386-apple-macosx10.4: build/builtins-i386/
# libclang_rt.builtins-i386.a. This is NOT produced by the build/runtimes-i386 CMake build
# (that one only covers libc++/libc++abi/libunwind) -- compiler-rt is built standalone here
# by compiling the individual sources directly with the raw cross-compiler (no CMake), since
# compiler-rt's own CMake machinery targets its usual multi-arch "darwin" packaging layout
# and fights being pointed at a single bare-metal-style Mach-O target like this one.
#
# Must run under bash, not zsh (zsh doesn't word-split unquoted $VARS -- see toolchain/env.sh).
#
# ARTIFACT OWNERSHIP: this is the one producer of build/builtins-i386/libclang_rt.builtins-i386.a.
# Never hand-build and copy it elsewhere; rerun this script.
set -e
WKT=/Users/shg/Developer/WebKitTiger
SRC=$WKT/toolchain/src/llvm-project/compiler-rt/lib/builtins
OUT=$WKT/build/builtins-i386
OBJ=$OUT/obj
CC=$WKT/toolchain/llvm-tiger/bin/clang
# Raw cross-compiler, not the tiger-clang wrapper: compiler-rt is meant to be freestanding-ish
# and shouldn't see our -include tigerprelude.h forced prelude (harmless in practice, but
# there's no reason to risk it, matching how a real compiler-rt build invokes clang directly).
FLAGS=(-target i386-apple-macosx10.4 -isysroot $WKT/sdk/MacOSX10.4u.sdk -mmacosx-version-min=10.4 \
  -fno-stack-protector --ld-path=$WKT/toolchain/cctools/bin/i386-apple-darwin8-ld \
  -Os -fPIC -DVISIBILITY_HIDDEN -I$SRC)

mkdir -p "$OBJ"
rm -f "$OBJ"/*.o

echo "=== generic builtins/*.c ==="
cd "$SRC"
ok=0; fail=0
for f in *.c; do
  out="$OBJ/$(basename "$f" .c).o"
  if $CC "${FLAGS[@]}" -c "$f" -o "$out" > "$WKT/logs/builtins-i386-build.log" 2>&1; then
    ok=$((ok+1))
  else
    fail=$((fail+1))
  fi
done
echo "ok=$ok fail=$fail (see logs/builtins-i386-build.log for the failing ones)"

# Known-expected failures, not bugs -- exclude rather than fix:
#   crtbegin.c / crtend.c    -- ELF .ctors/.dtors section attrs, never apply to Mach-O.
#   apple_versioning.c        -- needs Availability.h, which our SDK overlay doesn't provide;
#                                 only used for symbol-versioning aliases we don't need.
#   clear_cache.c              -- needs libkern/OSCacheControl.h (missing from our SDK); only
#                                 needed for JIT code-cache flushing, not used by the spikes
#                                 this archive exists for.
#   os_version_check.c        -- needs dispatch.h + CoreFoundation, targets 10.7+; deliberately
#                                 NOT what we want -- __isPlatformVersionAtLeast/
#                                 __isOSVersionAtLeast are reimplemented for Tiger in
#                                 compat/availability.c instead (reads ProductVersion from
#                                 SystemVersion.plist). Pulling this file in would shadow that.
for sym in crtbegin crtend apple_versioning clear_cache os_version_check; do
  rm -f "$OBJ/$sym.o"
done

echo "=== i386/*.S and i386/*.c (prefer over the generic .c for the same symbol: real carry-chain asm) ==="
# These duplicate symbols already compiled above from the generic .c sources -- i386's asm/C
# overrides win, so drop the generic ones first.
for sym in ashldi3 ashrdi3 divdi3 floatdidf floatdisf floatdixf floatundidf floatundisf \
           floatundixf lshrdi3 moddi3 muldi3 udivdi3 umoddi3 fp_mode; do
  rm -f "$OBJ/$sym.o"
done
cd "$SRC/i386"
ok=0; fail=0
for f in *.S *.c; do
  out="$OBJ/i386_$(basename "$f" | sed 's/\.[cS]$//').o"
  if $CC "${FLAGS[@]}" -c "$f" -o "$out" >> "$WKT/logs/builtins-i386-build.log" 2>&1; then
    ok=$((ok+1))
  else
    fail=$((fail+1))
    echo "i386/$f FAILED" >> "$WKT/logs/builtins-i386-build.log"
  fi
done
echo "i386-specific: ok=$ok fail=$fail"

echo "=== archiving ==="
cd "$OBJ"
$WKT/toolchain/bin/tiger-ar rcs "$OUT/libclang_rt.builtins-i386.a" *.o
$WKT/toolchain/bin/tiger-ranlib "$OUT/libclang_rt.builtins-i386.a" 2>&1 | grep -v "has no symbols" || true
# "has no symbols" warnings from ranlib on ti/tf (__int128 / long-double-as-128-bit) builtins
# are expected and harmless: those types don't materialize on a 32-bit i386 target, so those
# translation units compile to empty objects.
ls -la "$OUT/libclang_rt.builtins-i386.a"
