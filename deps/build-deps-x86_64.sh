#!/bin/bash
# Cross-build the x86_64 dependency set (64-bit content process on Tiger's x86_64 libSystem,
# see NOTES.md "DIRECTION SET BY THE USER") as static libs into toolchain/sysroot-x86_64/usr.
# Sibling of deps/build-c-deps.sh (the i386 set); kept separate because flags, toolchain
# binaries and a couple of source-tree-local patches differ per arch.
set -u
WKT=/Users/shg/Developer/WebKitTiger
P=$WKT/toolchain/sysroot-x86_64/usr
export PATH=$WKT/toolchain/bin:$PATH
export CC=tiger-clang64 CXX=tiger-clang64++ AR=tiger-ar RANLIB=tiger-ranlib NM=tiger-nm STRIP=tiger-strip
# Core 2 Duo T7500: SSSE3 is the ceiling (no SSE4). tiger-ar/ranlib/nm/strip are the same
# cctools binaries used for i386 -- they operate on Mach-O generically, not arch-specific.
export CFLAGS="-O3 -march=core2 -mtune=core2" CPPFLAGS="-I$P/include" LDFLAGS="-L$P/lib"
export LIBS="-ltigercompat"
export PKG_CONFIG_PATH=$P/lib/pkgconfig PKG_CONFIG_LIBDIR=$P/lib/pkgconfig
HOST=x86_64-apple-darwin8
NASM=/opt/homebrew/bin/nasm  # brew install nasm; needed by libjpeg-turbo SIMD and dav1d asm.
cd $WKT/deps/src
log() { echo "=== $1"; }
build() { # name dir configure-args...
  local name=$1 dir=$2; shift 2
  log "$name configure"; (cd $dir && ./configure --host=$HOST --prefix=$P --disable-shared --enable-static "$@") > $WKT/logs/dep64-$name.log 2>&1 || { echo "$name CONFIGURE FAILED"; tail -15 $WKT/logs/dep64-$name.log; return 1; }
  log "$name make"; (cd $dir && make -j8 && make install) >> $WKT/logs/dep64-$name.log 2>&1 || { echo "$name MAKE FAILED"; grep -E "error:" $WKT/logs/dep64-$name.log | head -10; return 1; }
  echo "$name OK"
}

# --- zlib (own configure, not autotools) ---
build_zlib() {
  local dir=x86_64/zlib-1.3.1
  (cd $dir && ./configure --prefix=$P --static) > $WKT/logs/dep64-zlib.log 2>&1 && \
  (cd $dir && make -j8 libz.a && make install) >> $WKT/logs/dep64-zlib.log 2>&1 && echo "zlib OK" || \
    { echo "zlib FAILED"; tail -30 $WKT/logs/dep64-zlib.log; return 1; }
}
build_zlib

# --- brotli (CMake) ---
build_cmake() { # name srcdir extra-cmake-args...
  local name=$1 src=$2 builddir=$WKT/deps/build/$name-x86_64; shift 2
  mkdir -p $builddir
  cat > $builddir/toolchain-tiger64.cmake <<EOF
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $WKT/toolchain/bin/tiger-clang64)
set(CMAKE_CXX_COMPILER $WKT/toolchain/bin/tiger-clang64++)
set(CMAKE_AR $WKT/toolchain/bin/tiger-ar)
set(CMAKE_RANLIB $WKT/toolchain/bin/tiger-ranlib)
set(CMAKE_ASM_NASM_COMPILER $NASM)
set(CMAKE_FIND_ROOT_PATH $P)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
  log "$name cmake configure"
  ( cd $builddir && PATH="/opt/homebrew/bin:$PATH" cmake -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=$builddir/toolchain-tiger64.cmake \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$P \
      -DCMAKE_C_FLAGS="-O3 -march=core2 -mtune=core2 -I$P/include" \
      -DCMAKE_EXE_LINKER_FLAGS="-L$P/lib -ltigercompat" \
      "$@" $src ) > $WKT/logs/dep64-$name.log 2>&1 || { echo "$name CONFIGURE FAILED"; tail -40 $WKT/logs/dep64-$name.log; return 1; }
  log "$name make"
  ( cd $builddir && PATH="/opt/homebrew/bin:$PATH" make -j8 && make install ) >> $WKT/logs/dep64-$name.log 2>&1 || \
    { echo "$name MAKE FAILED"; grep -E "error:" $WKT/logs/dep64-$name.log | head -20; return 1; }
  echo "$name OK"
}
build_cmake brotli $WKT/deps/src/brotli-1.1.0 -DBUILD_SHARED_LIBS=OFF -DBROTLI_DISABLE_TESTS=ON

