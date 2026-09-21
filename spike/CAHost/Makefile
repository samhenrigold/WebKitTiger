ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../..)
CLANG   := $(ROOT)/toolchain/bin/tiger-clang
APP     := $(ROOT)/build/CAHost.app
FW      := $(ROOT)/spike/CAHost/Frameworks/QuartzCore.framework

CFLAGS  := -fobjc-runtime=macosx-fragile-10.4 -O2 -Wall \
           -F$(ROOT)/spike/CAHost/Frameworks
LDFLAGS := -framework Cocoa -framework OpenGL -framework ApplicationServices \
           -F$(ROOT)/spike/CAHost/Frameworks -framework QuartzCore

all: $(APP)

$(FW)/Versions/A/QuartzCore:
	bash $(ROOT)/spike/CAHost/rebundle.sh

$(APP): CAHost.m Info.plist $(FW)/Versions/A/QuartzCore
	rm -rf $(APP)
	mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Frameworks
	cp Info.plist $(APP)/Contents/Info.plist
	printf 'APPL????' > $(APP)/Contents/PkgInfo
	cp -R $(FW) $(APP)/Contents/Frameworks/
	$(CLANG) $(CFLAGS) -o $(APP)/Contents/MacOS/CAHost CAHost.m $(LDFLAGS)
	$(ROOT)/toolchain/bin/tiger-otool -L $(APP)/Contents/MacOS/CAHost | grep -i quartz

clean:
	rm -rf $(APP)

.PHONY: all clean

# --- CAVideo: QuickTime OpenGL visual context under the CA host -------------
VAPP := $(ROOT)/build/CAVideo.app
# Link order matters, and -framework does not preserve it (ld64 hoists those
# after explicit inputs) — pass both QuartzCores by path. The Apple TV QuartzCore
# duplicates 207 CI* class names from Tiger's own, which QTKit weak-links and
# QuickTime needs; its CIFilter is a stripped Apple TV build with no
# +filterWithName:. Tiger's dyld loads the main executable's direct dependencies
# in load-command order and its libobjc keeps the FIRST registration of a
# duplicate class name, so Tiger's QuartzCore has to come first. -u keeps ld from
# dead-stripping a dylib we reference no symbol from.
VLD  := -Wl,-u,_CICreateCGImageRefFromYCbCrData \
        $(ROOT)/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/QuartzCore.framework/QuartzCore \
        $(ROOT)/spike/CAHost/Frameworks/QuartzCore.framework/Versions/A/QuartzCore \
        -framework Cocoa -framework OpenGL -framework QTKit -framework QuickTime \
        -framework CoreVideo -framework ApplicationServices

CAVideo: $(VAPP)

$(VAPP): CAVideo.m Info-CAVideo.plist $(FW)/Versions/A/QuartzCore
	rm -rf $(VAPP)
	mkdir -p $(VAPP)/Contents/MacOS $(VAPP)/Contents/Frameworks
	cp Info-CAVideo.plist $(VAPP)/Contents/Info.plist
	printf 'APPL????' > $(VAPP)/Contents/PkgInfo
	cp -R $(FW) $(VAPP)/Contents/Frameworks/
	$(CLANG) $(CFLAGS) -o $(VAPP)/Contents/MacOS/CAVideo CAVideo.m $(VLD)

.PHONY: CAVideo

# Same program with no Core Animation at all: isolates whether the Apple TV
# QuartzCore's duplicate CIFilter is what breaks QuickTime's visual context.
CAVideoNoCA: CAVideo.m
	$(CLANG) $(CFLAGS) -DCAVIDEO_NO_CA -o $(ROOT)/build/CAVideoNoCA CAVideo.m \
	  -framework Cocoa -framework OpenGL -framework QTKit -framework QuickTime \
	  -framework CoreVideo -framework ApplicationServices
.PHONY: CAVideoNoCA

# --- CAWidgets: real Aqua controls over the CA-hosted page -------------------
WAPP := $(ROOT)/build/CAWidgets.app

CAWidgets: $(WAPP)

$(WAPP): CAWidgets.m Info-CAWidgets.plist $(FW)/Versions/A/QuartzCore
	rm -rf $(WAPP)
	mkdir -p $(WAPP)/Contents/MacOS $(WAPP)/Contents/Frameworks
	cp Info-CAWidgets.plist $(WAPP)/Contents/Info.plist
	printf 'APPL????' > $(WAPP)/Contents/PkgInfo
	cp -R $(FW) $(WAPP)/Contents/Frameworks/
	$(CLANG) $(CFLAGS) -o $(WAPP)/Contents/MacOS/CAWidgets CAWidgets.m $(LDFLAGS)

.PHONY: CAWidgets

# --- CASceneTest: the WC-shaped layer delta applier ------------------------
SAPP   := $(ROOT)/build/CASceneTest.app
CLANGXX := $(ROOT)/toolchain/bin/tiger-clang++
CXXFLAGS := -fobjc-runtime=macosx-fragile-10.4 -O2 -Wall -std=c++11 \
            -nostdinc++ -isystem $(ROOT)/toolchain/sysroot-i386/usr/include/c++/v1 \
            -stdlib=libc++ -F$(ROOT)/spike/CAHost/Frameworks
SLD := -framework Cocoa -framework OpenGL -framework ApplicationServices \
       -F$(ROOT)/spike/CAHost/Frameworks -framework QuartzCore \
       -lc++ -lc++abi -lunwind -ltigercompat \
       $(BUILTINS)

BUILTINS := $(wildcard $(ROOT)/build/builtins-i386/libclang_rt.builtins-i386.a)

CASceneTest: $(SAPP)

$(SAPP): CASceneTest.mm CAScene.mm CAScene.h Info-CASceneTest.plist $(FW)/Versions/A/QuartzCore
	rm -rf $(SAPP)
	mkdir -p $(SAPP)/Contents/MacOS $(SAPP)/Contents/Frameworks
	cp Info-CASceneTest.plist $(SAPP)/Contents/Info.plist
	printf 'APPL????' > $(SAPP)/Contents/PkgInfo
	cp -R $(FW) $(SAPP)/Contents/Frameworks/
	$(CLANGXX) $(CXXFLAGS) -o $(SAPP)/Contents/MacOS/CASceneTest \
	  CASceneTest.mm CAScene.mm $(SLD)

.PHONY: CASceneTest
