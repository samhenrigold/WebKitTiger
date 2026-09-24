#!/bin/bash
# Rebuild just the x86_64 font dependency during an idle Mini build window.
set -euo pipefail
WKT=${WKT:-/Users/shg/Developer/WebKitTiger}
PREFIX="$WKT/toolchain/sysroot-x86_64/usr"
export PATH="$WKT/toolchain/bin:$PATH"
export CC=tiger-clang64 CXX=tiger-clang64++ AR=tiger-ar RANLIB=tiger-ranlib NM=tiger-nm STRIP=tiger-strip
export CFLAGS='-O3 -march=core2 -mtune=core2' CPPFLAGS="-I$PREFIX/include" LDFLAGS="-L$PREFIX/lib"
export LIBS=-ltigercompat cross_compiling=yes
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
python3 "$WKT/deps/apply-freetype-patches.py" "$WKT/deps/src/freetype-2.13.3"
cd "$WKT/deps/src/freetype-2.13.3"
./configure --host=x86_64-apple-darwin8 --prefix="$PREFIX" --disable-shared --enable-static \
    --with-zlib=yes --with-png=yes --with-harfbuzz=no --with-bzip2=no --with-brotli=no
make -j8
make install