# --- nghttp2 (lib only) ---
build nghttp2 nghttp2-1.65.0 --enable-lib-only \
  ac_cv_func_clock_gettime=yes ac_cv_search_clock_gettime="none required"

# --disable-hardening: same reason as the i386 build (see deps/build-c-deps.sh).
# apps/ (ocspcheck) is skipped: it hit the x86_64 classic-stub ld64 crash that's since been
# fixed (toolchain/patches/cctools-ld64-x86_64-classic-stubs.patch) -- not worth rebuilding
# for a CLI tool we don't need, but note the crash is gone if you ever want it.
build_libressl64() {
  local dir=x86_64/libressl-4.1.0
  log "libressl configure"
  (cd $dir && ./configure --host=$HOST --prefix=$P --disable-shared --enable-static --disable-tests --disable-hardening --with-openssldir=$P/etc/ssl \
    ac_cv_func_clock_gettime=yes ac_cv_search_clock_gettime="none required") \
    > $WKT/logs/dep64-libressl.log 2>&1 || { echo "libressl CONFIGURE FAILED"; tail -30 $WKT/logs/dep64-libressl.log; return 1; }
  log "libressl make"
  (cd $dir && make -j8) >> $WKT/logs/dep64-libressl.log 2>&1 || { echo "libressl MAKE FAILED"; grep -E "error:" $WKT/logs/dep64-libressl.log | head -20; return 1; }
  (cd $dir && make -C crypto install && make -C ssl install && make -C tls install && make -C include install) \
    >> $WKT/logs/dep64-libressl.log 2>&1 || { echo "libressl INSTALL FAILED"; tail -30 $WKT/logs/dep64-libressl.log; return 1; }
  mkdir -p $P/etc/ssl && cp cacert.pem $P/etc/ssl/
  echo "libressl OK"
}
build_libressl64

# --- curl: with OpenSSL(LibreSSL)+zlib+brotli+nghttp2, HTTP/2 enabled. Was blocked on the
# cctools ld64 x86_64 classic-stub crash (see libressl above); fixed upstream, curl links
# and runs fine now -- verified with a live HTTP/2 GET over TLS on the Tiger box.
build_curl64() {
  local dir=x86_64/curl-8.14.1
  (cd $dir && ./configure --host=$HOST --prefix=$P --disable-shared --enable-static \
    --with-openssl=$P --with-zlib=$P --with-brotli=$P --with-nghttp2=$P \
    --without-libpsl --without-libidn2 --without-zstd --disable-ipv6 \
    --disable-ldap --disable-ldaps --disable-rtsp --disable-manual --without-ca-path --with-ca-bundle=$P/etc/ssl/cacert.pem \
    ac_cv_func_clock_gettime=yes ac_cv_func_pthread_create=yes) \
    > $WKT/logs/dep64-curl.log 2>&1 || { echo "curl CONFIGURE FAILED"; tail -40 $WKT/logs/dep64-curl.log; return 1; }
  # See deps/build-c-deps.sh's build_curl for why: __builtin_available(macOS 10.12...) in
  # curl's own curlx_now() needs a 10.7+-only compiler-rt shim we don't have.
  sed -i '' 's/#define HAVE_BUILTIN_AVAILABLE 1/#define HAVE_BUILTIN_AVAILABLE 0/' $dir/lib/curl_config.h
  (cd $dir/lib && make -j8 && make install) >> $WKT/logs/dep64-curl.log 2>&1 || { echo "curl MAKE FAILED"; grep -E "error:" $WKT/logs/dep64-curl.log | head -20; return 1; }
  (cd $dir/include && make install) >> $WKT/logs/dep64-curl.log 2>&1 || { echo "curl INSTALL FAILED"; tail -30 $WKT/logs/dep64-curl.log; return 1; }
  echo "curl OK"
}
build_curl64

