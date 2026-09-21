#!/bin/bash
# Cross-build the C/C++ dependencies for i386 / Mac OS X 10.4 as static libs into toolchain/sysroot-i386/usr.
set -u
WKT=/Users/shg/Developer/WebKitTiger
P=$WKT/toolchain/sysroot-i386/usr
export PATH=$WKT/toolchain/bin:$PATH
export CC=tiger-clang CXX=tiger-clang++ AR=tiger-ar RANLIB=tiger-ranlib NM=tiger-nm STRIP=tiger-strip
export CFLAGS="-O2" CPPFLAGS="-I$P/include" LDFLAGS="-L$P/lib"
# -ltigercompat: needed by every dep for the libc gaps below (clock_gettime, arc4random_buf,
# posix_memalign, __eprintf/assert, ...). LDFLAGS alone won't add it to an autotools-generated
# link line the way LIBS does, so it goes in LIBS.
export LIBS="-ltigercompat"
export PKG_CONFIG_PATH=$P/lib/pkgconfig PKG_CONFIG_LIBDIR=$P/lib/pkgconfig
HOST=i386-apple-darwin8
cd $WKT/deps/src
log() { echo "=== $1"; }
build() { # name dir configure-args...
  local name=$1 dir=$2; shift 2
  log "$name configure"; (cd $dir && ./configure --host=$HOST --prefix=$P --disable-shared --enable-static "$@") > $WKT/logs/dep-$name.log 2>&1 || { echo "$name CONFIGURE FAILED"; tail -15 $WKT/logs/dep-$name.log; return 1; }
  log "$name make"; (cd $dir && make -j8 && make install) >> $WKT/logs/dep-$name.log 2>&1 || { echo "$name MAKE FAILED"; grep -E "error:" $WKT/logs/dep-$name.log | head -10; return 1; }
  echo "$name OK"
}

# Most of these old-style AC_CHECK_FUNC probes ("char foo ();" then call it with no args)
# fail to cross-compile against the 10.4u SDK's real prototyped declarations -- clang treats
# the conflicting/too-few-args shapes as hard errors, not warnings -- so configure concludes
# the libc function is missing even though it's present and fine. Force the cache vars
# instead of fighting each test; this affects libxslt (snprintf/vsnprintf -> its bundled
# trio impl, which then fails to link) and libressl/curl (clock_gettime, pthread_create).
CROSS_FUNC_CACHE="ac_cv_func_snprintf=yes ac_cv_func_vsnprintf=yes ac_cv_func_clock_gettime=yes ac_cv_search_clock_gettime='none required' ac_cv_func_pthread_create=yes"

# sqlite's amalgamation Makefile always builds the .dylib as part of `all` (its
# ENABLE_LIB_SHARED gate only affects `install`, not the build), and -rpath is
# rejected by the ld64 targeting 10.4. Build/install only the static lib + headers.
build_sqlite() {
  local dir=sqlite-autoconf-3490100
  log "sqlite configure"
  (cd $dir && ./configure --host=$HOST --prefix=$P --disable-shared --enable-static --disable-readline) \
    > $WKT/logs/dep-sqlite.log 2>&1 || { echo "sqlite CONFIGURE FAILED"; tail -15 $WKT/logs/dep-sqlite.log; return 1; }
  log "sqlite make"
  (cd $dir && make libsqlite3.a sqlite3.h sqlite3.pc && \
    make install-lib install-headers install-pc) >> $WKT/logs/dep-sqlite.log 2>&1 || \
    { echo "sqlite MAKE FAILED"; grep -E "error:" $WKT/logs/dep-sqlite.log | head -10; return 1; }
  echo "sqlite OK"
}
build_sqlite

# zlib: not autotools -- its own configure just probes $CC directly, no --host needed since
# the compiler is already the cross one. --static skips the shared lib (and its install step)
# entirely, so plain `make install` is safe.
build_zlib() {
  local dir=zlib-1.3.1
  log "zlib configure"
  (cd $dir && ./configure --prefix=$P --static) > $WKT/logs/dep-zlib.log 2>&1 || \
    { echo "zlib CONFIGURE FAILED"; tail -15 $WKT/logs/dep-zlib.log; return 1; }
  log "zlib make"
  (cd $dir && make -j8 libz.a && make install) >> $WKT/logs/dep-zlib.log 2>&1 || \
    { echo "zlib MAKE FAILED"; grep -E "error:" $WKT/logs/dep-zlib.log | head -10; return 1; }
  echo "zlib OK"
}
build_zlib

build libxml2 libxml2-2.13.8 --without-python --without-lzma --without-zlib --without-iconv --without-icu

build libxslt libxslt-1.1.43 --without-python --without-crypto --with-libxml-prefix=$P \
  ac_cv_func_snprintf=yes ac_cv_func_vsnprintf=yes

# --disable-hardening: LibreSSL's configure probes for -fstack-protector-strong and,
# finding it accepted by the compiler, appends it to CFLAGS -- overriding tiger-clang's
# default -fno-stack-protector (later flag wins) and pulling in __stack_chk_guard/
# __stack_chk_fail, which Tiger's libSystem doesn't have. Also: rebuild from a clean
# tarball extraction if retrying -- a stale build dir keeps stack-protector-flavored
# .o files around even after reconfiguring with --disable-hardening, since make sees
# their sources as unchanged and only relinks.
build libressl libressl-4.1.0 --disable-tests --disable-hardening --with-openssldir=$P/etc/ssl \
  ac_cv_func_clock_gettime=yes ac_cv_search_clock_gettime="none required"

