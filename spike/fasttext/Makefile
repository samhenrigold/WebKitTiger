WKT := /Users/shg/Developer/WebKitTiger
S64 := $(WKT)/toolchain/sysroot-x86_64/usr
CC32 := $(WKT)/toolchain/bin/tiger-clang
CC64 := $(WKT)/toolchain/bin/tiger-clang64
FW := -F$(WKT)/sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks

all: ctref32 fast64

ctref32: ctref32.c sample.h
	$(CC32) -O1 -g -Wall ctref32.c -o $@ -ltigercompat \
	  -F$(WKT)/compat/sdk-overlay $(FW) \
	  -framework CoreFoundation -framework CoreServices -framework ApplicationServices

fast64: fast64.c sample.h
	$(CC64) -O2 -g -Wall fast64.c -o $@ \
	  -I$(S64)/include/cairo -I$(S64)/include/freetype2 -I$(S64)/include \
	  -L$(S64)/lib -lcairo -lpixman-1 -lfontconfig -lfreetype -lexpat -lpng16 -lz -lm -ltigercompat $(S64)/lib/libclang_rt.builtins-x86_64.a

clean:
	rm -f ctref32 fast64
.PHONY: all clean
