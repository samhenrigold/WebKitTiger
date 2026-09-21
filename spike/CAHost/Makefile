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
