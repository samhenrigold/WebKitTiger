# i386 audio helper for the x86_64 web process. `make -C spike/media` builds
# build/tigeraudio32; the media stage script copies it next to TigerWebProcess.
WKT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
OUT := $(WKT)/build/tigeraudio32
$(OUT): tigeraudio32.c tigeraudioring.h
	$(WKT)/toolchain/bin/tiger-clang -O2 -Wall -o $@ tigeraudio32.c -framework CoreAudio -framework AudioUnit -framework AudioToolbox -framework Carbon
all: $(OUT)
clean:
	rm -f $(OUT)
.DEFAULT_GOAL := all
