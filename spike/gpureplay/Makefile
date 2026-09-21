# gpureplay -- the GPU process's job in one process: record a WebCore display
# list, replay it through the CoreGraphics backend into an ImageBuffer, check
# the pixels against direct CoreGraphics, and put the tile on a CALayer through
# spike/CAHost's scene applier.
#
# Links the i386 GPU configuration's WebCore. Build that first:
#   ninja -C build/tiger-gpu WebCore
#
# The library list and its order are cribbed from the web process's own link
# line (build/tiger-web-port/build.ninja, CXX_EXECUTABLE_LINKER__WebProcess),
# minus cairo and the image codecs the i386 side does not build, plus the
# frameworks and the Apple TV QuartzCore the CA host needs.

ROOT     := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../..)
BUILD    := $(ROOT)/build/tiger-gpu
SYSROOT  := $(ROOT)/toolchain/sysroot-i386/usr
CLANGXX  := $(ROOT)/toolchain/bin/tiger-clang++
CAHOST   := $(ROOT)/spike/CAHost
FW       := $(CAHOST)/Frameworks/QuartzCore.framework
APP      := $(ROOT)/build/GPUReplay.app

# WebCore's own headers come from the build tree's framework/private header
# directories, the same ones its translation units see.
# The two header maps are how WebCore's own translation units resolve
# "PlatformExportMacros.h" and the other 2,000 unqualified WebCore includes;
# replicating the -I list by hand is a losing game.
#
# The compiler flags the toolchain file gives every CMake target in this port
# (toolchain/tiger.cmake, _tiger_common_flags and the framework search path) are
# not in the tiger-clang wrapper, so they are repeated here. Without them
# <Availability.h> alone is enough to stop the build.
SDKFW    := $(ROOT)/sdk/MacOSX10.4u.sdk/System/Library/Frameworks
CXXFLAGS := -std=c++23 -fobjc-runtime=macosx-fragile-10.4 -O2 -g \
            -fno-exceptions -fno-rtti -fwrapv -fno-strict-aliasing \
            -femulated-tls -fno-stack-protector \
            -D__ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES=0 \
            -stdlib=libc++ -cxx-isystem $(SYSROOT)/include/c++/v1 \
            -D_LIBCPP_DISABLE_AVAILABILITY \
            -isystem $(ROOT)/compat/sdk-overlay/usr/include \
            -isystem $(ROOT)/compat/include/sdk-fill \
            -isystem $(ROOT)/compat/include \
            -isystem $(ROOT)/compat/dispatch/include \
            -F$(CAHOST)/Frameworks \
            -F$(BUILD) \
            -F$(ROOT)/compat/sdk-overlay \
            -F$(SDKFW) \
            -F$(SDKFW)/ApplicationServices.framework/Frameworks \
            -F$(SDKFW)/Carbon.framework/Frameworks \
            -F$(SDKFW)/CoreServices.framework/Frameworks \
            -DBUILDING_WEBKIT -DBUILDING_WITH_CMAKE -DHAVE_CONFIG_H \
            -DSTATICALLY_LINKED_WITH_JavaScriptCore -DSTATICALLY_LINKED_WITH_PAL \
            -DSTATICALLY_LINKED_WITH_WTF -DSTATICALLY_LINKED_WITH_bmalloc \
            -DU_DISABLE_RENAMING -DWTF_PLATFORM_TIGER \
            -I$(BUILD)/Source/WebCore/WebCore-framework-headers.hmap \
            -I$(BUILD)/Source/WebCore/WebCore-project-headers.hmap \
            -I$(BUILD)/Source/JavaScriptCore/JavaScriptCore-framework-headers.hmap \
            -I$(BUILD)/Source/JavaScriptCore/JavaScriptCore-project-headers.hmap \
            -I$(BUILD) \
            -I$(BUILD)/WebCore.framework/Versions/A/PrivateHeaders \
            -I$(BUILD)/WebCore/DerivedSources \
            -I$(BUILD)/WTF/Headers \
            -I$(BUILD)/PAL/Headers \
            -I$(BUILD)/bmalloc/Headers \
            -I$(ROOT)/WebKit/Source/WebCore \
            -I$(ROOT)/WebKit/Source/WebCore/platform/graphics \
            -I$(ROOT)/WebKit/Source/WebCore/platform/graphics/cg \
            -I$(ROOT)/WebKit/Source/WebCore/platform/graphics/displaylists \
            -I$(CAHOST)