# sqlite needs its own recipe (skips the .dylib target `all` always builds); see
# deps/build-c-deps.sh's build_sqlite for the full explanation.
build_sqlite64() {
  local dir=x86_64/sqlite-autoconf-3490100
  (cd $dir && ./configure --host=$HOST --prefix=$P --disable-shared --enable-static --disable-readline) \
    > $WKT/logs/dep64-sqlite.log 2>&1 || return 1
  (cd $dir && make libsqlite3.a sqlite3.h sqlite3.pc && make install-lib install-headers install-pc) \
    >> $WKT/logs/dep64-sqlite.log 2>&1 || return 1
  echo "sqlite OK"
}
build_sqlite64

build libxml2 x86_64/libxml2-2.13.8 --without-python --without-lzma --without-zlib --without-iconv --without-icu
build libxslt x86_64/libxslt-1.1.43 --without-python --without-crypto --with-libxml-prefix=$P \
  ac_cv_func_snprintf=yes ac_cv_func_vsnprintf=yes
build libpng x86_64/libpng-1.6.48

# libjpeg-turbo WITH SIMD (nasm-assembled SSE2/AVX2 x86_64 kernels; the CPU dispatches to
# what the running core actually supports, SSE2 at worst since it's the x86_64 baseline).
build_cmake libjpeg-turbo $WKT/deps/src/libjpeg-turbo-3.1.0 \
  -DENABLE_SHARED=0 -DENABLE_STATIC=1 -DWITH_SIMD=1 -DWITH_TURBOJPEG=0

build_libwebp64() {
  local dir=x86_64/libwebp-1.5.0
  (cd $dir && ./configure --host=$HOST --prefix=$P --disable-shared --enable-static \
    --disable-png --disable-jpeg --disable-tiff --disable-gif --disable-wic --disable-gl --disable-sdl) \
    > $WKT/logs/dep64-libwebp.log 2>&1 || return 1
  (cd $dir && make -j8 -C sharpyuv && make -j8 -C src) >> $WKT/logs/dep64-libwebp.log 2>&1 || return 1
  (cd $dir && make -C sharpyuv install && make -C src install) >> $WKT/logs/dep64-libwebp.log 2>&1 || return 1
  echo "libwebp OK"
}
build_libwebp64

