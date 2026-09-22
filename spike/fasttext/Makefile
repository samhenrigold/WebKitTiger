WKT := /Users/shg/Developer/WebKitTiger
S64 := $(WKT)/toolchain/sysroot-x86_64/usr
CC32 := $(WKT)/toolchain/bin/tiger-clang
CC64 := $(WKT)/toolchain/bin/tiger-clang64
FW := -F$(WKT)/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks

all: ctref32 ctref32int ctref32zoom fast64

ctref32: ctref32.c sample.h
	$(CC32) -O1 -g -Wall ctref32.c -o $@ -ltigercompat \
	  -F$(WKT)/compat/sdk-overlay $(FW) \
	  -framework CoreFoundation -framework CoreServices -framework ApplicationServices

# Integer baselines (17 + 30 i): the reference for run-box.sh, since WebKit rounds
# text baselines to whole pixels and pagedriver cannot draw the fractional sample.
ctref32int: ctref32.c sample.h
	$(CC32) -O1 -g -Wall -DFT_LINE0=17.0 -DFT_LEADING=30.0 ctref32.c -o $@ -ltigercompat \
	  -F$(WKT)/compat/sdk-overlay $(FW) \
	  -framework CoreFoundation -framework CoreServices -framework ApplicationServices

# Integer baselines under a 1.125 page scale: 16 px faces land on 18 device px, where Quartz fits Helvetica.
ctref32zoom: ctref32.c sample.h
	$(CC32) -O1 -g -Wall -DFT_LINE0=17.0 -DFT_LEADING=30.0 -DFT_SCALE=1.125 ctref32.c -o $@ -ltigercompat \
	  -F$(WKT)/compat/sdk-overlay $(FW) \
	  -framework CoreFoundation -framework CoreServices -framework ApplicationServices

fast64: fast64.c sample.h
	$(CC64) -O2 -g -Wall fast64.c -o $@ \
	  -I$(S64)/include/cairo -I$(S64)/include/freetype2 -I$(S64)/include \
	  -L$(S64)/lib -lcairo -lpixman-1 -lfontconfig -lfreetype -lexpat -lpng16 -lz -lm -ltigercompat $(S64)/lib/libclang_rt.builtins-x86_64.a

clean:
	rm -f ctref32 ctref32int fast64
.PHONY: all clean