WKLIBS := $(BUILD)/lib/libWebCore.a $(BUILD)/lib/libJavaScriptCore.a \
          $(BUILD)/lib/libPAL.a $(BUILD)/lib/libWTF.a $(BUILD)/lib/libbmalloc.a

SYSLIBS := $(SYSROOT)/lib/libicui18n.a $(SYSROOT)/lib/libicuuc.a $(SYSROOT)/lib/libicudata.a \
           $(SYSROOT)/lib/libxml2.a $(SYSROOT)/lib/libxslt.a $(SYSROOT)/lib/libsqlite3.a \
           $(SYSROOT)/lib/libcurl.a $(SYSROOT)/lib/libssl.a $(SYSROOT)/lib/libcrypto.a \
           $(SYSROOT)/lib/libharfbuzz.a $(SYSROOT)/lib/libfreetype.a \
           $(SYSROOT)/lib/libjpeg.a $(SYSROOT)/lib/libpng.a \
           $(SYSROOT)/lib/libwebpdemux.a $(SYSROOT)/lib/libwebp.a $(SYSROOT)/lib/libsharpyuv.a \
           $(SYSROOT)/lib/libbrotlidec.a $(SYSROOT)/lib/libbrotlicommon.a \
           $(SYSROOT)/lib/libz.a

# -force_load on libtigercompat only, NOT the port's usual -ObjC. The LINK RULE
# in NOTES.md needs every ObjC member of libtigercompat in the link, because its
# Foundation and AppKit surface is categories and a static archive member is
# pulled in only when it defines a referenced symbol. But -ObjC applies to every
# archive on the line, and libWebCore.a has ~200 Objective-C++ members, most of
# which this spike never calls and some of whose dependencies are not built for
# this configuration. force_load says the same thing about one archive.
RTLIBS := -lc++ -lc++abi -lunwind -ltigerdispatch \
          -Wl,-force_load,$(SYSROOT)/lib/libtigercompat.a \
          $(wildcard $(ROOT)/build/builtins-i386/libclang_rt.builtins-i386.a)

# Tiger's own QuartzCore first, then the Apple TV one: libobjc keeps the FIRST
# registration of a duplicated class name. Same rule as CAVideo; see the note in
# spike/CAHost/Makefile.
LDFLAGS := -L$(SYSROOT)/lib \
           $(WKLIBS) $(SYSLIBS) $(WKLIBS) $(RTLIBS) \
           $(ROOT)/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/QuartzCore.framework/QuartzCore \
           $(FW)/Versions/A/QuartzCore \
           -framework Cocoa -framework OpenGL -framework ApplicationServices \
           -framework Carbon -framework CoreServices -framework IOKit \
           -framework SystemConfiguration -framework Security

all: $(APP)

$(FW)/Versions/A/QuartzCore:
	bash $(CAHOST)/rebundle.sh

$(APP): GPUReplay.mm $(CAHOST)/CAScene.mm $(CAHOST)/CAScene.h $(WKLIBS) Info.plist $(FW)/Versions/A/QuartzCore
	rm -rf $(APP)
	mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Frameworks
	cp Info.plist $(APP)/Contents/Info.plist
	printf 'APPL????' > $(APP)/Contents/PkgInfo
	cp -R $(FW) $(APP)/Contents/Frameworks/
	$(CLANGXX) $(CXXFLAGS) -o $(APP)/Contents/MacOS/GPUReplay \
	  GPUReplay.mm $(CAHOST)/CAScene.mm $(LDFLAGS)

clean:
	rm -rf $(APP)

.PHONY: all clean