# --- dav1d (meson; SSSE3 asm via nasm) ---
build_meson() { # name srcdir cross_ini_extra_c_args(space-joined,quoted-later) meson-args...
  local name=$1 src=$2; shift 2
  local builddir=$WKT/deps/build/$name-x86_64
  mkdir -p $builddir
  cat > $builddir/tiger64-cross.ini <<EOF
[binaries]
c = '$WKT/toolchain/bin/tiger-clang64'
cpp = '$WKT/toolchain/bin/tiger-clang64++'
ar = '$WKT/toolchain/bin/tiger-ar'
strip = '$WKT/toolchain/bin/tiger-strip'
nasm = '$NASM'
pkg-config = 'pkg-config'

[built-in options]
c_args = ['-O3', '-march=core2', '-mtune=core2', '-Qunused-arguments', '-I$P/include']
c_link_args = ['-L$P/lib', '-ltigercompat']
cpp_args = ['-std=c++17', '-stdlib=libc++', '-Qunused-arguments', '-I$P/include/c++/v1', '-I$P/include']
cpp_link_args = ['-L$P/lib', '-stdlib=libc++', '-ltigercompat']
default_library = 'static'

[host_machine]
system = 'darwin'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[properties]
pkg_config_libdir = '$P/lib/pkgconfig'
needs_exe_wrapper = true
EOF
  # -Qunused-arguments: without it, meson's own compiler-probe invocations (which always add
  # -Werror=unused-command-line-argument to catch driver mistakes) turn tiger-clang64's
  # harmless "argument unused during compilation" notices for --ld-path/-L on -c-only probe
  # compiles into hard errors -- breaks basic feature detection (e.g. stdatomic.h) otherwise.
  log "$name meson setup"
  ( cd $src && PATH="/opt/homebrew/bin:$PATH" meson setup $builddir/build --cross-file $builddir/tiger64-cross.ini \
      --prefix=$P --buildtype=release --default-library=static "$@" ) \
    > $WKT/logs/dep64-$name.log 2>&1 || { echo "$name CONFIGURE FAILED"; tail -50 $WKT/logs/dep64-$name.log; return 1; }
  log "$name ninja"
  ( cd $builddir/build && PATH="/opt/homebrew/bin:$PATH" ninja && ninja install ) >> $WKT/logs/dep64-$name.log 2>&1 || \
    { echo "$name BUILD FAILED"; grep -E "error|FAILED" $WKT/logs/dep64-$name.log | head -30; return 1; }
  echo "$name OK"
}
build_meson dav1d $WKT/deps/src/dav1d-1.5.1 -Denable_tools=false -Denable_tests=false -Denable_examples=false

# --- libavif (CMake, dav1d as the AV1 decoder; libyuv/libsharpyuv/libxml2 integrations off
# since we don't have/need them here) ---
build_cmake libavif $WKT/deps/src/libavif-1.2.1 \
  -DBUILD_SHARED_LIBS=OFF -DAVIF_BUILD_APPS=OFF -DAVIF_BUILD_TESTS=OFF -DAVIF_BUILD_EXAMPLES=OFF \
  -DAVIF_CODEC_DAV1D=SYSTEM -DAVIF_LIBYUV=OFF -DAVIF_LIBSHARPYUV=OFF -DAVIF_LIBXML2=OFF \
  -DPKG_CONFIG_EXECUTABLE=/opt/homebrew/bin/pkg-config -DCMAKE_PREFIX_PATH=$P

build freetype freetype-2.13.3 --with-zlib=yes --with-png=yes --with-harfbuzz=no --with-bzip2=no --with-brotli=no
build expat expat-2.6.4 --without-docbook --without-tests --without-examples

# fontconfig: `mkstemp` false-negatives the same AC_CHECK_FUNC way sqlite/libressl's
# clock_gettime did on i386 (see deps/build-c-deps.sh). Separately, its fcobjshash.gperf
# generation step runs the C preprocessor over a header and greps out everything between
# CUT_OUT_BEGIN/END markers -- but tiger-clang64's baked-in `-include tigerprelude.h` isn't
# wrapped by those markers, so tigerprelude's transitively-included system headers
# (pthread.h, sys/types.h, ...) leak straight into the generated file and collide with the
# real system headers fcobjs.c also includes. Fix: override $(CPP) for this one build with
# the raw cross-compiler (no -include), bypassing the wrapper just for preprocessing.
build_fontconfig64() {
  local dir=fontconfig-2.15.0
  (cd $dir && ./configure --host=$HOST --prefix=$P --disable-shared --enable-static --with-expat=$P \
    --disable-docs --disable-docbook --disable-cache-build --disable-nls \
    ac_cv_func_clock_gettime=yes ac_cv_search_clock_gettime="none required" ac_cv_func_mkstemp=yes) \
    > $WKT/logs/dep64-fontconfig.log 2>&1 || { echo "fontconfig CONFIGURE FAILED"; tail -40 $WKT/logs/dep64-fontconfig.log; return 1; }
  (cd $dir && make -j8 CPP="$WKT/toolchain/llvm-tiger/bin/clang -target x86_64-apple-macosx10.4 -isysroot $WKT/sdk/MacOSX10.4u.sdk -E" \
    && make install) >> $WKT/logs/dep64-fontconfig.log 2>&1 || { echo "fontconfig MAKE FAILED"; grep -E "error:" $WKT/logs/dep64-fontconfig.log | head -20; return 1; }
  echo "fontconfig OK"
}
build_fontconfig64

