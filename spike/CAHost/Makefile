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
