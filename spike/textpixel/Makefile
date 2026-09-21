WKT := /Users/shg/Developer/WebKitTiger
CC32 := $(WKT)/toolchain/bin/tiger-clang
CC64 := $(WKT)/toolchain/bin/tiger-clang64
# HarfBuzz for x86_64 was built with FreeType support, so it pulls freetype,
# png, brotli and zlib along with it. zlib is one of the three system libraries
# that actually have an x86_64 slice on Tiger, which is lucky.
HB64 := -I$(WKT)/toolchain/sysroot-x86_64/usr/include/harfbuzz -lharfbuzz -lfreetype -lpng16 -lbrotlidec -lbrotlicommon -lz
CF32 := -O1 -g -Wall -Fcompat/sdk-overlay
FW := -F$(WKT)/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks

all: raster32 shaper64

raster32: raster32.c common.h
	$(CC32) -O1 -g -Wall raster32.c -o $@ -ltigercompat \
	  -F$(WKT)/compat/sdk-overlay $(FW) \
	  -framework CoreFoundation -framework CoreServices -framework ApplicationServices

# x86_64 on Tiger links libSystem, libstdc++ and libz and nothing else, so this
# side gets HarfBuzz and the C library and makes its font decisions without any
# of CoreText, ATS or CoreFoundation.
shaper64: shaper64.c common.h
	$(CC64) -O1 -g -Wall -x c++ shaper64.c -o $@ $(HB64) \
	  -nostdinc++ -isystem $(WKT)/toolchain/sysroot-x86_64/usr/include/c++/v1 \
	  -stdlib=libc++ -lc++ -lc++abi

clean:
	rm -f raster32 shaper64
.PHONY: all clean
