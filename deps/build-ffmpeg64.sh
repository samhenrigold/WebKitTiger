#!/bin/bash
# FFmpeg 8.0, static, decoders only, x86_64-apple-macosx10.4, -O3 -march=core2, SSSE3 asm via nasm.
# Installs into toolchain/sysroot-x86_64/usr. Run from anywhere (bash, not zsh).
set -euo pipefail

WKT=/Users/shg/Developer/WebKitTiger
SRC=$WKT/deps/src/ffmpeg-8.0
BLD=$WKT/build/ffmpeg-x86_64
PREFIX=$WKT/toolchain/sysroot-x86_64/usr
CC=$WKT/toolchain/bin/tiger-clang64

DECODERS=libdav1d,h264,aac,aac_latm,mp3,vp8,vp9,opus,vorbis,flac,pcm_s16le,pcm_s16be,pcm_f32le
DEMUXERS=mov,matroska,av1,webm_dash_manifest,mp3,ogg,flac,wav,aac
PARSERS=h264,aac,aac_latm,vp8,vp9,opus,vorbis,mpegaudio,flac

# Tiger's <mach/i386/thread_status.h> already defines `struct xmm_reg`; rename FFmpeg's.
for f in libavutil/x86/asm.h libavcodec/x86/constants.c libavcodec/x86/constants.h; do
  grep -q ff_xmm_reg "$SRC/$f" || sed -i '' 's/xmm_reg/ff_xmm_reg/g' "$SRC/$f"
done

export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig

# tiger-clang64 puts -I$PREFIX/include ahead of FFmpeg's own -I, so a previously
# installed copy of the headers shadows the source tree. Remove it before building.
rm -rf "$PREFIX"/include/libav{util,codec,format} "$PREFIX"/include/libsw{scale,resample}

mkdir -p "$BLD"
cd "$BLD"

"$SRC/configure" \
  --prefix="$PREFIX" \
  --enable-cross-compile --target-os=darwin --arch=x86_64 \
  --cc="$CC" --ar="$WKT/toolchain/cctools/bin/i386-apple-darwin8-ar" \
  --nm="$WKT/toolchain/cctools/bin/i386-apple-darwin8-nm" \
  --ranlib="$WKT/toolchain/cctools/bin/i386-apple-darwin8-ranlib" \
  --strip=: --pkg-config-flags=--static \
  --enable-libdav1d \
  --x86asmexe=nasm --enable-asm --enable-x86asm \
  --extra-cflags="-O3 -march=core2 -fomit-frame-pointer -Dstatic_assert=_Static_assert" \
  --extra-ldflags="-ltigercompat" \
  --enable-static --disable-shared --disable-programs --disable-doc --disable-debug \
  --disable-network --disable-iconv --disable-securetransport --disable-videotoolbox \
  --disable-audiotoolbox --disable-coreimage --disable-avfoundation --disable-appkit \
  --disable-sdl2 --disable-zlib --disable-bzlib --disable-lzma \
  --disable-xlib --disable-libxcb --disable-avdevice --disable-avfilter \
  --disable-everything \
  --enable-decoder=$DECODERS \
  --enable-demuxer=$DEMUXERS \
  --enable-parser=$PARSERS \
  --enable-protocol=file \
  --enable-bsf=h264_mp4toannexb,vp9_superframe_split,extract_extradata \
  "$@"

make -j"$(sysctl -n hw.ncpu)"
make install