# --- pixman (meson-only as of 0.43; SSSE3 auto-detected/compiled in) ---
build_meson pixman $WKT/deps/src/pixman-0.43.4 -Dtests=disabled -Ddemos=disabled -Dgtk=disabled -Dlibpng=disabled -Dopenmp=disabled

# --- cairo (meson-only as of 1.18; no quartz/glib/xlib/xcb; fontconfig+freetype+png+zlib on) ---
# -DHAVE_CTIME_R=1: cairo-ps-surface.c has `#ifndef HAVE_CTIME_R` around a fallback
# ctime_r() shim, but cairo's meson.build never actually probes for/defines HAVE_CTIME_R,
# so the shim always compiles in and collides with the 10.4u SDK's real declaration
# ("static declaration follows non-static declaration"). Force the macro instead.
# Only src/libcairo.a is installed (manually, headers + pkgconfig too): cairo's own
# `examples`/`util/cairo-script` test tools fail to link (missing -lexpat wiring for
# fontconfig, and a missing __udivti3 compiler-rt symbol) but we only need the library.
build_cairo64() {
  local name=cairo src=$WKT/deps/src/cairo-1.18.2 builddir=$WKT/deps/build/cairo-x86_64
  mkdir -p $builddir
  cat > $builddir/tiger64-cross.ini <<EOF
[binaries]
c = '$WKT/toolchain/bin/tiger-clang64'
ar = '$WKT/toolchain/bin/tiger-ar'
strip = '$WKT/toolchain/bin/tiger-strip'
pkg-config = 'pkg-config'

[built-in options]
c_args = ['-O3', '-march=core2', '-mtune=core2', '-Qunused-arguments', '-DHAVE_CTIME_R=1', '-I$P/include']
c_link_args = ['-L$P/lib', '-ltigercompat']
default_library = 'static'

[host_machine]
system = 'darwin'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[properties]
pkg_config_libdir = '$P/lib/pkgconfig'
needs_exe_wrapper = true
EOF
  ( cd $src && PATH="/opt/homebrew/bin:$PATH" meson setup $builddir/build --cross-file $builddir/tiger64-cross.ini \
      --prefix=$P --buildtype=release --default-library=static \
      -Dquartz=disabled -Dglib=disabled -Dxlib=disabled -Dxcb=disabled -Ddwrite=disabled -Dtee=disabled \
      -Dspectre=disabled -Dtests=disabled -Dgtk2-utils=disabled \
      -Dfontconfig=enabled -Dfreetype=enabled -Dpng=enabled -Dzlib=enabled ) \
    > $WKT/logs/dep64-cairo.log 2>&1 || { echo "cairo CONFIGURE FAILED"; tail -50 $WKT/logs/dep64-cairo.log; return 1; }
  ( cd $builddir/build && PATH="/opt/homebrew/bin:$PATH" ninja src/libcairo.a ) >> $WKT/logs/dep64-cairo.log 2>&1 || \
    { echo "cairo BUILD FAILED"; grep -E "error:" $WKT/logs/dep64-cairo.log | head -20; return 1; }
  mkdir -p $P/include/cairo $P/lib/pkgconfig
  cp $builddir/build/src/libcairo.a $P/lib/
  cp $src/src/cairo.h $src/src/cairo-deprecated.h $src/src/cairo-pdf.h $src/src/cairo-ps.h \
     $src/src/cairo-svg.h $src/src/cairo-ft.h $src/src/cairo-script.h $src/src/cairo-tee.h \
     $src/src/cairo-version.h $builddir/build/src/cairo-features.h $P/include/cairo/
  cp $builddir/build/meson-private/cairo.pc $builddir/build/meson-private/cairo-ft.pc \
     $builddir/build/meson-private/cairo-fc.pc $builddir/build/meson-private/cairo-pdf.pc \
     $builddir/build/meson-private/cairo-ps.pc $builddir/build/meson-private/cairo-svg.pc \
     $builddir/build/meson-private/cairo-png.pc $P/lib/pkgconfig/
  echo "cairo OK"
}
build_cairo64

