ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../..)
CLANG   := $(ROOT)/toolchain/bin/tiger-clang
APP     := $(ROOT)/build/TigerBrowser.app

CFLAGS  := -fobjc-runtime=macosx-fragile-10.4 -O0 -g -Wall
LDFLAGS := -framework Cocoa -framework WebKit

all: $(APP)

$(APP): TigerBrowser.m Info.plist
	rm -rf $(APP)
	mkdir -p $(APP)/Contents/MacOS
	cp Info.plist $(APP)/Contents/Info.plist
	printf 'APPL????' > $(APP)/Contents/PkgInfo
	$(CLANG) $(CFLAGS) -o $(APP)/Contents/MacOS/TigerBrowser TigerBrowser.m $(LDFLAGS)

clean:
	rm -rf $(APP)

.PHONY: all clean
