WKT := /Users/shg/Developer/WebKitTiger
TIGER64 := $(WKT)/WebKit/Source/WebCore/platform/graphics/tiger64
SYS64 := $(WKT)/toolchain/sysroot-x86_64

CXX64 := $(WKT)/toolchain/bin/tiger-clang64++
CC32 := $(WKT)/toolchain/bin/tiger-clang

# x86_64 on Tiger links libSystem, libstdc++ and libz and nothing else, so
# FreeType and HarfBuzz come from the cross sysroot and the C++ library is our
# own libc++.
DEPS64 := -I$(SYS64)/usr/include/harfbuzz -I$(SYS64)/usr/include/freetype2 \
  -lharfbuzz -lfreetype -lpng16 -lbrotlidec -lbrotlicommon -lz
STD64 := -nostdinc++ -isystem $(SYS64)/usr/include/c++/v1 -stdlib=libc++ -lc++ -lc++abi -ltigercompat

all: tigerfontcachetest ctreference32

tigerfontcachetest: tigerfontcachetest.cpp $(TIGER64)/TigerFontDatabase.cpp $(TIGER64)/TigerAppleKern.cpp
	$(CXX64) -O1 -g -Wall -std=c++11 -I$(TIGER64) $^ -o $@ $(DEPS64) $(STD64)

ctreference32: ctreference32.c
	$(CC32) -O1 -g -Wall $< -o $@ -ltigercompat -F$(WKT)/compat/sdk-overlay \
	  -F$(WKT)/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks \
	  -framework CoreFoundation -framework ApplicationServices

clean:
	rm -f tigerfontcachetest ctreference32
.PHONY: all clean