# --- ICU 76.1: cross-build from the existing host build (deps/build/icu-host, built once
# for the i386 pass -- shared, arch-independent since it only runs on this Mac). ---
# Uses deps/src/icu-x86_64 (a separate copy of the ICU source), NOT deps/src/icu: this
# Apple Silicon Mac's Rosetta 2 can transparently *execute* x86_64 binaries, which fools
# ICU's (and autoconf's in general) "are we cross compiling" runtime check -- it compiles
# a trivial x86_64 conftest and runs it to decide, and because Rosetta lets it run,
# configure concludes cross_compiling=no even though --host=x86_64-apple-darwin8 was given.
# That silently drops `data` from ICU's SUBDIRS (data is only built "if $tools=true or
# cross_compiling=yes"), producing a 680-byte *stub* libicudata.a with no real Unicode
# data instead of the real ~30MB one. The patched copy's configure hardcodes
# cross_compiling=yes, skipping the runtime-execution probe entirely; see its diff against
# deps/src/icu/source/configure for the exact change (search "TIGER64: patched").
build_icu64() {
  local src=$WKT/deps/src/icu-x86_64/source
  local hostdir=$WKT/deps/build/icu-host
  local tgtdir=$WKT/deps/build/icu-x86_64
  mkdir -p $tgtdir
  log "icu x86_64 configure+make+install"
  ( cd $tgtdir
    export PATH=$WKT/toolchain/bin:$PATH
    export CC=tiger-clang64 CXX=tiger-clang64++ AR=tiger-ar RANLIB=tiger-ranlib
    export CFLAGS="-O3 -march=core2 -mtune=core2" CXXFLAGS="-O3 -march=core2 -mtune=core2 -stdlib=libc++ -I$P/include/c++/v1" CPPFLAGS="-I$P/include" LDFLAGS="-L$P/lib -stdlib=libc++"
    $src/runConfigureICU MacOSX --prefix=$P --host=$HOST --with-cross-build=$hostdir \
      --enable-static --disable-shared --disable-tests --disable-samples --disable-extras --disable-tools --disable-icuio \
      --disable-renaming --with-data-packaging=static \
      && make -j8 && make install ) \
    > $WKT/logs/dep64-icu.log 2>&1 || { echo "icu x86_64 FAILED"; tail -40 $WKT/logs/dep64-icu.log; return 1; }
  echo "icu OK"
}
build_icu64

# --- harfbuzz (meson; freetype enabled this time, since freetype now exists for x86_64;
# still no coretext/glib/icu -- WebKit feeds it font tables itself) ---
build_meson harfbuzz $WKT/deps/src/harfbuzz-14.5.0 \
  -Dglib=disabled -Dcairo=disabled -Dicu=disabled -Dfreetype=enabled -Dcoretext=disabled \
  -Dgraphite=disabled -Dwasm=disabled -Dtests=disabled -Dutilities=disabled -Ddocs=disabled \
  -Dbenchmark=disabled -Dintrospection=disabled

ls $P/lib/*.a
