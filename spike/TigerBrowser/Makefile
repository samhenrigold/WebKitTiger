ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../..)
CLANG   := $(ROOT)/toolchain/bin/tiger-clang
APP     := $(ROOT)/build/TigerBrowser.app
# Reuse spike/CAHost's rebundled, decollided Apple TV QuartzCore (private
# install name @executable_path/../Frameworks/QuartzCore.framework/...)
# instead of duplicating rebundle.sh/decollide.py here.
FW      := $(ROOT)/spike/CAHost/Frameworks/QuartzCore.framework

CFLAGS  := -fobjc-runtime=macosx-fragile-10.4 -O0 -g -Wall \
           -F$(ROOT)/spike/CAHost/Frameworks
LDFLAGS := -framework Cocoa -framework OpenGL -framework ApplicationServices \
           -F$(ROOT)/spike/CAHost/Frameworks -framework QuartzCore

all: $(APP)

$(FW)/Versions/A/QuartzCore:
	bash $(ROOT)/spike/CAHost/rebundle.sh

$(APP): TigerBrowser.m Info.plist $(FW)/Versions/A/QuartzCore
	rm -rf $(APP)
	mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Frameworks
	cp Info.plist $(APP)/Contents/Info.plist
	printf 'APPL????' > $(APP)/Contents/PkgInfo
	cp -R $(FW) $(APP)/Contents/Frameworks/
	$(CLANG) $(CFLAGS) -o $(APP)/Contents/MacOS/TigerBrowser TigerBrowser.m $(LDFLAGS)
	$(ROOT)/toolchain/bin/tiger-otool -L $(APP)/Contents/MacOS/TigerBrowser | grep -i quartz

clean:
	rm -rf $(APP)

.PHONY: all clean
