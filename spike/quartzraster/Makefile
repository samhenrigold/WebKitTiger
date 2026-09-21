# TIGER quartzraster: probes for how CoreGraphics rasterises glyphs, and a model
# of it. See logs/quartz-raster.md.
#
#   make
#   scp pathprobe32 ctglyph32 ctgrid32 model64 tiger:~/qraster/
#   ssh tiger 'cd ~/qraster && cp ~/fasttext/tiger-fonts.json . && ./pathprobe32 | less'
#   ssh tiger 'cd ~/qraster && ./ctglyph32 glyphs.bin && ./model64 tiger-fonts.json glyphs.bin'
#   ssh tiger 'cd ~/fasttext && ./model64 tiger-fonts.json page . ref-smooth.bin'
WKT := /Users/shg/Developer/WebKitTiger
S64 := $(WKT)/toolchain/sysroot-x86_64/usr
CC32 := $(WKT)/toolchain/bin/tiger-clang
CC64 := $(WKT)/toolchain/bin/tiger-clang64
FW := -F$(WKT)/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks

all: pathprobe32 ctglyph32 ctgrid32 model64

pathprobe32: pathprobe32.c
	$(CC32) -O1 -Wall $< -o $@ -framework ApplicationServices -framework CoreFoundation

ctglyph32 ctgrid32: %: %.c
	$(CC32) -O1 -w $< -o $@ -ltigercompat -F$(WKT)/compat/sdk-overlay $(FW) \
	  -framework CoreFoundation -framework CoreServices -framework ApplicationServices

model64: model64.c
	$(CC64) -O2 -w $< -o $@ -I$(S64)/include/freetype2 -I$(S64)/include \
	  -L$(S64)/lib -lfreetype -lpng16 -lz -lm -ltigercompat \
	  $(S64)/lib/libclang_rt.builtins-x86_64.a

clean:
	rm -f pathprobe32 ctglyph32 ctgrid32 model64 *.bin
.PHONY: all clean