mkdir -p $P/etc/ssl && cp cacert.pem $P/etc/ssl/

# Must come after libressl and zlib are installed: curl's configure rejects --with-openssl=$P
# if it can't find libcrypto/libssl there yet, and --with-zlib=$P needs our libz.a/zlib.h
# (the 10.4u SDK's own zlib.h predates z_const/inflateReset2 -- zlib < 1.2.5.2 -- which is
# why we build our own instead of using the SDK's).
# --disable-ipv6: curl's Apple-specific IPv6 path links CoreFoundation/CoreServices/
# SystemConfiguration, which isn't part of this cross toolchain.
build_curl() {
  local dir=curl-8.14.1
  log "curl configure"
  (cd $dir && ./configure --host=$HOST --prefix=$P --disable-shared --enable-static \
    --with-openssl=$P --with-zlib=$P --without-libpsl --without-libidn2 --without-brotli --without-zstd --without-nghttp2 \
    --disable-ipv6 \
    --disable-ldap --disable-ldaps --disable-rtsp --disable-manual --without-ca-path --with-ca-bundle=$P/etc/ssl/cacert.pem \
    ac_cv_func_clock_gettime=yes ac_cv_func_pthread_create=yes) \
    > $WKT/logs/dep-curl.log 2>&1 || { echo "curl CONFIGURE FAILED"; tail -15 $WKT/logs/dep-curl.log; return 1; }
  # curl's own curlx_now() guards CLOCK_MONOTONIC_RAW with __builtin_available(macOS 10.12...),
  # which needs CoreFoundation at runtime pre-10.13 to check the OS version -- unavailable/
  # pointless on a fixed 10.4 target. Force the compile-time macro off after configure (a
  # command-line -D would just get redefined by curl_config.h's own #define).
  sed -i '' 's/#define HAVE_BUILTIN_AVAILABLE 1/#define HAVE_BUILTIN_AVAILABLE 0/' $dir/lib/curl_config.h
  log "curl make"
  # Only lib+include: the curl CLI tool (src/) additionally needs libclang_rt.osx.a's
  # os_version_check shim for unrelated reasons and we don't need the CLI for WebKit.
  (cd $dir/lib && make -j8 && make install) > $WKT/logs/dep-curl.log 2>&1 && \
  (cd $dir/include && make install) >> $WKT/logs/dep-curl.log 2>&1 || \
    { echo "curl MAKE FAILED"; grep -E "error:" $WKT/logs/dep-curl.log | head -10; return 1; }
  echo "curl OK"
}
build_curl

# --- ICU 76.1: needs a host (arm64, native clang) build first for its build-time tools
# (genrb/pkgdata/...), then a cross build against libc++ for the i386 target. ---
build_icu() {
  local src=$WKT/deps/src/icu/source
  local hostdir=$WKT/deps/build/icu-host
  local tgtdir=$WKT/deps/build/icu-i386
  mkdir -p $hostdir $tgtdir

  log "icu host configure+make"
  ( cd $hostdir && unset CC CXX AR RANLIB NM STRIP CFLAGS CPPFLAGS LDFLAGS LIBS PKG_CONFIG_PATH PKG_CONFIG_LIBDIR
    $src/runConfigureICU MacOSX --enable-static --disable-shared --disable-tests --disable-samples && make -j8 ) \
    > $WKT/logs/dep-icu-host.log 2>&1 || { echo "icu host FAILED"; tail -30 $WKT/logs/dep-icu-host.log; return 1; }

  log "icu i386 configure+make+install"
  ( cd $tgtdir
    export PATH=$WKT/toolchain/bin:$PATH
    export CC=tiger-clang CXX=tiger-clang++ AR=tiger-ar RANLIB=tiger-ranlib
    # libc++ headers live outside the SDK's own search path (toolchain/sysroot-i386, not
    # sdk/MacOSX10.4u.sdk), so -stdlib=libc++ alone doesn't find <memory> etc; add the -I.
    export CFLAGS="-O2" CXXFLAGS="-O2 -stdlib=libc++ -I$P/include/c++/v1" CPPFLAGS="-I$P/include" LDFLAGS="-L$P/lib -stdlib=libc++"
    # --disable-renaming: WebKit's bundled Source/WTF/icu/unicode headers are ICU 74.2 and
    # by default suffix every exported symbol with the *headers'* version (ucol_open_74).
    # A same-default build of our (newer) ICU 76.1 would suffix with _76 instead, so WebKit's
    # calls wouldn't resolve at link time even though the library is present. Unsuffixed
    # symbols side-step the version skew entirely (matches how Apple's own icucore.dylib
    # exports symbols); WebKit's build must define -DU_DISABLE_RENAMING=1 to match.
    $src/runConfigureICU MacOSX --prefix=$P --host=$HOST --with-cross-build=$hostdir \
      --enable-static --disable-shared --disable-tests --disable-samples --disable-extras --disable-tools --disable-icuio \
      --disable-renaming --with-data-packaging=static \
      && make -j8 && make install ) \
    > $WKT/logs/dep-icu-i386.log 2>&1 || { echo "icu i386 FAILED"; tail -30 $WKT/logs/dep-icu-i386.log; return 1; }
  echo "icu OK"
}
build_icu

ls $P/lib/*.a
